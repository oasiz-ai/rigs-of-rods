#include "EpisodeFormat.h"
#include "TestProvenance.h"
#include "WorldModelCaptureEncoding.h"
#include "WorldModelCaptureSession.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int Fail(const char* expression, int line)
{
    std::cerr << "CHECK failed at line " << line << ": "
              << expression << std::endl;
    return 1;
}

#define CHECK(expression) \
    do { if (!(expression)) return Fail(#expression, __LINE__); } while (0)

using namespace RoR::WorldModel;

void Identity(std::array<double, 16>& matrix)
{
    matrix = {};
    matrix[0] = 1.0;
    matrix[5] = 1.0;
    matrix[10] = 1.0;
    matrix[15] = 1.0;
}

bool FillObservation(
    const ObservationId& id,
    const std::string& target_id,
    ObservationSample& sample)
{
    std::vector<std::uint8_t> rgb(6U, 7U);
    ObservationRecord record;
    record.episode_id = id.episode;
    record.observation_id = id;
    record.frame_id = id.observation_index;
    record.target_id = target_id;
    record.nominal_time = {id.observation_index, 48U};
    if (id.observation_index == 0U)
    {
        record.physics_steps = {
            id.completed_physics_steps,
            id.completed_physics_steps,
            0U};
    }
    else
    {
        ObservationId prior;
        if (!MakeObservationId(
                id.episode, 0U, id.observation_index - 1U, prior))
        {
            return false;
        }
        record.physics_steps = {
            prior.completed_physics_steps,
            id.completed_physics_steps,
            static_cast<std::uint32_t>(
                id.completed_physics_steps -
                prior.completed_physics_steps)};
    }
    record.vehicle.mass_kg = 1000.0;
    record.engine.mode = "manual";
    record.world.world_id = "test";
    record.world.terrain_id = "test/default";
    record.world.terrain_sha256 = std::string(64U, 'a');
    record.world.gravity_mps2 = {0.0, -9.81, 0.0};
    record.world.weather_id = "clear";
    record.camera.camera_id = "driver/main";
    record.camera.coordinate_frame = "ror.world.rh-y-up";
    Identity(record.camera.view_matrix);
    Identity(record.camera.projection_matrix);
    record.camera.intrinsics = {
        1.0, 0.0, 1.0,
        0.0, 1.0, 1.0,
        0.0, 0.0, 1.0};
    record.camera.vertical_fov_radians = 1.0;
    record.camera.near_clip_m = 0.1;
    record.camera.far_clip_m = 1000.0;
    if (!CanonicalRgbRecordId(
            id.observation_index,
            record.rgb.record_id))
    {
        return false;
    }
    record.rgb.width = 2U;
    record.rgb.height = 1U;
    record.rgb.row_stride_bytes = 6U;
    record.rgb.pixel_format = "rgb8";
    record.rgb.color_space = "srgb";
    record.rgb.row_origin = "top-left";
    record.rgb.raw_sha256 =
        ComputeSha256(rgb.data(), rgb.size()).ToHex();
    record.state_sha256 = std::string(64U, 'b');
    return EncodeObservationSample(record, rgb, sample);
}

ControlSample Control(
    const std::string& sample_id,
    const std::string& control_id,
    std::uint64_t source_tick,
    std::uint64_t effective_tick,
    const std::string& parent)
{
    ControlSample sample;
    sample.sample_id = sample_id;
    sample.control_id = control_id;
    sample.source_id = "test/session";
    sample.source_tick = source_tick;
    sample.effective_tick = effective_tick;
    sample.value =
        control_id == "vehicle.steering" ? 0.0 : 0.5;
    if (!parent.empty())
        sample.parent_sample_ids.push_back(parent);
    return sample;
}

bool FillTransition(
    const TransitionId& id,
    const std::string& target_id,
    const std::vector<std::string>& control_ids,
    TransitionSample& sample)
{
    TransitionRecord record;
    record.episode_id = id.source.episode;
    record.transition_index = id.source.observation_index;
    record.transition_id = id;
    record.target_id = target_id;
    record.source_time = {id.source.observation_index, 48U};
    record.target_time = {id.target.observation_index, 48U};
    record.effective_steps = {
        id.source.completed_physics_steps,
        id.target.completed_physics_steps,
        static_cast<std::uint32_t>(
            id.target.completed_physics_steps -
            id.source.completed_physics_steps)};
    const std::string suffix =
        std::to_string(record.transition_index);
    for (const std::string& control_id : control_ids)
    {
        const std::string raw =
            "raw/" + suffix + "/" + control_id;
        const std::string issued =
            "issued/" + suffix + "/" + control_id;
        const std::string resolved =
            "resolved/" + suffix + "/" + control_id;
        record.controls.raw.push_back(
            Control(
                raw,
                control_id,
                id.source.completed_physics_steps,
                id.source.completed_physics_steps,
                ""));
        record.controls.issued.push_back(
            Control(
                issued,
                control_id,
                id.source.completed_physics_steps,
                id.source.completed_physics_steps,
                raw));
        record.controls.resolved.push_back(
            Control(
                resolved,
                control_id,
                id.source.completed_physics_steps,
                id.source.completed_physics_steps,
                issued));
    }
    for (std::uint64_t tick = id.source.completed_physics_steps;
         tick < id.target.completed_physics_steps;
         ++tick)
    {
        for (const std::string& control_id : control_ids)
        {
            record.controls.applied.push_back(
                Control(
                    "applied/" + std::to_string(tick) +
                        "/" + control_id,
                    control_id,
                    id.source.completed_physics_steps,
                    tick,
                    "resolved/" + suffix + "/" + control_id));
        }
    }
    record.outcome.status = "running";
    return EncodeTransitionSample(record, sample);
}

class FakeBackend : public CaptureBackend
{
public:
    std::uint64_t completed = 0U;
    std::string target = "truck/main";
    bool fail_begin = false;
    bool fail_advance = false;
    bool fail_join = false;
    bool fail_capture = false;
    bool advance_wrong_count = false;
    bool mutate_during_capture = false;
    bool out_of_range_control = false;
    bool fail_acquire = false;
    bool runtime_owned = false;
    std::vector<std::string> control_ids = {
        "vehicle.throttle"};
    std::vector<std::uint32_t> requested_steps;
    std::vector<std::string> calls;
    mutable unsigned int completed_step_reads = 0U;

    bool AcquireRuntimeOwnership() override
    {
        calls.push_back("acquire");
        if (fail_acquire || runtime_owned)
            return false;
        runtime_owned = true;
        return true;
    }

    void ReleaseRuntimeOwnership() noexcept override
    {
        if (!runtime_owned)
            return;
        calls.push_back("release");
        runtime_owned = false;
    }

    std::uint64_t GetCompletedPhysicsSteps() const override
    {
        ++completed_step_reads;
        return completed;
    }

    bool CaptureResetBaseline(
        const ObservationId& expected,
        ObservationSample& observation) override
    {
        calls.push_back("baseline");
        return FillObservation(expected, target, observation);
    }

    bool BeginTransition(const TransitionId&) override
    {
        calls.push_back("begin");
        return !fail_begin;
    }

    bool AdvanceFixedSteps(std::uint32_t count) override
    {
        calls.push_back("advance");
        requested_steps.push_back(count);
        if (fail_advance)
            return false;
        completed += advance_wrong_count
            ? static_cast<std::uint64_t>(count) + 1U
            : static_cast<std::uint64_t>(count);
        return true;
    }

    bool JoinPhysics() override
    {
        calls.push_back("join");
        return !fail_join;
    }

    bool CaptureCompletedTransition(
        const TransitionId& expected,
        TransitionSample& transition,
        ObservationSample& observation) override
    {
        calls.push_back("capture");
        if (fail_capture)
            return false;
        if (!FillTransition(
                expected,
                target,
                control_ids,
                transition) ||
            !FillObservation(expected.target, target, observation))
        {
            return false;
        }
        if (out_of_range_control)
            transition.record.controls.applied.front().value = 1.5;
        if (mutate_during_capture)
            ++completed;
        return true;
    }
};

class FakeSink : public CaptureSink
{
public:
    bool fail_begin = false;
    bool fail_baseline = false;
    bool fail_append = false;
    bool fail_complete = false;
    int begin_count = 0;
    int baseline_count = 0;
    int append_count = 0;
    int complete_count = 0;
    int abort_count = 0;

    bool BeginEpisode(const CaptureConfig&) override
    {
        ++begin_count;
        return !fail_begin;
    }

    bool AppendBaseline(const ObservationSample&) override
    {
        ++baseline_count;
        return !fail_baseline;
    }

    bool AppendTransitionAndObservation(
        const TransitionSample&,
        const ObservationSample&) override
    {
        ++append_count;
        return !fail_append;
    }

    bool CompleteEpisode() override
    {
        ++complete_count;
        return !fail_complete;
    }

    void AbortEpisode() override
    {
        ++abort_count;
    }
};

CaptureConfig Config(std::uint64_t transitions)
{
    CaptureConfig config;
    config.episode = EpisodeId(1U, 2U);
    config.target_id = "truck/main";
    config.origin_completed_physics_steps = 0U;
    config.maximum_transitions = transitions;
    config.provenance = RoRWorldModelTest::MakeProvenance(
        config.episode,
        config.target_id,
        "test/default",
        "driver/main");
    config.provenance.terrain_sha256 = std::string(64U, 'a');
    config.provenance.reset_state_sha256 = std::string(64U, 'b');
    return config;
}

int HappyPath()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);

    CHECK(session.Begin(Config(6U)));
    CHECK(session.GetLifecycle() == CaptureLifecycle::READY);
    for (std::uint64_t index = 0U; index < 6U; ++index)
        CHECK(session.CaptureNext());
    CHECK((backend.requested_steps ==
        std::vector<std::uint32_t>{41U, 42U, 42U, 41U, 42U, 42U}));
    CHECK(backend.completed == 250U);
    CHECK(session.GetCapturedTransitionCount() == 6U);
    CHECK(session.GetCurrentObservation().observation_index == 6U);
    CHECK(session.Complete());
    CHECK(session.GetLifecycle() == CaptureLifecycle::COMPLETE);
    CHECK(!backend.runtime_owned);
    CHECK(sink.begin_count == 1);
    CHECK(sink.baseline_count == 1);
    CHECK(sink.append_count == 6);
    CHECK(sink.complete_count == 1);
    CHECK(sink.abort_count == 0);
    return 0;
}

