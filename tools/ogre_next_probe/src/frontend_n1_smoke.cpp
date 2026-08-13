/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextN1Frontend.h"
#include "OgreNextN1NativeInterop.h"
#include "OgreNextN1Policy.h"
#include "OgreNextReflectionProbeRuntime.h"
#include "Ogre14GraphicsSceneSource.h"
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
#include <map>
#include <set>
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
constexpr std::uint64_t kRetirementRegistryId =
    UINT64_C(0x5254345F52455449);
constexpr std::uint64_t kDynamicRegistryId =
    UINT64_C(0x44594E5F4D455348);
constexpr std::uint64_t kDisplayDomainRegistryId =
    UINT64_C(0x444953505F554E4C);

struct Arguments {
  std::string media_root;
  std::string image_path;
  std::string report_path;
  std::string evidence_path;
  std::string reflection_evidence_path;
  std::string compositor_evidence_path;
  std::string analytic_sky_evidence_path;
  std::string analytic_sky_image_path;
  bool modern_pbr = false;
};

struct Metrics {
  std::size_t distinct_rgb = 0U;
  std::size_t non_background_pixels = 0U;
  float minimum_luminance = std::numeric_limits<float>::infinity();
  float maximum_luminance = -std::numeric_limits<float>::infinity();
  std::uint64_t fnv1a64 = UINT64_C(14695981039346656037);
  std::uint64_t attachment_fnv1a64 = UINT64_C(14695981039346656037);
  std::vector<std::uint8_t> rgb;
  std::vector<std::uint8_t> attachment_bytes;
};

struct ReflectionSectionMetrics final {
  std::uint64_t exact_fnv1a64 = UINT64_C(14695981039346656037);
  std::uint64_t finite_component_count = 0U;
  std::uint64_t nonzero_rgb_component_count = 0U;
  std::size_t distinct_texel_count = 0U;
  float max_absolute_rgb = 0.0F;
};

struct VariantEvidence final {
  std::string name;
  std::string changed_input;
  std::uint64_t asset_sequence = 0U;
  Metrics hdr;
  Metrics sdr;
  std::size_t hdr_changed_pixels = 0U;
  std::size_t sdr_changed_pixels = 0U;
};

struct SmokeResult final {
  Metrics hdr;
  Metrics sdr;
  std::vector<VariantEvidence> variants;
  OgreNextN1TextureAllocationAudit texture_allocations;
  OgreNextReflectionProbeAudit reflection_probes;
  OgreNextReflectionProbeCaptureEvidence reflection_capture;
  bool reflection_same_device_deterministic_replay = false;
  OgreNextN1TextureAllocationAudit replacement_final_audit;
  bool live_replacement_retirement = false;
  struct DynamicMeshEvidence final {
    Metrics base;
    Metrics deformed;
    std::size_t changed_pixels = 0U;
    bool base_exact_replay = false;
    bool deformed_exact_replay = false;
  } dynamic_mesh;
  struct TextureRetirementEvidence final {
    OgreNextN1TextureAllocationAudit initial;
    OgreNextN1TextureAllocationAudit expanded;
    OgreNextN1TextureAllocationAudit restored;
    OgreNextN1TextureAllocationAudit first_shutdown;
    OgreNextN1TextureAllocationAudit restarted;
    OgreNextN1TextureAllocationAudit final_shutdown;
    OgreNextN1NormalUploadAudit expanded_normal_upload;
    bool exact_extent_and_mip_transitions = false;
    bool renders_through_transitions_and_restart = false;
    bool old_names_rejected = false;
  } retirement;
  struct TextureUploadRollbackStageEvidence final {
    std::string name;
    OgreNextN1TextureAllocationAudit after_failure;
    OgreNextN1TextureAllocationAudit after_retry;
    OgreNextN1TextureAllocationAudit after_replacement;
    OgreNextN1TextureAllocationAudit after_shutdown;
  };
  std::vector<TextureUploadRollbackStageEvidence> texture_upload_rollback;
  struct TangentHandednessEvidence final {
    Metrics positive_hdr;
    Metrics positive_sdr;
    Metrics negative_hdr;
    Metrics negative_sdr;
    std::size_t hdr_changed_pixels = 0U;
    std::size_t sdr_changed_pixels = 0U;
    bool only_tangent_w_changed = false;
  } tangent_handedness;
  struct DisplayDomainUnlitEvidence final {
    std::array<float, 3U> encoded_filtered{};
    std::array<float, 3U> filter_then_eotf{};
    std::array<float, 3U> decode_before_filter{};
    std::size_t matching_foreground_pixels = 0U;
    std::size_t decode_before_filter_pixels = 0U;
    bool complete_unorm_mips_uploaded = false;
    bool full32_after_filter_shader_executed = false;
    bool alpha_untouched_opaque = false;
    bool no_cast_or_receive_shadow_flags = false;
    bool usage_transition_rollback_exact = false;
    bool usage_transition_commit_exact = false;
  } display_domain_unlit;
  struct AnalyticSkyEvidence final {
    OgreNextAnalyticSkyRuntimeAudit first_committed;
    OgreNextAnalyticSkyRuntimeAudit final_committed;
    Metrics camera_facing_sunless_hdr;
    Metrics camera_facing_sun_hdr;
    Metrics camera_facing_sun_sdr;
    std::uint32_t visual_width = 0U;
    std::uint32_t visual_height = 0U;
    std::size_t hemisphere_covered_pixels = 0U;
    std::size_t hemisphere_gradient_rows = 0U;
    std::size_t sun_changed_pixels = 0U;
    std::size_t sun_changed_pixels_alpha_exact_one = 0U;
    std::size_t sun_hdr_opaque_alpha_pixels = 0U;
    std::uint32_t rollback_stages_verified = 0U;
    bool rollback_publication_unchanged = false;
    bool rollback_lifetimes_balanced = false;
    bool clean_retry = false;
    bool broad_hemisphere_coverage = false;
    bool visible_sun_effect = false;
    bool visible_sun_alpha_exact_one = false;
    bool production_default_gpu_content_readbacks_zero = false;
  } analytic_sky;
  bool non_uniform_scale_rejected_before_submission = false;
  struct HdrCompositorEvidence final {
    OgreNextHdrCompositorAudit initialized;
    OgreNextHdrCompositorAudit committed;
    OgreNextNativeLightingPassAudit lighting;
    OgreNextHdrLightingSplitContentEvidence split_content;
    std::array<std::uint64_t, 4U> split_content_fnv1a64{};
    std::size_t split_rgb_channels_verified = 0U;
    std::size_t positive_sun_direct_pixels = 0U;
    bool canonical_split_alpha = false;
    Metrics first;
    Metrics final;
    Metrics ui_overlay_control;
    std::size_t exposure_changed_pixels = 0U;
    std::size_t ui_overlay_control_changed_pixels = 0U;
    std::size_t ui_overlay_control_magenta_pixels = 0U;
    std::uint32_t initialization_failure_stages_verified = 0U;
    bool same_object_reinitialize_verified = false;
    bool frame_commit_prepare_failure_verified = false;
    bool aborted_hdr_audit_unchanged = false;
    bool aborted_reflection_audit_unchanged = false;
    bool aborted_submission_uncommitted = false;
    bool aborted_output_unchanged = false;
    bool post_render_failure_fault_latched = false;
    bool suspend_restore_preserved_graph = false;
    bool invalid_resize_rollback_verified = false;
    bool resize_rebuild_verified = false;
    bool resized_frame_verified = false;
    bool clean_shutdown = false;
  } hdr_compositor;
};

enum class TextureVariant : std::uint8_t {
  BASELINE,
  BASE_COLOR,
  ROUGHNESS_G,
  METALLIC_B,
  EMISSIVE,
  NORMAL_RG,
  SAMPLER_UV,
};

struct VariantSpec final {
  TextureVariant variant;
  const char *name;
  const char *changed_input;
  std::uint64_t sequence;
  std::uint64_t material_revision;
  std::uint64_t base_color_revision;
  std::uint64_t packed_revision;
  std::uint64_t emissive_revision;
  std::uint64_t normal_revision;
  std::uint64_t sampler_revision;
  std::uint64_t expected_native_creates;
  std::uint64_t expected_native_destroys;
};

constexpr std::array<VariantSpec, 7U> kVariantSpecs{{
    {TextureVariant::BASELINE, "baseline", "none", 1U, 1U, 1U, 1U, 1U,
     1U, 1U, 5U, 0U},
    {TextureVariant::BASE_COLOR, "base_color", "base_color_rgb", 2U, 2U,
     2U, 1U, 1U, 1U, 1U, 6U, 1U},
    {TextureVariant::ROUGHNESS_G, "roughness_g", "packed_green_roughness",
     3U, 3U, 3U, 2U, 1U, 1U, 1U, 9U, 4U},
    {TextureVariant::METALLIC_B, "metallic_b", "packed_blue_metallic", 4U,
     4U, 3U, 3U, 1U, 1U, 1U, 11U, 6U},
    {TextureVariant::EMISSIVE, "emissive", "emissive_rgb", 5U, 5U, 3U,
     4U, 2U, 1U, 1U, 14U, 9U},
    {TextureVariant::NORMAL_RG, "normal_rg", "canonical_positive_z_normal_rg",
     6U, 6U, 3U, 4U, 3U, 2U, 1U, 16U, 11U},
    {TextureVariant::SAMPLER_UV, "sampler_uv", "sampler_address_over_uv0",
     7U, 7U, 3U, 4U, 3U, 3U, 2U, 17U, 12U},
}};

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
    } else if (option == "--evidence" && index + 1 < argc) {
      arguments.evidence_path = argv[++index];
    } else if (option == "--reflection-evidence" && index + 1 < argc) {
      arguments.reflection_evidence_path = argv[++index];
    } else if (option == "--compositor-evidence" && index + 1 < argc) {
      arguments.compositor_evidence_path = argv[++index];
    } else if (option == "--analytic-sky-evidence" && index + 1 < argc) {
      arguments.analytic_sky_evidence_path = argv[++index];
    } else if (option == "--analytic-sky-output" && index + 1 < argc) {
      arguments.analytic_sky_image_path = argv[++index];
    } else if (option == "--modern-pbr") {
      arguments.modern_pbr = true;
    } else {
      Fail("usage: ror_ogre_next_frontend_n1_smoke --media-root ABSOLUTE_PATH [--modern-pbr --evidence ISOLATION.bin --reflection-evidence REFLECTION.bin --compositor-evidence HDR.bin --analytic-sky-evidence SKY.bin --analytic-sky-output SKY.ppm] [--output FRAME.ppm] [--report REPORT.json]");
    }
  }
  if (arguments.media_root.empty()) {
    Fail("--media-root is required for the relocatable N1 frontend");
  }
  if (arguments.modern_pbr && arguments.evidence_path.empty()) {
    Fail("--evidence is required for exact RT4/V1 texture-isolation output");
  }
  if (arguments.modern_pbr && arguments.reflection_evidence_path.empty()) {
    Fail("--reflection-evidence is required for native PCC capture output");
  }
  if (arguments.modern_pbr && arguments.compositor_evidence_path.empty()) {
    Fail("--compositor-evidence is required for exact RT4/V1 HDR output");
  }
  if (arguments.modern_pbr &&
      arguments.analytic_sky_evidence_path.empty()) {
    Fail("--analytic-sky-evidence is required for exact RT4/V1 sky output");
  }
  if (arguments.modern_pbr && arguments.analytic_sky_image_path.empty()) {
    Fail("--analytic-sky-output is required for the committed RT4/V1 sky visual");
  }
  return arguments;
}

RenderAssetId AssetId(std::uint64_t low) {
  return RenderAssetId::FromWords(UINT64_C(0x4E315F4153534554), low);
}

RenderAssetReference AssetRef(RenderAssetKind kind, std::uint64_t low,
                              std::uint64_t revision = 1U) {
  return RenderAssetReference::Create(kind, AssetId(low), revision);
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
        {-0.35F, 1.35F},
        {1.65F, 1.35F},
        {0.5F, -0.35F},
    };
  }
  mesh.indices = {0U, 1U, 2U};
  return mesh;
}

MaterialDescriptor MakeMaterial(bool modern_pbr = false,
                                const VariantSpec *variant = nullptr) {
  MaterialDescriptor material;
  material.debug_name = "N1 texture-free emissive metallic-roughness PBS";
  material.base_color_factor = {0.05F, 0.32F, 0.92F, 1.0F};
  material.metallic_factor = 0.2F;
  material.roughness_factor = 0.28F;
  material.double_sided = true;
  material.emissive_factor = {0.78F, 0.12F, 0.035F};
  // The GitHub macOS arm64 runner exposes Apple's paravirtual Metal device.
  // That driver currently produces roughly one third of the physical Apple M5
  // luminance for this texture-free emissive fixture.  Keep the source energy
  // far enough above display white that both devices independently prove the
  // RGBA16_FLOAT attachment is scene-linear and unclamped.  This smoke checks
  // HDR storage, not cross-device photometric parity; that is a separate V1
  // backend-oracle gate.
  material.emissive_strength = 24.0F;
  if (modern_pbr) {
    Require(variant != nullptr, "RT4/V1 material lacks its revision plan");
    material.debug_name = "RT4/V1 texture-backed metallic-roughness PBS";
    material.base_color_factor = {0.7F, 0.8F, 0.9F, 1.0F};
    material.metallic_factor = 0.85F;
    material.roughness_factor = 0.65F;
    material.emissive_factor = {1.0F, 0.7F, 0.4F};
    // Keep the independent texture-backed RT4 fixture above display white on
    // both the hosted paravirtual Metal device and physical Apple silicon.
    // Input isolation still changes exactly one texture role at a time; this
    // scalar is identical across all variants.
    material.emissive_strength = 6.0F;
    material.base_color_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 3U,
                 variant->base_color_revision);
    material.base_color_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U,
                 variant->sampler_revision);
    material.metallic_roughness_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 4U, variant->packed_revision);
    material.metallic_roughness_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U,
                 variant->sampler_revision);
    material.normal_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 9U, variant->normal_revision);
    material.normal_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U,
                 variant->sampler_revision);
    material.normal_scale = 1.0F;
    material.emissive_texture.texture =
        AssetRef(RenderAssetKind::TEXTURE, 5U,
                 variant->emissive_revision);
    material.emissive_texture.sampler =
        AssetRef(RenderAssetKind::SAMPLER, 6U,
                 variant->sampler_revision);
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

RenderAssetDelta MakeDisplayDomainUnlitCatalog() {
  RenderAssetDelta delta;
  delta.registry_id = kDisplayDomainRegistryId;
  delta.sequence = 1U;
  delta.full_snapshot = true;

  MeshResourceDescriptor mesh = MakeMesh(true);
  mesh.debug_name = "RT4/V1 constant-UV display-domain Unlit triangle";
  mesh.texture_coordinates_0.assign(mesh.positions.size(), Float2{0.5F, 0.5F});
  RenderAssetMutation mesh_mutation;
  mesh_mutation.asset = AssetRef(RenderAssetKind::MESH, 1U);
  mesh_mutation.payload = std::move(mesh);
  delta.mutations.push_back(std::move(mesh_mutation));

  MaterialDescriptor material;
  material.debug_name = "RT4/V1 exact display-domain Unlit";
  material.model = MaterialModel::UNLIT;
  material.base_color_transfer =
      BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE;
  material.base_color_texture.texture = AssetRef(RenderAssetKind::TEXTURE, 3U);
  material.base_color_texture.sampler = AssetRef(RenderAssetKind::SAMPLER, 4U);
  RenderAssetMutation material_mutation;
  material_mutation.asset = AssetRef(RenderAssetKind::MATERIAL, 2U);
  material_mutation.payload = std::move(material);
  delta.mutations.push_back(std::move(material_mutation));

  TextureResourceDescriptor texture;
  texture.debug_name = "RT4/V1 complete display-domain RGBA8 mip chain";
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = 2U;
  texture.height = 2U;
  TextureMipLevelDescriptor base;
  base.width = 2U;
  base.height = 2U;
  base.row_pitch_bytes = 8U;
  base.layer_pitch_bytes = 16U;
  base.bytes = {0U,   32U,  64U,  255U, 255U, 96U,  64U,  255U,
                0U,   160U, 192U, 255U, 255U, 224U, 192U, 255U};
  texture.mip_levels.push_back(std::move(base));
  TextureMipLevelDescriptor last;
  last.width = 1U;
  last.height = 1U;
  last.row_pitch_bytes = 4U;
  last.layer_pitch_bytes = 4U;
  last.bytes = {250U, 7U, 201U, 255U};
  texture.mip_levels.push_back(std::move(last));
  RenderAssetMutation texture_mutation;
  texture_mutation.asset = AssetRef(RenderAssetKind::TEXTURE, 3U);
  texture_mutation.payload = std::move(texture);
  delta.mutations.push_back(std::move(texture_mutation));

  SamplerResourceDescriptor sampler;
  sampler.debug_name = "RT4/V1 display-domain bilinear nearest-mip clamp";
  sampler.mip_filter = SamplerFilter::NEAREST;
  sampler.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_w = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.maximum_lod = 1.0F;
  RenderAssetMutation sampler_mutation;
  sampler_mutation.asset = AssetRef(RenderAssetKind::SAMPLER, 4U);
  sampler_mutation.payload = std::move(sampler);
  delta.mutations.push_back(std::move(sampler_mutation));
  return delta;
}

RenderAssetDelta MakeDisplayDomainTransferCatalog(
    BaseColorTransfer transfer, std::uint64_t sequence,
    std::uint64_t material_revision) {
  RenderAssetDelta delta = MakeDisplayDomainUnlitCatalog();
  delta.sequence = sequence;
  const auto material_record = std::find_if(
      delta.mutations.begin(), delta.mutations.end(),
      [](const RenderAssetMutation &mutation) {
        return mutation.asset.kind == RenderAssetKind::MATERIAL;
      });
  Require(material_record != delta.mutations.end(),
          "display-domain role-transition catalog lost its material");
  RenderAssetMutation &material_mutation = *material_record;
  material_mutation.asset =
      AssetRef(RenderAssetKind::MATERIAL, 2U, material_revision);
  MaterialDescriptor &material =
      std::get<MaterialDescriptor>(material_mutation.payload);
  material.base_color_transfer = transfer;
  material.model =
      transfer == BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE
          ? MaterialModel::UNLIT
          : MaterialModel::PBR_METALLIC_ROUGHNESS;
  material.debug_name =
      transfer == BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE
          ? "RT4/V1 exact display-domain Unlit role transition"
          : "RT4/V1 exact decode-before-filter PBR role transition";
  return delta;
}

TextureResourceDescriptor MakeRetirementTexture(std::uint64_t revision) {
  if (revision == 1U || revision == 3U) {
    std::vector<std::uint8_t> rgba =
        revision == 1U
            ? std::vector<std::uint8_t>{
                  180U, 128U, 244U, 255U, 128U, 180U, 244U, 255U,
                  64U, 128U, 238U, 255U, 128U, 64U, 238U, 255U}
            : std::vector<std::uint8_t>{
                  200U, 128U, 232U, 255U, 128U, 200U, 232U, 255U,
                  160U, 96U, 247U, 255U, 96U, 160U, 247U, 255U};
    TextureResourceDescriptor texture =
        MakeTexture(TextureColorSpace::LINEAR, std::move(rgba));
    texture.debug_name =
        "RT4 normal retirement 2x2 one-mip padded-row texture";
    return texture;
  }
  Require(revision == 2U, "RT4 retirement requested an unknown revision");
  TextureResourceDescriptor texture;
  texture.debug_name =
      "RT4 normal retirement 4x2 two-mip padded-row texture";
  texture.color_space = TextureColorSpace::LINEAR;
  texture.width = 4U;
  texture.height = 2U;

  TextureMipLevelDescriptor level_zero;
  level_zero.width = 4U;
  level_zero.height = 2U;
  level_zero.row_pitch_bytes = 20U;
  level_zero.layer_pitch_bytes = 40U;
  level_zero.bytes.assign(40U, 0xA5U);
  const std::array<std::uint8_t, 32U> level_zero_rgba{{
      180U, 180U, 231U, 255U, 200U, 160U, 227U, 255U,
      160U, 200U, 227U, 255U, 220U, 128U, 215U, 255U,
      128U, 220U, 215U, 255U, 64U, 128U, 238U, 255U,
      128U, 64U, 238U, 255U, 160U, 96U, 247U, 255U,
  }};
  std::memcpy(level_zero.bytes.data(), level_zero_rgba.data(), 16U);
  std::memcpy(level_zero.bytes.data() + 20U,
              level_zero_rgba.data() + 16U, 16U);
  texture.mip_levels.push_back(std::move(level_zero));

  TextureMipLevelDescriptor level_one;
  level_one.width = 2U;
  level_one.height = 1U;
  level_one.row_pitch_bytes = 12U;
  level_one.layer_pitch_bytes = 12U;
  level_one.bytes = {
      96U, 160U, 247U, 255U, 200U, 96U, 228U, 255U,
      0x5AU, 0x5AU, 0x5AU, 0x5AU,
  };
  texture.mip_levels.push_back(std::move(level_one));
  return texture;
}

RenderAssetDelta MakeRetirementCatalog(std::uint64_t revision) {
  RenderAssetDelta delta;
  delta.registry_id = kRetirementRegistryId;
  delta.sequence = revision;
  delta.full_snapshot = true;

  RenderAssetMutation mesh;
  mesh.asset = AssetRef(RenderAssetKind::MESH, 1U);
  mesh.payload = MakeMesh(true);
  delta.mutations.push_back(std::move(mesh));

  RenderAssetMutation texture;
  texture.asset = AssetRef(RenderAssetKind::TEXTURE, 30U, revision);
  texture.payload = MakeRetirementTexture(revision);
  delta.mutations.push_back(std::move(texture));

  SamplerResourceDescriptor sampler_descriptor;
  sampler_descriptor.debug_name = "RT4 isolated retirement sampler";
  sampler_descriptor.address_u = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler_descriptor.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler_descriptor.maximum_lod = 1.0F;
  RenderAssetMutation sampler;
  sampler.asset = AssetRef(RenderAssetKind::SAMPLER, 31U);
  sampler.payload = sampler_descriptor;
  delta.mutations.push_back(std::move(sampler));

  MaterialDescriptor material = MakeMaterial();
  material.debug_name = "RT4 isolated retirement normal-map material";
  material.base_color_factor = {0.8F, 0.85F, 0.9F, 1.0F};
  material.metallic_factor = 0.15F;
  material.roughness_factor = 0.42F;
  material.emissive_factor = {0.0F, 0.0F, 0.0F};
  material.emissive_strength = 0.0F;
  material.normal_texture.texture =
      AssetRef(RenderAssetKind::TEXTURE, 30U, revision);
  material.normal_texture.sampler =
      AssetRef(RenderAssetKind::SAMPLER, 31U);
  material.normal_scale = 1.0F;
  RenderAssetMutation material_mutation;
  material_mutation.asset =
      AssetRef(RenderAssetKind::MATERIAL, 32U, revision);
  material_mutation.payload = std::move(material);
  delta.mutations.push_back(std::move(material_mutation));
  return delta;
}

RenderAssetDelta MakeCatalog(bool modern_pbr = false,
                             const VariantSpec *variant = nullptr) {
  if (modern_pbr) {
    Require(variant != nullptr, "RT4/V1 catalog lacks its variant plan");
  }
  RenderAssetDelta delta;
  delta.registry_id = kRegistryId;
  delta.sequence = modern_pbr ? variant->sequence : 1U;
  delta.full_snapshot = true;

  RenderAssetMutation mesh;
  mesh.asset = AssetRef(RenderAssetKind::MESH, 1U);
  mesh.payload = MakeMesh(modern_pbr);
  delta.mutations.push_back(std::move(mesh));

  RenderAssetMutation material;
  material.asset = AssetRef(RenderAssetKind::MATERIAL, 2U,
                            modern_pbr ? variant->material_revision : 1U);
  material.payload = MakeMaterial(modern_pbr, variant);
  delta.mutations.push_back(std::move(material));
  if (modern_pbr) {
    std::vector<std::uint8_t> base_color_bytes{
        255U, 28U, 12U, 255U, 18U, 220U, 42U, 255U,
        24U, 42U, 255U, 255U, 255U, 190U, 30U, 255U};
    if (variant->variant == TextureVariant::BASE_COLOR) {
      base_color_bytes = {
          12U, 238U, 255U, 255U, 245U, 18U, 210U, 255U,
          250U, 220U, 15U, 255U, 20U, 35U, 245U, 255U};
    }
    RenderAssetMutation base_color;
    base_color.asset = AssetRef(RenderAssetKind::TEXTURE, 3U,
                                variant->base_color_revision);
    base_color.payload = MakeTexture(
        TextureColorSpace::SRGB, std::move(base_color_bytes));
    delta.mutations.push_back(std::move(base_color));

    std::vector<std::uint8_t> packed_bytes{
        255U, 32U, 220U, 255U, 255U, 64U, 180U, 255U,
        255U, 96U, 96U, 255U, 255U, 128U, 40U, 255U};
    if (variant->variant == TextureVariant::ROUGHNESS_G) {
      for (std::size_t index = 1U; index < packed_bytes.size(); index += 4U) {
        packed_bytes[index] = 245U;
      }
    } else if (variant->variant == TextureVariant::METALLIC_B) {
      for (std::size_t index = 2U; index < packed_bytes.size(); index += 4U) {
        packed_bytes[index] = 5U;
      }
    }
    RenderAssetMutation metallic_roughness;
    metallic_roughness.asset = AssetRef(RenderAssetKind::TEXTURE, 4U,
                                        variant->packed_revision);
    metallic_roughness.payload = MakeTexture(
        TextureColorSpace::LINEAR, std::move(packed_bytes));
    delta.mutations.push_back(std::move(metallic_roughness));

    std::vector<std::uint8_t> emissive_bytes{
        255U, 96U, 12U, 255U, 18U, 255U, 80U, 255U,
        20U, 90U, 255U, 255U, 255U, 235U, 42U, 255U};
    if (variant->variant == TextureVariant::EMISSIVE) {
      emissive_bytes = {
          8U, 18U, 255U, 255U, 255U, 12U, 18U, 255U,
          20U, 255U, 30U, 255U, 255U, 25U, 220U, 255U};
    }
    RenderAssetMutation emissive;
    emissive.asset = AssetRef(RenderAssetKind::TEXTURE, 5U,
                              variant->emissive_revision);
    emissive.payload = MakeTexture(
        TextureColorSpace::SRGB, std::move(emissive_bytes));
    delta.mutations.push_back(std::move(emissive));

    std::vector<std::uint8_t> normal_bytes{
        128U, 128U, 255U, 255U, 128U, 128U, 255U, 255U,
        128U, 128U, 255U, 255U, 128U, 128U, 255U, 255U};
    if (variant->variant == TextureVariant::NORMAL_RG) {
      normal_bytes = {
          180U, 128U, 244U, 255U, 128U, 180U, 244U, 255U,
          64U, 128U, 238U, 255U, 128U, 64U, 238U, 255U};
    }
    RenderAssetMutation normal;
    normal.asset = AssetRef(RenderAssetKind::TEXTURE, 9U,
                            variant->normal_revision);
    normal.payload = MakeTexture(
        TextureColorSpace::LINEAR, std::move(normal_bytes));

    SamplerResourceDescriptor sampler_descriptor;
    sampler_descriptor.debug_name = "RT4/V1 controlled UV0 sampler";
    // REPEAT deliberately maps both authored out-of-range UV0 sides to
    // interior texels.  MIRRORED_REPEAT made the two top vertices converge on
    // nearly the same coordinate and the hosted Apple paravirtual Metal
    // rasterizer could quantize that fixture below the exact isolation
    // threshold even though the sampler block was bound correctly.
    sampler_descriptor.address_u =
        variant->variant == TextureVariant::SAMPLER_UV
            ? SamplerAddressMode::REPEAT
            : SamplerAddressMode::CLAMP_TO_EDGE;
    sampler_descriptor.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
    sampler_descriptor.address_w = SamplerAddressMode::REPEAT;
    sampler_descriptor.maximum_lod = 0.0F;
    RenderAssetMutation sampler;
    sampler.asset = AssetRef(RenderAssetKind::SAMPLER, 6U,
                             variant->sampler_revision);
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
    delta.mutations.push_back(std::move(normal));
  }
  return delta;
}

