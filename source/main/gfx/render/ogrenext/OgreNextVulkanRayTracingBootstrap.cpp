/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextVulkanRayTracingBootstrap.h"

// The RT6 smoke executable is deliberately standalone and does not link the
// frontend library. Compile the exact renderer-neutral sample oracle into this
// isolated binary instead of cloning its RGBA16 mapping rules here.
#include "NativeDirectionalShadowContract.cpp"

#if !defined(ROR_OGRE_NEXT_N1_VULKAN)
#error "The RT6 ray-tracing bootstrap is reviewed only for Linux Vulkan"
#endif

#include "OgreVulkanDevice.h"
#include "OgreVulkanRenderSystem.h"

#include <shaderc/shaderc.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace RoR::Render {
namespace {

constexpr std::uint64_t kTimelineWaitNanoseconds = 10'000'000'000ULL;
constexpr VkFormat kOutputFormat = VK_FORMAT_R32G32B32A32_UINT;
constexpr std::array<std::uint32_t, 4U> kExpectedPrimaryHit = {
    0x52543601U, 0x13579bdfU, 0x2468ace0U, 0x00000001U};
constexpr std::uint32_t kSemanticSampleCount = 2U;
constexpr std::uint32_t kReceiverInstanceId = 1U;
constexpr std::uint32_t kOccluderInstanceId = 2U;
constexpr std::array<std::uint16_t, 4U> kSemanticRasterRgba16 = {
    0x3800U, 0x3400U, 0x3a00U, 0x3c00U};
constexpr std::array<const char*, 4U> kRequiredDeviceExtensions = {
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
};

constexpr const char* kRayGenerationShader = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 0, binding = 1, rgba32ui) uniform uimage2D output_image;
layout(set = 0, binding = 2, std430) buffer SemanticOutput {
  uint visibility_r16_bits[2];
  uint lineage_r32[2];
  uvec2 raster_rgba16_words[2];
  uvec2 hybrid_rgba16_words[2];
} semantic_output;
layout(location = 0) rayPayloadEXT uvec4 payload;
void main() {
  // Preserve the original RT6 1x1 primary-hit proof byte-for-byte.
  payload = uvec4(0u);
  traceRayEXT(scene,
              gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
              0x01, 0, 0, 0,
              vec3(0.0, 0.0, 1.0), 0.001,
              vec3(0.0, 0.0, -1.0), 100.0,
              0);
  imageStore(output_image, ivec2(0, 0), payload);

  const vec4 raster = vec4(0.5, 0.25, 0.75, 1.0);
  const vec3 directional_light_direction = vec3(0.0, 0.0, 1.0);
  for (uint sample_index = 0u; sample_index < 2u; ++sample_index) {
    const float sample_x = sample_index == 0u ? -0.5 : 0.5;
    payload = uvec4(0u);
    traceRayEXT(scene,
                gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                0x01, 0, 0, 0,
                vec3(sample_x, 0.0, 1.0), 0.001,
                vec3(0.0, 0.0, -1.0), 100.0,
                0);
    const bool receiver_hit =
        all(equal(payload,
                  uvec4(0x52543601u, 0x13579bdfu, 0x2468ace0u, 1u)));

    // The initialized payload is the canonical visible/miss result. The
    // occluder closest-hit shader changes it to occluded and adds lineage bit 2.
    payload = uvec4(0x3c00u, receiver_hit ? 1u : 0u, 0u, 0x4e344101u);
    traceRayEXT(scene,
                gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                0x02, 0, 0, 0,
                vec3(sample_x, 0.0, 0.001), 0.001,
                directional_light_direction, 100.0,
                0);

    const float visibility = payload.z == 2u ? 0.0 : 1.0;
    const vec4 hybrid = payload.z == 2u
                            ? vec4(0.0, 0.0, 0.0, raster.a)
                            : raster;
    semantic_output.visibility_r16_bits[sample_index] =
        packHalf2x16(vec2(visibility, 0.0)) & 0xffffu;
    semantic_output.lineage_r32[sample_index] = payload.y;
    semantic_output.raster_rgba16_words[sample_index] =
        uvec2(packHalf2x16(raster.rg), packHalf2x16(raster.ba));
    semantic_output.hybrid_rgba16_words[sample_index] =
        uvec2(packHalf2x16(hybrid.rg), packHalf2x16(hybrid.ba));
  }
}
)glsl";

constexpr const char* kClosestHitShader = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT uvec4 payload;
void main() {
  if (uint(gl_InstanceCustomIndexEXT) == 1u) {
    payload = uvec4(0x52543601u, 0x13579bdfu, 0x2468ace0u, 1u);
  } else if (uint(gl_InstanceCustomIndexEXT) == 2u) {
    payload.x = 0u;
    payload.y |= 2u;
    payload.z = 2u;
  } else {
    payload = uvec4(0xffffffffu);
  }
}
)glsl";

constexpr const char* kMissShader = R"glsl(
#version 460
#extension GL_EXT_ray_tracing : require
layout(location = 0) rayPayloadInEXT uvec4 payload;
void main() {
  // Preserve the ray-generation shader's initialized miss value. The original
  // primary ray initializes zero; the N4A secondary ray initializes visible.
}
)glsl";

VulkanRt6BootstrapResult Ready() { return {VulkanRt6BootstrapCode::READY, {}}; }

VulkanRt6BootstrapResult Unsupported(std::string message) {
  return {VulkanRt6BootstrapCode::UNSUPPORTED, std::move(message)};
}

VulkanRt6BootstrapResult Failure(std::string message) {
  return {VulkanRt6BootstrapCode::FAILURE, std::move(message)};
}

std::string VkFailure(const char* operation, VkResult result) {
  std::ostringstream message;
  message << operation << " failed with VkResult " << static_cast<int>(result);
  return message.str();
}

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

bool IsKnownSoftwareAdapter(const std::string& name,
                            VulkanRt5DeviceClass device_class) {
  if (device_class == VulkanRt5DeviceClass::CPU) {
    return true;
  }
  const std::string lower_name = Lowercase(name);
  for (const char* marker :
       {"lavapipe", "llvmpipe", "swiftshader", "software rasterizer",
        "software renderer", "microsoft basic render"}) {
    if (lower_name.find(marker) != std::string::npos) {
      return true;
    }
  }
  return false;
}

VulkanRt5DeviceClass MapDeviceClass(VkPhysicalDeviceType type) {
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return VulkanRt5DeviceClass::INTEGRATED_GPU;
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return VulkanRt5DeviceClass::DISCRETE_GPU;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return VulkanRt5DeviceClass::VIRTUAL_GPU;
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return VulkanRt5DeviceClass::CPU;
  default:
    return VulkanRt5DeviceClass::OTHER;
  }
}

std::string HexUuid(const std::uint8_t* bytes, std::size_t count) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result(count * 2U, '0');
  for (std::size_t index = 0U; index < count; ++index) {
    result[index * 2U] = kHex[(bytes[index] >> 4U) & 0x0fU];
    result[index * 2U + 1U] = kHex[bytes[index] & 0x0fU];
  }
  return result;
}

bool IsPowerOfTwo(std::uint32_t value) {
  return value > 0U && (value & (value - 1U)) == 0U;
}

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

struct Candidate {
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceFeatures core_features{};
  VkPhysicalDeviceVulkan12Features vulkan12_features{};
  VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{};
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_pipeline_features{};
  VkPhysicalDeviceIDProperties id_properties{};
  VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_properties{};
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_pipeline_properties{};
  std::vector<VkExtensionProperties> extension_properties;
  std::uint32_t graphics_queue_family =
      std::numeric_limits<std::uint32_t>::max();
  VulkanRt6CandidateContract contract;
};

bool HasExtension(const Candidate& candidate, const char* name) {
  return std::any_of(candidate.extension_properties.begin(),
                     candidate.extension_properties.end(),
                     [name](const VkExtensionProperties& property) {
                       return std::strcmp(property.extensionName, name) == 0;
                     });
}

