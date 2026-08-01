/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "ror_ogre_next_windows_dxr7_config.h"

#include "OgreNextD3D12DxrBootstrap.h"

#include "OgreAbiUtils.h"
#include "OgreArchiveManager.h"
#include "OgreCamera.h"
#include "OgreColourValue.h"
#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "OgreD3D11Plugin.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreImage2.h"
#include "OgreLight.h"
#include "OgreManualObject2.h"
#include "OgreRenderSystem.h"
#include "OgreRenderSystemCapabilities.h"
#include "OgreRoot.h"
#include "OgreSceneManager.h"
#include "OgreSceneNode.h"
#include "OgreTextureGpu.h"
#include "OgreTextureGpuManager.h"
#include "OgreWindow.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using RoR::Render::Dxr7BootstrapEvidence;
using RoR::Render::Dxr7BootstrapResult;
using RoR::Render::Dxr7OgreTeardownStep;
using RoR::Render::Dxr7OgreTeardownTracker;
using RoR::Render::OgreNextD3D12DxrBootstrap;

constexpr int kUnsupportedExitCode = 77;
constexpr Ogre::uint32 kFrameWidth = 192U;
constexpr Ogre::uint32 kFrameHeight = 128U;
constexpr std::size_t kWarmupFrames = 4U;
constexpr const char* kScopeLimitation =
    "one hardware DXR primary-ray closest-hit readback plus exact D3D11On12 "
    "Ogre device adoption and a separate UI-free Ogre PBS raster readback; "
    "no hybrid ray/raster composite, GI, reflection, denoising, multi-bounce, "
    "or production material-parity claim";

struct Arguments {
  std::filesystem::path report;
  std::filesystem::path shader;
  std::filesystem::path image;
  std::filesystem::path media_root;
  std::string execution_nonce;
};

struct FrameMetrics {
  std::size_t distinct_pixels = 0U;
  std::size_t non_background_pixels = 0U;
  std::uint64_t fnv1a64 = UINT64_C(14695981039346656037);
  float minimum_luminance = std::numeric_limits<float>::infinity();
  float maximum_luminance = -std::numeric_limits<float>::infinity();
  std::vector<unsigned char> rgb;
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
    } else if (argument == "--image" && index + 1 < argc) {
      arguments.image = argv[++index];
    } else if (argument == "--media-root" && index + 1 < argc) {
      arguments.media_root = argv[++index];
    } else if (argument == "--execution-nonce" && index + 1 < argc) {
      arguments.execution_nonce = argv[++index];
    } else {
      throw std::runtime_error(
          "usage: ror_ogre_next_windows_dxr7_smoke --report <report.json> "
          "--shader <library.dxil> --image <frame.ppm> "
          "--media-root <Samples/Media> --execution-nonce <64 hex>");
    }
  }
  if (arguments.report.empty() || arguments.shader.empty() ||
      arguments.image.empty() || arguments.media_root.empty() ||
      arguments.execution_nonce.size() != 64U ||
      arguments.execution_nonce.find_first_not_of("0123456789abcdef") !=
          std::string::npos) {
    throw std::runtime_error(
        "--report, --shader, --image, --media-root, and a lowercase 64-hex "
        "--execution-nonce are required");
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
  if (contents.size() > static_cast<std::size_t>(MAXDWORD)) {
    throw std::runtime_error("atomic DXR7 artifact is too large");
  }
  std::filesystem::path temporary = destination;
  temporary += ".tmp";
  std::filesystem::remove(temporary, error);
  error.clear();
  HANDLE file = CreateFileW(
      temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("could not create temporary DXR7 artifact");
  }
  DWORD written = 0U;
  const BOOL wrote = WriteFile(
      file, contents.data(), static_cast<DWORD>(contents.size()), &written,
      nullptr);
  const BOOL flushed = wrote ? FlushFileBuffers(file) : FALSE;
  const BOOL closed = CloseHandle(file);
  if (!wrote || static_cast<std::size_t>(written) != contents.size() ||
      !flushed || !closed) {
    std::filesystem::remove(temporary, error);
    throw std::runtime_error("could not durably write temporary DXR7 artifact");
  }
  if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD move_error = GetLastError();
    std::filesystem::remove(temporary, error);
    std::ostringstream detail;
    detail << "could not atomically replace DXR7 artifact: Win32 error "
           << move_error;
    throw std::runtime_error(detail.str());
  }
}

