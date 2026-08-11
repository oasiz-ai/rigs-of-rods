/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RenderTransportEnvelope.h"

#include "RenderTransportDetail.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <stdexcept>

namespace RoR::Render {
namespace {

using TransportDetail::WireWriter;

constexpr std::uint16_t kHeaderFlags = 0U;
constexpr std::size_t kPayloadDigestOffset = 32U;

constexpr std::array<std::uint32_t, 64U> kSha256RoundConstants{{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
}};

constexpr std::uint32_t RotateRight(std::uint32_t value,
                                    std::uint32_t amount) noexcept {
  return (value >> amount) | (value << (32U - amount));
}

class Sha256 final {
public:
  void Update(const std::uint8_t *bytes, std::size_t size) noexcept {
    for (std::size_t index = 0U; index < size; ++index) {
      block_[block_size_++] = bytes[index];
      if (block_size_ == block_.size()) {
        Transform();
        total_bytes_ += block_.size();
        block_size_ = 0U;
      }
    }
  }

  [[nodiscard]] std::array<std::uint8_t, 32U> Final() noexcept {
    const std::uint64_t bit_count =
        static_cast<std::uint64_t>(total_bytes_ + block_size_) * 8ULL;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      while (block_size_ < block_.size()) {
        block_[block_size_++] = 0U;
      }
      Transform();
      block_size_ = 0U;
    }
    while (block_size_ < 56U) {
      block_[block_size_++] = 0U;
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
      block_[63U - index] =
          static_cast<std::uint8_t>(bit_count >> (index * 8U));
    }
    Transform();

    std::array<std::uint8_t, 32U> digest{};
    for (std::size_t word = 0U; word < state_.size(); ++word) {
      for (std::size_t byte = 0U; byte < 4U; ++byte) {
        digest[word * 4U + byte] = static_cast<std::uint8_t>(
            state_[word] >> ((3U - byte) * 8U));
      }
    }
    return digest;
  }

private:
  void Transform() noexcept {
    std::array<std::uint32_t, 64U> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
      const std::size_t offset = index * 4U;
      words[index] = (static_cast<std::uint32_t>(block_[offset]) << 24U) |
                     (static_cast<std::uint32_t>(block_[offset + 1U]) << 16U) |
                     (static_cast<std::uint32_t>(block_[offset + 2U]) << 8U) |
                     static_cast<std::uint32_t>(block_[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const std::uint32_t before = words[index - 15U];
      const std::uint32_t after = words[index - 2U];
      const std::uint32_t sigma0 = RotateRight(before, 7U) ^
                                   RotateRight(before, 18U) ^ (before >> 3U);
      const std::uint32_t sigma1 = RotateRight(after, 17U) ^
                                   RotateRight(after, 19U) ^ (after >> 10U);
      words[index] = words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
    }

    std::uint32_t a = state_[0U];
    std::uint32_t b = state_[1U];
    std::uint32_t c = state_[2U];
    std::uint32_t d = state_[3U];
    std::uint32_t e = state_[4U];
    std::uint32_t f = state_[5U];
    std::uint32_t g = state_[6U];
    std::uint32_t h = state_[7U];

    for (std::size_t index = 0U; index < words.size(); ++index) {
      const std::uint32_t sum1 = RotateRight(e, 6U) ^ RotateRight(e, 11U) ^
                                 RotateRight(e, 25U);
      const std::uint32_t choose = (e & f) ^ ((~e) & g);
      const std::uint32_t temporary1 =
          h + sum1 + choose + kSha256RoundConstants[index] + words[index];
      const std::uint32_t sum0 = RotateRight(a, 2U) ^ RotateRight(a, 13U) ^
                                 RotateRight(a, 22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }

    state_[0U] += a;
    state_[1U] += b;
    state_[2U] += c;
    state_[3U] += d;
    state_[4U] += e;
    state_[5U] += f;
    state_[6U] += g;
    state_[7U] += h;
  }

  std::array<std::uint32_t, 8U> state_{{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
  }};
  std::array<std::uint8_t, 64U> block_{};
  std::size_t block_size_ = 0U;
  std::size_t total_bytes_ = 0U;
};

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
                  const std::array<std::uint8_t, 32U> &computed) noexcept {
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
  case RenderTransportMessageKind::SCENE_SNAPSHOT_V4_CAMERA_V2:
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

std::array<std::uint8_t, 32U>
ComputeRenderTransportPayloadDigest(const std::uint8_t *payload,
                                    std::size_t payload_size) noexcept {
  if (payload == nullptr && payload_size != 0U) {
    return {};
  }
  Sha256 hasher;
  if (payload_size != 0U) {
    hasher.Update(payload, payload_size);
  }
  return hasher.Final();
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
    const auto digest = ComputeRenderTransportPayloadDigest(
        payload.data(), payload.size());
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
  const auto digest = ComputeRenderTransportPayloadDigest(
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
