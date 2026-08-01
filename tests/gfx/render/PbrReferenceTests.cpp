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
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "PBR reference test failed: " << message << '\n';
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
    std::cerr << "PBR reference mismatch: actual=" << actual
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

RoR::Render::PbrDirectReferenceResult
Evaluate(const RoR::Render::PbrDirectReferenceInput &input) {
  RoR::Render::PbrDirectReferenceResult result;
  const RoR::Render::ValidationResult validation =
      RoR::Render::EvaluatePbrDirectReference(input, result);
  Require(validation.ok(), "valid fixture was rejected");
  return result;
}

std::uint64_t NextRandom(std::uint64_t &state) {
  state ^= state >> 12U;
  state ^= state << 25U;
  state ^= state >> 27U;
  return state * UINT64_C(2685821657736338717);
}

double UnitRandom(std::uint64_t &state) {
  return static_cast<double>(NextRandom(state) >> 11U) *
         (1.0 / 9007199254740992.0);
}

RoR::Render::Float3 HemisphereDirection(std::uint64_t &state) {
  constexpr double kTwoPi = 6.283185307179586476925286766559;
  const double cosine = 0.001 + UnitRandom(state) * 0.999;
  const double sine = std::sqrt((std::max)(0.0, 1.0 - cosine * cosine));
  const double azimuth = kTwoPi * UnitRandom(state);
  return {static_cast<float>(sine * std::cos(azimuth)),
          static_cast<float>(cosine),
          static_cast<float>(sine * std::sin(azimuth))};
}

RoR::Render::Float3 RotateAroundY(const RoR::Render::Float3 &value,
                                  double angle) {
  const double cosine = std::cos(angle);
  const double sine = std::sin(angle);
  return {static_cast<float>(cosine * value.x + sine * value.z), value.y,
          static_cast<float>(-sine * value.x + cosine * value.z)};
}

struct SourceVector3 {
  double x;
  double y;
  double z;
};

SourceVector3 SourceNormalize(double x, double y, double z) {
  const double length = std::sqrt(x * x + y * y + z * z);
  Require(length > 0.0 && std::isfinite(length),
          "source-equation fixture direction was degenerate");
  return {x / length, y / length, z / length};
}

SourceVector3 SourceNormalize(const RoR::Render::Float3 &value) {
  return SourceNormalize(value.x, value.y, value.z);
}

