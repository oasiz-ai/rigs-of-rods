/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "SceneGenerationBoundaryTransport.h"

#include "RenderTransportDetail.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace RoR::Render {
namespace {

using TransportDetail::AllocationBudget;
using TransportDetail::WireReader;
using TransportDetail::WireWriter;

constexpr std::uint64_t kBoundaryPayloadBytes = 48U;

bool IsIdentifier(std::uint64_t value) noexcept {
  return value != 0U && value != (std::numeric_limits<std::uint64_t>::max)();
}

RenderTransportEnvelopeEncodeResult Failure(
    RenderTransportStatus status) noexcept {
  RenderTransportEnvelopeEncodeResult result;
  result.status = status;
  return result;
}

SceneGenerationBoundaryTransportDecodeResult DecodeFailure(
    std::uint64_t sequence, RenderTransportStatus status) noexcept {
  SceneGenerationBoundaryTransportDecodeResult result;
  result.sequence = sequence;
  result.status = status;
  return result;
}

} // namespace

bool IsValidSceneGenerationBoundary(
    const SceneGenerationBoundary &boundary) noexcept {
  return boundary.version == kSceneGenerationBoundaryPayloadVersion &&
         IsIdentifier(boundary.registry_id) &&
         IsIdentifier(boundary.completed_generation) &&
         IsIdentifier(boundary.next_generation) &&
         boundary.completed_generation <
             (std::numeric_limits<std::uint64_t>::max)() - 1U &&
         boundary.next_generation == boundary.completed_generation + 1U &&
         IsIdentifier(boundary.asset_sequence) &&
         IsIdentifier(boundary.finalized_snapshot_id);
}

RenderTransportEnvelopeEncodeResult EncodeSceneGenerationBoundaryFrame(
    std::uint64_t sequence, const SceneGenerationBoundary &boundary) {
  if (!IsValidSceneGenerationBoundary(boundary)) {
    return Failure(RenderTransportStatus::INVALID_ARGUMENT);
  }
  try {
    std::vector<std::uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(kBoundaryPayloadBytes));
    WireWriter writer(&payload, kSceneGenerationBoundaryMaximumPayloadBytes);
    if (!writer.AddU32(boundary.version) || !writer.AddU32(0U) ||
        !writer.AddU64(boundary.registry_id) ||
        !writer.AddU64(boundary.completed_generation) ||
        !writer.AddU64(boundary.next_generation) ||
        !writer.AddU64(boundary.asset_sequence) ||
        !writer.AddU64(boundary.finalized_snapshot_id) ||
        writer.size() != kBoundaryPayloadBytes) {
      return Failure(RenderTransportStatus::INVALID_ARGUMENT);
    }
    return EncodeRenderTransportEnvelope(
        RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1, sequence,
        payload, kSceneGenerationBoundaryMaximumPayloadBytes);
  } catch (const std::bad_alloc &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (const std::length_error &) {
    return Failure(RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (...) {
    return Failure(RenderTransportStatus::INVALID_ARGUMENT);
  }
}

SceneGenerationBoundaryTransportDecoder::
    SceneGenerationBoundaryTransportDecoder(
        std::uint64_t registry_id,
        RenderTransportSequenceState &shared_sequence_state) noexcept
    : registry_id_(registry_id), sequence_state_(&shared_sequence_state) {}

SceneGenerationBoundaryTransportDecodeResult
SceneGenerationBoundaryTransportDecoder::Accept(
    const std::vector<std::uint8_t> &frame) noexcept {
  RenderTransportEnvelopeView envelope;
  const RenderTransportStatus envelope_status = DecodeRenderTransportEnvelope(
      frame, kSceneGenerationBoundaryMaximumPayloadBytes, envelope);
  if (envelope_status != RenderTransportStatus::OK) {
    return DecodeFailure(0U, envelope_status);
  }
  if (envelope.kind !=
      RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1) {
    return DecodeFailure(envelope.sequence,
                         RenderTransportStatus::UNKNOWN_MESSAGE_KIND);
  }
  if (!IsIdentifier(registry_id_) || sequence_state_ == nullptr) {
    return DecodeFailure(envelope.sequence,
                         RenderTransportStatus::INVALID_ARGUMENT);
  }
  const RenderTransportStatus sequence_status =
      sequence_state_->ValidateCandidate(envelope.sequence);
  if (sequence_status != RenderTransportStatus::OK) {
    return DecodeFailure(envelope.sequence, sequence_status);
  }

  AllocationBudget budget(0U);
  WireReader reader(envelope.payload, envelope.payload_size, budget);
  SceneGenerationBoundaryTransportDecodeResult result;
  result.sequence = envelope.sequence;
  std::uint32_t reserved = 0U;
  if (!reader.ReadU32(result.boundary.version) ||
      !reader.ReadU32(reserved) ||
      !reader.ReadU64(result.boundary.registry_id) ||
      !reader.ReadU64(result.boundary.completed_generation) ||
      !reader.ReadU64(result.boundary.next_generation) ||
      !reader.ReadU64(result.boundary.asset_sequence) ||
      !reader.ReadU64(result.boundary.finalized_snapshot_id) ||
      !reader.consumed() || reserved != 0U) {
    return DecodeFailure(envelope.sequence,
                         reader.status() == RenderTransportStatus::OK
                             ? RenderTransportStatus::MALFORMED_PAYLOAD
                             : reader.status());
  }
  if (result.boundary.version != kSceneGenerationBoundaryPayloadVersion) {
    return DecodeFailure(
        envelope.sequence,
        RenderTransportStatus::UNSUPPORTED_TRANSPORT_VERSION);
  }
  if (!IsValidSceneGenerationBoundary(result.boundary)) {
    return DecodeFailure(envelope.sequence,
                         RenderTransportStatus::RECONCILIATION_MISMATCH);
  }
  if (result.boundary.registry_id != registry_id_) {
    return DecodeFailure(envelope.sequence,
                         RenderTransportStatus::REGISTRY_VALIDATION_FAILED);
  }
  if (!sequence_state_->CommitAccepted(envelope.sequence)) {
    return DecodeFailure(envelope.sequence,
                         RenderTransportStatus::INVALID_SEQUENCE);
  }
  result.status = RenderTransportStatus::OK;
  return result;
}

} // namespace RoR::Render
