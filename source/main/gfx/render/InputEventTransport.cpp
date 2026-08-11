/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "InputEventTransport.h"

#include "RenderTransportDetail.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace RoR::Render {
namespace {

using TransportDetail::AllocationBudget;
using TransportDetail::WireReader;
using TransportDetail::WireWriter;

constexpr std::size_t kMinimumEventBytes = 17U;
bool IsKnownClockDomain(InputTransportClockDomain domain) noexcept {
  return domain == InputTransportClockDomain::HOST_MONOTONIC_NANOSECONDS;
}

bool IsKnownDigitalState(InputTransportDigitalState state) noexcept {
  return state == InputTransportDigitalState::RELEASED ||
         state == InputTransportDigitalState::PRESSED;
}

bool IsKnownFocusState(InputTransportFocusState state) noexcept {
  return state == InputTransportFocusState::LOST ||
         state == InputTransportFocusState::GAINED;
}

bool IsKnownConnectionState(
    InputTransportDeviceConnectionState state) noexcept {
  return state == InputTransportDeviceConnectionState::DISCONNECTED ||
         state == InputTransportDeviceConnectionState::CONNECTED;
}

bool IsKnownMouseButton(Sdl2MouseButton button) noexcept {
  const auto raw = static_cast<std::uint8_t>(button);
  return raw >= static_cast<std::uint8_t>(Sdl2MouseButton::LEFT) &&
         raw <= static_cast<std::uint8_t>(Sdl2MouseButton::X2);
}

bool IsKnownWheelDirection(Sdl2MouseWheelDirection direction) noexcept {
  return direction == Sdl2MouseWheelDirection::NORMAL ||
         direction == Sdl2MouseWheelDirection::FLIPPED;
}

bool IsKnownGamepadButton(Sdl2GamepadButton button) noexcept {
  return static_cast<std::uint8_t>(button) <=
         static_cast<std::uint8_t>(Sdl2GamepadButton::TOUCHPAD);
}

bool IsKnownGamepadAxis(Sdl2GamepadAxis axis) noexcept {
  return static_cast<std::uint8_t>(axis) <=
         static_cast<std::uint8_t>(Sdl2GamepadAxis::TRIGGER_RIGHT);
}

bool IsValidGamepadAxisValue(Sdl2GamepadAxis axis,
                             std::int16_t value) noexcept {
  return IsKnownGamepadAxis(axis) &&
         ((axis != Sdl2GamepadAxis::TRIGGER_LEFT &&
           axis != Sdl2GamepadAxis::TRIGGER_RIGHT) ||
          value >= 0);
}

bool IsKnownRawDeviceClass(InputTransportRawDeviceClass device_class) noexcept {
  switch (device_class) {
  case InputTransportRawDeviceClass::JOYSTICK:
  case InputTransportRawDeviceClass::WHEEL:
  case InputTransportRawDeviceClass::FLIGHT_STICK:
  case InputTransportRawDeviceClass::THROTTLE:
  case InputTransportRawDeviceClass::OTHER:
    return true;
  }
  return false;
}

bool IsKnownRawAxisMode(InputTransportRawAxisMode mode) noexcept {
  return mode == InputTransportRawAxisMode::ABSOLUTE ||
         mode == InputTransportRawAxisMode::RELATIVE;
}

bool IsKnownHatState(Sdl2HatState state) noexcept {
  switch (state) {
  case Sdl2HatState::CENTERED:
  case Sdl2HatState::UP:
  case Sdl2HatState::RIGHT:
  case Sdl2HatState::RIGHT_UP:
  case Sdl2HatState::DOWN:
  case Sdl2HatState::RIGHT_DOWN:
  case Sdl2HatState::LEFT:
  case Sdl2HatState::LEFT_UP:
  case Sdl2HatState::LEFT_DOWN:
    return true;
  }
  return false;
}

bool IsPressed(InputTransportDigitalState state) noexcept {
  return state == InputTransportDigitalState::PRESSED;
}

bool IsConnected(InputTransportDeviceConnectionState state) noexcept {
  return state == InputTransportDeviceConnectionState::CONNECTED;
}

bool AddI16(WireWriter &writer, std::int16_t value) {
  return writer.AddU16(static_cast<std::uint16_t>(value));
}

bool AddI32(WireWriter &writer, std::int32_t value) {
  return writer.AddU32(static_cast<std::uint32_t>(value));
}

bool ReadI16(WireReader &reader, std::int16_t &value) noexcept {
  std::uint16_t encoded = 0U;
  if (!reader.ReadU16(encoded)) {
    return false;
  }
  if (encoded <=
      static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)())) {
    value = static_cast<std::int16_t>(encoded);
  } else {
    const std::uint16_t distance = static_cast<std::uint16_t>(
        (std::numeric_limits<std::uint16_t>::max)() - encoded);
    value = static_cast<std::int16_t>(-1 - static_cast<std::int32_t>(distance));
  }
  return true;
}

bool ReadI32(WireReader &reader, std::int32_t &value) noexcept {
  std::uint32_t encoded = 0U;
  if (!reader.ReadU32(encoded)) {
    return false;
  }
  if (encoded <=
      static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
    value = static_cast<std::int32_t>(encoded);
  } else {
    const std::uint32_t distance =
        (std::numeric_limits<std::uint32_t>::max)() - encoded;
    value =
        static_cast<std::int32_t>(-1LL - static_cast<std::int64_t>(distance));
  }
  return true;
}

bool DigestHasIdentity(const std::array<std::uint8_t, 32U> &digest) noexcept {
  for (const std::uint8_t byte : digest) {
    if (byte != 0U) {
      return true;
    }
  }
  return false;
}

bool ValidateRawAxisDescriptor(const InputTransportRawAxisDescriptor &axis,
                               std::uint16_t expected_index) noexcept {
  return axis.index == expected_index && IsKnownRawAxisMode(axis.mode) &&
         axis.logical_minimum < axis.logical_maximum &&
         axis.center >= axis.logical_minimum &&
         axis.center <= axis.logical_maximum &&
         axis.deadzone_minimum >= axis.logical_minimum &&
         axis.deadzone_minimum <= axis.center &&
         axis.deadzone_maximum >= axis.center &&
         axis.deadzone_maximum <= axis.logical_maximum &&
         (axis.mode != InputTransportRawAxisMode::RELATIVE ||
          (axis.logical_minimum <= 0 && axis.logical_maximum >= 0 &&
           axis.center == 0));
}

bool ValidateRawSliderDescriptor(
    const InputTransportRawSliderDescriptor &slider,
    std::uint16_t expected_index) noexcept {
  return slider.index == expected_index &&
         ValidateRawAxisDescriptor(slider.x_axis, 0U) &&
         ValidateRawAxisDescriptor(slider.y_axis, 1U) &&
         slider.x_axis.mode == InputTransportRawAxisMode::ABSOLUTE &&
         slider.y_axis.mode == InputTransportRawAxisMode::ABSOLUTE;
}

bool ValidateRawDeviceDescriptor(
    const InputTransportRawDeviceDescriptor &descriptor) noexcept {
  if (descriptor.device_id == 0U || descriptor.connection_generation == 0U ||
      !IsKnownRawDeviceClass(descriptor.device_class) ||
      !DigestHasIdentity(descriptor.name_sha256) ||
      descriptor.axes.size() > kInputEventTransportMaximumRawAxes ||
      descriptor.button_count > kInputEventTransportMaximumRawButtons ||
      descriptor.hat_count > kInputEventTransportMaximumRawHats ||
      descriptor.sliders.size() > kInputEventTransportMaximumRawSliders) {
    return false;
  }
  for (std::size_t index = 0U; index < descriptor.axes.size(); ++index) {
    if (!ValidateRawAxisDescriptor(descriptor.axes[index],
                                   static_cast<std::uint16_t>(index))) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < descriptor.sliders.size(); ++index) {
    if (!ValidateRawSliderDescriptor(descriptor.sliders[index],
                                     static_cast<std::uint16_t>(index))) {
      return false;
    }
  }
  return true;
}

bool EqualRawAxisDescriptor(const InputTransportRawAxisDescriptor &lhs,
                            const InputTransportRawAxisDescriptor &rhs) {
  return lhs.index == rhs.index && lhs.mode == rhs.mode &&
         lhs.logical_minimum == rhs.logical_minimum &&
         lhs.logical_maximum == rhs.logical_maximum &&
         lhs.center == rhs.center &&
         lhs.deadzone_minimum == rhs.deadzone_minimum &&
         lhs.deadzone_maximum == rhs.deadzone_maximum;
}

bool EqualRawSliderDescriptor(const InputTransportRawSliderDescriptor &lhs,
                              const InputTransportRawSliderDescriptor &rhs) {
  return lhs.index == rhs.index &&
         EqualRawAxisDescriptor(lhs.x_axis, rhs.x_axis) &&
         EqualRawAxisDescriptor(lhs.y_axis, rhs.y_axis);
}

