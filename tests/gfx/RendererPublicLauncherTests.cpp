/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererPublicLauncher.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {

#if defined(_WIN32)
#define ROR_NATIVE_TEXT(value) L##value
#else
#define ROR_NATIVE_TEXT(value) value
#endif

using NativeString = std::basic_string<RoR::RendererChildLauncherChar>;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer public launcher test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

RoR::HostRenderPlatform CurrentPlatform() {
#if defined(_WIN32)
  return RoR::HostRenderPlatform::WINDOWS;
#elif defined(__APPLE__)
  return RoR::HostRenderPlatform::MACOS;
#else
  return RoR::HostRenderPlatform::LINUX;
#endif
}

template <std::size_t Count>
RoR::RendererPublicLauncherArguments Parse(
    const RoR::RendererChildLauncherChar *const (&arguments)[Count]) {
  return RoR::ParseRendererPublicLauncherArguments(
      static_cast<int>(Count), arguments);
}

template <std::size_t Count>
void RequireForwarded(
    const RoR::RendererPublicLauncherArguments &parsed,
    const RoR::RendererChildLauncherChar *const (&expected)[Count],
    const char *message) {
  Require(parsed.forwarded_arguments.size() == Count, message);
  for (std::size_t index = 0U; index < Count; ++index) {
    Require(parsed.forwarded_arguments[index] == expected[index], message);
    Require(NativeString(parsed.forwarded_arguments[index]) == expected[index],
            message);
  }
}

void TestStatusContracts() {
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const auto argument_status =
        static_cast<RoR::RendererPublicLauncherArgumentStatus>(value);
    Require(RoR::IsKnownRendererPublicLauncherArgumentStatus(argument_status) ==
                (value <= 5U),
            "argument status classifier accepted an unknown value");
    const auto decision_status =
        static_cast<RoR::RendererPublicLauncherDecisionStatus>(value);
    Require(RoR::IsKnownRendererPublicLauncherDecisionStatus(decision_status) ==
                (value <= 3U),
            "decision status classifier accepted an unknown value");
  }
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererPublicLauncherArgumentStatus::
                      REJECTED_DUPLICATE_OPTION),
              "rejected-duplicate-option") == 0,
          "argument status string changed");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererPublicLauncherDecisionStatus::
                      REJECTED_OGRE_NEXT_CHILD_INTENT_ENCODING_UNAVAILABLE),
              "rejected-ogre-next-child-intent-encoding-unavailable") == 0,
          "decision status string changed");
  Require(std::strcmp(
              RoR::ToString(
                  static_cast<RoR::RendererPublicLauncherDecisionStatus>(
                      255U)),
              "invalid") == 0,
          "unknown decision status did not fail closed");
  Require(RoR::kRendererOgreNextChildIntentArgvContractVersion == 0U,
          "phase-2 build unexpectedly enabled Ogre-Next intent encoding");
}

void TestImmutablePackageFactsAndDefaultFallback() {
  const RoR::RendererStartupPackageAvailability package =
      RoR::RendererPublicLauncherPackageAvailability();
  Require(package.version == RoR::kRendererStartupHandoffContractVersion,
          "generated package-fact version changed");
  Require(package.package_platform == CurrentPlatform(),
          "generated package platform differs from the compiled host");
  Require(package.ogre14_child_present,
          "generated package omitted the OGRE 14 child");
  Require(!package.ogre_next_child_present &&
              !package.ogre_next_child_production_ready &&
              !package.ogre_next_pssm_admitted &&
              package.native_directional_shadow_backend ==
                  RoR::NativeRayTracingBackend::NONE,
          "generated package facts admitted an Ogre-Next/probe path");

  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("launcher"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("CityWorld")};
  const auto parsed = Parse(arguments);
  Require(parsed.accepted &&
              parsed.status ==
                  RoR::RendererPublicLauncherArgumentStatus::READY,
          "default arguments were rejected");
  Require(parsed.intent.frontend ==
                  RoR::RendererFrontendPreference::OGRE_NEXT_PREFER &&
              parsed.intent.directional_shadows ==
                  RoR::DirectionalShadowPreference::PSSM &&
              !parsed.intent.frontend_was_explicit &&
              !parsed.intent.directional_shadows_were_explicit,
          "default normalized intent changed");
  const RoR::RendererChildLauncherChar *expected[] = {
      arguments[0], arguments[1], arguments[2]};
  RequireForwarded(parsed, expected, "default legacy suffix changed");

  const auto decision = RoR::ResolveRendererPublicLauncherDecision(
      parsed.intent, package);
  Require(decision.accepted &&
              decision.status ==
                  RoR::RendererPublicLauncherDecisionStatus::READY_OGRE14 &&
              decision.handoff.accepted &&
              decision.handoff.child ==
                  RoR::RendererFrontendChild::OGRE14 &&
              decision.handoff.status == RoR::RendererStartupHandoffStatus::
                  FALLBACK_TO_OGRE14_CHILD &&
              decision.handoff.used_frontend_fallback &&
              !decision.handoff.used_shadow_fallback,
          "default Ogre-Next preference did not safely fall back to OGRE 14");
}

