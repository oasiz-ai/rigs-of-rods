/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RendererOgre14InputAdapter.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <variant>

namespace RoR {
namespace {

using namespace Render;

static_assert(kRendererGameMaximumJoystickButtons ==
              kInputEventTransportMaximumRawButtons);

struct DeviceCandidate final {
  bool raw = false;
  std::uint64_t device_id = 0U;
  std::uint64_t generation = 0U;
  const InputTransportGamepadReconciliationState *gamepad = nullptr;
  const InputTransportRawDeviceReconciliationState *raw_state = nullptr;
  std::size_t slot = kRendererGameJoystickSlots;
};

struct KeyTransition final {
  RendererGameKey key = RendererGameKey::UNASSIGNED;
  bool pressed = false;
};
struct MouseMotion final {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t dx = 0;
  std::int32_t dy = 0;
};
struct MouseButtonTransition final {
  RendererGameMouseButton button = RendererGameMouseButton::LEFT;
  bool pressed = false;
};
struct MouseWheelTransition final {
  float x = 0.0F;
  float y = 0.0F;
};
struct TextTransition final { std::string_view text; };
struct FocusTransition final { bool focused = false; };
struct CloseTransition final {};
using PendingTransition =
    std::variant<KeyTransition, MouseMotion, MouseButtonTransition,
                 MouseWheelTransition, TextTransition, FocusTransition,
                 CloseTransition>;

std::string RawVendor(
    const InputTransportRawDeviceDescriptor &descriptor) {
  std::ostringstream stream;
  stream << "OgreNext Raw " << std::hex << std::setfill('0');
  for (std::size_t index = 0U; index < 4U; ++index) {
    stream << std::setw(2)
           << static_cast<unsigned int>(descriptor.name_sha256[index]);
  }
  return stream.str();
}

std::string GamepadVendor(std::uint64_t device_id) {
  return "OgreNext SDL2 Gamepad " + std::to_string(device_id);
}

bool IsPressed(InputTransportDigitalState state) noexcept {
  return state == InputTransportDigitalState::PRESSED;
}

std::uint8_t HatBits(Sdl2HatState state) noexcept {
  return static_cast<std::uint8_t>(state);
}

std::int32_t NormalizeRawAxis(
    std::int64_t value,
    const InputTransportRawAxisDescriptor &axis) noexcept {
  const std::int64_t bounded = std::max<std::int64_t>(
      axis.logical_minimum,
      std::min<std::int64_t>(axis.logical_maximum, value));
  if (bounded >= axis.deadzone_minimum &&
      bounded <= axis.deadzone_maximum) {
    return 0;
  }
  if (bounded < axis.deadzone_minimum) {
    const std::int64_t extent =
        static_cast<std::int64_t>(axis.deadzone_minimum) -
        axis.logical_minimum;
    return extent > 0
               ? static_cast<std::int32_t>(
                     ((bounded - axis.deadzone_minimum) * 32768) / extent)
               : -32768;
  }
  const std::int64_t extent =
      static_cast<std::int64_t>(axis.logical_maximum) -
      axis.deadzone_maximum;
  return extent > 0
             ? static_cast<std::int32_t>(
                   ((bounded - axis.deadzone_maximum) * 32767) / extent)
             : 32767;
}

std::int32_t SaturateInt32(std::int64_t value) noexcept {
  return static_cast<std::int32_t>(std::max<std::int64_t>(
      (std::numeric_limits<std::int32_t>::min)(),
      std::min<std::int64_t>((std::numeric_limits<std::int32_t>::max)(),
                             value)));
}

float SaturateFloat(double value) noexcept {
  const double maximum =
      static_cast<double>((std::numeric_limits<float>::max)());
  return static_cast<float>(std::max(-maximum, std::min(maximum, value)));
}

} // namespace

RendererGameKey TranslateRendererSdl2ScancodeToGame(
    Sdl2PhysicalScancode scancode) noexcept {
  return TranslateRendererSdlScancodeToGame(
      static_cast<std::uint16_t>(scancode));
}

RendererGameMouseButton TranslateRendererSdl2MouseButtonToGame(
    Sdl2MouseButton button) noexcept {
  RendererGameMouseButton translated = RendererGameMouseButton::LEFT;
  (void)TryTranslateRendererSdlMouseButtonToGame(
      static_cast<std::uint8_t>(button), translated);
  return translated;
}

RendererOgre14InputAdapter::RendererOgre14InputAdapter(
    IRendererGameInputTarget &target) noexcept
    : target_(target) {}

bool RendererOgre14InputAdapter::ActivateTarget() noexcept {
  if (activated_) {
    return true;
  }
  if (!target_.ActivateInput()) {
    return false;
  }
  activated_ = true;
  return true;
}

RendererOgre14InputApplyResult RendererOgre14InputAdapter::Apply(
    const DecodedInputEventTransportMessage &message) {
  if (message.batch() == nullptr) {
    return {};
  }
  return ApplyKnownValidBatch(message.sequence(), *message.batch());
}

RendererOgre14InputApplyResult
RendererOgre14InputAdapter::ApplyValidatedBatch(
    std::uint64_t reverse_sequence, const InputTransportBatch &batch) {
  const RenderTransportStatus validation = ValidateInputTransportBatch(batch);
  if (validation != RenderTransportStatus::OK) {
    RendererOgre14InputApplyResult result;
    result.status = RendererOgre14InputApplyStatus::REJECTED_INVALID_MESSAGE;
    result.transport_status = validation;
    result.reverse_sequence = reverse_sequence;
    return result;
  }
  return ApplyKnownValidBatch(reverse_sequence, batch);
}

RendererOgre14InputApplyResult
RendererOgre14InputAdapter::ApplyKnownValidBatch(
    std::uint64_t reverse_sequence, const InputTransportBatch &batch) {
  RendererOgre14InputApplyResult result;
  result.reverse_sequence = reverse_sequence;
  result.transport_status = RenderTransportStatus::OK;
  result.resolved_through_event_id = batch.reconciliation.through_event_id;
  if (!batch.events.empty()) {
    result.issued_first_event_id = batch.events.front().event_id;
    result.issued_last_event_id = batch.events.back().event_id;
  }
  if (reverse_sequence == 0U || reverse_sequence <= last_reverse_sequence_ ||
      batch.reconciliation.through_event_id < applied_through_event_id_) {
    result.status = RendererOgre14InputApplyStatus::REJECTED_SEQUENCE;
    result.transport_status = RenderTransportStatus::OUT_OF_ORDER_SEQUENCE;
    return result;
  }

  const std::size_t device_count = batch.reconciliation.gamepads.size() +
                                   batch.reconciliation.raw_devices.size();
  if (device_count > kRendererGameJoystickSlots) {
    result.status = RendererOgre14InputApplyStatus::REJECTED_DEVICE_CAPACITY;
    result.transport_status = RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED;
    return result;
  }

  try {
    std::vector<PendingTransition> transitions;
    transitions.reserve(batch.events.size());
    RendererGameInputState state;
    state.through_event_id = batch.reconciliation.through_event_id;
    state.focused = batch.reconciliation.focus ==
                    InputTransportFocusState::GAINED;
    state.window_close_requested =
        batch.reconciliation.window_close_requested;
    state.mouse_x_pixels = mouse_x_pixels_;
    state.mouse_y_pixels = mouse_y_pixels_;
    std::int64_t mouse_delta_x = 0;
    std::int64_t mouse_delta_y = 0;
    double wheel_delta_x = 0.0;
    double wheel_delta_y = 0.0;

    using RelativeAxisKey =
        std::tuple<std::uint64_t, std::uint64_t, std::uint16_t>;
    std::map<RelativeAxisKey, std::int64_t> raw_relative_axes;

    for (const InputTransportEvent &event : batch.events) {
      if (const auto *key =
              std::get_if<InputTransportKeyboardKeyEvent>(&event.payload)) {
        const RendererGameKey mapped =
            TranslateRendererSdl2ScancodeToGame(key->scancode);
        if (mapped == RendererGameKey::UNASSIGNED) {
          ++result.ignored_unmapped_scancodes;
        } else if (!key->repeat) {
          transitions.emplace_back(KeyTransition{mapped,
                                                  IsPressed(key->state)});
        }
      } else if (const auto *motion =
                     std::get_if<InputTransportMouseMotionEvent>(
                         &event.payload)) {
        state.mouse_x_pixels = motion->position_x_pixels;
        state.mouse_y_pixels = motion->position_y_pixels;
        mouse_delta_x += motion->delta_x_pixels;
        mouse_delta_y += motion->delta_y_pixels;
        transitions.emplace_back(MouseMotion{
            motion->position_x_pixels, motion->position_y_pixels,
            motion->delta_x_pixels, motion->delta_y_pixels});
      } else if (const auto *button =
                     std::get_if<InputTransportMouseButtonEvent>(
                         &event.payload)) {
        transitions.emplace_back(MouseButtonTransition{
            TranslateRendererSdl2MouseButtonToGame(button->button),
            IsPressed(button->state)});
      } else if (const auto *wheel =
                     std::get_if<InputTransportMouseWheelEvent>(
                         &event.payload)) {
        const float sign = wheel->direction ==
                                   Sdl2MouseWheelDirection::FLIPPED
                               ? -1.0F
                               : 1.0F;
        const float x = wheel->delta_x * sign;
        const float y = wheel->delta_y * sign;
        wheel_delta_x += x;
        wheel_delta_y += y;
        transitions.emplace_back(MouseWheelTransition{x, y});
      } else if (const auto *text =
                     std::get_if<InputTransportTextInputEvent>(
                         &event.payload)) {
        transitions.emplace_back(TextTransition{text->utf8_text});
      } else if (const auto *focus =
                     std::get_if<InputTransportFocusEvent>(&event.payload)) {
        transitions.emplace_back(FocusTransition{
            focus->state == InputTransportFocusState::GAINED});
      } else if (std::holds_alternative<InputTransportWindowCloseEvent>(
                     event.payload)) {
        transitions.emplace_back(CloseTransition{});
      } else if (const auto *axis =
                     std::get_if<InputTransportRawAxisEvent>(
                         &event.payload)) {
        raw_relative_axes[{axis->device_id, axis->connection_generation,
                           axis->axis_index}] += axis->value;
      }
    }
    state.mouse_delta_x_pixels = SaturateInt32(mouse_delta_x);
    state.mouse_delta_y_pixels = SaturateInt32(mouse_delta_y);
    state.wheel_delta_x = SaturateFloat(wheel_delta_x);
    state.wheel_delta_y = SaturateFloat(wheel_delta_y);

    state.pressed_keys.reserve(
        batch.reconciliation.pressed_scancodes.size());
    for (const Sdl2PhysicalScancode scancode :
         batch.reconciliation.pressed_scancodes) {
      const RendererGameKey key =
          TranslateRendererSdl2ScancodeToGame(scancode);
      if (key == RendererGameKey::UNASSIGNED) {
        ++result.ignored_unmapped_scancodes;
      } else {
        state.pressed_keys.push_back(key);
      }
    }
    std::sort(state.pressed_keys.begin(), state.pressed_keys.end());
    state.pressed_keys.erase(
        std::unique(state.pressed_keys.begin(), state.pressed_keys.end()),
        state.pressed_keys.end());

    state.pressed_mouse_buttons.reserve(
        batch.reconciliation.pressed_mouse_buttons.size());
    for (const Sdl2MouseButton button :
         batch.reconciliation.pressed_mouse_buttons) {
      state.pressed_mouse_buttons.push_back(
          TranslateRendererSdl2MouseButtonToGame(button));
    }

    std::vector<DeviceCandidate> candidates;
    candidates.reserve(device_count);
    for (const InputTransportGamepadReconciliationState &gamepad :
         batch.reconciliation.gamepads) {
      candidates.push_back(
          {false, gamepad.device_id, gamepad.connection_generation,
           &gamepad, nullptr, kRendererGameJoystickSlots});
    }
    for (const InputTransportRawDeviceReconciliationState &raw :
         batch.reconciliation.raw_devices) {
      candidates.push_back(
          {true, raw.descriptor.device_id,
           raw.descriptor.connection_generation, nullptr, &raw,
           kRendererGameJoystickSlots});
    }

    const auto same_device = [](const DeviceSlot &slot,
                                const DeviceCandidate &candidate) noexcept {
      return slot.active && slot.raw == candidate.raw &&
             slot.device_id == candidate.device_id &&
             slot.generation == candidate.generation;
    };

    std::array<bool, kRendererGameJoystickSlots> claimed{};
    for (DeviceCandidate &candidate : candidates) {
      for (std::size_t slot = 0U; slot < slots_.size(); ++slot) {
        if (!claimed[slot] && same_device(slots_[slot], candidate)) {
          candidate.slot = slot;
          claimed[slot] = true;
          break;
        }
      }
    }
    for (DeviceCandidate &candidate : candidates) {
      if (candidate.slot != kRendererGameJoystickSlots) {
        continue;
      }
      const auto free = std::find(claimed.begin(), claimed.end(), false);
      if (free == claimed.end()) {
        result.status =
            RendererOgre14InputApplyStatus::REJECTED_DEVICE_CAPACITY;
        result.transport_status =
            RenderTransportStatus::RESOURCE_LIMIT_EXCEEDED;
        return result;
      }
      candidate.slot = static_cast<std::size_t>(free - claimed.begin());
      claimed[candidate.slot] = true;
    }

    std::array<DeviceSlot, kRendererGameJoystickSlots> next_slots{};
    state.joysticks.reserve(candidates.size());
    for (const DeviceCandidate &candidate : candidates) {
      RendererGameJoystickState joystick;
      joystick.slot = candidate.slot;
      joystick.raw_device = candidate.raw;
      joystick.device_id = candidate.device_id;
      joystick.connection_generation = candidate.generation;
      const DeviceSlot &previous = slots_[candidate.slot];
      const bool same_previous = same_device(previous, candidate);
      if (candidate.gamepad != nullptr) {
        joystick.vendor = GamepadVendor(candidate.device_id);
        joystick.axes_absolute.assign(candidate.gamepad->axes.begin(),
                                      candidate.gamepad->axes.end());
        joystick.buttons.assign(kInputEventTransportMaximumGamepadButtons,
                                false);
        for (const Sdl2GamepadButton button :
             candidate.gamepad->pressed_buttons) {
          joystick.buttons[static_cast<std::size_t>(button)] = true;
        }
      } else {
        const InputTransportRawDeviceReconciliationState &raw =
            *candidate.raw_state;
        joystick.vendor = RawVendor(raw.descriptor);
        joystick.axes_absolute.resize(raw.axes.size());
        for (std::size_t axis = 0U; axis < raw.axes.size(); ++axis) {
          joystick.axes_absolute[axis] =
              NormalizeRawAxis(raw.axes[axis], raw.descriptor.axes[axis]);
        }
        joystick.buttons.assign(raw.descriptor.button_count, false);
        for (const std::uint16_t button : raw.pressed_buttons) {
          joystick.buttons[button] = true;
        }
        joystick.hats.reserve(raw.hats.size());
        for (const Sdl2HatState hat : raw.hats) {
          joystick.hats.push_back(HatBits(hat));
        }
        joystick.sliders.reserve(raw.sliders.size());
        for (std::size_t index = 0U; index < raw.sliders.size(); ++index) {
          const InputTransportRawSliderState &slider = raw.sliders[index];
          const InputTransportRawSliderDescriptor &descriptor =
              raw.descriptor.sliders[index];
          joystick.sliders.emplace_back(
              NormalizeRawAxis(slider.x, descriptor.x_axis),
              NormalizeRawAxis(slider.y, descriptor.y_axis));
        }
      }

      joystick.axes_relative.resize(joystick.axes_absolute.size(), 0);
      for (std::size_t axis = 0U;
           axis < joystick.axes_absolute.size(); ++axis) {
        bool relative = false;
        if (candidate.raw_state != nullptr) {
          relative = candidate.raw_state->descriptor.axes[axis].mode ==
                     InputTransportRawAxisMode::RELATIVE;
        }
        if (relative) {
          const auto found = raw_relative_axes.find(
              {candidate.device_id, candidate.generation,
               static_cast<std::uint16_t>(axis)});
          if (found != raw_relative_axes.end()) {
            const std::int64_t bounded = std::max<std::int64_t>(
                (std::numeric_limits<std::int32_t>::min)(),
                std::min<std::int64_t>(
                    (std::numeric_limits<std::int32_t>::max)(),
                    found->second));
            joystick.axes_relative[axis] = NormalizeRawAxis(
                bounded, candidate.raw_state->descriptor.axes[axis]);
          }
        } else if (same_previous && axis < previous.axes.size()) {
          const std::int64_t delta =
              static_cast<std::int64_t>(joystick.axes_absolute[axis]) -
              previous.axes[axis];
          joystick.axes_relative[axis] = static_cast<std::int32_t>(
              std::max<std::int64_t>(
                  (std::numeric_limits<std::int32_t>::min)(),
                  std::min<std::int64_t>(
                      (std::numeric_limits<std::int32_t>::max)(), delta)));
        }
      }

      DeviceSlot &next = next_slots[candidate.slot];
      next.active = true;
      next.raw = candidate.raw;
      next.device_id = candidate.device_id;
      next.generation = candidate.generation;
      next.axes = joystick.axes_absolute;
      state.joysticks.push_back(std::move(joystick));
    }
    std::sort(state.joysticks.begin(), state.joysticks.end(),
              [](const auto &lhs, const auto &rhs) {
                return lhs.slot < rhs.slot;
              });

    if (!ActivateTarget()) {
      result.status = RendererOgre14InputApplyStatus::FAILED_TARGET;
      result.transport_status = RenderTransportStatus::INVALID_ARGUMENT;
      return result;
    }
    for (const PendingTransition &transition : transitions) {
      std::visit(
          [this](const auto &typed) noexcept {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, KeyTransition>) {
              target_.KeyChanged(typed.key, typed.pressed);
            } else if constexpr (std::is_same_v<T, MouseMotion>) {
              target_.MouseMoved(typed.x, typed.y, typed.dx, typed.dy);
            } else if constexpr (std::is_same_v<T,
                                                MouseButtonTransition>) {
              target_.MouseButtonChanged(typed.button, typed.pressed);
            } else if constexpr (std::is_same_v<T,
                                                MouseWheelTransition>) {
              target_.MouseWheel(typed.x, typed.y);
            } else if constexpr (std::is_same_v<T, TextTransition>) {
              target_.TextInput(typed.text);
            } else if constexpr (std::is_same_v<T, FocusTransition>) {
              target_.FocusChanged(typed.focused);
            } else {
              target_.WindowCloseRequested();
            }
          },
          transition);
    }
    if (!target_.Reconcile(state)) {
      result.status = RendererOgre14InputApplyStatus::FAILED_TARGET;
      result.transport_status = RenderTransportStatus::INVALID_ARGUMENT;
      return result;
    }

    slots_ = std::move(next_slots);
    mouse_x_pixels_ = state.mouse_x_pixels;
    mouse_y_pixels_ = state.mouse_y_pixels;
    last_reverse_sequence_ = reverse_sequence;
    applied_through_event_id_ = batch.reconciliation.through_event_id;
    result.status = RendererOgre14InputApplyStatus::APPLIED;
    result.applied_through_event_id = applied_through_event_id_;
    result.accepted = true;
    return result;
  } catch (const std::bad_alloc &) {
    result.status = RendererOgre14InputApplyStatus::FAILED_ALLOCATION;
    result.transport_status = RenderTransportStatus::ALLOCATION_FAILURE;
    return result;
  } catch (const std::length_error &) {
    result.status = RendererOgre14InputApplyStatus::FAILED_ALLOCATION;
    result.transport_status = RenderTransportStatus::ALLOCATION_FAILURE;
    return result;
  }
}

