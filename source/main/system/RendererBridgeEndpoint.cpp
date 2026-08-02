/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBridgeEndpoint.h"

#include <climits>
#include <cstddef>
#include <limits>

namespace RoR {
namespace {

constexpr std::size_t kBridgePrefixRecords = 6U;

template <typename Character>
bool HasValidArguments(int argc, const Character *const argv[]) {
  if (argc < 1 || argv == nullptr) {
    return false;
  }
  for (int index = 0; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return false;
    }
  }
  return true;
}

template <typename Character>
bool EqualsAscii(const Character *value, const char *expected) {
  if (value == nullptr || expected == nullptr) {
    return false;
  }
  while (*expected != '\0') {
    if (*value !=
        static_cast<Character>(static_cast<unsigned char>(*expected))) {
      return false;
    }
    ++value;
    ++expected;
  }
  return *value == static_cast<Character>('\0');
}

template <typename Character>
bool StartsWithAscii(const Character *value, const char *prefix) {
  if (value == nullptr || prefix == nullptr) {
    return false;
  }
  while (*prefix != '\0') {
    if (*value !=
        static_cast<Character>(static_cast<unsigned char>(*prefix))) {
      return false;
    }
    ++value;
    ++prefix;
  }
  return true;
}

template <typename Character>
const Character *ValueAfterAsciiPrefix(const Character *value,
                                       const char *prefix) {
  if (!StartsWithAscii(value, prefix)) {
    return nullptr;
  }
  while (*prefix != '\0') {
    ++value;
    ++prefix;
  }
  return value;
}

RendererChildLauncherString NativeAscii(const char *value) {
  RendererChildLauncherString result;
  while (value != nullptr && *value != '\0') {
    result.push_back(static_cast<RendererChildLauncherChar>(
        static_cast<unsigned char>(*value)));
    ++value;
  }
  return result;
}

HostRenderPlatform CompileTimeHostPlatform() noexcept {
#if defined(_WIN32)
  return HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return HostRenderPlatform::MACOS;
#elif defined(__linux__)
  return HostRenderPlatform::LINUX;
#else
  return HostRenderPlatform::UNKNOWN;
#endif
}

const char *PlatformValue(HostRenderPlatform platform) noexcept {
  switch (platform) {
  case HostRenderPlatform::MACOS:
    return "macos";
  case HostRenderPlatform::WINDOWS:
    return "windows";
  case HostRenderPlatform::LINUX:
    return "linux";
  case HostRenderPlatform::UNKNOWN:
    break;
  }
  return nullptr;
}

const char *RoleValue(RendererBridgeRole role) noexcept {
  switch (role) {
  case RendererBridgeRole::GAME_HOST:
    return "game-host";
  case RendererBridgeRole::PRESENTATION_FRONTEND:
    return "presentation-frontend";
  }
  return nullptr;
}

bool ParsePlatformValue(const RendererChildLauncherChar *value,
                        HostRenderPlatform &platform) noexcept {
  if (EqualsAscii(value, "macos")) {
    platform = HostRenderPlatform::MACOS;
    return true;
  }
  if (EqualsAscii(value, "windows")) {
    platform = HostRenderPlatform::WINDOWS;
    return true;
  }
  if (EqualsAscii(value, "linux")) {
    platform = HostRenderPlatform::LINUX;
    return true;
  }
  return false;
}

bool ParseRoleValue(const RendererChildLauncherChar *value,
                    RendererBridgeRole &role) noexcept {
  if (EqualsAscii(value, "game-host")) {
    role = RendererBridgeRole::GAME_HOST;
    return true;
  }
  if (EqualsAscii(value, "presentation-frontend")) {
    role = RendererBridgeRole::PRESENTATION_FRONTEND;
    return true;
  }
  return false;
}

char HexDigit(std::uint8_t value) noexcept {
  return value < 10U ? static_cast<char>('0' + value)
                     : static_cast<char>('a' + value - 10U);
}

int HexValue(RendererChildLauncherChar value) noexcept {
  if (value >= static_cast<RendererChildLauncherChar>('0') &&
      value <= static_cast<RendererChildLauncherChar>('9')) {
    return static_cast<int>(value -
                            static_cast<RendererChildLauncherChar>('0'));
  }
  if (value >= static_cast<RendererChildLauncherChar>('a') &&
      value <= static_cast<RendererChildLauncherChar>('f')) {
    return static_cast<int>(value -
                            static_cast<RendererChildLauncherChar>('a')) +
           10;
  }
  return -1;
}

RendererChildLauncherString EncodeSession(
    const RendererBridgeSessionId &session) {
  RendererChildLauncherString result;
  result.reserve(session.size() * 2U);
  for (const std::uint8_t byte : session) {
    result.push_back(static_cast<RendererChildLauncherChar>(
        HexDigit(static_cast<std::uint8_t>(byte >> 4U))));
    result.push_back(static_cast<RendererChildLauncherChar>(
        HexDigit(static_cast<std::uint8_t>(byte & 0x0fU))));
  }
  return result;
}

RendererChildLauncherString EncodeHandle(std::uint64_t handle) {
  RendererChildLauncherString result(16U,
                                     RendererChildLauncherChar{});
  for (std::size_t index = 0U; index < result.size(); ++index) {
    const std::size_t shift = (result.size() - index - 1U) * 4U;
    result[index] = static_cast<RendererChildLauncherChar>(
        HexDigit(static_cast<std::uint8_t>((handle >> shift) & 0x0fU)));
  }
  return result;
}

bool ParseSession(const RendererChildLauncherChar *value,
                  RendererBridgeSessionId &session) noexcept {
  if (value == nullptr) {
    return false;
  }
  bool any_nonzero = false;
  for (std::size_t index = 0U; index < session.size(); ++index) {
    const int high = HexValue(value[index * 2U]);
    if (high < 0) {
      return false;
    }
    const int low = HexValue(value[index * 2U + 1U]);
    if (low < 0) {
      return false;
    }
    session[index] = static_cast<std::uint8_t>((high << 4U) | low);
    any_nonzero = any_nonzero || session[index] != 0U;
  }
  return value[session.size() * 2U] ==
             static_cast<RendererChildLauncherChar>('\0') &&
         any_nonzero;
}

std::uint64_t MaximumNativeHandleToken() noexcept {
#if defined(_WIN32)
  // INVALID_HANDLE_VALUE is all bits set and must never be adopted.
  return static_cast<std::uint64_t>(
             (std::numeric_limits<std::uintptr_t>::max)()) -
         1U;
#else
  // POSIX file descriptors are signed int values even on a 64-bit host.
  return static_cast<std::uint64_t>(INT_MAX);
#endif
}

bool ParseHandle(const RendererChildLauncherChar *value,
                 std::uint64_t &handle) noexcept {
  if (value == nullptr) {
    return false;
  }
  std::uint64_t candidate = 0U;
  for (std::size_t index = 0U; index < 16U; ++index) {
    const int digit = HexValue(value[index]);
    if (digit < 0) {
      return false;
    }
    candidate = (candidate << 4U) | static_cast<std::uint64_t>(digit);
  }
  if (value[16U] != static_cast<RendererChildLauncherChar>('\0') ||
      candidate < 3U ||
      candidate > MaximumNativeHandleToken()) {
    return false;
  }
  handle = candidate;
  return true;
}

bool IsValidEndpoint(const RendererBridgeEndpoint &endpoint) noexcept {
  if (endpoint.version != kRendererBridgeEndpointArgvContractVersion ||
      endpoint.platform != CompileTimeHostPlatform() ||
      !IsKnownRendererBridgeRole(endpoint.role) ||
      endpoint.inbound_native_handle < 3U ||
      endpoint.outbound_native_handle < 3U ||
      endpoint.inbound_native_handle == endpoint.outbound_native_handle ||
      endpoint.inbound_native_handle > MaximumNativeHandleToken() ||
      endpoint.outbound_native_handle > MaximumNativeHandleToken()) {
    return false;
  }
  for (const std::uint8_t byte : endpoint.session_id) {
    if (byte != 0U) {
      return true;
    }
  }
  return false;
}

} // namespace

