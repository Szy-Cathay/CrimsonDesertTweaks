#include "config.h"
#include "hooks/blood_decals_timing.h"
#include "logger.h"
#include "scanner.h"

#include <safetyhook.hpp>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

using blood_decals::IsExpectedBloodDecalTiming;
using blood_decals::EffectiveScanIntervalMs;
using blood_decals::kDefaultBloodLifeMs;
using blood_decals::kDefaultBloodFadeMs;
using blood_decals::kMaxLifetimeMs;
using blood_decals::kReferenceExtendedLifeMs;

struct ModuleBounds {
    uintptr_t base = 0;
    std::size_t size = 0;
};

struct PublishedSlots {
    uintptr_t manager_slot = 0;
    uintptr_t actor_array_slot = 0;
    ModuleBounds module;
};

struct RttiCompleteObjectLocator {
    std::uint32_t signature = 0;
    std::uint32_t offset = 0;
    std::uint32_t cd_offset = 0;
    std::int32_t type_descriptor_rva = 0;
    std::int32_t class_descriptor_rva = 0;
    std::int32_t self_rva = 0;
};

struct TickStats {
    std::uint32_t roots = 0;
    std::uint32_t vector_views = 0;
    std::uint32_t candidates = 0;
    std::uint32_t patched = 0;
    std::uint32_t already_extended = 0;
    std::uint32_t skipped_unexpected = 0;
    std::uint32_t read_failed = 0;
    std::uint32_t write_failed = 0;
    std::uint32_t diag_rtti_logged = 0;
    std::uint32_t diag_particle_logged = 0;
};

constexpr uintptr_t kMinimumPointerAddress = 0x10000;
constexpr uintptr_t kMaximumUserPointer = 0x0000800000000000ULL;
constexpr std::size_t kMaxVectorViewsPerTick = 64;
constexpr std::size_t kMaxSeenCandidates = 4096;
constexpr std::size_t kMaxSkippedRttiSamples = 32;
constexpr std::size_t kMaxParticleDiagSamples = 32;
constexpr std::size_t kMaxRuntimeParticleDiagSamples = 64;
constexpr uintptr_t kParticleDiagBytesOffset = 0x118;
constexpr std::size_t kParticleDiagBytesSize = 0x20;
constexpr uintptr_t kParticleGpuDecalFlagsOffset = 0x119;
constexpr uintptr_t kParticleRenderDecalOnTerrainOffset = 0x12B;
constexpr uintptr_t kParticleIsBloodOffset = 0x135;
constexpr uintptr_t kParticleRuntimeConfigOffset = 0xD0;
constexpr uintptr_t kParticleRuntimeUsesLifetimeOffset = 0x20B;
constexpr uintptr_t kParticleRuntimeLifeMinOffset = 0x2D8;
constexpr uintptr_t kParticleRuntimeLifeMaxOffset = 0x2DC;
constexpr uintptr_t kHitMaterialCategoryOffset = 0x11A;
constexpr std::uint8_t kGroundBloodDecalCategory = 8;
constexpr uintptr_t kHitMaterialEffectEntryLifeMsOffset = 0x40;
constexpr uintptr_t kGroundBloodParticleLifeSecondsOffset = 0x5C;
constexpr uintptr_t kGroundBloodEntryParamStackOffset = 0x38;
constexpr uintptr_t kManagerPrimaryOffset = 0x58;
constexpr uintptr_t kManagerPrimaryChildOffset = 0x08;
constexpr uintptr_t kEffectRootOffset = 0x38;
constexpr uintptr_t kEffectOwnerOffset = 0x68;
constexpr uintptr_t kEffectContainerOffset = 0x60;
constexpr uintptr_t kManagerActorArrayOffset = 0x130;
constexpr uintptr_t kManagerActorArrayCapacityOffset = 0x13C;
constexpr uintptr_t kEffectListAOffset = 0xA0;
constexpr uintptr_t kEffectListBOffset = 0x100;
constexpr uintptr_t kEffectListCountOffset = 0x08;
constexpr std::ptrdiff_t kUserActorHealWindow = 0x200;
constexpr std::uint32_t kActorArrayCapacityMin = 1;
constexpr std::uint32_t kActorArrayCapacityFallback = 1024;
constexpr std::uint32_t kActorArrayCapacityMax = 8192;
constexpr std::size_t kMinEffectListScanBytes = 0x100;
constexpr std::size_t kMaxEffectListScanBytes = 0x1000;

std::atomic<bool> g_worker_running{false};
std::atomic<uintptr_t> g_observed_manager{0};
std::atomic<std::uint64_t> g_total_patched{0};
std::atomic<std::uint64_t> g_publish_hits{0};
std::atomic<std::uint64_t> g_ticks{0};
std::atomic<std::ptrdiff_t> g_user_actor_offset{kManagerPrimaryOffset};
HANDLE g_worker_thread = nullptr;
HANDLE g_stop_event = nullptr;
PublishedSlots g_slots{};
SafetyHookMid g_publish_store_hook{};
SafetyHookMid g_blood_particle_sim_check_hook{};
SafetyHookMid g_blood_particle_check_hook{};
SafetyHookMid g_terrain_particle_check_hook{};
SafetyHookMid g_particle_lifetime_update_hook{};
SafetyHookMid g_ground_blood_entry_build_hook{};

enum class RuntimeParticleSource : std::uint8_t {
    BloodSimulationCheck,
    BloodCheck,
    TerrainCheck,
};

struct SkippedRttiSample {
    char name[128]{};
    char reason[32]{};
    std::uint32_t count = 0;
};

struct ParticleDiagSample {
    uintptr_t ptr = 0;
    std::uint8_t gpu_decal_flags = 0;
    std::uint8_t render_terrain = 0;
    std::uint8_t is_blood = 0;
    std::uint32_t count = 0;
};

struct RuntimeParticleDiagSample {
    uintptr_t ptr = 0;
    RuntimeParticleSource source = RuntimeParticleSource::BloodCheck;
    std::uint32_t count = 0;
};

std::vector<SkippedRttiSample> g_skipped_rtti_samples;
std::vector<ParticleDiagSample> g_particle_diag_samples;
std::vector<RuntimeParticleDiagSample> g_runtime_particle_diag_samples;
std::mutex g_runtime_particle_diag_mutex;
std::atomic<std::uint64_t> g_runtime_particle_hits{0};
std::atomic<std::uint64_t> g_particle_lifetime_hits{0};
std::atomic<std::uint64_t> g_particle_lifetime_diag_logs{0};
std::atomic<std::uint64_t> g_particle_lifetime_patches{0};
std::atomic<std::uint64_t> g_ground_blood_entry_hits{0};
std::atomic<std::uint64_t> g_ground_blood_entry_patches{0};
std::atomic<std::uint64_t> g_ground_blood_entry_read_failed{0};
std::atomic<std::uint64_t> g_ground_blood_entry_write_failed{0};
std::atomic<std::uint64_t> g_ground_blood_particle_param_patches{0};
std::atomic<std::uint64_t> g_ground_blood_particle_param_write_failed{0};

