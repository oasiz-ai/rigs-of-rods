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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace Input = RoR::DeterministicInputTrace;
namespace Vehicle = RoR::DeterministicVehicleInput;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::fprintf(
            stderr,
            "FAIL line %d: %s\n",
            line,
            expression);
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

std::uint64_t DoubleBits(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double DoubleFromBits(std::uint64_t bits)
{
    // Preserve hostile IEEE-754 payloads even when the test is compiled with
    // the game's -ffast-math floating-point mode. A plain memcpy can be
    // optimized as an ordinary floating assignment after constant folding.
    double value = 0.0;
    const volatile unsigned char* const source =
        reinterpret_cast<const volatile unsigned char*>(&bits);
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&value);
    for (std::size_t index = 0U; index < sizeof(value); ++index)
        destination[index] = source[index];
    return value;
}

double F32(double value)
{
    return static_cast<double>(
        static_cast<float>(value));
}

bool SameSnapshot(
    const Vehicle::Snapshot& first,
    const Vehicle::Snapshot& second)
{
    if (first.schema_version != second.schema_version ||
        first.target_id != second.target_id)
    {
        return false;
    }
    for (std::size_t index = 0;
        index < first.values.size();
        ++index)
    {
        if (DoubleBits(first.values[index]) !=
            DoubleBits(second.values[index]))
        {
            return false;
        }
    }
    return true;
}

Input::Metadata ValidMetadata(
    std::uint64_t first_step,
    std::uint64_t target_id)
{
    Input::Metadata metadata;
    metadata.scenario_id = UINT64_C(0x72095d8335918c3b);
    metadata.stream_id = target_id;
    metadata.first_physics_step = first_step;
    metadata.physics_step_numerator = 1;
    metadata.physics_step_denominator = 2000;
    metadata.scenario_name = "vehicle-input-test";
    metadata.source_name = Vehicle::RegistrySourceName();
    for (std::size_t index = 0;
        index < metadata.source_digest.bytes.size();
        ++index)
    {
        metadata.source_digest.bytes[index] =
            static_cast<std::uint8_t>(
                1U + ((index * 37U) & 0xffU));
    }
    return metadata;
}

Vehicle::Snapshot BaseSnapshot(std::uint64_t target_id)
{
    Vehicle::Snapshot snapshot;
    snapshot.target_id = target_id;
    return snapshot;
}

class SequenceProvider:
    public Vehicle::SnapshotProvider
{
public:
    SequenceProvider(
        const std::vector<Vehicle::Snapshot>& snapshots,
        std::uint64_t first_step):
        m_snapshots(snapshots),
        m_first_step(first_step)
    {
    }

    bool CaptureAppliedControls(
        std::uint64_t physics_step,
        Vehicle::Snapshot& snapshot) override
    {
        ++calls;
        if (reject ||
            physics_step != m_first_step + next ||
            next >= m_snapshots.size())
        {
            return false;
        }
        snapshot = m_snapshots[next++];
        return true;
    }

    std::size_t calls = 0;
    std::size_t next = 0;
    bool reject = false;

private:
    const std::vector<Vehicle::Snapshot>& m_snapshots;
    std::uint64_t m_first_step;
};

class CollectingConsumer:
    public Vehicle::SnapshotConsumer
{
public:
    bool ApplyAppliedControls(
        std::uint64_t physics_step,
        const Vehicle::Snapshot& snapshot) override
    {
        ++calls;
        if (reject)
            return false;
        steps.push_back(physics_step);
        snapshots.push_back(snapshot);
        return true;
    }

    std::size_t calls = 0;
    bool reject = false;
    std::vector<std::uint64_t> steps;
    std::vector<Vehicle::Snapshot> snapshots;
};

class CappedGearConsumer:
    public Vehicle::SnapshotConsumer
{
public:
    explicit CappedGearConsumer(int maximum_gear):
        m_maximum_gear(maximum_gear)
    {
    }

    bool ApplyAppliedControls(
        std::uint64_t,
        const Vehicle::Snapshot& snapshot) override
    {
        ++calls;
        double gear = 0.0;
        if (!snapshot.Get(Vehicle::CONTROL_GEAR, gear) ||
            gear > static_cast<double>(m_maximum_gear))
        {
            return false;
        }
        ++mutations;
        return true;
    }

    std::size_t calls = 0;
    std::size_t mutations = 0;

private:
    int m_maximum_gear;
};

bool RecordSnapshots(
    const std::vector<Vehicle::Snapshot>& snapshots,
    std::uint64_t first_step,
    std::uint64_t target_id,
    std::string& trace)
{
    SequenceProvider provider(snapshots, first_step);
    Input::Runtime runtime;
    if (!runtime.BeginRecording(
            ValidMetadata(first_step, target_id)))
        return false;
    Vehicle::RecordingSource source(
        runtime,
        target_id,
        provider);
    for (std::size_t index = 0;
        index < snapshots.size();
        ++index)
    {
        if (!runtime.RecordFixedStep(
                first_step + index,
                source))
        {
            return false;
        }
    }
    return runtime.FinalizeRecording(trace);
}

