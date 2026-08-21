#include "JBeamStructuralIR.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
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

using RoR::BeamNG::JBeamPackageIndex;
using RoR::BeamNG::JBeamPackageSource;
using RoR::BeamNG::JBeamObjectField;
using RoR::BeamNG::JBeamParseResult;
using RoR::BeamNG::JBeamConfigurationResult;
using RoR::BeamNG::JBeamResolvedGraph;
using RoR::BeamNG::JBeamStructuralBeamStatus;
using RoR::BeamNG::JBeamStructuralDiagnostic;
using RoR::BeamNG::JBeamStructuralDiagnosticCode;
using RoR::BeamNG::JBeamStructuralIR;
using RoR::BeamNG::JBeamStructuralLimits;
using RoR::BeamNG::JBeamStructuralTriangleOrigin;
using RoR::BeamNG::JBeamValue;
using RoR::BeamNG::JBeamValueType;

JBeamPackageSource Package(
    const std::string& path,
    const std::string& source)
{
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, path);
    CHECK(parsed.IsValid());
    JBeamPackageSource package;
    package.package_path = path;
    package.document = parsed.root;
    return package;
}

JBeamResolvedGraph Resolve(
    const std::vector<JBeamPackageSource>& packages)
{
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(packages);
    CHECK(index.IsValid());
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(index);
    CHECK(graph.IsValid());
    return graph;
}

JBeamResolvedGraph ResolveConfigured(
    const std::vector<JBeamPackageSource>& packages,
    const std::string& configuration_source)
{
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(
            configuration_source,
            "vehicles/clean/config.pc");
    CHECK(parsed.IsValid());
    const JBeamConfigurationResult configuration =
        RoR::BeamNG::ParseJBeamConfiguration(parsed.root);
    CHECK(configuration.IsValid());
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(packages);
    CHECK(index.IsValid());
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, configuration.request);
    CHECK(graph.IsValid());
    return graph;
}

JBeamResolvedGraph ResolveSingle(const std::string& body)
{
    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/clean/main.jbeam",
        std::string("{\"car\":{\"slotType\":\"main\",") +
            body + "}}"));
    return Resolve(packages);
}

const JBeamValue* FieldValue(
    const JBeamValue& object,
    const std::string& key)
{
    const JBeamObjectField* field =
        RoR::BeamNG::FindLastJBeamObjectField(object, key);
    return field != NULL && field->value
        ? field->value.get()
        : NULL;
}

JBeamObjectField* MutableLastField(
    JBeamValue& object,
    const std::string& key)
{
    for (std::size_t i = object.object_fields.size();
         i > 0U;
         --i)
    {
        if (object.object_fields[i - 1U].key == key)
        {
            return &object.object_fields[i - 1U];
        }
    }
    return NULL;
}

std::string DirectoryName(const std::string& path)
{
    const std::string::size_type separator =
        path.find_last_of("/\\");
    return separator == std::string::npos
        ? std::string(".")
        : path.substr(0U, separator);
}

bool ReadTextFile(
    const std::string& path,
    std::string& text)
{
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input)
    {
        return false;
    }
    std::ostringstream output;
    output << input.rdbuf();
    if (!input.good() && !input.eof())
    {
        return false;
    }
    text = output.str();
    return true;
}

std::string FixturePath(const std::string& relative_path)
{
    const std::string fixture_suffix =
        "fixtures/beamng/cleanroom_structural/" + relative_path;
    const std::string source_relative =
        DirectoryName(__FILE__) + "/../" + fixture_suffix;
    std::string ignored;
    if (ReadTextFile(source_relative, ignored))
    {
        return source_relative;
    }

    std::string prefix;
    for (std::size_t depth = 0U; depth < 8U; ++depth)
    {
        const std::string candidate =
            prefix + "tests/" + fixture_suffix;
        if (ReadTextFile(candidate, ignored))
        {
            return candidate;
        }
        prefix += "../";
    }
    return std::string();
}

std::size_t CountDiagnostic(
    const JBeamStructuralIR& ir,
    JBeamStructuralDiagnosticCode code)
{
    std::size_t count = 0U;
    for (std::size_t i = 0U; i < ir.diagnostics.size(); ++i)
    {
        if (ir.diagnostics[i].code == code)
        {
            ++count;
        }
    }
    return count;
}

const JBeamStructuralDiagnostic* FindDiagnostic(
    const JBeamStructuralIR& ir,
    JBeamStructuralDiagnosticCode code)
{
    for (std::size_t i = 0U; i < ir.diagnostics.size(); ++i)
    {
        if (ir.diagnostics[i].code == code)
        {
            return &ir.diagnostics[i];
        }
    }
    return NULL;
}

double PositiveInfinityBits()
{
    const std::uint64_t bits = UINT64_C(0x7ff0000000000000);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string FrameAndNodes()
{
    return
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"ref\",0,0,0],"
        "[\"back\",0,1,0],"
        "[\"left\",1,0,0],"
        "[\"up\",0,0,1],"
        "[\"leftCorner\",1,-1,0],"
        "[\"rightCorner\",-1,-1,0]"
        "],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]"
        "]";
}

void TestNormalizedCoreAndPreservedFields()
{
    const JBeamResolvedGraph graph = ResolveSingle(
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "{\"nodeWeight\":5,\"collision\":true},"
        "[\"ref\",0,0,0],"
        "[\"back\",0,1,0],"
        "[\"left\",1,0,0,{\"nodeWeight\":7,\"mystery\":9}],"
        "[\"up\",0,0,1],"
        "[\"leftCorner\",1,-1,0],"
        "[\"rightCorner\",-1,-1,0],"
        "[\"q\",1,1,0]"
        "],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]"
        "],"
        "\"beams\":["
        "[\"id1:\",\"id2:\",\"beamSpring\",\"beamDamp\"],"
        "{\"beamDeform\":400,\"beamPrecompression\":0.9},"
        "[\"ref\",\"back\",100,2,{\"beamStrength\":300}]"
        "],"
        "\"triangles\":["
        "[\"id1:\",\"id2:\",\"id3:\"],"
        "[\"ref\",\"back\",\"left\"]"
        "],"
        "\"quads\":["
        "[\"id1:\",\"id2:\",\"id3:\",\"id4:\"],"
        "[\"ref\",\"back\",\"q\",\"left\"]"
        "]");
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(ir.IsValid());
    CHECK(ir.parts.size() == 1U);
    CHECK(ir.nodes.size() == 7U);
    CHECK(ir.nodes[0].id == "ref");
    CHECK(ir.nodes[0].node_weight == 5.0);
    CHECK(ir.nodes[0].node_weight_authored);
    CHECK(ir.nodes[2].node_weight == 7.0);
    CHECK(ir.nodes[2].node_weight_authored);
    CHECK(ir.nodes[6].x == 1.0);
    CHECK(ir.nodes[6].y == 1.0);
    CHECK(ir.has_ref_frame);
    CHECK(ir.ref_frame.reference == "ref");
    CHECK(ir.ref_frame.back_index == 1U);
    CHECK(ir.ref_frame.left_corner == "leftCorner");
    CHECK(ir.ref_frame.right_corner_index == 5U);
    CHECK(ir.beams.size() == 1U);
    CHECK(ir.beams[0].status ==
        JBeamStructuralBeamStatus::ENABLED);
    CHECK(ir.beams[0].has_spring);
    CHECK(ir.beams[0].spring == 100.0);
    CHECK(ir.beams[0].has_damping);
    CHECK(ir.beams[0].damping == 2.0);
    CHECK(ir.beams[0].has_deform);
    CHECK(ir.beams[0].deform == 400.0);
    CHECK(ir.beams[0].has_strength);
    CHECK(ir.beams[0].strength == 300.0);
    CHECK(ir.beams[0].has_precompression);
    CHECK(ir.beams[0].precompression == 0.9);
    CHECK(ir.triangles.size() == 3U);
    CHECK(ir.triangles[0].origin ==
        JBeamStructuralTriangleOrigin::TRIANGLE);
    CHECK(ir.triangles[1].origin ==
        JBeamStructuralTriangleOrigin::QUAD_FIRST);
    CHECK(ir.triangles[1].node_a == "ref");
    CHECK(ir.triangles[1].node_b == "back");
    CHECK(ir.triangles[1].node_c == "q");
    CHECK(ir.triangles[2].origin ==
        JBeamStructuralTriangleOrigin::QUAD_SECOND);
    CHECK(ir.triangles[2].node_a == "ref");
    CHECK(ir.triangles[2].node_b == "q");
    CHECK(ir.triangles[2].node_c == "left");
    CHECK(CountDiagnostic(
        ir, JBeamStructuralDiagnosticCode::UNSUPPORTED_FIELD) == 1U);
    CHECK(CountDiagnostic(
        ir, JBeamStructuralDiagnosticCode::UNKNOWN_FIELD) == 1U);
    const JBeamStructuralDiagnostic* unknown = FindDiagnostic(
        ir, JBeamStructuralDiagnosticCode::UNKNOWN_FIELD);
    CHECK(unknown != NULL);
    CHECK(unknown->has_preserved_value);
    CHECK(static_cast<bool>(unknown->preserved_value));
    CHECK(unknown->preserved_value->number_value == 9.0);
    CHECK(unknown->provenance.PartName() == "car");
    CHECK(unknown->provenance.SourceName() ==
        "vehicles/clean/main.jbeam");
}

void TestResolvedPartPreorderAndCanonicalSourcePermutation()
{
    const JBeamPackageSource main = Package(
        "vehicles/order/main.jbeam",
        "{"
        "\"car\":{"
        "\"slotType\":\"main\","
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"engine\",\"engine_part\",\"Engine\"]"
        "],"
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"ref\",0,0,0],"
        "[\"back\",0,1,0],"
        "[\"left\",1,0,0],"
        "[\"up\",0,0,1],"
        "[\"leftCorner\",1,-1,0],"
        "[\"rightCorner\",-1,-1,0]"
        "],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]"
        "]"
        "}"
        "}");
    const JBeamPackageSource child = Package(
        "vehicles/order/engine.jbeam",
        "{"
        "\"engine_part\":{"
        "\"slotType\":\"engine\","
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"engine\",0.5,-0.5,0.5]"
        "],"
        "\"beams\":["
        "[\"id1:\",\"id2:\"],"
        "[\"ref\",\"engine\"]"
        "]"
        "}"
        "}");

    std::vector<JBeamPackageSource> forward;
    forward.push_back(main);
    forward.push_back(child);
    std::vector<JBeamPackageSource> reverse;
    reverse.push_back(child);
    reverse.push_back(main);
    const JBeamStructuralIR a =
        RoR::BeamNG::BuildJBeamStructuralIR(Resolve(forward));
    const JBeamStructuralIR b =
        RoR::BeamNG::BuildJBeamStructuralIR(Resolve(reverse));
    CHECK(a.IsValid());
    CHECK(b.IsValid());
    CHECK(a.parts.size() == 2U);
    CHECK(a.parts[0].provenance.PartName() == "car");
    CHECK(a.parts[1].provenance.PartName() == "engine_part");
    CHECK(a.nodes.size() == 7U);
    CHECK(a.nodes[6].provenance.PartPreorderIndex() == 1U);
    CHECK(a.beams[0].provenance.PartName() == "engine_part");
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(a) ==
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(b));
}

