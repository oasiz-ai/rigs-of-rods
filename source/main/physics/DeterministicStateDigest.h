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
/// @brief Canonical, dependency-free physics-state digest contract.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace RoR {
namespace DeterministicStateDigest {

static const std::uint32_t SCHEMA_VERSION = 3;

// These ceilings are part of the digest schema and cannot be loosened by
// content or callers. The streaming builder itself retains no records.
static const std::uint32_t MAX_ACTORS = 4096;
static const std::uint32_t MAX_NODES = 1048576;
static const std::uint32_t MAX_BEAMS = 4194304;
static const std::uint32_t MAX_HYDROS = 1048576;
static const std::uint32_t MAX_CONTACTS = 65536;

struct Digest
{
    std::array<std::uint8_t, 32> bytes;

    Digest();
    std::string ToHex() const;
    bool operator==(const Digest& other) const;
    bool operator!=(const Digest& other) const;
};

enum class Error
{
    NONE = 0,
    INVALID_SECTION_ORDER,
    COUNT_LIMIT_EXCEEDED,
    COUNT_MISMATCH,
    NON_CANONICAL_KEY,
    INVALID_RECORD,
    NON_FINITE_VALUE,
    ALREADY_FINISHED
};

enum ActorStateCode : std::uint32_t
{
    ACTOR_STATE_LOCAL_SIMULATED = 0,
    ACTOR_STATE_NETWORKED_OK = 1,
    ACTOR_STATE_NETWORKED_HIDDEN = 2,
    ACTOR_STATE_LOCAL_REPLAY = 3,
    ACTOR_STATE_LOCAL_SLEEPING = 4,
    ACTOR_STATE_DISPOSED = 5
};

enum ActorRecordFlags : std::uint32_t
{
    ACTOR_FLAG_UPDATE_PHYSICS = UINT32_C(1) << 0,
    ACTOR_FLAG_PHYSICS_PAUSED = UINT32_C(1) << 1,
    ACTOR_FLAG_COLLISION_RELEVANT = UINT32_C(1) << 2,
    ACTOR_FLAG_ONGOING_RESET = UINT32_C(1) << 3
};

static const std::uint32_t ACTOR_FLAG_MASK =
    ACTOR_FLAG_UPDATE_PHYSICS |
    ACTOR_FLAG_PHYSICS_PAUSED |
    ACTOR_FLAG_COLLISION_RELEVANT |
    ACTOR_FLAG_ONGOING_RESET;

struct ActorRecord
{
    std::int32_t actor_id;
    std::uint32_t state;
    std::uint32_t flags;
    std::uint64_t deterministic_seed;
    std::uint64_t actor_physics_step;
    std::uint64_t engine_update_step;
    std::array<float, 3> origin;

    ActorRecord();
};

struct NodeRecord
{
    std::int32_t actor_id;
    std::uint32_t node_id;
    /// Actor-local simulation position (`node_t::RelPosition` in production).
    /// ActorRecord::origin carries the world-space offset separately.
    std::array<float, 3> position;
    std::array<float, 3> velocity;

