# Blood Decals Reverse Engineering Notes

Last updated: 2026-07-04

This document records the confirmed technical route for the Blood Decals feature.
It is meant for fast repair after frequent Crimson Desert updates.

## Current Result

- Confirmed working: player-controlled character face/skinned blood lifetime is extended.
- Confirmed not enough: GroundBloodDecal hit-material `entry + 0x40` was
  patched, but user testing still showed short-lived ground spray.
- Implemented, pending in-game confirmation: GroundBloodDecal category-8
  `material + 0x5C` float seconds patch. Static flow shows this field feeds
  both `"ParticleLifeTime"` and `"ParticleLifeTimeMax"` in the entry parameter map.
- Rejected and removed: terrain object `+0x160` end-time write filtered through
  `object + 0x08 -> config + 0x12B` and `object + 0xA0` Blood key. Static
  analysis showed those fields were misidentified.
- Out of scope for this note: corpse lifetime and the original-game issue where body blood does not appear.

## Confirmed Skinned Blood Path

Target program:

- `D:\steam\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`
- Observed module base during the successful run: `0x140000000`
- Ghidra project: `D:\ghidra project\CrimsonDesert`
- Ghidra full analysis on 2026-07-03 reported `analyzed=true`, `analyzing=false`,
  `function_count=220524`.

Publish-store anchor:

- Function around `0x140712600`
- Current label in Ghidra: `CDT_BloodDecalActorManagerPublish`
- Hook instruction: `0x1407126DA`
- `0x1407126DA`: writes `rdi` to global manager slot `0x1461E47F0`
- `0x1407126E8`: writes `rdi + 0x130` to actor-array descriptor slot `0x1461E47F8`
- `0x1407126F6`: writes `rdi + 0x1B0` to another descriptor slot `0x1461E4800`

Primary object path:

```text
[0x1461E47F0] -> manager
manager + 0x58 -> ClientUserActor
ClientUserActor + 0x08 -> sub manager
sub manager + 0x38 -> current CCOIA
CCOIA + 0x68 -> effect owner
effect owner + 0x60 -> effect container
effect container + 0xA0 / +0x100 -> effect lists
```

Fallback path:

```text
[0x1461E47F8] or manager + 0x130 -> actor array descriptor
actor array entries -> CCOIA
CCOIA + 0x68 -> effect owner
effect owner + 0x60 -> effect container
effect container + 0xA0 / +0x100 -> effect lists
```

Current implementation also self-heals `manager -> ClientUserActor` by scanning around
`0x58 +/- 0x200` for RTTI containing `ClientUserActor@pa`.

## Confirmed Object Recognition

Do not treat the first qword of the object as a C string.
The object starts with a vtable pointer. Use MSVC RTTI:

```text
object[0] -> vtable
vtable[-1] -> RttiCompleteObjectLocator
COL.type_descriptor_rva -> TypeDescriptor
TypeDescriptor + 0x10 -> decorated type name
```

Confirmed skinned blood RTTI names in the current EXE:

- `.?AUGameSkinnedDecalElement@pa@@`
- `.?AUSkinnedDecalElement@pa@@`

The mod currently accepts names containing:

- `GameSkinnedDecalElement@pa`
- `SkinnedDecalElement@pa`
- both `Skinned` and `Decal`

## Confirmed Lifetime Fields

The reference ASI `D:\AI Agent Resources\GitHub\rumi_blood_mod\CrimsonDesertBloodDecals.asi`
and the successful in-game run both support this layout:

```text
object + 0x78 -> uint32 lifetime in milliseconds
object + 0x80 -> uint32 fade time in milliseconds
```

Known values:

- Default `life78 = 0xBB8 = 3000 ms`
- Default `fade80 = 0x5DC = 1500 ms`
- Reference ASI writes `life78 = 0x493E0 = 300000 ms`
- Current mod writes configured `LifetimeSeconds * 1000` to `object + 0x78`
- Current mod leaves `object + 0x80` unchanged

Important correction:

- `+0x78` is not a float seconds field.
- Do not reintroduce `SafeWriteFloat` for this feature.

## Current Log Evidence

Successful partial run:

