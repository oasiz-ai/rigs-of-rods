/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextDisplayDomainUnlit.h"

#include "OgreHlmsDatablock.h"
#include "OgreRenderable.h"

namespace RoR::Render {

OgreNextDisplayDomainUnlit::OgreNextDisplayDomainUnlit(
    Ogre::Archive *data_folder, Ogre::ArchiveVec *library_folders)
    : Ogre::HlmsUnlit(data_folder, library_folders) {}

void OgreNextDisplayDomainUnlit::calculateHashForPreCreate(
    Ogre::Renderable *renderable, Ogre::PiecesMap *in_out_pieces) {
  Ogre::HlmsUnlit::calculateHashForPreCreate(renderable, in_out_pieces);

  const Ogre::HlmsDatablock *datablock =
      renderable != nullptr ? renderable->getDatablock() : nullptr;
  const Ogre::String *name =
      datablock != nullptr ? datablock->getNameStr() : nullptr;
  const bool selected =
      name != nullptr &&
      name->compare(0U,
                    sizeof(kOgreNextDisplayDomainDatablockPrefix) - 1U,
                    kOgreNextDisplayDomainDatablockPrefix) == 0;
  setProperty(Ogre::IdString(kOgreNextDisplayDomainProperty),
              selected ? 1 : 0);
}

} // namespace RoR::Render
