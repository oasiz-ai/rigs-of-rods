/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"

#include <OgreBuildSettings.h>
#include <OgreException.h>
#include <OgreHardwarePixelBuffer.h>
#include <OgrePixelFormat.h>
#include <OgreResource.h>
#include <OgreTexture.h>
#include <Terrain/OgreTerrain.h>
#include <Terrain/OgreTerrainGroup.h>

#include <limits>
#include <new>
#include <utility>
#include <vector>

static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                  OGRE_VERSION_PATCH == 2,
              "terrain composite capture is pinned to OGRE 14.5.2");
static_assert(sizeof(Ogre::Real) == sizeof(float),
              "terrain composite page identity requires binary32 Ogre::Real");

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

void MaybeInject(Ogre14TerrainCompositeCaptureStage stage,
                 IOgre14TerrainCompositeCaptureFaultInjector *injector) {
  if (injector != nullptr) {
    injector->BeforeTerrainCompositeCaptureStage(stage);
  }
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

ValidationResult FindExactSlot(Ogre::TerrainGroup &group,
                               std::int32_t slot_x, std::int32_t slot_y,
                               std::uint32_t &packed_key,
                               Ogre::TerrainGroup::TerrainSlot *&slot,
                               Ogre::Terrain *&terrain) {
  // TerrainGroup::packIndex narrows both coordinates to signed 16-bit. Reject
  // aliases before calling it so an out-of-range request cannot authenticate a
  // different in-range page with the same packed key.
  if (slot_x < -32768 || slot_x > 32767 || slot_y < -32768 ||
      slot_y > 32767) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "terrain_composite.native.slot_coordinates",
                   "TerrainGroup slot coordinates exceed its signed 16-bit identity range");
  }
  packed_key = group.packIndex(static_cast<long>(slot_x),
                               static_cast<long>(slot_y));
  const Ogre::TerrainGroup::TerrainSlotMap &slots = group.getTerrainSlots();
  const auto found = slots.find(packed_key);
  if (found == slots.end() || found->second == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "terrain_composite.native.slot",
                   "TerrainGroup has no exact slot at the requested coordinates");
  }
  slot = found->second;
  terrain = group.getTerrain(static_cast<long>(slot_x),
                             static_cast<long>(slot_y));
  if (slot->x != static_cast<long>(slot_x) ||
      slot->y != static_cast<long>(slot_y) || slot->instance == nullptr ||
      slot->instance != terrain ||
      group.packIndex(slot->x, slot->y) != packed_key) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.slot_identity",
                   "TerrainGroup packed slot, coordinates, and Terrain pointer disagree");
  }
  return ValidationResult::Success();
}

