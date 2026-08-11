/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Strict 1,000-frame SDL/Ogre-Next production presentation smoke.

#include "OgreNextN1Frontend.h"
#include "RendererOgreNextSdlWindowRuntime.h"
#include "RendererOgreNextWindowHost.h"
#include "ror_ogre_next_n1_config.h"

#if defined(_WIN32)
// This executable owns the normal console entry point. SDL otherwise rewrites
// `main` to `SDL_main` on Windows, leaving the console subsystem with no entry
// point unless SDL2main is linked.
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

constexpr std::uint64_t kRegistryId = UINT64_C(0x52554E4C4F4F5031);
constexpr std::uint64_t kViewId = 1U;
constexpr std::uint64_t kRequiredPresentedFrames = 1000U;

struct Arguments final {
  std::string media_root;
  std::string output_path;
  std::string report_path;
};

struct ShowContext final {
  RoR::RendererOgreNextWindowHost *host = nullptr;
  NativeWindowHandle window;
  std::uint64_t surface_revision = 1U;
  std::uint64_t metrics_generation = 0U;
  std::uint32_t pixel_width = 0U;
  std::uint32_t pixel_height = 0U;
  std::uint64_t calls = 0U;
};

struct EventTotals final {
  std::uint64_t polls = 0U;
  std::uint64_t polled_events = 0U;
  std::uint64_t matched_window_events = 0U;
  std::uint64_t close_events = 0U;
  std::uint64_t focus_gained_events = 0U;
  std::uint64_t focus_lost_events = 0U;
  std::uint64_t resize_events = 0U;
  std::uint64_t minimize_events = 0U;
  std::uint64_t restore_events = 0U;
  std::uint64_t display_change_events = 0U;
  std::uint64_t drawable_size_changes = 0U;
  std::uint64_t display_metric_refreshes = 0U;
  bool close_requested = false;
};

[[noreturn]] void Fail(const std::string &detail) {
  throw std::runtime_error(detail);
}

void Require(bool condition, const std::string &detail) {
  if (!condition) {
    Fail(detail);
  }
}

void RequireSuccess(const RenderOperationResult &result,
                    const std::string &operation) {
  if (!result) {
    Fail(operation + " failed: " + result.detail);
  }
}

Arguments ParseArguments(int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--media-root" && index + 1 < argc) {
      arguments.media_root = argv[++index];
    } else if (option == "--output" && index + 1 < argc) {
      arguments.output_path = argv[++index];
    } else if (option == "--report" && index + 1 < argc) {
      arguments.report_path = argv[++index];
    } else {
      Fail("usage: ror_ogre_next_window_run_loop_smoke --media-root "
           "ABSOLUTE_PATH --output FRAME.ppm --report REPORT.json");
    }
  }
  Require(!arguments.media_root.empty() && !arguments.output_path.empty() &&
              !arguments.report_path.empty(),
          "run-loop smoke media, output, and report paths are required");
  Require(std::filesystem::path(arguments.media_root).is_absolute(),
          "run-loop smoke media root must be absolute");
  return arguments;
}

RoR::RendererOgreNextWindowPlatform HostPlatform() noexcept {
#if defined(__APPLE__)
  return RoR::RendererOgreNextWindowPlatform::MACOS_COCOA_METAL;
#elif defined(_WIN32)
  return RoR::RendererOgreNextWindowPlatform::WINDOWS_WIN32;
#elif defined(__linux__)
  return RoR::RendererOgreNextWindowPlatform::LINUX_X11_XCB;
#else
  return RoR::RendererOgreNextWindowPlatform::UNKNOWN;
#endif
}

NativeWindowHandle MakeWindowHandle(
    const RoR::RendererOgreNextSdlNativeWindow &native) {
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
  Require(result.valid(), "SDL host did not expose a valid native window");
  return result;
}

Float2 ContentScale(const RoR::RendererOgreNextWindowMetrics &metrics) {
  return {static_cast<float>(metrics.content_scale_x),
          static_cast<float>(metrics.content_scale_y)};
}

