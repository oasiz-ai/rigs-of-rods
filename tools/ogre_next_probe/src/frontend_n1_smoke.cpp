/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Frontend.h"
#include "OgreNextN1Policy.h"
#include "ror_ogre_next_n1_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

constexpr std::uint32_t kWidth = 192U;
constexpr std::uint32_t kHeight = 128U;
constexpr std::uint64_t kRegistryId = UINT64_C(0x4E315F534D4F4B45);
constexpr std::uint64_t kRetirementRegistryId =
    UINT64_C(0x5254345F52455449);

struct Arguments {
  std::string media_root;
  std::string image_path;
  std::string report_path;
  std::string evidence_path;
  bool modern_pbr = false;
};

struct Metrics {
  std::size_t distinct_rgb = 0U;
  std::size_t non_background_pixels = 0U;
  float minimum_luminance = std::numeric_limits<float>::infinity();
  float maximum_luminance = -std::numeric_limits<float>::infinity();
  std::uint64_t fnv1a64 = UINT64_C(14695981039346656037);
  std::uint64_t attachment_fnv1a64 = UINT64_C(14695981039346656037);
  std::vector<std::uint8_t> rgb;
  std::vector<std::uint8_t> attachment_bytes;
};

struct VariantEvidence final {
  std::string name;
  std::string changed_input;
  std::uint64_t asset_sequence = 0U;
  Metrics hdr;
  Metrics sdr;
  std::size_t hdr_changed_pixels = 0U;
  std::size_t sdr_changed_pixels = 0U;
};

struct SmokeResult final {
  Metrics hdr;
  Metrics sdr;
  std::vector<VariantEvidence> variants;
  OgreNextN1TextureAllocationAudit texture_allocations;
  OgreNextN1TextureAllocationAudit replacement_final_audit;
  bool live_replacement_retirement = false;
  struct TextureRetirementEvidence final {
    OgreNextN1TextureAllocationAudit initial;
    OgreNextN1TextureAllocationAudit expanded;
    OgreNextN1TextureAllocationAudit restored;
    OgreNextN1TextureAllocationAudit first_shutdown;
    OgreNextN1TextureAllocationAudit restarted;
    OgreNextN1TextureAllocationAudit final_shutdown;
    OgreNextN1NormalUploadAudit expanded_normal_upload;
    bool exact_extent_and_mip_transitions = false;
    bool renders_through_transitions_and_restart = false;
    bool old_names_rejected = false;
  } retirement;
  struct TextureUploadRollbackStageEvidence final {
    std::string name;
    OgreNextN1TextureAllocationAudit after_failure;
    OgreNextN1TextureAllocationAudit after_retry;
    OgreNextN1TextureAllocationAudit after_replacement;
    OgreNextN1TextureAllocationAudit after_shutdown;
  };
  std::vector<TextureUploadRollbackStageEvidence> texture_upload_rollback;
  struct TangentHandednessEvidence final {
    Metrics positive_hdr;
    Metrics positive_sdr;
    Metrics negative_hdr;
    Metrics negative_sdr;
    std::size_t hdr_changed_pixels = 0U;
    std::size_t sdr_changed_pixels = 0U;
    bool only_tangent_w_changed = false;
  } tangent_handedness;
  bool non_uniform_scale_rejected_before_submission = false;
};

enum class TextureVariant : std::uint8_t {
  BASELINE,
  BASE_COLOR,
  ROUGHNESS_G,
  METALLIC_B,
  EMISSIVE,
  NORMAL_RG,
  SAMPLER_UV,
};

struct VariantSpec final {
  TextureVariant variant;
  const char *name;
  const char *changed_input;
  std::uint64_t sequence;
  std::uint64_t material_revision;
  std::uint64_t base_color_revision;
  std::uint64_t packed_revision;
  std::uint64_t emissive_revision;
  std::uint64_t normal_revision;
  std::uint64_t sampler_revision;
  std::uint64_t expected_native_creates;
  std::uint64_t expected_native_destroys;
};

constexpr std::array<VariantSpec, 7U> kVariantSpecs{{
    {TextureVariant::BASELINE, "baseline", "none", 1U, 1U, 1U, 1U, 1U,
     1U, 1U, 5U, 0U},
    {TextureVariant::BASE_COLOR, "base_color", "base_color_rgb", 2U, 2U,
     2U, 1U, 1U, 1U, 1U, 6U, 1U},
    {TextureVariant::ROUGHNESS_G, "roughness_g", "packed_green_roughness",
     3U, 3U, 3U, 2U, 1U, 1U, 1U, 9U, 4U},
    {TextureVariant::METALLIC_B, "metallic_b", "packed_blue_metallic", 4U,
     4U, 3U, 3U, 1U, 1U, 1U, 11U, 6U},
    {TextureVariant::EMISSIVE, "emissive", "emissive_rgb", 5U, 5U, 3U,
     4U, 2U, 1U, 1U, 14U, 9U},
    {TextureVariant::NORMAL_RG, "normal_rg", "canonical_positive_z_normal_rg",
     6U, 6U, 3U, 4U, 3U, 2U, 1U, 16U, 11U},
    {TextureVariant::SAMPLER_UV, "sampler_uv", "sampler_address_over_uv0",
     7U, 7U, 3U, 4U, 3U, 3U, 2U, 17U, 12U},
}};

[[noreturn]] void Fail(const std::string &message) {
  throw std::runtime_error(message);
}

void Require(bool condition, const std::string &message) {
  if (!condition) {
    Fail(message);
  }
}

void RequireSuccess(const RenderOperationResult &result,
                    const std::string &operation) {
  if (!result.ok()) {
    Fail(operation + " failed: " + result.detail);
  }
}

Arguments ParseArguments(int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--media-root" && index + 1 < argc) {
      arguments.media_root = argv[++index];
    } else if (option == "--output" && index + 1 < argc) {
      arguments.image_path = argv[++index];
    } else if (option == "--report" && index + 1 < argc) {
      arguments.report_path = argv[++index];
    } else if (option == "--evidence" && index + 1 < argc) {
      arguments.evidence_path = argv[++index];
    } else if (option == "--modern-pbr") {
      arguments.modern_pbr = true;
    } else {
      Fail("usage: ror_ogre_next_frontend_n1_smoke --media-root ABSOLUTE_PATH [--modern-pbr --evidence ISOLATION.bin] [--output FRAME.ppm] [--report REPORT.json]");
    }
  }
  if (arguments.media_root.empty()) {
    Fail("--media-root is required for the relocatable N1 frontend");
  }
  if (arguments.modern_pbr && arguments.evidence_path.empty()) {
    Fail("--evidence is required for exact RT4/V1 texture-isolation output");
  }
  return arguments;
}

RenderAssetId AssetId(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x4E315F4153534554), low);
}

RenderAssetReference AssetRef(RenderAssetKind kind, std::uint64_t low,
                              std::uint64_t revision = 1U) {
  return RenderAssetReference::Create(kind, AssetId(low), revision);
}

MeshResourceDescriptor MakeMesh(bool modern_pbr = false) {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "N1 native v2 VAO smoke triangle";
  mesh.index_format = MeshIndexFormat::UINT16;
  mesh.local_bounds.minimum = {-1.15F, -0.85F, 0.0F};
  mesh.local_bounds.maximum = {1.15F, 0.95F, 0.0F};
  mesh.positions = {
      {-1.15F, -0.85F, 0.0F},
      {1.15F, -0.85F, 0.0F},
      {0.0F, 0.95F, 0.0F},
  };
  mesh.normals.assign(mesh.positions.size(), Float3{0.0F, 0.0F, 1.0F});
  if (modern_pbr) {
    mesh.debug_name = "RT4/V1 authored tangent UV0 smoke triangle";
    mesh.tangents.assign(mesh.positions.size(),
                         Float4{1.0F, 0.0F, 0.0F, 1.0F});
    mesh.texture_coordinates_0 = {
        {-0.35F, 1.35F},
        {1.65F, 1.35F},
        {0.5F, -0.35F},
    };
  }
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

MaterialDescriptor MakeMaterial(bool modern_pbr = false,
                                const VariantSpec *variant = nullptr) {
  MaterialDescriptor material;
  material.debug_name = "N1 texture-free emissive metallic-roughness PBS";
  material.base_color_factor = {0.05F, 0.32F, 0.92F, 1.0F};
  material.metallic_factor = 0.2F;
  material.roughness_factor = 0.28F;
  material.double_sided = true;
  material.emissive_factor = {0.78F, 0.12F, 0.035F};
  // The GitHub macOS arm64 runner exposes Apple's paravirtual Metal device.
  // That driver currently produces roughly one third of the physical Apple M5
  // luminance for this texture-free emissive fixture.  Keep the source energy
  // far enough above display white that both devices independently prove the
  // RGBA16_FLOAT attachment is scene-linear and unclamped.  This smoke checks
  // HDR storage, not cross-device photometric parity; that is a separate V1
  // backend-oracle gate.
  material.emissive_strength = 24.0F;
  if (modern_pbr) {
    Require(variant != nullptr, "RT4/V1 material lacks its revision plan");
    material.debug_name = "RT4/V1 texture-backed metallic-roughness PBS";
    material.base_color_factor = {0.7F, 0.8F, 0.9F, 1.0F};
    material.metallic_factor = 0.85F;
    material.roughness_factor = 0.65F;
    material.emissive_factor = {1.0F, 0.7F, 0.4F};
    // Keep the independent texture-backed RT4 fixture above display white on
    // both the hosted paravirtual Metal device and physical Apple silicon.
    // Input isolation still changes exactly one texture role at a time; this
    // scalar is identical across all variants.
    material.emissive_strength = 6.0F;
    material.base_color_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 3U,
                 variant->base_color_revision);
    material.base_color_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U,
                 variant->sampler_revision);
    material.metallic_roughness_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 4U, variant->packed_revision);
    material.metallic_roughness_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U,
                 variant->sampler_revision);
    material.normal_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 9U, variant->normal_revision);
    material.normal_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U,
                 variant->sampler_revision);
    material.normal_scale = 1.0F;
    material.emissive_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 5U,
                 variant->emissive_revision);
    material.emissive_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U,
                 variant->sampler_revision);
  }
  return material;
}

