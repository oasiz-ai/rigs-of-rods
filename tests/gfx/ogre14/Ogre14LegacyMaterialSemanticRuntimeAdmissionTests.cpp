/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.h"
#include "resources/LegacyMaterialScriptSanitizer.h"
#include "resources/terrn2_fileformat/TerrainBundleArchiveVerifier.h"

#include <openssl/evp.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef ROR_OGRE14_MATERIAL_SEMANTIC_RUNTIME_ADMISSION_FIXTURE
#error "compiled synthetic runtime-admission fixture path is required"
#endif

namespace RoR::Render::Testing {

class Ogre14AuthenticatedMaterialScriptTestAccess final {
public:
  static ValidationResult Initialize(
      const Ogre14AuthenticatedMaterialScriptRegistryConfiguration &config,
      Ogre14AuthenticatedMaterialScriptRegistry &registry) {
    return registry.Initialize(config);
  }
  static ValidationResult Advance(
      Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const std::string &group, std::uint64_t generation) {
    return registry.AdvanceGroupGeneration(group, generation);
  }
  static ValidationResult Commit(
      Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      const std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> &sources,
      const std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput>
          &materials) {
    return registry.CommitWholeGroup(group, generation, sources, materials);
  }
  static ValidationResult Mint(
      const Ogre14AuthenticatedMaterialScriptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      std::uintptr_t pointer_token, std::uint64_t handle,
      const std::string &name, const std::string &origin,
      std::uintptr_t resolver_token,
      Ogre14AuthenticatedMaterialScriptResolution &resolution) {
    return registry.MintResolution(group, generation, pointer_token, handle,
                                   name, origin, resolver_token, resolution);
  }
  static ValidationResult Snapshot(
      const Ogre14AuthenticatedMaterialScriptRegistry &registry,
      std::uintptr_t resolver_token,
      Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &snapshot) {
    return registry.MintResolverAuthoritySnapshot(resolver_token, snapshot);
  }
};

class Ogre14AuthenticatedTextureResolutionTestAccess final {
public:
  static ValidationResult Initialize(
      Ogre14AuthenticatedTextureReceiptRegistry &registry) {
    return InitializeOgre14AuthenticatedTextureReceiptRegistry({}, registry);
  }
  static ValidationResult Snapshot(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      std::uintptr_t resolver_token,
      Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) {
    return registry.MintResolverAuthoritySnapshot(resolver_token, snapshot);
  }
  static ValidationResult Mint(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      std::uintptr_t pointer_token, std::uint64_t handle,
      const std::string &name, std::uint64_t loaded_state_count,
      std::uintptr_t resolver_token,
      Ogre14AuthenticatedTextureResolution &resolution) {
    return registry.MintLoadedResourceResolution(
        group, generation, pointer_token, handle, name, loaded_state_count,
        resolver_token, resolution);
  }
};

class Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess final {
public:
  static ValidationResult MintApprovedManifest(
      const Ogre14LegacyMaterialSemanticApprovedManifestDescription
          &description,
      Ogre14LegacyMaterialSemanticApprovedManifest &manifest) {
    return Ogre14LegacyMaterialSemanticApprovedManifest::
        MintFromTrustedDescription(description, manifest);
  }

  static ValidationResult AdmitSynthetic(
      const Ogre14LegacyMaterialSemanticRuntimeAuthority &authority,
      const Ogre14AuthenticatedMaterialScriptResolution &script_resolution,
      const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot
          &script_authority,
      const Ogre14LegacyMaterialSemanticResolution &semantic_resolution,
      const Ogre14LegacyNativeMaterialCapture &capture,
      Ogre14LegacyMaterialSemanticAdmission &output,
      IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *fault =
          nullptr) {
    ValidationResult validation =
        authority.ValidateScriptAndSemanticPrerequisites(
            script_resolution, semantic_resolution, capture.material.key,
            fault);
    if (!validation) {
      return validation;
    }
    const auto *receipt = script_resolution.receipt();
    const auto *source = receipt != nullptr ? receipt->source_metadata() : nullptr;
    if (source == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "semantic_runtime.test.runtime_group_generation",
          "synthetic script resolution has no source metadata");
    }
    validation = authority.ValidateNativeCapture(
        capture.material.key, source->group_generation, capture, fault);
    if (!validation) {
      return validation;
    }
    Ogre14LegacyNativeMaterialCapture copied_capture = capture;
    return authority.PublishAdmission(
        script_resolution, script_authority, semantic_resolution,
        std::move(copied_capture), output, fault);
  }
};

} // namespace RoR::Render::Testing

namespace {

using namespace RoR;
using namespace RoR::Render;

constexpr const char *kGroup = "RuntimeAdmissionGroup";
constexpr const char *kAcceptedMaterial = "RuntimeAccepted";
constexpr const char *kImportedMaterial = "RuntimeImported";
constexpr const char *kTexturedMaterial = "RuntimeTextured";
constexpr const char *kTexture = "RuntimeTexture";
constexpr std::uint64_t kReviewedResourceGeneration = 17U;
constexpr std::uint64_t kRuntimeGroupGeneration = 101U;
constexpr std::uintptr_t kAcceptedMaterialPointer = 0x5100U;
constexpr std::uintptr_t kImportedMaterialPointer = 0x5200U;
constexpr std::uintptr_t kTexturedMaterialPointer = 0x5300U;
constexpr std::uintptr_t kTexturePointer = 0x6100U;
constexpr std::uint64_t kAcceptedMaterialHandle = 51U;
constexpr std::uint64_t kImportedMaterialHandle = 52U;
constexpr std::uint64_t kTexturedMaterialHandle = 53U;
constexpr std::uint64_t kTextureHandle = 61U;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void Require(const ValidationResult &result, const char *message) {
  if (!result) {
    std::cerr << "FAIL: " << message << " [" << result.field << ": "
              << result.detail << "]\n";
    std::exit(EXIT_FAILURE);
  }
}

std::vector<std::uint8_t> LoadFixture() {
  std::ifstream input(ROR_OGRE14_MATERIAL_SEMANTIC_RUNTIME_ADMISSION_FIXTURE,
                      std::ios::binary);
  Require(input.good(), "could not open compiled runtime-admission fixture");
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>());
}

Ogre14LegacySha256 Sha(const void *bytes, std::size_t size) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0U;
  Require(EVP_Digest(bytes, size, digest.data(), &digest_size, EVP_sha256(),
                     nullptr) == 1 &&
              digest_size == 32U,
          "test SHA-256 failed");
  Ogre14LegacySha256 output{};
  std::copy_n(digest.begin(), output.size(), output.begin());
  return output;
}

Ogre14LegacySha256 Sha(const std::vector<std::uint8_t> &bytes) {
  return Sha(bytes.empty() ? static_cast<const void *>("") : bytes.data(),
             bytes.size());
}

std::string ShaHex(const Ogre14LegacySha256 &sha) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output(64U, '0');
  for (std::size_t index = 0U; index < sha.size(); ++index) {
    output[index * 2U] = kHex[sha[index] >> 4U];
    output[index * 2U + 1U] = kHex[sha[index] & 0x0fU];
  }
  return output;
}

