/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace RoR::Render::Testing {

static_assert(!std::is_nothrow_invocable_v<
                  decltype(&EvaluateOgre14TerrainCompositeBilinearOracle),
                  Ogre14TerrainCompositeRgbTransfer,
                  const std::array<Ogre14TerrainCompositeOracleTexel, 4U> &,
                  float, float, Ogre14TerrainCompositeOracleSample &>,
              "oracle failure construction must remain catchable");

ValidationResult Ogre14TerrainCompositeCaptureTestAccess::Capture(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before, const void *bytes,
    std::size_t byte_count,
    const Ogre14TerrainCompositeNativeObservation &after,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  if (bytes == nullptr && byte_count != 0U) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD,
        "terrain_composite.readback.mip_rgba_bytes",
        "nonempty synthetic level-zero readback has no source bytes");
  }
  std::vector<std::vector<std::uint8_t>> chain(1U);
  if (byte_count != 0U) {
    const auto *const first = static_cast<const std::uint8_t *>(bytes);
    chain.front().assign(first, first + byte_count);
  }
  return Ogre14TerrainCompositeNativeAdapter::CaptureSyntheticForTesting(
      configuration, before, chain, after, receipt, fault_injector);
}

ValidationResult Ogre14TerrainCompositeCaptureTestAccess::CaptureMipChain(
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before,
    const std::vector<std::vector<std::uint8_t>> &mip_rgba_bytes,
    const Ogre14TerrainCompositeNativeObservation &after,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  return Ogre14TerrainCompositeNativeAdapter::CaptureSyntheticForTesting(
      configuration, before, mip_rgba_bytes, after, receipt, fault_injector);
}

} // namespace RoR::Render::Testing

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::array<float, 16U> IdentityTransform() {
  return {{
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
      0.0F, 0.0F, 0.0F, 1.0F,
  }};
}

std::vector<std::vector<std::uint8_t>> CanonicalMipBytes() {
  std::vector<std::vector<std::uint8_t>> bytes(3U);
  for (std::uint8_t value = 0U; value < 64U; ++value) {
    bytes[0U].push_back(value);
  }
  for (std::uint8_t value = 64U; value < 80U; ++value) {
    bytes[1U].push_back(value);
  }
  bytes[2U] = {200U, 100U, 50U, 17U};
  return bytes;
}

