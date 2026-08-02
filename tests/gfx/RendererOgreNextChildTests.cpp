/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextChild.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

#if defined(_WIN32)
#define ROR_NATIVE_TEXT(value) L##value
#else
#define ROR_NATIVE_TEXT(value) value
#endif

std::vector<int> g_calls;
RoR::RendererOgreNextFrontendBootstrapStatus g_frontend_status =
    RoR::RendererOgreNextFrontendBootstrapStatus::COMPLETED;
bool g_frontend_completed = true;
std::uint32_t g_frontend_version =
    RoR::kRendererOgreNextChildContractVersion;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer Ogre-Next child test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
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

RoR::RendererNativeShadowPreflight CollectPreflight() {
  g_calls.push_back(1);
  return RoR::CollectRendererOgreNextChildNativePreflight();
}

RoR::RendererOgreNextFrontendBootstrapResult BootstrapFrontend(
    const RoR::RendererStartupPlanResult &plan) {
  g_calls.push_back(2);
  Require(plan.accepted &&
              plan.effective_path ==
                  RoR::RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1,
          "frontend received an unadmitted startup plan");
  RoR::RendererOgreNextFrontendBootstrapResult result;
  result.version = g_frontend_version;
  result.status = g_frontend_status;
  result.completed = g_frontend_completed;
  return result;
}

RoR::RendererNativeShadowPreflight ThrowingPreflight() {
  throw 1;
}

RoR::RendererOgreNextFrontendBootstrapResult ThrowingFrontend(
    const RoR::RendererStartupPlanResult &) {
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
  g_frontend_status =
      RoR::RendererOgreNextFrontendBootstrapStatus::COMPLETED;
  g_frontend_completed = true;
  g_frontend_version = RoR::kRendererOgreNextChildContractVersion;
}

void TestStatusAndImmutableAvailabilityContracts() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const auto frontend =
        static_cast<RoR::RendererOgreNextFrontendBootstrapStatus>(value);
    Require(RoR::IsKnownRendererOgreNextFrontendBootstrapStatus(frontend) ==
                (value <= 4U),
            "frontend status classifier accepted an unknown value");
    const auto child = static_cast<RoR::RendererOgreNextChildStatus>(value);
    Require(RoR::IsKnownRendererOgreNextChildStatus(child) == (value <= 9U),
            "child status classifier accepted an unknown value");
  }
  Require(std::strcmp(
              RoR::ToString(RoR::RendererOgreNextChildStatus::
                                REJECTED_GAME_BRIDGE_UNAVAILABLE),
              "rejected-game-bridge-unavailable") == 0,
          "game bridge status string changed");
  Require(std::strcmp(
              RoR::ToString(static_cast<RoR::RendererOgreNextChildStatus>(
                  255U)),
              "invalid") == 0,
          "unknown child status did not fail closed");
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