bool IsKnownRendererOgre14InputApplyStatus(
    RendererOgre14InputApplyStatus status) noexcept {
  switch (status) {
  case RendererOgre14InputApplyStatus::APPLIED:
  case RendererOgre14InputApplyStatus::REJECTED_INVALID_MESSAGE:
  case RendererOgre14InputApplyStatus::REJECTED_SEQUENCE:
  case RendererOgre14InputApplyStatus::REJECTED_DEVICE_CAPACITY:
  case RendererOgre14InputApplyStatus::FAILED_TARGET:
  case RendererOgre14InputApplyStatus::FAILED_ALLOCATION: return true;
  }
  return false;
}

const char *ToString(RendererOgre14InputApplyStatus status) noexcept {
  switch (status) {
  case RendererOgre14InputApplyStatus::APPLIED: return "applied";
  case RendererOgre14InputApplyStatus::REJECTED_INVALID_MESSAGE:
    return "rejected_invalid_message";
  case RendererOgre14InputApplyStatus::REJECTED_SEQUENCE:
    return "rejected_sequence";
  case RendererOgre14InputApplyStatus::REJECTED_DEVICE_CAPACITY:
    return "rejected_device_capacity";
  case RendererOgre14InputApplyStatus::FAILED_TARGET: return "failed_target";
  case RendererOgre14InputApplyStatus::FAILED_ALLOCATION:
    return "failed_allocation";
  }
  return "invalid";
}

} // namespace RoR