void TestDuplicateAndMalformedNodes()
{
    const JBeamStructuralIR duplicate =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\",\"nodeWeight\"],"
            "[\"ref\",0,0,0,1],"
            "[\"ref\",0,-1,0,1],"
            "[\"left\",1,0,0,1],"
            "[\"up\",0,0,1,1],"
            "[\"leftCorner\",1,-1,0,1],"
            "[\"rightCorner\",-1,-1,0,1]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"ref\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!duplicate.IsValid());
    CHECK(CountDiagnostic(
        duplicate,
        JBeamStructuralDiagnosticCode::DUPLICATE_NODE_ID) == 1U);

    const JBeamStructuralIR invalid =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\",\"nodeWeight\"],"
            "[\"ref\",\"$=1\",0,0,-2],"
            "[\"back\",0,1,0,1],"
            "[\"left\",1,0,0,1],"
            "[\"up\",0,0,1,1],"
            "[\"leftCorner\",1,-1,0,1],"
            "[\"rightCorner\",-1,-1,0,1]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!invalid.IsValid());
    CHECK(CountDiagnostic(
        invalid,
        JBeamStructuralDiagnosticCode::EXPRESSION_DISABLED) == 0U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamStructuralDiagnosticCode::INVALID_NODE_WEIGHT) == 1U);
}

void TestResolvedExpressionsVariablesAndComponents()
{
    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/formulacoupe-representative/main.jbeam",
        "{"
        "\"car\":{"
        "\"slotType\":\"main\","
        "\"components\":{"
        "\"geometry\":{\"reference\":\"ref\",\"x\":0.25},"
        "\"unsupportedTable\":[1,2,3]"
        "},"
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\",\"nodeWeight\"],"
        "[\"$=$components.geometry.reference\","
        "\"$=case(1,clamp(max(abs(-($components.geometry.x+$offset)),"
        "square(0)),0,min(1,2)),0)\","
        "\"$=$missing == nil and 0 or $missing\",0,"
        "\"$lanceMass\"],"
        "[\"back\",\"$=round(pow(fmod(5.25,4),1))\","
        "\"$=ceil(modf(1.25))\",0,1],"
        "[\"left\",\"$=floor(deg(rad(2.9)))\","
        "\"$=smoothstep(-1)\",0,1],"
        "[\"up\",1,0,\"$=smootherstep(ldexp(0.5,1))\",1],"
        "[\"leftCorner\",2,-1,"
        "\"$=smootheststep(modf(0.5))\",1],"
        "[\"rightCorner\",0,-1,0,1]"
        "],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"$=$components.geometry.reference\",\"back\","
        "\"left\",\"up\",\"leftCorner\",\"rightCorner\"]"
        "],"
        "\"beams\":["
        "[\"id1:\",\"id2:\",\"beamPrecompression\",\"optional\"],"
        "[\"ref\",\"back\",\"$=2-$caster_F\",\"$=$optional\"]"
        "]"
        "}"
        "}"));
    const JBeamResolvedGraph graph = ResolveConfigured(
        packages,
        "{"
        "\"parts\":{},"
        "\"vars\":{"
        "\"$offset\":0.75,"
        "\"$baseMass\":7.5,"
        "\"$lanceMass\":\"$=$baseMass*2\","
        "\"$caster_F\":1.01695,"
        "\"$optional\":false"
        "}"
        "}");
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(ir.IsValid());
    CHECK(ir.nodes.size() == 6U);
    CHECK(ir.nodes[0].id == "ref");
    CHECK(ir.nodes[0].x == 1.0);
    CHECK(ir.nodes[0].y == 0.0);
    CHECK(ir.nodes[0].node_weight == 15.0);
    CHECK(ir.nodes[0].node_weight_authored);
    CHECK(ir.beams.size() == 1U);
    CHECK(ir.beams[0].has_precompression);
    CHECK(ir.beams[0].precompression == 2.0 - 1.01695);
    CHECK(!ir.beams[0].optional);
    CHECK(ir.ref_frame.reference == "ref");
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::
            UNSUPPORTED_COMPONENT_VALUE) == 0U);
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 0U);
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::EXPRESSION_DISABLED) == 0U);

    const JBeamStructuralIR repeated =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(repeated.IsValid());
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(ir) ==
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(repeated));
}

void TestExactTableComponentRowExpansion()
{
    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/components/main.jbeam",
        "{"
        "\"car\":{"
        "\"slotType\":\"main\","
        "\"components\":{"
        "\"mass\":1,"
        "\"rows\":{"
        "\"reference\":[\"ref\",0,0,0,"
        "\"$=$components.mass\"],"
        "\"beam\":[\"ref\",\"back\",125000,50]"
        "}"
        "},"
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"body\",\"component_body\",\"Body\"]"
        "],"
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\","
        "\"nodeWeight\"],"
        "\"$= $components.rows.reference\","
        "[\"back\",0,1,0,1],"
        "[\"left\",1,0,0,1],"
        "[\"up\",0,0,1,1],"
        "[\"leftCorner\",1,-1,0,1],"
        "[\"rightCorner\",-1,-1,0,1]"
        "],"
        "\"beams\":["
        "[\"id1:\",\"id2:\",\"beamSpring\",\"beamDamp\"],"
        "\"$=$components.rows.beam\""
        "],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]"
        "]"
        "}"
        "}"));
    packages.push_back(Package(
        "vehicles/components/body.jbeam",
        "{"
        "\"component_body\":{"
        "\"slotType\":\"body\","
        "\"components\":{"
        "\"mass\":2,"
        "\"rows\":{"
        "\"reference\":[\"ref\",0,0,0,"
        "\"$=$components.mass\"]"
        "}"
        "}"
        "}"
        "}"));

    const JBeamResolvedGraph graph = ResolveConfigured(
        packages,
        "{\"parts\":{},\"vars\":{}}");
    CHECK(graph.IsValid());
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(ir.IsValid());
    CHECK(ir.nodes.size() == 6U);
    if (ir.nodes.size() == 6U)
    {
        CHECK(ir.nodes[0].id == "ref");
        CHECK(ir.nodes[0].z == 0.0);
        CHECK(ir.nodes[0].node_weight == 2.0);
    }
    CHECK(ir.beams.size() == 1U);
    if (ir.beams.size() == 1U)
    {
        CHECK(ir.beams[0].node_a == "ref");
        CHECK(ir.beams[0].node_b == "back");
        CHECK(ir.beams[0].spring == 125000.0);
        CHECK(ir.beams[0].damping == 50.0);
    }
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::
            UNSUPPORTED_COMPONENT_VALUE) == 0U);
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::NORMALIZATION_ERROR) == 0U);

    std::reverse(packages.begin(), packages.end());
    const JBeamStructuralIR permuted =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveConfigured(
            packages,
            "{\"parts\":{},\"vars\":{}}"));
    CHECK(permuted.IsValid());
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(ir) ==
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(permuted));

    JBeamStructuralLimits expansion_limit;
    expansion_limit.max_preserved_value_work_units = 1U;
    const JBeamStructuralIR limited =
        RoR::BeamNG::BuildJBeamStructuralIR(graph, expansion_limit);
    CHECK(!limited.IsValid());
    CHECK(limited.nodes.empty());
    CHECK(limited.beams.empty());
    CHECK(CountDiagnostic(
        limited,
        JBeamStructuralDiagnosticCode::PRESERVED_VALUE_LIMIT) == 1U);

    const JBeamStructuralIR non_exact =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"components\":{"
            "\"row\":[\"ref\",0,0,0]"
            "},"
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "\"$=$components.row or nil\","
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!non_exact.IsValid());
    CHECK(CountDiagnostic(
        non_exact,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 1U);
    CHECK(CountDiagnostic(
        non_exact,
        JBeamStructuralDiagnosticCode::NORMALIZATION_WARNING) == 0U);

    const JBeamStructuralIR wrong_type =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"components\":{\"row\":1},"
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "\"$=$components.row\","
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!wrong_type.IsValid());
    CHECK(CountDiagnostic(
        wrong_type,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 1U);
    const JBeamStructuralDiagnostic* wrong_type_error = FindDiagnostic(
        wrong_type,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR);
    CHECK(wrong_type_error != NULL);
    if (wrong_type_error != NULL)
    {
        CHECK(wrong_type_error->detail.find("array-valued") !=
            std::string::npos);
    }
}

