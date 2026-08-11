/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Explicit semantic declarations for OGRE 14 legacy materials.

#pragma once

#include "Ogre14LegacyNativeAssetExtractor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyMaterialSemanticRegistryVersion = 1U;
constexpr std::uint32_t kOgre14LegacyMaterialSemanticDeclarationVersion = 1U;
constexpr std::uint32_t kOgre14LegacyMaterialSemanticResolutionVersion = 1U;
constexpr std::size_t kDefaultOgre14LegacyMaximumSemanticDeclarations =
    65536U;
constexpr std::uint64_t kDefaultOgre14LegacyMaximumSemanticKeyBytes =
    16U * 1024U * 1024U;

/// The declaration source is authored evidence. Runtime pass state, resource
/// names, filenames, and legacy specular values are never declaration sources.
enum class Ogre14LegacyMaterialSemanticSource : std::uint8_t {
  CONTENT_METADATA = 0U,
  VERSIONED_COMPATIBILITY_TABLE = 1U,
};

struct Ogre14LegacyMaterialSemanticRegistryConfiguration final {
  std::uint32_t version = kOgre14LegacyMaterialSemanticRegistryVersion;
  std::size_t maximum_declarations =
      kDefaultOgre14LegacyMaximumSemanticDeclarations;
  std::uint64_t maximum_total_key_bytes =
      kDefaultOgre14LegacyMaximumSemanticKeyBytes;
};

struct Ogre14LegacyMaterialSemanticDeclaration final {
  std::uint32_t version = kOgre14LegacyMaterialSemanticDeclarationVersion;
  Ogre14LegacyAssetKey material_key;
  Ogre14LegacyMaterialSemanticSource source =
      Ogre14LegacyMaterialSemanticSource::CONTENT_METADATA;
  /// Monotonic revision of the exact metadata record or compatibility table.
  std::uint64_t source_revision = 0U;
  Ogre14LegacyBaseColorSemantic base_color_semantic =
      Ogre14LegacyBaseColorSemantic::UNLIT;
  Ogre14LegacyTextureColorRole texture_color_role =
      Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
};

class Ogre14LegacyMaterialSemanticRegistry;

/// Unforgeable identity of one declaration in one immutable registry build.
/// Numeric revisions and diagnostic fingerprints cannot substitute for this
/// pointer-exact lineage. Callers may retain/copy a receipt, but only a
/// registry build can mint a nonempty one.
class Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt final {
public:
  Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt() noexcept = default;
  Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt(
      const Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &) noexcept =
      default;
  Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &operator=(
      const Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &) noexcept =
      default;
  Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt(
      Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &&) noexcept =
      default;
  Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &operator=(
      Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &&) noexcept =
      default;
  ~Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt() = default;

  [[nodiscard]] bool has_value() const noexcept { return owner_ != nullptr; }
  void swap(
      Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &other) noexcept {
    owner_.swap(other.owner_);
  }

private:
  explicit Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt(
      std::shared_ptr<const void> owner) noexcept
      : owner_(std::move(owner)) {}

  std::shared_ptr<const void> owner_;

  friend class Ogre14LegacyMaterialSemanticRegistry;
  friend bool SameOgre14LegacyMaterialSemanticDeclarationIdentity(
      const Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &,
      const Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &) noexcept;
};

/// Pointer-exact identity comparison. Two empty receipts never authenticate.
[[nodiscard]] bool SameOgre14LegacyMaterialSemanticDeclarationIdentity(
    const Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &lhs,
    const Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt &rhs) noexcept;

/// Resolution retains the explicit declaration provenance alongside the exact
/// native-extractor input. `registry_fingerprint` is diagnostic content
/// provenance, not an authentication token or collision-free identity.
struct Ogre14LegacyMaterialSemanticResolution final {
  std::uint32_t version = kOgre14LegacyMaterialSemanticResolutionVersion;
  Ogre14LegacyAssetKey material_key;
  Ogre14LegacyMaterialSemanticSource source =
      Ogre14LegacyMaterialSemanticSource::CONTENT_METADATA;
  std::uint64_t source_revision = 0U;
  std::uint64_t registry_fingerprint = 0U;
  Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt declaration_identity;
  Ogre14LegacyNativeMaterialDeclaration native_declaration;
};

