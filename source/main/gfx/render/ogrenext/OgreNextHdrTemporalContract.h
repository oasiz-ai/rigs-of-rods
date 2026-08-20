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

constexpr std::uint32_t kOgreNextHdrTemporalContractVersion = 2U;
constexpr const char kOgreNextHdrHistoryValidationMode[] =
    "native_authoritative_conditioning_plus_one_r16_ulp_v2";

enum class OgreNextHdrHistoryValidationMode : std::uint8_t {
  NONE = 0,
  NATIVE_AUTHORITATIVE_CONDITIONING_PLUS_ONE_R16_ULP,
};

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
  /// Exact upstream initial contents of persistent R16_FLOAT `oldLumRt`.
  /// Version 2 accepts only 0.01 because the pinned compositor owns that clear.
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

/// Evidence from the most recently committed native exposure-history sample.
/// The native R16 value is authoritative after this bounded comparison passes.
struct OgreNextHdrHistoryComparison final {
  std::uint32_t version = kOgreNextHdrTemporalContractVersion;
  OgreNextHdrHistoryValidationMode mode =
      OgreNextHdrHistoryValidationMode::NONE;
  HdrR16Float native_inverse_luminance_r16{};
  HdrR16Float reference_inverse_luminance_r16{};
  /// Exact shader-domain inputs used to derive the independently reproducible
  /// CPU oracle for this committed frame.
  float ogre_exposure = 0.0F;
  float minimum_auto_exposure = 0.0F;
  float maximum_auto_exposure = 0.0F;
  float average_log_luminance = 0.0F;
  HdrR16Float previous_inverse_luminance_r16{};
  float delta_seconds = 0.0F;
  double absolute_error = 0.0;
  double allowed_error = 0.0;
  double conditioning_bound = 0.0;
  double binary32_rounding_bound = 0.0;
  double storage_ulp = 0.0;
  std::uint32_t r16_ulp_distance = 0U;
  bool accepted = false;
};

/// Persistent history for one frontend lifetime.
///
/// PrepareFrame is read-only and PrepareCommit validates a candidate without
/// publishing it. The caller may commit that candidate only after all other
/// frame participants can commit, or abort it without changing any committed
/// accessor. The first frame uses delta zero so initialization is independent
/// of launch time. Each later delta comes only from immutable simulation
/// timestamps. PrepareCommit requires a canonical finite-positive native R16
/// readback within the conditioning-aware CPU reference bound plus one
/// binary16 ULP. The accepted native bits, rather than CPU reference bits,
/// advance history only when CommitPrepared is called.
class OgreNextHdrTemporalState final {
public:
  [[nodiscard]] ValidationResult
  Initialize(const OgreNextHdrTemporalConfiguration &configuration);

  [[nodiscard]] ValidationResult PrepareFrame(
      const RenderFrameRequest &request,
      OgreNextRasterFeatureTier raster_feature_tier,
      OgreNextHdrTemporalFramePlan &output,
      bool deferred_sun_visibility_v2 = false) const;

  [[nodiscard]] ValidationResult CommitFrame(
      const OgreNextHdrTemporalFramePlan &plan,
      float average_log_luminance,
      const HdrR16Float &native_stored_inverse_luminance);

  /// Validates and stages a temporal-history update without publishing it.
  /// Exactly one candidate may be pending at a time.
  [[nodiscard]] ValidationResult PrepareCommit(
      const OgreNextHdrTemporalFramePlan &plan,
      float average_log_luminance,
      const HdrR16Float &native_stored_inverse_luminance);

  /// Stages only the frame/time sequencing metadata when exposure history is
  /// intentionally retained on the GPU. This path performs no content
  /// readback and publishes no native-history validation claim.
  [[nodiscard]] ValidationResult
  PrepareGpuOnlyCommit(const OgreNextHdrTemporalFramePlan &plan);

  /// True only while the staged candidate still descends from the currently
  /// committed state. This check is allocation-free and cannot fail.
  [[nodiscard]] bool CanCommitPrepared() const noexcept;

  /// Publishes the staged candidate if CanCommitPrepared is true. Otherwise it
  /// is a no-op; callers may use AbortPrepared to discard a stale candidate.
  void CommitPrepared() noexcept;

  /// Discards the staged candidate without changing committed state.
  void AbortPrepared() noexcept;

  /// A retired frame consumes frontend frame identity
  /// (RendererFrontendDirectDispatcher.cpp:415) but evaluates no exposure.
  /// Nothing else advances committed_frame_id_, and ResetSceneGeneration
  /// deliberately preserves it, so without this every retirement permanently
  /// breaks the contiguity check in PrepareFrame for every later rendered
  /// frame. Checked before the retirement commits, applied after. The
  /// luminance history is untouched: no exposure pass ran, and claiming one
  /// would be fabrication. Committed simulation time IS advanced, so the next
  /// rendered frame's delta is the true inter-frame delta rather than an
  /// inflated one that would trip kHdrMaximumFrameDeltaSeconds after a long
  /// suspension.
  [[nodiscard]] bool CanAccountRetiredFrame(
      std::uint64_t frame_id, double simulation_time_seconds) const noexcept;

  /// Applies what CanAccountRetiredFrame validated. Returns false, changing
  /// nothing, if the state moved since that check; the caller must treat that
  /// as a fault, because frame identity has already advanced elsewhere.
  [[nodiscard]] bool AccountRetiredFrame(
      std::uint64_t frame_id, double simulation_time_seconds) noexcept;

  /// Resets map-scoped simulation time and exposure history while preserving
  /// the renderer-global committed frame ID. The next request must therefore
  /// continue frame identity but may begin again at simulation time zero.
  [[nodiscard]] ValidationResult ResetSceneGeneration();

  void Reset() noexcept;

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] std::uint64_t committed_frame_id() const noexcept {
    return committed_frame_id_;
  }
  [[nodiscard]] HdrR16Float previous_inverse_luminance() const noexcept {
    return previous_inverse_luminance_;
  }
  [[nodiscard]] OgreNextHdrHistoryComparison
  last_history_comparison() const noexcept {
    return last_history_comparison_;
  }

private:
  void ClearPending() noexcept;

  OgreNextHdrTemporalConfiguration configuration_{};
  HdrR16Float previous_inverse_luminance_{};
  OgreNextHdrHistoryComparison last_history_comparison_{};
  std::uint64_t committed_frame_id_ = 0U;
  double committed_simulation_time_seconds_ = 0.0;
  /// Frames whose simulation-time delta ran past the shader envelope and were
  /// saturated to it. Nonzero after a map load or a suspended window; the
  /// adaptation has fully converged at that point, so the image is unchanged.
  /// Purely observational: PrepareFrame is const because it publishes no
  /// durable state, and this counter is not durable state - it records that a
  /// saturation happened and is never read back into any validated value.
  mutable std::uint64_t saturated_frame_deltas_ = 0U;

  // The pending transaction is deliberately POD-only: preparing a renderer
  // frame must not retain handles, allocate, or expose partially committed
  // temporal evidence.
  HdrR16Float pending_previous_inverse_luminance_{};
  OgreNextHdrHistoryComparison pending_history_comparison_{};
  std::uint64_t pending_frame_id_ = 0U;
  double pending_simulation_time_seconds_ = 0.0;
  HdrR16Float pending_base_previous_inverse_luminance_{};
  std::uint64_t pending_base_committed_frame_id_ = 0U;
  double pending_base_committed_simulation_time_seconds_ = 0.0;
  bool commit_prepared_ = false;
  bool initialized_ = false;
};

} // namespace RoR::Render
