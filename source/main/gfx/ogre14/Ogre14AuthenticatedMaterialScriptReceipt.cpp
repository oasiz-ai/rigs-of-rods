/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14AuthenticatedMaterialScriptReceipt.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t element = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, element);
}

bool IsIdentifier(const std::string &value, bool allow_empty = false) noexcept {
  return (allow_empty || !value.empty()) &&
         value.size() <=
             kOgre14AuthenticatedMaterialScriptMaximumIdentifierBytes &&
         value.find('\0') == std::string::npos;
}

bool IsSha256(const std::string &value) noexcept {
  if (value.size() != 64U) {
    return false;
  }
  for (char character : value) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool IsKnownRole(Ogre14MaterialScriptSourceRole role) noexcept {
  switch (role) {
  case Ogre14MaterialScriptSourceRole::ROOT_SCRIPT:
  case Ogre14MaterialScriptSourceRole::COMPILER_DEPENDENCY:
    return true;
  }
  return false;
}

bool IsKnownRepairState(Ogre14MaterialScriptRepairState state) noexcept {
  switch (state) {
  case Ogre14MaterialScriptRepairState::NONE:
  case Ogre14MaterialScriptRepairState::APPLIED:
    return true;
  }
  return false;
}

std::string Sha256(const std::vector<std::uint8_t> &bytes) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0U;
  if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &digest_size,
                 EVP_sha256(), nullptr) != 1 ||
      digest_size != 32U) {
    throw std::runtime_error("SHA-256 computation failed");
  }
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output(64U, '0');
  for (std::size_t index = 0U; index < 32U; ++index) {
    output[index * 2U] = kHex[digest[index] >> 4U];
    output[index * 2U + 1U] = kHex[digest[index] & 0x0fU];
  }
  return output;
}

bool AddBytes(std::uint64_t value, std::uint64_t &total) noexcept {
  if (value > (std::numeric_limits<std::uint64_t>::max)() - total) {
    return false;
  }
  total += value;
  return true;
}

std::uint64_t SourceIdentityBytes(
    const Ogre14AuthenticatedMaterialScriptSourceMetadata &metadata) noexcept {
  const std::array<const std::string *, 14U> strings = {
      &metadata.effective_group,
      &metadata.root_script_request,
      &metadata.compiler_file_identity,
      &metadata.archive_source_identity,
      &metadata.selected_archive_name,
      &metadata.selected_archive_type,
      &metadata.archive_sha256,
      &metadata.file_info_filename,
      &metadata.file_info_path,
      &metadata.file_info_basename,
      &metadata.exact_member_name,
      &metadata.original_sha256,
      &metadata.effective_sha256,
      &metadata.repair_plan_sha256};
  std::uint64_t total = 0U;
  for (const std::string *value : strings) {
    if (!AddBytes(static_cast<std::uint64_t>(value->size()), total)) {
      return (std::numeric_limits<std::uint64_t>::max)();
    }
  }
  return total;
}

std::uint64_t BindingIdentityBytes(
    const Ogre14AuthenticatedMaterialScriptBindingMetadata &binding) noexcept {
  std::uint64_t total = 0U;
  for (const std::string *value :
       {&binding.exact_material_name, &binding.exact_group,
        &binding.exact_origin}) {
    if (!AddBytes(static_cast<std::uint64_t>(value->size()), total)) {
      return (std::numeric_limits<std::uint64_t>::max)();
    }
  }
  return total;
}

struct MaterialKey final {
  std::string group;
  std::uintptr_t pointer_token = 0U;
  std::uint64_t handle = 0U;
  std::string name;

  bool operator<(const MaterialKey &other) const noexcept {
    return std::tie(group, pointer_token, handle, name) <
           std::tie(other.group, other.pointer_token, other.handle,
                    other.name);
  }
};

struct MaterialNameKey final {
  std::string group;
  std::string name;

  bool operator<(const MaterialNameKey &other) const noexcept {
    return std::tie(group, name) < std::tie(other.group, other.name);
  }
};

