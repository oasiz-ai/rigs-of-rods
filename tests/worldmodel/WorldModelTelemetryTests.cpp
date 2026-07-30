#include "WorldModelTelemetry.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

using namespace RoR::WorldModel;

namespace {

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

std::string Hash(char digit)
{
    return std::string(64U, digit);
}

std::string PaddedTick(std::uint64_t tick)
{
    std::ostringstream stream;
    stream << std::setw(4) << std::setfill('0') << tick;
    return stream.str();
}

void Identity(std::array<double, 16>& matrix)
{
    matrix = {};
    matrix[0] = 1.0;
    matrix[5] = 1.0;
    matrix[10] = 1.0;
    matrix[15] = 1.0;
}

ObservationRecord ValidObservation()
{
    ObservationRecord value;
    value.episode_id = EpisodeId(1U, 2U);
    CHECK(MakeObservationId(
        value.episode_id, 0U, 1U, value.observation_id));
    value.frame_id = 1U;
    value.target_id = "truck/main";
    value.nominal_time = {1U, 48U};
    value.physics_steps = {0U, 41U, 41U};
    value.vehicle.position_m = {1.0, 2.0, 3.0};
    value.vehicle.linear_velocity_mps = {4.0, 0.0, 0.0};
    value.vehicle.angular_velocity_radps = {0.0, 0.5, 0.0};
    value.vehicle.speed_mps = 4.0;
    value.vehicle.mass_kg = 1200.0;
    value.engine.running = true;
    value.engine.contact = true;
    value.engine.rpm = 1500.0;
    value.engine.torque_nm = 200.0;
    value.engine.throttle = 0.25;
    value.engine.clutch = 1.0;
    value.engine.gear = 2;
    value.engine.gear_range = 1;
    value.engine.mode = "automatic";
    value.engine.timers_seconds = {{"shift", 0.1}, {"starter", 0.0}};
    value.world.world_id = "nhelens";
    value.world.terrain_id = "nhelens/default";
    value.world.terrain_sha256 = Hash('a');
    value.world.gravity_mps2 = {0.0, -9.81, 0.0};
    value.world.weather_id = "clear";
    value.camera.camera_id = "driver/main";
    value.camera.coordinate_frame = "ror.world.rh-y-up";
    value.camera.position_m = {1.0, 3.0, 2.0};
    Identity(value.camera.view_matrix);
    Identity(value.camera.projection_matrix);
    value.camera.intrinsics = {
        500.0, 0.0, 320.0,
        0.0, 500.0, 240.0,
        0.0, 0.0, 1.0};
    value.camera.vertical_fov_radians = 1.0;
    value.camera.near_clip_m = 0.1;
    value.camera.far_clip_m = 1000.0;
    value.rgb.record_id = 2U;
    value.rgb.pixel_format = "rgb8";
    value.rgb.color_space = "srgb";
    value.rgb.row_origin = "top-left";
    value.rgb.width = 640U;
    value.rgb.height = 480U;
    value.rgb.row_stride_bytes = 1920U;
    value.rgb.raw_sha256 = Hash('b');
    value.contacts.contact_count = 2U;
    value.contacts.wheel_contact_count = 2U;
    value.contacts.maximum_normal_impulse_ns = 12.0;
    value.contacts.maximum_penetration_m = 0.01;
    value.state_sha256 = Hash('c');
    return value;
}

ControlSample Sample(
    const char* sample,
    const char* control,
    std::uint64_t tick,
    double value,
    std::vector<std::string> parents = {})
{
    ControlSample result;
    result.sample_id = sample;
    result.control_id = control;
    result.source_id = "gamepad/0";
    result.source_tick = tick;
    result.effective_tick = tick;
    result.value = value;
    result.parent_sample_ids = std::move(parents);
    return result;
}

TransitionRecord ValidTransition()
{
    TransitionRecord value;
    value.episode_id = EpisodeId(1U, 2U);
    CHECK(MakeTransitionId(
        value.episode_id, 0U, 0U, value.transition_id));
    value.transition_index = 0U;
    value.target_id = "truck/main";
    value.source_time = {0U, 48U};
    value.target_time = {1U, 48U};
    value.effective_steps = {0U, 41U, 41U};
    value.controls.raw.push_back(
        Sample("raw/0", "vehicle.throttle", 0U, 0.25));
    value.controls.issued.push_back(
        Sample("issued/0", "vehicle.throttle", 0U, 0.25, {"raw/0"}));
    value.controls.resolved.push_back(
        Sample("resolved/0", "vehicle.throttle", 0U, 0.25, {"issued/0"}));
    for (std::uint64_t tick = 0U; tick < 41U; ++tick)
    {
        value.controls.applied.push_back(
            Sample(
                ("applied/" + PaddedTick(tick)).c_str(),
                "vehicle.throttle",
                tick,
                0.25,
                {"resolved/0"}));
    }
    value.contacts.contact_count = 1U;
    value.contacts.wheel_contact_count = 1U;
    value.events.push_back({"event/0", "wheel.contact", 0U, "left-front"});
    value.outcome.status = "running";
    value.outcome.reward = 1.0;
    value.outcome.detail = "ok";
    return value;
}

void TestGoldenObservation()
{
    const ObservationRecord value = ValidObservation();
    std::string json;
    std::string error;
    CHECK(SerializeObservationRecord(value, json, &error));
    const std::string expected =
        "{\"schema\":\"org.rigsofrods.worldmodel.observation@1.0\","
        "\"episode_id\":\"00000000000000010000000000000002\","
        "\"observation_id\":{\"index\":1,\"completed_physics_steps\":41},"
        "\"frame_id\":1,\"target_id\":\"truck/main\","
        "\"nominal_time\":{\"numerator\":1,\"denominator\":48},"
        "\"physics_steps\":{\"first_completed_step\":0,"
        "\"last_completed_step\":41,\"substep_count\":41},"
        "\"vehicle\":{\"position_m\":{\"x\":1,\"y\":2,\"z\":3},"
        "\"orientation_world_from_vehicle_wxyz\":{\"w\":1,\"x\":0,\"y\":0,\"z\":0},"
        "\"linear_velocity_mps\":{\"x\":4,\"y\":0,\"z\":0},"
        "\"angular_velocity_radps\":{\"x\":0,\"y\":0.5,\"z\":0},"
        "\"speed_mps\":4,\"mass_kg\":1200},"
        "\"engine\":{\"running\":true,\"contact\":true,\"starter\":false,"
        "\"rpm\":1500,\"torque_nm\":200,\"throttle\":0.25,\"clutch\":1,"
        "\"gear\":2,\"gear_range\":1,\"mode\":\"automatic\","
        "\"timers_seconds\":{\"shift\":0.10000000000000001,\"starter\":0}},"
        "\"world\":{\"world_id\":\"nhelens\",\"terrain_id\":\"nhelens/default\","
        "\"terrain_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"gravity_mps2\":{\"x\":0,\"y\":-9.8100000000000005,\"z\":0},"
        "\"water_enabled\":false,\"water_level_m\":0,\"weather_id\":\"clear\"},"
        "\"camera\":{\"camera_id\":\"driver/main\","
        "\"coordinate_frame\":\"ror.world.rh-y-up\","
        "\"position_m\":{\"x\":1,\"y\":3,\"z\":2},"
        "\"orientation_world_from_camera_wxyz\":{\"w\":1,\"x\":0,\"y\":0,\"z\":0},"
        "\"view_matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"
        "\"projection_matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"
        "\"intrinsics\":[500,0,320,0,500,240,0,0,1],"
        "\"vertical_fov_radians\":1,\"near_clip_m\":0.10000000000000001,"
        "\"far_clip_m\":1000},"
        "\"rgb\":{\"record_id\":2,\"pixel_format\":\"rgb8\","
        "\"color_space\":\"srgb\",\"row_origin\":\"top-left\",\"width\":640,"
        "\"height\":480,\"row_stride_bytes\":1920,"
        "\"raw_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"},"
        "\"contacts\":{\"contact_count\":2,\"wheel_contact_count\":2,"
        "\"maximum_normal_impulse_ns\":12,"
        "\"maximum_penetration_m\":0.01},"
        "\"state_sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"}";
    CHECK(json == expected);
}

