/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Authenticated, byte-exact OGRE 14 source-texture receipts.

#pragma once

#include "gfx/render/RenderValidation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Ogre {
class Texture;
}

namespace RoR {
class ContentManager;
}

namespace RoR::Render {

constexpr std::uint32_t kOgre14AuthenticatedTextureReceiptVersion = 1U;
constexpr std::uint32_t kOgre14AuthenticatedTextureCaptureInputVersion = 1U;
constexpr std::uint32_t kOgre14AuthenticatedTextureRegistryVersion = 1U;
constexpr std::uint32_t kOgre14AuthenticatedTextureResolutionVersion = 1U;
constexpr std::uint32_t
    kOgre14AuthenticatedTextureAuthoritySnapshotVersion = 1U;
constexpr std::uint32_t kOgre14GeneratedTextureFallbackRuleVersion = 1U;
constexpr const char kOgre14GeneratedTextureFallbackRule[] =
    "ror-legacy-material-procedural-dds-v1";

constexpr std::size_t kOgre14AuthenticatedTextureMaximumLiveReceipts =
    65536U;
constexpr std::size_t kOgre14AuthenticatedTextureMaximumGroupRecords =
    65536U;
constexpr std::uint64_t kOgre14AuthenticatedTextureMaximumSourceBytes =
    512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kOgre14AuthenticatedTextureMaximumRetainedBytes =
    1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kOgre14AuthenticatedTextureMaximumIdentityBytes =
    16ULL * 1024ULL * 1024ULL;
constexpr std::size_t kOgre14AuthenticatedTextureMaximumIdentifierBytes =
    16384U;
constexpr std::size_t
    kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates = 65536U;
constexpr std::uint64_t
    kOgre14AuthenticatedTextureMaximumArchiveMemberIdentityBytes =
        16ULL * 1024ULL * 1024ULL;

enum class Ogre14AuthenticatedTextureSourceKind : std::uint8_t {
  AUTHENTICATED_ARCHIVE_MEMBER = 0U,
  VERSIONED_GENERATED_FALLBACK = 1U,
};

enum class Ogre14AuthenticatedTextureBindingKind : std::uint8_t {
  RESOURCE = 0U,
  PRE_RESOURCE_TOKEN = 1U,
};

enum class Ogre14SourceDdsHeaderKind : std::uint8_t {
  NOT_DDS = 0U,
  LEGACY = 1U,
  DX10 = 2U,
};

/// Exact source DDS facts. Extension and resource name are never used to infer
/// this structure: only the captured bytes select NOT_DDS, LEGACY, or DX10.
struct Ogre14SourceDdsHeaderFacts final {
  Ogre14SourceDdsHeaderKind kind = Ogre14SourceDdsHeaderKind::NOT_DDS;
  std::uint32_t header_size = 0U;
  std::uint32_t flags = 0U;
  std::uint32_t height = 0U;
  std::uint32_t width = 0U;
  std::uint32_t pitch_or_linear_size = 0U;
  std::uint32_t depth = 0U;
  std::uint32_t mip_map_count = 0U;
  std::array<std::uint32_t, 11U> reserved1{};
  std::uint32_t pixel_format_size = 0U;
  std::uint32_t pixel_format_flags = 0U;
  std::uint32_t four_cc = 0U;
  std::uint32_t rgb_bit_count = 0U;
  std::uint32_t red_mask = 0U;
  std::uint32_t green_mask = 0U;
  std::uint32_t blue_mask = 0U;
  std::uint32_t alpha_mask = 0U;
  std::uint32_t caps = 0U;
  std::uint32_t caps2 = 0U;
  std::uint32_t caps3 = 0U;
  std::uint32_t caps4 = 0U;
  std::uint32_t reserved2 = 0U;
  std::uint32_t dxgi_format = 0U;
  std::uint32_t resource_dimension = 0U;
  std::uint32_t misc_flag = 0U;
  std::uint32_t array_size = 0U;
  std::uint32_t misc_flags2 = 0U;
};

/// One exact archive-index observation. ContentManager computes the match
/// flags with OGRE's own StringUtil rules while preserving the case-sensitive
/// path + basename identity in exact_member_name.
struct Ogre14AuthenticatedTextureArchiveMemberObservation final {
  std::string exact_member_name;
  bool exact_full_match = false;
  bool folded_full_match = false;
  bool folded_basename_match = false;
};

struct Ogre14AuthenticatedTextureRegistryConfiguration final {
  std::uint32_t version = kOgre14AuthenticatedTextureRegistryVersion;
  std::size_t maximum_live_receipts =
      kOgre14AuthenticatedTextureMaximumLiveReceipts;
  std::size_t maximum_group_records =
      kOgre14AuthenticatedTextureMaximumGroupRecords;
  std::uint64_t maximum_source_bytes =
      kOgre14AuthenticatedTextureMaximumSourceBytes;
  std::uint64_t maximum_retained_source_bytes =
      kOgre14AuthenticatedTextureMaximumRetainedBytes;
  std::uint64_t maximum_total_identity_bytes =
      kOgre14AuthenticatedTextureMaximumIdentityBytes;
};

struct Ogre14AuthenticatedTextureResourceBinding final {
  Ogre14AuthenticatedTextureBindingKind kind =
      Ogre14AuthenticatedTextureBindingKind::RESOURCE;
  /// Exact uintptr_t representation. Zero is never a valid resource token.
  std::uintptr_t resource_pointer_token = 0U;
  std::uint64_t resource_handle = 0U;
  /// OGRE Resource::getStateCount() before the selected source bytes load.
  std::uint64_t resource_state_count = 0U;
  /// Monotonic caller-minted token for capture before a Resource exists.
  std::uint64_t pre_resource_token = 0U;
  std::string exact_resource_name;
};

/// Inputs are exact observations, not lookup hints. For archive members,
/// archive_identity is the authenticated registration key and archive_name /
/// archive_type are the selected Ogre::Archive values. For generated fallback
/// bytes, those three fields are empty and archive_sha256 identifies the
/// authenticated compatibility authority which authorized the exact rule.
struct Ogre14AuthenticatedTextureCaptureInput final {
  std::uint32_t version = kOgre14AuthenticatedTextureCaptureInputVersion;
  Ogre14AuthenticatedTextureSourceKind source_kind =
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER;
  std::string effective_resource_group;
  std::uint64_t group_generation = 0U;
  std::string archive_identity;
  std::string archive_name;
  std::string archive_type;
  std::string archive_sha256;
  /// Exact uintptr_t representation of the selected Ogre::Archive instance.
  /// Required for archive members and zero for generated fallback bytes.
  std::uintptr_t archive_pointer_token = 0U;
  std::string exact_member_name;
  std::string generated_fallback_rule;
  std::uint32_t generated_fallback_rule_version = 0U;
  Ogre14AuthenticatedTextureResourceBinding binding;
};

struct Ogre14AuthenticatedTextureReceiptMetadata final {
  std::uint32_t version = kOgre14AuthenticatedTextureReceiptVersion;
  Ogre14AuthenticatedTextureCaptureInput source;
  std::uint64_t byte_count = 0U;
  std::string bytes_sha256;
  Ogre14SourceDdsHeaderFacts dds;
};

enum class Ogre14AuthenticatedTextureTransactionStage : std::uint8_t {
  AFTER_SOURCE_BYTES_COPIED = 0U,
  BEFORE_RECEIPT_COMMIT = 1U,
  BEFORE_REGISTRY_COMMIT = 2U,
  BEFORE_GROUP_TRANSITION_COMMIT = 3U,
  BEFORE_RESOLUTION_COMMIT = 4U,
};

/// Exact externally visible boundaries in the authenticated EmbeddedZip mount
/// transaction. Fault injection is a test-only borrowed callback; production
/// always passes null and therefore cannot alter the mount sequence.
enum class Ogre14AuthenticatedArchiveMountStage : std::uint8_t {
  AFTER_EMBEDDED_ZIP_REGISTRATION = 0U,
  AFTER_RESOURCE_LOCATION_INSERTION = 1U,
  BEFORE_POINTER_BOUND_STATE_SWAP = 2U,
};

class IOgre14AuthenticatedTextureFaultInjector {
public:
  virtual ~IOgre14AuthenticatedTextureFaultInjector() = default;
  /// Borrowed test seam. Production always passes null. May throw anything.
  virtual void BeforeAuthenticatedTextureStage(
      Ogre14AuthenticatedTextureTransactionStage) {}
};

class IOgre14AuthenticatedArchiveMountFaultInjector {
public:
  virtual ~IOgre14AuthenticatedArchiveMountFaultInjector() = default;
  /// Borrowed test seam. Production always passes null. May throw anything.
  virtual void BeforeAuthenticatedArchiveMountStage(
      Ogre14AuthenticatedArchiveMountStage) {}
};

inline void MaybeInjectOgre14AuthenticatedArchiveMountFault(
    Ogre14AuthenticatedArchiveMountStage stage,
    IOgre14AuthenticatedArchiveMountFaultInjector *fault_injector) {
  if (fault_injector != nullptr) {
    fault_injector->BeforeAuthenticatedArchiveMountStage(stage);
  }
}

class Ogre14AuthenticatedTextureReceiptRegistry;
class Ogre14AuthenticatedTextureResolution;
class Ogre14AuthenticatedTextureAuthoritySnapshot;
class IOgre14AuthenticatedTextureResolver;
class IOgre14AuthenticatedTextureAuthorityProvider;

namespace Testing {
class Ogre14AuthenticatedTextureResolutionTestAccess;
}

/// Immutable owner for the exact bytes and their authenticated metadata.
class Ogre14AuthenticatedTextureReceipt final {
public:
  Ogre14AuthenticatedTextureReceipt() = default;
  ~Ogre14AuthenticatedTextureReceipt() = default;
  Ogre14AuthenticatedTextureReceipt(
      const Ogre14AuthenticatedTextureReceipt &) noexcept = default;
  Ogre14AuthenticatedTextureReceipt &operator=(
      const Ogre14AuthenticatedTextureReceipt &) noexcept = default;
  Ogre14AuthenticatedTextureReceipt(
      Ogre14AuthenticatedTextureReceipt &&) noexcept = default;
  Ogre14AuthenticatedTextureReceipt &operator=(
      Ogre14AuthenticatedTextureReceipt &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const Ogre14AuthenticatedTextureReceiptMetadata *metadata()
      const noexcept;
  [[nodiscard]] const std::uint8_t *source_bytes() const noexcept;
  [[nodiscard]] std::size_t source_size() const noexcept;
  [[nodiscard]] std::uint64_t identity_size() const noexcept;
  [[nodiscard]] bool ReplacementBytesMatch(const void *bytes,
                                            std::size_t size) const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14AuthenticatedTextureReceipt &other) const noexcept;

private:
  struct State;
  explicit Ogre14AuthenticatedTextureReceipt(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend ValidationResult BuildOgre14AuthenticatedTextureReceipt(
      const Ogre14AuthenticatedTextureRegistryConfiguration &,
      const Ogre14AuthenticatedTextureCaptureInput &, const void *,
      std::size_t, Ogre14AuthenticatedTextureReceipt &,
      IOgre14AuthenticatedTextureFaultInjector *);
  friend class Ogre14AuthenticatedTextureReceiptRegistry;
  friend ValidationResult CommitOgre14AuthenticatedTextureReceipt(
      const Ogre14AuthenticatedTextureReceipt &,
      Ogre14AuthenticatedTextureReceiptRegistry &,
      IOgre14AuthenticatedTextureFaultInjector *);
};

/// Immutable-snapshot registry. Copies are cheap ownership snapshots; every
/// mutation publishes a complete candidate only after all checks/allocations.
class Ogre14AuthenticatedTextureReceiptRegistry final {
public:
  Ogre14AuthenticatedTextureReceiptRegistry() = default;
  ~Ogre14AuthenticatedTextureReceiptRegistry() = default;
  Ogre14AuthenticatedTextureReceiptRegistry(
      const Ogre14AuthenticatedTextureReceiptRegistry &) noexcept = default;
  Ogre14AuthenticatedTextureReceiptRegistry &operator=(
      const Ogre14AuthenticatedTextureReceiptRegistry &) noexcept = default;
  Ogre14AuthenticatedTextureReceiptRegistry(
      Ogre14AuthenticatedTextureReceiptRegistry &&) noexcept = default;
  Ogre14AuthenticatedTextureReceiptRegistry &operator=(
      Ogre14AuthenticatedTextureReceiptRegistry &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::uint64_t retained_source_bytes() const noexcept;
  [[nodiscard]] std::uint64_t maximum_group_generation_seen() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14AuthenticatedTextureReceiptRegistry &other) const noexcept;