```text
blood-decals: worker started publish=0x00000001407126DA manager-slot=0x00000001461E47F0 actor-array-slot=0x00000001461E47F8 ...
blood-decals: tick=146 roots=193 effectLists=4 candidates=320 patched=9 ...
...
blood-decals: tick=252 roots=184 effectLists=17 candidates=3232 patched=1 ... totalPatched=145
```

Interpretation:

- AOB scan and publish-store capture are correct.
- Effect-container traversal finds live objects.
- Skinned blood object identification and `+0x78` writing are correct.
- Ground sprayed blood is either a different object type or a different container/path.

Diagnostic run after enabling skipped RTTI sampling with `[General] Verbose=0`:

```text
blood-decals: DIAG skipped-rtti reason=timing name=".?AUSkinnedDecalDynamicProperty@pa@@" ... life78Ms=1152721928 fade80Ms=2096886528
blood-decals: DIAG skipped-rtti reason=timing name=".?AUGameSkinnedDecalElement@pa@@" ... life78Ms=1000 fade80Ms=0
```

Interpretation:

- The diagnostic build was installed correctly.
- The current skinned effect-container scan did not reveal a ground-blood RTTI type.
- No sampled candidate had the expected `3000 / 1500` blood timing pair.
- Ground sprayed blood should be treated as a terrain / particle decal path until proven otherwise.

## Ground Blood Clues

The current EXE contains these relevant strings:

- `GroundBloodDecal`
- `TargetGroundDecal`
- `TargetBodyDecal`
- `BodyBloodDecal`
- `BloodDecalInfo`
- `MaterialBloodDecalInfo`
- `BloodDecalData`
- `MaterialBloodDecalKey`
- `GPUParticleDecalFlags`
- `_isBloodParticle`
- `_renderDecalOnTerrain`

These are not yet confirmed lifetime fields or object types.
Treat them as navigation clues only.

Useful addresses in the current EXE:

- `GroundBloodDecal`: `0x144F446F8`
- `TargetGroundDecal`: `0x144F44710`
- `TargetBodyDecal`: `0x144F44750`
- `BodyBloodDecal`: `0x144F44790`
- `MaterialBloodDecalInfo` field strings start around `0x144CDB000`
- `BloodDecalData`: `0x144CDD8D0`
- `MaterialBloodDecalInfoManager` RTTI string: `0x145F4CA40`

The static strings above did not have direct Ghidra xrefs in this build.
They appear to live in reflection/data registration areas.

Additional static note:

- The `GroundBloodDecal` block belongs to hit-material fields near
  `WeaponHitRibbon`, `WeaponHitDecal`, `TargetGroundDecal`, and `BodyBloodDecal`.
- `MaterialBloodDecalInfo` exposes `_isBlocked`, `_stringKey`,
  `_bloodDecalVariationData`, and `_skillKey`, but no confirmed lifetime field.
- Particle decal fields include `_renderDecalOnTerrain`, `_isBloodParticle`, and
  `_gpuParticleDecalFlags`, so ground spray may be a GPU particle decal path rather
  than a `SkinnedDecalElement`.
- Direct xrefs to the plain strings were not available in Ghidra for this build;
  runtime RTTI sampling is the lowest-risk next evidence step.

Confirmed particle-emitter field offsets from `FUN_1429bdec0` registration stubs:

```text
_gpuParticleDecalFlags -> byte at emitter data + 0x119
_renderDecalOnTerrain  -> byte at emitter data + 0x12B
_isBloodParticle       -> byte at emitter data + 0x135
```

Tiny setters confirm the offsets by writing `byte ptr [RCX + offset]`.
These fields belong to a different particle / terrain-decal structure, not to
`GameSkinnedDecalElement + 0x78`.

Nearby lifetime-looking strings:

- `_particleLifeTimeMin`
- `_particleLifeTimeMax`
- `_particleLifeTime`
- `_effectLifeTime`

These strings are navigation clues only. They are not yet tied to the same runtime
object as `_isBloodParticle`, so they must not be written until the read/update path
is confirmed.

## Particle Lifetime Runtime Path

Static analysis on 2026-07-03 tied the lifetime-looking particle fields to a
runtime update path.

Confirmed update function:

```text
FUN_14069D5F0
runtime object = RDI
runtime + 0xD0 -> config holder
[runtime + 0xD0] -> emitter/config object
runtime + 0x20B -> enables the particle parameter block refresh
```