    NodeRecord();
};

enum BeamStateFlags : std::uint32_t
{
    BEAM_STATE_DISABLED = UINT32_C(1) << 0,
    BEAM_STATE_BROKEN = UINT32_C(1) << 1,
    BEAM_STATE_MATERIAL_FRACTURED = UINT32_C(1) << 2,
    BEAM_STATE_MATERIAL_FAULTED = UINT32_C(1) << 3
};

static const std::uint32_t BEAM_STATE_MASK =
    BEAM_STATE_DISABLED |
    BEAM_STATE_BROKEN |
    BEAM_STATE_MATERIAL_FRACTURED |
    BEAM_STATE_MATERIAL_FAULTED;
static const std::uint32_t BEAM_MATERIAL_SCHEMA_NONE = 0;
static const std::uint32_t BEAM_MATERIAL_SCHEMA_CALIBRATED_V1 = 1;

/// Schema-v3 codes are intentionally independent of the implementation enum
/// ordinals. Only errors which can be latched into calibrated runtime state
/// have a representation here.
enum BeamMaterialRuntimeErrorCode : std::uint32_t
{
    BEAM_MATERIAL_RUNTIME_ERROR_NONE = 0,
    BEAM_MATERIAL_RUNTIME_ERROR_UNSUPPORTED_ADAPTER_SCHEMA = 1,
    BEAM_MATERIAL_RUNTIME_ERROR_UNSUPPORTED_BEAM_ROLE = 2,
    BEAM_MATERIAL_RUNTIME_ERROR_NONFINITE_INPUT = 3,
    BEAM_MATERIAL_RUNTIME_ERROR_INVALID_CROSS_SECTION_AREA = 4,
    BEAM_MATERIAL_RUNTIME_ERROR_INVALID_REFERENCE_LENGTH = 5,
    BEAM_MATERIAL_RUNTIME_ERROR_INVALID_CURRENT_LENGTH = 6,
    BEAM_MATERIAL_RUNTIME_ERROR_INVALID_DIRECTION = 7,
    BEAM_MATERIAL_RUNTIME_ERROR_MATERIAL_FAILURE = 8,
    BEAM_MATERIAL_RUNTIME_ERROR_NUMERIC_OVERFLOW = 9,
    BEAM_MATERIAL_RUNTIME_ERROR_FORCE_OUT_OF_RUNTIME_RANGE = 10
};

enum BeamMaterialErrorCode : std::uint32_t
{
    BEAM_MATERIAL_ERROR_NONE = 0,
    BEAM_MATERIAL_ERROR_UNSUPPORTED_SCHEMA = 1,
    BEAM_MATERIAL_ERROR_NONFINITE_INPUT = 2,
    BEAM_MATERIAL_ERROR_INVALID_ELASTIC_MODULUS = 3,
    BEAM_MATERIAL_ERROR_INVALID_YIELD_STRESS = 4,
    BEAM_MATERIAL_ERROR_INVALID_HARDENING_MODULUS = 5,
    BEAM_MATERIAL_ERROR_INVALID_DAMAGE_ONSET = 6,
    BEAM_MATERIAL_ERROR_INVALID_DAMAGE_DRIVER_CAPACITY = 7,
    BEAM_MATERIAL_ERROR_INVALID_STATE = 8,
    BEAM_MATERIAL_ERROR_NUMERIC_OVERFLOW = 9
};

struct BeamRecord
{
    std::int32_t actor_id;
    std::uint32_t beam_id;
    float rest_length;
    float stress;
    std::uint32_t material_schema_version;
    double plastic_strain;
    double accumulated_plastic_strain;
    double damage;
    double damage_driver_density;
    double last_total_strain;
    std::uint32_t material_runtime_error;
    std::uint32_t material_error;
    std::uint32_t state_flags;

