#include "JBeamHydroSavegameJson.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdint>
#include <iostream>
#include <string>
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

RoR::JBeamHydroRuntimeConfig Config()
{
    RoR::JBeamHydroRuntimeConfig config;
    config.response.has_factor = true;
    config.response.factor = 0.5;
    config.response.in_rate = 1.0;
    config.response.out_rate = 2.0;
    config.response.auto_center_rate = 0.5;
    config.has_steering_wheel_lock = true;
    config.steering_wheel_lock = 500.0;
    return config;
}

RoR::JBeamHydroSavegame::ActorPayload Payload()
{
    const RoR::JBeamHydroRuntimeConfig config = Config();
    const RoR::JBeamHydroRuntimeStep initialized =
        RoR::InitializeJBeamHydroRuntime(config, 2.0);
    const RoR::JBeamHydroRuntimeStep advanced =
        RoR::AdvanceJBeamHydroRuntime(
            config, initialized.state, 2.0, 1.0, 0.25, false);
    RoR::JBeamHydroSavegame::HydroRecord record;
    record.hydro_index = 0U;
    record.beam_index = 4U;
    record.reference_length = 2.0f;
    record.config = config;
    record.state = advanced.state;
    RoR::JBeamHydroSavegame::ActorPayload payload;
    payload.hydro_count = 1U;
    payload.records.push_back(record);
    return payload;
}

std::string Serialize(const RoR::JBeamHydroSavegame::ActorPayload& payload)
{
    rapidjson::Document document;
    rapidjson::Value root = RoR::JBeamHydroSavegame::SerializeJson(
        payload, document.GetAllocator());
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    CHECK(root.Accept(writer));
    return std::string(buffer.GetString(), buffer.GetSize());
}

bool Parse(
    const std::string& text,
    RoR::JBeamHydroSavegame::ActorPayload& output)
{
    rapidjson::Document document;
    document.Parse(text.data(), text.size());
    return !document.HasParseError() &&
        RoR::JBeamHydroSavegame::ParseJson(document, output);
}

void TestExactRoundTripAndStaging()
{
    const RoR::JBeamHydroSavegame::ActorPayload original = Payload();
    const std::string serialized = Serialize(original);
    RoR::JBeamHydroSavegame::ActorPayload parsed;
    CHECK(Parse(serialized, parsed));
    CHECK(parsed.schema_version == 1U);
    CHECK(parsed.hydro_count == 1U);
    CHECK(parsed.records.size() == 1U);
    CHECK(parsed.records[0].hydro_index == 0U);
    CHECK(parsed.records[0].beam_index == 4U);
    CHECK(parsed.records[0].reference_length == 2.0f);
    CHECK(RoR::JBeamHydroSavegame::SameConfiguration(
        parsed.records[0].config, original.records[0].config));
    CHECK(parsed.records[0].state.accepted_step_count == 1U);
    CHECK(parsed.records[0].state.response.length_ratio == 1.5);

    RoR::JBeamHydroSavegame::LiveHydro live;
    live.hydro_index = 0U;
    live.beam_index = 4U;
    live.enabled = true;
    live.reference_length = 2.0f;
    live.saved_runtime_rest_length = 3.0f;
    live.config = Config();
    std::vector<RoR::JBeamHydroSavegame::LiveHydro> live_hydros(1U, live);
    std::vector<RoR::JBeamHydroSavegame::StagedHydro> staged;
    CHECK(RoR::JBeamHydroSavegame::TryStage(
        parsed, live_hydros, staged).IsValid());
    CHECK(staged.size() == 1U);
    CHECK(staged[0].runtime_rest_length == 3.0f);
}

void TestParseFailureIsAtomic()
{
    RoR::JBeamHydroSavegame::ActorPayload output;
    output.hydro_count = 77U;
    output.records.resize(1U);
    output.records[0].hydro_index = 99U;
    CHECK(!Parse("{\"schema_version\":1}", output));
    CHECK(output.hydro_count == 77U);
    CHECK(output.records.size() == 1U);
    CHECK(output.records[0].hydro_index == 99U);
}

void TestRejectsEveryTruncation()
{
    const std::string serialized = Serialize(Payload());
    for (std::size_t size = 0U; size < serialized.size(); ++size)
    {
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!Parse(serialized.substr(0U, size), output));
    }
}

void TestStrictShapeAndTypes()
{
    const std::string serialized = Serialize(Payload());
    rapidjson::Document document;
    document.Parse(serialized.data(), serialized.size());
    CHECK(!document.HasParseError());

    {
        rapidjson::Document changed;
        changed.CopyFrom(document, changed.GetAllocator());
        changed.AddMember("extra", true, changed.GetAllocator());
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!RoR::JBeamHydroSavegame::ParseJson(changed, output));
    }
    {
        rapidjson::Document changed;
        changed.CopyFrom(document, changed.GetAllocator());
        changed["hydro_count"].SetUint(65536U);
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!RoR::JBeamHydroSavegame::ParseJson(changed, output));
    }
    {
        rapidjson::Document changed;
        changed.CopyFrom(document, changed.GetAllocator());
        changed["records"][0]["beam_index"].SetUint(65536U);
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!RoR::JBeamHydroSavegame::ParseJson(changed, output));
    }
    {
        rapidjson::Document changed;
        changed.CopyFrom(document, changed.GetAllocator());
        changed["records"][0]["reference_length"].SetDouble(0.0);
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!RoR::JBeamHydroSavegame::ParseJson(changed, output));
    }
    {
        rapidjson::Document changed;
        changed.CopyFrom(document, changed.GetAllocator());
        changed["records"][0]["configuration"]["input_route"].SetString(
            "throttle-input", changed.GetAllocator());
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!RoR::JBeamHydroSavegame::ParseJson(changed, output));
    }
    {
        rapidjson::Document changed;
        changed.CopyFrom(document, changed.GetAllocator());
        static const char embedded_nul_route[] =
            "steering-input\0unexpected";
        changed["records"][0]["configuration"]["input_route"].SetString(
            embedded_nul_route,
            static_cast<rapidjson::SizeType>(
                sizeof(embedded_nul_route) - 1U),
            changed.GetAllocator());
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!RoR::JBeamHydroSavegame::ParseJson(changed, output));
    }
    {
        rapidjson::Document changed;
        changed.CopyFrom(document, changed.GetAllocator());
        changed["records"][0]["state"]["fault"].SetInt(999);
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!RoR::JBeamHydroSavegame::ParseJson(changed, output));
    }
    {
        rapidjson::Document changed;
        changed.CopyFrom(document, changed.GetAllocator());
        changed["records"][0]["configuration"]["response"].AddMember(
            "extra", true, changed.GetAllocator());
        RoR::JBeamHydroSavegame::ActorPayload output;
        CHECK(!RoR::JBeamHydroSavegame::ParseJson(changed, output));
    }
}

} // namespace

int main()
{
    TestExactRoundTripAndStaging();
    TestParseFailureIsAtomic();
    TestRejectsEveryTruncation();
    TestStrictShapeAndTypes();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "JBeam hydro savegame JSON tests passed\n";
    return 0;
}
