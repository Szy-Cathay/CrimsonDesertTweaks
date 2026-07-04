#pragma once

#include <cstdint>

namespace blood_decals {

inline constexpr std::uint32_t kDefaultBloodLifeMs = 3000;
inline constexpr std::uint32_t kDefaultBloodFadeMs = 1500;
inline constexpr std::uint32_t kV113SkinnedBloodLifeMs = 1000;
inline constexpr std::uint32_t kV113SkinnedBloodFadeMs = 0;
inline constexpr std::uint32_t kReferenceExtendedLifeMs = 300000;
inline constexpr std::uint32_t kMaxLifetimeMs = 86400000;

inline bool IsExpectedBloodDecalTiming(const std::uint32_t life78,
                                       const std::uint32_t fade80,
                                       const std::uint32_t target_life) {
    const bool classic_timing =
        fade80 == kDefaultBloodFadeMs &&
        (life78 == kDefaultBloodLifeMs ||
         life78 == kReferenceExtendedLifeMs ||
         life78 == target_life);
    const bool v113_skinned_timing =
        fade80 == kV113SkinnedBloodFadeMs &&
        (life78 == kV113SkinnedBloodLifeMs ||
         life78 == target_life);
    return classic_timing || v113_skinned_timing;
}

inline std::uint32_t EffectiveScanIntervalMs(const std::uint32_t configured_interval_ms) {
    constexpr std::uint32_t kMaxSkinnedScanIntervalMs = 100;
    return configured_interval_ms < kMaxSkinnedScanIntervalMs ?
               configured_interval_ms :
               kMaxSkinnedScanIntervalMs;
}

}  // namespace blood_decals
