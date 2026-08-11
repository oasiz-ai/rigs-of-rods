/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextWindowHost.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

struct FakeSdlRuntime {
  RoR::RendererOgreNextWindowPlatform platform =
      RoR::RendererOgreNextWindowPlatform::WINDOWS_WIN32;
  RoR::RendererOgreNextSdlVideoDriver driver =
      RoR::RendererOgreNextSdlVideoDriver::WINDOWS;
  void *window = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x1110U));
  void *display = nullptr;
  std::uintptr_t native_window = 0x2220U;
  void *metal_view =
      reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x3330U));
  std::uint32_t drawable_width = 1600U;
  std::uint32_t drawable_height = 1200U;
  std::uint32_t post_show_drawable_width = 0U;
  std::uint32_t post_show_drawable_height = 0U;
  bool main_thread = true;
  bool fail_video = false;
  bool fail_create = false;
  bool fail_query = false;
  bool fail_metal_view = false;
  bool fail_visibility = false;
  bool fail_resize = false;
  bool fail_destroy_metal_view = false;
  bool fail_destroy_window = false;
  bool fail_shutdown_video = false;
  bool throw_destroy_metal_view = false;
  bool throw_destroy_window = false;
  bool throw_shutdown_video = false;
  bool throw_query = false;
  bool change_identity_on_query = false;
  bool visible = false;
  std::string required_driver;
  RoR::RendererOgreNextSdlWindowCreateRequest create_request{};
  std::uint32_t resize_timeout_ms = 0U;
  std::uint32_t visibility_timeout_ms = 0U;
  std::thread::id owner_thread{};
  bool owner_thread_claimed = false;
  std::vector<std::string> calls;
};

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer Ogre-Next window host test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool IsMainThread(void *context) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("main-thread");
  return fake.main_thread;
}

bool ClaimOrValidateOwnerThread(void *context) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("owner-thread");
  const std::thread::id current = std::this_thread::get_id();
  if (!fake.owner_thread_claimed) {
    fake.owner_thread = current;
    fake.owner_thread_claimed = true;
    return true;
  }
  return fake.owner_thread == current;
}

bool InitializeVideo(void *context, const char *required_driver) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("video-initialize");
  fake.required_driver = required_driver == nullptr ? "" : required_driver;
  return !fake.fail_video;
}

bool CreateWindow(
    void *context,
    const RoR::RendererOgreNextSdlWindowCreateRequest &request,
    void **window) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("window-create");
  fake.create_request = request;
  *window = fake.fail_create ? nullptr : fake.window;
  return !fake.fail_create;
}

bool QueryWindow(void *context, void *window,
                 RoR::RendererOgreNextSdlNativeWindow *result) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("window-query");
  if (fake.throw_query) {
    throw 1;
  }
  if (fake.fail_query) {
    return false;
  }
  *result = RoR::RendererOgreNextSdlNativeWindow{};
  result->platform = fake.platform;
  result->driver = fake.driver;
  result->sdl_window = window;
  result->native_display = fake.display;
  result->native_window = fake.change_identity_on_query
                              ? fake.native_window + 1U
                              : fake.native_window;
  result->drawable_width = fake.drawable_width;
  result->drawable_height = fake.drawable_height;
  return true;
}

void FillWindow(FakeSdlRuntime &fake, void *window,
                RoR::RendererOgreNextSdlNativeWindow *result) {
  *result = RoR::RendererOgreNextSdlNativeWindow{};
  result->platform = fake.platform;
  result->driver = fake.driver;
  result->sdl_window = window;
  result->native_display = fake.display;
  result->native_window = fake.change_identity_on_query
                              ? fake.native_window + 1U
                              : fake.native_window;
  result->drawable_width = fake.drawable_width;
  result->drawable_height = fake.drawable_height;
}

bool CreateMetalView(void *context, void *window,
                     std::uintptr_t cocoa_window, void **metal_view) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("metal-view-create");
  Require(window == fake.window && cocoa_window == fake.native_window,
          "Metal view did not receive the owned SDL/Cocoa window");
  *metal_view = fake.fail_metal_view ? nullptr : fake.metal_view;
  return !fake.fail_metal_view;
}

bool SetVisibleAndWaitForAck(void *context, void *window, bool visible,
                            std::uint32_t timeout_ms) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back(visible ? "window-show" : "window-hide");
  Require(window == fake.window, "visibility used a foreign SDL window");
  fake.visibility_timeout_ms = timeout_ms;
  if (fake.fail_visibility) {
    return false;
  }
  fake.visible = visible;
  if (visible && fake.post_show_drawable_width != 0U &&
      fake.post_show_drawable_height != 0U) {
    fake.drawable_width = fake.post_show_drawable_width;
    fake.drawable_height = fake.post_show_drawable_height;
  }
  return true;
}

bool ResizeWindowAndWaitForConfigure(
    void *context, void *window, std::uint32_t logical_width,
    std::uint32_t logical_height, std::uint32_t timeout_ms,
    RoR::RendererOgreNextSdlNativeWindow *acknowledged) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("window-resize-await-configure");
  Require(window == fake.window, "resize used a foreign SDL window");
  fake.resize_timeout_ms = timeout_ms;
  if (fake.fail_resize) {
    return false;
  }
  fake.drawable_width = logical_width * 2U;
  fake.drawable_height = logical_height * 2U;
  FillWindow(fake, window, acknowledged);
  return true;
}

