/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Validated Ogre-Next input to legacy InputEngine semantic adapter.

#pragma once

#include "render/InputEventTransport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RoR {

constexpr std::uint32_t kRendererOgre14InputAdapterContractVersion = 1U;
constexpr std::size_t kRendererOgre14LegacyJoystickSlots = 10U;

/// Numeric values are the stable OIS/DirectInput scan identities consumed by
/// RoR input.map. Keeping them here avoids importing OIS or a platform API into
/// the renderer process boundary and its portable strict tests.
enum class RendererOgre14LegacyKey : std::uint16_t {
  UNASSIGNED = 0x00U, ESCAPE = 0x01U,
  DIGIT_1 = 0x02U, DIGIT_2 = 0x03U, DIGIT_3 = 0x04U,
  DIGIT_4 = 0x05U, DIGIT_5 = 0x06U, DIGIT_6 = 0x07U,
  DIGIT_7 = 0x08U, DIGIT_8 = 0x09U, DIGIT_9 = 0x0AU,
  DIGIT_0 = 0x0BU, MINUS = 0x0CU, EQUALS = 0x0DU,
  BACK = 0x0EU, TAB = 0x0FU, Q = 0x10U, W = 0x11U,
  E = 0x12U, R = 0x13U, T = 0x14U, Y = 0x15U,
  U = 0x16U, I = 0x17U, O = 0x18U, P = 0x19U,
  LEFT_BRACKET = 0x1AU, RIGHT_BRACKET = 0x1BU, RETURN = 0x1CU,
  LEFT_CONTROL = 0x1DU, A = 0x1EU, S = 0x1FU, D = 0x20U,
  F = 0x21U, G = 0x22U, H = 0x23U, J = 0x24U,
  K = 0x25U, L = 0x26U, SEMICOLON = 0x27U,
  APOSTROPHE = 0x28U, GRAVE = 0x29U, LEFT_SHIFT = 0x2AU,
  BACKSLASH = 0x2BU, Z = 0x2CU, X = 0x2DU, C = 0x2EU,
  V = 0x2FU, B = 0x30U, N = 0x31U, M = 0x32U,
  COMMA = 0x33U, PERIOD = 0x34U, SLASH = 0x35U,
  RIGHT_SHIFT = 0x36U, MULTIPLY = 0x37U, LEFT_ALT = 0x38U,
  SPACE = 0x39U, CAPITAL = 0x3AU, F1 = 0x3BU, F2 = 0x3CU,
  F3 = 0x3DU, F4 = 0x3EU, F5 = 0x3FU, F6 = 0x40U,
  F7 = 0x41U, F8 = 0x42U, F9 = 0x43U, F10 = 0x44U,
  NUM_LOCK = 0x45U, SCROLL_LOCK = 0x46U, NUMPAD_7 = 0x47U,
  NUMPAD_8 = 0x48U, NUMPAD_9 = 0x49U, SUBTRACT = 0x4AU,
  NUMPAD_4 = 0x4BU, NUMPAD_5 = 0x4CU, NUMPAD_6 = 0x4DU,
  ADD = 0x4EU, NUMPAD_1 = 0x4FU, NUMPAD_2 = 0x50U,
  NUMPAD_3 = 0x51U, NUMPAD_0 = 0x52U, DECIMAL = 0x53U,
  OEM_102 = 0x56U, F11 = 0x57U, F12 = 0x58U, F13 = 0x64U,
  F14 = 0x65U, F15 = 0x66U, KANA = 0x70U, ABNT_C1 = 0x73U,
  CONVERT = 0x79U, NO_CONVERT = 0x7BU, YEN = 0x7DU,
  NUMPAD_EQUALS = 0x8DU, PREVIOUS_TRACK = 0x90U, KANJI = 0x94U,
  NEXT_TRACK = 0x99U, NUMPAD_ENTER = 0x9CU,
  RIGHT_CONTROL = 0x9DU, MUTE = 0xA0U, CALCULATOR = 0xA1U,
  PLAY_PAUSE = 0xA2U, MEDIA_STOP = 0xA4U, VOLUME_DOWN = 0xAEU,
  VOLUME_UP = 0xB0U, WEB_HOME = 0xB2U, NUMPAD_COMMA = 0xB3U,
  DIVIDE = 0xB5U, SYS_REQUEST = 0xB7U, RIGHT_ALT = 0xB8U,
  PAUSE = 0xC5U, HOME = 0xC7U, UP = 0xC8U, PAGE_UP = 0xC9U,
  LEFT = 0xCBU, RIGHT = 0xCDU, END = 0xCFU, DOWN = 0xD0U,
  PAGE_DOWN = 0xD1U, INSERT = 0xD2U, DELETE_KEY = 0xD3U,
  LEFT_GUI = 0xDBU, RIGHT_GUI = 0xDCU, APPLICATION = 0xDDU,
  POWER = 0xDEU, SLEEP = 0xDFU, WEB_SEARCH = 0xE5U,
  WEB_FAVORITES = 0xE6U, WEB_REFRESH = 0xE7U, WEB_STOP = 0xE8U,
  WEB_FORWARD = 0xE9U, WEB_BACK = 0xEAU, MY_COMPUTER = 0xEBU,
  MAIL = 0xECU, MEDIA_SELECT = 0xEDU,
};

