/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free validation for deterministic impact initial velocity.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RoR {
namespace DeterministicImpactInitialCondition {

static const std::uint32_t SCHEMA_VERSION = 1U;
static const double MAXIMUM_SPEED_METERS_PER_SECOND = 100.0;
static const std::uint32_t PLACEMENT_SCHEMA_VERSION = 1U;
static const double MAXIMUM_TRANSLATION_METERS = 100.0;
static const double MAXIMUM_ABSOLUTE_WORLD_POSITION_METERS = 1000000.0;

enum class Error
{
    NONE,
    UNSUPPORTED_SCHEMA,
    NONFINITE_VELOCITY,
    ZERO_SPEED,
    SPEED_OUT_OF_RANGE,
    NUMERIC_OVERFLOW
};

struct Request
{
    std::uint32_t schema_version = SCHEMA_VERSION;
    std::array<double, 3> velocity_meters_per_second = {{0.0, 0.0, 0.0}};
};

struct Result
{
    Error error = Error::NONE;
    double speed_squared_meters2_per_second2 = 0.0;

    bool IsValid() const { return error == Error::NONE; }
};

enum class PlacementError
{
    NONE,
    UNSUPPORTED_SCHEMA,
    NONFINITE_TRANSLATION,
    TRANSLATION_OUT_OF_RANGE,
    NUMERIC_OVERFLOW
};

struct PlacementRequest
{
    std::uint32_t schema_version = PLACEMENT_SCHEMA_VERSION;
    std::array<double, 3> translation_offset_meters = {{0.0, 0.0, 0.0}};
};

struct PlacementResult
{
    PlacementError error = PlacementError::NONE;
    double translation_squared_meters2 = 0.0;

    bool IsValid() const { return error == PlacementError::NONE; }
};

inline bool IsFinite(double value)
{
    static_assert(
        sizeof(double) == sizeof(std::uint64_t),
        "64-bit IEEE-754 double required");
    static_assert(
        std::numeric_limits<double>::is_iec559,
        "IEEE-754 double required");
    std::uint64_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    const volatile std::uint64_t observed = bits;
    return (observed & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

inline Result Validate(const Request& request)
{
    Result result;
    if (request.schema_version != SCHEMA_VERSION)
    {
        result.error = Error::UNSUPPORTED_SCHEMA;
        return result;
    }

    const double x = request.velocity_meters_per_second[0];
    const double y = request.velocity_meters_per_second[1];
    const double z = request.velocity_meters_per_second[2];
    if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z))
    {
        result.error = Error::NONFINITE_VELOCITY;
        return result;
    }

    const double x_squared = x * x;
    const double y_squared = y * y;
    const double z_squared = z * z;
    const double speed_squared = x_squared + y_squared + z_squared;
    if (!IsFinite(x_squared) || !IsFinite(y_squared) ||
        !IsFinite(z_squared) || !IsFinite(speed_squared))
    {
        result.error = Error::NUMERIC_OVERFLOW;
        return result;
    }
    if (speed_squared <= 0.0)
    {
        result.error = Error::ZERO_SPEED;
        return result;
    }

    const double maximum_speed_squared =
        MAXIMUM_SPEED_METERS_PER_SECOND *
        MAXIMUM_SPEED_METERS_PER_SECOND;
    if (speed_squared > maximum_speed_squared)
    {
        result.error = Error::SPEED_OUT_OF_RANGE;
        return result;
    }

    result.speed_squared_meters2_per_second2 = speed_squared;
    return result;
}

inline PlacementResult ValidatePlacement(const PlacementRequest& request)
{
    PlacementResult result;
    if (request.schema_version != PLACEMENT_SCHEMA_VERSION)
    {
        result.error = PlacementError::UNSUPPORTED_SCHEMA;
        return result;
    }

    const double x = request.translation_offset_meters[0];
    const double y = request.translation_offset_meters[1];
    const double z = request.translation_offset_meters[2];
    if (!IsFinite(x) || !IsFinite(y) || !IsFinite(z))
    {
        result.error = PlacementError::NONFINITE_TRANSLATION;
        return result;
    }

    const double x_squared = x * x;
    const double y_squared = y * y;
    const double z_squared = z * z;
    const double translation_squared =
        x_squared + y_squared + z_squared;
    if (!IsFinite(x_squared) || !IsFinite(y_squared) ||
        !IsFinite(z_squared) || !IsFinite(translation_squared))
    {
        result.error = PlacementError::NUMERIC_OVERFLOW;
        return result;
    }

    const double maximum_translation_squared =
        MAXIMUM_TRANSLATION_METERS * MAXIMUM_TRANSLATION_METERS;
    if (translation_squared > maximum_translation_squared)
    {
        result.error = PlacementError::TRANSLATION_OUT_OF_RANGE;
        return result;
    }

    result.translation_squared_meters2 = translation_squared;
    return result;
}

} // namespace DeterministicImpactInitialCondition
} // namespace RoR
