/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free orchestration contract for the Ogre-Next child.

#pragma once

#include "RendererBridgeEndpoint.h"

#include <cstdint>
#include <vector>

namespace RoR {

constexpr std::uint32_t kRendererOgreNextChildContractVersion = 2U;

/// The no-suffix mode remains only for the isolated native execution receipt.
/// A production invocation must carry the exact presentation bridge endpoint
/// nested inside the renderer intent.
enum class RendererOgreNextChildInvocationMode : std::uint8_t {
  PROBE_HEADLESS = 0U,
  PRODUCTION_BRIDGE = 1U,
};

enum class RendererOgreNextFrontendBootstrapStatus : std::uint8_t {
  COMPLETED = 0,
  REJECTED_STARTUP_PATH = 1,
  INITIALIZATION_FAILED = 2,
  SHUTDOWN_FAILED = 3,
  FAILED_INTERNAL = 4,
  ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION = 5,
};

/// Fully owned input to the injected frontend seam. The orchestration layer
/// copies endpoint tokens and decoded game arguments but never adopts, closes,
/// duplicates, reads, or writes either inherited native handle. A future
/// presentation-session implementation will own that separate transition.
struct RendererOgreNextFrontendBootstrapRequest final {
  std::uint32_t version = kRendererOgreNextChildContractVersion;
  RendererOgreNextChildInvocationMode invocation_mode =
      RendererOgreNextChildInvocationMode::PROBE_HEADLESS;
  RendererStartupPlanResult startup_plan;
  RendererBridgeEndpoint bridge_endpoint;
  std::vector<RendererChildLauncherString> game_arguments;
  bool has_bridge_endpoint = false;
};

struct RendererOgreNextFrontendBootstrapResult final {
  std::uint32_t version = kRendererOgreNextChildContractVersion;
  RendererOgreNextChildInvocationMode invocation_mode =
      RendererOgreNextChildInvocationMode::PROBE_HEADLESS;
  RendererOgreNextFrontendBootstrapStatus status =
      RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL;
  bool accepted = false;
  bool completed = false;
};

enum class RendererOgreNextChildStatus : std::uint8_t {
  COMPLETED_HEADLESS_BOOTSTRAP = 0,
  REJECTED_INVALID_RUNTIME = 1,
  REJECTED_CHILD_INTENT = 2,
  ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION = 3,
  REJECTED_STARTUP_PLAN = 4,
  REJECTED_UNSUPPORTED_STARTUP_PATH = 5,
  FAILED_FRONTEND_INITIALIZATION = 6,
  FAILED_FRONTEND_SHUTDOWN = 7,
  FAILED_FRONTEND_INTERNAL = 8,
  FAILED_INTERNAL = 9,
  REJECTED_BRIDGE_ENDPOINT = 10,
  REJECTED_BRIDGE_ROLE = 11,
};

using RendererOgreNextNativePreflightFunction =
    RendererNativeShadowPreflight (*)();
using RendererOgreNextFrontendBootstrapFunction =
    RendererOgreNextFrontendBootstrapResult (*)(
        const RendererOgreNextFrontendBootstrapRequest &);

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
  RendererBridgeEndpointArgvStatus bridge_status =
      RendererBridgeEndpointArgvStatus::REJECTED_MISSING_CONTRACT;
  RendererOgreNextChildInvocationMode invocation_mode =
      RendererOgreNextChildInvocationMode::PROBE_HEADLESS;
  RendererNativeShadowPreflight native_preflight;
  RendererStartupPlanResult startup_plan;
  RendererOgreNextFrontendBootstrapRequest frontend_request;
  RendererOgreNextFrontendBootstrapResult frontend;
  bool accepted = false;
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

/// Decode immutable launcher intent and then either accept the exact no-suffix
/// probe path or strictly decode a same-host presentation endpoint. Endpoint
/// rejection happens before native preflight or the frontend callback. The
/// accepted decoded endpoint and game suffix are copied into frontend_request;
/// this pure layer deliberately performs no native-handle adoption.
RendererOgreNextChildResult RunRendererOgreNextChild(
    int argc, const RendererChildLauncherChar *const argv[],
    const RendererOgreNextChildRuntime &runtime) noexcept;

/// Preserve decoder-internal failures while classifying all known caller-side
/// rejections as child-intent failures. READY and unknown values are invalid
/// in a rejection path and therefore fail internally.
RendererOgreNextChildStatus ClassifyRendererOgreNextChildIntentFailure(
    RendererOgreNextChildIntentArgvStatus status) noexcept;

/// Preserve decoder-internal failures while classifying all known caller-side
/// endpoint rejections as bridge-endpoint failures. READY and unknown values
/// are invalid in a rejection path and therefore fail internally.
RendererOgreNextChildStatus ClassifyRendererBridgeEndpointFailure(
    RendererBridgeEndpointArgvStatus status) noexcept;

bool IsKnownRendererOgreNextChildInvocationMode(
    RendererOgreNextChildInvocationMode mode) noexcept;
bool IsKnownRendererOgreNextFrontendBootstrapStatus(
    RendererOgreNextFrontendBootstrapStatus status) noexcept;
bool IsKnownRendererOgreNextChildStatus(
    RendererOgreNextChildStatus status) noexcept;
const char *ToString(RendererOgreNextChildInvocationMode mode) noexcept;
const char *ToString(RendererOgreNextFrontendBootstrapStatus status) noexcept;
const char *ToString(RendererOgreNextChildStatus status) noexcept;

} // namespace RoR
