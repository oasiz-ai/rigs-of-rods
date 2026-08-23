/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextWindowHost.h"

#include <climits>
#include <limits>
#include <utility>

namespace RoR {
namespace {

constexpr std::uint32_t kMaximumWindowDimension = 32768U;

bool IsSupportedFsaa(std::uint32_t samples) noexcept {
  return samples == 0U || samples == 2U || samples == 4U || samples == 8U;
}

bool HasValidExtent(std::uint32_t width, std::uint32_t height) noexcept {
  return width > 0U && height > 0U && width <= kMaximumWindowDimension &&
         height <= kMaximumWindowDimension;
}

bool IsSupportedPresentationPlatform(
    RendererOgreNextWindowPlatform platform) noexcept {
  return platform == RendererOgreNextWindowPlatform::MACOS_COCOA_METAL ||
         platform == RendererOgreNextWindowPlatform::WINDOWS_WIN32 ||
         platform == RendererOgreNextWindowPlatform::LINUX_X11_XCB;
}

RendererOgreNextSdlVideoDriver ExpectedDriver(
    RendererOgreNextWindowPlatform platform) noexcept {
  switch (platform) {
  case RendererOgreNextWindowPlatform::MACOS_COCOA_METAL:
    return RendererOgreNextSdlVideoDriver::COCOA;
  case RendererOgreNextWindowPlatform::WINDOWS_WIN32:
    return RendererOgreNextSdlVideoDriver::WINDOWS;
  case RendererOgreNextWindowPlatform::LINUX_X11_XCB:
    return RendererOgreNextSdlVideoDriver::X11;
  case RendererOgreNextWindowPlatform::LINUX_WAYLAND:
    return RendererOgreNextSdlVideoDriver::WAYLAND;
  case RendererOgreNextWindowPlatform::UNKNOWN:
    return RendererOgreNextSdlVideoDriver::UNKNOWN;
  }
  return RendererOgreNextSdlVideoDriver::UNKNOWN;
}

const char *RequiredDriverName(
    RendererOgreNextWindowPlatform platform) noexcept {
  switch (platform) {
  case RendererOgreNextWindowPlatform::MACOS_COCOA_METAL:
    return "cocoa";
  case RendererOgreNextWindowPlatform::WINDOWS_WIN32:
    return "windows";
  case RendererOgreNextWindowPlatform::LINUX_X11_XCB:
    return "x11";
  case RendererOgreNextWindowPlatform::LINUX_WAYLAND:
    return "wayland";
  case RendererOgreNextWindowPlatform::UNKNOWN:
    return "";
  }
  return "";
}

std::uint32_t RequiredCreateFlags(
    RendererOgreNextWindowPlatform platform) noexcept {
  std::uint32_t flags = ROR_OGRE_NEXT_WINDOW_HIDDEN |
                        ROR_OGRE_NEXT_WINDOW_RESIZABLE |
                        ROR_OGRE_NEXT_WINDOW_ALLOW_HIGHDPI;
  if (platform == RendererOgreNextWindowPlatform::MACOS_COCOA_METAL) {
    flags |= ROR_OGRE_NEXT_WINDOW_METAL;
  } else if (platform ==
             RendererOgreNextWindowPlatform::LINUX_X11_XCB) {
    flags |= ROR_OGRE_NEXT_WINDOW_VULKAN;
  }
  return flags;
}

bool IsValidRequest(const RendererOgreNextWindowRequest &request) noexcept {
  if (request.version != kRendererOgreNextWindowHostContractVersion ||
      !IsSupportedPresentationPlatform(request.platform) ||
      !HasValidExtent(request.logical_width, request.logical_height) ||
      !IsSupportedFsaa(request.fsaa_samples) ||
      request.configure_ack_timeout_ms == 0U ||
      request.configure_ack_timeout_ms > 10000U) {
    return false;
  }

  // The pinned Metal render-window implementation does not expose a reliable
  // vsync control. Accept only the platform default until a native live gate
  // proves a controllable alternative.
  if (request.platform ==
      RendererOgreNextWindowPlatform::MACOS_COCOA_METAL) {
    return request.vertical_sync && request.vertical_sync_interval == 1U;
  }

  if (request.presents_with_transaction) {
    return false;
  }
  if (request.vertical_sync) {
    return request.vertical_sync_interval >= 1U &&
           request.vertical_sync_interval <= 4U;
  }
  return request.vertical_sync_interval == 0U;
}

bool IsValidRuntime(const RendererOgreNextWindowHostRuntime &runtime,
                    RendererOgreNextWindowPlatform platform) noexcept {
  if (runtime.version != kRendererOgreNextWindowHostContractVersion ||
      runtime.compiled_platform != platform ||
      runtime.sdl_major != kRendererOgreNextWindowHostSdlMajor ||
      runtime.sdl_minor != kRendererOgreNextWindowHostSdlMinor ||
      runtime.sdl_patch != kRendererOgreNextWindowHostSdlPatch ||
      runtime.context == nullptr ||
      runtime.claim_or_validate_owner_thread == nullptr ||
      runtime.initialize_sdl_video == nullptr ||
      runtime.create_sdl_window == nullptr ||
      runtime.query_sdl_native_window == nullptr ||
      runtime.set_sdl_window_visible_and_wait_for_ack == nullptr ||
      runtime.resize_sdl_window_and_wait_for_configure == nullptr ||
      runtime.destroy_sdl_window == nullptr ||
      runtime.shutdown_sdl_video == nullptr) {
    return false;
  }

  if (platform == RendererOgreNextWindowPlatform::MACOS_COCOA_METAL) {
    return runtime.is_main_thread != nullptr &&
           runtime.create_ogre_metal_view != nullptr &&
           runtime.destroy_ogre_metal_view != nullptr;
  }
  return true;
}

bool IsValidInitialNativeWindow(
    const RendererOgreNextSdlNativeWindow &window,
    const RendererOgreNextWindowRequest &request,
    void *owned_sdl_window) noexcept {
  if (window.version != kRendererOgreNextWindowHostContractVersion ||
      window.platform != request.platform ||
      window.driver != ExpectedDriver(request.platform) ||
      window.sdl_window == nullptr || window.sdl_window != owned_sdl_window ||
      window.native_window == 0U || window.native_render_view != nullptr ||
      !HasValidExtent(window.drawable_width, window.drawable_height)) {
    return false;
  }

  if (request.platform ==
      RendererOgreNextWindowPlatform::MACOS_COCOA_METAL) {
    return window.native_display == nullptr;
  }
  if (request.platform == RendererOgreNextWindowPlatform::WINDOWS_WIN32) {
    return window.native_display == nullptr;
  }
  if (request.platform ==
      RendererOgreNextWindowPlatform::LINUX_X11_XCB) {
    return window.native_display != nullptr &&
           window.native_window <=
               static_cast<std::uintptr_t>(
                   std::numeric_limits<unsigned long>::max());
  }
  return false;
}

bool IsSameNativeWindow(const RendererOgreNextSdlNativeWindow &candidate,
                        const RendererOgreNextSdlNativeWindow &current,
                        const RendererOgreNextWindowRequest &request) noexcept {
  return IsValidInitialNativeWindow(candidate, request, current.sdl_window) &&
         candidate.native_display == current.native_display &&
         candidate.native_window == current.native_window;
}

bool CommitMetrics(std::uint32_t logical_width,
                   std::uint32_t logical_height,
                   std::uint32_t drawable_width,
                   std::uint32_t drawable_height,
                   RendererOgreNextWindowMetrics &metrics) noexcept {
  if (!HasValidExtent(logical_width, logical_height) ||
      !HasValidExtent(drawable_width, drawable_height)) {
    return false;
  }
  const bool changed = metrics.generation == 0U ||
                       metrics.logical_width != logical_width ||
                       metrics.logical_height != logical_height ||
                       metrics.drawable_width != drawable_width ||
                       metrics.drawable_height != drawable_height;
  if (changed &&
      metrics.generation == std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  if (changed) {
    ++metrics.generation;
  }
  metrics.logical_width = logical_width;
  metrics.logical_height = logical_height;
  metrics.drawable_width = drawable_width;
  metrics.drawable_height = drawable_height;
  metrics.content_scale_x =
      static_cast<double>(drawable_width) / static_cast<double>(logical_width);
  metrics.content_scale_y = static_cast<double>(drawable_height) /
                            static_cast<double>(logical_height);
  return true;
}

std::string BoolString(bool value) { return value ? "true" : "false"; }

std::string PointerString(const void *pointer) {
  return std::to_string(
      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(pointer)));
}

std::string HandleString(std::uintptr_t handle) {
  return std::to_string(static_cast<unsigned long long>(handle));
}

template <std::size_t Size>
void AddParameter(std::array<RendererOgreNextWindowParameter, Size> &parameters,
                  std::size_t &count, const char *name,
                  const std::string &value) {
  if (count >= Size) {
    throw 1;
  }
  parameters[count].name = name;
  parameters[count].value = value;
  ++count;
}

bool BuildBinding(const RendererOgreNextWindowRequest &request,
                  const RendererOgreNextSdlNativeWindow &native,
                  RendererOgreNextWindowBinding &binding) {
  binding = RendererOgreNextWindowBinding{};
  const std::string fsaa = std::to_string(request.fsaa_samples);
  const std::string gamma = BoolString(request.gamma);

  if (request.platform ==
      RendererOgreNextWindowPlatform::MACOS_COCOA_METAL) {
    if (native.native_render_view == nullptr) {
      return false;
    }
    binding.bridge =
        RendererOgreNextWindowBridge::COCOA_OGRE_METAL_VIEW;
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count,
                 "externalWindowHandle",
                 PointerString(native.native_render_view));
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "gamma",
                 gamma);
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "FSAA",
                 fsaa);
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count,
                 "presentsWithTransaction",
                 BoolString(request.presents_with_transaction));
  } else if (request.platform ==
             RendererOgreNextWindowPlatform::WINDOWS_WIN32) {
    binding.bridge = RendererOgreNextWindowBridge::WIN32_EXTERNAL_HWND;
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count,
                 "externalWindowHandle", HandleString(native.native_window));
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "gamma",
                 gamma);
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "FSAA",
                 fsaa);
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "vsync",
                 BoolString(request.vertical_sync));
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count,
                 "vsyncInterval",
                 std::to_string(request.vertical_sync_interval));
  } else if (request.platform ==
             RendererOgreNextWindowPlatform::LINUX_X11_XCB) {
    binding.bridge = RendererOgreNextWindowBridge::X11_XCB_SDL2_PAIR;
    binding.x11_pair.display = native.native_display;
    binding.x11_pair.window =
        static_cast<unsigned long>(native.native_window);
    AddParameter(binding.renderer_options, binding.renderer_option_count,
                 "Interface", "xcb");
    AddParameter(binding.bootstrap_window_parameters,
                 binding.bootstrap_window_parameter_count, "windowType",
                 "null");
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "SDL2x11",
                 PointerString(&binding.x11_pair));
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "gamma",
                 gamma);
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "FSAA",
                 fsaa);
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count, "vsync",
                 BoolString(request.vertical_sync));
    AddParameter(binding.presentation_window_parameters,
                 binding.presentation_window_parameter_count,
                 "vsyncInterval",
                 std::to_string(request.vertical_sync_interval));
  } else {
    return false;
  }

  binding.valid = true;
  return true;
}

} // namespace

