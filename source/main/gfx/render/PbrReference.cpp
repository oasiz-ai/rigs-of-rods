/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PbrReference.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace RoR::Render {
namespace {

constexpr double kOgreNextPi = 3.141592654;
constexpr double kOgreNextInversePi = 1.0 / kOgreNextPi;
constexpr double kDielectricF0 = 0.04;
constexpr double kMinimumMicrofacetAlpha = 0.001;

struct Vector3d {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

double Dot(const Vector3d &lhs, const Vector3d &rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

double ClampUnit(double value) noexcept {
  return (std::max)(0.0, (std::min)(1.0, value));
}

double Pow5(double value) noexcept {
  const double squared = value * value;
  return squared * squared * value;
}

bool Normalize(double x, double y, double z, Vector3d &output) noexcept {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    return false;
  }
  const double scale = (std::max)({std::fabs(x), std::fabs(y), std::fabs(z)});
  if (!(scale > 0.0) || !std::isfinite(scale)) {
    return false;
  }
  const double sx = x / scale;
  const double sy = y / scale;
  const double sz = z / scale;
  const double scaled_length = std::sqrt(sx * sx + sy * sy + sz * sz);
  if (!(scaled_length > 0.0) || !std::isfinite(scaled_length)) {
    return false;
  }
  const double inverse_length = 1.0 / (scale * scaled_length);
  output = {x * inverse_length, y * inverse_length, z * inverse_length};
  return std::isfinite(output.x) && std::isfinite(output.y) &&
         std::isfinite(output.z);
}

bool Normalize(const Float3 &input, Vector3d &output) noexcept {
  return Normalize(static_cast<double>(input.x), static_cast<double>(input.y),
                   static_cast<double>(input.z), output);
}

bool IsNormalizedLinearColor(const Float3 &color) noexcept {
  return IsFinite(color) && color.x >= 0.0F && color.x <= 1.0F &&
         color.y >= 0.0F && color.y <= 1.0F && color.z >= 0.0F &&
         color.z <= 1.0F;
}

} // namespace

ValidationResult
EvaluatePbrDirectReference(const PbrDirectReferenceInput &input,
                           PbrDirectReferenceResult &output) {
  if (input.version != kPbrDirectReferenceVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported direct PBR reference version");
  }
  if (!IsNormalizedLinearColor(input.base_color_linear)) {
    return ValidationResult::Failure(
        IsFinite(input.base_color_linear) ? ValidationCode::VALUE_OUT_OF_RANGE
                                          : ValidationCode::NON_FINITE_VALUE,
        "base_color_linear", "base color must be finite and in [0, 1]");
  }
  if (!IsFinite(input.metallic)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "metallic", "metallic must be finite");
  }
  if (input.metallic < 0.0F || input.metallic > 1.0F) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "metallic", "metallic must be in [0, 1]");
  }
  if (!IsFinite(input.perceptual_roughness)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE,
                                     "perceptual_roughness",
                                     "roughness must be finite");
  }
  if (input.perceptual_roughness < 0.0F || input.perceptual_roughness > 1.0F) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                                     "perceptual_roughness",
                                     "roughness must be in [0, 1]");
  }

  Vector3d normal;
  Vector3d view;
  Vector3d light;
  if (!Normalize(input.shading_normal, normal)) {
    return ValidationResult::Failure(
        IsFinite(input.shading_normal) ? ValidationCode::VALUE_OUT_OF_RANGE
                                       : ValidationCode::NON_FINITE_VALUE,
        "shading_normal", "normal must be finite and nonzero");
  }
  if (!Normalize(input.view_direction, view)) {
    return ValidationResult::Failure(
        IsFinite(input.view_direction) ? ValidationCode::VALUE_OUT_OF_RANGE
                                       : ValidationCode::NON_FINITE_VALUE,
        "view_direction", "view direction must be finite and nonzero");
  }
  if (!Normalize(input.light_direction, light)) {
    return ValidationResult::Failure(
        IsFinite(input.light_direction) ? ValidationCode::VALUE_OUT_OF_RANGE
                                        : ValidationCode::NON_FINITE_VALUE,
        "light_direction", "light direction must be finite and nonzero");
  }

  PbrDirectReferenceResult candidate;
  candidate.n_dot_l = ClampUnit(Dot(normal, light));
  candidate.n_dot_v = ClampUnit(Dot(normal, view));
  const double perceptual_roughness = input.perceptual_roughness;
  candidate.microfacet_alpha =
      (std::max)(perceptual_roughness * perceptual_roughness,
                 kMinimumMicrofacetAlpha);

  const double metallic = input.metallic;
  const std::array<double, 3U> base{{
      static_cast<double>(input.base_color_linear.x),
      static_cast<double>(input.base_color_linear.y),
      static_cast<double>(input.base_color_linear.z),
  }};
  std::array<double, 3U> f0{};
  std::array<double, 3U> diffuse{};
  for (std::size_t channel = 0U; channel < base.size(); ++channel) {
    f0[channel] = kDielectricF0 + (base[channel] - kDielectricF0) * metallic;
    diffuse[channel] = base[channel] * (1.0 - metallic) * kOgreNextInversePi;
  }
  candidate.specular_f0 = {f0[0U], f0[1U], f0[2U]};

  Vector3d half_way;
  if (!Normalize(view.x + light.x, view.y + light.y, view.z + light.z,
                 half_way)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "view_direction",
        "view and light directions produced an undefined half vector");
  }
  candidate.n_dot_h = ClampUnit(Dot(normal, half_way));
  candidate.v_dot_h = ClampUnit(Dot(view, half_way));

  const double alpha_squared =
      candidate.microfacet_alpha * candidate.microfacet_alpha;
  const double lambda_v =
      candidate.n_dot_l *
      std::sqrt(candidate.n_dot_v * candidate.n_dot_v * (1.0 - alpha_squared) +
                alpha_squared);
  const double lambda_l =
      candidate.n_dot_v *
      std::sqrt(candidate.n_dot_l * candidate.n_dot_l * (1.0 - alpha_squared) +
                alpha_squared);
  const double visibility =
      0.5 / ((lambda_v + lambda_l + 1.0e-6) * kOgreNextPi);
  const double distribution_denominator =
      (candidate.n_dot_h * alpha_squared - candidate.n_dot_h) *
          candidate.n_dot_h +
      1.0;
  const double distribution =
      alpha_squared / (distribution_denominator * distribution_denominator);
  const double distribution_visibility = distribution * visibility;

  const double fresnel_weight = Pow5(1.0 - candidate.v_dot_h);

  const double energy_bias = perceptual_roughness * 0.5;
  const double energy_factor =
      1.0 + ((1.0 / 1.51) - 1.0) * perceptual_roughness;
  const double fd90 = energy_bias + 2.0 * candidate.v_dot_h *
                                        candidate.v_dot_h *
                                        perceptual_roughness;
  const double light_scatter =
      1.0 + (fd90 - 1.0) * Pow5(1.0 - candidate.n_dot_l);
  const double view_scatter =
      1.0 + (fd90 - 1.0) * Pow5(1.0 - candidate.n_dot_v);
  const double diffuse_scale = light_scatter * view_scatter * energy_factor;

  std::array<double, 3U> diffuse_brdf{};
  std::array<double, 3U> specular_brdf{};
  std::array<double, 3U> response{};
  for (std::size_t channel = 0U; channel < base.size(); ++channel) {
    const double fresnel = f0[channel] + fresnel_weight * (1.0 - f0[channel]);
    diffuse_brdf[channel] = diffuse[channel] * diffuse_scale;
    specular_brdf[channel] = fresnel * distribution_visibility;
    response[channel] =
        candidate.n_dot_l * (diffuse_brdf[channel] + specular_brdf[channel]);
  }
  candidate.diffuse_brdf = {diffuse_brdf[0U], diffuse_brdf[1U],
                            diffuse_brdf[2U]};
  candidate.specular_brdf = {specular_brdf[0U], specular_brdf[1U],
                             specular_brdf[2U]};
  candidate.direct_response = {response[0U], response[1U], response[2U]};

  if (!IsFinite(candidate.diffuse_brdf) || !IsFinite(candidate.specular_brdf) ||
      !IsFinite(candidate.direct_response) ||
      !IsFinite(candidate.specular_f0) || !std::isfinite(candidate.n_dot_h) ||
      !std::isfinite(candidate.v_dot_h) ||
      !std::isfinite(candidate.microfacet_alpha)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "evaluation",
        "PBR reference evaluation overflowed its finite output domain");
  }

  output = candidate;
  return ValidationResult::Success();
}

} // namespace RoR::Render
