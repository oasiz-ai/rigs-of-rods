/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Deterministic temporal contract for the pinned Ogre-Next HDR path.

#include "OgreNextHdrTemporalContract.h"

#include "../SceneSnapshot.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace RoR::Render {
namespace {

ValidationResult Invalid(const char *field, const char *detail) {
  return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE, field,
                                   detail);
}

ValidationResult ValidateConfiguration(
    const OgreNextHdrTemporalConfiguration &configuration,
    HdrR16Float &initial_history) {
  if (configuration.version != kOgreNextHdrTemporalContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "configuration.version",
        "unsupported Ogre-Next HDR temporal configuration version");
  }
  if (!std::isfinite(configuration.minimum_auto_exposure) ||
      !std::isfinite(configuration.maximum_auto_exposure) ||
      configuration.minimum_auto_exposure < kHdrMinimumExposure ||
      configuration.maximum_auto_exposure > kHdrMaximumExposure ||
      configuration.minimum_auto_exposure >
          configuration.maximum_auto_exposure) {
    return Invalid(
        "configuration.auto_exposure",
        "auto-exposure bounds must be finite, ordered, and inside the pinned HDR envelope");
  }
  if (!std::isfinite(configuration.bloom_minimum_threshold) ||
      !std::isfinite(configuration.bloom_full_colour_threshold) ||
      configuration.bloom_minimum_threshold < 0.0F ||
      configuration.bloom_full_colour_threshold <=
          configuration.bloom_minimum_threshold ||
      configuration.bloom_full_colour_threshold > kHdrR16MaximumFinite) {
    return Invalid(
        "configuration.bloom_threshold",
        "bloom thresholds must be finite, nonnegative, strictly ordered, and R16-representable");
  }
  const ValidationResult initial = QuantizeHdrR16Float(
      configuration.initial_inverse_luminance, initial_history);
  HdrR16Float upstream_initial;
  const ValidationResult upstream =
      QuantizeHdrR16Float(0.01F, upstream_initial);
  if (!initial || !upstream || !(initial_history.decoded > 0.0F) ||
      configuration.initial_inverse_luminance != 0.01F ||
      initial_history.bits != upstream_initial.bits ||
      initial_history.decoded != upstream_initial.decoded) {
    return Invalid(
        "configuration.initial_inverse_luminance",
        "version 2 requires the pinned upstream 0.01 R16_FLOAT initial history");
  }
  return ValidationResult::Success();
}

bool TryPositiveR16StorageUlp(const HdrR16Float &reference,
                              double &output) {
  HdrR16Float canonical;
  if (!DecodeFiniteHdrR16Float(reference.bits, canonical) ||
      canonical.decoded != reference.decoded || !(canonical.decoded > 0.0F) ||
      reference.bits > 0x7bffU) {
    return false;
  }

  double candidate = 0.0;
  if (reference.bits > 0x0001U) {
    HdrR16Float lower;
    if (!DecodeFiniteHdrR16Float(
            static_cast<std::uint16_t>(reference.bits - 1U), lower)) {
      return false;
    }
    candidate = (std::max)(candidate,
                           static_cast<double>(reference.decoded) -
                               static_cast<double>(lower.decoded));
  } else {
    candidate = kHdrR16MinimumPositive;
  }
  if (reference.bits < 0x7bffU) {
    HdrR16Float upper;
    if (!DecodeFiniteHdrR16Float(
            static_cast<std::uint16_t>(reference.bits + 1U), upper)) {
      return false;
    }
    candidate = (std::max)(candidate,
                           static_cast<double>(upper.decoded) -
                               static_cast<double>(reference.decoded));
  }
  if (!std::isfinite(candidate) || !(candidate > 0.0)) {
    return false;
  }
  output = candidate;
  return true;
}

