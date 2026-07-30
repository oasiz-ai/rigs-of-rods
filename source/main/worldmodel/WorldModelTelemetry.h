/*
    This source file is part of Rigs of Rods
    Rigs of Rods is free software under the GNU General Public License v3.
*/

/// @file
/// @brief Typed, canonical telemetry records for native world-model capture.

#pragma once

#include "WorldModelCaptureContract.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace RoR {
namespace WorldModel {

static const std::uint32_t WORLD_MODEL_OBSERVATION_RATE_HZ = 48U;
static const std::uint32_t WORLD_MODEL_PHYSICS_RATE_HZ = 2000U;

struct RationalTime
{
    std::uint64_t numerator = 0U;
    std::uint32_t denominator = 1U;
};

struct Vector3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/// Quaternion components are always serialized in explicit w, x, y, z order.
struct QuaternionWxyz
{
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct PhysicsStepRange
{
    std::uint64_t first_completed_step = 0U;
    std::uint64_t last_completed_step = 0U;
    std::uint32_t substep_count = 0U;
};

struct NamedScalar
{
    std::string name;
    double value = 0.0;
};

struct VehicleTelemetry
{
    Vector3 position_m;
    QuaternionWxyz orientation_world_from_vehicle;
    Vector3 linear_velocity_mps;
    Vector3 angular_velocity_radps;
    double speed_mps = 0.0;
    double mass_kg = 0.0;
};

struct EngineTelemetry
{
    bool running = false;
    bool contact = false;
    bool starter = false;
    double rpm = 0.0;
    double torque_nm = 0.0;
    double throttle = 0.0;
    double clutch = 0.0;
    std::int32_t gear = 0;
    std::int32_t gear_range = 0;
    std::string mode;
    std::vector<NamedScalar> timers_seconds;
};

struct WorldTelemetry
{
    std::string world_id;
    std::string terrain_id;
    std::string terrain_sha256;
    Vector3 gravity_mps2;
    bool water_enabled = false;
    double water_level_m = 0.0;
    std::string weather_id;
};

struct CameraTelemetry
{
    std::string camera_id;
    std::string coordinate_frame;
    Vector3 position_m;
    QuaternionWxyz orientation_world_from_camera;
    std::array<double, 16> view_matrix = {};
    std::array<double, 16> projection_matrix = {};
    std::array<double, 9> intrinsics = {};
    double vertical_fov_radians = 0.0;
    double near_clip_m = 0.0;
    double far_clip_m = 0.0;
};

struct RgbDescriptor
{
    std::uint64_t record_id = 0U;
    std::string pixel_format;
    std::string color_space;
    std::string row_origin;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t row_stride_bytes = 0U;
    std::string raw_sha256;
};

struct ContactSummary
{
    std::uint32_t contact_count = 0U;
    std::uint32_t wheel_contact_count = 0U;
    double maximum_normal_impulse_ns = 0.0;
    double maximum_penetration_m = 0.0;
};

struct EventRecord
{
    std::string event_id;
    std::string event_type;
    std::uint64_t physics_tick = 0U;
    std::string detail;
};

struct OutcomeRecord
{
    std::string status;
    bool terminal = false;
    bool reset = false;
    bool success = false;
    double reward = 0.0;
    std::string detail;
};

struct ObservationRecord
{
    EpisodeId episode_id;
    ObservationId observation_id;
    std::uint64_t frame_id = 0U;
    std::string target_id;
    RationalTime nominal_time;
    PhysicsStepRange physics_steps;
    VehicleTelemetry vehicle;
    EngineTelemetry engine;
    WorldTelemetry world;
    CameraTelemetry camera;
    RgbDescriptor rgb;
    ContactSummary contacts;
    std::string state_sha256;
};

enum class ControlStage : std::uint8_t
{
    RAW = 0U,
    ISSUED = 1U,
    RESOLVED = 2U,
    APPLIED = 3U
};

/// A stable sample identity plus explicit ancestry makes the transformation
/// from raw input to the value applied at a fixed-step boundary auditable.
struct ControlSample
{
    std::string sample_id;
    std::string control_id;
    std::string source_id;
    std::uint64_t source_tick = 0U;
    std::uint64_t effective_tick = 0U;
    double value = 0.0;
    std::vector<std::string> parent_sample_ids;
};

struct ControlLineage
{
    std::vector<ControlSample> raw;
    std::vector<ControlSample> issued;
    std::vector<ControlSample> resolved;
    std::vector<ControlSample> applied;
};

struct TransitionRecord
{
    EpisodeId episode_id;
    std::uint64_t transition_index = 0U;
    TransitionId transition_id;
    std::string target_id;
    RationalTime source_time;
    RationalTime target_time;
    PhysicsStepRange effective_steps;
    ControlLineage controls;
    ContactSummary contacts;
    std::vector<EventRecord> events;
    OutcomeRecord outcome;
};

/// Identifiers and SHA-256 strings are deliberately stricter than arbitrary
/// text fields. Identifiers are lowercase ASCII and hashes are lowercase hex.
bool IsCanonicalWorldModelIdentifier(const std::string& value);
bool IsCanonicalSha256(const std::string& value);
bool IsStrictUtf8(const std::string& value);

bool ValidateObservationRecord(
    const ObservationRecord& record,
    std::string* error = nullptr);
bool ValidateTransitionRecord(
    const TransitionRecord& record,
    std::string* error = nullptr);

/// On failure, output is unchanged. On success, output is one strict UTF-8
/// JSON value with stable key ordering and no insignificant whitespace.
bool SerializeObservationRecord(
    const ObservationRecord& record,
    std::string& output,
    std::string* error = nullptr);
bool SerializeTransitionRecord(
    const TransitionRecord& record,
    std::string& output,
    std::string* error = nullptr);

} // namespace WorldModel
} // namespace RoR
