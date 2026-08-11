/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RendererOgre14InputAdapter.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace RoR;
using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "renderer OGRE 14 input adapter test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

template <typename Payload>
InputTransportEvent Event(std::uint64_t id, Payload payload) {
  InputTransportEvent event;
  event.event_id = id;
  event.host_timestamp_ns = id * 10U;
  event.payload = std::move(payload);
  return event;
}

class FakeTarget final : public IRendererGameInputTarget {
public:
  bool ActivateInput() noexcept override {
    ++activations;
    return activation_succeeds;
  }
  void KeyChanged(RendererGameKey key,
                  bool pressed) noexcept override {
    keys.emplace_back(key, pressed);
  }
  void MouseMoved(std::int32_t x, std::int32_t y, std::int32_t dx,
                  std::int32_t dy) noexcept override {
    mouse_motion = {x, y, dx, dy};
    ++mouse_moves;
  }
  void MouseButtonChanged(RendererGameMouseButton button,
                          bool pressed) noexcept override {
    mouse_buttons.emplace_back(button, pressed);
  }
  void MouseWheel(float x, float y) noexcept override {
    wheel_x += x;
    wheel_y += y;
  }
  void TextInput(std::string_view utf8) noexcept override {
    text.assign(utf8.begin(), utf8.end());
  }
  void FocusChanged(bool focused) noexcept override {
    focus = focused;
    ++focus_changes;
  }
  void WindowCloseRequested() noexcept override { ++close_requests; }
  bool Reconcile(const RendererGameInputState &input) noexcept override {
    try {
      state = input;
      ++reconciliations;
      return reconciliation_succeeds;
    } catch (...) {
      return false;
    }
  }

  bool activation_succeeds = true;
  bool reconciliation_succeeds = true;
  int activations = 0;
  int mouse_moves = 0;
  int focus_changes = 0;
  int close_requests = 0;
  int reconciliations = 0;
  bool focus = false;
  std::array<std::int32_t, 4U> mouse_motion{};
  float wheel_x = 0.0F;
  float wheel_y = 0.0F;
  std::string text;
  std::vector<std::pair<RendererGameKey, bool>> keys;
  std::vector<std::pair<RendererGameMouseButton, bool>>
      mouse_buttons;
  RendererGameInputState state;
};

void TestRendererNeutralSdlMapping() {
  Require(TranslateRendererSdlScancodeToGame(4U) ==
                  RendererGameKey::A &&
              TranslateRendererSdlScancodeToGame(230U) ==
                  RendererGameKey::RIGHT_ALT &&
              TranslateRendererSdlScancodeToGame(257U) ==
                  RendererGameKey::RIGHT_ALT &&
              TranslateRendererSdlScancodeToGame(40U) ==
                  RendererGameKey::RETURN &&
              TranslateRendererSdlScancodeToGame(158U) ==
                  RendererGameKey::RETURN &&
              TranslateRendererSdlScancodeToGame(0U) ==
                  RendererGameKey::UNASSIGNED &&
              TranslateRendererSdlScancodeToGame(
                  (std::numeric_limits<std::uint16_t>::max)()) ==
                  RendererGameKey::UNASSIGNED,
          "renderer-neutral numeric SDL key mapping changed");

  const std::array<std::pair<std::uint8_t, RendererGameMouseButton>, 5U>
      mouse_buttons{{
          {1U, RendererGameMouseButton::LEFT},
          {2U, RendererGameMouseButton::MIDDLE},
          {3U, RendererGameMouseButton::RIGHT},
          {4U, RendererGameMouseButton::X1},
          {5U, RendererGameMouseButton::X2},
      }};
  for (const auto &entry : mouse_buttons) {
    RendererGameMouseButton translated = RendererGameMouseButton::LEFT;
    Require(TryTranslateRendererSdlMouseButtonToGame(entry.first,
                                                      translated) &&
                translated == entry.second,
            "renderer-neutral numeric SDL mouse mapping changed");
  }
  RendererGameMouseButton invalid = RendererGameMouseButton::X2;
  Require(!TryTranslateRendererSdlMouseButtonToGame(0U, invalid) &&
              invalid == RendererGameMouseButton::X2,
          "invalid SDL mouse input mutated the target value");

  Require(TranslateRendererSdl2ScancodeToGame(
              Sdl2PhysicalScancode::A) ==
                  TranslateRendererSdlScancodeToGame(4U) &&
              TranslateRendererSdl2MouseButtonToGame(
                  Sdl2MouseButton::RIGHT) ==
                  RendererGameMouseButton::RIGHT,
          "temporary transport adapter diverged from neutral SDL mapping");
}

