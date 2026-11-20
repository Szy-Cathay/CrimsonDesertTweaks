#include "scanner.h"

#include "logger.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

struct PatternDefinition {
    const char* name = nullptr;
    const char* text = nullptr;
    std::size_t hook_offset = 0;
};

struct SectionSpan {
    const std::uint8_t* begin = nullptr;
    std::size_t size = 0;
    std::string section_name;
    bool executable = false;
};

struct PatternByte {
    std::uint8_t value = 0;
    bool wildcard = false;
};

struct MatchResult {
    uintptr_t address = 0;
    std::string section_name;
};

constexpr uintptr_t kMinimumPointerAddress = 0x10000;

SectionSpan EnumerateMainModuleImageSpan() {
    const auto module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        Log("scanner: GetModuleHandleW(nullptr) failed");
        return {};
    }

    const auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("scanner: invalid DOS header");
        return {};
    }

    const auto nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        Log("scanner: invalid NT header");
        return {};
    }

    const auto image_size = static_cast<std::size_t>(nt_headers->OptionalHeader.SizeOfImage);
    if (image_size == 0) {
        Log("scanner: main-module image size is zero");
        return {};
    }

    return {
        module,
        image_size,
        "[image]",
        true,
    };
}

std::vector<SectionSpan> EnumerateMainModuleSections(const bool text_only) {
    std::vector<SectionSpan> sections;

    const auto module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        Log("scanner: GetModuleHandleW(nullptr) failed");
        return sections;
    }

    const auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("scanner: invalid DOS header");
        return sections;
    }

    const auto nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        Log("scanner: invalid NT header");
        return sections;
    }

    const auto first_section = IMAGE_FIRST_SECTION(nt_headers);
    for (unsigned i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i) {
        const auto& section = first_section[i];

        char name_buffer[9]{};
        std::memcpy(name_buffer, section.Name, 8);
        const std::string section_name(name_buffer);

        if (text_only && section_name != ".text") {
            continue;
        }

        if (section.Misc.VirtualSize == 0) {
            continue;
        }

        const bool executable = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if (!text_only && !executable) {
            continue;
        }

        sections.push_back({
            module + section.VirtualAddress,
            static_cast<std::size_t>(section.Misc.VirtualSize),
            section_name,
            executable,
        });
    }

    return sections;
}

std::vector<PatternByte> ParsePattern(const char* text) {
    std::vector<PatternByte> bytes;
    std::string token;

    for (const char* cursor = text; *cursor != '\0';) {
        while (*cursor == ' ') {
            ++cursor;
        }

        if (*cursor == '\0') {
            break;
        }

        const char* token_start = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }

        token.assign(token_start, cursor);
        if (token == "?" || token == "??" || token == "*") {
            bytes.push_back({0, true});
        } else {
            bytes.push_back({static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)), false});
        }
    }

    return bytes;
}

std::vector<MatchResult> ScanPattern(const std::vector<SectionSpan>& spans,
                                     const std::vector<PatternByte>& pattern,
                                     const std::size_t max_results) {
    std::vector<MatchResult> results;
    if (pattern.empty()) {
        return results;
    }

    for (const auto& span : spans) {
        if (span.begin == nullptr || span.size < pattern.size()) {
            continue;
        }

        const auto end = span.size - pattern.size();
        for (std::size_t offset = 0; offset <= end; ++offset) {
            bool matched = true;
            for (std::size_t index = 0; index < pattern.size(); ++index) {
                if (!pattern[index].wildcard && span.begin[offset + index] != pattern[index].value) {
                    matched = false;
                    break;
                }
            }

            if (!matched) {
                continue;
            }

            results.push_back({
                reinterpret_cast<uintptr_t>(span.begin + offset),
                span.section_name,
            });

            if (results.size() >= max_results) {
                return results;
            }
        }
    }

    return results;
}