ValidationResult ValidateConfiguration(
    const Ogre14AuthenticatedMaterialScriptRegistryConfiguration &config) {
  if (config.version !=
      kOgre14AuthenticatedMaterialScriptRegistryVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "material_script_registry.version",
                   "unsupported material-script registry version");
  }
  if (config.maximum_live_receipts == 0U ||
      config.maximum_live_receipts >
          kOgre14AuthenticatedMaterialScriptMaximumLiveReceipts ||
      config.maximum_live_sources == 0U ||
      config.maximum_live_sources >
          kOgre14AuthenticatedMaterialScriptMaximumLiveSources ||
      config.maximum_group_records == 0U ||
      config.maximum_group_records >
          kOgre14AuthenticatedMaterialScriptMaximumGroupRecords ||
      config.maximum_source_bytes == 0U ||
      config.maximum_source_bytes >
          kOgre14AuthenticatedMaterialScriptMaximumSourceBytes ||
      config.maximum_retained_source_bytes == 0U ||
      config.maximum_retained_source_bytes >
          kOgre14AuthenticatedMaterialScriptMaximumRetainedBytes ||
      config.maximum_total_identity_bytes == 0U ||
      config.maximum_total_identity_bytes >
          kOgre14AuthenticatedMaterialScriptMaximumIdentityBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.configuration",
                   "material-script registry limit is zero or exceeds the hard cap");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateSource(
    const Ogre14AuthenticatedMaterialScriptRegistryConfiguration &config,
    const Ogre14AuthenticatedMaterialScriptSourceInput &input,
    std::size_t index) {
  const auto &metadata = input.metadata;
  if (metadata.version !=
      kOgre14AuthenticatedMaterialScriptSourceMetadataVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "material_script_source.version",
                   "unsupported material-script source metadata version",
                   index);
  }
  if (!IsKnownRole(metadata.source_role) ||
      !IsKnownRepairState(metadata.repair_state)) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "material_script_source.state",
                   "material-script source role or repair state is invalid",
                   index);
  }
  if (metadata.parse_token == 0U || metadata.source_open_ordinal == 0U ||
      metadata.group_generation == 0U ||
      metadata.archive_pointer_token == 0U) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "material_script_source.identity",
                   "parse, source, generation, and archive identities must be nonzero",
                   index);
  }
  const std::array<const std::string *, 13U> required = {
      &metadata.effective_group,
      &metadata.root_script_request,
      &metadata.compiler_file_identity,
      &metadata.archive_source_identity,
      &metadata.selected_archive_name,
      &metadata.selected_archive_type,
      &metadata.archive_sha256,
      &metadata.file_info_filename,
      &metadata.file_info_basename,
      &metadata.exact_member_name,
      &metadata.original_sha256,
      &metadata.effective_sha256,
      &metadata.repair_plan_sha256};
  for (const std::string *value : required) {
    if (!IsIdentifier(*value)) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "material_script_source.identifier",
                     "required material-script source identifier is invalid",
                     index);
    }
  }
  if (!IsIdentifier(metadata.file_info_path, true) ||
      metadata.exact_member_name !=
          metadata.file_info_path + metadata.file_info_basename) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "material_script_source.exact_member_name",
                   "exact member must equal FileInfo path plus basename",
                   index);
  }
  if (!IsSha256(metadata.archive_sha256) ||
      !IsSha256(metadata.original_sha256) ||
      !IsSha256(metadata.effective_sha256) ||
      !IsSha256(metadata.repair_plan_sha256)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "material_script_source.sha256",
                   "material-script digests must be lowercase SHA-256",
                   index);
  }
  if (!input.authenticated_archive_snapshot.initialized() ||
      input.authenticated_archive_snapshot.source_archive_identity() !=
          metadata.archive_source_identity ||
      input.authenticated_archive_snapshot.archive_sha256() !=
          metadata.archive_sha256) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "material_script_source.archive_snapshot",
                   "metadata does not match the immutable archive snapshot owner",
                   index);
  }
  if (!input.original_bytes || !input.effective_bytes) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_script_source.byte_owner",
                   "source byte owners must be immutable and nonnull", index);
  }
  if (input.original_bytes->size() > config.maximum_source_bytes ||
      input.effective_bytes->size() > config.maximum_source_bytes ||
      metadata.original_byte_count != input.original_bytes->size() ||
      metadata.effective_byte_count != input.effective_bytes->size() ||
      metadata.uncompressed_size != metadata.original_byte_count) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "material_script_source.bytes",
                   "source byte counts or archive uncompressed size disagree",
                   index);
  }
  if (Sha256(*input.original_bytes) != metadata.original_sha256 ||
      Sha256(*input.effective_bytes) != metadata.effective_sha256) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "material_script_source.bytes_sha256",
                   "source bytes do not match their authenticated digests",
                   index);
  }
  if (metadata.repair_plan_version !=
          kOgre14AuthenticatedMaterialScriptRepairPlanVersion ||
      (metadata.repair_state == Ogre14MaterialScriptRepairState::NONE &&
       (metadata.applied_edit_count != 0U ||
        *input.original_bytes != *input.effective_bytes)) ||
      (metadata.repair_state == Ogre14MaterialScriptRepairState::APPLIED &&
       (metadata.applied_edit_count == 0U ||
        *input.original_bytes == *input.effective_bytes))) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "material_script_source.repair",
                   "repair state, version, edit count, or payload transition disagree",
                   index);
  }
  return ValidationResult::Success();
}

} // namespace

struct Ogre14AuthenticatedMaterialScriptReceipt::SourceState final {
  Ogre14AuthenticatedMaterialScriptSourceMetadata metadata;
  TerrainBundleAuthenticatedArchiveSnapshot archive_snapshot;
  std::shared_ptr<const std::vector<std::uint8_t>> original_bytes;
  std::shared_ptr<const std::vector<std::uint8_t>> effective_bytes;
};

struct Ogre14AuthenticatedMaterialScriptReceipt::SourceClosureState final {
  std::vector<std::shared_ptr<const SourceState>> sources;
};

struct Ogre14AuthenticatedMaterialScriptReceipt::State final {
  std::uint32_t version =
      kOgre14AuthenticatedMaterialScriptReceiptVersion;
  std::shared_ptr<const SourceClosureState> source_closure;
  std::size_t primary_source_index = 0U;
  Ogre14AuthenticatedMaterialScriptBindingMetadata binding;
};

struct Ogre14AuthenticatedMaterialScriptRegistry::State final {
  struct GroupGeneration final {
    std::uint64_t generation = 0U;
    bool committed = false;
  };

  Ogre14AuthenticatedMaterialScriptRegistryConfiguration configuration;
  std::map<std::string, GroupGeneration> group_generations;
  std::map<MaterialKey, Ogre14AuthenticatedMaterialScriptReceipt> receipts;
  std::size_t source_count = 0U;
  std::uint64_t retained_source_bytes = 0U;
  std::uint64_t total_identity_bytes = 0U;
  std::uint64_t maximum_group_generation_seen = 0U;
};