void TestExactWholeTableComponentExpansion()
{
    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/components/table-main.jbeam",
        "{"
        "\"table_car\":{"
        "\"slotType\":\"main\","
        "\"components\":{"
        "\"mass\":1,"
        "\"tables\":{"
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\","
        "\"nodeWeight\"],"
        "[\"ref\",0,0,0,\"$=$components.mass\"],"
        "[\"back\",0,1,0,1],"
        "[\"left\",1,0,0,1],"
        "[\"up\",0,0,1,1],"
        "[\"leftCorner\",1,-1,0,1],"
        "[\"rightCorner\",-1,-1,0,1]"
        "],"
        "\"beams\":["
        "[\"id1:\",\"id2:\",\"beamSpring\",\"beamDamp\"],"
        "[\"ref\",\"back\",200000,20]"
        "],"
        "\"frame\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]"
        "]"
        "}"
        "},"
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"body\",\"table_body\",\"Body\"]"
        "],"
        "\"nodes\":\"$= $components.tables.nodes\","
        "\"beams\":\"$=$components.tables.beams\","
        "\"refNodes\":\"$=$components.tables.frame\""
        "}"
        "}"));
    packages.push_back(Package(
        "vehicles/components/table-body.jbeam",
        "{"
        "\"table_body\":{"
        "\"slotType\":\"body\","
        "\"components\":{"
        "\"mass\":3,"
        "\"tables\":{"
        "\"beams\":["
        "[\"id1:\",\"id2:\",\"beamSpring\",\"beamDamp\"],"
        "[\"ref\",\"back\",300000,30]"
        "]"
        "}"
        "}"
        "}"
        "}"));

    const JBeamResolvedGraph graph = ResolveConfigured(
        packages,
        "{\"parts\":{},\"vars\":{}}");
    CHECK(graph.IsValid());
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(ir.IsValid());
    CHECK(ir.nodes.size() == 6U);
    if (ir.nodes.size() == 6U)
    {
        CHECK(ir.nodes[0].id == "ref");
        CHECK(ir.nodes[0].node_weight == 3.0);
    }
    CHECK(ir.beams.size() == 1U);
    if (ir.beams.size() == 1U)
    {
        CHECK(ir.beams[0].spring == 300000.0);
        CHECK(ir.beams[0].damping == 30.0);
    }
    CHECK(ir.ref_frame.reference == "ref");
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 0U);
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::INVALID_SECTION) == 0U);

    std::reverse(packages.begin(), packages.end());
    const JBeamStructuralIR permuted =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveConfigured(
            packages,
            "{\"parts\":{},\"vars\":{}}"));
    CHECK(permuted.IsValid());
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(ir) ==
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(permuted));

    JBeamStructuralLimits reference_limit;
    reference_limit.expression_limits.max_expression_bytes = 8U;
    const JBeamStructuralIR limited =
        RoR::BeamNG::BuildJBeamStructuralIR(graph, reference_limit);
    CHECK(!limited.IsValid());
    CHECK(limited.nodes.empty());
    CHECK(limited.beams.empty());
    CHECK(CountDiagnostic(
        limited,
        JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT) == 3U);

    const JBeamStructuralIR wrong_type =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"components\":{\"table\":1},"
            "\"nodes\":\"$=$components.table\""));
    CHECK(!wrong_type.IsValid());
    CHECK(CountDiagnostic(
        wrong_type,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 1U);
    const JBeamStructuralDiagnostic* wrong_type_error = FindDiagnostic(
        wrong_type,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR);
    CHECK(wrong_type_error != NULL);
    if (wrong_type_error != NULL)
    {
        CHECK(wrong_type_error->detail.find("array-valued") !=
            std::string::npos);
    }

    const JBeamStructuralIR non_exact =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"components\":{\"table\":[[\"id\"]]},"
            "\"nodes\":\"$=$components.table or nil\""));
    CHECK(!non_exact.IsValid());
    CHECK(CountDiagnostic(
        non_exact,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 1U);
}

void TestSlotNamespaceExpressionPipeline()
{
    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/namespace/main.jbeam",
        "{"
        "\"car\":{"
        "\"slotType\":\"main\","
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"aux\",\"namespace_child\",\"Aux\",{"
        "\"variables\":{"
        "\"$prefix\":\"aux_\","
        "\"$suffix\":\"_x\","
        "\"$delta\":0.25"
        "}}]"
        "],"
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"ref\",0,0,0],"
        "[\"back\",0,1,0],"
        "[\"left\",1,0,0],"
        "[\"up\",0,0,1],"
        "[\"leftCorner\",1,-1,0],"
        "[\"rightCorner\",-1,-1,0]"
        "],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]"
        "]"
        "}"
        "}"));
    packages.push_back(Package(
        "vehicles/namespace/child.jbeam",
        "{"
        "\"namespace_child\":{"
        "\"slotType\":\"aux\","
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"$.sensor\",\"$=$delta\",0,0]"
        "],"
        "\"beams\":["
        "[\"id1:\",\"id2:\"],"
        "[\"ref\",\"$.sensor\"]"
        "]"
        "}"
        "}"));
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(Resolve(packages));
    CHECK(ir.IsValid());
    CHECK(ir.nodes.size() == 7U);
    CHECK(ir.nodes[6].id == "aux_sensor_x");
    CHECK(ir.nodes[6].x == 0.25);
    CHECK(ir.beams.size() == 1U);
    CHECK(ir.beams[0].node_b == "aux_sensor_x");
}

