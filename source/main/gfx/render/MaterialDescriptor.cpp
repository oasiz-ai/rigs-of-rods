/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "MaterialDescriptor.h"

#include "RenderResourceDescriptors.h"
#include "ValidatedAssetCompatibilityInternal.h"

#include <array>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult ValidateUnitValue(float value, const char *field) {
  if (!IsFinite(value)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE, field,
                                     "value must be finite");
  }
  if (value < 0.0F || value > 1.0F) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE, field,
                                     "value must be in [0, 1]");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateTextureBinding(const TextureBinding &binding,
                                        const char *field) {
  const bool texture_absent =
      IsAbsentRenderAssetReference(binding.texture);
  const bool sampler_absent =
      IsAbsentRenderAssetReference(binding.sampler);
  if ((!texture_absent && !binding.texture.valid()) ||
      (!sampler_absent && !binding.sampler.valid())) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ASSET_REFERENCE, field,
        "optional texture references must be canonical absent or fully valid");
  }
  if (!texture_absent && binding.texture.kind != RenderAssetKind::TEXTURE) {
    return ValidationResult::Failure(ValidationCode::WRONG_ASSET_KIND, field,
                                     "texture reference must identify a texture");
  }

  if (!sampler_absent &&
      binding.sampler.kind != RenderAssetKind::SAMPLER) {
    return ValidationResult::Failure(ValidationCode::WRONG_ASSET_KIND, field,
                                     "sampler reference must identify a sampler");
  }
  if (texture_absent != sampler_absent) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ASSET_REFERENCE, field,
        "texture and explicit sampler must be supplied together");
  }
  if (binding.texture_coordinate_set > 1U) {
    return ValidationResult::Failure(ValidationCode::VALUE_OUT_OF_RANGE, field,
                                     "texture coordinate set must be 0 or 1");
  }
  if (!IsFinite(binding.scale) || !IsFinite(binding.offset) ||
      !IsFinite(binding.rotation_radians)) {
    return ValidationResult::Failure(ValidationCode::NON_FINITE_VALUE, field,
                                     "texture transform values must be finite");
  }
  if (binding.scale.x == 0.0F || binding.scale.y == 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, field,
        "texture scale components must be nonzero");
  }
  return ValidationResult::Success();
}

} // namespace

bool IsKnownMaterialModel(MaterialModel model) noexcept {
  switch (model) {
  case MaterialModel::PBR_METALLIC_ROUGHNESS:
  case MaterialModel::UNLIT:
    return true;
  }
  return false;
}

bool IsKnownMaterialPbrWorkflow(MaterialPbrWorkflow workflow) noexcept {
  switch (workflow) {
  case MaterialPbrWorkflow::METALLIC_ROUGHNESS:
  case MaterialPbrWorkflow::SPECULAR:
    return true;
  }
  return false;
}

bool IsKnownMaterialBlendMode(MaterialBlendMode mode) noexcept {
  switch (mode) {
  case MaterialBlendMode::REPLACE:
  case MaterialBlendMode::STRAIGHT_SOURCE_OVER:
  case MaterialBlendMode::LEGACY_STRAIGHT_ALPHA:
    return true;
  }
  return false;
}

bool IsKnownMaterialAlphaTestMode(MaterialAlphaTestMode mode) noexcept {
  switch (mode) {
  case MaterialAlphaTestMode::DISABLED:
  case MaterialAlphaTestMode::GREATER:
  case MaterialAlphaTestMode::GREATER_EQUAL:
    return true;
  }
  return false;
}

bool IsKnownBaseColorTransfer(BaseColorTransfer transfer) noexcept {
  switch (transfer) {
  case BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER:
  case BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE:
    return true;
  }
  return false;
}

