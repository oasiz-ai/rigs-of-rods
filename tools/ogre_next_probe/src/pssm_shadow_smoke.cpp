/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Frontend.h"
#include "OgreNextPssmShadowPolicy.h"
#include "ror_ogre_next_n1_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace RoR::Render;

constexpr std::uint32_t kWidth = 192U;
constexpr std::uint32_t kHeight = 128U;
constexpr std::uint64_t kRegistryId = UINT64_C(0x5053534D5F534D4B);
constexpr int kUnsupportedExitCode = 77;

struct Arguments final {
  std::string media_root;
  std::string report_path;
  std::string evidence_path;
};

struct ImagePair final {
  std::vector<std::uint8_t> no_occluder;
  std::vector<std::uint8_t> occluder;
  std::size_t changed_pixels = 0U;
  std::size_t darkened_pixels = 0U;
  std::uint64_t no_occluder_hash = UINT64_C(14695981039346656037);
  std::uint64_t occluder_hash = UINT64_C(14695981039346656037);
};

struct SmokeResult final {
  ImagePair hdr;
  ImagePair sdr;
  OgreNextPssmShadowRuntimeAudit audit;
  std::uint64_t disabled_default_hash = 0U;
  std::uint64_t disabled_explicit_hash = 0U;
};

[[noreturn]] void Fail(const std::string &detail) {
  throw std::runtime_error(detail);
}

void Require(bool condition, const std::string &detail) {
  if (!condition) {
    Fail(detail);
  }
}

std::string JsonEscape(const std::string &value) {
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

Arguments ParseArguments(int argc, char **argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--media-root" && index + 1 < argc) {
      arguments.media_root = argv[++index];
    } else if (option == "--report" && index + 1 < argc) {
      arguments.report_path = argv[++index];
    } else if (option == "--evidence" && index + 1 < argc) {
      arguments.evidence_path = argv[++index];
    } else {
      Fail("usage: ror_ogre_next_pssm_shadow_smoke --media-root ABSOLUTE_PATH --report REPORT.json --evidence EVIDENCE.bin");
    }
  }
  Require(!arguments.media_root.empty() && !arguments.report_path.empty() &&
              !arguments.evidence_path.empty(),
          "media, report, and evidence paths are required");
  return arguments;
}

void WriteText(const std::string &path, const std::string &contents) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(output), "could not open report output");
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  Require(static_cast<bool>(output), "could not write complete report output");
}

std::uint64_t Hash(const std::vector<std::uint8_t> &bytes) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

std::string Hex(std::uint64_t value) {
  std::ostringstream text;
  text << std::hex << std::setfill('0') << std::setw(16) << value;
  return text.str();
}

RenderAssetId AssetId(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x5053534D5F415353), low);
}

RenderAssetReference Asset(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, AssetId(low), 1U);
}

MeshResourceDescriptor QuadMesh(float half_width, float half_height,
                                const char *name) {
  MeshResourceDescriptor mesh;
  mesh.debug_name = name;
  mesh.index_format = MeshIndexFormat::UINT16;
  mesh.local_bounds.minimum = {-half_width, -half_height, 0.0F};
  mesh.local_bounds.maximum = {half_width, half_height, 0.0F};
  mesh.positions = {{-half_width, -half_height, 0.0F},
                    {half_width, -half_height, 0.0F},
                    {half_width, half_height, 0.0F},
                    {-half_width, half_height, 0.0F}};
  mesh.normals.assign(4U, Float3{0.0F, 0.0F, 1.0F});
  mesh.tangents.assign(4U, Float4{1.0F, 0.0F, 0.0F, 1.0F});
  mesh.texture_coordinates_0 = {
      {0.0F, 1.0F}, {1.0F, 1.0F}, {1.0F, 0.0F}, {0.0F, 0.0F}};
  mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
  return mesh;
}

MeshResourceDescriptor ReceiverMesh() {
  MeshResourceDescriptor mesh =
      QuadMesh(2.5F, 1.8F, "PSSM receiver quad");
  // Conservative caster bounds keep Ogre's focused PSSM fit invariant when
  // the fully-contained test occluder toggles its cast flag.
  mesh.local_bounds.minimum.z = -0.1F;
  mesh.local_bounds.maximum.z = 2.0F;
  return mesh;
}

