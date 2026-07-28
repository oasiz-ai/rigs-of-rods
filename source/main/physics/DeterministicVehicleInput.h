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
/// @brief Versioned applied-vehicle-control bridge for deterministic traces.

#pragma once

#include "DeterministicInputTraceRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace RoR {
namespace DeterministicVehicleInput {

/// Schema 1 records the first common truck-control subset consumed by the
/// fixed-step solver. It does not cover every drivetrain/controller mode and
/// therefore does not by itself authorize live Actor replay.
/// It is intentionally distinct from raw device mappings and from vehicle
/// state. New controls require a new schema or an explicitly compatible
/// extension of this registry. One trace stream owns exactly one stable target;
/// multi-actor capture uses independently identified streams until a future
/// atomic multi-target schema is defined.
static const std::uint32_t SNAPSHOT_SCHEMA_VERSION = 1;
static const std::size_t STANDARD_CONTROL_COUNT = 11;
static const std::size_t COMMAND_CONTROL_COUNT = 84;
static const std::size_t CONTROL_SLOT_COUNT =
    STANDARD_CONTROL_COUNT + COMMAND_CONTROL_COUNT;

enum ControlId : std::uint32_t
{
    CONTROL_STEERING_COMMAND = 1,
    CONTROL_SERVICE_BRAKE = 2,
    CONTROL_THROTTLE = 3,
    CONTROL_CLUTCH = 4,
    CONTROL_PARKING_BRAKE = 5,
    CONTROL_ENGINE_CONTACT = 6,
    CONTROL_ENGINE_STARTER = 7,
    CONTROL_GEAR = 8,
    CONTROL_GEAR_RANGE = 9,
    CONTROL_HYDRO_SPEED_COUPLING = 10,
    CONTROL_TRAILER_PARKING_BRAKE = 11,

    /// Command keys are indexed 1 through 84 in the native truck format.
    CONTROL_COMMAND_1 = 1024,
    CONTROL_COMMAND_84 =
        CONTROL_COMMAND_1 +
        static_cast<std::uint32_t>(COMMAND_CONTROL_COUNT) - 1
};

enum class Error : std::uint32_t
{
    NONE = 0,
    INVALID_SCHEMA,
    INVALID_TARGET,
    UNKNOWN_CONTROL,
    NONFINITE_VALUE,
    NEGATIVE_ZERO,
    NON_BINARY32_VALUE,
    VALUE_OUT_OF_RANGE,
    NONINTEGRAL_VALUE,
    NONCANONICAL_ACTIVE_ZERO,
    PROVIDER_REJECTED,
    COLLECTOR_REJECTED,
    TARGET_MISMATCH,
    NONCANONICAL_ORDER,
    DUPLICATE_CONTROL,
    UNSUPPORTED_IMPULSE,
    REDUNDANT_DELTA,
    STATE_DELTA_MISMATCH,
    RUNTIME_MODE_MISMATCH,
    METADATA_SCHEMA_MISMATCH,
    FIXED_STEP_MISMATCH,
    CONSUMER_REJECTED
};

struct Status
{
    Error error;
    std::uint32_t control_id;

    Status();
};

/// Complete fixed-step-start applied-control snapshot for one stable,
/// scenario-assigned target. Every slot is initialized to positive zero and
/// every admitted scalar must have an exact IEEE-754 binary32 representation,
/// matching the production Actor/Engine storage boundary.
struct Snapshot
{
    std::uint32_t schema_version;
    std::uint64_t target_id;
    std::array<double, CONTROL_SLOT_COUNT> values;

    Snapshot();