ValidationResult ValidatePlanBasics(
    const OgreNextHdrTemporalFramePlan &plan,
    const OgreNextHdrTemporalConfiguration &configuration,
    std::uint64_t committed_frame_id,
    double committed_simulation_time_seconds,
    const HdrR16Float &previous_inverse_luminance) {
  if (plan.version != kOgreNextHdrTemporalContractVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "plan.version",
        "unsupported Ogre-Next HDR temporal frame-plan version");
  }
  if (committed_frame_id ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      plan.frame_id != committed_frame_id + 1U || plan.snapshot_id == 0U) {
    return Invalid("plan.frame_id",
                   "HDR temporal frames must begin at one and remain contiguous");
  }
  if (!std::isfinite(plan.simulation_time_seconds) ||
      plan.simulation_time_seconds < 0.0 ||
      (committed_frame_id != 0U &&
       plan.simulation_time_seconds < committed_simulation_time_seconds)) {
    return Invalid("plan.simulation_time_seconds",
                   "HDR temporal simulation time must be finite, nonnegative, and monotonic");
  }
  const double expected_delta =
      committed_frame_id == 0U
          ? 0.0
          : plan.simulation_time_seconds - committed_simulation_time_seconds;
  if (expected_delta > kHdrMaximumFrameDeltaSeconds ||
      plan.delta_seconds != static_cast<float>(expected_delta)) {
    return Invalid(
        "plan.delta_seconds",
        "HDR frame delta must exactly match bounded simulation-time lineage");
  }
  if (!std::isfinite(plan.effective_exposure) ||
      !std::isfinite(plan.ogre_exposure) ||
      !(plan.effective_exposure > 0.0F) ||
      std::log(plan.effective_exposure) != plan.ogre_exposure ||
      plan.ogre_exposure < kHdrMinimumExposure ||
      plan.ogre_exposure > kHdrMaximumExposure ||
      plan.minimum_auto_exposure != configuration.minimum_auto_exposure ||
      plan.maximum_auto_exposure != configuration.maximum_auto_exposure ||
      plan.bloom_minimum_threshold !=
          configuration.bloom_minimum_threshold ||
      plan.bloom_full_colour_threshold !=
          configuration.bloom_full_colour_threshold) {
    return Invalid("plan.parameters",
                   "HDR frame plan differs from its validated configuration");
  }
  const float expected_inverse_width =
      1.0F / (configuration.bloom_full_colour_threshold -
              configuration.bloom_minimum_threshold);
  if (!std::isfinite(expected_inverse_width) ||
      plan.bloom_inverse_transition_width != expected_inverse_width) {
    return Invalid("plan.bloom_inverse_transition_width",
                   "HDR bloom transition reciprocal changed after planning");
  }
  if (plan.previous_inverse_luminance_r16.bits !=
          previous_inverse_luminance.bits ||
      plan.previous_inverse_luminance_r16.decoded !=
          previous_inverse_luminance.decoded) {
    return Invalid("plan.previous_inverse_luminance_r16",
                   "HDR frame plan does not consume the committed R16 history");
  }
  return ValidationResult::Success();
}

} // namespace

ValidationResult OgreNextHdrTemporalState::Initialize(
    const OgreNextHdrTemporalConfiguration &configuration) {
  if (initialized_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "Ogre-Next HDR temporal state is already initialized");
  }
  HdrR16Float initial_history;
  const ValidationResult validation =
      ValidateConfiguration(configuration, initial_history);
  if (!validation) {
    return validation;
  }
  configuration_ = configuration;
  previous_inverse_luminance_ = initial_history;
  last_history_comparison_ = OgreNextHdrHistoryComparison{};
  committed_frame_id_ = 0U;
  committed_simulation_time_seconds_ = 0.0;
  ClearPending();
  initialized_ = true;
  return ValidationResult::Success();
}

