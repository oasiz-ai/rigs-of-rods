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
#include <cstring>
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

#ifndef ROR_OGRE_NEXT_N3_SOURCE_COMMIT
#define ROR_OGRE_NEXT_N3_SOURCE_COMMIT "unknown"
#endif
#ifndef ROR_OGRE_NEXT_N3_SOURCE_REPOSITORY
#define ROR_OGRE_NEXT_N3_SOURCE_REPOSITORY "unknown"
#endif
#ifndef ROR_OGRE_NEXT_N3_SOURCE_REF
#define ROR_OGRE_NEXT_N3_SOURCE_REF "unknown"
#endif
#ifndef ROR_OGRE_NEXT_N3_RELEVANT_SOURCE_CLEAN
#define ROR_OGRE_NEXT_N3_RELEVANT_SOURCE_CLEAN 0
#endif
#ifndef ROR_OGRE_NEXT_N3_SOURCE_MANIFEST_SHA256
#define ROR_OGRE_NEXT_N3_SOURCE_MANIFEST_SHA256 "unknown"
#endif

#if !defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
#error "The Metal N3 smoke target requires its isolated native fault seam"
#endif

namespace {

using namespace RoR::Render;

constexpr std::uint32_t kWidth = 96U;
constexpr std::uint32_t kHeight = 64U;
constexpr std::uint64_t kRegistryId = UINT64_C(0x4E335F4D4554414C);
constexpr int kCapabilitySkipExitCode = 77;

struct Arguments {
  std::string media_root;
  std::string raster_path;
  std::string contribution_path;
  std::string hybrid_path;
  std::string report_path;
  std::string executable_path;
};

struct ImageMetrics {
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint64_t bytes = 0U;
  std::uint64_t nontrivial_pixels = 0U;
  double mean_luminance = 0.0;
  std::string sha256;
};

struct FileDigest {
  std::uint64_t bytes = 0U;
  std::string sha256;
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
    if (option == "--media-root" && index + 1 < argc) {
      arguments.media_root = argv[++index];
    } else if (option == "--raster" && index + 1 < argc) {
      arguments.raster_path = argv[++index];
    } else if (option == "--contribution" && index + 1 < argc) {
      arguments.contribution_path = argv[++index];
    } else if (option == "--hybrid" && index + 1 < argc) {
      arguments.hybrid_path = argv[++index];
    } else if (option == "--report" && index + 1 < argc) {
      arguments.report_path = argv[++index];
    } else {
      Fail("usage: ror_ogre_next_metal_n3_smoke --media-root ABSOLUTE_PATH [--raster RASTER.bin] [--contribution RT.bin] [--hybrid HYBRID.bin] [--report REPORT.json]");
    }
  }
  Require(!arguments.media_root.empty(), "--media-root is required");
  return arguments;
}

std::string JsonEscape(const std::string &value) {
  std::string escaped;
  for (const unsigned char character : value) {
    if (character == '"' || character == '\\') {
      escaped.push_back('\\');
      escaped.push_back(static_cast<char>(character));
    } else if (character == '\n') {
      escaped += "\\n";
    } else if (character >= 0x20U) {
      escaped.push_back(static_cast<char>(character));
    }
  }
  return escaped;
}

std::string Hex(const unsigned char *bytes, std::size_t count) {
  std::ostringstream text;
  text << std::hex << std::setfill('0');
  for (std::size_t index = 0U; index < count; ++index) {
    text << std::setw(2) << static_cast<unsigned int>(bytes[index]);
  }
  return text.str();
}

std::string Sha256(const std::vector<std::uint8_t> &bytes) {
  CC_SHA256_CTX context;
  Require(CC_SHA256_Init(&context) == 1, "SHA-256 initialization failed");
  Require(bytes.size() <= (std::numeric_limits<CC_LONG>::max)(),
          "SHA-256 input is too large");
  if (!bytes.empty()) {
    Require(CC_SHA256_Update(&context, bytes.data(),
                             static_cast<CC_LONG>(bytes.size())) == 1,
            "SHA-256 update failed");
  }
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  Require(CC_SHA256_Final(digest.data(), &context) == 1,
          "SHA-256 finalization failed");
  return Hex(digest.data(), digest.size());
}

FileDigest HashFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  Require(static_cast<bool>(input), "could not open N3 executable");
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
  Require(input.eof(), "could not finish reading N3 executable");
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  Require(CC_SHA256_Final(digest.data(), &context) == 1,
          "executable SHA-256 finalization failed");
  result.sha256 = Hex(digest.data(), digest.size());
  return result;
}

float HalfToFloat(std::uint16_t value) {
  const bool negative = (value & UINT16_C(0x8000)) != 0U;
  const std::uint16_t exponent =
      static_cast<std::uint16_t>((value >> 10U) & UINT16_C(0x1F));
  const std::uint16_t mantissa = value & UINT16_C(0x03FF);
  float magnitude = 0.0F;
  if (exponent == 0U) {
    magnitude = std::ldexp(static_cast<float>(mantissa), -24);
  } else if (exponent == 31U) {
    magnitude = mantissa == 0U
                    ? (std::numeric_limits<float>::infinity)()
                    : (std::numeric_limits<float>::quiet_NaN)();
  } else {
    magnitude = std::ldexp(1.0F + static_cast<float>(mantissa) / 1024.0F,
                           static_cast<int>(exponent) - 15);
  }
  return negative ? -magnitude : magnitude;
}

