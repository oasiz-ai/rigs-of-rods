/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Atomic legacy-material frame preparation for the live OGRE adapter.

#pragma once

#include "Ogre14LegacyMaterialSemanticRuntimeAdmission.h"
#include "gfx/render/Ogre14LegacyMaterialClosure.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyLiveMaterialCoordinatorVersion = 1U;
constexpr std::uint32_t kOgre14LegacyMaterialObservationVersion = 1U;
constexpr std::uint32_t kOgre14LegacyPreparedMaterialFrameVersion = 1U;
constexpr std::uint32_t kOgre14LegacyAdmittedPreparedMaterialFrameVersion = 1U;
constexpr std::size_t kDefaultOgre14LegacyMaximumMaterialObservations =
    kDefaultOgre14LegacyMaximumMaterialInputsPerFrame;

struct Ogre14LegacyLiveMaterialCoordinatorConfiguration final {
  std::uint32_t version = kOgre14LegacyLiveMaterialCoordinatorVersion;
  Ogre14LegacyAssetTranslatorConfiguration translator;
  Ogre14LegacyAssetTranslatorTransactionConfiguration transaction;
  std::size_t maximum_material_observations =
      kDefaultOgre14LegacyMaximumMaterialObservations;
};

/// One native capture and the exact key used to resolve its authored semantic
/// declaration. The coordinator resolves the immutable semantic registry
/// itself and requires the returned declaration values and registry-minted
/// identity receipt to authenticate. Textured captures additionally require
/// one aligned registry-minted loaded-resource resolution per native texture;
/// the compatibility extractor's unauthenticated textured output is rejected.
struct Ogre14LegacyMaterialObservation final {
  std::uint32_t version = kOgre14LegacyMaterialObservationVersion;
  Ogre14LegacyAssetKey material_key;
  /// Exact declaration issued by this coordinator before native capture. Its
  /// provenance and native values are re-resolved and compared during frame
  /// preparation; the semantic registry's opaque receipt authenticates it.
  Ogre14LegacyMaterialSemanticResolution semantic_resolution;
  Ogre14LegacyNativeMaterialCapture native_capture;
};

struct Ogre14LegacyPreparedMaterial final {
  Ogre14LegacyAssetKey material_key;
  /// Exact independently captured native owner, retained without copying or
  /// reboxing. Procedural roads assign this owner directly to
  /// Ogre14ProceduralRoadCapture::exact_native_material_audit.
  std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit>
      native_material_audit;
  /// Separately owned translated closure. Its audit must be bit-exact with the
  /// native owner above but may never share the same control block.
  std::shared_ptr<const Ogre14LegacyMaterialClosure> closure;
};

/// Immutable frame and closure owners exposed to the scene transaction. The
/// owners remain readable after discard, but only the coordinator with the
/// matching active lease can publish their translator state.
class Ogre14LegacyPreparedMaterialFrame final {
public:
  Ogre14LegacyPreparedMaterialFrame() noexcept = default;
  ~Ogre14LegacyPreparedMaterialFrame() = default;
  Ogre14LegacyPreparedMaterialFrame(
      const Ogre14LegacyPreparedMaterialFrame &) noexcept = default;
  Ogre14LegacyPreparedMaterialFrame &
  operator=(const Ogre14LegacyPreparedMaterialFrame &) noexcept = default;
  Ogre14LegacyPreparedMaterialFrame(
      Ogre14LegacyPreparedMaterialFrame &&) noexcept = default;
  Ogre14LegacyPreparedMaterialFrame &
  operator=(Ogre14LegacyPreparedMaterialFrame &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] const Ogre14LegacyTranslatedFrame *
  translated_frame() const noexcept;
  [[nodiscard]] const std::vector<Ogre14LegacyPreparedMaterial> &
  materials() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14LegacyPreparedMaterialFrame &other) const noexcept;

private:
  struct State;
  explicit Ogre14LegacyPreparedMaterialFrame(
      std::shared_ptr<const State> state) noexcept;

  std::shared_ptr<const State> state_;

  friend class Ogre14LegacyLiveMaterialCoordinator;
};

