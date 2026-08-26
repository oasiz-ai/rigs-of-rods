/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "ogrenext/OgreNextN1ParticleRuntime.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace {

using namespace RoR::Render;

void Require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "OgreNext N1 particle runtime test failed: " << message
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool NearlyEqual(float lhs, float rhs) {
  return std::fabs(lhs - rhs) <= 2.0e-6F;
}

void TestPinnedTextureCoordinateRotation() {
  const std::array<Float2, 4U> zero =
      BuildOgre14ParticleTextureCoordinateQuad(0.0F);
  Require(zero == std::array<Float2, 4U>{{{0.0F, 0.0F}, {0.0F, 1.0F},
                                          {1.0F, 1.0F}, {1.0F, 0.0F}}},
          "zero Dust rotation changed the full texture rectangle");

  constexpr float half_pi = 1.57079632679489661923F;
  const std::array<Float2, 4U> quarter_turn =
      BuildOgre14ParticleTextureCoordinateQuad(half_pi);
  const std::array<Float2, 4U> quarter_turn_expected = {
      Float2{1.0F, 0.0F}, Float2{0.0F, 0.0F}, Float2{0.0F, 1.0F},
      Float2{1.0F, 1.0F}};
  for (std::size_t index = 0U; index < quarter_turn.size(); ++index) {
    Require(NearlyEqual(quarter_turn[index].x,
                        quarter_turn_expected[index].x) &&
                NearlyEqual(quarter_turn[index].y,
                            quarter_turn_expected[index].y),
            "quarter-turn Dust UVs differ from pinned BBR_TEXCOORD order");
  }

  const std::array<Float2, 4U> asymmetric =
      BuildOgre14ParticleTextureCoordinateQuad(0.37F);
  const std::array<Float2, 4U> asymmetric_golden = {
      Float2{0.214644F, -0.146971F}, Float2{-0.146971F, 0.785356F},
      Float2{0.785356F, 1.146971F}, Float2{1.146971F, 0.214644F}};
  for (std::size_t index = 0U; index < asymmetric.size(); ++index) {
    Require(NearlyEqual(asymmetric[index].x, asymmetric_golden[index].x) &&
                NearlyEqual(asymmetric[index].y,
                            asymmetric_golden[index].y),
            "asymmetric Rotator UV golden changed");
  }
}

RenderAssetReference Ref(RenderAssetKind kind, std::uint64_t low) {
  return RenderAssetReference::Create(
      kind, RenderAssetId::FromWords(0x5041525449434C45ULL, low), 1U);
}

