/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file JBeamHydroRuntime.h
/// @brief Deterministic, fault-latching runtime state for admitted hydros.

#pragma once

#include "HydroActuatorResponse.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace RoR {

enum class JBeamHydroInputRoute
{
    STEERING_INPUT
};

enum class JBeamHydroRuntimeFault
{
    NONE,
    INVALID_CONFIG,
    INVALID_INITIAL_LENGTH,
    INVALID_PREVIOUS_STATE,
    INVALID_INPUT,
    INVALID_TIMESTEP,
    STEP_REJECTED,
    REST_LENGTH_REJECTED,
    FLOAT_NARROWING,
    STEP_COUNTER_EXHAUSTED
};

struct JBeamHydroRuntimeConfig
{
    HydroActuatorConfig response;
    JBeamHydroInputRoute input_route =
        JBeamHydroInputRoute::STEERING_INPUT;
    bool has_steering_wheel_lock = false;
    double steering_wheel_lock = 0.0;
};

struct JBeamHydroRuntimeState
{
    HydroActuatorState response;
    std::uint64_t accepted_step_count = 0U;
    bool fault_latched = false;
    JBeamHydroRuntimeFault fault = JBeamHydroRuntimeFault::NONE;
};

struct JBeamHydroRuntimeStep
{
    JBeamHydroRuntimeState state;
    double rest_length = 0.0;
    float runtime_rest_length = 0.0f;
    double target_ratio = 1.0;
    bool input_was_clamped = false;
    bool valid = false;
};

namespace JBeamHydroRuntimeDetail {

inline bool IsNormalBinary32(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t),
        "JBeam hydro runtime requires a binary32 float");
    static_assert(std::numeric_limits<float>::is_iec559,
        "JBeam hydro runtime requires IEC 60559 floats");
    std::uint32_t bits = 0U;
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    unsigned char* destination =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t i = 0U; i < sizeof(bits); ++i)
    {
        destination[i] = source[i];
    }
    const std::uint32_t exponent = bits & UINT32_C(0x7f800000);
    return exponent != 0U && exponent != UINT32_C(0x7f800000);
}

inline bool TryRuntimeRestLength(double value, float& output)
{
    output = 0.0f;
    if (!HydroActuatorDetail::IsFinite(value) ||
        !(value > 0.0) ||
        value > static_cast<double>(std::numeric_limits<float>::max()))
    {
        return false;
    }
    const volatile float narrowed = static_cast<float>(value);
    output = narrowed;
    return output > 0.0f && IsNormalBinary32(output);
}

inline bool IsValidConfig(const JBeamHydroRuntimeConfig& config)
{
    if (!HydroActuatorDetail::IsValidConfig(config.response) ||
        config.input_route != JBeamHydroInputRoute::STEERING_INPUT)
    {
        return false;
    }
    return !config.has_steering_wheel_lock ||
        (HydroActuatorDetail::IsFinite(config.steering_wheel_lock) &&
         config.steering_wheel_lock > 0.0);
}

inline JBeamHydroRuntimeStep Reject(
    const JBeamHydroRuntimeState& previous,
    JBeamHydroRuntimeFault fault)
{
    JBeamHydroRuntimeStep result;
    result.state = previous;
    result.state.fault_latched = true;
    result.state.fault = fault;
    return result;
}

} // namespace JBeamHydroRuntimeDetail

/// Creates the exact ratio-one state and proves that its initial rest length
/// can be handed to the binary32 beam solver. No source or input authority is
/// implied by this function.
inline JBeamHydroRuntimeStep InitializeJBeamHydroRuntime(
    const JBeamHydroRuntimeConfig& config,
    double initial_length)
{
    JBeamHydroRuntimeState empty;
    if (!JBeamHydroRuntimeDetail::IsValidConfig(config))
    {
        return JBeamHydroRuntimeDetail::Reject(
            empty, JBeamHydroRuntimeFault::INVALID_CONFIG);
    }
    if (!HydroActuatorDetail::IsFinite(initial_length) ||
        !(initial_length > std::numeric_limits<double>::min()))
    {
        return JBeamHydroRuntimeDetail::Reject(
            empty, JBeamHydroRuntimeFault::INVALID_INITIAL_LENGTH);
    }

    JBeamHydroRuntimeStep result;
    result.state = empty;
    if (!ResolveHydroRestLength(
            initial_length, result.state.response, &result.rest_length))
    {
        return JBeamHydroRuntimeDetail::Reject(
            empty, JBeamHydroRuntimeFault::REST_LENGTH_REJECTED);
    }
    if (!JBeamHydroRuntimeDetail::TryRuntimeRestLength(
            result.rest_length, result.runtime_rest_length))
    {
        return JBeamHydroRuntimeDetail::Reject(
            empty, JBeamHydroRuntimeFault::FLOAT_NARROWING);
    }
    result.valid = true;
    return result;
}

