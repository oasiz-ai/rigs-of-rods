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
#include <cstring>
#include <limits>

namespace RoR::Render {
namespace {

static_assert(sizeof(float) == sizeof(std::uint32_t) &&
                  std::numeric_limits<float>::is_iec559 &&
                  std::numeric_limits<float>::radix == 2 &&
                  std::numeric_limits<float>::digits == 24,
              "HDR shader reference requires IEEE-754 binary32");
static_assert(sizeof(double) == sizeof(std::uint64_t) &&
                  std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::radix == 2 &&
                  std::numeric_limits<double>::digits == 53,
              "HDR analytic reference requires IEEE-754 binary64");

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

constexpr float kExposureCalibrationF = 1024.0F;
constexpr float kExposureOffsetF = 2.0F;
constexpr float kAutoExposureLogPivotF = 7.5F;
constexpr float kAdaptationBaseF = 0.25F;
constexpr float kBloomMultiplierF = 16.0F;

constexpr float kFilmicAF = 0.22F;
constexpr float kFilmicBF = 0.3F;
constexpr float kFilmicCF = 0.10F;
constexpr float kFilmicDF = 0.20F;
constexpr float kFilmicEF = 0.01F;
constexpr float kFilmicFF = 0.30F;
constexpr float kFilmicWhitePointF = 11.2F;
constexpr float kContrastF = 1.25F;
constexpr float kMidpointF = 0.5F;
constexpr float kLiftF = 0.11F;

bool IsWithin(double value, double minimum, double maximum) noexcept {
  return IsFinite(value) && value >= minimum && value <= maximum;
}

bool IsWithin(const Double3 &value, double minimum, double maximum) noexcept {
  return IsWithin(value.x, minimum, maximum) &&
         IsWithin(value.y, minimum, maximum) &&
         IsWithin(value.z, minimum, maximum);
}

double FilmicToneMapAnalytic(double value) noexcept {
  const double numerator =
      value * (kFilmicA * value + kFilmicC * kFilmicB) + kFilmicD * kFilmicE;
  const double denominator =
      value * (kFilmicA * value + kFilmicB) + kFilmicD * kFilmicF;
  return numerator / denominator - kFilmicE / kFilmicF;
}

float FilmicToneMapShader(float value) noexcept {
  const float cb = kFilmicCF * kFilmicBF;
  const float ax_plus_cb = kFilmicAF * value + cb;
  const float numerator = value * ax_plus_cb + kFilmicDF * kFilmicEF;
  const float ax_plus_b = kFilmicAF * value + kFilmicBF;
  const float denominator = value * ax_plus_b + kFilmicDF * kFilmicFF;
  return numerator / denominator - kFilmicEF / kFilmicFF;
}

std::uint32_t FloatBits(float value) noexcept {
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float FloatFromBits(std::uint32_t bits) noexcept {
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::uint32_t RoundShiftRightToEven(std::uint32_t value,
                                    unsigned int shift) noexcept {
  if (shift == 0U) {
    return value;
  }
  if (shift >= 32U) {
    return 0U;
  }
  const std::uint32_t quotient = value >> shift;
  const std::uint32_t mask = (std::uint32_t{1U} << shift) - 1U;
  const std::uint32_t remainder = value & mask;
  const std::uint32_t halfway = std::uint32_t{1U} << (shift - 1U);
  return quotient + static_cast<std::uint32_t>(
                        remainder > halfway ||
                        (remainder == halfway && (quotient & 1U) != 0U));
}

bool EncodePositiveFiniteBinary16(float input, std::uint16_t &bits) noexcept {
  if (!IsFinite(input) || input < 0.0F ||
      input > static_cast<float>(kHdrR16MaximumFinite)) {
    return false;
  }

  const std::uint32_t source = FloatBits(input);
  const std::uint32_t exponent = (source >> 23U) & 0xffU;
  const std::uint32_t mantissa = source & 0x7fffffU;
  if (exponent == 0U || input == 0.0F) {
    bits = 0U;
    return true;
  }

  const int unbiased_exponent = static_cast<int>(exponent) - 127;
  if (unbiased_exponent >= -14) {
    std::uint32_t half_exponent =
        static_cast<std::uint32_t>(unbiased_exponent + 15);
    std::uint32_t half_mantissa = RoundShiftRightToEven(mantissa, 13U);
    if (half_mantissa == 0x400U) {
      half_mantissa = 0U;
      ++half_exponent;
    }
    if (half_exponent >= 31U) {
      return false;
    }
    bits = static_cast<std::uint16_t>((half_exponent << 10U) | half_mantissa);
    return true;
  }

  const unsigned int subnormal_shift =
      static_cast<unsigned int>(-14 - unbiased_exponent);
  const std::uint32_t significant = 0x800000U | mantissa;
  const std::uint32_t half_mantissa =
      RoundShiftRightToEven(significant, 13U + subnormal_shift);
  bits = static_cast<std::uint16_t>(half_mantissa);
  return true;
}

float DecodePositiveFiniteBinary16(std::uint16_t bits) noexcept {
  const std::uint32_t exponent = (bits >> 10U) & 0x1fU;
  std::uint32_t mantissa = bits & 0x3ffU;
  if (exponent == 0U) {
    if (mantissa == 0U) {
      return 0.0F;
    }
    int unbiased_exponent = -14;
    while ((mantissa & 0x400U) == 0U) {
      mantissa <<= 1U;
      --unbiased_exponent;
    }
    mantissa &= 0x3ffU;
    const std::uint32_t float_exponent =
        static_cast<std::uint32_t>(unbiased_exponent + 127);
    return FloatFromBits((float_exponent << 23U) | (mantissa << 13U));
  }
  const std::uint32_t float_exponent = exponent - 15U + 127U;
  return FloatFromBits((float_exponent << 23U) | (mantissa << 13U));
}

bool QuantizePositiveFiniteBinary16(float input, HdrR16Float &output) noexcept {
  std::uint16_t bits = 0U;
  if (!EncodePositiveFiniteBinary16(input, bits)) {
    return false;
  }
  output.bits = bits;
  output.decoded = DecodePositiveFiniteBinary16(bits);
  return true;
}

ValidationResult ValidateExposureDomain(double exposure,
                                        double minimum_auto_exposure,
                                        double maximum_auto_exposure,
                                        double average_log_luminance,
                                        double previous_inverse_luminance,
                                        double delta_seconds) {
  if (!IsFinite(exposure)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "exposure", "exposure must be finite");
  }
  if (!IsWithin(exposure, kHdrMinimumExposure, kHdrMaximumExposure)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "exposure",
        "exposure is outside the approved HDR source envelope");
  }
  if (!IsFinite(minimum_auto_exposure)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "minimum_auto_exposure",
                                     "minimum auto exposure must be finite");
  }
  if (!IsWithin(minimum_auto_exposure, kHdrMinimumExposure,
                kHdrMaximumExposure)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "minimum_auto_exposure",
        "minimum auto exposure is outside the approved source envelope");
  }
  if (!IsFinite(maximum_auto_exposure)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "maximum_auto_exposure",
                                     "maximum auto exposure must be finite");
  }
  if (!IsWithin(maximum_auto_exposure, kHdrMinimumExposure,
                kHdrMaximumExposure)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "maximum_auto_exposure",
        "maximum auto exposure is outside the approved source envelope");
  }
  if (minimum_auto_exposure > maximum_auto_exposure) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "minimum_auto_exposure",
        "minimum auto exposure must not exceed maximum auto exposure");
  }
  if (!IsFinite(average_log_luminance)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "average_log_luminance",
                                     "average log luminance must be finite");
  }
  if (!IsWithin(average_log_luminance, -kHdrR16MaximumFinite,
                kHdrR16MaximumFinite)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "average_log_luminance",
        "average log luminance exceeds the R16_FLOAT source envelope");
  }
  if (!IsFinite(previous_inverse_luminance)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "previous_inverse_luminance",
        "previous inverse luminance must be finite");
  }
  if (!IsWithin(previous_inverse_luminance, kHdrR16MinimumPositive,
                kHdrR16MaximumFinite)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "previous_inverse_luminance",
        "previous inverse luminance exceeds the positive R16_FLOAT envelope");
  }
  if (!IsFinite(delta_seconds)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "delta_seconds",
                                     "frame delta must be finite");
  }
  if (!IsWithin(delta_seconds, 0.0, kHdrMaximumFrameDeltaSeconds)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "delta_seconds",
        "frame delta is outside the approved HDR source envelope");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateToneMapDomain(const Double3 &scene_linear_hdr,
                                       const Double3 &bloom_gamma2_encoded,
                                       double inverse_luminance, double alpha) {
  if (!IsFinite(scene_linear_hdr)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "scene_linear_hdr",
                                     "linear HDR scene color must be finite");
  }
  if (!IsWithin(scene_linear_hdr, 0.0, kHdrR16MaximumFinite)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "scene_linear_hdr",
        "linear HDR scene color exceeds the RGBA16_FLOAT source envelope");
  }
  if (!IsFinite(bloom_gamma2_encoded)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "bloom_gamma2_encoded",
                                     "gamma-2 bloom sample must be finite");
  }
  if (!IsWithin(bloom_gamma2_encoded, 0.0, 1.0)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "bloom_gamma2_encoded",
        "gamma-2 bloom sample must be in the UNORM interval [0, 1]");
  }
  if (!IsFinite(inverse_luminance)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "inverse_luminance",
                                     "inverse luminance must be finite");
  }
  if (!IsWithin(inverse_luminance, kHdrR16MinimumPositive,
                kHdrR16MaximumFinite)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "inverse_luminance",
        "inverse luminance exceeds the positive R16_FLOAT envelope");
  }
  if (!IsFinite(alpha)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE, "alpha",
                                     "alpha must be finite");
  }
  if (!IsWithin(alpha, 0.0, 1.0)) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "alpha", "alpha must be in [0, 1]");
  }
  return ValidationResult::Success();
}

