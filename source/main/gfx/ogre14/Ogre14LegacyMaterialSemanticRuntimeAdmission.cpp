/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14LegacyMaterialSemanticRuntimeAdmission.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace RoR::Render {

#if !defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
// Production has no public fault callback or stage type. These private stage
// labels keep the shared implementation readable while MaybeInject compiles to
// a no-op and every production entry point supplies only a null opaque pointer.
enum class Ogre14LegacyMaterialSemanticRuntimeAdmissionStage : std::uint8_t {
  AFTER_EXTERNAL_FULL_FILE_SHA = 0U,
  AFTER_CATALOG_PARSE = 1U,
  AFTER_TRUSTED_SCOPE = 2U,
  AFTER_EXACT_REGISTRY = 3U,
  BEFORE_RUNTIME_AUTHORITY_PUBLICATION = 4U,
  AFTER_CURRENT_SCRIPT_CLOSURE = 5U,
  AFTER_SEMANTIC_IDENTITY = 6U,
  AFTER_NATIVE_RECEIPT_AND_DIGEST = 7U,
  BEFORE_MATERIAL_ADMISSION_PUBLICATION = 8U,
};
#endif

namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t element = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, element);
}

void MaybeInject(
    Ogre14LegacyMaterialSemanticRuntimeAdmissionStage stage,
    IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *injector) {
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  if (injector != nullptr) {
    injector->BeforeOgre14LegacyMaterialSemanticRuntimeAdmissionStage(stage);
  }
#else
  (void)stage;
  (void)injector;
#endif
}

bool IsZeroSha(const Ogre14LegacySha256 &sha) noexcept {
  return std::all_of(sha.begin(), sha.end(),
                     [](std::uint8_t byte) { return byte == 0U; });
}

bool IsSupportedRepairState(
    Ogre14MaterialScriptRepairState state) noexcept {
  return state == Ogre14MaterialScriptRepairState::NONE ||
         state == Ogre14MaterialScriptRepairState::APPLIED;
}

bool ComputeSha256(const std::vector<std::uint8_t> &bytes,
                   Ogre14LegacySha256 &output) noexcept {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0U;
  const void *data = bytes.empty() ? static_cast<const void *>("")
                                   : static_cast<const void *>(bytes.data());
  if (EVP_Digest(data, bytes.size(), digest.data(), &digest_size,
                 EVP_sha256(), nullptr) != 1 ||
      digest_size != output.size()) {
    return false;
  }
  std::copy_n(digest.begin(), output.size(), output.begin());
  return true;
}

std::string ShaHex(const Ogre14LegacySha256 &sha) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output(sha.size() * 2U, '0');
  for (std::size_t index = 0U; index < sha.size(); ++index) {
    output[index * 2U] = kHex[sha[index] >> 4U];
    output[index * 2U + 1U] = kHex[sha[index] & 0x0fU];
  }
  return output;
}

