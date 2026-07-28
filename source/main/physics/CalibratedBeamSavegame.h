/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Atomic savegame validation for calibrated per-beam material state.

#pragma once

#include "CalibratedBeamMaterialAdapter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace RoR {
namespace CalibratedBeamSavegame {

static const std::uint32_t PAYLOAD_SCHEMA_VERSION = 1;

/// Identity and saved legacy flags for one live actor beam.
///
/// The JSON bridge obtains the identity from the spawned actor and the flags
/// from the existing version-3 `beams` array. Keeping those inputs separate
/// makes it possible to validate the complete material payload before either
/// the legacy beam state or material history is assigned.
struct LiveBeam
{
    std::uint32_t beam_index = 0;
    std::int32_t node_1 = -1;
    std::int32_t node_2 = -1;
    std::int32_t beam_type = 0;
    std::int32_t special_beam = 0;
    bool is_plain_axial_beam = false;
    bool saved_disabled = false;
    bool saved_broken = false;
    CalibratedBeamMaterialAdapter::Runtime authored_runtime;
};

struct BeamRecord
{
    std::uint32_t beam_index = 0;
    std::int32_t node_1 = -1;
    std::int32_t node_2 = -1;
    std::int32_t beam_type = 0;
    std::int32_t special_beam = 0;
    bool disabled = false;
    bool broken = false;
    CalibratedBeamMaterialAdapter::Runtime runtime;
};

struct ActorPayload
{
    std::uint32_t schema_version = PAYLOAD_SCHEMA_VERSION;
    std::uint32_t beam_count = 0;
    std::vector<BeamRecord> records;
};

struct StagedBeam
{
    std::uint32_t beam_index = 0;
    CalibratedBeamMaterialAdapter::Runtime runtime;
};

enum class Error
{
    NONE,
    MALFORMED_PAYLOAD,
    UNSUPPORTED_SCHEMA,
    BEAM_COUNT_MISMATCH,
    RECORD_COUNT_MISMATCH,
    RECORD_ORDER,
    BEAM_IDENTITY_MISMATCH,
    ENABLEMENT_MISMATCH,
    UNSUPPORTED_BEAM_ROLE,
    CONFIGURATION_MISMATCH,
    INVALID_CONFIGURATION,
    INVALID_HISTORY,
    INVALID_ERROR_CODE,
    INVALID_FAULT_STATE,
    INVALID_FRACTURE_STATE,
    FLAG_MISMATCH
};

struct Result
{
    Error error = Error::NONE;
    std::uint32_t beam_index = std::numeric_limits<std::uint32_t>::max();

