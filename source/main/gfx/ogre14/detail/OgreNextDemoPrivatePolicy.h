/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Pure policy used only by the disposable OgreNext product demo.

#pragma once

#include "gfx/render/Ogre14SourceTextureDecoder.h"
#include "gfx/render/RenderResourceDescriptors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RoR::Gfx::Detail {

enum class OgreNextDemoTextureSourceMode : std::uint8_t {
  AUTHENTICATED_ARCHIVE_SOURCE_BYTES = 0U,
  AUTHENTICATED_GENERATED_SOURCE_BYTES = 1U,
  ORDINARY_OBSERVED_SOURCE_BYTES = 2U,
};

/// Versioned alpha interpretation for decoded conventional sRGB base colour.
/// PRESERVE_STRAIGHT keeps every authored alpha byte and generates only a
/// missing tail by filtering in premultiplied linear light, then robustly
/// unpremultiplying back to straight RGBA8. Alpha testing uses this same byte
/// policy; it deliberately performs no coverage remapping and retains one
/// fixed cutoff at every level.
enum class OgreNextDemoTextureAlphaPolicy : std::uint8_t {
  FORCE_OPAQUE = 0U,
  PRESERVE_STRAIGHT = 1U,
};

/// Bounded reasons why an automatic TUS0 remains on the deterministic matte
/// path.
/// These are policy outcomes, not permission to inspect GPU storage.
enum class OgreNextDemoTextureProjectionExclusion : std::uint8_t {
  NONE = 0U,
  SOURCE_UNAVAILABLE = 1U,
  MANUAL_OR_PROCEDURAL = 2U,
  RENDER_TARGET = 3U,
  CUBE_TEXTURE = 4U,
  VOLUME_TEXTURE = 5U,
  NON_2D = 6U,
  NON_UNIT_DEPTH = 7U,
  NON_UNIT_FACE_COUNT = 8U,
  DIMENSION_OUT_OF_RANGE = 9U,
  ORDINARY_SELECTED_SOURCE_UNAVAILABLE = 10U,
  UNSUPPORTED_SOURCE_CONTAINER = 11U,
  UNSUPPORTED_SOURCE_SEMANTIC = 12U,
  SOURCE_DECODE_REJECTED = 13U,
  MISSING_AUTHORED_UV0 = 14U,
  MATERIAL_STRUCTURE_UNSUPPORTED = 15U,
  MATERIAL_STATE_UNSUPPORTED = 16U,
  TEXTURE_UNIT_STRUCTURE_UNSUPPORTED = 17U,
  TEXTURE_UNIT_SEMANTIC_UNSUPPORTED = 18U,
  SAMPLER_STATE_UNSUPPORTED = 19U,
  ALEXIS_APPROXIMATION_UNSAFE = 20U,
  TEXTURE_ALPHA_COMBINE_UNSUPPORTED = 21U,
  ALPHA_STATE_UNSUPPORTED = 22U,
  MANAGED_MATERIAL_AUTHORITY_UNAVAILABLE = 23U,
  MANAGED_MATERIAL_SEMANTIC_UNSUPPORTED = 24U,
  AMBIGUOUS_BC1_ALPHA_SEMANTIC = 25U,
  COUNT = 26U,
};

constexpr std::size_t kOgreNextDemoTextureProjectionExclusionCount =
    static_cast<std::size_t>(OgreNextDemoTextureProjectionExclusion::COUNT);

