/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Deterministic temporal contract for the pinned Ogre-Next HDR path.

#pragma once

#include "../HdrReference.h"
#include "../RenderFrame.h"
#include "RasterFeatureTier.h"

#include <cstdint>

namespace RoR::Render {

constexpr std::uint32_t kOgreNextHdrTemporalContractVersion = 1U;

/// Versioned settings for the first Ogre-Next auto-exposure and bloom path.
///
/// This structure intentionally contains no renderer-native handles. The
/// native compositor must consume the resulting frame plan unchanged on
/// Metal, D3D11, and Vulkan.
struct OgreNextHdrTemporalConfiguration final {
  std::uint32_t version = kOgreNextHdrTemporalContractVersion;
  float minimum_auto_exposure = -2.5F;
  float maximum_auto_exposure = 2.5F;
  float bloom_minimum_threshold = 3.0F;
  float bloom_full_colour_threshold = 5.0F;
  /// Exact initial contents of Ogre's persistent R16_FLOAT `oldLumRt`.
  float initial_inverse_luminance = 0.01F;
};

/// One immutable, renderer-neutral set of parameters for the pinned upstream
/// HDR compositor. `ogre_exposure` is the natural-log parameter consumed by
/// Ogre's `1024 * exp(exposure - 2)` shader equation. It is derived from the
/// renderer-boundary dimensionless effective exposure without changing the
/// public camera contract.
struct OgreNextHdrTemporalFramePlan final {
  std::uint32_t version = kOgreNextHdrTemporalContractVersion;
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  double simulation_time_seconds = 0.0;
  float effective_exposure = 1.0F;
  float ogre_exposure = 0.0F;
  float minimum_auto_exposure = -2.5F;
  float maximum_auto_exposure = 2.5F;
  float bloom_minimum_threshold = 3.0F;
  float bloom_full_colour_threshold = 5.0F;
  float bloom_inverse_transition_width = 0.5F;
  float delta_seconds = 0.0F;
  HdrR16Float previous_inverse_luminance_r16{};
};

/// Persistent history for one frontend lifetime.
///
/// PrepareFrame is read-only and CommitFrame is transactional. The first
/// frame uses delta zero so initialization is independent of launch time. Each
/// later delta comes only from immutable simulation timestamps. CommitFrame
/// requires the native one-pixel R16 readback to equal the pinned shader
/// oracle bit-for-bit before advancing history.
class OgreNextHdrTemporalState final {
public:
  [[nodiscard]] ValidationResult
  Initialize(const OgreNextHdrTemporalConfiguration &configuration);

  [[nodiscard]] ValidationResult PrepareFrame(
      const RenderFrameRequest &request,
      OgreNextRasterFeatureTier raster_feature_tier,
      OgreNextHdrTemporalFramePlan &output) const;

  [[nodiscard]] ValidationResult CommitFrame(
      const OgreNextHdrTemporalFramePlan &plan,
      float average_log_luminance,
      const HdrR16Float &native_stored_inverse_luminance);

  void Reset() noexcept;

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] std::uint64_t committed_frame_id() const noexcept {
    return committed_frame_id_;
  }
  [[nodiscard]] HdrR16Float previous_inverse_luminance() const noexcept {
    return previous_inverse_luminance_;
  }

private:
  OgreNextHdrTemporalConfiguration configuration_{};
  HdrR16Float previous_inverse_luminance_{};
  std::uint64_t committed_frame_id_ = 0U;
  double committed_simulation_time_seconds_ = 0.0;
  bool initialized_ = false;
};

} // namespace RoR::Render
