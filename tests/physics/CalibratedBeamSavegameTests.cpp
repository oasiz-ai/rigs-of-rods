#include "CalibratedBeamSavegame.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "line " << line
                  << ": check failed: " << expression << '\n';
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
    double value = 0.0;
    const volatile unsigned char* const source =
        reinterpret_cast<const volatile unsigned char*>(&bits);
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index)
        destination[index] = source[index];
    return value;
}

RoR::CalibratedBeamMaterialAdapter::Configuration Configuration()
{
    RoR::CalibratedBeamMaterialAdapter::Configuration configuration;
    configuration.cross_section_area_m2 = 0.002;
    configuration.material.elastic_modulus = 200.0e9;
    configuration.material.yield_stress = 250.0e6;
    configuration.material.hardening_modulus = 2.0e9;
    configuration.material.damage_onset_plastic_strain = 0.04;
    configuration.material.damage_driver_capacity_density = 50.0e6;
    return configuration;
}

RoR::CalibratedBeamMaterialAdapter::Runtime ConfiguredRuntime()
{
    RoR::CalibratedBeamMaterialAdapter::Runtime runtime;
    CHECK(RoR::CalibratedBeamMaterialAdapter::TryConfigure(
        runtime,
        Configuration()));
    return runtime;
}

RoR::CalibratedBeamMaterialAdapter::StepInput Input(double strain)
{
    RoR::CalibratedBeamMaterialAdapter::StepInput input;
    input.reference_length_m = 1.0;
    input.current_length_m = 1.0 + strain;
    input.damping_force_n = 125.0;
    input.direction = {{1.0, 0.0, 0.0}};
    input.is_plain_axial_beam = true;
    return input;
}

std::vector<RoR::CalibratedBeamSavegame::LiveBeam> LiveBeams()
{
    std::vector<RoR::CalibratedBeamSavegame::LiveBeam> beams(3);
    for (std::uint32_t index = 0; index < beams.size(); ++index)
    {
        beams[index].beam_index = index;
        beams[index].node_1 = static_cast<std::int32_t>(index);
        beams[index].node_2 = static_cast<std::int32_t>(index + 1U);
        beams[index].beam_type = 0;
        beams[index].special_beam = 0;
        beams[index].is_plain_axial_beam = true;
    }
    beams[1].authored_runtime = ConfiguredRuntime();
    return beams;
}

RoR::CalibratedBeamSavegame::ActorPayload Payload(
    const std::vector<RoR::CalibratedBeamSavegame::LiveBeam>& live,
    const RoR::CalibratedBeamMaterialAdapter::Runtime& saved_runtime)
{
    RoR::CalibratedBeamSavegame::ActorPayload payload;
    payload.beam_count = static_cast<std::uint32_t>(live.size());
    RoR::CalibratedBeamSavegame::BeamRecord record;
    record.beam_index = 1;
    record.node_1 = live[1].node_1;
    record.node_2 = live[1].node_2;
    record.beam_type = live[1].beam_type;
    record.special_beam = live[1].special_beam;
    record.disabled = live[1].saved_disabled;
    record.broken = live[1].saved_broken;
    record.runtime = saved_runtime;
    payload.records.push_back(record);
    return payload;
}

bool SameState(
    const RoR::CalibratedBeamMaterial::State& first,
    const RoR::CalibratedBeamMaterial::State& second)
{
    return
        DoubleBits(first.plastic_strain) ==
            DoubleBits(second.plastic_strain) &&
        DoubleBits(first.accumulated_plastic_strain) ==
            DoubleBits(second.accumulated_plastic_strain) &&
        DoubleBits(first.damage) == DoubleBits(second.damage) &&
        DoubleBits(first.damage_driver_density) ==
            DoubleBits(second.damage_driver_density) &&
        DoubleBits(first.last_total_strain) ==
            DoubleBits(second.last_total_strain) &&
        first.fractured == second.fractured;
}

bool SameRuntime(
    const RoR::CalibratedBeamMaterialAdapter::Runtime& first,
    const RoR::CalibratedBeamMaterialAdapter::Runtime& second)
{
    return
        first.enabled == second.enabled &&
        first.faulted == second.faulted &&
        RoR::CalibratedBeamSavegame::SameConfiguration(
            first.configuration,
            second.configuration) &&
        SameState(first.state, second.state) &&
        first.last_error == second.last_error &&
        first.last_material_error == second.last_material_error;
}

