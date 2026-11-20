#pragma once

#include <cstdint>
#include <vector>

// ── Shield Hide scanner ─────────────────────────────────────────────────

uintptr_t ScanForShieldFakeEquipAccess();
std::vector<uintptr_t> ScanForShieldFakeEquipAccesses();
