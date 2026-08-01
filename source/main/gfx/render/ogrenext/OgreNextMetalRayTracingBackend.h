/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Metal hardware-ray-tracing acceptance backend for Ogre-Next N2.

#pragma once

#include "../RendererFrontend.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RoR::Render {

/// Captured live evidence from the one-ray same-device acceptance dispatch.
/// This proves API/hardware/dispatch and exact Ogre geometry interoperability;
/// it deliberately does not claim ray-traced material or compositing parity.
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
};

/// macOS-only N2 acceptance backend. Its implementation is ObjC++ and must be
/// compiled only in the Apple Metal target; this header remains pure C++ so
/// callers and contract tests do not import native platform headers.
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
  /// INativeRayTracingBackend::Render remains unsupported until this backend
  /// can produce a view-dependent image. N2 is deliberately a geometry-
  /// interop capability probe rather than a renderer.
  RenderOperationResult Render(const NativeRayTracingFrameRequest &request,
                               RenderFrameOutput &output) override;
  RenderOperationResult RunGeometryInteropProbe(
      const NativeRayTracingFrameRequest &request);
  [[nodiscard]] RenderOperationResult ValidateInteropEvidence(
      const NativeGeometryExport &geometry,
      const NativeFrameSynchronization &synchronization) const override;
  RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) override;

  [[nodiscard]] const OgreNextMetalRayTracingEvidence &evidence() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
