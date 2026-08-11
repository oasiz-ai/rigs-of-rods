/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderResourceDescriptors.h"
#include "SceneSnapshot.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "render resource descriptor test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void RequireCode(const RoR::Render::ValidationResult &validation,
                 RoR::Render::ValidationCode code, const char *message) {
  Require(!validation && validation.code == code, message);
}

RoR::Render::MeshResourceDescriptor MakeMesh() {
  using namespace RoR::Render;
  MeshResourceDescriptor descriptor;
  descriptor.debug_name = "portable triangle";
  descriptor.local_bounds.minimum = {-1.0F, 0.0F, 0.0F};
  descriptor.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  descriptor.positions = {
      {-1.0F, 0.0F, 0.0F},
      {1.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F},
  };
  descriptor.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  descriptor.tangents.assign(3U, Float4{1.0F, 0.0F, 0.0F, 1.0F});
  descriptor.texture_coordinates_0 = {
      {0.0F, 0.0F},
      {1.0F, 0.0F},
      {0.5F, 1.0F},
  };
  descriptor.colors.assign(3U, Float4{1.0F, 1.0F, 1.0F, 1.0F});
  descriptor.indices = {0U, 1U, 2U};
  return descriptor;
}

RoR::Render::TextureResourceDescriptor MakeTexture() {
  using namespace RoR::Render;
  TextureResourceDescriptor descriptor;
  descriptor.debug_name = "two mip color";
  descriptor.format = TextureResourceFormat::RGBA8_UNORM;
  descriptor.color_space = TextureColorSpace::SRGB;
  descriptor.width = 2U;
  descriptor.height = 2U;

  TextureMipLevelDescriptor base;
  base.width = 2U;
  base.height = 2U;
  base.row_pitch_bytes = 8U;
  base.layer_pitch_bytes = 16U;
  base.bytes.resize(16U);
  descriptor.mip_levels.push_back(base);

  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 4U;
  mip.layer_pitch_bytes = 4U;
  mip.bytes.resize(4U);
  descriptor.mip_levels.push_back(mip);
  return descriptor;
}

RoR::Render::TextureResourceDescriptor MakeEnvironmentTexture() {
  using namespace RoR::Render;
  TextureResourceDescriptor descriptor;
  descriptor.debug_name = "linear HDR equirectangular environment";
  descriptor.format = TextureResourceFormat::RGBA16_FLOAT;
  descriptor.color_space = TextureColorSpace::LINEAR;
  descriptor.width = 1U;
  descriptor.height = 1U;
  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = 8U;
  mip.layer_pitch_bytes = 8U;
  mip.bytes.resize(8U);
  descriptor.mip_levels.push_back(mip);
  return descriptor;
}

void TestMeshValidation() {
  using namespace RoR::Render;
  MeshResourceDescriptor descriptor = MakeMesh();
  Require(ValidateMeshResourceDescriptor(descriptor).ok(),
          "valid mesh was rejected");

  descriptor.normals.front() = {};
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE, "zero normal was accepted");

  descriptor = MakeMesh();
  descriptor.tangents.front().w = 0.0F;
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "invalid tangent handedness was accepted");

  descriptor = MakeMesh();
  descriptor.tangents.front() = {0.0F, 0.0F, 1.0F, 1.0F};
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "non-orthogonal tangent basis was accepted");

  descriptor = MakeMesh();
  descriptor.normals.clear();
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::MISSING_REFERENCE,
              "tangent stream without normals was accepted");

  descriptor = MakeMesh();
  descriptor.texture_coordinates_0.pop_back();
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::SIZE_MISMATCH,
              "mismatched vertex stream was accepted");

  descriptor = MakeMesh();
  descriptor.local_bounds.maximum.x = 0.5F;
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::INVALID_BOUNDS,
              "bounds excluding a position were accepted");

  descriptor = MakeMesh();
  descriptor.positions.front().x = std::numeric_limits<float>::infinity();
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::NON_FINITE_VALUE,
              "infinite mesh position was accepted");

  descriptor = MakeMesh();
  descriptor.indices.back() = 9U;
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "out-of-range mesh index was accepted");

  descriptor = MakeMesh();
  descriptor.indices = {0U, 0U, 2U};
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "degenerate repeated triangle index was accepted");

  descriptor = MakeMesh();
  descriptor.index_format = MeshIndexFormat::UINT16;
  descriptor.positions.resize(65537U, Float3{});
  RequireCode(ValidateMeshResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "16-bit mesh with too many vertices was accepted");
}

