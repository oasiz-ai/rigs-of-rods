/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "HdrReference.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace RoR::Render {
namespace {

constexpr double kExposureCalibration = 1024.0;
constexpr double kExposureOffset = 2.0;
constexpr double kAutoExposureLogPivot = 7.5;
constexpr double kAdaptationBase = 0.25;
constexpr double kBloomMultiplier = 16.0;

constexpr double kFilmicA = 0.22;
constexpr double kFilmicB = 0.3;
constexpr double kFilmicC = 0.10;
constexpr double kFilmicD = 0.20;
constexpr double kFilmicE = 0.01;
constexpr double kFilmicF = 0.30;
constexpr double kFilmicWhitePoint = 11.2;
constexpr double kContrast = 1.25;
constexpr double kMidpoint = 0.5;
constexpr double kLift = 0.11;

bool IsNonNegativeFinite(const Float3 &value) noexcept {
  return IsFinite(value) && value.x >= 0.0F && value.y >= 0.0F &&
         value.z >= 0.0F;
}

bool IsUnitColor(const Float3 &value) noexcept {
  return IsNonNegativeFinite(value) && value.x <= 1.0F && value.y <= 1.0F &&
         value.z <= 1.0F;
}

double FilmicToneMap(double value) noexcept {
  const double numerator =
      value * (kFilmicA * value + kFilmicC * kFilmicB) + kFilmicD * kFilmicE;
  const double denominator =
      value * (kFilmicA * value + kFilmicB) + kFilmicD * kFilmicF;
  return numerator / denominator - kFilmicE / kFilmicF;
}

} // namespace

ValidationResult
EvaluateHdrAutoExposureReference(const HdrAutoExposureReferenceInput &input,
                                 HdrAutoExposureReferenceResult &output) {
  if (input.version != kHdrReferenceVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported HDR reference version");
  }
  if (!IsFinite(input.exposure)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "exposure", "exposure must be finite");
  }
  if (!IsFinite(input.minimum_auto_exposure)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "minimum_auto_exposure",
                                     "minimum auto exposure must be finite");
  }
  if (!IsFinite(input.maximum_auto_exposure)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "maximum_auto_exposure",
                                     "maximum auto exposure must be finite");
  }
  if (input.minimum_auto_exposure > input.maximum_auto_exposure) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "minimum_auto_exposure",
        "minimum auto exposure must not exceed maximum auto exposure");
  }
  if (!IsFinite(input.average_log_luminance)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "average_log_luminance",
                                     "average log luminance must be finite");
  }
  if (!IsFinite(input.previous_inverse_luminance)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "previous_inverse_luminance",
        "previous inverse luminance must be finite");
  }
  if (!(input.previous_inverse_luminance > 0.0)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "previous_inverse_luminance",
        "previous inverse luminance must be positive");
  }
  if (!IsFinite(input.delta_seconds)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "delta_seconds",
                                     "frame delta must be finite");
  }
  if (input.delta_seconds < 0.0) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "delta_seconds",
                                     "frame delta must be nonnegative");
  }

  HdrAutoExposureReferenceResult candidate;
  candidate.exposure_numerator =
      kExposureCalibration * std::exp(input.exposure - kExposureOffset);
  candidate.minimum_log_luminance =
      kAutoExposureLogPivot - input.maximum_auto_exposure;
  candidate.maximum_log_luminance =
      kAutoExposureLogPivot - input.minimum_auto_exposure;
  candidate.clamped_log_luminance =
      (std::max)(candidate.minimum_log_luminance,
                 (std::min)(candidate.maximum_log_luminance,
                            input.average_log_luminance));
  const double luminance_denominator =
      std::exp(candidate.clamped_log_luminance);
  candidate.target_inverse_luminance =
      candidate.exposure_numerator / luminance_denominator;
  candidate.previous_frame_weight =
      std::pow(kAdaptationBase, input.delta_seconds);
  candidate.adapted_inverse_luminance =
      candidate.target_inverse_luminance *
          (1.0 - candidate.previous_frame_weight) +
      input.previous_inverse_luminance * candidate.previous_frame_weight;

  if (!IsFinite(candidate.exposure_numerator) ||
      !IsFinite(candidate.minimum_log_luminance) ||
      !IsFinite(candidate.maximum_log_luminance) ||
      !IsFinite(candidate.clamped_log_luminance) ||
      !IsFinite(candidate.target_inverse_luminance) ||
      !IsFinite(candidate.previous_frame_weight) ||
      !IsFinite(candidate.adapted_inverse_luminance) ||
      !(candidate.exposure_numerator > 0.0) ||
      !(candidate.target_inverse_luminance > 0.0) ||
      candidate.previous_frame_weight < 0.0 ||
      candidate.previous_frame_weight > 1.0 ||
      !(candidate.adapted_inverse_luminance > 0.0)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "HDR auto-exposure evaluation overflowed its finite positive domain");
  }

  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
