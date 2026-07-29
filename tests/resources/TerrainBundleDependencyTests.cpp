#include "TerrainBundleDependency.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
const char* const HASH_A =
    "0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef";
const char* const HASH_B =
    "abcdef0123456789abcdef0123456789"
    "abcdef0123456789abcdef0123456789";

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ":" << __LINE__                           \
                      << ": check failed: " #condition << "\n";                \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

bool HasCode(
    const RoR::TerrainBundleDependencyPlan& plan,
    RoR::TerrainBundleDependencyDiagnosticCode code)
{
    for (std::size_t index = 0U;
         index < plan.diagnostics.size();
         ++index)
    {
        if (plan.diagnostics[index].code == code)
        {
            return true;
        }
    }
    return false;
}

std::string Authenticated(
    const std::string& identity,
    const std::string& sha256 = HASH_A)
{
    return identity + ":" + sha256;
}

void TestValidExactDependencies()
{
    std::vector<std::string> authored;
    authored.push_back(Authenticated(
        "CityWorld.zip:CityWorld.terrn2"));
    authored.push_back(Authenticated(
        "Shared Roads 2.ZIP:Road Pack.TERRN2",
        HASH_B));
    const RoR::TerrainBundleDependencyPlan plan =
        RoR::BuildTerrainBundleDependencyPlan(authored);
    CHECK(plan.IsValid());
    CHECK(plan.dependencies.size() == 2U);
    if (plan.dependencies.size() == 2U)
    {
        CHECK(plan.dependencies[0].authored_name == authored[0]);
        CHECK(plan.dependencies[0].bundle_name == "CityWorld.zip");
        CHECK(plan.dependencies[0].terrain_filename ==
            "CityWorld.terrn2");
        CHECK(plan.dependencies[0].expected_archive_sha256 == HASH_A);
        CHECK(plan.dependencies[1].authored_name == authored[1]);
        CHECK(plan.dependencies[1].expected_archive_sha256 == HASH_B);
    }
}

