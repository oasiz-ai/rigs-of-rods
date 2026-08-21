/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Stable scenario/actor stream identity for deterministic simulation.

#pragma once

#include "DeterministicCounterNoise.h"

#include <cstdint>

namespace RoR {
namespace DeterministicScenarioIdentity {

static const std::uint32_t SCHEMA_VERSION = 1U;

enum class Error
{
    NONE = 0,
    PARTIAL_EXPLICIT_IDENTITY
};

struct Resolution
{
    Error error = Error::NONE;
    bool explicit_identity = false;
    std::uint64_t scenario_seed = 0U;
    std::uint64_t actor_stream_id = 0U;
    std::uint64_t deterministic_seed = 0U;
};

/// Resolve the actor's counter-noise seed without mutable/global state.
///
/// An all-zero authored pair selects the legacy actor-ID mapping. An explicit
/// identity is valid only when both values are nonzero. This makes partially
/// transported scenario metadata fail closed instead of silently falling back
/// to an allocation-order-dependent seed.
inline Resolution Resolve(
    std::uint64_t scenario_seed,
    std::uint64_t actor_stream_id,
    std::uint64_t legacy_actor_id)
{
    Resolution result;
    if (scenario_seed == 0U && actor_stream_id == 0U)
    {
        result.deterministic_seed =
            DeterministicCounterNoise::MakeActorSeed(legacy_actor_id);
        return result;
    }
    if (scenario_seed == 0U || actor_stream_id == 0U)
    {
        result.error = Error::PARTIAL_EXPLICIT_IDENTITY;
        return result;
    }

    result.explicit_identity = true;
    result.scenario_seed = scenario_seed;
    result.actor_stream_id = actor_stream_id;
    result.deterministic_seed =
        DeterministicCounterNoise::MakeActorSeed(
            actor_stream_id,
            scenario_seed);
    return result;
}

inline bool IsValid(const Resolution& resolution)
{
    return resolution.error == Error::NONE;
}

inline bool MatchesExplicitIdentity(
    const Resolution& left,
    const Resolution& right)
{
    return IsValid(left) && IsValid(right) &&
        left.explicit_identity && right.explicit_identity &&
        left.scenario_seed == right.scenario_seed &&
        left.actor_stream_id == right.actor_stream_id;
}

inline bool RevalidatesStoredSeed(
    std::uint64_t scenario_seed,
    std::uint64_t actor_stream_id,
    std::uint64_t stored_deterministic_seed)
{
    const Resolution resolution =
        Resolve(scenario_seed, actor_stream_id, 0U);
    return IsValid(resolution) && resolution.explicit_identity &&
        resolution.deterministic_seed == stored_deterministic_seed;
}

} // namespace DeterministicScenarioIdentity
} // namespace RoR