ImageMetrics Analyze(const std::vector<std::uint8_t> &bytes,
                     std::uint32_t width, std::uint32_t height) {
  const std::uint64_t expected =
      static_cast<std::uint64_t>(width) * height * 8U;
  Require(bytes.size() == expected,
          "RGBA16F attachment byte count does not match its extent");
  ImageMetrics metrics;
  metrics.width = width;
  metrics.height = height;
  metrics.bytes = bytes.size();
  metrics.sha256 = Sha256(bytes);
  double luminance_sum = 0.0;
  for (std::uint64_t pixel = 0U;
       pixel < static_cast<std::uint64_t>(width) * height; ++pixel) {
    float channels[4U] = {};
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
      std::uint16_t half = 0U;
      std::memcpy(&half,
                  bytes.data() + static_cast<std::size_t>(pixel) * 8U +
                      channel * 2U,
                  sizeof(half));
      channels[channel] = HalfToFloat(half);
      Require(std::isfinite(channels[channel]),
              "RGBA16F attachment contains a non-finite channel");
    }
    const double luminance = 0.2126 * channels[0U] +
                             0.7152 * channels[1U] +
                             0.0722 * channels[2U];
    luminance_sum += luminance;
    if (std::fabs(channels[0U]) > 1.0e-6F ||
        std::fabs(channels[1U]) > 1.0e-6F ||
        std::fabs(channels[2U]) > 1.0e-6F) {
      ++metrics.nontrivial_pixels;
    }
  }
  metrics.mean_luminance = luminance_sum /
      static_cast<double>(static_cast<std::uint64_t>(width) * height);
  return metrics;
}

void VerifyContributionMapping(const std::vector<std::uint8_t> &raster,
                               const std::vector<std::uint8_t> &contribution,
                               const std::vector<std::uint8_t> &hybrid,
                               std::uint64_t expected_contribution_pixels) {
  Require(raster.size() == contribution.size() &&
              raster.size() == hybrid.size() && raster.size() % 8U == 0U,
          "N3 evidence attachments are not exactly correlated");
  std::uint64_t applied = 0U;
  std::uint64_t untouched = 0U;
  for (std::size_t offset = 0U; offset < raster.size(); offset += 8U) {
    bool applies = false;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      std::uint16_t half = 0U;
      std::memcpy(&half, contribution.data() + offset + channel * 2U,
                  sizeof(half));
      applies = applies || (half & UINT16_C(0x7FFF)) != 0U;
    }
    std::uint16_t contribution_alpha = 1U;
    std::memcpy(&contribution_alpha, contribution.data() + offset + 6U,
                sizeof(contribution_alpha));
    Require(contribution_alpha == 0U,
            "N3 contribution modified straight alpha");
    if (applies) {
      ++applied;
      Require(std::memcmp(raster.data() + offset, hybrid.data() + offset,
                          6U) != 0,
              "N3 applied contribution did not change hybrid RGB");
    } else {
      ++untouched;
      Require(std::memcmp(raster.data() + offset, hybrid.data() + offset,
                          8U) == 0,
              "N3 hybrid changed a pixel without an RT contribution");
    }
  }
  Require(applied == expected_contribution_pixels && applied != 0U &&
              untouched != 0U,
          "N3 contribution mask did not match independently counted pixels");
}

RenderAssetId AssetId(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x4E335F4153534554), low);
}

RenderAssetReference AssetRef(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, AssetId(low), 1U);
}

