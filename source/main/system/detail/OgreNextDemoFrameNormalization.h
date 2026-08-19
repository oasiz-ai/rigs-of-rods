/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Disposable product-only frame normalization for the first demo.

#pragma once

#include "render/GraphicsSceneSnapshotProducer.h"
#include "render/ogrenext/OgreNextPssmShadowPolicy.h"

#include <cstdint>

namespace RoR::Detail {

constexpr float kOgreNextDemoCameraNearMeters = 0.5F;
/// Aliases the render-layer pinned view far so the normalized camera is
/// bit-exactly what the PSSM view validation expects. The previous 350 m
/// far clipped everything past the first city block, which presented as
/// pop-in no matter how far static admission reached.
constexpr float kOgreNextDemoCameraFarMeters =
    Render::kOgreNextExpectedViewFarMeters;

/// Resolve OGRE 14's exact zero far-clip sentinel to the finite product
/// capture range. Every other value is preserved for the ordinary camera
/// contract to validate; this must not sanitize malformed native state.
[[nodiscard]] float
ResolveOgreNextDemoCaptureFarPlane(float native_far_plane) noexcept;

/// Publishes the exact child drawable extent only for the synchronous joined
/// capture performed by RendererOgre14ProductSession. The scope is private to
/// this disposable product bridge and restores any enclosing capture state.
class OgreNextDemoCaptureSurfaceScope final {
public:
  OgreNextDemoCaptureSurfaceScope(std::uint32_t drawable_width,
                                  std::uint32_t drawable_height) noexcept;
  ~OgreNextDemoCaptureSurfaceScope();

  OgreNextDemoCaptureSurfaceScope(const OgreNextDemoCaptureSurfaceScope &) =
      delete;
  OgreNextDemoCaptureSurfaceScope &
  operator=(const OgreNextDemoCaptureSurfaceScope &) = delete;

private:
  std::uint32_t previous_width_ = 0U;
  std::uint32_t previous_height_ = 0U;
};

/// Reads the drawable aspect belonging to the current synchronous capture.
/// Failure leaves `aspect` unchanged.
[[nodiscard]] Render::ValidationResult
CaptureOgreNextDemoDrawableAspect(float &aspect);

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
