/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/detail/OgreNextDemoPrivatePolicy.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Gfx::Detail;
using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void CheckCuratedCityWorldAsiaPolicy() {
  Require(kOgreNextDemoCuratedCityWorldAsiaPolicyVersion == 1U &&
              kOgreNextDemoCuratedCityWorldAsiaPolicyEntryCount == 3U &&
              kOgreNextDemoCuratedCityWorldSamplerProfile ==
                  "reviewed_configured_anisotropic_min_mag_linear_mip_"
                  "anisotropy4_v1" &&
              kOgreNextDemoCuratedCityWorldAcceptanceConfigSha256 ==
                  "54305f5c7f99fa6a9628337508d230f588e60d1d410f6d6fe56be3186790a57e" &&
              kOgreNextDemoCuratedCityWorldEnvironmentPolicy ==
                  "reviewed_spherical_environment_authority_bound_pending_not_presented_v1",
          "curated CityWorld policy version or pending environment semantic changed");
  const std::array<std::string_view, 3U> names{{
      "Material_#58/asiafacade", "Material_#58/asiawindow",
      "Material_#58/asiamarble"}};
  const std::array<std::string_view, 3U> bases{{
      "asiafacade.dds", "asiawindow.dds", "marble.dds"}};
  const std::array<std::string_view, 3U> speculars{{
      "asiafacade_spec.dds", "asiawindow_spec.dds", "marble_spec.dds"}};
  const std::array<float, 3U> roughness{{0.48F, 0.12F, 0.27F}};
  const std::array<float, 3U> ior{{1.50F, 1.52F, 1.50F}};
  for (std::size_t index = 0U; index < names.size(); ++index) {
    const OgreNextDemoCuratedCityWorldMaterial *const policy =
        OgreNextDemoCuratedCityWorldMaterialAt(index);
    Require(policy != nullptr &&
                FindOgreNextDemoCuratedCityWorldMaterial(names[index]) ==
                    policy &&
                policy->exact_material_name == names[index] &&
                policy->base_color_texture_name == bases[index] &&
                policy->linear_specular_texture_name == speculars[index] &&
                policy->spherical_environment_texture_name ==
                    "767chrome.jpg" &&
                policy->workflow ==
                    OgreNextDemoCuratedCityWorldWorkflow::SPECULAR &&
                policy->base_color_texture_unit == 0U &&
                policy->linear_specular_texture_unit == 1U &&
                policy->spherical_environment_texture_unit == 2U &&
                policy->roughness_factor == roughness[index] &&
                policy->specular_factor ==
                    std::array<float, 3U>{1.0F, 1.0F, 1.0F} &&
                policy->index_of_refraction == ior[index] &&
                policy->alpha_policy ==
                    OgreNextDemoCuratedCityWorldAlphaPolicy::FORCE_OPAQUE &&
                policy->depth_write && policy->clockwise_cull &&
                policy->sampler_policy ==
                    OgreNextDemoCuratedCityWorldSamplerPolicy::
                        REVIEWED_CONFIGURED_ANISOTROPIC4_V1 &&
                policy->environment_policy ==
                    OgreNextDemoCuratedCityWorldEnvironmentPolicy::
                        SPHERICAL_AUTHORITY_BOUND_PENDING_NOT_PRESENTED,
            "one exact curated CityWorld reviewed row changed");
  }
  Require(OgreNextDemoCuratedCityWorldMaterialAt(3U) == nullptr &&
              FindOgreNextDemoCuratedCityWorldMaterial(
                  "Material_#58/not-reviewed") == nullptr,
          "curated CityWorld lookup broadened beyond three reviewed rows");

  OgreNextDemoCuratedCityWorldMaterial modified =
      *OgreNextDemoCuratedCityWorldMaterialAt(0U);
  modified.roughness_factor = 0.49F;
  const std::array<std::uint8_t, 1U> unauthenticated_bytes{{0U}};
  const OgreNextDemoCuratedCityWorldSourceObservation incomplete_observation{
      kOgreNextDemoCuratedCityWorldArchiveSha256,
      kOgreNextDemoCuratedCityWorldScriptMember,
      kOgreNextDemoCuratedCityWorldScriptSha256,
      unauthenticated_bytes.data(), unauthenticated_bytes.size()};
  Require(!AuthenticateOgreNextDemoCuratedCityWorldMaterial(
               modified, incomplete_observation),
          "unauthored PBR parameter change retained reviewed authority");
  Require(!AuthenticateOgreNextDemoCuratedCityWorldMaterial(
              *OgreNextDemoCuratedCityWorldMaterialAt(0U),
              incomplete_observation),
          "incomplete private source bytes retained reviewed authority");
}

TextureMipLevelDescriptor MakeMip(std::uint32_t width, std::uint32_t height,
                                  std::vector<std::uint8_t> bytes) {
  TextureMipLevelDescriptor mip;
  mip.width = width;
  mip.height = height;
  mip.row_pitch_bytes = static_cast<std::uint64_t>(width) * 4U;
  mip.layer_pitch_bytes = mip.row_pitch_bytes * height;
  mip.bytes = std::move(bytes);
  return mip;
}

Ogre14DecodedSourceTextureMip MakeDecodedMip(std::uint32_t width,
                                             std::uint32_t height,
                                             std::vector<std::uint8_t> bytes) {
  Ogre14DecodedSourceTextureMip mip;
  mip.width = width;
  mip.height = height;
  mip.row_pitch_bytes = static_cast<std::uint64_t>(width) * 4U;
  mip.slice_pitch_bytes = mip.row_pitch_bytes * height;
  mip.rgba8_unorm = std::move(bytes);
  return mip;
}

TextureResourceDescriptor NativeBaseLevel() {
  TextureResourceDescriptor texture;
  texture.debug_name = "OgreNextDemo/TestComposite";
  texture.type = TextureResourceType::TEXTURE_2D;
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 4U;
  texture.height = 4U;
  texture.array_layers = 1U;

  std::vector<std::uint8_t> base(4U * 4U * 4U);
  for (std::size_t texel = 0U; texel < 16U; ++texel) {
    base[texel * 4U + 0U] = static_cast<std::uint8_t>(10U + texel);
    base[texel * 4U + 1U] = static_cast<std::uint8_t>(30U + texel);
    base[texel * 4U + 2U] = static_cast<std::uint8_t>(50U + texel);
    base[texel * 4U + 3U] = static_cast<std::uint8_t>(texel);
  }
  texture.mip_levels.push_back(MakeMip(4U, 4U, std::move(base)));
  return texture;
}

void CheckFullMipOpaqueLowering() {
  TextureResourceDescriptor texture = NativeBaseLevel();
  const std::vector<std::uint8_t> base_before = texture.mip_levels[0U].bytes;

  const ValidationResult result = CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(result.ok(), "valid native base level was rejected");
  Require(texture.mip_levels.size() == 3U &&
              texture.mip_levels.back().width == 1U &&
              texture.mip_levels.back().height == 1U,
          "native base was not completed through 1x1");
  for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
    const auto &bytes = texture.mip_levels[level].bytes;
    for (std::size_t alpha = 3U; alpha < bytes.size(); alpha += 4U) {
      Require(bytes[alpha] == 255U, "one output mip retained non-opaque alpha");
    }
  }
  for (std::size_t offset = 0U; offset < base_before.size(); ++offset) {
    if (offset % 4U != 3U) {
      Require(texture.mip_levels[0U].bytes[offset] == base_before[offset],
              "base native RGB byte changed");
    }
  }
  const std::array<std::uint8_t, 16U> expected_second{
      {13U, 33U, 53U, 255U, 15U, 35U, 55U, 255U, 21U, 41U, 61U, 255U, 23U, 43U,
       63U, 255U}};
  Require(std::equal(expected_second.begin(), expected_second.end(),
                     texture.mip_levels[1U].bytes.begin()),
          "generated 4x4-to-2x2 display-domain box result changed");
  const std::array<std::uint8_t, 4U> expected{{18U, 38U, 58U, 255U}};
  Require(std::equal(expected.begin(), expected.end(),
                     texture.mip_levels[2U].bytes.begin()),
          "generated display-domain 2x2 box result changed");
}

void CheckMalformedMipRollback() {
  TextureResourceDescriptor texture = NativeBaseLevel();
  texture.mip_levels[0U].row_pitch_bytes = 9U;
  const TextureResourceDescriptor before = texture;
  const ValidationResult result = CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(!result.ok() && result.code == ValidationCode::SIZE_MISMATCH,
          "malformed native base level was accepted");
  Require(texture.mip_levels.size() == before.mip_levels.size() &&
              texture.mip_levels[0U].bytes == before.mip_levels[0U].bytes &&
              texture.mip_levels[0U].row_pitch_bytes == 9U,
          "failed mip validation partially changed the candidate");

  texture = NativeBaseLevel();
  texture.mip_levels.push_back(
      MakeMip(2U, 2U, std::vector<std::uint8_t>(2U * 2U * 4U, 7U)));
  const TextureResourceDescriptor extra_before = texture;
  const ValidationResult extra = CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(!extra.ok() && texture.mip_levels.size() == 2U &&
              texture.mip_levels[0U].bytes ==
                  extra_before.mip_levels[0U].bytes &&
              texture.mip_levels[1U].bytes == extra_before.mip_levels[1U].bytes,
          "native nonzero mip was read or partially rewritten");
}

