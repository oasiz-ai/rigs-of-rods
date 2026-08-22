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
  std::string execution_challenge;
};

struct ImagePair final {
  std::vector<std::uint8_t> no_occluder;
  std::vector<std::uint8_t> occluder;
  std::size_t changed_pixels = 0U;
  std::size_t darkened_pixels = 0U;
  std::uint64_t no_occluder_hash = UINT64_C(14695981039346656037);
  std::uint64_t occluder_hash = UINT64_C(14695981039346656037);
};

struct ReviewedRegion final {
  std::uint32_t receiver_min_x = 0U;
  std::uint32_t receiver_max_x = 0U;
  std::uint32_t receiver_min_y = 0U;
  std::uint32_t receiver_max_y = 0U;
  std::uint32_t occluder_min_x = 0U;
  std::uint32_t occluder_max_x = 0U;
  std::uint32_t occluder_min_y = 0U;
  std::uint32_t occluder_max_y = 0U;
};

struct CascadeProof final {
  std::uint32_t cascade_index = 0U;
  float receiver_depth_m = 0.0F;
  float occluder_depth_m = 0.0F;
  ImagePair sdr;
};

struct SmokeResult final {
  ImagePair hdr;
  ImagePair sdr;
  std::array<CascadeProof, 2U> distant_cascades;
  OgreNextPssmShadowRuntimeAudit audit;
  std::uint64_t disabled_default_hash = 0U;
  std::uint64_t disabled_explicit_hash = 0U;
  bool normalized_visibility_mask_verified = false;
  bool d32_post_create_retry_verified = false;
  bool d32_cleanup_lookup_retry_verified = false;
  bool receiver_clone_retry_verified = false;
  bool workspace_node_retry_verified = false;
  bool receiver_cleanup_lookup_retry_verified = false;
  bool workspace_definition_cleanup_lookup_retry_verified = false;
  bool workspace_cleanup_lookup_retry_verified = false;
  bool shadow_cleanup_lookup_retry_verified = false;
  bool target_cleanup_lookup_retry_verified = false;
  ImagePair off_center_tight_bounds;
  bool off_center_projection_verified = false;
  bool tight_caster_bounds_verified = false;
};

[[noreturn]] void Fail(const std::string &detail) {
  throw std::runtime_error(detail);
}

void Require(bool condition, const std::string &detail) {
  if (!condition) {
    Fail(detail);
  }
}

bool Matches(const Float3 &value, float x, float y, float z) noexcept {
  constexpr float kTolerance = 1.0e-6F;
  return std::fabs(value.x - x) <= kTolerance &&
         std::fabs(value.y - y) <= kTolerance &&
         std::fabs(value.z - z) <= kTolerance;
}

bool Matches(const OgreNextPssmNativeAabb &value, float minimum_x,
             float minimum_y, float minimum_z, float maximum_x, float maximum_y,
             float maximum_z) noexcept {
  return Matches(value.minimum, minimum_x, minimum_y, minimum_z) &&
         Matches(value.maximum, maximum_x, maximum_y, maximum_z);
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
    } else if (option == "--execution-challenge" && index + 1 < argc) {
      arguments.execution_challenge = argv[++index];
    } else {
      Fail("usage: ror_ogre_next_pssm_shadow_smoke --media-root ABSOLUTE_PATH --report REPORT.json --evidence EVIDENCE.bin --execution-challenge 64_LOWER_HEX");
    }
  }
  Require(!arguments.media_root.empty() && !arguments.report_path.empty() &&
              !arguments.evidence_path.empty() &&
              arguments.execution_challenge.size() == 64U &&
              std::all_of(arguments.execution_challenge.begin(),
                          arguments.execution_challenge.end(),
                          [](char character) {
                            return (character >= '0' && character <= '9') ||
                                   (character >= 'a' && character <= 'f');
                          }),
          "media, report, evidence, and a 64-lowercase-hex execution challenge are required");
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

