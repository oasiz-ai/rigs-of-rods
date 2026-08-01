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

struct OgreNextPssmShadowFramePlan final {
  std::uint32_t version = kOgreNextPssmShadowContractVersion;
  bool enabled = false;
  std::uint64_t shadow_light_id = 0U;
  std::uint32_t static_caster_count = 0U;
  std::uint32_t dynamic_caster_count = 0U;
  std::uint32_t receiver_count = 0U;
};

[[nodiscard]] bool IsKnownOgreNextDirectionalShadowMode(
    OgreNextDirectionalShadowMode mode) noexcept;

/// Reproduces the pinned Ogre PSSM split arithmetic using strict binary32.
/// The output is assigned only after every result is finite and ordered.
[[nodiscard]] bool TryBuildOgreNextPssmSplitPolicy(
    OgreNextPssmSplitPolicy &output) noexcept;

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
    OgreNextPssmShadowFramePlan &output);

} // namespace RoR::Render