ValidationResult CompareCrossPrecisionScalar(
    double analytic_value, double shader_value, double allowed_difference,
    const char *field, HdrCrossPrecisionComparison &output,
    std::size_t element_index = ValidationResult::kNoElement) {
  const double absolute_difference = std::fabs(analytic_value - shader_value);
  if (!IsFinite(analytic_value) || !IsFinite(shader_value) ||
      !IsFinite(absolute_difference) || !IsFinite(allowed_difference) ||
      allowed_difference < 0.0) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, field,
        "HDR cross-precision comparison produced a non-finite bound",
        element_index);
  }
  if (absolute_difference > allowed_difference) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, field,
        "HDR analytic and shader values exceed their declared comparison bound",
        element_index);
  }
  HdrCrossPrecisionComparison candidate;
  candidate.analytic_value = analytic_value;
  candidate.shader_value = shader_value;
  candidate.absolute_difference = absolute_difference;
  candidate.allowed_difference = allowed_difference;
  output = candidate;
  return ValidationResult::Success();
}

double ScalarCrossPrecisionTolerance(double analytic_value,
                                     double shader_value) noexcept {
  return kHdrAnalyticShaderAbsoluteTolerance +
         kHdrAnalyticShaderRelativeTolerance *
             (std::max)(std::fabs(analytic_value), std::fabs(shader_value));
}

} // namespace