int CaptureNextJoinsBeforeReadingOrProviding()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CHECK(session.Begin(Config(1U)));

    backend.calls.clear();
    backend.completed_step_reads = 0U;
    backend.fail_join = true;
    CHECK(!session.CaptureNext());
    CHECK((backend.calls ==
        std::vector<std::string>{"join", "release"}));
    CHECK(backend.completed_step_reads == 0U);
    CHECK(sink.abort_count == 1);
    CHECK(!backend.runtime_owned);
    return 0;
}

int OwnershipFailureStopsBeforeJoinAndProvider()
{
    FakeBackend backend;
    backend.fail_acquire = true;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CHECK(!session.Begin(Config(1U)));
    CHECK((backend.calls ==
        std::vector<std::string>{"acquire"}));
    CHECK(backend.completed_step_reads == 0U);
    CHECK(sink.begin_count == 0);
    CHECK(sink.abort_count == 0);
    return 0;
}

int BoundaryFailureQuarantines()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CHECK(session.Begin(Config(1U)));
    backend.advance_wrong_count = true;
    CHECK(!session.CaptureNext());
    CHECK(session.GetLifecycle() == CaptureLifecycle::FAULTED);
    CHECK(session.GetStatus().error ==
        CaptureError::PHYSICS_BOUNDARY_MISMATCH);
    CHECK(sink.abort_count == 1);
    CHECK(sink.append_count == 0);
    CHECK(!session.CaptureNext());
    CHECK(sink.abort_count == 1);
    return 0;
}

