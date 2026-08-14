/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "DeterministicVehicleInputActorAdapter.h"

#include <cstddef>

namespace RoR {
namespace DeterministicVehicleInputActorAdapter {
namespace {

float CanonicalZero(float value)
{
    return value == 0.0f ? 0.0f : value;
}

bool Set(
    DeterministicVehicleInput::Snapshot& snapshot,
    std::uint32_t control_id,
    double value)
{
    return snapshot.Set(control_id, value);
}

bool GetFloat(
    const DeterministicVehicleInput::Snapshot& snapshot,
    std::uint32_t control_id,
    float& value)
{
    double exact = 0.0;
    if (!snapshot.Get(control_id, exact))
        return false;
    value = static_cast<float>(exact);
    return static_cast<double>(value) == exact;
}

bool GetBool(
    const DeterministicVehicleInput::Snapshot& snapshot,
    std::uint32_t control_id,
    bool& value)
{
    double exact = 0.0;
    if (!snapshot.Get(control_id, exact) ||
        (exact != 0.0 && exact != 1.0))
    {
        return false;
    }
    value = exact == 1.0;
    return true;
}

bool GetInteger(
    const DeterministicVehicleInput::Snapshot& snapshot,
    std::uint32_t control_id,
    std::int32_t& value)
{
    double exact = 0.0;
    if (!snapshot.Get(control_id, exact))
        return false;
    value = static_cast<std::int32_t>(exact);
    return static_cast<double>(value) == exact;
}

} // namespace

PolicySnapshot::PolicySnapshot():
    schema_version(POLICY_SCHEMA_VERSION),
    target_id(0),
    gearbox_mode(0),
    forward_gear_count(0),
    gear_range_count(0),
    fixed_gear(0),
    fixed_gear_range(0),
    local_simulated(false),
    truck(false),
    has_engine(false),
    resetting(false),
    physics_paused(false),
    ai_active(false),
    has_linked_actors(false),
    has_transfer_case(false),
    anti_lock_brake_enabled(false),
    traction_control_enabled(false),
    cruise_control_enabled(false),
    speed_limiter_enabled(false),
    forward_commands_enabled(false),
    import_commands_enabled(false),
    has_simulated_event_overrides(false),
    hydro_speed_coupling_enabled(false)
{
}

Status::Status():
    error(PolicyError::NONE),
    snapshot_status()
{
}

AppliedControlState::AppliedControlState():
    steering_command(0.0f),
    service_brake(0.0f),
    throttle(0.0f),
    clutch(0.0f),
    parking_brake(false),
    engine_contact(false),
    engine_starter(false),
    gear(0),
    gear_range(0),
    hydro_speed_coupling(false),
    trailer_parking_brake(false),
    command_values()
{
    command_values.fill(0.0f);
}

ApplyPlan::ApplyPlan():
    controls()
{
}

const char* PolicyManifest()
{
    return
        "ror-live-actor-input-policy\n"
        "schema=1\n"
        "target=scenario-assigned-single-player-truck\n"
        "cadence=fixed-step-start-2000hz\n"
        "gearbox=manual-sequential-fixed-gear\n"
        "controllers=abs-off,tc-off,cruise-off,limiter-off\n"
        "drivetrain=unlinked,no-transfer-case,no-ai\n"
        "commands=local-only,no-forwarding,no-overrides\n"
        "replay=complete-transactional-snapshot\n";
}

const char* ToString(PolicyError error)
{
    switch (error)
    {
    case PolicyError::NONE: return "none";
    case PolicyError::INVALID_SCHEMA: return "invalid_schema";
    case PolicyError::INVALID_TARGET: return "invalid_target";
    case PolicyError::NOT_LOCAL_SIMULATED: return "not_local_simulated";
    case PolicyError::NOT_TRUCK: return "not_truck";
    case PolicyError::MISSING_ENGINE: return "missing_engine";
    case PolicyError::RESETTING: return "resetting";
    case PolicyError::PHYSICS_PAUSED: return "physics_paused";
    case PolicyError::AI_ACTIVE: return "ai_active";
    case PolicyError::LINKED_ACTORS: return "linked_actors";
    case PolicyError::TRANSFER_CASE: return "transfer_case";
    case PolicyError::UNSUPPORTED_GEARBOX: return "unsupported_gearbox";
    case PolicyError::INVALID_GEAR_CONFIGURATION:
        return "invalid_gear_configuration";
    case PolicyError::GEAR_CHANGE_UNSUPPORTED:
        return "gear_change_unsupported";
    case PolicyError::CONTROLLER_ENABLED: return "controller_enabled";
    case PolicyError::COMMAND_FORWARDING_ENABLED:
        return "command_forwarding_enabled";
    case PolicyError::SIMULATED_EVENT_OVERRIDE:
        return "simulated_event_override";
    case PolicyError::POLICY_CHANGED: return "policy_changed";
    case PolicyError::SNAPSHOT_REJECTED: return "snapshot_rejected";
    case PolicyError::TARGET_MISMATCH: return "target_mismatch";
    }
    return "unknown";
}

bool ValidatePolicy(const PolicySnapshot& policy, Status& status)
{
    status = Status();
    if (policy.schema_version != POLICY_SCHEMA_VERSION)
        status.error = PolicyError::INVALID_SCHEMA;
    else if (policy.target_id == 0)
        status.error = PolicyError::INVALID_TARGET;
    else if (!policy.local_simulated)
        status.error = PolicyError::NOT_LOCAL_SIMULATED;
    else if (!policy.truck)
        status.error = PolicyError::NOT_TRUCK;
    else if (!policy.has_engine)
        status.error = PolicyError::MISSING_ENGINE;
    else if (policy.resetting)
        status.error = PolicyError::RESETTING;
    else if (policy.physics_paused)
        status.error = PolicyError::PHYSICS_PAUSED;
    else if (policy.ai_active)
        status.error = PolicyError::AI_ACTIVE;
    else if (policy.has_linked_actors)
        status.error = PolicyError::LINKED_ACTORS;
    else if (policy.has_transfer_case)
        status.error = PolicyError::TRANSFER_CASE;
    else if (policy.gearbox_mode != MANUAL_GEARBOX_MODE)
        status.error = PolicyError::UNSUPPORTED_GEARBOX;
    else if (policy.forward_gear_count <= 0 ||
        policy.forward_gear_count > 255 ||
        policy.gear_range_count <= 0 ||
        policy.gear_range_count > 256 ||
        policy.fixed_gear < -1 ||
        policy.fixed_gear > policy.forward_gear_count ||
        policy.fixed_gear_range < 0 ||
        policy.fixed_gear_range >= policy.gear_range_count)
    {
        status.error = PolicyError::INVALID_GEAR_CONFIGURATION;
    }
    else if (policy.anti_lock_brake_enabled ||
        policy.traction_control_enabled ||
        policy.cruise_control_enabled ||
        policy.speed_limiter_enabled)
    {
        status.error = PolicyError::CONTROLLER_ENABLED;
    }
    else if (policy.forward_commands_enabled ||
        policy.import_commands_enabled)
    {
        status.error = PolicyError::COMMAND_FORWARDING_ENABLED;
    }
    else if (policy.has_simulated_event_overrides)
        status.error = PolicyError::SIMULATED_EVENT_OVERRIDE;

    return status.error == PolicyError::NONE;
}

bool SamePolicy(
    const PolicySnapshot& first,
    const PolicySnapshot& second)
{
    return first.schema_version == second.schema_version &&
        first.target_id == second.target_id &&
        first.gearbox_mode == second.gearbox_mode &&
        first.forward_gear_count == second.forward_gear_count &&
        first.gear_range_count == second.gear_range_count &&
        first.fixed_gear == second.fixed_gear &&
        first.fixed_gear_range == second.fixed_gear_range &&
        first.local_simulated == second.local_simulated &&
        first.truck == second.truck &&
        first.has_engine == second.has_engine &&
        first.resetting == second.resetting &&
        first.physics_paused == second.physics_paused &&
        first.ai_active == second.ai_active &&
        first.has_linked_actors == second.has_linked_actors &&
        first.has_transfer_case == second.has_transfer_case &&
        first.anti_lock_brake_enabled ==
            second.anti_lock_brake_enabled &&
        first.traction_control_enabled ==
            second.traction_control_enabled &&
        first.cruise_control_enabled == second.cruise_control_enabled &&
        first.speed_limiter_enabled == second.speed_limiter_enabled &&
        first.forward_commands_enabled ==
            second.forward_commands_enabled &&
        first.import_commands_enabled == second.import_commands_enabled &&
        first.has_simulated_event_overrides ==
            second.has_simulated_event_overrides &&
        first.hydro_speed_coupling_enabled ==
            second.hydro_speed_coupling_enabled;
}

bool CaptureSnapshot(
    const PolicySnapshot& policy,
    const AppliedControlState& controls,
    DeterministicVehicleInput::Snapshot& snapshot,
    Status& status)
{
    status = Status();
    if (!ValidatePolicy(policy, status))
        return false;
    if (controls.gear != policy.fixed_gear ||
        controls.gear_range != policy.fixed_gear_range)
    {
        status.error = PolicyError::GEAR_CHANGE_UNSUPPORTED;
        return false;
    }

    DeterministicVehicleInput::Snapshot candidate;
    candidate.target_id = policy.target_id;
    bool ok =
        Set(candidate,
            DeterministicVehicleInput::CONTROL_STEERING_COMMAND,
            CanonicalZero(controls.steering_command)) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_SERVICE_BRAKE,
            CanonicalZero(controls.service_brake)) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_THROTTLE,
            CanonicalZero(controls.throttle)) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_CLUTCH,
            CanonicalZero(controls.clutch)) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_PARKING_BRAKE,
            controls.parking_brake ? 1.0 : 0.0) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_ENGINE_CONTACT,
            controls.engine_contact ? 1.0 : 0.0) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_ENGINE_STARTER,
            controls.engine_starter ? 1.0 : 0.0) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_GEAR,
            static_cast<double>(controls.gear)) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_GEAR_RANGE,
            static_cast<double>(controls.gear_range)) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_HYDRO_SPEED_COUPLING,
            controls.hydro_speed_coupling ? 1.0 : 0.0) &&
        Set(candidate,
            DeterministicVehicleInput::CONTROL_TRAILER_PARKING_BRAKE,
            controls.trailer_parking_brake ? 1.0 : 0.0);
    for (std::size_t index = 0;
        ok && index < controls.command_values.size();
        ++index)
    {
        ok = Set(
            candidate,
            DeterministicVehicleInput::CommandControlId(
                static_cast<std::uint32_t>(index + 1U)),
            CanonicalZero(controls.command_values[index]));
    }
    if (!ok ||
        !DeterministicVehicleInput::ValidateSnapshot(
            candidate,
            status.snapshot_status))
    {
        status.error = PolicyError::SNAPSHOT_REJECTED;
        return false;
    }
    snapshot = candidate;
    status = Status();
    return true;
}