void TestRegistryAndValidation()
{
    CHECK(Vehicle::SNAPSHOT_SCHEMA_VERSION == 1);
    CHECK(Vehicle::STANDARD_CONTROL_COUNT == 11);
    CHECK(Vehicle::COMMAND_CONTROL_COUNT == 84);
    CHECK(Vehicle::CONTROL_SLOT_COUNT == 95);
    CHECK(Vehicle::IsKnownControl(
        Vehicle::CONTROL_STEERING_COMMAND));
    CHECK(Vehicle::IsKnownControl(
        Vehicle::CONTROL_TRAILER_PARKING_BRAKE));
    CHECK(Vehicle::IsKnownControl(
        Vehicle::CONTROL_COMMAND_1));
    CHECK(Vehicle::IsKnownControl(
        Vehicle::CONTROL_COMMAND_84));
    CHECK(!Vehicle::IsKnownControl(0));
    CHECK(!Vehicle::IsKnownControl(12));
    CHECK(!Vehicle::IsKnownControl(1023));
    CHECK(!Vehicle::IsKnownControl(1108));
    CHECK(
        std::strcmp(
            Vehicle::RegistrySourceName(),
            "ror-restricted-applied-control-state-v1+sha256:"
            "5368675b48c68ee2804455ed0577bc5069aab2fa50a210c3dbe9d28785057f95") ==
        0);
    CHECK(
        std::strstr(
            Vehicle::RegistryManifest(),
            "semantics=restricted-applied-control-state\n") !=
        nullptr);
    CHECK(
        std::strstr(
            Vehicle::RegistryManifest(),
            "target_identity=stream_id\n") !=
        nullptr);
    CHECK(
        std::strstr(
            Vehicle::RegistryManifest(),
            "1024..1107=command_key_1..84[0,1]\n") !=
        nullptr);
    Input::Metadata registry_metadata = ValidMetadata(0, 7);
    CHECK(Vehicle::IsRegistryMetadata(registry_metadata, 7));
    CHECK(!Vehicle::IsRegistryMetadata(registry_metadata, 8));
    registry_metadata.source_name += "-changed";
    CHECK(!Vehicle::IsRegistryMetadata(registry_metadata, 7));
    CHECK(Vehicle::CommandControlId(0) == 0);
    CHECK(
        Vehicle::CommandControlId(1) ==
        Vehicle::CONTROL_COMMAND_1);
    CHECK(
        Vehicle::CommandControlId(84) ==
        Vehicle::CONTROL_COMMAND_84);
    CHECK(Vehicle::CommandControlId(85) == 0);

    for (std::uint32_t index = 1; index <= 84; ++index)
    {
        const std::uint32_t control_id =
            Vehicle::CommandControlId(index);
        std::uint32_t recovered = 0;
        CHECK(Vehicle::CommandIndex(control_id, recovered));
        CHECK(recovered == index);
    }
    std::uint32_t command_index = 99;
    CHECK(!Vehicle::CommandIndex(
        Vehicle::CONTROL_THROTTLE,
        command_index));
    CHECK(command_index == 99);

    Vehicle::Snapshot snapshot = BaseSnapshot(7);
    Vehicle::Status status;
    CHECK(Vehicle::ValidateSnapshot(snapshot, status));
    CHECK(status.error == Vehicle::Error::NONE);
    for (std::size_t index = 0;
        index < snapshot.values.size();
        ++index)
    {
        CHECK(DoubleBits(snapshot.values[index]) == 0);
    }

    double value = 99.0;
    CHECK(snapshot.Set(Vehicle::CONTROL_THROTTLE, 0.75));
    CHECK(snapshot.Get(Vehicle::CONTROL_THROTTLE, value));
    CHECK(value == 0.75);
    CHECK(!snapshot.Set(999, 1.0));
    CHECK(!snapshot.Get(999, value));
    CHECK(Vehicle::ValidateSnapshot(snapshot, status));

    snapshot = BaseSnapshot(7);
    snapshot.Set(Vehicle::CONTROL_THROTTLE, 0.1);
    CHECK(!Vehicle::ValidateSnapshot(snapshot, status));
    CHECK(
        status.error ==
        Vehicle::Error::NON_BINARY32_VALUE);
    CHECK(
        status.control_id ==
        Vehicle::CONTROL_THROTTLE);

    snapshot.schema_version = 2;
    CHECK(!Vehicle::ValidateSnapshot(snapshot, status));
    CHECK(status.error == Vehicle::Error::INVALID_SCHEMA);
    snapshot.schema_version = Vehicle::SNAPSHOT_SCHEMA_VERSION;
    snapshot.target_id = 0;
    CHECK(!Vehicle::ValidateSnapshot(snapshot, status));
    CHECK(status.error == Vehicle::Error::INVALID_TARGET);
    snapshot.target_id = 7;

    struct InvalidValue
    {
        std::uint32_t control_id;
        double value;
        Vehicle::Error error;
    };
    const InvalidValue invalid_values[] = {
        {Vehicle::CONTROL_STEERING_COMMAND, -1.0001,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_STEERING_COMMAND, 1.0001,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_SERVICE_BRAKE, -0.1,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_THROTTLE, 1.1,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_CLUTCH, 1.1,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_PARKING_BRAKE, 0.5,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_ENGINE_CONTACT, -1.0,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_ENGINE_STARTER, 2.0,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_GEAR, -2.0,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_GEAR, 2.5,
            Vehicle::Error::NONINTEGRAL_VALUE},
        {Vehicle::CONTROL_GEAR_RANGE, -1.0,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_GEAR_RANGE, 1.25,
            Vehicle::Error::NONINTEGRAL_VALUE},
        {Vehicle::CONTROL_COMMAND_1, 1.01,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_COMMAND_84, -0.01,
            Vehicle::Error::VALUE_OUT_OF_RANGE},
        {Vehicle::CONTROL_THROTTLE,
            DoubleFromBits(UINT64_C(0x8000000000000000)),
            Vehicle::Error::NEGATIVE_ZERO},
        {Vehicle::CONTROL_THROTTLE,
            DoubleFromBits(UINT64_C(0x7ff8000000000042)),
            Vehicle::Error::NONFINITE_VALUE},
        {Vehicle::CONTROL_THROTTLE,
            DoubleFromBits(UINT64_C(0x7ff0000000000000)),
            Vehicle::Error::NONFINITE_VALUE}
    };

    for (std::size_t index = 0;
        index <
            sizeof(invalid_values) /
                sizeof(invalid_values[0]);
        ++index)
    {
        Vehicle::Snapshot invalid = BaseSnapshot(7);
        CHECK(invalid.Set(
            invalid_values[index].control_id,
            invalid_values[index].value));
        CHECK(!Vehicle::ValidateSnapshot(invalid, status));
        CHECK(status.error == invalid_values[index].error);
        CHECK(
            status.control_id ==
            invalid_values[index].control_id);
    }

    CHECK(
        std::strcmp(
            Vehicle::ControlName(
                Vehicle::CONTROL_STEERING_COMMAND),
            "steering_command") == 0);
    CHECK(
        std::strcmp(
            Vehicle::ControlName(
                Vehicle::CONTROL_COMMAND_1),
            "command_key") == 0);
    CHECK(
        std::strcmp(
            Vehicle::ControlName(999),
            "unknown") == 0);
}

