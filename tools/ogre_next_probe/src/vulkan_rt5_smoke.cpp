/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ror_ogre_next_vulkan_rt5_config.h"

#include "OgreNextVulkanExternalDeviceBootstrap.h"

#include "Compositor/OgreCompositorManager2.h"
#include "OgreAbiUtils.h"
#include "OgreException.h"
#include "OgreRoot.h"
#include "OgreVulkanPlugin.h"
#include "OgreWindow.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

using RoR::Render::OgreNextVulkanExternalDeviceBootstrap;
using RoR::Render::VulkanRt5BootstrapCode;
using RoR::Render::VulkanRt5BootstrapEvidence;
using RoR::Render::VulkanRt5BootstrapResult;

constexpr int kUnsupportedExitCode = 77;

struct Arguments {
  std::filesystem::path report;
};

std::string JsonEscape(const std::string& value) {
  std::ostringstream escaped;
  for (const unsigned char character : value) {
    switch (character) {
      case '\\':
        escaped << "\\\\";
        break;
      case '"':
        escaped << "\\\"";
        break;
      case '\b':
        escaped << "\\b";
        break;
      case '\f':
        escaped << "\\f";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (character < 0x20U) {
          const char hex[] = "0123456789abcdef";
          escaped << "\\u00" << hex[(character >> 4U) & 0x0fU]
                  << hex[character & 0x0fU];
        } else {
          escaped << static_cast<char>(character);
        }
        break;
    }
  }
  return escaped.str();
}

std::string ApiVersion(std::uint32_t packed) {
  std::ostringstream version;
  version << VK_API_VERSION_MAJOR(packed) << '.'
          << VK_API_VERSION_MINOR(packed) << '.'
          << VK_API_VERSION_PATCH(packed);
  return version.str();
}

Arguments ParseArguments(int argc, char** argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--report" && index + 1 < argc) {
      arguments.report = argv[++index];
    } else {
      throw std::runtime_error("usage: ror_ogre_next_vulkan_rt5_smoke "
                               "--report <report.json>");
    }
  }
  if (arguments.report.empty()) {
    throw std::runtime_error("--report is required");
  }
  return arguments;
}

void WriteAtomically(const std::filesystem::path& destination,
                     const std::string& contents) {
  if (!destination.has_parent_path()) {
    throw std::runtime_error("report path must have an explicit parent");
  }
  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  if (error) {
    throw std::runtime_error("could not create report directory: " +
                             error.message());
  }
  std::filesystem::path temporary = destination;
  temporary += ".tmp";
  std::filesystem::remove(temporary, error);
  error.clear();
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("could not open temporary report");
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) {
      throw std::runtime_error("could not flush temporary report");
    }
  }
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary);
    throw std::runtime_error("could not publish report atomically: " +
                             error.message());
  }
}