bool InspectCandidate(VkPhysicalDevice physical_device, Candidate& candidate,
                      std::string& error) {
  candidate.physical_device = physical_device;

  VkPhysicalDeviceProperties2 properties2{};
  properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  candidate.id_properties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
  properties2.pNext = &candidate.id_properties;
  vkGetPhysicalDeviceProperties2(physical_device, &properties2);
  candidate.properties = properties2.properties;

  std::uint32_t queue_family_count = 0U;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  if (queue_family_count > 0U) {
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device, &queue_family_count, queue_families.data());
  }
  for (std::uint32_t index = 0U; index < queue_family_count; ++index) {
    const bool graphics =
        queue_families[index].queueCount > 0U &&
        (queue_families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U;
    candidate.contract.has_graphics_queue |= graphics;
    if (graphics &&
        (queue_families[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U) {
      candidate.graphics_queue_family = index;
      candidate.contract.has_compute_on_graphics_queue = true;
      break;
    }
  }

  std::uint32_t extension_count = 0U;
  VkResult result = vkEnumerateDeviceExtensionProperties(
      physical_device, nullptr, &extension_count, nullptr);
  if (result != VK_SUCCESS) {
    error = VkFailure("vkEnumerateDeviceExtensionProperties", result);
    return false;
  }
  candidate.extension_properties.resize(extension_count);
  if (extension_count > 0U) {
    result = vkEnumerateDeviceExtensionProperties(
        physical_device, nullptr, &extension_count,
        candidate.extension_properties.data());
    if (result != VK_SUCCESS) {
      error = VkFailure("vkEnumerateDeviceExtensionProperties", result);
      return false;
    }
    candidate.extension_properties.resize(extension_count);
  }

  candidate.contract.api_major =
      VK_API_VERSION_MAJOR(candidate.properties.apiVersion);
  candidate.contract.api_minor =
      VK_API_VERSION_MINOR(candidate.properties.apiVersion);
  candidate.contract.device_class =
      MapDeviceClass(candidate.properties.deviceType);
  candidate.contract.known_software_adapter = IsKnownSoftwareAdapter(
      candidate.properties.deviceName, candidate.contract.device_class);
  candidate.contract.device_identity_available =
      candidate.properties.deviceName[0] != '\0' &&
      std::any_of(candidate.id_properties.deviceUUID,
                  candidate.id_properties.deviceUUID + VK_UUID_SIZE,
                  [](std::uint8_t byte) { return byte != 0U; });
  candidate.contract.deferred_host_operations_extension =
      HasExtension(candidate, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
  candidate.contract.buffer_device_address_extension =
      HasExtension(candidate, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
  candidate.contract.acceleration_structure_extension =
      HasExtension(candidate, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
  candidate.contract.ray_tracing_pipeline_extension =
      HasExtension(candidate, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

  if (candidate.contract.api_major > 1U ||
      (candidate.contract.api_major == 1U &&
       candidate.contract.api_minor >= 2U)) {
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    candidate.vulkan12_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features2.pNext = &candidate.vulkan12_features;
    void** next = &candidate.vulkan12_features.pNext;
    if (candidate.contract.acceleration_structure_extension) {
      candidate.acceleration_features.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
      *next = &candidate.acceleration_features;
      next = &candidate.acceleration_features.pNext;
    }
    if (candidate.contract.ray_tracing_pipeline_extension) {
      candidate.ray_pipeline_features.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
      *next = &candidate.ray_pipeline_features;
      next = &candidate.ray_pipeline_features.pNext;
    }
    *next = nullptr;
    vkGetPhysicalDeviceFeatures2(physical_device, &features2);
    candidate.core_features = features2.features;
    candidate.contract.timeline_semaphore_supported =
        candidate.vulkan12_features.timelineSemaphore == VK_TRUE;
    candidate.contract.buffer_device_address_feature =
        candidate.vulkan12_features.bufferDeviceAddress == VK_TRUE;
    candidate.contract.acceleration_structure_feature =
        candidate.acceleration_features.accelerationStructure == VK_TRUE;
    candidate.contract.ray_tracing_pipeline_feature =
        candidate.ray_pipeline_features.rayTracingPipeline == VK_TRUE;
  }

  VkFormatProperties format_properties{};
  vkGetPhysicalDeviceFormatProperties(physical_device, kOutputFormat,
                                      &format_properties);
  const VkFormatFeatureFlags required_format_features =
      VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
  candidate.contract.rgba32_uint_storage_image =
      (format_properties.optimalTilingFeatures & required_format_features) ==
      required_format_features;

  if (candidate.contract.acceleration_structure_extension ||
      candidate.contract.ray_tracing_pipeline_extension) {
    VkPhysicalDeviceProperties2 ray_properties2{};
    ray_properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    void** next = &ray_properties2.pNext;
    if (candidate.contract.acceleration_structure_extension) {
      candidate.acceleration_properties.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
      *next = &candidate.acceleration_properties;
      next = &candidate.acceleration_properties.pNext;
    }
    if (candidate.contract.ray_tracing_pipeline_extension) {
      candidate.ray_pipeline_properties.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
      *next = &candidate.ray_pipeline_properties;
      next = &candidate.ray_pipeline_properties.pNext;
    }
    *next = nullptr;
    vkGetPhysicalDeviceProperties2(physical_device, &ray_properties2);
  }
  const std::uint32_t handle_alignment =
      candidate.ray_pipeline_properties.shaderGroupHandleAlignment;
  const std::uint32_t handle_stride =
      IsPowerOfTwo(handle_alignment)
          ? static_cast<std::uint32_t>(
                AlignUp(candidate.ray_pipeline_properties.shaderGroupHandleSize,
                        handle_alignment))
          : 0U;
  candidate.contract.ray_tracing_properties_valid =
      candidate.ray_pipeline_properties.shaderGroupHandleSize == 32U &&
      IsPowerOfTwo(
          candidate.ray_pipeline_properties.shaderGroupHandleAlignment) &&
      IsPowerOfTwo(
          candidate.ray_pipeline_properties.shaderGroupBaseAlignment) &&
      candidate.ray_pipeline_properties.maxShaderGroupStride >= handle_stride &&
      candidate.ray_pipeline_properties.maxRayRecursionDepth >= 1U &&
      candidate.ray_pipeline_properties.maxRayDispatchInvocationCount >= 1U &&
      candidate.acceleration_properties.maxGeometryCount >= 1U &&
      candidate.acceleration_properties.maxPrimitiveCount >= 1U &&
      candidate.acceleration_properties.maxInstanceCount >= 2U &&
      candidate.acceleration_properties
              .maxDescriptorSetAccelerationStructures >= 1U &&
      IsPowerOfTwo(candidate.acceleration_properties
                       .minAccelerationStructureScratchOffsetAlignment);
  candidate.contract.every_ogre_observed_core_feature_enabled = true;
  candidate.contract.claimed_extension_set_is_exact = true;
  return true;
}

int CandidateRank(const Candidate& candidate) {
  if (EvaluateVulkanRt6Candidate(candidate.contract) !=
      VulkanRt6CandidateDecision::ACCEPT) {
    return 0;
  }
  return candidate.contract.device_class == VulkanRt5DeviceClass::DISCRETE_GPU
             ? 2
             : 1;
}

bool CompileShader(const char* source, shaderc_shader_kind kind,
                   const char* name, std::vector<std::uint32_t>& spirv,
                   std::string& error) {
  shaderc_compiler_t compiler = shaderc_compiler_initialize();
  shaderc_compile_options_t options = shaderc_compile_options_initialize();
  if (compiler == nullptr || options == nullptr) {
    if (options != nullptr) {
      shaderc_compile_options_release(options);
    }
    if (compiler != nullptr) {
      shaderc_compiler_release(compiler);
    }
    error = "shaderc could not initialize";
    return false;
  }
  shaderc_compile_options_set_source_language(options,
                                              shaderc_source_language_glsl);
  shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan,
                                         shaderc_env_version_vulkan_1_2);
  shaderc_compile_options_set_target_spirv(options, shaderc_spirv_version_1_4);
  shaderc_compile_options_set_warnings_as_errors(options);
  shaderc_compile_options_set_optimization_level(
      options, shaderc_optimization_level_performance);
  shaderc_compilation_result_t result = shaderc_compile_into_spv(
      compiler, source, std::strlen(source), kind, name, "main", options);
  if (result == nullptr) {
    error = std::string("shaderc returned no result for ") + name;
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);
    return false;
  }
  const shaderc_compilation_status status =
      shaderc_result_get_compilation_status(result);
  if (status != shaderc_compilation_status_success) {
    error = std::string("shaderc failed for ") + name + ": " +
            shaderc_result_get_error_message(result);
    shaderc_result_release(result);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);
    return false;
  }
  const std::size_t byte_count = shaderc_result_get_length(result);
  if (byte_count == 0U || byte_count % sizeof(std::uint32_t) != 0U) {
    error = std::string("shaderc returned invalid SPIR-V for ") + name;
    shaderc_result_release(result);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);
    return false;
  }
  spirv.resize(byte_count / sizeof(std::uint32_t));
  std::memcpy(spirv.data(), shaderc_result_get_bytes(result), byte_count);
  shaderc_result_release(result);
  shaderc_compile_options_release(options);
  shaderc_compiler_release(compiler);
  return true;
}

using ShaderSpirv = std::array<std::vector<std::uint32_t>, 3U>;

bool CompileRt6Shaders(ShaderSpirv& shader_spirv, std::string& error) {
  const std::array<const char*, 3U> shader_sources = {
      kRayGenerationShader, kMissShader, kClosestHitShader};
  const std::array<const char*, 3U> shader_names = {"rt6.rgen", "rt6.rmiss",
                                                    "rt6.rchit"};
  const std::array<shaderc_shader_kind, 3U> shader_kinds = {
      shaderc_raygen_shader, shaderc_miss_shader, shaderc_closesthit_shader};
  for (std::size_t index = 0U; index < shader_sources.size(); ++index) {
    if (!CompileShader(shader_sources[index], shader_kinds[index],
                       shader_names[index], shader_spirv[index], error)) {
      return false;
    }
  }
  return true;
}

struct OwnedBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0U;
};

struct SemanticGpuOutput {
  std::array<std::uint32_t, kSemanticSampleCount> visibility_r16_bits{};
  std::array<std::uint32_t, kSemanticSampleCount> lineage_r32{};
  std::array<std::array<std::uint32_t, 2U>, kSemanticSampleCount>
      raster_rgba16_words{};
  std::array<std::array<std::uint32_t, 2U>, kSemanticSampleCount>
      hybrid_rgba16_words{};
};

static_assert(sizeof(SemanticGpuOutput) == 48U,
              "N4A semantic storage-buffer layout must remain std430 exact");

}  // namespace

