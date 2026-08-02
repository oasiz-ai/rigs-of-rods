/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextChild.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
#define ROR_NATIVE_TEXT(value) L##value
#else
#define ROR_NATIVE_TEXT(value) value
#endif

using NativeString =
    std::basic_string<RoR::RendererChildLauncherChar>;

std::vector<int> g_calls;
RoR::RendererOgreNextFrontendBootstrapRequest g_frontend_request;
RoR::RendererOgreNextFrontendBootstrapStatus g_frontend_status =
    RoR::RendererOgreNextFrontendBootstrapStatus::COMPLETED;
RoR::RendererOgreNextChildInvocationMode g_frontend_mode =
    RoR::RendererOgreNextChildInvocationMode::PROBE_HEADLESS;
bool g_frontend_accepted = true;
bool g_frontend_completed = true;
bool g_use_default_frontend_result = true;
std::uint32_t g_frontend_version =
    RoR::kRendererOgreNextChildContractVersion;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer Ogre-Next child test failed: " << message << '\n';
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

const RoR::RendererChildLauncherChar *CurrentNativeBackendArgument() {
#if defined(_WIN32)
  return ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=dxr");
#elif defined(__APPLE__)
  return ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=metal");
#else
  return ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=vulkan-khr");
#endif
}

NativeString CurrentPlatformBridgeArgument() {
#if defined(_WIN32)
  return ROR_NATIVE_TEXT("--ror-render-bridge-platform=windows");
#elif defined(__APPLE__)
  return ROR_NATIVE_TEXT("--ror-render-bridge-platform=macos");
#else
  return ROR_NATIVE_TEXT("--ror-render-bridge-platform=linux");
#endif
}

NativeString ForeignPlatformBridgeArgument() {
#if defined(_WIN32)
  return ROR_NATIVE_TEXT("--ror-render-bridge-platform=linux");
#else
  return ROR_NATIVE_TEXT("--ror-render-bridge-platform=windows");
#endif
}

RoR::RendererNativeShadowPreflight CollectPreflight() {
  g_calls.push_back(1);
  return RoR::CollectRendererOgreNextChildNativePreflight();
}

RoR::RendererOgreNextFrontendBootstrapResult BootstrapFrontend(
    const RoR::RendererOgreNextFrontendBootstrapRequest &request) {
  g_calls.push_back(2);
  g_frontend_request = request;
  Require(request.startup_plan.accepted &&
              request.startup_plan.effective_path ==
                  RoR::RendererStartupPath::
                      OGRE_NEXT_PSSM_3_CASCADE_V1,
          "frontend received an unadmitted startup plan");
  RoR::RendererOgreNextFrontendBootstrapResult result;
  result.version = g_frontend_version;
  if (g_use_default_frontend_result) {
    result.invocation_mode = request.invocation_mode;
    result.accepted = true;
    result.status =
        RoR::RendererOgreNextFrontendBootstrapStatus::COMPLETED;
    result.completed = true;
    return result;
  }
  result.invocation_mode = g_frontend_mode;
  result.status = g_frontend_status;
  result.accepted = g_frontend_accepted;
  result.completed = g_frontend_completed;
  return result;
}

RoR::RendererNativeShadowPreflight ThrowingPreflight() {
  throw 1;
}

RoR::RendererOgreNextFrontendBootstrapResult ThrowingFrontend(
    const RoR::RendererOgreNextFrontendBootstrapRequest &) {
  throw 2;
}

RoR::RendererOgreNextChildRuntime Runtime() {
  RoR::RendererOgreNextChildRuntime runtime;
  runtime.collect_native_preflight = &CollectPreflight;
  runtime.bootstrap_frontend = &BootstrapFrontend;
  return runtime;
}

void ResetCallbacks() {
  g_calls.clear();
  g_frontend_request =
      RoR::RendererOgreNextFrontendBootstrapRequest{};
  g_frontend_status =
      RoR::RendererOgreNextFrontendBootstrapStatus::COMPLETED;
  g_frontend_mode =
      RoR::RendererOgreNextChildInvocationMode::PROBE_HEADLESS;
  g_frontend_accepted = true;
  g_frontend_completed = true;
  g_use_default_frontend_result = true;
  g_frontend_version = RoR::kRendererOgreNextChildContractVersion;
}