ValidationResult OgreNextHdrTemporalState::PrepareFrame(
    const RenderFrameRequest &request,
    OgreNextRasterFeatureTier raster_feature_tier,
    OgreNextHdrTemporalFramePlan &output) const {
  if (!initialized_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "Ogre-Next HDR temporal state is not initialized");
  }
  if (commit_prepared_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "an Ogre-Next HDR temporal commit is already prepared");
  }
  if (raster_feature_tier !=
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "raster_feature_tier",
        "the pinned HDR compositor requires the reviewed RT4/V1 PBS tier");
  }
  const ValidationResult request_validation =
      ValidateRenderFrameRequest(request);
  if (!request_validation) {
    return request_validation;
  }
  if (request.color_format != PixelFormat::RGBA8_SRGB ||
      request.requested_outputs != FrameOutputMask::COLOR ||
      request.views.size() != 1U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "request.output",
        "the first pinned HDR compositor produces exactly one tone-mapped RGBA8_SRGB colour view");
  }
  if (committed_frame_id_ ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      request.frame_id != committed_frame_id_ + 1U) {
    return Invalid("request.frame_id",
                   "HDR temporal frames must begin at one and remain contiguous");
  }
  const double simulation_time =
      request.scene_snapshot->simulation_time_seconds();
  if (!std::isfinite(simulation_time) || simulation_time < 0.0 ||
      (committed_frame_id_ != 0U &&
       simulation_time < committed_simulation_time_seconds_)) {
    return Invalid("scene_snapshot.simulation_time_seconds",
                   "HDR temporal simulation time must be finite, nonnegative, and monotonic");
  }
  const double delta =
      committed_frame_id_ == 0U
          ? 0.0
          : simulation_time - committed_simulation_time_seconds_;
  if (delta > kHdrMaximumFrameDeltaSeconds ||
      delta > static_cast<double>((std::numeric_limits<float>::max)())) {
    return Invalid("scene_snapshot.simulation_time_seconds",
                   "HDR temporal delta exceeds the pinned shader envelope");
  }

  const CameraViewRequest &view = request.views.front();
  float effective_exposure = 0.0F;
  if (!ComputePortableEffectiveExposure(
          view.exposure,
          request.scene_snapshot->environment().exposure_compensation_ev,
          effective_exposure)) {
    return Invalid("views.effective_exposure",
                   "view and scene exposure do not produce a finite positive binary32 value");
  }
  const float ogre_exposure = std::log(effective_exposure);
  if (!std::isfinite(ogre_exposure) ||
      ogre_exposure < kHdrMinimumExposure ||
      ogre_exposure > kHdrMaximumExposure) {
    return Invalid(
        "views.effective_exposure",
        "the natural-log Ogre exposure parameter exceeds the pinned HDR envelope");
  }

  OgreNextHdrTemporalFramePlan candidate;
  candidate.frame_id = request.frame_id;
  candidate.snapshot_id = request.scene_snapshot->snapshot_id();
  candidate.simulation_time_seconds = simulation_time;
  candidate.effective_exposure = effective_exposure;
  candidate.ogre_exposure = ogre_exposure;
  candidate.minimum_auto_exposure = configuration_.minimum_auto_exposure;
  candidate.maximum_auto_exposure = configuration_.maximum_auto_exposure;
  candidate.bloom_minimum_threshold =
      configuration_.bloom_minimum_threshold;
  candidate.bloom_full_colour_threshold =
      configuration_.bloom_full_colour_threshold;
  candidate.bloom_inverse_transition_width =
      1.0F / (candidate.bloom_full_colour_threshold -
              candidate.bloom_minimum_threshold);
  candidate.delta_seconds = static_cast<float>(delta);
  candidate.previous_inverse_luminance_r16 =
      previous_inverse_luminance_;

  const ValidationResult plan_validation = ValidatePlanBasics(
      candidate, configuration_, committed_frame_id_,
      committed_simulation_time_seconds_, previous_inverse_luminance_);
  if (!plan_validation) {
    return plan_validation;
  }
  output = candidate;
  return ValidationResult::Success();
}

ValidationResult OgreNextHdrTemporalState::CommitFrame(
    const OgreNextHdrTemporalFramePlan &plan,
    float average_log_luminance,
    const HdrR16Float &native_stored_inverse_luminance) {
  const ValidationResult prepared = PrepareCommit(
      plan, average_log_luminance, native_stored_inverse_luminance);
  if (!prepared) {
    return prepared;
  }
  if (!CanCommitPrepared()) {
    AbortPrepared();
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "prepared Ogre-Next HDR temporal lineage became stale");
  }
  CommitPrepared();
  return ValidationResult::Success();
}

