/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Probe-only live hidden SDL/native-window ownership smoke.

#include "RendererOgreNextSdlWindowRuntime.h"
#include "RendererOgreNextWindowHost.h"

#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

constexpr int kUnsupportedExitCode = 77;

RoR::RendererOgreNextWindowPlatform Platform() noexcept {
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

bool HasExpectedBinding(const RoR::RendererOgreNextWindowBinding &binding) {
#if defined(__APPLE__)
  return binding.bridge ==
         RoR::RendererOgreNextWindowBridge::COCOA_OGRE_METAL_VIEW;
#elif defined(_WIN32)
  return binding.bridge ==
         RoR::RendererOgreNextWindowBridge::WIN32_EXTERNAL_HWND;
#elif defined(__linux__)
  return binding.bridge ==
             RoR::RendererOgreNextWindowBridge::X11_XCB_SDL2_PAIR &&
         binding.x11_pair.display != nullptr && binding.x11_pair.window != 0UL;
#else
  (void)binding;
  return false;
#endif
}

} // namespace

int main() {
  RoR::RendererOgreNextSdlWindowRuntime adapter;
  RoR::RendererOgreNextWindowRequest request;
  request.platform = Platform();
  request.logical_width = 640U;
  request.logical_height = 360U;
  request.fsaa_samples = 0U;
#if !defined(__APPLE__)
  request.vertical_sync = false;
  request.vertical_sync_interval = 0U;
#endif

  RoR::RendererOgreNextWindowHost host;
  const RoR::RendererOgreNextWindowHostStatus initialized =
      host.Initialize(request, adapter.Runtime());
  if (initialized == RoR::RendererOgreNextWindowHostStatus::
                         FAILED_SDL_VIDEO_INITIALIZATION ||
      initialized == RoR::RendererOgreNextWindowHostStatus::
                         FAILED_SDL_WINDOW_CREATION) {
    std::cerr << "SKIP: live hidden SDL window unavailable: "
              << adapter.LastError() << '\n';
    return kUnsupportedExitCode;
  }
  if (initialized != RoR::RendererOgreNextWindowHostStatus::COMPLETED) {
    std::cerr << "hidden-window initialization failed: "
              << RoR::ToString(initialized) << ": " << adapter.LastError()
              << '\n';
    return EXIT_FAILURE;
  }

  const RoR::RendererOgreNextWindowBinding *binding = host.Binding();
  const RoR::RendererOgreNextWindowMetrics *metrics = host.Metrics();
  const RoR::RendererOgreNextSdlNativeWindow *native = host.NativeWindow();
  if (host.Lifecycle() !=
          RoR::RendererOgreNextWindowLifecycle::READY_HIDDEN ||
      binding == nullptr || !HasExpectedBinding(*binding) ||
      metrics == nullptr || metrics->generation != 1U ||
      metrics->logical_width != request.logical_width ||
      metrics->logical_height != request.logical_height ||
      metrics->drawable_width == 0U || metrics->drawable_height == 0U ||
      native == nullptr || native->sdl_window == nullptr ||
      native->native_window == 0U) {
    std::cerr << "hidden-window native binding/metrics validation failed\n";
    (void)host.Shutdown();
    return EXIT_FAILURE;
  }

  RoR::RendererOgreNextWindowHostStatus foreign_thread_status =
      RoR::RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
  std::thread foreign_thread([&host, &foreign_thread_status]() {
    foreign_thread_status = host.RefreshMetrics();
  });
  foreign_thread.join();
  if (foreign_thread_status !=
          RoR::RendererOgreNextWindowHostStatus::
              REJECTED_OWNER_THREAD_REQUIRED ||
      host.Lifecycle() !=
          RoR::RendererOgreNextWindowLifecycle::READY_HIDDEN ||
      host.Binding() != binding || host.Metrics() != metrics ||
      host.NativeWindow() != native ||
      host.Metrics()->generation != 1U) {
    std::cerr << "foreign-thread live host validation did not fail closed\n";
    (void)host.Shutdown();
    return EXIT_FAILURE;
  }

  const RoR::RendererOgreNextWindowHostStatus shutdown = host.Shutdown();
  if (shutdown != RoR::RendererOgreNextWindowHostStatus::COMPLETED) {
    std::cerr << "hidden-window shutdown failed: " << adapter.LastError()
              << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "probe-only hidden SDL native-window lifecycle passed; "
               "no Ogre presentation or package admission claimed\n";
  return EXIT_SUCCESS;
}
