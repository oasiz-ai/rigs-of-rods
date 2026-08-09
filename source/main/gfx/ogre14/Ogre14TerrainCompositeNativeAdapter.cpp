/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"

#include <OgreBuildSettings.h>
#include <OgreException.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgreMaterial.h>
#include <OgrePass.h>
#include <OgrePixelFormat.h>
#include <OgreResource.h>
#include <OgreSceneManager.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureUnitState.h>
#include <Terrain/OgreTerrain.h>
#include <Terrain/OgreTerrainGroup.h>

#include <algorithm>
#include <limits>
#include <new>
#include <utility>
#include <vector>

static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                  OGRE_VERSION_PATCH == 2,
              "terrain composite capture is pinned to OGRE 14.5.2");
static_assert(sizeof(Ogre::Real) == sizeof(float),
              "terrain composite facts require binary32 Ogre::Real");

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

ValidationResult ConvertAlignment(Ogre::Terrain::Alignment alignment,
                                  Ogre14TerrainCompositeAlignment &output) {
  switch (alignment) {
  case Ogre::Terrain::ALIGN_X_Z:
    output = Ogre14TerrainCompositeAlignment::X_Z;
    return ValidationResult::Success();
  case Ogre::Terrain::ALIGN_X_Y:
    output = Ogre14TerrainCompositeAlignment::X_Y;
    return ValidationResult::Success();
  case Ogre::Terrain::ALIGN_Y_Z:
    output = Ogre14TerrainCompositeAlignment::Y_Z;
    return ValidationResult::Success();
  }
  return Failure(ValidationCode::INVALID_ENUM,
                 "terrain_composite.native.alignment",
                 "OGRE Terrain exposes an unknown alignment");
}

ValidationResult ConvertAddress(Ogre::TextureAddressingMode address,
                                Ogre14TerrainCompositeAddressMode &output) {
  if (address != Ogre::TAM_CLAMP) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.native.sampling_address",
                   "terrain composite texture unit is not clamped");
  }
  output = Ogre14TerrainCompositeAddressMode::CLAMP;
  return ValidationResult::Success();
}

ValidationResult ConvertFilter(Ogre::FilterOptions filter,
                               Ogre14TerrainCompositeFilter &output) {
  switch (filter) {
  case Ogre::FO_POINT:
    output = Ogre14TerrainCompositeFilter::POINT;
    return ValidationResult::Success();
  case Ogre::FO_LINEAR:
    output = Ogre14TerrainCompositeFilter::LINEAR;
    return ValidationResult::Success();
  default:
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.native.sampling_filter",
                   "terrain composite sampler exposes an unsupported filter");
  }
}

ValidationResult ConvertFog(Ogre::FogMode mode,
                            Ogre14TerrainCompositeSceneFogMode &output) {
  switch (mode) {
  case Ogre::FOG_NONE:
    output = Ogre14TerrainCompositeSceneFogMode::FOG_NONE;
    return ValidationResult::Success();
  case Ogre::FOG_EXP:
    output = Ogre14TerrainCompositeSceneFogMode::FOG_EXP;
    return ValidationResult::Success();
  case Ogre::FOG_EXP2:
    output = Ogre14TerrainCompositeSceneFogMode::FOG_EXP2;
    return ValidationResult::Success();
  case Ogre::FOG_LINEAR:
    output = Ogre14TerrainCompositeSceneFogMode::FOG_LINEAR;
    return ValidationResult::Success();
  }
  return Failure(ValidationCode::INVALID_ENUM,
                 "terrain_composite.native.scene_fog_mode",
                 "SceneManager exposes an unknown direct fog mode");
}

ValidationResult NarrowStateCount(std::size_t native,
                                  std::uint64_t &portable) {
  portable = static_cast<std::uint64_t>(native);
  if (static_cast<std::size_t>(portable) != native || portable == 0U ||
      portable == (std::numeric_limits<std::uint64_t>::max)()) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.resource_revision",
                   "composite texture has no stable nonzero content revision");
  }
  return ValidationResult::Success();
}

