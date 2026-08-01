/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN2InteropState.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

NativeObjectToken Token(NativeObjectKind kind, std::uint64_t value,
                        std::uint64_t generation = 7U) {
  NativeObjectToken token;
  token.api = NativeGraphicsApi::METAL;
  token.kind = kind;
  token.context_id = 41U;
  token.value = value;
  token.generation = generation;
  return token;
}

NativeContextExport Context() {
  NativeContextExport context;
  context.native_api = NativeGraphicsApi::METAL;
  context.context_id = 41U;
  context.device = Token(NativeObjectKind::DEVICE, 100U);
  context.graphics_queue = Token(NativeObjectKind::QUEUE, 101U);
  return context;
}

RenderAssetReference Mesh(std::uint64_t revision = 3U) {
  RenderAssetReference mesh;
  mesh.id = RenderAssetId::FromWords(10U, 20U);
  mesh.revision = revision;
  mesh.kind = RenderAssetKind::MESH;
  return mesh;
}

OgreNextN2PublishedGeometry Published(std::uint64_t frame_id,
                                     std::uint64_t snapshot_id,
                                     std::uint64_t deformation_revision,
                                     std::uint64_t buffer_generation = 7U) {
  OgreNextN2PublishedGeometry published;
  NativeGeometryExport &geometry = published.geometry;
  geometry.frame_id = frame_id;
  geometry.snapshot_id = snapshot_id;
  geometry.instance_id = 9U;
  geometry.mesh = Mesh();
  geometry.topology_revision = 5U;
  geometry.deformation_revision = deformation_revision;
  geometry.topology = MeshPrimitiveTopology::TRIANGLE_LIST;
  geometry.positions.buffer =
      Token(NativeObjectKind::BUFFER, 200U, buffer_generation);
  geometry.positions.offset_bytes = 128U;
  geometry.positions.size_bytes = 60U;
  geometry.positions.stride_bytes = 24U;
  geometry.indices.buffer =
      Token(NativeObjectKind::BUFFER, 201U, buffer_generation);
  geometry.indices.offset_bytes = 64U;
  geometry.indices.size_bytes = 6U;
  geometry.indices.stride_bytes = 2U;
  geometry.position_format = NativeVertexPositionFormat::FLOAT32_XYZ;
  geometry.index_format = NativeIndexFormat::UINT16;
  geometry.vertex_count = 3U;
  geometry.index_count = 3U;
  return published;
}

NativeGeometryExportRequest Request(std::uint64_t frame_id,
                                    std::uint64_t snapshot_id,
                                    std::uint64_t deformation_revision) {
  NativeGeometryExportRequest request;
  request.frame_id = frame_id;
  request.snapshot_id = snapshot_id;
  request.instance_id = 9U;
  request.mesh = Mesh();
  request.topology_revision = 5U;
  request.deformation_revision = deformation_revision;
  return request;
}

void CompleteExternal(OgreNextN2InteropState &state,
                      NativeFrameSynchronization &synchronization) {
  Require(state.ArmExternalCompletion(synchronization).ok(),
          "could not arm external completion");
  Require(state.MarkExternalSubmitted(synchronization).ok(),
          "could not mark external submission");
  Require(state.MarkExternalCompleted(synchronization).ok(),
          "could not mark external completion");
}