RenderAssetDelta MakeCatalog() {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "N3 dynamic triangle";
  mesh.index_format = MeshIndexFormat::UINT16;
  mesh.dynamic = true;
  mesh.local_bounds.minimum = {-0.5F, -0.5F, 0.0F};
  mesh.local_bounds.maximum = {0.5F, 0.5F, 0.0F};
  mesh.positions = {{-0.5F, -0.5F, 0.0F},
                    {0.5F, -0.5F, 0.0F},
                    {0.0F, 0.5F, 0.0F}};
  mesh.normals.assign(mesh.positions.size(), Float3{0.0F, 0.0F, 1.0F});
  mesh.tangents.assign(mesh.positions.size(),
                       Float4{1.0F, 0.0F, 0.0F, 1.0F});
  mesh.texture_coordinates_0 = {
      {0.0F, 1.0F},
      {1.0F, 1.0F},
      {0.5F, 0.0F},
  };
  mesh.indices = {0U, 1U, 2U};

  MaterialDescriptor material;
  material.debug_name = "simultaneous RT4 textured N3 raster witness";
  material.base_color_factor = {0.72F, 0.82F, 0.94F, 1.0F};
  material.metallic_factor = 0.15F;
  material.roughness_factor = 0.38F;
  material.emissive_factor = {0.02F, 0.01F, 0.005F};
  material.emissive_strength = 0.25F;
  material.base_color_texture.texture =
      AssetRef(RenderAssetKind::TEXTURE, 3U);
  material.base_color_texture.sampler =
      AssetRef(RenderAssetKind::SAMPLER, 4U);

  TextureResourceDescriptor texture;
  texture.debug_name = "N3 RT4 sRGB padded-row base-color texture";
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 2U;
  texture.height = 2U;
  TextureMipLevelDescriptor mip;
  mip.width = 2U;
  mip.height = 2U;
  mip.row_pitch_bytes = 12U;
  mip.layer_pitch_bytes = 24U;
  mip.bytes.assign(24U, 0xA7U);
  const std::array<std::uint8_t, 16U> rgba{{
      245U, 35U, 20U, 255U, 25U, 225U, 55U, 255U,
      30U, 60U, 240U, 255U, 245U, 205U, 25U, 255U,
  }};
  std::memcpy(mip.bytes.data(), rgba.data(), 8U);
  std::memcpy(mip.bytes.data() + 12U, rgba.data() + 8U, 8U);
  texture.mip_levels.push_back(std::move(mip));

  SamplerResourceDescriptor sampler;
  sampler.debug_name = "N3 RT4 base-color sampler";
  sampler.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;

  RenderAssetDelta delta;
  delta.registry_id = kRegistryId;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  RenderAssetMutation mesh_mutation;
  mesh_mutation.asset = AssetRef(RenderAssetKind::MESH, 1U);
  mesh_mutation.payload = std::move(mesh);
  delta.mutations.push_back(std::move(mesh_mutation));
  RenderAssetMutation material_mutation;
  material_mutation.asset = AssetRef(RenderAssetKind::MATERIAL, 2U);
  material_mutation.payload = std::move(material);
  delta.mutations.push_back(std::move(material_mutation));
  RenderAssetMutation texture_mutation;
  texture_mutation.asset = AssetRef(RenderAssetKind::TEXTURE, 3U);
  texture_mutation.payload = std::move(texture);
  delta.mutations.push_back(std::move(texture_mutation));
  RenderAssetMutation sampler_mutation;
  sampler_mutation.asset = AssetRef(RenderAssetKind::SAMPLER, 4U);
  sampler_mutation.payload = std::move(sampler);
  delta.mutations.push_back(std::move(sampler_mutation));
  return delta;
}

std::shared_ptr<const SceneSnapshot> MakeScene(
    std::uint64_t snapshot_id, std::uint64_t revision,
    const Matrix4x4 &render_from_object = Matrix4x4{}) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds = static_cast<double>(snapshot_id) / 48.0;
  descriptor.environment.ambient_radiance = {0.01F, 0.012F, 0.015F};
  LightDescriptor light;
  light.light_id = 1U;
  light.type = LightType::DIRECTIONAL;
  Require(NormalizePhotometricColorLinear({1.0F, 0.92F, 0.82F},
                                          light.color_linear),
          "N3 directional tint could not be normalized");
  light.intensity = 1024.0F;
  light.direction = {0.0F, 0.0F, -1.0F};
  light.shadow_flags = 0U;
  descriptor.lights.push_back(light);

  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = AssetRef(RenderAssetKind::MESH, 1U);
  instance.material = AssetRef(RenderAssetKind::MATERIAL, 2U);
  instance.topology_revision = 1U;
  instance.deformation_revision = revision;
  instance.render_from_object = render_from_object;
  instance.previous_render_from_object = render_from_object;
  instance.local_bounds.minimum = {-0.5F, -0.5F, 0.0F};
  instance.local_bounds.maximum = {0.5F, 0.5F, 0.0F};
  descriptor.mesh_instances.push_back(instance);

  DynamicMeshUpdateDescriptor update;
  update.update_sequence = revision - 1U;
  update.instance_id = instance.instance_id;
  update.mesh = instance.mesh;
  update.topology_revision = instance.topology_revision;
  update.deformation_revision = revision;
  update.positions = {{-0.5F, -0.5F, 0.0F},
                      {0.5F, -0.5F, 0.0F},
                      {0.0F, 0.5F, 0.0F}};
  update.normals.assign(update.positions.size(), Float3{0.0F, 0.0F, 1.0F});
  update.tangents.assign(update.positions.size(),
                         Float4{1.0F, 0.0F, 0.0F, 1.0F});
  update.has_updated_bounds = true;
  update.updated_local_bounds = instance.local_bounds;
  descriptor.dynamic_mesh_updates.push_back(std::move(update));
  SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "could not create N3 scene: " +
                            created.validation.field + ": " +
                            created.validation.detail);
  return created.snapshot;
}

Matrix4x4 OffAxisFarPlaneTransform() {
  Matrix4x4 transform;
  // The far-plane proof needs a larger planar target, not a non-uniform
  // tangent transform. Keep all three axes uniform so the same scene remains
  // valid under RT4's authored tangent-frame contract.
  transform.elements[0U] = 3.0F;
  transform.elements[5U] = 3.0F;
  transform.elements[10U] = 3.0F;
  transform.elements[12U] = 20.0F;
  transform.elements[14U] = -16.0F;
  return transform;
}