RendererOgreNextWindowHost::~RendererOgreNextWindowHost() {
  (void)Shutdown();
}

RendererOgreNextWindowHostStatus RendererOgreNextWindowHost::Initialize(
    const RendererOgreNextWindowRequest &request,
    const RendererOgreNextWindowHostRuntime &runtime) noexcept {
  if (m_lifecycle != RendererOgreNextWindowLifecycle::NEW) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (request.platform ==
      RendererOgreNextWindowPlatform::LINUX_WAYLAND) {
    return RendererOgreNextWindowHostStatus::REJECTED_WAYLAND_UNSUPPORTED;
  }
  if (!IsValidRequest(request)) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (runtime.compiled_platform != request.platform) {
    return RendererOgreNextWindowHostStatus::REJECTED_PLATFORM_MISMATCH;
  }
  if (!IsValidRuntime(runtime, request.platform)) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_RUNTIME;
  }

  try {
    if (request.platform ==
            RendererOgreNextWindowPlatform::MACOS_COCOA_METAL &&
        !runtime.is_main_thread(runtime.context)) {
      return RendererOgreNextWindowHostStatus::REJECTED_MAIN_THREAD_REQUIRED;
    }
  } catch (...) {
    return RendererOgreNextWindowHostStatus::REJECTED_MAIN_THREAD_REQUIRED;
  }
  try {
    if (!runtime.claim_or_validate_owner_thread(runtime.context)) {
      return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
    }
  } catch (...) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }

  m_request = request;
  m_runtime = runtime;
  m_owner_thread_claimed = true;

  try {
    if (!m_runtime.initialize_sdl_video(
            m_runtime.context, RequiredDriverName(request.platform))) {
      return Cleanup(RendererOgreNextWindowHostStatus::
                         FAILED_SDL_VIDEO_INITIALIZATION);
    }
    m_video_owned = true;

    RendererOgreNextSdlWindowCreateRequest create_request;
    create_request.logical_width = request.logical_width;
    create_request.logical_height = request.logical_height;
    create_request.flags = RequiredCreateFlags(request.platform);
    void *created_window = nullptr;
    const bool created = m_runtime.create_sdl_window(
        m_runtime.context, create_request, &created_window);
    if (created_window != nullptr) {
      m_native.sdl_window = created_window;
      m_window_owned = true;
    }
    if (!created || !m_window_owned) {
      return Cleanup(RendererOgreNextWindowHostStatus::
                         FAILED_SDL_WINDOW_CREATION);
    }

    RendererOgreNextSdlNativeWindow queried;
    if (!m_runtime.query_sdl_native_window(
            m_runtime.context, m_native.sdl_window, &queried) ||
        !IsValidInitialNativeWindow(queried, request,
                                    m_native.sdl_window)) {
      return Cleanup(RendererOgreNextWindowHostStatus::
                         FAILED_NATIVE_WINDOW_QUERY);
    }
    m_native = queried;

    if (request.platform ==
        RendererOgreNextWindowPlatform::MACOS_COCOA_METAL) {
      void *metal_view = nullptr;
      const bool created_view = m_runtime.create_ogre_metal_view(
          m_runtime.context, m_native.sdl_window, m_native.native_window,
          &metal_view);
      if (metal_view != nullptr) {
        m_native.native_render_view = metal_view;
        m_metal_view_owned = true;
      }
      if (!created_view || !m_metal_view_owned) {
        return Cleanup(RendererOgreNextWindowHostStatus::
                           FAILED_METAL_VIEW_CREATION);
      }
    }

    if (!CommitMetrics(request.logical_width, request.logical_height,
                       m_native.drawable_width, m_native.drawable_height,
                       m_metrics)) {
      return Cleanup(RendererOgreNextWindowHostStatus::FAILED_INTERNAL);
    }

    if (!BuildBinding(m_request, m_native, m_binding)) {
      return Cleanup(RendererOgreNextWindowHostStatus::FAILED_INTERNAL);
    }
    m_lifecycle = RendererOgreNextWindowLifecycle::READY_HIDDEN;
    return RendererOgreNextWindowHostStatus::COMPLETED;
  } catch (...) {
    return Cleanup(RendererOgreNextWindowHostStatus::FAILED_INTERNAL);
  }
}