Ogre14TerrainCompositeNativeObservation CanonicalObservation(
    bool hardware_gamma = true,
    Ogre14TerrainCompositeSceneFogMode fog =
        Ogre14TerrainCompositeSceneFogMode::FOG_NONE) {
  Ogre14TerrainCompositeNativeObservation observation;
  observation.terrain_group_pointer_token = 0x100U;
  observation.terrain_slot_pointer_token = 0x180U;
  observation.terrain_pointer_token = 0x200U;
  observation.packed_slot_key = 0x0001ffffU;
  observation.slot_x = 1;
  observation.slot_y = -1;
  observation.exact_terrain_resource_group = "CityWorld";
  observation.exact_filename_prefix = "cityworld-page";
  observation.exact_filename_extension = "dat";
  observation.page_definition_kind =
      Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED;
  observation.exact_definition_filename = "cityworld-import.dat";
  observation.generated_save_filename = "cityworld-page_0001ffff.dat";
  observation.exact_terrain_material_name = "CityWorld/Terrain/1/-1";
  observation.terrain_alignment = Ogre14TerrainCompositeAlignment::X_Z;
  observation.terrain_size = 1025U;
  observation.terrain_world_size = 12000.0F;
  observation.terrain_world_position = {12000.0F, 0.0F, -12000.0F};
  observation.terrain_is_loaded = true;
  observation.terrain_derived_data_update_in_progress = false;
  observation.texture_pointer_token = 0x300U;
  observation.pixel_buffer_pointer_token = 0x400U;
  observation.texture_handle = 55U;
  observation.exact_texture_resource_group = "CityWorldDerived";
  observation.exact_texture_name = "CityWorld/Terrain/1/-1/comp";
  observation.texture_type = Ogre14TerrainCompositeTextureType::TEXTURE_2D;
  observation.texture_loading_state =
      Ogre14TerrainCompositeTextureLoadingState::LOADED;
  observation.texture_width = 4U;
  observation.texture_height = 4U;
  observation.texture_depth = 1U;
  observation.texture_face_count = 1U;
  observation.texture_additional_mip_count = 2U;
  observation.texture_mip_count = 3U;
  observation.texture_usage = 0x20U;
  observation.texture_is_loaded = true;
  observation.texture_is_manual = true;
  observation.texture_hardware_gamma_enabled = hardware_gamma;
  observation.texture_resource_revision = 7U;
  observation.tight_row_pitch_bytes = 16U;
  observation.tight_slice_pitch_bytes = 64U;
  observation.mip_chain = {
      {0U, 0x400U, 4U, 4U, 1U, 16U, 64U},
      {1U, 0x410U, 2U, 2U, 1U, 8U, 16U},
      {2U, 0x420U, 1U, 1U, 1U, 4U, 4U},
  };

  Ogre14TerrainCompositeSamplingObservation &sampling = observation.sampling;
  sampling.scene_manager_pointer_token = 0x500U;
  sampling.texture_unit_pointer_token = 0x530U;
  sampling.sampler_pointer_token = 0x540U;
  sampling.bound_texture_pointer_token = observation.texture_pointer_token;
  sampling.texture_unit_content_named = true;
  sampling.texture_unit_frame_count = 1U;
  sampling.texture_unit_current_frame = 0U;
  sampling.texture_unit_texture_2d = true;
  sampling.texture_unit_is_blank = false;
  sampling.texture_unit_load_failing = false;
  sampling.unordered_access_mip_level = -1;
  sampling.texture_coord_set = 0U;
  sampling.texcoord_calculation_none = true;
  sampling.texture_effect_count = 0U;
  sampling.texture_u_scroll = 0.0F;
  sampling.texture_v_scroll = 0.0F;
  sampling.texture_u_scale = 1.0F;
  sampling.texture_v_scale = 1.0F;
  sampling.texture_rotation_radians = 0.0F;
  sampling.texture_transform = IdentityTransform();
  sampling.address_u = Ogre14TerrainCompositeAddressMode::CLAMP;
  sampling.address_v = Ogre14TerrainCompositeAddressMode::CLAMP;
  sampling.address_w = Ogre14TerrainCompositeAddressMode::CLAMP;
  sampling.min_filter = Ogre14TerrainCompositeFilter::LINEAR;
  sampling.mag_filter = Ogre14TerrainCompositeFilter::LINEAR;
  sampling.mip_filter = Ogre14TerrainCompositeFilter::POINT;
  sampling.maximum_anisotropy = 1U;
  sampling.mipmap_bias = 0.0F;
  sampling.compare_enabled = false;
  sampling.compare_function =
      Ogre14TerrainCompositeCompareFunction::GREATER_EQUAL;
  sampling.border_colour = {0.0F, 0.0F, 0.0F, 1.0F};
  sampling.texture_unit_hardware_gamma_enabled = hardware_gamma;
  sampling.scene_fog_mode = fog;
  return observation;
}

constexpr std::array<std::uint8_t, 32U> kExpectedMip0Sha256{{
    0x3aU, 0xecU, 0x02U, 0x60U, 0x4fU, 0x0bU, 0x03U, 0x86U,
    0x23U, 0x33U, 0x4eU, 0x3dU, 0xc9U, 0x20U, 0x86U, 0xcaU,
    0xb9U, 0x2eU, 0xd3U, 0xecU, 0x65U, 0x66U, 0x1bU, 0xd2U,
    0xa9U, 0x74U, 0x95U, 0x6cU, 0xa0U, 0xddU, 0x80U, 0x1aU,
}};

constexpr std::array<std::uint8_t, 32U> kExpectedChainSha256{{
    0x21U, 0x37U, 0x4aU, 0xe5U, 0x19U, 0x20U, 0xd5U, 0xe0U,
    0x1dU, 0x2aU, 0xb2U, 0xedU, 0xdbU, 0xb6U, 0x34U, 0xa2U,
    0x50U, 0x79U, 0x3bU, 0x93U, 0x6bU, 0x8cU, 0xc5U, 0xbeU,
    0x6fU, 0x13U, 0x37U, 0xfbU, 0xd0U, 0x03U, 0xeeU, 0x64U,
}};

Ogre14TerrainCompositeCaptureReceipt CaptureCanonical(
    bool hardware_gamma = true,
    Ogre14TerrainCompositeSceneFogMode fog =
        Ogre14TerrainCompositeSceneFogMode::FOG_NONE) {
  Ogre14TerrainCompositeCaptureReceipt receipt;
  const Ogre14TerrainCompositeNativeObservation observation =
      CanonicalObservation(hardware_gamma, fog);
  const ValidationResult result = RoR::Render::Testing::
      Ogre14TerrainCompositeCaptureTestAccess::CaptureMipChain(
          {}, observation, CanonicalMipBytes(), observation, receipt);
  Require(result.ok(), "canonical V2 composite capture failed");
  return receipt;
}

