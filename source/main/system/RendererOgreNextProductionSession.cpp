/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextProductionSession.h"

#include "OgreNextN1Frontend.h"
#include "RendererOgreNextSdlWindowRuntime.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace RoR {
namespace {

using namespace Render;

constexpr std::uint64_t kSurfaceUpdateTimeoutNanoseconds =
    UINT64_C(5000000000);

RendererOgreNextWindowPlatform HostWindowPlatform() noexcept {
#if defined(__APPLE__)
  return RendererOgreNextWindowPlatform::MACOS_COCOA_METAL;
#elif defined(_WIN32)
  return RendererOgreNextWindowPlatform::WINDOWS_WIN32;
#elif defined(__linux__)
  return RendererOgreNextWindowPlatform::LINUX_X11_XCB;
#else
  return RendererOgreNextWindowPlatform::UNKNOWN;
#endif
}

NativeWindowHandle MakeWindowHandle(
    const RendererOgreNextSdlNativeWindow &native) noexcept {
  NativeWindowHandle result;
  result.generation = 1U;
#if defined(__APPLE__)
  result.system = NativeWindowSystem::COCOA;
  result.surface =
      reinterpret_cast<std::uintptr_t>(native.native_render_view);
#elif defined(_WIN32)
  result.system = NativeWindowSystem::WINDOWS;
  result.surface = native.native_window;
#elif defined(__linux__)
  result.system = NativeWindowSystem::X11;
  result.connection =
      reinterpret_cast<std::uintptr_t>(native.native_display);
  result.surface = native.native_window;
#endif
  return result;
}

Float2 ContentScale(const RendererOgreNextWindowMetrics &metrics) noexcept {
  return {static_cast<float>(metrics.content_scale_x),
          static_cast<float>(metrics.content_scale_y)};
}

FrontendSurfaceUpdate MakeSurface(
    const NativeWindowHandle &window,
    const RendererOgreNextWindowMetrics &metrics,
    std::uint64_t revision, bool suspended) noexcept {
  FrontendSurfaceUpdate update;
  update.surface_revision = revision;
  update.window = window;
  update.pixel_width = suspended ? 0U : metrics.drawable_width;
  update.pixel_height = suspended ? 0U : metrics.drawable_height;
  update.content_scale = ContentScale(metrics);
  update.suspended = suspended;
  return update;
}

RenderBridgeSurfaceState MakeBridgeSurface(
    const RendererOgreNextWindowMetrics &metrics, std::uint64_t revision,
    bool suspended) noexcept {
  RenderBridgeSurfaceState surface;
  surface.surface_revision = revision;
  surface.logical_width = metrics.logical_width;
  surface.logical_height = metrics.logical_height;
  surface.drawable_width = suspended ? 0U : metrics.drawable_width;
  surface.drawable_height = suspended ? 0U : metrics.drawable_height;
  surface.suspended = suspended;
  return surface;
}

template <std::size_t DestinationCapacity, std::size_t SourceCapacity>
bool CopyParameters(
    const std::array<RendererOgreNextWindowParameter, SourceCapacity> &source,
    std::size_t source_count,
    std::array<OgreNextN1PresentationParameter, DestinationCapacity>
        &destination,
    std::size_t &destination_count) {
  if (source_count > source.size() || source_count > destination.size()) {
    return false;
  }
  destination_count = source_count;
  for (std::size_t index = 0U; index < source_count; ++index) {
    destination[index].name = source[index].name;
    destination[index].value = source[index].value;
  }
  return true;
}

std::int32_t ScaledPixels(int logical, double scale) noexcept {
  const double scaled = static_cast<double>(logical) * scale;
  if (!std::isfinite(scaled)) {
    return 0;
  }
  if (scaled <=
      static_cast<double>((std::numeric_limits<std::int32_t>::min)())) {
    return (std::numeric_limits<std::int32_t>::min)();
  }
  if (scaled >=
      static_cast<double>((std::numeric_limits<std::int32_t>::max)())) {
    return (std::numeric_limits<std::int32_t>::max)();
  }
  return static_cast<std::int32_t>(std::llround(scaled));
}

template <typename Value>
void SetPressed(std::vector<Value> &pressed, Value value, bool down) {
  const auto position = std::lower_bound(pressed.begin(), pressed.end(), value);
  if (down && (position == pressed.end() || *position != value)) {
    pressed.insert(position, value);
  } else if (!down && position != pressed.end() && *position == value) {
    pressed.erase(position);
  }
}

struct ProductionContext final {
  RendererOgreNextSdlWindowRuntime *adapter = nullptr;
  RendererOgreNextWindowHost *host = nullptr;
  OgreNextN1Frontend *frontend = nullptr;
  NativeWindowHandle window;
  void *sdl_window = nullptr;
  std::uint64_t registry_id = 0U;
  std::uint64_t surface_revision = 1U;
  std::uint64_t metrics_generation = 0U;
  std::uint64_t next_event_id = 1U;
  std::uint64_t last_timestamp_ns = 0U;
  std::vector<Sdl2PhysicalScancode> pressed_scancodes;
  std::vector<Sdl2MouseButton> pressed_mouse_buttons;
  InputTransportFocusState focus = InputTransportFocusState::LOST;
  bool focus_observed = false;
  bool close_observed = false;
  bool suspended = false;
};

bool ShowAfterWorkspaceReady(void *opaque,
                             FrontendSurfaceUpdate *acknowledged_surface) {
  auto *context = static_cast<ProductionContext *>(opaque);
  if (context == nullptr || context->host == nullptr ||
      acknowledged_surface == nullptr ||
      context->host->Resume() !=
          RendererOgreNextWindowHostStatus::COMPLETED ||
      context->host->Lifecycle() !=
          RendererOgreNextWindowLifecycle::ACTIVE) {
    return false;
  }
  const RendererOgreNextWindowMetrics *metrics = context->host->Metrics();
  if (metrics == nullptr) {
    return false;
  }
  if (context->metrics_generation != metrics->generation) {
    if (context->surface_revision ==
        (std::numeric_limits<std::uint64_t>::max)() - 1U) {
      return false;
    }
    ++context->surface_revision;
  }
  context->metrics_generation = metrics->generation;
  context->suspended = false;
  *acknowledged_surface = MakeSurface(
      context->window, *metrics, context->surface_revision, false);
  return true;
}

std::uint64_t TimestampNow(ProductionContext &context) noexcept {
  const std::uint64_t ticks = SDL_GetTicks64();
  const std::uint64_t candidate =
      ticks > (std::numeric_limits<std::uint64_t>::max)() / UINT64_C(1000000)
          ? (std::numeric_limits<std::uint64_t>::max)()
          : ticks * UINT64_C(1000000);
  context.last_timestamp_ns =
      (std::max)(context.last_timestamp_ns, candidate);
  return context.last_timestamp_ns;
}

bool AddInputEvent(ProductionContext &context,
                   InputTransportEventPayload payload,
                   InputTransportBatch &batch) {
  if (batch.events.size() >= kInputEventTransportMaximumEvents ||
      context.next_event_id == 0U ||
      context.next_event_id ==
          (std::numeric_limits<std::uint64_t>::max)()) {
    return false;
  }
  InputTransportEvent event;
  event.event_id = context.next_event_id;
  event.host_timestamp_ns = TimestampNow(context);
  event.payload = std::move(payload);
  batch.events.push_back(std::move(event));
  ++context.next_event_id;
  return true;
}

bool PollSdlInput(ProductionContext &context,
                  const RendererOgreNextSdlWindowEventBatch &window_events,
                  const RendererOgreNextWindowMetrics &metrics,
                  InputTransportBatch &batch) {
  SDL_Event event{};
  int status = 0;
  while (batch.events.size() < kInputEventTransportMaximumEvents &&
         (status = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_KEYDOWN,
                                  SDL_MOUSEWHEEL)) > 0) {
    switch (event.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      const auto scancode =
          static_cast<Sdl2PhysicalScancode>(event.key.keysym.scancode);
      if (!IsKnownSdl2PhysicalScancode(scancode)) {
        break;
      }
      const bool down = event.type == SDL_KEYDOWN;
      SetPressed(context.pressed_scancodes, scancode, down);
      InputTransportKeyboardKeyEvent key;
      key.scancode = scancode;
      key.state = down ? InputTransportDigitalState::PRESSED
                       : InputTransportDigitalState::RELEASED;
      key.repeat = event.key.repeat != 0U;
      if (!AddInputEvent(context, key, batch)) {
        return false;
      }
      break;
    }
    case SDL_TEXTINPUT: {
      InputTransportTextInputEvent text;
      text.utf8_text = event.text.text;
      if (!text.utf8_text.empty() &&
          (!IsValidInputTransportUtf8(text.utf8_text) ||
           !AddInputEvent(context, std::move(text), batch))) {
        return false;
      }
      break;
    }
    case SDL_MOUSEMOTION: {
      InputTransportMouseMotionEvent motion;
      motion.position_x_pixels =
          ScaledPixels(event.motion.x, metrics.content_scale_x);
      motion.position_y_pixels =
          ScaledPixels(event.motion.y, metrics.content_scale_y);
      motion.delta_x_pixels =
          ScaledPixels(event.motion.xrel, metrics.content_scale_x);
      motion.delta_y_pixels =
          ScaledPixels(event.motion.yrel, metrics.content_scale_y);
      if (!AddInputEvent(context, motion, batch)) {
        return false;
      }
      break;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
      const auto button = static_cast<Sdl2MouseButton>(event.button.button);
      if (event.button.button < SDL_BUTTON_LEFT ||
          event.button.button > SDL_BUTTON_X2) {
        break;
      }
      const bool down = event.type == SDL_MOUSEBUTTONDOWN;
      SetPressed(context.pressed_mouse_buttons, button, down);
      InputTransportMouseButtonEvent mouse_button;
      mouse_button.button = button;
      mouse_button.state = down ? InputTransportDigitalState::PRESSED
                                : InputTransportDigitalState::RELEASED;
      if (!AddInputEvent(context, mouse_button, batch)) {
        return false;
      }
      break;
    }
    case SDL_MOUSEWHEEL: {
      InputTransportMouseWheelEvent wheel;
      wheel.delta_x = event.wheel.preciseX;
      wheel.delta_y = event.wheel.preciseY;
      wheel.direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                            ? Sdl2MouseWheelDirection::FLIPPED
                            : Sdl2MouseWheelDirection::NORMAL;
      if (!AddInputEvent(context, wheel, batch)) {
        return false;
      }
      break;
    }
    default:
      break;
    }
  }
  if (status < 0) {
    return false;
  }

  const InputTransportFocusState observed_focus =
      window_events.focused ? InputTransportFocusState::GAINED
                            : InputTransportFocusState::LOST;
  if (!context.focus_observed || context.focus != observed_focus) {
    context.focus_observed = true;
    context.focus = observed_focus;
    if (observed_focus == InputTransportFocusState::LOST) {
      context.pressed_scancodes.clear();
      context.pressed_mouse_buttons.clear();
    }
    InputTransportFocusEvent focus;
    focus.state = observed_focus;
    if (!AddInputEvent(context, focus, batch)) {
      return false;
    }
  }
  if (window_events.close_requested && !context.close_observed) {
    context.close_observed = true;
    if (!AddInputEvent(context, InputTransportWindowCloseEvent{}, batch)) {
      return false;
    }
  }

  batch.clock_origin_id = context.registry_id;
  batch.reconciliation.through_event_id = context.next_event_id - 1U;
  batch.reconciliation.host_timestamp_ns = TimestampNow(context);
  batch.reconciliation.focus = context.focus;
  batch.reconciliation.window_close_requested = context.close_observed;
  batch.reconciliation.pressed_scancodes = context.pressed_scancodes;
  batch.reconciliation.pressed_mouse_buttons =
      context.pressed_mouse_buttons;
  return ValidateInputTransportBatch(batch) == RenderTransportStatus::OK;
}

