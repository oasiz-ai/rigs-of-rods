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

RoR::Render::RenderAssetReference Asset(RoR::Render::RenderAssetKind kind,
                                        std::uint64_t value) {
  return RoR::Render::RenderAssetReference::Create(
      kind, RoR::Render::RenderAssetId::FromWords(0xA55E7U, value), 1U);
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
  descriptor.base_color_texture.texture = Asset(RenderAssetKind::TEXTURE, 1U);
  descriptor.base_color_texture.sampler = Asset(RenderAssetKind::SAMPLER, 2U);
  descriptor.normal_texture.texture = Asset(RenderAssetKind::TEXTURE, 3U);
  descriptor.normal_texture.sampler = Asset(RenderAssetKind::SAMPLER, 4U);
  descriptor.normal_texture.texture_coordinate_set = 1U;
  Require(ValidateMaterialDescriptor(descriptor).ok(),
          "valid PBR material was rejected");

  MaterialDescriptor specular;
  specular.debug_name = "authored-linear-specular";
  specular.pbr_workflow = MaterialPbrWorkflow::SPECULAR;
  specular.specular_factor = {0.25F, 0.5F, 0.75F};
  specular.specular_texture.texture = Asset(RenderAssetKind::TEXTURE, 5U);
  specular.specular_texture.sampler = Asset(RenderAssetKind::SAMPLER, 6U);
  specular.blend_mode = MaterialBlendMode::STRAIGHT_SOURCE_OVER;
  specular.alpha_test_mode = MaterialAlphaTestMode::GREATER;
  specular.alpha_cutoff = 2.0F / 255.0F;
  specular.depth_write = false;
  Require(ValidateMaterialDescriptor(specular).ok(),
          "independent true source-over, GREATER test, and specular workflow "
          "were rejected");

  descriptor.model = MaterialModel::UNLIT;
  descriptor.blend_mode = MaterialBlendMode::LEGACY_STRAIGHT_ALPHA;
  Require(ValidateMaterialDescriptor(descriptor).ok(),
          "valid unlit material was rejected");

  Require(IsKnownMaterialBlendMode(
              MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER),
          "premultiplied source-over blend mode is not a known mode");
  MaterialDescriptor hud;
  hud.debug_name = "hud-overlay";
  hud.model = MaterialModel::UNLIT;
  hud.blend_mode = MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER;
  hud.base_color_transfer =
      BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE;
  hud.depth_write = false;
  hud.base_color_texture.texture = Asset(RenderAssetKind::TEXTURE, 7U);
  hud.base_color_texture.sampler = Asset(RenderAssetKind::SAMPLER, 8U);
  Require(ValidateMaterialDescriptor(hud).ok(),
          "premultiplied source-over HUD overlay material was rejected");
}

