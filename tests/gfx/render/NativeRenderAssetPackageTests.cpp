/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "NativeRenderAssetPackage.h"

#include "MaterialDescriptor.h"
#include "RenderAssetRegistry.h"
#include "RenderResourceDescriptors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifndef ROR_NATIVE_RENDER_ASSET_FIXTURE
#error "ROR_NATIVE_RENDER_ASSET_FIXTURE must name the checked .rornative file"
#endif

namespace {

using namespace RoR::Render;

constexpr char kCheckedPackageSha256[] =
    "5f91c134231d5b86cd0c291d30018aa2f8aa4958c8e9267ec1c9068a0ea9bc05";

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "native render asset package test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::vector<std::uint8_t> ReadFixture() {
  std::ifstream stream(ROR_NATIVE_RENDER_ASSET_FIXTURE,
                       std::ios::binary | std::ios::ate);
  Require(stream.good(), "checked package fixture is unavailable");
  const std::streamoff size = stream.tellg();
  Require(size > 0, "checked package fixture is empty");
  stream.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char *>(bytes.data()), size);
  Require(stream.good(), "checked package fixture could not be read");
  return bytes;
}

std::uint32_t U32(const std::vector<std::uint8_t> &bytes,
                  std::size_t offset) {
  Require(offset + 4U <= bytes.size(), "test read escaped fixture");
  return static_cast<std::uint32_t>(bytes[offset]) |
         static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U |
         static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U |
         static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U;
}

std::uint64_t U64(const std::vector<std::uint8_t> &bytes,
                  std::size_t offset) {
  Require(offset + 8U <= bytes.size(), "test read escaped fixture");
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[offset + index])
             << (index * 8U);
  }
  return value;
}

void PutU32(std::vector<std::uint8_t> &bytes, std::size_t offset,
            std::uint32_t value) {
  Require(offset + 4U <= bytes.size(), "test write escaped fixture");
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] =
        static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
  }
}

void PutU64(std::vector<std::uint8_t> &bytes, std::size_t offset,
            std::uint64_t value) {
  Require(offset + 8U <= bytes.size(), "test write escaped fixture");
  for (std::size_t index = 0U; index < 8U; ++index) {
    bytes[offset + index] =
        static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
  }
}

void AppendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes.push_back(
        static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
  }
}

void AppendFloat(std::vector<std::uint8_t> &bytes, float value) {
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "package test requires IEEE binary32 storage");
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(bytes, bits);
}

struct RecordView {
  std::uint32_t type = 0U;
  std::uint64_t source_id = 0U;
  std::size_t header_offset = 0U;
  std::size_t payload_offset = 0U;
  std::size_t payload_size = 0U;
};

std::vector<RecordView> Records(const std::vector<std::uint8_t> &bytes) {
  Require(bytes.size() >= kNativeRenderAssetPackageHeaderBytes,
          "fixture is too small for records");
  std::vector<RecordView> records;
  std::size_t offset = kNativeRenderAssetPackageHeaderBytes;
  while (offset < bytes.size()) {
    Require(offset + 24U <= bytes.size(), "record header is truncated");
    const std::uint64_t payload_size = U64(bytes, offset + 16U);
    Require(payload_size <= bytes.size() - offset - 24U,
            "record payload is truncated");
    records.push_back(RecordView{
        U32(bytes, offset),
        U64(bytes, offset + 8U),
        offset,
        offset + 24U,
        static_cast<std::size_t>(payload_size),
    });
    offset += 24U + static_cast<std::size_t>(payload_size);
  }
  Require(offset == bytes.size(), "records do not close over package bytes");
  return records;
}

std::string RecordName(const std::vector<std::uint8_t> &bytes,
                       const RecordView &record) {
  Require(record.payload_size >= 8U, "asset record lacks a name");
  const std::uint32_t size = U32(bytes, record.payload_offset + 4U);
  Require(size <= record.payload_size - 8U, "asset name is truncated");
  return std::string(
      reinterpret_cast<const char *>(bytes.data() + record.payload_offset + 8U),
      size);
}

RecordView FindRecord(const std::vector<std::uint8_t> &bytes,
                      std::uint32_t type, const std::string &name = {}) {
  for (const RecordView &record : Records(bytes)) {
    if (record.type == type && (name.empty() || RecordName(bytes, record) == name)) {
      return record;
    }
  }
  Require(false, "requested test record is absent");
  return {};
}

void RefreshBodyDigest(std::vector<std::uint8_t> &bytes) {
  const RenderPayloadDigest digest = ComputeRenderPayloadDigest(
      bytes.data() + kNativeRenderAssetPackageHeaderBytes,
      bytes.size() - kNativeRenderAssetPackageHeaderBytes);
  std::copy(digest.begin(), digest.end(), bytes.begin() + 40U);
}

RenderPayloadDigest DigestFromHex(const char *text) {
  RenderPayloadDigest digest{};
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    std::uint8_t byte = 0U;
    for (std::size_t nibble = 0U; nibble < 2U; ++nibble) {
      const char character = text[index * 2U + nibble];
      const std::uint8_t digit =
          character >= '0' && character <= '9'
              ? static_cast<std::uint8_t>(character - '0')
              : static_cast<std::uint8_t>(character - 'a' + 10);
      byte = static_cast<std::uint8_t>((byte << 4U) | digit);
    }
    digest[index] = byte;
  }
  Require(text[digest.size() * 2U] == '\0', "test SHA-256 literal length changed");
  return digest;
}

RenderPayloadDigest TrustedDigest(const std::vector<std::uint8_t> &bytes) {
  return ComputeRenderPayloadDigest(bytes.data(), bytes.size());
}

std::vector<std::uint8_t> ReplaceManifestPayload(
    const std::vector<std::uint8_t> &bytes, const std::string &from,
    const std::string &to) {
  const std::vector<RecordView> records = Records(bytes);
  Require(!records.empty() && records.front().type == 1U,
          "test package manifest is absent");
  const RecordView &manifest = records.front();
  std::string text(
      reinterpret_cast<const char *>(bytes.data() + manifest.payload_offset),
      manifest.payload_size);
  const std::size_t found = text.find(from);
  Require(found != std::string::npos && text.find(from, found + 1U) == std::string::npos,
          "manifest hostile replacement is absent or ambiguous");
  text.replace(found, from.size(), to);
  std::vector<std::uint8_t> result;
  result.reserve(bytes.size() - from.size() + to.size());
  result.insert(result.end(), bytes.begin(),
                bytes.begin() + static_cast<std::ptrdiff_t>(manifest.payload_offset));
  result.insert(result.end(), text.begin(), text.end());
  result.insert(
      result.end(),
      bytes.begin() + static_cast<std::ptrdiff_t>(manifest.payload_offset +
                                                  manifest.payload_size),
      bytes.end());
  PutU64(result, manifest.header_offset + 16U, text.size());
  PutU64(result, 32U, result.size());
  RefreshBodyDigest(result);
  return result;
}

