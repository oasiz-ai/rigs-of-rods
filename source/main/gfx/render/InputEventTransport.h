/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Versioned renderer-child to game-process input event transport.

#pragma once

#include "RenderTransportEnvelope.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kInputEventTransportPayloadVersion = 1U;
constexpr std::uint32_t kInputEventTransportSdl2ScancodeVersion = 1U;
constexpr std::uint32_t kInputEventTransportSdl2GamepadVersion = 1U;
constexpr std::uint64_t kInputEventTransportMaximumPayloadBytes =
    4ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kInputEventTransportMaximumDecodedAllocationBytes =
    8ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kInputEventTransportMaximumEvents = 8192U;
constexpr std::uint32_t kInputEventTransportMaximumTextBytesPerEvent = 4096U;
constexpr std::uint32_t kInputEventTransportMaximumTotalTextBytes =
    1024U * 1024U;
constexpr std::uint32_t kInputEventTransportMaximumPressedScancodes = 512U;
constexpr std::uint32_t kInputEventTransportMaximumPressedMouseButtons = 5U;
constexpr std::uint32_t kInputEventTransportMaximumDevices = 10U;
constexpr std::uint32_t kInputEventTransportMaximumGamepads = 10U;
constexpr std::uint32_t kInputEventTransportMaximumGamepadButtons = 21U;
constexpr std::size_t kInputEventTransportGamepadAxisCount = 6U;
constexpr std::uint32_t kInputEventTransportMaximumRawDevices = 10U;
constexpr std::uint32_t kInputEventTransportMaximumRawAxes = 32U;
constexpr std::uint32_t kInputEventTransportMaximumRawButtons = 128U;
constexpr std::uint32_t kInputEventTransportMaximumRawHats = 4U;
constexpr std::uint32_t kInputEventTransportMaximumRawSliders = 4U;
constexpr std::uint32_t kInputEventTransportMaximumTrackedDeviceIdentities =
    256U;
constexpr std::size_t kInputEventTransportMaximumDeviceGenerationsPerBatch =
    64U;
constexpr float kInputEventTransportMaximumWheelMagnitude = 4096.0F;

