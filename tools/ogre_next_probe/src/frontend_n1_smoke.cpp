/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Frontend.h"
#include "OgreNextN1Policy.h"
#include "ror_ogre_next_n1_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

constexpr std::uint32_t kWidth = 192U;
constexpr std::uint32_t kHeight = 128U;
constexpr std::uint64_t kRegistryId = UINT64_C(0x4E315F534D4F4B45);

struct Arguments {
  std::string media_root;
  std::string image_path;
  std::string report_path;
  bool modern_pbr = false;
};

struct Metrics {
  std::size_t distinct_rgb = 0U;
  std::size_t non_background_pixels = 0U;
  float minimum_luminance = std::numeric_limits<float>::infinity();
  float maximum_luminance = -std::numeric_limits<float>::infinity();
  std::uint64_t fnv1a64 = UINT64_C(14695981039346656037);
  std::vector<std::uint8_t> rgb;
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
  if (!result.ok()) {
    Fail(operation + " failed: " + result.detail);
  }
}

Arguments ParseArguments(int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--media-root" && index + 1 < argc) {
      arguments.media_root = argv[++index];
    } else if (option == "--output" && index + 1 < argc) {
      arguments.image_path = argv[++index];
    } else if (option == "--report" && index + 1 < argc) {
      arguments.report_path = argv[++index];
    } else if (option == "--modern-pbr") {
      arguments.modern_pbr = true;
    } else {
      Fail("usage: ror_ogre_next_frontend_n1_smoke --media-root ABSOLUTE_PATH [--modern-pbr] [--output FRAME.ppm] [--report REPORT.json]");
    }
  }
  if (arguments.media_root.empty()) {
    Fail("--media-root is required for the relocatable N1 frontend");
  }
  return arguments;
}

RenderAssetId AssetId(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x4E315F4153534554), low);
}

RenderAssetReference AssetRef(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, AssetId(low), 1U);
}

MeshResourceDescriptor MakeMesh(bool modern_pbr = false) {
  MeshResourceDescriptor mesh;
  mesh.debug_name = "N1 native v2 VAO smoke triangle";
  mesh.index_format = MeshIndexFormat::UINT16;
  mesh.local_bounds.minimum = {-1.15F, -0.85F, 0.0F};
  mesh.local_bounds.maximum = {1.15F, 0.95F, 0.0F};
  mesh.positions = {
      {-1.15F, -0.85F, 0.0F},
      {1.15F, -0.85F, 0.0F},
      {0.0F, 0.95F, 0.0F},
  };
  mesh.normals.assign(mesh.positions.size(), Float3{0.0F, 0.0F, 1.0F});
  if (modern_pbr) {
    mesh.debug_name = "RT4/V1 authored tangent UV0 smoke triangle";
    mesh.tangents.assign(mesh.positions.size(),
                         Float4{1.0F, 0.0F, 0.0F, 1.0F});
    mesh.texture_coordinates_0 = {
        {0.0F, 1.0F},
        {1.0F, 1.0F},
        {0.5F, 0.0F},
    };
  }
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

MaterialDescriptor MakeMaterial(bool modern_pbr = false) {
  MaterialDescriptor material;
  material.debug_name = "N1 texture-free emissive metallic-roughness PBS";
  material.base_color_factor = {0.05F, 0.32F, 0.92F, 1.0F};
  material.metallic_factor = 0.2F;
  material.roughness_factor = 0.28F;
  material.double_sided = true;
  material.emissive_factor = {0.78F, 0.12F, 0.035F};
  material.emissive_strength = 6.0F;
  if (modern_pbr) {
    material.debug_name = "RT4/V1 texture-backed metallic-roughness PBS";
    material.base_color_factor = {0.7F, 0.8F, 0.9F, 1.0F};
    material.metallic_factor = 0.85F;
    material.roughness_factor = 0.65F;
    material.emissive_factor = {1.0F, 0.7F, 0.4F};
    material.emissive_strength = 6.0F;
    material.base_color_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 3U);
    material.base_color_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U);
    material.metallic_roughness_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 4U);
    material.metallic_roughness_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U);
    material.emissive_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 5U);
    material.emissive_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U);
  }
  return material;
}

