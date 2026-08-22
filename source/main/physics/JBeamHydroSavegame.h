/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file JBeamHydroSavegame.h
/// @brief Atomic savegame validation for native JBeam hydro history.

#pragma once

#include "JBeamHydroRuntime.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace RoR {
namespace JBeamHydroSavegame {

static const std::uint32_t PAYLOAD_SCHEMA_VERSION = 1U;

/// Spawned identity plus the rest length already decoded from the ordinary
/// version-3 beam array. The latter prevents two savegame members from
/// publishing conflicting solver state.
struct LiveHydro
{
    std::uint32_t hydro_index = 0U;
    std::uint16_t beam_index = 0U;
    bool enabled = false;
    float reference_length = 0.0f;
    float saved_runtime_rest_length = 0.0f;
    JBeamHydroRuntimeConfig config;
};

struct HydroRecord
{
    std::uint32_t hydro_index = 0U;
    std::uint16_t beam_index = 0U;
    float reference_length = 0.0f;
    JBeamHydroRuntimeConfig config;
    JBeamHydroRuntimeState state;
};

struct ActorPayload
{
    std::uint32_t schema_version = PAYLOAD_SCHEMA_VERSION;
    std::uint32_t hydro_count = 0U;
    std::vector<HydroRecord> records;
};

struct StagedHydro
{
    std::uint32_t hydro_index = 0U;
    std::uint16_t beam_index = 0U;
    JBeamHydroRuntimeState state;
    float runtime_rest_length = 0.0f;
};

enum class Error
{
    NONE,
    UNSUPPORTED_SCHEMA,
    HYDRO_COUNT_MISMATCH,
    RECORD_COUNT_MISMATCH,
    RECORD_ORDER,
    HYDRO_IDENTITY_MISMATCH,
    ENABLEMENT_MISMATCH,
    CONFIGURATION_MISMATCH,
    INVALID_CONFIGURATION,
    REFERENCE_LENGTH_MISMATCH,
    INVALID_STATE,
    INVALID_FAULT,
    REST_LENGTH_REJECTED,
    FLOAT_NARROWING,
    SAVED_REST_LENGTH_MISMATCH
};

struct Result
{
    Error error = Error::NONE;
    std::uint32_t hydro_index =
        std::numeric_limits<std::uint32_t>::max();