/// Values and names are pinned to SDL 2.32.10 SDL_Scancode. No SDL headers or
/// layout-dependent keycodes enter this portable boundary.
enum class Sdl2PhysicalScancode : std::uint16_t {
  A = 4U,
  B = 5U,
  C = 6U,
  D = 7U,
  E = 8U,
  F = 9U,
  G = 10U,
  H = 11U,
  I = 12U,
  J = 13U,
  K = 14U,
  L = 15U,
  M = 16U,
  N = 17U,
  O = 18U,
  P = 19U,
  Q = 20U,
  R = 21U,
  S = 22U,
  T = 23U,
  U = 24U,
  V = 25U,
  W = 26U,
  X = 27U,
  Y = 28U,
  Z = 29U,
  DIGIT_1 = 30U,
  DIGIT_2 = 31U,
  DIGIT_3 = 32U,
  DIGIT_4 = 33U,
  DIGIT_5 = 34U,
  DIGIT_6 = 35U,
  DIGIT_7 = 36U,
  DIGIT_8 = 37U,
  DIGIT_9 = 38U,
  DIGIT_0 = 39U,
  RETURN = 40U,
  ESCAPE = 41U,
  BACKSPACE = 42U,
  TAB = 43U,
  SPACE = 44U,
  MINUS = 45U,
  EQUALS = 46U,
  LEFT_BRACKET = 47U,
  RIGHT_BRACKET = 48U,
  BACKSLASH = 49U,
  NON_US_HASH = 50U,
  SEMICOLON = 51U,
  APOSTROPHE = 52U,
  GRAVE = 53U,
  COMMA = 54U,
  PERIOD = 55U,
  SLASH = 56U,
  CAPS_LOCK = 57U,
  F1 = 58U,
  F2 = 59U,
  F3 = 60U,
  F4 = 61U,
  F5 = 62U,
  F6 = 63U,
  F7 = 64U,
  F8 = 65U,
  F9 = 66U,
  F10 = 67U,
  F11 = 68U,
  F12 = 69U,
  PRINT_SCREEN = 70U,
  SCROLL_LOCK = 71U,
  PAUSE = 72U,
  INSERT = 73U,
  HOME = 74U,
  PAGE_UP = 75U,
  DELETE_KEY = 76U,
  END = 77U,
  PAGE_DOWN = 78U,
  RIGHT = 79U,
  LEFT = 80U,
  DOWN = 81U,
  UP = 82U,
  NUM_LOCK_CLEAR = 83U,
  KP_DIVIDE = 84U,
  KP_MULTIPLY = 85U,
  KP_MINUS = 86U,
  KP_PLUS = 87U,
  KP_ENTER = 88U,
  KP_1 = 89U,
  KP_2 = 90U,
  KP_3 = 91U,
  KP_4 = 92U,
  KP_5 = 93U,
  KP_6 = 94U,
  KP_7 = 95U,
  KP_8 = 96U,
  KP_9 = 97U,
  KP_0 = 98U,
  KP_PERIOD = 99U,
  NON_US_BACKSLASH = 100U,
  APPLICATION = 101U,
  POWER = 102U,
  KP_EQUALS = 103U,
  F13 = 104U,
  F14 = 105U,
  F15 = 106U,
  F16 = 107U,
  F17 = 108U,
  F18 = 109U,
  F19 = 110U,
  F20 = 111U,
  F21 = 112U,
  F22 = 113U,
  F23 = 114U,
  F24 = 115U,
  EXECUTE = 116U,
  HELP = 117U,
  MENU = 118U,
  SELECT = 119U,
  STOP = 120U,
  AGAIN = 121U,
  UNDO = 122U,
  CUT = 123U,
  COPY = 124U,
  PASTE = 125U,
  FIND = 126U,
  MUTE = 127U,
  VOLUME_UP = 128U,
  VOLUME_DOWN = 129U,
  KP_COMMA = 133U,
  KP_EQUALS_AS400 = 134U,
  INTERNATIONAL_1 = 135U,
  INTERNATIONAL_2 = 136U,
  INTERNATIONAL_3 = 137U,
  INTERNATIONAL_4 = 138U,
  INTERNATIONAL_5 = 139U,
  INTERNATIONAL_6 = 140U,
  INTERNATIONAL_7 = 141U,
  INTERNATIONAL_8 = 142U,
  INTERNATIONAL_9 = 143U,
  LANG_1 = 144U,
  LANG_2 = 145U,
  LANG_3 = 146U,
  LANG_4 = 147U,
  LANG_5 = 148U,
  LANG_6 = 149U,
  LANG_7 = 150U,
  LANG_8 = 151U,
  LANG_9 = 152U,
  ALT_ERASE = 153U,
  SYS_REQ = 154U,
  CANCEL = 155U,
  CLEAR = 156U,
  PRIOR = 157U,
  RETURN_2 = 158U,
  SEPARATOR = 159U,
  OUT = 160U,
  OPER = 161U,
  CLEAR_AGAIN = 162U,
  CR_SEL = 163U,
  EX_SEL = 164U,
  KP_00 = 176U,
  KP_000 = 177U,
  THOUSANDS_SEPARATOR = 178U,
  DECIMAL_SEPARATOR = 179U,
  CURRENCY_UNIT = 180U,
  CURRENCY_SUBUNIT = 181U,
  KP_LEFT_PAREN = 182U,
  KP_RIGHT_PAREN = 183U,
  KP_LEFT_BRACE = 184U,
  KP_RIGHT_BRACE = 185U,
  KP_TAB = 186U,
  KP_BACKSPACE = 187U,
  KP_A = 188U,
  KP_B = 189U,
  KP_C = 190U,
  KP_D = 191U,
  KP_E = 192U,
  KP_F = 193U,
  KP_XOR = 194U,
  KP_POWER = 195U,
  KP_PERCENT = 196U,
  KP_LESS = 197U,
  KP_GREATER = 198U,
  KP_AMPERSAND = 199U,
  KP_DOUBLE_AMPERSAND = 200U,
  KP_VERTICAL_BAR = 201U,
  KP_DOUBLE_VERTICAL_BAR = 202U,
  KP_COLON = 203U,
  KP_HASH = 204U,
  KP_SPACE = 205U,
  KP_AT = 206U,
  KP_EXCLAMATION = 207U,
  KP_MEMORY_STORE = 208U,
  KP_MEMORY_RECALL = 209U,
  KP_MEMORY_CLEAR = 210U,
  KP_MEMORY_ADD = 211U,
  KP_MEMORY_SUBTRACT = 212U,
  KP_MEMORY_MULTIPLY = 213U,
  KP_MEMORY_DIVIDE = 214U,
  KP_PLUS_MINUS = 215U,
  KP_CLEAR = 216U,
  KP_CLEAR_ENTRY = 217U,
  KP_BINARY = 218U,
  KP_OCTAL = 219U,
  KP_DECIMAL = 220U,
  KP_HEXADECIMAL = 221U,
  LEFT_CTRL = 224U,
  LEFT_SHIFT = 225U,
  LEFT_ALT = 226U,
  LEFT_GUI = 227U,
  RIGHT_CTRL = 228U,
  RIGHT_SHIFT = 229U,
  RIGHT_ALT = 230U,
  RIGHT_GUI = 231U,
  MODE = 257U,
  AUDIO_NEXT = 258U,
  AUDIO_PREVIOUS = 259U,
  AUDIO_STOP = 260U,
  AUDIO_PLAY = 261U,
  AUDIO_MUTE = 262U,
  MEDIA_SELECT = 263U,
  WWW = 264U,
  MAIL = 265U,
  CALCULATOR = 266U,
  COMPUTER = 267U,
  AC_SEARCH = 268U,
  AC_HOME = 269U,
  AC_BACK = 270U,
  AC_FORWARD = 271U,
  AC_STOP = 272U,
  AC_REFRESH = 273U,
  AC_BOOKMARKS = 274U,
  BRIGHTNESS_DOWN = 275U,
  BRIGHTNESS_UP = 276U,
  DISPLAY_SWITCH = 277U,
  KEYBOARD_ILLUMINATION_TOGGLE = 278U,
  KEYBOARD_ILLUMINATION_DOWN = 279U,
  KEYBOARD_ILLUMINATION_UP = 280U,
  EJECT = 281U,
  SLEEP = 282U,
  APP_1 = 283U,
  APP_2 = 284U,
  AUDIO_REWIND = 285U,
  AUDIO_FAST_FORWARD = 286U,
  SOFT_LEFT = 287U,
  SOFT_RIGHT = 288U,
  CALL = 289U,
  END_CALL = 290U,
};