/// Opaque capability for a prepared frame produced from reviewed live
/// admissions. It retains the exact manifest/catalog/registry authority,
/// admissions, final script/texture snapshots, and inner prepared frame.
class Ogre14LegacyAdmittedPreparedMaterialFrame final {
public:
  Ogre14LegacyAdmittedPreparedMaterialFrame() noexcept = default;
  ~Ogre14LegacyAdmittedPreparedMaterialFrame() = default;
  Ogre14LegacyAdmittedPreparedMaterialFrame(
      const Ogre14LegacyAdmittedPreparedMaterialFrame &) noexcept = default;
  Ogre14LegacyAdmittedPreparedMaterialFrame &operator=(
      const Ogre14LegacyAdmittedPreparedMaterialFrame &) noexcept = default;
  Ogre14LegacyAdmittedPreparedMaterialFrame(
      Ogre14LegacyAdmittedPreparedMaterialFrame &&) noexcept = default;
  Ogre14LegacyAdmittedPreparedMaterialFrame &operator=(
      Ogre14LegacyAdmittedPreparedMaterialFrame &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] const Ogre14LegacyPreparedMaterialFrame *prepared_frame() const
      noexcept;
  [[nodiscard]] std::size_t admission_count() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14LegacyAdmittedPreparedMaterialFrame &) const noexcept;

private:
  struct State;
  explicit Ogre14LegacyAdmittedPreparedMaterialFrame(
      std::shared_ptr<const State>) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14LegacyLiveMaterialCoordinator;
};

enum class Ogre14LegacyLiveMaterialCoordinatorFaultPoint : std::uint8_t {
  AFTER_FIRST_OBSERVATION = 0U,
  AFTER_NATIVE_AUDIT_MATCH = 1U,
  BEFORE_PREPARED_FRAME_PUBLISH = 2U,
  AFTER_ADMITTED_INNER_PREPARE = 3U,
};

class IOgre14LegacyLiveMaterialCoordinatorFaultInjector {
public:
  virtual ~IOgre14LegacyLiveMaterialCoordinatorFaultInjector() = default;
  /// Borrowed test seam. Production passes null. Implementations may throw.
  virtual void AtFaultPoint(Ogre14LegacyLiveMaterialCoordinatorFaultPoint) {}
};

enum class Ogre14LegacyPreparedMaterialCommitResult : std::uint8_t {
  COMMITTED = 0U,
  NO_PENDING_FRAME,
  PREPARED_FRAME_MISMATCH,
  TRANSLATOR_INVARIANT_BROKEN,
};

class Ogre14LegacyLiveMaterialCoordinator final {
public:
  ~Ogre14LegacyLiveMaterialCoordinator();
  Ogre14LegacyLiveMaterialCoordinator(
      const Ogre14LegacyLiveMaterialCoordinator &) = delete;
  Ogre14LegacyLiveMaterialCoordinator &
  operator=(const Ogre14LegacyLiveMaterialCoordinator &) = delete;
  Ogre14LegacyLiveMaterialCoordinator(Ogre14LegacyLiveMaterialCoordinator &&) =
      delete;
  Ogre14LegacyLiveMaterialCoordinator &
  operator=(Ogre14LegacyLiveMaterialCoordinator &&) = delete;

  [[nodiscard]] std::uint64_t source_sequence() const noexcept;
  [[nodiscard]] std::uint64_t catalog_sequence() const noexcept;
  [[nodiscard]] bool has_pending_frame() const noexcept;
  [[nodiscard]] const Ogre14LegacyMaterialSemanticRegistry &
  semantic_registry() const noexcept {
    return semantic_registry_;
  }

  /// Resolves the exact declaration which must be passed to the native
  /// extractor and returned in the corresponding observation.
  [[nodiscard]] ValidationResult ResolveMaterialSemantics(
      const Ogre14LegacyAssetKey &material_key,
      Ogre14LegacyMaterialSemanticResolution &output) const;

