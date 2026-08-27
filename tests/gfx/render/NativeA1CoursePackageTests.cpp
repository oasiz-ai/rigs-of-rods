/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "NativeRenderAssetPackage.h"

#include "MaterialDescriptor.h"
#include "RenderResourceDescriptors.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#ifndef ROR_NATIVE_A1_COURSE_PACKAGE_FIXTURE
#error "ROR_NATIVE_A1_COURSE_PACKAGE_FIXTURE must name the checked A1 package"
#endif

namespace {

using namespace RoR::Render;

constexpr char kCheckedSha256[] =
    "e420438797a77e4e49b91e3c6c930f39d340f99a4772ef989182a62605f2d53b";

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "native A1 course package test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::vector<std::uint8_t> ReadFixture() {
  std::ifstream stream(ROR_NATIVE_A1_COURSE_PACKAGE_FIXTURE,
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

RenderPayloadDigest DigestFromHex(const char *text) {
  RenderPayloadDigest digest{};
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    std::uint8_t byte = 0U;
    for (std::size_t nibble = 0U; nibble < 2U; ++nibble) {
      const char character = text[index * 2U + nibble];
      Require((character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f'),
              "SHA-256 literal is not lowercase hexadecimal");
      const std::uint8_t digit =
          character <= '9'
              ? static_cast<std::uint8_t>(character - '0')
              : static_cast<std::uint8_t>(character - 'a' + 10);
      byte = static_cast<std::uint8_t>((byte << 4U) | digit);
    }
    digest[index] = byte;
  }
  Require(text[digest.size() * 2U] == '\0', "SHA-256 literal length changed");
  return digest;
}

std::uint32_t U32(const std::vector<std::uint8_t> &bytes,
                  std::size_t offset) {
  Require(offset + 4U <= bytes.size(), "test read escaped package bytes");
  return static_cast<std::uint32_t>(bytes[offset]) |
         static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U |
         static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U |
         static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U;
}

std::uint64_t U64(const std::vector<std::uint8_t> &bytes,
                  std::size_t offset) {
  Require(offset + 8U <= bytes.size(), "test read escaped package bytes");
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[offset + index])
             << (index * 8U);
  }
  return value;
}

void PutU32(std::vector<std::uint8_t> &bytes, std::size_t offset,
            std::uint32_t value) {
  Require(offset + 4U <= bytes.size(), "test write escaped package bytes");
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] =
        static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
  }
}

void PutFloat(std::vector<std::uint8_t> &bytes, std::size_t offset,
              float value) {
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "package test requires IEEE binary32 storage");
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  PutU32(bytes, offset, bits);
}

struct RecordView {
  std::uint32_t type = 0U;
  std::size_t payload_offset = 0U;
  std::size_t payload_size = 0U;
};

