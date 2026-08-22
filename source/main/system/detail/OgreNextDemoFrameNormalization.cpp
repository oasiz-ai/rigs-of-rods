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

thread_local std::uint32_t g_capture_drawable_width = 0U;
thread_local std::uint32_t g_capture_drawable_height = 0U;

Render::ValidationResult Failure(Render::ValidationCode code,
                                 const char *field, const char *detail) {
  return Render::ValidationResult::Failure(code, field, detail);
}

} // namespace

float ResolveOgreNextDemoCaptureFarPlane(float native_far_plane) noexcept {
  return native_far_plane == 0.0F ? kOgreNextDemoCameraFarMeters
                                  : native_far_plane;
}

OgreNextDemoCaptureSurfaceScope::OgreNextDemoCaptureSurfaceScope(
    std::uint32_t drawable_width, std::uint32_t drawable_height) noexcept
    : previous_width_(g_capture_drawable_width),
      previous_height_(g_capture_drawable_height) {
  g_capture_drawable_width = drawable_width;
  g_capture_drawable_height = drawable_height;
}

OgreNextDemoCaptureSurfaceScope::~OgreNextDemoCaptureSurfaceScope() {
  g_capture_drawable_width = previous_width_;
  g_capture_drawable_height = previous_height_;
}

Render::ValidationResult CaptureOgreNextDemoDrawableAspect(float &aspect) {
  if (g_capture_drawable_width == 0U || g_capture_drawable_height == 0U ||
      g_capture_drawable_width > Render::kMaximumRenderDimension ||
      g_capture_drawable_height > Render::kMaximumRenderDimension) {
    return Failure(Render::ValidationCode::INVALID_DIMENSIONS,
                   "ogre_next_demo.capture_surface.extent",
                   "static admission requires the current child drawable extent");
  }
  const double candidate =
      static_cast<double>(g_capture_drawable_width) /
      static_cast<double>(g_capture_drawable_height);
  if (!std::isfinite(candidate) || !(candidate > 0.0) ||
      candidate > static_cast<double>((std::numeric_limits<float>::max)())) {
    return Failure(Render::ValidationCode::VALUE_OUT_OF_RANGE,
                   "ogre_next_demo.capture_surface.aspect",
                   "child drawable aspect is not finite and positive");
  }
  aspect = static_cast<float>(candidate);
  return Render::ValidationResult::Success();
}

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
  // Stage 2: the captured inventory carries the one shadow sun plus a
  // bounded set of shadowless point/spot lights for Forward+ clustered
  // shading. Exactly one directional caster remains mandatory; every local
  // light must be schema-valid and explicitly shadowless.
  constexpr float kHalfPi = 1.57079632679489661923F;
  std::size_t directional_count = 0U;
  std::size_t local_count = 0U;
  for (const Render::GraphicsSceneLightInput &light : lights) {
    if (light.source_light_id == 0U ||
        !Render::IsFinite(light.color_linear) ||
        !Render::IsNonNegative(light.color_linear) ||
        !Render::IsFinite(light.intensity) || light.intensity < 0.0F) {
      return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                     "ogre_next_demo.lights.photometry",
                     "captured demo lights require identities and finite nonnegative photometry");
    }
    if (light.type == Render::LightType::DIRECTIONAL) {
      ++directional_count;
      if (!(light.intensity > 0.0F) ||
          !Render::IsNormalized(light.direction) ||
          light.shadow_flags != Render::LIGHT_SHADOW_DEFAULT_FLAGS) {
        return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                       "ogre_next_demo.lights.main_shadow",
                       "captured demo light must be one visible directional caster for static and dynamic geometry");
      }
      continue;
    }
    if (light.type != Render::LightType::POINT &&
        light.type != Render::LightType::SPOT) {
      return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                     "ogre_next_demo.lights.type",
                     "the product demo transports directional, point, and spot lights only");
    }
    ++local_count;
    if (light.shadow_flags != 0U) {
      return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                     "ogre_next_demo.lights.local_shadow",
                     "captured local lights never substitute shadow maps");
    }
    if (!Render::IsFinite(light.range) || !(light.range > 0.0F) ||
        !Render::IsFinite(light.position)) {
      return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                     "ogre_next_demo.lights.local_geometry",
                     "captured local lights require finite positions and a positive range cutoff");
    }
    if (light.type == Render::LightType::SPOT &&
        (!Render::IsNormalized(light.direction) ||
         !Render::IsFinite(light.inner_cone_radians) ||
         !Render::IsFinite(light.outer_cone_radians) ||
         light.inner_cone_radians < 0.0F ||
         !(light.outer_cone_radians > 0.0F) ||
         light.outer_cone_radians < light.inner_cone_radians ||
         light.outer_cone_radians > kHalfPi)) {
      return Failure(Render::ValidationCode::UNSUPPORTED_FEATURE,
                     "ogre_next_demo.lights.cone",
                     "captured spot lights require a unit direction and ordered half-angles within pi/2");
    }
  }
  if (directional_count != 1U) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.lights.inventory",
                   "the product demo requires exactly one captured shadow sun");
  }
  if (local_count > 256U) {
    return Failure(Render::ValidationCode::SIZE_MISMATCH,
                   "ogre_next_demo.lights.inventory",
                   "captured local light count exceeds the Forward+ admission bound");
  }
  return Render::ValidationResult::Success();
}

} // namespace RoR::Detail
