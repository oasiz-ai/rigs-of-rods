/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include <cstdint>

namespace RoR::Render {

enum class VulkanRt5DeviceClass : std::uint8_t {
  OTHER = 0,
  INTEGRATED_GPU,
  DISCRETE_GPU,
  VIRTUAL_GPU,
  CPU,
};

struct VulkanRt5CandidateContract {
  std::uint32_t api_major = 0U;
  std::uint32_t api_minor = 0U;
  VulkanRt5DeviceClass device_class = VulkanRt5DeviceClass::OTHER;
  bool known_software_adapter = false;
  bool has_graphics_queue = false;
  bool timeline_semaphore_supported = false;
  bool every_ogre_observed_core_feature_enabled = false;
  bool claimed_extension_set_is_exact = false;
};

enum class VulkanRt5CandidateDecision : std::uint8_t {
  ACCEPT = 0,
  API_TOO_OLD,
  SOFTWARE_OR_UNATTESTED_DEVICE,
  GRAPHICS_QUEUE_UNAVAILABLE,
  TIMELINE_SEMAPHORE_UNAVAILABLE,
  ENABLED_FEATURE_STATE_AMBIGUOUS,
  ENABLED_EXTENSION_STATE_AMBIGUOUS,
};

VulkanRt5CandidateDecision EvaluateVulkanRt5Candidate(
    const VulkanRt5CandidateContract& candidate) noexcept;

enum class VulkanRt5LifecycleStage : std::uint8_t {
  EMPTY = 0,
  OWNER_READY,
  OGRE_ATTACHED,
  OGRE_DETACHED,
  TIMELINE_DESTROYED,
  DEVICE_DESTROYED,
  INSTANCE_DESTROYED,
};

class VulkanRt5LifecycleContract final {
 public:
  bool MarkOwnerReady() noexcept;
  bool MarkOgreAttached() noexcept;
  bool MarkOgreDetached() noexcept;
  bool MarkTimelineDestroyed() noexcept;
  bool MarkDeviceDestroyed() noexcept;
  bool MarkInstanceDestroyed() noexcept;

  VulkanRt5LifecycleStage stage() const noexcept { return stage_; }
  bool complete() const noexcept {
    return stage_ == VulkanRt5LifecycleStage::INSTANCE_DESTROYED;
  }

 private:
  bool Advance(VulkanRt5LifecycleStage expected,
               VulkanRt5LifecycleStage next) noexcept;

  VulkanRt5LifecycleStage stage_ = VulkanRt5LifecycleStage::EMPTY;
};

}  // namespace RoR::Render
