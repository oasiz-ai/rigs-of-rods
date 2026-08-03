/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Canonical, renderer-neutral reflection-probe capture receipts.

#pragma once

#include "ReflectionProbeRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace RoR::Render {

class OgreNextReflectionProbeRuntime;

namespace Testing {
class ReflectionProbeCaptureTestAdapter;
}

constexpr std::uint32_t kReflectionProbeCaptureReceiptVersion = 1U;
constexpr std::size_t kReflectionProbeMaximumFilteredMipCount = 8U;

/// The native API that actually produced the cubemap. These values describe
/// execution, not the presentation API selected by a host window.
enum class ReflectionProbeCaptureBackend : std::uint8_t {
  OGRE_NEXT_METAL = 1,
  OGRE_NEXT_VULKAN = 2,
  OGRE_NEXT_D3D11 = 3,
  OGRE_NEXT_D3D12 = 4,
};

enum class ReflectionProbeCapturePixelFormat : std::uint8_t {
  RGBA16_FLOAT = 1,
};

/// The native texture whose bytes are attested by a receipt. Ogre-Next's PCC
/// output is a prefiltered IBL cubemap, not a full raw cubemap mip chain.
enum class ReflectionProbeCaptureMipContract : std::uint8_t {
  OGRE_NEXT_PCC_FILTERED_IBL = 1,
};

/// Exact native output layout bound into the measurement and opaque receipt.
/// Entries [0, mip_count) are contiguous native mip levels; unused entries
/// are zero. The reviewed V1 descriptor range (32..2048) produces 2..8 mips.
struct ReflectionProbeCaptureMipMetadata {
  std::uint32_t version = kReflectionProbeCaptureReceiptVersion;
  ReflectionProbeCaptureMipContract contract =
      ReflectionProbeCaptureMipContract::OGRE_NEXT_PCC_FILTERED_IBL;
  std::uint32_t face_count = 0U;
  std::uint16_t mip_count = 0U;
  std::array<std::uint32_t, kReflectionProbeMaximumFilteredMipCount> widths{};
  std::array<std::uint32_t, kReflectionProbeMaximumFilteredMipCount> heights{};
};

/// Borrowed one-face/one-mip readback. Rows may be padded; only active texel
/// bytes are committed to the digest. The caller retains ownership for the
/// duration of ComputeReflectionProbeCaptureMeasurement().
struct ReflectionProbeCapturedSubresourceView {
  std::uint32_t version = kReflectionProbeCaptureReceiptVersion;
  std::uint32_t face_index = 0U;
  std::uint16_t mip_level = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t row_pitch_bytes = 0U;
  const std::uint8_t *bytes = nullptr;
  std::size_t byte_count = 0U;
};

/// Canonical CPU measurement of readback bytes. This is deliberately not a
/// receipt and cannot be submitted to ReflectionProbeUpdateScheduler::Commit.
/// A reviewed concrete native adapter must bind this measurement to its actual
/// execution before it can issue an authoritative receipt.
struct ReflectionProbeCaptureMeasurementResult {
  ValidationResult validation;
  std::uint64_t canonical_capture_digest = 0U;
  std::uint32_t completed_face_count = 0U;
  std::uint16_t completed_mip_count = 0U;
  std::uint64_t canonical_payload_bytes = 0U;
  ReflectionProbeCaptureMipMetadata mip_metadata;
  ReflectionProbeUpdateRequest request;
  ReflectionProbeCaptureBackend backend =
      ReflectionProbeCaptureBackend::OGRE_NEXT_METAL;
  ReflectionProbeCapturePixelFormat pixel_format =
      ReflectionProbeCapturePixelFormat::RGBA16_FLOAT;

  [[nodiscard]] bool ok() const noexcept { return validation.ok(); }
  explicit operator bool() const noexcept { return ok(); }
};

/// Opaque completion authority bound to one exact scheduler plan and request.
/// Successful receipts have no public constructor: only a reviewed concrete
/// capture adapter can issue one after native execution and canonical readback.
/// The repository currently provides only a test adapter outside shipping
/// sources, so portable measurements alone are never authoritative.
class ReflectionProbeCaptureReceipt final {
public:
  ReflectionProbeCaptureReceipt(const ReflectionProbeCaptureReceipt &) =
      default;
  ReflectionProbeCaptureReceipt &
  operator=(const ReflectionProbeCaptureReceipt &) = default;
  ReflectionProbeCaptureReceipt(ReflectionProbeCaptureReceipt &&) noexcept =
      default;
  ReflectionProbeCaptureReceipt &
  operator=(ReflectionProbeCaptureReceipt &&) noexcept = default;