  [[nodiscard]] ValidationResult FindResource(
      const std::string &effective_resource_group,
      std::uint64_t group_generation, std::uintptr_t resource_pointer_token,
      std::uint64_t resource_handle, const std::string &exact_resource_name,
      Ogre14AuthenticatedTextureReceipt &receipt) const;

private:
  struct State;
  explicit Ogre14AuthenticatedTextureReceiptRegistry(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend ValidationResult InitializeOgre14AuthenticatedTextureReceiptRegistry(
      const Ogre14AuthenticatedTextureRegistryConfiguration &,
      Ogre14AuthenticatedTextureReceiptRegistry &);
  friend ValidationResult AdvanceOgre14AuthenticatedTextureGroupGeneration(
      const std::string &, std::uint64_t,
      Ogre14AuthenticatedTextureReceiptRegistry &,
      IOgre14AuthenticatedTextureFaultInjector *);
  friend ValidationResult TeardownOgre14AuthenticatedTextureGroup(
      const std::string &, std::uint64_t,
      Ogre14AuthenticatedTextureReceiptRegistry &,
      IOgre14AuthenticatedTextureFaultInjector *);
  friend ValidationResult CommitOgre14AuthenticatedTextureReceipt(
      const Ogre14AuthenticatedTextureReceipt &,
      Ogre14AuthenticatedTextureReceiptRegistry &,
      IOgre14AuthenticatedTextureFaultInjector *);
  friend ValidationResult RemoveOgre14AuthenticatedTextureResource(
      const std::string &, std::uintptr_t, std::uint64_t,
      const std::string &, Ogre14AuthenticatedTextureReceiptRegistry &,
      IOgre14AuthenticatedTextureFaultInjector *);
  friend void PoisonOgre14AuthenticatedTextureReceiptRegistry(
      Ogre14AuthenticatedTextureReceiptRegistry &) noexcept;
  [[nodiscard]] ValidationResult MintLoadedResourceResolution(
      const std::string &effective_resource_group,
      std::uint64_t group_generation, std::uintptr_t resource_pointer_token,
      std::uint64_t resource_handle, const std::string &exact_resource_name,
      std::uint64_t loaded_resource_state_count,
      std::uintptr_t resolver_pointer_token,
      Ogre14AuthenticatedTextureResolution &resolution,
      IOgre14AuthenticatedTextureFaultInjector *fault_injector = nullptr)
      const;
  [[nodiscard]] bool RevalidateLoadedResourceResolution(
      const Ogre14AuthenticatedTextureResolution &resolution,
      std::uintptr_t resolver_pointer_token,
      std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
      const std::string &exact_resource_group,
      const std::string &exact_resource_name,
      std::uint64_t loaded_resource_state_count) const noexcept;
  [[nodiscard]] ValidationResult MintResolverAuthoritySnapshot(
      std::uintptr_t resolver_pointer_token,
      Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) const;

  friend class ::RoR::ContentManager;
  friend class Testing::Ogre14AuthenticatedTextureResolutionTestAccess;
};

/// Registry-minted proof for one already-loaded texture. Build-only source
/// receipts cannot construct a nonempty resolution: its immutable state is
/// minted only from the exact current registry snapshot and is additionally
/// bound to the ContentManager resolver instance which requested it. Copies
/// preserve the exact registry and source-receipt control blocks.
class Ogre14AuthenticatedTextureResolution final {
public:
  Ogre14AuthenticatedTextureResolution() noexcept = default;
  ~Ogre14AuthenticatedTextureResolution() = default;
  Ogre14AuthenticatedTextureResolution(
      const Ogre14AuthenticatedTextureResolution &) noexcept = default;
  Ogre14AuthenticatedTextureResolution &operator=(
      const Ogre14AuthenticatedTextureResolution &) noexcept = default;
  Ogre14AuthenticatedTextureResolution(
      Ogre14AuthenticatedTextureResolution &&) noexcept = default;
  Ogre14AuthenticatedTextureResolution &operator=(
      Ogre14AuthenticatedTextureResolution &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] const Ogre14AuthenticatedTextureReceipt *source_receipt()
      const noexcept;
  [[nodiscard]] std::uint64_t loaded_resource_state_count() const noexcept;