ValidationResult
ValidateMaterialDescriptor(const MaterialDescriptor &descriptor) {
  if (descriptor.version != kMaterialDescriptorVersion) {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_VERSION,
                                     "version",
                                     "unsupported material descriptor version");
  }
  if (descriptor.debug_name.size() > kMaximumMaterialDebugNameBytes ||
      descriptor.debug_name.find('\0') != std::string::npos) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "debug_name",
        "debug name must be at most 255 bytes and contain no NUL");
  }
  if (!IsKnownMaterialModel(descriptor.model)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM, "model",
                                     "unknown material model");
  }
  if (!IsKnownMaterialPbrWorkflow(descriptor.pbr_workflow)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "pbr_workflow",
                                     "unknown PBR workflow");
  }
  if (!IsKnownMaterialBlendMode(descriptor.blend_mode)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "blend_mode", "unknown blend mode");
  }
  if (!IsKnownMaterialAlphaTestMode(descriptor.alpha_test_mode)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "alpha_test_mode",
                                     "unknown alpha-test mode");
  }
  if (!IsKnownBaseColorTransfer(descriptor.base_color_transfer)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "base_color_transfer",
                                     "unknown base-color transfer ordering");
  }
  if (!IsNormalizedColor(descriptor.base_color_factor)) {
    return ValidationResult::Failure(IsFinite(descriptor.base_color_factor)
                                         ? ValidationCode::VALUE_OUT_OF_RANGE
                                         : ValidationCode::NON_FINITE_VALUE,
                                     "base_color_factor",
                                     "base color must be finite and in [0, 1]");
  }
  const bool normalized_specular =
      IsFinite(descriptor.specular_factor) &&
      descriptor.specular_factor.x >= 0.0F &&
      descriptor.specular_factor.x <= 1.0F &&
      descriptor.specular_factor.y >= 0.0F &&
      descriptor.specular_factor.y <= 1.0F &&
      descriptor.specular_factor.z >= 0.0F &&
      descriptor.specular_factor.z <= 1.0F;
  if (!normalized_specular) {
    return ValidationResult::Failure(IsFinite(descriptor.specular_factor)
                                         ? ValidationCode::VALUE_OUT_OF_RANGE
                                         : ValidationCode::NON_FINITE_VALUE,
                                     "specular_factor",
                                     "specular factor must be finite and in [0, 1]");
  }

  const std::array<std::pair<float, const char *>, 4U> unit_values{{
      {descriptor.metallic_factor, "metallic_factor"},
      {descriptor.roughness_factor, "roughness_factor"},
      {descriptor.occlusion_strength, "occlusion_strength"},
      {descriptor.alpha_cutoff, "alpha_cutoff"},
  }};
  for (const auto &value : unit_values) {
    const ValidationResult validation =
        ValidateUnitValue(value.first, value.second);
    if (!validation) {
      return validation;
    }
  }

  if (!IsFinite(descriptor.normal_scale) || descriptor.normal_scale < 0.0F) {
    return ValidationResult::Failure(
        IsFinite(descriptor.normal_scale) ? ValidationCode::VALUE_OUT_OF_RANGE
                                          : ValidationCode::NON_FINITE_VALUE,
        "normal_scale", "normal scale must be finite and nonnegative");
  }
  if (!IsNonNegative(descriptor.emissive_factor)) {
    return ValidationResult::Failure(
        IsFinite(descriptor.emissive_factor)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "emissive_factor", "emissive factor must be finite and nonnegative");
  }
  if (!IsFinite(descriptor.emissive_strength) ||
      descriptor.emissive_strength < 0.0F) {
    return ValidationResult::Failure(
        IsFinite(descriptor.emissive_strength)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "emissive_strength",
        "emissive strength must be finite and nonnegative");
  }
  if (!IsFinite(descriptor.index_of_refraction) ||
      descriptor.index_of_refraction < 1.0F ||
      descriptor.index_of_refraction > 3.0F) {
    return ValidationResult::Failure(IsFinite(descriptor.index_of_refraction)
                                         ? ValidationCode::VALUE_OUT_OF_RANGE
                                         : ValidationCode::NON_FINITE_VALUE,
                                     "index_of_refraction",
                                     "index of refraction must be in [1, 3]");
  }

  const std::array<std::pair<const TextureBinding *, const char *>, 6U>
      texture_bindings{{
          {&descriptor.base_color_texture, "base_color_texture"},
          {&descriptor.metallic_roughness_texture,
           "metallic_roughness_texture"},
          {&descriptor.normal_texture, "normal_texture"},
          {&descriptor.occlusion_texture, "occlusion_texture"},
          {&descriptor.emissive_texture, "emissive_texture"},
          {&descriptor.specular_texture, "specular_texture"},
      }};
  for (const auto &binding : texture_bindings) {
    const ValidationResult validation =
        ValidateTextureBinding(*binding.first, binding.second);
    if (!validation) {
      return validation;
    }
  }

  const bool metallic_roughness_absent =
      IsAbsentRenderAssetReference(
          descriptor.metallic_roughness_texture.texture) &&
      IsAbsentRenderAssetReference(
          descriptor.metallic_roughness_texture.sampler);
  const bool specular_absent =
      IsAbsentRenderAssetReference(descriptor.specular_texture.texture) &&
      IsAbsentRenderAssetReference(descriptor.specular_texture.sampler);
  if (descriptor.model != MaterialModel::PBR_METALLIC_ROUGHNESS &&
      descriptor.pbr_workflow != MaterialPbrWorkflow::METALLIC_ROUGHNESS) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "pbr_workflow",
        "non-PBR materials require the canonical metallic-roughness workflow token");
  }
  if (descriptor.pbr_workflow == MaterialPbrWorkflow::METALLIC_ROUGHNESS) {
    if (!specular_absent ||
        descriptor.specular_factor != Float3{1.0F, 1.0F, 1.0F}) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "specular_texture",
          "metallic-roughness workflow requires canonical unused specular fields");
    }
  } else if (!metallic_roughness_absent || descriptor.metallic_factor != 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "metallic_roughness_texture",
        "specular workflow forbids metallic input or synthesis");
  }

  return ValidationResult::Success();
}