void TestExpressionFailuresAndAggregateLimits()
{
    const JBeamStructuralIR missing =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",\"$missing\",0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!missing.IsValid());
    CHECK(CountDiagnostic(
        missing,
        JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE) == 1U);
    CHECK(CountDiagnostic(
        missing,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 0U);

    const JBeamStructuralIR forbidden =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",\"$=os.execute('x')\",0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!forbidden.IsValid());
    CHECK(CountDiagnostic(
        forbidden,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 1U);
    const JBeamStructuralDiagnostic* expression_error =
        FindDiagnostic(
            forbidden,
            JBeamStructuralDiagnosticCode::EXPRESSION_ERROR);
    CHECK(expression_error != NULL);
    if (expression_error != NULL)
    {
        CHECK(expression_error->section == "nodes");
        CHECK(expression_error->field_name == "posX");
        CHECK(expression_error->provenance.SourceName() ==
            "vehicles/clean/main.jbeam");
        CHECK(expression_error->detail.find("decoded byte") !=
            std::string::npos);
    }

    const JBeamStructuralIR invalid_function_argument =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",\"$=clamp(1,2,-2)\",0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!invalid_function_argument.IsValid());
    CHECK(CountDiagnostic(
        invalid_function_argument,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 1U);
    const JBeamStructuralDiagnostic* invalid_function_error =
        FindDiagnostic(
            invalid_function_argument,
            JBeamStructuralDiagnosticCode::EXPRESSION_ERROR);
    CHECK(invalid_function_error != NULL);
    if (invalid_function_error != NULL)
    {
        CHECK(invalid_function_error->section == "nodes");
        CHECK(invalid_function_error->field_name == "posX");
        CHECK(invalid_function_error->provenance.SourceName() ==
            "vehicles/clean/main.jbeam");
        CHECK(invalid_function_error->detail.find(
            "decoded byte 2 (invalid-function-argument)") !=
            std::string::npos);
    }

    std::string too_many_arguments = "$=max(";
    for (std::size_t index = 0U; index < 65U; ++index)
    {
        if (index != 0U)
        {
            too_many_arguments.push_back(',');
        }
        too_many_arguments.push_back('1');
    }
    too_many_arguments.push_back(')');
    const std::string function_limit_body =
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"ref\",\"" + too_many_arguments + "\",0,0],"
        "[\"back\",0,1,0],"
        "[\"left\",1,0,0],"
        "[\"up\",0,0,1],"
        "[\"leftCorner\",1,-1,0],"
        "[\"rightCorner\",-1,-1,0]"
        "],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]"
        "]";
    const JBeamStructuralIR function_limit =
        RoR::BeamNG::BuildJBeamStructuralIR(
            ResolveSingle(function_limit_body));
    CHECK(!function_limit.IsValid());
    CHECK(CountDiagnostic(
        function_limit,
        JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT) == 1U);
    CHECK(CountDiagnostic(
        function_limit,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 0U);
    const JBeamStructuralDiagnostic* function_limit_error =
        FindDiagnostic(
            function_limit,
            JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT);
    CHECK(function_limit_error != NULL);
    if (function_limit_error != NULL)
    {
        CHECK(function_limit_error->detail.find(
            "function-argument-limit") != std::string::npos);
    }

    const JBeamStructuralIR table_component =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"components\":{\"position\":[1,2,3]},"
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\","
            "\"$=$components.position == nil and 0 or 1\",0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!table_component.IsValid());
    CHECK(CountDiagnostic(
        table_component,
        JBeamStructuralDiagnosticCode::
            UNSUPPORTED_COMPONENT_VALUE) == 0U);
    CHECK(CountDiagnostic(
        table_component,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 1U);

    JBeamStructuralLimits evaluation_limit;
    evaluation_limit.max_expression_evaluations = 0U;
    const JBeamStructuralIR no_evaluations =
        RoR::BeamNG::BuildJBeamStructuralIR(
            ResolveSingle(
                "\"nodes\":["
                "[\"id\",\"posX\",\"posY\",\"posZ\"],"
                "[\"ref\",\"$=1\",0,0],"
                "[\"back\",0,1,0],"
                "[\"left\",1,0,0],"
                "[\"up\",0,0,1],"
                "[\"leftCorner\",1,-1,0],"
                "[\"rightCorner\",-1,-1,0]"
                "],"
                "\"refNodes\":["
                "[\"ref:\",\"back:\",\"left:\",\"up:\","
                "\"leftCorner:\",\"rightCorner:\"],"
                "[\"ref\",\"back\",\"left\",\"up\","
                "\"leftCorner\",\"rightCorner\"]"
                "]"),
            evaluation_limit);
    CHECK(!no_evaluations.IsValid());
    CHECK(CountDiagnostic(
        no_evaluations,
        JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT) == 1U);

    JBeamStructuralLimits component_depth;
    component_depth.max_component_depth = 1U;
    const JBeamStructuralIR deep_component =
        RoR::BeamNG::BuildJBeamStructuralIR(
            ResolveSingle(
                "\"components\":{\"a\":{\"b\":1}},"
                + FrameAndNodes()),
            component_depth);
    CHECK(!deep_component.IsValid());
    CHECK(CountDiagnostic(
        deep_component,
        JBeamStructuralDiagnosticCode::EXPRESSION_LIMIT) == 1U);
}

void TestNonFiniteDefenseUnderFastMath()
{
    JBeamResolvedGraph graph = ResolveSingle(FrameAndNodes());
    JBeamValue* nodes = NULL;
    for (std::size_t i = 0U;
         i < graph.root->definition.body.object_fields.size();
         ++i)
    {
        if (graph.root->definition.body.object_fields[i].key == "nodes")
        {
            nodes = const_cast<JBeamValue*>(
                graph.root->definition.body.object_fields[i].value.get());
        }
    }
    CHECK(nodes != NULL);
    if (nodes != NULL)
    {
        nodes->array_values[1].array_values[1].number_value =
            PositiveInfinityBits();
    }
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(!ir.IsValid());
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::NON_FINITE_NUMBER) == 1U);
}

void TestBeamReferenceAndTypeSemantics()
{
    const JBeamStructuralIR optional =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"beams\":["
            "[\"id1:\",\"id2:\",\"optional\",\"beamType\"],"
            "[\"ref\",\"missing\",true,\"NORMAL\"],"
            "[\"ref\",\"left\",false,\"|BOUNDED\"]"
            "]"));
    CHECK(optional.IsValid());
    CHECK(optional.beams.size() == 2U);
    CHECK(optional.beams[0].status ==
        JBeamStructuralBeamStatus::
            PRESERVED_DISABLED_OPTIONAL_REFERENCE);
    CHECK(optional.beams[0].node_b_index ==
        static_cast<std::size_t>(-1));
    CHECK(optional.beams[1].status ==
        JBeamStructuralBeamStatus::
            PRESERVED_DISABLED_SPECIAL_TYPE);
    CHECK(optional.beams[1].beam_type == "|BOUNDED");
    CHECK(CountDiagnostic(
        optional,
        JBeamStructuralDiagnosticCode::OPTIONAL_BEAM_SKIPPED) == 1U);
    CHECK(CountDiagnostic(
        optional,
        JBeamStructuralDiagnosticCode::
            SPECIAL_BEAM_TYPE_DISABLED) == 1U);

    const JBeamStructuralIR required =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"beams\":["
            "[\"id1:\",\"id2:\"],"
            "[\"ref\",\"missing\"]"
            "]"));
    CHECK(!required.IsValid());
    CHECK(required.beams.empty());
    CHECK(CountDiagnostic(
        required,
        JBeamStructuralDiagnosticCode::MISSING_NODE_REFERENCE) == 1U);

    const JBeamStructuralIR degenerate =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",0,0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0],"
            "[\"other\",0,0,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "],"
            "\"beams\":["
            "[\"id1:\",\"id2:\"],"
            "[\"ref\",\"other\"]"
            "]"));
    CHECK(!degenerate.IsValid());
    CHECK(CountDiagnostic(
        degenerate,
        JBeamStructuralDiagnosticCode::DEGENERATE_BEAM) == 1U);
}

void TestBeamInfinitySentinel()
{
    const JBeamStructuralIR unbounded =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"beams\":["
            "[\"id1:\",\"id2:\",\"beamDeform\",\"beamStrength\"],"
            "[\"ref\",\"back\",\"FLT_MAX\",\"FLT_MAX\"]"
            "]"));
    CHECK(unbounded.IsValid());
    CHECK(unbounded.beams.size() == 1U);
    CHECK(unbounded.beams[0].has_deform);
    CHECK(unbounded.beams[0].deform_unbounded);
    CHECK(unbounded.beams[0].deform == 0.0);
    CHECK(unbounded.beams[0].has_strength);
    CHECK(unbounded.beams[0].strength_unbounded);
    CHECK(unbounded.beams[0].strength == 0.0);

    const JBeamStructuralIR finite =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"beams\":["
            "[\"id1:\",\"id2:\",\"beamDeform\",\"beamStrength\"],"
            "[\"ref\",\"back\",1.5e8,2.5e8]"
            "]"));
    CHECK(finite.IsValid());
    CHECK(finite.beams.size() == 1U);
    CHECK(!finite.beams[0].deform_unbounded);
    CHECK(!finite.beams[0].strength_unbounded);
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(unbounded) !=
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(finite));

    const JBeamStructuralIR arbitrary_string =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"beams\":["
            "[\"id1:\",\"id2:\",\"beamDeform\",\"beamStrength\"],"
            "[\"ref\",\"back\",\"Infinity\",\"FLT_MAX\"]"
            "]"));
    CHECK(!arbitrary_string.IsValid());
    CHECK(CountDiagnostic(
        arbitrary_string,
        JBeamStructuralDiagnosticCode::INVALID_FIELD_TYPE) == 1U);
}

