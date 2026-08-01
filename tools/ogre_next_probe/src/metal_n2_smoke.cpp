/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextMetalRayTracingBackend.h"
#include "OgreNextN1Frontend.h"
#include "OgreNextN1NativeInterop.h"
#include "ror_ogre_next_n1_config.h"

#include <CommonCrypto/CommonDigest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef ROR_OGRE_NEXT_N2_SOURCE_COMMIT
#define ROR_OGRE_NEXT_N2_SOURCE_COMMIT "unknown"
#endif

#ifndef ROR_OGRE_NEXT_N2_SOURCE_REF
#define ROR_OGRE_NEXT_N2_SOURCE_REF "unknown"
#endif

namespace {

using namespace RoR::Render;

constexpr std::uint32_t kWidth = 96U;
constexpr std::uint32_t kHeight = 64U;
constexpr std::uint64_t kRegistryId = UINT64_C(0x4E325F4D4554414C);
constexpr int kCapabilitySkipExitCode = 77;

struct Arguments {
  std::string artifact_path;
  std::string report_path;
  std::string executable_path;
};

struct FileDigest {
  std::string sha256;
  std::uint64_t bytes = 0U;
};

struct SmokeResult {
  std::string report;
  int exit_code = EXIT_SUCCESS;
};

[[noreturn]] void Fail(const std::string &message) {
  throw std::runtime_error(message);
}

void Require(bool condition, const std::string &message) {
  if (!condition) {
    Fail(message);
  }
}

void RequireSuccess(const RenderOperationResult &result,
                    const std::string &operation) {
  if (!result) {
    Fail(operation + " failed: " + result.detail);
  }
}

Arguments ParseArguments(int argc, char **argv) {
  Arguments arguments;
  arguments.executable_path = argc > 0 && argv[0] != nullptr ? argv[0] : "";
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--output" && index + 1 < argc) {
      arguments.artifact_path = argv[++index];
    } else if (option == "--report" && index + 1 < argc) {
      arguments.report_path = argv[++index];
    } else {
      Fail("usage: ror_ogre_next_metal_n2_smoke [--output PROBE.bin] [--report REPORT.json]");
    }
  }
  return arguments;
}

std::string JsonEscape(const std::string &value) {
  std::string escaped;
  for (const unsigned char character : value) {
    switch (character) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (character >= 0x20U) {
        escaped.push_back(static_cast<char>(character));
      }
      break;
    }
  }
  return escaped;
}

std::string Hex(const unsigned char *bytes, std::size_t count) {
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (std::size_t index = 0U; index < count; ++index) {
    result << std::setw(2) << static_cast<unsigned int>(bytes[index]);
  }
  return result.str();
}

std::string Sha256(const std::vector<std::uint8_t> &bytes) {
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  CC_SHA256_CTX context;
  Require(CC_SHA256_Init(&context) == 1, "SHA-256 initialization failed");
  if (!bytes.empty()) {
    Require(bytes.size() <= (std::numeric_limits<CC_LONG>::max)(),
            "SHA-256 input exceeds CommonCrypto's bound");
    Require(CC_SHA256_Update(&context, bytes.data(),
                             static_cast<CC_LONG>(bytes.size())) == 1,
            "SHA-256 update failed");
  }
  Require(CC_SHA256_Final(digest.data(), &context) == 1,
          "SHA-256 finalization failed");
  return Hex(digest.data(), digest.size());
}

FileDigest HashFile(const std::string &path) {
  if (path.empty()) {
    Fail("the Metal N2 executable path is empty");
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail("could not open the Metal N2 executable for provenance: " + path);
  }
  CC_SHA256_CTX context;
  Require(CC_SHA256_Init(&context) == 1, "SHA-256 initialization failed");
  FileDigest result;
  char buffer[64U * 1024U];
  while (input) {
    input.read(buffer, sizeof(buffer));
    const std::streamsize count = input.gcount();
    if (count > 0) {
      Require(CC_SHA256_Update(&context, buffer,
                               static_cast<CC_LONG>(count)) == 1,
              "executable SHA-256 update failed");
      result.bytes += static_cast<std::uint64_t>(count);
    }
  }
  Require(input.eof(), "could not read the Metal N2 executable for provenance");
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  Require(CC_SHA256_Final(digest.data(), &context) == 1,
          "executable SHA-256 finalization failed");
  result.sha256 = Hex(digest.data(), digest.size());
  return result;
}

