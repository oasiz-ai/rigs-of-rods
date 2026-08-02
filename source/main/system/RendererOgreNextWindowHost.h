/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral ownership contract for an SDL2 presentation window.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace RoR {

constexpr std::uint32_t kRendererOgreNextWindowHostContractVersion = 1U;
constexpr std::uint32_t kRendererOgreNextWindowHostSdlMajor = 2U;
constexpr std::uint32_t kRendererOgreNextWindowHostSdlMinor = 32U;
constexpr std::uint32_t kRendererOgreNextWindowHostSdlPatch = 10U;

enum class RendererOgreNextWindowPlatform : std::uint8_t {
  UNKNOWN = 0,
  MACOS_COCOA_METAL = 1,
  WINDOWS_WIN32 = 2,
  LINUX_X11_XCB = 3,
  LINUX_WAYLAND = 4,
};

enum class RendererOgreNextSdlVideoDriver : std::uint8_t {
  UNKNOWN = 0,
  COCOA = 1,
  WINDOWS = 2,
  X11 = 3,
  WAYLAND = 4,
};

enum class RendererOgreNextWindowBridge : std::uint8_t {
  NONE = 0,
  COCOA_OGRE_METAL_VIEW = 1,
  WIN32_EXTERNAL_HWND = 2,
  X11_XCB_SDL2_PAIR = 3,
};

enum class RendererOgreNextWindowLifecycle : std::uint8_t {
  NEW = 0,
  READY_HIDDEN = 1,
  ACTIVE = 2,
  SUSPENDED = 3,
  FAILED = 4,
  SHUTDOWN = 5,
};

enum class RendererOgreNextWindowHostStatus : std::uint8_t {
  COMPLETED = 0,
  REJECTED_INVALID_REQUEST = 1,
  REJECTED_INVALID_RUNTIME = 2,
  REJECTED_PLATFORM_MISMATCH = 3,
  REJECTED_WAYLAND_UNSUPPORTED = 4,
  REJECTED_MAIN_THREAD_REQUIRED = 5,
  FAILED_SDL_VIDEO_INITIALIZATION = 6,
  FAILED_SDL_WINDOW_CREATION = 7,
  FAILED_NATIVE_WINDOW_QUERY = 8,
  FAILED_METAL_VIEW_CREATION = 9,
  FAILED_WINDOW_VISIBILITY = 10,
  FAILED_WINDOW_RESIZE = 11,
  FAILED_SHUTDOWN = 12,
  FAILED_INTERNAL = 13,
};

enum RendererOgreNextWindowCreateFlag : std::uint32_t {
  ROR_OGRE_NEXT_WINDOW_HIDDEN = 1U << 0U,
  ROR_OGRE_NEXT_WINDOW_RESIZABLE = 1U << 1U,
  ROR_OGRE_NEXT_WINDOW_ALLOW_HIGHDPI = 1U << 2U,
  ROR_OGRE_NEXT_WINDOW_METAL = 1U << 3U,
  ROR_OGRE_NEXT_WINDOW_VULKAN = 1U << 4U,
};

struct RendererOgreNextWindowRequest {
  std::uint32_t version = kRendererOgreNextWindowHostContractVersion;
  RendererOgreNextWindowPlatform platform =
      RendererOgreNextWindowPlatform::UNKNOWN;
  std::uint32_t logical_width = 0U;
  std::uint32_t logical_height = 0U;
  std::uint32_t fsaa_samples = 0U;
  bool gamma = true;
  bool vertical_sync = true;
  std::uint32_t vertical_sync_interval = 1U;
  bool presents_with_transaction = false;
  std::uint32_t configure_ack_timeout_ms = 2000U;
};

/// Renderer-neutral description of the exact SDL_CreateWindow request.
/// The future SDL adapter maps these bits to SDL_WINDOW_* constants; no SDL
/// numeric ABI is exposed through this header.
struct RendererOgreNextSdlWindowCreateRequest {
  std::uint32_t version = kRendererOgreNextWindowHostContractVersion;
  std::uint32_t logical_width = 0U;
  std::uint32_t logical_height = 0U;
  std::uint32_t flags = 0U;
};

/// Process-local native handles normalized from SDL_SysWMinfo.
/// `native_render_view` is not supplied by SDL. On Cocoa the host creates and
/// owns an OgreMetalView child of `native_window`, then stores it here.
struct RendererOgreNextSdlNativeWindow {
  std::uint32_t version = kRendererOgreNextWindowHostContractVersion;
  RendererOgreNextWindowPlatform platform =
      RendererOgreNextWindowPlatform::UNKNOWN;
  RendererOgreNextSdlVideoDriver driver =
      RendererOgreNextSdlVideoDriver::UNKNOWN;
  void *sdl_window = nullptr;
  void *native_display = nullptr;
  std::uintptr_t native_window = 0U;
  void *native_render_view = nullptr;
  std::uint32_t drawable_width = 0U;
  std::uint32_t drawable_height = 0U;
};

/// Normalized presentation-surface dimensions. Logical window units are
/// intentionally separate from drawable pixels: Retina/HiDPI callers must use
/// the drawable extent for the Ogre surface and input/UI code may use logical
/// units. `generation` advances only after a complete, validated observation
/// changes either extent and never identifies an uncommitted resize.
struct RendererOgreNextWindowMetrics {
  std::uint32_t version = kRendererOgreNextWindowHostContractVersion;
  std::uint64_t generation = 0U;
  std::uint32_t logical_width = 0U;
  std::uint32_t logical_height = 0U;
  std::uint32_t drawable_width = 0U;
  std::uint32_t drawable_height = 0U;
  double content_scale_x = 0.0;
  double content_scale_y = 0.0;
};

/// Exact layout consumed synchronously by Ogre-Next's Vulkan XCB window path.
/// The address of this pair is serialized into `SDL2x11`; it therefore lives
/// inside the non-copyable host and remains valid until Shutdown begins.
struct RendererOgreNextX11WindowPair {
  void *display = nullptr;
  unsigned long window = 0UL;
};

struct RendererOgreNextWindowParameter {
  std::string name;
  std::string value;
};

struct RendererOgreNextWindowBinding {
  std::uint32_t version = kRendererOgreNextWindowHostContractVersion;
  bool valid = false;
  RendererOgreNextWindowBridge bridge =
      RendererOgreNextWindowBridge::NONE;
  std::array<RendererOgreNextWindowParameter, 2U> renderer_options{};
  std::size_t renderer_option_count = 0U;
  std::array<RendererOgreNextWindowParameter, 2U>
      bootstrap_window_parameters{};
  std::size_t bootstrap_window_parameter_count = 0U;
  std::array<RendererOgreNextWindowParameter, 8U>
      presentation_window_parameters{};
  std::size_t presentation_window_parameter_count = 0U;
  RendererOgreNextX11WindowPair x11_pair{};
};

/// The SDL/Objective-C adapter boundary. Callbacks are deliberately injected:
/// this contract can be tested on every host without constructing a native
/// window, while the eventual platform adapter remains the only SDL consumer.
/// Callback implementations may throw; the host catches every exception and
/// still attempts reverse-order cleanup.
struct RendererOgreNextWindowHostRuntime {
  std::uint32_t version = kRendererOgreNextWindowHostContractVersion;
  RendererOgreNextWindowPlatform compiled_platform =
      RendererOgreNextWindowPlatform::UNKNOWN;
  std::uint32_t sdl_major = 0U;
  std::uint32_t sdl_minor = 0U;
  std::uint32_t sdl_patch = 0U;
  void *context = nullptr;
  bool (*is_main_thread)(void *context) = nullptr;
  bool (*initialize_sdl_video)(void *context,
                               const char *required_driver) = nullptr;
  bool (*create_sdl_window)(
      void *context, const RendererOgreNextSdlWindowCreateRequest &request,
      void **sdl_window) = nullptr;
  bool (*query_sdl_native_window)(
      void *context, void *sdl_window,
      RendererOgreNextSdlNativeWindow *window) = nullptr;
  bool (*create_ogre_metal_view)(void *context, void *sdl_window,
                                 std::uintptr_t cocoa_window,
                                 void **metal_view) = nullptr;
  /// Requests visibility and returns only after the matching native SDL event
  /// and settled window flags are observed within `timeout_ms`.
  bool (*set_sdl_window_visible_and_wait_for_ack)(
      void *context, void *sdl_window, bool visible,
      std::uint32_t timeout_ms) = nullptr;
  /// Requests the logical size, pumps native events, and returns only after a
  /// matching Cocoa/WM_SIZE/X11 ConfigureNotify has been observed. An
  /// immediate post-SDL_SetWindowSize query is not an acknowledgement. The
  /// callback must honor the finite timeout and return the acknowledged
  /// drawable-pixel snapshot in `window`.
  bool (*resize_sdl_window_and_wait_for_configure)(
      void *context, void *sdl_window, std::uint32_t logical_width,
      std::uint32_t logical_height, std::uint32_t timeout_ms,
      RendererOgreNextSdlNativeWindow *window) = nullptr;
  bool (*destroy_ogre_metal_view)(void *context, void *metal_view) = nullptr;
  bool (*destroy_sdl_window)(void *context, void *sdl_window) = nullptr;
  bool (*shutdown_sdl_video)(void *context) = nullptr;
};

/// Owns one hidden SDL presentation window and, on macOS, its OgreMetalView.
/// It never creates an Ogre render window and never admits or packages the
/// Ogre-Next child. The binding is valid only while this object owns the
/// native resources and is intentionally returned by reference.
class RendererOgreNextWindowHost final {
public:
  RendererOgreNextWindowHost() = default;
  ~RendererOgreNextWindowHost();

  RendererOgreNextWindowHost(const RendererOgreNextWindowHost &) = delete;
  RendererOgreNextWindowHost &
  operator=(const RendererOgreNextWindowHost &) = delete;
  RendererOgreNextWindowHost(RendererOgreNextWindowHost &&) = delete;
  RendererOgreNextWindowHost &
  operator=(RendererOgreNextWindowHost &&) = delete;

  RendererOgreNextWindowHostStatus Initialize(
      const RendererOgreNextWindowRequest &request,
      const RendererOgreNextWindowHostRuntime &runtime) noexcept;
  RendererOgreNextWindowHostStatus Resume() noexcept;
  RendererOgreNextWindowHostStatus Suspend() noexcept;
  RendererOgreNextWindowHostStatus Resize(
      std::uint32_t logical_width,
      std::uint32_t logical_height) noexcept;
  /// Observes drawable-pixel changes without issuing a logical resize. Use for
  /// display/content-scale changes such as Retina migration at the same SDL
  /// logical extent.
  RendererOgreNextWindowHostStatus RefreshMetrics() noexcept;
  RendererOgreNextWindowHostStatus Shutdown() noexcept;

  RendererOgreNextWindowLifecycle Lifecycle() const noexcept {
    return m_lifecycle;
  }
  const RendererOgreNextWindowBinding *Binding() const noexcept;
  const RendererOgreNextSdlNativeWindow *NativeWindow() const noexcept;
  const RendererOgreNextWindowMetrics *Metrics() const noexcept;

private:
  RendererOgreNextWindowHostStatus Cleanup(
      RendererOgreNextWindowHostStatus success_status) noexcept;
  void FailClosedAfterLiveWindowFailure() noexcept;

  RendererOgreNextWindowLifecycle m_lifecycle =
      RendererOgreNextWindowLifecycle::NEW;
  RendererOgreNextWindowRequest m_request{};
  RendererOgreNextWindowHostRuntime m_runtime{};
  RendererOgreNextSdlNativeWindow m_native{};
  RendererOgreNextWindowMetrics m_metrics{};
  RendererOgreNextWindowBinding m_binding{};
  bool m_video_owned = false;
  bool m_window_owned = false;
  bool m_metal_view_owned = false;
};

bool IsKnownRendererOgreNextWindowPlatform(
    RendererOgreNextWindowPlatform platform) noexcept;
bool IsKnownRendererOgreNextSdlVideoDriver(
    RendererOgreNextSdlVideoDriver driver) noexcept;
bool IsKnownRendererOgreNextWindowBridge(
    RendererOgreNextWindowBridge bridge) noexcept;
bool IsKnownRendererOgreNextWindowLifecycle(
    RendererOgreNextWindowLifecycle lifecycle) noexcept;
bool IsKnownRendererOgreNextWindowHostStatus(
    RendererOgreNextWindowHostStatus status) noexcept;
const char *ToString(RendererOgreNextWindowHostStatus status) noexcept;

} // namespace RoR