void TestTriangleAndFrameFailures()
{
    const JBeamStructuralIR bad_triangle =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"triangles\":["
            "[\"id1:\",\"id2:\",\"id3:\"],"
            "[\"ref\",\"back\",\"missing\"],"
            "[\"ref\",\"back\",\"back\"]"
            "]"));
    CHECK(!bad_triangle.IsValid());
    CHECK(CountDiagnostic(
        bad_triangle,
        JBeamStructuralDiagnosticCode::MISSING_NODE_REFERENCE) == 1U);
    CHECK(CountDiagnostic(
        bad_triangle,
        JBeamStructuralDiagnosticCode::DUPLICATE_VERTEX) == 1U);

    const JBeamStructuralIR missing_frame =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"n\",0,0,0]"
            "]"));
    CHECK(!missing_frame.IsValid());
    CHECK(CountDiagnostic(
        missing_frame,
        JBeamStructuralDiagnosticCode::MISSING_REF_NODES) == 1U);

    const JBeamStructuralIR duplicate_frame =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",0,0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!duplicate_frame.IsValid());
    CHECK(CountDiagnostic(
        duplicate_frame,
        JBeamStructuralDiagnosticCode::DUPLICATE_REF_NODES) == 1U);

    const JBeamStructuralIR degenerate_frame =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",0,0,0],"
            "[\"back\",1,0,0],"
            "[\"left\",2,0,0],"
            "[\"up\",3,0,0],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!degenerate_frame.IsValid());
    CHECK(CountDiagnostic(
        degenerate_frame,
        JBeamStructuralDiagnosticCode::DEGENERATE_REF_NODES) == 1U);

    const JBeamStructuralIR reversed_frame =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",0,0,0],"
            "[\"back\",0,-1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,1,0],"
            "[\"rightCorner\",-1,1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!reversed_frame.IsValid());
    CHECK(CountDiagnostic(
        reversed_frame,
        JBeamStructuralDiagnosticCode::MISALIGNED_REF_NODES) == 1U);

    const JBeamStructuralIR skewed_frame =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",10,20,30],"
            "[\"back\",10.001,21,30],"
            "[\"left\",11,20,30],"
            "[\"up\",10,20,31],"
            "[\"leftCorner\",11,19,30],"
            "[\"rightCorner\",9,19,30]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!skewed_frame.IsValid());
    CHECK(CountDiagnostic(
        skewed_frame,
        JBeamStructuralDiagnosticCode::MISALIGNED_REF_NODES) == 1U);
}

void TestOptionalSurfacesAndTranslatedGeometry()
{
    const JBeamStructuralIR optional =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"triangles\":["
            "[\"id1:\",\"id2:\",\"id3:\",\"optional\"],"
            "[\"ref\",\"left\",\"missing\",true]"
            "],"
            "\"quads\":["
            "[\"id1:\",\"id2:\",\"id3:\",\"id4:\",\"optional\"],"
            "[\"ref\",\"back\",\"leftCorner\",\"missing\",true]"
            "]"));
    CHECK(optional.IsValid());
    CHECK(optional.triangles.size() == 3U);
    CHECK(optional.triangles[0].optional);
    CHECK(optional.triangles[0].status ==
        RoR::BeamNG::JBeamStructuralTriangleStatus::
            PRESERVED_DISABLED_OPTIONAL_REFERENCE);
    CHECK(optional.triangles[1].status ==
        RoR::BeamNG::JBeamStructuralTriangleStatus::
            PRESERVED_DISABLED_OPTIONAL_REFERENCE);
    CHECK(optional.triangles[2].status ==
        RoR::BeamNG::JBeamStructuralTriangleStatus::
            PRESERVED_DISABLED_OPTIONAL_REFERENCE);
    CHECK(CountDiagnostic(
        optional,
        JBeamStructuralDiagnosticCode::
            OPTIONAL_SURFACE_SKIPPED) == 2U);

    const JBeamStructuralIR translated_valid =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",0,0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0],"
            "[\"ta\",1000000000000,1000000000000,0],"
            "[\"tb\",1000000000001,1000000000000,0],"
            "[\"tc\",1000000000000,1000000000001,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "],"
            "\"triangles\":["
            "[\"id1:\",\"id2:\",\"id3:\"],"
            "[\"ta\",\"tb\",\"tc\"]"
            "]"));
    CHECK(translated_valid.IsValid());
    CHECK(translated_valid.triangles.size() == 1U);
}

void TestExtremeFiniteReferenceAlignment()
{
    const JBeamStructuralIR diagonal_back =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",-1.7976931348623157e308,"
                "-1.7976931348623157e308,"
                "-1.7976931348623157e308],"
            "[\"back\",1.7976931348623157e308,"
                "1.7976931348623157e308,"
                "-1.7976931348623157e308],"
            "[\"left\",1.7976931348623157e308,"
                "-1.7976931348623157e308,"
                "-1.7976931348623157e308],"
            "[\"up\",-1.7976931348623157e308,"
                "-1.7976931348623157e308,"
                "1.7976931348623157e308],"
            "[\"leftCorner\",1.7976931348623157e308,"
                "-1.7976931348623157e308,"
                "-1.7976931348623157e308],"
            "[\"rightCorner\",-1.7976931348623157e308,"
                "-1.7976931348623157e308,"
                "-1.7976931348623157e308]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!diagonal_back.IsValid());
    CHECK(CountDiagnostic(
        diagonal_back,
        JBeamStructuralDiagnosticCode::MISALIGNED_REF_NODES) == 1U);
    CHECK(!diagonal_back.has_ref_frame);
}

void TestPreservedValueBudgetsAndCycles()
{
    JBeamResolvedGraph graph = ResolveSingle(
        "\"unknown\":[]," + FrameAndNodes());
    CHECK(graph.root != NULL);
    if (!graph.root)
    {
        return;
    }
    JBeamObjectField* unknown =
        MutableLastField(graph.root->definition.body, "unknown");
    CHECK(unknown != NULL);
    if (unknown == NULL)
    {
        return;
    }

    std::shared_ptr<JBeamValue> large(new JBeamValue());
    large->type = JBeamValueType::ARRAY;
    large->array_values.resize(20000U);
    unknown->value = large;

    const JBeamStructuralIR admitted =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(admitted.IsValid());
    CHECK(admitted.retained_byte_count >
        20000U * sizeof(JBeamValue));

    JBeamStructuralLimits tiny;
    tiny.max_retained_bytes = 2048U;
    const JBeamStructuralIR rejected =
        RoR::BeamNG::BuildJBeamStructuralIR(graph, tiny);
    CHECK(!rejected.IsValid());
    CHECK(rejected.nodes.empty());
    CHECK(rejected.retained_byte_count <= tiny.max_retained_bytes);
    CHECK(CountDiagnostic(
        rejected,
        JBeamStructuralDiagnosticCode::RETAINED_BYTE_LIMIT) == 1U);

    std::shared_ptr<JBeamValue> cyclic(new JBeamValue());
    cyclic->type = JBeamValueType::OBJECT;
    JBeamObjectField self;
    self.key = "self";
    self.value = cyclic;
    cyclic->object_fields.push_back(self);
    unknown->value = cyclic;
    const JBeamStructuralIR cycle_rejected =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(!cycle_rejected.IsValid());
    CHECK(cycle_rejected.nodes.empty());
    CHECK(CountDiagnostic(
        cycle_rejected,
        JBeamStructuralDiagnosticCode::PRESERVED_VALUE_LIMIT) == 1U);
    cyclic->object_fields.clear();
}

void TestResolvedPartGraphCyclesAndAliases()
{
    JBeamStructuralLimits unlimited;
    unlimited.max_parts = static_cast<std::size_t>(-1);

    JBeamResolvedGraph cyclic = ResolveSingle(FrameAndNodes());
    CHECK(cyclic.root != NULL);
    if (!cyclic.root)
    {
        return;
    }
    RoR::BeamNG::JBeamResolvedSlot self_slot;
    self_slot.child = cyclic.root;
    cyclic.root->slots.push_back(self_slot);
    const JBeamStructuralIR cycle_rejected =
        RoR::BeamNG::BuildJBeamStructuralIR(cyclic, unlimited);
    CHECK(!cycle_rejected.IsValid());
    CHECK(cycle_rejected.nodes.empty());
    CHECK(CountDiagnostic(
        cycle_rejected,
        JBeamStructuralDiagnosticCode::INVALID_RESOLVED_GRAPH) == 1U);
    cyclic.root->slots.clear();

    JBeamResolvedGraph aliased = ResolveSingle(FrameAndNodes());
    CHECK(aliased.root != NULL);
    if (!aliased.root)
    {
        return;
    }
    std::shared_ptr<RoR::BeamNG::JBeamResolvedPartNode> child(
        new RoR::BeamNG::JBeamResolvedPartNode());
    child->definition.name = "shared-child";
    child->definition.package_path =
        "vehicles/clean/shared-child.jbeam";
    child->definition.body.type = JBeamValueType::OBJECT;
    RoR::BeamNG::JBeamResolvedSlot first;
    first.child = child;
    RoR::BeamNG::JBeamResolvedSlot second;
    second.child = child;
    aliased.root->slots.push_back(first);
    aliased.root->slots.push_back(second);
    const JBeamStructuralIR alias_rejected =
        RoR::BeamNG::BuildJBeamStructuralIR(aliased, unlimited);
    CHECK(!alias_rejected.IsValid());
    CHECK(alias_rejected.nodes.empty());
    CHECK(CountDiagnostic(
        alias_rejected,
        JBeamStructuralDiagnosticCode::INVALID_RESOLVED_GRAPH) == 1U);
}

