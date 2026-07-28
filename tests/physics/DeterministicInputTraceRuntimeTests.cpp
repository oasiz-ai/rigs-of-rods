#include "DeterministicInputTraceRuntime.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

namespace {

namespace Trace = RoR::DeterministicInputTrace;

int g_failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

std::uint64_t DoubleBits(double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Trace::Metadata MakeMetadata()
{
    Trace::Metadata metadata;
    metadata.scenario_id = UINT64_C(0x1122334455667788);
    metadata.stream_id = UINT64_C(0xa1b2c3d4e5f60718);
    metadata.first_physics_step = 100;
    metadata.physics_step_numerator = 1;
    metadata.physics_step_denominator = 2000;
    metadata.scenario_name = "runtime-lifecycle";
    metadata.source_name = "stable-logical-controls-v1";
    for (std::size_t index = 0;
         index < metadata.source_digest.bytes.size();
         ++index)
    {
        metadata.source_digest.bytes[index] =
            static_cast<std::uint8_t>(3U + index * 7U);
    }
    return metadata;
}

Trace::Event MakeEvent(
    std::uint64_t target_id,
    std::uint32_t control_id,
    Trace::EventKind kind,
    double value)
{
    Trace::Event event;
    event.target_id = target_id;
    event.control_id = control_id;
    event.kind = kind;
    event.value = value;
    return event;
}

Trace::Frame MakeFrame(
    std::uint64_t physics_step,
    const std::vector<Trace::Event>& events)
{
    Trace::Frame frame;
    frame.physics_step = physics_step;
    frame.events = events;
    return frame;
}

std::vector<Trace::Frame> MakeFrames()
{
    std::vector<Trace::Frame> frames;
    frames.push_back(MakeFrame(
        100,
        std::vector<Trace::Event>{
            MakeEvent(1, 10, Trace::EventKind::STATE, 0.25),
            MakeEvent(1, 20, Trace::EventKind::IMPULSE, 1.0)}));
    frames.push_back(MakeFrame(
        101,
        std::vector<Trace::Event>{
            MakeEvent(1, 10, Trace::EventKind::STATE, 0.5),
            MakeEvent(2, 1, Trace::EventKind::STATE, -0.75)}));
    frames.push_back(MakeFrame(
        102,
        std::vector<Trace::Event>{
            MakeEvent(1, 10, Trace::EventKind::STATE, 0.0),
            MakeEvent(2, 9, Trace::EventKind::IMPULSE, -1.0)}));
    frames.push_back(MakeFrame(103, std::vector<Trace::Event>()));
    return frames;
}

std::string WriteDirect(
    const Trace::Metadata& metadata,
    const std::vector<Trace::Frame>& frames,
    Trace::Digest* digest = nullptr,
    const Trace::Limits& limits = Trace::Limits())
{
    std::ostringstream output(std::ios::out | std::ios::binary);
    Trace::Writer writer(output, metadata, limits);
    CHECK(writer.IsReady());
    for (std::size_t index = 0; index < frames.size(); ++index)
        CHECK(writer.Append(frames[index]));
    CHECK(writer.Finish());
    if (digest != nullptr)
        *digest = writer.GetTraceDigest();
    return output.str();
}

bool ValidateTrace(
    const std::string& bytes,
    std::uint64_t* steps = nullptr,
    const Trace::Limits& limits = Trace::Limits())
{
    std::istringstream input(bytes, std::ios::in | std::ios::binary);
    Trace::Reader reader(input, limits);
    Trace::Frame frame;
    while (reader.IsReady() &&
           reader.ReadNext(frame) == Trace::ReadResult::FRAME)
    {
    }
    if (steps != nullptr)
        *steps = reader.GetStepCount();
    return reader.GetStatus().error == Trace::Error::NONE;
}

struct ScriptSource : public Trace::FixedStepSampleSource
{
    std::vector<Trace::Frame> frames;
    std::size_t next;
    std::size_t calls;
    bool reject;
    bool throw_exception;
    bool ignore_add_failure;

    explicit ScriptSource(const std::vector<Trace::Frame>& script):
        frames(script),
        next(0),
        calls(0),
        reject(false),
        throw_exception(false),
        ignore_add_failure(true)
    {
    }

    bool SampleFixedStepStart(
        std::uint64_t physics_step,
        Trace::SampleCollector& collector) override
    {
        ++calls;
        if (throw_exception)
            throw std::runtime_error("hostile source");
        if (next >= frames.size() ||
            frames[next].physics_step != physics_step)
        {
            return false;
        }
        bool additions_ok = true;
        for (std::size_t index = 0;
             index < frames[next].events.size();
             ++index)
        {
            const Trace::Event& event = frames[next].events[index];
            const bool added =
                event.kind == Trace::EventKind::STATE ?
                    collector.AddPersistentDelta(
                        event.target_id,
                        event.control_id,
                        event.value) :
                    collector.AddImpulse(
                        event.target_id,
                        event.control_id,
                        event.value);
            additions_ok = additions_ok && added;
        }
        ++next;
        if (reject)
            return false;
        return ignore_add_failure || additions_ok;
    }
};

struct CapturingSink : public Trace::FixedStepInjectionSink
{
    std::vector<Trace::StepInjection> injections;
    std::size_t calls;
    bool reject;
    bool throw_exception;

    CapturingSink():
        injections(),
        calls(0),
        reject(false),
        throw_exception(false)
    {
    }

    bool InjectFixedStepStart(
        const Trace::StepInjection& injection) override
    {
        ++calls;
        if (throw_exception)
            throw std::runtime_error("hostile sink");
        if (reject)
            return false;
        injections.push_back(injection);
        return true;
    }
};

std::string RecordFrames(
    const Trace::Metadata& metadata,
    const std::vector<Trace::Frame>& frames,
    const std::vector<std::size_t>& pause_before =
        std::vector<std::size_t>())
{
    Trace::Runtime runtime;
    CHECK(runtime.BeginRecording(metadata));
    ScriptSource source(frames);
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        if (std::find(
                pause_before.begin(),
                pause_before.end(),
                index) != pause_before.end())
        {
            CHECK(runtime.Pause());
            CHECK(runtime.Resume());
        }
        CHECK(runtime.RecordFixedStep(
            frames[index].physics_step,
            source));
    }
    CHECK(source.calls == frames.size());
    std::string output;
    CHECK(runtime.FinalizeRecording(output));
    return output;
}

void CheckInjectionEqual(
    const Trace::StepInjection& left,
    const Trace::StepInjection& right)
{
    CHECK(left.physics_step == right.physics_step);
    CHECK(left.persistent_state.size() ==
        right.persistent_state.size());
    CHECK(left.persistent_deltas.size() ==
        right.persistent_deltas.size());
    CHECK(left.impulses.size() == right.impulses.size());
    for (std::size_t index = 0;
         index < left.persistent_state.size() &&
         index < right.persistent_state.size();
         ++index)
    {
        CHECK(left.persistent_state[index].target_id ==
            right.persistent_state[index].target_id);
        CHECK(left.persistent_state[index].control_id ==
            right.persistent_state[index].control_id);
        CHECK(DoubleBits(left.persistent_state[index].value) ==
            DoubleBits(right.persistent_state[index].value));
    }
    const std::vector<Trace::Event>* left_lists[2] = {
        &left.persistent_deltas,
        &left.impulses
    };
    const std::vector<Trace::Event>* right_lists[2] = {
        &right.persistent_deltas,
        &right.impulses
    };
    for (std::size_t list = 0; list < 2; ++list)
    {
        for (std::size_t index = 0;
             index < left_lists[list]->size() &&
             index < right_lists[list]->size();
             ++index)
        {
            CHECK((*left_lists[list])[index].target_id ==
                (*right_lists[list])[index].target_id);
            CHECK((*left_lists[list])[index].control_id ==
                (*right_lists[list])[index].control_id);
            CHECK((*left_lists[list])[index].kind ==
                (*right_lists[list])[index].kind);
            CHECK(DoubleBits((*left_lists[list])[index].value) ==
                DoubleBits((*right_lists[list])[index].value));
        }
    }
}

void TestGoldenLifecycle()
{
    const Trace::Metadata metadata = MakeMetadata();
    const std::vector<Trace::Frame> frames = MakeFrames();
    Trace::Digest direct_digest;
    const std::string direct =
        WriteDirect(metadata, frames, &direct_digest);

    Trace::Runtime recorder;
    CHECK(recorder.GetMode() == Trace::RuntimeMode::NONE);
    CHECK(recorder.GetLifecycle() == Trace::RuntimeLifecycle::IDLE);
    CHECK(recorder.BeginRecording(metadata));
    CHECK(recorder.GetMode() == Trace::RuntimeMode::RECORD);
    CHECK(recorder.GetLifecycle() == Trace::RuntimeLifecycle::RUNNING);
    CHECK(recorder.GetNextPhysicsStep() == 100);
    CHECK(recorder.GetIdentity().source_digest == metadata.source_digest);

    ScriptSource source(frames);
    CHECK(recorder.RecordFixedStep(100, source));
    CHECK(source.calls == 1);
    CHECK(recorder.Pause());
    CHECK(recorder.GetLifecycle() == Trace::RuntimeLifecycle::PAUSED);
    CHECK(recorder.GetProcessedStepCount() == 1);
    CHECK(recorder.GetNextPhysicsStep() == 101);
    CHECK(recorder.Resume());
    for (std::size_t index = 1; index < frames.size(); ++index)
        CHECK(recorder.RecordFixedStep(frames[index].physics_step, source));
    CHECK(source.calls == frames.size());

    std::string recorded("caller-sentinel");
    CHECK(recorder.FinalizeRecording(recorded));
    CHECK(recorder.GetLifecycle() == Trace::RuntimeLifecycle::FINISHED);
    CHECK(recorded == direct);
    CHECK(recorder.GetAuthenticatedTrace() == direct);
    CHECK(recorder.GetTraceDigest() == direct_digest);
    CHECK(recorder.GetTraceDigest().ToHex() ==
        "4c54a6ab5d5ff6e4f6f3db94d8ceeb5c941231670c6526d39a2b9523c5324bfb");
    CHECK(recorder.GetPersistentState().GetControlCount() == 1);

    Trace::Runtime replay;
    CHECK(replay.BeginReplay(recorded, metadata));
    CHECK(replay.GetMode() == Trace::RuntimeMode::REPLAY);
    CHECK(replay.GetLifecycle() == Trace::RuntimeLifecycle::RUNNING);
    CapturingSink sink;
    CHECK(replay.ReplayFixedStep(100, sink));
    CHECK(replay.ReplayFixedStep(101, sink));
    CHECK(replay.Pause());
    CHECK(replay.GetNextPhysicsStep() == 102);
    CHECK(replay.Resume());
    CHECK(replay.ReplayFixedStep(102, sink));
    CHECK(replay.ReplayFixedStep(103, sink));
    CHECK(replay.GetLifecycle() == Trace::RuntimeLifecycle::FINISHED);
    CHECK(sink.calls == 4);
    CHECK(sink.injections.size() == 4);

    CHECK(sink.injections[0].persistent_state.size() == 1);
    CHECK(sink.injections[0].persistent_deltas.size() == 1);
    CHECK(sink.injections[0].impulses.size() == 1);
    CHECK(sink.injections[1].persistent_state.size() == 2);
    CHECK(sink.injections[1].persistent_deltas.size() == 2);
    CHECK(sink.injections[1].impulses.empty());
    CHECK(sink.injections[2].persistent_state.size() == 1);
    CHECK(sink.injections[2].persistent_deltas.size() == 1);
    CHECK(sink.injections[2].impulses.size() == 1);
    CHECK(sink.injections[3].persistent_state.size() == 1);
    CHECK(sink.injections[3].persistent_deltas.empty());
    CHECK(sink.injections[3].impulses.empty());
    CHECK(sink.injections[3].persistent_state[0].target_id == 2);
    CHECK(sink.injections[3].persistent_state[0].control_id == 1);
    CHECK(sink.injections[3].persistent_state[0].value == -0.75);
}

void TestRecordAndReplayContinuation()
{
    const Trace::Metadata metadata = MakeMetadata();
    const std::vector<Trace::Frame> frames = MakeFrames();
    const std::string uninterrupted = RecordFrames(metadata, frames);

    Trace::Runtime recorder;
    CHECK(recorder.BeginRecording(metadata));
    const std::vector<Trace::Frame> first(
        frames.begin(),
        frames.begin() + 2);
    ScriptSource first_source(first);
    CHECK(recorder.RecordFixedStep(100, first_source));
    CHECK(recorder.RecordFixedStep(101, first_source));
    CHECK(recorder.Pause());
    const Trace::Digest live_record_digest =
        recorder.GetTraceDigest();

    Trace::RuntimeContinuation record_checkpoint;
    CHECK(recorder.ExportContinuation(record_checkpoint));
    CHECK(recorder.GetLifecycle() == Trace::RuntimeLifecycle::PAUSED);
    CHECK(record_checkpoint.mode == Trace::RuntimeMode::RECORD);
    CHECK(record_checkpoint.lifecycle == Trace::RuntimeLifecycle::PAUSED);
    CHECK(record_checkpoint.processed_steps == 2);
    CHECK(record_checkpoint.next_physics_step == 102);
    std::uint64_t checkpoint_steps = 0;
    CHECK(ValidateTrace(
        record_checkpoint.authenticated_trace,
        &checkpoint_steps,
        record_checkpoint.limits));
    CHECK(checkpoint_steps == 2);

    Trace::Runtime resumed_recording;
    CHECK(resumed_recording.ImportContinuation(
        record_checkpoint,
        metadata));
    CHECK(resumed_recording.GetLifecycle() ==
        Trace::RuntimeLifecycle::PAUSED);
    CHECK(resumed_recording.GetPersistentState().GetControlCount() == 2);
    CHECK(resumed_recording.GetTraceDigest() == live_record_digest);
    Trace::RuntimeContinuation reexported_record_checkpoint;
    CHECK(resumed_recording.ExportContinuation(
        reexported_record_checkpoint));
    CHECK(reexported_record_checkpoint.authentication_digest ==
        record_checkpoint.authentication_digest);
    CHECK(reexported_record_checkpoint.authenticated_trace ==
        record_checkpoint.authenticated_trace);
    CHECK(resumed_recording.Resume());
    const std::vector<Trace::Frame> remainder(
        frames.begin() + 2,
        frames.end());
    ScriptSource remainder_source(remainder);
    CHECK(resumed_recording.RecordFixedStep(102, remainder_source));
    CHECK(resumed_recording.RecordFixedStep(103, remainder_source));
    std::string resumed_bytes;
    CHECK(resumed_recording.FinalizeRecording(resumed_bytes));
    CHECK(resumed_bytes == uninterrupted);

    Trace::Runtime uninterrupted_replay;
    CapturingSink all_sink;
    CHECK(uninterrupted_replay.BeginReplay(uninterrupted, metadata));
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        CHECK(uninterrupted_replay.ReplayFixedStep(
            frames[index].physics_step,
            all_sink));
    }