    bool IsValid() const { return error == Error::NONE; }
};

inline bool ExactDoubleEqual(double first, double second)
{
    std::uint64_t first_bits = 0;
    std::uint64_t second_bits = 0;
    std::memcpy(&first_bits, &first, sizeof(first_bits));
    std::memcpy(&second_bits, &second, sizeof(second_bits));
    return first_bits == second_bits;
}

inline bool SameConfiguration(
    const CalibratedBeamMaterialAdapter::Configuration& first,
    const CalibratedBeamMaterialAdapter::Configuration& second)
{
    return
        first.schema_version == second.schema_version &&
        ExactDoubleEqual(
            first.cross_section_area_m2,
            second.cross_section_area_m2) &&
        first.material.schema_version ==
            second.material.schema_version &&
        ExactDoubleEqual(
            first.material.elastic_modulus,
            second.material.elastic_modulus) &&
        ExactDoubleEqual(
            first.material.yield_stress,
            second.material.yield_stress) &&
        ExactDoubleEqual(
            first.material.hardening_modulus,
            second.material.hardening_modulus) &&
        ExactDoubleEqual(
            first.material.damage_onset_plastic_strain,
            second.material.damage_onset_plastic_strain) &&
        ExactDoubleEqual(
            first.material.damage_driver_capacity_density,
            second.material.damage_driver_capacity_density);
}

inline bool IsKnownAdapterError(
    CalibratedBeamMaterialAdapter::Error error)
{
    using CalibratedBeamMaterialAdapter::Error;
    switch (error)
    {
    case Error::NONE:
    case Error::DISABLED:
    case Error::FAULT_LATCHED:
    case Error::UNSUPPORTED_ADAPTER_SCHEMA:
    case Error::UNSUPPORTED_BEAM_ROLE:
    case Error::NONFINITE_INPUT:
    case Error::INVALID_CROSS_SECTION_AREA:
    case Error::INVALID_REFERENCE_LENGTH:
    case Error::INVALID_CURRENT_LENGTH:
    case Error::INVALID_DIRECTION:
    case Error::MATERIAL_FAILURE:
    case Error::NUMERIC_OVERFLOW:
    case Error::FORCE_OUT_OF_RUNTIME_RANGE:
        return true;
    }
    return false;
}

inline bool IsKnownMaterialError(CalibratedBeamMaterial::Error error)
{
    using CalibratedBeamMaterial::Error;
    switch (error)
    {
    case Error::NONE:
    case Error::UNSUPPORTED_SCHEMA:
    case Error::NONFINITE_INPUT:
    case Error::INVALID_ELASTIC_MODULUS:
    case Error::INVALID_YIELD_STRESS:
    case Error::INVALID_HARDENING_MODULUS:
    case Error::INVALID_DAMAGE_ONSET:
    case Error::INVALID_DAMAGE_DRIVER_CAPACITY:
    case Error::INVALID_STATE:
    case Error::NUMERIC_OVERFLOW:
        return true;
    }
    return false;
}

inline Result Failure(Error error, std::uint32_t beam_index)
{
    Result result;
    result.error = error;
    result.beam_index = beam_index;
    return result;
}

/// Validates every record into temporary storage, then swaps it into `staged`.
///
/// Failure leaves `staged` unchanged. Callers can therefore apply every
/// runtime only after this function succeeds; a malformed late record cannot
/// partially restore earlier beams.
inline Result TryStage(
    const ActorPayload& payload,
    const std::vector<LiveBeam>& live_beams,
    std::vector<StagedBeam>& staged)
{
    if (payload.schema_version != PAYLOAD_SCHEMA_VERSION)
        return Failure(Error::UNSUPPORTED_SCHEMA, 0);
    if (payload.beam_count != live_beams.size())
        return Failure(Error::BEAM_COUNT_MISMATCH, 0);

    std::size_t enabled_count = 0;
    for (std::size_t index = 0; index < live_beams.size(); ++index)
    {
        if (live_beams[index].beam_index != index)
        {
            return Failure(
                Error::BEAM_IDENTITY_MISMATCH,
                static_cast<std::uint32_t>(index));
        }
        if (live_beams[index].authored_runtime.enabled)
            ++enabled_count;
    }
    if (payload.records.size() != enabled_count)
        return Failure(Error::RECORD_COUNT_MISMATCH, 0);

    std::vector<StagedBeam> candidate;
    candidate.reserve(payload.records.size());
    std::uint32_t previous_index = 0;
    bool have_previous = false;
    for (std::size_t record_index = 0;
         record_index < payload.records.size();
         ++record_index)
    {
        const BeamRecord& record = payload.records[record_index];
        if (have_previous && record.beam_index <= previous_index)
            return Failure(Error::RECORD_ORDER, record.beam_index);
        previous_index = record.beam_index;
        have_previous = true;

        if (record.beam_index >= live_beams.size())
            return Failure(Error::BEAM_IDENTITY_MISMATCH, record.beam_index);
        const LiveBeam& live = live_beams[record.beam_index];
        if (record.beam_index != live.beam_index ||
            record.node_1 != live.node_1 ||
            record.node_2 != live.node_2 ||
            record.beam_type != live.beam_type ||
            record.special_beam != live.special_beam)
        {
            return Failure(Error::BEAM_IDENTITY_MISMATCH, record.beam_index);
        }
        if (!live.authored_runtime.enabled || !record.runtime.enabled)
            return Failure(Error::ENABLEMENT_MISMATCH, record.beam_index);
        if (!live.is_plain_axial_beam)
            return Failure(Error::UNSUPPORTED_BEAM_ROLE, record.beam_index);
        if (!SameConfiguration(
                live.authored_runtime.configuration,
                record.runtime.configuration))
        {
            return Failure(Error::CONFIGURATION_MISMATCH, record.beam_index);
        }

        CalibratedBeamMaterial::Error material_error =
            CalibratedBeamMaterial::Error::NONE;
        if (CalibratedBeamMaterialAdapter::ValidateConfiguration(
                record.runtime.configuration,
                &material_error) !=
            CalibratedBeamMaterialAdapter::Error::NONE)
        {
            return Failure(Error::INVALID_CONFIGURATION, record.beam_index);
        }
        if (!CalibratedBeamMaterial::IsValidState(
                record.runtime.state,
                record.runtime.configuration.material))
        {
            return Failure(Error::INVALID_HISTORY, record.beam_index);
        }
        if (!IsKnownAdapterError(record.runtime.last_error) ||
            !IsKnownMaterialError(
                record.runtime.last_material_error))
        {
            return Failure(Error::INVALID_ERROR_CODE, record.beam_index);
        }

        if (record.runtime.faulted)
        {
            if (record.runtime.last_error ==
                    CalibratedBeamMaterialAdapter::Error::NONE ||
                record.runtime.last_error ==
                    CalibratedBeamMaterialAdapter::Error::DISABLED ||
                record.runtime.last_error ==
                    CalibratedBeamMaterialAdapter::Error::FAULT_LATCHED ||
                !record.disabled)
            {
                return Failure(Error::INVALID_FAULT_STATE, record.beam_index);
            }
            const bool is_material_failure =
                record.runtime.last_error ==
                CalibratedBeamMaterialAdapter::Error::MATERIAL_FAILURE;
            const bool has_material_error =
                record.runtime.last_material_error !=
                CalibratedBeamMaterial::Error::NONE;
            if (is_material_failure != has_material_error)
            {
                return Failure(Error::INVALID_FAULT_STATE, record.beam_index);
            }
        }
        else if (record.runtime.last_error !=
                     CalibratedBeamMaterialAdapter::Error::NONE ||
                 record.runtime.last_material_error !=
                     CalibratedBeamMaterial::Error::NONE)
        {
            return Failure(Error::INVALID_FAULT_STATE, record.beam_index);
        }

        if (record.runtime.state.fractured &&
            (!record.disabled || !record.broken ||
                record.runtime.faulted))
        {
            return Failure(Error::INVALID_FRACTURE_STATE, record.beam_index);
        }
        if (record.disabled != live.saved_disabled ||
            record.broken != live.saved_broken)
        {
            return Failure(Error::FLAG_MISMATCH, record.beam_index);
        }

        StagedBeam restored;
        restored.beam_index = record.beam_index;
        restored.runtime = record.runtime;
        candidate.push_back(restored);
    }

    // Every authored opt-in must have exactly one sorted record. Record count
    // equality alone is insufficient when an attacker replaces one index with
    // a different valid index.
    std::size_t candidate_index = 0;
    for (std::size_t live_index = 0;
         live_index < live_beams.size();
         ++live_index)
    {
        if (!live_beams[live_index].authored_runtime.enabled)
            continue;
        if (candidate_index >= candidate.size() ||
            candidate[candidate_index].beam_index != live_index)
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

} // namespace CalibratedBeamSavegame
} // namespace RoR
