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

TextureMipLevelDescriptor MakeMip(
    std::uint32_t width, std::uint32_t height,
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
  const std::vector<std::uint8_t> base_before =
      texture.mip_levels[0U].bytes;

  const ValidationResult result =
      CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(result.ok(), "valid native base level was rejected");
  Require(texture.mip_levels.size() == 3U &&
              texture.mip_levels.back().width == 1U &&
              texture.mip_levels.back().height == 1U,
          "native base was not completed through 1x1");
  for (std::size_t level = 0U; level < texture.mip_levels.size(); ++level) {
    const auto &bytes = texture.mip_levels[level].bytes;
    for (std::size_t alpha = 3U; alpha < bytes.size(); alpha += 4U) {
      Require(bytes[alpha] == 255U,
              "one output mip retained non-opaque alpha");
    }
  }
  for (std::size_t offset = 0U; offset < base_before.size(); ++offset) {
    if (offset % 4U != 3U) {
      Require(texture.mip_levels[0U].bytes[offset] == base_before[offset],
              "base native RGB byte changed");
    }
  }
  const std::array<std::uint8_t, 16U> expected_second{{
      13U, 33U, 53U, 255U, 15U, 35U, 55U, 255U,
      21U, 41U, 61U, 255U, 23U, 43U, 63U, 255U}};
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
  const ValidationResult result =
      CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(!result.ok() &&
              result.code == ValidationCode::SIZE_MISMATCH,
          "malformed native base level was accepted");
  Require(texture.mip_levels.size() == before.mip_levels.size() &&
              texture.mip_levels[0U].bytes == before.mip_levels[0U].bytes &&
              texture.mip_levels[0U].row_pitch_bytes == 9U,
          "failed mip validation partially changed the candidate");

  texture = NativeBaseLevel();
  texture.mip_levels.push_back(MakeMip(
      2U, 2U, std::vector<std::uint8_t>(2U * 2U * 4U, 7U)));
  const TextureResourceDescriptor extra_before = texture;
  const ValidationResult extra =
      CompleteOgreNextDemoOpaqueMipChain(texture);
  Require(!extra.ok() && texture.mip_levels.size() == 2U &&
              texture.mip_levels[0U].bytes ==
                  extra_before.mip_levels[0U].bytes &&
              texture.mip_levels[1U].bytes ==
                  extra_before.mip_levels[1U].bytes,
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
  texture.mip_levels.push_back(MakeMip(
      2U, 2U,
      {// Hostile black/white contrast in R, midtones in G, and four
       // distinct midtones in B. Authored alpha must not affect RGB.
       0U, 64U, 16U, 0U, 0U, 64U, 80U, 1U,
       255U, 192U, 144U, 127U, 255U, 192U, 208U, 254U}));
  const std::vector<std::uint8_t> base_before =
      texture.mip_levels.front().bytes;

  const ValidationResult result =
      CompleteOgreNextDemoSrgbPbrMipChain(texture);
  Require(result.ok(), "valid conventional sRGB PBR base was rejected");
  Require(texture.mip_levels.size() == 2U &&
              texture.mip_levels.back().width == 1U &&
              texture.mip_levels.back().height == 1U,
          "conventional sRGB PBR base was not completed through 1x1");
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
}

void CheckConventionalSrgbPbrRollback() {
  TextureResourceDescriptor texture = NativeBaseLevel();
  texture.mip_levels[0U].layer_pitch_bytes -= 4U;
  const TextureResourceDescriptor before = texture;
  const ValidationResult result =
      CompleteOgreNextDemoSrgbPbrMipChain(texture);
  Require(!result.ok() && result.code == ValidationCode::SIZE_MISMATCH &&
              texture.mip_levels.size() == before.mip_levels.size() &&
              texture.mip_levels[0U].bytes ==
                  before.mip_levels[0U].bytes &&
              texture.mip_levels[0U].layer_pitch_bytes ==
                  before.mip_levels[0U].layer_pitch_bytes,
          "malformed sRGB PBR base partially changed the candidate");

  texture = NativeBaseLevel();
  texture.mip_levels.push_back(MakeMip(
      2U, 2U, std::vector<std::uint8_t>(2U * 2U * 4U, 31U)));
  const TextureResourceDescriptor extra_before = texture;
  const ValidationResult extra =
      CompleteOgreNextDemoSrgbPbrMipChain(texture);
  Require(!extra.ok() && texture.mip_levels.size() == 2U &&
              texture.mip_levels[0U].bytes ==
                  extra_before.mip_levels[0U].bytes &&
              texture.mip_levels[1U].bytes ==
                  extra_before.mip_levels[1U].bytes,
          "authored sRGB PBR nonzero mip was consumed or rewritten");
}

Ogre14DecodedSourceTexture DecodedSrgbMipPrefix() {
  const TextureResourceDescriptor native = NativeBaseLevel();
  Ogre14DecodedSourceTexture decoded;
  decoded.width = native.width;
  decoded.height = native.height;
  // Model the generic decoder's canonical output for one authored multi-mip
  // legacy DXT1 source. The product validates every decoded level but consumes
  // only this base when regenerating its deterministic chain.
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

void CheckDecodedSourceUsesOnlyDeterministicBase() {
  TextureResourceDescriptor expected = NativeBaseLevel();
  Require(CompleteOgreNextDemoSrgbPbrMipChain(expected).ok(),
          "test deterministic PBR chain could not be built");

  Ogre14DecodedSourceTexture decoded = DecodedSrgbMipPrefix();
  const std::vector<std::uint8_t> authored_second =
      decoded.mip_levels[1U].rgba8_unorm;
  TextureResourceDescriptor output;
  const ValidationResult result =
      BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
          std::move(decoded), 4U, 4U, "decoded/multi-mip", output);
  Require(result.ok() && output.debug_name == "decoded/multi-mip" &&
              output.mip_levels.size() == expected.mip_levels.size(),
          "valid decoded multi-mip source was not lowered");
  for (std::size_t level = 0U; level < output.mip_levels.size(); ++level) {
    Require(output.mip_levels[level].bytes == expected.mip_levels[level].bytes,
            "authored nonzero source mip changed deterministic PBR output");
    for (std::size_t alpha = 3U; alpha < output.mip_levels[level].bytes.size();
         alpha += 4U) {
      Require(output.mip_levels[level].bytes[alpha] == 255U,
              "decoded-source PBR output retained nonopaque alpha");
    }
  }
  Require(output.mip_levels[1U].bytes != authored_second,
          "authored nonzero source mip was published instead of regenerated");
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
  OgreNextDemoTextureSourceMode mode =
      OgreNextDemoTextureSourceMode::UNAUTHENTICATED_GPU_READBACK;
  const ValidationResult missing_receipt = ValidationResult::Failure(
      ValidationCode::MISSING_REFERENCE, "texture_registry.resource_lookup",
      "exact authenticated texture resource is absent");
  const ValidationResult required_missing =
      SelectOgreNextDemoTextureSourceMode(true, true, missing_receipt, mode);
  std::size_t gpu_readbacks = 0U;
  if (required_missing.ok() &&
      mode == OgreNextDemoTextureSourceMode::UNAUTHENTICATED_GPU_READBACK) {
    ++gpu_readbacks;
  }
  Require(
      !required_missing.ok() && gpu_readbacks == 0U &&
          mode == OgreNextDemoTextureSourceMode::UNAUTHENTICATED_GPU_READBACK,
      "authenticated-required missing receipt fell through to GPU readback");

  const ValidationResult ordinary = SelectOgreNextDemoTextureSourceMode(
      false, false, ValidationResult::Success(), mode);
  if (ordinary.ok() &&
      mode == OgreNextDemoTextureSourceMode::UNAUTHENTICATED_GPU_READBACK) {
    ++gpu_readbacks;
  }
  Require(ordinary.ok() && gpu_readbacks == 1U,
          "ordinary texture did not select exactly one GPU readback");

  mode = OgreNextDemoTextureSourceMode::UNAUTHENTICATED_GPU_READBACK;
  const ValidationResult required = SelectOgreNextDemoTextureSourceMode(
      true, true, ValidationResult::Success(), mode);
  Require(required.ok() &&
              mode == OgreNextDemoTextureSourceMode::AUTHENTICATED_SOURCE_BYTES,
          "successful authenticated resolution did not select source bytes");

  const ValidationResult ordinary_probe = SelectOgreNextDemoTextureSourceMode(
      false, true, ValidationResult::Success(), mode);
  Require(!ordinary_probe.ok(),
          "ordinary texture was allowed to probe authenticated authority");
}

void CheckAuthenticatedCacheReachabilitySequence() {
  constexpr OgreNextDemoTextureSourceMode authenticated =
      OgreNextDemoTextureSourceMode::AUTHENTICATED_SOURCE_BYTES;
  std::size_t authenticated_gpu_readbacks = 0U;

  const ValidationResult frame_n =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, true, true, true, ValidationResult::Success(), true);
  Require(frame_n.ok() && authenticated_gpu_readbacks == 0U,
          "frame N authenticated cache observation was rejected or read back");

  // N+1 has no reachable instance after same-map bundle unload. The immutable
  // owner remains only to prevent source-ID resurrection and probes nothing.
  const ValidationResult frame_n_plus_1 =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, false, false, false, ValidationResult::Success(),
          false);
  Require(frame_n_plus_1.ok() && authenticated_gpu_readbacks == 0U,
          "frame N+1 unreachable owner probed or read back native storage");

  const ValidationResult missing_fresh_receipt = ValidationResult::Failure(
      ValidationCode::MISSING_REFERENCE, "texture_resolution.resource_lookup",
      "same group/name reload has no exact frozen receipt");
  const ValidationResult frame_n_plus_2_missing =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, true, true, true, missing_fresh_receipt, false);
  Require(!frame_n_plus_2_missing.ok() && authenticated_gpu_readbacks == 0U,
          "frame N+2 missing fresh receipt fell through to GPU readback");

  const ValidationResult frame_n_plus_2_mutated =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, true, true, true, ValidationResult::Success(), false);
  Require(!frame_n_plus_2_mutated.ok() && authenticated_gpu_readbacks == 0U,
          "frame N+2 changed immutable receipt was accepted or read back");

  const ValidationResult revoked_before_reuse =
      ValidateOgreNextDemoCachedTextureSourceAuthority(
          authenticated, true, false, false, ValidationResult::Success(),
          false);
  Require(!revoked_before_reuse.ok() && authenticated_gpu_readbacks == 0U,
          "revoked authenticated cache entry demoted to native readback");
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
       OgreNextDemoTextureSourceMode::AUTHENTICATED_SOURCE_BYTES});
  catalog.textures.push_back(
      {"texture/ordinary", 102U,
       OgreNextDemoTextureSourceMode::UNAUTHENTICATED_GPU_READBACK});
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
    : public IOgreNextDemoAuthenticatedTexturePublicationBatchValidator {
public:
  ValidationResult ValidateReachableAuthenticatedTextureBatch(
      const std::vector<std::string> &texture_keys) override {
    ++batch_calls;
    observed_texture_keys = texture_keys;
    return result;
  }

  ValidationResult result = ValidationResult::Success();
  std::size_t batch_calls = 0U;
  std::size_t gpu_readback_calls = 0U;
  std::vector<std::string> observed_texture_keys;
};

