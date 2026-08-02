/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free orchestration contract for the Ogre-Next child.

#pragma once

#include "RendererChildIntent.h"

#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererOgreNextChildContractVersion = 1U;

enum class RendererOgreNextFrontendBootstrapStatus : std::uint8_t {
  COMPLETED = 0,
  REJECTED_STARTUP_PATH = 1,
  INITIALIZATION_FAILED = 2,
  SHUTDOWN_FAILED = 3,
  FAILED_INTERNAL = 4,
};

struct RendererOgreNextFrontendBootstrapResult {
  std::uint32_t version = kRendererOgreNextChildContractVersion;
  RendererOgreNextFrontendBootstrapStatus status =
      RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL;
  bool completed = false;
};

enum class RendererOgreNextChildStatus : std::uint8_t {
  COMPLETED_HEADLESS_BOOTSTRAP = 0,
  REJECTED_INVALID_RUNTIME = 1,
  REJECTED_CHILD_INTENT = 2,
  REJECTED_GAME_BRIDGE_UNAVAILABLE = 3,
  REJECTED_STARTUP_PLAN = 4,
  REJECTED_UNSUPPORTED_STARTUP_PATH = 5,
  FAILED_FRONTEND_INITIALIZATION = 6,
  FAILED_FRONTEND_SHUTDOWN = 7,
  FAILED_FRONTEND_INTERNAL = 8,
  FAILED_INTERNAL = 9,
};

using RendererOgreNextNativePreflightFunction =
    RendererNativeShadowPreflight (*)();
using RendererOgreNextFrontendBootstrapFunction =
    RendererOgreNextFrontendBootstrapResult (*)(
        const RendererStartupPlanResult &);

/// Child-owned runtime functions. Neither launcher state nor environment
/// variables may supply build availability or select a startup path.
struct RendererOgreNextChildRuntime {
  std::uint32_t version = kRendererOgreNextChildContractVersion;
  RendererOgreNextNativePreflightFunction collect_native_preflight = nullptr;
  RendererOgreNextFrontendBootstrapFunction bootstrap_frontend = nullptr;
};

struct RendererOgreNextChildResult {
  std::uint32_t version = kRendererOgreNextChildContractVersion;
  RendererOgreNextChildStatus status =
      RendererOgreNextChildStatus::FAILED_INTERNAL;
  RendererOgreNextChildIntentArgvStatus intent_status =
      RendererOgreNextChildIntentArgvStatus::REJECTED_INVALID_ARGUMENTS;
  RendererNativeShadowPreflight native_preflight;
  RendererStartupPlanResult startup_plan;
  RendererOgreNextFrontendBootstrapResult frontend;
  bool completed = false;
};

/// Immutable facts for this standalone executable. It contains Ogre-Next and
/// the reviewed PSSM path, never OGRE 1.x or a native RT startup backend.
RendererStartupBuildAvailability
RendererOgreNextChildBuildAvailability() noexcept;

/// Current bootstrap has no reviewed pre-Root native-device collector. Report
/// that absence explicitly instead of treating launcher declarations or
/// persisted evidence as current-process availability.
RendererNativeShadowPreflight
CollectRendererOgreNextChildNativePreflight() noexcept;

/// Decode immutable launcher intent, reject unsupported game arguments,
/// collect current-process preflight, resolve the child-owned startup plan,
/// and only then invoke the frontend bootstrap. This target is intentionally
/// headless until the native presentation and game bridge are admitted.
RendererOgreNextChildResult RunRendererOgreNextChild(
    int argc, const RendererChildLauncherChar *const argv[],
    const RendererOgreNextChildRuntime &runtime) noexcept;

/// Preserve decoder-internal failures while classifying all known caller-side
/// rejections as child-intent failures. READY and unknown values are invalid
/// in a rejection path and therefore fail internally.
RendererOgreNextChildStatus ClassifyRendererOgreNextChildIntentFailure(
    RendererOgreNextChildIntentArgvStatus status) noexcept;

bool IsKnownRendererOgreNextFrontendBootstrapStatus(
    RendererOgreNextFrontendBootstrapStatus status) noexcept;
bool IsKnownRendererOgreNextChildStatus(
    RendererOgreNextChildStatus status) noexcept;
const char *ToString(RendererOgreNextFrontendBootstrapStatus status) noexcept;
const char *ToString(RendererOgreNextChildStatus status) noexcept;

} // namespace RoR