enum class InputTransportClockDomain : std::uint8_t {
  HOST_MONOTONIC_NANOSECONDS = 1U,
};

enum class InputTransportDigitalState : std::uint8_t {
  RELEASED = 0U,
  PRESSED = 1U,
};

enum class InputTransportFocusState : std::uint8_t {
  LOST = 0U,
  GAINED = 1U,
};

enum class InputTransportDeviceConnectionState : std::uint8_t {
  DISCONNECTED = 0U,
  CONNECTED = 1U,
};

/// Numeric values match SDL_BUTTON_LEFT through SDL_BUTTON_X2.
enum class Sdl2MouseButton : std::uint8_t {
  LEFT = 1U,
  MIDDLE = 2U,
  RIGHT = 3U,
  X1 = 4U,
  X2 = 5U,
};

/// Numeric values match SDL_MouseWheelDirection.
enum class Sdl2MouseWheelDirection : std::uint8_t {
  NORMAL = 0U,
  FLIPPED = 1U,
};

/// Numeric values match SDL_GameControllerButton through SDL 2.32.10.
enum class Sdl2GamepadButton : std::uint8_t {
  A = 0U,
  B = 1U,
  X = 2U,
  Y = 3U,
  BACK = 4U,
  GUIDE = 5U,
  START = 6U,
  LEFT_STICK = 7U,
  RIGHT_STICK = 8U,
  LEFT_SHOULDER = 9U,
  RIGHT_SHOULDER = 10U,
  DPAD_UP = 11U,
  DPAD_DOWN = 12U,
  DPAD_LEFT = 13U,
  DPAD_RIGHT = 14U,
  MISC_1 = 15U,
  PADDLE_1 = 16U,
  PADDLE_2 = 17U,
  PADDLE_3 = 18U,
  PADDLE_4 = 19U,
  TOUCHPAD = 20U,
};

/// Numeric values and wire samples match SDL_GameControllerAxis in SDL
/// 2.32.10 exactly. Sticks use signed int16 [-32768, 32767]; triggers use
/// [0, 32767]. No float normalization is performed at this boundary.
enum class Sdl2GamepadAxis : std::uint8_t {
  LEFT_X = 0U,
  LEFT_Y = 1U,
  RIGHT_X = 2U,
  RIGHT_Y = 3U,
  TRIGGER_LEFT = 4U,
  TRIGGER_RIGHT = 5U,
};

enum class InputTransportRawDeviceClass : std::uint8_t {
  JOYSTICK = 1U,
  WHEEL = 2U,
  FLIGHT_STICK = 3U,
  THROTTLE = 4U,
  OTHER = 5U,
};

