#include "JBeamWheel2Approximation.h"

#include <iostream>
#include <limits>
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

RoR::BeamNG::JBeamPackageSource Package(const std::string& source)
{
    const RoR::BeamNG::JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, "vehicles/wheel/main.jbeam");
    CHECK(parsed.IsValid());
    RoR::BeamNG::JBeamPackageSource package;
    package.package_path = "vehicles/wheel/main.jbeam";
    package.document = parsed.root;
    return package;
}

std::string Source(
    const std::string& defaults_extra = std::string(),
    const std::string& row_node_s = "9999",
    const std::string& body_extra = std::string(),
    const std::string& arm_x = "0",
    const std::string& arm_y = "0.25")
{
    return std::string(
        "{\"wheel_test\":{\"slotType\":\"main\",") +
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\",\"nodeWeight\"],"
        "[\"ref\",0,0,1,20],[\"back\",0,1,1,20],"
        "[\"left\",1,0,1,20],[\"up\",0,0,2,20],"
        "[\"leftCorner\",1,-1,1,20],"
        "[\"rightCorner\",-1,-1,1,20],"
        "[\"axle1\",0.125,0,1,5],"
        "[\"axle2\",-0.125,0,1,5],"
        "[\"arm\"," + arm_x + "," + arm_y + ",1,5]],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]],"
        "\"beams\":["
        "[\"id1:\",\"id2:\",\"beamSpring\",\"beamDamp\"],"
        "[\"ref\",\"back\",4300000,580],"
        "[\"ref\",\"left\",4300000,580],"
        "[\"ref\",\"up\",4300000,580],"
        "[\"ref\",\"leftCorner\",4300000,580],"
        "[\"ref\",\"rightCorner\",4300000,580],"
        "[\"ref\",\"axle1\",4300000,580],"
        "[\"ref\",\"axle2\",4300000,580],"
        "[\"ref\",\"arm\",4300000,580]],"
        "\"pressureWheels\":["
        "[\"name\",\"hubGroup\",\"group\",\"node1:\","
        "\"node2:\",\"nodeS\",\"nodeArm:\",\"wheelDir\"],"
        "{\"radius\":0.5,\"hubRadius\":0.25,"
        "\"wheelOffset\":0,\"tireWidth\":0.25,"
        "\"hubWidth\":0.25,\"hasTire\":true,\"numRays\":16,"
        "\"nodeWeight\":0.5,\"hubNodeWeight\":0.25,"
        "\"hubBeamSpring\":1000000,\"hubBeamDamp\":100,"
        "\"wheelSideBeamSpring\":500000,"
        "\"wheelSideBeamDamp\":50" + defaults_extra + "},"
        "[\"FL\",\"hub_FL\",\"tire_FL\",\"axle1\","
        "\"axle2\"," + row_node_s + ",\"arm\",-1]]" +
        body_extra + "}}";
}

RoR::BeamNG::JBeamResolvedGraph Resolve(const std::string& source)
{
    std::vector<RoR::BeamNG::JBeamPackageSource> packages;
    packages.push_back(Package(source));
    const RoR::BeamNG::JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(packages);
    CHECK(index.IsValid());
    const RoR::BeamNG::JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(index);
    CHECK(graph.IsValid());
    return graph;
}

using RoR::BeamNG::JBeamWheel2ApproximationCode;
using RoR::BeamNG::JBeamWheel2ApproximationLimits;
using RoR::BeamNG::JBeamWheel2ApproximationPlanSet;

JBeamWheel2ApproximationPlanSet Build(const std::string& source)
{
    return RoR::BeamNG::BuildJBeamWheel2ApproximationPlanSet(
        Resolve(source));
}

void TestExactBoundedPlan()
{
    const JBeamWheel2ApproximationPlanSet plans = Build(Source());
    CHECK(plans.IsAdmitted());
    CHECK(plans.plans.size() == 1U);
    CHECK(plans.generated_node_count == 64U);
    CHECK(plans.generated_beam_count == 384U);
    CHECK(plans.rejected_wheel_index ==
        (std::numeric_limits<std::size_t>::max)());
    CHECK(!RoR::BeamNG::
        SerializeCanonicalJBeamWheel2ApproximationPlanSet(plans).empty());
    if (!plans.plans.empty())
    {
        const RoR::BeamNG::JBeamWheel2ApproximationPlan& plan =
            plans.plans[0];
        CHECK(plan.name == "FL");
        CHECK(plan.node1 == "axle1");
        CHECK(plan.node2 == "axle2");
        CHECK(plan.node_arm == "arm");
        CHECK(plan.wheel_direction == -1);
        CHECK(plan.rim_radius == 0.25f);
        CHECK(plan.tyre_radius == 0.5f);
        CHECK(plan.width == 0.25f);
        CHECK(plan.num_rays == 16U);
        CHECK(plan.mass == 24.0f);
        CHECK(plan.rim_spring == 1000000.0f);
        CHECK(plan.rim_damping == 100.0f);
        CHECK(plan.tyre_spring == 500000.0f);
        CHECK(plan.tyre_damping == 50.0f);
        CHECK(plan.approximated_semantics ==
            RoR::BeamNG::JBEAM_WHEEL2_APPROXIMATION_SEMANTICS);
    }

    const JBeamWheel2ApproximationPlanSet repeated = Build(Source());
    CHECK(repeated.IsAdmitted());
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamWheel2ApproximationPlanSet(plans) ==
        RoR::BeamNG::
            SerializeCanonicalJBeamWheel2ApproximationPlanSet(repeated));
}