std::vector<uintptr_t> ScanAllInSections(const PatternDefinition* definitions,
                                         const std::size_t definition_count,
                                         const std::size_t max_results) {
    for (int pass = 0; pass < 2; ++pass) {
        const bool text_only = pass == 0;
        const auto sections = EnumerateMainModuleSections(text_only);
        if (sections.empty()) {
            continue;
        }

        for (std::size_t definition_index = 0; definition_index < definition_count; ++definition_index) {
            const auto& definition = definitions[definition_index];
            const auto pattern = ParsePattern(definition.text);
            const auto matches = ScanPattern(sections, pattern, max_results);

            Log("scanner: DIAG multi pattern=\"%s\" pass=%s match_count=%llu",
                definition.name != nullptr ? definition.name : "<unnamed>",
                text_only ? ".text" : "executable",
                static_cast<unsigned long long>(matches.size()));

            if (matches.empty()) {
                continue;
            }

            std::vector<uintptr_t> addresses;
            addresses.reserve(matches.size());
            for (const auto& match : matches) {
                const uintptr_t hook_address = match.address + definition.hook_offset;
                Log("scanner: DIAG MULTI MATCH pattern=\"%s\" addr=0x%p hook_off=0x%llX final=0x%p",
                    definition.name != nullptr ? definition.name : "<unnamed>",
                    reinterpret_cast<void*>(match.address),
                    static_cast<unsigned long long>(definition.hook_offset),
                    reinterpret_cast<void*>(hook_address));
                addresses.push_back(hook_address);
            }
            return addresses;
        }
    }

    const auto image_span = EnumerateMainModuleImageSpan();
    if (image_span.begin == nullptr || image_span.size == 0) {
        Log("scanner: DIAG multi failed - no image span available");
        return {};
    }

    const std::vector<SectionSpan> image_spans{image_span};
    for (std::size_t definition_index = 0; definition_index < definition_count; ++definition_index) {
        const auto& definition = definitions[definition_index];
        const auto pattern = ParsePattern(definition.text);
        const auto matches = ScanPattern(image_spans, pattern, max_results);

        Log("scanner: DIAG multi pattern=\"%s\" pass=image match_count=%llu",
            definition.name != nullptr ? definition.name : "<unnamed>",
            static_cast<unsigned long long>(matches.size()));

        if (matches.empty()) {
            continue;
        }

        std::vector<uintptr_t> addresses;
        addresses.reserve(matches.size());
        for (const auto& match : matches) {
            const uintptr_t hook_address = match.address + definition.hook_offset;
            Log("scanner: DIAG MULTI MATCH pattern=\"%s\" addr=0x%p hook_off=0x%llX final=0x%p",
                definition.name != nullptr ? definition.name : "<unnamed>",
                reinterpret_cast<void*>(match.address),
                static_cast<unsigned long long>(definition.hook_offset),
                reinterpret_cast<void*>(hook_address));
            addresses.push_back(hook_address);
        }
        return addresses;
    }

    Log("scanner: DIAG multi failed - all passes exhausted, 0 matches");
    return {};
}

}  // namespace

// ── Shield Hide scanner ────────────────────────────────────────────────

std::vector<uintptr_t> ScanForShieldFakeEquipAccesses() {
    // Hook at "mov rcx,[rax]" (offset 7 into pattern).  In the callback we
    // swap rax to point at a dummy null qword so that rcx becomes 0, then the
    // original "test rcx,rcx / jz skip" naturally bypasses the render call.
    static constexpr PatternDefinition kPatterns[] = {
        {
            "exact-key-load-1.12.02",
            "48 8B 05 0A 3B C3 04 48 8B 08",
            7,  // hook at mov rcx,[rax]
        },
        {
            "reserve-slot-key-load-cluster-1.13.00",
            "48 8B 05 ?? ?? ?? ?? 48 8B 08 48 85 C9 74 44 80 39 00 74 3F "
            "48 8D 55 67 E8 ?? ?? ?? ?? 8B 45 67 89 45 77 "
            "48 8B 0D ?? ?? ?? ?? 48 83 C1 60 48 8D 55 77 "
            "E8 ?? ?? ?? ?? 48 85 C0",
            7,  // hook at mov rcx,[rax]
        },
    };

    auto addresses = ScanAllInSections(kPatterns,
                                       sizeof(kPatterns) / sizeof(kPatterns[0]),
                                       16);
    if (addresses.empty()) {
        Log("scanner: shield-fake-equip found 0 matches");
        return {};
    }

    return addresses;
}

uintptr_t ScanForShieldFakeEquipAccess() {
    const auto addresses = ScanForShieldFakeEquipAccesses();
    if (addresses.empty()) {
        return 0;
    }

    if (addresses.size() > 1) {
        Log("scanner: shield-fake-equip legacy single-address request got %llu matches; using first",
            static_cast<unsigned long long>(addresses.size()));
    }
    return addresses.front();
}