void WriteAabbJson(std::ostream &output, const OgreNextPssmNativeAabb &aabb) {
  output << "{\"minimum\": [" << aabb.minimum.x << ", " << aabb.minimum.y
         << ", " << aabb.minimum.z << "], \"maximum\": [" << aabb.maximum.x
         << ", " << aabb.maximum.y << ", " << aabb.maximum.z << "]}";
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

MeshResourceDescriptor TightReceiverMesh() {
  return QuadMesh(2.5F, 1.8F, "PSSM exact-bounds receiver quad");
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
  add(RenderAssetKind::MESH, 5U, TightReceiverMesh());
  return delta;
}

std::shared_ptr<const SceneSnapshot> Scene(std::uint64_t snapshot_id,
                                           bool casts_shadow,
                                           bool shadows_enabled = true,
                                           float scale = 1.0F,
                                           float horizontal_offset = 0.0F,
                                           float occluder_vertical_offset =
                                               0.0F,
                                           bool tight_receiver_bounds = false) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = 1U;
  descriptor.simulation_time_seconds = 1.0 / 48.0;
  descriptor.environment.ambient_radiance = {0.01F, 0.012F, 0.015F};

  MeshInstanceDescriptor receiver;
  receiver.instance_id = 1U;
  receiver.mesh = Asset(RenderAssetKind::MESH,
                        tight_receiver_bounds ? 5U : 1U);
  receiver.material = Asset(RenderAssetKind::MATERIAL, 3U);
  receiver.local_bounds = tight_receiver_bounds
                              ? TightReceiverMesh().local_bounds
                              : ReceiverMesh().local_bounds;
  receiver.flags =
      MESH_INSTANCE_CASTS_SHADOW | MESH_INSTANCE_RECEIVES_SHADOW;
  receiver.render_from_object.elements[0U] = scale;
  receiver.render_from_object.elements[5U] = scale;
  receiver.render_from_object.elements[10U] = scale;
  receiver.render_from_object.elements[12U] = horizontal_offset;
  receiver.previous_render_from_object = receiver.render_from_object;
  descriptor.mesh_instances.push_back(receiver);

  MeshInstanceDescriptor occluder;
  occluder.instance_id = 2U;
  occluder.mesh = Asset(RenderAssetKind::MESH, 2U);
  occluder.material = Asset(RenderAssetKind::MATERIAL, 4U);
  occluder.local_bounds = QuadMesh(0.45F, 0.45F, "occluder").local_bounds;
  occluder.render_from_object.elements[0U] = scale;
  occluder.render_from_object.elements[5U] = scale;
  occluder.render_from_object.elements[10U] = scale;
  occluder.render_from_object.elements[12U] = horizontal_offset;
  occluder.render_from_object.elements[13U] = occluder_vertical_offset;
  occluder.render_from_object.elements[14U] = 1.5F * scale;
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

Matrix4x4 Projection(float horizontal_lens_offset = 0.0F,
                     float vertical_lens_offset = 0.0F) {
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  projection.elements[0U] = 1.0F;
  projection.elements[5U] = 1.5F;
  projection.elements[8U] = horizontal_lens_offset;
  projection.elements[9U] = vertical_lens_offset;
  projection.elements[10U] =
      kOgreNextExpectedViewFarMeters /
      (kOgreNextPssmNearMeters - kOgreNextExpectedViewFarMeters);
  projection.elements[11U] = -1.0F;
  projection.elements[14U] =
      kOgreNextPssmNearMeters * kOgreNextExpectedViewFarMeters /
      (kOgreNextPssmNearMeters - kOgreNextExpectedViewFarMeters);
  return projection;
}

RenderFrameRequest Frame(
    std::uint64_t frame_id,
    const std::shared_ptr<const SceneSnapshot> &scene, PixelFormat format,
    float camera_depth = 4.0F,
    std::uint32_t visibility_mask = 0xffffffffU,
    float horizontal_lens_offset = 0.0F,
    float vertical_lens_offset = 0.0F) {
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
  view.far_plane = kOgreNextExpectedViewFarMeters;
  view.view_from_render.elements[14U] = -camera_depth;
  view.previous_view_from_render = view.view_from_render;
  view.clip_from_view =
      Projection(horizontal_lens_offset, vertical_lens_offset);
  view.previous_clip_from_view = view.clip_from_view;
  view.visibility_mask = visibility_mask;
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
                  PixelFormat format, const ReviewedRegion &region) {
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
      const bool in_receiver =
          x >= region.receiver_min_x && x <= region.receiver_max_x &&
          y >= region.receiver_min_y && y <= region.receiver_max_y;
      const bool in_visible_occluder =
          x >= region.occluder_min_x && x <= region.occluder_max_x &&
          y >= region.occluder_min_y && y <= region.occluder_max_y;
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
  configuration.retain_native_lighting_content_evidence = true;
  OgreNextN1Frontend frontend(std::move(configuration));
  const RenderOperationResult initialization = InitializeAndSync(frontend);
  if (!initialization) {
    result.audit = frontend.QueryDirectionalShadowAudit();
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
  constexpr ReviewedRegion kNearRegion{34U, 158U, 18U, 110U,
                                        80U, 111U, 48U, 79U};
  result.hdr =
      Compare(hdr_without, hdr_with, PixelFormat::RGBA16_FLOAT, kNearRegion);
  result.sdr =
      Compare(sdr_without, sdr_with, PixelFormat::RGBA8_SRGB, kNearRegion);

  // Distant occluders sit wholly above the image while their directional
  // projections land on the off-axis receiver. Thus every changed image
  // pixel is receiver evidence, not a self-shadowed visible caster.
  constexpr ReviewedRegion kOffAxisRegion{68U, 191U, 18U, 110U,
                                           192U, 192U, 128U, 128U};
  struct DistantScenario final {
    std::uint32_t cascade_index;
    float camera_depth;
    float scale;
    float horizontal_offset;
    float occluder_vertical_offset;
    std::uint32_t visibility_mask;
  };
  constexpr std::array<DistantScenario, 2U> kDistantScenarios{{
      {1U, 20.0F, 5.0F, 7.5F, 11.0F, 0x00000001U},
      {2U, 100.0F, 25.0F, 37.5F, 55.0F, 0x00000001U},
  }};
  std::uint64_t frame_id = 5U;
  std::uint64_t snapshot_id = 3U;
  for (std::size_t index = 0U; index < kDistantScenarios.size(); ++index) {
    const DistantScenario &scenario = kDistantScenarios[index];
    const auto distant_without =
        Scene(snapshot_id++, false, true, scenario.scale,
              scenario.horizontal_offset,
              scenario.occluder_vertical_offset);
    const auto distant_with =
        Scene(snapshot_id++, true, true, scenario.scale,
              scenario.horizontal_offset,
              scenario.occluder_vertical_offset);
    RenderFrameOutput distant_without_output;
    RenderFrameOutput distant_with_output;
    RequireSuccess(frontend.Render(
                       Frame(frame_id++, distant_without,
                             PixelFormat::RGBA8_SRGB,
                             scenario.camera_depth,
                             scenario.visibility_mask),
                       distant_without_output),
                   "PSSM distant no-occluder Render");
    RequireSuccess(frontend.Render(
                       Frame(frame_id++, distant_with,
                             PixelFormat::RGBA8_SRGB,
                             scenario.camera_depth,
                             scenario.visibility_mask),
                       distant_with_output),
                   "PSSM distant occluder Render");
    CascadeProof &proof = result.distant_cascades[index];
    proof.cascade_index = scenario.cascade_index;
    proof.receiver_depth_m = scenario.camera_depth;
    proof.occluder_depth_m = scenario.camera_depth - 1.5F * scenario.scale;
    proof.sdr = Compare(distant_without_output, distant_with_output,
                        PixelFormat::RGBA8_SRGB, kOffAxisRegion);
  }

  constexpr float kHorizontalLensOffset = 0.25F;
  constexpr float kVerticalLensOffset = -0.125F;
  const auto tight_without =
      Scene(snapshot_id++, false, true, 1.0F, 0.0F, 0.0F, true);
  const auto tight_with =
      Scene(snapshot_id++, true, true, 1.0F, 0.0F, 0.0F, true);
  RenderFrameOutput tight_without_output;
  RenderFrameOutput tight_with_output;
  RequireSuccess(frontend.Render(
                     Frame(frame_id++, tight_without, PixelFormat::RGBA8_SRGB,
                           4.0F, 0x00000001U, kHorizontalLensOffset,
                           kVerticalLensOffset),
                     tight_without_output),
                 "PSSM off-center exact-bounds no-occluder Render");
  RequireSuccess(frontend.Render(
                     Frame(frame_id++, tight_with, PixelFormat::RGBA8_SRGB,
                           4.0F, 0x00000001U, kHorizontalLensOffset,
                           kVerticalLensOffset),
                     tight_with_output),
                 "PSSM off-center exact-bounds occluder Render");
  // This fixture deliberately uses exact zero-thickness geometry bounds. A
  // full-frame mask admits cascade-fit movement while still requiring a
  // predominantly darkened caster response. Its evidence is kept separate
  // from the stricter receiver-local isolation pairs above.
  constexpr ReviewedRegion kFullFrameRegion{0U, 191U, 0U, 127U,
                                             192U, 192U, 128U, 128U};
  result.off_center_tight_bounds =
      Compare(tight_without_output, tight_with_output,
              PixelFormat::RGBA8_SRGB, kFullFrameRegion);
  const OgreNextPssmShadowRuntimeAudit fixture_audit =
      frontend.QueryDirectionalShadowAudit();
  result.off_center_projection_verified =
      std::fabs(fixture_audit.last_frame.projection_extents.left + 0.75F) <
          1.0e-6F &&
      std::fabs(fixture_audit.last_frame.projection_extents.right - 1.25F) <
          1.0e-6F &&
      std::fabs(fixture_audit.last_frame.projection_extents.top -
                (0.875F / 1.5F)) < 1.0e-6F &&
      std::fabs(fixture_audit.last_frame.projection_extents.bottom -
                (-1.125F / 1.5F)) < 1.0e-6F;
  const auto &native_bounds = fixture_audit.last_native_bounds_observations;
  const auto receiver_local_matches = [](const OgreNextPssmNativeAabb &aabb) {
    return Matches(aabb, -2.5F, -1.8F, 0.0F, 2.5F, 1.8F, 0.0F);
  };
  const auto caster_local_matches = [](const OgreNextPssmNativeAabb &aabb) {
    return Matches(aabb, -0.45F, -0.45F, 0.0F, 0.45F, 0.45F, 0.0F);
  };
  result.tight_caster_bounds_verified =
      fixture_audit.native_bounds_readback_verified &&
      native_bounds.size() == 2U && native_bounds[0U].instance_id == 1U &&
      native_bounds[0U].casts_shadow && native_bounds[0U].receives_shadow &&
      receiver_local_matches(native_bounds[0U].expected_local) &&
      receiver_local_matches(native_bounds[0U].ogre_mesh_local) &&
      receiver_local_matches(native_bounds[0U].ogre_item_local) &&
      receiver_local_matches(native_bounds[0U].expected_world) &&
      receiver_local_matches(native_bounds[0U].ogre_item_world) &&
      native_bounds[1U].instance_id == 2U && native_bounds[1U].casts_shadow &&
      !native_bounds[1U].receives_shadow &&
      caster_local_matches(native_bounds[1U].expected_local) &&
      caster_local_matches(native_bounds[1U].ogre_mesh_local) &&
      caster_local_matches(native_bounds[1U].ogre_item_local) &&
      Matches(native_bounds[1U].expected_world, -0.45F, -0.45F, 1.5F, 0.45F,
              0.45F, 1.5F) &&
      Matches(native_bounds[1U].ogre_item_world, -0.45F, -0.45F, 1.5F, 0.45F,
              0.45F, 1.5F);
  Require(result.off_center_projection_verified &&
              result.tight_caster_bounds_verified,
          "PSSM off-center tangent or exact caster-bounds fixture failed");
  result.normalized_visibility_mask_verified = true;
  const OgreNextPssmShadowRuntimeAudit live_audit =
      frontend.QueryDirectionalShadowAudit();
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "PSSM Shutdown");
  result.audit = frontend.QueryDirectionalShadowAudit();
  Require(result.audit.version == kOgreNextPssmShadowContractVersion &&
              result.audit.configured_mode ==
                  OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1 &&
              result.audit.shadow_frames_completed == 10U &&
              result.audit.shadow_node_creates == 10U &&
              result.audit.shadow_node_creates ==
                  result.audit.shadow_node_destroys &&
              result.audit.workspace_node_definition_creates == 10U &&
              result.audit.workspace_node_definition_creates ==
                  result.audit.workspace_node_definition_destroys &&
              // The non-receiver clone now lives for its instance's
              // retained lifetime: one create when the occluder instance is
              // admitted, one destroy (plus absence proof) at shutdown.
              result.audit.receiver_datablock_creates == 1U &&
              result.audit.receiver_datablock_creates ==
                  result.audit.receiver_datablock_destroys &&
              result.audit.workspace_definition_cleanup_absence_checks == 10U &&
              result.audit.workspace_node_cleanup_absence_checks == 10U &&
              result.audit.shadow_node_cleanup_absence_checks == 10U &&
              result.audit.receiver_datablock_cleanup_absence_checks == 1U &&
              result.audit.target_texture_cleanup_absence_checks == 10U &&
              result.audit.last_frame.enabled &&
              result.audit.last_frame.static_caster_count == 2U &&
              result.audit.last_frame.dynamic_caster_count == 0U &&
              result.audit.last_frame.receiver_count == 1U &&
              result.audit.last_frame.native_visibility_mask == 1U &&
              result.audit.d32_probe_attempted &&
              result.audit.d32_render_target_supported &&
              result.audit.d32_atlas_allocation_verified &&
              result.audit.d32_atlas_readback_verified &&
              result.audit.d32_atlas_cleanup_verified &&
              result.audit.d32_atlas_cleanup_absence_checks == 1U &&
              result.audit.native_projection_extents_verified &&
              result.audit.native_readback_verified &&
              result.audit.native_bounds_readback_verified &&
              live_audit.last_native_bounds_observations.size() == 2U &&
              result.audit.last_native_bounds_observations.empty() &&
              std::all_of(result.audit.last_native_normal_offset_bias.begin(),
                          result.audit.last_native_normal_offset_bias.end(),
                          [](float bias) {
                            return std::isfinite(bias) &&
                                   bias >= kOgreNextPssmNormalOffsetBias;
                          }),
          "PSSM runtime topology/caster/receiver audit is incomplete");
  return RenderOperationResult::Success();
}

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
bool ProveTransactionalRetry(const std::string &media_root,
                             OgreNextN1PssmFailureStage stage) {
  OgreNextN1Configuration configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
  configuration.directional_shadow_mode =
      OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1;
  configuration.retain_native_lighting_content_evidence = true;
  configuration.pssm_failure_stage = stage;
  OgreNextN1Frontend frontend(std::move(configuration));
  RequireSuccess(InitializeAndSync(frontend),
                 "PSSM transactional retry Initialize/sync");
  const RenderFrameRequest request =
      Frame(1U, Scene(201U + static_cast<std::uint64_t>(stage), true),
            PixelFormat::RGBA8_SRGB);
  RenderFrameOutput ignored;
  const RenderOperationResult injected = frontend.Render(request, ignored);
  Require(injected.code == RenderOperationCode::BACKEND_FAILURE,
          "PSSM transactional fault seam did not fail closed");
  RenderFrameOutput recovered;
  RequireSuccess(frontend.Render(request, recovered),
                 "PSSM same-frame transactional retry");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "PSSM transactional retry Shutdown");
  const OgreNextPssmShadowRuntimeAudit audit =
      frontend.QueryDirectionalShadowAudit();
  Require(audit.shadow_frames_completed == 1U &&
              audit.shadow_node_creates == audit.shadow_node_destroys &&
              audit.workspace_node_definition_creates ==
                  audit.workspace_node_definition_destroys &&
              audit.receiver_datablock_creates ==
                  audit.receiver_datablock_destroys,
          "PSSM transactional retry leaked native frame-local state");
  return true;
}

