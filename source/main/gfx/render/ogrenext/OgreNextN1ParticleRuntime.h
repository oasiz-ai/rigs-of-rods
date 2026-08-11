/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Transactional persistent state for N1 continuous particles.

#pragma once

#include "Ogre14ParticleCaptureSource.h"
#include "RenderAssetRegistry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace RoR::Render {

/// Full-rect UVs in N1 quad order (left-top, left-bottom, right-bottom,
/// right-top), matching OGRE 14 BBR_TEXCOORD rotation for tracks/Dust.
[[nodiscard]] std::array<Float2, 4U>
BuildOgre14ParticleTextureCoordinateQuad(float rotation_radians) noexcept;

struct OgreNextN1ParticleRuntimeAudit final {
  std::uint32_t version = 1U;
  std::uint64_t prepared_source_sequence = 0U;
  std::uint64_t committed_source_sequence = 0U;
  std::uint64_t create_commands = 0U;
  std::uint64_t update_commands = 0U;
  std::uint64_t stop_commands = 0U;
  std::uint64_t destroy_commands = 0U;
  std::uint64_t live_systems = 0U;
  std::uint64_t live_particles = 0U;
  std::uint64_t lifetime_max_live_systems = 0U;
  std::uint64_t lifetime_max_live_particles = 0U;
  std::uint64_t source_backed_textures = 0U;
  std::uint64_t source_alpha_textures = 0U;
  std::uint64_t lifetime_max_source_backed_textures = 0U;
  std::uint64_t lifetime_max_source_alpha_textures = 0U;
  std::uint64_t gpu_readbacks = 0U;
  std::uint64_t native_batch_creates = 0U;
  std::uint64_t native_batch_destroys = 0U;
  std::uint64_t native_particles_submitted = 0U;
  std::uint64_t native_state_readbacks = 0U;
  std::uint64_t native_state_verifications = 0U;
};

class OgreNextN1ParticleRuntime final {
public:
  OgreNextN1ParticleRuntime() = default;

  [[nodiscard]] ValidationResult Prepare(
      std::uint64_t frame_id,
      const std::shared_ptr<const Ogre14ParticleCapturedFrame> &frame,
      const RenderAssetRegistry &registry, std::uint64_t simulation_tick,
      const Double3 &absolute_world_origin_meters);
  [[nodiscard]] const std::vector<
      std::shared_ptr<const Ogre14CapturedParticleSystem>> &
  prepared_systems() const noexcept;
  [[nodiscard]] bool CanCommit(std::uint64_t frame_id) const noexcept;
  [[nodiscard]] bool Commit(std::uint64_t frame_id) noexcept;
  void Abort(std::uint64_t frame_id) noexcept;
  void Reset() noexcept;
  [[nodiscard]] OgreNextN1ParticleRuntimeAudit audit() const noexcept;

private:
  struct Record final {
    std::shared_ptr<const Ogre14CapturedParticleSystem> system;
    bool stopped = false;
  };

  std::map<std::uint64_t, Record> committed_;
  std::map<std::uint64_t, Record> prepared_;
  std::set<std::uint64_t> committed_tombstones_;
  std::set<std::uint64_t> prepared_tombstones_;
  std::vector<std::shared_ptr<const Ogre14CapturedParticleSystem>>
      prepared_systems_;
  OgreNextN1ParticleRuntimeAudit audit_;
  OgreNextN1ParticleRuntimeAudit prepared_audit_;
  std::uint64_t prepared_frame_id_ = 0U;
  std::uint64_t highest_event_id_ = 0U;
  std::uint64_t prepared_highest_event_id_ = 0U;
  std::uint64_t highest_system_id_ = 0U;
  std::uint64_t prepared_highest_system_id_ = 0U;
  bool has_prepared_ = false;
};

} // namespace RoR::Render