TextureResourceDescriptor MakeTexture(TextureColorSpace color_space,
                                      std::vector<std::uint8_t> rgba) {
  Require(rgba.size() == 16U, "RT4/V1 texture fixture is not 2x2 RGBA8");
  TextureResourceDescriptor texture;
  texture.debug_name = "RT4/V1 padded-row 2x2 texture";
  texture.color_space = color_space;
  texture.width = 2U;
  texture.height = 2U;
  TextureMipLevelDescriptor mip;
  mip.width = 2U;
  mip.height = 2U;
  mip.row_pitch_bytes = 12U;
  mip.layer_pitch_bytes = 24U;
  mip.bytes.assign(24U, 0xCDU);
  std::memcpy(mip.bytes.data(), rgba.data(), 8U);
  std::memcpy(mip.bytes.data() + 12U, rgba.data() + 8U, 8U);
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

TextureResourceDescriptor MakeRetirementTexture(std::uint64_t revision) {
  if (revision == 1U || revision == 3U) {
    std::vector<std::uint8_t> rgba =
        revision == 1U
            ? std::vector<std::uint8_t>{
                  180U, 128U, 244U, 255U, 128U, 180U, 244U, 255U,
                  64U, 128U, 238U, 255U, 128U, 64U, 238U, 255U}
            : std::vector<std::uint8_t>{
                  200U, 128U, 232U, 255U, 128U, 200U, 232U, 255U,
                  160U, 96U, 247U, 255U, 96U, 160U, 247U, 255U};
    TextureResourceDescriptor texture =
        MakeTexture(TextureColorSpace::LINEAR, std::move(rgba));
    texture.debug_name =
        "RT4 normal retirement 2x2 one-mip padded-row texture";
    return texture;
  }
  Require(revision == 2U, "RT4 retirement requested an unknown revision");
  TextureResourceDescriptor texture;
  texture.debug_name =
      "RT4 normal retirement 4x2 two-mip padded-row texture";
  texture.color_space = TextureColorSpace::LINEAR;
  texture.width = 4U;
  texture.height = 2U;

  TextureMipLevelDescriptor level_zero;
  level_zero.width = 4U;
  level_zero.height = 2U;
  level_zero.row_pitch_bytes = 20U;
  level_zero.layer_pitch_bytes = 40U;
  level_zero.bytes.assign(40U, 0xA5U);
  const std::array<std::uint8_t, 32U> level_zero_rgba{{
      180U, 180U, 231U, 255U, 200U, 160U, 227U, 255U,
      160U, 200U, 227U, 255U, 220U, 128U, 215U, 255U,
      128U, 220U, 215U, 255U, 64U, 128U, 238U, 255U,
      128U, 64U, 238U, 255U, 160U, 96U, 247U, 255U,
  }};
  std::memcpy(level_zero.bytes.data(), level_zero_rgba.data(), 16U);
  std::memcpy(level_zero.bytes.data() + 20U,
              level_zero_rgba.data() + 16U, 16U);
  texture.mip_levels.push_back(std::move(level_zero));

  TextureMipLevelDescriptor level_one;
  level_one.width = 2U;
  level_one.height = 1U;
  level_one.row_pitch_bytes = 12U;
  level_one.layer_pitch_bytes = 12U;
  level_one.bytes = {
      96U, 160U, 247U, 255U, 200U, 96U, 228U, 255U,
      0x5AU, 0x5AU, 0x5AU, 0x5AU,
  };
  texture.mip_levels.push_back(std::move(level_one));
  return texture;
}

RenderAssetDelta MakeRetirementCatalog(std::uint64_t revision) {
  RenderAssetDelta delta;
  delta.registry_id = kRetirementRegistryId;
  delta.sequence = revision;
  delta.full_snapshot = true;

  RenderAssetMutation mesh;
  mesh.asset = AssetRef(RenderAssetKind::MESH, 1U);
  mesh.payload = MakeMesh(true);
  delta.mutations.push_back(std::move(mesh));

  RenderAssetMutation texture;
  texture.asset = AssetRef(RenderAssetKind::TEXTURE, 30U, revision);
  texture.payload = MakeRetirementTexture(revision);
  delta.mutations.push_back(std::move(texture));

  SamplerResourceDescriptor sampler_descriptor;
  sampler_descriptor.debug_name = "RT4 isolated retirement sampler";
  sampler_descriptor.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler_descriptor.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler_descriptor.maximum_lod = 1.0F;
  RenderAssetMutation sampler;
  sampler.asset = AssetRef(RenderAssetKind::SAMPLER, 31U);
  sampler.payload = sampler_descriptor;
  delta.mutations.push_back(std::move(sampler));

  MaterialDescriptor material = MakeMaterial();
  material.debug_name = "RT4 isolated retirement normal-map material";
  material.base_color_factor = {0.8F, 0.85F, 0.9F, 1.0F};
  material.metallic_factor = 0.15F;
  material.roughness_factor = 0.42F;
  material.emissive_factor = {0.0F, 0.0F, 0.0F};
  material.emissive_strength = 0.0F;
  material.normal_texture.texture =
      AssetRef(RenderAssetKind::TEXTURE, 30U, revision);
  material.normal_texture.sampler =
      AssetRef(RenderAssetKind::SAMPLER, 31U);
  material.normal_scale = 1.0F;
  RenderAssetMutation material_mutation;
  material_mutation.asset =
      AssetRef(RenderAssetKind::MATERIAL, 32U, revision);
  material_mutation.payload = std::move(material);
  delta.mutations.push_back(std::move(material_mutation));
  return delta;
}

RenderAssetDelta MakeCatalog(bool modern_pbr = false,
                             const VariantSpec *variant = nullptr) {
  if (modern_pbr) {
    Require(variant != nullptr, "RT4/V1 catalog lacks its variant plan");
  }
  RenderAssetDelta delta;
  delta.registry_id = kRegistryId;
  delta.sequence = modern_pbr ? variant->sequence : 1U;
  delta.full_snapshot = true;

  RenderAssetMutation mesh;
  mesh.asset = AssetRef(RenderAssetKind::MESH, 1U);
  mesh.payload = MakeMesh(modern_pbr);
  delta.mutations.push_back(std::move(mesh));

  RenderAssetMutation material;
  material.asset = AssetRef(RenderAssetKind::MATERIAL, 2U,
                            modern_pbr ? variant->material_revision : 1U);
  material.payload = MakeMaterial(modern_pbr, variant);
  delta.mutations.push_back(std::move(material));
  if (modern_pbr) {
    std::vector<std::uint8_t> base_color_bytes{
        255U, 28U, 12U, 255U, 18U, 220U, 42U, 255U,
        24U, 42U, 255U, 255U, 255U, 190U, 30U, 255U};
    if (variant->variant == TextureVariant::BASE_COLOR) {
      base_color_bytes = {
          12U, 238U, 255U, 255U, 245U, 18U, 210U, 255U,
          250U, 220U, 15U, 255U, 20U, 35U, 245U, 255U};
    }
    RenderAssetMutation base_color;
    base_color.asset = AssetRef(RenderAssetKind::TEXTURE, 3U,
                                variant->base_color_revision);
    base_color.payload = MakeTexture(
        TextureColorSpace::SRGB, std::move(base_color_bytes));
    delta.mutations.push_back(std::move(base_color));

    std::vector<std::uint8_t> packed_bytes{
        255U, 32U, 220U, 255U, 255U, 64U, 180U, 255U,
        255U, 96U, 96U, 255U, 255U, 128U, 40U, 255U};
    if (variant->variant == TextureVariant::ROUGHNESS_G) {
      for (std::size_t index = 1U; index < packed_bytes.size(); index += 4U) {
        packed_bytes[index] = 245U;
      }
    } else if (variant->variant == TextureVariant::METALLIC_B) {
      for (std::size_t index = 2U; index < packed_bytes.size(); index += 4U) {
        packed_bytes[index] = 5U;
      }
    }
    RenderAssetMutation metallic_roughness;
    metallic_roughness.asset = AssetRef(RenderAssetKind::TEXTURE, 4U,
                                        variant->packed_revision);
    metallic_roughness.payload = MakeTexture(
        TextureColorSpace::LINEAR, std::move(packed_bytes));
    delta.mutations.push_back(std::move(metallic_roughness));

    std::vector<std::uint8_t> emissive_bytes{
        255U, 96U, 12U, 255U, 18U, 255U, 80U, 255U,
        20U, 90U, 255U, 255U, 255U, 235U, 42U, 255U};
    if (variant->variant == TextureVariant::EMISSIVE) {
      emissive_bytes = {
          8U, 18U, 255U, 255U, 255U, 12U, 18U, 255U,
          20U, 255U, 30U, 255U, 255U, 25U, 220U, 255U};
    }
    RenderAssetMutation emissive;
    emissive.asset = AssetRef(RenderAssetKind::TEXTURE, 5U,
                              variant->emissive_revision);
    emissive.payload = MakeTexture(
        TextureColorSpace::SRGB, std::move(emissive_bytes));
    delta.mutations.push_back(std::move(emissive));

    std::vector<std::uint8_t> normal_bytes{
        128U, 128U, 255U, 255U, 128U, 128U, 255U, 255U,
        128U, 128U, 255U, 255U, 128U, 128U, 255U, 255U};
    if (variant->variant == TextureVariant::NORMAL_RG) {
      normal_bytes = {
          180U, 128U, 244U, 255U, 128U, 180U, 244U, 255U,
          64U, 128U, 238U, 255U, 128U, 64U, 238U, 255U};
    }
    RenderAssetMutation normal;
    normal.asset = AssetRef(RenderAssetKind::TEXTURE, 9U,
                            variant->normal_revision);
    normal.payload = MakeTexture(
        TextureColorSpace::LINEAR, std::move(normal_bytes));

    SamplerResourceDescriptor sampler_descriptor;
    sampler_descriptor.debug_name = "RT4/V1 controlled UV0 sampler";
    sampler_descriptor.address_u =
        variant->variant == TextureVariant::SAMPLER_UV
            ? SamplerAddressMode::MIRRORED_REPEAT
            : SamplerAddressMode::CLAMP_TO_EDGE;
    sampler_descriptor.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
    sampler_descriptor.address_w = SamplerAddressMode::REPEAT;
    sampler_descriptor.maximum_lod = 0.0F;
    RenderAssetMutation sampler;
    sampler.asset = AssetRef(RenderAssetKind::SAMPLER, 6U,
                             variant->sampler_revision);
    sampler.payload = sampler_descriptor;
    delta.mutations.push_back(std::move(sampler));

    TextureResourceDescriptor unreferenced_texture;
    unreferenced_texture.debug_name =
        "shared-catalog unreferenced R8 texture";
    unreferenced_texture.format = TextureResourceFormat::R8_UNORM;
    unreferenced_texture.width = 1U;
    unreferenced_texture.height = 1U;
    TextureMipLevelDescriptor unreferenced_mip;
    unreferenced_mip.width = 1U;
    unreferenced_mip.height = 1U;
    unreferenced_mip.row_pitch_bytes = 1U;
    unreferenced_mip.layer_pitch_bytes = 1U;
    unreferenced_mip.bytes = {127U};
    unreferenced_texture.mip_levels.push_back(std::move(unreferenced_mip));
    RenderAssetMutation unreferenced_texture_mutation;
    unreferenced_texture_mutation.asset =
        AssetRef(RenderAssetKind::TEXTURE, 7U);
    unreferenced_texture_mutation.payload = std::move(unreferenced_texture);
    delta.mutations.push_back(std::move(unreferenced_texture_mutation));

    SamplerResourceDescriptor unreferenced_sampler_descriptor;
    unreferenced_sampler_descriptor.debug_name =
        "shared-catalog unreferenced border sampler";
    unreferenced_sampler_descriptor.address_u =
        SamplerAddressMode::CLAMP_TO_BORDER;
    RenderAssetMutation unreferenced_sampler;
    unreferenced_sampler.asset = AssetRef(RenderAssetKind::SAMPLER, 8U);
    unreferenced_sampler.payload = unreferenced_sampler_descriptor;
    delta.mutations.push_back(std::move(unreferenced_sampler));
    delta.mutations.push_back(std::move(normal));
  }
  return delta;
}

const RenderAssetMutation &MutationFor(const RenderAssetDelta &catalog,
                                       std::uint64_t low) {
  const auto found = std::find_if(
      catalog.mutations.begin(), catalog.mutations.end(),
      [low](const RenderAssetMutation &mutation) {
        return mutation.asset.id == AssetId(low);
      });
  Require(found != catalog.mutations.end(),
          "RT4/V1 controlled catalog lost a required asset");
  return *found;
}

void RequireEquivalentPayload(const RenderAssetPayload &lhs,
                              const RenderAssetPayload &rhs,
                              const std::string &label);

RenderAssetDelta MakeTangentHandednessCatalog(bool negative) {
  RenderAssetDelta catalog = MakeCatalog(true, &kVariantSpecs.front());
  catalog.sequence = negative ? 2U : 1U;
  for (RenderAssetMutation &mutation : catalog.mutations) {
    if (mutation.asset.id == AssetId(1U)) {
      MeshResourceDescriptor &mesh =
          std::get<MeshResourceDescriptor>(mutation.payload);
      for (Float4 &tangent : mesh.tangents) {
        tangent.w = negative ? -1.0F : 1.0F;
      }
      mutation.asset = AssetRef(RenderAssetKind::MESH, 1U,
                                negative ? 2U : 1U);
    } else if (mutation.asset.id == AssetId(9U)) {
      TextureResourceDescriptor &normal =
          std::get<TextureResourceDescriptor>(mutation.payload);
      constexpr std::array<std::uint8_t, 16U> kAsymmetricNormal{{
          180U, 180U, 231U, 255U, 160U, 200U, 227U, 255U,
          96U, 160U, 247U, 255U, 128U, 220U, 215U, 255U,
      }};
      TextureMipLevelDescriptor &mip = normal.mip_levels.front();
      std::memcpy(mip.bytes.data(), kAsymmetricNormal.data(), 8U);
      std::memcpy(mip.bytes.data() + mip.row_pitch_bytes,
                  kAsymmetricNormal.data() + 8U, 8U);
    }
  }
  return catalog;
}

void RequireControlledTangentHandednessCatalogs(
    const RenderAssetDelta &positive, const RenderAssetDelta &negative) {
  Require(positive.registry_id == negative.registry_id &&
              positive.sequence == 1U && negative.sequence == 2U &&
              positive.full_snapshot && negative.full_snapshot &&
              positive.mutations.size() == negative.mutations.size(),
          "RT4 tangent-handedness catalog envelope changed");
  for (std::size_t index = 0U; index < positive.mutations.size(); ++index) {
    const RenderAssetMutation &expected = positive.mutations[index];
    const RenderAssetMutation &actual = negative.mutations[index];
    Require(expected.asset.id == actual.asset.id &&
                expected.asset.kind == actual.asset.kind,
            "RT4 tangent-handedness asset identity changed");
    if (expected.asset.id != AssetId(1U)) {
      Require(expected.asset == actual.asset,
              "RT4 tangent-handedness changed a non-mesh revision");
      RequireEquivalentPayload(expected.payload, actual.payload,
                               "tangent-handedness non-mesh payload");
      continue;
    }
    Require(expected.asset.revision == 1U && actual.asset.revision == 2U,
            "RT4 tangent-handedness mesh revision plan drifted");
    const MeshResourceDescriptor &positive_mesh =
        std::get<MeshResourceDescriptor>(expected.payload);
    MeshResourceDescriptor normalized_negative =
        std::get<MeshResourceDescriptor>(actual.payload);
    Require(positive_mesh.tangents.size() == normalized_negative.tangents.size(),
            "RT4 tangent-handedness stream size changed");
    for (std::size_t tangent_index = 0U;
         tangent_index < positive_mesh.tangents.size(); ++tangent_index) {
      Require(positive_mesh.tangents[tangent_index].w == 1.0F &&
                  normalized_negative.tangents[tangent_index].w == -1.0F,
              "RT4 tangent-handedness fixture does not use exact signs");
      normalized_negative.tangents[tangent_index].w = 1.0F;
    }
    RequireEquivalentPayload(RenderAssetPayload{positive_mesh},
                             RenderAssetPayload{normalized_negative},
                             "tangent-handedness mesh attributes");
  }
}

void RequireEquivalentPayload(const RenderAssetPayload &lhs,
                              const RenderAssetPayload &rhs,
                              const std::string &label) {
  Require(EquivalentRenderAssetPayload(lhs, rhs),
          "RT4/V1 controlled " + label + " changed unexpectedly");
}

void RequireTextureOnlyChannelChange(
    const TextureResourceDescriptor &baseline,
    const TextureResourceDescriptor &variant, std::size_t allowed_channel,
    const std::string &label) {
  TextureResourceDescriptor normalized = variant;
  normalized.mip_levels = baseline.mip_levels;
  RequireEquivalentPayload(RenderAssetPayload{baseline},
                           RenderAssetPayload{normalized}, label + " metadata");
  Require(baseline.mip_levels.size() == variant.mip_levels.size(),
          "RT4/V1 controlled texture mip count changed");
  std::size_t changed = 0U;
  for (std::size_t mip_index = 0U;
       mip_index < baseline.mip_levels.size(); ++mip_index) {
    const TextureMipLevelDescriptor &expected =
        baseline.mip_levels[mip_index];
    const TextureMipLevelDescriptor &actual = variant.mip_levels[mip_index];
    Require(expected.bytes.size() == actual.bytes.size(),
            "RT4/V1 controlled texture byte count changed");
    for (std::size_t offset = 0U; offset < expected.bytes.size(); ++offset) {
      if (expected.bytes[offset] == actual.bytes[offset]) {
        continue;
      }
      const std::size_t row = offset / expected.row_pitch_bytes;
      const std::size_t row_offset = offset % expected.row_pitch_bytes;
      Require(row < expected.height && row_offset < expected.width * 4U &&
                  row_offset % 4U == allowed_channel,
              "RT4/V1 controlled texture changed padding, alpha, or the wrong packed channel");
      ++changed;
    }
  }
  Require(changed > 0U,
          "RT4/V1 controlled " + label + " variant changed no texels");
}

void RequireControlledCatalog(const RenderAssetDelta &baseline,
                              const RenderAssetDelta &variant,
                              const VariantSpec &spec) {
  Require(variant.registry_id == baseline.registry_id &&
              variant.full_snapshot &&
              variant.mutations.size() == baseline.mutations.size() &&
              variant.sequence == spec.sequence,
          "RT4/V1 controlled catalog envelope changed");
  RequireEquivalentPayload(MutationFor(baseline, 1U).payload,
                           MutationFor(variant, 1U).payload,
                           "geometry, normals, tangents, or UV0");

  const MaterialDescriptor &baseline_material =
      std::get<MaterialDescriptor>(MutationFor(baseline, 2U).payload);
  MaterialDescriptor normalized_material =
      std::get<MaterialDescriptor>(MutationFor(variant, 2U).payload);
  normalized_material.base_color_texture =
      baseline_material.base_color_texture;
  normalized_material.metallic_roughness_texture =
      baseline_material.metallic_roughness_texture;
  normalized_material.normal_texture = baseline_material.normal_texture;
  normalized_material.occlusion_texture = baseline_material.occlusion_texture;
  normalized_material.emissive_texture = baseline_material.emissive_texture;
  RequireEquivalentPayload(RenderAssetPayload{baseline_material},
                           RenderAssetPayload{normalized_material},
                           "material factors or constants");

  const std::uint64_t changed_texture =
      spec.variant == TextureVariant::BASE_COLOR
          ? 3U
          : spec.variant == TextureVariant::ROUGHNESS_G ||
                    spec.variant == TextureVariant::METALLIC_B
                ? 4U
                : spec.variant == TextureVariant::EMISSIVE
                      ? 5U
                      : spec.variant == TextureVariant::NORMAL_RG ? 9U : 0U;
  constexpr std::array<std::uint64_t, 4U> kControlledTextureIds{{3U, 4U,
                                                                 5U, 9U}};
  for (const std::uint64_t low : kControlledTextureIds) {
    const TextureResourceDescriptor &expected =
        std::get<TextureResourceDescriptor>(MutationFor(baseline, low).payload);
    const TextureResourceDescriptor &actual =
        std::get<TextureResourceDescriptor>(MutationFor(variant, low).payload);
    if (low != changed_texture) {
      RequireEquivalentPayload(RenderAssetPayload{expected},
                               RenderAssetPayload{actual},
                               "non-target texture");
      continue;
    }
    const std::size_t allowed_channel =
        spec.variant == TextureVariant::ROUGHNESS_G
            ? 1U
            : spec.variant == TextureVariant::METALLIC_B ? 2U : 0U;
    if (allowed_channel == 0U) {
      TextureResourceDescriptor normalized = actual;
      normalized.mip_levels = expected.mip_levels;
      RequireEquivalentPayload(RenderAssetPayload{expected},
                               RenderAssetPayload{normalized},
                               "target texture metadata");
      std::size_t changed_rgb = 0U;
      for (std::size_t mip_index = 0U;
           mip_index < expected.mip_levels.size(); ++mip_index) {
        const TextureMipLevelDescriptor &expected_mip =
            expected.mip_levels[mip_index];
        const TextureMipLevelDescriptor &actual_mip =
            actual.mip_levels[mip_index];
        Require(expected_mip.bytes.size() == actual_mip.bytes.size(),
                "RT4/V1 target texture byte count changed");
        for (std::size_t offset = 0U; offset < expected_mip.bytes.size();
             ++offset) {
          if (expected_mip.bytes[offset] == actual_mip.bytes[offset]) {
            continue;
          }
          const std::size_t row = offset / expected_mip.row_pitch_bytes;
          const std::size_t row_offset = offset % expected_mip.row_pitch_bytes;
          Require(row < expected_mip.height &&
                      row_offset < expected_mip.width * 4U &&
                      row_offset % 4U < 3U,
                  "RT4/V1 RGB texture variant changed padding or alpha");
          ++changed_rgb;
        }
      }
      Require(changed_rgb > 0U,
              "RT4/V1 RGB texture variant changed no RGB texels");
    } else {
      RequireTextureOnlyChannelChange(expected, actual, allowed_channel,
                                      spec.name);
    }
  }

  const SamplerResourceDescriptor &baseline_sampler =
      std::get<SamplerResourceDescriptor>(MutationFor(baseline, 6U).payload);
  SamplerResourceDescriptor normalized_sampler =
      std::get<SamplerResourceDescriptor>(MutationFor(variant, 6U).payload);
  if (spec.variant == TextureVariant::SAMPLER_UV) {
    Require(normalized_sampler.address_u != baseline_sampler.address_u,
            "RT4/V1 sampler/UV variant changed no addressing state");
    normalized_sampler.address_u = baseline_sampler.address_u;
  }
  RequireEquivalentPayload(RenderAssetPayload{baseline_sampler},
                           RenderAssetPayload{normalized_sampler},
                           "non-target sampler state");
  RequireEquivalentPayload(MutationFor(baseline, 7U).payload,
                           MutationFor(variant, 7U).payload,
                           "unreferenced shared-catalog texture");

  Require(MutationFor(variant, 2U).asset.revision ==
              spec.material_revision &&
              MutationFor(variant, 3U).asset.revision ==
                  spec.base_color_revision &&
              MutationFor(variant, 4U).asset.revision ==
                  spec.packed_revision &&
              MutationFor(variant, 5U).asset.revision ==
                  spec.emissive_revision &&
              MutationFor(variant, 9U).asset.revision ==
                  spec.normal_revision &&
              MutationFor(variant, 6U).asset.revision ==
                  spec.sampler_revision,
          "RT4/V1 controlled replacement revision plan drifted");
}

std::shared_ptr<const SceneSnapshot> MakeScene(std::uint64_t snapshot_id,
                                               bool shifted = false,
                                               bool modern_pbr = false,
                                               std::uint64_t asset_sequence = 1U,
                                               std::uint64_t material_revision = 1U,
                                               Matrix4x4 render_from_object =
                                                   Matrix4x4{},
                                               std::uint64_t mesh_revision = 1U,
                                               Float3 light_direction =
                                                   {0.0F, 0.0F, -1.0F}) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = asset_sequence;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds = static_cast<double>(snapshot_id) / 48.0;
  descriptor.environment.ambient_radiance = {0.03F, 0.04F, 0.055F};
  if (modern_pbr) {
    descriptor.environment.ambient_radiance = {0.01F, 0.012F, 0.015F};
    LightDescriptor light;
    light.light_id = 1U;
    light.type = LightType::DIRECTIONAL;
    Require(NormalizePhotometricColorLinear({1.0F, 0.92F, 0.82F},
                                            light.color_linear),
            "RT4/V1 directional tint could not be normalized");
    light.intensity = 1024.0F;
    light.direction = light_direction;
    light.shadow_flags = 0U;
    descriptor.lights.push_back(light);
  }

  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = AssetRef(RenderAssetKind::MESH, 1U, mesh_revision);
  instance.material = AssetRef(RenderAssetKind::MATERIAL, 2U,
                               material_revision);
  instance.render_from_object = render_from_object;
  if (shifted) {
    instance.render_from_object.elements[12U] = 0.15F;
  }
  instance.previous_render_from_object = instance.render_from_object;
  instance.local_bounds = MakeMesh(modern_pbr).local_bounds;
  descriptor.mesh_instances.push_back(instance);

  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  if (!result) {
    Fail("could not create N1 smoke scene: " + result.validation.field +
         ": " + result.validation.detail);
  }
  return result.snapshot;
}

