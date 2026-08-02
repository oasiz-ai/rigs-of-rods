/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free public renderer-launcher argument and decision seam.

#pragma once

#include "RendererChildIntent.h"
#include "RendererChildLauncher.h"

#include <cstdint>
#include <vector>

namespace RoR {

struct RendererBridgeProcessResult;

constexpr std::uint32_t kRendererPublicLauncherContractVersion = 1U;

constexpr int kRendererPublicLauncherUsageExitCode = 64;
constexpr int kRendererPublicLauncherSelectionExitCode = 70;
constexpr int kRendererPublicLauncherChildLaunchExitCode = 71;
constexpr int kRendererPublicLauncherInternalExitCode = 72;

enum class RendererPublicLauncherArgumentStatus : std::uint8_t {
  READY = 0,
  REJECTED_INVALID_ARGUMENT_VECTOR = 1,
  REJECTED_MALFORMED_OPTION = 2,
  REJECTED_INVALID_OPTION_VALUE = 3,
  REJECTED_DUPLICATE_OPTION = 4,
  FAILED_INTERNAL = 5,
};

enum class RendererPublicLauncherDecisionStatus : std::uint8_t {
  READY_OGRE14 = 0,
  REJECTED_INVALID_INTENT = 1,
  REJECTED_HANDOFF = 2,
  /// Retained as a stable version-one status value. The version-one child argv
  /// contract is now available, so production code no longer emits it.
  REJECTED_OGRE_NEXT_CHILD_INTENT_ENCODING_UNAVAILABLE = 3,
  READY_OGRE_NEXT = 4,
};

/// Versioned normalized intent retained independently from the argv forwarded
/// to the legacy child. Defaults match RendererStartupRequest: prefer
/// Ogre-Next, with the admitted PSSM path and an explicit OGRE14 fallback.
struct RendererPublicLauncherIntent {
  std::uint32_t version = kRendererPublicLauncherContractVersion;
  RendererFrontendPreference frontend =
      RendererFrontendPreference::OGRE_NEXT_PREFER;
  DirectionalShadowPreference directional_shadows =
      DirectionalShadowPreference::PSSM;
  bool frontend_was_explicit = false;
  bool directional_shadows_were_explicit = false;
};

/// Launcher-only options are recognized only as an initial argv prefix. This
/// prevents a renderer-shaped token used as a legacy option value from being
/// consumed. Every unowned argument and the entire suffix after the first
/// unowned token are forwarded in their original order and native encoding.
struct RendererPublicLauncherArguments {
  std::uint32_t version = kRendererPublicLauncherContractVersion;
  RendererPublicLauncherArgumentStatus status =
      RendererPublicLauncherArgumentStatus::REJECTED_INVALID_ARGUMENT_VECTOR;
  RendererPublicLauncherIntent intent;
  std::vector<const RendererChildLauncherChar *> forwarded_arguments;
  bool accepted = false;
};

struct RendererPublicLauncherDecision {
  std::uint32_t version = kRendererPublicLauncherContractVersion;
  RendererPublicLauncherDecisionStatus status =
      RendererPublicLauncherDecisionStatus::REJECTED_INVALID_INTENT;
  RendererPublicLauncherIntent intent;
  RendererStartupHandoffResult handoff;
  bool accepted = false;
};

/// Returns the exact immutable facts compiled into the public launcher. No
/// environment variable, external file, or command-line option can alter them.
RendererStartupPackageAvailability
RendererPublicLauncherPackageAvailability() noexcept;

/// Parses exact initial-prefix forms:
///   --renderer-frontend=<legacy-only|ogre-next-prefer|ogre-next-require>
///   --renderer-directional-shadows=<pssm|prefer-native|require-native>
/// Values are required inline so parsing never consumes a legacy argument.
RendererPublicLauncherArguments ParseRendererPublicLauncherArguments(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept;

/// Pure decision seam. The production entrypoint always passes the immutable
/// package facts above; accepting facts here permits exhaustive contract tests
/// without creating a mutable runtime package channel.
RendererPublicLauncherDecision ResolveRendererPublicLauncherDecision(
    const RendererPublicLauncherIntent &intent,
    const RendererStartupPackageAvailability &availability) noexcept;

/// True only for the exact recoverable production-child boundary: an
/// Ogre-Next-preferred, non-native-required launch whose presentation child
/// exits with the reserved pre-PEER_READY status after both bridge children
/// were reaped. Post-ready failures and explicit requirements never fall back.
bool ShouldFallbackRendererBridgeToOgre14(
    const RendererPublicLauncherIntent &intent,
    const RendererBridgeProcessResult &bridge) noexcept;

/// Parse and resolve the immutable package policy, then fail closed over the
/// exact executable-relative runtime artifacts. Missing Ogre-Next artifacts
/// may narrow OGRE_NEXT_PREFER to OGRE14; an explicit REQUIRE never widens.
/// A legacy selection transfers control to the exact OGRE 14 sibling. An
/// admitted Ogre-Next selection starts the exact OGRE 14 game host and
/// Ogre-Next presentation siblings under the render-bridge supervisor, then
/// propagates the game host's exact exit status or POSIX terminating signal.
int RunRendererPublicLauncher(
    int argc, const RendererChildLauncherChar *const argv[]) noexcept;

bool IsKnownRendererPublicLauncherArgumentStatus(
    RendererPublicLauncherArgumentStatus status) noexcept;
bool IsKnownRendererPublicLauncherDecisionStatus(
    RendererPublicLauncherDecisionStatus status) noexcept;
const char *ToString(RendererPublicLauncherArgumentStatus status) noexcept;
const char *ToString(RendererPublicLauncherDecisionStatus status) noexcept;

} // namespace RoR
