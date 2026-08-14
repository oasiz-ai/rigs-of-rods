/*
    This source file is part of Rigs of Rods
    For more information, see http://www.rigsofrods.org/
*/

#include "DeterministicVehicleInputActorAdapter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Adapter =
    RoR::DeterministicVehicleInputActorAdapter;
namespace Vehicle = RoR::DeterministicVehicleInput;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
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

Adapter::PolicySnapshot ValidPolicy()
{
    Adapter::PolicySnapshot policy;
    policy.target_id = UINT64_C(0x0102030405060708);
    policy.gearbox_mode = Adapter::MANUAL_GEARBOX_MODE;
    policy.forward_gear_count = 6;
    policy.gear_range_count = 2;
    policy.fixed_gear = 1;
    policy.fixed_gear_range = 0;
    policy.local_simulated = true;
    policy.truck = true;
    policy.has_engine = true;
    policy.hydro_speed_coupling_enabled = true;
    return policy;
}

Adapter::AppliedControlState ValidControls()
{
    Adapter::AppliedControlState controls;
    controls.steering_command = -0.25f;
    controls.service_brake = 0.125f;
    controls.throttle = 0.75f;
    controls.clutch = 0.5f;
    controls.parking_brake = true;
    controls.engine_contact = true;
    controls.engine_starter = false;
    controls.gear = 1;
    controls.gear_range = 0;
    controls.hydro_speed_coupling = true;
    controls.trailer_parking_brake = false;
    controls.command_values[0] = 0.375f;
    controls.command_values[83] = 1.0f;
    return controls;
}

class PolicyProvider final: public Vehicle::SnapshotProvider
{
public:
    Adapter::PolicySnapshot policy;
    std::vector<Adapter::AppliedControlState> states;
    std::size_t index = 0;

    bool CaptureAppliedControls(
        std::uint64_t,
        Vehicle::Snapshot& snapshot) override
    {
        if (index >= states.size())
            return false;
        Adapter::Status status;
        return Adapter::CaptureSnapshot(
            policy,
            states[index++],
            snapshot,
            status);
    }
};

class PolicyConsumer final: public Vehicle::SnapshotConsumer
{
public:
    Adapter::PolicySnapshot policy;
    std::vector<Adapter::ApplyPlan> plans;

    bool ApplyAppliedControls(
        std::uint64_t,
        const Vehicle::Snapshot& snapshot) override
    {
        Adapter::ApplyPlan plan;
        Adapter::Status status;
        if (!Adapter::BuildApplyPlan(policy, snapshot, plan, status))
            return false;
        plans.push_back(plan);
        return true;
    }
};

RoR::DeterministicInputTrace::Metadata Metadata(
    const Adapter::PolicySnapshot& policy)
{
    RoR::DeterministicInputTrace::Metadata metadata;
    metadata.semantic_flags =
        RoR::DeterministicInputTrace::REQUIRED_SEMANTIC_FLAGS;
    metadata.scenario_id = 77;
    metadata.stream_id = policy.target_id;
    metadata.first_physics_step = 100;
    metadata.physics_step_numerator = 1;
    metadata.physics_step_denominator = 2000;
    metadata.scenario_name = "actor-adapter-roundtrip";
    metadata.source_name = Vehicle::RegistrySourceName();
    for (std::size_t index = 0;
        index < metadata.source_digest.bytes.size();
        ++index)
    {
        metadata.source_digest.bytes[index] =
            static_cast<std::uint8_t>(index + 1U);
    }
    return metadata;
}

