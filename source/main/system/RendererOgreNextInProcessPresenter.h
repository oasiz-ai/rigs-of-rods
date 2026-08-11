/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief One-process OgreNext window, frontend, and direct game-input owner.

#pragma once

#include "RendererGameInputTarget.h"
#include "RendererInProcessSession.h"

#include <cstdint>
#include <memory>
#include <string>

namespace RoR {

namespace Detail {

/// Allocation-free FIFO policy shared by the concrete SDL drain and its
/// deterministic contract test. Focus and native visibility are independent:
/// keyboard, text, and mouse events are admitted only after both transitions
/// have made the presentation window interactive.
class RendererOgreNextInProcessInputGate final {
public:
  void ObserveFocus(bool focused) noexcept { focused_ = focused; }
  void ObserveWindowSuppressed(bool suppressed) noexcept {
    window_suppressed_ = suppressed;
  }

  [[nodiscard]] bool focused() const noexcept { return focused_; }
  [[nodiscard]] bool window_suppressed() const noexcept {
    return window_suppressed_;
  }
  [[nodiscard]] bool AcceptsKeyboardTextMouse() const noexcept {
    return focused_ && !window_suppressed_;
  }
  [[nodiscard]] bool AcceptsPhysicalInput() const noexcept {
    return AcceptsKeyboardTextMouse();
  }

private:
  bool focused_ = false;
  bool window_suppressed_ = true;
};

} // namespace Detail

constexpr std::uint32_t
    kRendererOgreNextInProcessPresenterContractVersion = 1U;

struct RendererOgreNextInProcessPresenterConfiguration final {
  std::uint32_t version =
      kRendererOgreNextInProcessPresenterContractVersion;
  std::string shader_media_root;
  std::string presentation_media_root;
  std::uint32_t logical_width = 1280U;
  std::uint32_t logical_height = 720U;
};

enum class RendererOgreNextInProcessPresenterStatus : std::uint8_t {
  COMPLETED = 0U,
  REJECTED_CONFIGURATION,
  REJECTED_LIFECYCLE,
  FAILED_WINDOW_INITIALIZATION,
  FAILED_FRONTEND_CONFIGURATION,
  FAILED_INPUT_ACTIVATION,
  FAILED_WINDOW_SHUTDOWN,
  FAILED_ALLOCATION,
  FAILED_INTERNAL,
};

/// Owns the sole visible SDL/Metal presentation window and an uninitialized
/// OgreNext N1 frontend. The public boundary is renderer-neutral; the Pimpl
/// implementation is the only translation unit that includes OgreNext or SDL.
///
/// PrepareWindow() must run before the transitional Ogre14 resource host so
/// this object becomes SDL's process video owner. AttachInputTarget() runs
/// after RoR creates its InputEngine. RendererInProcessSession then initializes
/// and shuts down Frontend() while this object services PollEvents().
/// ShutdownEventPump() deliberately only quiesces polling: the caller must
/// first destroy the hidden Ogre14 window and only then call ShutdownWindow(),
/// which releases the visible native window and process-global SDL ownership.
class RendererOgreNextInProcessPresenter final
    : public IRendererInProcessEventPump {
public:
  RendererOgreNextInProcessPresenter();
  ~RendererOgreNextInProcessPresenter() override;

  RendererOgreNextInProcessPresenter(
      const RendererOgreNextInProcessPresenter &) = delete;
  RendererOgreNextInProcessPresenter &operator=(
      const RendererOgreNextInProcessPresenter &) = delete;
  RendererOgreNextInProcessPresenter(
      RendererOgreNextInProcessPresenter &&) = delete;
  RendererOgreNextInProcessPresenter &operator=(
      RendererOgreNextInProcessPresenter &&) = delete;

  [[nodiscard]] RendererOgreNextInProcessPresenterStatus PrepareWindow(
      const RendererOgreNextInProcessPresenterConfiguration &configuration)
      noexcept;
  /// The hidden resource window is never treated as an input or presentation
  /// owner. SHOWN/RESTORED events for it are forced back to hidden by the
  /// presenter's single SDL drain. Pass null to clear the protection.
  [[nodiscard]] RendererOgreNextInProcessPresenterStatus
  ProtectHiddenResourceWindow(void *sdl_window) noexcept;
  [[nodiscard]] RendererOgreNextInProcessPresenterStatus
  AttachInputTarget(IRendererGameInputTarget &target) noexcept;

  [[nodiscard]] Render::IRendererFrontend *Frontend() noexcept;
  [[nodiscard]] const Render::IRendererFrontend *Frontend() const noexcept;
  [[nodiscard]] Render::FrontendInitializationRequest
  InitialFrontendRequest() const noexcept;
  [[nodiscard]] Render::FrontendSurfaceUpdate CurrentSurface() const noexcept;

  [[nodiscard]] Render::ValidationResult PollEvents(
      RendererInProcessEventPollPoint point,
      RendererInProcessEventObservation &observation) override;
  void ShutdownEventPump() noexcept override;

  [[nodiscard]] RendererOgreNextInProcessPresenterStatus
  ShutdownWindow() noexcept;
  [[nodiscard]] bool prepared() const noexcept;
  [[nodiscard]] bool input_attached() const noexcept;
  [[nodiscard]] bool quiesced() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool IsKnownRendererOgreNextInProcessPresenterStatus(
    RendererOgreNextInProcessPresenterStatus status) noexcept;
[[nodiscard]] const char *ToString(
    RendererOgreNextInProcessPresenterStatus status) noexcept;

} // namespace RoR
