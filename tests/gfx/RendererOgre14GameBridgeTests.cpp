/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgre14GameBridge.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer OGRE 14 game bridge test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::HostRenderPlatform CurrentPlatform() {
#if defined(_WIN32)
  return RoR::HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return RoR::HostRenderPlatform::MACOS;
#elif defined(__linux__)
  return RoR::HostRenderPlatform::LINUX;
#else
  return RoR::HostRenderPlatform::UNKNOWN;
#endif
}

#if defined(_WIN32)

using NativeHandle = HANDLE;
const NativeHandle kInvalidNativeHandle = INVALID_HANDLE_VALUE;

struct NativePipe final {
  NativeHandle read_handle = kInvalidNativeHandle;
  NativeHandle write_handle = kInvalidNativeHandle;
};

NativePipe MakePipe() {
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  NativePipe result;
  Require(::CreatePipe(&result.read_handle, &result.write_handle, &security,
                       0U) != FALSE,
          "CreatePipe failed");
  return result;
}

std::uint64_t NativeToken(NativeHandle handle) {
  return static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(handle));
}

bool IsOpen(NativeHandle handle) {
  DWORD flags = 0U;
  return handle != nullptr && handle != kInvalidNativeHandle &&
         ::GetHandleInformation(handle, &flags) != FALSE;
}

bool IsInheritable(NativeHandle handle) {
  DWORD flags = 0U;
  Require(::GetHandleInformation(handle, &flags) != FALSE,
          "GetHandleInformation failed");
  return (flags & HANDLE_FLAG_INHERIT) != 0U;
}

void CloseNative(NativeHandle &handle) {
  if (handle == nullptr || handle == kInvalidNativeHandle) {
    return;
  }
  Require(::CloseHandle(handle) != FALSE, "CloseHandle failed");
  handle = kInvalidNativeHandle;
}

void WriteByte(NativeHandle handle, std::uint8_t value) {
  DWORD transferred = 0U;
  Require(::WriteFile(handle, &value, 1U, &transferred, nullptr) != FALSE &&
              transferred == 1U,
          "WriteFile failed");
}

std::uint8_t ReadByte(NativeHandle handle) {
  std::uint8_t value = 0U;
  DWORD transferred = 0U;
  Require(::ReadFile(handle, &value, 1U, &transferred, nullptr) != FALSE &&
              transferred == 1U,
          "ReadFile failed");
  return value;
}

NativeHandle MakeNonPipe() {
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  NativeHandle result = ::CreateEventW(&security, TRUE, FALSE, nullptr);
  Require(result != nullptr, "CreateEventW failed");
  return result;
}

#else

using NativeHandle = int;
const NativeHandle kInvalidNativeHandle = -1;

struct NativePipe final {
  NativeHandle read_handle = kInvalidNativeHandle;
  NativeHandle write_handle = kInvalidNativeHandle;
};

int PromoteReservedDescriptor(int descriptor) {
  if (descriptor >= 3) {
    return descriptor;
  }
  errno = 0;
  const int promoted = ::fcntl(descriptor, F_DUPFD, 3);
  Require(promoted >= 3, "could not promote reserved descriptor");
  Require(::close(descriptor) == 0,
          "could not close reserved descriptor");
  return promoted;
}

NativePipe MakePipe() {
  int descriptors[2] = {-1, -1};
  Require(::pipe(descriptors) == 0, "pipe failed");
  NativePipe result;
  result.read_handle = PromoteReservedDescriptor(descriptors[0]);
  result.write_handle = PromoteReservedDescriptor(descriptors[1]);
  return result;
}

std::uint64_t NativeToken(NativeHandle handle) {
  return static_cast<std::uint64_t>(handle);
}

bool IsOpen(NativeHandle handle) {
  errno = 0;
  return handle >= 0 && ::fcntl(handle, F_GETFD) >= 0;
}

bool IsInheritable(NativeHandle handle) {
  errno = 0;
  const int flags = ::fcntl(handle, F_GETFD);
  Require(flags >= 0, "fcntl(F_GETFD) failed");
  return (flags & FD_CLOEXEC) == 0;
}

void CloseNative(NativeHandle &handle) {
  if (handle < 0) {
    return;
  }
  errno = 0;
  Require(::close(handle) == 0, "close failed");
  handle = kInvalidNativeHandle;
}

void WriteByte(NativeHandle handle, std::uint8_t value) {
  ssize_t transferred = -1;
  do {
    errno = 0;
    transferred = ::write(handle, &value, 1U);
  } while (transferred < 0 && errno == EINTR);
  Require(transferred == 1, "write failed");
}

