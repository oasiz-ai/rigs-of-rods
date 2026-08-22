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

constexpr std::uint32_t kRendererOgreNextSdlWindowEventContractVersion = 1U;

/// One normalized, owner-thread drain of SDL's window/quit event types for the
/// production Ogre-Next presenter. Keyboard, mouse, controller, and other
/// input events remain queued for RoR's input pipeline.
/// `drawable_size_changed` is derived from pixel metrics, independently of a
/// logical resize event, and therefore covers Retina/display migration.
struct RendererOgreNextSdlWindowEventBatch final {
  std::uint32_t version =
      kRendererOgreNextSdlWindowEventContractVersion;
  std::uint64_t polled_events = 0U;
  std::uint64_t matched_window_events = 0U;
  std::uint64_t close_events = 0U;
  std::uint64_t focus_gained_events = 0U;
  std::uint64_t focus_lost_events = 0U;
  std::uint64_t resize_events = 0U;
  std::uint64_t minimize_events = 0U;
  std::uint64_t restore_events = 0U;
  std::uint64_t display_change_events = 0U;
  std::uint32_t logical_width = 0U;
  std::uint32_t logical_height = 0U;
  std::uint32_t drawable_width = 0U;
  std::uint32_t drawable_height = 0U;
  bool close_requested = false;
  bool focused = false;
  bool minimized = false;
  bool hidden = false;
  bool drawable_size_changed = false;
};

/// Owns only SDL's process-global video initialization bookkeeping. The
/// RendererOgreNextWindowHost owns every SDL_Window and platform child view.
/// This is the in-process window adapter used by RoR-Combined as well as the
/// isolated presentation probes. The first host validation claims the current
/// thread; every SDL/AppKit callback must remain on that thread, and Cocoa also
/// requires the AppKit main thread. The host must finish explicit owner-thread
/// Shutdown before this adapter is destroyed.
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
  /// Validates the thread claimed during host initialization before a direct
  /// SDL operation owned by the in-process presenter.
  bool ValidateOwnerThread();
  /// Drains and normalizes close/focus/resize/minimize/restore/display events
  /// for exactly the SDL window owned by the matching host runtime.
  bool PollWindowEvents(
      void *sdl_window,
      RendererOgreNextSdlWindowEventBatch &batch);
  const std::string &LastError() const noexcept { return m_last_error; }

private:
  static bool ClaimOrValidateOwnerThread(void *context);
  static bool IsMainThread(void *context);
  static bool InitializeVideo(void *context, const char *required_driver);
  static bool CreateNativeWindow(
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
  void *m_polled_window = nullptr;
  std::uint32_t m_last_drawable_width = 0U;
  std::uint32_t m_last_drawable_height = 0U;
  bool m_has_drawable_baseline = false;
};

} // namespace RoR