InputTransportBatch KeyboardMouseBatch() {
  InputTransportBatch batch;
  batch.clock_origin_id = 77U;
  batch.events.push_back(Event(
      1U, InputTransportFocusEvent{InputTransportFocusState::GAINED}));
  batch.events.push_back(Event(
      2U, InputTransportKeyboardKeyEvent{
              Sdl2PhysicalScancode::A,
              InputTransportDigitalState::PRESSED, false}));
  batch.events.push_back(Event(
      3U, InputTransportKeyboardKeyEvent{
              Sdl2PhysicalScancode::A,
              InputTransportDigitalState::PRESSED, true}));
  batch.events.push_back(Event(
      4U, InputTransportKeyboardKeyEvent{
              Sdl2PhysicalScancode::F16,
              InputTransportDigitalState::PRESSED, false}));
  batch.events.push_back(
      Event(5U, InputTransportMouseMotionEvent{120, 75, -3, 4}));
  batch.events.push_back(Event(
      6U, InputTransportMouseButtonEvent{
              Sdl2MouseButton::RIGHT,
              InputTransportDigitalState::PRESSED}));
  batch.events.push_back(Event(
      7U, InputTransportMouseWheelEvent{
              0.5F, -2.0F, Sdl2MouseWheelDirection::FLIPPED}));
  batch.events.push_back(
      Event(8U, InputTransportTextInputEvent{"Beam \xf0\x9f\x9a\x99"}));
  batch.events.push_back(Event(9U, InputTransportWindowCloseEvent{}));
  batch.reconciliation.through_event_id = 9U;
  batch.reconciliation.host_timestamp_ns = 90U;
  batch.reconciliation.focus = InputTransportFocusState::GAINED;
  batch.reconciliation.window_close_requested = true;
  batch.reconciliation.pressed_scancodes = {
      Sdl2PhysicalScancode::A, Sdl2PhysicalScancode::F16};
  batch.reconciliation.pressed_mouse_buttons = {Sdl2MouseButton::RIGHT};
  Require(ValidateInputTransportBatch(batch) == RenderTransportStatus::OK,
          "keyboard/mouse fixture is invalid");
  return batch;
}

InputTransportRawAxisDescriptor RawAxis(
    std::uint16_t index, InputTransportRawAxisMode mode) {
  InputTransportRawAxisDescriptor axis;
  axis.index = index;
  axis.mode = mode;
  axis.logical_minimum = -1000;
  axis.logical_maximum = 1000;
  axis.center = 0;
  axis.deadzone_minimum = -10;
  axis.deadzone_maximum = 10;
  return axis;
}

InputTransportRawDeviceDescriptor RawDescriptor() {
  InputTransportRawDeviceDescriptor descriptor;
  descriptor.device_id = 200U;
  descriptor.connection_generation = 3U;
  descriptor.device_class = InputTransportRawDeviceClass::WHEEL;
  descriptor.guid[0U] = 1U;
  descriptor.name_sha256[0U] = 0xABU;
  descriptor.axes.push_back(
      RawAxis(0U, InputTransportRawAxisMode::ABSOLUTE));
  descriptor.axes.push_back(
      RawAxis(1U, InputTransportRawAxisMode::RELATIVE));
  descriptor.button_count = 4U;
  descriptor.hat_count = 1U;
  InputTransportRawSliderDescriptor slider;
  slider.index = 0U;
  slider.x_axis = RawAxis(0U, InputTransportRawAxisMode::ABSOLUTE);
  slider.y_axis = RawAxis(1U, InputTransportRawAxisMode::ABSOLUTE);
  descriptor.sliders.push_back(slider);
  return descriptor;
}

