/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ReflectionProbeCaptureReceipt.h"

#include <algorithm>
#include <limits>

namespace RoR::Render {
namespace {

constexpr std::uint32_t kRgba16FloatBytesPerPixel = 8U;

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t element = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, element);
}

class StableHasher final {
public:
  void AddByte(std::uint8_t value) noexcept {
    hash_ ^= value;
    hash_ *= UINT64_C(1099511628211);
  }

  void AddU16(std::uint16_t value) noexcept {
    AddByte(static_cast<std::uint8_t>(value & 0xFFU));
    AddByte(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  }

  void AddU32(std::uint32_t value) noexcept {
    for (std::uint32_t index = 0U; index < 4U; ++index) {
      AddByte(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
  }

  void AddU64(std::uint64_t value) noexcept {
    for (std::uint32_t index = 0U; index < 8U; ++index) {
      AddByte(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
  }

  void AddBytes(const std::uint8_t *bytes, std::size_t count) noexcept {
    for (std::size_t index = 0U; index < count; ++index) {
      AddByte(bytes[index]);
    }
  }

  [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
  std::uint64_t hash_ = UINT64_C(14695981039346656037);
};

bool TryMultiply(std::uint64_t lhs, std::uint64_t rhs,
                 std::uint64_t &product) noexcept {
  if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  product = lhs * rhs;
  return true;
}

ValidationResult ValidateRequest(const ReflectionProbeUpdateRequest &request) {
  const ValidationResult descriptor =
      ValidateReflectionProbeRuntimeDescriptor(request.descriptor);
  if (!descriptor) {
    return Failure(descriptor.code, "request.descriptor",
                   "capture request carries an invalid probe descriptor");
  }
  if (request.probe_id != request.descriptor.probe_id ||
      request.probe_id == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "request.probe_id",
                   "capture request identity differs from its descriptor");
  }
  if (request.content_revision != request.descriptor.content_revision ||
      request.content_revision == 0U) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "request.content_revision",
                   "capture request revision differs from its descriptor");
  }
  if (request.candidate_generation == 0U ||
      request.deterministic_seed == 0U) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "request.capture_lineage",
                   "capture generation and deterministic seed must be nonzero");
  }
  if (request.descriptor_fingerprint !=
          ComputeReflectionProbeDescriptorFingerprint(request.descriptor) ||
      request.descriptor_fingerprint == 0U) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "request.descriptor_fingerprint",
                   "capture request descriptor fingerprint is stale");
  }
  if (request.resolution != request.descriptor.resolution ||
      request.expected_face_count != kReflectionProbeCubemapFaceCount ||
      request.expected_mip_count == 0U) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "request.capture_shape",
                   "capture request shape differs from its descriptor contract");
  }
  std::uint16_t available_mips = 1U;
  std::uint16_t dimension = request.resolution;
  while (dimension > 1U) {
    dimension = static_cast<std::uint16_t>(dimension >> 1U);
    ++available_mips;
  }
  if (request.expected_mip_count > available_mips) {
    return Failure(ValidationCode::INVALID_DIMENSIONS,
                   "request.expected_mip_count",
                   "capture request asks for more mips than its resolution owns");
  }
  return ValidationResult::Success();
}

} // namespace

bool IsKnownReflectionProbeCaptureBackend(
    ReflectionProbeCaptureBackend backend) noexcept {
  switch (backend) {
  case ReflectionProbeCaptureBackend::OGRE_NEXT_METAL:
  case ReflectionProbeCaptureBackend::OGRE_NEXT_VULKAN:
  case ReflectionProbeCaptureBackend::OGRE_NEXT_D3D11:
  case ReflectionProbeCaptureBackend::OGRE_NEXT_D3D12:
    return true;
  }
  return false;
}

bool IsKnownReflectionProbeCapturePixelFormat(
    ReflectionProbeCapturePixelFormat format) noexcept {
  return format == ReflectionProbeCapturePixelFormat::RGBA16_FLOAT;
}