void RequireOwnerUnchanged(
    const Ogre14TerrainCompositeCaptureReceipt &owner,
    const Ogre14TerrainCompositeCaptureReceipt &output,
    const Ogre14TerrainCompositeCaptureMetadata *metadata_before,
    const std::array<const std::uint8_t *, 3U> &mip_pointers_before,
    const char *message) {
  Require(output.SharesImmutableStateWith(owner), message);
  Require(output.metadata() == metadata_before, message);
  for (std::size_t level = 0U; level < mip_pointers_before.size(); ++level) {
    Require(output.mip_rgba_bytes(level) == mip_pointers_before[level], message);
  }
}

ValidationResult CaptureHostile(
    const Ogre14TerrainCompositeCaptureReceipt &owner,
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const Ogre14TerrainCompositeNativeObservation &before,
    const std::vector<std::vector<std::uint8_t>> &bytes,
    const Ogre14TerrainCompositeNativeObservation &after) {
  Ogre14TerrainCompositeCaptureReceipt output = owner;
  const auto *const metadata_before = output.metadata();
  const std::array<const std::uint8_t *, 3U> pointers{{
      output.mip_rgba_bytes(0U), output.mip_rgba_bytes(1U),
      output.mip_rgba_bytes(2U),
  }};
  const ValidationResult result = RoR::Render::Testing::
      Ogre14TerrainCompositeCaptureTestAccess::CaptureMipChain(
          configuration, before, bytes, after, output);
  Require(!result.ok(), "hostile V2 capture unexpectedly succeeded");
  RequireOwnerUnchanged(owner, output, metadata_before, pointers,
                        "hostile V2 capture changed prior receipt owner");
  return result;
}

template <typename Mutation>
void RequireRevalidationRejected(
    const Ogre14TerrainCompositeCaptureReceipt &owner, Mutation mutate,
    const char *message) {
  const Ogre14TerrainCompositeNativeObservation before = CanonicalObservation();
  Ogre14TerrainCompositeNativeObservation after = before;
  mutate(after);
  const ValidationResult result =
      CaptureHostile(owner, {}, before, CanonicalMipBytes(), after);
  Require(result.field == "terrain_composite.readback.revalidation", message);
}

using ObservationMutation =
    std::function<void(Ogre14TerrainCompositeNativeObservation &)>;

struct AuthoritySubstitution final {
  ObservationMutation mutate;
  const char *revalidation_message = nullptr;
};

std::vector<AuthoritySubstitution> AuthoritySubstitutions() {
  return {
      {[](auto &v) { ++v.terrain_group_pointer_token; },
       "substituted TerrainGroup pointer was accepted during revalidation"},
      {[](auto &v) { ++v.terrain_slot_pointer_token; },
       "substituted Terrain slot pointer was accepted during revalidation"},
      {[](auto &v) { ++v.terrain_pointer_token; },
       "substituted Terrain pointer was accepted during revalidation"},
      {[](auto &v) {
         ++v.texture_pointer_token;
         ++v.sampling.bound_texture_pointer_token;
       },
       "substituted bound texture pointer was accepted during revalidation"},
      {[](auto &v) {
         ++v.pixel_buffer_pointer_token;
         ++v.mip_chain[0U].pixel_buffer_pointer_token;
       },
       "substituted level-zero pixel buffer was accepted during revalidation"},
      {[](auto &v) { ++v.mip_chain[1U].pixel_buffer_pointer_token; },
       "substituted child-mip pixel buffer was accepted during revalidation"},
      {[](auto &v) { ++v.texture_handle; },
       "substituted texture handle was accepted during revalidation"},
      {[](auto &v) { v.exact_texture_name += ".replacement"; },
       "name-only texture substitution was accepted during revalidation"},
      {[](auto &v) { v.exact_texture_resource_group = "Other"; },
       "texture resource-group substitution was accepted during revalidation"},
      {[](auto &v) { ++v.packed_slot_key; },
       "packed terrain slot substitution was accepted during revalidation"},
      {[](auto &v) { v.exact_definition_filename += ".replacement"; },
       "terrain page definition substitution was accepted during revalidation"},
      {[](auto &v) { v.generated_save_filename += ".replacement"; },
       "generated save identity substitution was accepted during revalidation"},
      {[](auto &v) { v.terrain_world_position[0U] += 1.0F; },
       "terrain transform substitution was accepted during revalidation"},
      {[](auto &v) {
         v.texture_hardware_gamma_enabled = false;
         v.sampling.texture_unit_hardware_gamma_enabled = false;
       },
       "coupled Texture/TUS gamma substitution was accepted during revalidation"},
      {[](auto &v) { ++v.texture_usage; },
       "texture usage substitution was accepted during revalidation"},
      {[](auto &v) { ++v.texture_resource_revision; },
       "stale texture revision was accepted during revalidation"},
      {[](auto &v) { ++v.sampling.texture_unit_pointer_token; },
       "texture-unit substitution was accepted during revalidation"},
      {[](auto &v) { ++v.sampling.sampler_pointer_token; },
       "sampler substitution was accepted during revalidation"},
      {[](auto &v) { ++v.sampling.scene_manager_pointer_token; },
       "SceneManager substitution was accepted during revalidation"},
      {[](auto &v) {
         v.sampling.scene_fog_mode =
             Ogre14TerrainCompositeSceneFogMode::FOG_EXP;
       },
       "direct scene-fog substitution was accepted during revalidation"},
  };
}