std::unique_ptr<RenderAssetRegistry> Catalog(bool authored_alpha = true,
                                             bool clamp_sampler = true,
                                             bool bc3_storage = false) {
  auto registry = std::make_unique<RenderAssetRegistry>(61U);
  RenderAssetDelta delta;
  delta.registry_id = 61U;
  delta.sequence = 1U;
  delta.full_snapshot = true;

  RenderAssetMutation texture_mutation;
  texture_mutation.asset = Ref(RenderAssetKind::TEXTURE, 1U);
  TextureResourceDescriptor texture;
  texture.debug_name = "source-backed smoke.dds";
  texture.format = bc3_storage ? TextureResourceFormat::BC3_UNORM
                               : TextureResourceFormat::RGBA8_UNORM;
  texture.color_space = TextureColorSpace::SRGB;
  texture.width = bc3_storage ? 4U : 1U;
  texture.height = 1U;
  TextureMipLevelDescriptor mip;
  mip.width = texture.width;
  mip.height = 1U;
  if (bc3_storage) {
    mip.row_pitch_bytes = 16U;
    mip.layer_pitch_bytes = 16U;
    mip.bytes.assign(16U, 0U);
    mip.bytes[0U] = 0U;
    mip.bytes[1U] = 255U;
    std::uint64_t alpha_indices = 0U;
    for (std::uint32_t texel = 0U; texel < 4U; ++texel) {
      const std::uint64_t index =
          authored_alpha && texel == 0U ? UINT64_C(0) : UINT64_C(1);
      alpha_indices |= index << (texel * 3U);
    }
    for (std::uint32_t byte = 0U; byte < 6U; ++byte) {
      mip.bytes[2U + byte] = static_cast<std::uint8_t>(
          (alpha_indices >> (byte * 8U)) & UINT64_C(0xff));
    }
  } else {
    mip.row_pitch_bytes = 4U;
    mip.layer_pitch_bytes = 4U;
    mip.bytes = {220U, 220U, 220U,
                 static_cast<std::uint8_t>(authored_alpha ? 96U : 255U)};
  }
  texture.mip_levels.push_back(std::move(mip));
  texture_mutation.payload = std::move(texture);
  delta.mutations.push_back(std::move(texture_mutation));

  RenderAssetMutation sampler_mutation;
  sampler_mutation.asset = Ref(RenderAssetKind::SAMPLER, 2U);
  SamplerResourceDescriptor sampler;
  sampler.debug_name = "exact clamp smoke sampler";
  sampler.address_u = clamp_sampler ? SamplerAddressMode::CLAMP_TO_EDGE
                                    : SamplerAddressMode::REPEAT;
  sampler.address_v = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.address_w = SamplerAddressMode::CLAMP_TO_EDGE;
  sampler.maximum_lod = 0.0F;
  sampler.anisotropy_enabled = true;
  sampler.maximum_anisotropy = 8.0F;
  sampler_mutation.payload = std::move(sampler);
  delta.mutations.push_back(std::move(sampler_mutation));

  RenderAssetMutation material_mutation;
  material_mutation.asset = Ref(RenderAssetKind::MATERIAL, 3U);
  MaterialDescriptor material;
  material.debug_name = "tracks/SmokeMat closure asset";
  material.blend_mode = MaterialBlendMode::LEGACY_STRAIGHT_ALPHA;
  material.alpha_test_mode = MaterialAlphaTestMode::GREATER;
  material.depth_write = false;
  material.alpha_cutoff = 2.0F / 255.0F;
  material.base_color_texture.texture = Ref(RenderAssetKind::TEXTURE, 1U);
  material.base_color_texture.sampler = Ref(RenderAssetKind::SAMPLER, 2U);
  material_mutation.payload = std::move(material);
  delta.mutations.push_back(std::move(material_mutation));

  Require(registry->Apply(delta).ok(), "test catalog was invalid");
  return registry;
}

Ogre14ParticleMaterialClosureReceipt Closure() {
  Ogre14ParticleMaterialClosureReceipt closure;
  closure.material_catalog_registry_id = 61U;
  closure.material_catalog_sequence = 1U;
  closure.material = Ref(RenderAssetKind::MATERIAL, 3U);
  closure.source_texture = Ref(RenderAssetKind::TEXTURE, 1U);
  closure.sampler = Ref(RenderAssetKind::SAMPLER, 2U);
  closure.translation_source_sequence = 1U;
  closure.blend = ContinuousParticleBlendMode::LEGACY_STRAIGHT_ALPHA;
  closure.alpha_reject = ContinuousParticleAlphaReject::GREATER;
  closure.alpha_reject_threshold = 2.0F / 255.0F;
  closure.sort_policy = ContinuousParticleSortPolicy::STABLE_PARTICLE_ID;
  closure.depth_check = true;
  closure.depth_write = false;
  closure.lighting_enabled = false;
  closure.receives_shadows = false;
  closure.casts_shadows = false;
  closure.vertex_color_modulation = true;
  closure.source_backed_texture = true;
  closure.gpu_readback_used = false;
  return closure;
}

std::shared_ptr<const Ogre14CapturedParticleSystem>
System(std::uint64_t id, bool emitting = true, float age = 0.25F) {
  Ogre14CapturedParticleSystem system;
  system.system_id = id;
  system.effect = ParticleEffect::DUST;
  system.material_closure = Closure();
  system.billboard_mode = Ogre14ParticleBillboardMode::CAMERA_FACING_POINT;
  system.billboard_rotation_mode =
      Ogre14ParticleBillboardRotationMode::TEXTURE_COORDINATES;
  system.effective_visible = true;
  system.emitting = emitting;
  Ogre14ParticleState particle;
  particle.particle_id = 1U;
  particle.position = {static_cast<float>(id), 2.0F, 3.0F};
  particle.direction = {0.0F, 1.0F, 0.0F};
  particle.velocity = {0.0F, 0.5F, 0.0F};
  particle.color_linear = {0.7F, 0.7F, 0.7F, 0.5F};
  particle.size_meters = {0.6F, 0.6F};
  particle.age_seconds = age;
  particle.lifetime_seconds = 2.0F;
  system.particles.push_back(particle);
  return std::make_shared<const Ogre14CapturedParticleSystem>(
      std::move(system));
}