When `runtime + 0x20B` is non-zero, the function calls:

```text
0x14069DCC4: call FUN_140696FD0
RCX = runtime + 0x2B0
hook point after call: 0x14069DCC9
```

`FUN_140696FD0` maps the parameter block fields:

```text
runtime + 0x2B0 + 0x28 -> runtime + 0x2D8 -> _particleLifeTimeMin
runtime + 0x2B0 + 0x2C -> runtime + 0x2DC -> _particleLifeTimeMax
```

Additional static checks:

- `FUN_140698F80` reads `runtime + 0x2DC` in timing math.
- `FUN_14069AED0` reads nearby live particle generation parameters such as
  `runtime + 0x34C`, `+0x350`, and `+0x354`.
- Therefore `runtime + 0x2D8 / +0x2DC` are live particle runtime lifetime
  fields, not only reflection/editor metadata.

Important hook-timing correction:

- An earlier candidate hook at `0x14069D6EE` was too early.
- It runs before `FUN_140696FD0` refreshes the `runtime + 0x2B0` parameter
  block, so writes to `+0x2D8/+0x2DC` could be overwritten in the same update.
- The current scanner hooks after the refresh call.  Version-specific hook
  points:
  - v1.12.02: pattern `0x14069DCAD`, hook `0x14069DCC9`.
  - v1.13.00: pattern `0x1406AD92D`, hook `0x1406AD949`.

Current AOB:

```text
80 BF 0B 02 00 00 00 74 ?? 48 8D 8F B0 02 00 00
48 8D 95 80 01 00 00 E8 ?? ?? ?? ?? 48 8B 85 80 01 00 00
```

Hook offset: `0x1C`.

Window scan on v1.12.02:

```text
post-copy-wide  -> 1 match, addr=0x14069DCAD, hook=0x14069DCC9
post-copy-exact -> 1 match, addr=0x14069DCAD, hook=0x14069DCC9
```

v1.13.00 MCP byte-pattern scan:

```text
post-copy-wide -> 1 match, addr=0x1406AD92D, hook=0x1406AD949
```

v1.13.00 disassembly spot check:

```text
0x1406AD92D: cmp byte ptr [rdi+0x20B], 0
0x1406AD936: lea rcx, [rdi+0x2B0]
0x1406AD944: call FUN_1406A6C70
0x1406AD949: mov rax, [rbp+0x180] ; hook point after refresh call
```

Runtime patch condition:

```text
runtime + 0x20B != 0
[runtime + 0xD0] -> emitter/config is pointer-shaped
config + 0x12B (_renderDecalOnTerrain) is boolean
config + 0x135 (_isBloodParticle) is boolean
(terrain12B == 1 || blood135 == 1)
runtime + 0x2D8 / +0x2DC are finite floats
at least one lifetime value is positive
```

The OR condition is deliberate. The 2026-07-02 runtime hook captured true terrain
runtime objects with `terrain12B=1 blood135=0`, so requiring `_isBloodParticle`
would likely miss ground blood decals. This can extend some terrain particle
decals beyond blood; if that proves too broad, the next refinement should identify
a material/name/key pointer from the emitter config rather than reverting to
effect-list guessing.

Current write set:

```text
runtime + 0x2D8 -> float LifetimeSeconds
runtime + 0x2DC -> float LifetimeSeconds
```

Do not write:

```text
config + 0x119
config + 0x12B
config + 0x135
particle + 0x78
particle + 0x80
```

## Diagnostic Build Behavior

Current diagnostic behavior is intentionally conservative:

- Default behavior patches skinned decal objects and the confirmed particle
  runtime lifetime fields.
- Skipped candidates whose RTTI name contains `Decal` or `Blood` are sampled even when
  `[General] Verbose=0`; this is capped and should not spam the log.
- Each sampled line includes:
  - skip reason
  - RTTI name
  - object pointer
  - `life78`
  - `fade80`
- Sampling is capped at 32 unique `name + reason` pairs per process.

Expected diagnostic line:

```text
blood-decals: DIAG skipped-rtti reason=non-skinned name="..." ptr=0x... life78Ms=... fade80Ms=...
```

Particle / terrain-decal diagnostic line:

