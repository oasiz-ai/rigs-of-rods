/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextSdlWindowRuntime.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <atomic>
#include <cstring>
#include <cstddef>
#include <limits>
#include <type_traits>

#if defined(__APPLE__)
namespace RoR {
bool RendererOgreNextCocoaIsMainThread() noexcept;
bool RendererOgreNextCocoaCreateMetalView(
    std::uintptr_t cocoa_window, void **metal_view) noexcept;
bool RendererOgreNextCocoaDestroyMetalView(void *metal_view) noexcept;
} // namespace RoR
#endif

namespace RoR {
namespace {

#if defined(__linux__)
struct AuditedSdl2X11Pair {
  Display *display;
  ::Window window;
};
static_assert(std::is_standard_layout<RendererOgreNextX11WindowPair>::value,
              "SDL2x11 bridge must remain standard-layout");
static_assert(sizeof(RendererOgreNextX11WindowPair) ==
                  sizeof(AuditedSdl2X11Pair) &&
                  alignof(RendererOgreNextX11WindowPair) ==
                      alignof(AuditedSdl2X11Pair) &&
                  offsetof(RendererOgreNextX11WindowPair, display) ==
                      offsetof(AuditedSdl2X11Pair, display) &&
                  offsetof(RendererOgreNextX11WindowPair, window) ==
                      offsetof(AuditedSdl2X11Pair, window),
              "SDL2x11 pair must match Ogre's audited Display/Window ABI");
static_assert(
    std::is_same<
        decltype(static_cast<SDL_SysWMinfo *>(nullptr)->info.x11.display),
        Display *>::value &&
        std::is_same<
            decltype(static_cast<SDL_SysWMinfo *>(nullptr)->info.x11.window),
            ::Window>::value,
    "SDL_SysWMinfo X11 fields changed ABI");
#endif

RendererOgreNextWindowPlatform CompiledPlatform() noexcept {
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

std::uint32_t SdlFlags(std::uint32_t flags) noexcept {
  constexpr std::uint32_t known = ROR_OGRE_NEXT_WINDOW_HIDDEN |
                                  ROR_OGRE_NEXT_WINDOW_RESIZABLE |
                                  ROR_OGRE_NEXT_WINDOW_ALLOW_HIGHDPI |
                                  ROR_OGRE_NEXT_WINDOW_METAL |
                                  ROR_OGRE_NEXT_WINDOW_VULKAN;
  if ((flags & ~known) != 0U) {
    return 0U;
  }
  std::uint32_t result = 0U;
  if ((flags & ROR_OGRE_NEXT_WINDOW_HIDDEN) != 0U) {
    result |= SDL_WINDOW_HIDDEN;
  }
  if ((flags & ROR_OGRE_NEXT_WINDOW_RESIZABLE) != 0U) {
    result |= SDL_WINDOW_RESIZABLE;
  }
  if ((flags & ROR_OGRE_NEXT_WINDOW_ALLOW_HIGHDPI) != 0U) {
    result |= SDL_WINDOW_ALLOW_HIGHDPI;
  }
  if ((flags & ROR_OGRE_NEXT_WINDOW_METAL) != 0U) {
    result |= SDL_WINDOW_METAL;
  }
  if ((flags & ROR_OGRE_NEXT_WINDOW_VULKAN) != 0U) {
    result |= SDL_WINDOW_VULKAN;
  }
  return result;
}

enum class WindowWatchKind {
  CONFIGURE,
  VISIBILITY,
};

struct WindowEventWatch {
  WindowWatchKind kind = WindowWatchKind::CONFIGURE;
  Uint32 window_id = 0U;
  std::uint32_t logical_width = 0U;
  std::uint32_t logical_height = 0U;
  bool visible = false;
  std::atomic<bool> acknowledged{false};
};

int SDLCALL ObserveWindowEvent(void *userdata, SDL_Event *event) {
  if (userdata == nullptr || event == nullptr ||
      event->type != SDL_WINDOWEVENT) {
    return 0;
  }
  WindowEventWatch &watch = *static_cast<WindowEventWatch *>(userdata);
  if (event->window.windowID != watch.window_id) {
    return 0;
  }
  if (watch.kind == WindowWatchKind::CONFIGURE) {
    if ((event->window.event == SDL_WINDOWEVENT_RESIZED ||
         event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) &&
        event->window.data1 == static_cast<Sint32>(watch.logical_width) &&
        event->window.data2 == static_cast<Sint32>(watch.logical_height)) {
      watch.acknowledged.store(true, std::memory_order_release);
    }
  } else {
    const Uint8 expected = watch.visible ? SDL_WINDOWEVENT_SHOWN
                                         : SDL_WINDOWEVENT_HIDDEN;
    if (event->window.event == expected) {
      watch.acknowledged.store(true, std::memory_order_release);
    }
  }
  return 0;
}

class ScopedWindowEventWatch final {
public:
  explicit ScopedWindowEventWatch(WindowEventWatch &watch) : m_watch(watch) {
    SDL_AddEventWatch(&ObserveWindowEvent, &m_watch);
  }
  ~ScopedWindowEventWatch() {
    SDL_DelEventWatch(&ObserveWindowEvent, &m_watch);
  }
  ScopedWindowEventWatch(const ScopedWindowEventWatch &) = delete;
  ScopedWindowEventWatch &operator=(const ScopedWindowEventWatch &) = delete;

private:
  WindowEventWatch &m_watch;
};

bool HasSettledVisibility(SDL_Window *window, bool visible) noexcept {
  const std::uint32_t flags = SDL_GetWindowFlags(window);
  const bool hidden = (flags & SDL_WINDOW_HIDDEN) != 0U;
  const bool shown = (flags & SDL_WINDOW_SHOWN) != 0U;
  return visible ? shown && !hidden : hidden && !shown;
}

} // namespace

RendererOgreNextSdlWindowRuntime::~RendererOgreNextSdlWindowRuntime() {
  if (m_video_initialized) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    m_video_initialized = false;
    RestoreVideoDriverHint();
  }
}

RendererOgreNextWindowHostRuntime
RendererOgreNextSdlWindowRuntime::Runtime() noexcept {
  RendererOgreNextWindowHostRuntime runtime;
  runtime.compiled_platform = CompiledPlatform();
  runtime.sdl_major = SDL_MAJOR_VERSION;
  runtime.sdl_minor = SDL_MINOR_VERSION;
  runtime.sdl_patch = SDL_PATCHLEVEL;
  runtime.context = this;
#if defined(__APPLE__)
  runtime.is_main_thread = &IsMainThread;
  runtime.create_ogre_metal_view = &CreateMetalView;
  runtime.destroy_ogre_metal_view = &DestroyMetalView;
#endif
  runtime.initialize_sdl_video = &InitializeVideo;
  runtime.create_sdl_window = &CreateWindow;
  runtime.query_sdl_native_window = &QueryNativeWindow;
  runtime.set_sdl_window_visible_and_wait_for_ack =
      &SetWindowVisibleAndWaitForAck;
  runtime.resize_sdl_window_and_wait_for_configure =
      &ResizeWindowAndWaitForConfigure;
  runtime.destroy_sdl_window = &DestroyWindow;
  runtime.shutdown_sdl_video = &ShutdownVideo;
  return runtime;
}

bool RendererOgreNextSdlWindowRuntime::IsMainThread(void *) {
#if defined(__APPLE__)
  return RendererOgreNextCocoaIsMainThread();
#else
  return true;
#endif
}

bool RendererOgreNextSdlWindowRuntime::InitializeVideo(
    void *context, const char *required_driver) {
  RendererOgreNextSdlWindowRuntime &self =
      *static_cast<RendererOgreNextSdlWindowRuntime *>(context);
  self.m_last_error.clear();
  if (required_driver == nullptr || required_driver[0] == '\0' ||
      self.m_video_initialized ||
      SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0U) {
    self.m_last_error = "SDL video ownership is ambiguous";
    return false;
  }

  SDL_version linked;
  SDL_GetVersion(&linked);
  if (linked.major != kRendererOgreNextWindowHostSdlMajor ||
      linked.minor != kRendererOgreNextWindowHostSdlMinor ||
      linked.patch != kRendererOgreNextWindowHostSdlPatch) {
    self.m_last_error = "linked SDL version differs from 2.32.10";
    return false;
  }

  const char *previous = SDL_GetHint(SDL_HINT_VIDEODRIVER);
  self.m_had_previous_video_driver_hint = previous != nullptr;
  self.m_previous_video_driver_hint = previous == nullptr ? "" : previous;
  if (SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, required_driver,
                              SDL_HINT_OVERRIDE) == SDL_FALSE) {
    self.m_last_error = "SDL video-driver hint could not be forced";
    self.RestoreVideoDriverHint();
    return false;
  }
  if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    self.RecordSdlError("SDL_InitSubSystem");
    self.RestoreVideoDriverHint();
    return false;
  }
  const char *actual_driver = SDL_GetCurrentVideoDriver();
  if (actual_driver == nullptr ||
      std::strcmp(actual_driver, required_driver) != 0) {
    self.m_last_error = "SDL initialized an unexpected video driver";
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    self.RestoreVideoDriverHint();
    return false;
  }
  self.m_video_initialized = true;
  return true;
}

