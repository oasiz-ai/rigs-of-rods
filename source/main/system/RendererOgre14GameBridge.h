/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Early OGRE 14 game-host adoption of a supervisor render bridge.

#pragma once

#include "RendererBridgeChannel.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR {

constexpr std::uint32_t kRendererOgre14GameBridgeContractVersion = 1U;

enum class RendererOgre14GameBridgeStatus : std::uint8_t {
  LEGACY_DIRECT = 0U,
  READY,
  REJECTED_INVALID_ARGUMENT_VECTOR,
  REJECTED_MALFORMED_ENDPOINT,
  REJECTED_WRONG_ROLE,
  REJECTED_NOT_READY,
  FAILED_CHANNEL_ADOPTION,
  FAILED_INTERNAL,
};

struct RendererOgre14GameBridgeResult final {
  std::uint32_t version = kRendererOgre14GameBridgeContractVersion;
  RendererOgre14GameBridgeStatus status =
      RendererOgre14GameBridgeStatus::FAILED_INTERNAL;
  RendererBridgeEndpointArgvStatus endpoint_status =
      RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ARGUMENTS;
  RendererBridgeChannelResult channel;
  bool accepted = false;
  bool active = false;

  bool ok() const noexcept { return accepted; }
  explicit operator bool() const noexcept { return ok(); }
};

/// Parses the optional bridge contract before any game worker is created. A
/// direct legacy invocation is accepted unchanged. If argv[1] begins with the
/// reserved bridge prefix, the complete endpoint must decode as a same-host
/// GAME_HOST endpoint and its two native handles are adopted immediately.
///
/// The stripped argument buffers are owned by this object and remain mutable
/// and stable until Close()/destruction, matching the legacy command-line API.
/// The channel also remains owned for the complete game-host lifetime so later
/// scene/input bridge layers can use it without re-decoding native handles.
class RendererOgre14GameBridge final {
public:
  RendererOgre14GameBridge() = default;
  ~RendererOgre14GameBridge();

  RendererOgre14GameBridge(const RendererOgre14GameBridge &) = delete;
  RendererOgre14GameBridge &
  operator=(const RendererOgre14GameBridge &) = delete;
  RendererOgre14GameBridge(RendererOgre14GameBridge &&) = delete;
  RendererOgre14GameBridge &operator=(RendererOgre14GameBridge &&) = delete;

  RendererOgre14GameBridgeResult
  Initialize(int argc, char **argv) noexcept;
  RendererBridgeChannelResult Close() noexcept;

  RendererOgre14GameBridgeStatus status() const noexcept { return status_; }
  bool active() const noexcept { return active_; }
  int forwarded_argc() const noexcept { return forwarded_argc_; }
  char **forwarded_argv() noexcept { return forwarded_argv_; }
  const RendererBridgeEndpoint *endpoint() const noexcept;
  RendererBridgeChannel *channel() noexcept { return channel_.get(); }
  const RendererBridgeChannel *channel() const noexcept {
    return channel_.get();
  }

private:
  bool StoreForwardedArguments(
      const std::vector<RendererChildLauncherString> &arguments,
      int original_argc, char *const original_argv[]) noexcept;
  void ClearForwardedArguments() noexcept;

  RendererOgre14GameBridgeStatus status_ =
      RendererOgre14GameBridgeStatus::FAILED_INTERNAL;
  RendererBridgeEndpoint endpoint_;
  std::unique_ptr<RendererBridgeChannel> channel_;
  std::vector<std::vector<char>> forwarded_storage_;
  std::vector<char *> forwarded_pointers_;
  char **forwarded_argv_ = nullptr;
  int forwarded_argc_ = 0;
  bool initialized_ = false;
  bool active_ = false;
};

bool IsKnownRendererOgre14GameBridgeStatus(
    RendererOgre14GameBridgeStatus status) noexcept;
const char *ToString(RendererOgre14GameBridgeStatus status) noexcept;

} // namespace RoR