  /// Begins one exclusive translator lease, resolves semantics, canonicalizes
  /// shared texture inputs, translates one complete frame, and resolves all
  /// closures in one batch. Every failure leaves durable translator state and
  /// `output` unchanged and releases the lease.
  [[nodiscard]] ValidationResult PrepareFrame(
      std::uint64_t source_sequence,
      const std::vector<Ogre14LegacyMaterialObservation> &observations,
      Ogre14LegacyPreparedMaterialFrame &output,
      IOgre14LegacyLiveMaterialCoordinatorFaultInjector *fault_injector =
          nullptr);

  /// Production authenticated preparation. Each exact live material is
  /// resolved, extractor-captured, admitted, and immediately translated in
  /// this call on the serialized resource/render thread. Previously returned
  /// admission values are intentionally not accepted as preparation input.
  [[nodiscard]] ValidationResult PrepareAdmittedFrame(
      std::uint64_t source_sequence,
      const std::vector<Ogre::Material *> &live_materials,
      Ogre14LegacyAdmittedPreparedMaterialFrame &output
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
      ,
      IOgre14LegacyLiveMaterialCoordinatorFaultInjector *fault_injector =
          nullptr
#endif
      );

#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  /// Synthetic-only seam for transaction/capability tests which have no OGRE
  /// native runtime. It is absent from production headers and binaries.
  [[nodiscard]] ValidationResult PreparePreviouslyAdmittedFrameForTesting(
      std::uint64_t source_sequence,
      const std::vector<Ogre14LegacyMaterialSemanticAdmission> &admissions,
      Ogre14LegacyAdmittedPreparedMaterialFrame &output,
      IOgre14LegacyLiveMaterialCoordinatorFaultInjector *fault_injector =
          nullptr);
#endif

  /// Called only after the joined scene transaction has been accepted. A
  /// valid pending frame has no fallible publication work left. Publication
  /// requires the exact immutable prepared-frame state exposed downstream;
  /// stale, retried, copied-and-replaced, or unrelated frames cannot commit.
  [[nodiscard]] Ogre14LegacyPreparedMaterialCommitResult
  CommitPreparedFrameAfterAcceptedExposure(
      const Ogre14LegacyPreparedMaterialFrame &accepted_frame) noexcept;
  [[nodiscard]] Ogre14LegacyPreparedMaterialCommitResult
  CommitAdmittedPreparedFrameAfterAcceptedExposure(
      const Ogre14LegacyAdmittedPreparedMaterialFrame &accepted_frame)
      noexcept;
  void DiscardPreparedFrame() noexcept;

private:
  struct PendingFrame;
  struct ObservationView;
  enum class PrepareStartStatus : std::uint8_t {
    READY = 0U,
    FAIL_STOPPED,
    PENDING,
    MISSING_STATE,
    COUNT_EXCEEDED,
    SEQUENCE_MISMATCH,
  };

  Ogre14LegacyLiveMaterialCoordinator(
      Ogre14LegacyLiveMaterialCoordinatorConfiguration configuration,
      Ogre14LegacyMaterialSemanticRegistry semantic_registry,
      std::unique_ptr<Ogre14LegacyAssetTranslator> translator,
      const IOgre14AuthenticatedTextureAuthorityProvider
          *texture_authority_provider,
      Ogre14LegacyMaterialSemanticRuntimeAuthority runtime_authority = {},
      ::RoR::ContentManager *content_manager = nullptr
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
      , IOgre14LegacyMaterialRuntimeLiveAuthority *testing_live_authority =
            nullptr
#endif
      )
      noexcept;