void CheckConventionalSrgbPbrMipChain() {
  TextureResourceDescriptor texture;
  texture.debug_name = "OgreNextDemo/TestSrgbPbr";
  texture.type = TextureResourceType::TEXTURE_2D;
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 2U;
  texture.height = 2U;
  texture.array_layers = 1U;
  texture.mip_levels.push_back(
      MakeMip(2U, 2U,
              {// Hostile black/white contrast in R, midtones in G, and four
               // distinct midtones in B. Authored alpha must not affect RGB.
               0U, 64U, 16U, 0U, 0U, 64U, 80U, 1U, 255U, 192U, 144U, 127U, 255U,
               192U, 208U, 254U}));
  const std::vector<std::uint8_t> base_before =
      texture.mip_levels.front().bytes;

  OgreNextDemoTextureNormalizationObservation normalization;
  const ValidationResult result =
      CompleteOgreNextDemoSrgbPbrMipChain(texture, &normalization);
  Require(result.ok(), "valid conventional sRGB PBR base was rejected");
  Require(texture.mip_levels.size() == 2U &&
              texture.mip_levels.back().width == 1U &&
              texture.mip_levels.back().height == 1U,
          "conventional sRGB PBR base was not completed through 1x1");
  Require(normalization.policy ==
                  OgreNextDemoTextureNormalizationObservation::Policy::
                      SRGB_OPAQUE_V2 &&
              normalization.policy_version ==
                  kOgreNextDemoModernSourceNormalizationPolicyVersion &&
              kOgreNextDemoModernSourceNormalizationPolicyVersion == 2U &&
              kOgreNextDemoModernSourceNormalizationPolicy ==
                  "srgb_opaque_authored_prefix_linear_tail_v2" &&
              normalization.authored_mip_prefix_levels == 1U &&
              normalization.generated_mip_tail_levels == 1U,
          "modern normalization version or mip provenance changed");
  const std::array<std::uint8_t, 4U> expected{{188U, 146U, 137U, 255U}};
  Require(std::equal(expected.begin(), expected.end(),
                     texture.mip_levels[1U].bytes.begin()),
          "linear-light sRGB 2x2 box result changed");
  Require(texture.mip_levels[1U].bytes[0U] != 128U &&
              texture.mip_levels[1U].bytes[1U] != 128U &&
              texture.mip_levels[1U].bytes[2U] != 112U,
          "sRGB PBR mips regressed to averaging encoded bytes");
  for (std::size_t offset = 0U; offset < base_before.size(); ++offset) {
    if (offset % 4U == 3U) {
      Require(texture.mip_levels[0U].bytes[offset] == 255U,
              "sRGB PBR base alpha was not forced opaque");
    } else {
      Require(texture.mip_levels[0U].bytes[offset] == base_before[offset],
              "sRGB PBR base RGB byte changed");
    }
  }

  TextureResourceDescriptor threshold = texture;
  threshold.debug_name = "OgreNextDemo/TestSrgbThreshold";
  threshold.mip_levels.resize(1U);
  threshold.mip_levels.front() =
      MakeMip(2U, 2U,
              {0U, 0U, 0U, 255U, 0U, 0U, 0U, 255U, 0U, 0U, 0U, 255U, 174U, 174U,
               174U, 255U});
  Require(CompleteOgreNextDemoSrgbPbrMipChain(threshold).ok() &&
              threshold.mip_levels[1U].bytes[0U] == 92U &&
              threshold.mip_levels[1U].bytes[1U] == 92U &&
              threshold.mip_levels[1U].bytes[2U] == 92U,
          "standard sRGB half-code threshold quantization regressed to "
          "nearest decoded-code midpoint");
}

void CheckConventionalSrgbPbrRollback() {
  TextureResourceDescriptor texture = NativeBaseLevel();
  texture.mip_levels[0U].layer_pitch_bytes -= 4U;
  const TextureResourceDescriptor before = texture;
  const ValidationResult result = CompleteOgreNextDemoSrgbPbrMipChain(texture);
  Require(!result.ok() && result.code == ValidationCode::SIZE_MISMATCH &&
              texture.mip_levels.size() == before.mip_levels.size() &&
              texture.mip_levels[0U].bytes == before.mip_levels[0U].bytes &&
              texture.mip_levels[0U].layer_pitch_bytes ==
                  before.mip_levels[0U].layer_pitch_bytes,
          "malformed sRGB PBR base partially changed the candidate");

  texture = NativeBaseLevel();
  texture.mip_levels.push_back(
      MakeMip(2U, 2U, std::vector<std::uint8_t>(2U * 2U * 4U, 31U)));
  const TextureResourceDescriptor extra_before = texture;
  OgreNextDemoTextureNormalizationObservation normalization;
  const ValidationResult extra =
      CompleteOgreNextDemoSrgbPbrMipChain(texture, &normalization);
  Require(extra.ok() && texture.mip_levels.size() == 3U &&
              normalization.authored_mip_prefix_levels == 2U &&
              normalization.generated_mip_tail_levels == 1U,
          "valid authored sRGB PBR mip prefix was not preserved/completed");
  for (std::size_t offset = 0U;
       offset < extra_before.mip_levels[1U].bytes.size(); ++offset) {
    Require(texture.mip_levels[1U].bytes[offset] ==
                (offset % 4U == 3U ? 255U
                                   : extra_before.mip_levels[1U].bytes[offset]),
            "authored nonzero mip RGB changed or alpha was not normalized");
  }
}

void CheckStraightAlphaPremultipliedMipChain() {
  TextureResourceDescriptor texture;
  texture.debug_name = "OgreNextDemo/TestStraightAlpha";
  texture.type = TextureResourceType::TEXTURE_2D;
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 2U;
  texture.height = 2U;
  texture.array_layers = 1U;
  texture.mip_levels.push_back(MakeMip(
      2U, 2U,
      {// One opaque red texel surrounded by fully transparent hidden blue.
       // Straight averaging would create a purple fringe; premultiplied-linear
       // filtering followed by robust unpremultiply must remain red.
       255U, 0U, 0U, 255U, 0U, 0U, 255U, 0U,
       0U, 0U, 255U, 0U,   0U, 0U, 255U, 0U}));
  const std::vector<std::uint8_t> authored = texture.mip_levels[0U].bytes;
  OgreNextDemoTextureNormalizationObservation normalization;
  Require(CompleteOgreNextDemoSrgbPbrMipChain(
              texture, OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT,
              &normalization)
              .ok() &&
              texture.mip_levels.size() == 2U &&
              texture.mip_levels[0U].bytes == authored &&
              texture.mip_levels[1U].bytes ==
                  std::vector<std::uint8_t>({255U, 0U, 0U, 64U}) &&
              normalization.policy ==
                  OgreNextDemoTextureNormalizationObservation::Policy::
                      SRGB_STRAIGHT_ALPHA_V1 &&
              normalization.policy_version ==
                  kOgreNextDemoStraightAlphaNormalizationPolicyVersion &&
              normalization.authored_mip_prefix_levels == 1U &&
              normalization.generated_mip_tail_levels == 1U,
          "straight-alpha premultiplied-linear mip filtering or fixed "
          "coverage behavior changed");

  TextureResourceDescriptor transparent = texture;
  transparent.debug_name = "OgreNextDemo/TestTransparentCanonicalBlack";
  transparent.mip_levels.resize(1U);
  transparent.mip_levels[0U] = MakeMip(
      2U, 2U, {255U, 0U, 0U, 0U, 0U, 255U, 0U, 0U,
               0U, 0U, 255U, 0U, 255U, 255U, 255U, 0U});
  Require(CompleteOgreNextDemoSrgbPbrMipChain(
              transparent,
              OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT)
              .ok() &&
              transparent.mip_levels[1U].bytes ==
                  std::vector<std::uint8_t>({0U, 0U, 0U, 0U}),
          "zero-coverage straight-alpha mip amplified hidden RGB");

  TextureResourceDescriptor authored_prefix = NativeBaseLevel();
  authored_prefix.debug_name = "OgreNextDemo/TestStraightAlphaPrefix";
  const std::vector<std::uint8_t> authored_lower{
      1U, 2U, 3U, 0U, 4U, 5U, 6U, 64U,
      7U, 8U, 9U, 128U, 10U, 11U, 12U, 255U};
  authored_prefix.mip_levels.push_back(
      MakeMip(2U, 2U, authored_lower));
  OgreNextDemoTextureNormalizationObservation prefix_observation;
  Require(CompleteOgreNextDemoSrgbPbrMipChain(
              authored_prefix,
              OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT,
              &prefix_observation)
              .ok() &&
              authored_prefix.mip_levels.size() == 3U &&
              authored_prefix.mip_levels[1U].bytes == authored_lower &&
              prefix_observation.authored_mip_prefix_levels == 2U &&
              prefix_observation.generated_mip_tail_levels == 1U,
          "straight-alpha authored mip prefix was rewritten or discarded");

  TextureResourceDescriptor malformed_prefix = authored_prefix;
  malformed_prefix.debug_name = "straight-alpha-sentinel";
  malformed_prefix.mip_levels.resize(2U);
  malformed_prefix.mip_levels[1U].width = 3U;
  const TextureResourceDescriptor malformed_before = malformed_prefix;
  const ValidationResult malformed_result =
      CompleteOgreNextDemoSrgbPbrMipChain(
          malformed_prefix,
          OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT);
  Require(!malformed_result &&
              malformed_prefix.debug_name == malformed_before.debug_name &&
              malformed_prefix.mip_levels.size() ==
                  malformed_before.mip_levels.size() &&
              malformed_prefix.mip_levels[0U].bytes ==
                  malformed_before.mip_levels[0U].bytes &&
              malformed_prefix.mip_levels[1U].bytes ==
                  malformed_before.mip_levels[1U].bytes &&
              malformed_prefix.mip_levels[1U].width == 3U,
          "malformed straight-alpha authored prefix partially published");
}

void CheckBc1AlphaAuthority() {
  Ogre14SourceTextureBc1AlphaMode mode =
      Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA;
  Require(ResolveOgreNextDemoBc1AlphaMode(
              true, OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE, false, mode)
                  .ok() &&
              mode == Ogre14SourceTextureBc1AlphaMode::OPAQUE,
          "explicit opaque BC1 policy was not selected");
  mode = Ogre14SourceTextureBc1AlphaMode::OPAQUE;
  const ValidationResult ambiguous = ResolveOgreNextDemoBc1AlphaMode(
      true, OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT, false, mode);
  Require(!ambiguous && ambiguous.code == ValidationCode::MISSING_REFERENCE &&
              ambiguous.field ==
                  "ogre_next_demo.material.bc1_alpha.authority" &&
              mode == Ogre14SourceTextureBc1AlphaMode::OPAQUE,
          "blend/test state inferred one-bit alpha for ambiguous legacy DXT1");
  Require(ResolveOgreNextDemoBc1AlphaMode(
              true, OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT, true,
              mode)
                  .ok() &&
              mode == Ogre14SourceTextureBc1AlphaMode::ONE_BIT_ALPHA,
          "explicit one-bit BC1 authority was ignored");
  Require(ResolveOgreNextDemoBc1AlphaMode(
              false, OgreNextDemoTextureAlphaPolicy::PRESERVE_STRAIGHT, false,
              mode)
                  .ok() &&
              mode == Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE,
          "non-BC1 source was assigned a BC1 alpha interpretation");
}

