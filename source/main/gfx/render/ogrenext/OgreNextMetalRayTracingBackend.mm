/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Same-device Metal BLAS/TLAS and hybrid image path for N2/N3.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <simd/simd.h>

#include "OgreNextMetalRayTracingBackend.h"

#include "OgreNextN1NativeInterop.h"

#include <algorithm>
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

struct ProbeResult {
  std::uint32_t magic = 0U;
  float distance = -1.0F;
};
static_assert(sizeof(ProbeResult) == 8U,
              "the Metal and host probe result ABI must remain eight bytes");

struct alignas(16) HybridParameters {
  simd_float4x4 render_from_clip;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  float minimum_distance = 0.0F;
  float maximum_distance = 0.0F;
};
static_assert(sizeof(HybridParameters) == 80U,
              "the Metal and host N3 parameter ABI must remain 80 bytes");

enum class SubmissionKind : std::uint8_t {
  NONE = 0,
  N2_PROBE = 1,
  N3_HYBRID = 2,
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
         lhs.frontend_image_release_state ==
             rhs.frontend_image_release_state &&
         lhs.external_image_return_state ==
             rhs.external_image_return_state &&
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

NativeImageExportRequest MakeImageRequest(
    const NativeRayTracingFrameRequest &request) {
  const CameraViewRequest &view = request.frame.views.front();
  NativeImageExportRequest image_request;
  image_request.frame_id = request.frame.frame_id;
  image_request.snapshot_id = request.frame.scene_snapshot->snapshot_id();
  image_request.view_id = view.view_id;
  image_request.output = FrameOutputMask::COLOR;
  image_request.format = request.frame.color_format;
  image_request.width = view.width;
  image_request.height = view.height;
  return image_request;
}

Matrix4x4 Multiply(const Matrix4x4 &lhs,
                   const Matrix4x4 &rhs) noexcept {
  Matrix4x4 result;
  result.elements.fill(0.0F);
  for (std::size_t column = 0U; column < 4U; ++column) {
    for (std::size_t row = 0U; row < 4U; ++row) {
      double value = 0.0;
      for (std::size_t inner = 0U; inner < 4U; ++inner) {
        value += static_cast<double>(lhs.elements[inner * 4U + row]) *
                 static_cast<double>(rhs.elements[column * 4U + inner]);
      }
      result.elements[column * 4U + row] = static_cast<float>(value);
    }
  }
  return result;
}

bool Invert(const Matrix4x4 &input, Matrix4x4 &output) noexcept {
  double augmented[4U][8U] = {};
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      augmented[row][column] =
          static_cast<double>(input.elements[column * 4U + row]);
    }
    augmented[row][4U + row] = 1.0;
  }
  for (std::size_t pivot_column = 0U; pivot_column < 4U; ++pivot_column) {
    std::size_t pivot_row = pivot_column;
    double pivot_magnitude = std::fabs(augmented[pivot_row][pivot_column]);
    for (std::size_t row = pivot_column + 1U; row < 4U; ++row) {
      const double magnitude = std::fabs(augmented[row][pivot_column]);
      if (magnitude > pivot_magnitude) {
        pivot_magnitude = magnitude;
        pivot_row = row;
      }
    }
    if (!std::isfinite(pivot_magnitude) || pivot_magnitude <= 1.0e-12) {
      return false;
    }
    if (pivot_row != pivot_column) {
      for (std::size_t column = 0U; column < 8U; ++column) {
        std::swap(augmented[pivot_column][column],
                  augmented[pivot_row][column]);
      }
    }
    const double pivot = augmented[pivot_column][pivot_column];
    for (std::size_t column = 0U; column < 8U; ++column) {
      augmented[pivot_column][column] /= pivot;
    }
    for (std::size_t row = 0U; row < 4U; ++row) {
      if (row == pivot_column) {
        continue;
      }
      const double scale = augmented[row][pivot_column];
      for (std::size_t column = 0U; column < 8U; ++column) {
        augmented[row][column] -= scale * augmented[pivot_column][column];
      }
    }
  }
  for (std::size_t row = 0U; row < 4U; ++row) {
    for (std::size_t column = 0U; column < 4U; ++column) {
      const double value = augmented[row][4U + column];
      if (!std::isfinite(value) ||
          std::fabs(value) >
              static_cast<double>((std::numeric_limits<float>::max)())) {
        return false;
      }
      output.elements[column * 4U + row] = static_cast<float>(value);
    }
  }
  return IsFinite(output);
}

simd_float4x4 ToSimd(const Matrix4x4 &matrix) noexcept {
  simd_float4x4 output;
  for (std::size_t column = 0U; column < 4U; ++column) {
    for (std::size_t row = 0U; row < 4U; ++row) {
      output.columns[column][row] = matrix.elements[column * 4U + row];
    }
  }
  return output;
}

MTLPackedFloat4x3 ToMetalTransform(const Matrix4x4 &matrix) noexcept {
  return MTLPackedFloat4x3(
      MTLPackedFloat3Make(matrix.elements[0U], matrix.elements[1U],
                          matrix.elements[2U]),
      MTLPackedFloat3Make(matrix.elements[4U], matrix.elements[5U],
                          matrix.elements[6U]),
      MTLPackedFloat3Make(matrix.elements[8U], matrix.elements[9U],
                          matrix.elements[10U]),
      MTLPackedFloat3Make(matrix.elements[12U], matrix.elements[13U],
                          matrix.elements[14U]));
}

bool IsFiniteHalf(std::uint16_t value) noexcept {
  return (value & UINT16_C(0x7C00)) != UINT16_C(0x7C00);
}

bool IsNonzeroHalf(std::uint16_t value) noexcept {
  return (value & UINT16_C(0x7FFF)) != 0U;
}

std::uint64_t AlignUp(std::uint64_t value,
                      std::uint64_t alignment) noexcept {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

} // namespace

