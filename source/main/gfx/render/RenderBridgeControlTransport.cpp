/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderBridgeControlTransport.h"

#include "RenderTransportDetail.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace RoR::Render {
namespace {

using TransportDetail::AllocationBudget;
using TransportDetail::WireReader;
using TransportDetail::WireWriter;

constexpr std::uint64_t kAcknowledgementPayloadBytes = 40U;
constexpr std::uint64_t kControlPayloadBytes = 32U;

RenderTransportEnvelopeEncodeResult Failure(
    RenderTransportStatus status) noexcept {
  RenderTransportEnvelopeEncodeResult result;
  result.status = status;
  return result;
}

RenderBridgeControlTransportDecodeResult DecodeFailure(
    RenderTransportMessageKind kind, std::uint64_t sequence,
    RenderTransportStatus status) noexcept {
  RenderBridgeControlTransportDecodeResult result;
  result.kind = kind;
  result.sequence = sequence;
  result.status = status;
  return result;
}

bool IsValidIdentifier(std::uint64_t value) noexcept {
  return value != 0U && value != (std::numeric_limits<std::uint64_t>::max)();
}

} // namespace

bool IsKnownRenderBridgeControlKind(RenderBridgeControlKind kind) noexcept {
  switch (kind) {
  case RenderBridgeControlKind::PEER_READY:
  case RenderBridgeControlKind::REQUEST_GRACEFUL_SHUTDOWN:
  case RenderBridgeControlKind::HEARTBEAT:
    return true;
  }
  return false;
}

