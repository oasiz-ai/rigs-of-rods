/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Native SDL/Ogre-Next owner for one production bridge session.

#pragma once

#include "RendererOgreNextLiveSession.h"

#include <cstdint>
#include <string>

namespace RoR {

constexpr std::uint32_t kRendererOgreNextProductionSessionContractVersion = 1U;

struct RendererOgreNextProductionSessionConfiguration final {
  std::uint32_t version =
      kRendererOgreNextProductionSessionContractVersion;
  std::string shader_media_root;
  std::string presentation_media_root;
  std::uint32_t logical_width = 1280U;
  std::uint32_t logical_height = 720U;
};

enum class RendererOgreNextProductionSessionStatus : std::uint8_t {
  COMPLETED = 0U,
  REJECTED_INVALID_CONFIGURATION,
  FAILED_WINDOW_INITIALIZATION,
  FAILED_FRONTEND_INITIALIZATION,
  FAILED_LIVE_SESSION,
  FAILED_FRONTEND_AUDIT,
  FAILED_FRONTEND_SHUTDOWN,
  FAILED_WINDOW_SHUTDOWN,
  FAILED_INTERNAL,
};

struct RendererOgreNextProductionSessionResult final {
  std::uint32_t version =
      kRendererOgreNextProductionSessionContractVersion;
  RendererOgreNextProductionSessionStatus status =
      RendererOgreNextProductionSessionStatus::FAILED_INTERNAL;
  RendererOgreNextLiveSessionResult live;
  std::uint64_t presented_frames = 0U;
  std::uint64_t gpu_only_output_frames = 0U;
  std::uint64_t source_readbacks = 0U;
  bool ui_free_source = false;
  bool cpu_window_copy = false;
  bool completed = false;
};

/// Own the native window, Ogre-Next frontend, and bridge in strict teardown
/// order. This function is compiled only into the standalone modern child;
/// it does not alter launcher or package-admission facts.
RendererOgreNextProductionSessionResult RunRendererOgreNextProductionSession(
    const RendererBridgeEndpoint &endpoint,
    const RendererOgreNextProductionSessionConfiguration &configuration)
    noexcept;

bool IsKnownRendererOgreNextProductionSessionStatus(
    RendererOgreNextProductionSessionStatus status) noexcept;
const char *ToString(RendererOgreNextProductionSessionStatus status) noexcept;

} // namespace RoR