void TestOwnedPrefixFilteringAndExplicitLegacy() {
  const RoR::RendererChildLauncherChar *arguments[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-frontend=legacy-only"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("-map"), ROR_NATIVE_TEXT("City World"),
      ROR_NATIVE_TEXT(""), ROR_NATIVE_TEXT("unicode-\u03a9")};
  const auto parsed = Parse(arguments);
  Require(parsed.accepted && parsed.intent.frontend_was_explicit &&
              parsed.intent.directional_shadows_were_explicit &&
              parsed.intent.frontend ==
                  RoR::RendererFrontendPreference::LEGACY_ONLY &&
              parsed.intent.directional_shadows ==
                  RoR::DirectionalShadowPreference::PSSM,
          "explicit legacy intent was not normalized");
  const RoR::RendererChildLauncherChar *expected[] = {
      arguments[0], arguments[3], arguments[4], arguments[5], arguments[6]};
  RequireForwarded(parsed, expected,
                   "launcher-owned options were not filtered exactly");

  const auto decision = RoR::ResolveRendererPublicLauncherDecision(
      parsed.intent, RoR::RendererPublicLauncherPackageAvailability());
  Require(decision.accepted &&
              decision.handoff.child ==
                  RoR::RendererFrontendChild::OGRE14 &&
              decision.handoff.status == RoR::RendererStartupHandoffStatus::
                  SELECTED_REQUESTED_CHILD &&
              !decision.handoff.used_frontend_fallback,
          "explicit legacy intent did not select OGRE 14 directly");
}

