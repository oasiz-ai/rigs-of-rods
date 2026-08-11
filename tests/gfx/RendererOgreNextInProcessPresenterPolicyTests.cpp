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
  TestFocusTransitionsGateFollowingFifoEvents();
  TestRestoreRequiresFocusBeforeInputResumes();
  std::cout << "renderer Ogre-Next in-process input policy tests passed\n";
  return EXIT_SUCCESS;
}
