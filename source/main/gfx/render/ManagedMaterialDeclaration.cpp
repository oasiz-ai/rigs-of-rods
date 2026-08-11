/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "ManagedMaterialDeclaration.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, index);
}

bool IsIdentifier(const std::string &value) noexcept {
  return !value.empty() &&
         value.size() <= kManagedMaterialMaximumIdentifierBytes &&
         value.find('\0') == std::string::npos;
}

bool IsOptionalIdentifier(const std::string &value) noexcept {
  return value.empty() || IsIdentifier(value);
}

bool IsZeroDigest(const RenderPayloadDigest &digest) noexcept {
  return std::all_of(digest.begin(), digest.end(),
                     [](std::uint8_t byte) { return byte == 0U; });
}

bool IsLowercaseSha256(const std::string &value) noexcept {
  if (value.size() != 64U) {
    return false;
  }
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool AddChecked(std::uint64_t value, std::uint64_t &total) noexcept {
  if (value > (std::numeric_limits<std::uint64_t>::max)() - total) {
    return false;
  }
  total += value;
  return true;
}

std::uint64_t StringBytes(const std::string &value) noexcept {
  return static_cast<std::uint64_t>(value.size());
}

std::uint64_t SourceIdentityBytes(
    const ManagedMaterialTextureSourceIdentity &identity) noexcept {
  std::uint64_t total = 0U;
  const std::string *const strings[] = {
      &identity.effective_resource_group, &identity.archive_identity,
      &identity.archive_name,             &identity.archive_type,
      &identity.archive_sha256,            &identity.exact_member_name,
      &identity.exact_resource_name,       &identity.generated_rule};
  for (const std::string *value : strings) {
    if (!AddChecked(StringBytes(*value), total)) {
      return (std::numeric_limits<std::uint64_t>::max)();
    }
  }
  return total;
}

class CanonicalWriter final {
public:
  void AddByte(std::uint8_t value) { bytes_.push_back(value); }

  void AddBool(bool value) { AddByte(value ? 1U : 0U); }

  void AddU32(std::uint32_t value) {
    for (std::uint32_t index = 0U; index < 4U; ++index) {
      AddByte(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
  }

  void AddU64(std::uint64_t value) {
    for (std::uint32_t index = 0U; index < 8U; ++index) {
      AddByte(static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU));
    }
  }

  void AddString(const std::string &value) {
    AddU64(static_cast<std::uint64_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  void AddDigest(const RenderPayloadDigest &digest) {
    bytes_.insert(bytes_.end(), digest.begin(), digest.end());
  }

  [[nodiscard]] RenderPayloadDigest Digest() const noexcept {
    return ComputeRenderPayloadDigest(bytes_.data(), bytes_.size());
  }

private:
  std::vector<std::uint8_t> bytes_;
};

RenderPayloadDigest ComputeSourceIdentityDigest(
    const ManagedMaterialTextureSourceIdentity &identity) {
  CanonicalWriter writer;
  writer.AddString("ror-managed-material-source-receipt-v1");
  writer.AddU32(identity.version);
  writer.AddByte(static_cast<std::uint8_t>(identity.trust));
  writer.AddU64(identity.group_generation);
  writer.AddString(identity.effective_resource_group);
  writer.AddString(identity.archive_identity);
  writer.AddString(identity.archive_name);
  writer.AddString(identity.archive_type);
  writer.AddString(identity.archive_sha256);
  writer.AddString(identity.exact_member_name);
  writer.AddString(identity.exact_resource_name);
  writer.AddString(identity.generated_rule);
  writer.AddU32(identity.generated_rule_version);
  writer.AddU64(identity.byte_count);
  writer.AddDigest(identity.source_sha256);
  return writer.Digest();
}

RenderPayloadDigest ComputeDeclarationIdentityDigest(
    const ManagedMaterialDeclarationMetadata &metadata,
    const std::array<ManagedMaterialTextureSourceReceipt,
                     kManagedMaterialTextureSlotCount> &sources) {
  CanonicalWriter writer;
  writer.AddString("ror-managed-material-declaration-v1");
  writer.AddU32(metadata.version);
  writer.AddU64(metadata.actor_generation);
  writer.AddU64(metadata.definition_generation);
  writer.AddString(metadata.exact_material_name);
  writer.AddByte(static_cast<std::uint8_t>(metadata.declared_type));
  writer.AddByte(static_cast<std::uint8_t>(metadata.resolved_type));
  writer.AddBool(metadata.type_overridden_by_tuneup);
  writer.AddBool(metadata.double_sided);
  writer.AddBool(metadata.removed_by_tuneup);
  for (std::size_t index = 0U; index < metadata.textures.size(); ++index) {
    const ManagedMaterialTextureBindingMetadata &binding =
        metadata.textures[index];
    writer.AddByte(static_cast<std::uint8_t>(binding.slot));
    writer.AddBool(binding.configured);
    writer.AddString(binding.declared_texture_name);
    writer.AddString(binding.resolved_texture_name);
    writer.AddString(binding.effective_texture_name);
    writer.AddString(binding.requested_resource_group);
    writer.AddString(binding.effective_resource_group);
    writer.AddBool(binding.source_receipt_present);
    writer.AddDigest(binding.source_receipt_present
                         ? sources[index].canonical_identity_sha256()
                         : RenderPayloadDigest{});
  }
  return writer.Digest();
}

std::uint64_t DeclarationIdentityBytes(
    const ManagedMaterialDeclarationMetadata &metadata) noexcept {
  std::uint64_t total = StringBytes(metadata.exact_material_name);
  for (const ManagedMaterialTextureBindingMetadata &binding :
       metadata.textures) {
    if (!AddChecked(StringBytes(binding.declared_texture_name), total) ||
        !AddChecked(StringBytes(binding.resolved_texture_name), total) ||
        !AddChecked(StringBytes(binding.effective_texture_name), total) ||
        !AddChecked(StringBytes(binding.requested_resource_group), total) ||
        !AddChecked(StringBytes(binding.effective_resource_group), total)) {
      return (std::numeric_limits<std::uint64_t>::max)();
    }
  }
  return total;
}

ValidationResult ValidateSourceIdentity(
    const ManagedMaterialDeclarationRegistryConfiguration &configuration,
    const ManagedMaterialTextureSourceIdentity &identity,
    std::size_t source_size) {
  if (identity.version != kManagedMaterialSourceReceiptVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "managed_material_source.version",
                   "source receipt version is unsupported");
  }
  if (!IsKnownManagedMaterialSourceTrust(identity.trust)) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "managed_material_source.trust",
                   "source trust kind is invalid");
  }
  if (identity.group_generation == 0U ||
      !IsIdentifier(identity.effective_resource_group) ||
      !IsIdentifier(identity.exact_resource_name)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "managed_material_source.resource_identity",
                   "group generation, resource group, and resource name are required");
  }
  if (source_size == 0U ||
      static_cast<std::uint64_t>(source_size) != identity.byte_count ||
      identity.byte_count > configuration.maximum_source_bytes) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "managed_material_source.byte_count",
                   "required source bytes are missing or exceed the configured bound");
  }
  if (IsZeroDigest(identity.source_sha256)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "managed_material_source.source_sha256",
                   "source SHA-256 must be nonzero");
  }
  if (!IsOptionalIdentifier(identity.archive_identity) ||
      !IsOptionalIdentifier(identity.archive_name) ||
      !IsOptionalIdentifier(identity.archive_type) ||
      !IsOptionalIdentifier(identity.archive_sha256) ||
      !IsOptionalIdentifier(identity.exact_member_name) ||
      !IsOptionalIdentifier(identity.generated_rule)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "managed_material_source.source_identity",
                   "one or more source identity fields are invalid");
  }

  switch (identity.trust) {
  case ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES:
    if (!identity.archive_identity.empty() ||
        !identity.archive_sha256.empty() || !identity.generated_rule.empty() ||
        identity.generated_rule_version != 0U) {
      return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                     "managed_material_source.caller_identity",
                     "caller-supplied bytes cannot claim authentication or generation authority");
    }
    break;
  case ManagedMaterialSourceTrust::OBSERVED_SELECTED_SOURCE:
    if (!identity.archive_identity.empty() ||
        !identity.archive_sha256.empty() || identity.archive_name.empty() ||
        identity.archive_type.empty() || identity.exact_member_name.empty() ||
        !identity.generated_rule.empty() ||
        identity.generated_rule_version != 0U) {
      return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                     "managed_material_source.ordinary_identity",
                     "ordinary selected source identity is incomplete or claims authentication");
    }
    break;
  case ManagedMaterialSourceTrust::AUTHENTICATED_ARCHIVE_MEMBER:
    if (identity.archive_identity.empty() || identity.archive_name.empty() ||
        identity.archive_type.empty() || identity.exact_member_name.empty() ||
        !IsLowercaseSha256(identity.archive_sha256) ||
        !identity.generated_rule.empty() ||
        identity.generated_rule_version != 0U) {
      return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                     "managed_material_source.authenticated_identity",
                     "authenticated archive source identity is incomplete");
    }
    break;
  case ManagedMaterialSourceTrust::AUTHENTICATED_GENERATED_FALLBACK:
    if (!identity.archive_identity.empty() || !identity.archive_name.empty() ||
        !identity.archive_type.empty() || identity.exact_member_name.empty() ||
        identity.exact_member_name != identity.exact_resource_name ||
        !IsLowercaseSha256(identity.archive_sha256) ||
        identity.generated_rule.empty() ||
        identity.generated_rule_version == 0U) {
      return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                     "managed_material_source.generated_identity",
                     "authenticated generated source identity is incomplete");
    }
    break;
  }

  if (SourceIdentityBytes(identity) >
      configuration.maximum_retained_identity_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "managed_material_source.identity_bytes",
                   "source identity exceeds the configured retained bound");
  }
  return ValidationResult::Success();
}