    bool IsValid() const { return error == Error::NONE; }
};

inline bool ExactDoubleEqual(double first, double second)
{
    std::uint64_t first_bits = 0U;
    std::uint64_t second_bits = 0U;
    std::memcpy(&first_bits, &first, sizeof(first_bits));
    std::memcpy(&second_bits, &second, sizeof(second_bits));
    return first_bits == second_bits;
}

inline bool ExactFloatEqual(float first, float second)
{
    std::uint32_t first_bits = 0U;
    std::uint32_t second_bits = 0U;
    std::memcpy(&first_bits, &first, sizeof(first_bits));
    std::memcpy(&second_bits, &second, sizeof(second_bits));
    return first_bits == second_bits;
}

inline bool SameResponseConfiguration(
    const HydroActuatorConfig& first,
    const HydroActuatorConfig& second)
{
    return first.has_factor == second.has_factor &&
        ExactDoubleEqual(first.factor, second.factor) &&
        ExactDoubleEqual(first.in_limit, second.in_limit) &&
        ExactDoubleEqual(first.out_limit, second.out_limit) &&
        ExactDoubleEqual(first.input_factor, second.input_factor) &&
        ExactDoubleEqual(first.input_center, second.input_center) &&
        ExactDoubleEqual(first.input_in_limit, second.input_in_limit) &&
        ExactDoubleEqual(first.input_out_limit, second.input_out_limit) &&
        ExactDoubleEqual(first.in_rate, second.in_rate) &&
        ExactDoubleEqual(first.out_rate, second.out_rate) &&
        ExactDoubleEqual(
            first.auto_center_rate, second.auto_center_rate);
}

inline bool SameConfiguration(
    const JBeamHydroRuntimeConfig& first,
    const JBeamHydroRuntimeConfig& second)
{
    return SameResponseConfiguration(first.response, second.response) &&
        first.input_route == second.input_route &&
        first.has_steering_wheel_lock ==
            second.has_steering_wheel_lock &&
        ExactDoubleEqual(
            first.steering_wheel_lock, second.steering_wheel_lock);
}

inline bool IsKnownFault(JBeamHydroRuntimeFault fault)
{
    switch (fault)
    {
    case JBeamHydroRuntimeFault::NONE:
    case JBeamHydroRuntimeFault::INVALID_CONFIG:
    case JBeamHydroRuntimeFault::INVALID_INITIAL_LENGTH:
    case JBeamHydroRuntimeFault::INVALID_PREVIOUS_STATE:
    case JBeamHydroRuntimeFault::INVALID_INPUT:
    case JBeamHydroRuntimeFault::INVALID_TIMESTEP:
    case JBeamHydroRuntimeFault::STEP_REJECTED:
    case JBeamHydroRuntimeFault::REST_LENGTH_REJECTED:
    case JBeamHydroRuntimeFault::FLOAT_NARROWING:
    case JBeamHydroRuntimeFault::STEP_COUNTER_EXHAUSTED:
        return true;
    }
    return false;
}

inline Result Failure(Error error, std::uint32_t hydro_index)
{
    Result result;
    result.error = error;
    result.hydro_index = hydro_index;
    return result;
}

/// Validates the entire actor payload into temporary storage. Failure leaves
/// `staged` unchanged so a malformed late record cannot partially restore an
/// actor. The caller may assign every staged state only after success.
inline Result TryStage(
    const ActorPayload& payload,
    const std::vector<LiveHydro>& live_hydros,
    std::vector<StagedHydro>& staged)
{
    if (payload.schema_version != PAYLOAD_SCHEMA_VERSION)
        return Failure(Error::UNSUPPORTED_SCHEMA, 0U);
    if (payload.hydro_count != live_hydros.size())
        return Failure(Error::HYDRO_COUNT_MISMATCH, 0U);

    std::size_t enabled_count = 0U;
    for (std::size_t index = 0U; index < live_hydros.size(); ++index)
    {
        const LiveHydro& live = live_hydros[index];
        if (live.hydro_index != index)
        {
            return Failure(
                Error::HYDRO_IDENTITY_MISMATCH,
                static_cast<std::uint32_t>(index));
        }
        if (!live.enabled)
            continue;
        ++enabled_count;
        if (!JBeamHydroRuntimeDetail::IsValidConfig(live.config))
        {
            return Failure(
                Error::INVALID_CONFIGURATION, live.hydro_index);
        }
        if (!JBeamHydroRuntimeDetail::IsNormalBinary32(
                live.reference_length) ||
            !JBeamHydroRuntimeDetail::IsNormalBinary32(
                live.saved_runtime_rest_length))
        {
            return Failure(
                Error::REFERENCE_LENGTH_MISMATCH, live.hydro_index);
        }
    }
    if (payload.records.size() != enabled_count)
        return Failure(Error::RECORD_COUNT_MISMATCH, 0U);

    std::vector<StagedHydro> candidate;
    candidate.reserve(payload.records.size());
    bool have_previous = false;
    std::uint32_t previous_index = 0U;
    for (std::size_t record_index = 0U;
         record_index < payload.records.size();
         ++record_index)
    {
        const HydroRecord& record = payload.records[record_index];
        if (have_previous && record.hydro_index <= previous_index)
            return Failure(Error::RECORD_ORDER, record.hydro_index);
        previous_index = record.hydro_index;
        have_previous = true;

        if (record.hydro_index >= live_hydros.size())
        {
            return Failure(
                Error::HYDRO_IDENTITY_MISMATCH, record.hydro_index);
        }
        const LiveHydro& live = live_hydros[record.hydro_index];
        if (record.hydro_index != live.hydro_index ||
            record.beam_index != live.beam_index)
        {
            return Failure(
                Error::HYDRO_IDENTITY_MISMATCH, record.hydro_index);
        }
        if (!live.enabled)
            return Failure(Error::ENABLEMENT_MISMATCH, record.hydro_index);
        if (!SameConfiguration(record.config, live.config))
        {
            return Failure(
                Error::CONFIGURATION_MISMATCH, record.hydro_index);
        }
        if (!JBeamHydroRuntimeDetail::IsValidConfig(record.config))
        {
            return Failure(
                Error::INVALID_CONFIGURATION, record.hydro_index);
        }
        if (!ExactFloatEqual(
                record.reference_length, live.reference_length) ||
            !JBeamHydroRuntimeDetail::IsNormalBinary32(
                record.reference_length))
        {
            return Failure(
                Error::REFERENCE_LENGTH_MISMATCH, record.hydro_index);
        }
        if (!HydroActuatorDetail::IsFinite(
                record.state.response.length_ratio) ||
            !(record.state.response.length_ratio > 0.0))
        {
            return Failure(Error::INVALID_STATE, record.hydro_index);
        }
        if (!IsKnownFault(record.state.fault) ||
            (record.state.fault_latched &&
                record.state.fault == JBeamHydroRuntimeFault::NONE) ||
            (!record.state.fault_latched &&
                record.state.fault != JBeamHydroRuntimeFault::NONE))
        {
            return Failure(Error::INVALID_FAULT, record.hydro_index);
        }

        double resolved_rest_length = 0.0;
        if (!ResolveHydroRestLength(
                static_cast<double>(record.reference_length),
                record.state.response,
                &resolved_rest_length))
        {
            return Failure(
                Error::REST_LENGTH_REJECTED, record.hydro_index);
        }
        float runtime_rest_length = 0.0f;
        if (!JBeamHydroRuntimeDetail::TryRuntimeRestLength(
                resolved_rest_length, runtime_rest_length))
        {
            return Failure(Error::FLOAT_NARROWING, record.hydro_index);
        }
        if (!ExactFloatEqual(
                runtime_rest_length, live.saved_runtime_rest_length))
        {
            return Failure(
                Error::SAVED_REST_LENGTH_MISMATCH,
                record.hydro_index);
        }

        StagedHydro restored;
        restored.hydro_index = record.hydro_index;
        restored.beam_index = record.beam_index;
        restored.state = record.state;
        restored.runtime_rest_length = runtime_rest_length;
        candidate.push_back(restored);
    }

    std::size_t candidate_index = 0U;
    for (std::size_t live_index = 0U;
         live_index < live_hydros.size();
         ++live_index)
    {
        if (!live_hydros[live_index].enabled)
            continue;
        if (candidate_index >= candidate.size() ||
            candidate[candidate_index].hydro_index != live_index)
        {
            return Failure(
                Error::ENABLEMENT_MISMATCH,
                static_cast<std::uint32_t>(live_index));
        }
        ++candidate_index;
    }

    staged.swap(candidate);
    return Result();
}

} // namespace JBeamHydroSavegame
} // namespace RoR
