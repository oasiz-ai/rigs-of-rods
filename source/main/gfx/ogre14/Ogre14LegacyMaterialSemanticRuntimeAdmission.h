/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Fail-closed activation and live admission for reviewed RORMAT2 data.

#pragma once

#include "Ogre14AuthenticatedMaterialScriptReceipt.h"
#include "Ogre14LegacyMaterialSemanticCatalogV2.h"

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
    kOgre14LegacyMaterialSemanticApprovedManifestVersion = 1U;
constexpr std::uint32_t
    kOgre14LegacyMaterialSemanticRuntimeAuthorityVersion = 1U;
constexpr std::uint32_t
    kOgre14LegacyMaterialSemanticAdmissionVersion = 1U;
constexpr char kOgre14LegacyReviewedPackageRevisionV1Domain[] =
    "REVIEWED_PACKAGE_REVISION_V1";

/// One source in the canonical compiler closure approved out of band. This is
/// only a value description; constructing or copying it grants no authority.
struct Ogre14LegacyApprovedMaterialScriptSource final {
  Ogre14MaterialScriptSourceRole source_role =
      Ogre14MaterialScriptSourceRole::ROOT_SCRIPT;
  std::string exact_member_name;
  Ogre14LegacySha256 original_sha256{};
  Ogre14LegacySha256 effective_sha256{};
  Ogre14MaterialScriptRepairState repair_state =
      Ogre14MaterialScriptRepairState::NONE;
  std::uint32_t repair_plan_version = 0U;
  Ogre14LegacySha256 repair_plan_sha256{};
};

/// Exact loaded texture-source binding approved for one catalog unit. V1 live
/// admission accepts authenticated archive members only. Runtime generation is
/// intentionally absent here: admission binds the current texture receipt to
/// the current script receipt's runtime nonce instead of reviewing mount order.
struct Ogre14LegacyApprovedMaterialTextureSource final {
  std::uint16_t ordinal = 0U;
  Ogre14LegacyAssetKey texture_key;
  Ogre14AuthenticatedTextureSourceKind source_kind =
      Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER;
  std::string exact_member_name;
};

/// Complete ordered source closure for one exact catalog record. The primary
/// index binds RORMAT2's source member/digests to one element; imports remain
/// independently pinned rather than being ignored by the v2 record.
struct Ogre14LegacyApprovedMaterialScriptClosure final {
  Ogre14LegacyAssetKey material_key;
  std::size_t primary_source_index = 0U;
  std::vector<Ogre14LegacyApprovedMaterialScriptSource> sources;
  std::vector<Ogre14LegacyApprovedMaterialTextureSource> texture_sources;
};

/// Candidate input for a future trusted compiled/signed configuration loader.
/// It is deliberately not itself an approval token. Production currently has
/// no public function which converts this value into an approved manifest.
struct Ogre14LegacyMaterialSemanticApprovedManifestDescription final {
  Ogre14LegacySha256 catalog_full_file_sha256{};
  Ogre14LegacySha256 package_archive_sha256{};
  std::string exact_resource_group;
  /// Stable reviewed package/catalog revision in the explicit domain above.
  /// This is never a ContentManager resource-group generation nonce.
  std::uint64_t reviewed_resource_generation = 0U;
  std::vector<Ogre14LegacyApprovedMaterialScriptClosure> material_closures;
};

class Ogre14LegacyMaterialSemanticRuntimeAuthority;
class Ogre14LegacyMaterialSemanticAdmission;
class IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector;

#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
namespace Testing {
class Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess;
}
#endif

/// Non-caller-mintable approval authority. A full-file SHA carried beside
/// caller bytes is forgeable, so authentication accepts only this opaque owner.
/// A future production loader must be a complete library-owned type which
/// authenticates compiled or signed configuration before any mint access is
/// added; there is deliberately no production friend seam today.
class Ogre14LegacyMaterialSemanticApprovedManifest final {
public:
  Ogre14LegacyMaterialSemanticApprovedManifest() noexcept = default;
  ~Ogre14LegacyMaterialSemanticApprovedManifest() = default;
  Ogre14LegacyMaterialSemanticApprovedManifest(
      const Ogre14LegacyMaterialSemanticApprovedManifest &) noexcept = default;
  Ogre14LegacyMaterialSemanticApprovedManifest &operator=(
      const Ogre14LegacyMaterialSemanticApprovedManifest &) noexcept = default;
  Ogre14LegacyMaterialSemanticApprovedManifest(
      Ogre14LegacyMaterialSemanticApprovedManifest &&) noexcept = default;
  Ogre14LegacyMaterialSemanticApprovedManifest &operator=(
      Ogre14LegacyMaterialSemanticApprovedManifest &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] const char *domain() const noexcept;
  [[nodiscard]] std::uint64_t reviewed_resource_generation() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14LegacyMaterialSemanticApprovedManifest &) const noexcept;

