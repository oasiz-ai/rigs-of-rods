/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14MaterialDetailLayerDeclaration.h"

#include <OgreBlendMode.h>
#include <OgreMatrix4.h>
#include <OgrePass.h>
#include <OgreTechnique.h>
#include <OgreTextureUnitState.h>

#include <cmath>
#include <cstdlib>
#include <limits>

namespace RoR::Gfx::Detail {
namespace {

using Render::kMaterialDetailMapCount;
using Render::MaterialDetailBlendMode;

constexpr float kTransformEpsilon = 1.0e-6F;

[[nodiscard]] bool NearlyEqual(float left, float right) noexcept {
  return std::isfinite(left) && std::isfinite(right) &&
         std::fabs(left - right) <= kTransformEpsilon;
}

/// Decodes one texture unit's composed UV0 transform into the descriptor's
/// `offset + scale * uv` model.
///
/// The composed matrix is the ground truth: it is exactly what the authored
/// `scale`/`scroll`/`rotate` directives mean once OGRE has combined them,
/// including OGRE's own centre pivot for scaling, which lands in the
/// translation term where this model wants it. Anything with rotation, shear,
/// a third dimension, or a non-finite entry is refused rather than
/// approximated.
[[nodiscard]] bool DecodeAxisAlignedTransform(
    const Ogre::TextureUnitState &unit, Render::Float2 &scale,
    Render::Float2 &offset) noexcept {
  const Ogre::Matrix4 &matrix = unit.getTextureTransform();
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      if (!std::isfinite(static_cast<float>(matrix[row][column]))) {
        return false;
      }
    }
  }
  // Off-diagonal UV coupling is rotation or shear; neither is representable.
  if (!NearlyEqual(static_cast<float>(matrix[0][1]), 0.0F) ||
      !NearlyEqual(static_cast<float>(matrix[1][0]), 0.0F)) {
    return false;
  }
  // The transform must not disturb the unused third and fourth rows/columns.
  if (!NearlyEqual(static_cast<float>(matrix[2][2]), 1.0F) ||
      !NearlyEqual(static_cast<float>(matrix[3][3]), 1.0F) ||
      !NearlyEqual(static_cast<float>(matrix[0][2]), 0.0F) ||
      !NearlyEqual(static_cast<float>(matrix[1][2]), 0.0F) ||
      !NearlyEqual(static_cast<float>(matrix[2][0]), 0.0F) ||
      !NearlyEqual(static_cast<float>(matrix[2][1]), 0.0F) ||
      !NearlyEqual(static_cast<float>(matrix[2][3]), 0.0F) ||
      !NearlyEqual(static_cast<float>(matrix[3][0]), 0.0F) ||
      !NearlyEqual(static_cast<float>(matrix[3][1]), 0.0F) ||
      !NearlyEqual(static_cast<float>(matrix[3][2]), 0.0F)) {
    return false;
  }
  const float scale_u = static_cast<float>(matrix[0][0]);
  const float scale_v = static_cast<float>(matrix[1][1]);
  // A zero or negative repeat is not a density; the native detail path also
  // requires strictly positive scale components.
  if (!(scale_u > 0.0F) || !(scale_v > 0.0F)) {
    return false;
  }
  if (unit.getTextureRotate().valueRadians() != Ogre::Radian(0.0F).valueRadians()) {
    return false;
  }
  scale = Render::Float2{scale_u, scale_v};
  offset = Render::Float2{static_cast<float>(matrix[0][3]),
                          static_cast<float>(matrix[1][3])};
  return true;
}

[[nodiscard]] bool IsIdentityTransform(const Render::Float2 &scale,
                                       const Render::Float2 &offset) noexcept {
  return NearlyEqual(scale.x, 1.0F) && NearlyEqual(scale.y, 1.0F) &&
         NearlyEqual(offset.x, 0.0F) && NearlyEqual(offset.y, 0.0F);
}