void TestMalformedAndUnsafeNames()
{
    struct Case
    {
        std::string value;
        RoR::TerrainBundleDependencyDiagnosticCode code;
    };
    const Case cases[] = {
        {"", RoR::TerrainBundleDependencyDiagnosticCode::EMPTY_DEPENDENCY},
        {"CityWorld.terrn2",
         RoR::TerrainBundleDependencyDiagnosticCode::MALFORMED_QUALIFIER},
        {Authenticated(":CityWorld.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::MALFORMED_QUALIFIER},
        {"CityWorld.zip:CityWorld.terrn2",
         RoR::TerrainBundleDependencyDiagnosticCode::
             MISSING_ARCHIVE_SHA256},
        {"CityWorld.zip:CityWorld.terrn2:",
         RoR::TerrainBundleDependencyDiagnosticCode::
             MISSING_ARCHIVE_SHA256},
        {Authenticated("CityWorld.zip:"),
         RoR::TerrainBundleDependencyDiagnosticCode::MALFORMED_QUALIFIER},
        {Authenticated("A.zip:B.terrn2") + ":extra",
         RoR::TerrainBundleDependencyDiagnosticCode::MALFORMED_QUALIFIER},
        {"CityWorld.zip:CityWorld.terrn2:ABCDEF0123456789"
         "abcdef0123456789abcdef0123456789abcdef0123456789",
         RoR::TerrainBundleDependencyDiagnosticCode::
             INVALID_ARCHIVE_SHA256},
        {"CityWorld.zip:CityWorld.terrn2:0123",
         RoR::TerrainBundleDependencyDiagnosticCode::
             INVALID_ARCHIVE_SHA256},
        {"CityWorld.zip:CityWorld.terrn2:"
         "g123456789abcdef0123456789abcdef"
         "0123456789abcdef0123456789abcdef",
         RoR::TerrainBundleDependencyDiagnosticCode::
             INVALID_ARCHIVE_SHA256},
        {Authenticated("../CityWorld.zip:CityWorld.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated("CityWorld.zip:nested/CityWorld.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated("CityWorld.zip:CityWorld?.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated(" CityWorld.zip:CityWorld.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated("CityWorld.zip :CityWorld.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated("CityWorld.zip:CityWorld.terrn2 "),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated("Cit\xC3\xA9.zip:CityWorld.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated("CON.zip:CityWorld.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated("CityWorld.zip:lPt9.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::UNSAFE_PORTABLE_NAME},
        {Authenticated("CityWorld.7z:CityWorld.terrn2"),
         RoR::TerrainBundleDependencyDiagnosticCode::
             UNSUPPORTED_BUNDLE_TYPE},
        {Authenticated("CityWorld.zip:CityWorld.truck"),
         RoR::TerrainBundleDependencyDiagnosticCode::
             UNSUPPORTED_TERRAIN_TYPE}
    };
    for (std::size_t index = 0U;
         index < sizeof(cases) / sizeof(cases[0]);
         ++index)
    {
        const RoR::TerrainBundleDependencyPlan plan =
            RoR::BuildTerrainBundleDependencyPlan(
                std::vector<std::string>(1U, cases[index].value));
        CHECK(!plan.IsValid());
        CHECK(HasCode(plan, cases[index].code));
        CHECK(plan.dependencies.empty());
    }
}

void TestDuplicatesAndQuotas()
{
    std::vector<std::string> duplicate;
    duplicate.push_back(Authenticated(
        "CityWorld.zip:CityWorld.terrn2"));
    duplicate.push_back(Authenticated(
        "cityworld.ZIP:Alternate.terrn2",
        HASH_B));
    RoR::TerrainBundleDependencyPlan plan =
        RoR::BuildTerrainBundleDependencyPlan(duplicate);
    CHECK(!plan.IsValid());
    CHECK(HasCode(
        plan,
        RoR::TerrainBundleDependencyDiagnosticCode::DUPLICATE_BUNDLE));

    std::vector<std::string> too_many;
    for (std::size_t index = 0U; index < 9U; ++index)
    {
        too_many.push_back(
            Authenticated(
                "Bundle" + std::to_string(index) +
                ".zip:Terrain" + std::to_string(index) + ".terrn2"));
    }
    plan = RoR::BuildTerrainBundleDependencyPlan(too_many);
    CHECK(!plan.IsValid());
    CHECK(HasCode(
        plan,
        RoR::TerrainBundleDependencyDiagnosticCode::
            DEPENDENCY_COUNT_LIMIT));
    CHECK(plan.dependencies.empty());

    std::vector<std::string> at_limit;
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        at_limit.push_back(
            Authenticated(
                "Bundle" + std::to_string(index) +
                ".zip:Terrain" + std::to_string(index) + ".terrn2"));
    }
    plan = RoR::BuildTerrainBundleDependencyPlan(at_limit);
    CHECK(plan.IsValid());
    CHECK(plan.dependencies.size() == 8U);

    std::string oversized(500U, 'a');
    oversized = Authenticated(oversized + ".zip:B.terrn2");
    plan = RoR::BuildTerrainBundleDependencyPlan(
        std::vector<std::string>(1U, oversized));
    CHECK(!plan.IsValid());
    CHECK(HasCode(
        plan,
        RoR::TerrainBundleDependencyDiagnosticCode::
            DEPENDENCY_BYTES_LIMIT));

    std::vector<std::string> excessive_total;
    for (std::size_t index = 0U; index < 7U; ++index)
    {
        excessive_total.push_back(
            Authenticated(
                std::string(
                    230U,
                    static_cast<char>('a' + index)) +
                std::to_string(index) +
                ".zip:T.terrn2"));
    }
    plan = RoR::BuildTerrainBundleDependencyPlan(excessive_total);
    CHECK(!plan.IsValid());
    CHECK(HasCode(
        plan,
        RoR::TerrainBundleDependencyDiagnosticCode::
            DEPENDENCY_BYTES_LIMIT));
}

void TestStableDiagnosticNames()
{
    CHECK(std::string(
        RoR::TerrainBundleDependencyDiagnosticCodeToString(
            RoR::TerrainBundleDependencyDiagnosticCode::
                MALFORMED_QUALIFIER)) == "malformed-qualifier");
    CHECK(std::string(
        RoR::TerrainBundleDependencyDiagnosticCodeToString(
            RoR::TerrainBundleDependencyDiagnosticCode::
                MISSING_ARCHIVE_SHA256)) == "missing-archive-sha256");
    CHECK(std::string(
        RoR::TerrainBundleDependencyDiagnosticCodeToString(
            RoR::TerrainBundleDependencyDiagnosticCode::
                INVALID_ARCHIVE_SHA256)) == "invalid-archive-sha256");
    CHECK(std::string(
        RoR::TerrainBundleDependencyDiagnosticCodeToString(
            RoR::TerrainBundleDependencyDiagnosticCode::
                DUPLICATE_BUNDLE)) == "duplicate-bundle");
}

} // namespace

int main()
{
    TestValidExactDependencies();
    TestMalformedAndUnsafeNames();
    TestDuplicatesAndQuotas();
    TestStableDiagnosticNames();
    if (failures != 0)
    {
        std::cerr << failures << " terrain dependency checks failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