private:
  struct State;
  explicit Ogre14LegacyMaterialSemanticApprovedManifest(
      std::shared_ptr<const State>) noexcept;
  [[nodiscard]] static ValidationResult MintFromTrustedDescription(
      const Ogre14LegacyMaterialSemanticApprovedManifestDescription &,
      Ogre14LegacyMaterialSemanticApprovedManifest &);
  std::shared_ptr<const State> state_;

  friend class Ogre14LegacyMaterialSemanticRuntimeAuthority;
  friend ValidationResult AuthenticateOgre14LegacyMaterialSemanticRuntime(
      const Ogre14LegacyMaterialSemanticApprovedManifest &,
      const Ogre14LegacyMaterialSemanticCatalogV2Configuration &,
      const Ogre14LegacyMaterialSemanticRegistryConfiguration &,
      const std::vector<std::uint8_t> &,
      Ogre14LegacyMaterialSemanticRuntimeAuthority &
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
      , class IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *
#endif
      );
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  friend class Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess;
#endif
};

#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
/// Synthetic rollback checkpoints. The type and callback exist only in the
/// dedicated test build, so production callers cannot interpose between hash,
/// parse, live native capture, final revalidation, or publication.
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

class IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector {
public:
  virtual ~IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector() =
      default;
  virtual void BeforeOgre14LegacyMaterialSemanticRuntimeAdmissionStage(
      Ogre14LegacyMaterialSemanticRuntimeAdmissionStage) {}
};
#endif

/// Immutable authenticated catalog activation. It retains the exact approved
/// manifest, parsed catalog, and registry publications. Diagnostic hashes and
/// revisions cannot substitute for this owner identity.
class Ogre14LegacyMaterialSemanticRuntimeAuthority final {
public:
  Ogre14LegacyMaterialSemanticRuntimeAuthority() noexcept = default;
  ~Ogre14LegacyMaterialSemanticRuntimeAuthority() = default;
  Ogre14LegacyMaterialSemanticRuntimeAuthority(
      const Ogre14LegacyMaterialSemanticRuntimeAuthority &) noexcept = default;
  Ogre14LegacyMaterialSemanticRuntimeAuthority &operator=(
      const Ogre14LegacyMaterialSemanticRuntimeAuthority &) noexcept = default;
  Ogre14LegacyMaterialSemanticRuntimeAuthority(
      Ogre14LegacyMaterialSemanticRuntimeAuthority &&) noexcept = default;
  Ogre14LegacyMaterialSemanticRuntimeAuthority &operator=(
      Ogre14LegacyMaterialSemanticRuntimeAuthority &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::uint64_t reviewed_resource_generation() const noexcept;
  [[nodiscard]] const Ogre14LegacyMaterialSemanticRegistry *semantic_registry()
      const noexcept;
  [[nodiscard]] ValidationResult ResolveMaterialSemantics(
      const Ogre14LegacyAssetKey &,
      const Ogre14LegacyAssetTranslatorConfiguration &,
      Ogre14LegacyMaterialSemanticResolution &) const;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14LegacyMaterialSemanticRuntimeAuthority &) const noexcept;
  [[nodiscard]] bool AuthenticatesAdmission(
      const Ogre14LegacyMaterialSemanticAdmission &,
      const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &,
      const Ogre14AuthenticatedTextureAuthoritySnapshot &) const noexcept;
  [[nodiscard]] ValidationResult RevalidateAdmission(
      const Ogre14LegacyMaterialSemanticAdmission &,
      const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &,
      const Ogre14AuthenticatedTextureAuthoritySnapshot &) const;

private:
  struct State;
  explicit Ogre14LegacyMaterialSemanticRuntimeAuthority(
      std::shared_ptr<const State>) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14LegacyMaterialSemanticAdmission;

  [[nodiscard]] ValidationResult ValidateScriptAndSemanticPrerequisites(
      const Ogre14AuthenticatedMaterialScriptResolution &,
      const Ogre14LegacyMaterialSemanticResolution &,
      const Ogre14LegacyAssetKey &,
      IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *) const;
  [[nodiscard]] ValidationResult ValidateNativeCapture(
      const Ogre14LegacyAssetKey &,
      std::uint64_t,
      const Ogre14LegacyNativeMaterialCapture &,
      IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *) const;
  [[nodiscard]] ValidationResult PublishAdmission(
      const Ogre14AuthenticatedMaterialScriptResolution &,
      const Ogre14AuthenticatedMaterialScriptAuthoritySnapshot &,
      const Ogre14LegacyMaterialSemanticResolution &,
      Ogre14LegacyNativeMaterialCapture,
      Ogre14LegacyMaterialSemanticAdmission &,
      IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *) const;
  template <typename LiveAuthority>
  [[nodiscard]] ValidationResult CaptureAndAdmitWithLiveAuthority(
      const Ogre14LegacyAssetTranslatorConfiguration &,
      Ogre::Material &,
      LiveAuthority &,
      Ogre14LegacyMaterialSemanticAdmission &,
      IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *) const;

