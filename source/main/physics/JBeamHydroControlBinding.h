/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file JBeamHydroControlBinding.h
/// @brief Fail-closed BeamNG electrics to RoR fixed-step control identity.

#pragma once

#include "JBeamHydroRuntime.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace RoR {

static const std::uint32_t JBEAM_HYDRO_CONTROL_BINDING_SCHEMA_VERSION = 1U;
static const std::uint32_t
    JBEAM_HYDRO_SOURCE_ELECTRICS_STEERING_INPUT = 1U;
static const std::uint32_t
    JBEAM_HYDRO_RUNTIME_INPUT_REGISTRY_SCHEMA_VERSION = 1U;
static const std::uint32_t
    JBEAM_HYDRO_RUNTIME_CONTROL_STEERING_COMMAND = 1U;

/// Immutable identity for the only BeamNG hydro control route currently
/// admitted. The numeric runtime fields are cross-checked against
/// DeterministicVehicleInput at the production call site.
struct JBeamHydroControlBinding
{
    std::uint32_t schema_version =
        JBEAM_HYDRO_CONTROL_BINDING_SCHEMA_VERSION;
    std::uint32_t source_electrics =
        JBEAM_HYDRO_SOURCE_ELECTRICS_STEERING_INPUT;
    std::uint32_t runtime_registry_schema_version =
        JBEAM_HYDRO_RUNTIME_INPUT_REGISTRY_SCHEMA_VERSION;
    std::uint32_t runtime_control_id =
        JBEAM_HYDRO_RUNTIME_CONTROL_STEERING_COMMAND;
};

struct JBeamHydroAppliedControlSample
{
    std::uint32_t registry_schema_version = 0U;
    std::uint64_t actor_instance_id = 0U;
    std::uint32_t control_id = 0U;
    float value = 0.0f;
};

enum class JBeamHydroControlBindingError
{
    NONE,
    INVALID_BINDING,
    INVALID_RUNTIME_CONFIG,
    INVALID_ACTOR_TARGET,
    REGISTRY_SCHEMA_MISMATCH,
    CONTROL_ID_MISMATCH,
    INVALID_VALUE
};

struct JBeamHydroControlBindingStatus
{
    JBeamHydroControlBindingError error =
        JBeamHydroControlBindingError::NONE;
};

inline const char* JBeamHydroControlBindingManifest()
{
    return
        "ror-jbeam-hydro-control-binding\n"
        "schema=1\n"
        "source_system=beamng-electrics\n"
        "source_name=steering_input\n"
        "source_hydros_docs=https://documentation.beamng.com/modding/vehicle/"
            "sections/hydros/\n"
        "source_electrics_docs=https://documentation.beamng.com/modding/"
            "vehicle/sections/electrics/\n"
        "runtime_registry=ror-deterministic-vehicle-input\n"
        "runtime_registry_schema=1\n"
        "runtime_control_id=1\n"
        "runtime_control_name=steering_command\n"
        "sampling=fixed-step-start-applied-control\n";
}

inline bool IsValidJBeamHydroControlBinding(
    const JBeamHydroControlBinding& binding,
    const JBeamHydroRuntimeConfig& config)
{
    return binding.schema_version ==
            JBEAM_HYDRO_CONTROL_BINDING_SCHEMA_VERSION &&
        binding.source_electrics ==
            JBEAM_HYDRO_SOURCE_ELECTRICS_STEERING_INPUT &&
        binding.runtime_registry_schema_version ==
            JBEAM_HYDRO_RUNTIME_INPUT_REGISTRY_SCHEMA_VERSION &&
        binding.runtime_control_id ==
            JBEAM_HYDRO_RUNTIME_CONTROL_STEERING_COMMAND &&
        config.input_route == JBeamHydroInputRoute::STEERING_INPUT;
}

namespace JBeamHydroControlBindingDetail {

inline bool IsFiniteBinary32(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t),
        "JBeam hydro control binding requires binary32 float storage");
    static_assert(std::numeric_limits<float>::is_iec559,
        "JBeam hydro control binding requires IEC 60559 floats");
    std::uint32_t bits = 0U;
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    unsigned char* destination =
        reinterpret_cast<unsigned char*>(&bits);
    for (std::size_t index = 0U; index < sizeof(bits); ++index)
        destination[index] = source[index];
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

} // namespace JBeamHydroControlBindingDetail

/// Resolves one already-applied fixed-step control sample. Failure is
/// transactional: `input` is unchanged and the exact rejected identity is
/// available through `status`.
inline bool ResolveJBeamHydroControlInput(
    const JBeamHydroControlBinding& binding,
    const JBeamHydroRuntimeConfig& config,
    std::uint64_t expected_actor_instance_id,
    const JBeamHydroAppliedControlSample& sample,
    double& input,
    JBeamHydroControlBindingStatus& status)
{
    JBeamHydroControlBindingStatus candidate_status;
    if (!IsValidJBeamHydroControlBinding(binding, config))
    {
        candidate_status.error =
            JBeamHydroControlBindingError::INVALID_BINDING;
    }
    else if (!JBeamHydroRuntimeDetail::IsValidConfig(config))
    {
        candidate_status.error =
            JBeamHydroControlBindingError::INVALID_RUNTIME_CONFIG;
    }
    else if (expected_actor_instance_id == 0U ||
        sample.actor_instance_id != expected_actor_instance_id)
    {
        candidate_status.error =
            JBeamHydroControlBindingError::INVALID_ACTOR_TARGET;
    }
    else if (sample.registry_schema_version !=
        binding.runtime_registry_schema_version)
    {
        candidate_status.error =
            JBeamHydroControlBindingError::REGISTRY_SCHEMA_MISMATCH;
    }
    else if (sample.control_id != binding.runtime_control_id)
    {
        candidate_status.error =
            JBeamHydroControlBindingError::CONTROL_ID_MISMATCH;
    }
    else if (!JBeamHydroControlBindingDetail::IsFiniteBinary32(
            sample.value) ||
        sample.value < -1.0f || sample.value > 1.0f)
    {
        candidate_status.error =
            JBeamHydroControlBindingError::INVALID_VALUE;
    }
    else
    {
        input = static_cast<double>(sample.value);
        status = candidate_status;
        return true;
    }
    status = candidate_status;
    return false;
}

inline const char* JBeamHydroControlBindingErrorToString(
    JBeamHydroControlBindingError error)
{
    switch (error)
    {
    case JBeamHydroControlBindingError::NONE:
        return "none";
    case JBeamHydroControlBindingError::INVALID_BINDING:
        return "invalid-binding";
    case JBeamHydroControlBindingError::INVALID_RUNTIME_CONFIG:
        return "invalid-runtime-config";
    case JBeamHydroControlBindingError::INVALID_ACTOR_TARGET:
        return "invalid-actor-target";
    case JBeamHydroControlBindingError::REGISTRY_SCHEMA_MISMATCH:
        return "registry-schema-mismatch";
    case JBeamHydroControlBindingError::CONTROL_ID_MISMATCH:
        return "control-id-mismatch";
    case JBeamHydroControlBindingError::INVALID_VALUE:
        return "invalid-value";
    }
    return "unknown";
}

} // namespace RoR