void TestBoundedNormalizerDiagnostics()
{
    const JBeamStructuralIR warned =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",0,0,0,99],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",1,-1,0],"
            "[\"rightCorner\",-1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(warned.IsValid());
    const JBeamStructuralDiagnostic* warning =
        FindDiagnostic(
            warned,
            JBeamStructuralDiagnosticCode::NORMALIZATION_WARNING);
    CHECK(warning != NULL);
    if (warning != NULL)
    {
        CHECK(warning->provenance.SourceName() ==
            "vehicles/clean/main.jbeam");
        CHECK(warning->detail.find("table-row-too-long") !=
            std::string::npos);
    }

    JBeamStructuralLimits tiny;
    tiny.max_retained_bytes = 2048U;
    const JBeamStructuralIR rejected =
        RoR::BeamNG::BuildJBeamStructuralIR(
            ResolveSingle(FrameAndNodes()),
            tiny);
    CHECK(!rejected.IsValid());
    CHECK(rejected.nodes.empty());
    const JBeamStructuralDiagnostic* diagnostic =
        FindDiagnostic(
            rejected,
            JBeamStructuralDiagnosticCode::RETAINED_BYTE_LIMIT);
    CHECK(diagnostic != NULL);
    if (diagnostic != NULL)
    {
        CHECK(diagnostic->provenance.SourceName() ==
            "vehicles/clean/main.jbeam");
        CHECK(diagnostic->detail.find(
            "normalize-retained-bytes-limit") !=
            std::string::npos);
    }
}

void TestCanonicalPreservedValueCycle()
{
    std::shared_ptr<JBeamValue> cyclic(new JBeamValue());
    cyclic->type = JBeamValueType::OBJECT;
    JBeamObjectField self;
    self.key = "self";
    self.value = cyclic;
    cyclic->object_fields.push_back(self);

    JBeamStructuralDiagnostic diagnostic;
    diagnostic.code = JBeamStructuralDiagnosticCode::UNKNOWN_SECTION;
    diagnostic.severity = RoR::BeamNG::JBeamStructuralSeverity::WARNING;
    diagnostic.has_preserved_value = true;
    diagnostic.preserved_value = cyclic;
    JBeamStructuralIR ir;
    ir.diagnostics.push_back(diagnostic);
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(ir).empty());
    cyclic->object_fields.clear();
}

void TestOfficialRefCornersAndBeamParameterRanges()
{
    const JBeamStructuralIR missing_corners =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",0,0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\"],"
            "[\"ref\",\"back\",\"left\",\"up\"]"
            "]"));
    CHECK(!missing_corners.IsValid());
    CHECK(CountDiagnostic(
        missing_corners,
        JBeamStructuralDiagnosticCode::MISSING_REQUIRED_FIELD) == 2U);

    const JBeamStructuralIR swapped_corners =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"ref\",0,0,0],"
            "[\"back\",0,1,0],"
            "[\"left\",1,0,0],"
            "[\"up\",0,0,1],"
            "[\"leftCorner\",-1,-1,0],"
            "[\"rightCorner\",1,-1,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!swapped_corners.IsValid());
    CHECK(CountDiagnostic(
        swapped_corners,
        JBeamStructuralDiagnosticCode::MISALIGNED_REF_CORNERS) == 1U);

    const JBeamStructuralIR negative =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"beams\":["
            "[\"id1:\",\"id2:\",\"beamSpring\",\"beamDamp\","
            "\"beamDeform\",\"beamStrength\",\"beamPrecompression\"],"
            "[\"ref\",\"back\",-1,1,1,1,1],"
            "[\"ref\",\"back\",1,-1,1,1,1],"
            "[\"ref\",\"back\",1,1,-1,1,1],"
            "[\"ref\",\"back\",1,1,1,-1,1],"
            "[\"ref\",\"back\",1,1,1,1,0]"
            "]"));
    CHECK(!negative.IsValid());
    CHECK(negative.beams.empty());
    CHECK(CountDiagnostic(
        negative,
        JBeamStructuralDiagnosticCode::INVALID_BEAM_PARAMETER) == 5U);
}

class GroupedNumbers : public std::numpunct<char>
{
protected:
    virtual char do_thousands_sep() const
    {
        return '_';
    }

    virtual std::string do_grouping() const
    {
        return "\3";
    }
};

void TestSharedProvenanceAndSerializationBudgets()
{
    std::ostringstream source;
    source.imbue(std::locale::classic());
    source
        << "{\"car\":{\"slotType\":\"main\",\"nodes\":["
        << "[\"id\",\"posX\",\"posY\",\"posZ\"],"
        << "[\"ref\",0,0,0],[\"back\",0,1,0],"
        << "[\"left\",1,0,0],[\"up\",0,0,1],"
        << "[\"leftCorner\",1,-1,0],"
        << "[\"rightCorner\",-1,-1,0]";
    for (std::size_t i = 0U; i < 500U; ++i)
    {
        source << ",[\"n" << i << "\"," << (i + 2U)
               << ",2,3]";
    }
    source
        << "],\"refNodes\":["
        << "[\"ref:\",\"back:\",\"left:\",\"up:\","
        << "\"leftCorner:\",\"rightCorner:\"],"
        << "[\"ref\",\"back\",\"left\",\"up\","
        << "\"leftCorner\",\"rightCorner\"]]}}";

    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source.str(), "short-source.jbeam");
    CHECK(parsed.IsValid());
    JBeamPackageSource package;
    package.package_path =
        std::string(40000U, 'p') + ".jbeam";
    package.document = parsed.root;
    std::vector<JBeamPackageSource> packages;
    packages.push_back(package);
    const JBeamResolvedGraph graph = Resolve(packages);

    JBeamStructuralLimits limits;
    limits.max_retained_bytes = 2U * 1024U * 1024U;
    limits.max_canonical_output_bytes = 1000000U;
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph, limits);
    CHECK(ir.IsValid());
    CHECK(ir.nodes.size() == 506U);
    CHECK(ir.retained_byte_count < limits.max_retained_bytes);
    if (!ir.nodes.empty())
    {
        CHECK(ir.nodes.front().provenance.part ==
            ir.nodes.back().provenance.part);
    }
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(ir).empty());

    limits.max_retained_bytes = 1000U;
    const JBeamStructuralIR rejected =
        RoR::BeamNG::BuildJBeamStructuralIR(graph, limits);
    CHECK(!rejected.IsValid());
    CHECK(rejected.nodes.empty());
    CHECK(CountDiagnostic(
        rejected,
        JBeamStructuralDiagnosticCode::RETAINED_BYTE_LIMIT) == 1U);

    const JBeamStructuralIR ordinary =
        RoR::BeamNG::BuildJBeamStructuralIR(
            ResolveSingle(FrameAndNodes()));
    const std::string baseline =
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(ordinary);
    const std::locale previous = std::locale::global(
        std::locale(
            std::locale::classic(), new GroupedNumbers()));
    const std::string localized =
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(ordinary);
    std::locale::global(previous);
    CHECK(!baseline.empty());
    CHECK(localized == baseline);

    JBeamStructuralIR zero_output = ordinary;
    zero_output.canonical_output_byte_limit = 0U;
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(
        zero_output).empty());

    const std::size_t ordinary_work =
        1U +
        ordinary.parts.size() +
        ordinary.nodes.size() +
        ordinary.beams.size() +
        ordinary.triangles.size() +
        ordinary.diagnostics.size();
    JBeamStructuralIR bounded_work = ordinary;
    bounded_work.canonical_work_unit_limit = ordinary_work - 1U;
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(
        bounded_work).empty());
    bounded_work.canonical_work_unit_limit = ordinary_work;
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(
        bounded_work) == baseline);

    JBeamStructuralIR preserved_work;
    preserved_work.canonical_output_byte_limit = 4096U;
    preserved_work.canonical_work_unit_limit = 2U;
    JBeamStructuralDiagnostic diagnostic;
    diagnostic.code = JBeamStructuralDiagnosticCode::UNKNOWN_FIELD;
    diagnostic.severity =
        RoR::BeamNG::JBeamStructuralSeverity::WARNING;
    diagnostic.has_preserved_value = true;
    diagnostic.preserved_value.reset(new JBeamValue());
    preserved_work.diagnostics.push_back(diagnostic);
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(
        preserved_work).empty());
}

