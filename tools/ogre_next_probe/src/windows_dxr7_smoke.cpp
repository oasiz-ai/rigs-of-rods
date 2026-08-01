/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ror_ogre_next_windows_dxr7_config.h"

#include "OgreNextD3D12DxrBootstrap.h"

#include "OgreAbiUtils.h"
#include "OgreD3D11Plugin.h"
#include "OgreRoot.h"

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

using RoR::Render::Dxr7BootstrapEvidence;
using RoR::Render::Dxr7BootstrapResult;
using RoR::Render::OgreNextD3D12DxrBootstrap;

constexpr int kUnsupportedExitCode = 77;
constexpr const char* kScopeLimitation =
    "one hardware DXR primary-ray closest-hit readback plus exact D3D11On12 "
    "Ogre device adoption; no hybrid image, GI, reflection, denoising, "
    "multi-bounce, or material parity claim";

struct Arguments {
  std::filesystem::path report;
  std::filesystem::path shader;
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
          constexpr char kHex[] = "0123456789abcdef";
          escaped << "\\u00" << kHex[(character >> 4U) & 0x0fU]
                  << kHex[character & 0x0fU];
        } else {
          escaped << static_cast<char>(character);
        }
        break;
    }
  }
  return escaped.str();
}

Arguments ParseArguments(int argc, char** argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--report" && index + 1 < argc) {
      arguments.report = argv[++index];
    } else if (argument == "--shader" && index + 1 < argc) {
      arguments.shader = argv[++index];
    } else {
      throw std::runtime_error(
          "usage: ror_ogre_next_windows_dxr7_smoke --report <report.json> "
          "--shader <library.dxil>");
    }
  }
  if (arguments.report.empty() || arguments.shader.empty()) {
    throw std::runtime_error("--report and --shader are required");
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

const char* Boolean(bool value) { return value ? "true" : "false"; }

std::string MakeReport(const char* status, const std::string& reason,
                       const Dxr7BootstrapEvidence& evidence) {
  const bool passed = std::string(status) == "pass";
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"" << ROR_OGRE_NEXT_DXR7_SCHEMA << "\",\n"
         << "  \"status\": \"" << status << "\",\n"
         << "  \"reason\": \"" << JsonEscape(reason) << "\",\n"
         << "  \"scope\": {\n"
         << "    \"external_d3d11on12_foundation\": true,\n"
         << "    \"hardware_dxr_pass\": " << Boolean(passed) << ",\n"
         << "    \"native_ray_tracing\": \""
         << (passed ? "dispatch_rays" : "unsupported") << "\",\n"
         << "    \"acceleration_structure_built\": "
         << Boolean(evidence.blas_built && evidence.tlas_built) << ",\n"
         << "    \"ray_traced_probe_readback\": "
         << Boolean(evidence.closest_hit_readback_exact) << ",\n"
         << "    \"ray_traced_image_produced\": false,\n"
         << "    \"hybrid_ogre_image_composite\": false,\n"
         << "    \"limitation\": \"" << kScopeLimitation << "\"\n"
         << "  },\n"
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \""
         << ROR_OGRE_NEXT_DXR7_ROR_REPOSITORY << "\",\n"
         << "    \"ror_ref\": \"" << ROR_OGRE_NEXT_DXR7_ROR_REF
         << "\",\n"
         << "    \"ror_commit\": \"" << ROR_OGRE_NEXT_DXR7_ROR_COMMIT
         << "\",\n"
         << "    \"ror_relevant_source_manifest_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_ROR_SOURCE_MANIFEST_SHA256 << "\",\n"
         << "    \"ror_relevant_source_manifest_file_count\": "
         << ROR_OGRE_NEXT_DXR7_ROR_SOURCE_MANIFEST_FILE_COUNT << ",\n"
         << "    \"ogre_next_repository\": \""
         << ROR_OGRE_NEXT_DXR7_OGRE_REPOSITORY << "\",\n"
         << "    \"ogre_next_branch\": \""
         << ROR_OGRE_NEXT_DXR7_OGRE_BRANCH << "\",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_DXR7_OGRE_COMMIT << "\",\n"
         << "    \"ogre_next_archive_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_OGRE_ARCHIVE_SHA256 << "\",\n"
         << "    \"ogre_next_license_spdx\": \""
         << ROR_OGRE_NEXT_DXR7_OGRE_LICENSE_SPDX << "\",\n"
         << "    \"ogre_next_license_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_OGRE_LICENSE_SHA256 << "\",\n"
         << "    \"dxr7_toolchain_lock_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_TOOLCHAIN_LOCK_SHA256 << "\",\n"
         << "    \"ogre_adaptation_patch_path\": \""
         << ROR_OGRE_NEXT_DXR7_ADAPTATION_PATCH_PATH << "\",\n"
         << "    \"ogre_adaptation_patch_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_ADAPTATION_PATCH_SHA256 << "\",\n"
         << "    \"hlsl_source_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_HLSL_SOURCE_SHA256 << "\",\n"
         << "    \"dxc_executable_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_DXC_SHA256 << "\"\n"
         << "  },\n"
         << "  \"build\": {\n"
         << "    \"platform_policy\": \"windows-x64-d3d11on12-dxr\",\n"
         << "    \"system\": \"" << ROR_OGRE_NEXT_DXR7_SYSTEM_NAME
         << "\",\n"
         << "    \"processor\": \""
         << ROR_OGRE_NEXT_DXR7_SYSTEM_PROCESSOR << "\",\n"
         << "    \"compiler_id\": \""
         << ROR_OGRE_NEXT_DXR7_COMPILER_ID << "\",\n"
         << "    \"compiler_version\": \""
         << ROR_OGRE_NEXT_DXR7_COMPILER_VERSION << "\",\n"
         << "    \"ogre_version\": \"" << OGRE_VERSION_MAJOR << '.'
         << OGRE_VERSION_MINOR << '.' << OGRE_VERSION_PATCH << "\",\n"
         << "    \"pointer_bits\": " << sizeof(void*) * 8U << "\n"
         << "  },\n"
         << "  \"adapter\": {\n"
         << "    \"name\": \"" << JsonEscape(evidence.adapter_name)
         << "\",\n"
         << "    \"luid\": \"" << evidence.adapter_luid << "\",\n"
         << "    \"vendor_id\": " << evidence.vendor_id << ",\n"
         << "    \"device_id\": " << evidence.device_id << ",\n"
         << "    \"software_adapter\": "
         << Boolean(evidence.software_adapter) << ",\n"
         << "    \"d3d12_feature_level\": "
         << evidence.d3d12_feature_level << ",\n"
         << "    \"d3d11_feature_level\": "
         << evidence.d3d11_feature_level << ",\n"
         << "    \"raytracing_tier\": "
         << evidence.candidate.raytracing_tier << ",\n"
         << "    \"candidate_decision\": \""
         << RoR::Render::Dxr7CandidateDecisionName(
                evidence.candidate_decision)
         << "\"\n"
         << "  },\n"
         << "  \"ownership\": {\n"
         << "    \"app_owned_d3d12_device\": "
         << Boolean(evidence.app_owned_d3d12_device) << ",\n"
         << "    \"app_owned_direct_queue\": "
         << Boolean(evidence.app_owned_direct_queue) << ",\n"
         << "    \"app_owned_fence\": "
         << Boolean(evidence.app_owned_fence) << ",\n"
         << "    \"d3d11on12_device_created\": "
         << Boolean(evidence.d3d11on12_device_created) << ",\n"
         << "    \"d3d11on12_created_with_exact_direct_queue\": "
         << Boolean(evidence.d3d11on12_created_with_exact_direct_queue)
         << ",\n"
         << "    \"d3d11on12_underlying_d3d12_device_exact\": "
         << Boolean(evidence.d3d11on12_underlying_d3d12_device_exact)
         << ",\n"
         << "    \"d3d11on12_adapter_luid_exact\": "
         << Boolean(evidence.d3d11on12_adapter_luid_exact) << ",\n"
         << "    \"ogre_plugin_option\": \"external_device\",\n"
         << "    \"ogre_external_device_option_used\": "
         << Boolean(evidence.ogre_external_device_option_used) << ",\n"
         << "    \"ogre_d3d11_device_exact\": "
         << Boolean(evidence.ogre_d3d11_device_exact) << ",\n"
         << "    \"ogre_external_device_active\": "
         << Boolean(evidence.ogre_external_device_active) << "\n"
         << "  },\n"
         << "  \"ray_tracing\": {\n"
         << "    \"blas_built\": " << Boolean(evidence.blas_built)
         << ",\n"
         << "    \"tlas_built\": " << Boolean(evidence.tlas_built)
         << ",\n"
         << "    \"state_object_created\": "
         << Boolean(evidence.state_object_created) << ",\n"
         << "    \"shader_identifiers_resolved\": "
         << Boolean(evidence.shader_identifiers_resolved) << ",\n"
         << "    \"dispatch_rays_called\": "
         << Boolean(evidence.dispatch_rays_called) << ",\n"
         << "    \"dispatch_width\": " << evidence.dispatch_width << ",\n"
         << "    \"dispatch_height\": " << evidence.dispatch_height
         << ",\n"
         << "    \"dispatch_depth\": " << evidence.dispatch_depth << ",\n"
         << "    \"readback_value\": " << evidence.readback_value << ",\n"
         << "    \"closest_hit_readback_exact\": "
         << Boolean(evidence.closest_hit_readback_exact) << "\n"
         << "  },\n"
         << "  \"synchronization\": {\n"
         << "    \"fence_before_dispatch\": "
         << evidence.fence_before_dispatch << ",\n"
         << "    \"fence_after_dispatch\": "
         << evidence.fence_after_dispatch << ",\n"
         << "    \"fence_after_ogre\": " << evidence.fence_after_ogre
         << "\n"
         << "  },\n"
         << "  \"lifecycle\": {\n"
         << "    \"ogre_shutdown_before_d3d11_release\": "
         << Boolean(evidence.ogre_shutdown_before_d3d11_release) << ",\n"
         << "    \"d3d11_context_flushed_before_release\": "
         << Boolean(evidence.d3d11_context_flushed_before_release) << ",\n"
         << "    \"d3d11_released_before_d3d12_queue\": "
         << Boolean(evidence.d3d11_released_before_d3d12_queue) << ",\n"
         << "    \"d3d12_queue_released_before_device\": "
         << Boolean(evidence.d3d12_queue_released_before_device) << ",\n"
         << "    \"shutdown_completed\": "
         << Boolean(evidence.shutdown_completed) << "\n"
         << "  }\n"
         << "}\n";
  return report.str();
}

void RequireReady(const Dxr7BootstrapResult& result) {
  if (!result.ready()) {
    throw std::runtime_error(result.message);
  }
}

void RunOgreAdoption(OgreNextD3D12DxrBootstrap& bootstrap) {
  const Ogre::AbiCookie abi_cookie = Ogre::generateAbiCookie();
  Ogre::D3D11Plugin renderer_plugin;
  bool attachment_marked = false;
  try {
    {
      Ogre::Root root(&abi_cookie, "", "", "",
                      "RoR Ogre-Next Windows DXR7");
      Ogre::NameValuePairList plugin_options;
      plugin_options["external_device"] = std::to_string(
          bootstrap.external_d3d11_device_address());
      root.installPlugin(&renderer_plugin, &plugin_options);
      Ogre::RenderSystem* renderer =
          root.getRenderSystemByName("Direct3D11 Rendering Subsystem");
      if (renderer == nullptr) {
        throw std::runtime_error("pinned D3D11 renderer did not register");
      }
      root.setRenderSystem(renderer);
      root.initialise(false);
      RequireReady(bootstrap.MarkOgreAttached());
      attachment_marked = true;
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
                "DXR7 external device handles require a 64-bit ABI");
  static_assert(OGRE_VERSION_MAJOR == 3 && OGRE_VERSION_MINOR == 0,
                "DXR7 must be reviewed before moving Ogre-Next versions");

  OgreNextD3D12DxrBootstrap bootstrap;
  try {
    const Dxr7BootstrapResult initialization = bootstrap.Initialize();
    if (initialization.unsupported()) {
      WriteAtomically(arguments.report,
                      MakeReport("unsupported", initialization.message,
                                 bootstrap.evidence()));
      std::cout << "DXR7 unsupported: " << initialization.message << '\n';
      return kUnsupportedExitCode;
    }
    RequireReady(initialization);
    RequireReady(bootstrap.ProveFenceBeforeDispatch());
    RequireReady(bootstrap.DispatchProbe(arguments.shader));
    RunOgreAdoption(bootstrap);
    RequireReady(bootstrap.ProveFenceAfterOgre());
    RequireReady(bootstrap.Shutdown());
    if (!RoR::Render::ValidateDxr7PassContract(
            bootstrap.pass_contract())) {
      throw std::runtime_error("complete DXR7 pass contract was not satisfied");
    }
    WriteAtomically(arguments.report,
                    MakeReport("pass", "", bootstrap.evidence()));
    std::cout << "DXR7 D3D12/D3D11On12/Ogre proof passed\n";
    return 0;
  } catch (const std::exception& error) {
    static_cast<void>(bootstrap.AbortAfterFailure());
    try {
      WriteAtomically(arguments.report,
                      MakeReport("error", error.what(), bootstrap.evidence()));
    } catch (const std::exception& report_error) {
      std::cerr << "DXR7 report failure: " << report_error.what() << '\n';
    }
    std::cerr << "DXR7 proof failed: " << error.what() << '\n';
    return 1;
  }
}