OgreNextDemoCachedProjectionPublicationTransaction SentinelTransaction() {
  OgreNextDemoCachedProjectionPublicationTransaction sentinel;
  sentinel.owner_catalog.push_back({"sentinel", 9991U, 9992U, 9993U, true});
  sentinel.frame_root_material_source_ids.push_back(9991U);
  sentinel.authenticated_texture_keys.push_back("sentinel/texture");
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
             std::vector<std::string>{"sentinel/texture"};
}

void CheckCachedPublicationTransactionSequence() {
  const auto committed = std::make_shared<const FrozenPublicationCatalog>(
      RepresentativePublicationCatalog());
  const std::string frozen_fingerprint =
      PublicationCatalogFingerprint(*committed);

  // Frame N: the actual used-projection key roots only the authenticated
  // material, while Apply still publishes both cached P/T/S owner triples.
  FakePublicationBatchValidator frame_n_validator;
  OgreNextDemoCachedProjectionPublicationTransaction frame_n;
  Require(BuildOgreNextDemoCachedProjectionPublicationTransaction(
              committed->projections, committed->textures, committed->samplers,
              {"projection/authenticated"}, frame_n_validator, frame_n)
              .ok(),
          "frame N cached publication transaction failed");
  Require(frame_n.owner_catalog.size() == 2U &&
              frame_n.owner_catalog[0U].frame_reachable &&
              !frame_n.owner_catalog[1U].frame_reachable &&
              frame_n.frame_root_material_source_ids ==
                  std::vector<std::uint64_t>{301U} &&
              frame_n.authenticated_texture_keys ==
                  std::vector<std::string>{"texture/authenticated"} &&
              frame_n_validator.batch_calls == 1U &&
              frame_n_validator.observed_texture_keys ==
                  frame_n.authenticated_texture_keys &&
              frame_n_validator.gpu_readback_calls == 0U,
          "frame N did not retain all owners and validate exactly one rooted "
          "authenticated texture");

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
              frame_n_plus_1_validator.batch_calls == 0U &&
              frame_n_plus_1_validator.gpu_readback_calls == 0U,
          "frame N+1 changed the COW owner catalog or rooted/probed an "
          "unreachable texture");

  // Frame N+2 unchanged reuse must retain stable IDs and perform one fresh
  // batch-authority validation before the transaction commits.
  FakePublicationBatchValidator frame_n_plus_2_validator;
  OgreNextDemoCachedProjectionPublicationTransaction frame_n_plus_2;
  Require(BuildOgreNextDemoCachedProjectionPublicationTransaction(
              committed->projections, committed->textures, committed->samplers,
              {"projection/authenticated"}, frame_n_plus_2_validator,
              frame_n_plus_2)
                  .ok() &&
              frame_n_plus_2.owner_catalog[0U].material_source_id == 301U &&
              frame_n_plus_2.owner_catalog[0U].texture_source_id == 101U &&
              frame_n_plus_2.owner_catalog[0U].sampler_source_id == 201U &&
              frame_n_plus_2_validator.batch_calls == 1U &&
              frame_n_plus_2_validator.gpu_readback_calls == 0U,
          "frame N+2 unchanged reuse changed IDs or skipped fresh authority");

  const auto require_authority_rollback = [&](ValidationCode code,
                                              const char *field,
                                              const char *detail) {
    FakePublicationBatchValidator validator;
    validator.result = ValidationResult::Failure(code, field, detail);
    OgreNextDemoCachedProjectionPublicationTransaction output =
        SentinelTransaction();
    const ValidationResult result =
        BuildOgreNextDemoCachedProjectionPublicationTransaction(
            committed->projections, committed->textures, committed->samplers,
            {"projection/authenticated"}, validator, output);
    Require(
        !result.ok() && IsSentinelTransaction(output) &&
            PublicationCatalogFingerprint(*committed) == frozen_fingerprint &&
            validator.batch_calls == 1U && validator.gpu_readback_calls == 0U,
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
              dependency_validator.batch_calls == 0U,
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
  Require(!mutated.ok() &&
              mutated.code == ValidationCode::REVISION_MISMATCH &&
              mutated.field ==
                  "ogre_next_demo.terrain.readback.revalidation",
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
              DeriveOgreNextDemoSourceId("demo/texture", "page/0/0",
                                         texture_id).ok() &&
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
              -0.51320022F, 0.51320022F,
              vertical_half_extent, -vertical_half_extent,
              0.5F, 350.0F, 16.0F / 9.0F, radius)
              .ok() &&
              radius > 539.0F && radius < 542.0F,
          "normalized 16:9 far-frustum capture radius changed");
  float ultrawide_radius = -1.0F;
  Require(BuildOgreNextDemoStaticCaptureRadius(
              -0.51320022F, 0.51320022F,
              vertical_half_extent, -vertical_half_extent,
              0.5F, 350.0F, 32.0F / 9.0F, ultrawide_radius)
              .ok() &&
              ultrawide_radius > radius,
          "ultrawide child aspect did not expand static admission");

  Bounds3 touching;
  touching.minimum = {radius - 1.0F, -1.0F, -1.0F};
  touching.maximum = {radius + 1.0F, 1.0F, 1.0F};
  bool retained = false;
  Require(ClassifyOgreNextDemoStaticBounds(touching, {}, radius, retained)
              .ok() &&
              retained,
          "AABB touching the demo capture sphere was omitted");
  Bounds3 outside = touching;
  outside.minimum.x = radius + 1.0F;
  outside.maximum.x = radius + 2.0F;
  retained = true;
  Require(ClassifyOgreNextDemoStaticBounds(outside, {}, radius, retained)
              .ok() &&
              !retained,
          "AABB outside the demo capture sphere was retained");

  float sentinel_radius = 17.0F;
  Require(!BuildOgreNextDemoStaticCaptureRadius(
               1.0F, -1.0F, 1.0F, -1.0F,
               0.5F, 350.0F, 16.0F / 9.0F, sentinel_radius)
               .ok() &&
              sentinel_radius == 17.0F,
          "invalid frustum partially published a capture radius");
  Bounds3 invalid = touching;
  invalid.minimum.x = (std::numeric_limits<float>::quiet_NaN)();
  retained = true;
  Require(!ClassifyOgreNextDemoStaticBounds(invalid, {}, radius, retained)
               .ok() &&
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
  Require(OgreNextDemoOmitsNonUniformSpeedBump(
              "topeQr.mesh", {1.0F, 0.5F, 0.5F}) &&
              !OgreNextDemoOmitsNonUniformSpeedBump(
                  "topeQr.mesh", {1.0F, 1.0F, 1.0F}) &&
              !OgreNextDemoOmitsNonUniformSpeedBump(
                  "other.mesh", {1.0F, 0.5F, 0.5F}),
          "CityWorld speed-bump omission broadened beyond its exact identity");
  Require(OgreNextDemoAllowsAlexisTUS0Approximation(
                  "{bundle USER:/mods/AlexisSaber.zip}",
                  "SaberChassis (AlexisSaber.truck [Instance ID 17])") &&
              OgreNextDemoAllowsAlexisTUS0Approximation(
                  "{bundle USER:/mods/AlexisSaber.zip}",
                  "SaberGrilles (AlexisSaber.truck [Instance ID 0])") &&
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

void CheckMatteMeshNormalization() {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "demo matte triangle";
  mesh.dynamic = true;
  mesh.local_bounds.minimum = {-1.0F, 0.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {{-1.0F, 0.0F, 0.0F},
                    {1.0F, 0.0F, 0.0F},
                    {0.0F, 1.0F, 0.0F}};
  mesh.normals = {
      {0.0F, 0.0F, 2.0F},
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
  Require(mesh.velocities.empty() &&
              mesh.texture_coordinates_1.empty() && mesh.colors.empty() &&
              ValidateMeshResourceDescriptor(mesh).ok(),
          "matte normalization retained an unsupported RT4 stream");

  std::vector<Float3> dynamic_normals{
      {0.0F, 3.0F, 0.0F},
      {(std::numeric_limits<float>::infinity)(), 1.0F, 0.0F},
      {0.0F, 0.0F, 0.0F}};
  std::vector<Float4> dynamic_tangents{{9.0F, 9.0F, 9.0F, 9.0F}};
  Require(BuildOgreNextDemoMatteTangents(
              3U, dynamic_normals, dynamic_tangents)
              .ok() &&
              dynamic_normals ==
                  std::vector<Float3>(3U, {0.0F, 1.0F, 0.0F}) &&
              dynamic_tangents ==
                  std::vector<Float4>(3U, {-1.0F, 0.0F, 0.0F, 1.0F}),
          "joined dynamic normals/tangents were not sanitized deterministically");

  std::vector<Float3> missing_normals;
  std::vector<Float4> missing_tangents{{7.0F, 7.0F, 7.0F, 7.0F}};
  Require(BuildOgreNextDemoMatteTangents(
              2U, missing_normals, missing_tangents)
              .ok() &&
              missing_normals ==
                  std::vector<Float3>(2U, {0.0F, 1.0F, 0.0F}) &&
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
  Require(!rollback_result.ok() &&
              rollback.normals == rollback_normals &&
              rollback.tangents == rollback_tangents &&
              rollback.texture_coordinates_0.size() == 2U,
          "post-validation failure partially committed sanitized mesh streams");
}

} // namespace

int main() {
  CheckFullMipOpaqueLowering();
  CheckMalformedMipRollback();
  CheckConventionalSrgbPbrMipChain();
  CheckConventionalSrgbPbrRollback();
  CheckDecodedSourceUsesOnlyDeterministicBase();
  CheckDecodedSourceFailureRollback();
  CheckTextureSourceSelectionContract();
  CheckAuthenticatedCacheReachabilitySequence();
  CheckCachedPublicationTransactionSequence();
  CheckSamplingRejectionsAndMutation();
  CheckIdentityCollisionAndRollback();
  CheckStaticCaptureAdmission();
  CheckMatteFallbackPolicy();
  CheckMatteMeshNormalization();
  std::cout << "OgreNext demo private policy tests passed\n";
  return EXIT_SUCCESS;
}
