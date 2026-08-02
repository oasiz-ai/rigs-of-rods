/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Pinned SDL 2.32.10 adapter for RendererOgreNextWindowHost.

#pragma once

#include "RendererOgreNextWindowHost.h"

#include <cstdint>
#include <string>
#include <thread>

namespace RoR {

/// Owns only SDL's process-global video initialization bookkeeping. The
/// RendererOgreNextWindowHost owns every SDL_Window and platform child view.
/// This adapter is probe-only until a live Ogre presentation/swap/readback
/// acceptance gate and package review admit it. The first host validation
/// claims the current thread; every SDL/AppKit callback must remain on that
/// thread, and Cocoa also requires the AppKit main thread. The host must finish
/// explicit owner-thread Shutdown before this adapter is destroyed.
class RendererOgreNextSdlWindowRuntime final {
public:
  RendererOgreNextSdlWindowRuntime() = default;
  ~RendererOgreNextSdlWindowRuntime();

  RendererOgreNextSdlWindowRuntime(
      const RendererOgreNextSdlWindowRuntime &) = delete;
  RendererOgreNextSdlWindowRuntime &
  operator=(const RendererOgreNextSdlWindowRuntime &) = delete;
  RendererOgreNextSdlWindowRuntime(
      RendererOgreNextSdlWindowRuntime &&) = delete;
  RendererOgreNextSdlWindowRuntime &
  operator=(RendererOgreNextSdlWindowRuntime &&) = delete;

  RendererOgreNextWindowHostRuntime Runtime() noexcept;
  const std::string &LastError() const noexcept { return m_last_error; }

private:
  static bool ClaimOrValidateOwnerThread(void *context);
  static bool IsMainThread(void *context);
  static bool InitializeVideo(void *context, const char *required_driver);
  static bool CreateWindow(
      void *context, const RendererOgreNextSdlWindowCreateRequest &request,
      void **sdl_window);
  static bool QueryNativeWindow(
      void *context, void *sdl_window,
      RendererOgreNextSdlNativeWindow *window);
  static bool CreateMetalView(void *context, void *sdl_window,
                              std::uintptr_t cocoa_window,
                              void **metal_view);
  static bool SetWindowVisibleAndWaitForAck(
      void *context, void *sdl_window, bool visible,
      std::uint32_t timeout_ms);
  static bool ResizeWindowAndWaitForConfigure(
      void *context, void *sdl_window, std::uint32_t logical_width,
      std::uint32_t logical_height, std::uint32_t timeout_ms,
      RendererOgreNextSdlNativeWindow *window);
  static bool DestroyMetalView(void *context, void *metal_view);
  static bool DestroyWindow(void *context, void *sdl_window);
  static bool ShutdownVideo(void *context);

  void RecordSdlError(const char *operation);
  void RestoreVideoDriverHint() noexcept;
  bool RequireOwnerThread(const char *operation);
  bool IsCurrentOwnerThread() const noexcept;

  std::string m_last_error;
  std::string m_previous_video_driver_hint;
  std::thread::id m_owner_thread{};
  bool m_had_previous_video_driver_hint = false;
  bool m_video_initialized = false;
  bool m_owner_thread_claimed = false;
};

} // namespace RoR
