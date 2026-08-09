/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Atomic legacy-material frame preparation for the live OGRE adapter.

#pragma once

#include "Ogre14LegacyMaterialSemanticRegistry.h"
#include "gfx/render/Ogre14LegacyMaterialClosure.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyLiveMaterialCoordinatorVersion = 1U;
constexpr std::uint32_t kOgre14LegacyMaterialObservationVersion = 1U;
constexpr std::uint32_t kOgre14LegacyPreparedMaterialFrameVersion = 1U;
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
/// identity receipt to authenticate.
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

enum class Ogre14LegacyLiveMaterialCoordinatorFaultPoint : std::uint8_t {
  AFTER_FIRST_OBSERVATION = 0U,
  BEFORE_PREPARED_FRAME_PUBLISH = 1U,
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

  /// Called only after the joined scene transaction has been accepted. A
  /// valid pending frame has no fallible publication work left. Publication
  /// requires the exact immutable prepared-frame state exposed downstream;
  /// stale, retried, copied-and-replaced, or unrelated frames cannot commit.
  [[nodiscard]] Ogre14LegacyPreparedMaterialCommitResult
  CommitPreparedFrameAfterAcceptedExposure(
      const Ogre14LegacyPreparedMaterialFrame &accepted_frame) noexcept;
  void DiscardPreparedFrame() noexcept;

private:
  struct PendingFrame;

  Ogre14LegacyLiveMaterialCoordinator(
      Ogre14LegacyLiveMaterialCoordinatorConfiguration configuration,
      Ogre14LegacyMaterialSemanticRegistry semantic_registry,
      std::unique_ptr<Ogre14LegacyAssetTranslator> translator) noexcept;

  Ogre14LegacyLiveMaterialCoordinatorConfiguration configuration_;
  Ogre14LegacyMaterialSemanticRegistry semantic_registry_;
  std::unique_ptr<Ogre14LegacyAssetTranslator> translator_;
  std::unique_ptr<PendingFrame> pending_;

  friend ValidationResult CreateOgre14LegacyLiveMaterialCoordinator(
      const Ogre14LegacyLiveMaterialCoordinatorConfiguration &,
      const Ogre14LegacyMaterialSemanticRegistry &,
      std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &,
      IOgre14LegacyAssetTranslatorFaultInjector *);
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

/// Exact lookup in the source-ID-ordered prepared set. Returns null for an
/// invalid key, malformed prepared frame, or absent material.
[[nodiscard]] const Ogre14LegacyMaterialClosure *
FindOgre14LegacyPreparedMaterialClosure(
    const Ogre14LegacyPreparedMaterialFrame &frame,
    const Ogre14LegacyAssetKey &material_key) noexcept;

} // namespace RoR::Render