int AdvanceFailureStillJoins()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CHECK(session.Begin(Config(1U)));
    backend.fail_advance = true;
    CHECK(!session.CaptureNext());
    CHECK(session.GetLifecycle() == CaptureLifecycle::FAULTED);
    CHECK((backend.calls ==
        std::vector<std::string>{
            "acquire", "join", "baseline", "join", "begin",
            "advance", "join", "release"}));
    CHECK(sink.abort_count == 1);
    CHECK(!backend.runtime_owned);
    return 0;
}

int RenderMutationQuarantines()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CHECK(session.Begin(Config(1U)));
    backend.mutate_during_capture = true;
    CHECK(!session.CaptureNext());
    CHECK(session.GetStatus().error ==
        CaptureError::PHYSICS_BOUNDARY_MISMATCH);
    CHECK(sink.abort_count == 1);
    return 0;
}

int SinkFailureQuarantines()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CHECK(session.Begin(Config(1U)));
    sink.fail_append = true;
    CHECK(!session.CaptureNext());
    CHECK(session.GetStatus().error == CaptureError::SINK_REJECTED);
    CHECK(sink.abort_count == 1);
    CHECK(sink.complete_count == 0);
    return 0;
}

int CompletionRequiresDeclaredCount()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CHECK(session.Begin(Config(2U)));
    CHECK(session.CaptureNext());
    CHECK(!session.Complete());
    CHECK(session.GetStatus().error ==
        CaptureError::INVALID_TRANSITION);
    CHECK(session.GetLifecycle() == CaptureLifecycle::FAULTED);
    CHECK(sink.abort_count == 1);
    return 0;
}

