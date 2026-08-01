/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextVulkanRt6Contract.h"

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

VulkanRt6CandidateContract HardwareCandidate() {
  VulkanRt6CandidateContract candidate;
  candidate.api_major = 1U;
  candidate.api_minor = 2U;
  candidate.device_class = VulkanRt5DeviceClass::DISCRETE_GPU;
  candidate.has_graphics_queue = true;
  candidate.timeline_semaphore_supported = true;
  candidate.deferred_host_operations_extension = true;
  candidate.buffer_device_address_extension = true;
  candidate.acceleration_structure_extension = true;
  candidate.ray_tracing_pipeline_extension = true;
  candidate.buffer_device_address_feature = true;
  candidate.acceleration_structure_feature = true;
  candidate.ray_tracing_pipeline_feature = true;
  candidate.rgba32_uint_storage_image = true;
  candidate.ray_tracing_properties_valid = true;
  candidate.every_ogre_observed_core_feature_enabled = true;
  candidate.claimed_extension_set_is_exact = true;
  return candidate;
}

void TestEveryRequiredExtensionAndFeatureFailsClosed() {
  VulkanRt6CandidateContract candidate = HardwareCandidate();
  Require(EvaluateVulkanRt6Candidate(candidate) ==
              VulkanRt6CandidateDecision::ACCEPT,
          "reviewed RT6 candidate was rejected");

  struct Mutation {
    bool VulkanRt6CandidateContract::*field;
    VulkanRt6CandidateDecision decision;
  };
  const Mutation mutations[] = {
      {&VulkanRt6CandidateContract::deferred_host_operations_extension,
       VulkanRt6CandidateDecision::
           DEFERRED_HOST_OPERATIONS_EXTENSION_UNAVAILABLE},
      {&VulkanRt6CandidateContract::buffer_device_address_extension,
       VulkanRt6CandidateDecision::
           BUFFER_DEVICE_ADDRESS_EXTENSION_UNAVAILABLE},
      {&VulkanRt6CandidateContract::acceleration_structure_extension,
       VulkanRt6CandidateDecision::ACCELERATION_STRUCTURE_EXTENSION_UNAVAILABLE},
      {&VulkanRt6CandidateContract::ray_tracing_pipeline_extension,
       VulkanRt6CandidateDecision::RAY_TRACING_PIPELINE_EXTENSION_UNAVAILABLE},
      {&VulkanRt6CandidateContract::buffer_device_address_feature,
       VulkanRt6CandidateDecision::BUFFER_DEVICE_ADDRESS_FEATURE_UNAVAILABLE},
      {&VulkanRt6CandidateContract::acceleration_structure_feature,
       VulkanRt6CandidateDecision::ACCELERATION_STRUCTURE_FEATURE_UNAVAILABLE},
      {&VulkanRt6CandidateContract::ray_tracing_pipeline_feature,
       VulkanRt6CandidateDecision::RAY_TRACING_PIPELINE_FEATURE_UNAVAILABLE},
      {&VulkanRt6CandidateContract::rgba32_uint_storage_image,
       VulkanRt6CandidateDecision::OUTPUT_STORAGE_IMAGE_FORMAT_UNAVAILABLE},
      {&VulkanRt6CandidateContract::ray_tracing_properties_valid,
       VulkanRt6CandidateDecision::RAY_TRACING_PROPERTIES_INVALID},
      {&VulkanRt6CandidateContract::every_ogre_observed_core_feature_enabled,
       VulkanRt6CandidateDecision::ENABLED_FEATURE_STATE_AMBIGUOUS},
      {&VulkanRt6CandidateContract::claimed_extension_set_is_exact,
       VulkanRt6CandidateDecision::ENABLED_EXTENSION_STATE_AMBIGUOUS},
  };
  for (const Mutation& mutation : mutations) {
    candidate = HardwareCandidate();
    candidate.*(mutation.field) = false;
    Require(EvaluateVulkanRt6Candidate(candidate) == mutation.decision,
            "RT6 requirement did not fail closed");
  }
}

void TestSoftwareAndBaseVulkanRequirementsFailBeforeRayTracing() {
  VulkanRt6CandidateContract candidate = HardwareCandidate();
  candidate.known_software_adapter = true;
  Require(EvaluateVulkanRt6Candidate(candidate) ==
              VulkanRt6CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE,
          "software adapter reached the RT6 extension checks");
  candidate = HardwareCandidate();
  candidate.api_minor = 1U;
  Require(EvaluateVulkanRt6Candidate(candidate) ==
              VulkanRt6CandidateDecision::API_TOO_OLD,
          "Vulkan 1.1 reached the RT6 extension checks");
  candidate = HardwareCandidate();
  candidate.has_graphics_queue = false;
  Require(EvaluateVulkanRt6Candidate(candidate) ==
              VulkanRt6CandidateDecision::GRAPHICS_QUEUE_UNAVAILABLE,
          "candidate without graphics queue reached RT6");
  candidate = HardwareCandidate();
  candidate.timeline_semaphore_supported = false;
  Require(EvaluateVulkanRt6Candidate(candidate) ==
              VulkanRt6CandidateDecision::TIMELINE_SEMAPHORE_UNAVAILABLE,
          "candidate without timeline semaphore reached RT6");
}

void TestLifecycleRequiresDispatchBeforeOgreAndOgreBeforeTeardown() {
  VulkanRt6LifecycleContract lifecycle;
  Require(!lifecycle.MarkRayResourcesReady(),
          "ray resources existed before the device owner");
  Require(lifecycle.MarkOwnerReady(), "owner-ready transition failed");
  Require(lifecycle.MarkRayResourcesReady(),
          "ray-resource transition failed");
  Require(!lifecycle.MarkOgreAttached(),
          "Ogre attached before a real ray dispatch");
  Require(lifecycle.MarkRayDispatched(), "ray-dispatch transition failed");
  Require(lifecycle.MarkOgreAttached(), "Ogre attach transition failed");
  Require(!lifecycle.MarkRayResourcesDestroyed(),
          "ray resources were destroyed while Ogre was attached");
  Require(lifecycle.MarkOgreDetached(), "Ogre detach transition failed");
  Require(lifecycle.MarkRayResourcesDestroyed(),
          "ray-resource teardown transition failed");
  Require(lifecycle.MarkTimelineDestroyed(),
          "timeline teardown transition failed");
  Require(lifecycle.MarkDeviceDestroyed(), "device teardown transition failed");
  Require(lifecycle.MarkInstanceDestroyed(),
          "instance teardown transition failed");
  Require(lifecycle.complete(), "RT6 lifecycle did not complete");
}

}  // namespace

int main() {
  try {
    TestEveryRequiredExtensionAndFeatureFailsClosed();
    TestSoftwareAndBaseVulkanRequirementsFailBeforeRayTracing();
    TestLifecycleRequiresDispatchBeforeOgreAndOgreBeforeTeardown();
    std::cout << "Ogre-Next Vulkan RT6 contract tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Ogre-Next Vulkan RT6 contract tests failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
