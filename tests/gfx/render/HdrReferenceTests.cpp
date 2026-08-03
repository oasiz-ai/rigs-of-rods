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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "HDR reference test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void RequireNear(double actual, double expected, double tolerance,
                 const char *message) {
  Require(std::isfinite(actual), message);
  Require(std::isfinite(expected), message);
  const double scale =
      (std::max)({1.0, std::fabs(actual), std::fabs(expected)});
  if (std::fabs(actual - expected) > tolerance * scale) {
    std::cerr << "HDR reference mismatch: actual=" << actual
              << " expected=" << expected << " tolerance=" << tolerance << '\n';
    Require(false, message);
  }
}

void RequireNear(const RoR::Render::Double3 &actual,
                 const RoR::Render::Double3 &expected, double tolerance,
                 const char *message) {
  RequireNear(actual.x, expected.x, tolerance, message);
  RequireNear(actual.y, expected.y, tolerance, message);
  RequireNear(actual.z, expected.z, tolerance, message);
}

void RequireNear(const RoR::Render::Float3 &actual,
                 const RoR::Render::Float3 &expected, double tolerance,
                 const char *message) {
  RequireNear(static_cast<double>(actual.x), static_cast<double>(expected.x),
              tolerance, message);
  RequireNear(static_cast<double>(actual.y), static_cast<double>(expected.y),
              tolerance, message);
  RequireNear(static_cast<double>(actual.z), static_cast<double>(expected.z),
              tolerance, message);
}

void RequireFailure(const RoR::Render::ValidationResult &result,
                    RoR::Render::ValidationCode code, const char *field,
                    const char *message) {
  Require(!result.ok(), message);
  Require(result.code == code, message);
  Require(result.field == field, message);
}

bool Equal(const RoR::Render::HdrR16Float &lhs,
           const RoR::Render::HdrR16Float &rhs) {
  return lhs.bits == rhs.bits && lhs.decoded == rhs.decoded;
}

bool Equal(const RoR::Render::HdrAnalyticAutoExposureResult &lhs,
           const RoR::Render::HdrAnalyticAutoExposureResult &rhs) {
  return lhs.exposure_numerator == rhs.exposure_numerator &&
         lhs.minimum_log_luminance == rhs.minimum_log_luminance &&
         lhs.maximum_log_luminance == rhs.maximum_log_luminance &&
         lhs.clamped_log_luminance == rhs.clamped_log_luminance &&
         lhs.target_inverse_luminance == rhs.target_inverse_luminance &&
         lhs.previous_frame_weight == rhs.previous_frame_weight &&
         lhs.adapted_inverse_luminance == rhs.adapted_inverse_luminance;
}

bool Equal(const RoR::Render::HdrShaderAutoExposureResult &lhs,
           const RoR::Render::HdrShaderAutoExposureResult &rhs) {
  return lhs.exposure_numerator == rhs.exposure_numerator &&
         lhs.minimum_log_luminance == rhs.minimum_log_luminance &&
         lhs.maximum_log_luminance == rhs.maximum_log_luminance &&
         lhs.clamped_log_luminance == rhs.clamped_log_luminance &&
         lhs.target_inverse_luminance == rhs.target_inverse_luminance &&
         lhs.previous_frame_weight == rhs.previous_frame_weight &&
         Equal(lhs.previous_inverse_luminance_r16,
               rhs.previous_inverse_luminance_r16) &&
         lhs.adapted_inverse_luminance_before_storage ==
             rhs.adapted_inverse_luminance_before_storage &&
         Equal(lhs.stored_inverse_luminance_r16,
               rhs.stored_inverse_luminance_r16);
}

bool Equal(const RoR::Render::HdrAnalyticFinalToneMapResult &lhs,
           const RoR::Render::HdrAnalyticFinalToneMapResult &rhs) {
  return lhs.exposed_scene_linear == rhs.exposed_scene_linear &&
         lhs.bloom_linear_approximation == rhs.bloom_linear_approximation &&
         lhs.combined_linear == rhs.combined_linear &&
         lhs.filmic_normalized == rhs.filmic_normalized &&
         lhs.analytic_output == rhs.analytic_output && lhs.alpha == rhs.alpha;
}

bool Equal(const RoR::Render::HdrShaderFinalToneMapResult &lhs,
           const RoR::Render::HdrShaderFinalToneMapResult &rhs) {
  return lhs.scene_linear_hdr_r16 == rhs.scene_linear_hdr_r16 &&
         lhs.scene_linear_hdr_r16_bits == rhs.scene_linear_hdr_r16_bits &&
         Equal(lhs.inverse_luminance_r16, rhs.inverse_luminance_r16) &&
         Equal(lhs.alpha_r16, rhs.alpha_r16) &&
         lhs.exposed_scene_linear == rhs.exposed_scene_linear &&
         lhs.bloom_linear_approximation == rhs.bloom_linear_approximation &&
         lhs.combined_linear == rhs.combined_linear &&
         lhs.filmic_normalized == rhs.filmic_normalized &&
         lhs.shader_output == rhs.shader_output && lhs.alpha == rhs.alpha;
}