std::vector<const RoR::RendererChildLauncherChar *> Pointers(
    const std::vector<NativeString> &arguments) {
  std::vector<const RoR::RendererChildLauncherChar *> pointers;
  pointers.reserve(arguments.size());
  for (const NativeString &argument : arguments) {
    pointers.push_back(argument.c_str());
  }
  return pointers;
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
    session[index] = static_cast<std::uint8_t>(0x50U + index);
  }
  return session;
}

RoR::RendererBridgeEndpoint MakeEndpoint(
    RoR::RendererBridgeRole role, std::uint64_t inbound = 11U,
    std::uint64_t outbound = 14U) {
  RoR::RendererBridgeEndpoint endpoint;
  endpoint.platform = CurrentPlatform();
  endpoint.role = role;
  endpoint.session_id = Session();
  endpoint.inbound_native_handle = inbound;
  endpoint.outbound_native_handle = outbound;
  return endpoint;
}

std::vector<NativeString> HeadlessArguments() {
  return {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none"),
  };
}

std::vector<NativeString> ProductionArguments(
    const RoR::RendererBridgeEndpoint &endpoint,
    const std::vector<NativeString> &game_arguments) {
  const std::vector<const RoR::RendererChildLauncherChar *> game_pointers =
      Pointers(game_arguments);
  const RoR::RendererBridgeEndpointArgvEncoding bridge =
      RoR::EncodeRendererBridgeEndpoint(
          endpoint, static_cast<int>(game_pointers.size()),
          game_pointers.data());
  Require(bridge.accepted, "test bridge endpoint encoding failed");
  const std::vector<const RoR::RendererChildLauncherChar *> bridge_pointers =
      Pointers(bridge.arguments);
  const RoR::RendererOgreNextChildIntentEncoding child =
      RoR::EncodeRendererOgreNextChildIntent(
          MakeAdmittedHandoff(),
          static_cast<int>(bridge_pointers.size()),
          bridge_pointers.data());
  Require(child.accepted, "test child-intent encoding failed");
  return child.arguments;
}

RoR::RendererOgreNextChildResult Run(
    const std::vector<NativeString> &arguments,
    const RoR::RendererOgreNextChildRuntime &runtime = Runtime()) {
  const std::vector<const RoR::RendererChildLauncherChar *> pointers =
      Pointers(arguments);
  return RoR::RunRendererOgreNextChild(
      static_cast<int>(pointers.size()), pointers.data(), runtime);
}

std::size_t FindArgument(
    const std::vector<NativeString> &arguments,
    const NativeString &value) {
  for (std::size_t index = 0U; index < arguments.size(); ++index) {
    if (arguments[index] == value) {
      return index;
    }
  }
  return arguments.size();
}

