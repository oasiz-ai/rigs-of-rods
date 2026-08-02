/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral contract for explicit native directional shadows.

#include "NativeDirectionalShadowContract.h"

#include <algorithm>
#include <cstddef>

namespace RoR::Render {
namespace {

constexpr std::uint16_t kHalfSignMask = 0x8000U;
constexpr std::uint16_t kHalfExponentMask = 0x7c00U;

ValidationResult Invalid(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

bool IsFiniteNonnegativeBinary16(std::uint16_t bits) noexcept {
  return (bits & kHalfSignMask) == 0U &&
         (bits & kHalfExponentMask) != kHalfExponentMask;
}

ValidationResult ValidateRasterPixel(
    const NativeDirectionalShadowRgba16Pixel &pixel) {
  for (std::size_t channel = 0U; channel < pixel.channels.size(); ++channel) {
    if (!IsFiniteNonnegativeBinary16(pixel.channels[channel])) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "raster_rgba16.channels",
          "native shadow raster channels must be canonical finite nonnegative binary16 values",
          channel);
    }
  }
  if (pixel.channels[3U] > kNativeDirectionalShadowVisibleR16) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "raster_rgba16.alpha",
        "native shadow raster alpha must remain inside the straight-alpha [0, 1] envelope",
        3U);
  }
  return ValidationResult::Success();
}

bool PixelsEqual(const NativeDirectionalShadowRgba16Pixel &lhs,
                 const NativeDirectionalShadowRgba16Pixel &rhs) noexcept {
  return std::equal(lhs.channels.begin(), lhs.channels.end(),
                    rhs.channels.begin());
}

} // namespace

bool IsKnownNativeDirectionalShadowBackend(
    NativeDirectionalShadowBackend backend) noexcept {
  switch (backend) {
  case NativeDirectionalShadowBackend::METAL:
  case NativeDirectionalShadowBackend::VULKAN_KHR:
  case NativeDirectionalShadowBackend::DIRECT3D12_DXR:
    return true;
  case NativeDirectionalShadowBackend::INVALID:
    return false;
  }
  return false;
}

bool HasAttestedNativeDirectionalShadowCapabilities(
    const NativeDirectionalShadowCapabilities &capabilities) noexcept {
  return capabilities.version == kNativeDirectionalShadowContractVersion &&
         IsKnownNativeDirectionalShadowBackend(capabilities.backend) &&
         capabilities.hardware_ray_tracing &&
         capabilities.same_device_raster_and_ray_queue &&
         capabilities.two_level_acceleration_structures &&
         capabilities.primary_camera_rays &&
         capabilities.secondary_directional_visibility_rays &&
         capabilities.r16_float_visibility &&
         capabilities.rgba16_float_hybrid_composite;
}

NativeDirectionalShadowTier ResolveNativeDirectionalShadowTier(
    bool native_requested,
    const NativeDirectionalShadowCapabilities &capabilities) noexcept {
  return native_requested &&
                 HasAttestedNativeDirectionalShadowCapabilities(capabilities)
             ? NativeDirectionalShadowTier::
                   NATIVE_DIRECTIONAL_HARD_SHADOW_V1
             : NativeDirectionalShadowTier::PORTABLE_PSSM_FALLBACK_V1;
}

ValidationResult TryBuildNativeDirectionalShadowSampleOracle(
    NativeDirectionalShadowVisibility visibility,
    const NativeDirectionalShadowRgba16Pixel &raster_rgba16,
    NativeDirectionalShadowSampleOracle &output) {
  if (visibility != NativeDirectionalShadowVisibility::VISIBLE &&
      visibility != NativeDirectionalShadowVisibility::OCCLUDED) {
    return Invalid(ValidationCode::INVALID_ENUM, "visibility",
                   "native directional shadow visibility must be explicitly visible or occluded");
  }
  const ValidationResult raster_validation =
      ValidateRasterPixel(raster_rgba16);
  if (!raster_validation) {
    return raster_validation;
  }

  NativeDirectionalShadowSampleOracle candidate;
  candidate.visibility = visibility;
  candidate.visibility_r16_bits =
      visibility == NativeDirectionalShadowVisibility::VISIBLE
          ? kNativeDirectionalShadowVisibleR16
          : kNativeDirectionalShadowOccludedR16;
  candidate.hybrid_rgba16 = raster_rgba16;
  if (visibility == NativeDirectionalShadowVisibility::OCCLUDED) {
    candidate.hybrid_rgba16.channels[0U] = 0U;
    candidate.hybrid_rgba16.channels[1U] = 0U;
    candidate.hybrid_rgba16.channels[2U] = 0U;
  }
  output = candidate;
  return ValidationResult::Success();
}