template <typename T>
bool SafeRead(const uintptr_t address, T* const out) {
    if (out == nullptr || address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *out = *reinterpret_cast<const T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadBytes(const uintptr_t address, void* const out, const std::size_t size) {
    if (out == nullptr || size == 0 || address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadCString(const uintptr_t address, char* const out, const std::size_t size) {
    if (out == nullptr || size == 0 || address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        for (std::size_t i = 0; i + 1 < size; ++i) {
            const char value = *reinterpret_cast<const char*>(address + i);
            out[i] = value;
            if (value == '\0') {
                return true;
            }
        }
        out[size - 1] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
        return false;
    }
}

bool SafeWriteDword(const uintptr_t address, const std::uint32_t value) {
    if (address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *reinterpret_cast<std::uint32_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeWriteFloat(const uintptr_t address, const float value) {
    if (address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *reinterpret_cast<float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsLikelyPointer(const uintptr_t value) {
    return value >= kMinimumPointerAddress && value < kMaximumUserPointer;
}

bool IsBoolByte(const std::uint8_t value) {
    return value == 0 || value == 1;
}

bool ContainsAsciiInsensitive(const char* const text, const char* const pattern);

bool IsInMainModule(const uintptr_t value) {
    const ModuleBounds module = g_slots.module;
    return module.base != 0 && module.size != 0 &&
           value >= module.base && value < module.base + module.size;
}

bool IsLikelyRuntimeHeapPointer(const uintptr_t value) {
    return IsLikelyPointer(value) &&
           !IsInMainModule(value) &&
           value >= 0x0000010000000000ULL &&
           (value % alignof(uintptr_t)) == 0;
}

bool ReadRttiTypeName(const uintptr_t object, char* const out, const std::size_t size) {
    if (out == nullptr || size == 0 || !IsLikelyPointer(object)) {
        return false;
    }
    out[0] = '\0';

    uintptr_t vtable = 0;
    uintptr_t locator_address = 0;
    if (!SafeRead(object, &vtable) ||
        !IsInMainModule(vtable) ||
        !SafeRead(vtable - sizeof(uintptr_t), &locator_address) ||
        !IsInMainModule(locator_address)) {
        return false;
    }

    RttiCompleteObjectLocator locator{};
    if (!SafeRead(locator_address, &locator) ||
        locator.signature != 1 ||
        locator.type_descriptor_rva <= 0) {
        return false;
    }

    const uintptr_t type_descriptor =
        g_slots.module.base + static_cast<std::uint32_t>(locator.type_descriptor_rva);
    if (!IsInMainModule(type_descriptor)) {
        return false;
    }

    return SafeReadCString(type_descriptor + 0x10, out, size);
}

bool ObjectRttiContains(const uintptr_t object, const char* const needle) {
    char name[256]{};
    return ReadRttiTypeName(object, name, sizeof(name)) &&
           ContainsAsciiInsensitive(name, needle);
}

bool IsClientUserActor(const uintptr_t object) {
    return ObjectRttiContains(object, "ClientUserActor@pa");
}

void BloodDecalsPublishStoreCallback(SafetyHookContext& ctx) {
    const uintptr_t manager = ctx.rdi;
    if (!IsLikelyPointer(manager)) {
        return;
    }

    g_observed_manager.store(manager, std::memory_order_release);
    const auto hits = g_publish_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    if (hits <= 3) {
        Log("blood-decals: publish capture hit=%llu manager=0x%p",
            static_cast<unsigned long long>(hits),
            reinterpret_cast<void*>(manager));
    }
}

ModuleBounds GetMainModuleBounds() {
    const auto module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        return {};
    }

    const auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return {};
    }

    const auto nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return {};
    }

    return {
        reinterpret_cast<uintptr_t>(module),
        static_cast<std::size_t>(nt_headers->OptionalHeader.SizeOfImage),
    };
}

bool InstructionMatches(const uintptr_t instruction,
                        const std::uint8_t byte0,
                        const std::uint8_t byte1,
                        const std::uint8_t byte2) {
    std::uint8_t bytes[3]{};
    return SafeReadBytes(instruction, bytes, sizeof(bytes)) &&
           bytes[0] == byte0 && bytes[1] == byte1 && bytes[2] == byte2;
}

bool ResolveRipRelativeTarget(const uintptr_t instruction,
                              const std::uint8_t byte0,
                              const std::uint8_t byte1,
                              const std::uint8_t byte2,
                              uintptr_t* const target) {
    if (target == nullptr ||
        !InstructionMatches(instruction, byte0, byte1, byte2)) {
        return false;
    }

    std::int32_t displacement = 0;
    if (!SafeRead(instruction + 3, &displacement)) {
        return false;
    }

    *target = instruction + 7 + displacement;
    return IsLikelyPointer(*target);
}

bool WasSeen(const std::vector<uintptr_t>& seen, const uintptr_t candidate) {
    return std::find(seen.begin(), seen.end(), candidate) != seen.end();
}

char ToLowerAscii(const char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }

    return value;
}

bool ContainsAsciiInsensitive(const char* const text, const char* const pattern) {
    if (text == nullptr || pattern == nullptr || pattern[0] == '\0') {
        return false;
    }

    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const char* haystack = cursor;
        const char* needle = pattern;
        while (*haystack != '\0' && *needle != '\0' &&
               ToLowerAscii(*haystack) == ToLowerAscii(*needle)) {
            ++haystack;
            ++needle;
        }

        if (*needle == '\0') {
            return true;
        }
    }

    return false;
}

bool HasGameSkinnedDecalName(const char* const name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }

    return ContainsAsciiInsensitive(name, "GameSkinnedDecalElement@pa") ||
           ContainsAsciiInsensitive(name, "SkinnedDecalElement@pa") ||
           (ContainsAsciiInsensitive(name, "Skinned") &&
            ContainsAsciiInsensitive(name, "Decal"));
}

bool ShouldLogSkippedRttiName(const char* const name) {
    return ContainsAsciiInsensitive(name, "Decal") ||
           ContainsAsciiInsensitive(name, "Blood");
}

bool SameAsciiString(const char* const lhs, const char* const rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }

    return std::strcmp(lhs, rhs) == 0;
}

void CopyBoundedString(const char* const source, char* const target, const std::size_t target_size) {
    if (source == nullptr || target == nullptr || target_size == 0) {
        return;
    }

    const std::size_t length = std::min(std::strlen(source), target_size - 1);
    std::memcpy(target, source, length);
    target[length] = '\0';
}

void LogSkippedRttiCandidate(const uintptr_t candidate,
                             const char* const name,
                             const char* const reason,
                             const bool timing_read,
                             const std::uint32_t life78,
                             const std::uint32_t fade80,
                             TickStats* const stats) {
    if (stats == nullptr ||
        name == nullptr ||
        reason == nullptr ||
        !ShouldLogSkippedRttiName(name)) {
        return;
    }

    for (auto& sample : g_skipped_rtti_samples) {
        if (SameAsciiString(sample.name, name) && SameAsciiString(sample.reason, reason)) {
            ++sample.count;
            return;
        }
    }

    if (g_skipped_rtti_samples.size() >= kMaxSkippedRttiSamples) {
        return;
    }

    SkippedRttiSample sample{};
    CopyBoundedString(name, sample.name, sizeof(sample.name));
    CopyBoundedString(reason, sample.reason, sizeof(sample.reason));
    sample.count = 1;
    g_skipped_rtti_samples.push_back(sample);
    ++stats->diag_rtti_logged;

    if (timing_read) {
        Log("blood-decals: DIAG skipped-rtti reason=%s name=\"%s\" ptr=0x%p life78Ms=%lu fade80Ms=%lu",
            reason,
            name,
            reinterpret_cast<void*>(candidate),
            static_cast<unsigned long>(life78),
            static_cast<unsigned long>(fade80));
    } else {
        Log("blood-decals: DIAG skipped-rtti reason=%s name=\"%s\" ptr=0x%p timing=unreadable",
            reason,
            name,
            reinterpret_cast<void*>(candidate));
    }
}

void TryLogParticleDecalCandidate(const uintptr_t candidate, TickStats* const stats) {
    if (stats == nullptr || !IsLikelyRuntimeHeapPointer(candidate)) {
        return;
    }

    std::uint8_t gpu_decal_flags = 0;
    std::uint8_t render_terrain = 0;
    std::uint8_t is_blood = 0;
    std::uint8_t diag_bytes[kParticleDiagBytesSize]{};
    if (!SafeRead(candidate + kParticleGpuDecalFlagsOffset, &gpu_decal_flags) ||
        !SafeRead(candidate + kParticleRenderDecalOnTerrainOffset, &render_terrain) ||
        !SafeRead(candidate + kParticleIsBloodOffset, &is_blood) ||
        !SafeReadBytes(candidate + kParticleDiagBytesOffset, diag_bytes, sizeof(diag_bytes))) {
        return;
    }

    if (!IsBoolByte(render_terrain) || !IsBoolByte(is_blood)) {
        return;
    }

    if (is_blood == 0 && render_terrain == 0) {
        return;
    }

    for (auto& sample : g_particle_diag_samples) {
        if (sample.ptr == candidate) {
            ++sample.count;
            return;
        }
    }

    if (g_particle_diag_samples.size() >= kMaxParticleDiagSamples) {
        return;
    }

    ParticleDiagSample sample{};
    sample.ptr = candidate;
    sample.gpu_decal_flags = gpu_decal_flags;
    sample.render_terrain = render_terrain;
    sample.is_blood = is_blood;
    sample.count = 1;
    g_particle_diag_samples.push_back(sample);
    ++stats->diag_particle_logged;

    Log("blood-decals: DIAG particle-strict-candidate ptr=0x%p gpuDecal119=0x%02X terrain12B=%u blood135=%u bytes118=\"%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\"",
        reinterpret_cast<void*>(candidate),
        static_cast<unsigned int>(gpu_decal_flags),
        static_cast<unsigned int>(render_terrain),
        static_cast<unsigned int>(is_blood),
        static_cast<unsigned int>(diag_bytes[0]),
        static_cast<unsigned int>(diag_bytes[1]),
        static_cast<unsigned int>(diag_bytes[2]),
        static_cast<unsigned int>(diag_bytes[3]),
        static_cast<unsigned int>(diag_bytes[4]),
        static_cast<unsigned int>(diag_bytes[5]),
        static_cast<unsigned int>(diag_bytes[6]),
        static_cast<unsigned int>(diag_bytes[7]),
        static_cast<unsigned int>(diag_bytes[8]),
        static_cast<unsigned int>(diag_bytes[9]),
        static_cast<unsigned int>(diag_bytes[10]),
        static_cast<unsigned int>(diag_bytes[11]),
        static_cast<unsigned int>(diag_bytes[12]),
        static_cast<unsigned int>(diag_bytes[13]),
        static_cast<unsigned int>(diag_bytes[14]),
        static_cast<unsigned int>(diag_bytes[15]),
        static_cast<unsigned int>(diag_bytes[16]),
        static_cast<unsigned int>(diag_bytes[17]),
        static_cast<unsigned int>(diag_bytes[18]),
        static_cast<unsigned int>(diag_bytes[19]),
        static_cast<unsigned int>(diag_bytes[20]),
        static_cast<unsigned int>(diag_bytes[21]),
        static_cast<unsigned int>(diag_bytes[22]),
        static_cast<unsigned int>(diag_bytes[23]),
        static_cast<unsigned int>(diag_bytes[24]),
        static_cast<unsigned int>(diag_bytes[25]),
        static_cast<unsigned int>(diag_bytes[26]),
        static_cast<unsigned int>(diag_bytes[27]),
        static_cast<unsigned int>(diag_bytes[28]),
        static_cast<unsigned int>(diag_bytes[29]),
        static_cast<unsigned int>(diag_bytes[30]),
        static_cast<unsigned int>(diag_bytes[31]));
}

const char* RuntimeParticleSourceName(const RuntimeParticleSource source) {
    switch (source) {
    case RuntimeParticleSource::BloodSimulationCheck:
        return "blood135-sim-check";
    case RuntimeParticleSource::BloodCheck:
        return "blood135-check";
    case RuntimeParticleSource::TerrainCheck:
        return "terrain12B-check";
    default:
        return "unknown";
    }
}

void TryLogRuntimeParticleCandidate(const RuntimeParticleSource source, const uintptr_t candidate) {
    g_runtime_particle_hits.fetch_add(1, std::memory_order_relaxed);

    if (!IsLikelyPointer(candidate)) {
        return;
    }

    std::uint8_t gpu_decal_flags = 0;
    std::uint8_t render_terrain = 0;
    std::uint8_t is_blood = 0;
    std::uint64_t qword68 = 0;
    std::uint64_t qword70 = 0;
    std::uint64_t qword78 = 0;
    std::uint32_t dword78 = 0;
    std::uint32_t dword80 = 0;
    std::uint8_t diag_bytes[kParticleDiagBytesSize]{};
    if (!SafeRead(candidate + kParticleGpuDecalFlagsOffset, &gpu_decal_flags) ||
        !SafeRead(candidate + kParticleRenderDecalOnTerrainOffset, &render_terrain) ||
        !SafeRead(candidate + kParticleIsBloodOffset, &is_blood) ||
        !SafeRead(candidate + 0x68, &qword68) ||
        !SafeRead(candidate + 0x70, &qword70) ||
        !SafeRead(candidate + 0x78, &qword78) ||
        !SafeRead(candidate + 0x78, &dword78) ||
        !SafeRead(candidate + 0x80, &dword80) ||
        !SafeReadBytes(candidate + kParticleDiagBytesOffset, diag_bytes, sizeof(diag_bytes))) {
        return;
    }

    if (!IsBoolByte(render_terrain) || !IsBoolByte(is_blood)) {
        return;
    }

    if (render_terrain == 0 && is_blood == 0) {
        return;
    }

    std::lock_guard lock(g_runtime_particle_diag_mutex);
    for (auto& sample : g_runtime_particle_diag_samples) {
        if (sample.ptr == candidate && sample.source == source) {
            ++sample.count;
            return;
        }
    }

    if (g_runtime_particle_diag_samples.size() >= kMaxRuntimeParticleDiagSamples) {
        return;
    }

    g_runtime_particle_diag_samples.push_back({candidate, source, 1});

    Log("blood-decals: DIAG particle-runtime source=%s ptr=0x%p q68=0x%llX q70=0x%llX q78=0x%llX d78=%lu d80=%lu gpuDecal119=0x%02X terrain12B=%u blood135=%u bytes118=\"%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\"",
        RuntimeParticleSourceName(source),
        reinterpret_cast<void*>(candidate),
        static_cast<unsigned long long>(qword68),
        static_cast<unsigned long long>(qword70),
        static_cast<unsigned long long>(qword78),
        static_cast<unsigned long>(dword78),
        static_cast<unsigned long>(dword80),
        static_cast<unsigned int>(gpu_decal_flags),
        static_cast<unsigned int>(render_terrain),
        static_cast<unsigned int>(is_blood),
        static_cast<unsigned int>(diag_bytes[0]),
        static_cast<unsigned int>(diag_bytes[1]),
        static_cast<unsigned int>(diag_bytes[2]),
        static_cast<unsigned int>(diag_bytes[3]),
        static_cast<unsigned int>(diag_bytes[4]),
        static_cast<unsigned int>(diag_bytes[5]),
        static_cast<unsigned int>(diag_bytes[6]),
        static_cast<unsigned int>(diag_bytes[7]),
        static_cast<unsigned int>(diag_bytes[8]),
        static_cast<unsigned int>(diag_bytes[9]),
        static_cast<unsigned int>(diag_bytes[10]),
        static_cast<unsigned int>(diag_bytes[11]),
        static_cast<unsigned int>(diag_bytes[12]),
        static_cast<unsigned int>(diag_bytes[13]),
        static_cast<unsigned int>(diag_bytes[14]),
        static_cast<unsigned int>(diag_bytes[15]),
        static_cast<unsigned int>(diag_bytes[16]),
        static_cast<unsigned int>(diag_bytes[17]),
        static_cast<unsigned int>(diag_bytes[18]),
        static_cast<unsigned int>(diag_bytes[19]),
        static_cast<unsigned int>(diag_bytes[20]),
        static_cast<unsigned int>(diag_bytes[21]),
        static_cast<unsigned int>(diag_bytes[22]),
        static_cast<unsigned int>(diag_bytes[23]),
        static_cast<unsigned int>(diag_bytes[24]),
        static_cast<unsigned int>(diag_bytes[25]),
        static_cast<unsigned int>(diag_bytes[26]),
        static_cast<unsigned int>(diag_bytes[27]),
        static_cast<unsigned int>(diag_bytes[28]),
        static_cast<unsigned int>(diag_bytes[29]),
        static_cast<unsigned int>(diag_bytes[30]),
        static_cast<unsigned int>(diag_bytes[31]));
}

std::size_t RuntimeParticleDiagSampleCount() {
    std::lock_guard lock(g_runtime_particle_diag_mutex);
    return g_runtime_particle_diag_samples.size();
}

void BloodParticleRuntimeCheckCallback(SafetyHookContext& ctx) {
    TryLogRuntimeParticleCandidate(RuntimeParticleSource::BloodCheck, ctx.rcx);
}

void BloodParticleSimulationCheckCallback(SafetyHookContext& ctx) {
    TryLogRuntimeParticleCandidate(RuntimeParticleSource::BloodSimulationCheck, ctx.rcx);
}

void TerrainParticleRuntimeCheckCallback(SafetyHookContext& ctx) {
    TryLogRuntimeParticleCandidate(RuntimeParticleSource::TerrainCheck, ctx.rcx);
}

std::uint32_t TargetLifetimeMs(const ModConfig& config) {
    const auto seconds = static_cast<double>(config.blood_decals.lifetime_seconds);
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return kReferenceExtendedLifeMs;
    }

    const auto milliseconds = seconds * 1000.0;
    if (milliseconds >= static_cast<double>(kMaxLifetimeMs)) {
        return kMaxLifetimeMs;
    }

    if (milliseconds <= 1.0) {
        return 1;
    }

    return static_cast<std::uint32_t>(milliseconds + 0.5);
}

