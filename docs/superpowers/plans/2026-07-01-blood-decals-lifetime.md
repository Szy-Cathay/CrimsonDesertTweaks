# Blood Decals Lifetime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a guarded Blood Decals feature that extends `GameSkinnedDecalElement` lifetime by writing only the `object + 0x78` float field.

**Architecture:** Add one independent runtime module that resolves the blood decal manager publish globals, starts a worker thread, validates decal objects through MSVC RTTI, and patches only confirmed decal objects. Configuration and logging follow the existing Shield Hide patterns.

**Tech Stack:** C++23, Windows API, CMake, existing scanner/config/logger helpers, current Ghidra/static evidence from `CrimsonDesert.exe`.

---

### File Structure

- Create: `src/hooks/blood_decals.cpp`
  - Owns scan resolution, worker thread, safe reads/writes, RTTI validation, and lifetime patching.
- Create: `crimson-desert-tweaks.ini`
  - Default local template with `[BloodDecals]` settings.
- Modify: `src/config.h`
  - Add `BloodDecalsConfig` and `ShouldInstallBloodDecalsHook`.
- Modify: `src/config.cpp`
  - Read `[BloodDecals]` using `ReadBoolAlias`.
  - Add float parsing and sanitize `LifetimeSeconds` / `ScanIntervalMs`.
- Modify: `src/scanner.h`
  - Declare `ScanForBloodDecalActorManagerPublish`.
- Modify: `src/scanner.cpp`
  - Add the blood decal publish-store AOB scan.
- Modify: `src/dllmain.cpp`
  - Log Blood Decals config, install/remove Blood Decals runtime.
- Modify: `src/config_watcher.cpp`
  - Include Blood Decals values in reload logs and restart-required detection.
- Modify: `CMakeLists.txt`
  - Add `src/hooks/blood_decals.cpp` to the target.
- Modify: `PROGRESS.md`
  - Record reverse-engineering evidence, implementation status, verification, and corpse follow-up boundary.

### Task 1: Config Surface

**Files:**
- Modify: `src/config.h`
- Modify: `src/config.cpp`
- Modify: `src/config_watcher.cpp`
- Create: `crimson-desert-tweaks.ini`
- Modify: `PROGRESS.md`

- [ ] **Step 1: Add config fields**

Add this structure to `src/config.h` after `ShieldConfig`:

```cpp
struct BloodDecalsConfig {
    bool enabled = false;
    float lifetime_seconds = 600.0f;
    DWORD scan_interval_ms = 1000;

    bool operator==(const BloodDecalsConfig&) const = default;
};
```

Add `BloodDecalsConfig blood_decals;` to `ModConfig`, and add:

```cpp
inline bool ShouldInstallBloodDecalsHook(const ModConfig& config) {
    return config.general.enabled && config.blood_decals.enabled;
}
```

- [ ] **Step 2: Add INI parsing**

In `src/config.cpp`, add a `ReadFloatIfPresent` helper based on `GetPrivateProfileStringW`, then read:

```cpp
ReadBoolAliasIfPresent(L"BloodDecals", L"Enabled", L"Enable", &next->blood_decals.enabled, path);
ReadFloatIfPresent(L"BloodDecals", L"LifetimeSeconds", &next->blood_decals.lifetime_seconds, path);
ReadDwordIfPresent(L"BloodDecals", L"ScanIntervalMs", &next->blood_decals.scan_interval_ms, path);
```

Sanitize to:

```cpp
if (!std::isfinite(next->blood_decals.lifetime_seconds) ||
    next->blood_decals.lifetime_seconds <= 0.0f) {
    next->blood_decals.lifetime_seconds = 600.0f;
} else if (next->blood_decals.lifetime_seconds > 86400.0f) {
    next->blood_decals.lifetime_seconds = 86400.0f;
}

if (next->blood_decals.scan_interval_ms < 100) {
    next->blood_decals.scan_interval_ms = 100;
} else if (next->blood_decals.scan_interval_ms > 60000) {
    next->blood_decals.scan_interval_ms = 60000;
}
```

- [ ] **Step 3: Update reload logging**