std::string SourceCollisionKey(
    const ManagedMaterialTextureSourceIdentity &identity) {
  CanonicalWriter writer;
  writer.AddString("ror-managed-material-source-key-v1");
  writer.AddByte(static_cast<std::uint8_t>(identity.trust));
  writer.AddU64(identity.group_generation);
  writer.AddString(identity.effective_resource_group);
  writer.AddString(identity.archive_identity);
  writer.AddString(identity.archive_name);
  writer.AddString(identity.archive_type);
  writer.AddString(identity.archive_sha256);
  writer.AddString(identity.exact_member_name);
  writer.AddString(identity.exact_resource_name);
  writer.AddString(identity.generated_rule);
  writer.AddU32(identity.generated_rule_version);
  const RenderPayloadDigest digest = writer.Digest();
  return std::string(reinterpret_cast<const char *>(digest.data()),
                     digest.size());
}

void MaybeInject(ManagedMaterialDeclarationTransactionStage stage,
                 IManagedMaterialDeclarationFaultInjector *injector) {
  if (injector != nullptr) {
    injector->BeforeManagedMaterialDeclarationStage(stage);
  }
}

} // namespace

struct ManagedMaterialTextureSourceReceipt::State final {
  ManagedMaterialTextureSourceIdentity identity;
  std::vector<std::uint8_t> bytes;
  std::uint64_t retained_identity_bytes = 0U;
  RenderPayloadDigest canonical_identity_sha256{};
};

