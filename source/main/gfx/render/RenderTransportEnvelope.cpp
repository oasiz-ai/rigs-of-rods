/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderTransportEnvelope.h"

#include "RenderTransportDetail.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace RoR::Render {
namespace {

using TransportDetail::WireWriter;

constexpr std::uint16_t kHeaderFlags = 0U;
constexpr std::size_t kPayloadDigestOffset = 32U;

std::uint16_t ReadHeaderU16(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint16_t>(
      static_cast<std::uint32_t>(bytes[0U]) |
      (static_cast<std::uint32_t>(bytes[1U]) << 8U));
}

std::uint64_t ReadHeaderU64(const std::uint8_t *bytes) noexcept {
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

bool DigestsEqual(const std::uint8_t *encoded,
                  const RenderPayloadDigest &computed) noexcept {
  std::uint8_t difference = 0U;
  for (std::size_t index = 0U; index < computed.size(); ++index) {
    difference |= static_cast<std::uint8_t>(encoded[index] ^ computed[index]);
  }
  return difference == 0U;
}

} // namespace

bool IsKnownRenderTransportMessageKind(
    RenderTransportMessageKind kind) noexcept {
  switch (kind) {
  case RenderTransportMessageKind::SCENE_SNAPSHOT_V6_CAMERA_V2:
  case RenderTransportMessageKind::RENDER_ASSET_DELTA_V1:
  case RenderTransportMessageKind::INPUT_EVENT_BATCH_V1:
  case RenderTransportMessageKind::RENDER_BRIDGE_ACKNOWLEDGEMENT_V1:
  case RenderTransportMessageKind::RENDER_BRIDGE_CONTROL_V1:
  case RenderTransportMessageKind::SCENE_GENERATION_BOUNDARY_V1:
  case RenderTransportMessageKind::RENDER_ASSET_DELTA_V2:
    return true;
  }
  return false;
}

RenderTransportEnvelopeEncodeResult EncodeRenderTransportEnvelope(
    RenderTransportMessageKind kind, std::uint64_t sequence,
    const std::vector<std::uint8_t> &payload,
    std::uint64_t maximum_payload_bytes) {
  RenderTransportEnvelopeEncodeResult result;
  if (!IsKnownRenderTransportMessageKind(kind) || sequence == 0U ||
      sequence == (std::numeric_limits<std::uint64_t>::max)() ||
      payload.size() > maximum_payload_bytes) {
    result.status = payload.size() > maximum_payload_bytes
                        ? RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED
                        : RenderTransportStatus::INVALID_ARGUMENT;
    return result;
  }

  try {
    const auto digest = ComputeRenderPayloadDigest(payload.data(), payload.size());
    const std::uint64_t frame_size =
        kRenderTransportEnvelopeHeaderBytes +
        static_cast<std::uint64_t>(payload.size());
    result.bytes.reserve(static_cast<std::size_t>(frame_size));
    WireWriter writer(&result.bytes, frame_size);
    if (!writer.AddBytes(kRenderTransportEnvelopeMagic.data(),
                         kRenderTransportEnvelopeMagic.size()) ||
        !writer.AddU16(kRenderTransportEnvelopeVersion) ||
        !writer.AddU16(
            static_cast<std::uint16_t>(kRenderTransportEnvelopeHeaderBytes)) ||
        !writer.AddU16(static_cast<std::uint16_t>(kind)) ||
        !writer.AddU16(kHeaderFlags) || !writer.AddU64(sequence) ||
        !writer.AddU64(static_cast<std::uint64_t>(payload.size())) ||
        !writer.AddBytes(digest.data(), digest.size()) ||
        !writer.AddBytes(payload.data(), payload.size()) ||
        writer.size() != frame_size) {
      result.bytes.clear();
      result.status = RenderTransportStatus::INVALID_ARGUMENT;
      return result;
    }
    result.status = RenderTransportStatus::OK;
    return result;
  } catch (const std::bad_alloc &) {
    result.bytes.clear();
    result.status = RenderTransportStatus::ALLOCATION_FAILURE;
    return result;
  } catch (const std::length_error &) {
    result.bytes.clear();
    result.status = RenderTransportStatus::ALLOCATION_FAILURE;
    return result;
  }
}

RenderTransportStatus DecodeRenderTransportEnvelope(
    const std::vector<std::uint8_t> &frame,
    std::uint64_t maximum_payload_bytes,
    RenderTransportEnvelopeView &view) noexcept {
  if (frame.size() < kRenderTransportEnvelopeHeaderBytes) {
    return RenderTransportStatus::FRAME_TRUNCATED;
  }
  if (!std::equal(kRenderTransportEnvelopeMagic.begin(),
                  kRenderTransportEnvelopeMagic.end(), frame.begin())) {
    return RenderTransportStatus::INVALID_MAGIC;
  }
  const std::uint16_t transport_version = ReadHeaderU16(frame.data() + 8U);
  const std::uint16_t header_bytes = ReadHeaderU16(frame.data() + 10U);
  const auto kind = static_cast<RenderTransportMessageKind>(
      ReadHeaderU16(frame.data() + 12U));
  const std::uint16_t flags = ReadHeaderU16(frame.data() + 14U);
  const std::uint64_t sequence = ReadHeaderU64(frame.data() + 16U);
  const std::uint64_t payload_size = ReadHeaderU64(frame.data() + 24U);
  if (transport_version != kRenderTransportEnvelopeVersion) {
    return RenderTransportStatus::UNSUPPORTED_TRANSPORT_VERSION;
  }
  if (header_bytes != kRenderTransportEnvelopeHeaderBytes ||
      flags != kHeaderFlags) {
    return RenderTransportStatus::INVALID_HEADER;
  }
  if (!IsKnownRenderTransportMessageKind(kind)) {
    return RenderTransportStatus::UNKNOWN_MESSAGE_KIND;
  }
  if (sequence == 0U ||
      sequence == (std::numeric_limits<std::uint64_t>::max)()) {
    return RenderTransportStatus::INVALID_SEQUENCE;
  }
  if (payload_size > maximum_payload_bytes) {
    return RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED;
  }
  if (payload_size != frame.size() - kRenderTransportEnvelopeHeaderBytes) {
    return RenderTransportStatus::FRAME_SIZE_MISMATCH;
  }
  const std::uint8_t *payload =
      frame.data() + kRenderTransportEnvelopeHeaderBytes;
  const auto digest = ComputeRenderPayloadDigest(
      payload, static_cast<std::size_t>(payload_size));
  if (!DigestsEqual(frame.data() + kPayloadDigestOffset, digest)) {
    return RenderTransportStatus::PAYLOAD_DIGEST_MISMATCH;
  }

  const RenderTransportEnvelopeView candidate{
      kind, sequence, payload, static_cast<std::size_t>(payload_size)};
  view = candidate;
  return RenderTransportStatus::OK;
}

RenderTransportStatus RenderTransportSequenceState::ValidateCandidate(
    std::uint64_t sequence) const noexcept {
  if (next_expected_sequence_ == 0U ||
      next_expected_sequence_ == (std::numeric_limits<std::uint64_t>::max)() ||
      sequence == 0U ||
      sequence == (std::numeric_limits<std::uint64_t>::max)()) {
    return RenderTransportStatus::INVALID_SEQUENCE;
  }
  if (sequence < next_expected_sequence_) {
    return RenderTransportStatus::REPLAYED_SEQUENCE;
  }
  if (sequence > next_expected_sequence_) {
    return RenderTransportStatus::OUT_OF_ORDER_SEQUENCE;
  }
  return RenderTransportStatus::OK;
}

bool RenderTransportSequenceState::CommitAccepted(
    std::uint64_t sequence) noexcept {
  if (ValidateCandidate(sequence) != RenderTransportStatus::OK) {
    return false;
  }
  last_accepted_sequence_ = sequence;
  next_expected_sequence_ = sequence + 1U;
  return true;
}

} // namespace RoR::Render