class ThrowingInjector final
    : public IOgre14TerrainCompositeCaptureFaultInjector {
public:
  Ogre14TerrainCompositeCaptureStage target =
      Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_IDENTITY_CAPTURE;
  bool bad_alloc = false;
  std::size_t calls = 0U;

  void BeforeTerrainCompositeCaptureStage(
      Ogre14TerrainCompositeCaptureStage stage) override {
    ++calls;
    if (stage == target) {
      if (bad_alloc) {
        throw std::bad_alloc();
      }
      throw 17;
    }
  }
};

void CheckCanonicalTransportAndLowering() {
  const auto source = CanonicalMipBytes();
  for (const bool hardware_gamma : {true, false}) {
    const Ogre14TerrainCompositeCaptureReceipt receipt =
        CaptureCanonical(hardware_gamma);
    const Ogre14TerrainCompositeCaptureMetadata &metadata = *receipt.metadata();
    Require(metadata.version == 2U && metadata.semantic_contract_version == 2U,
            "V2 receipt versions changed");
    Require(metadata.texture_additional_mip_count == 2U &&
                metadata.texture_mip_count == 3U &&
                receipt.mip_level_count() == 3U,
            "OGRE additional-versus-total mip semantics changed");
    Require(metadata.terrain_group_pointer_token == 0x100U &&
                metadata.terrain_slot_pointer_token == 0x180U &&
                metadata.terrain_pointer_token == 0x200U &&
                metadata.packed_slot_key == 0x0001ffffU &&
                metadata.slot_x == 1 && metadata.slot_y == -1 &&
                metadata.page_definition_kind ==
                    Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED &&
                metadata.exact_definition_filename ==
                    "cityworld-import.dat" &&
                metadata.generated_save_filename ==
                    "cityworld-page_0001ffff.dat" &&
                metadata.terrain_world_position ==
                    std::array<float, 3U>{{12000.0F, 0.0F, -12000.0F}},
            "terrain group/slot/page/transform identity was not retained");
    Require(metadata.texture_pointer_token == 0x300U &&
                metadata.pixel_buffer_pointer_token == 0x400U &&
                metadata.texture_handle == 55U &&
                metadata.exact_texture_resource_group == "CityWorldDerived" &&
                metadata.exact_texture_name ==
                    "CityWorld/Terrain/1/-1/comp" &&
                metadata.texture_resource_revision_before_readback == 7U &&
                metadata.texture_resource_revision_after_readback == 7U &&
                metadata.mip_chain[1U].pixel_buffer_pointer_token == 0x410U &&
                metadata.mip_chain[2U].pixel_buffer_pointer_token == 0x420U,
            "texture/handle/name/group/full-mip identity was not retained");
    Require(metadata.mip_chain[0U].rgba_sha256 == kExpectedMip0Sha256 &&
                metadata.full_mip_chain_rgba_byte_count == 84U &&
                metadata.full_mip_chain_sha256 == kExpectedChainSha256,
            "domain-separated exact mip digests changed");
    Require(metadata.rgb_semantic ==
                    Ogre14TerrainCompositeRgbSemantic::BAKED_DIFFUSE &&
                metadata.alpha_semantic ==
                    Ogre14TerrainCompositeAlphaSemantic::LINEAR_SPECULAR_MASK,
            "transport RGB/alpha semantics changed");
    Require(metadata.rgb_transfer ==
                    (hardware_gamma
                         ? Ogre14TerrainCompositeRgbTransfer::
                               DECODE_BEFORE_FILTER
                         : Ogre14TerrainCompositeRgbTransfer::
                               LEGACY_UNORM_DISPLAY_DOMAIN) &&
                metadata.sampling.texture_unit_hardware_gamma_enabled ==
                    hardware_gamma,
            "Texture/TUS gamma or transfer classification changed");
    for (std::size_t level = 0U; level < source.size(); ++level) {
      Require(receipt.mip_rgba_size(level) == source[level].size() &&
                  std::equal(source[level].begin(), source[level].end(),
                             receipt.mip_rgba_bytes(level)),
              "receipt did not retain an exact source mip");
    }
    Require(receipt.mip_rgba_bytes(3U) == nullptr &&
                receipt.mip_rgba_size(3U) == 0U,
            "out-of-range mip accessor did not fail closed");

    Ogre14TerrainCompositeOpaqueLowering lowering;
    const ValidationResult result =
        LowerOgre14TerrainCompositeOpaque(receipt, lowering);
    Require(result.ok(), "honest transfer-preserving lowering failed");
    Require(lowering.version == 1U &&
                lowering.rgb_transfer == metadata.rgb_transfer &&
                lowering.rgb_semantic ==
                    Ogre14TerrainCompositeRgbSemantic::BAKED_DIFFUSE &&
                lowering.forced_opaque_alpha == 255U &&
                lowering.mip_chain.size() == source.size(),
            "opaque lowering lost its explicit semantic or transfer");
    for (std::size_t level = 0U; level < source.size(); ++level) {
      const auto &target = lowering.mip_chain[level].rgba_bytes;
      Require(target.size() == source[level].size(),
              "lowered mip byte count changed");
      for (std::size_t byte = 0U; byte < target.size(); ++byte) {
        Require(byte % 4U == 3U ? target[byte] == 255U
                               : target[byte] == source[level][byte],
                "lowering changed RGB or failed to force opaque alpha");
      }
      Require(std::equal(source[level].begin(), source[level].end(),
                         receipt.mip_rgba_bytes(level)),
              "lowering modified immutable source alpha evidence");
    }
    Require(lowering.sampler.minification_filter == SamplerFilter::LINEAR &&
                lowering.sampler.magnification_filter ==
                    SamplerFilter::LINEAR &&
                lowering.sampler.mip_filter == SamplerFilter::NEAREST &&
                lowering.sampler.address_u ==
                    SamplerAddressMode::CLAMP_TO_EDGE &&
                lowering.sampler.address_v ==
                    SamplerAddressMode::CLAMP_TO_EDGE &&
                lowering.sampler.address_w ==
                    SamplerAddressMode::CLAMP_TO_EDGE &&
                lowering.sampler.maximum_lod == 2.0F &&
                !lowering.sampler.anisotropy_enabled &&
                !lowering.sampler.compare_enabled &&
                lowering.sampler.border_color.x == 0.0F &&
                lowering.sampler.border_color.y == 0.0F &&
                lowering.sampler.border_color.z == 0.0F &&
                lowering.sampler.border_color.w == 0.0F &&
                lowering.texture_coordinate_set == 0U,
            "lowering sampler/UV policy changed");
  }

  const Ogre14TerrainCompositeCaptureReceipt fogged = CaptureCanonical(
      true, Ogre14TerrainCompositeSceneFogMode::FOG_EXP2);
  Ogre14TerrainCompositeOpaqueLowering sentinel;
  sentinel.version = 99U;
  sentinel.mip_chain.push_back({});
  const ValidationResult fog_result =
      LowerOgre14TerrainCompositeOpaque(fogged, sentinel);
  Require(fog_result.field == "terrain_composite.lowering.scene_fog_mode" &&
              sentinel.version == 99U && sentinel.mip_chain.size() == 1U,
          "non-FOG_NONE lowering did not fail transactionally");
}