ReflectionProbeCaptureReceiptResult ComputeReflectionProbeCaptureReceipt(
    const ReflectionProbeUpdateRequest &request,
    ReflectionProbeCaptureBackend backend,
    ReflectionProbeCapturePixelFormat pixel_format,
    std::uint64_t native_execution_receipt,
    const std::vector<ReflectionProbeCapturedSubresourceView> &subresources) {
  ReflectionProbeCaptureReceiptResult result;
  result.validation = ValidateRequest(request);
  if (!result.validation) {
    return result;
  }
  if (!IsKnownReflectionProbeCaptureBackend(backend)) {
    result.validation = Failure(ValidationCode::INVALID_ENUM, "backend",
                                "unknown native reflection-capture backend");
    return result;
  }
  if (!IsKnownReflectionProbeCapturePixelFormat(pixel_format)) {
    result.validation = Failure(ValidationCode::INVALID_ENUM, "pixel_format",
                                "unknown reflection-capture pixel format");
    return result;
  }
  if (native_execution_receipt == 0U) {
    result.validation = Failure(
        ValidationCode::EMPTY_PAYLOAD, "native_execution_receipt",
        "native IBL execution must supply an independently measured receipt");
    return result;
  }

  std::uint64_t expected_subresource_count = 0U;
  if (!TryMultiply(request.expected_face_count, request.expected_mip_count,
                   expected_subresource_count) ||
      expected_subresource_count != subresources.size()) {
    result.validation = Failure(
        ValidationCode::SIZE_MISMATCH, "subresources",
        "capture must contain every required face and mip exactly once");
    return result;
  }

  StableHasher hasher;
  hasher.AddU32(kReflectionProbeCaptureReceiptVersion);
  hasher.AddByte(static_cast<std::uint8_t>(backend));
  hasher.AddByte(static_cast<std::uint8_t>(pixel_format));
  hasher.AddU64(request.probe_id);
  hasher.AddU64(request.content_revision);
  hasher.AddU64(request.candidate_generation);
  hasher.AddU64(request.simulation_tick);
  hasher.AddU64(request.deterministic_seed);
  hasher.AddU64(request.descriptor_fingerprint);
  hasher.AddU16(request.resolution);
  hasher.AddU16(request.expected_mip_count);
  hasher.AddU32(request.expected_face_count);
  hasher.AddU64(native_execution_receipt);

  std::uint64_t canonical_payload_bytes = 0U;
  for (std::size_t index = 0U; index < subresources.size(); ++index) {
    const ReflectionProbeCapturedSubresourceView &subresource =
        subresources[index];
    const std::uint16_t expected_mip = static_cast<std::uint16_t>(
        index / request.expected_face_count);
    const std::uint32_t expected_face = static_cast<std::uint32_t>(
        index % request.expected_face_count);
    if (subresource.version != kReflectionProbeCaptureReceiptVersion) {
      result.validation = Failure(
          ValidationCode::UNSUPPORTED_VERSION, "subresources.version",
          "unsupported captured-subresource version", index);
      return result;
    }
    if (subresource.mip_level != expected_mip ||
        subresource.face_index != expected_face) {
      result.validation = Failure(
          ValidationCode::NON_DETERMINISTIC_ORDER, "subresources.order",
          "subresources must be mip-major then face-major", index);
      return result;
    }
    const std::uint32_t expected_dimension =
        (std::max)(1U, static_cast<std::uint32_t>(request.resolution) >>
                           expected_mip);
    if (subresource.width != expected_dimension ||
        subresource.height != expected_dimension) {
      result.validation = Failure(
          ValidationCode::INVALID_DIMENSIONS, "subresources.dimensions",
          "captured subresource dimensions do not match its mip", index);
      return result;
    }
    std::uint64_t active_row_bytes = 0U;
    std::uint64_t required_storage = 0U;
    if (!TryMultiply(expected_dimension, kRgba16FloatBytesPerPixel,
                     active_row_bytes) ||
        subresource.row_pitch_bytes < active_row_bytes ||
        !TryMultiply(subresource.row_pitch_bytes, expected_dimension,
                     required_storage) ||
        required_storage > subresource.byte_count ||
        subresource.bytes == nullptr) {
      result.validation = Failure(
          ValidationCode::SIZE_MISMATCH, "subresources.bytes",
          "captured subresource storage is null, truncated, or under-pitched",
          index);
      return result;
    }
    std::uint64_t subresource_payload = 0U;
    if (!TryMultiply(active_row_bytes, expected_dimension,
                     subresource_payload) ||
        subresource_payload >
            (std::numeric_limits<std::uint64_t>::max)() -
                canonical_payload_bytes) {
      result.validation = Failure(
          ValidationCode::SIZE_MISMATCH, "subresources.bytes",
          "canonical capture payload size overflowed", index);
      return result;
    }
    canonical_payload_bytes += subresource_payload;
    hasher.AddU16(expected_mip);
    hasher.AddU32(expected_face);
    hasher.AddU32(expected_dimension);
    hasher.AddU32(expected_dimension);
    hasher.AddU64(active_row_bytes);
    for (std::uint32_t row = 0U; row < expected_dimension; ++row) {
      const std::uint8_t *row_bytes =
          subresource.bytes +
          static_cast<std::size_t>(subresource.row_pitch_bytes) * row;
      hasher.AddBytes(row_bytes, static_cast<std::size_t>(active_row_bytes));
    }
  }

  result.capture_digest = hasher.value();
  if (result.capture_digest == 0U) {
    result.validation = Failure(ValidationCode::EMPTY_PAYLOAD,
                                "capture_digest",
                                "canonical capture digest must be nonzero");
    return result;
  }
  result.completed_face_count = request.expected_face_count;
  result.completed_mip_count = request.expected_mip_count;
  result.canonical_payload_bytes = canonical_payload_bytes;
  result.validation = ValidationResult::Success();
  return result;
}

} // namespace RoR::Render