bool ApplySurfaceEvents(
    ProductionContext &context,
    const RendererOgreNextSdlWindowEventBatch &events) {
  if (context.host == nullptr || context.frontend == nullptr) {
    return false;
  }
  const RendererOgreNextWindowLifecycle lifecycle = context.host->Lifecycle();
  if ((events.minimized || events.hidden) &&
      lifecycle == RendererOgreNextWindowLifecycle::ACTIVE) {
    if (context.host->Suspend() !=
        RendererOgreNextWindowHostStatus::COMPLETED) {
      return false;
    }
    const RendererOgreNextWindowMetrics *metrics = context.host->Metrics();
    if (metrics == nullptr || context.surface_revision ==
                                  (std::numeric_limits<std::uint64_t>::max)() -
                                      1U) {
      return false;
    }
    ++context.surface_revision;
    context.suspended = true;
    return static_cast<bool>(context.frontend->UpdateSurface(
        MakeSurface(context.window, *metrics, context.surface_revision, true),
        false, kSurfaceUpdateTimeoutNanoseconds));
  }
  if (!events.minimized && !events.hidden &&
      lifecycle == RendererOgreNextWindowLifecycle::SUSPENDED) {
    if (context.host->Resume() !=
            RendererOgreNextWindowHostStatus::COMPLETED ||
        context.host->RefreshMetrics() !=
            RendererOgreNextWindowHostStatus::COMPLETED) {
      return false;
    }
    const RendererOgreNextWindowMetrics *metrics = context.host->Metrics();
    if (metrics == nullptr || context.surface_revision ==
                                  (std::numeric_limits<std::uint64_t>::max)() -
                                      1U) {
      return false;
    }
    ++context.surface_revision;
    context.metrics_generation = metrics->generation;
    context.suspended = false;
    return static_cast<bool>(context.frontend->UpdateSurface(
        MakeSurface(context.window, *metrics, context.surface_revision, false),
        false, kSurfaceUpdateTimeoutNanoseconds));
  }
  if (events.resize_events != 0U &&
      lifecycle == RendererOgreNextWindowLifecycle::ACTIVE) {
    if (context.host->Resize(events.logical_width, events.logical_height) !=
        RendererOgreNextWindowHostStatus::COMPLETED) {
      return false;
    }
  } else if (events.drawable_size_changed &&
             lifecycle == RendererOgreNextWindowLifecycle::ACTIVE &&
             context.host->RefreshMetrics() !=
                 RendererOgreNextWindowHostStatus::COMPLETED) {
    return false;
  }
  const RendererOgreNextWindowMetrics *metrics = context.host->Metrics();
  if (metrics == nullptr) {
    return false;
  }
  if (lifecycle == RendererOgreNextWindowLifecycle::ACTIVE &&
      metrics->generation != context.metrics_generation) {
    if (context.surface_revision ==
        (std::numeric_limits<std::uint64_t>::max)() - 1U) {
      return false;
    }
    ++context.surface_revision;
    context.metrics_generation = metrics->generation;
    if (!context.frontend->UpdateSurface(
            MakeSurface(context.window, *metrics, context.surface_revision,
                        false),
            false, kSurfaceUpdateTimeoutNanoseconds)) {
      return false;
    }
  }
  return true;
}