struct Ogre14AuthenticatedMaterialScriptResolution::State final {
  std::uint32_t version =
      kOgre14AuthenticatedMaterialScriptResolutionVersion;
  std::shared_ptr<const Ogre14AuthenticatedMaterialScriptRegistry::State>
      registry_state;
  Ogre14AuthenticatedMaterialScriptReceipt receipt;
  std::uintptr_t resolver_pointer_token = 0U;
};

Ogre14AuthenticatedMaterialScriptAuthoritySnapshot::
    Ogre14AuthenticatedMaterialScriptAuthoritySnapshot(
        Ogre14AuthenticatedMaterialScriptRegistry registry_snapshot,
        std::uintptr_t resolver_pointer_token) noexcept
    : version_(
          kOgre14AuthenticatedMaterialScriptAuthoritySnapshotVersion),
      registry_snapshot_(std::move(registry_snapshot)),
      resolver_pointer_token_(resolver_pointer_token) {}

void Ogre14AuthenticatedMaterialScriptRegistry::RecomputeAccounting(
    State &state) {
  std::set<const Ogre14AuthenticatedMaterialScriptReceipt::SourceState *>
      unique_sources;
  std::uint64_t retained = 0U;
  std::uint64_t identity = 0U;
  for (const auto &group : state.group_generations) {
    if (!AddBytes(static_cast<std::uint64_t>(group.first.size()), identity)) {
      throw std::length_error("material group identity accounting overflow");
    }
  }
  for (const auto &entry : state.receipts) {
    const auto &receipt_state = entry.second.state_;
    if (!receipt_state || !receipt_state->source_closure) {
      throw std::logic_error("registry contains an empty receipt");
    }
    // State retains group/name twice: once in the immutable binding and once
    // in the ordered MaterialKey. Charge both physical owners rather than
    // advertising a merely logical metadata budget.
    if (!AddBytes(BindingIdentityBytes(receipt_state->binding), identity) ||
        !AddBytes(static_cast<std::uint64_t>(entry.first.group.size()),
                  identity) ||
        !AddBytes(static_cast<std::uint64_t>(entry.first.name.size()),
                  identity)) {
      throw std::length_error("material identity accounting overflow");
    }
    for (const auto &source : receipt_state->source_closure->sources) {
      if (unique_sources.insert(source.get()).second) {
        if (!source->original_bytes || !source->effective_bytes ||
            !AddBytes(static_cast<std::uint64_t>(source->original_bytes->size()),
                      retained) ||
            !AddBytes(static_cast<std::uint64_t>(source->effective_bytes->size()),
                      retained) ||
            !AddBytes(SourceIdentityBytes(source->metadata), identity)) {
          throw std::length_error("material source accounting overflow");
        }
      }
    }
  }
  state.source_count = unique_sources.size();
  state.retained_source_bytes = retained;
  state.total_identity_bytes = identity;
}

ValidationResult Ogre14AuthenticatedMaterialScriptRegistry::CheckAccounting(
    const State &state) {
  if (state.receipts.size() >
          state.configuration.maximum_live_receipts ||
      state.source_count > state.configuration.maximum_live_sources ||
      state.group_generations.size() >
          state.configuration.maximum_group_records ||
      state.retained_source_bytes >
          state.configuration.maximum_retained_source_bytes ||
      state.total_identity_bytes >
          state.configuration.maximum_total_identity_bytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.capacity",
                   "material-script registry capacity would be exceeded");
  }
  return ValidationResult::Success();
}

