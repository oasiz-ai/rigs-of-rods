/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Asynchronous renderer-neutral game-host bridge stream session.

#pragma once

#include "RendererOgre14GameBridge.h"

#include "render/InputEventTransport.h"
#include "render/RenderAssetDeltaTransport.h"
#include "render/RenderBridgeControlTransport.h"
#include "render/SceneSnapshotTransport.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace RoR {

constexpr std::uint32_t kRendererOgre14GameHostSessionContractVersion = 1U;
constexpr std::uint32_t kRendererOgre14GameHostSessionConfigVersion = 1U;

struct RendererOgre14GameHostSessionConfig final {
  std::uint32_t version = kRendererOgre14GameHostSessionConfigVersion;
  std::size_t maximum_forward_queue_bytes =
      768U * 1024U * 1024U;
  std::uint32_t maximum_forward_messages = 64U;
  std::uint32_t maximum_unacknowledged_forward_messages = 4096U;
  std::uint32_t maximum_reverse_messages = 256U;
  std::size_t reverse_read_chunk_bytes = 64U * 1024U;
  std::uint32_t idle_wait_milliseconds = 1U;
};

enum class RendererOgre14GameHostSessionStatus : std::uint8_t {
  READY = 0U,
  ASSET_DELTA_QUEUED,
  SCENE_SNAPSHOT_QUEUED,
  REVERSE_MESSAGE_READY,
  OUTBOUND_HALF_CLOSE_REQUESTED,
  CLOSED,
  EMPTY,
  BACKPRESSURE,
  REJECTED_INVALID_CONFIGURATION,
  REJECTED_INACTIVE_BRIDGE,
  REJECTED_NOT_READY,
  REJECTED_REGISTRY_ID,
  REJECTED_ASSET_LINEAGE,
  REJECTED_SCENE_LINEAGE,
  REJECTED_SURFACE_STATE,
  FAILED_FORWARD_ENCODING,
  FAILED_REVERSE_STREAM,
  FAILED_REVERSE_DECODE,
  FAILED_REVERSE_LINEAGE,
  PEER_CLOSED,
  FAILED_CHANNEL,
  FAILED_INTERNAL,
};

struct RendererOgre14GameHostSessionResult final {
  std::uint32_t version = kRendererOgre14GameHostSessionContractVersion;
  RendererOgre14GameHostSessionStatus status =
      RendererOgre14GameHostSessionStatus::FAILED_INTERNAL;
  RendererOgre14GameHostSessionStatus terminal_cause =
      RendererOgre14GameHostSessionStatus::FAILED_INTERNAL;
  Render::RenderTransportMessageKind kind =
      Render::RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2;
  std::uint64_t forward_sequence = 0U;
  std::uint64_t reverse_sequence = 0U;
  std::uint64_t surface_revision = 0U;
  Render::RenderTransportStatus transport_status =
      Render::RenderTransportStatus::OK;
  Render::ValidationCode validation_code = Render::ValidationCode::OK;
  RendererBridgeChannelStatus channel_status =
      RendererBridgeChannelStatus::UNINITIALIZED;
  bool accepted = false;
  bool terminal = false;

  [[nodiscard]] bool ok() const noexcept { return accepted && !terminal; }
  explicit operator bool() const noexcept { return ok(); }
};

/// Deep-owned reverse message. Input lineage exposes the renderer-issued event
/// interval and the authoritative reconciliation watermark resolved by the
/// decoder. Exactly one of input/acknowledgement/control matches `kind`.
struct RendererOgre14GameHostReverseMessage final {
  Render::RenderTransportMessageKind kind =
      Render::RenderTransportMessageKind::INPUT_EVENT_BATCH_V1;
  std::uint64_t reverse_sequence = 0U;
  std::uint64_t issued_first_event_id = 0U;
  std::uint64_t issued_last_event_id = 0U;
  std::uint64_t resolved_through_event_id = 0U;
  std::shared_ptr<const Render::DecodedInputEventTransportMessage> input;
  Render::RenderBridgeAcknowledgement acknowledgement;
  Render::RenderBridgeControl control;
};