RendererOgreNextWindowHostStatus RendererOgreNextWindowHost::Resume() noexcept {
  if (m_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE &&
      m_lifecycle != RendererOgreNextWindowLifecycle::READY_HIDDEN &&
      m_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (!IsOwnerThread()) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }
  if (m_lifecycle == RendererOgreNextWindowLifecycle::ACTIVE) {
    return RendererOgreNextWindowHostStatus::COMPLETED;
  }
  try {
    RendererOgreNextSdlNativeWindow candidate;
    if (!m_runtime.query_sdl_native_window(
            m_runtime.context, m_native.sdl_window, &candidate) ||
        !IsSameNativeWindow(candidate, m_native, m_request)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_NATIVE_WINDOW_QUERY;
    }
    if (!m_runtime.set_sdl_window_visible_and_wait_for_ack(
            m_runtime.context, m_native.sdl_window, true,
            m_request.configure_ack_timeout_ms)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_WINDOW_VISIBILITY;
    }
    RendererOgreNextSdlNativeWindow post_ack_candidate;
    if (!m_runtime.query_sdl_native_window(
            m_runtime.context, m_native.sdl_window, &post_ack_candidate) ||
        !IsSameNativeWindow(post_ack_candidate, m_native, m_request)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_NATIVE_WINDOW_QUERY;
    }
    if (!CommitMetrics(m_request.logical_width, m_request.logical_height,
                       post_ack_candidate.drawable_width,
                       post_ack_candidate.drawable_height, m_metrics)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
    }
    post_ack_candidate.native_render_view = m_native.native_render_view;
    m_native = post_ack_candidate;
    m_lifecycle = RendererOgreNextWindowLifecycle::ACTIVE;
    return RendererOgreNextWindowHostStatus::COMPLETED;
  } catch (...) {
    FailClosedAfterLiveWindowFailure();
    return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
  }
}

