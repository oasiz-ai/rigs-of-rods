/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14ParticleCaptureSource.h"

#include "RenderAssetRegistry.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <new>
#include <type_traits>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, index);
}

bool IsKnownEffect(ParticleEffect effect) noexcept {
  switch (effect) {
  case ParticleEffect::TIRE_SMOKE:
  case ParticleEffect::DUST:
  case ParticleEffect::SPARKS:
  case ParticleEffect::WATER_SPRAY:
  case ParticleEffect::STEAM:
  case ParticleEffect::FIRE:
  case ParticleEffect::DEBRIS:
    return true;
  }
  return false;
}

bool IsKnownOperation(Ogre14ParticleLifecycleOperation operation) noexcept {
  switch (operation) {
  case Ogre14ParticleLifecycleOperation::CREATE:
  case Ogre14ParticleLifecycleOperation::UPDATE:
  case Ogre14ParticleLifecycleOperation::STOP:
  case Ogre14ParticleLifecycleOperation::DESTROY:
    return true;
  }
  return false;
}

bool AddChecked(std::uint64_t amount, std::uint64_t &total) noexcept {
  if (amount > (std::numeric_limits<std::uint64_t>::max)() - total) {
    return false;
  }
  total += amount;
  return true;
}

bool AddMultipliedChecked(std::size_t count, std::uint64_t element_size,
                          std::uint64_t &total) noexcept {
  if (element_size != 0U &&
      count > (std::numeric_limits<std::uint64_t>::max)() / element_size) {
    return false;
  }
  return AddChecked(static_cast<std::uint64_t>(count) * element_size, total);
}

bool AddCountChecked(std::uint64_t amount, std::uint64_t &total) noexcept {
  return AddChecked(amount, total);
}

bool IsUnitDirection(const Float3 &direction) noexcept {
  if (!IsFinite(direction)) {
    return false;
  }
  const double x = static_cast<double>(direction.x);
  const double y = static_cast<double>(direction.y);
  const double z = static_cast<double>(direction.z);
  const double length_squared = x * x + y * y + z * z;
  return std::isfinite(length_squared) &&
         std::fabs(length_squared - 1.0) <= 1.0e-3;
}

void FoldSignedZero(float &value) noexcept {
  if (value == 0.0F) {
    value = 0.0F;
  }
}

void FoldSignedZero(double &value) noexcept {
  if (value == 0.0) {
    value = 0.0;
  }
}

void CanonicalizeParticle(Ogre14ParticleState &particle) noexcept {
  FoldSignedZero(particle.position.x);
  FoldSignedZero(particle.position.y);
  FoldSignedZero(particle.position.z);
  FoldSignedZero(particle.direction.x);
  FoldSignedZero(particle.direction.y);
  FoldSignedZero(particle.direction.z);
  FoldSignedZero(particle.velocity.x);
  FoldSignedZero(particle.velocity.y);
  FoldSignedZero(particle.velocity.z);
  FoldSignedZero(particle.color_linear.x);
  FoldSignedZero(particle.color_linear.y);
  FoldSignedZero(particle.color_linear.z);
  FoldSignedZero(particle.color_linear.w);
  FoldSignedZero(particle.size_meters.x);
  FoldSignedZero(particle.size_meters.y);
  FoldSignedZero(particle.rotation_radians);
  FoldSignedZero(particle.age_seconds);
  FoldSignedZero(particle.lifetime_seconds);
}

ValidationResult
ValidateConfiguration(const Ogre14ParticleCaptureConfiguration &configuration) {
  if (configuration.version != kOgre14ParticleCaptureVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "particle_capture.configuration.version",
                   "unsupported particle-capture configuration version");
  }
  if (configuration.first_source_sequence == 0U ||
      configuration.maximum_live_systems == 0U ||
      configuration.maximum_lifetime_systems == 0U ||
      configuration.maximum_particles_per_system == 0U ||
      configuration.maximum_particles_per_frame == 0U ||
      configuration.maximum_lifetime_particles == 0U ||
      configuration.maximum_events_per_frame == 0U ||
      configuration.maximum_lifetime_events == 0U ||
      configuration.maximum_payload_bytes_per_frame == 0U) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "particle_capture.configuration",
        "every identity, frame, lifetime, and byte cap must be nonzero");
  }
  if (configuration.maximum_live_systems >
          configuration.maximum_lifetime_systems ||
      configuration.maximum_particles_per_system >
          configuration.maximum_particles_per_frame ||
      static_cast<std::uint64_t>(configuration.maximum_particles_per_frame) >
          configuration.maximum_lifetime_particles ||
      static_cast<std::uint64_t>(configuration.maximum_events_per_frame) >
          configuration.maximum_lifetime_events) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "particle_capture.configuration",
                   "per-system and per-frame caps must fit lifetime caps");
  }
  return ValidationResult::Success();
}

