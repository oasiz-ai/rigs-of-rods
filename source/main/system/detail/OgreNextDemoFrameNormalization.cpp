/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "OgreNextDemoFrameNormalization.h"

#include "render/RenderFrame.h"

#include <cmath>
#include <limits>

namespace RoR::Detail {
namespace {

Render::ValidationResult Failure(Render::ValidationCode code,
                                 const char *field, const char *detail) {
  return Render::ValidationResult::Failure(code, field, detail);
}

} // namespace

Render::ValidationResult NormalizeOgreNextDemoCamera(
    Render::GraphicsSceneCameraInput &camera,
    std::uint32_t drawable_width, std::uint32_t drawable_height) {
  if (camera.width == 0U || camera.height == 0U ||
      camera.width > Render::kMaximumRenderDimension ||
      camera.height > Render::kMaximumRenderDimension ||
      drawable_width == 0U || drawable_height == 0U ||
      drawable_width > Render::kMaximumRenderDimension ||
      drawable_height > Render::kMaximumRenderDimension) {
    return Failure(Render::ValidationCode::INVALID_DIMENSIONS,
                   "ogre_next_demo.camera.extent",
                   "captured and drawable camera extents must be in [1, 65535]");
  }
  if (!Render::IsFinite(camera.clip_from_view) ||
      !Render::IsFinite(camera.near_plane) ||
      !Render::IsFinite(camera.far_plane) ||
      !Render::IsCanonicalProjection(camera.clip_from_view,
                                     camera.near_plane,
                                     camera.far_plane) ||
      camera.clip_from_view.elements[11U] != -1.0F ||
      camera.clip_from_view.elements[15U] != 0.0F) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.camera.projection",
                   "the product demo requires one finite canonical perspective camera");
  }

  const double new_aspect = static_cast<double>(drawable_width) /
                            static_cast<double>(drawable_height);
  const double normalized_m00 =
      static_cast<double>(camera.clip_from_view.elements[5U]) / new_aspect;
  if (!std::isfinite(new_aspect) || !(new_aspect > 0.0) ||
      !std::isfinite(normalized_m00) || !(normalized_m00 > 0.0) ||
      normalized_m00 >
          static_cast<double>((std::numeric_limits<float>::max)())) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.camera.aspect",
                   "camera aspect normalization is not finite and positive");
  }

  Render::GraphicsSceneCameraInput candidate = camera;
  candidate.width = drawable_width;
  candidate.height = drawable_height;
  candidate.near_plane = kOgreNextDemoCameraNearMeters;
  candidate.far_plane = kOgreNextDemoCameraFarMeters;
  candidate.clip_from_view.elements[0U] =
      static_cast<float>(normalized_m00);
  candidate.clip_from_view.elements[10U] =
      candidate.far_plane / (candidate.near_plane - candidate.far_plane);
  candidate.clip_from_view.elements[14U] =
      candidate.near_plane * candidate.clip_from_view.elements[10U];
  if (!Render::IsCanonicalProjection(candidate.clip_from_view,
                                     candidate.near_plane,
                                     candidate.far_plane)) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.camera.normalized_projection",
                   "normalized camera is not a canonical perspective projection");
  }
  camera = candidate;
  return Render::ValidationResult::Success();
}

Render::ValidationResult ValidateOgreNextDemoShadowLights(
    const std::vector<Render::GraphicsSceneLightInput> &lights) {
  if (lights.size() != 1U) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.lights.inventory",
                   "the product demo requires exactly one captured shadow sun");
  }
  const Render::GraphicsSceneLightInput &light = lights.front();
  if (light.source_light_id == 0U ||
      light.type != Render::LightType::DIRECTIONAL ||
      !Render::IsFinite(light.color_linear) ||
      !Render::IsNonNegative(light.color_linear) ||
      !Render::IsFinite(light.intensity) || !(light.intensity > 0.0F) ||
      !Render::IsNormalized(light.direction) ||
      light.shadow_flags != Render::LIGHT_SHADOW_DEFAULT_FLAGS) {
    return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                   "ogre_next_demo.lights.main_shadow",
                   "captured demo light must be one visible directional caster for static and dynamic geometry");
  }
  return Render::ValidationResult::Success();
}

} // namespace RoR::Detail