MaterialDescriptor Material(const char *name, Float4 color,
                            float roughness) {
  MaterialDescriptor material;
  material.debug_name = name;
  material.base_color_factor = color;
  material.metallic_factor = 0.0F;
  material.roughness_factor = roughness;
  material.double_sided = true;
  return material;
}

RenderAssetDelta Catalog() {
  RenderAssetDelta delta;
  delta.registry_id = kRegistryId;
  delta.sequence = 1U;
  delta.full_snapshot = true;
  const auto add = [&](RenderAssetKind kind, std::uint64_t low,
                       RenderAssetPayload payload) {
    RenderAssetMutation mutation;
    mutation.asset = Asset(kind, low);
    mutation.payload = std::move(payload);
    delta.mutations.push_back(std::move(mutation));
  };
  add(RenderAssetKind::MESH, 1U, ReceiverMesh());
  add(RenderAssetKind::MESH, 2U,
      QuadMesh(0.45F, 0.45F, "PSSM isolated occluder quad"));
  add(RenderAssetKind::MATERIAL, 3U,
      Material("PSSM matte receiver", {0.72F, 0.74F, 0.78F, 1.0F},
               0.72F));
  add(RenderAssetKind::MATERIAL, 4U,
      Material("PSSM non-receiving occluder",
               {0.82F, 0.22F, 0.08F, 1.0F}, 0.44F));
  return delta;
}

std::shared_ptr<const SceneSnapshot> Scene(std::uint64_t snapshot_id,
                                           bool casts_shadow,
                                           bool shadows_enabled = true) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds =
      static_cast<double>(snapshot_id) / 48.0;
  descriptor.environment.ambient_radiance = {0.01F, 0.012F, 0.015F};

  MeshInstanceDescriptor receiver;
  receiver.instance_id = 1U;
  receiver.mesh = Asset(RenderAssetKind::MESH, 1U);
  receiver.material = Asset(RenderAssetKind::MATERIAL, 3U);
  receiver.local_bounds = ReceiverMesh().local_bounds;
  receiver.flags =
      MESH_INSTANCE_CASTS_SHADOW | MESH_INSTANCE_RECEIVES_SHADOW;
  descriptor.mesh_instances.push_back(receiver);

  MeshInstanceDescriptor occluder;
  occluder.instance_id = 2U;
  occluder.mesh = Asset(RenderAssetKind::MESH, 2U);
  occluder.material = Asset(RenderAssetKind::MATERIAL, 4U);
  occluder.local_bounds = QuadMesh(0.45F, 0.45F, "occluder").local_bounds;
  occluder.render_from_object.elements[14U] = 1.5F;
  occluder.previous_render_from_object = occluder.render_from_object;
  occluder.flags = casts_shadow ? MESH_INSTANCE_CASTS_SHADOW : 0U;
  descriptor.mesh_instances.push_back(occluder);

  LightDescriptor light;
  light.light_id = 1U;
  light.type = LightType::DIRECTIONAL;
  Require(NormalizePhotometricColorLinear({1.0F, 0.94F, 0.86F},
                                          light.color_linear),
          "directional color could not be normalized");
  light.intensity = 2048.0F;
  const float inverse_length = 1.0F / std::sqrt(0.4F * 0.4F +
                                                0.2F * 0.2F + 0.8F);
  light.direction = {0.4F * inverse_length, -0.2F * inverse_length,
                     -std::sqrt(0.8F) * inverse_length};
  light.previous_direction = light.direction;
  light.shadow_flags = shadows_enabled ? LIGHT_SHADOW_STATIC_GEOMETRY : 0U;
  descriptor.lights.push_back(light);

  SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "could not create controlled PSSM scene: " +
                            created.validation.field + ": " +
                            created.validation.detail);
  return created.snapshot;
}

Matrix4x4 Projection() {
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.5F;
  projection.elements[10U] =
      kOgreNextPssmFarMeters /
      (kOgreNextPssmNearMeters - kOgreNextPssmFarMeters);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      kOgreNextPssmNearMeters * kOgreNextPssmFarMeters /
      (kOgreNextPssmNearMeters - kOgreNextPssmFarMeters);
  return projection;
}

