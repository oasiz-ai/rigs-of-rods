/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14ParticleCaptureSource.h"
#include "RenderAssetRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "OGRE 14 particle capture source test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void TestPoolIdentityRequiresExpirationSurvivalProof() {
  using RoR::Render::CanRetainOgre14ParticlePoolIdentity;
  Require(CanRetainOgre14ParticlePoolIdentity(
              1.0F, 5.0F, 4.0F, 10U, 1.25F, 5.0F, 3.75F, 11U, 0.25F),
          "a surviving monotonic particle must retain its ID");
  Require(CanRetainOgre14ParticlePoolIdentity(
              4.75F, 5.0F, 0.25F, 10U, 5.0F, 5.0F, 0.0F, 11U, 0.25F),
          "OGRE expires on remaining < elapsed, so exact equality survives");
  Require(!CanRetainOgre14ParticlePoolIdentity(
              4.95F, 5.0F, 0.05F, 10U, 0.20F, 5.0F, 4.80F, 11U, 0.10F),
          "a slot that could expire must get a new monotonic ID");
  Require(!CanRetainOgre14ParticlePoolIdentity(
              1.0F, 5.0F, 4.0F, 10U, 1.1F, 5.0F, 4.1F, 11U, 0.1F),
          "remaining lifetime must not increase for a retained particle");
  Require(!CanRetainOgre14ParticlePoolIdentity(
              1.0F, 5.0F, 4.0F, 10U, 1.1F, 5.0F, 3.9F, 12U, 0.1F),
          "multiple native updates must force a new ID");
  Require(!CanRetainOgre14ParticlePoolIdentity(
              1.0F, 5.0F, 4.0F, 10U, 1.1F, 6.0F, 4.9F, 11U, 0.1F),
          "changed authored lifetime must force a new ID");
  Require(!CanRetainOgre14ParticlePoolIdentity(
              1.0F, 5.0F, 4.0F, 10U,
              (std::numeric_limits<float>::quiet_NaN)(), 5.0F, 3.9F, 11U,
              0.1F),
          "non-finite identity evidence must fail closed");
  const float frame_interval = 1.0F / 60.0F;
  const float speed_factor = 0.1F;
  const float native_effective_interval = frame_interval * speed_factor;
  Require(!CanRetainOgre14ParticlePoolIdentity(
              0.99F, 1.0F, std::nextafter(native_effective_interval, 0.0F),
              10U, 0.0F, 1.0F, 1.0F, 11U,
              native_effective_interval),
          "exact native Real multiplication must not be replaced by an understated interval");
}

void TestNativeParticleColourByteDecode() {
  using namespace RoR::Render;
  constexpr std::array<std::uint8_t, 4U> native_rgba = {
      17U, 67U, 149U, 203U};
  const Float4 decoded = DecodeOgre14ParticleColourBytes(native_rgba);
  Require(decoded ==
              Float4{17.0F / 255.0F, 67.0F / 255.0F,
                     149.0F / 255.0F, 203.0F / 255.0F},
          "native RGBA bytes changed channel order or particle alpha");
  Require(decoded.x != decoded.w && decoded.x < decoded.y &&
              decoded.y < decoded.z && decoded.z < decoded.w,
          "asymmetric colour golden cannot detect a red/alpha swap");
}

void TestInactiveFirstAdmissionStateTable() {
  using namespace RoR::Render;
  using Decision = Ogre14ParticleSystemAdmissionDecision;
  Require(ClassifyOgre14ParticleSystemAdmission(false, false, 0U) ==
              Decision::DEFER_INACTIVE_FIRST_OBSERVATION,
          "unseen stopped empty system was not deferred");
  Require(ClassifyOgre14ParticleSystemAdmission(false, true, 0U) ==
              Decision::ADMIT_FIRST_ACTIVITY,
          "unseen emitting empty system was not admitted");
  Require(ClassifyOgre14ParticleSystemAdmission(false, false, 1U) ==
              Decision::ADMIT_FIRST_ACTIVITY,
          "unseen stopped system with a live particle was not admitted");
  Require(ClassifyOgre14ParticleSystemAdmission(true, false, 0U) ==
              Decision::RETAIN_ADMITTED,
          "admitted stopped empty system was not retained");
  Require(ClassifyOgre14ParticleSystemAdmission(true, true, 0U) ==
              Decision::RETAIN_ADMITTED,
          "admitted emitting system changed identity state");
  Require(ClassifyOgre14ParticleSystemAdmission(true, false, 1U) ==
              Decision::RETAIN_ADMITTED,
          "admitted stopped system with a live particle changed identity state");
}

RoR::Render::RenderAssetReference Material(std::uint64_t low = 7U,
                                           std::uint64_t revision = 1U) {
  using namespace RoR::Render;
  return RenderAssetReference::Create(
      RenderAssetKind::MATERIAL, RenderAssetId::FromWords(9U, low), revision);
}

RoR::Render::RenderAssetReference Texture(std::uint64_t low = 7U,
                                          std::uint64_t revision = 1U) {
  using namespace RoR::Render;
  return RenderAssetReference::Create(
      RenderAssetKind::TEXTURE, RenderAssetId::FromWords(10U, low), revision);
}

RoR::Render::RenderAssetReference Sampler(std::uint64_t low = 7U,
                                          std::uint64_t revision = 1U) {
  using namespace RoR::Render;
  return RenderAssetReference::Create(
      RenderAssetKind::SAMPLER, RenderAssetId::FromWords(11U, low), revision);
}

