/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ParallaxProbeReference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace RoR::Render {
namespace {

static_assert(std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<float>::digits == 24,
              "parallax probe inputs require IEEE-754 binary32");
static_assert(std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53,
              "parallax probe reference requires IEEE-754 binary64");

constexpr double kDirectionLengthTolerance = 1.0e-4;
constexpr double kNdfDenominatorEpsilon = 1.0e-6;
constexpr double kManualFadeScale = 200.0;

bool IsStrictlyPositive(const Float3 &value) noexcept {
  return IsFinite(value) && value.x > 0.0F && value.y > 0.0F && value.z > 0.0F;
}

double ClampUnit(double value) noexcept {
  return (std::max)(0.0, (std::min)(1.0, value));
}

} // namespace

ValidationResult
EvaluateParallaxProbeReference(const ParallaxProbeReferenceInput &input,
                               ParallaxProbeReferenceResult &output) {
  if (input.version != kParallaxProbeReferenceVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported parallax-probe reference version");
  }
  if (!IsFinite(input.position_local)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "position_local",
                                     "probe-local position must be finite");
  }
  if (!IsFinite(input.reflection_direction_local)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "reflection_direction_local",
        "probe-local reflection direction must be finite");
  }
  const double direction_x = input.reflection_direction_local.x;
  const double direction_y = input.reflection_direction_local.y;
  const double direction_z = input.reflection_direction_local.z;
  const double direction_length_squared = direction_x * direction_x +
                                          direction_y * direction_y +
                                          direction_z * direction_z;
  if (!std::isfinite(direction_length_squared) ||
      std::fabs(direction_length_squared - 1.0) > kDirectionLengthTolerance) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "reflection_direction_local",
        "probe-local reflection direction must have unit length");
  }
  if (!IsStrictlyPositive(input.probe_half_size)) {
    return ValidationResult::Failure(
        IsFinite(input.probe_half_size) ? ValidationCode::VALUE_OUT_OF_RANGE
                                        : ValidationCode::NON_FINITE_VALUE,
        "probe_half_size", "probe half size must be finite and positive");
  }
  if (!IsFinite(input.cubemap_position_local)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "cubemap_position_local",
                                     "cubemap position must be finite");
  }
  if (!IsFinite(input.area_center_offset_local)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "area_center_offset_local",
                                     "area center offset must be finite");
  }
  if (!IsNonNegative(input.area_inner_range)) {
    return ValidationResult::Failure(
        IsFinite(input.area_inner_range) ? ValidationCode::VALUE_OUT_OF_RANGE
                                         : ValidationCode::NON_FINITE_VALUE,
        "area_inner_range", "inner range must be finite and nonnegative");
  }
  if (!IsNonNegative(input.area_outer_range)) {
    return ValidationResult::Failure(
        IsFinite(input.area_outer_range) ? ValidationCode::VALUE_OUT_OF_RANGE
                                         : ValidationCode::NON_FINITE_VALUE,
        "area_outer_range", "outer range must be finite and nonnegative");
  }
  if (input.area_inner_range.x > input.area_outer_range.x ||
      input.area_inner_range.y > input.area_outer_range.y ||
      input.area_inner_range.z > input.area_outer_range.z) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "area_inner_range",
                                     "inner range must not exceed outer range");
  }
  if (input.priority == 0U) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "priority",
        "automatic probe priority must be nonzero");
  }

  const std::array<double, 3U> position{{
      input.position_local.x,
      input.position_local.y,
      input.position_local.z,
  }};
  const std::array<double, 3U> direction{{
      direction_x,
      direction_y,
      direction_z,
  }};
  const std::array<double, 3U> half_size{{
      input.probe_half_size.x,
      input.probe_half_size.y,
      input.probe_half_size.z,
  }};
  const std::array<double, 3U> cubemap_position{{
      input.cubemap_position_local.x,
      input.cubemap_position_local.y,
      input.cubemap_position_local.z,
  }};
  const std::array<double, 3U> area_offset{{
      input.area_center_offset_local.x,
      input.area_center_offset_local.y,
      input.area_center_offset_local.z,
  }};
  const std::array<double, 3U> inner_range{{
      input.area_inner_range.x,
      input.area_inner_range.y,
      input.area_inner_range.z,
  }};
  const std::array<double, 3U> outer_range{{
      input.area_outer_range.x,
      input.area_outer_range.y,
      input.area_outer_range.z,
  }};

  ParallaxProbeReferenceResult candidate;
  candidate.raw_box_fade = (std::numeric_limits<double>::infinity)();
  candidate.automatic_ndf = -(std::numeric_limits<double>::infinity)();
  for (std::size_t axis = 0U; axis < position.size(); ++axis) {
    const double axis_fade =
        (half_size[axis] - std::fabs(position[axis])) / half_size[axis];
    candidate.raw_box_fade = (std::min)(candidate.raw_box_fade, axis_fade);

    const double area_distance = std::fabs(position[axis] - area_offset[axis]);
    const double axis_ndf =
        (area_distance - inner_range[axis]) /
        (outer_range[axis] - inner_range[axis] + kNdfDenominatorEpsilon);
    candidate.automatic_ndf = (std::max)(candidate.automatic_ndf, axis_ndf);
  }

  candidate.sample_active = candidate.raw_box_fade > 0.0;
  if (!candidate.sample_active) {
    candidate.raw_box_fade = (std::min)(candidate.raw_box_fade, 0.0);
    output = candidate;
    return ValidationResult::Success();
  }

  candidate.manual_probe_weight =
      ClampUnit(candidate.raw_box_fade * kManualFadeScale);
  const double saturated_ndf = ClampUnit(candidate.automatic_ndf);
  const double automatic_fade = 1.0 - saturated_ndf;
  const double automatic_fade_squared = automatic_fade * automatic_fade;
  candidate.automatic_probe_weight =
      automatic_fade_squared * automatic_fade_squared * input.priority;

  candidate.intersection_distance = (std::numeric_limits<double>::infinity)();
  for (std::size_t axis = 0U; axis < position.size(); ++axis) {
    double axis_distance = (std::numeric_limits<double>::infinity)();
    if (direction[axis] > 0.0) {
      axis_distance = (half_size[axis] - position[axis]) / direction[axis];
    } else if (direction[axis] < 0.0) {
      axis_distance = (-half_size[axis] - position[axis]) / direction[axis];
    }
    candidate.intersection_distance =
        (std::min)(candidate.intersection_distance, axis_distance);
  }

  std::array<double, 3U> intersection{};
  std::array<double, 3U> corrected{};
  for (std::size_t axis = 0U; axis < position.size(); ++axis) {
    intersection[axis] =
        position[axis] + direction[axis] * candidate.intersection_distance;
    corrected[axis] = intersection[axis] - cubemap_position[axis];
  }
  corrected[2U] = -corrected[2U];
  candidate.intersection_local = {intersection[0U], intersection[1U],
                                  intersection[2U]};
  candidate.corrected_direction_left_handed = {corrected[0U], corrected[1U],
                                               corrected[2U]};
  const double corrected_length_squared = corrected[0U] * corrected[0U] +
                                          corrected[1U] * corrected[1U] +
                                          corrected[2U] * corrected[2U];

  if (!std::isfinite(candidate.raw_box_fade) ||
      !std::isfinite(candidate.manual_probe_weight) ||
      !std::isfinite(candidate.automatic_ndf) ||
      !std::isfinite(candidate.automatic_probe_weight) ||
      !std::isfinite(candidate.intersection_distance) ||
      !(candidate.intersection_distance > 0.0) ||
      !IsFinite(candidate.intersection_local) ||
      !IsFinite(candidate.corrected_direction_left_handed) ||
      !std::isfinite(corrected_length_squared) ||
      !(corrected_length_squared > 0.0)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "active parallax-probe evaluation produced an undefined sample");
  }

  output = candidate;
  return ValidationResult::Success();
}

} // namespace RoR::Render
