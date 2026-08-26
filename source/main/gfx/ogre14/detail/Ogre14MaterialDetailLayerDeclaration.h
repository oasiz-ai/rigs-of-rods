/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Declarative content vocabulary for weighted detail material layers.

#pragma once

#include "gfx/render/MaterialDescriptor.h"

#include <OgreMaterial.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace RoR::Gfx::Detail {

/// Name prefix of the companion material that declares a base material's
/// detail layers. The companion is looked up as
/// `kMaterialDetailLayerCompanionPrefix + <base material name>`.
inline constexpr char kMaterialDetailLayerCompanionPrefix[] =
    "RoR/DetailLayers/";

/// Reserved texture-unit name declaring the layer-selecting weight mask.
inline constexpr char kMaterialDetailWeightUnitName[] = "ror_detail_weight";
/// Reserved texture-unit name prefix declaring one layer. The remainder is
/// `<index>` optionally followed by `_<blend token>`, or `<index>_nm` for that
/// layer's detail normal.
inline constexpr char kMaterialDetailUnitNamePrefix[] = "ror_detail";

/// Why a companion declaration was refused. Every value is a truthful,
/// specific statement about the authored script; there is no generic bucket.
enum class MaterialDetailLayerRefusal : std::uint8_t {
  NONE = 0,
  /// No companion material exists. This is the ordinary case for the vast
  /// majority of materials and is not an error.
  ABSENT = 1,
  /// The companion exists but is not a single technique with a single pass.
  COMPANION_SHAPE_UNSUPPORTED = 2,
  /// A texture unit's name is not in the reserved vocabulary. The companion
  /// exists only to declare layers, so a stray unit means the author intended
  /// something this reader does not implement.
  UNIT_NAME_UNRECOGNISED = 3,
  /// Two units claimed the same layer role.
  UNIT_ROLE_DUPLICATED = 4,
  /// A unit named a layer index at or beyond the pinned detail budget.
  LAYER_INDEX_OUT_OF_RANGE = 5,
  /// A unit's blend token is not one of MaterialDetailBlendMode's tokens.
  BLEND_TOKEN_UNRECOGNISED = 6,
  /// A unit does not name exactly one ordinary 2D texture.
  UNIT_TEXTURE_UNSUPPORTED = 7,
  /// A unit's texture transform is not a finite, axis-aligned scale and
  /// translation of UV0. Rotation and shear stay fail-closed.
  UNIT_TRANSFORM_UNSUPPORTED = 8,
  /// A layer's albedo and normal disagree on their shared UV transform.
  LAYER_TRANSFORM_DISAGREES = 9,
  /// The weight mask carries a transform. It spans the surface exactly once.
  WEIGHT_MASK_TRANSFORM_UNSUPPORTED = 10,
  /// A declared weight is not finite or lies outside the unit range.
  WEIGHT_OUT_OF_RANGE = 11,
  /// Layers were declared without the mask that selects them.
  WEIGHT_MASK_ABSENT = 12,
  /// The companion declared no layer at all.
  NO_LAYER_DECLARED = 13,
};

/// Stable lowercase token for one refusal, for audit lines. Never null.
[[nodiscard]] const char *
MaterialDetailLayerRefusalToken(MaterialDetailLayerRefusal refusal) noexcept;

/// One authored layer request, resolved from the companion script but not yet
/// bound to any engine asset.
struct MaterialDetailLayerRequest final {
  /// Ogre resource name of this layer's albedo, empty when the layer declares
  /// only relief.
  std::string albedo_texture_name;
  /// Ogre resource name of this layer's tangent-space detail normal, empty
  /// when the layer declares only colour.
  std::string normal_texture_name;
  Render::MaterialDetailBlendMode blend_mode =
      Render::MaterialDetailBlendMode::NORMAL_NON_PREMUL;
  float weight = 1.0F;
  float normal_weight = 1.0F;
  /// The layer's own UV0 scale and offset, decoded from the authored texture
  /// matrix. Shared by the albedo and the normal, which is why they must
  /// agree.
  Render::Float2 scale{1.0F, 1.0F};
  Render::Float2 offset{};

  [[nodiscard]] bool declared() const noexcept {
    return !albedo_texture_name.empty() || !normal_texture_name.empty();
  }
};

/// A whole companion declaration.
struct MaterialDetailLayerDeclaration final {
  /// Ogre resource name of the linear RGBA mask whose R/G/B/A channels select
  /// layers 0..3. Sampled unscaled across the surface.
  std::string weight_mask_texture_name;
  std::array<MaterialDetailLayerRequest, Render::kMaterialDetailMapCount>
      layers;

  [[nodiscard]] std::size_t declared_layer_count() const noexcept {
    std::size_t count = 0U;
    for (const MaterialDetailLayerRequest &layer : layers) {
      count += layer.declared() ? 1U : 0U;
    }
    return count;
  }
  [[nodiscard]] std::size_t declared_normal_layer_count() const noexcept {
    std::size_t count = 0U;
    for (const MaterialDetailLayerRequest &layer : layers) {
      count += layer.normal_texture_name.empty() ? 0U : 1U;
    }
    return count;
  }
};

/// Companion material name for one base material.
[[nodiscard]] std::string
BuildMaterialDetailLayerCompanionName(std::string_view base_material_name);

/// Reads the companion material's declaration.
///
/// The vocabulary is ordinary OGRE material script: every value below is read
/// back from live TextureUnitState accessors, so an author writes nothing the
/// stock parser does not already understand and the companion produces no
/// script warnings.
///
///   texture_unit ror_detail_weight  -> the layer-selecting mask, identity
///                                      transform required
///   texture_unit ror_detail<i>            -> layer i albedo, `normal` operator
///   texture_unit ror_detail<i>_<token>    -> layer i albedo, named operator
///   texture_unit ror_detail<i>_nm         -> layer i detail normal
///
/// A unit's UV scale and offset come from its composed texture matrix, which
/// is the exact meaning of whatever `scale`/`scroll` the author wrote, and a
/// layer's weight comes from `alpha_op_ex source1 src_manual src_current <w>`
/// when present (otherwise 1).
///
/// Returns false and sets `refusal` for anything it cannot read exactly;
/// `declaration` is then left untouched. A null or empty companion answers
/// ABSENT, which callers treat as "this material simply has no layers", not as
/// an error. Never throws.
[[nodiscard]] bool ReadMaterialDetailLayerDeclaration(
    const Ogre::MaterialPtr &companion,
    MaterialDetailLayerDeclaration &declaration,
    MaterialDetailLayerRefusal &refusal) noexcept;

} // namespace RoR::Gfx::Detail
