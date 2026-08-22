#include "DeterministicInputContinuationSavegame.h"
#include "DeterministicStateDigest.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace Save = RoR::DeterministicInputContinuationSavegame;
namespace State = RoR::DeterministicStateDigest;
namespace Trace = RoR::DeterministicInputTrace;

namespace {

int g_failures = 0;

#define CHECK(expression)                                                     \
    do                                                                        \
    {                                                                         \
        if (!(expression))                                                    \
        {                                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                         \
                      << ": CHECK failed: " #expression << '\n';             \
            ++g_failures;                                                     \
        }                                                                     \
    } while (false)

Trace::Metadata MakeMetadata()
{
    Trace::Metadata metadata;
    metadata.semantic_flags = Trace::REQUIRED_SEMANTIC_FLAGS;
    metadata.scenario_id = UINT64_C(2026082001);
    metadata.stream_id = UINT64_C(77);
    metadata.first_physics_step = UINT64_C(100);
    metadata.physics_step_numerator = UINT64_C(1);
    metadata.physics_step_denominator = UINT64_C(2000);
    metadata.scenario_name = "savegame-continuation-test";
    metadata.source_name = "restricted-controls-v1";
    metadata.source_digest = Trace::ComputeSha256(
        reinterpret_cast<const std::uint8_t*>("source"),
        6U);
    return metadata;
}

Trace::Limits MakeLimits()
{
    Trace::Limits limits;
    limits.max_steps = UINT64_C(16);
    limits.max_events = UINT64_C(64);
    limits.max_bytes = UINT64_C(64) * UINT64_C(1024);
    limits.max_events_per_step = 8U;
    limits.max_active_controls = 8U;
    limits.max_identity_string_bytes = 64U;
    return limits;
}

class Source: public Trace::FixedStepSampleSource
{
public:
    explicit Source(std::uint64_t first_step): m_first_step(first_step) {}

    bool SampleFixedStepStart(
        std::uint64_t physics_step,
        Trace::SampleCollector& collector) override
    {
        if (physics_step == m_first_step)
            return collector.AddPersistentDelta(77U, 1U, 0.5);
        if (physics_step == m_first_step + 1U)
            return collector.AddPersistentDelta(77U, 1U, 0.75);
        return true;
    }

private:
    std::uint64_t m_first_step;
};

class Sink: public Trace::FixedStepInjectionSink
{
public:
    bool InjectFixedStepStart(
        const Trace::StepInjection& injection) override
    {
        steps.push_back(injection.physics_step);
        return true;
    }

    std::vector<std::uint64_t> steps;
};

struct ToyPhysicsState
{
    float position = 0.0F;
    float velocity = 0.0F;
    std::uint64_t completed_steps = 0U;
};

class StateSink: public Trace::FixedStepInjectionSink
{
public:
    explicit StateSink(const ToyPhysicsState& initial): state(initial) {}

    bool InjectFixedStepStart(
        const Trace::StepInjection& injection) override
    {
        double input = 0.0;
        for (std::size_t index = 0U;
            index < injection.persistent_state.size();
            ++index)
        {
            const Trace::PersistentControl& control =
                injection.persistent_state[index];
            if (control.target_id == 77U && control.control_id == 1U)
                input = control.value;
        }
        volatile float acceleration = static_cast<float>(input) * 0.125F;
        volatile float velocity = state.velocity + acceleration;
        volatile float position = state.position + velocity * 0.0005F;
        state.velocity = velocity;
        state.position = position;
        state.completed_steps = injection.physics_step + 1U;
        return true;
    }