RenderFrameRequest Frame(
    std::uint64_t frame_id,
    const std::shared_ptr<const SceneSnapshot> &scene, PixelFormat format) {
  RenderFrameRequest request;
  request.frame_id = frame_id;
  request.scene_snapshot = scene;
  request.present = false;
  request.color_format = format;
  CameraViewRequest view;
  view.view_id = 1U;
  view.width = kWidth;
  view.height = kHeight;
  view.near_plane = kOgreNextPssmNearMeters;
  view.far_plane = kOgreNextPssmFarMeters;
  view.view_from_render.elements[14U] = -4.0F;
  view.previous_view_from_render = view.view_from_render;
  view.clip_from_view = Projection();
  view.previous_clip_from_view = view.clip_from_view;
  request.views.push_back(view);
  return request;
}

FrontendInitializationRequest Initialization() {
  FrontendInitializationRequest request;
  request.initial_width = kWidth;
  request.initial_height = kHeight;
  request.maximum_frames_in_flight = 1U;
  request.headless = true;
  request.vertical_sync = false;
  return request;
}

const std::vector<std::uint8_t> &Bytes(const RenderFrameOutput &output,
                                       PixelFormat format) {
  Require(output.status == RenderFrameStatus::RENDERED &&
              output.attachments.size() == 1U,
          "PSSM smoke returned an incomplete frame");
  const FrameAttachment &attachment = output.attachments.front();
  const std::uint64_t bytes_per_pixel =
      format == PixelFormat::RGBA16_FLOAT ? 8U : 4U;
  Require(attachment.format == format && attachment.width == kWidth &&
              attachment.height == kHeight &&
              attachment.row_pitch_bytes == kWidth * bytes_per_pixel &&
              attachment.bytes.size() ==
                  static_cast<std::size_t>(attachment.row_pitch_bytes) *
                      kHeight,
          "PSSM readback metadata is not exact and tightly packed");
  return attachment.bytes;
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

float Luminance(const std::uint8_t *pixel, PixelFormat format) {
  if (format == PixelFormat::RGBA8_SRGB) {
    return 0.2126F * static_cast<float>(pixel[0U]) +
           0.7152F * static_cast<float>(pixel[1U]) +
           0.0722F * static_cast<float>(pixel[2U]);
  }
  float channels[3U]{};
  for (std::size_t channel = 0U; channel < 3U; ++channel) {
    std::uint16_t half = 0U;
    std::memcpy(&half, pixel + channel * 2U, sizeof(half));
    channels[channel] = HalfToFloat(half);
    Require(std::isfinite(channels[channel]),
            "PSSM HDR evidence contains a non-finite value");
  }
  return 0.2126F * channels[0U] + 0.7152F * channels[1U] +
         0.0722F * channels[2U];
}

ImagePair Compare(const RenderFrameOutput &without_occluder,
                  const RenderFrameOutput &with_occluder,
                  PixelFormat format) {
  ImagePair result;
  result.no_occluder = Bytes(without_occluder, format);
  result.occluder = Bytes(with_occluder, format);
  const std::size_t bytes_per_pixel =
      format == PixelFormat::RGBA16_FLOAT ? 8U : 4U;
  for (std::uint32_t y = 0U; y < kHeight; ++y) {
    for (std::uint32_t x = 0U; x < kWidth; ++x) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * kWidth + x) * bytes_per_pixel;
      if (std::equal(result.no_occluder.begin() +
                         static_cast<std::ptrdiff_t>(offset),
                     result.no_occluder.begin() +
                         static_cast<std::ptrdiff_t>(offset + bytes_per_pixel),
                     result.occluder.begin() +
                         static_cast<std::ptrdiff_t>(offset))) {
        continue;
      }
      ++result.changed_pixels;
      // The reviewed projection puts the receiver wholly inside this mask.
      // A changed background or visible-occluder pixel is a hard isolation
      // failure; no backend-specific mask widening is permitted.
      const bool in_receiver = x >= 34U && x <= 158U && y >= 18U && y <= 110U;
      const bool in_visible_occluder =
          x >= 80U && x <= 111U && y >= 48U && y <= 79U;
      if (!in_receiver || in_visible_occluder) {
        std::ostringstream detail;
        detail << "occluder cast flag changed a non-receiver pixel at ("
               << x << ", " << y << ")"
               << " in_receiver=" << in_receiver
               << " in_visible_occluder=" << in_visible_occluder;
        Fail(detail.str());
      }
      const float unshadowed =
          Luminance(result.no_occluder.data() + offset, format);
      const float shadowed = Luminance(result.occluder.data() + offset, format);
      if (shadowed < unshadowed) {
        ++result.darkened_pixels;
      }
    }
  }
  result.no_occluder_hash = Hash(result.no_occluder);
  result.occluder_hash = Hash(result.occluder);
  if (!(result.changed_pixels >= 16U &&
        result.darkened_pixels * 10U >= result.changed_pixels * 9U &&
        result.no_occluder_hash != result.occluder_hash)) {
    std::ostringstream detail;
    detail << "isolated caster flag produced no receiver-local shadow response"
           << " (changed=" << result.changed_pixels
           << ", darkened=" << result.darkened_pixels
           << ", no_occluder=" << Hex(result.no_occluder_hash)
           << ", occluder=" << Hex(result.occluder_hash) << ')';
    Fail(detail.str());
  }
  return result;
}