std::vector<Vehicle::Snapshot> RepresentativeSnapshots(
    std::uint64_t target_id)
{
    std::vector<Vehicle::Snapshot> snapshots(5);
    for (std::size_t index = 0; index < snapshots.size(); ++index)
        snapshots[index].target_id = target_id;

    snapshots[0].Set(
        Vehicle::CONTROL_STEERING_COMMAND,
        -0.25);
    snapshots[0].Set(
        Vehicle::CONTROL_SERVICE_BRAKE,
        0.125);
    snapshots[0].Set(Vehicle::CONTROL_THROTTLE, 0.75);
    snapshots[0].Set(Vehicle::CONTROL_CLUTCH, 1.0);
    snapshots[0].Set(
        Vehicle::CONTROL_ENGINE_CONTACT,
        1.0);
    snapshots[0].Set(
        Vehicle::CONTROL_ENGINE_STARTER,
        1.0);
    snapshots[0].Set(Vehicle::CONTROL_GEAR, 3.0);
    snapshots[0].Set(Vehicle::CONTROL_GEAR_RANGE, 1.0);
    snapshots[0].Set(
        Vehicle::CONTROL_HYDRO_SPEED_COUPLING,
        1.0);
    snapshots[0].Set(
        Vehicle::CONTROL_COMMAND_1,
        F32(0.4));
    snapshots[0].Set(Vehicle::CONTROL_COMMAND_84, 1.0);

    snapshots[1] = snapshots[0];

    snapshots[2] = snapshots[1];
    snapshots[2].Set(
        Vehicle::CONTROL_ENGINE_STARTER,
        0.0);
    snapshots[2].Set(Vehicle::CONTROL_COMMAND_1, 0.0);
    snapshots[2].Set(Vehicle::CONTROL_THROTTLE, 0.0);
    snapshots[2].Set(
        Vehicle::CONTROL_SERVICE_BRAKE,
        1.0);
    snapshots[2].Set(
        Vehicle::CONTROL_PARKING_BRAKE,
        1.0);

    snapshots[3] = snapshots[2];
    snapshots[3].Set(
        Vehicle::CONTROL_STEERING_COMMAND,
        1.0);
    snapshots[3].Set(Vehicle::CONTROL_GEAR, -1.0);
    snapshots[3].Set(
        Vehicle::CONTROL_TRAILER_PARKING_BRAKE,
        1.0);

    snapshots[4] = BaseSnapshot(target_id);
    return snapshots;
}

void TestAuthenticatedRoundTrip()
{
    const std::uint64_t target_id = UINT64_C(0x8b71a421);
    const std::uint64_t first_step = UINT64_C(7000000000);
    const std::vector<Vehicle::Snapshot> snapshots =
        RepresentativeSnapshots(target_id);
    SequenceProvider provider(snapshots, first_step);
    Input::Runtime recorder;
    CHECK(recorder.BeginRecording(
        ValidMetadata(first_step, target_id)));
    Vehicle::RecordingSource source(
        recorder,
        target_id,
        provider);

    for (std::size_t index = 0;
        index < snapshots.size();
        ++index)
    {
        CHECK(recorder.RecordFixedStep(
            first_step + index,
            source));
    }
    CHECK(provider.calls == snapshots.size());
    CHECK(provider.next == snapshots.size());
    CHECK(SameSnapshot(
        source.GetPreviousSnapshot(),
        snapshots.back()));
    CHECK(source.GetStatus().error == Vehicle::Error::NONE);

    std::string trace;
    CHECK(recorder.FinalizeRecording(trace));
    CHECK(!trace.empty());
    CHECK(
        recorder.GetLifecycle() ==
        Input::RuntimeLifecycle::FINISHED);

    Input::Runtime replay;
    CHECK(replay.BeginReplay(
        trace,
        ValidMetadata(first_step, target_id)));
    CollectingConsumer consumer;
    Vehicle::ReplaySink sink(
        replay,
        target_id,
        consumer);
    for (std::size_t index = 0;
        index < snapshots.size();
        ++index)
    {
        CHECK(replay.ReplayFixedStep(
            first_step + index,
            sink));
    }
    CHECK(
        replay.GetLifecycle() ==
        Input::RuntimeLifecycle::FINISHED);
    CHECK(consumer.calls == snapshots.size());
    CHECK(consumer.steps.size() == snapshots.size());
    CHECK(consumer.snapshots.size() == snapshots.size());
    if (consumer.steps.size() != snapshots.size() ||
        consumer.snapshots.size() != snapshots.size())
    {
        return;
    }
    for (std::size_t index = 0;
        index < snapshots.size();
        ++index)
    {
        CHECK(consumer.steps[index] == first_step + index);
        CHECK(SameSnapshot(
            consumer.snapshots[index],
            snapshots[index]));
    }
    CHECK(sink.GetStatus().error == Vehicle::Error::NONE);
}

