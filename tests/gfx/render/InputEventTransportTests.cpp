/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "InputEventTransport.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

static_assert(static_cast<std::uint16_t>(Sdl2PhysicalScancode::A) == 4U);
static_assert(static_cast<std::uint16_t>(Sdl2PhysicalScancode::RETURN) == 40U);
static_assert(static_cast<std::uint16_t>(Sdl2PhysicalScancode::LEFT_CTRL) ==
              224U);
static_assert(static_cast<std::uint16_t>(Sdl2PhysicalScancode::END_CALL) ==
              290U);
static_assert(static_cast<std::uint8_t>(Sdl2MouseButton::LEFT) == 1U);
static_assert(static_cast<std::uint8_t>(Sdl2GamepadButton::TOUCHPAD) == 20U);
static_assert(static_cast<std::uint8_t>(Sdl2GamepadAxis::TRIGGER_RIGHT) == 5U);
static_assert(static_cast<std::uint8_t>(Sdl2HatState::LEFT_DOWN) == 12U);
static_assert(!std::is_copy_constructible_v<DecodedInputEventTransportMessage>);
static_assert(!std::is_move_constructible_v<DecodedInputEventTransportMessage>);

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "input event transport test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void RequireStatus(RenderTransportStatus actual, RenderTransportStatus expected,
                   const char *message) {
  if (actual != expected) {
    std::cerr << "input event transport test failed: " << message
              << " (actual=" << static_cast<unsigned>(actual)
              << ", expected=" << static_cast<unsigned>(expected) << ")\n";
    std::exit(EXIT_FAILURE);
  }
}

template <typename Payload>
InputTransportEvent Event(std::uint64_t id, std::uint64_t timestamp,
                          Payload payload) {
  InputTransportEvent event;
  event.event_id = id;
  event.host_timestamp_ns = timestamp;
  event.payload = std::move(payload);
  return event;
}

InputTransportRawAxisDescriptor
RawAxis(std::uint16_t index, std::int32_t minimum, std::int32_t maximum,
        std::int32_t center, std::int32_t deadzone_minimum,
        std::int32_t deadzone_maximum,
        InputTransportRawAxisMode mode = InputTransportRawAxisMode::ABSOLUTE) {
  InputTransportRawAxisDescriptor axis;
  axis.index = index;
  axis.mode = mode;
  axis.logical_minimum = minimum;
  axis.logical_maximum = maximum;
  axis.center = center;
  axis.deadzone_minimum = deadzone_minimum;
  axis.deadzone_maximum = deadzone_maximum;
  return axis;
}

InputTransportRawDeviceDescriptor
RawWheelDescriptor(std::uint64_t generation = 3U) {
  InputTransportRawDeviceDescriptor descriptor;
  descriptor.device_id = 200U;
  descriptor.connection_generation = generation;
  descriptor.device_class = InputTransportRawDeviceClass::WHEEL;
  for (std::size_t index = 0U; index < descriptor.guid.size(); ++index) {
    descriptor.guid[index] = static_cast<std::uint8_t>(index + 1U);
  }
  descriptor.vendor_id = 0x046dU;
  descriptor.product_id = 0xc24fU;
  descriptor.product_version = 0x0101U;
  const std::string name = "RoR contract wheel";
  descriptor.name_sha256 = ComputeRenderTransportPayloadDigest(
      reinterpret_cast<const std::uint8_t *>(name.data()), name.size());
  descriptor.axes.push_back(RawAxis(0U, -32768, 32767, 0, -512, 512));
  descriptor.axes.push_back(RawAxis(1U, -1000, 1000, 0, -10, 10,
                                    InputTransportRawAxisMode::RELATIVE));
  descriptor.button_count = 8U;
  descriptor.hat_count = 1U;
  InputTransportRawSliderDescriptor slider;
  slider.index = 0U;
  slider.x_axis = RawAxis(0U, -1000, 1000, 0, -5, 5);
  slider.y_axis = RawAxis(1U, -1000, 1000, 0, -5, 5);
  descriptor.sliders.push_back(slider);
  return descriptor;
}

InputTransportRawDeviceReconciliationState RichRawState() {
  InputTransportRawDeviceReconciliationState state;
  state.descriptor = RawWheelDescriptor();
  state.pressed_buttons = {2U};
  state.axes = {-12345, 50};
  state.hats = {Sdl2HatState::RIGHT_UP};
  state.sliders = {{100, -200}};
  return state;
}

InputTransportBatch
EmptyBatch(InputTransportFocusState focus = InputTransportFocusState::GAINED,
           std::uint64_t timestamp = 100U) {
  InputTransportBatch batch;
  batch.clock_origin_id = 0x484F5354434C4B31ULL;
  batch.reconciliation.host_timestamp_ns = timestamp;
  batch.reconciliation.focus = focus;
  return batch;
}