In `src/config_watcher.cpp`, include Blood Decals fields in the reload log and include `ShouldInstallBloodDecalsHook(previous) != ShouldInstallBloodDecalsHook(next)` in the restart-required condition.

- [ ] **Step 4: Add default INI template**

Create `crimson-desert-tweaks.ini` with:

```ini
[General]
Enabled=1
LogEnabled=1
Verbose=0
MaxLogLines=2000
InitDelayMs=3000

[Shield]
Enable=1

[BloodDecals]
Enabled=1
LifetimeSeconds=600.0
ScanIntervalMs=1000
```

- [ ] **Step 5: Verify config changes**

Run:

```powershell
rg -n "BloodDecals|ReadBoolAliasIfPresent|ShouldInstallBloodDecalsHook" src crimson-desert-tweaks.ini
```

Expected: Blood Decals config is present, and `Enabled` / `Enable` goes through `ReadBoolAliasIfPresent`.

### Task 2: Scanner Target

**Files:**
- Modify: `src/scanner.h`
- Modify: `src/scanner.cpp`
- Modify: `PROGRESS.md`

- [ ] **Step 1: Add scanner declaration**

Add to `src/scanner.h`:

```cpp
// Blood Decals scanner
uintptr_t ScanForBloodDecalActorManagerPublish();
```

- [ ] **Step 2: Add AOB scan**

Add `ScanForBloodDecalActorManagerPublish()` to `src/scanner.cpp` with these candidates:

```cpp
static constexpr PatternDefinition kPatterns[] = {
    {
        "blood-decals-publish-store-wide-1.12.02",
        "48 89 3D ?? ?? ?? ?? 48 8D 87 30 01 00 00 48 89 05 ?? ?? ?? ?? "
        "48 8D 87 B0 01 00 00 48 89 05 ?? ?? ?? ?? 44 88 2D ?? ?? ?? ?? "
        "8B 35 ?? ?? ?? ?? 39 35 ?? ?? ?? ?? 74 0E 8B D6 48 8D 0D ?? ?? ?? ?? "
        "E8 ?? ?? ?? ?? 39 35",
        0,
    },
    {
        "blood-decals-publish-store-exact-1.12.02",
        "48 89 3D 0F 21 AD 05 48 8D 87 30 01 00 00 48 89 05 09 21 AD 05 "
        "48 8D 87 B0 01 00 00 48 89 05 03 21 AD 05 44 88 2D 04 21 AD 05 "
        "8B 35 9E E9 96 05 39 35 08 21 AD 05 74 0E 8B D6 48 8D 0D F5 20 AD 05 "
        "E8 60 7A 00 00 39 35",
        0,
    },
};
```

If the scan is ambiguous or missing, log and return `0`, matching the Shield scanner style.

- [ ] **Step 3: Verify scanner references**

Run:

```powershell
rg -n "ScanForBloodDecalActorManagerPublish|blood-decals-publish-store" src/scanner.*
```

Expected: declaration and implementation exist.

### Task 3: Blood Decals Runtime

**Files:**
- Create: `src/hooks/blood_decals.cpp`
- Modify: `PROGRESS.md`

- [ ] **Step 1: Implement safe runtime module**

Create `src/hooks/blood_decals.cpp` with:

```cpp
#include "config.h"
#include "logger.h"
#include "scanner.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
```

The module must provide:

```cpp
bool InstallBloodDecalsHook();
void RemoveBloodDecalsHook();
```

It must resolve the publish-store global slots from the scanned instruction:

```cpp
manager_slot = ResolveRipRelativeTarget(publish_store + 0x00, 7);
list_a_slot = ResolveRipRelativeTarget(publish_store + 0x0E, 7);
list_b_slot = ResolveRipRelativeTarget(publish_store + 0x1C, 7);
```

- [ ] **Step 2: Implement validation before writes**

Use SEH guarded read/write helpers. Before writing:

1. Candidate pointer is above `0x10000`.
2. Candidate has MSVC RTTI containing `GameSkinnedDecalElement`.
3. `object + 0x78` and `object + 0x80` read as finite floats.
4. Only write `object + 0x78`.
5. Never write `object + 0x80`.

