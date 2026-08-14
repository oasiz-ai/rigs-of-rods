/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Dependency-free policy boundary for the production Actor adapter.

#pragma once

#include "DeterministicVehicleInput.h"

#include <array>
#include <cstdint>

namespace RoR {
namespace DeterministicVehicleInputActorAdapter {

static const std::uint32_t POLICY_SCHEMA_VERSION = 1;
static const std::uint32_t MANUAL_GEARBOX_MODE = 2;

/// Versioned fail-closed production subset. Mutable applied controls are not
/// part of this structure; every field here must remain bit-for-bit stable for
/// the lifetime of a recording or replay.
struct PolicySnapshot
{
    std::uint32_t schema_version;
    std::uint64_t target_id;
    std::uint32_t gearbox_mode;
    std::int32_t forward_gear_count;
    std::int32_t gear_range_count;
    std::int32_t fixed_gear;
    std::int32_t fixed_gear_range;
    bool local_simulated;
    bool truck;
    bool has_engine;
    bool resetting;
    bool physics_paused;
    bool ai_active;
    bool has_linked_actors;
    bool has_transfer_case;
    bool anti_lock_brake_enabled;
    bool traction_control_enabled;
    bool cruise_control_enabled;
    bool speed_limiter_enabled;
    bool forward_commands_enabled;
    bool import_commands_enabled;
    bool has_simulated_event_overrides;
    bool hydro_speed_coupling_enabled;

    PolicySnapshot();
};

enum class PolicyError : std::uint32_t
{
    NONE = 0,
    INVALID_SCHEMA,
    INVALID_TARGET,
    NOT_LOCAL_SIMULATED,
    NOT_TRUCK,
    MISSING_ENGINE,
    RESETTING,
    PHYSICS_PAUSED,
    AI_ACTIVE,
    LINKED_ACTORS,
    TRANSFER_CASE,
    UNSUPPORTED_GEARBOX,
    INVALID_GEAR_CONFIGURATION,
    GEAR_CHANGE_UNSUPPORTED,
    CONTROLLER_ENABLED,
    COMMAND_FORWARDING_ENABLED,
    SIMULATED_EVENT_OVERRIDE,
    POLICY_CHANGED,
    SNAPSHOT_REJECTED,
    TARGET_MISMATCH
};

struct Status
{
    PolicyError error;
    DeterministicVehicleInput::Status snapshot_status;

    Status();
};

/// Values observed at the exact fixed-step start after normal input mapping.
struct AppliedControlState
{
    float steering_command;
    float service_brake;
    float throttle;
    float clutch;
    bool parking_brake;
    bool engine_contact;
    bool engine_starter;
    std::int32_t gear;
    std::int32_t gear_range;
    bool hydro_speed_coupling;
    bool trailer_parking_brake;
    std::array<float,
        DeterministicVehicleInput::COMMAND_CONTROL_COUNT> command_values;

    AppliedControlState();
};

/// Fully validated, no-fail commit payload for the live Actor/Engine objects.
struct ApplyPlan
{
    AppliedControlState controls;

    ApplyPlan();
};

const char* PolicyManifest();
const char* ToString(PolicyError error);

bool ValidatePolicy(const PolicySnapshot& policy, Status& status);
bool SamePolicy(
    const PolicySnapshot& first,
    const PolicySnapshot& second);

/// Converts binary32 Actor storage to the canonical schema-1 snapshot. Exact
/// negative zero is normalized to positive zero at this serialization
/// boundary; all other non-finite/out-of-domain values remain rejected.
bool CaptureSnapshot(
    const PolicySnapshot& policy,
    const AppliedControlState& controls,
    DeterministicVehicleInput::Snapshot& snapshot,
    Status& status);

/// Validates the complete authenticated snapshot, target identity, live
/// policy, and fixed-gear restriction before publishing a no-fail apply plan.
bool BuildApplyPlan(
    const PolicySnapshot& policy,
    const DeterministicVehicleInput::Snapshot& snapshot,
    ApplyPlan& plan,
    Status& status);

} // namespace DeterministicVehicleInputActorAdapter
} // namespace RoR