ValidationResult
PreflightFrame(const Ogre14JoinedParticleFrame &frame,
               const Ogre14ParticleCaptureConfiguration &configuration) {
  if (frame.version != kOgre14ParticleCaptureVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "particle_frame.version",
                   "unsupported joined particle-frame version");
  }
  if (!frame.complete_inventory) {
    return Failure(
        ValidationCode::EMPTY_PAYLOAD, "particle_frame.complete_inventory",
        "partial particle inventories may not mutate lifecycle state");
  }
  if (frame.source_sequence == 0U || frame.material_catalog_registry_id == 0U ||
      frame.material_catalog_sequence == 0U ||
      frame.joined_buffer_epoch == 0U || frame.post_physics_epoch == 0U) {
    return Failure(
        ValidationCode::INVALID_IDENTIFIER, "particle_frame.lineage",
        "source, catalog, and joined post-physics lineage must be nonzero");
  }
  if (frame.joined_buffer_epoch != frame.post_physics_epoch) {
    return Failure(
        ValidationCode::SEQUENCE_MISMATCH, "particle_frame.post_physics_epoch",
        "particle capture must follow the exact joined buffer epoch");
  }
  if (!IsFinite(frame.simulation_time_seconds) ||
      frame.simulation_time_seconds < 0.0 ||
      !IsFinite(frame.absolute_world_origin_meters)) {
    return Failure(ValidationCode::NON_FINITE_VALUE,
                   "particle_frame.simulation_state",
                   "simulation time and absolute world origin must be finite");
  }
  if (frame.systems.size() > configuration.maximum_live_systems) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "particle_frame.systems",
        "live particle-system count exceeds the configured frame cap");
  }
  if (frame.events.size() > configuration.maximum_events_per_frame) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "particle_frame.events",
        "particle transition count exceeds the configured frame cap");
  }

  std::uint64_t particle_count = 0U;
  std::uint64_t payload_bytes = 0U;
  if (!AddMultipliedChecked(frame.systems.size(),
                            kOgre14ParticleLogicalSystemBytes, payload_bytes) ||
      !AddMultipliedChecked(frame.events.size(),
                            kOgre14ParticleLogicalEventBytes, payload_bytes)) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "particle_frame.payload_bytes",
                   "particle descriptor byte accounting overflowed");
  }
  for (std::size_t index = 0U; index < frame.systems.size(); ++index) {
    const std::size_t count = frame.systems[index].particles.size();
    if (count > configuration.maximum_particles_per_system) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "particle_frame.systems.particles",
                     "particle count exceeds the configured per-system cap",
                     index);
    }
    if (!AddCountChecked(static_cast<std::uint64_t>(count), particle_count) ||
        particle_count > configuration.maximum_particles_per_frame) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "particle_frame.particle_count",
          "aggregate particle count exceeds the configured frame cap", index);
    }
    if (!AddMultipliedChecked(count, kOgre14ParticleLogicalStateBytes,
                              payload_bytes)) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "particle_frame.payload_bytes",
                     "particle-state byte accounting overflowed", index);
    }
  }
  if (payload_bytes > configuration.maximum_payload_bytes_per_frame) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "particle_frame.payload_bytes",
                   "logical particle payload exceeds the configured byte cap");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateParticle(const Ogre14ParticleState &particle,
                                  std::size_t index) {
  if (particle.particle_id == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "particle_system.particles.particle_id",
                   "particle identity must be nonzero", index);
  }
  if (!IsFinite(particle.position) || !IsFinite(particle.direction) ||
      !IsFinite(particle.velocity) || !IsFinite(particle.color_linear) ||
      !IsFinite(particle.size_meters) || !IsFinite(particle.rotation_radians) ||
      !IsFinite(particle.age_seconds) || !IsFinite(particle.lifetime_seconds)) {
    return Failure(ValidationCode::NON_FINITE_VALUE,
                   "particle_system.particles.payload",
                   "particle position, unit direction, velocity, color, size, "
                   "rotation, age, and lifetime must be finite",
                   index);
  }
  if (!IsUnitDirection(particle.direction) || particle.color_linear.x < 0.0F ||
      particle.color_linear.y < 0.0F || particle.color_linear.z < 0.0F ||
      particle.color_linear.w < 0.0F || particle.color_linear.w > 1.0F ||
      particle.size_meters.x <= 0.0F || particle.size_meters.y <= 0.0F ||
      particle.age_seconds < 0.0F || particle.lifetime_seconds <= 0.0F ||
      particle.age_seconds > particle.lifetime_seconds) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "particle_system.particles.payload",
                   "particle direction, color, size, age, and lifetime are "
                   "outside the portable range",
                   index);
  }
  return ValidationResult::Success();
}