/// A unit must name exactly one ordinary, loadable 2D texture on UV0. This is
/// the same shape the base-colour projection already demands.
[[nodiscard]] bool IsOrdinaryNamed2dUnit(
    const Ogre::TextureUnitState &unit) noexcept {
  return unit.getContentType() == Ogre::TextureUnitState::CONTENT_NAMED &&
         unit.getTextureType() == Ogre::TEX_TYPE_2D &&
         unit.getNumFrames() == 1U && unit.getCurrentFrame() == 0U &&
         unit.getTextureCoordSet() == 0U && unit.getEffects().empty() &&
         unit._deriveTexCoordCalcMethod() == Ogre::TEXCALC_NONE &&
         unit.getProjectiveTexturingFrustum() == nullptr &&
         !unit.getTextureName().empty();
}

/// A manual alpha argument is the authored weight; anything else means the
/// author did not state one and the layer keeps full strength.
[[nodiscard]] bool ReadManualAlphaWeight(const Ogre::TextureUnitState &unit,
                                         float &weight) noexcept {
  const Ogre::LayerBlendModeEx alpha = unit.getAlphaBlendMode();
  if (alpha.source1 != Ogre::LBS_MANUAL) {
    weight = 1.0F;
    return true;
  }
  const float value = alpha.alphaArg1;
  if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
    return false;
  }
  weight = value;
  return true;
}

struct ParsedUnitName final {
  bool weight_mask = false;
  bool normal = false;
  std::size_t layer_index = 0U;
  MaterialDetailBlendMode blend_mode =
      MaterialDetailBlendMode::NORMAL_NON_PREMUL;
};

/// Grammar: `ror_detail_weight`, `ror_detail<i>`, `ror_detail<i>_nm`, or
/// `ror_detail<i>_<blend token>`.
[[nodiscard]] bool ParseUnitName(std::string_view name, ParsedUnitName &parsed,
                                 MaterialDetailLayerRefusal &refusal) noexcept {
  if (name == kMaterialDetailWeightUnitName) {
    parsed.weight_mask = true;
    return true;
  }
  const std::string_view prefix(kMaterialDetailUnitNamePrefix);
  if (name.size() <= prefix.size() || name.substr(0U, prefix.size()) != prefix) {
    refusal = MaterialDetailLayerRefusal::UNIT_NAME_UNRECOGNISED;
    return false;
  }
  std::string_view remainder = name.substr(prefix.size());
  if (remainder.empty() || remainder[0U] < '0' || remainder[0U] > '9') {
    refusal = MaterialDetailLayerRefusal::UNIT_NAME_UNRECOGNISED;
    return false;
  }
  const std::size_t index = static_cast<std::size_t>(remainder[0U] - '0');
  remainder = remainder.substr(1U);
  if (index >= kMaterialDetailMapCount) {
    refusal = MaterialDetailLayerRefusal::LAYER_INDEX_OUT_OF_RANGE;
    return false;
  }
  parsed.layer_index = index;
  if (remainder.empty()) {
    return true;
  }
  if (remainder[0U] != '_') {
    refusal = MaterialDetailLayerRefusal::UNIT_NAME_UNRECOGNISED;
    return false;
  }
  remainder = remainder.substr(1U);
  if (remainder == "nm") {
    parsed.normal = true;
    return true;
  }
  if (!Render::ParseMaterialDetailBlendModeToken(remainder,
                                                 parsed.blend_mode)) {
    refusal = MaterialDetailLayerRefusal::BLEND_TOKEN_UNRECOGNISED;
    return false;
  }
  return true;
}

} // namespace