    Trace::Runtime replay;
    CapturingSink prefix_sink;
    CHECK(replay.BeginReplay(uninterrupted, metadata));
    CHECK(replay.ReplayFixedStep(100, prefix_sink));
    CHECK(replay.ReplayFixedStep(101, prefix_sink));
    CHECK(replay.Pause());
    Trace::RuntimeContinuation replay_checkpoint;
    CHECK(replay.ExportContinuation(replay_checkpoint));
    CHECK(replay_checkpoint.mode == Trace::RuntimeMode::REPLAY);
    CHECK(replay_checkpoint.processed_steps == 2);
    CHECK(replay_checkpoint.authenticated_trace == uninterrupted);

    Trace::Runtime resumed_replay;
    CHECK(resumed_replay.ImportContinuation(
        replay_checkpoint,
        metadata));
    CHECK(resumed_replay.GetPersistentState().GetControlCount() == 2);
    CHECK(resumed_replay.Resume());
    CapturingSink suffix_sink;
    CHECK(resumed_replay.ReplayFixedStep(102, suffix_sink));
    CHECK(resumed_replay.ReplayFixedStep(103, suffix_sink));
    CHECK(resumed_replay.GetLifecycle() ==
        Trace::RuntimeLifecycle::FINISHED);
    CHECK(prefix_sink.injections.size() + suffix_sink.injections.size() ==
        all_sink.injections.size());
    for (std::size_t index = 0;
         index < prefix_sink.injections.size();
         ++index)
    {
        CheckInjectionEqual(
            prefix_sink.injections[index],
            all_sink.injections[index]);
    }
    for (std::size_t index = 0;
         index < suffix_sink.injections.size();
         ++index)
    {
        CheckInjectionEqual(
            suffix_sink.injections[index],
            all_sink.injections[index + 2]);
    }

