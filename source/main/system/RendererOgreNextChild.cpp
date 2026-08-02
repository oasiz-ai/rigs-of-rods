/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextChild.h"

namespace RoR {
namespace {

RendererOgreNextChildStatus MapFrontendFailure(
    RendererOgreNextFrontendBootstrapStatus status) noexcept {
  switch (status) {
  case RendererOgreNextFrontendBootstrapStatus::COMPLETED:
    return RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
  case RendererOgreNextFrontendBootstrapStatus::REJECTED_STARTUP_PATH:
    return RendererOgreNextChildStatus::REJECTED_UNSUPPORTED_STARTUP_PATH;
  case RendererOgreNextFrontendBootstrapStatus::INITIALIZATION_FAILED:
    return RendererOgreNextChildStatus::FAILED_FRONTEND_INITIALIZATION;
  case RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED:
    return RendererOgreNextChildStatus::FAILED_FRONTEND_SHUTDOWN;
  case RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL:
    return RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
  }
  return RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
}

} // namespace

RendererStartupBuildAvailability
RendererOgreNextChildBuildAvailability() noexcept {
  RendererStartupBuildAvailability availability;
  availability.version = kRendererStartupPlanContractVersion;
  availability.ogre14_frontend_available = false;
  availability.ogre_next_frontend_available = true;
  availability.ogre_next_pssm_available = true;
  availability.native_directional_shadow_backend =
      NativeRayTracingBackend::NONE;
  return availability;
}

RendererNativeShadowPreflight
CollectRendererOgreNextChildNativePreflight() noexcept {
  RendererNativeShadowPreflight preflight;
  preflight.version = kRendererStartupPlanContractVersion;
  preflight.source = RendererNativePreflightSource::NONE;
  preflight.backend = NativeRayTracingBackend::NONE;
  preflight.completed = false;
  return preflight;
}

RendererOgreNextChildResult RunRendererOgreNextChild(
    int argc, const RendererChildLauncherChar *const argv[],
    const RendererOgreNextChildRuntime &runtime) noexcept {
  RendererOgreNextChildResult result;
  try {
    if (runtime.version != kRendererOgreNextChildContractVersion ||
        runtime.collect_native_preflight == nullptr ||
        runtime.bootstrap_frontend == nullptr) {
      result.status =
          RendererOgreNextChildStatus::REJECTED_INVALID_RUNTIME;
      return result;
    }

    const RendererOgreNextChildIntentParseResult intent =
        ParseRendererOgreNextChildIntent(argc, argv);
    result.intent_status = intent.status;
    if (!intent.accepted) {
      result.status =
          ClassifyRendererOgreNextChildIntentFailure(intent.status);
      return result;
    }
    if (intent.forwarded_arguments.size() != 1U) {
      result.status =
          RendererOgreNextChildStatus::REJECTED_GAME_BRIDGE_UNAVAILABLE;
      return result;
    }

    result.native_preflight = runtime.collect_native_preflight();
    result.startup_plan = ResolveRendererStartupPlan(
        intent.startup, RendererOgreNextChildBuildAvailability(),
        result.native_preflight);
    if (!result.startup_plan.accepted) {
      result.status = RendererOgreNextChildStatus::REJECTED_STARTUP_PLAN;
      return result;
    }
    if (result.startup_plan.effective_path !=
        RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1) {
      result.status =
          RendererOgreNextChildStatus::REJECTED_UNSUPPORTED_STARTUP_PATH;
      return result;
    }

    result.frontend = runtime.bootstrap_frontend(result.startup_plan);
    if (result.frontend.version != kRendererOgreNextChildContractVersion ||
        !IsKnownRendererOgreNextFrontendBootstrapStatus(
            result.frontend.status)) {
      result.status = RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
      return result;
    }
    if (result.frontend.status !=
            RendererOgreNextFrontendBootstrapStatus::COMPLETED ||
        !result.frontend.completed) {
      if (result.frontend.completed) {
        result.status =
            RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
        return result;
      }
      result.status = MapFrontendFailure(result.frontend.status);
      return result;
    }

    result.status =
        RendererOgreNextChildStatus::COMPLETED_HEADLESS_BOOTSTRAP;
    result.completed = true;
    return result;
  } catch (...) {
    result.completed = false;
    result.status = RendererOgreNextChildStatus::FAILED_INTERNAL;
    return result;
  }
}

RendererOgreNextChildStatus ClassifyRendererOgreNextChildIntentFailure(
    RendererOgreNextChildIntentArgvStatus status) noexcept {
  if (status == RendererOgreNextChildIntentArgvStatus::FAILED_INTERNAL) {
    return RendererOgreNextChildStatus::FAILED_INTERNAL;
  }
  if (status == RendererOgreNextChildIntentArgvStatus::READY ||
      !IsKnownRendererOgreNextChildIntentArgvStatus(status)) {
    return RendererOgreNextChildStatus::FAILED_INTERNAL;
  }
  return RendererOgreNextChildStatus::REJECTED_CHILD_INTENT;
}

bool IsKnownRendererOgreNextFrontendBootstrapStatus(
    RendererOgreNextFrontendBootstrapStatus status) noexcept {
  switch (status) {
  case RendererOgreNextFrontendBootstrapStatus::COMPLETED:
  case RendererOgreNextFrontendBootstrapStatus::REJECTED_STARTUP_PATH:
  case RendererOgreNextFrontendBootstrapStatus::INITIALIZATION_FAILED:
  case RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED:
  case RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

bool IsKnownRendererOgreNextChildStatus(
    RendererOgreNextChildStatus status) noexcept {
  switch (status) {
  case RendererOgreNextChildStatus::COMPLETED_HEADLESS_BOOTSTRAP:
  case RendererOgreNextChildStatus::REJECTED_INVALID_RUNTIME:
  case RendererOgreNextChildStatus::REJECTED_CHILD_INTENT:
  case RendererOgreNextChildStatus::REJECTED_GAME_BRIDGE_UNAVAILABLE:
  case RendererOgreNextChildStatus::REJECTED_STARTUP_PLAN:
  case RendererOgreNextChildStatus::REJECTED_UNSUPPORTED_STARTUP_PATH:
  case RendererOgreNextChildStatus::FAILED_FRONTEND_INITIALIZATION:
  case RendererOgreNextChildStatus::FAILED_FRONTEND_SHUTDOWN:
  case RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL:
  case RendererOgreNextChildStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(
    RendererOgreNextFrontendBootstrapStatus status) noexcept {
  switch (status) {
  case RendererOgreNextFrontendBootstrapStatus::COMPLETED:
    return "completed";
  case RendererOgreNextFrontendBootstrapStatus::REJECTED_STARTUP_PATH:
    return "rejected-startup-path";
  case RendererOgreNextFrontendBootstrapStatus::INITIALIZATION_FAILED:
    return "initialization-failed";
  case RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED:
    return "shutdown-failed";
  case RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

const char *ToString(RendererOgreNextChildStatus status) noexcept {
  switch (status) {
  case RendererOgreNextChildStatus::COMPLETED_HEADLESS_BOOTSTRAP:
    return "completed-headless-bootstrap";
  case RendererOgreNextChildStatus::REJECTED_INVALID_RUNTIME:
    return "rejected-invalid-runtime";
  case RendererOgreNextChildStatus::REJECTED_CHILD_INTENT:
    return "rejected-child-intent";
  case RendererOgreNextChildStatus::REJECTED_GAME_BRIDGE_UNAVAILABLE:
    return "rejected-game-bridge-unavailable";
  case RendererOgreNextChildStatus::REJECTED_STARTUP_PLAN:
    return "rejected-startup-plan";
  case RendererOgreNextChildStatus::REJECTED_UNSUPPORTED_STARTUP_PATH:
    return "rejected-unsupported-startup-path";
  case RendererOgreNextChildStatus::FAILED_FRONTEND_INITIALIZATION:
    return "failed-frontend-initialization";
  case RendererOgreNextChildStatus::FAILED_FRONTEND_SHUTDOWN:
    return "failed-frontend-shutdown";
  case RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL:
    return "failed-frontend-internal";
  case RendererOgreNextChildStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