const char *
MaterialDetailLayerRefusalToken(MaterialDetailLayerRefusal refusal) noexcept {
  switch (refusal) {
  case MaterialDetailLayerRefusal::NONE:
    return "none";
  case MaterialDetailLayerRefusal::ABSENT:
    return "absent";
  case MaterialDetailLayerRefusal::COMPANION_SHAPE_UNSUPPORTED:
    return "companion_shape_unsupported";
  case MaterialDetailLayerRefusal::UNIT_NAME_UNRECOGNISED:
    return "unit_name_unrecognised";
  case MaterialDetailLayerRefusal::UNIT_ROLE_DUPLICATED:
    return "unit_role_duplicated";
  case MaterialDetailLayerRefusal::LAYER_INDEX_OUT_OF_RANGE:
    return "layer_index_out_of_range";
  case MaterialDetailLayerRefusal::BLEND_TOKEN_UNRECOGNISED:
    return "blend_token_unrecognised";
  case MaterialDetailLayerRefusal::UNIT_TEXTURE_UNSUPPORTED:
    return "unit_texture_unsupported";
  case MaterialDetailLayerRefusal::UNIT_TRANSFORM_UNSUPPORTED:
    return "unit_transform_unsupported";
  case MaterialDetailLayerRefusal::LAYER_TRANSFORM_DISAGREES:
    return "layer_transform_disagrees";
  case MaterialDetailLayerRefusal::WEIGHT_MASK_TRANSFORM_UNSUPPORTED:
    return "weight_mask_transform_unsupported";
  case MaterialDetailLayerRefusal::WEIGHT_OUT_OF_RANGE:
    return "weight_out_of_range";
  case MaterialDetailLayerRefusal::WEIGHT_MASK_ABSENT:
    return "weight_mask_absent";
  case MaterialDetailLayerRefusal::NO_LAYER_DECLARED:
    return "no_layer_declared";
  case MaterialDetailLayerRefusal::ARTWORK_UNRESOLVABLE:
    return "artwork_unresolvable";
  case MaterialDetailLayerRefusal::COUNT:
    break;
  }
  return "unknown";
}

std::string
BuildMaterialDetailLayerCompanionName(std::string_view base_material_name) {
  std::string name(kMaterialDetailLayerCompanionPrefix);
  name.append(base_material_name);
  return name;
}

