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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kReflectionProbeCaptureReceiptVersion = 1U;

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

/// Borrowed one-face/one-mip readback. Rows may be padded; only active texel
/// bytes are committed to the digest. The caller retains ownership for the
/// duration of ComputeReflectionProbeCaptureReceipt().
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

struct ReflectionProbeCaptureReceiptResult {
  ValidationResult validation;
  std::uint64_t capture_digest = 0U;
  std::uint32_t completed_face_count = 0U;
  std::uint16_t completed_mip_count = 0U;
  std::uint64_t canonical_payload_bytes = 0U;

  [[nodiscard]] bool ok() const noexcept { return validation.ok(); }
  explicit operator bool() const noexcept { return ok(); }
};

[[nodiscard]] bool IsKnownReflectionProbeCaptureBackend(
    ReflectionProbeCaptureBackend backend) noexcept;
[[nodiscard]] bool IsKnownReflectionProbeCapturePixelFormat(
    ReflectionProbeCapturePixelFormat format) noexcept;

/// Authenticates one complete native capture against its immutable scheduler
/// request. Subresources must be ordered mip-major then face-major and contain
/// exactly all six faces for every required mip. A nonzero backend receipt must
/// independently bind the native IBL/filter execution. Failure is
/// transactional and returns a zero digest/counts.
[[nodiscard]] ReflectionProbeCaptureReceiptResult
ComputeReflectionProbeCaptureReceipt(
    const ReflectionProbeUpdateRequest &request,
    ReflectionProbeCaptureBackend backend,
    ReflectionProbeCapturePixelFormat pixel_format,
    std::uint64_t native_execution_receipt,
    const std::vector<ReflectionProbeCapturedSubresourceView> &subresources);

} // namespace RoR::Render
