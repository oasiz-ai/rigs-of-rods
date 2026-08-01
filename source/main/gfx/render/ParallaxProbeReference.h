/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Portable numerical oracle for Ogre-Next box-projected probes.

#pragma once

#include "RenderMath.h"
#include "RenderValidation.h"

#include <cstdint>

namespace RoR::Render {

constexpr std::uint32_t kParallaxProbeReferenceVersion = 1U;
constexpr const char kParallaxProbeReferenceOgreNextCommit[] =
    "37149a802de747f6806996fa3067b0748ecc1084";
constexpr const char kParallaxProbeReferenceShaderPath[] =
    "Samples/Media/Hlms/Common/Any/Cubemap_piece_all.any";
constexpr const char kParallaxProbeReferenceShaderSha256[] =
    "ed281b8599716c769f1d99f14fb42568a586e018b1e9fac9ee03944cbd1d7fbb";
constexpr const char kParallaxProbeReferenceManualWeightShaderPath[] =
    "Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any";
constexpr const char kParallaxProbeReferenceManualWeightShaderSha256[] =
    "12cebd71e877c1d265df8d68f6c3f2931127679a8e77bb01c92b1221a09f5a7f";
constexpr const char kParallaxProbeReferenceAutomaticWeightShaderPath[] =
    "Samples/Media/Hlms/Pbs/Any/ForwardPlus_DecalsCubemaps_piece_ps.any";
constexpr const char kParallaxProbeReferenceAutomaticWeightShaderSha256[] =
    "64d53a29393192598e0111d1a729eb031eb7234273fef5753e2ccc9a121b5ada";
constexpr const char kParallaxProbeReferenceBufferSourcePath[] =
    "Components/Hlms/Pbs/src/Cubemaps/OgreParallaxCorrectedCubemapBase.cpp";
constexpr const char kParallaxProbeReferenceBufferSourceSha256[] =
    "e4832fb5afbc466fc1d07a8b08e11a72a84cd38eced229ce70e4cf625a898ea6";
constexpr const char kParallaxProbeReferenceProbeSourcePath[] =
    "Components/Hlms/Pbs/src/Cubemaps/OgreCubemapProbe.cpp";
constexpr const char kParallaxProbeReferenceProbeSourceSha256[] =
    "9c7a7dd6560fd11654a7751978c44752a8845ffb83966c57e2320cc0e46cf8ee";

/// Inputs are already in the oriented probe's local right-handed space. The
/// backend adapter must apply the pinned view-to-probe rotation and center
/// translation before invoking or comparing this oracle.
struct ParallaxProbeReferenceInput {
  std::uint32_t version = kParallaxProbeReferenceVersion;
  Float3 position_local{};
  Float3 reflection_direction_local{0.0F, 0.0F, 1.0F};
  Float3 probe_half_size{1.0F, 1.0F, 1.0F};
  Float3 cubemap_position_local{};

  /// Automatic-probe influence parameters from Ogre-Next's forward-plus
  /// cubemap path. The area center is expressed relative to the probe center.
  Float3 area_center_offset_local{};
  Float3 area_inner_range{};
  Float3 area_outer_range{1.0F, 1.0F, 1.0F};
  std::uint16_t priority = 1U;
};

struct ParallaxProbeReferenceResult {
  bool sample_active = false;
  double raw_box_fade = 0.0;
  double manual_probe_weight = 0.0;
  double automatic_ndf = 0.0;
  double automatic_probe_weight = 0.0;
  double intersection_distance = 0.0;
  Double3 intersection_local{};
  /// Ogre-Next flips local Z before cubemap lookup, producing a left-handed,
  /// deliberately unnormalized sampling vector.
  Double3 corrected_direction_left_handed{};
};

/// Evaluates the pinned `getProbeFade`, `getProbeNDF`, and `localCorrect`
/// equations as an idealized deterministic binary64 reference. Backend shader
/// captures remain subject to their declared float/mediump tolerance. Positions
/// at or outside the box boundary succeed with `sample_active=false`.
/// Malformed or undefined active samples fail transactionally and leave
/// `output` unchanged.
[[nodiscard]] ValidationResult
EvaluateParallaxProbeReference(const ParallaxProbeReferenceInput &input,
                               ParallaxProbeReferenceResult &output);

} // namespace RoR::Render