bool RendererOgreNextSdlWindowRuntime::CreateWindow(
    void *context, const RendererOgreNextSdlWindowCreateRequest &request,
    void **sdl_window) {
  RendererOgreNextSdlWindowRuntime &self =
      *static_cast<RendererOgreNextSdlWindowRuntime *>(context);
  if (sdl_window == nullptr ||
      request.version != kRendererOgreNextWindowHostContractVersion ||
      request.logical_width == 0U || request.logical_height == 0U ||
      request.logical_width >
          static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      request.logical_height >
          static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    self.m_last_error = "invalid SDL window request";
    return false;
  }
  *sdl_window = nullptr;
  const std::uint32_t flags = SdlFlags(request.flags);
  if (flags == 0U || (flags & SDL_WINDOW_HIDDEN) == 0U) {
    self.m_last_error = "SDL window flags are invalid or not hidden";
    return false;
  }
  SDL_Window *window = SDL_CreateWindow(
      "RoR Ogre-Next presentation probe", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, static_cast<int>(request.logical_width),
      static_cast<int>(request.logical_height), flags);
  if (window == nullptr) {
    self.RecordSdlError("SDL_CreateWindow");
    return false;
  }
  *sdl_window = window;
  return true;
}

bool RendererOgreNextSdlWindowRuntime::QueryNativeWindow(
    void *context, void *sdl_window,
    RendererOgreNextSdlNativeWindow *window) {
  RendererOgreNextSdlWindowRuntime &self =
      *static_cast<RendererOgreNextSdlWindowRuntime *>(context);
  if (sdl_window == nullptr || window == nullptr) {
    self.m_last_error = "invalid SDL native-window query";
    return false;
  }
  SDL_Window *typed_window = static_cast<SDL_Window *>(sdl_window);
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (SDL_GetWindowWMInfo(typed_window, &info) != SDL_TRUE) {
    self.RecordSdlError("SDL_GetWindowWMInfo");
    return false;
  }

  int drawable_width = 0;
  int drawable_height = 0;
  SDL_GetWindowSizeInPixels(typed_window, &drawable_width, &drawable_height);
  if (drawable_width <= 0 || drawable_height <= 0) {
    self.m_last_error = "SDL drawable-pixel extent is empty";
    return false;
  }

  *window = RendererOgreNextSdlNativeWindow{};
  window->platform = CompiledPlatform();
  window->sdl_window = typed_window;
  window->drawable_width = static_cast<std::uint32_t>(drawable_width);
  window->drawable_height = static_cast<std::uint32_t>(drawable_height);
#if defined(__APPLE__)
  if (info.subsystem != SDL_SYSWM_COCOA || info.info.cocoa.window == nullptr) {
    self.m_last_error = "SDL did not return a Cocoa NSWindow";
    return false;
  }
  window->driver = RendererOgreNextSdlVideoDriver::COCOA;
  window->native_window = reinterpret_cast<std::uintptr_t>(
      info.info.cocoa.window);
#elif defined(_WIN32)
  if (info.subsystem != SDL_SYSWM_WINDOWS || info.info.win.window == nullptr) {
    self.m_last_error = "SDL did not return a Win32 HWND";
    return false;
  }
  window->driver = RendererOgreNextSdlVideoDriver::WINDOWS;
  window->native_window =
      reinterpret_cast<std::uintptr_t>(info.info.win.window);
#elif defined(__linux__)
  if (info.subsystem != SDL_SYSWM_X11 || info.info.x11.display == nullptr ||
      info.info.x11.window == 0UL) {
    self.m_last_error = "SDL did not return an X11 Display/Window pair";
    return false;
  }
  window->driver = RendererOgreNextSdlVideoDriver::X11;
  window->native_display = info.info.x11.display;
  window->native_window =
      static_cast<std::uintptr_t>(info.info.x11.window);
#else
  self.m_last_error = "unsupported SDL native-window platform";
  return false;
#endif
  return true;
}

