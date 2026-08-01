/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextPssmShadowPolicy.h"

#include <cmath>
#include <limits>

namespace RoR::Render {
namespace {

ValidationResult Unsupported(const char *field, const char *detail,
                             std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(ValidationCode::UNSUPPORTED_FEATURE, field,
                                   detail, index);
}

bool IsFiniteOrdered(const OgreNextPssmSplitPolicy &policy) noexcept {
  for (std::size_t index = 0U; index < policy.split_points.size(); ++index) {
    if (!IsFinite(policy.split_points[index]) ||
        (index != 0U &&
         policy.split_points[index] <= policy.split_points[index - 1U])) {
      return false;
    }
  }
  for (const float blend : policy.blend_points) {
    if (!IsFinite(blend)) {
      return false;
    }
  }
  return IsFinite(policy.fade_point);
}

bool NearlyEqual(float lhs, float rhs) noexcept {
  constexpr float kTolerance = 1.0e-6F;
  return IsFinite(lhs) && IsFinite(rhs) &&
         std::fabs(lhs - rhs) <=
             kTolerance * std::max(1.0F, std::max(std::fabs(lhs),
                                                  std::fabs(rhs)));
}

} // namespace

bool IsKnownOgreNextDirectionalShadowMode(
    OgreNextDirectionalShadowMode mode) noexcept {
  switch (mode) {
  case OgreNextDirectionalShadowMode::DISABLED:
  case OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1:
    return true;
  }
  return false;
}

bool TryBuildOgreNextPssmSplitPolicy(
    OgreNextPssmSplitPolicy &output) noexcept {
  OgreNextPssmSplitPolicy candidate;
  candidate.split_points[0U] = kOgreNextPssmNearMeters;
  for (std::size_t index = 1U; index < kOgreNextPssmCascadeCount; ++index) {
    const float fraction = static_cast<float>(index) /
                           static_cast<float>(kOgreNextPssmCascadeCount);
    const float logarithmic =
        kOgreNextPssmNearMeters *
        std::pow(kOgreNextPssmFarMeters / kOgreNextPssmNearMeters,
                 fraction);
    const float uniform =
        kOgreNextPssmNearMeters +
        fraction * (kOgreNextPssmFarMeters - kOgreNextPssmNearMeters);
    candidate.split_points[index] =
        kOgreNextPssmLambda * logarithmic +
        (1.0F - kOgreNextPssmLambda) * uniform;
    candidate.blend_points[index - 1U] =
        candidate.split_points[index] +
        (candidate.split_points[index - 1U] -
         candidate.split_points[index]) *
            kOgreNextPssmSplitBlend;
  }
  candidate.split_points[kOgreNextPssmCascadeCount] =
      kOgreNextPssmFarMeters;
  candidate.fade_point =
      candidate.split_points[kOgreNextPssmCascadeCount] +
      (candidate.split_points[kOgreNextPssmCascadeCount - 1U] -
       candidate.split_points[kOgreNextPssmCascadeCount]) *
          kOgreNextPssmSplitFade;
  if (!IsFiniteOrdered(candidate)) {
    return false;
  }
  for (std::size_t index = 0U; index < candidate.blend_points.size(); ++index) {
    if (!(candidate.blend_points[index] > candidate.split_points[index] &&
          candidate.blend_points[index] <
              candidate.split_points[index + 1U])) {
      return false;
    }
  }
  if (!(candidate.fade_point >
            candidate.split_points[kOgreNextPssmCascadeCount - 1U] &&
        candidate.fade_point < kOgreNextPssmFarMeters)) {
    return false;
  }
  output = candidate;
  return true;
}

bool TryBuildOgreNextPssmProjectionExtents(
    const Matrix4x4 &portable_projection,
    OgreNextPssmProjectionExtents &output) noexcept {
  // Column-major storage of the canonical right-handed [0,1] perspective
  // matrix. Off-centre C/D terms are admitted; shear, oblique, orthographic,
  // reversed-Z, and mismatched near/far projections remain fail-closed.
  constexpr std::array<std::size_t, 9U> kRequiredZeroIndices{
      1U, 2U, 3U, 4U, 6U, 7U, 12U, 13U, 15U};
  for (const std::size_t index : kRequiredZeroIndices) {
    if (portable_projection.elements[index] != 0.0F) {
      return false;
    }
  }
  const float horizontal_scale = portable_projection.elements[0U];
  const float vertical_scale = portable_projection.elements[5U];
  const float horizontal_offset = portable_projection.elements[8U];
  const float vertical_offset = portable_projection.elements[9U];
  const float expected_depth_scale =
      kOgreNextPssmFarMeters /
      (kOgreNextPssmNearMeters - kOgreNextPssmFarMeters);
  const float expected_depth_offset =
      kOgreNextPssmNearMeters * kOgreNextPssmFarMeters /
      (kOgreNextPssmNearMeters - kOgreNextPssmFarMeters);
  if (!(IsFinite(horizontal_scale) && horizontal_scale > 0.0F &&
        IsFinite(vertical_scale) && vertical_scale > 0.0F &&
        IsFinite(horizontal_offset) && IsFinite(vertical_offset)) ||
      portable_projection.elements[11U] != -1.0F ||
      !NearlyEqual(portable_projection.elements[10U],
                   expected_depth_scale) ||
      !NearlyEqual(portable_projection.elements[14U],
                   expected_depth_offset)) {
    return false;
  }

  OgreNextPssmProjectionExtents candidate;
  candidate.left = (horizontal_offset - 1.0F) / horizontal_scale;
  candidate.right = (horizontal_offset + 1.0F) / horizontal_scale;
  candidate.top = (vertical_offset + 1.0F) / vertical_scale;
  candidate.bottom = (vertical_offset - 1.0F) / vertical_scale;
  if (!(IsFinite(candidate.left) && IsFinite(candidate.right) &&
        IsFinite(candidate.top) && IsFinite(candidate.bottom) &&
        candidate.left < candidate.right &&
        candidate.bottom < candidate.top)) {
    return false;
  }
  output = candidate;
  return true;
}

ValidationResult ValidateOgreNextPssmInitialization(
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextDirectionalShadowMode shadow_mode) {
  if (!IsKnownOgreNextDirectionalShadowMode(shadow_mode)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "directional_shadow_mode",
                                     "unknown Ogre-Next shadow mode");
  }
  if (shadow_mode == OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1 &&
      raster_feature_tier !=
          OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
    return Unsupported(
        "directional_shadow_mode",
        "the PSSM checkpoint is opt-in and requires MODERN_PBR_RT4_V1");
  }
  OgreNextPssmSplitPolicy splits;
  if (shadow_mode == OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1 &&
      !TryBuildOgreNextPssmSplitPolicy(splits)) {
    return Unsupported("directional_shadow_mode",
                       "the fixed PSSM split policy is not representable");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateOgreNextPssmShadowScene(
    const SceneSnapshot &snapshot,
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextDirectionalShadowMode shadow_mode) {
  const ValidationResult initialization =
      ValidateOgreNextPssmInitialization(raster_feature_tier, shadow_mode);
  if (!initialization) {
    return initialization;
  }
  if (shadow_mode == OgreNextDirectionalShadowMode::DISABLED) {
    for (std::size_t index = 0U; index < snapshot.lights().size(); ++index) {
      if (snapshot.lights()[index].shadow_flags != 0U) {
        return Unsupported(
            "lights.shadow_flags",
            "directional shadows require the explicit PSSM_3_CASCADE_V1 mode",
            index);
      }
    }
    return ValidationResult::Success();
  }
  if (snapshot.lights().size() != 1U) {
    return Unsupported(
        "lights",
        "PSSM_3_CASCADE_V1 requires exactly one shadow-casting directional light");
  }
  const LightDescriptor &light = snapshot.lights().front();
  if (light.type != LightType::DIRECTIONAL) {
    return Unsupported(
        "lights.type",
        "PSSM_3_CASCADE_V1 does not substitute local-light shadows");
  }
  if (light.shadow_flags == 0U) {
    return Unsupported(
        "lights.shadow_flags",
        "PSSM_3_CASCADE_V1 requires a nonzero static/dynamic geometry mask");
  }
  return ValidationResult::Success();
}

ValidationResult TryBuildOgreNextPssmShadowFramePlan(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    const CameraViewRequest &view,
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextDirectionalShadowMode shadow_mode,
    OgreNextPssmShadowFramePlan &output) {
  const ValidationResult scene_validation =
      ValidateOgreNextPssmShadowScene(snapshot, raster_feature_tier,
                                      shadow_mode);
  if (!scene_validation) {
    return scene_validation;
  }

  OgreNextPssmShadowFramePlan candidate;
  if (shadow_mode == OgreNextDirectionalShadowMode::DISABLED) {
    output = candidate;
    return ValidationResult::Success();
  }

  if (view.near_plane != kOgreNextPssmNearMeters ||
      view.far_plane != kOgreNextPssmFarMeters) {
    return Unsupported(
        "views.clip_distance",
        "PSSM_3_CASCADE_V1 requires exact 0.5 m near and 350 m far clip distances");
  }
  if ((view.visibility_mask & kOgreNextPssmNativeVisibilityMask) == 0U) {
    return Unsupported(
        "views.visibility_mask",
        "PSSM_3_CASCADE_V1 requires at least one Ogre-representable portable visibility bit");
  }
  if (!TryBuildOgreNextPssmProjectionExtents(
          view.clip_from_view, candidate.projection_extents)) {
    return Unsupported(
        "views.clip_from_view",
        "PSSM_3_CASCADE_V1 requires a canonical finite perspective projection matching its fixed near/far planes");
  }
  const LightDescriptor &light = snapshot.lights().front();
  candidate.enabled = true;
  candidate.shadow_light_id = light.light_id;
  candidate.native_visibility_mask =
      view.visibility_mask & kOgreNextPssmNativeVisibilityMask;
  for (std::size_t index = 0U; index < snapshot.mesh_instances().size();
       ++index) {
    const MeshInstanceDescriptor &instance = snapshot.mesh_instances()[index];
    const MeshResourceDescriptor *mesh = registry.ResolveMesh(instance.mesh);
    if (mesh == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "mesh_instances.mesh",
          "PSSM caster classification lost its synchronized mesh", index);
    }
    if (MeshInstanceCastsShadowForLight(light, instance, *mesh)) {
      if (ClassifyShadowGeometry(*mesh) == ShadowGeometryClass::STATIC) {
        if (candidate.static_caster_count ==
            (std::numeric_limits<std::uint32_t>::max)()) {
          return Unsupported("mesh_instances",
                             "PSSM static caster count overflowed", index);
        }
        ++candidate.static_caster_count;
      } else {
        if (candidate.dynamic_caster_count ==
            (std::numeric_limits<std::uint32_t>::max)()) {
          return Unsupported("mesh_instances",
                             "PSSM dynamic caster count overflowed", index);
        }
        ++candidate.dynamic_caster_count;
      }
    }
    if ((instance.flags & MESH_INSTANCE_RECEIVES_SHADOW) != 0U) {
      if (candidate.receiver_count ==
          (std::numeric_limits<std::uint32_t>::max)()) {
        return Unsupported("mesh_instances",
                           "PSSM receiver count overflowed", index);
      }
      ++candidate.receiver_count;
    }
  }
  output = candidate;
  return ValidationResult::Success();
}

} // namespace RoR::Render