InputTransportBatch DeviceBatch() {
  InputTransportBatch batch;
  batch.clock_origin_id = 77U;
  batch.events.push_back(Event(
      10U, InputTransportGamepadConnectionEvent{
               100U, 7U, InputTransportDeviceConnectionState::CONNECTED}));
  const InputTransportRawDeviceDescriptor raw = RawDescriptor();
  batch.events.push_back(Event(
      11U, InputTransportRawDeviceConnectionEvent{
               InputTransportDeviceConnectionState::CONNECTED, raw}));
  batch.events.push_back(Event(
      12U, InputTransportRawAxisEvent{raw.device_id,
                                      raw.connection_generation, 1U, 500}));
  batch.reconciliation.through_event_id = 12U;
  batch.reconciliation.host_timestamp_ns = 120U;
  batch.reconciliation.focus = InputTransportFocusState::GAINED;
  InputTransportGamepadReconciliationState gamepad;
  gamepad.device_id = 100U;
  gamepad.connection_generation = 7U;
  gamepad.pressed_buttons = {Sdl2GamepadButton::A};
  gamepad.axes[0U] = -32768;
  batch.reconciliation.gamepads.push_back(gamepad);
  InputTransportRawDeviceReconciliationState raw_state;
  raw_state.descriptor = raw;
  raw_state.pressed_buttons = {2U};
  raw_state.axes = {1000, 500};
  raw_state.hats = {Sdl2HatState::RIGHT_UP};
  raw_state.sliders = {{1000, -1000}};
  batch.reconciliation.raw_devices.push_back(raw_state);
  Require(ValidateInputTransportBatch(batch) == RenderTransportStatus::OK,
          "device fixture is invalid");
  return batch;
}

InputTransportBatch SaturatingDeltaBatch() {
  InputTransportBatch batch;
  batch.clock_origin_id = 77U;
  const std::int32_t maximum =
      (std::numeric_limits<std::int32_t>::max)();
  const std::int32_t minimum =
      (std::numeric_limits<std::int32_t>::min)();
  const float wheel_maximum =
      kInputEventTransportMaximumWheelMagnitude;
  batch.events.push_back(
      Event(13U, InputTransportMouseMotionEvent{1, 2, maximum, minimum}));
  batch.events.push_back(
      Event(14U, InputTransportMouseMotionEvent{3, 4, maximum, minimum}));
  batch.events.push_back(Event(
      15U, InputTransportMouseWheelEvent{
               wheel_maximum, -wheel_maximum,
               Sdl2MouseWheelDirection::NORMAL}));
  batch.events.push_back(Event(
      16U, InputTransportMouseWheelEvent{
               wheel_maximum, -wheel_maximum,
               Sdl2MouseWheelDirection::NORMAL}));
  batch.reconciliation.through_event_id = 16U;
  batch.reconciliation.host_timestamp_ns = 160U;
  batch.reconciliation.focus = InputTransportFocusState::GAINED;
  Require(ValidateInputTransportBatch(batch) == RenderTransportStatus::OK,
          "saturating delta fixture is invalid");
  return batch;
}