ValidationResult ValidateSystem(Ogre14ParticleSystemCapture &system,
                                std::size_t system_index,
                                const Ogre14JoinedParticleFrame &frame,
                                const RenderAssetRegistry &material_catalog) {
  if (system.version != kOgre14ParticleCaptureVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "particle_system.version",
                   "unsupported particle-system capture version", system_index);
  }
  if (system.system_id == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "particle_system.system_id",
                   "particle-system identity must be nonzero", system_index);
  }
  if (!IsKnownEffect(system.effect)) {
    return Failure(ValidationCode::INVALID_ENUM, "particle_system.effect",
                   "unknown particle effect", system_index);
  }
  const Ogre14ParticleMaterialClosureReceipt &closure = system.material_closure;
  if (closure.version != kOgre14ParticleMaterialClosureReceiptVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "particle_system.material_closure.version",
                   "unsupported particle material-closure receipt version",
                   system_index);
  }
  if (closure.material_catalog_registry_id !=
          frame.material_catalog_registry_id ||
      closure.material_catalog_sequence != frame.material_catalog_sequence) {
    return Failure(
        ValidationCode::SEQUENCE_MISMATCH,
        "particle_system.material_closure.catalog_lineage",
        "material-closure receipt does not name the joined frame catalog",
        system_index);
  }
  if (closure.translation_source_sequence == 0U) {
    return Failure(
        ValidationCode::INVALID_IDENTIFIER,
        "particle_system.material_closure.translation_source_sequence",
        "material-closure translator lineage must be nonzero", system_index);
  }
  if (!closure.material.valid() ||
      closure.material.kind != RenderAssetKind::MATERIAL) {
    return Failure(
        closure.material.valid() ? ValidationCode::WRONG_ASSET_KIND
                                 : ValidationCode::INVALID_ASSET_REFERENCE,
        "particle_system.material_closure.material",
        "continuous particles require an exact material asset reference",
        system_index);
  }
  if (material_catalog.ResolveMaterial(closure.material) == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "particle_system.material_closure.material",
                   "material-closure receipt does not resolve to a live exact "
                   "material revision",
                   system_index);
  }
  if (system.billboard_mode !=
      Ogre14ParticleBillboardMode::CAMERA_FACING_POINT) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "particle_system.billboard_mode",
        "particle capture v1 supports only camera-facing point billboards",
        system_index);
  }
  if (!system.particles_are_world_space) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "particle_system.particles_are_world_space",
                   "local-space particle transforms are not supported",
                   system_index);
  }
  if (system.requires_frontend_emitter_evaluation ||
      system.requires_frontend_affector_evaluation ||
      system.requires_frontend_sorting || system.requires_texture_animation) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "particle_system.frontend_behavior",
                   "emitter, affector, sorting, and texture-animation "
                   "translation is not supported",
                   system_index);
  }

  for (Ogre14ParticleState &particle : system.particles) {
    CanonicalizeParticle(particle);
  }
  std::sort(system.particles.begin(), system.particles.end(),
            [](const Ogre14ParticleState &lhs, const Ogre14ParticleState &rhs) {
              return lhs.particle_id < rhs.particle_id;
            });
  std::uint64_t previous_id = 0U;
  for (std::size_t index = 0U; index < system.particles.size(); ++index) {
    ValidationResult validation =
        ValidateParticle(system.particles[index], index);
    if (!validation) {
      return validation;
    }
    if (index != 0U && system.particles[index].particle_id == previous_id) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "particle_system.particles.particle_id",
                     "particle identity is duplicated", index);
    }
    previous_id = system.particles[index].particle_id;
  }
  return ValidationResult::Success();
}

bool EqualParticle(const Ogre14ParticleState &lhs,
                   const Ogre14ParticleState &rhs) noexcept {
  return lhs.particle_id == rhs.particle_id && lhs.position == rhs.position &&
         lhs.direction == rhs.direction && lhs.velocity == rhs.velocity &&
         lhs.color_linear == rhs.color_linear &&
         lhs.size_meters == rhs.size_meters &&
         lhs.rotation_radians == rhs.rotation_radians &&
         lhs.age_seconds == rhs.age_seconds &&
         lhs.lifetime_seconds == rhs.lifetime_seconds;
}

