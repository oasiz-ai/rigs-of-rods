/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free, versioned supervisor-to-bridge endpoint protocol.

#pragma once

#include "RendererChildIntent.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR {

/// Exact native-argv contract used by the future two-process render bridge.
/// It is independent of the renderer-selection intent version because the
/// compatibility game host and Ogre-Next presentation child both consume it.
constexpr std::uint32_t kRendererBridgeEndpointArgvContractVersion = 1U;
constexpr std::size_t kRendererBridgeEndpointArgvRecordCount = 6U;

enum class RendererBridgeRole : std::uint8_t {
  GAME_HOST = 0U,
  PRESENTATION_FRONTEND = 1U,
};

enum class RendererBridgeEndpointArgvStatus : std::uint8_t {
  READY = 0U,
  REJECTED_INVALID_ENDPOINT,
  REJECTED_INVALID_ARGUMENTS,
  REJECTED_INVALID_PLATFORM,
  REJECTED_MISSING_CONTRACT,
  REJECTED_MALFORMED_CONTRACT,
  FAILED_INTERNAL,
};

/// One supervisor-created session identifier. The all-zero value is invalid.
/// This binds the two inherited byte streams to one launch transaction; it is
/// not an authentication token against another local process.
using RendererBridgeSessionId = std::array<std::uint8_t, 16U>;

struct RendererBridgeEndpoint final {
  std::uint32_t version = kRendererBridgeEndpointArgvContractVersion;
  HostRenderPlatform platform = HostRenderPlatform::UNKNOWN;
  RendererBridgeRole role = RendererBridgeRole::GAME_HOST;
  RendererBridgeSessionId session_id{};
  std::uint64_t inbound_native_handle = 0U;
  std::uint64_t outbound_native_handle = 0U;
};

struct RendererBridgeEndpointArgvEncoding final {
  std::uint32_t version = kRendererBridgeEndpointArgvContractVersion;
  RendererBridgeEndpointArgvStatus status =
      RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ENDPOINT;
  RendererBridgeEndpoint endpoint;
  std::vector<RendererChildLauncherString> arguments;
  bool accepted = false;
};

struct RendererBridgeEndpointArgvParseResult final {
  std::uint32_t version = kRendererBridgeEndpointArgvContractVersion;
  RendererBridgeEndpointArgvStatus status =
      RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ARGUMENTS;
  RendererBridgeEndpoint endpoint;
  std::vector<RendererChildLauncherString> forwarded_arguments;
  bool accepted = false;
};

/// Insert the exact six-record bridge prefix after argv[0]. For the modern
/// child, call this first and then EncodeRendererOgreNextChildIntent(); the
/// renderer-intent decoder will expose this bridge prefix as its game suffix.
RendererBridgeEndpointArgvEncoding EncodeRendererBridgeEndpoint(
    const RendererBridgeEndpoint &endpoint, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept;

/// Decode and remove only the exact six-record bridge prefix. Unknown roles,
/// foreign platforms, non-canonical hex, zero/equal/host-width-invalid native
/// handles, an all-zero session, and duplicate reserved suffix records fail
/// before either child adopts an inherited OS resource.
RendererBridgeEndpointArgvParseResult ParseRendererBridgeEndpoint(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept;

bool IsKnownRendererBridgeRole(RendererBridgeRole role) noexcept;
bool IsValidRendererBridgeEndpoint(
    const RendererBridgeEndpoint &endpoint) noexcept;
bool IsKnownRendererBridgeEndpointArgvStatus(
    RendererBridgeEndpointArgvStatus status) noexcept;
const char *ToString(RendererBridgeRole role) noexcept;
const char *ToString(RendererBridgeEndpointArgvStatus status) noexcept;

} // namespace RoR