std::uint32_t FloatBits(float value) noexcept {
  static_assert(sizeof(float) == sizeof(std::uint32_t));
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float FloatFromBits(std::uint32_t bits) noexcept {
  static_assert(sizeof(float) == sizeof(std::uint32_t));
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

Ogre14LegacyPipelineStateInput ProjectCatalogPass(
    const Ogre14LegacyCatalogPassFacts &pass) noexcept {
  Ogre14LegacyPipelineStateInput projected;
  projected.source_color = pass.source_color;
  projected.destination_color = pass.destination_color;
  projected.source_alpha = pass.source_alpha;
  projected.destination_alpha = pass.destination_alpha;
  projected.color_operation = pass.color_operation;
  projected.alpha_operation = pass.alpha_operation;
  projected.color_write_mask = pass.color_write_mask;
  projected.depth_check_enabled = pass.depth_check_enabled;
  projected.depth_write_enabled = pass.depth_write_enabled;
  projected.depth_compare = pass.depth_compare;
  projected.constant_depth_bias =
      FloatFromBits(pass.constant_depth_bias_f32_bits);
  projected.slope_scale_depth_bias =
      FloatFromBits(pass.slope_scale_depth_bias_f32_bits);
  projected.iteration_depth_bias =
      FloatFromBits(pass.iteration_depth_bias_f32_bits);
  projected.cull = pass.cull;
  projected.manual_cull = pass.manual_cull;
  projected.alpha_reject = pass.alpha_reject;
  projected.alpha_reject_value = pass.alpha_reject_value;
  projected.alpha_to_coverage = pass.alpha_to_coverage;
  projected.solid_fill = pass.solid_fill;
  projected.pass_iteration_count = pass.pass_iteration_count;
  return projected;
}

Ogre14LegacySamplerInput ProjectCatalogSampler(
    const Ogre14LegacyCatalogSamplerFacts &sampler,
    std::uint64_t source_revision) noexcept {
  Ogre14LegacySamplerInput projected;
  projected.source_revision = source_revision;
  projected.minification = sampler.minification;
  projected.magnification = sampler.magnification;
  projected.mip = sampler.mip;
  projected.address_u = sampler.address_u;
  projected.address_v = sampler.address_v;
  projected.address_w = sampler.address_w;
  projected.mip_lod_bias = FloatFromBits(sampler.mip_lod_bias_f32_bits);
  projected.minimum_lod = FloatFromBits(sampler.minimum_lod_f32_bits);
  projected.maximum_lod = FloatFromBits(sampler.maximum_lod_f32_bits);
  projected.maximum_anisotropy = sampler.maximum_anisotropy;
  projected.compare_enabled = sampler.compare_enabled;
  projected.compare_operation = sampler.compare_operation;
  projected.border_color = {
      FloatFromBits(sampler.border_color_f32_bits[0U]),
      FloatFromBits(sampler.border_color_f32_bits[1U]),
      FloatFromBits(sampler.border_color_f32_bits[2U]),
      FloatFromBits(sampler.border_color_f32_bits[3U])};
  return projected;
}

bool IdentityUv(const std::array<std::uint32_t, 9U> &bits) noexcept;
bool CanonicalCombine(const Ogre14LegacyCatalogCombineFacts &combine) noexcept;

Ogre14LegacyMaterialInput ProjectInitialCatalogMaterial(
    const Ogre14LegacyMaterialSemanticCatalogV2Record &record) {
  Ogre14LegacyMaterialInput projected;
  projected.key = record.material_key;
  projected.source_revision = record.declaration_revision;
  projected.base_color_semantic = record.base_color_semantic;
  projected.lighting_enabled =
      record.base_color_semantic != Ogre14LegacyBaseColorSemantic::UNLIT;
  projected.pipeline = ProjectCatalogPass(record.pass);
  if (!record.texture_units.empty()) {
    const Ogre14LegacyCatalogTextureUnitFacts &catalog_unit =
        record.texture_units.front();
    Ogre14LegacyTextureUnitInput unit;
    unit.exact_unit_name = catalog_unit.exact_unit_name;
    unit.texture_key = catalog_unit.texture_key;
    unit.texture_coordinate_set = catalog_unit.texture_coordinate_set;
    unit.projective = catalog_unit.projective;
    unit.canonical_color_modulate = CanonicalCombine(catalog_unit.combine);
    unit.canonical_alpha_modulate = unit.canonical_color_modulate;
    unit.identity_texture_transform =
        IdentityUv(catalog_unit.uv_transform_f32_bits);
    unit.sampler =
        ProjectCatalogSampler(catalog_unit.sampler, record.declaration_revision);
    projected.texture_units.push_back(std::move(unit));
  }
  return projected;
}

bool KeyLess(const Ogre14LegacyAssetKey &lhs,
             const Ogre14LegacyAssetKey &rhs) noexcept {
  if (lhs.exact_resource_group != rhs.exact_resource_group) {
    return lhs.exact_resource_group < rhs.exact_resource_group;
  }
  return lhs.exact_name < rhs.exact_name;
}

bool SamePass(const Ogre14LegacyCatalogPassFacts &catalog,
              const Ogre14LegacyPipelineStateInput &native) noexcept {
  return catalog.source_color == native.source_color &&
         catalog.destination_color == native.destination_color &&
         catalog.source_alpha == native.source_alpha &&
         catalog.destination_alpha == native.destination_alpha &&
         catalog.color_operation == native.color_operation &&
         catalog.alpha_operation == native.alpha_operation &&
         catalog.color_write_mask == native.color_write_mask &&
         catalog.depth_check_enabled == native.depth_check_enabled &&
         catalog.depth_write_enabled == native.depth_write_enabled &&
         catalog.depth_compare == native.depth_compare &&
         catalog.constant_depth_bias_f32_bits ==
             FloatBits(native.constant_depth_bias) &&
         catalog.slope_scale_depth_bias_f32_bits ==
             FloatBits(native.slope_scale_depth_bias) &&
         catalog.iteration_depth_bias_f32_bits ==
             FloatBits(native.iteration_depth_bias) &&
         catalog.cull == native.cull &&
         catalog.manual_cull == native.manual_cull &&
         catalog.alpha_reject == native.alpha_reject &&
         catalog.alpha_reject_value == native.alpha_reject_value &&
         catalog.alpha_to_coverage == native.alpha_to_coverage &&
         catalog.solid_fill == native.solid_fill &&
         catalog.pass_iteration_count == native.pass_iteration_count;
}

bool SameSampler(const Ogre14LegacyCatalogSamplerFacts &catalog,
                 const Ogre14LegacySamplerInput &native) noexcept {
  return catalog.minification == native.minification &&
         catalog.magnification == native.magnification &&
         catalog.mip == native.mip && catalog.address_u == native.address_u &&
         catalog.address_v == native.address_v &&
         catalog.address_w == native.address_w &&
         catalog.mip_lod_bias_f32_bits == FloatBits(native.mip_lod_bias) &&
         catalog.minimum_lod_f32_bits == FloatBits(native.minimum_lod) &&
         catalog.maximum_lod_f32_bits == FloatBits(native.maximum_lod) &&
         catalog.maximum_anisotropy == native.maximum_anisotropy &&
         catalog.compare_enabled == native.compare_enabled &&
         catalog.compare_operation == native.compare_operation &&
         catalog.border_color_f32_bits[0U] ==
             FloatBits(native.border_color.x) &&
         catalog.border_color_f32_bits[1U] ==
             FloatBits(native.border_color.y) &&
         catalog.border_color_f32_bits[2U] ==
             FloatBits(native.border_color.z) &&
         catalog.border_color_f32_bits[3U] ==
             FloatBits(native.border_color.w);
}

bool IdentityUv(const std::array<std::uint32_t, 9U> &bits) noexcept {
  static constexpr std::array<std::uint32_t, 9U> kIdentity{{
      0x3f800000U, 0U, 0U, 0U, 0x3f800000U, 0U, 0U, 0U,
      0x3f800000U}};
  return bits == kIdentity;
}

bool IdentitySwizzle(
    const std::array<Ogre14LegacyTextureSwizzle, 4U> &swizzle) noexcept {
  static constexpr std::array<Ogre14LegacyTextureSwizzle, 4U> kIdentity{{
      Ogre14LegacyTextureSwizzle::RED,
      Ogre14LegacyTextureSwizzle::GREEN,
      Ogre14LegacyTextureSwizzle::BLUE,
      Ogre14LegacyTextureSwizzle::ALPHA}};
  return swizzle == kIdentity;
}

bool CanonicalCombine(
    const Ogre14LegacyCatalogCombineFacts &combine) noexcept {
  return combine.color_operation ==
             Ogre14LegacyTextureCombineOperation::MODULATE &&
         combine.color_source_one ==
             Ogre14LegacyTextureCombineSource::TEXTURE &&
         combine.color_source_two ==
             Ogre14LegacyTextureCombineSource::CURRENT &&
         combine.alpha_operation ==
             Ogre14LegacyTextureCombineOperation::MODULATE &&
         combine.alpha_source_one ==
             Ogre14LegacyTextureCombineSource::TEXTURE &&
         combine.alpha_source_two ==
             Ogre14LegacyTextureCombineSource::CURRENT &&
         combine.color_manual_one_f32_bits ==
             std::array<std::uint32_t, 4U>{} &&
         combine.color_manual_two_f32_bits ==
             std::array<std::uint32_t, 4U>{} &&
         combine.color_manual_factor_f32_bits == 0U &&
         combine.alpha_manual_one_f32_bits == 0U &&
         combine.alpha_manual_two_f32_bits == 0U &&
         combine.alpha_manual_factor_f32_bits == 0U;
}

ValidationResult ValidateInitialRecordSurface(
    const Ogre14LegacyMaterialSemanticCatalogV2Record &record) {
  if (record.runtime_generation !=
          Ogre14LegacyMaterialRuntimeGeneration::AUTHORED &&
      record.runtime_generation !=
          Ogre14LegacyMaterialRuntimeGeneration::REPAIRED_SCRIPT) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.record.runtime_generation",
                   "generated and listener material generations are not admitted");
  }
  if (record.environment_augmentation !=
          Ogre14LegacyEnvironmentAugmentation::NONE ||
      record.shadow_augmentation != Ogre14LegacyShadowAugmentation::NONE ||
      record.shadow_technique != Ogre14LegacyShadowTechnique::NONE) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.record.augmentation",
                   "environment and shadow augmentation are not admitted");
  }
  if (record.selected_scheme != "Default" || record.selected_lod != 0U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.record.technique_selection",
                   "initial admission requires the default scheme and LOD zero");
  }
  if (record.exact_lowering_algorithm !=
          "ror.ogre14.explicit-fixed-function" ||
      record.lowering_version != 2U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.record.lowering",
                   "lowering algorithm/version is not in the initial reviewed set");
  }
  if (record.texture_units.size() > 1U) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.record.texture_units",
                   "initial admission accepts at most one base-color unit");
  }
  if (record.registry_texture_color_role !=
      Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "semantic_runtime.record.registry_texture_color_role",
        "initial admission requires canonical BASE_COLOR_SRGB");
  }
  if (!record.texture_units.empty()) {
    const auto &unit = record.texture_units.front();
    if (unit.ordinal != 0U ||
        unit.semantic != Ogre14LegacyTextureSemantic::BASE_COLOR ||
        unit.color_role != record.registry_texture_color_role ||
        !IdentitySwizzle(unit.swizzle) || unit.texture_coordinate_set != 0U ||
        unit.projective || !IdentityUv(unit.uv_transform_f32_bits) ||
        !CanonicalCombine(unit.combine)) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "semantic_runtime.record.texture_projection",
                     "texture semantic, swizzle, UV, or combine state is outside the initial set");
    }
  }
  // Authentication must reject the same pass and sampler surfaces that the
  // renderer-neutral translator rejects. Deferring this exact projection to
  // live capture would publish authority for a catalog that can never prepare.
  return ValidateOgre14LegacyMaterialInput(
      ProjectInitialCatalogMaterial(record));
}

} // namespace

