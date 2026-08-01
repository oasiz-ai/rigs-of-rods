/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Portable numerical oracle for the pinned Ogre-Next direct BRDF.

#pragma once

#include "RenderMath.h"
#include "RenderValidation.h"

#include <cstdint>

namespace RoR::Render {

/// Version 1 reproduces the full-precision, height-correlated
/// `PbsBrdf::Default` metallic workflow in the pinned Ogre-Next source. It is
/// deliberately narrower than the complete material contract: no clear coat,
/// transmission, sheen, anisotropy, or diffuse-Fresnel variant is implied.
constexpr std::uint32_t kPbrDirectReferenceVersion = 1U;
constexpr const char kPbrDirectReferenceOgreNextCommit[] =
    "37149a802de747f6806996fa3067b0748ecc1084";

struct PbrDirectReferenceInput {
  std::uint32_t version = kPbrDirectReferenceVersion;
  /// Resolved linear-light base color after factors, texture, and vertex color.
  Float3 base_color_linear{1.0F, 1.0F, 1.0F};
  float metallic = 0.0F;
  /// glTF perceptual roughness. The pinned shader squares this value and then
  /// clamps the resulting microfacet alpha to 0.001.
  float perceptual_roughness = 1.0F;
  /// Surface-to-view and surface-to-light directions. All three directions are
  /// normalized by this oracle using binary64 arithmetic before evaluation.
  Float3 shading_normal{0.0F, 1.0F, 0.0F};
  Float3 view_direction{0.0F, 1.0F, 0.0F};
  Float3 light_direction{0.0F, 1.0F, 0.0F};
};

struct PbrDirectReferenceResult {
  /// Diffuse and specular BRDF lobes before multiplying by N dot L or light.
  Double3 diffuse_brdf{};
  Double3 specular_brdf{};
  /// `NdotL * (diffuse_brdf + specular_brdf)`: the RGB multiplier for one
  /// unit of equal diffuse/specular linear incident light.
  Double3 direct_response{};
  Double3 specular_f0{};
  double n_dot_l = 0.0;
  double n_dot_v = 0.0;
  double n_dot_h = 0.0;
  double v_dot_h = 0.0;
  double microfacet_alpha = 0.0;
};

/// Evaluates a deterministic CPU reference for the exact supported shader
/// slice. Inputs behind the shading hemisphere produce an exact black response;
/// malformed inputs fail transactionally and leave `output` unchanged.
[[nodiscard]] ValidationResult
EvaluatePbrDirectReference(const PbrDirectReferenceInput &input,
                           PbrDirectReferenceResult &output);

} // namespace RoR::Render