void TestTextureValidation() {
  using namespace RoR::Render;
  TextureResourceDescriptor descriptor = MakeTexture();
  Require(ValidateTextureResourceDescriptor(descriptor).ok(),
          "valid texture was rejected");

  descriptor.mip_levels[1].width = 2U;
  RequireCode(ValidateTextureResourceDescriptor(descriptor),
              ValidationCode::INVALID_DIMENSIONS,
              "invalid mip sequence was accepted");

  descriptor = MakeTexture();
  descriptor.mip_levels.front().row_pitch_bytes = 7U;
  RequireCode(ValidateTextureResourceDescriptor(descriptor),
              ValidationCode::SIZE_MISMATCH,
              "undersized or partial-texel row pitch was accepted");

  descriptor = MakeTexture();
  descriptor.mip_levels.front().bytes.pop_back();
  RequireCode(ValidateTextureResourceDescriptor(descriptor),
              ValidationCode::SIZE_MISMATCH,
              "truncated texture payload was accepted");

  descriptor = MakeTexture();
  descriptor.format = TextureResourceFormat::R32_FLOAT;
  RequireCode(ValidateTextureResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "sRGB transfer was accepted for a float texture");

  descriptor = MakeTexture();
  descriptor.type = TextureResourceType::TEXTURE_CUBE;
  RequireCode(ValidateTextureResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "cube texture without six layers was accepted");

  descriptor = MakeTexture();
  descriptor.type = TextureResourceType::TEXTURE_CUBE;
  descriptor.array_layers = 6U;
  descriptor.width = 2U;
  descriptor.height = 1U;
  RequireCode(ValidateTextureResourceDescriptor(descriptor),
              ValidationCode::INVALID_DIMENSIONS,
              "nonsquare cube texture was accepted");

  descriptor = MakeTexture();
  descriptor.format = static_cast<TextureResourceFormat>(255U);
  RequireCode(ValidateTextureResourceDescriptor(descriptor),
              ValidationCode::INVALID_ENUM,
              "unknown texture format was accepted");

  descriptor = MakeTexture();
  descriptor.width = 1U;
  descriptor.height = 2U;
  descriptor.mip_levels.resize(1U);
  descriptor.mip_levels.front().width = 1U;
  descriptor.mip_levels.front().height = 2U;
  descriptor.mip_levels.front().row_pitch_bytes =
      (std::numeric_limits<std::uint64_t>::max)() - 3U;
  descriptor.mip_levels.front().layer_pitch_bytes =
      (std::numeric_limits<std::uint64_t>::max)();
  descriptor.mip_levels.front().bytes.resize(1U);
  RequireCode(ValidateTextureResourceDescriptor(descriptor),
              ValidationCode::SIZE_MISMATCH,
              "overflowing texture row span was accepted");
}