float TargetLifetimeSeconds(const ModConfig& config) {
    const auto seconds = static_cast<double>(config.blood_decals.lifetime_seconds);
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return static_cast<float>(static_cast<double>(kReferenceExtendedLifeMs) / 1000.0);
    }

    const double max_seconds = static_cast<double>(kMaxLifetimeMs) / 1000.0;
    return static_cast<float>(std::min(seconds, max_seconds));
}

void LogGroundBloodEntryReadFailure(const ModConfig& config,
                                    const char* const reason,
                                    const uintptr_t material_config,
                                    const uintptr_t entry) {
    const auto failures =
        g_ground_blood_entry_read_failed.fetch_add(1, std::memory_order_relaxed) + 1;
    if (failures > 16 && !config.general.verbose) {
        return;
    }

    Log("blood-decals: DIAG ground blood entry read failed reason=%s material=0x%p entry=0x%p",
        reason != nullptr ? reason : "unknown",
        reinterpret_cast<void*>(material_config),
        reinterpret_cast<void*>(entry));
}

void TryPatchGroundBloodParticleLifeParam(const ModConfig& config,
                                          const std::uint64_t hit,
                                          const uintptr_t material_config) {
    float old_life = 0.0f;
    if (!SafeRead(material_config + kGroundBloodParticleLifeSecondsOffset, &old_life)) {
        LogGroundBloodEntryReadFailure(config, "particle-life-read-failed", material_config, 0);
        return;
    }

    const float target_life = TargetLifetimeSeconds(config);
    if (std::isfinite(old_life) && old_life >= target_life) {
        return;
    }

    if (!SafeWriteFloat(material_config + kGroundBloodParticleLifeSecondsOffset, target_life)) {
        const auto failures =
            g_ground_blood_particle_param_write_failed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (failures <= 16 || config.general.verbose) {
            Log("blood-decals: ground blood particle-life param write failed hit=%llu material=0x%p offset=0x%llX old=%.3f target=%.3f",
                static_cast<unsigned long long>(hit),
                reinterpret_cast<void*>(material_config),
                static_cast<unsigned long long>(kGroundBloodParticleLifeSecondsOffset),
                static_cast<double>(old_life),
                static_cast<double>(target_life));
        }
        return;
    }

    const auto patches =
        g_ground_blood_particle_param_patches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (patches <= 16 || config.general.verbose) {
        Log("blood-decals: ground blood particle-life param patch hit=%llu material=0x%p offset=0x%llX seconds %.3f->%.3f",
            static_cast<unsigned long long>(hit),
            reinterpret_cast<void*>(material_config),
            static_cast<unsigned long long>(kGroundBloodParticleLifeSecondsOffset),
            static_cast<double>(old_life),
            static_cast<double>(target_life));
    }
}