const RoR::Render::RenderAssetRegistry &Catalog() {
  using namespace RoR::Render;
  static const std::unique_ptr<RenderAssetRegistry> catalog = [] {
    auto registry = std::make_unique<RenderAssetRegistry>(51U);
    RenderAssetDelta delta;
    delta.registry_id = 51U;
    delta.sequence = 3U;
    delta.full_snapshot = true;
    for (const std::uint64_t id : {1U, 5U, 10U, 20U}) {
      RenderAssetMutation texture_mutation;
      texture_mutation.type = RenderAssetMutationType::UPSERT;
      texture_mutation.asset = Texture(id);
      TextureResourceDescriptor texture;
      texture.debug_name = "particle texture " + std::to_string(id);
      texture.format = TextureResourceFormat::RGBA8_UNORM;
      texture.color_space = TextureColorSpace::SRGB;
      texture.width = 1U;
      texture.height = 1U;
      TextureMipLevelDescriptor mip;
      mip.width = 1U;
      mip.height = 1U;
      mip.row_pitch_bytes = 4U;
      mip.layer_pitch_bytes = 4U;
      mip.bytes = {255U, 255U, 255U, 128U};
      texture.mip_levels.push_back(std::move(mip));
      texture_mutation.payload = std::move(texture);
      delta.mutations.push_back(std::move(texture_mutation));

      RenderAssetMutation sampler_mutation;
      sampler_mutation.type = RenderAssetMutationType::UPSERT;
      sampler_mutation.asset = Sampler(id);
      SamplerResourceDescriptor sampler;
      sampler.debug_name = "particle sampler " + std::to_string(id);
      sampler_mutation.payload = std::move(sampler);
      delta.mutations.push_back(std::move(sampler_mutation));

      RenderAssetMutation mutation;
      mutation.type = RenderAssetMutationType::UPSERT;
      mutation.asset = Material(id);
      MaterialDescriptor descriptor;
      descriptor.debug_name = "particle material " + std::to_string(id);
      descriptor.base_color_texture.texture = Texture(id);
      descriptor.base_color_texture.sampler = Sampler(id);
      mutation.payload = std::move(descriptor);
      delta.mutations.push_back(std::move(mutation));
    }
    std::sort(delta.mutations.begin(), delta.mutations.end(),
              [](const RenderAssetMutation &left,
                 const RenderAssetMutation &right) {
                return left.asset.id < right.asset.id;
              });
    const ValidationResult result = registry->Apply(delta);
    Require(result.ok(), "test particle material catalog was invalid");
    return registry;
  }();
  return *catalog;
}

RoR::Render::RenderAssetRegistry AdvancedCatalog() {
  using namespace RoR::Render;
  RenderAssetRegistry registry = Catalog();
  RenderAssetDelta advance;
  advance.registry_id = registry.registry_id();
  advance.base_sequence = registry.sequence();
  advance.sequence = registry.sequence() + 1U;
  Require(registry.Apply(advance).ok() && registry.sequence() == 4U,
          "receipt-only particle catalog advance was invalid");
  return registry;
}

RoR::Render::RenderAssetRegistry EmptyFinalCatalog() {
  using namespace RoR::Render;
  RenderAssetRegistry registry = Catalog();
  RenderAssetDelta delta = registry.BuildFullSnapshot();
  delta.sequence = 4U;
  for (RenderAssetMutation &mutation : delta.mutations) {
    mutation.type = RenderAssetMutationType::DESTROY;
    ++mutation.asset.revision;
    mutation.payload = std::monostate{};
  }
  Require(registry.Apply(delta).ok() && registry.live_count() == 0U,
          "empty final particle catalog was invalid");
  return registry;
}

RoR::Render::Ogre14ParticleState Particle(std::uint64_t id) {
  RoR::Render::Ogre14ParticleState particle;
  particle.particle_id = id;
  particle.position = {static_cast<float>(id), 2.0F, 3.0F};
  particle.direction = {0.0F, 1.0F, 0.0F};
  particle.velocity = {0.5F, 1.0F, -0.25F};
  particle.color_linear = {0.5F, 0.6F, 0.7F, 0.8F};
  particle.size_meters = {0.2F, 0.3F};
  particle.rotation_radians = 0.4F;
  particle.age_seconds = 0.25F;
  particle.lifetime_seconds = 2.0F;
  return particle;
}

RoR::Render::Ogre14ParticleSystemCapture System(
    std::uint64_t id,
    std::vector<RoR::Render::Ogre14ParticleState> particles = {Particle(1U)}) {
  using namespace RoR::Render;
  Ogre14ParticleSystemCapture system;
  system.system_id = id;
  system.effect = ParticleEffect::TIRE_SMOKE;
  system.material_closure.material_catalog_registry_id = 51U;
  system.material_closure.material_catalog_sequence = 3U;
  system.material_closure.material = Material(id);
  system.material_closure.source_texture = Texture(id);
  system.material_closure.sampler = Sampler(id);
  system.material_closure.translation_source_sequence = 1U;
  system.material_closure.alpha_reject =
      ContinuousParticleAlphaReject::GREATER;
  system.material_closure.alpha_reject_threshold = 2.0F / 255.0F;
  system.material_closure.source_backed_texture = true;
  system.particles = std::move(particles);
  return system;
}

RoR::Render::Ogre14ParticleLifecycleEvent
Event(std::uint64_t event_id, std::uint64_t system_id,
      RoR::Render::Ogre14ParticleLifecycleOperation operation) {
  return {event_id, system_id, operation};
}

RoR::Render::Ogre14JoinedParticleFrame
Frame(std::uint64_t sequence, std::uint64_t epoch,
      std::vector<RoR::Render::Ogre14ParticleSystemCapture> systems,
      std::vector<RoR::Render::Ogre14ParticleLifecycleEvent> events) {
  RoR::Render::Ogre14JoinedParticleFrame frame;
  frame.source_sequence = sequence;
  frame.material_catalog_registry_id = 51U;
  frame.material_catalog_sequence = 3U;
  frame.simulation_tick = sequence * 2U;
  frame.simulation_time_seconds = static_cast<double>(sequence) / 48.0;
  frame.absolute_world_origin_meters = {1000.0, 20.0, -3000.0};
  frame.joined_buffer_epoch = epoch;
  frame.post_physics_epoch = epoch;
  frame.complete_inventory = true;
  frame.systems = std::move(systems);
  frame.events = std::move(events);
  return frame;
}