  /// Exact authority equivalence for separately minted resolutions of the
  /// same already-loaded resource. This is deliberately stronger than value
  /// equality: both resolutions must retain the same registry snapshot,
  /// source-receipt control block, resolver identity, and loaded state.
  [[nodiscard]] bool SharesLoadedResourceAuthorityWith(
      const Ogre14AuthenticatedTextureResolution &other) const noexcept;

  /// This is an extractor-side substitution check, not registry
  /// revalidation. Only the bound resolver can authoritatively revalidate the
  /// current registry snapshot immediately before publication.
  [[nodiscard]] bool MatchesResolver(
      const IOgre14AuthenticatedTextureResolver &resolver) const noexcept;
  [[nodiscard]] bool MatchesLoadedResourceIdentity(
      std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
      const std::string &exact_resource_group,
      const std::string &exact_resource_name,
      std::uint64_t loaded_resource_state_count) const noexcept;

private:
  struct State;
  explicit Ogre14AuthenticatedTextureResolution(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14AuthenticatedTextureReceiptRegistry;
  friend class Ogre14AuthenticatedTextureAuthoritySnapshot;
  friend class Testing::Ogre14AuthenticatedTextureResolutionTestAccess;
};

/// Opaque proof of one resolver and one exact current immutable receipt
/// registry snapshot. The live coordinator obtains this from its trusted
/// provider for every frame; callers cannot construct or rebox nonempty
/// authority. It authenticates distinct texture resources from the same
/// registry publication without requiring them to share a source receipt.
class Ogre14AuthenticatedTextureAuthoritySnapshot final {
public:
  Ogre14AuthenticatedTextureAuthoritySnapshot() noexcept = default;
  ~Ogre14AuthenticatedTextureAuthoritySnapshot() = default;
  Ogre14AuthenticatedTextureAuthoritySnapshot(
      const Ogre14AuthenticatedTextureAuthoritySnapshot &) noexcept = default;
  Ogre14AuthenticatedTextureAuthoritySnapshot &operator=(
      const Ogre14AuthenticatedTextureAuthoritySnapshot &) noexcept = default;
  Ogre14AuthenticatedTextureAuthoritySnapshot(
      Ogre14AuthenticatedTextureAuthoritySnapshot &&) noexcept = default;
  Ogre14AuthenticatedTextureAuthoritySnapshot &operator=(
      Ogre14AuthenticatedTextureAuthoritySnapshot &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] bool Authenticates(
      const Ogre14AuthenticatedTextureResolution &resolution) const noexcept;
  [[nodiscard]] bool SharesImmutableAuthorityWith(
      const Ogre14AuthenticatedTextureAuthoritySnapshot &other) const noexcept;

private:
  Ogre14AuthenticatedTextureAuthoritySnapshot(
      Ogre14AuthenticatedTextureReceiptRegistry registry_snapshot,
      std::uintptr_t resolver_pointer_token) noexcept;