struct ManagedMaterialDeclaration::State final {
  ManagedMaterialDeclarationMetadata metadata;
  std::array<ManagedMaterialTextureSourceReceipt,
             kManagedMaterialTextureSlotCount>
      sources{};
  std::uint64_t retained_identity_bytes = 0U;
};

struct ManagedMaterialDeclarationRegistry::State final {
  std::uint32_t version = kManagedMaterialDeclarationRegistryVersion;
  ManagedMaterialDeclarationRegistryConfiguration configuration;
  std::uint64_t actor_generation = 0U;
  bool active = false;
  std::map<std::string, ManagedMaterialDeclaration> declarations;
  std::uint64_t retained_source_bytes = 0U;
  std::uint64_t retained_identity_bytes = 0U;
};

bool IsKnownManagedMaterialSemanticType(
    ManagedMaterialSemanticType type) noexcept {
  switch (type) {
  case ManagedMaterialSemanticType::FLEXMESH_STANDARD:
  case ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT:
  case ManagedMaterialSemanticType::MESH_STANDARD:
  case ManagedMaterialSemanticType::MESH_TRANSPARENT:
    return true;
  }
  return false;
}

bool IsKnownManagedMaterialTextureSlot(
    ManagedMaterialTextureSlot slot) noexcept {
  return static_cast<std::size_t>(slot) < kManagedMaterialTextureSlotCount;
}

bool IsKnownManagedMaterialSourceTrust(
    ManagedMaterialSourceTrust trust) noexcept {
  switch (trust) {
  case ManagedMaterialSourceTrust::CALLER_SUPPLIED_UNAUTHENTICATED_BYTES:
  case ManagedMaterialSourceTrust::OBSERVED_SELECTED_SOURCE:
  case ManagedMaterialSourceTrust::AUTHENTICATED_ARCHIVE_MEMBER:
  case ManagedMaterialSourceTrust::AUTHENTICATED_GENERATED_FALLBACK:
    return true;
  }
  return false;
}

ValidationResult ValidateManagedMaterialDeclarationRegistryConfiguration(
    const ManagedMaterialDeclarationRegistryConfiguration &configuration) {
  if (configuration.version != kManagedMaterialDeclarationRegistryVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "managed_material_registry.version",
                   "registry version is unsupported");
  }
  if (configuration.maximum_declarations == 0U ||
      configuration.maximum_declarations >
          kManagedMaterialMaximumDeclarationsPerActor ||
      configuration.maximum_source_bytes == 0U ||
      configuration.maximum_source_bytes > kManagedMaterialMaximumSourceBytes ||
      configuration.maximum_retained_source_bytes == 0U ||
      configuration.maximum_retained_source_bytes >
          kManagedMaterialMaximumRetainedSourceBytes ||
      configuration.maximum_retained_identity_bytes == 0U ||
      configuration.maximum_retained_identity_bytes >
          kManagedMaterialMaximumRetainedIdentityBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "managed_material_registry.configuration",
                   "one or more registry bounds are zero or exceed hard limits");
  }
  return ValidationResult::Success();
}