std::vector<RecordView> Records(const std::vector<std::uint8_t> &bytes) {
  Require(bytes.size() >= kNativeRenderAssetPackageHeaderBytes,
          "package is too small for records");
  std::vector<RecordView> records;
  std::size_t offset = kNativeRenderAssetPackageHeaderBytes;
  while (offset < bytes.size()) {
    Require(offset + 24U <= bytes.size(), "record header is truncated");
    const std::uint64_t payload_size = U64(bytes, offset + 16U);
    Require(payload_size <= bytes.size() - offset - 24U,
            "record payload is truncated");
    records.push_back(RecordView{
        U32(bytes, offset), offset + 24U,
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
                      std::uint32_t type, const std::string &name) {
  for (const RecordView &record : Records(bytes)) {
    if (record.type == type && RecordName(bytes, record) == name) {
      return record;
    }
  }
  Require(false, "requested package record is absent");
  return {};
}

struct LodLevelWireView {
  std::size_t distance_offset = 0U;
  std::size_t index_count_offset = 0U;
  std::size_t indices_offset = 0U;
  std::uint32_t index_count = 0U;
};

struct MeshLodWireView {
  std::size_t level_count_offset = 0U;
  std::vector<LodLevelWireView> levels;
};

MeshLodWireView FindMeshLodWire(const std::vector<std::uint8_t> &bytes,
                                const std::string &name) {
  const RecordView record = FindRecord(bytes, 2U, name);
  Require(U32(bytes, record.payload_offset) == 2U,
          "checked v3 mesh record version changed");
  const std::uint32_t name_size = U32(bytes, record.payload_offset + 4U);
  std::size_t cursor = record.payload_offset + 8U + name_size + 12U + 24U;
  const std::size_t counts_offset = cursor;
  std::array<std::uint32_t, 8U> counts{};
  for (std::size_t index = 0U; index < counts.size(); ++index) {
    counts[index] = U32(bytes, counts_offset + index * 4U);
  }
  cursor += counts.size() * 4U;
  cursor += static_cast<std::size_t>(counts[0U]) * 12U;
  cursor += static_cast<std::size_t>(counts[1U]) * 12U;
  cursor += static_cast<std::size_t>(counts[2U]) * 16U;
  cursor += static_cast<std::size_t>(counts[3U]) * 12U;
  cursor += static_cast<std::size_t>(counts[4U]) * 8U;
  cursor += static_cast<std::size_t>(counts[5U]) * 8U;
  cursor += static_cast<std::size_t>(counts[6U]) * 16U;
  cursor += static_cast<std::size_t>(counts[7U]) * 4U;
  const std::size_t record_end = record.payload_offset + record.payload_size;
  Require(cursor + 4U <= record_end, "mesh LOD count is truncated");

  MeshLodWireView view;
  view.level_count_offset = cursor;
  const std::uint32_t level_count = U32(bytes, cursor);
  cursor += 4U;
  for (std::uint32_t level = 0U; level < level_count; ++level) {
    Require(cursor + 8U <= record_end, "mesh LOD header is truncated");
    const std::uint32_t index_count = U32(bytes, cursor + 4U);
    const std::size_t indices_offset = cursor + 8U;
    Require(index_count <= (record_end - indices_offset) / 4U,
            "mesh LOD indices are truncated");
    view.levels.push_back(
        LodLevelWireView{cursor, cursor + 4U, indices_offset, index_count});
    cursor = indices_offset + static_cast<std::size_t>(index_count) * 4U;
  }
  Require(cursor == record_end, "mesh LOD records have trailing bytes");
  return view;
}

RenderPayloadDigest IndexDigest(const std::vector<std::uint32_t> &indices) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(indices.size() * 4U);
  for (const std::uint32_t index : indices) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
      bytes.push_back(
          static_cast<std::uint8_t>((index >> (byte * 8U)) & 0xFFU));
    }
  }
  return ComputeRenderPayloadDigest(bytes.data(), bytes.size());
}

void RefreshPackageDigests(std::vector<std::uint8_t> &bytes,
                           RenderPayloadDigest &package_digest) {
  Require(bytes.size() > kNativeRenderAssetPackageHeaderBytes,
          "hostile package is truncated");
  const RenderPayloadDigest body_digest = ComputeRenderPayloadDigest(
      bytes.data() + kNativeRenderAssetPackageHeaderBytes,
      bytes.size() - kNativeRenderAssetPackageHeaderBytes);
  constexpr std::size_t kBodyDigestOffset = 40U;
  std::copy(body_digest.begin(), body_digest.end(),
            bytes.begin() + kBodyDigestOffset);
  package_digest = ComputeRenderPayloadDigest(bytes.data(), bytes.size());
}

void RequireHostileLodMutationFails(
    const std::string &mesh_name,
    const std::function<void(std::vector<std::uint8_t> &,
                             const MeshLodWireView &)> &mutate,
    const std::string &expected_field) {
  std::vector<std::uint8_t> bytes = ReadFixture();
  const MeshLodWireView wire = FindMeshLodWire(bytes, mesh_name);
  mutate(bytes, wire);
  RenderPayloadDigest digest{};
  RefreshPackageDigests(bytes, digest);
  const NativeRenderAssetPackageDecodeResult rejected =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(), digest);
  Require(!rejected.ok() && rejected.package == nullptr,
          "self-consistent hostile v3 LOD package was published");
  Require(rejected.validation.field == expected_field,
          "hostile v3 LOD package failed at an unexpected boundary");
}