void TestCanonicalCreateAndEffectiveVisibility() {
  using namespace RoR::Render;
  Ogre14ParticleSystemCapture first = System(10U);
  first.particles[0U].velocity.z = -0.0F;
  Ogre14ParticleSystemCapture second = System(20U, {Particle(2U)});
  second.parent_visible = false;

  Ogre14JoinedParticleFrame input =
      Frame(1U, 11U, {second, first},
            {Event(2U, 20U, Ogre14ParticleLifecycleOperation::CREATE),
             Event(1U, 10U, Ogre14ParticleLifecycleOperation::CREATE)});
  Ogre14ParticleCaptureSource source;
  Ogre14ParticleCapturedFrame output;
  ValidationResult result = source.Capture(input, Catalog(), output);
  Require(result.ok() && output.version == 1U && output.source_sequence == 1U &&
              output.commands.size() == 2U,
          "valid unordered initial inventory was rejected");
  Require(output.commands[0U].event_id == 1U &&
              output.commands[0U].system_id == 10U &&
              output.commands[1U].event_id == 2U &&
              output.commands[1U].system_id == 20U,
          "events were not emitted in deterministic identity order");
  Require(output.commands[0U].system != nullptr &&
              output.commands[0U].system->effective_visible &&
              output.commands[1U].system != nullptr &&
              !output.commands[1U].system->effective_visible,
          "effective parent visibility was not retained");
  Require(
      output.commands[0U].system->material_closure.material ==
              first.material_closure.material &&
          output.commands[0U]
                  .system->material_closure.translation_source_sequence ==
              first.material_closure.translation_source_sequence &&
          output.commands[0U].system->billboard_rotation_mode ==
              Ogre14ParticleBillboardRotationMode::TEXTURE_COORDINATES &&
          output.commands[0U].system->particles[0U].direction ==
              first.particles[0U].direction &&
          output.commands[0U].system->particles[0U].rotation_radians ==
              first.particles[0U].rotation_radians &&
          !std::signbit(output.commands[0U].system->particles[0U].velocity.z),
      "exact material or continuous particle state was lost");
  Require(source.known_system_count() == 2U &&
              source.live_system_count() == 2U &&
              source.lifetime_particle_count() == 2U &&
              source.highest_event_id() == 2U,
          "initial particle registry lineage is incorrect");
}

void TestReplayUnchangedStopDestroyAndResurrection() {
  using namespace RoR::Render;
  Ogre14ParticleCaptureSource source;
  Ogre14ParticleSystemCapture system = System(1U);
  Ogre14JoinedParticleFrame first =
      Frame(1U, 1U, {system},
            {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)});
  Ogre14ParticleCapturedFrame output;
  ValidationResult result = source.Capture(first, Catalog(), output);
  Require(result.ok(), "initial lifecycle frame failed");

  Ogre14ParticleCapturedFrame replay_sentinel;
  replay_sentinel.source_sequence = 999U;
  result = source.Capture(first, Catalog(), replay_sentinel);
  Require(result.ok() && replay_sentinel.source_sequence == 1U &&
              replay_sentinel.commands.size() == 1U &&
              source.highest_event_id() == 1U,
          "exact replay was not idempotent");

  Ogre14JoinedParticleFrame changed_replay = first;
  changed_replay.systems[0U].particles[0U].age_seconds = 0.5F;
  Ogre14ParticleCapturedFrame untouched;
  untouched.source_sequence = 444U;
  result = source.Capture(changed_replay, Catalog(), untouched);
  Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH &&
              untouched.source_sequence == 444U &&
              source.last_source_sequence() == 1U,
          "changed same-sequence replay mutated output or registry");

  Ogre14JoinedParticleFrame unchanged = Frame(2U, 2U, {system}, {});
  result = source.Capture(unchanged, Catalog(), output);
  Require(result.ok() && output.commands.empty() &&
              source.highest_event_id() == 1U,
          "unchanged new frame was not an empty idempotent delta");

  system.particles[0U].age_seconds = 0.5F;
  system.particles.push_back(Particle(2U));
  Ogre14JoinedParticleFrame updated =
      Frame(3U, 3U, {system},
            {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)});
  result = source.Capture(updated, Catalog(), output);
  Require(result.ok() && output.commands.size() == 1U &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::UPDATE &&
              output.commands[0U].system->particles.size() == 2U &&
              source.lifetime_particle_count() == 2U,
          "continuous particle update did not preserve retained identity");

  system.emitting = false;
  system.particles[0U].age_seconds = 0.75F;
  Ogre14JoinedParticleFrame stopped =
      Frame(4U, 4U, {system},
            {Event(3U, 1U, Ogre14ParticleLifecycleOperation::STOP)});
  result = source.Capture(stopped, Catalog(), output);
  Require(result.ok() &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::STOP &&
              output.commands[0U].system != nullptr &&
              !output.commands[0U].system->emitting,
          "stop did not retain final complete live-particle state");

  Ogre14JoinedParticleFrame destroyed = Frame(
      5U, 5U, {}, {Event(4U, 1U, Ogre14ParticleLifecycleOperation::DESTROY)});
  result = source.Capture(destroyed, Catalog(), output);
  Require(result.ok() &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::DESTROY &&
              output.commands[0U].system == nullptr &&
              source.known_system_count() == 1U &&
              source.live_system_count() == 0U,
          "destroy did not retain a permanent system tombstone");

  Ogre14JoinedParticleFrame resurrected =
      Frame(6U, 6U, {system},
            {Event(5U, 1U, Ogre14ParticleLifecycleOperation::CREATE)});
  untouched.source_sequence = 555U;
  result = source.Capture(resurrected, Catalog(), untouched);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              untouched.source_sequence == 555U &&
              source.last_source_sequence() == 5U &&
              source.live_system_count() == 0U,
          "destroyed system was resurrected or failure was not transactional");
}