ValidationResult DecodeFiniteHdrR16Float(std::uint16_t bits,
                                         HdrR16Float &output) {
  constexpr std::uint16_t kBinary16Sign = 0x8000U;
  constexpr std::uint16_t kBinary16Magnitude = 0x7fffU;
  constexpr std::uint16_t kBinary16Exponent = 0x7c00U;
  if ((bits & kBinary16Exponent) == kBinary16Exponent) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "bits",
        "R16_FLOAT bit pattern must encode a finite value");
  }

  HdrR16Float candidate;
  candidate.bits = bits;
  candidate.decoded =
      DecodePositiveFiniteBinary16(bits & kBinary16Magnitude);
  if ((bits & kBinary16Sign) != 0U) {
    candidate.decoded = -candidate.decoded;
  }
  if (!IsFinite(candidate.decoded)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "bits",
        "R16_FLOAT decoding produced a non-finite value");
  }
  output = candidate;
  return ValidationResult::Success();
}

ValidationResult QuantizeHdrR16Float(float input, HdrR16Float &output) {
  if (!IsFinite(input)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE, "value",
                                     "R16_FLOAT source must be finite");
  }
  if (input < 0.0F || input > static_cast<float>(kHdrR16MaximumFinite)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "value",
        "R16_FLOAT source must be nonnegative and at most 65504");
  }
  HdrR16Float candidate;
  if (!QuantizePositiveFiniteBinary16(input, candidate) ||
      !IsFinite(candidate.decoded)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "R16_FLOAT conversion produced an infinity");
  }
  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
