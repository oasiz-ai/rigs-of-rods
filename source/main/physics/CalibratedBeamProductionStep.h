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
/// @brief Strict-FP production boundary for opted-in calibrated beams.

#pragma once

#include "BeamAxialKinematics.h"
#include "BeamAxialResponse.h"
#include "CalibratedBeamMaterialAdapter.h"

namespace RoR {
namespace CalibratedBeamProductionStep {

struct Input
{
    BeamAxialKinematics::Input endpoints;
    double reference_length_m = 0.0;
    float damping_coefficient = 0.0f;
    float time_step = 0.0f;
    float mass_1 = 0.0f;
    float mass_2 = 0.0f;
    bool movable_1 = false;
    bool movable_2 = false;
    bool is_plain_axial_beam = false;
};

struct Result
{
    BeamAxialKinematics::Result kinematics;
    BeamAxialResponse::DampingResult damping;
    CalibratedBeamMaterialAdapter::StepResult material;

    bool IsValid() const
    {
        return kinematics.IsValid() && material.IsValid();
    }
};

/// Computes geometry, bounded damping, the material update, and the exact
/// equal-and-opposite force pair in one strict-FP translation unit. Geometry
/// failure latches the opted-in runtime exactly once; material history remains
/// unchanged apart from that fault metadata.
Result Step(
    CalibratedBeamMaterialAdapter::Runtime& runtime,
    const Input& input);

} // namespace CalibratedBeamProductionStep
} // namespace RoR
