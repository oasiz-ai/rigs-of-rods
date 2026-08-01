/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ParallaxProbeReference.h"

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
    std::cerr << "parallax probe reference test failed: " << message << '\n';
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
    std::cerr << "parallax probe mismatch: actual=" << actual
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

RoR::Render::ParallaxProbeReferenceResult
Evaluate(const RoR::Render::ParallaxProbeReferenceInput &input) {
  RoR::Render::ParallaxProbeReferenceResult result;
  Require(RoR::Render::EvaluateParallaxProbeReference(input, result).ok(),
          "valid parallax-probe fixture was rejected");
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

RoR::Render::Float3 UnitDirection(std::uint64_t &state) {
  constexpr double kTwoPi = 6.283185307179586476925286766559;
  const double z = UnitRandom(state) * 2.0 - 1.0;
  const double radius = std::sqrt((std::max)(0.0, 1.0 - z * z));
  const double azimuth = kTwoPi * UnitRandom(state);
  return {static_cast<float>(radius * std::cos(azimuth)),
          static_cast<float>(radius * std::sin(azimuth)),
          static_cast<float>(z)};
}

double ShaderEquationExitDistance(
    const RoR::Render::ParallaxProbeReferenceInput &input) {
  const double position[] = {input.position_local.x, input.position_local.y,
                             input.position_local.z};
  const double direction[] = {input.reflection_direction_local.x,
                              input.reflection_direction_local.y,
                              input.reflection_direction_local.z};
  const double half_size[] = {input.probe_half_size.x, input.probe_half_size.y,
                              input.probe_half_size.z};
  double distance = (std::numeric_limits<double>::infinity)();
  for (std::size_t axis = 0U; axis < 3U; ++axis) {
    if (direction[axis] == 0.0) {
      continue;
    }
    const double at_min = (-half_size[axis] - position[axis]) / direction[axis];
    const double at_max = (half_size[axis] - position[axis]) / direction[axis];
    distance = (std::min)(distance, (std::max)(at_min, at_max));
  }
  return distance;
}

void TestPinnedIdentityAndAxisGolden() {
  using namespace RoR::Render;
  Require(kParallaxProbeReferenceVersion == 1U,
          "probe reference version changed unexpectedly");
  Require(std::string(kParallaxProbeReferenceOgreNextCommit) ==
              "37149a802de747f6806996fa3067b0748ecc1084",
          "probe reference lost its exact Ogre-Next identity");
  Require(std::string(kParallaxProbeReferenceShaderPath) ==
              "Samples/Media/Hlms/Common/Any/Cubemap_piece_all.any",
          "common cubemap shader path drifted");
  Require(
      std::string(kParallaxProbeReferenceShaderSha256) ==
          "ed281b8599716c769f1d99f14fb42568a586e018b1e9fac9ee03944cbd1d7fbb",
      "common cubemap shader identity drifted");
  Require(
      std::string(kParallaxProbeReferenceManualWeightShaderPath) ==
          "Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any" &&
          std::string(kParallaxProbeReferenceManualWeightShaderSha256) ==
              "12cebd71e877c1d265df8d68f6c3f2931127679a8e77bb01c92b1221a09f5a7f",
      "manual probe-weight shader identity drifted");
  Require(
      std::string(kParallaxProbeReferenceAutomaticWeightShaderPath) ==
          "Samples/Media/Hlms/Pbs/Any/"
          "ForwardPlus_DecalsCubemaps_piece_ps.any" &&
          std::string(kParallaxProbeReferenceAutomaticWeightShaderSha256) ==
              "64d53a29393192598e0111d1a729eb031eb7234273fef5753e2ccc9a121b5ada",
      "automatic probe-weight shader identity drifted");
  Require(std::string(kParallaxProbeReferenceBufferSourcePath) ==
              "Components/Hlms/Pbs/src/Cubemaps/"
              "OgreParallaxCorrectedCubemapBase.cpp",
          "probe buffer source path drifted");
  Require(
      std::string(kParallaxProbeReferenceBufferSourceSha256) ==
          "e4832fb5afbc466fc1d07a8b08e11a72a84cd38eced229ce70e4cf625a898ea6",
      "probe buffer source identity drifted");
  Require(std::string(kParallaxProbeReferenceProbeSourcePath) ==
              "Components/Hlms/Pbs/src/Cubemaps/OgreCubemapProbe.cpp",
          "cubemap probe source path drifted");
  Require(
      std::string(kParallaxProbeReferenceProbeSourceSha256) ==
          "9c7a7dd6560fd11654a7751978c44752a8845ffb83966c57e2320cc0e46cf8ee",
      "cubemap probe source identity drifted");

  ParallaxProbeReferenceInput input;
  input.probe_half_size = {2.0F, 3.0F, 4.0F};
  input.area_outer_range = input.probe_half_size;
  input.reflection_direction_local = {1.0F, 0.0F, 0.0F};
  const ParallaxProbeReferenceResult result = Evaluate(input);
  Require(result.sample_active, "box center was not active");
  RequireNear(result.raw_box_fade, 1.0, 0.0, "center fade drifted");
  RequireNear(result.manual_probe_weight, 1.0, 0.0,
              "center manual weight drifted");
  RequireNear(result.automatic_ndf, 0.0, 0.0, "center NDF drifted");
  RequireNear(result.automatic_probe_weight, 1.0, 0.0,
              "center automatic weight drifted");
  RequireNear(result.intersection_distance, 2.0, 0.0,
              "axis-aligned exit distance drifted");
  RequireNear(result.intersection_local, {2.0, 0.0, 0.0}, 0.0,
              "axis-aligned intersection drifted");
  RequireNear(result.corrected_direction_left_handed, {2.0, 0.0, 0.0}, 0.0,
              "axis-aligned corrected direction drifted");
}

void TestOffsetGoldenAndBoundaryPolicy() {
  using namespace RoR::Render;
  ParallaxProbeReferenceInput input;
  input.position_local = {1.0F, 0.0F, 0.0F};
  input.reflection_direction_local = {0.7071067690849304F, 0.0F,
                                      0.7071067690849304F};
  input.probe_half_size = {2.0F, 3.0F, 4.0F};
  input.cubemap_position_local = {0.5F, 0.0F, -1.0F};
  input.area_inner_range = {0.25F, 0.5F, 0.75F};
  input.area_outer_range = input.probe_half_size;
  input.priority = 4U;
  const ParallaxProbeReferenceResult result = Evaluate(input);
  RequireNear(result.raw_box_fade, 0.5, 0.0, "offset fade drifted");
  RequireNear(result.intersection_distance, 1.4142135865763297, 3.0e-8,
              "diagonal exit distance drifted");
  RequireNear(result.intersection_local, {2.0, 0.0, 1.0}, 3.0e-8,
              "diagonal intersection drifted");
  RequireNear(result.corrected_direction_left_handed, {1.5, 0.0, -2.0}, 3.0e-8,
              "left-handed corrected vector drifted");
  RequireNear(result.automatic_ndf, 0.42857118367360936, 3.0e-8,
              "automatic probe NDF drifted");
  RequireNear(result.automatic_probe_weight, 0.4264896940561002, 3.0e-8,
              "fourth-power priority weight drifted");

  input.position_local = {2.0F, 0.0F, 0.0F};
  const ParallaxProbeReferenceResult boundary = Evaluate(input);
  Require(!boundary.sample_active, "box boundary incorrectly sampled a probe");
  RequireNear(boundary.raw_box_fade, 0.0, 0.0,
              "boundary fade was not exact zero");
  RequireNear(boundary.manual_probe_weight, 0.0, 0.0,
              "inactive boundary retained manual weight");
  RequireNear(boundary.automatic_probe_weight, 0.0, 0.0,
              "inactive boundary retained automatic weight");

  input.position_local = {2.1F, 0.0F, 0.0F};
  const ParallaxProbeReferenceResult outside = Evaluate(input);
  Require(!outside.sample_active, "outside position sampled a probe");
  Require(outside.raw_box_fade < 0.0,
          "outside position did not preserve negative raw fade");
}

void TestFixedSeedIntersectionAndWeights() {
  using namespace RoR::Render;
  std::uint64_t random = UINT64_C(0x6f67726570636331);
  for (std::uint32_t sample = 0U; sample < 20000U; ++sample) {
    ParallaxProbeReferenceInput input;
    input.probe_half_size = {
        static_cast<float>(0.5 + UnitRandom(random) * 20.0),
        static_cast<float>(0.5 + UnitRandom(random) * 20.0),
        static_cast<float>(0.5 + UnitRandom(random) * 20.0),
    };
    input.position_local = {
        static_cast<float>((UnitRandom(random) * 1.8 - 0.9) *
                           input.probe_half_size.x),
        static_cast<float>((UnitRandom(random) * 1.8 - 0.9) *
                           input.probe_half_size.y),
        static_cast<float>((UnitRandom(random) * 1.8 - 0.9) *
                           input.probe_half_size.z),
    };
    input.reflection_direction_local = UnitDirection(random);
    input.area_inner_range = {
        input.probe_half_size.x * 0.25F,
        input.probe_half_size.y * 0.25F,
        input.probe_half_size.z * 0.25F,
    };
    input.area_outer_range = input.probe_half_size;
    input.priority = static_cast<std::uint16_t>(1U + NextRandom(random) % 64U);
    const ParallaxProbeReferenceResult result = Evaluate(input);
    Require(result.sample_active, "strict interior sample became inactive");
    Require(result.intersection_distance > 0.0,
            "interior ray had no positive exit");
    RequireNear(result.intersection_distance, ShaderEquationExitDistance(input),
                3.0e-12,
                "stable ray-box result diverged from pinned shader equation");
    Require(result.raw_box_fade > 0.0 && result.raw_box_fade <= 1.0,
            "interior fade escaped (0, 1]");
    Require(result.manual_probe_weight > 0.0 &&
                result.manual_probe_weight <= 1.0,
            "manual probe weight escaped (0, 1]");
    Require(result.automatic_probe_weight >= 0.0 &&
                result.automatic_probe_weight <= input.priority,
            "automatic probe weight escaped its priority bound");

    const double normalized_x =
        std::fabs(result.intersection_local.x) / input.probe_half_size.x;
    const double normalized_y =
        std::fabs(result.intersection_local.y) / input.probe_half_size.y;
    const double normalized_z =
        std::fabs(result.intersection_local.z) / input.probe_half_size.z;
    RequireNear((std::max)({normalized_x, normalized_y, normalized_z}), 1.0,
                3.0e-6, "ray intersection did not land on the box");

    const Double3 expected_corrected{
        result.intersection_local.x - input.cubemap_position_local.x,
        result.intersection_local.y - input.cubemap_position_local.y,
        -(result.intersection_local.z - input.cubemap_position_local.z),
    };
    RequireNear(result.corrected_direction_left_handed, expected_corrected,
                3.0e-12, "corrected vector diverged from pinned shader math");
  }
}

void TestMalformedInputsAreTransactional() {
  using namespace RoR::Render;
  ParallaxProbeReferenceResult sentinel;
  sentinel.intersection_distance = 7.0;
  sentinel.corrected_direction_left_handed = {8.0, 9.0, 10.0};
  const auto unchanged = [&sentinel]() {
    return sentinel.intersection_distance == 7.0 &&
           sentinel.corrected_direction_left_handed == Double3{8.0, 9.0, 10.0};
  };

  ParallaxProbeReferenceInput input;
  input.version += 1U;
  Require(!EvaluateParallaxProbeReference(input, sentinel),
          "unknown probe version was accepted");
  Require(unchanged(), "version failure changed output");

  input = {};
  input.reflection_direction_local = {};
  Require(!EvaluateParallaxProbeReference(input, sentinel),
          "zero reflection direction was accepted");
  Require(unchanged(), "direction failure changed output");

  input = {};
  input.reflection_direction_local = {2.0F, 0.0F, 0.0F};
  Require(!EvaluateParallaxProbeReference(input, sentinel),
          "non-unit reflection direction was accepted");
  Require(unchanged(), "unit-length failure changed output");

  input = {};
  input.probe_half_size.x = 0.0F;
  Require(!EvaluateParallaxProbeReference(input, sentinel),
          "zero probe half size was accepted");
  Require(unchanged(), "half-size failure changed output");

  input = {};
  input.area_inner_range.x = 2.0F;
  Require(!EvaluateParallaxProbeReference(input, sentinel),
          "inner range above outer range was accepted");
  Require(unchanged(), "range failure changed output");

  input = {};
  input.priority = 0U;
  Require(!EvaluateParallaxProbeReference(input, sentinel),
          "zero automatic priority was accepted");
  Require(unchanged(), "priority failure changed output");

  input = {};
  input.position_local.x = (std::numeric_limits<float>::quiet_NaN)();
  Require(!EvaluateParallaxProbeReference(input, sentinel),
          "NaN position was accepted");
  Require(unchanged(), "non-finite failure changed output");

  input = {};
  input.reflection_direction_local = {1.0F, 0.0F, 0.0F};
  input.cubemap_position_local = {1.0F, 0.0F, 0.0F};
  Require(!EvaluateParallaxProbeReference(input, sentinel),
          "zero corrected sampling vector was accepted");
  Require(unchanged(), "undefined-sample failure changed output");
}

} // namespace

int main() {
  TestPinnedIdentityAndAxisGolden();
  TestOffsetGoldenAndBoundaryPolicy();
  TestFixedSeedIntersectionAndWeights();
  TestMalformedInputsAreTransactional();
  std::cout << "portable pinned Ogre-Next parallax-probe tests passed\n";
  return EXIT_SUCCESS;
}