bool RendererOgreNextSdlWindowRuntime::CreateMetalView(
    void *context, void *, std::uintptr_t cocoa_window, void **metal_view) {
  RendererOgreNextSdlWindowRuntime &self =
      *static_cast<RendererOgreNextSdlWindowRuntime *>(context);
#if defined(__APPLE__)
  if (RendererOgreNextCocoaCreateMetalView(cocoa_window, metal_view)) {
    return true;
  }
  self.m_last_error = "OgreMetalView could not be attached to SDL NSWindow";
#else
  (void)cocoa_window;
  (void)metal_view;
  self.m_last_error = "OgreMetalView is unavailable on this platform";
#endif
  return false;
}

bool RendererOgreNextSdlWindowRuntime::SetWindowVisibleAndWaitForAck(
    void *context, void *sdl_window, bool visible,
    std::uint32_t timeout_ms) {
  RendererOgreNextSdlWindowRuntime &self =
      *static_cast<RendererOgreNextSdlWindowRuntime *>(context);
  if (sdl_window == nullptr || timeout_ms == 0U || timeout_ms > 10000U) {
    self.m_last_error = "invalid bounded SDL visibility request";
    return false;
  }
  SDL_Window *typed_window = static_cast<SDL_Window *>(sdl_window);
  if (HasSettledVisibility(typed_window, visible)) {
    return true;
  }
  const Uint32 window_id = SDL_GetWindowID(typed_window);
  if (window_id == 0U) {
    self.RecordSdlError("SDL_GetWindowID");
    return false;
  }
  WindowEventWatch watch;
  watch.kind = WindowWatchKind::VISIBILITY;
  watch.window_id = window_id;
  watch.visible = visible;
  ScopedWindowEventWatch scoped_watch(watch);
  if (visible) {
    SDL_ShowWindow(typed_window);
  } else {
    SDL_HideWindow(typed_window);
  }
  const Uint64 deadline = SDL_GetTicks64() + static_cast<Uint64>(timeout_ms);
  while (SDL_GetTicks64() <= deadline) {
    SDL_PumpEvents();
    if (watch.acknowledged.load(std::memory_order_acquire) &&
        HasSettledVisibility(typed_window, visible)) {
      return true;
    }
    SDL_Delay(1U);
  }
  self.m_last_error = "timed out awaiting native SDL visibility acknowledgement";
  return false;
}