TextureResourceDescriptor MakeTexture(TextureColorSpace color_space,
                                      std::vector<std::uint8_t> rgba) {
  Require(rgba.size() == 16U, "RT4/V1 texture fixture is not 2x2 RGBA8");
  TextureResourceDescriptor texture;
  texture.debug_name = "RT4/V1 padded-row 2x2 texture";
  texture.color_space = color_space;
  texture.width = 2U;
  texture.height = 2U;
  TextureMipLevelDescriptor mip;
  mip.width = 2U;
  mip.height = 2U;
  mip.row_pitch_bytes = 12U;
  mip.layer_pitch_bytes = 24U;
  mip.bytes.assign(24U, 0xCDU);
  std::memcpy(mip.bytes.data(), rgba.data(), 8U);
  std::memcpy(mip.bytes.data() + 12U, rgba.data() + 8U, 8U);
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

RenderAssetDelta MakeCatalog(bool modern_pbr = false) {
  RenderAssetDelta delta;
  delta.registry_id = kRegistryId;
  delta.sequence = 1U;
  delta.full_snapshot = true;

  RenderAssetMutation mesh;
  mesh.asset = AssetRef(RenderAssetKind::MESH, 1U);
  mesh.payload = MakeMesh(modern_pbr);
  delta.mutations.push_back(std::move(mesh));

  RenderAssetMutation material;
  material.asset = AssetRef(RenderAssetKind::MATERIAL, 2U);
  material.payload = MakeMaterial(modern_pbr);
  delta.mutations.push_back(std::move(material));
  if (modern_pbr) {
    RenderAssetMutation base_color;
    base_color.asset = AssetRef(RenderAssetKind::TEXTURE, 3U);
    base_color.payload = MakeTexture(
        TextureColorSpace::SRGB,
        {255U, 28U, 12U, 255U, 18U, 220U, 42U, 255U,
         24U, 42U, 255U, 255U, 255U, 190U, 30U, 255U});
    delta.mutations.push_back(std::move(base_color));

    RenderAssetMutation metallic_roughness;
    metallic_roughness.asset = AssetRef(RenderAssetKind::TEXTURE, 4U);
    metallic_roughness.payload = MakeTexture(
        TextureColorSpace::LINEAR,
        {255U, 40U, 230U, 255U, 255U, 96U, 180U, 255U,
         255U, 170U, 80U, 255U, 255U, 220U, 25U, 255U});
    delta.mutations.push_back(std::move(metallic_roughness));

    RenderAssetMutation emissive;
    emissive.asset = AssetRef(RenderAssetKind::TEXTURE, 5U);
    emissive.payload = MakeTexture(
        TextureColorSpace::SRGB,
        {255U, 96U, 12U, 255U, 18U, 255U, 80U, 255U,
         20U, 90U, 255U, 255U, 255U, 235U, 42U, 255U});
    delta.mutations.push_back(std::move(emissive));

    SamplerResourceDescriptor sampler_descriptor;
    sampler_descriptor.debug_name = "RT4/V1 linear mirror-edge sampler";
    sampler_descriptor.address_u = SamplerAddressMode::MIRRORED_REPEAT;
    sampler_descriptor.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
    sampler_descriptor.address_w = SamplerAddressMode::REPEAT;
    sampler_descriptor.maximum_lod = 0.0F;
    RenderAssetMutation sampler;
    sampler.asset = AssetRef(RenderAssetKind::SAMPLER, 6U);
    sampler.payload = sampler_descriptor;
    delta.mutations.push_back(std::move(sampler));

    TextureResourceDescriptor unreferenced_texture;
    unreferenced_texture.debug_name =
        "shared-catalog unreferenced R8 texture";
    unreferenced_texture.format = TextureResourceFormat::R8_UNORM;
    unreferenced_texture.width = 1U;
    unreferenced_texture.height = 1U;
    TextureMipLevelDescriptor unreferenced_mip;
    unreferenced_mip.width = 1U;
    unreferenced_mip.height = 1U;
    unreferenced_mip.row_pitch_bytes = 1U;
    unreferenced_mip.layer_pitch_bytes = 1U;
    unreferenced_mip.bytes = {127U};
    unreferenced_texture.mip_levels.push_back(std::move(unreferenced_mip));
    RenderAssetMutation unreferenced_texture_mutation;
    unreferenced_texture_mutation.asset =
        AssetRef(RenderAssetKind::TEXTURE, 7U);
    unreferenced_texture_mutation.payload = std::move(unreferenced_texture);
    delta.mutations.push_back(std::move(unreferenced_texture_mutation));

    SamplerResourceDescriptor unreferenced_sampler_descriptor;
    unreferenced_sampler_descriptor.debug_name =
        "shared-catalog unreferenced border sampler";
    unreferenced_sampler_descriptor.address_u =
        SamplerAddressMode::CLAMP_TO_BORDER;
    RenderAssetMutation unreferenced_sampler;
    unreferenced_sampler.asset = AssetRef(RenderAssetKind::SAMPLER, 8U);
    unreferenced_sampler.payload = unreferenced_sampler_descriptor;
    delta.mutations.push_back(std::move(unreferenced_sampler));
  }
  return delta;
}

std::shared_ptr<const SceneSnapshot> MakeScene(std::uint64_t snapshot_id,
                                               bool shifted = false,
                                               bool modern_pbr = false) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds = static_cast<double>(snapshot_id) / 48.0;
  descriptor.environment.ambient_radiance = {0.03F, 0.04F, 0.055F};

  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = AssetRef(RenderAssetKind::MESH, 1U);
  instance.material = AssetRef(RenderAssetKind::MATERIAL, 2U);
  if (shifted) {
    instance.render_from_object.elements[12U] = 0.15F;
    instance.previous_render_from_object = instance.render_from_object;
  }
  instance.local_bounds = MakeMesh(modern_pbr).local_bounds;
  descriptor.mesh_instances.push_back(instance);

  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  if (!result) {
    Fail("could not create N1 smoke scene: " + result.validation.field +
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
    const std::shared_ptr<const SceneSnapshot> &scene,
    PixelFormat format) {
  RenderFrameRequest request;
  request.frame_id = frame_id;
  request.scene_snapshot = scene;
  request.present = false;
  request.color_format = format;
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

float HalfToFloat(std::uint16_t half) {
  const bool negative = (half & 0x8000U) != 0U;
  const std::uint16_t exponent = (half >> 10U) & 0x1FU;
  const std::uint16_t mantissa = half & 0x03FFU;
  float value = 0.0F;
  if (exponent == 0U) {
    value = mantissa == 0U
                ? 0.0F
                : std::ldexp(static_cast<float>(mantissa), -24);
  } else if (exponent == 0x1FU) {
    value = mantissa == 0U
                ? std::numeric_limits<float>::infinity()
                : std::numeric_limits<float>::quiet_NaN();
  } else {
    value = std::ldexp(1.0F + static_cast<float>(mantissa) / 1024.0F,
                       static_cast<int>(exponent) - 15);
  }
  return negative ? -value : value;
}

std::uint8_t Quantize(float value) {
  const float clamped = std::max(0.0F, std::min(1.0F, value));
  return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
}

void Hash(std::uint64_t &hash, std::uint8_t value) {
  hash ^= value;
  hash *= UINT64_C(1099511628211);
}

std::string HexHash(std::uint64_t hash);

void Accumulate(Metrics &metrics, std::map<std::uint32_t, std::size_t> &runs,
                float red, float green, float blue) {
  const std::uint8_t r = Quantize(red);
  const std::uint8_t g = Quantize(green);
  const std::uint8_t b = Quantize(blue);
  Hash(metrics.fnv1a64, r);
  Hash(metrics.fnv1a64, g);
  Hash(metrics.fnv1a64, b);
  const std::uint32_t packed = (static_cast<std::uint32_t>(r) << 16U) |
                               (static_cast<std::uint32_t>(g) << 8U) |
                               static_cast<std::uint32_t>(b);
  ++runs[packed];
  const float luminance = 0.2126F * red + 0.7152F * green + 0.0722F * blue;
  metrics.minimum_luminance = std::min(metrics.minimum_luminance, luminance);
  metrics.maximum_luminance = std::max(metrics.maximum_luminance, luminance);
}

void FinishMetrics(Metrics &metrics,
                   const std::map<std::uint32_t, std::size_t> &runs) {
  metrics.distinct_rgb = runs.size();
  std::size_t largest_run = 0U;
  for (const auto &run : runs) {
    largest_run = std::max(largest_run, run.second);
  }
  metrics.non_background_pixels =
      static_cast<std::size_t>(kWidth) * kHeight - largest_run;
}

const FrameAttachment &RequireAttachment(const RenderFrameOutput &output,
                                         PixelFormat format) {
  Require(output.status == RenderFrameStatus::RENDERED && !output.presented,
          "N1 returned a non-rendered or presented frame");
  Require(output.attachments.size() == 1U,
          "N1 did not return exactly one color attachment");
  const FrameAttachment &attachment = output.attachments.front();
  Require(attachment.output == FrameOutputMask::COLOR &&
              attachment.format == format && attachment.width == kWidth &&
              attachment.height == kHeight &&
              !attachment.gpu_resource.valid(),
          "N1 attachment metadata does not match the CPU-only request");
  return attachment;
}

Metrics InspectHdr(const RenderFrameOutput &output) {
  const FrameAttachment &attachment =
      RequireAttachment(output, PixelFormat::RGBA16_FLOAT);
  Require(attachment.row_pitch_bytes == static_cast<std::uint64_t>(kWidth) * 8U,
          "HDR readback is not tightly packed");
  Require(attachment.bytes.size() ==
              static_cast<std::size_t>(attachment.row_pitch_bytes) * kHeight,
          "HDR readback byte count is incomplete");
  Metrics metrics;
  std::map<std::uint32_t, std::size_t> runs;
  for (std::size_t pixel = 0U;
       pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
    float channels[4U]{};
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
      std::uint16_t half = 0U;
      std::memcpy(&half, attachment.bytes.data() + pixel * 8U + channel * 2U,
                  sizeof(half));
      channels[channel] = HalfToFloat(half);
      Require(std::isfinite(channels[channel]),
              "HDR GPU readback contains a non-finite half-float");
    }
    Require(channels[3U] >= 0.99F && channels[3U] <= 1.01F,
            "HDR GPU readback alpha is not straight opaque alpha");
    Accumulate(metrics, runs, channels[0U], channels[1U], channels[2U]);
  }
  FinishMetrics(metrics, runs);
  if (metrics.distinct_rgb < 2U || metrics.non_background_pixels < 512U) {
    std::ostringstream detail;
    detail << "HDR readback does not prove scene geometry over the clear color"
           << " (distinct=" << metrics.distinct_rgb
           << ", foreground=" << metrics.non_background_pixels
           << ", min=" << metrics.minimum_luminance
           << ", max=" << metrics.maximum_luminance << ')';
    Fail(detail.str());
  }
  if (metrics.maximum_luminance <= 1.05F) {
    std::ostringstream detail;
    detail << "RGBA16_FLOAT readback did not preserve scene-referred HDR energy"
           << " (distinct=" << metrics.distinct_rgb
           << ", foreground=" << metrics.non_background_pixels
           << ", min=" << metrics.minimum_luminance
           << ", max=" << metrics.maximum_luminance
           << ", hash=" << HexHash(metrics.fnv1a64) << ')';
    Fail(detail.str());
  }
  return metrics;
}

Metrics InspectSdr(const RenderFrameOutput &output) {
  const FrameAttachment &attachment =
      RequireAttachment(output, PixelFormat::RGBA8_SRGB);
  Require(attachment.row_pitch_bytes == static_cast<std::uint64_t>(kWidth) * 4U,
          "SDR readback is not tightly packed");
  Require(attachment.bytes.size() ==
              static_cast<std::size_t>(attachment.row_pitch_bytes) * kHeight,
          "SDR readback byte count is incomplete");
  Metrics metrics;
  metrics.rgb.reserve(static_cast<std::size_t>(kWidth) * kHeight * 3U);
  std::map<std::uint32_t, std::size_t> runs;
  for (std::size_t pixel = 0U;
       pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
    const std::uint8_t *rgba = attachment.bytes.data() + pixel * 4U;
    Require(rgba[3U] >= 250U, "SDR GPU readback alpha is not opaque");
    metrics.rgb.push_back(rgba[0U]);
    metrics.rgb.push_back(rgba[1U]);
    metrics.rgb.push_back(rgba[2U]);
    Accumulate(metrics, runs, static_cast<float>(rgba[0U]) / 255.0F,
               static_cast<float>(rgba[1U]) / 255.0F,
               static_cast<float>(rgba[2U]) / 255.0F);
  }
  FinishMetrics(metrics, runs);
  Require(metrics.distinct_rgb >= 2U && metrics.non_background_pixels >= 512U,
          "SDR readback does not prove scene geometry over the clear color");
  Require(metrics.maximum_luminance - metrics.minimum_luminance > 0.05F,
          "SDR readback has no meaningful foreground/background contrast");
  return metrics;
}

void WriteText(const std::string &path, const std::string &text) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open output: " + path);
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!output) {
    Fail("could not write output: " + path);
  }
}