bool PollProduction(void *opaque, std::uint64_t,
                    RendererOgreNextLiveSessionObservation *observation) {
  auto *context = static_cast<ProductionContext *>(opaque);
  if (context == nullptr || context->adapter == nullptr ||
      context->host == nullptr || context->frontend == nullptr ||
      context->sdl_window == nullptr || observation == nullptr) {
    return false;
  }
  RendererOgreNextSdlWindowEventBatch events;
  if (!context->adapter->PollWindowEvents(context->sdl_window, events) ||
      !ApplySurfaceEvents(*context, events)) {
    return false;
  }
  const RendererOgreNextWindowMetrics *metrics = context->host->Metrics();
  if (metrics == nullptr) {
    return false;
  }
  *observation = RendererOgreNextLiveSessionObservation{};
  if (!PollSdlInput(*context, events, *metrics, observation->response)) {
    return false;
  }
  observation->surface = MakeBridgeSurface(
      *metrics, context->surface_revision, context->suspended);
  observation->window_close_requested = events.close_requested;
  return true;
}

bool ValidConfiguration(
    const RendererOgreNextProductionSessionConfiguration &configuration)
    noexcept {
  return configuration.version ==
             kRendererOgreNextProductionSessionContractVersion &&
         !configuration.shader_media_root.empty() &&
         !configuration.presentation_media_root.empty() &&
         configuration.logical_width > 0U &&
         configuration.logical_height > 0U &&
         configuration.logical_width <= 32768U &&
         configuration.logical_height <= 32768U;
}

