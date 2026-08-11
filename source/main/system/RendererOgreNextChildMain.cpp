/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Probe-only native entrypoint for the real Ogre-Next child bootstrap.

#include "RendererOgreNextChild.h"
#include "RendererOgreNextProductionSession.h"
#include "RendererPackagedMediaPath.h"

#include "OgreNextN1Frontend.h"
#include "renderer_ogre_next_child_config.generated.h"

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM) || \
    defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
#error "RoR-OgreNext child must not compile with an Ogre-Next test seam"
#endif

#include <cstdio>
#include <filesystem>
#include <utility>

namespace {

constexpr int kRendererOgreNextChildSuccessExitCode = 0;
constexpr int kRendererOgreNextChildRejectedExitCode = 64;
constexpr int kRendererOgreNextChildInitializationExitCode = 70;
constexpr int kRendererOgreNextChildShutdownExitCode = 71;
constexpr int kRendererOgreNextChildInternalExitCode = 72;
constexpr int kRendererOgreNextChildCapabilityUnsupportedExitCode = 77;

enum class BootstrapObservation {
  NOT_RUN = 0,
  COMPLETED,
  EXACT_PSSM_CAPABILITY_UNSUPPORTED,
  INITIALIZATION_FAILED,
  SHUTDOWN_FAILED,
  FAILED_INTERNAL,
  PRODUCTION_BRIDGE_SESSION_COMPLETED,
  PRODUCTION_BRIDGE_SESSION_FAILED,
  PRODUCTION_MEDIA_RESOLUTION_FAILED,
};

BootstrapObservation g_bootstrap_observation = BootstrapObservation::NOT_RUN;
RoR::RendererOgreNextProductionSessionResult g_production_session;
RoR::RendererPackagedMediaPathResult g_packaged_media;

bool IsExactPssmCapabilityUnsupported(
    const RoR::Render::RenderOperationResult &result) {
  return result.code == RoR::Render::RenderOperationCode::UNSUPPORTED &&
         result.detail ==
             RoR::Render::kOgreNextPssmCapabilityUnsupportedDetail;
}

RoR::RendererOgreNextFrontendBootstrapResult BootstrapProductionFrontend(
    const RoR::RendererOgreNextFrontendBootstrapRequest &request) {
  RoR::RendererOgreNextFrontendBootstrapResult result;
  try {
    result.invocation_mode = request.invocation_mode;
    result.production_readiness =
        request.invocation_mode ==
                RoR::RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE
            ? RoR::RendererOgreNextProductionReadiness::PRE_PEER_READY
            : RoR::RendererOgreNextProductionReadiness::NOT_PRODUCTION;
    if (request.version != RoR::kRendererOgreNextChildContractVersion ||
        request.startup_plan.version !=
            RoR::kRendererStartupPlanContractVersion ||
        !request.startup_plan.accepted ||
        request.startup_plan.effective_path !=
            RoR::RendererStartupPath::OGRE_NEXT_PSSM_3_CASCADE_V1) {
      result.status = RoR::RendererOgreNextFrontendBootstrapStatus::
          REJECTED_STARTUP_PATH;
      return result;
    }
    if (request.invocation_mode ==
        RoR::RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE) {
      if (!request.has_bridge_endpoint ||
          !RoR::IsValidRendererBridgeEndpoint(
              request.bridge_endpoint) ||
          request.bridge_endpoint.role !=
              RoR::RendererBridgeRole::PRESENTATION_FRONTEND ||
          request.game_arguments.empty()) {
        result.status = RoR::RendererOgreNextFrontendBootstrapStatus::
            FAILED_INTERNAL;
        return result;
      }
      g_packaged_media = RoR::ResolveRendererPackagedMediaPath(
          request.bridge_endpoint.platform);
      if (!g_packaged_media.accepted ||
          g_packaged_media.status !=
              RoR::RendererPackagedMediaPathStatus::READY) {
        g_bootstrap_observation =
            BootstrapObservation::PRODUCTION_MEDIA_RESOLUTION_FAILED;
        result.status = RoR::RendererOgreNextFrontendBootstrapStatus::
            INITIALIZATION_FAILED;
        return result;
      }
      RoR::RendererOgreNextProductionSessionConfiguration configuration;
      configuration.shader_media_root =
          std::filesystem::path(g_packaged_media.shader_media_root).u8string();
      configuration.presentation_media_root =
          std::filesystem::path(g_packaged_media.presentation_media_root)
              .u8string();
      g_production_session = RoR::RunRendererOgreNextProductionSession(
          request.bridge_endpoint, configuration);
      result.production_readiness =
          g_production_session.live.peer_ready_sent
              ? RoR::RendererOgreNextProductionReadiness::PEER_READY_SENT
              : RoR::RendererOgreNextProductionReadiness::PRE_PEER_READY;
      if (g_production_session.completed &&
          g_production_session.status ==
              RoR::RendererOgreNextProductionSessionStatus::COMPLETED) {
        g_bootstrap_observation =
            BootstrapObservation::PRODUCTION_BRIDGE_SESSION_COMPLETED;
        result.status =
            RoR::RendererOgreNextFrontendBootstrapStatus::COMPLETED;
        result.accepted = true;
        result.completed = true;
        return result;
      }
      g_bootstrap_observation =
          BootstrapObservation::PRODUCTION_BRIDGE_SESSION_FAILED;
      switch (g_production_session.status) {
      case RoR::RendererOgreNextProductionSessionStatus::
          FAILED_WINDOW_INITIALIZATION:
      case RoR::RendererOgreNextProductionSessionStatus::
          FAILED_FRONTEND_INITIALIZATION:
        result.status = RoR::RendererOgreNextFrontendBootstrapStatus::
            INITIALIZATION_FAILED;
        return result;
      case RoR::RendererOgreNextProductionSessionStatus::
          FAILED_FRONTEND_SHUTDOWN:
      case RoR::RendererOgreNextProductionSessionStatus::
          FAILED_WINDOW_SHUTDOWN:
        result.status =
            RoR::RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED;
        return result;
      case RoR::RendererOgreNextProductionSessionStatus::COMPLETED:
      case RoR::RendererOgreNextProductionSessionStatus::
          REJECTED_INVALID_CONFIGURATION:
      case RoR::RendererOgreNextProductionSessionStatus::FAILED_LIVE_SESSION:
      case RoR::RendererOgreNextProductionSessionStatus::
          FAILED_FRONTEND_AUDIT:
      case RoR::RendererOgreNextProductionSessionStatus::FAILED_INTERNAL:
        result.status =
            RoR::RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL;
        return result;
      }
      result.status =
          RoR::RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL;
      return result;
    }
    if (request.invocation_mode !=
            RoR::RendererOgreNextChildInvocationMode::PROBE_HEADLESS ||
        request.has_bridge_endpoint ||
        request.game_arguments.size() != 1U) {
      result.status =
          RoR::RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL;
      return result;
    }

    RoR::Render::OgreNextN1Configuration configuration;
    configuration.shader_media_root =
        ROR_OGRE_NEXT_CHILD_SHADER_MEDIA_ROOT;
    configuration.raster_feature_tier =
        RoR::Render::OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
    configuration.directional_shadow_mode =
        RoR::Render::OgreNextDirectionalShadowMode::
            PSSM_3_CASCADE_V1;
    configuration.enable_hdr_compositor = false;

    RoR::Render::OgreNextN1Frontend frontend(std::move(configuration));
    RoR::Render::FrontendInitializationRequest initialization;
    initialization.initial_width = 64U;
    initialization.initial_height = 64U;
    initialization.maximum_frames_in_flight = 1U;
    initialization.headless = true;
    initialization.vertical_sync = false;

    const RoR::Render::RenderOperationResult initialized =
        frontend.Initialize(initialization);
    if (!initialized) {
      g_bootstrap_observation =
          IsExactPssmCapabilityUnsupported(initialized)
              ? BootstrapObservation::EXACT_PSSM_CAPABILITY_UNSUPPORTED
              : BootstrapObservation::INITIALIZATION_FAILED;
      result.status = RoR::RendererOgreNextFrontendBootstrapStatus::
          INITIALIZATION_FAILED;
      return result;
    }

    const RoR::Render::RenderOperationResult shutdown =
        frontend.Shutdown(
            RoR::Render::kInfiniteRenderTimeoutNanoseconds);
    if (!shutdown) {
      g_bootstrap_observation = BootstrapObservation::SHUTDOWN_FAILED;
      result.status =
          RoR::RendererOgreNextFrontendBootstrapStatus::SHUTDOWN_FAILED;
      return result;
    }

    g_bootstrap_observation = BootstrapObservation::COMPLETED;
    result.status =
        RoR::RendererOgreNextFrontendBootstrapStatus::COMPLETED;
    result.accepted = true;
    result.completed = true;
    return result;
  } catch (...) {
    g_bootstrap_observation = BootstrapObservation::FAILED_INTERNAL;
    result.status =
        RoR::RendererOgreNextFrontendBootstrapStatus::FAILED_INTERNAL;
    result.completed = false;
    return result;
  }
}

int ExitCodeFor(const RoR::RendererOgreNextChildResult &result) noexcept {
  const bool headless_completed =
      result.status == RoR::RendererOgreNextChildStatus::
                           COMPLETED_HEADLESS_BOOTSTRAP &&
      g_bootstrap_observation == BootstrapObservation::COMPLETED;
  const bool production_completed =
      result.status == RoR::RendererOgreNextChildStatus::
                           COMPLETED_PRODUCTION_BRIDGE_SESSION &&
      g_bootstrap_observation ==
          BootstrapObservation::PRODUCTION_BRIDGE_SESSION_COMPLETED;
  if (result.completed && result.accepted &&
      (headless_completed || production_completed)) {
    return kRendererOgreNextChildSuccessExitCode;
  }
  const int production_failure_exit =
      RoR::ResolveRendererOgreNextProductionFailureExitCode(result);
  if (production_failure_exit != 0) {
    return production_failure_exit;
  }
  if (result.status ==
          RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INITIALIZATION &&
      result.frontend.status ==
          RoR::RendererOgreNextFrontendBootstrapStatus::
              INITIALIZATION_FAILED &&
      g_bootstrap_observation ==
          BootstrapObservation::EXACT_PSSM_CAPABILITY_UNSUPPORTED) {
    return kRendererOgreNextChildCapabilityUnsupportedExitCode;
  }
  if (result.status ==
      RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INITIALIZATION) {
    return kRendererOgreNextChildInitializationExitCode;
  }
  if (result.status ==
      RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_SHUTDOWN) {
    return kRendererOgreNextChildShutdownExitCode;
  }
  if (result.status == RoR::RendererOgreNextChildStatus::FAILED_INTERNAL ||
      result.status ==
          RoR::RendererOgreNextChildStatus::FAILED_FRONTEND_INTERNAL) {
    return kRendererOgreNextChildInternalExitCode;
  }
  return kRendererOgreNextChildRejectedExitCode;
}

int RunRendererOgreNextChildExecutable(
    int argc,
    const RoR::RendererChildLauncherChar *const argv[]) noexcept {
  g_bootstrap_observation = BootstrapObservation::NOT_RUN;
  g_production_session = RoR::RendererOgreNextProductionSessionResult{};
  g_packaged_media = RoR::RendererPackagedMediaPathResult{};
  RoR::RendererOgreNextChildRuntime runtime;
  runtime.collect_native_preflight =
      &RoR::CollectRendererOgreNextChildNativePreflight;
  runtime.bootstrap_frontend = &BootstrapProductionFrontend;
  const RoR::RendererOgreNextChildResult result =
      RoR::RunRendererOgreNextChild(argc, argv, runtime);
  const int exit_code = ExitCodeFor(result);

  if (exit_code == kRendererOgreNextChildSuccessExitCode) {
    if (result.invocation_mode ==
        RoR::RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE) {
      (void)std::fprintf(
          stdout,
          "RoR Ogre-Next child: completed-production-bridge-session "
          "(frames=%llu, gpu-only=%llu, readbacks=%llu)\n",
          static_cast<unsigned long long>(
              g_production_session.presented_frames),
          static_cast<unsigned long long>(
              g_production_session.gpu_only_output_frames),
          static_cast<unsigned long long>(
              g_production_session.source_readbacks));
    } else {
      (void)std::fprintf(
          stdout,
          "RoR Ogre-Next child: completed-headless-bootstrap\n");
    }
    (void)std::fflush(stdout);
    return exit_code;
  }
  if (exit_code ==
      kRendererOgreNextChildCapabilityUnsupportedExitCode) {
    (void)std::fprintf(
        stderr,
        "RoR Ogre-Next child: skipped-exact-pssm-capability-unsupported\n");
    (void)std::fflush(stderr);
    return exit_code;
  }
  (void)std::fprintf(
      stderr,
      "RoR Ogre-Next child: %s (intent=%s, bridge=%s, mode=%s, "
      "startup=%s, frontend=%s, readiness=%s)\n",
      RoR::ToString(result.status),
      RoR::ToString(result.intent_status),
      RoR::ToString(result.bridge_status),
      RoR::ToString(result.invocation_mode),
      RoR::ToString(result.startup_plan.status),
      RoR::ToString(result.frontend.status),
      RoR::ToString(result.production_readiness));
  if (result.invocation_mode ==
      RoR::RendererOgreNextChildInvocationMode::PRODUCTION_BRIDGE) {
    if (g_bootstrap_observation ==
        BootstrapObservation::PRODUCTION_MEDIA_RESOLUTION_FAILED) {
      (void)std::fprintf(
          stderr,
          "RoR Ogre-Next packaged media: %s (native-error=%u)\n",
          RoR::ToString(g_packaged_media.status),
          static_cast<unsigned>(g_packaged_media.native_error_code));
    }
    (void)std::fprintf(
        stderr,
        "RoR Ogre-Next production session: %s "
        "(live=%s, channel=%s, stream=%s, dispatch=%s)\n",
        RoR::ToString(g_production_session.status),
        RoR::ToString(g_production_session.live.status),
        RoR::ToString(g_production_session.live.channel_status),
        RoR::Render::ToString(g_production_session.live.stream_status),
        RoR::Render::ToString(g_production_session.live.dispatch_status));
  }
  (void)std::fflush(stderr);
  return exit_code;
}

} // namespace

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argument_count = 0;
  LPWSTR *arguments =
      ::CommandLineToArgvW(::GetCommandLineW(), &argument_count);
  if (arguments == nullptr || argument_count < 1) {
    (void)std::fprintf(
        stderr,
        "RoR Ogre-Next child: failed-windows-command-line-decode\n");
    (void)std::fflush(stderr);
    return kRendererOgreNextChildInternalExitCode;
  }
  const int result = RunRendererOgreNextChildExecutable(
      argument_count,
      const_cast<const wchar_t *const *>(arguments));
  (void)::LocalFree(arguments);
  return result;
}

#else

int main(int argc, char *argv[]) {
  return RunRendererOgreNextChildExecutable(
      argc, const_cast<const char *const *>(argv));
}

#endif
