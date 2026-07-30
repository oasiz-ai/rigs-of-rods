#include "EpisodeCaptureSink.h"
#include "EpisodeFormat.h"
#include "EpisodeValidator.h"
#include "TestProvenance.h"
#include "WorldModelCaptureEncoding.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace RoR::WorldModel;

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

std::string HexIndex(std::uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string HashText(const std::string& value)
{
    return ComputeSha256(value.data(), value.size()).ToHex();
}

void Identity(std::array<double, 16>& matrix)
{
    matrix = {};
    matrix[0] = 1.0;
    matrix[5] = 1.0;
    matrix[10] = 1.0;
    matrix[15] = 1.0;
}

std::vector<std::uint8_t> Frame(
    std::uint64_t observation_index)
{
    std::vector<std::uint8_t> frame(12U);
    for (std::size_t index = 0U; index < frame.size(); ++index)
    {
        frame[index] = static_cast<std::uint8_t>(
            (observation_index * 29U + index * 7U) & 0xffU);
    }
    return frame;
}

ObservationRecord Observation(
    const EpisodeId& episode,
    const ObservationId& id,
    const std::string& target,
    const std::vector<std::uint8_t>& frame)
{
    ObservationRecord value;
    value.episode_id = episode;
    value.observation_id = id;
    value.frame_id = id.observation_index;
    value.target_id = target;
    value.nominal_time = {
        id.observation_index,
        WORLD_MODEL_OBSERVATION_RATE_HZ};
    if (id.observation_index == 0U)
    {
        value.physics_steps = {
            id.completed_physics_steps,
            id.completed_physics_steps,
            0U};
    }
    else
    {
        ObservationId prior;
        CHECK(MakeObservationId(
            episode,
            0U,
            id.observation_index - 1U,
            prior));
        value.physics_steps = {
            prior.completed_physics_steps,
            id.completed_physics_steps,
            static_cast<std::uint32_t>(
                id.completed_physics_steps -
                prior.completed_physics_steps)};
    }

    const double index = static_cast<double>(id.observation_index);
    value.vehicle.position_m = {index, 1.0, 2.0};
    value.vehicle.linear_velocity_mps = {2.0 + index, 0.0, 0.0};
    value.vehicle.angular_velocity_radps = {0.0, 0.1, 0.0};
    value.vehicle.speed_mps = 2.0 + index;
    value.vehicle.mass_kg = 1250.0;
    value.engine.running = true;
    value.engine.contact = true;
    value.engine.rpm = 900.0 + index * 10.0;
    value.engine.torque_nm = 150.0;
    value.engine.throttle = 0.5;
    value.engine.clutch = 1.0;
    value.engine.gear = 1;
    value.engine.gear_range = 0;
    value.engine.mode = "manual";
    value.engine.timers_seconds = {
        {"shift", 0.0},
        {"starter", 0.0}};
    value.world.world_id = "nhelens";
    value.world.terrain_id = "nhelens/default";
    value.world.terrain_sha256 = HashText("terrain:nhelens");
    value.world.gravity_mps2 = {0.0, -9.81, 0.0};
    value.world.weather_id = "clear";
    value.camera.camera_id = "driver/main";
    value.camera.coordinate_frame = "ror.world.rh-y-up";
    value.camera.position_m = {index, 2.0, 3.0};
    Identity(value.camera.view_matrix);
    Identity(value.camera.projection_matrix);
    value.camera.intrinsics = {
        2.0, 0.0, 1.0,
        0.0, 2.0, 1.0,
        0.0, 0.0, 1.0};
    value.camera.vertical_fov_radians = 1.0;
    value.camera.near_clip_m = 0.1;
    value.camera.far_clip_m = 1000.0;
    CHECK(CanonicalRgbRecordId(
        id.observation_index,
        value.rgb.record_id));
    value.rgb.width = 2U;
    value.rgb.height = 2U;
    value.rgb.row_stride_bytes = 6U;
    value.rgb.pixel_format = "rgb8";
    value.rgb.color_space = "srgb";
    value.rgb.row_origin = "top-left";
    value.rgb.raw_sha256 =
        ComputeSha256(frame.data(), frame.size()).ToHex();
    value.state_sha256 =
        HashText("state:" + std::to_string(id.observation_index));
    return value;
}

ControlSample Control(
    const std::string& sample_id,
    const std::string& parent,
    std::uint64_t source_tick,
    std::uint64_t effective_tick)
{
    ControlSample sample;
    sample.sample_id = sample_id;
    sample.control_id = "vehicle.throttle";
    sample.source_id = "script/worldmodel";
    sample.source_tick = source_tick;
    sample.effective_tick = effective_tick;
    sample.value = 0.5;
    if (!parent.empty())
        sample.parent_sample_ids.push_back(parent);
    return sample;
}

TransitionRecord Transition(
    const EpisodeId& episode,
    const TransitionId& id,
    const std::string& target)
{
    TransitionRecord value;
    value.episode_id = episode;
    value.transition_index =
        id.source.observation_index;
    value.transition_id = id;
    value.target_id = target;
    value.source_time = {
        id.source.observation_index,
        WORLD_MODEL_OBSERVATION_RATE_HZ};
    value.target_time = {
        id.target.observation_index,
        WORLD_MODEL_OBSERVATION_RATE_HZ};
    value.effective_steps = {
        id.source.completed_physics_steps,
        id.target.completed_physics_steps,
        static_cast<std::uint32_t>(
            id.target.completed_physics_steps -
            id.source.completed_physics_steps)};

    const std::string transition =
        HexIndex(value.transition_index);
    const std::string raw = "raw/" + transition;
    const std::string issued = "issued/" + transition;
    const std::string resolved = "resolved/" + transition;
    value.controls.raw.push_back(
        Control(raw, "", id.source.completed_physics_steps,
            id.source.completed_physics_steps));
    value.controls.issued.push_back(
        Control(issued, raw, id.source.completed_physics_steps,
            id.source.completed_physics_steps));
    value.controls.resolved.push_back(
        Control(resolved, issued, id.source.completed_physics_steps,
            id.source.completed_physics_steps));
    for (std::uint64_t tick = id.source.completed_physics_steps;
         tick < id.target.completed_physics_steps;
         ++tick)
    {
        value.controls.applied.push_back(
            Control(
                "applied/" + HexIndex(tick),
                resolved,
                id.source.completed_physics_steps,
                tick));
    }
    value.outcome.status = "running";
    value.outcome.detail = "semantic-fixture";
    return value;
}

class SemanticBackend final : public CaptureBackend
{
public:
    SemanticBackend(EpisodeId episode, std::string target):
        m_episode(episode),
        m_target(std::move(target)),
        m_completed(0U),
        m_active()
    {
    }

    bool AcquireRuntimeOwnership() override
    {
        if (m_runtime_owned)
            return false;
        m_runtime_owned = true;
        return true;
    }

    void ReleaseRuntimeOwnership() noexcept override
    {
        m_runtime_owned = false;
    }

    std::uint64_t GetCompletedPhysicsSteps() const override
    {
        return m_completed;
    }

    bool CaptureResetBaseline(
        const ObservationId& expected,
        ObservationSample& observation) override
    {
        const std::vector<std::uint8_t> frame =
            Frame(expected.observation_index);
        const ObservationRecord record =
            Observation(m_episode, expected, m_target, frame);
        return EncodeObservationSample(
            record,
            frame,
            observation,
            &m_error);
    }

    bool BeginTransition(const TransitionId& transition) override
    {
        m_active = transition;
        return transition.source.completed_physics_steps == m_completed;
    }

    bool AdvanceFixedSteps(std::uint32_t step_count) override
    {
        const std::uint64_t expected =
            m_active.target.completed_physics_steps -
            m_active.source.completed_physics_steps;
        if (step_count != expected)
            return false;
        m_completed += step_count;
        return true;
    }

    bool JoinPhysics() override
    {
        return m_completed ==
            m_active.target.completed_physics_steps;
    }

    bool CaptureCompletedTransition(
        const TransitionId& expected,
        TransitionSample& transition,
        ObservationSample& observation) override
    {
        if (expected != m_active)
            return false;
        const TransitionRecord transition_record =
            Transition(m_episode, expected, m_target);
        if (!EncodeTransitionSample(
                transition_record,
                transition,
                &m_error))
        {
            return false;
        }
        const std::vector<std::uint8_t> frame =
            Frame(expected.target.observation_index);
        const ObservationRecord observation_record =
            Observation(
                m_episode,
                expected.target,
                m_target,
                frame);
        return EncodeObservationSample(
            observation_record,
            frame,
            observation,
            &m_error);
    }

    const std::string& Error() const { return m_error; }

private:
    EpisodeId m_episode;
    std::string m_target;
    std::uint64_t m_completed;
    TransitionId m_active;
    std::string m_error;
    bool m_runtime_owned = false;
};

bool WriteSemanticEpisode(
    const std::filesystem::path& root,
    std::filesystem::path& episode_path,
    const std::string& producer_commit =
        "27ad2540075e45020498840859a0a386a2e5f814")
{
    const EpisodeId episode(0x0123456789abcdefULL, 0x0123456789abcdefULL);
    const std::string target = "truck/main";
    SemanticBackend backend(episode, target);
    EpisodeWriterOptions options;
    options.max_records_per_chunk = 2U;
    options.max_chunk_bytes = 1024U * 1024U;
    EpisodeCaptureSink sink(root, options);
    CaptureSession session(backend, sink);

    CaptureConfig config;
    config.episode = episode;
    config.target_id = target;
    config.origin_completed_physics_steps = 0U;
    config.maximum_transitions = 2U;
    config.provenance = RoRWorldModelTest::MakeProvenance(
        episode,
        target,
        "nhelens/default",
        "driver/main");
    config.provenance.engine_commit = producer_commit;
    config.provenance.terrain_sha256 =
        HashText("terrain:nhelens");
    config.provenance.reset_state_sha256 =
        HashText("state:0");
    if (!session.Begin(config) ||
        !session.CaptureNext() ||
        !session.CaptureNext() ||
        !session.Complete())
    {
        std::cerr << "semantic capture failed: "
                  << sink.GetLastError() << ' '
                  << backend.Error() << '\n';
        return false;
    }
    episode_path = sink.GetFinalDirectory();
    return sink.IsComplete();
}

std::filesystem::path TemporaryRoot()
{
    const std::uint64_t nonce = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now()
            .time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
        ("ror-worldmodel-episode-tests-" + std::to_string(nonce));
}

void TestSemanticEpisode(const std::filesystem::path& root)
{
    std::filesystem::path episode;
    CHECK(WriteSemanticEpisode(root, episode));
    const EpisodeValidationResult result =
        EpisodeValidator::Validate(episode);
    if (!result.IsValid())
    {
        std::cerr << EpisodeValidationErrorName(result.error)
                  << ": " << result.detail << '\n';
    }
    CHECK(result.IsValid());
    CHECK(result.telemetry_record_count == 5U);
    CHECK(result.rgb_record_count == 3U);
    CHECK(result.telemetry_chunk_count == 3U);
    CHECK(result.rgb_chunk_count == 2U);
}

void TestEncodingRejectsRgbMismatch()
{
    const EpisodeId episode(1U, 2U);
    ObservationId id;
    CHECK(MakeObservationId(episode, 0U, 0U, id));
    std::vector<std::uint8_t> frame = Frame(0U);
    ObservationRecord record =
        Observation(episode, id, "truck/main", frame);
    ObservationSample output;
    CHECK(EncodeObservationSample(record, frame, output));
    const std::string original =
        output.record.rgb.raw_sha256;
    frame[0] ^= 0xffU;
    CHECK(!EncodeObservationSample(record, frame, output));
    CHECK(output.record.rgb.raw_sha256 == original);
}

void TestRecordIdentifiers()
{
    std::uint64_t value = 0U;
    CHECK(CanonicalObservationTelemetryRecordId(0U, value) && value == 1U);
    CHECK(CanonicalTransitionTelemetryRecordId(0U, value) && value == 2U);
    CHECK(CanonicalObservationTelemetryRecordId(1U, value) && value == 3U);
    CHECK(CanonicalRgbRecordId(0U, value) && value == 1U);
}

void TestTransitionTargetBinding()
{
    const EpisodeId episode(1U, 2U);
    TransitionId id;
    CHECK(MakeTransitionId(episode, 0U, 0U, id));
    const TransitionRecord record =
        Transition(episode, id, "truck/main");
    TransitionSample sample;
    CHECK(EncodeTransitionSample(record, sample));
    CHECK(ValidateTransitionSample(
        sample,
        id,
        "truck/main"));
    sample.record.target_id = "truck/other";
    CHECK(!ValidateTransitionSample(
        sample,
        id,
        "truck/main"));
}

} // namespace

int main(int argc, char** argv)
{
    if ((argc == 3 || argc == 4) &&
        std::string(argv[1]) == "--emit-fixture")
    {
        const std::filesystem::path root(argv[2]);
        std::error_code error;
        if (std::filesystem::exists(root, error) || error)
        {
            std::cerr << "fixture output root must not already exist\n";
            return 2;
        }
        if (!std::filesystem::create_directories(root, error) || error)
        {
            std::cerr << "could not create fixture output root\n";
            return 2;
        }
        std::filesystem::path episode;
        const std::string producer_commit =
            argc == 4
                ? std::string(argv[3])
                : "27ad2540075e45020498840859a0a386a2e5f814";
        if (!WriteSemanticEpisode(
                root,
                episode,
                producer_commit))
            return 1;
        std::cout << episode.string() << '\n';
        return 0;
    }

    const std::filesystem::path root = TemporaryRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    CHECK(!error);
    TestSemanticEpisode(root);
    TestEncodingRejectsRgbMismatch();
    TestRecordIdentifiers();
    TestTransitionTargetBinding();
    std::filesystem::remove_all(root, error);
    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " world-model episode test(s) failed\n";
        return 1;
    }
    std::cout << "world-model semantic episode tests passed\n";
    return 0;
}