InputTransportBatch RichBatch() {
  InputTransportBatch batch = EmptyBatch();
  std::uint64_t id = 101U;
  std::uint64_t timestamp = 1000U;
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportFocusEvent{InputTransportFocusState::GAINED}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportKeyboardKeyEvent{Sdl2PhysicalScancode::A,
                                           InputTransportDigitalState::PRESSED,
                                           false}));
  batch.events.push_back(Event(
      id++, timestamp++,
      InputTransportKeyboardKeyEvent{
          Sdl2PhysicalScancode::A, InputTransportDigitalState::PRESSED, true}));
  batch.events.push_back(Event(
      id++, timestamp++, InputTransportMouseMotionEvent{-5, 720, -8, 12}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportMouseButtonEvent{
                Sdl2MouseButton::LEFT, InputTransportDigitalState::PRESSED}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportMouseWheelEvent{0.5F, -2.0F,
                                          Sdl2MouseWheelDirection::FLIPPED}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportGamepadConnectionEvent{
                100U, 7U, InputTransportDeviceConnectionState::CONNECTED}));
  batch.events.push_back(Event(
      id++, timestamp++,
      InputTransportGamepadButtonEvent{100U, 7U, Sdl2GamepadButton::A,
                                       InputTransportDigitalState::PRESSED}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportGamepadAxisEvent{
                100U, 7U, Sdl2GamepadAxis::LEFT_X,
                (std::numeric_limits<std::int16_t>::min)()}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportTextInputEvent{"Beam \xf0\x9f\x9a\x99"}));
  const InputTransportRawDeviceDescriptor raw = RawWheelDescriptor();
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportRawDeviceConnectionEvent{
                InputTransportDeviceConnectionState::CONNECTED, raw}));
  batch.events.push_back(Event(
      id++, timestamp++,
      InputTransportRawButtonEvent{raw.device_id, raw.connection_generation, 2U,
                                   InputTransportDigitalState::PRESSED}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportRawAxisEvent{raw.device_id, raw.connection_generation,
                                       0U, -12345}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportRawHatEvent{raw.device_id, raw.connection_generation,
                                      0U, Sdl2HatState::RIGHT_UP}));
  batch.events.push_back(
      Event(id++, timestamp++,
            InputTransportRawSliderEvent{
                raw.device_id, raw.connection_generation, 0U, 100, -200}));
  batch.events.push_back(
      Event(id++, timestamp++, InputTransportWindowCloseEvent{}));

  batch.reconciliation.through_event_id = 120U;
  batch.reconciliation.host_timestamp_ns = 2000U;
  batch.reconciliation.focus = InputTransportFocusState::GAINED;
  batch.reconciliation.window_close_requested = true;
  batch.reconciliation.pressed_scancodes = {Sdl2PhysicalScancode::A};
  batch.reconciliation.pressed_mouse_buttons = {Sdl2MouseButton::LEFT};
  InputTransportGamepadReconciliationState gamepad;
  gamepad.device_id = 100U;
  gamepad.connection_generation = 7U;
  gamepad.pressed_buttons = {Sdl2GamepadButton::A};
  gamepad.axes[0U] = (std::numeric_limits<std::int16_t>::min)();
  gamepad.axes[4U] = 12345;
  batch.reconciliation.gamepads.push_back(gamepad);
  batch.reconciliation.raw_devices.push_back(RichRawState());
  RequireStatus(ValidateInputTransportBatch(batch), RenderTransportStatus::OK,
                "rich fixture must be valid");
  return batch;
}

std::uint16_t ReadU16(const std::vector<std::uint8_t> &bytes,
                      std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint64_t ReadU64(const std::vector<std::uint8_t> &bytes,
                      std::size_t offset) {
  std::uint64_t value = 0U;
  for (std::size_t byte = 0U; byte < 8U; ++byte) {
    value |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8U);
  }
  return value;
}

void WriteU16(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint16_t value) {
  for (std::size_t byte = 0U; byte < 2U; ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
  }
}

void WriteU32(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint32_t value) {
  for (std::size_t byte = 0U; byte < 4U; ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
  }
}

void WriteU64(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint64_t value) {
  for (std::size_t byte = 0U; byte < 8U; ++byte) {
    bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
  }
}

void RefreshPayloadDigest(std::vector<std::uint8_t> &frame) {
  const auto digest = ComputeRenderTransportPayloadDigest(
      frame.data() + kRenderTransportEnvelopeHeaderBytes,
      frame.size() - kRenderTransportEnvelopeHeaderBytes);
  std::copy(digest.begin(), digest.end(), frame.begin() + 32U);
}

std::string ToHex(const std::vector<std::uint8_t> &bytes) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const std::uint8_t byte : bytes) {
    output << std::setw(2) << static_cast<unsigned>(byte);
  }
  return output.str();
}

std::size_t FindBytes(const std::vector<std::uint8_t> &bytes,
                      const std::vector<std::uint8_t> &needle) {
  const auto found =
      std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end());
  Require(found != bytes.end(), "expected byte pattern was absent");
  return static_cast<std::size_t>(found - bytes.begin());
}