struct OgreNextVulkanRayTracingBootstrap::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphics_queue = VK_NULL_HANDLE;
  VkSemaphore timeline = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memory_properties{};
  Ogre::VulkanExternalInstance external_instance{};
  Ogre::VulkanExternalDevice external_device{};
  VulkanRt6LifecycleContract lifecycle;
  VulkanRt6BootstrapEvidence evidence;
  bool ogre_attached = false;

  PFN_vkCreateAccelerationStructureKHR create_acceleration_structure = nullptr;
  PFN_vkDestroyAccelerationStructureKHR destroy_acceleration_structure =
      nullptr;
  PFN_vkGetAccelerationStructureBuildSizesKHR get_build_sizes = nullptr;
  PFN_vkCmdBuildAccelerationStructuresKHR cmd_build_acceleration_structures =
      nullptr;
  PFN_vkGetAccelerationStructureDeviceAddressKHR get_acceleration_address =
      nullptr;
  PFN_vkCreateRayTracingPipelinesKHR create_ray_pipelines = nullptr;
  PFN_vkGetRayTracingShaderGroupHandlesKHR get_shader_group_handles = nullptr;
  PFN_vkCmdTraceRaysKHR cmd_trace_rays = nullptr;

  OwnedBuffer geometry_buffer;
  OwnedBuffer instance_buffer;
  OwnedBuffer blas_storage;
  OwnedBuffer semantic_occluder_blas_storage;
  OwnedBuffer tlas_storage;
  OwnedBuffer scratch_buffer;
  OwnedBuffer shader_binding_table;
  OwnedBuffer readback_buffer;
  OwnedBuffer semantic_output_buffer;
  OwnedBuffer semantic_readback_buffer;
  VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
  VkAccelerationStructureKHR semantic_occluder_blas = VK_NULL_HANDLE;
  VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
  VkImage output_image = VK_NULL_HANDLE;
  VkDeviceMemory output_image_memory = VK_NULL_HANDLE;
  VkImageView output_image_view = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline ray_pipeline = VK_NULL_HANDLE;
  std::array<VkShaderModule, 3U> shader_modules{};
  ShaderSpirv shader_spirv;
  VkCommandPool command_pool = VK_NULL_HANDLE;

  void RecordCandidate(const Candidate& candidate,
                       VulkanRt6CandidateDecision decision) {
    evidence.physical_device_api_version = candidate.properties.apiVersion;
    evidence.driver_version = candidate.properties.driverVersion;
    evidence.vendor_id = candidate.properties.vendorID;
    evidence.device_id = candidate.properties.deviceID;
    evidence.device_name = candidate.properties.deviceName;
    evidence.device_uuid =
        HexUuid(candidate.id_properties.deviceUUID, VK_UUID_SIZE);
    evidence.device_class = candidate.contract.device_class;
    evidence.candidate_decision = decision;
    evidence.known_software_adapter = candidate.contract.known_software_adapter;
    evidence.device_identity_available =
        candidate.contract.device_identity_available;
    evidence.graphics_queue_available = candidate.contract.has_graphics_queue;
    evidence.compute_on_graphics_queue_available =
        candidate.contract.has_compute_on_graphics_queue;
    evidence.timeline_semaphore_supported =
        candidate.contract.timeline_semaphore_supported;
    evidence.deferred_host_operations_extension_supported =
        candidate.contract.deferred_host_operations_extension;
    evidence.buffer_device_address_extension_supported =
        candidate.contract.buffer_device_address_extension;
    evidence.acceleration_structure_extension_supported =
        candidate.contract.acceleration_structure_extension;
    evidence.ray_tracing_pipeline_extension_supported =
        candidate.contract.ray_tracing_pipeline_extension;
    evidence.buffer_device_address_feature_supported =
        candidate.contract.buffer_device_address_feature;
    evidence.acceleration_structure_feature_supported =
        candidate.contract.acceleration_structure_feature;
    evidence.ray_tracing_pipeline_feature_supported =
        candidate.contract.ray_tracing_pipeline_feature;
    evidence.output_storage_image_format_supported =
        candidate.contract.rgba32_uint_storage_image;
    evidence.ray_tracing_properties_valid =
        candidate.contract.ray_tracing_properties_valid;
    evidence.shader_group_handle_size =
        candidate.ray_pipeline_properties.shaderGroupHandleSize;
    evidence.shader_group_handle_alignment =
        candidate.ray_pipeline_properties.shaderGroupHandleAlignment;
    evidence.shader_group_base_alignment =
        candidate.ray_pipeline_properties.shaderGroupBaseAlignment;
    evidence.max_ray_recursion_depth =
        candidate.ray_pipeline_properties.maxRayRecursionDepth;
    evidence.acceleration_structure_scratch_alignment =
        candidate.acceleration_properties
            .minAccelerationStructureScratchOffsetAlignment;
    evidence.graphics_queue_family =
        candidate.contract.has_compute_on_graphics_queue
            ? candidate.graphics_queue_family
            : 0U;
    evidence.graphics_queue_index = 0U;
  }

  bool FindMemoryType(std::uint32_t type_bits, VkMemoryPropertyFlags required,
                      std::uint32_t& index) const {
    for (std::uint32_t candidate = 0U;
         candidate < memory_properties.memoryTypeCount; ++candidate) {
      const bool allowed = (type_bits & (1U << candidate)) != 0U;
      const VkMemoryPropertyFlags flags =
          memory_properties.memoryTypes[candidate].propertyFlags;
      if (allowed && (flags & required) == required) {
        index = candidate;
        return true;
      }
    }
    return false;
  }

  VulkanRt6BootstrapResult CreateBuffer(VkDeviceSize size,
                                        VkBufferUsageFlags usage,
                                        VkMemoryPropertyFlags memory_flags,
                                        bool device_address,
                                        OwnedBuffer& output) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result =
        vkCreateBuffer(device, &buffer_info, nullptr, &output.buffer);
    if (result != VK_SUCCESS) {
      return Failure(VkFailure("vkCreateBuffer", result));
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, output.buffer, &requirements);
    std::uint32_t memory_type = 0U;
    if (!FindMemoryType(requirements.memoryTypeBits, memory_flags,
                        memory_type)) {
      return Failure("no Vulkan memory type satisfies the RT6 buffer policy");
    }
    VkMemoryAllocateFlagsInfo allocate_flags{};
    allocate_flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocate_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = device_address ? &allocate_flags : nullptr;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;
    result = vkAllocateMemory(device, &allocate_info, nullptr, &output.memory);
    if (result != VK_SUCCESS) {
      return Failure(VkFailure("vkAllocateMemory(buffer)", result));
    }
    result = vkBindBufferMemory(device, output.buffer, output.memory, 0U);
    if (result != VK_SUCCESS) {
      return Failure(VkFailure("vkBindBufferMemory", result));
    }
    output.size = size;
    return Ready();
  }

  VulkanRt6BootstrapResult Upload(const OwnedBuffer& buffer, const void* data,
                                  std::size_t size) {
    if (size > buffer.size) {
      return Failure("RT6 upload exceeded its owned buffer");
    }
    void* mapped = nullptr;
    const VkResult result =
        vkMapMemory(device, buffer.memory, 0U, size, 0U, &mapped);
    if (result != VK_SUCCESS) {
      return Failure(VkFailure("vkMapMemory(upload)", result));
    }
    std::memcpy(mapped, data, size);
    vkUnmapMemory(device, buffer.memory);
    return Ready();
  }

  VkDeviceAddress BufferAddress(const OwnedBuffer& buffer) const {
    VkBufferDeviceAddressInfo address_info{};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = buffer.buffer;
    return vkGetBufferDeviceAddress(device, &address_info);
  }

  void DestroyBuffer(OwnedBuffer& buffer) noexcept {
    if (buffer.buffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, buffer.buffer, nullptr);
      buffer.buffer = VK_NULL_HANDLE;
    }
    if (buffer.memory != VK_NULL_HANDLE) {
      vkFreeMemory(device, buffer.memory, nullptr);
      buffer.memory = VK_NULL_HANDLE;
    }
    buffer.size = 0U;
  }

  void DestroyRayResourcesRaw() noexcept {
    if (device == VK_NULL_HANDLE) {
      return;
    }
    if (command_pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, command_pool, nullptr);
      command_pool = VK_NULL_HANDLE;
    }
    if (ray_pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, ray_pipeline, nullptr);
      ray_pipeline = VK_NULL_HANDLE;
    }
    for (VkShaderModule& module : shader_modules) {
      if (module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, module, nullptr);
        module = VK_NULL_HANDLE;
      }
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
      pipeline_layout = VK_NULL_HANDLE;
    }
    if (descriptor_pool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
      descriptor_pool = VK_NULL_HANDLE;
      descriptor_set = VK_NULL_HANDLE;
    }
    if (descriptor_set_layout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
      descriptor_set_layout = VK_NULL_HANDLE;
    }
    if (output_image_view != VK_NULL_HANDLE) {
      vkDestroyImageView(device, output_image_view, nullptr);
      output_image_view = VK_NULL_HANDLE;
    }
    if (output_image != VK_NULL_HANDLE) {
      vkDestroyImage(device, output_image, nullptr);
      output_image = VK_NULL_HANDLE;
    }
    if (output_image_memory != VK_NULL_HANDLE) {
      vkFreeMemory(device, output_image_memory, nullptr);
      output_image_memory = VK_NULL_HANDLE;
    }
    if (tlas != VK_NULL_HANDLE && destroy_acceleration_structure != nullptr) {
      destroy_acceleration_structure(device, tlas, nullptr);
      tlas = VK_NULL_HANDLE;
    }
    if (blas != VK_NULL_HANDLE && destroy_acceleration_structure != nullptr) {
      destroy_acceleration_structure(device, blas, nullptr);
      blas = VK_NULL_HANDLE;
    }
    if (semantic_occluder_blas != VK_NULL_HANDLE &&
        destroy_acceleration_structure != nullptr) {
      destroy_acceleration_structure(device, semantic_occluder_blas, nullptr);
      semantic_occluder_blas = VK_NULL_HANDLE;
    }
    DestroyBuffer(shader_binding_table);
    DestroyBuffer(readback_buffer);
    DestroyBuffer(semantic_readback_buffer);
    DestroyBuffer(semantic_output_buffer);
    DestroyBuffer(scratch_buffer);
    DestroyBuffer(instance_buffer);
    DestroyBuffer(geometry_buffer);
    DestroyBuffer(tlas_storage);
    DestroyBuffer(semantic_occluder_blas_storage);
    DestroyBuffer(blas_storage);
  }

  VulkanRt6BootstrapResult DestroyPartial() noexcept {
    if (device != VK_NULL_HANDLE) {
      const VkResult idle_result = vkDeviceWaitIdle(device);
      if (idle_result != VK_SUCCESS) {
        return Failure(VkFailure("vkDeviceWaitIdle", idle_result));
      }
      DestroyRayResourcesRaw();
      if (timeline != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, timeline, nullptr);
        timeline = VK_NULL_HANDLE;
        evidence.timeline_destroyed_before_device = true;
      }
      vkDestroyDevice(device, nullptr);
      device = VK_NULL_HANDLE;
      evidence.device_destroyed_before_instance = instance != VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
      vkDestroyInstance(instance, nullptr);
      instance = VK_NULL_HANDLE;
    }
    evidence.shutdown_completed = true;
    return Ready();
  }
};

OgreNextVulkanRayTracingBootstrap::OgreNextVulkanRayTracingBootstrap()
    : impl_(std::make_unique<Impl>()) {}

OgreNextVulkanRayTracingBootstrap::~OgreNextVulkanRayTracingBootstrap() {
  if (impl_ && !impl_->ogre_attached) {
    static_cast<void>(impl_->DestroyPartial());
  }
}

VulkanRt6BootstrapResult
OgreNextVulkanRayTracingBootstrap::ValidateShaderContract() {
  if (impl_->lifecycle.stage() != VulkanRt6LifecycleStage::EMPTY ||
      impl_->evidence.shader_contract_compiled) {
    return Failure("RT6 shaders must be compiled exactly once before Vulkan "
                   "initialization");
  }
  std::string error;
  if (!CompileRt6Shaders(impl_->shader_spirv, error)) {
    return Failure(error);
  }
  impl_->evidence.shader_contract_compiled = true;
  return Ready();
}

