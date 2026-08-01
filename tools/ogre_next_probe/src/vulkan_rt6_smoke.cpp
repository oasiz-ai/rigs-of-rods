/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ror_ogre_next_vulkan_rt6_config.h"

#include "OgreNextVulkanRayTracingBootstrap.h"

#include "Compositor/OgreCompositorManager2.h"
#include "OgreAbiUtils.h"
#include "OgreRoot.h"
#include "OgreVulkanPlugin.h"
#include "OgreWindow.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

using RoR::Render::OgreNextVulkanRayTracingBootstrap;
using RoR::Render::VulkanRt6BootstrapEvidence;
using RoR::Render::VulkanRt6BootstrapResult;

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
  version << VK_API_VERSION_MAJOR(packed) << '.' << VK_API_VERSION_MINOR(packed)
          << '.' << VK_API_VERSION_PATCH(packed);
  return version.str();
}

const char* JsonBool(bool value) { return value ? "true" : "false"; }

Arguments ParseArguments(int argc, char** argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--report" && index + 1 < argc) {
      arguments.report = argv[++index];
    } else {
      throw std::runtime_error("usage: ror_ogre_next_vulkan_rt6_smoke "
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
      throw std::runtime_error("could not open temporary RT6 report");
    }
    output.write(contents.data(),
                 static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) {
      throw std::runtime_error("could not flush temporary RT6 report");
    }
  }
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary);
    throw std::runtime_error("could not publish RT6 report atomically: " +
                             error.message());
  }
}

