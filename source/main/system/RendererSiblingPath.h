/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Exact canonical sibling resolution shared by renderer launch modes.

#pragma once

#include "RendererChildIntent.h"

#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererSiblingPathContractVersion = 1U;

enum class RendererSiblingPathStatus : std::uint8_t {
  READY = 0U,
  REJECTED_INVALID_BASENAME,
  FAILED_CURRENT_EXECUTABLE_PATH,
  FAILED_CHILD_PATH,
  FAILED_INTERNAL,
};

struct RendererSiblingPathResult final {
  std::uint32_t version = kRendererSiblingPathContractVersion;
  RendererSiblingPathStatus status =
      RendererSiblingPathStatus::REJECTED_INVALID_BASENAME;
  RendererChildLauncherString path;
  std::uint32_t native_error_code = 0U;
  bool accepted = false;
};

/// Resolve one ASCII basename relative to the final opened executable rather
/// than cwd, PATH, environment, or an alias directory. This function does not
/// open or execute the sibling and deliberately accepts no directory override.
RendererSiblingPathResult ResolveRendererSiblingPath(
    const char *basename) noexcept;

bool IsKnownRendererSiblingPathStatus(
    RendererSiblingPathStatus status) noexcept;
const char *ToString(RendererSiblingPathStatus status) noexcept;

} // namespace RoR