std::vector<std::uint8_t> MakeSyntheticTransmissionV2Package() {
  std::vector<std::uint8_t> source = ReplaceManifestPayload(
      ReadFixture(), "ror-native-render-package-manifest-v1",
      "ror-native-render-package-manifest-v2");
  Require(U32(source, 8U) == kNativeRenderAssetPackageVersion,
          "v2 regression source is not the checked v1 package");

  std::vector<std::uint8_t> result(
      source.begin(),
      source.begin() +
          static_cast<std::ptrdiff_t>(kNativeRenderAssetPackageHeaderBytes));
  constexpr std::array<std::uint8_t, 8U> kTransmissionMagic{{
      'R', 'O', 'R', 'N', 'A', 'T', '2', 0U,
  }};
  std::copy(kTransmissionMagic.begin(), kTransmissionMagic.end(),
            result.begin());
  PutU32(result, 8U, kNativeRenderAssetPackageTransmissionVersion);

  for (const RecordView &record : Records(source)) {
    std::vector<std::uint8_t> payload(
        source.begin() + static_cast<std::ptrdiff_t>(record.payload_offset),
        source.begin() + static_cast<std::ptrdiff_t>(record.payload_offset +
                                                      record.payload_size));
    if (record.type == 4U) {
      const std::string name = RecordName(source, record);
      const std::uint32_t name_size = U32(payload, 4U);
      const std::size_t state_offset = 8U + name_size;
      const std::size_t transmission_offset = state_offset + 8U + 17U * 4U;
      Require(transmission_offset <= payload.size(),
              "v1 material layout is truncated");
      PutU32(payload, 0U, kMaterialDescriptorTransmissionVersion);

      const bool thin_slab = name == "rorng_a0_reflector_material";
      if (thin_slab) {
        payload[state_offset + 6U] = 0U; // depth_write
        payload[state_offset + 7U] = static_cast<std::uint8_t>(
            MaterialTransmissionMode::THIN_PARALLEL_SLAB);
      }
      std::vector<std::uint8_t> transmission;
      transmission.reserve(6U * sizeof(float));
      AppendFloat(transmission, thin_slab ? 0.75F : 0.0F);
      AppendFloat(transmission, thin_slab ? 0.8F : 1.0F);
      AppendFloat(transmission, thin_slab ? 0.9F : 1.0F);
      AppendFloat(transmission, 1.0F);
      AppendFloat(transmission, thin_slab ? 2.0F : 1.0F);
      AppendFloat(transmission, thin_slab ? 0.1F : 0.0F);
      payload.insert(
          payload.begin() + static_cast<std::ptrdiff_t>(transmission_offset),
          transmission.begin(), transmission.end());
    }

    const std::size_t header_offset = result.size();
    result.insert(
        result.end(),
        source.begin() + static_cast<std::ptrdiff_t>(record.header_offset),
        source.begin() + static_cast<std::ptrdiff_t>(record.payload_offset));
    PutU64(result, header_offset + 16U, payload.size());
    result.insert(result.end(), payload.begin(), payload.end());
  }
  PutU64(result, 32U, result.size());
  RefreshBodyDigest(result);
  return result;
}

void RequireDecodeFailure(const std::vector<std::uint8_t> &bytes,
                          const char *message) {
  const NativeRenderAssetPackageDecodeResult result =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                     TrustedDigest(bytes));
  Require(!result.ok(), message);
  Require(!result.validation.ok(), "failed decode returned successful validation");
  Require(result.package == nullptr, "failed decode published a partial package");
}

void RequirePinnedDecodeFailure(const std::vector<std::uint8_t> &bytes,
                                const char *message) {
  const NativeRenderAssetPackageDecodeResult result =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                     DigestFromHex(kCheckedPackageSha256));
  Require(!result.ok() && result.package == nullptr, message);
}

const GraphicsSceneAssetInput *FindAsset(
    const NativeRenderAssetPackage &package, const std::string &debug_name) {
  for (const GraphicsSceneAssetInput &input : package.assets) {
    if (input.payload == nullptr) {
      continue;
    }
    const bool matches = std::visit(
        [&debug_name](const auto &value) {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, MeshResourceDescriptor> ||
                        std::is_same_v<Value, TextureResourceDescriptor> ||
                        std::is_same_v<Value, MaterialDescriptor> ||
                        std::is_same_v<Value, SamplerResourceDescriptor>) {
            return value.debug_name == debug_name;
          }
          return false;
        },
        *input.payload);
    if (matches) {
      return &input;
    }
  }
  return nullptr;
}

const GraphicsSceneStaticMeshInput *FindInstanceForMesh(
    const NativeRenderAssetPackage &package, const std::string &debug_name) {
  const GraphicsSceneAssetInput *mesh = FindAsset(package, debug_name);
  if (mesh == nullptr) {
    return nullptr;
  }
  for (const GraphicsSceneStaticMeshInput &instance : package.static_meshes) {
    if (instance.mesh_source_asset_id == mesh->source_asset_id) {
      return &instance;
    }
  }
  return nullptr;
}

std::array<const TextureBinding *, 6U>
Bindings(const MaterialDescriptor &material) {
  return {{
      &material.base_color_texture,
      &material.metallic_roughness_texture,
      &material.normal_texture,
      &material.occlusion_texture,
      &material.emissive_texture,
      &material.specular_texture,
  }};
}

