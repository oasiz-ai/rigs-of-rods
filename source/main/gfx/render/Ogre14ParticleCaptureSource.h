/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Transactional pure-data capture of realized OGRE 14 particles.

#pragma once

#include "SceneSnapshot.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14ParticleCaptureVersion = 1U;
constexpr std::uint32_t kOgre14ParticleCapturedFrameVersion = 1U;
constexpr std::uint32_t kOgre14ParticleMaterialClosureReceiptVersion = 1U;

/// Canonical logical byte accounting excludes container padding and allocator
/// metadata. The named terms make the version-one sums auditable on every ABI.
constexpr std::uint64_t kOgre14ParticleLogicalU8Bytes = 1U;
constexpr std::uint64_t kOgre14ParticleLogicalU32Bytes = 4U;
constexpr std::uint64_t kOgre14ParticleLogicalU64Bytes = 8U;
constexpr std::uint64_t kOgre14ParticleLogicalFloat2Bytes = 8U;
constexpr std::uint64_t kOgre14ParticleLogicalFloat3Bytes = 12U;
constexpr std::uint64_t kOgre14ParticleLogicalFloat4Bytes = 16U;
constexpr std::uint64_t kOgre14ParticleLogicalAssetReferenceBytes =
    kOgre14ParticleLogicalU8Bytes + 2U * kOgre14ParticleLogicalU64Bytes +
    kOgre14ParticleLogicalU64Bytes;
constexpr std::uint64_t kOgre14ParticleLogicalClosureReceiptBytes =
    kOgre14ParticleLogicalU32Bytes + 2U * kOgre14ParticleLogicalU64Bytes +
    kOgre14ParticleLogicalAssetReferenceBytes + kOgre14ParticleLogicalU64Bytes;
constexpr std::uint64_t kOgre14ParticleLogicalStateBytes =
    kOgre14ParticleLogicalU64Bytes + 3U * kOgre14ParticleLogicalFloat3Bytes +
    kOgre14ParticleLogicalFloat4Bytes + kOgre14ParticleLogicalFloat2Bytes +
    3U * kOgre14ParticleLogicalU32Bytes;
constexpr std::uint64_t kOgre14ParticleLogicalSystemBytes =
    kOgre14ParticleLogicalU32Bytes + kOgre14ParticleLogicalU64Bytes +
    kOgre14ParticleLogicalU8Bytes + kOgre14ParticleLogicalClosureReceiptBytes +
    kOgre14ParticleLogicalU8Bytes + 8U * kOgre14ParticleLogicalU8Bytes +
    kOgre14ParticleLogicalU64Bytes;
constexpr std::uint64_t kOgre14ParticleLogicalEventBytes =
    2U * kOgre14ParticleLogicalU64Bytes + kOgre14ParticleLogicalU8Bytes;
static_assert(kOgre14ParticleLogicalAssetReferenceBytes == 25U);
static_assert(kOgre14ParticleLogicalClosureReceiptBytes == 53U);
static_assert(kOgre14ParticleLogicalStateBytes == 80U);
static_assert(kOgre14ParticleLogicalSystemBytes == 83U);
static_assert(kOgre14ParticleLogicalEventBytes == 17U);

class RenderAssetRegistry;

/// Version one admits only camera-facing point billboards. The other values
/// are named so a native producer can report the exact blocker without
/// collapsing an unsupported source mode into the supported one.
enum class Ogre14ParticleBillboardMode : std::uint8_t {
  CAMERA_FACING_POINT = 0U,
  ORIENTED_COMMON = 1U,
  ORIENTED_SELF = 2U,
  PERPENDICULAR_COMMON = 3U,
  PERPENDICULAR_SELF = 4U,
};

/// Explicit delta semantics for one stable particle-system identity.
enum class Ogre14ParticleLifecycleOperation : std::uint8_t {
  CREATE = 0U,
  UPDATE = 1U,
  STOP = 2U,
  DESTROY = 3U,
};

/// One realized continuous particle after all native simulation-side emitter
/// and affector work. Position is an absolute render-space position relative
/// to the frame's double-precision world origin, never emitter-local. A native
/// producer allocates particle IDs monotonically within a system and never
/// reuses them, allowing retained particles to be updated without pretending
/// each frame is a new burst.
struct Ogre14ParticleState {
  std::uint64_t particle_id = 0U;
  Float3 position{};
  Float3 direction{0.0F, 1.0F, 0.0F};
  Float3 velocity{};
  Float4 color_linear{1.0F, 1.0F, 1.0F, 1.0F};
  Float2 size_meters{0.1F, 0.1F};
  float rotation_radians = 0.0F;
  float age_seconds = 0.0F;
  float lifetime_seconds = 1.0F;
};

