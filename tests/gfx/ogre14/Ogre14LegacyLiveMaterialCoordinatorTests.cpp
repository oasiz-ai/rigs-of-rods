/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.h"
#include "gfx/render/Ogre14ProceduralRoadSource.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace RoR::Render::Testing {

/// Focused pure-data fixture for the coordinator test only. Production has no
/// synthetic mint API: the exact friend in Ogre14LegacyNativeAssetExtractor.h
/// grants nonempty receipt construction to this named fixture and the pinned
/// OGRE-native capture function alone.
class Ogre14LegacyNativeMaterialAuditTestAccess final {
public:
  static ValidationResult
  SealSyntheticCapture(Ogre14LegacyNativeMaterialCapture &capture) {
    Ogre14LegacyMaterialPipelineAudit value;
    ValidationResult validation =
        DeriveOgre14LegacyMaterialPipelineAudit(capture.material, value);
    if (!validation) {
      return validation;
    }
    auto owner = std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
        std::move(value));
    capture.exact_native_material_audit = owner;
    capture.native_material_audit_receipt =
        Ogre14LegacyNativeMaterialAuditReceipt(std::move(owner));
    return ValidationResult::Success();
  }

  static void AuthenticateExistingOwnerForHostileTesting(
      Ogre14LegacyNativeMaterialCapture &capture,
      std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> owner) {
    capture.exact_native_material_audit = owner;
    capture.native_material_audit_receipt =
        Ogre14LegacyNativeMaterialAuditReceipt(std::move(owner));
  }
};

class Ogre14AuthenticatedTextureResolutionTestAccess final {
public:
  static ValidationResult Mint(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      const std::string &group, std::uint64_t generation,
      std::uintptr_t pointer_token, std::uint64_t handle,
      const std::string &name, std::uint64_t loaded_state_count,
      const IOgre14AuthenticatedTextureResolver &resolver,
      Ogre14AuthenticatedTextureResolution &resolution) {
    return registry.MintLoadedResourceResolution(
        group, generation, pointer_token, handle, name, loaded_state_count,
        reinterpret_cast<std::uintptr_t>(&resolver), resolution);
  }

  static ValidationResult MintAuthority(
      const Ogre14AuthenticatedTextureReceiptRegistry &registry,
      const IOgre14AuthenticatedTextureResolver &resolver,
      Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) {
    return registry.MintResolverAuthoritySnapshot(
        reinterpret_cast<std::uintptr_t>(&resolver), snapshot);
  }
};

} // namespace RoR::Render::Testing

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Ogre14LegacyAssetKey Key(std::string group, std::string name) {
  return {std::move(group), std::move(name)};
}

class SyntheticTextureResolver final
    : public IOgre14AuthenticatedTextureResolver {
public:
  ValidationResult ResolveAuthenticatedTexture(
      Ogre::Texture &,
      Ogre14AuthenticatedTextureResolution &) const override {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "synthetic_texture.resolve",
        "pure coordinator test never resolves an OGRE texture");
  }

  bool RevalidateAuthenticatedTexture(
      Ogre::Texture &,
      const Ogre14AuthenticatedTextureResolution &) const noexcept override {
    return false;
  }
};

struct SyntheticTextureAuthority final {
  Ogre14AuthenticatedTextureReceiptRegistry registry;
  SyntheticTextureResolver resolver;
  struct Binding final {
    Ogre14LegacyAssetKey key;
    std::uint64_t generation = 0U;
    std::uintptr_t pointer_token = 0U;
    std::uint64_t handle = 0U;
  };
  std::map<std::string, Binding, std::less<>> bindings;
  std::map<std::string, std::uint64_t, std::less<>> group_generations;
  std::uint64_t next_generation = 0U;
  std::uintptr_t next_pointer_token = 0x1000U;
  std::uint64_t next_handle = 1U;

  explicit SyntheticTextureAuthority(
      const std::vector<Ogre14LegacyAssetKey> &exact_keys) {
    Require(InitializeOgre14AuthenticatedTextureReceiptRegistry(
                Ogre14AuthenticatedTextureRegistryConfiguration{}, registry)
                .ok(),
            "synthetic texture authority registry did not initialize");
    for (const Ogre14LegacyAssetKey &key : exact_keys) {
      Add(key);
    }
  }

  explicit SyntheticTextureAuthority(Ogre14LegacyAssetKey exact_key)
      : SyntheticTextureAuthority(
            std::vector<Ogre14LegacyAssetKey>{std::move(exact_key)}) {}

  static std::string StableKey(const Ogre14LegacyAssetKey &key) {
    return key.exact_resource_group + std::string(1U, '\0') + key.exact_name;
  }

  void CommitBinding(const Binding &binding) {
    const Ogre14LegacyAssetKey &key = binding.key;
    Ogre14AuthenticatedTextureCaptureInput input;
    input.effective_resource_group = key.exact_resource_group;
    input.group_generation = binding.generation;
    input.archive_identity = "/synthetic/texture-authority.zip";
    input.archive_name = "ror-synthetic-texture-authority";
    input.archive_type = "EmbeddedZip";
    input.archive_sha256 = std::string(64U, 'a');
    input.archive_pointer_token = binding.pointer_token + 0x10000U;
    input.exact_member_name = key.exact_name;
    input.binding.resource_pointer_token = binding.pointer_token;
    input.binding.resource_handle = binding.handle;
    input.binding.resource_state_count = 0U;
    input.binding.exact_resource_name = key.exact_name;
    const std::vector<std::uint8_t> source_bytes = {1U, 2U, 3U, 4U};
    Ogre14AuthenticatedTextureReceipt receipt;
    Require(BuildOgre14AuthenticatedTextureReceipt(
                Ogre14AuthenticatedTextureRegistryConfiguration{}, input,
                source_bytes.data(), source_bytes.size(), receipt)
                    .ok() &&
                CommitOgre14AuthenticatedTextureReceipt(receipt, registry)
                    .ok(),
            "synthetic texture authority receipt did not commit");
  }

  void Add(const Ogre14LegacyAssetKey &key) {
    const std::string stable_key = StableKey(key);
    Require(bindings.find(stable_key) == bindings.end(),
            "synthetic texture authority duplicated one exact key");
    auto generation = group_generations.find(key.exact_resource_group);
    if (generation == group_generations.end()) {
      ++next_generation;
      Require(AdvanceOgre14AuthenticatedTextureGroupGeneration(
                  key.exact_resource_group, next_generation, registry)
                  .ok(),
              "synthetic texture authority group did not activate");
      generation =
          group_generations.emplace(key.exact_resource_group, next_generation)
              .first;
    }
    Binding binding;
    binding.key = key;
    binding.generation = generation->second;
    binding.pointer_token = next_pointer_token++;
    binding.handle = next_handle++;
    CommitBinding(binding);
    bindings.emplace(stable_key, std::move(binding));
  }

  void Remove(const Ogre14LegacyAssetKey &key) {
    const auto binding = bindings.find(StableKey(key));
    Require(binding != bindings.end() &&
                RemoveOgre14AuthenticatedTextureResource(
                    key.exact_resource_group, binding->second.pointer_token,
                    binding->second.handle, key.exact_name, registry)
                    .ok(),
            "synthetic texture authority resource did not remove");
  }

  void Recommit(const Ogre14LegacyAssetKey &key) {
    const auto binding = bindings.find(StableKey(key));
    Require(binding != bindings.end(),
            "synthetic texture authority cannot recommit an absent binding");
    CommitBinding(binding->second);
  }

  void TeardownAndReactivate(const std::string &group) {
    const auto generation = group_generations.find(group);
    Require(generation != group_generations.end() &&
                TeardownOgre14AuthenticatedTextureGroup(
                    group, generation->second, registry)
                    .ok(),
            "synthetic texture authority group did not tear down");
    ++next_generation;
    Require(AdvanceOgre14AuthenticatedTextureGroupGeneration(
                group, next_generation, registry)
                .ok(),
            "synthetic texture authority group did not reactivate");
    generation->second = next_generation;
    for (auto &entry : bindings) {
      if (entry.second.key.exact_resource_group == group) {
        entry.second.generation = next_generation;
        CommitBinding(entry.second);
      }
    }
  }