struct RendererOgre14GameHostPollResult final {
  RendererOgre14GameHostSessionResult result;
  RendererOgre14GameHostReverseMessage message;

  [[nodiscard]] bool available() const noexcept {
    return result.status ==
               RendererOgre14GameHostSessionStatus::REVERSE_MESSAGE_READY &&
           result.accepted;
  }
  explicit operator bool() const noexcept { return available(); }
};

/// Renderer-neutral producer/consumer boundary for the legacy game process.
/// Submit() and PostPhysics() validate and serialize complete immutable
/// messages, then only enqueue bounded bytes; pipe I/O is owned by one worker.
/// Poll methods never wait. Asset and scene envelopes share one exact forward
/// sequence, while input/control/ack messages share a private reverse sequence.
/// Reverse traffic is drained before each zero-wait forward write, and full
/// reverse delivery capacity pauses forward progress until the game polls it.
/// Assets may queue during child initialization, but scenes require the latest
/// active PEER_READY/SURFACE_CHANGED drawable extent and remain blocked while
/// the presentation surface is suspended.
/// The referenced bootstrap owns the channel and must outlive this session.
class RendererOgre14GameHostSession final {
public:
  explicit RendererOgre14GameHostSession(
      RendererOgre14GameBridge &bridge);
  ~RendererOgre14GameHostSession();

  RendererOgre14GameHostSession(
      const RendererOgre14GameHostSession &) = delete;
  RendererOgre14GameHostSession &operator=(
      const RendererOgre14GameHostSession &) = delete;
  RendererOgre14GameHostSession(
      RendererOgre14GameHostSession &&) = delete;
  RendererOgre14GameHostSession &operator=(
      RendererOgre14GameHostSession &&) = delete;

  [[nodiscard]] RendererOgre14GameHostSessionResult Start(
      const RendererOgre14GameHostSessionConfig &config = {});
  [[nodiscard]] RendererOgre14GameHostSessionResult
  Submit(const Render::RenderAssetDelta &delta);
  [[nodiscard]] RendererOgre14GameHostSessionResult
  PostPhysics(const Render::SceneSnapshot &snapshot,
              const Render::CameraViewRequest &camera);

  [[nodiscard]] RendererOgre14GameHostPollResult PollReverse();
  [[nodiscard]] RendererOgre14GameHostPollResult PollInput();
  [[nodiscard]] RendererOgre14GameHostPollResult PollAcknowledgement();
  [[nodiscard]] RendererOgre14GameHostPollResult PollControl();

  /// Drains already queued forward messages, closes only the outbound pipe,
  /// and continues accepting final reverse acknowledgements until peer EOF.
  [[nodiscard]] RendererOgre14GameHostSessionResult FinishForward();
  [[nodiscard]] RendererOgre14GameHostSessionResult Close();

  [[nodiscard]] std::uint64_t registry_id() const noexcept;
  [[nodiscard]] std::uint64_t next_forward_sequence() const noexcept;
  [[nodiscard]] std::uint64_t last_written_forward_sequence() const noexcept;
  [[nodiscard]] std::uint64_t last_acknowledged_forward_sequence() const
      noexcept;
  [[nodiscard]] std::size_t queued_forward_bytes() const noexcept;
  [[nodiscard]] std::size_t queued_reverse_messages() const noexcept;
  [[nodiscard]] Render::RenderBridgeSurfaceState
  current_surface_state() const noexcept;
  [[nodiscard]] bool peer_ready() const noexcept;
  [[nodiscard]] bool terminal() const noexcept;
  [[nodiscard]] RendererOgre14GameHostSessionStatus terminal_cause() const
      noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool IsKnownRendererOgre14GameHostSessionStatus(
    RendererOgre14GameHostSessionStatus status) noexcept;
[[nodiscard]] const char *
ToString(RendererOgre14GameHostSessionStatus status) noexcept;

} // namespace RoR