void CheckLinearSpecularMipChain() {
  Ogre14DecodedSourceTexture decoded;
  decoded.width = 2U;
  decoded.height = 2U;
  decoded.source_format = Ogre14SourceTextureFormat::RGBA8_UNORM;
  decoded.color_semantic = Ogre14SourceTextureColorSemantic::LINEAR_DATA;
  decoded.bc1_alpha_mode =
      Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  decoded.source_has_alpha = true;
  decoded.mip_levels.push_back(MakeDecodedMip(
      2U, 2U, {0U, 4U, 252U, 1U, 1U, 5U, 253U, 2U,
               2U, 6U, 254U, 3U, 3U, 7U, 255U, 4U}));
  const std::vector<std::uint8_t> authored =
      decoded.mip_levels[0U].rgba8_unorm;
  TextureResourceDescriptor output;
  OgreNextDemoTextureNormalizationObservation normalization;
  Require(BuildOgreNextDemoLinearSpecularTextureFromDecodedSource(
              std::move(decoded), 2U, 2U, "decoded/linear-specular", output,
              &normalization)
              .ok() &&
              output.color_space == TextureColorSpace::LINEAR &&
              output.mip_levels.size() == 2U &&
              output.mip_levels[1U].bytes ==
                  std::vector<std::uint8_t>({2U, 6U, 254U, 255U}) &&
              normalization.policy ==
                  OgreNextDemoTextureNormalizationObservation::Policy::
                      LINEAR_SPECULAR_V1 &&
              normalization.policy_version ==
                  kOgreNextDemoLinearSpecularNormalizationPolicyVersion,
          "linear authored specular mip normalization changed");
  for (std::size_t offset = 0U; offset < authored.size(); ++offset) {
    Require(output.mip_levels[0U].bytes[offset] ==
                (offset % 4U == 3U ? 255U : authored[offset]),
            "authored linear specular RGB changed or alpha stayed noncanonical");
  }


  Ogre14DecodedSourceTexture prefix;
  prefix.width = 4U;
  prefix.height = 4U;
  prefix.source_format = Ogre14SourceTextureFormat::RGBA8_UNORM;
  prefix.color_semantic = Ogre14SourceTextureColorSemantic::LINEAR_DATA;
  prefix.bc1_alpha_mode =
      Ogre14SourceTextureBc1AlphaMode::NOT_APPLICABLE;
  prefix.source_has_alpha = true;
  std::vector<std::uint8_t> specular_base(4U * 4U * 4U, 17U);
  std::vector<std::uint8_t> specular_lower{
      1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
      9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U};
  prefix.mip_levels.push_back(
      MakeDecodedMip(4U, 4U, specular_base));
  prefix.mip_levels.push_back(
      MakeDecodedMip(2U, 2U, specular_lower));
  TextureResourceDescriptor prefix_output;
  OgreNextDemoTextureNormalizationObservation prefix_observation;
  Require(BuildOgreNextDemoLinearSpecularTextureFromDecodedSource(
              Ogre14DecodedSourceTexture(prefix), 4U, 4U,
              "decoded/linear-specular-prefix", prefix_output,
              &prefix_observation)
              .ok() &&
              prefix_output.mip_levels.size() == 3U &&
              prefix_observation.authored_mip_prefix_levels == 2U &&
              prefix_observation.generated_mip_tail_levels == 1U,
          "linear-specular authored mip prefix was not completed through 1x1");
  for (std::size_t offset = 0U; offset < specular_lower.size(); ++offset) {
    Require(prefix_output.mip_levels[1U].bytes[offset] ==
                (offset % 4U == 3U ? 255U : specular_lower[offset]),
            "authored linear-specular lower mip RGB changed");
  }

  Ogre14DecodedSourceTexture malformed = prefix;
  malformed.mip_levels[1U].width = 3U;
  TextureResourceDescriptor sentinel = NativeBaseLevel();
  sentinel.debug_name = "linear-specular-sentinel";
  const TextureResourceDescriptor sentinel_before = sentinel;
  const ValidationResult malformed_result =
      BuildOgreNextDemoLinearSpecularTextureFromDecodedSource(
          std::move(malformed), 4U, 4U, "decoded/malformed-specular",
          sentinel);
  Require(!malformed_result &&
              sentinel.debug_name == sentinel_before.debug_name &&
              sentinel.mip_levels.size() == sentinel_before.mip_levels.size() &&
              sentinel.mip_levels.front().bytes ==
                  sentinel_before.mip_levels.front().bytes,
          "malformed linear-specular lower mip partially published output");
}

Ogre14DecodedSourceTexture DecodedSrgbMipPrefix() {
  const TextureResourceDescriptor native = NativeBaseLevel();
  Ogre14DecodedSourceTexture decoded;
  decoded.width = native.width;
  decoded.height = native.height;
  // Model the generic decoder's canonical output for one authored multi-mip
  // legacy DXT1 source. The product preserves every decoded authored level
  // and generates only a missing modern tail.
  decoded.source_format = Ogre14SourceTextureFormat::BC1_UNORM;
  decoded.color_semantic = Ogre14SourceTextureColorSemantic::SRGB_COLOR;
  decoded.bc1_alpha_mode = Ogre14SourceTextureBc1AlphaMode::OPAQUE;
  decoded.source_has_alpha = false;
  decoded.mip_levels.push_back(
      MakeDecodedMip(4U, 4U, native.mip_levels.front().bytes));
  decoded.mip_levels.push_back(
      MakeDecodedMip(2U, 2U, std::vector<std::uint8_t>(2U * 2U * 4U, 7U)));
  decoded.mip_levels.push_back(
      MakeDecodedMip(1U, 1U, std::vector<std::uint8_t>(4U, 19U)));
  return decoded;
}

void CheckDecodedSourcePreservesAuthoredMipPrefix() {
  Ogre14DecodedSourceTexture decoded = DecodedSrgbMipPrefix();
  std::vector<std::vector<std::uint8_t>> authored;
  for (const auto &mip : decoded.mip_levels) {
    authored.push_back(mip.rgba8_unorm);
  }
  TextureResourceDescriptor output;
  OgreNextDemoTextureNormalizationObservation normalization;
  const ValidationResult result =
      BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(std::move(decoded), 4U,
                                                       4U, "decoded/multi-mip",
                                                       output, &normalization);
  Require(result.ok() && output.debug_name == "decoded/multi-mip" &&
              output.mip_levels.size() == 3U &&
              normalization.authored_mip_prefix_levels == 3U &&
              normalization.generated_mip_tail_levels == 0U,
          "valid decoded multi-mip source was not lowered");
  for (std::size_t level = 0U; level < output.mip_levels.size(); ++level) {
    for (std::size_t offset = 0U;
         offset < output.mip_levels[level].bytes.size(); ++offset) {
      Require(output.mip_levels[level].bytes[offset] ==
                  (offset % 4U == 3U ? 255U : authored[level][offset]),
              "authored decoded mip RGB changed or alpha stayed nonopaque");
    }
  }
}

void CheckDecodedSourceFailureRollback() {
  TextureResourceDescriptor output = NativeBaseLevel();
  const TextureResourceDescriptor before = output;
  Ogre14DecodedSourceTexture malformed = DecodedSrgbMipPrefix();
  malformed.mip_levels[1U].width = 3U;
  const ValidationResult malformed_result =
      BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
          std::move(malformed), 4U, 4U, "decoded/malformed", output);
  Require(!malformed_result.ok() && output.debug_name == before.debug_name &&
              output.width == before.width && output.height == before.height &&
              output.mip_levels.size() == before.mip_levels.size() &&
              output.mip_levels.front().bytes ==
                  before.mip_levels.front().bytes,
          "malformed decoded lower mip changed product output");

  Ogre14DecodedSourceTexture wrong_dimensions = DecodedSrgbMipPrefix();
  const ValidationResult dimension_result =
      BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
          std::move(wrong_dimensions), 8U, 4U,
          "decoded/wrong-native-dimensions", output);
  Require(!dimension_result.ok() && output.debug_name == before.debug_name &&
              output.width == before.width && output.height == before.height &&
              output.mip_levels.size() == before.mip_levels.size() &&
              output.mip_levels.front().bytes ==
                  before.mip_levels.front().bytes,
          "decoded/native dimension mismatch changed product output");
}