void CheckRejectedAtomically(
    const RoR::CalibratedBeamSavegame::ActorPayload& payload,
    const std::vector<RoR::CalibratedBeamSavegame::LiveBeam>& live,
    RoR::CalibratedBeamSavegame::Error expected)
{
    std::vector<RoR::CalibratedBeamSavegame::StagedBeam> staged(1);
    staged[0].beam_index = 999U;
    staged[0].runtime = ConfiguredRuntime();
    staged[0].runtime.state.last_total_strain = 0.00025;
    const std::vector<RoR::CalibratedBeamSavegame::StagedBeam> before =
        staged;
    const RoR::CalibratedBeamSavegame::Result result =
        RoR::CalibratedBeamSavegame::TryStage(
            payload,
            live,
            staged);
    CHECK(result.error == expected);
    CHECK(staged.size() == before.size());
    CHECK(staged[0].beam_index == before[0].beam_index);
    CHECK(SameRuntime(staged[0].runtime, before[0].runtime));
}

void TestExactContinuation()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;
    std::vector<RoR::CalibratedBeamSavegame::LiveBeam> live =
        LiveBeams();
    Runtime control = ConfiguredRuntime();
    CHECK(Step(control, Input(0.0025)).IsValid());
    CHECK(Step(control, Input(0.0040)).IsValid());
    CHECK(control.state.accumulated_plastic_strain > 0.0);

    const RoR::CalibratedBeamSavegame::ActorPayload payload =
        Payload(live, control);
    std::vector<RoR::CalibratedBeamSavegame::StagedBeam> staged;
    const RoR::CalibratedBeamSavegame::Result result =
        RoR::CalibratedBeamSavegame::TryStage(payload, live, staged);
    CHECK(result.IsValid());
    CHECK(staged.size() == 1U);
    CHECK(staged[0].beam_index == 1U);
    CHECK(SameRuntime(staged[0].runtime, control));

    Runtime restored = staged[0].runtime;
    const StepResult control_next = Step(control, Input(-0.0010));
    const StepResult restored_next = Step(restored, Input(-0.0010));
    CHECK(control_next.IsValid());
    CHECK(restored_next.IsValid());
    CHECK(SameRuntime(control, restored));
    CHECK(DoubleBits(control_next.axial_force_n) ==
        DoubleBits(restored_next.axial_force_n));
    CHECK(DoubleBits(control_next.stored_energy_j) ==
        DoubleBits(restored_next.stored_energy_j));
    CHECK(DoubleBits(control_next.dissipated_energy_increment_j) ==
        DoubleBits(restored_next.dissipated_energy_increment_j));
}

void TestFaultAndFractureState()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    {
        std::vector<RoR::CalibratedBeamSavegame::LiveBeam> live =
            LiveBeams();
        Runtime faulted = ConfiguredRuntime();
        CHECK(Step(faulted, Input(0.0025)).IsValid());
        LatchFailure(faulted, Error::INVALID_DIRECTION);
        live[1].saved_disabled = true;
        RoR::CalibratedBeamSavegame::ActorPayload payload =
            Payload(live, faulted);
        payload.records[0].disabled = true;
        std::vector<RoR::CalibratedBeamSavegame::StagedBeam> staged;
        CHECK(RoR::CalibratedBeamSavegame::TryStage(
            payload,
            live,
            staged).IsValid());
        CHECK(staged[0].runtime.faulted);
        CHECK(
            staged[0].runtime.last_error ==
            Error::INVALID_DIRECTION);
    }

    {
        std::vector<RoR::CalibratedBeamSavegame::LiveBeam> live =
            LiveBeams();
        Runtime fractured;
        RoR::CalibratedBeamMaterialAdapter::Configuration configuration =
            Configuration();
        configuration.material.damage_onset_plastic_strain = 0.0001;
        configuration.material.damage_driver_capacity_density = 1.0e5;
        CHECK(TryConfigure(fractured, configuration));
        live[1].authored_runtime = fractured;
        const StepResult response = Step(fractured, Input(0.02));
        CHECK(response.IsValid());
        CHECK(fractured.state.fractured);
        live[1].saved_disabled = true;
        live[1].saved_broken = true;
        RoR::CalibratedBeamSavegame::ActorPayload payload =
            Payload(live, fractured);
        payload.records[0].disabled = true;
        payload.records[0].broken = true;
        std::vector<RoR::CalibratedBeamSavegame::StagedBeam> staged;
        CHECK(RoR::CalibratedBeamSavegame::TryStage(
            payload,
            live,
            staged).IsValid());
        CHECK(staged[0].runtime.state.fractured);
    }
}