  std::uint32_t version_ = 0U;
  Ogre14AuthenticatedTextureReceiptRegistry registry_snapshot_;
  std::uintptr_t resolver_pointer_token_ = 0U;

  friend class Ogre14AuthenticatedTextureReceiptRegistry;
  friend class Testing::Ogre14AuthenticatedTextureResolutionTestAccess;
};

/// Trusted scene authority which captures the resolver's current immutable
/// registry publication. Implementations must serialize this call with
/// resource lifecycle mutation. The coordinator invokes it once at the start
/// of every frame instead of trusting a caller-supplied or first-observation
/// snapshot.
class IOgre14AuthenticatedTextureAuthorityProvider {
public:
  virtual ~IOgre14AuthenticatedTextureAuthorityProvider() = default;

  [[nodiscard]] virtual ValidationResult
  CaptureAuthenticatedTextureAuthoritySnapshot(
      Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) const = 0;
};

/// Narrow OGRE-native authority used by the legacy extractor. Implementations
/// must resolve and revalidate on OGRE's serialized resource/render thread;
/// pinned OGRE 14.5.2 exposes a non-atomic Resource state counter and therefore
/// cannot authenticate concurrent reload/readback.
class IOgre14AuthenticatedTextureResolver {
public:
  virtual ~IOgre14AuthenticatedTextureResolver() = default;