void CheckTextureSourceSelectionContract() {
  OgreNextDemoTextureSourceSelection selection;
  selection.selected = true;
  selection.mode =
      OgreNextDemoTextureSourceMode::AUTHENTICATED_GENERATED_SOURCE_BYTES;
  selection.exclusion = OgreNextDemoTextureProjectionExclusion::NONE;
  const OgreNextDemoTextureSourceSelection sentinel = selection;
  const ValidationResult missing_receipt = ValidationResult::Failure(
      ValidationCode::MISSING_REFERENCE, "texture_registry.resource_lookup",
      "exact authenticated texture resource is absent");
  const ValidationResult required_missing = SelectOgreNextDemoTextureSourceMode(
      true, true, missing_receipt,
      OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES, false,
      ValidationResult::Success(), selection);
  Require(
      !required_missing.ok() && selection.selected == sentinel.selected &&
          selection.mode == sentinel.mode &&
          selection.exclusion == sentinel.exclusion,
      "authenticated-required missing receipt fell through to GPU readback");

  const ValidationResult ordinary_unavailable =
      SelectOgreNextDemoTextureSourceMode(
          false, false, ValidationResult::Success(),
          OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES,
          false, ValidationResult::Success(), selection);
  Require(
      ordinary_unavailable.ok() && !selection.selected &&
          selection.mode ==
              OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES &&
          selection.exclusion == OgreNextDemoTextureProjectionExclusion::
                                     ORDINARY_SELECTED_SOURCE_UNAVAILABLE,
      "ordinary texture without selected source was not explicitly matted");

  const ValidationResult ordinary = SelectOgreNextDemoTextureSourceMode(
      false, false, ValidationResult::Success(),
      OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES, true,
      ValidationResult::Success(), selection);
  Require(
      ordinary.ok() && selection.selected &&
          selection.mode ==
              OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES &&
          selection.exclusion == OgreNextDemoTextureProjectionExclusion::NONE,
      "ordinary selected source bytes were not selected");

  const ValidationResult ordinary_missing_receipt =
      SelectOgreNextDemoTextureSourceMode(
          false, false, ValidationResult::Success(),
          OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES,
          true,
          ValidationResult::Failure(
              ValidationCode::MISSING_REFERENCE,
              "selected_texture_registry.resource_lookup",
              "active group has no exact selected-source receipt"),
          selection);
  Require(ordinary_missing_receipt.ok() && !selection.selected &&
              selection.exclusion == OgreNextDemoTextureProjectionExclusion::
                                         ORDINARY_SELECTED_SOURCE_UNAVAILABLE,
          "honestly absent ordinary selected source was not explicitly matted");

  const ValidationResult ordinary_group_absent =
      SelectOgreNextDemoTextureSourceMode(
          false, false, ValidationResult::Success(),
          OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES,
          true,
          ValidationResult::Failure(
              ValidationCode::MISSING_REFERENCE,
              "selected_texture_resolution.group_generation",
              "resource group is not registered"),
          selection);
  Require(ordinary_group_absent.ok() && !selection.selected &&
              selection.exclusion == OgreNextDemoTextureProjectionExclusion::
                                         ORDINARY_SELECTED_SOURCE_UNAVAILABLE,
          "honestly unregistered ordinary group was not explicitly matted");

  const ValidationResult ordinary_package_absent =
      SelectOgreNextDemoTextureSourceMode(
          false, false, ValidationResult::Success(),
          OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES,
          true,
          ValidationResult::Failure(
              ValidationCode::MISSING_REFERENCE,
              "selected_texture_resolution.package_marker",
              "resource group has no ordinary package marker"),
          selection);
  Require(ordinary_package_absent.ok() && !selection.selected &&
              selection.exclusion == OgreNextDemoTextureProjectionExclusion::
                                         ORDINARY_SELECTED_SOURCE_UNAVAILABLE,
          "honestly absent ordinary package marker was not retryable matte");

  const std::array<ValidationResult, 7U> terminal_ordinary_failures{{
      missing_receipt,
      ValidationResult::Failure(ValidationCode::SEQUENCE_MISMATCH,
                                "selected_texture_resolution.group_generation",
                                "selected-source group generation changed"),
      ValidationResult::Failure(ValidationCode::INVALID_HANDLE,
                                "selected_texture_resolution.loaded",
                                "loaded resource identity changed"),
      ValidationResult::Failure(ValidationCode::REVISION_MISMATCH,
                                "selected_texture_registry.resource_lookup",
                                "selected-source receipt changed"),
      ValidationResult::Failure(ValidationCode::EMPTY_PAYLOAD,
                                "selected_texture_resolution.source_bytes",
                                "selected source is empty"),
      ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "ogre_next_demo.material.ordinary.selected_source",
          "resolver returned an unusable successful receipt"),
      ValidationResult::Failure(ValidationCode::MISSING_REFERENCE,
                                "selected_texture_registry",
                                "registry is unavailable"),
  }};
  for (const ValidationResult &terminal_failure : terminal_ordinary_failures) {
    selection = sentinel;
    const ValidationResult result = SelectOgreNextDemoTextureSourceMode(
        false, false, ValidationResult::Success(),
        OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES, true,
        terminal_failure, selection);
    Require(!result.ok() && result.code == terminal_failure.code &&
                result.field == terminal_failure.field &&
                selection.selected == sentinel.selected &&
                selection.mode == sentinel.mode &&
                selection.exclusion == sentinel.exclusion,
            "terminal ordinary resolver failure was flattened or committed");
  }

  const ValidationResult required_archive = SelectOgreNextDemoTextureSourceMode(
      true, true, ValidationResult::Success(),
      OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES, false,
      ValidationResult::Success(), selection);
  Require(
      required_archive.ok() && selection.selected &&
          selection.mode ==
              OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES,
      "authenticated archive resolution did not select source bytes");

  const ValidationResult required_generated =
      SelectOgreNextDemoTextureSourceMode(
          true, true, ValidationResult::Success(),
          OgreNextDemoTextureSourceMode::AUTHENTICATED_GENERATED_SOURCE_BYTES,
          false, ValidationResult::Success(), selection);
  Require(required_generated.ok() && selection.selected &&
              selection.mode == OgreNextDemoTextureSourceMode::
                                    AUTHENTICATED_GENERATED_SOURCE_BYTES,
          "authenticated generated resolution lost its distinct source mode");

  const ValidationResult ordinary_probe = SelectOgreNextDemoTextureSourceMode(
      false, true, ValidationResult::Success(),
      OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES, true,
      ValidationResult::Success(), selection);
  Require(!ordinary_probe.ok(),
          "ordinary texture was allowed to probe authenticated authority");

  const ValidationResult authenticated_probe =
      SelectOgreNextDemoTextureSourceMode(
          true, true, ValidationResult::Success(),
          OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES,
          true, ValidationResult::Success(), selection);
  Require(!authenticated_probe.ok(),
          "authenticated texture was allowed to probe ordinary authority");
}

void CheckCachedSourceReachabilitySequence() {
  constexpr OgreNextDemoTextureSourceMode authenticated =
      OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES;
  constexpr OgreNextDemoTextureSourceMode ordinary =
      OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES;

  const ValidationResult frame_n =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, true, true, true, ValidationResult::Success(), true);
  Require(frame_n.ok(), "frame N authenticated cache observation was rejected");
  Require(ValidateOgreNextDemoCachedTextureSourceAuthority(
              ordinary, true, true, true, ValidationResult::Success(), true)
              .ok(),
          "frame N ordinary cache observation was rejected");

  // N+1 has no reachable instance after same-map bundle unload. The immutable
  // owner remains only to prevent source-ID resurrection and probes nothing.
  const ValidationResult frame_n_plus_1 =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, false, false, false, ValidationResult::Success(),
          false);
  Require(
      frame_n_plus_1.ok() &&
          ValidateOgreNextDemoCachedTextureSourceAuthority(
              ordinary, false, false, false, ValidationResult::Success(), false)
              .ok(),
      "frame N+1 unreachable owner probed live source authority");

  const ValidationResult missing_fresh_receipt = ValidationResult::Failure(
      ValidationCode::MISSING_REFERENCE, "texture_resolution.resource_lookup",
      "same group/name reload has no exact frozen receipt");
  const ValidationResult frame_n_plus_2_missing =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, true, true, true, missing_fresh_receipt, false);
  Require(!frame_n_plus_2_missing.ok(),
          "frame N+2 missing fresh receipt fell through to GPU readback");

  const ValidationResult frame_n_plus_2_mutated =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, true, true, true, ValidationResult::Success(), false);
  Require(!frame_n_plus_2_mutated.ok(),
          "frame N+2 changed immutable receipt was accepted or read back");

  const ValidationResult reclassified_before_reuse =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, true, false, false, ValidationResult::Success(),
          false);
  Require(!reclassified_before_reuse.ok(),
          "cached authenticated entry changed authority classification");

  const ValidationResult ordinary_mutated =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          ordinary, true, true, true, ValidationResult::Success(), false);
  Require(!ordinary_mutated.ok(),
          "cached ordinary entry accepted changed selected-source bytes");
}

void CheckSourceAccountingAndEligibility() {
  OgreNextDemoTextureSourceCounters counters;
  for (std::size_t index = 0U; index < 29U; ++index) {
    Require(
        RecordOgreNextDemoTextureSourceDecode(
            OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES,
            counters)
            .ok(),
        "authenticated archive decode accounting failed");
  }
  for (std::size_t index = 0U; index < 3U; ++index) {
    Require(RecordOgreNextDemoTextureSourceDecode(
                OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES,
                counters)
                .ok(),
            "ordinary selected-source decode accounting failed");
  }
  Require(counters.authenticated_archive_source_decodes == 29U &&
              counters.authenticated_generated_source_decodes == 0U &&
              counters.authenticated_source_decodes == 29U &&
              counters.ordinary_observed_source_decodes == 3U &&
              counters.gpu_readbacks == 0U &&
              counters.authenticated_gpu_readbacks == 0U &&
              counters.unauthenticated_gpu_readbacks == 0U,
          "29 authenticated plus 3 ordinary source-byte accounting changed");

  Require(RecordOgreNextDemoTextureSourceCacheHit(counters).ok() &&
              counters.source_cache_hits == 1U,
          "source cache hit was not counted without a readback");
  Require(RecordOgreNextDemoTextureProjectionExclusion(
              OgreNextDemoTextureProjectionExclusion::SOURCE_DECODE_REJECTED,
              counters)
                  .ok() &&
              counters.source_decode_rejections == 1U &&
              counters.source_exclusions == 1U,
          "decode rejection was not retained as a bounded matte exclusion");
  Require(
      RecordOgreNextDemoTextureProjectionExclusion(
          OgreNextDemoTextureProjectionExclusion::UNSUPPORTED_SOURCE_CONTAINER,
          counters)
              .ok() &&
          RecordOgreNextDemoTextureProjectionExclusion(
              OgreNextDemoTextureProjectionExclusion::
                  UNSUPPORTED_SOURCE_SEMANTIC,
              counters)
              .ok() &&
          counters.source_decode_rejections == 2U &&
          counters.source_exclusions == 3U &&
          counters.candidate_sections == 3U &&
          counters.projected_sections == 0U &&
          counters.matte_excluded_sections == 3U &&
          counters.exclusions_by_reason[static_cast<std::size_t>(
              OgreNextDemoTextureProjectionExclusion::
                  UNSUPPORTED_SOURCE_CONTAINER)] == 1U &&
          counters.exclusions_by_reason[static_cast<std::size_t>(
              OgreNextDemoTextureProjectionExclusion::
                  UNSUPPORTED_SOURCE_SEMANTIC)] == 1U,
      "unsupported ordinary sources lost bounded exclusion accounting");

  OgreNextDemoTextureEligibilityObservation eligible;
  eligible.source_available = true;
  eligible.texture_2d = true;
  eligible.unit_depth = true;
  eligible.unit_face_count = true;
  eligible.dimensions_in_range = true;
  OgreNextDemoTextureProjectionExclusion exclusion =
      OgreNextDemoTextureProjectionExclusion::SOURCE_UNAVAILABLE;
  Require(ClassifyOgreNextDemoTextureProjectionEligibility(eligible, exclusion)
                  .ok() &&
              exclusion == OgreNextDemoTextureProjectionExclusion::NONE,
          "eligible file-backed 2D texture was excluded");

  const auto require_exclusion =
      [&](OgreNextDemoTextureEligibilityObservation observation,
          OgreNextDemoTextureProjectionExclusion expected) {
        OgreNextDemoTextureProjectionExclusion observed =
            OgreNextDemoTextureProjectionExclusion::NONE;
        Require(ClassifyOgreNextDemoTextureProjectionEligibility(observation,
                                                                 observed)
                        .ok() &&
                    observed == expected,
                "native texture exclusion classification changed");
      };
  OgreNextDemoTextureEligibilityObservation mutation = eligible;
  mutation.manually_loaded = true;
  require_exclusion(
      mutation, OgreNextDemoTextureProjectionExclusion::MANUAL_OR_PROCEDURAL);
  mutation = eligible;
  mutation.render_target = true;
  require_exclusion(mutation,
                    OgreNextDemoTextureProjectionExclusion::RENDER_TARGET);
  mutation.manually_loaded = true;
  require_exclusion(mutation,
                    OgreNextDemoTextureProjectionExclusion::RENDER_TARGET);
  mutation = eligible;
  mutation.texture_2d = false;
  mutation.cube_texture = true;
  mutation.unit_face_count = false;
  require_exclusion(mutation,
                    OgreNextDemoTextureProjectionExclusion::CUBE_TEXTURE);
  mutation = eligible;
  mutation.texture_2d = false;
  mutation.volume_texture = true;
  mutation.unit_depth = false;
  require_exclusion(mutation,
                    OgreNextDemoTextureProjectionExclusion::VOLUME_TEXTURE);
  mutation = eligible;
  mutation.texture_2d = false;
  require_exclusion(mutation, OgreNextDemoTextureProjectionExclusion::NON_2D);
  mutation.manually_loaded = true;
  require_exclusion(mutation, OgreNextDemoTextureProjectionExclusion::NON_2D);
  mutation = eligible;
  mutation.unit_depth = false;
  require_exclusion(mutation,
                    OgreNextDemoTextureProjectionExclusion::NON_UNIT_DEPTH);
  mutation = eligible;
  mutation.unit_face_count = false;
  require_exclusion(
      mutation, OgreNextDemoTextureProjectionExclusion::NON_UNIT_FACE_COUNT);

  counters.candidate_sections += 3U;
  counters.projected_sections = 3U;
  counters.projections = 3U;
  counters.active_replace_material_projections = 1U;
  counters.active_straight_source_over_material_projections = 1U;
  counters.active_legacy_straight_alpha_material_projections = 1U;
  counters.active_alpha_test_disabled_material_projections = 1U;
  counters.active_alpha_test_greater_material_projections = 1U;
  counters.active_alpha_test_greater_equal_material_projections = 1U;
  counters.active_metallic_roughness_workflow_projections = 2U;
  counters.active_specular_workflow_projections = 1U;
  counters.active_anisotropic_sampler_projections = 2U;
  counters.active_normalized_texture_observations = 3U;
  counters.active_opaque_texture_normalizations = 1U;
  counters.active_straight_alpha_texture_normalizations = 1U;
  counters.active_linear_specular_texture_normalizations = 1U;

  OgreNextDemoTextureSourceCounters committed;
  Require(
      AccumulateOgreNextDemoTextureSourceCounters(counters, committed).ok() &&
          committed.authenticated_source_decodes == 29U &&
          committed.ordinary_observed_source_decodes == 3U &&
          committed.gpu_readbacks == 0U,
      "valid source-byte counters did not commit atomically");
  OgreNextDemoTextureSourceCounters hostile_increment;
  hostile_increment.gpu_readbacks = 1U;
  const OgreNextDemoTextureSourceCounters before = committed;
  Require(
      !AccumulateOgreNextDemoTextureSourceCounters(hostile_increment, committed)
              .ok() &&
          committed.authenticated_source_decodes ==
              before.authenticated_source_decodes &&
          committed.ordinary_observed_source_decodes ==
              before.ordinary_observed_source_decodes &&
          committed.gpu_readbacks == 0U,
      "GPU-readback accounting bypassed the zero gate or partially committed");

  OgreNextDemoTextureSourceCounters broken_denominator = counters;
  --broken_denominator.candidate_sections;
  const OgreNextDemoTextureSourceCounters denominator_before = committed;
  Require(!AccumulateOgreNextDemoTextureSourceCounters(broken_denominator,
                                                       committed)
                  .ok() &&
              committed.candidate_sections ==
                  denominator_before.candidate_sections &&
              committed.matte_excluded_sections ==
                  denominator_before.matte_excluded_sections,
          "corrupt candidate/projected/matte equation partially committed");

  OgreNextDemoTextureSourceCounters broken_blend_partition = counters;
  ++broken_blend_partition.active_replace_material_projections;
  Require(!AccumulateOgreNextDemoTextureSourceCounters(
               broken_blend_partition, committed)
               .ok(),
          "corrupt active blend denominator was accepted");
  OgreNextDemoTextureSourceCounters broken_normalization_partition = counters;
  --broken_normalization_partition.active_linear_specular_texture_normalizations;
  Require(!AccumulateOgreNextDemoTextureSourceCounters(
               broken_normalization_partition, committed)
               .ok(),
          "normalized texture disappeared from the exact policy partition");
}