RenderAssetDelta MakeDynamicCatalog(bool modern_pbr) {
  RenderAssetDelta catalog = MakeCatalog(
      modern_pbr, modern_pbr ? &kVariantSpecs.front() : nullptr);
  catalog.registry_id = kDynamicRegistryId;
  const auto mesh = std::find_if(
      catalog.mutations.begin(), catalog.mutations.end(),
      [](const RenderAssetMutation &mutation) {
        return mutation.asset.kind == RenderAssetKind::MESH;
      });
  Require(mesh != catalog.mutations.end(),
          "dynamic mesh proof catalog lost its base mesh");
  std::get<MeshResourceDescriptor>(mesh->payload).dynamic = true;
  return catalog;
}

const RenderAssetMutation &MutationFor(const RenderAssetDelta &catalog,
                                       std::uint64_t low) {
  const auto found = std::find_if(
      catalog.mutations.begin(), catalog.mutations.end(),
      [low](const RenderAssetMutation &mutation) {
        return mutation.asset.id == AssetId(low);
      });
  Require(found != catalog.mutations.end(),
          "RT4/V1 controlled catalog lost a required asset");
  return *found;
}

void RequireEquivalentPayload(const RenderAssetPayload &lhs,
                              const RenderAssetPayload &rhs,
                              const std::string &label);

RenderAssetDelta MakeTangentHandednessCatalog(bool negative) {
  RenderAssetDelta catalog = MakeCatalog(true, &kVariantSpecs.front());
  catalog.sequence = negative ? 2U : 1U;
  for (RenderAssetMutation &mutation : catalog.mutations) {
    if (mutation.asset.id == AssetId(1U)) {
      MeshResourceDescriptor &mesh =
          std::get<MeshResourceDescriptor>(mutation.payload);
      for (Float4 &tangent : mesh.tangents) {
        tangent.w = negative ? -1.0F : 1.0F;
      }
      mutation.asset = AssetRef(RenderAssetKind::MESH, 1U,
                                negative ? 2U : 1U);
    } else if (mutation.asset.id == AssetId(9U)) {
      TextureResourceDescriptor &normal =
          std::get<TextureResourceDescriptor>(mutation.payload);
      constexpr std::array<std::uint8_t, 16U> kAsymmetricNormal{{
          180U, 180U, 231U, 255U, 160U, 200U, 227U, 255U,
          96U, 160U, 247U, 255U, 128U, 220U, 215U, 255U,
      }};
      TextureMipLevelDescriptor &mip = normal.mip_levels.front();
      std::memcpy(mip.bytes.data(), kAsymmetricNormal.data(), 8U);
      std::memcpy(mip.bytes.data() + mip.row_pitch_bytes,
                  kAsymmetricNormal.data() + 8U, 8U);
    }
  }
  return catalog;
}

void RequireControlledTangentHandednessCatalogs(
    const RenderAssetDelta &positive, const RenderAssetDelta &negative) {
  Require(positive.registry_id == negative.registry_id &&
              positive.sequence == 1U && negative.sequence == 2U &&
              positive.full_snapshot && negative.full_snapshot &&
              positive.mutations.size() == negative.mutations.size(),
          "RT4 tangent-handedness catalog envelope changed");
  for (std::size_t index = 0U; index < positive.mutations.size(); ++index) {
    const RenderAssetMutation &expected = positive.mutations[index];
    const RenderAssetMutation &actual = negative.mutations[index];
    Require(expected.asset.id == actual.asset.id &&
                expected.asset.kind == actual.asset.kind,
            "RT4 tangent-handedness asset identity changed");
    if (expected.asset.id != AssetId(1U)) {
      Require(expected.asset == actual.asset,
              "RT4 tangent-handedness changed a non-mesh revision");
      RequireEquivalentPayload(expected.payload, actual.payload,
                               "tangent-handedness non-mesh payload");
      continue;
    }
    Require(expected.asset.revision == 1U && actual.asset.revision == 2U,
            "RT4 tangent-handedness mesh revision plan drifted");
    const MeshResourceDescriptor &positive_mesh =
        std::get<MeshResourceDescriptor>(expected.payload);
    MeshResourceDescriptor normalized_negative =
        std::get<MeshResourceDescriptor>(actual.payload);
    Require(positive_mesh.tangents.size() == normalized_negative.tangents.size(),
            "RT4 tangent-handedness stream size changed");
    for (std::size_t tangent_index = 0U;
         tangent_index < positive_mesh.tangents.size(); ++tangent_index) {
      Require(positive_mesh.tangents[tangent_index].w == 1.0F &&
                  normalized_negative.tangents[tangent_index].w == -1.0F,
              "RT4 tangent-handedness fixture does not use exact signs");
      normalized_negative.tangents[tangent_index].w = 1.0F;
    }
    RequireEquivalentPayload(RenderAssetPayload{positive_mesh},
                             RenderAssetPayload{normalized_negative},
                             "tangent-handedness mesh attributes");
  }
}

void RequireEquivalentPayload(const RenderAssetPayload &lhs,
                              const RenderAssetPayload &rhs,
                              const std::string &label) {
  Require(EquivalentRenderAssetPayload(lhs, rhs),
          "RT4/V1 controlled " + label + " changed unexpectedly");
}

void RequireTextureOnlyChannelChange(
    const TextureResourceDescriptor &baseline,
    const TextureResourceDescriptor &variant, std::size_t allowed_channel,
    const std::string &label) {
  TextureResourceDescriptor normalized = variant;
  normalized.mip_levels = baseline.mip_levels;
  RequireEquivalentPayload(RenderAssetPayload{baseline},
                           RenderAssetPayload{normalized}, label + " metadata");
  Require(baseline.mip_levels.size() == variant.mip_levels.size(),
          "RT4/V1 controlled texture mip count changed");
  std::size_t changed = 0U;
  for (std::size_t mip_index = 0U;
       mip_index < baseline.mip_levels.size(); ++mip_index) {
    const TextureMipLevelDescriptor &expected =
        baseline.mip_levels[mip_index];
    const TextureMipLevelDescriptor &actual = variant.mip_levels[mip_index];
    Require(expected.bytes.size() == actual.bytes.size(),
            "RT4/V1 controlled texture byte count changed");
    for (std::size_t offset = 0U; offset < expected.bytes.size(); ++offset) {
      if (expected.bytes[offset] == actual.bytes[offset]) {
        continue;
      }
      const std::size_t row = offset / expected.row_pitch_bytes;
      const std::size_t row_offset = offset % expected.row_pitch_bytes;
      Require(row < expected.height && row_offset < expected.width * 4U &&
                  row_offset % 4U == allowed_channel,
              "RT4/V1 controlled texture changed padding, alpha, or the wrong packed channel");
      ++changed;
    }
  }
  Require(changed > 0U,
          "RT4/V1 controlled " + label + " variant changed no texels");
}

void RequireControlledCatalog(const RenderAssetDelta &baseline,
                              const RenderAssetDelta &variant,
                              const VariantSpec &spec) {
  Require(variant.registry_id == baseline.registry_id &&
              variant.full_snapshot &&
              variant.mutations.size() == baseline.mutations.size() &&
              variant.sequence == spec.sequence,
          "RT4/V1 controlled catalog envelope changed");
  RequireEquivalentPayload(MutationFor(baseline, 1U).payload,
                           MutationFor(variant, 1U).payload,
                           "geometry, normals, tangents, or UV0");

  const MaterialDescriptor &baseline_material =
      std::get<MaterialDescriptor>(MutationFor(baseline, 2U).payload);
  MaterialDescriptor normalized_material =
      std::get<MaterialDescriptor>(MutationFor(variant, 2U).payload);
  normalized_material.base_color_texture =
      baseline_material.base_color_texture;
  normalized_material.metallic_roughness_texture =
      baseline_material.metallic_roughness_texture;
  normalized_material.normal_texture = baseline_material.normal_texture;
  normalized_material.occlusion_texture = baseline_material.occlusion_texture;
  normalized_material.emissive_texture = baseline_material.emissive_texture;
  RequireEquivalentPayload(RenderAssetPayload{baseline_material},
                           RenderAssetPayload{normalized_material},
                           "material factors or constants");

  const std::uint64_t changed_texture =
      spec.variant == TextureVariant::BASE_COLOR
          ? 3U
          : spec.variant == TextureVariant::ROUGHNESS_G ||
                    spec.variant == TextureVariant::METALLIC_B
                ? 4U
                : spec.variant == TextureVariant::EMISSIVE
                      ? 5U
                      : spec.variant == TextureVariant::NORMAL_RG ? 9U : 0U;
  constexpr std::array<std::uint64_t, 4U> kControlledTextureIds{{3U, 4U,
                                                                 5U, 9U}};
  for (const std::uint64_t low : kControlledTextureIds) {
    const TextureResourceDescriptor &expected =
        std::get<TextureResourceDescriptor>(MutationFor(baseline, low).payload);
    const TextureResourceDescriptor &actual =
        std::get<TextureResourceDescriptor>(MutationFor(variant, low).payload);
    if (low != changed_texture) {
      RequireEquivalentPayload(RenderAssetPayload{expected},
                               RenderAssetPayload{actual},
                               "non-target texture");
      continue;
    }
    const std::size_t allowed_channel =
        spec.variant == TextureVariant::ROUGHNESS_G
            ? 1U
            : spec.variant == TextureVariant::METALLIC_B ? 2U : 0U;
    if (allowed_channel == 0U) {
      TextureResourceDescriptor normalized = actual;
      normalized.mip_levels = expected.mip_levels;
      RequireEquivalentPayload(RenderAssetPayload{expected},
                               RenderAssetPayload{normalized},
                               "target texture metadata");
      std::size_t changed_rgb = 0U;
      for (std::size_t mip_index = 0U;
           mip_index < expected.mip_levels.size(); ++mip_index) {
        const TextureMipLevelDescriptor &expected_mip =
            expected.mip_levels[mip_index];
        const TextureMipLevelDescriptor &actual_mip =
            actual.mip_levels[mip_index];
        Require(expected_mip.bytes.size() == actual_mip.bytes.size(),
                "RT4/V1 target texture byte count changed");
        for (std::size_t offset = 0U; offset < expected_mip.bytes.size();
             ++offset) {
          if (expected_mip.bytes[offset] == actual_mip.bytes[offset]) {
            continue;
          }
          const std::size_t row = offset / expected_mip.row_pitch_bytes;
          const std::size_t row_offset = offset % expected_mip.row_pitch_bytes;
          Require(row < expected_mip.height &&
                      row_offset < expected_mip.width * 4U &&
                      row_offset % 4U < 3U,
                  "RT4/V1 RGB texture variant changed padding or alpha");
          ++changed_rgb;
        }
      }
      Require(changed_rgb > 0U,
              "RT4/V1 RGB texture variant changed no RGB texels");
    } else {
      RequireTextureOnlyChannelChange(expected, actual, allowed_channel,
                                      spec.name);
    }
  }

  const SamplerResourceDescriptor &baseline_sampler =
      std::get<SamplerResourceDescriptor>(MutationFor(baseline, 6U).payload);
  SamplerResourceDescriptor normalized_sampler =
      std::get<SamplerResourceDescriptor>(MutationFor(variant, 6U).payload);
  if (spec.variant == TextureVariant::SAMPLER_UV) {
    Require(normalized_sampler.address_u != baseline_sampler.address_u,
            "RT4/V1 sampler/UV variant changed no addressing state");
    normalized_sampler.address_u = baseline_sampler.address_u;
  }
  RequireEquivalentPayload(RenderAssetPayload{baseline_sampler},
                           RenderAssetPayload{normalized_sampler},
                           "non-target sampler state");
  RequireEquivalentPayload(MutationFor(baseline, 7U).payload,
                           MutationFor(variant, 7U).payload,
                           "unreferenced shared-catalog texture");

  Require(MutationFor(variant, 2U).asset.revision ==
              spec.material_revision &&
              MutationFor(variant, 3U).asset.revision ==
                  spec.base_color_revision &&
              MutationFor(variant, 4U).asset.revision ==
                  spec.packed_revision &&
              MutationFor(variant, 5U).asset.revision ==
                  spec.emissive_revision &&
              MutationFor(variant, 9U).asset.revision ==
                  spec.normal_revision &&
              MutationFor(variant, 6U).asset.revision ==
                  spec.sampler_revision,
          "RT4/V1 controlled replacement revision plan drifted");
}

std::shared_ptr<const SceneSnapshot> MakeScene(std::uint64_t snapshot_id,
                                               bool shifted = false,
                                               bool modern_pbr = false,
                                               std::uint64_t asset_sequence = 1U,
                                               std::uint64_t material_revision = 1U,
                                               Matrix4x4 render_from_object =
                                                   Matrix4x4{},
                                               std::uint64_t mesh_revision = 1U,
                                               Float3 light_direction =
                                                   {0.0F, -0.8F, -0.6F},
                                               float exposure_compensation_ev =
                                                   0.0F,
                                               bool include_mesh = true,
                                               bool include_reflection_probe =
                                                   true,
                                               bool suppress_sun_disk = false,
                                               bool enable_shadows = false) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kRegistryId;
  descriptor.asset_sequence = asset_sequence;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds = static_cast<double>(snapshot_id) / 48.0;
  descriptor.environment.ambient_radiance = {0.03F, 0.04F, 0.055F};
  descriptor.environment.exposure_compensation_ev =
      exposure_compensation_ev;
  if (modern_pbr) {
    descriptor.environment.ambient_radiance = {0.01F, 0.012F, 0.015F};
    GraphicsSceneLightInput captured_sun;
    captured_sun.source_light_id = 1U;
    captured_sun.type = LightType::DIRECTIONAL;
    Require(NormalizePhotometricColorLinear({1.0F, 0.92F, 0.82F},
                                            captured_sun.color_linear),
            "RT4/V1 directional tint could not be normalized");
    captured_sun.intensity = 1024.0F;
    captured_sun.direction = light_direction;
    captured_sun.shadow_flags =
        enable_shadows ? LIGHT_SHADOW_STATIC_GEOMETRY : 0U;
    const ValidationResult sky =
        BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
            descriptor.environment.ambient_radiance, captured_sun,
            descriptor.environment);
    Require(sky.ok(), "RT4/V1 policy-v1 analytic sky could not be staged: " +
                          sky.field + ": " + sky.detail);
    if (suppress_sun_disk) {
      descriptor.environment.analytic_sky.sun_disk_radiance = {};
    }
    LightDescriptor light;
    light.light_id = captured_sun.source_light_id;
    light.type = captured_sun.type;
    light.color_linear = captured_sun.color_linear;
    light.intensity = captured_sun.intensity;
    light.direction = captured_sun.direction;
    light.shadow_flags = captured_sun.shadow_flags;
    descriptor.lights.push_back(light);

    if (include_reflection_probe) {
      ReflectionProbeRuntimeDescriptor probe;
      probe.probe_id = 1U;
      probe.content_revision = 1U;
      probe.capture_position_local = {0.0F, 0.0F, 1.0F};
      probe.influence_half_size = {3.0F, 3.0F, 3.0F};
      probe.influence_inner_fraction = {0.75F, 0.75F, 0.75F};
      probe.correction_shape_half_size = {4.0F, 4.0F, 4.0F};
      probe.resolution = 32U;
      probe.capture_near_meters = 0.05F;
      probe.capture_far_meters = 10.0F;
      descriptor.reflection_probes.push_back(probe);
    }
  }

  if (include_mesh) {
    MeshInstanceDescriptor instance;
    instance.instance_id = 1U;
    instance.mesh = AssetRef(RenderAssetKind::MESH, 1U, mesh_revision);
    instance.material = AssetRef(RenderAssetKind::MATERIAL, 2U,
                                 material_revision);
    instance.render_from_object = render_from_object;
    if (shifted) {
      instance.render_from_object.elements[12U] = 0.15F;
    }
    instance.previous_render_from_object = instance.render_from_object;
    instance.local_bounds = MakeMesh(modern_pbr).local_bounds;
    descriptor.mesh_instances.push_back(instance);
  }

  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  if (!result) {
    Fail("could not create N1 smoke scene: " + result.validation.field +
         ": " + result.validation.detail);
  }
  return result.snapshot;
}

std::shared_ptr<const SceneSnapshot> MakeDisplayDomainUnlitScene(
    std::uint64_t asset_sequence = 1U,
    std::uint64_t material_revision = 1U,
    std::uint64_t snapshot_id = 900U,
    Float3 ambient_radiance = Float3{}) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kDisplayDomainRegistryId;
  descriptor.asset_sequence = asset_sequence;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds =
      static_cast<double>(snapshot_id) / 48.0;
  descriptor.environment.ambient_radiance = ambient_radiance;
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = AssetRef(RenderAssetKind::MESH, 1U);
  instance.material =
      AssetRef(RenderAssetKind::MATERIAL, 2U, material_revision);
  instance.local_bounds = MakeMesh(true).local_bounds;
  instance.flags = MESH_INSTANCE_VISIBLE_IN_REFLECTIONS;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  if (!result) {
    Fail("could not create display-domain Unlit smoke scene: " +
         result.validation.field + ": " + result.validation.detail);
  }
  return result.snapshot;
}

std::shared_ptr<const SceneSnapshot>
MakeDynamicScene(std::uint64_t snapshot_id, bool modern_pbr,
                 bool deformed) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = snapshot_id;
  descriptor.asset_registry_id = kDynamicRegistryId;
  descriptor.asset_sequence = 1U;
  descriptor.simulation_tick = snapshot_id;
  descriptor.simulation_time_seconds =
      static_cast<double>(snapshot_id) / 48.0;
  descriptor.environment.ambient_radiance = {0.03F, 0.04F, 0.055F};

  MeshResourceDescriptor mesh = MakeMesh(modern_pbr);
  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = AssetRef(RenderAssetKind::MESH, 1U);
  instance.material = AssetRef(RenderAssetKind::MATERIAL, 2U);
  instance.deformation_revision = deformed ? 2U : 1U;
  if (deformed) {
    mesh.positions[2U] = {0.65F, 0.30F, 0.0F};
    mesh.local_bounds.minimum = {-1.15F, -0.85F, 0.0F};
    mesh.local_bounds.maximum = {1.15F, 0.30F, 0.0F};
  }
  instance.local_bounds = mesh.local_bounds;
  descriptor.mesh_instances.push_back(instance);

  if (deformed) {
    DynamicMeshUpdateDescriptor update;
    update.update_sequence = 1U;
    update.instance_id = instance.instance_id;
    update.mesh = instance.mesh;
    update.topology_revision = instance.topology_revision;
    update.deformation_revision = instance.deformation_revision;
    update.positions = mesh.positions;
    update.normals = mesh.normals;
    update.tangents = mesh.tangents;
    update.velocities = mesh.velocities;
    update.has_updated_bounds = true;
    update.updated_local_bounds = mesh.local_bounds;
    descriptor.dynamic_mesh_updates.push_back(std::move(update));
  }

  SceneSnapshotCreateResult result =
      CreateSceneSnapshot(std::move(descriptor));
  if (!result) {
    Fail("could not create full dynamic-mesh smoke scene: " +
         result.validation.field + ": " + result.validation.detail);
  }
  return result.snapshot;
}

std::shared_ptr<const SceneSnapshot>
MakeRetirementScene(std::uint64_t revision) {
  SceneSnapshotDescriptor descriptor;
  descriptor.snapshot_id = 900U + revision;
  descriptor.asset_registry_id = kRetirementRegistryId;
  descriptor.asset_sequence = revision;
  descriptor.simulation_tick = revision;
  descriptor.simulation_time_seconds = static_cast<double>(revision) / 48.0;
  descriptor.environment.ambient_radiance = {0.01F, 0.012F, 0.015F};
  LightDescriptor light;
  light.light_id = 1U;
  light.type = LightType::DIRECTIONAL;
  Require(NormalizePhotometricColorLinear({1.0F, 0.92F, 0.82F},
                                          light.color_linear),
          "RT4 retirement directional tint could not be normalized");
  light.intensity = 1024.0F;
  light.direction = {0.0F, 0.0F, -1.0F};
  light.shadow_flags = 0U;
  descriptor.lights.push_back(light);

  MeshInstanceDescriptor instance;
  instance.instance_id = 1U;
  instance.mesh = AssetRef(RenderAssetKind::MESH, 1U);
  instance.material = AssetRef(RenderAssetKind::MATERIAL, 32U, revision);
  instance.local_bounds = MakeMesh(true).local_bounds;
  descriptor.mesh_instances.push_back(instance);
  SceneSnapshotCreateResult result = CreateSceneSnapshot(std::move(descriptor));
  if (!result) {
    Fail("could not create RT4 retirement scene: " + result.validation.field +
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

void RequireControlledSceneAndView(const SceneSnapshot &baseline_scene,
                                   const SceneSnapshot &variant_scene,
                                   const RenderFrameRequest &baseline_frame,
                                   const RenderFrameRequest &variant_frame,
                                   bool allow_mesh_revision_change = false) {
  const SceneEnvironmentDescriptor &expected = baseline_scene.environment();
  const SceneEnvironmentDescriptor &actual = variant_scene.environment();
  Require(expected.ambient_radiance == actual.ambient_radiance &&
              expected.environment_intensity ==
                  actual.environment_intensity &&
              expected.environment_texture == actual.environment_texture &&
              expected.environment_sampler == actual.environment_sampler &&
              expected.exposure_compensation_ev ==
                  actual.exposure_compensation_ev &&
              expected.analytic_sky.enabled ==
                  actual.analytic_sky.enabled &&
              expected.analytic_sky.sun_light_id ==
                  actual.analytic_sky.sun_light_id &&
              expected.analytic_sky.zenith_radiance ==
                  actual.analytic_sky.zenith_radiance &&
              expected.analytic_sky.horizon_radiance ==
                  actual.analytic_sky.horizon_radiance &&
              expected.analytic_sky.ground_radiance ==
                  actual.analytic_sky.ground_radiance &&
              expected.analytic_sky.sun_disk_radiance ==
                  actual.analytic_sky.sun_disk_radiance &&
              expected.analytic_sky.sun_angular_radius_radians ==
                  actual.analytic_sky.sun_angular_radius_radians &&
              baseline_scene.lights().size() == 1U &&
              variant_scene.lights().size() == 1U,
          "RT4/V1 controlled environment or lights changed");
  Require(baseline_scene.reflection_probes().size() == 1U &&
              variant_scene.reflection_probes().size() == 1U &&
              AreReflectionProbeRuntimeDescriptorsEquivalent(
                  baseline_scene.reflection_probes().front(),
                  variant_scene.reflection_probes().front()),
          "RT4/V1 controlled reflection probe changed");
  const LightDescriptor &expected_light = baseline_scene.lights().front();
  const LightDescriptor &actual_light = variant_scene.lights().front();
  Require(expected_light.light_id == actual_light.light_id &&
              expected_light.type == actual_light.type &&
              expected_light.color_linear == actual_light.color_linear &&
              expected_light.intensity == actual_light.intensity &&
              expected_light.position == actual_light.position &&
              expected_light.previous_position ==
                  actual_light.previous_position &&
              expected_light.direction == actual_light.direction &&
              expected_light.previous_direction ==
                  actual_light.previous_direction &&
              expected_light.range == actual_light.range &&
              expected_light.inner_cone_radians ==
                  actual_light.inner_cone_radians &&
              expected_light.outer_cone_radians ==
                  actual_light.outer_cone_radians &&
              expected_light.shadow_flags == actual_light.shadow_flags,
          "RT4/V1 controlled directional light changed");
  Require(baseline_scene.mesh_instances().size() == 1U &&
              variant_scene.mesh_instances().size() == 1U,
          "RT4/V1 controlled instance count changed");
  const MeshInstanceDescriptor &expected_instance =
      baseline_scene.mesh_instances().front();
  const MeshInstanceDescriptor &actual_instance =
      variant_scene.mesh_instances().front();
  Require(expected_instance.instance_id == actual_instance.instance_id &&
              expected_instance.mesh.id == actual_instance.mesh.id &&
              expected_instance.mesh.kind == actual_instance.mesh.kind &&
              (allow_mesh_revision_change ||
               expected_instance.mesh.revision ==
                   actual_instance.mesh.revision) &&
              expected_instance.material.id == actual_instance.material.id &&
              expected_instance.material.kind == actual_instance.material.kind &&
              expected_instance.render_from_object ==
                  actual_instance.render_from_object &&
              expected_instance.previous_render_from_object ==
                  actual_instance.previous_render_from_object &&
              expected_instance.local_bounds == actual_instance.local_bounds &&
              expected_instance.visibility_mask ==
                  actual_instance.visibility_mask &&
              expected_instance.flags == actual_instance.flags,
          "RT4/V1 controlled geometry or transform changed");
  Require(baseline_frame.color_format == variant_frame.color_format &&
              baseline_frame.present == variant_frame.present &&
              baseline_frame.requested_outputs ==
                  variant_frame.requested_outputs &&
              baseline_frame.views.size() == 1U &&
              variant_frame.views.size() == 1U,
          "RT4/V1 controlled frame envelope changed");
  const CameraViewRequest &expected_view = baseline_frame.views.front();
  const CameraViewRequest &actual_view = variant_frame.views.front();
  Require(expected_view.view_id == actual_view.view_id &&
              expected_view.width == actual_view.width &&
              expected_view.height == actual_view.height &&
              expected_view.near_plane == actual_view.near_plane &&
              expected_view.far_plane == actual_view.far_plane &&
              expected_view.view_from_render == actual_view.view_from_render &&
              expected_view.previous_view_from_render ==
                  actual_view.previous_view_from_render &&
              expected_view.clip_from_view == actual_view.clip_from_view &&
              expected_view.previous_clip_from_view ==
                  actual_view.previous_clip_from_view &&
              expected_view.exposure == actual_view.exposure &&
              expected_view.temporal_jitter_pixels ==
                  actual_view.temporal_jitter_pixels &&
              expected_view.visibility_mask == actual_view.visibility_mask,
          "RT4/V1 controlled camera changed");
}

std::size_t CountChangedPixels(const std::vector<std::uint8_t> &baseline,
                               const std::vector<std::uint8_t> &variant,
                               std::size_t bytes_per_pixel) {
  Require(baseline.size() == variant.size() && bytes_per_pixel != 0U &&
              baseline.size() % bytes_per_pixel == 0U,
          "renderer evidence attachment layout changed");
  std::size_t changed = 0U;
  for (std::size_t offset = 0U; offset < baseline.size();
       offset += bytes_per_pixel) {
    if (!std::equal(baseline.begin() + static_cast<std::ptrdiff_t>(offset),
                    baseline.begin() + static_cast<std::ptrdiff_t>(
                                           offset + bytes_per_pixel),
                    variant.begin() + static_cast<std::ptrdiff_t>(offset))) {
      ++changed;
    }
  }
  return changed;
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

std::uint64_t HashBytes(const std::vector<std::uint8_t> &bytes) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const std::uint8_t value : bytes) {
    Hash(hash, value);
  }
  return hash;
}

std::uint64_t HashHalfWords(const std::vector<std::uint16_t> &words) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const std::uint16_t value : words) {
    Hash(hash, static_cast<std::uint8_t>(value & 0xffU));
    Hash(hash, static_cast<std::uint8_t>(value >> 8U));
  }
  return hash;
}