struct Ogre14LegacyMaterialSemanticApprovedManifest::State final {
  std::uint32_t version =
      kOgre14LegacyMaterialSemanticApprovedManifestVersion;
  std::string domain = kOgre14LegacyReviewedPackageRevisionV1Domain;
  Ogre14LegacyMaterialSemanticApprovedManifestDescription description;
};

struct Ogre14LegacyMaterialSemanticRuntimeAuthority::State final {
  std::uint32_t version =
      kOgre14LegacyMaterialSemanticRuntimeAuthorityVersion;
  Ogre14LegacyMaterialSemanticApprovedManifest approved_manifest;
  Ogre14LegacyMaterialSemanticCatalogV2 catalog;
  Ogre14LegacyMaterialSemanticRegistry registry;
};

struct Ogre14LegacyMaterialSemanticAdmission::State final {
  std::uint32_t version = kOgre14LegacyMaterialSemanticAdmissionVersion;
  std::shared_ptr<const Ogre14LegacyMaterialSemanticRuntimeAuthority::State>
      runtime_authority;
  Ogre14LegacyAssetKey material_key;
  std::uint64_t reviewed_resource_generation = 0U;
  std::uint64_t runtime_group_generation = 0U;
  Ogre14AuthenticatedMaterialScriptResolution script_resolution;
  Ogre14LegacyMaterialSemanticResolution semantic_resolution;
  Ogre14LegacyNativeMaterialCapture native_capture;
};