```text
blood-decals: DIAG particle-strict-candidate ptr=0x... gpuDecal119=0x.. terrain12B=.. blood135=.. bytes118="..."
```

This line is read-only. It samples offsets confirmed from particle registration
stubs and must not be treated as proof of a safe lifetime field by itself.
The strict diagnostic intentionally ignores low addresses, main-module static
addresses, and candidates where `_renderDecalOnTerrain` / `_isBloodParticle`
are not boolean-shaped bytes (`0` or `1`).

How to use it:

- If a non-skinned `Decal` / `Blood` RTTI sample appears with values near `3000 / 1500`,
  that is the next candidate for a tightly scoped whitelist expansion.
- If no relevant skipped RTTI appears while ground blood is visible, ground blood is probably
  outside the current effect-container lists and the next target is the terrain/particle decal path.
- If `DIAG particle-strict-candidate` appears with `blood135=1` or `terrain12B=1`,
  use the raw `bytes118` dump to continue locating the matching runtime lifetime /
  fade field before adding any write.
- If only the older `DIAG particle-candidate` style appears with non-boolean values
  such as `terrain12B=64/128/255`, treat it as a false positive from scanning the
  wrong candidate pool, not as a particle object.

## Latest Ground-Blood Triage

User test on 2026-07-02 with the first particle diagnostic produced 23
`DIAG particle-candidate` samples, but none had clean `blood135=1` or
`terrain12B=1`. Several samples were clearly not particle objects:

- Low or scalar-looking pseudo-pointers such as `0x000000003F800000`.
- Static main-module addresses such as `0x00000001450E7378`.
- Non-boolean field values such as `terrain12B=64/81/191/255` and
  `blood135=128/199/255`.

Interpretation:

- The current skinned effect-list scan can read arbitrary qwords from list
  storage and sometimes those qwords are not object pointers.
- Reading particle offsets from those candidates creates false positives.
- Ground sprayed blood should now be pursued from real particle/decal runtime
  code paths instead of expanding the skinned effect-list scan.

Confirmed code-level navigation points:

```text
0x14044A8B9: cmp byte ptr [RCX + 0x135], 0
0x1407E4FBC: cmp byte ptr [RCX + 0x135], 0
0x1435773FA: cmp byte ptr [RCX + 0x12B], 0
0x140BDA26C: mov byte ptr [RCX + 0x119], DL
```

These accesses are better reverse-engineering anchors than the reflection field
strings. The next useful reverse pass is to trace the `RCX` source in the two
`+0x135` checks and identify the runtime emitter/container that owns ground
blood particles.

Current-disk EXE recheck on 2026-07-02 showed address drift versus the first
Ghidra notes. Treat hard-coded addresses as examples only; the AOB signatures
are the portable part. Use scanner or dumpbin RVA/VA output, not raw file
offsets:

```text
_isBloodParticle check pattern unique hit:           0x1407E4FB4, hook at 0x1407E4FBC
_isBloodParticle simulation check pattern unique hit: 0x14044A8B2, hook at 0x14044A8B9
_renderDecalOnTerrain check pattern unique hit:      0x1435773F6, hook at 0x1435773FA
_gpuParticleDecalFlags setter pattern hit:           0x140BDA250 / field write at 0x140BDA26C
```

Note: an earlier quick Python byte scan added raw file offsets to the image
base and produced wrong addresses such as `0x1407E43B4`. Do not reuse those.

Runtime diagnostic hook line:

```text
blood-decals: DIAG particle-runtime source=blood135-check ptr=0x... q68=0x... q70=0x... q78=0x... d78=... d80=... gpuDecal119=0x.. terrain12B=.. blood135=.. bytes118="..."
```

This line is stronger evidence than `particle-strict-candidate`, because the
pointer comes directly from the game's own runtime `RCX` at the `+0x135` or
`+0x12B` checks. It is still read-only evidence. Do not write any particle
offset until the lifetime/fade field has been separately proven.

The 2026-07-02 test with runtime hooks installed produced two true terrain
runtime samples from `terrain12B-check`; both were `terrain12B=1 blood135=0`.
Their `d78/d80` values were not `3000/1500`-like (`16777217/1341456385` and
`16777217/1`), so `+0x78/+0x80` must not be treated as particle lifetime data.
No `blood135-check` runtime sample appeared in that run, so the next diagnostic
hook is `blood135-sim-check` at `0x14044A8B9`.