void TestGoldenTransition()
{
    const TransitionRecord value = ValidTransition();
    std::string json;
    std::string error;
    CHECK(SerializeTransitionRecord(value, json, &error));
    std::string expected =
        "{\"schema\":\"org.rigsofrods.worldmodel.transition@1.0\","
        "\"episode_id\":\"00000000000000010000000000000002\","
        "\"transition_id\":{\"index\":0,"
        "\"source\":{\"index\":0,\"completed_physics_steps\":0},"
        "\"target\":{\"index\":1,\"completed_physics_steps\":41}},"
        "\"target_id\":\"truck/main\","
        "\"source_time\":{\"numerator\":0,\"denominator\":48},"
        "\"target_time\":{\"numerator\":1,\"denominator\":48},"
        "\"effective_steps\":{\"first_completed_step\":0,"
        "\"last_completed_step\":41,\"substep_count\":41},"
        "\"controls\":{\"raw\":[{\"sample_id\":\"raw/0\","
        "\"control_id\":\"vehicle.throttle\",\"source_id\":\"gamepad/0\","
        "\"source_tick\":0,\"effective_tick\":0,\"value\":0.25,"
        "\"parent_sample_ids\":[]}],"
        "\"issued\":[{\"sample_id\":\"issued/0\","
        "\"control_id\":\"vehicle.throttle\",\"source_id\":\"gamepad/0\","
        "\"source_tick\":0,\"effective_tick\":0,\"value\":0.25,"
        "\"parent_sample_ids\":[\"raw/0\"]}],"
        "\"resolved\":[{\"sample_id\":\"resolved/0\","
        "\"control_id\":\"vehicle.throttle\",\"source_id\":\"gamepad/0\","
        "\"source_tick\":0,\"effective_tick\":0,\"value\":0.25,"
        "\"parent_sample_ids\":[\"issued/0\"]}],"
        "\"applied\":[";
    for (std::uint64_t tick = 0U; tick < 41U; ++tick)
    {
        if (tick != 0U)
            expected += ',';
        expected +=
            "{\"sample_id\":\"applied/" + PaddedTick(tick) + "\","
            "\"control_id\":\"vehicle.throttle\",\"source_id\":\"gamepad/0\","
            "\"source_tick\":" + std::to_string(tick) +
            ",\"effective_tick\":" + std::to_string(tick) +
            ",\"value\":0.25,\"parent_sample_ids\":[\"resolved/0\"]}";
    }
    expected +=
        "]},\"contacts\":{\"contact_count\":1,\"wheel_contact_count\":1,"
        "\"maximum_normal_impulse_ns\":0,\"maximum_penetration_m\":0},"
        "\"events\":[{\"event_id\":\"event/0\",\"event_type\":\"wheel.contact\","
        "\"physics_tick\":0,\"detail\":\"left-front\"}],"
        "\"outcome\":{\"status\":\"running\",\"terminal\":false,"
        "\"reset\":false,\"success\":false,\"reward\":1,\"detail\":\"ok\"}}";
    CHECK(json == expected);
}