enum class InputTransportRawAxisMode : std::uint8_t {
  ABSOLUTE = 1U,
  RELATIVE = 2U,
};

/// Numeric values match SDL 2 hat bit semantics, without importing SDL.
enum class Sdl2HatState : std::uint8_t {
  CENTERED = 0U,
  UP = 1U,
  RIGHT = 2U,
  RIGHT_UP = 3U,
  DOWN = 4U,
  RIGHT_DOWN = 6U,
  LEFT = 8U,
  LEFT_UP = 9U,
  LEFT_DOWN = 12U,
};

enum class InputTransportEventKind : std::uint8_t {
  KEYBOARD_KEY = 1U,
  MOUSE_MOTION = 2U,
  MOUSE_BUTTON = 3U,
  MOUSE_WHEEL = 4U,
  GAMEPAD_CONNECTION = 5U,
  GAMEPAD_BUTTON = 6U,
  GAMEPAD_AXIS = 7U,
  TEXT_INPUT = 8U,
  FOCUS = 9U,
  WINDOW_CLOSE = 10U,
  RAW_DEVICE_CONNECTION = 11U,
  RAW_BUTTON = 12U,
  RAW_AXIS = 13U,
  RAW_HAT = 14U,
  RAW_SLIDER = 15U,
};

struct InputTransportKeyboardKeyEvent {
  Sdl2PhysicalScancode scancode = Sdl2PhysicalScancode::A;
  InputTransportDigitalState state = InputTransportDigitalState::RELEASED;
  bool repeat = false;
};

struct InputTransportMouseMotionEvent {
  std::int32_t position_x_pixels = 0;
  std::int32_t position_y_pixels = 0;
  std::int32_t delta_x_pixels = 0;
  std::int32_t delta_y_pixels = 0;
};

struct InputTransportMouseButtonEvent {
  Sdl2MouseButton button = Sdl2MouseButton::LEFT;
  InputTransportDigitalState state = InputTransportDigitalState::RELEASED;
};

struct InputTransportMouseWheelEvent {
  float delta_x = 0.0F;
  float delta_y = 0.0F;
  Sdl2MouseWheelDirection direction = Sdl2MouseWheelDirection::NORMAL;
};

struct InputTransportGamepadConnectionEvent {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  InputTransportDeviceConnectionState state =
      InputTransportDeviceConnectionState::DISCONNECTED;
};

struct InputTransportGamepadButtonEvent {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  Sdl2GamepadButton button = Sdl2GamepadButton::A;
  InputTransportDigitalState state = InputTransportDigitalState::RELEASED;
};

struct InputTransportGamepadAxisEvent {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  Sdl2GamepadAxis axis = Sdl2GamepadAxis::LEFT_X;
  std::int16_t value = 0;
};

struct InputTransportTextInputEvent {
  std::string utf8_text;
};

struct InputTransportFocusEvent {
  InputTransportFocusState state = InputTransportFocusState::LOST;
};

struct InputTransportWindowCloseEvent {};

struct InputTransportRawAxisDescriptor {
  std::uint16_t index = 0U;
  InputTransportRawAxisMode mode = InputTransportRawAxisMode::ABSOLUTE;
  std::int32_t logical_minimum = -32768;
  std::int32_t logical_maximum = 32767;
  std::int32_t center = 0;
  std::int32_t deadzone_minimum = 0;
  std::int32_t deadzone_maximum = 0;
};

struct InputTransportRawSliderDescriptor {
  std::uint16_t index = 0U;
  InputTransportRawAxisDescriptor x_axis;
  InputTransportRawAxisDescriptor y_axis;
};

/// Stable physical identity plus a connection generation. The name digest is
/// SHA-256 over the producer's UTF-8 device name bytes; raw names do not cross
/// the process boundary. Vendor/product/version may be zero when unavailable.
struct InputTransportRawDeviceDescriptor {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  InputTransportRawDeviceClass device_class =
      InputTransportRawDeviceClass::JOYSTICK;
  std::array<std::uint8_t, 16U> guid{};
  std::uint16_t vendor_id = 0U;
  std::uint16_t product_id = 0U;
  std::uint16_t product_version = 0U;
  std::array<std::uint8_t, 32U> name_sha256{};
  std::vector<InputTransportRawAxisDescriptor> axes;
  std::uint16_t button_count = 0U;
  std::uint8_t hat_count = 0U;
  std::vector<InputTransportRawSliderDescriptor> sliders;
};