bool ProveInitializationRetry(const std::string &media_root,
                              OgreNextN1PssmFailureStage stage) {
  OgreNextN1Configuration configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
  configuration.directional_shadow_mode =
      OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1;
  configuration.retain_native_lighting_content_evidence = true;
  configuration.pssm_failure_stage = stage;
  OgreNextN1Frontend frontend(std::move(configuration));
  const RenderOperationResult injected = frontend.Initialize(Initialization());
  Require(injected.code == RenderOperationCode::BACKEND_FAILURE,
          "PSSM initialization fault was misclassified as unsupported");
  RequireSuccess(InitializeAndSync(frontend),
                 "PSSM same-instance initialization retry");
  const OgreNextPssmShadowRuntimeAudit audit =
      frontend.QueryDirectionalShadowAudit();
  Require(audit.d32_probe_attempted && audit.d32_render_target_supported &&
              audit.d32_atlas_allocation_verified &&
              audit.d32_atlas_readback_verified &&
              audit.d32_atlas_cleanup_verified &&
              audit.d32_atlas_cleanup_absence_checks == 1U,
          "PSSM D32 retry did not prove exact native cleanup and reuse");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "PSSM initialization retry Shutdown");
  return true;
}

bool ProveCleanupLookupRetry(const std::string &media_root,
                             OgreNextN1PssmFailureStage stage) {
  OgreNextN1Configuration configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
  configuration.directional_shadow_mode =
      OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1;
  configuration.retain_native_lighting_content_evidence = true;
  configuration.pssm_failure_stage = stage;
  OgreNextN1Frontend frontend(std::move(configuration));
  RequireSuccess(InitializeAndSync(frontend),
                 "PSSM cleanup-lookup Initialize/sync");
  const RenderFrameRequest request =
      Frame(1U, Scene(301U + static_cast<std::uint64_t>(stage), true),
            PixelFormat::RGBA8_SRGB);
  RenderFrameOutput ignored;
  const RenderOperationResult injected = frontend.Render(request, ignored);
  Require(injected.code == RenderOperationCode::BACKEND_FAILURE &&
              !frontend.IsFrameComplete(request.frame_id),
          "PSSM cleanup lookup exception did not fail the frame closed");
  const OgreNextPssmShadowRuntimeAudit failed_audit =
      frontend.QueryDirectionalShadowAudit();
  bool missing_expected_absence_proof = false;
  switch (stage) {
  case OgreNextN1PssmFailureStage::DURING_WORKSPACE_DEFINITION_CLEANUP_LOOKUP:
    missing_expected_absence_proof =
        failed_audit.workspace_definition_cleanup_absence_checks == 0U;
    break;
  case OgreNextN1PssmFailureStage::DURING_WORKSPACE_NODE_CLEANUP_LOOKUP:
    missing_expected_absence_proof =
        failed_audit.workspace_node_cleanup_absence_checks == 0U;
    break;
  case OgreNextN1PssmFailureStage::DURING_SHADOW_NODE_CLEANUP_LOOKUP:
    missing_expected_absence_proof =
        failed_audit.shadow_node_cleanup_absence_checks == 0U;
    break;
  case OgreNextN1PssmFailureStage::DURING_TARGET_TEXTURE_CLEANUP_LOOKUP:
    missing_expected_absence_proof =
        failed_audit.target_texture_cleanup_absence_checks == 0U;
    break;
  default:
    break;
  }
  Require(
      missing_expected_absence_proof &&
          failed_audit.shadow_frames_completed == 0U,
      "PSSM cleanup lookup fault was incorrectly recorded as proven absent");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "PSSM cleanup-lookup fault Shutdown");
  RequireSuccess(InitializeAndSync(frontend),
                 "PSSM cleanup-lookup same-instance reinitialize/sync");
  RenderFrameOutput recovered;
  RequireSuccess(frontend.Render(request, recovered),
                 "PSSM cleanup-lookup same-frame retry");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "PSSM cleanup-lookup retry Shutdown");
  const OgreNextPssmShadowRuntimeAudit recovered_audit =
      frontend.QueryDirectionalShadowAudit();
  bool recovered_absence_proven = false;
  switch (stage) {
  case OgreNextN1PssmFailureStage::DURING_WORKSPACE_DEFINITION_CLEANUP_LOOKUP:
    recovered_absence_proven =
        recovered_audit.workspace_definition_cleanup_absence_checks == 1U;
    break;
  case OgreNextN1PssmFailureStage::DURING_WORKSPACE_NODE_CLEANUP_LOOKUP:
    recovered_absence_proven =
        recovered_audit.workspace_node_cleanup_absence_checks == 1U;
    break;
  case OgreNextN1PssmFailureStage::DURING_SHADOW_NODE_CLEANUP_LOOKUP:
    recovered_absence_proven =
        recovered_audit.shadow_node_cleanup_absence_checks == 1U;
    break;
  case OgreNextN1PssmFailureStage::DURING_TARGET_TEXTURE_CLEANUP_LOOKUP:
    recovered_absence_proven =
        recovered_audit.target_texture_cleanup_absence_checks == 1U;
    break;
  default:
    break;
  }
  Require(recovered_absence_proven &&
              recovered_audit.shadow_frames_completed == 1U &&
              recovered_audit.shadow_node_creates ==
                  recovered_audit.shadow_node_destroys &&
              recovered_audit.workspace_node_definition_creates ==
                  recovered_audit.workspace_node_definition_destroys &&
              recovered_audit.receiver_datablock_creates ==
                  recovered_audit.receiver_datablock_destroys,
          "PSSM cleanup lookup retry leaked a named native resource");
  return true;
}