ValidationResult NarrowHandle(Ogre::ResourceHandle native,
                              std::uint64_t &portable, const char *field) {
  portable = static_cast<std::uint64_t>(native);
  if (native == 0U || static_cast<Ogre::ResourceHandle>(portable) != native) {
    return Failure(ValidationCode::INVALID_HANDLE, field,
                   "OGRE resource handle is zero or exceeds uint64");
  }
  return ValidationResult::Success();
}

ValidationResult NarrowCount(std::size_t native, std::uint32_t &portable,
                             const char *field) {
  if (native > (std::numeric_limits<std::uint32_t>::max)()) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, field,
                   "OGRE collection count exceeds uint32");
  }
  portable = static_cast<std::uint32_t>(native);
  return ValidationResult::Success();
}

bool CheckedAddU64(std::uint64_t lhs, std::uint64_t rhs,
                   std::uint64_t &result) noexcept {
  if (rhs > (std::numeric_limits<std::uint64_t>::max)() - lhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

bool CheckedMultiplyU64(std::uint64_t lhs, std::uint64_t rhs,
                        std::uint64_t &result) noexcept {
  if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

std::uint32_t FullMipLevelCount(std::uint32_t width,
                                std::uint32_t height) noexcept {
  std::uint32_t count = 0U;
  while (width != 0U && height != 0U) {
    ++count;
    if (width == 1U && height == 1U) {
      break;
    }
    width = (std::max)(1U, width / 2U);
    height = (std::max)(1U, height / 2U);
  }
  return count;
}

ValidationResult PreflightTextureEnvelope(
    const Ogre::Texture &texture,
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    std::uint32_t &width, std::uint32_t &height,
    std::uint32_t &additional_mip_levels, std::uint32_t &total_mip_levels) {
  const std::size_t native_width = texture.getWidth();
  const std::size_t native_height = texture.getHeight();
  if (native_width == 0U || native_height == 0U ||
      native_width > (std::numeric_limits<std::uint32_t>::max)() ||
      native_height > (std::numeric_limits<std::uint32_t>::max)() ||
      native_width > configuration.maximum_dimension ||
      native_height > configuration.maximum_dimension) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "terrain_composite.native.texture_dimensions",
                   "native texture dimensions are zero, truncated, or exceed "
                   "the configured cap");
  }
  width = static_cast<std::uint32_t>(native_width);
  height = static_cast<std::uint32_t>(native_height);

  // Pinned OGRE getNumMipmaps() reports levels additional to level zero.
  const std::size_t native_additional = texture.getNumMipmaps();
  if (native_additional >= (std::numeric_limits<std::uint32_t>::max)() ||
      native_additional + 1U > configuration.maximum_mip_levels) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "terrain_composite.native.additional_mip_count",
                   "OGRE additional mip count is truncated or exceeds the "
                   "configured total-level cap");
  }
  additional_mip_levels = static_cast<std::uint32_t>(native_additional);
  total_mip_levels = additional_mip_levels + 1U;
  if (total_mip_levels != FullMipLevelCount(width, height)) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "terrain_composite.native.full_mip_chain",
                   "OGRE texture does not expose the exact complete "
                   "zero-through-1x1 mip chain");
  }

  std::uint64_t aggregate = 0U;
  std::uint32_t mip_width = width;
  std::uint32_t mip_height = height;
  for (std::uint32_t level = 0U; level < total_mip_levels; ++level) {
    std::uint64_t row_bytes = 0U;
    std::uint64_t slice_bytes = 0U;
    if (!CheckedMultiplyU64(mip_width, 4U, row_bytes) ||
        !CheckedMultiplyU64(row_bytes, mip_height, slice_bytes) ||
        !CheckedAddU64(aggregate, slice_bytes, aggregate) ||
        aggregate > configuration.maximum_rgba_bytes) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "terrain_composite.native.full_mip_cap",
                     "native full mip-chain RGBA layout overflows or exceeds "
                     "the configured aggregate cap");
    }
    mip_width = (std::max)(1U, mip_width / 2U);
    mip_height = (std::max)(1U, mip_height / 2U);
  }
  return ValidationResult::Success();
}