void TestCheckedFixtureDecodesToCanonicalJoinedInputs() {
  static_assert(
      std::is_same_v<decltype(NativeRenderAssetPackageDecodeResult{}.package),
                     std::shared_ptr<const NativeRenderAssetPackage>>,
      "decoded package publication must be immutable");
  const std::vector<std::uint8_t> bytes = ReadFixture();
  const RenderPayloadDigest checked_digest =
      DigestFromHex(kCheckedPackageSha256);
  const NativeRenderAssetPackageDecodeResult decoded =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(), checked_digest);
  Require(decoded.ok(), "checked package did not decode");
  const NativeRenderAssetPackage &package = *decoded.package;
  Require(package.version == 1U, "decoded package version changed");
  Require(package.package_sha256 == checked_digest &&
              package.package_id == "rorng_a0_road_tile_12m" &&
              package.origin_class == "project_original",
          "trusted package identity/provenance changed");
  const RenderPayloadDigest zero_digest{};
  Require(package.compiler_sha256 != zero_digest &&
              package.generator_sha256 != zero_digest &&
              package.glb_sha256 != zero_digest &&
              package.composition_sha256 != zero_digest &&
              package.source_manifest_sha256 != zero_digest,
          "decoded provenance hashes were not bound into the package result");
  Require(package.assets.size() == 21U, "decoded asset count changed");
  Require(package.static_meshes.size() == 5U,
          "decoded static instance count changed");
  Require(package.provenance_manifest_json.find(
              "\"origin_class\":\"project_original\"") !=
              std::string::npos,
          "A0 project-original origin is absent");
  Require(package.provenance_manifest_json.find(
              "\"ambient_occlusion\":false") != std::string::npos &&
              package.provenance_manifest_json.find("\"lods\":false") !=
                  std::string::npos &&
              package.provenance_manifest_json.find("\"collision\":false") !=
                  std::string::npos &&
              package.provenance_manifest_json.find(
                  "\"native_terrain\":false") != std::string::npos,
          "bounded capability non-claims are absent");
  Require(package.provenance_manifest_json.find(".mesh") == std::string::npos &&
              package.provenance_manifest_json.find(".material") ==
                  std::string::npos &&
              package.provenance_manifest_json.find(".odef") ==
                  std::string::npos &&
              package.provenance_manifest_json.find("RenderAssetDelta") ==
                  std::string::npos,
          "native manifest routed through a forbidden legacy/delta artifact");

  std::array<std::size_t, 5U> kind_counts{};
  std::uint64_t previous_asset_id = 0U;
  for (const GraphicsSceneAssetInput &input : package.assets) {
    Require(input.source_asset_id > previous_asset_id,
            "decoded assets are not in canonical source-ID order");
    previous_asset_id = input.source_asset_id;
    const RenderAssetKind kind = RenderAssetPayloadKind(*input.payload);
    ++kind_counts[static_cast<std::size_t>(kind)];
  }
  Require(kind_counts[static_cast<std::size_t>(RenderAssetKind::MESH)] == 5U &&
              kind_counts[static_cast<std::size_t>(RenderAssetKind::TEXTURE)] ==
                  10U &&
              kind_counts[static_cast<std::size_t>(RenderAssetKind::MATERIAL)] ==
                  4U &&
              kind_counts[static_cast<std::size_t>(RenderAssetKind::SAMPLER)] ==
                  2U,
          "decoded asset-kind counts changed");
  std::uint64_t previous_object_id = 0U;
  std::array<bool, 8U> observed_instance_flags{};
  for (const GraphicsSceneStaticMeshInput &instance : package.static_meshes) {
    Require(instance.source_object_id > previous_object_id,
            "decoded instances are not in canonical source-ID order");
    previous_object_id = instance.source_object_id;
    Require(instance.visibility_mask == 0xFFFFFFFFU && instance.flags < 8U,
            "fixture instance visibility/shadow policy is invalid");
    observed_instance_flags[instance.flags] = true;
  }
  Require(observed_instance_flags[0U] && observed_instance_flags[6U] &&
              observed_instance_flags[7U],
          "fixture lost its inert/receiver/caster-receiver shadow policy split");
  const GraphicsSceneStaticMeshInput *lane_instance =
      FindInstanceForMesh(package, "rorng_a0_lane_decal_mesh");
  const GraphicsSceneStaticMeshInput *reflector_instance =
      FindInstanceForMesh(package, "rorng_a0_reflector_mesh");
  const GraphicsSceneStaticMeshInput *gate_instance =
      FindInstanceForMesh(package, "rorng_a0_road_shadow_gate_mesh");
  const GraphicsSceneStaticMeshInput *road_instance =
      FindInstanceForMesh(package, "rorng_a0_road_surface_mesh");
  const GraphicsSceneStaticMeshInput *wet_instance =
      FindInstanceForMesh(package, "rorng_a0_wet_asphalt_mesh");
  Require(lane_instance != nullptr && lane_instance->flags == 0U &&
              reflector_instance != nullptr && reflector_instance->flags == 0U &&
              gate_instance != nullptr && gate_instance->flags == 7U &&
              road_instance != nullptr && road_instance->flags == 6U &&
              wet_instance != nullptr && wet_instance->flags == 6U,
          "decoded instance flags no longer preserve the authored RT mapping");

  const GraphicsSceneAssetInput *lane =
      FindAsset(package, "rorng_a0_lane_decal_material");
  const GraphicsSceneAssetInput *reflector =
      FindAsset(package, "rorng_a0_reflector_material");
  const GraphicsSceneAssetInput *road =
      FindAsset(package, "rorng_a0_road_surface_material");
  const GraphicsSceneAssetInput *wet =
      FindAsset(package, "rorng_a0_wet_asphalt_material");
  Require(lane != nullptr && reflector != nullptr && road != nullptr &&
              wet != nullptr,
          "one of the four fixture materials is absent");
  const auto &lane_material = std::get<MaterialDescriptor>(*lane->payload);
  const auto &reflector_material =
      std::get<MaterialDescriptor>(*reflector->payload);
  const auto &road_material = std::get<MaterialDescriptor>(*road->payload);
  const auto &wet_material = std::get<MaterialDescriptor>(*wet->payload);
  Require(lane_material.alpha_test_mode ==
              MaterialAlphaTestMode::GREATER_EQUAL &&
              lane->material_bindings[0U].texture_source_asset_id != 0U,
          "alpha-tested lane declaration changed");
  Require(reflector_material.pbr_workflow == MaterialPbrWorkflow::SPECULAR &&
              reflector_material.emissive_strength == 4.0F &&
              reflector->material_bindings[4U].texture_source_asset_id != 0U &&
              reflector->material_bindings[5U].texture_source_asset_id != 0U,
          "specular/emissive reflector declaration changed");
  Require(road_material.pbr_workflow ==
              MaterialPbrWorkflow::METALLIC_ROUGHNESS &&
              road->material_bindings[0U].texture_source_asset_id != 0U &&
              road->material_bindings[1U].texture_source_asset_id != 0U &&
              road->material_bindings[2U].texture_source_asset_id != 0U,
          "base/normal/metallic-roughness road declaration changed");
  Require(wet_material.pbr_workflow == MaterialPbrWorkflow::SPECULAR &&
              wet_material.roughness_factor == 0.08F &&
              wet->material_bindings[0U].texture_source_asset_id != 0U &&
              wet->material_bindings[2U].texture_source_asset_id != 0U &&
              wet->material_bindings[5U].texture_source_asset_id != 0U,
          "wet specular/normal lighting declaration changed");

  const GraphicsSceneAssetInput *road_base_input =
      FindAsset(package, "rorng_a0_road_base");
  const GraphicsSceneAssetInput *road_mr_input =
      FindAsset(package, "rorng_a0_road_metallic_roughness");
  const GraphicsSceneAssetInput *road_normal_input =
      FindAsset(package, "rorng_a0_road_normal");
  const GraphicsSceneAssetInput *wet_base_input =
      FindAsset(package, "rorng_a0_wet_base");
  const GraphicsSceneAssetInput *wet_normal_input =
      FindAsset(package, "rorng_a0_wet_normal");
  const GraphicsSceneAssetInput *wet_specular_input =
      FindAsset(package, "rorng_a0_wet_specular");
  const GraphicsSceneAssetInput *lane_base_input =
      FindAsset(package, "rorng_a0_lane_base");
  const GraphicsSceneAssetInput *reflector_emissive_input =
      FindAsset(package, "rorng_a0_reflector_emissive");
  Require(road_base_input != nullptr && road_mr_input != nullptr &&
              road_normal_input != nullptr && wet_base_input != nullptr &&
              wet_normal_input != nullptr && wet_specular_input != nullptr &&
              lane_base_input != nullptr && reflector_emissive_input != nullptr,
          "lighting-response texture inputs are absent");
  const auto &road_base_texture =
      std::get<TextureResourceDescriptor>(*road_base_input->payload);
  const auto &road_mr_texture =
      std::get<TextureResourceDescriptor>(*road_mr_input->payload);
  const auto &road_normal_texture =
      std::get<TextureResourceDescriptor>(*road_normal_input->payload);
  const auto &wet_base_texture =
      std::get<TextureResourceDescriptor>(*wet_base_input->payload);
  const auto &wet_normal_texture =
      std::get<TextureResourceDescriptor>(*wet_normal_input->payload);
  const auto &wet_specular_texture =
      std::get<TextureResourceDescriptor>(*wet_specular_input->payload);
  const auto &lane_base_texture =
      std::get<TextureResourceDescriptor>(*lane_base_input->payload);
  const auto &reflector_emissive_texture =
      std::get<TextureResourceDescriptor>(*reflector_emissive_input->payload);
  const std::array<const TextureResourceDescriptor *, 6U> surface_textures{{
      &road_base_texture,
      &road_mr_texture,
      &road_normal_texture,
      &wet_base_texture,
      &wet_normal_texture,
      &wet_specular_texture,
  }};
  Require(std::all_of(surface_textures.begin(), surface_textures.end(),
                      [](const TextureResourceDescriptor *texture) {
                        return texture->version ==
                                   kTextureResourceDescriptorVersion &&
                               texture->format ==
                                   TextureResourceFormat::RGBA8_UNORM &&
                               texture->width == 512U &&
                               texture->height == 512U &&
                               texture->mip_levels.size() == 10U;
                      }),
          "wire-v1 textures were not upgraded or lost their exact RGBA8 mip "
          "chain");
  Require(road_base_texture.color_space == TextureColorSpace::SRGB &&
              road_mr_texture.color_space == TextureColorSpace::LINEAR &&
              road_normal_texture.color_space == TextureColorSpace::LINEAR &&
              wet_base_texture.color_space == TextureColorSpace::SRGB &&
              wet_normal_texture.color_space == TextureColorSpace::LINEAR &&
              wet_specular_texture.color_space == TextureColorSpace::LINEAR &&
              lane_base_texture.color_space == TextureColorSpace::SRGB &&
              reflector_emissive_texture.color_space == TextureColorSpace::SRGB,
          "lighting-response color-space or mip declarations changed");
  bool alpha_zero = false;
  bool alpha_partial = false;
  bool alpha_opaque = false;
  const std::vector<std::uint8_t> &lane_pixels =
      lane_base_texture.mip_levels.front().bytes;
  for (std::size_t offset = 3U; offset < lane_pixels.size(); offset += 4U) {
    alpha_zero = alpha_zero || lane_pixels[offset] == 0U;
    alpha_partial = alpha_partial || lane_pixels[offset] == 96U;
    alpha_opaque = alpha_opaque || lane_pixels[offset] == 255U;
  }
  Require(alpha_zero && alpha_partial && alpha_opaque,
          "lane alpha source lost transparent/edge/opaque coverage");
  const std::vector<std::uint8_t> &emissive_pixels =
      reflector_emissive_texture.mip_levels.front().bytes;
  bool emissive_hot_core = false;
  for (std::size_t offset = 0U; offset < emissive_pixels.size(); offset += 4U) {
    emissive_hot_core = emissive_hot_core || emissive_pixels[offset] >= 230U;
  }
  Require(emissive_hot_core,
          "reflector emissive source lost its authored hot core");
  const std::vector<std::uint8_t> &wet_specular_pixels =
      wet_specular_texture.mip_levels.front().bytes;
  for (std::size_t offset = 0U; offset < wet_specular_pixels.size();
       offset += 4U) {
    Require(wet_specular_pixels[offset] >= 220U,
            "wet specular map lost its high-response floor");
  }
  const std::vector<std::uint8_t> &roughness_pixels =
      road_mr_texture.mip_levels.front().bytes;
  Require(!roughness_pixels.empty() && roughness_pixels.size() % 4U == 0U,
          "rough asphalt map payload is malformed");
  for (std::size_t offset = 0U; offset < roughness_pixels.size(); offset += 4U) {
    Require(roughness_pixels[offset + 1U] >= 198U &&
                roughness_pixels[offset + 2U] == 0U,
            "rough asphalt G/B channels stopped proving rough non-metal input");
  }
  const std::vector<std::uint8_t> &normal_pixels =
      road_normal_texture.mip_levels.front().bytes;
  Require(std::any_of(normal_pixels.begin(), normal_pixels.end(),
                      [](std::uint8_t value) {
                        return value != 128U && value != 254U && value != 255U;
                      }),
          "road normal map lost authored surface variation");
  const GraphicsSceneAssetInput *repeat_sampler_input =
      FindAsset(package, "rorng_a0_mipped_repeat_sampler");
  const GraphicsSceneAssetInput *clamp_sampler_input =
      FindAsset(package, "rorng_a0_mipped_clamp_sampler");
  Require(repeat_sampler_input != nullptr && clamp_sampler_input != nullptr,
          "explicit native sampler inputs are absent");
  const auto &repeat_sampler =
      std::get<SamplerResourceDescriptor>(*repeat_sampler_input->payload);
  const auto &clamp_sampler =
      std::get<SamplerResourceDescriptor>(*clamp_sampler_input->payload);
  Require(repeat_sampler.minification_filter == SamplerFilter::LINEAR &&
              repeat_sampler.magnification_filter == SamplerFilter::LINEAR &&
              repeat_sampler.mip_filter == SamplerFilter::LINEAR &&
              repeat_sampler.address_u == SamplerAddressMode::REPEAT &&
              repeat_sampler.address_v == SamplerAddressMode::REPEAT &&
              repeat_sampler.anisotropy_enabled &&
              repeat_sampler.maximum_anisotropy == 4.0F &&
              repeat_sampler.mip_lod_bias == 0.0F &&
              !std::signbit(repeat_sampler.mip_lod_bias) &&
              repeat_sampler.maximum_lod == 9.0F &&
              clamp_sampler.address_u == SamplerAddressMode::CLAMP_TO_EDGE &&
              clamp_sampler.address_v == SamplerAddressMode::CLAMP_TO_EDGE &&
              clamp_sampler.maximum_lod == 3.0F,
          "explicit mipped sampler policy changed");
  for (const GraphicsSceneAssetInput *input : {lane, reflector, road, wet}) {
    const MaterialDescriptor &material =
        std::get<MaterialDescriptor>(*input->payload);
    for (const TextureBinding *binding : Bindings(material)) {
      Require(IsAbsentRenderAssetReference(binding->texture) &&
                  IsAbsentRenderAssetReference(binding->sampler),
              "offline package manufactured runtime RenderAssetReferences");
    }
    for (std::size_t slot = 6U;
         slot < kGraphicsSceneMaterialTextureSlotCount; ++slot) {
      Require(input->material_bindings[slot] == GraphicsSceneAssetBinding{},
              "v1 package manufactured a native-only detail binding");
    }
    Require(IsAbsentRenderAssetReference(
                material.detail_weight_texture.texture) &&
                IsAbsentRenderAssetReference(
                    material.detail_weight_texture.sampler),
            "v1 package manufactured a detail-weight runtime reference");
    for (const TextureBinding &binding : material.detail_textures) {
      Require(IsAbsentRenderAssetReference(binding.texture) &&
                  IsAbsentRenderAssetReference(binding.sampler),
              "v1 package manufactured a detail runtime reference");
    }
  }

  const GraphicsSceneAssetInput *road_mesh_input =
      FindAsset(package, "rorng_a0_road_surface_mesh");
  Require(road_mesh_input != nullptr, "road surface mesh is absent");
  const MeshResourceDescriptor &road_mesh =
      std::get<MeshResourceDescriptor>(*road_mesh_input->payload);
  Require(road_mesh.local_bounds.minimum == Float3{-3.0F, 0.0F, -6.0F} &&
              road_mesh.local_bounds.maximum == Float3{3.0F, 0.0F, 6.0F},
          "12 m road surface bounds changed");
  const GraphicsSceneAssetInput *caster_mesh_input =
      FindAsset(package, "rorng_a0_road_shadow_gate_mesh");
  Require(caster_mesh_input != nullptr, "raised shadow gate mesh is absent");
  const MeshResourceDescriptor &caster_mesh =
      std::get<MeshResourceDescriptor>(*caster_mesh_input->payload);
  Require(caster_mesh.local_bounds.minimum == Float3{-1.8F, 0.0F, -1.65F} &&
              caster_mesh.local_bounds.maximum ==
                  Float3{1.8F, 1.45F, -1.35F},
          "raised shadow gate bounds changed");

  const NativeRenderAssetPackageDecodeResult repeated =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(), checked_digest);
  Require(repeated.ok(), "identical repeat decode failed");
  Require(repeated.package->body_sha256 == package.body_sha256,
          "repeat decode changed package digest");
  for (std::size_t index = 0U; index < package.assets.size(); ++index) {
    Require(package.assets[index].source_asset_id ==
                repeated.package->assets[index].source_asset_id &&
                EquivalentRenderAssetPayload(
                    *package.assets[index].payload,
                    *repeated.package->assets[index].payload),
            "repeat decode changed canonical asset contents");
  }
}

