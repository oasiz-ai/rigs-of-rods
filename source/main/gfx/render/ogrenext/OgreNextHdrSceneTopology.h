/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Public identity for an Ogre-Next HDR scene-evaluation topology.

#pragma once

#include <cstdint>

namespace RoR::Render {

/// Exact scene-evaluation topology feeding Ogre's stock HDR post stack. The
/// split topology is retained for directional-visibility evidence. The
/// single-evaluation topology is the reviewed production raster/PSSM graph:
/// one RGBA16F RT4 scene pass and one R16F history input.
enum class OgreNextHdrSceneTopology : std::uint8_t {
  DIRECTIONAL_SPLIT_V2 = 0U,
  SINGLE_EVALUATION_PSSM_V1,
};

} // namespace RoR::Render