void TestStatusAndImmutableAvailabilityContracts() {
  Require(RoR::kRendererOgreNextChildContractVersion == 3U,
          "child live-session contract version changed");
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const auto mode =
        static_cast<RoR::RendererOgreNextChildInvocationMode>(value);
    Require(RoR::IsKnownRendererOgreNextChildInvocationMode(mode) ==
                (value <= 1U),
            "invocation-mode classifier accepted an unknown value");
    const auto frontend =
        static_cast<RoR::RendererOgreNextFrontendBootstrapStatus>(value);
    Require(RoR::IsKnownRendererOgreNextFrontendBootstrapStatus(frontend) ==
                (value <= 5U),
            "frontend status classifier accepted an unknown value");
    const auto child = static_cast<RoR::RendererOgreNextChildStatus>(value);
    Require(RoR::IsKnownRendererOgreNextChildStatus(child) ==
                (value <= 12U),
            "child status classifier accepted an unknown value");
  }
  Require(std::strcmp(
              RoR::ToString(RoR::RendererOgreNextChildStatus::
                                COMPLETED_PRODUCTION_BRIDGE_SESSION),
              "completed-production-bridge-session") == 0 &&
              std::strcmp(
                  RoR::ToString(
                      RoR::RendererOgreNextChildInvocationMode::
                          PRODUCTION_BRIDGE),
                  "production-bridge") == 0,
          "production bridge status strings changed");
  Require(std::strcmp(
              RoR::ToString(static_cast<
                  RoR::RendererOgreNextChildStatus>(255U)),
              "invalid") == 0 &&
              std::strcmp(
                  RoR::ToString(static_cast<
                      RoR::RendererOgreNextChildInvocationMode>(255U)),
                  "invalid") == 0,
          "unknown child contract values did not fail closed");
  Require(RoR::ClassifyRendererOgreNextChildIntentFailure(
              RoR::RendererOgreNextChildIntentArgvStatus::FAILED_INTERNAL) ==
              RoR::RendererOgreNextChildStatus::FAILED_INTERNAL &&
              RoR::ClassifyRendererOgreNextChildIntentFailure(
                  RoR::RendererOgreNextChildIntentArgvStatus::
                      REJECTED_MALFORMED_CONTRACT) ==
                  RoR::RendererOgreNextChildStatus::REJECTED_CHILD_INTENT &&
              RoR::ClassifyRendererOgreNextChildIntentFailure(
                  RoR::RendererOgreNextChildIntentArgvStatus::READY) ==
                  RoR::RendererOgreNextChildStatus::FAILED_INTERNAL &&
              RoR::ClassifyRendererOgreNextChildIntentFailure(
                  static_cast<
                      RoR::RendererOgreNextChildIntentArgvStatus>(255U)) ==
                  RoR::RendererOgreNextChildStatus::FAILED_INTERNAL,
          "child-intent failure classification changed");
  Require(RoR::ClassifyRendererBridgeEndpointFailure(
              RoR::RendererBridgeEndpointArgvStatus::FAILED_INTERNAL) ==
              RoR::RendererOgreNextChildStatus::FAILED_INTERNAL &&
              RoR::ClassifyRendererBridgeEndpointFailure(
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MALFORMED_CONTRACT) ==
                  RoR::RendererOgreNextChildStatus::
                      REJECTED_BRIDGE_ENDPOINT &&
              RoR::ClassifyRendererBridgeEndpointFailure(
                  RoR::RendererBridgeEndpointArgvStatus::READY) ==
                  RoR::RendererOgreNextChildStatus::FAILED_INTERNAL &&
              RoR::ClassifyRendererBridgeEndpointFailure(
                  static_cast<
                      RoR::RendererBridgeEndpointArgvStatus>(255U)) ==
                  RoR::RendererOgreNextChildStatus::FAILED_INTERNAL,
          "bridge failure classification changed");

  const RoR::RendererStartupBuildAvailability availability =
      RoR::RendererOgreNextChildBuildAvailability();
  Require(availability.version == RoR::kRendererStartupPlanContractVersion &&
              !availability.ogre14_frontend_available &&
              availability.ogre_next_frontend_available &&
              availability.ogre_next_pssm_available &&
              availability.native_directional_shadow_backend ==
                  RoR::NativeRayTracingBackend::NONE,
          "standalone child build facts changed");
  const RoR::RendererNativeShadowPreflight preflight =
      RoR::CollectRendererOgreNextChildNativePreflight();
  Require(preflight.version == RoR::kRendererStartupPlanContractVersion &&
              preflight.source ==
                  RoR::RendererNativePreflightSource::NONE &&
              preflight.backend == RoR::NativeRayTracingBackend::NONE &&
              !preflight.completed && !preflight.device_selected &&
              preflight.device_identity == 0U &&
              !preflight.api_supported &&
              !preflight.hardware_accelerated &&
              !preflight.hardware_floor_met,
          "native preflight did not fail closed");
}