void TestGoldenAndRichRoundTrip() {
  const InputTransportBatch batch = RichBatch();
  const auto first = EncodeInputEventTransportFrame(1U, batch);
  const auto second = EncodeInputEventTransportFrame(1U, batch);
  Require(first.ok() && second.ok() && first.bytes == second.bytes,
          "rich input encoding was not deterministic");
  Require(
      ReadU16(first.bytes, 8U) == kRenderTransportEnvelopeVersion &&
          ReadU16(first.bytes, 10U) == kRenderTransportEnvelopeHeaderBytes &&
          ReadU16(first.bytes, 12U) == 3U && ReadU16(first.bytes, 14U) == 0U &&
          ReadU64(first.bytes, 16U) == 1U,
      "input envelope header is not canonical kind 3");

  static const std::string kGoldenHex =
      "524f5253434e303101004000030000000100000000000000b70300000000000087042160"
      "06cb40a415c32b5b544350b2"
      "78e802c9a9f6d33e5af4d5bbb437c5520100000001000000010000000000000001000000"
      "314b4c4354534f4810000000"
      "6500000000000000e80300000000000009016600000000000000e9030000000000000104"
      "0001006700000000000000ea"
      "0300000000000001040001016800000000000000eb0300000000000002fbffffffd00200"
      "00f8ffffff0c000000690000"
      "0000000000ec030000000000000301016a00000000000000ed0300000000000004000000"
      "3f000000c0016b0000000000"
      "0000ee030000000000000564000000000000000700000000000000016c00000000000000"
      "ef0300000000000006640000"
      "0000000000070000000000000000016d00000000000000f0030000000000000764000000"
      "000000000700000000000000"
      "0000806e00000000000000f10300000000000008090000004265616d20f09f9a996f0000"
      "0000000000f2030000000000"
      "000b01c8000000000000000300000000000000020102030405060708090a0b0c0d0e0f10"
      "6d044fc20101451a3b5c8364"
      "468b0cde7af1031f728a7b403b0bc5faed1f513d43d46d7dde1b020000000000010080ff"
      "ffff7f00000000000000feff"
      "ff0002000001000218fcffffe803000000000000f6ffffff0a0000000800010100000000"
      "0000000118fcffffe8030000"
      "00000000fbffffff0500000001000118fcffffe803000000000000fbffffff0500000070"
      "00000000000000f303000000"
      "0000000cc80000000000000003000000000000000200017100000000000000f403000000"
      "0000000dc800000000000000"
      "03000000000000000000c7cfffff7200000000000000f5030000000000000ec800000000"
      "000000030000000000000000"
      "00037300000000000000f6030000000000000fc800000000000000030000000000000000"
      "006400000038ffffff740000"
      "0000000000f7030000000000000a7800000000000000d007000000000000010101000000"
      "040001000000010100000064"
      "000000000000000700000000000000010000000000800000000000003930000001000000"
      "c80000000000000003000000"
      "00000000020102030405060708090a0b0c0d0e0f106d044fc20101451a3b5c8364468b0c"
      "de7af1031f728a7b403b0bc5"
      "faed1f513d43d46d7dde1b020000000000010080ffffff7f00000000000000feffff0002"
      "000001000218fcffffe80300"
      "0000000000f6ffffff0a00000008000101000000000000000118fcffffe8030000000000"
      "00fbffffff05000000010001"
      "18fcffffe803000000000000fbffffff0500000001000000020002000000c7cfffff3200"
      "000001000000030100000064"
      "00000038ffffff";
  Require(ToHex(first.bytes) == kGoldenHex,
          "rich input frame no longer matches golden bytes");

  RenderTransportSequenceState forward_direction;
  Require(forward_direction.CommitAccepted(1U),
          "forward sequence fixture did not advance");
  InputEventTransportDecoder reverse_direction;
  const auto decoded = reverse_direction.Accept(first.bytes);
  Require(decoded.ok() && decoded.message->sequence() == 1U &&
              decoded.message->kind() ==
                  RenderTransportMessageKind::INPUT_EVENT_BATCH_V1 &&
              reverse_direction.next_expected_sequence() == 2U &&
              forward_direction.next_expected_sequence() == 2U,
          "reverse input sequence was not independent and accepted");
  const auto reencoded =
      EncodeInputEventTransportFrame(1U, *decoded.message->batch());
  Require(reencoded.ok() && reencoded.bytes == first.bytes,
          "input decode/re-encode changed exact bytes");
  const auto &roundtrip = *decoded.message->batch();
  Require(
      roundtrip.events.size() == batch.events.size() &&
          std::get<InputTransportGamepadAxisEvent>(roundtrip.events[8U].payload)
                  .value == (std::numeric_limits<std::int16_t>::min)() &&
          roundtrip.reconciliation.raw_devices[0U].axes[0U] == -12345 &&
          roundtrip.reconciliation.raw_devices[0U].sliders[0U].y == -200,
      "signed controller/raw numeric semantics did not round-trip");
}