  /// May allocate while minting the immutable resolution. Failure must leave
  /// `resolution` untouched.
  [[nodiscard]] virtual ValidationResult ResolveAuthenticatedTexture(
      Ogre::Texture &texture,
      Ogre14AuthenticatedTextureResolution &resolution) const = 0;

  /// Allocation-free, nonthrowing final authority check against the exact
  /// current registry snapshot and live Texture identity.
  [[nodiscard]] virtual bool RevalidateAuthenticatedTexture(
      Ogre::Texture &texture,
      const Ogre14AuthenticatedTextureResolution &resolution) const
      noexcept = 0;
};

[[nodiscard]] ValidationResult
ValidateOgre14AuthenticatedTextureRegistryConfiguration(
    const Ogre14AuthenticatedTextureRegistryConfiguration &configuration);

/// Selects the one member that an archive can open without identity
/// ambiguity. Case-sensitive archives require one exact full-name match.
/// Case-insensitive archives require one folded full-name match; the explicit
/// Zip fallback is considered only when no full-name match exists.
[[nodiscard]] ValidationResult SelectOgre14AuthenticatedTextureArchiveMember(
    bool archive_case_sensitive, bool allow_zip_basename_fallback,
    const Ogre14AuthenticatedTextureArchiveMemberObservation *observations,
    std::size_t observation_count, std::string &exact_member_name);

[[nodiscard]] ValidationResult ValidateOgre14AuthenticatedTextureCaptureInput(
    const Ogre14AuthenticatedTextureCaptureInput &input);

[[nodiscard]] ValidationResult BuildOgre14AuthenticatedTextureReceipt(
    const Ogre14AuthenticatedTextureRegistryConfiguration &configuration,
    const Ogre14AuthenticatedTextureCaptureInput &input,
    const void *source_bytes, std::size_t source_size,
    Ogre14AuthenticatedTextureReceipt &output,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector = nullptr);

[[nodiscard]] ValidationResult
InitializeOgre14AuthenticatedTextureReceiptRegistry(
    const Ogre14AuthenticatedTextureRegistryConfiguration &configuration,
    Ogre14AuthenticatedTextureReceiptRegistry &output);

/// New generations are globally strictly monotonic and clear every receipt in
/// the named group before activating it. Stale captures can never resurrect.
[[nodiscard]] ValidationResult AdvanceOgre14AuthenticatedTextureGroupGeneration(
    const std::string &effective_resource_group,
    std::uint64_t new_group_generation,
    Ogre14AuthenticatedTextureReceiptRegistry &registry,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector = nullptr);

/// Deactivates an exact current generation and clears its receipts while
/// retaining the global generation watermark.
[[nodiscard]] ValidationResult TeardownOgre14AuthenticatedTextureGroup(
    const std::string &effective_resource_group,
    std::uint64_t exact_group_generation,
    Ogre14AuthenticatedTextureReceiptRegistry &registry,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector = nullptr);

/// Commits an already-built immutable receipt. An exact same-state,
/// same-bytes/provenance resource retry is an idempotent success so a failure
/// in OGRE's later decode/upload phase can retry safely. Other duplicates,
/// handle collisions, stale generations, and live pointer reuse fail closed. A
/// reload may replace the same resource only when its pre-load state increased.
[[nodiscard]] ValidationResult CommitOgre14AuthenticatedTextureReceipt(
    const Ogre14AuthenticatedTextureReceipt &receipt,
    Ogre14AuthenticatedTextureReceiptRegistry &registry,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector = nullptr);

/// Removes only the exact resource pointer/handle/group/name binding. Missing
/// entries are an idempotent success; mismatched live pointer reuse is rejected.
[[nodiscard]] ValidationResult RemoveOgre14AuthenticatedTextureResource(
    const std::string &effective_resource_group,
    std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
    const std::string &exact_resource_name,
    Ogre14AuthenticatedTextureReceiptRegistry &registry,
    IOgre14AuthenticatedTextureFaultInjector *fault_injector = nullptr);

/// Terminally invalidates the current registry publication without allocating
/// or throwing. Callers must use this immediately when OGRE has already
/// removed or substituted a resource but the transactional registry removal
/// could not publish. No new resolution or authority snapshot can then be
/// minted until the owning ContentManager is reconstructed.
void PoisonOgre14AuthenticatedTextureReceiptRegistry(
    Ogre14AuthenticatedTextureReceiptRegistry &registry) noexcept;

[[nodiscard]] bool IsLowercaseOgre14Sha256(
    const std::string &value) noexcept;

} // namespace RoR::Render
