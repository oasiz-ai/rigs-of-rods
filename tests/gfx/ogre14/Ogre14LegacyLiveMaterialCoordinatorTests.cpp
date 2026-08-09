/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

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

Ogre14LegacyTextureInput
MakeTexture(const Ogre14LegacyAssetKey &key,
            std::vector<std::uint8_t> rgba = {20U, 40U, 60U, 255U},
            std::uint32_t width = 1U, std::uint32_t height = 1U) {
  Ogre14LegacyTextureInput texture;
  texture.key = key;
  texture.source_revision = 1U;
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
  }
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
  Require(CreateOgre14LegacyLiveMaterialCoordinator(configuration, registry,
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
              sentinel.materials()[0].closure != nullptr,
          message);
}

template <typename T>
bool SharesExactOwner(const std::shared_ptr<const T> &lhs,
                      const std::shared_ptr<const T> &rhs) noexcept {
  return lhs != nullptr && rhs != nullptr && lhs.get() == rhs.get() &&
         !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
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
  Require(closure_a != nullptr && closure_b != nullptr &&
              closure_a->assets.size() == 3U &&
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
              ClosureSharesExactFrameOwners(*translated, *closure_a) &&
              ClosureSharesExactFrameOwners(*translated, *closure_b),
          "prepared material lookup did not retain exact closures");
  Ogre14LegacyPreparedMaterialFrame uninitialized;
  Require(FindOgre14LegacyPreparedMaterialClosure(uninitialized, material_a) ==
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

  RejectWithoutMutation(2U, {observation_a},
                        "skipped source sequence was accepted");
  RejectWithoutMutation(1U, {observation_a, observation_a},
                        "duplicate material observation was accepted");

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
  TestExceptionRollbackAndFreshGenerationIdentity();
  TestFactoryValidationAndSentinelPreservation();
  std::cout << "OGRE 14 live material coordinator tests passed\n";
  return EXIT_SUCCESS;
}