void TestProbeHeadlessBootstrapAndPreferNativeFallback() {
  ResetCallbacks();
  const RoR::RendererOgreNextChildResult pssm_result =
      Run(HeadlessArguments());
  Require(pssm_result.accepted && pssm_result.completed &&
              pssm_result.status == RoR::RendererOgreNextChildStatus::
                                        COMPLETED_HEADLESS_BOOTSTRAP &&
              pssm_result.invocation_mode ==
                  RoR::RendererOgreNextChildInvocationMode::PROBE_HEADLESS &&
              pssm_result.bridge_status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MISSING_CONTRACT &&
              !pssm_result.frontend_request.has_bridge_endpoint &&
              pssm_result.frontend_request.game_arguments.size() == 1U &&
              pssm_result.startup_plan.status ==
                  RoR::RendererStartupSelectionStatus::
                      SELECTED_REQUESTED_PATH &&
              !pssm_result.startup_plan.used_shadow_fallback &&
              g_calls.size() == 2U && g_calls[0] == 1 && g_calls[1] == 2,
          "headless PSSM probe did not preserve parse-preflight-frontend order");

  std::vector<NativeString> prefer_native = HeadlessArguments();
  prefer_native[3] = ROR_NATIVE_TEXT(
      "--ror-renderer-child-directional-shadows=prefer-native");
  ResetCallbacks();
  const RoR::RendererOgreNextChildResult fallback = Run(prefer_native);
  Require(fallback.accepted && fallback.completed &&
              fallback.startup_plan.used_shadow_fallback &&
              fallback.startup_plan.status ==
                  RoR::RendererStartupSelectionStatus::
                      FALLBACK_TO_OGRE_NEXT_PSSM &&
              fallback.startup_plan.native_preflight_readiness ==
                  RoR::RendererNativePreflightReadiness::
                      BACKEND_UNAVAILABLE &&
              g_calls.size() == 2U,
          "prefer-native did not fall back to child-owned PSSM");
}