ValidationResult ValidateNativeDirectionalShadowPassContract(
    const NativeDirectionalShadowPassContract &contract) {
  if (contract.version != kNativeDirectionalShadowContractVersion) {
    return Invalid(
        ValidationCode::UNSUPPORTED_VERSION, "contract.version",
        "unsupported native directional shadow pass-contract version");
  }
  if (contract.tier != NativeDirectionalShadowTier::
                           NATIVE_DIRECTIONAL_HARD_SHADOW_V1) {
    return Invalid(
        ValidationCode::UNSUPPORTED_FEATURE, "contract.tier",
        "native directional shadow evidence requires the explicit native V1 tier");
  }
  if (contract.raster_feature_tier !=
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1) {
    return Invalid(
        ValidationCode::UNSUPPORTED_FEATURE,
        "contract.raster_feature_tier",
        "native directional hard shadows require the reviewed RT4/V1 raster tier");
  }
  if (!HasAttestedNativeDirectionalShadowCapabilities(
          contract.capabilities)) {
    return Invalid(
        ValidationCode::UNSUPPORTED_FEATURE, "contract.capabilities",
        "native directional shadow capabilities are incomplete or unrecognized");
  }
  if (contract.frame_id == 0U || contract.snapshot_id == 0U ||
      contract.view_id == 0U) {
    return Invalid(
        ValidationCode::INVALID_IDENTIFIER, "contract.lineage",
        "native directional shadow frame, snapshot, and view identifiers must be nonzero");
  }
  if (contract.receiver_instance_id == 0U ||
      contract.occluder_instance_id == 0U ||
      contract.receiver_instance_id == contract.occluder_instance_id) {
    return Invalid(
        ValidationCode::INVALID_IDENTIFIER, "contract.instance_id",
        "native directional shadow receiver and occluder identifiers must be nonzero and distinct");
  }
  if (contract.blas_count != kNativeDirectionalShadowRequiredBlasCount ||
      contract.tlas_instance_count !=
          kNativeDirectionalShadowRequiredTlasInstanceCount ||
      !contract.receiver_blas_built || !contract.occluder_blas_built ||
      !contract.tlas_built) {
    return Invalid(
        ValidationCode::SIZE_MISMATCH, "contract.acceleration_structure",
        "native directional shadow V1 requires exactly two built BLAS and two TLAS instances");
  }
  if (contract.primary_camera_ray_count !=
          kNativeDirectionalShadowRequiredPrimaryRayCount ||
      contract.secondary_visibility_ray_count !=
          kNativeDirectionalShadowRequiredVisibilityRayCount ||
      contract.primary_hit_instance_id != contract.receiver_instance_id ||
      !contract.primary_camera_ray_geometry_exact ||
      !contract.secondary_directional_ray_geometry_exact) {
    return Invalid(
        ValidationCode::REVISION_MISMATCH, "contract.ray_lineage",
        "native directional shadow V1 requires one exact camera ray hitting the receiver and one exact directional visibility ray");
  }
  const std::uint64_t expected_blocker =
      contract.visibility == NativeDirectionalShadowVisibility::OCCLUDED
          ? contract.occluder_instance_id
          : 0U;
  if (contract.secondary_blocker_instance_id != expected_blocker) {
    return Invalid(
        ValidationCode::REVISION_MISMATCH,
        "contract.secondary_blocker_instance_id",
        "native visibility and the secondary-ray blocker identity disagree");
  }
  if (!contract.native_submission_completed ||
      !contract.raster_source_ui_free ||
      !contract.visibility_readback_completed ||
      !contract.hybrid_readback_completed) {
    return Invalid(
        ValidationCode::SEQUENCE_MISMATCH, "contract.readback",
        "native directional shadow submission and UI-free readbacks must complete before validation");
  }

  NativeDirectionalShadowSampleOracle oracle;
  const ValidationResult oracle_validation =
      TryBuildNativeDirectionalShadowSampleOracle(
          contract.visibility, contract.raster_rgba16, oracle);
  if (!oracle_validation) {
    return oracle_validation;
  }
  if (contract.native_visibility_r16_bits !=
      oracle.visibility_r16_bits) {
    return Invalid(
        ValidationCode::REVISION_MISMATCH,
        "contract.native_visibility_r16_bits",
        "native visibility readback differs from the exact R16_FLOAT oracle");
  }
  if (!PixelsEqual(contract.native_hybrid_rgba16,
                   oracle.hybrid_rgba16)) {
    return Invalid(
        ValidationCode::REVISION_MISMATCH,
        "contract.native_hybrid_rgba16",
        "native hybrid readback differs from the byte-exact RGBA16_FLOAT oracle");
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
