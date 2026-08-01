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
  if (!initial || !(initial_history.decoded > 0.0F)) {
    return Invalid(
        "configuration.initial_inverse_luminance",
        "initial inverse luminance must round to a finite positive R16_FLOAT value");
  }
  return ValidationResult::Success();
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
  committed_frame_id_ = 0U;
  committed_simulation_time_seconds_ = 0.0;
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
  if (!initialized_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "state",
        "Ogre-Next HDR temporal state is not initialized");
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
  HdrShaderAutoExposureResult expected;
  const ValidationResult evaluated =
      EvaluateHdrShaderAutoExposure(input, expected);
  if (!evaluated) {
    return evaluated;
  }
  if (native_stored_inverse_luminance.bits !=
          expected.stored_inverse_luminance_r16.bits ||
      native_stored_inverse_luminance.decoded !=
          expected.stored_inverse_luminance_r16.decoded) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH,
        "native_stored_inverse_luminance",
        "native R16 exposure history differs from the pinned shader oracle");
  }

  previous_inverse_luminance_ = expected.stored_inverse_luminance_r16;
  committed_frame_id_ = plan.frame_id;
  committed_simulation_time_seconds_ = plan.simulation_time_seconds;
  return ValidationResult::Success();
}

void OgreNextHdrTemporalState::Reset() noexcept {
  configuration_ = OgreNextHdrTemporalConfiguration{};
  previous_inverse_luminance_ = HdrR16Float{};
  committed_frame_id_ = 0U;
  committed_simulation_time_seconds_ = 0.0;
  initialized_ = false;
}

} // namespace RoR::Render
