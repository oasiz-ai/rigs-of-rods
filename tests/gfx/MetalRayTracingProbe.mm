/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Executable Metal hardware ray-tracing admission probe.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <AvailabilityMacros.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

constexpr int kSkipReturnCode = 77;
constexpr std::uint32_t kHitMagic = 0x52545254u;

struct ProbeResult {
  std::uint32_t magic;
  float distance;
};

std::string JsonEscape(NSString *value) {
  if (value == nil) {
    return "";
  }

  const char *utf8 = [value UTF8String];
  if (utf8 == nullptr) {
    return "";
  }

  std::string escaped;
  for (const unsigned char ch : std::string(utf8)) {
    switch (ch) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (ch >= 0x20u) {
        escaped.push_back(static_cast<char>(ch));
      }
      break;
    }
  }
  return escaped;
}

void PrintSkip(id<MTLDevice> device, bool supports_ray_tracing,
               bool supports_apple9, const char *reason) {
  std::cout << "{\"schema\":\"ror.metal_rt_probe.v1\","
            << "\"result\":\"skip\","
            << "\"reason\":\"" << reason << "\","
            << "\"device_name\":\""
            << JsonEscape(device == nil ? nil : device.name) << "\","
            << "\"supports_raytracing\":"
            << (supports_ray_tracing ? "true" : "false") << ','
            << "\"supports_family_apple9\":"
            << (supports_apple9 ? "true" : "false") << "}\n";
}

bool CommandBufferPassed(id<MTLCommandBuffer> command_buffer,
                         const char *phase) {
  [command_buffer commit];
  [command_buffer waitUntilCompleted];
  if (command_buffer.status == MTLCommandBufferStatusCompleted) {
    return true;
  }

  std::cerr << "Metal RT probe " << phase << " failed: "
            << JsonEscape(command_buffer.error.localizedDescription) << '\n';
  return false;
}

} // namespace

int main() {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
      PrintSkip(nil, false, false, "no-metal-device");
      return kSkipReturnCode;
    }

    const bool supports_ray_tracing = device.supportsRaytracing;
    bool supports_apple9 = false;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
    if (@available(macOS 14.0, *)) {
      supports_apple9 = [device supportsFamily:MTLGPUFamilyApple9];
    }