void CheckExactSamplerMappingAndFingerprint() {
  OgreNextDemoExactSamplerObservation observation;
  observation.minification_filter = OgreNextDemoObservedSamplerFilter::POINT;
  observation.magnification_filter = OgreNextDemoObservedSamplerFilter::LINEAR;
  observation.mip_filter = OgreNextDemoObservedSamplerFilter::POINT;
  observation.address_u = OgreNextDemoObservedSamplerAddressMode::WRAP;
  observation.address_v = OgreNextDemoObservedSamplerAddressMode::MIRROR;
  observation.address_w = OgreNextDemoObservedSamplerAddressMode::CLAMP;
  observation.compare_function_token = 7U;
  observation.border_color = {0.0F, 0.25F, 0.5F, 1.0F};

  SamplerResourceDescriptor descriptor;
  Require(
      BuildOgreNextDemoSamplerDescriptor(observation, 4U, "exact", descriptor)
              .ok() &&
          descriptor.minification_filter == SamplerFilter::NEAREST &&
          descriptor.magnification_filter == SamplerFilter::LINEAR &&
          descriptor.mip_filter == SamplerFilter::NEAREST &&
          descriptor.address_u == SamplerAddressMode::REPEAT &&
          descriptor.address_v == SamplerAddressMode::MIRRORED_REPEAT &&
          descriptor.address_w == SamplerAddressMode::CLAMP_TO_EDGE &&
          descriptor.mip_lod_bias == 0.0F && descriptor.minimum_lod == 0.0F &&
          descriptor.maximum_lod == 3.0F && !descriptor.anisotropy_enabled &&
          descriptor.maximum_anisotropy == 1.0F &&
          !descriptor.compare_enabled &&
          descriptor.compare_operation == SamplerCompareOperation::ALWAYS &&
          descriptor.border_color == Float4{0.0F, 0.25F, 0.5F, 1.0F},
      "exact supported sampler did not map without approximation");
  Require(MatchOgreNextDemoExactSamplerObservation(observation, observation),
          "identical exact sampler observations did not fingerprint equally");

  OgreNextDemoExactSamplerObservation anisotropic = observation;
  anisotropic.minification_filter =
      OgreNextDemoObservedSamplerFilter::ANISOTROPIC;
  anisotropic.magnification_filter =
      OgreNextDemoObservedSamplerFilter::ANISOTROPIC;
  anisotropic.mip_filter = OgreNextDemoObservedSamplerFilter::LINEAR;
  anisotropic.maximum_anisotropy = 8U;
  SamplerResourceDescriptor anisotropic_descriptor;
  Require(BuildOgreNextDemoSamplerDescriptor(anisotropic, 4U, "anisotropic",
                                              anisotropic_descriptor)
                  .ok() &&
              anisotropic_descriptor.minification_filter ==
                  SamplerFilter::LINEAR &&
              anisotropic_descriptor.magnification_filter ==
                  SamplerFilter::LINEAR &&
              anisotropic_descriptor.mip_filter == SamplerFilter::LINEAR &&
              anisotropic_descriptor.anisotropy_enabled &&
              anisotropic_descriptor.maximum_anisotropy == 8.0F,
          "exact authored anisotropic sampler was excluded or approximated");
  OgreNextDemoExactSamplerObservation anisotropic_mutation = anisotropic;
  anisotropic_mutation.maximum_anisotropy = 16U;
  Require(!MatchOgreNextDemoExactSamplerObservation(anisotropic,
                                                    anisotropic_mutation),
          "anisotropy mutation escaped exact sampler fingerprint");

  const auto reject = [&observation](
                          OgreNextDemoExactSamplerObservation mutation,
                          const char *expected_field) {
    SamplerResourceDescriptor sentinel;
    sentinel.debug_name = "unchanged";
    const ValidationResult result =
        BuildOgreNextDemoSamplerDescriptor(mutation, 4U, "rejected", sentinel);
    Require(
        !result.ok() && result.field == expected_field &&
            sentinel.debug_name == "unchanged" &&
            !MatchOgreNextDemoExactSamplerObservation(observation, mutation),
        "unsupported sampler state was approximated or partially committed");
  };
  OgreNextDemoExactSamplerObservation mutation = observation;
  mutation.minification_filter = OgreNextDemoObservedSamplerFilter::UNSUPPORTED;
  reject(mutation, "ogre_next_demo.material.sampler.filter");
  mutation = observation;
  mutation.magnification_filter =
      OgreNextDemoObservedSamplerFilter::UNSUPPORTED;
  reject(mutation, "ogre_next_demo.material.sampler.filter");
  mutation = observation;
  mutation.mip_filter = OgreNextDemoObservedSamplerFilter::UNSUPPORTED;
  reject(mutation, "ogre_next_demo.material.sampler.filter");
  mutation = observation;
  mutation.address_u = OgreNextDemoObservedSamplerAddressMode::UNSUPPORTED;
  reject(mutation, "ogre_next_demo.material.sampler.address");
  mutation = observation;
  mutation.mip_lod_bias = 0.25F;
  reject(mutation, "ogre_next_demo.material.sampler.mip_lod_bias");
  mutation = observation;
  mutation.maximum_anisotropy = 2U;
  reject(mutation, "ogre_next_demo.material.sampler.anisotropy");
  mutation = observation;
  mutation.compare_enabled = true;
  reject(mutation, "ogre_next_demo.material.sampler.compare");

  mutation = observation;
  mutation.compare_function_token = 6U;
  Require(!MatchOgreNextDemoExactSamplerObservation(observation, mutation),
          "dormant exact compare function escaped sampler fingerprint");
  mutation = observation;
  mutation.border_color[2U] = 0.75F;
  Require(!MatchOgreNextDemoExactSamplerObservation(observation, mutation),
          "exact border color escaped sampler fingerprint");
}