Ogre14LegacyMaterialSemanticApprovedManifest::
    Ogre14LegacyMaterialSemanticApprovedManifest(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14LegacyMaterialSemanticApprovedManifest::initialized() const
    noexcept {
  return state_ &&
         state_->version ==
             kOgre14LegacyMaterialSemanticApprovedManifestVersion &&
         state_->domain == kOgre14LegacyReviewedPackageRevisionV1Domain;
}

std::uint32_t
Ogre14LegacyMaterialSemanticApprovedManifest::version() const noexcept {
  return state_ ? state_->version : 0U;
}

const char *Ogre14LegacyMaterialSemanticApprovedManifest::domain() const
    noexcept {
  return initialized() ? state_->domain.c_str() : nullptr;
}

std::uint64_t
Ogre14LegacyMaterialSemanticApprovedManifest::reviewed_resource_generation()
    const noexcept {
  return initialized() ? state_->description.reviewed_resource_generation : 0U;
}

bool Ogre14LegacyMaterialSemanticApprovedManifest::SharesImmutableStateWith(
    const Ogre14LegacyMaterialSemanticApprovedManifest &other) const noexcept {
  return state_ && state_ == other.state_;
}

ValidationResult
Ogre14LegacyMaterialSemanticApprovedManifest::MintFromTrustedDescription(
    const Ogre14LegacyMaterialSemanticApprovedManifestDescription &description,
    Ogre14LegacyMaterialSemanticApprovedManifest &output) {
  try {
    if (IsZeroSha(description.catalog_full_file_sha256) ||
        IsZeroSha(description.package_archive_sha256) ||
        description.exact_resource_group.empty() ||
        description.exact_resource_group.find('\0') != std::string::npos ||
        description.reviewed_resource_generation == 0U ||
        description.reviewed_resource_generation ==
            (std::numeric_limits<std::uint64_t>::max)() ||
        description.material_closures.empty()) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "semantic_runtime.approved_manifest",
                     "trusted manifest description is incomplete");
    }
    Ogre14LegacyAssetKey previous;
    bool have_previous = false;
    for (std::size_t index = 0U;
         index < description.material_closures.size(); ++index) {
      const auto &closure = description.material_closures[index];
      std::string stable_key;
      ValidationResult key_validation = BuildOgre14LegacyStableAssetKey(
          RenderAssetKind::MATERIAL, closure.material_key, stable_key);
      if (!key_validation ||
          closure.material_key.exact_resource_group !=
              description.exact_resource_group ||
          closure.sources.empty() ||
          closure.primary_source_index >= closure.sources.size() ||
          (have_previous && !KeyLess(previous, closure.material_key))) {
        return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                       "semantic_runtime.approved_manifest.material_closure",
                       "material closures are invalid, duplicated, or noncanonical",
                       index);
      }
      for (std::size_t source_index = 0U;
           source_index < closure.sources.size(); ++source_index) {
        const auto &source = closure.sources[source_index];
        const Ogre14MaterialScriptSourceRole expected_role =
            source_index == 0U
                ? Ogre14MaterialScriptSourceRole::ROOT_SCRIPT
                : Ogre14MaterialScriptSourceRole::COMPILER_DEPENDENCY;
        if (source.exact_member_name.empty() ||
            source.exact_member_name.find('\0') != std::string::npos ||
            source.source_role != expected_role ||
            IsZeroSha(source.original_sha256) ||
            IsZeroSha(source.effective_sha256) ||
            !IsSupportedRepairState(source.repair_state) ||
            source.repair_plan_version !=
                kOgre14AuthenticatedMaterialScriptRepairPlanVersion ||
            IsZeroSha(source.repair_plan_sha256)) {
          return Failure(ValidationCode::INVALID_IDENTIFIER,
                         "semantic_runtime.approved_manifest.source",
                         "approved source closure entry is incomplete", index);
        }
      }
      if (closure.sources.front().source_role !=
          Ogre14MaterialScriptSourceRole::ROOT_SCRIPT) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "semantic_runtime.approved_manifest.primary_source",
                       "closure must put one root script first and dependencies after it",
                       index);
      }
      for (std::size_t unit = 0U; unit < closure.texture_sources.size();
           ++unit) {
        const auto &texture = closure.texture_sources[unit];
        if (texture.ordinal != unit ||
            texture.source_kind !=
                Ogre14AuthenticatedTextureSourceKind::
                    AUTHENTICATED_ARCHIVE_MEMBER ||
            texture.exact_member_name.empty() ||
            texture.exact_member_name.find('\0') != std::string::npos) {
          return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                         "semantic_runtime.approved_manifest.texture_source",
                         "initial approval accepts canonical archive texture sources only",
                         index);
        }
      }
      previous = closure.material_key;
      have_previous = true;
    }
    auto state = std::make_shared<State>();
    state->description = description;
    Ogre14LegacyMaterialSemanticApprovedManifest candidate(std::move(state));
    output = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_runtime.approved_manifest.allocation",
                   "allocation failed while retaining trusted manifest data");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.approved_manifest.exception",
                   "unexpected trusted manifest construction failure");
  }
}