ReflectionSectionMetrics InspectReflectionSection(
    const std::vector<std::uint8_t> &bytes, std::size_t begin,
    std::size_t end, const char *section) {
  Require(begin <= end && end <= bytes.size() && (end - begin) % 8U == 0U,
          std::string("invalid RGBA16F reflection layout for ") + section);
  ReflectionSectionMetrics metrics;
  std::set<std::array<std::uint16_t, 4U>> distinct_texels;
  for (std::size_t offset = begin; offset < end; offset += 8U) {
    std::array<std::uint16_t, 4U> texel{};
    for (std::size_t channel = 0U; channel < texel.size(); ++channel) {
      std::memcpy(&texel[channel], bytes.data() + offset + channel * 2U,
                  sizeof(texel[channel]));
      const float value = HalfToFloat(texel[channel]);
      Require(std::isfinite(value),
              std::string("non-finite RGBA16F reflection component in ") +
                  section);
      ++metrics.finite_component_count;
      if (channel < 3U) {
        if (value != 0.0F) {
          ++metrics.nonzero_rgb_component_count;
        }
        metrics.max_absolute_rgb =
            std::max(metrics.max_absolute_rgb, std::fabs(value));
      }
    }
    distinct_texels.insert(texel);
  }
  metrics.distinct_texel_count = distinct_texels.size();
  std::vector<std::uint8_t> section_bytes(
      bytes.begin() + static_cast<std::ptrdiff_t>(begin),
      bytes.begin() + static_cast<std::ptrdiff_t>(end));
  metrics.exact_fnv1a64 = HashBytes(section_bytes);
  Require(metrics.nonzero_rgb_component_count != 0U &&
              metrics.distinct_texel_count >= 2U &&
              metrics.max_absolute_rgb > 0.0F,
          std::string("reflection evidence lacks spatial radiance in ") +
              section);
  return metrics;
}

const char *ReflectionBackendName(ReflectionProbeCaptureBackend backend) {
  switch (backend) {
  case ReflectionProbeCaptureBackend::OGRE_NEXT_METAL:
    return "OGRE_NEXT_METAL";
  case ReflectionProbeCaptureBackend::OGRE_NEXT_D3D11:
    return "OGRE_NEXT_D3D11";
  case ReflectionProbeCaptureBackend::OGRE_NEXT_VULKAN:
    return "OGRE_NEXT_VULKAN";
  case ReflectionProbeCaptureBackend::OGRE_NEXT_D3D12:
    return "OGRE_NEXT_D3D12";
  }
  Fail("unknown native reflection capture backend");
}

std::string JsonEscape(const std::string &value) {
  std::ostringstream escaped;
  escaped << std::hex << std::setfill('0');
  for (const unsigned char byte : value) {
    switch (byte) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
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
      if (byte < 0x20U) {
        escaped << "\\u00" << std::setw(2) << static_cast<unsigned>(byte);
      } else {
        escaped << static_cast<char>(byte);
      }
      break;
    }
  }
  return escaped.str();
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

Metrics InspectHdr(const RenderFrameOutput &output,
                   bool require_scene_referred_energy = true) {
  const FrameAttachment &attachment =
      RequireAttachment(output, PixelFormat::RGBA16_FLOAT);
  Require(attachment.row_pitch_bytes == static_cast<std::uint64_t>(kWidth) * 8U,
          "HDR readback is not tightly packed");
  Require(attachment.bytes.size() ==
              static_cast<std::size_t>(attachment.row_pitch_bytes) * kHeight,
          "HDR readback byte count is incomplete");
  Metrics metrics;
  metrics.attachment_bytes = attachment.bytes;
  metrics.attachment_fnv1a64 = HashBytes(metrics.attachment_bytes);
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
  if (require_scene_referred_energy && metrics.maximum_luminance <= 1.05F) {
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

Metrics ReadSdrAttachment(const RenderFrameOutput &output) {
  const FrameAttachment &attachment =
      RequireAttachment(output, PixelFormat::RGBA8_SRGB);
  Require(attachment.row_pitch_bytes == static_cast<std::uint64_t>(kWidth) * 4U,
          "SDR readback is not tightly packed");
  Require(attachment.bytes.size() ==
              static_cast<std::size_t>(attachment.row_pitch_bytes) * kHeight,
          "SDR readback byte count is incomplete");
  Metrics metrics;
  metrics.attachment_bytes = attachment.bytes;
  metrics.attachment_fnv1a64 = HashBytes(metrics.attachment_bytes);
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
  return metrics;
}

Metrics InspectSdr(const RenderFrameOutput &output) {
  Metrics metrics = ReadSdrAttachment(output);
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

void WritePpm(const std::string &path, const Metrics &metrics,
              std::uint32_t width = kWidth,
              std::uint32_t height = kHeight) {
  if (path.empty()) {
    return;
  }
  Require(width > 0U && height > 0U &&
              metrics.rgb.size() ==
                  static_cast<std::size_t>(width) * height * 3U,
          "frame output RGB extent is incomplete");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open frame output: " + path);
  }
  output << "P6\n" << width << ' ' << height << "\n255\n";
  output.write(reinterpret_cast<const char *>(metrics.rgb.data()),
               static_cast<std::streamsize>(metrics.rgb.size()));
  if (!output) {
    Fail("could not write frame output: " + path);
  }
}

void WriteIsolationEvidence(const std::string &path,
                            const SmokeResult &result) {
  Require(!path.empty(), "RT4/V1 isolation evidence path is empty");
  Require(result.variants.size() == kVariantSpecs.size(),
          "RT4/V1 isolation evidence is incomplete");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open RT4/V1 isolation evidence: " + path);
  }
  const auto write_attachment = [&](const Metrics &metrics) {
    output.write(
        reinterpret_cast<const char *>(metrics.attachment_bytes.data()),
        static_cast<std::streamsize>(metrics.attachment_bytes.size()));
  };
  for (const VariantEvidence &variant : result.variants) {
    write_attachment(variant.hdr);
    write_attachment(variant.sdr);
  }
  write_attachment(result.tangent_handedness.positive_hdr);
  write_attachment(result.tangent_handedness.positive_sdr);
  write_attachment(result.tangent_handedness.negative_hdr);
  write_attachment(result.tangent_handedness.negative_sdr);
  if (!output) {
    Fail("could not write complete RT4/V1 isolation evidence: " + path);
  }
}

void WriteReflectionEvidence(const std::string &path,
                             const SmokeResult &result) {
  Require(!path.empty(), "RT4/V1 reflection evidence path is empty");
  Require(result.reflection_capture.valid,
          "RT4/V1 reflection evidence is unavailable");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open RT4/V1 reflection evidence: " + path);
  }
  output.write(
      reinterpret_cast<const char *>(
          result.reflection_capture.raw_mip_zero_rgba16f.data()),
      static_cast<std::streamsize>(
          result.reflection_capture.raw_mip_zero_rgba16f.size()));
  output.write(
      reinterpret_cast<const char *>(
          result.reflection_capture.filtered_rgba16f.data()),
      static_cast<std::streamsize>(
          result.reflection_capture.filtered_rgba16f.size()));
  if (!output) {
    Fail("could not write complete RT4/V1 reflection evidence: " + path);
  }
}

void WriteHdrCompositorEvidence(
    const std::string &path,
    const SmokeResult::HdrCompositorEvidence &evidence) {
  Require(!path.empty(), "HDR compositor evidence path is empty");
  const std::size_t expected_bytes =
      static_cast<std::size_t>(kWidth) * kHeight * 4U;
  const std::array<const Metrics *, 3U> attachments{{
      &evidence.first, &evidence.final, &evidence.ui_overlay_control}};
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open HDR compositor evidence: " + path);
  }
  for (const Metrics *metrics : attachments) {
    Require(metrics != nullptr &&
                metrics->attachment_bytes.size() == expected_bytes,
            "HDR compositor evidence attachment is incomplete");
    output.write(
        reinterpret_cast<const char *>(metrics->attachment_bytes.data()),
        static_cast<std::streamsize>(metrics->attachment_bytes.size()));
  }
  const std::array<const std::vector<std::uint16_t> *, 4U> split_targets{{
      &evidence.split_content.base_hdr_rgba16,
      &evidence.split_content.sun_full_hdr_rgba16,
      &evidence.split_content.sun_direct_hdr_rgba16,
      &evidence.split_content.raster_lit_hdr_rgba16}};
  for (const std::vector<std::uint16_t> *target : split_targets) {
    Require(target != nullptr &&
                target->size() ==
                    static_cast<std::size_t>(kWidth) * kHeight * 4U,
            "HDR split evidence attachment is incomplete");
    output.write(reinterpret_cast<const char *>(target->data()),
                 static_cast<std::streamsize>(target->size() *
                                              sizeof(std::uint16_t)));
  }
  if (!output) {
    Fail("could not write complete HDR compositor evidence: " + path);
  }
}

void WriteAnalyticSkyEvidence(
    const std::string &path,
    const SmokeResult::AnalyticSkyEvidence &sky) {
  Require(!path.empty(), "RT4/V1 analytic-sky evidence path is empty");
  const std::size_t attachment_bytes =
      static_cast<std::size_t>(sky.visual_width) * sky.visual_height * 8U;
  Require(sky.camera_facing_sunless_hdr.attachment_bytes.size() ==
              attachment_bytes &&
              sky.camera_facing_sun_hdr.attachment_bytes.size() ==
                  attachment_bytes,
          "RT4/V1 analytic-sky evidence is incomplete");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    Fail("could not open RT4/V1 analytic-sky evidence: " + path);
  }
  output.write(
      reinterpret_cast<const char *>(
          sky.camera_facing_sunless_hdr.attachment_bytes.data()),
      static_cast<std::streamsize>(attachment_bytes));
  output.write(
      reinterpret_cast<const char *>(
          sky.camera_facing_sun_hdr.attachment_bytes.data()),
      static_cast<std::streamsize>(attachment_bytes));
  if (!output) {
    Fail("could not write complete RT4/V1 analytic-sky evidence: " + path);
  }
}

std::string HexHash(std::uint64_t hash) {
  std::ostringstream value;
  value << std::hex << std::setfill('0') << std::setw(16) << hash;
  return value.str();
}

const char *HdrHistoryValidationModeName(
    OgreNextHdrHistoryValidationMode mode) {
  switch (mode) {
  case OgreNextHdrHistoryValidationMode::NONE:
    return "none";
  case OgreNextHdrHistoryValidationMode::
      NATIVE_AUTHORITATIVE_CONDITIONING_PLUS_ONE_R16_ULP:
    return kOgreNextHdrHistoryValidationMode;
  }
  Fail("unknown HDR history validation mode");
}