    BeamRecord();
};

static const std::uint32_t HYDRO_RUNTIME_SCHEMA_JBEAM_V1 = 1;
static const std::uint32_t HYDRO_INPUT_ROUTE_STEERING = 1;

enum HydroRecordFlags : std::uint32_t
{
    HYDRO_FLAG_HAS_FACTOR = UINT32_C(1) << 0,
    HYDRO_FLAG_HAS_STEERING_WHEEL_LOCK = UINT32_C(1) << 1,
    HYDRO_FLAG_FAULT_LATCHED = UINT32_C(1) << 2
};

static const std::uint32_t HYDRO_FLAG_MASK =
    HYDRO_FLAG_HAS_FACTOR |
    HYDRO_FLAG_HAS_STEERING_WHEEL_LOCK |
    HYDRO_FLAG_FAULT_LATCHED;

/// Canonical schema codes deliberately do not depend on
/// JBeamHydroRuntimeFault ordinals.
enum HydroRuntimeFaultCode : std::uint32_t
{
    HYDRO_RUNTIME_FAULT_NONE = 0,
    HYDRO_RUNTIME_FAULT_INVALID_CONFIG = 1,
    HYDRO_RUNTIME_FAULT_INVALID_INITIAL_LENGTH = 2,
    HYDRO_RUNTIME_FAULT_INVALID_PREVIOUS_STATE = 3,
    HYDRO_RUNTIME_FAULT_INVALID_INPUT = 4,
    HYDRO_RUNTIME_FAULT_INVALID_TIMESTEP = 5,
    HYDRO_RUNTIME_FAULT_STEP_REJECTED = 6,
    HYDRO_RUNTIME_FAULT_REST_LENGTH_REJECTED = 7,
    HYDRO_RUNTIME_FAULT_FLOAT_NARROWING = 8,
    HYDRO_RUNTIME_FAULT_STEP_COUNTER_EXHAUSTED = 9
};

/// Complete native JBeam hydro configuration and continuation state. The
/// runtime rest length is cross-bound to the referenced BeamRecord by the
/// snapshot adapter, while the reference length and ratio prove that the
/// binary32 solver value is the exact deterministic resolution of history.
struct HydroRecord
{
    std::int32_t actor_id;
    std::uint32_t hydro_id;
    std::uint32_t beam_id;
    std::uint32_t runtime_schema_version;
    std::uint32_t input_route;
    std::uint32_t flags;
    float reference_length;
    float runtime_rest_length;
    double factor;
    double in_limit;
    double out_limit;
    double input_factor;
    double input_center;
    double input_in_limit;
    double input_out_limit;
    double in_rate;
    double out_rate;
    double auto_center_rate;
    double steering_wheel_lock;
    double length_ratio;
    std::uint64_t accepted_step_count;
    std::uint32_t fault;

    HydroRecord();
};

struct ContactRecord
{
    std::int32_t surface_actor;
    std::uint32_t surface_contact;
    std::int32_t hit_actor;
    std::uint32_t hit_node;

    ContactRecord();
};

/// Streams one complete snapshot in the fixed section order
/// actors -> nodes -> beams -> hydros -> contacts. Records must have strictly
/// increasing canonical keys within each section. Counts are written before
/// records, and Finish() succeeds only after every declared record was
/// observed.
///
/// Floating-point values are hashed as their exact IEEE-754 binary32 or
/// binary64 payloads in little-endian schema order. Signed zero is
/// intentionally distinct. NaN and infinity are rejected through bit
/// inspection so release fast-math cannot make an invalid snapshot appear
/// finite.
class Builder
{
public:
    Builder(std::uint64_t physics_step, std::uint64_t scenario_id);

    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    bool BeginActors(std::uint32_t count);
    bool AddActor(const ActorRecord& record);
    bool BeginNodes(std::uint32_t count);
    bool AddNode(const NodeRecord& record);
    bool BeginBeams(std::uint32_t count);
    bool AddBeam(const BeamRecord& record);
    bool BeginHydros(std::uint32_t count);
    bool AddHydro(const HydroRecord& record);
    bool BeginContacts(std::uint32_t count);
    bool AddContact(const ContactRecord& record);
    bool Finish(Digest& digest);

    Error GetError() const;
    std::uint32_t GetErrorRecordIndex() const;

private:
    enum class Section
    {
        INITIAL,
        ACTORS,
        NODES,
        BEAMS,
        HYDROS,
        CONTACTS,
        FINISHED
    };

    bool BeginSection(
        Section required_previous,
        Section next,
        std::uint32_t count,
        std::uint32_t limit,
        std::uint8_t tag);
    bool RequireWritable();
    bool RequireFinite(const float& value, std::uint32_t record_index);
    bool RequireFinite(const double& value, std::uint32_t record_index);
    bool Fail(Error error, std::uint32_t record_index);
    bool CompleteCurrentSection() const;