void TestLifecycleAndImmutableRevision() {
  OgreNextN2InteropState state;
  Require(state.Initialize(Context(),
                           Token(NativeObjectKind::TIMELINE_SYNC, 102U))
              .ok(),
          "state initialization failed");
  Require(state.PublishFrame(11U, 21U, {Published(11U, 21U, 2U)}).ok(),
          "initial frame publication failed");

  NativeGeometryExport lease;
  Require(state.AcquireGeometry(Request(11U, 21U, 2U), lease).ok(),
          "geometry acquisition failed");
  Require(state.ValidateGeometryLease(lease).ok(),
          "fresh geometry lease was rejected");

  NativeGeometryExportRequest stale_request = Request(11U, 21U, 3U);
  NativeGeometryExport untouched;
  untouched.export_id = 999U;
  const RenderOperationResult stale =
      state.AcquireGeometry(stale_request, untouched);
  Require(stale.code == RenderOperationCode::RESOURCE_STALE &&
              untouched.export_id == 999U,
          "stale deformation revision mutated the output");

  NativeFrameSynchronization synchronization;
  Require(state.BeginExternalFrame(11U, 21U, synchronization).ok(),
          "external frame begin failed");
  Require(synchronization.frontend_complete_value == 1U &&
              !synchronization.external_complete_timeline.valid(),
          "frontend completion token was not staged exactly");
  CompleteExternal(state, synchronization);
  Require(synchronization.external_complete_value == 2U &&
              state.ValidateFrameLease(synchronization).ok(),
          "completed frame lease was not live");

  const RenderOperationResult blocked =
      state.PublishFrame(12U, 22U, {Published(12U, 22U, 3U, 8U)});
  Require(blocked.code == RenderOperationCode::OUTSTANDING_LEASES,
          "revision N+1 replaced live revision N bytes");
  Require(state.ValidateGeometryLease(lease).ok(),
          "blocked replacement invalidated revision N");

  Require(state.EndExternalFrame(synchronization).ok(),
          "external frame end failed");
  Require(state.PublishFrame(12U, 22U, {Published(12U, 22U, 3U, 8U)})
              .code == RenderOperationCode::OUTSTANDING_LEASES,
          "released frame still allowed replacement with a geometry lease");
  state.ReleaseGeometry(lease.export_id);
  Require(!state.ValidateGeometryLease(lease).ok(),
          "released geometry lease remained valid");
  Require(state.PublishFrame(12U, 22U, {Published(12U, 22U, 3U, 8U)}).ok(),
          "revision N+1 did not publish after every N lease ended");

  NativeGeometryExport next_lease;
  Require(state.AcquireGeometry(Request(12U, 22U, 3U), next_lease).ok(),
          "next geometry revision acquisition failed");
  NativeFrameSynchronization next_sync;
  Require(state.BeginExternalFrame(12U, 22U, next_sync).ok() &&
              next_sync.frontend_complete_value == 3U,
          "timeline did not advance monotonically");
  CompleteExternal(state, next_sync);
  Require(next_sync.external_complete_value == 4U,
          "external timeline value was not strictly later");
  Require(state.EndExternalFrame(next_sync).ok(),
          "next external frame end failed");
  state.ReleaseGeometry(next_lease.export_id);
  Require(state.Reset().ok(), "clean state reset failed");
}

void TestTransactionalPublicationAndStaleGeneration() {
  OgreNextN2InteropState state;
  Require(state.Initialize(Context(),
                           Token(NativeObjectKind::TIMELINE_SYNC, 102U))
              .ok(),
          "state initialization failed");
  Require(state.PublishFrame(31U, 41U, {Published(31U, 41U, 2U)}).ok(),
          "baseline frame publication failed");

  OgreNextN2PublishedGeometry invalid = Published(32U, 42U, 3U);
  invalid.geometry.positions.size_bytes = 1U;
  const RenderOperationResult rejected =
      state.PublishFrame(32U, 42U, {invalid});
  Require(rejected.code == RenderOperationCode::INVALID_ARGUMENT,
          "invalid replacement was accepted");

  NativeGeometryExport old_lease;
  Require(state.AcquireGeometry(Request(31U, 41U, 2U), old_lease).ok(),
          "failed publication replaced the prior frame");
  NativeGeometryExport stale_generation = old_lease;
  ++stale_generation.positions.buffer.generation;
  Require(state.ValidateGeometryLease(stale_generation).code ==
              RenderOperationCode::RESOURCE_STALE,
          "stale resource generation passed live-lease validation");
  Require(state.DiscardPublishedFrame().code ==
              RenderOperationCode::OUTSTANDING_LEASES,
          "published storage was discarded while its lease was live");
  state.ReleaseGeometry(old_lease.export_id);
  Require(state.DiscardPublishedFrame().ok(),
          "unleased published storage could not be discarded");
  NativeGeometryExport discarded;
  Require(state.AcquireGeometry(Request(31U, 41U, 2U), discarded).code ==
              RenderOperationCode::RESOURCE_STALE,
          "discarded geometry remained acquirable");
  Require(state.Reset().ok(), "state reset failed");
}

void TestSubmittedLeaseIsRetryableButNotAbortable() {
  OgreNextN2InteropState state;
  Require(state.Initialize(Context(),
                           Token(NativeObjectKind::TIMELINE_SYNC, 102U))
              .ok(),
          "state initialization failed");
  Require(state.PublishFrame(51U, 61U, {Published(51U, 61U, 2U)}).ok(),
          "frame publication failed");
  NativeGeometryExport lease;
  Require(state.AcquireGeometry(Request(51U, 61U, 2U), lease).ok(),
          "geometry acquisition failed");
  NativeFrameSynchronization synchronization;
  Require(state.BeginExternalFrame(51U, 61U, synchronization).ok(),
          "frame begin failed");
  Require(state.ArmExternalCompletion(synchronization).ok() &&
              state.MarkExternalSubmitted(synchronization).ok(),
          "submission staging failed");
  Require(state.AbortExternalFrameBeforeSubmission(synchronization).code ==
              RenderOperationCode::RESOURCE_STALE,
          "submitted dependency was unsafely aborted");
  Require(state.CanShutdown().code ==
              RenderOperationCode::OUTSTANDING_LEASES,
          "submitted dependency did not remain retryable after timeout");
  Require(state.MarkExternalCompleted(synchronization).ok() &&
              state.ValidateFrameLease(synchronization).ok(),
          "retry completion did not recover the live lease");
  Require(state.EndExternalFrame(synchronization).ok(),
          "recovered frame could not end");
  state.ReleaseGeometry(lease.export_id);
  Require(state.Reset().ok(), "recovered state could not reset");
}

void TestPreSubmissionAbortAndShutdownOrder() {
  OgreNextN2InteropState state;
  Require(state.Initialize(Context(),
                           Token(NativeObjectKind::TIMELINE_SYNC, 102U))
              .ok(),
          "state initialization failed");
  Require(state.RegisterRayTracingBackend().ok(),
          "RT backend registration failed");
  Require(state.CanShutdown().code == RenderOperationCode::OUTSTANDING_LEASES,
          "frontend shutdown ignored the live RT backend");
  Require(state.PublishFrame(71U, 81U, {Published(71U, 81U, 2U)}).ok(),
          "frame publication failed");
  NativeGeometryExport lease;
  Require(state.AcquireGeometry(Request(71U, 81U, 2U), lease).ok(),
          "geometry acquisition failed");
  NativeFrameSynchronization synchronization;
  Require(state.BeginExternalFrame(71U, 81U, synchronization).ok(),
          "frame begin failed");
  Require(state.AbortExternalFrameBeforeSubmission(synchronization).ok(),
          "unsubmitted frame could not roll back");
  state.ReleaseGeometry(lease.export_id);
  Require(state.UnregisterRayTracingBackend().ok(),
          "RT backend unregistration failed");
  Require(state.CanShutdown().ok() && state.Reset().ok(),
          "frontend did not become shutdown-safe");
}

void TestSubmittedDeviceLossCanDrainForTeardown() {
  OgreNextN2InteropState state;
  Require(state.Initialize(Context(),
                           Token(NativeObjectKind::TIMELINE_SYNC, 102U))
              .ok(),
          "state initialization failed");
  Require(state.RegisterRayTracingBackend().ok(),
          "RT backend registration failed");
  Require(state.PublishFrame(91U, 101U, {Published(91U, 101U, 2U)}).ok(),
          "frame publication failed");
  NativeGeometryExport lease;
  Require(state.AcquireGeometry(Request(91U, 101U, 2U), lease).ok(),
          "geometry acquisition failed");
  NativeFrameSynchronization synchronization;
  Require(state.BeginExternalFrame(91U, 101U, synchronization).ok() &&
              state.ArmExternalCompletion(synchronization).ok() &&
              state.MarkExternalSubmitted(synchronization).ok(),
          "submitted fault fixture could not be staged");

  Require(state.AbandonRayTracingBackendAfterFault().ok(),
          "device-loss teardown could not revoke submitted leases");
  Require(!state.ray_tracing_backend_registered() &&
              !state.has_outstanding_leases() && state.CanShutdown().ok(),
          "fault teardown left the frontend permanently blocked");
  Require(state.ValidateGeometryLease(lease).code ==
              RenderOperationCode::RESOURCE_STALE &&
              state.ValidateFrameLease(synchronization).code ==
                  RenderOperationCode::RESOURCE_STALE,
          "fault teardown left stale native leases valid");
  Require(state.Reset().ok(), "fault-drained state could not reset");
}

} // namespace

int main() {
  try {
    TestLifecycleAndImmutableRevision();
    TestTransactionalPublicationAndStaleGeneration();
    TestSubmittedLeaseIsRetryableButNotAbortable();
    TestPreSubmissionAbortAndShutdownOrder();
    TestSubmittedDeviceLossCanDrainForTeardown();
    std::cout << "Ogre-Next N2 interop state tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next N2 interop state tests failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
