# Shield Hide Hook Technical Notes

This document records the reverse-engineering knowledge behind the Shield Hide
feature. Keep it here so future Codex sessions can find the reference with:

```powershell
rg -n "Shield Hide|ShieldFakeEquip|FakeEquip" docs src PROGRESS.md
```

## Purpose

Shield Hide disables the visible back shield when the player switches to dual
wield. The game represents the displayed back shield as a FakeEquip entry named
`ShieldFakeEquip`.

The hook clears the FakeEquip entry `flags` field when the relevant game code
touches the ShieldFakeEquip global key. This hides the shield for the current
runtime session without permanently modifying game data.

## FakeEquip Entry Layout

The FakeEquip system uses entries that are 0x30 bytes each:

```text
FakeEquipTypeEntry:
  +0x00: name_ptr   -> self-reference pointer to +0x18 inline string
  +0x08: slot_type  int32, 15 = ShieldFakeEquip
  +0x0C: unknown    int32, usually -1
  +0x10: flags      int64, 1 = enabled, 0 = disabled
  +0x18: name_buf   inline string, for example "ShieldFakeEquip\0"
```

The implemented callback validates the candidate entry before writing:

- `slot_type == 15`
- `flags != 0`
- `name_ptr` points to `name_buf`
- `name_buf` starts with `ShieldFakeEquip`

## Hook Location

Original v1.12 hook target:

- AOB signature: `48 8B 05 0A 3B C3 04 48 8B 08`
- Hook offset: 7
- Hook instruction: `mov rcx, [rax]`
- Hook type: `SafetyHookMid`
- Scanner: `ScanForShieldFakeEquipAccess()` in `src/scanner.cpp`
- Hook file: `src/hooks/shield_hide.cpp`

The hook is installed at the instruction that loads the `FakeEquipTypeEntry`
pointer from the ShieldFakeEquip global key.

v1.13.00 update:

- New EXE SHA256: `EA2F5300B30BC890911AABBCB65D4AB9750D051F6D774D1CA364A501E85BC2E2`
- Old exact AOB: 0 matches.
- Replacement scanner: `reserve-slot-key-load-cluster-1.13.00`
- Pattern:
  ```text
  48 8B 05 ?? ?? ?? ?? 48 8B 08 48 85 C9 74 44 80 39 00 74 3F
  48 8D 55 67 E8 ?? ?? ?? ?? 8B 45 67 89 45 77
  48 8B 0D ?? ?? ?? ?? 48 83 C1 60 48 8D 55 77
  E8 ?? ?? ?? ?? 48 85 C0
  ```
- Hook offset: 7.
- v1.13.00 matches:
  - `0x1414E73CF`
  - `0x1414E7422`
  - `0x1414E7475`
  - `0x1414E74C8`
  - `0x1414E751B`
  - `0x1414E756E`
  - `0x1414E75C1`
  - `0x1414E7614`
  - `0x1414E7667`

The v1.13.00 scanner returns all matching fixed ReserveSlot/FakeEquip key loads.
`shield_hide.cpp` installs a hook on each match. The callback still requires
`entry + 0x08 == 15` before writing `entry + 0x10`, so non-Shield keys are
observed but skipped.

The July 4, 2026 runtime log recorded nine installed hooks at the match
addresses above plus the hook offset of 7.

v1.13.00 first-match disassembly spot check:

```text
0x1414E73CF: mov rax, [0x1460FE078]
0x1414E73D6: mov rcx, [rax]       ; hook point = match + 7
0x1414E73D9: test rcx, rcx
0x1414E73DC: jz 0x1414E7422
```

## Global Key

Known reference from the original analysis:

- ShieldFakeEquip global key RVA: `0x60E9C98`
- The key is in the `.data` section and stores a pointer to the main
  ShieldFakeEquip definition entry.
- The pointer is computed during game initialization through a hash/lookup path.
- Previous xref note: `0x140174E71`.

Treat hard-coded addresses as version-specific notes. Prefer the AOB scanner
when adapting to a new game build.

## Runtime Behavior

The hook fires when the game processes FakeEquip state, typically after an
equipment switch. The callback clears `flags` at `entry + 0x10`, causing the
back shield FakeEquip entry to be disabled.

This is a runtime-only change:

- It does not patch files on disk.
- It resets when the game restarts.
- It depends on the equipment/FakeEquip code path being executed.

## Known Limitation

If the player starts the game already dual wielding and the shield has already
rendered, the hook may not run immediately. Switching equipment once should
trigger the FakeEquip code path and hide the shield.

## Version Update Checklist

When a game update breaks Shield Hide:

1. Enable verbose logging in `crimson-desert-tweaks.ini`.
2. Confirm `InstallShieldHideHook()` runs.
3. Check whether `ScanForShieldFakeEquipAccess()` finds exactly one match.
4. If the old AOB fails, check whether
   `reserve-slot-key-load-cluster-1.13.00` finds the fixed ReserveSlot key-load
   cluster.
5. Re-confirm the hook instruction still loads the entry pointer into `RCX`.
6. Re-check the entry layout:
   - `+0x08` slot type
   - `+0x10` flags
   - `+0x18` inline name string
7. If using the multi-hook cluster, keep the `slot_type == 15` guard. Do not
   broaden the callback to write every ReserveSlot key.
8. Only after the layout is confirmed, update the scanner or callback.

## Related Files

- `src/hooks/shield_hide.cpp`
- `src/scanner.cpp`
- `src/scanner.h`
- `PROGRESS.md`