Ogre14AuthenticatedMaterialScriptReceipt::
    Ogre14AuthenticatedMaterialScriptReceipt(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14AuthenticatedMaterialScriptReceipt::initialized() const noexcept {
  return state_ != nullptr;
}

const Ogre14AuthenticatedMaterialScriptSourceMetadata *
Ogre14AuthenticatedMaterialScriptReceipt::source_metadata() const noexcept {
  return source_metadata_at(primary_source_index());
}

std::size_t
Ogre14AuthenticatedMaterialScriptReceipt::source_count() const noexcept {
  return state_ && state_->source_closure
             ? state_->source_closure->sources.size()
             : 0U;
}

std::size_t Ogre14AuthenticatedMaterialScriptReceipt::primary_source_index()
    const noexcept {
  return state_ ? state_->primary_source_index : 0U;
}

const Ogre14AuthenticatedMaterialScriptSourceMetadata *
Ogre14AuthenticatedMaterialScriptReceipt::source_metadata_at(
    std::size_t index) const noexcept {
  if (!state_ || !state_->source_closure ||
      index >= state_->source_closure->sources.size()) {
    return nullptr;
  }
  return &state_->source_closure->sources[index]->metadata;
}

const Ogre14AuthenticatedMaterialScriptBindingMetadata *
Ogre14AuthenticatedMaterialScriptReceipt::binding_metadata() const noexcept {
  return state_ ? &state_->binding : nullptr;
}

const std::uint8_t *
Ogre14AuthenticatedMaterialScriptReceipt::original_bytes() const noexcept {
  return original_bytes_at(primary_source_index());
}

std::size_t
Ogre14AuthenticatedMaterialScriptReceipt::original_size() const noexcept {
  return original_size_at(primary_source_index());
}

const std::uint8_t *
Ogre14AuthenticatedMaterialScriptReceipt::original_bytes_at(
    std::size_t index) const noexcept {
  if (!state_ || !state_->source_closure ||
      index >= state_->source_closure->sources.size()) {
    return nullptr;
  }
  const auto &bytes = state_->source_closure->sources[index]->original_bytes;
  return !bytes || bytes->empty() ? nullptr : bytes->data();
}

std::size_t Ogre14AuthenticatedMaterialScriptReceipt::original_size_at(
    std::size_t index) const noexcept {
  return state_ && state_->source_closure &&
                 index < state_->source_closure->sources.size()
             ? state_->source_closure->sources[index]->original_bytes->size()
             : 0U;
}

const std::uint8_t *
Ogre14AuthenticatedMaterialScriptReceipt::effective_bytes() const noexcept {
  return effective_bytes_at(primary_source_index());
}

std::size_t
Ogre14AuthenticatedMaterialScriptReceipt::effective_size() const noexcept {
  return effective_size_at(primary_source_index());
}

const std::uint8_t *
Ogre14AuthenticatedMaterialScriptReceipt::effective_bytes_at(
    std::size_t index) const noexcept {
  if (!state_ || !state_->source_closure ||
      index >= state_->source_closure->sources.size()) {
    return nullptr;
  }
  const auto &bytes = state_->source_closure->sources[index]->effective_bytes;
  return !bytes || bytes->empty() ? nullptr : bytes->data();
}

std::size_t Ogre14AuthenticatedMaterialScriptReceipt::effective_size_at(
    std::size_t index) const noexcept {
  return state_ && state_->source_closure &&
                 index < state_->source_closure->sources.size()
             ? state_->source_closure->sources[index]->effective_bytes->size()
             : 0U;
}

const TerrainBundleAuthenticatedArchiveSnapshot *
Ogre14AuthenticatedMaterialScriptReceipt::authenticated_archive_snapshot()
    const noexcept {
  return authenticated_archive_snapshot_at(primary_source_index());
}

const TerrainBundleAuthenticatedArchiveSnapshot *
Ogre14AuthenticatedMaterialScriptReceipt::authenticated_archive_snapshot_at(
    std::size_t index) const noexcept {
  return state_ && state_->source_closure &&
                 index < state_->source_closure->sources.size()
             ? &state_->source_closure->sources[index]->archive_snapshot
             : nullptr;
}

bool Ogre14AuthenticatedMaterialScriptReceipt::SharesImmutableStateWith(
    const Ogre14AuthenticatedMaterialScriptReceipt &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

bool Ogre14AuthenticatedMaterialScriptReceipt::SharesSourceStateWith(
    const Ogre14AuthenticatedMaterialScriptReceipt &other) const noexcept {
  return state_ && other.state_ && state_->source_closure &&
         state_->source_closure == other.state_->source_closure;
}

Ogre14AuthenticatedMaterialScriptRegistry::
    Ogre14AuthenticatedMaterialScriptRegistry(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14AuthenticatedMaterialScriptRegistry::initialized() const noexcept {
  return state_ != nullptr;
}

std::size_t Ogre14AuthenticatedMaterialScriptRegistry::size() const noexcept {
  return state_ ? state_->receipts.size() : 0U;
}

std::size_t
Ogre14AuthenticatedMaterialScriptRegistry::source_count() const noexcept {
  return state_ ? state_->source_count : 0U;
}

std::uint64_t
Ogre14AuthenticatedMaterialScriptRegistry::retained_source_bytes()
    const noexcept {
  return state_ ? state_->retained_source_bytes : 0U;
}

std::uint64_t
Ogre14AuthenticatedMaterialScriptRegistry::retained_identity_bytes()
    const noexcept {
  return state_ ? state_->total_identity_bytes : 0U;
}

std::uint64_t
Ogre14AuthenticatedMaterialScriptRegistry::maximum_group_generation_seen()
    const noexcept {
  return state_ ? state_->maximum_group_generation_seen : 0U;
}

bool Ogre14AuthenticatedMaterialScriptRegistry::SharesImmutableStateWith(
    const Ogre14AuthenticatedMaterialScriptRegistry &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult Ogre14AuthenticatedMaterialScriptRegistry::Initialize(
    const Ogre14AuthenticatedMaterialScriptRegistryConfiguration &config) {
  try {
    const ValidationResult valid = ValidateConfiguration(config);
    if (!valid) {
      return valid;
    }
    auto candidate = std::make_shared<State>();
    candidate->configuration = config;
    state_ = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.allocation",
                   "material-script registry allocation failed");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.capacity",
                   "material-script registry capacity overflowed");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "material_script_registry.internal",
                   "unexpected material-script registry failure");
  }
}

ValidationResult
Ogre14AuthenticatedMaterialScriptRegistry::AdvanceGroupGeneration(
    const std::string &group, std::uint64_t generation) {
  try {
    if (!state_) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_script_registry.state",
                     "material-script registry is not initialized");
    }
    if (!IsIdentifier(group) || generation == 0U ||
        generation <= state_->maximum_group_generation_seen) {
      return Failure(ValidationCode::SEQUENCE_MISMATCH,
                     "material_script_registry.group_generation",
                     "group generation must be a new nonzero global sequence");
    }
    const bool group_is_new =
        state_->group_generations.find(group) == state_->group_generations.end();
    if (group_is_new &&
        (group.size() > state_->configuration.maximum_total_identity_bytes ||
         state_->total_identity_bytes >
             state_->configuration.maximum_total_identity_bytes -
                 static_cast<std::uint64_t>(group.size()))) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "material_script_registry.group_identity",
                     "group identity exceeds configured retained capacity");
    }
    auto candidate = std::make_shared<State>(*state_);
    for (auto entry = candidate->receipts.begin();
         entry != candidate->receipts.end();) {
      if (entry->first.group == group) {
        entry = candidate->receipts.erase(entry);
      } else {
        ++entry;
      }
    }
    candidate->group_generations[group] = {generation, false};
    candidate->maximum_group_generation_seen = generation;
    RecomputeAccounting(*candidate);
    const ValidationResult capacity = CheckAccounting(*candidate);
    if (!capacity) {
      return capacity;
    }
    state_ = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.allocation",
                   "group-generation allocation failed");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.capacity",
                   "group-generation capacity overflowed");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "material_script_registry.internal",
                   "unexpected group-generation failure");
  }
}

