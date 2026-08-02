/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Same-device Metal N4 directional hard-shadow acceptance smoke.

#include "NativeDirectionalShadowContract.h"
#include "OgreNextMetalRayTracingBackend.h"
#include "OgreNextN1Frontend.h"
#include "OgreNextN1NativeInterop.h"
#include "ror_ogre_next_n1_config.h"

#include <CommonCrypto/CommonDigest.h>

#include <array>
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

#ifndef ROR_OGRE_NEXT_N4_SOURCE_COMMIT
#define ROR_OGRE_NEXT_N4_SOURCE_COMMIT "unknown"
#endif
#ifndef ROR_OGRE_NEXT_N4_SOURCE_REPOSITORY
#define ROR_OGRE_NEXT_N4_SOURCE_REPOSITORY "unknown"
#endif
#ifndef ROR_OGRE_NEXT_N4_SOURCE_REF
#define ROR_OGRE_NEXT_N4_SOURCE_REF "unknown"
#endif
#ifndef ROR_OGRE_NEXT_N4_RELEVANT_SOURCE_CLEAN
#define ROR_OGRE_NEXT_N4_RELEVANT_SOURCE_CLEAN 0
#endif
#ifndef ROR_OGRE_NEXT_N4_SOURCE_MANIFEST_SHA256
#define ROR_OGRE_NEXT_N4_SOURCE_MANIFEST_SHA256 "unknown"
#endif

#if !defined(ROR_OGRE_NEXT_N2_TEST_SEAM)
#error "The Metal N4 smoke target requires its isolated native test seam"
#endif

namespace {

using namespace RoR::Render;

constexpr std::uint32_t kWidth = 96U;
constexpr std::uint32_t kHeight = 64U;
constexpr std::uint64_t kRegistryId = UINT64_C(0x4E345F4D4554414C);
constexpr int kCapabilitySkipExitCode = 77;

struct Arguments final {
  std::string media_root;
  std::string raster_path;
  std::string visibility_path;
  std::string lineage_path;
  std::string hybrid_path;
  std::string report_path;
  std::string executable_path;
};

struct FileDigest final {
  std::uint64_t bytes = 0U;
  std::string sha256;
};

struct ShadowMetrics final {
  std::uint64_t pixel_count = 0U;
  std::uint64_t visible_pixels = 0U;
  std::uint64_t occluded_pixels = 0U;
  std::string raster_sha256;
  std::string visibility_sha256;
  std::string lineage_sha256;
  std::string hybrid_sha256;
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
    } else if (option == "--visibility" && index + 1 < argc) {
      arguments.visibility_path = argv[++index];
    } else if (option == "--lineage" && index + 1 < argc) {
      arguments.lineage_path = argv[++index];
    } else if (option == "--hybrid" && index + 1 < argc) {
      arguments.hybrid_path = argv[++index];
    } else if (option == "--report" && index + 1 < argc) {
      arguments.report_path = argv[++index];
    } else {
      Fail("usage: ror_ogre_next_metal_n4_directional_shadow_smoke --media-root ABSOLUTE_PATH [--raster RASTER.bin] [--visibility VISIBILITY.bin] [--lineage LINEAGE.bin] [--hybrid HYBRID.bin] [--report REPORT.json]");
    }
  }
  Require(!arguments.media_root.empty(), "--media-root is required");
  Require(!arguments.executable_path.empty(), "N4 executable path is empty");
  return arguments;
}

std::string JsonEscape(const std::string &value) {
  std::string escaped;
  for (const char source_character : value) {
    const unsigned char character =
        static_cast<unsigned char>(source_character);
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

std::string Hex16(std::uint16_t value) {
  std::ostringstream text;
  text << "0x" << std::hex << std::setfill('0') << std::setw(4)
       << static_cast<unsigned int>(value);
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
  Require(static_cast<bool>(input), "could not open N4 executable");
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
              "N4 executable SHA-256 update failed");
      result.bytes += static_cast<std::uint64_t>(count);
    }
  }
  Require(input.eof(), "could not finish reading N4 executable");
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  Require(CC_SHA256_Final(digest.data(), &context) == 1,
          "N4 executable SHA-256 finalization failed");
  result.sha256 = Hex(digest.data(), digest.size());
  return result;
}