enum class RendererOgre14LegacyMouseButton : std::uint8_t {
  LEFT = 0U,
  RIGHT = 1U,
  MIDDLE = 2U,
  X1 = 3U,
  X2 = 4U,
};

struct RendererOgre14LegacyJoystickState final {
  std::size_t slot = 0U;
  bool raw_device = false;
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  std::string vendor;
  std::vector<std::int32_t> axes_absolute;
  std::vector<std::int32_t> axes_relative;
  std::vector<bool> buttons;
  std::vector<std::uint8_t> hats;
  std::vector<std::pair<std::int32_t, std::int32_t>> sliders;
};

/// Complete state applied after ordered transition callbacks. Relative mouse,
/// wheel, and relative raw-axis values cover only this decoded batch; held
/// digital and absolute device values are authoritative through event_id.
struct RendererOgre14LegacyInputState final {
  std::uint64_t through_event_id = 0U;
  bool focused = false;
  bool window_close_requested = false;
  std::vector<RendererOgre14LegacyKey> pressed_keys;
  std::vector<RendererOgre14LegacyMouseButton> pressed_mouse_buttons;
  std::int32_t mouse_x_pixels = 0;
  std::int32_t mouse_y_pixels = 0;
  std::int32_t mouse_delta_x_pixels = 0;
  std::int32_t mouse_delta_y_pixels = 0;
  float wheel_delta_x = 0.0F;
  float wheel_delta_y = 0.0F;
  std::vector<RendererOgre14LegacyJoystickState> joysticks;
};

/// Target callbacks are non-throwing so a validated batch is applied exactly
/// once: ordered UI transitions first, then one authoritative InputEngine
/// reconciliation. Implementations must not retain string_view arguments.
class IRendererOgre14InputTarget {
public:
  virtual ~IRendererOgre14InputTarget() = default;
  virtual bool ActivateTransport() noexcept = 0;
  virtual void KeyChanged(RendererOgre14LegacyKey key,
                          bool pressed) noexcept = 0;
  virtual void MouseMoved(std::int32_t x, std::int32_t y,
                          std::int32_t delta_x,
                          std::int32_t delta_y) noexcept = 0;
  virtual void MouseButtonChanged(RendererOgre14LegacyMouseButton button,
                                  bool pressed) noexcept = 0;
  virtual void MouseWheel(float delta_x, float delta_y) noexcept = 0;
  virtual void TextInput(std::string_view utf8) noexcept = 0;
  virtual void FocusChanged(bool focused) noexcept = 0;
  virtual void WindowCloseRequested() noexcept = 0;
  virtual bool Reconcile(
      const RendererOgre14LegacyInputState &state) noexcept = 0;
};

