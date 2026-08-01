/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include "OgreNextVulkanRt5Contract.h"

#include <cstdint>

namespace RoR::Render {

struct VulkanRt6CandidateContract {
  std::uint32_t api_major = 0U;
  std::uint32_t api_minor = 0U;
  VulkanRt5DeviceClass device_class = VulkanRt5DeviceClass::OTHER;
  bool known_software_adapter = false;
  bool has_graphics_queue = false;
  bool timeline_semaphore_supported = false;
  bool deferred_host_operations_extension = false;
  bool buffer_device_address_extension = false;
  bool acceleration_structure_extension = false;
  bool ray_tracing_pipeline_extension = false;
  bool buffer_device_address_feature = false;
  bool acceleration_structure_feature = false;
  bool ray_tracing_pipeline_feature = false;
  bool rgba32_uint_storage_image = false;
  bool ray_tracing_properties_valid = false;
  bool every_ogre_observed_core_feature_enabled = false;
  bool claimed_extension_set_is_exact = false;
};

enum class VulkanRt6CandidateDecision : std::uint8_t {
  ACCEPT = 0,
  API_TOO_OLD,
  SOFTWARE_OR_UNATTESTED_DEVICE,
  GRAPHICS_QUEUE_UNAVAILABLE,
  TIMELINE_SEMAPHORE_UNAVAILABLE,
  DEFERRED_HOST_OPERATIONS_EXTENSION_UNAVAILABLE,
  BUFFER_DEVICE_ADDRESS_EXTENSION_UNAVAILABLE,
  ACCELERATION_STRUCTURE_EXTENSION_UNAVAILABLE,
  RAY_TRACING_PIPELINE_EXTENSION_UNAVAILABLE,
  BUFFER_DEVICE_ADDRESS_FEATURE_UNAVAILABLE,
  ACCELERATION_STRUCTURE_FEATURE_UNAVAILABLE,
  RAY_TRACING_PIPELINE_FEATURE_UNAVAILABLE,
  OUTPUT_STORAGE_IMAGE_FORMAT_UNAVAILABLE,
  RAY_TRACING_PROPERTIES_INVALID,
  ENABLED_FEATURE_STATE_AMBIGUOUS,
  ENABLED_EXTENSION_STATE_AMBIGUOUS,
};

VulkanRt6CandidateDecision EvaluateVulkanRt6Candidate(
    const VulkanRt6CandidateContract& candidate) noexcept;

enum class VulkanRt6LifecycleStage : std::uint8_t {
  EMPTY = 0,
  OWNER_READY,
  RAY_RESOURCES_READY,
  RAY_DISPATCHED,
  OGRE_ATTACHED,
  OGRE_DETACHED,
  RAY_RESOURCES_DESTROYED,
  TIMELINE_DESTROYED,
  DEVICE_DESTROYED,
  INSTANCE_DESTROYED,
};

class VulkanRt6LifecycleContract final {
 public:
  bool MarkOwnerReady() noexcept;
  bool MarkRayResourcesReady() noexcept;
  bool MarkRayDispatched() noexcept;
  bool MarkOgreAttached() noexcept;
  bool MarkOgreDetached() noexcept;
  bool MarkRayResourcesDestroyed() noexcept;
  bool MarkTimelineDestroyed() noexcept;
  bool MarkDeviceDestroyed() noexcept;
  bool MarkInstanceDestroyed() noexcept;

  VulkanRt6LifecycleStage stage() const noexcept { return stage_; }
  bool complete() const noexcept {
    return stage_ == VulkanRt6LifecycleStage::INSTANCE_DESTROYED;
  }

 private:
  bool Advance(VulkanRt6LifecycleStage expected,
               VulkanRt6LifecycleStage next) noexcept;

  VulkanRt6LifecycleStage stage_ = VulkanRt6LifecycleStage::EMPTY;
};

}  // namespace RoR::Render
