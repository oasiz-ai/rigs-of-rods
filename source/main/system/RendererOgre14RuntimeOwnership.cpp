/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RendererOgre14RuntimeOwnership.h"

namespace RoR {

bool IsKnownRendererOgre14HostMode(
    RendererOgre14HostMode host_mode) noexcept {
  switch (host_mode) {
  case RendererOgre14HostMode::LEGACY_STANDALONE:
  case RendererOgre14HostMode::OGRE_NEXT_BRIDGE_HOST:
  case RendererOgre14HostMode::OGRE_NEXT_COMBINED_HOST:
    return true;
  }
  return false;
}

bool RendererOgre14RuntimeOwnership::valid() const noexcept {
  if (version != kRendererOgre14RuntimeOwnershipContractVersion ||
      !IsKnownRendererOgre14HostMode(host_mode)) {
    return false;
  }
  const unsigned visible_owners =
      static_cast<unsigned>(legacy_window_visible) +
      static_cast<unsigned>(ogre_next_presenter_window_visible);
  const unsigned physical_input_owners =
      static_cast<unsigned>(legacy_physical_input_enabled) +
      static_cast<unsigned>(ogre_next_presenter_physical_input_enabled);
  if (visible_owners != 1U || physical_input_owners != 1U) {
    return false;
  }
  if (legacy_frame_presentation_enabled != legacy_window_visible) {
    return false;
  }
  if (host_mode == RendererOgre14HostMode::LEGACY_STANDALONE) {
    return legacy_window_visible && legacy_frame_presentation_enabled &&
           legacy_physical_input_enabled &&
           !ogre_next_presenter_window_visible &&
           !ogre_next_presenter_physical_input_enabled;
  }
  return !legacy_window_visible && !legacy_frame_presentation_enabled &&
         !legacy_physical_input_enabled &&
         ogre_next_presenter_window_visible &&
         ogre_next_presenter_physical_input_enabled;
}

RendererOgre14RuntimeOwnership ResolveRendererOgre14RuntimeOwnership(
    RendererOgre14HostMode host_mode) noexcept {
  RendererOgre14RuntimeOwnership ownership;
  ownership.host_mode = host_mode;
  if (host_mode == RendererOgre14HostMode::OGRE_NEXT_BRIDGE_HOST ||
      host_mode == RendererOgre14HostMode::OGRE_NEXT_COMBINED_HOST) {
    ownership.legacy_window_visible = false;
    ownership.legacy_frame_presentation_enabled = false;
    ownership.legacy_physical_input_enabled = false;
    ownership.ogre_next_presenter_window_visible = true;
    ownership.ogre_next_presenter_physical_input_enabled = true;
  }
  return ownership;
}

} // namespace RoR