const GraphicsSceneAssetInput *FindAsset(
    const NativeRenderAssetPackage &package, const std::string &name) {
  for (const GraphicsSceneAssetInput &asset : package.assets) {
    if (asset.payload == nullptr) {
      continue;
    }
    const bool matches = std::visit(
        [&name](const auto &value) {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, MeshResourceDescriptor> ||
                        std::is_same_v<Value, TextureResourceDescriptor> ||
                        std::is_same_v<Value, MaterialDescriptor> ||
                        std::is_same_v<Value, SamplerResourceDescriptor>) {
            return value.debug_name == name;
          }
          return false;
        },
        *asset.payload);
    if (matches) {
      return &asset;
    }
  }
  return nullptr;
}

void CheckTexture(const NativeRenderAssetPackage &package,
                  const std::string &name, std::uint32_t dimension,
                  std::size_t mip_count, TextureColorSpace color_space) {
  const GraphicsSceneAssetInput *asset = FindAsset(package, name);
  Require(asset != nullptr, "required texture is absent");
  const auto &texture = std::get<TextureResourceDescriptor>(*asset->payload);
  Require(texture.version == kTextureResourceDescriptorVersion &&
              texture.format == TextureResourceFormat::RGBA8_UNORM &&
              texture.width == dimension && texture.height == dimension &&
              texture.mip_levels.size() == mip_count &&
              texture.color_space == color_space &&
              texture.mip_levels.back().width == 1U &&
              texture.mip_levels.back().height == 1U,
          "wire-v1 texture upgrade, dimensions, color space, or mip chain "
          "changed");
}

