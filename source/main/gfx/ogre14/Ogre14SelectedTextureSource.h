/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Byte-exact receipts for ordinary OGRE 14 package textures.

#pragma once

#include "gfx/render/RenderValidation.h"

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

constexpr std::uint32_t kOgre14SelectedTextureSourceReceiptVersion = 1U;
constexpr std::uint32_t kOgre14SelectedTextureSourceCaptureInputVersion = 1U;
constexpr std::uint32_t kOgre14SelectedTextureSourceRegistryVersion = 1U;
constexpr std::uint32_t kOgre14SelectedTextureSourceResolutionVersion = 1U;

constexpr std::size_t kOgre14SelectedTextureSourceMaximumLiveReceipts =
    65536U;
constexpr std::size_t kOgre14SelectedTextureSourceMaximumGroupRecords =
    65536U;
constexpr std::uint64_t kOgre14SelectedTextureSourceMaximumBytes =
    512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kOgre14SelectedTextureSourceMaximumRetainedBytes =
    1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kOgre14SelectedTextureSourceMaximumIdentityBytes =
    16ULL * 1024ULL * 1024ULL;
constexpr std::size_t kOgre14SelectedTextureSourceMaximumIdentifierBytes =
    16384U;

/// Honest trust label for ordinary package content. This receipt proves which
/// bytes OGRE selected; it does not authenticate the containing archive.
enum class Ogre14SelectedTextureSourceKind : std::uint8_t {
  UNAUTHENTICATED_PACKAGE_ARCHIVE_MEMBER = 0U,
};

struct Ogre14SelectedTextureSourceRegistryConfiguration final {
  std::uint32_t version = kOgre14SelectedTextureSourceRegistryVersion;
  std::size_t maximum_live_receipts =
      kOgre14SelectedTextureSourceMaximumLiveReceipts;
  std::size_t maximum_group_records =
      kOgre14SelectedTextureSourceMaximumGroupRecords;
  std::uint64_t maximum_source_bytes =
      kOgre14SelectedTextureSourceMaximumBytes;
  std::uint64_t maximum_retained_source_bytes =
      kOgre14SelectedTextureSourceMaximumRetainedBytes;
  std::uint64_t maximum_total_identity_bytes =
      kOgre14SelectedTextureSourceMaximumIdentityBytes;
};

/// Exact observations from one OGRE archive lookup and stream open. FileInfo
/// identity is preserved separately from the selected archive and opened
/// stream identities so a caller cannot silently substitute any of them.
struct Ogre14SelectedTextureSourceCaptureInput final {
  std::uint32_t version = kOgre14SelectedTextureSourceCaptureInputVersion;
  Ogre14SelectedTextureSourceKind source_kind =
      Ogre14SelectedTextureSourceKind::
          UNAUTHENTICATED_PACKAGE_ARCHIVE_MEMBER;
  std::string effective_resource_group;
  std::uint64_t group_generation = 0U;

  std::string selected_archive_name;
  std::string selected_archive_type;
  std::uintptr_t selected_archive_pointer_token = 0U;

  /// Ogre::FileInfo::archive must be the exact selected archive instance.
  std::uintptr_t file_info_archive_pointer_token = 0U;
  std::string file_info_filename;
  std::string file_info_path;
  std::string file_info_basename;
  /// Canonical selected member; exactly path plus basename.
  std::string exact_member_name;
  std::uint64_t file_info_compressed_size = 0U;
  std::uint64_t file_info_uncompressed_size = 0U;

  /// Exact stream returned by opening exact_member_name.
  std::uintptr_t opened_stream_pointer_token = 0U;
  std::string opened_stream_name;
  std::uint64_t opened_stream_size = 0U;

  std::uintptr_t resource_pointer_token = 0U;
  std::uint64_t resource_handle = 0U;
  std::string exact_resource_name;
  /// Ogre::Resource::getStateCount() immediately before the source load.
  std::uint64_t resource_state_count_before_load = 0U;
};

struct Ogre14SelectedTextureSourceReceiptMetadata final {
  std::uint32_t version = kOgre14SelectedTextureSourceReceiptVersion;
  Ogre14SelectedTextureSourceCaptureInput source;
  std::uint64_t byte_count = 0U;
  std::string observed_bytes_sha256;
};