bool DestroyMetalView(void *context, void *metal_view) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("metal-view-destroy");
  Require(metal_view == fake.metal_view,
          "cleanup destroyed a foreign Metal view");
  if (fake.throw_destroy_metal_view) {
    throw 1;
  }
  return !fake.fail_destroy_metal_view;
}

bool DestroyWindow(void *context, void *window) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("window-destroy");
  Require(window == fake.window, "cleanup destroyed a foreign SDL window");
  if (fake.throw_destroy_window) {
    throw 1;
  }
  return !fake.fail_destroy_window;
}

bool ShutdownVideo(void *context) {
  FakeSdlRuntime &fake = *static_cast<FakeSdlRuntime *>(context);
  fake.calls.push_back("video-shutdown");
  if (fake.throw_shutdown_video) {
    throw 1;
  }
  return !fake.fail_shutdown_video;
}

RoR::RendererOgreNextWindowHostRuntime Runtime(
    FakeSdlRuntime &fake,
    RoR::RendererOgreNextWindowPlatform platform) {
  RoR::RendererOgreNextWindowHostRuntime runtime;
  runtime.compiled_platform = platform;
  runtime.sdl_major = RoR::kRendererOgreNextWindowHostSdlMajor;
  runtime.sdl_minor = RoR::kRendererOgreNextWindowHostSdlMinor;
  runtime.sdl_patch = RoR::kRendererOgreNextWindowHostSdlPatch;
  runtime.context = &fake;
  runtime.claim_or_validate_owner_thread = &ClaimOrValidateOwnerThread;
  runtime.is_main_thread = &IsMainThread;
  runtime.initialize_sdl_video = &InitializeVideo;
  runtime.create_sdl_window = &CreateWindow;
  runtime.query_sdl_native_window = &QueryWindow;
  runtime.create_ogre_metal_view = &CreateMetalView;
  runtime.set_sdl_window_visible_and_wait_for_ack =
      &SetVisibleAndWaitForAck;
  runtime.resize_sdl_window_and_wait_for_configure =
      &ResizeWindowAndWaitForConfigure;
  runtime.destroy_ogre_metal_view = &DestroyMetalView;
  runtime.destroy_sdl_window = &DestroyWindow;
  runtime.shutdown_sdl_video = &ShutdownVideo;
  return runtime;
}

RoR::RendererOgreNextWindowRequest Request(
    RoR::RendererOgreNextWindowPlatform platform) {
  RoR::RendererOgreNextWindowRequest request;
  request.platform = platform;
  request.logical_width = 800U;
  request.logical_height = 600U;
  request.fsaa_samples = 4U;
  request.gamma = true;
  request.vertical_sync = true;
  request.vertical_sync_interval = 1U;
  return request;
}

const std::string *Find(
    const std::array<RoR::RendererOgreNextWindowParameter, 8U> &parameters,
    std::size_t count, const char *name) {
  for (std::size_t index = 0U; index < count; ++index) {
    if (parameters[index].name == name) {
      return &parameters[index].value;
    }
  }
  return nullptr;
}

template <std::size_t Size>
const std::string *FindAny(
    const std::array<RoR::RendererOgreNextWindowParameter, Size> &parameters,
    std::size_t count, const char *name) {
  for (std::size_t index = 0U; index < count; ++index) {
    if (parameters[index].name == name) {
      return &parameters[index].value;
    }
  }
  return nullptr;
}

std::uintptr_t ParseAddress(const std::string &value) {
  char *end = nullptr;
  const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
  Require(end != nullptr && *end == '\0', "native address is not decimal");
  return static_cast<std::uintptr_t>(parsed);
}

std::size_t CountCall(const FakeSdlRuntime &fake, const char *call) {
  std::size_t count = 0U;
  for (const std::string &candidate : fake.calls) {
    if (candidate == call) {
      ++count;
    }
  }
  return count;
}

template <typename Operation>
RoR::RendererOgreNextWindowHostStatus RunOnForeignThread(
    Operation operation) {
  RoR::RendererOgreNextWindowHostStatus status =
      RoR::RendererOgreNextWindowHostStatus::FAILED_INTERNAL;
  std::thread worker([&status, &operation]() { status = operation(); });
  worker.join();
  return status;
}

void RequireMetrics(const RoR::RendererOgreNextWindowHost &host,
                    std::uint64_t generation, std::uint32_t logical_width,
                    std::uint32_t logical_height,
                    std::uint32_t drawable_width,
                    std::uint32_t drawable_height) {
  const RoR::RendererOgreNextWindowMetrics *metrics = host.Metrics();
  Require(metrics != nullptr && metrics->generation == generation &&
              metrics->logical_width == logical_width &&
              metrics->logical_height == logical_height &&
              metrics->drawable_width == drawable_width &&
              metrics->drawable_height == drawable_height,
          "logical/drawable surface metrics changed");
  Require(std::fabs(metrics->content_scale_x -
                    static_cast<double>(drawable_width) /
                        static_cast<double>(logical_width)) < 1e-12 &&
              std::fabs(metrics->content_scale_y -
                    static_cast<double>(drawable_height) /
                        static_cast<double>(logical_height)) < 1e-12,
          "content scale does not derive from drawable pixels");
}

