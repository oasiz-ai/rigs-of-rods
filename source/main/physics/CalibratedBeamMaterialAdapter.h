/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

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

/// @file
/// @brief SI area/rest-length adapter for calibrated axial beam materials.

#pragma once

#include "CalibratedBeamMaterial.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace RoR {
namespace CalibratedBeamMaterialAdapter {

static const std::uint32_t ADAPTER_SCHEMA_VERSION = 1;

/// A configuration is inert until TryConfigure() validates and enables it.
///
/// The material kernel maps strain to stress. The adapter supplies the missing
/// beam geometry:
///
///     strain = (current_length - reference_length) / reference_length
///     axial material force = -nominal_stress * cross_section_area
///
/// Stress is positive in tension. Actor's beam direction points from endpoint
/// 2 to endpoint 1, hence the minus sign for the force on endpoint 1.
struct Configuration
{
    std::uint32_t schema_version = ADAPTER_SCHEMA_VERSION;
    double cross_section_area_m2 = 0.0;
    CalibratedBeamMaterial::Parameters material;
};

enum class Error
{
    NONE,
    DISABLED,
    FAULT_LATCHED,
    UNSUPPORTED_ADAPTER_SCHEMA,
    UNSUPPORTED_BEAM_ROLE,
    NONFINITE_INPUT,
    INVALID_CROSS_SECTION_AREA,
    INVALID_REFERENCE_LENGTH,
    INVALID_CURRENT_LENGTH,
    INVALID_DIRECTION,
    MATERIAL_FAILURE,
    NUMERIC_OVERFLOW,
    FORCE_OUT_OF_RUNTIME_RANGE
};

/// Per-beam state. Configuration changes reset history by construction.
///
/// A runtime fault is latched so malformed authored data or corrupted history
/// can never fall back to the legacy force law on a later step. ResetHistory()
/// is the explicit recovery boundary used by Actor::reset().
struct Runtime
{
    bool enabled = false;
    bool faulted = false;
    Configuration configuration;
    CalibratedBeamMaterial::State state;
    Error last_error = Error::NONE;
    CalibratedBeamMaterial::Error last_material_error =
        CalibratedBeamMaterial::Error::NONE;
};

struct StepInput
{
    double reference_length_m = 0.0;
    double current_length_m = 0.0;
    double damping_force_n = 0.0;
    std::array<double, 3> direction = {{0.0, 0.0, 0.0}};
    bool is_plain_axial_beam = false;
};

struct ForcePair
{
    std::array<double, 3> endpoint_1 = {{0.0, 0.0, 0.0}};
    std::array<double, 3> endpoint_2 = {{0.0, 0.0, 0.0}};
};

struct StepResult
{
    Error error = Error::NONE;
    CalibratedBeamMaterial::Error material_error =
        CalibratedBeamMaterial::Error::NONE;
    double total_strain = 0.0;
    double nominal_stress_pa = 0.0;
    double material_force_n = 0.0;
    double damping_force_n = 0.0;
    double axial_force_n = 0.0;
    double stored_energy_j = 0.0;
    double dissipated_energy_increment_j = 0.0;
    ForcePair forces;
    bool yielded = false;
    bool fractured = false;
    bool fractured_this_step = false;

