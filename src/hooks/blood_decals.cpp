#include "config.h"
#include "logger.h"
#include "scanner.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct ModuleBounds {
    uintptr_t base = 0;
    std::size_t size = 0;
};

struct PublishedSlots {
    uintptr_t manager_slot = 0;
    uintptr_t list_a_slot = 0;
    uintptr_t list_b_slot = 0;
    ModuleBounds module;
};

struct VectorView {
    uintptr_t begin = 0;
    uintptr_t end = 0;
    uintptr_t cap = 0;
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
};

constexpr uintptr_t kMinimumPointerAddress = 0x10000;
constexpr uintptr_t kMaximumUserPointer = 0x0000800000000000ULL;
constexpr std::size_t kRootScanBytes = 0x400;
constexpr std::size_t kRootVectorScanBytes = 0x200;
constexpr std::size_t kMaxVectorSpanBytes = 0x40000;
constexpr std::size_t kMaxVectorViewsPerTick = 64;
constexpr std::size_t kMaxSeenCandidates = 4096;
constexpr float kMaxReasonableLifetime = 86400.0f;
constexpr float kPatchEpsilon = 0.001f;

std::atomic<bool> g_worker_running{false};
std::atomic<std::uint64_t> g_total_patched{0};
std::atomic<std::uint64_t> g_ticks{0};
HANDLE g_worker_thread = nullptr;
HANDLE g_stop_event = nullptr;
PublishedSlots g_slots{};

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