void TestPolicyAdmission()
{
    Adapter::Status status;
    Adapter::PolicySnapshot policy = ValidPolicy();
    CHECK(Adapter::ValidatePolicy(policy, status));
    CHECK(status.error == Adapter::PolicyError::NONE);

    Adapter::PolicySnapshot hostile = policy;
    hostile.gearbox_mode = 0;
    CHECK(!Adapter::ValidatePolicy(hostile, status));
    CHECK(status.error == Adapter::PolicyError::UNSUPPORTED_GEARBOX);

    hostile = policy;
    hostile.anti_lock_brake_enabled = true;
    CHECK(!Adapter::ValidatePolicy(hostile, status));
    CHECK(status.error == Adapter::PolicyError::CONTROLLER_ENABLED);

    hostile = policy;
    hostile.has_linked_actors = true;
    CHECK(!Adapter::ValidatePolicy(hostile, status));
    CHECK(status.error == Adapter::PolicyError::LINKED_ACTORS);

    hostile = policy;
    hostile.fixed_gear = 7;
    CHECK(!Adapter::ValidatePolicy(hostile, status));
    CHECK(status.error ==
        Adapter::PolicyError::INVALID_GEAR_CONFIGURATION);

    hostile = policy;
    hostile.hydro_speed_coupling_enabled = false;
    CHECK(!Adapter::SamePolicy(policy, hostile));
    CHECK(Adapter::SamePolicy(policy, policy));
}

void TestCaptureAndApplyRoundTrip()
{
    const Adapter::PolicySnapshot policy = ValidPolicy();
    Adapter::AppliedControlState controls = ValidControls();
    // The live binary32 boundary may contain negative zero. The wire is
    // canonical and must publish positive zero instead.
    std::uint32_t negative_zero_bits = UINT32_C(0x80000000);
    std::memcpy(&controls.command_values[1],
        &negative_zero_bits,
        sizeof(negative_zero_bits));

    Vehicle::Snapshot snapshot;
    Adapter::Status status;
    CHECK(Adapter::CaptureSnapshot(
        policy,
        controls,
        snapshot,
        status));
    CHECK(snapshot.target_id == policy.target_id);
    double command_two = 1.0;
    CHECK(snapshot.Get(Vehicle::CommandControlId(2), command_two));
    CHECK(DoubleBits(command_two) == 0);

    Adapter::ApplyPlan plan;
    CHECK(Adapter::BuildApplyPlan(policy, snapshot, plan, status));
    CHECK(plan.controls.steering_command == controls.steering_command);
    CHECK(plan.controls.service_brake == controls.service_brake);
    CHECK(plan.controls.throttle == controls.throttle);
    CHECK(plan.controls.clutch == controls.clutch);
    CHECK(plan.controls.parking_brake == controls.parking_brake);
    CHECK(plan.controls.engine_contact == controls.engine_contact);
    CHECK(plan.controls.gear == policy.fixed_gear);
    CHECK(plan.controls.command_values[0] == controls.command_values[0]);
    CHECK(plan.controls.command_values[1] == 0.0f);
    CHECK(plan.controls.command_values[83] == 1.0f);
}

void TestFailClosedTransaction()
{
    const Adapter::PolicySnapshot policy = ValidPolicy();
    Adapter::AppliedControlState controls = ValidControls();
    Vehicle::Snapshot snapshot;
    Adapter::Status status;
    CHECK(Adapter::CaptureSnapshot(
        policy,
        controls,
        snapshot,
        status));

    Adapter::ApplyPlan sentinel;
    sentinel.controls.throttle = 0.625f;
    Vehicle::Snapshot wrong_target = snapshot;
    wrong_target.target_id += 1;
    CHECK(!Adapter::BuildApplyPlan(
        policy,
        wrong_target,
        sentinel,
        status));
    CHECK(status.error == Adapter::PolicyError::TARGET_MISMATCH);
    CHECK(sentinel.controls.throttle == 0.625f);

    Vehicle::Snapshot shifted = snapshot;
    CHECK(shifted.Set(Vehicle::CONTROL_GEAR, 2.0));
    CHECK(!Adapter::BuildApplyPlan(
        policy,
        shifted,
        sentinel,
        status));
    CHECK(status.error == Adapter::PolicyError::GEAR_CHANGE_UNSUPPORTED);
    CHECK(sentinel.controls.throttle == 0.625f);

    controls.gear = 2;
    Vehicle::Snapshot unchanged = snapshot;
    CHECK(!Adapter::CaptureSnapshot(
        policy,
        controls,
        unchanged,
        status));
    CHECK(status.error == Adapter::PolicyError::GEAR_CHANGE_UNSUPPORTED);
    CHECK(unchanged.target_id == snapshot.target_id);

    Adapter::PolicySnapshot drift = policy;
    drift.cruise_control_enabled = true;
    CHECK(!Adapter::BuildApplyPlan(drift, snapshot, sentinel, status));
    CHECK(status.error == Adapter::PolicyError::CONTROLLER_ENABLED);
    CHECK(sentinel.controls.throttle == 0.625f);
}

