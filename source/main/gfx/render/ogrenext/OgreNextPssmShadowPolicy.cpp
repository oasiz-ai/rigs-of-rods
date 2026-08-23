/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextPssmShadowPolicy.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

namespace RoR::Render {
namespace {

ValidationResult Unsupported(const char *field, const char *detail,
                             std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(ValidationCode::UNSUPPORTED_FEATURE, field,
                                   detail, index);
}

bool IsFiniteOrdered(const OgreNextPssmSplitPolicy &policy) noexcept {
  if (policy.cascade_count < 2U ||
      policy.cascade_count > kOgreNextPssmMaxCascadeCount) {
    return false;
  }
  for (std::size_t index = 0U; index <= policy.cascade_count; ++index) {
    if (!IsFinite(policy.split_points[index]) ||
        (index != 0U &&
         policy.split_points[index] <= policy.split_points[index - 1U])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index + 1U < policy.cascade_count; ++index) {
    if (!IsFinite(policy.blend_points[index])) {
      return false;
    }
  }
  return IsFinite(policy.fade_point);
}

/// Stage-3 quality defaults requested by the combined presenter: one more
/// cascade and a 1200 m shadow horizon over the same near plane, with the
/// second cascade promoted to 2048^2 so the 15-50 m band roughly doubles its
/// texel density relative to the V1 checkpoint.
constexpr std::uint32_t kOgreNextPssmModernCascadeCount = 4U;
constexpr float kOgreNextPssmModernFarMeters = 1200.0F;
constexpr float kOgreNextPssmModernLambda = 0.96F;
constexpr std::array<std::uint32_t, kOgreNextPssmMaxCascadeCount>
    kOgreNextPssmModernResolutions{{2048U, 2048U, 1024U, 1024U}};
constexpr std::array<std::uint32_t, kOgreNextPssmMaxCascadeCount>
    kOgreNextPssmLegacyResolutions{{2048U, 1024U, 1024U, 1024U}};
constexpr std::uint32_t kOgreNextPssmMaxAtlasDimension = 8192U;

std::atomic<bool> g_modern_shadow_defaults_requested{false};

bool IsPowerOfTwoResolution(std::uint32_t value) noexcept {
  return value >= 256U && value <= 4096U && (value & (value - 1U)) == 0U;
}

/// Deterministic shelf packing: cascade zero pins the atlas width; later
/// cascades fill left-to-right shelves in declaration order. Reproduces the
/// exact V1 layout (2048^2 above two 1024^2 tiles in one 2048x3072 atlas)
/// for the legacy resolution set.
bool PackCascadeShelves(const std::array<std::uint32_t,
                                         kOgreNextPssmMaxCascadeCount> &sizes,
                        std::uint32_t cascade_count,
                        OgreNextPssmShadowQualityConfig &config) noexcept {
  if (cascade_count < 2U || cascade_count > kOgreNextPssmMaxCascadeCount) {
    return false;
  }
  const std::uint32_t atlas_width = sizes[0U];
  std::uint32_t shelf_y = 0U;
  std::uint32_t shelf_height = 0U;
  std::uint32_t cursor_x = 0U;
  for (std::size_t index = 0U; index < cascade_count; ++index) {
    const std::uint32_t size = sizes[index];
    if (!IsPowerOfTwoResolution(size) || size > atlas_width) {
      return false;
    }
    if (cursor_x + size > atlas_width) {
      shelf_y += shelf_height;
      shelf_height = 0U;
      cursor_x = 0U;
    }
    config.layouts[index].width = size;
    config.layouts[index].height = size;
    config.layouts[index].atlas_x = cursor_x;
    config.layouts[index].atlas_y = shelf_y;
    cursor_x += size;
    shelf_height = std::max(shelf_height, size);
  }
  config.atlas_width = atlas_width;
  config.atlas_height = shelf_y + shelf_height;
  return config.atlas_width <= kOgreNextPssmMaxAtlasDimension &&
         config.atlas_height <= kOgreNextPssmMaxAtlasDimension;
}

bool ParseEnvUnsigned(const char *name, std::uint32_t minimum,
                      std::uint32_t maximum, std::uint32_t &value) noexcept {
  const char *raw = std::getenv(name);
  if (raw == nullptr || raw[0U] == '\0') {
    return false;
  }
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(raw, &end, 10);
  if (end == raw || *end != '\0' || parsed < minimum || parsed > maximum) {
    return false;
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool ParseEnvFloat(const char *name, float minimum, float maximum,
                   float &value) noexcept {
  const char *raw = std::getenv(name);
  if (raw == nullptr || raw[0U] == '\0') {
    return false;
  }
  char *end = nullptr;
  const float parsed = std::strtof(raw, &end);
  if (end == raw || *end != '\0' || !std::isfinite(parsed) ||
      parsed < minimum || parsed > maximum) {
    return false;
  }
  value = parsed;
  return true;
}

OgreNextPssmShadowQualityConfig ResolveShadowQualityConfig() noexcept {
  OgreNextPssmShadowQualityConfig config;
  std::array<std::uint32_t, kOgreNextPssmMaxCascadeCount> resolutions =
      kOgreNextPssmLegacyResolutions;
  const bool modern = g_modern_shadow_defaults_requested.load(
      std::memory_order_acquire);
  const char *legacy_raw = std::getenv("ROR_SHADOW_LEGACY");
  const bool legacy_forced =
      legacy_raw != nullptr && std::strcmp(legacy_raw, "1") == 0;
  if (modern && !legacy_forced) {
    config.cascade_count = kOgreNextPssmModernCascadeCount;
    config.far_meters = kOgreNextPssmModernFarMeters;
    config.lambda = kOgreNextPssmModernLambda;
    resolutions = kOgreNextPssmModernResolutions;
    std::uint32_t knob_unsigned = 0U;
    float knob_float = 0.0F;
    if (ParseEnvUnsigned("ROR_SHADOW_CASCADES", 2U,
                         kOgreNextPssmMaxCascadeCount, knob_unsigned)) {
      config.cascade_count = knob_unsigned;
    }
    if (ParseEnvFloat("ROR_SHADOW_FAR_M", 100.0F, 4000.0F, knob_float)) {
      config.far_meters = knob_float;
    }
    if (ParseEnvFloat("ROR_SHADOW_LAMBDA", 0.5F, 0.99F, knob_float)) {
      config.lambda = knob_float;
    }
    const char *resolution_raw = std::getenv("ROR_SHADOW_RES");
    if (resolution_raw != nullptr && resolution_raw[0U] != '\0') {
      std::array<std::uint32_t, kOgreNextPssmMaxCascadeCount> parsed =
          resolutions;
      const char *cursor = resolution_raw;
      bool valid = true;
      for (std::size_t index = 0U;
           valid && index < config.cascade_count; ++index) {
        char *end = nullptr;
        const unsigned long value = std::strtoul(cursor, &end, 10);
        if (end == cursor ||
            !IsPowerOfTwoResolution(static_cast<std::uint32_t>(value))) {
          valid = false;
          break;
        }
        parsed[index] = static_cast<std::uint32_t>(value);
        cursor = end;
        if (index + 1U < config.cascade_count) {
          if (*cursor != ',') {
            valid = false;
            break;
          }
          ++cursor;
        }
      }
      if (valid && *cursor == '\0') {
        resolutions = parsed;
      }
    }
  }
  if (!PackCascadeShelves(resolutions, config.cascade_count, config)) {
    // Fail closed onto the reviewed V1 checkpoint layout.
    config = OgreNextPssmShadowQualityConfig{};
    (void)PackCascadeShelves(kOgreNextPssmLegacyResolutions,
                             config.cascade_count, config);
  }
  if (modern) {
    std::fprintf(stderr,
                 "[RoR|PssmShadowConfig] cascades=%u far_m=%.1f lambda=%.3f "
                 "atlas=%ux%u res=%u,%u,%u,%u legacy_forced=%d\n",
                 config.cascade_count,
                 static_cast<double>(config.far_meters),
                 static_cast<double>(config.lambda), config.atlas_width,
                 config.atlas_height, config.layouts[0U].width,
                 config.layouts[1U].width, config.layouts[2U].width,
                 config.layouts[3U].width,
                 legacy_forced ? 1 : 0);
  }
  return config;
}

bool NearlyEqual(float lhs, float rhs) noexcept {
  constexpr float kTolerance = 1.0e-6F;
  return IsFinite(lhs) && IsFinite(rhs) &&
         std::fabs(lhs - rhs) <=
             kTolerance * std::max(1.0F, std::max(std::fabs(lhs),
                                                  std::fabs(rhs)));
}

} // namespace

bool NearlyEqualOgreNextPssmNativeTransformValue(float expected,
                                                  float observed) noexcept {
  // Node::setOrientation normalizes the quaternion and Ogre-Next's NEON/SIMD
  // world-AABB path evaluates the same admitted TRS with a different/FMA
  // ordering. Budget 64 binary32 epsilons only for that native readback.
  constexpr float kRelativeTolerance =
      64.0F * (std::numeric_limits<float>::epsilon)();
  return IsFinite(expected) && IsFinite(observed) &&
         std::fabs(expected - observed) <=
             kRelativeTolerance *
                 std::max(1.0F, std::max(std::fabs(expected),
                                         std::fabs(observed)));
}

bool IsKnownOgreNextDirectionalShadowMode(
    OgreNextDirectionalShadowMode mode) noexcept {
  switch (mode) {
  case OgreNextDirectionalShadowMode::DISABLED:
  case OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1:
    return true;
  }
  return false;
}

void RequestOgreNextPssmModernShadowQualityDefaults() noexcept {
  g_modern_shadow_defaults_requested.store(true, std::memory_order_release);
}

const OgreNextPssmShadowQualityConfig &
GetOgreNextPssmShadowQualityConfig() noexcept {
  static const OgreNextPssmShadowQualityConfig config =
      ResolveShadowQualityConfig();
  return config;
}

bool TryBuildOgreNextPssmSplitPolicy(
    OgreNextPssmSplitPolicy &output) noexcept {
  const OgreNextPssmShadowQualityConfig &config =
      GetOgreNextPssmShadowQualityConfig();
  const std::uint32_t cascade_count = config.cascade_count;
  const float far_meters = config.far_meters;
  const float lambda = config.lambda;
  if (cascade_count < 2U || cascade_count > kOgreNextPssmMaxCascadeCount ||
      !IsFinite(far_meters) || far_meters <= kOgreNextPssmNearMeters ||
      !IsFinite(lambda) || lambda <= 0.0F || lambda >= 1.0F) {
    return false;
  }
  OgreNextPssmSplitPolicy candidate;
  candidate.cascade_count = cascade_count;
  candidate.split_points[0U] = kOgreNextPssmNearMeters;
  for (std::size_t index = 1U; index < cascade_count; ++index) {
    const float fraction = static_cast<float>(index) /
                           static_cast<float>(cascade_count);
    const float logarithmic =
        kOgreNextPssmNearMeters *
        std::pow(far_meters / kOgreNextPssmNearMeters,
                 fraction);
    const float uniform =
        kOgreNextPssmNearMeters +
        fraction * (far_meters - kOgreNextPssmNearMeters);
    candidate.split_points[index] =
        lambda * logarithmic +
        (1.0F - lambda) * uniform;
    candidate.blend_points[index - 1U] =
        candidate.split_points[index] +
        (candidate.split_points[index - 1U] -
         candidate.split_points[index]) *
            kOgreNextPssmSplitBlend;
  }
  candidate.split_points[cascade_count] = far_meters;
  candidate.fade_point =
      candidate.split_points[cascade_count] +
      (candidate.split_points[cascade_count - 1U] -
       candidate.split_points[cascade_count]) *
          kOgreNextPssmSplitFade;
  if (!IsFiniteOrdered(candidate)) {
    return false;
  }
  for (std::size_t index = 0U; index + 1U < cascade_count; ++index) {
    if (!(candidate.blend_points[index] > candidate.split_points[index] &&
          candidate.blend_points[index] <
              candidate.split_points[index + 1U])) {
      return false;
    }
  }
  if (!(candidate.fade_point >
            candidate.split_points[cascade_count - 1U] &&
        candidate.fade_point < far_meters)) {
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
      kOgreNextExpectedViewFarMeters /
      (kOgreNextPssmNearMeters - kOgreNextExpectedViewFarMeters);
  const float expected_depth_offset =
      kOgreNextPssmNearMeters * kOgreNextExpectedViewFarMeters /
      (kOgreNextPssmNearMeters - kOgreNextExpectedViewFarMeters);
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
  // Stage 2: the light set may carry point/spot lights alongside the sun.
  // Those are lit through Forward+ and never substitute PSSM shadows, so
  // this gate requires exactly one directional light, that it casts, and
  // that every local light is explicitly shadowless.
  std::size_t directional_count = 0U;
  for (std::size_t index = 0U; index < snapshot.lights().size(); ++index) {
    const LightDescriptor &light = snapshot.lights()[index];
    if (light.type == LightType::DIRECTIONAL) {
      ++directional_count;
      if (light.shadow_flags == 0U) {
        return Unsupported(
            "lights.shadow_flags",
            "PSSM_3_CASCADE_V1 requires a nonzero static/dynamic geometry mask",
            index);
      }
    } else if (light.shadow_flags != 0U) {
      return Unsupported(
          "lights.shadow_flags",
          "PSSM_3_CASCADE_V1 does not substitute local-light shadows",
          index);
    }
  }
  if (directional_count != 1U) {
    return Unsupported(
        "lights",
        "PSSM_3_CASCADE_V1 requires exactly one shadow-casting directional light");
  }
  return ValidationResult::Success();
}

ValidationResult TryBuildOgreNextPssmShadowFramePlan(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    const CameraViewRequest &view,
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextDirectionalShadowMode shadow_mode,
    OgreNextPssmShadowFramePlan &output,
    bool defer_instance_counts_to_retained_scene) {
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
      view.far_plane != kOgreNextExpectedViewFarMeters) {
    return Unsupported(
        "views.clip_distance",
        "PSSM_3_CASCADE_V1 requires the exact pinned view near and far clip distances");
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
  // The scene validation above admitted exactly one directional light; the
  // remaining entries are shadowless local lights that this plan ignores.
  const auto sun_iterator = std::find_if(
      snapshot.lights().begin(), snapshot.lights().end(),
      [](const LightDescriptor &light_entry) {
        return light_entry.type == LightType::DIRECTIONAL;
      });
  if (sun_iterator == snapshot.lights().end()) {
    return Unsupported(
        "lights",
        "PSSM_3_CASCADE_V1 lost its directional light between validation and planning");
  }
  const LightDescriptor &light = *sun_iterator;
  candidate.enabled = true;
  candidate.shadow_light_id = light.light_id;
  candidate.native_visibility_mask =
      view.visibility_mask & kOgreNextPssmNativeVisibilityMask;
  if (defer_instance_counts_to_retained_scene) {
    // The frontend owns an exact retained native scene for the attested
    // predecessor and applies every patched descriptor transactionally. Its
    // O(changed) aggregates fill these three counts after the diff; all view,
    // light, mode, and projection checks above remain per-frame.
    output = candidate;
    return ValidationResult::Success();
  }
  for (std::size_t index = 0U; index < snapshot.mesh_instances().size();
       ++index) {
    const MeshInstanceDescriptor &instance = snapshot.mesh_instances()[index];
    // An instance the RT4/V1 presenter will not draw casts and receives
    // nothing. It is skipped there because the pinned PBS tangent path cannot
    // carry a non-uniform scale; counting it here would leave the plan's
    // aggregates permanently disagreeing with the retained scene, which the
    // frontend treats as a hard invariant violation. The severity split must
    // not turn one undrawable object into a different fatal error.
    if (raster_feature_tier ==
            OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1 &&
        !HasEffectivelyUniformLinearScale(instance.render_from_object)) {
      continue;
    }
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