void TestEmptySetIsAdmitted()
{
    std::string source = Source();
    const std::size_t begin = source.find("\"pressureWheels\"");
    CHECK(begin != std::string::npos);
    if (begin == std::string::npos)
    {
        return;
    }
    CHECK(begin > 0U);
    if (begin == 0U)
    {
        return;
    }
    const std::size_t property_begin = begin - 1U;
    CHECK(source[property_begin] == ',');
    source.erase(property_begin, source.size() - 2U - property_begin);
    const JBeamWheel2ApproximationPlanSet plans = Build(source);
    CHECK(plans.IsAdmitted());
    CHECK(plans.plans.empty());
    CHECK(plans.generated_node_count == 0U);
    CHECK(plans.generated_beam_count == 0U);
}

void TestFailClosedProfiles()
{
    struct Hostile
    {
        std::string source;
        JBeamWheel2ApproximationCode code;
    };
    const Hostile hostiles[] = {
        {Source(",\"wheelOffset\":0.125"),
            JBeamWheel2ApproximationCode::UNSUPPORTED_GEOMETRY},
        {Source(",\"tireWidth\":0.5"),
            JBeamWheel2ApproximationCode::FLOAT_NARROWING},
        {Source(std::string(), "\"arm\""),
            JBeamWheel2ApproximationCode::UNSUPPORTED_GEOMETRY},
        {Source(",\"collision\":false"),
            JBeamWheel2ApproximationCode::UNSUPPORTED_COLLISION_MODE},
        {Source(",\"selfCollision\":true"),
            JBeamWheel2ApproximationCode::UNSUPPORTED_COLLISION_MODE},
        {Source(",\"propulsed\":1"),
            JBeamWheel2ApproximationCode::UNSUPPORTED_PROPULSION_OR_BRAKING},
        {Source(",\"brakeTorque\":1000"),
            JBeamWheel2ApproximationCode::UNSUPPORTED_PROPULSION_OR_BRAKING},
        {Source(",\"pressurePSI\":30"),
            JBeamWheel2ApproximationCode::INVALID_SOURCE_FIELD_SET},
        {Source(std::string(), "9999",
            ",\"controller\":[{\"fileName\":\"vehicleController\"}]"),
            JBeamWheel2ApproximationCode::UNSUPPORTED_SOURCE_SECTION},
        {Source(std::string(), "9999", std::string(), "0.5", "0"),
            JBeamWheel2ApproximationCode::INVALID_AXIS_GEOMETRY}
    };
    for (std::size_t i = 0U; i < sizeof(hostiles) / sizeof(hostiles[0]); ++i)
    {
        const JBeamWheel2ApproximationPlanSet plans = Build(hostiles[i].source);
        CHECK(!plans.IsAdmitted());
        if (plans.code != hostiles[i].code)
        {
            std::cerr << "hostile " << i << ": expected "
                      << RoR::BeamNG::JBeamWheel2ApproximationCodeToString(
                             hostiles[i].code)
                      << ", got "
                      << RoR::BeamNG::JBeamWheel2ApproximationCodeToString(
                             plans.code)
                      << " (" << plans.detail << ")\n";
        }
        CHECK(plans.code == hostiles[i].code);
        CHECK(plans.plans.empty());
        CHECK(plans.generated_node_count == 0U);
        CHECK(plans.generated_beam_count == 0U);
    }
}

void TestExplicitLimitsAndCanonicalBound()
{
    const RoR::BeamNG::JBeamResolvedGraph graph = Resolve(Source());
    JBeamWheel2ApproximationLimits wheel_limit;
    wheel_limit.max_wheels = 0U;
    JBeamWheel2ApproximationPlanSet plans =
        RoR::BeamNG::BuildJBeamWheel2ApproximationPlanSet(
            graph, wheel_limit);
    CHECK(!plans.IsAdmitted());
    CHECK(plans.code == JBeamWheel2ApproximationCode::WHEEL_LIMIT);

    JBeamWheel2ApproximationLimits node_limit;
    node_limit.max_generated_nodes = 63U;
    plans = RoR::BeamNG::BuildJBeamWheel2ApproximationPlanSet(
        graph, node_limit);
    CHECK(!plans.IsAdmitted());
    CHECK(plans.code ==
        JBeamWheel2ApproximationCode::GENERATED_TOPOLOGY_LIMIT);

    JBeamWheel2ApproximationLimits output_limit;
    output_limit.max_canonical_output_bytes = 8U;
    plans = RoR::BeamNG::BuildJBeamWheel2ApproximationPlanSet(
        graph, output_limit);
    CHECK(plans.IsAdmitted());
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamWheel2ApproximationPlanSet(plans).empty());
}

} // namespace

int main()
{
    TestExactBoundedPlan();
    TestEmptySetIsAdmitted();
    TestFailClosedProfiles();
    TestExplicitLimitsAndCanonicalBound();
    return g_failures == 0 ? 0 : 1;
}
