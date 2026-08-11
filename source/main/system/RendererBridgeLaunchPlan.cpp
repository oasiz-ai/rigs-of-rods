/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBridgeLaunchPlan.h"

#include <array>
#include <cstddef>
#include <limits>

namespace RoR {
namespace {

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

RendererChildLauncherString NativeAscii(const char *value) {
  RendererChildLauncherString result;
  while (value != nullptr && *value != '\0') {
    result.push_back(static_cast<RendererChildLauncherChar>(
        static_cast<unsigned char>(*value)));
    ++value;
  }
  return result;
}

bool HasValidArguments(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept {
  if (argc < 1 || argv == nullptr || argv[0] == nullptr ||
      argv[0][0] == 0) {
    return false;
  }
  for (int index = 0; index < argc; ++index) {
    if (argv[index] == nullptr) {
      return false;
    }
  }
  return true;
}

bool HasNonzeroSession(const RendererBridgeSessionId &session) noexcept {
  for (const std::uint8_t byte : session) {
    if (byte != 0U) {
      return true;
    }
  }
  return false;
}

bool HasDistinctStreamHandles(
    const RendererBridgeStreamHandles &streams) noexcept {
  if (streams.version != kRendererBridgeLaunchPlanContractVersion) {
    return false;
  }
  const std::array<std::uint64_t, 4U> handles{{
      streams.game_to_frontend_read,
      streams.game_to_frontend_write,
      streams.frontend_to_game_read,
      streams.frontend_to_game_write,
  }};
  for (std::size_t first = 0U; first < handles.size(); ++first) {
    if (handles[first] < 3U) {
      return false;
    }
    for (std::size_t second = first + 1U; second < handles.size(); ++second) {
      if (handles[first] == handles[second]) {
        return false;
      }
    }
  }
  return true;
}

std::vector<const RendererChildLauncherChar *> Pointers(
    const std::vector<RendererChildLauncherString> &arguments) {
  std::vector<const RendererChildLauncherChar *> pointers;
  pointers.reserve(arguments.size());
  for (const RendererChildLauncherString &argument : arguments) {
    pointers.push_back(argument.c_str());
  }
  return pointers;
}

bool SameArguments(
    const std::vector<RendererChildLauncherString> &owned, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept {
  if (owned.size() != static_cast<std::size_t>(argc)) {
    return false;
  }
  for (std::size_t index = 0U; index < owned.size(); ++index) {
    if (owned[index] != argv[index]) {
      return false;
    }
  }
  return true;
}

bool SelfValidateGameEndpoint(
    const std::vector<RendererChildLauncherString> &arguments,
    const RendererBridgeEndpoint &expected, int argc,
    const RendererChildLauncherChar *const argv[]) {
  const auto pointers = Pointers(arguments);
  const RendererBridgeEndpointArgvParseResult parsed =
      ParseRendererBridgeEndpoint(static_cast<int>(pointers.size()),
                                  pointers.data());
  return parsed.accepted && parsed.endpoint.version == expected.version &&
         parsed.endpoint.platform == expected.platform &&
         parsed.endpoint.role == expected.role &&
         parsed.endpoint.session_id == expected.session_id &&
         parsed.endpoint.inbound_native_handle ==
             expected.inbound_native_handle &&
         parsed.endpoint.outbound_native_handle ==
             expected.outbound_native_handle &&
         SameArguments(parsed.forwarded_arguments, argc, argv);
}

bool SelfValidatePresentationEndpoint(
    const std::vector<RendererChildLauncherString> &arguments,
    const RendererStartupHandoffResult &handoff,
    const RendererBridgeEndpoint &expected, int argc,
    const RendererChildLauncherChar *const argv[]) {
  const auto pointers = Pointers(arguments);
  const RendererOgreNextChildIntentParseResult renderer =
      ParseRendererOgreNextChildIntent(static_cast<int>(pointers.size()),
                                       pointers.data());
  if (!renderer.accepted ||
      renderer.startup.frontend != handoff.requested_frontend ||
      renderer.startup.directional_shadows !=
          handoff.requested_directional_shadows ||
      renderer.startup.host_platform != handoff.package_platform ||
      renderer.declared_native_backend != handoff.declared_native_backend) {
    return false;
  }
  const auto bridge_pointers = Pointers(renderer.forwarded_arguments);
  const RendererBridgeEndpointArgvParseResult bridge =
      ParseRendererBridgeEndpoint(
          static_cast<int>(bridge_pointers.size()),
          bridge_pointers.data());
  return bridge.accepted && bridge.endpoint.version == expected.version &&
         bridge.endpoint.platform == expected.platform &&
         bridge.endpoint.role == expected.role &&
         bridge.endpoint.session_id == expected.session_id &&
         bridge.endpoint.inbound_native_handle ==
             expected.inbound_native_handle &&
         bridge.endpoint.outbound_native_handle ==
             expected.outbound_native_handle &&
         SameArguments(bridge.forwarded_arguments, argc, argv);
}

} // namespace

RendererBridgeLaunchPlan BuildRendererBridgeLaunchPlan(
    const RendererStartupHandoffResult &handoff,
    const RendererBridgeSessionId &session_id,
    const RendererBridgeStreamHandles &streams, int argc,
    const RendererChildLauncherChar *const argv[]) noexcept {
  RendererBridgeLaunchPlan result;
  try {
    const HostRenderPlatform host = CompileTimeHostPlatform();
    const char *selected_child = RendererFrontendChildExecutableName(handoff);
    if (!handoff.accepted ||
        handoff.child != RendererFrontendChild::OGRE_NEXT ||
        handoff.package_platform != host || selected_child == nullptr ||
        selected_child[0] == '\0') {
      return result;
    }
    result.status = RendererBridgeLaunchPlanStatus::REJECTED_INVALID_ARGUMENTS;
    if (!HasValidArguments(argc, argv) ||
        argc > (std::numeric_limits<int>::max)() - 10) {
      return result;
    }
    result.status = RendererBridgeLaunchPlanStatus::REJECTED_INVALID_SESSION;
    if (!HasNonzeroSession(session_id)) {
      return result;
    }
    result.status =
        RendererBridgeLaunchPlanStatus::REJECTED_INVALID_STREAM_HANDLES;
    if (!HasDistinctStreamHandles(streams)) {
      return result;
    }

    RendererBridgeEndpoint game_endpoint;
    game_endpoint.platform = host;
    game_endpoint.role = RendererBridgeRole::GAME_HOST;
    game_endpoint.session_id = session_id;
    game_endpoint.inbound_native_handle = streams.frontend_to_game_read;
    game_endpoint.outbound_native_handle = streams.game_to_frontend_write;
    const RendererBridgeEndpointArgvEncoding game =
        EncodeRendererBridgeEndpoint(game_endpoint, argc, argv);
    if (!game.accepted ||
        !SelfValidateGameEndpoint(game.arguments, game_endpoint, argc, argv)) {
      result.status =
          RendererBridgeLaunchPlanStatus::REJECTED_ENDPOINT_ENCODING;
      return result;
    }

    RendererBridgeEndpoint presentation_endpoint;
    presentation_endpoint.platform = host;
    presentation_endpoint.role =
        RendererBridgeRole::PRESENTATION_FRONTEND;
    presentation_endpoint.session_id = session_id;
    presentation_endpoint.inbound_native_handle =
        streams.game_to_frontend_read;
    presentation_endpoint.outbound_native_handle =
        streams.frontend_to_game_write;
    const RendererBridgeEndpointArgvEncoding presentation_bridge =
        EncodeRendererBridgeEndpoint(presentation_endpoint, argc, argv);
    if (!presentation_bridge.accepted) {
      result.status =
          RendererBridgeLaunchPlanStatus::REJECTED_ENDPOINT_ENCODING;
      return result;
    }
    const auto presentation_bridge_pointers =
        Pointers(presentation_bridge.arguments);
    const RendererOgreNextChildIntentEncoding presentation =
        EncodeRendererOgreNextChildIntent(
            handoff,
            static_cast<int>(presentation_bridge_pointers.size()),
            presentation_bridge_pointers.data());
    if (!presentation.accepted ||
        !SelfValidatePresentationEndpoint(
            presentation.arguments, handoff, presentation_endpoint, argc,
            argv)) {
      result.status = RendererBridgeLaunchPlanStatus::
          REJECTED_RENDERER_INTENT_ENCODING;
      return result;
    }

    result.platform = host;
    result.session_id = session_id;
    result.streams = streams;
    result.game_child_basename = NativeAscii(
        host == HostRenderPlatform::WINDOWS ? "RoR-Ogre14.exe"
                                            : "RoR-Ogre14");
    result.presentation_child_basename = NativeAscii(selected_child);
    result.game_child_arguments = game.arguments;
    result.presentation_child_arguments = presentation.arguments;
    result.status = RendererBridgeLaunchPlanStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    result.game_child_arguments.clear();
    result.presentation_child_arguments.clear();
    result.accepted = false;
    result.status = RendererBridgeLaunchPlanStatus::FAILED_INTERNAL;
    return result;
  }
}

bool IsKnownRendererBridgeLaunchPlanStatus(
    RendererBridgeLaunchPlanStatus status) noexcept {
  switch (status) {
  case RendererBridgeLaunchPlanStatus::READY:
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_HANDOFF:
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_ARGUMENTS:
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_SESSION:
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_STREAM_HANDLES:
  case RendererBridgeLaunchPlanStatus::REJECTED_ENDPOINT_ENCODING:
  case RendererBridgeLaunchPlanStatus::REJECTED_RENDERER_INTENT_ENCODING:
  case RendererBridgeLaunchPlanStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererBridgeLaunchPlanStatus status) noexcept {
  switch (status) {
  case RendererBridgeLaunchPlanStatus::READY:
    return "ready";
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_HANDOFF:
    return "rejected-invalid-handoff";
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_ARGUMENTS:
    return "rejected-invalid-arguments";
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_SESSION:
    return "rejected-invalid-session";
  case RendererBridgeLaunchPlanStatus::REJECTED_INVALID_STREAM_HANDLES:
    return "rejected-invalid-stream-handles";
  case RendererBridgeLaunchPlanStatus::REJECTED_ENDPOINT_ENCODING:
    return "rejected-endpoint-encoding";
  case RendererBridgeLaunchPlanStatus::REJECTED_RENDERER_INTENT_ENCODING:
    return "rejected-renderer-intent-encoding";
  case RendererBridgeLaunchPlanStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