  Ogre14AuthenticatedTextureResolution
  Mint(const Ogre14LegacyAssetKey &key) const {
    const auto binding = bindings.find(StableKey(key));
    Require(binding != bindings.end(),
            "synthetic texture authority does not own the requested key");
    Ogre14AuthenticatedTextureResolution resolution;
    Require(
        Testing::Ogre14AuthenticatedTextureResolutionTestAccess::Mint(
            registry, key.exact_resource_group, binding->second.generation,
            binding->second.pointer_token, binding->second.handle,
            key.exact_name, 1U, resolver, resolution)
            .ok(),
        "synthetic loaded texture resolution did not mint");
    return resolution;
  }

  ValidationResult CaptureAuthenticatedTextureAuthoritySnapshot(
      Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) const {
    return Testing::Ogre14AuthenticatedTextureResolutionTestAccess::
        MintAuthority(registry, resolver, snapshot);
  }
};

class SyntheticTextureAuthorityProvider final
    : public IOgre14AuthenticatedTextureAuthorityProvider {
public:
  explicit SyntheticTextureAuthorityProvider(SyntheticTextureAuthority &owner)
      : owner_(owner) {}

  ValidationResult CaptureAuthenticatedTextureAuthoritySnapshot(
      Ogre14AuthenticatedTextureAuthoritySnapshot &snapshot) const override {
    return owner_.CaptureAuthenticatedTextureAuthoritySnapshot(snapshot);
  }

private:
  SyntheticTextureAuthority &owner_;
};

class HostileTextureAuthorityProvider final
    : public IOgre14AuthenticatedTextureAuthorityProvider {
public:
  enum class Mode : std::uint8_t { EMPTY_SUCCESS = 0U, BAD_ALLOC, UNEXPECTED };
  Mode mode = Mode::EMPTY_SUCCESS;

  ValidationResult CaptureAuthenticatedTextureAuthoritySnapshot(
      Ogre14AuthenticatedTextureAuthoritySnapshot &) const override {
    if (mode == Mode::BAD_ALLOC) {
      throw std::bad_alloc();
    }
    if (mode == Mode::UNEXPECTED) {
      throw 29;
    }
    return ValidationResult::Success();
  }
};

SyntheticTextureAuthority &TrustedTextureAuthority() {
  static SyntheticTextureAuthority authority(
      {Key("City", "Texture/Shared"), Key("Main", "Shared"),
       Key("Main", "Other")});
  return authority;
}

SyntheticTextureAuthorityProvider &TrustedTextureAuthorityProvider() {
  static SyntheticTextureAuthorityProvider provider(TrustedTextureAuthority());
  return provider;
}

Ogre14AuthenticatedTextureResolution
MakeAuthenticatedTextureResolution(const Ogre14LegacyAssetKey &key) {
  return TrustedTextureAuthority().Mint(key);
}

Ogre14AuthenticatedTextureResolution
MakeForeignAuthenticatedTextureResolution(const Ogre14LegacyAssetKey &key) {
  SyntheticTextureAuthority authority(key);
  return authority.Mint(key);
}

Ogre14LegacyTextureInput
MakeTexture(const Ogre14LegacyAssetKey &key,
            std::vector<std::uint8_t> rgba = {20U, 40U, 60U, 255U},
            std::uint32_t width = 1U, std::uint32_t height = 1U) {
  Ogre14LegacyTextureInput texture;
  texture.key = key;
  // The registry receipt records resource state zero, the successful OGRE load
  // advances it to one, and the portable translator revision is state + 1.
  texture.source_revision = 2U;
  texture.width = width;
  texture.height = height;
  Ogre14LegacyTextureMipInput mip;
  mip.width = width;
  mip.height = height;
  mip.row_pitch_bytes = static_cast<std::uint64_t>(width) * 4U;
  mip.slice_pitch_bytes =
      mip.row_pitch_bytes * static_cast<std::uint64_t>(height);
  mip.bytes = std::move(rgba);
  texture.mip_levels.push_back(std::move(mip));
  return texture;
}

Ogre14LegacyMaterialObservation
MakeRawObservation(const Ogre14LegacyAssetKey &material_key,
                   const Ogre14LegacyAssetKey *texture_key = nullptr) {
  Ogre14LegacyMaterialObservation observation;
  observation.material_key = material_key;
  observation.native_capture.material.key = material_key;
  observation.native_capture.material.source_revision = 1U;
  if (texture_key != nullptr) {
    Ogre14LegacyTextureUnitInput unit;
    unit.texture_key = *texture_key;
    unit.sampler.source_revision = 1U;
    observation.native_capture.material.texture_units.push_back(unit);
    observation.native_capture.textures.push_back(MakeTexture(*texture_key));
    observation.native_capture.authenticated_texture_resolutions.push_back(
        MakeAuthenticatedTextureResolution(*texture_key));
  }
  Require(
      Testing::Ogre14LegacyNativeMaterialAuditTestAccess::SealSyntheticCapture(
          observation.native_capture)
          .ok(),
      "synthetic native audit fixture did not seal");
  return observation;
}

Ogre14LegacyMaterialSemanticRegistry
MakeRegistry(const std::vector<Ogre14LegacyAssetKey> &keys) {
  std::vector<Ogre14LegacyMaterialSemanticDeclaration> declarations;
  declarations.reserve(keys.size());
  for (std::size_t index = 0U; index < keys.size(); ++index) {
    Ogre14LegacyMaterialSemanticDeclaration declaration;
    declaration.material_key = keys[index];
    declaration.source_revision = static_cast<std::uint64_t>(index + 1U);
    declarations.push_back(std::move(declaration));
  }
  Ogre14LegacyMaterialSemanticRegistry registry;
  Require(BuildOgre14LegacyMaterialSemanticRegistry(
              Ogre14LegacyMaterialSemanticRegistryConfiguration{}, declarations,
              registry)
              .ok(),
          "semantic registry fixture did not build");
  return registry;
}

std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> MakeCoordinator(
    const Ogre14LegacyMaterialSemanticRegistry &registry,
    Ogre14LegacyLiveMaterialCoordinatorConfiguration configuration = {}) {
  std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> coordinator;
  Require(CreateOgre14LegacyLiveMaterialCoordinator(
              configuration, registry, TrustedTextureAuthorityProvider(),
              coordinator)
                  .ok() &&
              coordinator != nullptr,
          "live material coordinator fixture did not build");
  return coordinator;
}

Ogre14LegacyMaterialObservation
MakeObservation(const Ogre14LegacyLiveMaterialCoordinator &coordinator,
                const Ogre14LegacyAssetKey &material_key,
                const Ogre14LegacyAssetKey *texture_key = nullptr) {
  Ogre14LegacyMaterialObservation observation =
      MakeRawObservation(material_key, texture_key);
  Require(coordinator
              .ResolveMaterialSemantics(material_key,
                                        observation.semantic_resolution)
              .ok(),
          "semantic resolution fixture was not issued by the coordinator");
  return observation;
}

Ogre14LegacyPreparedMaterialFrame SentinelFrame() {
  const Ogre14LegacyAssetKey material_key = Key("sentinel", "material");
  auto coordinator = MakeCoordinator(MakeRegistry({material_key}));
  Ogre14LegacyPreparedMaterialFrame sentinel;
  const Ogre14LegacyMaterialObservation observation =
      MakeObservation(*coordinator, material_key);
  Require(coordinator->PrepareFrame(1U, {observation}, sentinel).ok(),
          "immutable sentinel fixture did not prepare");
  coordinator->DiscardPreparedFrame();
  return sentinel;
}