// The PSSM non-receiver clone lives for its retained instance's lifetime, so
// its cleanup absence lookup runs when the retained scene is torn down —
// here at shutdown — instead of during a presented frame. The injected
// lookup fault must fail that teardown closed, and the same frontend
// instance must re-initialize, re-present, and prove the absence cleanly.
bool ProveRetainedCloneCleanupLookupRetry(const std::string &media_root) {
  OgreNextN1Configuration configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
  configuration.directional_shadow_mode =
      OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1;
  configuration.retain_native_lighting_content_evidence = true;
  configuration.pssm_failure_stage =
      OgreNextN1PssmFailureStage::DURING_RECEIVER_DATABLOCK_CLEANUP_LOOKUP;
  OgreNextN1Frontend frontend(std::move(configuration));
  RequireSuccess(InitializeAndSync(frontend),
                 "retained-clone cleanup-lookup Initialize/sync");
  const RenderFrameRequest request =
      Frame(1U, Scene(351U, true), PixelFormat::RGBA8_SRGB);
  RenderFrameOutput rendered;
  RequireSuccess(frontend.Render(request, rendered),
                 "retained-clone cleanup-lookup first Render");
  const RenderOperationResult injected =
      frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds);
  Require(injected.code == RenderOperationCode::BACKEND_FAILURE,
          "retained-clone teardown absence-lookup fault did not fail closed");
  const OgreNextPssmShadowRuntimeAudit failed_audit =
      frontend.QueryDirectionalShadowAudit();
  Require(failed_audit.receiver_datablock_cleanup_absence_checks == 0U &&
              failed_audit.receiver_datablock_creates == 1U &&
              failed_audit.receiver_datablock_destroys == 1U &&
              failed_audit.shadow_frames_completed == 1U,
          "retained-clone teardown fault was incorrectly recorded as proven absent");
  RequireSuccess(InitializeAndSync(frontend),
                 "retained-clone same-instance reinitialize/sync");
  RenderFrameOutput recovered;
  RequireSuccess(frontend.Render(request, recovered),
                 "retained-clone same-frame retry");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "retained-clone retry Shutdown");
  const OgreNextPssmShadowRuntimeAudit recovered_audit =
      frontend.QueryDirectionalShadowAudit();
  Require(recovered_audit.receiver_datablock_cleanup_absence_checks == 1U &&
              recovered_audit.receiver_datablock_creates ==
                  recovered_audit.receiver_datablock_destroys &&
              recovered_audit.shadow_frames_completed == 2U,
          "retained-clone cleanup retry leaked a named native resource");
  return true;
}
#endif