void TestInvalidVersionEnumsAndNames() {
  using namespace RoR::Render;

  MaterialDescriptor descriptor;
  descriptor.version = 1U;
  RequireCode(descriptor, ValidationCode::UNSUPPORTED_VERSION,
              "legacy handle-based material version was accepted");

  descriptor = {};
  descriptor.version = kMaterialDescriptorTransmissionVersion + 1U;
  RequireCode(descriptor, ValidationCode::UNSUPPORTED_VERSION,
              "unknown material version was accepted");

  descriptor = {};
  descriptor.version = 3U;
  RequireCode(descriptor, ValidationCode::UNSUPPORTED_VERSION,
              "v3 material payload was silently reinterpreted as v4");

  descriptor = {};
  descriptor.model = static_cast<MaterialModel>(255U);
  RequireCode(descriptor, ValidationCode::INVALID_ENUM,
              "unknown material model was accepted");

  descriptor = {};
  descriptor.pbr_workflow = static_cast<MaterialPbrWorkflow>(255U);
  RequireCode(descriptor, ValidationCode::INVALID_ENUM,
              "unknown PBR workflow was accepted");

  descriptor = {};
  descriptor.transmission_mode =
      static_cast<MaterialTransmissionMode>(255U);
  RequireCode(descriptor, ValidationCode::INVALID_ENUM,
              "unknown transmission mode was accepted");

  descriptor = {};
  descriptor.blend_mode = static_cast<MaterialBlendMode>(255U);
  RequireCode(descriptor, ValidationCode::INVALID_ENUM,
              "unknown blend mode was accepted");

  descriptor = {};
  descriptor.alpha_test_mode = static_cast<MaterialAlphaTestMode>(255U);
  RequireCode(descriptor, ValidationCode::INVALID_ENUM,
              "unknown alpha-test mode was accepted");

  descriptor = {};
  descriptor.base_color_transfer = static_cast<BaseColorTransfer>(255U);
  RequireCode(descriptor, ValidationCode::INVALID_ENUM,
              "unknown base-color transfer ordering was accepted");

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
  descriptor.specular_factor.y = std::numeric_limits<float>::quiet_NaN();
  RequireCode(descriptor, ValidationCode::NON_FINITE_VALUE,
              "NaN specular factor was accepted");

  descriptor = {};
  descriptor.specular_factor.z = 1.01F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "specular factor above one was accepted");

  descriptor = {};
  descriptor.specular_texture.texture = Asset(RenderAssetKind::TEXTURE, 7U);
  descriptor.specular_texture.sampler = Asset(RenderAssetKind::SAMPLER, 8U);
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "metallic-roughness workflow accepted a specular texture");

  descriptor = {};
  descriptor.pbr_workflow = MaterialPbrWorkflow::SPECULAR;
  descriptor.metallic_factor = 0.1F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "specular workflow synthesized metallic state");

  descriptor = {};
  descriptor.emissive_factor.z = -1.0F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "negative emissive factor was accepted");

  descriptor = {};
  descriptor.index_of_refraction = 3.1F;
  RequireCode(descriptor, ValidationCode::VALUE_OUT_OF_RANGE,
              "unsupported index of refraction was accepted");
}

void TestVersionedThinSlabTransmission() {
  using namespace RoR::Render;

  MaterialDescriptor glass;
  glass.version = kMaterialDescriptorTransmissionVersion;
  glass.debug_name = "project-original-thin-glass";
  glass.pbr_workflow = MaterialPbrWorkflow::SPECULAR;
  glass.depth_write = false;
  glass.index_of_refraction = 1.52F;
  glass.roughness_factor = 0.03F;
  glass.transmission_mode =
      MaterialTransmissionMode::THIN_PARALLEL_SLAB;
  glass.transmission_factor = 0.96F;
  glass.attenuation_color = {0.92F, 0.98F, 1.0F};
  glass.attenuation_distance_m = 0.6F;
  glass.slab_thickness_m = 0.08F;
  Require(ValidateMaterialDescriptor(glass).ok(),
          "valid v5 thin-slab transmission was rejected");

  MaterialDescriptor hostile = glass;
  hostile.version = kMaterialDescriptorVersion;
  RequireCode(hostile, ValidationCode::UNSUPPORTED_FEATURE,
              "v4 material smuggled transmission state");

  hostile = glass;
  hostile.transmission_mode = MaterialTransmissionMode::NONE;
  RequireCode(hostile, ValidationCode::VALUE_OUT_OF_RANGE,
              "absent transmission retained noncanonical physical state");

  hostile = glass;
  hostile.depth_write = true;
  RequireCode(hostile, ValidationCode::UNSUPPORTED_FEATURE,
              "thin slab with depth writes was accepted");

  hostile = glass;
  hostile.double_sided = true;
  RequireCode(hostile, ValidationCode::UNSUPPORTED_FEATURE,
              "double-sided thin slab was accepted");

  hostile = glass;
  hostile.pbr_workflow = MaterialPbrWorkflow::METALLIC_ROUGHNESS;
  RequireCode(hostile, ValidationCode::UNSUPPORTED_FEATURE,
              "metallic thin slab was accepted");

  hostile = glass;
  hostile.attenuation_distance_m = 0.0F;
  RequireCode(hostile, ValidationCode::VALUE_OUT_OF_RANGE,
              "zero attenuation distance was accepted");

  hostile = glass;
  hostile.slab_thickness_m = 0.0F;
  RequireCode(hostile, ValidationCode::UNSUPPORTED_FEATURE,
              "zero slab thickness was accepted");
}