/// Transactional source accounting. The three legacy fields are retained for
/// log compatibility; all GPU-readback fields are hard-zero invariants.
struct OgreNextDemoTextureSourceCounters final {
  std::size_t authenticated_archive_source_decodes = 0U;
  std::size_t authenticated_generated_source_decodes = 0U;
  std::size_t ordinary_observed_source_decodes = 0U;
  std::size_t source_cache_hits = 0U;
  std::size_t source_decode_rejections = 0U;
  std::size_t source_exclusions = 0U;
  std::array<std::size_t, kOgreNextDemoTextureProjectionExclusionCount>
      exclusions_by_reason{};
  std::size_t gpu_readbacks = 0U;
  std::size_t authenticated_source_decodes = 0U;
  std::size_t authenticated_gpu_readbacks = 0U;
  std::size_t unauthenticated_gpu_readbacks = 0U;
  std::size_t projections = 0U;
  /// Each section first freezes a decision per map generation; only explicit
  /// source-unavailable reasons are re-evaluated for later promotion.
  /// Candidate/projected/matte counts below are section observations in the
  /// current capture; lifetime accumulation therefore sums observations, not
  /// unique map identities.
  std::size_t new_frozen_material_decisions = 0U;
  std::size_t candidate_sections = 0U;
  std::size_t projected_sections = 0U;
  std::size_t matte_excluded_sections = 0U;
  /// Per-capture distinct key cardinalities. Lifetime accumulation is the sum
  /// of per-capture cardinalities and deliberately is not global uniqueness.
  std::size_t distinct_eligible_texture_keys = 0U;
  std::size_t distinct_projected_texture_keys = 0U;
  std::size_t distinct_matte_only_texture_keys = 0U;
  std::size_t modern_source_normalizations = 0U;
  std::size_t authored_mip_prefix_levels = 0U;
  std::size_t generated_mip_tail_levels = 0U;
  std::size_t normalized_output_mip_levels = 0U;
  std::size_t legacy_native_additional_mip_levels = 0U;
  std::size_t legacy_texture_unit_gamma_nonunit_observations = 0U;
  std::size_t legacy_texture_gamma_nonunit_observations = 0U;
  std::size_t legacy_texture_unit_hardware_gamma_off_observations = 0U;
  std::size_t legacy_hardware_gamma_off_observations = 0U;
  std::size_t legacy_automipmap_observations = 0U;
  /// Active per-capture distinct texture observations. These drive the
  /// change-only coverage snapshot; lifetime values sum committed captures.
  std::size_t active_texture_state_observations = 0U;
  std::size_t active_authored_mip_prefix_levels = 0U;
  std::size_t active_generated_mip_tail_levels = 0U;
  std::size_t active_normalized_output_mip_levels = 0U;
  std::size_t active_legacy_native_additional_mip_levels = 0U;
  std::size_t active_legacy_texture_unit_gamma_nonunit_observations = 0U;
  std::size_t active_legacy_texture_gamma_nonunit_observations = 0U;
  std::size_t active_legacy_texture_unit_hardware_gamma_off_observations = 0U;
  std::size_t active_legacy_hardware_gamma_off_observations = 0U;
  std::size_t active_legacy_automipmap_observations = 0U;
  /// Ambient/specular and the legacy fixed-function lighting equation are
  /// deliberately normalized, not translated, by the current versioned PBR
  /// policy. This count prevents that loss from being presented as parity.
  std::size_t lossy_material_normalizations = 0U;
  std::size_t opaque_source_normalizations = 0U;
  std::size_t straight_alpha_source_normalizations = 0U;
  std::size_t alpha_test_material_projections = 0U;
  std::size_t straight_source_over_material_projections = 0U;
  std::size_t legacy_straight_alpha_material_projections = 0U;
  std::size_t specular_workflow_projections = 0U;
  std::size_t authored_specular_source_decodes = 0U;
  /// New managed linear-specular decode activity. These buckets are also
  /// included in the common modern normalization/mip totals above; the
  /// explicit fields make the authored specular contribution auditable.
  std::size_t linear_specular_source_normalizations = 0U;
  std::size_t authored_specular_mip_prefix_levels = 0U;
  std::size_t generated_specular_mip_tail_levels = 0U;
  std::size_t normalized_specular_output_mip_levels = 0U;
  std::size_t anisotropic_sampler_projections = 0U;
  /// Exact active-projection partitions. `projections` is the common
  /// denominator: every reachable projection belongs to exactly one member
  /// of each blend, alpha-test, and PBR-workflow partition. Lifetime values
  /// are sums of these per-capture active inventories.
  std::size_t active_replace_material_projections = 0U;
  std::size_t active_straight_source_over_material_projections = 0U;
  std::size_t active_legacy_straight_alpha_material_projections = 0U;
  std::size_t active_alpha_test_disabled_material_projections = 0U;
  std::size_t active_alpha_test_greater_material_projections = 0U;
  std::size_t active_alpha_test_greater_equal_material_projections = 0U;
  std::size_t active_metallic_roughness_workflow_projections = 0U;
  std::size_t active_specular_workflow_projections = 0U;
  std::size_t active_anisotropic_sampler_projections = 0U;
  /// Exact active normalized-texture partition, including a distinct managed
  /// specular texture only once even when several projections share it.
  std::size_t active_normalized_texture_observations = 0U;
  std::size_t active_opaque_texture_normalizations = 0U;
  std::size_t active_straight_alpha_texture_normalizations = 0U;
  std::size_t active_linear_specular_texture_normalizations = 0U;
};