void TestEnumAndVersionContracts() {
  Require(RoR::kRendererOgreNextWindowHostContractVersion == 2U &&
              RoR::kRendererOgreNextWindowHostSdlMajor == 2U &&
              RoR::kRendererOgreNextWindowHostSdlMinor == 32U &&
              RoR::kRendererOgreNextWindowHostSdlPatch == 10U,
          "SDL source version pin changed");
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    Require(RoR::IsKnownRendererOgreNextWindowPlatform(
                static_cast<RoR::RendererOgreNextWindowPlatform>(value)) ==
                (value <= 4U),
            "platform classifier accepted an unknown value");
    Require(RoR::IsKnownRendererOgreNextSdlVideoDriver(
                static_cast<RoR::RendererOgreNextSdlVideoDriver>(value)) ==
                (value <= 4U),
            "driver classifier accepted an unknown value");
    Require(RoR::IsKnownRendererOgreNextWindowBridge(
                static_cast<RoR::RendererOgreNextWindowBridge>(value)) ==
                (value <= 3U),
            "bridge classifier accepted an unknown value");
    Require(RoR::IsKnownRendererOgreNextWindowLifecycle(
                static_cast<RoR::RendererOgreNextWindowLifecycle>(value)) ==
                (value <= 5U),
            "lifecycle classifier accepted an unknown value");
    Require(RoR::IsKnownRendererOgreNextWindowHostStatus(
                static_cast<RoR::RendererOgreNextWindowHostStatus>(value)) ==
                (value <= 14U),
            "status classifier accepted an unknown value");
  }
  Require(std::strcmp(
              RoR::ToString(RoR::RendererOgreNextWindowHostStatus::
                                REJECTED_WAYLAND_UNSUPPORTED),
              "rejected-wayland-unsupported") == 0 &&
              std::strcmp(RoR::ToString(
                              static_cast<
                                  RoR::RendererOgreNextWindowHostStatus>(255U)),
                          "invalid") == 0,
          "status strings changed");
}

void TestCocoaMetalViewTranslationAndLifecycle() {
  FakeSdlRuntime fake;
  fake.platform = RoR::RendererOgreNextWindowPlatform::MACOS_COCOA_METAL;
  fake.driver = RoR::RendererOgreNextSdlVideoDriver::COCOA;
  RoR::RendererOgreNextWindowHost host;
  const auto status = host.Initialize(
      Request(fake.platform), Runtime(fake, fake.platform));
  Require(status == RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::READY_HIDDEN &&
              fake.required_driver == "cocoa",
          "Cocoa host did not initialize hidden on its exact driver");
  const std::uint32_t expected_flags =
      RoR::ROR_OGRE_NEXT_WINDOW_HIDDEN |
      RoR::ROR_OGRE_NEXT_WINDOW_RESIZABLE |
      RoR::ROR_OGRE_NEXT_WINDOW_ALLOW_HIGHDPI |
      RoR::ROR_OGRE_NEXT_WINDOW_METAL;
  Require(fake.create_request.flags == expected_flags,
          "Cocoa SDL window flags changed");
  const RoR::RendererOgreNextWindowBinding *binding = host.Binding();
  Require(binding != nullptr &&
              binding->bridge ==
                  RoR::RendererOgreNextWindowBridge::COCOA_OGRE_METAL_VIEW,
          "Cocoa binding did not select the OgreMetalView bridge");
  const std::string *external =
      Find(binding->presentation_window_parameters,
           binding->presentation_window_parameter_count,
           "externalWindowHandle");
  Require(external != nullptr &&
              ParseAddress(*external) ==
                  reinterpret_cast<std::uintptr_t>(fake.metal_view) &&
              ParseAddress(*external) != fake.native_window,
          "Cocoa externalWindowHandle did not identify OgreMetalView");
  Require(Find(binding->presentation_window_parameters,
               binding->presentation_window_parameter_count,
               "parentWindowHandle") == nullptr &&
              Find(binding->presentation_window_parameters,
                   binding->presentation_window_parameter_count,
                   "vsync") == nullptr &&
              *Find(binding->presentation_window_parameters,
                    binding->presentation_window_parameter_count,
                    "gamma") == "true" &&
              *Find(binding->presentation_window_parameters,
                    binding->presentation_window_parameter_count,
                    "FSAA") == "4" &&
              *Find(binding->presentation_window_parameters,
                    binding->presentation_window_parameter_count,
                    "presentsWithTransaction") == "false",
          "Cocoa Ogre parameters changed");
  RequireMetrics(host, 1U, 800U, 600U, 1600U, 1200U);

  fake.post_show_drawable_width = 2000U;
  fake.post_show_drawable_height = 1500U;
  Require(host.Resume() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::ACTIVE &&
              fake.visible,
          "Cocoa host did not resume");
  Require(fake.visibility_timeout_ms == 2000U,
          "visibility did not enforce the finite native-ack timeout");
  Require(fake.calls.size() >= 3U &&
              fake.calls[fake.calls.size() - 3U] == "window-query" &&
              fake.calls[fake.calls.size() - 2U] == "window-show" &&
              fake.calls.back() == "window-query",
          "resume did not re-query native metrics after the show ack");
  RequireMetrics(host, 2U, 800U, 600U, 2000U, 1500U);
  Require(host.Resize(1024U, 700U) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "Cocoa host did not accept a logical resize");
  Require(fake.resize_timeout_ms == 2000U,
          "resize did not enforce the finite configure-ack timeout");
  RequireMetrics(host, 3U, 1024U, 700U, 2048U, 1400U);
  const std::size_t calls_before_scale_refresh = fake.calls.size();
  fake.drawable_width = 2560U;
  fake.drawable_height = 1750U;
  Require(host.Resize(1024U, 700U) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              fake.calls.size() == calls_before_scale_refresh + 3U &&
              fake.calls[fake.calls.size() - 3U] == "main-thread" &&
              fake.calls[fake.calls.size() - 2U] == "owner-thread" &&
              fake.calls.back() == "window-query",
          "same-logical HiDPI migration issued a resize instead of refresh");
  RequireMetrics(host, 4U, 1024U, 700U, 2560U, 1750U);
  const std::size_t resize_commands_before_external =
      CountCall(fake, "window-resize-await-configure");
  fake.drawable_width = 2400U;
  fake.drawable_height = 1600U;
  Require(host.AdoptExternalResize(1200U, 800U) ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              CountCall(fake, "window-resize-await-configure") ==
                  resize_commands_before_external,
          "external resize adoption issued a second native resize command");
  RequireMetrics(host, 5U, 1200U, 800U, 2400U, 1600U);
  Require(host.Suspend() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              !fake.visible,
          "Cocoa host did not suspend");
  Require(host.Shutdown() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Binding() == nullptr && host.Metrics() == nullptr,
          "Cocoa host did not shut down");
  const std::vector<std::string> tail(fake.calls.end() - 3,
                                      fake.calls.end());
  Require(tail == std::vector<std::string>({"metal-view-destroy",
                                            "window-destroy",
                                            "video-shutdown"}),
          "Cocoa resources were not destroyed in reverse ownership order");
  const std::size_t calls_after_shutdown = fake.calls.size();
  Require(host.Shutdown() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              fake.calls.size() == calls_after_shutdown,
          "Cocoa shutdown was not idempotent");
}