bool ReadMaterialDetailLayerDeclaration(
    const Ogre::MaterialPtr &companion,
    MaterialDetailLayerDeclaration &declaration,
    MaterialDetailLayerRefusal &refusal) noexcept {
  refusal = MaterialDetailLayerRefusal::ABSENT;
  try {
    if (!companion) {
      return false;
    }
    if (companion->getNumTechniques() != 1U) {
      refusal = MaterialDetailLayerRefusal::COMPANION_SHAPE_UNSUPPORTED;
      return false;
    }
    const Ogre::Technique *const technique = companion->getTechnique(0U);
    if (technique == nullptr || technique->getNumPasses() != 1U) {
      refusal = MaterialDetailLayerRefusal::COMPANION_SHAPE_UNSUPPORTED;
      return false;
    }
    const Ogre::Pass *const pass = technique->getPass(0U);
    if (pass == nullptr) {
      refusal = MaterialDetailLayerRefusal::COMPANION_SHAPE_UNSUPPORTED;
      return false;
    }
    // The companion exists only to declare layers. A vertex or fragment
    // program on it would mean the author expected it to be drawn, which it
    // never is.
    if (pass->hasVertexProgram() || pass->hasFragmentProgram() ||
        pass->hasGeometryProgram()) {
      refusal = MaterialDetailLayerRefusal::COMPANION_SHAPE_UNSUPPORTED;
      return false;
    }
    const std::uint16_t unit_count = pass->getNumTextureUnitStates();
    if (unit_count == 0U) {
      refusal = MaterialDetailLayerRefusal::NO_LAYER_DECLARED;
      return false;
    }

    MaterialDetailLayerDeclaration candidate;
    std::array<bool, kMaterialDetailMapCount> albedo_seen{};
    std::array<bool, kMaterialDetailMapCount> normal_seen{};
    std::array<bool, kMaterialDetailMapCount> transform_seen{};
    bool weight_seen = false;

    for (std::uint16_t index = 0U; index < unit_count; ++index) {
      const Ogre::TextureUnitState *const unit =
          pass->getTextureUnitState(index);
      if (unit == nullptr) {
        refusal = MaterialDetailLayerRefusal::UNIT_TEXTURE_UNSUPPORTED;
        return false;
      }
      ParsedUnitName parsed;
      if (!ParseUnitName(unit->getName(), parsed, refusal)) {
        return false;
      }
      if (!IsOrdinaryNamed2dUnit(*unit)) {
        refusal = MaterialDetailLayerRefusal::UNIT_TEXTURE_UNSUPPORTED;
        return false;
      }
      Render::Float2 scale{1.0F, 1.0F};
      Render::Float2 offset{};
      if (!DecodeAxisAlignedTransform(*unit, scale, offset)) {
        refusal = MaterialDetailLayerRefusal::UNIT_TRANSFORM_UNSUPPORTED;
        return false;
      }
      float weight = 1.0F;
      if (!ReadManualAlphaWeight(*unit, weight)) {
        refusal = MaterialDetailLayerRefusal::WEIGHT_OUT_OF_RANGE;
        return false;
      }

      if (parsed.weight_mask) {
        if (weight_seen) {
          refusal = MaterialDetailLayerRefusal::UNIT_ROLE_DUPLICATED;
          return false;
        }
        // The mask spans the surface once, which is the entire reason the
        // layers may each repeat at their own rate.
        if (!IsIdentityTransform(scale, offset)) {
          refusal =
              MaterialDetailLayerRefusal::WEIGHT_MASK_TRANSFORM_UNSUPPORTED;
          return false;
        }
        weight_seen = true;
        candidate.weight_mask_texture_name = unit->getTextureName();
        continue;
      }

      MaterialDetailLayerRequest &layer = candidate.layers[parsed.layer_index];
      if (parsed.normal) {
        if (normal_seen[parsed.layer_index]) {
          refusal = MaterialDetailLayerRefusal::UNIT_ROLE_DUPLICATED;
          return false;
        }
        normal_seen[parsed.layer_index] = true;
        layer.normal_texture_name = unit->getTextureName();
        layer.normal_weight = weight;
      } else {
        if (albedo_seen[parsed.layer_index]) {
          refusal = MaterialDetailLayerRefusal::UNIT_ROLE_DUPLICATED;
          return false;
        }
        albedo_seen[parsed.layer_index] = true;
        layer.albedo_texture_name = unit->getTextureName();
        layer.blend_mode = parsed.blend_mode;
        layer.weight = weight;
      }

      // Both bindings of one layer read a single UV transform row in the
      // datablock, so the second unit to arrive must agree with the first.
      if (!transform_seen[parsed.layer_index]) {
        transform_seen[parsed.layer_index] = true;
        layer.scale = scale;
        layer.offset = offset;
      } else if (!NearlyEqual(layer.scale.x, scale.x) ||
                 !NearlyEqual(layer.scale.y, scale.y) ||
                 !NearlyEqual(layer.offset.x, offset.x) ||
                 !NearlyEqual(layer.offset.y, offset.y)) {
        refusal = MaterialDetailLayerRefusal::LAYER_TRANSFORM_DISAGREES;
        return false;
      }
    }

    if (candidate.declared_layer_count() == 0U) {
      refusal = MaterialDetailLayerRefusal::NO_LAYER_DECLARED;
      return false;
    }
    if (candidate.weight_mask_texture_name.empty()) {
      refusal = MaterialDetailLayerRefusal::WEIGHT_MASK_ABSENT;
      return false;
    }

    declaration = std::move(candidate);
    refusal = MaterialDetailLayerRefusal::NONE;
    return true;
  } catch (...) {
    refusal = MaterialDetailLayerRefusal::COMPANION_SHAPE_UNSUPPORTED;
    return false;
  }
}

} // namespace RoR::Gfx::Detail