bool BuildApplyPlan(
    const PolicySnapshot& policy,
    const DeterministicVehicleInput::Snapshot& snapshot,
    ApplyPlan& plan,
    Status& status)
{
    status = Status();
    if (!ValidatePolicy(policy, status))
        return false;
    if (snapshot.target_id != policy.target_id)
    {
        status.error = PolicyError::TARGET_MISMATCH;
        return false;
    }
    if (!DeterministicVehicleInput::ValidateSnapshot(
            snapshot,
            status.snapshot_status))
    {
        status.error = PolicyError::SNAPSHOT_REJECTED;
        return false;
    }

    ApplyPlan candidate;
    bool ok =
        GetFloat(snapshot,
            DeterministicVehicleInput::CONTROL_STEERING_COMMAND,
            candidate.controls.steering_command) &&
        GetFloat(snapshot,
            DeterministicVehicleInput::CONTROL_SERVICE_BRAKE,
            candidate.controls.service_brake) &&
        GetFloat(snapshot,
            DeterministicVehicleInput::CONTROL_THROTTLE,
            candidate.controls.throttle) &&
        GetFloat(snapshot,
            DeterministicVehicleInput::CONTROL_CLUTCH,
            candidate.controls.clutch) &&
        GetBool(snapshot,
            DeterministicVehicleInput::CONTROL_PARKING_BRAKE,
            candidate.controls.parking_brake) &&
        GetBool(snapshot,
            DeterministicVehicleInput::CONTROL_ENGINE_CONTACT,
            candidate.controls.engine_contact) &&
        GetBool(snapshot,
            DeterministicVehicleInput::CONTROL_ENGINE_STARTER,
            candidate.controls.engine_starter) &&
        GetInteger(snapshot,
            DeterministicVehicleInput::CONTROL_GEAR,
            candidate.controls.gear) &&
        GetInteger(snapshot,
            DeterministicVehicleInput::CONTROL_GEAR_RANGE,
            candidate.controls.gear_range) &&
        GetBool(snapshot,
            DeterministicVehicleInput::CONTROL_HYDRO_SPEED_COUPLING,
            candidate.controls.hydro_speed_coupling) &&
        GetBool(snapshot,
            DeterministicVehicleInput::CONTROL_TRAILER_PARKING_BRAKE,
            candidate.controls.trailer_parking_brake);
    for (std::size_t index = 0;
        ok && index < candidate.controls.command_values.size();
        ++index)
    {
        ok = GetFloat(
            snapshot,
            DeterministicVehicleInput::CommandControlId(
                static_cast<std::uint32_t>(index + 1U)),
            candidate.controls.command_values[index]);
    }
    if (!ok)
    {
        status.error = PolicyError::SNAPSHOT_REJECTED;
        return false;
    }
    if (candidate.controls.gear != policy.fixed_gear ||
        candidate.controls.gear_range != policy.fixed_gear_range)
    {
        status.error = PolicyError::GEAR_CHANGE_UNSUPPORTED;
        return false;
    }
    plan = candidate;
    status = Status();
    return true;
}

} // namespace DeterministicVehicleInputActorAdapter
} // namespace RoR