The 2026-07-03 static pass found the particle lifetime update path above. The
next game-side confirmation should look for:

```text
blood-decals: installed particle lifetime hook at 0x...
blood-decals: particle lifetime patch runtime=0x... config=0x... terrain12B=... blood135=... lifeMin ... lifeMax ...
blood-decals: worker stopped ... particleLifetimePatches=...
```

The 2026-07-04 test confirmed the hook installed but did not log any
`particle lifetime patch` line. The follow-up diagnostic build logs why the
callback returns:

```text
blood-decals: DIAG particle-lifetime reason=... hit=... runtime=0x... holder=0x... config=0x... uses20B=... terrain12B=... blood135=... lifeMin=... lifeMax=... target=...
blood-decals: worker stopped ... particleLifetimeHits=... particleLifetimePatches=...
```

Reason meanings:

- `invalid-runtime`: hook fired, but `RDI` was not pointer-shaped.
- `uses20B-read-failed` / `uses20B-off`: runtime parameter refresh flag was not readable or off.
- `config-read-failed`: `runtime + 0xD0 -> holder -> config` did not resolve.
- `flags-filter`: config did not look like blood/terrain decal particle data.
- `life-read-failed`: `runtime + 0x2D8/+0x2DC` was unreadable or not finite.
- `life-not-positive`: both lifetime floats were zero or negative.
- `already-extended`: both lifetime floats were already at or above the configured target.
- `write-failed`: the final float writes failed.

## GroundBloodDecal Hit-Material Entry Path

This was a useful static path, but it is not sufficient by itself.  User testing
after this hook still showed ground sprayed blood disappearing quickly.

Confirmed anchors:

```text
0x140548C2B: movzx edx, byte ptr [r15+0x11A]
0x140548C3A: call FUN_141FB1950
0x1405497F0: FUN_1405497F0 hit-material effect entry builder
0x140549DF4: call FUN_1405497F0 with 7th arg = 0x1C0 entry pointer
```

`FUN_141FB1950` uses `param_2` as the hit-material decal category selector.
Category `8` is the `GroundBloodDecal` / `TargetGroundDecal` bucket.  The entry
builder receives the material config in `R9`; therefore `R9 + 0x11A == 8` is the
runtime category filter for ground blood.

`FUN_1405497F0` receives the 0x1C0 entry pointer as the 7th x64 argument.  At a
hook placed on the function entry, the pointer is still at:

```text
[RSP + 0x38]
```

Inside the builder, the same value is later read as `[rsp+0x90]` after five
pushes and `sub rsp, 0x30`, which confirms the stack-offset calculation.  The
builder writes many entry fields (`+0x20`, `+0x24`, `+0x28`, `+0x78`, `+0xA0`,
`+0xA8`, `+0x1BA`) but not `+0x40`, so the entry hook write is not immediately
overwritten by this function.

Observed copy chain:

```text
0x1C0 entry + 0x40
  -> 0x70 active record + 0x40
  -> final object + 0x78
```

The final `+0x78` field is confirmed in-game for skinned/face blood as a uint32
millisecond lifetime.  However, terrain ground blood has an additional terrain
node visibility path, so copying to final `+0x78` is not enough evidence that it
controls the visible ground spray lifetime.

Stronger category-8 particle-parameter path found on v1.13.00:

```text
FUN_14055B020:
  R9 / param_4 = material config
  stack arg 7 / param_7 = 0x1C0 entry
  material + 0x11A == 8 -> GroundBloodDecal category
  call FUN_1408E8B70(entry + 0x78, material + 0x08, &DAT_144B2B73C)

FUN_1408E8B70:
  reads param_2 + 0x54
  writes "ParticleLifeTime"    to the parameter map
  writes "ParticleLifeTimeMax" to the parameter map

Therefore:
  material + 0x08 + 0x54 = material + 0x5C
  material + 0x5C is the category-8 particle lifetime seconds field.
```

Current category-8 hook writes both:

```text
material + 0x5C -> float LifetimeSeconds
entry    + 0x40 -> uint32 LifetimeSeconds * 1000
```