VulkanRt6BootstrapResult OgreNextVulkanRayTracingBootstrap::Initialize() {
  if (!impl_->evidence.shader_contract_compiled) {
    return Failure("RT6 initialization requires validated SPIR-V 1.4 shaders");
  }
  if (impl_->instance != VK_NULL_HANDLE || impl_->device != VK_NULL_HANDLE) {
    return Failure("RT6 bootstrap cannot be initialized twice");
  }

  std::uint32_t loader_version = VK_API_VERSION_1_0;
  const auto enumerate_instance_version =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
          vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
  if (enumerate_instance_version != nullptr) {
    const VkResult result = enumerate_instance_version(&loader_version);
    if (result != VK_SUCCESS) {
      return Failure(VkFailure("vkEnumerateInstanceVersion", result));
    }
  }
  impl_->evidence.loader_api_version = loader_version;
  impl_->evidence.requested_instance_api_version = VK_API_VERSION_1_2;
  if (loader_version < VK_API_VERSION_1_2) {
    static_cast<void>(impl_->DestroyPartial());
    return Unsupported("Vulkan loader does not expose Vulkan 1.2");
  }

  VkApplicationInfo application_info{};
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pApplicationName = "Rigs of Rods Ogre-Next RT6";
  application_info.applicationVersion = VK_MAKE_VERSION(0, 6, 0);
  application_info.pEngineName = "Rigs of Rods";
  application_info.engineVersion = VK_MAKE_VERSION(0, 6, 0);
  application_info.apiVersion = VK_API_VERSION_1_2;
  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &application_info;
  VkResult result = vkCreateInstance(&instance_info, nullptr, &impl_->instance);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateInstance", result));
  }
  impl_->evidence.enabled_instance_extensions_exact = true;

  std::uint32_t physical_device_count = 0U;
  result = vkEnumeratePhysicalDevices(impl_->instance, &physical_device_count,
                                      nullptr);
  if (result != VK_SUCCESS) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure(VkFailure("vkEnumeratePhysicalDevices", result));
  }
  if (physical_device_count == 0U) {
    static_cast<void>(impl_->DestroyPartial());
    return Unsupported("Vulkan reported no physical devices");
  }
  std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
  result = vkEnumeratePhysicalDevices(impl_->instance, &physical_device_count,
                                      physical_devices.data());
  if (result != VK_SUCCESS) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure(VkFailure("vkEnumeratePhysicalDevices", result));
  }
  physical_devices.resize(physical_device_count);
  if (physical_devices.empty()) {
    static_cast<void>(impl_->DestroyPartial());
    return Unsupported("Vulkan physical-device enumeration became empty");
  }

  Candidate selected;
  Candidate fallback;
  bool have_fallback = false;
  int selected_rank = 0;
  for (VkPhysicalDevice physical_device : physical_devices) {
    Candidate candidate;
    std::string inspection_error;
    if (!InspectCandidate(physical_device, candidate, inspection_error)) {
      static_cast<void>(impl_->DestroyPartial());
      return Failure(inspection_error);
    }
    if (!have_fallback) {
      fallback = candidate;
      have_fallback = true;
    }
    const int rank = CandidateRank(candidate);
    if (rank > selected_rank) {
      selected = candidate;
      selected_rank = rank;
    }
  }
  if (selected_rank == 0) {
    const VulkanRt6CandidateDecision decision =
        EvaluateVulkanRt6Candidate(fallback.contract);
    impl_->RecordCandidate(fallback, decision);
    static_cast<void>(impl_->DestroyPartial());
    return Unsupported(std::string("no attested RT6 hardware device: ") +
                       VulkanRt6CandidateDecisionName(decision));
  }
  const VulkanRt6CandidateDecision decision =
      EvaluateVulkanRt6Candidate(selected.contract);
  impl_->RecordCandidate(selected, decision);
  if (decision != VulkanRt6CandidateDecision::ACCEPT) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure("selected Vulkan candidate violated the RT6 policy");
  }
  impl_->physical_device = selected.physical_device;
  vkGetPhysicalDeviceMemoryProperties(impl_->physical_device,
                                      &impl_->memory_properties);

  const float queue_priority = 1.0F;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = selected.graphics_queue_family;
  queue_info.queueCount = 1U;
  queue_info.pQueuePriorities = &queue_priority;

  VkPhysicalDeviceVulkan12Features enabled_vulkan12{};
  enabled_vulkan12.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  enabled_vulkan12.timelineSemaphore = VK_TRUE;
  enabled_vulkan12.bufferDeviceAddress = VK_TRUE;
  VkPhysicalDeviceAccelerationStructureFeaturesKHR enabled_acceleration{};
  enabled_acceleration.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
  enabled_acceleration.accelerationStructure = VK_TRUE;
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabled_ray_pipeline{};
  enabled_ray_pipeline.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
  enabled_ray_pipeline.rayTracingPipeline = VK_TRUE;
  enabled_vulkan12.pNext = &enabled_acceleration;
  enabled_acceleration.pNext = &enabled_ray_pipeline;

  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.pNext = &enabled_vulkan12;
  device_info.queueCreateInfoCount = 1U;
  device_info.pQueueCreateInfos = &queue_info;
  device_info.pEnabledFeatures = &selected.core_features;
  device_info.enabledExtensionCount =
      static_cast<std::uint32_t>(kRequiredDeviceExtensions.size());
  device_info.ppEnabledExtensionNames = kRequiredDeviceExtensions.data();
  result = vkCreateDevice(impl_->physical_device, &device_info, nullptr,
                          &impl_->device);
  if (result != VK_SUCCESS) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure(VkFailure("vkCreateDevice", result));
  }
  impl_->evidence.all_supported_core_features_enabled = true;
  impl_->evidence.timeline_semaphore_enabled = true;
  impl_->evidence.buffer_device_address_feature_enabled = true;
  impl_->evidence.acceleration_structure_feature_enabled = true;
  impl_->evidence.ray_tracing_pipeline_feature_enabled = true;
  impl_->evidence.enabled_device_extensions_exact = true;
  impl_->evidence.enabled_device_extension_count =
      static_cast<std::uint32_t>(kRequiredDeviceExtensions.size());

  vkGetDeviceQueue(impl_->device, selected.graphics_queue_family, 0U,
                   &impl_->graphics_queue);
  if (impl_->graphics_queue == VK_NULL_HANDLE) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure("vkGetDeviceQueue returned a null graphics queue");
  }

  impl_->create_acceleration_structure =
      reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
          vkGetDeviceProcAddr(impl_->device,
                              "vkCreateAccelerationStructureKHR"));
  impl_->destroy_acceleration_structure =
      reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
          vkGetDeviceProcAddr(impl_->device,
                              "vkDestroyAccelerationStructureKHR"));
  impl_->get_build_sizes =
      reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
          vkGetDeviceProcAddr(impl_->device,
                              "vkGetAccelerationStructureBuildSizesKHR"));
  impl_->cmd_build_acceleration_structures =
      reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
          vkGetDeviceProcAddr(impl_->device,
                              "vkCmdBuildAccelerationStructuresKHR"));
  impl_->get_acceleration_address =
      reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
          vkGetDeviceProcAddr(impl_->device,
                              "vkGetAccelerationStructureDeviceAddressKHR"));
  impl_->create_ray_pipelines =
      reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
          vkGetDeviceProcAddr(impl_->device, "vkCreateRayTracingPipelinesKHR"));
  impl_->get_shader_group_handles =
      reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
          vkGetDeviceProcAddr(impl_->device,
                              "vkGetRayTracingShaderGroupHandlesKHR"));
  impl_->cmd_trace_rays = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
      vkGetDeviceProcAddr(impl_->device, "vkCmdTraceRaysKHR"));
  if (impl_->create_acceleration_structure == nullptr ||
      impl_->destroy_acceleration_structure == nullptr ||
      impl_->get_build_sizes == nullptr ||
      impl_->cmd_build_acceleration_structures == nullptr ||
      impl_->get_acceleration_address == nullptr ||
      impl_->create_ray_pipelines == nullptr ||
      impl_->get_shader_group_handles == nullptr ||
      impl_->cmd_trace_rays == nullptr) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure("required Vulkan KHR ray-tracing entry point is null");
  }

  VkSemaphoreTypeCreateInfo timeline_type{};
  timeline_type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  timeline_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  timeline_type.initialValue = 0U;
  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_info.pNext = &timeline_type;
  result = vkCreateSemaphore(impl_->device, &semaphore_info, nullptr,
                             &impl_->timeline);
  if (result != VK_SUCCESS) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure(VkFailure("vkCreateSemaphore(timeline)", result));
  }

  impl_->external_instance.instance = impl_->instance;
  impl_->external_instance.instanceLayers.clear();
  impl_->external_instance.instanceExtensions.clear();
  impl_->external_device.physicalDevice = impl_->physical_device;
  impl_->external_device.device = impl_->device;
  impl_->external_device.deviceExtensions.clear();
  for (const char* extension_name : kRequiredDeviceExtensions) {
    const auto property = std::find_if(
        selected.extension_properties.begin(),
        selected.extension_properties.end(),
        [extension_name](const VkExtensionProperties& candidate) {
          return std::strcmp(candidate.extensionName, extension_name) == 0;
        });
    if (property == selected.extension_properties.end()) {
      static_cast<void>(impl_->DestroyPartial());
      return Failure("enabled RT6 extension disappeared from candidate data");
    }
    impl_->external_device.deviceExtensions.push_back(*property);
  }
  impl_->external_device.graphicsQueue = impl_->graphics_queue;
  impl_->external_device.presentQueue = impl_->graphics_queue;

  if (!impl_->lifecycle.MarkOwnerReady()) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure("RT6 lifecycle rejected owner-ready initialization");
  }
  return Ready();
}

VulkanRt6BootstrapResult OgreNextVulkanRayTracingBootstrap::ProveTimelineQueue(
    std::uint64_t signal_value) {
  if (impl_->device == VK_NULL_HANDLE ||
      impl_->graphics_queue == VK_NULL_HANDLE ||
      impl_->timeline == VK_NULL_HANDLE || signal_value == 0U) {
    return Failure("timeline proof requires live owned Vulkan objects");
  }
  const bool before_dispatch =
      impl_->lifecycle.stage() == VulkanRt6LifecycleStage::OWNER_READY &&
      impl_->evidence.timeline_value_before_ray_dispatch == 0U &&
      impl_->evidence.timeline_value_at_ray_dispatch == 0U &&
      signal_value == 1U;
  const bool after_ogre =
      impl_->lifecycle.stage() == VulkanRt6LifecycleStage::OGRE_DETACHED &&
      impl_->evidence.timeline_value_before_ray_dispatch == 1U &&
      impl_->evidence.timeline_value_at_ray_dispatch == 2U &&
      signal_value == 3U;
  if (!before_dispatch && !after_ogre) {
    return Failure(
        "RT6 timeline proof requires the exact owner/dispatch/Ogre sequence");
  }
  VkTimelineSemaphoreSubmitInfo timeline_submit{};
  timeline_submit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  timeline_submit.signalSemaphoreValueCount = 1U;
  timeline_submit.pSignalSemaphoreValues = &signal_value;
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.pNext = &timeline_submit;
  submit.signalSemaphoreCount = 1U;
  submit.pSignalSemaphores = &impl_->timeline;
  VkResult result =
      vkQueueSubmit(impl_->graphics_queue, 1U, &submit, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkQueueSubmit(timeline)", result));
  }
  VkSemaphoreWaitInfo wait_info{};
  wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  wait_info.semaphoreCount = 1U;
  wait_info.pSemaphores = &impl_->timeline;
  wait_info.pValues = &signal_value;
  result =
      vkWaitSemaphores(impl_->device, &wait_info, kTimelineWaitNanoseconds);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkWaitSemaphores", result));
  }
  std::uint64_t observed_value = 0U;
  result = vkGetSemaphoreCounterValue(impl_->device, impl_->timeline,
                                      &observed_value);
  if (result != VK_SUCCESS || observed_value != signal_value) {
    return result == VK_SUCCESS
               ? Failure("timeline semaphore did not reach the exact submitted "
                         "value")
               : Failure(VkFailure("vkGetSemaphoreCounterValue", result));
  }
  if (before_dispatch) {
    impl_->evidence.timeline_value_before_ray_dispatch = observed_value;
  } else {
    impl_->evidence.timeline_value_after_ogre = observed_value;
  }
  return Ready();
}