std::shared_ptr<const Ogre14ParticleCapturedFrame> Frame(
    std::uint64_t sequence,
    std::vector<Ogre14CapturedParticleCommand> commands,
    std::uint64_t material_catalog_sequence = 1U,
    bool finalizes_scene_generation = false) {
  auto frame = std::make_shared<Ogre14ParticleCapturedFrame>();
  frame->finalizes_scene_generation = finalizes_scene_generation;
  frame->source_sequence = sequence;
  frame->material_catalog_registry_id = 61U;
  frame->material_catalog_sequence = material_catalog_sequence;
  frame->simulation_tick = 9U;
  frame->simulation_time_seconds = 0.5;
  frame->absolute_world_origin_meters = {100.0, 0.0, -50.0};
  frame->joined_buffer_epoch = 7U;
  frame->commands = std::move(commands);
  return frame;
}

std::unique_ptr<RenderAssetRegistry> EmptyFinalCatalog() {
  std::unique_ptr<RenderAssetRegistry> registry = Catalog();
  RenderAssetDelta final = registry->BuildFullSnapshot();
  final.sequence = 2U;
  for (RenderAssetMutation &mutation : final.mutations) {
    mutation.type = RenderAssetMutationType::DESTROY;
    ++mutation.asset.revision;
    mutation.payload = std::monostate{};
  }
  Require(registry->Apply(final).ok() && registry->live_count() == 0U,
          "empty final runtime catalog was invalid");
  return registry;
}

std::unique_ptr<RenderAssetRegistry> AdvancedCatalog() {
  std::unique_ptr<RenderAssetRegistry> registry = Catalog();
  RenderAssetDelta advance;
  advance.registry_id = registry->registry_id();
  advance.base_sequence = registry->sequence();
  advance.sequence = registry->sequence() + 1U;
  Require(registry->Apply(advance).ok() && registry->sequence() == 2U,
          "runtime receipt-only catalog advance was invalid");
  return registry;
}

void TestLifecycleDistinctTextureAndRollback() {
  const std::unique_ptr<RenderAssetRegistry> catalog = Catalog();
  OgreNextN1ParticleRuntime runtime;
  const Double3 origin{100.0, 0.0, -50.0};

  auto create = Frame(
      1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE, System(10U)},
           {2U, 20U, Ogre14ParticleLifecycleOperation::CREATE, System(20U)}});
  ValidationResult result = runtime.Prepare(1U, create, *catalog, 9U, origin);
  Require(result.ok() && runtime.CanCommit(1U) && runtime.Commit(1U),
          "two-system CREATE did not commit");
  OgreNextN1ParticleRuntimeAudit audit = runtime.audit();
  Require(audit.committed_source_sequence == 1U &&
              audit.create_commands == 2U && audit.live_systems == 2U &&
              audit.live_particles == 2U &&
              audit.lifetime_max_live_systems == 2U &&
              audit.lifetime_max_live_particles == 2U &&
              audit.source_backed_textures == 1U &&
              audit.source_alpha_textures == 1U &&
              audit.lifetime_max_source_backed_textures == 1U &&
              audit.lifetime_max_source_alpha_textures == 1U &&
              audit.gpu_readbacks == 0U,
          "CREATE audit did not count one distinct alpha source texture");

  auto stop_destroy = Frame(
      2U, {{3U, 10U, Ogre14ParticleLifecycleOperation::STOP,
            System(10U, false, 0.5F)},
           {4U, 20U, Ogre14ParticleLifecycleOperation::DESTROY, nullptr}});
  result = runtime.Prepare(2U, stop_destroy, *catalog, 9U, origin);
  Require(result.ok(), "STOP/DESTROY candidate was rejected");
  runtime.Abort(2U);
  Require(runtime.audit().committed_source_sequence == 1U &&
              runtime.audit().live_systems == 2U &&
              !runtime.CanCommit(2U),
          "Abort changed persistent particle state");

  result = runtime.Prepare(3U, stop_destroy, *catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(3U),
          "STOP/DESTROY retry did not commit");
  audit = runtime.audit();
  Require(audit.stop_commands == 1U && audit.destroy_commands == 1U &&
              audit.live_systems == 1U && audit.live_particles == 1U &&
              audit.lifetime_max_live_systems == 2U,
          "STOP/DESTROY state or lifetime maximum was lost");

  auto destroy = Frame(
      3U, {{5U, 10U, Ogre14ParticleLifecycleOperation::DESTROY, nullptr}});
  result = runtime.Prepare(4U, destroy, *catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(4U) &&
              runtime.audit().live_systems == 0U &&
              runtime.audit().source_backed_textures == 0U &&
              runtime.audit().lifetime_max_source_backed_textures == 1U &&
              runtime.audit().lifetime_max_source_alpha_textures == 1U &&
              runtime.audit().lifetime_max_live_particles == 2U,
          "final tombstone did not retain lifetime evidence");
}

