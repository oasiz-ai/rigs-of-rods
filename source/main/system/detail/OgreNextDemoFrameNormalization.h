/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Disposable product-only frame normalization for the first demo.

#pragma once

#include "render/GraphicsSceneSnapshotProducer.h"

#include <cstdint>

namespace RoR::Detail {

constexpr float kOgreNextDemoCameraNearMeters = 0.5F;
constexpr float kOgreNextDemoCameraFarMeters = 350.0F;

/// Rebuilds only the captured frame copy. The native OGRE Camera and user
/// settings are never mutated. On failure, camera remains byte-for-byte owned
/// by the caller and unchanged.
[[nodiscard]] Render::ValidationResult NormalizeOgreNextDemoCamera(
    Render::GraphicsSceneCameraInput &camera,
    std::uint32_t drawable_width, std::uint32_t drawable_height);

/// The first product build supports the one PSSM sun selected by GfxScene.
[[nodiscard]] Render::ValidationResult ValidateOgreNextDemoShadowLights(
    const std::vector<Render::GraphicsSceneLightInput> &lights);

} // namespace RoR::Detail
