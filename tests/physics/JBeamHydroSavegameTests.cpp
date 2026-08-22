#include "JBeamHydroSavegame.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "line " << line << ": check failed: "
                  << expression << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

RoR::JBeamHydroRuntimeConfig Config(double factor)
{
    RoR::JBeamHydroRuntimeConfig config;
    config.response.has_factor = true;
    config.response.factor = factor;
    config.response.in_rate = 1.0;
    config.response.out_rate = 2.0;
    config.response.auto_center_rate = 0.5;
    config.has_steering_wheel_lock = true;
    config.steering_wheel_lock = 500.0;
    return config;
}

RoR::JBeamHydroSavegame::LiveHydro Live(
    std::uint32_t hydro_index,
    std::uint16_t beam_index,
    const RoR::JBeamHydroRuntimeConfig& config,
    float saved_runtime_rest_length)
{
    RoR::JBeamHydroSavegame::LiveHydro live;
    live.hydro_index = hydro_index;
    live.beam_index = beam_index;
    live.enabled = true;
    live.reference_length = 2.0f;
    live.saved_runtime_rest_length = saved_runtime_rest_length;
    live.config = config;
    return live;
}

RoR::JBeamHydroSavegame::HydroRecord Record(
    std::uint32_t hydro_index,
    std::uint16_t beam_index,
    const RoR::JBeamHydroRuntimeConfig& config,
    const RoR::JBeamHydroRuntimeState& state)
{
    RoR::JBeamHydroSavegame::HydroRecord record;
    record.hydro_index = hydro_index;
    record.beam_index = beam_index;
    record.reference_length = 2.0f;
    record.config = config;
    record.state = state;
    return record;
}

void TestStagesHealthyAndFaultedHistory()
{
    const RoR::JBeamHydroRuntimeConfig first_config = Config(0.5);
    const RoR::JBeamHydroRuntimeConfig second_config = Config(0.25);
    const RoR::JBeamHydroRuntimeStep first_initial =
        RoR::InitializeJBeamHydroRuntime(first_config, 2.0);
    const RoR::JBeamHydroRuntimeStep first_advanced =
        RoR::AdvanceJBeamHydroRuntime(
            first_config, first_initial.state, 2.0, 1.0, 0.25, false);
    CHECK(first_advanced.valid);
    CHECK(first_advanced.runtime_rest_length == 3.0f);

    const RoR::JBeamHydroRuntimeStep second_initial =
        RoR::InitializeJBeamHydroRuntime(second_config, 2.0);
    const RoR::JBeamHydroRuntimeStep second_faulted =
        RoR::AdvanceJBeamHydroRuntime(
            second_config,
            second_initial.state,
            2.0,
            0.0,
            0.0,
            false);
    CHECK(!second_faulted.valid);
    CHECK(second_faulted.state.fault_latched);

    std::vector<RoR::JBeamHydroSavegame::LiveHydro> live;
    live.push_back(Live(0U, 4U, first_config, 3.0f));
    live.push_back(Live(1U, 7U, second_config, 2.0f));

    RoR::JBeamHydroSavegame::ActorPayload payload;
    payload.hydro_count = 2U;
    payload.records.push_back(
        Record(0U, 4U, first_config, first_advanced.state));
    payload.records.push_back(
        Record(1U, 7U, second_config, second_faulted.state));

    std::vector<RoR::JBeamHydroSavegame::StagedHydro> staged;
    const RoR::JBeamHydroSavegame::Result result =
        RoR::JBeamHydroSavegame::TryStage(payload, live, staged);
    CHECK(result.IsValid());
    CHECK(staged.size() == 2U);
    CHECK(staged[0].hydro_index == 0U);
    CHECK(staged[0].beam_index == 4U);
    CHECK(staged[0].state.accepted_step_count == 1U);
    CHECK(staged[0].state.response.length_ratio == 1.5);
    CHECK(staged[0].runtime_rest_length == 3.0f);
    CHECK(staged[1].state.fault_latched);
    CHECK(staged[1].state.fault ==
        RoR::JBeamHydroRuntimeFault::INVALID_TIMESTEP);
    CHECK(staged[1].runtime_rest_length == 2.0f);
}

