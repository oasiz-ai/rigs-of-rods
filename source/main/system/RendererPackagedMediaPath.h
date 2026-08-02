/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Immutable executable-relative Ogre-Next media layout.

#pragma once

#include "RendererBackendPolicy.h"
#include "RendererSiblingPath.h"

#include <cstdint>

namespace RoR {

constexpr std::uint32_t kRendererPackagedMediaPathContractVersion = 1U;

enum class RendererPackagedMediaPathStatus : std::uint8_t {
  READY = 0U,
  REJECTED_INVALID_PLATFORM,
  REJECTED_INVALID_EXECUTABLE_PATH,
  REJECTED_INVALID_MACOS_BUNDLE_LAYOUT,
  FAILED_CURRENT_EXECUTABLE_PATH,
  FAILED_INTERNAL,
};

struct RendererPackagedMediaPathResult final {
  std::uint32_t version = kRendererPackagedMediaPathContractVersion;
  RendererPackagedMediaPathStatus status =
      RendererPackagedMediaPathStatus::REJECTED_INVALID_PLATFORM;
  HostRenderPlatform package_platform = HostRenderPlatform::UNKNOWN;
  RendererChildLauncherString shader_media_root;
  RendererChildLauncherString presentation_media_root;
  std::uint32_t native_error_code = 0U;
  bool accepted = false;
};

/// Pure layout seam used by package-contract tests. The input must already be
/// the canonical final executable path. macOS accepts only the exact
/// `Contents/MacOS/<child>` bundle layout; Windows/Linux use the executable's
/// directory. Every appended component is fixed by this contract.
RendererPackagedMediaPathResult
ResolveRendererPackagedMediaPathFromExecutable(
    HostRenderPlatform package_platform,
    const RendererChildLauncherString &canonical_executable_path) noexcept;

/// Production resolver anchored exclusively to the final running executable.
/// It never reads argv[0], cwd, PATH, an environment variable, or a config
/// file. Directory/manifest existence is a later package-admission check.
RendererPackagedMediaPathResult ResolveRendererPackagedMediaPath(
    HostRenderPlatform package_platform) noexcept;

bool IsKnownRendererPackagedMediaPathStatus(
    RendererPackagedMediaPathStatus status) noexcept;
const char *ToString(RendererPackagedMediaPathStatus status) noexcept;

} // namespace RoR