std::string MakeReport(const char* status, const std::string& reason,
                       const VulkanRt5BootstrapEvidence& evidence) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"" << ROR_OGRE_NEXT_RT5_SCHEMA << "\",\n"
         << "  \"status\": \"" << status << "\",\n"
         << "  \"reason\": \"" << JsonEscape(reason) << "\",\n"
         << "  \"scope\": {\n"
         << "    \"external_instance_device_foundation\": true,\n"
         << "    \"hardware_bootstrap_pass\": "
         << (std::string(status) == "pass" ? "true" : "false") << ",\n"
         << "    \"native_ray_tracing\": \"not_evaluated\",\n"
         << "    \"ray_traced_image_produced\": false,\n"
         << "    \"acceleration_structure_built\": false\n"
         << "  },\n"
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \""
         << ROR_OGRE_NEXT_RT5_ROR_REPOSITORY << "\",\n"
         << "    \"ror_ref\": \"" << ROR_OGRE_NEXT_RT5_ROR_REF << "\",\n"
         << "    \"ror_commit\": \"" << ROR_OGRE_NEXT_RT5_ROR_COMMIT
         << "\",\n"
         << "    \"ror_relevant_source_manifest_sha256\": \""
         << ROR_OGRE_NEXT_RT5_ROR_SOURCE_MANIFEST_SHA256 << "\",\n"
         << "    \"ror_relevant_source_manifest_file_count\": "
         << ROR_OGRE_NEXT_RT5_ROR_SOURCE_MANIFEST_FILE_COUNT << ",\n"
         << "    \"ogre_next_repository\": \""
         << ROR_OGRE_NEXT_RT5_OGRE_REPOSITORY << "\",\n"
         << "    \"ogre_next_branch\": \""
         << ROR_OGRE_NEXT_RT5_OGRE_BRANCH << "\",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_RT5_OGRE_COMMIT << "\",\n"
         << "    \"ogre_next_archive_sha256\": \""
         << ROR_OGRE_NEXT_RT5_OGRE_ARCHIVE_SHA256 << "\",\n"
         << "    \"ogre_next_license_spdx\": \""
         << ROR_OGRE_NEXT_RT5_OGRE_LICENSE_SPDX << "\",\n"
         << "    \"ogre_next_license_sha256\": \""
         << ROR_OGRE_NEXT_RT5_OGRE_LICENSE_SHA256 << "\"\n"
         << "  },\n"
         << "  \"build\": {\n"
         << "    \"platform_policy\": \""
         << ROR_OGRE_NEXT_RT5_PLATFORM_POLICY << "\",\n"
         << "    \"system\": \"" << ROR_OGRE_NEXT_RT5_SYSTEM_NAME << "\",\n"
         << "    \"processor\": \"" << ROR_OGRE_NEXT_RT5_SYSTEM_PROCESSOR
         << "\",\n"
         << "    \"compiler_id\": \"" << ROR_OGRE_NEXT_RT5_COMPILER_ID
         << "\",\n"
         << "    \"compiler_version\": \""
         << ROR_OGRE_NEXT_RT5_COMPILER_VERSION << "\",\n"
         << "    \"ogre_version\": \"" << OGRE_VERSION_MAJOR << '.'
         << OGRE_VERSION_MINOR << '.' << OGRE_VERSION_PATCH << "\",\n"
         << "    \"pointer_bits\": " << sizeof(void*) * 8U << "\n"
         << "  },\n"
         << "  \"vulkan\": {\n"
         << "    \"loader_api_version\": \""
         << ApiVersion(evidence.loader_api_version) << "\",\n"
         << "    \"requested_instance_api_version\": \""
         << ApiVersion(evidence.requested_instance_api_version) << "\",\n"
         << "    \"physical_device_api_version\": \""
         << ApiVersion(evidence.physical_device_api_version) << "\",\n"
         << "    \"driver_version_packed\": " << evidence.driver_version
         << ",\n"
         << "    \"vendor_id\": " << evidence.vendor_id << ",\n"
         << "    \"device_id\": " << evidence.device_id << ",\n"
         << "    \"device_name\": \"" << JsonEscape(evidence.device_name)
         << "\",\n"
         << "    \"device_uuid\": \"" << evidence.device_uuid << "\",\n"
         << "    \"device_class\": \""
         << RoR::Render::VulkanRt5DeviceClassName(evidence.device_class)
         << "\",\n"
         << "    \"known_software_adapter\": "
         << (evidence.known_software_adapter ? "true" : "false") << ",\n"
         << "    \"candidate_decision\": \""
         << RoR::Render::VulkanRt5CandidateDecisionName(
                evidence.candidate_decision)
         << "\",\n"
         << "    \"graphics_queue_family\": "
         << evidence.graphics_queue_family << ",\n"
         << "    \"graphics_queue_index\": " << evidence.graphics_queue_index
         << "\n"
         << "  },\n"
         << "  \"enabled_state_contract\": {\n"
         << "    \"graphics_queue_available\": "
         << (evidence.graphics_queue_available ? "true" : "false") << ",\n"
         << "    \"timeline_semaphore_supported\": "
         << (evidence.timeline_semaphore_supported ? "true" : "false")
         << ",\n"
         << "    \"timeline_semaphore_enabled\": "
         << (std::string(status) == "pass" ? "true" : "false") << ",\n"
         << "    \"all_supported_core_features_enabled\": "
         << (evidence.all_supported_core_features_enabled ? "true" : "false")
         << ",\n"
         << "    \"claimed_instance_extension_count\": 0,\n"
         << "    \"claimed_device_extension_count\": 0,\n"
         << "    \"enabled_instance_extensions_exact\": "
         << (evidence.enabled_instance_extensions_exact ? "true" : "false")
         << ",\n"
         << "    \"enabled_device_extensions_exact\": "
         << (evidence.enabled_device_extensions_exact ? "true" : "false")
         << "\n"
         << "  },\n"
         << "  \"ogre_external_adoption\": {\n"
         << "    \"plugin_option\": \"external_instance\",\n"
         << "    \"first_window_option\": \"external_device\",\n"
         << "    \"window_type\": \"null\",\n"
         << "    \"instance_injected_exactly\": "
         << (evidence.instance_injected_exactly ? "true" : "false") << ",\n"
         << "    \"physical_device_injected_exactly\": "
         << (evidence.physical_device_injected_exactly ? "true" : "false")
         << ",\n"
         << "    \"logical_device_injected_exactly\": "
         << (evidence.logical_device_injected_exactly ? "true" : "false")
         << ",\n"
         << "    \"graphics_queue_injected_exactly\": "
         << (evidence.graphics_queue_injected_exactly ? "true" : "false")
         << ",\n"
         << "    \"ogre_external_ownership_observed\": "
         << (evidence.ogre_external_ownership_observed ? "true" : "false")
         << "\n"
         << "  },\n"
         << "  \"timeline\": {\n"
         << "    \"queue_submit_and_host_wait\": "
         << (evidence.timeline_value_before_ogre > 0U ? "true" : "false")
         << ",\n"
         << "    \"value_before_ogre\": "
         << evidence.timeline_value_before_ogre << ",\n"
         << "    \"value_after_ogre_shutdown\": "
         << evidence.timeline_value_after_ogre << "\n"
         << "  },\n"
         << "  \"lifecycle\": {\n"
         << "    \"ogre_shutdown_before_owner_teardown\": "
         << (evidence.ogre_shutdown_before_owner_teardown ? "true" : "false")
         << ",\n"
         << "    \"timeline_destroyed_before_device\": "
         << (evidence.timeline_destroyed_before_device ? "true" : "false")
         << ",\n"
         << "    \"device_destroyed_before_instance\": "
         << (evidence.device_destroyed_before_instance ? "true" : "false")
         << ",\n"
         << "    \"shutdown_completed\": "
         << (evidence.shutdown_completed ? "true" : "false") << "\n"
         << "  }\n"
         << "}\n";
  return report.str();
}