struct InputTransportRawDeviceConnectionEvent {
  InputTransportDeviceConnectionState state =
      InputTransportDeviceConnectionState::DISCONNECTED;
  InputTransportRawDeviceDescriptor descriptor;
};

struct InputTransportRawButtonEvent {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  std::uint16_t button_index = 0U;
  InputTransportDigitalState state = InputTransportDigitalState::RELEASED;
};

struct InputTransportRawAxisEvent {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  std::uint16_t axis_index = 0U;
  std::int32_t value = 0;
};

struct InputTransportRawHatEvent {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  std::uint16_t hat_index = 0U;
  Sdl2HatState state = Sdl2HatState::CENTERED;
};

struct InputTransportRawSliderEvent {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  std::uint16_t slider_index = 0U;
  std::int32_t x = 0;
  std::int32_t y = 0;
};

using InputTransportEventPayload =
    std::variant<InputTransportKeyboardKeyEvent, InputTransportMouseMotionEvent,
                 InputTransportMouseButtonEvent, InputTransportMouseWheelEvent,
                 InputTransportGamepadConnectionEvent,
                 InputTransportGamepadButtonEvent,
                 InputTransportGamepadAxisEvent, InputTransportTextInputEvent,
                 InputTransportFocusEvent, InputTransportWindowCloseEvent,
                 InputTransportRawDeviceConnectionEvent,
                 InputTransportRawButtonEvent, InputTransportRawAxisEvent,
                 InputTransportRawHatEvent, InputTransportRawSliderEvent>;

struct InputTransportEvent {
  std::uint64_t event_id = 0U;
  std::uint64_t host_timestamp_ns = 0U;
  InputTransportEventPayload payload;
};

struct InputTransportGamepadReconciliationState {
  std::uint64_t device_id = 0U;
  std::uint64_t connection_generation = 0U;
  std::vector<Sdl2GamepadButton> pressed_buttons;
  std::array<std::int16_t, kInputEventTransportGamepadAxisCount> axes{};
};

struct InputTransportRawSliderState {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

struct InputTransportRawDeviceReconciliationState {
  InputTransportRawDeviceDescriptor descriptor;
  std::vector<std::uint16_t> pressed_buttons;
  std::vector<std::int32_t> axes;
  std::vector<Sdl2HatState> hats;
  std::vector<InputTransportRawSliderState> sliders;
};

/// Complete authoritative state through one event-ID watermark. The producer
/// may coalesce event IDs under back-pressure; this snapshot heals any gap.
struct InputTransportReconciliationSnapshot {
  std::uint64_t through_event_id = 0U;
  std::uint64_t host_timestamp_ns = 0U;
  InputTransportFocusState focus = InputTransportFocusState::LOST;
  bool window_close_requested = false;
  std::vector<Sdl2PhysicalScancode> pressed_scancodes;
  std::vector<Sdl2MouseButton> pressed_mouse_buttons;
  std::vector<InputTransportGamepadReconciliationState> gamepads;
  std::vector<InputTransportRawDeviceReconciliationState> raw_devices;
};

struct InputTransportBatch {
  std::uint32_t version = kInputEventTransportPayloadVersion;
  InputTransportClockDomain clock_domain =
      InputTransportClockDomain::HOST_MONOTONIC_NANOSECONDS;
  /// Nonzero identity for the host clock epoch. It remains stable for the
  /// lifetime of one reverse-direction bridge connection.
  std::uint64_t clock_origin_id = 0U;
  std::vector<InputTransportEvent> events;
  InputTransportReconciliationSnapshot reconciliation;
};

[[nodiscard]] bool
IsKnownSdl2PhysicalScancode(Sdl2PhysicalScancode scancode) noexcept;
[[nodiscard]] bool
IsKnownInputTransportEventKind(InputTransportEventKind kind) noexcept;
[[nodiscard]] InputTransportEventKind
InputTransportPayloadKind(const InputTransportEventPayload &payload) noexcept;
[[nodiscard]] bool IsValidInputTransportUtf8(const std::string &text) noexcept;
[[nodiscard]] RenderTransportStatus
ValidateInputTransportBatch(const InputTransportBatch &batch) noexcept;

using InputEventTransportEncodeResult = RenderTransportEnvelopeEncodeResult;

class DecodedInputEventTransportMessage final {
public:
  DecodedInputEventTransportMessage(const DecodedInputEventTransportMessage &) =
      delete;
  DecodedInputEventTransportMessage &
  operator=(const DecodedInputEventTransportMessage &) = delete;
  DecodedInputEventTransportMessage(DecodedInputEventTransportMessage &&) =
      delete;
  DecodedInputEventTransportMessage &
  operator=(DecodedInputEventTransportMessage &&) = delete;
  ~DecodedInputEventTransportMessage() = default;

