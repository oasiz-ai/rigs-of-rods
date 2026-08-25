/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include <cstdint>

namespace RoR::Render {

constexpr std::uint32_t kOgreNextDefaultMaximumSceneWorkers = 4U;
constexpr std::uint32_t kOgreNextOverrideMaximumSceneWorkers = 8U;

struct OgreNextSceneWorkerSelection {
  std::uint32_t hardware_threads = 0U;
  std::uint32_t requested_worker_threads = 1U;
  bool override_present = false;
  bool override_valid = false;
};

/// Resolves the Ogre-Next scene-worker count without consulting global state.
///
/// The normal policy uses the reported hardware concurrency, bounded to the
/// four-worker lifetime path qualified by the full-runtime ThreadSanitizer
/// soak. ROR_OGRE_NEXT_SCENE_WORKERS is deliberately strict: only one ASCII
/// digit in [1, 8] is accepted. Invalid values fall back to the hardware-bounded
/// default and remain observable in the startup receipt.
inline OgreNextSceneWorkerSelection ResolveOgreNextSceneWorkerSelection(
    std::uint32_t hardware_threads, const char *override_value) noexcept {
  OgreNextSceneWorkerSelection selection;
  selection.hardware_threads = hardware_threads;
  selection.requested_worker_threads =
      hardware_threads == 0U
          ? 1U
          : (hardware_threads > kOgreNextDefaultMaximumSceneWorkers
                 ? kOgreNextDefaultMaximumSceneWorkers
                 : hardware_threads);

  selection.override_present = override_value != nullptr;
  if (override_value != nullptr && override_value[0] >= '1' &&
      override_value[0] <= '8' && override_value[1] == '\0') {
    selection.requested_worker_threads =
        static_cast<std::uint32_t>(override_value[0] - '0');
    selection.override_valid = true;
  }
  return selection;
}

} // namespace RoR::Render