void TestRecordingFailuresAreTransactional()
{
    const std::uint64_t target_id = 91;
    const std::uint64_t first_step = 400;
    std::vector<Vehicle::Snapshot> snapshots(1);
    snapshots[0] = BaseSnapshot(target_id);
    snapshots[0].Set(Vehicle::CONTROL_THROTTLE, 0.5);
    snapshots[0].Set(Vehicle::CONTROL_CLUTCH, 1.0);

    SequenceProvider provider(snapshots, first_step);
    provider.reject = true;
    Input::Runtime runtime;
    CHECK(runtime.BeginRecording(
        ValidMetadata(first_step, target_id)));
    Vehicle::RecordingSource source(
        runtime,
        target_id,
        provider);
    CHECK(!runtime.RecordFixedStep(first_step, source));
    CHECK(
        runtime.GetLifecycle() ==
        Input::RuntimeLifecycle::FAULTED);
    CHECK(
        source.GetStatus().error ==
        Vehicle::Error::PROVIDER_REJECTED);
    CHECK(
        SameSnapshot(
            source.GetPreviousSnapshot(),
            BaseSnapshot(target_id)));

    SequenceProvider limited_provider(snapshots, first_step);
    Input::Limits limits;
    limits.max_events_per_step = 1;
    Input::Runtime limited_runtime;
    CHECK(limited_runtime.BeginRecording(
        ValidMetadata(first_step, target_id),
        limits));
    Vehicle::RecordingSource limited_source(
        limited_runtime,
        target_id,
        limited_provider);
    CHECK(!limited_runtime.RecordFixedStep(
        first_step,
        limited_source));
    CHECK(
        limited_source.GetStatus().error ==
        Vehicle::Error::COLLECTOR_REJECTED);
    CHECK(
        SameSnapshot(
            limited_source.GetPreviousSnapshot(),
            BaseSnapshot(target_id)));

    SequenceProvider wrong_target_provider(
        snapshots,
        first_step);
    snapshots[0].target_id = target_id + 1;
    Input::Runtime wrong_target_runtime;
    CHECK(wrong_target_runtime.BeginRecording(
        ValidMetadata(first_step, target_id)));
    Vehicle::RecordingSource wrong_target_source(
        wrong_target_runtime,
        target_id,
        wrong_target_provider);
    CHECK(!wrong_target_runtime.RecordFixedStep(
        first_step,
        wrong_target_source));
    CHECK(
        wrong_target_source.GetStatus().error ==
        Vehicle::Error::TARGET_MISMATCH);

    SequenceProvider zero_provider(snapshots, first_step);
    Input::Runtime zero_runtime;
    CHECK(zero_runtime.BeginRecording(
        ValidMetadata(first_step, target_id)));
    Vehicle::RecordingSource zero_source(
        zero_runtime,
        0,
        zero_provider);
    CHECK(!zero_runtime.RecordFixedStep(
        first_step,
        zero_source));
    CHECK(
        zero_source.GetStatus().error ==
        Vehicle::Error::INVALID_TARGET);
}

Input::PersistentControl Persistent(
    std::uint64_t target_id,
    std::uint32_t control_id,
    double value)
{
    Input::PersistentControl control;
    control.target_id = target_id;
    control.control_id = control_id;
    control.value = value;
    return control;
}

Input::Event Event(
    std::uint64_t target_id,
    std::uint32_t control_id,
    Input::EventKind kind,
    double value)
{
    Input::Event event;
    event.target_id = target_id;
    event.control_id = control_id;
    event.kind = kind;
    event.value = value;
    return event;
}

std::string OneEmptyStepTrace(
    std::uint64_t physics_step,
    std::uint64_t target_id)
{
    std::ostringstream output(
        std::ios::out | std::ios::binary);
    Input::Writer writer(
        output,
        ValidMetadata(physics_step, target_id));
    CHECK(writer.IsReady());
    Input::Frame frame;
    frame.physics_step = physics_step;
    CHECK(writer.Append(frame));
    CHECK(writer.Finish());
    return output.str();
}

void ExpectReplayFailure(
    const Input::StepInjection& injection,
    std::uint64_t target_id,
    Vehicle::Error expected)
{
    Input::Runtime runtime;
    CHECK(runtime.BeginReplay(
        OneEmptyStepTrace(injection.physics_step, target_id),
        ValidMetadata(injection.physics_step, target_id)));
    CollectingConsumer consumer;
    Vehicle::ReplaySink sink(
        runtime,
        target_id,
        consumer);
    CHECK(!sink.InjectFixedStepStart(injection));
    CHECK(sink.GetStatus().error == expected);
    CHECK(consumer.calls == 0);
    CHECK(consumer.snapshots.empty());
}