void TestTextureBindingValidation() {
  using namespace RoR::Render;

  MaterialDescriptor descriptor;
  descriptor.base_color_texture.texture = Asset(RenderAssetKind::MESH, 1U);
  RequireCode(descriptor, ValidationCode::WRONG_ASSET_KIND,
              "mesh asset was accepted as a texture");

  descriptor = {};
  descriptor.base_color_texture.sampler = Asset(RenderAssetKind::SAMPLER, 2U);
  RequireCode(descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
              "sampler without texture was accepted");

  descriptor = {};
  descriptor.base_color_texture.texture = Asset(RenderAssetKind::TEXTURE, 1U);
  RequireCode(descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
              "texture without an explicit sampler was accepted");

  descriptor = {};
  descriptor.base_color_texture.texture.id =
      RenderAssetId::FromWords(0xA55E7U, 1U);
  RequireCode(descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
              "partially populated invalid texture reference was treated as absent");

  descriptor = {};
  descriptor.base_color_texture.sampler.kind = RenderAssetKind::SAMPLER;
  RequireCode(descriptor, ValidationCode::INVALID_ASSET_REFERENCE,
              "partially populated invalid sampler reference was treated as absent");

  descriptor = {};
  descriptor.base_color_texture.texture = Asset(RenderAssetKind::TEXTURE, 1U);
  descriptor.base_color_texture.sampler = Asset(RenderAssetKind::MATERIAL, 2U);
  RequireCode(descriptor, ValidationCode::WRONG_ASSET_KIND,
              "material asset was accepted as a sampler");

  descriptor = {};
  descriptor.normal_texture.texture = Asset(RenderAssetKind::TEXTURE, 1U);
  descriptor.normal_texture.sampler = Asset(RenderAssetKind::SAMPLER, 2U);
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

  MeshResourceDescriptor malformed_mesh = mesh;
  malformed_mesh.version = kMeshResourceDescriptorVersion + 1U;
  Require(ValidateMaterialMeshCompatibility(material, malformed_mesh).code ==
              ValidationCode::UNSUPPORTED_VERSION,
          "standalone material/mesh validation trusted a malformed mesh");
  MaterialDescriptor malformed_material = material;
  malformed_material.roughness_factor = std::numeric_limits<float>::quiet_NaN();
  Require(ValidateMaterialMeshCompatibility(malformed_material, mesh).code ==
              ValidationCode::NON_FINITE_VALUE,
          "standalone material/mesh validation trusted a malformed material");

  material.base_color_texture.texture = Asset(RenderAssetKind::TEXTURE, 1U);
  material.base_color_texture.sampler = Asset(RenderAssetKind::SAMPLER, 2U);
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

  material.normal_texture.texture = Asset(RenderAssetKind::TEXTURE, 3U);
  material.normal_texture.sampler = Asset(RenderAssetKind::SAMPLER, 4U);
  Require(ValidateMaterialMeshCompatibility(material, mesh).code ==
              ValidationCode::MISSING_REFERENCE,
          "normal map without authored tangents was accepted");
  mesh.tangents.assign(3U, Float4{1.0F, 0.0F, 0.0F, 1.0F});
  Require(ValidateMaterialMeshCompatibility(material, mesh).ok(),
          "normal-mapped mesh with authored tangent frame was rejected");

  material.emissive_texture.texture = Asset(RenderAssetKind::TEXTURE, 5U);
  material.emissive_texture.sampler = Asset(RenderAssetKind::SAMPLER, 6U);
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
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::SPECULAR,
                                               texture, sampler)
              .ok(),
          "valid linear RGBA specular texture was rejected");

  texture.color_space = TextureColorSpace::SRGB;
  Require(ValidateMaterialTextureCompatibility(MaterialTextureSlot::SPECULAR,
                                               texture, sampler)
                  .code == ValidationCode::VALUE_OUT_OF_RANGE,
          "sRGB specular texture was accepted as linear authored data");

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
  TestVersionedThinSlabTransmission();
  TestTextureBindingValidation();
  TestMeshMaterialCompatibilityRequiresAuthoredAttributes();
  TestMaterialTextureCompatibility();
  std::cout << "material descriptor tests passed\n";
  return EXIT_SUCCESS;
}