enum class Ogre14SelectedTextureSourceTransactionStage : std::uint8_t {
  AFTER_SOURCE_BYTES_COPIED = 0U,
  BEFORE_RECEIPT_COMMIT = 1U,
  BEFORE_REGISTRY_COMMIT = 2U,
  BEFORE_GROUP_TRANSITION_COMMIT = 3U,
  BEFORE_RESOLUTION_COMMIT = 4U,
};

/// Borrowed deterministic test seam. Production passes null. It may throw;
/// every operation preserves its prior immutable publication on failure.
class IOgre14SelectedTextureSourceFaultInjector {
public:
  virtual ~IOgre14SelectedTextureSourceFaultInjector() = default;
  virtual void BeforeSelectedTextureSourceStage(
      Ogre14SelectedTextureSourceTransactionStage) {}
};

class Ogre14SelectedTextureSourceReceiptRegistry;
class Ogre14SelectedTextureSourceResolution;
class IOgre14SelectedTextureSourceResolver;

namespace Testing {
class Ogre14SelectedTextureSourceResolutionTestAccess;
}

/// Immutable owner of the exact selected source bytes and observation.
class Ogre14SelectedTextureSourceReceipt final {
public:
  Ogre14SelectedTextureSourceReceipt() = default;
  ~Ogre14SelectedTextureSourceReceipt() = default;
  Ogre14SelectedTextureSourceReceipt(
      const Ogre14SelectedTextureSourceReceipt &) noexcept = default;
  Ogre14SelectedTextureSourceReceipt &operator=(
      const Ogre14SelectedTextureSourceReceipt &) noexcept = default;
  Ogre14SelectedTextureSourceReceipt(
      Ogre14SelectedTextureSourceReceipt &&) noexcept = default;
  Ogre14SelectedTextureSourceReceipt &operator=(
      Ogre14SelectedTextureSourceReceipt &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const Ogre14SelectedTextureSourceReceiptMetadata *metadata()
      const noexcept;
  [[nodiscard]] const std::uint8_t *source_bytes() const noexcept;
  [[nodiscard]] std::size_t source_size() const noexcept;
  [[nodiscard]] std::uint64_t identity_size() const noexcept;
  [[nodiscard]] bool ReplacementBytesMatch(const void *bytes,
                                            std::size_t size) const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14SelectedTextureSourceReceipt &other) const noexcept;

private:
  struct State;
  explicit Ogre14SelectedTextureSourceReceipt(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend ValidationResult BuildOgre14SelectedTextureSourceReceipt(
      const Ogre14SelectedTextureSourceRegistryConfiguration &,
      const Ogre14SelectedTextureSourceCaptureInput &, const void *,
      std::size_t, Ogre14SelectedTextureSourceReceipt &,
      IOgre14SelectedTextureSourceFaultInjector *);
  friend class Ogre14SelectedTextureSourceReceiptRegistry;
  friend ValidationResult CommitOgre14SelectedTextureSourceReceipt(
      const Ogre14SelectedTextureSourceReceipt &,
      Ogre14SelectedTextureSourceReceiptRegistry &,
      IOgre14SelectedTextureSourceFaultInjector *);
};

/// Immutable-snapshot registry. Mutations clone and validate a complete
/// candidate before publishing it, so copies retain their original state.
class Ogre14SelectedTextureSourceReceiptRegistry final {
public:
  Ogre14SelectedTextureSourceReceiptRegistry() = default;
  ~Ogre14SelectedTextureSourceReceiptRegistry() = default;
  Ogre14SelectedTextureSourceReceiptRegistry(
      const Ogre14SelectedTextureSourceReceiptRegistry &) noexcept = default;
  Ogre14SelectedTextureSourceReceiptRegistry &operator=(
      const Ogre14SelectedTextureSourceReceiptRegistry &) noexcept = default;
  Ogre14SelectedTextureSourceReceiptRegistry(
      Ogre14SelectedTextureSourceReceiptRegistry &&) noexcept = default;
  Ogre14SelectedTextureSourceReceiptRegistry &operator=(
      Ogre14SelectedTextureSourceReceiptRegistry &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::uint64_t retained_source_bytes() const noexcept;
  [[nodiscard]] std::uint64_t retained_identity_bytes() const noexcept;
  [[nodiscard]] std::uint64_t maximum_group_generation_seen() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14SelectedTextureSourceReceiptRegistry &other) const noexcept;

