/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Renderer-neutral contract for explicit native directional shadows.

#pragma once

#include "../RenderValidation.h"
#include "RasterFeatureTier.h"

#include <array>
#include <cstdint>

namespace RoR::Render {

constexpr std::uint32_t kNativeDirectionalShadowContractVersion = 1U;
constexpr std::uint32_t kNativeDirectionalShadowRequiredBlasCount = 2U;
constexpr std::uint32_t kNativeDirectionalShadowRequiredTlasInstanceCount =
    2U;
constexpr std::uint32_t kNativeDirectionalShadowRequiredPrimaryRayCount = 1U;
constexpr std::uint32_t
    kNativeDirectionalShadowRequiredVisibilityRayCount = 1U;
constexpr std::uint16_t kNativeDirectionalShadowOccludedR16 = 0x0000U;
constexpr std::uint16_t kNativeDirectionalShadowVisibleR16 = 0x3c00U;

/// Native API family which may execute the portable V1 contract. No native
/// handles cross this boundary; each platform adapter must independently
/// attest the same semantics before it may select the native path.
enum class NativeDirectionalShadowBackend : std::uint8_t {
  INVALID = 0,
  METAL = 1,
  VULKAN_KHR = 2,
  DIRECT3D12_DXR = 3,
};

/// V1 keeps the reviewed Ogre-Next PSSM implementation as the portable
/// fallback. Native admission is explicit and cannot silently replace PSSM
/// from a partially reported capability set.
enum class NativeDirectionalShadowTier : std::uint8_t {
  PORTABLE_PSSM_FALLBACK_V1 = 0,
  NATIVE_DIRECTIONAL_HARD_SHADOW_V1 = 1,
};

/// INVALID is deliberately zero so a default-constructed native result is
/// never interpreted as a valid occluded sample.
enum class NativeDirectionalShadowVisibility : std::uint8_t {
  INVALID = 0,
  VISIBLE = 1,
  OCCLUDED = 2,
};

/// Cross-platform capability facts required before native V1 may be selected.
/// An API name or generic ray-tracing bit alone is insufficient evidence.
struct NativeDirectionalShadowCapabilities final {
  std::uint32_t version = kNativeDirectionalShadowContractVersion;
  NativeDirectionalShadowBackend backend =
      NativeDirectionalShadowBackend::INVALID;
  bool hardware_ray_tracing = false;
  bool same_device_raster_and_ray_queue = false;
  bool two_level_acceleration_structures = false;
  bool primary_camera_rays = false;
  bool secondary_directional_visibility_rays = false;
  bool r16_float_visibility = false;
  bool rgba16_float_hybrid_composite = false;
};

[[nodiscard]] bool IsKnownNativeDirectionalShadowBackend(
    NativeDirectionalShadowBackend backend) noexcept;

/// Allocation-free, fail-closed native admission. False means the caller must
/// retain the separately validated PSSM V1 path; it does not imply that PSSM
/// capability validation itself has succeeded.
[[nodiscard]] bool HasAttestedNativeDirectionalShadowCapabilities(
    const NativeDirectionalShadowCapabilities &capabilities) noexcept;

[[nodiscard]] NativeDirectionalShadowTier ResolveNativeDirectionalShadowTier(
    bool native_requested,
    const NativeDirectionalShadowCapabilities &capabilities) noexcept;

/// One tightly packed linear RGBA16_FLOAT sample represented by its exact
/// binary16 channel encodings. RGB must be canonical, finite, and nonnegative;
/// straight alpha must additionally remain in [0, 1].
struct NativeDirectionalShadowRgba16Pixel final {
  std::array<std::uint16_t, 4U> channels{};
};

/// Independently reproducible result for one hard-shadow proof sample.
/// Visible preserves every raster channel bit. Occluded replaces RGB with
/// canonical positive zero while preserving the exact raster alpha bits.
struct NativeDirectionalShadowSampleOracle final {
  std::uint32_t version = kNativeDirectionalShadowContractVersion;
  NativeDirectionalShadowVisibility visibility =
      NativeDirectionalShadowVisibility::INVALID;
  std::uint16_t visibility_r16_bits = 0xffffU;
  NativeDirectionalShadowRgba16Pixel hybrid_rgba16;
};

/// Builds the V1 CPU oracle transactionally. `output` is unchanged on error.
[[nodiscard]] ValidationResult TryBuildNativeDirectionalShadowSampleOracle(
    NativeDirectionalShadowVisibility visibility,
    const NativeDirectionalShadowRgba16Pixel &raster_rgba16,
    NativeDirectionalShadowSampleOracle &output);

/// Complete evidence for the first native semantic checkpoint. It deliberately
/// describes a one-sample scene with a receiver and a distinct occluder: two
/// BLAS, two TLAS instances, one primary camera ray, and one secondary ray
/// toward the directional light. Future full-frame implementations may apply
/// the same sample oracle independently to every admitted pixel.
struct NativeDirectionalShadowPassContract final {
  std::uint32_t version = kNativeDirectionalShadowContractVersion;
  NativeDirectionalShadowTier tier =
      NativeDirectionalShadowTier::PORTABLE_PSSM_FALLBACK_V1;
  OgreNextRasterFeatureTier raster_feature_tier =
      OgreNextRasterFeatureTier::STATIC_PBR_N1;
  NativeDirectionalShadowCapabilities capabilities;

  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  std::uint64_t receiver_instance_id = 0U;
  std::uint64_t occluder_instance_id = 0U;

  std::uint32_t blas_count = 0U;
  std::uint32_t tlas_instance_count = 0U;
  bool receiver_blas_built = false;
  bool occluder_blas_built = false;
  bool tlas_built = false;

  std::uint32_t primary_camera_ray_count = 0U;
  std::uint32_t secondary_visibility_ray_count = 0U;
  std::uint64_t primary_hit_instance_id = 0U;
  /// Zero is the canonical miss sentinel for VISIBLE. OCCLUDED must name the
  /// exact occluder instance selected above.
  std::uint64_t secondary_blocker_instance_id = 0U;
  bool primary_camera_ray_geometry_exact = false;
  bool secondary_directional_ray_geometry_exact = false;

  bool native_submission_completed = false;
  bool raster_source_ui_free = false;
  bool visibility_readback_completed = false;
  bool hybrid_readback_completed = false;
  NativeDirectionalShadowVisibility visibility =
      NativeDirectionalShadowVisibility::INVALID;
  std::uint16_t native_visibility_r16_bits = 0xffffU;
  NativeDirectionalShadowRgba16Pixel raster_rgba16;
  NativeDirectionalShadowRgba16Pixel native_hybrid_rgba16;
};

/// Validates exact native evidence against the portable CPU oracle. A failed
/// native pass is an error, not permission to switch to PSSM mid-frame.
[[nodiscard]] ValidationResult ValidateNativeDirectionalShadowPassContract(
    const NativeDirectionalShadowPassContract &contract);

} // namespace RoR::Render