bool EqualRawDeviceDescriptor(const InputTransportRawDeviceDescriptor &lhs,
                              const InputTransportRawDeviceDescriptor &rhs) {
  if (lhs.device_id != rhs.device_id ||
      lhs.connection_generation != rhs.connection_generation ||
      lhs.device_class != rhs.device_class || lhs.guid != rhs.guid ||
      lhs.vendor_id != rhs.vendor_id || lhs.product_id != rhs.product_id ||
      lhs.product_version != rhs.product_version ||
      lhs.name_sha256 != rhs.name_sha256 ||
      lhs.button_count != rhs.button_count || lhs.hat_count != rhs.hat_count ||
      lhs.axes.size() != rhs.axes.size() ||
      lhs.sliders.size() != rhs.sliders.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.axes.size(); ++index) {
    if (!EqualRawAxisDescriptor(lhs.axes[index], rhs.axes[index])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < lhs.sliders.size(); ++index) {
    if (!EqualRawSliderDescriptor(lhs.sliders[index], rhs.sliders[index])) {
      return false;
    }
  }
  return true;
}

const InputTransportGamepadReconciliationState *
FindGamepad(const InputTransportReconciliationSnapshot &snapshot,
            std::uint64_t device_id, std::uint64_t generation) noexcept {
  for (const auto &gamepad : snapshot.gamepads) {
    if (gamepad.device_id == device_id &&
        gamepad.connection_generation == generation) {
      return &gamepad;
    }
  }
  return nullptr;
}

const InputTransportRawDeviceReconciliationState *
FindRawDevice(const InputTransportReconciliationSnapshot &snapshot,
              std::uint64_t device_id, std::uint64_t generation) noexcept {
  for (const auto &device : snapshot.raw_devices) {
    if (device.descriptor.device_id == device_id &&
        device.descriptor.connection_generation == generation) {
      return &device;
    }
  }
  return nullptr;
}

bool ContainsScancode(const InputTransportReconciliationSnapshot &snapshot,
                      Sdl2PhysicalScancode scancode) noexcept {
  return std::binary_search(snapshot.pressed_scancodes.begin(),
                            snapshot.pressed_scancodes.end(), scancode);
}

bool ContainsMouseButton(const InputTransportReconciliationSnapshot &snapshot,
                         Sdl2MouseButton button) noexcept {
  return std::binary_search(snapshot.pressed_mouse_buttons.begin(),
                            snapshot.pressed_mouse_buttons.end(), button);
}

bool ContainsGamepadButton(
    const InputTransportGamepadReconciliationState &gamepad,
    Sdl2GamepadButton button) noexcept {
  return std::binary_search(gamepad.pressed_buttons.begin(),
                            gamepad.pressed_buttons.end(), button);
}

bool ContainsRawButton(const InputTransportRawDeviceReconciliationState &device,
                       std::uint16_t button) noexcept {
  return std::binary_search(device.pressed_buttons.begin(),
                            device.pressed_buttons.end(), button);
}

bool ValidateReconciliation(
    const InputTransportReconciliationSnapshot &snapshot) noexcept {
  if (!IsKnownFocusState(snapshot.focus) ||
      snapshot.pressed_scancodes.size() >
          kInputEventTransportMaximumPressedScancodes ||
      snapshot.pressed_mouse_buttons.size() >
          kInputEventTransportMaximumPressedMouseButtons ||
      snapshot.gamepads.size() > kInputEventTransportMaximumGamepads ||
      snapshot.raw_devices.size() > kInputEventTransportMaximumRawDevices ||
      snapshot.gamepads.size() + snapshot.raw_devices.size() >
          kInputEventTransportMaximumDevices) {
    return false;
  }

  for (std::size_t index = 0U; index < snapshot.pressed_scancodes.size();
       ++index) {
    if (!IsKnownSdl2PhysicalScancode(snapshot.pressed_scancodes[index]) ||
        (index != 0U && !(snapshot.pressed_scancodes[index - 1U] <
                          snapshot.pressed_scancodes[index]))) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < snapshot.pressed_mouse_buttons.size();
       ++index) {
    if (!IsKnownMouseButton(snapshot.pressed_mouse_buttons[index]) ||
        (index != 0U && !(snapshot.pressed_mouse_buttons[index - 1U] <
                          snapshot.pressed_mouse_buttons[index]))) {
      return false;
    }
  }

  std::uint64_t previous_device_id = 0U;
  bool has_previous_device = false;
  for (const auto &gamepad : snapshot.gamepads) {
    if (gamepad.device_id == 0U || gamepad.connection_generation == 0U ||
        (has_previous_device && gamepad.device_id <= previous_device_id) ||
        gamepad.pressed_buttons.size() >
            kInputEventTransportMaximumGamepadButtons) {
      return false;
    }
    for (std::size_t index = 0U; index < gamepad.pressed_buttons.size();
         ++index) {
      if (!IsKnownGamepadButton(gamepad.pressed_buttons[index]) ||
          (index != 0U && !(gamepad.pressed_buttons[index - 1U] <
                            gamepad.pressed_buttons[index]))) {
        return false;
      }
    }
    if (gamepad.axes[static_cast<std::size_t>(Sdl2GamepadAxis::TRIGGER_LEFT)] <
            0 ||
        gamepad.axes[static_cast<std::size_t>(Sdl2GamepadAxis::TRIGGER_RIGHT)] <
            0) {
      return false;
    }
    previous_device_id = gamepad.device_id;
    has_previous_device = true;
  }

  previous_device_id = 0U;
  has_previous_device = false;
  for (const auto &raw : snapshot.raw_devices) {
    const auto &descriptor = raw.descriptor;
    if (!ValidateRawDeviceDescriptor(descriptor) ||
        (has_previous_device && descriptor.device_id <= previous_device_id) ||
        raw.pressed_buttons.size() > descriptor.button_count ||
        raw.axes.size() != descriptor.axes.size() ||
        raw.hats.size() != descriptor.hat_count ||
        raw.sliders.size() != descriptor.sliders.size()) {
      return false;
    }
    if (std::any_of(snapshot.gamepads.begin(), snapshot.gamepads.end(),
                    [&descriptor](const auto &gamepad) {
                      return gamepad.device_id == descriptor.device_id;
                    })) {
      return false;
    }
    for (std::size_t index = 0U; index < raw.pressed_buttons.size(); ++index) {
      if (raw.pressed_buttons[index] >= descriptor.button_count ||
          (index != 0U &&
           raw.pressed_buttons[index - 1U] >= raw.pressed_buttons[index])) {
        return false;
      }
    }
    for (std::size_t index = 0U; index < raw.axes.size(); ++index) {
      if (raw.axes[index] < descriptor.axes[index].logical_minimum ||
          raw.axes[index] > descriptor.axes[index].logical_maximum) {
        return false;
      }
    }
    for (const Sdl2HatState hat : raw.hats) {
      if (!IsKnownHatState(hat)) {
        return false;
      }
    }
    for (std::size_t index = 0U; index < raw.sliders.size(); ++index) {
      const auto &state = raw.sliders[index];
      const auto &descriptor_state = descriptor.sliders[index];
      if (state.x < descriptor_state.x_axis.logical_minimum ||
          state.x > descriptor_state.x_axis.logical_maximum ||
          state.y < descriptor_state.y_axis.logical_minimum ||
          state.y > descriptor_state.y_axis.logical_maximum) {
        return false;
      }
    }
    previous_device_id = descriptor.device_id;
    has_previous_device = true;
  }

  if (snapshot.focus == InputTransportFocusState::LOST) {
    if (!snapshot.pressed_scancodes.empty() ||
        !snapshot.pressed_mouse_buttons.empty()) {
      return false;
    }
    for (const auto &gamepad : snapshot.gamepads) {
      if (!gamepad.pressed_buttons.empty() ||
          std::any_of(gamepad.axes.begin(), gamepad.axes.end(),
                      [](std::int16_t value) { return value != 0; })) {
        return false;
      }
    }
    for (const auto &raw : snapshot.raw_devices) {
      if (!raw.pressed_buttons.empty()) {
        return false;
      }
      for (std::size_t index = 0U; index < raw.axes.size(); ++index) {
        const auto &axis = raw.descriptor.axes[index];
        const std::int32_t neutral =
            axis.mode == InputTransportRawAxisMode::ABSOLUTE ? axis.center : 0;
        if (raw.axes[index] != neutral) {
          return false;
        }
      }
      if (std::any_of(raw.hats.begin(), raw.hats.end(), [](Sdl2HatState state) {
            return state != Sdl2HatState::CENTERED;
          })) {
        return false;
      }
      for (std::size_t index = 0U; index < raw.sliders.size(); ++index) {
        if (raw.sliders[index].x !=
                raw.descriptor.sliders[index].x_axis.center ||
            raw.sliders[index].y !=
                raw.descriptor.sliders[index].y_axis.center) {
          return false;
        }
      }
    }
  }
  return true;
}

struct DeviceTerminalState {
  enum class Family : std::uint8_t { GAMEPAD, RAW };

  Family family = Family::GAMEPAD;
  std::uint64_t device_id = 0U;
  std::uint64_t generation = 0U;
  bool connection_seen = false;
  InputTransportDeviceConnectionState connection =
      InputTransportDeviceConnectionState::DISCONNECTED;
  const InputTransportRawDeviceDescriptor *raw_descriptor = nullptr;
  std::array<bool, kInputEventTransportMaximumGamepadButtons>
      gamepad_button_seen{};
  std::array<bool, kInputEventTransportMaximumGamepadButtons>
      gamepad_button_pressed{};
  std::array<bool, kInputEventTransportGamepadAxisCount> gamepad_axis_seen{};
  std::array<std::int16_t, kInputEventTransportGamepadAxisCount>
      gamepad_axis_values{};
  std::array<bool, kInputEventTransportMaximumRawButtons> raw_button_seen{};
  std::array<bool, kInputEventTransportMaximumRawButtons> raw_button_pressed{};
  std::array<bool, kInputEventTransportMaximumRawAxes> raw_axis_seen{};
  std::array<std::int32_t, kInputEventTransportMaximumRawAxes>
      raw_axis_values{};
  std::array<bool, kInputEventTransportMaximumRawHats> raw_hat_seen{};
  std::array<Sdl2HatState, kInputEventTransportMaximumRawHats> raw_hat_values{};
  std::array<bool, kInputEventTransportMaximumRawSliders> raw_slider_seen{};
  std::array<InputTransportRawSliderState,
             kInputEventTransportMaximumRawSliders>
      raw_slider_values{};
};

DeviceTerminalState *FindOrAddDeviceTerminal(
    std::array<DeviceTerminalState,
               kInputEventTransportMaximumDeviceGenerationsPerBatch> &states,
    std::size_t &count, DeviceTerminalState::Family family,
    std::uint64_t device_id, std::uint64_t generation) noexcept {
  for (std::size_t index = 0U; index < count; ++index) {
    if (states[index].device_id == device_id &&
        states[index].generation == generation) {
      return states[index].family == family ? &states[index] : nullptr;
    }
  }
  if (count == states.size() || device_id == 0U || generation == 0U) {
    return nullptr;
  }
  DeviceTerminalState &state = states[count++];
  state.family = family;
  state.device_id = device_id;
  state.generation = generation;
  return &state;
}

void ResetTerminalComponents(DeviceTerminalState &state) noexcept {
  state.gamepad_button_seen.fill(false);
  state.gamepad_axis_seen.fill(false);
  state.raw_button_seen.fill(false);
  state.raw_axis_seen.fill(false);
  state.raw_hat_seen.fill(false);
  state.raw_slider_seen.fill(false);
}

bool ValidateEventTerminalReconciliation(
    const InputTransportBatch &batch) noexcept {
  std::array<bool, 512U> key_seen{};
  std::array<InputTransportDigitalState, 512U> key_states{};
  std::array<bool, 6U> mouse_seen{};
  std::array<InputTransportDigitalState, 6U> mouse_states{};
  bool focus_seen = false;
  InputTransportFocusState focus_state = InputTransportFocusState::LOST;
  bool close_seen = false;
  std::array<DeviceTerminalState,
             kInputEventTransportMaximumDeviceGenerationsPerBatch>
      devices{};
  std::size_t device_count = 0U;

  for (const InputTransportEvent &event : batch.events) {
    if (const auto *key =
            std::get_if<InputTransportKeyboardKeyEvent>(&event.payload)) {
      const std::size_t index = static_cast<std::size_t>(key->scancode);
      key_seen[index] = true;
      key_states[index] = key->state;
      continue;
    }
    if (const auto *button =
            std::get_if<InputTransportMouseButtonEvent>(&event.payload)) {
      const std::size_t index = static_cast<std::size_t>(button->button);
      mouse_seen[index] = true;
      mouse_states[index] = button->state;
      continue;
    }
    if (const auto *focus =
            std::get_if<InputTransportFocusEvent>(&event.payload)) {
      focus_seen = true;
      focus_state = focus->state;
      if (focus->state == InputTransportFocusState::LOST) {
        key_seen.fill(false);
        mouse_seen.fill(false);
        for (std::size_t index = 0U; index < device_count; ++index) {
          ResetTerminalComponents(devices[index]);
        }
      }
      continue;
    }
    if (std::holds_alternative<InputTransportWindowCloseEvent>(event.payload)) {
      close_seen = true;
      continue;
    }
    if (const auto *connection =
            std::get_if<InputTransportGamepadConnectionEvent>(&event.payload)) {
      DeviceTerminalState *state = FindOrAddDeviceTerminal(
          devices, device_count, DeviceTerminalState::Family::GAMEPAD,
          connection->device_id, connection->connection_generation);
      if (state == nullptr) {
        return false;
      }
      state->connection_seen = true;
      state->connection = connection->state;
      ResetTerminalComponents(*state);
      continue;
    }
    if (const auto *button =
            std::get_if<InputTransportGamepadButtonEvent>(&event.payload)) {
      DeviceTerminalState *state = FindOrAddDeviceTerminal(
          devices, device_count, DeviceTerminalState::Family::GAMEPAD,
          button->device_id, button->connection_generation);
      if (state == nullptr) {
        return false;
      }
      const std::size_t index = static_cast<std::size_t>(button->button);
      state->gamepad_button_seen[index] = true;
      state->gamepad_button_pressed[index] = IsPressed(button->state);
      continue;
    }
    if (const auto *axis =
            std::get_if<InputTransportGamepadAxisEvent>(&event.payload)) {
      DeviceTerminalState *state = FindOrAddDeviceTerminal(
          devices, device_count, DeviceTerminalState::Family::GAMEPAD,
          axis->device_id, axis->connection_generation);
      if (state == nullptr) {
        return false;
      }
      const std::size_t index = static_cast<std::size_t>(axis->axis);
      state->gamepad_axis_seen[index] = true;
      state->gamepad_axis_values[index] = axis->value;
      continue;
    }
    if (const auto *connection =
            std::get_if<InputTransportRawDeviceConnectionEvent>(
                &event.payload)) {
      DeviceTerminalState *state = FindOrAddDeviceTerminal(
          devices, device_count, DeviceTerminalState::Family::RAW,
          connection->descriptor.device_id,
          connection->descriptor.connection_generation);
      if (state == nullptr) {
        return false;
      }
      if (state->raw_descriptor != nullptr &&
          !EqualRawDeviceDescriptor(*state->raw_descriptor,
                                    connection->descriptor)) {
        return false;
      }
      state->connection_seen = true;
      state->connection = connection->state;
      state->raw_descriptor = &connection->descriptor;
      ResetTerminalComponents(*state);
      continue;
    }
    if (const auto *button =
            std::get_if<InputTransportRawButtonEvent>(&event.payload)) {
      DeviceTerminalState *state = FindOrAddDeviceTerminal(
          devices, device_count, DeviceTerminalState::Family::RAW,
          button->device_id, button->connection_generation);
      if (state == nullptr ||
          button->button_index >= kInputEventTransportMaximumRawButtons) {
        return false;
      }
      state->raw_button_seen[button->button_index] = true;
      state->raw_button_pressed[button->button_index] =
          IsPressed(button->state);
      continue;
    }
    if (const auto *axis =
            std::get_if<InputTransportRawAxisEvent>(&event.payload)) {
      DeviceTerminalState *state = FindOrAddDeviceTerminal(
          devices, device_count, DeviceTerminalState::Family::RAW,
          axis->device_id, axis->connection_generation);
      if (state == nullptr ||
          axis->axis_index >= kInputEventTransportMaximumRawAxes) {
        return false;
      }
      state->raw_axis_seen[axis->axis_index] = true;
      state->raw_axis_values[axis->axis_index] = axis->value;
      continue;
    }
    if (const auto *hat =
            std::get_if<InputTransportRawHatEvent>(&event.payload)) {
      DeviceTerminalState *state = FindOrAddDeviceTerminal(
          devices, device_count, DeviceTerminalState::Family::RAW,
          hat->device_id, hat->connection_generation);
      if (state == nullptr ||
          hat->hat_index >= kInputEventTransportMaximumRawHats) {
        return false;
      }
      state->raw_hat_seen[hat->hat_index] = true;
      state->raw_hat_values[hat->hat_index] = hat->state;
      continue;
    }
    if (const auto *slider =
            std::get_if<InputTransportRawSliderEvent>(&event.payload)) {
      DeviceTerminalState *state = FindOrAddDeviceTerminal(
          devices, device_count, DeviceTerminalState::Family::RAW,
          slider->device_id, slider->connection_generation);
      if (state == nullptr ||
          slider->slider_index >= kInputEventTransportMaximumRawSliders) {
        return false;
      }
      state->raw_slider_seen[slider->slider_index] = true;
      state->raw_slider_values[slider->slider_index] = {slider->x, slider->y};
    }
  }

  for (std::size_t index = 0U; index < key_seen.size(); ++index) {
    if (key_seen[index] &&
        ContainsScancode(batch.reconciliation,
                         static_cast<Sdl2PhysicalScancode>(index)) !=
            IsPressed(key_states[index])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < mouse_seen.size(); ++index) {
    if (mouse_seen[index] &&
        ContainsMouseButton(batch.reconciliation,
                            static_cast<Sdl2MouseButton>(index)) !=
            IsPressed(mouse_states[index])) {
      return false;
    }
  }
  if ((focus_seen && batch.reconciliation.focus != focus_state) ||
      (close_seen && !batch.reconciliation.window_close_requested)) {
    return false;
  }

  for (std::size_t device_index = 0U; device_index < device_count;
       ++device_index) {
    const DeviceTerminalState &terminal = devices[device_index];
    if (terminal.family == DeviceTerminalState::Family::GAMEPAD) {
      const auto *gamepad = FindGamepad(
          batch.reconciliation, terminal.device_id, terminal.generation);
      if (terminal.connection_seen &&
          (gamepad != nullptr) != IsConnected(terminal.connection)) {
        return false;
      }
      const bool has_component =
          std::any_of(terminal.gamepad_button_seen.begin(),
                      terminal.gamepad_button_seen.end(),
                      [](bool value) { return value; }) ||
          std::any_of(terminal.gamepad_axis_seen.begin(),
                      terminal.gamepad_axis_seen.end(),
                      [](bool value) { return value; });
      if (has_component && gamepad == nullptr) {
        return false;
      }
      if (gamepad != nullptr) {
        for (std::size_t index = 0U;
             index < terminal.gamepad_button_seen.size(); ++index) {
          if (terminal.gamepad_button_seen[index] &&
              ContainsGamepadButton(*gamepad,
                                    static_cast<Sdl2GamepadButton>(index)) !=
                  terminal.gamepad_button_pressed[index]) {
            return false;
          }
        }
        for (std::size_t index = 0U; index < terminal.gamepad_axis_seen.size();
             ++index) {
          if (terminal.gamepad_axis_seen[index] &&
              gamepad->axes[index] != terminal.gamepad_axis_values[index]) {
            return false;
          }
        }
      }
      continue;
    }

    const auto *raw = FindRawDevice(batch.reconciliation, terminal.device_id,
                                    terminal.generation);
    if (terminal.connection_seen &&
        (raw != nullptr) != IsConnected(terminal.connection)) {
      return false;
    }
    const bool has_component =
        std::any_of(terminal.raw_button_seen.begin(),
                    terminal.raw_button_seen.end(),
                    [](bool value) { return value; }) ||
        std::any_of(terminal.raw_axis_seen.begin(),
                    terminal.raw_axis_seen.end(),
                    [](bool value) { return value; }) ||
        std::any_of(terminal.raw_hat_seen.begin(), terminal.raw_hat_seen.end(),
                    [](bool value) { return value; }) ||
        std::any_of(terminal.raw_slider_seen.begin(),
                    terminal.raw_slider_seen.end(),
                    [](bool value) { return value; });
    if (has_component && raw == nullptr) {
      return false;
    }
    if (raw == nullptr) {
      continue;
    }
    if (terminal.connection_seen && terminal.raw_descriptor != nullptr &&
        !EqualRawDeviceDescriptor(*terminal.raw_descriptor, raw->descriptor)) {
      return false;
    }
    for (std::size_t index = 0U; index < terminal.raw_button_seen.size();
         ++index) {
      if (terminal.raw_button_seen[index] &&
          (index >= raw->descriptor.button_count ||
           ContainsRawButton(*raw, static_cast<std::uint16_t>(index)) !=
               terminal.raw_button_pressed[index])) {
        return false;
      }
    }
    for (std::size_t index = 0U; index < terminal.raw_axis_seen.size();
         ++index) {
      if (terminal.raw_axis_seen[index] &&
          (index >= raw->axes.size() ||
           raw->axes[index] != terminal.raw_axis_values[index])) {
        return false;
      }
    }
    for (std::size_t index = 0U; index < terminal.raw_hat_seen.size();
         ++index) {
      if (terminal.raw_hat_seen[index] &&
          (index >= raw->hats.size() ||
           raw->hats[index] != terminal.raw_hat_values[index])) {
        return false;
      }
    }
    for (std::size_t index = 0U; index < terminal.raw_slider_seen.size();
         ++index) {
      if (terminal.raw_slider_seen[index] &&
          (index >= raw->sliders.size() ||
           raw->sliders[index].x != terminal.raw_slider_values[index].x ||
           raw->sliders[index].y != terminal.raw_slider_values[index].y)) {
        return false;
      }
    }
  }
  return true;
}

RenderTransportStatus ValidateEvent(const InputTransportEvent &event,
                                    std::uint64_t &total_text_bytes) noexcept {
  if (event.payload.valueless_by_exception()) {
    return RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *key =
          std::get_if<InputTransportKeyboardKeyEvent>(&event.payload)) {
    if (!IsKnownSdl2PhysicalScancode(key->scancode) ||
        !IsKnownDigitalState(key->state) ||
        (key->repeat && !IsPressed(key->state))) {
      return RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
    }
    return RenderTransportStatus::OK;
  }
  if (std::holds_alternative<InputTransportMouseMotionEvent>(event.payload)) {
    return RenderTransportStatus::OK;
  }
  if (const auto *button =
          std::get_if<InputTransportMouseButtonEvent>(&event.payload)) {
    return IsKnownMouseButton(button->button) &&
                   IsKnownDigitalState(button->state)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *wheel =
          std::get_if<InputTransportMouseWheelEvent>(&event.payload)) {
    return std::isfinite(wheel->delta_x) && std::isfinite(wheel->delta_y) &&
                   std::fabs(wheel->delta_x) <=
                       kInputEventTransportMaximumWheelMagnitude &&
                   std::fabs(wheel->delta_y) <=
                       kInputEventTransportMaximumWheelMagnitude &&
                   IsKnownWheelDirection(wheel->direction)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::NON_CANONICAL_FLOAT;
  }
  if (const auto *connection =
          std::get_if<InputTransportGamepadConnectionEvent>(&event.payload)) {
    return connection->device_id != 0U &&
                   connection->connection_generation != 0U &&
                   IsKnownConnectionState(connection->state)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *button =
          std::get_if<InputTransportGamepadButtonEvent>(&event.payload)) {
    return button->device_id != 0U && button->connection_generation != 0U &&
                   IsKnownGamepadButton(button->button) &&
                   IsKnownDigitalState(button->state)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *axis =
          std::get_if<InputTransportGamepadAxisEvent>(&event.payload)) {
    return axis->device_id != 0U && axis->connection_generation != 0U &&
                   IsValidGamepadAxisValue(axis->axis, axis->value)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *text =
          std::get_if<InputTransportTextInputEvent>(&event.payload)) {
    const std::uint64_t bytes = text->utf8_text.size();
    if (bytes == 0U || bytes > kInputEventTransportMaximumTextBytesPerEvent ||
        total_text_bytes > kInputEventTransportMaximumTotalTextBytes ||
        bytes > kInputEventTransportMaximumTotalTextBytes - total_text_bytes) {
      return RenderTransportStatus::BLOB_LIMIT_EXCEEDED;
    }
    if (!IsValidInputTransportUtf8(text->utf8_text)) {
      return RenderTransportStatus::INVALID_UTF8;
    }
    total_text_bytes += bytes;
    return RenderTransportStatus::OK;
  }
  if (const auto *focus =
          std::get_if<InputTransportFocusEvent>(&event.payload)) {
    return IsKnownFocusState(focus->state)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (std::holds_alternative<InputTransportWindowCloseEvent>(event.payload)) {
    return RenderTransportStatus::OK;
  }
  if (const auto *connection =
          std::get_if<InputTransportRawDeviceConnectionEvent>(&event.payload)) {
    return IsKnownConnectionState(connection->state) &&
                   ValidateRawDeviceDescriptor(connection->descriptor)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *button =
          std::get_if<InputTransportRawButtonEvent>(&event.payload)) {
    return button->device_id != 0U && button->connection_generation != 0U &&
                   button->button_index <
                       kInputEventTransportMaximumRawButtons &&
                   IsKnownDigitalState(button->state)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *axis =
          std::get_if<InputTransportRawAxisEvent>(&event.payload)) {
    return axis->device_id != 0U && axis->connection_generation != 0U &&
                   axis->axis_index < kInputEventTransportMaximumRawAxes
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *hat =
          std::get_if<InputTransportRawHatEvent>(&event.payload)) {
    return hat->device_id != 0U && hat->connection_generation != 0U &&
                   hat->hat_index < kInputEventTransportMaximumRawHats &&
                   IsKnownHatState(hat->state)
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (const auto *slider =
          std::get_if<InputTransportRawSliderEvent>(&event.payload)) {
    return slider->device_id != 0U && slider->connection_generation != 0U &&
                   slider->slider_index < kInputEventTransportMaximumRawSliders
               ? RenderTransportStatus::OK
               : RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  return RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
}

} // namespace

bool IsKnownSdl2PhysicalScancode(Sdl2PhysicalScancode scancode) noexcept {
  const auto value = static_cast<std::uint16_t>(scancode);
  return (value >= 4U && value <= 129U) || (value >= 133U && value <= 164U) ||
         (value >= 176U && value <= 221U) || (value >= 224U && value <= 231U) ||
         (value >= 257U && value <= 290U);
}

bool IsKnownInputTransportEventKind(InputTransportEventKind kind) noexcept {
  switch (kind) {
  case InputTransportEventKind::KEYBOARD_KEY:
  case InputTransportEventKind::MOUSE_MOTION:
  case InputTransportEventKind::MOUSE_BUTTON:
  case InputTransportEventKind::MOUSE_WHEEL:
  case InputTransportEventKind::GAMEPAD_CONNECTION:
  case InputTransportEventKind::GAMEPAD_BUTTON:
  case InputTransportEventKind::GAMEPAD_AXIS:
  case InputTransportEventKind::TEXT_INPUT:
  case InputTransportEventKind::FOCUS:
  case InputTransportEventKind::WINDOW_CLOSE:
  case InputTransportEventKind::RAW_DEVICE_CONNECTION:
  case InputTransportEventKind::RAW_BUTTON:
  case InputTransportEventKind::RAW_AXIS:
  case InputTransportEventKind::RAW_HAT:
  case InputTransportEventKind::RAW_SLIDER:
    return true;
  }
  return false;
}

InputTransportEventKind
InputTransportPayloadKind(const InputTransportEventPayload &payload) noexcept {
  if (std::holds_alternative<InputTransportKeyboardKeyEvent>(payload)) {
    return InputTransportEventKind::KEYBOARD_KEY;
  }
  if (std::holds_alternative<InputTransportMouseMotionEvent>(payload)) {
    return InputTransportEventKind::MOUSE_MOTION;
  }
  if (std::holds_alternative<InputTransportMouseButtonEvent>(payload)) {
    return InputTransportEventKind::MOUSE_BUTTON;
  }
  if (std::holds_alternative<InputTransportMouseWheelEvent>(payload)) {
    return InputTransportEventKind::MOUSE_WHEEL;
  }
  if (std::holds_alternative<InputTransportGamepadConnectionEvent>(payload)) {
    return InputTransportEventKind::GAMEPAD_CONNECTION;
  }
  if (std::holds_alternative<InputTransportGamepadButtonEvent>(payload)) {
    return InputTransportEventKind::GAMEPAD_BUTTON;
  }
  if (std::holds_alternative<InputTransportGamepadAxisEvent>(payload)) {
    return InputTransportEventKind::GAMEPAD_AXIS;
  }
  if (std::holds_alternative<InputTransportTextInputEvent>(payload)) {
    return InputTransportEventKind::TEXT_INPUT;
  }
  if (std::holds_alternative<InputTransportFocusEvent>(payload)) {
    return InputTransportEventKind::FOCUS;
  }
  if (std::holds_alternative<InputTransportWindowCloseEvent>(payload)) {
    return InputTransportEventKind::WINDOW_CLOSE;
  }
  if (std::holds_alternative<InputTransportRawDeviceConnectionEvent>(payload)) {
    return InputTransportEventKind::RAW_DEVICE_CONNECTION;
  }
  if (std::holds_alternative<InputTransportRawButtonEvent>(payload)) {
    return InputTransportEventKind::RAW_BUTTON;
  }
  if (std::holds_alternative<InputTransportRawAxisEvent>(payload)) {
    return InputTransportEventKind::RAW_AXIS;
  }
  if (std::holds_alternative<InputTransportRawHatEvent>(payload)) {
    return InputTransportEventKind::RAW_HAT;
  }
  return std::holds_alternative<InputTransportRawSliderEvent>(payload)
             ? InputTransportEventKind::RAW_SLIDER
             : static_cast<InputTransportEventKind>(0U);
}

bool IsValidInputTransportUtf8(const std::string &text) noexcept {
  const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
  std::size_t index = 0U;
  while (index < text.size()) {
    const unsigned char first = bytes[index++];
    if (first <= 0x7fU) {
      continue;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
      if (index >= text.size() || (bytes[index] & 0xc0U) != 0x80U) {
        return false;
      }
      ++index;
      continue;
    }
    if (first >= 0xe0U && first <= 0xefU) {
      if (index + 1U >= text.size()) {
        return false;
      }
      const unsigned char second = bytes[index];
      const unsigned char third = bytes[index + 1U];
      if ((third & 0xc0U) != 0x80U ||
          (first == 0xe0U && (second < 0xa0U || second > 0xbfU)) ||
          (first == 0xedU && (second < 0x80U || second > 0x9fU)) ||
          (first != 0xe0U && first != 0xedU && (second & 0xc0U) != 0x80U)) {
        return false;
      }
      index += 2U;
      continue;
    }
    if (first >= 0xf0U && first <= 0xf4U) {
      if (index + 2U >= text.size()) {
        return false;
      }
      const unsigned char second = bytes[index];
      const unsigned char third = bytes[index + 1U];
      const unsigned char fourth = bytes[index + 2U];
      if ((third & 0xc0U) != 0x80U || (fourth & 0xc0U) != 0x80U ||
          (first == 0xf0U && (second < 0x90U || second > 0xbfU)) ||
          (first == 0xf4U && (second < 0x80U || second > 0x8fU)) ||
          (first != 0xf0U && first != 0xf4U && (second & 0xc0U) != 0x80U)) {
        return false;
      }
      index += 3U;
      continue;
    }
    return false;
  }
  return true;
}

RenderTransportStatus
ValidateInputTransportBatch(const InputTransportBatch &batch) noexcept {
  if (batch.version != kInputEventTransportPayloadVersion ||
      !IsKnownClockDomain(batch.clock_domain) || batch.clock_origin_id == 0U) {
    return RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
  }
  if (batch.events.size() > kInputEventTransportMaximumEvents) {
    return RenderTransportStatus::COUNT_LIMIT_EXCEEDED;
  }
  std::uint64_t previous_event_id = 0U;
  std::uint64_t previous_timestamp = 0U;
  std::uint64_t total_text_bytes = 0U;
  for (std::size_t index = 0U; index < batch.events.size(); ++index) {
    const InputTransportEvent &event = batch.events[index];
    if (event.event_id == 0U ||
        (index != 0U && event.event_id <= previous_event_id)) {
      return RenderTransportStatus::EVENT_ID_ORDER_VIOLATION;
    }
    if (index != 0U && event.host_timestamp_ns < previous_timestamp) {
      return RenderTransportStatus::TIMESTAMP_ORDER_VIOLATION;
    }
    const RenderTransportStatus event_status =
        ValidateEvent(event, total_text_bytes);
    if (event_status != RenderTransportStatus::OK) {
      return event_status;
    }
    previous_event_id = event.event_id;
    previous_timestamp = event.host_timestamp_ns;
  }
  if (batch.reconciliation.through_event_id < previous_event_id) {
    return RenderTransportStatus::EVENT_ID_ORDER_VIOLATION;
  }
  if (batch.reconciliation.host_timestamp_ns < previous_timestamp) {
    return RenderTransportStatus::TIMESTAMP_ORDER_VIOLATION;
  }
  if (!ValidateReconciliation(batch.reconciliation) ||
      !ValidateEventTerminalReconciliation(batch)) {
    return RenderTransportStatus::RECONCILIATION_MISMATCH;
  }
  return RenderTransportStatus::OK;
}

} // namespace RoR::Render

namespace RoR::Render {
namespace {

bool WritePayload(WireWriter &writer, const InputTransportBatch &batch);
bool ReadPayload(const std::uint8_t *payload, std::size_t payload_size,
                 std::shared_ptr<const InputTransportBatch> &decoded,
                 RenderTransportStatus &status);
InputEventTransportDecodeResult Failure(RenderTransportStatus status);

template <typename Value>
bool SetPressedState(std::vector<Value> &values, Value value, bool pressed,
                     bool repeat = false) {
  const auto found = std::lower_bound(values.begin(), values.end(), value);
  const bool already_pressed = found != values.end() && *found == value;
  if (pressed) {
    if (already_pressed) {
      return repeat;
    }
    if (repeat) {
      return false;
    }
    values.insert(found, value);
    return true;
  }
  if (repeat || !already_pressed) {
    return false;
  }
  values.erase(found);
  return true;
}

InputTransportGamepadReconciliationState *
FindMutableGamepad(InputTransportReconciliationSnapshot &snapshot,
                   std::uint64_t device_id, std::uint64_t generation) {
  for (auto &gamepad : snapshot.gamepads) {
    if (gamepad.device_id == device_id &&
        gamepad.connection_generation == generation) {
      return &gamepad;
    }
  }
  return nullptr;
}

InputTransportRawDeviceReconciliationState *
FindMutableRawDevice(InputTransportReconciliationSnapshot &snapshot,
                     std::uint64_t device_id, std::uint64_t generation) {
  for (auto &device : snapshot.raw_devices) {
    if (device.descriptor.device_id == device_id &&
        device.descriptor.connection_generation == generation) {
      return &device;
    }
  }
  return nullptr;
}

bool HasDeviceId(const InputTransportReconciliationSnapshot &snapshot,
                 std::uint64_t device_id) noexcept {
  return std::any_of(snapshot.gamepads.begin(), snapshot.gamepads.end(),
                     [device_id](const auto &gamepad) {
                       return gamepad.device_id == device_id;
                     }) ||
         std::any_of(snapshot.raw_devices.begin(), snapshot.raw_devices.end(),
                     [device_id](const auto &raw) {
                       return raw.descriptor.device_id == device_id;
                     });
}

InputTransportRawDeviceReconciliationState
MakeNeutralRawState(const InputTransportRawDeviceDescriptor &descriptor) {
  InputTransportRawDeviceReconciliationState state;
  state.descriptor = descriptor;
  state.axes.reserve(descriptor.axes.size());
  for (const auto &axis : descriptor.axes) {
    state.axes.push_back(
        axis.mode == InputTransportRawAxisMode::ABSOLUTE ? axis.center : 0);
  }
  state.hats.assign(descriptor.hat_count, Sdl2HatState::CENTERED);
  state.sliders.reserve(descriptor.sliders.size());
  for (const auto &slider : descriptor.sliders) {
    state.sliders.push_back(InputTransportRawSliderState{slider.x_axis.center,
                                                         slider.y_axis.center});
  }
  return state;
}

void NeutralizeSnapshot(InputTransportReconciliationSnapshot &snapshot) {
  snapshot.pressed_scancodes.clear();
  snapshot.pressed_mouse_buttons.clear();
  for (auto &gamepad : snapshot.gamepads) {
    gamepad.pressed_buttons.clear();
    gamepad.axes.fill(0);
  }
  for (auto &raw : snapshot.raw_devices) {
    raw.pressed_buttons.clear();
    for (std::size_t index = 0U; index < raw.axes.size(); ++index) {
      const auto &axis = raw.descriptor.axes[index];
      raw.axes[index] =
          axis.mode == InputTransportRawAxisMode::ABSOLUTE ? axis.center : 0;
    }
    std::fill(raw.hats.begin(), raw.hats.end(), Sdl2HatState::CENTERED);
    for (std::size_t index = 0U; index < raw.sliders.size(); ++index) {
      raw.sliders[index].x = raw.descriptor.sliders[index].x_axis.center;
      raw.sliders[index].y = raw.descriptor.sliders[index].y_axis.center;
    }
  }
}

bool EqualGamepadState(
    const InputTransportGamepadReconciliationState &lhs,
    const InputTransportGamepadReconciliationState &rhs) noexcept {
  return lhs.device_id == rhs.device_id &&
         lhs.connection_generation == rhs.connection_generation &&
         lhs.pressed_buttons == rhs.pressed_buttons && lhs.axes == rhs.axes;
}

bool EqualRawState(const InputTransportRawDeviceReconciliationState &lhs,
                   const InputTransportRawDeviceReconciliationState &rhs) {
  if (!EqualRawDeviceDescriptor(lhs.descriptor, rhs.descriptor) ||
      lhs.pressed_buttons != rhs.pressed_buttons || lhs.axes != rhs.axes ||
      lhs.hats != rhs.hats || lhs.sliders.size() != rhs.sliders.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.sliders.size(); ++index) {
    if (lhs.sliders[index].x != rhs.sliders[index].x ||
        lhs.sliders[index].y != rhs.sliders[index].y) {
      return false;
    }
  }
  return true;
}

bool EqualReconciliationState(const InputTransportReconciliationSnapshot &lhs,
                              const InputTransportReconciliationSnapshot &rhs) {
  if (lhs.through_event_id != rhs.through_event_id ||
      lhs.host_timestamp_ns != rhs.host_timestamp_ns ||
      lhs.focus != rhs.focus ||
      lhs.window_close_requested != rhs.window_close_requested ||
      lhs.pressed_scancodes != rhs.pressed_scancodes ||
      lhs.pressed_mouse_buttons != rhs.pressed_mouse_buttons ||
      lhs.gamepads.size() != rhs.gamepads.size() ||
      lhs.raw_devices.size() != rhs.raw_devices.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.gamepads.size(); ++index) {
    if (!EqualGamepadState(lhs.gamepads[index], rhs.gamepads[index])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < lhs.raw_devices.size(); ++index) {
    if (!EqualRawState(lhs.raw_devices[index], rhs.raw_devices[index])) {
      return false;
    }
  }
  return true;
}

bool ApplyCompleteEvent(InputTransportReconciliationSnapshot &snapshot,
                        const InputTransportEventPayload &payload) {
  if (const auto *key = std::get_if<InputTransportKeyboardKeyEvent>(&payload)) {
    return SetPressedState(snapshot.pressed_scancodes, key->scancode,
                           IsPressed(key->state), key->repeat);
  }
  if (std::holds_alternative<InputTransportMouseMotionEvent>(payload) ||
      std::holds_alternative<InputTransportMouseWheelEvent>(payload) ||
      std::holds_alternative<InputTransportTextInputEvent>(payload)) {
    return true;
  }
  if (const auto *button =
          std::get_if<InputTransportMouseButtonEvent>(&payload)) {
    return SetPressedState(snapshot.pressed_mouse_buttons, button->button,
                           IsPressed(button->state));
  }
  if (const auto *connection =
          std::get_if<InputTransportGamepadConnectionEvent>(&payload)) {
    auto found = std::lower_bound(
        snapshot.gamepads.begin(), snapshot.gamepads.end(),
        connection->device_id, [](const auto &gamepad, std::uint64_t id) {
          return gamepad.device_id < id;
        });
    if (IsConnected(connection->state)) {
      if (found != snapshot.gamepads.end() &&
          found->device_id == connection->device_id) {
        return false;
      }
      InputTransportGamepadReconciliationState gamepad;
      gamepad.device_id = connection->device_id;
      gamepad.connection_generation = connection->connection_generation;
      snapshot.gamepads.insert(found, std::move(gamepad));
      return true;
    }
    if (found == snapshot.gamepads.end() ||
        found->device_id != connection->device_id ||
        found->connection_generation != connection->connection_generation) {
      return false;
    }
    snapshot.gamepads.erase(found);
    return true;
  }
  if (const auto *button =
          std::get_if<InputTransportGamepadButtonEvent>(&payload)) {
    auto *gamepad = FindMutableGamepad(snapshot, button->device_id,
                                       button->connection_generation);
    return gamepad != nullptr &&
           SetPressedState(gamepad->pressed_buttons, button->button,
                           IsPressed(button->state));
  }
  if (const auto *axis =
          std::get_if<InputTransportGamepadAxisEvent>(&payload)) {
    auto *gamepad = FindMutableGamepad(snapshot, axis->device_id,
                                       axis->connection_generation);
    if (gamepad == nullptr) {
      return false;
    }
    gamepad->axes[static_cast<std::size_t>(axis->axis)] = axis->value;
    return true;
  }
  if (const auto *focus = std::get_if<InputTransportFocusEvent>(&payload)) {
    snapshot.focus = focus->state;
    if (focus->state == InputTransportFocusState::LOST) {
      NeutralizeSnapshot(snapshot);
    }
    return true;
  }
  if (std::holds_alternative<InputTransportWindowCloseEvent>(payload)) {
    snapshot.window_close_requested = true;
    return true;
  }
  if (const auto *connection =
          std::get_if<InputTransportRawDeviceConnectionEvent>(&payload)) {
    const std::uint64_t id = connection->descriptor.device_id;
    auto found = std::lower_bound(snapshot.raw_devices.begin(),
                                  snapshot.raw_devices.end(), id,
                                  [](const auto &raw, std::uint64_t value) {
                                    return raw.descriptor.device_id < value;
                                  });
    if (IsConnected(connection->state)) {
      if (found != snapshot.raw_devices.end() &&
          found->descriptor.device_id == id) {
        return false;
      }
      snapshot.raw_devices.insert(found,
                                  MakeNeutralRawState(connection->descriptor));
      return true;
    }
    if (found == snapshot.raw_devices.end() ||
        found->descriptor.device_id != id ||
        !EqualRawDeviceDescriptor(found->descriptor, connection->descriptor)) {
      return false;
    }
    snapshot.raw_devices.erase(found);
    return true;
  }
  if (const auto *button =
          std::get_if<InputTransportRawButtonEvent>(&payload)) {
    auto *raw = FindMutableRawDevice(snapshot, button->device_id,
                                     button->connection_generation);
    return raw != nullptr &&
           button->button_index < raw->descriptor.button_count &&
           SetPressedState(raw->pressed_buttons, button->button_index,
                           IsPressed(button->state));
  }
  if (const auto *axis = std::get_if<InputTransportRawAxisEvent>(&payload)) {
    auto *raw = FindMutableRawDevice(snapshot, axis->device_id,
                                     axis->connection_generation);
    if (raw == nullptr || axis->axis_index >= raw->axes.size()) {
      return false;
    }
    const auto &descriptor = raw->descriptor.axes[axis->axis_index];
    if (axis->value < descriptor.logical_minimum ||
        axis->value > descriptor.logical_maximum) {
      return false;
    }
    raw->axes[axis->axis_index] = axis->value;
    return true;
  }
  if (const auto *hat = std::get_if<InputTransportRawHatEvent>(&payload)) {
    auto *raw = FindMutableRawDevice(snapshot, hat->device_id,
                                     hat->connection_generation);
    if (raw == nullptr || hat->hat_index >= raw->hats.size()) {
      return false;
    }
    raw->hats[hat->hat_index] = hat->state;
    return true;
  }
  if (const auto *slider =
          std::get_if<InputTransportRawSliderEvent>(&payload)) {
    auto *raw = FindMutableRawDevice(snapshot, slider->device_id,
                                     slider->connection_generation);
    if (raw == nullptr || slider->slider_index >= raw->sliders.size()) {
      return false;
    }
    const auto &descriptor = raw->descriptor.sliders[slider->slider_index];
    if (slider->x < descriptor.x_axis.logical_minimum ||
        slider->x > descriptor.x_axis.logical_maximum ||
        slider->y < descriptor.y_axis.logical_minimum ||
        slider->y > descriptor.y_axis.logical_maximum) {
      return false;
    }
    raw->sliders[slider->slider_index] = {slider->x, slider->y};
    return true;
  }
  return false;
}

bool HasCompleteEventLineage(const InputTransportBatch &batch,
                             std::uint64_t previous_event_id) noexcept {
  if (batch.events.empty()) {
    return batch.reconciliation.through_event_id == previous_event_id;
  }
  if (previous_event_id == (std::numeric_limits<std::uint64_t>::max)() ||
      batch.events.front().event_id != previous_event_id + 1U) {
    return false;
  }
  for (std::size_t index = 1U; index < batch.events.size(); ++index) {
    if (batch.events[index - 1U].event_id ==
            (std::numeric_limits<std::uint64_t>::max)() ||
        batch.events[index].event_id !=
            batch.events[index - 1U].event_id + 1U) {
      return false;
    }
  }
  return batch.reconciliation.through_event_id == batch.events.back().event_id;
}

bool ValidateCompleteTransition(
    const InputTransportReconciliationSnapshot &previous,
    const InputTransportBatch &batch) {
  InputTransportReconciliationSnapshot candidate = previous;
  for (const InputTransportEvent &event : batch.events) {
    if (!ApplyCompleteEvent(candidate, event.payload)) {
      return false;
    }
  }
  candidate.through_event_id = batch.reconciliation.through_event_id;
  candidate.host_timestamp_ns = batch.reconciliation.host_timestamp_ns;
  return EqualReconciliationState(candidate, batch.reconciliation);
}

} // namespace

InputEventTransportDecoder::InputEventTransportDecoder(
    std::uint64_t first_expected_sequence) noexcept
    : owned_sequence_state_(first_expected_sequence) {}

InputEventTransportDecoder::InputEventTransportDecoder(
    RenderTransportSequenceState &shared_sequence_state) noexcept
    : owned_sequence_state_(1U), sequence_state_(&shared_sequence_state) {}

InputEventTransportDecodeResult
InputEventTransportDecoder::Accept(const std::vector<std::uint8_t> &frame) {
  RenderTransportEnvelopeView envelope;
  const RenderTransportStatus envelope_status = DecodeRenderTransportEnvelope(
      frame, kInputEventTransportMaximumPayloadBytes, envelope);
  if (envelope_status != RenderTransportStatus::OK) {
    return Failure(envelope_status);
  }
  if (envelope.kind != RenderTransportMessageKind::INPUT_EVENT_BATCH_V1) {
    return Failure(RenderTransportStatus::UNKNOWN_MESSAGE_KIND);
  }
  const RenderTransportStatus sequence_status =
      sequence_state_->ValidateCandidate(envelope.sequence);
  if (sequence_status != RenderTransportStatus::OK) {
    return Failure(sequence_status);
  }

  try {
    std::shared_ptr<const InputTransportBatch> batch;
    RenderTransportStatus status = RenderTransportStatus::MALFORMED_PAYLOAD;
    if (!ReadPayload(envelope.payload, envelope.payload_size, batch, status)) {
      return Failure(status);
    }
    if (clock_configured_ && (batch->clock_domain != clock_domain_ ||
                              batch->clock_origin_id != clock_origin_id_)) {
      return Failure(RenderTransportStatus::CLOCK_DOMAIN_MISMATCH);
    }
    if (batch->reconciliation.through_event_id < last_event_id_ ||
        (!batch->events.empty() &&
         batch->events.front().event_id <= last_event_id_)) {
      return Failure(RenderTransportStatus::EVENT_ID_ORDER_VIOLATION);
    }
    if (batch->reconciliation.host_timestamp_ns < last_host_timestamp_ns_ ||
        (!batch->events.empty() &&
         batch->events.front().host_timestamp_ns < last_host_timestamp_ns_)) {
      return Failure(RenderTransportStatus::TIMESTAMP_ORDER_VIOLATION);
    }
    if (has_reconciliation_ && reconciliation_.window_close_requested &&
        !batch->reconciliation.window_close_requested) {
      return Failure(RenderTransportStatus::RECONCILIATION_MISMATCH);
    }
    if (has_reconciliation_ &&
        HasCompleteEventLineage(*batch, last_event_id_) &&
        !ValidateCompleteTransition(reconciliation_, *batch)) {
      return Failure(RenderTransportStatus::RECONCILIATION_MISMATCH);
    }

    std::vector<DeviceGenerationRecord> candidate_generations =
        device_generations_;
    const auto update_generation =
        [&](std::uint64_t device_id, std::uint64_t generation, bool raw,
            bool reject_absent_reuse,
            const InputTransportRawDeviceDescriptor *raw_descriptor) -> bool {
      auto found = std::find_if(
          candidate_generations.begin(), candidate_generations.end(),
          [device_id](const DeviceGenerationRecord &record) {
            return record.device_id == device_id;
          });
      const auto original =
          std::find_if(device_generations_.begin(), device_generations_.end(),
                       [device_id](const DeviceGenerationRecord &record) {
                         return record.device_id == device_id;
                       });
      const bool previously_present =
          has_reconciliation_ && HasDeviceId(reconciliation_, device_id);
      if (found == candidate_generations.end()) {
        if (candidate_generations.size() >=
            kInputEventTransportMaximumTrackedDeviceIdentities) {
          return false;
        }
        DeviceGenerationRecord record;
        record.device_id = device_id;
        record.connection_generation = generation;
        record.raw = raw;
        candidate_generations.push_back(std::move(record));
        found = std::prev(candidate_generations.end());
      }
      if (generation < found->connection_generation || found->raw != raw ||
          (reject_absent_reuse && !previously_present &&
           original != device_generations_.end() &&
           generation == original->connection_generation)) {
        return false;
      }
      if (generation > found->connection_generation) {
        found->connection_generation = generation;
        found->has_raw_descriptor = false;
        found->raw_descriptor = {};
      }
      if (raw_descriptor != nullptr) {
        if (found->has_raw_descriptor &&
            !EqualRawDeviceDescriptor(found->raw_descriptor, *raw_descriptor)) {
          return false;
        }
        found->raw_descriptor = *raw_descriptor;
        found->has_raw_descriptor = true;
      }
      return true;
    };

    for (const InputTransportEvent &event : batch->events) {
      std::uint64_t device_id = 0U;
      std::uint64_t generation = 0U;
      bool raw = false;
      bool reject_absent_reuse = false;
      const InputTransportRawDeviceDescriptor *raw_descriptor = nullptr;
      if (const auto *connection =
              std::get_if<InputTransportGamepadConnectionEvent>(
                  &event.payload)) {
        device_id = connection->device_id;
        generation = connection->connection_generation;
        reject_absent_reuse = IsConnected(connection->state);
      } else if (const auto *button =
                     std::get_if<InputTransportGamepadButtonEvent>(
                         &event.payload)) {
        device_id = button->device_id;
        generation = button->connection_generation;
      } else if (const auto *axis = std::get_if<InputTransportGamepadAxisEvent>(
                     &event.payload)) {
        device_id = axis->device_id;
        generation = axis->connection_generation;
      } else if (const auto *raw_connection =
                     std::get_if<InputTransportRawDeviceConnectionEvent>(
                         &event.payload)) {
        raw = true;
        device_id = raw_connection->descriptor.device_id;
        generation = raw_connection->descriptor.connection_generation;
        reject_absent_reuse = IsConnected(raw_connection->state);
        raw_descriptor = &raw_connection->descriptor;
      } else if (const auto *raw_button =
                     std::get_if<InputTransportRawButtonEvent>(
                         &event.payload)) {
        raw = true;
        device_id = raw_button->device_id;
        generation = raw_button->connection_generation;
      } else if (const auto *raw_axis =
                     std::get_if<InputTransportRawAxisEvent>(&event.payload)) {
        raw = true;
        device_id = raw_axis->device_id;
        generation = raw_axis->connection_generation;
      } else if (const auto *hat =
                     std::get_if<InputTransportRawHatEvent>(&event.payload)) {
        raw = true;
        device_id = hat->device_id;
        generation = hat->connection_generation;
      } else if (const auto *slider = std::get_if<InputTransportRawSliderEvent>(
                     &event.payload)) {
        raw = true;
        device_id = slider->device_id;
        generation = slider->connection_generation;
      } else {
        continue;
      }
      if (!update_generation(device_id, generation, raw, reject_absent_reuse,
                             raw_descriptor)) {
        return Failure(RenderTransportStatus::RECONCILIATION_MISMATCH);
      }
    }

    for (const auto &gamepad : batch->reconciliation.gamepads) {
      if (!update_generation(gamepad.device_id, gamepad.connection_generation,
                             false, true, nullptr)) {
        return Failure(RenderTransportStatus::RECONCILIATION_MISMATCH);
      }
    }
    for (const auto &raw : batch->reconciliation.raw_devices) {
      if (!update_generation(raw.descriptor.device_id,
                             raw.descriptor.connection_generation, true, true,
                             &raw.descriptor)) {
        return Failure(RenderTransportStatus::RECONCILIATION_MISMATCH);
      }
    }

    InputTransportReconciliationSnapshot candidate_reconciliation =
        batch->reconciliation;
    std::shared_ptr<const DecodedInputEventTransportMessage> candidate(
        new DecodedInputEventTransportMessage(envelope.sequence, batch));
    if (!sequence_state_->CommitAccepted(envelope.sequence)) {
      return Failure(RenderTransportStatus::INVALID_SEQUENCE);
    }
    clock_configured_ = true;
    clock_domain_ = batch->clock_domain;
    clock_origin_id_ = batch->clock_origin_id;
    last_event_id_ = batch->reconciliation.through_event_id;
    last_host_timestamp_ns_ = batch->reconciliation.host_timestamp_ns;
    has_reconciliation_ = true;
    reconciliation_ = std::move(candidate_reconciliation);
    device_generations_ = std::move(candidate_generations);
    published_ = candidate;
    return InputEventTransportDecodeResult{std::move(candidate),
                                           RenderTransportStatus::OK};
  } catch (const std::bad_alloc &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (const std::length_error &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  }
}

InputEventTransportEncodeResult
EncodeInputEventTransportFrame(std::uint64_t sequence,
                               const InputTransportBatch &batch) {
  InputEventTransportEncodeResult result;
  if (sequence == 0U ||
      sequence == (std::numeric_limits<std::uint64_t>::max)()) {
    result.status = RenderTransportStatus::INVALID_ARGUMENT;
    return result;
  }
  const RenderTransportStatus validation = ValidateInputTransportBatch(batch);
  if (validation != RenderTransportStatus::OK) {
    result.status = validation;
    return result;
  }
  try {
    WireWriter sizer(nullptr, kInputEventTransportMaximumPayloadBytes);
    if (!WritePayload(sizer, batch) || !sizer.ok()) {
      result.status = RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED;
      return result;
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(sizer.size()));
    WireWriter writer(&payload, kInputEventTransportMaximumPayloadBytes);
    if (!WritePayload(writer, batch) || !writer.ok() ||
        writer.size() != sizer.size()) {
      result.status = RenderTransportStatus::INVALID_ARGUMENT;
      return result;
    }
    return EncodeRenderTransportEnvelope(
        RenderTransportMessageKind::INPUT_EVENT_BATCH_V1, sequence, payload,
        kInputEventTransportMaximumPayloadBytes);
  } catch (const std::bad_alloc &) {
    result.status = RenderTransportStatus::ALLOCATION_FAILURE;
    return result;
  } catch (const std::length_error &) {
    result.status = RenderTransportStatus::ALLOCATION_FAILURE;
    return result;
  }
}

} // namespace RoR::Render

namespace RoR::Render {
namespace {

bool WriteRawAxisDescriptor(WireWriter &writer,
                            const InputTransportRawAxisDescriptor &axis) {
  return writer.AddU16(axis.index) &&
         writer.AddByte(static_cast<std::uint8_t>(axis.mode)) &&
         AddI32(writer, axis.logical_minimum) &&
         AddI32(writer, axis.logical_maximum) && AddI32(writer, axis.center) &&
         AddI32(writer, axis.deadzone_minimum) &&
         AddI32(writer, axis.deadzone_maximum);
}

bool WriteRawSliderDescriptor(WireWriter &writer,
                              const InputTransportRawSliderDescriptor &slider) {
  return writer.AddU16(slider.index) &&
         WriteRawAxisDescriptor(writer, slider.x_axis) &&
         WriteRawAxisDescriptor(writer, slider.y_axis);
}

bool WriteRawDeviceDescriptor(
    WireWriter &writer, const InputTransportRawDeviceDescriptor &descriptor) {
  if (!writer.AddU64(descriptor.device_id) ||
      !writer.AddU64(descriptor.connection_generation) ||
      !writer.AddByte(static_cast<std::uint8_t>(descriptor.device_class)) ||
      !writer.AddBytes(descriptor.guid.data(), descriptor.guid.size()) ||
      !writer.AddU16(descriptor.vendor_id) ||
      !writer.AddU16(descriptor.product_id) ||
      !writer.AddU16(descriptor.product_version) ||
      !writer.AddBytes(descriptor.name_sha256.data(),
                       descriptor.name_sha256.size()) ||
      !writer.AddU32(static_cast<std::uint32_t>(descriptor.axes.size()))) {
    return false;
  }
  for (const auto &axis : descriptor.axes) {
    if (!WriteRawAxisDescriptor(writer, axis)) {
      return false;
    }
  }
  if (!writer.AddU16(descriptor.button_count) ||
      !writer.AddByte(descriptor.hat_count) ||
      !writer.AddU32(static_cast<std::uint32_t>(descriptor.sliders.size()))) {
    return false;
  }
  for (const auto &slider : descriptor.sliders) {
    if (!WriteRawSliderDescriptor(writer, slider)) {
      return false;
    }
  }
  return true;
}

bool WriteEventPayload(WireWriter &writer,
                       const InputTransportEventPayload &payload) {
  if (const auto *key = std::get_if<InputTransportKeyboardKeyEvent>(&payload)) {
    return writer.AddU16(static_cast<std::uint16_t>(key->scancode)) &&
           writer.AddByte(static_cast<std::uint8_t>(key->state)) &&
           writer.AddBool(key->repeat);
  }
  if (const auto *motion =
          std::get_if<InputTransportMouseMotionEvent>(&payload)) {
    return AddI32(writer, motion->position_x_pixels) &&
           AddI32(writer, motion->position_y_pixels) &&
           AddI32(writer, motion->delta_x_pixels) &&
           AddI32(writer, motion->delta_y_pixels);
  }
  if (const auto *button =
          std::get_if<InputTransportMouseButtonEvent>(&payload)) {
    return writer.AddByte(static_cast<std::uint8_t>(button->button)) &&
           writer.AddByte(static_cast<std::uint8_t>(button->state));
  }
  if (const auto *wheel =
          std::get_if<InputTransportMouseWheelEvent>(&payload)) {
    return writer.AddFloat(wheel->delta_x) && writer.AddFloat(wheel->delta_y) &&
           writer.AddByte(static_cast<std::uint8_t>(wheel->direction));
  }
  if (const auto *connection =
          std::get_if<InputTransportGamepadConnectionEvent>(&payload)) {
    return writer.AddU64(connection->device_id) &&
           writer.AddU64(connection->connection_generation) &&
           writer.AddByte(static_cast<std::uint8_t>(connection->state));
  }
  if (const auto *button =
          std::get_if<InputTransportGamepadButtonEvent>(&payload)) {
    return writer.AddU64(button->device_id) &&
           writer.AddU64(button->connection_generation) &&
           writer.AddByte(static_cast<std::uint8_t>(button->button)) &&
           writer.AddByte(static_cast<std::uint8_t>(button->state));
  }
  if (const auto *axis =
          std::get_if<InputTransportGamepadAxisEvent>(&payload)) {
    return writer.AddU64(axis->device_id) &&
           writer.AddU64(axis->connection_generation) &&
           writer.AddByte(static_cast<std::uint8_t>(axis->axis)) &&
           AddI16(writer, axis->value);
  }
  if (const auto *text = std::get_if<InputTransportTextInputEvent>(&payload)) {
    return writer.AddU32(static_cast<std::uint32_t>(text->utf8_text.size())) &&
           writer.AddBytes(
               reinterpret_cast<const std::uint8_t *>(text->utf8_text.data()),
               text->utf8_text.size());
  }
  if (const auto *focus = std::get_if<InputTransportFocusEvent>(&payload)) {
    return writer.AddByte(static_cast<std::uint8_t>(focus->state));
  }
  if (std::holds_alternative<InputTransportWindowCloseEvent>(payload)) {
    return true;
  }
  if (const auto *connection =
          std::get_if<InputTransportRawDeviceConnectionEvent>(&payload)) {
    return writer.AddByte(static_cast<std::uint8_t>(connection->state)) &&
           WriteRawDeviceDescriptor(writer, connection->descriptor);
  }
  if (const auto *button =
          std::get_if<InputTransportRawButtonEvent>(&payload)) {
    return writer.AddU64(button->device_id) &&
           writer.AddU64(button->connection_generation) &&
           writer.AddU16(button->button_index) &&
           writer.AddByte(static_cast<std::uint8_t>(button->state));
  }
  if (const auto *axis = std::get_if<InputTransportRawAxisEvent>(&payload)) {
    return writer.AddU64(axis->device_id) &&
           writer.AddU64(axis->connection_generation) &&
           writer.AddU16(axis->axis_index) && AddI32(writer, axis->value);
  }
  if (const auto *hat = std::get_if<InputTransportRawHatEvent>(&payload)) {
    return writer.AddU64(hat->device_id) &&
           writer.AddU64(hat->connection_generation) &&
           writer.AddU16(hat->hat_index) &&
           writer.AddByte(static_cast<std::uint8_t>(hat->state));
  }
  if (const auto *slider =
          std::get_if<InputTransportRawSliderEvent>(&payload)) {
    return writer.AddU64(slider->device_id) &&
           writer.AddU64(slider->connection_generation) &&
           writer.AddU16(slider->slider_index) && AddI32(writer, slider->x) &&
           AddI32(writer, slider->y);
  }
  return false;
}

bool WriteReconciliation(WireWriter &writer,
                         const InputTransportReconciliationSnapshot &snapshot) {
  if (!writer.AddU64(snapshot.through_event_id) ||
      !writer.AddU64(snapshot.host_timestamp_ns) ||
      !writer.AddByte(static_cast<std::uint8_t>(snapshot.focus)) ||
      !writer.AddBool(snapshot.window_close_requested) ||
      !writer.AddU32(
          static_cast<std::uint32_t>(snapshot.pressed_scancodes.size()))) {
    return false;
  }
  for (const Sdl2PhysicalScancode scancode : snapshot.pressed_scancodes) {
    if (!writer.AddU16(static_cast<std::uint16_t>(scancode))) {
      return false;
    }
  }
  if (!writer.AddU32(
          static_cast<std::uint32_t>(snapshot.pressed_mouse_buttons.size()))) {
    return false;
  }
  for (const Sdl2MouseButton button : snapshot.pressed_mouse_buttons) {
    if (!writer.AddByte(static_cast<std::uint8_t>(button))) {
      return false;
    }
  }
  if (!writer.AddU32(static_cast<std::uint32_t>(snapshot.gamepads.size()))) {
    return false;
  }
  for (const auto &gamepad : snapshot.gamepads) {
    if (!writer.AddU64(gamepad.device_id) ||
        !writer.AddU64(gamepad.connection_generation) ||
        !writer.AddU32(
            static_cast<std::uint32_t>(gamepad.pressed_buttons.size()))) {
      return false;
    }
    for (const Sdl2GamepadButton button : gamepad.pressed_buttons) {
      if (!writer.AddByte(static_cast<std::uint8_t>(button))) {
        return false;
      }
    }
    for (const std::int16_t axis : gamepad.axes) {
      if (!AddI16(writer, axis)) {
        return false;
      }
    }
  }
  if (!writer.AddU32(static_cast<std::uint32_t>(snapshot.raw_devices.size()))) {
    return false;
  }
  for (const auto &raw : snapshot.raw_devices) {
    if (!WriteRawDeviceDescriptor(writer, raw.descriptor) ||
        !writer.AddU32(
            static_cast<std::uint32_t>(raw.pressed_buttons.size()))) {
      return false;
    }
    for (const std::uint16_t button : raw.pressed_buttons) {
      if (!writer.AddU16(button)) {
        return false;
      }
    }
    if (!writer.AddU32(static_cast<std::uint32_t>(raw.axes.size()))) {
      return false;
    }
    for (const std::int32_t axis : raw.axes) {
      if (!AddI32(writer, axis)) {
        return false;
      }
    }
    if (!writer.AddU32(static_cast<std::uint32_t>(raw.hats.size()))) {
      return false;
    }
    for (const Sdl2HatState hat : raw.hats) {
      if (!writer.AddByte(static_cast<std::uint8_t>(hat))) {
        return false;
      }
    }
    if (!writer.AddU32(static_cast<std::uint32_t>(raw.sliders.size()))) {
      return false;
    }
    for (const auto &slider : raw.sliders) {
      if (!AddI32(writer, slider.x) || !AddI32(writer, slider.y)) {
        return false;
      }
    }
  }
  return true;
}

bool WritePayload(WireWriter &writer, const InputTransportBatch &batch) {
  if (!writer.AddU32(kInputEventTransportPayloadVersion) ||
      !writer.AddU32(kInputEventTransportSdl2ScancodeVersion) ||
      !writer.AddU32(kInputEventTransportSdl2GamepadVersion) ||
      !writer.AddU32(0U) ||
      !writer.AddByte(static_cast<std::uint8_t>(batch.clock_domain)) ||
      !writer.AddByte(0U) || !writer.AddU16(0U) ||
      !writer.AddU64(batch.clock_origin_id) ||
      !writer.AddU32(static_cast<std::uint32_t>(batch.events.size()))) {
    return false;
  }
  for (const InputTransportEvent &event : batch.events) {
    const InputTransportEventKind kind =
        InputTransportPayloadKind(event.payload);
    if (!writer.AddU64(event.event_id) ||
        !writer.AddU64(event.host_timestamp_ns) ||
        !writer.AddByte(static_cast<std::uint8_t>(kind)) ||
        !WriteEventPayload(writer, event.payload)) {
      return false;
    }
  }
  return WriteReconciliation(writer, batch.reconciliation);
}

bool ReadRawAxisDescriptor(WireReader &reader,
                           InputTransportRawAxisDescriptor &axis) {
  std::uint8_t mode = 0U;
  if (!reader.ReadU16(axis.index) || !reader.ReadByte(mode) ||
      !ReadI32(reader, axis.logical_minimum) ||
      !ReadI32(reader, axis.logical_maximum) || !ReadI32(reader, axis.center) ||
      !ReadI32(reader, axis.deadzone_minimum) ||
      !ReadI32(reader, axis.deadzone_maximum)) {
    return false;
  }
  axis.mode = static_cast<InputTransportRawAxisMode>(mode);
  return true;
}

bool ReadRawSliderDescriptor(WireReader &reader,
                             InputTransportRawSliderDescriptor &slider) {
  return reader.ReadU16(slider.index) &&
         ReadRawAxisDescriptor(reader, slider.x_axis) &&
         ReadRawAxisDescriptor(reader, slider.y_axis);
}

bool ReadRawDeviceDescriptor(WireReader &reader,
                             InputTransportRawDeviceDescriptor &descriptor) {
  std::uint8_t device_class = 0U;
  const std::uint8_t *guid = nullptr;
  const std::uint8_t *name_digest = nullptr;
  if (!reader.ReadU64(descriptor.device_id) ||
      !reader.ReadU64(descriptor.connection_generation) ||
      !reader.ReadByte(device_class) ||
      !reader.ReadView(descriptor.guid.size(), guid) ||
      !reader.ReadU16(descriptor.vendor_id) ||
      !reader.ReadU16(descriptor.product_id) ||
      !reader.ReadU16(descriptor.product_version) ||
      !reader.ReadView(descriptor.name_sha256.size(), name_digest)) {
    return false;
  }
  descriptor.device_class =
      static_cast<InputTransportRawDeviceClass>(device_class);
  std::copy(guid, guid + descriptor.guid.size(), descriptor.guid.begin());
  std::copy(name_digest, name_digest + descriptor.name_sha256.size(),
            descriptor.name_sha256.begin());
  std::uint32_t axis_count = 0U;
  if (!reader.ReadCount(kInputEventTransportMaximumRawAxes, 23U, axis_count) ||
      !reader.Reserve(descriptor.axes, axis_count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < axis_count; ++index) {
    InputTransportRawAxisDescriptor axis;
    if (!ReadRawAxisDescriptor(reader, axis)) {
      return false;
    }
    descriptor.axes.push_back(axis);
  }
  std::uint32_t slider_count = 0U;
  if (!reader.ReadU16(descriptor.button_count) ||
      !reader.ReadByte(descriptor.hat_count) ||
      !reader.ReadCount(kInputEventTransportMaximumRawSliders, 48U,
                        slider_count) ||
      !reader.Reserve(descriptor.sliders, slider_count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < slider_count; ++index) {
    InputTransportRawSliderDescriptor slider;
    if (!ReadRawSliderDescriptor(reader, slider)) {
      return false;
    }
    descriptor.sliders.push_back(slider);
  }
  return true;
}

bool ReadText(WireReader &reader, std::uint64_t &total_text_bytes,
              InputTransportTextInputEvent &text) {
  std::uint32_t size = 0U;
  if (!reader.ReadU32(size)) {
    return false;
  }
  if (size == 0U || size > kInputEventTransportMaximumTextBytesPerEvent ||
      total_text_bytes > kInputEventTransportMaximumTotalTextBytes ||
      size > kInputEventTransportMaximumTotalTextBytes - total_text_bytes) {
    reader.Fail(RenderTransportStatus::BLOB_LIMIT_EXCEEDED);
    return false;
  }
  if (size > reader.remaining() ||
      !reader.ChargeAllocation(size, sizeof(char))) {
    if (size > reader.remaining()) {
      reader.Fail(RenderTransportStatus::MALFORMED_PAYLOAD);
    }
    return false;
  }
  const std::uint8_t *bytes = nullptr;
  if (!reader.ReadView(size, bytes)) {
    return false;
  }
  text.utf8_text.assign(reinterpret_cast<const char *>(bytes), size);
  if (!IsValidInputTransportUtf8(text.utf8_text)) {
    reader.Fail(RenderTransportStatus::INVALID_UTF8);
    return false;
  }
  total_text_bytes += size;
  return true;
}

bool ReadEventPayload(WireReader &reader, InputTransportEventKind kind,
                      std::uint64_t &total_text_bytes,
                      InputTransportEventPayload &payload) {
  switch (kind) {
  case InputTransportEventKind::KEYBOARD_KEY: {
    InputTransportKeyboardKeyEvent key;
    std::uint16_t scancode = 0U;
    std::uint8_t state = 0U;
    if (!reader.ReadU16(scancode) || !reader.ReadByte(state) ||
        !reader.ReadBool(key.repeat)) {
      return false;
    }
    key.scancode = static_cast<Sdl2PhysicalScancode>(scancode);
    key.state = static_cast<InputTransportDigitalState>(state);
    payload = key;
    return true;
  }
  case InputTransportEventKind::MOUSE_MOTION: {
    InputTransportMouseMotionEvent motion;
    if (!ReadI32(reader, motion.position_x_pixels) ||
        !ReadI32(reader, motion.position_y_pixels) ||
        !ReadI32(reader, motion.delta_x_pixels) ||
        !ReadI32(reader, motion.delta_y_pixels)) {
      return false;
    }
    payload = motion;
    return true;
  }
  case InputTransportEventKind::MOUSE_BUTTON: {
    InputTransportMouseButtonEvent button;
    std::uint8_t encoded_button = 0U;
    std::uint8_t state = 0U;
    if (!reader.ReadByte(encoded_button) || !reader.ReadByte(state)) {
      return false;
    }
    button.button = static_cast<Sdl2MouseButton>(encoded_button);
    button.state = static_cast<InputTransportDigitalState>(state);
    payload = button;
    return true;
  }
  case InputTransportEventKind::MOUSE_WHEEL: {
    InputTransportMouseWheelEvent wheel;
    std::uint8_t direction = 0U;
    if (!reader.ReadFloat(wheel.delta_x) || !reader.ReadFloat(wheel.delta_y) ||
        !reader.ReadByte(direction)) {
      return false;
    }
    wheel.direction = static_cast<Sdl2MouseWheelDirection>(direction);
    payload = wheel;
    return true;
  }
  case InputTransportEventKind::GAMEPAD_CONNECTION: {
    InputTransportGamepadConnectionEvent connection;
    std::uint8_t state = 0U;
    if (!reader.ReadU64(connection.device_id) ||
        !reader.ReadU64(connection.connection_generation) ||
        !reader.ReadByte(state)) {
      return false;
    }
    connection.state = static_cast<InputTransportDeviceConnectionState>(state);
    payload = connection;
    return true;
  }
  case InputTransportEventKind::GAMEPAD_BUTTON: {
    InputTransportGamepadButtonEvent button;
    std::uint8_t encoded_button = 0U;
    std::uint8_t state = 0U;
    if (!reader.ReadU64(button.device_id) ||
        !reader.ReadU64(button.connection_generation) ||
        !reader.ReadByte(encoded_button) || !reader.ReadByte(state)) {
      return false;
    }
    button.button = static_cast<Sdl2GamepadButton>(encoded_button);
    button.state = static_cast<InputTransportDigitalState>(state);
    payload = button;
    return true;
  }
  case InputTransportEventKind::GAMEPAD_AXIS: {
    InputTransportGamepadAxisEvent axis;
    std::uint8_t encoded_axis = 0U;
    if (!reader.ReadU64(axis.device_id) ||
        !reader.ReadU64(axis.connection_generation) ||
        !reader.ReadByte(encoded_axis) || !ReadI16(reader, axis.value)) {
      return false;
    }
    axis.axis = static_cast<Sdl2GamepadAxis>(encoded_axis);
    payload = axis;
    return true;
  }
  case InputTransportEventKind::TEXT_INPUT: {
    InputTransportTextInputEvent text;
    if (!ReadText(reader, total_text_bytes, text)) {
      return false;
    }
    payload = std::move(text);
    return true;
  }
  case InputTransportEventKind::FOCUS: {
    InputTransportFocusEvent focus;
    std::uint8_t state = 0U;
    if (!reader.ReadByte(state)) {
      return false;
    }
    focus.state = static_cast<InputTransportFocusState>(state);
    payload = focus;
    return true;
  }
  case InputTransportEventKind::WINDOW_CLOSE:
    payload = InputTransportWindowCloseEvent{};
    return true;
  case InputTransportEventKind::RAW_DEVICE_CONNECTION: {
    InputTransportRawDeviceConnectionEvent connection;
    std::uint8_t state = 0U;
    if (!reader.ReadByte(state) ||
        !ReadRawDeviceDescriptor(reader, connection.descriptor)) {
      return false;
    }
    connection.state = static_cast<InputTransportDeviceConnectionState>(state);
    payload = std::move(connection);
    return true;
  }
  case InputTransportEventKind::RAW_BUTTON: {
    InputTransportRawButtonEvent button;
    std::uint8_t state = 0U;
    if (!reader.ReadU64(button.device_id) ||
        !reader.ReadU64(button.connection_generation) ||
        !reader.ReadU16(button.button_index) || !reader.ReadByte(state)) {
      return false;
    }
    button.state = static_cast<InputTransportDigitalState>(state);
    payload = button;
    return true;
  }
  case InputTransportEventKind::RAW_AXIS: {
    InputTransportRawAxisEvent axis;
    if (!reader.ReadU64(axis.device_id) ||
        !reader.ReadU64(axis.connection_generation) ||
        !reader.ReadU16(axis.axis_index) || !ReadI32(reader, axis.value)) {
      return false;
    }
    payload = axis;
    return true;
  }
  case InputTransportEventKind::RAW_HAT: {
    InputTransportRawHatEvent hat;
    std::uint8_t state = 0U;
    if (!reader.ReadU64(hat.device_id) ||
        !reader.ReadU64(hat.connection_generation) ||
        !reader.ReadU16(hat.hat_index) || !reader.ReadByte(state)) {
      return false;
    }
    hat.state = static_cast<Sdl2HatState>(state);
    payload = hat;
    return true;
  }
  case InputTransportEventKind::RAW_SLIDER: {
    InputTransportRawSliderEvent slider;
    if (!reader.ReadU64(slider.device_id) ||
        !reader.ReadU64(slider.connection_generation) ||
        !reader.ReadU16(slider.slider_index) || !ReadI32(reader, slider.x) ||
        !ReadI32(reader, slider.y)) {
      return false;
    }
    payload = slider;
    return true;
  }
  }
  reader.Fail(RenderTransportStatus::PAYLOAD_VALIDATION_FAILED);
  return false;
}

bool ReadReconciliation(WireReader &reader,
                        InputTransportReconciliationSnapshot &snapshot) {
  std::uint8_t focus = 0U;
  if (!reader.ReadU64(snapshot.through_event_id) ||
      !reader.ReadU64(snapshot.host_timestamp_ns) || !reader.ReadByte(focus) ||
      !reader.ReadBool(snapshot.window_close_requested)) {
    return false;
  }
  snapshot.focus = static_cast<InputTransportFocusState>(focus);
  std::uint32_t key_count = 0U;
  if (!reader.ReadCount(kInputEventTransportMaximumPressedScancodes,
                        sizeof(std::uint16_t), key_count) ||
      !reader.Reserve(snapshot.pressed_scancodes, key_count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < key_count; ++index) {
    std::uint16_t scancode = 0U;
    if (!reader.ReadU16(scancode)) {
      return false;
    }
    snapshot.pressed_scancodes.push_back(
        static_cast<Sdl2PhysicalScancode>(scancode));
  }
  std::uint32_t mouse_count = 0U;
  if (!reader.ReadCount(kInputEventTransportMaximumPressedMouseButtons, 1U,
                        mouse_count) ||
      !reader.Reserve(snapshot.pressed_mouse_buttons, mouse_count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < mouse_count; ++index) {
    std::uint8_t button = 0U;
    if (!reader.ReadByte(button)) {
      return false;
    }
    snapshot.pressed_mouse_buttons.push_back(
        static_cast<Sdl2MouseButton>(button));
  }
  std::uint32_t gamepad_count = 0U;
  if (!reader.ReadCount(kInputEventTransportMaximumGamepads, 32U,
                        gamepad_count) ||
      !reader.Reserve(snapshot.gamepads, gamepad_count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < gamepad_count; ++index) {
    InputTransportGamepadReconciliationState gamepad;
    std::uint32_t button_count = 0U;
    if (!reader.ReadU64(gamepad.device_id) ||
        !reader.ReadU64(gamepad.connection_generation) ||
        !reader.ReadCount(kInputEventTransportMaximumGamepadButtons, 1U,
                          button_count) ||
        !reader.Reserve(gamepad.pressed_buttons, button_count)) {
      return false;
    }
    for (std::uint32_t button = 0U; button < button_count; ++button) {
      std::uint8_t encoded = 0U;
      if (!reader.ReadByte(encoded)) {
        return false;
      }
      gamepad.pressed_buttons.push_back(
          static_cast<Sdl2GamepadButton>(encoded));
    }
    for (std::int16_t &axis : gamepad.axes) {
      if (!ReadI16(reader, axis)) {
        return false;
      }
    }
    snapshot.gamepads.push_back(std::move(gamepad));
  }
  std::uint32_t raw_count = 0U;
  if (!reader.ReadCount(kInputEventTransportMaximumRawDevices, 96U,
                        raw_count) ||
      !reader.Reserve(snapshot.raw_devices, raw_count)) {
    return false;
  }
  for (std::uint32_t index = 0U; index < raw_count; ++index) {
    InputTransportRawDeviceReconciliationState raw;
    if (!ReadRawDeviceDescriptor(reader, raw.descriptor)) {
      return false;
    }
    std::uint32_t button_count = 0U;
    if (!reader.ReadCount(kInputEventTransportMaximumRawButtons, 2U,
                          button_count) ||
        !reader.Reserve(raw.pressed_buttons, button_count)) {
      return false;
    }
    for (std::uint32_t button = 0U; button < button_count; ++button) {
      std::uint16_t encoded = 0U;
      if (!reader.ReadU16(encoded)) {
        return false;
      }
      raw.pressed_buttons.push_back(encoded);
    }
    std::uint32_t axis_count = 0U;
    if (!reader.ReadCount(kInputEventTransportMaximumRawAxes, 4U, axis_count) ||
        !reader.Reserve(raw.axes, axis_count)) {
      return false;
    }
    for (std::uint32_t axis = 0U; axis < axis_count; ++axis) {
      std::int32_t value = 0;
      if (!ReadI32(reader, value)) {
        return false;
      }
      raw.axes.push_back(value);
    }
    std::uint32_t hat_count = 0U;
    if (!reader.ReadCount(kInputEventTransportMaximumRawHats, 1U, hat_count) ||
        !reader.Reserve(raw.hats, hat_count)) {
      return false;
    }
    for (std::uint32_t hat = 0U; hat < hat_count; ++hat) {
      std::uint8_t state = 0U;
      if (!reader.ReadByte(state)) {
        return false;
      }
      raw.hats.push_back(static_cast<Sdl2HatState>(state));
    }
    std::uint32_t slider_count = 0U;
    if (!reader.ReadCount(kInputEventTransportMaximumRawSliders, 8U,
                          slider_count) ||
        !reader.Reserve(raw.sliders, slider_count)) {
      return false;
    }
    for (std::uint32_t slider = 0U; slider < slider_count; ++slider) {
      InputTransportRawSliderState state;
      if (!ReadI32(reader, state.x) || !ReadI32(reader, state.y)) {
        return false;
      }
      raw.sliders.push_back(state);
    }
    snapshot.raw_devices.push_back(std::move(raw));
  }
  return true;
}

bool ReadPayload(const std::uint8_t *payload, std::size_t payload_size,
                 std::shared_ptr<const InputTransportBatch> &decoded,
                 RenderTransportStatus &status) {
  AllocationBudget allocation_budget(
      kInputEventTransportMaximumDecodedAllocationBytes);
  WireReader reader(payload, payload_size, allocation_budget);
  InputTransportBatch batch;
  std::uint32_t payload_version = 0U;
  std::uint32_t scancode_version = 0U;
  std::uint32_t gamepad_version = 0U;
  std::uint32_t reserved32 = 0U;
  std::uint8_t clock_domain = 0U;
  std::uint8_t reserved8 = 0U;
  std::uint16_t reserved16 = 0U;
  if (!reader.ReadU32(payload_version) || !reader.ReadU32(scancode_version) ||
      !reader.ReadU32(gamepad_version) || !reader.ReadU32(reserved32) ||
      !reader.ReadByte(clock_domain) || !reader.ReadByte(reserved8) ||
      !reader.ReadU16(reserved16) || !reader.ReadU64(batch.clock_origin_id)) {
    status = reader.status();
    return false;
  }
  if (payload_version != kInputEventTransportPayloadVersion ||
      scancode_version != kInputEventTransportSdl2ScancodeVersion ||
      gamepad_version != kInputEventTransportSdl2GamepadVersion ||
      reserved32 != 0U || reserved8 != 0U || reserved16 != 0U) {
    status = RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
    return false;
  }
  batch.version = payload_version;
  batch.clock_domain = static_cast<InputTransportClockDomain>(clock_domain);
  std::uint32_t event_count = 0U;
  if (!reader.ReadCount(kInputEventTransportMaximumEvents, kMinimumEventBytes,
                        event_count) ||
      !reader.Reserve(batch.events, event_count)) {
    status = reader.status();
    return false;
  }
  std::uint64_t total_text_bytes = 0U;
  for (std::uint32_t index = 0U; index < event_count; ++index) {
    InputTransportEvent event;
    std::uint8_t kind = 0U;
    if (!reader.ReadU64(event.event_id) ||
        !reader.ReadU64(event.host_timestamp_ns) || !reader.ReadByte(kind)) {
      status = reader.status();
      return false;
    }
    const auto event_kind = static_cast<InputTransportEventKind>(kind);
    if (!IsKnownInputTransportEventKind(event_kind)) {
      status = RenderTransportStatus::PAYLOAD_VALIDATION_FAILED;
      return false;
    }
    if (!ReadEventPayload(reader, event_kind, total_text_bytes,
                          event.payload)) {
      status = reader.status();
      return false;
    }
    batch.events.push_back(std::move(event));
  }
  if (!ReadReconciliation(reader, batch.reconciliation)) {
    status = reader.status();
    return false;
  }
  if (!reader.consumed()) {
    status = RenderTransportStatus::MALFORMED_PAYLOAD;
    return false;
  }
  status = ValidateInputTransportBatch(batch);
  if (status != RenderTransportStatus::OK) {
    return false;
  }
  decoded = std::make_shared<const InputTransportBatch>(std::move(batch));
  return true;
}

InputEventTransportDecodeResult Failure(RenderTransportStatus status) {
  InputEventTransportDecodeResult result;
  result.status = status;
  return result;
}

} // namespace

} // namespace RoR::Render
