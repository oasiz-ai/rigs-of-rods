/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Pure launch transaction for the isolated production render bridge.

#pragma once

#include "RendererBridgeEndpoint.h"

#include <cstdint>
#include <vector>

namespace RoR {

constexpr std::uint32_t kRendererBridgeLaunchPlanContractVersion = 1U;

/// Two one-way supervisor-created byte streams. All four inherited native
/// endpoints must be distinct. A handle is listed by the child which owns it.
struct RendererBridgeStreamHandles final {
  std::uint32_t version = kRendererBridgeLaunchPlanContractVersion;
  std::uint64_t game_to_frontend_read = 0U;
  std::uint64_t game_to_frontend_write = 0U;
  std::uint64_t frontend_to_game_read = 0U;
  std::uint64_t frontend_to_game_write = 0U;
};

enum class RendererBridgeLaunchPlanStatus : std::uint8_t {
  READY = 0U,
  REJECTED_INVALID_HANDOFF,
  REJECTED_INVALID_ARGUMENTS,
  REJECTED_INVALID_SESSION,
  REJECTED_INVALID_STREAM_HANDLES,
  REJECTED_ENDPOINT_ENCODING,
  REJECTED_RENDERER_INTENT_ENCODING,
  FAILED_INTERNAL,
};

/// Fully owned, process-independent transaction. OS launch code may resolve
/// only these exact sibling basenames and may expose to each child only the two
/// native handles named by its decoded endpoint.
struct RendererBridgeLaunchPlan final {
  std::uint32_t version = kRendererBridgeLaunchPlanContractVersion;
  RendererBridgeLaunchPlanStatus status =
      RendererBridgeLaunchPlanStatus::REJECTED_INVALID_HANDOFF;
  HostRenderPlatform platform = HostRenderPlatform::UNKNOWN;
  RendererBridgeSessionId session_id{};
  RendererBridgeStreamHandles streams;
  RendererChildLauncherString game_child_basename;
  RendererChildLauncherString presentation_child_basename;
  std::vector<RendererChildLauncherString> game_child_arguments;
  std::vector<RendererChildLauncherString> presentation_child_arguments;
  bool accepted = false;
};

/// Construct and self-decode both child argv vectors before publishing the
/// transaction. The accepted handoff must select a production-admitted
/// Ogre-Next child on this compile-time host. No OS resource is opened here.
RendererBridgeLaunchPlan BuildRendererBridgeLaunchPlan(
    const RendererStartupHandoffResult &handoff,
    const RendererBridgeSessionId &session_id,
    const RendererBridgeStreamHandles &streams, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept;

bool IsKnownRendererBridgeLaunchPlanStatus(
    RendererBridgeLaunchPlanStatus status) noexcept;
const char *ToString(RendererBridgeLaunchPlanStatus status) noexcept;

} // namespace RoR
