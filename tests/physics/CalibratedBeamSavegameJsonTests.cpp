#include "CalibratedBeamSavegameJson.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
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
        DoubleBits(first.state.plastic_strain) ==
            DoubleBits(second.state.plastic_strain) &&
        DoubleBits(first.state.accumulated_plastic_strain) ==
            DoubleBits(second.state.accumulated_plastic_strain) &&
        DoubleBits(first.state.damage) ==
            DoubleBits(second.state.damage) &&
        DoubleBits(first.state.damage_driver_density) ==
            DoubleBits(second.state.damage_driver_density) &&
        DoubleBits(first.state.last_total_strain) ==
            DoubleBits(second.state.last_total_strain) &&
        first.state.fractured == second.state.fractured &&
        first.last_error == second.last_error &&
        first.last_material_error == second.last_material_error;
}

void TestProductionJsonRoundTripAndStrictShape()
{
    using namespace RoR::CalibratedBeamMaterialAdapter;

    Runtime authored;
    CHECK(TryConfigure(authored, Configuration()));
    Runtime saved = authored;
    CHECK(Step(saved, Input(0.0025)).IsValid());
    CHECK(Step(saved, Input(0.0040)).IsValid());

    std::vector<RoR::CalibratedBeamSavegame::LiveBeam> live(3);
    for (std::uint32_t index = 0; index < live.size(); ++index)
    {
        live[index].beam_index = index;
        live[index].node_1 = static_cast<std::int32_t>(index);
        live[index].node_2 = static_cast<std::int32_t>(index + 1U);
        live[index].beam_type = 0;
        live[index].special_beam = 0;
        live[index].is_plain_axial_beam = true;
    }
    live[1].authored_runtime = authored;

    RoR::CalibratedBeamSavegame::ActorPayload payload;
    payload.beam_count = static_cast<std::uint32_t>(live.size());
    RoR::CalibratedBeamSavegame::BeamRecord record;
    record.beam_index = 1;
    record.node_1 = live[1].node_1;
    record.node_2 = live[1].node_2;
    record.beam_type = live[1].beam_type;
    record.special_beam = live[1].special_beam;
    record.runtime = saved;
    payload.records.push_back(record);

    rapidjson::Document document;
    document.SetObject();
    document.AddMember(
        "payload",
        RoR::CalibratedBeamSavegame::SerializeJson(
            payload,
            document.GetAllocator()),
        document.GetAllocator());
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    CHECK(document.Accept(writer));

    rapidjson::Document reparsed;
    reparsed.Parse(buffer.GetString(), buffer.GetSize());
    CHECK(!reparsed.HasParseError());
    RoR::CalibratedBeamSavegame::ActorPayload decoded;
    CHECK(RoR::CalibratedBeamSavegame::ParseJson(
        reparsed["payload"],
        decoded));
    std::vector<RoR::CalibratedBeamSavegame::StagedBeam> staged;
    CHECK(RoR::CalibratedBeamSavegame::TryStage(
        decoded,
        live,
        staged).IsValid());
    CHECK(staged.size() == 1U);
    CHECK(SameRuntime(staged[0].runtime, saved));

    // Prove continuation after the actual JSON text round trip rather than
    // merely comparing the decoded fields.
    Runtime restored = staged[0].runtime;
    const StepResult saved_next = Step(saved, Input(-0.0010));
    const StepResult restored_next = Step(restored, Input(-0.0010));
    CHECK(saved_next.IsValid());
    CHECK(restored_next.IsValid());
    CHECK(SameRuntime(saved, restored));
    CHECK(DoubleBits(saved_next.axial_force_n) ==
        DoubleBits(restored_next.axial_force_n));

    RoR::CalibratedBeamSavegame::ActorPayload untouched;
    untouched.schema_version = 77;
    rapidjson::Value& encoded = reparsed["payload"];
    encoded.AddMember(
        "unknown",
        true,
        reparsed.GetAllocator());
    CHECK(!RoR::CalibratedBeamSavegame::ParseJson(
        encoded,
        untouched));
    CHECK(untouched.schema_version == 77U);
    encoded.RemoveMember("unknown");

    encoded["records"][0]["state"]["last_total_strain"].SetDouble(
        DoubleFromBits(UINT64_C(0x7ff8000000000042)));
    CHECK(!RoR::CalibratedBeamSavegame::ParseJson(
        encoded,
        untouched));
    CHECK(untouched.schema_version == 77U);
}

} // namespace

int main()
{
    TestProductionJsonRoundTripAndStrictShape();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