std::shared_ptr<const SceneSnapshot>
MakeRetirementScene(std::uint64_t revision) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 900U + revision;
  descriptor.asset_registry_id = kRetirementRegistryId;
  descriptor.asset_sequence = revision;
  descriptor.simulation_tick = revision;
  descriptor.simulation_time_seconds = static_cast<double>(revision) / 48.0;
  descriptor.environment.ambient_radiance = {0.01F, 0.012F, 0.015F};
  LightDescriptor light;
  light.light_id = 1U;
  light.type = LightType::DIRECTIONAL;
  Require(NormalizePhotometricColorLinear({1.0F, 0.92F, 0.82F},
                                          light.color_linear),
          "RT4 retirement directional tint could not be normalized");
  light.intensity = 1024.0F;
  light.direction = {0.0F, 0.0F, -1.0F};
  light.shadow_flags = 0U;
  descriptor.lights.push_back(light);

  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = AssetRef(RenderAssetKind::MESH, 1U);
  instance.material = AssetRef(RenderAssetKind::MATERIAL, 32U, revision);
  instance.local_bounds = MakeMesh(true).local_bounds;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  if (!result) {
    Fail("could not create RT4 retirement scene: " + result.validation.field +
         ": " + result.validation.detail);
  }
  return result.snapshot;
}