  friend ValidationResult AuthenticateOgre14LegacyMaterialSemanticRuntime(
      const Ogre14LegacyMaterialSemanticApprovedManifest &,
      const Ogre14LegacyMaterialSemanticCatalogV2Configuration &,
      const Ogre14LegacyMaterialSemanticRegistryConfiguration &,
      const std::vector<std::uint8_t> &,
      Ogre14LegacyMaterialSemanticRuntimeAuthority &
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
      , IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *
#endif
      );
  friend ValidationResult CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(
      const Ogre14LegacyMaterialSemanticRuntimeAuthority &,
      const Ogre14LegacyAssetTranslatorConfiguration &,
      Ogre::Material &,
      ::RoR::ContentManager &,
      Ogre14LegacyMaterialSemanticAdmission &
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
      , IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *
#endif
      );
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  friend ValidationResult CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(
      const Ogre14LegacyMaterialSemanticRuntimeAuthority &,
      const Ogre14LegacyAssetTranslatorConfiguration &,
      Ogre::Material &,
      class IOgre14LegacyMaterialRuntimeLiveAuthority &,
      Ogre14LegacyMaterialSemanticAdmission &,
      IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *);
  friend class Testing::Ogre14LegacyMaterialSemanticRuntimeAdmissionTestAccess;
#endif
};

/// Immutable per-material proof minted only after live source, semantic, native
/// digest, and final-current revalidation all succeed in one call.
class Ogre14LegacyMaterialSemanticAdmission final {
public:
  Ogre14LegacyMaterialSemanticAdmission() noexcept = default;
  ~Ogre14LegacyMaterialSemanticAdmission() = default;
  Ogre14LegacyMaterialSemanticAdmission(
      const Ogre14LegacyMaterialSemanticAdmission &) noexcept = default;
  Ogre14LegacyMaterialSemanticAdmission &operator=(
      const Ogre14LegacyMaterialSemanticAdmission &) noexcept = default;
  Ogre14LegacyMaterialSemanticAdmission(
      Ogre14LegacyMaterialSemanticAdmission &&) noexcept = default;
  Ogre14LegacyMaterialSemanticAdmission &operator=(
      Ogre14LegacyMaterialSemanticAdmission &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] const Ogre14LegacyAssetKey *material_key() const noexcept;
  [[nodiscard]] std::uint64_t reviewed_resource_generation() const noexcept;
  [[nodiscard]] std::uint64_t runtime_group_generation() const noexcept;
  [[nodiscard]] const Ogre14LegacyMaterialSemanticResolution *
  semantic_resolution() const noexcept;
  [[nodiscard]] const Ogre14LegacyNativeMaterialCapture *native_capture() const
      noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14LegacyMaterialSemanticAdmission &) const noexcept;

private:
  struct State;
  explicit Ogre14LegacyMaterialSemanticAdmission(
      std::shared_ptr<const State>) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14LegacyMaterialSemanticRuntimeAuthority;
};

#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
/// Synthetic-only combined authority. Production accepts only the final
/// ContentManager concrete edge and cannot be supplied an independently
/// implemented resolver/provider object.
class IOgre14LegacyMaterialRuntimeLiveAuthority
    : public IOgre14AuthenticatedMaterialScriptResolver,
      public IOgre14AuthenticatedMaterialScriptAuthorityProvider,
      public IOgre14AuthenticatedTextureResolver,
      public IOgre14AuthenticatedTextureAuthorityProvider {
public:
  ~IOgre14LegacyMaterialRuntimeLiveAuthority() override = default;
};
#endif

/// Authenticates an exact full RORMAT2 file against an opaque approved
/// manifest, parses it, checks the manifest's trusted scope and complete
/// closure inventory, builds an exact registry, and publishes atomically.
[[nodiscard]] ValidationResult AuthenticateOgre14LegacyMaterialSemanticRuntime(
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
        *fault_injector = nullptr
#endif
    );

/// Production live path. The function resolves the current script authority,
/// resolves semantics, captures through the native extractor with the same
/// combined authority, verifies the opaque native receipt and reviewed digest,
/// performs fresh semantic and no-throw script revalidation, and only then
/// publishes an opaque admission. Failure leaves `output` unchanged.
[[nodiscard]] ValidationResult
CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(
    const Ogre14LegacyMaterialSemanticRuntimeAuthority &runtime_authority,
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    Ogre::Material &material,
    ::RoR::ContentManager &content_manager,
    Ogre14LegacyMaterialSemanticAdmission &output
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
    ,
    IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector
        *fault_injector = nullptr
#endif
    );

#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
/// Synthetic-only overload for hostile authority and rollback fixtures.
[[nodiscard]] ValidationResult
CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(
    const Ogre14LegacyMaterialSemanticRuntimeAuthority &runtime_authority,
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    Ogre::Material &material,
    IOgre14LegacyMaterialRuntimeLiveAuthority &live_authority,
    Ogre14LegacyMaterialSemanticAdmission &output,
    IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector
        *fault_injector = nullptr);
#endif

} // namespace RoR::Render
