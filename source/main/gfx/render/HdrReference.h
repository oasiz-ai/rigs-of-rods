/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Portable numerical oracle for the pinned Ogre-Next HDR pipeline.

#pragma once

#include "RenderMath.h"
#include "RenderValidation.h"

#include <cstdint>

namespace RoR::Render {

/// Version 1 mirrors the auto-exposure and final filmic tone-map equations in
/// the pinned Ogre-Next HDR sample. The oracle intentionally ends at the
/// fragment shader output: framebuffer clamping, gamut mapping, an SDR OETF,
/// HDR transfer functions, dithering, and display calibration are separate
/// versioned stages.
constexpr std::uint32_t kHdrReferenceVersion = 1U;
constexpr const char kHdrReferenceOgreNextCommit[] =
    "37149a802de747f6806996fa3067b0748ecc1084";
constexpr const char kHdrReferenceMetalShaderSha256[] =
    "c5646e0b52ddfff8da39b2cd81fc874d63ce1dfb72dcf325fe9e31cee366af40";
constexpr const char kHdrReferenceHlslShaderSha256[] =
    "6f8bdaee587565fdba06525ead91b1e6a2e8b86f8a1270d35850d6028c19a119";
constexpr const char kHdrReferenceGlslShaderSha256[] =
    "9ecc4946a5cc046c11eca50543af8a6d5bb4745475c6c5c166c25e76c6cbaedb";
constexpr const char kHdrReferenceUtilitySha256[] =
    "0c58ca0fe592b949662b8085b3aebb3949d4fa3c9f071f9a55f12836f3dadb13";

struct HdrAutoExposureReferenceInput {
  std::uint32_t version = kHdrReferenceVersion;
  /// Ogre's user-facing exposure value passed to HdrUtils::setExposure().
  double exposure = 0.0;
  double minimum_auto_exposure = -1.0;
  double maximum_auto_exposure = 2.5;
  /// Average natural-log luminance produced by Ogre's downscale chain.
  double average_log_luminance = 0.0;
  /// Previous frame's adapted inverse-luminance multiplier.
  double previous_inverse_luminance = 1.0;
  /// Explicit simulation/render delta. No wall clock is consulted.
  double delta_seconds = 1.0 / 48.0;
};

struct HdrAutoExposureReferenceResult {
  double exposure_numerator = 0.0;
  double minimum_log_luminance = 0.0;
  double maximum_log_luminance = 0.0;
  double clamped_log_luminance = 0.0;
  double target_inverse_luminance = 0.0;
  double previous_frame_weight = 0.0;
  double adapted_inverse_luminance = 0.0;
};

/// Reproduces Ogre-Next's exposure parameter setup, luminance clamp, and
/// 75%-per-second temporal adaptation in deterministic binary64 arithmetic.
/// A failure leaves `output` unchanged.
[[nodiscard]] ValidationResult
EvaluateHdrAutoExposureReference(const HdrAutoExposureReferenceInput &input,
                                 HdrAutoExposureReferenceResult &output);

struct HdrFinalToneMapReferenceInput {
  std::uint32_t version = kHdrReferenceVersion;
  /// Linear scene color sampled from Ogre's HDR render target.
  Float3 scene_linear_hdr{};
  /// Bloom texture sample as consumed by the pinned shader. The shader uses
  /// its historical x*x approximation when converting this sample to linear.
  Float3 bloom_srgb{};
  double inverse_luminance = 1.0;
  double alpha = 1.0;
};

struct HdrFinalToneMapReferenceResult {
  Double3 exposed_scene_linear{};
  Double3 bloom_linear_approximation{};
  Double3 combined_linear{};
  Double3 filmic_normalized{};
  /// Exact pre-framebuffer RGB returned by the pinned shader, including its
  /// contrast and lift operation. Values are deliberately not clamped.
  Double3 shader_output{};
  double alpha = 1.0;
};

/// Reproduces the Metal, HLSL, and GLSL FinalToneMapping shader equation in
/// deterministic binary64 arithmetic. A failure leaves `output` unchanged.
[[nodiscard]] ValidationResult
EvaluateHdrFinalToneMapReference(const HdrFinalToneMapReferenceInput &input,
                                 HdrFinalToneMapReferenceResult &output);

} // namespace RoR::Render