[[nodiscard]] bool IsOgreNextDemoAuthenticatedTextureSourceMode(
    OgreNextDemoTextureSourceMode mode) noexcept;

/// Records one committed source decode, one distinct per-capture cache reuse,
/// or one matte exclusion. Invalid enum values and any pre-existing/readback
/// count fail without changing `counters`.
[[nodiscard]] Render::ValidationResult RecordOgreNextDemoTextureSourceDecode(
    OgreNextDemoTextureSourceMode mode,
    OgreNextDemoTextureSourceCounters &counters);
[[nodiscard]] Render::ValidationResult RecordOgreNextDemoTextureSourceCacheHit(
    OgreNextDemoTextureSourceCounters &counters);
[[nodiscard]] Render::ValidationResult
RecordOgreNextDemoTextureProjectionExclusion(
    OgreNextDemoTextureProjectionExclusion exclusion,
    OgreNextDemoTextureSourceCounters &counters);

[[nodiscard]] std::string_view OgreNextDemoTextureProjectionExclusionName(
    OgreNextDemoTextureProjectionExclusion exclusion) noexcept;

/// Saturating accumulation for a committed capture. A nonzero GPU-readback
/// observation rejects the candidate and leaves `total` unchanged.
[[nodiscard]] Render::ValidationResult
AccumulateOgreNextDemoTextureSourceCounters(
    const OgreNextDemoTextureSourceCounters &increment,
    OgreNextDemoTextureSourceCounters &total);

/// Renderer-neutral eligibility facts gathered from the live TUS0. Exactly one
/// exclusion is selected in stable priority order.
struct OgreNextDemoTextureEligibilityObservation final {
  bool source_available = false;
  bool manually_loaded = false;
  bool render_target = false;
  bool cube_texture = false;
  bool volume_texture = false;
  bool texture_2d = false;
  bool unit_depth = false;
  bool unit_face_count = false;
  bool dimensions_in_range = false;
};

[[nodiscard]] Render::ValidationResult
ClassifyOgreNextDemoTextureProjectionEligibility(
    const OgreNextDemoTextureEligibilityObservation &observation,
    OgreNextDemoTextureProjectionExclusion &output);

/// Exact renderer-neutral snapshot of every OGRE sampler field observed while
/// the native Sampler is live. Unsupported native enum values are represented
/// explicitly and never approximated.
enum class OgreNextDemoObservedSamplerFilter : std::uint8_t {
  POINT = 0U,
  LINEAR = 1U,
  ANISOTROPIC = 2U,
  UNSUPPORTED = 3U,
};

enum class OgreNextDemoObservedSamplerAddressMode : std::uint8_t {
  WRAP = 0U,
  MIRROR = 1U,
  CLAMP = 2U,
  UNSUPPORTED = 3U,
};

struct OgreNextDemoExactSamplerObservation final {
  OgreNextDemoObservedSamplerFilter minification_filter =
      OgreNextDemoObservedSamplerFilter::UNSUPPORTED;
  OgreNextDemoObservedSamplerFilter magnification_filter =
      OgreNextDemoObservedSamplerFilter::UNSUPPORTED;
  OgreNextDemoObservedSamplerFilter mip_filter =
      OgreNextDemoObservedSamplerFilter::UNSUPPORTED;
  OgreNextDemoObservedSamplerAddressMode address_u =
      OgreNextDemoObservedSamplerAddressMode::UNSUPPORTED;
  OgreNextDemoObservedSamplerAddressMode address_v =
      OgreNextDemoObservedSamplerAddressMode::UNSUPPORTED;
  OgreNextDemoObservedSamplerAddressMode address_w =
      OgreNextDemoObservedSamplerAddressMode::UNSUPPORTED;
  float mip_lod_bias = 0.0F;
  std::uint32_t maximum_anisotropy = 1U;
  bool compare_enabled = false;
  /// Exact OGRE CompareFunction numeric token. It is fingerprinted even though
  /// compare_enabled must be false and the portable descriptor is canonical.
  std::uint8_t compare_function_token = 0U;
  std::array<float, 4U> border_color{};
};

