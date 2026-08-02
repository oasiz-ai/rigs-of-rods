/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Owned production byte-stream session for the Ogre-Next child.

#pragma once

#include "RendererBridgeChannel.h"

#include "InputEventTransport.h"
#include "RenderBridgeControlTransport.h"
#include "RendererFrontendTransportDispatcher.h"

#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererOgreNextLiveSessionContractVersion = 2U;

/// One owner-thread observation made immediately before a complete transport
/// frame is dispatched. The callback owns native event translation and any
/// required surface update. Polling is deliberately frame-driven: the
/// blocking bridge wakes the native event pump once for each complete forward
/// envelope. The bounded input/reconciliation batch is emitted before that
/// frame's post-dispatch acknowledgement.
struct RendererOgreNextLiveSessionObservation final {
  std::uint32_t version = kRendererOgreNextLiveSessionContractVersion;
  Render::InputTransportBatch response;
  Render::RenderBridgeSurfaceState surface;
  bool window_close_requested = false;
};

using RendererOgreNextLiveSessionPollFunction = bool (*)(
    void *context, std::uint64_t acknowledged_forward_sequence,
    RendererOgreNextLiveSessionObservation *observation);

/// Injected owner-thread boundary. The frontend is initialized and bound to
/// its native presentation surface before RunRendererOgreNextLiveSession().
/// The callback may throw; the session catches it and fails closed.
struct RendererOgreNextLiveSessionRuntime final {
  std::uint32_t version = kRendererOgreNextLiveSessionContractVersion;
  Render::IRendererFrontend *frontend = nullptr;
  void *context = nullptr;
  RendererOgreNextLiveSessionPollFunction poll = nullptr;
  /// Exact active native-window state already committed by the frontend.
  /// It is sent in the first PEER_READY response before any forward read.
  Render::RenderBridgeSurfaceState initial_surface;
  /// Zero retains the deterministic frame-driven test seam. Production uses
  /// a small nonzero interval so native events and surface controls continue
  /// while no forward envelope is available.
  std::uint32_t idle_poll_interval_milliseconds = 0U;
};

enum class RendererOgreNextLiveSessionStatus : std::uint8_t {
  COMPLETED_PEER_EOF = 0U,
  COMPLETED_WINDOW_CLOSE,
  COMPLETED_PEER_REVERSE_CLOSE,
  REJECTED_INVALID_ENDPOINT,
  REJECTED_INVALID_RUNTIME,
  FAILED_CHANNEL_ADOPTION,
  FAILED_PEER_CLOSED_BEFORE_READY,
  FAILED_CHANNEL_READ,
  FAILED_CHANNEL_WRITE,
  FAILED_STREAM,
  FAILED_DISPATCH,
  FAILED_EVENT_POLL,
  FAILED_RESPONSE_ENCODE,
  FAILED_CHANNEL_CLOSE,
  FAILED_INTERNAL,
};

struct RendererOgreNextLiveSessionResult final {
  std::uint32_t version = kRendererOgreNextLiveSessionContractVersion;
  RendererOgreNextLiveSessionStatus status =
      RendererOgreNextLiveSessionStatus::FAILED_INTERNAL;
  RendererBridgeChannelStatus channel_status =
      RendererBridgeChannelStatus::UNINITIALIZED;
  Render::RenderTransportStreamStatus stream_status =
      Render::RenderTransportStreamStatus::NEED_MORE_DATA;
  Render::RendererFrontendTransportDispatchStatus dispatch_status =
      Render::RendererFrontendTransportDispatchStatus::FAILED_INTERNAL;
  Render::RenderTransportStatus response_status =
      Render::RenderTransportStatus::INVALID_ARGUMENT;
  std::uint64_t last_forward_sequence = 0U;
  std::uint64_t last_reverse_sequence = 0U;
  std::uint64_t last_acknowledged_forward_sequence = 0U;
  std::uint64_t last_presented_scene_sequence = 0U;
  std::uint64_t last_presented_snapshot_id = 0U;
  std::uint64_t asset_frames = 0U;
  std::uint64_t scene_frames = 0U;
  std::uint64_t presented_scene_frames = 0U;
  std::uint64_t retired_scene_frames = 0U;
  std::uint64_t idle_polls = 0U;
  std::uint64_t responses_sent = 0U;
  std::uint64_t input_batches_sent = 0U;
  std::uint64_t acknowledgements_sent = 0U;
  std::uint64_t controls_sent = 0U;
  std::uint64_t surface_changes_sent = 0U;
  std::uint64_t last_announced_surface_revision = 0U;
  std::uint64_t bytes_read = 0U;
  std::uint64_t bytes_written = 0U;
  bool channel_adopted = false;
  bool peer_ready_sent = false;
  bool completed = false;
};

/// Adopt exactly one PRESENTATION_FRONTEND endpoint, incrementally decode the
/// game-host stream, and dispatch only complete, envelope-validated frames.
/// The reverse stream shares one exact sequence across PEER_READY,
/// SURFACE_CHANGED, input, cumulative acknowledgement, and graceful-shutdown
/// control envelopes. PEER_READY carries the exact initial logical/drawable
/// state. Every later surface revision is announced before the affected frame
/// response, and a scene is never dispatched while that state is suspended. An
/// acknowledgement is emitted only after its forward frame dispatch succeeds;
/// a presented scene binds that exact sequence to its decoded snapshot ID.
/// Orderly EOF, window close, and a closed reverse peer all close owned handles
/// before returning. No package facts are changed here.
RendererOgreNextLiveSessionResult RunRendererOgreNextLiveSession(
    const RendererBridgeEndpoint &endpoint,
    const RendererOgreNextLiveSessionRuntime &runtime) noexcept;

bool IsKnownRendererOgreNextLiveSessionStatus(
    RendererOgreNextLiveSessionStatus status) noexcept;
const char *ToString(RendererOgreNextLiveSessionStatus status) noexcept;

} // namespace RoR