class OgreNextMetalRayTracingBackend::Impl final {
public:
  NativeRayTracingCapabilityReport Capabilities() const {
    NativeRayTracingCapabilityReport report;
    report.native_api = NativeGraphicsApi::METAL;
    report.backend_compiled = true;
    report.api_supported = evidence_.api_supported;
    report.hardware_accelerated = evidence_.api_supported &&
                                  evidence_.apple_family_9_supported;
    const NativeInteropCapabilityReport interop =
        bridge_ ? bridge_->QueryCapabilities()
                : NativeInteropCapabilityReport{};
    const bool bridge_live = interop.exports_native_context;
    report.dispatch_readback_probe_passed =
        initialized_ && bridge_live && evidence_.dispatch_readback_passed;
    report.geometry_interop_ready =
        initialized_ && bridge_live && interop.geometry_interop_proven &&
        evidence_.geometry_interop_passed;
    report.view_dependent_output_ready =
        report.geometry_interop_ready && n3_enabled_ &&
        evidence_.view_dependent_image_passed;
    report.hybrid_composite_ready =
        report.view_dependent_output_ready &&
        evidence_.hybrid_composite_passed;
    report.maximum_instances = report.hardware_accelerated ? 1U : 0U;
    return report;
  }

  RenderOperationResult Initialize(NativeRenderInterop &interop) {
    if (initialized_) {
      return Invalid("Metal N2 ray-tracing backend is already initialized");
    }
    evidence_ = {};
    auto *borrowed_bridge =
        dynamic_cast<OgreNextN1NativeInteropBridge *>(&interop);
    if (borrowed_bridge == nullptr) {
      return Failure(RenderOperationCode::UNSUPPORTED,
                     "Metal N2 requires the live Ogre-Next native interop bridge");
    }
    std::shared_ptr<OgreNextN1NativeInteropBridge> bridge =
        borrowed_bridge->RetainForRayTracingBackend();
    if (!bridge || bridge.get() != borrowed_bridge) {
      return BackendFailure(
          "Metal N2 could not retain the live Ogre-Next interop bridge");
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
    evidence_.context = context;
    evidence_.device_name = Utf8(device.name);
    evidence_.api_supported = supports_ray_tracing;
    evidence_.apple_family_9_supported = supports_apple_9;
    evidence_.same_ogre_device = true;
    evidence_.same_ogre_queue = true;
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

    const NativeInteropCapabilityReport interop_capabilities =
        bridge->QueryCapabilities();
    const bool exports_any_image = interop_capabilities.exports_color_images ||
                                   interop_capabilities.supports_read_write_color_images;
    const bool n3_enabled = interop_capabilities.exports_color_images &&
                            interop_capabilities.supports_read_write_color_images;
    if (exports_any_image && !n3_enabled) {
      return BackendFailure(
          "Metal N3 image capabilities are only partially enabled");
    }

    id<MTLComputePipelineState> hybrid_pipeline = nil;
    if (n3_enabled) {
      static NSString *const hybrid_shader_source =
          @"#include <metal_stdlib>\n"
           "#include <metal_raytracing>\n"
           "using namespace metal;\n"
           "using namespace raytracing;\n"
           "struct HybridParameters {\n"
           "    float4x4 render_from_clip;\n"
           "    uint width;\n"
           "    uint height;\n"
           "    float minimum_distance;\n"
           "    float maximum_distance;\n"
           "};\n"
           "kernel void ror_ogre_next_metal_n3_hybrid(\n"
           "    instance_acceleration_structure scene [[buffer(0)]],\n"
           "    constant HybridParameters& parameters [[buffer(1)]],\n"
           "    texture2d<half, access::read_write> hybrid [[texture(0)]],\n"
           "    texture2d<half, access::write> contribution [[texture(1)]],\n"
           "    uint2 pixel [[thread_position_in_grid]])\n"
           "{\n"
           "    if (pixel.x >= parameters.width || pixel.y >= parameters.height) { return; }\n"
           "    const float2 sample_position = float2(pixel) + 0.5f;\n"
           "    const float2 uv = sample_position / float2(parameters.width, parameters.height);\n"
           "    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);\n"
           "    const float4 near_h = parameters.render_from_clip * float4(ndc, 0.0f, 1.0f);\n"
           "    const float4 far_h = parameters.render_from_clip * float4(ndc, 1.0f, 1.0f);\n"
           "    const float3 near_point = near_h.xyz / near_h.w;\n"
           "    const float3 far_point = far_h.xyz / far_h.w;\n"
           "    const float3 segment = far_point - near_point;\n"
           "    ray primary;\n"
           "    primary.origin = near_point;\n"
           "    primary.direction = normalize(segment);\n"
           "    primary.min_distance = parameters.minimum_distance;\n"
           "    primary.max_distance = min(length(segment), parameters.maximum_distance);\n"
           "    intersector<triangle_data, instancing> tracer;\n"
           "    tracer.accept_any_intersection(false);\n"
           "    const auto hit = tracer.intersect(primary, scene);\n"
           "    if (hit.type == intersection_type::none) {\n"
           "        contribution.write(half4(0.0h), pixel);\n"
           "        return;\n"
           "    }\n"
           "    const float2 barycentric = hit.triangle_barycentric_coord;\n"
           "    const float3 weights = float3(1.0f - barycentric.x - barycentric.y, barycentric);\n"
           "    const float view_factor = 0.65f + 0.35f * abs(primary.direction.z);\n"
           "    const float distance_factor = 1.0f / (1.0f + 0.08f * hit.distance);\n"
           "    const float3 tint = float3(0.12f + 0.18f * weights.y,\n"
           "                               0.08f + 0.14f * weights.z,\n"
           "                               0.05f + 0.10f * weights.x);\n"
           "    const float3 traced = clamp(tint * view_factor * distance_factor, 0.0f, 1.0f);\n"
           "    const half4 traced_half = half4(half3(traced), 0.0h);\n"
           "    contribution.write(traced_half, pixel);\n"
           "    const half4 raster = hybrid.read(pixel);\n"
           "    const float3 composed = clamp(float3(raster.rgb) + traced, -65504.0f, 65504.0f);\n"
           "    hybrid.write(half4(half3(composed), raster.a), pixel);\n"
           "}\n";
      NSError *hybrid_library_error = nil;
      id<MTLLibrary> hybrid_library =
          [device newLibraryWithSource:hybrid_shader_source
                                options:nil
                                  error:&hybrid_library_error];
      if (hybrid_library == nil) {
        return BackendFailure("Metal N3 hybrid shader compile failed: " +
                              Utf8(hybrid_library_error.localizedDescription));
      }
      id<MTLFunction> hybrid_function =
          [hybrid_library newFunctionWithName:@"ror_ogre_next_metal_n3_hybrid"];
      if (hybrid_function == nil) {
        return BackendFailure("Metal N3 hybrid function was not found");
      }
      NSError *hybrid_pipeline_error = nil;
      hybrid_pipeline =
          [device newComputePipelineStateWithFunction:hybrid_function
                                                error:&hybrid_pipeline_error];
      if (hybrid_pipeline == nil) {
        return BackendFailure("Metal N3 pipeline creation failed: " +
                              Utf8(hybrid_pipeline_error.localizedDescription));
      }
    }

    result = bridge->RegisterRayTracingBackend();
    if (!result) {
      return result;
    }
    bridge_ = std::move(bridge);
    context_ = context;
    device_ = device;
    queue_ = queue;
    pipeline_ = pipeline;
    hybrid_pipeline_ = hybrid_pipeline;
    n3_enabled_ = n3_enabled;
    owner_thread_ = std::this_thread::get_id();
    initialized_ = true;
    return RenderOperationResult::Success();
  }

  RenderOperationResult RunGeometryInteropProbe(
      const NativeRayTracingFrameRequest &request) {
    // N2 does not produce a view-dependent RenderFrameOutput. N3 is a
    // separate, explicitly image-enabled tier.
    if (!initialized_) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "Metal N2 ray-tracing backend is not initialized");
    }
    if (!OnOwnerThread()) {
      return Invalid("Metal N2 probe was called off its owner thread");
    }
    if (command_submitted_ || geometry_live_ || image_live_ || frame_live_) {
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

    const NativeGeometryExportRequest geometry_request =
        MakeGeometryRequest(request, *instance);
    NativeGeometryExport geometry;
    RenderOperationResult result =
        bridge_->AcquireGeometry(geometry_request, geometry);
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
    submission_kind_ = SubmissionKind::N2_PROBE;
    command_buffer_ = command_buffer;
    completion_ = completion;
    result_buffer_ = result_buffer;
    blas_ = blas;
    tlas_ = tlas;
    scratch_ = scratch;
    instance_buffer_ = instance_buffer;
    [command_buffer commit];

    if (dispatch_semaphore_wait(
            completion_, Deadline(kDispatchTimeoutNanoseconds)) != 0) {
      return Failure(
          RenderOperationCode::TIMEOUT,
          "Metal N2 dispatch exceeded five seconds; live leases were retained for retryable Shutdown");
    }
    completion_observed_ = true;
    return CompleteDispatch(geometry_request, vertex_buffer, index_buffer,
                            blas_sizes, tlas_sizes);
  }