ValidationResult FindExactSlot(Ogre::TerrainGroup &group, std::int32_t slot_x,
                               std::int32_t slot_y, std::uint32_t &packed_key,
                               Ogre::TerrainGroup::TerrainSlot *&slot,
                               Ogre::Terrain *&terrain) {
  if (slot_x < -32768 || slot_x > 32767 || slot_y < -32768 ||
      slot_y > 32767) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "terrain_composite.native.slot_coordinates",
                   "TerrainGroup slot coordinates exceed its signed 16-bit "
                   "identity range");
  }
  packed_key =
      group.packIndex(static_cast<long>(slot_x), static_cast<long>(slot_y));
  const Ogre::TerrainGroup::TerrainSlotMap &slots = group.getTerrainSlots();
  const auto found = slots.find(packed_key);
  if (found == slots.end() || found->second == nullptr) {
    return Failure(
        ValidationCode::MISSING_REFERENCE, "terrain_composite.native.slot",
        "TerrainGroup has no exact slot at the requested coordinates");
  }
  slot = found->second;
  terrain =
      group.getTerrain(static_cast<long>(slot_x), static_cast<long>(slot_y));
  if (slot->x != static_cast<long>(slot_x) ||
      slot->y != static_cast<long>(slot_y) || slot->instance == nullptr ||
      slot->instance != terrain ||
      group.packIndex(slot->x, slot->y) != packed_key) {
    return Failure(
        ValidationCode::REVISION_MISMATCH,
        "terrain_composite.native.slot_identity",
        "TerrainGroup packed slot, coordinates, and Terrain pointer disagree");
  }
  return ValidationResult::Success();
}