  [[nodiscard]] ValidationResult PrepareFrameImpl(
      std::uint64_t,
      const std::vector<Ogre14LegacyMaterialObservation> &,
      Ogre14LegacyPreparedMaterialFrame &,
      IOgre14LegacyLiveMaterialCoordinatorFaultInjector *);
  [[nodiscard]] PrepareStartStatus CheckPrepareStart(
      std::uint64_t, std::size_t) const noexcept;
  [[nodiscard]] static ValidationResult PrepareStartFailure(
      PrepareStartStatus);
  [[nodiscard]] ValidationResult PrepareFrameViewsImpl(
      std::uint64_t, const std::vector<ObservationView> &,
      Ogre14LegacyPreparedMaterialFrame &,
      IOgre14LegacyLiveMaterialCoordinatorFaultInjector *);
  [[nodiscard]] ValidationResult PrepareFreshContentManagerAdmissionsImpl(
      std::uint64_t,
      const std::vector<Ogre14LegacyMaterialSemanticAdmission> &,
      Ogre14LegacyAdmittedPreparedMaterialFrame &,
      IOgre14LegacyLiveMaterialCoordinatorFaultInjector *);
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  [[nodiscard]] ValidationResult PrepareFreshAdmissionsImpl(
      std::uint64_t,
      const std::vector<Ogre14LegacyMaterialSemanticAdmission> &,
      Ogre14LegacyAdmittedPreparedMaterialFrame &,
      IOgre14LegacyLiveMaterialCoordinatorFaultInjector *);
#endif

  Ogre14LegacyLiveMaterialCoordinatorConfiguration configuration_;
  Ogre14LegacyMaterialSemanticRegistry semantic_registry_;
  std::unique_ptr<Ogre14LegacyAssetTranslator> translator_;
  /// Borrowed stable scene-lifetime authority. The provider is queried at the
  /// start of every frame, so an older resolution cannot survive any receipt
  /// registry publication. A null provider admits only untextured captures.
  const IOgre14AuthenticatedTextureAuthorityProvider
      *texture_authority_provider_ = nullptr;
  Ogre14LegacyMaterialSemanticRuntimeAuthority semantic_runtime_authority_;
  ::RoR::ContentManager *content_manager_ = nullptr;
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  IOgre14LegacyMaterialRuntimeLiveAuthority *testing_live_authority_ = nullptr;
#endif
  std::unique_ptr<PendingFrame> pending_;
  bool fail_stopped_ = false;

  friend ValidationResult CreateOgre14LegacyLiveMaterialCoordinator(
      const Ogre14LegacyLiveMaterialCoordinatorConfiguration &,
      const Ogre14LegacyMaterialSemanticRegistry &,
      std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &,
      IOgre14LegacyAssetTranslatorFaultInjector *);
  friend ValidationResult CreateOgre14LegacyLiveMaterialCoordinator(
      const Ogre14LegacyLiveMaterialCoordinatorConfiguration &,
      const Ogre14LegacyMaterialSemanticRegistry &,
      IOgre14AuthenticatedTextureAuthorityProvider &,
      std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &,
      IOgre14LegacyAssetTranslatorFaultInjector *);
  friend ValidationResult CreateOgre14LegacyAuthenticatedMaterialCoordinator(
      const Ogre14LegacyLiveMaterialCoordinatorConfiguration &,
      const Ogre14LegacyMaterialSemanticRuntimeAuthority &,
      ::RoR::ContentManager &,
      std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
      , IOgre14LegacyAssetTranslatorFaultInjector *
#endif
      );
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  friend ValidationResult CreateOgre14LegacyAuthenticatedMaterialCoordinator(
      const Ogre14LegacyLiveMaterialCoordinatorConfiguration &,
      const Ogre14LegacyMaterialSemanticRuntimeAuthority &,
      IOgre14LegacyMaterialRuntimeLiveAuthority &,
      std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &,
      IOgre14LegacyAssetTranslatorFaultInjector *);
#endif
};

// Private state layouts are defined here because the renderer-neutral
// coordinator implementation and the native ContentManager admission bridge
// are separate translation units. They remain private nested types: this
// merely gives both owning member-function implementations a complete type
// without adding a public construction or observation surface.
struct Ogre14LegacyPreparedMaterialFrame::State final {
  std::uint32_t version = kOgre14LegacyPreparedMaterialFrameVersion;
  std::shared_ptr<const Ogre14LegacyTranslatedFrame> translated_frame;
  std::vector<Ogre14LegacyPreparedMaterial> materials;
};

