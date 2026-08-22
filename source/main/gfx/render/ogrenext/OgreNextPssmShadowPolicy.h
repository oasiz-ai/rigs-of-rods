/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-independent admission and constants for the first
///        Ogre-Next directional PSSM checkpoint.

#pragma once

#include "../RendererFrontend.h"
#include "RasterFeatureTier.h"

#include <array>
#include <cstdint>

namespace RoR::Render {

enum class OgreNextDirectionalShadowMode : std::uint8_t {
  DISABLED = 0,
  PSSM_3_CASCADE_V1 = 1,
};

constexpr std::uint32_t kOgreNextPssmShadowContractVersion = 1U;
constexpr std::uint32_t kOgreNextPssmCascadeCount = 3U;

// These values are deliberately fixed for the checkpoint. The shadow-enabled
// frontend rejects a view with different clip distances instead of silently
// deriving a different cascade distribution.
constexpr float kOgreNextPssmNearMeters = 0.5F;
constexpr float kOgreNextPssmFarMeters = 350.0F;
/// Static admission reaches far beyond the shadow range: reusing the PSSM
/// far as the admission far made every static object beyond ~500 m simply
/// not exist on the presenter, popping in as the camera approached. The
/// admission ball must cover the map's real sightlines; frustum culling on
/// the presenter keeps distant admitted objects cheap, and the retained
/// static scene keeps the larger walk a one-time cost per approach.
constexpr float kOgreNextDemoStaticAdmissionFarMeters = 12000.0F;
/// The one camera far plane every combined view carries. It matches the
/// admission far above so admitted distant content is actually visible:
/// with the previous 350 m view far, everything past the first block was
/// far-clipped and read as pop-in. Shadow split arithmetic stays bounded
/// by kOgreNextPssmFarMeters and is unaffected by the view far.
constexpr float kOgreNextExpectedViewFarMeters =
    kOgreNextDemoStaticAdmissionFarMeters;
constexpr float kOgreNextPssmLambda = 0.97F;
constexpr float kOgreNextPssmSplitBlend = 0.125F;
constexpr float kOgreNextPssmSplitPaddingMeters = 1.0F;
constexpr float kOgreNextPssmSplitFade = 0.313F;
constexpr float kOgreNextPssmXyPadding = 1.5F;
constexpr std::uint32_t kOgreNextPssmStableCascadeCount = 1U;
constexpr std::uint32_t kOgreNextPssmPcfKernelWidth = 4U;
constexpr float kOgreNextPssmMaterialConstantBias = 0.01F;
constexpr float kOgreNextPssmConstantBiasScale = 1.0F;
constexpr float kOgreNextPssmNormalOffsetBias = 168.0F;
constexpr float kOgreNextPssmAutoConstantBiasScale = 100.0F;
constexpr float kOgreNextPssmAutoNormalOffsetBiasScale = 4.0F;
// Keep PSSM on the same authored layer boundary as RT4. Bits 28 and 29 belong
// to the reflection/PCC capture pipeline, while Ogre reserves bits 30 and 31.
// None of those four internal layers may leak into a shadow-caster pass.
constexpr std::uint32_t kOgreNextPssmNativeVisibilityMask =
    kOgreNextRt4AuthoredVisibilityMask;

constexpr char kOgreNextPssmCapabilityUnsupportedDetail[] =
    "PSSM_3_CASCADE_V1 native capability gate rejected the required atlas "
    "dimensions or PCF4 texture-gather support";

// One D32_FLOAT atlas. Cascade zero occupies the full-width upper region;
// cascades one and two occupy the lower-left and lower-right regions.
constexpr std::uint32_t kOgreNextPssmAtlasWidth = 2048U;
constexpr std::uint32_t kOgreNextPssmAtlasHeight = 3072U;

struct OgreNextPssmCascadeLayout final {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t atlas_x = 0U;
  std::uint32_t atlas_y = 0U;
};

constexpr std::array<OgreNextPssmCascadeLayout,
                     kOgreNextPssmCascadeCount>
    kOgreNextPssmCascadeLayouts{{
        {2048U, 2048U, 0U, 0U},
        {1024U, 1024U, 0U, 2048U},
        {1024U, 1024U, 1024U, 2048U},
    }};

struct OgreNextPssmSplitPolicy final {
  std::array<float, kOgreNextPssmCascadeCount + 1U> split_points{};
  std::array<float, kOgreNextPssmCascadeCount - 1U> blend_points{};
  float fade_point = 0.0F;
};

/// Perspective side-plane tangents that remain valid when Ogre temporarily
/// changes the viewer camera's near/far distances for each PSSM split.
struct OgreNextPssmProjectionExtents final {
  float left = 0.0F;
  float right = 0.0F;
  float top = 0.0F;
  float bottom = 0.0F;
};

struct OgreNextPssmShadowFramePlan final {
  std::uint32_t version = kOgreNextPssmShadowContractVersion;
  bool enabled = false;
  std::uint64_t shadow_light_id = 0U;
  std::uint32_t static_caster_count = 0U;
  std::uint32_t dynamic_caster_count = 0U;
  std::uint32_t receiver_count = 0U;
  std::uint32_t native_visibility_mask = 0U;
  OgreNextPssmProjectionExtents projection_extents;
};

/// Compare a scalar portable transform result with the same value read back
/// after Ogre-Next evaluates its pinned native TRS/SIMD path. This deliberately
/// has a separate, small roundoff budget; it must not be used for local asset
/// authority or other renderer state.
[[nodiscard]] bool NearlyEqualOgreNextPssmNativeTransformValue(
    float expected, float observed) noexcept;

[[nodiscard]] bool IsKnownOgreNextDirectionalShadowMode(
    OgreNextDirectionalShadowMode mode) noexcept;

/// Reproduces the pinned Ogre PSSM split arithmetic using strict binary32.
/// The output is assigned only after every result is finite and ordered.
[[nodiscard]] bool TryBuildOgreNextPssmSplitPolicy(
    OgreNextPssmSplitPolicy &output) noexcept;

/// Admit only the canonical finite perspective matrix shape that Ogre can
/// reproduce through FET_TAN_HALF_ANGLES. This avoids a fixed custom
/// projection, whose near-plane extents do not follow PSSM split mutation.
[[nodiscard]] bool TryBuildOgreNextPssmProjectionExtents(
    const Matrix4x4 &portable_projection,
    OgreNextPssmProjectionExtents &output) noexcept;

[[nodiscard]] ValidationResult ValidateOgreNextPssmInitialization(
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextDirectionalShadowMode shadow_mode);

[[nodiscard]] ValidationResult ValidateOgreNextPssmShadowScene(
    const SceneSnapshot &snapshot,
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextDirectionalShadowMode shadow_mode);

/// Validates shadow admission and builds the exact per-frame caster/receiver
/// plan transactionally. DISABLED retains RT4's original zero-shadow policy.
[[nodiscard]] ValidationResult TryBuildOgreNextPssmShadowFramePlan(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    const CameraViewRequest &view,
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextDirectionalShadowMode shadow_mode,
    OgreNextPssmShadowFramePlan &output,
    bool defer_instance_counts_to_retained_scene = false);

} // namespace RoR::Render