void TestReplayRejectsHostileBatches()
{
    const std::uint64_t target_id = 55;
    Input::StepInjection injection;
    injection.physics_step = 100;

    injection.persistent_state.push_back(
        Persistent(
            target_id + 1,
            Vehicle::CONTROL_THROTTLE,
            0.5));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::TARGET_MISMATCH);

    injection = Input::StepInjection();
    injection.persistent_state.push_back(
        Persistent(target_id, 999, 0.5));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::UNKNOWN_CONTROL);

    injection = Input::StepInjection();
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            0.5));
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_SERVICE_BRAKE,
            0.5));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::NONCANONICAL_ORDER);

    injection = Input::StepInjection();
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            0.5));
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            0.25));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::DUPLICATE_CONTROL);

    injection = Input::StepInjection();
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            1.5));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::VALUE_OUT_OF_RANGE);

    injection = Input::StepInjection();
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            0.0));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::NONCANONICAL_ACTIVE_ZERO);

    injection = Input::StepInjection();
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            DoubleFromBits(UINT64_C(0x8000000000000000))));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::NEGATIVE_ZERO);

    injection = Input::StepInjection();
    injection.impulses.push_back(
        Event(
            target_id,
            Vehicle::CONTROL_ENGINE_STARTER,
            Input::EventKind::IMPULSE,
            1.0));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::UNSUPPORTED_IMPULSE);

    injection = Input::StepInjection();
    injection.persistent_deltas.push_back(
        Event(
            target_id + 1,
            Vehicle::CONTROL_THROTTLE,
            Input::EventKind::STATE,
            0.5));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::TARGET_MISMATCH);

    injection = Input::StepInjection();
    injection.persistent_deltas.push_back(
        Event(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            Input::EventKind::IMPULSE,
            0.5));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::UNSUPPORTED_IMPULSE);

    injection = Input::StepInjection();
    injection.persistent_deltas.push_back(
        Event(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            Input::EventKind::STATE,
            0.25));
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            0.5));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::STATE_DELTA_MISMATCH);

    injection = Input::StepInjection();
    injection.persistent_deltas.push_back(
        Event(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            Input::EventKind::STATE,
            0.0));
    ExpectReplayFailure(
        injection,
        target_id,
        Vehicle::Error::REDUNDANT_DELTA);

    Input::Runtime repeated_runtime;
    CHECK(repeated_runtime.BeginReplay(
        OneEmptyStepTrace(50, target_id),
        ValidMetadata(50, target_id)));
    CollectingConsumer repeated_consumer;
    Vehicle::ReplaySink repeated_sink(
        repeated_runtime,
        target_id,
        repeated_consumer);
    injection = Input::StepInjection();
    injection.physics_step = 50;
    injection.persistent_deltas.push_back(
        Event(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            Input::EventKind::STATE,
            0.5));
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            0.5));
    CHECK(repeated_sink.InjectFixedStepStart(injection));
    injection.physics_step = 51;
    CHECK(!repeated_sink.InjectFixedStepStart(injection));
    CHECK(
        repeated_sink.GetStatus().error ==
        Vehicle::Error::REDUNDANT_DELTA);
    CHECK(repeated_consumer.calls == 1);

    Input::Runtime wrong_step_runtime;
    CHECK(wrong_step_runtime.BeginReplay(
        OneEmptyStepTrace(70, target_id),
        ValidMetadata(70, target_id)));
    CollectingConsumer wrong_step_consumer;
    Vehicle::ReplaySink wrong_step_sink(
        wrong_step_runtime,
        target_id,
        wrong_step_consumer);
    injection = Input::StepInjection();
    injection.physics_step = 71;
    CHECK(!wrong_step_sink.InjectFixedStepStart(injection));
    CHECK(
        wrong_step_sink.GetStatus().error ==
        Vehicle::Error::FIXED_STEP_MISMATCH);
    CHECK(wrong_step_consumer.calls == 0);

    CollectingConsumer rejecting_consumer;
    rejecting_consumer.reject = true;
    injection = Input::StepInjection();
    injection.physics_step = 123;
    Input::Runtime rejecting_runtime;
    CHECK(rejecting_runtime.BeginReplay(
        OneEmptyStepTrace(injection.physics_step, target_id),
        ValidMetadata(injection.physics_step, target_id)));
    Vehicle::ReplaySink rejecting_sink(
        rejecting_runtime,
        target_id,
        rejecting_consumer);
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            0.5));
    injection.persistent_deltas.push_back(
        Event(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            Input::EventKind::STATE,
            0.5));
    CHECK(!rejecting_sink.InjectFixedStepStart(injection));
    CHECK(
        rejecting_sink.GetStatus().error ==
        Vehicle::Error::CONSUMER_REJECTED);
    CHECK(rejecting_consumer.calls == 1);

    CappedGearConsumer capped_consumer(6);
    Input::Runtime capped_runtime;
    CHECK(capped_runtime.BeginReplay(
        OneEmptyStepTrace(222, target_id),
        ValidMetadata(222, target_id)));
    Vehicle::ReplaySink capped_sink(
        capped_runtime,
        target_id,
        capped_consumer);
    injection = Input::StepInjection();
    injection.physics_step = 222;
    injection.persistent_deltas.push_back(
        Event(
            target_id,
            Vehicle::CONTROL_GEAR,
            Input::EventKind::STATE,
            10.0));
    injection.persistent_state.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_GEAR,
            10.0));
    CHECK(!capped_sink.InjectFixedStepStart(injection));
    CHECK(
        capped_sink.GetStatus().error ==
        Vehicle::Error::CONSUMER_REJECTED);
    CHECK(capped_consumer.calls == 1);
    CHECK(capped_consumer.mutations == 0);

    CollectingConsumer zero_consumer;
    injection = Input::StepInjection();
    Input::Runtime zero_runtime;
    CHECK(zero_runtime.BeginReplay(
        OneEmptyStepTrace(injection.physics_step, target_id),
        ValidMetadata(injection.physics_step, target_id)));
    Vehicle::ReplaySink zero_sink(
        zero_runtime,
        0,
        zero_consumer);
    CHECK(!zero_sink.InjectFixedStepStart(injection));
    CHECK(
        zero_sink.GetStatus().error ==
        Vehicle::Error::INVALID_TARGET);
    CHECK(zero_consumer.calls == 0);
}

