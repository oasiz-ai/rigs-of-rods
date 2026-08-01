/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Isolated Ogre-Next N1 static-PBR offscreen frontend.

#pragma once

#include "../RendererFrontend.h"
#include "OgreNextPssmShadowPolicy.h"
#include "RasterFeatureTier.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR::Render {

enum class OgreNextNativeFeatureTier : std::uint8_t;

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
/// Isolated native-smoke fault seam; never compiled into the production RoR
/// target. Each value injects one failure after the named Ogre allocation step.
enum class OgreNextN1TextureUploadFailureStage : std::uint8_t {
  NONE = 0,
  AFTER_CREATE,
  AFTER_SET_RESOLUTION,
  AFTER_SET_MIPMAPS,
  AFTER_SET_PIXEL_FORMAT,
  AFTER_SCHEDULE_TRANSITION,
};

/// PSSM-only transactional fault seam for the standalone native smoke.
enum class OgreNextN1PssmFailureStage : std::uint8_t {
  NONE = 0,
  AFTER_D32_ATLAS_CREATE,
  DURING_D32_ATLAS_CLEANUP_LOOKUP,
  AFTER_RECEIVER_DATABLOCK_CLONE,
  AFTER_WORKSPACE_NODE_DEFINITION,
  DURING_RECEIVER_DATABLOCK_CLEANUP_LOOKUP,
  DURING_WORKSPACE_DEFINITION_CLEANUP_LOOKUP,
  DURING_WORKSPACE_NODE_CLEANUP_LOOKUP,
  DURING_SHADOW_NODE_CLEANUP_LOOKUP,
  DURING_TARGET_TEXTURE_CLEANUP_LOOKUP,
};
#endif

/// Runtime-owned Ogre shader media. The root is an absolute UTF-8 path containing
/// the pinned `Hlms` directory; packaging code resolves its own relative
/// resource layout before constructing the frontend.
struct OgreNextN1Configuration final {
  std::string shader_media_root;
  OgreNextRasterFeatureTier raster_feature_tier =
      OgreNextRasterFeatureTier::STATIC_PBR_N1;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  OgreNextN1TextureUploadFailureStage texture_upload_failure_stage =
      OgreNextN1TextureUploadFailureStage::NONE;
#endif
  // Kept after the optional fault-injection seam so the existing standalone
  // test aggregate remains source-compatible. Production callers should set
  // this field by name after constructing the configuration.
  OgreNextDirectionalShadowMode directional_shadow_mode =
      OgreNextDirectionalShadowMode::DISABLED;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  OgreNextN1PssmFailureStage pssm_failure_stage =
      OgreNextN1PssmFailureStage::NONE;
#endif
};

struct OgreNextPssmNativeAabb final {
  Float3 minimum;
  Float3 maximum;
};

/// One direct native bounds observation for a PSSM frame item. The expected
/// values are retained beside Ogre's Mesh and Item readbacks so an evidence
/// consumer can reject a report that mutates either side of the comparison.
struct OgreNextPssmNativeBoundsObservation final {
  std::uint64_t instance_id = 0U;
  bool casts_shadow = false;
  bool receives_shadow = false;
  OgreNextPssmNativeAabb expected_local;
  OgreNextPssmNativeAabb ogre_mesh_local;
  OgreNextPssmNativeAabb ogre_item_local;
  OgreNextPssmNativeAabb expected_world;
  OgreNextPssmNativeAabb ogre_item_world;
};