RendererOgreNextWindowHostStatus RendererOgreNextWindowHost::Suspend() noexcept {
  if (m_lifecycle != RendererOgreNextWindowLifecycle::READY_HIDDEN &&
      m_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED &&
      m_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (!IsOwnerThread()) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }
  if (m_lifecycle == RendererOgreNextWindowLifecycle::READY_HIDDEN ||
      m_lifecycle == RendererOgreNextWindowLifecycle::SUSPENDED) {
    m_lifecycle = RendererOgreNextWindowLifecycle::SUSPENDED;
    return RendererOgreNextWindowHostStatus::COMPLETED;
  }
  try {
    if (!m_runtime.set_sdl_window_visible_and_wait_for_ack(
            m_runtime.context, m_native.sdl_window, false,
            m_request.configure_ack_timeout_ms)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_WINDOW_VISIBILITY;
    }
    m_lifecycle = RendererOgreNextWindowLifecycle::SUSPENDED;
    return RendererOgreNextWindowHostStatus::COMPLETED;
  } catch (...) {
    FailClosedAfterLiveWindowFailure();
    return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
  }
}

RendererOgreNextWindowHostStatus
RendererOgreNextWindowHost::AdoptExternalVisibility(bool visible) noexcept {
  if (m_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE &&
      m_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (!IsOwnerThread()) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }
  if (!visible) {
    // The native system has already hidden or minimized the exact owned
    // window. Issuing another hide here can consume the corresponding restore
    // transition and strand the presentation window off screen.
    m_lifecycle = RendererOgreNextWindowLifecycle::SUSPENDED;
    return RendererOgreNextWindowHostStatus::COMPLETED;
  }

  const RendererOgreNextWindowHostStatus refreshed =
      RefreshMetricsOnOwnerThread();
  if (refreshed == RendererOgreNextWindowHostStatus::COMPLETED) {
    m_lifecycle = RendererOgreNextWindowLifecycle::ACTIVE;
  }
  return refreshed;
}