void TestFramingHostileFieldsAndUtf8() {
  const InputTransportBatch batch = RichBatch();
  const auto encoded = EncodeInputEventTransportFrame(1U, batch);
  Require(encoded.ok(), "framing fixture did not encode");
  for (std::size_t size = 0U; size < encoded.bytes.size(); ++size) {
    const std::vector<std::uint8_t> prefix(encoded.bytes.begin(),
                                           encoded.bytes.begin() + size);
    const RenderTransportStatus expected =
        size < kRenderTransportEnvelopeHeaderBytes
            ? RenderTransportStatus::FRAME_TRUNCATED
            : RenderTransportStatus::FRAME_SIZE_MISMATCH;
    RequireStatus(InputEventTransportDecoder().Accept(prefix).status, expected,
                  "truncated input prefix was not rejected");
  }
  for (std::size_t offset = 32U; offset < encoded.bytes.size(); ++offset) {
    std::vector<std::uint8_t> corrupt = encoded.bytes;
    corrupt[offset] ^= 1U;
    RequireStatus(InputEventTransportDecoder().Accept(corrupt).status,
                  RenderTransportStatus::PAYLOAD_DIGEST_MISMATCH,
                  "input digest/payload corruption was accepted");
  }

  std::vector<std::uint8_t> frame = encoded.bytes;
  WriteU16(frame, 12U, 99U);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::UNKNOWN_MESSAGE_KIND,
                "unknown input envelope kind was accepted");
  frame = encoded.bytes;
  WriteU32(frame, kRenderTransportEnvelopeHeaderBytes, 2U);
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "unknown input payload version was accepted");
  frame = encoded.bytes;
  frame[kRenderTransportEnvelopeHeaderBytes + 16U] = 99U;
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "unknown clock domain was accepted");
  frame = encoded.bytes;
  WriteU32(frame, kRenderTransportEnvelopeHeaderBytes + 28U,
           (std::numeric_limits<std::uint32_t>::max)());
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::COUNT_LIMIT_EXCEEDED,
                "hostile event count reached allocation");
  frame = encoded.bytes;
  frame[kRenderTransportEnvelopeHeaderBytes + 48U] = 99U;
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "unknown input event kind was accepted");
  frame = encoded.bytes;
  WriteU16(frame, kRenderTransportEnvelopeHeaderBytes + 67U, 165U);
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "unassigned SDL2 scancode was accepted");

  const std::string utf8 = "Beam \xf0\x9f\x9a\x99";
  const std::vector<std::uint8_t> utf8_bytes(utf8.begin(), utf8.end());
  frame = encoded.bytes;
  const std::size_t utf8_offset = FindBytes(frame, utf8_bytes);
  frame[utf8_offset + 6U] = 0xffU;
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::INVALID_UTF8,
                "malformed UTF-8 text was accepted");
  frame = encoded.bytes;
  const std::size_t text_offset = FindBytes(frame, utf8_bytes);
  WriteU32(frame, text_offset - 4U,
           kInputEventTransportMaximumTextBytesPerEvent + 1U);
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::BLOB_LIMIT_EXCEEDED,
                "hostile text length reached allocation");

  frame = encoded.bytes;
  const auto raw = RawWheelDescriptor();
  const std::vector<std::uint8_t> guid(raw.guid.begin(), raw.guid.end());
  const std::size_t guid_offset = FindBytes(frame, guid);
  WriteU32(frame, guid_offset + 54U, kInputEventTransportMaximumRawAxes + 1U);
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::COUNT_LIMIT_EXCEEDED,
                "hostile raw-axis descriptor count reached allocation");

  InputTransportBatch empty = EmptyBatch();
  const auto empty_encoded = EncodeInputEventTransportFrame(1U, empty);
  Require(empty_encoded.ok(), "empty snapshot fixture did not encode");
  frame = empty_encoded.bytes;
  WriteU32(frame, kRenderTransportEnvelopeHeaderBytes + 50U,
           (std::numeric_limits<std::uint32_t>::max)());
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::COUNT_LIMIT_EXCEEDED,
                "hostile pressed-key count reached allocation");

  InputTransportBatch wheel = EmptyBatch();
  wheel.events.push_back(
      Event(1U, 100U,
            InputTransportMouseWheelEvent{1.0F, 2.0F,
                                          Sdl2MouseWheelDirection::NORMAL}));
  wheel.reconciliation.through_event_id = 1U;
  const auto wheel_encoded = EncodeInputEventTransportFrame(1U, wheel);
  Require(wheel_encoded.ok(), "wheel float fixture did not encode");
  frame = wheel_encoded.bytes;
  WriteU32(frame, kRenderTransportEnvelopeHeaderBytes + 49U, 0x7fc00000U);
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::NON_CANONICAL_FLOAT,
                "NaN wheel payload was accepted");

  frame = encoded.bytes;
  frame.push_back(0U);
  WriteU64(frame, 24U, ReadU64(frame, 24U) + 1U);
  RefreshPayloadDigest(frame);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::MALFORMED_PAYLOAD,
                "trailing input payload byte was accepted");
  frame = encoded.bytes;
  WriteU64(frame, 24U, kInputEventTransportMaximumPayloadBytes + 1U);
  RequireStatus(InputEventTransportDecoder().Accept(frame).status,
                RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED,
                "oversized input payload declaration was accepted");
}