    Trace::RuntimeContinuation finished_checkpoint;
    CHECK(resumed_replay.ExportContinuation(finished_checkpoint));
    Trace::Runtime finished_import;
    CHECK(finished_import.ImportContinuation(
        finished_checkpoint,
        metadata));
    CHECK(finished_import.GetLifecycle() ==
        Trace::RuntimeLifecycle::FINISHED);
    CHECK(finished_import.GetPersistentState() ==
        resumed_replay.GetPersistentState());
}

void TestFrameRegroupingAndNoWallClockRecords()
{
    Trace::Metadata metadata = MakeMetadata();
    metadata.first_physics_step = 5000;
    std::vector<Trace::Frame> frames;
    for (std::uint64_t offset = 0; offset < 257; ++offset)
    {
        std::vector<Trace::Event> events;
        if ((offset % 7U) == 0)
        {
            events.push_back(MakeEvent(
                1,
                1,
                Trace::EventKind::STATE,
                static_cast<double>(offset + 1U) / 258.0));
        }
        else if ((offset % 11U) == 0)
        {
            events.push_back(MakeEvent(
                1,
                2,
                Trace::EventKind::IMPULSE,
                1.0));
        }
        frames.push_back(MakeFrame(5000 + offset, events));
    }

    const std::string one_step_wall_frames =
        RecordFrames(metadata, frames);
    const std::string irregular_wall_frames =
        RecordFrames(
            metadata,
            frames,
            std::vector<std::size_t>{
                1, 2, 5, 13, 34, 89, 144, 233});
    CHECK(irregular_wall_frames == one_step_wall_frames);
    CHECK(WriteDirect(metadata, frames) == one_step_wall_frames);
}

