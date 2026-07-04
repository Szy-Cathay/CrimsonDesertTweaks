# Blood Decals Lifetime Design

## Goal

Add a formal Blood Decals feature that extends Crimson Desert blood decal lifetime through the ASI mod.

The first implementation will only cover blood/decal persistence. Corpse persistence is intentionally out of scope until the removal/lifetime field is identified with stronger runtime evidence.

## User-Facing Behavior

The feature is controlled from the ASI INI:

```ini
[BloodDecals]
Enabled=1
LifetimeSeconds=600.0
ScanIntervalMs=1000
```

When enabled, the mod scans the game for the blood decal actor manager publish path, starts a background worker, and periodically extends validated blood decal objects to `LifetimeSeconds`.

If the scan or validation fails, the feature logs the reason and does not write memory.

## Reverse Engineering Evidence

Static evidence from the current `CrimsonDesert.exe`:

- `ClientActorManagerGlobal_P1_PublishStore` is unique at `0x1407126DA`.
- `ClientActorManagerGlobal_P2_ZeroInitPrologue` is unique at `0x14071263B`.
- `ClientActorManagerGlobal_P3_LeaCallBody` is unique at `0x1407127F9`.
- Ghidra function `CDT_BloodDecalActorManagerPublish` at `0x140712600` publishes and refreshes global object-list pointers around `0x1461E47F0`.
- `GameSkinnedDecalElement` and `SkinnedDecalElement` RTTI/type strings exist in the effect/decal area.
- The local reference ASI `CrimsonDesertBloodDecals.asi` logs `GameSkinnedDecalElement`, `life78`, and `fade80`, and appears to extend `object + 0x78` while leaving `object + 0x80` unchanged.

This is enough evidence to implement a guarded blood decal feature, but final visual confirmation still requires one in-game run.

## Non-Goals

- Do not implement corpse persistence in this change.
- Do not patch `fade80`.
- Do not copy build output to the game directory or mod directory.
- Do not alter existing Shield Hide behavior.
- Do not refactor unrelated scanner, logger, or config code.

## Module Design

Add a new independent hook/runtime module:

- `src/hooks/blood_decals.cpp`
  - Owns Blood Decals startup, worker thread, shutdown, and patch loop.
  - Exposes `InstallBloodDecalsHook()` and `RemoveBloodDecalsHook()` to match existing module style.

Update existing files:

- `src/scanner.h`
  - Declare `ScanForBloodDecalActorManagerPublish()`.
- `src/scanner.cpp`
  - Add Blood Decals AOB candidates and return the final instruction address.
- `src/config.h`
  - Add `BloodDecalsConfig`.
  - Add helper for whether the feature should start.
- `src/config.cpp`
  - Read `[BloodDecals]`.
  - Preserve `Enabled`/`Enable` alias behavior through `ReadBoolAlias`.
  - Sanitize `LifetimeSeconds` and `ScanIntervalMs`.
- `src/dllmain.cpp`
  - Log effective Blood Decals config.
  - Start Blood Decals after Shield Hide.
  - Stop Blood Decals during cleanup and detach.
- `CMakeLists.txt`
  - Add `src/hooks/blood_decals.cpp`.
- `PROGRESS.md`
  - Record implementation status and corpse investigation status while source work happens.

## Data Flow

1. Load config from `crimson-desert-tweaks.ini`.
2. If `[BloodDecals] Enabled=1`, scan for the blood decal actor manager publish pattern.
3. Resolve the RIP-relative globals used by the publish path.
4. Start a background worker.
5. Every `ScanIntervalMs`, read candidate object pointers from the resolved list data.
6. Validate each candidate.
7. If valid and `life78 < LifetimeSeconds`, write `LifetimeSeconds` to `object + 0x78`.

## Safety Rules

The worker must avoid blind writes:

- Do not start if the AOB scan is missing or ambiguous.
- Ignore pointers below `0x10000`.
- Wrap candidate reads and writes in SEH guards.
- Validate candidate layout before writing.
- Treat `object + 0x78` as a float lifetime field only when the current value is finite and within a reasonable range.
- Leave `object + 0x80` unchanged.
- Avoid verbose per-object logs unless `Verbose=1`.

The feature should fail closed: log and skip instead of writing uncertain memory.

## Logging

Expected log lines:

- Scan success or failure.
- Worker start and stop.
- Periodic totals when useful:
  - candidates
  - patched
  - already extended
  - skipped as unexpected
  - read failures
  - total patched

Normal logs should be concise. Diagnostic detail belongs behind `Verbose=1`.

## Verification

Static verification:

- Build `Release`.
- Confirm AOB scan patterns remain unique in current EXE.
- Confirm config alias handling still uses `ReadBoolAlias`.
- Confirm no build output is copied manually to the game or mod directory.

Runtime verification:

- Enable `Verbose=1` for the first run.
- Start the game once with Blood Decals enabled.
- Confirm logs show worker started and patch attempts or safe skips.
- Confirm visual blood decals persist longer if patches succeed.

## Corpse Persistence Follow-Up

Current corpse-related static findings:

- `DeadBody` appears in tag/name registry tables.
- `DeadActor` appears in event registry tables.
- `DeadCharacter` appears in registry/string tables.
- `RagdollDynamicDead` appears in physics/collision type strings.

These are useful navigation clues, but they do not identify the corpse removal timer or lifetime field. Corpse persistence should be designed only after dynamic tracing or stronger static evidence finds the actual object creation/removal path.