void TestCheckedPackage() {
  const std::vector<std::uint8_t> bytes = ReadFixture();
  const RenderPayloadDigest digest = DigestFromHex(kCheckedSha256);
  Require(ComputeRenderPayloadDigest(bytes.data(), bytes.size()) == digest,
          "checked package SHA-256 changed");
  NativeRenderAssetPackageDecodeResult decoded =
      DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(), digest);
  Require(decoded.ok(), "checked package did not decode");
  const NativeRenderAssetPackage &package = *decoded.package;
  Require(package.version == 3U &&
              package.package_id == "rorng_a1_native_course_60m" &&
              package.origin_class == "project_original" &&
              package.package_sha256 == digest,
          "package identity changed");
  Require(package.assets.size() == 38U && package.static_meshes.size() == 9U,
          "package asset or instance count changed");
  Require(package.provenance_manifest_json.find("\"collision\":false") !=
                  std::string::npos &&
              package.provenance_manifest_json.find("\"native_terrain\":false") !=
                  std::string::npos &&
              package.provenance_manifest_json.find("\"lods\":true") !=
                  std::string::npos &&
              package.provenance_manifest_json.find("\"visual_only\":true") !=
                  std::string::npos,
          "package nonclaims changed");

  for (const char *name : {"rorng_a0_road_surface_mesh",
                           "rorng_a0_wet_asphalt_mesh",
                           "rorng_a0_road_shadow_gate_mesh",
                           "rorng_a1_barrier_mesh",
                           "rorng_a1_calibration_marker_mesh",
                           "rorng_a1_curb_mesh",
                           "rorng_a1_glass_slab_mesh",
                           "rorng_a1_lane_marking_mesh",
                           "rorng_a1_shoulder_mesh"}) {
    const GraphicsSceneAssetInput *mesh_asset = FindAsset(package, name);
    Require(mesh_asset != nullptr, "required course mesh is absent");
    const auto &mesh = std::get<MeshResourceDescriptor>(*mesh_asset->payload);
    Require(mesh.version == kMeshResourceDescriptorVersion,
            "wire-v3 mesh was not upgraded to the current descriptor");
  }

  const auto &barrier = std::get<MeshResourceDescriptor>(
      *FindAsset(package, "rorng_a1_barrier_mesh")->payload);
  Require(barrier.indices.size() == 720U &&
              barrier.distance_lod_levels.size() == 1U &&
              barrier.distance_lod_levels[0U].activation_distance_meters ==
                  35.0F &&
              barrier.distance_lod_levels[0U].indices.size() == 72U,
          "barrier distance LOD ladder changed");
  Require(IndexDigest(barrier.distance_lod_levels[0U].indices) ==
              DigestFromHex(
                  "587f5ea4da7e9971da359f665d776b0c60dff6474793e46d8227a43c9fcc4941"),
          "barrier LOD selected triangle chunks changed");
  const auto &calibration = std::get<MeshResourceDescriptor>(
      *FindAsset(package, "rorng_a1_calibration_marker_mesh")->payload);
  Require(calibration.indices.size() == 720U &&
              calibration.distance_lod_levels.size() == 2U &&
              calibration.distance_lod_levels[0U]
                      .activation_distance_meters == 30.0F &&
              calibration.distance_lod_levels[0U].indices.size() == 360U &&
              calibration.distance_lod_levels[1U]
                      .activation_distance_meters == 55.0F &&
              calibration.distance_lod_levels[1U].indices.size() == 216U,
          "calibration distance LOD ladder changed");
  Require(IndexDigest(calibration.distance_lod_levels[0U].indices) ==
                  DigestFromHex(
                      "1fd47222f7a1f4a6eea2cefa7271fb2be07c4cb2168500e3ca83cb416e2c6437") &&
              IndexDigest(calibration.distance_lod_levels[1U].indices) ==
                  DigestFromHex(
                      "f08801bac3224bfa5da7654d9eb26d772c8a8dacba3ee4147c49317454a49fdc"),
          "calibration LOD selected marker chunks changed");
  for (const char *name : {"rorng_a0_road_surface_mesh",
                           "rorng_a0_wet_asphalt_mesh",
                           "rorng_a0_road_shadow_gate_mesh",
                           "rorng_a1_curb_mesh",
                           "rorng_a1_glass_slab_mesh",
                           "rorng_a1_lane_marking_mesh",
                           "rorng_a1_shoulder_mesh"}) {
    const auto &mesh = std::get<MeshResourceDescriptor>(
        *FindAsset(package, name)->payload);
    Require(mesh.distance_lod_levels.empty(),
            "non-LOD A1 mesh unexpectedly gained a ladder");
  }

  CheckTexture(package, "rorng_a1_road_base", 1024U, 11U,
               TextureColorSpace::SRGB);
  CheckTexture(package, "rorng_a1_road_metallic_roughness", 1024U, 11U,
               TextureColorSpace::LINEAR);
  CheckTexture(package, "rorng_a1_road_normal", 1024U, 11U,
               TextureColorSpace::LINEAR);
  CheckTexture(package, "rorng_a1_wet_base", 1024U, 11U,
               TextureColorSpace::SRGB);
  CheckTexture(package, "rorng_a1_wet_normal", 1024U, 11U,
               TextureColorSpace::LINEAR);
  CheckTexture(package, "rorng_a1_wet_specular", 1024U, 11U,
               TextureColorSpace::LINEAR);
  CheckTexture(package, "rorng_a1_shoulder_base", 512U, 10U,
               TextureColorSpace::SRGB);

  const GraphicsSceneAssetInput *glass_asset =
      FindAsset(package, "rorng_a1_glass_material");
  Require(glass_asset != nullptr, "thin-slab glass material is absent");
  const auto &glass = std::get<MaterialDescriptor>(*glass_asset->payload);
  Require(glass.version == kMaterialDescriptorTransmissionVersion &&
              glass.pbr_workflow == MaterialPbrWorkflow::SPECULAR &&
              glass.transmission_mode ==
                  MaterialTransmissionMode::THIN_PARALLEL_SLAB &&
              glass.transmission_factor == 0.96F &&
              glass.attenuation_color == Float3{0.82F, 0.94F, 0.98F} &&
              glass.attenuation_distance_m == 0.75F &&
              glass.slab_thickness_m == 0.08F &&
              glass.index_of_refraction == 1.52F && !glass.depth_write &&
              !glass.base_color_texture.texture.valid() &&
              !glass.normal_texture.texture.valid(),
          "thin-slab glass material contract changed");

  std::vector<std::uint8_t> tampered = bytes;
  tampered.back() ^= 0x01U;
  NativeRenderAssetPackageDecodeResult rejected =
      DecodeNativeRenderAssetPackage(tampered.data(), tampered.size(), digest);
  Require(!rejected.ok() && rejected.package == nullptr,
          "tampered checked package was published");

  std::vector<std::uint8_t> vacuous_lod_claim = bytes;
  constexpr char kLodLevelCount[] = "\"lod_levels\":3";
  const auto lod_count = std::search(
      vacuous_lod_claim.begin(), vacuous_lod_claim.end(),
      reinterpret_cast<const std::uint8_t *>(kLodLevelCount),
      reinterpret_cast<const std::uint8_t *>(kLodLevelCount) +
          sizeof(kLodLevelCount) - 1U);
  Require(lod_count != vacuous_lod_claim.end(),
          "embedded LOD count was not found");
  *(lod_count + sizeof(kLodLevelCount) - 2U) =
      static_cast<std::uint8_t>('0');
  RenderPayloadDigest hostile_digest{};
  RefreshPackageDigests(vacuous_lod_claim, hostile_digest);
  rejected = DecodeNativeRenderAssetPackage(
      vacuous_lod_claim.data(), vacuous_lod_claim.size(), hostile_digest);
  Require(!rejected.ok() && rejected.package == nullptr &&
              rejected.validation.field == "native.package.manifest",
          "self-consistent vacuous v3 LOD claim was published");
}