Ogre14LegacyMaterialSemanticRuntimeAuthority::
    Ogre14LegacyMaterialSemanticRuntimeAuthority(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14LegacyMaterialSemanticRuntimeAuthority::initialized() const
    noexcept {
  return state_ &&
         state_->version ==
             kOgre14LegacyMaterialSemanticRuntimeAuthorityVersion &&
         state_->approved_manifest.initialized() && state_->catalog.initialized() &&
         state_->registry.initialized();
}

std::uint32_t
Ogre14LegacyMaterialSemanticRuntimeAuthority::version() const noexcept {
  return state_ ? state_->version : 0U;
}

std::size_t Ogre14LegacyMaterialSemanticRuntimeAuthority::size() const
    noexcept {
  return initialized() ? state_->catalog.size() : 0U;
}

std::uint64_t
Ogre14LegacyMaterialSemanticRuntimeAuthority::reviewed_resource_generation()
    const noexcept {
  return initialized()
             ? state_->approved_manifest.reviewed_resource_generation()
             : 0U;
}

const Ogre14LegacyMaterialSemanticRegistry *
Ogre14LegacyMaterialSemanticRuntimeAuthority::semantic_registry() const
    noexcept {
  return initialized() ? &state_->registry : nullptr;
}

ValidationResult
Ogre14LegacyMaterialSemanticRuntimeAuthority::ResolveMaterialSemantics(
    const Ogre14LegacyAssetKey &material_key,
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    Ogre14LegacyMaterialSemanticResolution &output) const {
  if (!initialized()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "semantic_runtime.authority",
                   "semantic runtime authority is not initialized");
  }
  return state_->registry.Resolve(material_key, translator_configuration,
                                  output);
}

bool Ogre14LegacyMaterialSemanticRuntimeAuthority::SharesImmutableStateWith(
    const Ogre14LegacyMaterialSemanticRuntimeAuthority &other) const noexcept {
  return state_ && state_ == other.state_;
}