EvaluateHdrAnalyticAutoExposure(const HdrAnalyticAutoExposureInput &input,
                                HdrAnalyticAutoExposureResult &output) {
  if (input.version != kHdrAnalyticReferenceVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported analytic HDR reference version");
  }
  const ValidationResult domain = ValidateExposureDomain(
      input.exposure, input.minimum_auto_exposure, input.maximum_auto_exposure,
      input.average_log_luminance, input.previous_inverse_luminance,
      input.delta_seconds);
  if (!domain) {
    return domain;
  }

  HdrAnalyticAutoExposureResult candidate;
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
        "analytic HDR exposure evaluation left its finite positive domain");
  }

  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
EvaluateHdrShaderAutoExposure(const HdrShaderAutoExposureInput &input,
                              HdrShaderAutoExposureResult &output) {
  if (input.version != kHdrShaderReferenceVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported shader HDR reference version");
  }
  const ValidationResult domain = ValidateExposureDomain(
      static_cast<double>(input.exposure),
      static_cast<double>(input.minimum_auto_exposure),
      static_cast<double>(input.maximum_auto_exposure),
      static_cast<double>(input.average_log_luminance),
      static_cast<double>(input.previous_inverse_luminance),
      static_cast<double>(input.delta_seconds));
  if (!domain) {
    return domain;
  }

  HdrShaderAutoExposureResult candidate;
  if (!QuantizePositiveFiniteBinary16(
          input.previous_inverse_luminance,
          candidate.previous_inverse_luminance_r16) ||
      !(candidate.previous_inverse_luminance_r16.decoded > 0.0F)) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "previous_inverse_luminance",
                                     "previous inverse luminance cannot be "
                                     "represented as positive R16_FLOAT");
  }

  const float exposure_exponent = input.exposure - kExposureOffsetF;
  const float exposure_exp = std::exp(exposure_exponent);
  candidate.exposure_numerator = kExposureCalibrationF * exposure_exp;
  candidate.minimum_log_luminance =
      kAutoExposureLogPivotF - input.maximum_auto_exposure;
  candidate.maximum_log_luminance =
      kAutoExposureLogPivotF - input.minimum_auto_exposure;
  candidate.clamped_log_luminance =
      (std::max)(candidate.minimum_log_luminance,
                 (std::min)(candidate.maximum_log_luminance,
                            input.average_log_luminance));
  const float luminance_denominator = std::exp(candidate.clamped_log_luminance);
  candidate.target_inverse_luminance =
      candidate.exposure_numerator / luminance_denominator;
  candidate.previous_frame_weight =
      std::pow(kAdaptationBaseF, input.delta_seconds);
  const float new_weight = 1.0F - candidate.previous_frame_weight;
  const float weighted_target = candidate.target_inverse_luminance * new_weight;
  const float weighted_previous =
      candidate.previous_inverse_luminance_r16.decoded *
      candidate.previous_frame_weight;
  candidate.adapted_inverse_luminance_before_storage =
      weighted_target + weighted_previous;

  if (!IsFinite(candidate.exposure_numerator) ||
      !IsFinite(candidate.minimum_log_luminance) ||
      !IsFinite(candidate.maximum_log_luminance) ||
      !IsFinite(candidate.clamped_log_luminance) ||
      !IsFinite(candidate.target_inverse_luminance) ||
      !IsFinite(candidate.previous_frame_weight) ||
      !IsFinite(candidate.adapted_inverse_luminance_before_storage) ||
      !(candidate.exposure_numerator > 0.0F) ||
      !(candidate.target_inverse_luminance > 0.0F) ||
      candidate.previous_frame_weight < 0.0F ||
      candidate.previous_frame_weight > 1.0F ||
      !(candidate.adapted_inverse_luminance_before_storage > 0.0F)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "shader HDR exposure evaluation overflowed or produced a NaN");
  }
  if (!QuantizePositiveFiniteBinary16(
          candidate.adapted_inverse_luminance_before_storage,
          candidate.stored_inverse_luminance_r16) ||
      !(candidate.stored_inverse_luminance_r16.decoded > 0.0F)) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "evaluation",
                                     "shader HDR exposure result cannot be "
                                     "stored as finite positive R16_FLOAT");
  }

  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
