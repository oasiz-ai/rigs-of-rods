/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Bounded reverse-direction bridge acknowledgement and control wire.

#pragma once

#include "RenderTransportEnvelope.h"

#include <cstdint>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kRenderBridgeControlTransportPayloadVersion = 1U;
constexpr std::uint64_t kRenderBridgeControlTransportMaximumPayloadBytes =
    128ULL;

enum class RenderBridgeControlKind : std::uint8_t {
  PEER_READY = 1U,
  REQUEST_GRACEFUL_SHUTDOWN = 2U,
  HEARTBEAT = 3U,
};

/// Cumulative presentation acknowledgement. `through_forward_sequence`
/// acknowledges every game-to-presentation envelope through that sequence.
/// The presented fields are either all zero (no scene presented yet), or name
/// one exact scene envelope and its immutable snapshot identity.
struct RenderBridgeAcknowledgement final {
  std::uint32_t version = kRenderBridgeControlTransportPayloadVersion;
  std::uint64_t registry_id = 0U;
  std::uint64_t through_forward_sequence = 0U;
  std::uint64_t presented_scene_sequence = 0U;
  std::uint64_t presented_snapshot_id = 0U;
};

/// Ordered presentation-process lifecycle command. Command IDs start at one
/// and are checked for exact monotonic lineage by the owning bridge session.
struct RenderBridgeControl final {
  std::uint32_t version = kRenderBridgeControlTransportPayloadVersion;
  RenderBridgeControlKind kind = RenderBridgeControlKind::PEER_READY;
  std::uint64_t registry_id = 0U;
  std::uint64_t command_id = 0U;
};

struct RenderBridgeControlTransportDecodeResult final {
  RenderTransportStatus status = RenderTransportStatus::INVALID_ARGUMENT;
  RenderTransportMessageKind kind =
      RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1;
  std::uint64_t sequence = 0U;
  RenderBridgeAcknowledgement acknowledgement;
  RenderBridgeControl control;

  [[nodiscard]] bool ok() const noexcept {
    return status == RenderTransportStatus::OK && sequence != 0U &&
           (kind == RenderTransportMessageKind::
                        RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 ||
            kind == RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1);
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Transactional decoder sharing the complete reverse-direction sequence with
/// InputEventTransportDecoder. Payload registry identity is pinned to the
/// bridge session before the shared sequence can advance.
class RenderBridgeControlTransportDecoder final {
public:
  RenderBridgeControlTransportDecoder(
      std::uint64_t registry_id,
      RenderTransportSequenceState &shared_sequence_state) noexcept;

  RenderBridgeControlTransportDecoder(
      const RenderBridgeControlTransportDecoder &) = delete;
  RenderBridgeControlTransportDecoder &operator=(
      const RenderBridgeControlTransportDecoder &) = delete;
  RenderBridgeControlTransportDecoder(
      RenderBridgeControlTransportDecoder &&) = delete;
  RenderBridgeControlTransportDecoder &operator=(
      RenderBridgeControlTransportDecoder &&) = delete;
  ~RenderBridgeControlTransportDecoder() = default;

  [[nodiscard]] RenderBridgeControlTransportDecodeResult
  Accept(const std::vector<std::uint8_t> &frame) noexcept;

  [[nodiscard]] std::uint64_t registry_id() const noexcept {
    return registry_id_;
  }

private:
  std::uint64_t registry_id_ = 0U;
  RenderTransportSequenceState *sequence_state_ = nullptr;
};

[[nodiscard]] bool
IsKnownRenderBridgeControlKind(RenderBridgeControlKind kind) noexcept;

[[nodiscard]] RenderTransportEnvelopeEncodeResult
EncodeRenderBridgeAcknowledgementFrame(
    std::uint64_t sequence,
    const RenderBridgeAcknowledgement &acknowledgement);

[[nodiscard]] RenderTransportEnvelopeEncodeResult
EncodeRenderBridgeControlFrame(std::uint64_t sequence,
                               const RenderBridgeControl &control);

} // namespace RoR::Render
