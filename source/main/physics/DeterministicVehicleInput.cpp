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

#include "DeterministicVehicleInput.h"

#include <cstring>

namespace RoR {
namespace DeterministicVehicleInput {
namespace {

const char REGISTRY_MANIFEST[] =
    "ror-deterministic-vehicle-input-registry\n"
    "schema=1\n"
    "semantics=restricted-applied-control-state\n"
    "stream=single-target\n"
    "target_identity=stream_id\n"
    "storage=binary32-exact\n"
    "1=steering_command[-1,1]\n"
    "2=service_brake[0,1]\n"
    "3=throttle[0,1]\n"
    "4=clutch[0,1]\n"
    "5=parking_brake{0,1}\n"
    "6=engine_contact{0,1}\n"
    "7=engine_starter{0,1}\n"
    "8=gear:int[-1,255]\n"
    "9=gear_range:int[0,255]\n"
    "10=hydro_speed_coupling{0,1}\n"
    "11=trailer_parking_brake{0,1}\n"
    "1024..1107=command_key_1..84[0,1]\n";

const char REGISTRY_SOURCE_NAME[] =
    "ror-restricted-applied-control-state-v1+sha256:"
    "5368675b48c68ee2804455ed0577bc5069aab2fa50a210c3dbe9d28785057f95";

std::uint64_t DoubleBits(const double& value)
{
    // Read the object representation through volatile bytes. Optimized
    // fast-math builds may otherwise prove a by-value floating argument
    // finite and fold away the exponent check, even though this validation
    // boundary deliberately accepts hostile serialized values.
    unsigned char representation[sizeof(value)];
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index)
        representation[index] = source[index];