RendererOgreNextProductionSessionResult MakeFailure(
    RendererOgreNextProductionSessionResult result,
    RendererOgreNextProductionSessionStatus status) noexcept {
  result.status = status;
  result.completed = false;
  return result;
}

} // namespace

RendererOgreNextProductionSessionResult RunRendererOgreNextProductionSession(
    const RendererBridgeEndpoint &endpoint,
    const RendererOgreNextProductionSessionConfiguration &configuration)
    noexcept {
  RendererOgreNextProductionSessionResult result;
  if (!ValidConfiguration(configuration) ||
      !IsValidRendererBridgeEndpoint(endpoint) ||
      endpoint.role != RendererBridgeRole::PRESENTATION_FRONTEND) {
    return MakeFailure(
        result,
        RendererOgreNextProductionSessionStatus::
            REJECTED_INVALID_CONFIGURATION);
  }
  try {
    RendererOgreNextSdlWindowRuntime adapter;
    RendererOgreNextWindowRequest window_request;
    window_request.platform = HostWindowPlatform();
    window_request.logical_width = configuration.logical_width;
    window_request.logical_height = configuration.logical_height;
    window_request.fsaa_samples = 0U;
#if !defined(__APPLE__)
    window_request.vertical_sync = false;
    window_request.vertical_sync_interval = 0U;
#endif
    RendererOgreNextWindowHost host;
    if (host.Initialize(window_request, adapter.Runtime()) !=
        RendererOgreNextWindowHostStatus::COMPLETED) {
      return MakeFailure(
          result,
          RendererOgreNextProductionSessionStatus::
              FAILED_WINDOW_INITIALIZATION);
    }
    const RendererOgreNextWindowBinding *binding = host.Binding();
    const RendererOgreNextSdlNativeWindow *native = host.NativeWindow();
    const RendererOgreNextWindowMetrics *metrics = host.Metrics();
    if (binding == nullptr || native == nullptr || metrics == nullptr) {
      (void)host.Shutdown();
      return MakeFailure(
          result,
          RendererOgreNextProductionSessionStatus::
              FAILED_WINDOW_INITIALIZATION);
    }
    const NativeWindowHandle window = MakeWindowHandle(*native);
    if (!window.valid()) {
      (void)host.Shutdown();
      return MakeFailure(
          result,
          RendererOgreNextProductionSessionStatus::
              FAILED_WINDOW_INITIALIZATION);
    }

    ProductionContext context;
    context.adapter = &adapter;
    context.host = &host;
    context.window = window;
    context.sdl_window = native->sdl_window;
    context.registry_id =
        DeriveRenderAssetRegistryIdFromBridgeSession(endpoint.session_id);
    context.metrics_generation = metrics->generation;
    if (context.registry_id == 0U) {
      (void)host.Shutdown();
      return MakeFailure(result,
                         RendererOgreNextProductionSessionStatus::
                             FAILED_INTERNAL);
    }
    OgreNextN1Configuration frontend_configuration;
    frontend_configuration.shader_media_root =
        configuration.shader_media_root;
    frontend_configuration.raster_feature_tier =
        OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
    // Persistent HDR and the pinned Ogre PSSM node do not yet have a safe
    // shared compositor contract. Do not advertise or execute that invalid
    // combination in the production session; V2 directional visibility is a
    // separate native tier and standalone PSSM keeps its own proof.
    frontend_configuration.directional_shadow_mode =
        OgreNextDirectionalShadowMode::DISABLED;
    frontend_configuration.enable_hdr_compositor = true;
    frontend_configuration.presentation.enabled = true;
    frontend_configuration.presentation.mode =
        OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP;
    frontend_configuration.presentation.gpu_only_output = true;
    frontend_configuration.presentation.shader_media_root =
        configuration.presentation_media_root;
    frontend_configuration.presentation.exact_window = window;
    if (!CopyParameters(
            binding->renderer_options, binding->renderer_option_count,
            frontend_configuration.presentation.renderer_options,
            frontend_configuration.presentation.renderer_option_count) ||
        !CopyParameters(
            binding->bootstrap_window_parameters,
            binding->bootstrap_window_parameter_count,
            frontend_configuration.presentation.bootstrap_window_parameters,
            frontend_configuration.presentation
                .bootstrap_window_parameter_count) ||
        !CopyParameters(
            binding->presentation_window_parameters,
            binding->presentation_window_parameter_count,
            frontend_configuration.presentation
                .presentation_window_parameters,
            frontend_configuration.presentation
                .presentation_window_parameter_count)) {
      (void)host.Shutdown();
      return MakeFailure(result,
                         RendererOgreNextProductionSessionStatus::
                             FAILED_INTERNAL);
    }
    frontend_configuration.presentation.show_callback_context = &context;
    frontend_configuration.presentation.show_after_workspace_ready =
        &ShowAfterWorkspaceReady;

    OgreNextN1Frontend frontend(std::move(frontend_configuration));
    context.frontend = &frontend;
    FrontendInitializationRequest initialization;
    initialization.initial_surface_revision = context.surface_revision;
    initialization.window = window;
    initialization.initial_width = metrics->drawable_width;
    initialization.initial_height = metrics->drawable_height;
    initialization.initial_content_scale = ContentScale(*metrics);
    initialization.maximum_frames_in_flight = 1U;
    initialization.headless = false;
    initialization.vertical_sync = false;
    if (!frontend.Initialize(initialization)) {
      (void)frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
      (void)host.Shutdown();
      return MakeFailure(
          result,
          RendererOgreNextProductionSessionStatus::
              FAILED_FRONTEND_INITIALIZATION);
    }

    metrics = host.Metrics();
    const RenderBridgeSurfaceState initial_bridge_surface =
        metrics == nullptr
            ? RenderBridgeSurfaceState{}
            : MakeBridgeSurface(*metrics, context.surface_revision, false);
    if (metrics == nullptr ||
        !IsValidRenderBridgeSurfaceState(initial_bridge_surface, false)) {
      (void)frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
      (void)host.Shutdown();
      return MakeFailure(
          result,
          RendererOgreNextProductionSessionStatus::
              FAILED_WINDOW_INITIALIZATION);
    }

    RendererOgreNextLiveSessionRuntime live_runtime;
    live_runtime.frontend = &frontend;
    live_runtime.context = &context;
    live_runtime.poll = &PollProduction;
    live_runtime.initial_surface = initial_bridge_surface;
    live_runtime.idle_poll_interval_milliseconds = 4U;
    result.live = RunRendererOgreNextLiveSession(endpoint, live_runtime);
    const OgreNextN1PresentationAudit audit =
        frontend.QueryPresentationAudit();
    const OgreNextNativeLightingPassAudit lighting_audit =
        frontend.QueryNativeLightingPassAudit();
    result.presented_frames = audit.presented_frames;
    result.gpu_only_output_frames = audit.gpu_only_output_frames;
    result.source_readbacks = audit.source_readbacks;
    result.ui_free_source = audit.ui_free_source;
    result.cpu_window_copy = audit.cpu_window_copy;

    RendererOgreNextProductionSessionStatus status =
        result.live.completed && result.live.peer_ready_sent
            ? RendererOgreNextProductionSessionStatus::COMPLETED
            : RendererOgreNextProductionSessionStatus::FAILED_LIVE_SESSION;
    if (result.live.presented_scene_frames != audit.presented_frames ||
        audit.gpu_only_output_frames != audit.presented_frames ||
        audit.source_readbacks != 0U || audit.cpu_window_copy ||
        (audit.presented_frames != 0U &&
         (!audit.ui_free_source || !audit.gpu_quad_copy ||
          !audit.monotonic_presented_frame_ids))) {
      status =
          RendererOgreNextProductionSessionStatus::FAILED_FRONTEND_AUDIT;
    }
    if (lighting_audit.completed_frames != audit.presented_frames ||
        lighting_audit.production_content_readbacks != 0U ||
        lighting_audit.production_framebuffer_readbacks != 0U ||
        lighting_audit.ogre14_lighting_passes != 0U ||
        (lighting_audit.completed_frames != 0U &&
         (!lighting_audit.linear_rgba16_hdr_target ||
          !lighting_audit.separate_base_hdr_target ||
          !lighting_audit.separate_unoccluded_sun_full_hdr_target ||
          !lighting_audit.separate_sun_direct_hdr_target ||
          !lighting_audit.gpu_sun_direct_derivation ||
          !lighting_audit.transactional_directional_sun_toggle ||
          !lighting_audit.raster_lit_hdr_target ||
          !lighting_audit.single_step_hdr_history ||
          lighting_audit.raster_scene_evaluations != 3U ||
          !lighting_audit.hdr_auto_exposure ||
          !lighting_audit.gpu_hdr_history_sequenced ||
          !lighting_audit.hdr_bloom || !lighting_audit.filmic_tone_map ||
          !lighting_audit.srgb_presentation ||
          !lighting_audit.production_gpu_only ||
          !lighting_audit.no_ogre14_lighting))) {
      status =
          RendererOgreNextProductionSessionStatus::FAILED_FRONTEND_AUDIT;
    }
    RenderOperationResult frontend_shutdown =
        frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
    if (!frontend_shutdown) {
      frontend_shutdown =
          frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
    }
    if (!frontend_shutdown) {
      status = RendererOgreNextProductionSessionStatus::
          FAILED_FRONTEND_SHUTDOWN;
    }
    RendererOgreNextWindowHostStatus window_shutdown = host.Shutdown();
    if (window_shutdown != RendererOgreNextWindowHostStatus::COMPLETED) {
      window_shutdown = host.Shutdown();
    }
    if (window_shutdown != RendererOgreNextWindowHostStatus::COMPLETED) {
      status =
          RendererOgreNextProductionSessionStatus::FAILED_WINDOW_SHUTDOWN;
    }
    result.status = status;
    result.completed = status ==
                       RendererOgreNextProductionSessionStatus::COMPLETED;
    return result;
  } catch (...) {
    return MakeFailure(result,
                       RendererOgreNextProductionSessionStatus::
                           FAILED_INTERNAL);
  }
}