void CheckExactTextureFingerprint() {
  OgreNextDemoExactTextureObservation observation;
  observation.texture_unit_gamma = 1.25F;
  observation.texture_gamma = 0.75F;
  observation.texture_unit_hardware_gamma = false;
  observation.texture_hardware_gamma = true;
  observation.additional_mip_count = 2U;
  observation.actual_mip_count = 3U;
  observation.mipmaps_hardware_generated = true;
  observation.usage_token = 17U;
  observation.source_width = 8U;
  observation.source_height = 4U;
  observation.source_depth = 1U;
  observation.source_format_token = 21U;
  observation.output_width = 8U;
  observation.output_height = 4U;
  observation.output_depth = 1U;
  observation.output_format_token = 22U;
  observation.face_count = 1U;
  observation.texture_type_token = 2U;
  Require(
      ValidateOgreNextDemoExactTextureObservation(observation).ok() &&
          MatchOgreNextDemoExactTextureObservation(observation, observation),
      "valid exact gamma/mip/source/output observation was rejected");

  const auto require_mutation = [&observation](auto mutate) {
    OgreNextDemoExactTextureObservation changed = observation;
    mutate(changed);
    Require(!MatchOgreNextDemoExactTextureObservation(observation, changed),
            "native gamma/mip/source/output mutation escaped fingerprint");
  };
  require_mutation([](auto &value) { value.texture_unit_gamma = 1.0F; });
  require_mutation([](auto &value) { value.texture_gamma = 1.0F; });
  require_mutation([](auto &value) { value.texture_hardware_gamma = false; });
  require_mutation([](auto &value) { value.additional_mip_count = 3U; });
  require_mutation(
      [](auto &value) { value.mipmaps_hardware_generated = false; });
  require_mutation([](auto &value) { value.usage_token ^= 4U; });
  require_mutation([](auto &value) { value.source_width = 16U; });
  require_mutation([](auto &value) { value.source_format_token = 23U; });
  require_mutation([](auto &value) { value.output_height = 8U; });
  require_mutation([](auto &value) { value.output_format_token = 24U; });
  require_mutation([](auto &value) { value.face_count = 2U; });
  require_mutation([](auto &value) { value.texture_type_token = 3U; });

  OgreNextDemoExactTextureObservation invalid = observation;
  invalid.actual_mip_count = 2U;
  Require(!ValidateOgreNextDemoExactTextureObservation(invalid).ok(),
          "inconsistent actual/additional native mip counts were accepted");
  invalid = observation;
  invalid.texture_gamma = (std::numeric_limits<float>::quiet_NaN)();
  Require(!ValidateOgreNextDemoExactTextureObservation(invalid).ok(),
          "nonfinite native texture gamma was accepted");
}

struct FrozenPublicationCatalog final {
  std::vector<OgreNextDemoCachedProjectionPublicationInput> projections;
  std::vector<OgreNextDemoCachedTexturePublicationInput> textures;
  std::vector<OgreNextDemoCachedSamplerPublicationInput> samplers;
};

std::string
PublicationCatalogFingerprint(const FrozenPublicationCatalog &catalog) {
  std::string fingerprint;
  const auto append = [&fingerprint](std::string_view value) {
    fingerprint.append(value.data(), value.size());
    fingerprint.push_back('\0');
  };
  for (const auto &projection : catalog.projections) {
    append(projection.projection_key);
    append(projection.texture_key);
    append(projection.sampler_key);
    append(std::to_string(projection.material_source_id));
  }
  for (const auto &texture : catalog.textures) {
    append(texture.texture_key);
    append(std::to_string(texture.texture_source_id));
    append(std::to_string(static_cast<std::uint64_t>(texture.source_mode)));
  }
  for (const auto &sampler : catalog.samplers) {
    append(sampler.sampler_key);
    append(std::to_string(sampler.sampler_source_id));
  }
  return fingerprint;
}

FrozenPublicationCatalog RepresentativePublicationCatalog() {
  FrozenPublicationCatalog catalog;
  catalog.textures.push_back(
      {"texture/authenticated", 101U,
       OgreNextDemoTextureSourceMode::AUTHENTICATED_ARCHIVE_SOURCE_BYTES});
  catalog.textures.push_back(
      {"texture/ordinary", 102U,
       OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES});
  catalog.samplers.push_back({"sampler/authenticated", 201U});
  catalog.samplers.push_back({"sampler/ordinary", 202U});
  catalog.projections.push_back({"projection/authenticated",
                                 "texture/authenticated",
                                 "sampler/authenticated", 301U});
  catalog.projections.push_back(
      {"projection/ordinary", "texture/ordinary", "sampler/ordinary", 302U});
  return catalog;
}

class FakePublicationBatchValidator final
    : public IOgreNextDemoTexturePublicationBatchValidator {
public:
  ValidationResult ValidateReachableAuthenticatedTextureBatch(
      const std::vector<std::string> &texture_keys) override {
    ++batch_calls;
    observed_texture_keys = texture_keys;
    return authenticated_result;
  }

  ValidationResult ValidateReachableOrdinaryTextureBatch(
      const std::vector<std::string> &texture_keys) override {
    ++ordinary_batch_calls;
    observed_ordinary_texture_keys = texture_keys;
    return ordinary_result;
  }

  ValidationResult authenticated_result = ValidationResult::Success();
  ValidationResult ordinary_result = ValidationResult::Success();
  std::size_t batch_calls = 0U;
  std::size_t ordinary_batch_calls = 0U;
  std::size_t gpu_readback_calls = 0U;
  std::vector<std::string> observed_texture_keys;
  std::vector<std::string> observed_ordinary_texture_keys;
};

OgreNextDemoCachedProjectionPublicationTransaction SentinelTransaction() {
  OgreNextDemoCachedProjectionPublicationTransaction sentinel;
  sentinel.owner_catalog.push_back({"sentinel", 9991U, 9992U, 9993U, true});
  sentinel.frame_root_material_source_ids.push_back(9991U);
  sentinel.authenticated_texture_keys.push_back("sentinel/texture");
  sentinel.ordinary_texture_keys.push_back("sentinel/ordinary");
  return sentinel;
}

bool IsSentinelTransaction(
    const OgreNextDemoCachedProjectionPublicationTransaction &transaction) {
  return transaction.owner_catalog.size() == 1U &&
         transaction.owner_catalog.front().projection_key == "sentinel" &&
         transaction.owner_catalog.front().material_source_id == 9991U &&
         transaction.owner_catalog.front().texture_source_id == 9992U &&
         transaction.owner_catalog.front().sampler_source_id == 9993U &&
         transaction.owner_catalog.front().frame_reachable &&
         transaction.frame_root_material_source_ids ==
             std::vector<std::uint64_t>{9991U} &&
         transaction.authenticated_texture_keys ==
             std::vector<std::string>{"sentinel/texture"} &&
         transaction.ordinary_texture_keys ==
             std::vector<std::string>{"sentinel/ordinary"};
}

void CheckCachedPublicationTransactionSequence() {
  const auto committed = std::make_shared<const FrozenPublicationCatalog>(
      RepresentativePublicationCatalog());
  const std::string frozen_fingerprint =
      PublicationCatalogFingerprint(*committed);

  // Frame N roots both source classes while Apply publishes the complete
  // cached P/T/S owner catalog.
  FakePublicationBatchValidator frame_n_validator;
  OgreNextDemoCachedProjectionPublicationTransaction frame_n;
  Require(BuildOgreNextDemoCachedProjectionPublicationTransaction(
              committed->projections, committed->textures, committed->samplers,
              {"projection/authenticated", "projection/ordinary"},
              frame_n_validator, frame_n)
              .ok(),
          "frame N cached publication transaction failed");
  Require(
      frame_n.owner_catalog.size() == 2U &&
          frame_n.owner_catalog[0U].frame_reachable &&
          frame_n.owner_catalog[1U].frame_reachable &&
          frame_n.frame_root_material_source_ids ==
              std::vector<std::uint64_t>({301U, 302U}) &&
          frame_n.authenticated_texture_keys ==
              std::vector<std::string>{"texture/authenticated"} &&
          frame_n.ordinary_texture_keys ==
              std::vector<std::string>{"texture/ordinary"} &&
          frame_n_validator.batch_calls == 1U &&
          frame_n_validator.ordinary_batch_calls == 1U &&
          frame_n_validator.observed_texture_keys ==
              frame_n.authenticated_texture_keys &&
          frame_n_validator.observed_ordinary_texture_keys ==
              frame_n.ordinary_texture_keys &&
          frame_n_validator.gpu_readback_calls == 0U,
      "frame N did not validate distinct authenticated and ordinary sources");

  // Frame N+1 represents same-map bundle unload after every instance root was
  // removed. BeginCapture retains the same immutable COW catalog; Apply emits
  // byte-identical P/T/S owners but no rooted closure or authority callback.
  const auto pending_unreachable = committed;
  FakePublicationBatchValidator frame_n_plus_1_validator;
  OgreNextDemoCachedProjectionPublicationTransaction frame_n_plus_1;
  Require(BuildOgreNextDemoCachedProjectionPublicationTransaction(
              pending_unreachable->projections, pending_unreachable->textures,
              pending_unreachable->samplers, {}, frame_n_plus_1_validator,
              frame_n_plus_1)
              .ok(),
          "frame N+1 unreachable cached publication failed");
  Require(committed.get() == pending_unreachable.get() &&
              committed.use_count() == 2U &&
              PublicationCatalogFingerprint(*committed) == frozen_fingerprint &&
              frame_n_plus_1.owner_catalog.size() == 2U &&
              !frame_n_plus_1.owner_catalog[0U].frame_reachable &&
              !frame_n_plus_1.owner_catalog[1U].frame_reachable &&
              frame_n_plus_1.owner_catalog[0U].material_source_id == 301U &&
              frame_n_plus_1.owner_catalog[0U].texture_source_id == 101U &&
              frame_n_plus_1.owner_catalog[0U].sampler_source_id == 201U &&
              frame_n_plus_1.frame_root_material_source_ids.empty() &&
              frame_n_plus_1.authenticated_texture_keys.empty() &&
              frame_n_plus_1.ordinary_texture_keys.empty() &&
              frame_n_plus_1_validator.batch_calls == 0U &&
              frame_n_plus_1_validator.ordinary_batch_calls == 0U &&
              frame_n_plus_1_validator.gpu_readback_calls == 0U,
          "frame N+1 changed the COW owner catalog or rooted/probed an "
          "unreachable texture");

  // Frame N+2 unchanged reuse must retain stable IDs and perform one fresh
  // batch-authority validation before the transaction commits.
  FakePublicationBatchValidator frame_n_plus_2_validator;
  OgreNextDemoCachedProjectionPublicationTransaction frame_n_plus_2;
  Require(BuildOgreNextDemoCachedProjectionPublicationTransaction(
              committed->projections, committed->textures, committed->samplers,
              {"projection/ordinary"}, frame_n_plus_2_validator, frame_n_plus_2)
                  .ok() &&
              frame_n_plus_2.owner_catalog[1U].material_source_id == 302U &&
              frame_n_plus_2.owner_catalog[1U].texture_source_id == 102U &&
              frame_n_plus_2.owner_catalog[1U].sampler_source_id == 202U &&
              frame_n_plus_2_validator.batch_calls == 0U &&
              frame_n_plus_2_validator.ordinary_batch_calls == 1U &&
              frame_n_plus_2_validator.gpu_readback_calls == 0U,
          "frame N+2 ordinary reuse changed IDs or skipped fresh authority");

  const auto require_authority_rollback = [&](ValidationCode code,
                                              const char *field,
                                              const char *detail) {
    FakePublicationBatchValidator validator;
    validator.authenticated_result =
        ValidationResult::Failure(code, field, detail);
    OgreNextDemoCachedProjectionPublicationTransaction output =
        SentinelTransaction();
    const ValidationResult result =
        BuildOgreNextDemoCachedProjectionPublicationTransaction(
            committed->projections, committed->textures, committed->samplers,
            {"projection/authenticated"}, validator, output);
    Require(
        !result.ok() && IsSentinelTransaction(output) &&
            PublicationCatalogFingerprint(*committed) == frozen_fingerprint &&
            validator.batch_calls == 1U &&
            validator.ordinary_batch_calls == 0U &&
            validator.gpu_readback_calls == 0U,
        "revoked, mutated, or missing authority partially committed or read "
        "back");
  };
  require_authority_rollback(ValidationCode::MISSING_REFERENCE,
                             "missing_receipt",
                             "reloaded texture has no exact receipt");
  require_authority_rollback(
      ValidationCode::REVISION_MISMATCH, "mutated_receipt",
      "reloaded texture receipt has different immutable state");
  require_authority_rollback(
      ValidationCode::REVISION_MISMATCH, "revoked_authority",
      "authenticated group authority was revoked before reuse");

  FakePublicationBatchValidator ordinary_rollback_validator;
  ordinary_rollback_validator.ordinary_result = ValidationResult::Failure(
      ValidationCode::REVISION_MISMATCH, "mutated_selected_source",
      "ordinary selected source changed before reuse");
  OgreNextDemoCachedProjectionPublicationTransaction ordinary_rollback_output =
      SentinelTransaction();
  Require(!BuildOgreNextDemoCachedProjectionPublicationTransaction(
               committed->projections, committed->textures, committed->samplers,
               {"projection/ordinary"}, ordinary_rollback_validator,
               ordinary_rollback_output)
                  .ok() &&
              IsSentinelTransaction(ordinary_rollback_output) &&
              ordinary_rollback_validator.batch_calls == 0U &&
              ordinary_rollback_validator.ordinary_batch_calls == 1U &&
              ordinary_rollback_validator.gpu_readback_calls == 0U,
          "ordinary authority failure partially committed or read back");

  FrozenPublicationCatalog missing_dependency = *committed;
  missing_dependency.samplers.pop_back();
  FakePublicationBatchValidator dependency_validator;
  OgreNextDemoCachedProjectionPublicationTransaction dependency_output =
      SentinelTransaction();
  Require(!BuildOgreNextDemoCachedProjectionPublicationTransaction(
               missing_dependency.projections, missing_dependency.textures,
               missing_dependency.samplers, {"projection/authenticated"},
               dependency_validator, dependency_output)
                  .ok() &&
              IsSentinelTransaction(dependency_output) &&
              dependency_validator.batch_calls == 0U &&
              dependency_validator.ordinary_batch_calls == 0U,
          "missing cached dependency partially committed publication output");

  FrozenPublicationCatalog duplicate = *committed;
  duplicate.projections.push_back(duplicate.projections.front());
  FakePublicationBatchValidator duplicate_validator;
  OgreNextDemoCachedProjectionPublicationTransaction duplicate_output =
      SentinelTransaction();
  Require(!BuildOgreNextDemoCachedProjectionPublicationTransaction(
               duplicate.projections, duplicate.textures, duplicate.samplers,
               {"projection/authenticated"}, duplicate_validator,
               duplicate_output)
                  .ok() &&
              IsSentinelTransaction(duplicate_output) &&
              duplicate_validator.batch_calls == 0U &&
              duplicate_validator.ordinary_batch_calls == 0U &&
              PublicationCatalogFingerprint(*committed) == frozen_fingerprint,
          "duplicate cached projection partially committed or mutated the "
          "frozen cache");
}