Ogre14LegacyMaterialSemanticAdmission::
    Ogre14LegacyMaterialSemanticAdmission(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14LegacyMaterialSemanticAdmission::initialized() const noexcept {
  return state_ &&
         state_->version == kOgre14LegacyMaterialSemanticAdmissionVersion &&
         state_->runtime_authority && state_->reviewed_resource_generation != 0U &&
         state_->runtime_group_generation != 0U &&
         state_->script_resolution.initialized() &&
         state_->semantic_resolution.declaration_identity.has_value() &&
         state_->native_capture.native_material_audit_receipt.has_value();
}

std::uint32_t Ogre14LegacyMaterialSemanticAdmission::version() const noexcept {
  return state_ ? state_->version : 0U;
}

const Ogre14LegacyAssetKey *
Ogre14LegacyMaterialSemanticAdmission::material_key() const noexcept {
  return initialized() ? &state_->material_key : nullptr;
}

std::uint64_t
Ogre14LegacyMaterialSemanticAdmission::reviewed_resource_generation() const
    noexcept {
  return initialized() ? state_->reviewed_resource_generation : 0U;
}

std::uint64_t
Ogre14LegacyMaterialSemanticAdmission::runtime_group_generation() const
    noexcept {
  return initialized() ? state_->runtime_group_generation : 0U;
}

const Ogre14LegacyMaterialSemanticResolution *
Ogre14LegacyMaterialSemanticAdmission::semantic_resolution() const noexcept {
  return initialized() ? &state_->semantic_resolution : nullptr;
}

const Ogre14LegacyNativeMaterialCapture *
Ogre14LegacyMaterialSemanticAdmission::native_capture() const noexcept {
  return initialized() ? &state_->native_capture : nullptr;
}

bool Ogre14LegacyMaterialSemanticAdmission::SharesImmutableStateWith(
    const Ogre14LegacyMaterialSemanticAdmission &other) const noexcept {
  return state_ && state_ == other.state_;
}

ValidationResult AuthenticateOgre14LegacyMaterialSemanticRuntime(
    const Ogre14LegacyMaterialSemanticApprovedManifest &approved_manifest,
    const Ogre14LegacyMaterialSemanticCatalogV2Configuration
        &catalog_configuration,
    const Ogre14LegacyMaterialSemanticRegistryConfiguration
        &registry_configuration,
    const std::vector<std::uint8_t> &catalog_file_bytes,
    Ogre14LegacyMaterialSemanticRuntimeAuthority &output
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
    ,
    IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector
        *fault_injector
#endif
    ) {
#if !defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *fault_injector =
      nullptr;
#endif
  try {
    if (!approved_manifest.initialized() || !approved_manifest.state_) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "semantic_runtime.approved_manifest",
                     "opaque approved manifest authority is missing");
    }
    Ogre14LegacySha256 full_file_sha{};
    if (!ComputeSha256(catalog_file_bytes, full_file_sha) ||
        full_file_sha !=
            approved_manifest.state_->description.catalog_full_file_sha256) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "semantic_runtime.catalog_full_file_sha256",
                     "exact RORMAT2 bytes do not match the independently approved full-file SHA-256");
    }
    MaybeInject(
        Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::
            AFTER_EXTERNAL_FULL_FILE_SHA,
        fault_injector);

    Ogre14LegacyMaterialSemanticCatalogV2 catalog;
    ValidationResult result = ParseOgre14LegacyMaterialSemanticCatalogV2(
        catalog_configuration, catalog_file_bytes, catalog);
    if (!result) {
      return result;
    }
    MaybeInject(
        Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::AFTER_CATALOG_PARSE,
        fault_injector);

    const auto &description = approved_manifest.state_->description;
    if (catalog.size() != description.material_closures.size()) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "semantic_runtime.trusted_scope.material_count",
                     "catalog and approved closure inventories differ");
    }
    for (std::size_t index = 0U; index < description.material_closures.size();
         ++index) {
      const auto &closure = description.material_closures[index];
      const auto *record = catalog.FindExact(closure.material_key);
      if (record == nullptr ||
          record->package_archive_sha256 != description.package_archive_sha256 ||
          record->material_key.exact_resource_group !=
              description.exact_resource_group ||
          record->resource_generation !=
              description.reviewed_resource_generation) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "semantic_runtime.trusted_scope",
                       "catalog record escaped the approved package, group, or reviewed revision",
                       index);
      }
      if (record->repair_plan_version !=
          kOgre14AuthenticatedMaterialScriptRepairPlanVersion) {
        return Failure(
            ValidationCode::UNSUPPORTED_VERSION,
            "semantic_runtime.trusted_scope.repair_plan_version",
            "catalog repair-plan version is outside the authenticated live domain",
            index);
      }
      result = ValidateInitialRecordSurface(*record);
      if (!result) {
        result.element_index = index;
        return result;
      }
      if (record->texture_units.size() != closure.texture_sources.size()) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "semantic_runtime.trusted_scope.texture_sources",
                       "catalog texture units differ from approved source bindings",
                       index);
      }
      for (std::size_t unit = 0U; unit < closure.texture_sources.size();
           ++unit) {
        if (closure.texture_sources[unit].texture_key !=
            record->texture_units[unit].texture_key) {
          return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                         "semantic_runtime.trusted_scope.texture_key",
                         "approved texture source does not bind the catalog unit",
                         index);
        }
      }
      const auto &primary = closure.sources[closure.primary_source_index];
      const Ogre14MaterialScriptRepairState expected_repair =
          record->runtime_generation ==
                  Ogre14LegacyMaterialRuntimeGeneration::AUTHORED
              ? Ogre14MaterialScriptRepairState::NONE
              : Ogre14MaterialScriptRepairState::APPLIED;
      if (primary.exact_member_name != record->exact_source_script_member ||
          primary.original_sha256 != record->source_script_sha256 ||
          primary.effective_sha256 != record->effective_script_sha256 ||
          primary.repair_plan_version != record->repair_plan_version ||
          primary.repair_state != expected_repair) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "semantic_runtime.trusted_scope.primary_source",
                       "catalog primary source differs from the approved complete closure",
                       index);
      }
    }
    MaybeInject(
        Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::AFTER_TRUSTED_SCOPE,
        fault_injector);

    Ogre14LegacyMaterialSemanticRegistry registry;
    result = BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2(
        catalog, registry_configuration, registry);
    if (!result) {
      return result;
    }
    if (registry.size() != catalog.size()) {
      return Failure(ValidationCode::SIZE_MISMATCH,
                     "semantic_runtime.registry",
                     "semantic registry is not an exact catalog projection");
    }
    for (std::size_t index = 0U; index < description.material_closures.size();
         ++index) {
      const auto &key = description.material_closures[index].material_key;
      const auto *record = catalog.FindExact(key);
      Ogre14LegacyMaterialSemanticResolution resolution;
      result = registry.Resolve(key, Ogre14LegacyAssetTranslatorConfiguration{},
                                resolution);
      if (!result || record == nullptr ||
          resolution.source !=
              Ogre14LegacyMaterialSemanticSource::
                  VERSIONED_COMPATIBILITY_TABLE ||
          resolution.source_revision != record->declaration_revision ||
          resolution.native_declaration.base_color_semantic !=
              record->base_color_semantic ||
          resolution.native_declaration.texture_color_role !=
              record->registry_texture_color_role ||
          !resolution.declaration_identity.has_value()) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "semantic_runtime.registry.exact_projection",
                       "semantic registry differs from the reviewed catalog record",
                       index);
      }
    }
    MaybeInject(
        Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::AFTER_EXACT_REGISTRY,
        fault_injector);

    auto state = std::make_shared<
        Ogre14LegacyMaterialSemanticRuntimeAuthority::State>();
    state->approved_manifest = approved_manifest;
    state->catalog = std::move(catalog);
    state->registry = std::move(registry);
    MaybeInject(
        Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::
            BEFORE_RUNTIME_AUTHORITY_PUBLICATION,
        fault_injector);
    Ogre14LegacyMaterialSemanticRuntimeAuthority candidate(std::move(state));
    output = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_runtime.authentication.allocation",
                   "allocation failed before runtime authority publication");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "semantic_runtime.authentication.allocation",
                   "runtime authority allocation exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.authentication.exception",
                   "unexpected exception before runtime authority publication");
  }
}