class FixedRandom
{
public:
    explicit FixedRandom(std::uint64_t seed):
        m_state(seed)
    {
    }

    std::uint64_t Next()
    {
        std::uint64_t value = m_state;
        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;
        m_state = value;
        return value * UINT64_C(2685821657736338717);
    }

    double Unit()
    {
        return static_cast<double>(Next() >> 11) *
            (1.0 / 9007199254740992.0);
    }

private:
    std::uint64_t m_state;
};

void TestFixedSeedLongContinuation()
{
    const std::uint64_t target_id = UINT64_C(0xf6e57891);
    const std::uint64_t first_step = 9000;
    const std::size_t step_count = 4096;
    std::vector<Vehicle::Snapshot> snapshots;
    snapshots.reserve(step_count);
    FixedRandom random(UINT64_C(0x64fcfcb1a452a25d));
    Vehicle::Snapshot current = BaseSnapshot(target_id);
    for (std::size_t step = 0; step < step_count; ++step)
    {
        if ((random.Next() & 7U) == 0)
        {
            current.Set(
                Vehicle::CONTROL_STEERING_COMMAND,
                F32(2.0 * random.Unit() - 1.0));
        }
        if ((random.Next() & 15U) == 0)
        {
            current.Set(
                Vehicle::CONTROL_THROTTLE,
                F32(random.Unit()));
        }
        if ((random.Next() & 15U) == 0)
        {
            current.Set(
                Vehicle::CONTROL_SERVICE_BRAKE,
                F32(random.Unit()));
        }
        if ((random.Next() & 31U) == 0)
        {
            current.Set(
                Vehicle::CONTROL_PARKING_BRAKE,
                (random.Next() & 1U) == 0 ? 0.0 : 1.0);
        }
        if ((random.Next() & 31U) == 0)
        {
            current.Set(
                Vehicle::CONTROL_GEAR,
                static_cast<double>(
                    static_cast<int>(random.Next() % 9U) - 1));
        }
        const std::uint32_t command =
            1U + static_cast<std::uint32_t>(
                random.Next() %
                Vehicle::COMMAND_CONTROL_COUNT);
        if ((random.Next() & 3U) == 0)
        {
            current.Set(
                Vehicle::CommandControlId(command),
                (random.Next() & 1U) == 0 ?
                    0.0 :
                    F32(random.Unit()));
        }
        snapshots.push_back(current);
    }

    std::string trace;
    CHECK(RecordSnapshots(
        snapshots,
        first_step,
        target_id,
        trace));
    CHECK(!trace.empty());

    Input::Runtime replay;
    CHECK(replay.BeginReplay(
        trace,
        ValidMetadata(first_step, target_id)));
    CollectingConsumer consumer;
    Vehicle::ReplaySink sink(
        replay,
        target_id,
        consumer);
    for (std::size_t step = 0; step < step_count; ++step)
    {
        CHECK(replay.ReplayFixedStep(
            first_step + step,
            sink));
        if (g_failures != 0)
            return;
    }
    CHECK(consumer.snapshots.size() == step_count);
    for (std::size_t step = 0; step < step_count; ++step)
    {
        CHECK(SameSnapshot(
            consumer.snapshots[step],
            snapshots[step]));
    }
}