void TestSceneGenerationFinalizationDestroysAndResets() {
  using namespace RoR::Render;
  Ogre14ParticleCaptureSource source;
  Ogre14ParticleCapturedFrame output;
  ValidationResult result = source.Capture(
      Frame(1U, 8U, {System(10U), System(20U)},
            {Event(7U, 10U, Ogre14ParticleLifecycleOperation::CREATE),
             Event(8U, 20U, Ogre14ParticleLifecycleOperation::CREATE)}),
      Catalog(), output);
  Require(result.ok() && source.live_system_count() == 2U,
          "generation-finalization seed failed");

  Ogre14ParticleCapturedFrame sentinel;
  sentinel.source_sequence = 999U;
  result = source.FinalizeSceneGeneration(
      Catalog(), 2U, 1.0 / 48.0, {1000.0, 20.0, -3000.0}, sentinel);
  Require(!result && sentinel.source_sequence == 999U &&
              source.last_source_sequence() == 1U &&
              source.live_system_count() == 2U,
          "nonempty final catalog mutated particle generation state");

  RenderAssetRegistry final_catalog = EmptyFinalCatalog();
  result = source.FinalizeSceneGeneration(
      final_catalog, 2U, 1.0 / 48.0, {1000.0, 20.0, -3000.0}, output);
  Require(result.ok() && output.finalizes_scene_generation &&
              output.source_sequence == 2U &&
              output.material_catalog_sequence == 4U &&
              output.joined_buffer_epoch == 8U &&
              output.commands.size() == 2U &&
              output.commands[0U].event_id == 9U &&
              output.commands[0U].system_id == 10U &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::DESTROY &&
              output.commands[0U].system == nullptr &&
              output.commands[1U].event_id == 10U &&
              output.commands[1U].system_id == 20U &&
              output.commands[1U].operation ==
                  Ogre14ParticleLifecycleOperation::DESTROY &&
              source.last_source_sequence() == 0U &&
              source.highest_event_id() == 0U &&
              source.highest_system_id() == 0U &&
              source.known_system_count() == 0U &&
              source.live_system_count() == 0U,
          "final particle tombstones or per-generation reset were incomplete");

  result = source.Capture(
      Frame(1U, 1U, {System(1U)},
            {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
      Catalog(), output);
  Require(result.ok() && source.last_source_sequence() == 1U &&
              source.highest_system_id() == 1U,
          "next scene generation did not reopen particle identity at one");
}

void TestParticleRemovalAndIdentityResurrection() {
  using namespace RoR::Render;
  Ogre14ParticleCaptureSource source;
  Ogre14ParticleSystemCapture system = System(1U, {Particle(1U), Particle(2U)});
  Ogre14ParticleCapturedFrame output;
  ValidationResult result = source.Capture(
      Frame(1U, 1U, {system},
            {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
      Catalog(), output);
  Require(result.ok(), "initial two-particle system failed");

  system.particles.erase(system.particles.begin());
  result = source.Capture(
      Frame(2U, 2U, {system},
            {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
      Catalog(), output);
  Require(result.ok(), "particle removal failed");

  system.particles.insert(system.particles.begin(), Particle(1U));
  Ogre14ParticleCapturedFrame sentinel;
  sentinel.source_sequence = 777U;
  result = source.Capture(
      Frame(3U, 3U, {system},
            {Event(3U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
      Catalog(), sentinel);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              sentinel.source_sequence == 777U &&
              source.last_source_sequence() == 2U,
          "removed particle identity returned or mutated durable state");
}

void TestInitialStoppedCreateAndSameIdentityRestart() {
  using namespace RoR::Render;
  Ogre14ParticleCaptureSource source;
  Ogre14ParticleSystemCapture system = System(1U);
  system.emitting = false;
  Ogre14ParticleCapturedFrame output;
  ValidationResult result = source.Capture(
      Frame(1U, 1U, {system},
            {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
      Catalog(), output);
  Require(result.ok() && output.commands.size() == 1U &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::CREATE &&
              output.commands[0U].system != nullptr &&
              !output.commands[0U].system->emitting,
          "initial non-emitting system was not an explicit stopped CREATE");

  system.particles.push_back(Particle(2U));
  Ogre14ParticleCapturedFrame sentinel;
  sentinel.source_sequence = 777U;
  result = source.Capture(
      Frame(2U, 2U, {system},
            {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
      Catalog(), sentinel);
  Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
              result.field == "particle_system.particles.particle_id" &&
              sentinel.source_sequence == 777U &&
              source.last_source_sequence() == 1U &&
              source.lifetime_particle_count() == 1U,
          "continuously stopped system minted a new particle identity");

  system.particles.pop_back();
  system.particles[0U].age_seconds = 0.5F;
  result = source.Capture(
      Frame(2U, 2U, {system},
            {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
      Catalog(), output);
  Require(result.ok() && output.commands.size() == 1U &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::UPDATE &&
              !output.commands[0U].system->emitting,
          "stopped system could not update an existing particle");

  system.emitting = true;
  system.particles[0U].age_seconds = 0.75F;
  system.particles.push_back(Particle(2U));
  result = source.Capture(
      Frame(3U, 3U, {system},
            {Event(3U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
      Catalog(), output);
  Require(result.ok() && output.commands.size() == 1U &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::UPDATE &&
              output.commands[0U].system != nullptr &&
              output.commands[0U].system->emitting &&
              output.commands[0U].system->particles.size() == 2U &&
              source.known_system_count() == 1U &&
              source.live_system_count() == 1U,
          "stopped system did not restart as UPDATE under the same identity");
}

void TestHostileIdentifiersEventsAndLineage() {
  using namespace RoR::Render;
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleCapturedFrame output;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U), System(1U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
                source.known_system_count() == 0U,
            "duplicate system identity was accepted");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleCapturedFrame output;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U), System(10U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE),
               Event(1U, 10U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
                source.highest_event_id() == 0U,
            "duplicate event identity was accepted");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleCapturedFrame output;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE),
               Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
        Catalog(), output);
    Require(!result && source.known_system_count() == 0U,
            "multiple events for one system were accepted");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleCapturedFrame output;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::REVISION_MISMATCH,
            "incorrect create/update event semantics were accepted");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleCapturedFrame output;
    Ogre14JoinedParticleFrame first =
        Frame(1U, 1U, {System(1U)},
              {Event(5U, 1U, Ogre14ParticleLifecycleOperation::CREATE)});
    ValidationResult result = source.Capture(first, Catalog(), output);
    Require(result.ok(), "event lineage seed failed");
    Ogre14ParticleSystemCapture updated = first.systems[0U];
    updated.particles[0U].age_seconds = 0.5F;
    result = source.Capture(
        Frame(2U, 2U, {updated},
              {Event(4U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::REVISION_MISMATCH &&
                source.highest_event_id() == 5U,
            "old event identity was replayed as a new transition");

    Ogre14JoinedParticleFrame gap = Frame(3U, 3U, {updated}, {});
    result = source.Capture(gap, Catalog(), output);
    Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH,
            "source sequence gap was accepted after a failed frame");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleCapturedFrame output;
    Ogre14ParticleSystemCapture high = System(10U);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {high},
              {Event(1U, 10U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(result.ok(), "system identity monotonicity seed failed");
    result = source.Capture(
        Frame(2U, 2U, {System(5U), high},
              {Event(2U, 5U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::DUPLICATE_IDENTIFIER &&
                source.known_system_count() == 1U &&
                source.last_source_sequence() == 1U,
            "out-of-order new system identity was accepted");
  }
}

void TestMaterialClosureReceiptAndCatalogLineage() {
  using namespace RoR::Render;
  Ogre14ParticleCapturedFrame output;
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.material_closure.material_catalog_registry_id = 52U;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH &&
                source.known_system_count() == 0U,
            "mismatched material-closure registry receipt was accepted");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.material_closure.material_catalog_sequence = 2U;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH,
            "stale material-closure catalog sequence was accepted");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.material_closure.material = Material(99U);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::MISSING_REFERENCE &&
                result.field == "particle_system.material_closure.material",
            "forged closure for a missing material revision was accepted");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.material_closure.translation_source_sequence = 0U;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::INVALID_IDENTIFIER,
            "closure receipt without translator lineage was accepted");
  }
  {
    RenderAssetRegistry wrong_catalog(52U);
    Ogre14ParticleCaptureSource source;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        wrong_catalog, output);
    Require(!result && result.code == ValidationCode::INVALID_IDENTIFIER &&
                source.known_system_count() == 0U,
            "capture accepted a catalog view outside declared lineage");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.material_closure.translation_source_sequence = 2U;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(result.ok(), "material translator lineage seed failed");
    system.material_closure.material = Material(5U);
    system.material_closure.source_texture = Texture(5U);
    system.material_closure.sampler = Sampler(5U);
    system.material_closure.translation_source_sequence = 1U;
    result = source.Capture(
        Frame(2U, 2U, {system},
              {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH &&
                source.last_source_sequence() == 1U,
            "changed material closure regressed translator lineage");
  }
}

void TestReceiptOnlyCatalogAdvanceDoesNotInventUpdate() {
  using namespace RoR::Render;
  Ogre14ParticleCaptureSource source;
  Ogre14ParticleSystemCapture system = System(1U);
  Ogre14ParticleCapturedFrame output;
  ValidationResult result = source.Capture(
      Frame(1U, 1U, {system},
            {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
      Catalog(), output);
  Require(result.ok() && output.commands.size() == 1U,
          "receipt-only lineage seed failed");

  RenderAssetRegistry advanced = AdvancedCatalog();
  system.material_closure.material_catalog_sequence = advanced.sequence();
  system.material_closure.translation_source_sequence = 2U;
  Ogre14JoinedParticleFrame receipt_only = Frame(2U, 2U, {system}, {});
  receipt_only.material_catalog_sequence = advanced.sequence();
  result = source.Capture(receipt_only, advanced, output);
  Require(result.ok() && output.commands.empty() &&
              output.material_catalog_sequence == advanced.sequence() &&
              source.last_source_sequence() == 2U &&
              source.highest_event_id() == 1U,
          "catalog/translator receipt advance invented a particle UPDATE");

  system.material_closure.material = Material(5U);
  system.material_closure.source_texture = Texture(5U);
  system.material_closure.sampler = Sampler(5U);
  Ogre14JoinedParticleFrame stale_translation = Frame(
      3U, 3U, {system},
      {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)});
  stale_translation.material_catalog_sequence = advanced.sequence();
  result = source.Capture(stale_translation, advanced, output);
  Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH &&
              source.last_source_sequence() == 2U,
          "changed exact asset references reused translator lineage");

  system.material_closure.translation_source_sequence = 3U;
  Ogre14JoinedParticleFrame missing_update =
      Frame(3U, 3U, {system}, {});
  missing_update.material_catalog_sequence = advanced.sequence();
  result = source.Capture(missing_update, advanced, output);
  Require(!result && result.code == ValidationCode::SIZE_MISMATCH &&
              source.last_source_sequence() == 2U,
          "changed exact asset references did not require UPDATE");

  Ogre14JoinedParticleFrame exact_update = Frame(
      3U, 3U, {system},
      {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)});
  exact_update.material_catalog_sequence = advanced.sequence();
  result = source.Capture(exact_update, advanced, output);
  Require(result.ok() && output.commands.size() == 1U &&
              output.commands.front().system != nullptr &&
              output.commands.front().system->material_closure.material ==
                  Material(5U),
          "exact asset-reference UPDATE failed after receipt-only advance");
}

void TestHostileNumericFeatureAndCapValidation() {
  using namespace RoR::Render;
  Ogre14ParticleCapturedFrame output;
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.particles[0U].position.x = (std::numeric_limits<float>::quiet_NaN)();
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::NON_FINITE_VALUE &&
                source.known_system_count() == 0U,
            "nonfinite particle payload was accepted");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.billboard_mode = Ogre14ParticleBillboardMode::ORIENTED_SELF;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE,
            "unsupported billboard mode was guessed as camera-facing");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.billboard_rotation_mode =
        Ogre14ParticleBillboardRotationMode::VERTICES;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE,
            "vertex-rotated billboard was guessed as shipped Dust UV rotation");
  }
  {
    Ogre14ParticleCaptureSource source;
    Ogre14ParticleSystemCapture system = System(1U);
    system.requires_frontend_affector_evaluation = true;
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE,
            "unresolved native affector behavior was accepted");
  }
  {
    Ogre14ParticleCaptureConfiguration configuration;
    configuration.maximum_particles_per_system = 1U;
    configuration.maximum_particles_per_frame = 1U;
    configuration.maximum_lifetime_particles = 1U;
    Ogre14ParticleCaptureSource source(configuration);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U, {Particle(1U), Particle(2U)})},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.code == ValidationCode::VALUE_OUT_OF_RANGE,
            "per-system particle cap was not enforced");
  }
  {
    Ogre14ParticleCaptureConfiguration configuration;
    configuration.maximum_live_systems = 1U;
    Ogre14ParticleCaptureSource source(configuration);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U), System(5U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE),
               Event(2U, 5U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.field == "particle_frame.systems" &&
                source.known_system_count() == 0U,
            "live-system cap+1 was not rejected transactionally");
  }
  {
    Ogre14ParticleCaptureConfiguration configuration;
    configuration.maximum_particles_per_system = 2U;
    configuration.maximum_particles_per_frame = 2U;
    Ogre14ParticleCaptureSource source(configuration);
    ValidationResult result = source.Capture(
        Frame(1U, 1U,
              {System(1U, {Particle(1U), Particle(2U)}),
               System(5U, {Particle(1U)})},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE),
               Event(2U, 5U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.field == "particle_frame.particle_count" &&
                source.lifetime_particle_count() == 0U,
            "aggregate frame-particle cap+1 was not rejected transactionally");
  }
  {
    Ogre14ParticleCaptureConfiguration configuration;
    configuration.maximum_events_per_frame = 1U;
    Ogre14ParticleCaptureSource source(configuration);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U), System(5U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE),
               Event(2U, 5U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.field == "particle_frame.events" &&
                source.highest_event_id() == 0U,
            "frame-event cap+1 was not rejected transactionally");
  }
  {
    Ogre14ParticleCaptureConfiguration configuration;
    configuration.maximum_live_systems = 1U;
    configuration.maximum_lifetime_systems = 1U;
    Ogre14ParticleCaptureSource source(configuration);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(result.ok(), "lifetime-system cap seed failed");
    result = source.Capture(
        Frame(2U, 2U, {},
              {Event(2U, 1U, Ogre14ParticleLifecycleOperation::DESTROY)}),
        Catalog(), output);
    Require(result.ok(), "lifetime-system tombstone seed failed");
    result = source.Capture(
        Frame(3U, 3U, {System(5U)},
              {Event(3U, 5U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.field == "particle_capture.lifetime_systems" &&
                source.known_system_count() == 1U &&
                source.live_system_count() == 0U &&
                source.last_source_sequence() == 2U,
            "lifetime-system cap+1 was not rejected transactionally");
  }
  {
    Ogre14ParticleCaptureConfiguration configuration;
    configuration.maximum_particles_per_system = 1U;
    configuration.maximum_particles_per_frame = 1U;
    configuration.maximum_lifetime_particles = 1U;
    Ogre14ParticleCaptureSource source(configuration);
    Ogre14ParticleSystemCapture system = System(1U);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {system},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(result.ok(), "lifetime-particle cap seed failed");
    system.particles.clear();
    result = source.Capture(
        Frame(2U, 2U, {system},
              {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
        Catalog(), output);
    Require(result.ok(), "lifetime-particle tombstone seed failed");
    system.particles.push_back(Particle(2U));
    result = source.Capture(
        Frame(3U, 3U, {system},
              {Event(3U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
        Catalog(), output);
    Require(!result && result.field == "particle_capture.lifetime_particles" &&
                source.lifetime_particle_count() == 1U &&
                source.last_source_sequence() == 2U,
            "lifetime-particle cap+1 was not rejected transactionally");
  }
  {
    Ogre14ParticleCaptureConfiguration configuration;
    configuration.maximum_payload_bytes_per_frame =
        kOgre14ParticleLogicalSystemBytes + kOgre14ParticleLogicalEventBytes +
        kOgre14ParticleLogicalStateBytes - 1U;
    Ogre14ParticleCaptureSource source(configuration);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(!result && result.field == "particle_frame.payload_bytes",
            "logical payload byte cap was not enforced");
  }
  {
    Ogre14ParticleCaptureConfiguration configuration;
    configuration.maximum_events_per_frame = 1U;
    configuration.maximum_lifetime_events = 1U;
    Ogre14ParticleCaptureSource source(configuration);
    ValidationResult result = source.Capture(
        Frame(1U, 1U, {System(1U)},
              {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
        Catalog(), output);
    Require(result.ok(), "lifetime event cap seed failed");
    Ogre14ParticleSystemCapture changed = System(1U);
    changed.particles[0U].age_seconds = 0.5F;
    result = source.Capture(
        Frame(2U, 2U, {changed},
              {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
        Catalog(), output);
    Require(!result && result.field == "particle_capture.lifetime_events" &&
                source.last_source_sequence() == 1U,
            "lifetime event cap failure mutated sequence state");
  }
}

bool SameFloatBits(float lhs, float rhs) {
  std::uint32_t lhs_bits = 0U;
  std::uint32_t rhs_bits = 0U;
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  return lhs_bits == rhs_bits;
}

bool SameDoubleBits(double lhs, double rhs) {
  std::uint64_t lhs_bits = 0U;
  std::uint64_t rhs_bits = 0U;
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  return lhs_bits == rhs_bits;
}

bool SameParticleBits(const RoR::Render::Ogre14ParticleState &lhs,
                      const RoR::Render::Ogre14ParticleState &rhs) {
  const float lhs_values[] = {
      lhs.position.x,       lhs.position.y,     lhs.position.z,
      lhs.direction.x,      lhs.direction.y,    lhs.direction.z,
      lhs.velocity.x,       lhs.velocity.y,     lhs.velocity.z,
      lhs.color_linear.x,   lhs.color_linear.y, lhs.color_linear.z,
      lhs.color_linear.w,   lhs.size_meters.x,  lhs.size_meters.y,
      lhs.rotation_radians, lhs.age_seconds,    lhs.lifetime_seconds,
  };
  const float rhs_values[] = {
      rhs.position.x,       rhs.position.y,     rhs.position.z,
      rhs.direction.x,      rhs.direction.y,    rhs.direction.z,
      rhs.velocity.x,       rhs.velocity.y,     rhs.velocity.z,
      rhs.color_linear.x,   rhs.color_linear.y, rhs.color_linear.z,
      rhs.color_linear.w,   rhs.size_meters.x,  rhs.size_meters.y,
      rhs.rotation_radians, rhs.age_seconds,    rhs.lifetime_seconds,
  };
  if (lhs.particle_id != rhs.particle_id) {
    return false;
  }
  for (std::size_t index = 0U; index < std::size(lhs_values); ++index) {
    if (!SameFloatBits(lhs_values[index], rhs_values[index])) {
      return false;
    }
  }
  return true;
}

template <typename T>
bool SameSharedOwner(const std::shared_ptr<const T> &lhs,
                     const std::shared_ptr<const T> &rhs) {
  return lhs.get() == rhs.get() && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

RoR::Render::Ogre14ParticleCapturedFrame SentinelOutput() {
  using namespace RoR::Render;
  Ogre14ParticleCapturedFrame sentinel;
  sentinel.version = 91U;
  sentinel.source_sequence = 92U;
  sentinel.material_catalog_registry_id = 93U;
  sentinel.material_catalog_sequence = 94U;
  sentinel.simulation_tick = 95U;
  sentinel.simulation_time_seconds = 96.25;
  sentinel.absolute_world_origin_meters = {97.5, -98.75, 99.125};
  sentinel.joined_buffer_epoch = 100U;

  auto system = std::make_shared<Ogre14CapturedParticleSystem>();
  system->system_id = 101U;
  system->effect = ParticleEffect::FIRE;
  system->material_closure.material_catalog_registry_id = 102U;
  system->material_closure.material_catalog_sequence = 103U;
  system->material_closure.material = Material(20U, 7U);
  system->material_closure.translation_source_sequence = 104U;
  system->effective_visible = false;
  system->emitting = false;
  system->particles = {Particle(105U)};
  system->particles[0U].rotation_radians = -0.0F;

  Ogre14CapturedParticleCommand command;
  command.event_id = 106U;
  command.system_id = 101U;
  command.operation = Ogre14ParticleLifecycleOperation::STOP;
  command.system = std::move(system);
  sentinel.commands.push_back(std::move(command));
  return sentinel;
}

bool SameSentinelOutput(const RoR::Render::Ogre14ParticleCapturedFrame &lhs,
                        const RoR::Render::Ogre14ParticleCapturedFrame &rhs) {
  using namespace RoR::Render;
  if (lhs.version != rhs.version ||
      lhs.finalizes_scene_generation != rhs.finalizes_scene_generation ||
      lhs.source_sequence != rhs.source_sequence ||
      lhs.material_catalog_registry_id != rhs.material_catalog_registry_id ||
      lhs.material_catalog_sequence != rhs.material_catalog_sequence ||
      lhs.simulation_tick != rhs.simulation_tick ||
      !SameDoubleBits(lhs.simulation_time_seconds,
                      rhs.simulation_time_seconds) ||
      !SameDoubleBits(lhs.absolute_world_origin_meters.x,
                      rhs.absolute_world_origin_meters.x) ||
      !SameDoubleBits(lhs.absolute_world_origin_meters.y,
                      rhs.absolute_world_origin_meters.y) ||
      !SameDoubleBits(lhs.absolute_world_origin_meters.z,
                      rhs.absolute_world_origin_meters.z) ||
      lhs.joined_buffer_epoch != rhs.joined_buffer_epoch ||
      lhs.commands.size() != 1U || rhs.commands.size() != 1U) {
    return false;
  }
  const Ogre14CapturedParticleCommand &lhs_command = lhs.commands[0U];
  const Ogre14CapturedParticleCommand &rhs_command = rhs.commands[0U];
  if (lhs_command.event_id != rhs_command.event_id ||
      lhs_command.system_id != rhs_command.system_id ||
      lhs_command.operation != rhs_command.operation ||
      !SameSharedOwner(lhs_command.system, rhs_command.system) ||
      lhs_command.system == nullptr || rhs_command.system == nullptr) {
    return false;
  }
  const Ogre14CapturedParticleSystem &lhs_system = *lhs_command.system;
  const Ogre14CapturedParticleSystem &rhs_system = *rhs_command.system;
  return lhs_system.system_id == rhs_system.system_id &&
         lhs_system.effect == rhs_system.effect &&
         lhs_system.material_closure.version ==
             rhs_system.material_closure.version &&
         lhs_system.material_closure.material_catalog_registry_id ==
             rhs_system.material_closure.material_catalog_registry_id &&
         lhs_system.material_closure.material_catalog_sequence ==
             rhs_system.material_closure.material_catalog_sequence &&
         lhs_system.material_closure.material ==
             rhs_system.material_closure.material &&
         lhs_system.material_closure.translation_source_sequence ==
             rhs_system.material_closure.translation_source_sequence &&
         lhs_system.billboard_mode == rhs_system.billboard_mode &&
         lhs_system.billboard_rotation_mode ==
             rhs_system.billboard_rotation_mode &&
         lhs_system.effective_visible == rhs_system.effective_visible &&
         lhs_system.emitting == rhs_system.emitting &&
         lhs_system.particles.size() == 1U &&
         rhs_system.particles.size() == 1U &&
         SameParticleBits(lhs_system.particles[0U], rhs_system.particles[0U]);
}

void TestDeferredEmptyFrameThenAtomicFirstCreate() {
  using namespace RoR::Render;
  Ogre14ParticleCaptureSource source;
  Ogre14ParticleCapturedFrame output;
  ValidationResult result =
      source.Capture(Frame(1U, 1U, {}, {}), Catalog(), output);
  Require(result.ok() && output.source_sequence == 1U &&
              output.commands.empty() && source.last_source_sequence() == 1U &&
              source.highest_system_id() == 0U &&
              source.highest_event_id() == 0U &&
              source.known_system_count() == 0U &&
              source.live_system_count() == 0U,
          "empty deferred inventory minted first-activity lifecycle state");

  Ogre14ParticleSystemCapture active = System(1U, {});
  active.emitting = true;
  Ogre14ParticleSystemCapture missing_source = active;
  missing_source.material_closure.source_texture = Texture(99U);
  output = SentinelOutput();
  const Ogre14ParticleCapturedFrame failure_sentinel = output;
  result = source.Capture(
      Frame(2U, 2U, {missing_source},
            {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
      Catalog(), output);
  Require(!result && result.code == ValidationCode::MISSING_REFERENCE &&
              result.field == "particle_system.material_closure.source_assets" &&
              SameSentinelOutput(output, failure_sentinel) &&
              source.last_source_sequence() == 1U &&
              source.highest_system_id() == 0U &&
              source.highest_event_id() == 0U &&
              source.known_system_count() == 0U &&
              source.live_system_count() == 0U &&
              source.lifetime_particle_count() == 0U &&
              source.lifetime_event_count() == 0U,
          "failed first CREATE consumed identity, sequence, event, or output state");

  result = source.Capture(
      Frame(2U, 2U, {active},
            {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)}),
      Catalog(), output);
  Require(result.ok() && output.source_sequence == 2U &&
              output.commands.size() == 1U &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::CREATE &&
              output.commands[0U].system != nullptr &&
              output.commands[0U].system->system_id == 1U &&
              output.commands[0U].system->particles.empty() &&
              output.commands[0U].system->material_closure.material ==
                  Material(1U) &&
              output.commands[0U].system->material_closure.source_texture ==
                  Texture(1U) &&
              output.commands[0U].system->material_closure.sampler ==
                  Sampler(1U) &&
              output.commands[0U].system->material_closure
                  .source_backed_texture &&
              !output.commands[0U].system->material_closure
                   .gpu_readback_used &&
              source.highest_system_id() == 1U &&
              source.highest_event_id() == 1U &&
              source.known_system_count() == 1U &&
              source.live_system_count() == 1U,
          "retry did not atomically publish exact zero-readback first CREATE");

  active.emitting = false;
  result = source.Capture(
      Frame(3U, 3U, {active},
            {Event(2U, 1U, Ogre14ParticleLifecycleOperation::STOP)}),
      Catalog(), output);
  Require(result.ok() && output.commands.size() == 1U &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::STOP &&
              output.commands[0U].system != nullptr &&
              !output.commands[0U].system->emitting &&
              source.known_system_count() == 1U &&
              source.live_system_count() == 1U,
          "admitted stopped empty system was not retained");

  result = source.Capture(Frame(4U, 4U, {active}, {}), Catalog(), output);
  Require(result.ok() && output.commands.empty() &&
              source.highest_system_id() == 1U &&
              source.highest_event_id() == 2U &&
              source.known_system_count() == 1U &&
              source.live_system_count() == 1U,
          "unchanged stopped empty system was destroyed or recreated");

  active.emitting = true;
  result = source.Capture(
      Frame(5U, 5U, {active},
            {Event(3U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)}),
      Catalog(), output);
  Require(result.ok() && output.commands.size() == 1U &&
              output.commands[0U].operation ==
                  Ogre14ParticleLifecycleOperation::UPDATE &&
              output.commands[0U].system != nullptr &&
              output.commands[0U].system->system_id == 1U &&
              output.commands[0U].system->emitting &&
              source.highest_system_id() == 1U &&
              source.known_system_count() == 1U,
          "stopped empty system did not resume as UPDATE under one identity");
}

class FaultInjector final
    : public RoR::Render::IOgre14ParticleCaptureFaultInjector {
public:
  enum class Behavior {
    DISABLED,
    RETURN_FAILURE,
    THROW_BAD_ALLOC,
    THROW_UNEXPECTED,
  };

  Behavior behavior = Behavior::DISABLED;
  RoR::Render::Ogre14ParticleCaptureFaultPoint target =
      RoR::Render::Ogre14ParticleCaptureFaultPoint::BEFORE_COMMIT;

  bool ShouldFail(RoR::Render::Ogre14ParticleCaptureFaultPoint point) override {
    if (point != target) {
      return false;
    }
    switch (behavior) {
    case Behavior::DISABLED:
      return false;
    case Behavior::RETURN_FAILURE:
      return true;
    case Behavior::THROW_BAD_ALLOC:
      throw std::bad_alloc{};
    case Behavior::THROW_UNEXPECTED:
      throw 417;
    }
    return false;
  }
};

void TestInjectedFailureIsStronglyTransactional() {
  using namespace RoR::Render;
  FaultInjector injector;
  Ogre14ParticleCaptureConfiguration configuration;
  configuration.fault_injector = &injector;
  Ogre14ParticleCaptureSource source(configuration);
  Ogre14JoinedParticleFrame first =
      Frame(1U, 1U, {System(1U)},
            {Event(1U, 1U, Ogre14ParticleLifecycleOperation::CREATE)});
  Ogre14ParticleCapturedFrame output = SentinelOutput();
  const Ogre14ParticleCapturedFrame allocation_sentinel = output;
  injector.target = Ogre14ParticleCaptureFaultPoint::AFTER_CANONICALIZATION;
  injector.behavior = FaultInjector::Behavior::THROW_BAD_ALLOC;
  ValidationResult result = source.Capture(first, Catalog(), output);
  Require(!result && result.field == "particle_capture.allocation" &&
              SameSentinelOutput(output, allocation_sentinel) &&
              source.last_source_sequence() == 0U &&
              source.highest_system_id() == 0U &&
              source.highest_event_id() == 0U &&
              source.known_system_count() == 0U &&
              source.live_system_count() == 0U &&
              source.lifetime_particle_count() == 0U &&
              source.lifetime_event_count() == 0U,
          "injected allocation failure changed complete output or durable "
          "counters");

  injector.behavior = FaultInjector::Behavior::DISABLED;
  result = source.Capture(first, Catalog(), output);
  Require(result.ok() && output.source_sequence == 1U &&
              source.known_system_count() == 1U &&
              source.highest_event_id() == 1U,
          "transaction could not be retried after injected failure");

  Ogre14ParticleSystemCapture changed = System(1U);
  changed.particles[0U].age_seconds = 0.5F;
  changed.particles.push_back(Particle(2U));
  Ogre14JoinedParticleFrame second =
      Frame(2U, 2U, {changed},
            {Event(2U, 1U, Ogre14ParticleLifecycleOperation::UPDATE)});
  output = SentinelOutput();
  const Ogre14ParticleCapturedFrame exception_sentinel = output;
  injector.target = Ogre14ParticleCaptureFaultPoint::BEFORE_COMMIT;
  injector.behavior = FaultInjector::Behavior::THROW_UNEXPECTED;
  result = source.Capture(second, Catalog(), output);
  Require(!result && result.field == "particle_capture.exception" &&
              SameSentinelOutput(output, exception_sentinel) &&
              source.last_source_sequence() == 1U &&
              source.highest_system_id() == 1U &&
              source.highest_event_id() == 1U &&
              source.known_system_count() == 1U &&
              source.live_system_count() == 1U &&
              source.lifetime_particle_count() == 1U &&
              source.lifetime_event_count() == 1U,
          "injected unexpected exception changed complete output or durable "
          "counters");

  injector.behavior = FaultInjector::Behavior::RETURN_FAILURE;
  result = source.Capture(second, Catalog(), output);
  Require(!result && result.field == "particle_capture.injected_failure" &&
              SameSentinelOutput(output, exception_sentinel) &&
              source.last_source_sequence() == 1U &&
              source.lifetime_particle_count() == 1U &&
              source.lifetime_event_count() == 1U,
          "injected validation failure changed output or durable counters");

  injector.behavior = FaultInjector::Behavior::DISABLED;
  result = source.Capture(second, Catalog(), output);
  Require(result.ok() && output.source_sequence == 2U &&
              source.lifetime_particle_count() == 2U &&
              source.lifetime_event_count() == 2U,
          "transaction could not retry after unexpected/injected failures");
}

} // namespace

int main() {
  TestPoolIdentityRequiresExpirationSurvivalProof();
  TestNativeParticleColourByteDecode();
  TestInactiveFirstAdmissionStateTable();
  TestCanonicalCreateAndEffectiveVisibility();
  TestReplayUnchangedStopDestroyAndResurrection();
  TestSceneGenerationFinalizationDestroysAndResets();
  TestParticleRemovalAndIdentityResurrection();
  TestInitialStoppedCreateAndSameIdentityRestart();
  TestHostileIdentifiersEventsAndLineage();
  TestMaterialClosureReceiptAndCatalogLineage();
  TestReceiptOnlyCatalogAdvanceDoesNotInventUpdate();
  TestHostileNumericFeatureAndCapValidation();
  TestDeferredEmptyFrameThenAtomicFirstCreate();
  TestInjectedFailureIsStronglyTransactional();
  std::cout << "OGRE 14 particle capture source tests passed\n";
  return EXIT_SUCCESS;
}