bool EqualParticles(const std::vector<Ogre14ParticleState> &lhs,
                    const std::vector<Ogre14ParticleState> &rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.size(); ++index) {
    if (!EqualParticle(lhs[index], rhs[index])) {
      return false;
    }
  }
  return true;
}

bool EqualClosure(const Ogre14ParticleMaterialClosureReceipt &lhs,
                  const Ogre14ParticleMaterialClosureReceipt &rhs) noexcept {
  return lhs.version == rhs.version &&
         lhs.material_catalog_registry_id == rhs.material_catalog_registry_id &&
         lhs.material_catalog_sequence == rhs.material_catalog_sequence &&
         lhs.material == rhs.material &&
         lhs.translation_source_sequence == rhs.translation_source_sequence;
}

bool EqualSystemInput(const Ogre14ParticleSystemCapture &lhs,
                      const Ogre14ParticleSystemCapture &rhs) noexcept {
  return lhs.version == rhs.version && lhs.system_id == rhs.system_id &&
         lhs.effect == rhs.effect &&
         EqualClosure(lhs.material_closure, rhs.material_closure) &&
         lhs.billboard_mode == rhs.billboard_mode &&
         lhs.particles_are_world_space == rhs.particles_are_world_space &&
         lhs.requires_frontend_emitter_evaluation ==
             rhs.requires_frontend_emitter_evaluation &&
         lhs.requires_frontend_affector_evaluation ==
             rhs.requires_frontend_affector_evaluation &&
         lhs.requires_frontend_sorting == rhs.requires_frontend_sorting &&
         lhs.requires_texture_animation == rhs.requires_texture_animation &&
         lhs.system_visible == rhs.system_visible &&
         lhs.parent_visible == rhs.parent_visible &&
         lhs.emitting == rhs.emitting &&
         EqualParticles(lhs.particles, rhs.particles);
}

bool EqualEvent(const Ogre14ParticleLifecycleEvent &lhs,
                const Ogre14ParticleLifecycleEvent &rhs) noexcept {
  return lhs.event_id == rhs.event_id && lhs.system_id == rhs.system_id &&
         lhs.operation == rhs.operation;
}

bool EqualJoinedFrame(const Ogre14JoinedParticleFrame &lhs,
                      const Ogre14JoinedParticleFrame &rhs) noexcept {
  if (lhs.version != rhs.version ||
      lhs.source_sequence != rhs.source_sequence ||
      lhs.material_catalog_registry_id != rhs.material_catalog_registry_id ||
      lhs.material_catalog_sequence != rhs.material_catalog_sequence ||
      lhs.simulation_tick != rhs.simulation_tick ||
      lhs.simulation_time_seconds != rhs.simulation_time_seconds ||
      lhs.absolute_world_origin_meters != rhs.absolute_world_origin_meters ||
      lhs.joined_buffer_epoch != rhs.joined_buffer_epoch ||
      lhs.post_physics_epoch != rhs.post_physics_epoch ||
      lhs.complete_inventory != rhs.complete_inventory ||
      lhs.systems.size() != rhs.systems.size() ||
      lhs.events.size() != rhs.events.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.systems.size(); ++index) {
    if (!EqualSystemInput(lhs.systems[index], rhs.systems[index])) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < lhs.events.size(); ++index) {
    if (!EqualEvent(lhs.events[index], rhs.events[index])) {
      return false;
    }
  }
  return true;
}

bool EqualCapturedState(const Ogre14CapturedParticleSystem &lhs,
                        const Ogre14ParticleSystemCapture &rhs) noexcept {
  return lhs.system_id == rhs.system_id && lhs.effect == rhs.effect &&
         EqualClosure(lhs.material_closure, rhs.material_closure) &&
         lhs.billboard_mode == rhs.billboard_mode &&
         lhs.effective_visible == (rhs.system_visible && rhs.parent_visible) &&
         lhs.emitting == rhs.emitting &&
         EqualParticles(lhs.particles, rhs.particles);
}

std::shared_ptr<const Ogre14CapturedParticleSystem>
BuildCapturedState(const Ogre14ParticleSystemCapture &source) {
  auto candidate = std::make_shared<Ogre14CapturedParticleSystem>();
  candidate->system_id = source.system_id;
  candidate->effect = source.effect;
  candidate->material_closure = source.material_closure;
  candidate->billboard_mode = source.billboard_mode;
  candidate->effective_visible = source.system_visible && source.parent_visible;
  candidate->emitting = source.emitting;
  candidate->particles = source.particles;
  return candidate;
}

} // namespace

class Ogre14ParticleCaptureSource::Impl final {
public:
  enum class SystemLifecycle : std::uint8_t {
    LIVE = 0U,
    STOPPED = 1U,
    DESTROYED = 2U,
  };