void CheckSamplingRejectionsAndMutation() {
  OgreNextDemoSamplingObservation canonical;
  canonical.exact_native_state = "stable-native-state";
  Require(ValidateOgreNextDemoSampling(canonical).ok(),
          "canonical sampling was rejected");

  const auto reject = [&canonical](bool OgreNextDemoSamplingObservation::*field,
                                   const char *expected_field) {
    OgreNextDemoSamplingObservation mutation = canonical;
    mutation.*field = false;
    const ValidationResult result = ValidateOgreNextDemoSampling(mutation);
    Require(!result.ok() && result.field == expected_field,
            "sampling mutation did not hit its exact rejection field");
  };
  reject(&OgreNextDemoSamplingObservation::ordinary_texture,
         "ogre_next_demo.terrain.sampling.ordinary");
  reject(&OgreNextDemoSamplingObservation::uv0_identity,
         "ogre_next_demo.terrain.sampling.uv");
  reject(&OgreNextDemoSamplingObservation::sampler_identity,
         "ogre_next_demo.terrain.sampling.sampler");
  reject(&OgreNextDemoSamplingObservation::gamma_disabled,
         "ogre_next_demo.terrain.sampling.gamma");
  reject(&OgreNextDemoSamplingObservation::fog_disabled,
         "ogre_next_demo.terrain.sampling.fog");

  Require(RevalidateOgreNextDemoSampling(canonical, canonical).ok(),
          "identical before/after sampling state was rejected");
  OgreNextDemoSamplingObservation after = canonical;
  after.exact_native_state.push_back('!');
  const ValidationResult mutated =
      RevalidateOgreNextDemoSampling(canonical, after);
  Require(!mutated.ok() && mutated.code == ValidationCode::REVISION_MISMATCH &&
              mutated.field == "ogre_next_demo.terrain.readback.revalidation",
          "before/after native state mutation was accepted");
}

void CheckIdentityCollisionAndRollback() {
  std::string mesh_key("demo/mesh");
  mesh_key.push_back('\0');
  mesh_key.append("page/0/0");
  std::string texture_key("demo/texture");
  texture_key.push_back('\0');
  texture_key.append("page/0/0");
  std::uint64_t mesh_id = 0U;
  std::uint64_t texture_id = 0U;
  Require(DeriveOgreNextDemoSourceId("demo/mesh", "page/0/0", mesh_id).ok() &&
              DeriveOgreNextDemoSourceId("demo/texture", "page/0/0", texture_id)
                  .ok() &&
              mesh_id != texture_id,
          "domain separation did not produce distinct stable IDs");

  OgreNextDemoIdentityRegistry committed;
  Require(committed.Register(mesh_key, mesh_id).ok(),
          "canonical identity registration failed");
  OgreNextDemoIdentityRegistry pending = committed;
  Require(pending.Register(texture_key, texture_id).ok(),
          "pending identity registration failed");
  pending = committed; // discard candidate
  Require(committed.size() == 1U && pending.size() == 1U &&
              !committed.Contains(texture_key, texture_id),
          "discarded identity leaked into committed state");

  const ValidationResult id_collision =
      committed.Register("different-key", mesh_id);
  Require(!id_collision.ok() &&
              id_collision.code == ValidationCode::DUPLICATE_IDENTIFIER,
          "distinct exact keys sharing one ID were accepted");
  const ValidationResult key_mutation =
      committed.Register(mesh_key, texture_id);
  Require(!key_mutation.ok() &&
              key_mutation.code == ValidationCode::REVISION_MISMATCH,
          "one exact key changing ID was accepted");
}

void CheckStaticCaptureAdmission() {
  float radius = -1.0F;
  const float vertical_half_extent = 0.28867513F;
  Require(BuildOgreNextDemoStaticCaptureRadius(
              -0.51320022F, 0.51320022F, vertical_half_extent,
              -vertical_half_extent, 0.5F, 350.0F, 16.0F / 9.0F, radius)
                  .ok() &&
              radius > 539.0F && radius < 542.0F,
          "normalized 16:9 far-frustum capture radius changed");
  float ultrawide_radius = -1.0F;
  Require(BuildOgreNextDemoStaticCaptureRadius(
              -0.51320022F, 0.51320022F, vertical_half_extent,
              -vertical_half_extent, 0.5F, 350.0F, 32.0F / 9.0F,
              ultrawide_radius)
                  .ok() &&
              ultrawide_radius > radius,
          "ultrawide child aspect did not expand static admission");

  Bounds3 touching;
  touching.minimum = {radius - 1.0F, -1.0F, -1.0F};
  touching.maximum = {radius + 1.0F, 1.0F, 1.0F};
  bool retained = false;
  Require(
      ClassifyOgreNextDemoStaticBounds(touching, {}, radius, retained).ok() &&
          retained,
      "AABB touching the demo capture sphere was omitted");
  Bounds3 outside = touching;
  outside.minimum.x = radius + 1.0F;
  outside.maximum.x = radius + 2.0F;
  retained = true;
  Require(
      ClassifyOgreNextDemoStaticBounds(outside, {}, radius, retained).ok() &&
          !retained,
      "AABB outside the demo capture sphere was retained");

  float sentinel_radius = 17.0F;
  Require(!BuildOgreNextDemoStaticCaptureRadius(1.0F, -1.0F, 1.0F, -1.0F, 0.5F,
                                                350.0F, 16.0F / 9.0F,
                                                sentinel_radius)
                  .ok() &&
              sentinel_radius == 17.0F,
          "invalid frustum partially published a capture radius");
  Bounds3 invalid = touching;
  invalid.minimum.x = (std::numeric_limits<float>::quiet_NaN)();
  retained = true;
  Require(
      !ClassifyOgreNextDemoStaticBounds(invalid, {}, radius, retained).ok() &&
          retained,
      "invalid AABB partially published its admission result");
}