void TestMappingsTransitionsReconciliationAndLineage() {
  Require(TranslateRendererSdl2ScancodeToGame(Sdl2PhysicalScancode::A) ==
              RendererGameKey::A &&
              TranslateRendererSdl2ScancodeToGame(
                  Sdl2PhysicalScancode::RIGHT_ALT) ==
                  RendererGameKey::RIGHT_ALT &&
              TranslateRendererSdl2ScancodeToGame(
                  Sdl2PhysicalScancode::F16) ==
                  RendererGameKey::UNASSIGNED,
          "portable SDL2 to legacy key mapping changed");

  FakeTarget target;
  RendererOgre14InputAdapter adapter(target);
  Require(adapter.ActivateTarget() && adapter.ActivateTarget() &&
              target.activations == 1,
          "explicit product activation was not idempotent");
  const RendererOgre14InputApplyResult first =
      adapter.ApplyValidatedBatch(2U, KeyboardMouseBatch());
  Require(first.ok() && first.reverse_sequence == 2U &&
              first.issued_first_event_id == 1U &&
              first.issued_last_event_id == 9U &&
              first.resolved_through_event_id == 9U &&
              first.applied_through_event_id == 9U &&
              first.ignored_unmapped_scancodes == 2U &&
              target.activations == 1 && target.reconciliations == 1,
          "issued/resolved/applied lineage or activation changed");
  Require(target.keys.size() == 1U &&
              target.keys[0U] == std::make_pair(
                  RendererGameKey::A, true) &&
              target.mouse_moves == 1 &&
              target.mouse_motion ==
                  std::array<std::int32_t, 4U>{120, 75, -3, 4} &&
              target.mouse_buttons.size() == 1U &&
              target.wheel_x == -0.5F && target.wheel_y == 2.0F &&
              target.text == "Beam \xf0\x9f\x9a\x99" &&
              target.focus && target.close_requests == 1,
          "ordered input transitions changed");
  Require(target.state.pressed_keys ==
                  std::vector<RendererGameKey>{
                      RendererGameKey::A} &&
              target.state.pressed_mouse_buttons ==
                  std::vector<RendererGameMouseButton>{
                      RendererGameMouseButton::RIGHT} &&
              target.state.mouse_x_pixels == 120 &&
              target.state.mouse_y_pixels == 75 &&
              target.state.mouse_delta_x_pixels == -3 &&
              target.state.mouse_delta_y_pixels == 4 &&
              target.state.window_close_requested,
          "authoritative keyboard/mouse reconciliation changed");

  const RendererOgre14InputApplyResult duplicate =
      adapter.ApplyValidatedBatch(2U, KeyboardMouseBatch());
  Require(duplicate.status ==
              RendererOgre14InputApplyStatus::REJECTED_SEQUENCE &&
              target.reconciliations == 1,
          "duplicate reverse sequence mutated target state");

  const RendererOgre14InputApplyResult devices =
      adapter.ApplyValidatedBatch(3U, DeviceBatch());
  Require(devices.ok() && target.reconciliations == 2 &&
              target.state.joysticks.size() == 2U &&
              target.state.joysticks[0U].slot == 0U &&
              target.state.joysticks[0U].axes_absolute[0U] == -32768 &&
              target.state.joysticks[0U].buttons[0U] &&
              target.state.joysticks[1U].slot == 1U &&
              target.state.joysticks[1U].axes_absolute[0U] == 32767 &&
              target.state.joysticks[1U].axes_relative[1U] > 0 &&
              target.state.joysticks[1U].buttons[2U] &&
              target.state.joysticks[1U].hats[0U] ==
                  static_cast<std::uint8_t>(Sdl2HatState::RIGHT_UP) &&
              target.state.joysticks[1U].sliders[0U].first == 32767 &&
              target.state.joysticks[1U].sliders[0U].second == -32768,
          "gamepad/raw reconciliation did not preserve legacy semantics");

  const RendererOgre14InputApplyResult saturated =
      adapter.ApplyValidatedBatch(4U, SaturatingDeltaBatch());
  Require(saturated.ok() &&
              target.state.mouse_delta_x_pixels ==
                  (std::numeric_limits<std::int32_t>::max)() &&
              target.state.mouse_delta_y_pixels ==
                  (std::numeric_limits<std::int32_t>::min)() &&
              target.state.wheel_delta_x == 8192.0F &&
              target.state.wheel_delta_y == -8192.0F,
          "bounded batch delta accumulation did not saturate safely");
}

void TestTargetFailuresDoNotAdvanceAppliedLineage() {
  FakeTarget activation_failure;
  activation_failure.activation_succeeds = false;
  RendererOgre14InputAdapter activation_adapter(activation_failure);
  const RendererOgre14InputApplyResult activation =
      activation_adapter.ApplyValidatedBatch(1U, KeyboardMouseBatch());
  Require(activation.status == RendererOgre14InputApplyStatus::FAILED_TARGET &&
              activation_adapter.last_reverse_sequence() == 0U &&
              activation_failure.reconciliations == 0,
          "failed activation advanced applied lineage");

  FakeTarget reconciliation_failure;
  reconciliation_failure.reconciliation_succeeds = false;
  RendererOgre14InputAdapter reconciliation_adapter(reconciliation_failure);
  const RendererOgre14InputApplyResult reconciliation =
      reconciliation_adapter.ApplyValidatedBatch(1U, KeyboardMouseBatch());
  Require(reconciliation.status ==
              RendererOgre14InputApplyStatus::FAILED_TARGET &&
              reconciliation_adapter.last_reverse_sequence() == 0U &&
              reconciliation_adapter.applied_through_event_id() == 0U,
          "failed reconciliation advanced applied lineage");
}

} // namespace

int main() {
  TestRendererNeutralSdlMapping();
  TestMappingsTransitionsReconciliationAndLineage();
  TestTargetFailuresDoNotAdvanceAppliedLineage();
  return EXIT_SUCCESS;
}
