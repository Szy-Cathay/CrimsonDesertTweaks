#pragma once

#include <cstdint>
#include <vector>

// ── Shield Hide scanner ─────────────────────────────────────────────────

uintptr_t ScanForShieldFakeEquipAccess();
std::vector<uintptr_t> ScanForShieldFakeEquipAccesses();

// ── Blood Decals scanner ────────────────────────────────────────────────

uintptr_t ScanForBloodDecalActorManagerPublish();
uintptr_t ScanForBloodParticleSimulationCheck();
uintptr_t ScanForBloodParticleRuntimeCheck();
uintptr_t ScanForTerrainParticleRuntimeCheck();
uintptr_t ScanForBloodParticleLifetimeUpdate();
uintptr_t ScanForGroundBloodHitMaterialEffectEntryBuild();
