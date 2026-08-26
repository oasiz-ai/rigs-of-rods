/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "NativeRenderAssetPackage.h"

#include "MaterialDescriptor.h"
#include "RenderResourceDescriptors.h"

#include <cstdint>
#include <cstdlib>
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
    "fe37f2bb05f15bc4954c07ff83a71c2dea24b51af473056f8257a47b4cc8cc7e";

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
  Require(package.version == 2U &&
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
    Require(mesh.version == kMeshResourceDescriptorVersion &&
                mesh.distance_lod_levels.empty(),
            "wire-v1 mesh was not upgraded to the current empty-LOD descriptor");
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
}

} // namespace

int main() {
  TestCheckedPackage();
  return EXIT_SUCCESS;
}
