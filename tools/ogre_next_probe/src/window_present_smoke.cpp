/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Strict one-frame SDL/native-window + Ogre-Next presentation smoke.

#include "OgreNextN1Frontend.h"
#include "RendererOgreNextSdlWindowRuntime.h"
#include "RendererOgreNextWindowHost.h"
#include "ror_ogre_next_n1_config.h"

#include <algorithm>
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

constexpr std::uint64_t kRegistryId = UINT64_C(0x50524553454E5456);
constexpr std::uint64_t kViewId = 1U;
constexpr std::uint64_t kFrameId = 1U;

struct Arguments final {
  std::string media_root;
  std::string output_path;
  std::string report_path;
};

struct ShowContext final {
  RoR::RendererOgreNextWindowHost *host = nullptr;
  NativeWindowHandle window;
  std::uint64_t surface_revision = 0U;
  std::uint64_t metrics_generation = 0U;
  std::uint32_t pixel_width = 0U;
  std::uint32_t pixel_height = 0U;
  std::uint64_t calls = 0U;
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
      Fail("usage: ror_ogre_next_window_present_smoke --media-root "
           "ABSOLUTE_PATH --output FRAME.ppm --report REPORT.json");
    }
  }
  Require(!arguments.media_root.empty() && !arguments.output_path.empty() &&
              !arguments.report_path.empty(),
          "presentation smoke media, output, and report paths are required");
  Require(std::filesystem::path(arguments.media_root).is_absolute(),
          "presentation smoke media root must be absolute");
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
  *acknowledged_surface =
      MakeSurface(context->window, *metrics, context->surface_revision);
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
  return RenderAssetId::FromWords(UINT64_C(0x50524553454E5441), low);
}

RenderAssetReference Asset(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, AssetId(low), 1U);
}