struct Ogre14LegacyAdmittedPreparedMaterialFrame::State final {
  std::uint32_t version = kOgre14LegacyAdmittedPreparedMaterialFrameVersion;
  Ogre14LegacyMaterialSemanticRuntimeAuthority runtime_authority;
  std::vector<Ogre14LegacyMaterialSemanticAdmission> admissions;
  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot script_authority;
  Ogre14AuthenticatedTextureAuthoritySnapshot texture_authority;
  Ogre14LegacyPreparedMaterialFrame prepared;
};

struct Ogre14LegacyLiveMaterialCoordinator::PendingFrame final {
  Ogre14LegacyAssetTranslatorCommittableTransaction transaction;
  Ogre14LegacyPreparedMaterialFrame prepared;
  Ogre14LegacyAdmittedPreparedMaterialFrame admitted;
};

struct Ogre14LegacyLiveMaterialCoordinator::ObservationView final {
  std::uint32_t version = kOgre14LegacyMaterialObservationVersion;
  const Ogre14LegacyAssetKey &material_key;
  const Ogre14LegacyMaterialSemanticResolution &semantic_resolution;
  const Ogre14LegacyNativeMaterialCapture &native_capture;
};

[[nodiscard]] ValidationResult
ValidateOgre14LegacyLiveMaterialCoordinatorConfiguration(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration);

/// Allocates a fresh translator/catalog identity for one scene generation.
/// Failure leaves `output` untouched.
[[nodiscard]] ValidationResult CreateOgre14LegacyLiveMaterialCoordinator(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration,
    const Ogre14LegacyMaterialSemanticRegistry &semantic_registry,
    std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &output,
    IOgre14LegacyAssetTranslatorFaultInjector *translator_fault_injector =
        nullptr);

/// Authenticated production factory. The exact registry is copied from the
/// opaque runtime authority and the inner translator is created here; a fresh
/// equal-value registry cannot be substituted.
[[nodiscard]] ValidationResult
CreateOgre14LegacyAuthenticatedMaterialCoordinator(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration,
    const Ogre14LegacyMaterialSemanticRuntimeAuthority &runtime_authority,
    ::RoR::ContentManager &content_manager,
    std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &output
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
    ,
    IOgre14LegacyAssetTranslatorFaultInjector *translator_fault_injector =
        nullptr
#endif
    );

#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
/// Synthetic-only overload for hostile combined-authority fixtures.
[[nodiscard]] ValidationResult
CreateOgre14LegacyAuthenticatedMaterialCoordinator(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration,
    const Ogre14LegacyMaterialSemanticRuntimeAuthority &runtime_authority,
    IOgre14LegacyMaterialRuntimeLiveAuthority &live_authority,
    std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &output,
    IOgre14LegacyAssetTranslatorFaultInjector *translator_fault_injector =
        nullptr);
#endif

/// Preferred textured-scene construction. `texture_authority_provider` is
/// borrowed for the coordinator lifetime and must be the same scene authority
/// that minted native extractor resolutions. It is queried afresh for every
/// PrepareFrame call. Failure leaves `output` untouched.
[[nodiscard]] ValidationResult CreateOgre14LegacyLiveMaterialCoordinator(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration,
    const Ogre14LegacyMaterialSemanticRegistry &semantic_registry,
    IOgre14AuthenticatedTextureAuthorityProvider &texture_authority_provider,
    std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &output,
    IOgre14LegacyAssetTranslatorFaultInjector *translator_fault_injector =
        nullptr);

/// Exact lookup in the source-ID-ordered prepared set. Returns null for an
/// invalid key, malformed prepared frame, or absent material.
[[nodiscard]] const Ogre14LegacyMaterialClosure *
FindOgre14LegacyPreparedMaterialClosure(
    const Ogre14LegacyPreparedMaterialFrame &frame,
    const Ogre14LegacyAssetKey &material_key) noexcept;

/// Exact lookup exposing both the independently captured native audit owner and
/// the translated closure. Returns null for malformed or absent material state.
[[nodiscard]] const Ogre14LegacyPreparedMaterial *
FindOgre14LegacyPreparedMaterial(
    const Ogre14LegacyPreparedMaterialFrame &frame,
    const Ogre14LegacyAssetKey &material_key) noexcept;

} // namespace RoR::Render