void TestSemanticValidation() {
  InputTransportBatch batch = RichBatch();
  batch.events[1U].event_id = batch.events[0U].event_id;
  RequireStatus(ValidateInputTransportBatch(batch),
                RenderTransportStatus::EVENT_ID_ORDER_VIOLATION,
                "duplicate event IDs were accepted");
  batch = RichBatch();
  batch.events[1U].host_timestamp_ns = batch.events[0U].host_timestamp_ns - 1U;
  RequireStatus(ValidateInputTransportBatch(batch),
                RenderTransportStatus::TIMESTAMP_ORDER_VIOLATION,
                "reordered host timestamps were accepted");
  batch = RichBatch();
  batch.reconciliation.pressed_scancodes.clear();
  RequireStatus(ValidateInputTransportBatch(batch),
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "terminal key state disagreed with reconciliation");
  batch = RichBatch();
  batch.reconciliation.focus = InputTransportFocusState::LOST;
  RequireStatus(ValidateInputTransportBatch(batch),
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "focused-lost snapshot retained pressed input");
  batch = RichBatch();
  std::get<InputTransportGamepadAxisEvent>(batch.events[8U].payload).axis =
      Sdl2GamepadAxis::TRIGGER_LEFT;
  std::get<InputTransportGamepadAxisEvent>(batch.events[8U].payload).value = -1;
  RequireStatus(ValidateInputTransportBatch(batch),
                RenderTransportStatus::PAYLOAD_VALIDATION_FAILED,
                "negative standardized trigger value was accepted");
  batch = RichBatch();
  batch.reconciliation.gamepads[0U]
      .axes[static_cast<std::size_t>(Sdl2GamepadAxis::TRIGGER_RIGHT)] = -1;
  RequireStatus(ValidateInputTransportBatch(batch),
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "negative reconciled trigger value was accepted");
  batch = RichBatch();
  batch.reconciliation.raw_devices[0U].descriptor.axes[0U].logical_maximum =
      -40000;
  RequireStatus(ValidateInputTransportBatch(batch),
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "invalid raw axis range was accepted");
  batch = RichBatch();
  std::get<InputTransportMouseWheelEvent>(batch.events[5U].payload).delta_x =
      std::numeric_limits<float>::quiet_NaN();
  RequireStatus(EncodeInputEventTransportFrame(1U, batch).status,
                RenderTransportStatus::NON_CANONICAL_FLOAT,
                "encoder admitted NaN wheel input");

  batch = RichBatch();
  batch.events.push_back(Event(
      121U, 2001U, InputTransportFocusEvent{InputTransportFocusState::LOST}));
  batch.reconciliation.through_event_id = 121U;
  batch.reconciliation.host_timestamp_ns = 2001U;
  batch.reconciliation.focus = InputTransportFocusState::LOST;
  batch.reconciliation.pressed_scancodes.clear();
  batch.reconciliation.pressed_mouse_buttons.clear();
  batch.reconciliation.gamepads[0U].pressed_buttons.clear();
  batch.reconciliation.gamepads[0U].axes.fill(0);
  batch.reconciliation.raw_devices[0U].pressed_buttons.clear();
  batch.reconciliation.raw_devices[0U].axes = {0, 0};
  batch.reconciliation.raw_devices[0U].hats = {Sdl2HatState::CENTERED};
  batch.reconciliation.raw_devices[0U].sliders = {{0, 0}};
  RequireStatus(ValidateInputTransportBatch(batch), RenderTransportStatus::OK,
                "same-batch focus loss did not supersede pressed inputs");

  Require(IsValidInputTransportUtf8("ASCII") &&
              IsValidInputTransportUtf8("\xe2\x82\xac") &&
              IsValidInputTransportUtf8("\xf4\x8f\xbf\xbf") &&
              !IsValidInputTransportUtf8("\xc0\x80") &&
              !IsValidInputTransportUtf8("\xed\xa0\x80") &&
              !IsValidInputTransportUtf8("\xf4\x90\x80\x80") &&
              !IsValidInputTransportUtf8("\xe2\x82"),
          "UTF-8 scalar validation is incomplete");
}

InputTransportRawDeviceReconciliationState
NeutralRawState(const InputTransportRawDeviceDescriptor &descriptor) {
  InputTransportRawDeviceReconciliationState state;
  state.descriptor = descriptor;
  state.axes = {0, 0};
  state.hats = {Sdl2HatState::CENTERED};
  state.sliders = {{0, 0}};
  return state;
}