std::uint8_t ReadByte(NativeHandle handle) {
  std::uint8_t value = 0U;
  ssize_t transferred = -1;
  do {
    errno = 0;
    transferred = ::read(handle, &value, 1U);
  } while (transferred < 0 && errno == EINTR);
  Require(transferred == 1, "read failed");
  return value;
}

NativeHandle MakeNonPipe() {
  NativeHandle result = ::open("/dev/null", O_RDONLY);
  Require(result >= 0, "open(/dev/null) failed");
  return PromoteReservedDescriptor(result);
}

#endif

RoR::RendererBridgeEndpoint MakeEndpoint(RoR::RendererBridgeRole role,
                                         NativeHandle inbound,
                                         NativeHandle outbound) {
  RoR::RendererBridgeEndpoint endpoint;
  endpoint.platform = CurrentPlatform();
  endpoint.role = role;
  for (std::size_t index = 0U; index < endpoint.session_id.size(); ++index) {
    endpoint.session_id[index] = static_cast<std::uint8_t>(0xa0U + index);
  }
  endpoint.inbound_native_handle = NativeToken(inbound);
  endpoint.outbound_native_handle = NativeToken(outbound);
  return endpoint;
}

RoR::RendererChildLauncherString NativeArgument(const std::string &value) {
  RoR::RendererChildLauncherString result;
  for (const char character : value) {
    const unsigned char byte = static_cast<unsigned char>(character);
    result.push_back(static_cast<RoR::RendererChildLauncherChar>(byte));
  }
  return result;
}

std::vector<const RoR::RendererChildLauncherChar *> NativePointers(
    const std::vector<RoR::RendererChildLauncherString> &arguments) {
  std::vector<const RoR::RendererChildLauncherChar *> result;
  result.reserve(arguments.size());
  for (const RoR::RendererChildLauncherString &argument : arguments) {
    result.push_back(argument.c_str());
  }
  return result;
}

std::vector<std::string> NarrowArguments(
    const std::vector<RoR::RendererChildLauncherString> &arguments) {
  std::vector<std::string> result;
  result.reserve(arguments.size());
  for (const RoR::RendererChildLauncherString &argument : arguments) {
    std::string narrowed;
    for (const RoR::RendererChildLauncherChar character : argument) {
#if defined(_WIN32)
      const std::uint32_t value = static_cast<std::uint32_t>(character);
      Require(value <= 0xffU, "encoded bridge argument was not byte-valued");
      narrowed.push_back(static_cast<char>(value));
#else
      narrowed.push_back(static_cast<char>(
          static_cast<unsigned char>(character)));
#endif
    }
    result.push_back(std::move(narrowed));
  }
  return result;
}

struct MutableArguments final {
  std::vector<std::vector<char>> storage;
  std::vector<char *> pointers;

  explicit MutableArguments(const std::vector<std::string> &arguments) {
    storage.reserve(arguments.size());
    for (const std::string &argument : arguments) {
      storage.emplace_back(argument.begin(), argument.end());
      storage.back().push_back('\0');
    }
    pointers.reserve(storage.size());
    for (std::vector<char> &argument : storage) {
      pointers.push_back(argument.data());
    }
  }
};

MutableArguments Encode(
    const RoR::RendererBridgeEndpoint &endpoint,
    const std::vector<std::string> &game_arguments) {
  std::vector<RoR::RendererChildLauncherString> native;
  native.reserve(game_arguments.size());
  for (const std::string &argument : game_arguments) {
    native.push_back(NativeArgument(argument));
  }
  const auto pointers = NativePointers(native);
  const RoR::RendererBridgeEndpointArgvEncoding encoded =
      RoR::EncodeRendererBridgeEndpoint(
          endpoint, static_cast<int>(pointers.size()), pointers.data());
  Require(encoded.accepted, "bridge endpoint encoder rejected test input");
  return MutableArguments(NarrowArguments(encoded.arguments));
}

void ClosePipe(NativePipe &pipe) {
  CloseNative(pipe.read_handle);
  CloseNative(pipe.write_handle);
}