ValidationResult OgreNextHdrTemporalState::PrepareCommit(
    const OgreNextHdrTemporalFramePlan &plan,
    float average_log_luminance,
    const HdrR16Float &native_stored_inverse_luminance) {
  if (!initialized_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "Ogre-Next HDR temporal state is not initialized");
  }
  if (commit_prepared_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "an Ogre-Next HDR temporal commit is already prepared");
  }
  const ValidationResult plan_validation = ValidatePlanBasics(
      plan, configuration_, committed_frame_id_,
      committed_simulation_time_seconds_, previous_inverse_luminance_);
  if (!plan_validation) {
    return plan_validation;
  }

  HdrShaderAutoExposureInput input;
  input.exposure = plan.ogre_exposure;
  input.minimum_auto_exposure = plan.minimum_auto_exposure;
  input.maximum_auto_exposure = plan.maximum_auto_exposure;
  input.average_log_luminance = average_log_luminance;
  input.previous_inverse_luminance =
      plan.previous_inverse_luminance_r16.decoded;
  input.delta_seconds = plan.delta_seconds;
  HdrAutoExposureComparisonResult reference;
  const ValidationResult evaluated =
      CompareHdrAutoExposureReferences(input, reference);
  if (!evaluated) {
    return evaluated;
  }

  HdrR16Float canonical_native;
  if (!DecodeFiniteHdrR16Float(native_stored_inverse_luminance.bits,
                               canonical_native) ||
      canonical_native.decoded != native_stored_inverse_luminance.decoded ||
      !(canonical_native.decoded > 0.0F)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "native_stored_inverse_luminance",
        "native R16 exposure history must be canonical, finite, and positive");
  }

  OgreNextHdrHistoryComparison comparison;
  comparison.mode = OgreNextHdrHistoryValidationMode::
      NATIVE_AUTHORITATIVE_CONDITIONING_PLUS_ONE_R16_ULP;
  comparison.native_inverse_luminance_r16 = canonical_native;
  comparison.reference_inverse_luminance_r16 =
      reference.shader.stored_inverse_luminance_r16;
  comparison.ogre_exposure = input.exposure;
  comparison.minimum_auto_exposure = input.minimum_auto_exposure;
  comparison.maximum_auto_exposure = input.maximum_auto_exposure;
  comparison.average_log_luminance = input.average_log_luminance;
  comparison.previous_inverse_luminance_r16 =
      reference.shader.previous_inverse_luminance_r16;
  comparison.delta_seconds = input.delta_seconds;
  comparison.conditioning_bound = reference.adapted_conditioning_bound;
  comparison.binary32_rounding_bound =
      reference.adapted_binary32_rounding_bound;
  if (!TryPositiveR16StorageUlp(
          comparison.reference_inverse_luminance_r16,
          comparison.storage_ulp)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "reference_inverse_luminance",
        "CPU reference R16 exposure history has no finite storage ULP");
  }
  comparison.absolute_error = std::fabs(
      static_cast<double>(comparison.native_inverse_luminance_r16.decoded) -
      static_cast<double>(comparison.reference_inverse_luminance_r16.decoded));
  comparison.allowed_error =
      reference.adapted_inverse_luminance.allowed_difference +
      comparison.storage_ulp;
  const std::uint16_t low =
      (std::min)(comparison.native_inverse_luminance_r16.bits,
                 comparison.reference_inverse_luminance_r16.bits);
  const std::uint16_t high =
      (std::max)(comparison.native_inverse_luminance_r16.bits,
                 comparison.reference_inverse_luminance_r16.bits);
  comparison.r16_ulp_distance = static_cast<std::uint32_t>(high - low);
  if (!std::isfinite(comparison.absolute_error) ||
      !std::isfinite(comparison.allowed_error) ||
      comparison.allowed_error < 0.0 ||
      comparison.absolute_error > comparison.allowed_error) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH,
        "native_stored_inverse_luminance",
        "native R16 exposure history exceeds the conditioning and storage bound");
  }

  comparison.accepted = true;
  pending_previous_inverse_luminance_ = canonical_native;
  pending_history_comparison_ = comparison;
  pending_frame_id_ = plan.frame_id;
  pending_simulation_time_seconds_ = plan.simulation_time_seconds;
  pending_base_previous_inverse_luminance_ =
      previous_inverse_luminance_;
  pending_base_committed_frame_id_ = committed_frame_id_;
  pending_base_committed_simulation_time_seconds_ =
      committed_simulation_time_seconds_;
  commit_prepared_ = true;
  return ValidationResult::Success();
}

bool OgreNextHdrTemporalState::CanCommitPrepared() const noexcept {
  return commit_prepared_ && initialized_ &&
         pending_base_committed_frame_id_ == committed_frame_id_ &&
         pending_base_committed_simulation_time_seconds_ ==
             committed_simulation_time_seconds_ &&
         pending_base_previous_inverse_luminance_.bits ==
             previous_inverse_luminance_.bits &&
         pending_base_previous_inverse_luminance_.decoded ==
             previous_inverse_luminance_.decoded &&
         pending_frame_id_ == committed_frame_id_ + 1U;
}

void OgreNextHdrTemporalState::CommitPrepared() noexcept {
  if (!CanCommitPrepared()) {
    return;
  }
  previous_inverse_luminance_ = pending_previous_inverse_luminance_;
  last_history_comparison_ = pending_history_comparison_;
  committed_frame_id_ = pending_frame_id_;
  committed_simulation_time_seconds_ =
      pending_simulation_time_seconds_;
  ClearPending();
}

void OgreNextHdrTemporalState::AbortPrepared() noexcept {
  ClearPending();
}

void OgreNextHdrTemporalState::ClearPending() noexcept {
  pending_previous_inverse_luminance_ = HdrR16Float{};
  pending_history_comparison_ = OgreNextHdrHistoryComparison{};
  pending_frame_id_ = 0U;
  pending_simulation_time_seconds_ = 0.0;
  pending_base_previous_inverse_luminance_ = HdrR16Float{};
  pending_base_committed_frame_id_ = 0U;
  pending_base_committed_simulation_time_seconds_ = 0.0;
  commit_prepared_ = false;
}

void OgreNextHdrTemporalState::Reset() noexcept {
  configuration_ = OgreNextHdrTemporalConfiguration{};
  previous_inverse_luminance_ = HdrR16Float{};
  last_history_comparison_ = OgreNextHdrHistoryComparison{};
  committed_frame_id_ = 0U;
  committed_simulation_time_seconds_ = 0.0;
  ClearPending();
  initialized_ = false;
}

} // namespace RoR::Render