Ogre14LegacySha256 ShaFromHex(const std::string &hex) {
  Require(hex.size() == 64U, "test SHA hex has wrong length");
  Ogre14LegacySha256 output{};
  auto nibble = [](char value) -> std::uint8_t {
    if (value >= '0' && value <= '9') {
      return static_cast<std::uint8_t>(value - '0');
    }
    return static_cast<std::uint8_t>(value - 'a' + 10);
  };
  for (std::size_t index = 0U; index < output.size(); ++index) {
    output[index] = static_cast<std::uint8_t>(
        nibble(hex[index * 2U]) * 16U + nibble(hex[index * 2U + 1U]));
  }
  return output;
}

std::size_t FindPackedString(const std::vector<std::uint8_t> &bytes,
                             const std::string &value) {
  Require(value.size() <= 0xffffU, "packed test string is oversized");
  std::vector<std::uint8_t> pattern;
  pattern.reserve(value.size() + 2U);
  pattern.push_back(static_cast<std::uint8_t>(value.size() & 0xffU));
  pattern.push_back(static_cast<std::uint8_t>(value.size() >> 8U));
  pattern.insert(pattern.end(), value.begin(), value.end());
  const auto found =
      std::search(bytes.begin(), bytes.end(), pattern.begin(), pattern.end());
  Require(found != bytes.end(), "packed test string was not found");
  const auto duplicate =
      std::search(found + static_cast<std::ptrdiff_t>(pattern.size()),
                  bytes.end(), pattern.begin(), pattern.end());
  Require(duplicate == bytes.end(), "packed test string was not unique");
  return static_cast<std::size_t>(found - bytes.begin());
}

std::size_t SkipPackedString(const std::vector<std::uint8_t> &bytes,
                             std::size_t offset) {
  Require(offset + 2U <= bytes.size(), "packed string length is truncated");
  const std::size_t size = static_cast<std::size_t>(bytes[offset]) |
                           (static_cast<std::size_t>(bytes[offset + 1U]) << 8U);
  Require(offset + 2U + size <= bytes.size(), "packed string is truncated");
  return offset + 2U + size;
}

void RehashRormat2Payload(std::vector<std::uint8_t> &bytes) {
  constexpr std::size_t kHeaderBytes = 64U;
  constexpr std::size_t kPayloadShaOffset = 24U;
  Require(bytes.size() > kHeaderBytes,
          "RORMAT2 test payload is smaller than its header");
  const auto digest =
      Sha(bytes.data() + kHeaderBytes, bytes.size() - kHeaderBytes);
  std::copy(digest.begin(), digest.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(kPayloadShaOffset));
}

void PatchRecordRepairPlanVersion(std::vector<std::uint8_t> &bytes,
                                  const std::string &material,
                                  std::uint32_t version) {
  const std::size_t material_offset = FindPackedString(bytes, material);
  Require(material_offset >= 4U, "record repair version offset underflowed");
  const std::size_t offset = material_offset - 4U;
  for (std::size_t byte = 0U; byte < 4U; ++byte) {
    bytes[offset + byte] =
        static_cast<std::uint8_t>(version >> (byte * 8U));
  }
  RehashRormat2Payload(bytes);
}

void PatchRecordRegistryRole(std::vector<std::uint8_t> &bytes,
                             const std::string &material,
                             std::uint8_t role) {
  std::size_t offset =
      FindPackedString(bytes, material) + 2U + material.size() + 32U;
  offset = SkipPackedString(bytes, offset);
  offset += 4U + 1U + 1U;
  Require(offset < bytes.size(), "record role offset is truncated");
  bytes[offset] = role;
  RehashRormat2Payload(bytes);
}

void PatchTexturedUnitRole(std::vector<std::uint8_t> &bytes,
                           std::uint8_t role) {
  std::size_t offset =
      FindPackedString(bytes, "baseColor") + 2U + std::string("baseColor").size();
  offset = SkipPackedString(bytes, offset);
  offset = SkipPackedString(bytes, offset);
  offset += 1U;
  Require(offset < bytes.size(), "texture-unit role offset is truncated");
  bytes[offset] = role;
  RehashRormat2Payload(bytes);
}

std::size_t RecordPassOffset(const std::vector<std::uint8_t> &bytes,
                             const std::string &material) {
  std::size_t offset = FindPackedString(bytes, material);
  offset = SkipPackedString(bytes, offset);
  offset += 32U;
  offset = SkipPackedString(bytes, offset);
  offset += 4U + 1U + 1U + 1U;
  offset = SkipPackedString(bytes, offset);
  offset += 4U + 8U;
  Require(offset + 29U <= bytes.size(), "record pass is truncated");
  return offset;
}

void PatchRecordColorWriteMask(std::vector<std::uint8_t> &bytes,
                               const std::string &material,
                               std::uint8_t mask) {
  constexpr std::size_t kColorWriteMaskOffset = 6U;
  bytes[RecordPassOffset(bytes, material) + kColorWriteMaskOffset] = mask;
  RehashRormat2Payload(bytes);
}

std::size_t TexturedSamplerOffset(const std::vector<std::uint8_t> &bytes) {
  std::size_t offset = FindPackedString(bytes, "baseColor");
  offset = SkipPackedString(bytes, offset);
  offset = SkipPackedString(bytes, offset);
  offset = SkipPackedString(bytes, offset);
  offset += 2U + 4U + 1U + 1U + 9U * 4U;
  Require(offset + 40U <= bytes.size(), "textured sampler is truncated");
  return offset;
}

void PatchTexturedSamplerByte(std::vector<std::uint8_t> &bytes,
                              std::size_t sampler_offset,
                              std::uint8_t value) {
  const std::size_t offset = TexturedSamplerOffset(bytes) + sampler_offset;
  Require(offset < bytes.size(), "sampler byte patch is truncated");
  bytes[offset] = value;
  RehashRormat2Payload(bytes);
}

void PatchTexturedSamplerU32(std::vector<std::uint8_t> &bytes,
                             std::size_t sampler_offset,
                             std::uint32_t value) {
  const std::size_t offset = TexturedSamplerOffset(bytes) + sampler_offset;
  Require(offset + 4U <= bytes.size(), "sampler u32 patch is truncated");
  for (std::size_t byte = 0U; byte < 4U; ++byte) {
    bytes[offset + byte] =
        static_cast<std::uint8_t>(value >> (byte * 8U));
  }
  RehashRormat2Payload(bytes);
}

TerrainBundleAuthenticatedArchiveSnapshot MakeSnapshot() {
  const std::vector<std::uint8_t> bytes{'a', 'r', 'c', 'h', 'i', 'v', 'e'};
  const auto nonce = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto path = std::filesystem::temp_directory_path() /
                    ("ror-semantic-runtime-" + std::to_string(nonce) +
                     ".archive");
  {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    Require(stream.good(), "could not create archive fixture");
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    Require(stream.good(), "could not write archive fixture");
  }
  TerrainBundleAuthenticatedArchiveSnapshot snapshot;
  std::string observed;
  std::string error;
  Require(LoadAndVerifyTerrainBundleArchiveSnapshot(
              path.string(), ShaHex(Sha(bytes)), 1024U, snapshot, observed,
              error),
          "could not authenticate archive fixture");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  return snapshot;
}