Matrix4x4 Projection() {
  constexpr float near_plane = 0.1F;
  constexpr float far_plane = 20.0F;
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.5F;
  projection.elements[10U] = far_plane / (near_plane - far_plane);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      near_plane * far_plane / (near_plane - far_plane);
  return projection;
}

RenderFrameRequest MakeFrame(
    std::uint64_t frame_id,
    const std::shared_ptr<const SceneSnapshot> &scene,
    PixelFormat format) {
  RenderFrameRequest request;
  request.frame_id = frame_id;
  request.scene_snapshot = scene;
  request.present = false;
  request.color_format = format;
  CameraViewRequest view;
  view.view_id = 1U;
  view.width = kWidth;
  view.height = kHeight;
  view.near_plane = 0.1F;
  view.far_plane = 20.0F;
  view.view_from_render.elements[14U] = -3.0F;
  view.previous_view_from_render = view.view_from_render;
  view.clip_from_view = Projection();
  view.previous_clip_from_view = view.clip_from_view;
  request.views.push_back(view);
  return request;
}

void RequireControlledSceneAndView(const SceneSnapshot &baseline_scene,
                                   const SceneSnapshot &variant_scene,
                                   const RenderFrameRequest &baseline_frame,
                                   const RenderFrameRequest &variant_frame,
                                   bool allow_mesh_revision_change = false) {
  const SceneEnvironmentDescriptor &expected = baseline_scene.environment();
  const SceneEnvironmentDescriptor &actual = variant_scene.environment();
  Require(expected.ambient_radiance == actual.ambient_radiance &&
              expected.environment_intensity ==
                  actual.environment_intensity &&
              expected.environment_texture == actual.environment_texture &&
              expected.environment_sampler == actual.environment_sampler &&
              baseline_scene.lights().size() == 1U &&
              variant_scene.lights().size() == 1U,
          "RT4/V1 controlled environment or lights changed");
  const LightDescriptor &expected_light = baseline_scene.lights().front();
  const LightDescriptor &actual_light = variant_scene.lights().front();
  Require(expected_light.light_id == actual_light.light_id &&
              expected_light.type == actual_light.type &&
              expected_light.color_linear == actual_light.color_linear &&
              expected_light.intensity == actual_light.intensity &&
              expected_light.position == actual_light.position &&
              expected_light.previous_position ==
                  actual_light.previous_position &&
              expected_light.direction == actual_light.direction &&
              expected_light.previous_direction ==
                  actual_light.previous_direction &&
              expected_light.range == actual_light.range &&
              expected_light.inner_cone_radians ==
                  actual_light.inner_cone_radians &&
              expected_light.outer_cone_radians ==
                  actual_light.outer_cone_radians &&
              expected_light.shadow_flags == actual_light.shadow_flags,
          "RT4/V1 controlled directional light changed");
  Require(baseline_scene.mesh_instances().size() == 1U &&
              variant_scene.mesh_instances().size() == 1U,
          "RT4/V1 controlled instance count changed");
  const MeshInstanceDescriptor &expected_instance =
      baseline_scene.mesh_instances().front();
  const MeshInstanceDescriptor &actual_instance =
      variant_scene.mesh_instances().front();
  Require(expected_instance.instance_id == actual_instance.instance_id &&
              expected_instance.mesh.id == actual_instance.mesh.id &&
              expected_instance.mesh.kind == actual_instance.mesh.kind &&
              (allow_mesh_revision_change ||
               expected_instance.mesh.revision ==
                   actual_instance.mesh.revision) &&
              expected_instance.material.id == actual_instance.material.id &&
              expected_instance.material.kind == actual_instance.material.kind &&
              expected_instance.render_from_object ==
                  actual_instance.render_from_object &&
              expected_instance.previous_render_from_object ==
                  actual_instance.previous_render_from_object &&
              expected_instance.local_bounds == actual_instance.local_bounds &&
              expected_instance.visibility_mask ==
                  actual_instance.visibility_mask &&
              expected_instance.flags == actual_instance.flags,
          "RT4/V1 controlled geometry or transform changed");
  Require(baseline_frame.color_format == variant_frame.color_format &&
              baseline_frame.present == variant_frame.present &&
              baseline_frame.requested_outputs ==
                  variant_frame.requested_outputs &&
              baseline_frame.views.size() == 1U &&
              variant_frame.views.size() == 1U,
          "RT4/V1 controlled frame envelope changed");
  const CameraViewRequest &expected_view = baseline_frame.views.front();
  const CameraViewRequest &actual_view = variant_frame.views.front();
  Require(expected_view.view_id == actual_view.view_id &&
              expected_view.width == actual_view.width &&
              expected_view.height == actual_view.height &&
              expected_view.near_plane == actual_view.near_plane &&
              expected_view.far_plane == actual_view.far_plane &&
              expected_view.view_from_render == actual_view.view_from_render &&
              expected_view.previous_view_from_render ==
                  actual_view.previous_view_from_render &&
              expected_view.clip_from_view == actual_view.clip_from_view &&
              expected_view.previous_clip_from_view ==
                  actual_view.previous_clip_from_view &&
              expected_view.exposure == actual_view.exposure &&
              expected_view.temporal_jitter_pixels ==
                  actual_view.temporal_jitter_pixels &&
              expected_view.visibility_mask == actual_view.visibility_mask,
          "RT4/V1 controlled camera changed");
}

std::size_t CountChangedPixels(const std::vector<std::uint8_t> &baseline,
                               const std::vector<std::uint8_t> &variant,
                               std::size_t bytes_per_pixel) {
  Require(baseline.size() == variant.size() && bytes_per_pixel != 0U &&
              baseline.size() % bytes_per_pixel == 0U,
          "RT4/V1 evidence attachment layout changed");
  std::size_t changed = 0U;
  for (std::size_t offset = 0U; offset < baseline.size();
       offset += bytes_per_pixel) {
    if (!std::equal(baseline.begin() + static_cast<std::ptrdiff_t>(offset),
                    baseline.begin() + static_cast<std::ptrdiff_t>(
                                           offset + bytes_per_pixel),
                    variant.begin() + static_cast<std::ptrdiff_t>(offset))) {
      ++changed;
    }
  }
  return changed;
}

float HalfToFloat(std::uint16_t half) {
  const bool negative = (half & 0x8000U) != 0U;
  const std::uint16_t exponent = (half >> 10U) & 0x1FU;
  const std::uint16_t mantissa = half & 0x03FFU;
  float value = 0.0F;
  if (exponent == 0U) {
    value = mantissa == 0U
                ? 0.0F
                : std::ldexp(static_cast<float>(mantissa), -24);
  } else if (exponent == 0x1FU) {
    value = mantissa == 0U
                ? std::numeric_limits<float>::infinity()
                : std::numeric_limits<float>::quiet_NaN();
  } else {
    value = std::ldexp(1.0F + static_cast<float>(mantissa) / 1024.0F,
                       static_cast<int>(exponent) - 15);
  }
  return negative ? -value : value;
}

std::uint8_t Quantize(float value) {
  const float clamped = std::max(0.0F, std::min(1.0F, value));
  return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
}

void Hash(std::uint64_t &hash, std::uint8_t value) {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

std::uint64_t HashBytes(const std::vector<std::uint8_t> &bytes) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const std::uint8_t value : bytes) {
    Hash(hash, value);
  }
  return hash;
}

std::string HexHash(std::uint64_t hash);

void Accumulate(Metrics &metrics, std::map<std::uint32_t, std::size_t> &runs,
                float red, float green, float blue) {
  const std::uint8_t r = Quantize(red);
  const std::uint8_t g = Quantize(green);
  const std::uint8_t b = Quantize(blue);
  Hash(metrics.fnv1a64, r);
  Hash(metrics.fnv1a64, g);
  Hash(metrics.fnv1a64, b);
  const std::uint32_t packed = (static_cast<std::uint32_t>(r) << 16U) |
                               (static_cast<std::uint32_t>(g) << 8U) |
                               static_cast<std::uint32_t>(b);
  ++runs[packed];
  const float luminance = 0.2126F * red + 0.7152F * green + 0.0722F * blue;
  metrics.minimum_luminance = std::min(metrics.minimum_luminance, luminance);
  metrics.maximum_luminance = std::max(metrics.maximum_luminance, luminance);
}

void FinishMetrics(Metrics &metrics,
                   const std::map<std::uint32_t, std::size_t> &runs) {
  metrics.distinct_rgb = runs.size();
  std::size_t largest_run = 0U;
  for (const auto &run : runs) {
    largest_run = std::max(largest_run, run.second);
  }
  metrics.non_background_pixels =
      static_cast<std::size_t>(kWidth) * kHeight - largest_run;
}

const FrameAttachment &RequireAttachment(const RenderFrameOutput &output,
                                         PixelFormat format) {
  Require(output.status == RenderFrameStatus::RENDERED && !output.presented,
          "N1 returned a non-rendered or presented frame");
  Require(output.attachments.size() == 1U,
          "N1 did not return exactly one color attachment");
  const FrameAttachment &attachment = output.attachments.front();
  Require(attachment.output == FrameOutputMask::COLOR &&
              attachment.format == format && attachment.width == kWidth &&
              attachment.height == kHeight &&
              !attachment.gpu_resource.valid(),
          "N1 attachment metadata does not match the CPU-only request");
  return attachment;
}

Metrics InspectHdr(const RenderFrameOutput &output) {
  const FrameAttachment &attachment =
      RequireAttachment(output, PixelFormat::RGBA16_FLOAT);
  Require(attachment.row_pitch_bytes == static_cast<std::uint64_t>(kWidth) * 8U,
          "HDR readback is not tightly packed");
  Require(attachment.bytes.size() ==
              static_cast<std::size_t>(attachment.row_pitch_bytes) * kHeight,
          "HDR readback byte count is incomplete");
  Metrics metrics;
  metrics.attachment_bytes = attachment.bytes;
  metrics.attachment_fnv1a64 = HashBytes(metrics.attachment_bytes);
  std::map<std::uint32_t, std::size_t> runs;
  for (std::size_t pixel = 0U;
       pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
    float channels[4U]{};
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
      std::uint16_t half = 0U;
      std::memcpy(&half, attachment.bytes.data() + pixel * 8U + channel * 2U,
                  sizeof(half));
      channels[channel] = HalfToFloat(half);
      Require(std::isfinite(channels[channel]),
              "HDR GPU readback contains a non-finite half-float");
    }
    Require(channels[3U] >= 0.99F && channels[3U] <= 1.01F,
            "HDR GPU readback alpha is not straight opaque alpha");
    Accumulate(metrics, runs, channels[0U], channels[1U], channels[2U]);
  }
  FinishMetrics(metrics, runs);
  if (metrics.distinct_rgb < 2U || metrics.non_background_pixels < 512U) {
    std::ostringstream detail;
    detail << "HDR readback does not prove scene geometry over the clear color"
           << " (distinct=" << metrics.distinct_rgb
           << ", foreground=" << metrics.non_background_pixels
           << ", min=" << metrics.minimum_luminance
           << ", max=" << metrics.maximum_luminance << ')';
    Fail(detail.str());
  }
  if (metrics.maximum_luminance <= 1.05F) {
    std::ostringstream detail;
    detail << "RGBA16_FLOAT readback did not preserve scene-referred HDR energy"
           << " (distinct=" << metrics.distinct_rgb
           << ", foreground=" << metrics.non_background_pixels
           << ", min=" << metrics.minimum_luminance
           << ", max=" << metrics.maximum_luminance
           << ", hash=" << HexHash(metrics.fnv1a64) << ')';
    Fail(detail.str());
  }
  return metrics;
}