RendererBridgeEndpointArgvEncoding EncodeRendererBridgeEndpoint(
    const RendererBridgeEndpoint &endpoint, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept {
  RendererBridgeEndpointArgvEncoding result;
  try {
    if (!IsValidEndpoint(endpoint)) {
      return result;
    }
    result.status =
        RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ARGUMENTS;
    if (!HasValidArguments(argc, argv) || argv[0][0] == 0 ||
        argc > (std::numeric_limits<int>::max)() -
                   static_cast<int>(kBridgePrefixRecords)) {
      return result;
    }
    for (int index = 1; index < argc; ++index) {
      if (StartsWithAscii(argv[index], "--ror-render-bridge-")) {
        return result;
      }
    }
    const char *platform = PlatformValue(endpoint.platform);
    const char *role = RoleValue(endpoint.role);
    if (platform == nullptr || role == nullptr) {
      result.status =
          RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ENDPOINT;
      return result;
    }

    result.endpoint = endpoint;
    result.arguments.reserve(static_cast<std::size_t>(argc) +
                             kBridgePrefixRecords);
    result.arguments.emplace_back(argv[0]);
    result.arguments.push_back(
        NativeAscii("--ror-render-bridge-version=1"));
    result.arguments.push_back(NativeAscii("--ror-render-bridge-role=") +
                               NativeAscii(role));
    result.arguments.push_back(
        NativeAscii("--ror-render-bridge-platform=") +
        NativeAscii(platform));
    result.arguments.push_back(
        NativeAscii("--ror-render-bridge-session=") +
        EncodeSession(endpoint.session_id));
    result.arguments.push_back(
        NativeAscii("--ror-render-bridge-inbound=") +
        EncodeHandle(endpoint.inbound_native_handle));
    result.arguments.push_back(
        NativeAscii("--ror-render-bridge-outbound=") +
        EncodeHandle(endpoint.outbound_native_handle));
    for (int index = 1; index < argc; ++index) {
      result.arguments.emplace_back(argv[index]);
    }
    result.status = RendererBridgeEndpointArgvStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    result.arguments.clear();
    result.accepted = false;
    result.status = RendererBridgeEndpointArgvStatus::FAILED_INTERNAL;
    return result;
  }
}

RendererBridgeEndpointArgvParseResult ParseRendererBridgeEndpoint(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept {
  RendererBridgeEndpointArgvParseResult result;
  try {
    const HostRenderPlatform host = CompileTimeHostPlatform();
    if (!IsKnownHostRenderPlatform(host) ||
        host == HostRenderPlatform::UNKNOWN) {
      result.status =
          RendererBridgeEndpointArgvStatus::REJECTED_INVALID_PLATFORM;
      return result;
    }
    if (!HasValidArguments(argc, argv) || argv[0][0] == 0) {
      return result;
    }
    if (argc < static_cast<int>(kBridgePrefixRecords + 1U) ||
        !EqualsAscii(argv[1], "--ror-render-bridge-version=1")) {
      result.status =
          RendererBridgeEndpointArgvStatus::REJECTED_MISSING_CONTRACT;
      return result;
    }

    static const char role_prefix[] = "--ror-render-bridge-role=";
    static const char platform_prefix[] =
        "--ror-render-bridge-platform=";
    static const char session_prefix[] =
        "--ror-render-bridge-session=";
    static const char inbound_prefix[] =
        "--ror-render-bridge-inbound=";
    static const char outbound_prefix[] =
        "--ror-render-bridge-outbound=";
    const RendererChildLauncherChar *role =
        ValueAfterAsciiPrefix(argv[2], role_prefix);
    const RendererChildLauncherChar *platform =
        ValueAfterAsciiPrefix(argv[3], platform_prefix);
    const RendererChildLauncherChar *session =
        ValueAfterAsciiPrefix(argv[4], session_prefix);
    const RendererChildLauncherChar *inbound =
        ValueAfterAsciiPrefix(argv[5], inbound_prefix);
    const RendererChildLauncherChar *outbound =
        ValueAfterAsciiPrefix(argv[6], outbound_prefix);
    result.endpoint.version = kRendererBridgeEndpointArgvContractVersion;
    if (role == nullptr || platform == nullptr || session == nullptr ||
        inbound == nullptr || outbound == nullptr ||
        !ParseRoleValue(role, result.endpoint.role) ||
        !ParsePlatformValue(platform, result.endpoint.platform) ||
        result.endpoint.platform != host ||
        !ParseSession(session, result.endpoint.session_id) ||
        !ParseHandle(inbound, result.endpoint.inbound_native_handle) ||
        !ParseHandle(outbound, result.endpoint.outbound_native_handle) ||
        result.endpoint.inbound_native_handle ==
            result.endpoint.outbound_native_handle ||
        !IsValidEndpoint(result.endpoint)) {
      result.status =
          RendererBridgeEndpointArgvStatus::REJECTED_MALFORMED_CONTRACT;
      return result;
    }
    for (int index =
             static_cast<int>(kBridgePrefixRecords + 1U);
         index < argc; ++index) {
      if (StartsWithAscii(argv[index], "--ror-render-bridge-")) {
        result.status =
            RendererBridgeEndpointArgvStatus::REJECTED_MALFORMED_CONTRACT;
        return result;
      }
    }
    result.forwarded_arguments.reserve(
        static_cast<std::size_t>(argc) - kBridgePrefixRecords);
    result.forwarded_arguments.emplace_back(argv[0]);
    for (int index = static_cast<int>(kBridgePrefixRecords + 1U);
         index < argc; ++index) {
      result.forwarded_arguments.emplace_back(argv[index]);
    }
    result.status = RendererBridgeEndpointArgvStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    result.forwarded_arguments.clear();
    result.accepted = false;
    result.status = RendererBridgeEndpointArgvStatus::FAILED_INTERNAL;
    return result;
  }
}

bool IsValidRendererBridgeEndpoint(
    const RendererBridgeEndpoint &endpoint) noexcept {
  return IsValidEndpoint(endpoint);
}

bool IsKnownRendererBridgeRole(RendererBridgeRole role) noexcept {
  switch (role) {
  case RendererBridgeRole::GAME_HOST:
  case RendererBridgeRole::PRESENTATION_FRONTEND:
    return true;
  }
  return false;
}

bool IsKnownRendererBridgeEndpointArgvStatus(
    RendererBridgeEndpointArgvStatus status) noexcept {
  switch (status) {
  case RendererBridgeEndpointArgvStatus::READY:
  case RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ENDPOINT:
  case RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ARGUMENTS:
  case RendererBridgeEndpointArgvStatus::REJECTED_INVALID_PLATFORM:
  case RendererBridgeEndpointArgvStatus::REJECTED_MISSING_CONTRACT:
  case RendererBridgeEndpointArgvStatus::REJECTED_MALFORMED_CONTRACT:
  case RendererBridgeEndpointArgvStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererBridgeRole role) noexcept {
  const char *value = RoleValue(role);
  return value == nullptr ? "invalid" : value;
}

const char *ToString(RendererBridgeEndpointArgvStatus status) noexcept {
  switch (status) {
  case RendererBridgeEndpointArgvStatus::READY:
    return "ready";
  case RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ENDPOINT:
    return "rejected-invalid-endpoint";
  case RendererBridgeEndpointArgvStatus::REJECTED_INVALID_ARGUMENTS:
    return "rejected-invalid-arguments";
  case RendererBridgeEndpointArgvStatus::REJECTED_INVALID_PLATFORM:
    return "rejected-invalid-platform";
  case RendererBridgeEndpointArgvStatus::REJECTED_MISSING_CONTRACT:
    return "rejected-missing-contract";
  case RendererBridgeEndpointArgvStatus::REJECTED_MALFORMED_CONTRACT:
    return "rejected-malformed-contract";
  case RendererBridgeEndpointArgvStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