void TestDroppedFrameAdvancesOnlyPortableSourceLineage() {
  const std::unique_ptr<RenderAssetRegistry> catalog = Catalog();
  OgreNextN1ParticleRuntime runtime;
  const Double3 origin{100.0, 0.0, -50.0};

  ValidationResult result = runtime.Prepare(
      1U,
      Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                  System(10U)}}),
      *catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(1U),
          "dropped-frame recovery seed failed");

  result = runtime.Prepare(
      2U,
      Frame(2U, {{2U, 10U, Ogre14ParticleLifecycleOperation::UPDATE,
                  System(10U, true, 0.5F)}}),
      *catalog, 9U, origin);
  Require(result.ok() && runtime.AdvanceDroppedFrame(2U) &&
              !runtime.CanCommit(2U),
          "cleanly dropped frame did not consume portable particle state");
  OgreNextN1ParticleRuntimeAudit audit = runtime.audit();
  Require(audit.version == 2U && audit.committed_source_sequence == 2U &&
              audit.dropped_source_frames == 1U &&
              audit.create_commands == 1U && audit.update_commands == 1U &&
              audit.live_systems == 1U &&
              audit.native_batch_creates == 0U &&
              audit.native_batch_destroys == 0U &&
              audit.native_particles_submitted == 0U &&
              audit.native_state_readbacks == 0U &&
              audit.native_state_verifications == 0U,
          "dropped frame claimed native work or lost its source-lineage receipt");

  // The frontend frame identity remains 2 because the failed renderer frame
  // did not present. The source sequence still advances to 3, and must be
  // accepted rather than wedging every subsequent frame on a gap.
  result = runtime.Prepare(
      2U,
      Frame(3U, {{3U, 10U, Ogre14ParticleLifecycleOperation::UPDATE,
                  System(10U, true, 0.75F)}}),
      *catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(2U),
          "frame after a clean drop did not resume contiguous publication");
  audit = runtime.audit();
  Require(audit.committed_source_sequence == 3U &&
              audit.dropped_source_frames == 1U &&
              audit.update_commands == 2U,
          "post-drop recovery changed the dropped count or source lineage");

  result = runtime.Prepare(3U, nullptr, *catalog, 9U, origin);
  Require(result.ok() && !runtime.AdvanceDroppedFrame(3U),
          "a frame with no particle source delta was counted as dropped");
  runtime.Abort(3U);
}

void TestFinalDestroySurvivesPriorAssetRetirement() {
  const Double3 origin{100.0, 0.0, -50.0};
  const std::unique_ptr<RenderAssetRegistry> live_catalog = Catalog();
  OgreNextN1ParticleRuntime runtime;
  auto create = Frame(
      1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE, System(10U)}});
  ValidationResult result =
      runtime.Prepare(1U, create, *live_catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(1U) &&
              runtime.audit().live_systems == 1U,
          "final-retirement runtime seed failed");

  const std::unique_ptr<RenderAssetRegistry> final_catalog =
      EmptyFinalCatalog();
  auto destroy = Frame(
      2U, {{2U, 10U, Ogre14ParticleLifecycleOperation::DESTROY, nullptr}},
      2U);
  result = runtime.Prepare(2U, destroy, *final_catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(2U) &&
              runtime.audit().committed_source_sequence == 2U &&
              runtime.audit().destroy_commands == 1U &&
              runtime.audit().live_systems == 0U &&
              runtime.audit().live_particles == 0U &&
              runtime.audit().source_backed_textures == 0U &&
              runtime.audit().lifetime_max_source_backed_textures == 1U,
          "final DESTROY required already-retired material assets");
}