bool IsKnownRendererOgreNextProductionSessionStatus(
    RendererOgreNextProductionSessionStatus status) noexcept {
  switch (status) {
  case RendererOgreNextProductionSessionStatus::COMPLETED:
  case RendererOgreNextProductionSessionStatus::
      REJECTED_INVALID_CONFIGURATION:
  case RendererOgreNextProductionSessionStatus::FAILED_WINDOW_INITIALIZATION:
  case RendererOgreNextProductionSessionStatus::
      FAILED_FRONTEND_INITIALIZATION:
  case RendererOgreNextProductionSessionStatus::FAILED_LIVE_SESSION:
  case RendererOgreNextProductionSessionStatus::FAILED_FRONTEND_AUDIT:
  case RendererOgreNextProductionSessionStatus::FAILED_FRONTEND_SHUTDOWN:
  case RendererOgreNextProductionSessionStatus::FAILED_WINDOW_SHUTDOWN:
  case RendererOgreNextProductionSessionStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererOgreNextProductionSessionStatus status) noexcept {
  switch (status) {
  case RendererOgreNextProductionSessionStatus::COMPLETED:
    return "completed";
  case RendererOgreNextProductionSessionStatus::
      REJECTED_INVALID_CONFIGURATION:
    return "rejected-invalid-configuration";
  case RendererOgreNextProductionSessionStatus::FAILED_WINDOW_INITIALIZATION:
    return "failed-window-initialization";
  case RendererOgreNextProductionSessionStatus::
      FAILED_FRONTEND_INITIALIZATION:
    return "failed-frontend-initialization";
  case RendererOgreNextProductionSessionStatus::FAILED_LIVE_SESSION:
    return "failed-live-session";
  case RendererOgreNextProductionSessionStatus::FAILED_FRONTEND_AUDIT:
    return "failed-frontend-audit";
  case RendererOgreNextProductionSessionStatus::FAILED_FRONTEND_SHUTDOWN:
    return "failed-frontend-shutdown";
  case RendererOgreNextProductionSessionStatus::FAILED_WINDOW_SHUTDOWN:
    return "failed-window-shutdown";
  case RendererOgreNextProductionSessionStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