void TestDiagnosticLimitStopsStructuralPhases()
{
    JBeamStructuralLimits limits;
    limits.max_diagnostics = 1U;
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"unknownOne\":{\"x\":1},"
            "\"unknownTwo\":{\"x\":2}," +
            FrameAndNodes()),
            limits);
    CHECK(!ir.IsValid());
    CHECK(ir.diagnostics.size() == 1U);
    CHECK(ir.diagnostics[0].code ==
        JBeamStructuralDiagnosticCode::DIAGNOSTIC_LIMIT);
    CHECK(ir.nodes.empty());
    CHECK(ir.beams.empty());
    CHECK(ir.triangles.empty());

    const JBeamStructuralIR current_phase =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"posX\",\"posY\",\"posZ\",\"u1\",\"u2\"],"
            "[\"ref\",0,0,0,1,2],"
            "[\"back\",0,1,0,1,2],"
            "[\"left\",1,0,0,1,2],"
            "[\"up\",0,0,1,1,2],"
            "[\"leftCorner\",1,-1,0,1,2],"
            "[\"rightCorner\",-1,-1,0,1,2]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"),
            limits);
    CHECK(!current_phase.IsValid());
    CHECK(current_phase.diagnostics.size() == 1U);
    CHECK(current_phase.diagnostics[0].code ==
        JBeamStructuralDiagnosticCode::DIAGNOSTIC_LIMIT);
    CHECK(current_phase.nodes.empty());
    CHECK(!current_phase.has_ref_frame);
}

void TestLimitsAndAmbiguity()
{
    const JBeamResolvedGraph graph = ResolveSingle(
        FrameAndNodes() + ","
        "\"beams\":["
        "[\"id1:\",\"id2:\"],"
        "[\"ref\",\"back\"]"
        "],"
        "\"triangles\":["
        "[\"id1:\",\"id2:\",\"id3:\"],"
        "[\"ref\",\"back\",\"left\"]"
        "]");

    JBeamStructuralLimits limits;
    limits.max_parts = 0U;
    JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph, limits);
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::RESOLVED_PART_LIMIT) == 1U);

    limits = JBeamStructuralLimits();
    limits.max_rows = 1U;
    ir = RoR::BeamNG::BuildJBeamStructuralIR(graph, limits);
    CHECK(CountDiagnostic(
        ir, JBeamStructuralDiagnosticCode::ROW_LIMIT) == 1U);

    limits = JBeamStructuralLimits();
    limits.max_nodes = 3U;
    ir = RoR::BeamNG::BuildJBeamStructuralIR(graph, limits);
    CHECK(CountDiagnostic(
        ir, JBeamStructuralDiagnosticCode::NODE_LIMIT) == 1U);

    limits = JBeamStructuralLimits();
    limits.max_beams = 0U;
    ir = RoR::BeamNG::BuildJBeamStructuralIR(graph, limits);
    CHECK(CountDiagnostic(
        ir, JBeamStructuralDiagnosticCode::BEAM_LIMIT) == 1U);

    limits = JBeamStructuralLimits();
    limits.max_triangles = 0U;
    ir = RoR::BeamNG::BuildJBeamStructuralIR(graph, limits);
    CHECK(CountDiagnostic(
        ir, JBeamStructuralDiagnosticCode::TRIANGLE_LIMIT) == 1U);

    limits = JBeamStructuralLimits();
    limits.max_diagnostics = 0U;
    ir = RoR::BeamNG::BuildJBeamStructuralIR(
        ResolveSingle("\"unknown\":{\"x\":1}"), limits);
    CHECK(ir.diagnostics.size() == 1U);
    CHECK(ir.diagnostics[0].code ==
        JBeamStructuralDiagnosticCode::DIAGNOSTIC_LIMIT);
    CHECK(!ir.IsValid());

    const JBeamStructuralIR duplicate_section =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            FrameAndNodes() + ","
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!duplicate_section.IsValid());
    CHECK(CountDiagnostic(
        duplicate_section,
        JBeamStructuralDiagnosticCode::DUPLICATE_SECTION) == 1U);

    const JBeamStructuralIR duplicate_header =
        RoR::BeamNG::BuildJBeamStructuralIR(ResolveSingle(
            "\"nodes\":["
            "[\"id\",\"id\",\"posX\",\"posY\",\"posZ\"],"
            "[\"wrong\",\"ref\",0,0,0]"
            "],"
            "\"refNodes\":["
            "[\"ref:\",\"back:\",\"left:\",\"up:\","
            "\"leftCorner:\",\"rightCorner:\"],"
            "[\"ref\",\"back\",\"left\",\"up\","
            "\"leftCorner\",\"rightCorner\"]"
            "]"));
    CHECK(!duplicate_header.IsValid());
    CHECK(CountDiagnostic(
        duplicate_header,
        JBeamStructuralDiagnosticCode::DUPLICATE_TABLE_HEADER) == 1U);
}

void TestCleanRoomFixtureConformance()
{
    const std::string profile_path =
        FixturePath("fixture-profile.json");
    const std::string main_path =
        FixturePath("vehicles/ror_cleanroom/main.jbeam");
    const std::string power_path =
        FixturePath("vehicles/ror_cleanroom/power.jbeam");
    CHECK(!profile_path.empty());
    CHECK(!main_path.empty());
    CHECK(!power_path.empty());
    if (profile_path.empty() ||
        main_path.empty() ||
        power_path.empty())
    {
        return;
    }

    std::string profile_source;
    std::string main_source;
    std::string power_source;
    CHECK(ReadTextFile(profile_path, profile_source));
    CHECK(ReadTextFile(main_path, main_source));
    CHECK(ReadTextFile(power_path, power_source));
    if (profile_source.empty() ||
        main_source.empty() ||
        power_source.empty())
    {
        return;
    }

    const JBeamParseResult profile =
        RoR::BeamNG::ParseJBeam(
            profile_source,
            "fixture-profile.json");
    CHECK(profile.IsValid());
    if (!profile.IsValid())
    {
        return;
    }
    const JBeamValue* fixture_id =
        FieldValue(profile.root, "fixtureId");
    const JBeamValue* documentation_profile =
        FieldValue(profile.root, "documentationProfile");
    const JBeamValue* authorship =
        FieldValue(profile.root, "authorship");
    const JBeamValue* license =
        FieldValue(profile.root, "license");
    const JBeamValue* redistributable =
        FieldValue(profile.root, "redistributable");
    const JBeamValue* execution =
        FieldValue(profile.root, "execution");
    const JBeamValue* root_part =
        FieldValue(profile.root, "rootPart");
    CHECK(fixture_id != NULL);
    CHECK(documentation_profile != NULL);
    CHECK(authorship != NULL);
    CHECK(license != NULL);
    CHECK(redistributable != NULL);
    CHECK(execution != NULL);
    CHECK(root_part != NULL);
    if (fixture_id != NULL)
    {
        CHECK(fixture_id->type == JBeamValueType::STRING);
        CHECK(fixture_id->scalar_text ==
            "ror-cleanroom-structural-v1");
    }
    if (documentation_profile != NULL)
    {
        CHECK(documentation_profile->type ==
            JBeamValueType::STRING);
        CHECK(documentation_profile->scalar_text ==
            "beamng-docs-0.38.5.0-2026-07-27");
    }
    if (authorship != NULL)
    {
        CHECK(authorship->scalar_text == "original-clean-room");
    }
    if (license != NULL)
    {
        CHECK(license->scalar_text == "GPL-3.0-or-later");
    }
    if (redistributable != NULL)
    {
        CHECK(redistributable->type == JBeamValueType::BOOLEAN);
        CHECK(redistributable->boolean_value);
    }
    if (execution != NULL)
    {
        CHECK(execution->scalar_text == "data-only");
    }
    if (root_part != NULL)
    {
        CHECK(root_part->scalar_text == "ror_cleanroom_frame");
    }

    const JBeamValue* source_files =
        FieldValue(profile.root, "sourceFiles");
    CHECK(source_files != NULL);
    if (source_files != NULL)
    {
        CHECK(source_files->type == JBeamValueType::ARRAY);
        CHECK(source_files->array_values.size() == 2U);
        if (source_files->array_values.size() == 2U)
        {
            const JBeamValue* first_path = FieldValue(
                source_files->array_values[0], "path");
            const JBeamValue* first_hash = FieldValue(
                source_files->array_values[0], "sha256");
            const JBeamValue* second_path = FieldValue(
                source_files->array_values[1], "path");
            const JBeamValue* second_hash = FieldValue(
                source_files->array_values[1], "sha256");
            CHECK(first_path != NULL);
            CHECK(first_hash != NULL);
            CHECK(second_path != NULL);
            CHECK(second_hash != NULL);
            if (first_path != NULL)
            {
                CHECK(first_path->scalar_text ==
                    "vehicles/ror_cleanroom/main.jbeam");
            }
            if (first_hash != NULL)
            {
                CHECK(first_hash->scalar_text ==
                    "46405a25fdc0bc05b6aeedd532784fd49197c586c151295e"
                    "02a5aff978713f7c");
                CHECK(first_hash->scalar_text.size() == 64U);
            }
            if (second_path != NULL)
            {
                CHECK(second_path->scalar_text ==
                    "vehicles/ror_cleanroom/power.jbeam");
            }
            if (second_hash != NULL)
            {
                CHECK(second_hash->scalar_text ==
                    "347aab6fa045fe213185b9a106b5d16072e326a4e8bfceb"
                    "ca2158513c941b7e3");
                CHECK(second_hash->scalar_text.size() == 64U);
            }
        }
    }

    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/ror_cleanroom/main.jbeam",
        main_source));
    packages.push_back(Package(
        "vehicles/ror_cleanroom/power.jbeam",
        power_source));
    const JBeamResolvedGraph graph = Resolve(packages);
    CHECK(graph.root != NULL);
    if (graph.root)
    {
        CHECK(graph.root->definition.name ==
            "ror_cleanroom_frame");
        CHECK(graph.root->slots.size() == 1U);
        if (graph.root->slots.size() == 1U)
        {
            CHECK(graph.root->slots[0].status ==
                RoR::BeamNG::JBeamResolvedSlotStatus::RESOLVED);
            CHECK(graph.root->slots[0].selected_part ==
                "ror_cleanroom_power_basic");
            CHECK(graph.root->slots[0].child != NULL);
        }
    }

    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(ir.IsValid());
    CHECK(ir.parts.size() == 2U);
    CHECK(ir.nodes.size() == 9U);
    CHECK(ir.beams.size() == 12U);
    CHECK(ir.triangles.size() == 4U);
    std::size_t enabled_triangles = 0U;
    for (std::size_t i = 0U; i < ir.triangles.size(); ++i)
    {
        if (ir.triangles[i].status ==
            RoR::BeamNG::JBeamStructuralTriangleStatus::ENABLED)
        {
            ++enabled_triangles;
        }
    }
    CHECK(enabled_triangles == 4U);
    double total_mass = 0.0;
    for (std::size_t i = 0U; i < ir.nodes.size(); ++i)
    {
        total_mass += ir.nodes[i].node_weight;
    }
    CHECK(total_mass == 200.0);
    CHECK(ir.has_ref_frame);
    if (ir.has_ref_frame)
    {
        CHECK(ir.ref_frame.reference == "ref");
        CHECK(ir.ref_frame.back == "back");
        CHECK(ir.ref_frame.left == "left");
        CHECK(ir.ref_frame.up == "up");
        CHECK(ir.ref_frame.left_corner == "leftCorner");
        CHECK(ir.ref_frame.right_corner == "rightCorner");
    }
    if (ir.parts.size() == 2U)
    {
        CHECK(ir.parts[0].provenance.PartName() ==
            "ror_cleanroom_frame");
        CHECK(ir.parts[0].provenance.PackagePath() ==
            "vehicles/ror_cleanroom/main.jbeam");
        CHECK(ir.parts[1].provenance.PartName() ==
            "ror_cleanroom_power_basic");
        CHECK(ir.parts[1].provenance.PackagePath() ==
            "vehicles/ror_cleanroom/power.jbeam");
    }

    const JBeamValue* expected =
        FieldValue(profile.root, "expectedStructural");
    CHECK(expected != NULL);
    if (expected != NULL)
    {
        const JBeamValue* expected_parts =
            FieldValue(*expected, "parts");
        const JBeamValue* expected_nodes =
            FieldValue(*expected, "nodes");
        const JBeamValue* expected_mass =
            FieldValue(*expected, "totalMassKg");
        const JBeamValue* expected_beams =
            FieldValue(*expected, "beams");
        const JBeamValue* expected_triangles =
            FieldValue(*expected, "enabledTriangles");
        CHECK(expected_parts != NULL);
        CHECK(expected_nodes != NULL);
        CHECK(expected_mass != NULL);
        CHECK(expected_beams != NULL);
        CHECK(expected_triangles != NULL);
        if (expected_parts != NULL)
        {
            CHECK(expected_parts->number_value ==
                static_cast<double>(ir.parts.size()));
        }
        if (expected_nodes != NULL)
        {
            CHECK(expected_nodes->number_value ==
                static_cast<double>(ir.nodes.size()));
        }
        if (expected_mass != NULL)
        {
            CHECK(expected_mass->number_value == total_mass);
        }
        if (expected_beams != NULL)
        {
            CHECK(expected_beams->number_value ==
                static_cast<double>(ir.beams.size()));
        }
        if (expected_triangles != NULL)
        {
            CHECK(expected_triangles->number_value ==
                static_cast<double>(enabled_triangles));
        }
    }

    const std::string canonical =
        RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(ir);
    CHECK(!canonical.empty());
    std::reverse(packages.begin(), packages.end());
    const JBeamStructuralIR permuted =
        RoR::BeamNG::BuildJBeamStructuralIR(Resolve(packages));
    CHECK(permuted.IsValid());
    CHECK(RoR::BeamNG::SerializeCanonicalJBeamStructuralIR(
        permuted) == canonical);
}

