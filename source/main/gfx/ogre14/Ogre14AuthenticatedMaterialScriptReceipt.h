/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Authenticated OGRE 14 material-script and material receipts.

#pragma once

#include "gfx/render/RenderValidation.h"
#include "resources/terrn2_fileformat/TerrainBundleArchiveVerifier.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Ogre {
class Material;
}

namespace RoR {
class ContentManager;
}

namespace RoR::Render {

constexpr std::uint32_t
    kOgre14AuthenticatedMaterialScriptReceiptVersion = 1U;
constexpr std::uint32_t
    kOgre14AuthenticatedMaterialScriptRegistryVersion = 1U;
constexpr std::uint32_t
    kOgre14AuthenticatedMaterialScriptResolutionVersion = 1U;
constexpr std::uint32_t
    kOgre14AuthenticatedMaterialScriptAuthoritySnapshotVersion = 1U;
constexpr std::uint32_t
    kOgre14AuthenticatedMaterialScriptSourceMetadataVersion = 1U;
constexpr std::uint32_t
    kOgre14AuthenticatedMaterialScriptBindingMetadataVersion = 1U;
constexpr std::uint32_t
    kOgre14AuthenticatedMaterialScriptRepairPlanVersion = 1U;

constexpr std::size_t
    kOgre14AuthenticatedMaterialScriptMaximumIdentifierBytes = 16384U;
constexpr std::size_t
    kOgre14AuthenticatedMaterialScriptMaximumLiveReceipts = 65536U;
constexpr std::size_t
    kOgre14AuthenticatedMaterialScriptMaximumLiveSources = 65536U;
constexpr std::size_t
    kOgre14AuthenticatedMaterialScriptMaximumGroupRecords = 65536U;
constexpr std::uint64_t
    kOgre14AuthenticatedMaterialScriptMaximumSourceBytes =
        16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t
    kOgre14AuthenticatedMaterialScriptMaximumRetainedBytes =
        1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t
    kOgre14AuthenticatedMaterialScriptMaximumIdentityBytes =
        16ULL * 1024ULL * 1024ULL;

enum class Ogre14MaterialScriptRepairState : std::uint8_t {
  NONE = 0U,
  APPLIED = 1U,
};

enum class Ogre14MaterialScriptSourceRole : std::uint8_t {
  ROOT_SCRIPT = 0U,
  COMPILER_DEPENDENCY = 1U,
};

struct Ogre14AuthenticatedMaterialScriptRegistryConfiguration final {
  std::uint32_t version =
      kOgre14AuthenticatedMaterialScriptRegistryVersion;
  std::size_t maximum_live_receipts =
      kOgre14AuthenticatedMaterialScriptMaximumLiveReceipts;
  std::size_t maximum_live_sources =
      kOgre14AuthenticatedMaterialScriptMaximumLiveSources;
  std::size_t maximum_group_records =
      kOgre14AuthenticatedMaterialScriptMaximumGroupRecords;
  std::uint64_t maximum_source_bytes =
      kOgre14AuthenticatedMaterialScriptMaximumSourceBytes;
  std::uint64_t maximum_retained_source_bytes =
      kOgre14AuthenticatedMaterialScriptMaximumRetainedBytes;
  std::uint64_t maximum_total_identity_bytes =
      kOgre14AuthenticatedMaterialScriptMaximumIdentityBytes;
};

struct Ogre14AuthenticatedMaterialScriptSourceMetadata final {
  std::uint32_t version =
      kOgre14AuthenticatedMaterialScriptSourceMetadataVersion;
  Ogre14MaterialScriptSourceRole source_role =
      Ogre14MaterialScriptSourceRole::ROOT_SCRIPT;
  std::uint64_t parse_token = 0U;
  std::uint64_t source_open_ordinal = 0U;
  std::uint64_t group_generation = 0U;
  std::string effective_group;
  std::string root_script_request;
  std::string compiler_file_identity;
  std::string archive_source_identity;
  std::string selected_archive_name;
  std::string selected_archive_type;
  std::string archive_sha256;
  std::uintptr_t archive_pointer_token = 0U;
  std::string file_info_filename;
  std::string file_info_path;
  std::string file_info_basename;
  std::string exact_member_name;
  std::uint64_t compressed_size = 0U;
  std::uint64_t uncompressed_size = 0U;
  std::uint64_t original_byte_count = 0U;
  std::uint64_t effective_byte_count = 0U;
  std::string original_sha256;
  std::string effective_sha256;
  std::uint32_t repair_plan_version = 0U;
  std::string repair_plan_sha256;
  Ogre14MaterialScriptRepairState repair_state =
      Ogre14MaterialScriptRepairState::NONE;
  std::uint64_t applied_edit_count = 0U;
};

struct Ogre14AuthenticatedMaterialScriptBindingMetadata final {
  std::uint32_t version =
      kOgre14AuthenticatedMaterialScriptBindingMetadataVersion;
  std::uint64_t event_ordinal = 0U;
  std::uintptr_t material_pointer_token = 0U;
  std::uint64_t material_handle = 0U;
  std::string exact_material_name;
  std::string exact_group;
  std::string exact_origin;
};

/// Private-mint input retained only inside ContentManager's group transaction.
/// Public construction does not confer authority; only the registry's private
/// ContentManager entry point can publish it.
struct Ogre14AuthenticatedMaterialScriptSourceInput final {
  Ogre14AuthenticatedMaterialScriptSourceMetadata metadata;
  TerrainBundleAuthenticatedArchiveSnapshot authenticated_archive_snapshot;
  std::shared_ptr<const std::vector<std::uint8_t>> original_bytes;
  std::shared_ptr<const std::vector<std::uint8_t>> effective_bytes;
};

struct Ogre14AuthenticatedMaterialScriptMaterialInput final {
  std::size_t source_index = 0U;
  Ogre14AuthenticatedMaterialScriptBindingMetadata binding;
};

enum class Ogre14AuthenticatedMaterialScriptCommitFaultPoint :
    std::uint8_t {
  AFTER_SOURCE_CANONICALIZATION = 0U,
  BEFORE_PUBLICATION = 1U,
};

/// Borrowed deterministic test seam. Production passes null. The caller must
/// keep the injector alive and quiescent for the synchronous commit call.
class IOgre14AuthenticatedMaterialScriptCommitFaultInjector {
public:
  virtual ~IOgre14AuthenticatedMaterialScriptCommitFaultInjector() = default;
  virtual void OnOgre14AuthenticatedMaterialScriptCommitFault(
      Ogre14AuthenticatedMaterialScriptCommitFaultPoint) = 0;
};

class Ogre14AuthenticatedMaterialScriptRegistry;
class Ogre14AuthenticatedMaterialScriptResolution;
class Ogre14AuthenticatedMaterialScriptAuthoritySnapshot;
class IOgre14AuthenticatedMaterialScriptResolver;

#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_TESTING)
namespace Testing {
class Ogre14AuthenticatedMaterialScriptTestAccess;
}
#endif

/// Immutable receipt sharing one exact source owner with every material
/// created from that same compiler file.
class Ogre14AuthenticatedMaterialScriptReceipt final {
public:
  Ogre14AuthenticatedMaterialScriptReceipt() = default;
  ~Ogre14AuthenticatedMaterialScriptReceipt() = default;
  Ogre14AuthenticatedMaterialScriptReceipt(
      const Ogre14AuthenticatedMaterialScriptReceipt &) noexcept = default;
  Ogre14AuthenticatedMaterialScriptReceipt &operator=(
      const Ogre14AuthenticatedMaterialScriptReceipt &) noexcept = default;
  Ogre14AuthenticatedMaterialScriptReceipt(
      Ogre14AuthenticatedMaterialScriptReceipt &&) noexcept = default;
  Ogre14AuthenticatedMaterialScriptReceipt &operator=(
      Ogre14AuthenticatedMaterialScriptReceipt &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const Ogre14AuthenticatedMaterialScriptSourceMetadata *
  source_metadata() const noexcept;
  [[nodiscard]] std::size_t source_count() const noexcept;
  [[nodiscard]] std::size_t primary_source_index() const noexcept;
  [[nodiscard]] const Ogre14AuthenticatedMaterialScriptSourceMetadata *
  source_metadata_at(std::size_t index) const noexcept;
  [[nodiscard]] const Ogre14AuthenticatedMaterialScriptBindingMetadata *
  binding_metadata() const noexcept;
  [[nodiscard]] const std::uint8_t *original_bytes() const noexcept;
  [[nodiscard]] std::size_t original_size() const noexcept;
  [[nodiscard]] const std::uint8_t *original_bytes_at(
      std::size_t index) const noexcept;
  [[nodiscard]] std::size_t original_size_at(
      std::size_t index) const noexcept;
  [[nodiscard]] const std::uint8_t *effective_bytes() const noexcept;
  [[nodiscard]] std::size_t effective_size() const noexcept;
  [[nodiscard]] const std::uint8_t *effective_bytes_at(
      std::size_t index) const noexcept;
  [[nodiscard]] std::size_t effective_size_at(
      std::size_t index) const noexcept;
  [[nodiscard]] const TerrainBundleAuthenticatedArchiveSnapshot *
  authenticated_archive_snapshot() const noexcept;
  [[nodiscard]] const TerrainBundleAuthenticatedArchiveSnapshot *
  authenticated_archive_snapshot_at(std::size_t index) const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14AuthenticatedMaterialScriptReceipt &other) const noexcept;
  [[nodiscard]] bool SharesSourceStateWith(
      const Ogre14AuthenticatedMaterialScriptReceipt &other) const noexcept;

private:
  struct SourceState;
  struct SourceClosureState;
  struct State;
  explicit Ogre14AuthenticatedMaterialScriptReceipt(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14AuthenticatedMaterialScriptRegistry;
};

/// Immutable registry snapshot. ContentManager alone may advance, publish,
/// remove, poison, or mint a nonempty resolution.
class Ogre14AuthenticatedMaterialScriptRegistry final {
public:
  Ogre14AuthenticatedMaterialScriptRegistry() = default;
  ~Ogre14AuthenticatedMaterialScriptRegistry() = default;
  Ogre14AuthenticatedMaterialScriptRegistry(
      const Ogre14AuthenticatedMaterialScriptRegistry &) noexcept = default;
  Ogre14AuthenticatedMaterialScriptRegistry &operator=(
      const Ogre14AuthenticatedMaterialScriptRegistry &) noexcept = default;
  Ogre14AuthenticatedMaterialScriptRegistry(
      Ogre14AuthenticatedMaterialScriptRegistry &&) noexcept = default;
  Ogre14AuthenticatedMaterialScriptRegistry &operator=(
      Ogre14AuthenticatedMaterialScriptRegistry &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t source_count() const noexcept;
  [[nodiscard]] std::uint64_t retained_source_bytes() const noexcept;
  [[nodiscard]] std::uint64_t retained_identity_bytes() const noexcept;
  [[nodiscard]] std::uint64_t maximum_group_generation_seen() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14AuthenticatedMaterialScriptRegistry &other) const noexcept;

private:
  struct State;
  explicit Ogre14AuthenticatedMaterialScriptRegistry(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  [[nodiscard]] ValidationResult Initialize(
      const Ogre14AuthenticatedMaterialScriptRegistryConfiguration &);
  [[nodiscard]] ValidationResult AdvanceGroupGeneration(
      const std::string &, std::uint64_t);
  [[nodiscard]] ValidationResult TeardownGroup(
      const std::string &, std::uint64_t);
  [[nodiscard]] ValidationResult CommitWholeGroup(
      const std::string &, std::uint64_t,
      const std::vector<Ogre14AuthenticatedMaterialScriptSourceInput> &,
      const std::vector<Ogre14AuthenticatedMaterialScriptMaterialInput> &,
      IOgre14AuthenticatedMaterialScriptCommitFaultInjector * = nullptr);
  [[nodiscard]] ValidationResult RemoveMaterial(
      const std::string &, std::uintptr_t, std::uint64_t,
      const std::string &, const std::string &);
  void Poison() noexcept;
  [[nodiscard]] ValidationResult MintResolution(
      const std::string &, std::uint64_t, std::uintptr_t, std::uint64_t,
      const std::string &, const std::string &, std::uintptr_t,
      Ogre14AuthenticatedMaterialScriptResolution &) const;
  [[nodiscard]] bool RevalidateResolution(
      const Ogre14AuthenticatedMaterialScriptResolution &,
      std::uintptr_t, std::uintptr_t, std::uint64_t, const std::string &,
      const std::string &, const std::string &) const noexcept;
  [[nodiscard]] ValidationResult MintResolverAuthoritySnapshot(
      std::uintptr_t,
      Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &) const;
  static void RecomputeAccounting(State &);
  [[nodiscard]] static ValidationResult CheckAccounting(const State &);

  friend class ::RoR::ContentManager;
  friend class Ogre14AuthenticatedMaterialScriptResolution;
  friend class Ogre14AuthenticatedMaterialScriptAuthoritySnapshot;
#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_TESTING)
  friend class Testing::Ogre14AuthenticatedMaterialScriptTestAccess;
#endif
};

/// Resolver-bound authority for one current material. A copied resolution
/// shares the registry and receipt control blocks; callers cannot mint one.
class Ogre14AuthenticatedMaterialScriptResolution final {
public:
  Ogre14AuthenticatedMaterialScriptResolution() = default;
  ~Ogre14AuthenticatedMaterialScriptResolution() = default;
  Ogre14AuthenticatedMaterialScriptResolution(
      const Ogre14AuthenticatedMaterialScriptResolution &) noexcept = default;
  Ogre14AuthenticatedMaterialScriptResolution &operator=(
      const Ogre14AuthenticatedMaterialScriptResolution &) noexcept = default;
  Ogre14AuthenticatedMaterialScriptResolution(
      Ogre14AuthenticatedMaterialScriptResolution &&) noexcept = default;
  Ogre14AuthenticatedMaterialScriptResolution &operator=(
      Ogre14AuthenticatedMaterialScriptResolution &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const Ogre14AuthenticatedMaterialScriptReceipt *receipt()
      const noexcept;
  [[nodiscard]] bool SharesCurrentAuthorityWith(
      const Ogre14AuthenticatedMaterialScriptResolution &other) const noexcept;
  /// Extractor-side substitution check binding the resolution to the exact
  /// resolver subobject that minted it. Current-state revalidation remains a
  /// separate final no-throw operation.
  [[nodiscard]] bool MatchesResolver(
      const IOgre14AuthenticatedMaterialScriptResolver &) const noexcept;

private:
  struct State;
  explicit Ogre14AuthenticatedMaterialScriptResolution(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14AuthenticatedMaterialScriptRegistry;
  friend class Ogre14AuthenticatedMaterialScriptAuthoritySnapshot;
};

/// Opaque proof of one material-script resolver and one exact current
/// immutable registry publication. Numeric generations and copied receipt
/// values cannot construct this authority.
class Ogre14AuthenticatedMaterialScriptAuthoritySnapshot final {
public:
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot() noexcept = default;
  ~Ogre14AuthenticatedMaterialScriptAuthoritySnapshot() = default;
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot(
      const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &) noexcept =
      default;
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &operator=(
      const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &) noexcept =
      default;
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot(
      Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &&) noexcept =
      default;
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &operator=(
      Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &&) noexcept =
      default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] bool Authenticates(
      const Ogre14AuthenticatedMaterialScriptResolution &) const noexcept;
  [[nodiscard]] bool SharesImmutableAuthorityWith(
      const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &) const
      noexcept;

private:
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot(
      Ogre14AuthenticatedMaterialScriptRegistry,
      std::uintptr_t) noexcept;

  std::uint32_t version_ = 0U;
  Ogre14AuthenticatedMaterialScriptRegistry registry_snapshot_;
  std::uintptr_t resolver_pointer_token_ = 0U;

  friend class Ogre14AuthenticatedMaterialScriptRegistry;
};

/// Trusted provider of the current material-script registry publication. Live
/// admission asks for a fresh snapshot rather than trusting a caller-supplied
/// generation or a first-observation proof.
class IOgre14AuthenticatedMaterialScriptAuthorityProvider {
public:
  virtual ~IOgre14AuthenticatedMaterialScriptAuthorityProvider() = default;
  [[nodiscard]] virtual ValidationResult
  CaptureAuthenticatedMaterialScriptAuthoritySnapshot(
      Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &) const = 0;
};

class IOgre14AuthenticatedMaterialScriptResolver {
public:
  virtual ~IOgre14AuthenticatedMaterialScriptResolver() = default;
  [[nodiscard]] virtual ValidationResult ResolveAuthenticatedMaterialScript(
      Ogre::Material &,
      Ogre14AuthenticatedMaterialScriptResolution &) const = 0;
  [[nodiscard]] virtual bool RevalidateAuthenticatedMaterialScript(
      Ogre::Material &,
      const Ogre14AuthenticatedMaterialScriptResolution &) const noexcept = 0;
};

} // namespace RoR::Render