void WriteEvidence(const std::string &path, const SmokeResult &result) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(output), "could not open PSSM evidence output");
  for (const std::vector<std::uint8_t> *bytes :
       {&result.hdr.no_occluder, &result.hdr.occluder,
        &result.sdr.no_occluder, &result.sdr.occluder}) {
    output.write(reinterpret_cast<const char *>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size()));
  }
  for (const CascadeProof &proof : result.distant_cascades) {
    for (const std::vector<std::uint8_t> *bytes :
         {&proof.sdr.no_occluder, &proof.sdr.occluder}) {
      output.write(reinterpret_cast<const char *>(bytes->data()),
                   static_cast<std::streamsize>(bytes->size()));
    }
  }
  for (const std::vector<std::uint8_t> *bytes :
       {&result.off_center_tight_bounds.no_occluder,
        &result.off_center_tight_bounds.occluder}) {
    output.write(reinterpret_cast<const char *>(bytes->data()),
                 static_cast<std::streamsize>(bytes->size()));
  }
  Require(static_cast<bool>(output), "could not write complete PSSM evidence");
}

std::string UnsupportedReport(
    const RenderOperationResult &failure,
    const OgreNextPssmShadowRuntimeAudit &audit,
    const std::string &execution_challenge) {
  Require(failure.code == RenderOperationCode::UNSUPPORTED &&
              failure.detail == kOgreNextPssmCapabilityUnsupportedDetail &&
              audit.capability_check_completed &&
              (!audit.atlas_dimensions_supported ||
               !audit.texture_gather_supported) &&
              !audit.d32_probe_attempted &&
              !audit.d32_render_target_supported &&
              !audit.d32_atlas_allocation_verified &&
              !audit.d32_atlas_readback_verified &&
              audit.d32_atlas_cleanup_verified &&
              audit.d32_atlas_cleanup_absence_checks == 1U,
          "PSSM unsupported result was not exact native capability evidence");
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_pssm_shadow_smoke.v5\",\n"
         << "  \"status\": \"unsupported\",\n"
         << "  \"execution\": {\"schema\": "
            "\"ror.ogre_next_pssm_shadow_execution_challenge.v1\", "
            "\"challenge_nonce\": \""
         << execution_challenge << "\"},\n"
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
         << "    \"shader_media_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_MANIFEST_SHA256 << "\",\n"
         << "    \"executable_build_identity\": \""
         << ROR_OGRE_NEXT_N1_BASE_BUILD_IDENTITY << "\"\n"
         << "  },\n"
         << "  \"platform_policy\": \""
         << ROR_OGRE_NEXT_N1_PLATFORM_POLICY << "\",\n"
         << "  \"renderer\": \"" << ROR_OGRE_NEXT_N1_RENDERER_NAME
         << "\",\n"
         << "  \"capability_evidence\": {\n"
         << "    \"code\": \"PSSM_REQUIRED_NATIVE_CAPABILITY_MISSING\",\n"
         << "    \"reason\": \"" << JsonEscape(failure.detail) << "\",\n"
         << "    \"required_atlas_width\": " << kOgreNextPssmAtlasWidth
         << ",\n"
         << "    \"required_atlas_height\": " << kOgreNextPssmAtlasHeight
         << ",\n"
         << "    \"required_format\": \"D32_FLOAT\",\n"
         << "    \"required_filter\": \"PCF_4x4_TEXTURE_GATHER\",\n"
         << "    \"observed_maximum_texture_dimension\": "
         << audit.observed_maximum_texture_dimension << ",\n"
         << "    \"atlas_dimensions_supported\": "
         << (audit.atlas_dimensions_supported ? "true" : "false") << ",\n"
         << "    \"texture_gather_supported\": "
         << (audit.texture_gather_supported ? "true" : "false") << ",\n"
         << "    \"d32_probe_attempted\": "
         << (audit.d32_probe_attempted ? "true" : "false") << ",\n"
         << "    \"d32_render_target_supported\": "
         << (audit.d32_render_target_supported ? "true" : "false") << ",\n"
         << "    \"d32_atlas_allocation_verified\": "
         << (audit.d32_atlas_allocation_verified ? "true" : "false")
         << ",\n"
         << "    \"d32_atlas_readback_verified\": "
         << (audit.d32_atlas_readback_verified ? "true" : "false")
         << ",\n"
         << "    \"d32_atlas_cleanup_verified\": "
         << (audit.d32_atlas_cleanup_verified ? "true" : "false") << ",\n"
         << "    \"d32_atlas_cleanup_absence_checks\": "
         << audit.d32_atlas_cleanup_absence_checks << "\n"
         << "  },\n"
         << "  \"backend_substitution\": false\n"
         << "}\n";
  return report.str();
}