MeshResourceDescriptor MakeMesh() {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "native presentation source triangle";
  mesh.index_format = MeshIndexFormat::UINT16;
  mesh.local_bounds.minimum = {-1.15F, -0.85F, 0.0F};
  mesh.local_bounds.maximum = {1.15F, 0.95F, 0.0F};
  mesh.positions = {{-1.15F, -0.85F, 0.0F},
                    {1.15F, -0.85F, 0.0F},
                    {0.0F, 0.95F, 0.0F}};
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
  material.debug_name = "native presentation emissive PBS";
  material.base_color_factor = {0.08F, 0.36F, 0.95F, 1.0F};
  material.metallic_factor = 0.2F;
  material.roughness_factor = 0.28F;
  material.double_sided = true;
  material.emissive_factor = {0.9F, 0.16F, 0.04F};
  material.emissive_strength = 8.0F;
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
  descriptor.environment.ambient_radiance = {0.03F, 0.04F, 0.055F};
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = Asset(RenderAssetKind::MESH, 1U);
  instance.material = Asset(RenderAssetKind::MATERIAL, 2U);
  instance.local_bounds = MakeMesh().local_bounds;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  Require(static_cast<bool>(result),
          "could not create native presentation scene: " +
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
    std::uint32_t width, std::uint32_t height, bool present,
    std::uint64_t surface_revision = 0U) {
  RenderFrameRequest request;
  request.frame_id = kFrameId;
  request.scene_snapshot = scene;
  request.present = present;
  request.presentation_view_id = present ? kViewId : 0U;
  request.presentation_surface_revision =
      present ? surface_revision : 0U;
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
    std::uint32_t width, std::uint32_t height, Float2 content_scale,
    bool headless, const NativeWindowHandle &window = {}) {
  FrontendInitializationRequest request;
  request.initial_surface_revision = 1U;
  request.window = window;
  request.initial_width = width;
  request.initial_height = height;
  request.initial_content_scale = content_scale;
  request.maximum_frames_in_flight = 1U;
  request.headless = headless;
  request.vertical_sync = false;
  return request;
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
  Require(output.good(), "could not open native presentation PPM");
  output << "P6\n" << attachment.width << ' ' << attachment.height
         << "\n255\n";
  for (std::uint32_t y = 0U; y < attachment.height; ++y) {
    const std::uint8_t *row = attachment.bytes.data() +
        static_cast<std::size_t>(y) * attachment.row_pitch_bytes;
    for (std::uint32_t x = 0U; x < attachment.width; ++x) {
      output.write(reinterpret_cast<const char *>(row + x * 4U), 3);
    }
  }
  Require(output.good(), "could not write native presentation PPM");
}

void WriteText(const std::string &path, const std::string &text) {
  std::ofstream output(std::filesystem::u8path(path),
                       std::ios::binary | std::ios::trunc);
  Require(output.good(), "could not open native presentation report");
  output << text;
  Require(output.good(), "could not write native presentation report");
}

void ShutdownFrontend(OgreNextN1Frontend &frontend) {
  RenderOperationResult result =
      frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
  if (!result) {
    result = frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
  }
  RequireSuccess(result, "presentation frontend retryable Shutdown");
}

std::string Report(const OgreNextN1PresentationAudit &audit,
                   std::uint64_t source_hash,
                   const ShowContext &show_context,
                   std::uint64_t resize_revision,
                   bool stale_retry_observed) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_n1_native_presentation.v1\",\n"
         << "  \"ror_repository\": \""
         << ROR_OGRE_NEXT_N1_ROR_REPOSITORY << "\",\n"
         << "  \"ror_ref\": \"" << ROR_OGRE_NEXT_N1_ROR_REF
         << "\",\n"
         << "  \"ror_commit\": \"" << ROR_OGRE_NEXT_N1_ROR_COMMIT
         << "\",\n"
         << "  \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "  \"platform_policy\": \""
         << ROR_OGRE_NEXT_N1_PLATFORM_POLICY << "\",\n"
         << "  \"renderer\": \"" << ROR_OGRE_NEXT_N1_RENDERER_NAME
         << "\",\n"
         << "  \"presentation_copy_lock_sha256\": \""
         << ROR_OGRE_NEXT_N1_PRESENTATION_COPY_LOCK_SHA256 << "\",\n"
         << "  \"presentation_media_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_SHA256 << "\",\n"
         << "  \"presentation_media_file_count\": "
         << ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_FILE_COUNT << ",\n"
         << "  \"source_attachment_fnv1a64\": \"" << Hex(source_hash)
         << "\",\n"
         << "  \"source_bytes_equal_headless\": true,\n"
         << "  \"resize_surface_revision\": " << resize_revision << ",\n"
         << "  \"post_show_surface_revision\": "
         << show_context.surface_revision << ",\n"
         << "  \"post_show_width\": " << show_context.pixel_width << ",\n"
         << "  \"post_show_height\": " << show_context.pixel_height
         << ",\n"
         << "  \"post_show_stale_retry_observed\": "
         << (stale_retry_observed ? "true" : "false") << ",\n"
         << "  \"show_ack_calls\": " << show_context.calls << ",\n"
         << "  \"exact_external_window_binding\": "
         << (audit.exact_external_window_binding ? "true" : "false")
         << ",\n"
         << "  \"exact_two_external_channels\": "
         << (audit.exact_two_external_channels ? "true" : "false")
         << ",\n"
         << "  \"ui_free_source\": "
         << (audit.ui_free_source ? "true" : "false") << ",\n"
         << "  \"gpu_quad_copy\": "
         << (audit.gpu_quad_copy ? "true" : "false") << ",\n"
         << "  \"cpu_window_copy\": "
         << (audit.cpu_window_copy ? "true" : "false") << ",\n"
         << "  \"workspace_ready_before_show\": "
         << (audit.workspace_ready_before_show ? "true" : "false")
         << ",\n"
         << "  \"bounded_swap_completed\": "
         << (audit.bounded_swap_completed ? "true" : "false") << ",\n"
         << "  \"window_moved_or_resized_calls\": "
         << audit.window_moved_or_resized_calls << ",\n"
         << "  \"source_scene_passes\": " << audit.source_scene_passes
         << ",\n"
         << "  \"presentation_quad_passes\": "
         << audit.presentation_quad_passes << ",\n"
         << "  \"render_one_frame_calls\": "
         << audit.render_one_frame_calls << ",\n"
         << "  \"window_final_target_updates\": "
         << audit.window_final_target_updates << ",\n"
         << "  \"window_swap_completions\": "
         << audit.window_swap_completions << ",\n"
         << "  \"presented_frames\": " << audit.presented_frames
         << ",\n"
         << "  \"source_readbacks\": " << audit.source_readbacks
         << "\n}\n";
  return report.str();
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    const RenderAssetDelta catalog = MakeCatalog();
    const std::shared_ptr<const SceneSnapshot> scene = MakeScene();
    constexpr std::uint32_t baseline_width = 672U;
    constexpr std::uint32_t baseline_height = 384U;

    RoR::RendererOgreNextSdlWindowRuntime adapter;
    RoR::RendererOgreNextWindowRequest host_request;
    host_request.platform = HostPlatform();
    host_request.logical_width = 640U;
    host_request.logical_height = 360U;
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
            std::string("strict native window Initialize failed: ") +
                RoR::ToString(host_initialized) + ": " + adapter.LastError());
    const RoR::RendererOgreNextWindowBinding *binding = host.Binding();
    const RoR::RendererOgreNextSdlNativeWindow *native = host.NativeWindow();
    const RoR::RendererOgreNextWindowMetrics *metrics = host.Metrics();
    Require(binding != nullptr && native != nullptr && metrics != nullptr,
            "native window host did not publish binding and metrics");
    const NativeWindowHandle window = MakeWindowHandle(*native);

    ShowContext show_context;
    show_context.host = &host;
    show_context.window = window;
    show_context.surface_revision = 1U;
    show_context.metrics_generation = metrics->generation;
    show_context.pixel_width = metrics->drawable_width;
    show_context.pixel_height = metrics->drawable_height;

    OgreNextN1Configuration presentation_configuration;
    presentation_configuration.shader_media_root = arguments.media_root;
    presentation_configuration.presentation =
        MakePresentationConfiguration(*binding, window, show_context);
    OgreNextN1Frontend frontend(std::move(presentation_configuration));
    RequireSuccess(frontend.Initialize(MakeInitialization(
                       metrics->drawable_width, metrics->drawable_height,
                       ContentScale(*metrics), false, window)),
                   "native presentation Initialize");
    RequireSuccess(frontend.SynchronizeAssets(catalog),
                   "native presentation SynchronizeAssets");

    const RoR::RendererOgreNextWindowHostStatus resized =
        host.Resize(baseline_width, baseline_height);
    Require(resized == RoR::RendererOgreNextWindowHostStatus::COMPLETED,
            std::string("native configure-ACK Resize failed: ") +
                RoR::ToString(resized) + ": " + adapter.LastError());
    metrics = host.Metrics();
    Require(metrics != nullptr, "resized host metrics disappeared");
    constexpr std::uint64_t resized_revision = 2U;
    show_context.surface_revision = resized_revision;
    show_context.metrics_generation = metrics->generation;
    show_context.pixel_width = metrics->drawable_width;
    show_context.pixel_height = metrics->drawable_height;
    RequireSuccess(frontend.UpdateSurface(
                       MakeSurface(window, *metrics, resized_revision), false,
                       UINT64_C(5000000000)),
                   "post-configure frontend UpdateSurface");

    RenderFrameRequest presented_request = MakeFrame(
        scene, metrics->drawable_width, metrics->drawable_height, true,
        resized_revision);
    RenderFrameOutput presented_output;
    RenderOperationResult presented =
        frontend.Render(presented_request, presented_output);
    bool stale_retry_observed = false;
    if (!presented && presented.code == RenderOperationCode::RESOURCE_STALE) {
      stale_retry_observed = true;
      presented_request = MakeFrame(
          scene, show_context.pixel_width, show_context.pixel_height, true,
          show_context.surface_revision);
      presented = frontend.Render(presented_request, presented_output);
    }
    RequireSuccess(presented, "one-frame native presentation Render");
    Require(presented_output.presented &&
                presented_output.presented_view_id == kViewId &&
                presented_output.attachments.size() == 1U,
            "presented output identity changed");
    const FrameAttachment &presented_attachment =
        presented_output.attachments.front();
    const std::vector<std::uint8_t> presented_bytes =
        presented_attachment.bytes;
    const std::uint64_t source_hash = HashBytes(presented_attachment.bytes);

    const OgreNextN1PresentationAudit audit =
        frontend.QueryPresentationAudit();
    Require(audit.enabled && audit.exact_external_window_binding &&
                audit.exact_two_external_channels && audit.ui_free_source &&
                audit.gpu_quad_copy && !audit.cpu_window_copy &&
                audit.workspace_ready_before_show &&
                audit.bounded_swap_completed &&
                audit.source_scene_passes == 1U &&
                audit.presentation_quad_passes == 1U &&
                audit.render_one_frame_calls == 1U &&
                audit.window_final_target_updates == 1U &&
                audit.window_swap_completions == 1U &&
                audit.presented_frames == 1U &&
                audit.source_readbacks == 1U &&
                audit.last_view_id == kViewId &&
                audit.last_surface_revision ==
                    show_context.surface_revision &&
                audit.last_width == show_context.pixel_width &&
                audit.last_height == show_context.pixel_height,
            "native presentation audit is incomplete or non-exact");

    Require(host.Suspend() ==
                RoR::RendererOgreNextWindowHostStatus::COMPLETED,
            "native presentation host Suspend failed");
    metrics = host.Metrics();
    Require(metrics != nullptr, "suspended host metrics disappeared");
    RequireSuccess(frontend.UpdateSurface(
                       MakeSurface(window, *metrics,
                                   show_context.surface_revision + 1U, true),
                       false, UINT64_C(5000000000)),
                   "suspended frontend UpdateSurface");
    ShutdownFrontend(frontend);

    OgreNextN1Configuration headless_configuration;
    headless_configuration.shader_media_root = arguments.media_root;
    OgreNextN1Frontend headless(std::move(headless_configuration));
    RequireSuccess(headless.Initialize(MakeInitialization(
                       presented_attachment.width,
                       presented_attachment.height, {1.0F, 1.0F}, true)),
                   "headless baseline Initialize");
    RequireSuccess(headless.SynchronizeAssets(catalog),
                   "headless baseline SynchronizeAssets");
    RenderFrameOutput headless_output;
    RequireSuccess(headless.Render(
                       MakeFrame(scene, presented_attachment.width,
                                 presented_attachment.height, false),
                       headless_output),
                   "headless baseline Render");
    Require(headless_output.attachments.size() == 1U &&
                headless_output.attachments.front().bytes == presented_bytes,
            "source-only attachment bytes changed when presentation was enabled");
    Require(HashBytes(headless_output.attachments.front().bytes) == source_hash,
            "source-only attachment hash changed when presentation was enabled");
    ShutdownFrontend(headless);

    WritePpm(arguments.output_path, presented_attachment);
    const std::string report =
        Report(audit, source_hash, show_context, resized_revision,
               stale_retry_observed);
    WriteText(arguments.report_path, report);

    RoR::RendererOgreNextWindowHostStatus host_shutdown = host.Shutdown();
    if (host_shutdown != RoR::RendererOgreNextWindowHostStatus::COMPLETED) {
      host_shutdown = host.Shutdown();
    }
    Require(host_shutdown ==
                RoR::RendererOgreNextWindowHostStatus::COMPLETED,
            "native window host retryable Shutdown failed");
    std::cout << report;
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next native presentation smoke failed: "
              << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