void TestDynamicMeshUpdateCompatibility() {
  using namespace RoR::Render;

  MeshResourceDescriptor mesh = MakeMesh();
  mesh.dynamic = true;
  mesh.velocities.assign(3U, Float3{});
  DynamicMeshUpdateDescriptor update;
  update.topology_revision = mesh.topology_revision;
  update.positions = {
      {-0.9F, 0.0F, 0.0F},
      {0.9F, 0.0F, 0.0F},
      {0.0F, 0.9F, 0.0F},
  };
  update.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  update.tangents.assign(3U, Float4{1.0F, 0.0F, 0.0F, 1.0F});
  update.velocities.assign(3U, Float3{});
  update.has_updated_bounds = true;
  update.updated_local_bounds = mesh.local_bounds;
  Require(ValidateDynamicMeshUpdateCompatibility(mesh, update).ok(),
          "valid in-range dynamic mesh update was rejected");

  MeshResourceDescriptor malformed_mesh = mesh;
  malformed_mesh.positions.front().x = std::numeric_limits<float>::infinity();
  RequireCode(ValidateDynamicMeshUpdateCompatibility(malformed_mesh, update),
              ValidationCode::NON_FINITE_VALUE,
              "standalone update validation trusted a malformed mesh");

  MeshInstanceDescriptor instance;
  instance.instance_id = 7U;
  instance.mesh = RenderAssetReference::Create(
      RenderAssetKind::MESH, RenderAssetId::FromWords(7U, 1U), 1U);
  instance.material = RenderAssetReference::Create(
      RenderAssetKind::MATERIAL, RenderAssetId::FromWords(7U, 2U), 1U);
  instance.topology_revision = mesh.topology_revision;
  instance.deformation_revision = 1U;
  instance.local_bounds = mesh.local_bounds;
  Require(ValidateMeshInstanceCompatibility(mesh, instance, nullptr).ok(),
          "base instance bounds matching its live mesh were rejected");
  RequireCode(
      ValidateMeshInstanceCompatibility(malformed_mesh, instance, nullptr),
      ValidationCode::NON_FINITE_VALUE,
      "standalone instance validation trusted a malformed mesh");
  instance.local_bounds.maximum.x = 2.0F;
  RequireCode(ValidateMeshInstanceCompatibility(mesh, instance, nullptr),
              ValidationCode::INVALID_BOUNDS,
              "base instance bounds diverged from its live mesh");
  instance.local_bounds = mesh.local_bounds;
  instance.deformation_revision = 2U;
  update.instance_id = instance.instance_id;
  update.mesh = instance.mesh;
  update.deformation_revision = instance.deformation_revision;
  Require(ValidateMeshInstanceCompatibility(mesh, instance, &update).ok(),
          "self-contained deformed instance was rejected");

  mesh.dynamic = false;
  RequireCode(ValidateDynamicMeshUpdateCompatibility(mesh, update),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "static mesh accepted a dynamic update");
  mesh.dynamic = true;

  update.topology_revision += 1U;
  RequireCode(ValidateDynamicMeshUpdateCompatibility(mesh, update),
              ValidationCode::MISSING_REFERENCE,
              "stale topology revision was accepted");
  update.topology_revision = mesh.topology_revision;

  update.positions.push_back({0.0F, 0.0F, 0.0F});
  RequireCode(ValidateDynamicMeshUpdateCompatibility(mesh, update),
              ValidationCode::SIZE_MISMATCH,
              "out-of-range dynamic vertex upload was accepted");
  update.positions.pop_back();

  mesh.tangents.clear();
  RequireCode(ValidateDynamicMeshUpdateCompatibility(mesh, update),
              ValidationCode::MISSING_REFERENCE,
              "dynamic update introduced an unallocated tangent stream");
  mesh.tangents.assign(3U, Float4{1.0F, 0.0F, 0.0F, 1.0F});

  update.positions.front().x = -2.0F;
  RequireCode(ValidateDynamicMeshUpdateCompatibility(mesh, update),
              ValidationCode::INVALID_BOUNDS,
              "dynamic position outside retained bounds was accepted");
  update.updated_local_bounds.minimum = {-2.0F, 0.0F, 0.0F};
  update.updated_local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  Require(ValidateDynamicMeshUpdateCompatibility(mesh, update).ok(),
          "dynamic update with expanded authored bounds was rejected");
}