void TestHostileDistanceLodWireDataFailsClosed() {
  RequireHostileLodMutationFails(
      "rorng_a1_barrier_mesh",
      [](std::vector<std::uint8_t> &bytes, const MeshLodWireView &wire) {
        PutU32(bytes, wire.level_count_offset, 0U);
      },
      "native.mesh");
  RequireHostileLodMutationFails(
      "rorng_a1_barrier_mesh",
      [](std::vector<std::uint8_t> &bytes, const MeshLodWireView &wire) {
        PutU32(bytes, wire.level_count_offset, 16U);
      },
      "native.mesh.distance_lod_levels");
  RequireHostileLodMutationFails(
      "rorng_a1_barrier_mesh",
      [](std::vector<std::uint8_t> &bytes, const MeshLodWireView &wire) {
        Require(wire.levels.size() == 1U, "barrier LOD wire count changed");
        PutU32(bytes, wire.levels[0U].distance_offset, 0x7FC00000U);
      },
      "native.mesh.distance_lod_levels");
  RequireHostileLodMutationFails(
      "rorng_a1_barrier_mesh",
      [](std::vector<std::uint8_t> &bytes, const MeshLodWireView &wire) {
        PutFloat(bytes, wire.levels[0U].distance_offset, -1.0F);
      },
      "native.mesh.distance_lod_levels");
  RequireHostileLodMutationFails(
      "rorng_a1_calibration_marker_mesh",
      [](std::vector<std::uint8_t> &bytes, const MeshLodWireView &wire) {
        Require(wire.levels.size() == 2U,
                "calibration LOD wire count changed");
        PutFloat(bytes, wire.levels[1U].distance_offset, 30.0F);
      },
      "native.mesh.distance_lod_levels");
  RequireHostileLodMutationFails(
      "rorng_a1_barrier_mesh",
      [](std::vector<std::uint8_t> &bytes, const MeshLodWireView &wire) {
        PutU32(bytes, wire.levels[0U].index_count_offset, 71U);
      },
      "native.mesh.distance_lod_levels");
  RequireHostileLodMutationFails(
      "rorng_a1_barrier_mesh",
      [](std::vector<std::uint8_t> &bytes, const MeshLodWireView &wire) {
        PutU32(bytes, wire.levels[0U].indices_offset, 0xFFFFFFFFU);
      },
      "native.mesh.distance_lod_levels.indices");
  RequireHostileLodMutationFails(
      "rorng_a1_barrier_mesh",
      [](std::vector<std::uint8_t> &bytes, const MeshLodWireView &wire) {
        const std::uint32_t first = U32(bytes, wire.levels[0U].indices_offset);
        const std::uint32_t second =
            U32(bytes, wire.levels[0U].indices_offset + 4U);
        const std::uint32_t third =
            U32(bytes, wire.levels[0U].indices_offset + 8U);
        PutU32(bytes, wire.levels[0U].indices_offset, second);
        PutU32(bytes, wire.levels[0U].indices_offset + 4U, third);
        PutU32(bytes, wire.levels[0U].indices_offset + 8U, first);
      },
      "native.mesh.distance_lod_levels.indices");
}

} // namespace

int main() {
  TestCheckedPackage();
  TestHostileDistanceLodWireDataFailsClosed();
  return EXIT_SUCCESS;
}