RendererOgreNextWindowHostStatus RendererOgreNextWindowHost::Resize(
    std::uint32_t logical_width, std::uint32_t logical_height) noexcept {
  if ((m_lifecycle != RendererOgreNextWindowLifecycle::READY_HIDDEN &&
       m_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE &&
       m_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED) ||
      !HasValidExtent(logical_width, logical_height)) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (!IsOwnerThread()) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }
  if (m_metrics.logical_width == logical_width &&
      m_metrics.logical_height == logical_height) {
    return RefreshMetricsOnOwnerThread();
  }
  try {
    RendererOgreNextSdlNativeWindow candidate;
    if (!m_runtime.resize_sdl_window_and_wait_for_configure(
            m_runtime.context, m_native.sdl_window, logical_width,
            logical_height, m_request.configure_ack_timeout_ms, &candidate)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_WINDOW_RESIZE;
    }
    if (!IsSameNativeWindow(candidate, m_native, m_request)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_NATIVE_WINDOW_QUERY;
    }
    candidate.native_render_view = m_native.native_render_view;
    if (!CommitMetrics(logical_width, logical_height,
                       candidate.drawable_width, candidate.drawable_height,
                       m_metrics)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
    }
    m_native = candidate;
    m_request.logical_width = logical_width;
    m_request.logical_height = logical_height;
    return RendererOgreNextWindowHostStatus::COMPLETED;
  } catch (...) {
    FailClosedAfterLiveWindowFailure();
    return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
  }
}

RendererOgreNextWindowHostStatus
RendererOgreNextWindowHost::ResizeToExactDrawable(
    std::uint32_t drawable_width, std::uint32_t drawable_height) noexcept {
  if ((m_lifecycle != RendererOgreNextWindowLifecycle::READY_HIDDEN &&
       m_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE &&
       m_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED) ||
      !HasValidExtent(drawable_width, drawable_height)) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (!IsOwnerThread()) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }
  if (m_metrics.drawable_width == drawable_width &&
      m_metrics.drawable_height == drawable_height) {
    return RendererOgreNextWindowHostStatus::COMPLETED;
  }

  const auto fit_logical_extent = [](std::uint32_t logical,
                                     std::uint32_t observed_drawable,
                                     std::uint32_t requested_drawable,
                                     std::uint32_t &fitted) noexcept {
    if (!HasValidExtent(logical, 1U) ||
        !HasValidExtent(observed_drawable, 1U) ||
        !HasValidExtent(requested_drawable, 1U)) {
      return false;
    }
    const std::uint64_t numerator =
        static_cast<std::uint64_t>(logical) * requested_drawable +
        observed_drawable / 2U;
    const std::uint64_t rounded = numerator / observed_drawable;
    if (rounded == 0U || rounded > kMaximumWindowDimension) {
      return false;
    }
    fitted = static_cast<std::uint32_t>(rounded);
    return true;
  };

  std::uint32_t logical_width = 0U;
  std::uint32_t logical_height = 0U;
  if (!fit_logical_extent(m_metrics.logical_width,
                          m_metrics.drawable_width, drawable_width,
                          logical_width) ||
      !fit_logical_extent(m_metrics.logical_height,
                          m_metrics.drawable_height, drawable_height,
                          logical_height)) {
    FailClosedAfterLiveWindowFailure();
    return RendererOgreNextWindowHostStatus::FAILED_WINDOW_RESIZE;
  }

  const RendererOgreNextWindowHostStatus resized =
      Resize(logical_width, logical_height);
  if (resized != RendererOgreNextWindowHostStatus::COMPLETED) {
    return resized;
  }
  if (m_metrics.drawable_width != drawable_width ||
      m_metrics.drawable_height != drawable_height) {
    FailClosedAfterLiveWindowFailure();
    return RendererOgreNextWindowHostStatus::FAILED_WINDOW_RESIZE;
  }
  return RendererOgreNextWindowHostStatus::COMPLETED;
}

