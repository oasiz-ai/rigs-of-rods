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
      ResolveRendererOgre14RuntimeOwnership(false);
  Require(ownership.valid() && !ownership.bridge_active &&
              ownership.legacy_window_visible &&
              ownership.legacy_frame_presentation_enabled &&
              ownership.legacy_physical_input_enabled &&
              !ownership.child_window_visible &&
              !ownership.child_physical_input_enabled,
          "standalone ownership changed");
}

void TestBridgeHasOneVisibleOwnerAndNoLegacyInputOwner() {
  const RendererOgre14RuntimeOwnership ownership =
      ResolveRendererOgre14RuntimeOwnership(true);
  Require(ownership.valid() && ownership.bridge_active &&
              !ownership.legacy_window_visible &&
              !ownership.legacy_frame_presentation_enabled &&
              !ownership.legacy_physical_input_enabled &&
              ownership.child_window_visible &&
              ownership.child_physical_input_enabled,
          "bridge did not transfer presentation and input ownership");
}

void TestSplitOwnershipCannotBeForged() {
  RendererOgre14RuntimeOwnership invalid =
      ResolveRendererOgre14RuntimeOwnership(true);
  invalid.legacy_window_visible = true;
  Require(!invalid.valid(), "two visible presentation owners were accepted");
  invalid = ResolveRendererOgre14RuntimeOwnership(true);
  invalid.legacy_physical_input_enabled = true;
  Require(!invalid.valid(), "two physical input owners were accepted");
  invalid = ResolveRendererOgre14RuntimeOwnership(true);
  invalid.legacy_frame_presentation_enabled = true;
  Require(!invalid.valid(), "hidden legacy frame presentation was accepted");
}

} // namespace

int main() {
  TestStandaloneOwnsItsExistingWindowAndInput();
  TestBridgeHasOneVisibleOwnerAndNoLegacyInputOwner();
  TestSplitOwnershipCannotBeForged();
  std::cout << "Renderer Ogre 14 ownership tests passed\n";
  return EXIT_SUCCESS;
}
