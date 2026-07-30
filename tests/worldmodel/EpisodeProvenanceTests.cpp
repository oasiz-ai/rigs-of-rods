#include "EpisodeProvenance.h"
#include "TestProvenance.h"
#include "WorldModelCaptureContract.h"

#include <cstdint>
#include <iostream>
#include <string>

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

using namespace RoR::WorldModel;

void TestCanonicalRoundTrip()
{
    const EpisodeId episode(1U, 2U);
    const EpisodeProvenance expected =
        RoRWorldModelTest::MakeProvenance(episode);
    std::string encoded;
    std::string error;
    CHECK(ValidateEpisodeProvenance(expected, &error));
    CHECK(SerializeEpisodeProvenance(expected, encoded, &error));
    CHECK(!encoded.empty());
    CHECK(encoded.back() == '\n');

    EpisodeProvenance parsed;
    CHECK(ParseEpisodeProvenance(encoded, parsed, &error));
    std::string round_trip;
    CHECK(SerializeEpisodeProvenance(parsed, round_trip, &error));
    CHECK(round_trip == encoded);
    CHECK(parsed.reset_seed == DeriveSeed(
        parsed.root_seed,
        SeedDomain::RESET,
        episode,
        0U));
}

void TestRequiredSemantics()
{
    const EpisodeId episode(1U, 2U);
    std::string error;

    EpisodeProvenance invalid =
        RoRWorldModelTest::MakeProvenance(episode);
    invalid.matrix_order = "column-major";
    CHECK(!ValidateEpisodeProvenance(invalid, &error));
    CHECK(error == "matrix_order must be row-major");

    invalid = RoRWorldModelTest::MakeProvenance(episode);
    invalid.coordinate_frame = "unknown";
    CHECK(!ValidateEpisodeProvenance(invalid, &error));
    CHECK(error == "coordinate_frame must be ror.world.rh-y-up");

    invalid = RoRWorldModelTest::MakeProvenance(episode);
    invalid.rights_manifest_sha256 = std::string(64U, '0');
    CHECK(!ValidateEpisodeProvenance(invalid, &error));

    invalid = RoRWorldModelTest::MakeProvenance(episode);
    invalid.control_ids.clear();
    CHECK(!ValidateEpisodeProvenance(invalid, &error));
    CHECK(error == "control_ids must not be empty");

    invalid = RoRWorldModelTest::MakeProvenance(episode);
    invalid.control_ids = {
        "vehicle.throttle",
        "vehicle.steering"};
    CHECK(!ValidateEpisodeProvenance(invalid, &error));
    CHECK(error == "control_ids must be sorted and unique");

    invalid = RoRWorldModelTest::MakeProvenance(episode);
    invalid.control_ids = {"vehicle.horn"};
    CHECK(!ValidateEpisodeProvenance(invalid, &error));
    CHECK(error ==
        "control_ids contains an unsupported schema-1 control");
}

void TestRejectsNoncanonicalEncoding()
{
    const EpisodeId episode(1U, 2U);
    const EpisodeProvenance value =
        RoRWorldModelTest::MakeProvenance(episode);
    std::string encoded;
    std::string error;
    CHECK(SerializeEpisodeProvenance(value, encoded, &error));

    EpisodeProvenance output;
    const std::string missing_newline =
        encoded.substr(0U, encoded.size() - 1U);
    CHECK(!ParseEpisodeProvenance(
        missing_newline,
        output,
        &error));

    std::string wrong_rate = encoded;
    const std::string needle =
        "  \"observation_rate_hz\": 48";
    const std::size_t rate = wrong_rate.find(needle);
    CHECK(rate != std::string::npos);
    if (rate != std::string::npos)
        wrong_rate.replace(rate, needle.size(),
            "  \"observation_rate_hz\": 60");
    CHECK(!ParseEpisodeProvenance(wrong_rate, output, &error));
}

} // namespace

int main()
{
    TestCanonicalRoundTrip();
    TestRequiredSemantics();
    TestRejectsNoncanonicalEncoding();
    if (g_failures != 0)
    {
        std::cerr << g_failures << " provenance test(s) failed\n";
        return 1;
    }
    std::cout << "episode provenance tests passed\n";
    return 0;
}
