/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Manifest-independent platform provenance for world-model captures.

#pragma once

#include <cstdint>
#include <string>

namespace RoR {
namespace WorldModel {

struct WindowsRuntimeVersion
{
    std::uint32_t major_version = 0U;
    std::uint32_t minor_version = 0U;
    std::uint32_t build_number = 0U;
    std::uint16_t processor_architecture = 0U;
};

/// Builds the canonical Windows provenance identifier from an already
/// authenticated runtime version. Kept platform-neutral for regression tests.
bool BuildWindowsPlatformIdentifier(
    const WindowsRuntimeVersion& version,
    std::string& platform_id,
    std::string* error = nullptr);

/// Inspects the running OS. On Windows this resolves RtlGetVersion from ntdll,
/// avoiding the application-manifest virtualization applied by GetVersionEx.
bool InspectRuntimePlatformIdentifier(
    std::string& platform_id,
    std::string* error = nullptr);

} // namespace WorldModel
} // namespace RoR
