/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Exact calibrated-beam runtime to deterministic-digest adapter.

#pragma once

#include "CalibratedBeamMaterialAdapter.h"
#include "DeterministicStateDigest.h"

namespace RoR {
namespace DeterministicStateDigest {
namespace CalibratedBeamStateDigest {
namespace Detail {

inline bool MapRuntimeError(
    CalibratedBeamMaterialAdapter::Error source,
    std::uint32_t& destination)
{
    using CalibratedBeamMaterialAdapter::Error;
    switch (source)
    {
    case Error::NONE:
        destination = BEAM_MATERIAL_RUNTIME_ERROR_NONE;
        return true;
    case Error::UNSUPPORTED_ADAPTER_SCHEMA:
        destination =
            BEAM_MATERIAL_RUNTIME_ERROR_UNSUPPORTED_ADAPTER_SCHEMA;
        return true;
    case Error::UNSUPPORTED_BEAM_ROLE:
        destination = BEAM_MATERIAL_RUNTIME_ERROR_UNSUPPORTED_BEAM_ROLE;
        return true;
    case Error::NONFINITE_INPUT:
        destination = BEAM_MATERIAL_RUNTIME_ERROR_NONFINITE_INPUT;
        return true;
    case Error::INVALID_CROSS_SECTION_AREA:
        destination =
            BEAM_MATERIAL_RUNTIME_ERROR_INVALID_CROSS_SECTION_AREA;
        return true;
    case Error::INVALID_REFERENCE_LENGTH:
        destination =
            BEAM_MATERIAL_RUNTIME_ERROR_INVALID_REFERENCE_LENGTH;
        return true;
    case Error::INVALID_CURRENT_LENGTH:
        destination = BEAM_MATERIAL_RUNTIME_ERROR_INVALID_CURRENT_LENGTH;
        return true;
    case Error::INVALID_DIRECTION:
        destination = BEAM_MATERIAL_RUNTIME_ERROR_INVALID_DIRECTION;
        return true;
    case Error::MATERIAL_FAILURE:
        destination = BEAM_MATERIAL_RUNTIME_ERROR_MATERIAL_FAILURE;
        return true;
    case Error::NUMERIC_OVERFLOW:
        destination = BEAM_MATERIAL_RUNTIME_ERROR_NUMERIC_OVERFLOW;
        return true;
    case Error::FORCE_OUT_OF_RUNTIME_RANGE:
        destination =
            BEAM_MATERIAL_RUNTIME_ERROR_FORCE_OUT_OF_RUNTIME_RANGE;
        return true;
    case Error::DISABLED:
    case Error::FAULT_LATCHED:
        return false;
    }
    return false;
}

inline bool MapMaterialError(
    CalibratedBeamMaterial::Error source,
    std::uint32_t& destination)
{
    using CalibratedBeamMaterial::Error;
    switch (source)
    {
    case Error::NONE:
        destination = BEAM_MATERIAL_ERROR_NONE;
        return true;
    case Error::UNSUPPORTED_SCHEMA:
        destination = BEAM_MATERIAL_ERROR_UNSUPPORTED_SCHEMA;
        return true;
    case Error::NONFINITE_INPUT:
        destination = BEAM_MATERIAL_ERROR_NONFINITE_INPUT;
        return true;
    case Error::INVALID_ELASTIC_MODULUS:
        destination = BEAM_MATERIAL_ERROR_INVALID_ELASTIC_MODULUS;
        return true;
    case Error::INVALID_YIELD_STRESS:
        destination = BEAM_MATERIAL_ERROR_INVALID_YIELD_STRESS;
        return true;
    case Error::INVALID_HARDENING_MODULUS:
        destination = BEAM_MATERIAL_ERROR_INVALID_HARDENING_MODULUS;
        return true;
    case Error::INVALID_DAMAGE_ONSET:
        destination = BEAM_MATERIAL_ERROR_INVALID_DAMAGE_ONSET;
        return true;
    case Error::INVALID_DAMAGE_DRIVER_CAPACITY:
        destination =
            BEAM_MATERIAL_ERROR_INVALID_DAMAGE_DRIVER_CAPACITY;
        return true;
    case Error::INVALID_STATE:
        destination = BEAM_MATERIAL_ERROR_INVALID_STATE;
        return true;
    case Error::NUMERIC_OVERFLOW:
        destination = BEAM_MATERIAL_ERROR_NUMERIC_OVERFLOW;
        return true;
    }
    return false;
}

} // namespace Detail

/// Adds the calibrated fracture/fault receipt to an already populated beam.
/// The update is transactional: invalid or internally inconsistent runtime
/// state leaves `beam` byte-for-byte semantically unchanged.
inline bool Populate(
    const CalibratedBeamMaterialAdapter::Runtime& runtime,
    bool beam_disabled,
    bool beam_broken,
    BeamRecord& beam)
{
    const bool has_calibrated_schema =
        beam.material_schema_version ==
        BEAM_MATERIAL_SCHEMA_CALIBRATED_V1;
    if (beam.material_schema_version != BEAM_MATERIAL_SCHEMA_NONE &&
        !has_calibrated_schema)
    {
        return false;
    }
    if (runtime.enabled != has_calibrated_schema)
        return false;
    if (((beam.state_flags & BEAM_STATE_DISABLED) != 0) !=
            beam_disabled ||
        ((beam.state_flags & BEAM_STATE_BROKEN) != 0) != beam_broken)
    {
        return false;
    }

    std::uint32_t runtime_error = BEAM_MATERIAL_RUNTIME_ERROR_NONE;
    std::uint32_t material_error = BEAM_MATERIAL_ERROR_NONE;
    std::uint32_t state_flags =
        beam.state_flags &
        ~(BEAM_STATE_MATERIAL_FRACTURED |
          BEAM_STATE_MATERIAL_FAULTED);

    if (!runtime.enabled)
    {
        if (runtime.faulted ||
            runtime.last_error !=
                CalibratedBeamMaterialAdapter::Error::NONE ||
            runtime.last_material_error !=
                CalibratedBeamMaterial::Error::NONE ||
            runtime.state.fractured)
        {
            return false;
        }
    }
    else if (runtime.faulted)
    {
        if (!beam_disabled || runtime.state.fractured ||
            !Detail::MapRuntimeError(runtime.last_error, runtime_error) ||
            !Detail::MapMaterialError(
                runtime.last_material_error,
                material_error) ||
            runtime_error == BEAM_MATERIAL_RUNTIME_ERROR_NONE)
        {
            return false;
        }
        const bool is_material_failure =
            runtime_error == BEAM_MATERIAL_RUNTIME_ERROR_MATERIAL_FAILURE;
        const bool has_material_error =
            material_error != BEAM_MATERIAL_ERROR_NONE;
        if (is_material_failure != has_material_error)
            return false;
        state_flags |= BEAM_STATE_MATERIAL_FAULTED;
    }
    else
    {
        if (runtime.last_error !=
                CalibratedBeamMaterialAdapter::Error::NONE ||
            runtime.last_material_error !=
                CalibratedBeamMaterial::Error::NONE)
        {
            return false;
        }
        if (runtime.state.fractured)
        {
            if (!beam_disabled || !beam_broken)
                return false;
            state_flags |= BEAM_STATE_MATERIAL_FRACTURED;
        }
    }

    beam.material_runtime_error = runtime_error;
    beam.material_error = material_error;
    beam.state_flags = state_flags;
    return true;
}

} // namespace CalibratedBeamStateDigest
} // namespace DeterministicStateDigest
} // namespace RoR