bool IsInModule(const uintptr_t value, const ModuleBounds& bounds) {
    return bounds.base != 0 && bounds.size != 0 &&
           value >= bounds.base && value < bounds.base + bounds.size;
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

bool HasGameSkinnedDecalRtti(const uintptr_t object, const ModuleBounds& module) {
    uintptr_t vtable = 0;
    if (!SafeRead(object, &vtable) || !IsLikelyPointer(vtable)) {
        return false;
    }

    uintptr_t locator_address = 0;
    if (!SafeRead(vtable - sizeof(uintptr_t), &locator_address) ||
        !IsInModule(locator_address, module)) {
        return false;
    }

    RttiCompleteObjectLocator locator{};
    if (!SafeRead(locator_address, &locator) || locator.signature != 1 ||
        locator.type_descriptor_rva <= 0) {
        return false;
    }

    const uintptr_t type_descriptor =
        module.base + static_cast<std::uint32_t>(locator.type_descriptor_rva);
    if (!IsInModule(type_descriptor, module)) {
        return false;
    }

    char type_name[192]{};
    if (!SafeReadCString(type_descriptor + 0x10, type_name, sizeof(type_name))) {
        return false;
    }

    return std::strstr(type_name, "GameSkinnedDecalElement") != nullptr;
}

bool IsValidLifetimeFloat(const float value) {
    return std::isfinite(value) && value >= -1.0f && value <= kMaxReasonableLifetime;
}

bool WasSeen(const std::vector<uintptr_t>& seen, const uintptr_t candidate) {
    return std::find(seen.begin(), seen.end(), candidate) != seen.end();
}

void TryPatchCandidate(const uintptr_t candidate,
                       const ModConfig& config,
                       const ModuleBounds& module,
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

    if (!HasGameSkinnedDecalRtti(candidate, module)) {
        ++stats->skipped_unexpected;
        return;
    }

    float life78 = 0.0f;
    float fade80 = 0.0f;
    if (!SafeRead(candidate + 0x78, &life78) ||
        !SafeRead(candidate + 0x80, &fade80)) {
        ++stats->read_failed;
        return;
    }

    if (!IsValidLifetimeFloat(life78) || !std::isfinite(fade80)) {
        ++stats->skipped_unexpected;
        return;
    }

    const float target_life = config.blood_decals.lifetime_seconds;
    if (life78 + kPatchEpsilon >= target_life) {
        ++stats->already_extended;
        return;
    }

    if (!SafeWriteFloat(candidate + 0x78, target_life)) {
        ++stats->write_failed;
        if (config.general.verbose) {
            Log("blood-decals: write failed addr=0x%p value=%.3f",
                reinterpret_cast<void*>(candidate + 0x78),
                static_cast<double>(target_life));
        }
        return;
    }

    ++stats->patched;
    if (config.general.verbose) {
        Log("blood-decals: patch ptr=0x%p life78 %.3f->%.3f fade80 %.3f unchanged",
            reinterpret_cast<void*>(candidate),
            static_cast<double>(life78),
            static_cast<double>(target_life),
            static_cast<double>(fade80));
    }
}

bool IsValidVectorView(const VectorView& view) {
    if (!IsLikelyPointer(view.begin) || !IsLikelyPointer(view.end) ||
        !IsLikelyPointer(view.cap)) {
        return false;
    }

    if (view.end < view.begin || view.cap < view.end) {
        return false;
    }

    const uintptr_t span = view.end - view.begin;
    return span <= kMaxVectorSpanBytes && (span % sizeof(uintptr_t)) == 0;
}

void ScanPointerSlots(const uintptr_t begin,
                      const std::size_t size,
                      const ModConfig& config,
                      const ModuleBounds& module,
                      std::vector<uintptr_t>* const seen,
                      TickStats* const stats) {
    for (std::size_t offset = 0; offset + sizeof(uintptr_t) <= size; offset += sizeof(uintptr_t)) {
        uintptr_t candidate = 0;
        if (!SafeRead(begin + offset, &candidate)) {
            ++stats->read_failed;
            continue;
        }

        if (IsLikelyPointer(candidate)) {
            TryPatchCandidate(candidate, config, module, seen, stats);
        }
    }
}

void ScanVectorView(const uintptr_t view_address,
                    const ModConfig& config,
                    const ModuleBounds& module,
                    std::vector<uintptr_t>* const seen,
                    TickStats* const stats) {
    if (stats == nullptr || stats->vector_views >= kMaxVectorViewsPerTick) {
        return;
    }

    VectorView view{};
    if (!SafeRead(view_address, &view) || !IsValidVectorView(view)) {
        return;
    }

    ++stats->vector_views;
    for (uintptr_t cursor = view.begin; cursor < view.end; cursor += sizeof(uintptr_t)) {
        uintptr_t candidate = 0;
        if (!SafeRead(cursor, &candidate)) {
            ++stats->read_failed;
            continue;
        }

        TryPatchCandidate(candidate, config, module, seen, stats);
    }
}

void ScanRoot(const uintptr_t root,
              const ModConfig& config,
              const PublishedSlots& slots,
              std::vector<uintptr_t>* const seen,
              TickStats* const stats) {
    if (!IsLikelyPointer(root) || seen == nullptr || stats == nullptr) {
        return;
    }

    ++stats->roots;
    ScanPointerSlots(root, kRootScanBytes, config, slots.module, seen, stats);

    for (std::size_t offset = 0; offset + sizeof(VectorView) <= kRootVectorScanBytes; offset += sizeof(uintptr_t)) {
        ScanVectorView(root + offset, config, slots.module, seen, stats);

        uintptr_t nested = 0;
        if (SafeRead(root + offset, &nested) && IsLikelyPointer(nested)) {
            ScanVectorView(nested, config, slots.module, seen, stats);
        }
    }
}

void TickBloodDecals(const ModConfig& config) {
    const PublishedSlots slots = g_slots;
    if (slots.manager_slot == 0 || slots.list_a_slot == 0 || slots.list_b_slot == 0 ||
        slots.module.base == 0 || slots.module.size == 0) {
        return;
    }

    uintptr_t manager = 0;
    uintptr_t list_a = 0;
    uintptr_t list_b = 0;
    TickStats stats{};

    if (!SafeRead(slots.manager_slot, &manager)) {
        ++stats.read_failed;
    }
    if (!SafeRead(slots.list_a_slot, &list_a)) {
        ++stats.read_failed;
    }
    if (!SafeRead(slots.list_b_slot, &list_b)) {
        ++stats.read_failed;
    }

    std::vector<uintptr_t> seen;
    seen.reserve(256);
    ScanRoot(manager, config, slots, &seen, &stats);
    ScanRoot(list_a, config, slots, &seen, &stats);
    ScanRoot(list_b, config, slots, &seen, &stats);

    g_total_patched.fetch_add(stats.patched, std::memory_order_relaxed);
    const auto tick = g_ticks.fetch_add(1, std::memory_order_relaxed) + 1;

    if (config.general.verbose || stats.patched > 0) {
        Log("blood-decals: tick=%llu roots=%lu vectors=%lu candidates=%lu patched=%lu alreadyExtended=%lu skippedUnexpected=%lu readFailed=%lu writeFailed=%lu totalPatched=%llu",
            static_cast<unsigned long long>(tick),
            static_cast<unsigned long>(stats.roots),
            static_cast<unsigned long>(stats.vector_views),
            static_cast<unsigned long>(stats.candidates),
            static_cast<unsigned long>(stats.patched),
            static_cast<unsigned long>(stats.already_extended),
            static_cast<unsigned long>(stats.skipped_unexpected),
            static_cast<unsigned long>(stats.read_failed),
            static_cast<unsigned long>(stats.write_failed),
            static_cast<unsigned long long>(g_total_patched.load(std::memory_order_relaxed)));
    }
}

DWORD WINAPI BloodDecalsThreadProc(LPVOID) {
    while (g_worker_running.load(std::memory_order_acquire)) {
        const ModConfig config = GetConfig();
        const DWORD wait_ms = config.blood_decals.scan_interval_ms;
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

    if (!ResolveRipRelativeTarget(publish_store + 0x00, 0x48, 0x89, 0x3D, &next.manager_slot) ||
        !ResolveRipRelativeTarget(publish_store + 0x0E, 0x48, 0x89, 0x05, &next.list_a_slot) ||
        !ResolveRipRelativeTarget(publish_store + 0x1C, 0x48, 0x89, 0x05, &next.list_b_slot)) {
        Log("blood-decals: failed to resolve publish global slots");
        return false;
    }

    *slots = next;
    return true;
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

    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stop_event == nullptr) {
        Log("blood-decals: failed to create stop event");
        return false;
    }

    g_slots = slots;
    g_total_patched.store(0, std::memory_order_relaxed);
    g_ticks.store(0, std::memory_order_relaxed);
    g_worker_running.store(true, std::memory_order_release);

    g_worker_thread = CreateThread(nullptr, 0, BloodDecalsThreadProc, nullptr, 0, nullptr);
    if (g_worker_thread == nullptr) {
        g_worker_running.store(false, std::memory_order_release);
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
        Log("blood-decals: failed to create worker thread");
        return false;
    }

    Log("blood-decals: worker started publish=0x%p manager-slot=0x%p list-a-slot=0x%p list-b-slot=0x%p interval=%lu life78=%.3f fade80=unchanged",
        reinterpret_cast<void*>(publish_store),
        reinterpret_cast<void*>(slots.manager_slot),
        reinterpret_cast<void*>(slots.list_a_slot),
        reinterpret_cast<void*>(slots.list_b_slot),
        static_cast<unsigned long>(config.blood_decals.scan_interval_ms),
        static_cast<double>(config.blood_decals.lifetime_seconds));
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

    Log("blood-decals: worker stopped ticks=%llu totalPatched=%llu",
        static_cast<unsigned long long>(g_ticks.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_total_patched.load(std::memory_order_relaxed)));
}