ValidationResult CaptureSamplingObservation(
    Ogre::Terrain &terrain, Ogre::Texture &texture,
    Ogre14TerrainCompositeSamplingObservation &observation) {
  const Ogre::MaterialPtr &material = terrain.getMaterial();
  if (!material || material->getNumTechniques() <= 1U) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "terrain_composite.native.sampling_material",
                   "terrain material has no technique-one sampling container");
  }
  Ogre::Technique *const technique = material->getTechnique(1U);
  if (technique == nullptr || technique->getParent() != material.get() ||
      technique->getLodIndex() != 1U || technique->getNumPasses() == 0U) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.sampling_technique",
                   "technique index one is not the LOD-one texture-unit "
                   "container");
  }
  Ogre::Pass *const pass = technique->getPass(0U);
  if (pass == nullptr || pass->getParent() != technique ||
      pass->getIndex() != 0U || pass->getNumTextureUnitStates() == 0U) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.sampling_pass_container",
                   "pass zero has no texture-unit-zero binding container");
  }
  Ogre::TextureUnitState *const unit = pass->getTextureUnitState(0U);
  if (unit == nullptr || unit->getParent() != pass) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.sampling_texture_unit",
                   "texture-unit-zero identity or parent changed");
  }
  const Ogre::TexturePtr &bound_texture = unit->_getTexturePtr();
  const Ogre::SamplerPtr &sampler = unit->getSampler();
  Ogre::SceneManager *const scene_manager = terrain.getSceneManager();
  if (!bound_texture || bound_texture.get() != &texture || !sampler ||
      scene_manager == nullptr) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.sampling_binding",
                   "texture-unit-zero is not pointer-bound to the exact "
                   "composite texture, sampler, and scene");
  }

  Ogre14TerrainCompositeSamplingObservation candidate;
  candidate.scene_manager_pointer_token =
      reinterpret_cast<std::uintptr_t>(scene_manager);
  candidate.texture_unit_pointer_token =
      reinterpret_cast<std::uintptr_t>(unit);
  candidate.sampler_pointer_token =
      reinterpret_cast<std::uintptr_t>(sampler.get());
  candidate.bound_texture_pointer_token =
      reinterpret_cast<std::uintptr_t>(bound_texture.get());
  candidate.texture_unit_content_named =
      unit->getContentType() == Ogre::TextureUnitState::CONTENT_NAMED;
  ValidationResult validation =
      NarrowCount(unit->getNumFrames(), candidate.texture_unit_frame_count,
                  "terrain_composite.native.texture_unit_frame_count");
  if (!validation) {
    return validation;
  }
  validation = NarrowCount(unit->getCurrentFrame(),
                           candidate.texture_unit_current_frame,
                           "terrain_composite.native.texture_unit_frame");
  if (!validation) {
    return validation;
  }
  candidate.texture_unit_texture_2d =
      unit->getTextureType() == Ogre::TEX_TYPE_2D;
  candidate.texture_unit_is_blank = unit->isBlank();
  candidate.texture_unit_load_failing = unit->isTextureLoadFailing();
  candidate.unordered_access_mip_level = unit->getUnorderedAccessMipLevel();

  // Widen through the checked path before storing; a future wider OGRE return
  // cannot wrap 256 to the admitted UV set zero.
  validation =
      NarrowCount(static_cast<std::size_t>(unit->getTextureCoordSet()),
                  candidate.texture_coord_set,
                  "terrain_composite.native.texture_coord_set");
  if (!validation) {
    return validation;
  }
  candidate.texcoord_calculation_none =
      unit->_deriveTexCoordCalcMethod() == Ogre::TEXCALC_NONE;
  validation =
      NarrowCount(unit->getEffects().size(), candidate.texture_effect_count,
                  "terrain_composite.native.texture_effect_count");
  if (!validation) {
    return validation;
  }
  candidate.texture_u_scroll = unit->getTextureUScroll();
  candidate.texture_v_scroll = unit->getTextureVScroll();
  candidate.texture_u_scale = unit->getTextureUScale();
  candidate.texture_v_scale = unit->getTextureVScale();
  candidate.texture_rotation_radians =
      static_cast<float>(unit->getTextureRotate().valueRadians());
  const Ogre::Matrix4 &transform = unit->getTextureTransform();
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      candidate.texture_transform[row * 4U + column] =
          static_cast<float>(transform[row][column]);
    }
  }

  const Ogre::Sampler::UVWAddressingMode &address =
      sampler->getAddressingMode();
  validation = ConvertAddress(address.u, candidate.address_u);
  if (!validation) {
    return validation;
  }
  validation = ConvertAddress(address.v, candidate.address_v);
  if (!validation) {
    return validation;
  }
  validation = ConvertAddress(address.w, candidate.address_w);
  if (!validation) {
    return validation;
  }
  validation =
      ConvertFilter(sampler->getFiltering(Ogre::FT_MIN), candidate.min_filter);
  if (!validation) {
    return validation;
  }
  validation =
      ConvertFilter(sampler->getFiltering(Ogre::FT_MAG), candidate.mag_filter);
  if (!validation) {
    return validation;
  }
  validation =
      ConvertFilter(sampler->getFiltering(Ogre::FT_MIP), candidate.mip_filter);
  if (!validation) {
    return validation;
  }
  candidate.maximum_anisotropy = sampler->getAnisotropy();
  candidate.mipmap_bias = sampler->getMipmapBias();
  candidate.compare_enabled = sampler->getCompareEnabled();
  if (sampler->getCompareFunction() != Ogre::CMPF_GREATER_EQUAL) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.native.sampler_compare_function",
                   "pinned terrain sampler compare function is not the exact "
                   "GREATER_EQUAL default");
  }
  candidate.compare_function =
      Ogre14TerrainCompositeCompareFunction::GREATER_EQUAL;
  const Ogre::ColourValue &border = sampler->getBorderColour();
  candidate.border_colour = {
      static_cast<float>(border.r), static_cast<float>(border.g),
      static_cast<float>(border.b), static_cast<float>(border.a)};
  candidate.texture_unit_hardware_gamma_enabled =
      unit->isHardwareGammaEnabled();
  validation = ConvertFog(scene_manager->getFogMode(),
                          candidate.scene_fog_mode);
  if (!validation) {
    return validation;
  }
  observation = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult CaptureObservation(
    Ogre::TerrainGroup &group, std::int32_t slot_x, std::int32_t slot_y,
    Ogre::Texture &texture,
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    const std::vector<Ogre::HardwarePixelBufferPtr> &pixel_buffers,
    Ogre14TerrainCompositeNativeObservation &observation) {
  std::uint32_t packed_key = 0U;
  Ogre::TerrainGroup::TerrainSlot *slot = nullptr;
  Ogre::Terrain *terrain = nullptr;
  ValidationResult validation =
      FindExactSlot(group, slot_x, slot_y, packed_key, slot, terrain);
  if (!validation) {
    return validation;
  }
  if (!terrain->isLoaded() || terrain->isDerivedDataUpdateInProgress()) {
    return Failure(
        ValidationCode::REVISION_MISMATCH,
        "terrain_composite.native.terrain_state",
        "Terrain must be loaded with no derived-data update in progress");
  }
  if (terrain->getAlignment() != group.getAlignment() ||
      terrain->getSize() != group.getTerrainSize() ||
      terrain->getWorldSize() != group.getTerrainWorldSize()) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.group_layout",
                   "Terrain page layout differs from its exact TerrainGroup");
  }
  const Ogre::TexturePtr &current_texture = terrain->getCompositeMap();
  if (!current_texture || current_texture.get() != &texture) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.texture_identity",
                   "Terrain composite Texture pointer changed during capture");
  }
  if (texture.getLoadingState() != Ogre::Resource::LOADSTATE_LOADED ||
      !texture.isLoaded() || !texture.isManuallyLoaded() ||
      texture.getTextureType() != Ogre::TEX_TYPE_2D ||
      texture.getFormat() != Ogre::PF_BYTE_RGBA || texture.getDepth() != 1U ||
      texture.getNumFaces() != 1U) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "terrain_composite.native.texture_state",
        "Terrain composite is not a loaded manual 2D PF_BYTE_RGBA texture");
  }

  std::uint32_t texture_width = 0U;
  std::uint32_t texture_height = 0U;
  std::uint32_t additional_mip_levels = 0U;
  std::uint32_t total_mip_levels = 0U;
  validation = PreflightTextureEnvelope(texture, configuration, texture_width,
                                        texture_height, additional_mip_levels,
                                        total_mip_levels);
  if (!validation) {
    return validation;
  }
  if (pixel_buffers.size() != total_mip_levels) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "terrain_composite.native.full_mip_chain",
                   "caller did not retain the exact contiguous zero-through-N "
                   "OGRE mip buffers");
  }
  if (texture.getHandle() == 0U || texture.getUsage() < 0) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "terrain_composite.native.texture_resource",
                   "composite texture handle or usage is invalid");
  }

  Ogre14TerrainCompositeNativeObservation candidate;
  candidate.terrain_group_pointer_token =
      reinterpret_cast<std::uintptr_t>(&group);
  candidate.terrain_slot_pointer_token = reinterpret_cast<std::uintptr_t>(slot);
  candidate.terrain_pointer_token = reinterpret_cast<std::uintptr_t>(terrain);
  candidate.packed_slot_key = packed_key;
  candidate.slot_x = slot_x;
  candidate.slot_y = slot_y;
  candidate.exact_terrain_resource_group = group.getResourceGroup();
  candidate.exact_filename_prefix = group.getFilenamePrefix();
  candidate.exact_filename_extension = group.getFilenameExtension();
  if (!slot->def.filename.empty() && slot->def.importData == nullptr) {
    candidate.page_definition_kind =
        Ogre14TerrainCompositePageDefinitionKind::FILE_BACKED;
    candidate.exact_definition_filename = slot->def.filename;
  } else if (slot->def.filename.empty() && slot->def.importData != nullptr) {
    candidate.page_definition_kind =
        Ogre14TerrainCompositePageDefinitionKind::LIVE_IMPORT;
    candidate.definition_import_data_pointer_token =
        reinterpret_cast<std::uintptr_t>(slot->def.importData);
  } else if (slot->def.filename.empty() && slot->def.importData == nullptr) {
    candidate.page_definition_kind =
        Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME;
  } else {
    return Failure(
        ValidationCode::INVALID_ASSET_REFERENCE,
        "terrain_composite.native.page_definition",
        "Terrain slot exposes conflicting filename and ImportData definitions");
  }
  candidate.generated_save_filename = group.generateFilename(
      static_cast<long>(slot_x), static_cast<long>(slot_y));
  candidate.exact_terrain_material_name = terrain->getMaterialName();
  validation =
      ConvertAlignment(terrain->getAlignment(), candidate.terrain_alignment);
  if (!validation) {
    return validation;
  }
  candidate.terrain_size = terrain->getSize();
  candidate.terrain_world_size = static_cast<float>(terrain->getWorldSize());
  const Ogre::Vector3 &position = terrain->getPosition();
  candidate.terrain_world_position = {static_cast<float>(position.x),
                                      static_cast<float>(position.y),
                                      static_cast<float>(position.z)};
  candidate.terrain_is_loaded = terrain->isLoaded();
  candidate.terrain_derived_data_update_in_progress =
      terrain->isDerivedDataUpdateInProgress();

  candidate.texture_pointer_token = reinterpret_cast<std::uintptr_t>(&texture);
  validation = NarrowHandle(texture.getHandle(), candidate.texture_handle,
                            "terrain_composite.native.texture_handle");
  if (!validation) {
    return validation;
  }
  candidate.exact_texture_resource_group = texture.getGroup();
  candidate.exact_texture_name = texture.getName();
  candidate.texture_type = Ogre14TerrainCompositeTextureType::TEXTURE_2D;
  candidate.texture_loading_state =
      Ogre14TerrainCompositeTextureLoadingState::LOADED;
  candidate.texture_width = texture_width;
  candidate.texture_height = texture_height;
  candidate.texture_depth = static_cast<std::uint32_t>(texture.getDepth());
  candidate.texture_face_count =
      static_cast<std::uint32_t>(texture.getNumFaces());
  candidate.texture_additional_mip_count = additional_mip_levels;
  candidate.texture_mip_count = total_mip_levels;
  candidate.texture_usage = static_cast<std::uint32_t>(texture.getUsage());
  candidate.texture_is_loaded = texture.isLoaded();
  candidate.texture_is_manual = texture.isManuallyLoaded();
  candidate.texture_hardware_gamma_enabled = texture.isHardwareGammaEnabled();
  validation = NarrowStateCount(texture.getStateCount(),
                                candidate.texture_resource_revision);
  if (!validation) {
    return validation;
  }

  std::uint32_t expected_width = texture_width;
  std::uint32_t expected_height = texture_height;
  candidate.mip_chain.reserve(total_mip_levels);
  for (std::uint32_t mip_level = 0U; mip_level < total_mip_levels;
       ++mip_level) {
    const Ogre::HardwarePixelBufferPtr &buffer = pixel_buffers[mip_level];
    const Ogre::HardwarePixelBufferPtr &current_buffer =
        texture.getBuffer(0U, mip_level);
    if (!buffer || !current_buffer || current_buffer.get() != buffer.get() ||
        buffer->getFormat() != Ogre::PF_BYTE_RGBA ||
        buffer->getWidth() != expected_width ||
        buffer->getHeight() != expected_height || buffer->getDepth() != 1U) {
      return Failure(
          ValidationCode::REVISION_MISMATCH,
          "terrain_composite.native.mip_buffer",
          "contiguous mip buffer identity, format, extent, or layout changed");
    }
    Ogre14TerrainCompositeNativeMipObservation mip;
    mip.mip_level = mip_level;
    mip.pixel_buffer_pointer_token =
        reinterpret_cast<std::uintptr_t>(buffer.get());
    mip.width = expected_width;
    mip.height = expected_height;
    mip.depth = 1U;
    mip.tight_row_pitch_bytes = static_cast<std::uint64_t>(expected_width) * 4U;
    mip.tight_slice_pitch_bytes = mip.tight_row_pitch_bytes * expected_height;
    candidate.mip_chain.push_back(mip);
    expected_width = (std::max)(1U, expected_width / 2U);
    expected_height = (std::max)(1U, expected_height / 2U);
  }
  candidate.pixel_buffer_pointer_token =
      candidate.mip_chain.front().pixel_buffer_pointer_token;
  candidate.tight_row_pitch_bytes =
      candidate.mip_chain.front().tight_row_pitch_bytes;
  candidate.tight_slice_pitch_bytes =
      candidate.mip_chain.front().tight_slice_pitch_bytes;

  validation = CaptureSamplingObservation(*terrain, texture,
                                          candidate.sampling);
  if (!validation) {
    return validation;
  }
  observation = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult AcquireMipBuffers(
    Ogre::Texture &texture,
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    std::vector<Ogre::HardwarePixelBufferPtr> &buffers) {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t additional = 0U;
  std::uint32_t total = 0U;
  ValidationResult validation = PreflightTextureEnvelope(
      texture, configuration, width, height, additional, total);
  if (!validation) {
    return validation;
  }
  (void)width;
  (void)height;
  (void)additional;
  std::vector<Ogre::HardwarePixelBufferPtr> candidate;
  candidate.reserve(total);
  for (std::uint32_t level = 0U; level < total; ++level) {
    Ogre::HardwarePixelBufferPtr buffer = texture.getBuffer(0U, level);
    if (!buffer) {
      return Failure(
          ValidationCode::MISSING_REFERENCE,
          "terrain_composite.native.mip_buffer",
          "Terrain composite texture is missing a contiguous mip buffer");
    }
    candidate.push_back(std::move(buffer));
  }
  buffers = std::move(candidate);
  return ValidationResult::Success();
}

} // namespace

