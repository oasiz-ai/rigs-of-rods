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

/// Canonical path of the running executable after resolving the platform's
/// executable handle/symlink. This is the only trusted anchor for sibling
/// binaries and immutable packaged media; cwd, PATH, argv[0], and environment
/// variables are deliberately excluded.
struct RendererCurrentExecutablePathResult final {
  std::uint32_t version = kRendererSiblingPathContractVersion;
  RendererSiblingPathStatus status =
      RendererSiblingPathStatus::FAILED_CURRENT_EXECUTABLE_PATH;
  RendererChildLauncherString path;
  std::uint32_t native_error_code = 0U;
  bool accepted = false;
};

struct RendererSiblingPathResult final {
  std::uint32_t version = kRendererSiblingPathContractVersion;
  RendererSiblingPathStatus status =
      RendererSiblingPathStatus::REJECTED_INVALID_BASENAME;
  RendererChildLauncherString path;
  std::uint32_t native_error_code = 0U;
  bool accepted = false;
};

RendererCurrentExecutablePathResult
ResolveRendererCurrentExecutablePath() noexcept;

/// Pure layout seam used by package-admission tests. The input must be the
/// canonical final path of the public launcher. It is never interpreted as an
/// argv, cwd, PATH, or environment override.
RendererSiblingPathResult ResolveRendererSiblingPathFromExecutable(
    const RendererChildLauncherString &canonical_executable_path,
    const char *basename) noexcept;

/// Resolve one ASCII basename relative to the final opened executable rather
/// than cwd, PATH, environment, or an alias directory. This function does not
/// open or execute the sibling and deliberately accepts no directory override.
RendererSiblingPathResult ResolveRendererSiblingPath(
    const char *basename) noexcept;

bool IsKnownRendererSiblingPathStatus(
    RendererSiblingPathStatus status) noexcept;
const char *ToString(RendererSiblingPathStatus status) noexcept;

} // namespace RoR
