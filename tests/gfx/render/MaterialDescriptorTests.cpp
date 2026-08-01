/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "MaterialDescriptor.h"
#include "RenderResourceDescriptors.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "material descriptor test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::Render::ResourceHandle Handle(RoR::Render::ResourceKind kind,
                                   std::uint32_t slot) {
  return RoR::Render::ResourceHandle::Create(kind, 1U, slot, 1U);
}

RoR::Render::MeshResourceDescriptor MakeTriangleMesh() {
  using namespace RoR::Render;
  MeshResourceDescriptor mesh;
  mesh.local_bounds.minimum = {0.0F, 0.0F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 1.0F, 0.0F};
  mesh.positions = {
      {0.0F, 0.0F, 0.0F},
      {1.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F},
  };
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

RoR::Render::TextureResourceDescriptor
MakeOnePixelTexture(RoR::Render::TextureResourceFormat format,
                    RoR::Render::TextureColorSpace color_space) {
  using namespace RoR::Render;
  TextureResourceDescriptor texture;
  texture.format = format;
  texture.color_space = color_space;
  texture.width = 1U;
  texture.height = 1U;
  TextureMipLevelDescriptor mip;
  mip.width = 1U;
  mip.height = 1U;
  mip.row_pitch_bytes = BytesPerTextureResourceTexel(format);
  mip.layer_pitch_bytes = mip.row_pitch_bytes;
  mip.bytes.resize(static_cast<std::size_t>(mip.layer_pitch_bytes));
  texture.mip_levels.push_back(mip);
  return texture;
}

void RequireCode(const RoR::Render::MaterialDescriptor &descriptor,
                 RoR::Render::ValidationCode expected, const char *message) {
  const RoR::Render::ValidationResult result =
      RoR::Render::ValidateMaterialDescriptor(descriptor);
  Require(!result && result.code == expected, message);
}

void TestValidPbrAndUnlitMaterials() {
  using namespace RoR::Render;

  MaterialDescriptor descriptor;
  descriptor.debug_name = "paint-metallic";
  descriptor.metallic_factor = 0.82F;
  descriptor.roughness_factor = 0.24F;
  descriptor.emissive_factor = {2.0F, 0.5F, 0.1F};
  descriptor.emissive_strength = 4.0F;
  descriptor.base_color_texture.texture = Handle(ResourceKind::TEXTURE, 1U);
  descriptor.base_color_texture.sampler = Handle(ResourceKind::SAMPLER, 2U);
  descriptor.normal_texture.texture = Handle(ResourceKind::TEXTURE, 3U);
  descriptor.normal_texture.sampler = Handle(ResourceKind::SAMPLER, 4U);
  descriptor.normal_texture.texture_coordinate_set = 1U;
  Require(ValidateMaterialDescriptor(descriptor).ok(),
          "valid PBR material was rejected");

  descriptor.model = MaterialModel::UNLIT;
  descriptor.alpha_mode = MaterialAlphaMode::BLEND;
  Require(ValidateMaterialDescriptor(descriptor).ok(),
          "valid unlit material was rejected");
}

void TestInvalidVersionEnumsAndNames() {
  using namespace RoR::Render;

  MaterialDescriptor descriptor;
  descriptor.version = 2U;
  RequireCode(descriptor, ValidationCode::UNSUPPORTED_VERSION,
              "unknown material version was accepted");

  descriptor = {};
  descriptor.model = static_cast<MaterialModel>(255U);
  RequireCode(descriptor, ValidationCode::INVALID_ENUM,
              "unknown material model was accepted");

  descriptor = {};
  descriptor.alpha_mode = static_cast<MaterialAlphaMode>(255U);
  RequireCode(descriptor, ValidationCode::INVALID_ENUM,
              "unknown alpha mode was accepted");

  descriptor = {};
  descriptor.debug_name = std::string(256U, 'x');
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "oversized debug name was accepted");

  descriptor.debug_name = std::string("bad\0name", 8U);
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "embedded NUL in debug name was accepted");
}

void TestInvalidPhysicalValues() {
  using namespace RoR::Render;

  MaterialDescriptor descriptor;
  descriptor.roughness_factor = 1.01F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "roughness above one was accepted");

  descriptor = {};
  descriptor.base_color_factor.x = -0.01F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "negative base color was accepted");

  descriptor = {};
  descriptor.metallic_factor = std::numeric_limits<float>::quiet_NaN();
  RequireCode(descriptor, ValidationCode::NON_FINITE_VALUE,
              "NaN metallic value was accepted");

  descriptor = {};
  descriptor.emissive_factor.z = -1.0F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "negative emissive factor was accepted");

  descriptor = {};
  descriptor.index_of_refraction = 3.1F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "unsupported index of refraction was accepted");
}

void TestTextureBindingValidation() {
  using namespace RoR::Render;

  MaterialDescriptor descriptor;
  descriptor.base_color_texture.texture = Handle(ResourceKind::MESH, 1U);
  RequireCode(descriptor, ValidationCode::WRONG_RESOURCE_KIND,
              "mesh handle was accepted as a texture");

  descriptor = {};
  descriptor.base_color_texture.sampler = Handle(ResourceKind::SAMPLER, 2U);
  RequireCode(descriptor, ValidationCode::INVALID_HANDLE,
              "sampler without texture was accepted");

  descriptor = {};
  descriptor.base_color_texture.texture = Handle(ResourceKind::TEXTURE, 1U);
  RequireCode(descriptor, ValidationCode::INVALID_HANDLE,
              "texture without an explicit sampler was accepted");

  descriptor = {};
  descriptor.base_color_texture.texture = Handle(ResourceKind::TEXTURE, 1U);
  descriptor.base_color_texture.sampler = Handle(ResourceKind::MATERIAL, 2U);
  RequireCode(descriptor, ValidationCode::WRONG_RESOURCE_KIND,
              "material handle was accepted as a sampler");

  descriptor = {};
  descriptor.normal_texture.texture = Handle(ResourceKind::TEXTURE, 1U);
  descriptor.normal_texture.sampler = Handle(ResourceKind::SAMPLER, 2U);
  descriptor.normal_texture.texture_coordinate_set = 2U;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "out-of-range UV set was accepted");

  descriptor.normal_texture.texture_coordinate_set = 0U;
  descriptor.normal_texture.scale.x = 0.0F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "zero texture scale was accepted");
}