Ogre14AuthenticatedMaterialScriptSourceInput MakeSource(
    const TerrainBundleAuthenticatedArchiveSnapshot &snapshot,
    std::uint64_t ordinal, Ogre14MaterialScriptSourceRole role,
    std::string member, std::string payload) {
  Ogre14AuthenticatedMaterialScriptSourceInput input;
  input.original_bytes =
      std::make_shared<const std::vector<std::uint8_t>>(payload.begin(),
                                                       payload.end());
  input.effective_bytes = input.original_bytes;
  auto &metadata = input.metadata;
  metadata.source_role = role;
  metadata.parse_token = 1U;
  metadata.source_open_ordinal = ordinal;
  metadata.group_generation = kRuntimeGroupGeneration;
  metadata.effective_group = kGroup;
  metadata.root_script_request = "root.material";
  metadata.compiler_file_identity = member;
  metadata.archive_source_identity = snapshot.source_archive_identity();
  metadata.selected_archive_name = "runtime-admission.synthetic";
  metadata.selected_archive_type = "EmbeddedZip";
  metadata.archive_sha256 = snapshot.archive_sha256();
  metadata.archive_pointer_token = 0x700U;
  metadata.file_info_filename = member;
  const std::size_t slash = member.find_last_of('/');
  metadata.file_info_path = slash == std::string::npos
                                ? std::string()
                                : member.substr(0U, slash + 1U);
  metadata.file_info_basename = slash == std::string::npos
                                    ? member
                                    : member.substr(slash + 1U);
  metadata.exact_member_name = std::move(member);
  metadata.compressed_size = input.original_bytes->size();
  metadata.uncompressed_size = input.original_bytes->size();
  metadata.original_byte_count = input.original_bytes->size();
  metadata.effective_byte_count = input.effective_bytes->size();
  metadata.original_sha256 = ShaHex(Sha(*input.original_bytes));
  metadata.effective_sha256 = metadata.original_sha256;
  metadata.repair_plan_version = kLegacyMaterialScriptRepairPlanVersion;
  Require(ComputeLegacyMaterialScriptNoRepairPlanSha256(
              metadata.archive_sha256, metadata.exact_member_name,
              metadata.original_sha256, metadata.repair_plan_sha256),
          "could not hash NONE repair plan");
  input.authenticated_archive_snapshot = snapshot;
  return input;
}

Ogre14AuthenticatedMaterialScriptMaterialInput MakeMaterial(
    std::size_t source_index, std::uintptr_t pointer_token,
    std::uint64_t handle, const char *name, const char *origin,
    std::uint64_t ordinal) {
  Ogre14AuthenticatedMaterialScriptMaterialInput material;
  material.source_index = source_index;
  material.binding.event_ordinal = ordinal;
  material.binding.material_pointer_token = pointer_token;
  material.binding.material_handle = handle;
  material.binding.exact_material_name = name;
  material.binding.exact_group = kGroup;
  material.binding.exact_origin = origin;
  return material;
}

class SyntheticLiveAuthority final
    : public IOgre14LegacyMaterialRuntimeLiveAuthority {
public:
  Ogre14AuthenticatedMaterialScriptRegistry script_registry;
  Ogre14AuthenticatedTextureReceiptRegistry texture_registry;

  std::uintptr_t script_token() const noexcept {
    return reinterpret_cast<std::uintptr_t>(
        static_cast<const IOgre14AuthenticatedMaterialScriptResolver *>(this));
  }
  std::uintptr_t texture_token() const noexcept {
    return reinterpret_cast<std::uintptr_t>(
        static_cast<const IOgre14AuthenticatedTextureResolver *>(this));
  }
  ValidationResult CaptureAuthenticatedMaterialScriptAuthoritySnapshot(
      Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &snapshot) const
      override {
    return Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Snapshot(
        script_registry, script_token(), snapshot);
  }
  ValidationResult CaptureAuthenticatedTextureAuthoritySnapshot(
      Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) const override {
    return Testing::Ogre14AuthenticatedTextureResolutionTestAccess::Snapshot(
        texture_registry, texture_token(), snapshot);
  }
  ValidationResult ResolveAuthenticatedMaterialScript(
      Ogre::Material &,
      Ogre14AuthenticatedMaterialScriptResolution &) const override {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_FEATURE,
                                     "synthetic.native", "not a native test");
  }
  bool RevalidateAuthenticatedMaterialScript(
      Ogre::Material &,
      const Ogre14AuthenticatedMaterialScriptResolution &) const noexcept
      override {
    return false;
  }
  ValidationResult ResolveAuthenticatedTexture(
      Ogre::Texture &,
      Ogre14AuthenticatedTextureResolution &) const override {
    return ValidationResult::Failure(ValidationCode::UNSUPPORTED_FEATURE,
                                     "synthetic.native", "not a native test");
  }
  bool RevalidateAuthenticatedTexture(
      Ogre::Texture &,
      const Ogre14AuthenticatedTextureResolution &) const noexcept override {
    return false;
  }
};

Ogre14LegacyApprovedMaterialScriptSource ApproveSource(
    const Ogre14AuthenticatedMaterialScriptSourceInput &input) {
  Ogre14LegacyApprovedMaterialScriptSource source;
  source.source_role = input.metadata.source_role;
  source.exact_member_name = input.metadata.exact_member_name;
  source.original_sha256 = ShaFromHex(input.metadata.original_sha256);
  source.effective_sha256 = ShaFromHex(input.metadata.effective_sha256);
  source.repair_state = Ogre14MaterialScriptRepairState::NONE;
  source.repair_plan_version = input.metadata.repair_plan_version;
  source.repair_plan_sha256 =
      ShaFromHex(input.metadata.repair_plan_sha256);
  return source;
}