- [ ] **Step 3: Implement bounded enumeration**

Every tick:

1. Read published roots from `manager_slot`, `list_a_slot`, and `list_b_slot`.
2. For each root, scan a bounded root window for pointer slots.
3. For each vector-like view inside that root window, enumerate qword elements only when `begin <= end <= cap`, the span is qword-aligned, and the span is no larger than `0x40000`.
4. De-duplicate candidate object pointers within the tick.
5. Patch validated decals whose `life78` is lower than `LifetimeSeconds`.

- [ ] **Step 4: Implement worker lifecycle**

Use a worker thread plus stop event:

```cpp
g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
g_worker_thread = CreateThread(nullptr, 0, BloodDecalsThreadProc, nullptr, 0, nullptr);
```

`RemoveBloodDecalsHook()` must signal the event, wait for the worker, close handles, and log stop status.

- [ ] **Step 5: Verify module API**

Run:

```powershell
rg -n "InstallBloodDecalsHook|RemoveBloodDecalsHook|GameSkinnedDecalElement|life78|fade80" src/hooks/blood_decals.cpp
```

Expected: install/remove functions exist, RTTI type check exists, and only `life78` is written.

### Task 4: Integration

**Files:**
- Modify: `src/dllmain.cpp`
- Modify: `CMakeLists.txt`
- Modify: `PROGRESS.md`

- [ ] **Step 1: Wire DLL entry points**

Add forward declarations to `src/dllmain.cpp`:

```cpp
bool InstallBloodDecalsHook();
void RemoveBloodDecalsHook();
```

Log config:

```cpp
Log("config: BloodDecals Enabled=%d LifetimeSeconds=%.3f ScanIntervalMs=%lu",
    config.blood_decals.enabled ? 1 : 0,
    static_cast<double>(config.blood_decals.lifetime_seconds),
    static_cast<unsigned long>(config.blood_decals.scan_interval_ms));
```

Install after Shield Hide and remove during cleanup and detach.

- [ ] **Step 2: Add source file to build**

In `CMakeLists.txt`, add:

```cmake
    src/hooks/blood_decals.cpp
```

Do not add any command that copies files to the game directory or mod directory.

- [ ] **Step 3: Verify integration references**

Run:

```powershell
rg -n "BloodDecals|blood_decals.cpp|InstallBloodDecalsHook|RemoveBloodDecalsHook" src CMakeLists.txt
```

Expected: all integration points are present.

### Task 5: Verification

**Files:**
- Modify: `PROGRESS.md`

- [ ] **Step 1: Static scope check**

Run:

```powershell
git diff --stat
git diff -- src/hooks/blood_decals.cpp src/config.cpp src/config.h src/scanner.cpp src/scanner.h src/dllmain.cpp src/config_watcher.cpp CMakeLists.txt crimson-desert-tweaks.ini PROGRESS.md
```

Expected: no unrelated files changed.

- [ ] **Step 2: Build without copying to mod directory**

Configure build output override inside the workspace:

```powershell
cmake -S . -B build -A x64 -DCRIMSON_DESERT_TWEAKS_OUTPUT_DIR="D:/crimson-desert-tweaks/build/Release"
cmake --build build --config Release
```

Expected: build succeeds and any post-build copy target stays under `D:/crimson-desert-tweaks/build/Release`, not the game directory or mod directory.

- [ ] **Step 3: Final static checks**

Run:

```powershell
rg -n "fade80|\\+ 0x80|0x80" src/hooks/blood_decals.cpp
rg -n "ReadBoolAliasIfPresent\\(L\"BloodDecals\"" src/config.cpp
git status --short
```

Expected: `fade80` / `0x80` is read or logged only, Blood Decals bool alias uses `ReadBoolAliasIfPresent`, and git status contains only intended files before commit.

- [ ] **Step 4: Commit**

Run:

```powershell
git add docs/superpowers/plans/2026-07-01-blood-decals-lifetime.md crimson-desert-tweaks.ini CMakeLists.txt PROGRESS.md src
git commit -m "feat: add blood decals lifetime extension"
```

Expected: one feature commit on `codex/blood-decals-lifetime`.