    void HashByte(std::uint8_t value);
    void HashU32(std::uint32_t value);
    void HashI32(std::int32_t value);
    void HashU64(std::uint64_t value);
    void HashFloat(const float& value);
    void HashDouble(const double& value);
    void HashBytes(const std::uint8_t* data, std::size_t size);
    void TransformSha256(const std::uint8_t block[64]);
    void FinalizeSha256(Digest& digest);

    Section m_section;
    Error m_error;
    std::uint32_t m_error_record_index;
    std::uint32_t m_expected_count;
    std::uint32_t m_observed_count;

    std::int32_t m_previous_actor_id;
    std::int32_t m_previous_node_actor_id;
    std::uint32_t m_previous_node_id;
    std::int32_t m_previous_beam_actor_id;
    std::uint32_t m_previous_beam_id;
    std::int32_t m_previous_hydro_actor_id;
    std::uint32_t m_previous_hydro_id;
    ContactRecord m_previous_contact;
    bool m_has_previous_key;

    std::array<std::uint32_t, 8> m_sha_state;
    std::array<std::uint8_t, 64> m_sha_buffer;
    std::uint64_t m_sha_byte_count;
    std::size_t m_sha_buffer_size;
};

/// Per-actor section sizes and actor state supplied to the production snapshot
/// adapter. Node and beam records are read by their immutable array index.
/// Hydro records enumerate only native JBeam hydros and retain their original
/// Actor::ar_hydros index as HydroRecord::hydro_id.
struct SnapshotActor
{
    ActorRecord actor;
    std::uint32_t node_count;
    std::uint32_t beam_count;
    std::uint32_t hydro_count;
    std::uint32_t surface_contact_count;

    SnapshotActor();
};

/// Dependency-free extraction seam. Implementations expose live simulation
/// state without transferring ownership or allowing the digest code to mutate
/// it. BuildSnapshotDigest() may read records in canonical actor-ID order rather
/// than source order. The caller must keep every exposed record immutable until
/// BuildSnapshotDigest() returns; this API performs no synchronization.
class SnapshotSource
{
public:
    virtual ~SnapshotSource();

    virtual std::size_t GetActorCount() const = 0;
    virtual bool ReadActor(
        std::size_t source_actor_index,
        SnapshotActor& actor) const = 0;
    virtual bool ReadNode(
        std::size_t source_actor_index,
        std::uint32_t node_index,
        NodeRecord& node) const = 0;
    virtual bool ReadBeam(
        std::size_t source_actor_index,
        std::uint32_t beam_index,
        BeamRecord& beam) const = 0;
    virtual bool ReadHydro(
        std::size_t source_actor_index,
        std::uint32_t hydro_index,
        HydroRecord& hydro) const = 0;
    virtual std::size_t GetContactCount() const = 0;
    virtual bool ReadContact(
        std::size_t source_contact_index,
        ContactRecord& contact) const = 0;
};

enum class SnapshotError
{
    NONE = 0,
    COUNT_LIMIT_EXCEEDED,
    SOURCE_READ_FAILED,
    INVALID_ACTOR_ID,
    DUPLICATE_ACTOR_ID,
    INVALID_CROSS_REFERENCE,
    ALLOCATION_FAILED,
    DIGEST_REJECTED
};

struct SnapshotStatus
{
    SnapshotError error;
    Error digest_error;
    std::size_t source_index;
    std::uint32_t record_index;

    SnapshotStatus();
};

/// Builds one canonical snapshot without changing the source or output on
/// failure. Actors and contacts are canonicalized within the immutable schema
/// ceilings; nodes and beams retain their Actor array indexes.
bool BuildSnapshotDigest(
    std::uint64_t physics_step,
    std::uint64_t scenario_id,
    const SnapshotSource& source,
    Digest& digest,
    SnapshotStatus* status = nullptr);

} // namespace DeterministicStateDigest
} // namespace RoR
