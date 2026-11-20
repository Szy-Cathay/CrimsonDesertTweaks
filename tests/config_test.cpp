#include "config.h"

#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::cerr << "Expected a path to the default INI\n";
        return 1;
    }

    ModConfig config{};
    if (!ReadConfigSnapshot(argv[1], &config) || !ShouldInstallShieldHideHook(config)) {
        std::cerr << "The default INI must enable the shield hook via [Shield] Enable=1\n";
        return 1;
    }

    config.general.enabled = false;
    if (ShouldInstallShieldHideHook(config)) {
        std::cerr << "The general switch must disable the shield hook\n";
        return 1;
    }

    config.general.enabled = true;
    config.shield.enabled = false;
    if (ShouldInstallShieldHideHook(config)) {
        std::cerr << "The shield switch must disable the shield hook\n";
        return 1;
    }

    return 0;
}