void TestSyntheticV2TransmissionRemainsBackwardCompatible() {
  const std::vector<std::uint8_t> bytes =
      MakeSyntheticTransmissionV2Package();
  const NativeRenderAssetPackageDecodeResult decoded =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                     TrustedDigest(bytes));
  Require(decoded.ok(), "synthetic checked v2 package did not decode");
  const NativeRenderAssetPackage &package = *decoded.package;
  Require(package.version == kNativeRenderAssetPackageTransmissionVersion &&
              package.provenance_manifest_json.find(
                  "ror-native-render-package-manifest-v2") !=
                  std::string::npos,
          "v2 package identity or embedded manifest changed");

  std::size_t mesh_count = 0U;
  std::size_t material_count = 0U;
  for (const GraphicsSceneAssetInput &input : package.assets) {
    if (const auto *mesh =
            std::get_if<MeshResourceDescriptor>(input.payload.get())) {
      ++mesh_count;
      Require(mesh->distance_lod_levels.empty(),
              "v2 mesh unexpectedly decoded a v3 LOD ladder");
    } else if (const auto *material =
                   std::get_if<MaterialDescriptor>(input.payload.get())) {
      ++material_count;
      Require(material->version == kMaterialDescriptorTransmissionVersion,
              "v2 material did not decode through the transmission wire path");
    }
  }
  Require(mesh_count == 5U && material_count == 4U,
          "synthetic v2 asset counts changed");

  const GraphicsSceneAssetInput *reflector =
      FindAsset(package, "rorng_a0_reflector_material");
  Require(reflector != nullptr, "synthetic v2 thin-slab material is absent");
  const auto &glass = std::get<MaterialDescriptor>(*reflector->payload);
  Require(glass.transmission_mode ==
                  MaterialTransmissionMode::THIN_PARALLEL_SLAB &&
              glass.transmission_factor == 0.75F &&
              glass.attenuation_color == Float3{0.8F, 0.9F, 1.0F} &&
              glass.attenuation_distance_m == 2.0F &&
              glass.slab_thickness_m == 0.1F && !glass.depth_write,
          "v2 thin-slab transmission fields changed during decode");
}