void TestLateFailureIsAtomic()
{
    const RoR::JBeamHydroRuntimeConfig config = Config(0.5);
    const RoR::JBeamHydroRuntimeStep initialized =
        RoR::InitializeJBeamHydroRuntime(config, 2.0);
    CHECK(initialized.valid);

    std::vector<RoR::JBeamHydroSavegame::LiveHydro> live;
    live.push_back(Live(0U, 4U, config, 2.0f));
    live.push_back(Live(1U, 7U, config, 2.0f));
    RoR::JBeamHydroSavegame::ActorPayload payload;
    payload.hydro_count = 2U;
    payload.records.push_back(Record(0U, 4U, config, initialized.state));
    payload.records.push_back(Record(1U, 7U, config, initialized.state));
    payload.records[1].beam_index = 8U;

    std::vector<RoR::JBeamHydroSavegame::StagedHydro> staged(1U);
    staged[0].hydro_index = 99U;
    const RoR::JBeamHydroSavegame::Result result =
        RoR::JBeamHydroSavegame::TryStage(payload, live, staged);
    CHECK(result.error ==
        RoR::JBeamHydroSavegame::Error::HYDRO_IDENTITY_MISMATCH);
    CHECK(result.hydro_index == 1U);
    CHECK(staged.size() == 1U);
    CHECK(staged[0].hydro_index == 99U);
}

void TestRejectsConflictsAndMalformedState()
{
    const RoR::JBeamHydroRuntimeConfig config = Config(0.5);
    const RoR::JBeamHydroRuntimeStep initialized =
        RoR::InitializeJBeamHydroRuntime(config, 2.0);
    CHECK(initialized.valid);
    std::vector<RoR::JBeamHydroSavegame::LiveHydro> live;
    live.push_back(Live(0U, 4U, config, 2.0f));

    RoR::JBeamHydroSavegame::ActorPayload payload;
    payload.hydro_count = 1U;
    payload.records.push_back(Record(0U, 4U, config, initialized.state));
    std::vector<RoR::JBeamHydroSavegame::StagedHydro> staged;

    RoR::JBeamHydroSavegame::ActorPayload wrong_schema = payload;
    wrong_schema.schema_version = 2U;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        wrong_schema, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::UNSUPPORTED_SCHEMA);

    RoR::JBeamHydroSavegame::ActorPayload wrong_count = payload;
    wrong_count.hydro_count = 2U;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        wrong_count, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::HYDRO_COUNT_MISMATCH);

    RoR::JBeamHydroSavegame::ActorPayload wrong_config = payload;
    wrong_config.records[0].config.response.factor = 0.25;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        wrong_config, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::CONFIGURATION_MISMATCH);

    std::vector<RoR::JBeamHydroSavegame::LiveHydro>
        wrong_control_binding = live;
    wrong_control_binding[0].control_binding.runtime_control_id++;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        payload, wrong_control_binding, staged).error ==
        RoR::JBeamHydroSavegame::Error::INVALID_CONTROL_BINDING);

    RoR::JBeamHydroSavegame::ActorPayload wrong_reference = payload;
    wrong_reference.records[0].reference_length = 3.0f;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        wrong_reference, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::REFERENCE_LENGTH_MISMATCH);

    std::vector<RoR::JBeamHydroSavegame::LiveHydro> wrong_saved = live;
    wrong_saved[0].saved_runtime_rest_length = 2.5f;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        payload, wrong_saved, staged).error ==
        RoR::JBeamHydroSavegame::Error::SAVED_REST_LENGTH_MISMATCH);

    RoR::JBeamHydroSavegame::ActorPayload invalid_fault = payload;
    invalid_fault.records[0].state.fault_latched = true;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        invalid_fault, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::INVALID_FAULT);

    invalid_fault = payload;
    invalid_fault.records[0].state.fault =
        static_cast<RoR::JBeamHydroRuntimeFault>(999);
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        invalid_fault, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::INVALID_FAULT);

    RoR::JBeamHydroSavegame::ActorPayload invalid_state = payload;
    std::uint64_t nan_bits = UINT64_C(0x7ff8000000000001);
    std::memcpy(
        &invalid_state.records[0].state.response.length_ratio,
        &nan_bits,
        sizeof(nan_bits));
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        invalid_state, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::INVALID_STATE);
}