void TestHostileValuesFailClosed()
{
    std::string output = "unchanged";
    std::string error;

    ObservationRecord observation = ValidObservation();
    observation.vehicle.speed_mps = std::numeric_limits<double>::quiet_NaN();
    CHECK(!SerializeObservationRecord(observation, output, &error));
    CHECK(output == "unchanged");

    observation = ValidObservation();
    observation.engine.throttle = -0.0;
    CHECK(!ValidateObservationRecord(observation, &error));

    observation = ValidObservation();
    observation.rgb.raw_sha256[0] = 'A';
    CHECK(!ValidateObservationRecord(observation, &error));

    observation = ValidObservation();
    observation.world.weather_id = "Clear";
    CHECK(!ValidateObservationRecord(observation, &error));

    TransitionRecord transition = ValidTransition();
    transition.controls.applied[0].parent_sample_ids[0] = "missing/0";
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.controls.resolved[0].effective_tick = 1U;
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.controls.resolved[0].control_id = "vehicle.steering";
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.controls.raw.push_back(transition.controls.raw.front());
    transition.controls.raw.back().sample_id = "raw/1";
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.controls.raw[0].control_id = "Vehicle.Throttle";
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.controls.raw[0].value = -0.0;
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.controls.applied.erase(
        transition.controls.applied.begin() + 20);
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.events[0].detail =
        std::string("\xc0\xaf", 2U); // overlong UTF-8
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.events.push_back(transition.events.front());
    transition.events.back().physics_tick = 1U;
    CHECK(!ValidateTransitionRecord(transition, &error));

    transition = ValidTransition();
    transition.transition_id.target.completed_physics_steps = 42U;
    CHECK(!ValidateTransitionRecord(transition, &error));
}

void TestIdentifiersCannotBecomePaths()
{
    CHECK(IsCanonicalWorldModelIdentifier("vehicle/truck-main"));
    CHECK(IsCanonicalWorldModelIdentifier("ror.world.rh-y-up"));
    CHECK(!IsCanonicalWorldModelIdentifier("../escape"));
    CHECK(!IsCanonicalWorldModelIdentifier("vehicle//truck"));
    CHECK(!IsCanonicalWorldModelIdentifier("vehicle/./truck"));
    CHECK(!IsCanonicalWorldModelIdentifier("vehicle/truck/"));
    CHECK(!IsCanonicalWorldModelIdentifier("/vehicle/truck"));
}

} // namespace

int main()
{
    TestGoldenObservation();
    TestGoldenTransition();
    TestHostileValuesFailClosed();
    TestIdentifiersCannotBecomePaths();
    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " world-model telemetry test(s) failed\n";
        return 1;
    }
    std::cout << "WorldModelTelemetryTests passed\n";
    return 0;
}