  /// Reports a failed capture without granting any publication authority.
  [[nodiscard]] static ReflectionProbeCaptureReceipt Failed(
      std::uint64_t plan_id, std::size_t request_index,
      const ReflectionProbeUpdateRequest &request);

  [[nodiscard]] bool successful() const noexcept { return successful_; }
  [[nodiscard]] bool authoritative() const noexcept {
    return adapter_authoritative_;
  }
  [[nodiscard]] std::uint64_t plan_id() const noexcept { return plan_id_; }
  [[nodiscard]] std::size_t request_index() const noexcept {
    return request_index_;
  }
  [[nodiscard]] std::uint64_t capture_digest() const noexcept {
    return capture_digest_;
  }
  [[nodiscard]] std::uint32_t completed_face_count() const noexcept {
    return completed_face_count_;
  }
  [[nodiscard]] std::uint16_t completed_mip_count() const noexcept {
    return completed_mip_count_;
  }
  [[nodiscard]] std::uint64_t canonical_payload_bytes() const noexcept {
    return canonical_payload_bytes_;
  }
  [[nodiscard]] const ReflectionProbeCaptureMipMetadata &
  mip_metadata() const noexcept {
    return mip_metadata_;
  }

private:
  struct FailureTag final {};

  ReflectionProbeCaptureReceipt(FailureTag, std::uint64_t plan_id,
                                std::size_t request_index,
                                const ReflectionProbeUpdateRequest &request);
  [[nodiscard]] static ReflectionProbeCaptureReceipt IssueFromConcreteAdapter(
      std::uint64_t plan_id, std::size_t request_index,
      const ReflectionProbeUpdateRequest &request,
      std::uint64_t native_execution_evidence,
      const ReflectionProbeCaptureMeasurementResult &measurement);
  ReflectionProbeCaptureReceipt(
      std::uint64_t plan_id, std::size_t request_index,
      const ReflectionProbeUpdateRequest &request,
      std::uint64_t native_execution_evidence,
      const ReflectionProbeCaptureMeasurementResult &measurement);

  std::uint32_t version_ = kReflectionProbeCaptureReceiptVersion;
  std::uint64_t plan_id_ = 0U;
  std::size_t request_index_ = 0U;
  ReflectionProbeUpdateRequest request_;
  ReflectionProbeCaptureBackend backend_ =
      ReflectionProbeCaptureBackend::OGRE_NEXT_METAL;
  ReflectionProbeCapturePixelFormat pixel_format_ =
      ReflectionProbeCapturePixelFormat::RGBA16_FLOAT;
  std::uint64_t native_execution_evidence_ = 0U;
  std::uint64_t capture_digest_ = 0U;
  std::uint32_t completed_face_count_ = 0U;
  std::uint16_t completed_mip_count_ = 0U;
  std::uint64_t canonical_payload_bytes_ = 0U;
  ReflectionProbeCaptureMipMetadata mip_metadata_;
  bool successful_ = false;
  bool adapter_authoritative_ = false;

  friend class OgreNextReflectionProbeRuntime;
  friend class ReflectionProbeUpdateScheduler;
  friend class Testing::ReflectionProbeCaptureTestAdapter;
};

[[nodiscard]] bool IsKnownReflectionProbeCaptureBackend(
    ReflectionProbeCaptureBackend backend) noexcept;
[[nodiscard]] bool IsKnownReflectionProbeCapturePixelFormat(
    ReflectionProbeCapturePixelFormat format) noexcept;
[[nodiscard]] bool IsKnownReflectionProbeCaptureMipContract(
    ReflectionProbeCaptureMipContract contract) noexcept;
[[nodiscard]] ReflectionProbeCaptureMipMetadata
ComputeReflectionProbeCaptureMipMetadata(std::uint16_t resolution) noexcept;
[[nodiscard]] bool AreReflectionProbeCaptureMipMetadataEquivalent(
    const ReflectionProbeCaptureMipMetadata &lhs,
    const ReflectionProbeCaptureMipMetadata &rhs) noexcept;

/// Measures one complete canonical readback against its immutable scheduler
/// request. Subresources must be ordered mip-major then face-major and contain
/// exactly all six faces for the recomputed Ogre-Next PCC filtered-IBL output
/// mip layout. This function does not attest native execution and its result
/// cannot authorize Commit().
/// Failure is transactional and returns a zero digest/counts.
[[nodiscard]] ReflectionProbeCaptureMeasurementResult
ComputeReflectionProbeCaptureMeasurement(
    const ReflectionProbeUpdateRequest &request,
    ReflectionProbeCaptureBackend backend,
    ReflectionProbeCapturePixelFormat pixel_format,
    const std::vector<ReflectionProbeCapturedSubresourceView> &subresources);

} // namespace RoR::Render
