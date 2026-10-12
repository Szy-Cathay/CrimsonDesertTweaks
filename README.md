[English](README.md) | [简体中文](README.zh-CN.md)

# Crimson Desert Tweaks

[![Language](https://img.shields.io/badge/language-C%2B%2B23-%23f34b7d)](https://en.cppreference.com/w/cpp/23)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078d7)](https://www.microsoft.com/windows)
[![Game](https://img.shields.io/badge/game-Crimson%20Desert-orange)](https://crimsondesert.pearlabyss.com/)

A collection of quality-of-life tweaks for **Crimson Desert**, implemented as an ASI plugin via runtime memory hooks.

Development and ground-blood experiments are paused. The main branch retains historical v1.13.00 code; the v2.00.02 adaptation was reverted, and compatibility with the installed game version is unconfirmed. See [current status and validation limits](PROGRESS.md) and [project build/test guidance](AGENTS.md). Documentation maintenance does not resume development.

---

## Features

| Feature | Description |
|---------|-------------|
| **Shield Auto-Hide** | Automatically hides the back shield when dual-wielding. Adapted for the v1.13.00 ReserveSlot/FakeEquip key-load path. |
| **Blood Decals Lifetime** | Extends skinned/body/face blood decals to the configured lifetime. v1.13.00 `GameSkinnedDecalElement` timing (`life78=1000ms`, `fade80=0`) is supported. Ground/terrain blood is unfinished and paused; the rejected terrain `+0x160` route is not used. |

## Installation

Install via [DMM](https://www.nexusmods.com/crimsondesert/mods/633):

1. Drag the downloaded archive into the **DMM** window
2. Switch to the **ASI Mods** tab
3. Check **crimson-desert-tweaks** to enable it
4. Click **Configure** to customize settings

## Configuration

Edit `crimson-desert-tweaks.ini`. Changes take effect automatically (hot reload).

```ini
[General]
Enabled=1                   # Master enable
LogEnabled=1                # Logging
Verbose=0                   # Verbose log (set 1 for debugging)
MaxLogLines=2000
InitDelayMs=3000

[Shield]
Enable=1                    # Shield auto-hide

[BloodDecals]
Enabled=1                   # Blood decal lifetime extension
LifetimeSeconds=600.0       # Target skinned/body/face blood lifetime
ScanIntervalMs=100          # Worker scan interval, capped internally to 100ms
```

## Build

**Requirements**: Visual Studio 2022+, CMake 3.20+

```bash
cmake -S . -B build -A x64
cmake --build build --config Release
```

Release output is kept in `build/Release/` only:

```text
build/Release/crimson-desert-tweaks.asi
build/Release/crimson-desert-tweaks.ini
```

The INI is copied from the repository template so the ASI and default configuration are always generated side by side. Installation to the game or DMM mod directory is manual.

## Tech Stack

- C++23 / MSVC x64
- [SafetyHook](https://github.com/cursey/safetyhook) — Hooking framework
- [Zydis](https://github.com/zyantific/zydis) — Disassembler

## Credits

### Development
- Project led by **Szy-Cathay**. Code and reverse-engineering notes were produced with AI coding assistance.

### Upstream Code
- [Orcax-1399/CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier) — Original mod
- [sreerajjayaraj/player-status-modifier](https://github.com/sreerajjayaraj/player-status-modifier) — Intermediate fork, direct source of this project

### Libraries
- [SafetyHook](https://github.com/cursey/safetyhook) — Hooking framework
- [Zydis](https://github.com/zyantific/zydis) — Disassembler

### Reverse Engineering
- [Rumi's Blood mod by RumiYumi](https://www.nexusmods.com/crimsondesert/mods/2929), local binary copy `D:\AI Agent Resources\GitHub\rumi_blood_mod\CrimsonDesertBloodDecals.asi` — behavior/compatibility reference for blood mark lifetime. The Nexus page lists MIT licensing but also restrictive file/asset permissions, so this project does not include, redistribute, or copy files/assets from that mod.
- [tkhquang/CrimsonDesertTools](https://github.com/tkhquang/CrimsonDesertTools), local source copy `D:\AI Agent Resources\GitHub\CrimsonDesertTools-main` — ASI project layout and Crimson Desert runtime-hook reference
- [Orcax-1399/CrimsonDesert-player-status-modifier](https://github.com/Orcax-1399/CrimsonDesert-player-status-modifier), local source copy `D:\AI Agent Resources\GitHub\CrimsonDesert-player-status-modifier-master` — player-status ASI source and SafetyHook/CMake patterns
- [Orcax-1399/CrimsonDesert-parry-evade](https://github.com/Orcax-1399/CrimsonDesert-parry-evade), local source copy `D:\AI Agent Resources\GitHub\CrimsonDesert-parry-evade-master` — Crimson Desert hook/scanner reference
- [Ronibuilds/CrimsonDesertMods](https://github.com/Ronibuilds/CrimsonDesertMods), local source copy `D:\AI Agent Resources\GitHub\CrimsonDesertMods-main` — Crimson Desert modding source reference
- [FLiNG Trainer](https://flingtrainer.com/trainer/crimson-desert-trainer/) — Trainer reference
- [bbfox0703](https://www.nexusmods.com/crimsondesert/mods/64) — Cheat Engine table (1.12.02)

Detailed reverse-engineering notes are kept in:

- `docs/reverse-engineering/shield-hide.md`
- `docs/reverse-engineering/blood-decals.md`
- `docs/reverse-engineering/v1.13.00-handoff.md`