FrontendSurfaceUpdate MakeSurface(
    const NativeWindowHandle &window,
    const RoR::RendererOgreNextWindowMetrics &metrics,
    std::uint64_t revision, bool suspended = false) {
  FrontendSurfaceUpdate update;
  update.surface_revision = revision;
  update.window = window;
  update.pixel_width = suspended ? 0U : metrics.drawable_width;
  update.pixel_height = suspended ? 0U : metrics.drawable_height;
  update.content_scale = ContentScale(metrics);
  update.suspended = suspended;
  return update;
}

bool ShowAfterWorkspaceReady(void *opaque,
                             FrontendSurfaceUpdate *acknowledged_surface) {
  auto *context = static_cast<ShowContext *>(opaque);
  if (context == nullptr || context->host == nullptr ||
      acknowledged_surface == nullptr ||
      context->host->Resume() !=
          RoR::RendererOgreNextWindowHostStatus::COMPLETED ||
      context->host->Lifecycle() !=
          RoR::RendererOgreNextWindowLifecycle::ACTIVE) {
    return false;
  }
  const RoR::RendererOgreNextWindowMetrics *metrics =
      context->host->Metrics();
  if (metrics == nullptr) {
    return false;
  }
  if (metrics->generation != context->metrics_generation ||
      metrics->drawable_width != context->pixel_width ||
      metrics->drawable_height != context->pixel_height) {
    ++context->surface_revision;
  }
  context->metrics_generation = metrics->generation;
  context->pixel_width = metrics->drawable_width;
  context->pixel_height = metrics->drawable_height;
  ++context->calls;
  *acknowledged_surface = MakeSurface(
      context->window, *metrics, context->surface_revision);
  return true;
}

template <std::size_t DestinationCapacity, std::size_t SourceCapacity>
void CopyParameters(
    const std::array<RoR::RendererOgreNextWindowParameter, SourceCapacity>
        &source,
    std::size_t source_count,
    std::array<OgreNextN1PresentationParameter, DestinationCapacity>
        &destination,
    std::size_t &destination_count) {
  Require(source_count <= source.size() && source_count <= destination.size(),
          "window binding parameter count exceeds presentation capacity");
  destination_count = source_count;
  for (std::size_t index = 0U; index < source_count; ++index) {
    destination[index].name = source[index].name;
    destination[index].value = source[index].value;
  }
}

OgreNextN1PresentationConfiguration MakePresentationConfiguration(
    const RoR::RendererOgreNextWindowBinding &binding,
    const NativeWindowHandle &window, ShowContext &show_context) {
  Require(binding.valid, "SDL host binding is not valid");
  OgreNextN1PresentationConfiguration configuration;
  configuration.enabled = true;
  configuration.mode = OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP;
  configuration.shader_media_root =
      ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_ROOT;
  configuration.exact_window = window;
  CopyParameters(binding.renderer_options, binding.renderer_option_count,
                 configuration.renderer_options,
                 configuration.renderer_option_count);
  CopyParameters(binding.bootstrap_window_parameters,
                 binding.bootstrap_window_parameter_count,
                 configuration.bootstrap_window_parameters,
                 configuration.bootstrap_window_parameter_count);
  CopyParameters(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count,
                 configuration.presentation_window_parameters,
                 configuration.presentation_window_parameter_count);
  configuration.show_callback_context = &show_context;
  configuration.show_after_workspace_ready = &ShowAfterWorkspaceReady;
  return configuration;
}

RenderAssetId AssetId(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x52554E4C4F4F5041), low);
}

RenderAssetReference Asset(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, AssetId(low), 1U);
}