ValidationResult CaptureObservation(
    Ogre::TerrainGroup &group, std::int32_t slot_x, std::int32_t slot_y,
    Ogre::Texture &texture, Ogre::HardwarePixelBuffer &pixel_buffer,
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
    return Failure(ValidationCode::REVISION_MISMATCH,
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
      texture.getNumFaces() != 1U ||
      texture.getNumMipmaps() ==
          (std::numeric_limits<std::uint32_t>::max)()) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.native.texture_state",
                   "Terrain composite is not a loaded manual 2D PF_BYTE_RGBA texture");
  }
  const Ogre::HardwarePixelBufferPtr &current_buffer =
      texture.getBuffer(0U, 0U);
  if (!current_buffer || current_buffer.get() != &pixel_buffer ||
      pixel_buffer.getFormat() != Ogre::PF_BYTE_RGBA ||
      pixel_buffer.getWidth() != texture.getWidth() ||
      pixel_buffer.getHeight() != texture.getHeight() ||
      pixel_buffer.getDepth() != 1U) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "terrain_composite.native.pixel_buffer",
                   "level-zero PF_BYTE_RGBA pixel buffer identity or extent changed");
  }
  if (texture.getHandle() == 0U || texture.getUsage() < 0) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "terrain_composite.native.texture_resource",
                   "composite texture handle or usage is invalid");
  }

  const std::uint64_t width = texture.getWidth();
  const std::uint64_t height = texture.getHeight();
  if (width > (std::numeric_limits<std::uint64_t>::max)() / 4U ||
      (width * 4U != 0U &&
       height > (std::numeric_limits<std::uint64_t>::max)() /
                    (width * 4U))) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "terrain_composite.native.tight_layout",
                   "native PF_BYTE_RGBA extent overflows uint64");
  }

  Ogre14TerrainCompositeNativeObservation candidate;
  candidate.terrain_group_pointer_token =
      reinterpret_cast<std::uintptr_t>(&group);
  candidate.terrain_slot_pointer_token =
      reinterpret_cast<std::uintptr_t>(slot);
  candidate.terrain_pointer_token =
      reinterpret_cast<std::uintptr_t>(terrain);
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
    // OGRE frees ImportData after a successful prepare. The historical import
    // source is then unavailable; do not mislabel the generated save name as
    // the loaded page source.
    candidate.page_definition_kind =
        Ogre14TerrainCompositePageDefinitionKind::CONSUMED_OR_RUNTIME;
  } else {
    return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                   "terrain_composite.native.page_definition",
                   "Terrain slot exposes conflicting filename and ImportData definitions");
  }
  candidate.generated_save_filename = group.generateFilename(
      static_cast<long>(slot_x), static_cast<long>(slot_y));
  candidate.exact_terrain_material_name = terrain->getMaterialName();
  validation = ConvertAlignment(terrain->getAlignment(),
                                candidate.terrain_alignment);
  if (!validation) {
    return validation;
  }
  candidate.terrain_size = terrain->getSize();
  candidate.terrain_world_size =
      static_cast<float>(terrain->getWorldSize());
  const Ogre::Vector3 &position = terrain->getPosition();
  candidate.terrain_world_position = {
      static_cast<float>(position.x), static_cast<float>(position.y),
      static_cast<float>(position.z)};
  candidate.terrain_is_loaded = terrain->isLoaded();
  candidate.terrain_derived_data_update_in_progress =
      terrain->isDerivedDataUpdateInProgress();

  candidate.texture_pointer_token =
      reinterpret_cast<std::uintptr_t>(&texture);
  candidate.pixel_buffer_pointer_token =
      reinterpret_cast<std::uintptr_t>(&pixel_buffer);
  candidate.texture_handle =
      static_cast<std::uint64_t>(texture.getHandle());
  if (static_cast<Ogre::ResourceHandle>(candidate.texture_handle) !=
      texture.getHandle()) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "terrain_composite.native.texture_handle",
                   "OGRE texture handle exceeds the portable uint64 range");
  }
  candidate.exact_texture_resource_group = texture.getGroup();
  candidate.exact_texture_name = texture.getName();
  candidate.texture_type = Ogre14TerrainCompositeTextureType::TEXTURE_2D;
  candidate.texture_loading_state =
      Ogre14TerrainCompositeTextureLoadingState::LOADED;
  candidate.texture_width = texture.getWidth();
  candidate.texture_height = texture.getHeight();
  candidate.texture_depth = texture.getDepth();
  candidate.texture_face_count = texture.getNumFaces();
  candidate.texture_mip_count = texture.getNumMipmaps() + 1U;
  candidate.texture_usage = static_cast<std::uint32_t>(texture.getUsage());
  candidate.texture_is_loaded = texture.isLoaded();
  candidate.texture_is_manual = texture.isManuallyLoaded();
  candidate.texture_hardware_gamma_enabled =
      texture.isHardwareGammaEnabled();
  validation = NarrowStateCount(texture.getStateCount(),
                                candidate.texture_resource_revision);
  if (!validation) {
    return validation;
  }
  candidate.tight_row_pitch_bytes = width * 4U;
  candidate.tight_slice_pitch_bytes = width * 4U * height;
  observation = std::move(candidate);
  return ValidationResult::Success();
}

} // namespace

