/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextVulkanRt5Contract.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

VulkanRt5CandidateContract HardwareCandidate() {
  VulkanRt5CandidateContract candidate;
  candidate.api_major = 1U;
  candidate.api_minor = 2U;
  candidate.device_class = VulkanRt5DeviceClass::DISCRETE_GPU;
  candidate.has_graphics_queue = true;
  candidate.timeline_semaphore_supported = true;
  candidate.every_ogre_observed_core_feature_enabled = true;
  candidate.claimed_extension_set_is_exact = true;
  return candidate;
}

void TestHardwareRequiresAnUnambiguousEnabledFeatureContract() {
  VulkanRt5CandidateContract candidate = HardwareCandidate();
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::ACCEPT,
          "reviewed hardware candidate was rejected");

  candidate.every_ogre_observed_core_feature_enabled = false;
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::ENABLED_FEATURE_STATE_AMBIGUOUS,
          "supported features were confused with enabled features");
  candidate.every_ogre_observed_core_feature_enabled = true;
  candidate.claimed_extension_set_is_exact = false;
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::ENABLED_EXTENSION_STATE_AMBIGUOUS,
          "supported extensions were confused with enabled extensions");
}

void TestSoftwareAndUnattestedDevicesNeverPass() {
  VulkanRt5CandidateContract candidate = HardwareCandidate();
  candidate.known_software_adapter = true;
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE,
          "known software adapter reported a hardware pass");

  candidate.known_software_adapter = false;
  candidate.device_class = VulkanRt5DeviceClass::CPU;
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE,
          "CPU Vulkan adapter reported a hardware pass");
  candidate.device_class = VulkanRt5DeviceClass::VIRTUAL_GPU;
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE,
          "unattested virtual adapter reported a hardware pass");
}

void TestMinimumVulkanQueueAndTimelineFloor() {
  VulkanRt5CandidateContract candidate = HardwareCandidate();
  candidate.api_minor = 1U;
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::API_TOO_OLD,
          "Vulkan 1.1 passed the 1.2 floor");
  candidate.api_minor = 2U;
  candidate.has_graphics_queue = false;
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::GRAPHICS_QUEUE_UNAVAILABLE,
          "candidate without a graphics queue passed");
  candidate.has_graphics_queue = true;
  candidate.timeline_semaphore_supported = false;
  Require(EvaluateVulkanRt5Candidate(candidate) ==
              VulkanRt5CandidateDecision::TIMELINE_SEMAPHORE_UNAVAILABLE,
          "candidate without timeline semaphores passed");
}

void TestLifecycleRequiresOgreBeforeOwnedVulkanTeardown() {
  VulkanRt5LifecycleContract lifecycle;
  Require(!lifecycle.MarkOgreAttached(),
          "Ogre attached before the RoR-owned device existed");
  Require(lifecycle.MarkOwnerReady(), "owner-ready transition failed");
  Require(lifecycle.MarkOgreAttached(), "Ogre attach transition failed");
  Require(!lifecycle.MarkTimelineDestroyed(),
          "timeline was destroyed while Ogre remained attached");
  Require(!lifecycle.MarkDeviceDestroyed(),
          "device was destroyed while Ogre remained attached");
  Require(lifecycle.MarkOgreDetached(), "Ogre detach transition failed");
  Require(lifecycle.MarkTimelineDestroyed(),
          "timeline teardown transition failed");
  Require(!lifecycle.MarkInstanceDestroyed(),
          "instance was destroyed before the logical device");
  Require(lifecycle.MarkDeviceDestroyed(), "device teardown transition failed");
  Require(lifecycle.MarkInstanceDestroyed(),
          "instance teardown transition failed");
  Require(lifecycle.complete(), "lifecycle did not reach complete teardown");
}

}  // namespace

int main() {
  try {
    TestHardwareRequiresAnUnambiguousEnabledFeatureContract();
    TestSoftwareAndUnattestedDevicesNeverPass();
    TestMinimumVulkanQueueAndTimelineFloor();
    TestLifecycleRequiresOgreBeforeOwnedVulkanTeardown();
    std::cout << "Ogre-Next Vulkan RT5 contract tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Ogre-Next Vulkan RT5 contract tests failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