void GroundBloodHitMaterialEffectEntryBuildCallback(SafetyHookContext& ctx) {
    const ModConfig config = GetConfig();
    if (!ShouldInstallBloodDecalsHook(config)) {
        return;
    }

    const uintptr_t material_config = ctx.r9;
    if (!IsLikelyPointer(material_config)) {
        LogGroundBloodEntryReadFailure(config, "invalid-material", material_config, 0);
        return;
    }

    std::uint8_t category = 0;
    if (!SafeRead(material_config + kHitMaterialCategoryOffset, &category)) {
        LogGroundBloodEntryReadFailure(config, "category-read-failed", material_config, 0);
        return;
    }

    if (category != kGroundBloodDecalCategory) {
        return;
    }

    const auto hit = g_ground_blood_entry_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    TryPatchGroundBloodParticleLifeParam(config, hit, material_config);

    uintptr_t entry = 0;
    if (!SafeRead(ctx.rsp + kGroundBloodEntryParamStackOffset, &entry) ||
        !IsLikelyPointer(entry)) {
        LogGroundBloodEntryReadFailure(config, "entry-read-failed", material_config, entry);
        return;
    }

    std::uint32_t old_life = 0;
    if (!SafeRead(entry + kHitMaterialEffectEntryLifeMsOffset, &old_life)) {
        LogGroundBloodEntryReadFailure(config, "life40-read-failed", material_config, entry);
        return;
    }

    const std::uint32_t target_life = TargetLifetimeMs(config);
    if (old_life == target_life) {
        return;
    }

    if (!SafeWriteDword(entry + kHitMaterialEffectEntryLifeMsOffset, target_life)) {
        const auto failures =
            g_ground_blood_entry_write_failed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (failures <= 16 || config.general.verbose) {
            Log("blood-decals: ground blood entry write failed hit=%llu entry=0x%p material=0x%p life40Ms=%lu targetMs=%lu",
                static_cast<unsigned long long>(hit),
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(material_config),
                static_cast<unsigned long>(old_life),
                static_cast<unsigned long>(target_life));
        }
        return;
    }

    const auto patches =
        g_ground_blood_entry_patches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (patches <= 16 || config.general.verbose) {
        Log("blood-decals: ground blood entry patch hit=%llu entry=0x%p material=0x%p category=%u life40Ms %lu->%lu",
            static_cast<unsigned long long>(hit),
            reinterpret_cast<void*>(entry),
            reinterpret_cast<void*>(material_config),
            static_cast<unsigned int>(category),
            static_cast<unsigned long>(old_life),
            static_cast<unsigned long>(target_life));
    }
}