void TestProductionBridgeAcceptanceAndGameSuffix() {
  const std::vector<NativeString> game_arguments{
      ROR_NATIVE_TEXT("RoR"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("City World"), ROR_NATIVE_TEXT("unicode-\u03a9"),
      ROR_NATIVE_TEXT("")};
  const RoR::RendererBridgeEndpoint endpoint = MakeEndpoint(
      RoR::RendererBridgeRole::PRESENTATION_FRONTEND);
  const std::vector<NativeString> arguments =
      ProductionArguments(endpoint, game_arguments);
  ResetCallbacks();
  const RoR::RendererOgreNextChildResult result = Run(arguments);
  Require(result.accepted && result.completed &&
              result.status == RoR::RendererOgreNextChildStatus::
                                   COMPLETED_PRODUCTION_BRIDGE_SESSION &&
              result.bridge_status ==
                  RoR::RendererBridgeEndpointArgvStatus::READY &&
              result.invocation_mode ==
                  RoR::RendererOgreNextChildInvocationMode::
                      PRODUCTION_BRIDGE &&
              result.frontend_request.has_bridge_endpoint &&
              result.frontend_request.bridge_endpoint.session_id ==
                  endpoint.session_id &&
              result.frontend_request.bridge_endpoint.role ==
                  RoR::RendererBridgeRole::PRESENTATION_FRONTEND &&
              result.frontend_request.bridge_endpoint.platform ==
                  CurrentPlatform() &&
              result.frontend_request.bridge_endpoint.
                      inbound_native_handle ==
                  endpoint.inbound_native_handle &&
              result.frontend_request.bridge_endpoint.
                      outbound_native_handle ==
                  endpoint.outbound_native_handle &&
              result.frontend_request.game_arguments == game_arguments &&
              g_frontend_request.game_arguments == game_arguments &&
              result.frontend.status ==
                  RoR::RendererOgreNextFrontendBootstrapStatus::
                      COMPLETED &&
              result.frontend.accepted && result.frontend.completed &&
              g_calls.size() == 2U && g_calls[0] == 1 && g_calls[1] == 2,
          "valid production bridge was not accepted with only its game suffix");
  for (const NativeString &argument : result.frontend_request.game_arguments) {
    Require(argument.find(
                ROR_NATIVE_TEXT("--ror-renderer-child-")) != 0U &&
                argument.find(
                    ROR_NATIVE_TEXT("--ror-render-bridge-")) != 0U,
            "orchestration prefix leaked into decoded game arguments");
  }
}

void TestBridgeFailuresHappenBeforeNativeCallbacks() {
  std::vector<NativeString> missing = HeadlessArguments();
  missing.push_back(ROR_NATIVE_TEXT("-map"));
  ResetCallbacks();
  RoR::RendererOgreNextChildResult result = Run(missing);
  Require(!result.accepted && !result.completed &&
              result.status == RoR::RendererOgreNextChildStatus::
                                   REJECTED_BRIDGE_ENDPOINT &&
              result.bridge_status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MISSING_CONTRACT &&
              result.frontend_request.game_arguments.empty() &&
              g_calls.empty(),
          "missing bridge prefix reached native preflight");

  const std::vector<NativeString> game{
      ROR_NATIVE_TEXT("RoR"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("CityWorld")};
  std::vector<NativeString> malformed = ProductionArguments(
      MakeEndpoint(RoR::RendererBridgeRole::PRESENTATION_FRONTEND), game);
  const std::size_t platform_index =
      FindArgument(malformed, CurrentPlatformBridgeArgument());
  Require(platform_index < malformed.size(),
          "test could not locate bridge platform argument");
  malformed[platform_index] = ForeignPlatformBridgeArgument();
  ResetCallbacks();
  result = Run(malformed);
  Require(result.status == RoR::RendererOgreNextChildStatus::
                               REJECTED_BRIDGE_ENDPOINT &&
              result.bridge_status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MALFORMED_CONTRACT &&
              g_calls.empty(),
          "foreign-host bridge endpoint reached native preflight");

  malformed = ProductionArguments(
      MakeEndpoint(RoR::RendererBridgeRole::PRESENTATION_FRONTEND), game);
  const NativeString session =
      ROR_NATIVE_TEXT("--ror-render-bridge-session=") +
      NativeString(32U, static_cast<RoR::RendererChildLauncherChar>('0'));
  for (std::size_t index = 0U; index < malformed.size(); ++index) {
    if (malformed[index].find(
            ROR_NATIVE_TEXT("--ror-render-bridge-session=")) == 0U) {
      malformed[index] = session;
    }
  }
  ResetCallbacks();
  result = Run(malformed);
  Require(result.status == RoR::RendererOgreNextChildStatus::
                               REJECTED_BRIDGE_ENDPOINT &&
              result.bridge_status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MALFORMED_CONTRACT &&
              g_calls.empty(),
          "zero-session bridge endpoint reached native preflight");

  std::vector<NativeString> wrong_role = ProductionArguments(
      MakeEndpoint(RoR::RendererBridgeRole::GAME_HOST), game);
  ResetCallbacks();
  result = Run(wrong_role);
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::REJECTED_BRIDGE_ROLE &&
              result.bridge_status ==
                  RoR::RendererBridgeEndpointArgvStatus::READY &&
              result.frontend_request.game_arguments.empty() &&
              g_calls.empty(),
          "game-host endpoint reached presentation preflight");

  malformed = ProductionArguments(
      MakeEndpoint(RoR::RendererBridgeRole::PRESENTATION_FRONTEND), game);
  malformed.push_back(
      ROR_NATIVE_TEXT("--ror-render-bridge-role=duplicate"));
  ResetCallbacks();
  result = Run(malformed);
  Require(result.status == RoR::RendererOgreNextChildStatus::
                               REJECTED_BRIDGE_ENDPOINT &&
              result.bridge_status ==
                  RoR::RendererBridgeEndpointArgvStatus::
                      REJECTED_MALFORMED_CONTRACT &&
              g_calls.empty(),
          "duplicate bridge record reached native preflight");
}

void TestIntentRuntimeAndStartupRejections() {
  const std::vector<NativeString> malformed{
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=2")};
  ResetCallbacks();
  RoR::RendererOgreNextChildResult result = Run(malformed);
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::REJECTED_CHILD_INTENT &&
              g_calls.empty(),
          "malformed renderer intent reached runtime callbacks");

  RoR::RendererOgreNextChildRuntime invalid_runtimes[3] = {
      Runtime(), Runtime(), Runtime()};
  invalid_runtimes[0].version = 1U;
  invalid_runtimes[1].collect_native_preflight = nullptr;
  invalid_runtimes[2].bootstrap_frontend = nullptr;
  for (const RoR::RendererOgreNextChildRuntime &invalid_runtime :
       invalid_runtimes) {
    ResetCallbacks();
    result = Run(HeadlessArguments(), invalid_runtime);
    Require(result.status ==
                RoR::RendererOgreNextChildStatus::REJECTED_INVALID_RUNTIME &&
                g_calls.empty(),
            "invalid runtime contract invoked callbacks");
  }

  std::vector<NativeString> require_native = ProductionArguments(
      MakeEndpoint(RoR::RendererBridgeRole::PRESENTATION_FRONTEND),
      std::vector<NativeString>{ROR_NATIVE_TEXT("RoR")});
  require_native[3] = ROR_NATIVE_TEXT(
      "--ror-renderer-child-directional-shadows=require-native");
  require_native[4] = CurrentNativeBackendArgument();
  ResetCallbacks();
  result = Run(require_native);
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::REJECTED_STARTUP_PLAN &&
              result.bridge_status ==
                  RoR::RendererBridgeEndpointArgvStatus::READY &&
              result.frontend_request.has_bridge_endpoint &&
              result.startup_plan.status ==
                  RoR::RendererStartupSelectionStatus::
                      REJECTED_NATIVE_REQUIRED_UNAVAILABLE &&
              g_calls.size() == 1U && g_calls[0] == 1,
          "production require-native did not parse endpoint before preflight");
}