#endif

    if (!supports_ray_tracing) {
      PrintSkip(device, false, supports_apple9,
                "metal-raytracing-api-unavailable");
      return kSkipReturnCode;
    }
    if (!supports_apple9) {
      PrintSkip(device, true, supports_apple9, "hardware-rt-floor-unavailable");
      return kSkipReturnCode;
    }

    id<MTLCommandQueue> command_queue = [device newCommandQueue];
    if (command_queue == nil) {
      std::cerr << "Metal RT probe failed to create command queue\n";
      return EXIT_FAILURE;
    }

    const MTLPackedFloat3 vertices[] = {
        MTLPackedFloat3Make(-0.5f, -0.5f, 0.0f),
        MTLPackedFloat3Make(0.5f, -0.5f, 0.0f),
        MTLPackedFloat3Make(0.0f, 0.5f, 0.0f),
    };
    id<MTLBuffer> vertex_buffer =
        [device newBufferWithBytes:vertices
                            length:sizeof(vertices)
                           options:MTLResourceStorageModeShared];
    if (vertex_buffer == nil) {
      std::cerr << "Metal RT probe failed to create vertex buffer\n";
      return EXIT_FAILURE;
    }

    MTLAccelerationStructureTriangleGeometryDescriptor *triangle_descriptor =
        [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
    triangle_descriptor.vertexBuffer = vertex_buffer;
    triangle_descriptor.vertexBufferOffset = 0;
    triangle_descriptor.vertexStride = sizeof(MTLPackedFloat3);
    triangle_descriptor.triangleCount = 1;
    triangle_descriptor.opaque = YES;

    MTLPrimitiveAccelerationStructureDescriptor *blas_descriptor =
        [MTLPrimitiveAccelerationStructureDescriptor descriptor];
    blas_descriptor.geometryDescriptors = @[ triangle_descriptor ];

    const MTLAccelerationStructureSizes blas_sizes =
        [device accelerationStructureSizesWithDescriptor:blas_descriptor];
    id<MTLAccelerationStructure> blas = [device
        newAccelerationStructureWithSize:blas_sizes.accelerationStructureSize];
    if (blas == nil || blas_sizes.accelerationStructureSize == 0 ||
        blas_sizes.buildScratchBufferSize == 0) {
      std::cerr << "Metal RT probe failed to allocate BLAS\n";
      return EXIT_FAILURE;
    }

    MTLAccelerationStructureInstanceDescriptor instance_descriptor = {};
    instance_descriptor.transformationMatrix =
        MTLPackedFloat4x3(MTLPackedFloat3Make(1.0f, 0.0f, 0.0f),
                          MTLPackedFloat3Make(0.0f, 1.0f, 0.0f),
                          MTLPackedFloat3Make(0.0f, 0.0f, 1.0f),
                          MTLPackedFloat3Make(0.0f, 0.0f, 0.0f));
    instance_descriptor.options = MTLAccelerationStructureInstanceOptionNone;
    instance_descriptor.mask = 0xffu;
    instance_descriptor.intersectionFunctionTableOffset = 0;
    instance_descriptor.accelerationStructureIndex = 0;

    id<MTLBuffer> instance_buffer =
        [device newBufferWithBytes:&instance_descriptor
                            length:sizeof(instance_descriptor)
                           options:MTLResourceStorageModeShared];
    if (instance_buffer == nil) {
      std::cerr << "Metal RT probe failed to create instance buffer\n";
      return EXIT_FAILURE;
    }

    MTLInstanceAccelerationStructureDescriptor *tlas_descriptor =
        [MTLInstanceAccelerationStructureDescriptor descriptor];
    tlas_descriptor.instanceDescriptorBuffer = instance_buffer;
    tlas_descriptor.instanceDescriptorBufferOffset = 0;
    tlas_descriptor.instanceDescriptorStride = sizeof(instance_descriptor);
    tlas_descriptor.instanceCount = 1;
    tlas_descriptor.instancedAccelerationStructures = @[ blas ];

    const MTLAccelerationStructureSizes tlas_sizes =
        [device accelerationStructureSizesWithDescriptor:tlas_descriptor];
    id<MTLAccelerationStructure> tlas = [device
        newAccelerationStructureWithSize:tlas_sizes.accelerationStructureSize];
    if (tlas == nil || tlas_sizes.accelerationStructureSize == 0 ||
        tlas_sizes.buildScratchBufferSize == 0) {
      std::cerr << "Metal RT probe failed to allocate TLAS\n";
      return EXIT_FAILURE;
    }

    const NSUInteger scratch_size = std::max(blas_sizes.buildScratchBufferSize,
                                             tlas_sizes.buildScratchBufferSize);
    id<MTLBuffer> scratch_buffer =
        [device newBufferWithLength:scratch_size
                            options:MTLResourceStorageModePrivate];
    id<MTLCommandBuffer> build_command = [command_queue commandBuffer];
    id<MTLAccelerationStructureCommandEncoder> build_encoder =
        [build_command accelerationStructureCommandEncoder];
    if (scratch_buffer == nil || build_command == nil || build_encoder == nil) {
      std::cerr << "Metal RT probe failed to create build resources\n";
      return EXIT_FAILURE;
    }

    [build_encoder buildAccelerationStructure:blas
                                   descriptor:blas_descriptor
                                scratchBuffer:scratch_buffer
                          scratchBufferOffset:0];
    [build_encoder buildAccelerationStructure:tlas
                                   descriptor:tlas_descriptor
                                scratchBuffer:scratch_buffer
                          scratchBufferOffset:0];
    [build_encoder endEncoding];
    if (!CommandBufferPassed(build_command, "BLAS/TLAS build")) {
      return EXIT_FAILURE;
    }

    static NSString *const shader_source =
        @"#include <metal_stdlib>\n"
         "#include <metal_raytracing>\n"
         "using namespace metal;\n"
         "using namespace raytracing;\n"
         "struct ProbeResult { uint magic; float distance; };\n"
         "kernel void ror_metal_rt_probe(\n"
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
      std::cerr << "Metal RT probe shader compile failed: "
                << JsonEscape(library_error.localizedDescription) << '\n';
      return EXIT_FAILURE;
    }

    id<MTLFunction> function =
        [library newFunctionWithName:@"ror_metal_rt_probe"];
    if (function == nil) {
      std::cerr << "Metal RT probe shader function was not found\n";
      return EXIT_FAILURE;
    }

    NSError *pipeline_error = nil;
    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function
                                              error:&pipeline_error];
    if (pipeline == nil) {
      std::cerr << "Metal RT probe pipeline creation failed: "
                << JsonEscape(pipeline_error.localizedDescription) << '\n';
      return EXIT_FAILURE;
    }

    const ProbeResult initial_result = {0u, -1.0f};
    id<MTLBuffer> result_buffer =
        [device newBufferWithBytes:&initial_result
                            length:sizeof(initial_result)
                           options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> trace_command = [command_queue commandBuffer];
    id<MTLComputeCommandEncoder> trace_encoder =
        [trace_command computeCommandEncoder];
    if (result_buffer == nil || trace_command == nil || trace_encoder == nil) {
      std::cerr << "Metal RT probe failed to create trace resources\n";
      return EXIT_FAILURE;
    }

    [trace_encoder setComputePipelineState:pipeline];
    [trace_encoder setAccelerationStructure:tlas atBufferIndex:0];
    [trace_encoder setBuffer:result_buffer offset:0 atIndex:1];
    [trace_encoder dispatchThreads:MTLSizeMake(1, 1, 1)
             threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
    [trace_encoder endEncoding];
    if (!CommandBufferPassed(trace_command, "intersection dispatch")) {
      return EXIT_FAILURE;
    }

    const ProbeResult result =
        *static_cast<const ProbeResult *>(result_buffer.contents);
    const bool hit_passed = result.magic == kHitMagic &&
                            std::isfinite(result.distance) &&
                            std::fabs(result.distance - 1.0f) <= 0.0001f;
    if (!hit_passed) {
      std::cerr << "Metal RT probe readback failed: magic=" << result.magic
                << " distance=" << result.distance << '\n';
      return EXIT_FAILURE;
    }

    std::cout << "{\"schema\":\"ror.metal_rt_probe.v1\","
              << "\"result\":\"pass\","
              << "\"device_name\":\"" << JsonEscape(device.name) << "\","
              << "\"supports_raytracing\":true,"
              << "\"supports_family_apple9\":"
              << (supports_apple9 ? "true" : "false") << ','
              << "\"blas_bytes\":" << blas_sizes.accelerationStructureSize
              << ','
              << "\"blas_scratch_bytes\":" << blas_sizes.buildScratchBufferSize
              << ','
              << "\"tlas_bytes\":" << tlas_sizes.accelerationStructureSize
              << ','
              << "\"tlas_scratch_bytes\":" << tlas_sizes.buildScratchBufferSize
              << ',' << "\"ray_distance\":" << result.distance << ','
              << "\"dispatch_threads\":1,"
              << "\"validated_blas\":true,"
              << "\"validated_tlas\":true,"
              << "\"validated_intersection_readback\":true"
              << "}\n";
    return EXIT_SUCCESS;
  }
}