    bool BuildDigest(State::Digest& output) const
    {
        State::Builder builder(
            state.completed_steps,
            MakeMetadata().scenario_id);
        State::ActorRecord actor;
        actor.actor_id = 1;
        actor.state = State::ACTOR_STATE_LOCAL_SIMULATED;
        actor.flags = State::ACTOR_FLAG_UPDATE_PHYSICS;
        actor.actor_physics_step = state.completed_steps;
        State::NodeRecord node;
        node.actor_id = 1;
        node.node_id = 0U;
        node.position[0] = state.position;
        node.velocity[0] = state.velocity;
        return builder.BeginActors(1U) &&
            builder.AddActor(actor) &&
            builder.BeginNodes(1U) &&
            builder.AddNode(node) &&
            builder.BeginBeams(0U) &&
            builder.BeginHydros(0U) &&
            builder.BeginContacts(0U) &&
            builder.Finish(output);
    }

    ToyPhysicsState state;
};

Save::Payload MakeRecordPayload()
{
    const Trace::Metadata metadata = MakeMetadata();
    const Trace::Limits limits = MakeLimits();
    Trace::Runtime runtime;
    Source source(metadata.first_physics_step);
    CHECK(runtime.BeginRecording(metadata, limits));
    CHECK(runtime.RecordFixedStep(100U, source));
    CHECK(runtime.RecordFixedStep(101U, source));
    CHECK(runtime.Pause());

    Save::Payload payload;
    payload.resume_after_load = true;
    payload.scenario_id = metadata.scenario_id;
    payload.target_id = metadata.stream_id;
    payload.step_limit = limits.max_steps;
    payload.completed_physics_steps = 102U;
    payload.actor_physics_step = 41U;
    CHECK(runtime.ExportContinuation(payload.continuation));
    return payload;
}

std::string CompleteRecordedTrace()
{
    Save::Payload recorded = MakeRecordPayload();
    Trace::Runtime recorder;
    CHECK(recorder.ImportContinuation(
        recorded.continuation,
        MakeMetadata()));
    CHECK(recorder.Resume());
    Source source(100U);
    CHECK(recorder.RecordFixedStep(102U, source));
    std::string trace;
    CHECK(recorder.FinalizeRecording(trace));
    return trace;
}

void CheckPayloadEqual(const Save::Payload& left, const Save::Payload& right)
{
    CHECK(left.schema_version == right.schema_version);
    CHECK(left.resume_after_load == right.resume_after_load);
    CHECK(left.scenario_id == right.scenario_id);
    CHECK(left.target_id == right.target_id);
    CHECK(left.step_limit == right.step_limit);
    CHECK(left.completed_physics_steps == right.completed_physics_steps);
    CHECK(left.actor_physics_step == right.actor_physics_step);
    CHECK(left.continuation.schema_version ==
        right.continuation.schema_version);
    CHECK(left.continuation.mode == right.continuation.mode);
    CHECK(left.continuation.lifecycle == right.continuation.lifecycle);
    CHECK(left.continuation.processed_steps ==
        right.continuation.processed_steps);
    CHECK(left.continuation.next_physics_step ==
        right.continuation.next_physics_step);
    CHECK(left.continuation.trace_digest == right.continuation.trace_digest);
    CHECK(left.continuation.authentication_digest ==
        right.continuation.authentication_digest);
    CHECK(left.continuation.authenticated_trace ==
        right.continuation.authenticated_trace);
}

void TestRecordRoundTripAndResume()
{
    Trace::Runtime uninterrupted;
    Source uninterrupted_source(100U);
    CHECK(uninterrupted.BeginRecording(MakeMetadata(), MakeLimits()));
    CHECK(uninterrupted.RecordFixedStep(100U, uninterrupted_source));
    CHECK(uninterrupted.RecordFixedStep(101U, uninterrupted_source));
    CHECK(uninterrupted.RecordFixedStep(102U, uninterrupted_source));
    std::string uninterrupted_trace;
    CHECK(uninterrupted.FinalizeRecording(uninterrupted_trace));

    const Save::Payload original = MakeRecordPayload();
    Save::Status status;
    std::string encoded = "unchanged";
    CHECK(Save::Encode(original, encoded, status));
    CHECK(status.error == Save::Error::NONE);
    CHECK(!encoded.empty());
    CHECK(encoded.find('=') == std::string::npos);
    CHECK(encoded.size() == 923U);
    CHECK(Trace::ComputeSha256(
        reinterpret_cast<const std::uint8_t*>(encoded.data()),
        encoded.size()).ToHex() ==
        "60272df8f50fc2f09e7c885a25a1b8bc6ae1f99fe3e3faaae7634affef598e0f");

    Save::Payload decoded;
    CHECK(Save::Decode(encoded, decoded, status));
    CheckPayloadEqual(original, decoded);

    Trace::Runtime resumed;
    CHECK(resumed.ImportContinuation(decoded.continuation, MakeMetadata()));
    CHECK(resumed.GetLifecycle() == Trace::RuntimeLifecycle::PAUSED);
    CHECK(resumed.Resume());
    Source source(100U);
    CHECK(resumed.RecordFixedStep(102U, source));
    CHECK(resumed.GetProcessedStepCount() == 3U);
    std::string resumed_trace;
    CHECK(resumed.FinalizeRecording(resumed_trace));
    CHECK(resumed_trace == uninterrupted_trace);
}

void TestReplayRoundTripAndResume()
{
    const std::string trace = CompleteRecordedTrace();

    Trace::Runtime replay;
    Sink prefix;
    CHECK(replay.BeginReplay(trace, MakeMetadata(), MakeLimits()));
    CHECK(replay.ReplayFixedStep(100U, prefix));
    CHECK(replay.Pause());

    Save::Payload original;
    original.resume_after_load = true;
    original.scenario_id = MakeMetadata().scenario_id;
    original.target_id = MakeMetadata().stream_id;
    original.step_limit = MakeLimits().max_steps;
    original.completed_physics_steps = 101U;
    original.actor_physics_step = 40U;
    CHECK(replay.ExportContinuation(original.continuation));

    Save::Status status;
    std::string encoded;
    CHECK(Save::Encode(original, encoded, status));
    Save::Payload decoded;
    CHECK(Save::Decode(encoded, decoded, status));
    Trace::Runtime resumed;
    CHECK(resumed.ImportContinuation(decoded.continuation, MakeMetadata()));
    CHECK(resumed.Resume());
    Sink suffix;
    CHECK(resumed.ReplayFixedStep(101U, suffix));
    CHECK(resumed.ReplayFixedStep(102U, suffix));
    CHECK(resumed.GetLifecycle() == Trace::RuntimeLifecycle::FINISHED);
    CHECK(prefix.steps.size() == 1U);
    CHECK(suffix.steps.size() == 2U);
}

void TestSaveLoadReplayFinalStateDigest()
{
    const std::string trace = CompleteRecordedTrace();

    Trace::Runtime uninterrupted;
    CHECK(uninterrupted.BeginReplay(trace, MakeMetadata(), MakeLimits()));
    StateSink uninterrupted_sink(ToyPhysicsState{});
    CHECK(uninterrupted.ReplayFixedStep(100U, uninterrupted_sink));
    CHECK(uninterrupted.ReplayFixedStep(101U, uninterrupted_sink));
    CHECK(uninterrupted.ReplayFixedStep(102U, uninterrupted_sink));
    State::Digest uninterrupted_digest;
    CHECK(uninterrupted_sink.BuildDigest(uninterrupted_digest));

    Trace::Runtime prefix;
    CHECK(prefix.BeginReplay(trace, MakeMetadata(), MakeLimits()));
    StateSink prefix_sink(ToyPhysicsState{});
    CHECK(prefix.ReplayFixedStep(100U, prefix_sink));
    CHECK(prefix.Pause());

    Save::Payload saved;
    saved.resume_after_load = true;
    saved.scenario_id = MakeMetadata().scenario_id;
    saved.target_id = MakeMetadata().stream_id;
    saved.step_limit = MakeLimits().max_steps;
    saved.completed_physics_steps = 101U;
    saved.actor_physics_step = prefix_sink.state.completed_steps;
    CHECK(prefix.ExportContinuation(saved.continuation));
    Save::Status status;
    std::string encoded;
    CHECK(Save::Encode(saved, encoded, status));

    Save::Payload loaded;
    CHECK(Save::Decode(encoded, loaded, status));
    CHECK(loaded.actor_physics_step == prefix_sink.state.completed_steps);
    Trace::Runtime resumed;
    CHECK(resumed.ImportContinuation(
        loaded.continuation,
        MakeMetadata()));
    CHECK(resumed.Resume());
    StateSink resumed_sink(prefix_sink.state);
    CHECK(resumed.ReplayFixedStep(101U, resumed_sink));
    CHECK(resumed.ReplayFixedStep(102U, resumed_sink));
    State::Digest resumed_digest;
    CHECK(resumed_sink.BuildDigest(resumed_digest));
    CHECK(resumed_digest == uninterrupted_digest);
    CHECK(uninterrupted_digest.ToHex() ==
        "df498432672738a6430ba0900aaeee8f2aa92fe190842a29d504784fb85ce2ee");
    std::cout << "save/load final state digest="
              << uninterrupted_digest.ToHex() << '\n';
}

void TestCorruptionAndCanonicalEncoding()
{
    const Save::Payload payload = MakeRecordPayload();
    Save::Status status;
    std::string encoded;
    CHECK(Save::Encode(payload, encoded, status));

    Save::Payload output;
    output.target_id = UINT64_C(999);
    for (std::size_t size = 0U; size < encoded.size(); ++size)
    {
        const std::string truncated = encoded.substr(0U, size);
        CHECK(!Save::Decode(truncated, output, status));
        CHECK(output.target_id == UINT64_C(999));
    }

    static const char ALPHABET[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (std::size_t index = 0U; index < encoded.size(); ++index)
    {
        std::string changed = encoded;
        changed[index] = encoded[index] == ALPHABET[0] ?
            ALPHABET[1] : ALPHABET[0];
        CHECK(!Save::Decode(changed, output, status));
        CHECK(output.target_id == UINT64_C(999));
    }

    CHECK(!Save::Decode(encoded + "=", output, status));
    CHECK(!Save::Decode("A", output, status));
    CHECK(!Save::Decode("AA+_", output, status));
}

void TestInvalidPayloadsAreTransactional()
{
    const Save::Payload valid = MakeRecordPayload();
    Save::Status status;
    std::string output = "sentinel";

    Save::Payload changed = valid;
    changed.target_id = 0U;
    CHECK(!Save::Encode(changed, output, status));
    CHECK(output == "sentinel");

    changed = valid;
    changed.completed_physics_steps += 1U;
    CHECK(!Save::Encode(changed, output, status));
    CHECK(output == "sentinel");

    changed = valid;
    changed.actor_physics_step =
        std::numeric_limits<std::uint64_t>::max();
    CHECK(!Save::Encode(changed, output, status));
    CHECK(output == "sentinel");

    changed = valid;
    changed.continuation.lifecycle = Trace::RuntimeLifecycle::RUNNING;
    CHECK(!Save::Encode(changed, output, status));
    CHECK(output == "sentinel");

    changed = valid;
    changed.continuation.authenticated_trace.clear();
    CHECK(!Save::Encode(changed, output, status));
    CHECK(output == "sentinel");

    changed = valid;
    changed.continuation.limits.max_bytes =
        Save::MAX_SAVEGAME_TRACE_BYTES + 1U;
    CHECK(!Save::Encode(changed, output, status));
    CHECK(output == "sentinel");
}

} // namespace

int main()
{
    TestRecordRoundTripAndResume();
    TestReplayRoundTripAndResume();
    TestSaveLoadReplayFinalStateDigest();
    TestCorruptionAndCanonicalEncoding();
    TestInvalidPayloadsAreTransactional();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " deterministic input continuation "
                  << "savegame check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "deterministic input continuation savegame tests passed\n";
    return EXIT_SUCCESS;
}