bool RendererOgreNextSdlWindowRuntime::ResizeWindowAndWaitForConfigure(
    void *context, void *sdl_window, std::uint32_t logical_width,
    std::uint32_t logical_height, std::uint32_t timeout_ms,
    RendererOgreNextSdlNativeWindow *window) {
  RendererOgreNextSdlWindowRuntime &self =
      *static_cast<RendererOgreNextSdlWindowRuntime *>(context);
  if (sdl_window == nullptr || window == nullptr || logical_width == 0U ||
      logical_height == 0U || timeout_ms == 0U || timeout_ms > 10000U ||
      logical_width >
          static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      logical_height >
          static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    self.m_last_error = "invalid SDL configure-ack resize request";
    return false;
  }
  SDL_Window *typed_window = static_cast<SDL_Window *>(sdl_window);
  const Uint32 window_id = SDL_GetWindowID(typed_window);
  if (window_id == 0U) {
    self.RecordSdlError("SDL_GetWindowID");
    return false;
  }

  WindowEventWatch watch;
  watch.kind = WindowWatchKind::CONFIGURE;
  watch.window_id = window_id;
  watch.logical_width = logical_width;
  watch.logical_height = logical_height;
  ScopedWindowEventWatch scoped_watch(watch);
  SDL_SetWindowSize(typed_window, static_cast<int>(logical_width),
                    static_cast<int>(logical_height));
  const Uint64 deadline = SDL_GetTicks64() + static_cast<Uint64>(timeout_ms);
  while (SDL_GetTicks64() <= deadline) {
    SDL_PumpEvents();
    if (watch.acknowledged.load(std::memory_order_acquire)) {
      int observed_width = 0;
      int observed_height = 0;
      SDL_GetWindowSize(typed_window, &observed_width, &observed_height);
      if (observed_width == static_cast<int>(logical_width) &&
          observed_height == static_cast<int>(logical_height) &&
          QueryNativeWindow(context, sdl_window, window)) {
        return true;
      }
    }
    SDL_Delay(1U);
  }
  self.m_last_error = "timed out awaiting native SDL configure acknowledgement";
  return false;
}