[[nodiscard]] bool MatchOgreNextDemoExactSamplerObservation(
    const OgreNextDemoExactSamplerObservation &left,
    const OgreNextDemoExactSamplerObservation &right) noexcept;

/// Admits POINT/LINEAR filtering or the exact all-three-filter ANISOTROPIC
/// state, WRAP/MIRROR/CLAMP addressing, zero LOD bias, and disabled comparison.
/// Anisotropy retains the exact authored maximum in (1, 16]. The exact border
/// color is retained in the portable descriptor, but border addressing is
/// unsupported.
[[nodiscard]] Render::ValidationResult BuildOgreNextDemoSamplerDescriptor(
    const OgreNextDemoExactSamplerObservation &observation,
    std::size_t mip_count, std::string_view debug_token,
    Render::SamplerResourceDescriptor &output);

/// Exact native texture/TUS state captured while all native owners are live.
/// These values are provenance and revalidation inputs only; they never
/// authorize GPU readback or override decoded source bytes.
struct OgreNextDemoExactTextureObservation final {
  float texture_unit_gamma = 1.0F;
  float texture_gamma = 1.0F;
  bool texture_unit_hardware_gamma = false;
  bool texture_hardware_gamma = false;
  std::uint32_t additional_mip_count = 0U;
  std::uint32_t actual_mip_count = 1U;
  bool mipmaps_hardware_generated = false;
  std::uint32_t usage_token = 0U;
  std::uint32_t source_width = 0U;
  std::uint32_t source_height = 0U;
  std::uint32_t source_depth = 0U;
  std::uint32_t source_format_token = 0U;
  std::uint32_t output_width = 0U;
  std::uint32_t output_height = 0U;
  std::uint32_t output_depth = 0U;
  std::uint32_t output_format_token = 0U;
  std::uint32_t face_count = 0U;
  std::uint32_t texture_type_token = 0U;
};

[[nodiscard]] Render::ValidationResult
ValidateOgreNextDemoExactTextureObservation(
    const OgreNextDemoExactTextureObservation &observation);
[[nodiscard]] bool MatchOgreNextDemoExactTextureObservation(
    const OgreNextDemoExactTextureObservation &left,
    const OgreNextDemoExactTextureObservation &right) noexcept;

struct OgreNextDemoTextureSourceSelection final {
  bool selected = false;
  OgreNextDemoTextureSourceMode mode =
      OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES;
  OgreNextDemoTextureProjectionExclusion exclusion =
      OgreNextDemoTextureProjectionExclusion::SOURCE_UNAVAILABLE;
};

/// Renderer-neutral inventory rows copied from MaterialSource's actual frozen
/// projection/texture/sampler maps immediately before Apply publication.
struct OgreNextDemoCachedProjectionPublicationInput final {
  std::string projection_key;
  std::string texture_key;
  std::string sampler_key;
  std::uint64_t material_source_id = 0U;
};

struct OgreNextDemoCachedTexturePublicationInput final {
  std::string texture_key;
  std::uint64_t texture_source_id = 0U;
  OgreNextDemoTextureSourceMode source_mode =
      OgreNextDemoTextureSourceMode::ORDINARY_OBSERVED_SOURCE_BYTES;
};

struct OgreNextDemoCachedSamplerPublicationInput final {
  std::string sampler_key;
  std::uint64_t sampler_source_id = 0U;
};

struct OgreNextDemoCachedProjectionPublicationOwner final {
  std::string projection_key;
  std::uint64_t material_source_id = 0U;
  std::uint64_t texture_source_id = 0U;
  std::uint64_t sampler_source_id = 0U;
  bool frame_reachable = false;
};

/// All cached owners remain in the asset catalog to prevent source-ID
/// resurrection. Only material IDs in frame_root_material_source_ids may be
/// reached by current instances/environment closures.
struct OgreNextDemoCachedProjectionPublicationTransaction final {
  std::vector<OgreNextDemoCachedProjectionPublicationOwner> owner_catalog;
  std::vector<std::uint64_t> frame_root_material_source_ids;
  std::vector<std::string> authenticated_texture_keys;
  std::vector<std::string> ordinary_texture_keys;
};

class IOgreNextDemoTexturePublicationBatchValidator {
public:
  virtual ~IOgreNextDemoTexturePublicationBatchValidator() = default;

