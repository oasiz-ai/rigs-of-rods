/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "SkyModeAvailability.h"

#include <cstdlib>
#include <iostream>

namespace
{

int Fail(const char* message)
{
    std::cerr << "sky mode availability test failed: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using RoR::ResolveAvailableSkyMode;
    using RoR::SkyModeAvailability;
    using RoR::SkyModeBackend;

    const SkyModeAvailability all_available = {true, true};
    if (ResolveAvailableSkyMode(
            SkyModeBackend::SANDSTORM, all_available) !=
            SkyModeBackend::SANDSTORM ||
        ResolveAvailableSkyMode(
            SkyModeBackend::CAELUM, all_available) !=
            SkyModeBackend::CAELUM ||
        ResolveAvailableSkyMode(
            SkyModeBackend::SKYX, all_available) !=
            SkyModeBackend::SKYX)
    {
        return Fail("available backends were not preserved");
    }

    const SkyModeAvailability no_caelum = {false, true};
    if (ResolveAvailableSkyMode(
            SkyModeBackend::CAELUM, no_caelum) !=
            SkyModeBackend::SANDSTORM ||
        ResolveAvailableSkyMode(
            SkyModeBackend::SKYX, no_caelum) !=
            SkyModeBackend::SKYX)
    {
        return Fail("missing Caelum did not fall back to Sandstorm");
    }

    const SkyModeAvailability no_skyx = {true, false};
    if (ResolveAvailableSkyMode(
            SkyModeBackend::SKYX, no_skyx) !=
            SkyModeBackend::SANDSTORM ||
        ResolveAvailableSkyMode(
            SkyModeBackend::CAELUM, no_skyx) !=
            SkyModeBackend::CAELUM)
    {
        return Fail("missing SkyX did not fall back to Sandstorm");
    }

    const SkyModeAvailability basic_only = {false, false};
    if (ResolveAvailableSkyMode(
            SkyModeBackend::CAELUM, basic_only) !=
            SkyModeBackend::SANDSTORM ||
        ResolveAvailableSkyMode(
            SkyModeBackend::SKYX, basic_only) !=
            SkyModeBackend::SANDSTORM)
    {
        return Fail("optional backends did not fail safe");
    }

    std::cout << "cross-platform sky backend fallback verified\n";
    return EXIT_SUCCESS;
}