void WriteBinary(const std::string &path,
                 const std::vector<std::uint8_t> &bytes) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(output), "could not open N4 binary artifact");
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  Require(static_cast<bool>(output), "could not write N4 binary artifact");
}

void WriteText(const std::string &path, const std::string &text) {
  if (path.empty()) {
    return;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  Require(static_cast<bool>(output), "could not open N4 report");
  output << text;
  Require(static_cast<bool>(output), "could not write N4 report");
}

RenderAssetId AssetId(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x4E345F4153534554), low);
}

RenderAssetReference Asset(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(kind, AssetId(low), 1U);
}

MeshResourceDescriptor Quad(float half_width, float half_height,
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

RenderAssetDelta MakeCatalog() {
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
  // This receiver covers every camera sample at z=0. The smaller, distinct
  // z=1 occluder covers only a central subset of the +Z visibility rays.
  add(RenderAssetKind::MESH, 1U,
      Quad(3.25F, 2.25F, "N4 full-view receiver quad"));
  add(RenderAssetKind::MESH, 2U,
      Quad(0.72F, 0.58F, "N4 partial distinct occluder quad"));
  add(RenderAssetKind::MATERIAL, 3U,
      Material("N4 receiver", {0.72F, 0.82F, 0.94F, 1.0F}, 0.68F));
  add(RenderAssetKind::MATERIAL, 4U,
      Material("N4 non-receiving occluder",
               {0.90F, 0.24F, 0.08F, 1.0F}, 0.42F));
  return delta;
}

std::shared_ptr<const SceneSnapshot> MakeScene() {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 1U;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = 1U;
  descriptor.simulation_time_seconds = 1.0 / 48.0;
  descriptor.environment.ambient_radiance = {0.035F, 0.04F, 0.05F};

  MeshInstanceDescriptor receiver;
  receiver.instance_id = 1U;
  receiver.mesh = Asset(RenderAssetKind::MESH, 1U);
  receiver.material = Asset(RenderAssetKind::MATERIAL, 3U);
  receiver.local_bounds = Quad(3.25F, 2.25F, "receiver").local_bounds;
  receiver.flags = MESH_INSTANCE_RECEIVES_SHADOW;
  receiver.visibility_mask = 0x01U;
  descriptor.mesh_instances.push_back(receiver);

  MeshInstanceDescriptor occluder;
  occluder.instance_id = 2U;
  occluder.mesh = Asset(RenderAssetKind::MESH, 2U);
  occluder.material = Asset(RenderAssetKind::MATERIAL, 4U);
  occluder.local_bounds = Quad(0.72F, 0.58F, "occluder").local_bounds;
  occluder.render_from_object.elements[14U] = 1.0F;
  occluder.previous_render_from_object = occluder.render_from_object;
  occluder.flags = MESH_INSTANCE_CASTS_SHADOW;
  // Keep the blocker out of the authored raster view while retaining its
  // separately published geometry for the native 0x02 TLAS instance mask.
  occluder.visibility_mask = 0x02U;
  descriptor.mesh_instances.push_back(occluder);

  LightDescriptor light;
  light.light_id = 1U;
  light.type = LightType::DIRECTIONAL;
  Require(NormalizePhotometricColorLinear({1.0F, 0.94F, 0.86F},
                                          light.color_linear),
          "N4 directional light color could not be normalized");
  light.intensity = 2048.0F;
  light.direction = {0.0F, 0.0F, -1.0F};
  light.previous_direction = light.direction;
  light.shadow_flags = LIGHT_SHADOW_STATIC_GEOMETRY;
  descriptor.lights.push_back(light);

  SceneSnapshotCreateResult created =
      CreateSceneSnapshot(std::move(descriptor));
  Require(created.ok(), "could not create N4 scene: " +
                            created.validation.field + ": " +
                            created.validation.detail);
  return created.snapshot;
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
    const std::shared_ptr<const SceneSnapshot> &scene) {
  RenderFrameRequest frame;
  frame.frame_id = 1U;
  frame.scene_snapshot = scene;
  frame.present = false;
  frame.color_format = PixelFormat::RGBA16_FLOAT;
  frame.requested_outputs = FrameOutputMask::COLOR;
  frame.allow_async_compute = false;
  CameraViewRequest view;
  view.view_id = 1U;
  view.width = kWidth;
  view.height = kHeight;
  view.near_plane = 0.1F;
  view.far_plane = 20.0F;
  view.visibility_mask = 0x01U;
  view.view_from_render.elements[14U] = -3.0F;
  view.previous_view_from_render = view.view_from_render;
  view.clip_from_view = Projection();
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

std::uint16_t Read16(const std::vector<std::uint8_t> &bytes,
                     std::size_t offset) {
  std::uint16_t value = 0U;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

std::uint32_t Read32(const std::vector<std::uint8_t> &bytes,
                     std::size_t offset) {
  std::uint32_t value = 0U;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

void RequireSampleMatchesReadback(
    const OgreNextMetalRayTracingEvidence &evidence, std::size_t sample_index,
    std::uint32_t width, std::uint32_t height) {
  Require(sample_index < evidence.directional_shadow_samples.size(),
          "N4 sample index is out of range");
  const std::uint32_t x =
      evidence.directional_shadow_sample_x[sample_index];
  const std::uint32_t y =
      evidence.directional_shadow_sample_y[sample_index];
  Require(x < width && y < height, "N4 sample coordinate is out of range");
  const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
  const NativeDirectionalShadowPassContract &sample =
      evidence.directional_shadow_samples[sample_index];
  const ValidationResult validation =
      ValidateNativeDirectionalShadowPassContract(sample);
  Require(static_cast<bool>(validation),
          "N4 backend sample did not validate against the portable contract: " +
              validation.field + ": " + validation.detail);
  Require(Read16(evidence.visibility_readback_bytes, pixel * 2U) ==
              sample.native_visibility_r16_bits,
          "N4 sample visibility does not name its exact readback texel");
  Require(std::memcmp(sample.raster_rgba16.channels.data(),
                      evidence.raster_readback_bytes.data() + pixel * 8U,
                      8U) == 0 &&
              std::memcmp(sample.native_hybrid_rgba16.channels.data(),
                          evidence.hybrid_readback_bytes.data() + pixel * 8U,
                          8U) == 0,
          "N4 sample pixels do not name their exact GPU readback texels");
}

ShadowMetrics ValidateEvidence(
    const OgreNextMetalRayTracingEvidence &evidence,
    const RenderFrameOutput &raster_output,
    const RenderFrameOutput &hybrid_output) {
  constexpr std::uint64_t pixel_count =
      static_cast<std::uint64_t>(kWidth) * kHeight;
  Require(evidence.directional_shadow_passed &&
              evidence.dispatch_readback_passed &&
              evidence.geometry_interop_passed &&
              evidence.exact_exported_vertex_slice_used &&
              evidence.exact_exported_index_slice_used &&
              evidence.exact_secondary_vertex_slice_used &&
              evidence.exact_secondary_index_slice_used &&
              evidence.exact_exported_color_image_used &&
              evidence.image_state_handoff_passed &&
              evidence.view_dependent_image_passed &&
              evidence.hybrid_composite_passed,
          "N4 backend did not retain complete native proof flags");
  Require(HasAttestedNativeDirectionalShadowCapabilities(
              evidence.directional_shadow_capabilities) &&
              evidence.blas_bytes != 0U &&
              evidence.secondary_blas_bytes != 0U &&
              evidence.tlas_bytes != 0U,
          "N4 backend did not attest two BLAS, one TLAS, and the complete Metal capability set");
  Require(evidence.geometry_request.instance_id == 1U &&
              evidence.secondary_geometry_request.instance_id == 2U &&
              evidence.geometry_export.instance_id == 1U &&
              evidence.secondary_geometry_export.instance_id == 2U,
          "N4 evidence lost the distinct receiver/occluder geometry lineage");
  Require(evidence.geometry_export.positions.stride_bytes ==
                  kOgreNextPositionNormalTangentUv0VertexStrideBytes &&
              evidence.secondary_geometry_export.positions.stride_bytes ==
                  kOgreNextPositionNormalTangentUv0VertexStrideBytes,
          "N4 did not use two exact RT4 48-byte geometry exports");
  Require(evidence.image_export.format == PixelFormat::RGBA16_FLOAT &&
              evidence.image_export.width == kWidth &&
              evidence.image_export.height == kHeight &&
              evidence.image_frame_synchronization
                      .frontend_image_release_state ==
                  NativeImageState::GENERAL_READ_WRITE &&
              evidence.image_frame_synchronization
                      .external_image_return_state ==
                  NativeImageState::GENERAL_READ_WRITE,
          "N4 evidence lost the exact Ogre RGBA16 image handoff");
  Require(evidence.raster_readback_bytes.size() == pixel_count * 8U &&
              evidence.visibility_readback_bytes.size() == pixel_count * 2U &&
              evidence.directional_lineage_readback_bytes.size() ==
                  pixel_count * 4U &&
              evidence.hybrid_readback_bytes.size() == pixel_count * 8U,
          "N4 readback byte layout is incomplete");
  Require(evidence.image_row_pitch_bytes >=
                  static_cast<std::uint64_t>(kWidth) * 8U &&
              evidence.visibility_row_pitch_bytes >=
                  static_cast<std::uint64_t>(kWidth) * 2U &&
              evidence.directional_lineage_row_pitch_bytes >=
                  static_cast<std::uint64_t>(kWidth) * 4U &&
              evidence.image_row_pitch_bytes % 256U == 0U &&
              evidence.visibility_row_pitch_bytes % 256U == 0U &&
              evidence.directional_lineage_row_pitch_bytes % 256U == 0U,
          "N4 native readback row pitches are not complete 256-byte Metal layouts");
  Require(raster_output.attachments.size() == 1U &&
              hybrid_output.attachments.size() == 1U &&
              raster_output.attachments.front().bytes ==
                  evidence.raster_readback_bytes &&
              hybrid_output.attachments.front().bytes ==
                  evidence.hybrid_readback_bytes,
          "N4 exact frontend/backend frame bytes disagree");

  ShadowMetrics metrics;
  metrics.pixel_count = pixel_count;
  for (std::size_t pixel = 0U;
       pixel < static_cast<std::size_t>(pixel_count); ++pixel) {
    const std::size_t raster_offset = pixel * 8U;
    const std::uint16_t visibility =
        Read16(evidence.visibility_readback_bytes, pixel * 2U);
    const std::uint32_t lineage =
        Read32(evidence.directional_lineage_readback_bytes, pixel * 4U);
    if (visibility == kNativeDirectionalShadowVisibleR16) {
      ++metrics.visible_pixels;
      Require(lineage == 1U,
              "N4 visible pixel did not record exact two-ray lineage");
      Require(std::memcmp(evidence.raster_readback_bytes.data() +
                              raster_offset,
                          evidence.hybrid_readback_bytes.data() +
                              raster_offset,
                          8U) == 0,
              "N4 visible pixel did not preserve exact raster RGBA16 bits");
    } else if (visibility == kNativeDirectionalShadowOccludedR16) {
      ++metrics.occluded_pixels;
      Require(lineage == 3U,
              "N4 occluded pixel did not record the distinct blocker hit");
      Require(Read16(evidence.hybrid_readback_bytes, raster_offset) == 0U &&
                  Read16(evidence.hybrid_readback_bytes,
                         raster_offset + 2U) == 0U &&
                  Read16(evidence.hybrid_readback_bytes,
                         raster_offset + 4U) == 0U &&
                  Read16(evidence.hybrid_readback_bytes,
                         raster_offset + 6U) ==
                      Read16(evidence.raster_readback_bytes,
                             raster_offset + 6U),
              "N4 occluded pixel did not zero exact RGB and preserve alpha");
    } else {
      Fail("N4 visibility contains a primary miss or noncanonical R16 value");
    }
  }
  Require(metrics.visible_pixels != 0U && metrics.occluded_pixels != 0U &&
              metrics.visible_pixels == evidence.receiver_visible_pixel_count &&
              metrics.occluded_pixels == evidence.occluded_pixel_count &&
              metrics.visible_pixels + metrics.occluded_pixels == pixel_count,
          "N4 full-view receiver and partial occluder evidence is incomplete");
  RequireSampleMatchesReadback(evidence, 0U, kWidth, kHeight);
  RequireSampleMatchesReadback(evidence, 1U, kWidth, kHeight);
  Require(evidence.directional_shadow_samples[0U].visibility ==
                  NativeDirectionalShadowVisibility::VISIBLE &&
              evidence.directional_shadow_samples[1U].visibility ==
                  NativeDirectionalShadowVisibility::OCCLUDED,
          "N4 evidence does not retain one visible and one occluded sample");

  metrics.raster_sha256 = Sha256(evidence.raster_readback_bytes);
  metrics.visibility_sha256 = Sha256(evidence.visibility_readback_bytes);
  metrics.lineage_sha256 =
      Sha256(evidence.directional_lineage_readback_bytes);
  metrics.hybrid_sha256 = Sha256(evidence.hybrid_readback_bytes);
  return metrics;
}

void WritePixelJson(std::ostream &output,
                    const NativeDirectionalShadowRgba16Pixel &pixel) {
  output << '[';
  for (std::size_t index = 0U; index < pixel.channels.size(); ++index) {
    if (index != 0U) {
      output << ", ";
    }
    output << '"' << Hex16(pixel.channels[index]) << '"';
  }
  output << ']';
}

std::string ProvenanceJson(const FileDigest &executable) {
  std::ostringstream report;
  report << "  \"provenance\": {\n"
         << "    \"ror_repository\": \""
         << JsonEscape(ROR_OGRE_NEXT_N4_SOURCE_REPOSITORY) << "\",\n"
         << "    \"ror_ref\": \""
         << JsonEscape(ROR_OGRE_NEXT_N4_SOURCE_REF) << "\",\n"
         << "    \"ror_commit\": \""
         << JsonEscape(ROR_OGRE_NEXT_N4_SOURCE_COMMIT) << "\",\n"
         << "    \"relevant_source_clean\": "
         << (ROR_OGRE_NEXT_N4_RELEVANT_SOURCE_CLEAN ? "true" : "false")
         << ",\n"
         << "    \"relevant_source_manifest_sha256\": \""
         << ROR_OGRE_NEXT_N4_SOURCE_MANIFEST_SHA256 << "\",\n"
         << "    \"ogre_next_commit\": \""
         << ROR_OGRE_NEXT_N1_OGRE_COMMIT << "\",\n"
         << "    \"build_artifact\": \"ror_ogre_next_metal_n4_directional_shadow_smoke\",\n"
         << "    \"build_artifact_bytes\": " << executable.bytes << ",\n"
         << "    \"build_artifact_sha256\": \"" << executable.sha256
         << "\"\n  }";
  return report.str();
}

std::string SkipReport(const OgreNextMetalRayTracingEvidence &evidence,
                       const RenderOperationResult &initialization,
                       const FileDigest &executable) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_metal_rt_n4_directional_shadow.v1\",\n"
         << "  \"status\": \"skip\",\n"
         << "  \"scope\": \"same-device Metal two-BLAS directional hard shadow; no GI, reflection, denoising, multi-bounce, soft-shadow, or material-parity claim\",\n"
         << "  \"reason\": \"" << JsonEscape(initialization.detail)
         << "\",\n"
         << ProvenanceJson(executable) << ",\n"
         << "  \"device_name\": \"" << JsonEscape(evidence.device_name)
         << "\",\n"
         << "  \"required_apple_gpu_family\": 9,\n"
         << "  \"required_metal_ray_tracing\": true,\n"
         << "  \"required_visibility_format\": \"R16_FLOAT\"\n}\n";
  return report.str();
}

std::string PassReport(const OgreNextMetalRayTracingEvidence &evidence,
                       const ShadowMetrics &metrics,
                       const NativeRayTracingCapabilityReport &capabilities,
                       const FileDigest &executable) {
  std::ostringstream report;
  report << "{\n"
         << "  \"schema\": \"ror.ogre_next_metal_rt_n4_directional_shadow.v1\",\n"
         << "  \"status\": \"pass\",\n"
         << "  \"scope\": \"same-device Metal two-BLAS hard directional visibility applied to the exact UI-free Ogre-Next HDR target; no GI, reflection, denoising, multi-bounce, soft-shadow, or material-parity claim\",\n"
         << ProvenanceJson(executable) << ",\n"
         << "  \"device\": {\"name\": \""
         << JsonEscape(evidence.device_name)
         << "\", \"same_ogre_device\": true, \"same_ogre_queue\": true, \"apple_family_9\": true},\n"
         << "  \"raster_contract\": {\"raster_feature_tier\": \"MODERN_PBR_RT4_V1\", \"native_feature_tier\": \"METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW\", \"directional_shadow_mode\": \"DISABLED\", \"pssm_enabled\": false, \"vertex_layout\": \"POSITION_NORMAL_TANGENT_UV0_FLOAT32_48\", \"vertex_stride_bytes\": 48},\n"
         << "  \"native_contract\": {\"version\": 1, \"backend\": \"METAL\", \"tier\": \"NATIVE_DIRECTIONAL_HARD_SHADOW_V1\", \"blas_count\": 2, \"tlas_instance_count\": 2, \"primary_camera_rays_per_sample\": 1, \"secondary_directional_visibility_rays_per_sample\": 1, \"receiver_instance_id\": 1, \"occluder_instance_id\": 2},\n"
         << "  \"artifacts\": {\n"
         << "    \"raster\": {\"format\": \"RGBA16_FLOAT\", \"bytes\": "
         << evidence.raster_readback_bytes.size() << ", \"sha256\": \""
         << metrics.raster_sha256 << "\"},\n"
         << "    \"visibility\": {\"format\": \"R16_FLOAT\", \"bytes\": "
         << evidence.visibility_readback_bytes.size()
         << ", \"sha256\": \"" << metrics.visibility_sha256
         << "\", \"visible_r16_bits\": \"0x3c00\", \"occluded_r16_bits\": \"0x0000\"},\n"
         << "    \"ray_lineage\": {\"format\": \"R32_UINT\", \"bytes\": "
         << evidence.directional_lineage_readback_bytes.size()
         << ", \"sha256\": \"" << metrics.lineage_sha256 << "\"},\n"
         << "    \"hybrid\": {\"format\": \"RGBA16_FLOAT\", \"bytes\": "
         << evidence.hybrid_readback_bytes.size() << ", \"sha256\": \""
         << metrics.hybrid_sha256 << "\"}\n  },\n"
         << "  \"coverage\": {\"width\": " << kWidth
         << ", \"height\": " << kHeight
         << ", \"pixels\": " << metrics.pixel_count
         << ", \"receiver_visible_pixels\": " << metrics.visible_pixels
         << ", \"occluded_pixels\": " << metrics.occluded_pixels
         << ", \"primary_miss_pixels\": 0},\n"
         << "  \"samples\": [\n";
  for (std::size_t index = 0U;
       index < evidence.directional_shadow_samples.size(); ++index) {
    const NativeDirectionalShadowPassContract &sample =
        evidence.directional_shadow_samples[index];
    report << "    {\"x\": "
           << evidence.directional_shadow_sample_x[index]
           << ", \"y\": " << evidence.directional_shadow_sample_y[index]
           << ", \"visibility\": \""
           << (sample.visibility == NativeDirectionalShadowVisibility::VISIBLE
                   ? "VISIBLE"
                   : "OCCLUDED")
           << "\", \"visibility_r16_bits\": \""
           << Hex16(sample.native_visibility_r16_bits)
           << "\", \"secondary_blocker_instance_id\": "
           << sample.secondary_blocker_instance_id
           << ", \"raster_rgba16_bits\": ";
    WritePixelJson(report, sample.raster_rgba16);
    report << ", \"hybrid_rgba16_bits\": ";
    WritePixelJson(report, sample.native_hybrid_rgba16);
    report << ", \"portable_contract_validated\": true}"
           << (index + 1U == evidence.directional_shadow_samples.size()
                   ? "\n"
                   : ",\n");
  }
  report << "  ],\n"
         << "  \"proof\": {\"full_view_receiver\": true, \"partial_distinct_occluder\": true, \"every_visibility_texel_canonical_r16\": true, \"visible_preserves_exact_rgba16\": true, \"occluded_zeros_rgb_preserves_alpha\": true, \"visible_and_occluded_sample_contracts_validated\": true, \"exact_exported_dual_geometry_used\": true, \"exact_exported_color_image_used\": true, \"gpu_composite_not_cpu_postprocess\": true, \"view_dependent_output_ready\": "
         << (capabilities.view_dependent_output_ready ? "true" : "false")
         << ", \"hybrid_composite_ready\": "
         << (capabilities.hybrid_composite_ready ? "true" : "false")
         << "}\n}\n";
  return report.str();
}

std::pair<std::string, int> Run(const Arguments &arguments) {
  const FileDigest executable = HashFile(arguments.executable_path);

  OgreNextN1Configuration configuration{
      arguments.media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
  configuration.directional_shadow_mode =
      OgreNextDirectionalShadowMode::DISABLED;
  OgreNextN1Frontend frontend(
      configuration,
      OgreNextNativeFeatureTier::
          METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW);
  FrontendInitializationRequest initialization;
  initialization.initial_width = kWidth;
  initialization.initial_height = kHeight;
  initialization.maximum_frames_in_flight = 1U;
  initialization.headless = true;
  initialization.vertical_sync = false;
  RequireSuccess(frontend.Initialize(initialization), "frontend Initialize");
  RequireSuccess(frontend.SynchronizeAssets(MakeCatalog()),
                 "frontend SynchronizeAssets");
  const OgreNextPssmShadowRuntimeAudit pssm_audit =
      frontend.QueryDirectionalShadowAudit();
  Require(pssm_audit.configured_mode ==
                  OgreNextDirectionalShadowMode::DISABLED &&
              pssm_audit.shadow_frames_completed == 0U,
          "N4 frontend unexpectedly enabled the PSSM fallback");

  NativeRenderInterop *interop = frontend.GetNativeInterop();
  Require(interop != nullptr, "N4 frontend did not expose native interop");
  const NativeInteropCapabilityReport interop_capabilities =
      interop->QueryCapabilities();
  Require(interop_capabilities.exports_color_images &&
              interop_capabilities.supports_read_write_color_images,
          "N4 frontend did not expose the exact read/write image contract");

  OgreNextMetalRayTracingBackend backend(
      OgreNextMetalRayTracingMode::N4_DIRECTIONAL_HARD_SHADOW);
  const RenderOperationResult initialized = backend.Initialize(*interop);
  if (!initialized && initialized.code == RenderOperationCode::UNSUPPORTED) {
    const OgreNextMetalRayTracingEvidence evidence = backend.evidence();
    RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                   "unsupported frontend Shutdown");
    return {SkipReport(evidence, initialized, executable),
            kCapabilitySkipExitCode};
  }
  RequireSuccess(initialized, "backend Initialize");

  std::shared_ptr<const SceneSnapshot> scene = MakeScene();
  RenderFrameRequest frame = MakeFrame(scene);
  const std::weak_ptr<const SceneSnapshot> scene_weak = scene;
  RenderFrameOutput raster_output;
  RequireSuccess(frontend.Render(frame, raster_output), "N4 raster frame");
  RenderFrameOutput hybrid_output;
  RequireSuccess(backend.Render(MakeRayRequest(frame), hybrid_output),
                 "N4 directional shadow");
  OgreNextMetalRayTracingEvidence evidence = backend.evidence();
  const ShadowMetrics metrics =
      ValidateEvidence(evidence, raster_output, hybrid_output);
  const NativeRayTracingCapabilityReport capabilities =
      backend.QueryCapabilities();
  Require(capabilities.hardware_accelerated &&
              capabilities.maximum_instances == 2U &&
              capabilities.dispatch_readback_probe_passed &&
              capabilities.geometry_interop_ready &&
              capabilities.view_dependent_output_ready &&
              capabilities.hybrid_composite_ready,
          "N4 readiness was published without complete native evidence");

  WriteBinary(arguments.raster_path, evidence.raster_readback_bytes);
  WriteBinary(arguments.visibility_path, evidence.visibility_readback_bytes);
  WriteBinary(arguments.lineage_path,
              evidence.directional_lineage_readback_bytes);
  WriteBinary(arguments.hybrid_path, evidence.hybrid_readback_bytes);
  const std::string report =
      PassReport(evidence, metrics, capabilities, executable);

  // Do not let the report-side copy mask ownership retained by either runtime.
  evidence.image_request.scene_snapshot.reset();
  evidence.image_export.scene_snapshot.reset();
  RequireSuccess(backend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "backend Shutdown");
  Require(!backend.evidence().image_request.scene_snapshot &&
              !backend.evidence().image_export.scene_snapshot,
          "N4 backend shutdown retained the exact world snapshot");
  frame.scene_snapshot.reset();
  scene.reset();
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "frontend Shutdown");
  Require(scene_weak.expired(),
          "N4 successful teardown retained the world snapshot");
  return {report, EXIT_SUCCESS};
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
    report << "{\n  \"schema\": \"ror.ogre_next_metal_rt_n4_directional_shadow.v1\",\n"
           << "  \"status\": \"fail\",\n"
           << "  \"error\": \"" << JsonEscape(error.what())
           << "\"\n}\n";
    try {
      WriteText(report_path, report.str());
    } catch (...) {
    }
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