void WritePpm(const std::string &path, const Metrics &metrics) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open frame output: " + path);
  }
  output << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
  output.write(reinterpret_cast<const char *>(metrics.rgb.data()),
               static_cast<std::streamsize>(metrics.rgb.size()));
  if (!output) {
    Fail("could not write frame output: " + path);
  }
}

std::string HexHash(std::uint64_t hash) {
  std::ostringstream value;
  value << std::hex << std::setfill('0') << std::setw(16) << hash;
  return value.str();
}

std::string MakeReport(const Metrics &hdr, const Metrics &sdr,
                       bool modern_pbr) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \""
         << (modern_pbr ? "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v1"
                        : "ror.ogre_next_frontend_n1_smoke.v1")
         << "\",\n"
         << "  \"status\": \"pass\",\n"
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \""
         << ROR_OGRE_NEXT_N1_ROR_REPOSITORY << "\",\n"
         << "    \"ror_ref\": \"" << ROR_OGRE_NEXT_N1_ROR_REF
         << "\",\n"
         << "    \"ror_commit\": \"" << ROR_OGRE_NEXT_N1_ROR_COMMIT
         << "\",\n"
         << "    \"ror_relevant_source_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N1_ROR_SOURCE_MANIFEST_SHA256 << "\",\n"
         << "    \"ror_relevant_source_manifest_file_count\": "
         << ROR_OGRE_NEXT_N1_ROR_SOURCE_MANIFEST_FILE_COUNT << ",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "    \"ogre_next_archive_sha256\": \""
         << ROR_OGRE_NEXT_N1_OGRE_ARCHIVE_SHA256 << "\",\n"
         << "    \"shader_media_root\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_ROOT << "\",\n"
         << "    \"shader_media_license_expression\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_LICENSE_EXPRESSION << "\",\n"
         << "    \"shader_media_notice_path\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_NOTICE_PATH << "\",\n"
         << "    \"shader_media_notice_sha256\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_NOTICE_SHA256 << "\",\n"
         << "    \"shader_media_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_MANIFEST_SHA256 << "\",\n"
         << "    \"shader_media_manifest_file_count\": "
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_MANIFEST_FILE_COUNT << "\n"
         << "  },\n"
         << "  \"platform_policy\": \""
         << ROR_OGRE_NEXT_N1_PLATFORM_POLICY << "\",\n"
         << "  \"renderer\": \"" << ROR_OGRE_NEXT_N1_RENDERER_NAME
         << "\",\n"
         << "  \"adapter\": {\n"
         << "    \"frontend_version\": \"" << ROR_OGRE_NEXT_N1_VERSION
         << "\",\n"
         << "    \"native_mesh_path\": \"Ogre v2 Mesh plus immutable VertexArrayObject\",\n"
         << "    \"material_path\": \"HLMS PBS metallic-roughness\",\n"
         << "    \"brdf\": \"PbsBrdf::Default height-correlated GGX\",\n"
         << "    \"pbr_datablock_readback_verified\": true,\n";
  if (modern_pbr) {
    report
        << "    \"raster_feature_tier\": \"MODERN_PBR_RT4_V1\",\n"
        << "    \"vertex_layout\": \"position_normal_tangent_uv0\",\n"
        << "    \"base_color_upload\": \"RGBA8_UNORM_SRGB\",\n"
        << "    \"metallic_roughness_upload\": \"linear_G_to_R8_roughness_B_to_R8_metallic\",\n"
        << "    \"emissive_upload\": \"RGBA8_UNORM_SRGB\",\n"
        << "    \"padded_source_rows_verified\": true,\n"
        << "    \"portable_sampler_mapping_verified\": true,\n"
        << "    \"normal_texture_admitted\": false,\n"
        << "    \"normal_texture_blocker\": \"pinned_PBS_reconstructs_positive_Z_from_RG\",\n"
        << "    \"occlusion_texture_admitted\": false,\n";
  }
  report
         << "    \"runtime_media_root\": \"explicit_absolute\",\n"
         << "    \"package_media_relative_path\": \""
         << ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE << "\",\n"
         << "    \"relocated_executable\": true,\n"
         << "    \"compositor2\": true,\n"
         << "    \"ui_included\": false,\n"
         << "    \"cpu_readback_completed\": true,\n"
         << "    \"analytic_lights_calibrated\": false,\n"
         << "    \"constant_environment_only\": true,\n"
         << "    \"native_interop\": false,\n"
         << "    \"ray_tracing\": false\n"
         << "  },\n"
         << "  \"catalog\": {\n"
         << "    \"registry_id\": " << kRegistryId << ",\n"
         << "    \"sequence\": 1,\n";
  if (modern_pbr) {
    report << "    \"referenced_texture_count\": 3,\n"
           << "    \"referenced_sampler_count\": 1,\n"
           << "    \"unreferenced_assets_not_uploaded\": true,\n";
  }
  report
         << "    \"transactional_replay_after_restart\": true\n"
         << "  },\n"
         << "  \"hdr\": {\n"
         << "    \"format\": \"RGBA16_FLOAT\",\n"
         << "    \"width\": " << kWidth << ",\n"
         << "    \"height\": " << kHeight << ",\n"
         << "    \"distinct_rgb8_values\": " << hdr.distinct_rgb << ",\n"
         << "    \"non_background_pixels\": "
         << hdr.non_background_pixels << ",\n"
         << "    \"minimum_luminance\": " << std::setprecision(9)
         << hdr.minimum_luminance << ",\n"
         << "    \"maximum_luminance\": " << hdr.maximum_luminance << ",\n"
         << "    \"rgb8_fnv1a64\": \"" << HexHash(hdr.fnv1a64)
         << "\"\n"
         << "  },\n"
         << "  \"sdr\": {\n"
         << "    \"format\": \"RGBA8_SRGB\",\n"
         << "    \"width\": " << kWidth << ",\n"
         << "    \"height\": " << kHeight << ",\n"
         << "    \"distinct_rgb8_values\": " << sdr.distinct_rgb << ",\n"
         << "    \"non_background_pixels\": "
         << sdr.non_background_pixels << ",\n"
         << "    \"minimum_luminance\": " << sdr.minimum_luminance << ",\n"
         << "    \"maximum_luminance\": " << sdr.maximum_luminance << ",\n"
         << "    \"rgb8_fnv1a64\": \"" << HexHash(sdr.fnv1a64)
         << "\"\n"
         << "  },\n"
         << "  \"lifecycle\": {\n"
         << "    \"unsupported_depth_failed_before_submission\": true,\n"
         << "    \"double_sided_pbs_readback\": true,\n"
         << "    \"lifetime_snapshot_identity_replay\": true,\n"
         << "    \"lifetime_completed_frame_queries\": true,\n"
         << "    \"process_global_root_exclusion\": true,\n"
         << "    \"shutdown_reinitialize_render_shutdown\": true\n"
         << "  }\n"
         << "}\n";
  return report.str();
}