void CheckMatteFallbackPolicy() {
  Require(!OgreNextDemoRequiresMatte(0U, false),
          "factor-only material was unnecessarily matted");
  Require(OgreNextDemoRequiresMatte(1U, false) &&
              OgreNextDemoRequiresMatte(0U, true),
          "textured or programmed material bypassed the matte fallback");
  Require(OgreNextDemoDropsDynamicBlendColors(true) &&
              !OgreNextDemoDropsDynamicBlendColors(false),
          "FlexBody blend-color drop policy changed");
  Require(OgreNextDemoOmitsInvisibleCab("invisible", 0.0F, false) &&
              !OgreNextDemoOmitsInvisibleCab("invisible", 1.0F, false) &&
              !OgreNextDemoOmitsInvisibleCab("invisible", 0.0F, true) &&
              !OgreNextDemoOmitsInvisibleCab("Invisible", 0.0F, false),
          "authored invisible cab omission is no longer exact");
  Require(
      OgreNextDemoOmitsNonUniformSpeedBump("topeQr.mesh", {1.0F, 0.5F, 0.5F}) &&
          !OgreNextDemoOmitsNonUniformSpeedBump("topeQr.mesh",
                                                {1.0F, 1.0F, 1.0F}) &&
          !OgreNextDemoOmitsNonUniformSpeedBump("other.mesh",
                                                {1.0F, 0.5F, 0.5F}),
      "CityWorld speed-bump omission broadened beyond its exact identity");
  constexpr std::array<std::pair<std::string_view, bool>, 7U>
      kAlexisAuthoredSpecularScope{{
          {"SaberChassis (AlexisSaber.truck [Instance ID 17])", true},
          {"SaberChassisM (AlexisSaber.truck [Instance ID 18])", true},
          {"SaberWheels (AlexisSaber.truck [Instance ID 19])", true},
          {"SaberGrilles (AlexisSaber.truck [Instance ID 0])", true},
          {"SaberLens (AlexisSaber.truck [Instance ID 20])", false},
          {"SaberWinds (AlexisSaber.truck [Instance ID 21])", false},
          {"SaberWinds_int (AlexisSaber.truck [Instance ID 22])", false},
      }};
  std::size_t alexis_projection_count = 0U;
  for (const auto &[name, expected] : kAlexisAuthoredSpecularScope) {
    const bool admitted = OgreNextDemoAllowsAlexisTUS0Approximation(
        "{bundle USER:/mods/AlexisSaber.zip}", name);
    Require(admitted == expected,
            "Alexis authored specular 4/7 scope changed");
    alexis_projection_count += admitted ? 1U : 0U;
  }
  Require(alexis_projection_count == 4U &&
              !OgreNextDemoAllowsAlexisTUS0Approximation(
                  "{bundle USER:/mods/AlexisSaber.zip}", "SaberLens") &&
              !OgreNextDemoAllowsAlexisTUS0Approximation(
                  "{bundle USER:/mods/AlexisSaber.zip}", "SaberBody") &&
              !OgreNextDemoAllowsAlexisTUS0Approximation(
                  "{bundle USER:/mods/AlexisSaber.zip}",
                  "SaberChassis (Other.truck [Instance ID 17])") &&
              !OgreNextDemoAllowsAlexisTUS0Approximation(
                  "{bundle USER:/mods/AlexisSaber.zip}",
                  "SaberChassis (AlexisSaber.truck [Instance ID x])") &&
              !OgreNextDemoAllowsAlexisTUS0Approximation(
                  "OtherGroup",
                  "SaberChassis (AlexisSaber.truck [Instance ID 17])"),
          "Alexis opaque TUS0 approximation escaped its exact content scope");
}

void CheckMatteTintPolicy() {
  const OgreNextDemoMatteTint white = OgreNextDemoResolveMatteTint(
      1.0F, 1.0F, 1.0F);
  Require(!white.tinted && white.red == 1.0F && white.green == 1.0F &&
              white.blue == 1.0F,
          "the OGRE default white diffuse no longer keeps the neutral matte");
  const OgreNextDemoMatteTint nearly_white =
      OgreNextDemoResolveMatteTint(0.999F, 0.999F, 0.999F);
  Require(!nearly_white.tinted,
          "a diffuse that quantizes to white left the neutral matte");
  for (const float bad : {-0.01F, 1.01F,
                          (std::numeric_limits<float>::quiet_NaN)(),
                          (std::numeric_limits<float>::infinity)()}) {
    Require(!OgreNextDemoResolveMatteTint(bad, 0.5F, 0.5F).tinted &&
                !OgreNextDemoResolveMatteTint(0.5F, bad, 0.5F).tinted &&
                !OgreNextDemoResolveMatteTint(0.5F, 0.5F, bad).tinted,
            "an unrepresentable authored diffuse was not failed closed");
  }
  const OgreNextDemoMatteTint dark = OgreNextDemoResolveMatteTint(
      0.1F, 0.1F, 0.1F);
  Require(dark.tinted && dark.red == dark.green && dark.green == dark.blue &&
              dark.red > 0.0F && dark.red < 0.2F,
          "an authored dark foliage diffuse lost its matte tint");
  const OgreNextDemoMatteTint green = OgreNextDemoResolveMatteTint(
      0.0785F, 0.4F, 0.0785F);
  Require(green.tinted && green.green > green.red &&
              green.red == green.blue,
          "an authored chromatic diffuse lost its matte hue");
  Require(green.token != dark.token && green.token != white.token,
          "distinct matte tints collapsed onto one material identity");
  Require(green.token < kOgreNextDemoMatteTintTokenCount &&
              dark.token < kOgreNextDemoMatteTintTokenCount,
          "a matte tint token escaped its declared bound");
  // The token must stay a pure function of the emitted factors so one matte
  // material key can never describe two different colours.
  const OgreNextDemoMatteTint replayed =
      OgreNextDemoResolveMatteTint(green.red, green.green, green.blue);
  Require(replayed.tinted && replayed.token == green.token &&
              replayed.red == green.red && replayed.green == green.green &&
              replayed.blue == green.blue,
          "matte tint quantization is not idempotent");
}

void CheckMatteMeshNormalization() {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "demo matte triangle";
  mesh.dynamic = true;
  mesh.local_bounds.minimum = {-1.0F, 0.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {
      {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  mesh.normals = {{0.0F, 0.0F, 2.0F},
                  {0.0F, 0.0F, 0.0F},
                  {(std::numeric_limits<float>::quiet_NaN)(), 0.0F, 0.0F}};
  mesh.velocities.assign(3U, {2.0F, 3.0F, 4.0F});
  mesh.texture_coordinates_1.assign(3U, {0.25F, 0.75F});
  mesh.colors.assign(3U, {0.2F, 0.4F, 0.6F, 0.8F});
  mesh.indices = {0U, 1U, 2U};

  Require(NormalizeOgreNextDemoMatteMesh(mesh).ok(),
          "valid matte mesh normalization failed");
  Require(mesh.texture_coordinates_0.size() == 3U &&
              std::all_of(mesh.texture_coordinates_0.begin(),
                          mesh.texture_coordinates_0.end(),
                          [](const Float2 &uv) {
                            return uv.x == 0.0F && uv.y == 0.0F;
                          }),
          "missing matte UV0 was not deterministically synthesized");
  Require(mesh.normals.size() == 3U &&
              mesh.normals[0U] == Float3{0.0F, 0.0F, 1.0F} &&
              mesh.normals[1U] == Float3{0.0F, 1.0F, 0.0F} &&
              mesh.normals[2U] == Float3{0.0F, 1.0F, 0.0F},
          "matte normals were not normalized/fallback-sanitized");
  Require(mesh.tangents.size() == 3U &&
              mesh.tangents[0U] == Float4{1.0F, 0.0F, 0.0F, 1.0F} &&
              mesh.tangents[1U] == Float4{-1.0F, 0.0F, 0.0F, 1.0F} &&
              mesh.tangents[2U] == Float4{-1.0F, 0.0F, 0.0F, 1.0F},
          "matte tangent basis was not rebuilt from sanitized normals");
  Require(mesh.velocities.empty() && mesh.texture_coordinates_1.empty() &&
              mesh.colors.empty() && ValidateMeshResourceDescriptor(mesh).ok(),
          "matte normalization retained an unsupported RT4 stream");

  std::vector<Float3> dynamic_normals{
      {0.0F, 3.0F, 0.0F},
      {(std::numeric_limits<float>::infinity)(), 1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F}};
  std::vector<Float4> dynamic_tangents{{9.0F, 9.0F, 9.0F, 9.0F}};
  Require(
      BuildOgreNextDemoMatteTangents(3U, dynamic_normals, dynamic_tangents)
              .ok() &&
          dynamic_normals == std::vector<Float3>(3U, {0.0F, 1.0F, 0.0F}) &&
          dynamic_tangents ==
              std::vector<Float4>(3U, {-1.0F, 0.0F, 0.0F, 1.0F}),
      "joined dynamic normals/tangents were not sanitized deterministically");

  std::vector<Float3> missing_normals;
  std::vector<Float4> missing_tangents{{7.0F, 7.0F, 7.0F, 7.0F}};
  Require(BuildOgreNextDemoMatteTangents(2U, missing_normals, missing_tangents)
                  .ok() &&
              missing_normals == std::vector<Float3>(2U, {0.0F, 1.0F, 0.0F}) &&
              missing_tangents ==
                  std::vector<Float4>(2U, {-1.0F, 0.0F, 0.0F, 1.0F}),
          "absent demo normal stream did not receive the fixed fallback");

  std::vector<Float3> wrong_size_normals{{0.0F, 2.0F, 0.0F}};
  std::vector<Float4> wrong_size_tangents{{9.0F, 9.0F, 9.0F, 9.0F}};
  const std::vector<Float3> normals_before = wrong_size_normals;
  const std::vector<Float4> tangents_before = wrong_size_tangents;
  const ValidationResult invalid = BuildOgreNextDemoMatteTangents(
      2U, wrong_size_normals, wrong_size_tangents);
  Require(!invalid.ok() && wrong_size_normals == normals_before &&
              wrong_size_tangents == tangents_before,
          "failed dynamic normal sanitization changed either output stream");

  MeshResourceDescriptor rollback = mesh;
  rollback.normals[0U] = {0.0F, 4.0F, 0.0F};
  rollback.texture_coordinates_0.pop_back();
  const std::vector<Float3> rollback_normals = rollback.normals;
  const std::vector<Float4> rollback_tangents = rollback.tangents;
  const ValidationResult rollback_result =
      NormalizeOgreNextDemoMatteMesh(rollback);
  Require(!rollback_result.ok() && rollback.normals == rollback_normals &&
              rollback.tangents == rollback_tangents &&
              rollback.texture_coordinates_0.size() == 2U,
          "post-validation failure partially committed sanitized mesh streams");
}

} // namespace

int main() {
  CheckCuratedCityWorldAsiaPolicy();
  CheckFullMipOpaqueLowering();
  CheckMalformedMipRollback();
  CheckConventionalSrgbPbrMipChain();
  CheckConventionalSrgbPbrRollback();
  CheckStraightAlphaPremultipliedMipChain();
  CheckBc1AlphaAuthority();
  CheckLinearSpecularMipChain();
  CheckDecodedSourcePreservesAuthoredMipPrefix();
  CheckDecodedSourceFailureRollback();
  CheckTextureSourceSelectionContract();
  CheckCachedSourceReachabilitySequence();
  CheckSourceAccountingAndEligibility();
  CheckExactSamplerMappingAndFingerprint();
  CheckExactTextureFingerprint();
  CheckCachedPublicationTransactionSequence();
  CheckSamplingRejectionsAndMutation();
  CheckIdentityCollisionAndRollback();
  CheckStaticCaptureAdmission();
  CheckMatteFallbackPolicy();
  CheckMatteTintPolicy();
  CheckMatteMeshNormalization();
  std::cout << "OgreNext demo private policy tests passed\n";
  return EXIT_SUCCESS;
}
