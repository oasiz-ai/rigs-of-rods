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

#include <array>
#include <cstdint>
#include <string>

namespace RoR::Render {

struct MeshResourceDescriptor;
struct SamplerResourceDescriptor;
struct TextureResourceDescriptor;

// Version 4 adds an explicit PBR workflow and authored linear-RGB specular
// texture/factor. Version 5 is an additive native-only profile for an authored
// thin parallel glass slab. Version 6 is an additive native-only profile for
// weighted detail maps, which carry their own per-binding UV scale instead of
// the material-wide UV0 affine. The cross-process V2 transport remains fixed
// to material v4; a v5 or v6 material can enter only through a versioned
// native package until a future wire kind is reviewed.
constexpr std::uint32_t kMaterialDescriptorVersion = 4U;
constexpr std::uint32_t kMaterialDescriptorTransmissionVersion = 5U;
constexpr std::uint32_t kMaterialDescriptorDetailVersion = 6U;
constexpr std::size_t kMaximumMaterialDebugNameBytes = 255U;
/// Weighted detail layers a v6 material may carry. This matches the pinned
/// HLMS PBS detail budget exactly; there is no fifth channel to grow into.
constexpr std::size_t kMaterialDetailMapCount = 4U;

enum class MaterialModel : std::uint8_t {
  PBR_METALLIC_ROUGHNESS = 0,
  UNLIT = 1,
};

enum class MaterialBlendMode : std::uint8_t {
  REPLACE = 0,
  /// RGB is SRC_ALPHA / ONE_MINUS_SRC_ALPHA while alpha is ONE /
  /// ONE_MINUS_SRC_ALPHA (Porter-Duff straight source-over).
  STRAIGHT_SOURCE_OVER = 1,
  /// OGRE's historical `scene_blend alpha_blend`: SRC_ALPHA /
  /// ONE_MINUS_SRC_ALPHA for both RGB and alpha. This intentionally preserves
  /// the legacy squared-alpha destination equation and is not source-over.
  LEGACY_STRAIGHT_ALPHA = 2,
  /// Porter-Duff source-over for premultiplied source content: ONE /
  /// ONE_MINUS_SRC_ALPHA for both RGB and alpha. Source RGB is already
  /// multiplied by its coverage (e.g. a GUI render target accumulated over a
  /// zero-cleared background), so no SRC_ALPHA factor is applied.
  PREMULTIPLIED_SOURCE_OVER = 3,
};

enum class MaterialAlphaTestMode : std::uint8_t {
  DISABLED = 0,
  /// Keep fragments whose resolved straight alpha is strictly greater than
  /// alpha_cutoff.
  GREATER = 1,
  /// Keep fragments whose resolved straight alpha is greater than or equal to
  /// alpha_cutoff.
  GREATER_EQUAL = 2,
};

enum class MaterialPbrWorkflow : std::uint8_t {
  METALLIC_ROUGHNESS = 0,
  SPECULAR = 1,
};

enum class MaterialTransmissionMode : std::uint8_t {
  NONE = 0,
  /// A watertight, planar-front/back slab with parallel interfaces. The
  /// renderer applies Snell entry/exit displacement and Beer-Lambert
  /// attenuation over the authored normal thickness. This deliberately does
  /// not describe a general closed solid or claim off-screen radiance.
  THIN_PARALLEL_SLAB = 1,
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
  SPECULAR = 5,
  /// Linear RGBA weights selecting DETAIL0..DETAIL3 respectively. Sampled at
  /// unscaled UV0 so one page-wide mask can drive layers that each repeat at
  /// their own authored rate.
  DETAIL_WEIGHT = 6,
  DETAIL0 = 7,
  DETAIL1 = 8,
  DETAIL2 = 9,
  DETAIL3 = 10,
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

/// Canonical renderer-neutral material interpretation:
///
/// - base-color texture RGB is sRGB, A is linear; the factor is linear RGBA;
///   final base RGBA is component-wise factor * sampled base texture * linear
///   vertex color (or white when absent), including vertex alpha before alpha
///   testing or blending;
/// - metallic/roughness is linear with roughness in G and metallic in B;
/// - tangent-space normal RGB is linear, decoded as `2 * texel - 1`, with +Y
///   aligned to the mesh bitangent convention from MeshResourceDescriptor;
/// - occlusion is linear R; emissive texture RGB is sRGB and factors are
/// linear;
/// - METALLIC_ROUGHNESS uses roughness G and metallic B exactly as above;
/// - SPECULAR uses linear authored RGB from specular_texture multiplied by
///   specular_factor. Roughness remains the explicit scalar
///   roughness_factor. Metallic state is never synthesized in this workflow;
/// - alpha is base texture A times factor A times vertex-color A (white when
///   absent). STRAIGHT_SOURCE_OVER uses SRC_ALPHA/ONE_MINUS_SRC_ALPHA for RGB
///   and ONE/ONE_MINUS_SRC_ALPHA for alpha; LEGACY_STRAIGHT_ALPHA uses
///   SRC_ALPHA/ONE_MINUS_SRC_ALPHA for both. Sampled content is never
///   premultiplied by the renderer; PREMULTIPLIED_SOURCE_OVER instead declares
///   that the authored texel RGB already carries its coverage and blends
///   ONE/ONE_MINUS_SRC_ALPHA on both channels;
/// - alpha testing is independent of blending. GREATER rejects equality while
///   GREATER_EQUAL keeps equality; depth testing is always enabled with
///   LESS_EQUAL while depth_write is explicit. This represents blended cutout
///   layers without silently collapsing them into one mutually exclusive alpha
///   mode;
/// - texture transforms apply `offset + rotate(rotation, scale * uv)` about
///   UV origin (0, 0). With +V downward, positive rotation is clockwise.
///
/// A frontend must check texture color-space compatibility against the live
/// texture registry when creating or updating a material.
struct MaterialDescriptor {
  std::uint32_t version = kMaterialDescriptorVersion;
  std::string debug_name;
  MaterialModel model = MaterialModel::PBR_METALLIC_ROUGHNESS;
  MaterialPbrWorkflow pbr_workflow =
      MaterialPbrWorkflow::METALLIC_ROUGHNESS;
  MaterialBlendMode blend_mode = MaterialBlendMode::REPLACE;
  MaterialAlphaTestMode alpha_test_mode = MaterialAlphaTestMode::DISABLED;
  BaseColorTransfer base_color_transfer =
      BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER;
  bool double_sided = false;
  bool depth_write = true;

