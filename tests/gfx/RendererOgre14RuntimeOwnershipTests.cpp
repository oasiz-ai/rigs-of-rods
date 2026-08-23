/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RendererOgre14RuntimeOwnership.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace RoR;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer Ogre 14 ownership test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void TestStandaloneOwnsItsExistingWindowAndInput() {
  const RendererOgre14RuntimeOwnership ownership =
      ResolveRendererOgre14RuntimeOwnership(
          RendererOgre14HostMode::LEGACY_STANDALONE);
  Require(ownership.valid() &&
              ownership.host_mode ==
                  RendererOgre14HostMode::LEGACY_STANDALONE &&
              ownership.legacy_window_visible &&
              ownership.legacy_frame_presentation_enabled &&
              ownership.legacy_physical_input_enabled &&
              !ownership.ogre_next_presenter_window_visible &&
              !ownership.ogre_next_presenter_physical_input_enabled,
          "standalone ownership changed");
}

void TestBridgeHasOneVisibleOwnerAndNoLegacyInputOwner() {
  const RendererOgre14RuntimeOwnership ownership =
      ResolveRendererOgre14RuntimeOwnership(
          RendererOgre14HostMode::OGRE_NEXT_BRIDGE_HOST);
  Require(ownership.valid() &&
              ownership.host_mode ==
                  RendererOgre14HostMode::OGRE_NEXT_BRIDGE_HOST &&
              !ownership.legacy_window_visible &&
              !ownership.legacy_frame_presentation_enabled &&
              !ownership.legacy_physical_input_enabled &&
              ownership.ogre_next_presenter_window_visible &&
              ownership.ogre_next_presenter_physical_input_enabled,
          "bridge did not transfer presentation and input ownership");
}

void TestCombinedHasNoLegacyVisibleFallback() {
  const RendererOgre14RuntimeOwnership ownership =
      ResolveRendererOgre14RuntimeOwnership(
          RendererOgre14HostMode::OGRE_NEXT_COMBINED_HOST);
  Require(ownership.valid() &&
              ownership.host_mode ==
                  RendererOgre14HostMode::OGRE_NEXT_COMBINED_HOST &&
              !ownership.legacy_window_visible &&
              !ownership.legacy_frame_presentation_enabled &&
              !ownership.legacy_physical_input_enabled &&
              ownership.ogre_next_presenter_window_visible &&
              ownership.ogre_next_presenter_physical_input_enabled,
          "combined mode admitted a legacy-visible fallback");
}

void TestSplitOwnershipCannotBeForged() {
  RendererOgre14RuntimeOwnership invalid =
      ResolveRendererOgre14RuntimeOwnership(
          RendererOgre14HostMode::OGRE_NEXT_COMBINED_HOST);
  invalid.legacy_window_visible = true;
  Require(!invalid.valid(), "two visible presentation owners were accepted");
  invalid = ResolveRendererOgre14RuntimeOwnership(
      RendererOgre14HostMode::OGRE_NEXT_COMBINED_HOST);
  invalid.legacy_physical_input_enabled = true;
  Require(!invalid.valid(), "two physical input owners were accepted");
  invalid = ResolveRendererOgre14RuntimeOwnership(
      RendererOgre14HostMode::OGRE_NEXT_COMBINED_HOST);
  invalid.legacy_frame_presentation_enabled = true;
  Require(!invalid.valid(), "hidden legacy frame presentation was accepted");
  invalid = ResolveRendererOgre14RuntimeOwnership(
      static_cast<RendererOgre14HostMode>(0xFFU));
  Require(!invalid.valid(), "unknown host mode was accepted");
}

} // namespace

int main() {
  TestStandaloneOwnsItsExistingWindowAndInput();
  TestBridgeHasOneVisibleOwnerAndNoLegacyInputOwner();
  TestCombinedHasNoLegacyVisibleFallback();
  TestSplitOwnershipCannotBeForged();
  std::cout << "Renderer Ogre 14 ownership tests passed\n";
  return EXIT_SUCCESS;
}