  [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }
  [[nodiscard]] RenderTransportMessageKind kind() const noexcept {
    return RenderTransportMessageKind::INPUT_EVENT_BATCH_V1;
  }
  [[nodiscard]] const std::shared_ptr<const InputTransportBatch> &
  batch() const noexcept {
    return batch_;
  }

private:
  DecodedInputEventTransportMessage(
      std::uint64_t sequence,
      std::shared_ptr<const InputTransportBatch> batch) noexcept
      : sequence_(sequence), batch_(std::move(batch)) {}

  std::uint64_t sequence_ = 0U;
  std::shared_ptr<const InputTransportBatch> batch_;

  friend class InputEventTransportDecoder;
};

struct InputEventTransportDecodeResult {
  std::shared_ptr<const DecodedInputEventTransportMessage> message;
  RenderTransportStatus status = RenderTransportStatus::INVALID_ARGUMENT;

  [[nodiscard]] bool ok() const noexcept {
    return status == RenderTransportStatus::OK && message != nullptr;
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Transactional receiver for the renderer-child -> game-process direction.
/// Standalone instances own a private sequence; a live session supplies the
/// reverse sequence shared with acknowledgement and control messages. That
/// reverse state remains independent of the game-to-renderer scene/asset state.
class InputEventTransportDecoder final {
public:
  explicit InputEventTransportDecoder(
      std::uint64_t first_expected_sequence = 1U) noexcept;
  explicit InputEventTransportDecoder(
      RenderTransportSequenceState &shared_sequence_state) noexcept;

  InputEventTransportDecoder(const InputEventTransportDecoder &) = delete;
  InputEventTransportDecoder &
  operator=(const InputEventTransportDecoder &) = delete;
  InputEventTransportDecoder(InputEventTransportDecoder &&) = delete;
  InputEventTransportDecoder &operator=(InputEventTransportDecoder &&) = delete;
  ~InputEventTransportDecoder() = default;

  [[nodiscard]] InputEventTransportDecodeResult
  Accept(const std::vector<std::uint8_t> &frame);

  [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept {
    return sequence_state_->next_expected_sequence();
  }
  [[nodiscard]] std::uint64_t last_accepted_sequence() const noexcept {
    return sequence_state_->last_accepted_sequence();
  }
  [[nodiscard]] std::uint64_t last_event_id() const noexcept {
    return last_event_id_;
  }
  [[nodiscard]] std::uint64_t last_host_timestamp_ns() const noexcept {
    return last_host_timestamp_ns_;
  }
  [[nodiscard]] const InputTransportReconciliationSnapshot *
  reconciliation() const noexcept {
    return has_reconciliation_ ? &reconciliation_ : nullptr;
  }
  [[nodiscard]] const std::shared_ptr<const DecodedInputEventTransportMessage> &
  published() const noexcept {
    return published_;
  }

private:
  struct DeviceGenerationRecord {
    std::uint64_t device_id = 0U;
    std::uint64_t connection_generation = 0U;
    bool raw = false;
    bool has_raw_descriptor = false;
    InputTransportRawDeviceDescriptor raw_descriptor;
  };

  RenderTransportSequenceState owned_sequence_state_;
  RenderTransportSequenceState *sequence_state_ = &owned_sequence_state_;
  bool clock_configured_ = false;
  InputTransportClockDomain clock_domain_ =
      InputTransportClockDomain::HOST_MONOTONIC_NANOSECONDS;
  std::uint64_t clock_origin_id_ = 0U;
  std::uint64_t last_event_id_ = 0U;
  std::uint64_t last_host_timestamp_ns_ = 0U;
  bool has_reconciliation_ = false;
  InputTransportReconciliationSnapshot reconciliation_;
  std::vector<DeviceGenerationRecord> device_generations_;
  std::shared_ptr<const DecodedInputEventTransportMessage> published_;
};

[[nodiscard]] InputEventTransportEncodeResult
EncodeInputEventTransportFrame(std::uint64_t sequence,
                               const InputTransportBatch &batch);

} // namespace RoR::Render