RenderTransportEnvelopeEncodeResult EncodeRenderBridgeAcknowledgementFrame(
    std::uint64_t sequence,
    const RenderBridgeAcknowledgement &acknowledgement) {
  if (acknowledgement.version !=
          kRenderBridgeControlTransportPayloadVersion ||
      !IsValidIdentifier(acknowledgement.registry_id) ||
      !IsValidIdentifier(acknowledgement.through_forward_sequence)) {
    return Failure(RenderTransportStatus::INVALID_ARGUMENT);
  }
  const bool has_presented_scene =
      acknowledgement.presented_scene_sequence != 0U ||
      acknowledgement.presented_snapshot_id != 0U;
  if ((has_presented_scene &&
       (!IsValidIdentifier(acknowledgement.presented_scene_sequence) ||
        !IsValidIdentifier(acknowledgement.presented_snapshot_id) ||
        acknowledgement.presented_scene_sequence >
            acknowledgement.through_forward_sequence)) ||
      (!has_presented_scene &&
       (acknowledgement.presented_scene_sequence != 0U ||
        acknowledgement.presented_snapshot_id != 0U))) {
    return Failure(RenderTransportStatus::RECONCILIATION_MISMATCH);
  }
  try {
    std::vector<std::uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(kAcknowledgementPayloadBytes));
    WireWriter writer(&payload,
                      kRenderBridgeControlTransportMaximumPayloadBytes);
    if (!writer.AddU32(acknowledgement.version) || !writer.AddU32(0U) ||
        !writer.AddU64(acknowledgement.registry_id) ||
        !writer.AddU64(acknowledgement.through_forward_sequence) ||
        !writer.AddU64(acknowledgement.presented_scene_sequence) ||
        !writer.AddU64(acknowledgement.presented_snapshot_id) ||
        writer.size() != kAcknowledgementPayloadBytes) {
      return Failure(RenderTransportStatus::INVALID_ARGUMENT);
    }
    return EncodeRenderTransportEnvelope(
        RenderTransportMessageKind::RENDER_BRIDGE_ACKNOWLEDGEMENT_V1,
        sequence, payload, kRenderBridgeControlTransportMaximumPayloadBytes);
  } catch (const std::bad_alloc &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (const std::length_error &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (...) {
    return Failure(RenderTransportStatus::INVALID_ARGUMENT);
  }
}

RenderTransportEnvelopeEncodeResult EncodeRenderBridgeControlFrame(
    std::uint64_t sequence, const RenderBridgeControl &control) {
  if (control.version != kRenderBridgeControlTransportPayloadVersion ||
      !IsKnownRenderBridgeControlKind(control.kind) ||
      !IsValidIdentifier(control.registry_id) ||
      !IsValidIdentifier(control.command_id)) {
    return Failure(RenderTransportStatus::INVALID_ARGUMENT);
  }
  try {
    std::vector<std::uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(kControlPayloadBytes));
    WireWriter writer(&payload,
                      kRenderBridgeControlTransportMaximumPayloadBytes);
    if (!writer.AddU32(control.version) ||
        !writer.AddByte(static_cast<std::uint8_t>(control.kind)) ||
        !writer.AddByte(0U) || !writer.AddU16(0U) ||
        !writer.AddU64(control.registry_id) ||
        !writer.AddU64(control.command_id) || !writer.AddU64(0U) ||
        writer.size() != kControlPayloadBytes) {
      return Failure(RenderTransportStatus::INVALID_ARGUMENT);
    }
    return EncodeRenderTransportEnvelope(
        RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1, sequence,
        payload, kRenderBridgeControlTransportMaximumPayloadBytes);
  } catch (const std::bad_alloc &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (const std::length_error &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (...) {
    return Failure(RenderTransportStatus::INVALID_ARGUMENT);
  }
}

RenderBridgeControlTransportDecoder::RenderBridgeControlTransportDecoder(
    std::uint64_t registry_id,
    RenderTransportSequenceState &shared_sequence_state) noexcept
    : registry_id_(registry_id), sequence_state_(&shared_sequence_state) {}

RenderBridgeControlTransportDecodeResult
RenderBridgeControlTransportDecoder::Accept(
    const std::vector<std::uint8_t> &frame) noexcept {
  RenderTransportEnvelopeView envelope;
  const RenderTransportStatus envelope_status = DecodeRenderTransportEnvelope(
      frame, kRenderBridgeControlTransportMaximumPayloadBytes, envelope);
  if (envelope_status != RenderTransportStatus::OK) {
    return DecodeFailure(RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1,
                         0U, envelope_status);
  }
  if (envelope.kind !=
          RenderTransportMessageKind::RENDER_BRIDGE_ACKNOWLEDGEMENT_V1 &&
      envelope.kind != RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1) {
    return DecodeFailure(envelope.kind, envelope.sequence,
                         RenderTransportStatus::UNKNOWN_MESSAGE_KIND);
  }
  if (registry_id_ == 0U || sequence_state_ == nullptr) {
    return DecodeFailure(envelope.kind, envelope.sequence,
                         RenderTransportStatus::INVALID_ARGUMENT);
  }
  const RenderTransportStatus sequence_status =
      sequence_state_->ValidateCandidate(envelope.sequence);
  if (sequence_status != RenderTransportStatus::OK) {
    return DecodeFailure(envelope.kind, envelope.sequence, sequence_status);
  }

  AllocationBudget allocation_budget(0U);
  WireReader reader(envelope.payload, envelope.payload_size,
                    allocation_budget);
  RenderBridgeControlTransportDecodeResult result;
  result.kind = envelope.kind;
  result.sequence = envelope.sequence;
  std::uint32_t version = 0U;
  std::uint64_t payload_registry_id = 0U;
  if (envelope.kind ==
      RenderTransportMessageKind::RENDER_BRIDGE_ACKNOWLEDGEMENT_V1) {
    std::uint32_t reserved = 0U;
    if (!reader.ReadU32(version) || !reader.ReadU32(reserved) ||
        !reader.ReadU64(payload_registry_id) ||
        !reader.ReadU64(result.acknowledgement.through_forward_sequence) ||
        !reader.ReadU64(result.acknowledgement.presented_scene_sequence) ||
        !reader.ReadU64(result.acknowledgement.presented_snapshot_id) ||
        !reader.consumed() || reserved != 0U) {
      return DecodeFailure(envelope.kind, envelope.sequence,
                           reader.status() == RenderTransportStatus::OK
                               ? RenderTransportStatus::MALFORMED_PAYLOAD
                               : reader.status());
    }
    result.acknowledgement.version = version;
    result.acknowledgement.registry_id = payload_registry_id;
    const bool has_presented_scene =
        result.acknowledgement.presented_scene_sequence != 0U ||
        result.acknowledgement.presented_snapshot_id != 0U;
    if (version != kRenderBridgeControlTransportPayloadVersion ||
        !IsValidIdentifier(
            result.acknowledgement.through_forward_sequence) ||
        (has_presented_scene &&
         (!IsValidIdentifier(
              result.acknowledgement.presented_scene_sequence) ||
          !IsValidIdentifier(result.acknowledgement.presented_snapshot_id) ||
          result.acknowledgement.presented_scene_sequence >
              result.acknowledgement.through_forward_sequence))) {
      return DecodeFailure(
          envelope.kind, envelope.sequence,
          version != kRenderBridgeControlTransportPayloadVersion
              ? RenderTransportStatus::UNSUPPORTED_TRANSPORT_VERSION
              : RenderTransportStatus::RECONCILIATION_MISMATCH);
    }
  } else {
    std::uint8_t encoded_kind = 0U;
    std::uint8_t reserved_byte = 0U;
    std::uint16_t reserved_u16 = 0U;
    std::uint64_t reserved_u64 = 0U;
    if (!reader.ReadU32(version) || !reader.ReadByte(encoded_kind) ||
        !reader.ReadByte(reserved_byte) ||
        !reader.ReadU16(reserved_u16) ||
        !reader.ReadU64(payload_registry_id) ||
        !reader.ReadU64(result.control.command_id) ||
        !reader.ReadU64(reserved_u64) || !reader.consumed() ||
        reserved_byte != 0U || reserved_u16 != 0U || reserved_u64 != 0U) {
      return DecodeFailure(envelope.kind, envelope.sequence,
                           reader.status() == RenderTransportStatus::OK
                               ? RenderTransportStatus::MALFORMED_PAYLOAD
                               : reader.status());
    }
    result.control.version = version;
    result.control.kind =
        static_cast<RenderBridgeControlKind>(encoded_kind);
    result.control.registry_id = payload_registry_id;
    if (version != kRenderBridgeControlTransportPayloadVersion ||
        !IsKnownRenderBridgeControlKind(result.control.kind) ||
        !IsValidIdentifier(result.control.command_id)) {
      return DecodeFailure(
          envelope.kind, envelope.sequence,
          version != kRenderBridgeControlTransportPayloadVersion
              ? RenderTransportStatus::UNSUPPORTED_TRANSPORT_VERSION
              : RenderTransportStatus::MALFORMED_PAYLOAD);
    }
  }
  if (payload_registry_id != registry_id_) {
    return DecodeFailure(envelope.kind, envelope.sequence,
                         RenderTransportStatus::REGISTRY_VALIDATION_FAILED);
  }
  if (!sequence_state_->CommitAccepted(envelope.sequence)) {
    return DecodeFailure(envelope.kind, envelope.sequence,
                         RenderTransportStatus::INVALID_SEQUENCE);
  }
  result.status = RenderTransportStatus::OK;
  return result;
}

} // namespace RoR::Render
