/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextVulkanExternalDeviceBootstrap.h"

#if !defined(ROR_OGRE_NEXT_N1_VULKAN)
#error "The RT5 external-device bootstrap is reviewed only for Linux Vulkan"
#endif

#include "OgreVulkanDevice.h"
#include "OgreVulkanRenderSystem.h"

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

constexpr std::uint64_t kTimelineWaitNanoseconds = 5'000'000'000ULL;

VulkanRt5BootstrapResult Ready() {
  return {VulkanRt5BootstrapCode::READY, {}};
}

VulkanRt5BootstrapResult Unsupported(std::string message) {
  return {VulkanRt5BootstrapCode::UNSUPPORTED, std::move(message)};
}

VulkanRt5BootstrapResult Failure(std::string message) {
  return {VulkanRt5BootstrapCode::FAILURE, std::move(message)};
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
  for (const char* marker : {"lavapipe", "llvmpipe", "swiftshader",
                             "software rasterizer", "software renderer",
                             "microsoft basic render"}) {
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

struct Candidate {
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceFeatures2 features2{};
  VkPhysicalDeviceVulkan12Features vulkan12_features{};
  VkPhysicalDeviceIDProperties id_properties{};
  std::uint32_t graphics_queue_family =
      std::numeric_limits<std::uint32_t>::max();
  VulkanRt5CandidateContract contract;
};

Candidate InspectCandidate(VkPhysicalDevice physical_device) {
  Candidate candidate;
  candidate.physical_device = physical_device;
  candidate.features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  candidate.vulkan12_features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  candidate.features2.pNext = &candidate.vulkan12_features;
  vkGetPhysicalDeviceFeatures2(physical_device, &candidate.features2);

  VkPhysicalDeviceProperties2 properties2{};
  properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  candidate.id_properties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
  properties2.pNext = &candidate.id_properties;
  vkGetPhysicalDeviceProperties2(physical_device, &properties2);
  candidate.properties = properties2.properties;

  std::uint32_t queue_family_count = 0U;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                           &queue_family_count, nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  if (queue_family_count > 0U) {
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device, &queue_family_count, queue_families.data());
  }
  for (std::uint32_t index = 0U; index < queue_family_count; ++index) {
    if (queue_families[index].queueCount > 0U &&
        (queue_families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
      candidate.graphics_queue_family = index;
      break;
    }
  }

  candidate.contract.api_major =
      VK_API_VERSION_MAJOR(candidate.properties.apiVersion);
  candidate.contract.api_minor =
      VK_API_VERSION_MINOR(candidate.properties.apiVersion);
  candidate.contract.device_class =
      MapDeviceClass(candidate.properties.deviceType);
  candidate.contract.known_software_adapter = IsKnownSoftwareAdapter(
      candidate.properties.deviceName, candidate.contract.device_class);
  candidate.contract.has_graphics_queue =
      candidate.graphics_queue_family !=
      std::numeric_limits<std::uint32_t>::max();
  candidate.contract.timeline_semaphore_supported =
      candidate.vulkan12_features.timelineSemaphore == VK_TRUE;
  // The owner passes the complete supported VkPhysicalDeviceFeatures value to
  // vkCreateDevice. This is intentionally broader than Ogre's supported-bit
  // projection, eliminating Ogre v3.0's supported-vs-enabled ambiguity.
  candidate.contract.every_ogre_observed_core_feature_enabled = true;
  // RT5 claims no device extensions. The empty set exactly matches the set
  // passed to vkCreateDevice; Vulkan 1.2 timeline semaphores are core.
  candidate.contract.claimed_extension_set_is_exact = true;
  return candidate;
}

int CandidateRank(const Candidate& candidate) {
  if (EvaluateVulkanRt5Candidate(candidate.contract) !=
      VulkanRt5CandidateDecision::ACCEPT) {
    return 0;
  }
  return candidate.contract.device_class == VulkanRt5DeviceClass::DISCRETE_GPU
             ? 2
             : 1;
}

}  // namespace

struct OgreNextVulkanExternalDeviceBootstrap::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphics_queue = VK_NULL_HANDLE;
  VkSemaphore timeline = VK_NULL_HANDLE;
  Ogre::VulkanExternalInstance external_instance{};
  Ogre::VulkanExternalDevice external_device{};
  VulkanRt5LifecycleContract lifecycle;
  VulkanRt5BootstrapEvidence evidence;
  bool ogre_attached = false;

  void RecordCandidate(const Candidate& candidate,
                       VulkanRt5CandidateDecision decision) {
    evidence.physical_device_api_version = candidate.properties.apiVersion;
    evidence.driver_version = candidate.properties.driverVersion;
    evidence.vendor_id = candidate.properties.vendorID;
    evidence.device_id = candidate.properties.deviceID;
    evidence.device_name = candidate.properties.deviceName;
    evidence.device_uuid = HexUuid(candidate.id_properties.deviceUUID,
                                   VK_UUID_SIZE);
    evidence.device_class = candidate.contract.device_class;
    evidence.candidate_decision = decision;
    evidence.known_software_adapter =
        candidate.contract.known_software_adapter;
    evidence.graphics_queue_available = candidate.contract.has_graphics_queue;
    evidence.timeline_semaphore_supported =
        candidate.contract.timeline_semaphore_supported;
    evidence.all_supported_core_features_enabled = false;
    evidence.enabled_instance_extensions_exact = true;
    evidence.enabled_device_extensions_exact = false;
    if (candidate.contract.has_graphics_queue) {
      evidence.graphics_queue_family = candidate.graphics_queue_family;
      evidence.graphics_queue_index = 0U;
    }
  }

  VulkanRt5BootstrapResult DestroyPartial() noexcept {
    if (device != VK_NULL_HANDLE) {
      const VkResult idle_result = vkDeviceWaitIdle(device);
      if (idle_result != VK_SUCCESS) {
        return Failure(VkFailure("vkDeviceWaitIdle", idle_result));
      }
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

OgreNextVulkanExternalDeviceBootstrap::
    OgreNextVulkanExternalDeviceBootstrap()
    : impl_(std::make_unique<Impl>()) {}

OgreNextVulkanExternalDeviceBootstrap::
    ~OgreNextVulkanExternalDeviceBootstrap() {
  if (impl_ && !impl_->ogre_attached) {
    static_cast<void>(impl_->DestroyPartial());
  }
}

VulkanRt5BootstrapResult
OgreNextVulkanExternalDeviceBootstrap::Initialize() {
  if (impl_->instance != VK_NULL_HANDLE || impl_->device != VK_NULL_HANDLE) {
    return Failure("RT5 bootstrap cannot be initialized twice");
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
    return Unsupported("Vulkan loader does not expose Vulkan 1.2");
  }

  VkApplicationInfo application_info{};
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pApplicationName = "Rigs of Rods Ogre-Next RT5";
  application_info.applicationVersion = VK_MAKE_VERSION(0, 5, 0);
  application_info.pEngineName = "Rigs of Rods";
  application_info.engineVersion = VK_MAKE_VERSION(0, 5, 0);
  application_info.apiVersion = VK_API_VERSION_1_2;

  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &application_info;
  // Deliberately exact: no layers or instance extensions are claimed or
  // enabled for the null-window external-device proof.
  const VkResult instance_result =
      vkCreateInstance(&instance_info, nullptr, &impl_->instance);
  if (instance_result != VK_SUCCESS) {
    return Failure(VkFailure("vkCreateInstance", instance_result));
  }

  std::uint32_t physical_device_count = 0U;
  VkResult result = vkEnumeratePhysicalDevices(
      impl_->instance, &physical_device_count, nullptr);
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
  int selected_rank = 0;
  bool have_fallback = false;
  for (VkPhysicalDevice physical_device : physical_devices) {
    Candidate candidate = InspectCandidate(physical_device);
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
    const VulkanRt5CandidateDecision decision =
        EvaluateVulkanRt5Candidate(fallback.contract);
    impl_->RecordCandidate(fallback, decision);
    static_cast<void>(impl_->DestroyPartial());
    return Unsupported(std::string("no attested RT5 hardware device: ") +
                       VulkanRt5CandidateDecisionName(decision));
  }

  const VulkanRt5CandidateDecision decision =
      EvaluateVulkanRt5Candidate(selected.contract);
  impl_->RecordCandidate(selected, decision);
  if (decision != VulkanRt5CandidateDecision::ACCEPT) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure("selected Vulkan candidate violated the RT5 policy");
  }
  impl_->physical_device = selected.physical_device;

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

  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.pNext = &enabled_vulkan12;
  device_info.queueCreateInfoCount = 1U;
  device_info.pQueueCreateInfos = &queue_info;
  // Enable every supported core bit because pinned Ogre v3.0 projects its
  // internal feature state from support rather than the external enable list.
  device_info.pEnabledFeatures = &selected.features2.features;
  // Deliberately exact: no device extensions are enabled or claimed.

  result = vkCreateDevice(impl_->physical_device, &device_info, nullptr,
                          &impl_->device);
  if (result != VK_SUCCESS) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure(VkFailure("vkCreateDevice", result));
  }
  impl_->evidence.all_supported_core_features_enabled = true;
  impl_->evidence.enabled_device_extensions_exact = true;
  vkGetDeviceQueue(impl_->device, selected.graphics_queue_family, 0U,
                   &impl_->graphics_queue);
  if (impl_->graphics_queue == VK_NULL_HANDLE) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure("vkGetDeviceQueue returned a null graphics queue");
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
  impl_->external_device.graphicsQueue = impl_->graphics_queue;
  impl_->external_device.presentQueue = impl_->graphics_queue;

  if (!impl_->lifecycle.MarkOwnerReady()) {
    static_cast<void>(impl_->DestroyPartial());
    return Failure("RT5 lifecycle rejected owner-ready initialization");
  }
  return Ready();
}

std::uintptr_t OgreNextVulkanExternalDeviceBootstrap::
    external_instance_descriptor_address() const noexcept {
  return reinterpret_cast<std::uintptr_t>(&impl_->external_instance);
}

std::uintptr_t OgreNextVulkanExternalDeviceBootstrap::
    external_device_descriptor_address() const noexcept {
  return reinterpret_cast<std::uintptr_t>(&impl_->external_device);
}

VulkanRt5BootstrapResult
OgreNextVulkanExternalDeviceBootstrap::MarkOgreAttached() noexcept {
  if (!impl_->lifecycle.MarkOgreAttached()) {
    return Failure("RT5 lifecycle rejected Ogre attachment");
  }
  impl_->ogre_attached = true;
  return Ready();
}

VulkanRt5BootstrapResult
OgreNextVulkanExternalDeviceBootstrap::VerifyOgreAdoption(
    Ogre::RenderSystem* render_system) noexcept {
  auto* vulkan_render_system =
      dynamic_cast<Ogre::VulkanRenderSystem*>(render_system);
  if (vulkan_render_system == nullptr) {
    return Failure("active Ogre renderer is not the pinned Vulkan renderer");
  }
  Ogre::VulkanDevice* ogre_device =
      vulkan_render_system->getVulkanDevice();
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
    return Failure("Ogre did not adopt the exact RoR-owned Vulkan object set");
  }
  return Ready();
}

