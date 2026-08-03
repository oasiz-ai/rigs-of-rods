/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ReflectionProbeCaptureReceipt.h"

#include <algorithm>
#include <cstring>
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

  void AddBool(bool value) noexcept { AddByte(value ? 1U : 0U); }

  void AddFloat(float value) noexcept {
    if (value == 0.0F) {
      value = 0.0F;
    }
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    AddU32(bits);
  }

  void AddDouble(double value) noexcept {
    if (value == 0.0) {
      value = 0.0;
    }
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    AddU64(bits);
  }

  void AddFloat3(const Float3 &value) noexcept {
    AddFloat(value.x);
    AddFloat(value.y);
    AddFloat(value.z);
  }

  void AddDouble3(const Double3 &value) noexcept {
    AddDouble(value.x);
    AddDouble(value.y);
    AddDouble(value.z);
  }

  void AddMatrix(const Matrix4x4 &value) noexcept {
    for (float element : value.elements) {
      AddFloat(element);
    }
  }

  void AddDescriptor(
      const ReflectionProbeRuntimeDescriptor &descriptor) noexcept {
    AddU32(descriptor.version);
    AddU64(descriptor.probe_id);
    AddU64(descriptor.content_revision);
    AddDouble3(descriptor.absolute_world_position_meters);
    AddMatrix(descriptor.world_from_probe_orientation);
    AddFloat3(descriptor.capture_position_local);
    AddFloat3(descriptor.influence_center_local);
    AddFloat3(descriptor.influence_half_size);
    AddFloat3(descriptor.influence_inner_fraction);
    AddFloat3(descriptor.correction_shape_center_local);
    AddFloat3(descriptor.correction_shape_half_size);
    AddU16(descriptor.priority);
    AddU16(descriptor.resolution);
    AddFloat(descriptor.capture_near_meters);
    AddFloat(descriptor.capture_far_meters);
    AddU32(descriptor.visibility_mask);
    AddByte(static_cast<std::uint8_t>(descriptor.update_mode));
    AddU64(descriptor.update_interval_simulation_ticks);
    AddBool(descriptor.include_dynamic_geometry);
  }

  void AddRequest(const ReflectionProbeUpdateRequest &request) noexcept {
    AddU64(request.probe_id);
    AddU64(request.content_revision);
    AddU64(request.candidate_generation);
    AddU64(request.simulation_tick);
    AddU64(request.deterministic_seed);
    AddU64(request.descriptor_fingerprint);
    AddDouble3(request.absolute_world_origin_meters);
    AddMatrix(request.render_from_probe);
    AddByte(static_cast<std::uint8_t>(request.reason));
    AddU16(request.resolution);
    AddU16(request.expected_mip_count);
    AddU32(request.expected_face_count);
    AddDescriptor(request.descriptor);
  }

  void AddMipMetadata(
      const ReflectionProbeCaptureMipMetadata &metadata) noexcept {
    AddU32(metadata.version);
    AddByte(static_cast<std::uint8_t>(metadata.contract));
    AddU32(metadata.face_count);
    AddU16(metadata.mip_count);
    for (std::size_t index = 0U;
         index < kReflectionProbeMaximumFilteredMipCount; ++index) {
      AddU32(metadata.widths[index]);
      AddU32(metadata.heights[index]);
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

std::uint64_t ComputeBoundReceiptDigest(
    std::uint64_t plan_id, std::size_t request_index,
    std::uint64_t native_execution_evidence,
    const ReflectionProbeCaptureMeasurementResult &measurement) noexcept {
  StableHasher hasher;
  hasher.AddU64(UINT64_C(0x524f525043525631));
  hasher.AddU64(plan_id);
  hasher.AddU64(static_cast<std::uint64_t>(request_index));
  hasher.AddU64(native_execution_evidence);
  hasher.AddByte(static_cast<std::uint8_t>(measurement.backend));
  hasher.AddByte(static_cast<std::uint8_t>(measurement.pixel_format));
  hasher.AddU64(measurement.canonical_capture_digest);
  hasher.AddU32(measurement.completed_face_count);
  hasher.AddU16(measurement.completed_mip_count);
  hasher.AddU64(measurement.canonical_payload_bytes);
  hasher.AddMipMetadata(measurement.mip_metadata);
  const std::uint64_t digest = hasher.value();
  return digest != 0U ? digest : UINT64_C(0x524f525043525631);
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

bool IsKnownReflectionProbeCaptureMipContract(
    ReflectionProbeCaptureMipContract contract) noexcept {
  return contract ==
         ReflectionProbeCaptureMipContract::OGRE_NEXT_PCC_FILTERED_IBL;
}

ReflectionProbeCaptureMipMetadata
ComputeReflectionProbeCaptureMipMetadata(std::uint16_t resolution) noexcept {
  ReflectionProbeCaptureMipMetadata metadata;
  metadata.face_count = kReflectionProbeCubemapFaceCount;
  metadata.mip_count = ComputeReflectionProbeRequiredMipCount(resolution);
  if (metadata.mip_count > kReflectionProbeMaximumFilteredMipCount) {
    metadata.mip_count = 0U;
    metadata.face_count = 0U;
    return metadata;
  }
  for (std::size_t index = 0U; index < metadata.mip_count; ++index) {
    const std::uint32_t dimension =
        (std::max)(1U, static_cast<std::uint32_t>(resolution) >> index);
    metadata.widths[index] = dimension;
    metadata.heights[index] = dimension;
  }
  return metadata;
}

bool AreReflectionProbeCaptureMipMetadataEquivalent(
    const ReflectionProbeCaptureMipMetadata &lhs,
    const ReflectionProbeCaptureMipMetadata &rhs) noexcept {
  return lhs.version == rhs.version && lhs.contract == rhs.contract &&
         lhs.face_count == rhs.face_count && lhs.mip_count == rhs.mip_count &&
         lhs.widths == rhs.widths && lhs.heights == rhs.heights;
}

ReflectionProbeCaptureReceipt ReflectionProbeCaptureReceipt::Failed(
    std::uint64_t plan_id, std::size_t request_index,
    const ReflectionProbeUpdateRequest &request) {
  return ReflectionProbeCaptureReceipt(FailureTag{}, plan_id, request_index,
                                       request);
}

ReflectionProbeCaptureReceipt::ReflectionProbeCaptureReceipt(
    FailureTag, std::uint64_t plan_id, std::size_t request_index,
    const ReflectionProbeUpdateRequest &request)
    : plan_id_(plan_id), request_index_(request_index), request_(request) {}

ReflectionProbeCaptureReceipt
ReflectionProbeCaptureReceipt::IssueFromConcreteAdapter(
    std::uint64_t plan_id, std::size_t request_index,
    const ReflectionProbeUpdateRequest &request,
    std::uint64_t native_execution_evidence,
    const ReflectionProbeCaptureMeasurementResult &measurement) {
  return ReflectionProbeCaptureReceipt(plan_id, request_index, request,
                                       native_execution_evidence, measurement);
}

ReflectionProbeCaptureReceipt::ReflectionProbeCaptureReceipt(
    std::uint64_t plan_id, std::size_t request_index,
    const ReflectionProbeUpdateRequest &request,
    std::uint64_t native_execution_evidence,
    const ReflectionProbeCaptureMeasurementResult &measurement)
    : plan_id_(plan_id), request_index_(request_index), request_(request),
      backend_(measurement.backend), pixel_format_(measurement.pixel_format),
      native_execution_evidence_(native_execution_evidence),
      completed_face_count_(measurement.completed_face_count),
      completed_mip_count_(measurement.completed_mip_count),
      canonical_payload_bytes_(measurement.canonical_payload_bytes),
      mip_metadata_(measurement.mip_metadata),
      successful_(measurement.ok()) {
  const ReflectionProbeCaptureMipMetadata expected_mip_metadata =
      ComputeReflectionProbeCaptureMipMetadata(request.resolution);
  adapter_authoritative_ =
      measurement.ok() && native_execution_evidence != 0U &&
      measurement.canonical_capture_digest != 0U &&
      IsKnownReflectionProbeCaptureBackend(measurement.backend) &&
      IsKnownReflectionProbeCapturePixelFormat(measurement.pixel_format) &&
      measurement.completed_face_count == request.expected_face_count &&
      measurement.completed_mip_count == request.expected_mip_count &&
      measurement.canonical_payload_bytes != 0U &&
      AreReflectionProbeCaptureMipMetadataEquivalent(
          measurement.mip_metadata, expected_mip_metadata) &&
      ValidateReflectionProbeUpdateRequest(request).ok() &&
      AreReflectionProbeUpdateRequestsEquivalent(request, measurement.request);
  if (adapter_authoritative_) {
    capture_digest_ = ComputeBoundReceiptDigest(
        plan_id, request_index, native_execution_evidence, measurement);
  }
}

ReflectionProbeCaptureMeasurementResult
ComputeReflectionProbeCaptureMeasurement(
    const ReflectionProbeUpdateRequest &request,
    ReflectionProbeCaptureBackend backend,
    ReflectionProbeCapturePixelFormat pixel_format,
    const std::vector<ReflectionProbeCapturedSubresourceView> &subresources) {
  ReflectionProbeCaptureMeasurementResult result;
  result.validation = ValidateReflectionProbeUpdateRequest(request);
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
  hasher.AddRequest(request);
  const ReflectionProbeCaptureMipMetadata expected_mip_metadata =
      ComputeReflectionProbeCaptureMipMetadata(request.resolution);
  if (!IsKnownReflectionProbeCaptureMipContract(
          expected_mip_metadata.contract) ||
      expected_mip_metadata.face_count != request.expected_face_count ||
      expected_mip_metadata.mip_count != request.expected_mip_count) {
    result.validation = Failure(
        ValidationCode::INVALID_DIMENSIONS, "request.capture_shape",
        "capture request has no exact supported PCC filtered-IBL mip layout");
    return result;
  }
  hasher.AddMipMetadata(expected_mip_metadata);

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

  result.canonical_capture_digest = hasher.value();
  if (result.canonical_capture_digest == 0U) {
    result.validation = Failure(ValidationCode::EMPTY_PAYLOAD,
                                "capture_digest",
                                "canonical capture digest must be nonzero");
    return result;
  }
  result.completed_face_count = request.expected_face_count;
  result.completed_mip_count = request.expected_mip_count;
  result.canonical_payload_bytes = canonical_payload_bytes;
  result.mip_metadata = expected_mip_metadata;
  result.request = request;
  result.backend = backend;
  result.pixel_format = pixel_format;
  result.validation = ValidationResult::Success();
  return result;
}

} // namespace RoR::Render