/// Exact proof that the native material translator closed this particle
/// system against one logical catalog state. Capture() independently resolves
/// `material` through the supplied borrowed catalog view while the caller keeps
/// that view quiescent for Capture(), so this receipt is lineage rather than a
/// trust-me boolean and the mutable registry is not treated as an immutable
/// snapshot. `translation_source_sequence` identifies the successful
/// translator frame that produced the closure.
struct Ogre14ParticleMaterialClosureReceipt {
  std::uint32_t version = kOgre14ParticleMaterialClosureReceiptVersion;
  std::uint64_t material_catalog_registry_id = 0U;
  std::uint64_t material_catalog_sequence = 0U;
  RenderAssetReference material;
  std::uint64_t translation_source_sequence = 0U;
};

/// Complete post-physics value snapshot for one live native particle system.
/// Definitions for emitters, affectors, sorting, texture animation, and local
/// transforms are deliberately not translated in v1. Instead the native tap
/// must provide already-realized world-space particles and set every
/// `requires_frontend_*` flag false. The exact translated material closure is
/// mandatory; this boundary never invents a smoke/fire shader.
struct Ogre14ParticleSystemCapture {
  std::uint32_t version = kOgre14ParticleCaptureVersion;
  std::uint64_t system_id = 0U;
  ParticleEffect effect = ParticleEffect::DUST;
  Ogre14ParticleMaterialClosureReceipt material_closure;
  Ogre14ParticleBillboardMode billboard_mode =
      Ogre14ParticleBillboardMode::CAMERA_FACING_POINT;
  bool particles_are_world_space = true;
  bool requires_frontend_emitter_evaluation = false;
  bool requires_frontend_affector_evaluation = false;
  bool requires_frontend_sorting = false;
  bool requires_texture_animation = false;
  bool system_visible = true;
  bool parent_visible = true;
  bool emitting = true;
  std::vector<Ogre14ParticleState> particles;
};

/// Producer-issued transition identity. Event IDs are strictly monotonic and
/// never reused for one capture-source lifetime. A complete frame contains
/// exactly one event for every derived create/update/stop/destroy transition
/// and no event for an unchanged system.
struct Ogre14ParticleLifecycleEvent {
  std::uint64_t event_id = 0U;
  std::uint64_t system_id = 0U;
  Ogre14ParticleLifecycleOperation operation =
      Ogre14ParticleLifecycleOperation::CREATE;
};

/// Complete authoritative joined post-physics particle inventory. Systems and
/// events may arrive in arbitrary traversal order; Capture() canonicalizes
/// them by stable identity. The two epochs must be equal and nonzero, proving
/// that the native tap ran after the copied simulation buffers were consumed.
struct Ogre14JoinedParticleFrame {
  std::uint32_t version = kOgre14ParticleCaptureVersion;
  std::uint64_t source_sequence = 0U;
  std::uint64_t material_catalog_registry_id = 0U;
  std::uint64_t material_catalog_sequence = 0U;
  std::uint64_t simulation_tick = 0U;
  double simulation_time_seconds = 0.0;
  Double3 absolute_world_origin_meters{};
  std::uint64_t joined_buffer_epoch = 0U;
  std::uint64_t post_physics_epoch = 0U;
  bool complete_inventory = false;
  std::vector<Ogre14ParticleSystemCapture> systems;
  std::vector<Ogre14ParticleLifecycleEvent> events;
};

/// Portable full state carried by CREATE, UPDATE, and STOP. Effective
/// visibility is the logical AND of system and every captured parent node.
struct Ogre14CapturedParticleSystem {
  std::uint64_t system_id = 0U;
  ParticleEffect effect = ParticleEffect::DUST;
  Ogre14ParticleMaterialClosureReceipt material_closure;
  Ogre14ParticleBillboardMode billboard_mode =
      Ogre14ParticleBillboardMode::CAMERA_FACING_POINT;
  bool effective_visible = true;
  bool emitting = true;
  std::vector<Ogre14ParticleState> particles;
};

