/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderTransportStream.h"

#include "InputEventTransport.h"
#include "RenderAssetDeltaTransport.h"
#include "SceneSnapshotTransport.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "render transport stream test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::vector<std::uint8_t> MakeFrame(
    RenderTransportMessageKind kind, std::uint64_t sequence,
    std::size_t payload_size) {
  std::vector<std::uint8_t> payload(payload_size);
  for (std::size_t index = 0U; index < payload.size(); ++index) {
    payload[index] = static_cast<std::uint8_t>(
        (index * 131U + static_cast<std::size_t>(sequence)) & 0xffU);
  }
  const RenderTransportEnvelopeEncodeResult encoded =
      EncodeRenderTransportEnvelope(kind, sequence, payload,
                                    payload.size());
  Require(encoded.ok(), "test envelope did not encode");
  return encoded.bytes;
}

void WriteU16(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void WriteU64(std::vector<std::uint8_t> &bytes, std::size_t offset,
              std::uint64_t value) {
  for (std::size_t index = 0U; index < 8U; ++index) {
    bytes[offset + index] =
        static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void TestStatusAndConfigurationContract() {
  Require(kRenderTransportStreamSceneMaximumPayloadBytes ==
                  kSceneSnapshotTransportMaximumPayloadBytes &&
              kRenderTransportStreamInputMaximumPayloadBytes ==
                  kInputEventTransportMaximumPayloadBytes &&
              kRenderTransportStreamAssetMaximumPayloadBytes ==
                  kRenderAssetDeltaTransportMaximumPayloadBytes,
          "stream and typed payload caps diverged");
  const unsigned int maximum = std::numeric_limits<std::uint8_t>::max();
  for (unsigned int value = 0U; value <= maximum; ++value) {
    const auto status = static_cast<RenderTransportStreamStatus>(value);
    Require(IsKnownRenderTransportStreamStatus(status) == (value <= 12U),
            "status classifier accepted an unknown value");
  }
  Require(std::strcmp(ToString(RenderTransportStreamStatus::FRAME_READY),
                      "frame-ready") == 0 &&
              std::strcmp(ToString(
                              RenderTransportStreamStatus::
                                  TRUNCATED_END_OF_STREAM),
                          "truncated-end-of-stream") == 0 &&
              std::strcmp(ToString(static_cast<RenderTransportStreamStatus>(
                              255U)),
                          "invalid") == 0,
          "stream status strings changed");

  RenderTransportStreamDecoder invalid(
      kRenderTransportStreamAbsoluteMaximumPayloadBytes + 1U);
  Require(invalid.terminal() &&
              invalid.status() == RenderTransportStreamStatus::
                                      REJECTED_INVALID_CONFIGURATION &&
              invalid.transport_status() ==
                  RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED,
          "oversized configuration did not fail closed");
  const auto rejected = invalid.Accept(nullptr, 0U);
  Require(rejected.version == kRenderTransportStreamContractVersion &&
              rejected.terminal && rejected.bytes_consumed == 0U &&
              rejected.status == invalid.status(),
          "terminal configuration accepted input");
}

void TestEveryTwoChunkBoundaryAndOwnership() {
  const std::vector<std::uint8_t> frame = MakeFrame(
      RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2, 9U, 257U);
  for (std::size_t split = 0U; split <= frame.size(); ++split) {
    RenderTransportStreamDecoder decoder(1024U);
    const auto first = decoder.Accept(frame.data(), split);
    Require(first.bytes_consumed == split && !first.terminal,
            "first chunk consumption changed");
    Require(first.status ==
                (split == frame.size()
                     ? RenderTransportStreamStatus::FRAME_READY
                     : RenderTransportStreamStatus::NEED_MORE_DATA),
            "first chunk state changed");
    if (split < frame.size()) {
      const auto second =
          decoder.Accept(frame.data() + split, frame.size() - split);
      Require(second.status == RenderTransportStreamStatus::FRAME_READY &&
                  second.bytes_consumed == frame.size() - split,
              "second chunk did not complete exactly one frame");
    }
    Require(decoder.frame_ready() && !decoder.terminal(),
            "complete split frame was not pending");
    const RenderTransportStreamFrameResult taken = decoder.TakeFrame();
    Require(taken.ok() && taken.kind ==
                              RenderTransportMessageKind::
                                  SCENE_SNAPSHOT_V4_CAMERA_V2 &&
                taken.sequence == 9U && taken.bytes == frame &&
                decoder.buffered_bytes() == 0U &&
                decoder.expected_frame_bytes() == 0U &&
                decoder.status() ==
                    RenderTransportStreamStatus::NEED_MORE_DATA,
            "frame ownership or decoder reset changed");
  }

  RenderTransportStreamDecoder bytewise(1024U);
  for (std::size_t index = 0U; index < frame.size(); ++index) {
    const auto accepted = bytewise.Accept(frame.data() + index, 1U);
    Require(accepted.bytes_consumed == 1U &&
                accepted.status ==
                    (index + 1U == frame.size()
                         ? RenderTransportStreamStatus::FRAME_READY
                         : RenderTransportStreamStatus::NEED_MORE_DATA),
            "single-byte fragmentation changed");
  }
  Require(bytewise.TakeFrame().bytes == frame,
          "single-byte frame contents changed");
}

void TestCoalescingAndPendingBackpressure() {
  const std::vector<std::uint8_t> first = MakeFrame(
      RenderTransportMessageKind::RENDER_ASSET_DELTA_V1, 1U, 3U);
  const std::vector<std::uint8_t> second = MakeFrame(
      RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2, 2U, 7U);
  std::vector<std::uint8_t> joined = first;
  joined.insert(joined.end(), second.begin(), second.end());

  RenderTransportStreamDecoder decoder(1024U);
  const auto accepted = decoder.Accept(joined.data(), joined.size());
  Require(accepted.status == RenderTransportStreamStatus::FRAME_READY &&
              accepted.bytes_consumed == first.size(),
          "coalesced input consumed bytes from the following frame");
  const auto pending = decoder.Accept(joined.data() + first.size(),
                                      second.size());
  Require(pending.status ==
                  RenderTransportStreamStatus::REJECTED_FRAME_PENDING &&
              pending.bytes_consumed == 0U && !pending.terminal,
          "pending frame did not apply backpressure");
  const auto first_taken = decoder.TakeFrame();
  Require(first_taken.ok() && first_taken.sequence == 1U &&
              first_taken.kind ==
                  RenderTransportMessageKind::RENDER_ASSET_DELTA_V1,
          "first coalesced frame metadata changed");
  const auto accepted_second = decoder.Accept(
      joined.data() + accepted.bytes_consumed,
      joined.size() - accepted.bytes_consumed);
  Require(accepted_second.status == RenderTransportStreamStatus::FRAME_READY &&
              accepted_second.bytes_consumed == second.size(),
          "following coalesced frame did not complete");
  const auto second_taken = decoder.TakeFrame();
  Require(second_taken.ok() && second_taken.sequence == 2U &&
              second_taken.bytes == second,
          "second coalesced frame changed");
}

void TestHeaderAndEnvelopeFailuresAreTerminal() {
  struct HeaderCase {
    std::size_t offset;
    std::uint64_t value;
    unsigned int width;
    RenderTransportStatus expected;
  };
  const HeaderCase cases[] = {
      {8U, 2U, 2U, RenderTransportStatus::UNSUPPORTED_TRANSPORT_VERSION},
      {10U, 63U, 2U, RenderTransportStatus::INVALID_HEADER},
      {12U, 65535U, 2U, RenderTransportStatus::UNKNOWN_MESSAGE_KIND},
      {14U, 1U, 2U, RenderTransportStatus::INVALID_HEADER},
      {16U, 0U, 8U, RenderTransportStatus::INVALID_SEQUENCE},
      {24U, 1025U, 8U, RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED},
  };
  for (const HeaderCase &test_case : cases) {
    std::vector<std::uint8_t> frame = MakeFrame(
        RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2, 1U, 8U);
    if (test_case.width == 2U) {
      WriteU16(frame, test_case.offset,
               static_cast<std::uint16_t>(test_case.value));
    } else {
      WriteU64(frame, test_case.offset, test_case.value);
    }
    RenderTransportStreamDecoder decoder(1024U);
    const auto result = decoder.Accept(frame.data(), frame.size());
    Require(result.status == RenderTransportStreamStatus::FAILED_HEADER &&
                result.transport_status == test_case.expected &&
                result.terminal &&
                result.bytes_consumed ==
                    kRenderTransportEnvelopeHeaderBytes,
            "invalid header did not stop before payload consumption");
    const auto retry = decoder.Accept(frame.data(), frame.size());
    Require(retry.status == result.status && retry.terminal &&
                retry.bytes_consumed == 0U,
            "terminal header failure attempted resynchronization");
  }

  std::vector<std::uint8_t> bad_magic = MakeFrame(
      RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2, 1U, 8U);
  bad_magic[0U] ^= 0xffU;
  RenderTransportStreamDecoder magic_decoder(1024U);
  const auto magic = magic_decoder.Accept(bad_magic.data(), bad_magic.size());
  Require(magic.status == RenderTransportStreamStatus::FAILED_HEADER &&
              magic.transport_status == RenderTransportStatus::INVALID_MAGIC,
          "invalid magic did not fail in the header");

  std::vector<std::uint8_t> oversized_scene = MakeFrame(
      RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2, 1U, 0U);
  WriteU64(oversized_scene, 24U,
           kRenderTransportStreamSceneMaximumPayloadBytes + 1U);
  RenderTransportStreamDecoder kind_limit_decoder(
      kRenderTransportStreamAbsoluteMaximumPayloadBytes);
  const auto kind_limit = kind_limit_decoder.Accept(
      oversized_scene.data(), oversized_scene.size());
  Require(kind_limit.status == RenderTransportStreamStatus::FAILED_HEADER &&
              kind_limit.transport_status ==
                  RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED &&
              kind_limit.bytes_consumed ==
                  kRenderTransportEnvelopeHeaderBytes,
          "scene kind exceeded its lower typed payload cap");

  std::vector<std::uint8_t> oversized_input = MakeFrame(
      RenderTransportMessageKind::INPUT_EVENT_BATCH_V1, 1U, 0U);
  WriteU64(oversized_input, 24U,
           kRenderTransportStreamInputMaximumPayloadBytes + 1U);
  RenderTransportStreamDecoder input_limit_decoder(
      kRenderTransportStreamAbsoluteMaximumPayloadBytes);
  const auto input_limit = input_limit_decoder.Accept(
      oversized_input.data(), oversized_input.size());
  Require(input_limit.status == RenderTransportStreamStatus::FAILED_HEADER &&
              input_limit.transport_status ==
                  RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED &&
              input_limit.bytes_consumed ==
                  kRenderTransportEnvelopeHeaderBytes,
          "input kind exceeded its lower typed payload cap");

  std::vector<std::uint8_t> corrupted = MakeFrame(
      RenderTransportMessageKind::RENDER_ASSET_DELTA_V1, 5U, 8U);
  corrupted.back() ^= 0x01U;
  RenderTransportStreamDecoder digest_decoder(1024U);
  const auto digest =
      digest_decoder.Accept(corrupted.data(), corrupted.size());
  Require(digest.status == RenderTransportStreamStatus::FAILED_ENVELOPE &&
              digest.transport_status ==
                  RenderTransportStatus::PAYLOAD_DIGEST_MISMATCH &&
              digest.terminal && digest.bytes_consumed == corrupted.size(),
          "payload corruption was published");
  Require(!digest_decoder.TakeFrame(),
          "terminal envelope failure exposed a frame");
}

void TestCloseAndCallerErrors() {
  const std::vector<std::uint8_t> frame = MakeFrame(
      RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2, 7U, 32U);
  RenderTransportStreamDecoder decoder(1024U);
  const auto null_input = decoder.Accept(nullptr, 1U);
  Require(null_input.status ==
                  RenderTransportStreamStatus::REJECTED_INVALID_ARGUMENT &&
              !null_input.terminal && decoder.buffered_bytes() == 0U,
          "caller pointer error poisoned an untouched stream");
  const auto no_op = decoder.Accept(nullptr, 0U);
  Require(no_op.status == RenderTransportStreamStatus::NEED_MORE_DATA &&
              no_op.bytes_consumed == 0U,
          "zero-length input changed state");
  Require(!decoder.TakeFrame(), "empty stream exposed a frame");
  const auto closed = decoder.Finish();
  Require(closed.status == RenderTransportStreamStatus::CLOSED &&
              !closed.terminal,
          "empty EOF did not close cleanly");
  Require(decoder.Accept(frame.data(), frame.size()).status ==
              RenderTransportStreamStatus::REJECTED_CLOSED,
          "closed stream accepted bytes");

  for (std::size_t prefix : {std::size_t{1U},
                             kRenderTransportEnvelopeHeaderBytes,
                             frame.size() - 1U}) {
    RenderTransportStreamDecoder partial(1024U);
    Require(partial.Accept(frame.data(), prefix).status ==
                RenderTransportStreamStatus::NEED_MORE_DATA,
            "partial stream unexpectedly completed");
    const auto truncated = partial.Finish();
    Require(truncated.status ==
                    RenderTransportStreamStatus::TRUNCATED_END_OF_STREAM &&
                truncated.transport_status ==
                    RenderTransportStatus::FRAME_TRUNCATED &&
                truncated.terminal,
            "partial EOF did not fail closed");
  }

  RenderTransportStreamDecoder pending(1024U);
  Require(pending.Accept(frame.data(), frame.size()).status ==
              RenderTransportStreamStatus::FRAME_READY,
          "complete frame did not become pending");
  Require(pending.Finish().status == RenderTransportStreamStatus::FRAME_READY,
          "EOF discarded a complete pending frame");
  const auto taken = pending.TakeFrame();
  Require(taken.ok() && taken.bytes == frame &&
              pending.status() == RenderTransportStreamStatus::CLOSED,
          "taking the last frame did not close the stream");
}

void TestZeroPayloadEnvelope() {
  const std::vector<std::uint8_t> frame = MakeFrame(
      RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2, 3U, 0U);
  RenderTransportStreamDecoder decoder(0U);
  const auto accepted = decoder.Accept(frame.data(), frame.size());
  Require(accepted.status == RenderTransportStreamStatus::FRAME_READY &&
              accepted.bytes_consumed == frame.size() &&
              decoder.expected_frame_bytes() == frame.size(),
          "zero-payload envelope did not complete at the header boundary");
  Require(decoder.TakeFrame().bytes == frame,
          "zero-payload envelope changed");
}

} // namespace

int main() {
  TestStatusAndConfigurationContract();
  TestEveryTwoChunkBoundaryAndOwnership();
  TestCoalescingAndPendingBackpressure();
  TestHeaderAndEnvelopeFailuresAreTerminal();
  TestCloseAndCallerErrors();
  TestZeroPayloadEnvelope();
  return EXIT_SUCCESS;
}
