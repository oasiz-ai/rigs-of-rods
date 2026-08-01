/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "HdrReference.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

RoR::Render::HdrAutoExposureReferenceResult
EvaluateExposure(const RoR::Render::HdrAutoExposureReferenceInput &input) {
  RoR::Render::HdrAutoExposureReferenceResult result;
  Require(RoR::Render::EvaluateHdrAutoExposureReference(input, result).ok(),
          "valid exposure fixture was rejected");
  return result;
}

RoR::Render::HdrFinalToneMapReferenceResult
EvaluateToneMap(const RoR::Render::HdrFinalToneMapReferenceInput &input) {
  RoR::Render::HdrFinalToneMapReferenceResult result;
  Require(RoR::Render::EvaluateHdrFinalToneMapReference(input, result).ok(),
          "valid tone-map fixture was rejected");
  return result;
}

void TestPinnedIdentityAndExposureGolden() {
  using namespace RoR::Render;
  Require(kHdrReferenceVersion == 1U,
          "HDR reference schema version changed unexpectedly");
  Require(std::string(kHdrReferenceOgreNextCommit) ==
              "37149a802de747f6806996fa3067b0748ecc1084",
          "HDR reference lost its exact Ogre-Next source identity");
  Require(
      std::string(kHdrReferenceMetalShaderSha256) ==
          "c5646e0b52ddfff8da39b2cd81fc874d63ce1dfb72dcf325fe9e31cee366af40",
      "Metal HDR shader identity drifted");
  Require(
      std::string(kHdrReferenceHlslShaderSha256) ==
          "6f8bdaee587565fdba06525ead91b1e6a2e8b86f8a1270d35850d6028c19a119",
      "HLSL HDR shader identity drifted");
  Require(
      std::string(kHdrReferenceGlslShaderSha256) ==
          "9ecc4946a5cc046c11eca50543af8a6d5bb4745475c6c5c166c25e76c6cbaedb",
      "GLSL HDR shader identity drifted");
  Require(
      std::string(kHdrReferenceUtilitySha256) ==
          "0c58ca0fe592b949662b8085b3aebb3949d4fa3c9f071f9a55f12836f3dadb13",
      "Ogre HDR utility identity drifted");

  HdrAutoExposureReferenceInput input;
  input.average_log_luminance = 6.25;
  input.previous_inverse_luminance = 0.75;
  const HdrAutoExposureReferenceResult result = EvaluateExposure(input);
  RequireNear(result.exposure_numerator, 138.5833300342914, 2.0e-14,
              "exposure numerator drifted");
  RequireNear(result.minimum_log_luminance, 5.0, 0.0,
              "minimum log luminance drifted");
  RequireNear(result.maximum_log_luminance, 8.5, 0.0,
              "maximum log luminance drifted");
  RequireNear(result.clamped_log_luminance, 6.25, 0.0,
              "log luminance clamp drifted");
  RequireNear(result.target_inverse_luminance, 0.2675287626769076, 2.0e-14,
              "target inverse luminance drifted");
  RequireNear(result.previous_frame_weight, 0.9715319411536059, 2.0e-14,
              "48 FPS adaptation weight drifted");
  RequireNear(result.adapted_inverse_luminance, 0.7362649804241936, 2.0e-14,
              "adapted inverse luminance drifted");
}

void TestExposureClampAndTemporalEndpoints() {
  using namespace RoR::Render;
  HdrAutoExposureReferenceInput input;
  input.average_log_luminance = -100.0;
  input.delta_seconds = 0.0;
  const HdrAutoExposureReferenceResult frozen = EvaluateExposure(input);
  RequireNear(frozen.clamped_log_luminance, 5.0, 0.0,
              "dark luminance did not clamp at the Ogre bound");
  RequireNear(frozen.previous_frame_weight, 1.0, 0.0,
              "zero delta did not preserve the previous exposure");
  RequireNear(frozen.adapted_inverse_luminance,
              input.previous_inverse_luminance, 0.0,
              "zero delta changed adapted exposure");

  input.average_log_luminance = 100.0;
  input.delta_seconds = 1.0;
  const HdrAutoExposureReferenceResult one_second = EvaluateExposure(input);
  RequireNear(one_second.clamped_log_luminance, 8.5, 0.0,
              "bright luminance did not clamp at the Ogre bound");
  RequireNear(one_second.previous_frame_weight, 0.25, 0.0,
              "adaptation is no longer 75 percent per second");
  RequireNear(one_second.adapted_inverse_luminance,
              one_second.target_inverse_luminance * 0.75 + 0.25, 2.0e-14,
              "one-second adaptation blend drifted");
}