void TestLegacyEmptyAndStructuralValidation()
{
    std::vector<RoR::CalibratedBeamSavegame::LiveBeam> no_material(2);
    for (std::uint32_t index = 0; index < no_material.size(); ++index)
    {
        no_material[index].beam_index = index;
        no_material[index].node_1 = static_cast<std::int32_t>(index);
        no_material[index].node_2 =
            static_cast<std::int32_t>(index + 1U);
        no_material[index].is_plain_axial_beam = true;
    }
    RoR::CalibratedBeamSavegame::ActorPayload empty;
    empty.beam_count = 2;
    std::vector<RoR::CalibratedBeamSavegame::StagedBeam> staged;
    CHECK(RoR::CalibratedBeamSavegame::TryStage(
        empty,
        no_material,
        staged).IsValid());
    CHECK(staged.empty());

    const std::vector<RoR::CalibratedBeamSavegame::LiveBeam> live =
        LiveBeams();
    const RoR::CalibratedBeamMaterialAdapter::Runtime runtime =
        ConfiguredRuntime();
    RoR::CalibratedBeamSavegame::ActorPayload payload =
        Payload(live, runtime);

    RoR::CalibratedBeamSavegame::ActorPayload changed = payload;
    changed.schema_version = 2;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::UNSUPPORTED_SCHEMA);
    changed = payload;
    changed.beam_count = 2;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::BEAM_COUNT_MISMATCH);
    changed = payload;
    changed.records.clear();
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::RECORD_COUNT_MISMATCH);
    changed = payload;
    changed.records.push_back(changed.records[0]);
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::RECORD_COUNT_MISMATCH);

    std::vector<RoR::CalibratedBeamSavegame::LiveBeam> two_enabled = live;
    two_enabled[2].authored_runtime = ConfiguredRuntime();
    changed = payload;
    changed.records.push_back(changed.records[0]);
    changed.records[1].beam_index = 1;
    CheckRejectedAtomically(
        changed,
        two_enabled,
        RoR::CalibratedBeamSavegame::Error::RECORD_ORDER);

    changed = payload;
    changed.records[0].node_2 += 1;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::BEAM_IDENTITY_MISMATCH);
    changed = payload;
    changed.records[0].runtime.enabled = false;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::ENABLEMENT_MISMATCH);
    std::vector<RoR::CalibratedBeamSavegame::LiveBeam> wrong_role = live;
    wrong_role[1].is_plain_axial_beam = false;
    CheckRejectedAtomically(
        payload,
        wrong_role,
        RoR::CalibratedBeamSavegame::Error::UNSUPPORTED_BEAM_ROLE);
}

void TestConfigurationHistoryAndFlagValidation()
{
    std::vector<RoR::CalibratedBeamSavegame::LiveBeam> live =
        LiveBeams();
    const RoR::CalibratedBeamMaterialAdapter::Runtime runtime =
        ConfiguredRuntime();
    const RoR::CalibratedBeamSavegame::ActorPayload original =
        Payload(live, runtime);
    RoR::CalibratedBeamSavegame::ActorPayload changed = original;

    changed.records[0].runtime.configuration.cross_section_area_m2 =
        0.003;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::CONFIGURATION_MISMATCH);

    changed = original;
    changed.records[0].runtime.configuration =
        live[1].authored_runtime.configuration;
    changed.records[0].runtime.configuration.material.schema_version = 2;
    live[1].authored_runtime.configuration.material.schema_version = 2;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::INVALID_CONFIGURATION);
    live = LiveBeams();

    changed = original;
    changed.records[0].runtime.state.damage = 1.5;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::INVALID_HISTORY);
    changed = original;
    changed.records[0].runtime.state.last_total_strain =
        DoubleFromBits(UINT64_C(0x7ff8000000000042));
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::INVALID_HISTORY);

    changed = original;
    changed.records[0].runtime.last_error =
        static_cast<RoR::CalibratedBeamMaterialAdapter::Error>(999);
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::INVALID_ERROR_CODE);
    changed = original;
    changed.records[0].runtime.faulted = true;
    changed.records[0].runtime.last_error =
        RoR::CalibratedBeamMaterialAdapter::Error::INVALID_DIRECTION;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::INVALID_FAULT_STATE);
    changed.records[0].disabled = true;
    changed.records[0].runtime.last_material_error =
        RoR::CalibratedBeamMaterial::Error::INVALID_STATE;
    live[1].saved_disabled = true;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::INVALID_FAULT_STATE);

    live = LiveBeams();
    changed = original;
    changed.records[0].runtime.state.fractured = true;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::INVALID_HISTORY);
    changed = original;
    changed.records[0].disabled = true;
    CheckRejectedAtomically(
        changed,
        live,
        RoR::CalibratedBeamSavegame::Error::FLAG_MISMATCH);
}

void TestConfigurationBitIdentity()
{
    CHECK(!RoR::CalibratedBeamSavegame::ExactDoubleEqual(0.0, -0.0));
    CHECK(RoR::CalibratedBeamSavegame::ExactDoubleEqual(
        std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::denorm_min()));
}

} // namespace

int main()
{
    TestExactContinuation();
    TestFaultAndFractureState();
    TestLegacyEmptyAndStructuralValidation();
    TestConfigurationHistoryAndFlagValidation();
    TestConfigurationBitIdentity();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