std::string PassReport(const SmokeResult &result,
                       const std::string &evidence_path,
                       const std::string &execution_challenge) {
  OgreNextPssmSplitPolicy splits;
  Require(TryBuildOgreNextPssmSplitPolicy(splits),
          "fixed PSSM splits disappeared while writing evidence");
  const std::size_t evidence_bytes =
      result.hdr.no_occluder.size() + result.hdr.occluder.size() +
      result.sdr.no_occluder.size() + result.sdr.occluder.size() +
      result.distant_cascades[0U].sdr.no_occluder.size() +
      result.distant_cascades[0U].sdr.occluder.size() +
      result.distant_cascades[1U].sdr.no_occluder.size() +
      result.distant_cascades[1U].sdr.occluder.size() +
      result.off_center_tight_bounds.no_occluder.size() +
      result.off_center_tight_bounds.occluder.size();
  std::ostringstream report;
  report << std::setprecision(9) << "{\n"
         << "  \"schema\": \"ror.ogre_next_pssm_shadow_smoke.v5\",\n"
         << "  \"status\": \"pass\",\n"
         << "  \"execution\": {\"schema\": "
            "\"ror.ogre_next_pssm_shadow_execution_challenge.v1\", "
            "\"challenge_nonce\": \""
         << execution_challenge << "\"},\n"
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
         << "    \"shader_media_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_MANIFEST_SHA256 << "\",\n"
         << "    \"executable_build_identity\": \""
         << ROR_OGRE_NEXT_N1_BASE_BUILD_IDENTITY << "\"\n"
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
         << "    \"split_stable_tangent_projection\": true,\n"
         << "    \"native_definition_split_and_runtime_bias_readback\": true,\n"
         << "    \"native_d32_probe_attempted\": true,\n"
         << "    \"native_d32_atlas_allocation_use_readback_verified\": true,\n"
         << "    \"native_d32_atlas_cleanup_verified\": true,\n"
         << "    \"native_d32_atlas_cleanup_absence_checks\": 1,\n"
         << "    \"runtime_normal_offset_bias\": ["
         << result.audit.last_native_normal_offset_bias[0U] << ", "
         << result.audit.last_native_normal_offset_bias[1U] << ", "
         << result.audit.last_native_normal_offset_bias[2U] << "]\n"
         << "  },\n"
         << "  \"isolation\": {\n"
         << "    \"controlled_visual_change\": \"occluder_instance_casts_shadow\",\n"
         << "    \"nonvisual_snapshot_identity_changed\": true,\n"
         << "    \"changed_pixels_outside_reviewed_receiver_region\": 0,\n"
         << "    \"changed_pixels_inside_reviewed_occluder_region\": 0,\n"
         << "    \"hdr_changed_receiver_pixels\": "
         << result.hdr.changed_pixels << ",\n"
         << "    \"hdr_darkened_receiver_pixels\": "
         << result.hdr.darkened_pixels << ",\n"
         << "    \"sdr_changed_receiver_pixels\": "
         << result.sdr.changed_pixels << ",\n"
         << "    \"sdr_darkened_receiver_pixels\": "
         << result.sdr.darkened_pixels << ",\n"
         << "    \"normalized_visibility_mask_0x1_verified\": "
         << (result.normalized_visibility_mask_verified ? "true" : "false")
         << ",\n"
         << "    \"shadow_disabled_default_equals_explicit\": true,\n"
         << "    \"shadow_disabled_exact_fnv1a64\": \""
         << Hex(result.disabled_default_hash) << "\"\n"
         << "  },\n"
         << "  \"distant_cascade_proof\": [\n";
  for (std::size_t index = 0U; index < result.distant_cascades.size(); ++index) {
    const CascadeProof &proof = result.distant_cascades[index];
    report << "    {\"cascade_index\": " << proof.cascade_index
           << ", \"receiver_depth_m\": " << proof.receiver_depth_m
           << ", \"occluder_depth_m\": " << proof.occluder_depth_m
           << ", \"off_axis\": true, \"sdr_changed_receiver_pixels\": "
           << proof.sdr.changed_pixels
           << ", \"sdr_darkened_receiver_pixels\": "
           << proof.sdr.darkened_pixels << "}"
           << (index + 1U == result.distant_cascades.size() ? "\n" : ",\n");
  }
  report << "  ],\n"
         << "  \"projection_and_bounds_fixture\": {\n"
         << "    \"horizontal_lens_offset\": 0.25,\n"
         << "    \"vertical_lens_offset\": -0.125,\n"
         << "    \"expected_tangent_extents\": [-0.75, 1.25, "
            "0.583333313, -0.75],\n"
         << "    \"off_center_projection_verified\": "
         << (result.off_center_projection_verified ? "true" : "false") << ",\n"
         << "    \"receiver_bounds_min_z\": 0,\n"
         << "    \"receiver_bounds_max_z\": 0,\n"
         << "    \"caster_bounds_min_z\": 0,\n"
         << "    \"caster_bounds_max_z\": 0,\n"
         << "    \"tight_caster_bounds_verified\": "
         << (result.tight_caster_bounds_verified ? "true" : "false") << ",\n"
         << "    \"native_bounds_readback_verified\": "
         << (result.audit.native_bounds_readback_verified ? "true" : "false")
         << ",\n"
         << "    \"native_aabb_observations\": [\n";
  for (std::size_t index = 0U;
       index < result.audit.last_native_bounds_observations.size(); ++index) {
    const OgreNextPssmNativeBoundsObservation &observation =
        result.audit.last_native_bounds_observations[index];
    report << "      {\"instance_id\": " << observation.instance_id
           << ", \"casts_shadow\": "
           << (observation.casts_shadow ? "true" : "false")
           << ", \"receives_shadow\": "
           << (observation.receives_shadow ? "true" : "false")
           << ", \"expected_local\": ";
    WriteAabbJson(report, observation.expected_local);
    report << ", \"ogre_mesh_local\": ";
    WriteAabbJson(report, observation.ogre_mesh_local);
    report << ", \"ogre_item_local\": ";
    WriteAabbJson(report, observation.ogre_item_local);
    report << ", \"expected_world\": ";
    WriteAabbJson(report, observation.expected_world);
    report << ", \"ogre_item_world\": ";
    WriteAabbJson(report, observation.ogre_item_world);
    report << "}"
           << (index + 1U == result.audit.last_native_bounds_observations.size()
                   ? "\n"
                   : ",\n");
  }
  report << "    ],\n"
         << "    \"sdr_changed_pixels\": "
         << result.off_center_tight_bounds.changed_pixels << ",\n"
         << "    \"sdr_darkened_pixels\": "
         << result.off_center_tight_bounds.darkened_pixels << "\n"
         << "  },\n"
         << "  \"lifecycle\": {\n"
         << "    \"shadow_frames_completed\": "
         << result.audit.shadow_frames_completed << ",\n"
         << "    \"shadow_node_creates\": "
         << result.audit.shadow_node_creates << ",\n"
         << "    \"shadow_node_destroys\": "
         << result.audit.shadow_node_destroys << ",\n"
         << "    \"workspace_node_definition_creates\": "
         << result.audit.workspace_node_definition_creates << ",\n"
         << "    \"workspace_node_definition_destroys\": "
         << result.audit.workspace_node_definition_destroys << ",\n"
         << "    \"receiver_datablock_creates\": "
         << result.audit.receiver_datablock_creates << ",\n"
         << "    \"receiver_datablock_destroys\": "
         << result.audit.receiver_datablock_destroys << ",\n"
         << "    \"d32_atlas_cleanup_absence_checks\": "
         << result.audit.d32_atlas_cleanup_absence_checks << ",\n"
         << "    \"workspace_definition_cleanup_absence_checks\": "
         << result.audit.workspace_definition_cleanup_absence_checks << ",\n"
         << "    \"workspace_node_cleanup_absence_checks\": "
         << result.audit.workspace_node_cleanup_absence_checks << ",\n"
         << "    \"shadow_node_cleanup_absence_checks\": "
         << result.audit.shadow_node_cleanup_absence_checks << ",\n"
         << "    \"receiver_datablock_cleanup_absence_checks\": "
         << result.audit.receiver_datablock_cleanup_absence_checks << ",\n"
         << "    \"target_texture_cleanup_absence_checks\": "
         << result.audit.target_texture_cleanup_absence_checks << ",\n"
         << "    \"d32_post_create_same_instance_retry_verified\": "
         << (result.d32_post_create_retry_verified ? "true" : "false") << ",\n"
         << "    \"d32_cleanup_lookup_failure_closed_retry_verified\": "
         << (result.d32_cleanup_lookup_retry_verified ? "true" : "false")
         << ",\n"
         << "    \"receiver_clone_same_frame_retry_verified\": "
         << (result.receiver_clone_retry_verified ? "true" : "false") << ",\n"
         << "    \"workspace_node_same_frame_retry_verified\": "
         << (result.workspace_node_retry_verified ? "true" : "false") << ",\n"
         << "    \"receiver_cleanup_lookup_failure_closed_retry_verified\": "
         << (result.receiver_cleanup_lookup_retry_verified ? "true" : "false")
         << ",\n"
         << "    \"workspace_definition_cleanup_lookup_failure_closed_retry_verified\": "
         << (result.workspace_definition_cleanup_lookup_retry_verified ? "true"
                                                                        : "false")
         << ",\n"
         << "    \"workspace_cleanup_lookup_failure_closed_retry_verified\": "
         << (result.workspace_cleanup_lookup_retry_verified ? "true" : "false")
         << ",\n"
         << "    \"shadow_cleanup_lookup_failure_closed_retry_verified\": "
         << (result.shadow_cleanup_lookup_retry_verified ? "true" : "false")
         << ",\n"
         << "    \"target_cleanup_lookup_failure_closed_retry_verified\": "
         << (result.target_cleanup_lookup_retry_verified ? "true" : "false")
         << "\n"
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
         << Hex(result.sdr.occluder_hash) << "\",\n"
         << "    \"cascade_2_sdr_no_occluder_fnv1a64\": \""
         << Hex(result.distant_cascades[0U].sdr.no_occluder_hash)
         << "\",\n"
         << "    \"cascade_2_sdr_occluder_fnv1a64\": \""
         << Hex(result.distant_cascades[0U].sdr.occluder_hash) << "\",\n"
         << "    \"cascade_3_sdr_no_occluder_fnv1a64\": \""
         << Hex(result.distant_cascades[1U].sdr.no_occluder_hash)
         << "\",\n"
         << "    \"cascade_3_sdr_occluder_fnv1a64\": \""
         << Hex(result.distant_cascades[1U].sdr.occluder_hash) << "\",\n"
         << "    \"off_center_tight_bounds_sdr_no_occluder_fnv1a64\": \""
         << Hex(result.off_center_tight_bounds.no_occluder_hash) << "\",\n"
         << "    \"off_center_tight_bounds_sdr_occluder_fnv1a64\": \""
         << Hex(result.off_center_tight_bounds.occluder_hash) << "\"\n"
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
        const std::string report = UnsupportedReport(
            shadow, result.audit, arguments.execution_challenge);
        WriteText(arguments.report_path, report);
        std::cout << report;
        return kUnsupportedExitCode;
      }
      Fail("PSSM initialization/sync failed: " + shadow.detail);
    }
