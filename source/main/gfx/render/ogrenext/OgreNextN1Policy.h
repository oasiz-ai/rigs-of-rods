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

#include <deque>
#include <memory>

namespace RoR::Render {

// Before a render device exists, N1 advertises only a conservative extent.
// The concrete frontend replaces this with the device-reported 2D limit after
// initialization and validates the requested offscreen extent against it.
constexpr std::uint32_t kOgreNextN1ConservativeMaximumTextureDimension = 2048U;
constexpr std::size_t kOgreNextN1MaximumDirectionalLights = 0U;
constexpr std::size_t kOgreNextN1CompletedFrameHistoryLimit = 64U;

/// Bounded lifetime identity state for N1's synchronous one-frame adapter.
/// Only the latest snapshot may be replayed; this avoids an unbounded pointer
/// identity cache while preserving the producer's monotonic-ID contract.
class OgreNextN1SubmissionState final {
public:
  [[nodiscard]] RenderOperationResult
  Validate(const RenderFrameRequest &request) const;
  void Commit(const RenderFrameRequest &request);
  [[nodiscard]] bool IsFrameComplete(std::uint64_t frame_id) const noexcept;
  void Reset() noexcept;

private:
  std::deque<std::uint64_t> completed_frames_;
  std::shared_ptr<const SceneSnapshot> last_snapshot_;
  std::uint64_t last_frame_id_ = 0U;
  std::uint64_t last_snapshot_id_ = 0U;
};

/// Converts the renderer-boundary right-handed [0, 1] depth projection into
/// Ogre's canonical right-handed [-1, 1] clip convention. The native render
/// system then performs its one normal API-specific projection conversion.
[[nodiscard]] Matrix4x4
ConvertPortableProjectionToOgreClip(const Matrix4x4 &portable) noexcept;

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