void TestInitialPrefixBoundary() {
  const RoR::RendererChildLauncherChar *legacy_value[] = {
      ROR_NATIVE_TEXT("launcher"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("--renderer-frontend=legacy-only"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows=require-native")};
  const auto after_legacy_option = Parse(legacy_value);
  Require(after_legacy_option.accepted &&
              !after_legacy_option.intent.frontend_was_explicit &&
              !after_legacy_option.intent.directional_shadows_were_explicit,
          "renderer-shaped legacy values were consumed");
  const RoR::RendererChildLauncherChar *legacy_expected[] = {
      legacy_value[0], legacy_value[1], legacy_value[2], legacy_value[3]};
  RequireForwarded(after_legacy_option, legacy_expected,
                   "legacy suffix was not byte/code-unit exact");

  const RoR::RendererChildLauncherChar *after_separator[] = {
      ROR_NATIVE_TEXT("launcher"), ROR_NATIVE_TEXT("--"),
      ROR_NATIVE_TEXT("--renderer-frontend=legacy-only")};
  const auto separated = Parse(after_separator);
  Require(separated.accepted &&
              !separated.intent.frontend_was_explicit,
          "renderer option after -- was consumed");
  const RoR::RendererChildLauncherChar *separator_expected[] = {
      after_separator[0], after_separator[1], after_separator[2]};
  RequireForwarded(separated, separator_expected,
                   "-- suffix was not preserved exactly");

  const RoR::RendererChildLauncherChar *frontend_near_collision[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-frontend-extra=legacy-only"),
      ROR_NATIVE_TEXT("--renderer-frontend=legacy-only")};
  const auto frontend_unowned = Parse(frontend_near_collision);
  Require(frontend_unowned.accepted &&
              !frontend_unowned.intent.frontend_was_explicit,
          "near-colliding frontend option was claimed by the launcher");
  RequireForwarded(frontend_unowned, frontend_near_collision,
                   "near-colliding frontend suffix was not preserved");

  const RoR::RendererChildLauncherChar *shadow_near_collision[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows-extra=pssm"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows=require-native")};
  const auto shadow_unowned = Parse(shadow_near_collision);
  Require(shadow_unowned.accepted &&
              !shadow_unowned.intent.directional_shadows_were_explicit,
          "near-colliding shadow option was claimed by the launcher");
  RequireForwarded(shadow_unowned, shadow_near_collision,
                   "near-colliding shadow suffix was not preserved");
}

void TestShadowFallbackAndHardGates() {
  const RoR::RendererChildLauncherChar *prefer_native_arguments[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows=prefer-native")};
  const auto prefer_native = Parse(prefer_native_arguments);
  const auto prefer_decision = RoR::ResolveRendererPublicLauncherDecision(
      prefer_native.intent, RoR::RendererPublicLauncherPackageAvailability());
  Require(prefer_decision.accepted &&
              prefer_decision.handoff.child ==
                  RoR::RendererFrontendChild::OGRE14 &&
              prefer_decision.handoff.used_frontend_fallback &&
              prefer_decision.handoff.used_shadow_fallback,
          "prefer-native lost the safe legacy/PSSM fallback");

  const RoR::RendererChildLauncherChar *require_next_arguments[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-frontend=ogre-next-require")};
  const auto require_next = Parse(require_next_arguments);
  const auto require_next_decision =
      RoR::ResolveRendererPublicLauncherDecision(
          require_next.intent,
          RoR::RendererPublicLauncherPackageAvailability());
  Require(!require_next_decision.accepted &&
              require_next_decision.status ==
                  RoR::RendererPublicLauncherDecisionStatus::
                      REJECTED_HANDOFF &&
              require_next_decision.handoff.status ==
                  RoR::RendererStartupHandoffStatus::
                      REJECTED_REQUIRED_OGRE_NEXT_CHILD_UNAVAILABLE,
          "Ogre-Next-required intent crossed the absent-child gate");

  const RoR::RendererChildLauncherChar *require_native_arguments[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows=require-native")};
  const auto require_native = Parse(require_native_arguments);
  const auto require_native_decision =
      RoR::ResolveRendererPublicLauncherDecision(
          require_native.intent,
          RoR::RendererPublicLauncherPackageAvailability());
  Require(!require_native_decision.accepted &&
              require_native_decision.status ==
                  RoR::RendererPublicLauncherDecisionStatus::
                      REJECTED_HANDOFF &&
              require_native_decision.handoff.status ==
                  RoR::RendererStartupHandoffStatus::
                      REJECTED_NATIVE_REQUIRED_UNAVAILABLE,
          "native-required intent silently degraded");
}

void TestMalformedAndDuplicateOptions() {
  const RoR::RendererChildLauncherChar *missing_equals[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-frontend")};
  Require(Parse(missing_equals).status ==
              RoR::RendererPublicLauncherArgumentStatus::
                  REJECTED_MALFORMED_OPTION,
          "missing equals was not rejected");

  const RoR::RendererChildLauncherChar *invalid_value[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-frontend=next-ish")};
  Require(Parse(invalid_value).status ==
              RoR::RendererPublicLauncherArgumentStatus::
                  REJECTED_INVALID_OPTION_VALUE,
          "unknown frontend value was not rejected");

  const RoR::RendererChildLauncherChar *missing_shadow_equals[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows")};
  Require(Parse(missing_shadow_equals).status ==
              RoR::RendererPublicLauncherArgumentStatus::
                  REJECTED_MALFORMED_OPTION,
          "missing shadow equals was not rejected");

  const RoR::RendererChildLauncherChar *invalid_shadow[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows=maybe-native")};
  Require(Parse(invalid_shadow).status ==
              RoR::RendererPublicLauncherArgumentStatus::
                  REJECTED_INVALID_OPTION_VALUE,
          "unknown shadow value was not rejected");

  const RoR::RendererChildLauncherChar *duplicate[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--renderer-directional-shadows=prefer-native")};
  Require(Parse(duplicate).status ==
              RoR::RendererPublicLauncherArgumentStatus::
                  REJECTED_DUPLICATE_OPTION,
          "duplicate shadow option was not rejected");

  const RoR::RendererChildLauncherChar *duplicate_frontend[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--renderer-frontend=legacy-only")};
  Require(Parse(duplicate_frontend).status ==
              RoR::RendererPublicLauncherArgumentStatus::
                  REJECTED_DUPLICATE_OPTION,
          "duplicate frontend option was not rejected");

  const RoR::RendererChildLauncherChar *embedded_null[] = {
      ROR_NATIVE_TEXT("launcher"), nullptr};
  const auto invalid_arguments = RoR::ParseRendererPublicLauncherArguments(
      2, embedded_null);
  Require(!invalid_arguments.accepted &&
              invalid_arguments.status ==
                  RoR::RendererPublicLauncherArgumentStatus::
                      REJECTED_INVALID_ARGUMENT_VECTOR,
          "embedded null argument escaped validation");
  Require(!RoR::ParseRendererPublicLauncherArguments(0, nullptr).accepted,
          "empty argument vector escaped validation");
}

void TestInvalidNormalizedIntent() {
  RoR::RendererPublicLauncherIntent invalid_version;
  invalid_version.version = RoR::kRendererPublicLauncherContractVersion + 1U;
  Require(!RoR::ResolveRendererPublicLauncherDecision(
               invalid_version,
               RoR::RendererPublicLauncherPackageAvailability())
               .accepted,
          "unknown normalized-intent version escaped validation");

  RoR::RendererPublicLauncherIntent invalid_frontend;
  invalid_frontend.frontend =
      static_cast<RoR::RendererFrontendPreference>(255U);
  const auto decision = RoR::ResolveRendererPublicLauncherDecision(
      invalid_frontend, RoR::RendererPublicLauncherPackageAvailability());
  Require(!decision.accepted &&
              decision.status ==
                  RoR::RendererPublicLauncherDecisionStatus::
                      REJECTED_INVALID_INTENT,
          "unknown normalized frontend escaped validation");
}

void TestFutureOgreNextIntentGate() {
  RoR::RendererStartupPackageAvailability future =
      RoR::RendererPublicLauncherPackageAvailability();
  future.ogre_next_child_present = true;
  future.ogre_next_child_production_ready = true;
  future.ogre_next_pssm_admitted = true;
  const RoR::RendererPublicLauncherIntent intent;
  const auto decision =
      RoR::ResolveRendererPublicLauncherDecision(intent, future);
  Require(!decision.accepted && decision.handoff.accepted &&
              decision.handoff.child ==
                  RoR::RendererFrontendChild::OGRE_NEXT &&
              decision.status ==
                  RoR::RendererPublicLauncherDecisionStatus::
                      REJECTED_OGRE_NEXT_CHILD_INTENT_ENCODING_UNAVAILABLE,
          "future Ogre-Next facts bypassed the missing intent encoder");
}

void TestStableRuntimeFailureCodes() {
  const RoR::RendererChildLauncherChar *invalid[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-frontend=invalid")};
  Require(RoR::RunRendererPublicLauncher(2, invalid) ==
              RoR::kRendererPublicLauncherUsageExitCode,
          "invalid option did not use the stable usage exit code");

  const RoR::RendererChildLauncherChar *hard_gate[] = {
      ROR_NATIVE_TEXT("launcher"),
      ROR_NATIVE_TEXT("--renderer-frontend=ogre-next-require")};
  Require(RoR::RunRendererPublicLauncher(2, hard_gate) ==
              RoR::kRendererPublicLauncherSelectionExitCode,
          "handoff rejection did not use the stable selection exit code");

  // This test target is deliberately isolated from every fake/production
  // child output. The immutable default selects the missing RoR-Ogre14 sibling
  // and must map the structured OS launch failure without changing cwd/PATH.
  const RoR::RendererChildLauncherChar *missing_child[] = {
      ROR_NATIVE_TEXT("launcher")};
  Require(RoR::RunRendererPublicLauncher(1, missing_child) ==
              RoR::kRendererPublicLauncherChildLaunchExitCode,
          "missing legacy child did not use the stable launch-failure code");
}

} // namespace

int main() {
  TestStatusContracts();
  TestImmutablePackageFactsAndDefaultFallback();
  TestOwnedPrefixFilteringAndExplicitLegacy();
  TestInitialPrefixBoundary();
  TestShadowFallbackAndHardGates();
  TestMalformedAndDuplicateOptions();
  TestInvalidNormalizedIntent();
  TestFutureOgreNextIntentGate();
  TestStableRuntimeFailureCodes();
  return EXIT_SUCCESS;
}
