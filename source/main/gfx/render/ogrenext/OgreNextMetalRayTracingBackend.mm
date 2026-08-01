/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Same-device Metal BLAS/TLAS and ray-query proof for Ogre-Next N2.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "OgreNextMetalRayTracingBackend.h"

#include "OgreNextN1NativeInterop.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <sstream>
#include <thread>
#include <utility>

namespace RoR::Render {
namespace {

constexpr std::uint32_t kHitMagic = UINT32_C(0x52545254);
constexpr std::uint64_t kDispatchTimeoutNanoseconds =
    UINT64_C(5) * UINT64_C(1000) * UINT64_C(1000) * UINT64_C(1000);
constexpr std::uint64_t kMaximumProofReadbackBytes =
    UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024);

struct ProbeResult {
  std::uint32_t magic = 0U;
  float distance = -1.0F;
};

RenderOperationResult Failure(RenderOperationCode code,
                              std::string detail) {
  return RenderOperationResult::Failure(code, std::move(detail));
}

RenderOperationResult Invalid(std::string detail) {
  return Failure(RenderOperationCode::INVALID_ARGUMENT, std::move(detail));
}

RenderOperationResult BackendFailure(std::string detail) {
  return Failure(RenderOperationCode::BACKEND_FAILURE, std::move(detail));
}

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

template <typename ObjectType>
ObjectType Decode(const NativeObjectToken &token) noexcept {
  void *identity = reinterpret_cast<void *>(
      static_cast<std::uintptr_t>(token.value));
  return (__bridge ObjectType)identity;
}

std::string Utf8(NSString *value) {
  if (value == nil) {
    return {};
  }
  const char *text = value.UTF8String;
  return text == nullptr ? std::string() : std::string(text);
}

std::string MetalError(id<MTLCommandBuffer> command_buffer,
                       const char *prefix) {
  std::ostringstream detail;
  detail << prefix;
  if (command_buffer != nil && command_buffer.error != nil) {
    const char *message =
        command_buffer.error.localizedDescription.UTF8String;
    if (message != nullptr) {
      detail << ": " << message;
    }
  }
  return detail.str();
}

dispatch_time_t Deadline(std::uint64_t timeout_nanoseconds) noexcept {
  if (timeout_nanoseconds == kInfiniteRenderTimeoutNanoseconds) {
    return DISPATCH_TIME_FOREVER;
  }
  const std::uint64_t bounded = std::min<std::uint64_t>(
      timeout_nanoseconds,
      static_cast<std::uint64_t>((std::numeric_limits<int64_t>::max)()));
  return dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(bounded));
}

bool CheckedAttachmentBytes(const CameraViewRequest &view,
                            std::uint64_t &row_pitch,
                            std::uint64_t &byte_count) noexcept {
  row_pitch = static_cast<std::uint64_t>(view.width) * 4U;
  if (view.height != 0U &&
      row_pitch > (std::numeric_limits<std::uint64_t>::max)() / view.height) {
    return false;
  }
  byte_count = row_pitch * view.height;
  return byte_count <= kMaximumProofReadbackBytes &&
         byte_count <= (std::numeric_limits<std::size_t>::max)();
}

bool IsIdentity(const Matrix4x4 &matrix) noexcept {
  for (std::size_t index = 0U; index < matrix.elements.size(); ++index) {
    const float expected = index % 5U == 0U ? 1.0F : 0.0F;
    if (matrix.elements[index] != expected) {
      return false;
    }
  }
  return true;
}

bool SamePoint(const Float3 &point, float x, float y, float z) noexcept {
  return point.x == x && point.y == y && point.z == z;
}

const MeshInstanceDescriptor *FindProofInstance(
    const SceneSnapshot &snapshot) noexcept {
  if (snapshot.mesh_instances().size() != 1U ||
      snapshot.dynamic_mesh_updates().size() != 1U) {
    return nullptr;
  }
  const MeshInstanceDescriptor &instance = snapshot.mesh_instances().front();
  const DynamicMeshUpdateDescriptor &update =
      snapshot.dynamic_mesh_updates().front();
  if (instance.deformation_revision <= 1U ||
      update.instance_id != instance.instance_id ||
      update.mesh != instance.mesh || update.positions.size() != 3U ||
      update.deformation_revision != instance.deformation_revision ||
      update.topology_revision != instance.topology_revision ||
      !IsIdentity(instance.render_from_object) ||
      !SamePoint(update.positions[0U], -0.5F, -0.5F, 0.0F) ||
      !SamePoint(update.positions[1U], 0.5F, -0.5F, 0.0F) ||
      !SamePoint(update.positions[2U], 0.0F, 0.5F, 0.0F)) {
    return nullptr;
  }
  return &instance;
}