const char* Boolean(bool value) { return value ? "true" : "false"; }

unsigned char Quantize(float value) {
  const float clamped = std::max(0.0F, std::min(1.0F, value));
  return static_cast<unsigned char>(std::lround(clamped * 255.0F));
}

void HashByte(std::uint64_t& hash, unsigned char value) {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

Ogre::HlmsPbs* RegisterPbs(Ogre::Root& root,
                           const std::filesystem::path& media_root) {
  Ogre::String data_path;
  Ogre::StringVector library_paths;
  Ogre::HlmsPbs::getDefaultPaths(data_path, library_paths);
  const Ogre::String root_path = media_root.generic_string() + "/";
  Ogre::ArchiveManager& archive_manager = Ogre::ArchiveManager::getSingleton();
  Ogre::Archive* data_archive =
      archive_manager.load(root_path + data_path, "FileSystem", true);
  Ogre::ArchiveVec libraries;
  for (const Ogre::String& library_path : library_paths) {
    libraries.push_back(
        archive_manager.load(root_path + library_path, "FileSystem", true));
  }
  Ogre::HlmsPbs* pbs = OGRE_NEW Ogre::HlmsPbs(data_archive, &libraries);
  root.getHlmsManager()->registerHlms(pbs);
  return pbs;
}

Ogre::HlmsPbsDatablock* CreateMaterial(Ogre::HlmsPbs& pbs) {
  auto* datablock = static_cast<Ogre::HlmsPbsDatablock*>(
      pbs.createDatablock("RoRDxr7Pbs", "RoRDxr7Pbs",
                          Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(),
                          Ogre::HlmsParamVec()));
  datablock->setDiffuse(Ogre::Vector3(0.08F, 0.48F, 0.92F));
  datablock->setRoughness(0.18F);
  datablock->setFresnel(Ogre::Vector3(0.06F), false);
  return datablock;
}

void CreateTriangle(Ogre::SceneManager& scene_manager,
                    Ogre::HlmsPbsDatablock& datablock) {
  Ogre::ManualObject* triangle = scene_manager.createManualObject();
  triangle->begin(*datablock.getNameStr(), Ogre::OT_TRIANGLE_LIST);
  triangle->position(-1.15F, -0.85F, 0.0F);
  triangle->normal(0.0F, 0.0F, 1.0F);
  triangle->tangent(1.0F, 0.0F, 0.0F);
  triangle->textureCoord(0.0F, 0.0F);
  triangle->position(1.15F, -0.85F, 0.0F);
  triangle->normal(0.0F, 0.0F, 1.0F);
  triangle->tangent(1.0F, 0.0F, 0.0F);
  triangle->textureCoord(1.0F, 0.0F);
  triangle->position(0.0F, 0.95F, 0.0F);
  triangle->normal(0.0F, 0.0F, 1.0F);
  triangle->tangent(1.0F, 0.0F, 0.0F);
  triangle->textureCoord(0.5F, 1.0F);
  triangle->triangle(0U, 1U, 2U);
  triangle->end();
  Ogre::SceneNode* node =
      scene_manager.getRootSceneNode(Ogre::SCENE_DYNAMIC)
          ->createChildSceneNode(Ogre::SCENE_DYNAMIC);
  node->attachObject(triangle);
}

FrameMetrics InspectFrame(Ogre::Image2& image) {
  if (image.getWidth() != kFrameWidth || image.getHeight() != kFrameHeight) {
    throw std::runtime_error(
        "DXR7 Ogre readback dimensions do not match the render target");
  }
  FrameMetrics metrics;
  const std::size_t pixel_count =
      static_cast<std::size_t>(kFrameWidth) * kFrameHeight;
  metrics.rgb.reserve(pixel_count * 3U);
  std::vector<std::uint32_t> colours;
  colours.reserve(pixel_count);
  for (Ogre::uint32 y = 0U; y < kFrameHeight; ++y) {
    for (Ogre::uint32 x = 0U; x < kFrameWidth; ++x) {
      const Ogre::ColourValue colour = image.getColourAt(x, y, 0U);
      if (!std::isfinite(colour.r) || !std::isfinite(colour.g) ||
          !std::isfinite(colour.b) || !std::isfinite(colour.a)) {
        throw std::runtime_error(
            "DXR7 Ogre frame contains a non-finite pixel");
      }
      const unsigned char red = Quantize(colour.r);
      const unsigned char green = Quantize(colour.g);
      const unsigned char blue = Quantize(colour.b);
      metrics.rgb.push_back(red);
      metrics.rgb.push_back(green);
      metrics.rgb.push_back(blue);
      HashByte(metrics.fnv1a64, red);
      HashByte(metrics.fnv1a64, green);
      HashByte(metrics.fnv1a64, blue);
      colours.push_back((static_cast<std::uint32_t>(red) << 16U) |
                        (static_cast<std::uint32_t>(green) << 8U) |
                        static_cast<std::uint32_t>(blue));
      const float luminance = 0.2126F * colour.r + 0.7152F * colour.g +
                              0.0722F * colour.b;
      metrics.minimum_luminance =
          std::min(metrics.minimum_luminance, luminance);
      metrics.maximum_luminance =
          std::max(metrics.maximum_luminance, luminance);
    }
  }
  std::sort(colours.begin(), colours.end());
  metrics.distinct_pixels = static_cast<std::size_t>(
      std::distance(colours.begin(),
                    std::unique(colours.begin(), colours.end())));
  std::size_t largest_colour_run = 0U;
  for (auto run_start = colours.cbegin(); run_start != colours.cend();) {
    const auto run_end =
        std::upper_bound(run_start, colours.cend(), *run_start);
    largest_colour_run = std::max<std::size_t>(
        largest_colour_run,
        static_cast<std::size_t>(std::distance(run_start, run_end)));
    run_start = run_end;
  }
  metrics.non_background_pixels = colours.size() - largest_colour_run;
  if (metrics.distinct_pixels < 8U ||
      metrics.non_background_pixels < 512U ||
      metrics.maximum_luminance - metrics.minimum_luminance < 0.05F) {
    throw std::runtime_error(
        "DXR7 Ogre readback does not prove nonblank PBS geometry");
  }
  return metrics;
}

void WritePpmAtomically(const std::filesystem::path& path,
                        const FrameMetrics& metrics) {
  std::ostringstream header;
  header << "P6\n" << kFrameWidth << ' ' << kFrameHeight << "\n255\n";
  std::string payload = header.str();
  payload.append(reinterpret_cast<const char*>(metrics.rgb.data()),
                 metrics.rgb.size());
  WriteAtomically(path, payload);
}

std::string MakeReport(const char* status, const std::string& reason,
                       const std::string& execution_nonce,
                       const Dxr7BootstrapEvidence& evidence) {
  const bool passed = std::string(status) == "pass";
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"" << ROR_OGRE_NEXT_DXR7_SCHEMA << "\",\n"
         << "  \"status\": \"" << status << "\",\n"
         << "  \"reason\": \"" << JsonEscape(reason) << "\",\n"
         << "  \"execution\": {\n"
         << "    \"challenge_nonce\": \"" << execution_nonce << "\",\n"
         << "    \"probe_binary_marker\": \""
         << ROR_OGRE_NEXT_DXR7_BINARY_MARKER << "\"\n"
         << "  },\n"
         << "  \"scope\": {\n"
         << "    \"external_d3d11on12_foundation\": "
         << Boolean(evidence.d3d11on12_device_created) << ",\n"
         << "    \"hardware_dxr_pass\": " << Boolean(passed) << ",\n"
         << "    \"native_ray_tracing\": \""
         << (passed ? "dispatch_rays" : "unsupported") << "\",\n"
         << "    \"acceleration_structure_built\": "
         << Boolean(evidence.blas_built && evidence.tlas_built) << ",\n"
         << "    \"ray_traced_probe_readback\": "
         << Boolean(evidence.closest_hit_readback_exact) << ",\n"
         << "    \"ray_traced_image_produced\": false,\n"
         << "    \"ogre_raster_image_produced\": "
         << Boolean(evidence.ogre_frame_readback_completed) << ",\n"
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
         << ROR_OGRE_NEXT_DXR7_DXC_SHA256 << "\",\n"
         << "    \"dxcompiler_dll_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_DXCOMPILER_SHA256 << "\",\n"
         << "    \"dxil_dll_sha256\": \""
         << ROR_OGRE_NEXT_DXR7_DXIL_DLL_SHA256 << "\",\n"
         << "    \"dxc_sdk_version\": \""
         << ROR_OGRE_NEXT_DXR7_DXC_SDK_VERSION << "\",\n"
         << "    \"dxc_version\": \""
         << JsonEscape(ROR_OGRE_NEXT_DXR7_DXC_VERSION) << "\",\n"
         << "    \"dxc_path\": \""
         << JsonEscape(ROR_OGRE_NEXT_DXR7_DXC_PATH) << "\",\n"
         << "    \"dxc_x64_directory\": \""
         << JsonEscape(ROR_OGRE_NEXT_DXR7_DXC_X64_DIRECTORY) << "\"\n"
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
         << "  \"ogre_frame\": {\n"
         << "    \"native_hidden_window_created\": "
         << Boolean(evidence.ogre_native_window_created) << ",\n"
         << "    \"pbs_material_created\": "
         << Boolean(evidence.ogre_pbs_material_created) << ",\n"
         << "    \"compositor_workspace_created\": "
         << Boolean(evidence.ogre_compositor_workspace_created) << ",\n"
         << "    \"frame_submitted\": "
         << Boolean(evidence.ogre_frame_submitted) << ",\n"
         << "    \"gpu_readback_completed\": "
         << Boolean(evidence.ogre_frame_readback_completed) << ",\n"
         << "    \"nonblank\": "
         << Boolean(evidence.ogre_frame_nonblank) << ",\n"
         << "    \"ui_included\": false,\n"
         << "    \"resources_destroyed_before_ogre_shutdown\": "
         << Boolean(evidence.ogre_frame_resources_destroyed) << ",\n"
         << "    \"workspace_removed\": "
         << Boolean(evidence.ogre_teardown.workspace_removed) << ",\n"
         << "    \"workspace_definition_removed\": "
         << Boolean(evidence.ogre_teardown.workspace_definition_removed)
         << ",\n"
         << "    \"render_target_destroyed\": "
         << Boolean(evidence.ogre_teardown.render_target_destroyed) << ",\n"
         << "    \"scene_destroyed\": "
         << Boolean(evidence.ogre_teardown.scene_destroyed) << ",\n"
         << "    \"pbs_datablock_destroyed\": "
         << Boolean(evidence.ogre_teardown.pbs_datablock_destroyed) << ",\n"
         << "    \"pbs_hlms_unregistered\": "
         << Boolean(evidence.ogre_teardown.pbs_hlms_unregistered) << ",\n"
         << "    \"native_window_destroyed\": "
         << Boolean(evidence.ogre_teardown.native_window_destroyed) << ",\n"
         << "    \"root_shutdown_completed\": "
         << Boolean(evidence.ogre_teardown.root_shutdown_completed) << ",\n"
         << "    \"width\": " << evidence.ogre_frame_width << ",\n"
         << "    \"height\": " << evidence.ogre_frame_height << ",\n"
         << "    \"distinct_rgb8_values\": "
         << evidence.ogre_frame_distinct_pixels << ",\n"
         << "    \"non_background_pixels\": "
         << evidence.ogre_frame_non_background_pixels << ",\n"
         << "    \"rgb8_fnv1a64\": \"" << std::hex << std::setfill('0')
         << std::setw(16) << evidence.ogre_frame_fnv1a64 << std::dec
         << "\"\n"
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

void RequireTeardownStep(Dxr7OgreTeardownTracker& teardown,
                         Dxr7OgreTeardownStep step) {
  if (!teardown.Record(step)) {
    throw std::runtime_error("DXR7 Ogre teardown order regressed");
  }
}

void RunOgreFrameProof(OgreNextD3D12DxrBootstrap& bootstrap,
                       const std::filesystem::path& media_root,
                       const std::filesystem::path& image_path) {
  const Ogre::AbiCookie abi_cookie = Ogre::generateAbiCookie();
  Ogre::D3D11Plugin renderer_plugin;
  bool attachment_marked = false;
  FrameMetrics metrics;
  Dxr7OgreTeardownTracker teardown;
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
      const Ogre::ConfigOptionMap options = renderer->getConfigOptions();
      if (options.find("sRGB Gamma Conversion") != options.end()) {
        renderer->setConfigOption("sRGB Gamma Conversion", "Yes");
      }
      root.initialise(false);
      RequireReady(bootstrap.MarkOgreAttached());
      attachment_marked = true;
      Ogre::NameValuePairList window_parameters;
      window_parameters["hidden"] = "true";
      window_parameters["gamma"] = "true";
      window_parameters["FSAA"] = "1";
      Ogre::Window* window = root.createRenderWindow(
          "RoR Ogre-Next Windows DXR7 hidden frame", 64U, 64U, false,
          &window_parameters);
      if (window == nullptr || root.getCompositorManager2() == nullptr) {
        throw std::runtime_error(
            "external D3D11On12 device did not create a native hidden window");
      }
      RequireReady(bootstrap.VerifyOgreAdoption(renderer));

      Ogre::HlmsPbs* pbs = RegisterPbs(root, media_root);
      Ogre::HlmsPbsDatablock* datablock = CreateMaterial(*pbs);
      const Ogre::IdString datablock_name = datablock->getName();
      Ogre::SceneManager* scene_manager = root.createSceneManager(
          Ogre::ST_GENERIC, 1U, "RoRDxr7FrameScene");
      CreateTriangle(*scene_manager, *datablock);

      Ogre::Camera* camera =
          scene_manager->createCamera("RoRDxr7FrameCamera");
      camera->setPosition(0.0F, 0.0F, 3.0F);
      camera->lookAt(Ogre::Vector3::ZERO);
      camera->setNearClipDistance(0.1F);
      camera->setFarClipDistance(20.0F);
      camera->setAspectRatio(static_cast<float>(kFrameWidth) /
                             static_cast<float>(kFrameHeight));

      Ogre::Light* light = scene_manager->createLight();
      Ogre::SceneNode* light_node =
          scene_manager->getRootSceneNode()->createChildSceneNode();
      light_node->attachObject(light);
      light->setType(Ogre::Light::LT_DIRECTIONAL);
      light->setDirection(
          Ogre::Vector3(0.2F, -0.3F, -1.0F).normalisedCopy());
      light->setPowerScale(Ogre::Math::PI * 1.5F);
      scene_manager->setAmbientLight(
          Ogre::ColourValue(0.03F, 0.04F, 0.06F),
          Ogre::ColourValue(0.01F, 0.01F, 0.015F), Ogre::Vector3::UNIT_Y);

      Ogre::TextureGpuManager* texture_manager =
          renderer->getTextureGpuManager();
      Ogre::TextureGpu* target = texture_manager->createTexture(
          "RoRDxr7FrameTarget", Ogre::GpuPageOutStrategy::Discard,
          Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D);
      target->setResolution(kFrameWidth, kFrameHeight);
      target->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
      target->scheduleTransitionTo(Ogre::GpuResidency::Resident);

      Ogre::CompositorManager2* compositor_manager =
          root.getCompositorManager2();
      const Ogre::String workspace_name = "RoRDxr7FrameWorkspace";
      compositor_manager->createBasicWorkspaceDef(
          workspace_name, Ogre::ColourValue(0.008F, 0.012F, 0.02F, 1.0F),
          Ogre::IdString());
      Ogre::CompositorWorkspace* workspace =
          compositor_manager->addWorkspace(scene_manager, target, camera,
                                           workspace_name, true);
      for (std::size_t frame = 0U; frame < kWarmupFrames; ++frame) {
        if (!root.renderOneFrame()) {
          throw std::runtime_error(
              "OGRE-Next ended the DXR7 frame loop early");
        }
      }
      Ogre::Image2 image;
      image.convertFromTexture(target, 0U, 0U);
      metrics = InspectFrame(image);
      WritePpmAtomically(image_path, metrics);

      compositor_manager->removeWorkspace(workspace);
      RequireTeardownStep(teardown,
                          Dxr7OgreTeardownStep::WORKSPACE_REMOVED);
      compositor_manager->removeWorkspaceDefinition(
          Ogre::IdString(workspace_name));
      RequireTeardownStep(
          teardown, Dxr7OgreTeardownStep::WORKSPACE_DEFINITION_REMOVED);
      texture_manager->destroyTexture(target);
      RequireTeardownStep(teardown,
                          Dxr7OgreTeardownStep::RENDER_TARGET_DESTROYED);
      root.destroySceneManager(scene_manager);
      RequireTeardownStep(teardown,
                          Dxr7OgreTeardownStep::SCENE_DESTROYED);
      pbs->destroyDatablock(datablock_name);
      RequireTeardownStep(teardown,
                          Dxr7OgreTeardownStep::PBS_DATABLOCK_DESTROYED);
      root.getHlmsManager()->unregisterHlms(Ogre::HLMS_PBS);
      RequireTeardownStep(teardown,
                          Dxr7OgreTeardownStep::PBS_HLMS_UNREGISTERED);
      renderer->destroyRenderWindow(window);
      RequireTeardownStep(teardown,
                          Dxr7OgreTeardownStep::NATIVE_WINDOW_DESTROYED);
    }
    RequireTeardownStep(teardown,
                        Dxr7OgreTeardownStep::ROOT_SHUTDOWN_COMPLETED);
    if (!teardown.complete() ||
        metrics.distinct_pixels >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max()) ||
        metrics.non_background_pixels >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
      throw std::runtime_error(
          "DXR7 Ogre frame metrics or teardown proof is incomplete");
    }
    RequireReady(bootstrap.RecordOgreFrameProof(
        kFrameWidth, kFrameHeight,
        static_cast<std::uint32_t>(metrics.distinct_pixels),
        static_cast<std::uint32_t>(metrics.non_background_pixels),
        metrics.fnv1a64, true, teardown.contract()));
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
                                 arguments.execution_nonce,
                                 bootstrap.evidence()));
      std::cout << "DXR7 unsupported: " << initialization.message << '\n';
      return kUnsupportedExitCode;
    }
    RequireReady(initialization);
    RequireReady(bootstrap.ProveFenceBeforeDispatch());
    RequireReady(bootstrap.DispatchProbe(arguments.shader));
    RunOgreFrameProof(bootstrap, arguments.media_root, arguments.image);
    RequireReady(bootstrap.ProveFenceAfterOgre());
    RequireReady(bootstrap.Shutdown());
    if (!RoR::Render::ValidateDxr7PassContract(
            bootstrap.pass_contract())) {
      throw std::runtime_error("complete DXR7 pass contract was not satisfied");
    }
    WriteAtomically(arguments.report,
                    MakeReport("pass", "", arguments.execution_nonce,
                               bootstrap.evidence()));
    std::cout << "DXR7 D3D12/D3D11On12/Ogre frame proof passed\n";
    return 0;
  } catch (const std::exception& error) {
    static_cast<void>(bootstrap.AbortAfterFailure());
    try {
      WriteAtomically(arguments.report,
                      MakeReport("error", error.what(),
                                 arguments.execution_nonce,
                                 bootstrap.evidence()));
    } catch (const std::exception& report_error) {
      std::cerr << "DXR7 report failure: " << report_error.what() << '\n';
    }
    std::cerr << "DXR7 proof failed: " << error.what() << '\n';
    return 1;
  }
}