ValidationResult Ogre14AuthenticatedMaterialScriptRegistry::TeardownGroup(
    const std::string &group, std::uint64_t generation) {
  try {
    if (!state_) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_script_registry.state",
                     "material-script registry is not initialized");
    }
    const auto current = state_->group_generations.find(group);
    if (!IsIdentifier(group) ||
        current == state_->group_generations.end() ||
        current->second.generation != generation) {
      return Failure(ValidationCode::SEQUENCE_MISMATCH,
                     "material_script_registry.group_teardown",
                     "group teardown does not target the current generation");
    }
    auto candidate = std::make_shared<State>(*state_);
    for (auto entry = candidate->receipts.begin();
         entry != candidate->receipts.end();) {
      if (entry->first.group == group) {
        entry = candidate->receipts.erase(entry);
      } else {
        ++entry;
      }
    }
    candidate->group_generations.erase(group);
    // maximum_group_generation_seen is process-global provenance and is never
    // rewound or reused when ephemeral groups release their retained records.
    RecomputeAccounting(*candidate);
    const ValidationResult capacity = CheckAccounting(*candidate);
    if (!capacity) {
      return capacity;
    }
    state_ = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.allocation",
                   "group-teardown allocation failed");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.capacity",
                   "group-teardown accounting overflowed");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "material_script_registry.internal",
                   "unexpected group-teardown failure");
  }
}

