/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include "OgreNextVulkanRt5Contract.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Ogre {
class RenderSystem;
}

namespace RoR::Render {

enum class VulkanRt5BootstrapCode : std::uint8_t {
  READY = 0,
  UNSUPPORTED,
  FAILURE,
};

struct VulkanRt5BootstrapResult {
  VulkanRt5BootstrapCode code = VulkanRt5BootstrapCode::FAILURE;
  std::string message;

  bool ready() const noexcept { return code == VulkanRt5BootstrapCode::READY; }
  bool unsupported() const noexcept {
    return code == VulkanRt5BootstrapCode::UNSUPPORTED;
  }
};

struct VulkanRt5BootstrapEvidence {
  std::uint32_t loader_api_version = 0U;
  std::uint32_t requested_instance_api_version = 0U;
  std::uint32_t physical_device_api_version = 0U;
  std::uint32_t driver_version = 0U;
  std::uint32_t vendor_id = 0U;
  std::uint32_t device_id = 0U;
  std::uint32_t graphics_queue_family = 0U;
  std::uint32_t graphics_queue_index = 0U;
  std::uint64_t timeline_value_before_ogre = 0U;
  std::uint64_t timeline_value_after_ogre = 0U;
  std::string device_name;
  std::string device_uuid;
  VulkanRt5DeviceClass device_class = VulkanRt5DeviceClass::OTHER;
  VulkanRt5CandidateDecision candidate_decision =
      VulkanRt5CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE;
  bool known_software_adapter = false;
  bool graphics_queue_available = false;
  bool timeline_semaphore_supported = false;
  bool all_supported_core_features_enabled = false;
  bool enabled_instance_extensions_exact = false;
  bool enabled_device_extensions_exact = false;
  bool instance_injected_exactly = false;
  bool physical_device_injected_exactly = false;
  bool logical_device_injected_exactly = false;
  bool graphics_queue_injected_exactly = false;
  bool ogre_external_ownership_observed = false;
  bool ogre_shutdown_before_owner_teardown = false;
  bool timeline_destroyed_before_device = false;
  bool device_destroyed_before_instance = false;
  bool shutdown_completed = false;
};

class OgreNextVulkanExternalDeviceBootstrap final {
 public:
  OgreNextVulkanExternalDeviceBootstrap();
  ~OgreNextVulkanExternalDeviceBootstrap();

  OgreNextVulkanExternalDeviceBootstrap(
      const OgreNextVulkanExternalDeviceBootstrap&) = delete;
  OgreNextVulkanExternalDeviceBootstrap& operator=(
      const OgreNextVulkanExternalDeviceBootstrap&) = delete;

  VulkanRt5BootstrapResult Initialize();
  std::uintptr_t external_instance_descriptor_address() const noexcept;
  std::uintptr_t external_device_descriptor_address() const noexcept;

  VulkanRt5BootstrapResult MarkOgreAttached() noexcept;
  VulkanRt5BootstrapResult VerifyOgreAdoption(
      Ogre::RenderSystem* render_system) noexcept;
  VulkanRt5BootstrapResult MarkOgreDetached() noexcept;
  VulkanRt5BootstrapResult ProveTimelineQueue(
      std::uint64_t signal_value) noexcept;
  VulkanRt5BootstrapResult Shutdown() noexcept;
  VulkanRt5BootstrapResult AbortAfterFailure() noexcept;

  const VulkanRt5BootstrapEvidence& evidence() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

const char* VulkanRt5DeviceClassName(VulkanRt5DeviceClass value) noexcept;
const char* VulkanRt5CandidateDecisionName(
    VulkanRt5CandidateDecision value) noexcept;

}  // namespace RoR::Render