void TestRuntimeContinuationRestoresAdapterState()
{
    const std::uint64_t target_id = UINT64_C(0xe2749a31);
    const std::uint64_t first_step = 17000;
    const std::vector<Vehicle::Snapshot> snapshots =
        RepresentativeSnapshots(target_id);

    std::string uninterrupted_trace;
    CHECK(RecordSnapshots(
        snapshots,
        first_step,
        target_id,
        uninterrupted_trace));

    const std::vector<Vehicle::Snapshot> first_segment(
        snapshots.begin(),
        snapshots.begin() + 2);
    SequenceProvider first_provider(
        first_segment,
        first_step);
    Input::Runtime first_runtime;
    CHECK(first_runtime.BeginRecording(
        ValidMetadata(first_step, target_id)));
    Vehicle::RecordingSource first_source(
        first_runtime,
        target_id,
        first_provider);
    CHECK(first_runtime.RecordFixedStep(
        first_step,
        first_source));
    CHECK(first_runtime.RecordFixedStep(
        first_step + 1,
        first_source));

    Input::RuntimeContinuation continuation;
    CHECK(first_runtime.ExportContinuation(continuation));
    CHECK(continuation.processed_steps == 2);
    CHECK(continuation.next_physics_step == first_step + 2);

    Input::Runtime resumed_runtime;
    CHECK(resumed_runtime.ImportContinuation(
        continuation,
        ValidMetadata(first_step, target_id)));
    Vehicle::Snapshot restored_snapshot =
        BaseSnapshot(UINT64_C(999));
    Vehicle::Status status;
    CHECK(Vehicle::BuildSnapshotFromPersistentState(
        target_id,
        resumed_runtime.GetPersistentState().GetControls(),
        restored_snapshot,
        status));
    CHECK(SameSnapshot(restored_snapshot, snapshots[1]));

    const std::vector<Vehicle::Snapshot> remaining(
        snapshots.begin() + 2,
        snapshots.end());
    SequenceProvider resumed_provider(
        remaining,
        first_step + 2);
    Vehicle::RecordingSource resumed_source(
        resumed_runtime,
        target_id,
        resumed_provider);
    CHECK(SameSnapshot(
        resumed_source.GetPreviousSnapshot(),
        restored_snapshot));
    for (std::size_t index = 0;
        index < remaining.size();
        ++index)
    {
        CHECK(resumed_runtime.RecordFixedStep(
            first_step + 2 + index,
            resumed_source));
    }
    std::string resumed_trace;
    CHECK(resumed_runtime.FinalizeRecording(resumed_trace));
    CHECK(resumed_trace == uninterrupted_trace);

    Input::Runtime replay_runtime;
    CHECK(replay_runtime.BeginReplay(
        uninterrupted_trace,
        ValidMetadata(first_step, target_id)));
    CollectingConsumer first_consumer;
    Vehicle::ReplaySink first_sink(
        replay_runtime,
        target_id,
        first_consumer);
    CHECK(replay_runtime.ReplayFixedStep(
        first_step,
        first_sink));
    CHECK(replay_runtime.ReplayFixedStep(
        first_step + 1,
        first_sink));

    Input::RuntimeContinuation replay_continuation;
    CHECK(replay_runtime.ExportContinuation(
        replay_continuation));
    Input::Runtime resumed_replay;
    CHECK(resumed_replay.ImportContinuation(
        replay_continuation,
        ValidMetadata(first_step, target_id)));
    Vehicle::Snapshot replay_previous;
    CHECK(Vehicle::BuildSnapshotFromPersistentState(
        target_id,
        resumed_replay.GetPersistentState().GetControls(),
        replay_previous,
        status));
    CHECK(SameSnapshot(replay_previous, snapshots[1]));

    CollectingConsumer resumed_consumer;
    Vehicle::ReplaySink resumed_sink(
        resumed_replay,
        target_id,
        resumed_consumer);
    CHECK(SameSnapshot(
        resumed_sink.GetPreviousSnapshot(),
        replay_previous));
    for (std::size_t index = 2;
        index < snapshots.size();
        ++index)
    {
        CHECK(resumed_replay.ReplayFixedStep(
            first_step + index,
            resumed_sink));
    }
    CHECK(first_consumer.snapshots.size() == 2);
    CHECK(
        resumed_consumer.snapshots.size() ==
        snapshots.size() - 2);
    for (std::size_t index = 0;
        index < first_consumer.snapshots.size();
        ++index)
    {
        CHECK(SameSnapshot(
            first_consumer.snapshots[index],
            snapshots[index]));
    }
    for (std::size_t index = 0;
        index < resumed_consumer.snapshots.size();
        ++index)
    {
        CHECK(SameSnapshot(
            resumed_consumer.snapshots[index],
            snapshots[index + 2]));
    }

}