Metrics InspectSdr(const RenderFrameOutput &output) {
  const FrameAttachment &attachment =
      RequireAttachment(output, PixelFormat::RGBA8_SRGB);
  Require(attachment.row_pitch_bytes == static_cast<std::uint64_t>(kWidth) * 4U,
          "SDR readback is not tightly packed");
  Require(attachment.bytes.size() ==
              static_cast<std::size_t>(attachment.row_pitch_bytes) * kHeight,
          "SDR readback byte count is incomplete");
  Metrics metrics;
  metrics.attachment_bytes = attachment.bytes;
  metrics.attachment_fnv1a64 = HashBytes(metrics.attachment_bytes);
  metrics.rgb.reserve(static_cast<std::size_t>(kWidth) * kHeight * 3U);
  std::map<std::uint32_t, std::size_t> runs;
  for (std::size_t pixel = 0U;
       pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
    const std::uint8_t *rgba = attachment.bytes.data() + pixel * 4U;
    Require(rgba[3U] >= 250U, "SDR GPU readback alpha is not opaque");
    metrics.rgb.push_back(rgba[0U]);
    metrics.rgb.push_back(rgba[1U]);
    metrics.rgb.push_back(rgba[2U]);
    Accumulate(metrics, runs, static_cast<float>(rgba[0U]) / 255.0F,
               static_cast<float>(rgba[1U]) / 255.0F,
               static_cast<float>(rgba[2U]) / 255.0F);
  }
  FinishMetrics(metrics, runs);
  Require(metrics.distinct_rgb >= 2U && metrics.non_background_pixels >= 512U,
          "SDR readback does not prove scene geometry over the clear color");
  Require(metrics.maximum_luminance - metrics.minimum_luminance > 0.05F,
          "SDR readback has no meaningful foreground/background contrast");
  return metrics;
}

void WriteText(const std::string &path, const std::string &text) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open output: " + path);
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!output) {
    Fail("could not write output: " + path);
  }
}

void WritePpm(const std::string &path, const Metrics &metrics) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open frame output: " + path);
  }
  output << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
  output.write(reinterpret_cast<const char *>(metrics.rgb.data()),
               static_cast<std::streamsize>(metrics.rgb.size()));
  if (!output) {
    Fail("could not write frame output: " + path);
  }
}

void WriteIsolationEvidence(const std::string &path,
                            const SmokeResult &result) {
  Require(!path.empty(), "RT4/V1 isolation evidence path is empty");
  Require(result.variants.size() == kVariantSpecs.size(),
          "RT4/V1 isolation evidence is incomplete");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open RT4/V1 isolation evidence: " + path);
  }
  const auto write_attachment = [&](const Metrics &metrics) {
    output.write(
        reinterpret_cast<const char *>(metrics.attachment_bytes.data()),
        static_cast<std::streamsize>(metrics.attachment_bytes.size()));
  };
  for (const VariantEvidence &variant : result.variants) {
    write_attachment(variant.hdr);
    write_attachment(variant.sdr);
  }
  write_attachment(result.tangent_handedness.positive_hdr);
  write_attachment(result.tangent_handedness.positive_sdr);
  write_attachment(result.tangent_handedness.negative_hdr);
  write_attachment(result.tangent_handedness.negative_sdr);
  if (!output) {
    Fail("could not write complete RT4/V1 isolation evidence: " + path);
  }
}

std::string HexHash(std::uint64_t hash) {
  std::ostringstream value;
  value << std::hex << std::setfill('0') << std::setw(16) << hash;
  return value.str();
}