void TestWin32Translation() {
  FakeSdlRuntime fake;
  RoR::RendererOgreNextWindowRequest request = Request(fake.platform);
  request.gamma = false;
  request.vertical_sync_interval = 2U;
  RoR::RendererOgreNextWindowHost host;
  Require(host.Initialize(request, Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              fake.required_driver == "windows",
          "Win32 host did not initialize");
  const std::uint32_t expected_flags =
      RoR::ROR_OGRE_NEXT_WINDOW_HIDDEN |
      RoR::ROR_OGRE_NEXT_WINDOW_RESIZABLE |
      RoR::ROR_OGRE_NEXT_WINDOW_ALLOW_HIGHDPI;
  Require(fake.create_request.flags == expected_flags,
          "Win32 SDL window flags changed");
  const RoR::RendererOgreNextWindowBinding &binding = *host.Binding();
  Require(binding.bridge ==
              RoR::RendererOgreNextWindowBridge::WIN32_EXTERNAL_HWND &&
              ParseAddress(*Find(binding.presentation_window_parameters,
                                 binding.presentation_window_parameter_count,
                                 "externalWindowHandle")) ==
                  fake.native_window &&
              Find(binding.presentation_window_parameters,
                   binding.presentation_window_parameter_count,
                   "parentWindowHandle") == nullptr &&
              *Find(binding.presentation_window_parameters,
                    binding.presentation_window_parameter_count,
                    "gamma") == "false" &&
              *Find(binding.presentation_window_parameters,
                    binding.presentation_window_parameter_count,
                    "FSAA") == "4" &&
              *Find(binding.presentation_window_parameters,
                    binding.presentation_window_parameter_count,
                    "vsync") == "true" &&
              *Find(binding.presentation_window_parameters,
                    binding.presentation_window_parameter_count,
                    "vsyncInterval") == "2",
          "Win32 external HWND/Ogre parameters changed");
  Require(host.Shutdown() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "Win32 host did not shut down");
  Require(fake.calls[fake.calls.size() - 2U] == "window-destroy" &&
              fake.calls.back() == "video-shutdown",
          "Win32 resource ownership order changed");
}

void TestLinuxX11XcbTranslationAndStablePair() {
  FakeSdlRuntime fake;
  fake.platform = RoR::RendererOgreNextWindowPlatform::LINUX_X11_XCB;
  fake.driver = RoR::RendererOgreNextSdlVideoDriver::X11;
  fake.display =
      reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x4440U));
  RoR::RendererOgreNextWindowRequest request = Request(fake.platform);
  request.vertical_sync = false;
  request.vertical_sync_interval = 0U;
  RoR::RendererOgreNextWindowHost host;
  Require(host.Initialize(request, Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              fake.required_driver == "x11",
          "Linux did not force the SDL X11 driver");
  const std::uint32_t expected_flags =
      RoR::ROR_OGRE_NEXT_WINDOW_HIDDEN |
      RoR::ROR_OGRE_NEXT_WINDOW_RESIZABLE |
      RoR::ROR_OGRE_NEXT_WINDOW_ALLOW_HIGHDPI |
      RoR::ROR_OGRE_NEXT_WINDOW_VULKAN;
  Require(fake.create_request.flags == expected_flags,
          "Linux SDL window flags changed");
  const RoR::RendererOgreNextWindowBinding *binding = host.Binding();
  Require(binding != nullptr &&
              binding->bridge ==
                  RoR::RendererOgreNextWindowBridge::X11_XCB_SDL2_PAIR &&
              *FindAny(binding->renderer_options,
                       binding->renderer_option_count, "Interface") == "xcb" &&
              *FindAny(binding->bootstrap_window_parameters,
                       binding->bootstrap_window_parameter_count,
                       "windowType") == "null" &&
              Find(binding->presentation_window_parameters,
                   binding->presentation_window_parameter_count,
                   "externalWindowHandle") == nullptr &&
              Find(binding->presentation_window_parameters,
                   binding->presentation_window_parameter_count,
                   "parentWindowHandle") == nullptr,
          "Linux did not select Null bootstrap plus XCB presentation");
  const std::string *sdl2x11 =
      Find(binding->presentation_window_parameters,
           binding->presentation_window_parameter_count, "SDL2x11");
  Require(sdl2x11 != nullptr, "Linux SDL2x11 parameter is missing");
  const auto *pair = reinterpret_cast<
      const RoR::RendererOgreNextX11WindowPair *>(ParseAddress(*sdl2x11));
  Require(pair == &binding->x11_pair && pair->display == fake.display &&
              pair->window == static_cast<unsigned long>(fake.native_window),
          "Linux SDL2x11 did not point at the stable Display/Window pair");
  Require(host.Resume() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Binding() == binding &&
              reinterpret_cast<const RoR::RendererOgreNextX11WindowPair *>(
                  ParseAddress(*Find(
                      binding->presentation_window_parameters,
                      binding->presentation_window_parameter_count,
                      "SDL2x11"))) == pair,
          "Linux SDL2x11 pair address moved across lifecycle operations");
  Require(host.Shutdown() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "Linux host did not shut down");
}