    bool Set(std::uint32_t control_id, double value);
    bool Get(std::uint32_t control_id, double& value) const;
};

bool IsKnownControl(std::uint32_t control_id);
bool IsCommandControl(std::uint32_t control_id);
std::uint32_t CommandControlId(std::uint32_t command_index);
bool CommandIndex(std::uint32_t control_id, std::uint32_t& command_index);
const char* ControlName(std::uint32_t control_id);

/// Canonical registry text and its SHA-256-bound D0 source identity. Every
/// Runtime Metadata used with this adapter must use RegistrySourceName() as
/// `source_name` and the stable target ID as `stream_id`, so both the schema
/// and the target remain authenticated even when every applied value is zero.
const char* RegistryManifest();
const char* RegistrySourceName();
bool IsRegistryMetadata(
    const DeterministicInputTrace::Metadata& metadata,
    std::uint64_t target_id);

/// Validates identity, every scalar domain, positive-zero canonicalization,
/// and the fixed schema. On failure, `status` identifies the first control in
/// registry order and the snapshot remains untouched.
bool ValidateSnapshot(const Snapshot& snapshot, Status& status);

/// Transactionally reconstructs a complete snapshot from D0 active state.
/// Active state must be sorted, unique, nonzero, and contain only this schema's
/// target and controls. `snapshot` is unchanged on failure.
bool BuildSnapshotFromPersistentState(
    std::uint64_t target_id,
    const std::vector<
        DeterministicInputTrace::PersistentControl>& persistent_state,
    Snapshot& snapshot,
    Status& status);

class SnapshotProvider
{
public:
    virtual ~SnapshotProvider() {}

    /// Capture must be observational: false rejects the step and must not
    /// partially change gameplay state.
    virtual bool CaptureAppliedControls(
        std::uint64_t physics_step,
        Snapshot& snapshot) = 0;
};

class SnapshotConsumer
{
public:
    virtual ~SnapshotConsumer() {}

    /// Called once only after the complete replay batch and schema domains
    /// validate. Production consumers must additionally validate target-
    /// specific gear counts and reject unencoded controller/drivetrain modes
    /// before applying the snapshot transactionally.
    virtual bool ApplyAppliedControls(
        std::uint64_t physics_step,
        const Snapshot& snapshot) = 0;
};

/// Emits only bitwise changes from the previous complete snapshot. Positive
/// zero removes a persistent control from the D0 active-state set. Construct
/// only after Runtime::BeginRecording() or ImportContinuation(); the
/// authenticated runtime state is the mandatory fresh/continuation baseline.
/// The bound Runtime must outlive this adapter.
class RecordingSource:
    public DeterministicInputTrace::FixedStepSampleSource
{
public:
    RecordingSource(
        const DeterministicInputTrace::Runtime& runtime,
        std::uint64_t target_id,
        SnapshotProvider& provider);

    bool AcceptsRuntime(
        const DeterministicInputTrace::Runtime& runtime) const override;

    bool SampleFixedStepStart(
        std::uint64_t physics_step,
        DeterministicInputTrace::SampleCollector& collector) override;

    const Snapshot& GetPreviousSnapshot() const;
    const Status& GetStatus() const;

private:
    const DeterministicInputTrace::Runtime* m_runtime;
    std::uint64_t m_target_id;
    SnapshotProvider& m_provider;
    Snapshot m_previous;
    Status m_status;
    Status m_initial_status;
    std::uint64_t m_next_physics_step;
    bool m_ready;
};

/// Reconstructs one complete snapshot from authenticated persistent state.
/// Schema 1 accepts no impulses: discrete switches are represented by their
/// applied persistent state at each fixed-step boundary. Construct only after
/// Runtime::BeginReplay() or ImportContinuation(); the runtime supplies the
/// mandatory baseline and authenticated registry metadata. The bound Runtime
/// must outlive this adapter.
class ReplaySink:
    public DeterministicInputTrace::FixedStepInjectionSink
{
public:
    ReplaySink(
        const DeterministicInputTrace::Runtime& runtime,
        std::uint64_t target_id,
        SnapshotConsumer& consumer);

    bool AcceptsRuntime(
        const DeterministicInputTrace::Runtime& runtime) const override;

    bool InjectFixedStepStart(
        const DeterministicInputTrace::StepInjection& injection) override;

    const Snapshot& GetPreviousSnapshot() const;
    const Status& GetStatus() const;

private:
    const DeterministicInputTrace::Runtime* m_runtime;
    std::uint64_t m_target_id;
    SnapshotConsumer& m_consumer;
    Snapshot m_previous;
    Status m_status;
    Status m_initial_status;
    std::uint64_t m_next_physics_step;
    bool m_ready;
};

const char* ToString(Error error);

} // namespace DeterministicVehicleInput
} // namespace RoR