  [[nodiscard]] ValidationResult FindResource(
      const std::string &effective_resource_group,
      std::uint64_t group_generation, std::uintptr_t resource_pointer_token,
      std::uint64_t resource_handle, const std::string &exact_resource_name,
      Ogre14SelectedTextureSourceReceipt &receipt) const;

private:
  struct State;
  explicit Ogre14SelectedTextureSourceReceiptRegistry(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  [[nodiscard]] ValidationResult MintLoadedResourceResolution(
      const std::string &effective_resource_group,
      std::uint64_t group_generation, std::uintptr_t resource_pointer_token,
      std::uint64_t resource_handle, const std::string &exact_resource_name,
      std::uint64_t loaded_resource_state_count,
      std::uintptr_t resolver_pointer_token,
      Ogre14SelectedTextureSourceResolution &resolution,
      IOgre14SelectedTextureSourceFaultInjector *fault_injector = nullptr)
      const;
  [[nodiscard]] bool RevalidateLoadedResourceResolution(
      const Ogre14SelectedTextureSourceResolution &resolution,
      std::uintptr_t resolver_pointer_token,
      std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
      const std::string &exact_resource_group,
      const std::string &exact_resource_name,
      std::uint64_t loaded_resource_state_count) const noexcept;

  friend class ::RoR::ContentManager;
  friend class Testing::Ogre14SelectedTextureSourceResolutionTestAccess;
  friend ValidationResult InitializeOgre14SelectedTextureSourceRegistry(
      const Ogre14SelectedTextureSourceRegistryConfiguration &,
      Ogre14SelectedTextureSourceReceiptRegistry &);
  friend ValidationResult AdvanceOgre14SelectedTextureSourceGroupGeneration(
      const std::string &, std::uint64_t,
      Ogre14SelectedTextureSourceReceiptRegistry &,
      IOgre14SelectedTextureSourceFaultInjector *);
  friend ValidationResult TeardownOgre14SelectedTextureSourceGroup(
      const std::string &, std::uint64_t,
      Ogre14SelectedTextureSourceReceiptRegistry &,
      IOgre14SelectedTextureSourceFaultInjector *);
  friend ValidationResult CommitOgre14SelectedTextureSourceReceipt(
      const Ogre14SelectedTextureSourceReceipt &,
      Ogre14SelectedTextureSourceReceiptRegistry &,
      IOgre14SelectedTextureSourceFaultInjector *);
  friend ValidationResult RemoveOgre14SelectedTextureSourceResource(
      const std::string &, std::uint64_t, std::uintptr_t, std::uint64_t,
      const std::string &, Ogre14SelectedTextureSourceReceiptRegistry &,
      IOgre14SelectedTextureSourceFaultInjector *);
  friend void PoisonOgre14SelectedTextureSourceRegistry(
      Ogre14SelectedTextureSourceReceiptRegistry &) noexcept;
};

/// Opaque registry-minted proof for one successfully loaded texture. Copies
/// retain the exact registry publication and source-receipt control blocks.
class Ogre14SelectedTextureSourceResolution final {
public:
  Ogre14SelectedTextureSourceResolution() noexcept = default;
  ~Ogre14SelectedTextureSourceResolution() = default;
  Ogre14SelectedTextureSourceResolution(
      const Ogre14SelectedTextureSourceResolution &) noexcept = default;
  Ogre14SelectedTextureSourceResolution &operator=(
      const Ogre14SelectedTextureSourceResolution &) noexcept = default;
  Ogre14SelectedTextureSourceResolution(
      Ogre14SelectedTextureSourceResolution &&) noexcept = default;
  Ogre14SelectedTextureSourceResolution &operator=(
      Ogre14SelectedTextureSourceResolution &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] const Ogre14SelectedTextureSourceReceipt *source_receipt()
      const noexcept;
  [[nodiscard]] std::uint64_t loaded_resource_state_count() const noexcept;
  [[nodiscard]] bool SharesLoadedResourceAuthorityWith(
      const Ogre14SelectedTextureSourceResolution &other) const noexcept;
  [[nodiscard]] bool MatchesResolver(
      const IOgre14SelectedTextureSourceResolver &resolver) const noexcept;
  [[nodiscard]] bool MatchesLoadedResourceIdentity(
      std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
      const std::string &exact_resource_group,
      const std::string &exact_resource_name,
      std::uint64_t loaded_resource_state_count) const noexcept;

private:
  struct State;
  explicit Ogre14SelectedTextureSourceResolution(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14SelectedTextureSourceReceiptRegistry;
  friend class Testing::Ogre14SelectedTextureSourceResolutionTestAccess;
};

/// Narrow OGRE-facing seam. Implementations resolve and revalidate while
/// holding a strong TexturePtr and while serialized with resource lifecycle.
class IOgre14SelectedTextureSourceResolver {
public:
  virtual ~IOgre14SelectedTextureSourceResolver() = default;