void TestFrontendFailuresAndContradictions() {
  struct Case {
    RoR::RendererOgreNextFrontendBootstrapStatus frontend;
    RoR::RendererOgreNextChildStatus child;
  };
  const Case cases[] = {
      {RoR::RendererOgreNextFrontendBootstrapStatus::
           REJECTED_STARTUP_PATH,
       RoR::RendererOgreNextChildStatus::
           REJECTED_UNSUPPORTED_STARTUP_PATH},
      {RoR::RendererOgreNextFrontendBootstrapStatus::INITIALIZATION_FAILED,
       RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INITIALIZATION},
      {RoR::RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED,
       RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_SHUTDOWN},
      {RoR::RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL,
       RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL},
  };
  for (const Case &test_case : cases) {
    ResetCallbacks();
    g_use_default_frontend_result = false;
    g_frontend_status = test_case.frontend;
    g_frontend_accepted = false;
    g_frontend_completed = false;
    const RoR::RendererOgreNextChildResult result =
        Run(HeadlessArguments());
    Require(!result.accepted && !result.completed &&
                result.status == test_case.child &&
                g_calls.size() == 2U,
            "frontend failure classification changed");
  }

  ResetCallbacks();
  g_use_default_frontend_result = false;
  g_frontend_accepted = false;
  g_frontend_completed = false;
  RoR::RendererOgreNextChildResult result = Run(HeadlessArguments());
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "unaccepted completed result did not fail closed");

  ResetCallbacks();
  g_use_default_frontend_result = false;
  g_frontend_status =
      RoR::RendererOgreNextFrontendBootstrapStatus::INITIALIZATION_FAILED;
  g_frontend_accepted = false;
  g_frontend_completed = false;
  g_frontend_version = 1U;
  result = Run(HeadlessArguments());
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "foreign frontend result version was trusted");

  ResetCallbacks();
  g_use_default_frontend_result = false;
  g_frontend_status =
      RoR::RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED;
  g_frontend_accepted = true;
  g_frontend_completed = true;
  result = Run(HeadlessArguments());
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "accepted completed failure did not fail closed");

  ResetCallbacks();
  g_use_default_frontend_result = false;
  g_frontend_status = static_cast<
      RoR::RendererOgreNextFrontendBootstrapStatus>(255U);
  g_frontend_accepted = false;
  g_frontend_completed = false;
  result = Run(HeadlessArguments());
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "unknown frontend status did not fail closed");

  const std::vector<NativeString> production = ProductionArguments(
      MakeEndpoint(RoR::RendererBridgeRole::PRESENTATION_FRONTEND),
      std::vector<NativeString>{ROR_NATIVE_TEXT("RoR")});
  ResetCallbacks();
  g_use_default_frontend_result = false;
  g_frontend_mode =
      RoR::RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE;
  g_frontend_status =
      RoR::RendererOgreNextFrontendBootstrapStatus::COMPLETED;
  g_frontend_accepted = true;
  g_frontend_completed = false;
  result = Run(production);
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "incomplete completion was accepted for a production bridge");

  ResetCallbacks();
  g_use_default_frontend_result = false;
  g_frontend_mode =
      RoR::RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE;
  g_frontend_status = RoR::RendererOgreNextFrontendBootstrapStatus::
      ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION;
  g_frontend_accepted = true;
  g_frontend_completed = false;
  result = Run(HeadlessArguments());
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "production acknowledgement was accepted for the probe path");

  ResetCallbacks();
  g_use_default_frontend_result = false;
  g_frontend_mode =
      RoR::RendererOgreNextChildInvocationMode::PROBE_HEADLESS;
  g_frontend_status = RoR::RendererOgreNextFrontendBootstrapStatus::
      ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION;
  g_frontend_accepted = true;
  g_frontend_completed = false;
  result = Run(production);
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "cross-mode frontend result was trusted");

  RoR::RendererOgreNextChildRuntime throwing_preflight = Runtime();
  throwing_preflight.collect_native_preflight = &ThrowingPreflight;
  ResetCallbacks();
  result = Run(production, throwing_preflight);
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_INTERNAL &&
              !result.accepted && !result.completed && g_calls.empty(),
          "preflight exception crossed the noexcept child boundary");

  RoR::RendererOgreNextChildRuntime throwing_frontend = Runtime();
  throwing_frontend.bootstrap_frontend = &ThrowingFrontend;
  ResetCallbacks();
  result = Run(production, throwing_frontend);
  Require(result.status ==
              RoR::RendererOgreNextChildStatus::FAILED_INTERNAL &&
              !result.accepted && !result.completed &&
              g_calls.size() == 1U && g_calls[0] == 1,
          "frontend exception crossed the noexcept child boundary");
}