ValidationResult Ogre14AuthenticatedMaterialScriptRegistry::CommitWholeGroup(
    const std::string &group, std::uint64_t generation,
    const std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> &sources,
    const std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput>
        &materials,
    IOgre14AuthenticatedMaterialScriptCommitFaultInjector *fault_injector) {
  try {
    if (!state_) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_script_registry.state",
                     "material-script registry is not initialized");
    }
    const auto group_generation = state_->group_generations.find(group);
    if (!IsIdentifier(group) ||
        group_generation == state_->group_generations.end() ||
        group_generation->second.generation != generation) {
      return Failure(ValidationCode::SEQUENCE_MISMATCH,
                     "material_script_registry.group_generation",
                     "group commit does not target the current generation");
    }
    if (sources.size() > state_->configuration.maximum_live_sources ||
        state_->source_count >
            state_->configuration.maximum_live_sources - sources.size() ||
        materials.size() > state_->configuration.maximum_live_receipts ||
        state_->receipts.size() >
            state_->configuration.maximum_live_receipts - materials.size()) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "material_script_registry.input_count",
                     "group input count exceeds configured capacity");
    }
    if (group_generation->second.committed) {
      return Failure(ValidationCode::SEQUENCE_MISMATCH,
                     "material_script_registry.group_commit",
                     "whole-group generation has already been published");
    }

    std::uint64_t prospective_retained_bytes =
        state_->retained_source_bytes;
    std::uint64_t prospective_identity_bytes = state_->total_identity_bytes;
    for (std::size_t index = 0U; index < sources.size(); ++index) {
      const auto &source = sources[index];
      if (!source.original_bytes || !source.effective_bytes) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "material_script_source.byte_owner",
                       "source byte owners must be immutable and nonnull",
                       index);
      }
      const std::uint64_t source_identity =
          SourceIdentityBytes(source.metadata);
      if (source.original_bytes->size() >
              state_->configuration.maximum_source_bytes ||
          source.effective_bytes->size() >
              state_->configuration.maximum_source_bytes ||
          !AddBytes(static_cast<std::uint64_t>(source.original_bytes->size()),
                    prospective_retained_bytes) ||
          !AddBytes(static_cast<std::uint64_t>(source.effective_bytes->size()),
                    prospective_retained_bytes) ||
          prospective_retained_bytes >
              state_->configuration.maximum_retained_source_bytes ||
          source_identity ==
              (std::numeric_limits<std::uint64_t>::max)() ||
          !AddBytes(source_identity, prospective_identity_bytes) ||
          prospective_identity_bytes >
              state_->configuration.maximum_total_identity_bytes) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "material_script_registry.input_capacity",
                       "group source bytes or identities exceed capacity",
                       index);
      }
    }
    for (std::size_t index = 0U; index < materials.size(); ++index) {
      const std::uint64_t material_identity =
          BindingIdentityBytes(materials[index].binding);
      std::uint64_t material_key_identity = 0U;
      if (!AddBytes(
              static_cast<std::uint64_t>(
                  materials[index].binding.exact_group.size()),
              material_key_identity) ||
          !AddBytes(
              static_cast<std::uint64_t>(
                  materials[index].binding.exact_material_name.size()),
              material_key_identity)) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "material_script_registry.input_capacity",
                       "group material-key identities exceed capacity", index);
      }
      if (material_identity ==
              (std::numeric_limits<std::uint64_t>::max)() ||
          !AddBytes(material_identity, prospective_identity_bytes) ||
          !AddBytes(material_key_identity, prospective_identity_bytes) ||
          prospective_identity_bytes >
              state_->configuration.maximum_total_identity_bytes) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "material_script_registry.input_capacity",
                       "group material identities exceed capacity", index);
      }
    }

    std::vector<std::shared_ptr<const
        Ogre14AuthenticatedMaterialScriptReceipt::SourceState>> source_states;
    source_states.reserve(sources.size());
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t>
        source_ordinals;
    std::map<std::pair<std::uint64_t, std::string>, std::size_t>
        compiler_sources;
    std::map<std::uint64_t, std::vector<std::size_t>> parse_sources;
    std::map<std::uint64_t, std::size_t> root_counts;
    for (std::size_t index = 0U; index < sources.size(); ++index) {
      const ValidationResult valid =
          ValidateSource(state_->configuration, sources[index], index);
      if (!valid) {
        return valid;
      }
      const auto &metadata = sources[index].metadata;
      if (metadata.effective_group != group ||
          metadata.group_generation != generation) {
        return Failure(ValidationCode::SEQUENCE_MISMATCH,
                       "material_script_source.group_generation",
                       "source belongs to another group generation", index);
      }
      if (!source_ordinals
               .emplace(std::make_pair(metadata.parse_token,
                                       metadata.source_open_ordinal),
                        index)
               .second ||
          !compiler_sources
               .emplace(std::make_pair(metadata.parse_token,
                                       metadata.compiler_file_identity),
                        index)
               .second) {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "material_script_source.identity",
                       "parse source ordinal or compiler-file identity is duplicated",
                       index);
      }
      if (metadata.source_role ==
          Ogre14MaterialScriptSourceRole::ROOT_SCRIPT) {
        ++root_counts[metadata.parse_token];
      }
      parse_sources[metadata.parse_token].push_back(index);
      auto source = std::make_shared<
          Ogre14AuthenticatedMaterialScriptReceipt::SourceState>();
      source->metadata = metadata;
      source->archive_snapshot =
          sources[index].authenticated_archive_snapshot;
      source->original_bytes = sources[index].original_bytes;
      source->effective_bytes = sources[index].effective_bytes;
      source_states.push_back(std::move(source));
    }
    for (const auto &parse : parse_sources) {
      if (root_counts[parse.first] != 1U ||
          parse.second.size() > 4096U) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "material_script_source.root",
                       "each parse must have one root and at most 4096 sources");
      }
    }

    std::map<std::uint64_t, std::shared_ptr<const
        Ogre14AuthenticatedMaterialScriptReceipt::SourceClosureState>>
        closures;
    std::map<std::uint64_t, std::map<std::size_t, std::size_t>>
        closure_indices;
    for (auto &parse : parse_sources) {
      std::sort(parse.second.begin(), parse.second.end(),
                [&sources](std::size_t left, std::size_t right) {
                  return sources[left].metadata.source_open_ordinal <
                         sources[right].metadata.source_open_ordinal;
                });
      const std::string &root_request =
          sources[parse.second.front()].metadata.root_script_request;
      for (std::size_t closure_index = 0U;
           closure_index < parse.second.size(); ++closure_index) {
        const auto &metadata = sources[parse.second[closure_index]].metadata;
        if (metadata.source_open_ordinal != closure_index + 1U ||
            metadata.root_script_request != root_request ||
            (closure_index == 0U &&
             (metadata.source_role !=
                  Ogre14MaterialScriptSourceRole::ROOT_SCRIPT ||
              metadata.compiler_file_identity != root_request)) ||
            (closure_index != 0U &&
             metadata.source_role !=
                 Ogre14MaterialScriptSourceRole::COMPILER_DEPENDENCY)) {
          return Failure(
              ValidationCode::NON_DETERMINISTIC_ORDER,
              "material_script_source.closure_order",
              "parse closure must begin with its root and use contiguous source ordinals");
        }
      }
      auto closure = std::make_shared<
          Ogre14AuthenticatedMaterialScriptReceipt::SourceClosureState>();
      closure->sources.reserve(parse.second.size());
      for (std::size_t closure_index = 0U;
           closure_index < parse.second.size(); ++closure_index) {
        const std::size_t source_index = parse.second[closure_index];
        closure->sources.push_back(source_states[source_index]);
        closure_indices[parse.first][source_index] = closure_index;
      }
      closures.emplace(parse.first, std::move(closure));
    }
    if (fault_injector != nullptr) {
      fault_injector->OnOgre14AuthenticatedMaterialScriptCommitFault(
          Ogre14AuthenticatedMaterialScriptCommitFaultPoint::
              AFTER_SOURCE_CANONICALIZATION);
    }

    auto candidate = std::make_shared<State>(*state_);
    std::set<MaterialNameKey> material_names;
    std::set<std::pair<std::uint64_t, std::uint64_t>> event_ordinals;
    std::map<std::uint64_t, std::vector<std::uint64_t>>
        events_by_parse;
    for (std::size_t index = 0U; index < materials.size(); ++index) {
      const auto &material = materials[index];
      if (material.source_index >= sources.size()) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "material_script_material.source_index",
                       "material references a missing primary source", index);
      }
      const auto &binding = material.binding;
      const auto &primary = sources[material.source_index].metadata;
      if (binding.version !=
              kOgre14AuthenticatedMaterialScriptBindingMetadataVersion ||
          binding.event_ordinal == 0U ||
          binding.material_pointer_token == 0U ||
          binding.material_handle == 0U) {
        return Failure(ValidationCode::INVALID_HANDLE,
                       "material_script_material.binding",
                       "material binding version or identity is invalid", index);
      }
      if (!IsIdentifier(binding.exact_material_name) ||
          !IsIdentifier(binding.exact_group) ||
          !IsIdentifier(binding.exact_origin) ||
          binding.exact_group != group ||
          binding.exact_origin != primary.compiler_file_identity) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "material_script_material.identity",
                       "material group or origin does not match its primary source",
                       index);
      }
      if (!material_names
               .insert({binding.exact_group, binding.exact_material_name})
               .second ||
          !event_ordinals
               .insert({primary.parse_token, binding.event_ordinal})
               .second) {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "material_script_material.identity",
                       "material name or event ordinal is duplicated", index);
      }
      events_by_parse[primary.parse_token].push_back(
          binding.event_ordinal);
      auto receipt_state =
          std::make_shared<Ogre14AuthenticatedMaterialScriptReceipt::State>();
      receipt_state->source_closure = closures.at(primary.parse_token);
      receipt_state->primary_source_index =
          closure_indices.at(primary.parse_token).at(material.source_index);
      receipt_state->binding = binding;
      Ogre14AuthenticatedMaterialScriptReceipt receipt(receipt_state);
      const MaterialKey key{binding.exact_group,
                            binding.material_pointer_token,
                            binding.material_handle,
                            binding.exact_material_name};
      if (!candidate->receipts.emplace(key, std::move(receipt)).second) {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "material_script_material.key",
                       "material pointer, handle, group, and name are duplicated",
                       index);
      }
    }
    for (auto &parse_events : events_by_parse) {
      std::sort(parse_events.second.begin(), parse_events.second.end());
      for (std::size_t index = 0U; index < parse_events.second.size(); ++index) {
        if (parse_events.second[index] != index + 1U) {
          return Failure(
              ValidationCode::NON_DETERMINISTIC_ORDER,
              "material_script_material.event_ordinal",
              "material event ordinals must be contiguous within a parse");
        }
      }
    }
    candidate->group_generations.at(group).committed = true;
    RecomputeAccounting(*candidate);
    const ValidationResult capacity = CheckAccounting(*candidate);
    if (!capacity) {
      return capacity;
    }
    if (fault_injector != nullptr) {
      fault_injector->OnOgre14AuthenticatedMaterialScriptCommitFault(
          Ogre14AuthenticatedMaterialScriptCommitFaultPoint::
              BEFORE_PUBLICATION);
    }
    state_ = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.allocation",
                   "whole-group material publication allocation failed");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.capacity",
                   "whole-group material publication overflowed");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "material_script_registry.internal",
                   "unexpected whole-group material publication failure");
  }
}

