/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Exact curb-opening repair for CityWorld's east Penguinville road.

#pragma once

#include "CityWorldNeoQ20Compatibility.h"

#include <cstddef>
#include <string>
#include <vector>

namespace RoR
{

struct CityWorldPenguinRoadCompatibilityResult
{
    bool applicable = false;
    bool applied = false;
    std::size_t replacement_count = 0U;
    std::string rejection_reason;
};

/// Replace exactly one authenticated curb-bearing T-junction in place.
///
/// The pinned CityWorld.tobj source placement at line 1354 is changed from
/// `troadavenuesidewalk` to the archive's own
/// `crossroadavenuesidewalk`. Position, rotation, type, instance identity,
/// and every unrelated placement remain unchanged. The replacement opens the
/// east road mouth without layering a second collision mesh over the curb.
///
/// Authentication requires the pinned CityWorld dependency, TOBJ name and
/// SHA-256. Any mismatch leaves `placements` byte-for-byte equivalent.
CityWorldPenguinRoadCompatibilityResult
ApplyCityWorldPenguinRoadCompatibility(
    const std::vector<std::string>& authored_dependencies,
    const std::string& tobj_name,
    const std::string& observed_tobj_sha256,
    std::vector<CityWorldNeoQ20Placement>& placements);

} // namespace RoR