void TestStatusContract() {
  Require(RoR::kRendererOgre14GameBridgeContractVersion == 1U,
          "game bridge contract version changed");
  Require(RoR::kRendererBridgeEndpointArgvRecordCount == 6U,
          "bridge endpoint record count changed");
  for (int value = 0;
       value <= static_cast<int>(
                    RoR::RendererOgre14GameBridgeStatus::FAILED_INTERNAL);
       ++value) {
    const auto status =
        static_cast<RoR::RendererOgre14GameBridgeStatus>(value);
    Require(RoR::IsKnownRendererOgre14GameBridgeStatus(status) &&
                std::string(RoR::ToString(status)) != "invalid",
            "known game bridge status was omitted");
  }
  Require(!RoR::IsKnownRendererOgre14GameBridgeStatus(
              static_cast<RoR::RendererOgre14GameBridgeStatus>(255U)) &&
              std::string(RoR::ToString(
                  static_cast<RoR::RendererOgre14GameBridgeStatus>(255U))) ==
                  "invalid",
          "unknown game bridge status was accepted");
  RoR::RendererOgre14GameBridgeResult result;
  Require(result.version == RoR::kRendererOgre14GameBridgeContractVersion &&
              !result.ok(),
          "default game bridge result changed");
}

void TestLegacyArgumentsRemainUnchanged() {
  const std::vector<std::string> values{
      "RoR-Ogre14", "-map", "City World",
      "--ror-render-bridge-version=1"};
  MutableArguments arguments(values);
  RoR::RendererOgre14GameBridge bridge;
  const auto result = bridge.Initialize(
      static_cast<int>(arguments.pointers.size()), arguments.pointers.data());
  Require(result.accepted && !result.active &&
              result.status ==
                  RoR::RendererOgre14GameBridgeStatus::LEGACY_DIRECT &&
              bridge.forwarded_argc() == static_cast<int>(values.size()) &&
              bridge.forwarded_argv() == arguments.pointers.data() &&
              bridge.endpoint() == nullptr && bridge.channel() == nullptr,
          "legacy invocation did not pass through exactly");
  Require(bridge.Initialize(1, arguments.pointers.data()).status ==
              RoR::RendererOgre14GameBridgeStatus::REJECTED_NOT_READY,
          "game bridge initialized twice");

  RoR::RendererOgre14GameBridge invalid;
  Require(invalid.Initialize(0, nullptr).status ==
              RoR::RendererOgre14GameBridgeStatus::
                  REJECTED_INVALID_ARGUMENT_VECTOR,
          "invalid argument vector was accepted");
}

void TestValidGameHostAdoptionAndOwnedSuffix() {
  NativePipe inbound = MakePipe();
  NativePipe outbound = MakePipe();
  const NativeHandle adopted_inbound = inbound.read_handle;
  const NativeHandle adopted_outbound = outbound.write_handle;
  const RoR::RendererBridgeEndpoint endpoint = MakeEndpoint(
      RoR::RendererBridgeRole::GAME_HOST, adopted_inbound, adopted_outbound);
  const std::vector<std::string> game_arguments{
      "RoR-Ogre14", "-map", "City World", std::string("utf8-\xc3\xa9"), ""};
  MutableArguments arguments = Encode(endpoint, game_arguments);

  RoR::RendererOgre14GameBridge bridge;
  const auto result = bridge.Initialize(
      static_cast<int>(arguments.pointers.size()), arguments.pointers.data());
  Require(result.accepted && result.active &&
              result.status == RoR::RendererOgre14GameBridgeStatus::READY &&
              result.endpoint_status ==
                  RoR::RendererBridgeEndpointArgvStatus::READY &&
              result.channel.status ==
                  RoR::RendererBridgeChannelStatus::READY &&
              bridge.active() && bridge.channel() != nullptr &&
              bridge.endpoint() != nullptr &&
              bridge.endpoint()->role == RoR::RendererBridgeRole::GAME_HOST &&
              bridge.endpoint()->session_id == endpoint.session_id &&
              bridge.forwarded_argc() ==
                  static_cast<int>(game_arguments.size()),
          "valid game-host endpoint was not adopted");
  Require(!IsInheritable(adopted_inbound) &&
              !IsInheritable(adopted_outbound),
          "game-host adoption did not harden inherited handles");
  for (std::size_t index = 0U; index < game_arguments.size(); ++index) {
    Require(std::string(bridge.forwarded_argv()[index]) ==
                game_arguments[index],
            "game suffix bytes changed during bridge stripping");
  }
  bridge.forwarded_argv()[1][0] = 'X';
  Require(std::string(arguments.pointers[7]) == "-map",
          "forwarded game argv was not independently owned");

  WriteByte(inbound.write_handle, 0x5aU);
  std::uint8_t received = 0U;
  const auto read = bridge.channel()->ReadSome(&received, 1U);
  Require(read.status == RoR::RendererBridgeChannelStatus::READY &&
              received == 0x5aU,
          "game-host inbound pipe did not transfer bytes");
  const std::uint8_t sent = 0xc3U;
  Require(bridge.channel()->WriteAll(&sent, 1U).status ==
                  RoR::RendererBridgeChannelStatus::READY &&
              ReadByte(outbound.read_handle) == sent,
          "game-host outbound pipe did not transfer bytes");

  Require(bridge.Close().status ==
              RoR::RendererBridgeChannelStatus::CLOSED &&
              !bridge.active() && bridge.channel() == nullptr &&
              bridge.forwarded_argv() == nullptr &&
              bridge.forwarded_argc() == 0,
          "game bridge close did not release owned state");
  Require(!IsOpen(adopted_inbound) && !IsOpen(adopted_outbound),
          "game bridge close leaked adopted handles");
  inbound.read_handle = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
  CloseNative(inbound.write_handle);
  CloseNative(outbound.read_handle);
}