ValidationResult Ogre14AuthenticatedMaterialScriptRegistry::RemoveMaterial(
    const std::string &group, std::uintptr_t pointer_token,
    std::uint64_t handle, const std::string &name,
    const std::string &origin) {
  try {
    if (!state_) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_script_registry.state",
                     "material-script registry is not initialized");
    }
    const MaterialKey key{group, pointer_token, handle, name};
    const auto found = state_->receipts.find(key);
    if (found == state_->receipts.end()) {
      for (const auto &entry : state_->receipts) {
        if (entry.first.group == group &&
            (entry.first.pointer_token == pointer_token ||
             entry.first.handle == handle || entry.first.name == name)) {
          return Failure(ValidationCode::REVISION_MISMATCH,
                         "material_script_registry.remove_identity",
                         "material removal partially matches a current receipt");
        }
      }
      return ValidationResult::Success();
    }
    const auto *binding = found->second.binding_metadata();
    if (binding == nullptr || binding->exact_origin != origin) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "material_script_registry.remove_origin",
                     "material removal origin does not match the receipt");
    }
    auto candidate = std::make_shared<State>(*state_);
    candidate->receipts.erase(key);
    RecomputeAccounting(*candidate);
    state_ = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_registry.allocation",
                   "material removal allocation failed");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "material_script_registry.internal",
                   "unexpected material removal failure");
  }
}

void Ogre14AuthenticatedMaterialScriptRegistry::Poison() noexcept {
  state_.reset();
}

ValidationResult Ogre14AuthenticatedMaterialScriptRegistry::MintResolution(
    const std::string &group, std::uint64_t generation,
    std::uintptr_t pointer_token, std::uint64_t handle,
    const std::string &name, const std::string &origin,
    std::uintptr_t resolver_pointer_token,
    Ogre14AuthenticatedMaterialScriptResolution &resolution) const {
  try {
    if (!state_ || resolver_pointer_token == 0U) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_script_resolution.authority",
                     "registry or resolver authority is missing");
    }
    const auto current_generation = state_->group_generations.find(group);
    if (current_generation == state_->group_generations.end() ||
        current_generation->second.generation != generation ||
        !current_generation->second.committed) {
      return Failure(ValidationCode::SEQUENCE_MISMATCH,
                     "material_script_resolution.generation",
                     "material group generation is stale");
    }
    const auto found =
        state_->receipts.find({group, pointer_token, handle, name});
    if (found == state_->receipts.end() ||
        found->second.binding_metadata() == nullptr ||
        found->second.binding_metadata()->exact_origin != origin) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_script_resolution.receipt",
                     "no exact current material-script receipt exists");
    }
    auto candidate =
        std::make_shared<Ogre14AuthenticatedMaterialScriptResolution::State>();
    candidate->registry_state = state_;
    candidate->receipt = found->second;
    candidate->resolver_pointer_token = resolver_pointer_token;
    resolution = Ogre14AuthenticatedMaterialScriptResolution(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_script_resolution.allocation",
                   "material resolution allocation failed");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "material_script_resolution.internal",
                   "unexpected material resolution failure");
  }
}

