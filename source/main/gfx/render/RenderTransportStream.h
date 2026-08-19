/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Bounded incremental framing for render-bridge byte streams.

#pragma once

#include "RenderTransportEnvelope.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kRenderTransportStreamContractVersion = 1U;
constexpr std::uint64_t kRenderTransportStreamSceneMaximumPayloadBytes =
    64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kRenderTransportStreamInputMaximumPayloadBytes =
    4ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kRenderTransportStreamAssetMaximumPayloadBytes =
    640ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kRenderTransportStreamControlMaximumPayloadBytes =
    128ULL;
constexpr std::uint64_t kRenderTransportStreamAbsoluteMaximumPayloadBytes =
    kRenderTransportStreamAssetMaximumPayloadBytes;

enum class RenderTransportStreamStatus : std::uint8_t {
  NEED_MORE_DATA = 0U,
  FRAME_READY,
  CLOSED,
  REJECTED_INVALID_CONFIGURATION,
  REJECTED_INVALID_ARGUMENT,
  REJECTED_FRAME_PENDING,
  REJECTED_NO_FRAME,
  REJECTED_CLOSED,
  FAILED_HEADER,
  FAILED_ENVELOPE,
  FAILED_ALLOCATION,
  TRUNCATED_END_OF_STREAM,
  FAILED_INTERNAL,
};

struct RenderTransportStreamResult final {
  std::uint32_t version = kRenderTransportStreamContractVersion;
  RenderTransportStreamStatus status =
      RenderTransportStreamStatus::FAILED_INTERNAL;
  RenderTransportStatus transport_status =
      RenderTransportStatus::INVALID_ARGUMENT;
  std::size_t bytes_consumed = 0U;
  bool terminal = false;
};

struct RenderTransportStreamFrameResult final {
  std::uint32_t version = kRenderTransportStreamContractVersion;
  RenderTransportStreamStatus status =
      RenderTransportStreamStatus::REJECTED_NO_FRAME;
  RenderTransportMessageKind kind =
      RenderTransportMessageKind::SCENE_SNAPSHOT_V5_CAMERA_V2;
  std::uint64_t sequence = 0U;
  std::vector<std::uint8_t> bytes;

  [[nodiscard]] bool ok() const noexcept {
    return status == RenderTransportStreamStatus::FRAME_READY &&
           sequence != 0U && !bytes.empty();
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Incrementally reconstruct one envelope at a time from an arbitrary byte
/// stream. The first complete 64-byte header is validated before its declared
/// payload can reserve memory. A malformed frame permanently poisons this
/// instance because silently scanning for another magic value would hide loss
/// or corruption. Callers retain unconsumed input and must TakeFrame() before
/// supplying bytes from a coalesced following frame.
class RenderTransportStreamDecoder final {
public:
  explicit RenderTransportStreamDecoder(
      std::uint64_t maximum_payload_bytes) noexcept;

  RenderTransportStreamDecoder(const RenderTransportStreamDecoder &) = delete;
  RenderTransportStreamDecoder &
  operator=(const RenderTransportStreamDecoder &) = delete;
  RenderTransportStreamDecoder(RenderTransportStreamDecoder &&) = delete;
  RenderTransportStreamDecoder &
  operator=(RenderTransportStreamDecoder &&) = delete;
  ~RenderTransportStreamDecoder() = default;

  /// Consume at most the bytes belonging to the current frame. A zero-length
  /// call is a no-op; a null pointer is valid only for that zero-length case.
  [[nodiscard]] RenderTransportStreamResult
  Accept(const std::uint8_t *bytes, std::size_t size) noexcept;

  /// Mark the byte stream closed. A complete pending frame remains available;
  /// a partial header or payload becomes a terminal truncation.
  [[nodiscard]] RenderTransportStreamResult Finish() noexcept;

  /// Move out exactly one fully envelope-validated frame. If Finish() was
  /// already called, taking the pending frame transitions the decoder closed.
  [[nodiscard]] RenderTransportStreamFrameResult TakeFrame() noexcept;

  [[nodiscard]] RenderTransportStreamStatus status() const noexcept {
    return status_;
  }
  [[nodiscard]] RenderTransportStatus transport_status() const noexcept {
    return transport_status_;
  }
  [[nodiscard]] std::uint64_t maximum_payload_bytes() const noexcept {
    return maximum_payload_bytes_;
  }
  [[nodiscard]] std::uint64_t expected_frame_bytes() const noexcept {
    return expected_frame_bytes_;
  }
  [[nodiscard]] std::size_t buffered_bytes() const noexcept {
    return frame_.size();
  }
  [[nodiscard]] bool frame_ready() const noexcept {
    return status_ == RenderTransportStreamStatus::FRAME_READY;
  }
  [[nodiscard]] bool terminal() const noexcept { return terminal_; }

private:
  [[nodiscard]] RenderTransportStreamResult MakeResult(
      RenderTransportStreamStatus status, std::size_t consumed) const noexcept;
  [[nodiscard]] bool InspectCompleteHeader() noexcept;
  [[nodiscard]] bool ValidateCompleteFrame() noexcept;
  void Fail(RenderTransportStreamStatus status,
            RenderTransportStatus transport_status) noexcept;

  std::uint64_t maximum_payload_bytes_ = 0U;
  std::uint64_t expected_frame_bytes_ = 0U;
  RenderTransportMessageKind kind_ =
      RenderTransportMessageKind::SCENE_SNAPSHOT_V5_CAMERA_V2;
  std::uint64_t sequence_ = 0U;
  std::vector<std::uint8_t> frame_;
  RenderTransportStreamStatus status_ =
      RenderTransportStreamStatus::NEED_MORE_DATA;
  RenderTransportStatus transport_status_ = RenderTransportStatus::OK;
  bool input_closed_ = false;
  bool terminal_ = false;
};

[[nodiscard]] bool IsKnownRenderTransportStreamStatus(
    RenderTransportStreamStatus status) noexcept;
[[nodiscard]] const char *
ToString(RenderTransportStreamStatus status) noexcept;

} // namespace RoR::Render