VulkanRt6BootstrapResult
OgreNextVulkanRayTracingBootstrap::ProveRayTracingDispatch(
    std::uint64_t signal_value) {
  if (impl_->lifecycle.stage() != VulkanRt6LifecycleStage::OWNER_READY ||
      impl_->evidence.timeline_value_before_ray_dispatch != 1U ||
      signal_value != 2U || !impl_->evidence.shader_contract_compiled) {
    return Failure(
        "RT6 dispatch requires an initialized owner and prior timeline proof");
  }

  struct Vertex {
    float x;
    float y;
    float z;
  };
  // Receiver and occluder occupy distinct vertex ranges and distinct BLAS.
  // The receiver covers both controlled sample rays. The elevated occluder
  // covers only x=+0.5 for a deterministic visible/occluded pair.
  const std::array<Vertex, 6U> vertices = {{
      {-2.0F, -1.0F, 0.0F},
      {2.0F, -1.0F, 0.0F},
      {0.0F, 2.0F, 0.0F},
      {0.1F, -0.6F, 0.5F},
      {0.9F, -0.6F, 0.5F},
      {0.5F, 0.7F, 0.5F},
  }};
  VulkanRt6BootstrapResult operation = impl_->CreateBuffer(
      sizeof(vertices),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      true, impl_->geometry_buffer);
  if (!operation.ready()) {
    return operation;
  }
  operation =
      impl_->Upload(impl_->geometry_buffer, vertices.data(), sizeof(vertices));
  if (!operation.ready()) {
    return operation;
  }
  const VkDeviceAddress geometry_address =
      impl_->BufferAddress(impl_->geometry_buffer);
  if (geometry_address == 0U) {
    return Failure("geometry buffer has a null device address");
  }
  if (geometry_address % sizeof(float) != 0U) {
    return Failure("geometry buffer address violates R32 component alignment");
  }
  impl_->evidence.geometry_buffer_created = true;
  impl_->evidence.geometry_buffer_device_address = geometry_address;

  VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
  triangles.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  triangles.vertexData.deviceAddress = geometry_address;
  triangles.vertexStride = sizeof(Vertex);
  triangles.maxVertex = 2U;
  triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
  VkAccelerationStructureGeometryTrianglesDataKHR occluder_triangles =
      triangles;
  occluder_triangles.vertexData.deviceAddress =
      geometry_address + sizeof(Vertex) * 3U;
  VkAccelerationStructureGeometryKHR blas_geometry{};
  blas_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  blas_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  blas_geometry.geometry.triangles = triangles;
  blas_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  VkAccelerationStructureGeometryKHR occluder_blas_geometry = blas_geometry;
  occluder_blas_geometry.geometry.triangles = occluder_triangles;
  VkAccelerationStructureBuildGeometryInfoKHR blas_build{};
  blas_build.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
  blas_build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  blas_build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  blas_build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  blas_build.geometryCount = 1U;
  blas_build.pGeometries = &blas_geometry;
  VkAccelerationStructureBuildGeometryInfoKHR occluder_blas_build = blas_build;
  occluder_blas_build.pGeometries = &occluder_blas_geometry;
  const std::uint32_t primitive_count = 1U;
  VkAccelerationStructureBuildSizesInfoKHR blas_sizes{};
  blas_sizes.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
  impl_->get_build_sizes(impl_->device,
                         VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &blas_build, &primitive_count, &blas_sizes);
  VkAccelerationStructureBuildSizesInfoKHR occluder_blas_sizes{};
  occluder_blas_sizes.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
  impl_->get_build_sizes(impl_->device,
                         VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &occluder_blas_build, &primitive_count,
                         &occluder_blas_sizes);
  if (blas_sizes.accelerationStructureSize == 0U ||
      blas_sizes.buildScratchSize == 0U ||
      occluder_blas_sizes.accelerationStructureSize == 0U ||
      occluder_blas_sizes.buildScratchSize == 0U) {
    return Failure("Vulkan returned zero BLAS build sizes");
  }
  operation = impl_->CreateBuffer(
      blas_sizes.accelerationStructureSize,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, impl_->blas_storage);
  if (!operation.ready()) {
    return operation;
  }
  operation = impl_->CreateBuffer(
      occluder_blas_sizes.accelerationStructureSize,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true,
      impl_->semantic_occluder_blas_storage);
  if (!operation.ready()) {
    return operation;
  }
  VkAccelerationStructureCreateInfoKHR blas_create{};
  blas_create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
  blas_create.buffer = impl_->blas_storage.buffer;
  blas_create.size = blas_sizes.accelerationStructureSize;
  blas_create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  VkResult result = impl_->create_acceleration_structure(
      impl_->device, &blas_create, nullptr, &impl_->blas);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateAccelerationStructureKHR(BLAS)", result));
  }
  VkAccelerationStructureCreateInfoKHR occluder_blas_create = blas_create;
  occluder_blas_create.buffer = impl_->semantic_occluder_blas_storage.buffer;
  occluder_blas_create.size =
      occluder_blas_sizes.accelerationStructureSize;
  result = impl_->create_acceleration_structure(
      impl_->device, &occluder_blas_create, nullptr,
      &impl_->semantic_occluder_blas);
  if (result != VK_SUCCESS) {
    return Failure(
        VkFailure("vkCreateAccelerationStructureKHR(occluder BLAS)", result));
  }
  VkAccelerationStructureDeviceAddressInfoKHR blas_address_info{};
  blas_address_info.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
  blas_address_info.accelerationStructure = impl_->blas;
  const VkDeviceAddress blas_address =
      impl_->get_acceleration_address(impl_->device, &blas_address_info);
  if (blas_address == 0U) {
    return Failure("BLAS has a null device address");
  }
  impl_->evidence.blas_device_address = blas_address;

  VkAccelerationStructureDeviceAddressInfoKHR occluder_blas_address_info{};
  occluder_blas_address_info.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
  occluder_blas_address_info.accelerationStructure =
      impl_->semantic_occluder_blas;
  const VkDeviceAddress occluder_blas_address =
      impl_->get_acceleration_address(impl_->device,
                                      &occluder_blas_address_info);
  if (occluder_blas_address == 0U || occluder_blas_address == blas_address) {
    return Failure("semantic receiver and occluder BLAS addresses are not "
                   "distinct nonzero values");
  }
  impl_->evidence.semantic_occluder_blas_device_address =
      occluder_blas_address;

  std::array<VkAccelerationStructureInstanceKHR, 2U> instance_data{};
  for (VkAccelerationStructureInstanceKHR& instance : instance_data) {
    instance.transform.matrix[0][0] = 1.0F;
    instance.transform.matrix[1][1] = 1.0F;
    instance.transform.matrix[2][2] = 1.0F;
    instance.instanceShaderBindingTableRecordOffset = 0U;
    instance.flags =
        VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  }
  instance_data[0U].instanceCustomIndex = kReceiverInstanceId;
  instance_data[0U].mask = 0x01U;
  instance_data[0U].accelerationStructureReference = blas_address;
  instance_data[1U].instanceCustomIndex = kOccluderInstanceId;
  instance_data[1U].mask = 0x02U;
  instance_data[1U].accelerationStructureReference = occluder_blas_address;
  operation = impl_->CreateBuffer(
      sizeof(instance_data),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      true, impl_->instance_buffer);
  if (!operation.ready()) {
    return operation;
  }
  operation = impl_->Upload(impl_->instance_buffer, instance_data.data(),
                            sizeof(instance_data));
  if (!operation.ready()) {
    return operation;
  }
  const VkDeviceAddress instance_address =
      impl_->BufferAddress(impl_->instance_buffer);
  if (instance_address == 0U) {
    return Failure("TLAS instance buffer has a null device address");
  }
  if (instance_address % 16U != 0U) {
    return Failure("TLAS instance buffer address is not 16-byte aligned");
  }
  impl_->evidence.instance_buffer_device_address = instance_address;

  VkAccelerationStructureGeometryInstancesDataKHR instances{};
  instances.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  instances.arrayOfPointers = VK_FALSE;
  instances.data.deviceAddress = instance_address;
  VkAccelerationStructureGeometryKHR tlas_geometry{};
  tlas_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  tlas_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  tlas_geometry.geometry.instances = instances;
  VkAccelerationStructureBuildGeometryInfoKHR tlas_build{};
  tlas_build.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
  tlas_build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  tlas_build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  tlas_build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  tlas_build.geometryCount = 1U;
  tlas_build.pGeometries = &tlas_geometry;
  const std::uint32_t tlas_primitive_count =
      static_cast<std::uint32_t>(instance_data.size());
  VkAccelerationStructureBuildSizesInfoKHR tlas_sizes{};
  tlas_sizes.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
  impl_->get_build_sizes(impl_->device,
                         VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &tlas_build, &tlas_primitive_count, &tlas_sizes);
  if (tlas_sizes.accelerationStructureSize == 0U ||
      tlas_sizes.buildScratchSize == 0U) {
    return Failure("Vulkan returned zero TLAS build sizes");
  }
  operation = impl_->CreateBuffer(
      tlas_sizes.accelerationStructureSize,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, impl_->tlas_storage);
  if (!operation.ready()) {
    return operation;
  }
  VkAccelerationStructureCreateInfoKHR tlas_create{};
  tlas_create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
  tlas_create.buffer = impl_->tlas_storage.buffer;
  tlas_create.size = tlas_sizes.accelerationStructureSize;
  tlas_create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  result = impl_->create_acceleration_structure(impl_->device, &tlas_create,
                                                nullptr, &impl_->tlas);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateAccelerationStructureKHR(TLAS)", result));
  }
  VkAccelerationStructureDeviceAddressInfoKHR tlas_address_info{};
  tlas_address_info.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
  tlas_address_info.accelerationStructure = impl_->tlas;
  const VkDeviceAddress tlas_address =
      impl_->get_acceleration_address(impl_->device, &tlas_address_info);
  if (tlas_address == 0U) {
    return Failure("TLAS has a null device address");
  }
  impl_->evidence.tlas_device_address = tlas_address;

  const VkDeviceSize required_scratch_size =
      std::max({blas_sizes.buildScratchSize,
                occluder_blas_sizes.buildScratchSize,
                tlas_sizes.buildScratchSize});
  const VkDeviceSize scratch_alignment =
      impl_->evidence.acceleration_structure_scratch_alignment;
  if (scratch_alignment == 0U ||
      !IsPowerOfTwo(static_cast<std::uint32_t>(scratch_alignment))) {
    return Failure("acceleration-structure scratch alignment is invalid");
  }
  const VkDeviceSize scratch_size =
      required_scratch_size + scratch_alignment - 1U;
  operation = impl_->CreateBuffer(scratch_size,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true,
                                  impl_->scratch_buffer);
  if (!operation.ready()) {
    return operation;
  }
  const VkDeviceAddress scratch_base =
      impl_->BufferAddress(impl_->scratch_buffer);
  if (scratch_base == 0U) {
    return Failure("acceleration-structure scratch buffer has a null address");
  }
  const VkDeviceAddress scratch_address =
      AlignUp(scratch_base, scratch_alignment);
  if (scratch_address + required_scratch_size > scratch_base + scratch_size) {
    return Failure("aligned acceleration-structure scratch range exceeds its "
                   "owned buffer");
  }
  impl_->evidence.scratch_buffer_device_address = scratch_address;
  blas_build.dstAccelerationStructure = impl_->blas;
  blas_build.scratchData.deviceAddress = scratch_address;
  occluder_blas_build.dstAccelerationStructure =
      impl_->semantic_occluder_blas;
  occluder_blas_build.scratchData.deviceAddress = scratch_address;
  tlas_build.dstAccelerationStructure = impl_->tlas;
  tlas_build.scratchData.deviceAddress = scratch_address;

  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = kOutputFormat;
  image_info.extent = {1U, 1U, 1U};
  image_info.mipLevels = 1U;
  image_info.arrayLayers = 1U;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage =
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  result =
      vkCreateImage(impl_->device, &image_info, nullptr, &impl_->output_image);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateImage(RT6 output)", result));
  }
  VkMemoryRequirements image_requirements{};
  vkGetImageMemoryRequirements(impl_->device, impl_->output_image,
                               &image_requirements);
  std::uint32_t image_memory_type = 0U;
  if (!impl_->FindMemoryType(image_requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             image_memory_type)) {
    return Failure("no device-local memory type for RT6 output image");
  }
  VkMemoryAllocateInfo image_allocate{};
  image_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  image_allocate.allocationSize = image_requirements.size;
  image_allocate.memoryTypeIndex = image_memory_type;
  result = vkAllocateMemory(impl_->device, &image_allocate, nullptr,
                            &impl_->output_image_memory);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkAllocateMemory(RT6 output)", result));
  }
  result = vkBindImageMemory(impl_->device, impl_->output_image,
                             impl_->output_image_memory, 0U);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkBindImageMemory(RT6 output)", result));
  }
  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = impl_->output_image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = kOutputFormat;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1U;
  view_info.subresourceRange.layerCount = 1U;
  result = vkCreateImageView(impl_->device, &view_info, nullptr,
                             &impl_->output_image_view);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateImageView(RT6 output)", result));
  }
  operation = impl_->CreateBuffer(sizeof(kExpectedPrimaryHit),
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  false, impl_->readback_buffer);
  if (!operation.ready()) {
    return operation;
  }
  operation = impl_->CreateBuffer(
      sizeof(SemanticGpuOutput),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false,
      impl_->semantic_output_buffer);
  if (!operation.ready()) {
    return operation;
  }
  operation = impl_->CreateBuffer(
      sizeof(SemanticGpuOutput), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      false, impl_->semantic_readback_buffer);
  if (!operation.ready()) {
    return operation;
  }

  const std::array<VkDescriptorSetLayoutBinding, 3U> bindings = {{
      {0U, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1U,
       VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
      {1U, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1U, VK_SHADER_STAGE_RAYGEN_BIT_KHR,
       nullptr},
      {2U, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U,
       VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
  }};
  VkDescriptorSetLayoutCreateInfo descriptor_layout_info{};
  descriptor_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_layout_info.bindingCount =
      static_cast<std::uint32_t>(bindings.size());
  descriptor_layout_info.pBindings = bindings.data();
  result = vkCreateDescriptorSetLayout(impl_->device, &descriptor_layout_info,
                                       nullptr, &impl_->descriptor_set_layout);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateDescriptorSetLayout", result));
  }
  const std::array<VkDescriptorPoolSize, 3U> pool_sizes = {{
      {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1U},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1U},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1U},
  }};
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 1U;
  pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
  pool_info.pPoolSizes = pool_sizes.data();
  result = vkCreateDescriptorPool(impl_->device, &pool_info, nullptr,
                                  &impl_->descriptor_pool);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateDescriptorPool", result));
  }
  VkDescriptorSetAllocateInfo descriptor_allocate{};
  descriptor_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_allocate.descriptorPool = impl_->descriptor_pool;
  descriptor_allocate.descriptorSetCount = 1U;
  descriptor_allocate.pSetLayouts = &impl_->descriptor_set_layout;
  result = vkAllocateDescriptorSets(impl_->device, &descriptor_allocate,
                                    &impl_->descriptor_set);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkAllocateDescriptorSets", result));
  }
  VkWriteDescriptorSetAccelerationStructureKHR acceleration_write{};
  acceleration_write.sType =
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
  acceleration_write.accelerationStructureCount = 1U;
  acceleration_write.pAccelerationStructures = &impl_->tlas;
  VkDescriptorImageInfo output_descriptor{};
  output_descriptor.imageView = impl_->output_image_view;
  output_descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkDescriptorBufferInfo semantic_descriptor{};
  semantic_descriptor.buffer = impl_->semantic_output_buffer.buffer;
  semantic_descriptor.offset = 0U;
  semantic_descriptor.range = sizeof(SemanticGpuOutput);
  std::array<VkWriteDescriptorSet, 3U> descriptor_writes{};
  descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_writes[0].pNext = &acceleration_write;
  descriptor_writes[0].dstSet = impl_->descriptor_set;
  descriptor_writes[0].dstBinding = 0U;
  descriptor_writes[0].descriptorCount = 1U;
  descriptor_writes[0].descriptorType =
      VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_writes[1].dstSet = impl_->descriptor_set;
  descriptor_writes[1].dstBinding = 1U;
  descriptor_writes[1].descriptorCount = 1U;
  descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  descriptor_writes[1].pImageInfo = &output_descriptor;
  descriptor_writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_writes[2].dstSet = impl_->descriptor_set;
  descriptor_writes[2].dstBinding = 2U;
  descriptor_writes[2].descriptorCount = 1U;
  descriptor_writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_writes[2].pBufferInfo = &semantic_descriptor;
  vkUpdateDescriptorSets(impl_->device,
                         static_cast<std::uint32_t>(descriptor_writes.size()),
                         descriptor_writes.data(), 0U, nullptr);

  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 1U;
  pipeline_layout_info.pSetLayouts = &impl_->descriptor_set_layout;
  result = vkCreatePipelineLayout(impl_->device, &pipeline_layout_info, nullptr,
                                  &impl_->pipeline_layout);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreatePipelineLayout", result));
  }

  for (std::size_t index = 0U; index < impl_->shader_spirv.size(); ++index) {
    if (impl_->shader_spirv[index].empty()) {
      return Failure("validated RT6 SPIR-V unexpectedly became empty");
    }
    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize =
        impl_->shader_spirv[index].size() * sizeof(std::uint32_t);
    module_info.pCode = impl_->shader_spirv[index].data();
    result = vkCreateShaderModule(impl_->device, &module_info, nullptr,
                                  &impl_->shader_modules[index]);
    if (result != VK_SUCCESS) {
      return Failure(VkFailure("vkCreateShaderModule(RT6)", result));
    }
  }
  const std::array<VkShaderStageFlagBits, 3U> stage_bits = {
      VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
      VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
  std::array<VkPipelineShaderStageCreateInfo, 3U> stages{};
  for (std::size_t index = 0U; index < stages.size(); ++index) {
    stages[index].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[index].stage = stage_bits[index];
    stages[index].module = impl_->shader_modules[index];
    stages[index].pName = "main";
  }
  std::array<VkRayTracingShaderGroupCreateInfoKHR, 3U> groups{};
  for (VkRayTracingShaderGroupCreateInfoKHR& group : groups) {
    group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = VK_SHADER_UNUSED_KHR;
    group.anyHitShader = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;
  }
  groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
  groups[0].generalShader = 0U;
  groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
  groups[1].generalShader = 1U;
  groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
  groups[2].closestHitShader = 2U;
  VkRayTracingPipelineCreateInfoKHR ray_pipeline_info{};
  ray_pipeline_info.sType =
      VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
  ray_pipeline_info.stageCount = static_cast<std::uint32_t>(stages.size());
  ray_pipeline_info.pStages = stages.data();
  ray_pipeline_info.groupCount = static_cast<std::uint32_t>(groups.size());
  ray_pipeline_info.pGroups = groups.data();
  ray_pipeline_info.maxPipelineRayRecursionDepth = 1U;
  ray_pipeline_info.layout = impl_->pipeline_layout;
  result = impl_->create_ray_pipelines(impl_->device, VK_NULL_HANDLE,
                                       VK_NULL_HANDLE, 1U, &ray_pipeline_info,
                                       nullptr, &impl_->ray_pipeline);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateRayTracingPipelinesKHR", result));
  }
  impl_->evidence.ray_pipeline_created = true;

  const std::uint32_t handle_size = impl_->evidence.shader_group_handle_size;
  const std::uint32_t handle_alignment =
      impl_->evidence.shader_group_handle_alignment;
  const std::uint32_t base_alignment =
      impl_->evidence.shader_group_base_alignment;
  const VkDeviceSize handle_stride = AlignUp(handle_size, handle_alignment);
  std::vector<std::uint8_t> handles(groups.size() * handle_size);
  result =
      impl_->get_shader_group_handles(impl_->device, impl_->ray_pipeline, 0U,
                                      static_cast<std::uint32_t>(groups.size()),
                                      handles.size(), handles.data());
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkGetRayTracingShaderGroupHandlesKHR", result));
  }
  const VkDeviceSize sbt_size =
      handle_stride * groups.size() + base_alignment * 4U;
  operation = impl_->CreateBuffer(sbt_size,
                                  VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  true, impl_->shader_binding_table);
  if (!operation.ready()) {
    return operation;
  }
  const VkDeviceAddress sbt_base =
      impl_->BufferAddress(impl_->shader_binding_table);
  if (sbt_base == 0U) {
    return Failure("shader binding table has a null device address");
  }
  const VkDeviceAddress raygen_address = AlignUp(sbt_base, base_alignment);
  const VkDeviceAddress miss_address =
      AlignUp(raygen_address + handle_stride, base_alignment);
  const VkDeviceAddress hit_address =
      AlignUp(miss_address + handle_stride, base_alignment);
  if (hit_address + handle_stride > sbt_base + sbt_size) {
    return Failure("aligned shader binding table exceeds its owned buffer");
  }
  void* sbt_mapping = nullptr;
  result = vkMapMemory(impl_->device, impl_->shader_binding_table.memory, 0U,
                       sbt_size, 0U, &sbt_mapping);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkMapMemory(SBT)", result));
  }
  std::memset(sbt_mapping, 0, static_cast<std::size_t>(sbt_size));
  const std::array<VkDeviceAddress, 3U> group_addresses = {
      raygen_address, miss_address, hit_address};
  for (std::size_t index = 0U; index < group_addresses.size(); ++index) {
    const VkDeviceSize destination_offset = group_addresses[index] - sbt_base;
    std::memcpy(static_cast<std::uint8_t*>(sbt_mapping) + destination_offset,
                handles.data() + index * handle_size, handle_size);
  }
  vkUnmapMemory(impl_->device, impl_->shader_binding_table.memory);
  impl_->evidence.shader_binding_table_created = true;
  impl_->evidence.shader_binding_table_device_address = raygen_address;

  VkCommandPoolCreateInfo command_pool_info{};
  command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  command_pool_info.queueFamilyIndex = impl_->evidence.graphics_queue_family;
  result = vkCreateCommandPool(impl_->device, &command_pool_info, nullptr,
                               &impl_->command_pool);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateCommandPool(RT6)", result));
  }
  VkCommandBufferAllocateInfo command_allocate{};
  command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_allocate.commandPool = impl_->command_pool;
  command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_allocate.commandBufferCount = 1U;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  result = vkAllocateCommandBuffers(impl_->device, &command_allocate,
                                    &command_buffer);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkAllocateCommandBuffers(RT6)", result));
  }
  if (!impl_->lifecycle.MarkRayResourcesReady()) {
    return Failure("RT6 lifecycle rejected ray-resource initialization");
  }
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  result = vkBeginCommandBuffer(command_buffer, &begin_info);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkBeginCommandBuffer(RT6)", result));
  }
  const VkAccelerationStructureBuildRangeInfoKHR blas_range = {1U, 0U, 0U, 0U};
  const VkAccelerationStructureBuildRangeInfoKHR* blas_ranges = &blas_range;
  impl_->cmd_build_acceleration_structures(command_buffer, 1U, &blas_build,
                                           &blas_ranges);
  VkMemoryBarrier blas_barrier{};
  blas_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  blas_barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  blas_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                               VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  vkCmdPipelineBarrier(command_buffer,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       0U, 1U, &blas_barrier, 0U, nullptr, 0U, nullptr);
  impl_->cmd_build_acceleration_structures(
      command_buffer, 1U, &occluder_blas_build, &blas_ranges);
  vkCmdPipelineBarrier(command_buffer,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       0U, 1U, &blas_barrier, 0U, nullptr, 0U, nullptr);
  const VkAccelerationStructureBuildRangeInfoKHR tlas_range = {2U, 0U, 0U, 0U};
  const VkAccelerationStructureBuildRangeInfoKHR* tlas_ranges = &tlas_range;
  impl_->cmd_build_acceleration_structures(command_buffer, 1U, &tlas_build,
                                           &tlas_ranges);
  VkMemoryBarrier tlas_barrier{};
  tlas_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  tlas_barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  tlas_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  vkCmdPipelineBarrier(command_buffer,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0U, 1U,
                       &tlas_barrier, 0U, nullptr, 0U, nullptr);
  VkImageMemoryBarrier image_to_general{};
  image_to_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_to_general.srcAccessMask = 0U;
  image_to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  image_to_general.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  image_to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_to_general.image = impl_->output_image;
  image_to_general.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_to_general.subresourceRange.levelCount = 1U;
  image_to_general.subresourceRange.layerCount = 1U;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0U, 0U,
                       nullptr, 0U, nullptr, 1U, &image_to_general);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                    impl_->ray_pipeline);
  vkCmdBindDescriptorSets(
      command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
      impl_->pipeline_layout, 0U, 1U, &impl_->descriptor_set, 0U, nullptr);
  impl_->evidence.descriptor_set_bound = true;
  const VkStridedDeviceAddressRegionKHR raygen_region = {
      raygen_address, handle_stride, handle_stride};
  const VkStridedDeviceAddressRegionKHR miss_region = {
      miss_address, handle_stride, handle_stride};
  const VkStridedDeviceAddressRegionKHR hit_region = {
      hit_address, handle_stride, handle_stride};
  const VkStridedDeviceAddressRegionKHR callable_region{};
  impl_->cmd_trace_rays(command_buffer, &raygen_region, &miss_region,
                        &hit_region, &callable_region, 1U, 1U, 1U);
  VkImageMemoryBarrier image_to_transfer{};
  image_to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_to_transfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  image_to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  image_to_transfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  image_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  image_to_transfer.image = impl_->output_image;
  image_to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_to_transfer.subresourceRange.levelCount = 1U;
  image_to_transfer.subresourceRange.layerCount = 1U;
  VkBufferMemoryBarrier semantic_to_transfer{};
  semantic_to_transfer.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  semantic_to_transfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  semantic_to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  semantic_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  semantic_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  semantic_to_transfer.buffer = impl_->semantic_output_buffer.buffer;
  semantic_to_transfer.offset = 0U;
  semantic_to_transfer.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(command_buffer,
                       VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 1U,
                       &semantic_to_transfer, 1U, &image_to_transfer);
  VkBufferImageCopy copy_region{};
  copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy_region.imageSubresource.layerCount = 1U;
  copy_region.imageExtent = {1U, 1U, 1U};
  vkCmdCopyImageToBuffer(command_buffer, impl_->output_image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         impl_->readback_buffer.buffer, 1U, &copy_region);
  VkBufferCopy semantic_copy{};
  semantic_copy.size = sizeof(SemanticGpuOutput);
  vkCmdCopyBuffer(command_buffer, impl_->semantic_output_buffer.buffer,
                  impl_->semantic_readback_buffer.buffer, 1U,
                  &semantic_copy);
  VkBufferMemoryBarrier readback_barrier{};
  readback_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  readback_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  readback_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  readback_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  readback_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  readback_barrier.buffer = impl_->readback_buffer.buffer;
  readback_barrier.offset = 0U;
  readback_barrier.size = VK_WHOLE_SIZE;
  VkBufferMemoryBarrier semantic_readback_barrier = readback_barrier;
  semantic_readback_barrier.buffer = impl_->semantic_readback_buffer.buffer;
  const std::array<VkBufferMemoryBarrier, 2U> readback_barriers = {
      readback_barrier, semantic_readback_barrier};
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0U, 0U, nullptr,
                       static_cast<std::uint32_t>(readback_barriers.size()),
                       readback_barriers.data(), 0U, nullptr);
  result = vkEndCommandBuffer(command_buffer);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkEndCommandBuffer(RT6)", result));
  }

  VkTimelineSemaphoreSubmitInfo timeline_submit{};
  timeline_submit.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  timeline_submit.signalSemaphoreValueCount = 1U;
  timeline_submit.pSignalSemaphoreValues = &signal_value;
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.pNext = &timeline_submit;
  submit.commandBufferCount = 1U;
  submit.pCommandBuffers = &command_buffer;
  submit.signalSemaphoreCount = 1U;
  submit.pSignalSemaphores = &impl_->timeline;
  result = vkQueueSubmit(impl_->graphics_queue, 1U, &submit, VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkQueueSubmit(RT6 dispatch)", result));
  }
  VkSemaphoreWaitInfo wait_info{};
  wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  wait_info.semaphoreCount = 1U;
  wait_info.pSemaphores = &impl_->timeline;
  wait_info.pValues = &signal_value;
  result =
      vkWaitSemaphores(impl_->device, &wait_info, kTimelineWaitNanoseconds);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkWaitSemaphores(RT6 dispatch)", result));
  }
  std::uint64_t observed_dispatch_value = 0U;
  result = vkGetSemaphoreCounterValue(impl_->device, impl_->timeline,
                                      &observed_dispatch_value);
  if (result != VK_SUCCESS || observed_dispatch_value != signal_value) {
    return result == VK_SUCCESS
               ? Failure("dispatch timeline did not reach the exact submitted "
                         "value")
               : Failure(
                     VkFailure("vkGetSemaphoreCounterValue(dispatch)", result));
  }
  impl_->evidence.timeline_value_at_ray_dispatch = observed_dispatch_value;
  impl_->evidence.blas_built = true;
  impl_->evidence.tlas_built = true;
  impl_->evidence.semantic_blas_count = 2U;
  impl_->evidence.semantic_tlas_instance_count = 2U;
  impl_->evidence.semantic_sample_count = kSemanticSampleCount;
  impl_->evidence.semantic_primary_receiver_ray_count = kSemanticSampleCount;
  impl_->evidence.semantic_directional_visibility_ray_count =
      kSemanticSampleCount;
  impl_->evidence.semantic_receiver_blas_built = true;
  impl_->evidence.semantic_occluder_blas_built = true;
  impl_->evidence.semantic_tlas_built = true;
  impl_->evidence.semantic_controlled_geometry_exact = true;
  impl_->evidence.ray_dispatch_completed = true;

  void* readback_mapping = nullptr;
  result = vkMapMemory(impl_->device, impl_->readback_buffer.memory, 0U,
                       sizeof(kExpectedPrimaryHit), 0U, &readback_mapping);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkMapMemory(RT6 readback)", result));
  }
  std::memcpy(impl_->evidence.readback_words.data(), readback_mapping,
              sizeof(kExpectedPrimaryHit));
  vkUnmapMemory(impl_->device, impl_->readback_buffer.memory);
  impl_->evidence.output_image_copied_to_host = true;
  impl_->evidence.primary_hit_observed =
      impl_->evidence.readback_words == kExpectedPrimaryHit;
  if (!impl_->evidence.primary_hit_observed) {
    return Failure(
        "RT6 dispatch did not produce the deterministic primary-hit pixel");
  }

  SemanticGpuOutput semantic_output{};
  result = vkMapMemory(impl_->device, impl_->semantic_readback_buffer.memory,
                       0U, sizeof(semantic_output), 0U, &readback_mapping);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkMapMemory(N4A semantic readback)", result));
  }
  std::memcpy(&semantic_output, readback_mapping, sizeof(semantic_output));
  vkUnmapMemory(impl_->device, impl_->semantic_readback_buffer.memory);

  const auto unpack_rgba16 = [](const std::array<std::uint32_t, 2U>& words) {
    return std::array<std::uint16_t, 4U>{
        static_cast<std::uint16_t>(words[0U] & 0xffffU),
        static_cast<std::uint16_t>(words[0U] >> 16U),
        static_cast<std::uint16_t>(words[1U] & 0xffffU),
        static_cast<std::uint16_t>(words[1U] >> 16U)};
  };
  for (std::size_t sample = 0U; sample < kSemanticSampleCount; ++sample) {
    if ((semantic_output.visibility_r16_bits[sample] & 0xffff0000U) != 0U) {
      return Failure("N4A visibility readback is not a canonical R16 value");
    }
    impl_->evidence.semantic_visibility_r16_bits[sample] =
        static_cast<std::uint16_t>(
            semantic_output.visibility_r16_bits[sample]);
    impl_->evidence.semantic_lineage_r32[sample] =
        semantic_output.lineage_r32[sample];
    impl_->evidence.semantic_raster_rgba16_bits[sample] =
        unpack_rgba16(semantic_output.raster_rgba16_words[sample]);
    impl_->evidence.semantic_hybrid_rgba16_bits[sample] =
        unpack_rgba16(semantic_output.hybrid_rgba16_words[sample]);
  }
  impl_->evidence.semantic_readback_completed = true;

  const std::array<std::uint16_t, kSemanticSampleCount> expected_visibility = {
      kNativeDirectionalShadowVisibleR16,
      kNativeDirectionalShadowOccludedR16};
  const std::array<std::uint32_t, kSemanticSampleCount> expected_lineage = {
      kReceiverInstanceId, kReceiverInstanceId | kOccluderInstanceId};
  if (impl_->evidence.semantic_visibility_r16_bits != expected_visibility ||
      impl_->evidence.semantic_lineage_r32 != expected_lineage) {
    return Failure("N4A rays did not produce the controlled visible/occluded "
                   "lineage pair");
  }
  for (std::size_t sample = 0U; sample < kSemanticSampleCount; ++sample) {
    NativeDirectionalShadowRgba16Pixel raster{};
    raster.channels = impl_->evidence.semantic_raster_rgba16_bits[sample];
    if (raster.channels != kSemanticRasterRgba16) {
      return Failure("N4A raster sample is not the canonical RGBA16 mapping");
    }
    NativeDirectionalShadowSampleOracle oracle{};
    const NativeDirectionalShadowVisibility visibility =
        sample == 0U ? NativeDirectionalShadowVisibility::VISIBLE
                     : NativeDirectionalShadowVisibility::OCCLUDED;
    const ValidationResult oracle_result =
        TryBuildNativeDirectionalShadowSampleOracle(visibility, raster,
                                                     oracle);
    if (!oracle_result ||
        oracle.visibility_r16_bits !=
            impl_->evidence.semantic_visibility_r16_bits[sample] ||
        oracle.hybrid_rgba16.channels !=
            impl_->evidence.semantic_hybrid_rgba16_bits[sample]) {
      return Failure("N4A GPU sample differs from the portable directional "
                     "shadow sample oracle");
    }
  }
  impl_->evidence.semantic_sample_oracle_passed = true;
  if (!impl_->lifecycle.MarkRayDispatched()) {
    return Failure("RT6 lifecycle rejected completed ray dispatch");
  }
  return Ready();
}