std::string MakeReport(const SmokeResult &result, bool modern_pbr,
                       const std::string &evidence_path,
                       const std::string &reflection_evidence_path,
                       const std::string &compositor_evidence_path,
                       const std::string &analytic_sky_evidence_path,
                       const std::string &analytic_sky_image_path) {
  const Metrics &hdr = result.hdr;
  const Metrics &sdr = result.sdr;
  ReflectionSectionMetrics raw_reflection;
  ReflectionSectionMetrics filtered_reflection;
  if (modern_pbr) {
    constexpr std::size_t kRawReflectionBytes = 32U * 32U * 6U * 8U;
    constexpr std::size_t kFilteredMipZeroBytes = kRawReflectionBytes;
    constexpr std::size_t kFilteredReflectionBytes =
        (32U * 32U + 16U * 16U) * 6U * 8U;
    Require(result.reflection_capture.raw_mip_zero_rgba16f.size() ==
                kRawReflectionBytes &&
                result.reflection_capture.filtered_rgba16f.size() ==
                    kFilteredReflectionBytes,
            "RT4/V1 reflection evidence has an unexpected byte layout");
    raw_reflection = InspectReflectionSection(
        result.reflection_capture.raw_mip_zero_rgba16f, 0U,
        kRawReflectionBytes, "raw mip zero");
    filtered_reflection = InspectReflectionSection(
        result.reflection_capture.filtered_rgba16f, 0U,
        kFilteredReflectionBytes, "filtered mip chain");
    static_cast<void>(InspectReflectionSection(
        result.reflection_capture.filtered_rgba16f,
        kFilteredMipZeroBytes, kFilteredReflectionBytes,
        "filtered mip one"));
  }
  std::ostringstream report;
  report << std::setprecision(std::numeric_limits<double>::max_digits10);
  report << "{\n"
         << "  \"schema\": \""
         << (modern_pbr ? "ror.ogre_next_frontend_rt4_pbr_v1_smoke.v4"
                        : "ror.ogre_next_frontend_n1_smoke.v1")
         << "\",\n"
         << "  \"status\": \"pass\",\n"
         << (modern_pbr
                 ? std::string("  \"executable_build_identity\": \"") +
                       ROR_OGRE_NEXT_N1_BUILD_IDENTITY + "\",\n"
                 : std::string())
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
         << "    \"normal_map_source_lock_sha256\": \""
         << ROR_OGRE_NEXT_N1_NORMAL_MAP_SOURCE_LOCK_SHA256 << "\",\n"
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
         << ROR_OGRE_NEXT_N1_SHADER_MEDIA_MANIFEST_FILE_COUNT;
  if (modern_pbr) {
    report << ",\n"
           << "    \"hdr_media_manifest_sha256\": \""
           << ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_SHA256 << "\",\n"
           << "    \"hdr_media_manifest_file_count\": "
           << ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_FILE_COUNT << "\n";
  } else {
    report << "\n";
  }
  report << "  },\n"
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
        << "    \"normal_upload\": \"linear_RGBA8_positive_Z_to_RG8_UNORM\",\n"
        << "    \"padded_source_rows_verified\": true,\n"
        << "    \"portable_sampler_mapping_verified\": true,\n"
        << "    \"normal_texture_admitted\": true,\n"
        << "    \"normal_slot\": \"PBSM_NORMAL\",\n"
        << "    \"normal_uv_source\": 0,\n"
        << "    \"normal_scale\": 1,\n"
        << "    \"normal_map_weight\": 1,\n"
        << "    \"normal_positive_z_tolerance_decoded\": \"1/255\",\n"
        << "    \"occlusion_texture_admitted\": false,\n"
        << "    \"occlusion_blocker\": \"pinned_HLMS_PBS_has_no_ambient_only_AO_slot\",\n";
  }
  report
         << "    \"runtime_media_root\": \"explicit_absolute\",\n"
         << "    \"package_media_relative_path\": \""
         << ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE << "\",\n"
         << "    \"relocated_executable\": true,\n"
         << "    \"compositor2\": true,\n"
         << "    \"ui_included\": false,\n"
         << "    \"cpu_readback_completed\": true,\n"
         << "    \"dynamic_mesh_updates\": \"synchronous_full_frame_owned\",\n";
  if (modern_pbr) {
    report << "    \"analytic_lights_calibrated\": true,\n"
           << "    \"directional_lux_to_native_power_scale\": 0.0009765625,\n"
           << "    \"maximum_directional_lights\": 1,\n"
           << "    \"analytic_sky_capture_policy_version\": 1,\n"
           << "    \"analytic_sky_native_render_policy_version\": 1,\n"
           << "    \"analytic_sky_path\": \"camera_centered_gradient_ground_additive_sun\",\n"
           << "    \"analytic_sky_exact_skyx_pixel_capture\": false,\n"
           << "    \"constant_environment_only\": false,\n";
  } else {
    report << "    \"analytic_lights_calibrated\": false,\n"
           << "    \"constant_environment_only\": true,\n";
  }
  report
         << "    \"native_interop\": false,\n"
         << "    \"ray_tracing\": false\n"
         << "  },\n"
         << "  \"catalog\": {\n"
         << "    \"registry_id\": " << kRegistryId << ",\n"
         << "    \"sequence\": "
         << (modern_pbr ? kVariantSpecs.back().sequence : 1U) << ",\n";
  if (modern_pbr) {
    report << "    \"baseline_sequence\": 1,\n"
           << "    \"live_replacement_count\": 6,\n"
           << "    \"referenced_texture_count\": 4,\n"
           << "    \"referenced_sampler_count\": 1,\n"
           << "    \"unreferenced_assets_not_uploaded\": true,\n";
  }
  report
         << "    \"transactional_replay_after_restart\": true\n"
         << "  },\n"
         << "  \"dynamic_meshes\": {\n"
         << "    \"schema\": \"ror.ogre_next_dynamic_mesh.v1\",\n"
         << "    \"base_deformation_revision\": 1,\n"
         << "    \"deformed_deformation_revision\": 2,\n"
         << "    \"full_update_owned\": true,\n"
         << "    \"solver_memory_aliased\": false,\n"
         << "    \"changed_pixels\": "
         << result.dynamic_mesh.changed_pixels << ",\n"
         << "    \"base_attachment_fnv1a64\": \""
         << HexHash(result.dynamic_mesh.base.attachment_fnv1a64) << "\",\n"
         << "    \"deformed_attachment_fnv1a64\": \""
         << HexHash(result.dynamic_mesh.deformed.attachment_fnv1a64)
         << "\",\n"
         << "    \"base_exact_replay\": "
         << (result.dynamic_mesh.base_exact_replay ? "true" : "false")
         << ",\n"
         << "    \"deformed_exact_replay\": "
         << (result.dynamic_mesh.deformed_exact_replay ? "true" : "false")
         << "\n"
         << "  },\n";
  if (modern_pbr) {
    const OgreNextAnalyticSkyRuntimeAudit &sky =
        result.analytic_sky.final_committed;
    const AnalyticSkyDescriptor &descriptor = sky.last_descriptor;
    constexpr std::uint32_t kBackgroundVertexCount =
        2U * (kOgreNextAnalyticSkyHemisphereRings *
                  (kOgreNextAnalyticSkyLongitudeSegments + 1U) +
              1U);
    constexpr std::uint32_t kBackgroundIndexCount =
        2U * ((kOgreNextAnalyticSkyHemisphereRings - 1U) *
                  kOgreNextAnalyticSkyLongitudeSegments * 6U +
              kOgreNextAnalyticSkyLongitudeSegments * 3U);
    Require(sky.last_background_vertex_count == kBackgroundVertexCount &&
                sky.last_background_index_count == kBackgroundIndexCount &&
                sky.last_sun_vertex_count ==
                    kOgreNextAnalyticSkySunSegments + 2U &&
                sky.last_sun_index_count ==
                    kOgreNextAnalyticSkySunSegments * 3U &&
                sky.last_native_content_bytes > 0U,
            "RT4/V1 analytic-sky native topology audit is incomplete");
    report << "  \"analytic_sky\": {\n"
           << "    \"schema\": \"ror.ogre_next_analytic_sky.v2\",\n"
           << "    \"evidence_file\": \""
           << JsonEscape(
                  std::filesystem::u8path(analytic_sky_evidence_path)
                      .filename()
                      .generic_u8string())
           << "\",\n"
           << "    \"visual_file\": \""
           << JsonEscape(std::filesystem::u8path(analytic_sky_image_path)
                             .filename()
                             .generic_u8string())
           << "\",\n"
           << "    \"capture_policy_version\": "
           << kOgre14ModernAnalyticSkyPolicyVersion << ",\n"
           << "    \"native_render_policy_version\": "
           << sky.native_render_policy_version << ",\n"
           << "    \"authoritative_inputs\": \"joined_live_ambient_and_exact_converted_main_light\",\n"
           << "    \"exact_skyx_pixel_capture\": false,\n"
           << "    \"skyx_capture_boundary\": \"SkyX_shader_is_azimuth_dependent_and_may_apply_LDR_exposure\",\n"
           << "    \"sun_light_id\": " << sky.last_sun_light_id << ",\n"
           << "    \"descriptor\": {\n"
           << "      \"zenith_radiance\": ["
           << descriptor.zenith_radiance.x << ", "
           << descriptor.zenith_radiance.y << ", "
           << descriptor.zenith_radiance.z << "],\n"
           << "      \"horizon_radiance\": ["
           << descriptor.horizon_radiance.x << ", "
           << descriptor.horizon_radiance.y << ", "
           << descriptor.horizon_radiance.z << "],\n"
           << "      \"ground_radiance\": ["
           << descriptor.ground_radiance.x << ", "
           << descriptor.ground_radiance.y << ", "
           << descriptor.ground_radiance.z << "],\n"
           << "      \"sun_disk_radiance\": ["
           << descriptor.sun_disk_radiance.x << ", "
           << descriptor.sun_disk_radiance.y << ", "
           << descriptor.sun_disk_radiance.z << "],\n"
           << "      \"sun_angular_radius_radians\": "
           << descriptor.sun_angular_radius_radians << "\n"
           << "    },\n"
           << "    \"native_geometry\": {\n"
           << "      \"resource_model\": \"frontend_owned_v2_mesh_item\",\n"
           << "      \"background_vertex_count\": "
           << sky.last_background_vertex_count << ",\n"
           << "      \"background_index_count\": "
           << sky.last_background_index_count << ",\n"
           << "      \"sun_vertex_count\": "
           << sky.last_sun_vertex_count << ",\n"
           << "      \"sun_index_count\": "
           << sky.last_sun_index_count << ",\n"
           << "      \"native_content_bytes\": "
           << sky.last_native_content_bytes << ",\n"
           << "      \"cpu_geometry_fnv1a64\": "
           << sky.last_cpu_geometry_fnv1a64 << ",\n"
           << "      \"native_geometry_metadata_verified\": "
           << (sky.native_geometry_metadata_verified ? "true" : "false")
           << ",\n"
           << "      \"production_default_gpu_content_readbacks_zero\": "
           << (result.analytic_sky
                       .production_default_gpu_content_readbacks_zero
                   ? "true"
                   : "false")
           << ",\n"
           << "      \"exact_gpu_buffer_content_readback\": "
           << (sky.exact_native_geometry_readback ? "true" : "false")
           << ",\n"
           << "      \"camera_centered\": "
           << (sky.camera_centered ? "true" : "false") << ",\n"
           << "      \"rendered_first\": "
           << (sky.rendered_first ? "true" : "false") << ",\n"
           << "      \"depth_check_disabled\": "
           << (sky.depth_check_disabled ? "true" : "false") << ",\n"
           << "      \"depth_write_disabled\": "
           << (sky.depth_write_disabled ? "true" : "false") << ",\n"
           << "      \"additive_sun_disk\": "
           << (sky.additive_sun_disk ? "true" : "false") << ",\n"
           << "      \"separate_sun_alpha_replace\": "
           << (sky.separate_sun_alpha_replace ? "true" : "false")
           << ",\n"
           << "      \"casts_shadows\": "
           << (sky.casts_shadows ? "true" : "false") << ",\n"
           << "      \"portable_scene_identity_absent\": "
           << (sky.portable_scene_identity_absent ? "true" : "false")
           << "\n"
           << "    },\n"
           << "    \"runtime_audit\": {\n"
           << "      \"version\": " << sky.version << ",\n"
           << "      \"completed_frames\": " << sky.completed_frames
           << ",\n"
           << "      \"native_mesh_creates\": "
           << sky.native_mesh_creates << ",\n"
           << "      \"native_mesh_destroys\": "
           << sky.native_mesh_destroys << ",\n"
           << "      \"native_vertex_buffer_creates\": "
           << sky.native_vertex_buffer_creates << ",\n"
           << "      \"native_vertex_buffer_destroys\": "
           << sky.native_vertex_buffer_destroys << ",\n"
           << "      \"native_index_buffer_creates\": "
           << sky.native_index_buffer_creates << ",\n"
           << "      \"native_index_buffer_destroys\": "
           << sky.native_index_buffer_destroys << ",\n"
           << "      \"native_vao_creates\": "
           << sky.native_vao_creates << ",\n"
           << "      \"native_vao_destroys\": "
           << sky.native_vao_destroys << ",\n"
           << "      \"native_item_creates\": "
           << sky.native_item_creates << ",\n"
           << "      \"native_item_destroys\": "
           << sky.native_item_destroys << ",\n"
           << "      \"native_scene_node_creates\": "
           << sky.native_scene_node_creates << ",\n"
           << "      \"native_scene_node_destroys\": "
           << sky.native_scene_node_destroys << ",\n"
           << "      \"native_datablock_creates\": "
           << sky.native_datablock_creates << ",\n"
           << "      \"native_datablock_destroys\": "
           << sky.native_datablock_destroys << ",\n"
           << "      \"native_mesh_absence_checks\": "
           << sky.native_mesh_absence_checks << ",\n"
           << "      \"native_item_absence_checks\": "
           << sky.native_item_absence_checks << ",\n"
           << "      \"native_scene_node_absence_checks\": "
           << sky.native_scene_node_absence_checks << ",\n"
           << "      \"native_datablock_absence_checks\": "
           << sky.native_datablock_absence_checks << ",\n"
           << "      \"native_gpu_content_readbacks\": "
           << sky.native_gpu_content_readbacks << ",\n"
           << "      \"native_state_verifications\": "
           << sky.native_state_verifications << "\n"
           << "    },\n"
           << "    \"visual_proof\": {\n"
           << "      \"sky_only\": true,\n"
           << "      \"camera_facing_sun\": true,\n"
           << "      \"width\": " << result.analytic_sky.visual_width
           << ",\n"
           << "      \"height\": " << result.analytic_sky.visual_height
           << ",\n"
           << "      \"hdr_pixel_format\": \"RGBA16_FLOAT\",\n"
           << "      \"evidence_bytes\": "
           << static_cast<std::size_t>(result.analytic_sky.visual_width) *
                  result.analytic_sky.visual_height * 16U
           << ",\n"
           << "      \"sunless_hdr_offset\": 0,\n"
           << "      \"sunless_hdr_bytes\": "
           << static_cast<std::size_t>(result.analytic_sky.visual_width) *
                  result.analytic_sky.visual_height * 8U
           << ",\n"
           << "      \"sun_hdr_offset\": "
           << static_cast<std::size_t>(result.analytic_sky.visual_width) *
                  result.analytic_sky.visual_height * 8U
           << ",\n"
           << "      \"sun_hdr_bytes\": "
           << static_cast<std::size_t>(result.analytic_sky.visual_width) *
                  result.analytic_sky.visual_height * 8U
           << ",\n"
           << "      \"sunless_hdr_fnv1a64\": \""
           << HexHash(result.analytic_sky.camera_facing_sunless_hdr
                          .attachment_fnv1a64)
           << "\",\n"
           << "      \"sun_hdr_fnv1a64\": \""
           << HexHash(result.analytic_sky.camera_facing_sun_hdr
                          .attachment_fnv1a64)
           << "\",\n"
           << "      \"visual_rgb_fnv1a64\": \""
           << HexHash(result.analytic_sky.camera_facing_sun_sdr.fnv1a64)
           << "\",\n"
           << "      \"hemisphere_covered_pixels\": "
           << result.analytic_sky.hemisphere_covered_pixels << ",\n"
           << "      \"hemisphere_gradient_rows\": "
           << result.analytic_sky.hemisphere_gradient_rows << ",\n"
           << "      \"broad_hemisphere_coverage\": "
           << (result.analytic_sky.broad_hemisphere_coverage ? "true"
                                                              : "false")
           << ",\n"
           << "      \"sun_changed_pixels\": "
           << result.analytic_sky.sun_changed_pixels << ",\n"
           << "      \"sun_changed_pixels_alpha_exact_one\": "
           << result.analytic_sky.sun_changed_pixels_alpha_exact_one
           << ",\n"
           << "      \"sun_hdr_opaque_alpha_pixels\": "
           << result.analytic_sky.sun_hdr_opaque_alpha_pixels << ",\n"
           << "      \"visible_sun_effect\": "
           << (result.analytic_sky.visible_sun_effect ? "true" : "false")
           << ",\n"
           << "      \"visible_sun_alpha_exact_one\": "
           << (result.analytic_sky.visible_sun_alpha_exact_one ? "true"
                                                               : "false")
           << "\n"
           << "    },\n"
           << "    \"transactional_rollback\": {\n"
           << "      \"injected_stage_count\": "
           << result.analytic_sky.rollback_stages_verified << ",\n"
           << "      \"publication_unchanged_on_failure\": "
           << (result.analytic_sky.rollback_publication_unchanged ? "true"
                                                                  : "false")
           << ",\n"
           << "      \"native_lifetimes_balanced_on_failure\": "
           << (result.analytic_sky.rollback_lifetimes_balanced ? "true"
                                                               : "false")
           << ",\n"
           << "      \"clean_retry\": "
           << (result.analytic_sky.clean_retry ? "true" : "false")
           << "\n"
           << "    }\n"
           << "  },\n";
    constexpr std::size_t kRawReflectionBytes = 32U * 32U * 6U * 8U;
    constexpr std::size_t kFilteredReflectionBytes =
        (32U * 32U + 16U * 16U) * 6U * 8U;
    const OgreNextReflectionProbeCaptureEvidence &capture =
        result.reflection_capture;
    const OgreNextReflectionProbeAudit &audit = result.reflection_probes;
    report << "  \"reflection_probes\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_reflection_probes.v1\",\n"
           << "    \"evidence_file\": \""
           << JsonEscape(std::filesystem::u8path(reflection_evidence_path)
                             .filename()
                             .generic_u8string())
           << "\",\n"
           << "    \"evidence_bytes\": "
           << kRawReflectionBytes + kFilteredReflectionBytes << ",\n"
           << "    \"backend\": \""
           << ReflectionBackendName(capture.backend) << "\",\n"
           << "    \"render_system\": \""
           << JsonEscape(capture.render_system) << "\",\n"
           << "    \"device_name\": \"" << JsonEscape(capture.device_name)
           << "\",\n"
           << "    \"driver_version\": \""
           << JsonEscape(capture.driver_version) << "\",\n"
           << "    \"pixel_format\": \"RGBA16_FLOAT\",\n"
           << "    \"byte_order\": \"little_endian\",\n"
           << "    \"row_padding_included\": false,\n"
           << "    \"subresource_order\": \"raw_face_major_then_filtered_mip_major_face_major\",\n"
           << "    \"ui_included\": false,\n"
           << "    \"same_device_exact_replay\": "
           << (result.reflection_same_device_deterministic_replay ? "true"
                                                                  : "false")
           << ",\n"
           << "    \"capture\": {\n"
           << "      \"render_frame_id\": " << capture.render_frame_id
           << ",\n"
           << "      \"simulation_tick\": " << capture.simulation_tick
           << ",\n"
           << "      \"probe_id\": " << capture.probe_id << ",\n"
           << "      \"content_revision\": " << capture.content_revision
           << ",\n"
           << "      \"candidate_generation\": "
           << capture.candidate_generation << ",\n"
           << "      \"deterministic_seed\": \""
           << HexHash(capture.deterministic_seed) << "\",\n"
           << "      \"resolution\": " << capture.capture_resolution << "\n"
           << "    },\n"
           << "    \"runtime_audit\": {\n"
           << "      \"version\": " << audit.version << ",\n"
           << "      \"successful_capture_count\": "
           << audit.successful_capture_count << ",\n"
           << "      \"failed_capture_count\": "
           << audit.failed_capture_count << ",\n"
           << "      \"live_probe_count\": " << audit.live_probe_count
           << ",\n"
           << "      \"blend_resolution\": " << audit.blend_resolution
           << ",\n"
           << "      \"blend_texture_ready\": "
           << (audit.blend_texture_ready ? "true" : "false") << ",\n"
           << "      \"committed_state_digest\": \""
           << HexHash(audit.committed_state_digest) << "\",\n"
           << "      \"native_execution_evidence\": \""
           << HexHash(audit.native_execution_evidence) << "\",\n"
           << "      \"capture_digest\": \""
           << HexHash(audit.last_capture_digest) << "\",\n"
           << "      \"canonical_filtered_payload_bytes\": "
           << audit.last_canonical_payload_bytes << ",\n"
           << "      \"completed_face_count\": "
           << audit.completed_face_count << ",\n"
           << "      \"completed_mip_count\": "
           << audit.completed_mip_count << ",\n"
           << "      \"ui_free_capture\": "
           << (audit.ui_free_capture ? "true" : "false") << ",\n"
           << "      \"reserved_render_queue_excluded\": "
           << (audit.reserved_render_queue_excluded ? "true" : "false")
           << "\n"
           << "    },\n"
           << "    \"raw\": {\n"
           << "      \"offset\": 0,\n"
           << "      \"bytes\": " << kRawReflectionBytes << ",\n"
           << "      \"face_count\": 6,\n"
           << "      \"mip_dimensions\": [32],\n"
           << "      \"exact_fnv1a64\": \""
           << HexHash(raw_reflection.exact_fnv1a64) << "\",\n"
           << "      \"finite_component_count\": "
           << raw_reflection.finite_component_count << ",\n"
           << "      \"nonzero_rgb_component_count\": "
           << raw_reflection.nonzero_rgb_component_count << ",\n"
           << "      \"distinct_texel_count\": "
           << raw_reflection.distinct_texel_count << ",\n"
           << "      \"max_absolute_rgb\": " << std::setprecision(9)
           << raw_reflection.max_absolute_rgb << "\n"
           << "    },\n"
           << "    \"filtered\": {\n"
           << "      \"offset\": " << kRawReflectionBytes << ",\n"
           << "      \"bytes\": " << kFilteredReflectionBytes << ",\n"
           << "      \"face_count\": 6,\n"
           << "      \"mip_dimensions\": [32, 16],\n"
           << "      \"exact_fnv1a64\": \""
           << HexHash(filtered_reflection.exact_fnv1a64) << "\",\n"
           << "      \"finite_component_count\": "
           << filtered_reflection.finite_component_count << ",\n"
           << "      \"nonzero_rgb_component_count\": "
           << filtered_reflection.nonzero_rgb_component_count << ",\n"
           << "      \"distinct_texel_count\": "
           << filtered_reflection.distinct_texel_count << ",\n"
           << "      \"max_absolute_rgb\": "
           << filtered_reflection.max_absolute_rgb << "\n"
           << "    }\n"
           << "  },\n"
           << "  \"display_domain_unlit\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_display_domain_unlit.v1\",\n"
           << "    \"base_color_transfer\": \"SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE\",\n"
           << "    \"upload_format\": \"RGBA8_UNORM\",\n"
           << "    \"mip_policy\": \"complete_base_to_1x1_nearest_mip\",\n"
           << "    \"sampler\": \"linear_min_mag_clamp_edge\",\n"
           << "    \"shader_precision\": \"PrecisionFull32\",\n"
           << "    \"encoded_filtered\": ["
           << result.display_domain_unlit.encoded_filtered[0U] << ", "
           << result.display_domain_unlit.encoded_filtered[1U] << ", "
           << result.display_domain_unlit.encoded_filtered[2U] << "],\n"
           << "    \"filter_then_eotf\": ["
           << result.display_domain_unlit.filter_then_eotf[0U] << ", "
           << result.display_domain_unlit.filter_then_eotf[1U] << ", "
           << result.display_domain_unlit.filter_then_eotf[2U] << "],\n"
           << "    \"decode_before_filter\": ["
           << result.display_domain_unlit.decode_before_filter[0U] << ", "
           << result.display_domain_unlit.decode_before_filter[1U] << ", "
           << result.display_domain_unlit.decode_before_filter[2U] << "],\n"
           << "    \"matching_foreground_pixels\": "
           << result.display_domain_unlit.matching_foreground_pixels << ",\n"
           << "    \"decode_before_filter_pixels\": "
           << result.display_domain_unlit.decode_before_filter_pixels << ",\n"
           << "    \"complete_unorm_mips_uploaded\": "
           << (result.display_domain_unlit.complete_unorm_mips_uploaded
                   ? "true"
                   : "false")
           << ",\n"
           << "    \"full32_after_filter_shader_executed\": "
           << (result.display_domain_unlit.full32_after_filter_shader_executed
                   ? "true"
                   : "false")
           << ",\n"
           << "    \"alpha_untouched_opaque\": "
           << (result.display_domain_unlit.alpha_untouched_opaque ? "true"
                                                                  : "false")
           << ",\n"
           << "    \"no_cast_or_receive_shadow_flags\": "
           << (result.display_domain_unlit.no_cast_or_receive_shadow_flags
                   ? "true"
                   : "false")
           << ",\n"
           << "    \"usage_transition_rollback_exact\": "
           << (result.display_domain_unlit.usage_transition_rollback_exact
                   ? "true"
                   : "false")
           << ",\n"
           << "    \"usage_transition_commit_exact\": "
           << (result.display_domain_unlit.usage_transition_commit_exact
                   ? "true"
                   : "false")
           << "\n"
           << "  },\n"
           << "  \"texture_allocations\": {\n"
           << "    \"version\": " << result.texture_allocations.version
           << ",\n"
           << "    \"live_source_textures\": "
           << result.texture_allocations.live_source_textures << ",\n"
           << "    \"sampled_rgba_allocations\": "
           << result.texture_allocations.sampled_rgba_allocations << ",\n"
           << "    \"linear_rgba_allocations\": "
           << result.texture_allocations.linear_rgba_allocations << ",\n"
           << "    \"roughness_r8_allocations\": "
           << result.texture_allocations.roughness_r8_allocations << ",\n"
           << "    \"metallic_r8_allocations\": "
           << result.texture_allocations.metallic_r8_allocations << ",\n"
           << "    \"normal_rg8_allocations\": "
           << result.texture_allocations.normal_rg8_allocations << ",\n"
           << "    \"unused_packed_rgba_allocations\": 0,\n"
           << "    \"exact_usage\": "
           << (result.texture_allocations.exact_usage ? "true" : "false")
           << "\n"
           << "  },\n"
           << "  \"texture_upload_rollback\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_texture_upload_rollback.v1\",\n"
           << "    \"derived_allocation\": \"normal_RG8_UNORM\",\n"
           << "    \"injected_post_create_stage_count\": "
           << result.texture_upload_rollback.size() << ",\n"
           << "    \"stages\": [\n";
    const auto write_rollback_audit =
        [&](const char *name,
            const OgreNextN1TextureAllocationAudit &audit, bool last) {
          report << "          \"" << name << "\": {"
                 << "\"creates\": " << audit.native_allocation_creates
                 << ", \"destroys\": " << audit.native_allocation_destroys
                 << ", \"live\": " << audit.live_native_allocations
                 << ", \"retired_name_lookups\": "
                 << audit.retired_name_lookups
                 << ", \"retired_name_rejections\": "
                 << audit.retired_name_rejections
                 << ", \"exact_usage\": "
                 << (audit.exact_usage ? "true" : "false") << "}"
                 << (last ? "\n" : ",\n");
        };
    for (std::size_t index = 0U;
         index < result.texture_upload_rollback.size(); ++index) {
      const SmokeResult::TextureUploadRollbackStageEvidence &stage =
          result.texture_upload_rollback[index];
      report << "      {\n"
             << "        \"name\": \"" << stage.name << "\",\n"
             << "        \"audits\": {\n";
      write_rollback_audit("after_failure", stage.after_failure, false);
      write_rollback_audit("after_retry", stage.after_retry, false);
      write_rollback_audit("after_replacement", stage.after_replacement,
                           false);
      write_rollback_audit("after_shutdown", stage.after_shutdown, true);
      report << "        }\n"
             << "      }"
             << (index + 1U == result.texture_upload_rollback.size()
                     ? "\n"
                     : ",\n");
    }
    report << "    ],\n"
           << "    \"clean_retry_replacement_shutdown\": true\n"
           << "  },\n"
           << "  \"texture_retirement\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_texture_retirement.v1\",\n"
           << "    \"derived_allocation\": \"normal_RG8_UNORM\",\n"
           << "    \"isolated_from_visual_variants\": true,\n"
           << "    \"native_image_rg8_staging\": {\"version\": "
           << result.retirement.expanded_normal_upload.version
           << ", \"verified_uploads\": "
           << result.retirement.expanded_normal_upload.verified_uploads
           << ", \"verified_mip_levels\": "
           << result.retirement.expanded_normal_upload.verified_mip_levels
           << ", \"verified_rows\": "
           << result.retirement.expanded_normal_upload.verified_rows
           << ", \"verified_texels\": "
           << result.retirement.expanded_normal_upload.verified_texels
           << ", \"verified_rg_bytes\": "
           << result.retirement.expanded_normal_upload.verified_rg_bytes
           << ", \"verified_padded_source_rows\": "
           << result.retirement.expanded_normal_upload
                  .verified_padded_source_rows
           << ", \"exact_source_rg_to_native_image\": "
           << (result.retirement.expanded_normal_upload
                       .exact_source_rg_to_native_image
                   ? "true"
                   : "false")
           << "},\n"
           << "    \"transitions\": [\n"
           << "      {\"revision\": 1, \"width\": 2, \"height\": 2, \"mip_levels\": 1},\n"
           << "      {\"revision\": 2, \"width\": 4, \"height\": 2, \"mip_levels\": 2, \"padded_rows\": true},\n"
           << "      {\"revision\": 3, \"width\": 2, \"height\": 2, \"mip_levels\": 1}\n"
           << "    ],\n"
           << "    \"exact_extent_and_mip_transitions\": "
           << (result.retirement.exact_extent_and_mip_transitions ? "true"
                                                                  : "false")
           << ",\n"
           << "    \"renders_through_transitions_and_restart\": "
           << (result.retirement.renders_through_transitions_and_restart
                   ? "true"
                   : "false")
           << ",\n"
           << "    \"find_texture_no_throw_rejected_old_names\": "
           << (result.retirement.old_names_rejected ? "true" : "false")
           << ",\n"
           << "    \"audits\": {\n";
    const auto write_retirement_audit =
        [&](const char *name,
            const OgreNextN1TextureAllocationAudit &audit, bool last) {
          report << "      \"" << name << "\": {"
                 << "\"creates\": " << audit.native_allocation_creates
                 << ", \"destroys\": " << audit.native_allocation_destroys
                 << ", \"live\": " << audit.live_native_allocations
                 << ", \"retired_name_lookups\": "
                 << audit.retired_name_lookups
                 << ", \"retired_name_rejections\": "
                 << audit.retired_name_rejections << "}"
                 << (last ? "\n" : ",\n");
        };
    write_retirement_audit("initial", result.retirement.initial, false);
    write_retirement_audit("expanded", result.retirement.expanded, false);
    write_retirement_audit("restored", result.retirement.restored, false);
    write_retirement_audit("first_shutdown",
                           result.retirement.first_shutdown, false);
    write_retirement_audit("restarted", result.retirement.restarted, false);
    write_retirement_audit("final_shutdown",
                           result.retirement.final_shutdown, true);
    report << "    }\n"
           << "  },\n"
           << "  \"texture_isolation\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_texture_isolation.v1\",\n"
           << "    \"evidence_file\": \""
           << std::filesystem::u8path(evidence_path)
                  .filename()
                  .generic_u8string()
           << "\",\n"
           << "    \"width\": " << kWidth << ",\n"
           << "    \"height\": " << kHeight << ",\n"
           << "    \"geometry_identical\": true,\n"
           << "    \"material_factors_constants_identical\": true,\n"
           << "    \"camera_identical\": true,\n"
           << "    \"lights_identical\": true,\n"
           << "    \"ui_included\": false,\n"
           << "    \"variants\": [\n";
    std::size_t offset = 0U;
    for (std::size_t index = 0U; index < result.variants.size(); ++index) {
      const VariantEvidence &variant = result.variants[index];
      report << "      {\n"
             << "        \"name\": \"" << variant.name << "\",\n"
             << "        \"changed_input\": \"" << variant.changed_input
             << "\",\n"
             << "        \"asset_sequence\": " << variant.asset_sequence
             << ",\n"
             << "        \"hdr\": {\"offset\": " << offset
             << ", \"bytes\": " << variant.hdr.attachment_bytes.size()
             << ", \"exact_fnv1a64\": \""
             << HexHash(variant.hdr.attachment_fnv1a64)
             << "\", \"changed_pixels_from_baseline\": "
             << variant.hdr_changed_pixels << "},\n";
      offset += variant.hdr.attachment_bytes.size();
      report << "        \"sdr\": {\"offset\": " << offset
             << ", \"bytes\": " << variant.sdr.attachment_bytes.size()
             << ", \"exact_fnv1a64\": \""
             << HexHash(variant.sdr.attachment_fnv1a64)
             << "\", \"changed_pixels_from_baseline\": "
             << variant.sdr_changed_pixels << "}\n"
             << "      }";
      offset += variant.sdr.attachment_bytes.size();
      report << (index + 1U == result.variants.size() ? "\n" : ",\n");
    }
    report << "    ],\n"
           << "    \"evidence_bytes\": " << offset << "\n"
           << "  },\n"
           << "  \"tangent_handedness\": {\n"
           << "    \"schema\": \"ror.ogre_next_rt4_tangent_handedness.v1\",\n"
           << "    \"evidence_file\": \""
           << std::filesystem::u8path(evidence_path)
                  .filename()
                  .generic_u8string()
           << "\",\n"
           << "    \"evidence_offset\": " << offset << ",\n";
    const std::size_t handedness_evidence_bytes =
        result.tangent_handedness.positive_hdr.attachment_bytes.size() +
        result.tangent_handedness.positive_sdr.attachment_bytes.size() +
        result.tangent_handedness.negative_hdr.attachment_bytes.size() +
        result.tangent_handedness.negative_sdr.attachment_bytes.size();
    report << "    \"evidence_bytes\": " << handedness_evidence_bytes
           << ",\n"
           << "    \"authored_tangent_format\": \"FLOAT4\",\n"
           << "    \"positive_tangent_w\": 1,\n"
           << "    \"negative_tangent_w\": -1,\n"
           << "    \"position_normal_tangent_xyz_uv0_identical\": "
           << (result.tangent_handedness.only_tangent_w_changed ? "true"
                                                                 : "false")
           << ",\n"
           << "    \"material_camera_lights_identical\": true,\n"
           << "    \"ui_included\": false,\n"
           << "    \"positive\": {\n"
           << "      \"hdr\": {\"offset\": " << offset
           << ", \"bytes\": "
           << result.tangent_handedness.positive_hdr.attachment_bytes.size()
           << ", \"exact_fnv1a64\": \""
           << HexHash(result.tangent_handedness.positive_hdr.attachment_fnv1a64)
           << "\"},\n";
    offset += result.tangent_handedness.positive_hdr.attachment_bytes.size();
    report << "      \"sdr\": {\"offset\": " << offset
           << ", \"bytes\": "
           << result.tangent_handedness.positive_sdr.attachment_bytes.size()
           << ", \"exact_fnv1a64\": \""
           << HexHash(result.tangent_handedness.positive_sdr.attachment_fnv1a64)
           << "\"}\n"
           << "    },\n";
    offset += result.tangent_handedness.positive_sdr.attachment_bytes.size();
    report << "    \"negative\": {\n"
           << "      \"hdr\": {\"offset\": " << offset
           << ", \"bytes\": "
           << result.tangent_handedness.negative_hdr.attachment_bytes.size()
           << ", \"exact_fnv1a64\": \""
           << HexHash(result.tangent_handedness.negative_hdr.attachment_fnv1a64)
           << "\"},\n";
    offset += result.tangent_handedness.negative_hdr.attachment_bytes.size();
    report << "      \"sdr\": {\"offset\": " << offset
           << ", \"bytes\": "
           << result.tangent_handedness.negative_sdr.attachment_bytes.size()
           << ", \"exact_fnv1a64\": \""
           << HexHash(result.tangent_handedness.negative_sdr.attachment_fnv1a64)
           << "\"}\n"
           << "    },\n";
    offset += result.tangent_handedness.negative_sdr.attachment_bytes.size();
    report << "    \"hdr_changed_pixels\": "
           << result.tangent_handedness.hdr_changed_pixels << ",\n"
           << "    \"sdr_changed_pixels\": "
           << result.tangent_handedness.sdr_changed_pixels << "\n"
           << "  },\n";
    const SmokeResult::HdrCompositorEvidence &compositor =
        result.hdr_compositor;
    report << std::setprecision(std::numeric_limits<double>::max_digits10)
           << "  \"hdr_compositor\": {\n"
           << "    \"schema\": \"ror.ogre_next_hdr_compositor.v5\",\n"
           << "    \"workspace\": \"RoRHdrWorkspaceUiFreeV2\",\n"
           << "    \"persistent_workspace\": true,\n"
           << "    \"scene_format\": \"RGBA16_FLOAT\",\n"
           << "    \"history_format\": \"R16_FLOAT\",\n"
           << "    \"output_format\": \"RGBA8_SRGB\",\n"
           << "    \"ui_included\": "
           << (compositor.committed.ui_free_workspace_verified ? "false"
                                                               : "true")
           << ",\n"
           << "    \"ui_free_workspace_verified\": "
           << (compositor.committed.ui_free_workspace_verified ? "true"
                                                               : "false")
           << ",\n"
           << "    \"deterministic_simulation_delta\": "
           << (compositor.committed.deterministic_delta_bound ? "true"
                                                              : "false")
           << ",\n"
           << "    \"history_validation_mode\": \""
           << HdrHistoryValidationModeName(
                  compositor.committed.history_validation_mode)
           << "\",\n"
           << "    \"native_r16_history_validated\": "
           << (compositor.committed.native_r16_history_validated ? "true"
                                                                : "false")
           << ",\n"
           << "    \"exact_current_to_old_copy_verified\": "
           << (compositor.committed.exact_current_to_old_copy_verified
                   ? "true"
                   : "false")
           << ",\n"
           << "    \"warmup_frames\": "
           << compositor.committed.warmup_frames << ",\n"
           << "    \"committed_frames\": "
           << compositor.committed.committed_frames << ",\n"
           << "    \"split_lighting\": {\"base_hdr_rgba16\": "
           << (compositor.lighting.separate_base_hdr_target ? "true"
                                                            : "false")
           << ", \"sun_full_unoccluded_rgba16\": "
           << (compositor.lighting.separate_unoccluded_sun_full_hdr_target
                   ? "true"
                   : "false")
           << ", \"sun_direct_rgba16\": "
           << (compositor.lighting.separate_sun_direct_hdr_target ? "true"
                                                                  : "false")
           << ", \"gpu_max_full_minus_base\": "
           << (compositor.lighting.gpu_sun_direct_derivation ? "true"
                                                             : "false")
           << ", \"transactional_sun_toggle\": "
           << (compositor.lighting.transactional_directional_sun_toggle
                   ? "true"
                   : "false")
           << ", \"raster_lit_rgba16\": "
           << (compositor.lighting.raster_lit_hdr_target ? "true"
                                                         : "false")
           << ", \"scene_evaluations\": "
           << compositor.lighting.raster_scene_evaluations
           << ", \"single_history_step\": "
           << (compositor.lighting.single_step_hdr_history ? "true"
                                                           : "false")
           << "},\n"
           << "    \"split_content\": {\"rgb_channels_verified\": "
           << compositor.split_rgb_channels_verified
           << ", \"positive_sun_direct_pixels\": "
           << compositor.positive_sun_direct_pixels
           << ", \"canonical_base_full_raster_alpha_one_direct_alpha_zero\": "
           << (compositor.canonical_split_alpha ? "true" : "false")
           << ", \"base_fnv1a64\": \""
           << HexHash(compositor.split_content_fnv1a64[0U])
           << "\", \"sun_full_fnv1a64\": \""
           << HexHash(compositor.split_content_fnv1a64[1U])
           << "\", \"sun_direct_fnv1a64\": \""
           << HexHash(compositor.split_content_fnv1a64[2U])
           << "\", \"raster_lit_fnv1a64\": \""
           << HexHash(compositor.split_content_fnv1a64[3U]) << "\"},\n"
           << "    \"native_lighting_state_verifications\": "
           << compositor.lighting.native_state_verifications << ",\n"
           << "    \"lighting_test_content_readbacks\": "
           << compositor.lighting.test_artifact_content_readbacks << ",\n"
           << "    \"lighting_production_content_readbacks\": "
           << compositor.lighting.production_content_readbacks << ",\n"
           << "    \"lighting_production_framebuffer_readbacks\": "
           << compositor.lighting.production_framebuffer_readbacks << ",\n"
           << "    \"ogre14_lighting_passes\": "
           << compositor.lighting.ogre14_lighting_passes << ",\n"
           << "    \"initial_inverse_luminance_r16_bits\": "
           << compositor.initialized.previous_inverse_luminance_r16_bits
           << ",\n"
           << "    \"final_inverse_luminance_r16_bits\": "
           << compositor.committed.previous_inverse_luminance_r16_bits
           << ",\n"
           << "    \"reference_inverse_luminance_r16_bits\": "
           << compositor.committed.reference_inverse_luminance_r16_bits
           << ",\n"
           << "    \"history_ogre_exposure\": "
           << compositor.committed.history_ogre_exposure << ",\n"
           << "    \"history_minimum_auto_exposure\": "
           << compositor.committed.history_minimum_auto_exposure << ",\n"
           << "    \"history_maximum_auto_exposure\": "
           << compositor.committed.history_maximum_auto_exposure << ",\n"
           << "    \"history_average_log_luminance\": "
           << compositor.committed.history_average_log_luminance << ",\n"
           << "    \"history_previous_inverse_luminance_r16_bits\": "
           << compositor.committed.history_previous_inverse_luminance_r16_bits
           << ",\n"
           << "    \"history_delta_seconds\": "
           << compositor.committed.history_delta_seconds << ",\n"
           << "    \"history_absolute_error\": "
           << compositor.committed.history_absolute_error << ",\n"
           << "    \"history_allowed_error\": "
           << compositor.committed.history_allowed_error << ",\n"
           << "    \"history_conditioning_bound\": "
           << compositor.committed.history_conditioning_bound << ",\n"
           << "    \"history_binary32_rounding_bound\": "
           << compositor.committed.history_binary32_rounding_bound << ",\n"
           << "    \"history_storage_ulp\": "
           << compositor.committed.history_storage_ulp << ",\n"
           << "    \"history_r16_ulp_distance\": "
           << compositor.committed.history_r16_ulp_distance << ",\n"
           << "    \"history_changed_from_initial\": "
           << (compositor.committed.previous_inverse_luminance_r16_bits !=
                       compositor.initialized.previous_inverse_luminance_r16_bits
                   ? "true"
                   : "false")
           << ",\n"
           << "    \"exposure_changed_pixels\": "
           << compositor.exposure_changed_pixels << ",\n"
           << "    \"ui_overlay_control_node\": \"HdrRenderUi\",\n"
           << "    \"ui_overlay_control_kind\": \"Ogre::v1::Overlay\",\n"
           << "    \"ui_overlay_control_changed_pixels\": "
           << compositor.ui_overlay_control_changed_pixels
           << ",\n"
           << "    \"ui_overlay_control_magenta_pixels\": "
           << compositor.ui_overlay_control_magenta_pixels << ",\n"
           << "    \"ui_overlay_control_fnv1a64\": \""
           << HexHash(compositor.ui_overlay_control.attachment_fnv1a64)
           << "\",\n"
           << "    \"initialization_failure_stages_verified\": "
           << compositor.initialization_failure_stages_verified << ",\n"
           << "    \"same_object_reinitialize_verified\": "
           << (compositor.same_object_reinitialize_verified ? "true" : "false")
           << ",\n"
           << "    \"frame_commit_prepare_failure_verified\": "
           << (compositor.frame_commit_prepare_failure_verified ? "true"
                                                                 : "false")
           << ",\n"
           << "    \"aborted_hdr_audit_unchanged\": "
           << (compositor.aborted_hdr_audit_unchanged ? "true" : "false")
           << ",\n"
           << "    \"aborted_reflection_audit_unchanged\": "
           << (compositor.aborted_reflection_audit_unchanged ? "true"
                                                              : "false")
           << ",\n"
           << "    \"aborted_submission_uncommitted\": "
           << (compositor.aborted_submission_uncommitted ? "true" : "false")
           << ",\n"
           << "    \"aborted_output_unchanged\": "
           << (compositor.aborted_output_unchanged ? "true" : "false")
           << ",\n"
           << "    \"post_render_failure_fault_latched\": "
           << (compositor.post_render_failure_fault_latched ? "true"
                                                            : "false")
           << ",\n"
           << "    \"suspend_restore_preserved_graph\": "
           << (compositor.suspend_restore_preserved_graph ? "true"
                                                           : "false")
           << ",\n"
           << "    \"invalid_resize_rollback_verified\": "
           << (compositor.invalid_resize_rollback_verified ? "true"
                                                            : "false")
           << ",\n"
           << "    \"resize_rebuild_verified\": "
           << (compositor.resize_rebuild_verified ? "true" : "false")
           << ",\n"
           << "    \"resized_frame_verified\": "
           << (compositor.resized_frame_verified ? "true" : "false")
           << ",\n"
           << "    \"first_attachment_fnv1a64\": \""
           << HexHash(compositor.first.attachment_fnv1a64) << "\",\n"
           << "    \"final_attachment_fnv1a64\": \""
           << HexHash(compositor.final.attachment_fnv1a64) << "\",\n"
           << "    \"clean_shutdown\": "
           << (compositor.clean_shutdown ? "true" : "false") << "\n"
           << "  },\n"
           << "  \"hdr_compositor_visual\": {\n"
           << "    \"schema\": \"ror.ogre_next_hdr_compositor_visual.v2\",\n"
           << "    \"evidence_file\": \""
           << std::filesystem::u8path(compositor_evidence_path)
                  .filename()
                  .generic_u8string()
           << "\",\n"
           << "    \"ppm_attachment\": \"final_ui_free\",\n"
           << "    \"width\": " << kWidth << ",\n"
           << "    \"height\": " << kHeight << ",\n"
           << "    \"bytes_per_pixel\": 4,\n"
           << "    \"attachments\": [\n";
    const std::array<std::pair<const char *, const Metrics *>, 3U>
        compositor_attachments{{
            {"first_ui_free", &compositor.first},
            {"final_ui_free", &compositor.final},
            {"ui_overlay_control", &compositor.ui_overlay_control},
        }};
    std::size_t compositor_offset = 0U;
    for (std::size_t index = 0U; index < compositor_attachments.size();
         ++index) {
      const Metrics &metrics = *compositor_attachments[index].second;
      const std::size_t changed =
          index == 0U
              ? 0U
              : CountChangedPixels(compositor.first.attachment_bytes,
                                   metrics.attachment_bytes, 4U);
      report << "      {\"name\": \""
             << compositor_attachments[index].first << "\", \"offset\": "
             << compositor_offset << ", \"bytes\": "
             << metrics.attachment_bytes.size()
             << ", \"exact_fnv1a64\": \""
             << HexHash(metrics.attachment_fnv1a64)
             << "\", \"changed_pixels_from_first\": " << changed << "}"
             << (index + 1U == compositor_attachments.size() ? "\n"
                                                             : ",\n");
      compositor_offset += metrics.attachment_bytes.size();
    }
    report << "    ],\n"
           << "    \"linear_split_attachments\": [\n";
    const std::array<const char *, 4U> split_names{{
        "base_hdr", "sun_full_unoccluded_hdr", "sun_direct_hdr",
        "raster_lit_hdr"}};
    const std::size_t split_bytes =
        static_cast<std::size_t>(kWidth) * kHeight * 8U;
    for (std::size_t index = 0U; index < split_names.size(); ++index) {
      report << "      {\"name\": \"" << split_names[index]
             << "\", \"offset\": " << compositor_offset
             << ", \"bytes\": " << split_bytes
             << ", \"format\": \"RGBA16_FLOAT\", \"exact_fnv1a64\": \""
             << HexHash(compositor.split_content_fnv1a64[index]) << "\"}"
             << (index + 1U == split_names.size() ? "\n" : ",\n");
      compositor_offset += split_bytes;
    }
    report << "    ],\n"
           << "    \"evidence_bytes\": " << compositor_offset << "\n"
           << "  },\n";
  }
  report
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
         << "    \"exact_attachment_fnv1a64\": \""
         << HexHash(hdr.attachment_fnv1a64) << "\",\n"
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
         << "    \"exact_attachment_fnv1a64\": \""
         << HexHash(sdr.attachment_fnv1a64) << "\",\n"
         << "    \"rgb8_fnv1a64\": \"" << HexHash(sdr.fnv1a64)
         << "\"\n"
         << "  },\n"
         << "  \"lifecycle\": {\n"
         << "    \"unsupported_depth_failed_before_submission\": true,\n"
         << "    \"double_sided_pbs_readback\": true,\n"
         << "    \"lifetime_snapshot_identity_replay\": true,\n"
         << "    \"lifetime_completed_frame_queries\": true,\n"
         << "    \"process_global_root_exclusion\": true,\n"
         ;
  if (modern_pbr) {
    report << "    \"non_uniform_scale_rejected_before_submission\": "
           << (result.non_uniform_scale_rejected_before_submission ? "true"
                                                                    : "false")
           << ",\n"
           << "    \"live_texture_replacement_retirement\": "
           << (result.live_replacement_retirement ? "true" : "false")
           << ",\n"
           << "    \"replacement_audit\": {\"creates\": "
           << result.replacement_final_audit.native_allocation_creates
           << ", \"destroys\": "
           << result.replacement_final_audit.native_allocation_destroys
           << ", \"live\": "
           << result.replacement_final_audit.live_native_allocations
           << ", \"retired_name_lookups\": "
           << result.replacement_final_audit.retired_name_lookups
           << ", \"retired_name_rejections\": "
           << result.replacement_final_audit.retired_name_rejections
           << ", \"exact_usage\": "
           << (result.replacement_final_audit.exact_usage ? "true" : "false")
           << "},\n";
  }
  report
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