Ogre14AuthenticatedTextureResolution InstallTextureResolution(
    SyntheticLiveAuthority &live, const std::string &package_sha256,
    std::uint64_t generation = kRuntimeGroupGeneration,
    std::string member = "textures/runtime.dds",
    Ogre14AuthenticatedTextureSourceKind source_kind =
        Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER,
    std::string group = kGroup, std::string resource_name = kTexture,
    std::uintptr_t pointer_token = kTexturePointer,
    std::uint64_t handle = kTextureHandle) {
  Ogre14AuthenticatedTextureCaptureInput input;
  input.source_kind = source_kind;
  input.effective_resource_group = group;
  input.group_generation = generation;
  input.archive_sha256 = package_sha256;
  input.exact_member_name = std::move(member);
  if (source_kind ==
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER) {
    input.archive_identity = "/runtime-admission.synthetic";
    input.archive_name = "runtime-admission.synthetic";
    input.archive_type = "EmbeddedZip";
    input.archive_pointer_token = 0x7100U;
  } else {
    input.generated_fallback_rule = kOgre14GeneratedTextureFallbackRule;
    input.generated_fallback_rule_version =
        kOgre14GeneratedTextureFallbackRuleVersion;
  }
  input.binding.resource_pointer_token = pointer_token;
  input.binding.resource_handle = handle;
  input.binding.resource_state_count = 0U;
  input.binding.exact_resource_name = resource_name;
  static const std::vector<std::uint8_t> bytes{'t', 'e', 'x'};
  Ogre14AuthenticatedTextureReceipt receipt;
  Require(AdvanceOgre14AuthenticatedTextureGroupGeneration(
              group, generation, live.texture_registry) &&
              BuildOgre14AuthenticatedTextureReceipt(
              {}, input, bytes.data(), bytes.size(), receipt) &&
              CommitOgre14AuthenticatedTextureReceipt(receipt,
                                                       live.texture_registry),
          "could not commit synthetic texture receipt");
  Ogre14AuthenticatedTextureResolution resolution;
  Require(Testing::Ogre14AuthenticatedTextureResolutionTestAccess::Mint(
              live.texture_registry, group, generation, pointer_token, handle,
              resource_name, 1U, live.texture_token(), resolution),
          "could not mint synthetic loaded texture resolution");
  return resolution;
}

Ogre14LegacyNativeMaterialCapture MakeCapture(
    const char *material_name, std::uint8_t digest_lead,
    const Ogre14AuthenticatedTextureResolution *texture_resolution = nullptr) {
  Ogre14LegacyNativeMaterialCapture capture;
  capture.material.key = {kGroup, material_name};
  capture.material.source_revision = 1U;
  capture.material.base_color_semantic =
      Ogre14LegacyBaseColorSemantic::UNLIT;
  if (texture_resolution != nullptr) {
    Ogre14LegacyTextureUnitInput unit;
    unit.exact_unit_name = "baseColor";
    unit.texture_key = {kGroup, kTexture};
    unit.sampler.source_revision = 1U;
    capture.material.texture_units.push_back(unit);

    Ogre14LegacyTextureInput texture;
    texture.key = {kGroup, kTexture};
    texture.source_revision = 2U;
    texture.width = 1U;
    texture.height = 1U;
    Ogre14LegacyTextureMipInput mip;
    mip.width = 1U;
    mip.height = 1U;
    mip.row_pitch_bytes = 4U;
    mip.slice_pitch_bytes = 4U;
    mip.bytes = {0x10U, 0x20U, 0x30U, 0xffU};
    texture.mip_levels.push_back(std::move(mip));
    capture.textures.push_back(std::move(texture));
    capture.authenticated_texture_resolutions.push_back(*texture_resolution);
  }
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealSyntheticCapture(capture),
          "could not seal synthetic extractor capture");
  capture.native_material_declaration_sha256.fill(0U);
  capture.native_material_declaration_sha256[0U] = digest_lead;
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealExistingSyntheticCapture(capture),
          "could not reseal reviewed synthetic declaration digest");
  return capture;
}

struct Fixture final {
  std::vector<std::uint8_t> catalog_bytes;
  TerrainBundleAuthenticatedArchiveSnapshot archive;
  Ogre14AuthenticatedMaterialScriptSourceInput root_source;
  Ogre14AuthenticatedMaterialScriptSourceInput dependency_source;
  SyntheticLiveAuthority live;
  Ogre14AuthenticatedMaterialScriptResolution accepted_script_resolution;
  Ogre14AuthenticatedMaterialScriptResolution imported_script_resolution;
  Ogre14AuthenticatedMaterialScriptResolution textured_script_resolution;
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot script_snapshot;
  Ogre14AuthenticatedTextureAuthoritySnapshot texture_snapshot;
  Ogre14AuthenticatedTextureResolution texture_resolution;
  Ogre14LegacyMaterialSemanticApprovedManifestDescription manifest_description;
  Ogre14LegacyMaterialSemanticApprovedManifest manifest;
  Ogre14LegacyMaterialSemanticRuntimeAuthority runtime;
  Ogre14LegacyMaterialSemanticResolution accepted_semantics;
  Ogre14LegacyMaterialSemanticResolution imported_semantics;
  Ogre14LegacyMaterialSemanticResolution textured_semantics;
  Ogre14LegacyNativeMaterialCapture accepted_capture;
  Ogre14LegacyNativeMaterialCapture imported_capture;
  Ogre14LegacyNativeMaterialCapture textured_capture;
  Ogre14LegacyMaterialSemanticAdmission accepted_admission;
  Ogre14LegacyMaterialSemanticAdmission imported_admission;
  Ogre14LegacyMaterialSemanticAdmission textured_admission;
};