RendererOgreNextWindowHostStatus
RendererOgreNextWindowHost::AdoptExternalResize(
    std::uint32_t logical_width, std::uint32_t logical_height) noexcept {
  if ((m_lifecycle != RendererOgreNextWindowLifecycle::READY_HIDDEN &&
       m_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE &&
       m_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED) ||
      !HasValidExtent(logical_width, logical_height)) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (!IsOwnerThread()) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }
  try {
    RendererOgreNextSdlNativeWindow candidate;
    if (!m_runtime.query_sdl_native_window(
            m_runtime.context, m_native.sdl_window, &candidate) ||
        !IsSameNativeWindow(candidate, m_native, m_request)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_NATIVE_WINDOW_QUERY;
    }
    candidate.native_render_view = m_native.native_render_view;
    if (!CommitMetrics(logical_width, logical_height,
                       candidate.drawable_width, candidate.drawable_height,
                       m_metrics)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
    }
    m_native = candidate;
    m_request.logical_width = logical_width;
    m_request.logical_height = logical_height;
    return RendererOgreNextWindowHostStatus::COMPLETED;
  } catch (...) {
    FailClosedAfterLiveWindowFailure();
    return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
  }
}

RendererOgreNextWindowHostStatus
RendererOgreNextWindowHost::RefreshMetrics() noexcept {
  if (m_lifecycle != RendererOgreNextWindowLifecycle::READY_HIDDEN &&
      m_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE &&
      m_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED) {
    return RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST;
  }
  if (!IsOwnerThread()) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }
  return RefreshMetricsOnOwnerThread();
}

RendererOgreNextWindowHostStatus
RendererOgreNextWindowHost::RefreshMetricsOnOwnerThread() noexcept {
  try {
    RendererOgreNextSdlNativeWindow candidate;
    if (!m_runtime.query_sdl_native_window(
            m_runtime.context, m_native.sdl_window, &candidate) ||
        !IsSameNativeWindow(candidate, m_native, m_request)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_NATIVE_WINDOW_QUERY;
    }
    if (!CommitMetrics(m_request.logical_width, m_request.logical_height,
                       candidate.drawable_width, candidate.drawable_height,
                       m_metrics)) {
      FailClosedAfterLiveWindowFailure();
      return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
    }
    candidate.native_render_view = m_native.native_render_view;
    m_native = candidate;
    return RendererOgreNextWindowHostStatus::COMPLETED;
  } catch (...) {
    FailClosedAfterLiveWindowFailure();
    return RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
  }
}

RendererOgreNextWindowHostStatus RendererOgreNextWindowHost::Shutdown() noexcept {
  return Cleanup(RendererOgreNextWindowHostStatus::COMPLETED);
}

const RendererOgreNextWindowBinding *
RendererOgreNextWindowHost::Binding() const noexcept {
  if (!m_binding.valid ||
      (m_lifecycle != RendererOgreNextWindowLifecycle::READY_HIDDEN &&
       m_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE &&
       m_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED)) {
    return nullptr;
  }
  return &m_binding;
}

const RendererOgreNextSdlNativeWindow *
RendererOgreNextWindowHost::NativeWindow() const noexcept {
  return Binding() == nullptr ? nullptr : &m_native;
}

const RendererOgreNextWindowMetrics *
RendererOgreNextWindowHost::Metrics() const noexcept {
  return Binding() == nullptr ? nullptr : &m_metrics;
}