NativeGeometryExportRequest MakeGeometryRequest(
    const NativeRayTracingFrameRequest &request,
    const MeshInstanceDescriptor &instance) {
  NativeGeometryExportRequest geometry_request;
  geometry_request.frame_id = request.frame.frame_id;
  geometry_request.snapshot_id = request.frame.scene_snapshot->snapshot_id();
  geometry_request.instance_id = instance.instance_id;
  geometry_request.mesh = instance.mesh;
  geometry_request.topology_revision = instance.topology_revision;
  geometry_request.deformation_revision = instance.deformation_revision;
  return geometry_request;
}

RenderOperationResult PrepareOutput(const NativeRayTracingFrameRequest &request,
                                    RenderFrameOutput &output) {
  if (request.frame.requested_outputs != FrameOutputMask::COLOR ||
      request.frame.color_format != PixelFormat::RGBA8_SRGB ||
      request.frame.views.size() != 1U || request.samples_per_pixel != 1U ||
      request.maximum_bounces != 1U || request.denoise) {
    return Failure(
        RenderOperationCode::UNSUPPORTED,
        "Metal N2 acceptance dispatch requires one offscreen RGBA8 color view, one sample, one bounce, and denoise disabled");
  }
  std::uint64_t row_pitch = 0U;
  std::uint64_t byte_count = 0U;
  if (!CheckedAttachmentBytes(request.frame.views.front(), row_pitch,
                              byte_count)) {
    return Failure(RenderOperationCode::UNSUPPORTED,
                   "Metal N2 proof readback exceeds its 64 MiB safety bound");
  }

  try {
    RenderFrameOutput candidate;
    candidate.frame_id = request.frame.frame_id;
    candidate.snapshot_id = request.frame.scene_snapshot->snapshot_id();
    candidate.status = RenderFrameStatus::RENDERED;
    candidate.presented = false;
    FrameAttachment attachment;
    attachment.view_id = request.frame.views.front().view_id;
    attachment.output = FrameOutputMask::COLOR;
    attachment.format = PixelFormat::RGBA8_SRGB;
    attachment.width = request.frame.views.front().width;
    attachment.height = request.frame.views.front().height;
    attachment.row_pitch_bytes = row_pitch;
    attachment.bytes.resize(static_cast<std::size_t>(byte_count));
    candidate.attachments.push_back(std::move(attachment));
    output = std::move(candidate);
    return RenderOperationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(RenderOperationCode::OUT_OF_MEMORY,
                   "Metal N2 proof readback allocation failed");
  }
}

void FillProofOutput(const ProbeResult &result,
                     RenderFrameOutput &output) noexcept {
  FrameAttachment &attachment = output.attachments.front();
  const std::uint8_t distance = static_cast<std::uint8_t>(std::min(
      255.0F, std::max(0.0F, result.distance * 64.0F)));
  for (std::size_t index = 0U; index < attachment.bytes.size(); index += 4U) {
    attachment.bytes[index + 0U] = 0x52U;
    attachment.bytes[index + 1U] = 0x54U;
    attachment.bytes[index + 2U] = distance;
    attachment.bytes[index + 3U] = 0xFFU;
  }
}

} // namespace

class OgreNextMetalRayTracingBackend::Impl final {
public:
  NativeRayTracingCapabilityReport Capabilities() const {
    NativeRayTracingCapabilityReport report;
    report.native_api = NativeGraphicsApi::METAL;
    report.backend_compiled = true;
    report.api_supported = initialized_;
    report.hardware_accelerated = initialized_;
    report.dispatch_readback_probe_passed =
        initialized_ && evidence_.dispatch_readback_passed;
    report.geometry_interop_ready =
        initialized_ && evidence_.geometry_interop_passed;
    report.maximum_instances = initialized_ ? 1U : 0U;
    return report;
  }