  /// Failure leaves resolution untouched.
  [[nodiscard]] virtual ValidationResult ResolveSelectedTextureSource(
      Ogre::Texture &texture,
      Ogre14SelectedTextureSourceResolution &resolution) const = 0;

  /// Allocation-free final check immediately before cached reuse or publish.
  [[nodiscard]] virtual bool RevalidateSelectedTextureSource(
      Ogre::Texture &texture,
      const Ogre14SelectedTextureSourceResolution &resolution) const
      noexcept = 0;
};

[[nodiscard]] ValidationResult
ValidateOgre14SelectedTextureSourceRegistryConfiguration(
    const Ogre14SelectedTextureSourceRegistryConfiguration &configuration);

[[nodiscard]] ValidationResult ValidateOgre14SelectedTextureSourceCaptureInput(
    const Ogre14SelectedTextureSourceCaptureInput &input);

[[nodiscard]] ValidationResult BuildOgre14SelectedTextureSourceReceipt(
    const Ogre14SelectedTextureSourceRegistryConfiguration &configuration,
    const Ogre14SelectedTextureSourceCaptureInput &input,
    const void *source_bytes, std::size_t source_size,
    Ogre14SelectedTextureSourceReceipt &output,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector = nullptr);

[[nodiscard]] ValidationResult InitializeOgre14SelectedTextureSourceRegistry(
    const Ogre14SelectedTextureSourceRegistryConfiguration &configuration,
    Ogre14SelectedTextureSourceReceiptRegistry &output);

/// Generations are globally strictly monotonic. Advancing a group first clears
/// every receipt from that group, preventing stale capture resurrection.
[[nodiscard]] ValidationResult
AdvanceOgre14SelectedTextureSourceGroupGeneration(
    const std::string &effective_resource_group,
    std::uint64_t new_group_generation,
    Ogre14SelectedTextureSourceReceiptRegistry &registry,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector = nullptr);

[[nodiscard]] ValidationResult TeardownOgre14SelectedTextureSourceGroup(
    const std::string &effective_resource_group,
    std::uint64_t exact_group_generation,
    Ogre14SelectedTextureSourceReceiptRegistry &registry,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector = nullptr);

/// A same-state/same-source retry replaces the exact stream observation.
/// Reload replacement requires the same exact resource identity and a
/// strictly increased pre-load state.
[[nodiscard]] ValidationResult CommitOgre14SelectedTextureSourceReceipt(
    const Ogre14SelectedTextureSourceReceipt &receipt,
    Ogre14SelectedTextureSourceReceiptRegistry &registry,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector = nullptr);

/// Missing is idempotent; an existing pointer must match every supplied field.
[[nodiscard]] ValidationResult RemoveOgre14SelectedTextureSourceResource(
    const std::string &effective_resource_group,
    std::uint64_t exact_group_generation,
    std::uintptr_t resource_pointer_token, std::uint64_t resource_handle,
    const std::string &exact_resource_name,
    Ogre14SelectedTextureSourceReceiptRegistry &registry,
    IOgre14SelectedTextureSourceFaultInjector *fault_injector = nullptr);

/// Terminal fail-closed publication used only after an external OGRE mutation
/// has succeeded but the matching COW registry publication could not commit.
void PoisonOgre14SelectedTextureSourceRegistry(
    Ogre14SelectedTextureSourceReceiptRegistry &registry) noexcept;

} // namespace RoR::Render
