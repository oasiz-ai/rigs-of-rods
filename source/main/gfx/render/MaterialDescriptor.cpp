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
#include <cmath>
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

bool IsKnownMaterialTransmissionMode(MaterialTransmissionMode mode) noexcept {
  switch (mode) {
  case MaterialTransmissionMode::NONE:
  case MaterialTransmissionMode::THIN_PARALLEL_SLAB:
    return true;
  }
  return false;
}

bool IsKnownMaterialBlendMode(MaterialBlendMode mode) noexcept {
  switch (mode) {
  case MaterialBlendMode::REPLACE:
  case MaterialBlendMode::STRAIGHT_SOURCE_OVER:
  case MaterialBlendMode::LEGACY_STRAIGHT_ALPHA:
  case MaterialBlendMode::PREMULTIPLIED_SOURCE_OVER:
    return true;
  }
  return false;
}

bool IsKnownMaterialAlphaTestMode(MaterialAlphaTestMode mode) noexcept {
  switch (mode) {
  case MaterialAlphaTestMode::DISABLED:
  case MaterialAlphaTestMode::GREATER:
  case MaterialAlphaTestMode::GREATER_EQUAL:
  case MaterialAlphaTestMode::LESS_EQUAL:
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

namespace {

// One row per enumerator, in enumerator order. The tokens are the authoring
// vocabulary and the audit vocabulary at once, so a log line and a material
// script always spell an operator the same way.
struct DetailBlendModeToken final {
  MaterialDetailBlendMode mode;
  const char *token;
};

constexpr DetailBlendModeToken kDetailBlendModeTokens[] = {
    {MaterialDetailBlendMode::NORMAL_NON_PREMUL, "normal"},
    {MaterialDetailBlendMode::NORMAL_PREMUL, "premul"},
    {MaterialDetailBlendMode::ADD, "add"},
    {MaterialDetailBlendMode::SUBTRACT, "subtract"},
    {MaterialDetailBlendMode::MULTIPLY, "multiply"},
    {MaterialDetailBlendMode::MULTIPLY2X, "multiply2x"},
    {MaterialDetailBlendMode::SCREEN, "screen"},
    {MaterialDetailBlendMode::OVERLAY, "overlay"},
    {MaterialDetailBlendMode::LIGHTEN, "lighten"},
    {MaterialDetailBlendMode::DARKEN, "darken"},
    {MaterialDetailBlendMode::GRAIN_EXTRACT, "grain_extract"},
    {MaterialDetailBlendMode::GRAIN_MERGE, "grain_merge"},
    {MaterialDetailBlendMode::DIFFERENCE_BLEND, "difference"},
};

} // namespace

bool IsKnownMaterialDetailBlendMode(MaterialDetailBlendMode mode) noexcept {
  for (const DetailBlendModeToken &entry : kDetailBlendModeTokens) {
    if (entry.mode == mode) {
      return true;
    }
  }
  return false;
}

const char *
MaterialDetailBlendModeToken(MaterialDetailBlendMode mode) noexcept {
  for (const DetailBlendModeToken &entry : kDetailBlendModeTokens) {
    if (entry.mode == mode) {
      return entry.token;
    }
  }
  return nullptr;
}

bool ParseMaterialDetailBlendModeToken(std::string_view token,
                                       MaterialDetailBlendMode &mode) noexcept {
  for (const DetailBlendModeToken &entry : kDetailBlendModeTokens) {
    if (token == entry.token) {
      mode = entry.mode;
      return true;
    }
  }
  return false;
}

ValidationResult
ValidateMaterialDescriptor(const MaterialDescriptor &descriptor) {
  if (descriptor.version != kMaterialDescriptorVersion &&
      descriptor.version != kMaterialDescriptorTransmissionVersion &&
      descriptor.version != kMaterialDescriptorDetailVersion) {
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
  if (!IsKnownMaterialTransmissionMode(descriptor.transmission_mode)) {
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                     "transmission_mode",
                                     "unknown material transmission mode");
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

  const ValidationResult transmission_factor = ValidateUnitValue(
      descriptor.transmission_factor, "transmission_factor");
  if (!transmission_factor) {
    return transmission_factor;
  }
  const bool normalized_attenuation =
      IsFinite(descriptor.attenuation_color) &&
      descriptor.attenuation_color.x >= 0.0F &&
      descriptor.attenuation_color.x <= 1.0F &&
      descriptor.attenuation_color.y >= 0.0F &&
      descriptor.attenuation_color.y <= 1.0F &&
      descriptor.attenuation_color.z >= 0.0F &&
      descriptor.attenuation_color.z <= 1.0F;
  if (!normalized_attenuation) {
    return ValidationResult::Failure(
        IsFinite(descriptor.attenuation_color)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "attenuation_color",
        "attenuation color must be finite and in [0, 1]");
  }
  if (!IsFinite(descriptor.attenuation_distance_m) ||
      descriptor.attenuation_distance_m <= 0.0F) {
    return ValidationResult::Failure(
        IsFinite(descriptor.attenuation_distance_m)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "attenuation_distance_m",
        "attenuation distance must be finite and positive");
  }
  if (!IsFinite(descriptor.slab_thickness_m) ||
      descriptor.slab_thickness_m < 0.0F) {
    return ValidationResult::Failure(
        IsFinite(descriptor.slab_thickness_m)
            ? ValidationCode::VALUE_OUT_OF_RANGE
            : ValidationCode::NON_FINITE_VALUE,
        "slab_thickness_m",
        "slab thickness must be finite and nonnegative");
  }
  const bool canonical_no_transmission =
      descriptor.transmission_mode == MaterialTransmissionMode::NONE &&
      descriptor.transmission_factor == 0.0F &&
      descriptor.attenuation_color == Float3{1.0F, 1.0F, 1.0F} &&
      descriptor.attenuation_distance_m == 1.0F &&
      descriptor.slab_thickness_m == 0.0F;
  if (descriptor.version == kMaterialDescriptorVersion &&
      !canonical_no_transmission) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "transmission_mode",
        "material v4 requires canonical absent transmission state");
  }
  if (descriptor.transmission_mode == MaterialTransmissionMode::NONE &&
      !canonical_no_transmission) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "transmission_mode",
        "absent transmission requires canonical factor, attenuation, distance, and thickness");
  }
  if (descriptor.transmission_mode ==
      MaterialTransmissionMode::THIN_PARALLEL_SLAB) {
    if (descriptor.version != kMaterialDescriptorTransmissionVersion ||
        descriptor.model != MaterialModel::PBR_METALLIC_ROUGHNESS ||
        descriptor.pbr_workflow != MaterialPbrWorkflow::SPECULAR ||
        descriptor.blend_mode != MaterialBlendMode::REPLACE ||
        descriptor.alpha_test_mode != MaterialAlphaTestMode::DISABLED ||
        descriptor.double_sided || descriptor.depth_write ||
        descriptor.transmission_factor <= 0.0F ||
        descriptor.index_of_refraction <= 1.0F ||
        descriptor.slab_thickness_m <= 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "transmission_mode",
          "thin-slab transmission requires material v5, single-sided specular PBR, replace blending, disabled alpha test/depth writes, positive factor/thickness, and IOR above one");
    }
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

  {
    const ValidationResult detail_validation =
        ValidateTextureBinding(descriptor.detail_weight_texture,
                               "detail_weight_texture");
    if (!detail_validation) {
      return detail_validation;
    }
    bool any_detail_layer = false;
    for (std::size_t layer = 0U; layer < kMaterialDetailMapCount; ++layer) {
      const ValidationResult layer_validation = ValidateTextureBinding(
          descriptor.detail_textures[layer], "detail_textures");
      if (!layer_validation) {
        return layer_validation;
      }
      const ValidationResult normal_validation = ValidateTextureBinding(
          descriptor.detail_normal_textures[layer], "detail_normal_textures");
      if (!normal_validation) {
        return normal_validation;
      }
      if (!std::isfinite(descriptor.detail_weights[layer]) ||
          descriptor.detail_weights[layer] < 0.0F ||
          descriptor.detail_weights[layer] > 1.0F) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "detail_weights",
            "detail weights must be finite and within the unit range", layer);
      }
      if (!std::isfinite(descriptor.detail_normal_weights[layer]) ||
          descriptor.detail_normal_weights[layer] < 0.0F ||
          descriptor.detail_normal_weights[layer] > 1.0F) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "detail_normal_weights",
            "detail normal weights must be finite and within the unit range",
            layer);
      }
      if (!IsKnownMaterialDetailBlendMode(descriptor.detail_blend_modes[layer])) {
        return ValidationResult::Failure(ValidationCode::INVALID_ENUM,
                                         "detail_blend_modes",
                                         "unknown detail blend operator", layer);
      }
      const bool albedo_present = !IsAbsentRenderAssetReference(
          descriptor.detail_textures[layer].texture);
      const bool normal_present = !IsAbsentRenderAssetReference(
          descriptor.detail_normal_textures[layer].texture);
      // One layer owns exactly one UV transform row in the datablock
      // (`mDetailsOffsetScale[i]`), which both its albedo and its normal read.
      // A descriptor asking for two different transforms cannot be projected
      // exactly, so it is refused rather than silently resolved to one of them.
      if (albedo_present && normal_present) {
        const TextureBinding &albedo = descriptor.detail_textures[layer];
        const TextureBinding &normal = descriptor.detail_normal_textures[layer];
        if (albedo.scale != normal.scale || albedo.offset != normal.offset ||
            albedo.rotation_radians != normal.rotation_radians ||
            albedo.texture_coordinate_set != normal.texture_coordinate_set) {
          return ValidationResult::Failure(
              ValidationCode::VALUE_OUT_OF_RANGE, "detail_normal_textures",
              "a detail layer's albedo and normal share one UV transform and "
              "must agree on it",
              layer);
        }
      }
      if (albedo_present || normal_present) {
        any_detail_layer = true;
      }
    }
    const bool weight_absent = IsAbsentRenderAssetReference(
                                   descriptor.detail_weight_texture.texture) &&
                               IsAbsentRenderAssetReference(
                                   descriptor.detail_weight_texture.sampler);
    const bool canonical_no_detail =
        !any_detail_layer && weight_absent &&
        descriptor.detail_weights ==
            std::array<float, kMaterialDetailMapCount>{1.0F, 1.0F, 1.0F, 1.0F} &&
        descriptor.detail_normal_weights ==
            std::array<float, kMaterialDetailMapCount>{1.0F, 1.0F, 1.0F, 1.0F} &&
        descriptor.detail_blend_modes ==
            std::array<MaterialDetailBlendMode, kMaterialDetailMapCount>{
                MaterialDetailBlendMode::NORMAL_NON_PREMUL,
                MaterialDetailBlendMode::NORMAL_NON_PREMUL,
                MaterialDetailBlendMode::NORMAL_NON_PREMUL,
                MaterialDetailBlendMode::NORMAL_NON_PREMUL};
    if (descriptor.version != kMaterialDescriptorDetailVersion &&
        !canonical_no_detail) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "detail_textures",
          "only material v6 may carry weighted detail layers");
    }
    if (any_detail_layer && weight_absent) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "detail_weight_texture",
          "weighted detail layers require their selecting weight mask");
    }
    // The mask spans the surface once. Scaling it would tile the selection
    // along with the layers and destroy the very separation this profile
    // exists to provide.
    const TextureBinding &weight = descriptor.detail_weight_texture;
    const bool weight_identity_transform =
        weight.scale == Float2{1.0F, 1.0F} && weight.offset == Float2{} &&
        weight.rotation_radians == 0.0F;
    if (!weight_absent && !weight_identity_transform) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "detail_weight_texture",
          "the detail weight mask must keep its canonical identity transform");
    }
    // Detail layers may only be applied to a lit surface: the composite is
    // performed by the PBS shading path, not by a blended second draw.
    if (any_detail_layer &&
        descriptor.model != MaterialModel::PBR_METALLIC_ROUGHNESS) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "detail_textures",
          "weighted detail layers require the metallic-roughness PBR model");
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
  // BC7 carries four channels at one byte per texel and is the only block
  // format that may hold an sRGB transfer, so it substitutes for RGBA8
  // wherever a slot wants displayable colour.
  const bool bc7_storage = texture.format == TextureResourceFormat::BC7_UNORM;
  // BC5 is two unsigned channels: exactly the tangent-space XY a normal map
  // needs, with Z reconstructed in the shader as it already is for RG8.
  const bool bc5_storage = texture.format == TextureResourceFormat::BC5_UNORM;
  // BC4 is one unsigned channel.
  const bool bc4_storage = texture.format == TextureResourceFormat::BC4_UNORM;
  switch (slot) {
  case MaterialTextureSlot::BASE_COLOR:
  case MaterialTextureSlot::EMISSIVE:
    if ((texture.format != TextureResourceFormat::RGBA8_UNORM && !bc7_storage) ||
        texture.color_space != TextureColorSpace::SRGB) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "base-color and emissive slots require RGBA8 or BC7 sRGB storage");
    }
    break;
  case MaterialTextureSlot::METALLIC_ROUGHNESS:
    // Deliberately NOT block-compressed. This one binding becomes two
    // single-channel GPU textures by extracting green and blue, and a channel
    // cannot be read out of a BC block without decoding it. Refusing by name
    // is honest; silently uploading the packed block to both roles would make
    // metallic equal roughness.
    if (!rgba_storage || texture.color_space != TextureColorSpace::LINEAR) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          IsBlockCompressedTextureResourceFormat(texture.format)
              ? "metallic-roughness cannot be block-compressed: the slot is "
                "channel-split into separate roughness and metallic textures, "
                "which requires addressable texels"
              : "metallic-roughness slot requires linear RGBA storage");
    }
    break;
  case MaterialTextureSlot::NORMAL:
    if ((!rgba_storage && !bc5_storage) ||
        texture.color_space != TextureColorSpace::LINEAR) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "normal slot requires linear RGBA or BC5 storage");
    }
    break;
  case MaterialTextureSlot::SPECULAR:
    if ((!rgba_storage && !bc7_storage) ||
        texture.color_space != TextureColorSpace::LINEAR) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "specular slot requires linear RGBA or BC7 storage");
    }
    break;
  case MaterialTextureSlot::OCCLUSION:
    if (texture.color_space != TextureColorSpace::LINEAR) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.color_space",
          "occlusion slot requires linear storage with an R channel");
    }
    if (IsBlockCompressedTextureResourceFormat(texture.format) &&
        !bc4_storage) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "a block-compressed occlusion map must be BC4, the single-channel "
          "block format");
    }
    break;
  case MaterialTextureSlot::DETAIL_WEIGHT:
    // The weight mask selects layers; it is never displayed, so it must not
    // carry a transfer function that sampling would decode.
    if (!rgba_storage || texture.color_space != TextureColorSpace::LINEAR) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "detail weight slot requires linear RGBA storage");
    }
    break;
  case MaterialTextureSlot::DETAIL0:
  case MaterialTextureSlot::DETAIL1:
  case MaterialTextureSlot::DETAIL2:
  case MaterialTextureSlot::DETAIL3:
    // Detail layers are albedo and composite with the base color, so they
    // share the base-color storage rule exactly.
    if ((texture.format != TextureResourceFormat::RGBA8_UNORM && !bc7_storage) ||
        texture.color_space != TextureColorSpace::SRGB) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "detail albedo slots require RGBA8 or BC7 sRGB storage");
    }
    break;
  case MaterialTextureSlot::DETAIL0_NM:
  case MaterialTextureSlot::DETAIL1_NM:
  case MaterialTextureSlot::DETAIL2_NM:
  case MaterialTextureSlot::DETAIL3_NM:
    // Detail normals are tangent-space vectors decoded as `2 * texel - 1`, so
    // they share the base normal slot's storage rule exactly, BC5 included: it
    // is the tangent-space XY with Z reconstructed in the shader. An sRGB
    // detail normal would decode its own texels before the vector decode and
    // bend the surface the wrong way, so the linear transfer stays required.
    if ((!rgba_storage && !bc5_storage) ||
        texture.color_space != TextureColorSpace::LINEAR) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "texture.format",
          "detail normal slots require linear RGBA or BC5 storage");
    }
    break;
  default:
    return ValidationResult::Failure(ValidationCode::INVALID_ENUM, "slot",
                                     "unknown material texture slot");
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
