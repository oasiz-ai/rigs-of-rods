/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include "OgreNextVulkanRt6Contract.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Ogre {
class RenderSystem;
}

namespace RoR::Render {

enum class VulkanRt6BootstrapCode : std::uint8_t {
  READY = 0,
  UNSUPPORTED,
  FAILURE,
};

struct VulkanRt6BootstrapResult {
  VulkanRt6BootstrapCode code = VulkanRt6BootstrapCode::FAILURE;
  std::string message;

  bool ready() const noexcept { return code == VulkanRt6BootstrapCode::READY; }
  bool unsupported() const noexcept {
    return code == VulkanRt6BootstrapCode::UNSUPPORTED;
  }
};

struct VulkanRt6BootstrapEvidence {
  std::uint32_t loader_api_version = 0U;
  std::uint32_t requested_instance_api_version = 0U;
  std::uint32_t physical_device_api_version = 0U;
  std::uint32_t driver_version = 0U;
  std::uint32_t vendor_id = 0U;
  std::uint32_t device_id = 0U;
  std::uint32_t graphics_queue_family = 0U;
  std::uint32_t graphics_queue_index = 0U;
  std::uint32_t enabled_device_extension_count = 0U;
  std::uint32_t shader_group_handle_size = 0U;
  std::uint32_t shader_group_handle_alignment = 0U;
  std::uint32_t shader_group_base_alignment = 0U;
  std::uint32_t max_ray_recursion_depth = 0U;
  std::uint32_t acceleration_structure_scratch_alignment = 0U;
  std::uint64_t geometry_buffer_device_address = 0U;
  std::uint64_t instance_buffer_device_address = 0U;
  std::uint64_t scratch_buffer_device_address = 0U;
  std::uint64_t shader_binding_table_device_address = 0U;
  std::uint64_t blas_device_address = 0U;
  std::uint64_t tlas_device_address = 0U;
  std::uint64_t timeline_value_before_ray_dispatch = 0U;
  std::uint64_t timeline_value_at_ray_dispatch = 0U;
  std::uint64_t timeline_value_after_ogre = 0U;
  std::string device_name;
  std::string device_uuid;
  VulkanRt5DeviceClass device_class = VulkanRt5DeviceClass::OTHER;
  VulkanRt6CandidateDecision candidate_decision =
      VulkanRt6CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE;
  bool known_software_adapter = false;
  bool device_identity_available = false;
  bool graphics_queue_available = false;
  bool compute_on_graphics_queue_available = false;
  bool timeline_semaphore_supported = false;
  bool deferred_host_operations_extension_supported = false;
  bool buffer_device_address_extension_supported = false;
  bool acceleration_structure_extension_supported = false;
  bool ray_tracing_pipeline_extension_supported = false;
  bool buffer_device_address_feature_supported = false;
  bool acceleration_structure_feature_supported = false;
  bool ray_tracing_pipeline_feature_supported = false;
  bool output_storage_image_format_supported = false;
  bool ray_tracing_properties_valid = false;
  bool all_supported_core_features_enabled = false;
  bool timeline_semaphore_enabled = false;
  bool buffer_device_address_feature_enabled = false;
  bool acceleration_structure_feature_enabled = false;
  bool ray_tracing_pipeline_feature_enabled = false;
  bool enabled_instance_extensions_exact = false;
  bool enabled_device_extensions_exact = false;
  bool instance_injected_exactly = false;
  bool physical_device_injected_exactly = false;
  bool logical_device_injected_exactly = false;
  bool graphics_queue_injected_exactly = false;
  bool ogre_external_ownership_observed = false;
  bool geometry_buffer_created = false;
  bool blas_built = false;
  bool tlas_built = false;
  bool descriptor_set_bound = false;
  bool shader_contract_compiled = false;
  bool ray_pipeline_created = false;
  bool shader_binding_table_created = false;
  bool ray_dispatch_completed = false;
  bool output_image_copied_to_host = false;
  bool primary_hit_observed = false;
  std::array<std::uint32_t, 4U> readback_words{};
  bool ogre_shutdown_before_owner_teardown = false;
  bool ray_resources_destroyed_before_device = false;
  bool timeline_destroyed_before_device = false;
  bool device_destroyed_before_instance = false;
  bool shutdown_completed = false;
};

class OgreNextVulkanRayTracingBootstrap final {
public:
  OgreNextVulkanRayTracingBootstrap();
  ~OgreNextVulkanRayTracingBootstrap();

  OgreNextVulkanRayTracingBootstrap(const OgreNextVulkanRayTracingBootstrap&) =
      delete;
  OgreNextVulkanRayTracingBootstrap&
  operator=(const OgreNextVulkanRayTracingBootstrap&) = delete;

  VulkanRt6BootstrapResult Initialize();
  VulkanRt6BootstrapResult ValidateShaderContract();
  VulkanRt6BootstrapResult ProveTimelineQueue(std::uint64_t signal_value);
  VulkanRt6BootstrapResult ProveRayTracingDispatch(std::uint64_t signal_value);
  std::uintptr_t external_instance_descriptor_address() const noexcept;
  std::uintptr_t external_device_descriptor_address() const noexcept;
  VulkanRt6BootstrapResult MarkOgreAttached();
  VulkanRt6BootstrapResult
  VerifyOgreAdoption(Ogre::RenderSystem* render_system);
  VulkanRt6BootstrapResult MarkOgreDetached();
  VulkanRt6BootstrapResult Shutdown();
  VulkanRt6BootstrapResult AbortAfterFailure();

  const VulkanRt6BootstrapEvidence& evidence() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

const char* VulkanRt6DeviceClassName(VulkanRt5DeviceClass value) noexcept;
const char*
VulkanRt6CandidateDecisionName(VulkanRt6CandidateDecision value) noexcept;

}  // namespace RoR::Render
