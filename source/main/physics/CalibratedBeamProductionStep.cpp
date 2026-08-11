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

#include "CalibratedBeamProductionStep.h"

namespace RoR {
namespace CalibratedBeamProductionStep {
namespace {

CalibratedBeamMaterialAdapter::Error MapKinematicsError(
    BeamAxialKinematics::Error error)
{
    switch (error)
    {
    case BeamAxialKinematics::Error::NONFINITE_INPUT:
        return CalibratedBeamMaterialAdapter::Error::NONFINITE_INPUT;
    case BeamAxialKinematics::Error::INVALID_LENGTH:
        return CalibratedBeamMaterialAdapter::Error::INVALID_CURRENT_LENGTH;
    case BeamAxialKinematics::Error::NUMERIC_OVERFLOW:
        return CalibratedBeamMaterialAdapter::Error::NUMERIC_OVERFLOW;
    case BeamAxialKinematics::Error::NONE:
    default:
        return CalibratedBeamMaterialAdapter::Error::NUMERIC_OVERFLOW;
    }
}

} // namespace

Result Step(
    CalibratedBeamMaterialAdapter::Runtime& runtime,
    const Input& input)
{
    Result result;
    if (!runtime.enabled)
    {
        result.material.error =
            CalibratedBeamMaterialAdapter::Error::DISABLED;
        return result;
    }
    if (runtime.faulted)
    {
        result.material.error =
            CalibratedBeamMaterialAdapter::Error::FAULT_LATCHED;
        result.material.material_error = runtime.last_material_error;
        return result;
    }

    result.kinematics = BeamAxialKinematics::Compute(input.endpoints);
    if (!result.kinematics.IsValid())
    {
        result.material = CalibratedBeamMaterialAdapter::LatchFailure(
            runtime,
            MapKinematicsError(result.kinematics.error));
        return result;
    }

    result.damping = BeamAxialResponse::ComputeDamping(
        result.kinematics.axial_relative_velocity_runtime_mps,
        input.damping_coefficient,
        input.time_step,
        input.mass_1,
        input.mass_2,
        input.movable_1,
        input.movable_2);

    CalibratedBeamMaterialAdapter::StepInput material_input;
    material_input.reference_length_m = input.reference_length_m;
    material_input.current_length_m =
        result.kinematics.current_length_m;
    material_input.damping_force_n = result.damping.force;
    material_input.direction = {{
        input.endpoints.endpoint_1_position_m[0] -
            input.endpoints.endpoint_2_position_m[0],
        input.endpoints.endpoint_1_position_m[1] -
            input.endpoints.endpoint_2_position_m[1],
        input.endpoints.endpoint_1_position_m[2] -
            input.endpoints.endpoint_2_position_m[2]
    }};
    material_input.is_plain_axial_beam = input.is_plain_axial_beam;
    result.material = CalibratedBeamMaterialAdapter::Step(
        runtime,
        material_input);
    return result;
}

} // namespace CalibratedBeamProductionStep
} // namespace RoR
