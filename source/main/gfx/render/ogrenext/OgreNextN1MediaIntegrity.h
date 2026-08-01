/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Runtime integrity gate for the shader media compiled into N1.

#pragma once

#include "../RendererFrontend.h"

#include <string>

namespace RoR::Render {

/// Verifies that root/Hlms contains exactly the regular files, byte sizes, and
/// SHA-256 digests compiled from the pinned Ogre-Next archive. The check must
/// complete before Ogre::Root or a native render device is constructed.
[[nodiscard]] RenderOperationResult VerifyOgreNextN1ShaderMedia(
    const std::string &resolved_media_root);

/// Verifies the exact pinned PCC depth-compressor, local-cubemap, and IBL
/// shader closure used by the modern RT4 reflection path. The check rejects
/// missing, extra, indirect, or byte-modified resources before device creation.
[[nodiscard]] RenderOperationResult VerifyOgreNextReflectionProbeMedia(
    const std::string &resolved_media_root);

} // namespace RoR::Render