RenderAssetId AssetId(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x4E325F4153534554), low);
}

RenderAssetReference AssetRef(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, AssetId(low), 1U);
}

Bounds3 Bounds(float z) {
  Bounds3 bounds;
  bounds.minimum = {-0.5F, -0.5F, z};
  bounds.maximum = {0.5F, 0.5F, z};
  return bounds;
}

MeshResourceDescriptor MakeMesh() {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "N2 Ogre v2 pooled dynamic triangle";
  mesh.index_format = MeshIndexFormat::UINT16;
  mesh.dynamic = true;
  mesh.local_bounds.minimum = {-0.45F, -0.45F, 0.0F};
  mesh.local_bounds.maximum = {0.45F, 0.45F, 0.0F};
  mesh.positions = {
      {-0.45F, -0.45F, 0.0F},
      {0.45F, -0.45F, 0.0F},
      {0.0F, 0.45F, 0.0F},
  };
  mesh.normals.assign(mesh.positions.size(), Float3{0.0F, 0.0F, 1.0F});
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

MaterialDescriptor MakeMaterial() {
  MaterialDescriptor material;
  material.debug_name = "N2 texture-free raster witness";
  material.base_color_factor = {0.08F, 0.5F, 0.95F, 1.0F};
  material.metallic_factor = 0.1F;
  material.roughness_factor = 0.35F;
  material.emissive_factor = {0.5F, 0.1F, 0.02F};
  material.emissive_strength = 3.0F;
  return material;
}

RenderAssetDelta MakeCatalog() {
  RenderAssetDelta delta;
  delta.registry_id = kRegistryId;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  RenderAssetMutation mesh;
  mesh.asset = AssetRef(RenderAssetKind::MESH, 1U);
  mesh.payload = MakeMesh();
  delta.mutations.push_back(std::move(mesh));
  RenderAssetMutation material;
  material.asset = AssetRef(RenderAssetKind::MATERIAL, 2U);
  material.payload = MakeMaterial();
  delta.mutations.push_back(std::move(material));
  return delta;
}

std::shared_ptr<const SceneSnapshot> MakeScene(std::uint64_t snapshot_id,
                                                std::uint64_t revision,
                                                float z) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds = static_cast<double>(snapshot_id) / 48.0;
  descriptor.environment.ambient_radiance = {0.04F, 0.04F, 0.04F};

  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = AssetRef(RenderAssetKind::MESH, 1U);
  instance.material = AssetRef(RenderAssetKind::MATERIAL, 2U);
  instance.topology_revision = 1U;
  instance.deformation_revision = revision;
  instance.local_bounds = Bounds(z);
  descriptor.mesh_instances.push_back(instance);

  DynamicMeshUpdateDescriptor update;
  update.update_sequence = revision - 1U;
  update.instance_id = instance.instance_id;
  update.mesh = instance.mesh;
  update.topology_revision = instance.topology_revision;
  update.deformation_revision = revision;
  update.positions = {
      {-0.5F, -0.5F, z},
      {0.5F, -0.5F, z},
      {0.0F, 0.5F, z},
  };
  update.normals.assign(update.positions.size(), Float3{0.0F, 0.0F, 1.0F});
  update.has_updated_bounds = true;
  update.updated_local_bounds = instance.local_bounds;
  descriptor.dynamic_mesh_updates.push_back(std::move(update));

  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  if (!result) {
    Fail("could not create Metal N2 scene: " + result.validation.field +
         ": " + result.validation.detail);
  }
  return result.snapshot;
}

Matrix4x4 Projection() {
  constexpr float near_plane = 0.1F;
  constexpr float far_plane = 20.0F;
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.5F;
  projection.elements[10U] = far_plane / (near_plane - far_plane);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      near_plane * far_plane / (near_plane - far_plane);
  return projection;
}

