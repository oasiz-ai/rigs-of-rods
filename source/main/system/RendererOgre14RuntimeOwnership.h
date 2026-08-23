/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Single-window and single-input-owner contract for the transitional
/// OGRE 14 host.

#pragma once

#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererOgre14RuntimeOwnershipContractVersion = 2U;

/// The reason OGRE 14 exists in the process. Combined mode is deliberately
/// distinct from the historical two-process bridge: it has no child process
/// and no renderer fallback. Both Ogre-Next modes keep the OGRE 14 host hidden.
enum class RendererOgre14HostMode : std::uint8_t {
  LEGACY_STANDALONE = 0U,
  OGRE_NEXT_BRIDGE_HOST = 1U,
  OGRE_NEXT_COMBINED_HOST = 2U,
};

/// Immutable startup decision. Standalone OGRE 14 retains its historical
/// visible window, local presentation, and physical devices. The bridge and
/// combined modes instead keep OGRE 14 as a hidden scene/resource host;
/// Ogre-Next exclusively owns the visible presentation surface and physical
/// input devices. A combined-mode failure is terminal and cannot be represented
/// as a legacy-visible fallback by this contract.
struct RendererOgre14RuntimeOwnership final {
  std::uint32_t version = kRendererOgre14RuntimeOwnershipContractVersion;
  RendererOgre14HostMode host_mode =
      RendererOgre14HostMode::LEGACY_STANDALONE;
  bool legacy_window_visible = true;
  bool legacy_frame_presentation_enabled = true;
  bool legacy_physical_input_enabled = true;
  bool ogre_next_presenter_window_visible = false;
  bool ogre_next_presenter_physical_input_enabled = false;

  [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] RendererOgre14RuntimeOwnership
ResolveRendererOgre14RuntimeOwnership(RendererOgre14HostMode host_mode) noexcept;

[[nodiscard]] bool
IsKnownRendererOgre14HostMode(RendererOgre14HostMode host_mode) noexcept;

} // namespace RoR
