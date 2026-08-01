/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "PbrReference.h"

#include <algorithm>
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

void TestPinnedIdentityAndGoldenSamples() {
  using namespace RoR::Render;
  Require(kPbrDirectReferenceVersion == 1U,
          "reference schema version changed unexpectedly");
  Require(std::string(kPbrDirectReferenceOgreNextCommit) ==
              "37149a802de747f6806996fa3067b0748ecc1084",
          "reference lost its exact Ogre-Next source identity");

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

  input.light_direction = {0.0F, -1.0F, 0.0F};
  const PbrDirectReferenceResult back_light = Evaluate(input);
  RequireNear(back_light.direct_response, {}, 0.0,
              "back-facing light produced energy");
  RequireNear(back_light.n_dot_l, 0.0, 0.0,
              "back-facing light did not clamp N dot L");

  input.light_direction = {0.0F, 1.0F, 0.0F};
  input.view_direction = {0.0F, -1.0F, 0.0F};
  const PbrDirectReferenceResult back_view = Evaluate(input);
  RequireNear(back_view.direct_response, {}, 0.0,
              "back-facing view produced energy");
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

void TestMalformedInputsAreTransactional() {
  using namespace RoR::Render;
  PbrDirectReferenceResult sentinel;
  sentinel.direct_response = {7.0, 8.0, 9.0};
  sentinel.specular_f0 = {4.0, 5.0, 6.0};
  sentinel.n_dot_l = 3.0;
  const auto unchanged = [&sentinel]() {
    return sentinel.direct_response.x == 7.0 &&
           sentinel.direct_response.y == 8.0 &&
           sentinel.direct_response.z == 9.0 && sentinel.specular_f0.x == 4.0 &&
           sentinel.n_dot_l == 3.0;
  };

  PbrDirectReferenceInput input;
  input.version += 1U;
  Require(!EvaluatePbrDirectReference(input, sentinel),
          "unknown oracle version was accepted");
  Require(unchanged(), "version failure changed output");

  input = {};
  input.base_color_linear.x = -0.01F;
  Require(!EvaluatePbrDirectReference(input, sentinel),
          "negative base color was accepted");
  Require(unchanged(), "base-color failure changed output");

  input = {};
  input.metallic = 1.01F;
  Require(!EvaluatePbrDirectReference(input, sentinel),
          "metallic above one was accepted");
  Require(unchanged(), "metallic failure changed output");

  input = {};
  input.perceptual_roughness = (std::numeric_limits<float>::quiet_NaN)();
  Require(!EvaluatePbrDirectReference(input, sentinel),
          "NaN roughness was accepted");
  Require(unchanged(), "roughness failure changed output");

  input = {};
  input.shading_normal = {};
  Require(!EvaluatePbrDirectReference(input, sentinel),
          "zero normal was accepted");
  Require(unchanged(), "normal failure changed output");

  input = {};
  input.view_direction.x = (std::numeric_limits<float>::infinity)();
  Require(!EvaluatePbrDirectReference(input, sentinel),
          "infinite view direction was accepted");
  Require(unchanged(), "view failure changed output");
}

} // namespace

int main() {
  TestPinnedIdentityAndGoldenSamples();
  TestHemisphereAndRoughnessFloor();
  TestReciprocityAndRotationalInvariance();
  TestMalformedInputsAreTransactional();
  std::cout << "portable pinned Ogre-Next PBR reference tests passed\n";
  return EXIT_SUCCESS;
}