  /// Called once with the complete distinct frame-reachable authenticated
  /// texture-key batch, and never for an empty batch. The production adapter
  /// resolves every key, captures one common authority snapshot, then
  /// authenticates/revalidates the whole batch. There is deliberately no GPU
  /// readback operation in this interface.
  [[nodiscard]] virtual Render::ValidationResult
  ValidateReachableAuthenticatedTextureBatch(
      const std::vector<std::string> &texture_keys) = 0;

  /// Called once with the complete distinct frame-reachable ordinary selected
  /// source batch, and never for an empty batch. Implementations fresh-resolve
  /// every key and immutable-match it to the frozen selected-source receipt.
  [[nodiscard]] virtual Render::ValidationResult
  ValidateReachableOrdinaryTextureBatch(
      const std::vector<std::string> &texture_keys) = 0;
};

/// Builds the exact all-cache Apply publication inventory and frame-root
/// closure. Every used projection must exist in the frozen cache. Reachable
/// authenticated textures are observed once before the transaction can
/// escape. `output` is unchanged on any validation/authority failure.
[[nodiscard]] Render::ValidationResult
BuildOgreNextDemoCachedProjectionPublicationTransaction(
    const std::vector<OgreNextDemoCachedProjectionPublicationInput>
        &projections,
    const std::vector<OgreNextDemoCachedTexturePublicationInput> &textures,
    const std::vector<OgreNextDemoCachedSamplerPublicationInput> &samplers,
    const std::vector<std::string> &used_projection_keys,
    IOgreNextDemoTexturePublicationBatchValidator &validator,
    OgreNextDemoCachedProjectionPublicationTransaction &output);

/// Renderer-neutral fail-closed decision among the three source-byte modes.
/// Required authentication must have one successful resolution and an exact
/// archive/generated classification. Ordinary content must not probe the
/// authenticated registry; an absent ordinary selected-source observation is
/// an explicit matte exclusion. Output is unchanged on sequencing failure.
[[nodiscard]] Render::ValidationResult SelectOgreNextDemoTextureSourceMode(
    bool authenticated_source_required, bool authenticated_resolution_attempted,
    const Render::ValidationResult &authenticated_resolution_result,
    OgreNextDemoTextureSourceMode authenticated_resolution_mode,
    bool ordinary_resolution_attempted,
    const Render::ValidationResult &ordinary_resolution_result,
    OgreNextDemoTextureSourceSelection &output);

/// Validates one cached source-mode observation without permitting authority
/// demotion. Unreachable entries may remain as immutable anti-tombstone owners
/// without probing live authority. A reachable authenticated entry requires a
/// successful fresh resolution whose receipt shares its frozen immutable state.
[[nodiscard]] Render::ValidationResult
ValidateOgreNextDemoCachedTextureSourceAuthority(
    OgreNextDemoTextureSourceMode frozen_mode, bool frame_reachable,
    bool source_classification_matches, bool fresh_resolution_attempted,
    const Render::ValidationResult &fresh_resolution_result,
    bool immutable_receipt_matches);

/// Canonicalized result of observing the exact OGRE terrain TUS0. Native
/// pointer/layout identity is retained in exact_native_state; the booleans
/// admit only the one sampling policy the demo can reproduce honestly.
struct OgreNextDemoSamplingObservation final {
  bool ordinary_texture = true;
  bool uv0_identity = true;
  bool sampler_identity = true;
  bool gamma_disabled = true;
  bool fog_disabled = true;
  std::string exact_native_state;
};

[[nodiscard]] Render::ValidationResult ValidateOgreNextDemoSampling(
    const OgreNextDemoSamplingObservation &observation);

[[nodiscard]] Render::ValidationResult
RevalidateOgreNextDemoSampling(const OgreNextDemoSamplingObservation &before,
                               const OgreNextDemoSamplingObservation &after);

/// Validates one freshly read tight RGBA8 base level, forces only its alpha
/// bytes opaque, then deterministically generates every remaining mip through
/// 1x1. Generated levels use an encoded/display-domain 2x2 integer box filter;
/// RGB bytes in the native base level are never rewritten. Reading native
/// nonzero mips is forbidden because pinned OGRE Metal aliases them to mip 0.
[[nodiscard]] Render::ValidationResult
CompleteOgreNextDemoOpaqueMipChain(Render::TextureResourceDescriptor &texture);

