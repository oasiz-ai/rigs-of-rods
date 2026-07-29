/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Exact in-place replacement policy for 18 CityWorld NeoQ trees.

#pragma once

#include "CityWorldNeoQ20Compatibility.h"

#include <cstddef>
#include <string>
#include <vector>

namespace RoR
{

struct CityWorldNeoQTreeCompatibilityResult
{
    bool applicable = false;
    bool applied = false;
    std::size_t replacement_count = 0U;
    std::string rejection_reason;
};

/// Validate and stage the complete NeoQueretaro tree-family replacement.
///
/// The exact 18 legacy `arbol1Qr` placements at authored source lines 9-26
/// keep their positions, types, instance names, X/Z rotations, and rendering
/// distances. Their ODEF names and Y rotations are replaced by the checked-in
/// deterministic family plan. The replacement uses one portable scale-wrapper
/// ODEF per instance, so visual and collision geometry share the same uniform
/// scale without adding a duplicate TOBJ placement.
///
/// Authentication requires the separately verified pinned CityWorld archive
/// dependency and the exact CityWorld.tobj SHA-256. Any mismatch leaves
/// `placements` byte-for-byte equivalent.
CityWorldNeoQTreeCompatibilityResult
ApplyCityWorldNeoQTreeCompatibility(
    const std::vector<std::string>& authored_dependencies,
    const std::string& tobj_name,
    const std::string& observed_tobj_sha256,
    std::vector<CityWorldNeoQ20Placement>& placements);

} // namespace RoR