void TestHostileSourcesAndOrdering()
{
    const Trace::Metadata metadata = MakeMetadata();
    const std::vector<Trace::Frame> frames = MakeFrames();

    {
        Trace::Runtime runtime;
        ScriptSource source(frames);
        CHECK(runtime.BeginRecording(metadata));
        CHECK(!runtime.RecordFixedStep(101, source));
        CHECK(source.calls == 0);
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::FIXED_STEP_MISMATCH);
        CHECK(runtime.GetLifecycle() == Trace::RuntimeLifecycle::FAULTED);
        CHECK(!runtime.RecordFixedStep(100, source));
        CHECK(source.calls == 0);
    }
    {
        Trace::Runtime runtime;
        ScriptSource source(
            std::vector<Trace::Frame>{frames[0]});
        source.reject = true;
        const std::vector<Trace::Frame> original = source.frames;
        CHECK(runtime.BeginRecording(metadata));
        CHECK(!runtime.RecordFixedStep(100, source));
        CHECK(source.calls == 1);
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::SOURCE_REJECTED);
        CHECK(source.frames.size() == original.size());
        CHECK(source.frames[0].events.size() ==
            original[0].events.size());
        CHECK(DoubleBits(source.frames[0].events[0].value) ==
            DoubleBits(original[0].events[0].value));
        CHECK(runtime.GetProcessedStepCount() == 0);
        CHECK(runtime.GetPersistentState().GetControlCount() == 0);
    }
    {
        Trace::Runtime runtime;
        ScriptSource source(
            std::vector<Trace::Frame>{frames[0]});
        source.throw_exception = true;
        CHECK(runtime.BeginRecording(metadata));
        CHECK(!runtime.RecordFixedStep(100, source));
        CHECK(source.calls == 1);
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::SOURCE_EXCEPTION);
    }
    {
        std::vector<Trace::Event> reversed = {
            MakeEvent(2, 1, Trace::EventKind::STATE, 0.5),
            MakeEvent(1, 1, Trace::EventKind::STATE, 0.25)
        };
        ScriptSource source(
            std::vector<Trace::Frame>{
                MakeFrame(100, reversed)});
        Trace::Runtime runtime;
        CHECK(runtime.BeginRecording(metadata));
        CHECK(!runtime.RecordFixedStep(100, source));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::TRACE_FAILURE);
        CHECK(runtime.GetStatus().trace_status.error ==
            Trace::Error::NON_CANONICAL_EVENT_ORDER);
        CHECK(runtime.GetProcessedStepCount() == 0);
    }
    {
        std::vector<Trace::Frame> redundant;
        redundant.push_back(MakeFrame(
            100,
            std::vector<Trace::Event>{
                MakeEvent(1, 1, Trace::EventKind::STATE, 0.5)}));
        redundant.push_back(MakeFrame(
            101,
            std::vector<Trace::Event>{
                MakeEvent(1, 1, Trace::EventKind::STATE, 0.5)}));
        ScriptSource source(redundant);
        Trace::Runtime runtime;
        CHECK(runtime.BeginRecording(metadata));
        CHECK(runtime.RecordFixedStep(100, source));
        CHECK(!runtime.RecordFixedStep(101, source));
        CHECK(runtime.GetStatus().trace_status.error ==
            Trace::Error::REDUNDANT_STATE_EVENT);
        CHECK(runtime.GetProcessedStepCount() == 1);
    }
}