void CheckConfigurationMipAndAuthorityHostility() {
  const Ogre14TerrainCompositeCaptureReceipt owner = CaptureCanonical();
  auto observation = CanonicalObservation();
  auto bytes = CanonicalMipBytes();

  Ogre14TerrainCompositeCaptureConfiguration configuration;
  configuration.version = 1U;
  Require(CaptureHostile(owner, configuration, observation, bytes, observation)
              .field == "terrain_composite.configuration.version",
          "configuration version returned the wrong blocker");
  const std::vector<std::function<void(
      Ogre14TerrainCompositeCaptureConfiguration &)>> cap_mutations{{
      [](auto &v) { v.maximum_dimension = 0U; },
      [](auto &v) { v.maximum_rgba_bytes = 0U; },
      [](auto &v) { v.maximum_identifier_bytes = 0U; },
      [](auto &v) { v.maximum_mip_levels = 0U; },
      [](auto &v) {
        v.maximum_mip_levels =
            kOgre14TerrainCompositeHardMaximumMipLevels + 1U;
      },
  }};
  for (const auto &mutate : cap_mutations) {
    configuration = {};
    mutate(configuration);
    Require(CaptureHostile(owner, configuration, observation, bytes,
                           observation)
                .field == "terrain_composite.configuration.caps",
            "configuration cap returned the wrong blocker");
  }

  auto missing = observation;
  missing.mip_chain.pop_back();
  Require(CaptureHostile(owner, {}, missing, bytes, missing).field ==
              "terrain_composite.observation.full_mip_chain",
          "missing final mip was accepted");
  auto raw_count = observation;
  raw_count.texture_additional_mip_count = 1U;
  Require(CaptureHostile(owner, {}, raw_count, bytes, raw_count).field ==
              "terrain_composite.observation.full_mip_chain",
          "additional-mip semantics were ambiguous");
  auto gap = observation;
  gap.mip_chain[1U].mip_level = 2U;
  Require(CaptureHostile(owner, {}, gap, bytes, gap).field ==
              "terrain_composite.observation.mip_layout",
          "gapped mip numbering was accepted");
  auto alias = observation;
  alias.mip_chain[1U].pixel_buffer_pointer_token =
      alias.mip_chain[0U].pixel_buffer_pointer_token;
  Require(CaptureHostile(owner, {}, alias, bytes, alias).field ==
              "terrain_composite.observation.mip_identity",
          "aliased mip buffers were accepted");
  auto truncated = bytes;
  truncated[1U].pop_back();
  Require(CaptureHostile(owner, {}, observation, truncated, observation).field ==
              "terrain_composite.readback.mip_rgba_bytes",
          "truncated mip payload was accepted");
  configuration = {};
  configuration.maximum_rgba_bytes = 83U;
  Require(CaptureHostile(owner, configuration, observation, bytes, observation)
              .field == "terrain_composite.observation.full_mip_cap",
          "aggregate mip-byte cap was not enforced");
  configuration = {};
  configuration.maximum_dimension = 3U;
  Require(CaptureHostile(owner, configuration, observation, bytes, observation)
              .field == "terrain_composite.observation.texture_cap",
          "dimension cap was not enforced");
  configuration = {};
  configuration.maximum_identifier_bytes = 4U;
  Require(CaptureHostile(owner, configuration, observation, bytes, observation)
              .field == "terrain_composite.observation.identifiers",
          "identifier cap was not enforced");
  configuration = {};
  configuration.maximum_mip_levels = 2U;
  Require(CaptureHostile(owner, configuration, observation, bytes, observation)
              .field == "terrain_composite.observation.full_mip_chain",
          "maximum_mip_levels below the complete chain was accepted");

  Ogre14TerrainCompositeCaptureReceipt null_output = owner;
  const auto *const null_metadata = null_output.metadata();
  const std::uint8_t *const null_bytes = null_output.rgba_bytes();
  const ValidationResult null_result = RoR::Render::Testing::
      Ogre14TerrainCompositeCaptureTestAccess::Capture(
          {}, observation, nullptr, bytes.front().size(), observation,
          null_output);
  Require(null_result.code == ValidationCode::EMPTY_PAYLOAD &&
              null_output.SharesImmutableStateWith(owner) &&
              null_output.metadata() == null_metadata &&
              null_output.rgba_bytes() == null_bytes,
          "null readback did not fail transactionally");

  auto zero_revision = observation;
  zero_revision.texture_resource_revision = 0U;
  Require(CaptureHostile(owner, {}, zero_revision, bytes, zero_revision).field ==
              "terrain_composite.observation.resource_revision",
          "zero texture revision was accepted");
  auto invalid_definition = observation;
  invalid_definition.exact_definition_filename.clear();
  Require(CaptureHostile(owner, {}, invalid_definition, bytes,
                         invalid_definition)
              .field == "terrain_composite.observation.page_definition",
          "file-backed page without a definition identity was accepted");
  auto updating_terrain = observation;
  updating_terrain.terrain_derived_data_update_in_progress = true;
  Require(CaptureHostile(owner, {}, updating_terrain, bytes, updating_terrain)
              .field == "terrain_composite.observation.terrain_page",
          "updating Terrain page was accepted");
  auto unloaded_terrain = observation;
  unloaded_terrain.terrain_is_loaded = false;
  Require(CaptureHostile(owner, {}, unloaded_terrain, bytes, unloaded_terrain)
              .field == "terrain_composite.observation.terrain_page",
          "unloaded Terrain page was accepted");
  auto invalid_texture_state = observation;
  invalid_texture_state.texture_loading_state =
      static_cast<Ogre14TerrainCompositeTextureLoadingState>(255U);
  Require(CaptureHostile(owner, {}, invalid_texture_state, bytes,
                         invalid_texture_state)
              .field == "terrain_composite.observation.texture_state",
          "invalid texture loading state was accepted");
  auto bad_layout = observation;
  ++bad_layout.mip_chain[0U].tight_row_pitch_bytes;
  Require(CaptureHostile(owner, {}, bad_layout, bytes, bad_layout).field ==
              "terrain_composite.observation.mip_layout",
          "padded mip-row layout was accepted");
  auto overflow_count = observation;
  overflow_count.texture_additional_mip_count =
      (std::numeric_limits<std::uint32_t>::max)();
  Require(CaptureHostile(owner, {}, overflow_count, bytes, overflow_count).field ==
              "terrain_composite.observation.full_mip_chain",
          "overflowing additional-mip count was accepted");

  auto consumed = observation;
  consumed.page_definition_kind =
      Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME;
  consumed.exact_definition_filename.clear();
  Ogre14TerrainCompositeCaptureReceipt consumed_receipt;
  const ValidationResult consumed_result = RoR::Render::Testing::
      Ogre14TerrainCompositeCaptureTestAccess::CaptureMipChain(
          {}, consumed, bytes, consumed, consumed_receipt);
  Require(consumed_result.ok() && consumed_receipt.initialized() &&
              consumed_receipt.metadata()->page_definition_kind ==
                  Ogre14TerrainCompositePageDefinitionKind::
                      CONSUMED_OR_RUNTIME &&
              consumed_receipt.metadata()->exact_definition_filename.empty() &&
              consumed_receipt.metadata()->generated_save_filename ==
                  "cityworld-page_0001ffff.dat",
          "consumed/runtime page identity was rejected or mislabeled");

  const std::vector<std::pair<
      std::function<void(Ogre14TerrainCompositeNativeObservation &)>,
      const char *>> invalid_sampling{{
      {[](auto &v) { v.sampling.scene_manager_pointer_token = 0U; },
       "terrain_composite.observation.sampling_identity"},
      {[](auto &v) { v.sampling.bound_texture_pointer_token++; },
       "terrain_composite.observation.texture_unit_binding"},
      {[](auto &v) { v.sampling.texture_unit_content_named = false; },
       "terrain_composite.observation.texture_unit_binding"},
      {[](auto &v) { v.sampling.texture_unit_frame_count = 2U; },
       "terrain_composite.observation.texture_unit_binding"},
      {[](auto &v) { v.sampling.texture_unit_current_frame = 1U; },
       "terrain_composite.observation.texture_unit_binding"},
      {[](auto &v) { v.sampling.texture_unit_texture_2d = false; },
       "terrain_composite.observation.texture_unit_binding"},
      {[](auto &v) { v.sampling.texture_unit_is_blank = true; },
       "terrain_composite.observation.texture_unit_binding"},
      {[](auto &v) { v.sampling.texture_unit_load_failing = true; },
       "terrain_composite.observation.texture_unit_binding"},
      {[](auto &v) { v.sampling.unordered_access_mip_level = 0; },
       "terrain_composite.observation.texture_unit_binding"},
      {[](auto &v) { v.sampling.texture_coord_set = 1U; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.texcoord_calculation_none = false; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.texture_effect_count = 1U; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.texture_u_scroll = -0.0F; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.texture_v_scroll = 0.25F; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.texture_u_scale = 0.5F; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.texture_v_scale = 2.0F; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.texture_rotation_radians = 0.5F; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.texture_transform[1U] = -0.0F; },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) {
         v.sampling.texture_transform[1U] =
             (std::numeric_limits<float>::quiet_NaN)();
       },
       "terrain_composite.observation.sampling_uv"},
      {[](auto &v) { v.sampling.address_u =
                         static_cast<Ogre14TerrainCompositeAddressMode>(9U); },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.address_v =
                         static_cast<Ogre14TerrainCompositeAddressMode>(9U); },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.address_w =
                         static_cast<Ogre14TerrainCompositeAddressMode>(9U); },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.min_filter =
                         Ogre14TerrainCompositeFilter::POINT; },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.mag_filter =
                         Ogre14TerrainCompositeFilter::POINT; },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.mip_filter =
                         Ogre14TerrainCompositeFilter::LINEAR; },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.maximum_anisotropy = 2U; },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.mipmap_bias = -0.0F; },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) {
         v.sampling.mipmap_bias =
             (std::numeric_limits<float>::quiet_NaN)();
       },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.compare_enabled = true; },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) {
         v.sampling.compare_function =
             static_cast<Ogre14TerrainCompositeCompareFunction>(9U);
       },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.border_colour[0U] = -0.0F; },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) { v.sampling.border_colour[3U] = 0.0F; },
       "terrain_composite.observation.sampling_sampler"},
      {[](auto &v) {
         v.sampling.texture_unit_hardware_gamma_enabled = false;
       },
       "terrain_composite.observation.gamma_agreement"},
      {[](auto &v) {
         v.sampling.scene_fog_mode =
             static_cast<Ogre14TerrainCompositeSceneFogMode>(9U);
       },
       "terrain_composite.observation.scene_fog_mode"},
  }};
  for (const auto &hostile : invalid_sampling) {
    auto value = CanonicalObservation();
    hostile.first(value);
    Require(CaptureHostile(owner, {}, value, bytes, value).field ==
                hostile.second,
            "hostile sampling fact returned the wrong blocker");
  }

  for (const AuthoritySubstitution &substitution : AuthoritySubstitutions()) {
    RequireRevalidationRejected(owner, substitution.mutate,
                                substitution.revalidation_message);
  }
  RequireRevalidationRejected(
      owner, [](auto &v) { v.terrain_world_position[1U] = -0.0F; },
      "bitwise terrain transform mutation returned the wrong blocker");
}