Matrix4x4 Projection(float aspect_scale) {
  constexpr float near_plane = 0.1F;
  constexpr float far_plane = 20.0F;
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = aspect_scale;
  projection.elements[5U] = 1.5F;
  projection.elements[10U] = far_plane / (near_plane - far_plane);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      near_plane * far_plane / (near_plane - far_plane);
  return projection;
}

RenderFrameRequest MakeFrame(
    std::uint64_t frame_id,
    const std::shared_ptr<const SceneSnapshot> &scene,
    std::uint32_t width = kWidth, std::uint32_t height = kHeight,
    float camera_x = 0.0F) {
  RenderFrameRequest frame;
  frame.frame_id = frame_id;
  frame.scene_snapshot = scene;
  frame.present = false;
  frame.color_format = PixelFormat::RGBA16_FLOAT;
  CameraViewRequest view;
  view.view_id = 1U;
  view.width = width;
  view.height = height;
  view.near_plane = 0.1F;
  view.far_plane = 20.0F;
  view.view_from_render.elements[12U] = -camera_x;
  view.view_from_render.elements[14U] = -3.0F;
  view.previous_view_from_render = view.view_from_render;
  view.clip_from_view = Projection(static_cast<float>(height) /
                                   static_cast<float>(width));
  view.previous_clip_from_view = view.clip_from_view;
  frame.views.push_back(view);
  return frame;
}

NativeRayTracingFrameRequest MakeRayRequest(
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
  Require(static_cast<bool>(output), "could not open N3 binary artifact");
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  Require(static_cast<bool>(output), "could not write N3 binary artifact");
}

void WriteText(const std::string &path, const std::string &text) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(output), "could not open N3 report");
  output << text;
  Require(static_cast<bool>(output), "could not write N3 report");
}

std::string MetricsJson(const ImageMetrics &metrics) {
  std::ostringstream json;
  json << "{\"width\": " << metrics.width
       << ", \"height\": " << metrics.height
       << ", \"format\": \"RGBA16_FLOAT\""
       << ", \"bytes\": " << metrics.bytes
       << ", \"sha256\": \"" << metrics.sha256 << "\""
       << ", \"mean_luminance\": " << std::setprecision(12)
       << metrics.mean_luminance
       << ", \"nontrivial_pixels\": " << metrics.nontrivial_pixels << '}';
  return json.str();
}

std::string SkipReport(const OgreNextMetalRayTracingEvidence &evidence,
                       const RenderOperationResult &initialization,
                       const FileDigest &executable) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_metal_rt_n3.v3\",\n"
         << "  \"status\": \"skip\",\n"
         << "  \"scope\": \"same-device Metal primary-ray hybrid HDR contribution; no GI, reflection, denoising, or material parity claim\",\n"
         << "  \"reason\": \"" << JsonEscape(initialization.detail) << "\",\n"
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \""
         << JsonEscape(ROR_OGRE_NEXT_N3_SOURCE_REPOSITORY) << "\",\n"
         << "    \"ror_ref\": \"" << JsonEscape(ROR_OGRE_NEXT_N3_SOURCE_REF)
         << "\",\n"
         << "    \"ror_commit\": \""
         << JsonEscape(ROR_OGRE_NEXT_N3_SOURCE_COMMIT) << "\",\n"
         << "    \"relevant_source_clean\": "
         << (ROR_OGRE_NEXT_N3_RELEVANT_SOURCE_CLEAN ? "true" : "false")
         << ",\n"
         << "    \"relevant_source_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N3_SOURCE_MANIFEST_SHA256 << "\",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "    \"build_artifact\": \"ror_ogre_next_metal_n3_smoke\",\n"
         << "    \"build_artifact_bytes\": " << executable.bytes << ",\n"
         << "    \"build_artifact_sha256\": \"" << executable.sha256
         << "\"\n  },\n"
         << "  \"device_name\": \"" << JsonEscape(evidence.device_name)
         << "\",\n"
         << "  \"required_apple_gpu_family\": 9,\n"
         << "  \"required_metal_ray_tracing\": true\n}\n";
  return report.str();
}