  RenderOperationResult Initialize(NativeRenderInterop &interop) {
    if (initialized_) {
      return Invalid("Metal N2 ray-tracing backend is already initialized");
    }
    auto *bridge = dynamic_cast<OgreNextN1NativeInteropBridge *>(&interop);
    if (bridge == nullptr) {
      return Failure(RenderOperationCode::UNSUPPORTED,
                     "Metal N2 requires the live Ogre-Next native interop bridge");
    }
    NativeContextExport context;
    RenderOperationResult result = bridge->AcquireContext(context);
    if (!result) {
      return result;
    }
    const ValidationResult context_validation =
        ValidateNativeContextExport(context);
    if (!context_validation || context.native_api != NativeGraphicsApi::METAL) {
      return Invalid("Metal N2 received an invalid non-Metal native context");
    }

    id<MTLDevice> device = Decode<id<MTLDevice>>(context.device);
    id<MTLCommandQueue> queue =
        Decode<id<MTLCommandQueue>>(context.graphics_queue);
    if (device == nil || queue == nil || queue.device != device) {
      return BackendFailure(
          "Metal N2 context tokens do not identify one live Ogre device and queue");
    }

    bool supports_ray_tracing = false;
    if (@available(macOS 11.0, *)) {
      supports_ray_tracing = device.supportsRaytracing;
    }
    bool supports_apple_9 = false;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
    if (@available(macOS 14.0, *)) {
      supports_apple_9 = [device supportsFamily:MTLGPUFamilyApple9];
    }
#endif
    if (!supports_ray_tracing) {
      return Failure(RenderOperationCode::UNSUPPORTED,
                     "the exact Ogre Metal device does not support ray tracing");
    }
    if (!supports_apple_9) {
      return Failure(RenderOperationCode::UNSUPPORTED,
                     "the exact Ogre Metal device does not meet the Apple family 9 hardware floor");
    }

    static NSString *const shader_source =
        @"#include <metal_stdlib>\n"
         "#include <metal_raytracing>\n"
         "using namespace metal;\n"
         "using namespace raytracing;\n"
         "struct ProbeResult { uint magic; float distance; };\n"
         "kernel void ror_ogre_next_metal_n2_probe(\n"
         "    instance_acceleration_structure scene [[buffer(0)]],\n"
         "    device ProbeResult* output [[buffer(1)]],\n"
         "    uint thread_id [[thread_position_in_grid]])\n"
         "{\n"
         "    if (thread_id != 0u) { return; }\n"
         "    ray probe_ray;\n"
         "    probe_ray.origin = float3(0.0f, 0.0f, 1.0f);\n"
         "    probe_ray.direction = float3(0.0f, 0.0f, -1.0f);\n"
         "    probe_ray.min_distance = 0.001f;\n"
         "    probe_ray.max_distance = 10.0f;\n"
         "    intersector<triangle_data, instancing> tracer;\n"
         "    auto hit = tracer.intersect(probe_ray, scene);\n"
         "    if (hit.type != intersection_type::none) {\n"
         "        output[0].magic = 0x52545254u;\n"
         "        output[0].distance = hit.distance;\n"
         "    } else {\n"
         "        output[0].magic = 0u;\n"
         "        output[0].distance = -1.0f;\n"
         "    }\n"
         "}\n";
    NSError *library_error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:shader_source
                                                  options:nil
                                                    error:&library_error];
    if (library == nil) {
      return BackendFailure("Metal N2 ray-query shader compile failed: " +
                            Utf8(library_error.localizedDescription));
    }
    id<MTLFunction> function =
        [library newFunctionWithName:@"ror_ogre_next_metal_n2_probe"];
    if (function == nil) {
      return BackendFailure("Metal N2 ray-query function was not found");
    }
    NSError *pipeline_error = nil;
    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function
                                              error:&pipeline_error];
    if (pipeline == nil) {
      return BackendFailure("Metal N2 pipeline creation failed: " +
                            Utf8(pipeline_error.localizedDescription));
    }

    result = bridge->RegisterRayTracingBackend();
    if (!result) {
      return result;
    }
    bridge_ = bridge;
    context_ = context;
    device_ = device;
    queue_ = queue;
    pipeline_ = pipeline;
    owner_thread_ = std::this_thread::get_id();
    initialized_ = true;
    evidence_ = {};
    evidence_.context = context;
    evidence_.device_name = Utf8(device.name);
    evidence_.api_supported = true;
    evidence_.apple_family_9_supported = true;
    evidence_.same_ogre_device = true;
    evidence_.same_ogre_queue = true;
    return RenderOperationResult::Success();
  }

  RenderOperationResult Render(const NativeRayTracingFrameRequest &request,
                               RenderFrameOutput &output) {
    if (!initialized_) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "Metal N2 ray-tracing backend is not initialized");
    }
    if (!OnOwnerThread()) {
      return Invalid("Metal N2 render was called off its owner thread");
    }
    if (command_submitted_ || geometry_live_ || frame_live_) {
      return Failure(RenderOperationCode::OUTSTANDING_LEASES,
                     "Metal N2 retains its acceptance evidence until Shutdown");
    }
    const ValidationResult request_validation =
        ValidateNativeRayTracingFrameRequest(request);
    if (!request_validation) {
      return Invalid("invalid Metal N2 request: " + request_validation.field +
                     ": " + request_validation.detail);
    }
    const MeshInstanceDescriptor *instance =
        FindProofInstance(*request.frame.scene_snapshot);
    if (instance == nullptr) {
      return Failure(
          RenderOperationCode::UNSUPPORTED,
          "Metal N2 acceptance requires one full three-vertex deformed mesh revision");
    }

    RenderFrameOutput candidate;
    RenderOperationResult result = PrepareOutput(request, candidate);
    if (!result) {
      return result;
    }
    const NativeGeometryExportRequest geometry_request =
        MakeGeometryRequest(request, *instance);
    NativeGeometryExport geometry;
    result = bridge_->AcquireGeometry(geometry_request, geometry);
    if (!result) {
      return result;
    }
    geometry_live_ = true;
    geometry_ = geometry;

    NativeFrameSynchronization synchronization;
    result = bridge_->BeginExternalFrame(
        request.frame.frame_id, request.frame.scene_snapshot->snapshot_id(),
        synchronization);
    if (!result) {
      ReleaseGeometry();
      return result;
    }
    frame_live_ = true;
    synchronization_ = synchronization;
    result = bridge_->ArmExternalCompletion(synchronization_);
    if (!result) {
      AbortBeforeSubmission();
      return result;
    }

    const ValidationResult geometry_validation = ValidateNativeGeometryExport(
        geometry_request, geometry_, NativeGraphicsApi::METAL,
        context_.context_id);
    const ValidationResult synchronization_validation =
        ValidateNativeFrameSynchronization(synchronization_, context_, true);
    if (!geometry_validation || !synchronization_validation ||
        !SameToken(synchronization_.interop_queue,
                   context_.graphics_queue) ||
        !SameToken(synchronization_.frontend_complete_timeline,
                   synchronization_.external_complete_timeline)) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N2 bridge generated invalid geometry or shared-event synchronization");
    }

    id<MTLBuffer> vertex_buffer =
        Decode<id<MTLBuffer>>(geometry_.positions.buffer);
    id<MTLBuffer> index_buffer =
        Decode<id<MTLBuffer>>(geometry_.indices.buffer);
    id<MTLSharedEvent> timeline = Decode<id<MTLSharedEvent>>(
        synchronization_.frontend_complete_timeline);
    id<MTLCommandQueue> interop_queue = Decode<id<MTLCommandQueue>>(
        synchronization_.interop_queue);
    if (vertex_buffer == nil || index_buffer == nil || timeline == nil ||
        interop_queue == nil || interop_queue != queue_ ||
        interop_queue.device != device_ || vertex_buffer.device != device_ ||
        index_buffer.device != device_ ||
        geometry_.positions.offset_bytes > vertex_buffer.length ||
        geometry_.positions.size_bytes >
            vertex_buffer.length - geometry_.positions.offset_bytes ||
        geometry_.indices.offset_bytes > index_buffer.length ||
        geometry_.indices.size_bytes >
            index_buffer.length - geometry_.indices.offset_bytes) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N2 exported slices do not belong to the exact Ogre device and queue");
    }

    MTLAccelerationStructureTriangleGeometryDescriptor *triangle =
        [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
    triangle.vertexBuffer = vertex_buffer;
    triangle.vertexBufferOffset =
        static_cast<NSUInteger>(geometry_.positions.offset_bytes);
    triangle.vertexStride = geometry_.positions.stride_bytes;
    triangle.indexBuffer = index_buffer;
    triangle.indexBufferOffset =
        static_cast<NSUInteger>(geometry_.indices.offset_bytes);
    triangle.indexType = geometry_.index_format == NativeIndexFormat::UINT16
                             ? MTLIndexTypeUInt16
                             : MTLIndexTypeUInt32;
    triangle.triangleCount = geometry_.index_count / 3U;
    triangle.opaque = YES;

    MTLPrimitiveAccelerationStructureDescriptor *blas_descriptor =
        [MTLPrimitiveAccelerationStructureDescriptor descriptor];
    blas_descriptor.geometryDescriptors = @[ triangle ];
    const MTLAccelerationStructureSizes blas_sizes =
        [device_ accelerationStructureSizesWithDescriptor:blas_descriptor];
    id<MTLAccelerationStructure> blas = [device_
        newAccelerationStructureWithSize:blas_sizes.accelerationStructureSize];

    MTLAccelerationStructureInstanceDescriptor native_instance = {};
    native_instance.transformationMatrix =
        MTLPackedFloat4x3(MTLPackedFloat3Make(1.0F, 0.0F, 0.0F),
                          MTLPackedFloat3Make(0.0F, 1.0F, 0.0F),
                          MTLPackedFloat3Make(0.0F, 0.0F, 1.0F),
                          MTLPackedFloat3Make(0.0F, 0.0F, 0.0F));
    native_instance.options = MTLAccelerationStructureInstanceOptionNone;
    native_instance.mask = 0xFFU;
    native_instance.intersectionFunctionTableOffset = 0U;
    native_instance.accelerationStructureIndex = 0U;
    id<MTLBuffer> instance_buffer = [device_
        newBufferWithBytes:&native_instance
                    length:sizeof(native_instance)
                   options:MTLResourceStorageModeShared];
    MTLInstanceAccelerationStructureDescriptor *tlas_descriptor =
        [MTLInstanceAccelerationStructureDescriptor descriptor];
    tlas_descriptor.instanceDescriptorBuffer = instance_buffer;
    tlas_descriptor.instanceDescriptorBufferOffset = 0U;
    tlas_descriptor.instanceDescriptorStride = sizeof(native_instance);
    tlas_descriptor.instanceCount = 1U;
    tlas_descriptor.instancedAccelerationStructures = @[ blas ];
    const MTLAccelerationStructureSizes tlas_sizes =
        [device_ accelerationStructureSizesWithDescriptor:tlas_descriptor];
    id<MTLAccelerationStructure> tlas = [device_
        newAccelerationStructureWithSize:tlas_sizes.accelerationStructureSize];

    const NSUInteger scratch_size = std::max(
        blas_sizes.buildScratchBufferSize, tlas_sizes.buildScratchBufferSize);
    id<MTLBuffer> scratch =
        [device_ newBufferWithLength:scratch_size
                             options:MTLResourceStorageModePrivate];
    const ProbeResult initial_result{};
    id<MTLBuffer> result_buffer =
        [device_ newBufferWithBytes:&initial_result
                             length:sizeof(initial_result)
                            options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command_buffer = [queue_ commandBuffer];
    if (blas_sizes.accelerationStructureSize == 0U ||
        blas_sizes.buildScratchBufferSize == 0U ||
        tlas_sizes.accelerationStructureSize == 0U ||
        tlas_sizes.buildScratchBufferSize == 0U || blas == nil ||
        instance_buffer == nil || tlas == nil || scratch == nil ||
        result_buffer == nil || command_buffer == nil) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N2 could not allocate its BLAS/TLAS proof resources");
    }

    [command_buffer encodeWaitForEvent:timeline
                                  value:synchronization_.frontend_complete_value];
    id<MTLAccelerationStructureCommandEncoder> acceleration_encoder =
        [command_buffer accelerationStructureCommandEncoder];
    if (acceleration_encoder == nil) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N2 could not create an acceleration-structure encoder");
    }
    [acceleration_encoder buildAccelerationStructure:blas
                                          descriptor:blas_descriptor
                                       scratchBuffer:scratch
                                 scratchBufferOffset:0U];
    [acceleration_encoder buildAccelerationStructure:tlas
                                          descriptor:tlas_descriptor
                                       scratchBuffer:scratch
                                 scratchBufferOffset:0U];
    [acceleration_encoder endEncoding];

    id<MTLComputeCommandEncoder> compute_encoder =
        [command_buffer computeCommandEncoder];
    if (compute_encoder == nil) {
      AbortBeforeSubmission();
      return BackendFailure("Metal N2 could not create a compute encoder");
    }
    [compute_encoder setComputePipelineState:pipeline_];
    [compute_encoder setAccelerationStructure:tlas atBufferIndex:0U];
    [compute_encoder setBuffer:result_buffer offset:0U atIndex:1U];
    [compute_encoder dispatchThreads:MTLSizeMake(1U, 1U, 1U)
                threadsPerThreadgroup:MTLSizeMake(1U, 1U, 1U)];
    [compute_encoder endEncoding];
    // Every external encoder is ended before the shared timeline is signaled.
    [command_buffer encodeSignalEvent:timeline
                                 value:synchronization_.external_complete_value];

    dispatch_semaphore_t completion = dispatch_semaphore_create(0);
    [command_buffer addCompletedHandler:^(id<MTLCommandBuffer>) {
      dispatch_semaphore_signal(completion);
    }];
    result = bridge_->MarkExternalSubmitted(synchronization_);
    if (!result) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N2 lifecycle rejected its fully encoded submission");
    }
    command_submitted_ = true;
    command_buffer_ = command_buffer;
    completion_ = completion;
    result_buffer_ = result_buffer;
    blas_ = blas;
    tlas_ = tlas;
    scratch_ = scratch;
    instance_buffer_ = instance_buffer;
    submission_start_ = std::chrono::steady_clock::now();
    [command_buffer commit];

    if (dispatch_semaphore_wait(
            completion_, Deadline(kDispatchTimeoutNanoseconds)) != 0) {
      return Failure(
          RenderOperationCode::TIMEOUT,
          "Metal N2 dispatch exceeded five seconds; live leases were retained for retryable Shutdown");
    }
    completion_observed_ = true;
    return CompleteDispatch(request, geometry_request, vertex_buffer,
                            index_buffer, std::move(candidate), output,
                            blas_sizes, tlas_sizes);
  }

  RenderOperationResult ValidateInteropEvidence(
      const NativeGeometryExport &geometry,
      const NativeFrameSynchronization &synchronization) const {
    if (!initialized_ || !evidence_.geometry_interop_passed) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "Metal N2 has no live passed interop evidence");
    }
    if (!OnOwnerThread()) {
      return Invalid("Metal N2 evidence was queried off its owner thread");
    }
    if (!SameGeometry(geometry, geometry_) ||
        !SameSynchronization(synchronization, synchronization_)) {
      return Failure(RenderOperationCode::RESOURCE_STALE,
                     "Metal N2 evidence payload differs from its live leases");
    }
    RenderOperationResult result = bridge_->ValidateGeometryLease(geometry);
    if (!result) {
      return result;
    }
    return bridge_->ValidateFrameLease(synchronization);
  }

  RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) {
    if (!initialized_) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "Metal N2 ray-tracing backend is not initialized");
    }
    if (!OnOwnerThread()) {
      return Invalid("Metal N2 shutdown was called off its owner thread");
    }

    RenderOperationResult dispatch_result = RenderOperationResult::Success();
    if (command_submitted_ && !completion_observed_) {
      if (dispatch_semaphore_wait(completion_, Deadline(timeout_nanoseconds)) !=
          0) {
        return Failure(
            RenderOperationCode::TIMEOUT,
            "timed out waiting for submitted Metal N2 work; backend and leases remain live");
      }
      completion_observed_ = true;
    }
    if (command_submitted_) {
      dispatch_result = ObserveSubmittedCommand();
      if (!dispatch_result &&
          dispatch_result.code == RenderOperationCode::DEVICE_LOST) {
        return dispatch_result;
      }
      if (!external_completed_) {
        const RenderOperationResult completed =
            bridge_->MarkExternalCompleted(synchronization_);
        if (!completed) {
          return BackendFailure(
              "Metal N2 could not recover its completed external lease");
        }
        external_completed_ = true;
      }
    }
    if (frame_live_) {
      const RenderOperationResult ended =
          bridge_->EndExternalFrame(synchronization_);
      if (!ended) {
        return ended;
      }
      frame_live_ = false;
    }
    ReleaseGeometry();
    bridge_->SetRayTracingProof(false, false);
    const RenderOperationResult unregistered =
        bridge_->UnregisterRayTracingBackend();
    if (!unregistered) {
      return unregistered;
    }
    ResetNativeState();
    initialized_ = false;
    bridge_ = nullptr;
    context_ = {};
    return dispatch_result;
  }

  const OgreNextMetalRayTracingEvidence &evidence() const noexcept {
    return evidence_;
  }

  void BestEffortDestructorShutdown() noexcept {
    if (!initialized_ || !OnOwnerThread()) {
      return;
    }
    static_cast<void>(Shutdown(0U));
  }

