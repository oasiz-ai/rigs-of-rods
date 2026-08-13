/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextInProcessPresenter.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

namespace {

using InputGate = RoR::Detail::RendererOgreNextInProcessInputGate;
using MouseCallbackState = RoR::Detail::RendererGameMouseCallbackState;

enum class Signal {
  SHOWN,
  FOCUS_GAINED,
  FOCUS_LOST,
  MINIMIZED,
  RESTORED,
  KEYBOARD,
  TEXT,
  MOUSE,
};

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer Ogre-Next in-process input policy test failed: "
              << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool Apply(InputGate &gate, Signal signal) noexcept {
  switch (signal) {
  case Signal::SHOWN:
  case Signal::RESTORED:
    gate.ObserveWindowSuppressed(false);
    return false;
  case Signal::FOCUS_GAINED:
    gate.ObserveFocus(true);
    return false;
  case Signal::FOCUS_LOST:
    gate.ObserveFocus(false);
    return false;
  case Signal::MINIMIZED:
    gate.ObserveWindowSuppressed(true);
    gate.ObserveFocus(false);
    return false;
  case Signal::KEYBOARD:
  case Signal::TEXT:
  case Signal::MOUSE:
    return gate.AcceptsKeyboardTextMouse();
  }
  return false;
}

void TestFocusTransitionsGateFollowingFifoEvents() {
  InputGate gate;
  const std::array<Signal, 10U> sequence = {
      Signal::SHOWN,
      Signal::FOCUS_GAINED,
      Signal::KEYBOARD,
      Signal::TEXT,
      Signal::FOCUS_LOST,
      Signal::MOUSE,
      Signal::FOCUS_GAINED,
      Signal::MOUSE,
      Signal::FOCUS_LOST,
      Signal::KEYBOARD,
  };
  const std::array<bool, sequence.size()> expected = {
      false, false, true, true, false, false, false, true, false, false,
  };
  for (std::size_t index = 0U; index < sequence.size(); ++index) {
    Require(Apply(gate, sequence[index]) == expected[index],
            "focus transition did not gate the immediately following FIFO "
            "event");
  }
}

void TestRestoreRequiresFocusBeforeInputResumes() {
  InputGate gate;
  const std::array<Signal, 11U> sequence = {
      Signal::SHOWN,
      Signal::FOCUS_GAINED,
      Signal::MOUSE,
      Signal::MINIMIZED,
      Signal::KEYBOARD,
      Signal::RESTORED,
      Signal::TEXT,
      Signal::FOCUS_GAINED,
      Signal::KEYBOARD,
      Signal::TEXT,
      Signal::MOUSE,
  };
  const std::array<bool, sequence.size()> expected = {
      false, false, true, false, false, false, false, false, true, true, true,
  };
  for (std::size_t index = 0U; index < sequence.size(); ++index) {
    Require(Apply(gate, sequence[index]) == expected[index],
            "restore admitted input before the ordered focus transition");
  }
  Require(gate.AcceptsPhysicalInput(),
          "focused visible policy did not reactivate physical input");
}

void TestDirectMouseCallbacksSeeOnlyTheirCurrentTransition() {
  MouseCallbackState state;
  state.x_absolute = 20;
  state.y_absolute = 30;
  state.x_relative = 9;
  state.y_relative = -7;
  state.wheel_relative = 120;

  RoR::Detail::StageRendererGameMouseMotion(state, 100, 120, 4, -3);
  Require(state.x_absolute == 100 && state.y_absolute == 120 &&
              state.x_relative == 4 && state.y_relative == -3 &&
              state.wheel_relative == 0,
          "motion callback observed stale position, motion, or wheel state");

  RoR::Detail::StageRendererGameMouseWheel(state, 0.5F);
  Require(state.x_relative == 0 && state.y_relative == 0 &&
              state.wheel_relative == 60,
          "wheel callback replayed prior camera motion or lost wheel units");

  Require(RoR::Detail::StageRendererGameMouseButton(state, 0U, true) &&
              (state.buttons & 1U) != 0U,
          "button-down callback did not observe its pressed edge");
  Require(RoR::Detail::StageRendererGameMouseButton(state, 0U, false) &&
              (state.buttons & 1U) == 0U,
          "button-up callback did not observe its released edge");
  Require(!RoR::Detail::StageRendererGameMouseButton(state, 5U, true),
          "out-of-contract mouse button was staged");
}

void TestVisibleRetinaMetricsKeepLogicalAndPixelDomainsPaired() {
  RoR::RendererGameDisplayMetrics metrics;
  metrics.logical_width = 1280U;
  metrics.logical_height = 720U;
  metrics.pixel_width = 2560U;
  metrics.pixel_height = 1440U;
  Require(metrics.valid(), "valid Retina presentation metrics were rejected");
  Require(RoR::RendererGameLogicalCoordinate(
              1280, metrics.logical_width, metrics.pixel_width) == 640 &&
              RoR::RendererGameLogicalCoordinate(
              720, metrics.logical_height, metrics.pixel_height) == 360 &&
              RoR::RendererGameLogicalCoordinate(
              2, metrics.logical_width, metrics.pixel_width) == 1 &&
              RoR::RendererGameLogicalCoordinate(
              -2, metrics.logical_height, metrics.pixel_height) == -1,
          "backing-pixel pointer input did not enter the visible logical "
          "ImGui domain at Retina scale");
  Require(RoR::RendererGameLogicalCoordinate(375, 1000U, 1500U) == 250,
          "fractional backing scale changed direct pointer coordinates");
  metrics.pixel_width = 0U;
  Require(!metrics.valid(), "zero drawable extent was admitted as interactive");
  metrics.pixel_width = 32769U;
  Require(!metrics.valid(), "hostile visible drawable extent was admitted");
}

void TestAnalyticSkyAuditDefaultsFailClosed() {
  const RoR::RendererAnalyticSkyAudit audit;
  Require(audit.completed_frames == 0U && audit.sun_light_id == 0U &&
              audit.cpu_geometry_fnv1a64 == 0U &&
              audit.native_gpu_content_readbacks == 0U &&
              audit.native_state_verifications == 0U &&
              !audit.native_ownership_balanced &&
              !audit.expected_per_frame_ownership &&
              !audit.cpu_geometry_digest_verified &&
              !audit.native_geometry_metadata_verified &&
              !audit.production_gpu_readbacks_zero &&
              !audit.exact_native_geometry_readback &&
              !audit.separate_sun_alpha_replace && !audit.available,
          "unavailable analytic-sky audit did not fail closed");
}

} // namespace

int main() {
  static_assert(std::is_nothrow_default_constructible<InputGate>::value,
                "input policy must remain allocation-free");
  static_assert(noexcept(std::declval<InputGate &>().ObserveFocus(true)),
                "focus transition must remain non-throwing");
  static_assert(noexcept(std::declval<InputGate &>()
                             .ObserveWindowSuppressed(true)),
                "visibility transition must remain non-throwing");
  static_assert(noexcept(std::declval<const InputGate &>()
                             .AcceptsKeyboardTextMouse()),
                "input admission query must remain non-throwing");
  static_assert(noexcept(RoR::Detail::StageRendererGameMouseMotion(
                    std::declval<MouseCallbackState &>(), 0, 0, 0, 0)),
                "direct mouse staging must remain non-throwing");
  TestFocusTransitionsGateFollowingFifoEvents();
  TestRestoreRequiresFocusBeforeInputResumes();
  TestDirectMouseCallbacksSeeOnlyTheirCurrentTransition();
  TestVisibleRetinaMetricsKeepLogicalAndPixelDomainsPaired();
  TestAnalyticSkyAuditDefaultsFailClosed();
  std::cout << "renderer Ogre-Next in-process input policy tests passed\n";
  return EXIT_SUCCESS;
}
