/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "InputEventTransport.h"
#include "RenderBridgeControlTransport.h"
#include "RenderTransportStream.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "render bridge control transport test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

InputTransportBatch EmptyInputBatch() {
  InputTransportBatch batch;
  batch.clock_origin_id = 0x4354524c494e5055ULL;
  batch.reconciliation.host_timestamp_ns = 100U;
  batch.reconciliation.focus = InputTransportFocusState::LOST;
  Require(ValidateInputTransportBatch(batch) == RenderTransportStatus::OK,
          "empty input fixture is invalid");
  return batch;
}

void TestStatusAndWireContract() {
  Require(kRenderBridgeControlTransportPayloadVersion == 1U &&
              kRenderBridgeControlTransportMaximumPayloadBytes == 128U &&
              kRenderTransportStreamControlMaximumPayloadBytes ==
                  kRenderBridgeControlTransportMaximumPayloadBytes,
          "control transport version or bound changed");
  for (unsigned int value = 0U; value <= 255U; ++value) {
    const auto kind = static_cast<RenderBridgeControlKind>(value);
    Require(IsKnownRenderBridgeControlKind(kind) ==
                (value >= 1U && value <= 3U),
            "control kind classifier changed");
  }
  Require(IsKnownRenderTransportMessageKind(
              RenderTransportMessageKind::
                  RENDER_BRIDGE_ACKNOWLEDGEMENT_V1) &&
              IsKnownRenderTransportMessageKind(
                  RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1),
          "reverse control envelope kinds are not admitted");

  RenderBridgeAcknowledgement acknowledgement;
  acknowledgement.registry_id = 44U;
  acknowledgement.through_forward_sequence = 9U;
  acknowledgement.presented_scene_sequence = 8U;
  acknowledgement.presented_snapshot_id = 71U;
  const auto ack_frame =
      EncodeRenderBridgeAcknowledgementFrame(2U, acknowledgement);
  Require(ack_frame.ok() &&
              ack_frame.bytes.size() ==
                  kRenderTransportEnvelopeHeaderBytes + 40U,
          "acknowledgement did not use its exact bounded payload");

  RenderBridgeControl control;
  control.kind = RenderBridgeControlKind::HEARTBEAT;
  control.registry_id = 44U;
  control.command_id = 1U;
  const auto control_frame = EncodeRenderBridgeControlFrame(3U, control);
  Require(control_frame.ok() &&
              control_frame.bytes.size() ==
                  kRenderTransportEnvelopeHeaderBytes + 32U,
          "control did not use its exact bounded payload");

  RenderTransportStreamDecoder stream(
      kRenderBridgeControlTransportMaximumPayloadBytes);
  Require(stream.Accept(ack_frame.bytes.data(), ack_frame.bytes.size()).status ==
              RenderTransportStreamStatus::FRAME_READY &&
              stream.TakeFrame().kind ==
                  RenderTransportMessageKind::
                      RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 &&
              stream.Accept(control_frame.bytes.data(),
                            control_frame.bytes.size()).status ==
                  RenderTransportStreamStatus::FRAME_READY &&
              stream.TakeFrame().kind ==
                  RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1,
          "stream decoder did not frame both reverse control kinds");
}