ValidationResult Ogre14LegacyMaterialSemanticRuntimeAuthority::
    ValidateScriptAndSemanticPrerequisites(
        const Ogre14AuthenticatedMaterialScriptResolution &script_resolution,
        const Ogre14LegacyMaterialSemanticResolution &semantic_resolution,
        const Ogre14LegacyAssetKey &material_key,
        IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector
            *fault_injector) const {
  if (!initialized()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "semantic_runtime.authority",
                   "semantic runtime authority is not initialized");
  }
  const auto *receipt = script_resolution.receipt();
  const auto *binding = receipt != nullptr ? receipt->binding_metadata() : nullptr;
  const auto &description =
      state_->approved_manifest.state_->description;
  const auto closure = std::lower_bound(
      description.material_closures.begin(),
      description.material_closures.end(), material_key,
      [](const Ogre14LegacyApprovedMaterialScriptClosure &lhs,
         const Ogre14LegacyAssetKey &rhs) {
        return KeyLess(lhs.material_key, rhs);
      });
  if (receipt == nullptr || binding == nullptr ||
      closure == description.material_closures.end() ||
      closure->material_key != material_key ||
      binding->exact_group != material_key.exact_resource_group ||
      binding->exact_material_name != material_key.exact_name ||
      receipt->source_count() != closure->sources.size() ||
      receipt->primary_source_index() != closure->primary_source_index) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "semantic_runtime.script_closure",
                   "current material-script receipt does not match the approved material closure");
  }
  const std::string package_sha = ShaHex(description.package_archive_sha256);
  std::uint64_t runtime_group_generation = 0U;
  for (std::size_t index = 0U; index < closure->sources.size(); ++index) {
    const auto *metadata = receipt->source_metadata_at(index);
    const auto *archive = receipt->authenticated_archive_snapshot_at(index);
    const auto &approved = closure->sources[index];
    if (metadata == nullptr || archive == nullptr ||
        metadata->source_role != approved.source_role ||
        metadata->exact_member_name != approved.exact_member_name ||
        metadata->original_sha256 != ShaHex(approved.original_sha256) ||
        metadata->effective_sha256 != ShaHex(approved.effective_sha256) ||
        metadata->repair_state != approved.repair_state ||
        metadata->repair_plan_version != approved.repair_plan_version ||
        metadata->repair_plan_sha256 != ShaHex(approved.repair_plan_sha256) ||
        metadata->archive_sha256 != package_sha ||
        archive->archive_sha256() != package_sha ||
        metadata->effective_group != material_key.exact_resource_group ||
        metadata->group_generation == 0U ||
        (runtime_group_generation != 0U &&
         metadata->group_generation != runtime_group_generation)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "semantic_runtime.script_closure.source",
                     "current ordered script/import closure differs from approval",
                     index);
    }
    runtime_group_generation = metadata->group_generation;
  }
  MaybeInject(
      Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::
          AFTER_CURRENT_SCRIPT_CLOSURE,
      fault_injector);

  Ogre14LegacyMaterialSemanticResolution authoritative;
  ValidationResult resolved = state_->registry.Resolve(
      material_key, semantic_resolution.native_declaration.translator_configuration,
      authoritative);
  if (!resolved ||
      !Ogre14LegacyMaterialSemanticResolutionAuthenticates(
          semantic_resolution, authoritative)) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "semantic_runtime.semantic_identity",
                   "semantic resolution is stale, foreign, forged, or reboxed");
  }
  MaybeInject(
      Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::
          AFTER_SEMANTIC_IDENTITY,
      fault_injector);
  return ValidationResult::Success();
}

ValidationResult
Ogre14LegacyMaterialSemanticRuntimeAuthority::ValidateNativeCapture(
    const Ogre14LegacyAssetKey &material_key,
    std::uint64_t runtime_group_generation,
    const Ogre14LegacyNativeMaterialCapture &capture,
    IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector
        *fault_injector) const {
  const auto *record = initialized() ? state_->catalog.FindExact(material_key)
                                     : nullptr;
  if (record == nullptr || capture.material.key != material_key ||
      !capture.native_material_audit_receipt.Authenticates(capture) ||
      capture.native_material_declaration_serialization_version !=
          kOgre14LegacyNativeMaterialDeclarationSerializationVersion ||
      capture.native_material_declaration_sha256 !=
          record->native_structure_sha256) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "semantic_runtime.native_receipt_digest",
                   "extractor-owned native receipt or reviewed structure digest does not authenticate");
  }
  if (capture.material.base_color_semantic != record->base_color_semantic ||
      !SamePass(record->pass, capture.material.pipeline) ||
      capture.material.texture_units.size() != record->texture_units.size() ||
      capture.textures.size() != record->texture_units.size() ||
      capture.authenticated_texture_resolutions.size() !=
          record->texture_units.size()) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "semantic_runtime.native_projection",
                   "native material/pass/texture projection differs from catalog");
  }

  const auto &description = state_->approved_manifest.state_->description;
  const auto closure = std::lower_bound(
      description.material_closures.begin(),
      description.material_closures.end(), material_key,
      [](const Ogre14LegacyApprovedMaterialScriptClosure &lhs,
         const Ogre14LegacyAssetKey &rhs) {
        return KeyLess(lhs.material_key, rhs);
      });
  for (std::size_t index = 0U; index < record->texture_units.size(); ++index) {
    const auto &catalog_unit = record->texture_units[index];
    const auto &native_unit = capture.material.texture_units[index];
    const auto &native_texture = capture.textures[index];
    const auto *source = capture.authenticated_texture_resolutions[index]
                             .source_receipt();
    const auto *metadata = source != nullptr ? source->metadata() : nullptr;
    if (closure == description.material_closures.end() ||
        index >= closure->texture_sources.size() || metadata == nullptr ||
        catalog_unit.exact_unit_name != native_unit.exact_unit_name ||
        catalog_unit.texture_key != native_unit.texture_key ||
        catalog_unit.texture_key != native_texture.key ||
        !SameSampler(catalog_unit.sampler, native_unit.sampler) ||
        native_unit.texture_coordinate_set !=
            catalog_unit.texture_coordinate_set ||
        native_unit.projective != catalog_unit.projective ||
        !native_unit.identity_texture_transform ||
        !native_unit.canonical_color_modulate ||
        !native_unit.canonical_alpha_modulate ||
        native_texture.color_role != catalog_unit.color_role ||
        metadata->source.source_kind !=
            Ogre14AuthenticatedTextureSourceKind::
                AUTHENTICATED_ARCHIVE_MEMBER ||
        metadata->source.source_kind !=
            closure->texture_sources[index].source_kind ||
        metadata->source.exact_member_name !=
            closure->texture_sources[index].exact_member_name ||
        metadata->source.archive_sha256 !=
            ShaHex(description.package_archive_sha256) ||
        metadata->source.group_generation != runtime_group_generation ||
        metadata->source.effective_resource_group !=
            catalog_unit.texture_key.exact_resource_group ||
        metadata->source.binding.exact_resource_name !=
            catalog_unit.texture_key.exact_name) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "semantic_runtime.native_texture_projection",
                     "native sampler/texture/source authority differs from catalog approval",
                     index);
    }
  }
  MaybeInject(
      Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::
          AFTER_NATIVE_RECEIPT_AND_DIGEST,
      fault_injector);
  return ValidationResult::Success();
}