float SrgbDisplayDomainEotf(float encoded) {
  return encoded <= 0.04045F
             ? encoded / 12.92F
             : std::pow((encoded + 0.055F) / 1.055F, 2.4F);
}

std::pair<bool, bool>
RunDisplayDomainUsageTransitionProof(const std::string &media_root) {
  const RenderAssetDelta decode_before = MakeDisplayDomainTransferCatalog(
      BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER, 1U, 1U);
  const RenderAssetDelta display_domain = MakeDisplayDomainTransferCatalog(
      BaseColorTransfer::SRGB_DISPLAY_DOMAIN_FILTER_THEN_DECODE, 2U, 2U);
  const RenderAssetDelta decode_before_replacement =
      MakeDisplayDomainTransferCatalog(
          BaseColorTransfer::SRGB_DECODE_BEFORE_FILTER, 3U, 3U);
  const auto decode_before_scene = MakeDisplayDomainUnlitScene(
      1U, 1U, 910U, Float3{1.0F, 1.0F, 1.0F});
  const auto display_domain_scene =
      MakeDisplayDomainUnlitScene(2U, 2U, 911U);
  const auto decode_before_replacement_scene = MakeDisplayDomainUnlitScene(
      3U, 3U, 912U, Float3{1.0F, 1.0F, 1.0F});

  OgreNextN1Configuration configuration;
  configuration.shader_media_root = media_root;
  configuration.raster_feature_tier =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  configuration.texture_upload_failure_stage =
      OgreNextN1TextureUploadFailureStage::
          AFTER_ROLE_TRANSITION_CANDIDATE_TEXTURES;
  OgreNextN1Frontend frontend(std::move(configuration));
  InitializeAndSync(frontend, decode_before);

  const auto require_audit =
      [](const OgreNextN1TextureAllocationAudit &audit,
         std::uint64_t creates, std::uint64_t destroys,
         std::uint64_t live, const char *label) {
        Require(audit.version == 2U && audit.live_source_textures == 1U &&
                    audit.sampled_rgba_allocations == 1U &&
                    audit.linear_rgba_allocations == 0U &&
                    audit.roughness_r8_allocations == 0U &&
                    audit.metallic_r8_allocations == 0U &&
                    audit.normal_rg8_allocations == 0U &&
                    audit.native_allocation_creates == creates &&
                    audit.native_allocation_destroys == destroys &&
                    audit.live_native_allocations == live &&
                    audit.retired_name_lookups == destroys &&
                    audit.retired_name_rejections == destroys &&
                    audit.exact_usage,
                std::string("display-domain usage transition audit drifted at ") +
                    label);
      };
  require_audit(frontend.QueryTextureAllocationAudit(), 1U, 0U, 1U,
                "initial decode-before-filter");

  const RenderOperationResult injected =
      frontend.SynchronizeAssets(display_domain);
  Require(injected.code == RenderOperationCode::BACKEND_FAILURE &&
              injected.detail.find("injected RT4/V1") != std::string::npos,
          "display-domain usage-transition rollback seam did not fire");
  require_audit(frontend.QueryTextureAllocationAudit(), 2U, 1U, 1U,
                "injected candidate rollback");

  RenderFrameOutput preserved_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, decode_before_scene,
                               PixelFormat::RGBA16_FLOAT),
                     preserved_output),
                 "decode-before-filter Render after transition rollback");
  static_cast<void>(InspectHdr(preserved_output, false));

  RequireSuccess(frontend.SynchronizeAssets(display_domain),
                 "display-domain role-transition retry");
  require_audit(frontend.QueryTextureAllocationAudit(), 3U, 2U, 1U,
                "display-domain commit");
  RenderFrameOutput display_domain_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(2U, display_domain_scene,
                               PixelFormat::RGBA16_FLOAT),
                     display_domain_output),
                 "display-domain Render after role-transition commit");
  static_cast<void>(InspectHdr(display_domain_output, false));

  RequireSuccess(frontend.SynchronizeAssets(decode_before_replacement),
                 "decode-before-filter reverse role transition");
  require_audit(frontend.QueryTextureAllocationAudit(), 4U, 3U, 1U,
                "decode-before-filter reverse commit");
  RenderFrameOutput replacement_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(3U, decode_before_replacement_scene,
                               PixelFormat::RGBA16_FLOAT),
                     replacement_output),
                 "decode-before-filter Render after reverse transition");
  static_cast<void>(InspectHdr(replacement_output, false));

  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "display-domain usage-transition Shutdown");
  const OgreNextN1TextureAllocationAudit after_shutdown =
      frontend.QueryTextureAllocationAudit();
  Require(after_shutdown.version == 2U &&
              after_shutdown.native_allocation_creates == 4U &&
              after_shutdown.native_allocation_destroys == 4U &&
              after_shutdown.live_native_allocations == 0U &&
              after_shutdown.retired_name_lookups == 4U &&
              after_shutdown.retired_name_rejections == 4U,
          "display-domain usage transition leaked a native allocation");
  return {true, true};
}

SmokeResult::DisplayDomainUnlitEvidence
RunDisplayDomainUnlitProof(const std::string &media_root) {
  const RenderAssetDelta catalog = MakeDisplayDomainUnlitCatalog();
  const auto scene = MakeDisplayDomainUnlitScene();
  OgreNextN1Frontend frontend(OgreNextN1Configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1});
  InitializeAndSync(frontend, catalog);

  const OgreNextN1TextureAllocationAudit allocations =
      frontend.QueryTextureAllocationAudit();
  const OgreNextN1DisplayDomainUploadAudit native_upload =
      frontend.QueryDisplayDomainUploadAudit();
  SmokeResult::DisplayDomainUnlitEvidence evidence;
  evidence.complete_unorm_mips_uploaded =
      allocations.version == 2U && allocations.live_source_textures == 1U &&
      allocations.sampled_rgba_allocations == 1U &&
      allocations.linear_rgba_allocations == 0U &&
      allocations.roughness_r8_allocations == 0U &&
      allocations.metallic_r8_allocations == 0U &&
      allocations.normal_rg8_allocations == 0U &&
      allocations.live_native_allocations == 1U && allocations.exact_usage &&
      native_upload.version == 1U && native_upload.source_textures == 1U &&
      native_upload.native_readbacks == 1U &&
      native_upload.expected_mip_levels == 2U &&
      native_upload.verified_mip_levels == 2U &&
      native_upload.verified_rows == 3U &&
      native_upload.verified_texels == 5U &&
      native_upload.verified_rgba_bytes == 20U &&
      native_upload.exact_source_rgba_to_native_texture;

  RenderFrameOutput output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, scene, PixelFormat::RGBA16_FLOAT), output),
                 "display-domain Unlit RGBA16_FLOAT Render");
  const FrameAttachment &attachment =
      RequireAttachment(output, PixelFormat::RGBA16_FLOAT);
  Require(attachment.row_pitch_bytes == static_cast<std::uint64_t>(kWidth) * 8U &&
              attachment.bytes.size() ==
                  static_cast<std::size_t>(attachment.row_pitch_bytes) *
                      kHeight,
          "display-domain Unlit HDR readback layout changed");

  constexpr std::array<std::array<std::uint8_t, 4U>, 3U> kEncodedTexels{{
      {{0U, 255U, 0U, 255U}},
      {{32U, 96U, 160U, 224U}},
      {{64U, 64U, 192U, 192U}},
  }};
  std::array<HdrR16Float, 3U> expected_after{};
  std::array<HdrR16Float, 3U> expected_before{};
  float maximum_oracle_separation = 0.0F;
  for (std::size_t channel = 0U; channel < 3U; ++channel) {
    const auto normalized = [&](std::size_t texel) {
      return static_cast<float>(kEncodedTexels[channel][texel]) / 255.0F;
    };
    const float top = (normalized(0U) + normalized(1U)) * 0.5F;
    const float bottom = (normalized(2U) + normalized(3U)) * 0.5F;
    evidence.encoded_filtered[channel] = (top + bottom) * 0.5F;
    evidence.filter_then_eotf[channel] =
        SrgbDisplayDomainEotf(evidence.encoded_filtered[channel]);
    const float decoded_top =
        (SrgbDisplayDomainEotf(normalized(0U)) +
         SrgbDisplayDomainEotf(normalized(1U))) *
        0.5F;
    const float decoded_bottom =
        (SrgbDisplayDomainEotf(normalized(2U)) +
         SrgbDisplayDomainEotf(normalized(3U))) *
        0.5F;
    evidence.decode_before_filter[channel] =
        (decoded_top + decoded_bottom) * 0.5F;
    maximum_oracle_separation = std::max(
        maximum_oracle_separation,
        std::fabs(evidence.filter_then_eotf[channel] -
                  evidence.decode_before_filter[channel]));
    Require(QuantizeHdrR16Float(evidence.filter_then_eotf[channel],
                                expected_after[channel])
                    .ok() &&
                QuantizeHdrR16Float(evidence.decode_before_filter[channel],
                                    expected_before[channel])
                    .ok(),
            "display-domain Unlit CPU oracle could not quantize to RGBA16F");
  }
  Require(maximum_oracle_separation > 0.03F,
          "display-domain Unlit fixture does not separate transfer ordering");

  HdrR16Float expected_opaque_alpha;
  Require(QuantizeHdrR16Float(1.0F, expected_opaque_alpha).ok(),
          "display-domain Unlit opaque alpha oracle is invalid");
  std::size_t matching_opaque_alpha_pixels = 0U;
  for (std::size_t pixel = 0U;
       pixel < static_cast<std::size_t>(kWidth) * kHeight; ++pixel) {
    bool matches_after = true;
    bool matches_before = true;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      std::uint16_t observed = 0U;
      std::memcpy(&observed,
                  attachment.bytes.data() + pixel * 8U + channel * 2U,
                  sizeof(observed));
      matches_after =
          matches_after &&
          std::abs(static_cast<int>(observed) -
                   static_cast<int>(expected_after[channel].bits)) <= 2;
      matches_before =
          matches_before &&
          std::abs(static_cast<int>(observed) -
                   static_cast<int>(expected_before[channel].bits)) <= 2;
    }
    evidence.matching_foreground_pixels += matches_after ? 1U : 0U;
    evidence.decode_before_filter_pixels += matches_before ? 1U : 0U;
    std::uint16_t observed_alpha = 0U;
    std::memcpy(&observed_alpha,
                attachment.bytes.data() + pixel * 8U + 3U * 2U,
                sizeof(observed_alpha));
    matching_opaque_alpha_pixels +=
        matches_after &&
                std::abs(static_cast<int>(observed_alpha) -
                         static_cast<int>(expected_opaque_alpha.bits)) <= 1
            ? 1U
            : 0U;
  }
  evidence.no_cast_or_receive_shadow_flags =
      (scene->mesh_instances().front().flags &
       (MESH_INSTANCE_CASTS_SHADOW | MESH_INSTANCE_RECEIVES_SHADOW)) == 0U;
  evidence.full32_after_filter_shader_executed =
      evidence.matching_foreground_pixels >= 512U &&
      evidence.decode_before_filter_pixels == 0U;
  evidence.alpha_untouched_opaque =
      matching_opaque_alpha_pixels == evidence.matching_foreground_pixels &&
      matching_opaque_alpha_pixels >= 512U;
  Require(evidence.complete_unorm_mips_uploaded &&
              evidence.full32_after_filter_shader_executed &&
              evidence.alpha_untouched_opaque &&
              evidence.no_cast_or_receive_shadow_flags,
          "display-domain Unlit did not execute filter-then-EOTF from complete UNORM mips");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "display-domain Unlit proof Shutdown");
  const std::pair<bool, bool> transition =
      RunDisplayDomainUsageTransitionProof(media_root);
  evidence.usage_transition_rollback_exact = transition.first;
  evidence.usage_transition_commit_exact = transition.second;
  return evidence;
}

SmokeResult::DynamicMeshEvidence
RunDynamicMeshProof(const std::string &media_root, bool modern_pbr) {
  const RenderAssetDelta catalog = MakeDynamicCatalog(modern_pbr);
  const auto base_scene = MakeDynamicScene(850U, modern_pbr, false);
  const auto deformed_scene = MakeDynamicScene(851U, modern_pbr, true);

  OgreNextN1Configuration configuration{
      media_root,
      modern_pbr ? OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1
                 : OgreNextRasterFeatureTier::STATIC_PBR_N1};
  OgreNextN1Frontend frontend(std::move(configuration));
  InitializeAndSync(frontend, catalog);
  Require(frontend.QueryCapabilities().supports_dynamic_mesh_updates,
          "initialized frontend did not advertise full dynamic updates");

  RenderFrameOutput base_output;
  RenderFrameOutput deformed_output;
  RenderFrameOutput base_replay_output;
  RenderFrameOutput deformed_replay_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, base_scene, PixelFormat::RGBA8_SRGB),
                     base_output),
                 "dynamic base Render");
  RequireSuccess(frontend.Render(
                     MakeFrame(2U, deformed_scene, PixelFormat::RGBA8_SRGB),
                     deformed_output),
                 "full dynamic deformation Render");
  RequireSuccess(frontend.Render(
                     MakeFrame(3U, base_scene, PixelFormat::RGBA8_SRGB),
                     base_replay_output),
                 "dynamic base replay Render");
  RequireSuccess(frontend.Render(
                     MakeFrame(4U, deformed_scene, PixelFormat::RGBA8_SRGB),
                     deformed_replay_output),
                 "full dynamic deformation replay Render");

  SmokeResult::DynamicMeshEvidence evidence;
  evidence.base = InspectSdr(base_output);
  evidence.deformed = InspectSdr(deformed_output);
  const Metrics base_replay = InspectSdr(base_replay_output);
  const Metrics deformed_replay = InspectSdr(deformed_replay_output);
  evidence.changed_pixels = CountChangedPixels(
      evidence.base.attachment_bytes, evidence.deformed.attachment_bytes, 4U);
  evidence.base_exact_replay =
      base_replay.attachment_bytes == evidence.base.attachment_bytes;
  evidence.deformed_exact_replay =
      deformed_replay.attachment_bytes == evidence.deformed.attachment_bytes;
  Require(evidence.changed_pixels >= 256U &&
              evidence.base.attachment_fnv1a64 !=
                  evidence.deformed.attachment_fnv1a64 &&
              evidence.base_exact_replay && evidence.deformed_exact_replay,
          "full dynamic mesh replacement was not visible and deterministic");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "dynamic mesh proof Shutdown");
  return evidence;
}

SmokeResult::TangentHandednessEvidence
RunTangentHandednessProof(const std::string &media_root) {
  const RenderAssetDelta positive_catalog =
      MakeTangentHandednessCatalog(false);
  const RenderAssetDelta negative_catalog =
      MakeTangentHandednessCatalog(true);
  RequireControlledTangentHandednessCatalogs(positive_catalog,
                                             negative_catalog);
  constexpr float kSqrtHalf = 0.707106769F;
  const Float3 angled_light{0.0F, -kSqrtHalf, -kSqrtHalf};
  const auto positive_scene = MakeScene(800U, false, true, 1U, 1U,
                                        Matrix4x4{}, 1U, angled_light);
  const auto negative_scene = MakeScene(801U, false, true, 2U, 1U,
                                        Matrix4x4{}, 2U, angled_light);

  OgreNextN1Frontend frontend(OgreNextN1Configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1});
  InitializeAndSync(frontend, positive_catalog);
  RenderFrameRequest positive_hdr_frame =
      MakeFrame(1U, positive_scene, PixelFormat::RGBA16_FLOAT);
  RenderFrameRequest positive_sdr_frame =
      MakeFrame(2U, positive_scene, PixelFormat::RGBA8_SRGB);
  RenderFrameOutput positive_hdr_output;
  RenderFrameOutput positive_sdr_output;
  RequireSuccess(frontend.Render(positive_hdr_frame, positive_hdr_output),
                 "RT4 positive tangent-w HDR Render");
  RequireSuccess(frontend.Render(positive_sdr_frame, positive_sdr_output),
                 "RT4 positive tangent-w SDR Render");

  RequireSuccess(frontend.SynchronizeAssets(negative_catalog),
                 "RT4 negative tangent-w SynchronizeAssets");
  RenderFrameRequest negative_hdr_frame =
      MakeFrame(3U, negative_scene, PixelFormat::RGBA16_FLOAT);
  RenderFrameRequest negative_sdr_frame =
      MakeFrame(4U, negative_scene, PixelFormat::RGBA8_SRGB);
  RequireControlledSceneAndView(*positive_scene, *negative_scene,
                                positive_hdr_frame, negative_hdr_frame, true);
  RequireControlledSceneAndView(*positive_scene, *negative_scene,
                                positive_sdr_frame, negative_sdr_frame, true);
  RenderFrameOutput negative_hdr_output;
  RenderFrameOutput negative_sdr_output;
  RequireSuccess(frontend.Render(negative_hdr_frame, negative_hdr_output),
                 "RT4 negative tangent-w HDR Render");
  RequireSuccess(frontend.Render(negative_sdr_frame, negative_sdr_output),
                 "RT4 negative tangent-w SDR Render");

  SmokeResult::TangentHandednessEvidence evidence;
  // This pair is a comparative TBN proof: the common angled light can
  // legitimately place one sign below the primary smoke's independent HDR
  // headroom threshold. InspectHdr still requires finite opaque half-floats
  // and visible geometry; the exact sign effect is required below.
  evidence.positive_hdr = InspectHdr(positive_hdr_output, false);
  evidence.positive_sdr = InspectSdr(positive_sdr_output);
  evidence.negative_hdr = InspectHdr(negative_hdr_output, false);
  evidence.negative_sdr = InspectSdr(negative_sdr_output);
  evidence.hdr_changed_pixels = CountChangedPixels(
      evidence.positive_hdr.attachment_bytes,
      evidence.negative_hdr.attachment_bytes, 8U);
  evidence.sdr_changed_pixels = CountChangedPixels(
      evidence.positive_sdr.attachment_bytes,
      evidence.negative_sdr.attachment_bytes, 4U);
  evidence.only_tangent_w_changed = true;
  Require(evidence.hdr_changed_pixels >= 64U &&
              evidence.sdr_changed_pixels >= 64U &&
              evidence.positive_hdr.attachment_fnv1a64 !=
                  evidence.negative_hdr.attachment_fnv1a64 &&
              evidence.positive_sdr.attachment_fnv1a64 !=
                  evidence.negative_sdr.attachment_fnv1a64,
          "RT4 authored tangent-w sign produced no exact native HDR/SDR effect");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "RT4 tangent-handedness Shutdown");
  return evidence;
}

void RequireRetirementAudit(const OgreNextN1TextureAllocationAudit &audit,
                            std::uint64_t creates,
                            std::uint64_t destroys,
                            std::uint64_t live,
                            const std::string &label,
                            bool require_exact_usage = true) {
  Require(audit.version == 2U &&
              audit.live_source_textures == (live > 0U ? 1U : 0U) &&
              audit.sampled_rgba_allocations == 0U &&
              audit.linear_rgba_allocations == 0U &&
              audit.roughness_r8_allocations == 0U &&
              audit.metallic_r8_allocations == 0U &&
              audit.normal_rg8_allocations == live &&
              audit.native_allocation_creates == creates &&
              audit.native_allocation_destroys == destroys &&
              audit.live_native_allocations == live &&
              audit.retired_name_lookups == destroys &&
              audit.retired_name_rejections == destroys &&
              (!require_exact_usage || audit.exact_usage),
          "RT4 normal retirement allocation/name audit drifted at " + label);
}

SmokeResult::TextureRetirementEvidence
RunTextureRetirementProof(const std::string &media_root) {
  SmokeResult::TextureRetirementEvidence evidence;
  OgreNextN1Frontend frontend(OgreNextN1Configuration{
      media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1});
  const RenderAssetDelta initial_catalog = MakeRetirementCatalog(1U);
  InitializeAndSync(frontend, initial_catalog);
  evidence.initial = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.initial, 1U, 0U, 1U, "initial 2x2/one-mip");
  RenderFrameOutput initial_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, MakeRetirementScene(1U),
                               PixelFormat::RGBA8_SRGB),
                     initial_output),
                 "RT4 retirement initial Render");
  static_cast<void>(InspectSdr(initial_output));

  const RenderAssetDelta expanded_catalog = MakeRetirementCatalog(2U);
  RequireSuccess(frontend.SynchronizeAssets(expanded_catalog),
                 "RT4 retirement expand SynchronizeAssets");
  evidence.expanded = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.expanded, 2U, 1U, 1U,
                         "expanded 4x2/two-mip");
  evidence.expanded_normal_upload = frontend.QueryNormalUploadAudit();
  Require(evidence.expanded_normal_upload.version == 1U &&
              evidence.expanded_normal_upload.verified_uploads == 2U &&
              evidence.expanded_normal_upload.verified_mip_levels == 3U &&
              evidence.expanded_normal_upload.verified_rows == 5U &&
              evidence.expanded_normal_upload.verified_texels == 14U &&
              evidence.expanded_normal_upload.verified_rg_bytes == 28U &&
              evidence.expanded_normal_upload
                      .verified_padded_source_rows == 5U &&
              evidence.expanded_normal_upload
                  .exact_source_rg_to_native_image,
          "RT4 padded multi-mip source RG bytes did not survive exactly in Ogre Image2");
  RenderFrameOutput expanded_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(2U, MakeRetirementScene(2U),
                               PixelFormat::RGBA8_SRGB),
                     expanded_output),
                 "RT4 retirement expanded Render");
  static_cast<void>(InspectSdr(expanded_output));

  const RenderAssetDelta restored_catalog = MakeRetirementCatalog(3U);
  RequireSuccess(frontend.SynchronizeAssets(restored_catalog),
                 "RT4 retirement restore SynchronizeAssets");
  evidence.restored = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.restored, 3U, 2U, 1U,
                         "restored 2x2/one-mip");
  RenderFrameOutput restored_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(3U, MakeRetirementScene(3U),
                               PixelFormat::RGBA8_SRGB),
                     restored_output),
                 "RT4 retirement restored Render");
  static_cast<void>(InspectSdr(restored_output));

  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "RT4 retirement first Shutdown");
  evidence.first_shutdown = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.first_shutdown, 3U, 3U, 0U,
                         "first shutdown", false);

  InitializeAndSync(frontend, restored_catalog);
  evidence.restarted = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.restarted, 4U, 3U, 1U, "restart");
  RenderFrameOutput restarted_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, MakeRetirementScene(3U),
                               PixelFormat::RGBA8_SRGB),
                     restarted_output),
                 "RT4 retirement restart Render");
  static_cast<void>(InspectSdr(restarted_output));
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "RT4 retirement final Shutdown");
  evidence.final_shutdown = frontend.QueryTextureAllocationAudit();
  RequireRetirementAudit(evidence.final_shutdown, 4U, 4U, 0U,
                         "final shutdown", false);

  evidence.exact_extent_and_mip_transitions = true;
  evidence.renders_through_transitions_and_restart = true;
  evidence.old_names_rejected =
      evidence.final_shutdown.retired_name_lookups == 4U &&
      evidence.final_shutdown.retired_name_rejections == 4U;
  Require(evidence.old_names_rejected,
          "RT4 retirement old names remained discoverable");
  return evidence;
}

