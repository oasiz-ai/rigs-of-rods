/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Reserved-name extension of the standard Ogre-Next Unlit HLMS.

#pragma once

#include "OgreHlmsUnlit.h"

namespace RoR::Render {

inline constexpr char kOgreNextDisplayDomainDatablockPrefix[] =
    "RoRDisplayDomainUnlit_";
inline constexpr char kOgreNextDisplayDomainProperty[] =
    "ror_display_domain_unlit";
inline constexpr char kOgreNextDisplayDomainMediaPath[] =
    "Hlms/RoR/DisplayDomain";

/// Uses the fully initialized standard HLMS_UNLIT constructor. Only native
/// datablocks whose frontend-owned name has the immutable reserved prefix set
/// the custom shader property; all other Ogre Unlit users remain upstream
/// standard Unlit.
class OgreNextDisplayDomainUnlit final : public Ogre::HlmsUnlit {
public:
  OgreNextDisplayDomainUnlit(Ogre::Archive *data_folder,
                                   Ogre::ArchiveVec *library_folders);

protected:
  void calculateHashForPreCreate(Ogre::Renderable *renderable,
                                 Ogre::PiecesMap *in_out_pieces) override;
};

} // namespace RoR::Render