VulkanRt5BootstrapResult
OgreNextVulkanExternalDeviceBootstrap::MarkOgreDetached() noexcept {
  if (!impl_->lifecycle.MarkOgreDetached()) {
    return Failure("RT5 lifecycle rejected Ogre detachment");
  }
  impl_->ogre_attached = false;
  impl_->evidence.ogre_shutdown_before_owner_teardown =
      impl_->device != VK_NULL_HANDLE && impl_->instance != VK_NULL_HANDLE &&
      impl_->timeline != VK_NULL_HANDLE;
  if (!impl_->evidence.ogre_shutdown_before_owner_teardown) {
    return Failure("owned Vulkan state disappeared before Ogre shutdown");
  }
  return Ready();
}

VulkanRt5BootstrapResult
OgreNextVulkanExternalDeviceBootstrap::ProveTimelineQueue(
    std::uint64_t signal_value) noexcept {
  if (impl_->device == VK_NULL_HANDLE || impl_->graphics_queue == VK_NULL_HANDLE ||
      impl_->timeline == VK_NULL_HANDLE || signal_value == 0U) {
    return Failure("timeline proof requires live owned Vulkan objects");
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
  VkResult result = vkQueueSubmit(impl_->graphics_queue, 1U, &submit,
                                  VK_NULL_HANDLE);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkQueueSubmit(timeline)", result));
  }

  VkSemaphoreWaitInfo wait_info{};
  wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  wait_info.semaphoreCount = 1U;
  wait_info.pSemaphores = &impl_->timeline;
  wait_info.pValues = &signal_value;
  result = vkWaitSemaphores(impl_->device, &wait_info,
                            kTimelineWaitNanoseconds);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkWaitSemaphores", result));
  }
  std::uint64_t observed_value = 0U;
  result = vkGetSemaphoreCounterValue(impl_->device, impl_->timeline,
                                      &observed_value);
  if (result != VK_SUCCESS) {
    return Failure(VkFailure("vkGetSemaphoreCounterValue", result));
  }
  if (observed_value < signal_value) {
    return Failure("timeline semaphore did not reach the submitted value");
  }
  if (impl_->evidence.timeline_value_before_ogre == 0U) {
    impl_->evidence.timeline_value_before_ogre = observed_value;
  } else {
    impl_->evidence.timeline_value_after_ogre = observed_value;
  }
  return Ready();
}

