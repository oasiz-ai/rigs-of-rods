/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Compile-isolated native interop hooks for the Ogre-Next frontend.

#pragma once

#include "../RendererFrontend.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

enum class OgreNextNativeFeatureTier : std::uint8_t {
  RASTER_N1 = 0,
  METAL_RAY_TRACING_N2 = 1,
  METAL_RAY_TRACING_N3 = 2,
};

/// Opaque references to the exact Ogre v2 buffers selected for a raster Item.
/// Only the compile-isolated platform adapter may decode the two identities.
struct OgreNextN2FrameGeometryBinding {
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t instance_id = 0U;
  RenderAssetReference mesh;
  std::uint64_t topology_revision = 0U;
  std::uint64_t deformation_revision = 0U;
  MeshPrimitiveTopology topology = MeshPrimitiveTopology::TRIANGLE_LIST;
  std::uintptr_t ogre_vertex_buffer = 0U;
  std::uintptr_t ogre_index_buffer = 0U;
  std::uint32_t position_offset_bytes = 0U;
  std::uint32_t vertex_count = 0U;
  std::uint32_t index_count = 0U;
  NativeIndexFormat index_format = NativeIndexFormat::UINT32;
};

/// Opaque reference to the exact Ogre render target retained by N3 after its
/// UI-free raster pass. The platform adapter alone may resolve the Ogre object
/// to its native image and must independently verify every declared property.
struct OgreNextN3FrameImageBinding {
  std::uint64_t frame_id = 0U;
  std::uint64_t snapshot_id = 0U;
  std::uint64_t view_id = 0U;
  FrameOutputMask output = FrameOutputMask::NONE;
  PixelFormat format = PixelFormat::INVALID;
  std::uintptr_t ogre_texture = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
};

/// Platform implementation of NativeRenderInterop plus the private
/// transactional hooks needed by its owning Ogre frontend and RT backend.
class OgreNextN1NativeInteropBridge
    : public NativeRenderInterop,
      public std::enable_shared_from_this<OgreNextN1NativeInteropBridge> {
public:
  ~OgreNextN1NativeInteropBridge() override = default;

  /// Retains the bridge across either frontend/backend destruction order.
  /// Native calls remain owner-thread serialized; RevokeFrontend() makes the
  /// retained object teardown-only before Ogre destroys its device objects.
  [[nodiscard]] std::shared_ptr<OgreNextN1NativeInteropBridge>
  RetainForRayTracingBackend() noexcept {
    return weak_from_this().lock();
  }

  virtual void DecorateFrontendCapabilities(
      FrontendCapabilityReport &report) const = 0;
  [[nodiscard]] virtual RenderOperationResult CanPublishFrame() const = 0;
  virtual RenderOperationResult PublishFrame(
      std::uint64_t frame_id, std::uint64_t snapshot_id,
      const std::vector<OgreNextN2FrameGeometryBinding> &geometry,
      const std::vector<OgreNextN3FrameImageBinding> &images = {}) = 0;
  virtual RenderOperationResult DiscardPublishedFrame() = 0;

  virtual RenderOperationResult ArmExternalCompletion(
      NativeFrameSynchronization &synchronization) = 0;
  virtual RenderOperationResult MarkExternalSubmitted(
      const NativeFrameSynchronization &synchronization) = 0;
  virtual RenderOperationResult MarkExternalCompleted(
      const NativeFrameSynchronization &synchronization) = 0;
  virtual RenderOperationResult AbortExternalFrameBeforeSubmission(
      const NativeFrameSynchronization &synchronization) = 0;

  virtual RenderOperationResult RegisterRayTracingBackend() = 0;
  virtual RenderOperationResult UnregisterRayTracingBackend() = 0;
  virtual RenderOperationResult AbandonRayTracingBackendAfterFault() = 0;
  virtual void SetRayTracingProof(bool dispatch_readback_passed,
                                  bool geometry_interop_passed) = 0;

  virtual RenderOperationResult
  PrepareFrontendShutdown(std::uint64_t timeout_nanoseconds) = 0;
  virtual void RevokeFrontend() noexcept = 0;
};

/// Creates Metal interop only from an initialized live Ogre Metal renderer.
/// The implementation exists solely in the Apple ObjC++ target.
RenderOperationResult CreateOgreNextMetalInterop(
    std::uintptr_t ogre_render_system, bool enable_image_exports,
    std::shared_ptr<OgreNextN1NativeInteropBridge> &output);

} // namespace RoR::Render