void TestPureOrchestrationDoesNotAdoptNativeHandles() {
#if defined(_WIN32)
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE inbound_read = nullptr;
  HANDLE inbound_write = nullptr;
  HANDLE outbound_read = nullptr;
  HANDLE outbound_write = nullptr;
  Require(::CreatePipe(&inbound_read, &inbound_write, &security, 0) != FALSE &&
              ::CreatePipe(&outbound_read, &outbound_write, &security, 0) !=
                  FALSE,
          "could not create inherited Windows test pipes");
  DWORD inbound_flags_before = 0U;
  DWORD outbound_flags_before = 0U;
  Require(::GetHandleInformation(inbound_read, &inbound_flags_before) != FALSE &&
              ::GetHandleInformation(
                  outbound_write, &outbound_flags_before) != FALSE,
          "could not inspect inherited Windows test handles");
  const std::uint64_t inbound = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(inbound_read));
  const std::uint64_t outbound = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(outbound_write));
#else
  int inbound_pipe[2] = {-1, -1};
  int outbound_pipe[2] = {-1, -1};
  Require(::pipe(inbound_pipe) == 0 && ::pipe(outbound_pipe) == 0,
          "could not create inherited POSIX test pipes");
  const int inbound_flags_before = ::fcntl(inbound_pipe[0], F_GETFD);
  const int outbound_flags_before = ::fcntl(outbound_pipe[1], F_GETFD);
  Require(inbound_flags_before >= 0 && outbound_flags_before >= 0,
          "could not inspect inherited POSIX test descriptors");
  const std::uint64_t inbound =
      static_cast<std::uint64_t>(inbound_pipe[0]);
  const std::uint64_t outbound =
      static_cast<std::uint64_t>(outbound_pipe[1]);
