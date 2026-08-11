/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral game input target used by direct and bridged paths.

#pragma once

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RoR {

constexpr std::size_t kRendererGameJoystickSlots = 10U;
constexpr std::size_t kRendererGameMaximumJoystickButtons = 128U;

/// Exact visible-window coordinate domains used by direct game input. SDL
/// reports pointer positions in logical window coordinates while RoR's direct
/// renderer contract publishes backing pixels. Keeping both extents prevents
/// the hidden Ogre 14 resource host from becoming an accidental input/UI
/// authority in the combined process.
struct RendererGameDisplayMetrics final {
  std::uint32_t logical_width = 0U;
  std::uint32_t logical_height = 0U;
  std::uint32_t pixel_width = 0U;
  std::uint32_t pixel_height = 0U;

  [[nodiscard]] bool valid() const noexcept {
    constexpr std::uint32_t kMaximumExtent = 32768U;
    return logical_width > 0U && logical_height > 0U && pixel_width > 0U &&
           pixel_height > 0U && logical_width <= kMaximumExtent &&
           logical_height <= kMaximumExtent && pixel_width <= kMaximumExtent &&
           pixel_height <= kMaximumExtent;
  }
};

/// Convert a renderer backing-pixel coordinate into the logical window domain
/// consumed by OIS callbacks and Dear ImGui. The presenter state intentionally
/// remains in backing pixels; conversion happens only at the direct game seam
/// so the temporary bridge contract is unchanged.
inline std::int32_t RendererGameLogicalCoordinate(
    std::int32_t pixel_coordinate, std::uint32_t logical_extent,
    std::uint32_t pixel_extent) noexcept {
  if (logical_extent == 0U || pixel_extent == 0U) {
    return 0;
  }
  const std::int64_t numerator =
      static_cast<std::int64_t>(pixel_coordinate) *
      static_cast<std::int64_t>(logical_extent);
  const std::int64_t half = static_cast<std::int64_t>(pixel_extent / 2U);
  const std::int64_t rounded = numerator >= 0
      ? (numerator + half) / static_cast<std::int64_t>(pixel_extent)
      : (numerator - half) / static_cast<std::int64_t>(pixel_extent);
  if (rounded <=
      static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)())) {
    return (std::numeric_limits<std::int32_t>::min)();
  }
  if (rounded >=
      static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)())) {
    return (std::numeric_limits<std::int32_t>::max)();
  }
  return static_cast<std::int32_t>(rounded);
}