void TestExternalVisibilityAdoptionNeverIssuesShowOrHide() {
  FakeSdlRuntime fake;
  RoR::RendererOgreNextWindowHost host;
  Require(host.Initialize(Request(fake.platform),
                          Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "external-visibility fixture did not initialize");
  const std::size_t hidden_show_count = CountCall(fake, "window-show");
  const std::size_t hidden_hide_count = CountCall(fake, "window-hide");
  Require(host.AdoptExternalVisibility(false) ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_INVALID_REQUEST &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::READY_HIDDEN &&
              CountCall(fake, "window-show") == hidden_show_count &&
              CountCall(fake, "window-hide") == hidden_hide_count,
          "external visibility bypassed first-frame show ownership");

  Require(host.Resume() ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::ACTIVE,
          "external-visibility fixture did not become active");
  const std::size_t show_count = CountCall(fake, "window-show");
  const std::size_t hide_count = CountCall(fake, "window-hide");
  const std::uint64_t generation = host.Metrics()->generation;

  // Model an OS minimize/hide that was already committed before the host was
  // notified. Adoption must retain the last valid drawable and must not hide
  // the window a second time.
  fake.visible = false;
  fake.drawable_width = 0U;
  fake.drawable_height = 0U;
  Require(host.AdoptExternalVisibility(false) ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::SUSPENDED &&
              host.Metrics()->generation == generation &&
              CountCall(fake, "window-show") == show_count &&
              CountCall(fake, "window-hide") == hide_count,
          "external minimize adoption issued a programmatic hide");

  // Model the matching OS restore. The host may query and commit current
  // drawable metrics, but it must not issue SDL_ShowWindow a second time.
  fake.visible = true;
  fake.drawable_width = 1920U;
  fake.drawable_height = 1080U;
  Require(host.AdoptExternalVisibility(true) ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::ACTIVE &&
              CountCall(fake, "window-show") == show_count &&
              CountCall(fake, "window-hide") == hide_count,
          "external restore adoption issued a programmatic show");
  RequireMetrics(host, generation + 1U, 800U, 600U, 1920U, 1080U);
  Require(host.Shutdown() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "external-visibility fixture did not shut down");
}

void TestWaylandAndInvalidContractsFailBeforeSdl() {
  FakeSdlRuntime fake;
  fake.platform = RoR::RendererOgreNextWindowPlatform::LINUX_WAYLAND;
  fake.driver = RoR::RendererOgreNextSdlVideoDriver::WAYLAND;
  RoR::RendererOgreNextWindowHost wayland;
  Require(wayland.Initialize(Request(fake.platform),
                             Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::
                  REJECTED_WAYLAND_UNSUPPORTED &&
              fake.calls.empty(),
          "Wayland did not fail before SDL initialization");

  FakeSdlRuntime mac;
  mac.platform = RoR::RendererOgreNextWindowPlatform::MACOS_COCOA_METAL;
  mac.driver = RoR::RendererOgreNextSdlVideoDriver::COCOA;
  mac.main_thread = false;
  RoR::RendererOgreNextWindowHost wrong_thread;
  Require(wrong_thread.Initialize(Request(mac.platform),
                                  Runtime(mac, mac.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::
                  REJECTED_MAIN_THREAD_REQUIRED &&
              mac.calls == std::vector<std::string>({"main-thread"}),
          "Cocoa did not reject a non-main-thread window create");

  FakeSdlRuntime invalid;
  RoR::RendererOgreNextWindowHostRuntime runtime =
      Runtime(invalid, invalid.platform);
  runtime.sdl_patch = 9U;
  RoR::RendererOgreNextWindowHost wrong_sdl;
  Require(wrong_sdl.Initialize(Request(invalid.platform), runtime) ==
              RoR::RendererOgreNextWindowHostStatus::
                  REJECTED_INVALID_RUNTIME &&
              invalid.calls.empty(),
          "an unpinned SDL runtime reached native initialization");

  FakeSdlRuntime legacy_runtime_fake;
  RoR::RendererOgreNextWindowHostRuntime legacy_runtime =
      Runtime(legacy_runtime_fake, legacy_runtime_fake.platform);
  legacy_runtime.version = 1U;
  RoR::RendererOgreNextWindowHost legacy_runtime_host;
  Require(legacy_runtime_host.Initialize(
              Request(legacy_runtime_fake.platform), legacy_runtime) ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_INVALID_RUNTIME &&
              legacy_runtime_fake.calls.empty(),
          "a v1 runtime escaped the owner-thread ABI v2 rejection");

  FakeSdlRuntime legacy_request_fake;
  RoR::RendererOgreNextWindowRequest legacy_request =
      Request(legacy_request_fake.platform);
  legacy_request.version = 1U;
  RoR::RendererOgreNextWindowHost legacy_request_host;
  Require(legacy_request_host.Initialize(
              legacy_request,
              Runtime(legacy_request_fake, legacy_request_fake.platform)) ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_INVALID_REQUEST &&
              legacy_request_fake.calls.empty(),
          "a v1 request escaped the owner-thread ABI v2 rejection");

  FakeSdlRuntime mismatch;
  RoR::RendererOgreNextWindowHost platform_mismatch;
  Require(platform_mismatch.Initialize(
              Request(RoR::RendererOgreNextWindowPlatform::WINDOWS_WIN32),
              Runtime(mismatch,
                      RoR::RendererOgreNextWindowPlatform::LINUX_X11_XCB)) ==
              RoR::RendererOgreNextWindowHostStatus::
                  REJECTED_PLATFORM_MISMATCH &&
              mismatch.calls.empty(),
          "a compiled-platform mismatch reached SDL");

  FakeSdlRuntime invalid_request;
  RoR::RendererOgreNextWindowRequest mac_no_vsync = Request(
      RoR::RendererOgreNextWindowPlatform::MACOS_COCOA_METAL);
  mac_no_vsync.vertical_sync = false;
  mac_no_vsync.vertical_sync_interval = 0U;
  RoR::RendererOgreNextWindowHost bad_request;
  Require(bad_request.Initialize(
              mac_no_vsync,
              Runtime(invalid_request, mac_no_vsync.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::
                  REJECTED_INVALID_REQUEST &&
              invalid_request.calls.empty(),
          "uncontrolled Metal vsync was accepted");
}

void TestOwnerThreadAffinityIsFailClosed() {
  FakeSdlRuntime fake;
  RoR::RendererOgreNextWindowHost host;
  Require(host.Initialize(Request(fake.platform),
                          Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Resume() ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "owner-thread fixture did not initialize and resume");
  const RoR::RendererOgreNextWindowBinding *binding = host.Binding();
  const RoR::RendererOgreNextWindowMetrics metrics = *host.Metrics();
  const std::size_t calls_before = fake.calls.size();

  Require(RunOnForeignThread([&host]() { return host.Resume(); }) ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_OWNER_THREAD_REQUIRED &&
              RunOnForeignThread([&host]() { return host.Suspend(); }) ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_OWNER_THREAD_REQUIRED &&
              RunOnForeignThread(
                  [&host]() { return host.Resize(900U, 700U); }) ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_OWNER_THREAD_REQUIRED &&
              RunOnForeignThread([&host]() {
                return host.AdoptExternalResize(900U, 700U);
              }) == RoR::RendererOgreNextWindowHostStatus::
                        REJECTED_OWNER_THREAD_REQUIRED &&
              RunOnForeignThread([&host]() {
                return host.AdoptExternalVisibility(false);
              }) == RoR::RendererOgreNextWindowHostStatus::
                        REJECTED_OWNER_THREAD_REQUIRED &&
              RunOnForeignThread([&host]() { return host.RefreshMetrics(); }) ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_OWNER_THREAD_REQUIRED &&
              RunOnForeignThread([&host]() { return host.Shutdown(); }) ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_OWNER_THREAD_REQUIRED,
          "a foreign thread reached a live lifecycle operation");
  Require(fake.calls.size() == calls_before + 7U &&
              fake.calls[calls_before] == "owner-thread" &&
              fake.calls[calls_before + 1U] == "owner-thread" &&
              fake.calls[calls_before + 2U] == "owner-thread" &&
              fake.calls[calls_before + 3U] == "owner-thread" &&
              fake.calls[calls_before + 4U] == "owner-thread" &&
              fake.calls[calls_before + 5U] == "owner-thread" &&
              fake.calls[calls_before + 6U] == "owner-thread" &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::ACTIVE &&
              host.Binding() == binding && host.Metrics() != nullptr &&
              host.Metrics()->generation == metrics.generation &&
              host.Metrics()->logical_width == metrics.logical_width &&
              host.Metrics()->logical_height == metrics.logical_height &&
              fake.visible,
          "foreign lifecycle calls invoked native callbacks or mutated state");
  Require(host.Shutdown() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "owner thread could not clean up after foreign-thread rejection");

  FakeSdlRuntime hidden_fake;
  RoR::RendererOgreNextWindowHost hidden_host;
  Require(hidden_host.Initialize(Request(hidden_fake.platform),
                                 Runtime(hidden_fake,
                                         hidden_fake.platform)) ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              RunOnForeignThread([&hidden_host]() {
                return hidden_host.Suspend();
              }) == RoR::RendererOgreNextWindowHostStatus::
                        REJECTED_OWNER_THREAD_REQUIRED &&
              hidden_host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::READY_HIDDEN &&
              !hidden_fake.visible &&
              hidden_host.Shutdown() ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "foreign hidden-window suspend mutated lifecycle state");
}

void TestCocoaMainThreadIsRevalidatedAfterInitialize() {
  FakeSdlRuntime fake;
  fake.platform = RoR::RendererOgreNextWindowPlatform::MACOS_COCOA_METAL;
  fake.driver = RoR::RendererOgreNextSdlVideoDriver::COCOA;
  RoR::RendererOgreNextWindowHost host;
  Require(host.Initialize(Request(fake.platform), Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "Cocoa main-thread-loss fixture did not initialize");
  const std::size_t calls_before = fake.calls.size();
  fake.main_thread = false;
  Require(host.Resume() ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_OWNER_THREAD_REQUIRED &&
              fake.calls.size() == calls_before + 1U &&
              fake.calls.back() == "main-thread" &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::READY_HIDDEN &&
              host.Binding() != nullptr && !fake.visible,
          "Cocoa main-thread loss reached a native callback or mutated state");
  const std::size_t calls_before_shutdown = fake.calls.size();
  Require(host.Shutdown() ==
                  RoR::RendererOgreNextWindowHostStatus::
                      REJECTED_OWNER_THREAD_REQUIRED &&
              fake.calls.size() == calls_before_shutdown + 1U &&
              fake.calls.back() == "main-thread" &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::READY_HIDDEN &&
              host.Binding() != nullptr,
          "Cocoa off-main shutdown mutated or released live ownership");
  fake.main_thread = true;
  Require(host.Shutdown() ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "Cocoa owner/main thread could not retry shutdown");
}

void TestDestructorNeverPerformsForeignThreadNativeCleanup() {
  FakeSdlRuntime foreign_fake;
  auto *foreign_host = new RoR::RendererOgreNextWindowHost();
  Require(foreign_host->Initialize(Request(foreign_fake.platform),
                                   Runtime(foreign_fake,
                                           foreign_fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "foreign-destructor fixture did not initialize");
  const std::size_t calls_before_foreign_delete = foreign_fake.calls.size();
  std::thread foreign_destructor(
      [&foreign_host]() { delete foreign_host; });
  foreign_destructor.join();
  foreign_host = nullptr;
  Require(foreign_fake.calls.size() == calls_before_foreign_delete + 1U &&
              foreign_fake.calls.back() == "owner-thread" &&
              CountCall(foreign_fake, "window-destroy") == 0U &&
              CountCall(foreign_fake, "video-shutdown") == 0U,
          "foreign-thread destructor invoked a native cleanup callback");

  FakeSdlRuntime owner_fake;
  auto *owner_host = new RoR::RendererOgreNextWindowHost();
  Require(owner_host->Initialize(Request(owner_fake.platform),
                                 Runtime(owner_fake, owner_fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "owner-destructor fixture did not initialize");
  delete owner_host;
  Require(CountCall(owner_fake, "window-destroy") == 1U &&
              CountCall(owner_fake, "video-shutdown") == 1U,
          "owner-thread destructor did not finish best-effort cleanup");
}

enum class CleanupFailureStage {
  METAL_VIEW,
  WINDOW,
  VIDEO,
};

void ExerciseRetryableCleanup(CleanupFailureStage stage, bool throws) {
  FakeSdlRuntime fake;
  fake.platform = RoR::RendererOgreNextWindowPlatform::MACOS_COCOA_METAL;
  fake.driver = RoR::RendererOgreNextSdlVideoDriver::COCOA;
  RoR::RendererOgreNextWindowHost host;
  Require(host.Initialize(Request(fake.platform), Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "retryable-cleanup fixture did not initialize");

  if (stage == CleanupFailureStage::METAL_VIEW) {
    fake.fail_destroy_metal_view = !throws;
    fake.throw_destroy_metal_view = throws;
  } else if (stage == CleanupFailureStage::WINDOW) {
    fake.fail_destroy_window = !throws;
    fake.throw_destroy_window = throws;
  } else {
    fake.fail_shutdown_video = !throws;
    fake.throw_shutdown_video = throws;
  }

  Require(host.Shutdown() ==
                  RoR::RendererOgreNextWindowHostStatus::FAILED_SHUTDOWN &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::FAILED &&
              host.Binding() == nullptr,
          "teardown failure did not retain a retryable FAILED host");
  const std::size_t metal_after_failure =
      CountCall(fake, "metal-view-destroy");
  const std::size_t window_after_failure = CountCall(fake, "window-destroy");
  const std::size_t video_after_failure = CountCall(fake, "video-shutdown");
  Require(metal_after_failure == 1U &&
              window_after_failure ==
                  (stage == CleanupFailureStage::METAL_VIEW ? 0U : 1U) &&
              video_after_failure ==
                  (stage == CleanupFailureStage::VIDEO ? 1U : 0U),
          "teardown did not stop at the first unsafe dependency");

  fake.fail_destroy_metal_view = false;
  fake.fail_destroy_window = false;
  fake.fail_shutdown_video = false;
  fake.throw_destroy_metal_view = false;
  fake.throw_destroy_window = false;
  fake.throw_shutdown_video = false;
  Require(host.Shutdown() ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::SHUTDOWN &&
              CountCall(fake, "metal-view-destroy") ==
                  (stage == CleanupFailureStage::METAL_VIEW ? 2U : 1U) &&
              CountCall(fake, "window-destroy") ==
                  (stage == CleanupFailureStage::WINDOW ? 2U : 1U) &&
              CountCall(fake, "video-shutdown") ==
                  (stage == CleanupFailureStage::VIDEO ? 2U : 1U),
          "owner-thread teardown retry leaked or double-destroyed a dependency");
  const std::size_t calls_after_success = fake.calls.size();
  Require(host.Shutdown() ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              fake.calls.size() == calls_after_success,
          "completed teardown was not idempotent");
}

void TestTeardownFailureIsDependencyOrderedAndRetryable() {
  const CleanupFailureStage stages[] = {
      CleanupFailureStage::METAL_VIEW,
      CleanupFailureStage::WINDOW,
      CleanupFailureStage::VIDEO,
  };
  for (CleanupFailureStage stage : stages) {
    ExerciseRetryableCleanup(stage, false);
    ExerciseRetryableCleanup(stage, true);
  }
}

void TestLiveFailureInvalidatesBindingAndCleanupRetries() {
  FakeSdlRuntime fake;
  RoR::RendererOgreNextWindowHost host;
  Require(host.Initialize(Request(fake.platform),
                          Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              host.Resume() ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED,
          "failure fixture did not initialize");
  fake.change_identity_on_query = true;
  Require(host.Resize(900U, 700U) ==
              RoR::RendererOgreNextWindowHostStatus::
                  FAILED_NATIVE_WINDOW_QUERY &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::FAILED &&
              host.Binding() == nullptr && host.Metrics() == nullptr &&
              !fake.visible,
          "foreign native identity did not fail closed and hide");
  fake.fail_destroy_window = true;
  fake.fail_shutdown_video = true;
  Require(host.Shutdown() ==
              RoR::RendererOgreNextWindowHostStatus::FAILED_SHUTDOWN &&
              host.Lifecycle() ==
                  RoR::RendererOgreNextWindowLifecycle::FAILED &&
              CountCall(fake, "window-destroy") == 1U &&
              CountCall(fake, "video-shutdown") == 0U,
          "failed window teardown released its SDL video dependency");
  fake.fail_destroy_window = false;
  Require(host.Shutdown() ==
                  RoR::RendererOgreNextWindowHostStatus::FAILED_SHUTDOWN &&
              CountCall(fake, "window-destroy") == 2U &&
              CountCall(fake, "video-shutdown") == 1U,
          "window teardown retry did not advance to retained SDL video");
  fake.fail_shutdown_video = false;
  Require(host.Shutdown() ==
                  RoR::RendererOgreNextWindowHostStatus::COMPLETED &&
              CountCall(fake, "window-destroy") == 2U &&
              CountCall(fake, "video-shutdown") == 2U,
          "retained SDL video ownership could not be retried");
}

void TestPartialInitializationCleansUp() {
  FakeSdlRuntime fake;
  fake.fail_query = true;
  RoR::RendererOgreNextWindowHost host;
  Require(host.Initialize(Request(fake.platform),
                          Runtime(fake, fake.platform)) ==
              RoR::RendererOgreNextWindowHostStatus::
                  FAILED_NATIVE_WINDOW_QUERY &&
              fake.calls == std::vector<std::string>(
                                {"owner-thread", "video-initialize", "window-create",
                                 "window-query", "owner-thread", "window-destroy",
                                 "video-shutdown"}) &&
              host.Binding() == nullptr,
          "partial initialization leaked or misordered SDL ownership");
}

} // namespace

int main() {
  TestEnumAndVersionContracts();
  TestCocoaMetalViewTranslationAndLifecycle();
  TestWin32Translation();
  TestLinuxX11XcbTranslationAndStablePair();
  TestExternalVisibilityAdoptionNeverIssuesShowOrHide();
  TestWaylandAndInvalidContractsFailBeforeSdl();
  TestOwnerThreadAffinityIsFailClosed();
  TestCocoaMainThreadIsRevalidatedAfterInitialize();
  TestDestructorNeverPerformsForeignThreadNativeCleanup();
  TestTeardownFailureIsDependencyOrderedAndRetryable();
  TestLiveFailureInvalidatesBindingAndCleanupRetries();
  TestPartialInitializationCleansUp();
  std::cout << "renderer Ogre-Next window host tests passed\n";
  return EXIT_SUCCESS;
}