ValidationResult Ogre14LegacyMaterialSemanticRuntimeAuthority::
    PublishAdmission(
        const Ogre14AuthenticatedMaterialScriptResolution &script_resolution,
        const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot
            &script_authority,
        const Ogre14LegacyMaterialSemanticResolution &semantic_resolution,
        Ogre14LegacyNativeMaterialCapture native_capture,
        Ogre14LegacyMaterialSemanticAdmission &output,
        IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector
            *fault_injector) const {
  try {
    const Ogre14LegacyAssetKey material_key = native_capture.material.key;
    const auto *receipt = script_resolution.receipt();
    const auto *source = receipt != nullptr ? receipt->source_metadata() : nullptr;
    if (source == nullptr || source->group_generation == 0U) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "semantic_runtime.runtime_group_generation",
                     "current script resolution has no runtime group generation");
    }
    if (!script_authority.Authenticates(script_resolution)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "semantic_runtime.final_script_authority",
                     "material-script registry changed before admission publication");
    }
    Ogre14LegacyMaterialSemanticResolution final_semantics;
    ValidationResult validation = state_->registry.Resolve(
        material_key,
        semantic_resolution.native_declaration.translator_configuration,
        final_semantics);
    if (!validation ||
        !Ogre14LegacyMaterialSemanticResolutionAuthenticates(
            semantic_resolution, final_semantics)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "semantic_runtime.final_semantic_authority",
                     "semantic declaration changed before admission publication");
    }
    auto state = std::make_shared<Ogre14LegacyMaterialSemanticAdmission::State>();
    state->runtime_authority = state_;
    state->material_key = material_key;
    state->reviewed_resource_generation = reviewed_resource_generation();
    state->runtime_group_generation = source->group_generation;
    state->script_resolution = script_resolution;
    state->semantic_resolution = semantic_resolution;
    state->native_capture = std::move(native_capture);
    MaybeInject(
        Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::
            BEFORE_MATERIAL_ADMISSION_PUBLICATION,
        fault_injector);
    Ogre14LegacyMaterialSemanticAdmission candidate(std::move(state));
    output = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_runtime.admission.allocation",
                   "allocation failed before material admission publication");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "semantic_runtime.admission.allocation",
                   "material admission allocation exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.admission.exception",
                   "unexpected exception before material admission publication");
  }
}

bool Ogre14LegacyMaterialSemanticRuntimeAuthority::AuthenticatesAdmission(
    const Ogre14LegacyMaterialSemanticAdmission &admission,
    const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot
        &script_authority,
    const Ogre14AuthenticatedTextureAuthoritySnapshot
        &texture_authority) const noexcept {
  if (!initialized() || !admission.initialized() || !admission.state_ ||
      admission.state_->runtime_authority != state_ ||
      admission.state_->reviewed_resource_generation !=
          reviewed_resource_generation() ||
      !script_authority.Authenticates(admission.state_->script_resolution) ||
      !texture_authority.initialized() ||
      !admission.state_->native_capture.native_material_audit_receipt
           .Authenticates(admission.state_->native_capture)) {
    return false;
  }
  for (const auto &resolution :
       admission.state_->native_capture.authenticated_texture_resolutions) {
    if (!texture_authority.Authenticates(resolution)) {
      return false;
    }
  }
  return true;
}

ValidationResult
Ogre14LegacyMaterialSemanticRuntimeAuthority::RevalidateAdmission(
    const Ogre14LegacyMaterialSemanticAdmission &admission,
    const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot
        &script_authority,
    const Ogre14AuthenticatedTextureAuthoritySnapshot
        &texture_authority) const {
  if (!AuthenticatesAdmission(admission, script_authority,
                              texture_authority)) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "semantic_runtime.admission.authority",
                   "admission is stale, foreign, reboxed, or no longer current");
  }
  Ogre14LegacyMaterialSemanticResolution current;
  ValidationResult validation = state_->registry.Resolve(
      admission.state_->material_key,
      admission.state_->semantic_resolution.native_declaration
          .translator_configuration,
      current);
  if (!validation ||
      !Ogre14LegacyMaterialSemanticResolutionAuthenticates(
          admission.state_->semantic_resolution, current)) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "semantic_runtime.admission.semantic_identity",
                   "admission semantic identity is no longer authoritative");
  }
  return ValidateNativeCapture(admission.state_->material_key,
                               admission.state_->runtime_group_generation,
                               admission.state_->native_capture, nullptr);
}

static_assert(std::is_nothrow_copy_constructible_v<
              Ogre14LegacyMaterialSemanticApprovedManifest>);
static_assert(std::is_nothrow_move_assignable_v<
              Ogre14LegacyMaterialSemanticRuntimeAuthority>);
static_assert(std::is_nothrow_copy_constructible_v<
              Ogre14LegacyMaterialSemanticAdmission>);

} // namespace RoR::Render
