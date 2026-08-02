/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextChild.h"

namespace RoR {
namespace {

std::vector<const RendererChildLauncherChar *> ArgumentPointers(
    const std::vector<RendererChildLauncherString> &arguments) {
  std::vector<const RendererChildLauncherChar *> pointers;
  pointers.reserve(arguments.size());
  for (const RendererChildLauncherString &argument : arguments) {
    pointers.push_back(argument.c_str());
  }
  return pointers;
}

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
  case RendererOgreNextFrontendBootstrapStatus::
      ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION:
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

    result.frontend_request.version =
        kRendererOgreNextChildContractVersion;
    if (intent.forwarded_arguments.size() == 1U) {
      result.invocation_mode =
          RendererOgreNextChildInvocationMode::PROBE_HEADLESS;
      result.frontend_request.invocation_mode = result.invocation_mode;
      result.frontend_request.game_arguments =
          intent.forwarded_arguments;
    } else {
      const std::vector<const RendererChildLauncherChar *> bridge_arguments =
          ArgumentPointers(intent.forwarded_arguments);
      const RendererBridgeEndpointArgvParseResult bridge =
          ParseRendererBridgeEndpoint(
              static_cast<int>(bridge_arguments.size()),
              bridge_arguments.data());
      result.bridge_status = bridge.status;
      if (!bridge.accepted) {
        result.status =
            ClassifyRendererBridgeEndpointFailure(bridge.status);
        return result;
      }
      if (!IsValidRendererBridgeEndpoint(bridge.endpoint)) {
        result.status = RendererOgreNextChildStatus::FAILED_INTERNAL;
        return result;
      }
      if (bridge.endpoint.role !=
          RendererBridgeRole::PRESENTATION_FRONTEND) {
        result.status =
            RendererOgreNextChildStatus::REJECTED_BRIDGE_ROLE;
        return result;
      }
      result.invocation_mode =
          RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE;
      result.frontend_request.invocation_mode = result.invocation_mode;
      result.frontend_request.bridge_endpoint = bridge.endpoint;
      result.frontend_request.game_arguments =
          bridge.forwarded_arguments;
      result.frontend_request.has_bridge_endpoint = true;
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
    result.frontend_request.startup_plan = result.startup_plan;

    result.frontend =
        runtime.bootstrap_frontend(result.frontend_request);
    if (result.frontend.version != kRendererOgreNextChildContractVersion ||
        !IsKnownRendererOgreNextChildInvocationMode(
            result.frontend.invocation_mode) ||
        result.frontend.invocation_mode != result.invocation_mode ||
        !IsKnownRendererOgreNextFrontendBootstrapStatus(
            result.frontend.status)) {
      result.status = RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
      return result;
    }

    if (result.invocation_mode ==
        RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE) {
      if (result.frontend.status ==
          RendererOgreNextFrontendBootstrapStatus::COMPLETED) {
        if (!result.frontend.accepted || !result.frontend.completed) {
          result.status =
              RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
          return result;
        }
        result.status = RendererOgreNextChildStatus::
            COMPLETED_PRODUCTION_BRIDGE_SESSION;
        result.accepted = true;
        result.completed = true;
        return result;
      }
      if (result.frontend.accepted || result.frontend.completed ||
          result.frontend.status ==
              RendererOgreNextFrontendBootstrapStatus::
                  ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION) {
        result.status =
            RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
        return result;
      }
      result.status = MapFrontendFailure(result.frontend.status);
      return result;
    }

    if (result.frontend.status !=
            RendererOgreNextFrontendBootstrapStatus::COMPLETED ||
        !result.frontend.accepted || !result.frontend.completed) {
      if (result.frontend.accepted || result.frontend.completed ||
          result.frontend.status ==
              RendererOgreNextFrontendBootstrapStatus::
                  ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION) {
        result.status =
            RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL;
        return result;
      }
      result.status = MapFrontendFailure(result.frontend.status);
      return result;
    }
    result.status =
        RendererOgreNextChildStatus::COMPLETED_HEADLESS_BOOTSTRAP;
    result.accepted = true;
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

RendererOgreNextChildStatus ClassifyRendererBridgeEndpointFailure(
    RendererBridgeEndpointArgvStatus status) noexcept {
  if (status == RendererBridgeEndpointArgvStatus::FAILED_INTERNAL) {
    return RendererOgreNextChildStatus::FAILED_INTERNAL;
  }
  if (status == RendererBridgeEndpointArgvStatus::READY ||
      !IsKnownRendererBridgeEndpointArgvStatus(status)) {
    return RendererOgreNextChildStatus::FAILED_INTERNAL;
  }
  return RendererOgreNextChildStatus::REJECTED_BRIDGE_ENDPOINT;
}

bool IsKnownRendererOgreNextChildInvocationMode(
    RendererOgreNextChildInvocationMode mode) noexcept {
  switch (mode) {
  case RendererOgreNextChildInvocationMode::PROBE_HEADLESS:
  case RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE:
    return true;
  }
  return false;
}

bool IsKnownRendererOgreNextFrontendBootstrapStatus(
    RendererOgreNextFrontendBootstrapStatus status) noexcept {
  switch (status) {
  case RendererOgreNextFrontendBootstrapStatus::COMPLETED:
  case RendererOgreNextFrontendBootstrapStatus::REJECTED_STARTUP_PATH:
  case RendererOgreNextFrontendBootstrapStatus::INITIALIZATION_FAILED:
  case RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED:
  case RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL:
  case RendererOgreNextFrontendBootstrapStatus::
      ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION:
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
  case RendererOgreNextChildStatus::
      ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION:
  case RendererOgreNextChildStatus::REJECTED_STARTUP_PLAN:
  case RendererOgreNextChildStatus::REJECTED_UNSUPPORTED_STARTUP_PATH:
  case RendererOgreNextChildStatus::FAILED_FRONTEND_INITIALIZATION:
  case RendererOgreNextChildStatus::FAILED_FRONTEND_SHUTDOWN:
  case RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL:
  case RendererOgreNextChildStatus::FAILED_INTERNAL:
  case RendererOgreNextChildStatus::REJECTED_BRIDGE_ENDPOINT:
  case RendererOgreNextChildStatus::REJECTED_BRIDGE_ROLE:
  case RendererOgreNextChildStatus::COMPLETED_PRODUCTION_BRIDGE_SESSION:
    return true;
  }
  return false;
}

const char *ToString(
    RendererOgreNextChildInvocationMode mode) noexcept {
  switch (mode) {
  case RendererOgreNextChildInvocationMode::PROBE_HEADLESS:
    return "probe-headless";
  case RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE:
    return "production-bridge";
  }
  return "invalid";
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
  case RendererOgreNextFrontendBootstrapStatus::
      ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION:
    return "accepted-production-bridge-orchestration";
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
  case RendererOgreNextChildStatus::
      ACCEPTED_PRODUCTION_BRIDGE_ORCHESTRATION:
    return "accepted-production-bridge-orchestration";
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
  case RendererOgreNextChildStatus::REJECTED_BRIDGE_ENDPOINT:
    return "rejected-bridge-endpoint";
  case RendererOgreNextChildStatus::REJECTED_BRIDGE_ROLE:
    return "rejected-bridge-role";
  case RendererOgreNextChildStatus::COMPLETED_PRODUCTION_BRIDGE_SESSION:
    return "completed-production-bridge-session";
  }
  return "invalid";
}

} // namespace RoR
