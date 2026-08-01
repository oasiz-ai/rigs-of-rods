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
#include "RasterFeatureTier.h"

#include <cstdint>
#include <memory>
#include <string>

namespace RoR::Render {

enum class OgreNextNativeFeatureTier : std::uint8_t;
struct OgreNextReflectionProbeAudit;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
struct OgreNextReflectionProbeCaptureEvidence;
#endif

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

/// Native Image2 staging proof exposed only by the standalone smoke seam.
/// Every counted byte was read back from Ogre's row-pitched RG8 TextureBox
/// before the same Image2 was handed to TextureGpu residency upload.
struct OgreNextN1NormalUploadAudit final {
  std::uint32_t version = 1U;
  std::uint64_t verified_uploads = 0U;
  std::uint64_t verified_mip_levels = 0U;
  std::uint64_t verified_rows = 0U;
  std::uint64_t verified_texels = 0U;
  std::uint64_t verified_rg_bytes = 0U;
  std::uint64_t verified_padded_source_rows = 0U;
  bool exact_source_rg_to_native_image = false;
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
  bool retain_reflection_capture_evidence = false;
#endif
};

/// Runtime audit of the native RT4/V1 texture variants owned by one frontend.
///
/// A source texture may require one sampled RGBA allocation (base colour or
/// emissive), the two R8 derivatives used by a packed metallic-roughness
/// binding, or one RG8 derivative for an admitted positive-Z normal map.
/// `exact_usage` is false unless every live native allocation exactly matches
/// the roles discovered from the currently published material graph.
struct OgreNextN1TextureAllocationAudit final {
  std::uint32_t version = 1U;
  std::uint32_t live_source_textures = 0U;
  std::uint32_t sampled_rgba_allocations = 0U;
  std::uint32_t roughness_r8_allocations = 0U;
  std::uint32_t metallic_r8_allocations = 0U;
  std::uint32_t normal_rg8_allocations = 0U;
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
  [[nodiscard]] OgreNextReflectionProbeAudit
  QueryReflectionProbeAudit() const noexcept;
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  [[nodiscard]] OgreNextN1NormalUploadAudit
  QueryNormalUploadAudit() const noexcept;
  [[nodiscard]] OgreNextReflectionProbeCaptureEvidence
  QueryReflectionProbeCaptureEvidence() const;
#endif
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
