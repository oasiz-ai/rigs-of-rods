/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererPublicLauncher.h"
#include "RendererBridgeProcessSupervisor.h"
#include "RendererOgreNextChild.h"

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

NativeString NativeAscii(const char *value) {
  NativeString result;
  while (value != nullptr && *value != '\0') {
    result.push_back(static_cast<RoR::RendererChildLauncherChar>(
        static_cast<unsigned char>(*value)));
    ++value;
  }
  return result;
}

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

RoR::HostRenderPlatform ForeignPlatform() {
#if defined(_WIN32)
  return RoR::HostRenderPlatform::MACOS;
#else
  return RoR::HostRenderPlatform::WINDOWS;
#endif
}

const char *CurrentNativeBackendValue() {
#if defined(_WIN32)
  return "dxr";
#elif defined(__APPLE__)
  return "metal";
#else
  return "vulkan-khr";
#endif
}

const char *ForeignNativeBackendValue() {
#if defined(_WIN32)
  return "metal";
#else
  return "dxr";
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
                (value <= 4U),
            "decision status classifier accepted an unknown value");
    const auto child_intent_status =
        static_cast<RoR::RendererOgreNextChildIntentArgvStatus>(value);
    Require(RoR::IsKnownRendererOgreNextChildIntentArgvStatus(
                child_intent_status) == (value <= 6U),
            "child-intent status classifier accepted an unknown value");
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
                  RoR::RendererPublicLauncherDecisionStatus::
                      READY_OGRE_NEXT),
              "ready-ogre-next") == 0,
          "Ogre-Next ready decision string changed");
  Require(std::strcmp(
              RoR::ToString(
                  static_cast<RoR::RendererPublicLauncherDecisionStatus>(
                      255U)),
              "invalid") == 0,
          "unknown decision status did not fail closed");
  Require(RoR::kRendererOgreNextChildIntentArgvContractVersion == 1U,
          "Ogre-Next child-intent argv contract version changed");
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