void TestHeaderDigestAndBoundsFailClosed() {
  const std::vector<std::uint8_t> fixture = ReadFixture();
  const RenderPayloadDigest empty_digest{};
  const NativeRenderAssetPackageDecodeResult null_result =
      DecodeNativeRenderAssetPackage(nullptr, 0U, empty_digest);
  Require(!null_result.ok() && null_result.package == nullptr,
          "null package input was accepted");
  const std::uint8_t one = 0U;
  const NativeRenderAssetPackageDecodeResult oversized =
      DecodeNativeRenderAssetPackage(
          &one, kMaximumNativeRenderAssetPackageBytes + 1U, empty_digest);
  Require(!oversized.ok() && oversized.package == nullptr,
          "oversized package input was accepted before access");
  for (std::size_t size = 0U; size < 80U; ++size) {
    const NativeRenderAssetPackageDecodeResult truncated =
        DecodeNativeRenderAssetPackage(fixture.data(), size, empty_digest);
    Require(!truncated.ok() && truncated.package == nullptr,
            "truncated package header was accepted");
  }

  std::vector<std::uint8_t> bytes = fixture;
  bytes[0U] ^= 1U;
  RequireDecodeFailure(bytes, "wrong magic was accepted");
  bytes = fixture;
  PutU32(bytes, 8U, 2U);
  RequireDecodeFailure(bytes, "unknown package version was accepted");
  bytes = fixture;
  PutU32(bytes, 12U, 79U);
  RequireDecodeFailure(bytes, "wrong header size was accepted");
  bytes = fixture;
  PutU32(bytes, 16U, 1U);
  RequireDecodeFailure(bytes, "unknown package flags were accepted");
  bytes = fixture;
  PutU32(bytes, 20U, U32(bytes, 20U) + 1U);
  RequireDecodeFailure(bytes, "inconsistent record count was accepted");
  bytes = fixture;
  PutU64(bytes, 32U, bytes.size() + 1U);
  RequireDecodeFailure(bytes, "wrong declared package size was accepted");
  bytes = fixture;
  bytes[40U] ^= 1U;
  RequireDecodeFailure(bytes, "wrong body digest was accepted");
  bytes = fixture;
  bytes[72U] = 1U;
  RequireDecodeFailure(bytes, "nonzero header reserve was accepted");
  bytes = fixture;
  bytes.back() ^= 1U;
  RequireDecodeFailure(bytes, "tampered body was accepted without digest update");
}