MeshResourceDescriptor MakeMesh() {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "production run-loop source triangle";
  mesh.index_format = MeshIndexFormat::UINT16;
  mesh.local_bounds.minimum = {-1.0F, -0.8F, 0.0F};
  mesh.local_bounds.maximum = {1.0F, 0.9F, 0.0F};
  mesh.positions = {{-1.0F, -0.8F, 0.0F},
                    {1.0F, -0.8F, 0.0F},
                    {0.0F, 0.9F, 0.0F}};
  mesh.normals.assign(mesh.positions.size(), Float3{0.0F, 0.0F, 1.0F});
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

RenderAssetDelta MakeCatalog() {
  RenderAssetDelta delta;
  delta.registry_id = kRegistryId;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  RenderAssetMutation mesh;
  mesh.asset = Asset(RenderAssetKind::MESH, 1U);
  mesh.payload = MakeMesh();
  delta.mutations.push_back(std::move(mesh));
  MaterialDescriptor material;
  material.debug_name = "production run-loop emissive PBS";
  material.base_color_factor = {0.04F, 0.28F, 0.92F, 1.0F};
  material.metallic_factor = 0.35F;
  material.roughness_factor = 0.22F;
  material.double_sided = true;
  material.emissive_factor = {0.8F, 0.12F, 0.03F};
  material.emissive_strength = 7.0F;
  RenderAssetMutation material_mutation;
  material_mutation.asset = Asset(RenderAssetKind::MATERIAL, 2U);
  material_mutation.payload = std::move(material);
  delta.mutations.push_back(std::move(material_mutation));
  return delta;
}

std::shared_ptr<const SceneSnapshot> MakeScene() {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 1U;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = 1U;
  descriptor.simulation_time_seconds = 1.0 / 48.0;
  descriptor.environment.ambient_radiance = {0.025F, 0.035F, 0.05F};
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = Asset(RenderAssetKind::MESH, 1U);
  instance.material = Asset(RenderAssetKind::MATERIAL, 2U);
  instance.local_bounds = MakeMesh().local_bounds;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  Require(static_cast<bool>(result),
          "could not create production run-loop scene: " +
              result.validation.field + ": " + result.validation.detail);
  return result.snapshot;
}

Matrix4x4 Projection() {
  constexpr float near_plane = 0.1F;
  constexpr float far_plane = 20.0F;
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.5F;
  projection.elements[10U] = far_plane / (near_plane - far_plane);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      near_plane * far_plane / (near_plane - far_plane);
  return projection;
}

RenderFrameRequest MakeFrame(
    const std::shared_ptr<const SceneSnapshot> &scene,
    std::uint64_t frame_id, std::uint32_t width, std::uint32_t height,
    std::uint64_t surface_revision) {
  RenderFrameRequest request;
  request.frame_id = frame_id;
  request.scene_snapshot = scene;
  request.present = true;
  request.presentation_view_id = kViewId;
  request.presentation_surface_revision = surface_revision;
  request.color_format = PixelFormat::RGBA8_SRGB;
  CameraViewRequest view;
  view.view_id = kViewId;
  view.width = width;
  view.height = height;
  view.near_plane = 0.1F;
  view.far_plane = 20.0F;
  view.view_from_render.elements[14U] = -3.0F;
  view.previous_view_from_render = view.view_from_render;
  view.clip_from_view = Projection();
  view.previous_clip_from_view = view.clip_from_view;
  request.views.push_back(view);
  return request;
}

FrontendInitializationRequest MakeInitialization(
    const RoR::RendererOgreNextWindowMetrics &metrics,
    const NativeWindowHandle &window) {
  FrontendInitializationRequest request;
  request.initial_surface_revision = 1U;
  request.window = window;
  request.initial_width = metrics.drawable_width;
  request.initial_height = metrics.drawable_height;
  request.initial_content_scale = ContentScale(metrics);
  request.maximum_frames_in_flight = 1U;
  request.headless = false;
  request.vertical_sync = false;
  return request;
}

void PushWindowEvent(void *opaque_window, SDL_WindowEventID event_id) {
  SDL_Window *window = static_cast<SDL_Window *>(opaque_window);
  const Uint32 window_id = SDL_GetWindowID(window);
  Require(window_id != 0U, "SDL window ID disappeared before event injection");
  SDL_Event event{};
  event.type = SDL_WINDOWEVENT;
  event.window.type = SDL_WINDOWEVENT;
  event.window.windowID = window_id;
  event.window.event = static_cast<Uint8>(event_id);
  if (event_id == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
    const int display = SDL_GetWindowDisplayIndex(window);
    event.window.data1 = display < 0 ? 0 : display;
  }
  Require(SDL_PushEvent(&event) == 1,
          "SDL rejected deterministic production window event");
}

void PollEvents(RoR::RendererOgreNextSdlWindowRuntime &adapter,
                void *sdl_window, EventTotals &totals,
                RoR::RendererOgreNextSdlWindowEventBatch *observed = nullptr) {
  RoR::RendererOgreNextSdlWindowEventBatch batch;
  Require(adapter.PollWindowEvents(sdl_window, batch),
          "production SDL event poll failed: " + adapter.LastError());
  ++totals.polls;
  totals.polled_events += batch.polled_events;
  totals.matched_window_events += batch.matched_window_events;
  totals.close_events += batch.close_events;
  totals.focus_gained_events += batch.focus_gained_events;
  totals.focus_lost_events += batch.focus_lost_events;
  totals.resize_events += batch.resize_events;
  totals.minimize_events += batch.minimize_events;
  totals.restore_events += batch.restore_events;
  totals.display_change_events += batch.display_change_events;
  totals.drawable_size_changes += batch.drawable_size_changed ? 1U : 0U;
  totals.close_requested = totals.close_requested || batch.close_requested;
  if (observed != nullptr) {
    *observed = batch;
  }
}

void SyncShowContext(ShowContext &context,
                     const RoR::RendererOgreNextWindowMetrics &metrics,
                     std::uint64_t surface_revision) {
  context.surface_revision = surface_revision;
  context.metrics_generation = metrics.generation;
  context.pixel_width = metrics.drawable_width;
  context.pixel_height = metrics.drawable_height;
}

std::uint64_t HashBytes(const std::vector<std::uint8_t> &bytes) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

std::string Hex(std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << value;
  return stream.str();
}

void WritePpm(const std::string &path, const FrameAttachment &attachment) {
  std::ofstream output(std::filesystem::u8path(path),
                       std::ios::binary | std::ios::trunc);
  Require(output.good(), "could not open production run-loop PPM");
  output << "P6\n" << attachment.width << ' ' << attachment.height
         << "\n255\n";
  for (std::uint32_t y = 0U; y < attachment.height; ++y) {
    const std::uint8_t *row = attachment.bytes.data() +
        static_cast<std::size_t>(y) * attachment.row_pitch_bytes;
    for (std::uint32_t x = 0U; x < attachment.width; ++x) {
      output.write(reinterpret_cast<const char *>(row + x * 4U), 3);
    }
  }
  Require(output.good(), "could not write production run-loop PPM");
}

void WriteText(const std::string &path, const std::string &text) {
  std::ofstream output(std::filesystem::u8path(path),
                       std::ios::binary | std::ios::trunc);
  Require(output.good(), "could not open production run-loop report");
  output << text;
  Require(output.good(), "could not write production run-loop report");
}

std::string Report(const OgreNextN1PresentationAudit &audit,
                   const EventTotals &events, std::uint64_t final_hash,
                   std::uint64_t final_surface_revision,
                   const ShowContext &show_context) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_n1_production_run_loop.v1\",\n"
         << "  \"ror_repository\": \""
         << ROR_OGRE_NEXT_N1_ROR_REPOSITORY << "\",\n"
         << "  \"ror_ref\": \"" << ROR_OGRE_NEXT_N1_ROR_REF << "\",\n"
         << "  \"ror_commit\": \"" << ROR_OGRE_NEXT_N1_ROR_COMMIT
         << "\",\n"
         << "  \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "  \"platform_policy\": \""
         << ROR_OGRE_NEXT_N1_PLATFORM_POLICY << "\",\n"
         << "  \"renderer\": \"" << ROR_OGRE_NEXT_N1_RENDERER_NAME
         << "\",\n"
         << "  \"required_presented_frames\": "
         << kRequiredPresentedFrames << ",\n"
         << "  \"presented_frames\": " << audit.presented_frames << ",\n"
         << "  \"first_presented_frame_id\": "
         << audit.first_presented_frame_id << ",\n"
         << "  \"last_presented_frame_id\": "
         << audit.last_presented_frame_id << ",\n"
         << "  \"monotonic_presented_frame_ids\": "
         << (audit.monotonic_presented_frame_ids ? "true" : "false")
         << ",\n"
         << "  \"show_callback_calls\": " << audit.show_callback_calls
         << ",\n"
         << "  \"source_target_creates\": "
         << audit.source_target_creates << ",\n"
         << "  \"source_target_destroys\": "
         << audit.source_target_destroys << ",\n"
         << "  \"compositor_node_definition_creates\": "
         << audit.compositor_node_definition_creates << ",\n"
         << "  \"compositor_node_definition_destroys\": "
         << audit.compositor_node_definition_destroys << ",\n"
         << "  \"compositor_workspace_creates\": "
         << audit.compositor_workspace_creates << ",\n"
         << "  \"compositor_workspace_destroys\": "
         << audit.compositor_workspace_destroys << ",\n"
         << "  \"compositor_workspace_rebinds\": "
         << audit.compositor_workspace_rebinds << ",\n"
         << "  \"surface_graph_rebuilds\": "
         << audit.surface_graph_rebuilds << ",\n"
         << "  \"suspended_surface_updates\": "
         << audit.suspended_surface_updates << ",\n"
         << "  \"restored_surface_updates\": "
         << audit.restored_surface_updates << ",\n"
         << "  \"source_scene_passes\": " << audit.source_scene_passes
         << ",\n"
         << "  \"presentation_quad_passes\": "
         << audit.presentation_quad_passes << ",\n"
         << "  \"render_one_frame_calls\": "
         << audit.render_one_frame_calls << ",\n"
         << "  \"window_swap_completions\": "
         << audit.window_swap_completions << ",\n"
         << "  \"source_readbacks\": " << audit.source_readbacks
         << ",\n"
         << "  \"cpu_window_copy\": "
         << (audit.cpu_window_copy ? "true" : "false") << ",\n"
         << "  \"ui_free_source\": "
         << (audit.ui_free_source ? "true" : "false") << ",\n"
         << "  \"gpu_quad_copy\": "
         << (audit.gpu_quad_copy ? "true" : "false") << ",\n"
         << "  \"event_polls\": " << events.polls << ",\n"
         << "  \"polled_events\": " << events.polled_events << ",\n"
         << "  \"matched_window_events\": "
         << events.matched_window_events << ",\n"
         << "  \"close_events\": " << events.close_events << ",\n"
         << "  \"focus_gained_events\": "
         << events.focus_gained_events << ",\n"
         << "  \"focus_lost_events\": "
         << events.focus_lost_events << ",\n"
         << "  \"resize_events\": " << events.resize_events << ",\n"
         << "  \"minimize_events\": " << events.minimize_events
         << ",\n"
         << "  \"restore_events\": " << events.restore_events << ",\n"
         << "  \"display_change_events\": "
         << events.display_change_events << ",\n"
         << "  \"drawable_size_changes\": "
         << events.drawable_size_changes << ",\n"
         << "  \"display_metric_refreshes\": "
         << events.display_metric_refreshes << ",\n"
         << "  \"close_requested_after_frame_budget\": "
         << (events.close_requested ? "true" : "false") << ",\n"
         << "  \"final_surface_revision\": " << final_surface_revision
         << ",\n"
         << "  \"show_context_calls\": " << show_context.calls << ",\n"
         << "  \"final_source_attachment_fnv1a64\": \""
         << Hex(final_hash) << "\"\n"
         << "}\n";
  return report.str();
}

} // namespace