void TestRequiresEveryEnabledHydroExactlyOnce()
{
    const RoR::JBeamHydroRuntimeConfig config = Config(0.5);
    const RoR::JBeamHydroRuntimeStep initialized =
        RoR::InitializeJBeamHydroRuntime(config, 2.0);
    std::vector<RoR::JBeamHydroSavegame::LiveHydro> live;
    live.push_back(Live(0U, 4U, config, 2.0f));
    live.push_back(Live(1U, 7U, config, 2.0f));

    RoR::JBeamHydroSavegame::ActorPayload missing;
    missing.hydro_count = 2U;
    missing.records.push_back(Record(0U, 4U, config, initialized.state));
    std::vector<RoR::JBeamHydroSavegame::StagedHydro> staged;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        missing, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::RECORD_COUNT_MISMATCH);

    RoR::JBeamHydroSavegame::ActorPayload duplicate;
    duplicate.hydro_count = 2U;
    duplicate.records.push_back(Record(0U, 4U, config, initialized.state));
    duplicate.records.push_back(Record(0U, 4U, config, initialized.state));
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        duplicate, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::RECORD_ORDER);

    live[1].enabled = false;
    RoR::JBeamHydroSavegame::ActorPayload disabled;
    disabled.hydro_count = 2U;
    disabled.records.push_back(Record(1U, 7U, config, initialized.state));
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        disabled, live, staged).error ==
        RoR::JBeamHydroSavegame::Error::ENABLEMENT_MISMATCH);
}

void TestCaptureValidatesBeforePublishing()
{
    const RoR::JBeamHydroRuntimeConfig config = Config(0.5);
    const RoR::JBeamHydroRuntimeStep initialized =
        RoR::InitializeJBeamHydroRuntime(config, 2.0);
    const RoR::JBeamHydroRuntimeStep advanced =
        RoR::AdvanceJBeamHydroRuntime(
            config,
            initialized.state,
            2.0,
            1.0,
            0.25,
            false);
    CHECK(advanced.valid);

    std::vector<RoR::JBeamHydroSavegame::LiveHydro> live;
    live.push_back(Live(0U, 4U, config, 3.0f));
    live[0].runtime_state = advanced.state;
    live.push_back(Live(1U, 7U, config, 0.0f));
    live[1].enabled = false;

    RoR::JBeamHydroSavegame::ActorPayload captured;
    CHECK(RoR::JBeamHydroSavegame::TryCapture(
        live, captured).IsValid());
    CHECK(captured.hydro_count == 2U);
    CHECK(captured.records.size() == 1U);
    CHECK(captured.records[0].hydro_index == 0U);
    CHECK(captured.records[0].beam_index == 4U);
    CHECK(captured.records[0].state.accepted_step_count == 1U);
    CHECK(captured.records[0].state.response.length_ratio == 1.5);

    RoR::JBeamHydroSavegame::ActorPayload untouched;
    untouched.schema_version = 77U;
    untouched.hydro_count = 88U;
    untouched.records.resize(1U);
    untouched.records[0].hydro_index = 99U;
    live[0].saved_runtime_rest_length = 2.5f;
    CHECK(RoR::JBeamHydroSavegame::TryCapture(
        live, untouched).error ==
        RoR::JBeamHydroSavegame::Error::SAVED_REST_LENGTH_MISMATCH);
    CHECK(untouched.schema_version == 77U);
    CHECK(untouched.hydro_count == 88U);
    CHECK(untouched.records.size() == 1U);
    CHECK(untouched.records[0].hydro_index == 99U);

    std::vector<RoR::JBeamHydroSavegame::LiveHydro> excessive(
        static_cast<std::size_t>(
            RoR::JBeamHydroSavegame::MAX_HYDRO_COUNT) + 1U);
    CHECK(RoR::JBeamHydroSavegame::TryCapture(
        excessive, untouched).error ==
        RoR::JBeamHydroSavegame::Error::HYDRO_COUNT_LIMIT);
    CHECK(untouched.schema_version == 77U);
}

} // namespace

int main()
{
    TestStagesHealthyAndFaultedHistory();
    TestLateFailureIsAtomic();
    TestRejectsConflictsAndMalformedState();
    TestRequiresEveryEnabledHydroExactlyOnce();
    TestCaptureValidatesBeforePublishing();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "JBeam hydro savegame tests passed\n";
    return 0;
}