ValidationResult Ogre14TerrainCompositeNativeAdapter::Capture(
    Ogre::TerrainGroup &terrain_group, std::int32_t slot_x,
    std::int32_t slot_y,
    const Ogre14TerrainCompositeCaptureConfiguration &configuration,
    Ogre14TerrainCompositeCaptureReceipt &receipt,
    IOgre14TerrainCompositeCaptureFaultInjector *fault_injector) {
  try {
    std::uint32_t packed_key = 0U;
    Ogre::TerrainGroup::TerrainSlot *slot = nullptr;
    Ogre::Terrain *terrain = nullptr;
    ValidationResult validation = FindExactSlot(
        terrain_group, slot_x, slot_y, packed_key, slot, terrain);
    if (!validation) {
      return validation;
    }
    if (!terrain->isLoaded() || terrain->isDerivedDataUpdateInProgress()) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "terrain_composite.native.terrain_state",
                     "Terrain must be stable before composite-map flush");
    }

    // The pinned 14.5.2 recipe publishes _dirtyState() only after this exact
    // synchronous RTT succeeds. No delayed or background update is accepted.
    terrain->updateCompositeMap();

    const Ogre::TexturePtr texture = terrain->getCompositeMap();
    if (!texture) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.native.texture",
                     "Terrain composite update produced no composite texture");
    }
    const Ogre::HardwarePixelBufferPtr pixel_buffer =
        texture->getBuffer(0U, 0U);
    if (!pixel_buffer) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.native.pixel_buffer",
                     "Terrain composite texture has no level-zero pixel buffer");
    }

    Ogre14TerrainCompositeNativeObservation before_readback;
    validation = CaptureObservation(terrain_group, slot_x, slot_y, *texture,
                                    *pixel_buffer, before_readback);
    if (!validation) {
      return validation;
    }
    validation = ValidateCaptureInputs(configuration, before_readback);
    if (!validation) {
      return validation;
    }
    MaybeInject(
        Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_IDENTITY_CAPTURE,
        fault_injector);

    if (before_readback.tight_slice_pitch_bytes >
        static_cast<std::uint64_t>(
            (std::numeric_limits<std::size_t>::max)())) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "terrain_composite.native.allocation",
                     "tight RGBA readback exceeds the host address range");
    }
    std::vector<std::uint8_t> rgba_bytes(
        static_cast<std::size_t>(
            before_readback.tight_slice_pitch_bytes));
    MaybeInject(Ogre14TerrainCompositeCaptureStage::AFTER_RGBA_ALLOCATION,
                fault_injector);

    Ogre::PixelBox destination(
        before_readback.texture_width, before_readback.texture_height, 1U,
        Ogre::PF_BYTE_RGBA, rgba_bytes.data());
    if (destination.rowPitch != before_readback.texture_width ||
        destination.slicePitch !=
            static_cast<std::size_t>(before_readback.texture_width) *
                before_readback.texture_height) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "terrain_composite.native.pixel_box",
                     "OGRE PixelBox did not preserve the requested tight layout");
    }
    pixel_buffer->blitToMemory(destination);
    MaybeInject(Ogre14TerrainCompositeCaptureStage::AFTER_NATIVE_READBACK,
                fault_injector);

    Ogre::Terrain *const current_terrain = terrain_group.getTerrain(
        static_cast<long>(slot_x), static_cast<long>(slot_y));
    if (current_terrain == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.native.final_terrain",
                     "TerrainGroup slot disappeared during readback");
    }
    const Ogre::TexturePtr current_texture =
        current_terrain->getCompositeMap();
    if (!current_texture) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.native.final_texture",
                     "Terrain composite texture disappeared during readback");
    }
    const Ogre::HardwarePixelBufferPtr current_buffer =
        current_texture->getBuffer(0U, 0U);
    if (!current_buffer) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "terrain_composite.native.final_pixel_buffer",
                     "Terrain composite pixel buffer disappeared during readback");
    }
    Ogre14TerrainCompositeNativeObservation after_readback;
    validation = CaptureObservation(terrain_group, slot_x, slot_y,
                                    *current_texture, *current_buffer,
                                    after_readback);
    if (!validation) {
      return validation;
    }
    return PublishOwnedReadback(configuration, before_readback,
                                std::move(rgba_bytes), after_readback,
                                receipt, fault_injector);
  } catch (const Ogre::Exception &) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "terrain_composite.capture.ogre_exception",
                   "OGRE failed before terrain composite receipt publication");
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "terrain_composite.capture.allocation",
                   "allocation failed before terrain composite receipt publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "terrain_composite.capture.unexpected_exception",
                   "unexpected exception before terrain composite receipt publication");
  }
}

} // namespace RoR::Render