std::string MakeReport(const SmokeResult &result, bool modern_pbr,
                       const std::string &evidence_path) {
  const Metrics &hdr = result.hdr;
  const Metrics &sdr = result.sdr;
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \""
         << (modern_pbr ? "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v1"
                        : "ror.ogre_next_frontend_n1_smoke.v1")
         << "\",\n"
         << "  \"status\": \"pass\",\n"
         << (modern_pbr
                 ? std::string("  \"executable_build_identity\": \"") +
                       ROR_OGRE_NEXT_N1_BUILD_IDENTITY + "\",\n"
                 : std::string())
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \""
         << ROR_OGRE_NEXT_N1_ROR_REPOSITORY << "\",\n"
         << "    \"ror_ref\": \"" << ROR_OGRE_NEXT_N1_ROR_REF
         << "\",\n"
         << "    \"ror_commit\": \"" << ROR_OGRE_NEXT_N1_ROR_COMMIT
         << "\",\n"
         << "    \"ror_relevant_source_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N1_ROR_SOURCE_MANIFEST_SHA256 << "\",\n"
         << "    \"ror_relevant_source_manifest_file_count\": "
         << ROR_OGRE_NEXT_N1_ROR_SOURCE_MANIFEST_FILE_COUNT << ",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "    \"ogre_next_archive_sha256\": \""
         << ROR_OGRE_NEXT_N1_OGRE_ARCHIVE_SHA256 << "\",\n"
         << "    \"normal_map_source_lock_sha256\": \""
         << ROR_OGRE_NEXT_N1_NORMAL_MAP_SOURCE_LOCK_SHA256 << "\",\n"
         << "    \"shader_media_root\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_ROOT << "\",\n"
         << "    \"shader_media_license_expression\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_LICENSE_EXPRESSION << "\",\n"
         << "    \"shader_media_notice_path\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_NOTICE_PATH << "\",\n"
         << "    \"shader_media_notice_sha256\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_NOTICE_SHA256 << "\",\n"
         << "    \"shader_media_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_MANIFEST_SHA256 << "\",\n"
         << "    \"shader_media_manifest_file_count\": "
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_MANIFEST_FILE_COUNT << "\n"
         << "  },\n"
         << "  \"platform_policy\": \""
         << ROR_OGRE_NEXT_N1_PLATFORM_POLICY << "\",\n"
         << "  \"renderer\": \"" << ROR_OGRE_NEXT_N1_RENDERER_NAME
         << "\",\n"
         << "  \"adapter\": {\n"
         << "    \"frontend_version\": \"" << ROR_OGRE_NEXT_N1_VERSION
         << "\",\n"
         << "    \"native_mesh_path\": \"Ogre v2 Mesh plus immutable VertexArrayObject\",\n"
         << "    \"material_path\": \"HLMS PBS metallic-roughness\",\n"
         << "    \"brdf\": \"PbsBrdf::Default height-correlated GGX\",\n"
         << "    \"pbr_datablock_readback_verified\": true,\n";
  if (modern_pbr) {
    report
        << "    \"raster_feature_tier\": \"MODERN_PBR_RT4_V1\",\n"
        << "    \"vertex_layout\": \"position_normal_tangent_uv0\",\n"
        << "    \"base_color_upload\": \"RGBA8_UNORM_SRGB\",\n"
        << "    \"metallic_roughness_upload\": \"linear_G_to_R8_roughness_B_to_R8_metallic\",\n"
        << "    \"emissive_upload\": \"RGBA8_UNORM_SRGB\",\n"
        << "    \"normal_upload\": \"linear_RGBA8_positive_Z_to_RG8_UNORM\",\n"
        << "    \"padded_source_rows_verified\": true,\n"
        << "    \"portable_sampler_mapping_verified\": true,\n"
        << "    \"normal_texture_admitted\": true,\n"
        << "    \"normal_slot\": \"PBSM_NORMAL\",\n"
        << "    \"normal_uv_source\": 0,\n"
        << "    \"normal_scale\": 1,\n"
        << "    \"normal_map_weight\": 1,\n"
        << "    \"normal_positive_z_tolerance_decoded\": \"1/255\",\n"
        << "    \"occlusion_texture_admitted\": false,\n"
        << "    \"occlusion_blocker\": \"pinned_HLMS_PBS_has_no_ambient_only_AO_slot\",\n";
  }
  report
         << "    \"runtime_media_root\": \"explicit_absolute\",\n"
         << "    \"package_media_relative_path\": \""
         << ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE << "\",\n"
         << "    \"relocated_executable\": true,\n"
         << "    \"compositor2\": true,\n"
         << "    \"ui_included\": false,\n"
         << "    \"cpu_readback_completed\": true,\n";
  if (modern_pbr) {
    report << "    \"analytic_lights_calibrated\": true,\n"
           << "    \"directional_lux_to_native_power_scale\": 0.0009765625,\n"
           << "    \"maximum_directional_lights\": 1,\n"
           << "    \"constant_environment_only\": false,\n";
  } else {
    report << "    \"analytic_lights_calibrated\": false,\n"
           << "    \"constant_environment_only\": true,\n";
  }
  report
         << "    \"native_interop\": false,\n"
         << "    \"ray_tracing\": false\n"
         << "  },\n"
         << "  \"catalog\": {\n"
         << "    \"registry_id\": " << kRegistryId << ",\n"
         << "    \"sequence\": "
         << (modern_pbr ? kVariantSpecs.back().sequence : 1U) << ",\n";
  if (modern_pbr) {
    report << "    \"baseline_sequence\": 1,\n"
           << "    \"live_replacement_count\": 6,\n"
           << "    \"referenced_texture_count\": 4,\n"
           << "    \"referenced_sampler_count\": 1,\n"
           << "    \"unreferenced_assets_not_uploaded\": true,\n";
  }
  report
         << "    \"transactional_replay_after_restart\": true\n"
         << "  },\n"
         ;
  if (modern_pbr) {
    report << "  \"texture_allocations\": {\n"
           << "    \"version\": " << result.texture_allocations.version
           << ",\n"
           << "    \"live_source_textures\": "
           << result.texture_allocations.live_source_textures << ",\n"
           << "    \"sampled_rgba_allocations\": "
           << result.texture_allocations.sampled_rgba_allocations << ",\n"
           << "    \"roughness_r8_allocations\": "
           << result.texture_allocations.roughness_r8_allocations << ",\n"
           << "    \"metallic_r8_allocations\": "
           << result.texture_allocations.metallic_r8_allocations << ",\n"
           << "    \"normal_rg8_allocations\": "
           << result.texture_allocations.normal_rg8_allocations << ",\n"
           << "    \"unused_packed_rgba_allocations\": 0,\n"
           << "    \"exact_usage\": "
           << (result.texture_allocations.exact_usage ? "true" : "false")
           << "\n"
           << "  },\n"
           << "  \"texture_upload_rollback\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_texture_upload_rollback.v1\",\n"
           << "    \"derived_allocation\": \"normal_RG8_UNORM\",\n"
           << "    \"injected_post_create_stage_count\": "
           << result.texture_upload_rollback.size() << ",\n"
           << "    \"stages\": [\n";
    const auto write_rollback_audit =
        [&](const char *name,
            const OgreNextN1TextureAllocationAudit &audit, bool last) {
          report << "          \"" << name << "\": {"
                 << "\"creates\": " << audit.native_allocation_creates
                 << ", \"destroys\": " << audit.native_allocation_destroys
                 << ", \"live\": " << audit.live_native_allocations
                 << ", \"retired_name_lookups\": "
                 << audit.retired_name_lookups
                 << ", \"retired_name_rejections\": "
                 << audit.retired_name_rejections
                 << ", \"exact_usage\": "
                 << (audit.exact_usage ? "true" : "false") << "}"
                 << (last ? "\n" : ",\n");
        };
    for (std::size_t index = 0U;
         index < result.texture_upload_rollback.size(); ++index) {
      const SmokeResult::TextureUploadRollbackStageEvidence &stage =
          result.texture_upload_rollback[index];
      report << "      {\n"
             << "        \"name\": \"" << stage.name << "\",\n"
             << "        \"audits\": {\n";
      write_rollback_audit("after_failure", stage.after_failure, false);
      write_rollback_audit("after_retry", stage.after_retry, false);
      write_rollback_audit("after_replacement", stage.after_replacement,
                           false);
      write_rollback_audit("after_shutdown", stage.after_shutdown, true);
      report << "        }\n"
             << "      }"
             << (index + 1U == result.texture_upload_rollback.size()
                     ? "\n"
                     : ",\n");
    }
    report << "    ],\n"
           << "    \"clean_retry_replacement_shutdown\": true\n"
           << "  },\n"
           << "  \"texture_retirement\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_texture_retirement.v1\",\n"
           << "    \"derived_allocation\": \"normal_RG8_UNORM\",\n"
           << "    \"isolated_from_visual_variants\": true,\n"
           << "    \"native_image_rg8_staging\": {\"version\": "
           << result.retirement.expanded_normal_upload.version
           << ", \"verified_uploads\": "
           << result.retirement.expanded_normal_upload.verified_uploads
           << ", \"verified_mip_levels\": "
           << result.retirement.expanded_normal_upload.verified_mip_levels
           << ", \"verified_rows\": "
           << result.retirement.expanded_normal_upload.verified_rows
           << ", \"verified_texels\": "
           << result.retirement.expanded_normal_upload.verified_texels
           << ", \"verified_rg_bytes\": "
           << result.retirement.expanded_normal_upload.verified_rg_bytes
           << ", \"verified_padded_source_rows\": "
           << result.retirement.expanded_normal_upload
                  .verified_padded_source_rows
           << ", \"exact_source_rg_to_native_image\": "
           << (result.retirement.expanded_normal_upload
                       .exact_source_rg_to_native_image
                   ? "true"
                   : "false")
           << "},\n"
           << "    \"transitions\": [\n"
           << "      {\"revision\": 1, \"width\": 2, \"height\": 2, \"mip_levels\": 1},\n"
           << "      {\"revision\": 2, \"width\": 4, \"height\": 2, \"mip_levels\": 2, \"padded_rows\": true},\n"
           << "      {\"revision\": 3, \"width\": 2, \"height\": 2, \"mip_levels\": 1}\n"
           << "    ],\n"
           << "    \"exact_extent_and_mip_transitions\": "
           << (result.retirement.exact_extent_and_mip_transitions ? "true"
                                                                  : "false")
           << ",\n"
           << "    \"renders_through_transitions_and_restart\": "
           << (result.retirement.renders_through_transitions_and_restart
                   ? "true"
                   : "false")
           << ",\n"
           << "    \"find_texture_no_throw_rejected_old_names\": "
           << (result.retirement.old_names_rejected ? "true" : "false")
           << ",\n"
           << "    \"audits\": {\n";
    const auto write_retirement_audit =
        [&](const char *name,
            const OgreNextN1TextureAllocationAudit &audit, bool last) {
          report << "      \"" << name << "\": {"
                 << "\"creates\": " << audit.native_allocation_creates
                 << ", \"destroys\": " << audit.native_allocation_destroys
                 << ", \"live\": " << audit.live_native_allocations
                 << ", \"retired_name_lookups\": "
                 << audit.retired_name_lookups
                 << ", \"retired_name_rejections\": "
                 << audit.retired_name_rejections << "}"
                 << (last ? "\n" : ",\n");
        };
    write_retirement_audit("initial", result.retirement.initial, false);
    write_retirement_audit("expanded", result.retirement.expanded, false);
    write_retirement_audit("restored", result.retirement.restored, false);
    write_retirement_audit("first_shutdown",
                           result.retirement.first_shutdown, false);
    write_retirement_audit("restarted", result.retirement.restarted, false);
    write_retirement_audit("final_shutdown",
                           result.retirement.final_shutdown, true);
    report << "    }\n"
           << "  },\n"
           << "  \"texture_isolation\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_texture_isolation.v1\",\n"
           << "    \"evidence_file\": \""
           << std::filesystem::u8path(evidence_path)
                  .filename()
                  .generic_u8string()
           << "\",\n"
           << "    \"width\": " << kWidth << ",\n"
           << "    \"height\": " << kHeight << ",\n"
           << "    \"geometry_identical\": true,\n"
           << "    \"material_factors_constants_identical\": true,\n"
           << "    \"camera_identical\": true,\n"
           << "    \"lights_identical\": true,\n"
           << "    \"ui_included\": false,\n"
           << "    \"variants\": [\n";
    std::size_t offset = 0U;
    for (std::size_t index = 0U; index < result.variants.size(); ++index) {
      const VariantEvidence &variant = result.variants[index];
      report << "      {\n"
             << "        \"name\": \"" << variant.name << "\",\n"
             << "        \"changed_input\": \"" << variant.changed_input
             << "\",\n"
             << "        \"asset_sequence\": " << variant.asset_sequence
             << ",\n"
             << "        \"hdr\": {\"offset\": " << offset
             << ", \"bytes\": " << variant.hdr.attachment_bytes.size()
             << ", \"exact_fnv1a64\": \""
             << HexHash(variant.hdr.attachment_fnv1a64)
             << "\", \"changed_pixels_from_baseline\": "
             << variant.hdr_changed_pixels << "},\n";
      offset += variant.hdr.attachment_bytes.size();
      report << "        \"sdr\": {\"offset\": " << offset
             << ", \"bytes\": " << variant.sdr.attachment_bytes.size()
             << ", \"exact_fnv1a64\": \""
             << HexHash(variant.sdr.attachment_fnv1a64)
             << "\", \"changed_pixels_from_baseline\": "
             << variant.sdr_changed_pixels << "}\n"
             << "      }";
      offset += variant.sdr.attachment_bytes.size();
      report << (index + 1U == result.variants.size() ? "\n" : ",\n");
    }
    report << "    ],\n"
           << "    \"evidence_bytes\": " << offset << "\n"
           << "  },\n"
           << "  \"tangent_handedness\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_tangent_handedness.v1\",\n"
           << "    \"evidence_file\": \""
           << std::filesystem::u8path(evidence_path)
                  .filename()
                  .generic_u8string()
           << "\",\n"
           << "    \"evidence_offset\": " << offset << ",\n";
    const std::size_t handedness_evidence_bytes =
        result.tangent_handedness.positive_hdr.attachment_bytes.size() +
        result.tangent_handedness.positive_sdr.attachment_bytes.size() +
        result.tangent_handedness.negative_hdr.attachment_bytes.size() +
        result.tangent_handedness.negative_sdr.attachment_bytes.size();
    report << "    \"evidence_bytes\": " << handedness_evidence_bytes
           << ",\n"
           << "    \"authored_tangent_format\": \"FLOAT4\",\n"
           << "    \"positive_tangent_w\": 1,\n"
           << "    \"negative_tangent_w\": -1,\n"
           << "    \"position_normal_tangent_xyz_uv0_identical\": "
           << (result.tangent_handedness.only_tangent_w_changed ? "true"
                                                                 : "false")
           << ",\n"
           << "    \"material_camera_lights_identical\": true,\n"
           << "    \"ui_included\": false,\n"
           << "    \"positive\": {\n"
           << "      \"hdr\": {\"offset\": " << offset
           << ", \"bytes\": "
           << result.tangent_handedness.positive_hdr.attachment_bytes.size()
           << ", \"exact_fnv1a64\": \""
           << HexHash(result.tangent_handedness.positive_hdr.attachment_fnv1a64)
           << "\"},\n";
    offset += result.tangent_handedness.positive_hdr.attachment_bytes.size();
    report << "      \"sdr\": {\"offset\": " << offset
           << ", \"bytes\": "
           << result.tangent_handedness.positive_sdr.attachment_bytes.size()
           << ", \"exact_fnv1a64\": \""
           << HexHash(result.tangent_handedness.positive_sdr.attachment_fnv1a64)
           << "\"}\n"
           << "    },\n";
    offset += result.tangent_handedness.positive_sdr.attachment_bytes.size();
    report << "    \"negative\": {\n"
           << "      \"hdr\": {\"offset\": " << offset
           << ", \"bytes\": "
           << result.tangent_handedness.negative_hdr.attachment_bytes.size()
           << ", \"exact_fnv1a64\": \""
           << HexHash(result.tangent_handedness.negative_hdr.attachment_fnv1a64)
           << "\"},\n";
    offset += result.tangent_handedness.negative_hdr.attachment_bytes.size();
    report << "      \"sdr\": {\"offset\": " << offset
           << ", \"bytes\": "
           << result.tangent_handedness.negative_sdr.attachment_bytes.size()
           << ", \"exact_fnv1a64\": \""
           << HexHash(result.tangent_handedness.negative_sdr.attachment_fnv1a64)
           << "\"}\n"
           << "    },\n";
    offset += result.tangent_handedness.negative_sdr.attachment_bytes.size();
    report << "    \"hdr_changed_pixels\": "
           << result.tangent_handedness.hdr_changed_pixels << ",\n"
           << "    \"sdr_changed_pixels\": "
           << result.tangent_handedness.sdr_changed_pixels << "\n"
           << "  },\n";
  }
  report
         << "  \"hdr\": {\n"
         << "    \"format\": \"RGBA16_FLOAT\",\n"
         << "    \"width\": " << kWidth << ",\n"
         << "    \"height\": " << kHeight << ",\n"
         << "    \"distinct_rgb8_values\": " << hdr.distinct_rgb << ",\n"
         << "    \"non_background_pixels\": "
         << hdr.non_background_pixels << ",\n"
         << "    \"minimum_luminance\": " << std::setprecision(9)
         << hdr.minimum_luminance << ",\n"
         << "    \"maximum_luminance\": " << hdr.maximum_luminance << ",\n"
         << "    \"exact_attachment_fnv1a64\": \""
         << HexHash(hdr.attachment_fnv1a64) << "\",\n"
         << "    \"rgb8_fnv1a64\": \"" << HexHash(hdr.fnv1a64)
         << "\"\n"
         << "  },\n"
         << "  \"sdr\": {\n"
         << "    \"format\": \"RGBA8_SRGB\",\n"
         << "    \"width\": " << kWidth << ",\n"
         << "    \"height\": " << kHeight << ",\n"
         << "    \"distinct_rgb8_values\": " << sdr.distinct_rgb << ",\n"
         << "    \"non_background_pixels\": "
         << sdr.non_background_pixels << ",\n"
         << "    \"minimum_luminance\": " << sdr.minimum_luminance << ",\n"
         << "    \"maximum_luminance\": " << sdr.maximum_luminance << ",\n"
         << "    \"exact_attachment_fnv1a64\": \""
         << HexHash(sdr.attachment_fnv1a64) << "\",\n"
         << "    \"rgb8_fnv1a64\": \"" << HexHash(sdr.fnv1a64)
         << "\"\n"
         << "  },\n"
         << "  \"lifecycle\": {\n"
         << "    \"unsupported_depth_failed_before_submission\": true,\n"
         << "    \"double_sided_pbs_readback\": true,\n"
         << "    \"lifetime_snapshot_identity_replay\": true,\n"
         << "    \"lifetime_completed_frame_queries\": true,\n"
         << "    \"process_global_root_exclusion\": true,\n"
         ;
  if (modern_pbr) {
    report << "    \"non_uniform_scale_rejected_before_submission\": "
           << (result.non_uniform_scale_rejected_before_submission ? "true"
                                                                    : "false")
           << ",\n"
           << "    \"live_texture_replacement_retirement\": "
           << (result.live_replacement_retirement ? "true" : "false")
           << ",\n"
           << "    \"replacement_audit\": {\"creates\": "
           << result.replacement_final_audit.native_allocation_creates
           << ", \"destroys\": "
           << result.replacement_final_audit.native_allocation_destroys
           << ", \"live\": "
           << result.replacement_final_audit.live_native_allocations
           << ", \"retired_name_lookups\": "
           << result.replacement_final_audit.retired_name_lookups
           << ", \"retired_name_rejections\": "
           << result.replacement_final_audit.retired_name_rejections
           << ", \"exact_usage\": "
           << (result.replacement_final_audit.exact_usage ? "true" : "false")
           << "},\n";
  }
  report
         << "    \"shutdown_reinitialize_render_shutdown\": true\n"
         << "  }\n"
         << "}\n";
  return report.str();
}

FrontendInitializationRequest Initialization() {
  FrontendInitializationRequest initialization;
  initialization.initial_width = kWidth;
  initialization.initial_height = kHeight;
  initialization.maximum_frames_in_flight = 1U;
  initialization.headless = true;
  initialization.vertical_sync = false;
  return initialization;
}

void InitializeAndSync(OgreNextN1Frontend &frontend,
                       const RenderAssetDelta &catalog) {
  const FrontendInitializationRequest initialization = Initialization();
  RequireSuccess(frontend.Initialize(initialization), "Initialize");
  RequireSuccess(frontend.SynchronizeAssets(catalog), "SynchronizeAssets");
}