CompareHdrAutoExposureReferences(const HdrShaderAutoExposureInput &input,
                                 HdrAutoExposureComparisonResult &output) {
  HdrAutoExposureComparisonResult candidate;
  ValidationResult validation =
      EvaluateHdrShaderAutoExposure(input, candidate.shader);
  if (!validation) {
    return validation;
  }

  candidate.analytic_input.exposure = static_cast<double>(input.exposure);
  candidate.analytic_input.minimum_auto_exposure =
      static_cast<double>(input.minimum_auto_exposure);
  candidate.analytic_input.maximum_auto_exposure =
      static_cast<double>(input.maximum_auto_exposure);
  candidate.analytic_input.average_log_luminance =
      static_cast<double>(input.average_log_luminance);
  candidate.analytic_input.previous_inverse_luminance = static_cast<double>(
      candidate.shader.previous_inverse_luminance_r16.decoded);
  candidate.analytic_input.delta_seconds =
      static_cast<double>(input.delta_seconds);
  validation = EvaluateHdrAnalyticAutoExposure(candidate.analytic_input,
                                               candidate.analytic);
  if (!validation) {
    return validation;
  }

  validation = CompareCrossPrecisionScalar(
      candidate.analytic.target_inverse_luminance,
      static_cast<double>(candidate.shader.target_inverse_luminance),
      ScalarCrossPrecisionTolerance(
          candidate.analytic.target_inverse_luminance,
          static_cast<double>(candidate.shader.target_inverse_luminance)),
      "target_inverse_luminance_comparison",
      candidate.target_inverse_luminance);
  if (!validation) {
    return validation;
  }
  validation = CompareCrossPrecisionScalar(
      candidate.analytic.previous_frame_weight,
      static_cast<double>(candidate.shader.previous_frame_weight),
      ScalarCrossPrecisionTolerance(
          candidate.analytic.previous_frame_weight,
          static_cast<double>(candidate.shader.previous_frame_weight)),
      "previous_frame_weight_comparison", candidate.previous_frame_weight);
  if (!validation) {
    return validation;
  }

  const double analytic_target = candidate.analytic.target_inverse_luminance;
  const double shader_target =
      static_cast<double>(candidate.shader.target_inverse_luminance);
  const double analytic_weight = candidate.analytic.previous_frame_weight;
  const double shader_weight =
      static_cast<double>(candidate.shader.previous_frame_weight);
  const double previous = static_cast<double>(
      candidate.shader.previous_inverse_luminance_r16.decoded);
  candidate.adapted_conditioning_bound =
      std::fabs(1.0 - analytic_weight) *
          std::fabs(analytic_target - shader_target) +
      std::fabs(shader_target - previous) *
          std::fabs(analytic_weight - shader_weight);
  candidate.adapted_binary32_rounding_bound =
      kHdrBinary32Gamma5 * (std::fabs(shader_target * (1.0 - shader_weight)) +
                            std::fabs(previous * shader_weight)) +
      4.0 * static_cast<double>((std::numeric_limits<float>::denorm_min)());
  const double adapted_bound = candidate.adapted_conditioning_bound +
                               candidate.adapted_binary32_rounding_bound;
  validation = CompareCrossPrecisionScalar(
      candidate.analytic.adapted_inverse_luminance,
      static_cast<double>(
          candidate.shader.adapted_inverse_luminance_before_storage),
      adapted_bound, "adapted_inverse_luminance_comparison",
      candidate.adapted_inverse_luminance);
  if (!validation) {
    return validation;
  }

  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
EvaluateHdrAnalyticFinalToneMap(const HdrAnalyticFinalToneMapInput &input,
                                HdrAnalyticFinalToneMapResult &output) {
  if (input.version != kHdrAnalyticReferenceVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported analytic HDR reference version");
  }
  const ValidationResult domain =
      ValidateToneMapDomain(input.scene_linear_hdr, input.bloom_gamma2_encoded,
                            input.inverse_luminance, input.alpha);
  if (!domain) {
    return domain;
  }

  HdrAnalyticFinalToneMapResult candidate;
  const std::array<double, 3U> scene{{input.scene_linear_hdr.x,
                                      input.scene_linear_hdr.y,
                                      input.scene_linear_hdr.z}};
  const std::array<double, 3U> bloom{{input.bloom_gamma2_encoded.x,
                                      input.bloom_gamma2_encoded.y,
                                      input.bloom_gamma2_encoded.z}};
  std::array<double, 3U> exposed{};
  std::array<double, 3U> bloom_linear{};
  std::array<double, 3U> combined{};
  std::array<double, 3U> filmic{};
  std::array<double, 3U> analytic_output{};
  const double white_scale = FilmicToneMapAnalytic(kFilmicWhitePoint);

  for (std::size_t channel = 0U; channel < scene.size(); ++channel) {
    exposed[channel] = scene[channel] * input.inverse_luminance;
    bloom_linear[channel] = bloom[channel] * bloom[channel];
    combined[channel] =
        exposed[channel] + bloom_linear[channel] * kBloomMultiplier;
    filmic[channel] = FilmicToneMapAnalytic(combined[channel]) / white_scale;
    analytic_output[channel] =
        (filmic[channel] - kMidpoint) * kContrast + kMidpoint + kLift;
  }
  candidate.exposed_scene_linear = {exposed[0U], exposed[1U], exposed[2U]};
  candidate.bloom_linear_approximation = {bloom_linear[0U], bloom_linear[1U],
                                          bloom_linear[2U]};
  candidate.combined_linear = {combined[0U], combined[1U], combined[2U]};
  candidate.filmic_normalized = {filmic[0U], filmic[1U], filmic[2U]};
  candidate.analytic_output = {analytic_output[0U], analytic_output[1U],
                               analytic_output[2U]};
  candidate.alpha = input.alpha;

  if (!IsFinite(white_scale) || !(white_scale > 0.0) ||
      !IsFinite(candidate.exposed_scene_linear) ||
      !IsFinite(candidate.bloom_linear_approximation) ||
      !IsFinite(candidate.combined_linear) ||
      !IsFinite(candidate.filmic_normalized) ||
      !IsFinite(candidate.analytic_output)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "analytic HDR tone-map evaluation left its finite output domain");
  }

  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
EvaluateHdrShaderFinalToneMap(const HdrShaderFinalToneMapInput &input,
                              HdrShaderFinalToneMapResult &output) {
  if (input.version != kHdrShaderReferenceVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported shader HDR reference version");
  }
  const Double3 scene{static_cast<double>(input.scene_linear_hdr.x),
                      static_cast<double>(input.scene_linear_hdr.y),
                      static_cast<double>(input.scene_linear_hdr.z)};
  const Double3 bloom{static_cast<double>(input.bloom_gamma2_encoded.x),
                      static_cast<double>(input.bloom_gamma2_encoded.y),
                      static_cast<double>(input.bloom_gamma2_encoded.z)};
  const ValidationResult domain = ValidateToneMapDomain(
      scene, bloom, static_cast<double>(input.inverse_luminance),
      static_cast<double>(input.alpha));
  if (!domain) {
    return domain;
  }

  HdrShaderFinalToneMapResult candidate;
  const std::array<float, 3U> supplied_scene{{input.scene_linear_hdr.x,
                                              input.scene_linear_hdr.y,
                                              input.scene_linear_hdr.z}};
  std::array<float, 3U> quantized_scene{};
  for (std::size_t channel = 0U; channel < supplied_scene.size(); ++channel) {
    HdrR16Float quantized;
    if (!QuantizePositiveFiniteBinary16(supplied_scene[channel], quantized)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "scene_linear_hdr",
          "scene color cannot be represented as finite RGBA16_FLOAT", channel);
    }
    quantized_scene[channel] = quantized.decoded;
    candidate.scene_linear_hdr_r16_bits[channel] = quantized.bits;
  }
  candidate.scene_linear_hdr_r16 = {quantized_scene[0U], quantized_scene[1U],
                                    quantized_scene[2U]};
  if (!QuantizePositiveFiniteBinary16(input.inverse_luminance,
                                      candidate.inverse_luminance_r16) ||
      !(candidate.inverse_luminance_r16.decoded > 0.0F)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "inverse_luminance",
        "inverse luminance cannot be represented as positive R16_FLOAT");
  }
  if (!QuantizePositiveFiniteBinary16(input.alpha, candidate.alpha_r16)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "alpha",
        "alpha cannot be represented as finite RGBA16_FLOAT");
  }

  const std::array<float, 3U> bloom_sample{{input.bloom_gamma2_encoded.x,
                                            input.bloom_gamma2_encoded.y,
                                            input.bloom_gamma2_encoded.z}};
  std::array<float, 3U> exposed{};
  std::array<float, 3U> bloom_linear{};
  std::array<float, 3U> combined{};
  std::array<float, 3U> filmic{};
  std::array<float, 3U> shader_output{};
  const float white_scale = FilmicToneMapShader(kFilmicWhitePointF);
  if (!IsFinite(white_scale) || !(white_scale > 0.0F)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "shader HDR filmic white point produced an invalid normalization");
  }

  for (std::size_t channel = 0U; channel < quantized_scene.size(); ++channel) {
    exposed[channel] =
        quantized_scene[channel] * candidate.inverse_luminance_r16.decoded;
    bloom_linear[channel] = bloom_sample[channel] * bloom_sample[channel];
    const float scaled_bloom = bloom_linear[channel] * kBloomMultiplierF;
    combined[channel] = exposed[channel] + scaled_bloom;
    filmic[channel] = FilmicToneMapShader(combined[channel]) / white_scale;
    const float contrasted = (filmic[channel] - kMidpointF) * kContrastF;
    shader_output[channel] = contrasted + kMidpointF + kLiftF;
  }
  candidate.exposed_scene_linear = {exposed[0U], exposed[1U], exposed[2U]};
  candidate.bloom_linear_approximation = {bloom_linear[0U], bloom_linear[1U],
                                          bloom_linear[2U]};
  candidate.combined_linear = {combined[0U], combined[1U], combined[2U]};
  candidate.filmic_normalized = {filmic[0U], filmic[1U], filmic[2U]};
  candidate.shader_output = {shader_output[0U], shader_output[1U],
                             shader_output[2U]};
  candidate.alpha = candidate.alpha_r16.decoded;

  if (!IsFinite(candidate.exposed_scene_linear) ||
      !IsFinite(candidate.bloom_linear_approximation) ||
      !IsFinite(candidate.combined_linear) ||
      !IsFinite(candidate.filmic_normalized) ||
      !IsFinite(candidate.shader_output)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "shader HDR tone-map evaluation overflowed or produced a NaN");
  }

  output = candidate;
  return ValidationResult::Success();
}