int main(int argc, char **argv) {
  try {
#if defined(_WIN32)
    SDL_SetMainReady();
#endif
    const Arguments arguments = ParseArguments(argc, argv);
    const RenderAssetDelta catalog = MakeCatalog();
    const std::shared_ptr<const SceneSnapshot> scene = MakeScene();

    RoR::RendererOgreNextSdlWindowRuntime adapter;
    RoR::RendererOgreNextWindowRequest host_request;
    host_request.platform = HostPlatform();
    host_request.logical_width = 96U;
    host_request.logical_height = 64U;
    host_request.fsaa_samples = 0U;
#if !defined(__APPLE__)
    host_request.vertical_sync = false;
    host_request.vertical_sync_interval = 0U;
#endif
    RoR::RendererOgreNextWindowHost host;
    const RoR::RendererOgreNextWindowHostStatus host_initialized =
        host.Initialize(host_request, adapter.Runtime());
    Require(host_initialized ==
                RoR::RendererOgreNextWindowHostStatus::COMPLETED,
            std::string("production native window Initialize failed: ") +
                RoR::ToString(host_initialized) + ": " +
                adapter.LastError());
    const RoR::RendererOgreNextWindowBinding *binding = host.Binding();
    const RoR::RendererOgreNextSdlNativeWindow *native = host.NativeWindow();
    const RoR::RendererOgreNextWindowMetrics *metrics = host.Metrics();
    Require(binding != nullptr && native != nullptr && metrics != nullptr,
            "production native host did not publish binding and metrics");
    const NativeWindowHandle window = MakeWindowHandle(*native);
    void *const sdl_window = native->sdl_window;

    EventTotals events;
    PollEvents(adapter, sdl_window, events);

    ShowContext show_context;
    show_context.host = &host;
    show_context.window = window;
    SyncShowContext(show_context, *metrics, 1U);

    OgreNextN1Configuration frontend_configuration;
    frontend_configuration.shader_media_root = arguments.media_root;
    frontend_configuration.presentation = MakePresentationConfiguration(
        *binding, window, show_context);
    OgreNextN1Frontend frontend(std::move(frontend_configuration));
    RequireSuccess(frontend.Initialize(MakeInitialization(*metrics, window)),
                   "production presentation Initialize");
    RequireSuccess(frontend.SynchronizeAssets(catalog),
                   "production presentation SynchronizeAssets");

    std::uint64_t surface_revision = 1U;
    std::uint32_t width = metrics->drawable_width;
    std::uint32_t height = metrics->drawable_height;
    RenderFrameOutput final_output;
    for (std::uint64_t frame_id = 1U;
         frame_id <= kRequiredPresentedFrames; ++frame_id) {
      RenderFrameRequest request = MakeFrame(
          scene, frame_id, width, height, surface_revision);
      RenderFrameOutput output;
      RenderOperationResult rendered = frontend.Render(request, output);
      if (!rendered && frame_id == 1U &&
          rendered.code == RenderOperationCode::RESOURCE_STALE) {
        surface_revision = show_context.surface_revision;
        width = show_context.pixel_width;
        height = show_context.pixel_height;
        request = MakeFrame(scene, frame_id, width, height,
                            surface_revision);
        rendered = frontend.Render(request, output);
      }
      RequireSuccess(rendered, "production presented frame " +
                                   std::to_string(frame_id));
      Require(output.presented && output.frame_id == frame_id &&
                  output.presented_view_id == kViewId &&
                  output.attachments.size() == 1U &&
                  !output.attachments.front().bytes.empty(),
              "production frame output identity or source readback changed");
      final_output = std::move(output);

      if (frame_id == 250U) {
        PushWindowEvent(sdl_window, SDL_WINDOWEVENT_FOCUS_LOST);
        PushWindowEvent(sdl_window, SDL_WINDOWEVENT_FOCUS_GAINED);
        PollEvents(adapter, sdl_window, events);
      } else if (frame_id == 400U) {
        const RoR::RendererOgreNextWindowHostStatus resized =
            host.Resize(112U, 80U);
        Require(resized ==
                    RoR::RendererOgreNextWindowHostStatus::COMPLETED,
                std::string("production configure-ACK Resize failed: ") +
                    RoR::ToString(resized) + ": " + adapter.LastError());
        RoR::RendererOgreNextSdlWindowEventBatch resize_batch;
        PollEvents(adapter, sdl_window, events, &resize_batch);
        Require(resize_batch.resize_events > 0U &&
                    resize_batch.drawable_size_changed,
                "production resize was not acknowledged as a drawable-pixel change");
        metrics = host.Metrics();
        Require(metrics != nullptr,
                "production resized host metrics disappeared");
        ++surface_revision;
        width = metrics->drawable_width;
        height = metrics->drawable_height;
        SyncShowContext(show_context, *metrics, surface_revision);
        RequireSuccess(frontend.UpdateSurface(
                           MakeSurface(window, *metrics, surface_revision),
                           false, UINT64_C(5000000000)),
                       "production resized frontend UpdateSurface");
      } else if (frame_id == 650U) {
        PushWindowEvent(sdl_window, SDL_WINDOWEVENT_MINIMIZED);
        PollEvents(adapter, sdl_window, events);
        Require(host.Suspend() ==
                    RoR::RendererOgreNextWindowHostStatus::COMPLETED,
                "production host Suspend failed");
        metrics = host.Metrics();
        Require(metrics != nullptr,
                "production suspended host metrics disappeared");
        ++surface_revision;
        RequireSuccess(frontend.UpdateSurface(
                           MakeSurface(window, *metrics, surface_revision,
                                       true),
                           false, UINT64_C(5000000000)),
                       "production suspended frontend UpdateSurface");
        PushWindowEvent(sdl_window, SDL_WINDOWEVENT_RESTORED);
        Require(host.Resume() ==
                    RoR::RendererOgreNextWindowHostStatus::COMPLETED,
                "production host Resume failed");
        Require(host.RefreshMetrics() ==
                    RoR::RendererOgreNextWindowHostStatus::COMPLETED,
                "production restored host metric refresh failed");
        metrics = host.Metrics();
        Require(metrics != nullptr,
                "production restored host metrics disappeared");
        ++surface_revision;
        width = metrics->drawable_width;
        height = metrics->drawable_height;
        SyncShowContext(show_context, *metrics, surface_revision);
        RequireSuccess(frontend.UpdateSurface(
                           MakeSurface(window, *metrics, surface_revision),
                           false, UINT64_C(5000000000)),
                       "production restored frontend UpdateSurface");
        PollEvents(adapter, sdl_window, events);
      } else if (frame_id == 800U) {
        PushWindowEvent(sdl_window, SDL_WINDOWEVENT_DISPLAY_CHANGED);
        RoR::RendererOgreNextSdlWindowEventBatch display_batch;
        PollEvents(adapter, sdl_window, events, &display_batch);
        Require(display_batch.display_change_events > 0U,
                "production display-change event was not acknowledged");
        Require(host.RefreshMetrics() ==
                    RoR::RendererOgreNextWindowHostStatus::COMPLETED,
                "production Retina/display metric refresh failed");
        ++events.display_metric_refreshes;
        const RoR::RendererOgreNextWindowMetrics *display_metrics =
            host.Metrics();
        Require(display_metrics != nullptr,
                "production display metrics disappeared");
        if (display_metrics->drawable_width != width ||
            display_metrics->drawable_height != height) {
          ++surface_revision;
          width = display_metrics->drawable_width;
          height = display_metrics->drawable_height;
          SyncShowContext(show_context, *display_metrics, surface_revision);
          RequireSuccess(frontend.UpdateSurface(
                             MakeSurface(window, *display_metrics,
                                         surface_revision),
                             false, UINT64_C(5000000000)),
                         "production Retina frontend UpdateSurface");
        }
        metrics = display_metrics;
      }
    }

    PushWindowEvent(sdl_window, SDL_WINDOWEVENT_CLOSE);
    PollEvents(adapter, sdl_window, events);
    Require(events.close_requested && events.close_events > 0U &&
                events.focus_gained_events > 0U &&
                events.focus_lost_events > 0U &&
                events.resize_events > 0U &&
                events.minimize_events > 0U &&
                events.restore_events > 0U &&
                events.display_change_events > 0U &&
                events.drawable_size_changes > 0U &&
                events.display_metric_refreshes == 1U,
            "production event lineage is incomplete");

    Require(final_output.attachments.size() == 1U,
            "production final source attachment disappeared");
    const FrameAttachment final_attachment =
        final_output.attachments.front();
    const std::uint64_t final_hash = HashBytes(final_attachment.bytes);
    OgreNextN1PresentationAudit live_audit =
        frontend.QueryPresentationAudit();
    Require(live_audit.enabled &&
                live_audit.mode ==
                    OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP &&
                live_audit.exact_external_window_binding &&
                live_audit.exact_two_external_channels &&
                live_audit.ui_free_source && live_audit.gpu_quad_copy &&
                !live_audit.cpu_window_copy &&
                live_audit.workspace_ready_before_show &&
                live_audit.bounded_swap_completed &&
                live_audit.monotonic_presented_frame_ids &&
                live_audit.presented_frames == kRequiredPresentedFrames &&
                live_audit.first_presented_frame_id == 1U &&
                live_audit.last_presented_frame_id ==
                    kRequiredPresentedFrames &&
                live_audit.source_scene_passes == kRequiredPresentedFrames &&
                live_audit.presentation_quad_passes ==
                    kRequiredPresentedFrames &&
                live_audit.render_one_frame_calls ==
                    kRequiredPresentedFrames &&
                live_audit.window_final_target_updates ==
                    kRequiredPresentedFrames &&
                live_audit.window_swap_completions ==
                    kRequiredPresentedFrames &&
                live_audit.source_readbacks == kRequiredPresentedFrames &&
                live_audit.show_callback_calls == 1U &&
                show_context.calls == 1U &&
                live_audit.source_target_creates >= 3U &&
                live_audit.source_target_creates <
                    live_audit.presented_frames &&
                live_audit.source_target_destroys + 1U ==
                    live_audit.source_target_creates &&
                live_audit.compositor_node_definition_creates ==
                    live_audit.source_target_creates &&
                live_audit.compositor_node_definition_destroys + 1U ==
                    live_audit.compositor_node_definition_creates &&
                live_audit.compositor_workspace_creates >=
                    live_audit.source_target_creates &&
                live_audit.compositor_workspace_destroys + 1U ==
                    live_audit.compositor_workspace_creates &&
                live_audit.surface_graph_rebuilds >= 2U &&
                live_audit.suspended_surface_updates == 1U &&
                live_audit.restored_surface_updates == 1U,
            "production presentation live audit is incomplete");

    RenderOperationResult shutdown =
        frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
    if (!shutdown) {
      shutdown = frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
    }
    RequireSuccess(shutdown, "production presentation retryable Shutdown");
    const OgreNextN1PresentationAudit final_audit =
        frontend.QueryPresentationAudit();
    Require(final_audit.source_target_creates ==
                final_audit.source_target_destroys &&
                final_audit.compositor_node_definition_creates ==
                    final_audit.compositor_node_definition_destroys &&
                final_audit.compositor_workspace_creates ==
                    final_audit.compositor_workspace_destroys,
            "production persistent graph cleanup counts are unbalanced");

    WritePpm(arguments.output_path, final_attachment);
    const std::string report = Report(
        final_audit, events, final_hash, surface_revision, show_context);
    WriteText(arguments.report_path, report);

    RoR::RendererOgreNextWindowHostStatus host_shutdown = host.Shutdown();
    if (host_shutdown != RoR::RendererOgreNextWindowHostStatus::COMPLETED) {
      host_shutdown = host.Shutdown();
    }
    Require(host_shutdown ==
                RoR::RendererOgreNextWindowHostStatus::COMPLETED,
            "production native window host retryable Shutdown failed");
    std::cout << report;
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next production window run-loop smoke failed: "
              << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
