/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Owned native byte channel for one renderer bridge endpoint.

#pragma once

#include "RendererBridgeEndpoint.h"

#include <cstddef>
#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererBridgeChannelContractVersion = 1U;

enum class RendererBridgeChannelStatus : std::uint8_t {
  UNINITIALIZED = 0U,
  READY,
  CLOSED,
  PEER_CLOSED,
  REJECTED_INVALID_ENDPOINT,
  REJECTED_INVALID_ARGUMENT,
  REJECTED_NOT_READY,
  FAILED_INBOUND_HANDLE,
  FAILED_OUTBOUND_HANDLE,
  FAILED_HANDLE_HARDENING,
  FAILED_READ,
  FAILED_WRITE,
  FAILED_CLOSE,
  FAILED_INTERNAL,
};

struct RendererBridgeChannelResult final {
  std::uint32_t version = kRendererBridgeChannelContractVersion;
  RendererBridgeChannelStatus status =
      RendererBridgeChannelStatus::FAILED_INTERNAL;
  std::size_t bytes_transferred = 0U;
  std::uint32_t native_error_code = 0U;
  bool peer_closed = false;
  bool terminal = false;

  bool ok() const noexcept {
    return status == RendererBridgeChannelStatus::READY ||
           status == RendererBridgeChannelStatus::CLOSED;
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Adopts exactly the two inherited anonymous-pipe handles named by a decoded
/// endpoint. Adoption validates native pipe type/direction where the host API
/// exposes it, rejects nonblocking POSIX descriptors, and immediately removes
/// inheritance from both handles. Once Adopt() accepts a structurally valid
/// endpoint, this object owns both handles even if native validation fails.
/// Windows validates byte-stream and blocking mode on the inbound/read handle.
/// Its documented query APIs require read-attribute rights not promised for a
/// CreatePipe() write handle, and do not expose overlapped creation. The trusted
/// supervisor must therefore pass the exact write end from its restricted
/// inheritance allow-list.
/// Operations are blocking by default and one caller must serialize access to
/// the object. A live duplex session may explicitly switch only the outbound
/// half to zero-wait writes; inherited nonblocking state remains rejected.
/// status() describes the channel lifecycle: it remains READY while either
/// half can transfer, becomes CLOSED when neither half remains, and preserves
/// fatal failures. PEER_CLOSED is an operation result, and terminal() means a
/// fatal contract/I/O failure rather than an orderly full peer shutdown.
class RendererBridgeChannel final {
public:
  explicit RendererBridgeChannel(
      const RendererBridgeEndpoint &endpoint) noexcept;
  ~RendererBridgeChannel();

  RendererBridgeChannel(const RendererBridgeChannel &) = delete;
  RendererBridgeChannel &operator=(const RendererBridgeChannel &) = delete;
  RendererBridgeChannel(RendererBridgeChannel &&) = delete;
  RendererBridgeChannel &operator=(RendererBridgeChannel &&) = delete;

  RendererBridgeChannelResult Adopt() noexcept;
  /// Consume at most one immediately available chunk without changing the
  /// native pipe's blocking mode. READY with zero bytes means the inbound pipe
  /// is still open but has no bytes available at this instant.
  RendererBridgeChannelResult
  TryReadSome(std::uint8_t *bytes, std::size_t capacity) noexcept;
  RendererBridgeChannelResult
  ReadSome(std::uint8_t *bytes, std::size_t capacity) noexcept;
  /// Permanently changes only the owned outbound half to zero-wait mode. The
  /// endpoint is adopted in blocking mode first, so inherited state is still
  /// rejected deterministically. Call before TryWriteSome().
  RendererBridgeChannelResult
  EnableNonblockingOutbound() noexcept;
  /// Writes at most one immediately accepted chunk. READY with zero bytes
  /// means the peer is open but its pipe buffer has no capacity right now.
  RendererBridgeChannelResult
  TryWriteSome(const std::uint8_t *bytes, std::size_t size) noexcept;
  RendererBridgeChannelResult
  WriteAll(const std::uint8_t *bytes, std::size_t size) noexcept;
  RendererBridgeChannelResult CloseInbound() noexcept;
  RendererBridgeChannelResult CloseOutbound() noexcept;
  RendererBridgeChannelResult Close() noexcept;

  const RendererBridgeEndpoint &endpoint() const noexcept {
    return endpoint_;
  }
  RendererBridgeChannelStatus status() const noexcept {
    return status_;
  }
  bool adopted() const noexcept { return adopted_; }
  bool inbound_open() const noexcept { return inbound_open_; }
  bool outbound_open() const noexcept { return outbound_open_; }
  bool outbound_nonblocking() const noexcept {
    return outbound_nonblocking_;
  }
  bool terminal() const noexcept { return terminal_; }

private:
  RendererBridgeChannelResult MakeResult(
      RendererBridgeChannelStatus status, std::size_t bytes_transferred = 0U,
      std::uint32_t native_error_code = 0U,
      bool peer_closed = false) const noexcept;
  RendererBridgeChannelResult FailAndClose(
      RendererBridgeChannelStatus status,
      std::uint32_t native_error_code,
      std::size_t bytes_transferred = 0U) noexcept;
  std::uint32_t CloseInboundNative() noexcept;
  std::uint32_t CloseOutboundNative() noexcept;
  void CloseNoexcept() noexcept;
  void RefreshClosedStatus() noexcept;

  RendererBridgeEndpoint endpoint_;
  std::uint64_t inbound_native_handle_ = 0U;
  std::uint64_t outbound_native_handle_ = 0U;
  RendererBridgeChannelStatus status_ =
      RendererBridgeChannelStatus::UNINITIALIZED;
  bool adopted_ = false;
  bool inbound_open_ = false;
  bool outbound_open_ = false;
  bool outbound_nonblocking_ = false;
  bool terminal_ = false;
  std::size_t terminal_bytes_transferred_ = 0U;
  std::uint32_t terminal_native_error_code_ = 0U;
};

bool IsKnownRendererBridgeChannelStatus(
    RendererBridgeChannelStatus status) noexcept;
const char *ToString(RendererBridgeChannelStatus status) noexcept;

} // namespace RoR