The `material + 0x5C` write happens at function entry, before
`FUN_1408E8B70` copies the value into the entry parameter map.  It is filtered
by `material + 0x11A == 8`, and does not write particle flags `+0x119/+0x12B/+0x135`.

Scanner:

```text
ground-blood-hit-material-entry-build-wide-1.12.02
48 89 5C 24 08 48 89 6C 24 18 89 54 24 10 56 57
41 54 41 56 41 57 48 83 EC 30 49 8B F1 48 8B E9
85 D2 74 ?? 4C 8D 44 24 68 48 8B D1
48 8B BC 24 90 00 00 00
```

v1.13.00 MCP byte-pattern scan:

```text
ground-blood-hit-material-entry-build-wide -> 1 match, addr=0x14055B020
```

v1.13.00 disassembly spot check:

```text
0x14055B020: mov [rsp+0x08], rbx
0x14055B036: sub rsp, 0x30
0x14055B03A: mov rsi, r9
0x14055B04C: mov rdi, [rsp+0x90] ; original 7th argument after prologue
```

Expected install and patch logs:

```text
scanner: DIAG pattern="ground-blood-hit-material-entry-build-wide-1.12.02" ...
blood-decals: installed ground blood hit-material entry hook at 0x...
blood-decals: worker started ... groundEntryHook=installed ... life40Ms=... groundParticleLifeSec=...
blood-decals: ground blood particle-life param patch hit=... material=0x... offset=0x5C seconds old->target
blood-decals: ground blood entry patch hit=... entry=0x... material=0x... category=8 life40Ms old->target
blood-decals: worker stopped ... groundEntryHits=... groundEntryPatches=... groundParticleParamPatches=...
```

Do not generalize this hook to all terrain decals.  The safety filter is the
category byte `material + 0x11A == 8`.  Do not write `entry + 0x80`, particle
flags `+0x119/+0x12B/+0x135`, or the generic particle runtime fields unless a
separate reverse pass proves they are required.

## Rejected Terrain Object Lifetime Path

This path is useful as reverse-engineering context, but it is not a current
implementation path. Do not reinstall a hook that writes terrain object
`+0x160` based on `object + 0x08 -> config + 0x12B` and `object + 0xA0` key
filtering.

Original reason it looked promising:

Consumer:

```text
FUN_143DFA040
0x143DFA0E0: r8 = [rdx]
0x143DFA0E3: movss xmm3, [r8+0x14]
0x143DFA0E9: movss xmm0, [r8+0x160]
0x143DFA0F2: maxss xmm0, [rdx+0x08]
0x143DFA0F7: subss xmm3, xmm0
0x143DFA180: cmp [node+0x148], r8
0x143DFA1A1: movss [node+0x198], xmm0
0x143DFA1B4: movss [node+0x19C], xmm1
0x143DFA1BF: update node+0x1A8 flags
```

Producer:

```text
FUN_143D83710
0x143D83742: xmm9 = [object+0x14]
...
0x143D83A29: addss xmm0, xmm9
0x143D83A31: movss [object+0x160], xmm0
0x143D83A39: hook point after the game write
```

Why the hook was rejected:

```text
FUN_143D9F150:
  - Allocates / initializes the terrain object.
  - In the 0x170-size branch it initializes:
    object + 0x158
    object + 0x15C
    object + 0x160
    object + 0x164
    object + 0x168
    object + 0x16C

FUN_143E05840:
  - Allocates a 0x210 helper with _Malloc_MemoryMgr_AK.
  - Stores that helper at object + 0x08.
  - Stores the terrain object back at helper + 0x200.
  - This makes object + 0x08 an AK/Wwise-style helper, not the particle
    emitter/config structure used by the confirmed +0x12B / +0x135 flags.

FUN_143D9EC00:
  - Searches existing terrain objects by comparing:
    candidate + 0x70 == param_1[0]
    candidate + 0x78 == param_1[1]
    candidate + 0x80 == param_1[2]
  - Therefore object + 0x70/+0x78/+0x80 are a three-part resource handle key.
```

The bad assumption was:

```text
object + 0x08  -> emitter/config object
config + 0x12B -> terrain decal bit
object + 0xA0  -> Blood resource/effect key
```