void TestGenerationCloseReconcilesRejectedFrameGap() {
  const Double3 origin{100.0, 0.0, -50.0};
  const std::unique_ptr<RenderAssetRegistry> live_catalog = Catalog();
  OgreNextN1ParticleRuntime runtime;
  ValidationResult result = runtime.Prepare(
      1U,
      Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                  System(10U)}}),
      *live_catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(1U),
          "generation-close recovery seed failed");

  // Source sequences 2-4 represent cleanly rejected visual frames. In that
  // interval source system 10 disappeared and source system 20 appeared, so
  // the final producer boundary contains only the source-side tombstone for
  // 20. The explicit close must still retire runtime-side system 10.
  const std::unique_ptr<RenderAssetRegistry> final_catalog =
      EmptyFinalCatalog();
  result = runtime.Prepare(
      5U,
      Frame(5U, {{8U, 20U, Ogre14ParticleLifecycleOperation::DESTROY,
                  nullptr}},
            final_catalog->sequence(), true),
      *final_catalog, 9U, origin);
  Require(result.ok() && runtime.prepared_systems().empty() &&
              runtime.Commit(5U) &&
              runtime.audit().committed_source_sequence == 5U &&
              runtime.audit().live_systems == 0U &&
              runtime.audit().live_particles == 0U,
          "authoritative generation close did not reconcile the skipped source interval");

  OgreNextN1ParticleRuntime ordinary_gap;
  result = ordinary_gap.Prepare(
      1U,
      Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                  System(10U)}}),
      *live_catalog, 9U, origin);
  Require(result.ok() && ordinary_gap.Commit(1U),
          "ordinary-gap hostile seed failed");
  result = ordinary_gap.Prepare(2U, Frame(3U, {}), *live_catalog, 9U, origin);
  Require(!result && result.field == "continuous_particles.lineage" &&
              ordinary_gap.audit().committed_source_sequence == 1U,
          "ordinary particle delta bypassed contiguous lineage");

  result = ordinary_gap.Prepare(
      3U,
      Frame(3U, {{2U, 10U, Ogre14ParticleLifecycleOperation::UPDATE,
                  System(10U, true, 0.5F)}},
            final_catalog->sequence(), true),
      *final_catalog, 9U, origin);
  Require(!result &&
              result.field ==
                  "continuous_particles.commands.generation_close" &&
              ordinary_gap.audit().committed_source_sequence == 1U,
          "generation close admitted a non-DESTROY command");
}

void TestRetainedClosureRevalidatesEveryCurrentCatalog() {
  const Double3 origin{100.0, 0.0, -50.0};
  const std::unique_ptr<RenderAssetRegistry> live_catalog = Catalog();
  OgreNextN1ParticleRuntime runtime;
  ValidationResult result = runtime.Prepare(
      1U,
      Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                  System(10U)}}),
      *live_catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(1U),
          "retained-closure revalidation seed failed");

  const std::unique_ptr<RenderAssetRegistry> advanced = AdvancedCatalog();
  result = runtime.Prepare(2U, Frame(2U, {}, advanced->sequence()),
                           *advanced, 9U, origin);
  Require(result.ok() && runtime.prepared_systems().size() == 1U &&
              runtime.prepared_systems().front()
                      ->material_closure.material_catalog_sequence == 1U &&
              runtime.Commit(2U) &&
              runtime.audit().committed_source_sequence == 2U &&
              runtime.audit().live_systems == 1U &&
              runtime.audit().source_backed_textures == 1U &&
              runtime.audit().source_alpha_textures == 1U,
          "unchanged revisioned closure did not survive a newer catalog receipt");

  const std::unique_ptr<RenderAssetRegistry> retired = EmptyFinalCatalog();
  result = runtime.Prepare(3U, Frame(3U, {}, retired->sequence()),
                           *retired, 9U, origin);
  Require(!result && result.code == ValidationCode::MISSING_REFERENCE &&
              result.field == "continuous_particles.material_closure" &&
              runtime.audit().committed_source_sequence == 2U &&
              runtime.audit().live_systems == 1U &&
              runtime.prepared_systems().empty(),
          "retained particle closure skipped current-catalog revalidation");
}

