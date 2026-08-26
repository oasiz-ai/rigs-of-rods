/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextUvAffinePbs.h"

#include "OgreHlmsDatablock.h"
#include "OgreRenderable.h"

#include <atomic>

namespace RoR::Render {
namespace {
std::atomic<bool> g_indirect_alpha_export_enabled{false};
} // namespace

void OgreNextUvAffinePbs::SetIndirectAlphaExportEnabled(
    bool enabled) noexcept {
  g_indirect_alpha_export_enabled.store(enabled, std::memory_order_release);
}

bool OgreNextUvAffinePbs::IndirectAlphaExportEnabled() noexcept {
  return g_indirect_alpha_export_enabled.load(std::memory_order_acquire);
}

OgreNextUvAffinePbs::OgreNextUvAffinePbs(
    Ogre::Archive *data_folder, Ogre::ArchiveVec *library_folders)
    : Ogre::HlmsPbs(data_folder, library_folders) {}

bool OgreNextUvAffinePbs::SelectsUv0AffineShader(
    const Ogre::HlmsDatablock *datablock) noexcept {
  const Ogre::String *name =
      datablock != nullptr ? datablock->getNameStr() : nullptr;
  return name != nullptr &&
         (name->compare(0U,
                        sizeof(kOgreNextUvAffinePbsDatablockPrefix) - 1U,
                        kOgreNextUvAffinePbsDatablockPrefix) == 0 ||
          name->compare(0U,
                        sizeof(kOgreNextThinSlabPbsDatablockPrefix) - 1U,
                        kOgreNextThinSlabPbsDatablockPrefix) == 0);
}

bool OgreNextUvAffinePbs::SelectsThinSlabTransmissionShader(
    const Ogre::HlmsDatablock *datablock) noexcept {
  const Ogre::String *name =
      datablock != nullptr ? datablock->getNameStr() : nullptr;
  return name != nullptr &&
         name->compare(0U,
                       sizeof(kOgreNextThinSlabPbsDatablockPrefix) - 1U,
                       kOgreNextThinSlabPbsDatablockPrefix) == 0;
}

bool OgreNextUvAffinePbs::SelectsNiceMetalFlexShader(
    const Ogre::HlmsDatablock *datablock) noexcept {
  const Ogre::String *name =
      datablock != nullptr ? datablock->getNameStr() : nullptr;
  return name != nullptr &&
         name->compare(0U,
                       sizeof(kOgreNextNiceMetalProofDatablockPrefix) - 1U,
                       kOgreNextNiceMetalProofDatablockPrefix) == 0;
}

void OgreNextUvAffinePbs::calculateHashForPreCreate(
    Ogre::Renderable *renderable, Ogre::PiecesMap *in_out_pieces) {
  Ogre::HlmsPbs::calculateHashForPreCreate(renderable, in_out_pieces);
  setProperty(
      Ogre::IdString(kOgreNextUvAffinePbsProperty),
      SelectsUv0AffineShader(renderable != nullptr
                                ? renderable->getDatablock()
                                : nullptr)
          ? 1
          : 0);
  setProperty(
      Ogre::IdString(kOgreNextThinSlabPbsProperty),
      SelectsThinSlabTransmissionShader(
          renderable != nullptr ? renderable->getDatablock() : nullptr)
          ? 1
          : 0);
  // Indirect-alpha export applies to the opaque RT4/V1 datablocks only. The
  // thin-slab transmission shader keeps its own applyRefractions override,
  // so the two custom pieces can never be active in one shader.
  const Ogre::HlmsDatablock *indirect_alpha_datablock =
      renderable != nullptr ? renderable->getDatablock() : nullptr;
  setProperty(
      Ogre::IdString(kOgreNextIndirectAlphaPbsProperty),
      (IndirectAlphaExportEnabled() &&
       SelectsUv0AffineShader(indirect_alpha_datablock) &&
       !SelectsThinSlabTransmissionShader(indirect_alpha_datablock))
          ? 1
          : 0);
  setProperty(
      Ogre::IdString(kOgreNextNiceMetalFlexProperty),
      SelectsNiceMetalFlexShader(renderable != nullptr
                                     ? renderable->getDatablock()
                                     : nullptr)
          ? 1
          : 0);
}

void OgreNextUvAffinePbs::calculateHashForPreCaster(
    Ogre::Renderable *renderable, Ogre::PiecesMap *in_out_pieces,
    const Ogre::PiecesMap *normal_pass_pieces) {
  Ogre::HlmsPbs::calculateHashForPreCaster(
      renderable, in_out_pieces, normal_pass_pieces);
  // The upstream caster reducer intentionally erases unrelated properties.
  // Re-select this one after reduction so base-alpha cutouts use exactly the
  // same authored coordinates as every color/HDR scene evaluation.
  setProperty(
      Ogre::IdString(kOgreNextUvAffinePbsProperty),
      SelectsUv0AffineShader(renderable != nullptr
                                ? renderable->getDatablock()
                                : nullptr)
          ? 1
          : 0);
  setProperty(Ogre::IdString(kOgreNextThinSlabPbsProperty), 0);
  // The caster body never reaches the colour write the indirect-alpha piece
  // edits; keep the property out of the caster hash entirely.
  setProperty(Ogre::IdString(kOgreNextIndirectAlphaPbsProperty), 0);
  // The isolated proof is opaque and does not need a vertex-colour attribute
  // in the reduced shadow-caster shader.
  setProperty(Ogre::IdString(kOgreNextNiceMetalFlexProperty), 0);
}

} // namespace RoR::Render