VulkanRt5BootstrapResult
OgreNextVulkanExternalDeviceBootstrap::Shutdown() noexcept {
  if (impl_->ogre_attached) {
    return Failure("cannot destroy RoR-owned Vulkan objects while Ogre is attached");
  }
  if (impl_->lifecycle.stage() != VulkanRt5LifecycleStage::OGRE_DETACHED) {
    return Failure("full RT5 shutdown requires a completed Ogre attachment cycle");
  }
  const VkResult idle_result = vkDeviceWaitIdle(impl_->device);
  if (idle_result != VK_SUCCESS) {
    return Failure(VkFailure("vkDeviceWaitIdle", idle_result));
  }
  vkDestroySemaphore(impl_->device, impl_->timeline, nullptr);
  impl_->timeline = VK_NULL_HANDLE;
  if (!impl_->lifecycle.MarkTimelineDestroyed()) {
    return Failure("RT5 lifecycle rejected timeline teardown");
  }
  impl_->evidence.timeline_destroyed_before_device =
      impl_->device != VK_NULL_HANDLE;

  vkDestroyDevice(impl_->device, nullptr);
  impl_->device = VK_NULL_HANDLE;
  if (!impl_->lifecycle.MarkDeviceDestroyed()) {
    return Failure("RT5 lifecycle rejected device teardown");
  }
  impl_->evidence.device_destroyed_before_instance =
      impl_->instance != VK_NULL_HANDLE;

  vkDestroyInstance(impl_->instance, nullptr);
  impl_->instance = VK_NULL_HANDLE;
  if (!impl_->lifecycle.MarkInstanceDestroyed()) {
    return Failure("RT5 lifecycle rejected instance teardown");
  }
  impl_->evidence.shutdown_completed = impl_->lifecycle.complete();
  return impl_->evidence.shutdown_completed
             ? Ready()
             : Failure("RT5 lifecycle did not reach complete teardown");
}