void TestDeviceGenerationLineage() {
  InputEventTransportDecoder decoder;
  const InputTransportRawDeviceDescriptor generation_one =
      RawWheelDescriptor(1U);

  InputTransportBatch disconnected = EmptyBatch();
  disconnected.events.push_back(Event(
      1U, 110U,
      InputTransportRawDeviceConnectionEvent{
          InputTransportDeviceConnectionState::DISCONNECTED, generation_one}));
  disconnected.reconciliation.through_event_id = 1U;
  disconnected.reconciliation.host_timestamp_ns = 110U;
  const auto disconnected_frame =
      EncodeInputEventTransportFrame(1U, disconnected);
  Require(disconnected_frame.ok() &&
              decoder.Accept(disconnected_frame.bytes).ok(),
          "initial raw disconnect generation was rejected");
  const auto published_disconnect = decoder.published();

  InputTransportBatch reused = EmptyBatch();
  reused.events.push_back(Event(
      2U, 120U,
      InputTransportRawDeviceConnectionEvent{
          InputTransportDeviceConnectionState::CONNECTED, generation_one}));
  reused.reconciliation.through_event_id = 2U;
  reused.reconciliation.host_timestamp_ns = 120U;
  reused.reconciliation.raw_devices = {NeutralRawState(generation_one)};
  const auto reused_frame = EncodeInputEventTransportFrame(2U, reused);
  Require(reused_frame.ok(), "same-generation reconnect was not wire-valid");
  RequireStatus(decoder.Accept(reused_frame.bytes).status,
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "disconnected raw generation was reused");
  Require(decoder.next_expected_sequence() == 2U &&
              decoder.published() == published_disconnect,
          "rejected generation reuse mutated receiver state");

  const InputTransportRawDeviceDescriptor generation_two =
      RawWheelDescriptor(2U);
  InputTransportBatch connected = EmptyBatch();
  connected.events.push_back(Event(
      2U, 120U,
      InputTransportRawDeviceConnectionEvent{
          InputTransportDeviceConnectionState::CONNECTED, generation_two}));
  connected.reconciliation.through_event_id = 2U;
  connected.reconciliation.host_timestamp_ns = 120U;
  connected.reconciliation.raw_devices = {NeutralRawState(generation_two)};
  const auto connected_frame = EncodeInputEventTransportFrame(2U, connected);
  Require(connected_frame.ok() && decoder.Accept(connected_frame.bytes).ok(),
          "incremented raw connection generation was rejected");

  InputTransportRawDeviceDescriptor mutated = generation_two;
  mutated.vendor_id ^= 1U;
  InputTransportBatch descriptor_change = EmptyBatch();
  descriptor_change.events.push_back(
      Event(10U, 130U,
            InputTransportRawDeviceConnectionEvent{
                InputTransportDeviceConnectionState::DISCONNECTED, mutated}));
  descriptor_change.reconciliation.through_event_id = 10U;
  descriptor_change.reconciliation.host_timestamp_ns = 130U;
  const auto descriptor_change_frame =
      EncodeInputEventTransportFrame(3U, descriptor_change);
  Require(descriptor_change_frame.ok(),
          "mutated descriptor gap fixture was not wire-valid");
  RequireStatus(decoder.Accept(descriptor_change_frame.bytes).status,
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "raw descriptor changed within one generation");

  InputTransportBatch correct_disconnect = EmptyBatch();
  correct_disconnect.events.push_back(Event(
      10U, 130U,
      InputTransportRawDeviceConnectionEvent{
          InputTransportDeviceConnectionState::DISCONNECTED, generation_two}));
  correct_disconnect.reconciliation.through_event_id = 10U;
  correct_disconnect.reconciliation.host_timestamp_ns = 130U;
  const auto correct_disconnect_frame =
      EncodeInputEventTransportFrame(3U, correct_disconnect);
  Require(correct_disconnect_frame.ok() &&
              decoder.Accept(correct_disconnect_frame.bytes).ok(),
          "matching raw disconnect descriptor was rejected");

  InputTransportBatch switched_family = EmptyBatch();
  switched_family.events.push_back(
      Event(11U, 140U,
            InputTransportGamepadConnectionEvent{
                generation_two.device_id, 3U,
                InputTransportDeviceConnectionState::CONNECTED}));
  switched_family.reconciliation.through_event_id = 11U;
  switched_family.reconciliation.host_timestamp_ns = 140U;
  InputTransportGamepadReconciliationState gamepad;
  gamepad.device_id = generation_two.device_id;
  gamepad.connection_generation = 3U;
  switched_family.reconciliation.gamepads = {gamepad};
  const auto switched_family_frame =
      EncodeInputEventTransportFrame(4U, switched_family);
  Require(switched_family_frame.ok(),
          "cross-family identity fixture was not wire-valid");
  RequireStatus(decoder.Accept(switched_family_frame.bytes).status,
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "stable device identity changed input family");
}