  RenderOperationResult Render(const NativeRayTracingFrameRequest &request,
                               RenderFrameOutput &output) {
    if (!initialized_) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "Metal N2/N3 ray-tracing backend is not initialized");
    }
    if (!OnOwnerThread()) {
      return Invalid("Metal N3 render was called off its owner thread");
    }
    if (!n3_enabled_ || hybrid_pipeline_ == nil) {
      return Failure(
          RenderOperationCode::UNSUPPORTED,
          "Metal N2 is a one-ray geometry interop probe and does not export an HDR image");
    }
    if (command_submitted_ || geometry_live_ || image_live_ || frame_live_) {
      return Failure(RenderOperationCode::OUTSTANDING_LEASES,
                     "Metal N3 still owns a prior native submission or lease");
    }
    const ValidationResult request_validation =
        ValidateNativeRayTracingFrameRequest(request);
    if (!request_validation) {
      return Invalid("invalid Metal N3 request: " + request_validation.field +
                     ": " + request_validation.detail);
    }
    if (request.frame.views.size() != 1U ||
        request.frame.requested_outputs != FrameOutputMask::COLOR ||
        request.frame.color_format != PixelFormat::RGBA16_FLOAT ||
        request.frame.allow_async_compute || request.samples_per_pixel != 1U ||
        request.maximum_bounces != 1U || request.denoise) {
      return Failure(
          RenderOperationCode::UNSUPPORTED,
          "Metal N3 currently requires one synchronous RGBA16_FLOAT colour view, one sample, one primary-ray bounce, and denoising disabled");
    }
    const SceneSnapshot &snapshot = *request.frame.scene_snapshot;
    if (snapshot.mesh_instances().size() != 1U) {
      return Failure(RenderOperationCode::UNSUPPORTED,
                     "Metal N3 currently supports exactly one scene instance");
    }
    const MeshInstanceDescriptor &instance = snapshot.mesh_instances().front();
    const CameraViewRequest &view = request.frame.views.front();
    if ((instance.visibility_mask & view.visibility_mask) == 0U) {
      return Failure(RenderOperationCode::UNSUPPORTED,
                     "Metal N3 requires its single instance to be visible in the requested view");
    }

    const NativeGeometryExportRequest geometry_request =
        MakeGeometryRequest(request, instance);
    const NativeImageExportRequest image_request = MakeImageRequest(request);
    NativeGeometryExport geometry;
    RenderOperationResult result =
        bridge_->AcquireGeometry(geometry_request, geometry);
    if (!result) {
      return result;
    }
    geometry_ = geometry;
    geometry_live_ = true;
    NativeImageExport image;
    result = bridge_->AcquireImage(image_request, image);
    if (!result) {
      ReleaseGeometry();
      return result;
    }
    image_ = image;
    image_live_ = true;

    NativeFrameSynchronization synchronization;
    result = bridge_->BeginExternalFrame(request.frame.frame_id,
                                         snapshot.snapshot_id(),
                                         synchronization);
    if (!result) {
      ReleaseImage();
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
    const ValidationResult image_validation = ValidateNativeImageExport(
        image_request, image_, NativeGraphicsApi::METAL, context_.context_id);
    const ValidationResult synchronization_validation =
        ValidateNativeFrameSynchronization(synchronization_, context_, true);
    if (!geometry_validation || !image_validation ||
        !synchronization_validation ||
        synchronization_.frontend_image_release_state !=
            NativeImageState::GENERAL_READ_WRITE ||
        synchronization_.external_image_return_state !=
            NativeImageState::GENERAL_READ_WRITE ||
        !SameToken(synchronization_.interop_queue,
                   context_.graphics_queue) ||
        !SameToken(synchronization_.frontend_complete_timeline,
                   synchronization_.external_complete_timeline)) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N3 bridge generated invalid geometry, image, or shared-event synchronization");
    }

    id<MTLBuffer> vertex_buffer =
        Decode<id<MTLBuffer>>(geometry_.positions.buffer);
    id<MTLBuffer> index_buffer =
        Decode<id<MTLBuffer>>(geometry_.indices.buffer);
    id<MTLTexture> hybrid_texture = Decode<id<MTLTexture>>(image_.image);
    id<MTLSharedEvent> timeline = Decode<id<MTLSharedEvent>>(
        synchronization_.frontend_complete_timeline);
    id<MTLCommandQueue> interop_queue = Decode<id<MTLCommandQueue>>(
        synchronization_.interop_queue);
    if (vertex_buffer == nil || index_buffer == nil || hybrid_texture == nil ||
        timeline == nil || interop_queue == nil || interop_queue != queue_ ||
        interop_queue.device != device_ || vertex_buffer.device != device_ ||
        index_buffer.device != device_ || hybrid_texture.device != device_ ||
        hybrid_texture.pixelFormat != MTLPixelFormatRGBA16Float ||
        hybrid_texture.width != image_.width ||
        hybrid_texture.height != image_.height ||
        geometry_.positions.offset_bytes > vertex_buffer.length ||
        geometry_.positions.size_bytes >
            vertex_buffer.length - geometry_.positions.offset_bytes ||
        geometry_.indices.offset_bytes > index_buffer.length ||
        geometry_.indices.size_bytes >
            index_buffer.length - geometry_.indices.offset_bytes) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N3 exports do not belong to the exact Ogre device and queue");
    }

    Matrix4x4 render_from_clip;
    if (!Invert(Multiply(view.clip_from_view, view.view_from_render),
                render_from_clip)) {
      AbortBeforeSubmission();
      return Failure(RenderOperationCode::UNSUPPORTED,
                     "Metal N3 could not invert the canonical camera transform");
    }
    HybridParameters parameters;
    parameters.render_from_clip = ToSimd(render_from_clip);
    parameters.width = view.width;
    parameters.height = view.height;
    parameters.minimum_distance = 0.0001F;
    parameters.maximum_distance = view.far_plane - view.near_plane;

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
        ToMetalTransform(instance.render_from_object);
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
    MTLTextureDescriptor *contribution_descriptor =
        [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                         width:view.width
                                        height:view.height
                                     mipmapped:NO];
    contribution_descriptor.storageMode = MTLStorageModePrivate;
    contribution_descriptor.usage =
        MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    id<MTLTexture> contribution_texture =
        [device_ newTextureWithDescriptor:contribution_descriptor];

    constexpr std::uint64_t kBytesPerPixel = 8U;
    const std::uint64_t tight_row_pitch =
        static_cast<std::uint64_t>(view.width) * kBytesPerPixel;
    const std::uint64_t image_row_pitch = AlignUp(tight_row_pitch, 256U);
    if (image_row_pitch == 0U ||
        image_row_pitch >
            (std::numeric_limits<std::uint64_t>::max)() / view.height) {
      AbortBeforeSubmission();
      return Failure(RenderOperationCode::OUT_OF_MEMORY,
                     "Metal N3 readback extent overflows its byte range");
    }
    const std::uint64_t image_buffer_bytes =
        image_row_pitch * static_cast<std::uint64_t>(view.height);
    if (image_buffer_bytes >
        static_cast<std::uint64_t>((std::numeric_limits<NSUInteger>::max)())) {
      AbortBeforeSubmission();
      return Failure(RenderOperationCode::OUT_OF_MEMORY,
                     "Metal N3 readback exceeds the native address range");
    }
    id<MTLBuffer> raster_readback = [device_
        newBufferWithLength:static_cast<NSUInteger>(image_buffer_bytes)
                    options:MTLResourceStorageModeShared];
    id<MTLBuffer> contribution_readback = [device_
        newBufferWithLength:static_cast<NSUInteger>(image_buffer_bytes)
                    options:MTLResourceStorageModeShared];
    id<MTLBuffer> hybrid_readback = [device_
        newBufferWithLength:static_cast<NSUInteger>(image_buffer_bytes)
                    options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command_buffer = [queue_ commandBuffer];
    if (blas_sizes.accelerationStructureSize == 0U ||
        blas_sizes.buildScratchBufferSize == 0U ||
        tlas_sizes.accelerationStructureSize == 0U ||
        tlas_sizes.buildScratchBufferSize == 0U || blas == nil ||
        instance_buffer == nil || tlas == nil || scratch == nil ||
        contribution_texture == nil || raster_readback == nil ||
        contribution_readback == nil || hybrid_readback == nil ||
        command_buffer == nil) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N3 could not allocate its acceleration, image, or readback resources");
    }

    [command_buffer encodeWaitForEvent:timeline
                                  value:synchronization_.frontend_complete_value];
    id<MTLBlitCommandEncoder> raster_blit =
        [command_buffer blitCommandEncoder];
    if (raster_blit == nil) {
      AbortBeforeSubmission();
      return BackendFailure("Metal N3 could not create its raster blit encoder");
    }
    const MTLOrigin origin = MTLOriginMake(0U, 0U, 0U);
    const MTLSize image_size = MTLSizeMake(view.width, view.height, 1U);
    [raster_blit copyFromTexture:hybrid_texture
                     sourceSlice:0U
                     sourceLevel:0U
                    sourceOrigin:origin
                      sourceSize:image_size
                        toBuffer:raster_readback
               destinationOffset:0U
          destinationBytesPerRow:static_cast<NSUInteger>(image_row_pitch)
        destinationBytesPerImage:static_cast<NSUInteger>(image_buffer_bytes)];
    [raster_blit endEncoding];

    id<MTLAccelerationStructureCommandEncoder> acceleration_encoder =
        [command_buffer accelerationStructureCommandEncoder];
    if (acceleration_encoder == nil) {
      AbortBeforeSubmission();
      return BackendFailure(
          "Metal N3 could not create an acceleration-structure encoder");
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
      return BackendFailure("Metal N3 could not create its compute encoder");
    }
    [compute_encoder setComputePipelineState:hybrid_pipeline_];
    [compute_encoder setAccelerationStructure:tlas atBufferIndex:0U];
    [compute_encoder setBytes:&parameters
                       length:sizeof(parameters)
                      atIndex:1U];
    [compute_encoder setTexture:hybrid_texture atIndex:0U];
    [compute_encoder setTexture:contribution_texture atIndex:1U];
    const NSUInteger thread_width = hybrid_pipeline_.threadExecutionWidth;
    const NSUInteger thread_height = std::max<NSUInteger>(
        1U, hybrid_pipeline_.maxTotalThreadsPerThreadgroup / thread_width);
    [compute_encoder dispatchThreads:image_size
                threadsPerThreadgroup:MTLSizeMake(thread_width, thread_height,
                                                  1U)];
    [compute_encoder endEncoding];

    id<MTLBlitCommandEncoder> output_blit =
        [command_buffer blitCommandEncoder];
    if (output_blit == nil) {
      AbortBeforeSubmission();
      return BackendFailure("Metal N3 could not create its output blit encoder");
    }
    [output_blit copyFromTexture:contribution_texture
                     sourceSlice:0U
                     sourceLevel:0U
                    sourceOrigin:origin
                      sourceSize:image_size
                        toBuffer:contribution_readback
               destinationOffset:0U
          destinationBytesPerRow:static_cast<NSUInteger>(image_row_pitch)
        destinationBytesPerImage:static_cast<NSUInteger>(image_buffer_bytes)];
    [output_blit copyFromTexture:hybrid_texture
                     sourceSlice:0U
                     sourceLevel:0U
                    sourceOrigin:origin
                      sourceSize:image_size
                        toBuffer:hybrid_readback
               destinationOffset:0U
          destinationBytesPerRow:static_cast<NSUInteger>(image_row_pitch)
        destinationBytesPerImage:static_cast<NSUInteger>(image_buffer_bytes)];
    [output_blit endEncoding];
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
          "Metal N3 lifecycle rejected its fully encoded submission");
    }
    command_submitted_ = true;
    submission_kind_ = SubmissionKind::N3_HYBRID;
    command_buffer_ = command_buffer;
    completion_ = completion;
    blas_ = blas;
    tlas_ = tlas;
    scratch_ = scratch;
    instance_buffer_ = instance_buffer;
    contribution_texture_ = contribution_texture;
    raster_readback_buffer_ = raster_readback;
    contribution_readback_buffer_ = contribution_readback;
    hybrid_readback_buffer_ = hybrid_readback;
    [command_buffer commit];

    if (dispatch_semaphore_wait(
            completion_, Deadline(kDispatchTimeoutNanoseconds)) != 0) {
      return Failure(
          RenderOperationCode::TIMEOUT,
          "Metal N3 dispatch exceeded five seconds; live leases were retained for retryable Shutdown");
    }
    completion_observed_ = true;
    return CompleteHybrid(request, geometry_request, image_request,
                          image_row_pitch, blas_sizes, tlas_sizes, output);
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
          (dispatch_result.code == RenderOperationCode::DEVICE_LOST ||
           dispatch_result.code == RenderOperationCode::TIMEOUT)) {
        return AbandonAfterFault(std::move(dispatch_result));
      }
      if (!external_completed_) {
        const RenderOperationResult completed =
            bridge_->MarkExternalCompleted(synchronization_);
        if (!completed) {
          return AbandonAfterFault(completed);
        }
        external_completed_ = true;
      }
    }
    if (frame_live_) {
      const RenderOperationResult ended =
          bridge_->EndExternalFrame(synchronization_);
      if (!ended) {
        return AbandonAfterFault(ended);
      }
      frame_live_ = false;
    }
    ReleaseImage();
    ReleaseGeometry();
    bridge_->SetRayTracingProof(false, false);
    const RenderOperationResult unregistered =
        bridge_->UnregisterRayTracingBackend();
    if (!unregistered) {
      return AbandonAfterFault(unregistered);
    }
    ResetNativeState();
    initialized_ = false;
    bridge_.reset();
    context_ = {};
    return dispatch_result;
  }

  const OgreNextMetalRayTracingEvidence &evidence() const noexcept {
    return evidence_;
  }