EvaluateHdrFinalToneMapReference(const HdrFinalToneMapReferenceInput &input,
                                 HdrFinalToneMapReferenceResult &output) {
  if (input.version != kHdrReferenceVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported HDR reference version");
  }
  if (!IsNonNegativeFinite(input.scene_linear_hdr)) {
    return ValidationResult::Failure(
        IsFinite(input.scene_linear_hdr) ? ValidationCode::VALUE_OUT_OF_RANGE
                                         : ValidationCode::NON_FINITE_VALUE,
        "scene_linear_hdr",
        "linear HDR scene color must be finite and nonnegative");
  }
  if (!IsUnitColor(input.bloom_srgb)) {
    return ValidationResult::Failure(
        IsFinite(input.bloom_srgb) ? ValidationCode::VALUE_OUT_OF_RANGE
                                   : ValidationCode::NON_FINITE_VALUE,
        "bloom_srgb", "bloom sample must be finite and in [0, 1]");
  }
  if (!IsFinite(input.inverse_luminance)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "inverse_luminance",
                                     "inverse luminance must be finite");
  }
  if (!(input.inverse_luminance > 0.0)) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "inverse_luminance",
                                     "inverse luminance must be positive");
  }
  if (!IsFinite(input.alpha)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE, "alpha",
                                     "alpha must be finite");
  }
  if (input.alpha < 0.0 || input.alpha > 1.0) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "alpha", "alpha must be in [0, 1]");
  }

  HdrFinalToneMapReferenceResult candidate;
  const std::array<double, 3U> scene{{
      static_cast<double>(input.scene_linear_hdr.x),
      static_cast<double>(input.scene_linear_hdr.y),
      static_cast<double>(input.scene_linear_hdr.z),
  }};
  const std::array<double, 3U> bloom{{
      static_cast<double>(input.bloom_srgb.x),
      static_cast<double>(input.bloom_srgb.y),
      static_cast<double>(input.bloom_srgb.z),
  }};
  std::array<double, 3U> exposed{};
  std::array<double, 3U> bloom_linear{};
  std::array<double, 3U> combined{};
  std::array<double, 3U> filmic{};
  std::array<double, 3U> shader_output{};
  const double white_scale = FilmicToneMap(kFilmicWhitePoint);
  if (!IsFinite(white_scale) || !(white_scale > 0.0)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "HDR filmic white point produced an invalid normalization");
  }

  for (std::size_t channel = 0U; channel < scene.size(); ++channel) {
    exposed[channel] = scene[channel] * input.inverse_luminance;
    bloom_linear[channel] = bloom[channel] * bloom[channel];
    combined[channel] =
        exposed[channel] + bloom_linear[channel] * kBloomMultiplier;
    filmic[channel] = FilmicToneMap(combined[channel]) / white_scale;
    shader_output[channel] =
        (filmic[channel] - kMidpoint) * kContrast + kMidpoint + kLift;
  }
  candidate.exposed_scene_linear = {exposed[0U], exposed[1U], exposed[2U]};
  candidate.bloom_linear_approximation = {bloom_linear[0U], bloom_linear[1U],
                                          bloom_linear[2U]};
  candidate.combined_linear = {combined[0U], combined[1U], combined[2U]};
  candidate.filmic_normalized = {filmic[0U], filmic[1U], filmic[2U]};
  candidate.shader_output = {shader_output[0U], shader_output[1U],
                             shader_output[2U]};
  candidate.alpha = input.alpha;

  if (!IsFinite(candidate.exposed_scene_linear) ||
      !IsFinite(candidate.bloom_linear_approximation) ||
      !IsFinite(candidate.combined_linear) ||
      !IsFinite(candidate.filmic_normalized) ||
      !IsFinite(candidate.shader_output)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "HDR tone-map evaluation overflowed its finite output domain");
  }

  output = candidate;
  return ValidationResult::Success();
}

} // namespace RoR::Render