void CheckSingleEventRejected(
    const Trace::Event& event,
    Trace::Error expected_error)
{
    const Trace::Metadata metadata = MakeMetadata();
    ScriptSource source(
        std::vector<Trace::Frame>{
            MakeFrame(
                metadata.first_physics_step,
                std::vector<Trace::Event>{event})});
    Trace::Runtime runtime;
    CHECK(runtime.BeginRecording(metadata));
    CHECK(!runtime.RecordFixedStep(
        metadata.first_physics_step,
        source));
    CHECK(runtime.GetStatus().error ==
        Trace::RuntimeError::TRACE_FAILURE);
    CHECK(runtime.GetStatus().trace_status.error == expected_error);
    CHECK(runtime.GetProcessedStepCount() == 0);
    CHECK(runtime.GetPersistentState().GetControlCount() == 0);
}

void TestSampleCollectorCanonicalBoundaries()
{
    CheckSingleEventRejected(
        MakeEvent(1, 0, Trace::EventKind::STATE, 0.5),
        Trace::Error::INVALID_CONTROL_ID);
    CheckSingleEventRejected(
        MakeEvent(
            1,
            1,
            Trace::EventKind::STATE,
            DoubleFromBits(UINT64_C(0x7ff8000000000001))),
        Trace::Error::NON_FINITE_VALUE);
    CheckSingleEventRejected(
        MakeEvent(
            1,
            1,
            Trace::EventKind::STATE,
            DoubleFromBits(UINT64_C(0x7ff0000000000000))),
        Trace::Error::NON_FINITE_VALUE);
    CheckSingleEventRejected(
        MakeEvent(
            1,
            1,
            Trace::EventKind::STATE,
            DoubleFromBits(UINT64_C(0x8000000000000000))),
        Trace::Error::NON_CANONICAL_VALUE);
    CheckSingleEventRejected(
        MakeEvent(1, 1, Trace::EventKind::IMPULSE, 0.0),
        Trace::Error::NON_CANONICAL_VALUE);
    CheckSingleEventRejected(
        MakeEvent(1, 1, Trace::EventKind::STATE, 0.0),
        Trace::Error::REDUNDANT_STATE_EVENT);
}

void TestQuotasFailClosed()
{
    const Trace::Metadata metadata = MakeMetadata();
    {
        Trace::Limits limits;
        limits.max_events_per_step = 1;
        Trace::Runtime runtime;
        ScriptSource source(
            std::vector<Trace::Frame>{MakeFrames()[0]});
        CHECK(runtime.BeginRecording(metadata, limits));
        CHECK(!runtime.RecordFixedStep(100, source));
        CHECK(source.calls == 1);
        CHECK(runtime.GetStatus().trace_status.error ==
            Trace::Error::EVENT_LIMIT_EXCEEDED);
        CHECK(runtime.GetPersistentState().GetControlCount() == 0);
    }
    {
        Trace::Limits limits;
        limits.max_steps = 1;
        Trace::Runtime runtime;
        ScriptSource first(
            std::vector<Trace::Frame>{
                MakeFrame(100, std::vector<Trace::Event>())});
        ScriptSource second(
            std::vector<Trace::Frame>{
                MakeFrame(101, std::vector<Trace::Event>())});
        CHECK(runtime.BeginRecording(metadata, limits));
        CHECK(runtime.RecordFixedStep(100, first));
        CHECK(!runtime.RecordFixedStep(101, second));
        CHECK(second.calls == 0);
        CHECK(runtime.GetStatus().trace_status.error ==
            Trace::Error::STEP_LIMIT_EXCEEDED);
    }
    {
        Trace::Limits limits;
        limits.max_active_controls = 1;
        Trace::Runtime runtime;
        ScriptSource source(
            std::vector<Trace::Frame>{
                MakeFrame(
                    100,
                    std::vector<Trace::Event>{
                        MakeEvent(
                            1, 1, Trace::EventKind::STATE, 0.5),
                        MakeEvent(
                            1, 2, Trace::EventKind::STATE, 0.25)})});
        CHECK(runtime.BeginRecording(metadata, limits));
        CHECK(!runtime.RecordFixedStep(100, source));
        CHECK(runtime.GetStatus().trace_status.error ==
            Trace::Error::ACTIVE_CONTROL_LIMIT_EXCEEDED);
        CHECK(runtime.GetPersistentState().GetControlCount() == 0);
    }
    {
        Trace::Limits limits;
        limits.max_bytes =
            Trace::HEADER_MIN_SIZE +
            MakeMetadata().scenario_name.size() +
            MakeMetadata().source_name.size() +
            Trace::TRAILER_SIZE;
        Trace::Runtime runtime;
        ScriptSource source(
            std::vector<Trace::Frame>{
                MakeFrame(100, std::vector<Trace::Event>())});
        CHECK(runtime.BeginRecording(metadata, limits));
        CHECK(!runtime.RecordFixedStep(100, source));
        CHECK(source.calls == 1);
        CHECK(runtime.GetStatus().trace_status.error ==
            Trace::Error::BYTE_LIMIT_EXCEEDED);
    }
}