ManagedMaterialTextureSourceReceipt::ManagedMaterialTextureSourceReceipt(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool ManagedMaterialTextureSourceReceipt::initialized() const noexcept {
  return state_ != nullptr;
}

const ManagedMaterialTextureSourceIdentity *
ManagedMaterialTextureSourceReceipt::identity() const noexcept {
  return state_ != nullptr ? &state_->identity : nullptr;
}

const std::uint8_t *
ManagedMaterialTextureSourceReceipt::source_bytes() const noexcept {
  return state_ != nullptr && !state_->bytes.empty() ? state_->bytes.data()
                                                      : nullptr;
}

std::size_t ManagedMaterialTextureSourceReceipt::source_size() const noexcept {
  return state_ != nullptr ? state_->bytes.size() : 0U;
}

std::uint64_t
ManagedMaterialTextureSourceReceipt::retained_identity_bytes() const noexcept {
  return state_ != nullptr ? state_->retained_identity_bytes : 0U;
}

RenderPayloadDigest
ManagedMaterialTextureSourceReceipt::canonical_identity_sha256() const
    noexcept {
  return state_ != nullptr ? state_->canonical_identity_sha256
                           : RenderPayloadDigest{};
}

bool ManagedMaterialTextureSourceReceipt::SharesImmutableStateWith(
    const ManagedMaterialTextureSourceReceipt &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult BuildManagedMaterialTextureSourceReceipt(
    const ManagedMaterialDeclarationRegistryConfiguration &configuration,
    const ManagedMaterialTextureSourceIdentity &identity,
    const void *source_bytes, std::size_t source_size,
    ManagedMaterialTextureSourceReceipt &output,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  if (identity.trust != ManagedMaterialSourceTrust::
                            CALLER_SUPPLIED_UNAUTHENTICATED_BYTES) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_source.authenticated_issuer",
                   "selected or authenticated trust requires an opaque registry-minted OGRE source resolution");
  }
  return ManagedMaterialTextureSourceReceipt::BuildFromTrustedIssuer(
      configuration, identity, source_bytes, source_size, output,
      fault_injector);
}

ValidationResult
ManagedMaterialTextureSourceReceipt::BuildFromTrustedIssuer(
    const ManagedMaterialDeclarationRegistryConfiguration &configuration,
    const ManagedMaterialTextureSourceIdentity &identity,
    const void *source_bytes, std::size_t source_size,
    ManagedMaterialTextureSourceReceipt &output,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  const ValidationResult configuration_validation =
      ValidateManagedMaterialDeclarationRegistryConfiguration(configuration);
  if (!configuration_validation) {
    return configuration_validation;
  }
  if (source_bytes == nullptr && source_size != 0U) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_source.bytes",
                   "nonempty source bytes have a null address");
  }
  const ValidationResult identity_validation =
      ValidateSourceIdentity(configuration, identity, source_size);
  if (!identity_validation) {
    return identity_validation;
  }

  try {
    auto candidate = std::make_shared<ManagedMaterialTextureSourceReceipt::State>();
    candidate->identity = identity;
    const auto *begin = static_cast<const std::uint8_t *>(source_bytes);
    candidate->bytes.assign(begin, begin + source_size);
    MaybeInject(ManagedMaterialDeclarationTransactionStage::AFTER_SOURCE_BYTES_COPIED,
                fault_injector);
    const RenderPayloadDigest actual = ComputeRenderPayloadDigest(
        candidate->bytes.data(), candidate->bytes.size());
    if (actual != identity.source_sha256) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "managed_material_source.source_sha256",
                     "copied source bytes do not match their declared SHA-256");
    }
    candidate->retained_identity_bytes = SourceIdentityBytes(identity);
    candidate->canonical_identity_sha256 =
        ComputeSourceIdentityDigest(identity);
    if (IsZeroDigest(candidate->canonical_identity_sha256)) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "managed_material_source.canonical_identity_sha256",
                     "canonical source identity digest is unusable");
    }
    MaybeInject(
        ManagedMaterialDeclarationTransactionStage::BEFORE_SOURCE_RECEIPT_COMMIT,
        fault_injector);
    ManagedMaterialTextureSourceReceipt staged(
        std::shared_ptr<const ManagedMaterialTextureSourceReceipt::State>(
            std::move(candidate)));
    output = std::move(staged);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_source.allocation",
                   "allocation failed before source receipt publication");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "managed_material_source.size",
                   "source receipt exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_source.exception",
                   "unexpected exception before source receipt publication");
  }
}