ValidationResult
ValidateMaterialMeshCompatibility(const MaterialDescriptor &material,
                                  const MeshResourceDescriptor &mesh) {
  ValidationResult validation = ValidateMaterialDescriptor(material);
  if (!validation) {
    return validation;
  }
  validation = ValidateMeshResourceDescriptor(mesh);
  if (!validation) {
    return validation;
  }

  return Detail::ValidateMaterialMeshCompatibilityFromValidatedAssets(
      ValidatedAssetCompatibilityAccess{}, material, mesh);
}

ValidationResult Detail::ValidateMaterialMeshCompatibilityFromValidatedAssets(
    const ValidatedAssetCompatibilityAccess &, const MaterialDescriptor &material,
    const MeshResourceDescriptor &mesh) {

  if ((material.model == MaterialModel::PBR_METALLIC_ROUGHNESS ||
       material.normal_texture.texture.valid()) &&
      mesh.normals.empty()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "mesh.normals",
        "PBR materials require authored per-vertex normals");
  }

  const std::array<const TextureBinding *, 6U> bindings{{
      &material.base_color_texture,
      &material.metallic_roughness_texture,
      &material.normal_texture,
      &material.occlusion_texture,
      &material.emissive_texture,
      &material.specular_texture,
  }};
  for (const TextureBinding *binding : bindings) {
    if (!binding->texture.valid()) {
      continue;
    }
    const bool has_coordinates = binding->texture_coordinate_set == 0U
                                     ? !mesh.texture_coordinates_0.empty()
                                     : !mesh.texture_coordinates_1.empty();
    if (!has_coordinates) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          binding->texture_coordinate_set == 0U ? "mesh.texture_coordinates_0"
                                                : "mesh.texture_coordinates_1",
          "material texture references a missing authored UV stream");
    }
  }
  if (material.normal_texture.texture.valid() && mesh.tangents.empty()) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "mesh.tangents",
        "normal mapping requires authored per-vertex tangents");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateMaterialTextureCompatibility(MaterialTextureSlot slot,
                                     const TextureResourceDescriptor &texture,
                                     const SamplerResourceDescriptor &sampler) {
  ValidationResult validation = ValidateTextureResourceDescriptor(texture);
  if (!validation) {
    return validation;
  }
  validation = ValidateSamplerResourceDescriptor(sampler);
  if (!validation) {
    return validation;
  }
  if (texture.type != TextureResourceType::TEXTURE_2D ||
      texture.array_layers != 1U) {
    return ValidationResult::Failure(
        ValidationCode::WRONG_RESOURCE_KIND, "texture.type",
        "material slots require a non-array 2D texture");
  }
  if (sampler.compare_enabled) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "sampler.compare_enabled",
        "ordinary PBR texture sampling cannot use a comparison sampler");
  }

  const bool rgba_storage =
      texture.format == TextureResourceFormat::RGBA8_UNORM ||
      texture.format == TextureResourceFormat::RGBA16_FLOAT ||
      texture.format == TextureResourceFormat::RGBA32_FLOAT;
  switch (slot) {
  case MaterialTextureSlot::BASE_COLOR:
  case MaterialTextureSlot::EMISSIVE:
    if (texture.format != TextureResourceFormat::RGBA8_UNORM ||
        texture.color_space != TextureColorSpace::SRGB) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "base-color and emissive slots require RGBA8 sRGB storage");
    }
    break;
  case MaterialTextureSlot::METALLIC_ROUGHNESS:
  case MaterialTextureSlot::NORMAL:
  case MaterialTextureSlot::SPECULAR:
    if (!rgba_storage || texture.color_space != TextureColorSpace::LINEAR) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "metallic-roughness, normal, and specular slots require linear RGBA storage");
    }
    break;
  case MaterialTextureSlot::OCCLUSION:
    if (texture.color_space != TextureColorSpace::LINEAR) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.color_space",
          "occlusion slot requires linear storage with an R channel");
    }
    break;
  default:
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM, "slot",
                                     "unknown material texture slot");
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