class FailingWriteBuffer : public std::streambuf
{
protected:
    std::streamsize xsputn(
        const char*,
        std::streamsize) override
    {
        return 0;
    }

    int overflow(int = traits_type::eof()) override
    {
        return traits_type::eof();
    }
};

class FailingReadBuffer : public std::streambuf
{
protected:
    std::streamsize xsgetn(
        char*,
        std::streamsize) override
    {
        throw std::ios_base::failure("hostile read");
    }

    int_type underflow() override
    {
        throw std::ios_base::failure("hostile read");
    }
};

class NonIosThrowingWriteBuffer : public std::streambuf
{
protected:
    std::streamsize xsputn(
        const char*,
        std::streamsize) override
    {
        throw std::runtime_error("hostile non-ios write");
    }
};

class NonIosThrowingReadBuffer : public std::streambuf
{
protected:
    std::streamsize xsgetn(
        char*,
        std::streamsize) override
    {
        throw std::runtime_error("hostile non-ios read");
    }
};

class ObservingWriteBuffer : public std::stringbuf
{
public:
    std::size_t calls = 0;
    std::streamsize largest_write = 0;

protected:
    std::streamsize xsputn(
        const char* data,
        std::streamsize size) override
    {
        ++calls;
        largest_write = std::max(largest_write, size);
        return std::stringbuf::xsputn(data, size);
    }
};

struct EmptyStepSource : public Trace::FixedStepSampleSource
{
    bool SampleFixedStepStart(
        std::uint64_t,
        Trace::SampleCollector&) override
    {
        return true;
    }
};

void TestIoMismatchCorruptionAndHostileSinks()
{
    const Trace::Metadata metadata = MakeMetadata();
    const std::vector<Trace::Frame> frames = MakeFrames();
    const std::string valid = WriteDirect(metadata, frames);

    {
        Trace::Runtime runtime;
        ScriptSource source(
            std::vector<Trace::Frame>{frames[0]});
        CHECK(runtime.BeginRecording(metadata));
        CHECK(runtime.RecordFixedStep(100, source));
        std::ostringstream output(
            std::ios::out | std::ios::binary);
        CHECK(runtime.FinalizeRecording(output));
        CHECK(ValidateTrace(output.str()));
    }
    {
        std::istringstream input(
            valid,
            std::ios::in | std::ios::binary);
        Trace::Runtime runtime;
        CapturingSink sink;
        CHECK(runtime.BeginReplay(input, metadata));
        CHECK(runtime.ReplayFixedStep(100, sink));
        CHECK(sink.calls == 1);
    }
    {
        Trace::Runtime runtime;
        ScriptSource source(
            std::vector<Trace::Frame>{frames[0]});
        CHECK(runtime.BeginRecording(metadata));
        CHECK(runtime.RecordFixedStep(100, source));
        FailingWriteBuffer buffer;
        std::ostream output(&buffer);
        CHECK(!runtime.FinalizeRecording(output));
        CHECK(runtime.GetStatus().error == Trace::RuntimeError::IO_FAILURE);
        CHECK(runtime.GetLifecycle() == Trace::RuntimeLifecycle::FAULTED);
    }
    {
        FailingReadBuffer buffer;
        std::istream input(&buffer);
        Trace::Runtime runtime;
        CHECK(!runtime.BeginReplay(input, metadata));
        CHECK(runtime.GetStatus().error == Trace::RuntimeError::IO_FAILURE);
    }
    {
        NonIosThrowingWriteBuffer buffer;
        std::ostream output(&buffer);
        output.exceptions(std::ios::badbit);
        Trace::Runtime runtime;
        CHECK(runtime.BeginRecording(metadata));
        bool escaped = false;
        bool finalized = true;
        try
        {
            finalized = runtime.FinalizeRecording(output);
        }
        catch (...)
        {
            escaped = true;
        }
        CHECK(!escaped);
        CHECK(!finalized);
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::IO_FAILURE);
    }
    {
        NonIosThrowingReadBuffer buffer;
        std::istream input(&buffer);
        input.exceptions(std::ios::badbit);
        Trace::Runtime runtime;
        bool escaped = false;
        bool began = true;
        try
        {
            began = runtime.BeginReplay(input, metadata);
        }
        catch (...)
        {
            escaped = true;
        }
        CHECK(!escaped);
        CHECK(!began);
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::IO_FAILURE);
    }
    {
        Trace::Metadata large_metadata = metadata;
        large_metadata.first_physics_step = 10000;
        Trace::Runtime runtime;
        EmptyStepSource source;
        CHECK(runtime.BeginRecording(large_metadata));
        for (std::uint64_t offset = 0; offset < 1400; ++offset)
        {
            CHECK(runtime.RecordFixedStep(
                large_metadata.first_physics_step + offset,
                source));
        }
        ObservingWriteBuffer buffer;
        std::ostream output(&buffer);
        CHECK(runtime.FinalizeRecording(output));
        CHECK(buffer.calls >= 2);
        CHECK(buffer.largest_write <=
            static_cast<std::streamsize>(UINT32_C(65536)));
        CHECK(ValidateTrace(buffer.str()));
    }
    {
        std::string corrupt = valid;
        corrupt[corrupt.size() / 2U] ^= 1;
        Trace::Runtime runtime;
        CHECK(!runtime.BeginReplay(corrupt, metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::TRACE_FAILURE);
        CHECK(runtime.GetStatus().trace_status.error ==
            Trace::Error::INTEGRITY_MISMATCH);
        CHECK(runtime.GetPersistentState().GetControlCount() == 0);
    }
    {
        Trace::Metadata mismatch = metadata;
        mismatch.stream_id += 1;
        Trace::Runtime runtime;
        CHECK(!runtime.BeginReplay(valid, mismatch));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::IDENTITY_MISMATCH);
        CHECK(runtime.GetProcessedStepCount() == 0);
    }
    {
        Trace::Runtime runtime;
        CapturingSink sink;
        sink.reject = true;
        CHECK(runtime.BeginReplay(valid, metadata));
        CHECK(!runtime.ReplayFixedStep(100, sink));
        CHECK(sink.calls == 1);
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::SINK_REJECTED);
        CHECK(runtime.GetPersistentState().GetControlCount() == 0);
        CHECK(runtime.GetProcessedStepCount() == 0);
        CHECK(!runtime.ReplayFixedStep(100, sink));
        CHECK(sink.calls == 1);
    }
    {
        Trace::Runtime runtime;
        CapturingSink sink;
        sink.throw_exception = true;
        CHECK(runtime.BeginReplay(valid, metadata));
        CHECK(!runtime.ReplayFixedStep(100, sink));
        CHECK(sink.calls == 1);
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::SINK_EXCEPTION);
        CHECK(runtime.GetPersistentState().GetControlCount() == 0);
    }
    {
        Trace::Runtime runtime;
        CapturingSink sink;
        CHECK(runtime.BeginReplay(valid, metadata));
        CHECK(!runtime.ReplayFixedStep(101, sink));
        CHECK(sink.calls == 0);
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::FIXED_STEP_MISMATCH);
    }
}