void TestTransactionalLineageAndGapHealing() {
  InputEventTransportDecoder decoder;
  InputTransportBatch initial = EmptyBatch();
  const auto initial_frame = EncodeInputEventTransportFrame(1U, initial);
  Require(initial_frame.ok() && decoder.Accept(initial_frame.bytes).ok(),
          "initial reconciliation was rejected");

  InputTransportBatch press = initial;
  press.events.push_back(
      Event(1U, 110U,
            InputTransportKeyboardKeyEvent{Sdl2PhysicalScancode::A,
                                           InputTransportDigitalState::PRESSED,
                                           false}));
  press.reconciliation.through_event_id = 1U;
  press.reconciliation.host_timestamp_ns = 110U;
  press.reconciliation.pressed_scancodes = {Sdl2PhysicalScancode::A};
  const auto press_frame = EncodeInputEventTransportFrame(2U, press);
  Require(press_frame.ok() && decoder.Accept(press_frame.bytes).ok() &&
              decoder.last_event_id() == 1U,
          "complete key transition was rejected");
  const auto published_press = decoder.published();

  InputTransportBatch silent_change = press;
  silent_change.events.clear();
  silent_change.reconciliation.host_timestamp_ns = 120U;
  silent_change.reconciliation.pressed_scancodes.clear();
  const auto silent_frame = EncodeInputEventTransportFrame(3U, silent_change);
  Require(silent_frame.ok(), "silent state change was not wire-valid");
  RequireStatus(decoder.Accept(silent_frame.bytes).status,
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "complete lineage allowed an unexplained state change");
  Require(decoder.next_expected_sequence() == 3U &&
              decoder.last_event_id() == 1U &&
              decoder.published() == published_press,
          "rejected state change mutated receiver publication");

  InputTransportBatch duplicate = press;
  duplicate.events[0U].host_timestamp_ns = 120U;
  duplicate.reconciliation.host_timestamp_ns = 120U;
  const auto duplicate_frame = EncodeInputEventTransportFrame(3U, duplicate);
  Require(duplicate_frame.ok(), "duplicate cross-frame fixture did not encode");
  RequireStatus(decoder.Accept(duplicate_frame.bytes).status,
                RenderTransportStatus::EVENT_ID_ORDER_VIOLATION,
                "duplicate cross-frame event ID was accepted");

  InputTransportBatch gap = silent_change;
  gap.reconciliation.through_event_id = 10U;
  gap.reconciliation.host_timestamp_ns = 130U;
  const auto gap_frame = EncodeInputEventTransportFrame(3U, gap);
  Require(gap_frame.ok() && decoder.Accept(gap_frame.bytes).ok() &&
              decoder.last_event_id() == 10U &&
              decoder.reconciliation()->pressed_scancodes.empty(),
          "authoritative gap reconciliation did not heal state");

  InputTransportBatch delayed = EmptyBatch();
  delayed.events.push_back(
      Event(5U, 135U,
            InputTransportKeyboardKeyEvent{Sdl2PhysicalScancode::B,
                                           InputTransportDigitalState::PRESSED,
                                           false}));
  delayed.reconciliation.through_event_id = 5U;
  delayed.reconciliation.host_timestamp_ns = 135U;
  delayed.reconciliation.pressed_scancodes = {Sdl2PhysicalScancode::B};
  const auto delayed_frame = EncodeInputEventTransportFrame(4U, delayed);
  Require(delayed_frame.ok(), "delayed event fixture did not encode");
  RequireStatus(decoder.Accept(delayed_frame.bytes).status,
                RenderTransportStatus::EVENT_ID_ORDER_VIOLATION,
                "delayed event below reconciliation watermark was accepted");

  InputTransportBatch focus_lost = gap;
  focus_lost.events.push_back(Event(
      11U, 140U, InputTransportFocusEvent{InputTransportFocusState::LOST}));
  focus_lost.reconciliation.through_event_id = 11U;
  focus_lost.reconciliation.host_timestamp_ns = 140U;
  focus_lost.reconciliation.focus = InputTransportFocusState::LOST;
  const auto focus_frame = EncodeInputEventTransportFrame(4U, focus_lost);
  Require(focus_frame.ok() && decoder.Accept(focus_frame.bytes).ok(),
          "complete focus-loss neutralization was rejected");

  InputTransportBatch wrong_clock = focus_lost;
  wrong_clock.events.clear();
  wrong_clock.clock_origin_id += 1U;
  wrong_clock.reconciliation.host_timestamp_ns = 150U;
  const auto clock_frame = EncodeInputEventTransportFrame(5U, wrong_clock);
  Require(clock_frame.ok(), "clock mismatch fixture did not encode");
  RequireStatus(decoder.Accept(clock_frame.bytes).status,
                RenderTransportStatus::CLOCK_DOMAIN_MISMATCH,
                "clock origin changed inside one connection");
  RequireStatus(decoder.Accept(focus_frame.bytes).status,
                RenderTransportStatus::REPLAYED_SEQUENCE,
                "reverse envelope replay was accepted");
}

void TestCompleteRawDeviceTransitions() {
  InputEventTransportDecoder decoder;
  InputTransportBatch initial = EmptyBatch();
  Require(
      decoder.Accept(EncodeInputEventTransportFrame(1U, initial).bytes).ok(),
      "raw transition initial state failed");

  const InputTransportRawDeviceDescriptor descriptor = RawWheelDescriptor(1U);
  InputTransportBatch connected = initial;
  connected.events.push_back(
      Event(1U, 110U,
            InputTransportRawDeviceConnectionEvent{
                InputTransportDeviceConnectionState::CONNECTED, descriptor}));
  connected.events.push_back(Event(
      2U, 111U,
      InputTransportRawAxisEvent{descriptor.device_id,
                                 descriptor.connection_generation, 0U, 1000}));
  connected.reconciliation.through_event_id = 2U;
  connected.reconciliation.host_timestamp_ns = 111U;
  auto state = RichRawState();
  state.descriptor = descriptor;
  state.pressed_buttons.clear();
  state.axes = {1000, 0};
  state.hats = {Sdl2HatState::CENTERED};
  state.sliders = {{0, 0}};
  connected.reconciliation.raw_devices = {state};
  const auto connected_frame = EncodeInputEventTransportFrame(2U, connected);
  Require(connected_frame.ok() && decoder.Accept(connected_frame.bytes).ok(),
          "complete raw connect/axis transition failed");

  InputTransportBatch unexplained = connected;
  unexplained.events.clear();
  unexplained.reconciliation.host_timestamp_ns = 120U;
  unexplained.reconciliation.raw_devices[0U].axes[0U] = 2000;
  const auto unexplained_frame =
      EncodeInputEventTransportFrame(3U, unexplained);
  Require(unexplained_frame.ok(), "unexplained raw state was not wire-valid");
  RequireStatus(decoder.Accept(unexplained_frame.bytes).status,
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "complete lineage allowed unexplained raw axis change");

  InputTransportBatch lost = connected;
  lost.events.clear();
  lost.events.push_back(Event(
      3U, 130U, InputTransportFocusEvent{InputTransportFocusState::LOST}));
  lost.reconciliation.through_event_id = 3U;
  lost.reconciliation.host_timestamp_ns = 130U;
  lost.reconciliation.focus = InputTransportFocusState::LOST;
  lost.reconciliation.raw_devices[0U].axes = {0, 0};
  const auto lost_frame = EncodeInputEventTransportFrame(3U, lost);
  Require(lost_frame.ok() && decoder.Accept(lost_frame.bytes).ok(),
          "focus loss did not neutralize raw controls");
}

