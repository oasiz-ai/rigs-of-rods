/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererBridgeLaunchPlan.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

#if defined(_WIN32)
#define ROR_NATIVE_TEXT(value) L##value
#else
#define ROR_NATIVE_TEXT(value) value
#endif

using NativeString = std::basic_string<RoR::RendererChildLauncherChar>;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer bridge launch plan test failed: " << message
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

RoR::RendererStartupHandoffResult MakeAdmittedHandoff() {
  RoR::RendererStartupHandoffRequest request;
  request.startup.frontend =
      RoR::RendererFrontendPreference::OGRE_NEXT_PREFER;
  request.startup.directional_shadows =
      RoR::DirectionalShadowPreference::PSSM;
  request.startup.host_platform = CurrentPlatform();
  RoR::RendererStartupPackageAvailability package;
  package.package_platform = CurrentPlatform();
  package.ogre14_child_present = true;
  package.ogre_next_child_present = true;
  package.ogre_next_child_production_ready = true;
  package.ogre_next_pssm_admitted = true;
  return RoR::ResolveRendererStartupHandoff(request, package);
}

RoR::RendererBridgeSessionId Session() {
  RoR::RendererBridgeSessionId session{};
  for (std::size_t index = 0U; index < session.size(); ++index) {
    session[index] = static_cast<std::uint8_t>(0xa0U + index);
  }
  return session;
}

RoR::RendererBridgeStreamHandles Streams() {
  RoR::RendererBridgeStreamHandles streams;
  streams.game_to_frontend_read = 11U;
  streams.game_to_frontend_write = 12U;
  streams.frontend_to_game_read = 13U;
  streams.frontend_to_game_write = 14U;
  return streams;
}

std::vector<const RoR::RendererChildLauncherChar *>
Pointers(const std::vector<NativeString> &arguments) {
  std::vector<const RoR::RendererChildLauncherChar *> pointers;
  pointers.reserve(arguments.size());
  for (const NativeString &argument : arguments) {
    pointers.push_back(argument.c_str());
  }
  return pointers;
}

void TestExactTwoChildPlan() {
  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("RoR"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("City World"), ROR_NATIVE_TEXT("unicode-\u03a9"),
      ROR_NATIVE_TEXT("")};
  const RoR::RendererBridgeLaunchPlan plan =
      RoR::BuildRendererBridgeLaunchPlan(
          MakeAdmittedHandoff(), Session(), Streams(), 5, arguments);
  Require(plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::READY &&
              plan.version ==
                  RoR::kRendererBridgeLaunchPlanContractVersion &&
              plan.platform == CurrentPlatform() &&
              plan.session_id == Session() &&
              plan.game_child_basename ==
                  (CurrentPlatform() == RoR::HostRenderPlatform::WINDOWS
                       ? ROR_NATIVE_TEXT("RoR-Ogre14.exe")
                       : ROR_NATIVE_TEXT("RoR-Ogre14")) &&
              plan.presentation_child_basename ==
                  (CurrentPlatform() == RoR::HostRenderPlatform::WINDOWS
                       ? ROR_NATIVE_TEXT("RoR-OgreNext.exe")
                       : ROR_NATIVE_TEXT("RoR-OgreNext")),
          "admitted handoff did not produce exact sibling roles");

  const auto game_pointers = Pointers(plan.game_child_arguments);
  const RoR::RendererBridgeEndpointArgvParseResult game =
      RoR::ParseRendererBridgeEndpoint(
          static_cast<int>(game_pointers.size()), game_pointers.data());
  Require(game.accepted &&
              game.endpoint.role == RoR::RendererBridgeRole::GAME_HOST &&
              game.endpoint.session_id == Session() &&
              game.endpoint.inbound_native_handle == 13U &&
              game.endpoint.outbound_native_handle == 12U &&
              game.forwarded_arguments.size() == 5U,
          "game endpoint direction changed");

  const auto presentation_pointers =
      Pointers(plan.presentation_child_arguments);
  const RoR::RendererOgreNextChildIntentParseResult renderer =
      RoR::ParseRendererOgreNextChildIntent(
          static_cast<int>(presentation_pointers.size()),
          presentation_pointers.data());
  Require(renderer.accepted &&
              renderer.startup.frontend ==
                  RoR::RendererFrontendPreference::OGRE_NEXT_PREFER &&
              renderer.startup.directional_shadows ==
                  RoR::DirectionalShadowPreference::PSSM,
          "presentation child lost renderer intent");
  const auto endpoint_pointers = Pointers(renderer.forwarded_arguments);
  const RoR::RendererBridgeEndpointArgvParseResult presentation =
      RoR::ParseRendererBridgeEndpoint(
          static_cast<int>(endpoint_pointers.size()),
          endpoint_pointers.data());
  Require(presentation.accepted &&
              presentation.endpoint.role ==
                  RoR::RendererBridgeRole::PRESENTATION_FRONTEND &&
              presentation.endpoint.session_id == Session() &&
              presentation.endpoint.inbound_native_handle == 11U &&
              presentation.endpoint.outbound_native_handle == 14U &&
              presentation.forwarded_arguments.size() == 5U,
          "presentation endpoint direction changed");
  for (std::size_t index = 0U; index < 5U; ++index) {
    Require(game.forwarded_arguments[index] == arguments[index] &&
                presentation.forwarded_arguments[index] == arguments[index],
            "two-child plan changed the game suffix");
  }
}