void RequireSuccess(const RenderOperationResult &result,
                    const std::string &operation) {
  Require(result.ok(), operation + " failed: " + result.detail);
}

RenderOperationResult InitializeAndSync(OgreNextN1Frontend &frontend) {
  const RenderOperationResult initialized = frontend.Initialize(Initialization());
  if (!initialized) {
    return initialized;
  }
  return frontend.SynchronizeAssets(Catalog());
}

std::vector<std::uint8_t> RenderDisabled(const std::string &media_root,
                                         bool explicit_disabled) {
  OgreNextN1Configuration configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
  if (explicit_disabled) {
    configuration.directional_shadow_mode =
        OgreNextDirectionalShadowMode::DISABLED;
  }
  OgreNextN1Frontend frontend(std::move(configuration));
  RequireSuccess(InitializeAndSync(frontend), "disabled Initialize/sync");
  RenderFrameOutput output;
  RequireSuccess(frontend.Render(
                     Frame(1U, Scene(explicit_disabled ? 102U : 101U, false,
                                     false),
                           PixelFormat::RGBA8_SRGB),
                     output),
                 "disabled Render");
  const std::vector<std::uint8_t> bytes = Bytes(output, PixelFormat::RGBA8_SRGB);
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "disabled Shutdown");
  return bytes;
}

RenderOperationResult RunShadow(const std::string &media_root,
                                SmokeResult &result) {
  OgreNextN1Configuration configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
  configuration.directional_shadow_mode =
      OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1;
  OgreNextN1Frontend frontend(std::move(configuration));
  const RenderOperationResult initialization = InitializeAndSync(frontend);
  if (!initialization) {
    return initialization;
  }

  const auto without_occluder = Scene(1U, false);
  const auto with_occluder = Scene(2U, true);
  RenderFrameOutput hdr_without;
  RenderFrameOutput hdr_with;
  RenderFrameOutput sdr_without;
  RenderFrameOutput sdr_with;
  RequireSuccess(frontend.Render(
                     Frame(1U, without_occluder, PixelFormat::RGBA16_FLOAT),
                     hdr_without),
                 "PSSM HDR no-occluder Render");
  RequireSuccess(frontend.Render(
                     Frame(2U, with_occluder, PixelFormat::RGBA16_FLOAT),
                     hdr_with),
                 "PSSM HDR occluder Render");
  RequireSuccess(frontend.Render(
                     Frame(3U, without_occluder, PixelFormat::RGBA8_SRGB),
                     sdr_without),
                 "PSSM SDR no-occluder Render");
  RequireSuccess(frontend.Render(
                     Frame(4U, with_occluder, PixelFormat::RGBA8_SRGB),
                     sdr_with),
                 "PSSM SDR occluder Render");
  result.hdr = Compare(hdr_without, hdr_with, PixelFormat::RGBA16_FLOAT);
  result.sdr = Compare(sdr_without, sdr_with, PixelFormat::RGBA8_SRGB);
  result.audit = frontend.QueryDirectionalShadowAudit();
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "PSSM Shutdown");
  result.audit = frontend.QueryDirectionalShadowAudit();
  Require(result.audit.version == kOgreNextPssmShadowContractVersion &&
              result.audit.configured_mode ==
                  OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1 &&
              result.audit.shadow_frames_completed == 4U &&
              result.audit.shadow_node_creates == 4U &&
              result.audit.shadow_node_destroys == 4U &&
              result.audit.receiver_datablock_creates == 4U &&
              result.audit.receiver_datablock_destroys == 4U &&
              result.audit.last_frame.enabled &&
              result.audit.last_frame.static_caster_count == 2U &&
              result.audit.last_frame.dynamic_caster_count == 0U &&
              result.audit.last_frame.receiver_count == 1U &&
              result.audit.exact_native_readback,
          "PSSM runtime topology/caster/receiver audit is incomplete");
  return RenderOperationResult::Success();
}