RendererOgreNextWindowHostStatus RendererOgreNextWindowHost::Cleanup(
    RendererOgreNextWindowHostStatus success_status) noexcept {
  if (HasLiveOwnership() && !IsOwnerThread()) {
    return RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED;
  }
  m_binding.valid = false;

  if (m_metal_view_owned) {
    bool destroyed = false;
    try {
      destroyed = m_runtime.destroy_ogre_metal_view(
          m_runtime.context, m_native.native_render_view);
    } catch (...) {
      destroyed = false;
    }
    if (!destroyed) {
      m_lifecycle = RendererOgreNextWindowLifecycle::FAILED;
      return RendererOgreNextWindowHostStatus::FAILED_SHUTDOWN;
    }
    m_metal_view_owned = false;
    m_native.native_render_view = nullptr;
  }
  if (m_window_owned) {
    bool destroyed = false;
    try {
      destroyed = m_runtime.destroy_sdl_window(
          m_runtime.context, m_native.sdl_window);
    } catch (...) {
      destroyed = false;
    }
    if (!destroyed) {
      m_lifecycle = RendererOgreNextWindowLifecycle::FAILED;
      return RendererOgreNextWindowHostStatus::FAILED_SHUTDOWN;
    }
    m_window_owned = false;
    m_native = RendererOgreNextSdlNativeWindow{};
  }
  if (m_video_owned) {
    bool shutdown = false;
    try {
      shutdown = m_runtime.shutdown_sdl_video(m_runtime.context);
    } catch (...) {
      shutdown = false;
    }
    if (!shutdown) {
      m_lifecycle = RendererOgreNextWindowLifecycle::FAILED;
      return RendererOgreNextWindowHostStatus::FAILED_SHUTDOWN;
    }
    m_video_owned = false;
  }

  m_request = RendererOgreNextWindowRequest{};
  m_runtime = RendererOgreNextWindowHostRuntime{};
  m_native = RendererOgreNextSdlNativeWindow{};
  m_metrics = RendererOgreNextWindowMetrics{};
  m_binding = RendererOgreNextWindowBinding{};
  m_owner_thread_claimed = false;
  m_lifecycle = RendererOgreNextWindowLifecycle::SHUTDOWN;
  return success_status;
}

bool RendererOgreNextWindowHost::IsOwnerThread() noexcept {
  if (!m_owner_thread_claimed ||
      m_runtime.claim_or_validate_owner_thread == nullptr) {
    return false;
  }
  try {
    if (m_request.platform ==
            RendererOgreNextWindowPlatform::MACOS_COCOA_METAL &&
        (m_runtime.is_main_thread == nullptr ||
         !m_runtime.is_main_thread(m_runtime.context))) {
      return false;
    }
    return m_runtime.claim_or_validate_owner_thread(m_runtime.context);
  } catch (...) {
    return false;
  }
}

bool RendererOgreNextWindowHost::HasLiveOwnership() const noexcept {
  return m_video_owned || m_window_owned || m_metal_view_owned;
}

void RendererOgreNextWindowHost::FailClosedAfterLiveWindowFailure() noexcept {
  m_binding.valid = false;
  if (m_window_owned &&
      m_runtime.set_sdl_window_visible_and_wait_for_ack != nullptr) {
    try {
      (void)m_runtime.set_sdl_window_visible_and_wait_for_ack(
          m_runtime.context, m_native.sdl_window, false,
          m_request.configure_ack_timeout_ms);
    } catch (...) {
    }
  }
  m_lifecycle = RendererOgreNextWindowLifecycle::FAILED;
}

bool IsKnownRendererOgreNextWindowPlatform(
    RendererOgreNextWindowPlatform platform) noexcept {
  switch (platform) {
  case RendererOgreNextWindowPlatform::UNKNOWN:
  case RendererOgreNextWindowPlatform::MACOS_COCOA_METAL:
  case RendererOgreNextWindowPlatform::WINDOWS_WIN32:
  case RendererOgreNextWindowPlatform::LINUX_X11_XCB:
  case RendererOgreNextWindowPlatform::LINUX_WAYLAND:
    return true;
  }
  return false;
}

bool IsKnownRendererOgreNextSdlVideoDriver(
    RendererOgreNextSdlVideoDriver driver) noexcept {
  switch (driver) {
  case RendererOgreNextSdlVideoDriver::UNKNOWN:
  case RendererOgreNextSdlVideoDriver::COCOA:
  case RendererOgreNextSdlVideoDriver::WINDOWS:
  case RendererOgreNextSdlVideoDriver::X11:
  case RendererOgreNextSdlVideoDriver::WAYLAND:
    return true;
  }
  return false;
}