void LogParticleLifetimeDecision(const ModConfig& config,
                                 const char* const reason,
                                 const std::uint64_t hit,
                                 const uintptr_t runtime,
                                 const uintptr_t config_holder,
                                 const uintptr_t emitter_config,
                                 const std::uint8_t uses_lifetime,
                                 const std::uint8_t render_terrain,
                                 const std::uint8_t is_blood,
                                 const float life_min,
                                 const float life_max,
                                 const float target_life) {
    const auto logs = g_particle_lifetime_diag_logs.fetch_add(1, std::memory_order_relaxed) + 1;
    if (logs > 32 && !config.general.verbose) {
        return;
    }

    Log("blood-decals: DIAG particle-lifetime reason=%s hit=%llu runtime=0x%p holder=0x%p config=0x%p uses20B=%u terrain12B=%u blood135=%u lifeMin=%.3f lifeMax=%.3f target=%.3f",
        reason != nullptr ? reason : "unknown",
        static_cast<unsigned long long>(hit),
        reinterpret_cast<void*>(runtime),
        reinterpret_cast<void*>(config_holder),
        reinterpret_cast<void*>(emitter_config),
        static_cast<unsigned int>(uses_lifetime),
        static_cast<unsigned int>(render_terrain),
        static_cast<unsigned int>(is_blood),
        static_cast<double>(life_min),
        static_cast<double>(life_max),
        static_cast<double>(target_life));
}

void ParticleLifetimeUpdateCallback(SafetyHookContext& ctx) {
    const ModConfig config = GetConfig();
    if (!ShouldInstallBloodDecalsHook(config)) {
        return;
    }

    const auto hit = g_particle_lifetime_hits.fetch_add(1, std::memory_order_relaxed) + 1;
    const float target_life = TargetLifetimeSeconds(config);
    const uintptr_t runtime = ctx.rdi;
    if (!IsLikelyPointer(runtime)) {
        LogParticleLifetimeDecision(config,
                                    "invalid-runtime",
                                    hit,
                                    runtime,
                                    0,
                                    0,
                                    0,
                                    0,
                                    0,
                                    0.0f,
                                    0.0f,
                                    target_life);
        return;
    }

    std::uint8_t uses_lifetime = 0;
    const bool uses_lifetime_read =
        SafeRead(runtime + kParticleRuntimeUsesLifetimeOffset, &uses_lifetime);
    if (!uses_lifetime_read || uses_lifetime == 0) {
        LogParticleLifetimeDecision(config,
                                    uses_lifetime_read ? "uses20B-off" : "uses20B-read-failed",
                                    hit,
                                    runtime,
                                    0,
                                    0,
                                    uses_lifetime,
                                    0,
                                    0,
                                    0.0f,
                                    0.0f,
                                    target_life);
        return;
    }

    uintptr_t config_holder = 0;
    uintptr_t emitter_config = 0;
    if (!SafeRead(runtime + kParticleRuntimeConfigOffset, &config_holder) ||
        !IsLikelyPointer(config_holder) ||
        !SafeRead(config_holder, &emitter_config) ||
        !IsLikelyPointer(emitter_config)) {
        LogParticleLifetimeDecision(config,
                                    "config-read-failed",
                                    hit,
                                    runtime,
                                    config_holder,
                                    emitter_config,
                                    uses_lifetime,
                                    0,
                                    0,
                                    0.0f,
                                    0.0f,
                                    target_life);
        return;
    }

    std::uint8_t render_terrain = 0;
    std::uint8_t is_blood = 0;
    if (!SafeRead(emitter_config + kParticleRenderDecalOnTerrainOffset, &render_terrain) ||
        !SafeRead(emitter_config + kParticleIsBloodOffset, &is_blood) ||
        !IsBoolByte(render_terrain) ||
        !IsBoolByte(is_blood) ||
        (render_terrain == 0 && is_blood == 0)) {
        LogParticleLifetimeDecision(config,
                                    "flags-filter",
                                    hit,
                                    runtime,
                                    config_holder,
                                    emitter_config,
                                    uses_lifetime,
                                    render_terrain,
                                    is_blood,
                                    0.0f,
                                    0.0f,
                                    target_life);
        return;
    }

    float life_min = 0.0f;
    float life_max = 0.0f;
    if (!SafeRead(runtime + kParticleRuntimeLifeMinOffset, &life_min) ||
        !SafeRead(runtime + kParticleRuntimeLifeMaxOffset, &life_max) ||
        !std::isfinite(life_min) ||
        !std::isfinite(life_max)) {
        LogParticleLifetimeDecision(config,
                                    "life-read-failed",
                                    hit,
                                    runtime,
                                    config_holder,
                                    emitter_config,
                                    uses_lifetime,
                                    render_terrain,
                                    is_blood,
                                    life_min,
                                    life_max,
                                    target_life);
        return;
    }

    const bool has_positive_life = life_min > 0.0f || life_max > 0.0f;
    const bool already_extended = life_min >= target_life && life_max >= target_life;
    if (!has_positive_life || already_extended) {
        LogParticleLifetimeDecision(config,
                                    !has_positive_life ? "life-not-positive" : "already-extended",
                                    hit,
                                    runtime,
                                    config_holder,
                                    emitter_config,
                                    uses_lifetime,
                                    render_terrain,
                                    is_blood,
                                    life_min,
                                    life_max,
                                    target_life);
        return;
    }

    if (!SafeWriteFloat(runtime + kParticleRuntimeLifeMinOffset, target_life) ||
        !SafeWriteFloat(runtime + kParticleRuntimeLifeMaxOffset, target_life)) {
        LogParticleLifetimeDecision(config,
                                    "write-failed",
                                    hit,
                                    runtime,
                                    config_holder,
                                    emitter_config,
                                    uses_lifetime,
                                    render_terrain,
                                    is_blood,
                                    life_min,
                                    life_max,
                                    target_life);
        return;
    }

    const auto patches = g_particle_lifetime_patches.fetch_add(1, std::memory_order_relaxed) + 1;
    if (patches <= 16 || config.general.verbose) {
        Log("blood-decals: particle lifetime patch runtime=0x%p config=0x%p terrain12B=%u blood135=%u lifeMin %.3f->%.3f lifeMax %.3f->%.3f",
            reinterpret_cast<void*>(runtime),
            reinterpret_cast<void*>(emitter_config),
            static_cast<unsigned int>(render_terrain),
            static_cast<unsigned int>(is_blood),
            static_cast<double>(life_min),
            static_cast<double>(target_life),
            static_cast<double>(life_max),
            static_cast<double>(target_life));
    }
}

bool ResolveEffectContainerFromCcoia(const uintptr_t ccoia, uintptr_t* const effect_container) {
    if (effect_container == nullptr) {
        return false;
    }

    *effect_container = 0;
    if (!IsLikelyPointer(ccoia)) {
        return false;
    }

    uintptr_t effect_owner = 0;
    if (!SafeRead(ccoia + kEffectOwnerOffset, &effect_owner) ||
        !SafeRead(effect_owner + kEffectContainerOffset, effect_container)) {
        return false;
    }

    return IsLikelyPointer(*effect_container);
}

std::ptrdiff_t ResolveUserActorOffset(const uintptr_t manager, TickStats* const stats) {
    if (!IsLikelyPointer(manager)) {
        return kManagerPrimaryOffset;
    }

    const std::ptrdiff_t cached = g_user_actor_offset.load(std::memory_order_acquire);
    uintptr_t user_actor = 0;
    if (SafeRead(manager + cached, &user_actor) && IsClientUserActor(user_actor)) {
        return cached;
    }

    for (std::ptrdiff_t delta = -kUserActorHealWindow;
         delta <= kUserActorHealWindow;
         delta += static_cast<std::ptrdiff_t>(sizeof(uintptr_t))) {
        const std::ptrdiff_t offset = kManagerPrimaryOffset + delta;
        if (offset < 0) {
            continue;
        }

        if (!SafeRead(manager + static_cast<uintptr_t>(offset), &user_actor)) {
            if (stats != nullptr) {
                ++stats->read_failed;
            }
            continue;
        }

        if (IsClientUserActor(user_actor)) {
            g_user_actor_offset.store(offset, std::memory_order_release);
            Log("blood-decals: manager->userActor offset resolved +0x%llX",
                static_cast<unsigned long long>(offset));
            return offset;
        }
    }

    return cached;
}