std::vector<SmokeResult::TextureUploadRollbackStageEvidence>
RunTextureUploadRollbackProof(const std::string &media_root) {
  using FailureStage = OgreNextN1TextureUploadFailureStage;
  const std::array<std::pair<FailureStage, const char *>, 5U> stages{{
      {FailureStage::AFTER_CREATE, "after_create"},
      {FailureStage::AFTER_SET_RESOLUTION, "after_set_resolution"},
      {FailureStage::AFTER_SET_MIPMAPS, "after_set_mipmaps"},
      {FailureStage::AFTER_SET_PIXEL_FORMAT, "after_set_pixel_format"},
      {FailureStage::AFTER_SCHEDULE_TRANSITION,
       "after_schedule_transition"},
  }};
  std::vector<SmokeResult::TextureUploadRollbackStageEvidence> evidence;
  evidence.reserve(stages.size());
  for (const auto &stage : stages) {
    OgreNextN1Frontend frontend(OgreNextN1Configuration{
        media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1,
        stage.first});
    RequireSuccess(frontend.Initialize(Initialization()),
                   std::string("RT4 rollback Initialize(") + stage.second + ')');
    const RenderAssetDelta initial_catalog = MakeRetirementCatalog(1U);
    const RenderOperationResult injected =
        frontend.SynchronizeAssets(initial_catalog);
    Require(injected.code == RenderOperationCode::BACKEND_FAILURE,
            std::string("RT4 rollback injection did not fail at ") +
                stage.second);

    SmokeResult::TextureUploadRollbackStageEvidence record;
    record.name = stage.second;
    record.after_failure = frontend.QueryTextureAllocationAudit();
    RequireRetirementAudit(record.after_failure, 1U, 1U, 0U,
                           record.name + " rollback");

    RequireSuccess(frontend.SynchronizeAssets(initial_catalog),
                   std::string("RT4 rollback retry(") + stage.second + ')');
    record.after_retry = frontend.QueryTextureAllocationAudit();
    RequireRetirementAudit(record.after_retry, 2U, 1U, 1U,
                           record.name + " retry");
    RenderFrameOutput retry_output;
    RequireSuccess(frontend.Render(
                       MakeFrame(1U, MakeRetirementScene(1U),
                                 PixelFormat::RGBA8_SRGB),
                       retry_output),
                   std::string("RT4 rollback retry Render(") + stage.second +
                       ')');
    static_cast<void>(InspectSdr(retry_output));

    const RenderAssetDelta replacement_catalog = MakeRetirementCatalog(2U);
    RequireSuccess(frontend.SynchronizeAssets(replacement_catalog),
                   std::string("RT4 rollback replacement(") + stage.second +
                       ')');
    record.after_replacement = frontend.QueryTextureAllocationAudit();
    RequireRetirementAudit(record.after_replacement, 3U, 2U, 1U,
                           record.name + " replacement");
    RenderFrameOutput replacement_output;
    RequireSuccess(frontend.Render(
                       MakeFrame(2U, MakeRetirementScene(2U),
                                 PixelFormat::RGBA8_SRGB),
                       replacement_output),
                   std::string("RT4 rollback replacement Render(") +
                       stage.second + ')');
    static_cast<void>(InspectSdr(replacement_output));

    RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                   std::string("RT4 rollback Shutdown(") + stage.second + ')');
    record.after_shutdown = frontend.QueryTextureAllocationAudit();
    RequireRetirementAudit(record.after_shutdown, 3U, 3U, 0U,
                           record.name + " shutdown", false);
    evidence.push_back(std::move(record));
  }
  return evidence;
}

void RunAnalyticSkyRollbackProof(
    const std::string &media_root,
    SmokeResult::AnalyticSkyEvidence &evidence) {
  using FailureStage = OgreNextN1AnalyticSkyFailureStage;
  const std::array<std::pair<FailureStage, const char *>, 20U> stages{{
      {FailureStage::AFTER_BACKGROUND_DATABLOCK,
       "after_background_datablock"},
      {FailureStage::AFTER_SUN_DATABLOCK, "after_sun_datablock"},
      {FailureStage::AFTER_BACKGROUND_MESH, "after_background_mesh"},
      {FailureStage::AFTER_BACKGROUND_CPU_VERTEX_ALLOCATION,
       "after_background_cpu_vertex_allocation"},
      {FailureStage::AFTER_BACKGROUND_VERTEX_BUFFER,
       "after_background_vertex_buffer"},
      {FailureStage::AFTER_BACKGROUND_CPU_INDEX_ALLOCATION,
       "after_background_cpu_index_allocation"},
      {FailureStage::AFTER_BACKGROUND_INDEX_BUFFER,
       "after_background_index_buffer"},
      {FailureStage::AFTER_BACKGROUND_VAO, "after_background_vao"},
      {FailureStage::AFTER_BACKGROUND_SUBMESH_ATTACH,
       "after_background_submesh_attach"},
      {FailureStage::AFTER_SUN_MESH, "after_sun_mesh"},
      {FailureStage::AFTER_SUN_CPU_VERTEX_ALLOCATION,
       "after_sun_cpu_vertex_allocation"},
      {FailureStage::AFTER_SUN_VERTEX_BUFFER,
       "after_sun_vertex_buffer"},
      {FailureStage::AFTER_SUN_CPU_INDEX_ALLOCATION,
       "after_sun_cpu_index_allocation"},
      {FailureStage::AFTER_SUN_INDEX_BUFFER, "after_sun_index_buffer"},
      {FailureStage::AFTER_SUN_VAO, "after_sun_vao"},
      {FailureStage::AFTER_SUN_SUBMESH_ATTACH,
       "after_sun_submesh_attach"},
      {FailureStage::AFTER_BACKGROUND_ITEM, "after_background_item"},
      {FailureStage::AFTER_SUN_ITEM, "after_sun_item"},
      {FailureStage::AFTER_SCENE_NODE, "after_scene_node"},
      {FailureStage::AFTER_ATTACHED_STATE_VERIFICATION,
       "after_attached_state_verification"},
  }};
  const auto lifetime_balanced = [](const OgreNextAnalyticSkyRuntimeAudit &audit) {
    return audit.native_mesh_creates == audit.native_mesh_destroys &&
           audit.native_vertex_buffer_creates ==
               audit.native_vertex_buffer_destroys &&
           audit.native_index_buffer_creates ==
               audit.native_index_buffer_destroys &&
           audit.native_vao_creates == audit.native_vao_destroys &&
           audit.native_item_creates == audit.native_item_destroys &&
           audit.native_scene_node_creates ==
               audit.native_scene_node_destroys &&
           audit.native_datablock_creates ==
               audit.native_datablock_destroys &&
           audit.native_mesh_absence_checks ==
               audit.native_mesh_destroys &&
           audit.native_item_absence_checks ==
               audit.native_item_destroys &&
           audit.native_scene_node_absence_checks ==
               audit.native_scene_node_destroys &&
           audit.native_datablock_absence_checks ==
               audit.native_datablock_destroys;
  };
  evidence.rollback_publication_unchanged = true;
  evidence.rollback_lifetimes_balanced = true;
  evidence.clean_retry = true;
  for (std::size_t index = 0U; index < stages.size(); ++index) {
    OgreNextN1Configuration configuration;
    configuration.shader_media_root = media_root;
    configuration.raster_feature_tier =
        OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
    configuration.analytic_sky_failure_stage = stages[index].first;
    configuration.retain_analytic_sky_geometry_content_evidence = true;
    OgreNextN1Frontend frontend(std::move(configuration));
    InitializeAndSync(frontend, MakeCatalog(true, &kVariantSpecs.front()));

    RenderFrameOutput untouched;
    untouched.frame_id = 777U;
    const RenderOperationResult injected = frontend.Render(
        MakeFrame(1U, MakeScene(950U + index, false, true),
                  PixelFormat::RGBA8_SRGB),
        untouched);
    const OgreNextAnalyticSkyRuntimeAudit after_failure =
        frontend.QueryAnalyticSkyAudit();
    const bool publication_unchanged =
        after_failure.version == 2U &&
        after_failure.native_render_policy_version == 1U &&
        after_failure.completed_frames == 0U &&
        after_failure.last_sun_light_id == 0U &&
        after_failure.last_cpu_geometry_fnv1a64 == 0U &&
        !after_failure.last_descriptor.enabled &&
        !after_failure.camera_centered && !after_failure.rendered_first &&
        !after_failure.depth_check_disabled &&
        !after_failure.depth_write_disabled &&
        !after_failure.additive_sun_disk &&
        !after_failure.separate_sun_alpha_replace &&
        !after_failure.native_geometry_metadata_verified &&
        !after_failure.exact_native_geometry_readback &&
        !after_failure.casts_shadows &&
        !after_failure.portable_scene_identity_absent;
    const bool failure_balanced = lifetime_balanced(after_failure);
    Require(injected.code == RenderOperationCode::BACKEND_FAILURE &&
                untouched.frame_id == 777U &&
                !frontend.IsFrameComplete(1U) && publication_unchanged &&
                failure_balanced,
            std::string("analytic-sky rollback published partial state at ") +
                stages[index].second);
    evidence.rollback_publication_unchanged =
        evidence.rollback_publication_unchanged && publication_unchanged;
    evidence.rollback_lifetimes_balanced =
        evidence.rollback_lifetimes_balanced && failure_balanced;

    RenderFrameOutput recovered;
    RequireSuccess(frontend.Render(
                       MakeFrame(1U,
                                 MakeScene(950U + index, false, true),
                                 PixelFormat::RGBA8_SRGB),
                       recovered),
                   std::string("analytic-sky clean retry at ") +
                       stages[index].second);
    static_cast<void>(InspectSdr(recovered));
    const OgreNextAnalyticSkyRuntimeAudit after_retry =
        frontend.QueryAnalyticSkyAudit();
    const bool retry_exact =
        after_retry.completed_frames == 1U &&
        lifetime_balanced(after_retry) &&
        after_retry.native_mesh_creates >= 2U &&
        after_retry.native_item_creates >= 2U &&
        after_retry.native_scene_node_creates >= 1U &&
        after_retry.native_datablock_creates >= 2U &&
        after_retry.native_gpu_content_readbacks >= 4U &&
        after_retry.native_state_verifications >= 1U &&
        after_retry.last_sun_light_id == 1U &&
        after_retry.last_cpu_geometry_fnv1a64 != 0U &&
        after_retry.last_descriptor.enabled &&
        after_retry.last_descriptor.sun_light_id == 1U &&
        after_retry.camera_centered && after_retry.rendered_first &&
        after_retry.depth_check_disabled &&
        after_retry.depth_write_disabled &&
        after_retry.additive_sun_disk &&
        after_retry.separate_sun_alpha_replace &&
        after_retry.native_geometry_metadata_verified &&
        after_retry.exact_native_geometry_readback &&
        !after_retry.casts_shadows &&
        after_retry.portable_scene_identity_absent;
    Require(retry_exact,
            std::string("analytic-sky retry audit drifted at ") +
                stages[index].second);
    evidence.clean_retry = evidence.clean_retry && retry_exact;
    RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                   std::string("analytic-sky rollback Shutdown at ") +
                       stages[index].second);
    ++evidence.rollback_stages_verified;
  }
}

void RunAnalyticSkyProductionDefaultReadbackProof(
    const std::string &media_root,
    SmokeResult::AnalyticSkyEvidence &evidence) {
  OgreNextN1Configuration configuration;
  configuration.shader_media_root = media_root;
  configuration.raster_feature_tier =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  OgreNextN1Frontend frontend(std::move(configuration));
  InitializeAndSync(frontend, MakeCatalog(true, &kVariantSpecs.front()));

  RenderFrameOutput output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, MakeScene(989U, false, true),
                               PixelFormat::RGBA8_SRGB),
                     output),
                 "analytic-sky production-default readback Render");
  static_cast<void>(InspectSdr(output));
  const OgreNextAnalyticSkyRuntimeAudit audit =
      frontend.QueryAnalyticSkyAudit();
  evidence.production_default_gpu_content_readbacks_zero =
      audit.completed_frames == 1U &&
      audit.native_gpu_content_readbacks == 0U &&
      audit.native_state_verifications == 1U &&
      audit.last_cpu_geometry_fnv1a64 != 0U &&
      audit.native_geometry_metadata_verified &&
      !audit.exact_native_geometry_readback &&
      audit.native_mesh_creates == audit.native_mesh_destroys &&
      audit.native_vertex_buffer_creates ==
          audit.native_vertex_buffer_destroys &&
      audit.native_index_buffer_creates ==
          audit.native_index_buffer_destroys &&
      audit.native_vao_creates == audit.native_vao_destroys &&
      audit.native_item_creates == audit.native_item_destroys &&
      audit.native_scene_node_creates == audit.native_scene_node_destroys &&
      audit.native_datablock_creates == audit.native_datablock_destroys;
  Require(evidence.production_default_gpu_content_readbacks_zero,
          "analytic-sky production-default path performed a GPU content readback or missed native metadata/state verification");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "analytic-sky production-default readback Shutdown");
}

void RunAnalyticSkyVisualProof(
    const std::string &media_root,
    SmokeResult::AnalyticSkyEvidence &evidence) {
  OgreNextN1Configuration configuration;
  configuration.shader_media_root = media_root;
  configuration.raster_feature_tier =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  configuration.retain_analytic_sky_geometry_content_evidence = true;
  OgreNextN1Frontend frontend(std::move(configuration));
  InitializeAndSync(frontend, MakeCatalog(true, &kVariantSpecs.front()));

  constexpr std::uint32_t kSkyWidth = 768U;
  constexpr std::uint32_t kSkyHeight = 512U;
  evidence.visual_width = kSkyWidth;
  evidence.visual_height = kSkyHeight;
  const auto sky_frame = [](std::uint64_t frame_id,
                            const std::shared_ptr<const SceneSnapshot> &scene,
                            PixelFormat format) {
    RenderFrameRequest request = MakeFrame(frame_id, scene, format);
    request.views.front().width = kSkyWidth;
    request.views.front().height = kSkyHeight;
    return request;
  };
  const auto inspect_hdr = [](const RenderFrameOutput &output) {
    Require(output.status == RenderFrameStatus::RENDERED &&
                !output.presented && output.attachments.size() == 1U,
            "analytic-sky HDR output was not one CPU attachment");
    const FrameAttachment &attachment = output.attachments.front();
    Require(attachment.output == FrameOutputMask::COLOR &&
                attachment.format == PixelFormat::RGBA16_FLOAT &&
                attachment.width == kSkyWidth &&
                attachment.height == kSkyHeight &&
                !attachment.gpu_resource.valid() &&
                attachment.row_pitch_bytes ==
                    static_cast<std::uint64_t>(kSkyWidth) * 8U &&
                attachment.bytes.size() ==
                    static_cast<std::size_t>(kSkyWidth) * kSkyHeight * 8U,
            "analytic-sky HDR attachment extent or layout drifted");
    Metrics metrics;
    metrics.attachment_bytes = attachment.bytes;
    metrics.attachment_fnv1a64 = HashBytes(metrics.attachment_bytes);
    std::map<std::uint32_t, std::size_t> runs;
    for (std::size_t pixel = 0U;
         pixel < static_cast<std::size_t>(kSkyWidth) * kSkyHeight; ++pixel) {
      float channels[4U]{};
      for (std::size_t channel = 0U; channel < 4U; ++channel) {
        std::uint16_t bits = 0U;
        std::memcpy(&bits,
                    attachment.bytes.data() + pixel * 8U + channel * 2U,
                    sizeof(bits));
        channels[channel] = HalfToFloat(bits);
        Require(std::isfinite(channels[channel]),
                "analytic-sky HDR attachment contains a non-finite value");
      }
      Require(channels[3U] >= 0.99F && channels[3U] <= 1.01F,
              "analytic-sky HDR attachment alpha is not opaque");
      Accumulate(metrics, runs, channels[0U], channels[1U], channels[2U]);
    }
    FinishMetrics(metrics, runs);
    return metrics;
  };
  const auto inspect_sdr = [](const RenderFrameOutput &output) {
    Require(output.status == RenderFrameStatus::RENDERED &&
                !output.presented && output.attachments.size() == 1U,
            "analytic-sky SDR output was not one CPU attachment");
    const FrameAttachment &attachment = output.attachments.front();
    Require(attachment.output == FrameOutputMask::COLOR &&
                attachment.format == PixelFormat::RGBA8_SRGB &&
                attachment.width == kSkyWidth &&
                attachment.height == kSkyHeight &&
                !attachment.gpu_resource.valid() &&
                attachment.row_pitch_bytes ==
                    static_cast<std::uint64_t>(kSkyWidth) * 4U &&
                attachment.bytes.size() ==
                    static_cast<std::size_t>(kSkyWidth) * kSkyHeight * 4U,
            "analytic-sky SDR attachment extent or layout drifted");
    Metrics metrics;
    metrics.attachment_bytes = attachment.bytes;
    metrics.attachment_fnv1a64 = HashBytes(metrics.attachment_bytes);
    metrics.rgb.reserve(
        static_cast<std::size_t>(kSkyWidth) * kSkyHeight * 3U);
    std::map<std::uint32_t, std::size_t> runs;
    for (std::size_t pixel = 0U;
         pixel < static_cast<std::size_t>(kSkyWidth) * kSkyHeight; ++pixel) {
      const std::uint8_t *rgba = attachment.bytes.data() + pixel * 4U;
      Require(rgba[3U] >= 250U,
              "analytic-sky SDR attachment alpha is not opaque");
      metrics.rgb.push_back(rgba[0U]);
      metrics.rgb.push_back(rgba[1U]);
      metrics.rgb.push_back(rgba[2U]);
      Accumulate(metrics, runs, static_cast<float>(rgba[0U]) / 255.0F,
                 static_cast<float>(rgba[1U]) / 255.0F,
                 static_cast<float>(rgba[2U]) / 255.0F);
    }
    FinishMetrics(metrics, runs);
    return metrics;
  };

  const Float3 camera_facing_sun_direction{0.0F, 0.0F, 1.0F};
  const auto sunless_scene = MakeScene(
      990U, false, true, 1U, 1U, Matrix4x4{}, 1U,
      camera_facing_sun_direction, 0.0F, false, false, true);
  const auto sun_scene = MakeScene(
      991U, false, true, 1U, 1U, Matrix4x4{}, 1U,
      camera_facing_sun_direction, 0.0F, false, false, false);

  RenderFrameOutput sunless_hdr_output;
  RequireSuccess(frontend.Render(
                     sky_frame(1U, sunless_scene,
                               PixelFormat::RGBA16_FLOAT),
                     sunless_hdr_output),
                 "analytic-sky camera-facing sunless HDR Render");
  evidence.camera_facing_sunless_hdr = inspect_hdr(sunless_hdr_output);

  RenderFrameOutput sun_hdr_output;
  RequireSuccess(frontend.Render(
                     sky_frame(2U, sun_scene, PixelFormat::RGBA16_FLOAT),
                     sun_hdr_output),
                 "analytic-sky camera-facing sun HDR Render");
  evidence.camera_facing_sun_hdr = inspect_hdr(sun_hdr_output);

  RenderFrameOutput sun_sdr_output;
  RequireSuccess(frontend.Render(
                     sky_frame(3U, sun_scene, PixelFormat::RGBA8_SRGB),
                     sun_sdr_output),
                 "analytic-sky camera-facing sun SDR Render");
  evidence.camera_facing_sun_sdr = inspect_sdr(sun_sdr_output);

  const std::vector<std::uint8_t> &sunless =
      evidence.camera_facing_sunless_hdr.attachment_bytes;
  const std::vector<std::uint8_t> &sun =
      evidence.camera_facing_sun_hdr.attachment_bytes;
  const std::size_t pixel_count =
      static_cast<std::size_t>(kSkyWidth) * kSkyHeight;
  Require(sunless.size() == pixel_count * 8U && sun.size() == sunless.size(),
          "analytic-sky HDR evidence byte count is incomplete");

  std::vector<double> row_luminance(kSkyHeight, 0.0);
  for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
    float sunless_rgb[3U]{};
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      std::uint16_t bits = 0U;
      std::memcpy(&bits,
                  sunless.data() + pixel * 8U + channel * 2U,
                  sizeof(bits));
      sunless_rgb[channel] = HalfToFloat(bits);
    }
    const float maximum =
        std::max({sunless_rgb[0U], sunless_rgb[1U], sunless_rgb[2U]});
    if (std::isfinite(maximum) && maximum > 0.0001F) {
      ++evidence.hemisphere_covered_pixels;
    }
    row_luminance[pixel / kSkyWidth] +=
        0.2126 * static_cast<double>(sunless_rgb[0U]) +
        0.7152 * static_cast<double>(sunless_rgb[1U]) +
        0.0722 * static_cast<double>(sunless_rgb[2U]);

    bool rgb_changed = false;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      rgb_changed =
          rgb_changed ||
          std::memcmp(sunless.data() + pixel * 8U + channel * 2U,
                      sun.data() + pixel * 8U + channel * 2U,
                      sizeof(std::uint16_t)) != 0;
    }
    std::uint16_t sun_alpha = 0U;
    std::memcpy(&sun_alpha, sun.data() + pixel * 8U + 6U,
                sizeof(sun_alpha));
    if (sun_alpha == UINT16_C(0x3c00)) {
      ++evidence.sun_hdr_opaque_alpha_pixels;
    }
    if (rgb_changed) {
      ++evidence.sun_changed_pixels;
      if (sun_alpha == UINT16_C(0x3c00)) {
        ++evidence.sun_changed_pixels_alpha_exact_one;
      }
    }
  }
  for (double &row : row_luminance) {
    row /= static_cast<double>(kSkyWidth);
  }
  for (std::size_t row = 1U; row < row_luminance.size(); ++row) {
    if (std::abs(row_luminance[row] - row_luminance[row - 1U]) >
        1.0e-6) {
      ++evidence.hemisphere_gradient_rows;
    }
  }

  evidence.broad_hemisphere_coverage =
      evidence.hemisphere_covered_pixels >= pixel_count * 95U / 100U &&
      evidence.hemisphere_gradient_rows >= kSkyHeight / 4U &&
      evidence.camera_facing_sunless_hdr.distinct_rgb >= 4U;
  evidence.visible_sun_effect =
      evidence.sun_changed_pixels > 0U &&
      evidence.camera_facing_sun_hdr.maximum_luminance >
          evidence.camera_facing_sunless_hdr.maximum_luminance;
  evidence.visible_sun_alpha_exact_one =
      evidence.sun_changed_pixels_alpha_exact_one ==
          evidence.sun_changed_pixels &&
      evidence.sun_hdr_opaque_alpha_pixels == pixel_count;
  if (!evidence.broad_hemisphere_coverage) {
    std::ostringstream detail;
    detail << "analytic-sky sky-only readback did not cover the broad viewport with a vertical gradient"
           << " (covered=" << evidence.hemisphere_covered_pixels
           << ", gradient_rows=" << evidence.hemisphere_gradient_rows
           << ", distinct="
           << evidence.camera_facing_sunless_hdr.distinct_rgb << ')';
    Fail(detail.str());
  }
  Require(evidence.visible_sun_effect,
          "analytic-sky camera-facing sun changed no HDR pixels");
  Require(evidence.visible_sun_alpha_exact_one,
          "analytic-sky visible sun pixels did not retain exact half-float alpha one");

  const OgreNextAnalyticSkyRuntimeAudit audit =
      frontend.QueryAnalyticSkyAudit();
  Require(audit.completed_frames == 3U &&
              audit.native_mesh_creates == 6U &&
              audit.native_mesh_destroys == 6U &&
              audit.native_vertex_buffer_creates == 6U &&
              audit.native_vertex_buffer_destroys == 6U &&
              audit.native_index_buffer_creates == 6U &&
              audit.native_index_buffer_destroys == 6U &&
              audit.native_vao_creates == 6U &&
              audit.native_vao_destroys == 6U &&
              audit.native_item_creates == 6U &&
              audit.native_item_destroys == 6U &&
              audit.native_scene_node_creates == 3U &&
              audit.native_scene_node_destroys == 3U &&
              audit.native_datablock_creates == 6U &&
              audit.native_datablock_destroys == 6U &&
              audit.native_mesh_absence_checks == 6U &&
              audit.native_item_absence_checks == 6U &&
              audit.native_scene_node_absence_checks == 3U &&
              audit.native_datablock_absence_checks == 6U &&
              audit.native_gpu_content_readbacks == 12U &&
              audit.native_state_verifications == 3U &&
              audit.last_cpu_geometry_fnv1a64 != 0U &&
              audit.separate_sun_alpha_replace &&
              audit.native_geometry_metadata_verified &&
              audit.exact_native_geometry_readback,
          "analytic-sky sky-only proof did not balance exact native v2 resources");
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "analytic-sky visual-proof Shutdown");
}

