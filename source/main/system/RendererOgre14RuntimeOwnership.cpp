/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RendererOgre14RuntimeOwnership.h"

namespace RoR {

bool RendererOgre14RuntimeOwnership::valid() const noexcept {
  if (version != kRendererOgre14RuntimeOwnershipContractVersion) {
    return false;
  }
  const unsigned visible_owners =
      static_cast<unsigned>(legacy_window_visible) +
      static_cast<unsigned>(child_window_visible);
  const unsigned physical_input_owners =
      static_cast<unsigned>(legacy_physical_input_enabled) +
      static_cast<unsigned>(child_physical_input_enabled);
  if (visible_owners != 1U || physical_input_owners != 1U) {
    return false;
  }
  if (legacy_frame_presentation_enabled != legacy_window_visible) {
    return false;
  }
  return bridge_active
             ? !legacy_window_visible &&
                   !legacy_frame_presentation_enabled &&
                   !legacy_physical_input_enabled && child_window_visible &&
                   child_physical_input_enabled
             : legacy_window_visible &&
                   legacy_frame_presentation_enabled &&
                   legacy_physical_input_enabled && !child_window_visible &&
                   !child_physical_input_enabled;
}

RendererOgre14RuntimeOwnership ResolveRendererOgre14RuntimeOwnership(
    bool bridge_active) noexcept {
  RendererOgre14RuntimeOwnership ownership;
  ownership.bridge_active = bridge_active;
  if (bridge_active) {
    ownership.legacy_window_visible = false;
    ownership.legacy_frame_presentation_enabled = false;
    ownership.legacy_physical_input_enabled = false;
    ownership.child_window_visible = true;
    ownership.child_physical_input_enabled = true;
  }
  return ownership;
}

} // namespace RoR