std::string MakeReport(const char* status, const std::string& reason,
                       const VulkanRt6BootstrapEvidence& evidence) {
  const bool passed = std::string(status) == "pass";
  std::ostringstream report;
  report
      << "{\n"
      << "  \"schema\": \"" << ROR_OGRE_NEXT_RT6_SCHEMA << "\",\n"
      << "  \"status\": \"" << status << "\",\n"
      << "  \"reason\": \"" << JsonEscape(reason) << "\",\n"
      << "  \"scope\": {\n"
      << "    \"checkpoint\": \"rt6\",\n"
      << "    \"hardware_ray_dispatch_pass\": " << JsonBool(passed) << ",\n"
      << "    \"native_ray_tracing\": \""
      << (passed ? "hardware_dispatch_pass" : "not_executed") << "\",\n"
      << "    \"ray_traced_image_produced\": " << JsonBool(passed) << ",\n"
      << "    \"ogre_native_image_composite\": \"not_evaluated\"\n"
      << "  },\n"
      << "  \"provenance\": {\n"
      << "    \"ror_repository\": \"" << ROR_OGRE_NEXT_RT6_ROR_REPOSITORY
      << "\",\n"
      << "    \"ror_ref\": \"" << ROR_OGRE_NEXT_RT6_ROR_REF << "\",\n"
      << "    \"ror_commit\": \"" << ROR_OGRE_NEXT_RT6_ROR_COMMIT << "\",\n"
      << "    \"ror_relevant_source_manifest_sha256\": \""
      << ROR_OGRE_NEXT_RT6_ROR_SOURCE_MANIFEST_SHA256 << "\",\n"
      << "    \"ror_relevant_source_manifest_file_count\": "
      << ROR_OGRE_NEXT_RT6_ROR_SOURCE_MANIFEST_FILE_COUNT << ",\n"
      << "    \"ogre_next_repository\": \"" << ROR_OGRE_NEXT_RT6_OGRE_REPOSITORY
      << "\",\n"
      << "    \"ogre_next_branch\": \"" << ROR_OGRE_NEXT_RT6_OGRE_BRANCH
      << "\",\n"
      << "    \"ogre_next_commit\": \"" << ROR_OGRE_NEXT_RT6_OGRE_COMMIT
      << "\",\n"
      << "    \"ogre_next_archive_sha256\": \""
      << ROR_OGRE_NEXT_RT6_OGRE_ARCHIVE_SHA256 << "\",\n"
      << "    \"ogre_next_license_spdx\": \""
      << ROR_OGRE_NEXT_RT6_OGRE_LICENSE_SPDX << "\",\n"
      << "    \"ogre_next_license_sha256\": \""
      << ROR_OGRE_NEXT_RT6_OGRE_LICENSE_SHA256 << "\"\n"
      << "  },\n"
      << "  \"build\": {\n"
      << "    \"platform_policy\": \"" << ROR_OGRE_NEXT_RT6_PLATFORM_POLICY
      << "\",\n"
      << "    \"system\": \"" << ROR_OGRE_NEXT_RT6_SYSTEM_NAME << "\",\n"
      << "    \"processor\": \"" << ROR_OGRE_NEXT_RT6_SYSTEM_PROCESSOR
      << "\",\n"
      << "    \"compiler_id\": \"" << ROR_OGRE_NEXT_RT6_COMPILER_ID << "\",\n"
      << "    \"compiler_version\": \"" << ROR_OGRE_NEXT_RT6_COMPILER_VERSION
      << "\",\n"
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
      << "    \"driver_version_packed\": " << evidence.driver_version << ",\n"
      << "    \"vendor_id\": " << evidence.vendor_id << ",\n"
      << "    \"device_id\": " << evidence.device_id << ",\n"
      << "    \"device_name\": \"" << JsonEscape(evidence.device_name)
      << "\",\n"
      << "    \"device_uuid\": \"" << evidence.device_uuid << "\",\n"
      << "    \"device_class\": \""
      << RoR::Render::VulkanRt6DeviceClassName(evidence.device_class) << "\",\n"
      << "    \"known_software_adapter\": "
      << JsonBool(evidence.known_software_adapter) << ",\n"
      << "    \"device_identity_available\": "
      << JsonBool(evidence.device_identity_available) << ",\n"
      << "    \"candidate_decision\": \""
      << RoR::Render::VulkanRt6CandidateDecisionName(
             evidence.candidate_decision)
      << "\",\n"
      << "    \"graphics_queue_family\": " << evidence.graphics_queue_family
      << ",\n"
      << "    \"graphics_queue_index\": " << evidence.graphics_queue_index
      << "\n"
      << "  },\n"
      << "  \"required_extensions\": {\n"
      << "    \"deferred_host_operations\": "
      << JsonBool(evidence.deferred_host_operations_extension_supported)
      << ",\n"
      << "    \"buffer_device_address\": "
      << JsonBool(evidence.buffer_device_address_extension_supported) << ",\n"
      << "    \"acceleration_structure\": "
      << JsonBool(evidence.acceleration_structure_extension_supported) << ",\n"
      << "    \"ray_tracing_pipeline\": "
      << JsonBool(evidence.ray_tracing_pipeline_extension_supported) << ",\n"
      << "    \"enabled_count\": " << evidence.enabled_device_extension_count
      << ",\n"
      << "    \"enabled_set_exact\": "
      << JsonBool(evidence.enabled_device_extensions_exact) << "\n"
      << "  },\n"
      << "  \"features\": {\n"
      << "    \"graphics_queue\": "
      << JsonBool(evidence.graphics_queue_available) << ",\n"
      << "    \"compute_on_graphics_queue\": "
      << JsonBool(evidence.compute_on_graphics_queue_available) << ",\n"
      << "    \"timeline_semaphore_supported\": "
      << JsonBool(evidence.timeline_semaphore_supported) << ",\n"
      << "    \"timeline_semaphore_enabled\": "
      << JsonBool(evidence.timeline_semaphore_enabled) << ",\n"
      << "    \"buffer_device_address_supported\": "
      << JsonBool(evidence.buffer_device_address_feature_supported) << ",\n"
      << "    \"buffer_device_address_enabled\": "
      << JsonBool(evidence.buffer_device_address_feature_enabled) << ",\n"
      << "    \"acceleration_structure_supported\": "
      << JsonBool(evidence.acceleration_structure_feature_supported) << ",\n"
      << "    \"acceleration_structure_enabled\": "
      << JsonBool(evidence.acceleration_structure_feature_enabled) << ",\n"
      << "    \"ray_tracing_pipeline_supported\": "
      << JsonBool(evidence.ray_tracing_pipeline_feature_supported) << ",\n"
      << "    \"ray_tracing_pipeline_enabled\": "
      << JsonBool(evidence.ray_tracing_pipeline_feature_enabled) << ",\n"
      << "    \"output_rgba32_uint_storage_image\": "
      << JsonBool(evidence.output_storage_image_format_supported) << ",\n"
      << "    \"ray_tracing_properties_valid\": "
      << JsonBool(evidence.ray_tracing_properties_valid) << ",\n"
      << "    \"all_supported_core_features_enabled\": "
      << JsonBool(evidence.all_supported_core_features_enabled) << ",\n"
      << "    \"enabled_instance_extensions_exact\": "
      << JsonBool(evidence.enabled_instance_extensions_exact) << "\n"
      << "  },\n"
      << "  \"ray_properties\": {\n"
      << "    \"shader_group_handle_size\": "
      << evidence.shader_group_handle_size << ",\n"
      << "    \"shader_group_handle_alignment\": "
      << evidence.shader_group_handle_alignment << ",\n"
      << "    \"shader_group_base_alignment\": "
      << evidence.shader_group_base_alignment << ",\n"
      << "    \"max_ray_recursion_depth\": " << evidence.max_ray_recursion_depth
      << ",\n"
      << "    \"acceleration_structure_scratch_alignment\": "
      << evidence.acceleration_structure_scratch_alignment << "\n"
      << "  },\n"
      << "  \"ogre_external_adoption\": {\n"
      << "    \"plugin_option\": \"external_instance\",\n"
      << "    \"first_window_option\": \"external_device\",\n"
      << "    \"window_type\": \"null\",\n"
      << "    \"instance_injected_exactly\": "
      << JsonBool(evidence.instance_injected_exactly) << ",\n"
      << "    \"physical_device_injected_exactly\": "
      << JsonBool(evidence.physical_device_injected_exactly) << ",\n"
      << "    \"logical_device_injected_exactly\": "
      << JsonBool(evidence.logical_device_injected_exactly) << ",\n"
      << "    \"graphics_queue_injected_exactly\": "
      << JsonBool(evidence.graphics_queue_injected_exactly) << ",\n"
      << "    \"ogre_external_ownership_observed\": "
      << JsonBool(evidence.ogre_external_ownership_observed) << "\n"
      << "  },\n"
      << "  \"geometry_and_acceleration\": {\n"
      << "    \"mirror_vertex_count\": 3,\n"
      << "    \"geometry_buffer_created\": "
      << JsonBool(evidence.geometry_buffer_created) << ",\n"
      << "    \"geometry_buffer_device_address\": "
      << evidence.geometry_buffer_device_address << ",\n"
      << "    \"instance_buffer_device_address\": "
      << evidence.instance_buffer_device_address << ",\n"
      << "    \"scratch_buffer_device_address\": "
      << evidence.scratch_buffer_device_address << ",\n"
      << "    \"blas_device_address\": " << evidence.blas_device_address
      << ",\n"
      << "    \"tlas_device_address\": " << evidence.tlas_device_address
      << ",\n"
      << "    \"blas_built\": " << JsonBool(evidence.blas_built) << ",\n"
      << "    \"tlas_built\": " << JsonBool(evidence.tlas_built) << "\n"
      << "  },\n"
      << "  \"pipeline_and_dispatch\": {\n"
      << "    \"shader_contract_compiled\": "
      << JsonBool(evidence.shader_contract_compiled) << ",\n"
      << "    \"descriptor_set_bound\": "
      << JsonBool(evidence.descriptor_set_bound) << ",\n"
      << "    \"ray_pipeline_created\": "
      << JsonBool(evidence.ray_pipeline_created) << ",\n"
      << "    \"shader_binding_table_created\": "
      << JsonBool(evidence.shader_binding_table_created) << ",\n"
      << "    \"shader_binding_table_device_address\": "
      << evidence.shader_binding_table_device_address << ",\n"
      << "    \"dispatch_dimensions\": [1, 1, 1],\n"
      << "    \"ray_dispatch_completed\": "
      << JsonBool(evidence.ray_dispatch_completed) << ",\n"
      << "    \"output_format\": \"VK_FORMAT_R32G32B32A32_UINT\",\n"
      << "    \"output_image_copied_to_host\": "
      << JsonBool(evidence.output_image_copied_to_host) << ",\n"
      << "    \"expected_primary_hit_words\": [1381250561, 324508639, "
         "610839776, 1],\n"
      << "    \"readback_words\": [" << evidence.readback_words[0] << ", "
      << evidence.readback_words[1] << ", " << evidence.readback_words[2]
      << ", " << evidence.readback_words[3] << "],\n"
      << "    \"primary_hit_observed\": "
      << JsonBool(evidence.primary_hit_observed) << "\n"
      << "  },\n"
      << "  \"timeline\": {\n"
      << "    \"value_before_ray_dispatch\": "
      << evidence.timeline_value_before_ray_dispatch << ",\n"
      << "    \"value_at_ray_dispatch\": "
      << evidence.timeline_value_at_ray_dispatch << ",\n"
      << "    \"value_after_ogre_shutdown\": "
      << evidence.timeline_value_after_ogre << "\n"
      << "  },\n"
      << "  \"lifecycle\": {\n"
      << "    \"ogre_shutdown_before_owner_teardown\": "
      << JsonBool(evidence.ogre_shutdown_before_owner_teardown) << ",\n"
      << "    \"ray_resources_destroyed_before_device\": "
      << JsonBool(evidence.ray_resources_destroyed_before_device) << ",\n"
      << "    \"timeline_destroyed_before_device\": "
      << JsonBool(evidence.timeline_destroyed_before_device) << ",\n"
      << "    \"device_destroyed_before_instance\": "
      << JsonBool(evidence.device_destroyed_before_instance) << ",\n"
      << "    \"shutdown_completed\": " << JsonBool(evidence.shutdown_completed)
      << "\n"
      << "  }\n"
      << "}\n";
  return report.str();
}