void TestAuthoredTuningVariableExpressionPipeline()
{
    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/tuning/main.jbeam",
        "{\"car\":{"
        "\"slotType\":\"main\","
        "\"variables\":["
        "[\"name\",\"type\",\"unit\",\"category\",\"default\","
        "\"min\",\"max\",\"title\",\"description\"],"
        "[\"$mass\",\"range\",\"kg\",\"Chassis\",5,1,10,"
        "\"Mass\",\"Root mass\"],"
        "[\"$x\",\"range\",\"m\",\"Geometry\",0.25,0,2,"
        "\"X\",\"Root position\"]"
        "],"
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"child\",\"tuning_child\",\"Child\",{"
        "\"variables\":{\"$childMass\":3}}]"
        "],"
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\",\"nodeWeight\"],"
        "[\"ref\",0,0,0,1],"
        "[\"back\",0,1,0,1],"
        "[\"left\",1,0,0,1],"
        "[\"up\",0,0,1,1],"
        "[\"leftCorner\",1,-1,0,1],"
        "[\"rightCorner\",-1,-1,0,1],"
        "[\"tuned\",\"$x\",0,0,\"$=$mass+$childMass\"]"
        "],"
        "\"refNodes\":["
        "[\"ref:\",\"back:\",\"left:\",\"up:\","
        "\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]"
        "]"
        "}}"));
    packages.push_back(Package(
        "vehicles/tuning/child.jbeam",
        "{\"tuning_child\":{"
        "\"slotType\":\"child\","
        "\"variables\":["
        "[\"name\",\"type\",\"unit\",\"category\",\"default\","
        "\"min\",\"max\",\"title\",\"description\"],"
        "[\"$childMass\",\"range\",\"kg\",\"Chassis\",2,1,4,"
        "\"Child mass\",\"Child default\"]"
        "],"
        "\"nodes\":["
        "[\"id\",\"posX\",\"posY\",\"posZ\",\"nodeWeight\"],"
        "[\"childNode\",0,0,0,\"$childMass\"]"
        "]"
        "}}"));

    const JBeamResolvedGraph graph = ResolveConfigured(
        packages,
        "{\"parts\":{},\"vars\":{\"$mass\":6}}" );
    CHECK(graph.tuning_variables.size() == 3U);
    const JBeamStructuralIR ir =
        RoR::BeamNG::BuildJBeamStructuralIR(graph);
    CHECK(ir.IsValid());
    CHECK(ir.nodes.size() == 8U);
    CHECK(ir.nodes[0].id == "ref");
    CHECK(ir.nodes[6].id == "tuned");
    CHECK(ir.nodes[6].x == 0.25);
    CHECK(ir.nodes[6].node_weight == 8.0);
    CHECK(ir.nodes[7].id == "childNode");
    CHECK(ir.nodes[7].node_weight == 3.0);
    CHECK(CountDiagnostic(
        ir,
        JBeamStructuralDiagnosticCode::EXPRESSION_ERROR) == 0U);
}

} // namespace

int main()
{
    TestNormalizedCoreAndPreservedFields();
    TestResolvedPartPreorderAndCanonicalSourcePermutation();
    TestDuplicateAndMalformedNodes();
    TestResolvedExpressionsVariablesAndComponents();
    TestExactTableComponentRowExpansion();
    TestExactWholeTableComponentExpansion();
    TestSlotNamespaceExpressionPipeline();
    TestExpressionFailuresAndAggregateLimits();
    TestNonFiniteDefenseUnderFastMath();
    TestBeamReferenceAndTypeSemantics();
    TestBeamInfinitySentinel();
    TestTriangleAndFrameFailures();
    TestOptionalSurfacesAndTranslatedGeometry();
    TestExtremeFiniteReferenceAlignment();
    TestPreservedValueBudgetsAndCycles();
    TestResolvedPartGraphCyclesAndAliases();
    TestBoundedNormalizerDiagnostics();
    TestCanonicalPreservedValueCycle();
    TestOfficialRefCornersAndBeamParameterRanges();
    TestSharedProvenanceAndSerializationBudgets();
    TestDiagnosticLimitStopsStructuralPhases();
    TestLimitsAndAmbiguity();
    TestCleanRoomFixtureConformance();
    TestAuthoredTuningVariableExpressionPipeline();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " JBeam structural IR test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JBeam structural IR tests passed\n";
    return EXIT_SUCCESS;
}