    bool IsValid() const { return error == Error::NONE; }
};

inline Error ValidateConfiguration(
    const Configuration& configuration,
    CalibratedBeamMaterial::Error* material_error = nullptr)
{
    if (material_error != nullptr)
        *material_error = CalibratedBeamMaterial::Error::NONE;

    if (configuration.schema_version != ADAPTER_SCHEMA_VERSION)
        return Error::UNSUPPORTED_ADAPTER_SCHEMA;
    if (!CalibratedBeamMaterial::IsFinite(
            configuration.cross_section_area_m2))
    {
        return Error::NONFINITE_INPUT;
    }
    if (configuration.cross_section_area_m2 <= 0.0)
        return Error::INVALID_CROSS_SECTION_AREA;

    const CalibratedBeamMaterial::Error validation =
        CalibratedBeamMaterial::ValidateParameters(
            configuration.material);
    if (validation != CalibratedBeamMaterial::Error::NONE)
    {
        if (material_error != nullptr)
            *material_error = validation;
        return Error::MATERIAL_FAILURE;
    }
    return Error::NONE;
}

/// Atomically enables a validated configuration and starts pristine history.
/// On failure, `runtime` remains byte-for-byte semantically unchanged.
inline bool TryConfigure(
    Runtime& runtime,
    const Configuration& configuration,
    Error* error = nullptr,
    CalibratedBeamMaterial::Error* material_error = nullptr)
{
    CalibratedBeamMaterial::Error validation_material_error =
        CalibratedBeamMaterial::Error::NONE;
    const Error validation_error =
        ValidateConfiguration(
            configuration,
            &validation_material_error);
    if (error != nullptr)
        *error = validation_error;
    if (material_error != nullptr)
        *material_error = validation_material_error;
    if (validation_error != Error::NONE)
        return false;

    Runtime configured;
    configured.enabled = true;
    configured.configuration = configuration;
    runtime = configured;
    return true;
}

inline void Disable(Runtime& runtime)
{
    runtime = Runtime();
}

/// Clears plasticity, damage, fracture, and a latched runtime fault while
/// preserving the explicit opt-in and its validated configuration.
inline void ResetHistory(Runtime& runtime)
{
    runtime.state = CalibratedBeamMaterial::State();
    runtime.faulted = false;
    runtime.last_error = Error::NONE;
    runtime.last_material_error =
        CalibratedBeamMaterial::Error::NONE;
}

inline StepResult LatchFailure(
    Runtime& runtime,
    Error error,
    CalibratedBeamMaterial::Error material_error =
        CalibratedBeamMaterial::Error::NONE)
{
    StepResult result;
    result.error = error;
    result.material_error = material_error;
    runtime.faulted = true;
    runtime.last_error = error;
    runtime.last_material_error = material_error;
    return result;
}

inline bool TryNormalize(
    const std::array<double, 3>& direction,
    std::array<double, 3>& normalized)
{
    if (!CalibratedBeamMaterial::IsFinite(direction[0]) ||
        !CalibratedBeamMaterial::IsFinite(direction[1]) ||
        !CalibratedBeamMaterial::IsFinite(direction[2]))
    {
        return false;
    }

    const double scale = std::max(
        std::abs(direction[0]),
        std::max(
            std::abs(direction[1]),
            std::abs(direction[2])));
    if (!CalibratedBeamMaterial::IsFinite(scale) || scale <= 0.0)
        return false;

    const double scaled_x = direction[0] / scale;
    const double scaled_y = direction[1] / scale;
    const double scaled_z = direction[2] / scale;
    const double scaled_norm_squared =
        scaled_x * scaled_x +
        scaled_y * scaled_y +
        scaled_z * scaled_z;
    if (!CalibratedBeamMaterial::IsFinite(scaled_norm_squared) ||
        scaled_norm_squared <= 0.0)
    {
        return false;
    }

    const double inverse_scaled_norm =
        1.0 / std::sqrt(scaled_norm_squared);
    if (!CalibratedBeamMaterial::IsFinite(inverse_scaled_norm) ||
        inverse_scaled_norm <= 0.0)
    {
        return false;
    }

    normalized[0] = scaled_x * inverse_scaled_norm;
    normalized[1] = scaled_y * inverse_scaled_norm;
    normalized[2] = scaled_z * inverse_scaled_norm;
    return
        CalibratedBeamMaterial::IsFinite(normalized[0]) &&
        CalibratedBeamMaterial::IsFinite(normalized[1]) &&
        CalibratedBeamMaterial::IsFinite(normalized[2]);
}

inline bool IsInRuntimeForceRange(double value)
{
    return
        CalibratedBeamMaterial::IsFinite(value) &&
        std::abs(value) <=
            static_cast<double>(std::numeric_limits<float>::max());
}

/// Advances one material point and assembles exactly equal-and-opposite
/// endpoint forces. History commits only after every geometry, material,
/// energy, and runtime-range check succeeds.
inline StepResult Step(Runtime& runtime, const StepInput& input)
{
    StepResult result;
    if (!runtime.enabled)
    {
        result.error = Error::DISABLED;
        return result;
    }
    if (runtime.faulted)
    {
        result.error = Error::FAULT_LATCHED;
        result.material_error = runtime.last_material_error;
        return result;
    }

    CalibratedBeamMaterial::Error configuration_material_error =
        CalibratedBeamMaterial::Error::NONE;
    const Error configuration_error =
        ValidateConfiguration(
            runtime.configuration,
            &configuration_material_error);
    if (configuration_error != Error::NONE)
    {
        return LatchFailure(
            runtime,
            configuration_error,
            configuration_material_error);
    }
    if (!input.is_plain_axial_beam)
        return LatchFailure(runtime, Error::UNSUPPORTED_BEAM_ROLE);
    if (!CalibratedBeamMaterial::IsFinite(
            input.reference_length_m) ||
        !CalibratedBeamMaterial::IsFinite(
            input.current_length_m) ||
        !CalibratedBeamMaterial::IsFinite(
            input.damping_force_n))
    {
        return LatchFailure(runtime, Error::NONFINITE_INPUT);
    }
    if (input.reference_length_m <= 0.0)
        return LatchFailure(runtime, Error::INVALID_REFERENCE_LENGTH);
    if (input.current_length_m <= 0.0)
        return LatchFailure(runtime, Error::INVALID_CURRENT_LENGTH);

    std::array<double, 3> unit_direction = {{0.0, 0.0, 0.0}};
    if (!TryNormalize(input.direction, unit_direction))
        return LatchFailure(runtime, Error::INVALID_DIRECTION);

    const double length_delta =
        input.current_length_m - input.reference_length_m;
    const double total_strain =
        length_delta / input.reference_length_m;
    if (!CalibratedBeamMaterial::IsFinite(length_delta) ||
        !CalibratedBeamMaterial::IsFinite(total_strain))
    {
        return LatchFailure(runtime, Error::NUMERIC_OVERFLOW);
    }

    const CalibratedBeamMaterial::Response material_response =
        CalibratedBeamMaterial::Update(
            total_strain,
            runtime.configuration.material,
            runtime.state);
    if (!material_response.IsValid())
    {
        return LatchFailure(
            runtime,
            Error::MATERIAL_FAILURE,
            material_response.error);
    }

    const double reference_volume_m3 =
        runtime.configuration.cross_section_area_m2 *
        input.reference_length_m;
    const double material_force_n =
        -material_response.stress *
        runtime.configuration.cross_section_area_m2;
    const double dissipation_density_increment =
        material_response.plastic_dissipation_increment +
        material_response.damage_dissipation_increment;
    const double stored_energy_j =
        material_response.stored_energy_density *
        reference_volume_m3;
    const double dissipated_energy_increment_j =
        dissipation_density_increment *
        reference_volume_m3;
    if (!CalibratedBeamMaterial::IsFinite(reference_volume_m3) ||
        reference_volume_m3 <= 0.0 ||
        !CalibratedBeamMaterial::IsFinite(material_force_n) ||
        !CalibratedBeamMaterial::IsFinite(
            dissipation_density_increment) ||
        !CalibratedBeamMaterial::IsFinite(stored_energy_j) ||
        !CalibratedBeamMaterial::IsFinite(
            dissipated_energy_increment_j))
    {
        return LatchFailure(runtime, Error::NUMERIC_OVERFLOW);
    }

    // A fractured beam transmits neither its material response nor viscous
    // damping during the fracture step. This matches the production break
    // boundary and prevents the damper from reconnecting a failed beam.
    const double axial_force_n =
        material_response.state.fractured
            ? 0.0
            : material_force_n + input.damping_force_n;
    if (!CalibratedBeamMaterial::IsFinite(axial_force_n))
        return LatchFailure(runtime, Error::NUMERIC_OVERFLOW);
    if (!IsInRuntimeForceRange(axial_force_n))
    {
        return LatchFailure(
            runtime,
            Error::FORCE_OUT_OF_RUNTIME_RANGE);
    }

    ForcePair pair;
    for (std::size_t lane = 0; lane < pair.endpoint_1.size(); ++lane)
    {
        pair.endpoint_1[lane] =
            unit_direction[lane] * axial_force_n;
        pair.endpoint_2[lane] = -pair.endpoint_1[lane];
        if (!IsInRuntimeForceRange(pair.endpoint_1[lane]) ||
            !IsInRuntimeForceRange(pair.endpoint_2[lane]))
        {
            return LatchFailure(
                runtime,
                Error::FORCE_OUT_OF_RUNTIME_RANGE);
        }
    }

    runtime.state = material_response.state;
    runtime.last_error = Error::NONE;
    runtime.last_material_error =
        CalibratedBeamMaterial::Error::NONE;

    result.total_strain = total_strain;
    result.nominal_stress_pa = material_response.stress;
    result.material_force_n = material_force_n;
    result.damping_force_n =
        material_response.state.fractured
            ? 0.0
            : input.damping_force_n;
    result.axial_force_n = axial_force_n;
    result.stored_energy_j = stored_energy_j;
    result.dissipated_energy_increment_j =
        dissipated_energy_increment_j;
    result.forces = pair;
    result.yielded = material_response.yielded;
    result.fractured = material_response.state.fractured;
    result.fractured_this_step =
        material_response.fractured_this_step;
    return result;
}

} // namespace CalibratedBeamMaterialAdapter
} // namespace RoR