std::string PassReport(
    const OgreNextMetalRayTracingEvidence &evidence,
    const NativeRayTracingCapabilityReport &capabilities,
    const ImageMetrics &raster, const ImageMetrics &contribution,
    const ImageMetrics &hybrid, const ImageMetrics &second_contribution,
    const ImageMetrics &resized_hybrid, std::uint64_t far_edge_pixels,
    const OgreNextN1TextureAllocationAudit &live_texture_audit,
    const OgreNextN1TextureAllocationAudit &shutdown_texture_audit,
    const FileDigest &executable) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_metal_rt_n3.v3\",\n"
         << "  \"status\": \"pass\",\n"
         << "  \"scope\": \"same-device Metal primary-ray hit contribution composited into exact UI-free Ogre-Next HDR target; no GI, reflection, denoising, multi-bounce, or material parity claim\",\n"
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \""
         << JsonEscape(ROR_OGRE_NEXT_N3_SOURCE_REPOSITORY) << "\",\n"
         << "    \"ror_ref\": \"" << JsonEscape(ROR_OGRE_NEXT_N3_SOURCE_REF)
         << "\",\n"
         << "    \"ror_commit\": \""
         << JsonEscape(ROR_OGRE_NEXT_N3_SOURCE_COMMIT) << "\",\n"
         << "    \"relevant_source_clean\": "
         << (ROR_OGRE_NEXT_N3_RELEVANT_SOURCE_CLEAN ? "true" : "false")
         << ",\n"
         << "    \"relevant_source_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N3_SOURCE_MANIFEST_SHA256 << "\",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "    \"build_artifact\": \"ror_ogre_next_metal_n3_smoke\",\n"
         << "    \"build_artifact_bytes\": " << executable.bytes << ",\n"
         << "    \"build_artifact_sha256\": \"" << executable.sha256
         << "\"\n  },\n"
         << "  \"device\": {\"name\": \""
         << JsonEscape(evidence.device_name)
         << "\", \"same_ogre_device\": true, \"same_ogre_queue\": true, \"apple_family_9\": true},\n"
         << "  \"contract\": {\"image_version\": "
         << evidence.image_export.version
         << ", \"image_generation\": " << evidence.image_export.image.generation
         << ", \"usage\": \"COLOR_ATTACHMENT_SHADER_READ_WRITE_COPY_SOURCE\", \"release_state\": \"GENERAL_READ_WRITE\", \"return_state\": \"GENERAL_READ_WRITE\"},\n"
         << "  \"raster_contract\": {\n"
         << "    \"raster_feature_tier\": \"MODERN_PBR_RT4_V1\",\n"
         << "    \"native_feature_tier\": \"METAL_RAY_TRACING_N3\",\n"
         << "    \"vertex_layout\": \"POSITION_NORMAL_TANGENT_UV0_FLOAT32_48\",\n"
         << "    \"vertex_stride_bytes\": 48,\n"
         << "    \"authored_tangent_uv0\": true,\n"
         << "    \"base_color_texture\": \"RGBA8_UNORM_SRGB\",\n"
         << "    \"directional_light_lux\": 1024,\n"
         << "    \"ray_material_parity_claimed\": false,\n"
         << "    \"texture_allocations\": {\n"
         << "      \"live\": {\"version\": "
         << live_texture_audit.version << ", \"source_textures\": "
         << live_texture_audit.live_source_textures
         << ", \"sampled_rgba\": "
         << live_texture_audit.sampled_rgba_allocations
         << ", \"linear_rgba\": "
         << live_texture_audit.linear_rgba_allocations
         << ", \"roughness_r8\": "
         << live_texture_audit.roughness_r8_allocations
         << ", \"metallic_r8\": "
         << live_texture_audit.metallic_r8_allocations
         << ", \"normal_rg8\": "
         << live_texture_audit.normal_rg8_allocations
         << ", \"creates\": "
         << live_texture_audit.native_allocation_creates
         << ", \"destroys\": "
         << live_texture_audit.native_allocation_destroys
         << ", \"live\": "
         << live_texture_audit.live_native_allocations
         << ", \"exact_usage\": "
         << (live_texture_audit.exact_usage ? "true" : "false") << "},\n"
         << "      \"after_shutdown\": {\"version\": "
         << shutdown_texture_audit.version << ", \"creates\": "
         << shutdown_texture_audit.native_allocation_creates
         << ", \"destroys\": "
         << shutdown_texture_audit.native_allocation_destroys
         << ", \"live\": "
         << shutdown_texture_audit.live_native_allocations
         << ", \"retired_name_lookups\": "
         << shutdown_texture_audit.retired_name_lookups
         << ", \"retired_name_rejections\": "
         << shutdown_texture_audit.retired_name_rejections << "}\n"
         << "    }\n"
         << "  },\n"
         << "  \"raster_only_hdr\": " << MetricsJson(raster) << ",\n"
         << "  \"rt_contribution\": " << MetricsJson(contribution) << ",\n"
         << "  \"hybrid_hdr\": " << MetricsJson(hybrid) << ",\n"
         << "  \"second_view_contribution\": "
         << MetricsJson(second_contribution) << ",\n"
         << "  \"resized_hybrid\": " << MetricsJson(resized_hybrid) << ",\n"
         << "  \"proof\": {\n"
         << "    \"exact_exported_vertex_slice_used\": true,\n"
         << "    \"exact_exported_index_slice_used\": true,\n"
         << "    \"exact_exported_color_image_used\": true,\n"
         << "    \"gpu_composite_not_cpu_postprocess\": true,\n"
         << "    \"contribution_pixels\": "
         << evidence.contribution_pixel_count << ",\n"
         << "    \"hybrid_changes_only_on_contribution\": true,\n"
         << "    \"all_channels_finite\": true,\n"
         << "    \"second_camera_changes_contribution_hash\": true,\n"
         << "    \"camera_mismatch_rejected\": true,\n"
         << "    \"snapshot_transform_mismatch_rejected\": true,\n"
         << "    \"off_axis_far_plane_contribution_pixels\": "
         << far_edge_pixels << ",\n"
         << "    \"off_axis_far_plane_hit_passed\": true,\n"
         << "    \"released_frame_allows_extent_change\": true,\n"
         << "    \"submitted_device_loss_and_timeout_paths_tested\": true,\n"
         << "    \"simultaneous_rt4_n3\": true,\n"
         << "    \"textured_rt4_geometry_rendered\": true,\n"
         << "    \"calibrated_directional_light_applied\": true,\n"
         << "    \"exact_48_byte_vertex_layout_exported\": true,\n"
         << "    \"texture_allocation_audit_exact\": true,\n"
         << "    \"texture_teardown_audit_exact\": true,\n"
         << "    \"view_dependent_output_ready\": "
         << (capabilities.view_dependent_output_ready ? "true" : "false")
         << ",\n"
         << "    \"hybrid_composite_ready\": "
         << (capabilities.hybrid_composite_ready ? "true" : "false")
         << "\n  }\n}\n";
  return report.str();
}

