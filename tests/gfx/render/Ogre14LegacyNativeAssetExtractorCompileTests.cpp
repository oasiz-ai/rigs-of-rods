/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14LegacyNativeAssetExtractor.h"

#include <OgreBuildSettings.h>
#include <OgreMaterial.h>

#include <cstdlib>
#include <type_traits>

int main() {
  static_assert(OGRE_VERSION_MAJOR == 14 && OGRE_VERSION_MINOR == 5 &&
                    OGRE_VERSION_PATCH == 2,
                "native extractor test requires pinned OGRE 14.5.2");
  using CaptureFunction = RoR::Render::ValidationResult (*)(
      const Ogre::Material &,
      const RoR::Render::Ogre14LegacyNativeMaterialDeclaration &,
      RoR::Render::Ogre14LegacyNativeMaterialCapture &);
  static_assert(
      std::is_same<decltype(&RoR::Render::CaptureOgre14LegacyNativeMaterial),
                   CaptureFunction>::value,
      "native capture ABI changed without a version migration");
  return EXIT_SUCCESS;
}