/// Numeric values are the stable OIS/DirectInput scan identities consumed by
/// existing RoR input.map files. The direct renderer translates physical SDL
/// positions before invoking this renderer-independent game target.
enum class RendererGameKey : std::uint16_t {
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

enum class RendererGameMouseButton : std::uint8_t {
  LEFT = 0U,
  RIGHT = 1U,
  MIDDLE = 2U,
  X1 = 3U,
  X2 = 4U,
};

struct RendererGameJoystickState final {
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

/// Complete input state after ordered transition callbacks. Relative mouse,
/// wheel, and relative raw-axis values cover only this poll; held digital and
/// absolute device values are authoritative through event_id.
struct RendererGameInputState final {
  std::uint64_t through_event_id = 0U;
  bool focused = false;
  bool window_close_requested = false;
  std::vector<RendererGameKey> pressed_keys;
  std::vector<RendererGameMouseButton> pressed_mouse_buttons;
  std::int32_t mouse_x_pixels = 0;
  std::int32_t mouse_y_pixels = 0;
  std::int32_t mouse_delta_x_pixels = 0;
  std::int32_t mouse_delta_y_pixels = 0;
  float wheel_delta_x = 0.0F;
  float wheel_delta_y = 0.0F;
  std::vector<RendererGameJoystickState> joysticks;
};

namespace Detail {

/// OIS-free state policy used by InputEngine immediately before invoking the
/// historical AppContext mouse callbacks. The final full-state reconciliation
/// remains authoritative; this staging exists only so synchronous camera,
/// scene-picking, and overlay callbacks observe the transition being delivered
/// instead of the preceding frame.
struct RendererGameMouseCallbackState final {
  std::int32_t x_absolute = 0;
  std::int32_t y_absolute = 0;
  std::int32_t x_relative = 0;
  std::int32_t y_relative = 0;
  std::int32_t wheel_relative = 0;
  std::uint32_t buttons = 0U;
};

inline void StageRendererGameMouseMotion(
    RendererGameMouseCallbackState &state, std::int32_t x, std::int32_t y,
    std::int32_t delta_x, std::int32_t delta_y) noexcept {
  state.x_absolute = x;
  state.y_absolute = y;
  state.x_relative = delta_x;
  state.y_relative = delta_y;
  // A motion callback must not replay a preceding wheel transition.
  state.wheel_relative = 0;
}

inline bool StageRendererGameMouseButton(
    RendererGameMouseCallbackState &state, std::uint8_t button,
    bool pressed) noexcept {
  if (button > 4U) {
    return false;
  }
  const std::uint32_t mask = std::uint32_t{1U} << button;
  if (pressed) {
    state.buttons |= mask;
  } else {
    state.buttons &= ~mask;
  }
  return true;
}

inline std::int32_t RendererGameWheelUnits(float delta) noexcept {
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "direct wheel input requires IEEE-754 binary32");
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &delta, sizeof(bits));
  if ((bits & 0x7f800000U) == 0x7f800000U) {
    return 0;
  }
  constexpr double kUnitsPerStep = 120.0;
  const double units = static_cast<double>(delta) * kUnitsPerStep;
  if (units <= static_cast<double>((std::numeric_limits<std::int32_t>::min)())) {
    return (std::numeric_limits<std::int32_t>::min)();
  }
  if (units >= static_cast<double>((std::numeric_limits<std::int32_t>::max)())) {
    return (std::numeric_limits<std::int32_t>::max)();
  }
  return static_cast<std::int32_t>(std::llround(units));
}

inline void StageRendererGameMouseWheel(
    RendererGameMouseCallbackState &state, float delta_y) noexcept {
  // A wheel callback must not replay preceding camera motion. The final
  // authoritative reconciliation restores the poll-wide accumulated deltas.
  state.x_relative = 0;
  state.y_relative = 0;
  state.wheel_relative = RendererGameWheelUnits(delta_y);
}

} // namespace Detail

/// Product input callbacks shared by the direct in-process renderer and the
/// temporary decoded bridge. Implementations must not retain string_view.
class IRendererGameInputTarget {
public:
  virtual ~IRendererGameInputTarget() = default;
  virtual bool ActivateInput() noexcept = 0;
  /// Called by the direct presenter after SDL has refreshed native state and
  /// before it drains any transition callbacks. Temporary bridge targets do
  /// not call this seam and therefore retain their existing coordinate path.
  virtual bool DisplayMetricsChanged(
      const RendererGameDisplayMetrics &metrics) noexcept {
    return metrics.valid();
  }
  virtual void KeyChanged(RendererGameKey key, bool pressed) noexcept = 0;
  virtual void MouseMoved(std::int32_t x, std::int32_t y,
                          std::int32_t delta_x,
                          std::int32_t delta_y) noexcept = 0;
  virtual void MouseButtonChanged(RendererGameMouseButton button,
                                  bool pressed) noexcept = 0;
  virtual void MouseWheel(float delta_x, float delta_y) noexcept = 0;
  virtual void TextInput(std::string_view utf8) noexcept = 0;
  virtual void FocusChanged(bool focused) noexcept = 0;
  virtual void WindowCloseRequested() noexcept = 0;
  virtual bool Reconcile(const RendererGameInputState &state) noexcept = 0;
};

/// Translate the pinned SDL 2.32.10 numeric physical identities without
/// importing SDL or the temporary transport schema into the game target.
[[nodiscard]] RendererGameKey
TranslateRendererSdlScancodeToGame(std::uint16_t scancode) noexcept;
[[nodiscard]] bool TryTranslateRendererSdlMouseButtonToGame(
    std::uint8_t button, RendererGameMouseButton &translated) noexcept;

} // namespace RoR