void TestFailClosedSourceAlphaAndStopSemantics() {
  const Double3 origin{100.0, 0.0, -50.0};
  {
    const std::unique_ptr<RenderAssetRegistry> compressed =
        Catalog(true, true, true);
    OgreNextN1ParticleRuntime runtime;
    ValidationResult result = runtime.Prepare(
        1U,
        Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                    System(10U)}}),
        *compressed, 9U, origin);
    Require(result.ok() && runtime.Commit(1U) &&
                runtime.audit().source_alpha_textures == 1U,
            "transparent BC3 smoke was rejected as source alpha");
  }
  {
    const std::unique_ptr<RenderAssetRegistry> compressed_opaque =
        Catalog(false, true, true);
    OgreNextN1ParticleRuntime runtime;
    const ValidationResult result = runtime.Prepare(
        1U,
        Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                    System(10U)}}),
        *compressed_opaque, 9U, origin);
    Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
                runtime.audit().committed_source_sequence == 0U,
            "opaque BC3 smoke was accepted as source alpha");
  }
  {
    const std::unique_ptr<RenderAssetRegistry> opaque = Catalog(false);
    OgreNextN1ParticleRuntime runtime;
    ValidationResult result = runtime.Prepare(
        1U,
        Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                    System(10U)}}),
        *opaque, 9U, origin);
    Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
                runtime.audit().committed_source_sequence == 0U,
            "opaque-normalized smoke was accepted as source alpha");
  }
  {
    const std::unique_ptr<RenderAssetRegistry> wrapping = Catalog(true, false);
    OgreNextN1ParticleRuntime runtime;
    const ValidationResult result = runtime.Prepare(
        1U,
        Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                    System(10U)}}),
        *wrapping, 9U, origin);
    Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
                runtime.prepared_systems().empty(),
            "wrapping smoke sampler was accepted or leaked prepared state");
  }
  {
    const std::unique_ptr<RenderAssetRegistry> catalog = Catalog();
    OgreNextN1ParticleRuntime runtime;
    ValidationResult result = runtime.Prepare(
        1U,
        Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                    System(10U)}}),
        *catalog, 9U, origin);
    Require(result.ok() && runtime.Commit(1U), "STOP hostile seed failed");
    result = runtime.Prepare(
        2U,
        Frame(2U, {{2U, 10U, Ogre14ParticleLifecycleOperation::STOP,
                    System(10U, true, 0.5F)}}),
        *catalog, 9U, origin);
    Require(!result && result.field == "continuous_particles.commands.stop" &&
                runtime.audit().committed_source_sequence == 1U,
            "STOP with active emission mutated persistent state");
  }
  {
    const std::unique_ptr<RenderAssetRegistry> catalog = Catalog();
    OgreNextN1ParticleRuntime runtime;
    Ogre14CapturedParticleSystem hostile = *System(10U);
    hostile.particles.front().position.x =
        (std::numeric_limits<float>::quiet_NaN)();
    auto hostile_owner =
        std::make_shared<const Ogre14CapturedParticleSystem>(
            std::move(hostile));
    const ValidationResult result = runtime.Prepare(
        1U,
        Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                    hostile_owner}}),
        *catalog, 9U, origin);
    Require(!result && result.code == ValidationCode::NON_FINITE_VALUE &&
                runtime.prepared_systems().empty() &&
                runtime.audit().committed_source_sequence == 0U,
            "nonfinite realized particle escaped native preflight or rollback");
  }
  {
    const std::unique_ptr<RenderAssetRegistry> catalog = Catalog();
    OgreNextN1ParticleRuntime runtime;
    const ValidationResult result = runtime.Prepare(
        1U,
        Frame(1U,
              {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                System(10U)},
               {2U, 10U, Ogre14ParticleLifecycleOperation::UPDATE,
                System(10U, true, 0.5F)}}),
        *catalog, 9U, origin);
    Require(!result && result.code == ValidationCode::SEQUENCE_MISMATCH &&
                runtime.prepared_systems().empty() &&
                runtime.audit().create_commands == 0U,
            "multiple commands for one system partially prepared state");
  }
  {
    const std::unique_ptr<RenderAssetRegistry> catalog = Catalog();
    OgreNextN1ParticleRuntime runtime;
    Ogre14CapturedParticleSystem hostile = *System(10U);
    hostile.billboard_rotation_mode =
        Ogre14ParticleBillboardRotationMode::VERTICES;
    auto hostile_owner =
        std::make_shared<const Ogre14CapturedParticleSystem>(
            std::move(hostile));
    const ValidationResult result = runtime.Prepare(
        1U,
        Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                    hostile_owner}}),
        *catalog, 9U, origin);
    Require(!result && result.code == ValidationCode::UNSUPPORTED_FEATURE &&
                runtime.prepared_systems().empty(),
            "N1 accepted vertex rotation as shipped Dust texture rotation");
  }
}