enum class RendererOgre14InputApplyStatus : std::uint8_t {
  APPLIED = 0U,
  REJECTED_INVALID_MESSAGE,
  REJECTED_SEQUENCE,
  REJECTED_DEVICE_CAPACITY,
  FAILED_TARGET,
  FAILED_ALLOCATION,
};

struct RendererOgre14InputApplyResult final {
  std::uint32_t version = kRendererOgre14InputAdapterContractVersion;
  RendererOgre14InputApplyStatus status =
      RendererOgre14InputApplyStatus::REJECTED_INVALID_MESSAGE;
  Render::RenderTransportStatus transport_status =
      Render::RenderTransportStatus::INVALID_ARGUMENT;
  std::uint64_t reverse_sequence = 0U;
  std::uint64_t issued_first_event_id = 0U;
  std::uint64_t issued_last_event_id = 0U;
  std::uint64_t resolved_through_event_id = 0U;
  std::uint64_t applied_through_event_id = 0U;
  std::uint32_t ignored_unmapped_scancodes = 0U;
  bool accepted = false;

  [[nodiscard]] bool ok() const noexcept { return accepted; }
  explicit operator bool() const noexcept { return ok(); }
};

/// Converts SDL2 physical scan positions to the OIS/DirectInput key identity
/// used by existing RoR mappings. Keys which OIS cannot represent are returned
/// as KC_UNASSIGNED and remain available through TEXT_INPUT.
[[nodiscard]] RendererOgre14LegacyKey TranslateRendererSdl2ScancodeToLegacy(
    Render::Sdl2PhysicalScancode scancode) noexcept;
[[nodiscard]] RendererOgre14LegacyMouseButton
TranslateRendererSdl2MouseButtonToLegacy(
    Render::Sdl2MouseButton button) noexcept;

/// Main-thread adapter. The decoded-message entry point consumes only the
/// immutable object published by InputEventTransportDecoder. ApplyValidatedBatch
/// exists for focused tests and independently revalidates its public input.
class RendererOgre14InputAdapter final {
public:
  explicit RendererOgre14InputAdapter(
      IRendererOgre14InputTarget &target) noexcept;

  /// Make the renderer transport authoritative before the first gameplay
  /// frame. Idempotent so product startup and the first decoded batch share
  /// one activation edge.
  [[nodiscard]] bool ActivateTarget() noexcept;
  [[nodiscard]] RendererOgre14InputApplyResult Apply(
      const Render::DecodedInputEventTransportMessage &message);
  [[nodiscard]] RendererOgre14InputApplyResult ApplyValidatedBatch(
      std::uint64_t reverse_sequence,
      const Render::InputTransportBatch &batch);

  [[nodiscard]] std::uint64_t last_reverse_sequence() const noexcept {
    return last_reverse_sequence_;
  }
  [[nodiscard]] std::uint64_t applied_through_event_id() const noexcept {
    return applied_through_event_id_;
  }

private:
  struct DeviceSlot final {
    bool active = false;
    bool raw = false;
    std::uint64_t device_id = 0U;
    std::uint64_t generation = 0U;
    std::vector<std::int32_t> axes;
  };

  [[nodiscard]] RendererOgre14InputApplyResult ApplyKnownValidBatch(
      std::uint64_t reverse_sequence,
      const Render::InputTransportBatch &batch);

  IRendererOgre14InputTarget &target_;
  std::array<DeviceSlot, kRendererOgre14LegacyJoystickSlots> slots_{};
  std::uint64_t last_reverse_sequence_ = 0U;
  std::uint64_t applied_through_event_id_ = 0U;
  std::int32_t mouse_x_pixels_ = 0;
  std::int32_t mouse_y_pixels_ = 0;
  bool activated_ = false;
};

[[nodiscard]] bool IsKnownRendererOgre14InputApplyStatus(
    RendererOgre14InputApplyStatus status) noexcept;
[[nodiscard]] const char *ToString(
    RendererOgre14InputApplyStatus status) noexcept;

} // namespace RoR