private:
  bool OnOwnerThread() const noexcept {
    return std::this_thread::get_id() == owner_thread_;
  }

  void AbortBeforeSubmission() noexcept {
    if (frame_live_ && !command_submitted_) {
      static_cast<void>(
          bridge_->AbortExternalFrameBeforeSubmission(synchronization_));
      frame_live_ = false;
    }
    ReleaseGeometry();
  }

  void ReleaseGeometry() noexcept {
    if (geometry_live_) {
      bridge_->ReleaseGeometry(geometry_.export_id);
      geometry_live_ = false;
      geometry_ = {};
    }
  }

  RenderOperationResult ObserveSubmittedCommand() const {
    if (command_buffer_ == nil ||
        command_buffer_.status != MTLCommandBufferStatusCompleted) {
      return Failure(
          RenderOperationCode::DEVICE_LOST,
          MetalError(command_buffer_,
                     "submitted Metal N2 command buffer did not complete"));
    }
    const ProbeResult observed =
        *static_cast<const ProbeResult *>(result_buffer_.contents);
    if (observed.magic != kHitMagic || !std::isfinite(observed.distance) ||
        std::fabs(observed.distance - 1.0F) > 0.0001F) {
      std::ostringstream detail;
      detail << "Metal N2 ray-query readback did not hit the exported Ogre triangle"
             << " (magic=" << observed.magic
             << ", distance=" << observed.distance << ')';
      return BackendFailure(detail.str());
    }
    return RenderOperationResult::Success();
  }

  RenderOperationResult CompleteDispatch(
      const NativeRayTracingFrameRequest &request,
      const NativeGeometryExportRequest &geometry_request,
      id<MTLBuffer> vertex_buffer, id<MTLBuffer> index_buffer,
      RenderFrameOutput candidate, RenderFrameOutput &output,
      const MTLAccelerationStructureSizes &blas_sizes,
      const MTLAccelerationStructureSizes &tlas_sizes) {
    RenderOperationResult result = ObserveSubmittedCommand();
    if (!result) {
      return result;
    }
    result = bridge_->MarkExternalCompleted(synchronization_);
    if (!result) {
      return BackendFailure(
          "Metal N2 lifecycle rejected its completed command buffer");
    }
    external_completed_ = true;
    const ProbeResult observed =
        *static_cast<const ProbeResult *>(result_buffer_.contents);
    FillProofOutput(observed, candidate);
    candidate.cpu_submit_milliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - submission_start_)
            .count();
    candidate.gpu_frame_milliseconds = candidate.cpu_submit_milliseconds;
    const ValidationResult output_validation =
        ValidateNativeRayTracingFrameOutput(request, candidate);
    if (!output_validation) {
      return BackendFailure("Metal N2 generated invalid readback output: " +
                            output_validation.field + ": " +
                            output_validation.detail);
    }

    evidence_.geometry_request = geometry_request;
    evidence_.geometry_export = geometry_;
    evidence_.frame_synchronization = synchronization_;
    evidence_.vertex_buffer_length_bytes = vertex_buffer.length;
    evidence_.index_buffer_length_bytes = index_buffer.length;
    evidence_.blas_bytes = blas_sizes.accelerationStructureSize;
    evidence_.blas_scratch_bytes = blas_sizes.buildScratchBufferSize;
    evidence_.tlas_bytes = tlas_sizes.accelerationStructureSize;
    evidence_.tlas_scratch_bytes = tlas_sizes.buildScratchBufferSize;
    evidence_.hit_magic = observed.magic;
    evidence_.hit_distance = observed.distance;
    evidence_.exact_exported_vertex_slice_used = true;
    evidence_.exact_exported_index_slice_used = true;
    evidence_.dispatch_readback_passed = true;
    evidence_.geometry_interop_passed = true;
    bridge_->SetRayTracingProof(true, true);
    output = std::move(candidate);
    return RenderOperationResult::Success();
  }

  void ResetNativeState() noexcept {
    command_buffer_ = nil;
    completion_ = nullptr;
    result_buffer_ = nil;
    blas_ = nil;
    tlas_ = nil;
    scratch_ = nil;
    instance_buffer_ = nil;
    pipeline_ = nil;
    queue_ = nil;
    device_ = nil;
    synchronization_ = {};
    geometry_ = {};
    command_submitted_ = false;
    completion_observed_ = false;
    external_completed_ = false;
    frame_live_ = false;
    geometry_live_ = false;
  }

  OgreNextN1NativeInteropBridge *bridge_ = nullptr;
  NativeContextExport context_;
  NativeGeometryExport geometry_;
  NativeFrameSynchronization synchronization_;
  OgreNextMetalRayTracingEvidence evidence_;
  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  id<MTLComputePipelineState> pipeline_ = nil;
  id<MTLCommandBuffer> command_buffer_ = nil;
  id<MTLBuffer> result_buffer_ = nil;
  id<MTLBuffer> scratch_ = nil;
  id<MTLBuffer> instance_buffer_ = nil;
  id<MTLAccelerationStructure> blas_ = nil;
  id<MTLAccelerationStructure> tlas_ = nil;
  dispatch_semaphore_t completion_ = nullptr;
  std::chrono::steady_clock::time_point submission_start_;
  std::thread::id owner_thread_;
  bool initialized_ = false;
  bool geometry_live_ = false;
  bool frame_live_ = false;
  bool command_submitted_ = false;
  bool completion_observed_ = false;
  bool external_completed_ = false;
};