ValidationResult Ogre14TerrainCompositeNativeAdapter::Capture(
    Ogre::TerrainGroup &terrain_group, std::int32_t slot_x,
    std::int32_t slot_y,
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    Ogre14TerrainCompositeCaptureReceipt &receipt) {
  try {
    ValidationResult validation = ValidateCaptureConfiguration(configuration);
    if (!validation) {
      return validation;
    }
    std::uint32_t packed_key = 0U;
    Ogre::TerrainGroup::TerrainSlot *slot = nullptr;
    Ogre::Terrain *terrain = nullptr;
    validation =
        FindExactSlot(terrain_group, slot_x, slot_y, packed_key, slot, terrain);
    if (!validation) {
      return validation;
    }
    if (!terrain->isLoaded() || terrain->isDerivedDataUpdateInProgress()) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "terrain_composite.native.terrain_state",
                     "Terrain must be stable before composite-map flush");
    }

    terrain->updateCompositeMap();
    const Ogre::TexturePtr texture = terrain->getCompositeMap();
    if (!texture) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.native.texture",
                     "Terrain composite update produced no composite texture");
    }
    std::vector<Ogre::HardwarePixelBufferPtr> pixel_buffers;
    validation = AcquireMipBuffers(*texture, configuration, pixel_buffers);
    if (!validation) {
      return validation;
    }
    // Retain only container lifetime. No Pass or shader response is admitted by
    // this transport receipt.
    const Ogre::MaterialPtr material = terrain->getMaterial();
    if (!material) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.native.material_container",
                     "Terrain produced no texture-unit binding container");
    }

    Ogre14TerrainCompositeNativeObservation before_readback;
    validation =
        CaptureObservation(terrain_group, slot_x, slot_y, *texture,
                           configuration, pixel_buffers, before_readback);
    if (!validation) {
      return validation;
    }
    const Ogre::MaterialPtr before_material = terrain->getMaterial();
    if (!before_material || before_material.get() != material.get()) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "terrain_composite.native.material_container_identity",
                     "Terrain texture-unit container changed during authority "
                     "capture");
    }
    validation = ValidateCaptureInputs(configuration, before_readback);
    if (!validation) {
      return validation;
    }
    std::vector<std::vector<std::uint8_t>> mip_rgba_bytes;
    mip_rgba_bytes.reserve(before_readback.mip_chain.size());
    for (const Ogre14TerrainCompositeNativeMipObservation &mip :
         before_readback.mip_chain) {
      if (mip.tight_slice_pitch_bytes >
          static_cast<std::uint64_t>(
              (std::numeric_limits<std::size_t>::max)())) {
        return Failure(
            ValidationCode::SIZE_MISMATCH,
            "terrain_composite.native.allocation",
            "tight RGBA mip readback exceeds the host address range");
      }
      mip_rgba_bytes.emplace_back(
          static_cast<std::size_t>(mip.tight_slice_pitch_bytes));
    }
    for (std::size_t index = 0U; index < pixel_buffers.size(); ++index) {
      const Ogre14TerrainCompositeNativeMipObservation &mip =
          before_readback.mip_chain[index];
      Ogre::PixelBox destination(mip.width, mip.height, 1U, Ogre::PF_BYTE_RGBA,
                                 mip_rgba_bytes[index].data());
      if (destination.rowPitch != mip.width ||
          destination.slicePitch !=
              static_cast<std::size_t>(mip.width) * mip.height) {
        return Failure(
            ValidationCode::SIZE_MISMATCH, "terrain_composite.native.pixel_box",
            "OGRE PixelBox did not preserve a requested tight mip layout");
      }
      pixel_buffers[index]->blitToMemory(destination);
    }

    Ogre::Terrain *const current_terrain = terrain_group.getTerrain(
        static_cast<long>(slot_x), static_cast<long>(slot_y));
    if (current_terrain == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.native.final_terrain",
                     "TerrainGroup slot disappeared during readback");
    }
    const Ogre::MaterialPtr current_material = current_terrain->getMaterial();
    const Ogre::TexturePtr current_texture = current_terrain->getCompositeMap();
    if (!current_material || current_material.get() != material.get() ||
        !current_texture || current_texture.get() != texture.get()) {
      return Failure(
          ValidationCode::REVISION_MISMATCH,
          "terrain_composite.native.final_container_texture",
          "Terrain texture-unit container or composite texture changed during "
          "readback");
    }
    std::vector<Ogre::HardwarePixelBufferPtr> current_buffers;
    validation =
        AcquireMipBuffers(*current_texture, configuration, current_buffers);
    if (!validation) {
      return validation;
    }
    Ogre14TerrainCompositeNativeObservation after_readback;
    validation =
        CaptureObservation(terrain_group, slot_x, slot_y, *current_texture,
                           configuration, current_buffers, after_readback);
    if (!validation) {
      return validation;
    }
    return PublishOwnedReadback(configuration, before_readback,
                                std::move(mip_rgba_bytes), after_readback,
                                receipt);
  } catch (const Ogre::Exception &) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "terrain_composite.capture.ogre_exception",
                   "OGRE failed before terrain composite receipt publication");
  } catch (const std::bad_alloc &) {
    return Failure(
        ValidationCode::EMPTY_PAYLOAD, "terrain_composite.capture.allocation",
        "allocation failed before terrain composite receipt publication");
  } catch (...) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "terrain_composite.capture.unexpected_exception",
        "unexpected exception before terrain composite receipt publication");
  }
}

} // namespace RoR::Render
