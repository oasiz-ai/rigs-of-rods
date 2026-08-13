/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextUvAffinePbs.h"

#include "OgreHlmsDatablock.h"
#include "OgreRenderable.h"

namespace RoR::Render {

OgreNextUvAffinePbs::OgreNextUvAffinePbs(
    Ogre::Archive *data_folder, Ogre::ArchiveVec *library_folders)
    : Ogre::HlmsPbs(data_folder, library_folders) {}

bool OgreNextUvAffinePbs::SelectsUv0AffineShader(
    const Ogre::HlmsDatablock *datablock) noexcept {
  const Ogre::String *name =
      datablock != nullptr ? datablock->getNameStr() : nullptr;
  return name != nullptr &&
         name->compare(0U,
                       sizeof(kOgreNextUvAffinePbsDatablockPrefix) - 1U,
                       kOgreNextUvAffinePbsDatablockPrefix) == 0;
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
}

} // namespace RoR::Render