Fixture MakeFixture() {
  Fixture fixture;
  fixture.catalog_bytes = LoadFixture();
  Require(ShaHex(Sha(fixture.catalog_bytes)) ==
              "e41391acb8f5e13232f2515a5d6cac6b9e8c486c3d54825d0dfe18894384d2ff",
          "compiled three-record RORMAT2 full-file SHA drifted");
  fixture.archive = MakeSnapshot();
  fixture.root_source = MakeSource(
      fixture.archive, 1U, Ogre14MaterialScriptSourceRole::ROOT_SCRIPT,
      "root.material", "material Root {}\n");
  fixture.dependency_source = MakeSource(
      fixture.archive, 2U,
      Ogre14MaterialScriptSourceRole::COMPILER_DEPENDENCY,
      "inc/base.material", "material Imported {}\n");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              {}, fixture.live.script_registry) &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  fixture.live.script_registry, kGroup,
                  kRuntimeGroupGeneration) &&
              Testing::Ogre14AuthenticatedTextureResolutionTestAccess::
                  Initialize(fixture.live.texture_registry),
          "could not initialize synthetic current authorities");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              fixture.live.script_registry, kGroup, kRuntimeGroupGeneration,
              {fixture.root_source, fixture.dependency_source},
              {MakeMaterial(0U, kAcceptedMaterialPointer,
                            kAcceptedMaterialHandle, kAcceptedMaterial,
                            "root.material", 1U),
               MakeMaterial(1U, kImportedMaterialPointer,
                            kImportedMaterialHandle, kImportedMaterial,
                            "inc/base.material", 2U),
               MakeMaterial(0U, kTexturedMaterialPointer,
                            kTexturedMaterialHandle, kTexturedMaterial,
                            "root.material", 3U)}),
          "could not commit synthetic script closure");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
              fixture.live.script_registry, kGroup, kRuntimeGroupGeneration,
              kAcceptedMaterialPointer, kAcceptedMaterialHandle,
              kAcceptedMaterial, "root.material", fixture.live.script_token(),
              fixture.accepted_script_resolution) &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
                  fixture.live.script_registry, kGroup,
                  kRuntimeGroupGeneration, kImportedMaterialPointer,
                  kImportedMaterialHandle, kImportedMaterial,
                  "inc/base.material", fixture.live.script_token(),
                  fixture.imported_script_resolution) &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
                  fixture.live.script_registry, kGroup,
                  kRuntimeGroupGeneration, kTexturedMaterialPointer,
                  kTexturedMaterialHandle, kTexturedMaterial,
                  "root.material", fixture.live.script_token(),
                  fixture.textured_script_resolution) &&
              fixture.live.CaptureAuthenticatedMaterialScriptAuthoritySnapshot(
                  fixture.script_snapshot),
          "could not mint synthetic current script resolution");

  fixture.texture_resolution = InstallTextureResolution(
      fixture.live, fixture.archive.archive_sha256());
  Require(fixture.live.CaptureAuthenticatedTextureAuthoritySnapshot(
              fixture.texture_snapshot),
          "could not mint current texture authority snapshot");

  fixture.manifest_description.catalog_full_file_sha256 =
      Sha(fixture.catalog_bytes);
  fixture.manifest_description.package_archive_sha256 =
      ShaFromHex(fixture.archive.archive_sha256());
  fixture.manifest_description.exact_resource_group = kGroup;
  fixture.manifest_description.reviewed_resource_generation =
      kReviewedResourceGeneration;
  const std::vector<Ogre14LegacyApprovedMaterialScriptSource> sources{
      ApproveSource(fixture.root_source),
      ApproveSource(fixture.dependency_source)};
  Ogre14LegacyApprovedMaterialScriptClosure accepted_closure;
  accepted_closure.material_key = {kGroup, kAcceptedMaterial};
  accepted_closure.primary_source_index = 0U;
  accepted_closure.sources = sources;
  fixture.manifest_description.material_closures.push_back(accepted_closure);
  Ogre14LegacyApprovedMaterialScriptClosure imported_closure;
  imported_closure.material_key = {kGroup, kImportedMaterial};
  imported_closure.primary_source_index = 1U;
  imported_closure.sources = sources;
  fixture.manifest_description.material_closures.push_back(imported_closure);
  Ogre14LegacyApprovedMaterialScriptClosure textured_closure;
  textured_closure.material_key = {kGroup, kTexturedMaterial};
  textured_closure.primary_source_index = 0U;
  textured_closure.sources = sources;
  Ogre14LegacyApprovedMaterialTextureSource texture_source;
  texture_source.texture_key = {kGroup, kTexture};
  texture_source.exact_member_name = "textures/runtime.dds";
  textured_closure.texture_sources.push_back(texture_source);
  fixture.manifest_description.material_closures.push_back(textured_closure);
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(fixture.manifest_description,
                                   fixture.manifest) &&
              AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  fixture.manifest, {}, {}, fixture.catalog_bytes,
                  fixture.runtime),
          "could not authenticate synthetic runtime catalog");
  Require(fixture.runtime.ResolveMaterialSemantics(
              {kGroup, kAcceptedMaterial}, {}, fixture.accepted_semantics) &&
              fixture.runtime.ResolveMaterialSemantics(
                  {kGroup, kImportedMaterial}, {},
                  fixture.imported_semantics) &&
              fixture.runtime.ResolveMaterialSemantics(
                  {kGroup, kTexturedMaterial}, {},
                  fixture.textured_semantics),
          "could not resolve exact runtime semantics");
  fixture.accepted_capture = MakeCapture(kAcceptedMaterial, 0xa5U);
  fixture.imported_capture = MakeCapture(kImportedMaterial, 0xb5U);
  fixture.textured_capture =
      MakeCapture(kTexturedMaterial, 0xc5U, &fixture.texture_resolution);
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              AdmitSynthetic(
                  fixture.runtime, fixture.accepted_script_resolution,
                  fixture.script_snapshot, fixture.accepted_semantics,
                  fixture.accepted_capture, fixture.accepted_admission),
          "could not publish root admission");
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              AdmitSynthetic(
                  fixture.runtime, fixture.imported_script_resolution,
                  fixture.script_snapshot, fixture.imported_semantics,
                  fixture.imported_capture, fixture.imported_admission),
          "could not publish imported admission");
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              AdmitSynthetic(
                  fixture.runtime, fixture.textured_script_resolution,
                  fixture.script_snapshot, fixture.textured_semantics,
                  fixture.textured_capture, fixture.textured_admission),
          "could not publish textured admission");
  return fixture;
}

class ThrowingAdmissionFault final
    : public IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector {
public:
  Ogre14LegacyMaterialSemanticRuntimeAdmissionStage target =
      Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::
          BEFORE_RUNTIME_AUTHORITY_PUBLICATION;
  bool bad_alloc = true;
  void BeforeOgre14LegacyMaterialSemanticRuntimeAdmissionStage(
      Ogre14LegacyMaterialSemanticRuntimeAdmissionStage stage) override {
    if (stage != target) {
      return;
    }
    if (bad_alloc) {
      throw std::bad_alloc();
    }
    throw 9;
  }
};

class ThrowingCoordinatorFault final
    : public IOgre14LegacyLiveMaterialCoordinatorFaultInjector {
public:
  void AtFaultPoint(Ogre14LegacyLiveMaterialCoordinatorFaultPoint point)
      override {
    if (point == Ogre14LegacyLiveMaterialCoordinatorFaultPoint::
                     AFTER_ADMITTED_INNER_PREPARE) {
      throw 7;
    }
  }
};

void TestOpaqueFullFileAuthenticationAndRollback() {
  Fixture fixture = MakeFixture();
  Require(fixture.manifest.initialized() &&
              std::string(fixture.manifest.domain()) ==
                  kOgre14LegacyReviewedPackageRevisionV1Domain &&
              fixture.runtime.reviewed_resource_generation() == 17U &&
              fixture.accepted_admission.runtime_group_generation() == 101U &&
              fixture.imported_admission.initialized() &&
              fixture.textured_admission.initialized(),
          "reviewed and runtime generation domains were conflated");

  const auto owner = fixture.runtime;
  auto tampered = fixture.catalog_bytes;
  tampered.back() ^= 1U;
  Require(!AuthenticateOgre14LegacyMaterialSemanticRuntime(
               fixture.manifest, {}, {}, tampered, fixture.runtime) &&
              fixture.runtime.SharesImmutableStateWith(owner),
          "full-file SHA failure changed the committed runtime authority");

  ThrowingAdmissionFault fault;
  Require(!AuthenticateOgre14LegacyMaterialSemanticRuntime(
               fixture.manifest, {}, {}, fixture.catalog_bytes,
               fixture.runtime, &fault) &&
              fixture.runtime.SharesImmutableStateWith(owner),
          "activation bad_alloc changed the committed runtime authority");
  fault.bad_alloc = false;
  fault.target = Ogre14LegacyMaterialSemanticRuntimeAdmissionStage::
      AFTER_EXACT_REGISTRY;
  Require(!AuthenticateOgre14LegacyMaterialSemanticRuntime(
               fixture.manifest, {}, {}, fixture.catalog_bytes,
               fixture.runtime, &fault) &&
              fixture.runtime.SharesImmutableStateWith(owner),
          "activation exception changed the committed runtime authority");
}

