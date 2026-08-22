/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Exact native JBeam hydro runtime to state-digest adapter.

#pragma once

#include "DeterministicStateDigest.h"
#include "JBeamHydroRuntime.h"

#include <cstdint>
#include <cstring>

namespace RoR {
namespace DeterministicStateDigest {
namespace JBeamHydroStateDigest {
namespace Detail {

inline bool MapInputRoute(
    JBeamHydroInputRoute source,
    std::uint32_t& destination)
{
    switch (source)
    {
    case JBeamHydroInputRoute::STEERING_INPUT:
        destination = HYDRO_INPUT_ROUTE_STEERING;
        return true;
    }
    return false;
}

inline bool MapFault(
    JBeamHydroRuntimeFault source,
    std::uint32_t& destination)
{
    switch (source)
    {
    case JBeamHydroRuntimeFault::NONE:
        destination = HYDRO_RUNTIME_FAULT_NONE;
        return true;
    case JBeamHydroRuntimeFault::INVALID_CONFIG:
        destination = HYDRO_RUNTIME_FAULT_INVALID_CONFIG;
        return true;
    case JBeamHydroRuntimeFault::INVALID_INITIAL_LENGTH:
        destination = HYDRO_RUNTIME_FAULT_INVALID_INITIAL_LENGTH;
        return true;
    case JBeamHydroRuntimeFault::INVALID_PREVIOUS_STATE:
        destination = HYDRO_RUNTIME_FAULT_INVALID_PREVIOUS_STATE;
        return true;
    case JBeamHydroRuntimeFault::INVALID_INPUT:
        destination = HYDRO_RUNTIME_FAULT_INVALID_INPUT;
        return true;
    case JBeamHydroRuntimeFault::INVALID_TIMESTEP:
        destination = HYDRO_RUNTIME_FAULT_INVALID_TIMESTEP;
        return true;
    case JBeamHydroRuntimeFault::STEP_REJECTED:
        destination = HYDRO_RUNTIME_FAULT_STEP_REJECTED;
        return true;
    case JBeamHydroRuntimeFault::REST_LENGTH_REJECTED:
        destination = HYDRO_RUNTIME_FAULT_REST_LENGTH_REJECTED;
        return true;
    case JBeamHydroRuntimeFault::FLOAT_NARROWING:
        destination = HYDRO_RUNTIME_FAULT_FLOAT_NARROWING;
        return true;
    case JBeamHydroRuntimeFault::STEP_COUNTER_EXHAUSTED:
        destination = HYDRO_RUNTIME_FAULT_STEP_COUNTER_EXHAUSTED;
        return true;
    }
    return false;
}

inline bool ExactFloatEqual(float first, float second)
{
    std::uint32_t first_bits = 0U;
    std::uint32_t second_bits = 0U;
    std::memcpy(&first_bits, &first, sizeof(first_bits));
    std::memcpy(&second_bits, &second, sizeof(second_bits));
    return first_bits == second_bits;
}

} // namespace Detail

/// Populates all canonical configuration and continuation fields while
/// preserving IDs already assigned by the caller. Invalid or inconsistent
/// source state leaves the destination unchanged.
inline bool Populate(
    const JBeamHydroRuntimeConfig& config,
    const JBeamHydroRuntimeState& state,
    float reference_length,
    float runtime_rest_length,
    HydroRecord& destination)
{
    if (!JBeamHydroRuntimeDetail::IsValidConfig(config) ||
        !JBeamHydroRuntimeDetail::IsNormalBinary32(reference_length) ||
        !JBeamHydroRuntimeDetail::IsNormalBinary32(runtime_rest_length) ||
        !HydroActuatorDetail::IsFinite(state.response.length_ratio) ||
        !(state.response.length_ratio > 0.0) ||
        (state.fault_latched !=
         (state.fault != JBeamHydroRuntimeFault::NONE)))
    {
        return false;
    }

    double resolved_rest_length = 0.0;
    if (!ResolveHydroRestLength(
            static_cast<double>(reference_length),
            state.response,
            &resolved_rest_length))
    {
        return false;
    }
    float resolved_runtime_rest_length = 0.0f;
    if (!JBeamHydroRuntimeDetail::TryRuntimeRestLength(
            resolved_rest_length,
            resolved_runtime_rest_length) ||
        !Detail::ExactFloatEqual(
            resolved_runtime_rest_length,
            runtime_rest_length))
    {
        return false;
    }

    HydroRecord candidate = destination;
    candidate.runtime_schema_version =
        HYDRO_RUNTIME_SCHEMA_JBEAM_V1;
    if (!Detail::MapInputRoute(
            config.input_route,
            candidate.input_route) ||
        !Detail::MapFault(state.fault, candidate.fault))
    {
        return false;
    }
    candidate.flags = 0U;
    if (config.response.has_factor)
        candidate.flags |= HYDRO_FLAG_HAS_FACTOR;
    if (config.has_steering_wheel_lock)
        candidate.flags |= HYDRO_FLAG_HAS_STEERING_WHEEL_LOCK;
    if (state.fault_latched)
        candidate.flags |= HYDRO_FLAG_FAULT_LATCHED;
    candidate.reference_length = reference_length;
    candidate.runtime_rest_length = runtime_rest_length;
    candidate.factor = config.response.factor;
    candidate.in_limit = config.response.in_limit;
    candidate.out_limit = config.response.out_limit;
    candidate.input_factor = config.response.input_factor;
    candidate.input_center = config.response.input_center;
    candidate.input_in_limit = config.response.input_in_limit;
    candidate.input_out_limit = config.response.input_out_limit;
    candidate.in_rate = config.response.in_rate;
    candidate.out_rate = config.response.out_rate;
    candidate.auto_center_rate = config.response.auto_center_rate;
    candidate.steering_wheel_lock = config.steering_wheel_lock;
    candidate.length_ratio = state.response.length_ratio;
    candidate.accepted_step_count = state.accepted_step_count;
    destination = candidate;
    return true;
}

} // namespace JBeamHydroStateDigest
} // namespace DeterministicStateDigest
} // namespace RoR