void TestSamplerValidation() {
  using namespace RoR::Render;
  SamplerResourceDescriptor descriptor;
  descriptor.debug_name = "default linear sampler";
  Require(ValidateSamplerResourceDescriptor(descriptor).ok(),
          "valid sampler was rejected");

  descriptor.minimum_lod = 2.0F;
  descriptor.maximum_lod = 1.0F;
  RequireCode(ValidateSamplerResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "inverted sampler LOD range was accepted");

  descriptor = {};
  descriptor.mip_lod_bias = std::numeric_limits<float>::quiet_NaN();
  RequireCode(ValidateSamplerResourceDescriptor(descriptor),
              ValidationCode::NON_FINITE_VALUE,
              "NaN sampler state was accepted");

  descriptor = {};
  descriptor.address_u = static_cast<SamplerAddressMode>(255U);
  RequireCode(ValidateSamplerResourceDescriptor(descriptor),
              ValidationCode::INVALID_ENUM,
              "unknown sampler address mode was accepted");

  descriptor = {};
  descriptor.maximum_anisotropy = 2.0F;
  RequireCode(ValidateSamplerResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "disabled noncanonical anisotropy was accepted");

  descriptor.anisotropy_enabled = true;
  descriptor.maximum_anisotropy = 8.0F;
  Require(ValidateSamplerResourceDescriptor(descriptor).ok(),
          "valid anisotropic sampler was rejected");

  descriptor.maximum_anisotropy = 17.0F;
  RequireCode(ValidateSamplerResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "anisotropy above the portable maximum was accepted");

  descriptor = {};
  descriptor.compare_operation = SamplerCompareOperation::LESS_EQUAL;
  RequireCode(ValidateSamplerResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "noncanonical disabled compare operation was accepted");

  descriptor = {};
  descriptor.border_color.x = 1.1F;
  RequireCode(ValidateSamplerResourceDescriptor(descriptor),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "out-of-range border color was accepted");
}

void TestEnvironmentTextureCompatibility() {
  using namespace RoR::Render;

  TextureResourceDescriptor texture = MakeEnvironmentTexture();
  SamplerResourceDescriptor sampler;
  sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  Require(ValidateEnvironmentTextureCompatibility(texture, sampler).ok(),
          "valid HDR environment texture and sampler were rejected");

  TextureResourceDescriptor malformed_texture = texture;
  malformed_texture.version = kTextureResourceDescriptorVersion + 1U;
  RequireCode(
      ValidateEnvironmentTextureCompatibility(malformed_texture, sampler),
      ValidationCode::UNSUPPORTED_VERSION,
      "standalone environment validation trusted a malformed texture");
  SamplerResourceDescriptor malformed_sampler = sampler;
  malformed_sampler.version = kSamplerResourceDescriptorVersion + 1U;
  RequireCode(
      ValidateEnvironmentTextureCompatibility(texture, malformed_sampler),
      ValidationCode::UNSUPPORTED_VERSION,
      "standalone environment validation trusted a malformed sampler");

  texture.type = TextureResourceType::TEXTURE_2D_ARRAY;
  RequireCode(ValidateEnvironmentTextureCompatibility(texture, sampler),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "array texture was accepted as an equirectangular environment");

  texture = MakeEnvironmentTexture();
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.mip_levels.front().row_pitch_bytes = 4U;
  texture.mip_levels.front().layer_pitch_bytes = 4U;
  texture.mip_levels.front().bytes.resize(4U);
  RequireCode(ValidateEnvironmentTextureCompatibility(texture, sampler),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "integer environment texture was accepted");

  texture = MakeEnvironmentTexture();
  texture.format = TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.mip_levels.front().row_pitch_bytes = 4U;
  texture.mip_levels.front().layer_pitch_bytes = 4U;
  texture.mip_levels.front().bytes.resize(4U);
  RequireCode(ValidateEnvironmentTextureCompatibility(texture, sampler),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "sRGB environment texture was accepted");

  texture = MakeEnvironmentTexture();
  sampler.compare_enabled = true;
  sampler.compare_operation = SamplerCompareOperation::LESS_EQUAL;
  RequireCode(ValidateEnvironmentTextureCompatibility(texture, sampler),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "comparison environment sampler was accepted");

  sampler = {};
  sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.minification_filter = SamplerFilter::NEAREST;
  RequireCode(ValidateEnvironmentTextureCompatibility(texture, sampler),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "nearest-filtered environment sampler was accepted");

  sampler = {};
  RequireCode(ValidateEnvironmentTextureCompatibility(texture, sampler),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "non-clamping environment V address mode was accepted");

  sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
  RequireCode(ValidateEnvironmentTextureCompatibility(texture, sampler),
              ValidationCode::VALUE_OUT_OF_RANGE,
              "non-repeating environment U address mode was accepted");
}

} // namespace

int main() {
  TestMeshValidation();
  TestTextureValidation();
  TestDynamicMeshUpdateCompatibility();
  TestSamplerValidation();
  TestEnvironmentTextureCompatibility();
  std::cout << "render resource descriptor tests passed\n";
  return EXIT_SUCCESS;
}
