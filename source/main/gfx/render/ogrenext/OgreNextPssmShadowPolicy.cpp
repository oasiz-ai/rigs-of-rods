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
  const LightDescriptor &light = snapshot.lights().front();
  candidate.enabled = true;
  candidate.shadow_light_id = light.light_id;
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