void RequireReady(const VulkanRt5BootstrapResult& result) {
  if (!result.ready()) {
    throw std::runtime_error(result.message);
  }
}

void RunOgreAdoption(OgreNextVulkanExternalDeviceBootstrap& bootstrap) {
  const Ogre::AbiCookie abi_cookie = Ogre::generateAbiCookie();
  Ogre::VulkanPlugin renderer_plugin;
  bool attachment_marked = false;
  try {
    {
      Ogre::Root root(&abi_cookie, "", "", "",
                      "RoR Ogre-Next Vulkan RT5");
      Ogre::NameValuePairList plugin_options;
      plugin_options["external_instance"] = std::to_string(
          bootstrap.external_instance_descriptor_address());
      root.installPlugin(&renderer_plugin, &plugin_options);
      Ogre::RenderSystem* renderer =
          root.getRenderSystemByName("Vulkan Rendering Subsystem");
      if (renderer == nullptr) {
        throw std::runtime_error("pinned Vulkan renderer did not register");
      }
      root.setRenderSystem(renderer);
      root.initialise(false);

      RequireReady(bootstrap.MarkOgreAttached());
      attachment_marked = true;
      Ogre::NameValuePairList window_parameters;
      window_parameters["windowType"] = "null";
      window_parameters["hidden"] = "true";
      window_parameters["external_device"] = std::to_string(
          bootstrap.external_device_descriptor_address());
      Ogre::Window* window = root.createRenderWindow(
          "RoR Ogre-Next Vulkan RT5 null bootstrap", 64U, 64U, false,
          &window_parameters);
      if (window == nullptr || root.getCompositorManager2() == nullptr) {
        throw std::runtime_error(
            "external Vulkan device did not initialize Ogre Compositor2");
      }
      RequireReady(bootstrap.VerifyOgreAdoption(renderer));
    }
    RequireReady(bootstrap.MarkOgreDetached());
    attachment_marked = false;
  } catch (...) {
    if (attachment_marked) {
      static_cast<void>(bootstrap.MarkOgreDetached());
    }
    throw;
  }
}

}  // namespace

int main(int argc, char** argv) {
  Arguments arguments;
  try {
    arguments = ParseArguments(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }

  static_assert(sizeof(void*) == 8U,
                "RT5 external Vulkan handles require a 64-bit ABI");
  static_assert(OGRE_VERSION_MAJOR == 3 && OGRE_VERSION_MINOR == 0,
                "RT5 must be reviewed before moving Ogre-Next versions");

  OgreNextVulkanExternalDeviceBootstrap bootstrap;
  try {
    const VulkanRt5BootstrapResult initialization = bootstrap.Initialize();
    if (initialization.unsupported()) {
      WriteAtomically(arguments.report,
                      MakeReport("unsupported", initialization.message,
                                 bootstrap.evidence()));
      std::cout << "RT5 unsupported: " << initialization.message << '\n';
      return kUnsupportedExitCode;
    }
    RequireReady(initialization);
    RequireReady(bootstrap.ProveTimelineQueue(1U));
    RunOgreAdoption(bootstrap);
    RequireReady(bootstrap.ProveTimelineQueue(2U));
    RequireReady(bootstrap.Shutdown());
    WriteAtomically(arguments.report,
                    MakeReport("pass", "", bootstrap.evidence()));
    std::cout << "RT5 external Vulkan instance/device proof passed\n";
    return 0;
  } catch (const std::exception& error) {
    static_cast<void>(bootstrap.AbortAfterFailure());
    try {
      WriteAtomically(arguments.report,
                      MakeReport("error", error.what(), bootstrap.evidence()));
    } catch (const std::exception& report_error) {
      std::cerr << "RT5 report failure: " << report_error.what() << '\n';
    }
    std::cerr << "RT5 external Vulkan device proof failed: " << error.what()
              << '\n';
    return 1;
  }
}