void TestInterleavedReverseSequenceAndRegistryIdentity() {
  constexpr std::uint64_t registry_id = 0x123456789abcdef0ULL;
  RenderTransportSequenceState shared_sequence;
  InputEventTransportDecoder input_decoder(shared_sequence);
  RenderBridgeControlTransportDecoder control_decoder(registry_id,
                                                        shared_sequence);

  const auto input = EncodeInputEventTransportFrame(1U, EmptyInputBatch());
  Require(input.ok() && input_decoder.Accept(input.bytes).ok() &&
              shared_sequence.next_expected_sequence() == 2U,
          "input did not advance the shared reverse sequence");

  RenderBridgeAcknowledgement acknowledgement;
  acknowledgement.registry_id = registry_id;
  acknowledgement.through_forward_sequence = 2U;
  acknowledgement.presented_scene_sequence = 2U;
  acknowledgement.presented_snapshot_id = 99U;
  const auto ack = EncodeRenderBridgeAcknowledgementFrame(2U,
                                                           acknowledgement);
  const auto decoded_ack = control_decoder.Accept(ack.bytes);
  Require(decoded_ack.ok() &&
              decoded_ack.kind == RenderTransportMessageKind::
                                      RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 &&
              decoded_ack.acknowledgement.presented_snapshot_id == 99U &&
              shared_sequence.next_expected_sequence() == 3U,
          "acknowledgement did not interleave after input");

  RenderBridgeControl control;
  control.kind = RenderBridgeControlKind::PEER_READY;
  control.registry_id = registry_id;
  control.command_id = 1U;
  const auto encoded_control = EncodeRenderBridgeControlFrame(3U, control);
  const auto decoded_control = control_decoder.Accept(encoded_control.bytes);
  Require(decoded_control.ok() &&
              decoded_control.control.kind ==
                  RenderBridgeControlKind::PEER_READY &&
              decoded_control.control.command_id == 1U &&
              shared_sequence.next_expected_sequence() == 4U,
          "control did not interleave after acknowledgement");

  control.registry_id = registry_id + 1U;
  control.command_id = 2U;
  const auto foreign = EncodeRenderBridgeControlFrame(4U, control);
  Require(control_decoder.Accept(foreign.bytes).status ==
              RenderTransportStatus::REGISTRY_VALIDATION_FAILED &&
              shared_sequence.next_expected_sequence() == 4U,
          "foreign registry advanced reverse lineage");
  control.registry_id = registry_id;
  const auto recovered = EncodeRenderBridgeControlFrame(4U, control);
  Require(control_decoder.Accept(recovered.bytes).ok() &&
              shared_sequence.next_expected_sequence() == 5U,
          "valid frame could not recover after transactional rejection");
}

void TestMalformedAndLineageFailures() {
  RenderBridgeAcknowledgement acknowledgement;
  acknowledgement.registry_id = 7U;
  acknowledgement.through_forward_sequence = 1U;
  acknowledgement.presented_scene_sequence = 2U;
  acknowledgement.presented_snapshot_id = 3U;
  Require(EncodeRenderBridgeAcknowledgementFrame(1U, acknowledgement).status ==
              RenderTransportStatus::RECONCILIATION_MISMATCH,
          "acknowledgement presented beyond its cumulative watermark");

  RenderBridgeControl control;
  control.registry_id = 7U;
  control.command_id = 1U;
  control.kind = static_cast<RenderBridgeControlKind>(255U);
  Require(EncodeRenderBridgeControlFrame(1U, control).status ==
              RenderTransportStatus::INVALID_ARGUMENT,
          "unknown control kind encoded");

  control.kind = RenderBridgeControlKind::HEARTBEAT;
  std::vector<std::uint8_t> frame =
      EncodeRenderBridgeControlFrame(1U, control).bytes;
  Require(!frame.empty(), "malformed fixture did not encode");
  frame[kRenderTransportEnvelopeHeaderBytes + 5U] = 1U;
  const std::vector<std::uint8_t> payload(
      frame.begin() +
          static_cast<std::ptrdiff_t>(kRenderTransportEnvelopeHeaderBytes),
      frame.end());
  frame = EncodeRenderTransportEnvelope(
              RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1, 1U,
              payload, kRenderBridgeControlTransportMaximumPayloadBytes)
              .bytes;
  RenderTransportSequenceState sequence;
  RenderBridgeControlTransportDecoder decoder(7U, sequence);
  Require(decoder.Accept(frame).status ==
              RenderTransportStatus::MALFORMED_PAYLOAD &&
              sequence.next_expected_sequence() == 1U,
          "nonzero reserved control byte advanced lineage");

  const auto valid = EncodeRenderBridgeControlFrame(1U, control);
  Require(decoder.Accept(valid.bytes).ok() &&
              decoder.Accept(valid.bytes).status ==
                  RenderTransportStatus::REPLAYED_SEQUENCE,
          "replayed reverse control sequence was accepted");
}

} // namespace

int main() {
  TestStatusAndWireContract();
  TestInterleavedReverseSequenceAndRegistryIdentity();
  TestMalformedAndLineageFailures();
  return EXIT_SUCCESS;
}
