/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Single-window and single-input-owner contract for Ogre-Next bridge mode.

#pragma once

#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererOgre14RuntimeOwnershipContractVersion = 1U;

/// Immutable startup decision made only after a real bridge endpoint has been
/// decoded. Standalone Ogre 14 retains its historical visible window, local
/// presentation, and physical devices. An adopted Ogre-Next bridge instead
/// keeps Ogre 14 as a hidden scene/resource host; the child exclusively owns
/// the visible presentation surface and physical input devices.
struct RendererOgre14RuntimeOwnership final {
  std::uint32_t version = kRendererOgre14RuntimeOwnershipContractVersion;
  bool bridge_active = false;
  bool legacy_window_visible = true;
  bool legacy_frame_presentation_enabled = true;
  bool legacy_physical_input_enabled = true;
  bool child_window_visible = false;
  bool child_physical_input_enabled = false;

  [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] RendererOgre14RuntimeOwnership
ResolveRendererOgre14RuntimeOwnership(bool bridge_active) noexcept;

} // namespace RoR
