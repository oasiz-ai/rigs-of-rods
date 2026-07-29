/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RoR {

/// Cross-renderer scene budget for terrain point and spot lights. This caps
/// scene-query and forward-lighting work; individual materials may expose a
/// smaller per-pass light count. Sparse scenes retain their legacy behavior.
constexpr std::size_t LOCAL_LIGHT_ACTIVE_BUDGET = 64u;

/// Stable prefix consumed by native scene gates.
constexpr const char* LOCAL_LIGHT_BUDGET_LOG_MARKER =
    "[RoR|TerrainObject|LocalLightBudget]";

struct LocalLightPosition
{
    LocalLightPosition(
        double x_value = 0.0,
        double y_value = 0.0,
        double z_value = 0.0)
        : x(x_value)
        , y(y_value)
        , z(z_value)
    {
    }

    double x;
    double y;
    double z;
};

struct LocalLightCandidate
{
    LocalLightCandidate(
        const LocalLightPosition& position_value = LocalLightPosition(),
        std::uint64_t stable_id_value = 0)
        : position(position_value)
        , stable_id(stable_id_value)
    {
    }

    LocalLightPosition position;
    std::uint64_t stable_id;
};

/// Caller-owned scratch record. Keeping this type public lets the runtime
/// reuse one allocation while the selector remains independent of OGRE.
struct LocalLightRank
{
    double squared_distance;
    std::uint64_t stable_id;
    std::size_t candidate_index;
};

inline bool IsFiniteLocalLightValue(double value)
{
    // RoR Release builds enable fast-math. Inspect IEEE-754 bits instead of
    // std::isfinite so hostile map coordinates still fail closed when a
    // compiler assumes floating-point expressions are finite.
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "local-light selection requires IEEE-754 binary64");
    // Read through volatile bytes before reconstructing the native integer
    // representation. Some fast-math optimizers otherwise fold an inlined
    // bit test back into an always-true floating-point finiteness assumption.
    unsigned char representation[sizeof(value)] = {};
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        representation[index] = source[index];
    }
    std::uint64_t bits = 0;
    std::memcpy(&bits, representation, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

inline bool IsFiniteLocalLightPosition(const LocalLightPosition& position)
{
    return IsFiniteLocalLightValue(position.x) &&
        IsFiniteLocalLightValue(position.y) &&
        IsFiniteLocalLightValue(position.z);
}

inline bool LocalLightRankLess(
    const LocalLightRank& lhs,
    const LocalLightRank& rhs)
{
    if (lhs.squared_distance != rhs.squared_distance)
    {
        return lhs.squared_distance < rhs.squared_distance;
    }
    if (lhs.stable_id != rhs.stable_id)
    {
        return lhs.stable_id < rhs.stable_id;
    }
    return lhs.candidate_index < rhs.candidate_index;
}

/// Selects the nearest valid candidates without allocating.
///
/// The output array is fully overwritten. Non-finite camera coordinates hide
/// every light. Non-finite candidates, arithmetic overflow, and malformed
/// coordinates are excluded. Equal-distance candidates use their immutable
/// registration ID and then input index as a total, deterministic tie-break.
inline std::size_t SelectLocalLights(
    const LocalLightCandidate* candidates,
    std::size_t candidate_count,
    const LocalLightPosition& camera_position,
    std::size_t active_budget,
    std::uint8_t* selected,
    LocalLightRank* scratch)
{
    if (candidate_count == 0)
    {
        return 0;
    }
    if (selected == nullptr)
    {
        return 0;
    }

    std::fill(selected, selected + candidate_count, std::uint8_t{0});
    if (candidates == nullptr ||
        scratch == nullptr ||
        active_budget == 0 ||
        !IsFiniteLocalLightPosition(camera_position))
    {
        return 0;
    }

    std::size_t valid_count = 0;
    for (std::size_t index = 0; index < candidate_count; ++index)
    {
        const LocalLightPosition& position = candidates[index].position;
        if (!IsFiniteLocalLightPosition(position))
        {
            continue;
        }

        const double dx = position.x - camera_position.x;
        const double dy = position.y - camera_position.y;
        const double dz = position.z - camera_position.z;
        const double squared_distance = dx * dx + dy * dy + dz * dz;
        if (!IsFiniteLocalLightValue(dx) ||
            !IsFiniteLocalLightValue(dy) ||
            !IsFiniteLocalLightValue(dz) ||
            !IsFiniteLocalLightValue(squared_distance))
        {
            continue;
        }

        LocalLightRank& rank = scratch[valid_count++];
        rank.squared_distance = squared_distance;
        rank.stable_id = candidates[index].stable_id;
        rank.candidate_index = index;
    }

    if (valid_count <= active_budget)
    {
        for (std::size_t index = 0; index < valid_count; ++index)
        {
            selected[scratch[index].candidate_index] = 1;
        }
        return valid_count;
    }

    std::sort(scratch, scratch + valid_count, LocalLightRankLess);
    for (std::size_t index = 0; index < active_budget; ++index)
    {
        selected[scratch[index].candidate_index] = 1;
    }
    return active_budget;
}

} // namespace RoR
