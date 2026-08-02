/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief OGRE 14.5.2-native capture edge for the pure legacy translator.

#pragma once

#include "Ogre14LegacyAssetTranslator.h"

#include <cstdint>
#include <vector>

namespace Ogre {
class Material;
}

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyNativeAssetExtractorVersion = 1U;

/// PBR intent and texture color role must come from explicit content metadata
/// or a versioned compatibility table. The native extractor never guesses
/// either value from a resource name or legacy specular state.
struct Ogre14LegacyNativeMaterialDeclaration {
  std::uint32_t version = kOgre14LegacyNativeAssetExtractorVersion;
  Ogre14LegacyBaseColorSemantic base_color_semantic =
      Ogre14LegacyBaseColorSemantic::UNLIT;
  Ogre14LegacyTextureColorRole texture_color_role =
      Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
  /// Must match the translator which consumes this capture. Native readback
  /// applies the per-asset decoded-byte cap before allocating mip storage.
  Ogre14LegacyAssetTranslatorConfiguration translator_configuration;
};

struct Ogre14LegacyNativeMaterialCapture {
  std::uint32_t version = kOgre14LegacyNativeAssetExtractorVersion;
  Ogre14LegacyMaterialInput material;
  std::vector<Ogre14LegacyTextureInput> textures;
};

/// Reads one already-loaded immutable Material and its optional already-loaded
/// 2D texture. It performs CPU readback through OGRE's PixelUtil/PF_BYTE_RGBA
/// path, which defines RGBA byte order on either host endianness. Exceptions,
/// unsupported native state, or readback failures leave `capture` untouched.
[[nodiscard]] ValidationResult CaptureOgre14LegacyNativeMaterial(
    const Ogre::Material &material,
    const Ogre14LegacyNativeMaterialDeclaration &declaration,
    Ogre14LegacyNativeMaterialCapture &capture);

} // namespace RoR::Render