SmokeResult::HdrCompositorEvidence
RunHdrCompositorProof(const std::string &media_root) {
  SmokeResult::HdrCompositorEvidence evidence;
  OgreNextN1Configuration configuration;
  configuration.shader_media_root = media_root;
  configuration.raster_feature_tier =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  configuration.enable_hdr_compositor = true;
  configuration.retain_native_lighting_content_evidence = true;
  OgreNextN1Frontend frontend(std::move(configuration));
  InitializeAndSync(frontend,
                    MakeCatalog(true, &kVariantSpecs.front()));

  evidence.initialized = frontend.QueryHdrCompositorAudit();
  HdrR16Float expected_initial;
  Require(QuantizeHdrR16Float(0.01F, expected_initial).ok(),
          "HDR compositor initial R16 fixture is invalid");
  Require(evidence.initialized.version == 2U &&
              evidence.initialized.enabled &&
              evidence.initialized.native_workspace_live &&
              evidence.initialized.deterministic_delta_bound &&
              evidence.initialized.native_r16_history_validated &&
              !evidence.initialized.exact_current_to_old_copy_verified &&
              evidence.initialized.ui_free_workspace_verified &&
              evidence.initialized.history_validation_mode ==
                  OgreNextHdrHistoryValidationMode::
                      NATIVE_AUTHORITATIVE_CONDITIONING_PLUS_ONE_R16_ULP &&
              evidence.initialized.width == kWidth &&
              evidence.initialized.height == kHeight &&
              evidence.initialized.warmup_frames == 2U &&
              evidence.initialized.committed_frames == 0U &&
              evidence.initialized.previous_inverse_luminance_r16_bits ==
                  expected_initial.bits &&
              evidence.initialized.reference_inverse_luminance_r16_bits ==
                  expected_initial.bits,
          "HDR compositor initialization audit is incomplete");

  RenderFrameRequest first = MakeFrame(
      1U, MakeScene(100U, false, true, 1U, 1U, Matrix4x4{}, 1U,
                   {0.0F, -0.8F, -0.6F}, 0.0F, true, true, false, false),
      PixelFormat::RGBA8_SRGB);
  RenderFrameOutput first_output;
  RequireSuccess(frontend.Render(first, first_output),
                 "HDR compositor first Render");
  evidence.first = InspectSdr(first_output);

  RenderFrameRequest second = MakeFrame(
      2U, MakeScene(101U, false, true, 1U, 1U, Matrix4x4{}, 1U,
                   {0.0F, -0.8F, -0.6F}, 0.5F, true, true, false, false),
      PixelFormat::RGBA8_SRGB);
  second.views.front().exposure = 1.25F;
  RenderFrameOutput second_output;
  RequireSuccess(frontend.Render(second, second_output),
                 "HDR compositor second Render");
  evidence.final = InspectSdr(second_output);

  evidence.exposure_changed_pixels = CountChangedPixels(
      evidence.first.attachment_bytes, evidence.final.attachment_bytes, 4U);
  evidence.committed = frontend.QueryHdrCompositorAudit();
  evidence.split_content =
      frontend.CaptureHdrLightingSplitContentEvidence();
  evidence.lighting = frontend.QueryNativeLightingPassAudit();
  const std::size_t expected_split_channels =
      static_cast<std::size_t>(kWidth) * kHeight * 4U;
  Require(evidence.split_content.version ==
                  kOgreNextHdrLightingSplitContentEvidenceVersion &&
              evidence.split_content.frame_id == 2U &&
              evidence.split_content.width == kWidth &&
              evidence.split_content.height == kHeight &&
              evidence.split_content.base_hdr_rgba16.size() ==
                  expected_split_channels &&
              evidence.split_content.sun_full_hdr_rgba16.size() ==
                  expected_split_channels &&
              evidence.split_content.sun_direct_hdr_rgba16.size() ==
                  expected_split_channels &&
              evidence.split_content.raster_lit_hdr_rgba16.size() ==
                  expected_split_channels,
          "HDR split content evidence layout changed");
  evidence.split_content_fnv1a64 = {
      HashHalfWords(evidence.split_content.base_hdr_rgba16),
      HashHalfWords(evidence.split_content.sun_full_hdr_rgba16),
      HashHalfWords(evidence.split_content.sun_direct_hdr_rgba16),
      HashHalfWords(evidence.split_content.raster_lit_hdr_rgba16)};
  std::size_t canonical_alpha_pixels = 0U;
  std::array<std::uint64_t, 4U> alpha_histogram{};
  std::array<std::uint16_t, 4U> first_alpha{};
  first_alpha = {evidence.split_content.base_hdr_rgba16[3U],
                 evidence.split_content.sun_full_hdr_rgba16[3U],
                 evidence.split_content.sun_direct_hdr_rgba16[3U],
                 evidence.split_content.raster_lit_hdr_rgba16[3U]};
  for (std::size_t offset = 0U; offset < expected_split_channels;
       offset += 4U) {
    bool positive_direct_pixel = false;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
      const float base = HalfToFloat(
          evidence.split_content.base_hdr_rgba16[offset + channel]);
      const float sun_full = HalfToFloat(
          evidence.split_content.sun_full_hdr_rgba16[offset + channel]);
      const float expected = std::max(sun_full - base, 0.0F);
      HdrR16Float expected_half;
      Require(QuantizeHdrR16Float(expected, expected_half).ok(),
              "HDR split CPU oracle exceeded finite binary16");
      Require(evidence.split_content.sun_direct_hdr_rgba16[offset + channel] ==
                  expected_half.bits,
              "GPU SunDirect texel differs from max(SunFull-Base,0)");
      ++evidence.split_rgb_channels_verified;
      positive_direct_pixel = positive_direct_pixel || expected_half.bits != 0U;
    }
    evidence.positive_sun_direct_pixels += positive_direct_pixel ? 1U : 0U;
    canonical_alpha_pixels +=
        evidence.split_content.base_hdr_rgba16[offset + 3U] == 0x3c00U &&
                evidence.split_content.sun_full_hdr_rgba16[offset + 3U] ==
                    0x3c00U &&
                evidence.split_content.sun_direct_hdr_rgba16[offset + 3U] ==
                    0U &&
                evidence.split_content.raster_lit_hdr_rgba16[offset + 3U] ==
                    0x3c00U
            ? 1U
            : 0U;
    alpha_histogram[0U] +=
        evidence.split_content.base_hdr_rgba16[offset + 3U] == 0x3c00U;
    alpha_histogram[1U] +=
        evidence.split_content.sun_full_hdr_rgba16[offset + 3U] == 0x3c00U;
    alpha_histogram[2U] +=
        evidence.split_content.sun_direct_hdr_rgba16[offset + 3U] == 0U;
    alpha_histogram[3U] +=
        evidence.split_content.raster_lit_hdr_rgba16[offset + 3U] == 0x3c00U;
  }
  evidence.canonical_split_alpha =
      canonical_alpha_pixels ==
      static_cast<std::size_t>(kWidth) * kHeight;
  if (evidence.split_rgb_channels_verified !=
          static_cast<std::size_t>(kWidth) * kHeight * 3U ||
      evidence.positive_sun_direct_pixels < 128U ||
      !evidence.canonical_split_alpha ||
      evidence.split_content_fnv1a64[0U] ==
          evidence.split_content_fnv1a64[1U] ||
      evidence.split_content_fnv1a64[2U] == 0U) {
    std::ostringstream detail;
    detail << "HDR split content did not prove a directional-only GPU radiance term"
           << " (rgb=" << evidence.split_rgb_channels_verified
           << ", positive=" << evidence.positive_sun_direct_pixels
           << ", alpha=" << evidence.canonical_split_alpha
           << ", base=" << HexHash(evidence.split_content_fnv1a64[0U])
           << ", full=" << HexHash(evidence.split_content_fnv1a64[1U])
           << ", direct=" << HexHash(evidence.split_content_fnv1a64[2U])
           << ", alpha_count=" << alpha_histogram[0U] << '/'
           << alpha_histogram[1U] << '/' << alpha_histogram[2U] << '/'
           << alpha_histogram[3U] << ", first_alpha=" << first_alpha[0U]
           << '/' << first_alpha[1U] << '/' << first_alpha[2U] << '/'
           << first_alpha[3U]
           << ')';
    throw std::runtime_error(detail.str());
  }
  Require(evidence.exposure_changed_pixels >= 512U &&
              evidence.committed.version == 2U &&
              evidence.committed.native_workspace_live &&
              evidence.committed.deterministic_delta_bound &&
              evidence.committed.native_r16_history_validated &&
              evidence.committed.exact_current_to_old_copy_verified &&
              evidence.committed.ui_free_workspace_verified &&
              evidence.committed.history_validation_mode ==
                  OgreNextHdrHistoryValidationMode::
                      NATIVE_AUTHORITATIVE_CONDITIONING_PLUS_ONE_R16_ULP &&
              evidence.committed.warmup_frames == 2U &&
              evidence.committed.committed_frames == 2U &&
              evidence.committed.previous_inverse_luminance_r16_bits != 0U &&
              evidence.committed.previous_inverse_luminance_r16_bits !=
                  evidence.initialized.previous_inverse_luminance_r16_bits &&
              evidence.committed.reference_inverse_luminance_r16_bits != 0U &&
              std::isfinite(evidence.committed.history_absolute_error) &&
              std::isfinite(evidence.committed.history_allowed_error) &&
              evidence.committed.history_absolute_error >= 0.0 &&
              evidence.committed.history_allowed_error >=
                  evidence.committed.history_absolute_error &&
              evidence.committed.history_storage_ulp > 0.0,
          "HDR compositor did not prove persistent deterministic adaptation");
  Require(evidence.lighting.version ==
                  kOgreNextNativeLightingPassAuditVersion &&
              evidence.lighting.completed_frames == 2U &&
              evidence.lighting.last_frame_id == 2U &&
              evidence.lighting.last_snapshot_id == 101U &&
              evidence.lighting.native_scene_lighting_pass &&
              evidence.lighting.linear_rgba16_hdr_target &&
              evidence.lighting.separate_base_hdr_target &&
              evidence.lighting.separate_unoccluded_sun_full_hdr_target &&
              evidence.lighting.separate_sun_direct_hdr_target &&
              evidence.lighting.gpu_sun_direct_derivation &&
              evidence.lighting.transactional_directional_sun_toggle &&
              evidence.lighting.raster_lit_hdr_target &&
              evidence.lighting.single_step_hdr_history &&
              evidence.lighting.raster_scene_evaluations == 3U &&
              evidence.lighting.test_artifact_content_readbacks >= 4U &&
              evidence.lighting.calibrated_directional_lighting &&
              evidence.lighting.ambient_environment_lighting &&
              !evidence.lighting.pssm_shadow_response &&
              evidence.lighting.shadow_mode ==
                  OgreNextDirectionalShadowMode::DISABLED &&
              evidence.lighting.hdr_auto_exposure &&
              evidence.lighting.hdr_bloom &&
              evidence.lighting.filmic_tone_map &&
              evidence.lighting.srgb_presentation &&
              evidence.lighting.ogre14_lighting_passes == 0U &&
              evidence.lighting.no_ogre14_lighting,
          "HDR compositor did not publish the exact split-lighting receipt");

  const auto same_hdr_state = [](const OgreNextHdrCompositorAudit &lhs,
                                 const OgreNextHdrCompositorAudit &rhs) {
    return lhs.version == rhs.version && lhs.enabled == rhs.enabled &&
           lhs.native_workspace_live == rhs.native_workspace_live &&
           lhs.width == rhs.width && lhs.height == rhs.height &&
           lhs.warmup_frames == rhs.warmup_frames &&
           lhs.committed_frames == rhs.committed_frames &&
           lhs.previous_inverse_luminance_r16_bits ==
               rhs.previous_inverse_luminance_r16_bits &&
           lhs.reference_inverse_luminance_r16_bits ==
               rhs.reference_inverse_luminance_r16_bits &&
           lhs.history_validation_mode == rhs.history_validation_mode;
  };
  FrontendSurfaceUpdate suspended_surface;
  suspended_surface.surface_revision = 2U;
  suspended_surface.pixel_width = 0U;
  suspended_surface.pixel_height = 0U;
  suspended_surface.suspended = true;
  RequireSuccess(frontend.UpdateSurface(
                     suspended_surface, true,
                     kInfiniteRenderTimeoutNanoseconds),
                 "HDR compositor suspend UpdateSurface");
  const OgreNextHdrCompositorAudit suspended_audit =
      frontend.QueryHdrCompositorAudit();

  FrontendSurfaceUpdate restored_surface;
  restored_surface.surface_revision = 3U;
  restored_surface.pixel_width = kWidth;
  restored_surface.pixel_height = kHeight;
  restored_surface.suspended = false;
  RequireSuccess(frontend.UpdateSurface(
                     restored_surface, true,
                     kInfiniteRenderTimeoutNanoseconds),
                 "HDR compositor restore UpdateSurface");
  const OgreNextHdrCompositorAudit restored_audit =
      frontend.QueryHdrCompositorAudit();
  evidence.suspend_restore_preserved_graph =
      same_hdr_state(evidence.committed, suspended_audit) &&
      same_hdr_state(evidence.committed, restored_audit);
  Require(evidence.suspend_restore_preserved_graph,
          "HDR suspend/restore changed the live graph or temporal state");

  const FrontendCapabilityReport resize_capabilities =
      frontend.QueryCapabilities();
  Require(resize_capabilities.maximum_texture_dimension_2d < 65535U,
          "HDR resize rollback fixture cannot exceed the native limit safely");
  FrontendSurfaceUpdate invalid_resize = restored_surface;
  invalid_resize.surface_revision = 4U;
  invalid_resize.pixel_width =
      resize_capabilities.maximum_texture_dimension_2d + 1U;
  const RenderOperationResult rejected_resize = frontend.UpdateSurface(
      invalid_resize, true, kInfiniteRenderTimeoutNanoseconds);
  evidence.invalid_resize_rollback_verified =
      !rejected_resize &&
      rejected_resize.code == RenderOperationCode::UNSUPPORTED &&
      same_hdr_state(restored_audit, frontend.QueryHdrCompositorAudit());
  Require(evidence.invalid_resize_rollback_verified,
          "HDR invalid resize did not fail before graph mutation");

  constexpr std::uint32_t kResizedWidth = 160U;
  constexpr std::uint32_t kResizedHeight = 112U;
  FrontendSurfaceUpdate resized_surface = restored_surface;
  resized_surface.surface_revision = 5U;
  resized_surface.pixel_width = kResizedWidth;
  resized_surface.pixel_height = kResizedHeight;
  RequireSuccess(frontend.UpdateSurface(
                     resized_surface, true,
                     kInfiniteRenderTimeoutNanoseconds),
                 "HDR compositor resize UpdateSurface");
  const OgreNextHdrCompositorAudit resized_audit =
      frontend.QueryHdrCompositorAudit();
  evidence.resize_rebuild_verified =
      resized_audit.version == 2U && resized_audit.enabled &&
      resized_audit.native_workspace_live &&
      resized_audit.ui_free_workspace_verified &&
      resized_audit.width == kResizedWidth &&
      resized_audit.height == kResizedHeight &&
      resized_audit.warmup_frames == 2U &&
      resized_audit.committed_frames == 2U &&
      resized_audit.previous_inverse_luminance_r16_bits ==
          evidence.committed.previous_inverse_luminance_r16_bits;
  if (!evidence.resize_rebuild_verified) {
    std::ostringstream detail;
    detail << "HDR resize did not rebuild an exact clean persistent graph"
           << " (version=" << resized_audit.version
           << ", enabled=" << resized_audit.enabled
           << ", live=" << resized_audit.native_workspace_live
           << ", ui_free=" << resized_audit.ui_free_workspace_verified
           << ", extent=" << resized_audit.width << 'x'
           << resized_audit.height
           << ", warmup=" << resized_audit.warmup_frames
           << ", frames=" << resized_audit.committed_frames
           << ", history="
           << resized_audit.previous_inverse_luminance_r16_bits
           << ", expected_history="
           << evidence.initialized.previous_inverse_luminance_r16_bits
           << ')';
    Fail(detail.str());
  }

  RenderFrameRequest resized_frame = MakeFrame(
      3U, MakeScene(102U, false, true, 1U, 1U, Matrix4x4{}, 1U,
                    {0.0F, -0.8F, -0.6F}, 0.5F, true, true),
      PixelFormat::RGBA8_SRGB);
  resized_frame.views.front().width = kResizedWidth;
  resized_frame.views.front().height = kResizedHeight;
  RenderFrameOutput resized_output;
  RequireSuccess(frontend.Render(resized_frame, resized_output),
                 "HDR compositor resized Render");
  const OgreNextHdrCompositorAudit resized_committed =
      frontend.QueryHdrCompositorAudit();
  const OgreNextNativeLightingPassAudit resized_lighting =
      frontend.QueryNativeLightingPassAudit();
  const bool exact_resized_attachment =
      resized_output.status == RenderFrameStatus::RENDERED &&
      resized_output.attachments.size() == 1U &&
      resized_output.attachments.front().format == PixelFormat::RGBA8_SRGB &&
      resized_output.attachments.front().width == kResizedWidth &&
      resized_output.attachments.front().height == kResizedHeight &&
      resized_output.attachments.front().row_pitch_bytes ==
          static_cast<std::uint64_t>(kResizedWidth) * 4U &&
      resized_output.attachments.front().bytes.size() ==
          static_cast<std::size_t>(kResizedWidth) * kResizedHeight * 4U;
  evidence.resized_frame_verified =
      exact_resized_attachment && resized_committed.committed_frames == 3U &&
      resized_lighting.completed_frames == 3U &&
      resized_lighting.last_frame_id == 3U &&
      resized_lighting.separate_base_hdr_target &&
      resized_lighting.separate_sun_direct_hdr_target &&
      resized_lighting.gpu_sun_direct_derivation &&
      resized_lighting.production_content_readbacks == 0U &&
      resized_lighting.production_framebuffer_readbacks == 0U;
  if (!evidence.resized_frame_verified) {
    std::ostringstream detail;
    detail << "HDR resized frame did not execute the exact split-lighting graph"
           << " (attachment=" << exact_resized_attachment
           << ", hdr_frames=" << resized_committed.committed_frames
           << ", lighting_frames=" << resized_lighting.completed_frames
           << ", frame_id=" << resized_lighting.last_frame_id
           << ", base=" << resized_lighting.separate_base_hdr_target
           << ", direct=" << resized_lighting.separate_sun_direct_hdr_target
           << ", derivation=" << resized_lighting.gpu_sun_direct_derivation
           << ", production_content_reads="
           << resized_lighting.production_content_readbacks
           << ", production_framebuffer_reads="
           << resized_lighting.production_framebuffer_readbacks << ')';
    Fail(detail.str());
  }

  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "HDR compositor Shutdown");
  const OgreNextHdrCompositorAudit shutdown =
      frontend.QueryHdrCompositorAudit();
  evidence.clean_shutdown =
      shutdown.enabled && !shutdown.native_workspace_live &&
              shutdown.width == 0U && shutdown.height == 0U &&
              shutdown.warmup_frames == 0U &&
              shutdown.committed_frames == 0U;
  Require(evidence.clean_shutdown,
          "HDR compositor shutdown did not retire its persistent graph");

#if defined(ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM)
  OgreNextN1Configuration transaction_configuration;
  transaction_configuration.shader_media_root = media_root;
  transaction_configuration.raster_feature_tier =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  transaction_configuration.enable_hdr_compositor = true;
  transaction_configuration.retain_native_lighting_content_evidence = true;
  transaction_configuration.hdr_failure_stage =
      OgreNextN1HdrFailureStage::AFTER_FRAME_COMMIT_PREPARE;
  OgreNextN1Frontend transaction(std::move(transaction_configuration));
  InitializeAndSync(transaction, MakeCatalog(true, &kVariantSpecs.front()));
  const OgreNextHdrCompositorAudit hdr_before_abort =
      transaction.QueryHdrCompositorAudit();
  const OgreNextReflectionProbeAudit reflection_before_abort =
      transaction.QueryReflectionProbeAudit();
  const OgreNextReflectionProbeNativeOwnershipEvidence
      reflection_ownership_before_abort =
          transaction.QueryReflectionProbeNativeOwnershipEvidence();
  RenderFrameOutput preserved_output;
  preserved_output.frame_id = 777U;
  preserved_output.snapshot_id = 778U;
  preserved_output.status = RenderFrameStatus::SKIPPED;
  preserved_output.presented = true;
  preserved_output.presented_view_id = 779U;
  preserved_output.cpu_submit_milliseconds = 1.25;
  preserved_output.gpu_frame_milliseconds = 2.5;
  FrameAttachment preserved_attachment;
  preserved_attachment.view_id = 780U;
  preserved_attachment.output = FrameOutputMask::COLOR;
  preserved_attachment.format = PixelFormat::RGBA8_SRGB;
  preserved_attachment.width = 1U;
  preserved_attachment.height = 1U;
  preserved_attachment.row_pitch_bytes = 4U;
  preserved_attachment.bytes = {0xA5U, 0x5AU, 0xC3U, 0x3CU};
  preserved_output.attachments.push_back(preserved_attachment);
  const RenderOperationResult frame_commit_prepare_failure =
      transaction.Render(
          MakeFrame(1U, MakeScene(400U, false, true),
                    PixelFormat::RGBA8_SRGB),
          preserved_output);
  const OgreNextHdrCompositorAudit hdr_after_abort =
      transaction.QueryHdrCompositorAudit();
  const OgreNextReflectionProbeAudit reflection_after_abort =
      transaction.QueryReflectionProbeAudit();
  const OgreNextReflectionProbeNativeOwnershipEvidence
      reflection_ownership_after_abort =
          transaction.QueryReflectionProbeNativeOwnershipEvidence();
  evidence.aborted_hdr_audit_unchanged =
      hdr_after_abort.version == hdr_before_abort.version &&
      hdr_after_abort.enabled == hdr_before_abort.enabled &&
      hdr_after_abort.native_workspace_live ==
          hdr_before_abort.native_workspace_live &&
      hdr_after_abort.deterministic_delta_bound ==
          hdr_before_abort.deterministic_delta_bound &&
      hdr_after_abort.native_r16_history_validated ==
          hdr_before_abort.native_r16_history_validated &&
      hdr_after_abort.exact_current_to_old_copy_verified ==
          hdr_before_abort.exact_current_to_old_copy_verified &&
      hdr_after_abort.ui_free_workspace_verified ==
          hdr_before_abort.ui_free_workspace_verified &&
      hdr_after_abort.width == hdr_before_abort.width &&
      hdr_after_abort.height == hdr_before_abort.height &&
      hdr_after_abort.warmup_frames == hdr_before_abort.warmup_frames &&
      hdr_after_abort.committed_frames == hdr_before_abort.committed_frames &&
      hdr_after_abort.previous_inverse_luminance_r16_bits ==
          hdr_before_abort.previous_inverse_luminance_r16_bits &&
      hdr_after_abort.history_validation_mode ==
          hdr_before_abort.history_validation_mode &&
      hdr_after_abort.reference_inverse_luminance_r16_bits ==
          hdr_before_abort.reference_inverse_luminance_r16_bits &&
      hdr_after_abort.history_ogre_exposure ==
          hdr_before_abort.history_ogre_exposure &&
      hdr_after_abort.history_minimum_auto_exposure ==
          hdr_before_abort.history_minimum_auto_exposure &&
      hdr_after_abort.history_maximum_auto_exposure ==
          hdr_before_abort.history_maximum_auto_exposure &&
      hdr_after_abort.history_average_log_luminance ==
          hdr_before_abort.history_average_log_luminance &&
      hdr_after_abort.history_previous_inverse_luminance_r16_bits ==
          hdr_before_abort.history_previous_inverse_luminance_r16_bits &&
      hdr_after_abort.history_delta_seconds ==
          hdr_before_abort.history_delta_seconds &&
      hdr_after_abort.history_absolute_error ==
          hdr_before_abort.history_absolute_error &&
      hdr_after_abort.history_allowed_error ==
          hdr_before_abort.history_allowed_error &&
      hdr_after_abort.history_conditioning_bound ==
          hdr_before_abort.history_conditioning_bound &&
      hdr_after_abort.history_binary32_rounding_bound ==
          hdr_before_abort.history_binary32_rounding_bound &&
      hdr_after_abort.history_storage_ulp ==
          hdr_before_abort.history_storage_ulp &&
      hdr_after_abort.history_r16_ulp_distance ==
          hdr_before_abort.history_r16_ulp_distance;
  evidence.aborted_reflection_audit_unchanged =
      reflection_after_abort.version == reflection_before_abort.version &&
      reflection_after_abort.committed_state_digest ==
          reflection_before_abort.committed_state_digest &&
      reflection_after_abort.successful_capture_count ==
          reflection_before_abort.successful_capture_count &&
      reflection_after_abort.failed_capture_count ==
          reflection_before_abort.failed_capture_count &&
      reflection_after_abort.native_execution_evidence ==
          reflection_before_abort.native_execution_evidence &&
      reflection_after_abort.last_capture_frame_id ==
          reflection_before_abort.last_capture_frame_id &&
      reflection_after_abort.last_capture_simulation_tick ==
          reflection_before_abort.last_capture_simulation_tick &&
      reflection_after_abort.last_probe_id ==
          reflection_before_abort.last_probe_id &&
      reflection_after_abort.last_content_revision ==
          reflection_before_abort.last_content_revision &&
      reflection_after_abort.last_candidate_generation ==
          reflection_before_abort.last_candidate_generation &&
      reflection_after_abort.last_deterministic_seed ==
          reflection_before_abort.last_deterministic_seed &&
      reflection_after_abort.last_capture_digest ==
          reflection_before_abort.last_capture_digest &&
      reflection_after_abort.last_canonical_payload_bytes ==
          reflection_before_abort.last_canonical_payload_bytes &&
      reflection_after_abort.filtered_finite_component_count ==
          reflection_before_abort.filtered_finite_component_count &&
      reflection_after_abort.filtered_nonzero_rgb_component_count ==
          reflection_before_abort.filtered_nonzero_rgb_component_count &&
      reflection_after_abort.filtered_max_absolute_rgb ==
          reflection_before_abort.filtered_max_absolute_rgb &&
      reflection_after_abort.live_probe_count ==
          reflection_before_abort.live_probe_count &&
      reflection_after_abort.completed_face_count ==
          reflection_before_abort.completed_face_count &&
      reflection_after_abort.blend_resolution ==
          reflection_before_abort.blend_resolution &&
      reflection_after_abort.completed_mip_count ==
          reflection_before_abort.completed_mip_count &&
      reflection_after_abort.initialized == reflection_before_abort.initialized &&
      reflection_after_abort.compositor_defined_in_code ==
          reflection_before_abort.compositor_defined_in_code &&
      reflection_after_abort.exact_resources_loaded ==
          reflection_before_abort.exact_resources_loaded &&
      reflection_after_abort.pcc_enabled ==
          reflection_before_abort.pcc_enabled &&
      reflection_after_abort.pbs_bound == reflection_before_abort.pbs_bound &&
      reflection_after_abort.blend_texture_ready ==
          reflection_before_abort.blend_texture_ready &&
      reflection_after_abort.ui_free_capture ==
          reflection_before_abort.ui_free_capture &&
      reflection_after_abort.reserved_render_queue_excluded ==
          reflection_before_abort.reserved_render_queue_excluded;
  const bool reflection_native_ownership_released =
      reflection_ownership_before_abort.version == 1U &&
      reflection_ownership_before_abort.pbs_query_succeeded &&
      reflection_ownership_before_abort.pcc_create_count == 0U &&
      reflection_ownership_before_abort.pcc_destroy_count == 0U &&
      reflection_ownership_before_abort.live_pcc_count == 0U &&
      reflection_ownership_before_abort.pbs_unbound &&
      !reflection_ownership_before_abort.pbs_bound_to_runtime &&
      reflection_ownership_after_abort.version == 1U &&
      reflection_ownership_after_abort.pbs_query_succeeded &&
      reflection_ownership_after_abort.pcc_create_count == 1U &&
      reflection_ownership_after_abort.pcc_destroy_count == 1U &&
      reflection_ownership_after_abort.live_pcc_count == 0U &&
      reflection_ownership_after_abort.pbs_unbound &&
      !reflection_ownership_after_abort.pbs_bound_to_runtime;
  evidence.aborted_submission_uncommitted =
      !transaction.IsFrameComplete(1U);
  evidence.aborted_output_unchanged =
      preserved_output.version == kRenderFrameContractVersion &&
      preserved_output.frame_id == 777U &&
      preserved_output.snapshot_id == 778U &&
      preserved_output.status == RenderFrameStatus::SKIPPED &&
      preserved_output.presented &&
      preserved_output.presented_view_id == 779U &&
      preserved_output.cpu_submit_milliseconds == 1.25 &&
      preserved_output.gpu_frame_milliseconds == 2.5 &&
      preserved_output.attachments.size() == 1U &&
      preserved_output.attachments.front().view_id == 780U &&
      preserved_output.attachments.front().output == FrameOutputMask::COLOR &&
      preserved_output.attachments.front().format == PixelFormat::RGBA8_SRGB &&
      preserved_output.attachments.front().width == 1U &&
      preserved_output.attachments.front().height == 1U &&
      preserved_output.attachments.front().row_pitch_bytes == 4U &&
      !preserved_output.attachments.front().gpu_resource.valid() &&
      preserved_output.attachments.front().bytes ==
          preserved_attachment.bytes;
  RenderFrameOutput fault_latch_output;
  fault_latch_output.frame_id = 781U;
  const RenderOperationResult fault_latched = transaction.Render(
      MakeFrame(1U, MakeScene(401U, false, true), PixelFormat::RGBA8_SRGB),
      fault_latch_output);
  evidence.post_render_failure_fault_latched =
      !fault_latched &&
      fault_latched.code == RenderOperationCode::BACKEND_FAILURE &&
      fault_latched.detail.find("fault-latched") != std::string::npos &&
      fault_latch_output.frame_id == 781U;
  evidence.frame_commit_prepare_failure_verified =
      !frame_commit_prepare_failure &&
      frame_commit_prepare_failure.code ==
          RenderOperationCode::BACKEND_FAILURE &&
      frame_commit_prepare_failure.detail.find("commit-prepare") !=
          std::string::npos &&
      evidence.aborted_hdr_audit_unchanged &&
      evidence.aborted_reflection_audit_unchanged &&
      reflection_native_ownership_released &&
      evidence.aborted_submission_uncommitted &&
      evidence.aborted_output_unchanged &&
      evidence.post_render_failure_fault_latched;
  Require(evidence.frame_commit_prepare_failure_verified,
          "HDR frame commit-prepare failure published partial state: hdr=" +
              std::string(evidence.aborted_hdr_audit_unchanged ? "true"
                                                               : "false") +
              ", reflection=" +
              (evidence.aborted_reflection_audit_unchanged ? "true"
                                                           : "false") +
              ", reflection_ownership=" +
              (reflection_native_ownership_released ? "true" : "false") +
              ", submission=" +
              (evidence.aborted_submission_uncommitted ? "true" : "false") +
              ", output=" +
              (evidence.aborted_output_unchanged ? "true" : "false") +
              ", fault_latch=" +
              (evidence.post_render_failure_fault_latched ? "true"
                                                          : "false"));
  RequireSuccess(transaction.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "HDR frame commit-prepare failure Shutdown");

  const std::array<std::pair<OgreNextN1HdrFailureStage, const char *>, 10U>
      failure_stages{{
          {OgreNextN1HdrFailureStage::AFTER_RESOURCE_GROUP_CREATE,
           "after_resource_group_create"},
          {OgreNextN1HdrFailureStage::AFTER_RESOURCE_LOCATIONS,
           "after_resource_locations"},
          {OgreNextN1HdrFailureStage::AFTER_RESOURCE_GROUP_INITIALIZE,
           "after_resource_group_initialize"},
          {OgreNextN1HdrFailureStage::AFTER_WORKSPACE_DEFINITION,
           "after_workspace_definition"},
          {OgreNextN1HdrFailureStage::AFTER_OUTPUT_CREATE,
           "after_output_create"},
          {OgreNextN1HdrFailureStage::AFTER_OUTPUT_CONFIGURE,
           "after_output_configure"},
          {OgreNextN1HdrFailureStage::AFTER_WORKSPACE_CREATE,
           "after_workspace_create"},
          {OgreNextN1HdrFailureStage::AFTER_PARAMETER_BINDING,
           "after_parameter_binding"},
          {OgreNextN1HdrFailureStage::AFTER_WARMUP_FRAME_ONE,
           "after_warmup_frame_one"},
          {OgreNextN1HdrFailureStage::AFTER_WARMUP_FRAME_TWO,
           "after_warmup_frame_two"},
      }};
  for (std::size_t index = 0U; index < failure_stages.size(); ++index) {
    OgreNextN1Configuration failure_configuration;
    failure_configuration.shader_media_root = media_root;
    failure_configuration.raster_feature_tier =
        OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
    failure_configuration.enable_hdr_compositor = true;
    failure_configuration.retain_native_lighting_content_evidence = true;
    failure_configuration.hdr_failure_stage = failure_stages[index].first;
    OgreNextN1Frontend rollback(std::move(failure_configuration));
    const RenderOperationResult injected = rollback.Initialize(Initialization());
    Require(!injected.ok(), std::string("HDR staged failure was not injected: ") +
                                failure_stages[index].second);
    const OgreNextHdrCompositorAudit after_failure =
        rollback.QueryHdrCompositorAudit();
    Require(after_failure.version == 2U && after_failure.enabled &&
                !after_failure.native_workspace_live && after_failure.width == 0U &&
                after_failure.height == 0U && after_failure.warmup_frames == 0U &&
                after_failure.committed_frames == 0U,
            std::string("HDR staged rollback leaked state: ") +
                failure_stages[index].second);

    InitializeAndSync(rollback, MakeCatalog(true, &kVariantSpecs.front()));
    RenderFrameOutput recovered;
    RequireSuccess(
        rollback.Render(MakeFrame(1U, MakeScene(300U + index, false, true),
                                  PixelFormat::RGBA8_SRGB),
                        recovered),
        std::string("HDR same-object recovery Render(") +
            failure_stages[index].second + ')');
    static_cast<void>(InspectSdr(recovered));
    const OgreNextHdrCompositorAudit recovered_audit =
        rollback.QueryHdrCompositorAudit();
    Require(recovered_audit.native_workspace_live &&
                recovered_audit.ui_free_workspace_verified &&
                recovered_audit.native_r16_history_validated &&
                recovered_audit.exact_current_to_old_copy_verified &&
                recovered_audit.committed_frames == 1U,
            std::string("HDR same-object recovery audit failed: ") +
                failure_stages[index].second);
    RequireSuccess(rollback.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                   std::string("HDR same-object recovery Shutdown(") +
                       failure_stages[index].second + ')');
    ++evidence.initialization_failure_stages_verified;
  }
  evidence.same_object_reinitialize_verified =
      evidence.initialization_failure_stages_verified == failure_stages.size();

  OgreNextN1Configuration overlay_configuration;
  overlay_configuration.shader_media_root = media_root;
  overlay_configuration.raster_feature_tier =
      OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
  overlay_configuration.enable_hdr_compositor = true;
  overlay_configuration.retain_native_lighting_content_evidence = true;
  overlay_configuration.hdr_ui_overlay_control = true;
  OgreNextN1Frontend overlay_control(std::move(overlay_configuration));
  InitializeAndSync(overlay_control, MakeCatalog(true, &kVariantSpecs.front()));
  RenderFrameOutput overlay_output;
  RequireSuccess(
      overlay_control.Render(
          MakeFrame(1U, MakeScene(100U, false, true),
                    PixelFormat::RGBA8_SRGB),
          overlay_output),
      "HDR real UI-overlay control Render");
  evidence.ui_overlay_control = ReadSdrAttachment(overlay_output);
  evidence.ui_overlay_control_changed_pixels = CountChangedPixels(
      evidence.first.attachment_bytes,
      evidence.ui_overlay_control.attachment_bytes,
      4U);
  for (std::size_t offset = 0U;
       offset + 3U < evidence.ui_overlay_control.attachment_bytes.size();
       offset += 4U) {
    if (evidence.ui_overlay_control.attachment_bytes[offset] >= 250U &&
        evidence.ui_overlay_control.attachment_bytes[offset + 1U] <= 5U &&
        evidence.ui_overlay_control.attachment_bytes[offset + 2U] >= 250U) {
      ++evidence.ui_overlay_control_magenta_pixels;
    }
  }
  Require(evidence.ui_overlay_control_changed_pixels >=
                  static_cast<std::size_t>(kWidth) * kHeight * 3U / 4U &&
              evidence.ui_overlay_control_magenta_pixels >=
                  static_cast<std::size_t>(kWidth) * kHeight * 3U / 4U &&
              evidence.ui_overlay_control.attachment_fnv1a64 !=
                  evidence.first.attachment_fnv1a64 &&
              !overlay_control.QueryHdrCompositorAudit()
                   .ui_free_workspace_verified,
          "real HdrRenderUi Overlay control did not visibly alter output");
  RequireSuccess(overlay_control.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "HDR real UI-overlay control Shutdown");