void TestClosureSemanticNativeAndCrossAuthorityRejection() {
  Fixture fixture = MakeFixture();
  const auto sentinel = fixture.accepted_admission;

  auto bad_description = fixture.manifest_description;
  bad_description.material_closures[0U]
      .sources[0U]
      .repair_plan_sha256[0U] ^= 1U;
  Ogre14LegacyMaterialSemanticApprovedManifest bad_manifest;
  Ogre14LegacyMaterialSemanticRuntimeAuthority bad_runtime;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description, bad_manifest) &&
              AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  bad_manifest, {}, {}, fixture.catalog_bytes, bad_runtime),
          "could not build hostile plan-authority fixture");
  Ogre14LegacyMaterialSemanticResolution bad_semantics;
  Require(bad_runtime.ResolveMaterialSemantics(
              {kGroup, kAcceptedMaterial}, {}, bad_semantics),
          "could not resolve hostile runtime semantics");
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               AdmitSynthetic(
                   bad_runtime, fixture.accepted_script_resolution,
                   fixture.script_snapshot, bad_semantics,
                   fixture.accepted_capture, fixture.accepted_admission) &&
              fixture.accepted_admission.SharesImmutableStateWith(sentinel),
          "repair-plan mismatch changed or minted admission authority");

  Ogre14LegacyMaterialSemanticRuntimeAuthority foreign_runtime;
  Require(AuthenticateOgre14LegacyMaterialSemanticRuntime(
              fixture.manifest, {}, {}, fixture.catalog_bytes,
              foreign_runtime),
          "could not build fresh equal-value runtime authority");
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               AdmitSynthetic(
                   foreign_runtime, fixture.accepted_script_resolution,
                   fixture.script_snapshot, fixture.accepted_semantics,
                   fixture.accepted_capture, fixture.accepted_admission) &&
              fixture.accepted_admission.SharesImmutableStateWith(sentinel),
          "cross-authority semantic identity minted an admission");

  Ogre14LegacyNativeMaterialCapture reboxed = fixture.accepted_capture;
  reboxed.exact_native_material_audit =
      std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
          *fixture.accepted_capture.exact_native_material_audit);
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               AdmitSynthetic(
                   fixture.runtime, fixture.accepted_script_resolution,
                   fixture.script_snapshot, fixture.accepted_semantics,
                   reboxed, fixture.accepted_admission) &&
              fixture.accepted_admission.SharesImmutableStateWith(sentinel),
          "same-value reboxed native audit minted authority");

  Ogre14LegacyNativeMaterialCapture changed = fixture.accepted_capture;
  changed.native_material_declaration_sha256[0U] ^= 1U;
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               AdmitSynthetic(
                   fixture.runtime, fixture.accepted_script_resolution,
                   fixture.script_snapshot, fixture.accepted_semantics,
                   changed, fixture.accepted_admission) &&
              fixture.accepted_admission.SharesImmutableStateWith(sentinel),
          "altered native digest minted authority");
}

void TestImportedClosureAndStaleScriptRejection() {
  Fixture fixture = MakeFixture();
  const auto *imported_receipt =
      fixture.imported_script_resolution.receipt();
  Require(imported_receipt != nullptr &&
              imported_receipt->primary_source_index() == 1U &&
              fixture.runtime.AuthenticatesAdmission(
                  fixture.imported_admission, fixture.script_snapshot,
                  fixture.texture_snapshot),
          "approved dependency-origin material was not admitted");

  SyntheticLiveAuthority foreign_closure_live;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Initialize(
              {}, foreign_closure_live.script_registry) &&
              Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
                  foreign_closure_live.script_registry, kGroup,
                  kRuntimeGroupGeneration) &&
              Testing::Ogre14AuthenticatedTextureResolutionTestAccess::
                  Initialize(foreign_closure_live.texture_registry),
          "could not initialize foreign closure fixture");
  auto changed_dependency = MakeSource(
      fixture.archive, 2U,
      Ogre14MaterialScriptSourceRole::COMPILER_DEPENDENCY, "inc/base.material",
      "material ForeignDependency {}\n");
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Commit(
              foreign_closure_live.script_registry, kGroup,
              kRuntimeGroupGeneration,
              {fixture.root_source, changed_dependency},
              {MakeMaterial(0U, kAcceptedMaterialPointer,
                            kAcceptedMaterialHandle, kAcceptedMaterial,
                            "root.material", 1U)}),
          "could not commit foreign closure fixture");
  Ogre14AuthenticatedMaterialScriptResolution foreign_closure_resolution;
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot foreign_closure_snapshot;
  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Mint(
              foreign_closure_live.script_registry, kGroup,
              kRuntimeGroupGeneration, kAcceptedMaterialPointer,
              kAcceptedMaterialHandle, kAcceptedMaterial, "root.material",
              foreign_closure_live.script_token(),
              foreign_closure_resolution) &&
              foreign_closure_live.
                  CaptureAuthenticatedMaterialScriptAuthoritySnapshot(
                      foreign_closure_snapshot),
          "could not mint foreign closure resolution");
  Ogre14LegacyMaterialSemanticAdmission output = fixture.accepted_admission;
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               AdmitSynthetic(
                   fixture.runtime, foreign_closure_resolution,
                   foreign_closure_snapshot, fixture.accepted_semantics,
                   fixture.accepted_capture, output) &&
              output.SharesImmutableStateWith(fixture.accepted_admission),
          "unapproved import closure minted admission authority");

  SyntheticLiveAuthority foreign_texture_live;
  Require(Testing::Ogre14AuthenticatedTextureResolutionTestAccess::Initialize(
              foreign_texture_live.texture_registry),
          "could not initialize foreign texture authority");
  Ogre14AuthenticatedTextureAuthoritySnapshot foreign_texture_snapshot;
  Require(foreign_texture_live.CaptureAuthenticatedTextureAuthoritySnapshot(
              foreign_texture_snapshot) &&
              !fixture.runtime.AuthenticatesAdmission(
                  fixture.textured_admission, fixture.script_snapshot,
                  foreign_texture_snapshot),
          "foreign texture snapshot authenticated a textured admission");

  Require(Testing::Ogre14AuthenticatedMaterialScriptTestAccess::Advance(
              fixture.live.script_registry, kGroup,
              kRuntimeGroupGeneration + 1U),
          "could not advance stale-script fixture");
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot fresh;
  Require(fixture.live.CaptureAuthenticatedMaterialScriptAuthoritySnapshot(
              fresh) &&
              !fixture.runtime.AuthenticatesAdmission(
                  fixture.accepted_admission, fresh,
                  fixture.texture_snapshot),
          "stale script resolution survived a runtime group transition");
}

void RequireTexturedCaptureRejected(
    Fixture &fixture, const Ogre14LegacyNativeMaterialCapture &capture,
    const char *message) {
  Ogre14LegacyMaterialSemanticAdmission output = fixture.textured_admission;
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               AdmitSynthetic(
                   fixture.runtime, fixture.textured_script_resolution,
                   fixture.script_snapshot, fixture.textured_semantics,
                   capture, output) &&
              output.SharesImmutableStateWith(fixture.textured_admission),
          message);
}