ManagedMaterialDeclaration::ManagedMaterialDeclaration(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool ManagedMaterialDeclaration::initialized() const noexcept {
  return state_ != nullptr;
}

const ManagedMaterialDeclarationMetadata *
ManagedMaterialDeclaration::metadata() const noexcept {
  return state_ != nullptr ? &state_->metadata : nullptr;
}

const ManagedMaterialTextureSourceReceipt *
ManagedMaterialDeclaration::source_receipt(
    ManagedMaterialTextureSlot slot) const noexcept {
  const std::size_t index = static_cast<std::size_t>(slot);
  if (state_ == nullptr || index >= state_->sources.size() ||
      !state_->sources[index].initialized()) {
    return nullptr;
  }
  return &state_->sources[index];
}

std::uint64_t
ManagedMaterialDeclaration::retained_identity_bytes() const noexcept {
  return state_ != nullptr ? state_->retained_identity_bytes : 0U;
}

bool ManagedMaterialDeclaration::SharesImmutableStateWith(
    const ManagedMaterialDeclaration &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult BuildManagedMaterialDeclaration(
    const ManagedMaterialDeclarationRegistryConfiguration &configuration,
    const ManagedMaterialDeclarationInput &input,
    ManagedMaterialDeclaration &output,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  const ValidationResult configuration_validation =
      ValidateManagedMaterialDeclarationRegistryConfiguration(configuration);
  if (!configuration_validation) {
    return configuration_validation;
  }
  if (input.version != kManagedMaterialDeclarationVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "managed_material_declaration.version",
                   "declaration version is unsupported");
  }
  if (input.actor_generation == 0U || input.definition_generation == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "managed_material_declaration.generation",
                   "actor and definition generations must be nonzero");
  }
  if (!IsIdentifier(input.exact_material_name)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "managed_material_declaration.exact_material_name",
                   "material name is empty or exceeds the identifier bound");
  }
  if (!IsKnownManagedMaterialSemanticType(input.declared_type) ||
      !IsKnownManagedMaterialSemanticType(input.resolved_type)) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "managed_material_declaration.semantic_type",
                   "declared or resolved managed-material type is invalid");
  }
  if (input.type_overridden_by_tuneup !=
      (input.declared_type != input.resolved_type)) {
    return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                   "managed_material_declaration.type_override",
                   "Tuneup type-override flag disagrees with exact types");
  }

  try {
    auto candidate = std::make_shared<ManagedMaterialDeclaration::State>();
    candidate->metadata.actor_generation = input.actor_generation;
    candidate->metadata.definition_generation = input.definition_generation;
    candidate->metadata.exact_material_name = input.exact_material_name;
    candidate->metadata.declared_type = input.declared_type;
    candidate->metadata.resolved_type = input.resolved_type;
    candidate->metadata.type_overridden_by_tuneup =
        input.type_overridden_by_tuneup;
    candidate->metadata.double_sided = input.double_sided;
    candidate->metadata.removed_by_tuneup = input.removed_by_tuneup;

    for (std::size_t index = 0U; index < input.textures.size(); ++index) {
      const ManagedMaterialTextureBindingInput &binding =
          input.textures[index];
      const ManagedMaterialTextureSlot expected_slot =
          static_cast<ManagedMaterialTextureSlot>(index);
      if (!IsKnownManagedMaterialTextureSlot(binding.slot) ||
          binding.slot != expected_slot) {
        return Failure(ValidationCode::NON_DETERMINISTIC_ORDER,
                       "managed_material_declaration.texture.slot",
                       "texture bindings must occupy exact canonical slot order",
                       index);
      }
      if (!binding.configured) {
        if (!binding.declared_texture_name.empty() ||
            !binding.resolved_texture_name.empty() ||
            !binding.effective_texture_name.empty() ||
            !binding.requested_resource_group.empty() ||
            !binding.effective_resource_group.empty() ||
            binding.source_receipt.initialized()) {
          return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                         "managed_material_declaration.texture.unconfigured",
                         "unconfigured texture binding contains source state",
                         index);
        }
      } else {
        if (!IsIdentifier(binding.declared_texture_name) ||
            !IsIdentifier(binding.resolved_texture_name) ||
            (!input.removed_by_tuneup &&
             !IsIdentifier(binding.effective_texture_name)) ||
            (input.removed_by_tuneup &&
             !binding.effective_texture_name.empty()) ||
            !IsIdentifier(binding.requested_resource_group) ||
            (!input.removed_by_tuneup &&
             !IsIdentifier(binding.effective_resource_group)) ||
            (input.removed_by_tuneup &&
             !binding.effective_resource_group.empty())) {
          return Failure(ValidationCode::INVALID_IDENTIFIER,
                         "managed_material_declaration.texture.identity",
                           "configured texture names and requested/effective groups are invalid",
                         index);
        }
        if (input.removed_by_tuneup) {
          if (binding.source_receipt.initialized()) {
            return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                           "managed_material_declaration.texture.removed_source",
                           "Tuneup-removed material must not claim live source authority",
                           index);
          }
        } else {
          const ManagedMaterialTextureSourceIdentity *source_identity =
              binding.source_receipt.identity();
          if (source_identity == nullptr) {
            return Failure(ValidationCode::MISSING_REFERENCE,
                           "managed_material_declaration.texture.source_receipt",
                           "configured live texture has no immutable source bytes",
                           index);
          }
          if (source_identity->exact_resource_name !=
              binding.effective_texture_name ||
              source_identity->effective_resource_group !=
                  binding.effective_resource_group) {
            return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                           "managed_material_declaration.texture.source_identity",
                           "source receipt does not bind the resolved texture name and group",
                           index);
          }
          candidate->sources[index] = binding.source_receipt;
        }
      }

      ManagedMaterialTextureBindingMetadata &metadata =
          candidate->metadata.textures[index];
      metadata.slot = binding.slot;
      metadata.configured = binding.configured;
      metadata.declared_texture_name = binding.declared_texture_name;
      metadata.resolved_texture_name = binding.resolved_texture_name;
      metadata.effective_texture_name = binding.effective_texture_name;
      metadata.requested_resource_group = binding.requested_resource_group;
      metadata.effective_resource_group = binding.effective_resource_group;
      metadata.source_receipt_present =
          candidate->sources[index].initialized();
    }
    if (!candidate->metadata.textures[static_cast<std::size_t>(
             ManagedMaterialTextureSlot::DIFFUSE)]
             .configured) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "managed_material_declaration.texture.diffuse",
                     "managed material requires a configured diffuse texture");
    }
    const bool resolved_flexmesh =
        input.resolved_type == ManagedMaterialSemanticType::FLEXMESH_STANDARD ||
        input.resolved_type ==
            ManagedMaterialSemanticType::FLEXMESH_TRANSPARENT;
    if (!resolved_flexmesh &&
        candidate->metadata.textures[static_cast<std::size_t>(
            ManagedMaterialTextureSlot::DAMAGED_DIFFUSE)]
            .configured) {
      return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                     "managed_material_declaration.texture.damaged_diffuse",
                     "damaged diffuse is only defined for a resolved flexmesh material");
    }

    candidate->retained_identity_bytes =
        DeclarationIdentityBytes(candidate->metadata);
    if (candidate->retained_identity_bytes >
        configuration.maximum_retained_identity_bytes) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "managed_material_declaration.identity_bytes",
                     "declaration identity exceeds the configured retained bound");
    }
    candidate->metadata.canonical_identity_sha256 =
        ComputeDeclarationIdentityDigest(candidate->metadata,
                                         candidate->sources);
    if (IsZeroDigest(candidate->metadata.canonical_identity_sha256)) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "managed_material_declaration.canonical_identity_sha256",
                     "canonical declaration digest is unusable");
    }
    MaybeInject(
        ManagedMaterialDeclarationTransactionStage::BEFORE_DECLARATION_COMMIT,
        fault_injector);
    ManagedMaterialDeclaration staged(
        std::shared_ptr<const ManagedMaterialDeclaration::State>(
            std::move(candidate)));
    output = std::move(staged);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_declaration.allocation",
                   "allocation failed before declaration publication");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "managed_material_declaration.size",
                   "declaration exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_declaration.exception",
                   "unexpected exception before declaration publication");
  }
}