void TestSnapshotBuildAndRuntimeBinding()
{
    const std::uint64_t target_id = 77;
    Vehicle::Snapshot snapshot = BaseSnapshot(target_id);
    snapshot.Set(Vehicle::CONTROL_THROTTLE, 0.5);

    std::vector<Input::PersistentControl> controls;
    controls.push_back(
        Persistent(
            target_id,
            Vehicle::CONTROL_THROTTLE,
            0.5));
    Vehicle::Snapshot rebuilt = BaseSnapshot(1234);
    Vehicle::Status status;
    CHECK(Vehicle::BuildSnapshotFromPersistentState(
        target_id,
        controls,
        rebuilt,
        status));
    CHECK(SameSnapshot(rebuilt, snapshot));

    const Vehicle::Snapshot sentinel = rebuilt;
    controls[0].value = 0.0;
    CHECK(!Vehicle::BuildSnapshotFromPersistentState(
        target_id,
        controls,
        rebuilt,
        status));
    CHECK(
        status.error ==
        Vehicle::Error::NONCANONICAL_ACTIVE_ZERO);
    CHECK(SameSnapshot(rebuilt, sentinel));

    std::vector<Vehicle::Snapshot> sequence(
        1,
        BaseSnapshot(target_id));
    SequenceProvider provider(sequence, 1);
    Input::Runtime idle_runtime;
    Vehicle::RecordingSource wrong_mode_source(
        idle_runtime,
        target_id,
        provider);
    Input::Runtime recording_runtime;
    CHECK(recording_runtime.BeginRecording(
        ValidMetadata(1, target_id)));
    CHECK(!recording_runtime.RecordFixedStep(
        1,
        wrong_mode_source));
    CHECK(
        wrong_mode_source.GetStatus().error ==
        Vehicle::Error::RUNTIME_MODE_MISMATCH);

    Input::Metadata wrong_metadata = ValidMetadata(1, target_id);
    wrong_metadata.source_name = "wrong-registry";
    Input::Runtime wrong_metadata_runtime;
    CHECK(wrong_metadata_runtime.BeginRecording(
        wrong_metadata));
    SequenceProvider wrong_metadata_provider(sequence, 1);
    Vehicle::RecordingSource wrong_metadata_source(
        wrong_metadata_runtime,
        target_id,
        wrong_metadata_provider);
    CHECK(!wrong_metadata_runtime.RecordFixedStep(
        1,
        wrong_metadata_source));
    CHECK(
        wrong_metadata_source.GetStatus().error ==
        Vehicle::Error::METADATA_SCHEMA_MISMATCH);

    std::string trace;
    CHECK(RecordSnapshots(
        sequence,
        1,
        target_id,
        trace));
    Input::Metadata mismatched = ValidMetadata(1, target_id);
    mismatched.source_name = "ror-restricted-state-v2";
    Input::Runtime mismatched_replay;
    CHECK(!mismatched_replay.BeginReplay(trace, mismatched));
    CHECK(
        mismatched_replay.GetStatus().error ==
        Input::RuntimeError::IDENTITY_MISMATCH);

    const std::uint64_t binding_step = 500;
    Vehicle::Snapshot active = BaseSnapshot(target_id);
    active.Set(Vehicle::CONTROL_THROTTLE, 0.5);
    std::vector<Vehicle::Snapshot> active_sequence(1, active);
    SequenceProvider prefix_provider(
        active_sequence,
        binding_step);
    Input::Runtime prefix_runtime;
    CHECK(prefix_runtime.BeginRecording(
        ValidMetadata(binding_step, target_id)));
    Vehicle::RecordingSource prefix_source(
        prefix_runtime,
        target_id,
        prefix_provider);
    CHECK(prefix_runtime.RecordFixedStep(
        binding_step,
        prefix_source));
    Input::RuntimeContinuation active_continuation;
    CHECK(prefix_runtime.ExportContinuation(
        active_continuation));

    Input::Runtime resumed_with_active_state;
    CHECK(resumed_with_active_state.ImportContinuation(
        active_continuation,
        ValidMetadata(binding_step, target_id)));
    std::vector<Vehicle::Snapshot> zero_sequence(
        1,
        BaseSnapshot(target_id));
    SequenceProvider foreign_provider(
        zero_sequence,
        binding_step + 1);
    Input::Runtime foreign_runtime;
    CHECK(foreign_runtime.BeginRecording(
        ValidMetadata(binding_step + 1, target_id)));
    Vehicle::RecordingSource foreign_source(
        foreign_runtime,
        target_id,
        foreign_provider);
    CHECK(!resumed_with_active_state.RecordFixedStep(
        binding_step + 1,
        foreign_source));
    CHECK(
        resumed_with_active_state.GetStatus().error ==
        Input::RuntimeError::SOURCE_REJECTED);
    CHECK(foreign_provider.calls == 0);
    const std::vector<Input::PersistentControl>& active_controls =
        resumed_with_active_state
            .GetPersistentState()
            .GetControls();
    CHECK(active_controls.size() == 1);
    if (active_controls.size() == 1)
    {
        CHECK(
            active_controls[0].control_id ==
            Vehicle::CONTROL_THROTTLE);
        CHECK(active_controls[0].value == 0.5);
    }

    const std::uint64_t replay_step = 700;
    const std::string empty_trace =
        OneEmptyStepTrace(replay_step, target_id);
    Input::Runtime replay_owner;
    Input::Runtime replay_foreign;
    CHECK(replay_owner.BeginReplay(
        empty_trace,
        ValidMetadata(replay_step, target_id)));
    CHECK(replay_foreign.BeginReplay(
        empty_trace,
        ValidMetadata(replay_step, target_id)));
    CollectingConsumer foreign_consumer;
    Vehicle::ReplaySink foreign_sink(
        replay_owner,
        target_id,
        foreign_consumer);
    CHECK(!replay_foreign.ReplayFixedStep(
        replay_step,
        foreign_sink));
    CHECK(
        replay_foreign.GetStatus().error ==
        Input::RuntimeError::SINK_REJECTED);
    CHECK(replay_foreign.GetProcessedStepCount() == 0);
    CHECK(foreign_consumer.calls == 0);

    Input::Runtime wrong_target_replay;
    CHECK(wrong_target_replay.BeginReplay(
        empty_trace,
        ValidMetadata(replay_step, target_id)));
    CollectingConsumer wrong_target_consumer;
    Vehicle::ReplaySink wrong_target_sink(
        wrong_target_replay,
        target_id + 1,
        wrong_target_consumer);
    CHECK(!wrong_target_replay.ReplayFixedStep(
        replay_step,
        wrong_target_sink));
    CHECK(
        wrong_target_sink.GetStatus().error ==
        Vehicle::Error::METADATA_SCHEMA_MISMATCH);
    CHECK(wrong_target_replay.GetProcessedStepCount() == 0);
    CHECK(wrong_target_consumer.calls == 0);
}

void TestErrorNames()
{
    for (std::uint32_t value = 0;
        value <=
            static_cast<std::uint32_t>(
                Vehicle::Error::CONSUMER_REJECTED);
        ++value)
    {
        CHECK(
            std::strcmp(
                Vehicle::ToString(
                    static_cast<Vehicle::Error>(value)),
                "unknown") != 0);
    }
    CHECK(
        std::strcmp(
            Vehicle::ToString(
                static_cast<Vehicle::Error>(999)),
            "unknown") == 0);
}

} // namespace

int main()
{
    TestRegistryAndValidation();
    TestAuthenticatedRoundTrip();
    TestRecordingFailuresAreTransactional();
    TestReplayRejectsHostileBatches();
    TestFixedSeedLongContinuation();
    TestRuntimeContinuationRestoresAdapterState();
    TestSnapshotBuildAndRuntimeBinding();
    TestErrorNames();

    if (g_failures != 0)
    {
        std::fprintf(
            stderr,
            "%d deterministic vehicle-input checks failed\n",
            g_failures);
        return 1;
    }

    std::printf(
        "deterministic applied vehicle-input bridge checks passed\n");
    return 0;
}
