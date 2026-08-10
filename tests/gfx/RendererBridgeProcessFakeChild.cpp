/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderTransportEnvelope.h"
#include "RendererBridgeEndpoint.h"
#include "RendererOgreNextChild.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define ROR_NATIVE_TEXT(value) L##value
#else
#define ROR_NATIVE_TEXT(value) value
#endif

namespace {

using NativeString = RoR::RendererChildLauncherString;

constexpr int kContractFailureExit = 90;
constexpr std::array<std::uint8_t, 8U> kAcknowledgement{{
    0x52U, 0x4fU, 0x52U, 0x41U, 0x43U, 0x4bU, 0x30U, 0x31U,
}};

[[maybe_unused]] std::vector<const RoR::RendererChildLauncherChar *>
Pointers(const std::vector<NativeString> &arguments) {
  std::vector<const RoR::RendererChildLauncherChar *> pointers;
  pointers.reserve(arguments.size());
  for (const NativeString &argument : arguments) {
    pointers.push_back(argument.c_str());
  }
  return pointers;
}

bool ExpectedSession(const RoR::RendererBridgeSessionId &session) noexcept {
#if defined(ROR_RENDERER_BRIDGE_FAKE_ACCEPT_ANY_SESSION)
  for (const std::uint8_t byte : session) {
    if (byte != 0U) {
      return true;
    }
  }
  return false;
#else
  for (std::size_t index = 0U; index < session.size(); ++index) {
    if (session[index] != static_cast<std::uint8_t>(0xa0U + index)) {
      return false;
    }
  }
  return true;
#endif
}

bool Equals(const NativeString &value,
            const RoR::RendererChildLauncherChar *expected) {
  return value == expected;
}

bool StartsWith(const NativeString &value,
                const RoR::RendererChildLauncherChar *prefix) {
  const NativeString expected(prefix);
  return value.size() >= expected.size() &&
         value.compare(0U, expected.size(), expected) == 0;
}

bool HasExpectedPublicLauncherArguments(
    const std::vector<NativeString> &arguments) {
#if defined(ROR_RENDERER_BRIDGE_FAKE_REQUIRE_PUBLIC_ARGUMENTS)
  if (arguments.size() != 6U || arguments[0U].empty() ||
      !Equals(arguments[1U],
              ROR_NATIVE_TEXT("--bridge-test-public-argv")) ||
      !Equals(arguments[2U], ROR_NATIVE_TEXT("-map")) ||
      !Equals(arguments[3U], ROR_NATIVE_TEXT("City World")) ||
      !Equals(arguments[4U], ROR_NATIVE_TEXT("space and unicode \u03a9"))) {
    return false;
  }
  return StartsWith(arguments[5U],
                    ROR_NATIVE_TEXT("--bridge-test-game-exit=")) ||
         Equals(arguments[5U],
                ROR_NATIVE_TEXT("--bridge-test-presentation-first")) ||
         Equals(arguments[5U],
                ROR_NATIVE_TEXT("--bridge-test-post-ready-failure")) ||
         StartsWith(arguments[5U],
                    ROR_NATIVE_TEXT("--bridge-test-pre-ready-fallback="));
#else
  (void)arguments;
  return true;
#endif
}

int ParseDecimalSuffix(
    const NativeString &argument,
    const RoR::RendererChildLauncherChar *prefix, int fallback) {
  if (!StartsWith(argument, prefix)) {
    return fallback;
  }
  const NativeString value = argument.substr(NativeString(prefix).size());
  if (value.empty()) {
    return fallback;
  }
  int parsed = 0;
  for (const RoR::RendererChildLauncherChar character : value) {
    if (character < static_cast<RoR::RendererChildLauncherChar>('0') ||
        character > static_cast<RoR::RendererChildLauncherChar>('9')) {
      return fallback;
    }
    const int digit = static_cast<int>(
        character - static_cast<RoR::RendererChildLauncherChar>('0'));
    if (parsed > ((std::numeric_limits<int>::max)() - digit) / 10) {
      return fallback;
    }
    parsed = parsed * 10 + digit;
  }
  return parsed;
}

#if defined(_WIN32)

using NativeIoHandle = HANDLE;

NativeIoHandle Adopt(std::uint64_t token) noexcept {
  return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(token));
}