void TestCanonicalRecordAndSemanticMutationsFailClosed() {
  const std::vector<std::uint8_t> fixture = ReadFixture();
  const std::vector<RecordView> fixture_records = Records(fixture);
  Require(fixture_records.size() == 27U, "fixture record count changed");

  std::vector<std::uint8_t> bytes = fixture;
  bytes = ReplaceManifestPayload(
      fixture, "\"counts\":{\"assets\":21", "\"counts\":{\"assets\":20");
  RequireDecodeFailure(bytes,
                       "manifest count disagreeing with records was accepted");

  bytes = ReplaceManifestPayload(
      fixture,
      "\"kind\":\"mesh\",\"logical_id\":\"rorng_a0_lane_decal_mesh\"",
      "\"kind\":\"xxxx\",\"logical_id\":\"rorng_a0_lane_decal_mesh\"");
  RequireDecodeFailure(bytes, "manifest asset type mutation was accepted");

  bytes = ReplaceManifestPayload(
      fixture, "\"logical_id\":\"rorng_a0_lane_decal_mesh\"",
      "\"logical_id\":\"rorng_a0_lane_decal_mesu\"");
  RequireDecodeFailure(bytes, "manifest asset identity mutation was accepted");

  bytes = ReplaceManifestPayload(
      fixture,
      "\"flags\":[\"casts_shadow\",\"receives_shadow\",\"visible_in_reflections\"]",
      "\"flags\":[\"receives_shadow\",\"visible_in_reflections\"]");
  RequireDecodeFailure(bytes,
                       "manifest instance flag mutation was accepted");

  bytes = ReplaceManifestPayload(fixture, "\"visual_only\":true",
                                 "\"visual_only\":false");
  RequireDecodeFailure(bytes, "manifest capability claim mutation was accepted");

  bytes = ReplaceManifestPayload(
      fixture, "ror-native-render-compiler-v1",
      "ror-native-render-compiler-v2");
  RequireDecodeFailure(bytes, "manifest compiler format mutation was accepted");

  bytes = ReplaceManifestPayload(fixture, "project_original",
                                 "clean_room_recreation");
  RequirePinnedDecodeFailure(
      bytes, "trusted package digest did not bind the manifest origin");

  const std::string glb_path =
      "content-source/native_render/a0_road_tile_12m/"
      "rorng_a0_road_tile_12m.glb";
  const std::array<std::pair<std::string, const char *>, 6U> hostile_paths{{
      {"./ntent-source/native_render/a0_road_tile_12m/"
       "rorng_a0_road_tile_12m.glb",
       "leading dot-segment manifest path was accepted"},
      {"content-source/./native_render/a0_road_tile_12m/"
       "rorng_a0_road_tile_12m.glb",
       "embedded dot-segment manifest path was accepted"},
      {"content-source//native_render/a0_road_tile_12m/"
       "rorng_a0_road_tile_12m.glb",
       "empty manifest path segment was accepted"},
      {"content-source/native_render/../a0_road_tile_12m/"
       "rorng_a0_road_tile_12m.glb",
       "parent manifest path segment was accepted"},
      {"/content-source/native_render/a0_road_tile_12m/"
       "rorng_a0_road_tile_12m.glb",
       "absolute manifest path was accepted"},
      {"content-source\\native_render/a0_road_tile_12m/"
       "rorng_a0_road_tile_12m.glb",
       "backslash manifest path was accepted"},
  }};
  for (const auto &hostile_path : hostile_paths) {
    bytes = ReplaceManifestPayload(fixture, glb_path, hostile_path.first);
    const NativeRenderAssetPackageDecodeResult path_result =
        DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                       TrustedDigest(bytes));
    Require(!path_result.ok() && path_result.package == nullptr &&
                path_result.validation.field == "native.package.manifest",
            hostile_path.second);
  }

  const RecordView original_manifest = fixture_records.front();
  const std::string original_manifest_text(
      reinterpret_cast<const char *>(fixture.data() +
                                     original_manifest.payload_offset),
      original_manifest.payload_size);
  const std::size_t compiler_object =
      original_manifest_text.find("\"compiler\":{");
  Require(compiler_object != std::string::npos,
          "manifest compiler object is absent");
  const std::size_t compiler_hash =
      original_manifest_text.find("\"sha256\":\"", compiler_object);
  Require(compiler_hash != std::string::npos,
          "manifest compiler/dependency hash is absent");
  const std::size_t compiler_hash_value =
      compiler_hash + std::string("\"sha256\":\"").size();
  const std::string compiler_digest =
      original_manifest_text.substr(compiler_hash_value, 64U);
  std::string changed_compiler_digest = compiler_digest;
  changed_compiler_digest[0U] =
      changed_compiler_digest[0U] == '0' ? '1' : '0';
  bytes = ReplaceManifestPayload(fixture, compiler_digest,
                                 changed_compiler_digest);
  RequirePinnedDecodeFailure(
      bytes, "trusted package digest did not bind compiler hashes");

  const std::size_t source_object = original_manifest_text.find("\"source\":{");
  Require(source_object != std::string::npos,
          "manifest source object is absent");
  const std::size_t source_hash =
      original_manifest_text.find("\"sha256\":\"", source_object);
  Require(source_hash != std::string::npos,
          "manifest source hash is absent");
  const std::size_t hash_value = source_hash + std::string("\"sha256\":\"").size();
  const std::string source_digest =
      original_manifest_text.substr(hash_value, 64U);
  std::string changed_source_digest = source_digest;
  changed_source_digest[0U] =
      changed_source_digest[0U] == '0' ? '1' : '0';
  bytes = ReplaceManifestPayload(fixture, source_digest,
                                 changed_source_digest);
  RequirePinnedDecodeFailure(
      bytes, "trusted package digest did not bind source hashes");

  bytes = fixture;
  const RecordView named_mesh = FindRecord(bytes, 2U);
  const std::uint32_t named_mesh_size = U32(bytes, named_mesh.payload_offset + 4U);
  Require(named_mesh_size != 0U, "mesh name is empty");
  const std::size_t final_name_byte =
      named_mesh.payload_offset + 8U + named_mesh_size - 1U;
  bytes[final_name_byte] =
      bytes[final_name_byte] == static_cast<std::uint8_t>('x')
          ? static_cast<std::uint8_t>('y')
          : static_cast<std::uint8_t>('x');
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "decoded payload name diverged from manifest");

  bytes = fixture;
  bytes[fixture_records[0U].payload_offset + 1U] =
      static_cast<std::uint8_t>(' ');
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "noncanonical manifest whitespace was accepted");

  bytes = fixture;
  const RecordView manifest = fixture_records[0U];
  const std::string manifest_text(
      reinterpret_cast<const char *>(bytes.data() + manifest.payload_offset),
      manifest.payload_size);
  const std::string sorted_pair = "\"assets\":[";
  const std::size_t sorted_pair_offset = manifest_text.find(sorted_pair);
  Require(sorted_pair_offset != std::string::npos,
          "canonical manifest test key is absent");
  bytes[manifest.payload_offset + sorted_pair_offset + 1U] =
      static_cast<std::uint8_t>('z');
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "unsorted manifest keys were accepted");

  bytes = fixture;
  bytes[manifest.payload_offset + 1U] = static_cast<std::uint8_t>(']');
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "syntactically malformed manifest was accepted");

  bytes = fixture;
  const std::string count_pair = "\"assets\":21";
  const std::size_t count_pair_offset = manifest_text.find(count_pair);
  Require(count_pair_offset != std::string::npos,
          "canonical manifest count is absent");
  bytes[manifest.payload_offset + count_pair_offset + count_pair.size() - 2U] =
      static_cast<std::uint8_t>('0');
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "noncanonical manifest integer was accepted");

  bytes = fixture;
  PutU32(bytes, fixture_records[1U].header_offset + 4U, 1U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "nonzero record flags were accepted");

  bytes = fixture;
  PutU64(bytes, fixture_records[1U].header_offset + 8U, 0U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "zero asset source identity was accepted");

  bytes = fixture;
  PutU64(bytes, fixture_records[2U].header_offset + 8U,
         fixture_records[1U].source_id);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "duplicate/non-increasing asset identity was accepted");

  bytes = fixture;
  PutU32(bytes, fixture_records[1U].header_offset, 99U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "unknown asset record type was accepted");

  bytes = fixture;
  PutU64(bytes, fixture_records[1U].header_offset + 16U,
         static_cast<std::uint64_t>(bytes.size()));
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "escaping record payload length was accepted");

  bytes = fixture;
  const RecordView mesh = FindRecord(bytes, 2U);
  PutU32(bytes, mesh.payload_offset, 2U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "unknown mesh descriptor version was accepted");

  bytes = fixture;
  const RecordView road_mesh_record =
      FindRecord(bytes, 2U, "rorng_a0_road_surface_mesh");
  const std::uint32_t road_mesh_name =
      U32(bytes, road_mesh_record.payload_offset + 4U);
  const std::size_t road_mesh_counts =
      road_mesh_record.payload_offset + 8U + road_mesh_name + 4U + 8U + 24U;
  const std::uint32_t road_vertex_count = U32(bytes, road_mesh_counts);
  const std::size_t road_tangents =
      road_mesh_counts + 32U +
      static_cast<std::size_t>(road_vertex_count) * 24U;
  PutU32(bytes, road_tangents, 0xBF800000U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "opposite-U package tangent was accepted");

  bytes = fixture;
  PutU32(bytes, road_tangents + 12U, 0x3F800000U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "flipped package tangent handedness was accepted");

  bytes = fixture;
  const std::size_t road_indices =
      road_mesh_counts + 32U +
      static_cast<std::size_t>(road_vertex_count) * 48U;
  const std::uint32_t road_second_index = U32(bytes, road_indices + 4U);
  const std::uint32_t road_third_index = U32(bytes, road_indices + 8U);
  PutU32(bytes, road_indices + 4U, road_third_index);
  PutU32(bytes, road_indices + 8U, road_second_index);
  RefreshBodyDigest(bytes);
  const NativeRenderAssetPackageDecodeResult winding =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                     TrustedDigest(bytes));
  Require(!winding.ok() && winding.package == nullptr &&
              winding.validation.field == "native.mesh.winding",
          "reversed package triangle winding was accepted");

  bytes = fixture;
  const std::uint32_t mesh_name = U32(bytes, mesh.payload_offset + 4U);
  const std::size_t mesh_topology_revision =
      mesh.payload_offset + 8U + mesh_name + 4U;
  PutU64(bytes, mesh_topology_revision, 2U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "noncanonical mesh topology revision was accepted");

  bytes = fixture;
  const std::size_t mesh_state = mesh.payload_offset + 8U + mesh_name;
  bytes[mesh_state + 1U] = 1U;
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "nonminimal mesh index format was accepted");

  bytes = fixture;
  const std::size_t mesh_bounds = mesh_topology_revision + 8U;
  PutU32(bytes, mesh_bounds, 0xC1000000U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "non-exact mesh bounds were accepted");

  bytes = fixture;
  const std::size_t mesh_counts = mesh_bounds + 24U;
  PutU32(bytes, mesh_counts, 4000000U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes,
                       "mesh count exceeding its record bytes was accepted");

  bytes = fixture;
  PutU32(bytes, mesh_counts + 4U, 0U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes,
                       "mesh missing its required v1 normal stream was accepted");

  bytes = fixture;
  const RecordView road_texture =
      FindRecord(bytes, 3U, "rorng_a0_road_base");
  const std::uint32_t road_texture_name =
      U32(bytes, road_texture.payload_offset + 4U);
  const std::size_t texture_state =
      road_texture.payload_offset + 8U + road_texture_name;

  bytes = fixture;
  PutU32(bytes, road_texture.payload_offset,
         kTextureResourceDescriptorVersion);
  RefreshBodyDigest(bytes);
  const NativeRenderAssetPackageDecodeResult current_wire =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                     TrustedDigest(bytes));
  Require(current_wire.ok(), "explicit wire-v2 RGBA8 texture was rejected");
  const GraphicsSceneAssetInput *current_wire_road =
      FindAsset(*current_wire.package, "rorng_a0_road_base");
  Require(current_wire_road != nullptr &&
              std::get<TextureResourceDescriptor>(*current_wire_road->payload)
                      .version == kTextureResourceDescriptorVersion,
          "wire-v2 RGBA8 texture did not publish the live descriptor version");

  bytes = fixture;
  bytes[texture_state + 1U] =
      static_cast<std::uint8_t>(TextureResourceFormat::BC4_UNORM);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes,
                       "block-compressed storage was reinterpreted under wire v1");

  bytes = fixture;
  bytes[texture_state + 2U] = 0U;
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "linear base-color source was accepted");

  bytes = fixture;
  const std::size_t texture_dimensions = texture_state + 4U;
  PutU32(bytes, texture_dimensions, 16385U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "oversized texture dimension was accepted");

  bytes = fixture;
  const RecordView sampler = FindRecord(bytes, 5U);
  const std::uint32_t sampler_name = U32(bytes, sampler.payload_offset + 4U);
  const std::size_t sampler_state = sampler.payload_offset + 8U + sampler_name;
  bytes[sampler_state + 6U] = 1U;
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "nonzero sampler reserve was accepted");

  bytes = fixture;
  const RecordView lane_material =
      FindRecord(bytes, 4U, "rorng_a0_lane_decal_material");
  const std::uint32_t lane_name = U32(bytes, lane_material.payload_offset + 4U);
  const std::size_t lane_factors =
      lane_material.payload_offset + 8U + lane_name + 8U;
  PutU32(bytes, lane_factors, 0x7FC00000U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "NaN material factor was accepted");

  bytes = fixture;
  const RecordView reflector_material =
      FindRecord(bytes, 4U, "rorng_a0_reflector_material");
  const std::uint32_t reflector_name =
      U32(bytes, reflector_material.payload_offset + 4U);
  const std::size_t reflector_state =
      reflector_material.payload_offset + 8U + reflector_name;
  bytes[reflector_state + 1U] = 0U;
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "workflow conflicting with source bindings was accepted");

  bytes = fixture;
  const std::size_t lane_bindings = lane_factors + 68U;
  PutU64(bytes, lane_bindings, 0x123456789ABCDEF0ULL);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "dangling material texture source was accepted");

  bytes = fixture;
  const RecordView instance = FindRecord(bytes, 6U);
  PutU64(bytes, instance.payload_offset + 4U, 0U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "instance with absent mesh source was accepted");

  bytes = fixture;
  PutU32(bytes, instance.payload_offset + 20U, 0U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "singular instance transform was accepted");

  bytes = fixture;
  // Binary64 multiplication is above 1e-8, but RenderMath's ordered
  // binary32 determinant is exactly 0x322bcc77 (1e-8F) and must be rejected.
  PutU32(bytes, instance.payload_offset + 20U, 0x4CBEBC20U); // 1e8F
  PutU32(bytes, instance.payload_offset + 40U, 0x24E69595U); // 1e-16F
  RefreshBodyDigest(bytes);
  const NativeRenderAssetPackageDecodeResult threshold_transform =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                     TrustedDigest(bytes));
  Require(!threshold_transform.ok() && threshold_transform.package == nullptr &&
              threshold_transform.validation.field == "native.instance",
          "binary32 determinant threshold transform was accepted");

  bytes = fixture;
  PutU32(bytes, instance.payload_offset + 20U, 0x4CBEBC20U); // 1e8F
  PutU32(bytes, instance.payload_offset + 40U,
         0x24E69596U); // next binary32 above 1e-16F
  RefreshBodyDigest(bytes);
  const NativeRenderAssetPackageDecodeResult above_threshold =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                     TrustedDigest(bytes));
  Require(above_threshold.ok(),
          "binary32 determinant immediately above threshold was rejected");

  bytes = fixture;
  PutU32(bytes, instance.payload_offset + 88U, 0x80000000U);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "unknown instance flags were accepted");

  bytes = fixture;
  PutU32(bytes, instance.payload_offset + 84U, 0x7FFFFFFFU);
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "noncanonical instance visibility was accepted");

  bytes = fixture;
  bytes.pop_back();
  PutU64(bytes, 32U, bytes.size());
  RefreshBodyDigest(bytes);
  RequireDecodeFailure(bytes, "truncated final record was accepted");
}

} // namespace

int main() {
  TestCheckedFixtureDecodesToCanonicalJoinedInputs();
  TestSyntheticV2TransmissionRemainsBackwardCompatible();
  TestHeaderDigestAndBoundsFailClosed();
  TestCanonicalRecordAndSemanticMutationsFailClosed();
  std::cout << "native render asset package tests passed\n";
  return EXIT_SUCCESS;
}