Do not use that assumption again without a new independent proof. The old AOB
can still locate the `object + 0x160` write for reference, but the current source
does not declare, scan, install, count, or log a terrain `+0x160` hook.

Removed scanner reference:

```text
terrain-blood-decal-lifetime-refresh-post-end-wide-1.12.02
0F 28 C6 F3 0F 11 B4 24 98 00 00 00 F3 41 0F 58 C1
48 8B CE F3 0F 11 83 60 01 00 00 E8 ?? ?? ?? ?? 8B F8 85 C0

hook offset: 0x1C
v1.12.02 reference: pattern addr 0x143D83A1D, hook 0x143D83A39
v1.13.00 reference: pattern addr 0x143D9E8DD, hook 0x143D9E8F9
```

v1.13.00 disassembly spot check:

```text
0x143D9E8E9: addss xmm0, xmm9
0x143D9E8F1: movss [rbx+0x160], xmm0
0x143D9E8F9: call FUN_143D84100 ; hook point after object+0x160 write
```

Better next direction:

```text
Continue from the GroundBloodDecal category-8 entry builder and resource handle
chain instead of guessing inside the terrain object:

FUN_14055B020  -> hit-material entry builder, material + 0x11A == 8
FUN_143D9EC00  -> terrain object lookup by +0x70/+0x78/+0x80 resource handle
FUN_143D9F090  -> nearby resource/object construction path
```

## Update Triage Checklist

After a Crimson Desert update breaks the feature:

1. Check the log for `blood-decals: worker started`.
2. If missing, re-scan the publish-store AOB and re-confirm the instruction around `0x1407126DA`.
3. Confirm the publish-store still writes:
   - manager slot
   - actor-array descriptor slot
   - optional second descriptor slot
4. Confirm `GameSkinnedDecalElement` / `SkinnedDecalElement` RTTI names still exist.
5. Confirm MSVC RTTI COL layout still works.
6. Run normally and confirm:
   - `roots > 0`
   - `effectLists > 0`
   - `candidates > 0`
   - either `patched > 0` or diagnostic skipped RTTI names explain why not
7. For ground blood, confirm the runtime diagnostic hooks install:
   - `installed runtime diagnostic hook blood135-sim-check`
   - `installed runtime diagnostic hook blood135-check`
   - `installed runtime diagnostic hook terrain12B-check`
8. Confirm the particle lifetime hook installs:
   - `installed particle lifetime hook`
   - scanner pattern `blood-particle-lifetime-post-copy-*` has exactly one match
9. Confirm the GroundBloodDecal hit-material entry hook installs:
   - `installed ground blood hit-material entry hook`
   - scanner pattern `ground-blood-hit-material-entry-build-*` has exactly one match
   - if it installs but no `ground blood particle-life param patch` appears,
     re-check `FUN_14055B020 -> FUN_1408E8B70` and `material + 0x5C`
   - if it installs but no `ground blood entry patch` appears, re-check
     `FUN_141FB1950` category `8` and the `material + 0x11A` field
10. Confirm no rejected terrain `+0x160` hook was reintroduced:
   - no `ScanForTerrainBloodDecalLifetimeRefresh`
   - no `terrain blood lifetime refresh hook`
   - no terrain `object + 0x160` write in `src/hooks/blood_decals.cpp`
11. If continuing ground blood work, trace the category-8 entry builder into the
   resource handle chain before adding any new write.
12. If `DIAG particle-runtime` appears, use those real runtime pointers for the
   next reverse pass instead of scanning arbitrary qwords from the skinned list.
13. Use `Verbose=1` only when extra per-tick detail is needed.
14. Only after these checks, change offsets or whitelist rules.

Avoid these known bad paths:

- Do not write `+0x80` unless a future reverse pass proves it is safe.
- Do not write floats to `+0x78`.
- Do not hook particle lifetime before `FUN_140696FD0`; the game can overwrite early writes.
- Do not remove the category-8 filter from the GroundBloodDecal entry hook.
- Do not re-enable the terrain `+0x160` hook based on `object + 0x08` or
  `object + 0xA0`; both were misidentified in the rejected path.
- Do not accept every RTTI name containing `Decal`; the renderer has many non-blood decals.
- Do not copy build output to the game directory from Codex.