void WriteEvidence(const std::string &path, const SmokeResult &result) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(output), "could not open PSSM evidence output");
  for (const std::vector<std::uint8_t> *bytes :
       {&result.hdr.no_occluder, &result.hdr.occluder,
        &result.sdr.no_occluder, &result.sdr.occluder}) {
    output.write(reinterpret_cast<const char *>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size()));
  }
  Require(static_cast<bool>(output), "could not write complete PSSM evidence");
}

std::string UnsupportedReport(const RenderOperationResult &failure) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_pssm_shadow_smoke.v1\",\n"
         << "  \"status\": \"unsupported\",\n"
         << "  \"reason\": \"" << JsonEscape(failure.detail) << "\",\n"
         << "  \"required\": \"2048x3072 D32_FLOAT RTT plus texture-gather PCF4\",\n"
         << "  \"backend_substitution\": false\n"
         << "}\n";
  return report.str();
}

std::string PassReport(const SmokeResult &result,
                       const std::string &evidence_path) {
  OgreNextPssmSplitPolicy splits;
  Require(TryBuildOgreNextPssmSplitPolicy(splits),
          "fixed PSSM splits disappeared while writing evidence");
  const std::size_t evidence_bytes =
      result.hdr.no_occluder.size() + result.hdr.occluder.size() +
      result.sdr.no_occluder.size() + result.sdr.occluder.size();
  std::ostringstream report;
  report << std::setprecision(9)
         << "{\n"
         << "  \"schema\": \"ror.ogre_next_pssm_shadow_smoke.v1\",\n"
         << "  \"status\": \"pass\",\n"
         << "  \"provenance\": {\n"
         << "    \"ror_repository\": \"" << ROR_OGRE_NEXT_N1_ROR_REPOSITORY
         << "\",\n"
         << "    \"ror_ref\": \"" << ROR_OGRE_NEXT_N1_ROR_REF << "\",\n"
         << "    \"ror_commit\": \"" << ROR_OGRE_NEXT_N1_ROR_COMMIT
         << "\",\n"
         << "    \"ror_relevant_source_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N1_ROR_SOURCE_MANIFEST_SHA256 << "\",\n"
         << "    \"ogre_next_commit\": \"" << ROR_OGRE_NEXT_N1_OGRE_COMMIT
         << "\",\n"
         << "    \"ogre_next_archive_sha256\": \""
         << ROR_OGRE_NEXT_N1_OGRE_ARCHIVE_SHA256 << "\",\n"
         << "    \"executable_build_identity\": \""
         << ROR_OGRE_NEXT_N1_BUILD_IDENTITY << "\"\n"
         << "  },\n"
         << "  \"platform_policy\": \""
         << ROR_OGRE_NEXT_N1_PLATFORM_POLICY << "\",\n"
         << "  \"renderer\": \"" << ROR_OGRE_NEXT_N1_RENDERER_NAME
         << "\",\n"
         << "  \"shadow_contract\": {\n"
         << "    \"version\": " << kOgreNextPssmShadowContractVersion
         << ",\n"
         << "    \"mode\": \"PSSM_3_CASCADE_V1\",\n"
         << "    \"cascade_count\": " << kOgreNextPssmCascadeCount
         << ",\n"
         << "    \"split_points_m\": [" << splits.split_points[0U] << ", "
         << splits.split_points[1U] << ", " << splits.split_points[2U]
         << ", " << splits.split_points[3U] << "],\n"
         << "    \"blend_points_m\": [" << splits.blend_points[0U] << ", "
         << splits.blend_points[1U] << "],\n"
         << "    \"fade_point_m\": " << splits.fade_point << ",\n"
         << "    \"atlas\": {\"format\": \"D32_FLOAT\", \"width\": "
         << kOgreNextPssmAtlasWidth << ", \"height\": "
         << kOgreNextPssmAtlasHeight << "},\n"
         << "    \"filter\": \"PCF_4x4\",\n"
         << "    \"programmatic_compositor2\": true,\n"
         << "    \"ui_included\": false,\n"
         << "    \"backend_substitution\": false,\n"
         << "    \"native_definition_and_split_readback\": true\n"
         << "  },\n"
         << "  \"isolation\": {\n"
         << "    \"only_changed_input\": \"occluder_instance_casts_shadow\",\n"
         << "    \"changed_pixels_outside_receiver\": 0,\n"
         << "    \"changed_visible_occluder_pixels\": 0,\n"
         << "    \"hdr_changed_receiver_pixels\": "
         << result.hdr.changed_pixels << ",\n"
         << "    \"hdr_darkened_receiver_pixels\": "
         << result.hdr.darkened_pixels << ",\n"
         << "    \"sdr_changed_receiver_pixels\": "
         << result.sdr.changed_pixels << ",\n"
         << "    \"sdr_darkened_receiver_pixels\": "
         << result.sdr.darkened_pixels << ",\n"
         << "    \"shadow_disabled_default_equals_explicit\": true,\n"
         << "    \"shadow_disabled_exact_fnv1a64\": \""
         << Hex(result.disabled_default_hash) << "\"\n"
         << "  },\n"
         << "  \"lifecycle\": {\n"
         << "    \"shadow_frames_completed\": "
         << result.audit.shadow_frames_completed << ",\n"
         << "    \"shadow_node_creates\": "
         << result.audit.shadow_node_creates << ",\n"
         << "    \"shadow_node_destroys\": "
         << result.audit.shadow_node_destroys << ",\n"
         << "    \"receiver_datablock_creates\": "
         << result.audit.receiver_datablock_creates << ",\n"
         << "    \"receiver_datablock_destroys\": "
         << result.audit.receiver_datablock_destroys << "\n"
         << "  },\n"
         << "  \"evidence\": {\n"
         << "    \"file\": \""
         << JsonEscape(std::filesystem::u8path(evidence_path)
                           .filename()
                           .generic_u8string())
         << "\",\n"
         << "    \"bytes\": " << evidence_bytes << ",\n"
         << "    \"hdr_no_occluder_fnv1a64\": \""
         << Hex(result.hdr.no_occluder_hash) << "\",\n"
         << "    \"hdr_occluder_fnv1a64\": \""
         << Hex(result.hdr.occluder_hash) << "\",\n"
         << "    \"sdr_no_occluder_fnv1a64\": \""
         << Hex(result.sdr.no_occluder_hash) << "\",\n"
         << "    \"sdr_occluder_fnv1a64\": \""
         << Hex(result.sdr.occluder_hash) << "\"\n"
         << "  }\n"
         << "}\n";
  return report.str();
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    SmokeResult result;
    const RenderOperationResult shadow = RunShadow(arguments.media_root, result);
    if (!shadow) {
      if (shadow.code == RenderOperationCode::UNSUPPORTED) {
        const std::string report = UnsupportedReport(shadow);
        WriteText(arguments.report_path, report);
        std::cout << report;
        return kUnsupportedExitCode;
      }
      Fail("PSSM initialization/sync failed: " + shadow.detail);
    }
    const std::vector<std::uint8_t> disabled_default =
        RenderDisabled(arguments.media_root, false);
    const std::vector<std::uint8_t> disabled_explicit =
        RenderDisabled(arguments.media_root, true);
    Require(disabled_default == disabled_explicit,
            "default and explicit shadow-disabled paths are not pixel-identical");
    result.disabled_default_hash = Hash(disabled_default);
    result.disabled_explicit_hash = Hash(disabled_explicit);
    WriteEvidence(arguments.evidence_path, result);
    const std::string report = PassReport(result, arguments.evidence_path);
    WriteText(arguments.report_path, report);
    std::cout << report;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next PSSM shadow smoke failed: " << error.what()
              << '\n';
    return 1;
  }
}