#endif
  return evidence;
}

SmokeResult RunSmoke(const std::string &media_root, bool modern_pbr) {
  const VariantSpec *baseline_spec = modern_pbr ? &kVariantSpecs.front()
                                                 : nullptr;
  const RenderAssetDelta catalog = MakeCatalog(modern_pbr, baseline_spec);
  const auto scene_one = MakeScene(1U, false, modern_pbr);
  const auto scene_two = MakeScene(2U, true, modern_pbr);
  const OgreNextRasterFeatureTier raster_feature_tier =
      modern_pbr ? OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1
                 : OgreNextRasterFeatureTier::STATIC_PBR_N1;
  OgreNextN1Frontend relative_media(
      OgreNextN1Configuration{"relative/shader/media", raster_feature_tier});
  const RenderOperationResult relative_media_result =
      relative_media.Initialize(Initialization());
  Require(relative_media_result.code == RenderOperationCode::INVALID_ARGUMENT,
          "relative shader media root did not fail closed (code " +
              std::to_string(static_cast<unsigned int>(
                  relative_media_result.code)) +
              "): " + relative_media_result.detail);
  OgreNextN1Frontend missing_media(
      OgreNextN1Configuration{media_root + "/missing", raster_feature_tier});
  const RenderOperationResult missing_media_result =
      missing_media.Initialize(Initialization());
  Require(missing_media_result.code == RenderOperationCode::INVALID_ARGUMENT,
          "missing shader media root did not fail closed (code " +
              std::to_string(static_cast<unsigned int>(
                  missing_media_result.code)) +
              "): " + missing_media_result.detail);

  if (modern_pbr) {
    OgreNextN1Configuration n4_with_pssm_configuration{
        media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
    n4_with_pssm_configuration.directional_shadow_mode =
        OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1;
    OgreNextN1Frontend n4_with_pssm(
        std::move(n4_with_pssm_configuration),
        OgreNextNativeFeatureTier::
            METAL_RAY_TRACING_N4_DIRECTIONAL_HARD_SHADOW);
    const RenderOperationResult n4_with_pssm_result =
        n4_with_pssm.Initialize(Initialization());
    Require(
        n4_with_pssm_result.code == RenderOperationCode::UNSUPPORTED &&
            n4_with_pssm_result.detail.find("mutually exclusive") !=
                std::string::npos,
        "N4 initialization did not reject simultaneous PSSM shadowing");

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

  OgreNextN1Configuration frontend_configuration{media_root,
                                                  raster_feature_tier};
  frontend_configuration.retain_reflection_capture_evidence = modern_pbr;
  frontend_configuration.retain_analytic_sky_geometry_content_evidence =
      modern_pbr;
  OgreNextN1Frontend frontend(std::move(frontend_configuration));

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
              capabilities.supports_dynamic_mesh_updates &&
              capabilities.supports_continuous_particles &&
              !capabilities.supports_particle_events &&
              !capabilities.supports_native_interop &&
              !capabilities.supports_native_ray_tracing_api,
          "N1 capability report enabled an unproved feature");
  Require(frontend.GetNativeInterop() == nullptr,
          "N1 unexpectedly exported native interop");

  SmokeResult result;
  result.dynamic_mesh = RunDynamicMeshProof(media_root, modern_pbr);
  if (modern_pbr) {
    RunAnalyticSkyProductionDefaultReadbackProof(media_root,
                                                 result.analytic_sky);
    RunAnalyticSkyVisualProof(media_root, result.analytic_sky);
    RunAnalyticSkyRollbackProof(media_root, result.analytic_sky);
    result.display_domain_unlit = RunDisplayDomainUnlitProof(media_root);
    result.tangent_handedness = RunTangentHandednessProof(media_root);
    result.texture_upload_rollback =
        RunTextureUploadRollbackProof(media_root);
    result.retirement = RunTextureRetirementProof(media_root);
    result.hdr_compositor = RunHdrCompositorProof(media_root);
  }
  InitializeAndSync(frontend, catalog);
  if (modern_pbr) {
    result.texture_allocations = frontend.QueryTextureAllocationAudit();
    Require(result.texture_allocations.version == 2U &&
                result.texture_allocations.live_source_textures == 4U &&
                result.texture_allocations.sampled_rgba_allocations == 2U &&
                result.texture_allocations.linear_rgba_allocations == 0U &&
                result.texture_allocations.roughness_r8_allocations == 1U &&
                result.texture_allocations.metallic_r8_allocations == 1U &&
                result.texture_allocations.normal_rg8_allocations == 1U &&
                result.texture_allocations.native_allocation_creates == 5U &&
                result.texture_allocations.native_allocation_destroys == 0U &&
                result.texture_allocations.exact_usage,
            "RT4/V1 allocated an unused source variant or lost a required derivative");
  }
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
  if (modern_pbr) {
    Matrix4x4 non_uniform_scale;
    non_uniform_scale.elements[0U] = 2.0F;
    const auto non_uniform_scene =
        MakeScene(700U, false, true, 1U, 1U, non_uniform_scale);
    RenderFrameOutput non_uniform_output;
    non_uniform_output.frame_id = 778U;
    const RenderOperationResult non_uniform_result = frontend.Render(
        MakeFrame(1U, non_uniform_scene, PixelFormat::RGBA16_FLOAT),
        non_uniform_output);
    result.non_uniform_scale_rejected_before_submission =
        non_uniform_result.code == RenderOperationCode::UNSUPPORTED &&
        non_uniform_output.frame_id == 778U &&
        !frontend.IsFrameComplete(1U);
    Require(result.non_uniform_scale_rejected_before_submission,
            "non-uniform RT4/V1 scale mutated output or reached native submission");
  }

  RenderFrameOutput hdr_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, scene_one, PixelFormat::RGBA16_FLOAT),
                     hdr_output),
                 "HDR Render");
  Require(frontend.IsFrameComplete(1U),
          "synchronous HDR frame was not complete on return");
  RequireSuccess(frontend.WaitForFrame(1U, 0U), "WaitForFrame(HDR)");
  result.hdr = InspectHdr(hdr_output);
  if (modern_pbr) {
    result.analytic_sky.first_committed =
        frontend.QueryAnalyticSkyAudit();
    const OgreNextAnalyticSkyRuntimeAudit &sky =
        result.analytic_sky.first_committed;
    Require(sky.version == 2U && sky.native_render_policy_version == 1U &&
                sky.completed_frames == 1U &&
                sky.native_mesh_creates == 2U &&
                sky.native_mesh_destroys == 2U &&
                sky.native_vertex_buffer_creates == 2U &&
                sky.native_vertex_buffer_destroys == 2U &&
                sky.native_index_buffer_creates == 2U &&
                sky.native_index_buffer_destroys == 2U &&
                sky.native_vao_creates == 2U &&
                sky.native_vao_destroys == 2U &&
                sky.native_item_creates == 2U &&
                sky.native_item_destroys == 2U &&
                sky.native_scene_node_creates == 1U &&
                sky.native_scene_node_destroys == 1U &&
                sky.native_datablock_creates == 2U &&
                sky.native_datablock_destroys == 2U &&
                sky.native_mesh_absence_checks == 2U &&
                sky.native_item_absence_checks == 2U &&
                sky.native_scene_node_absence_checks == 1U &&
                sky.native_datablock_absence_checks == 2U &&
                sky.native_gpu_content_readbacks == 4U &&
                sky.native_state_verifications == 1U &&
                sky.last_cpu_geometry_fnv1a64 != 0U &&
                sky.last_sun_light_id == 1U && sky.last_descriptor.enabled &&
                sky.last_descriptor.sun_light_id == 1U &&
                sky.camera_centered && sky.rendered_first &&
                sky.depth_check_disabled && sky.depth_write_disabled &&
                sky.additive_sun_disk &&
                sky.separate_sun_alpha_replace &&
                sky.native_geometry_metadata_verified &&
                sky.exact_native_geometry_readback && !sky.casts_shadows &&
                sky.portable_scene_identity_absent,
            "RT4/V1 did not publish one exact native analytic-sky frame");
    result.reflection_probes = frontend.QueryReflectionProbeAudit();
    Require(result.reflection_probes.version == 2U &&
                result.reflection_probes.initialized &&
                result.reflection_probes.compositor_defined_in_code &&
                result.reflection_probes.exact_resources_loaded &&
                result.reflection_probes.pcc_enabled &&
                result.reflection_probes.pbs_bound &&
                result.reflection_probes.successful_capture_count == 1U &&
                result.reflection_probes.failed_capture_count == 0U &&
                result.reflection_probes.live_probe_count == 1U &&
                result.reflection_probes.blend_resolution == 2048U &&
                result.reflection_probes.committed_state_digest != 0U &&
                result.reflection_probes.native_execution_evidence != 0U &&
                result.reflection_probes.last_capture_frame_id == 1U &&
                result.reflection_probes.last_capture_simulation_tick == 1U &&
                result.reflection_probes.last_probe_id == 1U &&
                result.reflection_probes.last_content_revision == 1U &&
                result.reflection_probes.last_candidate_generation == 1U &&
                result.reflection_probes.last_deterministic_seed != 0U &&
                result.reflection_probes.last_capture_digest != 0U &&
                result.reflection_probes.last_canonical_payload_bytes != 0U &&
                result.reflection_probes.filtered_finite_component_count != 0U &&
                result.reflection_probes.filtered_nonzero_rgb_component_count !=
                    0U &&
                result.reflection_probes.filtered_max_absolute_rgb > 0.0F &&
                result.reflection_probes.completed_face_count == 6U &&
                result.reflection_probes.completed_mip_count == 2U &&
                result.reflection_probes.ui_free_capture &&
                result.reflection_probes.reserved_render_queue_excluded,
            "RT4/V1 did not publish one complete native PCC probe generation");
  }

  RenderFrameOutput sdr_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(2U, scene_one, PixelFormat::RGBA8_SRGB),
                     sdr_output),
                 "SDR Render");
  result.sdr = InspectSdr(sdr_output);
  if (modern_pbr) {
    result.reflection_probes = frontend.QueryReflectionProbeAudit();
    result.reflection_capture =
        frontend.QueryReflectionProbeCaptureEvidence();
    Require(result.reflection_probes.blend_texture_ready &&
                result.reflection_probes.successful_capture_count == 1U &&
                result.reflection_probes.live_probe_count == 1U &&
                result.reflection_capture.valid &&
                result.reflection_capture.version == 1U &&
                result.reflection_capture.render_frame_id == 1U &&
                result.reflection_capture.simulation_tick == 1U &&
                result.reflection_capture.probe_id == 1U &&
                result.reflection_capture.content_revision == 1U &&
                result.reflection_capture.candidate_generation == 1U &&
                result.reflection_capture.capture_resolution == 32U &&
                result.reflection_capture.filtered_mips.face_count == 6U &&
                result.reflection_capture.filtered_mips.mip_count == 2U &&
                result.reflection_capture.raw_mip_zero_rgba16f.size() ==
                    32U * 32U * 6U * 8U &&
                result.reflection_capture.filtered_rgba16f.size() ==
                    (32U * 32U + 16U * 16U) * 6U * 8U,
            "RT4/V1 PCC blend texture was not consumed on the next main frame");
  }

  if (modern_pbr) {
    VariantEvidence baseline;
    baseline.name = kVariantSpecs.front().name;
    baseline.changed_input = kVariantSpecs.front().changed_input;
    baseline.asset_sequence = kVariantSpecs.front().sequence;
    baseline.hdr = result.hdr;
    baseline.sdr = result.sdr;
    result.variants.push_back(std::move(baseline));
  }

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

  RenderAssetDelta final_catalog = catalog;
  std::shared_ptr<const SceneSnapshot> final_scene = scene_one;
  std::uint64_t next_frame_id = 5U;
  if (modern_pbr) {
    const RenderFrameRequest baseline_hdr_frame =
        MakeFrame(1U, scene_one, PixelFormat::RGBA16_FLOAT);
    const RenderFrameRequest baseline_sdr_frame =
        MakeFrame(2U, scene_one, PixelFormat::RGBA8_SRGB);
    for (std::size_t variant_index = 1U;
         variant_index < kVariantSpecs.size(); ++variant_index) {
      const VariantSpec &spec = kVariantSpecs[variant_index];
      RenderAssetDelta variant_catalog = MakeCatalog(true, &spec);
      RequireControlledCatalog(catalog, variant_catalog, spec);
      RequireSuccess(frontend.SynchronizeAssets(variant_catalog),
                     std::string("SynchronizeAssets(") + spec.name + ')');
      const OgreNextN1TextureAllocationAudit audit =
          frontend.QueryTextureAllocationAudit();
      Require(audit.version == 2U && audit.live_source_textures == 4U &&
                  audit.sampled_rgba_allocations == 2U &&
                  audit.linear_rgba_allocations == 0U &&
                  audit.roughness_r8_allocations == 1U &&
                  audit.metallic_r8_allocations == 1U &&
                  audit.normal_rg8_allocations == 1U &&
                  audit.native_allocation_creates ==
                      spec.expected_native_creates &&
                  audit.native_allocation_destroys ==
                      spec.expected_native_destroys &&
                  audit.retired_name_lookups ==
                      spec.expected_native_destroys &&
                  audit.retired_name_rejections ==
                      spec.expected_native_destroys &&
                  audit.exact_usage,
              std::string("RT4/V1 replacement allocation drifted for ") +
                  spec.name);

      const auto variant_scene = MakeScene(
          100U + variant_index, false, true, spec.sequence,
          spec.material_revision);
      RenderFrameRequest variant_hdr_frame =
          MakeFrame(next_frame_id++, variant_scene,
                    PixelFormat::RGBA16_FLOAT);
      RenderFrameRequest variant_sdr_frame =
          MakeFrame(next_frame_id++, variant_scene,
                    PixelFormat::RGBA8_SRGB);
      RequireControlledSceneAndView(*scene_one, *variant_scene,
                                    baseline_hdr_frame, variant_hdr_frame);
      RequireControlledSceneAndView(*scene_one, *variant_scene,
                                    baseline_sdr_frame, variant_sdr_frame);

      RenderFrameOutput variant_hdr_output;
      RequireSuccess(frontend.Render(variant_hdr_frame, variant_hdr_output),
                     std::string("HDR Render(") + spec.name + ')');
      RenderFrameOutput variant_sdr_output;
      RequireSuccess(frontend.Render(variant_sdr_frame, variant_sdr_output),
                     std::string("SDR Render(") + spec.name + ')');
      VariantEvidence evidence;
      evidence.name = spec.name;
      evidence.changed_input = spec.changed_input;
      evidence.asset_sequence = spec.sequence;
      evidence.hdr = InspectHdr(variant_hdr_output);
      evidence.sdr = InspectSdr(variant_sdr_output);
      evidence.hdr_changed_pixels = CountChangedPixels(
          result.hdr.attachment_bytes, evidence.hdr.attachment_bytes, 8U);
      evidence.sdr_changed_pixels = CountChangedPixels(
          result.sdr.attachment_bytes, evidence.sdr.attachment_bytes, 4U);
      Require(evidence.hdr_changed_pixels >= 64U &&
                  evidence.sdr_changed_pixels >= 64U &&
                  evidence.hdr.attachment_fnv1a64 !=
                      result.hdr.attachment_fnv1a64 &&
                  evidence.sdr.attachment_fnv1a64 !=
                      result.sdr.attachment_fnv1a64,
              std::string("RT4/V1 isolated texture input produced no exact HDR/SDR effect: ") +
                  spec.name);
      result.variants.push_back(std::move(evidence));
      final_catalog = std::move(variant_catalog);
      final_scene = variant_scene;
    }
    result.replacement_final_audit =
        frontend.QueryTextureAllocationAudit();
    const OgreNextN1TextureAllocationAudit &final_audit =
        result.replacement_final_audit;
    result.live_replacement_retirement =
        result.variants.size() == kVariantSpecs.size() &&
        final_audit.native_allocation_creates == 17U &&
        final_audit.native_allocation_destroys == 12U &&
        final_audit.live_native_allocations == 5U &&
        final_audit.retired_name_lookups == 12U &&
        final_audit.retired_name_rejections == 12U &&
        final_audit.exact_usage;
    result.analytic_sky.final_committed =
        frontend.QueryAnalyticSkyAudit();
    const OgreNextAnalyticSkyRuntimeAudit &sky =
        result.analytic_sky.final_committed;
    Require(sky.completed_frames >= 4U &&
                sky.native_mesh_creates == sky.completed_frames * 2U &&
                sky.native_mesh_destroys == sky.completed_frames * 2U &&
                sky.native_vertex_buffer_creates ==
                    sky.completed_frames * 2U &&
                sky.native_vertex_buffer_destroys ==
                    sky.completed_frames * 2U &&
                sky.native_index_buffer_creates ==
                    sky.completed_frames * 2U &&
                sky.native_index_buffer_destroys ==
                    sky.completed_frames * 2U &&
                sky.native_vao_creates == sky.completed_frames * 2U &&
                sky.native_vao_destroys == sky.completed_frames * 2U &&
                sky.native_item_creates == sky.completed_frames * 2U &&
                sky.native_item_destroys == sky.completed_frames * 2U &&
                sky.native_scene_node_creates == sky.completed_frames &&
                sky.native_scene_node_destroys == sky.completed_frames &&
                sky.native_datablock_creates == sky.completed_frames * 2U &&
                sky.native_datablock_destroys == sky.completed_frames * 2U &&
                sky.native_mesh_absence_checks ==
                    sky.completed_frames * 2U &&
                sky.native_item_absence_checks ==
                    sky.completed_frames * 2U &&
                sky.native_scene_node_absence_checks ==
                    sky.completed_frames &&
                sky.native_datablock_absence_checks ==
                    sky.completed_frames * 2U &&
                sky.native_gpu_content_readbacks ==
                    sky.completed_frames * 4U &&
                sky.native_state_verifications == sky.completed_frames &&
                sky.last_cpu_geometry_fnv1a64 != 0U &&
                sky.last_sun_light_id == 1U &&
                sky.last_descriptor.enabled && sky.camera_centered &&
                sky.rendered_first && sky.depth_check_disabled &&
                sky.depth_write_disabled && sky.additive_sun_disk &&
                sky.separate_sun_alpha_replace &&
                sky.native_geometry_metadata_verified &&
                sky.exact_native_geometry_readback && !sky.casts_shadows &&
                sky.portable_scene_identity_absent,
            "RT4/V1 analytic-sky lifetime counters did not remain balanced");
  }
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "first Shutdown");

  if (modern_pbr) {
    OgreNextN1Configuration replay_configuration{
        media_root, OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1};
    replay_configuration.retain_reflection_capture_evidence = true;
    OgreNextN1Frontend replay(std::move(replay_configuration));
    InitializeAndSync(replay, catalog);
    RenderFrameOutput replay_output;
    RequireSuccess(replay.Render(
                       MakeFrame(1U, scene_one, PixelFormat::RGBA16_FLOAT),
                       replay_output),
                   "reflection deterministic replay Render");
    static_cast<void>(InspectHdr(replay_output));
    const OgreNextReflectionProbeCaptureEvidence replay_capture =
        replay.QueryReflectionProbeCaptureEvidence();
    result.reflection_same_device_deterministic_replay =
        replay_capture.valid &&
        replay_capture.backend == result.reflection_capture.backend &&
        replay_capture.render_system ==
            result.reflection_capture.render_system &&
        replay_capture.device_name == result.reflection_capture.device_name &&
        replay_capture.driver_version ==
            result.reflection_capture.driver_version &&
        replay_capture.deterministic_seed ==
            result.reflection_capture.deterministic_seed &&
        replay_capture.raw_mip_zero_rgba16f ==
            result.reflection_capture.raw_mip_zero_rgba16f &&
        replay_capture.filtered_rgba16f ==
            result.reflection_capture.filtered_rgba16f;
    Require(result.reflection_same_device_deterministic_replay,
            "RT4/V1 reflection capture changed across same-device replay");
    RequireSuccess(replay.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                   "reflection deterministic replay Shutdown");
  }

  InitializeAndSync(concurrent, final_catalog);
  RequireSuccess(concurrent.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "post-owner-release concurrent Shutdown");

  InitializeAndSync(frontend, final_catalog);
  RenderFrameOutput recovered_output;
  RequireSuccess(frontend.Render(
                     MakeFrame(1U, final_scene, PixelFormat::RGBA8_SRGB),
                     recovered_output),
                 "post-reinitialize Render");
  static_cast<void>(InspectSdr(recovered_output));
  RequireSuccess(frontend.Shutdown(kInfiniteRenderTimeoutNanoseconds),
                 "recovery Shutdown");
  return result;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Arguments arguments = ParseArguments(argc, argv);
    const SmokeResult result =
        RunSmoke(arguments.media_root, arguments.modern_pbr);
    WritePpm(arguments.image_path,
             arguments.modern_pbr ? result.hdr_compositor.final : result.sdr);
    if (arguments.modern_pbr) {
      WriteIsolationEvidence(arguments.evidence_path, result);
      WriteReflectionEvidence(arguments.reflection_evidence_path, result);
      WriteHdrCompositorEvidence(arguments.compositor_evidence_path,
                                 result.hdr_compositor);
      WriteAnalyticSkyEvidence(arguments.analytic_sky_evidence_path,
                               result.analytic_sky);
      WritePpm(arguments.analytic_sky_image_path,
               result.analytic_sky.camera_facing_sun_sdr,
               result.analytic_sky.visual_width,
               result.analytic_sky.visual_height);
    }
    const std::string report = MakeReport(
        result, arguments.modern_pbr, arguments.evidence_path,
        arguments.reflection_evidence_path,
        arguments.compositor_evidence_path,
        arguments.analytic_sky_evidence_path,
        arguments.analytic_sky_image_path);
    WriteText(arguments.report_path, report);
    std::cout << report;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Ogre-Next N1 frontend smoke failed: " << error.what() << '\n';
    return 1;
  }
}