void TryPatchCandidate(const uintptr_t candidate,
                       const ModConfig& config,
                       std::vector<uintptr_t>* const seen,
                       TickStats* const stats) {
    if (seen == nullptr || stats == nullptr) {
        return;
    }

    ++stats->candidates;
    if (!IsLikelyPointer(candidate) || WasSeen(*seen, candidate)) {
        return;
    }

    if (seen->size() >= kMaxSeenCandidates) {
        ++stats->skipped_unexpected;
        return;
    }
    seen->push_back(candidate);

    TryLogParticleDecalCandidate(candidate, stats);

    char name[256]{};
    if (!ReadRttiTypeName(candidate, name, sizeof(name))) {
        ++stats->skipped_unexpected;
        return;
    }

    std::uint32_t life78 = 0;
    std::uint32_t fade80 = 0;

    if (!HasGameSkinnedDecalName(name)) {
        const bool timing_read =
            SafeRead(candidate + 0x78, &life78) &&
            SafeRead(candidate + 0x80, &fade80);
        LogSkippedRttiCandidate(candidate,
                                name,
                                "non-skinned",
                                timing_read,
                                life78,
                                fade80,
                                stats);
        ++stats->skipped_unexpected;
        return;
    }

    if (!SafeRead(candidate + 0x78, &life78) ||
        !SafeRead(candidate + 0x80, &fade80)) {
        ++stats->read_failed;
        return;
    }

    const std::uint32_t target_life = TargetLifetimeMs(config);
    if (!IsExpectedBloodDecalTiming(life78, fade80, target_life)) {
        LogSkippedRttiCandidate(candidate,
                                name,
                                "timing",
                                true,
                                life78,
                                fade80,
                                stats);
        ++stats->skipped_unexpected;
        return;
    }

    if (life78 >= target_life) {
        ++stats->already_extended;
        return;
    }

    if (!SafeWriteDword(candidate + 0x78, target_life)) {
        ++stats->write_failed;
        if (config.general.verbose) {
            Log("blood-decals: write failed addr=0x%p valueMs=%lu",
                reinterpret_cast<void*>(candidate + 0x78),
                static_cast<unsigned long>(target_life));
        }
        return;
    }

    ++stats->patched;
    if (config.general.verbose) {
        Log("blood-decals: patch ptr=0x%p life78Ms %lu->%lu fade80Ms %lu unchanged",
            reinterpret_cast<void*>(candidate),
            static_cast<unsigned long>(life78),
            static_cast<unsigned long>(target_life),
            static_cast<unsigned long>(fade80));
    }
}

bool ResolveEffectContainer(const uintptr_t manager,
                            uintptr_t* const effect_container,
                            TickStats* const stats) {
    if (effect_container == nullptr || stats == nullptr) {
        return false;
    }

    *effect_container = 0;
    if (!IsLikelyPointer(manager)) {
        return false;
    }

    uintptr_t primary = 0;
    uintptr_t child = 0;
    uintptr_t ccoia = 0;
    const std::ptrdiff_t user_actor_offset = ResolveUserActorOffset(manager, stats);
    if (!SafeRead(manager + static_cast<uintptr_t>(user_actor_offset), &primary) ||
        !SafeRead(primary + kManagerPrimaryChildOffset, &child) ||
        !SafeRead(child + kEffectRootOffset, &ccoia) ||
        !ResolveEffectContainerFromCcoia(ccoia, effect_container)) {
        ++stats->read_failed;
        return false;
    }

    return IsLikelyPointer(*effect_container);
}

void ScanEffectListEntry(const uintptr_t entry,
                         const ModConfig& config,
                         std::vector<uintptr_t>* const seen,
                         TickStats* const stats) {
    if (!IsLikelyPointer(entry) || seen == nullptr || stats == nullptr ||
        stats->vector_views >= kMaxVectorViewsPerTick) {
        return;
    }

    uintptr_t begin = 0;
    std::uint64_t count_word = 0;
    if (!SafeRead(entry, &begin) ||
        !SafeRead(entry + kEffectListCountOffset, &count_word)) {
        ++stats->read_failed;
        return;
    }

    const std::uint32_t count = static_cast<std::uint32_t>(count_word >> 32);
    if (!IsLikelyPointer(begin) || count == 0 || count > 0x20) {
        return;
    }

    std::size_t span = static_cast<std::size_t>(count) * 0x100;
    if (span < kMinEffectListScanBytes) {
        span = kMinEffectListScanBytes;
    } else if (span > kMaxEffectListScanBytes) {
        span = kMaxEffectListScanBytes;
    }

    ++stats->vector_views;
    for (uintptr_t cursor = begin; cursor < begin + static_cast<uintptr_t>(span);
         cursor += sizeof(uintptr_t)) {
        uintptr_t candidate = 0;
        if (!SafeRead(cursor, &candidate)) {
            ++stats->read_failed;
            continue;
        }

        TryPatchCandidate(candidate, config, seen, stats);
    }
}

void ScanEffectList(const uintptr_t effect_container,
                    const uintptr_t list_offset,
                    const ModConfig& config,
                    std::vector<uintptr_t>* const seen,
                    TickStats* const stats) {
    if (!IsLikelyPointer(effect_container)) {
        return;
    }

    ScanEffectListEntry(effect_container + list_offset, config, seen, stats);
}

void ScanEffectContainer(const uintptr_t effect_container,
                         const ModConfig& config,
                         std::vector<uintptr_t>* const seen,
                         TickStats* const stats) {
    if (!IsLikelyPointer(effect_container) || seen == nullptr || stats == nullptr) {
        return;
    }

    ++stats->roots;
    ScanEffectList(effect_container, kEffectListAOffset, config, seen, stats);
    ScanEffectList(effect_container, kEffectListBOffset, config, seen, stats);
}

void ScanActorArrayForEffectContainers(const uintptr_t manager,
                                       const ModConfig& config,
                                       std::vector<uintptr_t>* const seen,
                                       TickStats* const stats) {
    if (seen == nullptr || stats == nullptr) {
        return;
    }

    uintptr_t descriptor = 0;
    if (g_slots.actor_array_slot != 0) {
        SafeRead(g_slots.actor_array_slot, &descriptor);
    }
    if (!IsLikelyPointer(descriptor) && IsLikelyPointer(manager)) {
        descriptor = manager + kManagerActorArrayOffset;
    }
    if (!IsLikelyPointer(descriptor)) {
        return;
    }

    uintptr_t actor_array = 0;
    std::uint32_t raw_capacity = 0;
    if (!SafeRead(descriptor, &actor_array) ||
        !SafeRead(descriptor + (kManagerActorArrayCapacityOffset - kManagerActorArrayOffset), &raw_capacity)) {
        ++stats->read_failed;
        return;
    }
    if (!IsLikelyPointer(actor_array)) {
        return;
    }

    std::uint32_t scan_capacity = raw_capacity;
    if (scan_capacity < kActorArrayCapacityMin) {
        scan_capacity = kActorArrayCapacityFallback;
    } else if (scan_capacity > kActorArrayCapacityMax) {
        scan_capacity = kActorArrayCapacityMax;
    }

    for (std::uint32_t i = 0; i < scan_capacity; ++i) {
        uintptr_t ccoia = 0;
        if (!SafeRead(actor_array + static_cast<uintptr_t>(i) * sizeof(uintptr_t), &ccoia)) {
            ++stats->read_failed;
            continue;
        }
        if (!IsLikelyPointer(ccoia)) {
            continue;
        }

        uintptr_t effect_container = 0;
        if (!ResolveEffectContainerFromCcoia(ccoia, &effect_container)) {
            continue;
        }
        ScanEffectContainer(effect_container, config, seen, stats);
    }
}