struct OgreNextPssmShadowRuntimeAudit final {
  std::uint32_t version = kOgreNextPssmShadowContractVersion;
  OgreNextDirectionalShadowMode configured_mode =
      OgreNextDirectionalShadowMode::DISABLED;
  std::uint64_t shadow_frames_completed = 0U;
  std::uint64_t shadow_node_creates = 0U;
  std::uint64_t shadow_node_destroys = 0U;
  std::uint64_t workspace_node_definition_creates = 0U;
  std::uint64_t workspace_node_definition_destroys = 0U;
  std::uint64_t receiver_datablock_creates = 0U;
  std::uint64_t receiver_datablock_destroys = 0U;
  bool capability_check_completed = false;
  std::uint32_t observed_maximum_texture_dimension = 0U;
  bool atlas_dimensions_supported = false;
  bool texture_gather_supported = false;
  bool d32_probe_attempted = false;
  bool d32_render_target_supported = false;
  bool d32_atlas_allocation_verified = false;
  bool d32_atlas_readback_verified = false;
  bool d32_atlas_cleanup_verified = false;
  std::uint64_t d32_atlas_cleanup_absence_checks = 0U;
  std::uint64_t workspace_definition_cleanup_absence_checks = 0U;
  std::uint64_t workspace_node_cleanup_absence_checks = 0U;
  std::uint64_t shadow_node_cleanup_absence_checks = 0U;
  std::uint64_t receiver_datablock_cleanup_absence_checks = 0U;
  std::uint64_t target_texture_cleanup_absence_checks = 0U;
  OgreNextPssmShadowFramePlan last_frame;
  OgreNextPssmSplitPolicy last_native_splits;
  std::array<float, kOgreNextPssmCascadeCount>
      last_native_normal_offset_bias{};
  bool native_projection_extents_verified = false;
  bool native_readback_verified = false;
  bool native_bounds_readback_verified = false;
  std::vector<OgreNextPssmNativeBoundsObservation>
      last_native_bounds_observations;
};

/// Runtime audit of the native RT4/V1 texture variants owned by one frontend.
///
/// A source texture may require one sampled RGBA allocation (base colour or
/// emissive), or the two R8 derivatives used by a packed metallic-roughness
/// binding. `exact_usage` is false unless every live native allocation exactly
/// matches the roles discovered from the currently published material graph.
struct OgreNextN1TextureAllocationAudit final {
  std::uint32_t version = 1U;
  std::uint32_t live_source_textures = 0U;
  std::uint32_t sampled_rgba_allocations = 0U;
  std::uint32_t roughness_r8_allocations = 0U;
  std::uint32_t metallic_r8_allocations = 0U;
  std::uint64_t native_allocation_creates = 0U;
  std::uint64_t native_allocation_destroys = 0U;
  std::uint64_t live_native_allocations = 0U;
  std::uint64_t retired_name_lookups = 0U;
  std::uint64_t retired_name_rejections = 0U;
  bool exact_usage = false;
};

/// First production adapter behind the renderer-neutral boundary.
///
/// Ogre headers and native objects are confined to the private implementation.
/// This class must only be built in the standalone Ogre-Next target; it must
/// never be linked into the OGRE 1.14 RoR executable.
class OgreNextN1Frontend final : public IRendererFrontend {
public:
  explicit OgreNextN1Frontend(OgreNextN1Configuration configuration);
  OgreNextN1Frontend(OgreNextN1Configuration configuration,
                     OgreNextNativeFeatureTier native_feature_tier);
  ~OgreNextN1Frontend() override;

  OgreNextN1Frontend(const OgreNextN1Frontend &) = delete;
  OgreNextN1Frontend &operator=(const OgreNextN1Frontend &) = delete;
  OgreNextN1Frontend(OgreNextN1Frontend &&) = delete;
  OgreNextN1Frontend &operator=(OgreNextN1Frontend &&) = delete;

  [[nodiscard]] FrontendCapabilityReport QueryCapabilities() const override;
  [[nodiscard]] OgreNextN1TextureAllocationAudit
  QueryTextureAllocationAudit() const noexcept;
  [[nodiscard]] OgreNextPssmShadowRuntimeAudit
  QueryDirectionalShadowAudit() const noexcept;
  RenderOperationResult
  Initialize(const FrontendInitializationRequest &request) override;
  RenderOperationResult
  UpdateSurface(const FrontendSurfaceUpdate &update, bool headless,
                std::uint64_t timeout_nanoseconds) override;
  RenderOperationResult
  SynchronizeAssets(const RenderAssetDelta &delta) override;
  RenderOperationResult ReleaseResource(ResourceHandle resource) override;
  RenderOperationResult Render(const RenderFrameRequest &request,
                               RenderFrameOutput &output) override;
  [[nodiscard]] bool
  IsFrameComplete(std::uint64_t frame_id) const noexcept override;
  RenderOperationResult
  WaitForFrame(std::uint64_t frame_id,
               std::uint64_t timeout_nanoseconds) override;
  [[nodiscard]] NativeRenderInterop *GetNativeInterop() noexcept override;
  RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