std::uintptr_t
OgreNextVulkanRayTracingBootstrap::external_instance_descriptor_address()
    const noexcept {
  return reinterpret_cast<std::uintptr_t>(&impl_->external_instance);
}

std::uintptr_t
OgreNextVulkanRayTracingBootstrap::external_device_descriptor_address()
    const noexcept {
  return reinterpret_cast<std::uintptr_t>(&impl_->external_device);
}

VulkanRt6BootstrapResult OgreNextVulkanRayTracingBootstrap::MarkOgreAttached() {
  if (!impl_->lifecycle.MarkOgreAttached()) {
    return Failure("RT6 lifecycle rejected Ogre attachment");
  }
  impl_->ogre_attached = true;
  return Ready();
}

VulkanRt6BootstrapResult OgreNextVulkanRayTracingBootstrap::VerifyOgreAdoption(
    Ogre::RenderSystem* render_system) {
  auto* vulkan_render_system =
      dynamic_cast<Ogre::VulkanRenderSystem*>(render_system);
  if (vulkan_render_system == nullptr) {
    return Failure("active Ogre renderer is not the pinned Vulkan renderer");
  }
  Ogre::VulkanDevice* ogre_device = vulkan_render_system->getVulkanDevice();
  if (ogre_device == nullptr) {
    return Failure("Ogre did not create its external Vulkan device wrapper");
  }
  impl_->evidence.instance_injected_exactly =
      vulkan_render_system->getVkInstance() == impl_->instance;
  impl_->evidence.physical_device_injected_exactly =
      ogre_device->mPhysicalDevice == impl_->physical_device;
  impl_->evidence.logical_device_injected_exactly =
      ogre_device->mDevice == impl_->device;
  impl_->evidence.graphics_queue_injected_exactly =
      ogre_device->mGraphicsQueue.mQueue == impl_->graphics_queue &&
      ogre_device->mGraphicsQueue.mFamilyIdx ==
          impl_->evidence.graphics_queue_family &&
      ogre_device->mGraphicsQueue.mQueueIdx ==
          impl_->evidence.graphics_queue_index;
  impl_->evidence.ogre_external_ownership_observed = ogre_device->mIsExternal;
  if (!impl_->evidence.instance_injected_exactly ||
      !impl_->evidence.physical_device_injected_exactly ||
      !impl_->evidence.logical_device_injected_exactly ||
      !impl_->evidence.graphics_queue_injected_exactly ||
      !impl_->evidence.ogre_external_ownership_observed) {
    return Failure("Ogre did not adopt the exact RT6 Vulkan object set");
  }
  return Ready();
}