/// Advances one fixed simulation step. Rejections return an invalid result
/// whose copied state has a permanent fault latch; callers must publish that
/// state rather than silently retaining the previous healthy receipt.
inline JBeamHydroRuntimeStep AdvanceJBeamHydroRuntime(
    const JBeamHydroRuntimeConfig& config,
    const JBeamHydroRuntimeState& previous,
    double initial_length,
    double input,
    double dt,
    bool auto_center)
{
    if (previous.fault_latched)
    {
        JBeamHydroRuntimeStep result;
        result.state = previous;
        return result;
    }
    if (!JBeamHydroRuntimeDetail::IsValidConfig(config))
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::INVALID_CONFIG);
    }
    if (!HydroActuatorDetail::IsFinite(initial_length) ||
        !(initial_length > std::numeric_limits<double>::min()))
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::INVALID_INITIAL_LENGTH);
    }
    if (!HydroActuatorDetail::IsFinite(previous.response.length_ratio) ||
        !(previous.response.length_ratio > 0.0))
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::INVALID_PREVIOUS_STATE);
    }
    if (!HydroActuatorDetail::IsFinite(input))
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::INVALID_INPUT);
    }
    if (!HydroActuatorDetail::IsFinite(dt) || !(dt > 0.0))
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::INVALID_TIMESTEP);
    }
    if (previous.accepted_step_count ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::STEP_COUNTER_EXHAUSTED);
    }

    const HydroActuatorStep advanced = AdvanceHydroActuator(
        config.response,
        previous.response,
        input,
        dt,
        auto_center);
    if (!advanced.valid)
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::STEP_REJECTED);
    }

    JBeamHydroRuntimeStep result;
    result.state = previous;
    result.state.response = advanced.state;
    ++result.state.accepted_step_count;
    if (!ResolveHydroRestLength(
            initial_length, result.state.response, &result.rest_length))
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::REST_LENGTH_REJECTED);
    }
    if (!JBeamHydroRuntimeDetail::TryRuntimeRestLength(
            result.rest_length, result.runtime_rest_length))
    {
        return JBeamHydroRuntimeDetail::Reject(
            previous, JBeamHydroRuntimeFault::FLOAT_NARROWING);
    }
    result.target_ratio = advanced.target_ratio;
    result.input_was_clamped = advanced.input_was_clamped;
    result.valid = true;
    return result;
}

inline const char* JBeamHydroRuntimeFaultToString(
    JBeamHydroRuntimeFault fault)
{
    switch (fault)
    {
    case JBeamHydroRuntimeFault::NONE:
        return "none";
    case JBeamHydroRuntimeFault::INVALID_CONFIG:
        return "invalid-config";
    case JBeamHydroRuntimeFault::INVALID_INITIAL_LENGTH:
        return "invalid-initial-length";
    case JBeamHydroRuntimeFault::INVALID_PREVIOUS_STATE:
        return "invalid-previous-state";
    case JBeamHydroRuntimeFault::INVALID_INPUT:
        return "invalid-input";
    case JBeamHydroRuntimeFault::INVALID_TIMESTEP:
        return "invalid-timestep";
    case JBeamHydroRuntimeFault::STEP_REJECTED:
        return "step-rejected";
    case JBeamHydroRuntimeFault::REST_LENGTH_REJECTED:
        return "rest-length-rejected";
    case JBeamHydroRuntimeFault::FLOAT_NARROWING:
        return "float-narrowing";
    case JBeamHydroRuntimeFault::STEP_COUNTER_EXHAUSTED:
        return "step-counter-exhausted";
    }
    return "unknown";
}

} // namespace RoR