bool ReadExact(NativeIoHandle handle, std::uint8_t *bytes,
               std::size_t size) noexcept {
  std::size_t offset = 0U;
  while (offset < size) {
    const std::size_t remaining = size - offset;
    const DWORD request = static_cast<DWORD>((std::min)(
        remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
    DWORD count = 0U;
    if (::ReadFile(handle, bytes + offset, request, &count, nullptr) == FALSE ||
        count == 0U) {
      return false;
    }
    offset += static_cast<std::size_t>(count);
  }
  return true;
}

bool WriteExact(NativeIoHandle handle, const std::uint8_t *bytes,
                std::size_t size) noexcept {
  std::size_t offset = 0U;
  while (offset < size) {
    const std::size_t remaining = size - offset;
    const DWORD request = static_cast<DWORD>((std::min)(
        remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
    DWORD count = 0U;
    if (::WriteFile(handle, bytes + offset, request, &count, nullptr) == FALSE ||
        count == 0U) {
      return false;
    }
    offset += static_cast<std::size_t>(count);
  }
  return true;
}

bool HasExactInheritedResources(const RoR::RendererBridgeEndpoint &endpoint,
                                bool game) noexcept {
  (void)game;
  DWORD inbound_flags = 0U;
  DWORD outbound_flags = 0U;
  BOOL in_job = FALSE;
  return ::GetHandleInformation(Adopt(endpoint.inbound_native_handle),
                                &inbound_flags) != FALSE &&
         ::GetHandleInformation(Adopt(endpoint.outbound_native_handle),
                                &outbound_flags) != FALSE &&
         ::IsProcessInJob(::GetCurrentProcess(), nullptr, &in_job) != FALSE &&
         in_job != FALSE;
}

[[noreturn]] void BlockForever() noexcept { ::Sleep(INFINITE); }

#else

using NativeIoHandle = int;

NativeIoHandle Adopt(std::uint64_t token) noexcept {
  return static_cast<int>(token);
}

bool ReadExact(NativeIoHandle handle, std::uint8_t *bytes,
               std::size_t size) noexcept {
  std::size_t offset = 0U;
  while (offset < size) {
    const ssize_t count = ::read(handle, bytes + offset, size - offset);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}

bool WriteExact(NativeIoHandle handle, const std::uint8_t *bytes,
                std::size_t size) noexcept {
  std::size_t offset = 0U;
  while (offset < size) {
    const ssize_t count = ::write(handle, bytes + offset, size - offset);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}

bool HasExactInheritedResources(const RoR::RendererBridgeEndpoint &endpoint,
                                bool game) noexcept {
  struct rlimit limit = {};
  if (::getrlimit(RLIMIT_NOFILE, &limit) != 0) {
    return false;
  }
  const rlim_t upper =
      limit.rlim_cur == RLIM_INFINITY
          ? static_cast<rlim_t>(65536U)
          : (std::min)(limit.rlim_cur, static_cast<rlim_t>(1048576U));
  std::size_t open_nonstandard = 0U;
  for (int descriptor = 3; descriptor < static_cast<int>(upper);
       ++descriptor) {
    errno = 0;
    if (::fcntl(descriptor, F_GETFD) >= 0 || errno != EBADF) {
      ++open_nonstandard;
      if (descriptor != Adopt(endpoint.inbound_native_handle) &&
          descriptor != Adopt(endpoint.outbound_native_handle)) {
        return false;
      }
    }
  }
  const bool group_is_correct =
      game ? ::getpgrp() == ::getpid() : ::getpgrp() != ::getpid();
  return open_nonstandard == 2U && group_is_correct;
}

[[noreturn]] void BlockForever() noexcept {
  for (;;) {
    (void)::pause();
  }
}

void ExitFortyTwoOnSigterm(int) noexcept { ::_exit(42); }

bool ArmGracefulSigtermExit() noexcept {
  struct sigaction action = {};
  action.sa_handler = ExitFortyTwoOnSigterm;
  if (sigemptyset(&action.sa_mask) != 0) {
    return false;
  }
  return ::sigaction(SIGTERM, &action, nullptr) == 0;
}

#endif

std::uint64_t ReadU64LittleEndian(const std::uint8_t *bytes) noexcept {
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

bool ReadEnvelope(NativeIoHandle inbound,
                  RoR::Render::RenderTransportMessageKind expected_kind,
                  std::uint64_t expected_sequence,
                  const std::vector<std::uint8_t> &expected_payload) {
  std::vector<std::uint8_t> frame(
      RoR::Render::kRenderTransportEnvelopeHeaderBytes);
  if (!ReadExact(inbound, frame.data(), frame.size())) {
    return false;
  }
  const std::uint64_t payload_size = ReadU64LittleEndian(frame.data() + 24U);
  if (payload_size > 1024U) {
    return false;
  }
  frame.resize(frame.size() + static_cast<std::size_t>(payload_size));
  if (payload_size != 0U &&
      !ReadExact(inbound,
                 frame.data() + RoR::Render::kRenderTransportEnvelopeHeaderBytes,
                 static_cast<std::size_t>(payload_size))) {
    return false;
  }
  RoR::Render::RenderTransportEnvelopeView view;
  return RoR::Render::DecodeRenderTransportEnvelope(frame, 1024U, view) ==
             RoR::Render::RenderTransportStatus::OK &&
         view.kind == expected_kind && view.sequence == expected_sequence &&
         view.payload_size == expected_payload.size() &&
         std::memcmp(view.payload, expected_payload.data(),
                     expected_payload.size()) == 0;
}

bool WriteEnvelope(NativeIoHandle outbound,
                   RoR::Render::RenderTransportMessageKind kind,
                   std::uint64_t sequence,
                   const std::vector<std::uint8_t> &payload) {
  const RoR::Render::RenderTransportEnvelopeEncodeResult encoded =
      RoR::Render::EncodeRenderTransportEnvelope(kind, sequence, payload,
                                                 1024U);
  return encoded.ok() &&
         WriteExact(outbound, encoded.bytes.data(), encoded.bytes.size());
}

[[maybe_unused]] int
RunGame(const RoR::RendererBridgeEndpointArgvParseResult &parsed) {
  if (parsed.endpoint.role != RoR::RendererBridgeRole::GAME_HOST ||
      !ExpectedSession(parsed.endpoint.session_id) ||
      !HasExpectedPublicLauncherArguments(parsed.forwarded_arguments) ||
      !HasExactInheritedResources(parsed.endpoint, true)) {
    return kContractFailureExit;
  }
  bool presentation_first = false;
  bool post_ready_failure = false;
  bool pre_ready_fallback = false;
  bool graceful_sigterm_exit = false;
  bool presentation_signal_requested = false;
  int exit_code = 0;
  int signal_number = 0;
  for (const NativeString &argument : parsed.forwarded_arguments) {
    presentation_first = presentation_first ||
                         Equals(argument,
                                ROR_NATIVE_TEXT(
                                    "--bridge-test-presentation-first"));
    post_ready_failure =
        post_ready_failure ||
        Equals(argument,
               ROR_NATIVE_TEXT("--bridge-test-post-ready-failure"));
    pre_ready_fallback = pre_ready_fallback ||
                         StartsWith(
                             argument,
                             ROR_NATIVE_TEXT(
                                 "--bridge-test-pre-ready-fallback="));
    graceful_sigterm_exit =
        graceful_sigterm_exit ||
        Equals(argument,
               ROR_NATIVE_TEXT("--bridge-test-game-graceful-sigterm"));
    presentation_signal_requested =
        presentation_signal_requested ||
        StartsWith(argument,
                   ROR_NATIVE_TEXT("--bridge-test-presentation-signal="));
    exit_code = ParseDecimalSuffix(
        argument, ROR_NATIVE_TEXT("--bridge-test-game-exit="), exit_code);
    signal_number = ParseDecimalSuffix(
        argument, ROR_NATIVE_TEXT("--bridge-test-game-signal="),
        signal_number);
  }
  if (presentation_first || post_ready_failure || pre_ready_fallback ||
      presentation_signal_requested) {
    BlockForever();
  }

  const NativeIoHandle inbound = Adopt(parsed.endpoint.inbound_native_handle);
  const NativeIoHandle outbound =
      Adopt(parsed.endpoint.outbound_native_handle);
#if !defined(_WIN32)
  if (graceful_sigterm_exit) {
    constexpr std::array<std::uint8_t, 1U> ready{{0x47U}};
    if (!ArmGracefulSigtermExit() ||
        !WriteExact(outbound, ready.data(), ready.size())) {
      return kContractFailureExit + 9;
    }
    BlockForever();
  }
#else
  (void)graceful_sigterm_exit;
#endif
  const std::vector<std::uint8_t> asset_payload{
      0x61U, 0x73U, 0x73U, 0x65U, 0x74U};
  const std::vector<std::uint8_t> scene_payload{
      0x73U, 0x63U, 0x65U, 0x6eU, 0x65U};
  if (!WriteEnvelope(
          outbound,
          RoR::Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V2,
          1U, asset_payload) ||
      !WriteEnvelope(
          outbound,
          RoR::Render::RenderTransportMessageKind::
              SCENE_SNAPSHOT_V4_CAMERA_V2,
          2U, scene_payload)) {
    return kContractFailureExit + 1;
  }
  std::array<std::uint8_t, kAcknowledgement.size()> acknowledgement{};
  if (!ReadExact(inbound, acknowledgement.data(), acknowledgement.size()) ||
      acknowledgement != kAcknowledgement) {
    return kContractFailureExit + 2;
  }
#if !defined(_WIN32)
  if (signal_number > 0) {
    (void)::raise(signal_number);
    return kContractFailureExit + 3;
  }
#else
  (void)signal_number;
#endif
  return exit_code;
}

[[maybe_unused]] int RunPresentation(
    const RoR::RendererBridgeEndpointArgvParseResult &parsed) {
  if (parsed.endpoint.role !=
          RoR::RendererBridgeRole::PRESENTATION_FRONTEND ||
      !ExpectedSession(parsed.endpoint.session_id) ||
      !HasExpectedPublicLauncherArguments(parsed.forwarded_arguments) ||
      !HasExactInheritedResources(parsed.endpoint, false)) {
    return kContractFailureExit;
  }
  for (const NativeString &argument : parsed.forwarded_arguments) {
    if (Equals(argument,
               ROR_NATIVE_TEXT("--bridge-test-presentation-first"))) {
      return 23;
    }
    if (StartsWith(
            argument,
            ROR_NATIVE_TEXT("--bridge-test-pre-ready-fallback="))) {
      return RoR::kRendererOgreNextChildPrePeerReadyFailureExitCode;
    }
    if (Equals(argument,
               ROR_NATIVE_TEXT("--bridge-test-post-ready-failure"))) {
      return RoR::kRendererOgreNextChildPostPeerReadyFailureExitCode;
    }
#if !defined(_WIN32)
    const int presentation_signal = ParseDecimalSuffix(
        argument, ROR_NATIVE_TEXT("--bridge-test-presentation-signal="), 0);
    if (presentation_signal > 0) {
      (void)::raise(presentation_signal);
      return kContractFailureExit + 11;
    }
#endif
    if (Equals(argument,
               ROR_NATIVE_TEXT("--bridge-test-game-graceful-sigterm"))) {
#if !defined(_WIN32)
      const NativeIoHandle inbound =
          Adopt(parsed.endpoint.inbound_native_handle);
      std::array<std::uint8_t, 1U> ready{};
      if (!ReadExact(inbound, ready.data(), ready.size()) ||
          ready[0U] != 0x47U) {
        return kContractFailureExit + 10;
      }
      return RoR::kRendererOgreNextChildPrePeerReadyFailureExitCode;
#else
      return kContractFailureExit + 10;
#endif
    }
  }

  const NativeIoHandle inbound = Adopt(parsed.endpoint.inbound_native_handle);
  const NativeIoHandle outbound =
      Adopt(parsed.endpoint.outbound_native_handle);
  const std::vector<std::uint8_t> asset_payload{
      0x61U, 0x73U, 0x73U, 0x65U, 0x74U};
  const std::vector<std::uint8_t> scene_payload{
      0x73U, 0x63U, 0x65U, 0x6eU, 0x65U};
  if (!ReadEnvelope(
          inbound,
          RoR::Render::RenderTransportMessageKind::RENDER_ASSET_DELTA_V2,
          1U, asset_payload) ||
      !ReadEnvelope(
          inbound,
          RoR::Render::RenderTransportMessageKind::
              SCENE_SNAPSHOT_V4_CAMERA_V2,
          2U, scene_payload) ||
      !WriteExact(outbound, kAcknowledgement.data(),
                  kAcknowledgement.size())) {
    return kContractFailureExit + 4;
  }
  BlockForever();
}

[[maybe_unused]] int RunStandaloneFallbackGame(
    int argc, const RoR::RendererChildLauncherChar *const argv[]) {
  std::vector<NativeString> arguments;
  if (argc < 1 || argv == nullptr) {
    return kContractFailureExit + 8;
  }
  arguments.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return kContractFailureExit + 8;
    }
    arguments.emplace_back(argv[index]);
  }
  if (!HasExpectedPublicLauncherArguments(arguments)) {
    return kContractFailureExit + 8;
  }
  for (const NativeString &argument : arguments) {
    if (StartsWith(
            argument,
            ROR_NATIVE_TEXT("--bridge-test-pre-ready-fallback="))) {
      return ParseDecimalSuffix(
          argument,
          ROR_NATIVE_TEXT("--bridge-test-pre-ready-fallback="),
          kContractFailureExit + 8);
    }
  }
  return kContractFailureExit + 8;
}

int Run(int argc, const RoR::RendererChildLauncherChar *const argv[]) {
#if defined(ROR_RENDERER_BRIDGE_FAKE_GAME)
  const RoR::RendererBridgeEndpointArgvParseResult parsed =
      RoR::ParseRendererBridgeEndpoint(argc, argv);
  if (parsed.accepted) {
    return RunGame(parsed);
  }
#if defined(ROR_RENDERER_BRIDGE_FAKE_REQUIRE_PUBLIC_ARGUMENTS)
  return RunStandaloneFallbackGame(argc, argv);
#else
  return kContractFailureExit + 5;
#endif
#elif defined(ROR_RENDERER_BRIDGE_FAKE_PRESENTATION)
  const RoR::RendererOgreNextChildIntentParseResult renderer =
      RoR::ParseRendererOgreNextChildIntent(argc, argv);
  if (!renderer.accepted) {
    return kContractFailureExit + 6;
  }
  const auto pointers = Pointers(renderer.forwarded_arguments);
  const RoR::RendererBridgeEndpointArgvParseResult parsed =
      RoR::ParseRendererBridgeEndpoint(static_cast<int>(pointers.size()),
                                       pointers.data());
  return parsed.accepted ? RunPresentation(parsed) : kContractFailureExit + 7;
#else
#error "One fake render-bridge child role must be selected"
#endif
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t *argv[]) { return Run(argc, argv); }
#else
int main(int argc, char *argv[]) { return Run(argc, argv); }
#endif