void RequireSentinelUnchanged(const Ogre14LegacyPreparedMaterialFrame &sentinel,
                              const Ogre14LegacyPreparedMaterialFrame &expected,
                              const char *message) {
  Require(sentinel.SharesImmutableStateWith(expected) &&
              sentinel.version() == kOgre14LegacyPreparedMaterialFrameVersion &&
              sentinel.translated_frame() != nullptr &&
              sentinel.translated_frame()->source_sequence == 1U &&
              sentinel.materials().size() == 1U &&
              sentinel.materials()[0].material_key ==
                  Key("sentinel", "material") &&
              sentinel.materials()[0].native_material_audit != nullptr &&
              sentinel.materials()[0].closure != nullptr &&
              expected.materials().size() == 1U &&
              sentinel.materials()[0].native_material_audit.get() ==
                  expected.materials()[0].native_material_audit.get() &&
              !sentinel.materials()[0].native_material_audit.owner_before(
                  expected.materials()[0].native_material_audit) &&
              !expected.materials()[0].native_material_audit.owner_before(
                  sentinel.materials()[0].native_material_audit),
          message);
}

template <typename T>
bool SharesExactOwner(const std::shared_ptr<const T> &lhs,
                      const std::shared_ptr<const T> &rhs) noexcept {
  return lhs != nullptr && rhs != nullptr && lhs.get() == rhs.get() &&
         !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
}