void TickBloodDecals(const ModConfig& config) {
    const PublishedSlots slots = g_slots;
    if (slots.manager_slot == 0 || slots.module.base == 0 || slots.module.size == 0) {
        return;
    }

    uintptr_t manager = 0;
    uintptr_t effect_container = 0;
    TickStats stats{};

    uintptr_t global_manager = 0;
    if (!SafeRead(slots.manager_slot, &manager)) {
        ++stats.read_failed;
    }
    global_manager = manager;

    const uintptr_t observed_manager = g_observed_manager.load(std::memory_order_acquire);
    if (!IsLikelyPointer(manager) && IsLikelyPointer(observed_manager)) {
        manager = observed_manager;
    }

    const auto tick = g_ticks.fetch_add(1, std::memory_order_relaxed) + 1;
    std::vector<uintptr_t> seen;
    seen.reserve(256);

    if (ResolveEffectContainer(manager, &effect_container, &stats)) {
        ScanEffectContainer(effect_container, config, &seen, &stats);
    }
    ScanActorArrayForEffectContainers(manager, config, &seen, &stats);

    if (stats.roots == 0) {
        if (config.general.verbose || tick == 1 || (tick % 10) == 0) {
            Log("blood-decals: tick=%llu effect path unavailable globalManager=0x%p observedManager=0x%p manager=0x%p effects=0x%p actorArraySlot=0x%p publishHits=%llu readFailed=%lu",
                static_cast<unsigned long long>(tick),
                reinterpret_cast<void*>(global_manager),
                reinterpret_cast<void*>(observed_manager),
                reinterpret_cast<void*>(manager),
                reinterpret_cast<void*>(effect_container),
                reinterpret_cast<void*>(slots.actor_array_slot),
                static_cast<unsigned long long>(g_publish_hits.load(std::memory_order_relaxed)),
                static_cast<unsigned long>(stats.read_failed));
        }
        return;
    }

    g_total_patched.fetch_add(stats.patched, std::memory_order_relaxed);

    if (config.general.verbose || stats.patched > 0 || tick == 1) {
        Log("blood-decals: tick=%llu roots=%lu effectLists=%lu candidates=%lu patched=%lu alreadyExtended=%lu skippedUnexpected=%lu readFailed=%lu writeFailed=%lu diagRttiLogged=%lu diagParticleLogged=%lu publishHits=%llu totalPatched=%llu",
            static_cast<unsigned long long>(tick),
            static_cast<unsigned long>(stats.roots),
            static_cast<unsigned long>(stats.vector_views),
            static_cast<unsigned long>(stats.candidates),
            static_cast<unsigned long>(stats.patched),
            static_cast<unsigned long>(stats.already_extended),
            static_cast<unsigned long>(stats.skipped_unexpected),
            static_cast<unsigned long>(stats.read_failed),
            static_cast<unsigned long>(stats.write_failed),
            static_cast<unsigned long>(stats.diag_rtti_logged),
            static_cast<unsigned long>(stats.diag_particle_logged),
            static_cast<unsigned long long>(g_publish_hits.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_total_patched.load(std::memory_order_relaxed)));
    }
}

DWORD WINAPI BloodDecalsThreadProc(LPVOID) {
    while (g_worker_running.load(std::memory_order_acquire)) {
        const ModConfig config = GetConfig();
        const DWORD wait_ms = EffectiveScanIntervalMs(config.blood_decals.scan_interval_ms);
        const DWORD wait_result = WaitForSingleObject(g_stop_event, wait_ms);
        if (wait_result != WAIT_TIMEOUT) {
            break;
        }

        if (!ShouldInstallBloodDecalsHook(config)) {
            continue;
        }

        TickBloodDecals(config);
    }

    return 0;
}

bool ResolvePublishedSlots(const uintptr_t publish_store, PublishedSlots* const slots) {
    if (slots == nullptr) {
        return false;
    }

    PublishedSlots next{};
    next.module = GetMainModuleBounds();
    if (next.module.base == 0 || next.module.size == 0) {
        Log("blood-decals: failed to resolve main module bounds");
        return false;
    }

    if (!ResolveRipRelativeTarget(publish_store + 0x00, 0x48, 0x89, 0x3D, &next.manager_slot)) {
        Log("blood-decals: failed to resolve manager global slot");
        return false;
    }

    if (!ResolveRipRelativeTarget(publish_store + 0x0E, 0x48, 0x89, 0x05, &next.actor_array_slot)) {
        Log("blood-decals: failed to resolve actor array global slot");
        return false;
    }

    *slots = next;
    return true;
}

void TryInstallDiagnosticHook(const char* const name,
                              const uintptr_t address,
                              SafetyHookMid* const hook,
                              void (*callback)(SafetyHookContext&)) {
    if (name == nullptr || hook == nullptr || callback == nullptr) {
        return;
    }

    if (address == 0) {
        Log("blood-decals: runtime diagnostic hook %s unavailable", name);
        return;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(address), callback);
    if (!hook_result.has_value()) {
        Log("blood-decals: failed to create runtime diagnostic hook %s at 0x%p",
            name,
            reinterpret_cast<void*>(address));
        return;
    }

    *hook = std::move(*hook_result);
    Log("blood-decals: installed runtime diagnostic hook %s at 0x%p",
        name,
        reinterpret_cast<void*>(address));
}

}  // namespace

