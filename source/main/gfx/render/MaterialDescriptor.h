/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral physically based material description.

#pragma once

#include "RenderAssetId.h"
#include "RenderMath.h"
#include "RenderValidation.h"

#include <cstdint>
#include <string>

namespace RoR::Render {

struct MeshResourceDescriptor;
struct SamplerResourceDescriptor;
struct TextureResourceDescriptor;

// Version 3 makes the base-color transfer/filter ordering explicit.
constexpr std::uint32_t kMaterialDescriptorVersion = 3U;
constexpr std::size_t kMaximumMaterialDebugNameBytes = 255U;

enum class MaterialModel : std::uint8_t {
  PBR_METALLIC_ROUGHNESS = 0,
  UNLIT = 1,
};

enum class MaterialAlphaMode : std::uint8_t {
  OPAQUE = 0,
  MASK = 1,
  BLEND = 2,
};

/// Transfer ordering for an sRGB base-color texture.
///
/// SRGB_DECODE_BEFORE_FILTER is the conventional GPU sRGB texture path:
/// texels are decoded before interpolation and mip filtering. The display
/// domain mode preserves encoded RGB through sampling/filtering and applies
/// the exact sRGB EOTF to the filtered result. It exists for authenticated
/// legacy composite maps and must never be inferred from MaterialModel.
enum class BaseColorTransfer : std::uint8_t {
  SRGB_DECODE_BEFORE_FILTER = 0,
  SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE = 1,
};

enum class MaterialTextureSlot : std::uint8_t {
  BASE_COLOR = 0,
  METALLIC_ROUGHNESS = 1,
  NORMAL = 2,
  OCCLUSION = 3,
  EMISSIVE = 4,
};

struct TextureBinding {
  RenderAssetReference texture;
  /// Required whenever texture is valid; there is no backend-defined implicit
  /// sampler state.
  RenderAssetReference sampler;
  std::uint8_t texture_coordinate_set = 0U;
  Float2 scale{1.0F, 1.0F};
  Float2 offset{};
  float rotation_radians = 0.0F;
};

/// Canonical glTF-style material interpretation:
///
/// - base-color texture RGB is sRGB, A is linear; the factor is linear RGBA;
///   final base RGBA is component-wise factor * sampled base texture * linear
///   vertex color (or white when absent), including vertex alpha before MASK
///   testing or straight-alpha BLEND;
/// - metallic/roughness is linear with roughness in G and metallic in B;
/// - tangent-space normal RGB is linear, decoded as `2 * texel - 1`, with +Y
///   aligned to the mesh bitangent convention from MeshResourceDescriptor;
/// - occlusion is linear R; emissive texture RGB is sRGB and factors are
/// linear;
/// - alpha is base texture A times factor A times vertex-color A (white when
///   absent), is never premultiplied, and BLEND uses straight-alpha source-over
///   compositing;
/// - texture transforms apply `offset + rotate(rotation, scale * uv)` about
///   UV origin (0, 0). With +V downward, positive rotation is clockwise.
///
/// A frontend must check texture color-space compatibility against the live
/// texture registry when creating or updating a material.
struct MaterialDescriptor {
  std::uint32_t version = kMaterialDescriptorVersion;
  std::string debug_name;
  MaterialModel model = MaterialModel::PBR_METALLIC_ROUGHNESS;
  MaterialAlphaMode alpha_mode = MaterialAlphaMode::OPAQUE;
  BaseColorTransfer base_color_transfer =
      BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER;
  bool double_sided = false;

  Float4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
  float metallic_factor = 0.0F;
  float roughness_factor = 1.0F;
  float normal_scale = 1.0F;
  float occlusion_strength = 1.0F;
  Float3 emissive_factor{};
  float emissive_strength = 1.0F;
  float alpha_cutoff = 0.5F;
  float index_of_refraction = 1.5F;

  TextureBinding base_color_texture;
  TextureBinding metallic_roughness_texture;
  TextureBinding normal_texture;
  TextureBinding occlusion_texture;
  TextureBinding emissive_texture;
};

[[nodiscard]] bool IsKnownMaterialModel(MaterialModel model) noexcept;
[[nodiscard]] bool IsKnownMaterialAlphaMode(MaterialAlphaMode mode) noexcept;
[[nodiscard]] bool
IsKnownBaseColorTransfer(BaseColorTransfer transfer) noexcept;
[[nodiscard]] ValidationResult
ValidateMaterialDescriptor(const MaterialDescriptor &descriptor);
/// Validates the exact vertex streams needed to apply this material without
/// backend-specific synthesis. Frontends call this for every mesh/material
/// pair referenced by a submitted scene.
[[nodiscard]] ValidationResult
ValidateMaterialMeshCompatibility(const MaterialDescriptor &material,
                                  const MeshResourceDescriptor &mesh);
/// Validates the live resource descriptors resolved for one bound slot.
[[nodiscard]] ValidationResult
ValidateMaterialTextureCompatibility(MaterialTextureSlot slot,
                                     const TextureResourceDescriptor &texture,
                                     const SamplerResourceDescriptor &sampler);

} // namespace RoR::Render
