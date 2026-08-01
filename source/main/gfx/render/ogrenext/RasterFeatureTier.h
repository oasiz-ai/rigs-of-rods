/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Opt-in raster feature tiers for the isolated Ogre-Next frontend.

#pragma once

#include <cstdint>

namespace RoR::Render {

/// The default preserves the reviewed texture-free N1 adapter. RT4/V1 is an
/// explicit, renderer-portable textured-PBS admission and upload path; it does
/// not imply native ray tracing or modify the renderer-neutral frontend ABI.
enum class OgreNextRasterFeatureTier : std::uint8_t {
  STATIC_PBR_N1 = 0,
  MODERN_PBR_RT4_V1 = 1,
};

[[nodiscard]] constexpr bool
IsKnownOgreNextRasterFeatureTier(OgreNextRasterFeatureTier tier) noexcept {
  return tier == OgreNextRasterFeatureTier::STATIC_PBR_N1 ||
         tier == OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
}

} // namespace RoR::Render