VulkanRt6BootstrapResult OgreNextVulkanRayTracingBootstrap::MarkOgreDetached() {
  if (!impl_->lifecycle.MarkOgreDetached()) {
    return Failure("RT6 lifecycle rejected Ogre detachment");
  }
  impl_->ogre_attached = false;
  impl_->evidence.ogre_shutdown_before_owner_teardown =
      impl_->device != VK_NULL_HANDLE && impl_->instance != VK_NULL_HANDLE &&
      impl_->timeline != VK_NULL_HANDLE && impl_->tlas != VK_NULL_HANDLE &&
      impl_->ray_pipeline != VK_NULL_HANDLE;
  return impl_->evidence.ogre_shutdown_before_owner_teardown
             ? Ready()
             : Failure("owned RT6 state disappeared before Ogre shutdown");
}

VulkanRt6BootstrapResult OgreNextVulkanRayTracingBootstrap::Shutdown() {
  if (impl_->ogre_attached) {
    return Failure(
        "cannot destroy RoR-owned Vulkan objects while Ogre is attached");
  }
  if (impl_->lifecycle.stage() != VulkanRt6LifecycleStage::OGRE_DETACHED) {
    return Failure(
        "full RT6 shutdown requires a completed Ogre attachment cycle");
  }
  const VkResult idle_result = vkDeviceWaitIdle(impl_->device);
  if (idle_result != VK_SUCCESS) {
    return Failure(VkFailure("vkDeviceWaitIdle", idle_result));
  }
  impl_->DestroyRayResourcesRaw();
  if (!impl_->lifecycle.MarkRayResourcesDestroyed()) {
    return Failure("RT6 lifecycle rejected ray-resource teardown");
  }
  impl_->evidence.ray_resources_destroyed_before_device =
      impl_->device != VK_NULL_HANDLE;
  vkDestroySemaphore(impl_->device, impl_->timeline, nullptr);
  impl_->timeline = VK_NULL_HANDLE;
  if (!impl_->lifecycle.MarkTimelineDestroyed()) {
    return Failure("RT6 lifecycle rejected timeline teardown");
  }
  impl_->evidence.timeline_destroyed_before_device =
      impl_->device != VK_NULL_HANDLE;
  vkDestroyDevice(impl_->device, nullptr);
  impl_->device = VK_NULL_HANDLE;
  if (!impl_->lifecycle.MarkDeviceDestroyed()) {
    return Failure("RT6 lifecycle rejected device teardown");
  }
  impl_->evidence.device_destroyed_before_instance =
      impl_->instance != VK_NULL_HANDLE;
  vkDestroyInstance(impl_->instance, nullptr);
  impl_->instance = VK_NULL_HANDLE;
  if (!impl_->lifecycle.MarkInstanceDestroyed()) {
    return Failure("RT6 lifecycle rejected instance teardown");
  }
  impl_->evidence.shutdown_completed = impl_->lifecycle.complete();
  return impl_->evidence.shutdown_completed
             ? Ready()
             : Failure("RT6 lifecycle did not reach complete teardown");
}