void ProveInjectedObservation(OgreNextMetalN2TestObservation observation,
                              RenderOperationCode expected,
                              const std::string &media_root) {
  OgreNextN1Frontend frontend(
      OgreNextN1Configuration{
          media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1},
      OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3);
  FrontendInitializationRequest initialization;
  initialization.initial_width = kWidth;
  initialization.initial_height = kHeight;
  initialization.maximum_frames_in_flight = 1U;
  initialization.headless = true;
  initialization.vertical_sync = false;
  RequireSuccess(frontend.Initialize(initialization), "fault frontend Initialize");
  RequireSuccess(frontend.SynchronizeAssets(MakeCatalog()),
                 "fault SynchronizeAssets");
  NativeRenderInterop *interop = frontend.GetNativeInterop();
  Require(interop != nullptr, "fault frontend did not export interop");
  OgreNextMetalRayTracingBackend backend;
  RequireSuccess(backend.Initialize(*interop), "fault backend Initialize");
  RequireSuccess(backend.InjectObservationForTesting(observation),
                 "inject native observation");
  RenderFrameRequest frame = MakeFrame(1U, MakeScene(1U, 2U));
  const std::weak_ptr<const SceneSnapshot> frame_snapshot =
      frame.scene_snapshot;
  RenderFrameOutput raster;
  RequireSuccess(frontend.Render(frame, raster), "fault raster frame");
  RenderFrameOutput hybrid;
  const RenderOperationResult render = backend.Render(MakeRayRequest(frame),
                                                       hybrid);
  Require(render.code == expected && hybrid.attachments.empty(),
          "injected native observation did not follow a real N3 submission");
  frame.scene_snapshot.reset();
  const RenderFrameRequest resized =
      MakeFrame(2U, MakeScene(2U, 3U), 80U, 48U, 0.2F);
  RenderFrameOutput blocked;
  Require(frontend.Render(resized, blocked).code ==
              RenderOperationCode::OUTSTANDING_LEASES,
          "submitted N3 fault did not block image replacement/resize");
  Require(frontend.Shutdown(0U).code ==
              RenderOperationCode::OUTSTANDING_LEASES,
          "submitted N3 fault did not block frontend shutdown");
  const RenderOperationResult shutdown = backend.Shutdown(0U);
  Require(shutdown.code == expected,
          "bounded N3 shutdown did not preserve the native fault outcome");
  Require(!backend.evidence().image_request.scene_snapshot &&
              !backend.evidence().image_export.scene_snapshot,
          "fault abandonment retained its exact scene snapshot evidence");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "fault frontend final Shutdown");
  Require(frame_snapshot.expired(),
          "fault teardown retained the submitted world snapshot");
}