void TestWrongRoleDoesNotAdoptHandles() {
  NativePipe inbound = MakePipe();
  NativePipe outbound = MakePipe();
  const NativeHandle caller_inbound = inbound.read_handle;
  const NativeHandle caller_outbound = outbound.write_handle;
  MutableArguments arguments = Encode(
      MakeEndpoint(RoR::RendererBridgeRole::PRESENTATION_FRONTEND,
                   caller_inbound, caller_outbound),
      {"RoR-Ogre14", "-map", "City World"});
  {
    RoR::RendererOgre14GameBridge bridge;
    const auto result = bridge.Initialize(
        static_cast<int>(arguments.pointers.size()),
        arguments.pointers.data());
    Require(!result.accepted && !result.active &&
                result.status ==
                    RoR::RendererOgre14GameBridgeStatus::REJECTED_WRONG_ROLE &&
                bridge.channel() == nullptr,
            "presentation endpoint was accepted by game host");
  }
  Require(IsOpen(caller_inbound) && IsOpen(caller_outbound),
          "wrong-role rejection stole caller handles");
  ClosePipe(inbound);
  ClosePipe(outbound);
}

void TestMalformedContractFailsClosed() {
  MutableArguments arguments({"RoR-Ogre14",
                              "--ror-render-bridge-version=2", "-map",
                              "City World"});
  RoR::RendererOgre14GameBridge bridge;
  const auto result = bridge.Initialize(
      static_cast<int>(arguments.pointers.size()), arguments.pointers.data());
  Require(!result.accepted && !result.active &&
              result.status == RoR::RendererOgre14GameBridgeStatus::
                                   REJECTED_MALFORMED_ENDPOINT &&
              result.endpoint_status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MISSING_CONTRACT,
          "reserved malformed bridge prefix fell through to legacy argv");
}

void TestNativeFailureClosesTransferredHandles() {
  NativeHandle non_pipe = MakeNonPipe();
  NativePipe outbound = MakePipe();
  const NativeHandle transferred_non_pipe = non_pipe;
  const NativeHandle transferred_outbound = outbound.write_handle;
  MutableArguments arguments = Encode(
      MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST, non_pipe,
                   transferred_outbound),
      {"RoR-Ogre14"});
  {
    RoR::RendererOgre14GameBridge bridge;
    const auto result = bridge.Initialize(
        static_cast<int>(arguments.pointers.size()),
        arguments.pointers.data());
    Require(!result.accepted && !result.active &&
                result.status == RoR::RendererOgre14GameBridgeStatus::
                                     FAILED_CHANNEL_ADOPTION &&
                result.channel.status ==
                    RoR::RendererBridgeChannelStatus::FAILED_INBOUND_HANDLE,
            "native-invalid endpoint was accepted");
  }
  Require(!IsOpen(transferred_non_pipe) && !IsOpen(transferred_outbound),
          "native failure leaked transferred handles");
  non_pipe = kInvalidNativeHandle;
  outbound.write_handle = kInvalidNativeHandle;
  CloseNative(outbound.read_handle);
}

} // namespace

int main() {
  Require(CurrentPlatform() != RoR::HostRenderPlatform::UNKNOWN,
          "test host is unsupported");
  TestStatusContract();
  TestLegacyArgumentsRemainUnchanged();
  TestValidGameHostAdoptionAndOwnedSuffix();
  TestWrongRoleDoesNotAdoptHandles();
  TestMalformedContractFailsClosed();
  TestNativeFailureClosesTransferredHandles();
  return EXIT_SUCCESS;
}
