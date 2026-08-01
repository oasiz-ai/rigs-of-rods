/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Fail-closed policy for the first isolated Ogre-Next frontend.

#pragma once

#include "../RendererFrontend.h"

#include <map>
#include <memory>

namespace RoR::Render {

// Before a render device exists, N1 advertises only a conservative extent.
// The concrete frontend replaces this with the device-reported 2D limit after
// initialization and validates the requested offscreen extent against it.
constexpr std::uint32_t kOgreNextN1ConservativeMaximumTextureDimension = 2048U;
constexpr std::size_t kOgreNextN1MaximumDirectionalLights = 0U;

/// Bounds after the portable descriptor has been reduced with overflow-safe
/// float arithmetic into Ogre's center/half-size representation.
struct OgreNextN1NativeMeshBounds final {
  Float3 center;
  Float3 half_size;
  float radius = 0.0F;
};

/// Returns false when finite portable bounds would manufacture a non-finite
/// Ogre Aabb or bounding-sphere value during native float arithmetic.
[[nodiscard]] bool TryBuildOgreNextN1NativeMeshBounds(
    const Bounds3 &portable,
    OgreNextN1NativeMeshBounds &native) noexcept;

/// Returns false when Ogre's float TRS/Aabb evaluation could overflow while
/// composing an otherwise valid local bound with an otherwise valid TRS.
[[nodiscard]] bool CanRepresentOgreNextN1WorldBounds(
    const Bounds3 &local_bounds,
    const Matrix4x4 &render_from_object) noexcept;

/// Lifetime identity state for N1's synchronous one-frame adapter. It retains
/// weak ownership identities only while their caller-owned snapshots remain
/// alive. N1 additionally requires contiguous frame IDs beginning at one, so
/// completion is represented by one high-water mark instead of per-frame data.
class OgreNextN1SubmissionState final {
public:
  [[nodiscard]] RenderOperationResult
  Validate(const RenderFrameRequest &request) const;
  void Commit(const RenderFrameRequest &request);
  [[nodiscard]] bool IsFrameComplete(std::uint64_t frame_id) const noexcept;
  [[nodiscard]] std::size_t TrackedSnapshotIdentityCount() const noexcept;
  void Reset() noexcept;

private:
  std::map<std::uint64_t, std::weak_ptr<const SceneSnapshot>> snapshots_;
  std::uint64_t last_frame_id_ = 0U;
  std::uint64_t last_snapshot_id_ = 0U;
};

/// Converts the renderer-boundary right-handed [0, 1] depth projection into
/// Ogre's canonical right-handed [-1, 1] clip convention. The native render
/// system then performs its one normal API-specific projection conversion.
[[nodiscard]] bool TryConvertPortableProjectionToOgreClip(
    const Matrix4x4 &portable, Matrix4x4 &converted) noexcept;

/// The N1 adapter deliberately reports only what its shipping code path has
/// proved: one offscreen colour view, static v2 geometry, and synchronous CPU
/// readback. A concrete native API is not exported by this milestone.
[[nodiscard]] FrontendCapabilityReport
BuildOgreNextN1CapabilityReport(RasterGraphicsApi raster_api,
                                const char *frontend_version);

[[nodiscard]] ValidationResult ValidateOgreNextN1Initialization(
    const FrontendInitializationRequest &request,
    const FrontendCapabilityReport &capabilities);
[[nodiscard]] ValidationResult
ValidateOgreNextN1AssetCatalog(const RenderAssetRegistry &registry);
[[nodiscard]] ValidationResult ValidateOgreNextN1Scene(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry);
[[nodiscard]] ValidationResult ValidateOgreNextN1Frame(
    const RenderFrameRequest &request,
    const FrontendCapabilityReport &capabilities,
    const RenderAssetRegistry &registry);

[[nodiscard]] RenderOperationResult
OgreNextN1OperationFromValidation(const ValidationResult &validation);

} // namespace RoR::Render
