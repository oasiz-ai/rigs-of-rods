/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Portable numerical references for the pinned Ogre-Next HDR pipeline.

#pragma once

#include "RenderMath.h"
#include "RenderValidation.h"

#include <array>
#include <cstdint>

namespace RoR::Render {

/// The analytic behavior evaluates the pinned equations in IEEE-754 binary64.
/// It is deliberately free of render-target quantization and is useful for
/// authoring, calibration, and high-precision regression fixtures.
constexpr std::uint32_t kHdrAnalyticReferenceVersion = 1U;

/// The shader behavior evaluates the pinned equations in IEEE-754 binary32 and
/// models the R16_FLOAT storage boundary used for luminance feedback.
constexpr std::uint32_t kHdrShaderReferenceVersion = 1U;

constexpr const char kHdrReferenceOgreNextCommit[] =
    "37149a802de747f6806996fa3067b0748ecc1084";

/// RoR's approved finite input envelope for the pinned sample. The exposure
/// interval safely contains every upstream HDR sample preset while preventing
/// the float exp/multiply overflow accepted by an unbounded binary64 model.
constexpr double kHdrMinimumExposure = -16.0;
constexpr double kHdrMaximumExposure = 16.0;
constexpr double kHdrMaximumFrameDeltaSeconds = 60.0;
constexpr double kHdrR16MaximumFinite = 65504.0;
constexpr double kHdrR16MinimumPositive = 0x1p-24;

/// A binary64 analytic result and a strict binary32 shader-equation result are
/// compared with both tolerances: abs(a-b) <= absolute + relative*max(abs(a),
/// abs(b)). This scalar rule applies to well-conditioned primitive results;
/// adapted exposure uses the separately declared conditioning-aware bound.
/// CPU R16 conversion and CPU golden fixtures are compared by exact binary16
/// bits. A real GPU value is validated separately using the numerical equation
/// bound plus one binary16 storage ULP before its native bits become temporal
/// history.
constexpr double kHdrAnalyticShaderAbsoluteTolerance = 2.0e-6;
constexpr double kHdrAnalyticShaderRelativeTolerance = 2.0e-5;

/// Unit roundoff for an IEEE-754 binary32 operation in round-to-nearest mode
/// and Higham's gamma bound for the five explicitly sequenced operations in
/// the adapted-exposure expression.
constexpr double kHdrBinary32UnitRoundoff = 0x1p-24;
constexpr double kHdrBinary32Gamma5 =
    (5.0 * kHdrBinary32UnitRoundoff) / (1.0 - 5.0 * kHdrBinary32UnitRoundoff);

struct HdrR16Float {
  std::uint16_t bits = 0U;
  float decoded = 0.0F;
};

/// Decode one finite IEEE-754 binary16 bit pattern without changing its sign
/// or payload-free value. Unlike QuantizeHdrR16Float, this admits negative
/// finite values because Ogre's intermediate log-luminance targets are signed.
/// Binary16 infinities and NaNs fail transactionally.
[[nodiscard]] ValidationResult DecodeFiniteHdrR16Float(
    std::uint16_t bits, HdrR16Float &output);

/// Deterministically round an admitted nonnegative binary32 source value in
/// `[0, 65504]` to IEEE-754 binary16 using round-to-nearest, ties-to-even.
/// Negative zero is intentionally canonicalized to positive zero. Values above
/// the admitted source-format maximum fail even when binary16 rounding could
/// return 65504. A failure leaves `output` unchanged.
[[nodiscard]] ValidationResult QuantizeHdrR16Float(float input,
                                                   HdrR16Float &output);

struct HdrCrossPrecisionComparison {
  double analytic_value = 0.0;
  double shader_value = 0.0;
  double absolute_difference = 0.0;
  double allowed_difference = 0.0;
};

struct HdrAnalyticAutoExposureInput {
  std::uint32_t version = kHdrAnalyticReferenceVersion;
  double exposure = 0.0;
  double minimum_auto_exposure = -1.0;
  double maximum_auto_exposure = 2.5;
  /// Average natural-log luminance presented to DownScale03 after its four
  /// samples have been averaged.
  double average_log_luminance = 0.0;
  /// Ideal previous value. Unlike shader mode, this value is not quantized at
  /// the R16 feedback boundary.
  double previous_inverse_luminance = 1.0;
  double delta_seconds = 1.0 / 48.0;
};

struct HdrAnalyticAutoExposureResult {
  double exposure_numerator = 0.0;
  double minimum_log_luminance = 0.0;
  double maximum_log_luminance = 0.0;
  double clamped_log_luminance = 0.0;
  double target_inverse_luminance = 0.0;
  double previous_frame_weight = 0.0;
  double adapted_inverse_luminance = 0.0;
};

/// Evaluate the ideal analytic exposure equation in binary64. Inputs are
/// restricted to the declared source envelope, but no R16 storage rounding is
/// applied. A failure leaves `output` unchanged.
[[nodiscard]] ValidationResult
EvaluateHdrAnalyticAutoExposure(const HdrAnalyticAutoExposureInput &input,
                                HdrAnalyticAutoExposureResult &output);

struct HdrShaderAutoExposureInput {
  std::uint32_t version = kHdrShaderReferenceVersion;
  float exposure = 0.0F;
  float minimum_auto_exposure = -1.0F;
  float maximum_auto_exposure = 2.5F;
  float average_log_luminance = 0.0F;
  /// The value read from oldLumRt. It is deterministically quantized to R16
  /// before the binary32 shader equation is evaluated.
  float previous_inverse_luminance = 1.0F;
  float delta_seconds = 1.0F / 48.0F;
};

struct HdrShaderAutoExposureResult {
  float exposure_numerator = 0.0F;
  float minimum_log_luminance = 0.0F;
  float maximum_log_luminance = 0.0F;
  float clamped_log_luminance = 0.0F;
  float target_inverse_luminance = 0.0F;
  float previous_frame_weight = 0.0F;
  HdrR16Float previous_inverse_luminance_r16{};
  float adapted_inverse_luminance_before_storage = 0.0F;
  HdrR16Float stored_inverse_luminance_r16{};
};

/// Evaluate one DownScale03 frame in binary32 and apply the exact R16_FLOAT
/// feedback storage conversion. Pass `stored_inverse_luminance_r16.decoded` as
/// the next frame's previous value. Any float overflow, NaN, or R16 infinity
/// fails transactionally.
[[nodiscard]] ValidationResult
EvaluateHdrShaderAutoExposure(const HdrShaderAutoExposureInput &input,
                              HdrShaderAutoExposureResult &output);

struct HdrAutoExposureComparisonResult {
  /// Binary64 input reconstructed from the shader input after applying the
  /// shader path's exact previous-frame R16 storage boundary.
  HdrAnalyticAutoExposureInput analytic_input{};
  HdrAnalyticAutoExposureResult analytic{};
  HdrShaderAutoExposureResult shader{};
  HdrCrossPrecisionComparison target_inverse_luminance{};
  HdrCrossPrecisionComparison previous_frame_weight{};
  HdrCrossPrecisionComparison adapted_inverse_luminance{};
  double adapted_conditioning_bound = 0.0;
  double adapted_binary32_rounding_bound = 0.0;
};

/// Evaluate both exposure behaviors from one shader-domain input. Primitive
/// target/weight values use the scalar tolerance. The adapted result uses a
/// sensitivity bound derived from their measured cross-precision differences
/// plus a gamma-5 binary32 operation-rounding term. R16 feedback bits remain a
/// separate exact gate. A failure leaves `output` unchanged.
[[nodiscard]] ValidationResult
CompareHdrAutoExposureReferences(const HdrShaderAutoExposureInput &input,
                                 HdrAutoExposureComparisonResult &output);

struct HdrAnalyticFinalToneMapInput {
  std::uint32_t version = kHdrAnalyticReferenceVersion;
  Double3 scene_linear_hdr{};
  /// Final bloom sample in Ogre's historical gamma-2 encoding. This is not the
  /// standard sRGB transfer function.
  Double3 bloom_gamma2_encoded{};
  double inverse_luminance = 1.0;
  double alpha = 1.0;
};

struct HdrAnalyticFinalToneMapResult {
  Double3 exposed_scene_linear{};
  Double3 bloom_linear_approximation{};
  Double3 combined_linear{};
  Double3 filmic_normalized{};
  Double3 analytic_output{};
  double alpha = 1.0;
};

/// Evaluate the ideal Hable/final-composite equation in binary64 without
/// source-texture quantization or framebuffer conversion.
[[nodiscard]] ValidationResult
EvaluateHdrAnalyticFinalToneMap(const HdrAnalyticFinalToneMapInput &input,
                                HdrAnalyticFinalToneMapResult &output);

struct HdrShaderFinalToneMapInput {
  std::uint32_t version = kHdrShaderReferenceVersion;
  /// Values supplied for the RGBA16_FLOAT scene sample. They are rounded to
  /// binary16 before shader evaluation.
  Float3 scene_linear_hdr{};
  /// Filtered R10G10B10A2_UNORM bloom sample after Ogre's explicit gamma-2
  /// encoding/blur chain. Filtering means it need not lie on a 10-bit step.
  Float3 bloom_gamma2_encoded{};
  /// Value supplied for the R16_FLOAT luminance sample; rounded before use.
  float inverse_luminance = 1.0F;
  float alpha = 1.0F;
};

struct HdrShaderFinalToneMapResult {
  Float3 scene_linear_hdr_r16{};
  std::array<std::uint16_t, 3U> scene_linear_hdr_r16_bits{};
  HdrR16Float inverse_luminance_r16{};
  HdrR16Float alpha_r16{};
  Float3 exposed_scene_linear{};
  Float3 bloom_linear_approximation{};
  Float3 combined_linear{};
  Float3 filmic_normalized{};
  /// Binary32 pre-framebuffer shader-equation result. GPU contraction and
  /// transcendental implementations are compared using the declared tolerance,
  /// never by claiming bit identity across APIs.
  Float3 shader_output{};
  float alpha = 1.0F;
};

[[nodiscard]] ValidationResult
EvaluateHdrShaderFinalToneMap(const HdrShaderFinalToneMapInput &input,
                              HdrShaderFinalToneMapResult &output);

struct HdrFinalToneMapComparisonResult {
  /// The analytic input uses the shader path's decoded RGBA16/R16 scene,
  /// inverse-luminance, and alpha samples. This prevents texture quantization
  /// error from being mislabeled as binary32 equation error.
  HdrAnalyticFinalToneMapInput analytic_input{};
  HdrAnalyticFinalToneMapResult analytic{};
  HdrShaderFinalToneMapResult shader{};
  std::array<HdrCrossPrecisionComparison, 3U> output_channels{};
};

/// Evaluate and compare the final tone-map equations after first equalizing the
/// source texture boundaries. The exact source R16 bits remain exposed in
/// `shader` for an independent storage gate. A failure leaves `output`
/// unchanged.
[[nodiscard]] ValidationResult
CompareHdrFinalToneMapReferences(const HdrShaderFinalToneMapInput &input,
                                 HdrFinalToneMapComparisonResult &output);

} // namespace RoR::Render
