#include "config.h"
#include "hooks/blood_decals_timing.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::filesystem::path MakeTempIniPath(const wchar_t* const name) {
    wchar_t temp_path[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, temp_path);
    if (length == 0 || length >= MAX_PATH) {
        return std::filesystem::path{name};
    }

    std::wstring unique_name{name};
    unique_name += L".";
    unique_name += std::to_wstring(GetCurrentProcessId());
    return std::filesystem::path{temp_path} / unique_name;
}

void WriteTextFile(const std::filesystem::path& path, const char* const text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

bool NearlyEqual(const float lhs, const float rhs) {
    return std::fabs(lhs - rhs) < 0.001f;
}

int ExpectTrue(const bool value, const char* const message) {
    if (value) {
        return 0;
    }

    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

int ExpectFalse(const bool value, const char* const message) {
    return ExpectTrue(!value, message);
}

int ExpectFloat(const float actual, const float expected, const char* const message) {
    if (NearlyEqual(actual, expected)) {
        return 0;
    }

    std::cerr << "FAIL: " << message << " expected " << expected << " got " << actual << '\n';
    return 1;
}

int ExpectDword(const DWORD actual, const DWORD expected, const char* const message) {
    if (actual == expected) {
        return 0;
    }

    std::cerr << "FAIL: " << message << " expected " << expected << " got " << actual << '\n';
    return 1;
}

int TestBloodDecalsSectionLoads() {
    const auto path = MakeTempIniPath(L"crimson-desert-tweaks-blooddecals.ini");
    WriteTextFile(path,
                  "[General]\n"
                  "Enabled=1\n"
                  "\n"
                  "[BloodDecals]\n"
                  "Enable=1\n"
                  "LifetimeSeconds=600.0\n"
                  "ScanIntervalMs=750\n");

    ModConfig config{};
    int failures = ExpectTrue(ReadConfigSnapshot(path.wstring(), &config),
                              "[BloodDecals] config should load");
    failures += ExpectTrue(config.blood_decals.enabled,
                           "[BloodDecals] Enable=1 should enable BloodDecals");
    failures += ExpectFloat(config.blood_decals.lifetime_seconds,
                            600.0f,
                            "[BloodDecals] LifetimeSeconds should be read");
    failures += ExpectDword(config.blood_decals.scan_interval_ms,
                            750,
                            "[BloodDecals] ScanIntervalMs should be read");

    std::filesystem::remove(path);
    return failures;
}

int TestBloodDecalTimingFilter() {
    constexpr std::uint32_t target_life = 600000;

    int failures = 0;
    failures += ExpectTrue(blood_decals::IsExpectedBloodDecalTiming(3000, 1500, target_life),
                           "classic 3000/1500 skinned blood timing should be accepted");
    failures += ExpectTrue(blood_decals::IsExpectedBloodDecalTiming(300000, 1500, target_life),
                           "reference extended 300000/1500 timing should be accepted");
    failures += ExpectTrue(blood_decals::IsExpectedBloodDecalTiming(target_life, 1500, target_life),
                           "already extended classic timing should be accepted");
    failures += ExpectTrue(blood_decals::IsExpectedBloodDecalTiming(1000, 0, target_life),
                           "v1.13.00 1000/0 skinned blood timing should be accepted");
    failures += ExpectTrue(blood_decals::IsExpectedBloodDecalTiming(target_life, 0, target_life),
                           "already extended v1.13.00 timing should be accepted");
    failures += ExpectFalse(blood_decals::IsExpectedBloodDecalTiming(1000, 1500, target_life),
                            "unknown 1000/1500 timing should stay rejected");
    failures += ExpectFalse(blood_decals::IsExpectedBloodDecalTiming(3000, 0, target_life),
                            "unknown 3000/0 timing should stay rejected");
    return failures;
}

int TestEffectiveScanIntervalForShortLivedSkinnedBlood() {
    int failures = 0;
    failures += ExpectDword(blood_decals::EffectiveScanIntervalMs(1000),
                            100,
                            "1000ms config interval should be capped for v1.13.00 1000ms skinned blood");
    failures += ExpectDword(blood_decals::EffectiveScanIntervalMs(250),
                            100,
                            "250ms config interval should be capped for v1.13.00 1000ms skinned blood");
    failures += ExpectDword(blood_decals::EffectiveScanIntervalMs(100),
                            100,
                            "100ms config interval should stay unchanged");
    return failures;
}

}  // namespace

int main() {
    int failures = 0;
    failures += TestBloodDecalsSectionLoads();
    failures += TestBloodDecalTimingFilter();
    failures += TestEffectiveScanIntervalForShortLivedSkinnedBlood();

    if (failures != 0) {
        std::cerr << failures << " test expectation(s) failed\n";
        return 1;
    }

    std::cout << "All config and BloodDecals timing tests passed\n";
    return 0;
}