void TestOgreNextIntentEncoding() {
  RoR::RendererStartupPackageAvailability future =
      RoR::RendererPublicLauncherPackageAvailability();
  future.ogre_next_child_present = true;
  future.ogre_next_child_production_ready = true;
  future.ogre_next_pssm_admitted = true;
  const RoR::RendererPublicLauncherIntent intent;
  const auto decision =
      RoR::ResolveRendererPublicLauncherDecision(intent, future);
  Require(decision.accepted && decision.handoff.accepted &&
              decision.handoff.child ==
                  RoR::RendererFrontendChild::OGRE_NEXT &&
              decision.status ==
                  RoR::RendererPublicLauncherDecisionStatus::
                      READY_OGRE_NEXT,
          "admitted Ogre-Next facts did not select the encoded child");

  const RoR::RendererChildLauncherChar *game_arguments[] = {
      ROR_NATIVE_TEXT("launcher"), ROR_NATIVE_TEXT("-map"),
      ROR_NATIVE_TEXT("City World"), ROR_NATIVE_TEXT(""),
      ROR_NATIVE_TEXT("unicode-\u03a9")};
  auto encoded = RoR::EncodeRendererOgreNextChildIntent(
      decision.handoff, 5, game_arguments);
  Require(encoded.accepted &&
              encoded.status ==
                  RoR::RendererOgreNextChildIntentArgvStatus::READY &&
              encoded.arguments.size() == 9U,
          "Ogre-Next child intent was not encoded");
  Require(encoded.startup.frontend ==
                  RoR::RendererFrontendPreference::OGRE_NEXT_PREFER &&
              encoded.startup.directional_shadows ==
                  RoR::DirectionalShadowPreference::PSSM &&
              encoded.startup.host_platform == CurrentPlatform() &&
              encoded.declared_native_backend ==
                  RoR::NativeRayTracingBackend::NONE,
          "encoded startup request changed");
  Require(encoded.arguments[0] == game_arguments[0] &&
              encoded.arguments[1] == ROR_NATIVE_TEXT(
                  "--ror-renderer-child-intent-version=1") &&
              encoded.arguments[2] == ROR_NATIVE_TEXT(
                  "--ror-renderer-child-frontend=ogre-next-prefer") &&
              encoded.arguments[3] == ROR_NATIVE_TEXT(
                  "--ror-renderer-child-directional-shadows=pssm") &&
              encoded.arguments[4] == ROR_NATIVE_TEXT(
                  "--ror-renderer-child-native-backend=none") &&
              encoded.arguments[5] == game_arguments[1] &&
              encoded.arguments[6] == game_arguments[2] &&
              encoded.arguments[7] == game_arguments[3] &&
              encoded.arguments[8] == game_arguments[4],
          "encoded child argv was not byte/code-unit exact");

  std::vector<const RoR::RendererChildLauncherChar *> encoded_pointers;
  encoded_pointers.reserve(encoded.arguments.size());
  for (const NativeString &argument : encoded.arguments) {
    encoded_pointers.push_back(argument.c_str());
  }
  const auto parsed = RoR::ParseRendererOgreNextChildIntent(
      static_cast<int>(encoded_pointers.size()), encoded_pointers.data());
  Require(parsed.accepted &&
              parsed.status ==
                  RoR::RendererOgreNextChildIntentArgvStatus::READY &&
              parsed.startup.frontend ==
                  RoR::RendererFrontendPreference::OGRE_NEXT_PREFER &&
              parsed.startup.directional_shadows ==
                  RoR::DirectionalShadowPreference::PSSM &&
              parsed.startup.host_platform == CurrentPlatform() &&
              parsed.declared_native_backend ==
                  RoR::NativeRayTracingBackend::NONE &&
              parsed.forwarded_arguments.size() == 5U,
          "Ogre-Next child did not decode the exact startup request");
  for (std::size_t index = 0U;
       index < parsed.forwarded_arguments.size(); ++index) {
    Require(NativeString(parsed.forwarded_arguments[index]) ==
                game_arguments[index],
            "decoded game suffix changed");
  }
  encoded.arguments.clear();
  encoded.arguments.shrink_to_fit();
  Require(parsed.forwarded_arguments[2] ==
              NativeString(ROR_NATIVE_TEXT("City World")) &&
              parsed.forwarded_arguments[3].empty(),
          "decoded game suffix still borrowed the source argv lifetime");

  RoR::RendererStartupHandoffResult invalid_handoff = decision.handoff;
  invalid_handoff.accepted = false;
  Require(!RoR::EncodeRendererOgreNextChildIntent(
               invalid_handoff, 5, game_arguments)
               .accepted,
          "rejected handoff entered the child encoder");
  Require(!RoR::EncodeRendererOgreNextChildIntent(
               decision.handoff, 0, nullptr)
               .accepted,
          "empty game argv entered the child encoder");
  RoR::RendererStartupHandoffResult foreign_handoff = decision.handoff;
  foreign_handoff.package_platform = ForeignPlatform();
  Require(!RoR::EncodeRendererOgreNextChildIntent(
               foreign_handoff, 5, game_arguments)
               .accepted,
          "foreign package platform entered the child encoder");
  RoR::RendererStartupHandoffResult wrong_child = decision.handoff;
  wrong_child.child = RoR::RendererFrontendChild::OGRE14;
  Require(!RoR::EncodeRendererOgreNextChildIntent(
               wrong_child, 5, game_arguments)
               .accepted,
          "legacy handoff entered the Ogre-Next child encoder");

  struct IntentCase {
    RoR::RendererFrontendPreference frontend;
    const char *frontend_value;
    RoR::DirectionalShadowPreference shadows;
    const char *shadow_value;
  };
  const IntentCase intent_cases[] = {
      {RoR::RendererFrontendPreference::OGRE_NEXT_PREFER,
       "ogre-next-prefer", RoR::DirectionalShadowPreference::PSSM,
       "pssm"},
      {RoR::RendererFrontendPreference::OGRE_NEXT_PREFER,
       "ogre-next-prefer",
       RoR::DirectionalShadowPreference::PREFER_NATIVE,
       "prefer-native"},
      {RoR::RendererFrontendPreference::OGRE_NEXT_PREFER,
       "ogre-next-prefer",
       RoR::DirectionalShadowPreference::REQUIRE_NATIVE,
       "require-native"},
      {RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE,
       "ogre-next-require", RoR::DirectionalShadowPreference::PSSM,
       "pssm"},
      {RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE,
       "ogre-next-require",
       RoR::DirectionalShadowPreference::PREFER_NATIVE,
       "prefer-native"},
      {RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE,
       "ogre-next-require",
       RoR::DirectionalShadowPreference::REQUIRE_NATIVE,
       "require-native"},
  };
  future.native_directional_shadow_backend =
      RoR::ExpectedNativeRayTracingBackend(CurrentPlatform());
  for (const IntentCase &intent_case : intent_cases) {
    RoR::RendererPublicLauncherIntent candidate;
    candidate.frontend = intent_case.frontend;
    candidate.directional_shadows = intent_case.shadows;
    const auto candidate_decision =
        RoR::ResolveRendererPublicLauncherDecision(candidate, future);
    Require(candidate_decision.accepted &&
                candidate_decision.status ==
                    RoR::RendererPublicLauncherDecisionStatus::
                        READY_OGRE_NEXT,
            "valid Ogre-Next child intent was not selected");
    const auto candidate_encoding =
        RoR::EncodeRendererOgreNextChildIntent(
            candidate_decision.handoff, 5, game_arguments);
    Require(candidate_encoding.accepted &&
                candidate_encoding.arguments[2] ==
                    NativeAscii("--ror-renderer-child-frontend=") +
                        NativeAscii(intent_case.frontend_value) &&
                candidate_encoding.arguments[3] ==
                    NativeAscii(
                        "--ror-renderer-child-directional-shadows=") +
                        NativeAscii(intent_case.shadow_value) &&
                candidate_encoding.arguments[4] ==
                    NativeAscii("--ror-renderer-child-native-backend=") +
                        NativeAscii(CurrentNativeBackendValue()),
            "Ogre-Next intent value encoding changed");
    std::vector<const RoR::RendererChildLauncherChar *> candidate_pointers;
    for (const NativeString &argument : candidate_encoding.arguments) {
      candidate_pointers.push_back(argument.c_str());
    }
    const auto candidate_parse =
        RoR::ParseRendererOgreNextChildIntent(
            static_cast<int>(candidate_pointers.size()),
            candidate_pointers.data());
    Require(candidate_parse.accepted &&
                candidate_parse.startup.frontend ==
                    intent_case.frontend &&
                candidate_parse.startup.directional_shadows ==
                    intent_case.shadows &&
                candidate_parse.declared_native_backend ==
                    RoR::ExpectedNativeRayTracingBackend(CurrentPlatform()),
            "Ogre-Next intent value round trip changed");
  }
}