#if defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
  RenderOperationResult InjectObservationForTesting(
      OgreNextMetalN2TestObservation observation) {
    if (!initialized_) {
      return Failure(RenderOperationCode::NOT_INITIALIZED,
                     "Metal N2 test observation requires an initialized backend");
    }
    if (!OnOwnerThread()) {
      return Invalid("Metal N2 test observation was set off its owner thread");
    }
    if (command_submitted_ || geometry_live_ || image_live_ || frame_live_) {
      return Failure(
          RenderOperationCode::OUTSTANDING_LEASES,
          "Metal N2 test observation must be set before native submission");
    }
    test_observation_ = observation;
    return RenderOperationResult::Success();
  }
#endif

  void BestEffortDestructorShutdown() noexcept {
    if (!initialized_ || !OnOwnerThread()) {
      return;
    }
    const RenderOperationResult result = Shutdown(kDispatchTimeoutNanoseconds);
    if (initialized_) {
      static_cast<void>(AbandonAfterFault(result));
    }
  }

private:
  bool OnOwnerThread() const noexcept {
    return std::this_thread::get_id() == owner_thread_;
  }

  RenderOperationResult
  AbandonAfterFault(RenderOperationResult cause) noexcept {
    evidence_.dispatch_readback_passed = false;
    evidence_.geometry_interop_passed = false;
    evidence_.hit_magic = 0U;
    evidence_.hit_distance = -1.0F;
    evidence_.probe_readback_bytes.clear();
    evidence_.raster_readback_bytes.clear();
    evidence_.contribution_readback_bytes.clear();
    evidence_.hybrid_readback_bytes.clear();
    evidence_.contribution_pixel_count = 0U;
    evidence_.exact_exported_color_image_used = false;
    evidence_.image_state_handoff_passed = false;
    evidence_.view_dependent_image_passed = false;
    evidence_.hybrid_composite_passed = false;
    if (bridge_) {
      bridge_->SetRayTracingProof(false, false);
      const RenderOperationResult abandoned =
          bridge_->AbandonRayTracingBackendAfterFault();
      if (!abandoned && cause.ok()) {
        cause = abandoned;
      }
    }
    ResetNativeState();
    initialized_ = false;
    bridge_.reset();
    context_ = {};
    return cause;
  }

  void AbortBeforeSubmission() noexcept {
    if (frame_live_ && !command_submitted_) {
      static_cast<void>(
          bridge_->AbortExternalFrameBeforeSubmission(synchronization_));
      frame_live_ = false;
    }
    ReleaseImage();
    ReleaseGeometry();
  }

  void ReleaseGeometry() noexcept {
    if (geometry_live_) {
      bridge_->ReleaseGeometry(geometry_.export_id);
      geometry_live_ = false;
      geometry_ = {};
    }
  }

  void ReleaseImage() noexcept {
    if (image_live_) {
      bridge_->ReleaseImage(image_.export_id);
      image_live_ = false;
      image_ = {};
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
#if defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
    // The acceptance smoke still encodes, commits, waits for, and validates a
    // real Metal completion before this narrow seam overrides its observation.
    if (test_observation_ == OgreNextMetalN2TestObservation::DEVICE_LOST) {
      return Failure(RenderOperationCode::DEVICE_LOST,
                     "injected post-submission Metal N2 device loss");
    }
    if (test_observation_ == OgreNextMetalN2TestObservation::TIMEOUT) {
      return Failure(RenderOperationCode::TIMEOUT,
                     "injected post-submission Metal N2 observation timeout");
    }
#endif
    if (submission_kind_ == SubmissionKind::N3_HYBRID) {
      if (raster_readback_buffer_ == nil ||
          contribution_readback_buffer_ == nil ||
          hybrid_readback_buffer_ == nil ||
          raster_readback_buffer_.contents == nullptr ||
          contribution_readback_buffer_.contents == nullptr ||
          hybrid_readback_buffer_.contents == nullptr) {
        return BackendFailure(
            "submitted Metal N3 command has incomplete image readbacks");
      }
      return RenderOperationResult::Success();
    }
    if (submission_kind_ != SubmissionKind::N2_PROBE ||
        result_buffer_ == nil || result_buffer_.contents == nullptr ||
        result_buffer_.length < sizeof(ProbeResult)) {
      return BackendFailure(
          "submitted Metal N2 command has no complete probe readback");
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

  RenderOperationResult FinalizeCompletedFrameAndRelease() {
    RenderOperationResult result =
        bridge_->MarkExternalCompleted(synchronization_);
    if (!result) {
      return AbandonAfterFault(BackendFailure(
          "Metal N3 lifecycle rejected its completed command buffer"));
    }
    external_completed_ = true;
    result = bridge_->EndExternalFrame(synchronization_);
    if (!result) {
      return AbandonAfterFault(result);
    }
    frame_live_ = false;
    ReleaseImage();
    ReleaseGeometry();
    return RenderOperationResult::Success();
  }

  bool CopyTightReadback(id<MTLBuffer> source,
                         std::uint64_t source_row_pitch,
                         std::uint32_t width, std::uint32_t height,
                         std::vector<std::uint8_t> &destination) const {
    constexpr std::uint64_t kBytesPerPixel = 8U;
    const std::uint64_t tight_row_pitch =
        static_cast<std::uint64_t>(width) * kBytesPerPixel;
    if (source == nil || source.contents == nullptr ||
        source_row_pitch < tight_row_pitch ||
        source_row_pitch >
            (std::numeric_limits<std::uint64_t>::max)() / height ||
        source.length < source_row_pitch * height ||
        tight_row_pitch >
            (std::numeric_limits<std::uint64_t>::max)() / height) {
      return false;
    }
    destination.resize(static_cast<std::size_t>(tight_row_pitch * height));
    const auto *source_bytes =
        static_cast<const std::uint8_t *>(source.contents);
    for (std::uint32_t row = 0U; row < height; ++row) {
      std::memcpy(destination.data() +
                      static_cast<std::size_t>(row) * tight_row_pitch,
                  source_bytes +
                      static_cast<std::size_t>(row) * source_row_pitch,
                  static_cast<std::size_t>(tight_row_pitch));
    }
    return true;
  }

  bool ValidateHybridReadbacks(std::uint64_t row_pitch,
                               std::uint32_t width, std::uint32_t height,
                               std::uint64_t &contribution_pixels) const {
    constexpr std::size_t kBytesPerPixel = 8U;
    const std::uint64_t required_bytes =
        row_pitch * static_cast<std::uint64_t>(height);
    if (raster_readback_buffer_ == nil ||
        contribution_readback_buffer_ == nil ||
        hybrid_readback_buffer_ == nil ||
        raster_readback_buffer_.contents == nullptr ||
        contribution_readback_buffer_.contents == nullptr ||
        hybrid_readback_buffer_.contents == nullptr ||
        raster_readback_buffer_.length < required_bytes ||
        contribution_readback_buffer_.length < required_bytes ||
        hybrid_readback_buffer_.length < required_bytes) {
      return false;
    }
    const auto *raster = static_cast<const std::uint8_t *>(
        raster_readback_buffer_.contents);
    const auto *contribution = static_cast<const std::uint8_t *>(
        contribution_readback_buffer_.contents);
    const auto *hybrid = static_cast<const std::uint8_t *>(
        hybrid_readback_buffer_.contents);
    contribution_pixels = 0U;
    std::uint64_t untouched_pixels = 0U;
    for (std::uint32_t row = 0U; row < height; ++row) {
      for (std::uint32_t column = 0U; column < width; ++column) {
        const std::size_t offset =
            static_cast<std::size_t>(row) * row_pitch +
            static_cast<std::size_t>(column) * kBytesPerPixel;
        std::uint16_t raster_channels[4U] = {};
        std::uint16_t contribution_channels[4U] = {};
        std::uint16_t hybrid_channels[4U] = {};
        std::memcpy(raster_channels, raster + offset, kBytesPerPixel);
        std::memcpy(contribution_channels, contribution + offset,
                    kBytesPerPixel);
        std::memcpy(hybrid_channels, hybrid + offset, kBytesPerPixel);
        bool applies = false;
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
          if (!IsFiniteHalf(raster_channels[channel]) ||
              !IsFiniteHalf(contribution_channels[channel]) ||
              !IsFiniteHalf(hybrid_channels[channel])) {
            return false;
          }
          if (channel < 3U &&
              IsNonzeroHalf(contribution_channels[channel])) {
            applies = true;
          }
        }
        if (contribution_channels[3U] != 0U ||
            hybrid_channels[3U] != raster_channels[3U]) {
          return false;
        }
        if (applies) {
          ++contribution_pixels;
          if (std::memcmp(raster + offset, hybrid + offset, 6U) == 0) {
            return false;
          }
        } else {
          ++untouched_pixels;
          if (std::memcmp(raster + offset, hybrid + offset,
                          kBytesPerPixel) != 0) {
            return false;
          }
        }
      }
    }
    return contribution_pixels != 0U && untouched_pixels != 0U;
  }

  RenderOperationResult CompleteHybrid(
      const NativeRayTracingFrameRequest &request,
      const NativeGeometryExportRequest &geometry_request,
      const NativeImageExportRequest &image_request,
      std::uint64_t image_row_pitch,
      const MTLAccelerationStructureSizes &blas_sizes,
      const MTLAccelerationStructureSizes &tlas_sizes,
      RenderFrameOutput &output) {
    RenderOperationResult result = ObserveSubmittedCommand();
    if (!result) {
      return result;
    }
    const NativeGeometryExport used_geometry = geometry_;
    const NativeImageExport used_image = image_;
    const NativeFrameSynchronization used_synchronization = synchronization_;
    const std::uint64_t vertex_buffer_length =
        Decode<id<MTLBuffer>>(used_geometry.positions.buffer).length;
    const std::uint64_t index_buffer_length =
        Decode<id<MTLBuffer>>(used_geometry.indices.buffer).length;
    const std::uint32_t width = request.frame.views.front().width;
    const std::uint32_t height = request.frame.views.front().height;
    std::uint64_t contribution_pixels = 0U;
    if (!ValidateHybridReadbacks(image_row_pitch, width, height,
                                 contribution_pixels)) {
      const RenderOperationResult finalized =
          FinalizeCompletedFrameAndRelease();
      ClearTransientSubmissionState();
      return finalized
                 ? BackendFailure(
                       "Metal N3 image readbacks were non-finite, empty, or changed pixels outside the traced contribution")
                 : finalized;
    }

    std::vector<std::uint8_t> raster;
    std::vector<std::uint8_t> contribution;
    std::vector<std::uint8_t> hybrid;
    OgreNextMetalRayTracingEvidence evidence_candidate = evidence_;
    RenderFrameOutput output_candidate;
    try {
      if (!CopyTightReadback(raster_readback_buffer_, image_row_pitch, width,
                             height, raster) ||
          !CopyTightReadback(contribution_readback_buffer_, image_row_pitch,
                             width, height, contribution) ||
          !CopyTightReadback(hybrid_readback_buffer_, image_row_pitch, width,
                             height, hybrid)) {
        const RenderOperationResult finalized =
            FinalizeCompletedFrameAndRelease();
        ClearTransientSubmissionState();
        return finalized
                   ? BackendFailure(
                         "Metal N3 could not repack its complete GPU image readbacks")
                   : finalized;
      }
      evidence_candidate.geometry_request = geometry_request;
      evidence_candidate.geometry_export = used_geometry;
      evidence_candidate.frame_synchronization = used_synchronization;
      evidence_candidate.image_request = image_request;
      evidence_candidate.image_export = used_image;
      evidence_candidate.image_frame_synchronization = used_synchronization;
      evidence_candidate.vertex_buffer_length_bytes = vertex_buffer_length;
      evidence_candidate.index_buffer_length_bytes = index_buffer_length;
      evidence_candidate.blas_bytes = blas_sizes.accelerationStructureSize;
      evidence_candidate.blas_scratch_bytes =
          blas_sizes.buildScratchBufferSize;
      evidence_candidate.tlas_bytes = tlas_sizes.accelerationStructureSize;
      evidence_candidate.tlas_scratch_bytes =
          tlas_sizes.buildScratchBufferSize;
      evidence_candidate.image_row_pitch_bytes = image_row_pitch;
      evidence_candidate.raster_readback_bytes = std::move(raster);
      evidence_candidate.contribution_readback_bytes =
          std::move(contribution);
      evidence_candidate.hybrid_readback_bytes = hybrid;
      evidence_candidate.contribution_pixel_count = contribution_pixels;
      evidence_candidate.exact_exported_vertex_slice_used = true;
      evidence_candidate.exact_exported_index_slice_used = true;
      evidence_candidate.exact_exported_color_image_used = true;
      evidence_candidate.image_state_handoff_passed = true;
      evidence_candidate.dispatch_readback_passed = true;
      evidence_candidate.geometry_interop_passed = true;
      evidence_candidate.view_dependent_image_passed = true;
      evidence_candidate.hybrid_composite_passed = true;

      output_candidate.frame_id = request.frame.frame_id;
      output_candidate.snapshot_id =
          request.frame.scene_snapshot->snapshot_id();
      output_candidate.status = RenderFrameStatus::RENDERED;
      output_candidate.presented = false;
      FrameAttachment attachment;
      attachment.view_id = request.frame.views.front().view_id;
      attachment.output = FrameOutputMask::COLOR;
      attachment.format = PixelFormat::RGBA16_FLOAT;
      attachment.width = width;
      attachment.height = height;
      attachment.row_pitch_bytes = static_cast<std::uint64_t>(width) * 8U;
      attachment.bytes = std::move(hybrid);
      output_candidate.attachments.push_back(std::move(attachment));
    } catch (const std::bad_alloc &) {
      const RenderOperationResult finalized =
          FinalizeCompletedFrameAndRelease();
      ClearTransientSubmissionState();
      return finalized
                 ? Failure(RenderOperationCode::OUT_OF_MEMORY,
                           "Metal N3 could not retain its image evidence")
                 : finalized;
    }
    const ValidationResult output_validation =
        ValidateNativeRayTracingFrameOutput(request, output_candidate);
    if (!output_validation) {
      const RenderOperationResult finalized =
          FinalizeCompletedFrameAndRelease();
      ClearTransientSubmissionState();
      return finalized
                 ? BackendFailure("Metal N3 generated invalid output: " +
                                  output_validation.field + ": " +
                                  output_validation.detail)
                 : finalized;
    }
    result = FinalizeCompletedFrameAndRelease();
    if (!result) {
      return result;
    }
    ClearTransientSubmissionState();
    evidence_ = std::move(evidence_candidate);
    bridge_->SetRayTracingProof(true, true);
    output = std::move(output_candidate);
    return RenderOperationResult::Success();
  }

  RenderOperationResult CompleteDispatch(
      const NativeGeometryExportRequest &geometry_request,
      id<MTLBuffer> vertex_buffer, id<MTLBuffer> index_buffer,
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
    try {
      const auto *begin = static_cast<const std::uint8_t *>(
          result_buffer_.contents);
      evidence_.probe_readback_bytes.assign(begin,
                                            begin + sizeof(ProbeResult));
    } catch (const std::bad_alloc &) {
      return Failure(RenderOperationCode::OUT_OF_MEMORY,
                     "Metal N2 could not retain its eight-byte probe evidence");
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
    return RenderOperationResult::Success();
  }

  void ClearTransientSubmissionState() noexcept {
    command_buffer_ = nil;
    completion_ = nullptr;
    result_buffer_ = nil;
    blas_ = nil;
    tlas_ = nil;
    scratch_ = nil;
    instance_buffer_ = nil;
    contribution_texture_ = nil;
    raster_readback_buffer_ = nil;
    contribution_readback_buffer_ = nil;
    hybrid_readback_buffer_ = nil;
    synchronization_ = {};
    geometry_ = {};
    image_ = {};
    submission_kind_ = SubmissionKind::NONE;
    command_submitted_ = false;
    completion_observed_ = false;
    external_completed_ = false;
    frame_live_ = false;
    geometry_live_ = false;
    image_live_ = false;
#if defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
    test_observation_ = OgreNextMetalN2TestObservation::NONE;
#endif
  }

  void ResetNativeState() noexcept {
    ClearTransientSubmissionState();
    pipeline_ = nil;
    hybrid_pipeline_ = nil;
    queue_ = nil;
    device_ = nil;
    n3_enabled_ = false;
  }

  std::shared_ptr<OgreNextN1NativeInteropBridge> bridge_;
  NativeContextExport context_;
  NativeGeometryExport geometry_;
  NativeImageExport image_;
  NativeFrameSynchronization synchronization_;
  OgreNextMetalRayTracingEvidence evidence_;
  id<MTLDevice> device_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  id<MTLComputePipelineState> pipeline_ = nil;
  id<MTLComputePipelineState> hybrid_pipeline_ = nil;
  id<MTLCommandBuffer> command_buffer_ = nil;
  id<MTLBuffer> result_buffer_ = nil;
  id<MTLBuffer> scratch_ = nil;
  id<MTLBuffer> instance_buffer_ = nil;
  id<MTLTexture> contribution_texture_ = nil;
  id<MTLBuffer> raster_readback_buffer_ = nil;
  id<MTLBuffer> contribution_readback_buffer_ = nil;
  id<MTLBuffer> hybrid_readback_buffer_ = nil;
  id<MTLAccelerationStructure> blas_ = nil;
  id<MTLAccelerationStructure> tlas_ = nil;
  dispatch_semaphore_t completion_ = nullptr;
  std::thread::id owner_thread_;
  SubmissionKind submission_kind_ = SubmissionKind::NONE;
  bool initialized_ = false;
  bool n3_enabled_ = false;
  bool geometry_live_ = false;
  bool image_live_ = false;
  bool frame_live_ = false;
  bool command_submitted_ = false;
  bool completion_observed_ = false;
  bool external_completed_ = false;
#if defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
  OgreNextMetalN2TestObservation test_observation_ =
      OgreNextMetalN2TestObservation::NONE;
#endif
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

RenderOperationResult OgreNextMetalRayTracingBackend::RunGeometryInteropProbe(
    const NativeRayTracingFrameRequest &request) {
  return impl_->RunGeometryInteropProbe(request);
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

#if defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
RenderOperationResult
OgreNextMetalRayTracingBackend::InjectObservationForTesting(
    OgreNextMetalN2TestObservation observation) {
  return impl_->InjectObservationForTesting(observation);
}
#endif

const OgreNextMetalRayTracingEvidence &
OgreNextMetalRayTracingBackend::evidence() const {
  return impl_->evidence();
}

} // namespace RoR::Render