bool Ogre14AuthenticatedMaterialScriptRegistry::RevalidateResolution(
    const Ogre14AuthenticatedMaterialScriptResolution &resolution,
    std::uintptr_t resolver_pointer_token, std::uintptr_t pointer_token,
    std::uint64_t handle, const std::string &group, const std::string &name,
    const std::string &origin) const noexcept {
  if (!state_ || !resolution.state_ ||
      resolution.state_->registry_state != state_ ||
      resolution.state_->resolver_pointer_token != resolver_pointer_token) {
    return false;
  }
  const auto *binding = resolution.state_->receipt.binding_metadata();
  if (binding == nullptr ||
      binding->material_pointer_token != pointer_token ||
      binding->material_handle != handle || binding->exact_group != group ||
      binding->exact_material_name != name || binding->exact_origin != origin) {
    return false;
  }
  const auto found =
      state_->receipts.find({group, pointer_token, handle, name});
  return found != state_->receipts.end() &&
         found->second.SharesImmutableStateWith(resolution.state_->receipt);
}

Ogre14AuthenticatedMaterialScriptResolution::
    Ogre14AuthenticatedMaterialScriptResolution(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14AuthenticatedMaterialScriptResolution::initialized() const noexcept {
  return state_ != nullptr;
}

const Ogre14AuthenticatedMaterialScriptReceipt *
Ogre14AuthenticatedMaterialScriptResolution::receipt() const noexcept {
  return state_ ? &state_->receipt : nullptr;
}

bool Ogre14AuthenticatedMaterialScriptResolution::SharesCurrentAuthorityWith(
    const Ogre14AuthenticatedMaterialScriptResolution &other) const noexcept {
  return state_ && other.state_ &&
         state_->registry_state == other.state_->registry_state &&
         state_->resolver_pointer_token == other.state_->resolver_pointer_token &&
         state_->receipt.SharesImmutableStateWith(other.state_->receipt);
}

bool Ogre14AuthenticatedMaterialScriptResolution::MatchesResolver(
    const IOgre14AuthenticatedMaterialScriptResolver &resolver) const
    noexcept {
  return initialized() &&
         state_->resolver_pointer_token ==
             reinterpret_cast<std::uintptr_t>(&resolver);
}

bool Ogre14AuthenticatedMaterialScriptAuthoritySnapshot::initialized() const
    noexcept {
  return version_ ==
             kOgre14AuthenticatedMaterialScriptAuthoritySnapshotVersion &&
         registry_snapshot_.initialized() && resolver_pointer_token_ != 0U;
}

std::uint32_t
Ogre14AuthenticatedMaterialScriptAuthoritySnapshot::version() const noexcept {
  return version_;
}

bool Ogre14AuthenticatedMaterialScriptAuthoritySnapshot::Authenticates(
    const Ogre14AuthenticatedMaterialScriptResolution &resolution) const
    noexcept {
  return initialized() && resolution.initialized() && resolution.state_ &&
         resolution.state_->registry_state == registry_snapshot_.state_ &&
         resolution.state_->resolver_pointer_token == resolver_pointer_token_;
}

bool Ogre14AuthenticatedMaterialScriptAuthoritySnapshot::
    SharesImmutableAuthorityWith(
        const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &other) const
    noexcept {
  return initialized() && other.initialized() &&
         registry_snapshot_.SharesImmutableStateWith(
             other.registry_snapshot_) &&
         resolver_pointer_token_ == other.resolver_pointer_token_;
}

ValidationResult
Ogre14AuthenticatedMaterialScriptRegistry::MintResolverAuthoritySnapshot(
    std::uintptr_t resolver_pointer_token,
    Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &snapshot) const {
  if (!state_) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_script_authority.registry",
                   "authenticated material-script registry is not initialized");
  }
  if (resolver_pointer_token == 0U) {
    return Failure(ValidationCode::INVALID_HANDLE,
                   "material_script_authority.resolver",
                   "authenticated material-script resolver identity is empty");
  }
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot candidate(
      *this, resolver_pointer_token);
  static_assert(std::is_nothrow_move_assignable_v<
                Ogre14AuthenticatedMaterialScriptAuthoritySnapshot>);
  snapshot = std::move(candidate);
  return ValidationResult::Success();
}

static_assert(std::is_nothrow_copy_constructible_v<
              Ogre14AuthenticatedMaterialScriptReceipt>);
static_assert(std::is_nothrow_move_assignable_v<
              Ogre14AuthenticatedMaterialScriptReceipt>);
static_assert(std::is_nothrow_copy_constructible_v<
              Ogre14AuthenticatedMaterialScriptRegistry>);
static_assert(std::is_nothrow_move_assignable_v<
              Ogre14AuthenticatedMaterialScriptRegistry>);
static_assert(std::is_nothrow_copy_constructible_v<
              Ogre14AuthenticatedMaterialScriptResolution>);
static_assert(std::is_nothrow_move_assignable_v<
              Ogre14AuthenticatedMaterialScriptResolution>);

} // namespace RoR::Render