double SourceDot(const SourceVector3 &lhs, const SourceVector3 &rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

double SourceSaturate(double value) {
  return (std::max)(0.0, (std::min)(1.0, value));
}

RoR::Render::PbrDirectReferenceResult EvaluatePinnedSourceEquations(
    const RoR::Render::PbrDirectReferenceInput &input) {
  // Independent transcription of pinned full32 PbsBrdf::Default. Source and
  // hashes are locked by ogre-next-pbr-reference.lock.json. This intentionally
  // avoids the production oracle's helpers and algebraic rearrangements.
  constexpr double kPi = 3.141592654;
  const SourceVector3 normal = SourceNormalize(input.shading_normal);
  const SourceVector3 view = SourceNormalize(input.view_direction);
  const SourceVector3 light = SourceNormalize(input.light_direction);
  const SourceVector3 half_way = SourceNormalize(
      light.x + view.x, light.y + view.y, light.z + view.z);

  RoR::Render::PbrDirectReferenceResult result;
  result.n_dot_l = SourceSaturate(SourceDot(normal, light));
  result.n_dot_v = SourceSaturate(SourceDot(normal, view));
  result.n_dot_h = SourceSaturate(SourceDot(normal, half_way));
  result.v_dot_h = SourceSaturate(SourceDot(view, half_way));
  const double perceptual_roughness = input.perceptual_roughness;
  result.microfacet_alpha =
      (std::max)(perceptual_roughness * perceptual_roughness, 0.001);
  const double squared_roughness =
      result.microfacet_alpha * result.microfacet_alpha;

  const double lambda_v =
      result.n_dot_l *
      std::sqrt((-result.n_dot_v * squared_roughness + result.n_dot_v) *
                    result.n_dot_v +
                squared_roughness);
  const double lambda_l =
      result.n_dot_v *
      std::sqrt((-result.n_dot_l * squared_roughness + result.n_dot_l) *
                    result.n_dot_l +
                squared_roughness);
  const double geometry =
      0.5 / ((lambda_v + lambda_l + 1.0e-6) * kPi);
  const double distribution_factor =
      (result.n_dot_h * squared_roughness - result.n_dot_h) * result.n_dot_h +
      1.0;
  const double distribution = squared_roughness /
                              (distribution_factor * distribution_factor);
  const double distribution_geometry = distribution * geometry;

  const double energy_bias = perceptual_roughness * 0.5;
  const double energy_factor =
      (1.0 - perceptual_roughness) +
      perceptual_roughness * (1.0 / 1.51);
  const double fd90 = energy_bias + 2.0 * result.v_dot_h * result.v_dot_h *
                                        perceptual_roughness;
  const double light_scatter =
      1.0 + (fd90 - 1.0) * std::pow(1.0 - result.n_dot_l, 5.0);
  const double view_scatter =
      1.0 + (fd90 - 1.0) * std::pow(1.0 - result.n_dot_v, 5.0);
  const double diffuse_factor = light_scatter * view_scatter * energy_factor;
  const double fresnel_factor = std::pow(1.0 - result.v_dot_h, 5.0);

  const std::array<double, 3U> base{{input.base_color_linear.x,
                                     input.base_color_linear.y,
                                     input.base_color_linear.z}};
  std::array<double, 3U> diffuse{};
  std::array<double, 3U> specular{};
  std::array<double, 3U> response{};
  std::array<double, 3U> f0{};
  for (std::size_t channel = 0U; channel < base.size(); ++channel) {
    f0[channel] = (1.0 - input.metallic) * 0.04 +
                  input.metallic * base[channel];
    const double fresnel =
        (1.0 - fresnel_factor) * f0[channel] + fresnel_factor;
    diffuse[channel] = base[channel] * (1.0 - input.metallic) / kPi *
                       diffuse_factor;
    specular[channel] = fresnel * distribution_geometry;
    response[channel] =
        result.n_dot_l * (diffuse[channel] + specular[channel]);
  }
  result.diffuse_brdf = {diffuse[0U], diffuse[1U], diffuse[2U]};
  result.specular_brdf = {specular[0U], specular[1U], specular[2U]};
  result.direct_response = {response[0U], response[1U], response[2U]};
  result.specular_f0 = {f0[0U], f0[1U], f0[2U]};
  return result;
}

void RequireResultNear(const RoR::Render::PbrDirectReferenceResult &actual,
                       const RoR::Render::PbrDirectReferenceResult &expected,
                       double tolerance, const char *message) {
  RequireNear(actual.diffuse_brdf, expected.diffuse_brdf, tolerance, message);
  RequireNear(actual.specular_brdf, expected.specular_brdf, tolerance, message);
  RequireNear(actual.direct_response, expected.direct_response, tolerance,
              message);
  RequireNear(actual.specular_f0, expected.specular_f0, tolerance, message);
  RequireNear(actual.n_dot_l, expected.n_dot_l, tolerance, message);
  RequireNear(actual.n_dot_v, expected.n_dot_v, tolerance, message);
  RequireNear(actual.n_dot_h, expected.n_dot_h, tolerance, message);
  RequireNear(actual.v_dot_h, expected.v_dot_h, tolerance, message);
  RequireNear(actual.microfacet_alpha, expected.microfacet_alpha, tolerance,
              message);
}

void TestPinnedIdentityAndGoldenSamples() {
  using namespace RoR::Render;
  Require(kPbrDirectReferenceVersion == 1U,
          "reference schema version changed unexpectedly");
  Require(std::string(kPbrDirectReferenceOgreNextCommit).size() == 40U,
          "reference lost its Ogre-Next commit identity");
  Require(std::string(kPbrDirectReferenceBrdfSourceSha256).size() == 64U &&
              std::string(kPbrDirectReferencePixelSourceSha256).size() == 64U &&
              std::string(kPbrDirectReferenceDatablockSourceSha256).size() ==
                  64U,
          "reference lost its pinned source hashes");
  RequireNear(kPbrDirectReferenceBackendRelativeTolerance, 0.01, 0.0,
              "backend comparison tolerance drifted");

  PbrDirectReferenceInput dielectric;
  dielectric.base_color_linear = {0.8F, 0.2F, 0.1F};
  dielectric.perceptual_roughness = 0.5F;
  const PbrDirectReferenceResult first = Evaluate(dielectric);
  RequireNear(first.diffuse_brdf,
              {0.21164445409721652, 0.05291111352430413, 0.026455556762152065},
              2.0e-7, "normal-incidence dielectric diffuse golden drifted");
  RequireNear(first.specular_brdf,
              {0.05092955631797832, 0.05092955631797832, 0.05092955631797832},
              2.0e-7, "normal-incidence dielectric specular golden drifted");
  RequireNear(first.direct_response,
              {0.26257401041519485, 0.10384066984228245, 0.07738511308013038},
              2.0e-7, "normal-incidence dielectric response drifted");
  RequireNear(first.specular_f0, {0.04, 0.04, 0.04}, 1.0e-14,
              "dielectric F0 drifted");
  RequireNear(first.microfacet_alpha, 0.25, 0.0,
              "perceptual roughness was not squared");

  PbrDirectReferenceInput metal;
  metal.base_color_linear = {0.9F, 0.7F, 0.2F};
  metal.metallic = 1.0F;
  metal.perceptual_roughness = 0.2F;
  const PbrDirectReferenceResult second = Evaluate(metal);
  RequireNear(second.diffuse_brdf, {}, 0.0,
              "fully metallic material retained diffuse energy");
  RequireNear(second.specular_f0, {0.9, 0.7, 0.2}, 3.0e-8,
              "metallic F0 did not inherit linear base color");
  RequireNear(second.direct_response,
              {44.76230535759559, 34.81512638924101, 9.947178968354576}, 3.0e-6,
              "smooth colored-metal golden drifted");

  PbrDirectReferenceInput oblique;
  oblique.base_color_linear = {0.4F, 0.5F, 0.8F};
  oblique.metallic = 0.35F;
  oblique.perceptual_roughness = 0.85F;
  oblique.view_direction = {0.6F, 0.8F, 0.0F};
  oblique.light_direction = {-0.3F, 0.9539392F, 0.0F};
  const PbrDirectReferenceResult third = Evaluate(oblique);
  RequireNear(third.direct_response,
              {0.08408494005512528, 0.10401753313121476, 0.16381531235948313},
              3.0e-6, "oblique mixed-material golden drifted");
  RequireNear(third.n_dot_l, 0.9539392014154205, 3.0e-8,
              "oblique N dot L drifted");
  RequireNear(third.n_dot_v, 0.8, 3.0e-8, "oblique N dot V drifted");
}

void TestHemisphereAndRoughnessFloor() {
  using namespace RoR::Render;
  PbrDirectReferenceInput input;
  input.perceptual_roughness = 0.0F;
  const PbrDirectReferenceResult smooth = Evaluate(input);
  RequireNear(smooth.microfacet_alpha, 0.001, 0.0,
              "Ogre-Next microfacet alpha floor drifted");

  input.perceptual_roughness = 0.5F;
  input.light_direction = {0.6F, -0.8F, 0.0F};
  const PbrDirectReferenceResult back_light = Evaluate(input);
  RequireNear(back_light.direct_response, {}, 0.0,
              "back-facing light produced energy");
  RequireNear(back_light.n_dot_l, 0.0, 0.0,
              "back-facing light did not clamp N dot L");
  RequireNear(back_light.specular_f0, {0.04, 0.04, 0.04}, 1.0e-14,
              "black light response lost material F0");
  Require(back_light.diffuse_brdf.x > 0.0 && back_light.specular_brdf.x > 0.0,
          "defined back-light half vector did not evaluate source lobes");
}

void TestIndependentSourceEquationBoundaries() {
  using namespace RoR::Render;
  struct Fixture {
    const char *name;
    Float3 view;
    Float3 light;
    bool positive_response;
  };
  const std::array<Fixture, 6U> fixtures{{
      {"normal", {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, true},
      {"grazing view", {1.0F, 0.001F, 0.0F},
       {-0.3F, 0.9539392F, 0.0F}, true},
      {"tangent view", {1.0F, 0.0F, 0.0F},
       {0.0F, 1.0F, 0.0F}, true},
      {"back-facing view", {0.6F, -0.8F, 0.0F},
       {0.0F, 1.0F, 0.0F}, true},
      {"tangent light", {0.0F, 1.0F, 0.0F},
       {1.0F, 0.0F, 0.0F}, false},
      {"back-facing light", {0.0F, 1.0F, 0.0F},
       {0.6F, -0.8F, 0.0F}, false},
  }};

  for (const Fixture &fixture : fixtures) {
    PbrDirectReferenceInput input;
    input.base_color_linear = {0.73F, 0.19F, 0.41F};
    input.metallic = 0.37F;
    input.perceptual_roughness = 0.63F;
    input.view_direction = fixture.view;
    input.light_direction = fixture.light;
    const PbrDirectReferenceResult actual = Evaluate(input);
    const PbrDirectReferenceResult source =
        EvaluatePinnedSourceEquations(input);
    RequireResultNear(actual, source, 2.0e-12, fixture.name);
    if (fixture.positive_response) {
      Require(actual.direct_response.x > 0.0,
              "view-hemisphere fixture was incorrectly forced black");
    } else {
      RequireNear(actual.direct_response, {}, 0.0,
                  "N dot L zero did not force exact black response");
    }
  }
}

void TestReciprocityAndRotationalInvariance() {
  using namespace RoR::Render;
  std::uint64_t random = UINT64_C(0x932b53c44f1a6e21);
  for (std::uint32_t sample = 0U; sample < 20000U; ++sample) {
    PbrDirectReferenceInput input;
    input.base_color_linear = {
        static_cast<float>(UnitRandom(random)),
        static_cast<float>(UnitRandom(random)),
        static_cast<float>(UnitRandom(random)),
    };
    input.metallic = static_cast<float>(UnitRandom(random));
    input.perceptual_roughness = static_cast<float>(UnitRandom(random));
    input.view_direction = HemisphereDirection(random);
    input.light_direction = HemisphereDirection(random);
    const PbrDirectReferenceResult original = Evaluate(input);

    Require(original.diffuse_brdf.x >= 0.0 && original.diffuse_brdf.y >= 0.0 &&
                original.diffuse_brdf.z >= 0.0 &&
                original.specular_brdf.x >= 0.0 &&
                original.specular_brdf.y >= 0.0 &&
                original.specular_brdf.z >= 0.0 &&
                original.direct_response.x >= 0.0 &&
                original.direct_response.y >= 0.0 &&
                original.direct_response.z >= 0.0,
            "fixed-seed BRDF produced negative energy");

    PbrDirectReferenceInput reciprocal = input;
    reciprocal.view_direction = input.light_direction;
    reciprocal.light_direction = input.view_direction;
    const PbrDirectReferenceResult swapped = Evaluate(reciprocal);
    RequireNear(original.diffuse_brdf, swapped.diffuse_brdf, 3.0e-11,
                "Disney diffuse violated Helmholtz reciprocity");
    RequireNear(original.specular_brdf, swapped.specular_brdf, 3.0e-11,
                "GGX specular violated Helmholtz reciprocity");

    const double rotation = UnitRandom(random) * 6.283185307179586;
    PbrDirectReferenceInput rotated = input;
    rotated.view_direction = RotateAroundY(input.view_direction, rotation);
    rotated.light_direction = RotateAroundY(input.light_direction, rotation);
    const PbrDirectReferenceResult spun = Evaluate(rotated);
    RequireNear(original.diffuse_brdf, spun.diffuse_brdf, 2.0e-5,
                "isotropic diffuse changed under normal-axis rotation");
    RequireNear(original.specular_brdf, spun.specular_brdf, 2.0e-5,
                "isotropic GGX changed under normal-axis rotation");
  }
}

bool ExactlyEqual(const RoR::Render::PbrDirectReferenceResult &lhs,
                  const RoR::Render::PbrDirectReferenceResult &rhs) {
  return lhs.diffuse_brdf == rhs.diffuse_brdf &&
         lhs.specular_brdf == rhs.specular_brdf &&
         lhs.direct_response == rhs.direct_response &&
         lhs.specular_f0 == rhs.specular_f0 && lhs.n_dot_l == rhs.n_dot_l &&
         lhs.n_dot_v == rhs.n_dot_v && lhs.n_dot_h == rhs.n_dot_h &&
         lhs.v_dot_h == rhs.v_dot_h &&
         lhs.microfacet_alpha == rhs.microfacet_alpha;
}

RoR::Render::PbrDirectReferenceResult SentinelResult() {
  using namespace RoR::Render;
  PbrDirectReferenceResult sentinel;
  sentinel.diffuse_brdf = {1.0, 2.0, 3.0};
  sentinel.specular_brdf = {4.0, 5.0, 6.0};
  sentinel.direct_response = {7.0, 8.0, 9.0};
  sentinel.specular_f0 = {10.0, 11.0, 12.0};
  sentinel.n_dot_l = 13.0;
  sentinel.n_dot_v = 14.0;
  sentinel.n_dot_h = 15.0;
  sentinel.v_dot_h = 16.0;
  sentinel.microfacet_alpha = 17.0;
  return sentinel;
}

void SetComponent(RoR::Render::Float3 &value, std::size_t component,
                  float replacement) {
  if (component == 0U) {
    value.x = replacement;
  } else if (component == 1U) {
    value.y = replacement;
  } else {
    value.z = replacement;
  }
}

void ExpectRejectedTransactional(
    const RoR::Render::PbrDirectReferenceInput &input,
    RoR::Render::ValidationCode expected_code, const char *expected_field) {
  using namespace RoR::Render;
  const PbrDirectReferenceResult sentinel = SentinelResult();
  PbrDirectReferenceResult output = sentinel;
  const ValidationResult validation = EvaluatePbrDirectReference(input, output);
  Require(!validation, "malformed PBR input was accepted");
  Require(validation.code == expected_code,
          "malformed PBR input returned the wrong validation code");
  Require(validation.field == expected_field,
          "malformed PBR input returned the wrong field");
  Require(ExactlyEqual(output, sentinel),
          "malformed PBR input changed output transactionally");
}

void TestMalformedInputsAreTransactional() {
  using namespace RoR::Render;

  PbrDirectReferenceInput input;
  input.version = 0U;
  ExpectRejectedTransactional(input, ValidationCode::UNSUPPORTED_VERSION,
                              "version");
  input.version = kPbrDirectReferenceVersion + 1U;
  ExpectRejectedTransactional(input, ValidationCode::UNSUPPORTED_VERSION,
                              "version");

  struct InvalidScalar {
    float value;
    ValidationCode code;
  };
  const float infinity = (std::numeric_limits<float>::infinity)();
  const float nan = (std::numeric_limits<float>::quiet_NaN)();
  const std::array<InvalidScalar, 5U> normalized_values{{
      {-0.01F, ValidationCode::VALUE_OUT_OF_RANGE},
      {1.01F, ValidationCode::VALUE_OUT_OF_RANGE},
      {nan, ValidationCode::NON_FINITE_VALUE},
      {infinity, ValidationCode::NON_FINITE_VALUE},
      {-infinity, ValidationCode::NON_FINITE_VALUE},
  }};

  for (std::size_t component = 0U; component < 3U; ++component) {
    for (const InvalidScalar invalid : normalized_values) {
      input = {};
      SetComponent(input.base_color_linear, component, invalid.value);
      ExpectRejectedTransactional(input, invalid.code, "base_color_linear");
    }
  }

  for (const InvalidScalar invalid : normalized_values) {
    input = {};
    input.metallic = invalid.value;
    ExpectRejectedTransactional(input, invalid.code, "metallic");

    input = {};
    input.perceptual_roughness = invalid.value;
    ExpectRejectedTransactional(input, invalid.code,
                                "perceptual_roughness");
  }

  struct DirectionField {
    Float3 PbrDirectReferenceInput::*member;
    const char *name;
  };
  const std::array<DirectionField, 3U> directions{{
      {&PbrDirectReferenceInput::shading_normal, "shading_normal"},
      {&PbrDirectReferenceInput::view_direction, "view_direction"},
      {&PbrDirectReferenceInput::light_direction, "light_direction"},
  }};
  const std::array<float, 3U> nonfinite{{nan, infinity, -infinity}};
  for (const DirectionField &direction : directions) {
    input = {};
    input.*(direction.member) = {};
    ExpectRejectedTransactional(input, ValidationCode::VALUE_OUT_OF_RANGE,
                                direction.name);

    for (std::size_t component = 0U; component < 3U; ++component) {
      for (const float invalid : nonfinite) {
        input = {};
        SetComponent(input.*(direction.member), component, invalid);
        ExpectRejectedTransactional(input, ValidationCode::NON_FINITE_VALUE,
                                    direction.name);
      }
    }
  }

  input = {};
  input.view_direction = {0.0F, -1.0F, 0.0F};
  input.light_direction = {0.0F, 1.0F, 0.0F};
  ExpectRejectedTransactional(input, ValidationCode::VALUE_OUT_OF_RANGE,
                              "view_direction");
}

void TestDirectionNormalizationExtremes() {
  using namespace RoR::Render;
  const std::array<float, 3U> scales{{
      (std::numeric_limits<float>::denorm_min)(),
      (std::numeric_limits<float>::min)(),
      (std::numeric_limits<float>::max)(),
  }};
  for (const float scale : scales) {
    PbrDirectReferenceInput input;
    input.shading_normal = {scale, 0.0F, 0.0F};
    const PbrDirectReferenceResult normal = Evaluate(input);
    Require(IsFinite(normal.direct_response),
            "extreme normal scale produced non-finite output");

    input = {};
    input.view_direction = {scale, 0.0F, 0.0F};
    const PbrDirectReferenceResult view = Evaluate(input);
    Require(IsFinite(view.direct_response),
            "extreme view scale produced non-finite output");

    input = {};
    input.light_direction = {scale, 0.0F, 0.0F};
    const PbrDirectReferenceResult light = Evaluate(input);
    Require(IsFinite(light.direct_response),
            "extreme light scale produced non-finite output");
  }
}

} // namespace

int main() {
  TestPinnedIdentityAndGoldenSamples();
  TestHemisphereAndRoughnessFloor();
  TestIndependentSourceEquationBoundaries();
  TestReciprocityAndRotationalInvariance();
  TestMalformedInputsAreTransactional();
  TestDirectionNormalizationExtremes();
  std::cout << "portable pinned Ogre-Next PBR reference tests passed\n";
  return EXIT_SUCCESS;
}