SmokeResult::TangentHandednessEvidence
RunTangentHandednessProof(const std::string &media_root) {
  const RenderAssetDelta positive_catalog =
      MakeTangentHandednessCatalog(false);
  const RenderAssetDelta negative_catalog =
      MakeTangentHandednessCatalog(true);
  RequireControlledTangentHandednessCatalogs(positive_catalog,
                                             negative_catalog);
  constexpr float kSqrtHalf = 0.707106769F;
  const Float3 angled_light{0.0F, -kSqrtHalf, -kSqrtHalf};
  const auto positive_scene = MakeScene(800U, false, true, 1U, 1U,
                                        Matrix4x4{}, 1U, angled_light);
  const auto negative_scene = MakeScene(801U, false, true, 2U, 1U,
                                        Matrix4x4{}, 2U, angled_light);

  OgreNextN1Frontend frontend(OgreNextN1Configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1});
  InitializeAndSync(frontend, positive_catalog);
  RenderFrameRequest positive_hdr_frame =
      MakeFrame(1U, positive_scene, PixelFormat::RGBA16_FLOAT);
  RenderFrameRequest positive_sdr_frame =
      MakeFrame(2U, positive_scene, PixelFormat::RGBA8_SRGB);
  RenderFrameOutput positive_hdr_output;
  RenderFrameOutput positive_sdr_output;
  RequireSuccess(frontend.Render(positive_hdr_frame, positive_hdr_output),
                 "RT4 positive tangent-w HDR Render");
  RequireSuccess(frontend.Render(positive_sdr_frame, positive_sdr_output),
                 "RT4 positive tangent-w SDR Render");

  RequireSuccess(frontend.SynchronizeAssets(negative_catalog),
                 "RT4 negative tangent-w SynchronizeAssets");
  RenderFrameRequest negative_hdr_frame =
      MakeFrame(3U, negative_scene, PixelFormat::RGBA16_FLOAT);
  RenderFrameRequest negative_sdr_frame =
      MakeFrame(4U, negative_scene, PixelFormat::RGBA8_SRGB);
  RequireControlledSceneAndView(*positive_scene, *negative_scene,
                                positive_hdr_frame, negative_hdr_frame, true);
  RequireControlledSceneAndView(*positive_scene, *negative_scene,
                                positive_sdr_frame, negative_sdr_frame, true);
  RenderFrameOutput negative_hdr_output;
  RenderFrameOutput negative_sdr_output;
  RequireSuccess(frontend.Render(negative_hdr_frame, negative_hdr_output),
                 "RT4 negative tangent-w HDR Render");
  RequireSuccess(frontend.Render(negative_sdr_frame, negative_sdr_output),
                 "RT4 negative tangent-w SDR Render");

  SmokeResult::TangentHandednessEvidence evidence;
  evidence.positive_hdr = InspectHdr(positive_hdr_output);
  evidence.positive_sdr = InspectSdr(positive_sdr_output);
  evidence.negative_hdr = InspectHdr(negative_hdr_output);
  evidence.negative_sdr = InspectSdr(negative_sdr_output);
  evidence.hdr_changed_pixels = CountChangedPixels(
      evidence.positive_hdr.attachment_bytes,
      evidence.negative_hdr.attachment_bytes, 8U);
  evidence.sdr_changed_pixels = CountChangedPixels(
      evidence.positive_sdr.attachment_bytes,
      evidence.negative_sdr.attachment_bytes, 4U);
  evidence.only_tangent_w_changed = true;
  Require(evidence.hdr_changed_pixels >= 64U &&
              evidence.sdr_changed_pixels >= 64U &&
              evidence.positive_hdr.attachment_fnv1a64 !=
                  evidence.negative_hdr.attachment_fnv1a64 &&
              evidence.positive_sdr.attachment_fnv1a64 !=
                  evidence.negative_sdr.attachment_fnv1a64,
          "RT4 authored tangent-w sign produced no exact native HDR/SDR effect");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "RT4 tangent-handedness Shutdown");
  return evidence;
}

void RequireRetirementAudit(const OgreNextN1TextureAllocationAudit &audit,
                            std::uint64_t creates,
                            std::uint64_t destroys,
                            std::uint64_t live,
                            const std::string &label,
                            bool require_exact_usage = true) {
  Require(audit.version == 1U && audit.live_source_textures == (live > 0U ? 1U : 0U) &&
              audit.sampled_rgba_allocations == 0U &&
              audit.roughness_r8_allocations == 0U &&
              audit.metallic_r8_allocations == 0U &&
              audit.normal_rg8_allocations == live &&
              audit.native_allocation_creates == creates &&
              audit.native_allocation_destroys == destroys &&
              audit.live_native_allocations == live &&
              audit.retired_name_lookups == destroys &&
              audit.retired_name_rejections == destroys &&
              (!require_exact_usage || audit.exact_usage),
          "RT4 normal retirement allocation/name audit drifted at " + label);
}

SmokeResult::TextureRetirementEvidence
RunTextureRetirementProof(const std::string &media_root) {
  SmokeResult::TextureRetirementEvidence evidence;
  OgreNextN1Frontend frontend(OgreNextN1Configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1});
  const RenderAssetDelta initial_catalog = MakeRetirementCatalog(1U);
  InitializeAndSync(frontend, initial_catalog);
  evidence.initial = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.initial, 1U, 0U, 1U, "initial 2x2/one-mip");
  RenderFrameOutput initial_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, MakeRetirementScene(1U),
                               PixelFormat::RGBA8_SRGB),
                     initial_output),
                 "RT4 retirement initial Render");
  static_cast<void>(InspectSdr(initial_output));

  const RenderAssetDelta expanded_catalog = MakeRetirementCatalog(2U);
  RequireSuccess(frontend.SynchronizeAssets(expanded_catalog),
                 "RT4 retirement expand SynchronizeAssets");
  evidence.expanded = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.expanded, 2U, 1U, 1U,
                         "expanded 4x2/two-mip");
  evidence.expanded_normal_upload = frontend.QueryNormalUploadAudit();
  Require(evidence.expanded_normal_upload.version == 1U &&
              evidence.expanded_normal_upload.verified_uploads == 2U &&
              evidence.expanded_normal_upload.verified_mip_levels == 3U &&
              evidence.expanded_normal_upload.verified_rows == 5U &&
              evidence.expanded_normal_upload.verified_texels == 14U &&
              evidence.expanded_normal_upload.verified_rg_bytes == 28U &&
              evidence.expanded_normal_upload
                      .verified_padded_source_rows == 5U &&
              evidence.expanded_normal_upload
                  .exact_source_rg_to_native_image,
          "RT4 padded multi-mip source RG bytes did not survive exactly in Ogre Image2");
  RenderFrameOutput expanded_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(2U, MakeRetirementScene(2U),
                               PixelFormat::RGBA8_SRGB),
                     expanded_output),
                 "RT4 retirement expanded Render");
  static_cast<void>(InspectSdr(expanded_output));

  const RenderAssetDelta restored_catalog = MakeRetirementCatalog(3U);
  RequireSuccess(frontend.SynchronizeAssets(restored_catalog),
                 "RT4 retirement restore SynchronizeAssets");
  evidence.restored = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.restored, 3U, 2U, 1U,
                         "restored 2x2/one-mip");
  RenderFrameOutput restored_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(3U, MakeRetirementScene(3U),
                               PixelFormat::RGBA8_SRGB),
                     restored_output),
                 "RT4 retirement restored Render");
  static_cast<void>(InspectSdr(restored_output));

  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "RT4 retirement first Shutdown");
  evidence.first_shutdown = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.first_shutdown, 3U, 3U, 0U,
                         "first shutdown", false);

  InitializeAndSync(frontend, restored_catalog);
  evidence.restarted = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.restarted, 4U, 3U, 1U, "restart");
  RenderFrameOutput restarted_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, MakeRetirementScene(3U),
                               PixelFormat::RGBA8_SRGB),
                     restarted_output),
                 "RT4 retirement restart Render");
  static_cast<void>(InspectSdr(restarted_output));
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "RT4 retirement final Shutdown");
  evidence.final_shutdown = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.final_shutdown, 4U, 4U, 0U,
                         "final shutdown", false);

  evidence.exact_extent_and_mip_transitions = true;
  evidence.renders_through_transitions_and_restart = true;
  evidence.old_names_rejected =
      evidence.final_shutdown.retired_name_lookups == 4U &&
      evidence.final_shutdown.retired_name_rejections == 4U;
  Require(evidence.old_names_rejected,
          "RT4 retirement old names remained discoverable");
  return evidence;
}

std::vector<SmokeResult::TextureUploadRollbackStageEvidence>
RunTextureUploadRollbackProof(const std::string &media_root) {
  using FailureStage = OgreNextN1TextureUploadFailureStage;
  const std::array<std::pair<FailureStage, const char *>, 5U> stages{{
      {FailureStage::AFTER_CREATE, "after_create"},
      {FailureStage::AFTER_SET_RESOLUTION, "after_set_resolution"},
      {FailureStage::AFTER_SET_MIPMAPS, "after_set_mipmaps"},
      {FailureStage::AFTER_SET_PIXEL_FORMAT, "after_set_pixel_format"},
      {FailureStage::AFTER_SCHEDULE_TRANSITION,
       "after_schedule_transition"},
  }};
  std::vector<SmokeResult::TextureUploadRollbackStageEvidence> evidence;
  evidence.reserve(stages.size());
  for (const auto &stage : stages) {
    OgreNextN1Frontend frontend(OgreNextN1Configuration{
        media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
        stage.first});
    RequireSuccess(frontend.Initialize(Initialization()),
                   std::string("RT4 rollback Initialize(") + stage.second + ')');
    const RenderAssetDelta initial_catalog = MakeRetirementCatalog(1U);
    const RenderOperationResult injected =
        frontend.SynchronizeAssets(initial_catalog);
    Require(injected.code == RenderOperationCode::BACKEND_FAILURE,
            std::string("RT4 rollback injection did not fail at ") +
                stage.second);

    SmokeResult::TextureUploadRollbackStageEvidence record;
    record.name = stage.second;
    record.after_failure = frontend.QueryTextureAllocationAudit();
    RequireRetirementAudit(record.after_failure, 1U, 1U, 0U,
                           record.name + " rollback");

    RequireSuccess(frontend.SynchronizeAssets(initial_catalog),
                   std::string("RT4 rollback retry(") + stage.second + ')');
    record.after_retry = frontend.QueryTextureAllocationAudit();
    RequireRetirementAudit(record.after_retry, 2U, 1U, 1U,
                           record.name + " retry");
    RenderFrameOutput retry_output;
    RequireSuccess(frontend.Render(
                       MakeFrame(1U, MakeRetirementScene(1U),
                                 PixelFormat::RGBA8_SRGB),
                       retry_output),
                   std::string("RT4 rollback retry Render(") + stage.second +
                       ')');
    static_cast<void>(InspectSdr(retry_output));

    const RenderAssetDelta replacement_catalog = MakeRetirementCatalog(2U);
    RequireSuccess(frontend.SynchronizeAssets(replacement_catalog),
                   std::string("RT4 rollback replacement(") + stage.second +
                       ')');
    record.after_replacement = frontend.QueryTextureAllocationAudit();
    RequireRetirementAudit(record.after_replacement, 3U, 2U, 1U,
                           record.name + " replacement");
    RenderFrameOutput replacement_output;
    RequireSuccess(frontend.Render(
                       MakeFrame(2U, MakeRetirementScene(2U),
                                 PixelFormat::RGBA8_SRGB),
                       replacement_output),
                   std::string("RT4 rollback replacement Render(") +
                       stage.second + ')');
    static_cast<void>(InspectSdr(replacement_output));

    RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                   std::string("RT4 rollback Shutdown(") + stage.second + ')');
    record.after_shutdown = frontend.QueryTextureAllocationAudit();
    RequireRetirementAudit(record.after_shutdown, 3U, 3U, 0U,
                           record.name + " shutdown", false);
    evidence.push_back(std::move(record));
  }
  return evidence;
}