RenderFrameRequest MakeFrame(
    std::uint64_t frame_id,
    const std::shared_ptr<const SceneSnapshot> &scene) {
  RenderFrameRequest request;
  request.frame_id = frame_id;
  request.scene_snapshot = scene;
  request.present = false;
  request.color_format = PixelFormat::RGBA8_SRGB;
  CameraViewRequest view;
  view.view_id = 1U;
  view.width = kWidth;
  view.height = kHeight;
  view.near_plane = 0.1F;
  view.far_plane = 20.0F;
  view.view_from_render.elements[14U] = -3.0F;
  view.previous_view_from_render = view.view_from_render;
  view.clip_from_view = Projection();
  view.previous_clip_from_view = view.clip_from_view;
  request.views.push_back(view);
  return request;
}

NativeRayTracingFrameRequest MakeRayTracingRequest(
    const RenderFrameRequest &frame) {
  NativeRayTracingFrameRequest request;
  request.frame = frame;
  request.samples_per_pixel = 1U;
  request.maximum_bounces = 1U;
  request.denoise = false;
  return request;
}

void WriteBinary(const std::string &path,
                 const std::vector<std::uint8_t> &bytes) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open Metal N2 readback artifact: " + path);
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    Fail("could not write Metal N2 readback artifact: " + path);
  }
}

void WriteText(const std::string &path, const std::string &text) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open Metal N2 report: " + path);
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!output) {
    Fail("could not write Metal N2 report: " + path);
  }
}