std::uint32_t FloatBits(float value) {
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float FloatFromBits(std::uint32_t bits) {
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

float FirstDeltaWhoseAdaptationWeightIsBelowOne() {
  std::uint32_t lower = FloatBits(0.0F);
  std::uint32_t upper = FloatBits(1.0e-3F);
  Require(std::pow(0.25F, FloatFromBits(lower)) == 1.0F,
          "zero frame delta did not preserve unit adaptation weight");
  Require(std::pow(0.25F, FloatFromBits(upper)) < 1.0F,
          "adaptation transition search upper bound is too small");
  while (lower + 1U < upper) {
    const std::uint32_t middle = lower + (upper - lower) / 2U;
    if (std::pow(0.25F, FloatFromBits(middle)) == 1.0F) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  Require(std::pow(0.25F, FloatFromBits(lower)) == 1.0F &&
              std::pow(0.25F, FloatFromBits(upper)) < 1.0F,
          "adaptation transition neighbors are not adjacent");
  return FloatFromBits(upper);
}

float NextUnitFloat(std::uint64_t &state) {
  state ^= state >> 12U;
  state ^= state << 25U;
  state ^= state >> 27U;
  const std::uint64_t sample = state * UINT64_C(2685821657736338717);
  return static_cast<float>(sample >> 40U) * 0x1p-24F;
}

RoR::Render::HdrAnalyticAutoExposureResult EvaluateAnalyticExposure(
    const RoR::Render::HdrAnalyticAutoExposureInput &input) {
  RoR::Render::HdrAnalyticAutoExposureResult result;
  Require(RoR::Render::EvaluateHdrAnalyticAutoExposure(input, result).ok(),
          "valid analytic exposure fixture was rejected");
  return result;
}

RoR::Render::HdrShaderAutoExposureResult
EvaluateShaderExposure(const RoR::Render::HdrShaderAutoExposureInput &input) {
  RoR::Render::HdrShaderAutoExposureResult result;
  Require(RoR::Render::EvaluateHdrShaderAutoExposure(input, result).ok(),
          "valid shader exposure fixture was rejected");
  return result;
}

RoR::Render::HdrAnalyticFinalToneMapResult EvaluateAnalyticToneMap(
    const RoR::Render::HdrAnalyticFinalToneMapInput &input) {
  RoR::Render::HdrAnalyticFinalToneMapResult result;
  Require(RoR::Render::EvaluateHdrAnalyticFinalToneMap(input, result).ok(),
          "valid analytic tone-map fixture was rejected");
  return result;
}

RoR::Render::HdrShaderFinalToneMapResult
EvaluateShaderToneMap(const RoR::Render::HdrShaderFinalToneMapInput &input) {
  RoR::Render::HdrShaderFinalToneMapResult result;
  Require(RoR::Render::EvaluateHdrShaderFinalToneMap(input, result).ok(),
          "valid shader tone-map fixture was rejected");
  return result;
}

void TestIdentityAndDeclaredDomains() {
  using namespace RoR::Render;
  Require(kHdrAnalyticReferenceVersion == 1U,
          "analytic HDR version changed unexpectedly");
  Require(kHdrShaderReferenceVersion == 1U,
          "shader HDR version changed unexpectedly");
  Require(std::string(kHdrReferenceOgreNextCommit) ==
              "37149a802de747f6806996fa3067b0748ecc1084",
          "HDR reference lost its Ogre-Next source identity");
  Require(kHdrMinimumExposure == -16.0 && kHdrMaximumExposure == 16.0,
          "approved exposure envelope drifted");
  Require(kHdrR16MaximumFinite == 65504.0 && kHdrR16MinimumPositive == 0x1p-24,
          "R16 source envelope drifted");
  Require(kHdrAnalyticShaderAbsoluteTolerance > 0.0 &&
              kHdrAnalyticShaderRelativeTolerance > 0.0,
          "analytic/shader tolerance must remain explicit");
  Require(kHdrBinary32UnitRoundoff == 0x1p-24 &&
              kHdrBinary32Gamma5 > 5.0 * kHdrBinary32UnitRoundoff,
          "binary32 adapted-exposure roundoff constants drifted");
}

void TestBinary16RoundToNearestEven() {
  using namespace RoR::Render;
  HdrR16Float output{0xffffU, 123.0F};
  Require(QuantizeHdrR16Float(0.0F, output).ok(), "zero R16 rejected");
  Require(output.bits == 0x0000U && output.decoded == 0.0F,
          "zero R16 encoding drifted");
  Require(QuantizeHdrR16Float(-0.0F, output).ok(),
          "negative zero R16 rejected");
  Require(output.bits == 0x0000U && !std::signbit(output.decoded),
          "negative zero was not canonicalized");
  Require(QuantizeHdrR16Float(1.0F, output).ok(), "one R16 rejected");
  Require(output.bits == 0x3c00U && output.decoded == 1.0F,
          "one R16 encoding drifted");
  Require(QuantizeHdrR16Float(0x1p-24F, output).ok(),
          "minimum R16 subnormal rejected");
  Require(output.bits == 0x0001U && output.decoded == 0x1p-24F,
          "minimum R16 subnormal encoding drifted");

  Require(QuantizeHdrR16Float(0x1p-25F, output).ok(),
          "half-subnormal tie rejected");
  Require(output.bits == 0x0000U,
          "half-subnormal tie did not round to even zero");
  const float above_half_subnormal =
      std::nextafter(0x1p-25F, (std::numeric_limits<float>::infinity)());
  Require(QuantizeHdrR16Float(above_half_subnormal, output).ok(),
          "above-half subnormal rejected");
  Require(output.bits == 0x0001U, "above-half subnormal did not round upward");
  Require(
      QuantizeHdrR16Float((std::numeric_limits<float>::denorm_min)(), output)
          .ok(),
      "binary32 subnormal R16 source was rejected");
  Require(output.bits == 0x0000U,
          "binary32 subnormal did not underflow to R16 zero");

  constexpr float halfway_at_one = 1.0F + 0x1p-11F;
  Require(QuantizeHdrR16Float(halfway_at_one, output).ok(),
          "normal halfway tie rejected");
  Require(output.bits == 0x3c00U, "normal halfway tie did not round to even");
  const float above_halfway_at_one =
      std::nextafter(halfway_at_one, (std::numeric_limits<float>::infinity)());
  Require(QuantizeHdrR16Float(above_halfway_at_one, output).ok(),
          "normal value above tie rejected");
  Require(output.bits == 0x3c01U,
          "normal value above tie did not round upward");

  Require(QuantizeHdrR16Float(65504.0F, output).ok(),
          "maximum finite R16 rejected");
  Require(output.bits == 0x7bffU && output.decoded == 65504.0F,
          "maximum finite R16 encoding drifted");

  const HdrR16Float sentinel{0x1234U, 7.0F};
  output = sentinel;
  ValidationResult failure = QuantizeHdrR16Float(
      std::nextafter(65504.0F, (std::numeric_limits<float>::infinity)()),
      output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE, "value",
                 "R16 source value above the admitted maximum was accepted");
  Require(Equal(output, sentinel), "R16 source-range failure changed output");
  failure =
      QuantizeHdrR16Float((std::numeric_limits<float>::quiet_NaN)(), output);
  RequireFailure(failure, ValidationCode::NON_FINITE_VALUE, "value",
                 "R16 NaN was accepted");
  Require(Equal(output, sentinel), "R16 NaN changed output");
  failure =
      QuantizeHdrR16Float((std::numeric_limits<float>::infinity)(), output);
  RequireFailure(failure, ValidationCode::NON_FINITE_VALUE, "value",
                 "R16 infinity was accepted");
  Require(Equal(output, sentinel), "R16 infinity changed output");
  failure = QuantizeHdrR16Float(-1.0F, output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE, "value",
                 "negative R16 was accepted");
  Require(Equal(output, sentinel), "negative R16 changed output");
}

void TestSignedFiniteBinary16Decode() {
  using namespace RoR::Render;
  HdrR16Float output{0x1234U, 7.0F};
  Require(DecodeFiniteHdrR16Float(0xc08fU, output).ok() &&
              output.bits == 0xc08fU && output.decoded == -2.279296875F,
          "negative log-luminance R16 did not decode exactly");
  Require(DecodeFiniteHdrR16Float(0x8001U, output).ok() &&
              output.bits == 0x8001U && output.decoded == -0x1p-24F,
          "negative R16 subnormal did not decode exactly");
  Require(DecodeFiniteHdrR16Float(0x8000U, output).ok() &&
              output.bits == 0x8000U && output.decoded == 0.0F &&
              std::signbit(output.decoded),
          "negative R16 zero lost its exact sign bit");
  Require(DecodeFiniteHdrR16Float(0xfbffU, output).ok() &&
              output.bits == 0xfbffU && output.decoded == -65504.0F,
          "minimum finite R16 did not decode exactly");

  const HdrR16Float sentinel{0x4321U, 9.0F};
  constexpr std::array<std::uint16_t, 4U> kNonFinitePatterns{{
      0x7c00U, 0xfc00U, 0x7e00U, 0xfe00U}};
  for (const std::uint16_t non_finite : kNonFinitePatterns) {
    output = sentinel;
    Require(!DecodeFiniteHdrR16Float(non_finite, output).ok() &&
                Equal(output, sentinel),
            "non-finite R16 decode was not transactional");
  }
}

void TestAnalyticExposureGolden() {
  using namespace RoR::Render;
  HdrAnalyticAutoExposureInput input;
  input.average_log_luminance = 6.25;
  input.previous_inverse_luminance = 0.75;
  const HdrAnalyticAutoExposureResult result = EvaluateAnalyticExposure(input);
  RequireNear(result.exposure_numerator, 138.5833300342914, 2.0e-14,
              "analytic exposure numerator drifted");
  RequireNear(result.minimum_log_luminance, 5.0, 0.0,
              "analytic minimum log luminance drifted");
  RequireNear(result.maximum_log_luminance, 8.5, 0.0,
              "analytic maximum log luminance drifted");
  RequireNear(result.clamped_log_luminance, 6.25, 0.0,
              "analytic log luminance clamp drifted");
  RequireNear(result.target_inverse_luminance, 0.2675287626769076, 2.0e-14,
              "analytic target inverse luminance drifted");
  RequireNear(result.previous_frame_weight, 0.9715319411536059, 2.0e-14,
              "analytic 48 FPS adaptation weight drifted");
  RequireNear(result.adapted_inverse_luminance, 0.7362649804241936, 2.0e-14,
              "analytic adapted inverse luminance drifted");
}

void TestShaderExposureAndR16Feedback() {
  using namespace RoR::Render;
  HdrShaderAutoExposureInput input;
  input.average_log_luminance = 6.25F;
  input.previous_inverse_luminance = 0.75F;
  const HdrShaderAutoExposureResult first = EvaluateShaderExposure(input);
  Require(first.previous_inverse_luminance_r16.bits == 0x3a00U,
          "shader previous R16 encoding drifted");
  Require(first.stored_inverse_luminance_r16.bits == 0x39e4U,
          "shader first-frame R16 encoding drifted");
  Require(first.stored_inverse_luminance_r16.decoded == 0.736328125F,
          "shader first-frame R16 value drifted");
  RequireNear(first.target_inverse_luminance, 0.26752877, 2.0e-6,
              "shader target inverse luminance drifted");

  float previous = input.previous_inverse_luminance;
  HdrShaderAutoExposureResult frame;
  for (std::uint32_t index = 0U; index < 480U; ++index) {
    input.previous_inverse_luminance = previous;
    frame = EvaluateShaderExposure(input);
    previous = frame.stored_inverse_luminance_r16.decoded;
  }
  Require(frame.stored_inverse_luminance_r16.bits == 0x3459U,
          "48 FPS R16 feedback stall point drifted");
  Require(frame.stored_inverse_luminance_r16.decoded == 0.271728515625F,
          "48 FPS R16 feedback value drifted");

  HdrAnalyticAutoExposureInput analytic;
  analytic.average_log_luminance = 6.25;
  analytic.previous_inverse_luminance = 0.75;
  HdrAnalyticAutoExposureResult ideal;
  for (std::uint32_t index = 0U; index < 480U; ++index) {
    ideal = EvaluateAnalyticExposure(analytic);
    analytic.previous_inverse_luminance = ideal.adapted_inverse_luminance;
  }
  RequireNear(ideal.adapted_inverse_luminance, 0.267529222797336, 2.0e-14,
              "analytic 480-frame convergence drifted");
  Require(frame.stored_inverse_luminance_r16.decoded -
                  ideal.adapted_inverse_luminance >
              0.004,
          "test no longer exposes the material R16 feedback difference");
}

void TestConditionedExposureComparison() {
  using namespace RoR::Render;
  HdrShaderAutoExposureInput input;
  input.exposure = 10.0F;
  input.minimum_auto_exposure = -1.0F;
  input.maximum_auto_exposure = 2.5F;
  input.average_log_luminance = -100.0F;
  input.previous_inverse_luminance = 1.0F;

  const float transition_above = FirstDeltaWhoseAdaptationWeightIsBelowOne();
  const float transition_below =
      FloatFromBits(FloatBits(transition_above) - 1U);
  const std::array<float, 8U> deltas{{
      0.0F,
      transition_below,
      transition_above,
      1.0e-8F,
      1.0e-3F,
      1.0F / 48.0F,
      1.0F / 60.0F,
      60.0F,
  }};

  for (const float delta : deltas) {
    input.delta_seconds = delta;
    HdrAutoExposureComparisonResult comparison;
    const ValidationResult validation =
        CompareHdrAutoExposureReferences(input, comparison);
    Require(validation.ok(),
            "valid conditioned exposure comparison was rejected");
    Require(comparison.analytic_input.previous_inverse_luminance ==
                static_cast<double>(
                    comparison.shader.previous_inverse_luminance_r16.decoded),
            "exposure comparison did not reconstruct prior R16 storage");
    Require(comparison.target_inverse_luminance.absolute_difference <=
                    comparison.target_inverse_luminance.allowed_difference &&
                comparison.previous_frame_weight.absolute_difference <=
                    comparison.previous_frame_weight.allowed_difference &&
                comparison.adapted_inverse_luminance.absolute_difference <=
                    comparison.adapted_inverse_luminance.allowed_difference,
            "exposure comparison exceeded a declared bound");
  }

  input.delta_seconds = 1.0e-8F;
  HdrAutoExposureComparisonResult counterexample;
  Require(CompareHdrAutoExposureReferences(input, counterexample).ok(),
          "near-zero-delta conditioning counterexample was rejected");
  const double obsolete_scalar_bound =
      kHdrAnalyticShaderAbsoluteTolerance +
      kHdrAnalyticShaderRelativeTolerance *
          (std::max)(std::fabs(
                         counterexample.analytic.adapted_inverse_luminance),
                     std::fabs(static_cast<double>(
                         counterexample.shader
                             .adapted_inverse_luminance_before_storage)));
  Require(counterexample.shader.previous_frame_weight == 1.0F &&
              counterexample.analytic.previous_frame_weight < 1.0 &&
              counterexample.adapted_inverse_luminance.absolute_difference >
                  obsolete_scalar_bound,
          "counterexample no longer demonstrates ill-conditioned adaptation");
  Require(counterexample.adapted_conditioning_bound > 0.0 &&
              counterexample.adapted_binary32_rounding_bound > 0.0 &&
              counterexample.adapted_inverse_luminance.allowed_difference ==
                  counterexample.adapted_conditioning_bound +
                      counterexample.adapted_binary32_rounding_bound,
          "adapted exposure did not publish the exact conditioned bound");

  input.delta_seconds = 0.0F;
  input.exposure = kHdrMinimumExposure;
  HdrAutoExposureComparisonResult boundary;
  Require(CompareHdrAutoExposureReferences(input, boundary).ok(),
          "minimum exposure boundary comparison failed");
  input.exposure = kHdrMaximumExposure;
  Require(CompareHdrAutoExposureReferences(input, boundary).ok(),
          "maximum exposure at zero delta comparison failed");
  Require(boundary.shader.stored_inverse_luminance_r16.bits == 0x3c00U,
          "zero delta did not preserve exact prior R16 bits");

  input.exposure = 0.0F;
  input.previous_inverse_luminance = 0x1p-24F;
  Require(CompareHdrAutoExposureReferences(input, boundary).ok(),
          "minimum positive R16 prior comparison failed");
  Require(boundary.shader.previous_inverse_luminance_r16.bits == 0x0001U &&
              boundary.shader.stored_inverse_luminance_r16.bits == 0x0001U,
          "minimum positive R16 exact-bit boundary drifted");
  input.previous_inverse_luminance = 65504.0F;
  Require(CompareHdrAutoExposureReferences(input, boundary).ok(),
          "maximum finite R16 prior comparison failed");
  Require(boundary.shader.previous_inverse_luminance_r16.bits == 0x7bffU &&
              boundary.shader.stored_inverse_luminance_r16.bits == 0x7bffU,
          "maximum finite R16 exact-bit boundary drifted");

  HdrAutoExposureComparisonResult sentinel;
  sentinel.adapted_conditioning_bound = 123.0;
  HdrAutoExposureComparisonResult unchanged = sentinel;
  input.previous_inverse_luminance =
      std::nextafter(65504.0F, (std::numeric_limits<float>::infinity)());
  ValidationResult failure = CompareHdrAutoExposureReferences(input, unchanged);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "previous_inverse_luminance",
                 "out-of-envelope R16 prior was accepted by comparison");
  Require(unchanged.adapted_conditioning_bound ==
              sentinel.adapted_conditioning_bound,
          "failed exposure comparison modified its output");

  input = {};
  input.exposure = 16.0F;
  input.delta_seconds = 1.0F / 48.0F;
  failure = CompareHdrAutoExposureReferences(input, unchanged);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
                 "R16-overflowing exposure comparison was accepted");
  Require(unchanged.adapted_conditioning_bound ==
              sentinel.adapted_conditioning_bound,
          "overflowing exposure comparison modified its output");
}

void TestAnalyticToneMapGolden() {
  using namespace RoR::Render;
  HdrAnalyticFinalToneMapInput input;
  input.scene_linear_hdr = {1.0, 2.0, 4.0};
  input.bloom_gamma2_encoded = {0.1F, 0.25F, 0.5F};
  input.inverse_luminance = 0.75;
  input.alpha = 0.4;
  const HdrAnalyticFinalToneMapResult result = EvaluateAnalyticToneMap(input);
  RequireNear(result.exposed_scene_linear, {0.75, 1.5, 3.0}, 0.0,
              "analytic scene exposure drifted");
  RequireNear(result.bloom_linear_approximation,
              {0.010000000298023226, 0.0625, 0.25}, 2.0e-14,
              "analytic gamma-2 bloom conversion drifted");
  RequireNear(result.combined_linear, {0.9100000047683716, 2.5, 7.0}, 2.0e-14,
              "analytic scene/bloom combination drifted");
  RequireNear(result.filmic_normalized,
              {0.43487346167472785, 0.72777206709626485, 0.94099459865407786},
              2.0e-14, "analytic Hable response drifted");
  RequireNear(result.analytic_output,
              {0.52859182709340979, 0.89471508387033105, 1.1612432483175974},
              2.0e-14, "analytic contrast/lift drifted");
}

void TestShaderToneMapGoldenAndTolerance() {
  using namespace RoR::Render;
  HdrShaderFinalToneMapInput input;
  input.scene_linear_hdr = {1.0F, 2.0F, 4.0F};
  input.bloom_gamma2_encoded = {0.1F, 0.25F, 0.5F};
  input.inverse_luminance = 0.75F;
  input.alpha = 0.4F;
  const HdrShaderFinalToneMapResult shader = EvaluateShaderToneMap(input);
  Require(shader.scene_linear_hdr_r16_bits ==
              std::array<std::uint16_t, 3U>{{0x3c00U, 0x4000U, 0x4400U}},
          "shader scene R16 encodings drifted");
  Require(shader.inverse_luminance_r16.bits == 0x3a00U,
          "shader inverse luminance R16 encoding drifted");
  Require(shader.alpha_r16.bits == 0x3666U && shader.alpha == 0.39990234375F,
          "shader alpha R16 preservation drifted");
  RequireNear(shader.shader_output,
              {0.5285918116569519F, 0.8947150707244873F, 1.161243200302124F},
              0.0, "binary32 shader golden drifted");

  HdrFinalToneMapComparisonResult comparison;
  Require(CompareHdrFinalToneMapReferences(input, comparison).ok(),
          "valid tone-map cross-precision comparison failed");
  Require(
      comparison.analytic_input.scene_linear_hdr == Double3{1.0, 2.0, 4.0} &&
          comparison.analytic_input.inverse_luminance == 0.75 &&
          comparison.analytic_input.alpha == static_cast<double>(shader.alpha),
      "tone-map comparison did not reconstruct the R16 source boundary");
  for (const HdrCrossPrecisionComparison &channel :
       comparison.output_channels) {
    Require(channel.absolute_difference <= channel.allowed_difference,
            "tone-map channel exceeds its explicit scalar tolerance");
  }

  HdrShaderFinalToneMapInput quantized;
  quantized.scene_linear_hdr = {1.0003F, 0.0F, 0.0F};
  quantized.inverse_luminance = 0.73626494F;
  const HdrShaderFinalToneMapResult rounded = EvaluateShaderToneMap(quantized);
  Require(rounded.scene_linear_hdr_r16.x == 1.0F,
          "scene source was not rounded to RGBA16_FLOAT");
  Require(rounded.inverse_luminance_r16.decoded == 0.736328125F,
          "luminance source was not rounded to R16_FLOAT");

  Require(CompareHdrFinalToneMapReferences(quantized, comparison).ok(),
          "nonrepresentable tone-map source comparison failed");
  Require(comparison.analytic_input.scene_linear_hdr.x == 1.0 &&
              comparison.analytic_input.inverse_luminance == 0.736328125,
          "tone-map comparison mislabeled R16 quantization as equation error");
  Require(comparison.shader.scene_linear_hdr_r16_bits[0U] == 0x3c00U &&
              comparison.shader.inverse_luminance_r16.bits == 0x39e4U,
          "tone-map comparison lost the independent exact-bit storage gate");
  Require(comparison.output_channels[0U].absolute_difference <=
              comparison.output_channels[0U].allowed_difference,
          "storage-normalized tone-map comparison exceeds scalar tolerance");
}

void TestFiniteRepresentableExtremesAndMonotonicity() {
  using namespace RoR::Render;
  HdrShaderFinalToneMapInput maximum;
  maximum.scene_linear_hdr = {65504.0F, 65504.0F, 65504.0F};
  maximum.bloom_gamma2_encoded = {1.0F, 1.0F, 1.0F};
  maximum.inverse_luminance = 65504.0F;
  const HdrShaderFinalToneMapResult maximum_result =
      EvaluateShaderToneMap(maximum);
  Require(IsFinite(maximum_result.shader_output),
          "representable HDR maximum produced a non-finite shader result");

  double previous_analytic = -1.0;
  float previous_shader = -1.0F;
  for (std::uint32_t sample = 0U; sample <= 10000U; ++sample) {
    HdrAnalyticFinalToneMapInput analytic;
    analytic.scene_linear_hdr.x = static_cast<double>(sample) * 0.01;
    analytic.inverse_luminance = 1.75;
    const HdrAnalyticFinalToneMapResult analytic_result =
        EvaluateAnalyticToneMap(analytic);
    Require(analytic_result.analytic_output.x >= previous_analytic,
            "analytic filmic curve was not monotonic");
    previous_analytic = analytic_result.analytic_output.x;

    HdrShaderFinalToneMapInput shader;
    shader.scene_linear_hdr.x = static_cast<float>(sample) * 0.01F;
    shader.inverse_luminance = 1.75F;
    const HdrShaderFinalToneMapResult shader_result =
        EvaluateShaderToneMap(shader);
    Require(shader_result.shader_output.x >= previous_shader,
            "shader filmic curve was not monotonic after R16 quantization");
    previous_shader = shader_result.shader_output.x;
  }
}

void TestRandomizedCrossPrecisionCoverage() {
  using namespace RoR::Render;
  std::uint64_t random_state = UINT64_C(0x4844525f52454631);
  const float transition_above = FirstDeltaWhoseAdaptationWeightIsBelowOne();
  const float transition_below =
      FloatFromBits(FloatBits(transition_above) - 1U);
  std::uint32_t successful_exposure_comparisons = 0U;

  for (std::uint32_t sample = 0U; sample < 100000U; ++sample) {
    HdrShaderAutoExposureInput exposure;
    exposure.exposure = -16.0F + 32.0F * NextUnitFloat(random_state);
    const float first_auto_exposure =
        -16.0F + 32.0F * NextUnitFloat(random_state);
    const float second_auto_exposure =
        -16.0F + 32.0F * NextUnitFloat(random_state);
    exposure.minimum_auto_exposure =
        (std::min)(first_auto_exposure, second_auto_exposure);
    exposure.maximum_auto_exposure =
        (std::max)(first_auto_exposure, second_auto_exposure);
    exposure.average_log_luminance =
        -65504.0F + 131008.0F * NextUnitFloat(random_state);
    exposure.previous_inverse_luminance =
        (std::min)(65504.0F,
                   std::exp2(-24.0F + 40.0F * NextUnitFloat(random_state)));
    switch (sample % 8U) {
    case 0U:
      exposure.delta_seconds = 0.0F;
      break;
    case 1U:
      exposure.delta_seconds = transition_below;
      break;
    case 2U:
      exposure.delta_seconds = transition_above;
      break;
    case 3U:
      exposure.delta_seconds = 1.0e-8F;
      break;
    case 4U:
      exposure.delta_seconds = 1.0e-3F;
      break;
    case 5U:
      exposure.delta_seconds = 1.0F / 48.0F;
      break;
    case 6U:
      exposure.delta_seconds = 1.0F / 60.0F;
      break;
    default:
      exposure.delta_seconds = 60.0F * NextUnitFloat(random_state);
      break;
    }
    HdrShaderAutoExposureResult shader_exposure;
    const ValidationResult shader_validation =
        EvaluateHdrShaderAutoExposure(exposure, shader_exposure);
    HdrAutoExposureComparisonResult exposure_comparison;
    exposure_comparison.adapted_conditioning_bound = 91.0;
    const ValidationResult comparison_validation =
        CompareHdrAutoExposureReferences(exposure, exposure_comparison);
    if (shader_validation) {
      Require(comparison_validation.ok(),
              "randomized exposure comparison exceeded its conditioned bound");
      ++successful_exposure_comparisons;
    } else {
      Require(!comparison_validation.ok() &&
                  comparison_validation.code == shader_validation.code &&
                  comparison_validation.field == shader_validation.field,
              "comparison did not preserve randomized shader failure");
      Require(exposure_comparison.adapted_conditioning_bound == 91.0,
              "randomized failed comparison modified its output");
    }

    HdrShaderFinalToneMapInput tone;
    tone.scene_linear_hdr = {65504.0F * NextUnitFloat(random_state),
                             65504.0F * NextUnitFloat(random_state),
                             65504.0F * NextUnitFloat(random_state)};
    tone.bloom_gamma2_encoded = {NextUnitFloat(random_state),
                                 NextUnitFloat(random_state),
                                 NextUnitFloat(random_state)};
    tone.inverse_luminance = 0.01F + 3.99F * NextUnitFloat(random_state);
    tone.alpha = NextUnitFloat(random_state);
    HdrFinalToneMapComparisonResult tone_comparison;
    Require(CompareHdrFinalToneMapReferences(tone, tone_comparison).ok(),
            "randomized tone-map comparison exceeded its scalar bound");
  }
  Require(successful_exposure_comparisons > 50000U,
          "randomized coverage produced too few successful exposure gates");

  HdrFinalToneMapComparisonResult sentinel;
  sentinel.output_channels[0U].allowed_difference = 77.0;
  HdrFinalToneMapComparisonResult unchanged = sentinel;
  HdrShaderFinalToneMapInput invalid;
  invalid.version += 1U;
  const ValidationResult failure =
      CompareHdrFinalToneMapReferences(invalid, unchanged);
  RequireFailure(failure, ValidationCode::UNSUPPORTED_VERSION, "version",
                 "invalid tone-map comparison version was accepted");
  Require(unchanged.output_channels[0U].allowed_difference == 77.0,
          "failed tone-map comparison modified its output");
}

void TestExposureFailuresAreOrderedAndTransactional() {
  using namespace RoR::Render;
  HdrAnalyticAutoExposureResult analytic_sentinel;
  analytic_sentinel.exposure_numerator = 1.0;
  analytic_sentinel.minimum_log_luminance = 2.0;
  analytic_sentinel.maximum_log_luminance = 3.0;
  analytic_sentinel.clamped_log_luminance = 4.0;
  analytic_sentinel.target_inverse_luminance = 5.0;
  analytic_sentinel.previous_frame_weight = 6.0;
  analytic_sentinel.adapted_inverse_luminance = 7.0;
  HdrAnalyticAutoExposureResult analytic_output = analytic_sentinel;
  HdrAnalyticAutoExposureInput analytic;
  analytic.version += 1U;
  analytic.exposure = (std::numeric_limits<double>::quiet_NaN)();
  ValidationResult failure =
      EvaluateHdrAnalyticAutoExposure(analytic, analytic_output);
  RequireFailure(failure, ValidationCode::UNSUPPORTED_VERSION, "version",
                 "analytic version was not validated first");
  Require(Equal(analytic_output, analytic_sentinel),
          "analytic version failure changed output");

  analytic = {};
  analytic.exposure = (std::numeric_limits<double>::quiet_NaN)();
  analytic.minimum_auto_exposure = 17.0;
  failure = EvaluateHdrAnalyticAutoExposure(analytic, analytic_output);
  RequireFailure(failure, ValidationCode::NON_FINITE_VALUE, "exposure",
                 "analytic exposure validation order drifted");
  Require(Equal(analytic_output, analytic_sentinel),
          "analytic exposure failure changed output");

  analytic = {};
  analytic.exposure = 84.0;
  failure = EvaluateHdrAnalyticAutoExposure(analytic, analytic_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE, "exposure",
                 "float-overflowing exposure was accepted by analytic mode");
  Require(Equal(analytic_output, analytic_sentinel),
          "analytic range failure changed output");

  analytic = {};
  analytic.minimum_auto_exposure = 3.0;
  analytic.maximum_auto_exposure = 2.0;
  failure = EvaluateHdrAnalyticAutoExposure(analytic, analytic_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "minimum_auto_exposure", "inverted bounds were accepted");
  Require(Equal(analytic_output, analytic_sentinel),
          "analytic bound failure changed output");

  const auto require_analytic_failure =
      [&](const HdrAnalyticAutoExposureInput &invalid, ValidationCode code,
          const char *field, const char *message) {
        analytic_output = analytic_sentinel;
        const ValidationResult result =
            EvaluateHdrAnalyticAutoExposure(invalid, analytic_output);
        RequireFailure(result, code, field, message);
        Require(Equal(analytic_output, analytic_sentinel),
                "analytic exposure failure was not fully transactional");
      };
  analytic = {};
  analytic.minimum_auto_exposure = (std::numeric_limits<double>::infinity)();
  require_analytic_failure(analytic, ValidationCode::NON_FINITE_VALUE,
                           "minimum_auto_exposure",
                           "non-finite minimum exposure was accepted");
  analytic = {};
  analytic.maximum_auto_exposure = 17.0;
  require_analytic_failure(analytic, ValidationCode::VALUE_OUT_OF_RANGE,
                           "maximum_auto_exposure",
                           "out-of-range maximum exposure was accepted");
  analytic = {};
  analytic.average_log_luminance = (std::numeric_limits<double>::quiet_NaN)();
  require_analytic_failure(analytic, ValidationCode::NON_FINITE_VALUE,
                           "average_log_luminance",
                           "non-finite log luminance was accepted");
  analytic = {};
  analytic.previous_inverse_luminance = 0x1p-25;
  require_analytic_failure(analytic, ValidationCode::VALUE_OUT_OF_RANGE,
                           "previous_inverse_luminance",
                           "sub-R16 positive luminance was accepted");
  analytic = {};
  analytic.delta_seconds = kHdrMaximumFrameDeltaSeconds + 0.01;
  require_analytic_failure(analytic, ValidationCode::VALUE_OUT_OF_RANGE,
                           "delta_seconds",
                           "oversized frame delta was accepted");

  HdrShaderAutoExposureResult shader_sentinel;
  shader_sentinel.exposure_numerator = 1.0F;
  shader_sentinel.minimum_log_luminance = 2.0F;
  shader_sentinel.maximum_log_luminance = 3.0F;
  shader_sentinel.clamped_log_luminance = 4.0F;
  shader_sentinel.target_inverse_luminance = 5.0F;
  shader_sentinel.previous_frame_weight = 6.0F;
  shader_sentinel.previous_inverse_luminance_r16 = {0x1111U, 7.0F};
  shader_sentinel.adapted_inverse_luminance_before_storage = 8.0F;
  shader_sentinel.stored_inverse_luminance_r16 = {0x2222U, 9.0F};
  HdrShaderAutoExposureResult shader_output = shader_sentinel;
  HdrShaderAutoExposureInput shader;
  shader.version += 1U;
  shader.exposure = (std::numeric_limits<float>::quiet_NaN)();
  failure = EvaluateHdrShaderAutoExposure(shader, shader_output);
  RequireFailure(failure, ValidationCode::UNSUPPORTED_VERSION, "version",
                 "shader exposure version was not validated first");
  Require(Equal(shader_output, shader_sentinel),
          "shader exposure version failure changed output");

  shader = {};
  shader.previous_inverse_luminance = 0.0F;
  failure = EvaluateHdrShaderAutoExposure(shader, shader_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "previous_inverse_luminance",
                 "zero previous shader luminance was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader previous-value failure changed output");

  shader = {};
  shader.delta_seconds = (std::numeric_limits<float>::infinity)();
  failure = EvaluateHdrShaderAutoExposure(shader, shader_output);
  RequireFailure(failure, ValidationCode::NON_FINITE_VALUE, "delta_seconds",
                 "infinite shader delta was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader delta failure changed output");

  shader = {};
  shader.average_log_luminance = 65505.0F;
  failure = EvaluateHdrShaderAutoExposure(shader, shader_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "average_log_luminance",
                 "shader log-luminance source overflow was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader source-range failure changed output");

  shader = {};
  shader.exposure = 16.0F;
  failure = EvaluateHdrShaderAutoExposure(shader, shader_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
                 "R16-overflowing adapted shader result was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader evaluation failure changed output");
}

void TestToneFailuresAreOrderedAndTransactional() {
  using namespace RoR::Render;
  HdrAnalyticFinalToneMapResult analytic_sentinel;
  analytic_sentinel.exposed_scene_linear = {1.0, 2.0, 3.0};
  analytic_sentinel.bloom_linear_approximation = {4.0, 5.0, 6.0};
  analytic_sentinel.combined_linear = {7.0, 8.0, 9.0};
  analytic_sentinel.filmic_normalized = {10.0, 11.0, 12.0};
  analytic_sentinel.analytic_output = {13.0, 14.0, 15.0};
  analytic_sentinel.alpha = 0.25;
  HdrAnalyticFinalToneMapResult analytic_output = analytic_sentinel;
  HdrAnalyticFinalToneMapInput analytic;
  analytic.version += 1U;
  analytic.scene_linear_hdr.x = (std::numeric_limits<double>::quiet_NaN)();
  ValidationResult failure =
      EvaluateHdrAnalyticFinalToneMap(analytic, analytic_output);
  RequireFailure(failure, ValidationCode::UNSUPPORTED_VERSION, "version",
                 "analytic tone version was not validated first");
  Require(Equal(analytic_output, analytic_sentinel),
          "analytic tone version failure changed output");

  analytic = {};
  analytic.scene_linear_hdr.x = (std::numeric_limits<double>::quiet_NaN)();
  analytic.bloom_gamma2_encoded.x = 2.0;
  failure = EvaluateHdrAnalyticFinalToneMap(analytic, analytic_output);
  RequireFailure(failure, ValidationCode::NON_FINITE_VALUE, "scene_linear_hdr",
                 "analytic scene validation order drifted");
  Require(Equal(analytic_output, analytic_sentinel),
          "analytic scene failure changed output");

  analytic = {};
  analytic.scene_linear_hdr.x = 65504.0001;
  failure = EvaluateHdrAnalyticFinalToneMap(analytic, analytic_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "scene_linear_hdr", "analytic scene overflow was accepted");
  Require(Equal(analytic_output, analytic_sentinel),
          "analytic scene range failure changed output");

  const auto require_analytic_failure =
      [&](const HdrAnalyticFinalToneMapInput &invalid, ValidationCode code,
          const char *field, const char *message) {
        analytic_output = analytic_sentinel;
        const ValidationResult result =
            EvaluateHdrAnalyticFinalToneMap(invalid, analytic_output);
        RequireFailure(result, code, field, message);
        Require(Equal(analytic_output, analytic_sentinel),
                "analytic tone failure was not fully transactional");
      };
  analytic = {};
  analytic.bloom_gamma2_encoded.z = 1.01;
  require_analytic_failure(analytic, ValidationCode::VALUE_OUT_OF_RANGE,
                           "bloom_gamma2_encoded",
                           "analytic bloom overflow was accepted");
  analytic = {};
  analytic.inverse_luminance = (std::numeric_limits<double>::quiet_NaN)();
  require_analytic_failure(analytic, ValidationCode::NON_FINITE_VALUE,
                           "inverse_luminance",
                           "analytic NaN luminance was accepted");
  analytic = {};
  analytic.alpha = (std::numeric_limits<double>::infinity)();
  require_analytic_failure(analytic, ValidationCode::NON_FINITE_VALUE, "alpha",
                           "analytic infinite alpha was accepted");

  HdrShaderFinalToneMapResult shader_sentinel;
  shader_sentinel.scene_linear_hdr_r16 = {1.0F, 2.0F, 3.0F};
  shader_sentinel.scene_linear_hdr_r16_bits = {1U, 2U, 3U};
  shader_sentinel.inverse_luminance_r16 = {4U, 4.0F};
  shader_sentinel.alpha_r16 = {5U, 5.0F};
  shader_sentinel.exposed_scene_linear = {6.0F, 7.0F, 8.0F};
  shader_sentinel.bloom_linear_approximation = {9.0F, 10.0F, 11.0F};
  shader_sentinel.combined_linear = {12.0F, 13.0F, 14.0F};
  shader_sentinel.filmic_normalized = {15.0F, 16.0F, 17.0F};
  shader_sentinel.shader_output = {18.0F, 19.0F, 20.0F};
  shader_sentinel.alpha = 0.5F;
  HdrShaderFinalToneMapResult shader_output = shader_sentinel;
  HdrShaderFinalToneMapInput shader;
  shader.version += 1U;
  shader.scene_linear_hdr.x = (std::numeric_limits<float>::quiet_NaN)();
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::UNSUPPORTED_VERSION, "version",
                 "shader tone version was not validated first");
  Require(Equal(shader_output, shader_sentinel),
          "shader tone version failure changed output");

  shader = {};
  shader.scene_linear_hdr.x = (std::numeric_limits<float>::quiet_NaN)();
  shader.bloom_gamma2_encoded.y = 2.0F;
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::NON_FINITE_VALUE, "scene_linear_hdr",
                 "shader scene validation order drifted");
  Require(Equal(shader_output, shader_sentinel),
          "shader scene NaN failure changed output");

  shader = {};
  shader.bloom_gamma2_encoded.y = (std::numeric_limits<float>::quiet_NaN)();
  shader.inverse_luminance = 0.0F;
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::NON_FINITE_VALUE,
                 "bloom_gamma2_encoded",
                 "shader bloom validation order drifted");
  Require(Equal(shader_output, shader_sentinel),
          "shader bloom failure changed output");

  shader = {};
  shader.scene_linear_hdr.x =
      std::nextafter(65504.0F, (std::numeric_limits<float>::infinity)());
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "scene_linear_hdr", "shader scene overflow was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader scene range failure changed output");

  shader = {};
  shader.bloom_gamma2_encoded.x = 1.01F;
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "bloom_gamma2_encoded", "shader bloom overflow was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader bloom range failure changed output");

  shader = {};
  shader.inverse_luminance =
      std::nextafter(65504.0F, (std::numeric_limits<float>::infinity)());
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "inverse_luminance", "shader luminance overflow was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader luminance range failure changed output");

  shader = {};
  shader.inverse_luminance = 0.0F;
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE,
                 "inverse_luminance", "zero shader luminance was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader zero-luminance failure changed output");

  shader = {};
  shader.alpha = 1.01F;
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::VALUE_OUT_OF_RANGE, "alpha",
                 "shader alpha overflow was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader alpha failure changed output");

  shader = {};
  shader.alpha = (std::numeric_limits<float>::quiet_NaN)();
  failure = EvaluateHdrShaderFinalToneMap(shader, shader_output);
  RequireFailure(failure, ValidationCode::NON_FINITE_VALUE, "alpha",
                 "shader NaN alpha was accepted");
  Require(Equal(shader_output, shader_sentinel),
          "shader NaN-alpha failure changed output");
}

} // namespace

int main() {
  TestIdentityAndDeclaredDomains();
  TestBinary16RoundToNearestEven();
  TestSignedFiniteBinary16Decode();
  TestAnalyticExposureGolden();
  TestShaderExposureAndR16Feedback();
  TestConditionedExposureComparison();
  TestAnalyticToneMapGolden();
  TestShaderToneMapGoldenAndTolerance();
  TestFiniteRepresentableExtremesAndMonotonicity();
  TestRandomizedCrossPrecisionCoverage();
  TestExposureFailuresAreOrderedAndTransactional();
  TestToneFailuresAreOrderedAndTransactional();
  std::cout
      << "pinned Ogre-Next analytic and shader HDR reference tests passed\n";
  return EXIT_SUCCESS;
}