OgreNextMetalRayTracingBackend::OgreNextMetalRayTracingBackend()
    : impl_(std::make_unique<Impl>()) {}

OgreNextMetalRayTracingBackend::~OgreNextMetalRayTracingBackend() {
  if (impl_) {
    impl_->BestEffortDestructorShutdown();
  }
}

NativeRayTracingCapabilityReport
OgreNextMetalRayTracingBackend::QueryCapabilities() const {
  return impl_->Capabilities();
}

RenderOperationResult
OgreNextMetalRayTracingBackend::Initialize(NativeRenderInterop &interop) {
  return impl_->Initialize(interop);
}

RenderOperationResult OgreNextMetalRayTracingBackend::Render(
    const NativeRayTracingFrameRequest &request, RenderFrameOutput &output) {
  return impl_->Render(request, output);
}

RenderOperationResult OgreNextMetalRayTracingBackend::ValidateInteropEvidence(
    const NativeGeometryExport &geometry,
    const NativeFrameSynchronization &synchronization) const {
  return impl_->ValidateInteropEvidence(geometry, synchronization);
}

RenderOperationResult OgreNextMetalRayTracingBackend::Shutdown(
    std::uint64_t timeout_nanoseconds) {
  return impl_->Shutdown(timeout_nanoseconds);
}

const OgreNextMetalRayTracingEvidence &
OgreNextMetalRayTracingBackend::evidence() const {
  return impl_->evidence();
}

} // namespace RoR::Render