struct OgreNextDemoTextureNormalizationObservation final {
  enum class Policy : std::uint8_t {
    SRGB_OPAQUE_V2 = 0U,
    SRGB_STRAIGHT_ALPHA_V1 = 1U,
    LINEAR_SPECULAR_V1 = 2U,
  };
  Policy policy = Policy::SRGB_OPAQUE_V2;
  std::uint32_t policy_version = 0U;
  std::size_t authored_mip_prefix_levels = 0U;
  std::size_t generated_mip_tail_levels = 0U;
};

inline constexpr std::uint32_t
    kOgreNextDemoModernSourceNormalizationPolicyVersion = 2U;
inline constexpr std::string_view
    kOgreNextDemoModernSourceNormalizationPolicy =
        "srgb_opaque_authored_prefix_linear_tail_v2";
inline constexpr std::uint32_t
    kOgreNextDemoStraightAlphaNormalizationPolicyVersion = 1U;
inline constexpr std::string_view
    kOgreNextDemoStraightAlphaNormalizationPolicy =
        "srgb_straight_alpha_authored_prefix_premultiplied_linear_tail_v1";
inline constexpr std::uint32_t
    kOgreNextDemoLinearSpecularNormalizationPolicyVersion = 1U;
inline constexpr std::string_view
    kOgreNextDemoLinearSpecularNormalizationPolicy =
        "linear_specular_authored_prefix_box_tail_v1";

/// Chooses BC1 alpha decoding only from explicit authority. A blend/test pass
/// is never evidence that legacy DXT1 uses the one-bit BC1 interpretation.
/// The current automatic source path has no such authority and therefore
/// rejects PRESERVE_STRAIGHT DXT1 instead of corrupting an opaque texture.
[[nodiscard]] Render::ValidationResult ResolveOgreNextDemoBc1AlphaMode(
    bool legacy_dxt1, OgreNextDemoTextureAlphaPolicy alpha_policy,
    bool authoritative_one_bit_alpha,
    Render::Ogre14SourceTextureBc1AlphaMode &output) noexcept;

/// Completes a canonical tight RGBA8 authored mip prefix for a conventional
/// sRGB PBR base-color texture. Authored RGB bytes at every supplied level are
/// retained. Alpha is forced opaque at every level, and only a missing tail is
/// generated through 1x1 with the exact linear-light sRGB box rule. This path
/// intentionally exceeds the OGRE14 runtime state, which remains provenance
/// rather than output authority. The input and observation are unchanged on
/// failure.
[[nodiscard]] Render::ValidationResult CompleteOgreNextDemoSrgbPbrMipChain(
    Render::TextureResourceDescriptor &texture,
    OgreNextDemoTextureAlphaPolicy alpha_policy,
    OgreNextDemoTextureNormalizationObservation *observation = nullptr);

[[nodiscard]] inline Render::ValidationResult
CompleteOgreNextDemoSrgbPbrMipChain(
    Render::TextureResourceDescriptor &texture,
    OgreNextDemoTextureNormalizationObservation *observation = nullptr) {
  return CompleteOgreNextDemoSrgbPbrMipChain(
      texture, OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE, observation);
}

/// Validates and preserves the complete renderer-neutral decoded authored mip
/// prefix, then generates only the missing modern tail. `output` and optional
/// observation are unchanged on failure.
[[nodiscard]] Render::ValidationResult
BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
    Render::Ogre14DecodedSourceTexture decoded,
    std::uint32_t expected_native_width, std::uint32_t expected_native_height,
    std::string_view debug_name, OgreNextDemoTextureAlphaPolicy alpha_policy,
    Render::TextureResourceDescriptor &output,
    OgreNextDemoTextureNormalizationObservation *observation = nullptr);

[[nodiscard]] inline Render::ValidationResult
BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
    Render::Ogre14DecodedSourceTexture decoded,
    std::uint32_t expected_native_width, std::uint32_t expected_native_height,
    std::string_view debug_name, Render::TextureResourceDescriptor &output,
    OgreNextDemoTextureNormalizationObservation *observation = nullptr) {
  return BuildOgreNextDemoSrgbPbrTextureFromDecodedSource(
      std::move(decoded), expected_native_width, expected_native_height,
      debug_name, OgreNextDemoTextureAlphaPolicy::FORCE_OPAQUE, output,
      observation);
}