SmokeResult RunSmoke(const std::string &media_root, bool modern_pbr) {
  const VariantSpec *baseline_spec = modern_pbr ? &kVariantSpecs.front()
                                                 : nullptr;
  const RenderAssetDelta catalog = MakeCatalog(modern_pbr, baseline_spec);
  const auto scene_one = MakeScene(1U, false, modern_pbr);
  const auto scene_two = MakeScene(2U, true, modern_pbr);
  const OgreNextRasterFeatureTier raster_feature_tier =
      modern_pbr ? OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1
                 : OgreNextRasterFeatureTier::STATIC_PBR_N1;
  OgreNextN1Frontend relative_media(
      OgreNextN1Configuration{"relative/shader/media", raster_feature_tier});
  Require(relative_media.Initialize(Initialization()).code ==
              RenderOperationCode::INVALID_ARGUMENT,
          "relative shader media root did not fail closed");
  OgreNextN1Frontend missing_media(
      OgreNextN1Configuration{media_root + "/missing", raster_feature_tier});
  Require(missing_media.Initialize(Initialization()).code ==
              RenderOperationCode::INVALID_ARGUMENT,
          "missing shader media root did not fail closed");

  if (modern_pbr) {
    OgreNextN1Frontend legacy_rejection(OgreNextN1Configuration{media_root});
    RequireSuccess(legacy_rejection.Initialize(Initialization()),
                   "legacy rejection Initialize");
    Require(legacy_rejection.SynchronizeAssets(catalog).code ==
                RenderOperationCode::UNSUPPORTED,
            "default N1 runtime silently enabled the opt-in RT4/V1 catalog");
    RequireSuccess(
        legacy_rejection.Shutdown(kInfiniteRenderTimeoutNanoseconds),
        "legacy rejection Shutdown");
  }

  OgreNextN1Frontend frontend(
      OgreNextN1Configuration{media_root, raster_feature_tier});

  const FrontendCapabilityReport capabilities = frontend.QueryCapabilities();
  Require(capabilities.frontend_kind == RendererFrontendKind::OGRE_NEXT &&
              capabilities.supported_outputs == FrameOutputMask::COLOR &&
              capabilities.supports_hdr_output &&
              capabilities.maximum_views == 1U &&
              capabilities.maximum_frames_in_flight == 1U &&
              capabilities.maximum_texture_dimension_2d ==
                  kOgreNextN1ConservativeMaximumTextureDimension &&
              capabilities.native_api == NativeGraphicsApi::NONE &&
              !capabilities.supports_compute &&
              !capabilities.supports_dynamic_mesh_updates &&
              !capabilities.supports_particle_events &&
              !capabilities.supports_native_interop &&
              !capabilities.supports_native_ray_tracing_api,
          "N1 capability report enabled an unproved feature");
  Require(frontend.GetNativeInterop() == nullptr,
          "N1 unexpectedly exported native interop");

  SmokeResult result;
  if (modern_pbr) {
    result.tangent_handedness = RunTangentHandednessProof(media_root);
    result.texture_upload_rollback =
        RunTextureUploadRollbackProof(media_root);
    result.retirement = RunTextureRetirementProof(media_root);
  }
  InitializeAndSync(frontend, catalog);
  if (modern_pbr) {
    result.texture_allocations = frontend.QueryTextureAllocationAudit();
    Require(result.texture_allocations.version == 1U &&
                result.texture_allocations.live_source_textures == 4U &&
                result.texture_allocations.sampled_rgba_allocations == 2U &&
                result.texture_allocations.roughness_r8_allocations == 1U &&
                result.texture_allocations.metallic_r8_allocations == 1U &&
                result.texture_allocations.normal_rg8_allocations == 1U &&
                result.texture_allocations.native_allocation_creates == 5U &&
                result.texture_allocations.native_allocation_destroys == 0U &&
                result.texture_allocations.exact_usage,
            "RT4/V1 allocated an unused source variant or lost a required derivative");
  }
  OgreNextN1Frontend concurrent(
      OgreNextN1Configuration{media_root, raster_feature_tier});
  const RenderOperationResult concurrent_result =
      concurrent.Initialize(Initialization());
  Require(concurrent_result.code == RenderOperationCode::BACKEND_FAILURE &&
              concurrent_result.detail.find("process-global Root") !=
                  std::string::npos,
          "a second simultaneous frontend escaped Ogre Root ownership");
  const FrontendCapabilityReport initialized_capabilities =
      frontend.QueryCapabilities();
  Require(initialized_capabilities.maximum_texture_dimension_2d >= kWidth &&
              initialized_capabilities.maximum_texture_dimension_2d >=
                  kHeight,
          "initialized N1 frontend did not publish its real device extent");
  RenderFrameRequest invalid =
      MakeFrame(1U, scene_one, PixelFormat::RGBA16_FLOAT);
  invalid.requested_outputs =
      FrameOutputMask::COLOR | FrameOutputMask::DEPTH;
  RenderFrameOutput untouched;
  untouched.frame_id = 777U;
  const RenderOperationResult invalid_result = frontend.Render(invalid, untouched);
  Require(invalid_result.code == RenderOperationCode::UNSUPPORTED &&
              untouched.frame_id == 777U && !frontend.IsFrameComplete(1U),
          "unsupported depth request mutated output or consumed frame identity");
  if (modern_pbr) {
    Matrix4x4 non_uniform_scale;
    non_uniform_scale.elements[0U] = 2.0F;
    const auto non_uniform_scene =
        MakeScene(700U, false, true, 1U, 1U, non_uniform_scale);
    RenderFrameOutput non_uniform_output;
    non_uniform_output.frame_id = 778U;
    const RenderOperationResult non_uniform_result = frontend.Render(
        MakeFrame(1U, non_uniform_scene, PixelFormat::RGBA16_FLOAT),
        non_uniform_output);
    result.non_uniform_scale_rejected_before_submission =
        non_uniform_result.code == RenderOperationCode::UNSUPPORTED &&
        non_uniform_output.frame_id == 778U &&
        !frontend.IsFrameComplete(1U);
    Require(result.non_uniform_scale_rejected_before_submission,
            "non-uniform RT4/V1 scale mutated output or reached native submission");
  }

  RenderFrameOutput hdr_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, scene_one, PixelFormat::RGBA16_FLOAT),
                     hdr_output),
                 "HDR Render");
  Require(frontend.IsFrameComplete(1U),
          "synchronous HDR frame was not complete on return");
  RequireSuccess(frontend.WaitForFrame(1U, 0U), "WaitForFrame(HDR)");
  result.hdr = InspectHdr(hdr_output);

  RenderFrameOutput sdr_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(2U, scene_one, PixelFormat::RGBA8_SRGB),
                     sdr_output),
                 "SDR Render");
  result.sdr = InspectSdr(sdr_output);

  if (modern_pbr) {
    VariantEvidence baseline;
    baseline.name = kVariantSpecs.front().name;
    baseline.changed_input = kVariantSpecs.front().changed_input;
    baseline.asset_sequence = kVariantSpecs.front().sequence;
    baseline.hdr = result.hdr;
    baseline.sdr = result.sdr;
    result.variants.push_back(std::move(baseline));
  }

  RenderFrameOutput newer_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(3U, scene_two, PixelFormat::RGBA8_SRGB),
                     newer_output),
                 "new snapshot Render");
  static_cast<void>(InspectSdr(newer_output));
  RenderFrameOutput old_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(4U, scene_one, PixelFormat::RGBA8_SRGB),
                     old_output),
                 "older snapshot replay Render");
  static_cast<void>(InspectSdr(old_output));
  Require(frontend.IsFrameComplete(1U) && frontend.IsFrameComplete(4U),
          "successful N1 frame fell out of lifetime completion history");

  RenderAssetDelta final_catalog = catalog;
  std::shared_ptr<const SceneSnapshot> final_scene = scene_one;
  std::uint64_t next_frame_id = 5U;
  if (modern_pbr) {
    const RenderFrameRequest baseline_hdr_frame =
        MakeFrame(1U, scene_one, PixelFormat::RGBA16_FLOAT);
    const RenderFrameRequest baseline_sdr_frame =
        MakeFrame(2U, scene_one, PixelFormat::RGBA8_SRGB);
    for (std::size_t variant_index = 1U;
         variant_index < kVariantSpecs.size(); ++variant_index) {
      const VariantSpec &spec = kVariantSpecs[variant_index];
      RenderAssetDelta variant_catalog = MakeCatalog(true, &spec);
      RequireControlledCatalog(catalog, variant_catalog, spec);
      RequireSuccess(frontend.SynchronizeAssets(variant_catalog),
                     std::string("SynchronizeAssets(") + spec.name + ')');
      const OgreNextN1TextureAllocationAudit audit =
          frontend.QueryTextureAllocationAudit();
      Require(audit.version == 1U && audit.live_source_textures == 4U &&
                  audit.sampled_rgba_allocations == 2U &&
                  audit.roughness_r8_allocations == 1U &&
                  audit.metallic_r8_allocations == 1U &&
                  audit.normal_rg8_allocations == 1U &&
                  audit.native_allocation_creates ==
                      spec.expected_native_creates &&
                  audit.native_allocation_destroys ==
                      spec.expected_native_destroys &&
                  audit.retired_name_lookups ==
                      spec.expected_native_destroys &&
                  audit.retired_name_rejections ==
                      spec.expected_native_destroys &&
                  audit.exact_usage,
              std::string("RT4/V1 replacement allocation drifted for ") +
                  spec.name);

      const auto variant_scene = MakeScene(
          100U + variant_index, false, true, spec.sequence,
          spec.material_revision);
      RenderFrameRequest variant_hdr_frame =
          MakeFrame(next_frame_id++, variant_scene,
                    PixelFormat::RGBA16_FLOAT);
      RenderFrameRequest variant_sdr_frame =
          MakeFrame(next_frame_id++, variant_scene,
                    PixelFormat::RGBA8_SRGB);
      RequireControlledSceneAndView(*scene_one, *variant_scene,
                                    baseline_hdr_frame, variant_hdr_frame);
      RequireControlledSceneAndView(*scene_one, *variant_scene,
                                    baseline_sdr_frame, variant_sdr_frame);

      RenderFrameOutput variant_hdr_output;
      RequireSuccess(frontend.Render(variant_hdr_frame, variant_hdr_output),
                     std::string("HDR Render(") + spec.name + ')');
      RenderFrameOutput variant_sdr_output;
      RequireSuccess(frontend.Render(variant_sdr_frame, variant_sdr_output),
                     std::string("SDR Render(") + spec.name + ')');
      VariantEvidence evidence;
      evidence.name = spec.name;
      evidence.changed_input = spec.changed_input;
      evidence.asset_sequence = spec.sequence;
      evidence.hdr = InspectHdr(variant_hdr_output);
      evidence.sdr = InspectSdr(variant_sdr_output);
      evidence.hdr_changed_pixels = CountChangedPixels(
          result.hdr.attachment_bytes, evidence.hdr.attachment_bytes, 8U);
      evidence.sdr_changed_pixels = CountChangedPixels(
          result.sdr.attachment_bytes, evidence.sdr.attachment_bytes, 4U);
      Require(evidence.hdr_changed_pixels >= 64U &&
                  evidence.sdr_changed_pixels >= 64U &&
                  evidence.hdr.attachment_fnv1a64 !=
                      result.hdr.attachment_fnv1a64 &&
                  evidence.sdr.attachment_fnv1a64 !=
                      result.sdr.attachment_fnv1a64,
              std::string("RT4/V1 isolated texture input produced no exact HDR/SDR effect: ") +
                  spec.name);
      result.variants.push_back(std::move(evidence));
      final_catalog = std::move(variant_catalog);
      final_scene = variant_scene;
    }
    result.replacement_final_audit =
        frontend.QueryTextureAllocationAudit();
    const OgreNextN1TextureAllocationAudit &final_audit =
        result.replacement_final_audit;
    result.live_replacement_retirement =
        result.variants.size() == kVariantSpecs.size() &&
        final_audit.native_allocation_creates == 17U &&
        final_audit.native_allocation_destroys == 12U &&
        final_audit.live_native_allocations == 5U &&
        final_audit.retired_name_lookups == 12U &&
        final_audit.retired_name_rejections == 12U &&
        final_audit.exact_usage;
  }
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "first Shutdown");

  InitializeAndSync(concurrent, final_catalog);
  RequireSuccess(concurrent.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "post-owner-release concurrent Shutdown");

  InitializeAndSync(frontend, final_catalog);
  RenderFrameOutput recovered_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, final_scene, PixelFormat::RGBA8_SRGB),
                     recovered_output),
                 "post-reinitialize Render");
  static_cast<void>(InspectSdr(recovered_output));
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "recovery Shutdown");
  return result;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    const SmokeResult result =
        RunSmoke(arguments.media_root, arguments.modern_pbr);
    WritePpm(arguments.image_path, result.sdr);
    if (arguments.modern_pbr) {
      WriteIsolationEvidence(arguments.evidence_path, result);
    }
    const std::string report = MakeReport(
        result, arguments.modern_pbr, arguments.evidence_path);
    WriteText(arguments.report_path, report);
    std::cout << report;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next N1 frontend smoke failed: " << error.what() << '\n';
    return 1;
  }
}