void CheckRollback() {
  const Ogre14TerrainCompositeCaptureReceipt sentinel = CaptureCanonical(false);
  for (const auto stage : {
           Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_IDENTITY_CAPTURE,
           Ogre14TerrainCompositeCaptureStage::AFTER_RGBA_ALLOCATION,
           Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_READBACK,
           Ogre14TerrainCompositeCaptureStage::BEFORE_RECEIPT_PUBLICATION,
       }) {
    for (const bool bad_alloc : {false, true}) {
      ThrowingInjector injector;
      injector.target = stage;
      injector.bad_alloc = bad_alloc;
      Ogre14TerrainCompositeCaptureReceipt output = sentinel;
      const auto observation = CanonicalObservation();
      const ValidationResult result = RoR::Render::Testing::
          Ogre14TerrainCompositeCaptureTestAccess::CaptureMipChain(
              {}, observation, CanonicalMipBytes(), observation, output,
              &injector);
      Require(!result.ok() && output.SharesImmutableStateWith(sentinel),
              "test-only injected failure changed published owner");
    }
  }
}

void CheckTransferOracle() {
  const std::array<Ogre14TerrainCompositeOracleTexel, 4U> texels{{
      {{{64U, 10U, 20U, 0U}}},
      {{{128U, 30U, 40U, 64U}}},
      {{{192U, 50U, 60U, 128U}}},
      {{{224U, 70U, 80U, 255U}}},
  }};
  Ogre14TerrainCompositeOracleSample decoded;
  Ogre14TerrainCompositeOracleSample legacy;
  Require(EvaluateOgre14TerrainCompositeBilinearOracle(
              Ogre14TerrainCompositeRgbTransfer::DECODE_BEFORE_FILTER, texels,
              0.5F, 0.5F, decoded)
              .ok() &&
              EvaluateOgre14TerrainCompositeBilinearOracle(
                  Ogre14TerrainCompositeRgbTransfer::
                      LEGACY_UNORM_DISPLAY_DOMAIN,
                  texels, 0.5F, 0.5F, legacy)
                  .ok(),
          "transfer oracle rejected honest inputs");
  // Independently frozen midpoint expectations. Decode each encoded texel
  // before averaging for the first value; average UNORM bytes for the second.
  Require(std::fabs(decoded.rgba[0U] - 0.3849123234F) < 1e-6F &&
              std::fabs(legacy.rgba[0U] - 0.5960784314F) < 1e-6F &&
              std::fabs(decoded.rgba[0U] - legacy.rgba[0U]) > 0.2F &&
              std::fabs(decoded.rgba[3U] - 0.4382352941F) < 1e-6F &&
              std::fabs(decoded.rgba[3U] - legacy.rgba[3U]) < 1e-6F,
          "oracle RGB/alpha transfer rules changed");
  Ogre14TerrainCompositeOracleSample unchanged;
  unchanged.rgba = {9.0F, 8.0F, 7.0F, 6.0F};
  Require(!EvaluateOgre14TerrainCompositeBilinearOracle(
               Ogre14TerrainCompositeRgbTransfer::DECODE_BEFORE_FILTER,
               texels, -0.1F, 0.5F, unchanged)
               .ok() &&
              unchanged.rgba[0U] == 9.0F,
          "oracle hostile failure was not transactional");
}

} // namespace

int main() {
  CheckCanonicalTransportAndLowering();
  CheckConfigurationMipAndAuthorityHostility();
  CheckRollback();
  CheckTransferOracle();
  std::cout << "ogre14 terrain composite transport receipt tests passed\n";
  return EXIT_SUCCESS;
}