#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
    result.d32_post_create_retry_verified = ProveInitializationRetry(
        arguments.media_root,
        OgreNextN1PssmFailureStage::AFTER_D32_ATLAS_CREATE);
    result.d32_cleanup_lookup_retry_verified = ProveInitializationRetry(
        arguments.media_root,
        OgreNextN1PssmFailureStage::DURING_D32_ATLAS_CLEANUP_LOOKUP);
    result.receiver_clone_retry_verified = ProveTransactionalRetry(
        arguments.media_root,
        OgreNextN1PssmFailureStage::AFTER_RECEIVER_DATABLOCK_CLONE);
    result.workspace_node_retry_verified = ProveTransactionalRetry(
        arguments.media_root,
        OgreNextN1PssmFailureStage::AFTER_WORKSPACE_NODE_DEFINITION);
    result.receiver_cleanup_lookup_retry_verified =
        ProveRetainedCloneCleanupLookupRetry(arguments.media_root);
    result.workspace_definition_cleanup_lookup_retry_verified =
        ProveCleanupLookupRetry(
            arguments.media_root,
            OgreNextN1PssmFailureStage::
                DURING_WORKSPACE_DEFINITION_CLEANUP_LOOKUP);
    result.workspace_cleanup_lookup_retry_verified = ProveCleanupLookupRetry(
        arguments.media_root,
        OgreNextN1PssmFailureStage::DURING_WORKSPACE_NODE_CLEANUP_LOOKUP);
    result.shadow_cleanup_lookup_retry_verified = ProveCleanupLookupRetry(
        arguments.media_root,
        OgreNextN1PssmFailureStage::DURING_SHADOW_NODE_CLEANUP_LOOKUP);
    result.target_cleanup_lookup_retry_verified = ProveCleanupLookupRetry(
        arguments.media_root,
        OgreNextN1PssmFailureStage::DURING_TARGET_TEXTURE_CLEANUP_LOOKUP);
#else
    Fail("PSSM transactional retry proof was not compiled");
#endif
    const std::vector<std::uint8_t> disabled_default =
        RenderDisabled(arguments.media_root, false);
    const std::vector<std::uint8_t> disabled_explicit =
        RenderDisabled(arguments.media_root, true);
    Require(disabled_default == disabled_explicit,
            "default and explicit shadow-disabled paths are not pixel-identical");
    result.disabled_default_hash = Hash(disabled_default);
    result.disabled_explicit_hash = Hash(disabled_explicit);
    WriteEvidence(arguments.evidence_path, result);
    const std::string report = PassReport(
        result, arguments.evidence_path, arguments.execution_challenge);
    WriteText(arguments.report_path, report);
    std::cout << report;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next PSSM shadow smoke failed: " << error.what()
              << '\n';
    return 1;
  }
}