#endif

  const RoR::RendererBridgeEndpoint endpoint = MakeEndpoint(
      RoR::RendererBridgeRole::PRESENTATION_FRONTEND, inbound, outbound);
  const std::vector<NativeString> arguments = ProductionArguments(
      endpoint, std::vector<NativeString>{ROR_NATIVE_TEXT("RoR")});
  ResetCallbacks();
  const RoR::RendererOgreNextChildResult result = Run(arguments);
  Require(result.accepted && result.completed &&
              result.frontend_request.bridge_endpoint.
                      inbound_native_handle == inbound &&
              result.frontend_request.bridge_endpoint.
                      outbound_native_handle == outbound,
          "native-handle tokens were not preserved by orchestration");

  const char sent = 'x';
  char received = 0;
#if defined(_WIN32)
  DWORD inbound_flags_after = 0U;
  DWORD outbound_flags_after = 0U;
  DWORD count = 0U;
  Require(::GetHandleInformation(inbound_read, &inbound_flags_after) != FALSE &&
              ::GetHandleInformation(
                  outbound_write, &outbound_flags_after) != FALSE &&
              inbound_flags_after == inbound_flags_before &&
              outbound_flags_after == outbound_flags_before,
          "pure orchestration adopted or hardened Windows handles");
  Require(::WriteFile(inbound_write, &sent, 1U, &count, nullptr) != FALSE &&
              count == 1U &&
              ::ReadFile(inbound_read, &received, 1U, &count, nullptr) !=
                  FALSE &&
              count == 1U && received == sent,
          "pure orchestration consumed or closed the inbound Windows pipe");
  received = 0;
  Require(::WriteFile(outbound_write, &sent, 1U, &count, nullptr) != FALSE &&
              count == 1U &&
              ::ReadFile(outbound_read, &received, 1U, &count, nullptr) !=
                  FALSE &&
              count == 1U && received == sent,
          "pure orchestration consumed or closed the outbound Windows pipe");
  (void)::CloseHandle(inbound_read);
  (void)::CloseHandle(inbound_write);
  (void)::CloseHandle(outbound_read);
  (void)::CloseHandle(outbound_write);
#else
  Require(::fcntl(inbound_pipe[0], F_GETFD) == inbound_flags_before &&
              ::fcntl(outbound_pipe[1], F_GETFD) == outbound_flags_before,
          "pure orchestration adopted or hardened POSIX descriptors");
  Require(::write(inbound_pipe[1], &sent, 1U) == 1 &&
              ::read(inbound_pipe[0], &received, 1U) == 1 &&
              received == sent,
          "pure orchestration consumed or closed the inbound POSIX pipe");
  received = 0;
  Require(::write(outbound_pipe[1], &sent, 1U) == 1 &&
              ::read(outbound_pipe[0], &received, 1U) == 1 &&
              received == sent,
          "pure orchestration consumed or closed the outbound POSIX pipe");
  (void)::close(inbound_pipe[0]);
  (void)::close(inbound_pipe[1]);
  (void)::close(outbound_pipe[0]);
  (void)::close(outbound_pipe[1]);
#endif
}

} // namespace

int main() {
  Require(CurrentPlatform() != RoR::HostRenderPlatform::UNKNOWN,
          "test host is unsupported");
  TestStatusAndImmutableAvailabilityContracts();
  TestProbeHeadlessBootstrapAndPreferNativeFallback();
  TestProductionBridgeAcceptanceAndGameSuffix();
  TestBridgeFailuresHappenBeforeNativeCallbacks();
  TestIntentRuntimeAndStartupRejections();
  TestFrontendFailuresAndContradictions();
  TestPureOrchestrationDoesNotAdoptNativeHandles();
  std::cout << "renderer Ogre-Next child bridge orchestration tests passed\n";
  return EXIT_SUCCESS;
}