void TestMeshMaterialCompatibilityRequiresAuthoredAttributes() {
  using namespace RoR::Render;

  MaterialDescriptor material;
  MeshResourceDescriptor mesh = MakeTriangleMesh();
  Require(ValidateMaterialMeshCompatibility(material, mesh).code ==
              ValidationCode::MISSING_REFERENCE,
          "PBR mesh without authored normals was accepted");

  mesh.normals.assign(3U, Float3{0.0F, 0.0F, 1.0F});
  Require(ValidateMaterialMeshCompatibility(material, mesh).ok(),
          "PBR mesh with authored normals was rejected");

  material.base_color_texture.texture = Handle(ResourceKind::TEXTURE, 1U);
  material.base_color_texture.sampler = Handle(ResourceKind::SAMPLER, 2U);
  Require(ValidateMaterialMeshCompatibility(material, mesh).code ==
              ValidationCode::MISSING_REFERENCE,
          "textured material without its UV stream was accepted");
  mesh.texture_coordinates_0 = {
      {0.0F, 0.0F},
      {1.0F, 0.0F},
      {0.0F, 1.0F},
  };
  Require(ValidateMaterialMeshCompatibility(material, mesh).ok(),
          "textured material with authored UV0 was rejected");

  material.normal_texture.texture = Handle(ResourceKind::TEXTURE, 3U);
  material.normal_texture.sampler = Handle(ResourceKind::SAMPLER, 4U);
  Require(ValidateMaterialMeshCompatibility(material, mesh).code ==
              ValidationCode::MISSING_REFERENCE,
          "normal map without authored tangents was accepted");
  mesh.tangents.assign(3U, Float4{1.0F, 0.0F, 0.0F, 1.0F});
  Require(ValidateMaterialMeshCompatibility(material, mesh).ok(),
          "normal-mapped mesh with authored tangent frame was rejected");

  material.emissive_texture.texture = Handle(ResourceKind::TEXTURE, 5U);
  material.emissive_texture.sampler = Handle(ResourceKind::SAMPLER, 6U);
  material.emissive_texture.texture_coordinate_set = 1U;
  Require(ValidateMaterialMeshCompatibility(material, mesh).code ==
              ValidationCode::MISSING_REFERENCE,
          "material referencing absent UV1 was accepted");
}

void TestMaterialTextureCompatibility() {
  using namespace RoR::Render;

  SamplerResourceDescriptor sampler;
  TextureResourceDescriptor texture = MakeOnePixelTexture(
      TextureResourceFormat::RGBA8_UNORM, TextureColorSpace::SRGB);
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::BASE_COLOR,
                                               texture, sampler)
              .ok(),
          "valid base-color texture and sampler were rejected");
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::EMISSIVE,
                                               texture, sampler)
              .ok(),
          "valid emissive texture and sampler were rejected");

  texture.type = TextureResourceType::TEXTURE_2D_ARRAY;
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::BASE_COLOR,
                                               texture, sampler)
                  .code == ValidationCode::WRONG_RESOURCE_KIND,
          "array texture was accepted for a material slot");

  texture = MakeOnePixelTexture(TextureResourceFormat::RGBA8_UNORM,
                                TextureColorSpace::LINEAR);
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::BASE_COLOR,
                                               texture, sampler)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "linear base-color texture was accepted");
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::NORMAL,
                                               texture, sampler)
              .ok(),
          "valid linear RGBA normal texture was rejected");

  texture = MakeOnePixelTexture(TextureResourceFormat::RG8_UNORM,
                                TextureColorSpace::LINEAR);
  Require(ValidateMaterialTextureCompatibility(
              MaterialTextureSlot::METALLIC_ROUGHNESS, texture, sampler)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "texture without a metallic B channel was accepted");
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::NORMAL,
                                               texture, sampler)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "texture without a complete normal RGB vector was accepted");

  texture = MakeOnePixelTexture(TextureResourceFormat::R8_UNORM,
                                TextureColorSpace::LINEAR);
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::OCCLUSION,
                                               texture, sampler)
              .ok(),
          "valid single-channel occlusion texture was rejected");

  texture = MakeOnePixelTexture(TextureResourceFormat::RGBA8_UNORM,
                                TextureColorSpace::SRGB);
  sampler.compare_enabled = true;
  sampler.compare_operation = SamplerCompareOperation::LESS_EQUAL;
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::BASE_COLOR,
                                               texture, sampler)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "comparison sampler was accepted for a material slot");

  sampler = {};
  Require(ValidateMaterialTextureCompatibility(
              static_cast<MaterialTextureSlot>(255U), texture, sampler)
                  .code == ValidationCode::INVALID_ENUM,
          "unknown material texture slot was accepted");
}

} // namespace

int main() {
  TestValidPbrAndUnlitMaterials();
  TestInvalidVersionEnumsAndNames();
  TestInvalidPhysicalValues();
  TestTextureBindingValidation();
  TestMeshMaterialCompatibilityRequiresAuthoredAttributes();
  TestMaterialTextureCompatibility();
  std::cout << "material descriptor tests passed\n";
  return EXIT_SUCCESS;
}