void TestHostileContinuations()
{
    const Trace::Metadata metadata = MakeMetadata();
    const std::vector<Trace::Frame> frames = MakeFrames();
    Trace::Runtime source;
    ScriptSource script(
        std::vector<Trace::Frame>{frames[0], frames[1]});
    CHECK(source.BeginRecording(metadata));
    CHECK(source.RecordFixedStep(100, script));
    CHECK(source.RecordFixedStep(101, script));
    Trace::RuntimeContinuation good;
    CHECK(source.ExportContinuation(good));

    {
        Trace::RuntimeContinuation changed = good;
        changed.processed_steps = 1;
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(changed, metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::INVALID_CONTINUATION);
    }
    {
        Trace::RuntimeContinuation changed = good;
        changed.next_physics_step += 1;
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(changed, metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::INVALID_CONTINUATION);
    }
    {
        Trace::RuntimeContinuation changed = good;
        changed.authenticated_trace[
            changed.authenticated_trace.size() / 2U] ^= 1;
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(changed, metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::TRACE_FAILURE);
        CHECK(runtime.GetStatus().trace_status.error ==
            Trace::Error::INTEGRITY_MISMATCH);
    }
    {
        Trace::RuntimeContinuation changed = good;
        changed.limits.max_steps = Trace::MAX_TRACE_STEPS + 1U;
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(changed, metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::INVALID_CONTINUATION);
    }
    {
        Trace::RuntimeContinuation changed = good;
        changed.authentication_digest.bytes[0] ^= 1U;
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(changed, metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::INVALID_CONTINUATION);
    }
    {
        Trace::RuntimeContinuation changed = good;
        changed.lifecycle = Trace::RuntimeLifecycle::PAUSED;
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(changed, metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::INVALID_CONTINUATION);
    }
    {
        Trace::RuntimeContinuation changed = good;
        --changed.limits.max_steps;
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(changed, metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::INVALID_CONTINUATION);
    }
    {
        const std::string replay_bytes = WriteDirect(metadata, frames);
        Trace::Runtime replay;
        CapturingSink sink;
        CHECK(replay.BeginReplay(replay_bytes, metadata));
        CHECK(replay.ReplayFixedStep(
            metadata.first_physics_step,
            sink));
        CHECK(replay.Pause());
        Trace::RuntimeContinuation replay_checkpoint;
        CHECK(replay.ExportContinuation(replay_checkpoint));

        replay_checkpoint.processed_steps = 0;
        replay_checkpoint.next_physics_step =
            metadata.first_physics_step;
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(
            replay_checkpoint,
            metadata));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::INVALID_CONTINUATION);
    }
    {
        Trace::Metadata mismatch = metadata;
        mismatch.scenario_name = "different-scenario";
        Trace::Runtime runtime;
        CHECK(!runtime.ImportContinuation(good, mismatch));
        CHECK(runtime.GetStatus().error ==
            Trace::RuntimeError::IDENTITY_MISMATCH);
    }
    {
        Trace::Runtime idle;
        Trace::RuntimeContinuation untouched;
        untouched.authenticated_trace = "caller-sentinel";
        CHECK(!idle.ExportContinuation(untouched));
        CHECK(untouched.authenticated_trace == "caller-sentinel");
        std::string trace_output("trace-sentinel");
        CHECK(!idle.FinalizeRecording(trace_output));
        CHECK(trace_output == "trace-sentinel");
    }
}

