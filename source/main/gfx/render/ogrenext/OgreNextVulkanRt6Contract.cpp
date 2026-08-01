/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextVulkanRt6Contract.h"

namespace RoR::Render {

VulkanRt6CandidateDecision EvaluateVulkanRt6Candidate(
    const VulkanRt6CandidateContract& candidate) noexcept {
  if (candidate.api_major < 1U ||
      (candidate.api_major == 1U && candidate.api_minor < 2U)) {
    return VulkanRt6CandidateDecision::API_TOO_OLD;
  }
  if (candidate.known_software_adapter ||
      (candidate.device_class != VulkanRt5DeviceClass::INTEGRATED_GPU &&
       candidate.device_class != VulkanRt5DeviceClass::DISCRETE_GPU)) {
    return VulkanRt6CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE;
  }
  if (!candidate.has_graphics_queue) {
    return VulkanRt6CandidateDecision::GRAPHICS_QUEUE_UNAVAILABLE;
  }
  if (!candidate.timeline_semaphore_supported) {
    return VulkanRt6CandidateDecision::TIMELINE_SEMAPHORE_UNAVAILABLE;
  }
  if (!candidate.deferred_host_operations_extension) {
    return VulkanRt6CandidateDecision::
        DEFERRED_HOST_OPERATIONS_EXTENSION_UNAVAILABLE;
  }
  if (!candidate.buffer_device_address_extension) {
    return VulkanRt6CandidateDecision::
        BUFFER_DEVICE_ADDRESS_EXTENSION_UNAVAILABLE;
  }
  if (!candidate.acceleration_structure_extension) {
    return VulkanRt6CandidateDecision::
        ACCELERATION_STRUCTURE_EXTENSION_UNAVAILABLE;
  }
  if (!candidate.ray_tracing_pipeline_extension) {
    return VulkanRt6CandidateDecision::
        RAY_TRACING_PIPELINE_EXTENSION_UNAVAILABLE;
  }
  if (!candidate.buffer_device_address_feature) {
    return VulkanRt6CandidateDecision::
        BUFFER_DEVICE_ADDRESS_FEATURE_UNAVAILABLE;
  }
  if (!candidate.acceleration_structure_feature) {
    return VulkanRt6CandidateDecision::
        ACCELERATION_STRUCTURE_FEATURE_UNAVAILABLE;
  }
  if (!candidate.ray_tracing_pipeline_feature) {
    return VulkanRt6CandidateDecision::
        RAY_TRACING_PIPELINE_FEATURE_UNAVAILABLE;
  }
  if (!candidate.rgba32_uint_storage_image) {
    return VulkanRt6CandidateDecision::
        OUTPUT_STORAGE_IMAGE_FORMAT_UNAVAILABLE;
  }
  if (!candidate.ray_tracing_properties_valid) {
    return VulkanRt6CandidateDecision::RAY_TRACING_PROPERTIES_INVALID;
  }
  if (!candidate.every_ogre_observed_core_feature_enabled) {
    return VulkanRt6CandidateDecision::ENABLED_FEATURE_STATE_AMBIGUOUS;
  }
  if (!candidate.claimed_extension_set_is_exact) {
    return VulkanRt6CandidateDecision::ENABLED_EXTENSION_STATE_AMBIGUOUS;
  }
  return VulkanRt6CandidateDecision::ACCEPT;
}

bool VulkanRt6LifecycleContract::Advance(
    VulkanRt6LifecycleStage expected, VulkanRt6LifecycleStage next) noexcept {
  if (stage_ != expected) {
    return false;
  }
  stage_ = next;
  return true;
}

bool VulkanRt6LifecycleContract::MarkOwnerReady() noexcept {
  return Advance(VulkanRt6LifecycleStage::EMPTY,
                 VulkanRt6LifecycleStage::OWNER_READY);
}

bool VulkanRt6LifecycleContract::MarkRayResourcesReady() noexcept {
  return Advance(VulkanRt6LifecycleStage::OWNER_READY,
                 VulkanRt6LifecycleStage::RAY_RESOURCES_READY);
}

bool VulkanRt6LifecycleContract::MarkRayDispatched() noexcept {
  return Advance(VulkanRt6LifecycleStage::RAY_RESOURCES_READY,
                 VulkanRt6LifecycleStage::RAY_DISPATCHED);
}

bool VulkanRt6LifecycleContract::MarkOgreAttached() noexcept {
  return Advance(VulkanRt6LifecycleStage::RAY_DISPATCHED,
                 VulkanRt6LifecycleStage::OGRE_ATTACHED);
}

bool VulkanRt6LifecycleContract::MarkOgreDetached() noexcept {
  return Advance(VulkanRt6LifecycleStage::OGRE_ATTACHED,
                 VulkanRt6LifecycleStage::OGRE_DETACHED);
}

bool VulkanRt6LifecycleContract::MarkRayResourcesDestroyed() noexcept {
  return Advance(VulkanRt6LifecycleStage::OGRE_DETACHED,
                 VulkanRt6LifecycleStage::RAY_RESOURCES_DESTROYED);
}

bool VulkanRt6LifecycleContract::MarkTimelineDestroyed() noexcept {
  return Advance(VulkanRt6LifecycleStage::RAY_RESOURCES_DESTROYED,
                 VulkanRt6LifecycleStage::TIMELINE_DESTROYED);
}

bool VulkanRt6LifecycleContract::MarkDeviceDestroyed() noexcept {
  return Advance(VulkanRt6LifecycleStage::TIMELINE_DESTROYED,
                 VulkanRt6LifecycleStage::DEVICE_DESTROYED);
}

bool VulkanRt6LifecycleContract::MarkInstanceDestroyed() noexcept {
  return Advance(VulkanRt6LifecycleStage::DEVICE_DESTROYED,
                 VulkanRt6LifecycleStage::INSTANCE_DESTROYED);
}

}  // namespace RoR::Render