  Float4 base_color_factor{1.0F, 1.0F, 1.0F, 1.0F};
  float metallic_factor = 0.0F;
  float roughness_factor = 1.0F;
  Float3 specular_factor{1.0F, 1.0F, 1.0F};
  float normal_scale = 1.0F;
  float occlusion_strength = 1.0F;
  Float3 emissive_factor{};
  float emissive_strength = 1.0F;
  float alpha_cutoff = 0.5F;
  float index_of_refraction = 1.5F;

  MaterialTransmissionMode transmission_mode =
      MaterialTransmissionMode::NONE;
  float transmission_factor = 0.0F;
  Float3 attenuation_color{1.0F, 1.0F, 1.0F};
  float attenuation_distance_m = 1.0F;
  float slab_thickness_m = 0.0F;

  TextureBinding base_color_texture;
  TextureBinding metallic_roughness_texture;
  TextureBinding normal_texture;
  TextureBinding occlusion_texture;
  TextureBinding emissive_texture;
  TextureBinding specular_texture;

  /// Weighted detail layers, v6 only.
  ///
  /// Unlike every slot above, each detail binding keeps its OWN scale/offset
  /// rather than joining the material-wide UV0 affine. That is the whole point
  /// of the profile: a page-sized surface can carry one unscaled weight mask
  /// while each layer repeats at the rate its texture was authored for.
  /// detail_weight_texture must therefore stay at identity scale.
  ///
  /// Layer i is composited over the running result as
  /// `lerp(result, detail_i, weight_channel_i * detail_weights[i])`, matching
  /// the sequential blend the legacy terrain material performed.
  TextureBinding detail_weight_texture;
  std::array<TextureBinding, kMaterialDetailMapCount> detail_textures;
  std::array<float, kMaterialDetailMapCount> detail_weights{1.0F, 1.0F, 1.0F,
                                                            1.0F};
};

[[nodiscard]] bool IsKnownMaterialModel(MaterialModel model) noexcept;
[[nodiscard]] bool
IsKnownMaterialPbrWorkflow(MaterialPbrWorkflow workflow) noexcept;
[[nodiscard]] bool
IsKnownMaterialTransmissionMode(MaterialTransmissionMode mode) noexcept;
[[nodiscard]] bool IsKnownMaterialBlendMode(MaterialBlendMode mode) noexcept;
[[nodiscard]] bool
IsKnownMaterialAlphaTestMode(MaterialAlphaTestMode mode) noexcept;
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