void RequireReady(const VulkanRt6BootstrapResult& result) {
  if (!result.ready()) {
    throw std::runtime_error(result.message);
  }
}

void RunOgreAdoption(OgreNextVulkanRayTracingBootstrap& bootstrap) {
  const Ogre::AbiCookie abi_cookie = Ogre::generateAbiCookie();
  Ogre::VulkanPlugin renderer_plugin;
  bool attachment_marked = false;
  try {
    {
      Ogre::Root root(&abi_cookie, "", "", "", "RoR Ogre-Next Vulkan RT6");
      Ogre::NameValuePairList plugin_options;
      plugin_options["external_instance"] =
          std::to_string(bootstrap.external_instance_descriptor_address());
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
      window_parameters["external_device"] =
          std::to_string(bootstrap.external_device_descriptor_address());
      Ogre::Window* window =
          root.createRenderWindow("RoR Ogre-Next Vulkan RT6 null bootstrap",
                                  64U, 64U, false, &window_parameters);
      if (window == nullptr || root.getCompositorManager2() == nullptr) {
        throw std::runtime_error(
            "external RT6 Vulkan device did not initialize Ogre Compositor2");
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
                "RT6 external Vulkan handles require a 64-bit ABI");
  static_assert(OGRE_VERSION_MAJOR == 3 && OGRE_VERSION_MINOR == 0,
                "RT6 must be reviewed before moving Ogre-Next versions");

  OgreNextVulkanRayTracingBootstrap bootstrap;
  try {
    RequireReady(bootstrap.ValidateShaderContract());
    const VulkanRt6BootstrapResult initialization = bootstrap.Initialize();
    if (initialization.unsupported()) {
      WriteAtomically(arguments.report,
                      MakeReport("unsupported", initialization.message,
                                 bootstrap.evidence()));
      std::cout << "RT6 unsupported: " << initialization.message << '\n';
      return kUnsupportedExitCode;
    }
    RequireReady(initialization);
    RequireReady(bootstrap.ProveTimelineQueue(1U));
    RequireReady(bootstrap.ProveRayTracingDispatch(2U));
    RunOgreAdoption(bootstrap);
    RequireReady(bootstrap.ProveTimelineQueue(3U));
    RequireReady(bootstrap.Shutdown());
    WriteAtomically(arguments.report,
                    MakeReport("pass", "", bootstrap.evidence()));
    std::cout << "RT6 Vulkan KHR primary-hit dispatch proof passed\n";
    return 0;
  } catch (const std::exception& error) {
    static_cast<void>(bootstrap.AbortAfterFailure());
    try {
      WriteAtomically(arguments.report,
                      MakeReport("error", error.what(), bootstrap.evidence()));
    } catch (const std::exception& report_error) {
      std::cerr << "RT6 report failure: " << report_error.what() << '\n';
    }
    std::cerr << "RT6 Vulkan KHR ray-dispatch proof failed: " << error.what()
              << '\n';
    return 1;
  }
}