void TestPermanentSystemTombstonesAndTransitionKinds() {
  const Double3 origin{100.0, 0.0, -50.0};
  const std::unique_ptr<RenderAssetRegistry> catalog = Catalog();
  OgreNextN1ParticleRuntime runtime;
  ValidationResult result = runtime.Prepare(
      1U,
      Frame(1U, {{1U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                  System(10U)}}),
      *catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(1U),
          "system tombstone seed CREATE failed");
  result = runtime.Prepare(
      2U,
      Frame(2U, {{2U, 10U, Ogre14ParticleLifecycleOperation::DESTROY,
                  nullptr}}),
      *catalog, 9U, origin);
  Require(result.ok() && runtime.Commit(2U),
          "system tombstone DESTROY failed");
  result = runtime.Prepare(
      3U,
      Frame(3U, {{3U, 10U, Ogre14ParticleLifecycleOperation::CREATE,
                  System(10U)}}),
      *catalog, 9U, origin);
  Require(!result &&
              result.field == "continuous_particles.commands.system_id" &&
              runtime.audit().committed_source_sequence == 2U,
          "destroyed system identity returned or changed durable state");

  OgreNextN1ParticleRuntime live_runtime;
  result = live_runtime.Prepare(
      1U,
      Frame(1U, {{1U, 20U, Ogre14ParticleLifecycleOperation::CREATE,
                  System(20U)}}),
      *catalog, 9U, origin);
  Require(result.ok() && live_runtime.Commit(1U),
          "transition-kind seed CREATE failed");
  result = live_runtime.Prepare(
      2U,
      Frame(2U, {{2U, 20U, Ogre14ParticleLifecycleOperation::UPDATE,
                  System(20U, false, 0.5F)}}),
      *catalog, 9U, origin);
  Require(!result &&
              result.field == "continuous_particles.commands.update" &&
              live_runtime.audit().committed_source_sequence == 1U,
          "UPDATE silently represented the required STOP transition");
  result = live_runtime.Prepare(
      3U,
      Frame(2U, {{2U, 20U, Ogre14ParticleLifecycleOperation::STOP,
                  System(20U, false, 0.5F)}}),
      *catalog, 9U, origin);
  Require(result.ok() && live_runtime.Commit(3U),
          "exact STOP transition failed");
  result = live_runtime.Prepare(
      4U,
      Frame(3U, {{3U, 20U, Ogre14ParticleLifecycleOperation::STOP,
                  System(20U, false, 0.75F)}}),
      *catalog, 9U, origin);
  Require(!result &&
              result.field == "continuous_particles.commands.stop" &&
              live_runtime.audit().committed_source_sequence == 2U,
          "repeated STOP was not rejected as an UPDATE semantic mismatch");
}

} // namespace

int main() {
  TestPinnedTextureCoordinateRotation();
  TestLifecycleDistinctTextureAndRollback();
  TestDroppedFrameAdvancesOnlyPortableSourceLineage();
  TestFinalDestroySurvivesPriorAssetRetirement();
  TestGenerationCloseReconcilesRejectedFrameGap();
  TestRetainedClosureRevalidatesEveryCurrentCatalog();
  TestFailClosedSourceAlphaAndStopSemantics();
  TestPermanentSystemTombstonesAndTransitionKinds();
  std::cout << "OgreNext N1 particle runtime tests passed\n";
  return EXIT_SUCCESS;
}