FrontendInitializationRequest Initialization() {
  FrontendInitializationRequest initialization;
  initialization.initial_width = kWidth;
  initialization.initial_height = kHeight;
  initialization.maximum_frames_in_flight = 1U;
  initialization.headless = true;
  initialization.vertical_sync = false;
  return initialization;
}

void InitializeAndSync(OgreNextN1Frontend &frontend,
                       const RenderAssetDelta &catalog) {
  const FrontendInitializationRequest initialization = Initialization();
  RequireSuccess(frontend.Initialize(initialization), "Initialize");
  RequireSuccess(frontend.SynchronizeAssets(catalog), "SynchronizeAssets");
}

std::pair<Metrics, Metrics> RunSmoke(const std::string &media_root,
                                    bool modern_pbr) {
  const RenderAssetDelta catalog = MakeCatalog(modern_pbr);
  const auto scene_one = MakeScene(1U, false, modern_pbr);
  const auto scene_two = MakeScene(2U, true, modern_pbr);
  const OgreNextRasterFeatureTier raster_feature_tier =
      modern_pbr ? OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1
                 : OgreNextRasterFeatureTier::STATIC_PBR_N1;
  OgreNextN1Frontend relative_media(
      OgreNextN1Configuration{"relative/shader/media", raster_feature_tier});
  Require(relative_media.Initialize(Initialization()).code ==
              RenderOperationCode::INVALID_ARGUMENT,
          "relative shader media root did not fail closed");
  OgreNextN1Frontend missing_media(
      OgreNextN1Configuration{media_root + "/missing", raster_feature_tier});
  Require(missing_media.Initialize(Initialization()).code ==
              RenderOperationCode::INVALID_ARGUMENT,
          "missing shader media root did not fail closed");

  if (modern_pbr) {
    OgreNextN1Frontend legacy_rejection(OgreNextN1Configuration{media_root});
    RequireSuccess(legacy_rejection.Initialize(Initialization()),
                   "legacy rejection Initialize");
    Require(legacy_rejection.SynchronizeAssets(catalog).code ==
                RenderOperationCode::UNSUPPORTED,
            "default N1 runtime silently enabled the opt-in RT4/V1 catalog");
    RequireSuccess(
        legacy_rejection.Shutdown(kInfiniteRenderTimeoutNanoseconds),
        "legacy rejection Shutdown");
  }

  OgreNextN1Frontend frontend(
      OgreNextN1Configuration{media_root, raster_feature_tier});

  const FrontendCapabilityReport capabilities = frontend.QueryCapabilities();
  Require(capabilities.frontend_kind == RendererFrontendKind::OGRE_NEXT &&
              capabilities.supported_outputs == FrameOutputMask::COLOR &&
              capabilities.supports_hdr_output &&
              capabilities.maximum_views == 1U &&
              capabilities.maximum_frames_in_flight == 1U &&
              capabilities.maximum_texture_dimension_2d ==
                  kOgreNextN1ConservativeMaximumTextureDimension &&
              capabilities.native_api == NativeGraphicsApi::NONE &&
              !capabilities.supports_compute &&
              !capabilities.supports_dynamic_mesh_updates &&
              !capabilities.supports_particle_events &&
              !capabilities.supports_native_interop &&
              !capabilities.supports_native_ray_tracing_api,
          "N1 capability report enabled an unproved feature");
  Require(frontend.GetNativeInterop() == nullptr,
          "N1 unexpectedly exported native interop");

  InitializeAndSync(frontend, catalog);
  OgreNextN1Frontend concurrent(
      OgreNextN1Configuration{media_root, raster_feature_tier});
  const RenderOperationResult concurrent_result =
      concurrent.Initialize(Initialization());
  Require(concurrent_result.code == RenderOperationCode::BACKEND_FAILURE &&
              concurrent_result.detail.find("process-global Root") !=
                  std::string::npos,
          "a second simultaneous frontend escaped Ogre Root ownership");
  const FrontendCapabilityReport initialized_capabilities =
      frontend.QueryCapabilities();
  Require(initialized_capabilities.maximum_texture_dimension_2d >= kWidth &&
              initialized_capabilities.maximum_texture_dimension_2d >=
                  kHeight,
          "initialized N1 frontend did not publish its real device extent");
  RenderFrameRequest invalid =
      MakeFrame(1U, scene_one, PixelFormat::RGBA16_FLOAT);
  invalid.requested_outputs =
      FrameOutputMask::COLOR | FrameOutputMask::DEPTH;
  RenderFrameOutput untouched;
  untouched.frame_id = 777U;
  const RenderOperationResult invalid_result = frontend.Render(invalid, untouched);
  Require(invalid_result.code == RenderOperationCode::UNSUPPORTED &&
              untouched.frame_id == 777U && !frontend.IsFrameComplete(1U),
          "unsupported depth request mutated output or consumed frame identity");

  RenderFrameOutput hdr_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, scene_one, PixelFormat::RGBA16_FLOAT),
                     hdr_output),
                 "HDR Render");
  Require(frontend.IsFrameComplete(1U),
          "synchronous HDR frame was not complete on return");
  RequireSuccess(frontend.WaitForFrame(1U, 0U), "WaitForFrame(HDR)");
  const Metrics hdr = InspectHdr(hdr_output);

  RenderFrameOutput sdr_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(2U, scene_one, PixelFormat::RGBA8_SRGB),
                     sdr_output),
                 "SDR Render");
  const Metrics sdr = InspectSdr(sdr_output);

  RenderFrameOutput newer_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(3U, scene_two, PixelFormat::RGBA8_SRGB),
                     newer_output),
                 "new snapshot Render");
  static_cast<void>(InspectSdr(newer_output));
  RenderFrameOutput old_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(4U, scene_one, PixelFormat::RGBA8_SRGB),
                     old_output),
                 "older snapshot replay Render");
  static_cast<void>(InspectSdr(old_output));
  Require(frontend.IsFrameComplete(1U) && frontend.IsFrameComplete(4U),
          "successful N1 frame fell out of lifetime completion history");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "first Shutdown");

  InitializeAndSync(concurrent, catalog);
  RequireSuccess(concurrent.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "post-owner-release concurrent Shutdown");

  InitializeAndSync(frontend, catalog);
  RenderFrameOutput recovered_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, scene_one, PixelFormat::RGBA8_SRGB),
                     recovered_output),
                 "post-reinitialize Render");
  static_cast<void>(InspectSdr(recovered_output));
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "recovery Shutdown");
  return {hdr, sdr};
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    const auto metrics = RunSmoke(arguments.media_root, arguments.modern_pbr);
    WritePpm(arguments.image_path, metrics.second);
    const std::string report =
        MakeReport(metrics.first, metrics.second, arguments.modern_pbr);
    WriteText(arguments.report_path, report);
    std::cout << report;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next N1 frontend smoke failed: " << error.what() << '\n';
    return 1;
  }
}