bool IsKnownRendererOgreNextWindowBridge(
    RendererOgreNextWindowBridge bridge) noexcept {
  switch (bridge) {
  case RendererOgreNextWindowBridge::NONE:
  case RendererOgreNextWindowBridge::COCOA_OGRE_METAL_VIEW:
  case RendererOgreNextWindowBridge::WIN32_EXTERNAL_HWND:
  case RendererOgreNextWindowBridge::X11_XCB_SDL2_PAIR:
    return true;
  }
  return false;
}

bool IsKnownRendererOgreNextWindowLifecycle(
    RendererOgreNextWindowLifecycle lifecycle) noexcept {
  switch (lifecycle) {
  case RendererOgreNextWindowLifecycle::NEW:
  case RendererOgreNextWindowLifecycle::READY_HIDDEN:
  case RendererOgreNextWindowLifecycle::ACTIVE:
  case RendererOgreNextWindowLifecycle::SUSPENDED:
  case RendererOgreNextWindowLifecycle::FAILED:
  case RendererOgreNextWindowLifecycle::SHUTDOWN:
    return true;
  }
  return false;
}

bool IsKnownRendererOgreNextWindowHostStatus(
    RendererOgreNextWindowHostStatus status) noexcept {
  switch (status) {
  case RendererOgreNextWindowHostStatus::COMPLETED:
  case RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST:
  case RendererOgreNextWindowHostStatus::REJECTED_INVALID_RUNTIME:
  case RendererOgreNextWindowHostStatus::REJECTED_PLATFORM_MISMATCH:
  case RendererOgreNextWindowHostStatus::REJECTED_WAYLAND_UNSUPPORTED:
  case RendererOgreNextWindowHostStatus::REJECTED_MAIN_THREAD_REQUIRED:
  case RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED:
  case RendererOgreNextWindowHostStatus::FAILED_SDL_VIDEO_INITIALIZATION:
  case RendererOgreNextWindowHostStatus::FAILED_SDL_WINDOW_CREATION:
  case RendererOgreNextWindowHostStatus::FAILED_NATIVE_WINDOW_QUERY:
  case RendererOgreNextWindowHostStatus::FAILED_METAL_VIEW_CREATION:
  case RendererOgreNextWindowHostStatus::FAILED_WINDOW_VISIBILITY:
  case RendererOgreNextWindowHostStatus::FAILED_WINDOW_RESIZE:
  case RendererOgreNextWindowHostStatus::FAILED_SHUTDOWN:
  case RendererOgreNextWindowHostStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RendererOgreNextWindowHostStatus status) noexcept {
  switch (status) {
  case RendererOgreNextWindowHostStatus::COMPLETED:
    return "completed";
  case RendererOgreNextWindowHostStatus::REJECTED_INVALID_REQUEST:
    return "rejected-invalid-request";
  case RendererOgreNextWindowHostStatus::REJECTED_INVALID_RUNTIME:
    return "rejected-invalid-runtime";
  case RendererOgreNextWindowHostStatus::REJECTED_PLATFORM_MISMATCH:
    return "rejected-platform-mismatch";
  case RendererOgreNextWindowHostStatus::REJECTED_WAYLAND_UNSUPPORTED:
    return "rejected-wayland-unsupported";
  case RendererOgreNextWindowHostStatus::REJECTED_MAIN_THREAD_REQUIRED:
    return "rejected-main-thread-required";
  case RendererOgreNextWindowHostStatus::REJECTED_OWNER_THREAD_REQUIRED:
    return "rejected-owner-thread-required";
  case RendererOgreNextWindowHostStatus::FAILED_SDL_VIDEO_INITIALIZATION:
    return "failed-sdl-video-initialization";
  case RendererOgreNextWindowHostStatus::FAILED_SDL_WINDOW_CREATION:
    return "failed-sdl-window-creation";
  case RendererOgreNextWindowHostStatus::FAILED_NATIVE_WINDOW_QUERY:
    return "failed-native-window-query";
  case RendererOgreNextWindowHostStatus::FAILED_METAL_VIEW_CREATION:
    return "failed-metal-view-creation";
  case RendererOgreNextWindowHostStatus::FAILED_WINDOW_VISIBILITY:
    return "failed-window-visibility";
  case RendererOgreNextWindowHostStatus::FAILED_WINDOW_RESIZE:
    return "failed-window-resize";
  case RendererOgreNextWindowHostStatus::FAILED_SHUTDOWN:
    return "failed-shutdown";
  case RendererOgreNextWindowHostStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR
