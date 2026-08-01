/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN2InteropState.h"

#include <limits>
#include <sstream>

namespace RoR::Render {
namespace {

bool SameToken(const NativeObjectToken &lhs,
               const NativeObjectToken &rhs) noexcept {
  return lhs.api == rhs.api && lhs.kind == rhs.kind &&
         lhs.context_id == rhs.context_id && lhs.value == rhs.value &&
         lhs.generation == rhs.generation;
}

bool SameSlice(const NativeBufferSlice &lhs,
               const NativeBufferSlice &rhs) noexcept {
  return SameToken(lhs.buffer, rhs.buffer) &&
         lhs.offset_bytes == rhs.offset_bytes &&
         lhs.size_bytes == rhs.size_bytes &&
         lhs.stride_bytes == rhs.stride_bytes;
}

bool SameGeometry(const NativeGeometryExport &lhs,
                  const NativeGeometryExport &rhs) noexcept {
  return lhs.version == rhs.version && lhs.export_id == rhs.export_id &&
         lhs.frame_id == rhs.frame_id && lhs.snapshot_id == rhs.snapshot_id &&
         lhs.instance_id == rhs.instance_id && lhs.mesh == rhs.mesh &&
         lhs.topology_revision == rhs.topology_revision &&
         lhs.deformation_revision == rhs.deformation_revision &&
         lhs.topology == rhs.topology && SameSlice(lhs.positions, rhs.positions) &&
         SameSlice(lhs.indices, rhs.indices) &&
         lhs.position_format == rhs.position_format &&
         lhs.index_format == rhs.index_format &&
         lhs.vertex_count == rhs.vertex_count &&
         lhs.index_count == rhs.index_count;
}

bool SameSynchronization(const NativeFrameSynchronization &lhs,
                         const NativeFrameSynchronization &rhs) noexcept {
  return lhs.version == rhs.version && lhs.frame_id == rhs.frame_id &&
         lhs.snapshot_id == rhs.snapshot_id &&
         SameToken(lhs.interop_queue, rhs.interop_queue) &&
         lhs.interop_queue_family == rhs.interop_queue_family &&
         lhs.frontend_release_state == rhs.frontend_release_state &&
         lhs.external_return_state == rhs.external_return_state &&
         SameToken(lhs.frontend_complete_timeline,
                   rhs.frontend_complete_timeline) &&
         lhs.frontend_complete_value == rhs.frontend_complete_value &&
         SameToken(lhs.external_complete_timeline,
                   rhs.external_complete_timeline) &&
         lhs.external_complete_value == rhs.external_complete_value;
}

RenderOperationResult Invalid(std::string detail) {
  return RenderOperationResult::Failure(RenderOperationCode::INVALID_ARGUMENT,
                                        std::move(detail));
}

RenderOperationResult Stale(std::string detail) {
  return RenderOperationResult::Failure(RenderOperationCode::RESOURCE_STALE,
                                        std::move(detail));
}

RenderOperationResult Outstanding(std::string detail) {
  return RenderOperationResult::Failure(RenderOperationCode::OUTSTANDING_LEASES,
                                        std::move(detail));
}

} // namespace

RenderOperationResult OgreNextN2InteropState::RequireInitialized() const {
  if (!initialized_) {
    return RenderOperationResult::Failure(
        RenderOperationCode::NOT_INITIALIZED,
        "Ogre-Next N2 interop state is not initialized");
  }
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::Initialize(
    const NativeContextExport &context,
    NativeObjectToken frontend_timeline) {
  if (initialized_) {
    return Invalid("Ogre-Next N2 interop state is already initialized");
  }
  const ValidationResult context_validation =
      ValidateNativeContextExport(context);
  if (!context_validation) {
    return Invalid("invalid native context: " + context_validation.field +
                   ": " + context_validation.detail);
  }
  NativeFrameSynchronization timeline_validation;
  timeline_validation.frame_id = 1U;
  timeline_validation.snapshot_id = 1U;
  timeline_validation.interop_queue = context.graphics_queue;
  timeline_validation.interop_queue_family = context.graphics_queue_family;
  timeline_validation.frontend_release_state =
      NativeGeometryBufferState::READ_ONLY_ACCELERATION_STRUCTURE_BUILD;
  timeline_validation.external_return_state =
      timeline_validation.frontend_release_state;
  timeline_validation.frontend_complete_timeline = frontend_timeline;
  timeline_validation.frontend_complete_value = 1U;
  const ValidationResult synchronization_validation =
      ValidateNativeFrameSynchronization(timeline_validation, context, false);
  if (!synchronization_validation) {
    return Invalid("invalid frontend timeline: " +
                   synchronization_validation.field + ": " +
                   synchronization_validation.detail);
  }

  context_ = context;
  frontend_timeline_ = frontend_timeline;
  next_export_id_ = 1U;
  next_timeline_value_ = 1U;
  initialized_ = true;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::PublishFrame(
    std::uint64_t frame_id, std::uint64_t snapshot_id,
    const std::vector<OgreNextN2PublishedGeometry> &geometry) {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (frame_id == 0U || snapshot_id == 0U || geometry.empty()) {
    return Invalid("published N2 frame requires nonzero identities and geometry");
  }
  if (active_frame_live_ || !geometry_leases_.empty()) {
    return Outstanding(
        "a prior native geometry revision remains leased by external work");
  }

  std::map<std::uint64_t, NativeGeometryExport> candidate;
  for (const OgreNextN2PublishedGeometry &entry : geometry) {
    NativeGeometryExport validated = entry.geometry;
    if (validated.export_id != 0U || validated.frame_id != frame_id ||
        validated.snapshot_id != snapshot_id) {
      return Invalid(
          "published geometry must have a zero export ID and match its frame");
    }
    validated.export_id = 1U;
    const ValidationResult validation = ValidateNativeGeometryExport(
        validated, context_.native_api, context_.context_id);
    if (!validation) {
      return Invalid("invalid published geometry: " + validation.field +
                     ": " + validation.detail);
    }
    validated.export_id = 0U;
    if (!candidate.emplace(validated.instance_id, validated).second) {
      return Invalid("published frame repeats a mesh instance ID");
    }
  }

  published_geometry_.swap(candidate);
  published_frame_id_ = frame_id;
  published_snapshot_id_ = snapshot_id;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::CanPublishFrame() const {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (active_frame_live_ || !geometry_leases_.empty()) {
    return Outstanding(
        "a prior native geometry revision remains leased by external work");
  }
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::DiscardPublishedFrame() {
  const RenderOperationResult can_publish = CanPublishFrame();
  if (!can_publish) {
    return can_publish;
  }
  published_geometry_.clear();
  published_frame_id_ = 0U;
  published_snapshot_id_ = 0U;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::AcquireGeometry(
    const NativeGeometryExportRequest &request,
    NativeGeometryExport &output) {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  const ValidationResult request_validation =
      ValidateNativeGeometryExportRequest(request);
  if (!request_validation) {
    return Invalid("invalid geometry request: " + request_validation.field +
                   ": " + request_validation.detail);
  }
  if (request.frame_id != published_frame_id_ ||
      request.snapshot_id != published_snapshot_id_) {
    return Stale("geometry request does not identify the published frame");
  }
  const auto found = published_geometry_.find(request.instance_id);
  if (found == published_geometry_.end()) {
    return Stale("geometry request instance is not in the published frame");
  }
  const NativeGeometryExport &published = found->second;
  if (published.mesh != request.mesh ||
      published.topology_revision != request.topology_revision ||
      published.deformation_revision != request.deformation_revision) {
    return Stale("geometry request revision differs from the published bytes");
  }
  if (next_export_id_ == 0U ||
      next_export_id_ == (std::numeric_limits<std::uint64_t>::max)()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "native geometry export identifier space is exhausted");
  }

  NativeGeometryExport candidate = published;
  candidate.export_id = next_export_id_;
  const ValidationResult validation = ValidateNativeGeometryExport(
      request, candidate, context_.native_api, context_.context_id);
  if (!validation) {
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "published geometry no longer satisfies its request: " +
            validation.field + ": " + validation.detail);
  }
  try {
    geometry_leases_.emplace(candidate.export_id, candidate);
  } catch (const std::bad_alloc &) {
    return RenderOperationResult::Failure(
        RenderOperationCode::OUT_OF_MEMORY,
        "native geometry lease allocation failed");
  }
  ++next_export_id_;
  output = candidate;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::BeginExternalFrame(
    std::uint64_t frame_id, std::uint64_t snapshot_id,
    NativeFrameSynchronization &output) {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (active_frame_live_) {
    return Outstanding("an external N2 frame lease is already live");
  }
  if (frame_id != published_frame_id_ ||
      snapshot_id != published_snapshot_id_) {
    return Stale("external frame does not identify the published frame");
  }
  bool has_geometry_lease = false;
  for (const auto &entry : geometry_leases_) {
    if (entry.second.frame_id == frame_id &&
        entry.second.snapshot_id == snapshot_id) {
      has_geometry_lease = true;
      break;
    }
  }
  if (!has_geometry_lease) {
    return Outstanding(
        "external frame requires an acquired geometry lease first");
  }
  if (next_timeline_value_ == 0U ||
      next_timeline_value_ == (std::numeric_limits<std::uint64_t>::max)()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "native timeline value space is exhausted");
  }

  NativeFrameSynchronization candidate;
  candidate.frame_id = frame_id;
  candidate.snapshot_id = snapshot_id;
  candidate.interop_queue = context_.graphics_queue;
  candidate.interop_queue_family = context_.graphics_queue_family;
  candidate.frontend_release_state =
      NativeGeometryBufferState::READ_ONLY_ACCELERATION_STRUCTURE_BUILD;
  candidate.external_return_state = candidate.frontend_release_state;
  candidate.frontend_complete_timeline = frontend_timeline_;
  candidate.frontend_complete_value = next_timeline_value_;
  const ValidationResult validation =
      ValidateNativeFrameSynchronization(candidate, context_, false);
  if (!validation) {
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "N2 generated invalid frontend synchronization: " +
            validation.field + ": " + validation.detail);
  }

  active_frame_ = {};
  active_frame_.synchronization = candidate;
  active_frame_live_ = true;
  ++next_timeline_value_;
  output = candidate;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::ArmExternalCompletion(
    NativeFrameSynchronization &synchronization) {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (!active_frame_live_ || active_frame_.external_armed) {
    return Invalid("external completion cannot be armed in the current state");
  }
  if (!SameSynchronization(synchronization,
                           active_frame_.synchronization)) {
    return Stale("external completion does not match the live frame lease");
  }
  if (next_timeline_value_ == 0U ||
      next_timeline_value_ == (std::numeric_limits<std::uint64_t>::max)()) {
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "native timeline value space is exhausted");
  }

  synchronization.external_complete_timeline = frontend_timeline_;
  synchronization.external_complete_value = next_timeline_value_;
  const ValidationResult validation =
      ValidateNativeFrameSynchronization(synchronization, context_, true);
  if (!validation) {
    return RenderOperationResult::Failure(
        RenderOperationCode::BACKEND_FAILURE,
        "N2 generated invalid external synchronization: " +
            validation.field + ": " + validation.detail);
  }
  active_frame_.synchronization = synchronization;
  active_frame_.external_armed = true;
  ++next_timeline_value_;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::MarkExternalSubmitted(
    const NativeFrameSynchronization &synchronization) {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (!active_frame_live_ || !active_frame_.external_armed ||
      !SameSynchronization(synchronization,
                           active_frame_.synchronization)) {
    return Stale("submitted external work does not match the armed lease");
  }
  if (active_frame_.external_submitted) {
    return Invalid("external command buffer cannot be submitted now");
  }
  active_frame_.external_submitted = true;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::MarkExternalCompleted(
    const NativeFrameSynchronization &synchronization) {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (!active_frame_live_ || !active_frame_.external_submitted ||
      active_frame_.external_completed ||
      !SameSynchronization(synchronization,
                           active_frame_.synchronization)) {
    return Stale("completed external work does not match the submitted lease");
  }
  active_frame_.external_completed = true;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::EndExternalFrame(
    const NativeFrameSynchronization &synchronization) {
  const RenderOperationResult validation = ValidateFrameLease(synchronization);
  if (!validation) {
    return validation;
  }
  active_frame_ = {};
  active_frame_live_ = false;
  return RenderOperationResult::Success();
}

RenderOperationResult
OgreNextN2InteropState::AbortExternalFrameBeforeSubmission(
    const NativeFrameSynchronization &synchronization) {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (!active_frame_live_ || active_frame_.external_submitted ||
      !SameSynchronization(synchronization,
                           active_frame_.synchronization)) {
    return Stale("only the exact unsubmitted external frame may be aborted");
  }
  active_frame_ = {};
  active_frame_live_ = false;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::ValidateGeometryLease(
    const NativeGeometryExport &geometry) const {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  const auto found = geometry_leases_.find(geometry.export_id);
  if (found == geometry_leases_.end() ||
      !SameGeometry(found->second, geometry)) {
    return Stale("geometry payload is not an exact live N2 lease");
  }
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::ValidateFrameLease(
    const NativeFrameSynchronization &synchronization) const {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (!active_frame_live_ || !active_frame_.external_armed ||
      !active_frame_.external_submitted ||
      !active_frame_.external_completed ||
      !SameSynchronization(synchronization,
                           active_frame_.synchronization)) {
    return Stale(
        "synchronization payload is not a completed live N2 frame lease");
  }
  return RenderOperationResult::Success();
}

void OgreNextN2InteropState::ReleaseGeometry(
    std::uint64_t export_id) noexcept {
  geometry_leases_.erase(export_id);
}

RenderOperationResult OgreNextN2InteropState::RegisterRayTracingBackend() {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (ray_tracing_backend_registered_) {
    return Outstanding("a native RT backend is already registered");
  }
  ray_tracing_backend_registered_ = true;
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::UnregisterRayTracingBackend() {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (!ray_tracing_backend_registered_) {
    return Invalid("no native RT backend is registered");
  }
  if (active_frame_live_ || !geometry_leases_.empty()) {
    return Outstanding(
        "native RT backend still owns geometry or frame leases");
  }
  ray_tracing_backend_registered_ = false;
  return RenderOperationResult::Success();
}

bool OgreNextN2InteropState::has_outstanding_leases() const noexcept {
  return active_frame_live_ || !geometry_leases_.empty();
}

RenderOperationResult OgreNextN2InteropState::CanShutdown() const {
  const RenderOperationResult initialized = RequireInitialized();
  if (!initialized) {
    return initialized;
  }
  if (ray_tracing_backend_registered_) {
    return Outstanding(
        "native RT backend must shut down before the Ogre frontend");
  }
  if (has_outstanding_leases()) {
    return Outstanding(
        "native geometry or external frame leases remain outstanding");
  }
  return RenderOperationResult::Success();
}

RenderOperationResult OgreNextN2InteropState::Reset() {
  const RenderOperationResult shutdown = CanShutdown();
  if (!shutdown) {
    return shutdown;
  }
  context_ = {};
  frontend_timeline_ = {};
  published_geometry_.clear();
  geometry_leases_.clear();
  active_frame_ = {};
  published_frame_id_ = 0U;
  published_snapshot_id_ = 0U;
  next_export_id_ = 1U;
  next_timeline_value_ = 1U;
  initialized_ = false;
  active_frame_live_ = false;
  ray_tracing_backend_registered_ = false;
  return RenderOperationResult::Success();
}

} // namespace RoR::Render
