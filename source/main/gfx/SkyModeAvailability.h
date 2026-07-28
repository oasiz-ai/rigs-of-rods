/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Renderer-independent selection of an available sky implementation.

#pragma once

namespace RoR
{

enum class SkyModeBackend
{
    SANDSTORM,
    CAELUM,
    SKYX,
};

struct SkyModeAvailability
{
    bool caelum;
    bool skyx;
};

inline bool IsSkyModeBackendAvailable(
    SkyModeBackend mode,
    const SkyModeAvailability& availability)
{
    switch (mode)
    {
    case SkyModeBackend::SANDSTORM:
        return true;
    case SkyModeBackend::CAELUM:
        return availability.caelum;
    case SkyModeBackend::SKYX:
        return availability.skyx;
    }
    return false;
}

/// Sandstorm is the dependency-free fallback on every supported platform.
/// Keeping this policy outside the renderer lets config parsing, UI, resource
/// loading, terrain initialization, and tests agree on the same effective
/// backend even when an optional dependency was not compiled.
inline SkyModeBackend ResolveAvailableSkyMode(
    SkyModeBackend requested,
    const SkyModeAvailability& availability)
{
    return IsSkyModeBackendAvailable(requested, availability)
        ? requested
        : SkyModeBackend::SANDSTORM;
}

} // namespace RoR