bool RendererOgreNextSdlWindowRuntime::DestroyMetalView(
    void *context, void *metal_view) {
  RendererOgreNextSdlWindowRuntime &self =
      *static_cast<RendererOgreNextSdlWindowRuntime *>(context);
#if defined(__APPLE__)
  if (RendererOgreNextCocoaDestroyMetalView(metal_view)) {
    return true;
  }
  self.m_last_error = "OgreMetalView cleanup failed";
#else
  (void)metal_view;
  self.m_last_error = "unexpected non-Cocoa Metal view cleanup";
#endif
  return false;
}

bool RendererOgreNextSdlWindowRuntime::DestroyWindow(void *,
                                                     void *sdl_window) {
  if (sdl_window == nullptr) {
    return false;
  }
  SDL_DestroyWindow(static_cast<SDL_Window *>(sdl_window));
  return true;
}

bool RendererOgreNextSdlWindowRuntime::ShutdownVideo(void *context) {
  RendererOgreNextSdlWindowRuntime &self =
      *static_cast<RendererOgreNextSdlWindowRuntime *>(context);
  if (!self.m_video_initialized) {
    self.m_last_error = "SDL video shutdown has no owned initialization";
    return false;
  }
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  self.m_video_initialized = false;
  self.RestoreVideoDriverHint();
  return true;
}

void RendererOgreNextSdlWindowRuntime::RecordSdlError(const char *operation) {
  m_last_error = operation == nullptr ? "SDL operation failed" : operation;
  const char *error = SDL_GetError();
  if (error != nullptr && error[0] != '\0') {
    m_last_error += ": ";
    m_last_error += error;
  }
}

void RendererOgreNextSdlWindowRuntime::RestoreVideoDriverHint() noexcept {
  if (m_had_previous_video_driver_hint) {
    (void)SDL_SetHintWithPriority(
        SDL_HINT_VIDEODRIVER, m_previous_video_driver_hint.c_str(),
        SDL_HINT_OVERRIDE);
  } else {
    (void)SDL_ResetHint(SDL_HINT_VIDEODRIVER);
  }
  m_had_previous_video_driver_hint = false;
  m_previous_video_driver_hint.clear();
}

} // namespace RoR