bool InstallBloodDecalsHook() {
    const ModConfig config = GetConfig();
    if (!ShouldInstallBloodDecalsHook(config)) {
        return true;
    }

    if (g_worker_running.load(std::memory_order_acquire)) {
        Log("blood-decals: worker already running");
        return true;
    }

    const uintptr_t publish_store = ScanForBloodDecalActorManagerPublish();
    if (publish_store == 0) {
        Log("blood-decals: failed to find actor manager publish store");
        return false;
    }

    PublishedSlots slots{};
    if (!ResolvePublishedSlots(publish_store, &slots)) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(publish_store),
                                             BloodDecalsPublishStoreCallback);
    if (!hook_result.has_value()) {
        Log("blood-decals: failed to create publish capture hook");
        return false;
    }

    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stop_event == nullptr) {
        hook_result->reset();
        Log("blood-decals: failed to create stop event");
        return false;
    }

    g_publish_store_hook = std::move(*hook_result);
    g_slots = slots;
    g_observed_manager.store(0, std::memory_order_relaxed);
    g_total_patched.store(0, std::memory_order_relaxed);
    g_publish_hits.store(0, std::memory_order_relaxed);
    g_runtime_particle_hits.store(0, std::memory_order_relaxed);
    g_particle_lifetime_hits.store(0, std::memory_order_relaxed);
    g_particle_lifetime_diag_logs.store(0, std::memory_order_relaxed);
    g_particle_lifetime_patches.store(0, std::memory_order_relaxed);
    g_ground_blood_entry_hits.store(0, std::memory_order_relaxed);
    g_ground_blood_entry_patches.store(0, std::memory_order_relaxed);
    g_ground_blood_entry_read_failed.store(0, std::memory_order_relaxed);
    g_ground_blood_entry_write_failed.store(0, std::memory_order_relaxed);
    g_ground_blood_particle_param_patches.store(0, std::memory_order_relaxed);
    g_ground_blood_particle_param_write_failed.store(0, std::memory_order_relaxed);
    g_ticks.store(0, std::memory_order_relaxed);
    g_user_actor_offset.store(kManagerPrimaryOffset, std::memory_order_relaxed);
    g_skipped_rtti_samples.clear();
    g_particle_diag_samples.clear();
    {
        std::lock_guard lock(g_runtime_particle_diag_mutex);
        g_runtime_particle_diag_samples.clear();
        g_runtime_particle_diag_samples.reserve(kMaxRuntimeParticleDiagSamples);
    }

    const uintptr_t blood_particle_sim_check = ScanForBloodParticleSimulationCheck();
    const uintptr_t blood_particle_check = ScanForBloodParticleRuntimeCheck();
    const uintptr_t terrain_particle_check = ScanForTerrainParticleRuntimeCheck();
    const uintptr_t particle_lifetime_update = ScanForBloodParticleLifetimeUpdate();
    const uintptr_t ground_blood_entry_build = ScanForGroundBloodHitMaterialEffectEntryBuild();
    TryInstallDiagnosticHook("blood135-sim-check",
                             blood_particle_sim_check,
                             &g_blood_particle_sim_check_hook,
                             BloodParticleSimulationCheckCallback);
    TryInstallDiagnosticHook("blood135-check",
                             blood_particle_check,
                             &g_blood_particle_check_hook,
                             BloodParticleRuntimeCheckCallback);
    TryInstallDiagnosticHook("terrain12B-check",
                             terrain_particle_check,
                             &g_terrain_particle_check_hook,
                             TerrainParticleRuntimeCheckCallback);
    if (particle_lifetime_update != 0) {
        auto lifetime_hook_result =
            SafetyHookMid::create(reinterpret_cast<void*>(particle_lifetime_update),
                                  ParticleLifetimeUpdateCallback);
        if (lifetime_hook_result.has_value()) {
            g_particle_lifetime_update_hook = std::move(*lifetime_hook_result);
            Log("blood-decals: installed particle lifetime hook at 0x%p",
                reinterpret_cast<void*>(particle_lifetime_update));
        } else {
            Log("blood-decals: failed to create particle lifetime hook at 0x%p",
                reinterpret_cast<void*>(particle_lifetime_update));
        }
    } else {
        Log("blood-decals: particle lifetime hook unavailable");
    }

    if (ground_blood_entry_build != 0) {
        auto ground_entry_hook_result =
            SafetyHookMid::create(reinterpret_cast<void*>(ground_blood_entry_build),
                                  GroundBloodHitMaterialEffectEntryBuildCallback);
        if (ground_entry_hook_result.has_value()) {
            g_ground_blood_entry_build_hook = std::move(*ground_entry_hook_result);
            Log("blood-decals: installed ground blood hit-material entry hook at 0x%p",
                reinterpret_cast<void*>(ground_blood_entry_build));
        } else {
            Log("blood-decals: failed to create ground blood hit-material entry hook at 0x%p",
                reinterpret_cast<void*>(ground_blood_entry_build));
        }
    } else {
        Log("blood-decals: ground blood hit-material entry hook unavailable");
    }

    g_worker_running.store(true, std::memory_order_release);

    g_worker_thread = CreateThread(nullptr, 0, BloodDecalsThreadProc, nullptr, 0, nullptr);
    if (g_worker_thread == nullptr) {
        g_worker_running.store(false, std::memory_order_release);
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
        g_blood_particle_sim_check_hook.reset();
        g_blood_particle_check_hook.reset();
        g_terrain_particle_check_hook.reset();
        g_particle_lifetime_update_hook.reset();
        g_ground_blood_entry_build_hook.reset();
        g_publish_store_hook.reset();
        Log("blood-decals: failed to create worker thread");
        return false;
    }

    Log("blood-decals: worker started publish=0x%p manager-slot=0x%p actor-array-slot=0x%p captureHook=installed groundEntryHook=%s effect-path=+0x%llX/+0x%llX/+0x%llX/+0x%llX/+0x%llX actor-array=+0x%llX cap=+0x%llX lists=+0x%llX,+0x%llX interval=%lu effectiveInterval=%lu life78Ms=%lu life40Ms=%lu groundParticleLifeSec=%.3f particleLifeSec=%.3f fade80Ms=unchanged",
        reinterpret_cast<void*>(publish_store),
        reinterpret_cast<void*>(slots.manager_slot),
        reinterpret_cast<void*>(slots.actor_array_slot),
        g_ground_blood_entry_build_hook ? "installed" : "unavailable",
        static_cast<unsigned long long>(kManagerPrimaryOffset),
        static_cast<unsigned long long>(kManagerPrimaryChildOffset),
        static_cast<unsigned long long>(kEffectRootOffset),
        static_cast<unsigned long long>(kEffectOwnerOffset),
        static_cast<unsigned long long>(kEffectContainerOffset),
        static_cast<unsigned long long>(kManagerActorArrayOffset),
        static_cast<unsigned long long>(kManagerActorArrayCapacityOffset),
        static_cast<unsigned long long>(kEffectListAOffset),
        static_cast<unsigned long long>(kEffectListBOffset),
        static_cast<unsigned long>(config.blood_decals.scan_interval_ms),
        static_cast<unsigned long>(EffectiveScanIntervalMs(config.blood_decals.scan_interval_ms)),
        static_cast<unsigned long>(TargetLifetimeMs(config)),
        static_cast<unsigned long>(TargetLifetimeMs(config)),
        static_cast<double>(TargetLifetimeSeconds(config)),
        static_cast<double>(TargetLifetimeSeconds(config)));
    return true;
}

void RemoveBloodDecalsHook() {
    if (!g_worker_running.exchange(false, std::memory_order_acq_rel) &&
        g_worker_thread == nullptr && g_stop_event == nullptr) {
        return;
    }

    if (g_stop_event != nullptr) {
        SetEvent(g_stop_event);
    }

    if (g_worker_thread != nullptr) {
        WaitForSingleObject(g_worker_thread, INFINITE);
        CloseHandle(g_worker_thread);
        g_worker_thread = nullptr;
    }

    if (g_stop_event != nullptr) {
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
    }

    if (g_publish_store_hook) {
        g_publish_store_hook.reset();
    }

    if (g_blood_particle_sim_check_hook) {
        g_blood_particle_sim_check_hook.reset();
    }

    if (g_blood_particle_check_hook) {
        g_blood_particle_check_hook.reset();
    }

    if (g_terrain_particle_check_hook) {
        g_terrain_particle_check_hook.reset();
    }

    if (g_particle_lifetime_update_hook) {
        g_particle_lifetime_update_hook.reset();
    }

    if (g_ground_blood_entry_build_hook) {
        g_ground_blood_entry_build_hook.reset();
    }

    Log("blood-decals: worker stopped ticks=%llu publishHits=%llu runtimeParticleHits=%llu runtimeParticleSamples=%llu particleLifetimeHits=%llu particleLifetimePatches=%llu groundEntryHits=%llu groundEntryPatches=%llu groundParticleParamPatches=%llu groundEntryReadFailed=%llu groundEntryWriteFailed=%llu groundParticleParamWriteFailed=%llu totalPatched=%llu",
        static_cast<unsigned long long>(g_ticks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_publish_hits.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_runtime_particle_hits.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(RuntimeParticleDiagSampleCount()),
        static_cast<unsigned long long>(g_particle_lifetime_hits.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_particle_lifetime_patches.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_ground_blood_entry_hits.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_ground_blood_entry_patches.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_ground_blood_particle_param_patches.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_ground_blood_entry_read_failed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_ground_blood_entry_write_failed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_ground_blood_particle_param_write_failed.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_total_patched.load(std::memory_order_relaxed)));
}