void TestTexturedProjectionAndSourceAuthorityRejection() {
  Fixture fixture = MakeFixture();

  Ogre14LegacyNativeMaterialCapture changed = fixture.textured_capture;
  changed.material.texture_units.front().exact_unit_name = "foreignUnit";
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealExistingSyntheticCapture(changed),
          "could not reseal hostile exact-unit-name capture");
  RequireTexturedCaptureRejected(
      fixture, changed,
      "exact texture-unit-name projection mismatch minted admission");

  changed = fixture.textured_capture;
  changed.material.texture_units.front().sampler.minification =
      Ogre14LegacyFilter::POINT;
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealExistingSyntheticCapture(changed),
          "could not reseal hostile sampler capture");
  RequireTexturedCaptureRejected(fixture, changed,
                                 "sampler mismatch minted admission");

  changed = fixture.textured_capture;
  changed.material.pipeline.cull =
      Ogre14LegacyCullMode::ANTICLOCKWISE;
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealExistingSyntheticCapture(changed),
          "could not reseal hostile pass capture");
  RequireTexturedCaptureRejected(fixture, changed,
                                 "pass projection mismatch minted admission");

  changed = fixture.textured_capture;
  changed.material.texture_units.front().texture_key.exact_name =
      "ForeignTexture";
  changed.textures.front().key.exact_name = "ForeignTexture";
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealExistingSyntheticCapture(changed),
          "could not reseal hostile resource-key capture");
  RequireTexturedCaptureRejected(fixture, changed,
                                 "resource-key mismatch minted admission");

  auto reject_source = [&](const std::string &package_sha256,
                           std::uint64_t generation,
                           const std::string &member,
                           Ogre14AuthenticatedTextureSourceKind source_kind,
                           const std::string &group,
                           const std::string &resource_name,
                           const char *message) {
    SyntheticLiveAuthority hostile;
    Require(Testing::Ogre14AuthenticatedTextureResolutionTestAccess::
                Initialize(hostile.texture_registry),
            "could not initialize hostile texture registry");
    const auto resolution = InstallTextureResolution(
        hostile, package_sha256, generation, member, source_kind, group,
        resource_name);
    const auto capture =
        MakeCapture(kTexturedMaterial, 0xc5U, &resolution);
    RequireTexturedCaptureRejected(fixture, capture, message);
  };
  reject_source(
      fixture.archive.archive_sha256(), kRuntimeGroupGeneration,
      "textures/foreign.dds",
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER,
      kGroup, kTexture, "texture archive-member mismatch minted admission");
  reject_source(
      std::string(64U, 'd'), kRuntimeGroupGeneration,
      "textures/runtime.dds",
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER,
      kGroup, kTexture, "texture package SHA mismatch minted admission");
  reject_source(
      fixture.archive.archive_sha256(), kRuntimeGroupGeneration + 1U,
      "textures/runtime.dds",
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER,
      kGroup, kTexture, "texture runtime-generation mismatch minted admission");
  reject_source(
      fixture.archive.archive_sha256(), kRuntimeGroupGeneration,
      kTexture,
      Ogre14AuthenticatedTextureSourceKind::VERSIONED_GENERATED_FALLBACK,
      kGroup, kTexture, "generated texture source kind minted admission");
  reject_source(
      fixture.archive.archive_sha256(), kRuntimeGroupGeneration,
      "textures/runtime.dds",
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER,
      "ForeignGroup", kTexture,
      "texture effective resource-group mismatch minted admission");
  reject_source(
      fixture.archive.archive_sha256(), kRuntimeGroupGeneration,
      "textures/runtime.dds",
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER,
      kGroup, "ForeignTexture",
      "texture exact resource-name mismatch minted admission");
}