    std::uint64_t bits = 0;
    std::memcpy(&bits, representation, sizeof(bits));
    return bits;
}

bool IsFiniteBits(double value)
{
    return
        (DoubleBits(value) & UINT64_C(0x7ff0000000000000)) !=
        UINT64_C(0x7ff0000000000000);
}

bool IsNegativeZero(double value)
{
    return DoubleBits(value) == UINT64_C(0x8000000000000000);
}

bool IsPositiveZero(double value)
{
    return DoubleBits(value) == 0;
}

bool SameBits(double first, double second)
{
    return DoubleBits(first) == DoubleBits(second);
}

bool SameSnapshot(
    const Snapshot& first,
    const Snapshot& second)
{
    if (first.schema_version != second.schema_version ||
        first.target_id != second.target_id)
    {
        return false;
    }
    for (std::size_t slot = 0;
        slot < first.values.size();
        ++slot)
    {
        if (!SameBits(
                first.values[slot],
                second.values[slot]))
        {
            return false;
        }
    }
    return true;
}

bool TryControlSlot(
    std::uint32_t control_id,
    std::size_t& slot)
{
    if (control_id >= CONTROL_STEERING_COMMAND &&
        control_id <= CONTROL_TRAILER_PARKING_BRAKE)
    {
        slot = static_cast<std::size_t>(
            control_id - CONTROL_STEERING_COMMAND);
        return true;
    }
    if (control_id >= CONTROL_COMMAND_1 &&
        control_id <= CONTROL_COMMAND_84)
    {
        slot =
            STANDARD_CONTROL_COUNT +
            static_cast<std::size_t>(
                control_id - CONTROL_COMMAND_1);
        return true;
    }
    return false;
}

std::uint32_t SlotControlId(std::size_t slot)
{
    if (slot < STANDARD_CONTROL_COUNT)
    {
        return
            CONTROL_STEERING_COMMAND +
            static_cast<std::uint32_t>(slot);
    }
    return
        CONTROL_COMMAND_1 +
        static_cast<std::uint32_t>(
            slot - STANDARD_CONTROL_COUNT);
}

bool IsBooleanControl(std::uint32_t control_id)
{
    return
        control_id == CONTROL_PARKING_BRAKE ||
        control_id == CONTROL_ENGINE_CONTACT ||
        control_id == CONTROL_ENGINE_STARTER ||
        control_id == CONTROL_HYDRO_SPEED_COUPLING ||
        control_id == CONTROL_TRAILER_PARKING_BRAKE;
}

bool IsUnitControl(std::uint32_t control_id)
{
    return
        control_id == CONTROL_SERVICE_BRAKE ||
        control_id == CONTROL_THROTTLE ||
        control_id == CONTROL_CLUTCH ||
        IsCommandControl(control_id);
}

bool ValidateControlValue(
    std::uint32_t control_id,
    double value,
    Status& status)
{
    status.control_id = control_id;
    if (!IsFiniteBits(value))
    {
        status.error = Error::NONFINITE_VALUE;
        return false;
    }
    if (IsNegativeZero(value))
    {
        status.error = Error::NEGATIVE_ZERO;
        return false;
    }
    if (control_id == CONTROL_STEERING_COMMAND)
    {
        if (value < -1.0 || value > 1.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
    }
    else if (IsUnitControl(control_id))
    {
        if (value < 0.0 || value > 1.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
    }
    else if (IsBooleanControl(control_id))
    {
        if (value != 0.0 && value != 1.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
    }
    else if (control_id == CONTROL_GEAR)
    {
        if (value < -1.0 || value > 255.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
        const int integer_value = static_cast<int>(value);
        if (value != static_cast<double>(integer_value))
        {
            status.error = Error::NONINTEGRAL_VALUE;
            return false;
        }
    }
    else if (control_id == CONTROL_GEAR_RANGE)
    {
        if (value < 0.0 || value > 255.0)
        {
            status.error = Error::VALUE_OUT_OF_RANGE;
            return false;
        }
        const int integer_value = static_cast<int>(value);
        if (value != static_cast<double>(integer_value))
        {
            status.error = Error::NONINTEGRAL_VALUE;
            return false;
        }
    }
    else
    {
        status.error = Error::UNKNOWN_CONTROL;
        return false;
    }

    const float narrowed = static_cast<float>(value);
    if (static_cast<double>(narrowed) != value)
    {
        status.error = Error::NON_BINARY32_VALUE;
        return false;
    }
    return true;
}

bool ValidatePersistentOrderAndValues(
    const std::vector<
        DeterministicInputTrace::PersistentControl>& controls,
    std::uint64_t target_id,
    Status& status)
{
    std::uint64_t previous_target = 0;
    std::uint32_t previous_control = 0;
    bool have_previous = false;
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        const DeterministicInputTrace::PersistentControl& control =
            controls[index];
        status.control_id = control.control_id;
        if (control.target_id != target_id)
        {
            status.error = Error::TARGET_MISMATCH;
            return false;
        }
        if (have_previous)
        {
            if (control.target_id < previous_target ||
                (control.target_id == previous_target &&
                 control.control_id < previous_control))
            {
                status.error = Error::NONCANONICAL_ORDER;
                return false;
            }
            if (control.target_id == previous_target &&
                control.control_id == previous_control)
            {
                status.error = Error::DUPLICATE_CONTROL;
                return false;
            }
        }
        if (!IsKnownControl(control.control_id))
        {
            status.error = Error::UNKNOWN_CONTROL;
            return false;
        }
        if (!ValidateControlValue(
                control.control_id,
                control.value,
                status))
        {
            return false;
        }
        if (IsPositiveZero(control.value))
        {
            status.error = Error::NONCANONICAL_ACTIVE_ZERO;
            return false;
        }
        previous_target = control.target_id;
        previous_control = control.control_id;
        have_previous = true;
    }
    return true;
}

bool ValidateDeltaOrderAndValues(
    const std::vector<DeterministicInputTrace::Event>& events,
    std::uint64_t target_id,
    Status& status)
{
    std::uint64_t previous_target = 0;
    std::uint32_t previous_control = 0;
    bool have_previous = false;
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const DeterministicInputTrace::Event& event = events[index];
        status.control_id = event.control_id;
        if (event.target_id != target_id)
        {
            status.error = Error::TARGET_MISMATCH;
            return false;
        }
        if (event.kind != DeterministicInputTrace::EventKind::STATE)
        {
            status.error = Error::UNSUPPORTED_IMPULSE;
            return false;
        }
        if (have_previous)
        {
            if (event.target_id < previous_target ||
                (event.target_id == previous_target &&
                 event.control_id < previous_control))
            {
                status.error = Error::NONCANONICAL_ORDER;
                return false;
            }
            if (event.target_id == previous_target &&
                event.control_id == previous_control)
            {
                status.error = Error::DUPLICATE_CONTROL;
                return false;
            }
        }
        if (!IsKnownControl(event.control_id))
        {
            status.error = Error::UNKNOWN_CONTROL;
            return false;
        }
        if (!ValidateControlValue(
                event.control_id,
                event.value,
                status))
        {
            return false;
        }
        previous_target = event.target_id;
        previous_control = event.control_id;
        have_previous = true;
    }
    return true;
}

bool ApplyDeltas(
    const std::vector<DeterministicInputTrace::Event>& events,
    Snapshot& snapshot,
    Status& status)
{
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const DeterministicInputTrace::Event& event = events[index];
        double previous = 0.0;
        if (!snapshot.Get(event.control_id, previous))
        {
            status.error = Error::UNKNOWN_CONTROL;
            status.control_id = event.control_id;
            return false;
        }
        if (SameBits(previous, event.value))
        {
            status.error = Error::REDUNDANT_DELTA;
            status.control_id = event.control_id;
            return false;
        }
        if (!snapshot.Set(event.control_id, event.value))
        {
            status.error = Error::UNKNOWN_CONTROL;
            status.control_id = event.control_id;
            return false;
        }
    }
    return true;
}

} // namespace

Status::Status():
    error(Error::NONE),
    control_id(0)
{
}

Snapshot::Snapshot():
    schema_version(SNAPSHOT_SCHEMA_VERSION),
    target_id(0),
    values()
{
}

bool Snapshot::Set(
    std::uint32_t control_id,
    double value)
{
    std::size_t slot = 0;
    if (!TryControlSlot(control_id, slot))
        return false;
    values[slot] = value;
    return true;
}

bool Snapshot::Get(
    std::uint32_t control_id,
    double& value) const
{
    std::size_t slot = 0;
    if (!TryControlSlot(control_id, slot))
        return false;
    value = values[slot];
    return true;
}

bool IsKnownControl(std::uint32_t control_id)
{
    std::size_t slot = 0;
    return TryControlSlot(control_id, slot);
}

bool IsCommandControl(std::uint32_t control_id)
{
    return
        control_id >= CONTROL_COMMAND_1 &&
        control_id <= CONTROL_COMMAND_84;
}

std::uint32_t CommandControlId(std::uint32_t command_index)
{
    if (command_index == 0 ||
        command_index > COMMAND_CONTROL_COUNT)
    {
        return 0;
    }
    return
        CONTROL_COMMAND_1 +
        command_index - 1;
}

bool CommandIndex(
    std::uint32_t control_id,
    std::uint32_t& command_index)
{
    if (!IsCommandControl(control_id))
        return false;
    command_index = control_id - CONTROL_COMMAND_1 + 1;
    return true;
}

const char* ControlName(std::uint32_t control_id)
{
    switch (control_id)
    {
    case CONTROL_STEERING_COMMAND:
        return "steering_command";
    case CONTROL_SERVICE_BRAKE:
        return "service_brake";
    case CONTROL_THROTTLE:
        return "throttle";
    case CONTROL_CLUTCH:
        return "clutch";
    case CONTROL_PARKING_BRAKE:
        return "parking_brake";
    case CONTROL_ENGINE_CONTACT:
        return "engine_contact";
    case CONTROL_ENGINE_STARTER:
        return "engine_starter";
    case CONTROL_GEAR:
        return "gear";
    case CONTROL_GEAR_RANGE:
        return "gear_range";
    case CONTROL_HYDRO_SPEED_COUPLING:
        return "hydro_speed_coupling";
    case CONTROL_TRAILER_PARKING_BRAKE:
        return "trailer_parking_brake";
    default:
        return
            IsCommandControl(control_id) ?
                "command_key" :
                "unknown";
    }
}

const char* RegistryManifest()
{
    return REGISTRY_MANIFEST;
}

const char* RegistrySourceName()
{
    return REGISTRY_SOURCE_NAME;
}

bool IsRegistryMetadata(
    const DeterministicInputTrace::Metadata& metadata,
    std::uint64_t target_id)
{
    return
        target_id != 0 &&
        metadata.source_name == REGISTRY_SOURCE_NAME &&
        metadata.stream_id == target_id;
}

bool ValidateSnapshot(
    const Snapshot& snapshot,
    Status& status)
{
    status = Status();
    if (snapshot.schema_version != SNAPSHOT_SCHEMA_VERSION)
    {
        status.error = Error::INVALID_SCHEMA;
        return false;
    }
    if (snapshot.target_id == 0)
    {
        status.error = Error::INVALID_TARGET;
        return false;
    }
    for (std::size_t slot = 0;
        slot < snapshot.values.size();
        ++slot)
    {
        const std::uint32_t control_id = SlotControlId(slot);
        if (!ValidateControlValue(
                control_id,
                snapshot.values[slot],
                status))
        {
            return false;
        }
    }
    status = Status();
    return true;
}

bool BuildSnapshotFromPersistentState(
    std::uint64_t target_id,
    const std::vector<
        DeterministicInputTrace::PersistentControl>& persistent_state,
    Snapshot& snapshot,
    Status& status)
{
    status = Status();
    if (target_id == 0)
    {
        status.error = Error::INVALID_TARGET;
        return false;
    }
    if (!ValidatePersistentOrderAndValues(
            persistent_state,
            target_id,
            status))
    {
        return false;
    }

    Snapshot candidate;
    candidate.target_id = target_id;
    for (std::size_t index = 0;
        index < persistent_state.size();
        ++index)
    {
        const DeterministicInputTrace::PersistentControl& control =
            persistent_state[index];
        if (!candidate.Set(control.control_id, control.value))
        {
            status.error = Error::UNKNOWN_CONTROL;
            status.control_id = control.control_id;
            return false;
        }
    }
    if (!ValidateSnapshot(candidate, status))
        return false;
    snapshot = candidate;
    status = Status();
    return true;
}

RecordingSource::RecordingSource(
    const DeterministicInputTrace::Runtime& runtime,
    std::uint64_t target_id,
    SnapshotProvider& provider):
    m_runtime(&runtime),
    m_target_id(target_id),
    m_provider(provider),
    m_previous(),
    m_status(),
    m_initial_status(),
    m_next_physics_step(runtime.GetNextPhysicsStep()),
    m_ready(false)
{
    m_previous.target_id = target_id;
    if (runtime.GetMode() !=
        DeterministicInputTrace::RuntimeMode::RECORD)
    {
        m_initial_status.error = Error::RUNTIME_MODE_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (target_id == 0)
    {
        m_initial_status.error = Error::INVALID_TARGET;
        m_status = m_initial_status;
        return;
    }
    if (!IsRegistryMetadata(runtime.GetIdentity(), target_id))
    {
        m_initial_status.error =
            Error::METADATA_SCHEMA_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (!BuildSnapshotFromPersistentState(
            target_id,
            runtime.GetPersistentState().GetControls(),
            m_previous,
            m_initial_status))
    {
        m_status = m_initial_status;
        return;
    }
    m_ready = true;
}

bool RecordingSource::AcceptsRuntime(
    const DeterministicInputTrace::Runtime& runtime) const
{
    return m_runtime == &runtime;
}

bool RecordingSource::SampleFixedStepStart(
    std::uint64_t physics_step,
    DeterministicInputTrace::SampleCollector& collector)
{
    m_status = Status();
    if (!m_ready)
    {
        m_status = m_initial_status;
        return false;
    }
    if (physics_step != m_next_physics_step)
    {
        m_status.error = Error::FIXED_STEP_MISMATCH;
        return false;
    }
    if (m_target_id == 0)
    {
        m_status.error = Error::INVALID_TARGET;
        return false;
    }

    Snapshot captured;
    captured.target_id = m_target_id;
    if (!m_provider.CaptureAppliedControls(
            physics_step,
            captured))
    {
        m_status.error = Error::PROVIDER_REJECTED;
        return false;
    }
    if (captured.target_id != m_target_id)
    {
        m_status.error = Error::TARGET_MISMATCH;
        return false;
    }
    if (!ValidateSnapshot(captured, m_status))
        return false;

    for (std::size_t slot = 0;
        slot < captured.values.size();
        ++slot)
    {
        if (SameBits(
                captured.values[slot],
                m_previous.values[slot]))
        {
            continue;
        }
        const std::uint32_t control_id = SlotControlId(slot);
        if (!collector.AddPersistentDelta(
                m_target_id,
                control_id,
                captured.values[slot]))
        {
            m_status.error = Error::COLLECTOR_REJECTED;
            m_status.control_id = control_id;
            return false;
        }
    }

    m_previous = captured;
    ++m_next_physics_step;
    m_status = Status();
    return true;
}

const Snapshot& RecordingSource::GetPreviousSnapshot() const
{
    return m_previous;
}

const Status& RecordingSource::GetStatus() const
{
    return m_status;
}

ReplaySink::ReplaySink(
    const DeterministicInputTrace::Runtime& runtime,
    std::uint64_t target_id,
    SnapshotConsumer& consumer):
    m_runtime(&runtime),
    m_target_id(target_id),
    m_consumer(consumer),
    m_previous(),
    m_status(),
    m_initial_status(),
    m_next_physics_step(runtime.GetNextPhysicsStep()),
    m_ready(false)
{
    m_previous.target_id = target_id;
    if (runtime.GetMode() !=
        DeterministicInputTrace::RuntimeMode::REPLAY)
    {
        m_initial_status.error = Error::RUNTIME_MODE_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (target_id == 0)
    {
        m_initial_status.error = Error::INVALID_TARGET;
        m_status = m_initial_status;
        return;
    }
    if (!IsRegistryMetadata(runtime.GetIdentity(), target_id))
    {
        m_initial_status.error =
            Error::METADATA_SCHEMA_MISMATCH;
        m_status = m_initial_status;
        return;
    }
    if (!BuildSnapshotFromPersistentState(
            target_id,
            runtime.GetPersistentState().GetControls(),
            m_previous,
            m_initial_status))
    {
        m_status = m_initial_status;
        return;
    }
    m_ready = true;
}

bool ReplaySink::AcceptsRuntime(
    const DeterministicInputTrace::Runtime& runtime) const
{
    return m_runtime == &runtime;
}

bool ReplaySink::InjectFixedStepStart(
    const DeterministicInputTrace::StepInjection& injection)
{
    m_status = Status();
    if (!m_ready)
    {
        m_status = m_initial_status;
        return false;
    }
    if (injection.physics_step != m_next_physics_step)
    {
        m_status.error = Error::FIXED_STEP_MISMATCH;
        return false;
    }
    if (m_target_id == 0)
    {
        m_status.error = Error::INVALID_TARGET;
        return false;
    }
    if (!injection.impulses.empty())
    {
        m_status.error = Error::UNSUPPORTED_IMPULSE;
        m_status.control_id =
            injection.impulses.front().control_id;
        return false;
    }
    if (!ValidateDeltaOrderAndValues(
            injection.persistent_deltas,
            m_target_id,
            m_status))
    {
        return false;
    }
    if (!ValidatePersistentOrderAndValues(
            injection.persistent_state,
            m_target_id,
            m_status))
    {
        return false;
    }

    Snapshot snapshot;
    if (!BuildSnapshotFromPersistentState(
            m_target_id,
            injection.persistent_state,
            snapshot,
            m_status))
    {
        return false;
    }

    Snapshot delta_result = m_previous;
    if (!ApplyDeltas(
            injection.persistent_deltas,
            delta_result,
            m_status))
    {
        return false;
    }
    if (!SameSnapshot(delta_result, snapshot))
    {
        m_status.error = Error::STATE_DELTA_MISMATCH;
        return false;
    }
    if (!m_consumer.ApplyAppliedControls(
            injection.physics_step,
            snapshot))
    {
        m_status.error = Error::CONSUMER_REJECTED;
        return false;
    }

    m_previous = snapshot;
    ++m_next_physics_step;
    m_status = Status();
    return true;
}

const Snapshot& ReplaySink::GetPreviousSnapshot() const
{
    return m_previous;
}

const Status& ReplaySink::GetStatus() const
{
    return m_status;
}

const char* ToString(Error error)
{
    switch (error)
    {
    case Error::NONE:
        return "none";
    case Error::INVALID_SCHEMA:
        return "invalid schema";
    case Error::INVALID_TARGET:
        return "invalid target";
    case Error::UNKNOWN_CONTROL:
        return "unknown control";
    case Error::NONFINITE_VALUE:
        return "non-finite value";
    case Error::NEGATIVE_ZERO:
        return "negative zero";
    case Error::NON_BINARY32_VALUE:
        return "value is not exactly binary32";
    case Error::VALUE_OUT_OF_RANGE:
        return "value out of range";
    case Error::NONINTEGRAL_VALUE:
        return "non-integral value";
    case Error::NONCANONICAL_ACTIVE_ZERO:
        return "noncanonical active zero";
    case Error::PROVIDER_REJECTED:
        return "provider rejected";
    case Error::COLLECTOR_REJECTED:
        return "collector rejected";
    case Error::TARGET_MISMATCH:
        return "target mismatch";
    case Error::NONCANONICAL_ORDER:
        return "noncanonical order";
    case Error::DUPLICATE_CONTROL:
        return "duplicate control";
    case Error::UNSUPPORTED_IMPULSE:
        return "unsupported impulse";
    case Error::REDUNDANT_DELTA:
        return "redundant delta";
    case Error::STATE_DELTA_MISMATCH:
        return "state and delta mismatch";
    case Error::RUNTIME_MODE_MISMATCH:
        return "runtime mode mismatch";
    case Error::METADATA_SCHEMA_MISMATCH:
        return "metadata schema mismatch";
    case Error::FIXED_STEP_MISMATCH:
        return "fixed-step mismatch";
    case Error::CONSUMER_REJECTED:
        return "consumer rejected";
    }
    return "unknown";
}

} // namespace DeterministicVehicleInput
} // namespace RoR