void TestToneMapGoldenAndChannelIndependence() {
  using namespace RoR::Render;
  HdrFinalToneMapReferenceInput input;
  input.scene_linear_hdr = {1.0F, 2.0F, 4.0F};
  input.bloom_srgb = {0.1F, 0.25F, 0.5F};
  input.inverse_luminance = 0.75;
  input.alpha = 0.4;
  const HdrFinalToneMapReferenceResult result = EvaluateToneMap(input);
  RequireNear(result.exposed_scene_linear, {0.75, 1.5, 3.0}, 0.0,
              "scene exposure multiplication drifted");
  RequireNear(result.bloom_linear_approximation,
              {0.010000000298023226, 0.0625, 0.25}, 2.0e-14,
              "historical bloom x*x conversion drifted");
  RequireNear(result.combined_linear, {0.9100000047683716, 2.5, 7.0}, 2.0e-14,
              "scene and bloom combination drifted");
  RequireNear(result.filmic_normalized,
              {0.43487346167472785, 0.72777206709626485, 0.94099459865407786},
              2.0e-14, "Hable filmic response drifted");
  RequireNear(result.shader_output,
              {0.52859182709340979, 0.89471508387033105, 1.1612432483175974},
              2.0e-14, "final contrast and lift drifted");
  RequireNear(result.alpha, 0.4, 0.0, "tone map did not preserve alpha");

  HdrFinalToneMapReferenceInput red;
  red.scene_linear_hdr = {2.0F, 0.0F, 0.0F};
  const HdrFinalToneMapReferenceResult only_red = EvaluateToneMap(red);
  RequireNear(only_red.shader_output.y, -0.015, 2.0e-14,
              "green channel leaked energy");
  RequireNear(only_red.shader_output.z, -0.015, 2.0e-14,
              "blue channel leaked energy");
}

void TestMonotonicFiniteRange() {
  using namespace RoR::Render;
  double previous = -1.0;
  for (std::uint32_t sample = 0U; sample <= 10000U; ++sample) {
    HdrFinalToneMapReferenceInput input;
    input.scene_linear_hdr.x = static_cast<float>(sample) * 0.01F;
    input.inverse_luminance = 1.75;
    const HdrFinalToneMapReferenceResult result = EvaluateToneMap(input);
    Require(result.shader_output.x >= previous,
            "filmic curve was not monotonic over the fixed sweep");
    Require(std::isfinite(result.shader_output.x),
            "filmic sweep produced a non-finite value");
    previous = result.shader_output.x;
  }
}

void TestMalformedInputsAreTransactional() {
  using namespace RoR::Render;
  HdrAutoExposureReferenceResult exposure_sentinel;
  exposure_sentinel.adapted_inverse_luminance = 7.0;
  const auto exposure_unchanged = [&exposure_sentinel]() {
    return exposure_sentinel.adapted_inverse_luminance == 7.0;
  };

  HdrAutoExposureReferenceInput exposure;
  exposure.version += 1U;
  Require(!EvaluateHdrAutoExposureReference(exposure, exposure_sentinel),
          "unknown exposure version was accepted");
  Require(exposure_unchanged(), "version failure changed exposure output");

  exposure = {};
  exposure.minimum_auto_exposure = 3.0;
  exposure.maximum_auto_exposure = 2.0;
  Require(!EvaluateHdrAutoExposureReference(exposure, exposure_sentinel),
          "inverted exposure bounds were accepted");
  Require(exposure_unchanged(), "bound failure changed exposure output");

  exposure = {};
  exposure.previous_inverse_luminance = 0.0;
  Require(!EvaluateHdrAutoExposureReference(exposure, exposure_sentinel),
          "zero previous inverse luminance was accepted");
  Require(exposure_unchanged(), "previous-value failure changed output");

  exposure = {};
  exposure.delta_seconds = -0.01;
  Require(!EvaluateHdrAutoExposureReference(exposure, exposure_sentinel),
          "negative frame delta was accepted");
  Require(exposure_unchanged(), "delta failure changed exposure output");

  exposure = {};
  exposure.exposure = (std::numeric_limits<double>::max)();
  Require(!EvaluateHdrAutoExposureReference(exposure, exposure_sentinel),
          "overflowing exposure evaluation was accepted");
  Require(exposure_unchanged(), "overflow failure changed exposure output");

  HdrFinalToneMapReferenceResult tone_sentinel;
  tone_sentinel.shader_output = {8.0, 9.0, 10.0};
  const auto tone_unchanged = [&tone_sentinel]() {
    return tone_sentinel.shader_output == Double3{8.0, 9.0, 10.0};
  };
  HdrFinalToneMapReferenceInput tone;
  tone.scene_linear_hdr.x = -0.01F;
  Require(!EvaluateHdrFinalToneMapReference(tone, tone_sentinel),
          "negative HDR scene value was accepted");
  Require(tone_unchanged(), "scene failure changed tone-map output");

  tone = {};
  tone.bloom_srgb.y = 1.01F;
  Require(!EvaluateHdrFinalToneMapReference(tone, tone_sentinel),
          "bloom value above one was accepted");
  Require(tone_unchanged(), "bloom failure changed tone-map output");

  tone = {};
  tone.inverse_luminance = (std::numeric_limits<double>::quiet_NaN)();
  Require(!EvaluateHdrFinalToneMapReference(tone, tone_sentinel),
          "NaN inverse luminance was accepted");
  Require(tone_unchanged(), "inverse-luminance failure changed output");

  tone = {};
  tone.alpha = 1.01;
  Require(!EvaluateHdrFinalToneMapReference(tone, tone_sentinel),
          "alpha above one was accepted");
  Require(tone_unchanged(), "alpha failure changed tone-map output");
}

} // namespace

int main() {
  TestPinnedIdentityAndExposureGolden();
  TestExposureClampAndTemporalEndpoints();
  TestToneMapGoldenAndChannelIndependence();
  TestMonotonicFiniteRange();
  TestMalformedInputsAreTransactional();
  std::cout << "portable pinned Ogre-Next HDR reference tests passed\n";
  return EXIT_SUCCESS;
}