void TestInvalidInputsFailClosed() {
  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("RoR")};
  RoR::RendererStartupHandoffResult handoff = MakeAdmittedHandoff();
  handoff.accepted = false;
  RoR::RendererBridgeLaunchPlan plan =
      RoR::BuildRendererBridgeLaunchPlan(
          handoff, Session(), Streams(), 1, arguments);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_INVALID_HANDOFF,
          "rejected handoff entered the bridge plan");

  handoff = MakeAdmittedHandoff();
  handoff.child = RoR::RendererFrontendChild::OGRE14;
  plan = RoR::BuildRendererBridgeLaunchPlan(
      handoff, Session(), Streams(), 1, arguments);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_INVALID_HANDOFF,
          "legacy selection entered the two-process bridge");

  RoR::RendererBridgeSessionId zero{};
  plan = RoR::BuildRendererBridgeLaunchPlan(
      MakeAdmittedHandoff(), zero, Streams(), 1, arguments);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_INVALID_SESSION,
          "zero bridge session reached encoding");

  RoR::RendererBridgeStreamHandles streams = Streams();
  streams.version = 2U;
  plan = RoR::BuildRendererBridgeLaunchPlan(
      MakeAdmittedHandoff(), Session(), streams, 1, arguments);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_INVALID_STREAM_HANDLES,
          "unknown stream version reached encoding");
  streams = Streams();
  streams.frontend_to_game_write =
      streams.game_to_frontend_write;
  plan = RoR::BuildRendererBridgeLaunchPlan(
      MakeAdmittedHandoff(), Session(), streams, 1, arguments);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_INVALID_STREAM_HANDLES,
          "aliased stream handles reached encoding");
  streams = Streams();
  streams.game_to_frontend_read = 2U;
  plan = RoR::BuildRendererBridgeLaunchPlan(
      MakeAdmittedHandoff(), Session(), streams, 1, arguments);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_INVALID_STREAM_HANDLES,
          "reserved stream handle reached encoding");

  plan = RoR::BuildRendererBridgeLaunchPlan(
      MakeAdmittedHandoff(), Session(), Streams(), 0, nullptr);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_INVALID_ARGUMENTS,
          "invalid argv reached encoding");
  const RoR::RendererChildLauncherChar *reserved[] = {
      ROR_NATIVE_TEXT("RoR"),
      ROR_NATIVE_TEXT("--ror-render-bridge-role=duplicate")};
  plan = RoR::BuildRendererBridgeLaunchPlan(
      MakeAdmittedHandoff(), Session(), Streams(), 2, reserved);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_ENDPOINT_ENCODING,
          "reserved bridge game suffix entered a child plan");
  const RoR::RendererChildLauncherChar *renderer_reserved[] = {
      ROR_NATIVE_TEXT("RoR"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1")};
  plan = RoR::BuildRendererBridgeLaunchPlan(
      MakeAdmittedHandoff(), Session(), Streams(), 2,
      renderer_reserved);
  Require(!plan.accepted &&
              plan.status == RoR::RendererBridgeLaunchPlanStatus::
                                 REJECTED_RENDERER_INTENT_ENCODING,
          "reserved renderer game suffix entered a child plan");
}

void TestKnownStatusDomain() {
  for (int value = 0;
       value <= static_cast<int>(
                    RoR::RendererBridgeLaunchPlanStatus::FAILED_INTERNAL);
       ++value) {
    const auto status =
        static_cast<RoR::RendererBridgeLaunchPlanStatus>(value);
    Require(RoR::IsKnownRendererBridgeLaunchPlanStatus(status) &&
                std::string(RoR::ToString(status)) != "invalid",
            "known launch-plan status was omitted");
  }
  Require(!RoR::IsKnownRendererBridgeLaunchPlanStatus(
              static_cast<RoR::RendererBridgeLaunchPlanStatus>(255U)),
          "unknown launch-plan status was accepted");
}

} // namespace

int main() {
  Require(CurrentPlatform() != RoR::HostRenderPlatform::UNKNOWN,
          "test host is unsupported");
  TestExactTwoChildPlan();
  TestInvalidInputsFailClosed();
  TestKnownStatusDomain();
  return EXIT_SUCCESS;
}