ManagedMaterialDeclarationRegistry::ManagedMaterialDeclarationRegistry(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool ManagedMaterialDeclarationRegistry::initialized() const noexcept {
  return state_ != nullptr;
}

bool ManagedMaterialDeclarationRegistry::active() const noexcept {
  return state_ != nullptr && state_->active;
}

std::uint64_t
ManagedMaterialDeclarationRegistry::actor_generation() const noexcept {
  return state_ != nullptr ? state_->actor_generation : 0U;
}

std::uint64_t
ManagedMaterialDeclarationRegistry::next_definition_generation() const
    noexcept {
  return state_ != nullptr && state_->active
             ? static_cast<std::uint64_t>(state_->declarations.size()) + 1U
             : 0U;
}

std::size_t ManagedMaterialDeclarationRegistry::size() const noexcept {
  return state_ != nullptr ? state_->declarations.size() : 0U;
}

std::uint64_t
ManagedMaterialDeclarationRegistry::retained_source_bytes() const noexcept {
  return state_ != nullptr ? state_->retained_source_bytes : 0U;
}

std::uint64_t
ManagedMaterialDeclarationRegistry::retained_identity_bytes() const noexcept {
  return state_ != nullptr ? state_->retained_identity_bytes : 0U;
}

bool ManagedMaterialDeclarationRegistry::SharesImmutableStateWith(
    const ManagedMaterialDeclarationRegistry &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult InitializeManagedMaterialDeclarationRegistry(
    const ManagedMaterialDeclarationRegistryConfiguration &configuration,
    std::uint64_t actor_generation,
    ManagedMaterialDeclarationRegistry &output) {
  const ValidationResult validation =
      ValidateManagedMaterialDeclarationRegistryConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  if (actor_generation == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "managed_material_registry.actor_generation",
                   "actor generation must be nonzero");
  }
  try {
    auto candidate = std::make_shared<ManagedMaterialDeclarationRegistry::State>();
    candidate->configuration = configuration;
    candidate->actor_generation = actor_generation;
    candidate->active = true;
    ManagedMaterialDeclarationRegistry staged(
        std::shared_ptr<const ManagedMaterialDeclarationRegistry::State>(
            std::move(candidate)));
    output = std::move(staged);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_registry.allocation",
                   "allocation failed before registry initialization");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_registry.exception",
                   "unexpected exception before registry initialization");
  }
}

