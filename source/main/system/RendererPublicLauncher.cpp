/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererPublicLauncher.h"

#include "RendererBridgeProcessSupervisor.h"
#include "RendererLauncherPackageConfig.generated.h"
#include "RendererOgreNextChild.h"
#include "RendererPackageRuntimeProbe.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

namespace RoR {
namespace {

template <typename Character>
bool EqualsAscii(const Character *value, const char *expected) {
  if (value == nullptr || expected == nullptr) {
    return false;
  }
  while (*expected != '\0') {
    const unsigned char expected_value =
        static_cast<unsigned char>(*expected);
    if (*value != static_cast<Character>(expected_value)) {
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
    const unsigned char expected_value =
        static_cast<unsigned char>(*prefix);
    if (*value != static_cast<Character>(expected_value)) {
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

bool HasValidIntent(const RendererPublicLauncherIntent &intent) noexcept {
  return intent.version == kRendererPublicLauncherContractVersion &&
         IsKnownRendererFrontendPreference(intent.frontend) &&
         IsKnownDirectionalShadowPreference(intent.directional_shadows);
}

void WriteArgumentFailure(RendererPublicLauncherArgumentStatus status) {
  (void)std::fprintf(stderr, "RoR renderer launcher: %s\n", ToString(status));
  (void)std::fflush(stderr);
}

void WriteDecisionFailure(const RendererPublicLauncherDecision &decision) {
  (void)std::fprintf(stderr, "RoR renderer launcher: %s (handoff=%s)\n",
                     ToString(decision.status),
                     ToString(decision.handoff.status));
  (void)std::fflush(stderr);
}

void WriteChildLaunchFailure(const RendererChildLaunchFailure &failure) {
  (void)std::fprintf(stderr,
                     "RoR renderer launcher: %s (native-error=%u)\n",
                     ToString(failure.status),
                     static_cast<unsigned int>(failure.native_error_code));
  (void)std::fflush(stderr);
}

void WriteBridgeProcessFailure(
    const RendererBridgeProcessResult &result) {
  (void)std::fprintf(stderr,
                     "RoR renderer launcher: bridge-%s "
                     "(plan=%s child=%s native-error=%u)\n",
                     ToString(result.status),
                     ToString(result.launch_plan_status),
                     ToString(result.failed_child),
                     static_cast<unsigned int>(result.native_error_code));
  (void)std::fflush(stderr);
}

void WritePreReadyFallback(
    const RendererBridgeProcessResult &result) {
  (void)std::fprintf(
      stderr,
      "RoR renderer launcher: Ogre-Next exited before PEER_READY "
      "(exit=%u); relaunching exact OGRE 14 fallback\n",
      static_cast<unsigned int>(result.presentation_exit_code));
  (void)std::fflush(stderr);
}

void WriteRuntimePackageProbe(
    const RendererPackageRuntimeProbeResult &result) {
  (void)std::fprintf(
      stderr,
      "RoR renderer launcher: package-%s "
      "(ogre14=%s ogre-next=%s shader-media=%s "
      "presentation-media=%s native-error=%u)\n",
      ToString(result.status), ToString(result.observation.ogre14_child),
      ToString(result.observation.ogre_next_child),
      ToString(result.observation.ogre_next_shader_media),
      ToString(result.observation.ogre_next_presentation_media),
      static_cast<unsigned int>(result.native_error_code));
  (void)std::fflush(stderr);
}

std::uint64_t MixRendererBridgeSessionWord(std::uint64_t value) noexcept {
  value += UINT64_C(0x9e3779b97f4a7c15);
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

RendererBridgeSessionId CreateRendererBridgeSessionId() noexcept {
  // The session binds two inherited pipes to one launch transaction; it is
  // explicitly not an authentication secret. A process-local ordinal makes
  // repeated launches distinct even when the steady clock has coarse
  // resolution, while the mixer avoids exposing raw addresses or clock bits
  // in the versioned child argv contract.
  static std::atomic<std::uint64_t> ordinal{UINT64_C(0)};
  const std::uint64_t sequence =
      ordinal.fetch_add(UINT64_C(1), std::memory_order_relaxed) +
      UINT64_C(1);
  const std::uint64_t ticks = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t process_identity = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(&ordinal));
  const std::uint64_t first = MixRendererBridgeSessionWord(
      ticks ^ process_identity ^ sequence);
  const std::uint64_t second = MixRendererBridgeSessionWord(
      first ^ (sequence * UINT64_C(0xd1342543de82ef95)));

  RendererBridgeSessionId session{};
  for (std::size_t index = 0U; index < 8U; ++index) {
    const std::size_t shift = index * 8U;
    session[index] = static_cast<std::uint8_t>(first >> shift);
    session[index + 8U] = static_cast<std::uint8_t>(second >> shift);
  }
  bool any_nonzero = false;
  for (const std::uint8_t byte : session) {
    any_nonzero = any_nonzero || byte != 0U;
  }
  if (!any_nonzero) {
    session[0U] = 1U;
  }
  return session;
}

} // namespace

RendererStartupPackageAvailability
RendererPublicLauncherPackageAvailability() noexcept {
  RendererStartupPackageAvailability availability;
  availability.version = RendererLauncherPackageConfig::kContractVersion;
  availability.package_platform =
      RendererLauncherPackageConfig::kPackagePlatform;
  availability.ogre14_child_present =
      RendererLauncherPackageConfig::kOgre14ChildPresent;
  availability.ogre_next_child_present =
      RendererLauncherPackageConfig::kOgreNextChildPresent;
  availability.ogre_next_child_production_ready =
      RendererLauncherPackageConfig::kOgreNextChildProductionReady;
  availability.ogre_next_pssm_admitted =
      RendererLauncherPackageConfig::kOgreNextPssmAdmitted;
  availability.native_directional_shadow_backend =
      RendererLauncherPackageConfig::kNativeDirectionalShadowBackend;
  return availability;
}

RendererPublicLauncherIntent
RendererPublicLauncherPackageDefaultIntent() noexcept {
  RendererPublicLauncherIntent intent;
  intent.frontend =
      RendererLauncherPackageConfig::kDefaultFrontendPreference;
  intent.directional_shadows =
      RendererLauncherPackageConfig::kDefaultDirectionalShadowPreference;
  return intent;
}

RendererPublicLauncherArguments ParseRendererPublicLauncherArguments(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept {
  RendererPublicLauncherArguments result;
  result.intent = RendererPublicLauncherPackageDefaultIntent();
  try {
    if (argc < 1 || argv == nullptr) {
      return result;
    }
    for (int index = 0; index < argc; ++index) {
      if (argv[index] == nullptr) {
        return result;
      }
    }

    bool frontend_seen = false;
    bool shadows_seen = false;
    int first_forwarded_index = 1;
    for (; first_forwarded_index < argc; ++first_forwarded_index) {
      const RendererChildLauncherChar *argument =
          argv[first_forwarded_index];
      if (EqualsAscii(argument, "--")) {
        break;
      }

      static const char frontend_option[] = "--renderer-frontend";
      static const char frontend_prefix[] = "--renderer-frontend=";
      if (EqualsAscii(argument, frontend_option)) {
        result.status =
            RendererPublicLauncherArgumentStatus::REJECTED_MALFORMED_OPTION;
        return result;
      }
      if (StartsWithAscii(argument, frontend_prefix)) {
        if (frontend_seen) {
          result.status =
              RendererPublicLauncherArgumentStatus::REJECTED_DUPLICATE_OPTION;
          return result;
        }
        const RendererChildLauncherChar *value =
            ValueAfterAsciiPrefix(argument, frontend_prefix);
        if (EqualsAscii(value, "legacy-only")) {
          result.intent.frontend = RendererFrontendPreference::LEGACY_ONLY;
        } else if (EqualsAscii(value, "ogre-next-prefer")) {
          result.intent.frontend =
              RendererFrontendPreference::OGRE_NEXT_PREFER;
        } else if (EqualsAscii(value, "ogre-next-require")) {
          result.intent.frontend =
              RendererFrontendPreference::OGRE_NEXT_REQUIRE;
        } else {
          result.status = RendererPublicLauncherArgumentStatus::
              REJECTED_INVALID_OPTION_VALUE;
          return result;
        }
        frontend_seen = true;
        result.intent.frontend_was_explicit = true;
        continue;
      }

      static const char shadow_option[] =
          "--renderer-directional-shadows";
      static const char shadow_prefix[] =
          "--renderer-directional-shadows=";
      if (EqualsAscii(argument, shadow_option)) {
        result.status =
            RendererPublicLauncherArgumentStatus::REJECTED_MALFORMED_OPTION;
        return result;
      }
      if (StartsWithAscii(argument, shadow_prefix)) {
        if (shadows_seen) {
          result.status =
              RendererPublicLauncherArgumentStatus::REJECTED_DUPLICATE_OPTION;
          return result;
        }
        const RendererChildLauncherChar *value =
            ValueAfterAsciiPrefix(argument, shadow_prefix);
        if (EqualsAscii(value, "pssm")) {
          result.intent.directional_shadows =
              DirectionalShadowPreference::PSSM;
        } else if (EqualsAscii(value, "prefer-native")) {
          result.intent.directional_shadows =
              DirectionalShadowPreference::PREFER_NATIVE;
        } else if (EqualsAscii(value, "require-native")) {
          result.intent.directional_shadows =
              DirectionalShadowPreference::REQUIRE_NATIVE;
        } else {
          result.status = RendererPublicLauncherArgumentStatus::
              REJECTED_INVALID_OPTION_VALUE;
          return result;
        }
        shadows_seen = true;
        result.intent.directional_shadows_were_explicit = true;
        continue;
      }

      // The public launcher owns only an initial prefix. Preserve this first
      // legacy argument and the complete remaining suffix without inspecting
      // renderer-shaped values which may belong to a preceding game option.
      break;
    }

    result.forwarded_arguments.reserve(
        static_cast<std::size_t>(argc - first_forwarded_index + 1));
    result.forwarded_arguments.push_back(argv[0]);
    for (int index = first_forwarded_index; index < argc; ++index) {
      result.forwarded_arguments.push_back(argv[index]);
    }
    result.status = RendererPublicLauncherArgumentStatus::READY;
    result.accepted = true;
    return result;
  } catch (...) {
    result.forwarded_arguments.clear();
    result.accepted = false;
    result.status = RendererPublicLauncherArgumentStatus::FAILED_INTERNAL;
    return result;
  }
}

RendererPublicLauncherDecision ResolveRendererPublicLauncherDecision(
    const RendererPublicLauncherIntent &intent,
    const RendererStartupPackageAvailability &availability) noexcept {
  RendererPublicLauncherDecision decision;
  decision.intent = intent;
  if (!HasValidIntent(intent)) {
    return decision;
  }

  RendererStartupHandoffRequest request;
  request.startup.frontend = intent.frontend;
  request.startup.directional_shadows = intent.directional_shadows;
  request.startup.host_platform = availability.package_platform;
  decision.handoff = ResolveRendererStartupHandoff(request, availability);
  if (!decision.handoff.accepted) {
    decision.status = RendererPublicLauncherDecisionStatus::REJECTED_HANDOFF;
    return decision;
  }

  if (decision.handoff.child == RendererFrontendChild::OGRE_NEXT) {
    decision.status =
        RendererPublicLauncherDecisionStatus::READY_OGRE_NEXT;
    decision.accepted = true;
    return decision;
  }
  if (decision.handoff.child != RendererFrontendChild::OGRE14) {
    decision.status =
        RendererPublicLauncherDecisionStatus::REJECTED_INVALID_INTENT;
    return decision;
  }

  decision.status = RendererPublicLauncherDecisionStatus::READY_OGRE14;
  decision.accepted = true;
  return decision;
}

bool ShouldFallbackRendererBridgeToOgre14(
    const RendererPublicLauncherIntent &intent,
    const RendererBridgeProcessResult &bridge) noexcept {
  return HasValidIntent(intent) &&
         intent.frontend == RendererFrontendPreference::OGRE_NEXT_PREFER &&
         intent.directional_shadows !=
             DirectionalShadowPreference::REQUIRE_NATIVE &&
         bridge.version == kRendererBridgeProcessSupervisorContractVersion &&
         bridge.status ==
             RendererBridgeProcessStatus::PRESENTATION_EXITED_FIRST &&
         bridge.first_exit ==
             RendererBridgeObservedChild::PRESENTATION_FRONTEND &&
         bridge.presentation_exit_kind ==
             RendererBridgeGameExitKind::EXIT_CODE &&
         bridge.presentation_exit_code == static_cast<std::uint32_t>(
             kRendererOgreNextChildPrePeerReadyFailureExitCode) &&
         bridge.game_exec_confirmed && bridge.presentation_exec_confirmed &&
         bridge.game_reaped && bridge.presentation_reaped &&
         bridge.peer_terminated && !bridge.completed;
}

int RunRendererPublicLauncher(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept {
  const RendererPublicLauncherArguments arguments =
      ParseRendererPublicLauncherArguments(argc, argv);
  if (!arguments.accepted) {
    WriteArgumentFailure(arguments.status);
    return arguments.status ==
                   RendererPublicLauncherArgumentStatus::FAILED_INTERNAL
               ? kRendererPublicLauncherInternalExitCode
               : kRendererPublicLauncherUsageExitCode;
  }

  const RendererStartupPackageAvailability declared_availability =
      RendererPublicLauncherPackageAvailability();
  const RendererPublicLauncherDecision declared_decision =
      ResolveRendererPublicLauncherDecision(arguments.intent,
                                            declared_availability);
  if (!declared_decision.accepted) {
    WriteDecisionFailure(declared_decision);
    return kRendererPublicLauncherSelectionExitCode;
  }

  const RendererPackageRuntimeProbeResult package =
      ProbeRendererPackageRuntimeAvailability(declared_availability);
  if (!package.accepted) {
    WriteRuntimePackageProbe(package);
    return package.status == RendererPackageRuntimeProbeStatus::FAILED_INTERNAL
               ? kRendererPublicLauncherInternalExitCode
               : kRendererPublicLauncherChildLaunchExitCode;
  }
  if (package.ogre_next_runtime_degraded) {
    WriteRuntimePackageProbe(package);
  }

  const RendererPublicLauncherDecision decision =
      ResolveRendererPublicLauncherDecision(arguments.intent,
                                            package.effective_availability);
  if (!decision.accepted) {
    WriteDecisionFailure(decision);
    return kRendererPublicLauncherSelectionExitCode;
  }
  if (arguments.forwarded_arguments.empty() ||
      arguments.forwarded_arguments.size() >
          static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    WriteArgumentFailure(
        RendererPublicLauncherArgumentStatus::FAILED_INTERNAL);
    return kRendererPublicLauncherInternalExitCode;
  }
  if (decision.handoff.child == RendererFrontendChild::OGRE_NEXT) {
    const RendererBridgeProcessResult bridge =
        SuperviseRendererBridgeProcesses(
            decision.handoff, CreateRendererBridgeSessionId(),
            static_cast<int>(arguments.forwarded_arguments.size()),
            arguments.forwarded_arguments.data());
    if (bridge.completed &&
        bridge.status ==
            RendererBridgeProcessStatus::COMPLETED_GAME_EXIT) {
      PropagateRendererBridgeGameExit(bridge);
    }
    if (ShouldFallbackRendererBridgeToOgre14(arguments.intent, bridge)) {
      RendererPublicLauncherIntent fallback_intent = arguments.intent;
      fallback_intent.frontend = RendererFrontendPreference::LEGACY_ONLY;
      const RendererPublicLauncherDecision fallback =
          ResolveRendererPublicLauncherDecision(
              fallback_intent, package.effective_availability);
      if (!fallback.accepted ||
          fallback.status !=
              RendererPublicLauncherDecisionStatus::READY_OGRE14 ||
          fallback.handoff.child != RendererFrontendChild::OGRE14) {
        WriteDecisionFailure(fallback);
        return kRendererPublicLauncherSelectionExitCode;
      }
      WritePreReadyFallback(bridge);
      const RendererChildLaunchFailure failure =
          LaunchRendererChildAndPropagateExit(
              fallback.handoff,
              static_cast<int>(arguments.forwarded_arguments.size()),
              arguments.forwarded_arguments.data());
      WriteChildLaunchFailure(failure);
      return kRendererPublicLauncherChildLaunchExitCode;
    }
    WriteBridgeProcessFailure(bridge);
    return bridge.status == RendererBridgeProcessStatus::FAILED_INTERNAL
               ? kRendererPublicLauncherInternalExitCode
               : kRendererPublicLauncherChildLaunchExitCode;
  }

  const RendererChildLaunchFailure failure =
      LaunchRendererChildAndPropagateExit(
          decision.handoff,
          static_cast<int>(arguments.forwarded_arguments.size()),
          arguments.forwarded_arguments.data());
  WriteChildLaunchFailure(failure);
  return kRendererPublicLauncherChildLaunchExitCode;
}

bool IsKnownRendererPublicLauncherArgumentStatus(
    RendererPublicLauncherArgumentStatus status) noexcept {
  switch (status) {
  case RendererPublicLauncherArgumentStatus::READY:
  case RendererPublicLauncherArgumentStatus::REJECTED_INVALID_ARGUMENT_VECTOR:
  case RendererPublicLauncherArgumentStatus::REJECTED_MALFORMED_OPTION:
  case RendererPublicLauncherArgumentStatus::REJECTED_INVALID_OPTION_VALUE:
  case RendererPublicLauncherArgumentStatus::REJECTED_DUPLICATE_OPTION:
  case RendererPublicLauncherArgumentStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

bool IsKnownRendererPublicLauncherDecisionStatus(
    RendererPublicLauncherDecisionStatus status) noexcept {
  switch (status) {
  case RendererPublicLauncherDecisionStatus::READY_OGRE14:
  case RendererPublicLauncherDecisionStatus::REJECTED_INVALID_INTENT:
  case RendererPublicLauncherDecisionStatus::REJECTED_HANDOFF:
  case RendererPublicLauncherDecisionStatus::
      REJECTED_OGRE_NEXT_CHILD_INTENT_ENCODING_UNAVAILABLE:
  case RendererPublicLauncherDecisionStatus::READY_OGRE_NEXT:
    return true;
  }
  return false;
}

const char *ToString(RendererPublicLauncherArgumentStatus status) noexcept {
  switch (status) {
  case RendererPublicLauncherArgumentStatus::READY:
    return "ready";
  case RendererPublicLauncherArgumentStatus::REJECTED_INVALID_ARGUMENT_VECTOR:
    return "rejected-invalid-argument-vector";
  case RendererPublicLauncherArgumentStatus::REJECTED_MALFORMED_OPTION:
    return "rejected-malformed-option";
  case RendererPublicLauncherArgumentStatus::REJECTED_INVALID_OPTION_VALUE:
    return "rejected-invalid-option-value";
  case RendererPublicLauncherArgumentStatus::REJECTED_DUPLICATE_OPTION:
    return "rejected-duplicate-option";
  case RendererPublicLauncherArgumentStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

const char *ToString(RendererPublicLauncherDecisionStatus status) noexcept {
  switch (status) {
  case RendererPublicLauncherDecisionStatus::READY_OGRE14:
    return "ready-ogre14";
  case RendererPublicLauncherDecisionStatus::REJECTED_INVALID_INTENT:
    return "rejected-invalid-intent";
  case RendererPublicLauncherDecisionStatus::REJECTED_HANDOFF:
    return "rejected-handoff";
  case RendererPublicLauncherDecisionStatus::
      REJECTED_OGRE_NEXT_CHILD_INTENT_ENCODING_UNAVAILABLE:
    return "rejected-ogre-next-child-intent-encoding-unavailable";
  case RendererPublicLauncherDecisionStatus::READY_OGRE_NEXT:
    return "ready-ogre-next";
  }
  return "invalid";
}

} // namespace RoR