  struct SystemRecord {
    SystemLifecycle lifecycle = SystemLifecycle::LIVE;
    std::uint64_t highest_particle_id = 0U;
    std::shared_ptr<const Ogre14CapturedParticleSystem> state;
  };

  explicit Impl(Ogre14ParticleCaptureConfiguration value)
      : configuration(std::move(value)) {}

  Ogre14ParticleCaptureConfiguration configuration;
  std::map<std::uint64_t, SystemRecord> systems;
  std::uint64_t highest_system_id = 0U;
  std::uint64_t highest_event_id_value = 0U;
  std::uint64_t lifetime_particle_count_value = 0U;
  std::uint64_t lifetime_event_count = 0U;
  std::uint64_t last_source_sequence_value = 0U;
  std::uint64_t material_catalog_registry_id = 0U;
  std::uint64_t material_catalog_sequence = 0U;
  std::uint64_t last_simulation_tick = 0U;
  double last_simulation_time_seconds = 0.0;
  std::uint64_t last_joined_buffer_epoch = 0U;
  bool has_frame = false;
  Ogre14JoinedParticleFrame last_input;
  Ogre14ParticleCapturedFrame last_output;
};

Ogre14ParticleCaptureSource::Ogre14ParticleCaptureSource(
    Ogre14ParticleCaptureConfiguration configuration)
    : impl_(std::make_unique<Impl>(std::move(configuration))) {}

Ogre14ParticleCaptureSource::~Ogre14ParticleCaptureSource() = default;