void TestOgreNextIntentDecoderRejectsMalformedPrefixes() {
  const RoR::RendererChildLauncherChar *wrong_version[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=2"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  Require(RoR::ParseRendererOgreNextChildIntent(
              5, wrong_version)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_MISSING_CONTRACT,
          "unknown child-intent contract version was accepted");

  const RoR::RendererChildLauncherChar *legacy_frontend[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=legacy-only"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  Require(RoR::ParseRendererOgreNextChildIntent(
              5, legacy_frontend)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_MALFORMED_CONTRACT,
          "legacy frontend entered the Ogre-Next child");

  const RoR::RendererChildLauncherChar *reordered[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  Require(RoR::ParseRendererOgreNextChildIntent(
              5, reordered)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_MALFORMED_CONTRACT,
          "reordered child-intent fields were accepted");

  const RoR::RendererChildLauncherChar *invalid_shadow[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT(
          "--ror-renderer-child-directional-shadows=maybe-native"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  Require(RoR::ParseRendererOgreNextChildIntent(
              5, invalid_shadow)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_MALFORMED_CONTRACT,
          "unknown child shadow intent was accepted");

  const RoR::RendererChildLauncherChar *embedded_null[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"), nullptr,
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  Require(RoR::ParseRendererOgreNextChildIntent(
              5, embedded_null)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_INVALID_ARGUMENTS,
          "null child-intent field was accepted");
  const NativeString foreign_backend =
      NativeAscii("--ror-renderer-child-native-backend=") +
      NativeAscii(ForeignNativeBackendValue());
  const RoR::RendererChildLauncherChar *foreign_native[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      foreign_backend.c_str()};
  Require(RoR::ParseRendererOgreNextChildIntent(5, foreign_native)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_MALFORMED_CONTRACT,
          "foreign native backend entered this compiled child");

  const RoR::RendererChildLauncherChar *required_without_backend[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-require"),
      ROR_NATIVE_TEXT(
          "--ror-renderer-child-directional-shadows=require-native"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  Require(RoR::ParseRendererOgreNextChildIntent(
              5, required_without_backend)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_MALFORMED_CONTRACT,
          "required-native intent without a package backend was accepted");

  const RoR::RendererChildLauncherChar *reserved_duplicate[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-require")};
  Require(RoR::ParseRendererOgreNextChildIntent(6, reserved_duplicate)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_MALFORMED_CONTRACT,
          "duplicate reserved child-intent record entered the game suffix");

  const RoR::RendererChildLauncherChar *near_collision[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none"),
      ROR_NATIVE_TEXT("--ror-renderer-childish=game-value")};
  const auto direct_parse =
      RoR::ParseRendererOgreNextChildIntent(6, near_collision);
  Require(direct_parse.accepted &&
              direct_parse.forwarded_arguments.size() == 2U &&
              direct_parse.forwarded_arguments[1] ==
                  NativeString(
                      ROR_NATIVE_TEXT("--ror-renderer-childish=game-value")),
          "near-collision game argument was consumed as launcher intent");

  const RoR::RendererChildLauncherChar *null_suffix[] = {
      ROR_NATIVE_TEXT("RoR-OgreNext"),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none"), nullptr};
  Require(RoR::ParseRendererOgreNextChildIntent(6, null_suffix)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_INVALID_ARGUMENTS,
          "null child game suffix was accepted");
  const RoR::RendererChildLauncherChar *empty_argv0[] = {
      ROR_NATIVE_TEXT(""),
      ROR_NATIVE_TEXT("--ror-renderer-child-intent-version=1"),
      ROR_NATIVE_TEXT("--ror-renderer-child-frontend=ogre-next-prefer"),
      ROR_NATIVE_TEXT("--ror-renderer-child-directional-shadows=pssm"),
      ROR_NATIVE_TEXT("--ror-renderer-child-native-backend=none")};
  Require(RoR::ParseRendererOgreNextChildIntent(5, empty_argv0)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_INVALID_ARGUMENTS,
          "empty child argv[0] was accepted");
  Require(RoR::ParseRendererOgreNextChildIntent(0, nullptr)
              .status == RoR::RendererOgreNextChildIntentArgvStatus::
                  REJECTED_INVALID_ARGUMENTS,
          "null child argv was accepted");
  Require(std::strcmp(
              RoR::ToString(
                  RoR::RendererOgreNextChildIntentArgvStatus::
                      REJECTED_MALFORMED_CONTRACT),
              "rejected-malformed-contract") == 0,
          "child-intent status string changed");
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

void TestPreReadyFallbackBoundary() {
  RoR::RendererPublicLauncherIntent intent;
  RoR::RendererBridgeProcessResult bridge;
  bridge.status =
      RoR::RendererBridgeProcessStatus::PRESENTATION_EXITED_FIRST;
  bridge.first_exit =
      RoR::RendererBridgeObservedChild::PRESENTATION_FRONTEND;
  bridge.presentation_exit_kind =
      RoR::RendererBridgeGameExitKind::EXIT_CODE;
  bridge.presentation_exit_code = static_cast<std::uint32_t>(
      RoR::kRendererOgreNextChildPrePeerReadyFailureExitCode);
  bridge.game_exec_confirmed = true;
  bridge.presentation_exec_confirmed = true;
  bridge.game_reaped = true;
  bridge.presentation_reaped = true;
  bridge.peer_terminated = true;
  Require(RoR::ShouldFallbackRendererBridgeToOgre14(intent, bridge),
          "exact pre-ready preferred failure was not recoverable");

  RoR::RendererBridgeProcessResult changed = bridge;
  changed.presentation_exit_code = static_cast<std::uint32_t>(
      RoR::kRendererOgreNextChildPostPeerReadyFailureExitCode);
  Require(!RoR::ShouldFallbackRendererBridgeToOgre14(intent, changed),
          "post-ready failure was allowed to change renderers");
  changed = bridge;
  changed.presentation_exit_kind =
      RoR::RendererBridgeGameExitKind::TERMINATION_SIGNAL;
  Require(!RoR::ShouldFallbackRendererBridgeToOgre14(intent, changed),
          "presentation signal was treated as a pre-ready exit");
  changed = bridge;
  changed.peer_terminated = false;
  Require(!RoR::ShouldFallbackRendererBridgeToOgre14(intent, changed),
          "fallback was allowed before both children were reaped");

  RoR::RendererPublicLauncherIntent required = intent;
  required.frontend =
      RoR::RendererFrontendPreference::OGRE_NEXT_REQUIRE;
  Require(!RoR::ShouldFallbackRendererBridgeToOgre14(required, bridge),
          "explicit Ogre-Next requirement was allowed to fall back");
  required = intent;
  required.directional_shadows =
      RoR::DirectionalShadowPreference::REQUIRE_NATIVE;
  Require(!RoR::ShouldFallbackRendererBridgeToOgre14(required, bridge),
          "native-shadow requirement was allowed to fall back");
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
  TestOgreNextIntentEncoding();
  TestOgreNextIntentDecoderRejectsMalformedPrefixes();
  TestPreReadyFallbackBoundary();
  TestStableRuntimeFailureCodes();
  return EXIT_SUCCESS;
}