ValidationResult CommitManagedMaterialDeclaration(
    const ManagedMaterialDeclaration &declaration,
    ManagedMaterialDeclarationRegistry &registry,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  if (registry.state_ == nullptr || !registry.state_->active) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "managed_material_registry.active_generation",
                   "no active actor generation can accept declarations");
  }
  const ManagedMaterialDeclarationMetadata *metadata = declaration.metadata();
  if (metadata == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "managed_material_registry.declaration",
                   "declaration is uninitialized");
  }
  if (metadata->actor_generation != registry.state_->actor_generation ||
      metadata->definition_generation !=
          registry.next_definition_generation()) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "managed_material_registry.generation",
                   "declaration does not target the exact next actor definition generation");
  }
  if (registry.state_->declarations.find(metadata->exact_material_name) !=
      registry.state_->declarations.end()) {
    return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                   "managed_material_registry.exact_material_name",
                   "managed material name is already published");
  }
  if (registry.state_->declarations.size() >=
      registry.state_->configuration.maximum_declarations) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "managed_material_registry.maximum_declarations",
                   "actor declaration count reached the configured bound");
  }

  try {
    auto candidate =
        std::make_shared<ManagedMaterialDeclarationRegistry::State>(
            *registry.state_);

    std::map<std::string, RenderPayloadDigest> existing_source_identities;
    for (const auto &entry : candidate->declarations) {
      for (std::size_t slot = 0U; slot < kManagedMaterialTextureSlotCount;
           ++slot) {
        const auto *receipt = entry.second.source_receipt(
            static_cast<ManagedMaterialTextureSlot>(slot));
        if (receipt == nullptr) {
          continue;
        }
        existing_source_identities.emplace(
            SourceCollisionKey(*receipt->identity()),
            receipt->identity()->source_sha256);
      }
    }
    for (std::size_t slot = 0U; slot < kManagedMaterialTextureSlotCount;
         ++slot) {
      const auto *receipt = declaration.source_receipt(
          static_cast<ManagedMaterialTextureSlot>(slot));
      if (receipt == nullptr) {
        continue;
      }
      const std::string key = SourceCollisionKey(*receipt->identity());
      const auto existing = existing_source_identities.find(key);
      if (existing != existing_source_identities.end() &&
          existing->second != receipt->identity()->source_sha256) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "managed_material_registry.source_identity_collision",
                       "one source identity resolves to different bytes");
      }
      existing_source_identities.emplace(key,
                                         receipt->identity()->source_sha256);
    }

    candidate->declarations.emplace(metadata->exact_material_name,
                                    declaration);
    candidate->retained_source_bytes = 0U;
    candidate->retained_identity_bytes = 0U;
    std::vector<const ManagedMaterialTextureSourceReceipt *> unique_receipts;
    for (const auto &entry : candidate->declarations) {
      if (!AddChecked(entry.second.retained_identity_bytes(),
                      candidate->retained_identity_bytes)) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "managed_material_registry.identity_bytes",
                       "retained identity byte count overflowed");
      }
      for (std::size_t slot = 0U; slot < kManagedMaterialTextureSlotCount;
           ++slot) {
        const auto *receipt = entry.second.source_receipt(
            static_cast<ManagedMaterialTextureSlot>(slot));
        if (receipt == nullptr) {
          continue;
        }
        const bool already_counted = std::any_of(
            unique_receipts.begin(), unique_receipts.end(),
            [receipt](const ManagedMaterialTextureSourceReceipt *other) {
              return receipt->SharesImmutableStateWith(*other);
            });
        if (already_counted) {
          continue;
        }
        unique_receipts.push_back(receipt);
        if (!AddChecked(static_cast<std::uint64_t>(receipt->source_size()),
                        candidate->retained_source_bytes) ||
            !AddChecked(receipt->retained_identity_bytes(),
                        candidate->retained_identity_bytes)) {
          return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                         "managed_material_registry.retained_bytes",
                         "retained source or identity byte count overflowed");
        }
      }
    }
    if (candidate->retained_source_bytes >
            candidate->configuration.maximum_retained_source_bytes ||
        candidate->retained_identity_bytes >
            candidate->configuration.maximum_retained_identity_bytes) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "managed_material_registry.retained_bytes",
                     "candidate publication exceeds configured retained bounds");
    }

    MaybeInject(
        ManagedMaterialDeclarationTransactionStage::BEFORE_REGISTRY_COMMIT,
        fault_injector);
    registry.state_ =
        std::shared_ptr<const ManagedMaterialDeclarationRegistry::State>(
            std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_registry.allocation",
                   "allocation failed before registry publication");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "managed_material_registry.size",
                   "registry candidate exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_registry.exception",
                   "unexpected exception before registry publication");
  }
}