std::pair<std::string, int> Run(const Arguments &arguments) {
  const FileDigest executable = HashFile(arguments.executable_path);
  OgreNextN1Frontend frontend(
      OgreNextN1Configuration{
          arguments.media_root,
          OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1},
      OgreNextNativeFeatureTier::METAL_RAY_TRACING_N3);
  FrontendInitializationRequest initialization;
  initialization.initial_width = kWidth;
  initialization.initial_height = kHeight;
  initialization.maximum_frames_in_flight = 1U;
  initialization.headless = true;
  initialization.vertical_sync = false;
  RequireSuccess(frontend.Initialize(initialization), "frontend Initialize");
  RequireSuccess(frontend.SynchronizeAssets(MakeCatalog()),
                 "frontend SynchronizeAssets");
  const OgreNextN1TextureAllocationAudit live_texture_audit =
      frontend.QueryTextureAllocationAudit();
  Require(live_texture_audit.version == 2U &&
              live_texture_audit.live_source_textures == 1U &&
              live_texture_audit.sampled_rgba_allocations == 1U &&
              live_texture_audit.linear_rgba_allocations == 0U &&
              live_texture_audit.roughness_r8_allocations == 0U &&
              live_texture_audit.metallic_r8_allocations == 0U &&
              live_texture_audit.normal_rg8_allocations == 0U &&
              live_texture_audit.native_allocation_creates == 1U &&
              live_texture_audit.native_allocation_destroys == 0U &&
              live_texture_audit.live_native_allocations == 1U &&
              live_texture_audit.exact_usage,
          "simultaneous RT4+N3 texture allocation audit drifted");
  NativeRenderInterop *interop = frontend.GetNativeInterop();
  Require(interop != nullptr, "N3 frontend did not expose interop");
  const NativeInteropCapabilityReport interop_capabilities =
      interop->QueryCapabilities();
  Require(interop_capabilities.exports_color_images &&
              interop_capabilities.supports_read_write_color_images,
          "N3 frontend did not expose the exact image contract");

  OgreNextMetalRayTracingBackend backend;
  const RenderOperationResult initialized = backend.Initialize(*interop);
  if (!initialized && initialized.code == RenderOperationCode::UNSUPPORTED) {
    const OgreNextMetalRayTracingEvidence evidence = backend.evidence();
    RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                   "unsupported frontend Shutdown");
    return {SkipReport(evidence, initialized, executable),
            kCapabilitySkipExitCode};
  }
  RequireSuccess(initialized, "backend Initialize");

  const RenderFrameRequest first = MakeFrame(1U, MakeScene(1U, 2U));
  RenderFrameOutput frontend_raster;
  RequireSuccess(frontend.Render(first, frontend_raster), "first raster");
  RenderFrameRequest camera_mismatch = first;
  camera_mismatch.views.front().view_from_render.elements[12U] += 0.25F;
  RenderFrameOutput rejected_output;
  Require(backend.Render(MakeRayRequest(camera_mismatch), rejected_output).code ==
              RenderOperationCode::RESOURCE_STALE &&
              rejected_output.attachments.empty(),
          "N3 accepted a camera that did not produce the published raster");
  Matrix4x4 mismatched_transform;
  mismatched_transform.elements[12U] = 0.25F;
  RenderFrameRequest transform_mismatch =
      MakeFrame(1U, MakeScene(1U, 2U, mismatched_transform));
  Require(backend.Render(MakeRayRequest(transform_mismatch), rejected_output)
                  .code == RenderOperationCode::RESOURCE_STALE &&
              rejected_output.attachments.empty(),
          "N3 accepted a transformed snapshot alias that did not produce the raster");
  RenderFrameOutput first_hybrid;
  RequireSuccess(backend.Render(MakeRayRequest(first), first_hybrid),
                 "first hybrid");
  const OgreNextMetalRayTracingEvidence first_evidence = backend.evidence();
  Require(first_evidence.image_export.frame_id == first.frame_id &&
              first_evidence.image_export.width == kWidth &&
              first_evidence.image_export.height == kHeight &&
              first_evidence.image_export.format == PixelFormat::RGBA16_FLOAT &&
              first_evidence.image_frame_synchronization
                      .frontend_image_release_state ==
                  NativeImageState::GENERAL_READ_WRITE &&
              first_evidence.image_frame_synchronization
                      .external_image_return_state ==
                  NativeImageState::GENERAL_READ_WRITE,
          "N3 evidence does not identify the exact HDR image handoff");
  Require(first_evidence.geometry_export.positions.stride_bytes ==
              kOgreNextPositionNormalTangentUv0VertexStrideBytes,
          "N3 did not export the exact reviewed 48-byte RT4 vertex layout");
  Require(frontend_raster.attachments.size() == 1U &&
              first_hybrid.attachments.size() == 1U &&
              frontend_raster.attachments.front().bytes ==
                  first_evidence.raster_readback_bytes &&
              first_hybrid.attachments.front().bytes ==
                  first_evidence.hybrid_readback_bytes,
          "N3 GPU readbacks differ from the frontend/backend frame outputs");
  const ImageMetrics raster = Analyze(first_evidence.raster_readback_bytes,
                                      first_evidence.image_export.width,
                                      first_evidence.image_export.height);
  const ImageMetrics contribution = Analyze(
      first_evidence.contribution_readback_bytes,
      first_evidence.image_export.width, first_evidence.image_export.height);
  const ImageMetrics hybrid = Analyze(first_evidence.hybrid_readback_bytes,
                                      first_hybrid.attachments.front().width,
                                      first_hybrid.attachments.front().height);
  VerifyContributionMapping(first_evidence.raster_readback_bytes,
                            first_evidence.contribution_readback_bytes,
                            first_evidence.hybrid_readback_bytes,
                            first_evidence.contribution_pixel_count);

  const RenderFrameRequest second =
      MakeFrame(2U, MakeScene(2U, 3U), kWidth, kHeight, 0.35F);
  RenderFrameOutput second_raster;
  RequireSuccess(frontend.Render(second, second_raster), "second raster");
  RenderFrameOutput second_hybrid_output;
  RequireSuccess(backend.Render(MakeRayRequest(second), second_hybrid_output),
                 "second hybrid");
  const OgreNextMetalRayTracingEvidence second_evidence = backend.evidence();
  const ImageMetrics second_contribution = Analyze(
      second_evidence.contribution_readback_bytes,
      second_evidence.image_export.width, second_evidence.image_export.height);
  Require(second_contribution.sha256 != contribution.sha256,
          "changing the camera did not change the traced contribution");

  const RenderFrameRequest resized =
      MakeFrame(3U, MakeScene(3U, 4U), 80U, 48U, -0.15F);
  RenderFrameOutput resized_raster;
  RequireSuccess(frontend.Render(resized, resized_raster), "resized raster");
  RenderFrameOutput resized_output;
  RequireSuccess(backend.Render(MakeRayRequest(resized), resized_output),
                 "resized hybrid");
  const OgreNextMetalRayTracingEvidence resized_evidence = backend.evidence();
  const ImageMetrics resized_hybrid = Analyze(
      resized_evidence.hybrid_readback_bytes,
      resized_evidence.image_export.width, resized_evidence.image_export.height);
  Require(resized_hybrid.width == 80U && resized_hybrid.height == 48U,
          "released N3 frame did not accept a new exact extent");

  RenderFrameRequest far_edge = MakeFrame(
      4U, MakeScene(4U, 5U, OffAxisFarPlaneTransform()));
  const std::weak_ptr<const SceneSnapshot> far_edge_snapshot =
      far_edge.scene_snapshot;
  RenderFrameOutput far_edge_raster;
  RequireSuccess(frontend.Render(far_edge, far_edge_raster),
                 "off-axis far-plane raster");
  RenderFrameOutput far_edge_output;
  RequireSuccess(backend.Render(MakeRayRequest(far_edge), far_edge_output),
                 "off-axis far-plane hybrid");
  OgreNextMetalRayTracingEvidence far_edge_evidence = backend.evidence();
  Require(far_edge_evidence.contribution_pixel_count > 0U,
          "off-axis geometry inside the far plane was truncated from primary rays");
  const std::uint64_t far_edge_contribution_pixels =
      far_edge_evidence.contribution_pixel_count;
  far_edge_evidence.image_request.scene_snapshot.reset();
  far_edge_evidence.image_export.scene_snapshot.reset();
  far_edge.scene_snapshot.reset();

  const NativeRayTracingCapabilityReport capabilities =
      backend.QueryCapabilities();
  Require(capabilities.dispatch_readback_probe_passed &&
              capabilities.geometry_interop_ready &&
              capabilities.view_dependent_output_ready &&
              capabilities.hybrid_composite_ready,
          "N3 readiness was published before all native evidence passed");
  RequireSuccess(backend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "backend Shutdown");
  Require(!backend.evidence().image_request.scene_snapshot &&
              !backend.evidence().image_export.scene_snapshot,
          "successful shutdown retained its exact scene snapshot evidence");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "frontend Shutdown");
  const OgreNextN1TextureAllocationAudit shutdown_texture_audit =
      frontend.QueryTextureAllocationAudit();
  Require(shutdown_texture_audit.version == 2U &&
              shutdown_texture_audit.live_source_textures == 0U &&
              shutdown_texture_audit.sampled_rgba_allocations == 0U &&
              shutdown_texture_audit.linear_rgba_allocations == 0U &&
              shutdown_texture_audit.roughness_r8_allocations == 0U &&
              shutdown_texture_audit.metallic_r8_allocations == 0U &&
              shutdown_texture_audit.normal_rg8_allocations == 0U &&
              shutdown_texture_audit.native_allocation_creates == 1U &&
              shutdown_texture_audit.native_allocation_destroys == 1U &&
              shutdown_texture_audit.live_native_allocations == 0U &&
              shutdown_texture_audit.retired_name_lookups == 1U &&
              shutdown_texture_audit.retired_name_rejections == 1U,
          "simultaneous RT4+N3 texture teardown audit drifted");
  Require(far_edge_snapshot.expired(),
          "successful teardown retained the final world snapshot");

  ProveInjectedObservation(OgreNextMetalN2TestObservation::DEVICE_LOST,
                           RenderOperationCode::DEVICE_LOST,
                           arguments.media_root);
  ProveInjectedObservation(OgreNextMetalN2TestObservation::TIMEOUT,
                           RenderOperationCode::TIMEOUT,
                           arguments.media_root);

  WriteBinary(arguments.raster_path,
              first_evidence.raster_readback_bytes);
  WriteBinary(arguments.contribution_path,
              first_evidence.contribution_readback_bytes);
  WriteBinary(arguments.hybrid_path,
              first_evidence.hybrid_readback_bytes);
  return {PassReport(first_evidence, capabilities, raster, contribution,
                     hybrid, second_contribution, resized_hybrid,
                     far_edge_contribution_pixels, live_texture_audit,
                     shutdown_texture_audit, executable),
          EXIT_SUCCESS};
}

} // namespace

int main(int argc, char **argv) {
  std::string report_path;
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    report_path = arguments.report_path;
    const auto result = Run(arguments);
    WriteText(report_path, result.first);
    std::cout << result.first;
    return result.second;
  } catch (const std::exception &error) {
    std::ostringstream report;
    report << "{\n  \"schema\": \"ror.ogre_next_metal_rt_n3.v3\",\n"
           << "  \"status\": \"fail\",\n"
           << "  \"error\": \"" << JsonEscape(error.what()) << "\"\n}\n";
    try {
      WriteText(report_path, report.str());
    } catch (...) {
    }
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
