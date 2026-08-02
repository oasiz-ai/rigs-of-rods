/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h"

#include <OgreBuildSettings.h>
#include <OgreLogManager.h>
#include <OgreMaterial.h>
#include <OgrePass.h>
#include <OgreRoot.h>
#include <OgreTechnique.h>
#include <OgreTextureUnitState.h>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

struct CanonicalMaterial {
  explicit CanonicalMaterial(std::string name)
      : material(nullptr, std::move(name), 1U, "NativeTests") {
    technique = material.createTechnique();
    pass = technique->createPass();
    pass->setLightingEnabled(false);
    pass->setAmbient(Ogre::ColourValue::Black);
  }

  void LoadForCapture(bool auto_manage_texture_units = true) {
    material.compile(auto_manage_texture_units);
    material.load();
  }

  Ogre::Material material;
  Ogre::Technique *technique = nullptr;
  Ogre::Pass *pass = nullptr;
};

void RequireRejectedWithoutMutation(
    const CanonicalMaterial &fixture,
    const RoR::Render::Ogre14LegacyNativeMaterialDeclaration &declaration,
    const char *field, const char *message) {
  RoR::Render::Ogre14LegacyNativeMaterialCapture capture;
  capture.material.key.exact_name = "sentinel";
  const RoR::Render::ValidationResult result =
      RoR::Render::CaptureOgre14LegacyNativeMaterial(
          fixture.material, declaration, capture);
  Require(!result && result.field == field &&
              capture.material.key.exact_name == "sentinel",
          message);
}

void TestNativeStateValidationIsSemanticAndTransactional() {
  using namespace RoR::Render;
  Ogre14LegacyNativeMaterialDeclaration declaration;

  CanonicalMaterial canonical("Canonical");
  canonical.LoadForCapture();
  Ogre14LegacyNativeMaterialCapture capture;
  Require(CaptureOgre14LegacyNativeMaterial(canonical.material, declaration,
                                             capture)
              .ok() &&
              capture.material.key.exact_name == "Canonical" &&
              capture.material.source_revision != 0U,
          "canonical loaded native material was not captured exactly");

  CanonicalMaterial fog("Fog");
  fog.pass->setFog(true, Ogre::FOG_LINEAR);
  fog.LoadForCapture();
  RequireRejectedWithoutMutation(fog, declaration,
                                 "material.pipeline.scene_overrides",
                                 "native fog override was silently dropped");

  CanonicalMaterial sorting("Sorting");
  sorting.pass->setTransparentSortingEnabled(false);
  sorting.LoadForCapture();
  RequireRejectedWithoutMutation(
      sorting, declaration, "material.pipeline.transparent_sorting",
      "native transparent sorting policy was silently dropped");

  CanonicalMaterial lighting("Lighting");
  lighting.pass->setIteratePerLight(true);
  lighting.LoadForCapture();
  RequireRejectedWithoutMutation(
      lighting, declaration, "material.pipeline.lighting_controls",
      "native per-light iteration was silently dropped");

  CanonicalMaterial vertex_color("VertexColor");
  vertex_color.pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
  vertex_color.LoadForCapture();
  RequireRejectedWithoutMutation(
      vertex_color, declaration,
      "material.pipeline.vertex_colour_tracking",
      "native vertex-colour tracking was silently dropped");

  CanonicalMaterial shadows("Shadows");
  shadows.material.setReceiveShadows(false);
  shadows.LoadForCapture();
  RequireRejectedWithoutMutation(shadows, declaration,
                                 "material.shadow_policy",
                                 "native material shadow policy was dropped");

  CanonicalMaterial hardware_rules("HardwareRules");
  hardware_rules.technique->addGPUVendorRule(
      Ogre::Technique::GPUVendorRule(Ogre::GPU_NVIDIA,
                                     Ogre::Technique::INCLUDE));
  hardware_rules.LoadForCapture();
  RequireRejectedWithoutMutation(
      hardware_rules, declaration, "material.technique_hardware_rules",
      "native hardware-specific technique rule was silently dropped");

  CanonicalMaterial texcoord("Texcoord");
  Ogre::TextureUnitState *texcoord_unit =
      texcoord.pass->createTextureUnitState();
  texcoord_unit->setTextureCoordSet(2U);
  texcoord.LoadForCapture(false);
  RequireRejectedWithoutMutation(
      texcoord, declaration,
      "material.texture_unit.texture_coordinate_set",
      "native texture-coordinate range was narrowed before validation");

  CanonicalMaterial unordered_access("UnorderedAccess");
  Ogre::TextureUnitState *unordered_access_unit =
      unordered_access.pass->createTextureUnitState();
  unordered_access_unit->setUnorderedAccessMipLevel(0);
  unordered_access.LoadForCapture(false);
  RequireRejectedWithoutMutation(
      unordered_access, declaration,
      "material.texture_unit.unordered_access",
      "native unordered-access texture state was silently dropped");

  if constexpr ((std::numeric_limits<std::size_t>::max)() >
                (std::numeric_limits<std::uint32_t>::max)()) {
    CanonicalMaterial iteration_range("IterationRange");
    iteration_range.pass->setPassIterationCount(
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint32_t>::max)()) +
        1U);
    iteration_range.LoadForCapture();
    RequireRejectedWithoutMutation(
        iteration_range, declaration,
        "material.pipeline.pass_iteration_count",
        "native pass iteration count saturated instead of failing closed");
  }

  Ogre14LegacyNativeMaterialDeclaration invalid_configuration = declaration;
  invalid_configuration.translator_configuration
      .maximum_decoded_bytes_per_asset = 0U;
  RequireRejectedWithoutMutation(
      canonical, invalid_configuration, "configuration.limits",
      "invalid native capture budget reached or mutated material output");
}

} // namespace

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
  Ogre::LogManager log_manager;
  log_manager.createLog("NativeExtractorTests", true, false, true);
  Ogre::Root root("", "", "");
  TestNativeStateValidationIsSemanticAndTransactional();
  std::cout << "OGRE 14 native legacy asset extractor tests passed\n";
  return EXIT_SUCCESS;
}