int ControlProfileMismatchQuarantines()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CaptureConfig config = Config(1U);
    config.provenance.control_ids = {
        "vehicle.steering",
        "vehicle.throttle"};
    config.provenance.controller_profile_sha256 =
        RoRWorldModelTest::HashText(
            "vehicle.steering\nvehicle.throttle");
    CHECK(session.Begin(config));
    CHECK(!session.CaptureNext());
    CHECK(session.GetStatus().error ==
        CaptureError::INVALID_TRANSITION_RECORD);
    CHECK(sink.abort_count == 1);
    CHECK(sink.append_count == 0);
    return 0;
}

int MultiControlProfileAccepted()
{
    FakeBackend backend;
    backend.control_ids = {
        "vehicle.brake",
        "vehicle.steering",
        "vehicle.throttle"};
    FakeSink sink;
    CaptureSession session(backend, sink);
    CaptureConfig config = Config(1U);
    config.provenance.control_ids = backend.control_ids;
    config.provenance.controller_profile_sha256 =
        RoRWorldModelTest::HashText(
            "vehicle.brake\nvehicle.steering\nvehicle.throttle");
    CHECK(session.Begin(config));
    CHECK(session.CaptureNext());
    CHECK(session.Complete());
    CHECK(sink.append_count == 1);
    return 0;
}

int OutOfRangeControlQuarantines()
{
    FakeBackend backend;
    FakeSink sink;
    CaptureSession session(backend, sink);
    CHECK(session.Begin(Config(1U)));
    backend.out_of_range_control = true;
    CHECK(!session.CaptureNext());
    CHECK(session.GetStatus().error ==
        CaptureError::INVALID_TRANSITION_RECORD);
    CHECK(sink.abort_count == 1);
    CHECK(sink.append_count == 0);
    return 0;
}

int NonbinaryParkingBrakeQuarantines()
{
    FakeBackend backend;
    backend.control_ids = {"vehicle.parking-brake"};
    FakeSink sink;
    CaptureSession session(backend, sink);
    CaptureConfig config = Config(1U);
    config.provenance.control_ids = backend.control_ids;
    config.provenance.controller_profile_sha256 =
        RoRWorldModelTest::HashText("vehicle.parking-brake");
    CHECK(session.Begin(config));
    CHECK(!session.CaptureNext());
    CHECK(session.GetStatus().error ==
        CaptureError::INVALID_TRANSITION_RECORD);
    CHECK(sink.abort_count == 1);
    CHECK(sink.append_count == 0);
    return 0;
}

int InvalidRgbRejected()
{
    ObservationSample sample;
    ObservationId id;
    id.episode = EpisodeId(1U, 1U);
    id.observation_index = 0U;
    id.completed_physics_steps = 0U;
    CHECK(FillObservation(id, "truck/main", sample));
    CHECK(ValidateObservationSample(sample, id, "truck/main"));
    sample.rgb8.pop_back();
    CHECK(!ValidateObservationSample(sample, id, "truck/main"));
    return 0;
}

} // namespace

int main()
{
    if (HappyPath() != 0)
        return 1;
    if (BoundaryFailureQuarantines() != 0)
        return 1;
    if (CaptureNextJoinsBeforeReadingOrProviding() != 0)
        return 1;
    if (OwnershipFailureStopsBeforeJoinAndProvider() != 0)
        return 1;
    if (AdvanceFailureStillJoins() != 0)
        return 1;
    if (RenderMutationQuarantines() != 0)
        return 1;
    if (SinkFailureQuarantines() != 0)
        return 1;
    if (CompletionRequiresDeclaredCount() != 0)
        return 1;
    if (ControlProfileMismatchQuarantines() != 0)
        return 1;
    if (MultiControlProfileAccepted() != 0)
        return 1;
    if (OutOfRangeControlQuarantines() != 0)
        return 1;
    if (NonbinaryParkingBrakeQuarantines() != 0)
        return 1;
    if (InvalidRgbRejected() != 0)
        return 1;
    return 0;
}
