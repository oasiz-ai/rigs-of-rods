/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextVulkanRt5Contract.h"

namespace RoR::Render {

VulkanRt5CandidateDecision EvaluateVulkanRt5Candidate(
    const VulkanRt5CandidateContract& candidate) noexcept {
  if (candidate.api_major < 1U ||
      (candidate.api_major == 1U && candidate.api_minor < 2U)) {
    return VulkanRt5CandidateDecision::API_TOO_OLD;
  }
  if (candidate.known_software_adapter ||
      (candidate.device_class != VulkanRt5DeviceClass::INTEGRATED_GPU &&
       candidate.device_class != VulkanRt5DeviceClass::DISCRETE_GPU)) {
    return VulkanRt5CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE;
  }
  if (!candidate.has_graphics_queue) {
    return VulkanRt5CandidateDecision::GRAPHICS_QUEUE_UNAVAILABLE;
  }
  if (!candidate.timeline_semaphore_supported) {
    return VulkanRt5CandidateDecision::TIMELINE_SEMAPHORE_UNAVAILABLE;
  }
  if (!candidate.every_ogre_observed_core_feature_enabled) {
    return VulkanRt5CandidateDecision::ENABLED_FEATURE_STATE_AMBIGUOUS;
  }
  if (!candidate.claimed_extension_set_is_exact) {
    return VulkanRt5CandidateDecision::ENABLED_EXTENSION_STATE_AMBIGUOUS;
  }
  return VulkanRt5CandidateDecision::ACCEPT;
}

bool VulkanRt5LifecycleContract::Advance(
    VulkanRt5LifecycleStage expected, VulkanRt5LifecycleStage next) noexcept {
  if (stage_ != expected) {
    return false;
  }
  stage_ = next;
  return true;
}

bool VulkanRt5LifecycleContract::MarkOwnerReady() noexcept {
  return Advance(VulkanRt5LifecycleStage::EMPTY,
                 VulkanRt5LifecycleStage::OWNER_READY);
}

bool VulkanRt5LifecycleContract::MarkOgreAttached() noexcept {
  return Advance(VulkanRt5LifecycleStage::OWNER_READY,
                 VulkanRt5LifecycleStage::OGRE_ATTACHED);
}

bool VulkanRt5LifecycleContract::MarkOgreDetached() noexcept {
  return Advance(VulkanRt5LifecycleStage::OGRE_ATTACHED,
                 VulkanRt5LifecycleStage::OGRE_DETACHED);
}

bool VulkanRt5LifecycleContract::MarkTimelineDestroyed() noexcept {
  return Advance(VulkanRt5LifecycleStage::OGRE_DETACHED,
                 VulkanRt5LifecycleStage::TIMELINE_DESTROYED);
}

bool VulkanRt5LifecycleContract::MarkDeviceDestroyed() noexcept {
  return Advance(VulkanRt5LifecycleStage::TIMELINE_DESTROYED,
                 VulkanRt5LifecycleStage::DEVICE_DESTROYED);
}

bool VulkanRt5LifecycleContract::MarkInstanceDestroyed() noexcept {
  return Advance(VulkanRt5LifecycleStage::DEVICE_DESTROYED,
                 VulkanRt5LifecycleStage::INSTANCE_DESTROYED);
}

}  // namespace RoR::Render