ValidationResult Ogre14ParticleCaptureSource::Capture(
    const Ogre14JoinedParticleFrame &frame,
    const RenderAssetRegistry &material_catalog,
    Ogre14ParticleCapturedFrame &captured) {
  static_assert(
      std::is_nothrow_move_assignable<Ogre14ParticleCapturedFrame>::value,
      "particle capture commits output with one non-throwing move");

  ValidationResult validation = ValidateConfiguration(impl_->configuration);
  if (!validation) {
    return validation;
  }
  validation = PreflightFrame(frame, impl_->configuration);
  if (!validation) {
    return validation;
  }
  if (material_catalog.registry_id() != frame.material_catalog_registry_id) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "particle_frame.material_catalog_registry_id",
                   "supplied catalog view has a different registry identity");
  }
  if (material_catalog.sequence() != frame.material_catalog_sequence) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "particle_frame.material_catalog_sequence",
                   "supplied catalog view is not the exact declared sequence");
  }

  try {
    Ogre14JoinedParticleFrame canonical = frame;
    FoldSignedZero(canonical.simulation_time_seconds);
    FoldSignedZero(canonical.absolute_world_origin_meters.x);
    FoldSignedZero(canonical.absolute_world_origin_meters.y);
    FoldSignedZero(canonical.absolute_world_origin_meters.z);
    std::sort(canonical.systems.begin(), canonical.systems.end(),
              [](const Ogre14ParticleSystemCapture &lhs,
                 const Ogre14ParticleSystemCapture &rhs) {
                return lhs.system_id < rhs.system_id;
              });
    for (std::size_t index = 0U; index < canonical.systems.size(); ++index) {
      validation = ValidateSystem(canonical.systems[index], index, canonical,
                                  material_catalog);
      if (!validation) {
        return validation;
      }
      if (index != 0U && canonical.systems[index - 1U].system_id ==
                             canonical.systems[index].system_id) {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "particle_frame.systems.system_id",
                       "particle-system identity is duplicated", index);
      }
    }

    std::sort(canonical.events.begin(), canonical.events.end(),
              [](const Ogre14ParticleLifecycleEvent &lhs,
                 const Ogre14ParticleLifecycleEvent &rhs) {
                return lhs.event_id < rhs.event_id;
              });
    for (std::size_t index = 0U; index < canonical.events.size(); ++index) {
      const Ogre14ParticleLifecycleEvent &event = canonical.events[index];
      if (event.event_id == 0U || event.system_id == 0U) {
        return Failure(ValidationCode::INVALID_IDENTIFIER,
                       "particle_frame.events.identity",
                       "event and particle-system identities must be nonzero",
                       index);
      }
      if (!IsKnownOperation(event.operation)) {
        return Failure(ValidationCode::INVALID_ENUM,
                       "particle_frame.events.operation",
                       "unknown particle lifecycle operation", index);
      }
      if (index != 0U &&
          canonical.events[index - 1U].event_id == event.event_id) {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "particle_frame.events.event_id",
                       "particle event identity is duplicated", index);
      }
    }

    if (impl_->configuration.fault_injector != nullptr &&
        impl_->configuration.fault_injector->ShouldFail(
            Ogre14ParticleCaptureFaultPoint::AFTER_CANONICALIZATION)) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "particle_capture.injected_failure",
                     "fault injected after canonicalization");
    }

    if (impl_->has_frame &&
        canonical.source_sequence == impl_->last_source_sequence_value) {
      if (!EqualJoinedFrame(canonical, impl_->last_input)) {
        return Failure(ValidationCode::SEQUENCE_MISMATCH,
                       "particle_frame.source_sequence",
                       "same-sequence replay changed authoritative contents");
      }
      Ogre14ParticleCapturedFrame replay = impl_->last_output;
      captured = std::move(replay);
      return ValidationResult::Success();
    }

    const std::uint64_t expected_sequence =
        impl_->has_frame ? (impl_->last_source_sequence_value ==
                                    (std::numeric_limits<std::uint64_t>::max)()
                                ? 0U
                                : impl_->last_source_sequence_value + 1U)
                         : impl_->configuration.first_source_sequence;
    if (expected_sequence == 0U ||
        canonical.source_sequence != expected_sequence) {
      return Failure(
          ValidationCode::SEQUENCE_MISMATCH, "particle_frame.source_sequence",
          "new particle frames must advance the source sequence exactly once");
    }
    if (impl_->has_frame) {
      if (canonical.material_catalog_registry_id !=
          impl_->material_catalog_registry_id) {
        return Failure(
            ValidationCode::INVALID_IDENTIFIER,
            "particle_frame.material_catalog_registry_id",
            "material catalog identity changed during capture lifetime");
      }
      if (canonical.material_catalog_sequence <
          impl_->material_catalog_sequence) {
        return Failure(ValidationCode::SEQUENCE_MISMATCH,
                       "particle_frame.material_catalog_sequence",
                       "material catalog sequence regressed");
      }
      if (canonical.joined_buffer_epoch <= impl_->last_joined_buffer_epoch) {
        return Failure(ValidationCode::SEQUENCE_MISMATCH,
                       "particle_frame.joined_buffer_epoch",
                       "joined post-physics epochs must advance monotonically");
      }
      if (canonical.simulation_tick < impl_->last_simulation_tick ||
          canonical.simulation_time_seconds <
              impl_->last_simulation_time_seconds) {
        return Failure(ValidationCode::SEQUENCE_MISMATCH,
                       "particle_frame.simulation_state",
                       "simulation tick and time may not regress");
      }
    }

    auto candidate = std::make_unique<Impl>(*impl_);
    std::map<std::uint64_t, Ogre14ParticleLifecycleOperation>
        required_operations;

    for (const Ogre14ParticleSystemCapture &system : canonical.systems) {
      auto prior = impl_->systems.find(system.system_id);
      auto durable = candidate->systems.find(system.system_id);
      if (prior == impl_->systems.end()) {
        if (system.system_id <= candidate->highest_system_id) {
          return Failure(
              ValidationCode::DUPLICATE_IDENTIFIER, "particle_system.system_id",
              "new particle-system identities must be globally monotonic");
        }
        if (candidate->systems.size() >=
            candidate->configuration.maximum_lifetime_systems) {
          return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                         "particle_capture.lifetime_systems",
                         "particle-system lifetime identity cap was exhausted");
        }
        Impl::SystemRecord record;
        record.lifecycle = system.emitting ? Impl::SystemLifecycle::LIVE
                                           : Impl::SystemLifecycle::STOPPED;
        record.state = BuildCapturedState(system);
        std::uint64_t previous_particle_id = 0U;
        for (const Ogre14ParticleState &particle : system.particles) {
          if (particle.particle_id <= previous_particle_id) {
            return Failure(
                ValidationCode::DUPLICATE_IDENTIFIER,
                "particle_system.particles.particle_id",
                "new particle identities must advance monotonically");
          }
          previous_particle_id = particle.particle_id;
          if (!AddCountChecked(1U, candidate->lifetime_particle_count_value) ||
              candidate->lifetime_particle_count_value >
                  candidate->configuration.maximum_lifetime_particles) {
            return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                           "particle_capture.lifetime_particles",
                           "particle lifetime identity cap was exhausted");
          }
        }
        record.highest_particle_id = previous_particle_id;
        candidate->systems.emplace(system.system_id, std::move(record));
        candidate->highest_system_id = system.system_id;
        required_operations.emplace(system.system_id,
                                    Ogre14ParticleLifecycleOperation::CREATE);
        continue;
      }

      if (prior->second.lifecycle == Impl::SystemLifecycle::DESTROYED ||
          durable == candidate->systems.end()) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "particle_system.system_id",
                       "a destroyed particle-system identity may never return");
      }
      if (system.effect != prior->second.state->effect) {
        return Failure(
            ValidationCode::REVISION_MISMATCH, "particle_system.effect",
            "a particle-system identity may never change effect kind");
      }
      const Ogre14ParticleMaterialClosureReceipt &prior_closure =
          prior->second.state->material_closure;
      if (system.material_closure.translation_source_sequence <
              prior_closure.translation_source_sequence ||
          (!EqualClosure(system.material_closure, prior_closure) &&
           system.material_closure.translation_source_sequence <=
               prior_closure.translation_source_sequence)) {
        return Failure(
            ValidationCode::SEQUENCE_MISMATCH,
            "particle_system.material_closure.translation_source_sequence",
            "changed material closure must advance translator lineage");
      }
      const std::vector<Ogre14ParticleState> &previous_particles =
          prior->second.state->particles;
      std::size_t previous_index = 0U;
      std::uint64_t new_highest = prior->second.highest_particle_id;
      for (const Ogre14ParticleState &particle : system.particles) {
        while (previous_index < previous_particles.size() &&
               previous_particles[previous_index].particle_id <
                   particle.particle_id) {
          ++previous_index;
        }
        const bool remains_live =
            previous_index < previous_particles.size() &&
            previous_particles[previous_index].particle_id ==
                particle.particle_id;
        if (remains_live &&
            (particle.age_seconds <
                 previous_particles[previous_index].age_seconds ||
             particle.lifetime_seconds !=
                 previous_particles[previous_index].lifetime_seconds)) {
          return Failure(ValidationCode::REVISION_MISMATCH,
                         "particle_system.particles.age_lineage",
                         "retained particle age may not regress and lifetime "
                         "may not change");
        }
        if (!remains_live) {
          if (prior->second.lifecycle == Impl::SystemLifecycle::STOPPED &&
              !system.emitting) {
            return Failure(
                ValidationCode::REVISION_MISMATCH,
                "particle_system.particles.particle_id",
                "a continuously stopped particle system may not introduce "
                "new particle identities");
          }
          if (particle.particle_id <= prior->second.highest_particle_id) {
            return Failure(ValidationCode::REVISION_MISMATCH,
                           "particle_system.particles.particle_id",
                           "a removed particle identity may never return");
          }
          if (particle.particle_id <= new_highest) {
            return Failure(
                ValidationCode::DUPLICATE_IDENTIFIER,
                "particle_system.particles.particle_id",
                "new particle identities must advance monotonically");
          }
          new_highest = particle.particle_id;
          if (!AddCountChecked(1U, candidate->lifetime_particle_count_value) ||
              candidate->lifetime_particle_count_value >
                  candidate->configuration.maximum_lifetime_particles) {
            return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                           "particle_capture.lifetime_particles",
                           "particle lifetime identity cap was exhausted");
          }
        }
      }

      const bool changed = !EqualCapturedState(*prior->second.state, system);
      if (changed) {
        const Ogre14ParticleLifecycleOperation operation =
            prior->second.state->emitting && !system.emitting
                ? Ogre14ParticleLifecycleOperation::STOP
                : Ogre14ParticleLifecycleOperation::UPDATE;
        required_operations.emplace(system.system_id, operation);
        durable->second.state = BuildCapturedState(system);
      }
      durable->second.highest_particle_id = new_highest;
      durable->second.lifecycle = system.emitting
                                      ? Impl::SystemLifecycle::LIVE
                                      : Impl::SystemLifecycle::STOPPED;
    }

    for (const auto &entry : impl_->systems) {
      if (entry.second.lifecycle == Impl::SystemLifecycle::DESTROYED) {
        continue;
      }
      const auto current = std::lower_bound(
          canonical.systems.begin(), canonical.systems.end(), entry.first,
          [](const Ogre14ParticleSystemCapture &system, std::uint64_t id) {
            return system.system_id < id;
          });
      if (current != canonical.systems.end() &&
          current->system_id == entry.first) {
        continue;
      }
      required_operations.emplace(entry.first,
                                  Ogre14ParticleLifecycleOperation::DESTROY);
      Impl::SystemRecord &record = candidate->systems.at(entry.first);
      record.lifecycle = Impl::SystemLifecycle::DESTROYED;
      record.state.reset();
    }

    if (required_operations.size() != canonical.events.size()) {
      return Failure(ValidationCode::SIZE_MISMATCH, "particle_frame.events",
                     "complete frame must carry exactly one event per derived "
                     "lifecycle transition");
    }

    std::map<std::uint64_t, std::uint64_t> event_by_system;
    for (std::size_t index = 0U; index < canonical.events.size(); ++index) {
      const Ogre14ParticleLifecycleEvent &event = canonical.events[index];
      if (event.event_id <= impl_->highest_event_id_value) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "particle_frame.events.event_id",
                       "particle event identities must be globally monotonic "
                       "and never reused",
                       index);
      }
      if (!event_by_system.emplace(event.system_id, event.event_id).second) {
        return Failure(
            ValidationCode::DUPLICATE_IDENTIFIER,
            "particle_frame.events.system_id",
            "one system received multiple lifecycle events in a frame", index);
      }
      const auto required = required_operations.find(event.system_id);
      if (required == required_operations.end() ||
          required->second != event.operation) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "particle_frame.events.operation",
                       "particle lifecycle event does not match the complete "
                       "inventory transition",
                       index);
      }
    }
    if (!AddCountChecked(static_cast<std::uint64_t>(canonical.events.size()),
                         candidate->lifetime_event_count) ||
        candidate->lifetime_event_count >
            candidate->configuration.maximum_lifetime_events) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "particle_capture.lifetime_events",
                     "particle event lifetime identity cap was exhausted");
    }

    Ogre14ParticleCapturedFrame result;
    result.source_sequence = canonical.source_sequence;
    result.material_catalog_registry_id =
        canonical.material_catalog_registry_id;
    result.material_catalog_sequence = canonical.material_catalog_sequence;
    result.simulation_tick = canonical.simulation_tick;
    result.simulation_time_seconds = canonical.simulation_time_seconds;
    result.absolute_world_origin_meters =
        canonical.absolute_world_origin_meters;
    result.joined_buffer_epoch = canonical.joined_buffer_epoch;
    result.commands.reserve(canonical.events.size());
    for (const Ogre14ParticleLifecycleEvent &event : canonical.events) {
      Ogre14CapturedParticleCommand command;
      command.event_id = event.event_id;
      command.system_id = event.system_id;
      command.operation = event.operation;
      if (event.operation != Ogre14ParticleLifecycleOperation::DESTROY) {
        command.system = candidate->systems.at(event.system_id).state;
      }
      result.commands.push_back(std::move(command));
    }

    candidate->last_source_sequence_value = canonical.source_sequence;
    candidate->material_catalog_registry_id =
        canonical.material_catalog_registry_id;
    candidate->material_catalog_sequence = canonical.material_catalog_sequence;
    candidate->last_simulation_tick = canonical.simulation_tick;
    candidate->last_simulation_time_seconds = canonical.simulation_time_seconds;
    candidate->last_joined_buffer_epoch = canonical.joined_buffer_epoch;
    candidate->has_frame = true;
    if (!canonical.events.empty()) {
      candidate->highest_event_id_value = canonical.events.back().event_id;
    }
    candidate->last_input = canonical;
    candidate->last_output = result;
    Ogre14ParticleCapturedFrame caller_result = result;

    if (impl_->configuration.fault_injector != nullptr &&
        impl_->configuration.fault_injector->ShouldFail(
            Ogre14ParticleCaptureFaultPoint::BEFORE_COMMIT)) {
      return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                     "particle_capture.injected_failure",
                     "fault injected before particle transaction commit");
    }

    impl_.swap(candidate);
    captured = std::move(caller_result);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "particle_capture.allocation",
                   "particle candidate allocation failed before commit");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "particle_capture.exception",
                   "particle candidate construction failed before commit");
  }
}

std::uint64_t
Ogre14ParticleCaptureSource::last_source_sequence() const noexcept {
  return impl_->last_source_sequence_value;
}

std::uint64_t Ogre14ParticleCaptureSource::highest_event_id() const noexcept {
  return impl_->highest_event_id_value;
}

std::uint64_t Ogre14ParticleCaptureSource::highest_system_id() const noexcept {
  return impl_->highest_system_id;
}

std::size_t Ogre14ParticleCaptureSource::known_system_count() const noexcept {
  return impl_->systems.size();
}

std::size_t Ogre14ParticleCaptureSource::live_system_count() const noexcept {
  std::size_t live = 0U;
  for (const auto &entry : impl_->systems) {
    if (entry.second.lifecycle != Impl::SystemLifecycle::DESTROYED) {
      ++live;
    }
  }
  return live;
}

std::uint64_t
Ogre14ParticleCaptureSource::lifetime_particle_count() const noexcept {
  return impl_->lifetime_particle_count_value;
}

std::uint64_t
Ogre14ParticleCaptureSource::lifetime_event_count() const noexcept {
  return impl_->lifetime_event_count;
}

} // namespace RoR::Render