void TestProtocolCaps() {
  InputTransportBatch too_many_events = EmptyBatch();
  too_many_events.events.resize(kInputEventTransportMaximumEvents + 1U);
  RequireStatus(ValidateInputTransportBatch(too_many_events),
                RenderTransportStatus::COUNT_LIMIT_EXCEEDED,
                "event cap was not enforced before event validation");

  InputTransportBatch oversized_text = EmptyBatch();
  oversized_text.events.push_back(
      Event(1U, 100U,
            InputTransportTextInputEvent{std::string(
                kInputEventTransportMaximumTextBytesPerEvent + 1U, 'x')}));
  oversized_text.reconciliation.through_event_id = 1U;
  RequireStatus(ValidateInputTransportBatch(oversized_text),
                RenderTransportStatus::BLOB_LIMIT_EXCEEDED,
                "per-event text cap was not enforced");

  InputTransportBatch too_much_text = EmptyBatch();
  const std::string text(kInputEventTransportMaximumTextBytesPerEvent, 'x');
  const std::uint32_t text_events =
      kInputEventTransportMaximumTotalTextBytes /
          kInputEventTransportMaximumTextBytesPerEvent +
      1U;
  for (std::uint32_t index = 1U; index <= text_events; ++index) {
    too_much_text.events.push_back(
        Event(index, 100U + index, InputTransportTextInputEvent{text}));
  }
  too_much_text.reconciliation.through_event_id = text_events;
  too_much_text.reconciliation.host_timestamp_ns = 100U + text_events;
  RequireStatus(ValidateInputTransportBatch(too_much_text),
                RenderTransportStatus::BLOB_LIMIT_EXCEEDED,
                "aggregate text cap was not enforced");

  InputTransportBatch too_many_connected = EmptyBatch();
  for (std::uint64_t id = 1U; id <= kInputEventTransportMaximumGamepads; ++id) {
    InputTransportGamepadReconciliationState gamepad;
    gamepad.device_id = id;
    gamepad.connection_generation = 1U;
    too_many_connected.reconciliation.gamepads.push_back(gamepad);
  }
  auto raw = NeutralRawState(RawWheelDescriptor());
  too_many_connected.reconciliation.raw_devices.push_back(std::move(raw));
  RequireStatus(ValidateInputTransportBatch(too_many_connected),
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "combined connected-device cap was not enforced");

  InputTransportBatch too_many_generations = EmptyBatch();
  for (std::uint64_t generation = 1U;
       generation <= kInputEventTransportMaximumDeviceGenerationsPerBatch + 1U;
       ++generation) {
    too_many_generations.events.push_back(
        Event(generation, 100U + generation,
              InputTransportRawDeviceConnectionEvent{
                  InputTransportDeviceConnectionState::DISCONNECTED,
                  RawWheelDescriptor(generation)}));
  }
  too_many_generations.reconciliation.through_event_id =
      kInputEventTransportMaximumDeviceGenerationsPerBatch + 1U;
  too_many_generations.reconciliation.host_timestamp_ns =
      101U + kInputEventTransportMaximumDeviceGenerationsPerBatch;
  RequireStatus(ValidateInputTransportBatch(too_many_generations),
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "per-batch device-generation cap was not enforced");

  InputEventTransportDecoder decoder;
  for (std::uint64_t index = 1U;
       index <= kInputEventTransportMaximumTrackedDeviceIdentities; ++index) {
    const std::uint64_t event_id = index * 2U;
    InputTransportBatch batch =
        EmptyBatch(InputTransportFocusState::GAINED, 100U + event_id);
    batch.events.push_back(
        Event(event_id, 100U + event_id,
              InputTransportGamepadConnectionEvent{
                  1000U + index, 1U,
                  InputTransportDeviceConnectionState::DISCONNECTED}));
    batch.reconciliation.through_event_id = event_id;
    const auto frame = EncodeInputEventTransportFrame(index, batch);
    Require(frame.ok() && decoder.Accept(frame.bytes).ok(),
            "tracked-device identity fixture failed before its cap");
  }
  const std::uint64_t rejected_index =
      kInputEventTransportMaximumTrackedDeviceIdentities + 1U;
  const std::uint64_t rejected_event_id = rejected_index * 2U;
  InputTransportBatch rejected =
      EmptyBatch(InputTransportFocusState::GAINED, 100U + rejected_event_id);
  rejected.events.push_back(
      Event(rejected_event_id, 100U + rejected_event_id,
            InputTransportGamepadConnectionEvent{
                1000U + rejected_index, 1U,
                InputTransportDeviceConnectionState::DISCONNECTED}));
  rejected.reconciliation.through_event_id = rejected_event_id;
  const auto rejected_frame =
      EncodeInputEventTransportFrame(rejected_index, rejected);
  Require(rejected_frame.ok(), "identity-cap rejection did not encode");
  RequireStatus(decoder.Accept(rejected_frame.bytes).status,
                RenderTransportStatus::RECONCILIATION_MISMATCH,
                "tracked-device identity cap was not enforced");
  Require(decoder.next_expected_sequence() == rejected_index,
          "identity-cap rejection advanced the envelope sequence");
}

} // namespace

int main() {
  TestGoldenAndRichRoundTrip();
  TestFramingHostileFieldsAndUtf8();
  TestSemanticValidation();
  TestDeviceGenerationLineage();
  TestTransactionalLineageAndGapHealing();
  TestCompleteRawDeviceTransitions();
  TestProtocolCaps();
  return EXIT_SUCCESS;
}
