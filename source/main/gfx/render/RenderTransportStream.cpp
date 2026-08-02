/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderTransportStream.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace RoR::Render {
namespace {

std::uint16_t ReadU16(const std::uint8_t *bytes) noexcept {
  return static_cast<std::uint16_t>(bytes[0U]) |
         (static_cast<std::uint16_t>(bytes[1U]) << 8U);
}

std::uint64_t ReadU64(const std::uint8_t *bytes) noexcept {
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

} // namespace

RenderTransportStreamDecoder::RenderTransportStreamDecoder(
    std::uint64_t maximum_payload_bytes) noexcept
    : maximum_payload_bytes_(maximum_payload_bytes) {
  if (maximum_payload_bytes >
          kRenderTransportStreamAbsoluteMaximumPayloadBytes ||
      maximum_payload_bytes >
          static_cast<std::uint64_t>(
              (std::numeric_limits<std::size_t>::max)()) -
              kRenderTransportEnvelopeHeaderBytes) {
    Fail(RenderTransportStreamStatus::REJECTED_INVALID_CONFIGURATION,
         RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED);
  }
}

RenderTransportStreamResult RenderTransportStreamDecoder::MakeResult(
    RenderTransportStreamStatus status, std::size_t consumed) const noexcept {
  RenderTransportStreamResult result;
  result.status = status;
  result.transport_status = transport_status_;
  result.bytes_consumed = consumed;
  result.terminal = terminal_;
  return result;
}

void RenderTransportStreamDecoder::Fail(
    RenderTransportStreamStatus status,
    RenderTransportStatus transport_status) noexcept {
  status_ = status;
  transport_status_ = transport_status;
  terminal_ = true;
}

bool RenderTransportStreamDecoder::InspectCompleteHeader() noexcept {
  if (frame_.size() != kRenderTransportEnvelopeHeaderBytes) {
    Fail(RenderTransportStreamStatus::FAILED_INTERNAL,
         RenderTransportStatus::INVALID_HEADER);
    return false;
  }
  if (!std::equal(kRenderTransportEnvelopeMagic.begin(),
                  kRenderTransportEnvelopeMagic.end(), frame_.begin())) {
    Fail(RenderTransportStreamStatus::FAILED_HEADER,
         RenderTransportStatus::INVALID_MAGIC);
    return false;
  }
  const std::uint16_t version = ReadU16(frame_.data() + 8U);
  const std::uint16_t header_bytes = ReadU16(frame_.data() + 10U);
  const auto kind = static_cast<RenderTransportMessageKind>(
      ReadU16(frame_.data() + 12U));
  const std::uint16_t flags = ReadU16(frame_.data() + 14U);
  const std::uint64_t sequence = ReadU64(frame_.data() + 16U);
  const std::uint64_t payload_bytes = ReadU64(frame_.data() + 24U);
  if (version != kRenderTransportEnvelopeVersion) {
    Fail(RenderTransportStreamStatus::FAILED_HEADER,
         RenderTransportStatus::UNSUPPORTED_TRANSPORT_VERSION);
    return false;
  }
  if (header_bytes != kRenderTransportEnvelopeHeaderBytes || flags != 0U) {
    Fail(RenderTransportStreamStatus::FAILED_HEADER,
         RenderTransportStatus::INVALID_HEADER);
    return false;
  }
  if (!IsKnownRenderTransportMessageKind(kind)) {
    Fail(RenderTransportStreamStatus::FAILED_HEADER,
         RenderTransportStatus::UNKNOWN_MESSAGE_KIND);
    return false;
  }
  if (sequence == 0U ||
      sequence == (std::numeric_limits<std::uint64_t>::max)()) {
    Fail(RenderTransportStreamStatus::FAILED_HEADER,
         RenderTransportStatus::INVALID_SEQUENCE);
    return false;
  }
  std::uint64_t message_payload_limit = 0U;
  switch (kind) {
  case RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2:
    message_payload_limit = kRenderTransportStreamSceneMaximumPayloadBytes;
    break;
  case RenderTransportMessageKind::RENDER_ASSET_DELTA_V1:
    message_payload_limit = kRenderTransportStreamAssetMaximumPayloadBytes;
    break;
  case RenderTransportMessageKind::INPUT_EVENT_BATCH_V1:
    message_payload_limit = kRenderTransportStreamInputMaximumPayloadBytes;
    break;
  }
  if (message_payload_limit == 0U) {
    Fail(RenderTransportStreamStatus::FAILED_INTERNAL,
         RenderTransportStatus::UNKNOWN_MESSAGE_KIND);
    return false;
  }
  if (payload_bytes > maximum_payload_bytes_ ||
      payload_bytes > message_payload_limit) {
    Fail(RenderTransportStreamStatus::FAILED_HEADER,
         RenderTransportStatus::PAYLOAD_LIMIT_EXCEEDED);
    return false;
  }

  expected_frame_bytes_ =
      static_cast<std::uint64_t>(kRenderTransportEnvelopeHeaderBytes) +
      payload_bytes;
  kind_ = kind;
  sequence_ = sequence;
  try {
    frame_.reserve(static_cast<std::size_t>(expected_frame_bytes_));
  } catch (const std::bad_alloc &) {
    Fail(RenderTransportStreamStatus::FAILED_ALLOCATION,
         RenderTransportStatus::ALLOCATION_FAILURE);
    return false;
  } catch (const std::length_error &) {
    Fail(RenderTransportStreamStatus::FAILED_ALLOCATION,
         RenderTransportStatus::ALLOCATION_FAILURE);
    return false;
  } catch (...) {
    Fail(RenderTransportStreamStatus::FAILED_INTERNAL,
         RenderTransportStatus::ALLOCATION_FAILURE);
    return false;
  }
  return true;
}

bool RenderTransportStreamDecoder::ValidateCompleteFrame() noexcept {
  if (expected_frame_bytes_ == 0U ||
      frame_.size() != static_cast<std::size_t>(expected_frame_bytes_)) {
    Fail(RenderTransportStreamStatus::FAILED_INTERNAL,
         RenderTransportStatus::FRAME_SIZE_MISMATCH);
    return false;
  }
  RenderTransportEnvelopeView envelope;
  const RenderTransportStatus decoded = DecodeRenderTransportEnvelope(
      frame_, maximum_payload_bytes_, envelope);
  if (decoded != RenderTransportStatus::OK) {
    Fail(RenderTransportStreamStatus::FAILED_ENVELOPE, decoded);
    return false;
  }
  if (envelope.kind != kind_ || envelope.sequence != sequence_) {
    Fail(RenderTransportStreamStatus::FAILED_INTERNAL,
         RenderTransportStatus::INVALID_HEADER);
    return false;
  }
  status_ = RenderTransportStreamStatus::FRAME_READY;
  transport_status_ = RenderTransportStatus::OK;
  return true;
}

RenderTransportStreamResult RenderTransportStreamDecoder::Accept(
    const std::uint8_t *bytes, std::size_t size) noexcept {
  if (terminal_) {
    return MakeResult(status_, 0U);
  }
  if (status_ == RenderTransportStreamStatus::FRAME_READY) {
    return MakeResult(RenderTransportStreamStatus::REJECTED_FRAME_PENDING, 0U);
  }
  if (input_closed_ || status_ == RenderTransportStreamStatus::CLOSED) {
    return MakeResult(RenderTransportStreamStatus::REJECTED_CLOSED, 0U);
  }
  if (bytes == nullptr && size != 0U) {
    return MakeResult(RenderTransportStreamStatus::REJECTED_INVALID_ARGUMENT,
                      0U);
  }
  if (size == 0U) {
    return MakeResult(RenderTransportStreamStatus::NEED_MORE_DATA, 0U);
  }

  std::size_t consumed = 0U;
  try {
    if (frame_.size() < kRenderTransportEnvelopeHeaderBytes) {
      const std::size_t wanted =
          kRenderTransportEnvelopeHeaderBytes - frame_.size();
      const std::size_t count = (std::min)(wanted, size);
      frame_.insert(frame_.end(), bytes, bytes + count);
      consumed += count;
      if (frame_.size() < kRenderTransportEnvelopeHeaderBytes) {
        status_ = RenderTransportStreamStatus::NEED_MORE_DATA;
        return MakeResult(status_, consumed);
      }
      if (!InspectCompleteHeader()) {
        return MakeResult(status_, consumed);
      }
      if (expected_frame_bytes_ == kRenderTransportEnvelopeHeaderBytes) {
        (void)ValidateCompleteFrame();
        return MakeResult(status_, consumed);
      }
    }

    const std::size_t expected =
        static_cast<std::size_t>(expected_frame_bytes_);
    const std::size_t wanted = expected - frame_.size();
    const std::size_t available = size - consumed;
    const std::size_t count = (std::min)(wanted, available);
    frame_.insert(frame_.end(), bytes + consumed, bytes + consumed + count);
    consumed += count;
    if (frame_.size() == expected) {
      (void)ValidateCompleteFrame();
      return MakeResult(status_, consumed);
    }
    status_ = RenderTransportStreamStatus::NEED_MORE_DATA;
    return MakeResult(status_, consumed);
  } catch (const std::bad_alloc &) {
    Fail(RenderTransportStreamStatus::FAILED_ALLOCATION,
         RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (const std::length_error &) {
    Fail(RenderTransportStreamStatus::FAILED_ALLOCATION,
         RenderTransportStatus::ALLOCATION_FAILURE);
  } catch (...) {
    Fail(RenderTransportStreamStatus::FAILED_INTERNAL,
         RenderTransportStatus::INVALID_ARGUMENT);
  }
  return MakeResult(status_, consumed);
}

RenderTransportStreamResult RenderTransportStreamDecoder::Finish() noexcept {
  if (terminal_) {
    return MakeResult(status_, 0U);
  }
  if (input_closed_) {
    return MakeResult(status_ == RenderTransportStreamStatus::FRAME_READY
                          ? RenderTransportStreamStatus::FRAME_READY
                          : RenderTransportStreamStatus::CLOSED,
                      0U);
  }
  input_closed_ = true;
  if (status_ == RenderTransportStreamStatus::FRAME_READY) {
    return MakeResult(status_, 0U);
  }
  if (frame_.empty()) {
    status_ = RenderTransportStreamStatus::CLOSED;
    transport_status_ = RenderTransportStatus::OK;
    return MakeResult(status_, 0U);
  }
  Fail(RenderTransportStreamStatus::TRUNCATED_END_OF_STREAM,
       RenderTransportStatus::FRAME_TRUNCATED);
  return MakeResult(status_, 0U);
}

RenderTransportStreamFrameResult
RenderTransportStreamDecoder::TakeFrame() noexcept {
  RenderTransportStreamFrameResult result;
  if (status_ != RenderTransportStreamStatus::FRAME_READY || terminal_) {
    result.status = terminal_ ? status_
                              : RenderTransportStreamStatus::REJECTED_NO_FRAME;
    return result;
  }
  try {
    result.kind = kind_;
    result.sequence = sequence_;
    result.bytes = std::move(frame_);
    result.status = RenderTransportStreamStatus::FRAME_READY;

    frame_.clear();
    expected_frame_bytes_ = 0U;
    sequence_ = 0U;
    kind_ = RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2;
    status_ = input_closed_ ? RenderTransportStreamStatus::CLOSED
                            : RenderTransportStreamStatus::NEED_MORE_DATA;
    transport_status_ = RenderTransportStatus::OK;
    return result;
  } catch (...) {
    result.bytes.clear();
    result.sequence = 0U;
    result.status = RenderTransportStreamStatus::FAILED_INTERNAL;
    Fail(RenderTransportStreamStatus::FAILED_INTERNAL,
         RenderTransportStatus::ALLOCATION_FAILURE);
    return result;
  }
}

bool IsKnownRenderTransportStreamStatus(
    RenderTransportStreamStatus status) noexcept {
  switch (status) {
  case RenderTransportStreamStatus::NEED_MORE_DATA:
  case RenderTransportStreamStatus::FRAME_READY:
  case RenderTransportStreamStatus::CLOSED:
  case RenderTransportStreamStatus::REJECTED_INVALID_CONFIGURATION:
  case RenderTransportStreamStatus::REJECTED_INVALID_ARGUMENT:
  case RenderTransportStreamStatus::REJECTED_FRAME_PENDING:
  case RenderTransportStreamStatus::REJECTED_NO_FRAME:
  case RenderTransportStreamStatus::REJECTED_CLOSED:
  case RenderTransportStreamStatus::FAILED_HEADER:
  case RenderTransportStreamStatus::FAILED_ENVELOPE:
  case RenderTransportStreamStatus::FAILED_ALLOCATION:
  case RenderTransportStreamStatus::TRUNCATED_END_OF_STREAM:
  case RenderTransportStreamStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(RenderTransportStreamStatus status) noexcept {
  switch (status) {
  case RenderTransportStreamStatus::NEED_MORE_DATA:
    return "need-more-data";
  case RenderTransportStreamStatus::FRAME_READY:
    return "frame-ready";
  case RenderTransportStreamStatus::CLOSED:
    return "closed";
  case RenderTransportStreamStatus::REJECTED_INVALID_CONFIGURATION:
    return "rejected-invalid-configuration";
  case RenderTransportStreamStatus::REJECTED_INVALID_ARGUMENT:
    return "rejected-invalid-argument";
  case RenderTransportStreamStatus::REJECTED_FRAME_PENDING:
    return "rejected-frame-pending";
  case RenderTransportStreamStatus::REJECTED_NO_FRAME:
    return "rejected-no-frame";
  case RenderTransportStreamStatus::REJECTED_CLOSED:
    return "rejected-closed";
  case RenderTransportStreamStatus::FAILED_HEADER:
    return "failed-header";
  case RenderTransportStreamStatus::FAILED_ENVELOPE:
    return "failed-envelope";
  case RenderTransportStreamStatus::FAILED_ALLOCATION:
    return "failed-allocation";
  case RenderTransportStreamStatus::TRUNCATED_END_OF_STREAM:
    return "truncated-end-of-stream";
  case RenderTransportStreamStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "invalid";
}

} // namespace RoR::Render