ValidationResult ResetManagedMaterialDeclarationRegistry(
    std::uint64_t exact_actor_generation,
    std::uint64_t new_actor_generation,
    ManagedMaterialDeclarationRegistry &registry,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  if (registry.state_ == nullptr || !registry.state_->active ||
      exact_actor_generation == 0U ||
      registry.state_->actor_generation != exact_actor_generation ||
      new_actor_generation <= exact_actor_generation) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "managed_material_registry.generation_reset",
                   "reset does not advance the exact active actor generation");
  }
  try {
    auto candidate =
        std::make_shared<ManagedMaterialDeclarationRegistry::State>();
    candidate->configuration = registry.state_->configuration;
    candidate->actor_generation = new_actor_generation;
    candidate->active = true;
    MaybeInject(
        ManagedMaterialDeclarationTransactionStage::BEFORE_GENERATION_TRANSITION_COMMIT,
        fault_injector);
    registry.state_ =
        std::shared_ptr<const ManagedMaterialDeclarationRegistry::State>(
            std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_registry.reset_allocation",
                   "allocation failed before generation reset");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_registry.reset_exception",
                   "unexpected exception before generation reset");
  }
}

ValidationResult TeardownManagedMaterialDeclarationRegistry(
    std::uint64_t exact_actor_generation,
    ManagedMaterialDeclarationRegistry &registry,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  if (registry.state_ == nullptr || !registry.state_->active ||
      exact_actor_generation == 0U ||
      registry.state_->actor_generation != exact_actor_generation) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "managed_material_registry.generation_teardown",
                   "teardown does not target the exact active actor generation");
  }
  try {
    MaybeInject(
        ManagedMaterialDeclarationTransactionStage::BEFORE_GENERATION_TRANSITION_COMMIT,
        fault_injector);
    registry.state_.reset();
    return ValidationResult::Success();
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_registry.teardown_exception",
                   "unexpected exception before generation teardown");
  }
}

bool RevokeManagedMaterialDeclarationRegistry(
    std::uint64_t exact_actor_generation,
    ManagedMaterialDeclarationRegistry &registry) noexcept {
  if (registry.state_ == nullptr || !registry.state_->active ||
      exact_actor_generation == 0U ||
      registry.state_->actor_generation != exact_actor_generation) {
    return false;
  }
  registry.state_.reset();
  return true;
}

ManagedMaterialDeclarationSnapshot::ManagedMaterialDeclarationSnapshot(
    std::shared_ptr<const ManagedMaterialDeclarationRegistry::State> state)
    noexcept
    : state_(std::move(state)) {}

bool ManagedMaterialDeclarationSnapshot::initialized() const noexcept {
  return state_ != nullptr;
}

std::uint32_t ManagedMaterialDeclarationSnapshot::version() const noexcept {
  return state_ != nullptr ? kManagedMaterialDeclarationSnapshotVersion : 0U;
}

std::uint64_t
ManagedMaterialDeclarationSnapshot::actor_generation() const noexcept {
  return state_ != nullptr ? state_->actor_generation : 0U;
}

std::size_t ManagedMaterialDeclarationSnapshot::size() const noexcept {
  return state_ != nullptr ? state_->declarations.size() : 0U;
}

const ManagedMaterialDeclaration *ManagedMaterialDeclarationSnapshot::at(
    std::size_t index) const noexcept {
  if (state_ == nullptr || index >= state_->declarations.size()) {
    return nullptr;
  }
  auto iterator = state_->declarations.begin();
  std::advance(iterator, static_cast<std::ptrdiff_t>(index));
  return &iterator->second;
}

const ManagedMaterialDeclaration *ManagedMaterialDeclarationSnapshot::Find(
    const std::string &exact_material_name) const noexcept {
  if (state_ == nullptr) {
    return nullptr;
  }
  const auto iterator = state_->declarations.find(exact_material_name);
  return iterator != state_->declarations.end() ? &iterator->second : nullptr;
}

std::uint64_t
ManagedMaterialDeclarationSnapshot::retained_source_bytes() const noexcept {
  return state_ != nullptr ? state_->retained_source_bytes : 0U;
}

std::uint64_t
ManagedMaterialDeclarationSnapshot::retained_identity_bytes() const noexcept {
  return state_ != nullptr ? state_->retained_identity_bytes : 0U;
}

bool ManagedMaterialDeclarationSnapshot::SharesImmutableStateWith(
    const ManagedMaterialDeclarationSnapshot &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult CaptureManagedMaterialDeclarationSnapshot(
    const ManagedMaterialDeclarationRegistry &registry,
    ManagedMaterialDeclarationSnapshot &output,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  if (registry.state_ == nullptr || !registry.state_->active) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "managed_material_snapshot.active_generation",
                   "no active actor generation can be captured");
  }
  try {
    MaybeInject(
        ManagedMaterialDeclarationTransactionStage::BEFORE_SNAPSHOT_COMMIT,
        fault_injector);
    ManagedMaterialDeclarationSnapshot candidate(registry.state_);
    output = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_snapshot.allocation",
                   "allocation failed before snapshot publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_snapshot.exception",
                   "unexpected exception before snapshot publication");
  }
}

bool IsManagedMaterialDeclarationSnapshotCurrent(
    const ManagedMaterialDeclarationRegistry &registry,
    const ManagedMaterialDeclarationSnapshot &snapshot) noexcept {
  return registry.state_ != nullptr && registry.state_->active &&
         snapshot.state_ != nullptr &&
         registry.state_ == snapshot.state_;
}

} // namespace RoR::Render
