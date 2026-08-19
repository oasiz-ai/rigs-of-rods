/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Shared deterministic cross-process render-message envelope.

#pragma once

#include "RenderPayloadDigest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RoR::Render {

constexpr std::uint16_t kRenderTransportEnvelopeVersion = 1U;
constexpr std::size_t kRenderTransportEnvelopeHeaderBytes = 64U;

inline constexpr std::array<std::uint8_t, 8U> kRenderTransportEnvelopeMagic{{
    0x52U, 0x4fU, 0x52U, 0x53U, 0x43U, 0x4eU, 0x30U, 0x31U,
}};

enum class RenderTransportMessageKind : std::uint16_t {
  // 1U carried the retired scene-snapshot v4 schema and stays reserved so a
  // stale peer's v4 frames fail closed as UNKNOWN_MESSAGE_KIND.
  RENDER_ASSET_DELTA_V1 = 2U,
  INPUT_EVENT_BATCH_V1 = 3U,
  RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 = 4U,
  RENDER_BRIDGE_CONTROL_V1 = 5U,
  SCENE_GENERATION_BOUNDARY_V1 = 6U,
  RENDER_ASSET_DELTA_V2 = 7U,
  SCENE_SNAPSHOT_V5_CAMERA_V2 = 8U,
};

enum class RenderTransportStatus : std::uint8_t {
  OK = 0U,
  INVALID_ARGUMENT,
  ALLOCATION_FAILURE,
  FRAME_TRUNCATED,
  INVALID_MAGIC,
  UNSUPPORTED_TRANSPORT_VERSION,
  INVALID_HEADER,
  UNKNOWN_MESSAGE_KIND,
  INVALID_SEQUENCE,
  REPLAYED_SEQUENCE,
  OUT_OF_ORDER_SEQUENCE,
  PAYLOAD_LIMIT_EXCEEDED,
  FRAME_SIZE_MISMATCH,
  PAYLOAD_DIGEST_MISMATCH,
  COUNT_LIMIT_EXCEEDED,
  DECODED_ALLOCATION_LIMIT_EXCEEDED,
  NON_CANONICAL_FLOAT,
  MALFORMED_PAYLOAD,
  PAYLOAD_VALIDATION_FAILED,
  RESOURCE_LIMIT_EXCEEDED,
  BLOB_LIMIT_EXCEEDED,
  REGISTRY_VALIDATION_FAILED,
  INVALID_UTF8,
  EVENT_ID_ORDER_VIOLATION,
  TIMESTAMP_ORDER_VIOLATION,
  CLOCK_DOMAIN_MISMATCH,
  RECONCILIATION_MISMATCH,
};

[[nodiscard]] bool
IsKnownRenderTransportMessageKind(RenderTransportMessageKind kind) noexcept;

/// Compatibility spelling retained for public transport callers. The digest
/// implementation and canonical API are renderer-neutral.
[[nodiscard]] inline RenderPayloadDigest ComputeRenderTransportPayloadDigest(
    const std::uint8_t *payload, std::size_t payload_size) noexcept {
  return ComputeRenderPayloadDigest(payload, payload_size);
}

struct RenderTransportEnvelopeEncodeResult {
  std::vector<std::uint8_t> bytes;
  RenderTransportStatus status = RenderTransportStatus::INVALID_ARGUMENT;

  [[nodiscard]] bool ok() const noexcept {
    return status == RenderTransportStatus::OK && !bytes.empty();
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Borrowed view valid only while the input frame remains alive and unchanged.
struct RenderTransportEnvelopeView {
  RenderTransportMessageKind kind =
      RenderTransportMessageKind::SCENE_SNAPSHOT_V5_CAMERA_V2;
  std::uint64_t sequence = 0U;
  const std::uint8_t *payload = nullptr;
  std::size_t payload_size = 0U;
};

/// Encodes one payload in the shared 64-byte envelope. Payload bytes are
/// opaque here; each pinned message kind owns its payload schema and cap.
[[nodiscard]] RenderTransportEnvelopeEncodeResult EncodeRenderTransportEnvelope(
    RenderTransportMessageKind kind, std::uint64_t sequence,
    const std::vector<std::uint8_t> &payload,
    std::uint64_t maximum_payload_bytes);

/// Validates framing, known kind, declared cap, exact size, and SHA-256 before
/// transactionally publishing a borrowed payload view. Ordering is checked by
/// RenderTransportSequenceState after a typed decoder validates its payload.
[[nodiscard]] RenderTransportStatus DecodeRenderTransportEnvelope(
    const std::vector<std::uint8_t> &frame,
    std::uint64_t maximum_payload_bytes,
    RenderTransportEnvelopeView &view) noexcept;

/// Ordered envelope sequence shared by typed decoders on one process bridge.
/// A standalone decoder owns one privately; an interleaved scene/asset stream
/// gives both decoders the same state. One caller serializes all operations.
class RenderTransportSequenceState final {
public:
  explicit RenderTransportSequenceState(
      std::uint64_t first_expected_sequence = 1U) noexcept
      : next_expected_sequence_(first_expected_sequence) {}

  RenderTransportSequenceState(const RenderTransportSequenceState &) = delete;
  RenderTransportSequenceState &
  operator=(const RenderTransportSequenceState &) = delete;
  RenderTransportSequenceState(RenderTransportSequenceState &&) = delete;
  RenderTransportSequenceState &operator=(RenderTransportSequenceState &&) =
      delete;
  ~RenderTransportSequenceState() = default;

  [[nodiscard]] RenderTransportStatus
  ValidateCandidate(std::uint64_t sequence) const noexcept;
  [[nodiscard]] bool CommitAccepted(std::uint64_t sequence) noexcept;

  [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept {
    return next_expected_sequence_;
  }
  [[nodiscard]] std::uint64_t last_accepted_sequence() const noexcept {
    return last_accepted_sequence_;
  }

private:
  std::uint64_t next_expected_sequence_ = 0U;
  std::uint64_t last_accepted_sequence_ = 0U;
};

} // namespace RoR::Render