VulkanRt5BootstrapResult
OgreNextVulkanExternalDeviceBootstrap::AbortAfterFailure() noexcept {
  if (impl_->ogre_attached) {
    return Failure("cannot abort RoR-owned Vulkan objects while Ogre is attached");
  }
  return impl_->DestroyPartial();
}

const VulkanRt5BootstrapEvidence&
OgreNextVulkanExternalDeviceBootstrap::evidence() const noexcept {
  return impl_->evidence;
}

const char* VulkanRt5DeviceClassName(VulkanRt5DeviceClass value) noexcept {
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

const char* VulkanRt5CandidateDecisionName(
    VulkanRt5CandidateDecision value) noexcept {
  switch (value) {
    case VulkanRt5CandidateDecision::ACCEPT:
      return "accept";
    case VulkanRt5CandidateDecision::API_TOO_OLD:
      return "api_too_old";
    case VulkanRt5CandidateDecision::SOFTWARE_OR_UNATTESTED_DEVICE:
      return "software_or_unattested_device";
    case VulkanRt5CandidateDecision::GRAPHICS_QUEUE_UNAVAILABLE:
      return "graphics_queue_unavailable";
    case VulkanRt5CandidateDecision::TIMELINE_SEMAPHORE_UNAVAILABLE:
      return "timeline_semaphore_unavailable";
    case VulkanRt5CandidateDecision::ENABLED_FEATURE_STATE_AMBIGUOUS:
      return "enabled_feature_state_ambiguous";
    case VulkanRt5CandidateDecision::ENABLED_EXTENSION_STATE_AMBIGUOUS:
      return "enabled_extension_state_ambiguous";
    default:
      return "unknown";
  }
}

}  // namespace RoR::Render
