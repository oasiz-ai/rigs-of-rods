/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Metal hardware-ray-tracing backend for Ogre-Next N2/N3.

#pragma once

#include "../RendererFrontend.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR::Render {

#if defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
/// Test-only post-submission observation outcomes. This API is compiled only
/// into the isolated Apple Metal acceptance target, never the game/runtime
/// renderer library.
enum class OgreNextMetalN2TestObservation : std::uint8_t {
  NONE = 0,
  DEVICE_LOST = 1,
  TIMEOUT = 2,
};
#endif

/// Captured evidence from the same-device acceptance dispatches. N2 fills the
/// single-ray fields and retains its leases until Shutdown(). N3 fills the
/// image fields only after a real view-dependent dispatch has completed,
/// returned the exact Ogre image to its queue, and released every frame lease.
struct OgreNextMetalRayTracingEvidence {
  NativeContextExport context;
  NativeGeometryExportRequest geometry_request;
  NativeGeometryExport geometry_export;
  NativeFrameSynchronization frame_synchronization;
  std::string device_name;
  std::uint64_t vertex_buffer_length_bytes = 0U;
  std::uint64_t index_buffer_length_bytes = 0U;
  std::uint64_t blas_bytes = 0U;
  std::uint64_t blas_scratch_bytes = 0U;
  std::uint64_t tlas_bytes = 0U;
  std::uint64_t tlas_scratch_bytes = 0U;
  std::uint32_t hit_magic = 0U;
  float hit_distance = -1.0F;
  /// Exact bytes copied from the GPU-written one-ray result buffer.
  std::vector<std::uint8_t> probe_readback_bytes;
  bool api_supported = false;
  bool apple_family_9_supported = false;
  bool same_ogre_device = false;
  bool same_ogre_queue = false;
  bool exact_exported_vertex_slice_used = false;
  bool exact_exported_index_slice_used = false;
  bool dispatch_readback_passed = false;
  bool geometry_interop_passed = false;

  NativeImageExportRequest image_request;
  NativeImageExport image_export;
  NativeFrameSynchronization image_frame_synchronization;
  std::uint64_t image_row_pitch_bytes = 0U;
  /// Each vector is tightly packed RGBA16_FLOAT, independently copied from a
  /// GPU readback. `raster` is captured before the RT dispatch, `contribution`
  /// is a separate hit-only texture, and `hybrid` is the exact Ogre image
  /// after GPU composition.
  std::vector<std::uint8_t> raster_readback_bytes;
  std::vector<std::uint8_t> contribution_readback_bytes;
  std::vector<std::uint8_t> hybrid_readback_bytes;
  std::uint64_t contribution_pixel_count = 0U;
  bool exact_exported_color_image_used = false;
  bool image_state_handoff_passed = false;
  bool view_dependent_image_passed = false;
  bool hybrid_composite_passed = false;
};

/// macOS-only N2/N3 backend. Its implementation is ObjC++ and must be compiled
/// only in the Apple Metal target; this header remains pure C++ so callers and
/// contract tests do not import native platform headers.
class OgreNextMetalRayTracingBackend final
    : public INativeRayTracingBackend {
public:
  OgreNextMetalRayTracingBackend();
  ~OgreNextMetalRayTracingBackend() override;

  OgreNextMetalRayTracingBackend(
      const OgreNextMetalRayTracingBackend &) = delete;
  OgreNextMetalRayTracingBackend &
  operator=(const OgreNextMetalRayTracingBackend &) = delete;
  OgreNextMetalRayTracingBackend(OgreNextMetalRayTracingBackend &&) = delete;
  OgreNextMetalRayTracingBackend &
  operator=(OgreNextMetalRayTracingBackend &&) = delete;

  [[nodiscard]] NativeRayTracingCapabilityReport
  QueryCapabilities() const override;
  RenderOperationResult Initialize(NativeRenderInterop &interop) override;
  /// N2 interop reports UNSUPPORTED here. N3 consumes the exact frontend HDR
  /// image and exported geometry on Ogre's Metal device/queue, traces primary
  /// camera rays, and returns a tightly packed hybrid CPU readback.
  RenderOperationResult Render(const NativeRayTracingFrameRequest &request,
                               RenderFrameOutput &output) override;
  RenderOperationResult RunGeometryInteropProbe(
      const NativeRayTracingFrameRequest &request);
  [[nodiscard]] RenderOperationResult ValidateInteropEvidence(
      const NativeGeometryExport &geometry,
      const NativeFrameSynchronization &synchronization) const override;
  RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) override;

#if defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
  RenderOperationResult InjectObservationForTesting(
      OgreNextMetalN2TestObservation observation);
#endif

  [[nodiscard]] const OgreNextMetalRayTracingEvidence &evidence() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