enum class Ogre14LegacyMaterialSemanticRegistryBuildStage : std::uint8_t {
  AFTER_FIRST_DECLARATION = 0U,
  BEFORE_COMMIT = 1U,
};

class IOgre14LegacyMaterialSemanticRegistryFaultInjector {
public:
  virtual ~IOgre14LegacyMaterialSemanticRegistryFaultInjector() = default;
  /// Borrowed test seam. Production passes null. Implementations may throw.
  virtual void BeforeRegistryBuildStage(
      Ogre14LegacyMaterialSemanticRegistryBuildStage) {}
};

class Ogre14LegacyMaterialSemanticRegistry final {
public:
  Ogre14LegacyMaterialSemanticRegistry() = default;
  ~Ogre14LegacyMaterialSemanticRegistry() = default;
  Ogre14LegacyMaterialSemanticRegistry(
      const Ogre14LegacyMaterialSemanticRegistry &) noexcept = default;
  Ogre14LegacyMaterialSemanticRegistry &operator=(
      const Ogre14LegacyMaterialSemanticRegistry &) noexcept = default;
  Ogre14LegacyMaterialSemanticRegistry(
      Ogre14LegacyMaterialSemanticRegistry &&) noexcept = default;
  Ogre14LegacyMaterialSemanticRegistry &operator=(
      Ogre14LegacyMaterialSemanticRegistry &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::uint64_t content_fingerprint() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14LegacyMaterialSemanticRegistry &other) const noexcept;

  /// Exact, case-sensitive lookup. Invalid/missing keys, configurations, or
  /// allocations leave `resolution` unchanged.
  [[nodiscard]] ValidationResult Resolve(
      const Ogre14LegacyAssetKey &material_key,
      const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
      Ogre14LegacyMaterialSemanticResolution &resolution) const;

private:
  struct State;
  explicit Ogre14LegacyMaterialSemanticRegistry(
      std::shared_ptr<const State> state) noexcept;
  [[nodiscard]] static
      Ogre14LegacyMaterialSemanticDeclarationIdentityReceipt
      MintDeclarationIdentity();

  std::shared_ptr<const State> state_;

  friend ValidationResult BuildOgre14LegacyMaterialSemanticRegistry(
      const Ogre14LegacyMaterialSemanticRegistryConfiguration &,
      const std::vector<Ogre14LegacyMaterialSemanticDeclaration> &,
      Ogre14LegacyMaterialSemanticRegistry &,
      IOgre14LegacyMaterialSemanticRegistryFaultInjector *);
};

[[nodiscard]] bool Ogre14LegacyMaterialSemanticResolutionMatchesKey(
    const Ogre14LegacyMaterialSemanticResolution &resolution,
    const Ogre14LegacyAssetKey &material_key) noexcept;

/// Authenticates an issued resolution (for example, the exact declaration
/// supplied to native capture) against a fresh authoritative resolution. All
/// public fields must agree and both must carry the same unforgeable receipt.
[[nodiscard]] bool Ogre14LegacyMaterialSemanticResolutionAuthenticates(
    const Ogre14LegacyMaterialSemanticResolution &issued,
    const Ogre14LegacyMaterialSemanticResolution &authoritative) noexcept;

/// Builds an immutable, sorted registry. Duplicates are rejected even when
/// byte-identical, so one exact material key has one authoritative declaration
/// location. Every failure leaves `output` and its immutable owner unchanged.
[[nodiscard]] ValidationResult BuildOgre14LegacyMaterialSemanticRegistry(
    const Ogre14LegacyMaterialSemanticRegistryConfiguration &configuration,
    const std::vector<Ogre14LegacyMaterialSemanticDeclaration> &declarations,
    Ogre14LegacyMaterialSemanticRegistry &output,
    IOgre14LegacyMaterialSemanticRegistryFaultInjector *fault_injector =
        nullptr);

[[nodiscard]] ValidationResult
ValidateOgre14LegacyMaterialSemanticRegistryConfiguration(
    const Ogre14LegacyMaterialSemanticRegistryConfiguration &configuration);

[[nodiscard]] ValidationResult ValidateOgre14LegacyMaterialSemanticDeclaration(
    const Ogre14LegacyMaterialSemanticDeclaration &declaration);

} // namespace RoR::Render