struct Ogre14CapturedParticleCommand {
  std::uint64_t event_id = 0U;
  std::uint64_t system_id = 0U;
  Ogre14ParticleLifecycleOperation operation =
      Ogre14ParticleLifecycleOperation::CREATE;
  /// Null only for DESTROY. STOP retains the final complete particle state so
  /// existing particles can age out after emission has stopped.
  std::shared_ptr<const Ogre14CapturedParticleSystem> system;
};

/// Versioned delta adjunct to SceneSnapshot v4. The existing ParticleEvent is
/// intentionally retained for discrete bursts; it cannot express continuous
/// identity, material, age, rotation, or stop/destroy state. This adjunct
/// extends that boundary without changing the established v4 wire layout.
struct Ogre14ParticleCapturedFrame {
  std::uint32_t version = kOgre14ParticleCapturedFrameVersion;
  std::uint64_t source_sequence = 0U;
  std::uint64_t material_catalog_registry_id = 0U;
  std::uint64_t material_catalog_sequence = 0U;
  std::uint64_t simulation_tick = 0U;
  double simulation_time_seconds = 0.0;
  Double3 absolute_world_origin_meters{};
  std::uint64_t joined_buffer_epoch = 0U;
  std::vector<Ogre14CapturedParticleCommand> commands;
};

enum class Ogre14ParticleCaptureFaultPoint : std::uint8_t {
  AFTER_CANONICALIZATION = 0U,
  BEFORE_COMMIT = 1U,
};

class IOgre14ParticleCaptureFaultInjector {
public:
  virtual ~IOgre14ParticleCaptureFaultInjector() = default;
  [[nodiscard]] virtual bool
  ShouldFail(Ogre14ParticleCaptureFaultPoint point) = 0;
};

struct Ogre14ParticleCaptureConfiguration {
  std::uint32_t version = kOgre14ParticleCaptureVersion;
  std::uint64_t first_source_sequence = 1U;
  std::size_t maximum_live_systems = 4096U;
  std::size_t maximum_lifetime_systems = 65536U;
  std::size_t maximum_particles_per_system = 16384U;
  std::size_t maximum_particles_per_frame = 65536U;
  std::uint64_t maximum_lifetime_particles = 16U * 1024U * 1024U;
  std::size_t maximum_events_per_frame = 65536U;
  std::uint64_t maximum_lifetime_events = 64U * 1024U * 1024U;
  std::uint64_t maximum_payload_bytes_per_frame = 64U * 1024U * 1024U;
  /// Borrowed test/diagnostic hook. Production configurations leave this null.
  IOgre14ParticleCaptureFaultInjector *fault_injector = nullptr;
};

/// Transactional continuous-particle inventory. New source frames must be
/// contiguous; exact same-sequence replay returns the prior immutable result.
/// Every other failure leaves both the durable registry and caller output
/// unchanged. Omitted systems become permanent tombstones.
class Ogre14ParticleCaptureSource final {
public:
  explicit Ogre14ParticleCaptureSource(
      Ogre14ParticleCaptureConfiguration configuration = {});
  ~Ogre14ParticleCaptureSource();

  Ogre14ParticleCaptureSource(const Ogre14ParticleCaptureSource &) = delete;
  Ogre14ParticleCaptureSource &
  operator=(const Ogre14ParticleCaptureSource &) = delete;
  Ogre14ParticleCaptureSource(Ogre14ParticleCaptureSource &&) = delete;
  Ogre14ParticleCaptureSource &
  operator=(Ogre14ParticleCaptureSource &&) = delete;

  /// `material_catalog` is borrowed only for this call. The caller must
  /// serialize RenderAssetRegistry::Apply() and keep this exact joined-boundary
  /// view quiescent until Capture() returns; const access does not turn the
  /// mutable registry type into a concurrent immutable snapshot.
  [[nodiscard]] ValidationResult
  Capture(const Ogre14JoinedParticleFrame &frame,
          const RenderAssetRegistry &material_catalog,
          Ogre14ParticleCapturedFrame &captured);

  [[nodiscard]] std::uint64_t last_source_sequence() const noexcept;
  [[nodiscard]] std::uint64_t highest_event_id() const noexcept;
  [[nodiscard]] std::uint64_t highest_system_id() const noexcept;
  [[nodiscard]] std::size_t known_system_count() const noexcept;
  [[nodiscard]] std::size_t live_system_count() const noexcept;
  [[nodiscard]] std::uint64_t lifetime_particle_count() const noexcept;
  [[nodiscard]] std::uint64_t lifetime_event_count() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