void TestAuthenticatedRecordReplayRoundTrip()
{
    const Adapter::PolicySnapshot policy = ValidPolicy();
    PolicyProvider provider;
    provider.policy = policy;
    provider.states.push_back(ValidControls());
    provider.states.push_back(ValidControls());
    provider.states.back().throttle = 0.5f;
    provider.states.back().steering_command = 0.25f;
    provider.states.push_back(provider.states.back());
    provider.states.back().service_brake = 0.75f;

    RoR::DeterministicInputTrace::Limits limits;
    limits.max_steps = 3;
    limits.max_events = 256;
    limits.max_bytes = 65536;
    limits.max_events_per_step =
        static_cast<std::uint32_t>(Vehicle::CONTROL_SLOT_COUNT);
    limits.max_active_controls =
        static_cast<std::uint32_t>(Vehicle::CONTROL_SLOT_COUNT);
    const RoR::DeterministicInputTrace::Metadata metadata = Metadata(policy);

    RoR::DeterministicInputTrace::Runtime recorder;
    CHECK(recorder.BeginRecording(metadata, limits));
    Vehicle::RecordingSource source(recorder, policy.target_id, provider);
    CHECK(recorder.RecordFixedStep(100, source));
    CHECK(recorder.RecordFixedStep(101, source));
    CHECK(recorder.RecordFixedStep(102, source));
    std::string bytes;
    CHECK(recorder.FinalizeRecording(bytes));
    CHECK(!bytes.empty());

    PolicyConsumer consumer;
    consumer.policy = policy;
    RoR::DeterministicInputTrace::Runtime replay;
    CHECK(replay.BeginReplay(bytes, metadata, limits));
    Vehicle::ReplaySink sink(replay, policy.target_id, consumer);
    CHECK(replay.ReplayFixedStep(100, sink));
    CHECK(replay.ReplayFixedStep(101, sink));
    CHECK(replay.ReplayFixedStep(102, sink));
    CHECK(replay.GetLifecycle() ==
        RoR::DeterministicInputTrace::RuntimeLifecycle::FINISHED);
    CHECK(consumer.plans.size() == provider.states.size());
    if (consumer.plans.size() == provider.states.size())
    {
        for (std::size_t index = 0; index < consumer.plans.size(); ++index)
        {
            CHECK(consumer.plans[index].controls.throttle ==
                provider.states[index].throttle);
            CHECK(consumer.plans[index].controls.steering_command ==
                provider.states[index].steering_command);
            CHECK(consumer.plans[index].controls.service_brake ==
                provider.states[index].service_brake);
            CHECK(consumer.plans[index].controls.command_values[0] ==
                provider.states[index].command_values[0]);
        }
    }
}

} // namespace

int main()
{
    TestPolicyAdmission();
    TestCaptureAndApplyRoundTrip();
    TestFailClosedTransaction();
    TestAuthenticatedRecordReplayRoundTrip();

    CHECK(std::strstr(Adapter::PolicyManifest(), "schema=1") != nullptr);
    CHECK(std::strstr(
        Adapter::PolicyManifest(),
        "fixed-gear") != nullptr);

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d deterministic Actor adapter tests failed\n",
            g_failures);
        return 1;
    }
    std::puts("deterministic Actor input adapter tests passed");
    return 0;
}
