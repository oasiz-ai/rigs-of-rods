/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file JBeamWheel2Approximation.h
/// @brief Fail-closed BeamNG pressureWheel to RoR Wheel2 J3 plan.

#pragma once

#include "JBeamPressureWheelIR.h"
#include "JBeamStructuralIR.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR {
namespace BeamNG {

/// This is an explicitly bounded J3 approximation. It does not claim that
/// RoR Wheel2 implements BeamNG pressure-volume, Stribeck/load-sensitive
/// friction, tyre anisotropy, brake thermals, ABS, or mesh-break semantics.
enum JBeamWheel2ApproximationSemantic : std::uint32_t
{
    JBEAM_WHEEL2_IGNORES_PRESSURE_VOLUME = UINT32_C(1) << 0U,
    JBEAM_WHEEL2_IGNORES_ADVANCED_FRICTION = UINT32_C(1) << 1U,
    JBEAM_WHEEL2_IGNORES_ANISOTROPIC_TYRE = UINT32_C(1) << 2U,
    JBEAM_WHEEL2_IGNORES_BRAKE_THERMALS_ABS = UINT32_C(1) << 3U,
    JBEAM_WHEEL2_IGNORES_BREAK_COUPLING = UINT32_C(1) << 4U
};

constexpr std::uint32_t JBEAM_WHEEL2_APPROXIMATION_SEMANTICS =
    JBEAM_WHEEL2_IGNORES_PRESSURE_VOLUME |
    JBEAM_WHEEL2_IGNORES_ADVANCED_FRICTION |
    JBEAM_WHEEL2_IGNORES_ANISOTROPIC_TYRE |
    JBEAM_WHEEL2_IGNORES_BRAKE_THERMALS_ABS |
    JBEAM_WHEEL2_IGNORES_BREAK_COUPLING;

enum class JBeamWheel2ApproximationCode
{
    ADMITTED,
    INVALID_STRUCTURAL_IR,
    INVALID_PRESSURE_WHEEL_IR,
    UNSUPPORTED_SOURCE_SECTION,
    WHEEL_LIMIT,
    GENERATED_TOPOLOGY_LIMIT,
    INVALID_SOURCE_FIELD_SET,
    INVALID_NODE_REFERENCE,
    INVALID_AXIS_GEOMETRY,
    UNSUPPORTED_GEOMETRY,
    UNSUPPORTED_COLLISION_MODE,
    UNSUPPORTED_PROPULSION_OR_BRAKING,
    INVALID_MASS,
    INVALID_BEAM_PARAMETERS,
    FLOAT_NARROWING,
    ALLOCATION_FAILURE,
    INTERNAL_FAILURE
};

struct JBeamWheel2ApproximationLimits
{
    std::size_t max_wheels;
    std::size_t max_generated_nodes;
    std::size_t max_generated_beams;
    std::size_t max_canonical_output_bytes;

    JBeamWheel2ApproximationLimits();
};

/// Exact binary32 values consumed by the later RigDef::Wheel2 construction.
/// The source wheel direction is retained even though the first J3 slice
/// requires zero source propulsion and publishes an unpropelled wheel.
struct JBeamWheel2ApproximationPlan
{
    std::size_t source_wheel_index;
    std::size_t source_record_index;
    std::size_t source_entry_index;
    std::string name;
    std::string node1;
    std::string node2;
    std::string node_arm;
    int wheel_direction;
    float rim_radius;
    float tyre_radius;
    float width;
    unsigned int num_rays;
    float mass;
    float rim_spring;
    float rim_damping;
    float tyre_spring;
    float tyre_damping;
    std::uint32_t approximated_semantics;

    JBeamWheel2ApproximationPlan();
};

struct JBeamWheel2ApproximationPlanSet
{
    JBeamWheel2ApproximationCode code;
    std::size_t rejected_wheel_index;
    std::string detail;
    std::vector<JBeamWheel2ApproximationPlan> plans;
    std::size_t generated_node_count;
    std::size_t generated_beam_count;
    std::size_t canonical_output_byte_limit;

    JBeamWheel2ApproximationPlanSet();
    bool IsAdmitted() const;
};

/// Builds all wheels or no wheels. Admission is intentionally narrow:
/// Wheel2 must preserve exact axis width/radii/count/mass/stiffness inputs;
/// nonzero offset, stabilizer nodes, source propulsion/braking, custom
/// collision modes, inert controller/powertrain/Lua sections, and unknown
/// effective fields remain rejected.
JBeamWheel2ApproximationPlanSet BuildJBeamWheel2ApproximationPlanSet(
    const JBeamResolvedGraph& graph,
    const JBeamWheel2ApproximationLimits& limits =
        JBeamWheel2ApproximationLimits());

/// Stable receipt material for the exact admitted plans. Returns empty for a
/// rejected/manual-invalid plan set or output overflow.
std::string SerializeCanonicalJBeamWheel2ApproximationPlanSet(
    const JBeamWheel2ApproximationPlanSet& plans);

const char* JBeamWheel2ApproximationCodeToString(
    JBeamWheel2ApproximationCode code);

} // namespace BeamNG
} // namespace RoR