/// Authored linear RGBA8 specular RGB remains byte-exact for every supplied
/// level; alpha is canonicalized opaque because PBSM_SPECULAR consumes RGB.
/// Only a missing tail is generated with exact half-up byte-domain averaging.
[[nodiscard]] Render::ValidationResult
BuildOgreNextDemoLinearSpecularTextureFromDecodedSource(
    Render::Ogre14DecodedSourceTexture decoded,
    std::uint32_t expected_native_width, std::uint32_t expected_native_height,
    std::string_view debug_name, Render::TextureResourceDescriptor &output,
    OgreNextDemoTextureNormalizationObservation *observation = nullptr);

[[nodiscard]] Render::ValidationResult
DeriveOgreNextDemoSourceId(std::string_view domain, std::string_view exact_key,
                           std::uint64_t &source_id);

/// Canonicalizes an untextured demo-matte mesh to the exact RT4 vertex
/// layout. Authored UV0 is retained, absent UV0 becomes deterministic zero,
/// finite nonzero normals are normalized, unusable or absent normals become
/// deterministic +Y, tangent directions are rebuilt, and streams with no
/// matte consumer are removed. The input is unchanged on failure.
[[nodiscard]] Render::ValidationResult
NormalizeOgreNextDemoMatteMesh(Render::MeshResourceDescriptor &mesh);

/// Sanitizes the complete normal stream and rebuilds the same matte-only
/// tangent basis for a joined dynamic update. Finite nonzero directions are
/// normalized; absent, zero, or non-finite directions become +Y. Both output
/// streams are unchanged on structural failure.
[[nodiscard]] Render::ValidationResult
BuildOgreNextDemoMatteTangents(std::size_t vertex_count,
                               std::vector<Render::Float3> &normals,
                               std::vector<Render::Float4> &tangents);

/// Builds the smallest camera-centered sphere containing the normalized
/// OgreNext demo far frustum. Source offsets and vertical FOV are retained;
/// target_aspect supplies the child surface aspect.
[[nodiscard]] Render::ValidationResult BuildOgreNextDemoStaticCaptureRadius(
    float left, float right, float top, float bottom, float near_plane,
    float far_plane, float target_aspect, float &radius_meters);

/// Classifies a finite world AABB by closest-point distance to the enclosing
/// demo frustum sphere. The output remains unchanged on failure.
[[nodiscard]] Render::ValidationResult ClassifyOgreNextDemoStaticBounds(
    const Render::Bounds3 &world_bounds, const Render::Float3 &camera_position,
    float radius_meters, bool &within_capture_radius);

/// Bidirectional collision audit. Transactions copy this private registry,
/// mutate the candidate, and replace the committed owner only on commit.
class OgreNextDemoIdentityRegistry final {
public:
  [[nodiscard]] Render::ValidationResult Register(std::string exact_key,
                                                  std::uint64_t source_id);
  [[nodiscard]] bool Contains(std::string_view exact_key,
                              std::uint64_t source_id) const;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  std::map<std::uint64_t, std::string> keys_by_id_;
  std::map<std::string, std::uint64_t, std::less<>> ids_by_key_;
};

[[nodiscard]] bool
OgreNextDemoRequiresMatte(std::size_t texture_unit_count,
                          bool has_authored_program) noexcept;
[[nodiscard]] bool
OgreNextDemoDropsDynamicBlendColors(bool has_dynamic_texture_blend) noexcept;
[[nodiscard]] bool
OgreNextDemoOmitsInvisibleCab(std::string_view exact_material_name,
                              float diffuse_alpha,
                              bool depth_write_enabled) noexcept;
[[nodiscard]] bool OgreNextDemoOmitsNonUniformSpeedBump(
    std::string_view exact_mesh_name,
    const Render::Float3 &derived_scale) noexcept;

/// Exact content-scoped exception for the first macOS demo. Only the four
/// reviewed opaque Alexis bases (Chassis, ChassisM, Wheels, Grilles) may lower
/// their authenticated two-pass declaration to diffuse plus authored linear
/// specular PBS inputs. Lens, Winds, and Winds_int remain excluded, so this is
/// deliberately reported as 4/7 authored Alexis specular declarations rather
/// than full bundle coverage. No other material receives this shortcut.
[[nodiscard]] bool OgreNextDemoAllowsAlexisTUS0Approximation(
    std::string_view exact_resource_group,
    std::string_view exact_material_name) noexcept;

} // namespace RoR::Gfx::Detail