std::string MakeReport(
    const OgreNextMetalRayTracingEvidence &evidence,
    const NativeRayTracingCapabilityReport &ray_tracing,
    const FrontendCapabilityReport &frontend,
    const NativeInteropCapabilityReport &interop,
    const std::string &probe_sha256, std::size_t probe_bytes,
    const FileDigest &executable) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_metal_rt_n2.v2\",\n"
         << "  \"status\": \"pass\",\n"
         << "  \"scope\": \"same-device single-ray geometry interop capability probe; no rendered image, view-dependent result, GPU timing, material, lighting, denoising, or compositing claim\",\n"
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \"https://github.com/RigsOfRods/rigs-of-rods\",\n"
         << "    \"ror_ref\": \"" << JsonEscape(ROR_OGRE_NEXT_N2_SOURCE_REF)
         << "\",\n"
         << "    \"ror_commit\": \""
         << JsonEscape(ROR_OGRE_NEXT_N2_SOURCE_COMMIT) << "\",\n"
         << "    \"ogre_next_repository\": \"https://github.com/OGRECave/ogre-next\",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "    \"ogre_next_archive_sha256\": \""
         << ROR_OGRE_NEXT_N1_OGRE_ARCHIVE_SHA256 << "\",\n"
         << "    \"build_artifact\": \"ror_ogre_next_metal_n2_smoke\",\n"
         << "    \"build_artifact_bytes\": " << executable.bytes
         << ",\n"
         << "    \"build_artifact_sha256\": \""
         << executable.sha256 << "\"\n"
         << "  },\n"
         << "  \"device\": {\n"
         << "    \"name\": \"" << JsonEscape(evidence.device_name)
         << "\",\n"
         << "    \"context_id\": " << evidence.context.context_id << ",\n"
         << "    \"same_ogre_device\": "
         << (evidence.same_ogre_device ? "true" : "false") << ",\n"
         << "    \"same_ogre_queue\": "
         << (evidence.same_ogre_queue ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"admission\": {\n"
         << "    \"frontend_api_reported\": "
         << (frontend.supports_native_ray_tracing_api ? "true" : "false")
         << ",\n"
         << "    \"backend_compiled\": "
         << (ray_tracing.backend_compiled ? "true" : "false") << ",\n"
         << "    \"api_supported\": "
         << (evidence.api_supported ? "true" : "false") << ",\n"
         << "    \"supports_raytracing\": "
         << (ray_tracing.api_supported ? "true" : "false") << ",\n"
         << "    \"supports_family_apple9\": "
         << (evidence.apple_family_9_supported ? "true" : "false") << ",\n"
         << "    \"hardware_accelerated\": "
         << (ray_tracing.hardware_accelerated ? "true" : "false") << ",\n"
         << "    \"dispatch_readback_probe_passed\": "
         << (ray_tracing.dispatch_readback_probe_passed ? "true" : "false")
         << ",\n"
         << "    \"geometry_interop_ready\": "
         << (ray_tracing.geometry_interop_ready ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"geometry\": {\n"
         << "    \"frame_id\": " << evidence.geometry_export.frame_id
         << ",\n"
         << "    \"snapshot_id\": " << evidence.geometry_export.snapshot_id
         << ",\n"
         << "    \"instance_id\": " << evidence.geometry_export.instance_id
         << ",\n"
         << "    \"topology_revision\": "
         << evidence.geometry_export.topology_revision << ",\n"
         << "    \"deformation_revision\": "
         << evidence.geometry_export.deformation_revision << ",\n"
         << "    \"vertex_count\": " << evidence.geometry_export.vertex_count
         << ",\n"
         << "    \"index_count\": " << evidence.geometry_export.index_count
         << ",\n"
         << "    \"vertex_buffer_generation\": "
         << evidence.geometry_export.positions.buffer.generation << ",\n"
         << "    \"vertex_pool_offset_bytes\": "
         << evidence.geometry_export.positions.offset_bytes << ",\n"
         << "    \"vertex_slice_bytes\": "
         << evidence.geometry_export.positions.size_bytes << ",\n"
         << "    \"vertex_stride_bytes\": "
         << evidence.geometry_export.positions.stride_bytes << ",\n"
         << "    \"vertex_buffer_length_bytes\": "
         << evidence.vertex_buffer_length_bytes << ",\n"
         << "    \"index_buffer_generation\": "
         << evidence.geometry_export.indices.buffer.generation << ",\n"
         << "    \"index_pool_offset_bytes\": "
         << evidence.geometry_export.indices.offset_bytes << ",\n"
         << "    \"index_slice_bytes\": "
         << evidence.geometry_export.indices.size_bytes << ",\n"
         << "    \"index_stride_bytes\": "
         << evidence.geometry_export.indices.stride_bytes << ",\n"
         << "    \"index_buffer_length_bytes\": "
         << evidence.index_buffer_length_bytes << ",\n"
         << "    \"exact_exported_vertex_slice_used\": "
         << (evidence.exact_exported_vertex_slice_used ? "true" : "false")
         << ",\n"
         << "    \"exact_exported_index_slice_used\": "
         << (evidence.exact_exported_index_slice_used ? "true" : "false")
         << "\n"
         << "  },\n"
         << "  \"synchronization\": {\n"
         << "    \"shared_event_generation\": "
         << evidence.frame_synchronization.frontend_complete_timeline.generation
         << ",\n"
         << "    \"frontend_complete_value\": "
         << evidence.frame_synchronization.frontend_complete_value << ",\n"
         << "    \"external_complete_value\": "
         << evidence.frame_synchronization.external_complete_value << ",\n"
         << "    \"same_shared_event\": true,\n"
         << "    \"external_encoders_ended_before_signal\": true,\n"
         << "    \"cpu_wait_after_commit_only\": true\n"
         << "  },\n"
         << "  \"acceleration_structures\": {\n"
         << "    \"blas_bytes\": " << evidence.blas_bytes << ",\n"
         << "    \"blas_scratch_bytes\": " << evidence.blas_scratch_bytes
         << ",\n"
         << "    \"tlas_bytes\": " << evidence.tlas_bytes << ",\n"
         << "    \"tlas_scratch_bytes\": " << evidence.tlas_scratch_bytes
         << "\n"
         << "  },\n"
         << "  \"probe\": {\n"
         << "    \"kind\": \"single_ray_geometry_interop\",\n"
         << "    \"rays\": 1,\n"
         << "    \"hit_magic\": " << evidence.hit_magic << ",\n"
         << "    \"hit_distance\": " << std::setprecision(9)
         << evidence.hit_distance << ",\n"
         << "    \"probe_readback_bytes\": " << probe_bytes << ",\n"
         << "    \"probe_readback_sha256\": \"" << probe_sha256
         << "\",\n"
         << "    \"rendered_image_produced\": false,\n"
         << "    \"view_dependent\": false,\n"
         << "    \"gpu_timestamp_measured\": false\n"
         << "  },\n"
         << "  \"lifecycle\": {\n"
         << "    \"stale_generation_rejected\": true,\n"
         << "    \"revision_n_plus_one_blocked_while_n_live\": true,\n"
         << "    \"frontend_shutdown_blocked_before_backend\": true,\n"
         << "    \"backend_shutdown_before_frontend\": true,\n"
         << "    \"frontend_destructor_before_backend_safe\": true,\n"
         << "    \"backend_destructor_before_frontend_safe\": true,\n"
         << "    \"post_release_revision_n_plus_one_rendered\": true,\n"
         << "    \"interop_report_geometry_proven\": "
         << (interop.geometry_interop_proven ? "true" : "false") << "\n"
         << "  }\n"
         << "}\n";
  return report.str();
}

std::string MakeSkipReport(
    const OgreNextMetalRayTracingEvidence &evidence,
    const NativeRayTracingCapabilityReport &ray_tracing,
    const FrontendCapabilityReport &frontend,
    const NativeInteropCapabilityReport &interop,
    const RenderOperationResult &initialization, const FileDigest &executable) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_metal_rt_n2.v2\",\n"
         << "  \"status\": \"skip\",\n"
         << "  \"scope\": \"same-device single-ray geometry interop capability probe; no rendered image, view-dependent result, GPU timing, material, lighting, denoising, or compositing claim\",\n"
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \"https://github.com/RigsOfRods/rigs-of-rods\",\n"
         << "    \"ror_ref\": \"" << JsonEscape(ROR_OGRE_NEXT_N2_SOURCE_REF)
         << "\",\n"
         << "    \"ror_commit\": \""
         << JsonEscape(ROR_OGRE_NEXT_N2_SOURCE_COMMIT) << "\",\n"
         << "    \"ogre_next_repository\": \"https://github.com/OGRECave/ogre-next\",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "    \"ogre_next_archive_sha256\": \""
         << ROR_OGRE_NEXT_N1_OGRE_ARCHIVE_SHA256 << "\",\n"
         << "    \"build_artifact\": \"ror_ogre_next_metal_n2_smoke\",\n"
         << "    \"build_artifact_bytes\": " << executable.bytes << ",\n"
         << "    \"build_artifact_sha256\": \"" << executable.sha256
         << "\"\n"
         << "  },\n"
         << "  \"device\": {\n"
         << "    \"name\": \"" << JsonEscape(evidence.device_name) << "\",\n"
         << "    \"context_id\": " << evidence.context.context_id << ",\n"
         << "    \"same_ogre_device\": "
         << (evidence.same_ogre_device ? "true" : "false") << ",\n"
         << "    \"same_ogre_queue\": "
         << (evidence.same_ogre_queue ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"admission\": {\n"
         << "    \"frontend_api_reported\": "
         << (frontend.supports_native_ray_tracing_api ? "true" : "false")
         << ",\n"
         << "    \"interop_context_exported\": "
         << (interop.exports_native_context ? "true" : "false") << ",\n"
         << "    \"backend_compiled\": "
         << (ray_tracing.backend_compiled ? "true" : "false") << ",\n"
         << "    \"supports_raytracing\": "
         << (evidence.api_supported ? "true" : "false") << ",\n"
         << "    \"supports_family_apple9\": "
         << (evidence.apple_family_9_supported ? "true" : "false") << ",\n"
         << "    \"hardware_floor_met\": "
         << (ray_tracing.hardware_accelerated ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"skip\": {\n"
         << "    \"initialization_code\": \"UNSUPPORTED\",\n"
         << "    \"reason\": \"" << JsonEscape(initialization.detail) << "\",\n"
         << "    \"required_metal_ray_tracing\": true,\n"
         << "    \"required_apple_gpu_family\": 9\n"
         << "  },\n"
         << "  \"probe\": {\n"
         << "    \"executed\": false,\n"
         << "    \"probe_readback_bytes\": 0,\n"
         << "    \"rendered_image_produced\": false,\n"
         << "    \"view_dependent\": false,\n"
         << "    \"gpu_timestamp_measured\": false\n"
         << "  }\n"
         << "}\n";
  return report.str();
}

void ProveFrontendDestructorBeforeBackend() {
  auto frontend = std::make_unique<OgreNextN1Frontend>(
      OgreNextNativeFeatureTier::METAL_RAY_TRACING_N2);
  FrontendInitializationRequest initialization;
  initialization.initial_width = kWidth;
  initialization.initial_height = kHeight;
  initialization.maximum_frames_in_flight = 1U;
  initialization.headless = true;
  initialization.vertical_sync = false;
  RequireSuccess(frontend->Initialize(initialization),
                 "frontend-first Initialize");
  RequireSuccess(frontend->SynchronizeAssets(MakeCatalog()),
                 "frontend-first SynchronizeAssets");
  NativeRenderInterop *interop = frontend->GetNativeInterop();
  Require(interop != nullptr,
          "frontend-first case did not expose Metal interop");
  auto ray_tracing = std::make_unique<OgreNextMetalRayTracingBackend>();
  RequireSuccess(ray_tracing->Initialize(*interop),
                 "frontend-first RT Initialize");
  const RenderFrameRequest frame = MakeFrame(10U, MakeScene(10U, 2U, 0.0F));
  RenderFrameOutput raster_output;
  RequireSuccess(frontend->Render(frame, raster_output),
                 "frontend-first raster");
  RequireSuccess(ray_tracing->RunGeometryInteropProbe(
                     MakeRayTracingRequest(frame)),
                 "frontend-first probe");

  frontend.reset();
  RequireSuccess(ray_tracing->Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "backend shutdown after frontend destructor");
}

void ProveBackendDestructorBeforeFrontend() {
  auto frontend = std::make_unique<OgreNextN1Frontend>(
      OgreNextNativeFeatureTier::METAL_RAY_TRACING_N2);
  FrontendInitializationRequest initialization;
  initialization.initial_width = kWidth;
  initialization.initial_height = kHeight;
  initialization.maximum_frames_in_flight = 1U;
  initialization.headless = true;
  initialization.vertical_sync = false;
  RequireSuccess(frontend->Initialize(initialization),
                 "backend-first Initialize");
  RequireSuccess(frontend->SynchronizeAssets(MakeCatalog()),
                 "backend-first SynchronizeAssets");
  NativeRenderInterop *interop = frontend->GetNativeInterop();
  Require(interop != nullptr,
          "backend-first case did not expose Metal interop");
  auto ray_tracing = std::make_unique<OgreNextMetalRayTracingBackend>();
  RequireSuccess(ray_tracing->Initialize(*interop),
                 "backend-first RT Initialize");
  const RenderFrameRequest frame = MakeFrame(20U, MakeScene(20U, 2U, 0.0F));
  RenderFrameOutput raster_output;
  RequireSuccess(frontend->Render(frame, raster_output),
                 "backend-first raster");
  RequireSuccess(ray_tracing->RunGeometryInteropProbe(
                     MakeRayTracingRequest(frame)),
                 "backend-first probe");

  ray_tracing.reset();
  const RenderFrameRequest next_frame =
      MakeFrame(21U, MakeScene(21U, 3U, 0.25F));
  RenderFrameOutput next_output;
  RequireSuccess(frontend->Render(next_frame, next_output),
                 "raster after backend destructor");
  RequireSuccess(frontend->Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "frontend shutdown after backend destructor");
}

SmokeResult RunSmoke(const Arguments &arguments) {
  const FileDigest executable = HashFile(arguments.executable_path);
  OgreNextN1Frontend frontend(
      OgreNextNativeFeatureTier::METAL_RAY_TRACING_N2);
  FrontendInitializationRequest initialization;
  initialization.initial_width = kWidth;
  initialization.initial_height = kHeight;
  initialization.maximum_frames_in_flight = 1U;
  initialization.headless = true;
  initialization.vertical_sync = false;
  RequireSuccess(frontend.Initialize(initialization), "frontend Initialize");
  RequireSuccess(frontend.SynchronizeAssets(MakeCatalog()),
                 "frontend SynchronizeAssets");
  NativeRenderInterop *interop = frontend.GetNativeInterop();
  Require(interop != nullptr, "N2 frontend did not expose Metal interop");

  OgreNextMetalRayTracingBackend ray_tracing;
  const RenderOperationResult rt_initialization =
      ray_tracing.Initialize(*interop);
  if (!rt_initialization &&
      rt_initialization.code == RenderOperationCode::UNSUPPORTED) {
    const OgreNextMetalRayTracingEvidence evidence = ray_tracing.evidence();
    const NativeRayTracingCapabilityReport ray_capabilities =
        ray_tracing.QueryCapabilities();
    const FrontendCapabilityReport frontend_capabilities =
        frontend.QueryCapabilities();
    const NativeInteropCapabilityReport interop_capabilities =
        interop->QueryCapabilities();
    RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                   "unsupported-hardware frontend Shutdown");
    return {MakeSkipReport(evidence, ray_capabilities,
                           frontend_capabilities, interop_capabilities,
                           rt_initialization, executable),
            kCapabilitySkipExitCode};
  }
  RequireSuccess(rt_initialization, "RT Initialize");
  const auto scene_n = MakeScene(1U, 2U, 0.0F);
  const RenderFrameRequest frame_n = MakeFrame(1U, scene_n);
  RenderFrameOutput raster_output;
  RequireSuccess(frontend.Render(frame_n, raster_output), "raster revision N");

  RenderFrameOutput unsupported_output;
  Require(ray_tracing
                  .Render(MakeRayTracingRequest(frame_n), unsupported_output)
                  .code == RenderOperationCode::UNSUPPORTED &&
              unsupported_output.attachments.empty(),
          "Metal N2 incorrectly claimed to produce a rendered image");
  RequireSuccess(ray_tracing.RunGeometryInteropProbe(
                     MakeRayTracingRequest(frame_n)),
                 "same-device one-ray geometry probe");

  const OgreNextMetalRayTracingEvidence evidence = ray_tracing.evidence();
  NativeGeometryInteropProofSet proof;
  proof.frontend = frontend.QueryCapabilities();
  proof.interop = interop->QueryCapabilities();
  proof.ray_tracing = ray_tracing.QueryCapabilities();
  proof.frontend_object = &frontend;
  proof.native_interop_object = interop;
  proof.native_ray_tracing_backend = &ray_tracing;
  proof.native_context = evidence.context;
  proof.geometry_request = evidence.geometry_request;
  proof.geometry_export = evidence.geometry_export;
  proof.frame_synchronization = evidence.frame_synchronization;
  const ValidationResult proof_validation =
      ValidateNativeGeometryInteropProofSet(proof);
  Require(proof_validation.ok(),
          "live Metal N2 proof set failed: " + proof_validation.field +
              ": " + proof_validation.detail);

  NativeGeometryExport stale_geometry = evidence.geometry_export;
  ++stale_geometry.positions.buffer.generation;
  Require(ray_tracing
                  .ValidateInteropEvidence(stale_geometry,
                                           evidence.frame_synchronization)
                  .code == RenderOperationCode::RESOURCE_STALE,
          "stale exported MTLBuffer generation passed evidence validation");
  Require(frontend.Shutdown(0U).code ==
              RenderOperationCode::OUTSTANDING_LEASES,
          "frontend shutdown ignored its registered RT backend and live leases");

  const auto scene_n_plus_one = MakeScene(2U, 3U, 0.25F);
  const RenderFrameRequest frame_n_plus_one = MakeFrame(2U, scene_n_plus_one);
  RenderFrameOutput blocked_output;
  Require(frontend.Render(frame_n_plus_one, blocked_output).code ==
              RenderOperationCode::OUTSTANDING_LEASES,
          "revision N+1 replaced the raster/export buffers held by revision N");

  RequireSuccess(ray_tracing.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "RT Shutdown");
  RenderFrameOutput released_output;
  RequireSuccess(frontend.Render(frame_n_plus_one, released_output),
                 "post-release raster revision N+1");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "frontend Shutdown");

  const std::vector<std::uint8_t> &probe = evidence.probe_readback_bytes;
  Require(probe.size() == 8U,
          "Metal N2 did not retain the exact eight-byte GPU probe result");
  WriteBinary(arguments.artifact_path, probe);
  ProveFrontendDestructorBeforeBackend();
  ProveBackendDestructorBeforeFrontend();
  return {MakeReport(evidence, proof.ray_tracing, proof.frontend, proof.interop,
                     Sha256(probe), probe.size(), executable),
          EXIT_SUCCESS};
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    const SmokeResult result = RunSmoke(arguments);
    WriteText(arguments.report_path, result.report);
    std::cout << result.report;
    return result.exit_code;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next Metal N2 smoke failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