ValidationResult
CompareHdrFinalToneMapReferences(const HdrShaderFinalToneMapInput &input,
                                 HdrFinalToneMapComparisonResult &output) {
  HdrFinalToneMapComparisonResult candidate;
  ValidationResult validation =
      EvaluateHdrShaderFinalToneMap(input, candidate.shader);
  if (!validation) {
    return validation;
  }

  candidate.analytic_input.scene_linear_hdr = {
      static_cast<double>(candidate.shader.scene_linear_hdr_r16.x),
      static_cast<double>(candidate.shader.scene_linear_hdr_r16.y),
      static_cast<double>(candidate.shader.scene_linear_hdr_r16.z)};
  candidate.analytic_input.bloom_gamma2_encoded = {
      static_cast<double>(input.bloom_gamma2_encoded.x),
      static_cast<double>(input.bloom_gamma2_encoded.y),
      static_cast<double>(input.bloom_gamma2_encoded.z)};
  candidate.analytic_input.inverse_luminance =
      static_cast<double>(candidate.shader.inverse_luminance_r16.decoded);
  candidate.analytic_input.alpha =
      static_cast<double>(candidate.shader.alpha_r16.decoded);
  validation = EvaluateHdrAnalyticFinalToneMap(candidate.analytic_input,
                                               candidate.analytic);
  if (!validation) {
    return validation;
  }

  const std::array<double, 3U> analytic_output{
      {candidate.analytic.analytic_output.x,
       candidate.analytic.analytic_output.y,
       candidate.analytic.analytic_output.z}};
  const std::array<double, 3U> shader_output{
      {static_cast<double>(candidate.shader.shader_output.x),
       static_cast<double>(candidate.shader.shader_output.y),
       static_cast<double>(candidate.shader.shader_output.z)}};
  for (std::size_t channel = 0U; channel < analytic_output.size(); ++channel) {
    validation = CompareCrossPrecisionScalar(
        analytic_output[channel], shader_output[channel],
        ScalarCrossPrecisionTolerance(analytic_output[channel],
                                      shader_output[channel]),
        "tone_map_output_comparison", candidate.output_channels[channel],
        channel);
    if (!validation) {
      return validation;
    }
  }

  output = candidate;
  return ValidationResult::Success();
}

} // namespace RoR::Render