void TestUnsupportedSurfaceRepairAndBindingRejection() {
  Fixture fixture = MakeFixture();

  auto bad_description = fixture.manifest_description;
  bad_description.material_closures.front()
      .sources.front()
      .repair_plan_version = 2U;
  Ogre14LegacyMaterialSemanticApprovedManifest manifest_sentinel =
      fixture.manifest;
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               MintApprovedManifest(bad_description, manifest_sentinel) &&
              manifest_sentinel.SharesImmutableStateWith(fixture.manifest),
          "unsupported approved repair-plan version minted authority");

  bad_description = fixture.manifest_description;
  bad_description.material_closures.front().sources.front().repair_state =
      static_cast<Ogre14MaterialScriptRepairState>(0xffU);
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               MintApprovedManifest(bad_description, manifest_sentinel) &&
              manifest_sentinel.SharesImmutableStateWith(fixture.manifest),
          "invalid approved repair state minted authority");

  bad_description = fixture.manifest_description;
  bad_description.material_closures.back()
      .texture_sources.front()
      .source_kind =
      Ogre14AuthenticatedTextureSourceKind::VERSIONED_GENERATED_FALLBACK;
  Require(!Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
               MintApprovedManifest(bad_description, manifest_sentinel) &&
              manifest_sentinel.SharesImmutableStateWith(fixture.manifest),
          "unsupported generated source binding minted manifest authority");

  bad_description = fixture.manifest_description;
  bad_description.material_closures.back()
      .texture_sources.front()
      .texture_key.exact_name = "ForeignTexture";
  Ogre14LegacyMaterialSemanticApprovedManifest bad_binding_manifest;
  Ogre14LegacyMaterialSemanticRuntimeAuthority runtime_sentinel =
      fixture.runtime;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description, bad_binding_manifest) &&
              !AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  bad_binding_manifest, {}, {}, fixture.catalog_bytes,
                  runtime_sentinel) &&
              runtime_sentinel.SharesImmutableStateWith(fixture.runtime),
          "foreign approved texture key escaped source-binding validation");

  auto unsupported_repair = fixture.catalog_bytes;
  PatchRecordRepairPlanVersion(unsupported_repair, kAcceptedMaterial, 999U);
  bad_description = fixture.manifest_description;
  bad_description.catalog_full_file_sha256 = Sha(unsupported_repair);
  Ogre14LegacyMaterialSemanticApprovedManifest unsupported_repair_manifest;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description,
                                   unsupported_repair_manifest) &&
              !AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  unsupported_repair_manifest, {}, {}, unsupported_repair,
                  runtime_sentinel) &&
              runtime_sentinel.SharesImmutableStateWith(fixture.runtime),
          "unsupported catalog repair-plan version activated");

  auto untextured_linear = fixture.catalog_bytes;
  PatchRecordRegistryRole(untextured_linear, kAcceptedMaterial, 1U);
  bad_description = fixture.manifest_description;
  bad_description.catalog_full_file_sha256 = Sha(untextured_linear);
  Ogre14LegacyMaterialSemanticApprovedManifest untextured_linear_manifest;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description,
                                   untextured_linear_manifest) &&
              !AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  untextured_linear_manifest, {}, {}, untextured_linear,
                  runtime_sentinel),
          "untextured LINEAR_DATA catalog role activated");

  auto textured_linear = fixture.catalog_bytes;
  PatchRecordRegistryRole(textured_linear, kTexturedMaterial, 1U);
  PatchTexturedUnitRole(textured_linear, 1U);
  bad_description = fixture.manifest_description;
  bad_description.catalog_full_file_sha256 = Sha(textured_linear);
  Ogre14LegacyMaterialSemanticApprovedManifest textured_linear_manifest;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description, textured_linear_manifest) &&
              !AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  textured_linear_manifest, {}, {}, textured_linear,
                  runtime_sentinel),
          "textured LINEAR_DATA catalog role activated");

  auto unsupported_pass = fixture.catalog_bytes;
  PatchRecordColorWriteMask(unsupported_pass, kAcceptedMaterial, 0x07U);
  bad_description = fixture.manifest_description;
  bad_description.catalog_full_file_sha256 = Sha(unsupported_pass);
  Ogre14LegacyMaterialSemanticApprovedManifest unsupported_pass_manifest;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description,
                                   unsupported_pass_manifest) &&
              !AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  unsupported_pass_manifest, {}, {}, unsupported_pass,
                  runtime_sentinel) &&
              runtime_sentinel.SharesImmutableStateWith(fixture.runtime),
          "unsupported catalog pass activated");

  // The catalog and a resealed native capture deliberately agree on this
  // comparison sampler. Activation must still reject the surface before any
  // live material can be admitted.
  auto unsupported_compare = fixture.catalog_bytes;
  constexpr std::size_t kSamplerCompareEnabledOffset = 22U;
  PatchTexturedSamplerByte(unsupported_compare,
                           kSamplerCompareEnabledOffset, 1U);
  Ogre14LegacyNativeMaterialCapture matching_unsupported_native =
      fixture.textured_capture;
  matching_unsupported_native.material.texture_units.front()
      .sampler.compare_enabled = true;
  Require(Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
              SealExistingSyntheticCapture(matching_unsupported_native) &&
              matching_unsupported_native.native_material_audit_receipt
                  .Authenticates(matching_unsupported_native),
          "could not reseal matched unsupported sampler capture");
  bad_description = fixture.manifest_description;
  bad_description.catalog_full_file_sha256 = Sha(unsupported_compare);
  Ogre14LegacyMaterialSemanticApprovedManifest unsupported_compare_manifest;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description,
                                   unsupported_compare_manifest) &&
              !AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  unsupported_compare_manifest, {}, {}, unsupported_compare,
                  runtime_sentinel),
          "catalog/native matched-but-unsupported sampler activated");

  auto unsupported_filter = fixture.catalog_bytes;
  constexpr std::size_t kSamplerMinificationOffset = 0U;
  PatchTexturedSamplerByte(unsupported_filter, kSamplerMinificationOffset, 0U);
  bad_description = fixture.manifest_description;
  bad_description.catalog_full_file_sha256 = Sha(unsupported_filter);
  Ogre14LegacyMaterialSemanticApprovedManifest unsupported_filter_manifest;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description,
                                   unsupported_filter_manifest) &&
              !AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  unsupported_filter_manifest, {}, {}, unsupported_filter,
                  runtime_sentinel),
          "unsupported catalog sampler filter activated");

  auto unsupported_lod = fixture.catalog_bytes;
  constexpr std::size_t kSamplerMaximumLodOffset = 14U;
  constexpr std::uint32_t kFloat1000Bits = 0x447a0000U;
  PatchTexturedSamplerU32(unsupported_lod, kSamplerMaximumLodOffset,
                          kFloat1000Bits);
  bad_description = fixture.manifest_description;
  bad_description.catalog_full_file_sha256 = Sha(unsupported_lod);
  Ogre14LegacyMaterialSemanticApprovedManifest unsupported_lod_manifest;
  Require(Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess::
              MintApprovedManifest(bad_description,
                                   unsupported_lod_manifest) &&
              !AuthenticateOgre14LegacyMaterialSemanticRuntime(
                  unsupported_lod_manifest, {}, {}, unsupported_lod,
                  runtime_sentinel),
          "unsupported catalog sampler LOD activated");
}

void TestAuthenticatedCoordinatorCapabilityAndRollback() {
  Fixture fixture = MakeFixture();
  Ogre14LegacyLiveMaterialCoordinatorConfiguration config;
  std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> coordinator;
  Require(CreateOgre14LegacyAuthenticatedMaterialCoordinator(
              config, fixture.runtime, fixture.live, coordinator),
          "authenticated factory rejected exact runtime authority");

  Ogre14LegacyPreparedMaterialFrame raw;
  Ogre14LegacyMaterialObservation observation;
  observation.material_key = {kGroup, kAcceptedMaterial};
  observation.semantic_resolution = fixture.accepted_semantics;
  observation.native_capture = fixture.accepted_capture;
  Require(!coordinator->PrepareFrame(1U, {observation}, raw),
          "authenticated coordinator accepted raw caller capture");

  Ogre14LegacyAdmittedPreparedMaterialFrame frame;
  Require(coordinator->PreparePreviouslyAdmittedFrameForTesting(
              1U, {fixture.accepted_admission}, frame) &&
              frame.initialized() && frame.admission_count() == 1U &&
              frame.prepared_frame() != nullptr &&
              coordinator->has_pending_frame(),
          "authenticated admission did not prepare an outer capability");
  Ogre14LegacyAdmittedPreparedMaterialFrame foreign;
  Require(coordinator->CommitAdmittedPreparedFrameAfterAcceptedExposure(
              foreign) == Ogre14LegacyPreparedMaterialCommitResult::
                             PREPARED_FRAME_MISMATCH &&
              coordinator->has_pending_frame(),
          "outer capability mismatch consumed the pending transaction");
  Require(coordinator->CommitAdmittedPreparedFrameAfterAcceptedExposure(
              frame) ==
              Ogre14LegacyPreparedMaterialCommitResult::COMMITTED &&
              !coordinator->has_pending_frame() &&
              coordinator->source_sequence() == 1U,
          "exact outer capability did not commit allocation-free");

  std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> rollback;
  Require(CreateOgre14LegacyAuthenticatedMaterialCoordinator(
              config, fixture.runtime, fixture.live, rollback),
          "could not create rollback coordinator");
  ThrowingCoordinatorFault fault;
  Ogre14LegacyAdmittedPreparedMaterialFrame sentinel = frame;
  Require(!rollback->PreparePreviouslyAdmittedFrameForTesting(
               1U, {fixture.accepted_admission}, sentinel, &fault) &&
              !rollback->has_pending_frame() &&
              sentinel.SharesImmutableStateWith(frame) &&
              rollback->source_sequence() == 0U,
          "post-inner exception leaked a pending lease or changed output");
}

} // namespace

int main() {
  TestOpaqueFullFileAuthenticationAndRollback();
  TestClosureSemanticNativeAndCrossAuthorityRejection();
  TestImportedClosureAndStaleScriptRejection();
  TestTexturedProjectionAndSourceAuthorityRejection();
  TestUnsupportedSurfaceRepairAndBindingRejection();
  TestAuthenticatedCoordinatorCapabilityAndRollback();
  std::cout << "OGRE14 semantic runtime admission tests passed\n";
  return EXIT_SUCCESS;
}