struct LongSource : public Trace::FixedStepSampleSource
{
    std::uint64_t first_step;
    std::uint64_t calls;

    explicit LongSource(std::uint64_t first):
        first_step(first),
        calls(0)
    {
    }

    bool SampleFixedStepStart(
        std::uint64_t physics_step,
        Trace::SampleCollector& collector) override
    {
        const std::uint64_t offset = physics_step - first_step;
        ++calls;
        if ((offset % 3U) == 0)
        {
            const double value =
                static_cast<double>((offset % 511U) + 1U) / 512.0;
            return collector.AddPersistentDelta(1, 1, value);
        }
        const double impulse =
            (offset & 1U) == 0 ? 1.0 : -1.0;
        return collector.AddImpulse(1, 2, impulse);
    }
};

struct LongSink : public Trace::FixedStepInjectionSink
{
    std::uint64_t first_step;
    std::uint64_t calls;

    explicit LongSink(std::uint64_t first):
        first_step(first),
        calls(0)
    {
    }

    bool InjectFixedStepStart(
        const Trace::StepInjection& injection) override
    {
        if (injection.physics_step != first_step + calls)
            return false;
        if (injection.persistent_state.size() != 1)
            return false;
        const std::uint64_t offset =
            injection.physics_step - first_step;
        if ((offset % 3U) == 0)
        {
            if (injection.persistent_deltas.size() != 1 ||
                !injection.impulses.empty())
            {
                return false;
            }
        }
        else if (!injection.persistent_deltas.empty() ||
                 injection.impulses.size() != 1)
        {
            return false;
        }
        ++calls;
        return true;
    }
};

void TestTenThousandStepRun()
{
    Trace::Metadata metadata = MakeMetadata();
    metadata.first_physics_step = 900000;
    metadata.scenario_name = "runtime-10k";
    Trace::Runtime recorder;
    LongSource source(metadata.first_physics_step);
    CHECK(recorder.BeginRecording(metadata));
    for (std::uint64_t offset = 0; offset < 10000; ++offset)
    {
        CHECK(recorder.RecordFixedStep(
            metadata.first_physics_step + offset,
            source));
    }
    CHECK(source.calls == 10000);
    std::string bytes;
    CHECK(recorder.FinalizeRecording(bytes));
    CHECK(recorder.GetProcessedStepCount() == 10000);
    CHECK(recorder.GetTraceDigest().ToHex() ==
        "e5f7dbd764dd78e81b1c4c4df408b51e2319af156bb2dda9e8b98a9945a184ba");
    CHECK(ValidateTrace(bytes));

    Trace::Runtime replay;
    LongSink sink(metadata.first_physics_step);
    CHECK(replay.BeginReplay(bytes, metadata));
    for (std::uint64_t offset = 0; offset < 10000; ++offset)
    {
        CHECK(replay.ReplayFixedStep(
            metadata.first_physics_step + offset,
            sink));
    }
    CHECK(sink.calls == 10000);
    CHECK(replay.GetLifecycle() == Trace::RuntimeLifecycle::FINISHED);
    CHECK(replay.GetPersistentState() == recorder.GetPersistentState());
}

void TestEmptyTraceAndStrings()
{
    const Trace::Metadata metadata = MakeMetadata();
    Trace::Runtime recorder;
    CHECK(recorder.BeginRecording(metadata));
    std::string bytes;
    CHECK(recorder.FinalizeRecording(bytes));
    Trace::Runtime replay;
    CHECK(replay.BeginReplay(bytes, metadata));
    CHECK(replay.GetLifecycle() == Trace::RuntimeLifecycle::FINISHED);
    CHECK(replay.GetProcessedStepCount() == 0);
    CHECK(std::string(Trace::ToString(Trace::RuntimeMode::RECORD)) ==
        "record");
    CHECK(std::string(Trace::ToString(
        Trace::RuntimeLifecycle::FAULTED)) == "faulted");
    CHECK(std::string(Trace::ToString(
        Trace::RuntimeError::INVALID_CONTINUATION)) ==
        "invalid_continuation");
}

} // namespace

int main()
{
    TestGoldenLifecycle();
    TestRecordAndReplayContinuation();
    TestFrameRegroupingAndNoWallClockRecords();
    TestHostileSourcesAndOrdering();
    TestSampleCollectorCanonicalBoundaries();
    TestQuotasFailClosed();
    TestIoMismatchCorruptionAndHostileSinks();
    TestHostileContinuations();
    TestTenThousandStepRun();
    TestEmptyTraceAndStrings();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " deterministic input runtime checks failed\n";
        return 1;
    }
    std::cout << "All deterministic input runtime checks passed\n";
    return 0;
}