void TestPssmBootstrapAndPreferNativeFallback() {
  const RoR::RendererChildLauncherChar *pssm[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  ResetCallbacks();
  const auto pssm_result = RoR::RunRendererOgreNextChild(5, pssm, Runtime());
  Require(pssm_result.completed &&
              pssm_result.status == RoR::RendererOgreNextChildStatus::
                                        COMPLETED_HEADLESS_BOOTSTRAP &&
              pssm_result.startup_plan.status ==
                  RoR::RendererStartupSelectionStatus::
                      SELECTED_REQUESTED_PATH &&
              !pssm_result.startup_plan.used_shadow_fallback &&
              g_calls.size() == 2U && g_calls[0] == 1 && g_calls[1] == 2,
          "PSSM child did not preserve preflight-plan-frontend order");

  const RoR::RendererChildLauncherChar *prefer_native[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-require"),
      ROR_NATIVE_TEXT(
          "--ror-renderer-child-directional-shadows=prefer-native"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  ResetCallbacks();
  const auto fallback =
      RoR::RunRendererOgreNextChild(5, prefer_native, Runtime());
  Require(fallback.completed && fallback.startup_plan.used_shadow_fallback &&
              fallback.startup_plan.status ==
                  RoR::RendererStartupSelectionStatus::
                      FALLBACK_TO_OGRE_NEXT_PSSM &&
              fallback.startup_plan.native_preflight_readiness ==
                  RoR::RendererNativePreflightReadiness::
                      BACKEND_UNAVAILABLE &&
              g_calls.size() == 2U,
          "prefer-native did not fall back to child-owned PSSM");
}

void TestRejectionsHappenBeforeFrontendCreation() {
  const RoR::RendererChildLauncherChar *malformed[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=2")};
  ResetCallbacks();
  const auto malformed_result =
      RoR::RunRendererOgreNextChild(2, malformed, Runtime());
  Require(!malformed_result.completed &&
              malformed_result.status ==
                  RoR::RendererOgreNextChildStatus::REJECTED_CHILD_INTENT &&
              g_calls.empty(),
          "malformed intent reached runtime callbacks");

  const RoR::RendererChildLauncherChar *game_suffix[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none"),
      ROR_NATIVE_TEXT("-map"), ROR_NATIVE_TEXT("CityWorld")};
  ResetCallbacks();
  const auto suffix_result =
      RoR::RunRendererOgreNextChild(7, game_suffix, Runtime());
  Require(!suffix_result.completed &&
              suffix_result.status == RoR::RendererOgreNextChildStatus::
                                          REJECTED_GAME_BRIDGE_UNAVAILABLE &&
              g_calls.empty(),
          "unimplemented game suffix reached native preflight");

  const RoR::RendererChildLauncherChar *require_native[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-require"),
      ROR_NATIVE_TEXT(
          "--ror-renderer-child-directional-shadows=require-native"),
      CurrentNativeBackendArgument()};
  ResetCallbacks();
  const auto native_result =
      RoR::RunRendererOgreNextChild(5, require_native, Runtime());
  Require(!native_result.completed &&
              native_result.status == RoR::RendererOgreNextChildStatus::
                                          REJECTED_STARTUP_PLAN &&
              native_result.startup_plan.status ==
                  RoR::RendererStartupSelectionStatus::
                      REJECTED_NATIVE_REQUIRED_UNAVAILABLE &&
              g_calls.size() == 1U && g_calls[0] == 1,
          "require-native reached frontend without a child-owned backend");

  RoR::RendererOgreNextChildRuntime invalid_runtime = Runtime();
  invalid_runtime.version = 0U;
  ResetCallbacks();
  const auto invalid =
      RoR::RunRendererOgreNextChild(5, require_native, invalid_runtime);
  Require(invalid.status ==
              RoR::RendererOgreNextChildStatus::REJECTED_INVALID_RUNTIME &&
              g_calls.empty(),
          "invalid runtime invoked callbacks");
}

void TestFrontendFailuresArePreserved() {
  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-require"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
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
    g_frontend_status = test_case.frontend;
    g_frontend_completed = false;
    const auto result =
        RoR::RunRendererOgreNextChild(5, arguments, Runtime());
    Require(!result.completed && result.status == test_case.child &&
                g_calls.size() == 2U,
            "frontend failure classification changed");
  }

  ResetCallbacks();
  g_frontend_completed = false;
  const auto contradictory =
      RoR::RunRendererOgreNextChild(5, arguments, Runtime());
  Require(contradictory.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "contradictory completed result did not fail closed");

  ResetCallbacks();
  g_frontend_status =
      RoR::RendererOgreNextFrontendBootstrapStatus::INITIALIZATION_FAILED;
  g_frontend_completed = false;
  g_frontend_version = 0U;
  const auto wrong_version =
      RoR::RunRendererOgreNextChild(5, arguments, Runtime());
  Require(wrong_version.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "foreign frontend result version was trusted");

  ResetCallbacks();
  g_frontend_status =
      RoR::RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED;
  const auto contradictory_failure =
      RoR::RunRendererOgreNextChild(5, arguments, Runtime());
  Require(contradictory_failure.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "completed failure result did not fail closed");

  ResetCallbacks();
  g_frontend_status =
      static_cast<RoR::RendererOgreNextFrontendBootstrapStatus>(255U);
  g_frontend_completed = false;
  const auto unknown_status =
      RoR::RunRendererOgreNextChild(5, arguments, Runtime());
  Require(unknown_status.status ==
              RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL,
          "unknown frontend status did not fail closed");

  RoR::RendererOgreNextChildRuntime throwing_preflight = Runtime();
  throwing_preflight.collect_native_preflight = &ThrowingPreflight;
  ResetCallbacks();
  const auto preflight_exception =
      RoR::RunRendererOgreNextChild(5, arguments, throwing_preflight);
  Require(preflight_exception.status ==
              RoR::RendererOgreNextChildStatus::FAILED_INTERNAL &&
              !preflight_exception.completed && g_calls.empty(),
          "preflight exception crossed the noexcept child boundary");

  RoR::RendererOgreNextChildRuntime throwing_frontend = Runtime();
  throwing_frontend.bootstrap_frontend = &ThrowingFrontend;
  ResetCallbacks();
  const auto frontend_exception =
      RoR::RunRendererOgreNextChild(5, arguments, throwing_frontend);
  Require(frontend_exception.status ==
              RoR::RendererOgreNextChildStatus::FAILED_INTERNAL &&
              !frontend_exception.completed && g_calls.size() == 1U &&
              g_calls[0] == 1,
          "frontend exception crossed the noexcept child boundary");
}

} // namespace

int main() {
  TestStatusAndImmutableAvailabilityContracts();
  TestPssmBootstrapAndPreferNativeFallback();
  TestRejectionsHappenBeforeFrontendCreation();
  TestFrontendFailuresArePreserved();
  std::cout << "renderer Ogre-Next child contract tests passed\n";
  return EXIT_SUCCESS;
}