template <typename T>
bool SharesControlBlock(const std::shared_ptr<const T> &lhs,
                        const std::shared_ptr<const T> &rhs) noexcept {
  return lhs != nullptr && rhs != nullptr && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

bool ClosureSharesExactFrameOwners(const Ogre14LegacyTranslatedFrame &frame,
                                   const Ogre14LegacyMaterialClosure &closure) {
  for (const GraphicsSceneAssetInput &closure_asset : closure.assets) {
    bool found = false;
    for (const Ogre14LegacyTranslatedAsset &frame_asset : frame.live_assets) {
      if (frame_asset.source_asset_id == closure_asset.source_asset_id) {
        if (!SharesExactOwner(frame_asset.payload, closure_asset.payload)) {
          return false;
        }
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  for (const Ogre14LegacyTranslatedAsset &frame_asset : frame.live_assets) {
    if (frame_asset.source_asset_id == closure.material_source_asset_id) {
      return SharesExactOwner(frame_asset.material_audit,
                              closure.material_audit);
    }
  }
  return false;
}

class ThrowingFault final
    : public IOgre14LegacyLiveMaterialCoordinatorFaultInjector {
public:
  Ogre14LegacyLiveMaterialCoordinatorFaultPoint point =
      Ogre14LegacyLiveMaterialCoordinatorFaultPoint::AFTER_FIRST_OBSERVATION;
  bool bad_allocation = true;

  void
  AtFaultPoint(Ogre14LegacyLiveMaterialCoordinatorFaultPoint current) override {
    if (current != point) {
      return;
    }
    if (bad_allocation) {
      throw std::bad_alloc();
    }
    throw 17;
  }
};

void TestCanonicalPrepareCommitDiscardAndLineage() {
  const Ogre14LegacyAssetKey material_a = Key("City", "Material/A");
  const Ogre14LegacyAssetKey material_b = Key("City", "Material/B");
  const Ogre14LegacyAssetKey texture = Key("City", "Texture/Shared");
  const Ogre14LegacyMaterialSemanticRegistry registry =
      MakeRegistry({material_a, material_b});
  auto coordinator = MakeCoordinator(registry);

  const Ogre14LegacyMaterialObservation observation_a =
      MakeObservation(*coordinator, material_a, &texture);
  const Ogre14LegacyMaterialObservation observation_b =
      MakeObservation(*coordinator, material_b, &texture);
  Ogre14LegacyPreparedMaterialFrame prepared;
  const ValidationResult first_result =
      coordinator->PrepareFrame(1U, {observation_b, observation_a}, prepared);
  const Ogre14LegacyTranslatedFrame *translated = prepared.translated_frame();
  Require(first_result.ok() && coordinator->has_pending_frame() &&
              prepared.initialized() && translated != nullptr &&
              translated->source_sequence == 1U &&
              translated->catalog_sequence == 1U && translated->full_snapshot &&
              translated->live_assets.size() == 5U &&
              prepared.materials().size() == 2U,
          "canonical material frame did not prepare");
  const Ogre14LegacyMaterialClosure *closure_a =
      FindOgre14LegacyPreparedMaterialClosure(prepared, material_a);
  const Ogre14LegacyMaterialClosure *closure_b =
      FindOgre14LegacyPreparedMaterialClosure(prepared, material_b);
  const Ogre14LegacyPreparedMaterial *prepared_a =
      FindOgre14LegacyPreparedMaterial(prepared, material_a);
  const Ogre14LegacyPreparedMaterial *prepared_b =
      FindOgre14LegacyPreparedMaterial(prepared, material_b);
  Require(
      closure_a != nullptr && closure_b != nullptr && prepared_a != nullptr &&
          prepared_b != nullptr && closure_a->assets.size() == 3U &&
          closure_b->assets.size() == 3U &&
          prepared.materials()[0].closure != nullptr &&
          prepared.materials()[1].closure != nullptr &&
          prepared.materials()[0].closure->material_source_asset_id <
              prepared.materials()[1].closure->material_source_asset_id &&
          ((prepared.materials()[0].closure.get() == closure_a &&
            prepared.materials()[1].closure.get() == closure_b) ||
           (prepared.materials()[0].closure.get() == closure_b &&
            prepared.materials()[1].closure.get() == closure_a)) &&
          SharesExactOwner(closure_a->assets.front().payload,
                           closure_b->assets.front().payload) &&
          SharesExactOwner(
              prepared_a->native_material_audit,
              observation_a.native_capture.exact_native_material_audit) &&
          SharesExactOwner(
              prepared_b->native_material_audit,
              observation_b.native_capture.exact_native_material_audit) &&
          EquivalentOgre14LegacyMaterialPipelineAudit(
              *prepared_a->native_material_audit, *closure_a->material_audit) &&
          EquivalentOgre14LegacyMaterialPipelineAudit(
              *prepared_b->native_material_audit, *closure_b->material_audit) &&
          !SharesControlBlock(prepared_a->native_material_audit,
                              closure_a->material_audit) &&
          !SharesControlBlock(prepared_b->native_material_audit,
                              closure_b->material_audit) &&
          ClosureSharesExactFrameOwners(*translated, *closure_a) &&
          ClosureSharesExactFrameOwners(*translated, *closure_b),
      "prepared material lookup did not retain exact closures");
  Ogre14ProceduralRoadCapture road_capture;
  road_capture.exact_native_material_audit = prepared_a->native_material_audit;
  Require(SharesExactOwner(road_capture.exact_native_material_audit,
                           prepared_a->native_material_audit),
          "procedural road could not retain the prepared native audit owner "
          "without copying or reboxing");
  Ogre14LegacyPreparedMaterialFrame uninitialized;
  Require(FindOgre14LegacyPreparedMaterialClosure(uninitialized, material_a) ==
                  nullptr &&
              FindOgre14LegacyPreparedMaterial(uninitialized, material_a) ==
                  nullptr &&
              FindOgre14LegacyPreparedMaterialClosure(
                  prepared, Key("City", "Material/Missing")) == nullptr,
          "prepared material lookup accepted an absent or uninitialized frame");

  Ogre14LegacyPreparedMaterialFrame pending_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame pending_expected = pending_sentinel;
  const ValidationResult already_pending =
      coordinator->PrepareFrame(2U, {observation_a}, pending_sentinel);
  Require(!already_pending &&
              already_pending.field == "material_coordinator.pending",
          "nested live material transaction was accepted");
  RequireSentinelUnchanged(pending_sentinel, pending_expected,
                           "nested rejection mutated caller output");

  Require(coordinator->CommitPreparedFrameAfterAcceptedExposure(prepared) ==
                  Ogre14LegacyPreparedMaterialCommitResult::COMMITTED &&
              !coordinator->has_pending_frame() &&
              coordinator->source_sequence() == 1U &&
              coordinator->catalog_sequence() == 1U,
          "accepted material frame did not commit exactly once");
  Require(coordinator->CommitPreparedFrameAfterAcceptedExposure(prepared) ==
              Ogre14LegacyPreparedMaterialCommitResult::NO_PENDING_FRAME,
          "material frame committed twice");

  Ogre14LegacyPreparedMaterialFrame unchanged;
  Require(
      coordinator->PrepareFrame(2U, {observation_a, observation_b}, unchanged)
              .ok() &&
          unchanged.translated_frame() != nullptr &&
          unchanged.translated_frame()->source_sequence == 2U &&
          unchanged.translated_frame()->catalog_sequence == 1U,
      "unchanged material inventory advanced the catalog");
  coordinator->DiscardPreparedFrame();
  Require(coordinator->source_sequence() == 1U &&
              coordinator->catalog_sequence() == 1U,
          "discard changed committed translator lineage");
  Ogre14LegacyPreparedMaterialFrame retry;
  Require(coordinator->PrepareFrame(2U, {observation_a}, retry).ok(),
          "discarded source sequence was not retryable");
  Require(coordinator->CommitPreparedFrameAfterAcceptedExposure(unchanged) ==
                  Ogre14LegacyPreparedMaterialCommitResult::
                      PREPARED_FRAME_MISMATCH &&
              coordinator->has_pending_frame() &&
              coordinator->source_sequence() == 1U,
          "stale discarded output committed a different retry candidate");
  Require(coordinator->CommitPreparedFrameAfterAcceptedExposure(retry) ==
                  Ogre14LegacyPreparedMaterialCommitResult::COMMITTED &&
              coordinator->source_sequence() == 2U &&
              coordinator->catalog_sequence() == 2U,
          "material removal did not commit one catalog revision");

  Ogre14LegacyPreparedMaterialFrame empty;
  Require(coordinator->PrepareFrame(3U, {}, empty).ok() &&
              empty.materials().empty() && empty.translated_frame() != nullptr,
          "authoritative empty material inventory did not prepare");
  Require(coordinator->CommitPreparedFrameAfterAcceptedExposure(empty) ==
                  Ogre14LegacyPreparedMaterialCommitResult::COMMITTED &&
              coordinator->source_sequence() == 3U,
          "authoritative empty material inventory did not commit");

  auto identical_retry_coordinator = MakeCoordinator(registry);
  const Ogre14LegacyMaterialObservation identical_a =
      MakeObservation(*identical_retry_coordinator, material_a, &texture);
  const Ogre14LegacyMaterialObservation identical_b =
      MakeObservation(*identical_retry_coordinator, material_b, &texture);
  Ogre14LegacyPreparedMaterialFrame discarded_identical;
  Require(
      identical_retry_coordinator
          ->PrepareFrame(1U, {identical_a, identical_b}, discarded_identical)
          .ok(),
      "identical-retry fixture did not prepare its discarded frame");
  identical_retry_coordinator->DiscardPreparedFrame();
  Ogre14LegacyPreparedMaterialFrame fresh_identical;
  Require(identical_retry_coordinator
              ->PrepareFrame(1U, {identical_a, identical_b}, fresh_identical)
              .ok(),
          "identical-retry fixture did not prepare the exact retry");
  Require(identical_retry_coordinator->CommitPreparedFrameAfterAcceptedExposure(
              discarded_identical) == Ogre14LegacyPreparedMaterialCommitResult::
                                          PREPARED_FRAME_MISMATCH &&
              identical_retry_coordinator->has_pending_frame() &&
              identical_retry_coordinator->source_sequence() == 0U,
          "discarded identical frame committed a fresh immutable retry state");
  const Ogre14LegacyPreparedMaterialFrame copied_fresh = fresh_identical;
  Require(
      copied_fresh.SharesImmutableStateWith(fresh_identical) &&
          identical_retry_coordinator->CommitPreparedFrameAfterAcceptedExposure(
              copied_fresh) ==
              Ogre14LegacyPreparedMaterialCommitResult::COMMITTED &&
          identical_retry_coordinator->source_sequence() == 1U,
      "copied handle sharing the accepted immutable state did not commit");
}

void TestHostileInputsAndTransactionalRollback() {
  const Ogre14LegacyAssetKey material_a = Key("Main", "A");
  const Ogre14LegacyAssetKey material_b = Key("Main", "B");
  const Ogre14LegacyAssetKey missing = Key("Main", "Missing");
  const Ogre14LegacyAssetKey texture = Key("Main", "Shared");
  const Ogre14LegacyAssetKey other_texture = Key("Main", "Other");
  auto coordinator = MakeCoordinator(MakeRegistry({material_a, material_b}));
  const Ogre14LegacyMaterialObservation observation_a =
      MakeObservation(*coordinator, material_a, &texture);
  const Ogre14LegacyMaterialObservation observation_b =
      MakeObservation(*coordinator, material_b, &texture);

  auto RejectWithoutMutation =
      [&coordinator](std::uint64_t sequence,
                     const std::vector<Ogre14LegacyMaterialObservation> &inputs,
                     const char *message) {
        Ogre14LegacyPreparedMaterialFrame sentinel = SentinelFrame();
        const Ogre14LegacyPreparedMaterialFrame expected = sentinel;
        const ValidationResult result =
            coordinator->PrepareFrame(sequence, inputs, sentinel);
        Require(!result && !coordinator->has_pending_frame(), message);
        RequireSentinelUnchanged(sentinel, expected, message);
      };
  auto RejectFieldWithoutMutation =
      [&coordinator](
          std::uint64_t sequence,
          const std::vector<Ogre14LegacyMaterialObservation> &inputs,
          const char *expected_field, const char *message) {
        Ogre14LegacyPreparedMaterialFrame sentinel = SentinelFrame();
        const Ogre14LegacyPreparedMaterialFrame expected = sentinel;
        const ValidationResult result =
            coordinator->PrepareFrame(sequence, inputs, sentinel);
        Require(!result && result.field == expected_field &&
                    !coordinator->has_pending_frame(),
                message);
        RequireSentinelUnchanged(sentinel, expected, message);
      };

  RejectWithoutMutation(2U, {observation_a},
                        "skipped source sequence was accepted");
  Ogre14LegacyPreparedMaterialFrame shared_material;
  Require(coordinator
                  ->PrepareFrame(1U, {observation_a, observation_a},
                                 shared_material)
                  .ok() &&
              shared_material.materials().size() == 1U &&
              SharesExactOwner(
                  shared_material.materials()[0].native_material_audit,
                  observation_a.native_capture.exact_native_material_audit),
          "shared material observations did not reuse one canonical native "
          "audit owner");
  coordinator->DiscardPreparedFrame();

  Ogre14LegacyMaterialObservation independently_minted_resolution =
      observation_a;
  independently_minted_resolution
      .native_capture.authenticated_texture_resolutions[0] =
      MakeAuthenticatedTextureResolution(texture);
  Require(
      observation_a.native_capture.authenticated_texture_resolutions[0]
          .SharesLoadedResourceAuthorityWith(
              independently_minted_resolution
                  .native_capture.authenticated_texture_resolutions[0]),
      "separate authentic resolutions did not retain one exact source "
      "authority");
  Ogre14LegacyPreparedMaterialFrame same_authority_frame;
  Require(coordinator
                  ->PrepareFrame(1U,
                                 {observation_a,
                                  independently_minted_resolution},
                                 same_authority_frame)
                  .ok() &&
              same_authority_frame.materials().size() == 1U,
          "separately minted resolutions for one exact loaded resource did "
          "not canonicalize");
  coordinator->DiscardPreparedFrame();

  Ogre14LegacyMaterialObservation missing_resolution = observation_a;
  missing_resolution.native_capture.authenticated_texture_resolutions.clear();
  RejectFieldWithoutMutation(
      1U, {missing_resolution}, "material_observations.textures",
      "textured observation without an authenticated resolution was accepted");

  Ogre14LegacyMaterialObservation empty_resolution = observation_a;
  empty_resolution.native_capture.authenticated_texture_resolutions[0] = {};
  RejectFieldWithoutMutation(
      1U, {empty_resolution}, "material_observations.texture_resolution",
      "textured observation with an empty authenticated resolution was accepted");

  Ogre14LegacyMaterialObservation substituted_resolution = observation_a;
  substituted_resolution.native_capture.authenticated_texture_resolutions[0] =
      MakeAuthenticatedTextureResolution(other_texture);
  RejectFieldWithoutMutation(
      1U, {substituted_resolution},
      "material_observations.texture_resolution",
      "authenticated resolution for another texture identity was accepted");

  Ogre14LegacyMaterialObservation stale_resolution = observation_a;
  stale_resolution.native_capture.textures[0].source_revision += 1U;
  RejectFieldWithoutMutation(
      1U, {stale_resolution}, "material_observations.texture_resolution",
      "authenticated resolution with a stale texture revision was accepted");

  Ogre14LegacyMaterialObservation foreign_resolution = observation_a;
  foreign_resolution.native_capture.authenticated_texture_resolutions[0] =
      MakeForeignAuthenticatedTextureResolution(texture);
  Require(
      !observation_a.native_capture.authenticated_texture_resolutions[0]
           .SharesLoadedResourceAuthorityWith(
               foreign_resolution
                   .native_capture.authenticated_texture_resolutions[0]),
      "foreign texture authority fixture unexpectedly shared exact owners");
  RejectFieldWithoutMutation(
      1U, {foreign_resolution}, "material_observations.texture_authority",
      "lone foreign texture authority was accepted");

  Ogre14LegacyMaterialObservation mixed_authority =
      MakeObservation(*coordinator, material_b, &other_texture);
  mixed_authority.native_capture.authenticated_texture_resolutions[0] =
      MakeForeignAuthenticatedTextureResolution(other_texture);
  RejectFieldWithoutMutation(
      1U, {observation_a, mixed_authority},
      "material_observations.texture_authority",
      "distinct texture keys from different registry snapshots were accepted");

  Ogre14LegacyMaterialObservation reboxed_material = observation_a;
  Require(
      Testing::Ogre14LegacyNativeMaterialAuditTestAccess::SealSyntheticCapture(
          reboxed_material.native_capture)
              .ok() &&
          EquivalentOgre14LegacyMaterialPipelineAudit(
              *observation_a.native_capture.exact_native_material_audit,
              *reboxed_material.native_capture.exact_native_material_audit) &&
          !SharesControlBlock(
              observation_a.native_capture.exact_native_material_audit,
              reboxed_material.native_capture.exact_native_material_audit),
      "same-value different-owner native audit fixture did not rebox");
  RejectWithoutMutation(
      1U, {observation_a, reboxed_material},
      "same material value under different native audit owners was accepted");

  Ogre14LegacyMaterialObservation missing_native_audit = observation_a;
  missing_native_audit.native_capture.exact_native_material_audit.reset();
  RejectWithoutMutation(1U, {missing_native_audit},
                        "missing native material audit owner was accepted");

  Ogre14LegacyMaterialObservation reboxed_owner = observation_a;
  reboxed_owner.native_capture.exact_native_material_audit =
      std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
          *observation_a.native_capture.exact_native_material_audit);
  RejectWithoutMutation(
      1U, {reboxed_owner},
      "same-value reboxed native audit bypassed the opaque capture receipt");

  const Ogre14LegacyPreparedMaterial *shared_prepared =
      FindOgre14LegacyPreparedMaterial(shared_material, material_a);
  Require(shared_prepared != nullptr && shared_prepared->closure != nullptr &&
              shared_prepared->closure->material_audit != nullptr,
          "closure-owner laundering fixture has no translated audit");
  Ogre14LegacyMaterialObservation laundered_closure_owner = observation_a;
  laundered_closure_owner.native_capture.exact_native_material_audit =
      shared_prepared->closure->material_audit;
  RejectWithoutMutation(
      1U, {laundered_closure_owner},
      "translated closure audit owner was laundered as native capture");

  auto closure_owner_coordinator = MakeCoordinator(MakeRegistry({material_a}));
  const Ogre14LegacyMaterialObservation closure_owner_observation =
      MakeObservation(*closure_owner_coordinator, material_a);
  Ogre14LegacyPreparedMaterialFrame closure_owner_first;
  Require(
      closure_owner_coordinator
              ->PrepareFrame(1U, {closure_owner_observation},
                             closure_owner_first)
              .ok() &&
          closure_owner_coordinator->CommitPreparedFrameAfterAcceptedExposure(
              closure_owner_first) ==
              Ogre14LegacyPreparedMaterialCommitResult::COMMITTED,
      "closure-owner control-block fixture did not commit its first frame");
  const Ogre14LegacyPreparedMaterial *committed_material =
      FindOgre14LegacyPreparedMaterial(closure_owner_first, material_a);
  Require(committed_material != nullptr &&
              committed_material->closure != nullptr &&
              committed_material->closure->material_audit != nullptr,
          "closure-owner control-block fixture has no committed audit");
  Ogre14LegacyMaterialObservation hostile_authenticated_closure_owner =
      closure_owner_observation;
  Testing::Ogre14LegacyNativeMaterialAuditTestAccess::
      AuthenticateExistingOwnerForHostileTesting(
          hostile_authenticated_closure_owner.native_capture,
          committed_material->closure->material_audit);
  Ogre14LegacyPreparedMaterialFrame closure_owner_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame closure_owner_expected =
      closure_owner_sentinel;
  const ValidationResult closure_owner_result =
      closure_owner_coordinator->PrepareFrame(
          2U, {hostile_authenticated_closure_owner}, closure_owner_sentinel);
  Require(!closure_owner_result &&
              closure_owner_result.field ==
                  "material_closures.native_material_audit_owner" &&
              closure_owner_coordinator->source_sequence() == 1U &&
              !closure_owner_coordinator->has_pending_frame(),
          "authenticated translated closure owner escaped the post-translation "
          "control-block rejection");
  RequireSentinelUnchanged(
      closure_owner_sentinel, closure_owner_expected,
      "closure-owner control-block rejection mutated caller output");

  Ogre14LegacyMaterialObservation mismatched_cull = observation_a;
  mismatched_cull.native_capture.material.pipeline.cull =
      Ogre14LegacyCullMode::ANTICLOCKWISE;
  RejectWithoutMutation(1U, {mismatched_cull},
                        "native audit with mismatched cull was accepted");

  Ogre14LegacyMaterialObservation mismatched_pipeline = observation_a;
  mismatched_pipeline.native_capture.material.pipeline.alpha_reject =
      Ogre14LegacyCompareOperation::GREATER_EQUAL;
  mismatched_pipeline.native_capture.material.pipeline.alpha_reject_value =
      128U;
  RejectWithoutMutation(1U, {mismatched_pipeline},
                        "native audit with mismatched pipeline was accepted");

  Ogre14LegacyMaterialObservation mismatched_texture_id = observation_a;
  mismatched_texture_id.native_capture.material.texture_units[0].texture_key =
      other_texture;
  mismatched_texture_id.native_capture.textures[0].key = other_texture;
  RejectWithoutMutation(
      1U, {mismatched_texture_id},
      "native audit with mismatched texture identity was accepted");

  Ogre14LegacyMaterialObservation untextured_a =
      MakeObservation(*coordinator, material_a);
  Ogre14LegacyMaterialObservation untextured_b =
      MakeObservation(*coordinator, material_b);
  Ogre14LegacyMaterialObservation untextured_with_resolution = untextured_a;
  untextured_with_resolution.native_capture.authenticated_texture_resolutions
      .push_back(
          observation_a.native_capture.authenticated_texture_resolutions[0]);
  RejectFieldWithoutMutation(
      1U, {untextured_with_resolution}, "material_observations.textures",
      "untextured observation with an authenticated resolution was accepted");
  untextured_b.native_capture.exact_native_material_audit =
      untextured_a.native_capture.exact_native_material_audit;
  untextured_b.native_capture.native_material_audit_receipt =
      untextured_a.native_capture.native_material_audit_receipt;
  Require(
      untextured_b.native_capture.native_material_audit_receipt.Authenticates(
          untextured_b.native_capture.exact_native_material_audit) &&
          EquivalentOgre14LegacyMaterialPipelineAudit(
              *untextured_b.native_capture.exact_native_material_audit,
              *untextured_a.native_capture.exact_native_material_audit),
      "cross-material native audit owner forgery fixture is not exact");
  RejectWithoutMutation(
      1U, {untextured_a, untextured_b},
      "one untextured native audit owner identified two exact materials");

  Ogre14LegacyMaterialObservation unknown = MakeRawObservation(missing);
  RejectWithoutMutation(1U, {unknown},
                        "material without semantic declaration was accepted");

  Ogre14LegacyMaterialObservation semantic_mismatch = observation_a;
  semantic_mismatch.semantic_resolution.source_revision += 1U;
  Ogre14LegacyPreparedMaterialFrame semantic_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame semantic_expected = semantic_sentinel;
  const ValidationResult semantic_result =
      coordinator->PrepareFrame(1U, {semantic_mismatch}, semantic_sentinel);
  Require(!semantic_result &&
              semantic_result.field ==
                  "material_observations.semantic_resolution" &&
              !coordinator->has_pending_frame(),
          "native capture with forged semantics was accepted");
  RequireSentinelUnchanged(semantic_sentinel, semantic_expected,
                           "semantic rejection mutated caller output");

  Ogre14LegacyMaterialObservation empty_receipt = observation_a;
  empty_receipt.semantic_resolution.declaration_identity = {};
  RejectWithoutMutation(
      1U, {empty_receipt},
      "material observation with an empty semantic receipt was accepted");

  auto foreign_coordinator =
      MakeCoordinator(MakeRegistry({material_a, material_b}));
  Ogre14LegacyMaterialObservation foreign_receipt = observation_a;
  Require(foreign_coordinator
              ->ResolveMaterialSemantics(material_a,
                                         foreign_receipt.semantic_resolution)
              .ok(),
          "foreign semantic receipt fixture did not resolve");
  RejectWithoutMutation(
      1U, {foreign_receipt},
      "material observation from a different registry build was accepted");

  Ogre14LegacyMaterialObservation foreign_shared_texture = observation_b;
  foreign_shared_texture.native_capture.authenticated_texture_resolutions[0] =
      MakeForeignAuthenticatedTextureResolution(texture);
  RejectFieldWithoutMutation(
      1U, {observation_a, foreign_shared_texture},
      "material_observations.texture_authority",
      "one shared texture key accepted conflicting authenticated source "
      "authority");

  Ogre14LegacyMaterialObservation conflicting = observation_b;
  conflicting.native_capture.textures[0].mip_levels[0].bytes[0] ^= 0xFFU;
  RejectWithoutMutation(1U, {observation_a, conflicting},
                        "conflicting shared texture capture was accepted");

  Ogre14LegacyMaterialObservation padded = observation_a;
  padded.native_capture.textures[0].mip_levels[0].row_pitch_bytes = 8U;
  padded.native_capture.textures[0].mip_levels[0].slice_pitch_bytes = 8U;
  padded.native_capture.textures[0].mip_levels[0].bytes.resize(8U, 0U);
  RejectWithoutMutation(1U, {padded},
                        "padded native texture payload was copied or accepted");

  Ogre14LegacyLiveMaterialCoordinatorConfiguration observed_byte_limited;
  observed_byte_limited.translator.maximum_decoded_bytes_per_asset = 4U;
  observed_byte_limited.translator.maximum_decoded_bytes_per_frame = 4U;
  auto observed_byte_bounded = MakeCoordinator(
      MakeRegistry({material_a, material_b}), observed_byte_limited);
  const Ogre14LegacyMaterialObservation observed_a =
      MakeObservation(*observed_byte_bounded, material_a, &texture);
  const Ogre14LegacyMaterialObservation observed_b =
      MakeObservation(*observed_byte_bounded, material_b, &texture);
  Ogre14LegacyPreparedMaterialFrame observed_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame observed_expected = observed_sentinel;
  const ValidationResult observed_result = observed_byte_bounded->PrepareFrame(
      1U, {observed_a, observed_b}, observed_sentinel);
  Require(!observed_result &&
              observed_result.field == "material_observations.texture_bytes" &&
              !observed_byte_bounded->has_pending_frame(),
          "repeated observed texture bytes escaped the aggregate source cap");
  RequireSentinelUnchanged(observed_sentinel, observed_expected,
                           "observed-byte cap mutated caller output");

  auto repeated_observation_bounded =
      MakeCoordinator(MakeRegistry({material_a}), observed_byte_limited);
  const Ogre14LegacyMaterialObservation repeated_observation =
      MakeObservation(*repeated_observation_bounded, material_a, &texture);
  Ogre14LegacyPreparedMaterialFrame repeated_observation_sentinel =
      SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame repeated_observation_expected =
      repeated_observation_sentinel;
  const ValidationResult repeated_observation_result =
      repeated_observation_bounded->PrepareFrame(
          1U, {repeated_observation, repeated_observation},
          repeated_observation_sentinel);
  Require(!repeated_observation_result &&
              repeated_observation_result.field ==
                  "material_observations.texture_bytes" &&
              !repeated_observation_bounded->has_pending_frame(),
          "shared native audit owner waived duplicated texture-byte admission");
  RequireSentinelUnchanged(
      repeated_observation_sentinel, repeated_observation_expected,
      "repeated-observation byte cap mutated caller output");

  Ogre14LegacyMaterialObservation wrong_key = observation_a;
  wrong_key.native_capture.material.key = material_b;
  RejectWithoutMutation(
      1U, {wrong_key}, "observation/native material key mismatch was accepted");

  Ogre14LegacyLiveMaterialCoordinatorConfiguration capped;
  capped.maximum_material_observations = 1U;
  auto count_bounded =
      MakeCoordinator(MakeRegistry({material_a, material_b}), capped);
  const Ogre14LegacyMaterialObservation count_a =
      MakeObservation(*count_bounded, material_a, &texture);
  const Ogre14LegacyMaterialObservation count_b =
      MakeObservation(*count_bounded, material_b, &texture);
  Ogre14LegacyPreparedMaterialFrame count_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame count_expected = count_sentinel;
  Require(
      !count_bounded->PrepareFrame(1U, {count_a, count_b}, count_sentinel) &&
          !count_bounded->has_pending_frame(),
      "material observation count cap+1 was accepted");
  RequireSentinelUnchanged(count_sentinel, count_expected,
                           "observation cap mutated caller output");

  Ogre14LegacyLiveMaterialCoordinatorConfiguration texture_count_limited;
  texture_count_limited.maximum_material_observations = 2U;
  texture_count_limited.translator.maximum_texture_inputs_per_frame = 1U;
  texture_count_limited.translator.maximum_material_inputs_per_frame = 2U;
  texture_count_limited.translator.maximum_live_assets_per_frame = 6U;
  texture_count_limited.translator.maximum_lifetime_asset_records = 6U;
  auto texture_count_bounded = MakeCoordinator(
      MakeRegistry({material_a, material_b}), texture_count_limited);
  const Ogre14LegacyMaterialObservation texture_count_a =
      MakeObservation(*texture_count_bounded, material_a, &texture);
  const Ogre14LegacyMaterialObservation texture_count_b =
      MakeObservation(*texture_count_bounded, material_b, &other_texture);
  Ogre14LegacyPreparedMaterialFrame texture_count_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame texture_count_expected =
      texture_count_sentinel;
  const ValidationResult texture_count_result =
      texture_count_bounded->PrepareFrame(
          1U, {texture_count_a, texture_count_b}, texture_count_sentinel);
  Require(!texture_count_result &&
              texture_count_result.field ==
                  "material_observations.texture_count" &&
              !texture_count_bounded->has_pending_frame(),
          "unique native texture count cap+1 was accepted");
  RequireSentinelUnchanged(texture_count_sentinel, texture_count_expected,
                           "unique texture count cap mutated caller output");

  Ogre14LegacyLiveMaterialCoordinatorConfiguration live_limited;
  live_limited.maximum_material_observations = 1U;
  live_limited.translator.maximum_texture_inputs_per_frame = 1U;
  live_limited.translator.maximum_material_inputs_per_frame = 1U;
  live_limited.translator.maximum_live_assets_per_frame = 2U;
  live_limited.translator.maximum_lifetime_asset_records = 3U;
  auto live_bounded = MakeCoordinator(MakeRegistry({material_a}), live_limited);
  const Ogre14LegacyMaterialObservation live_observation =
      MakeObservation(*live_bounded, material_a, &texture);
  Ogre14LegacyPreparedMaterialFrame live_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame live_expected = live_sentinel;
  const ValidationResult live_result =
      live_bounded->PrepareFrame(1U, {live_observation}, live_sentinel);
  Require(!live_result &&
              live_result.field == "material_observations.live_asset_count" &&
              live_bounded->source_sequence() == 0U &&
              !live_bounded->has_pending_frame(),
          "derived live-asset cap+1 was accepted by the coordinator");
  RequireSentinelUnchanged(live_sentinel, live_expected,
                           "live-asset cap mutated caller output");

  Ogre14LegacyLiveMaterialCoordinatorConfiguration byte_limited;
  byte_limited.translator.maximum_decoded_bytes_per_asset = 3U;
  byte_limited.translator.maximum_decoded_bytes_per_frame = 8U;
  auto byte_bounded = MakeCoordinator(MakeRegistry({material_a}), byte_limited);
  const Ogre14LegacyMaterialObservation byte_observation =
      MakeObservation(*byte_bounded, material_a, &texture);
  Ogre14LegacyPreparedMaterialFrame byte_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame byte_expected = byte_sentinel;
  const ValidationResult byte_result =
      byte_bounded->PrepareFrame(1U, {byte_observation}, byte_sentinel);
  Require(!byte_result &&
              byte_result.field == "material_observations.texture_payload" &&
              byte_bounded->source_sequence() == 0U &&
              !byte_bounded->has_pending_frame(),
          "decoded-byte cap+1 was accepted by the coordinator");
  RequireSentinelUnchanged(byte_sentinel, byte_expected,
                           "decoded-byte cap mutated caller output");

  Ogre14LegacyLiveMaterialCoordinatorConfiguration lifetime_limited;
  lifetime_limited.maximum_material_observations = 1U;
  lifetime_limited.translator.maximum_texture_inputs_per_frame = 1U;
  lifetime_limited.translator.maximum_material_inputs_per_frame = 1U;
  lifetime_limited.translator.maximum_live_assets_per_frame = 3U;
  lifetime_limited.translator.maximum_lifetime_asset_records = 3U;
  auto lifetime_bounded =
      MakeCoordinator(MakeRegistry({material_a, material_b}), lifetime_limited);
  const Ogre14LegacyMaterialObservation lifetime_a =
      MakeObservation(*lifetime_bounded, material_a);
  Ogre14LegacyMaterialObservation lifetime_b =
      MakeObservation(*lifetime_bounded, material_b, &other_texture);
  constexpr std::uint32_t kLargeTextureEdge = 1024U;
  lifetime_b.native_capture.textures[0] = MakeTexture(
      other_texture,
      std::vector<std::uint8_t>(static_cast<std::size_t>(kLargeTextureEdge) *
                                    kLargeTextureEdge * 4U,
                                0x7FU),
      kLargeTextureEdge, kLargeTextureEdge);
  Ogre14LegacyPreparedMaterialFrame lifetime_first;
  Require(
      lifetime_bounded->PrepareFrame(1U, {lifetime_a}, lifetime_first).ok() &&
          lifetime_bounded->CommitPreparedFrameAfterAcceptedExposure(
              lifetime_first) ==
              Ogre14LegacyPreparedMaterialCommitResult::COMMITTED,
      "lifetime-cap fixture did not commit its first exact inventory");
  Ogre14LegacyPreparedMaterialFrame lifetime_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame lifetime_expected = lifetime_sentinel;
  const ValidationResult lifetime_result =
      lifetime_bounded->PrepareFrame(2U, {lifetime_b}, lifetime_sentinel);
  Require(!lifetime_result &&
              lifetime_result.field == "frame.lifetime_asset_records" &&
              lifetime_bounded->source_sequence() == 1U &&
              !lifetime_bounded->has_pending_frame(),
          "lifetime asset cap+1 was accepted by the coordinator");
  RequireSentinelUnchanged(lifetime_sentinel, lifetime_expected,
                           "lifetime preflight cap mutated caller output");

  Ogre14LegacyLiveMaterialCoordinatorConfiguration epoch_limited;
  epoch_limited.transaction.maximum_epoch = 1U;
  auto epoch_bounded =
      MakeCoordinator(MakeRegistry({material_a}), epoch_limited);
  const Ogre14LegacyMaterialObservation epoch_observation =
      MakeObservation(*epoch_bounded, material_a, &texture);
  Ogre14LegacyPreparedMaterialFrame epoch_first;
  Require(
      epoch_bounded->PrepareFrame(1U, {epoch_observation}, epoch_first).ok() &&
          epoch_bounded->CommitPreparedFrameAfterAcceptedExposure(
              epoch_first) ==
              Ogre14LegacyPreparedMaterialCommitResult::COMMITTED,
      "epoch-cap fixture did not consume its exact first publication");
  Ogre14LegacyPreparedMaterialFrame epoch_sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame epoch_expected = epoch_sentinel;
  const ValidationResult epoch_result =
      epoch_bounded->PrepareFrame(2U, {epoch_observation}, epoch_sentinel);
  Require(!epoch_result &&
              epoch_result.field == "translator.transaction_epoch" &&
              epoch_bounded->source_sequence() == 1U &&
              !epoch_bounded->has_pending_frame(),
          "exhausted exclusive publication epoch was accepted");
  RequireSentinelUnchanged(epoch_sentinel, epoch_expected,
                           "epoch exhaustion mutated caller output");
}

void TestCurrentTextureAuthorityInvalidation() {
  const Ogre14LegacyAssetKey material = Key("Main", "Material");
  const Ogre14LegacyAssetKey texture = Key("Main", "Shared");
  const Ogre14LegacyMaterialSemanticRegistry registry = MakeRegistry({material});
  SyntheticTextureAuthority authority(texture);
  SyntheticTextureAuthorityProvider provider(authority);
  std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> coordinator;
  Require(CreateOgre14LegacyLiveMaterialCoordinator(
              Ogre14LegacyLiveMaterialCoordinatorConfiguration{}, registry,
              provider, coordinator)
                  .ok() &&
              coordinator != nullptr,
          "current-authority coordinator fixture did not build");

  auto MakeLocalObservation = [&]() {
    Ogre14LegacyMaterialObservation observation =
        MakeObservation(*coordinator, material, &texture);
    observation.native_capture.authenticated_texture_resolutions[0] =
        authority.Mint(texture);
    return observation;
  };
  Ogre14LegacyMaterialObservation observation = MakeLocalObservation();
  Ogre14LegacyPreparedMaterialFrame prepared;
  Require(coordinator->PrepareFrame(1U, {observation}, prepared).ok(),
          "current scene texture authority was rejected");
  coordinator->DiscardPreparedFrame();

  auto RequireStaleRejection =
      [&](const Ogre14LegacyMaterialObservation &stale, const char *message) {
        Ogre14LegacyPreparedMaterialFrame sentinel = SentinelFrame();
        const Ogre14LegacyPreparedMaterialFrame expected = sentinel;
        const ValidationResult result =
            coordinator->PrepareFrame(1U, {stale}, sentinel);
        Require(!result &&
                    result.field == "material_observations.texture_authority" &&
                    !coordinator->has_pending_frame() &&
                    coordinator->source_sequence() == 0U,
                message);
        RequireSentinelUnchanged(
            sentinel, expected,
            "stale scene texture authority mutated caller output");
      };

  authority.Add(Key("Unrelated", "Texture/Publication"));
  RequireStaleRejection(
      observation,
      "old texture proof survived an unrelated registry publication");
  observation = MakeLocalObservation();
  Require(coordinator->PrepareFrame(1U, {observation}, prepared).ok(),
          "refreshed texture proof did not recover after registry publication");
  coordinator->DiscardPreparedFrame();

  authority.Remove(texture);
  RequireStaleRejection(observation,
                        "removed texture resource retained frame authority");
  authority.Recommit(texture);
  observation = MakeLocalObservation();
  Require(coordinator->PrepareFrame(1U, {observation}, prepared).ok(),
          "refreshed texture proof did not recover after exact recommit");
  coordinator->DiscardPreparedFrame();

  authority.TeardownAndReactivate(texture.exact_resource_group);
  RequireStaleRejection(observation,
                        "group teardown retained stale texture authority");
  observation = MakeLocalObservation();
  Require(coordinator->PrepareFrame(1U, {observation}, prepared).ok(),
          "refreshed texture proof did not recover after group reactivation");
  coordinator->DiscardPreparedFrame();

  std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> unanchored;
  Require(CreateOgre14LegacyLiveMaterialCoordinator(
              Ogre14LegacyLiveMaterialCoordinatorConfiguration{}, registry,
              unanchored)
                  .ok(),
          "unanchored compatibility coordinator did not build");
  Ogre14LegacyMaterialObservation unanchored_observation =
      MakeRawObservation(material, &texture);
  Require(unanchored
              ->ResolveMaterialSemantics(
                  material, unanchored_observation.semantic_resolution)
              .ok(),
          "unanchored coordinator did not issue material semantics");
  Ogre14LegacyPreparedMaterialFrame unanchored_output = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame unanchored_expected =
      unanchored_output;
  const ValidationResult unanchored_result = unanchored->PrepareFrame(
      1U, {unanchored_observation}, unanchored_output);
  Require(!unanchored_result &&
              unanchored_result.field ==
                  "material_observations.texture_authority" &&
              !unanchored->has_pending_frame(),
          "textured frame without a trusted scene authority was accepted");
  RequireSentinelUnchanged(
      unanchored_output, unanchored_expected,
      "missing scene texture authority mutated caller output");

  for (const auto hostile_case : {
           std::pair{HostileTextureAuthorityProvider::Mode::EMPTY_SUCCESS,
                     "material_coordinator.texture_authority"},
           std::pair{HostileTextureAuthorityProvider::Mode::BAD_ALLOC,
                     "material_coordinator.allocation"},
           std::pair{HostileTextureAuthorityProvider::Mode::UNEXPECTED,
                     "material_coordinator.exception"}}) {
    HostileTextureAuthorityProvider hostile_provider;
    hostile_provider.mode = hostile_case.first;
    std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> hostile_coordinator;
    Require(CreateOgre14LegacyLiveMaterialCoordinator(
                Ogre14LegacyLiveMaterialCoordinatorConfiguration{}, registry,
                hostile_provider, hostile_coordinator)
                    .ok(),
            "hostile authority-provider coordinator did not build");
    Ogre14LegacyMaterialObservation hostile_observation =
        MakeRawObservation(material);
    Require(hostile_coordinator
                ->ResolveMaterialSemantics(
                    material, hostile_observation.semantic_resolution)
                .ok(),
            "hostile authority-provider semantics did not resolve");
    Ogre14LegacyPreparedMaterialFrame hostile_output = SentinelFrame();
    const Ogre14LegacyPreparedMaterialFrame hostile_expected = hostile_output;
    const ValidationResult hostile_result = hostile_coordinator->PrepareFrame(
        1U, {hostile_observation}, hostile_output);
    Require(!hostile_result && hostile_result.field == hostile_case.second &&
                !hostile_coordinator->has_pending_frame() &&
                hostile_coordinator->source_sequence() == 0U,
            "hostile texture authority provider published a material frame");
    RequireSentinelUnchanged(
        hostile_output, hostile_expected,
        "hostile texture authority provider mutated caller output");
  }
}

void TestExceptionRollbackAndFreshGenerationIdentity() {
  const Ogre14LegacyAssetKey material = Key("Generation", "Material");
  const Ogre14LegacyMaterialSemanticRegistry registry =
      MakeRegistry({material});
  auto coordinator = MakeCoordinator(registry);
  const Ogre14LegacyMaterialObservation observation =
      MakeObservation(*coordinator, material);

  Ogre14LegacyPreparedMaterialFrame sentinel = SentinelFrame();
  const Ogre14LegacyPreparedMaterialFrame expected = sentinel;
  ThrowingFault fault;
  fault.point =
      Ogre14LegacyLiveMaterialCoordinatorFaultPoint::AFTER_NATIVE_AUDIT_MATCH;
  ValidationResult result =
      coordinator->PrepareFrame(1U, {observation}, sentinel, &fault);
  Require(!result && result.field == "material_coordinator.allocation" &&
              coordinator->source_sequence() == 0U &&
              !coordinator->has_pending_frame(),
          "bad_alloc published coordinator state");
  RequireSentinelUnchanged(sentinel, expected,
                           "bad_alloc mutated deep output owners");

  fault.point = Ogre14LegacyLiveMaterialCoordinatorFaultPoint::
      BEFORE_PREPARED_FRAME_PUBLISH;
  fault.bad_allocation = false;
  result = coordinator->PrepareFrame(1U, {observation}, sentinel, &fault);
  Require(!result && result.field == "material_coordinator.exception" &&
              coordinator->source_sequence() == 0U &&
              !coordinator->has_pending_frame(),
          "unexpected exception published coordinator state");
  RequireSentinelUnchanged(sentinel, expected,
                           "unexpected exception mutated deep output owners");

  Ogre14LegacyPreparedMaterialFrame first;
  Require(coordinator->PrepareFrame(1U, {observation}, first).ok(),
          "retry after fault did not prepare");
  coordinator->DiscardPreparedFrame();
  auto fresh_generation = MakeCoordinator(registry);
  Ogre14LegacyPreparedMaterialFrame fresh;
  Require(fresh_generation->PrepareFrame(1U, {observation}, fresh).ok() &&
              !SameOgre14LegacyCatalogIdentity(
                  first.translated_frame()->catalog_identity,
                  fresh.translated_frame()->catalog_identity),
          "fresh scene generation reused an opaque catalog identity");
  fresh_generation->DiscardPreparedFrame();
}

void TestFactoryValidationAndSentinelPreservation() {
  const Ogre14LegacyMaterialSemanticRegistry registry =
      MakeRegistry({Key("Main", "Material")});
  std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> sentinel =
      MakeCoordinator(registry);
  auto *const sentinel_owner = sentinel.get();
  Ogre14LegacyLiveMaterialCoordinatorConfiguration invalid;
  invalid.version += 1U;
  Require(
      !CreateOgre14LegacyLiveMaterialCoordinator(invalid, registry, sentinel) &&
          sentinel.get() == sentinel_owner,
      "invalid coordinator version replaced caller owner");
  invalid = {};
  invalid.maximum_material_observations = 0U;
  Require(
      !CreateOgre14LegacyLiveMaterialCoordinator(invalid, registry, sentinel) &&
          sentinel.get() == sentinel_owner,
      "zero coordinator observation cap replaced caller owner");
  Ogre14LegacyMaterialSemanticRegistry absent;
  Require(!CreateOgre14LegacyLiveMaterialCoordinator(
              Ogre14LegacyLiveMaterialCoordinatorConfiguration{}, absent,
              sentinel) &&
              sentinel.get() == sentinel_owner,
          "absent semantic registry replaced caller coordinator");
}

} // namespace

int main() {
  TestCanonicalPrepareCommitDiscardAndLineage();
  TestHostileInputsAndTransactionalRollback();
  TestCurrentTextureAuthorityInvalidation();
  TestExceptionRollbackAndFreshGenerationIdentity();
  TestFactoryValidationAndSentinelPreservation();
  std::cout << "OGRE 14 live material coordinator tests passed\n";
  return EXIT_SUCCESS;
}