VulkanRt6BootstrapResult
OgreNextVulkanRayTracingBootstrap::AbortAfterFailure() {
  if (impl_->ogre_attached) {
    return Failure(
        "cannot abort RoR-owned Vulkan objects while Ogre is attached");
  }
  return impl_->DestroyPartial();
}

const VulkanRt6BootstrapEvidence&
OgreNextVulkanRayTracingBootstrap::evidence() const noexcept {
  return impl_->evidence;
}

const char* VulkanRt6DeviceClassName(VulkanRt5DeviceClass value) noexcept {
  switch (value) {
  case VulkanRt5DeviceClass::INTEGRATED_GPU:
    return "integrated_gpu";
  case VulkanRt5DeviceClass::DISCRETE_GPU:
    return "discrete_gpu";
  case VulkanRt5DeviceClass::VIRTUAL_GPU:
    return "virtual_gpu";
  case VulkanRt5DeviceClass::CPU:
    return "cpu";
  default:
    return "other";
  }
}

const char*
VulkanRt6CandidateDecisionName(VulkanRt6CandidateDecision value) noexcept {
  switch (value) {
  case VulkanRt6CandidateDecision::ACCEPT:
    return "accept";
  case VulkanRt6CandidateDecision::API_TOO_OLD:
    return "api_too_old";
  case VulkanRt6CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE:
    return "software_or_unattested_device";
  case VulkanRt6CandidateDecision::DEVICE_IDENTITY_UNAVAILABLE:
    return "device_identity_unavailable";
  case VulkanRt6CandidateDecision::GRAPHICS_QUEUE_UNAVAILABLE:
    return "graphics_queue_unavailable";
  case VulkanRt6CandidateDecision::COMPUTE_QUEUE_UNAVAILABLE:
    return "compute_queue_unavailable";
  case VulkanRt6CandidateDecision::TIMELINE_SEMAPHORE_UNAVAILABLE:
    return "timeline_semaphore_unavailable";
  case VulkanRt6CandidateDecision::
      DEFERRED_HOST_OPERATIONS_EXTENSION_UNAVAILABLE:
    return "deferred_host_operations_extension_unavailable";
  case VulkanRt6CandidateDecision::BUFFER_DEVICE_ADDRESS_EXTENSION_UNAVAILABLE:
    return "buffer_device_address_extension_unavailable";
  case VulkanRt6CandidateDecision::ACCELERATION_STRUCTURE_EXTENSION_UNAVAILABLE:
    return "acceleration_structure_extension_unavailable";
  case VulkanRt6CandidateDecision::RAY_TRACING_PIPELINE_EXTENSION_UNAVAILABLE:
    return "ray_tracing_pipeline_extension_unavailable";
  case VulkanRt6CandidateDecision::BUFFER_DEVICE_ADDRESS_FEATURE_UNAVAILABLE:
    return "buffer_device_address_feature_unavailable";
  case VulkanRt6CandidateDecision::ACCELERATION_STRUCTURE_FEATURE_UNAVAILABLE:
    return "acceleration_structure_feature_unavailable";
  case VulkanRt6CandidateDecision::RAY_TRACING_PIPELINE_FEATURE_UNAVAILABLE:
    return "ray_tracing_pipeline_feature_unavailable";
  case VulkanRt6CandidateDecision::OUTPUT_STORAGE_IMAGE_FORMAT_UNAVAILABLE:
    return "output_storage_image_format_unavailable";
  case VulkanRt6CandidateDecision::RAY_TRACING_PROPERTIES_INVALID:
    return "ray_tracing_properties_invalid";
  case VulkanRt6CandidateDecision::ENABLED_FEATURE_STATE_AMBIGUOUS:
    return "enabled_feature_state_ambiguous";
  case VulkanRt6CandidateDecision::ENABLED_EXTENSION_STATE_AMBIGUOUS:
    return "enabled_extension_state_ambiguous";
  default:
    return "unknown";
  }
}

}  // namespace RoR::Render
