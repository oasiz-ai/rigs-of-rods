#include "JBeamPartResolver.h"

#include <algorithm>
#include <cstdlib>
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

using RoR::BeamNG::JBeamConfigurationResult;
using RoR::BeamNG::JBeamPackageIndex;
using RoR::BeamNG::JBeamPackageSource;
using RoR::BeamNG::JBeamParseResult;
using RoR::BeamNG::JBeamResolveDiagnostic;
using RoR::BeamNG::JBeamResolveDiagnosticCode;
using RoR::BeamNG::JBeamResolvedGraph;
using RoR::BeamNG::JBeamResolvedPartNode;
using RoR::BeamNG::JBeamResolvedSlot;
using RoR::BeamNG::JBeamResolvedSlotStatus;
using RoR::BeamNG::JBeamResolverLimits;
using RoR::BeamNG::JBeamValueType;
using RoR::BeamNG::JBeamVariableAssignment;

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

RoR::BeamNG::JBeamValue Value(
    const std::string& source,
    const std::string& path)
{
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, path);
    CHECK(parsed.IsValid());
    return parsed.root;
}

const JBeamResolveDiagnostic* FindDiagnostic(
    const std::vector<JBeamResolveDiagnostic>& diagnostics,
    JBeamResolveDiagnosticCode code)
{
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        if (diagnostics[i].code == code)
        {
            return &diagnostics[i];
        }
    }
    return NULL;
}

std::size_t CountDiagnostic(
    const std::vector<JBeamResolveDiagnostic>& diagnostics,
    JBeamResolveDiagnosticCode code)
{
    std::size_t count = 0U;
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        if (diagnostics[i].code == code)
        {
            ++count;
        }
    }
    return count;
}

const JBeamResolvedSlot* FindSlot(
    const JBeamResolvedPartNode& node,
    const std::string& name)
{
    for (std::size_t i = 0; i < node.slots.size(); ++i)
    {
        if (node.slots[i].definition.name == name)
        {
            return &node.slots[i];
        }
    }
    return NULL;
}

std::size_t CountSubstring(
    const std::string& text,
    const std::string& needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos)
    {
        ++count;
        offset += needle.size();
    }
    return count;
}

std::vector<JBeamPackageSource> CompletePackage()
{
    std::vector<JBeamPackageSource> sources;
    sources.push_back(Package(
        "vehicles/test/main.jbeam",
        "{\n"
        "\"car\":{\n"
        " \"slotType\":\"main\",\n"
        " \"unknown\":1,\n"
        " \"unknown\":2,\n"
        " \"slots\":[\n"
        "  [\"type\",\"default\",\"description\"],\n"
        "  [\"engine\",\"engine_i4\",\"Engine\","
        "{\"coreSlot\":true,\"variables\":{"
        "\"$prefix\":\"front\",\"$boost\":1}}],\n"
        "  [\"spoiler\",\"\",\"Spoiler\"]\n"
        " ]\n"
        "}\n"
        "}"));
    sources.push_back(Package(
        "vehicles/test/engines.jbeam",
        "{\n"
        "\"engine_i4\":{\n"
        " \"slotType\":\"engine\",\n"
        " \"slots2\":[\n"
        "  [\"name\",\"allowTypes\",\"denyTypes\",\"default\","
        "\"description\"],\n"
        "  [\"turbo\",[\"turbo\"],[],\"turbo_small\",\"Turbo\","
        "{\"variables\":{\"$boost\":2,\"$flag\":true}}]\n"
        " ]\n"
        "},\n"
        "\"engine_v8\":{\"slotType\":\"engine\"}\n"
        "}"));
    sources.push_back(Package(
        "vehicles/test/turbo.jbeam",
        "{\"turbo_small\":{\"slotType\":[\"turbo\",\"alternate\"]}}"));
    return sources;
}

JBeamConfigurationResult StandardConfiguration()
{
    return RoR::BeamNG::ParseJBeamConfiguration(Value(
        "{"
        "\"parts\":{\"engine\":\"engine_i4\"},"
        "\"vars\":{\"$boost\":0.5,\"$mode\":\"$='sport'\"}"
        "}",
        "vehicles/test/sport.pc"));
}

void TestCanonicalIndexAndRecursiveResolution()
{
    const std::vector<JBeamPackageSource> sources = CompletePackage();
    const JBeamConfigurationResult configuration =
        StandardConfiguration();
    CHECK(configuration.IsValid());

    std::vector<std::size_t> permutation;
    permutation.push_back(0);
    permutation.push_back(1);
    permutation.push_back(2);
    std::string expected_index;
    std::string expected_graph;
    do
    {
        std::vector<JBeamPackageSource> ordered;
        for (std::size_t i = 0; i < permutation.size(); ++i)
        {
            ordered.push_back(sources[permutation[i]]);
        }
        const JBeamPackageIndex index =
            RoR::BeamNG::BuildJBeamPackageIndex(ordered);
        CHECK(index.IsValid());
        CHECK(index.parts.size() == 4);
        const std::string serialized_index =
            RoR::BeamNG::SerializeCanonicalJBeamPackageIndex(index);

        const JBeamResolvedGraph graph =
            RoR::BeamNG::ResolveJBeamPartGraph(
                index, configuration.request);
        CHECK(graph.IsValid());
        CHECK(graph.resolved_part_count == 3);
        CHECK(graph.root != NULL);
        const std::string serialized_graph =
            RoR::BeamNG::SerializeCanonicalJBeamResolvedGraph(graph);
        if (expected_index.empty())
        {
            expected_index = serialized_index;
            expected_graph = serialized_graph;
        }
        else
        {
            CHECK(serialized_index == expected_index);
            CHECK(serialized_graph == expected_graph);
        }
    }
    while (std::next_permutation(
        permutation.begin(), permutation.end()));

    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(sources);
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, configuration.request);
    CHECK(graph.root->definition.name == "car");
    const JBeamResolvedSlot* engine =
        FindSlot(*graph.root, "engine");
    const JBeamResolvedSlot* spoiler =
        FindSlot(*graph.root, "spoiler");
    CHECK(engine != NULL);
    CHECK(engine->explicitly_selected);
    CHECK(engine->status == JBeamResolvedSlotStatus::RESOLVED);
    CHECK(engine->child != NULL);
    CHECK(engine->child->definition.name == "engine_i4");
    CHECK(spoiler != NULL);
    CHECK(spoiler->status == JBeamResolvedSlotStatus::EMPTY);
    CHECK(!spoiler->child);

    const JBeamResolvedSlot* turbo =
        FindSlot(*engine->child, "turbo");
    CHECK(turbo != NULL);
    CHECK(!turbo->explicitly_selected);
    CHECK(turbo->status == JBeamResolvedSlotStatus::RESOLVED);
    CHECK(turbo->child != NULL);
    CHECK(turbo->child->definition.name == "turbo_small");
    CHECK(engine->child->inherited_variables.size() == 4);
    CHECK(turbo->child->inherited_variables.size() == 6);

    const JBeamVariableAssignment* boost =
        RoR::BeamNG::FindEffectiveJBeamVariable(
            *turbo->child, "$boost");
    const JBeamVariableAssignment* mode =
        RoR::BeamNG::FindEffectiveJBeamVariable(
            *turbo->child, "$mode");
    const JBeamVariableAssignment* flag =
        RoR::BeamNG::FindEffectiveJBeamVariable(
            *turbo->child, "$flag");
    CHECK(boost != NULL);
    CHECK(boost->value.type == JBeamValueType::NUMBER);
    CHECK(boost->value.number_value == 2.0);
    CHECK(mode != NULL);
    CHECK(mode->value.type == JBeamValueType::STRING);
    CHECK(mode->value.scalar_text == "$='sport'");
    CHECK(flag != NULL);
    CHECK(flag->value.type == JBeamValueType::BOOLEAN);
    CHECK(flag->value.boolean_value);

    const std::string serialized =
        RoR::BeamNG::SerializeCanonicalJBeamResolvedGraph(graph);
    CHECK(CountSubstring(serialized, "k7:unknown") == 2);
}

void TestExplicitSelectionsAndUnusedSelection()
{
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(CompletePackage());
    JBeamConfigurationResult configuration =
        RoR::BeamNG::ParseJBeamConfiguration(Value(
            "{"
            "\"parts\":{"
            "\"engine\":\"engine_v8\","
            "\"spoiler\":\"\","
            "\"not_a_slot\":\"ghost\""
            "},"
            "\"vars\":{}"
            "}",
            "vehicles/test/v8.pc"));
    CHECK(configuration.IsValid());
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, configuration.request);
    CHECK(graph.IsValid());
    CHECK(graph.resolved_part_count == 2);
    const JBeamResolvedSlot* engine =
        FindSlot(*graph.root, "engine");
    const JBeamResolvedSlot* spoiler =
        FindSlot(*graph.root, "spoiler");
    CHECK(engine != NULL && engine->child != NULL);
    CHECK(engine->child->definition.name == "engine_v8");
    CHECK(spoiler != NULL && spoiler->explicitly_selected);
    CHECK(spoiler->status == JBeamResolvedSlotStatus::EMPTY);
    CHECK(
        FindDiagnostic(
            graph.diagnostics,
            JBeamResolveDiagnosticCode::UNUSED_PART_SELECTION) != NULL);
}

void TestConfigurationDuplicatesRemainOrdered()
{
    const JBeamConfigurationResult configuration =
        RoR::BeamNG::ParseJBeamConfiguration(Value(
            "{"
            "\"parts\":{"
            "\"engine\":\"engine_v8\","
            "\"engine\":\"engine_i4\""
            "},"
            "\"vars\":{"
            "\"$boost\":0,"
            "\"$boost\":0.5,"
            "\"$flag\":true"
            "}"
            "}",
            "vehicles/test/duplicate.pc"));
    CHECK(configuration.IsValid());
    CHECK(configuration.request.part_selections.size() == 2);
    CHECK(configuration.request.variables.size() == 3);
    CHECK(
        FindDiagnostic(
            configuration.diagnostics,
            JBeamResolveDiagnosticCode::DUPLICATE_PART_SELECTION) !=
        NULL);
    CHECK(
        FindDiagnostic(
            configuration.diagnostics,
            JBeamResolveDiagnosticCode::
                DUPLICATE_CONFIGURATION_VARIABLE) != NULL);

    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(CompletePackage());
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, configuration.request);
    CHECK(graph.IsValid());
    const JBeamResolvedSlot* engine =
        FindSlot(*graph.root, "engine");
    CHECK(engine != NULL && engine->child != NULL);
    CHECK(engine->child->definition.name == "engine_i4");
    CHECK(graph.root->inherited_variables.size() == 3);
    CHECK(
        FindDiagnostic(
            graph.diagnostics,
            JBeamResolveDiagnosticCode::UNUSED_PART_SELECTION) == NULL);
    const JBeamVariableAssignment* root_boost =
        RoR::BeamNG::FindEffectiveJBeamVariable(
            *graph.root, "$boost");
    CHECK(root_boost != NULL);
    CHECK(root_boost->value.number_value == 0.5);
    const std::string serialized =
        RoR::BeamNG::SerializeCanonicalJBeamResolvedGraph(graph);
    CHECK(
        serialized.find(
            "request-selections\t2\n"
            "Q\t6:engine\t9:engine_v8\t") != std::string::npos);
    CHECK(
        serialized.find("Q\t6:engine\t9:engine_i4\t") !=
        std::string::npos);
}

void TestRequiredAndOptionalMissingParts()
{
    std::vector<JBeamPackageSource> sources;
    sources.push_back(Package(
        "vehicles/missing/main.jbeam",
        "{"
        "\"main\":{"
        "\"slotType\":\"main\","
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"required\",\"missing_required\",\"Required\","
        "{\"coreSlot\":true}],"
        "[\"optional\",\"missing_optional\",\"Optional\"],"
        "[\"empty\",\"\",\"Empty\"]"
        "]"
        "}"
        "}"));
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(sources);
    CHECK(index.IsValid());
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(index);
    CHECK(!graph.IsValid());
    CHECK(graph.root != NULL);
    CHECK(
        FindDiagnostic(
            graph.diagnostics,
            JBeamResolveDiagnosticCode::MISSING_REQUIRED_PART) != NULL);
    CHECK(
        FindDiagnostic(
            graph.diagnostics,
            JBeamResolveDiagnosticCode::MISSING_OPTIONAL_PART) != NULL);
    CHECK(
        FindSlot(*graph.root, "required")->status ==
        JBeamResolvedSlotStatus::MISSING);
    CHECK(
        FindSlot(*graph.root, "optional")->status ==
        JBeamResolvedSlotStatus::MISSING);
    CHECK(
        FindSlot(*graph.root, "empty")->status ==
        JBeamResolvedSlotStatus::EMPTY);
}

void TestSlots2DenyTakesPriority()
{
    std::vector<JBeamPackageSource> sources;
    sources.push_back(Package(
        "vehicles/deny/main.jbeam",
        "{"
        "\"main\":{"
        "\"slotType\":\"main\","
        "\"slots2\":["
        "[\"name\",\"allowTypes\",\"denyTypes\",\"default\","
        "\"description\"],"
        "[\"child\",[\"allowed\"],[\"denied\"],\"mixed\",\"Child\","
        "{\"coreSlot\":true}]"
        "]"
        "},"
        "\"mixed\":{\"slotType\":[\"allowed\",\"denied\"]}"
        "}"));
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(sources);
    CHECK(index.IsValid());
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(index);
    CHECK(!graph.IsValid());
    const JBeamResolvedSlot* child =
        FindSlot(*graph.root, "child");
    CHECK(child != NULL);
    CHECK(child->status == JBeamResolvedSlotStatus::NOT_ALLOWED);
    CHECK(
        FindDiagnostic(
            graph.diagnostics,
            JBeamResolveDiagnosticCode::PART_NOT_ALLOWED_IN_SLOT) !=
        NULL);
}

std::vector<JBeamPackageSource> CyclicPackage()
{
    std::vector<JBeamPackageSource> sources;
    sources.push_back(Package(
        "vehicles/cycle/cycle.jbeam",
        "{"
        "\"a\":{"
        "\"slotType\":[\"main\",\"a_type\"],"
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"b_type\",\"b\",\"B\",{\"coreSlot\":true}]"
        "]"
        "},"
        "\"b\":{"
        "\"slotType\":\"b_type\","
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"a_type\",\"a\",\"A\",{\"coreSlot\":true}]"
        "]"
        "}"
        "}"));
    return sources;
}

void TestCyclesDepthAndPartLimits()
{
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(CyclicPackage());
    CHECK(index.IsValid());

    const JBeamResolvedGraph cycle =
        RoR::BeamNG::ResolveJBeamPartGraph(index);
    CHECK(!cycle.IsValid());
    CHECK(cycle.resolved_part_count == 2);
    CHECK(
        FindDiagnostic(
            cycle.diagnostics,
            JBeamResolveDiagnosticCode::SLOT_CYCLE) != NULL);
    const JBeamResolvedSlot* b =
        FindSlot(*cycle.root, "b_type");
    CHECK(b != NULL && b->child != NULL);
    const JBeamResolvedSlot* a =
        FindSlot(*b->child, "a_type");
    CHECK(a != NULL);
    CHECK(a->status == JBeamResolvedSlotStatus::CYCLE);

    JBeamResolverLimits limits;
    limits.max_depth = 0;
    const JBeamResolvedGraph depth =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, RoR::BeamNG::JBeamResolveRequest(), limits);
    CHECK(
        FindDiagnostic(
            depth.diagnostics,
            JBeamResolveDiagnosticCode::RESOLVE_DEPTH_LIMIT) != NULL);
    CHECK(depth.resolved_part_count == 1);

    limits = JBeamResolverLimits();
    limits.max_resolved_parts = 1;
    const JBeamResolvedGraph count =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, RoR::BeamNG::JBeamResolveRequest(), limits);
    CHECK(
        FindDiagnostic(
            count.diagnostics,
            JBeamResolveDiagnosticCode::RESOLVED_PART_LIMIT) != NULL);
    CHECK(count.resolved_part_count == 1);
}

void TestDuplicatePartsAndSlotsAreRejected()
{
    std::vector<JBeamPackageSource> duplicate_parts;
    duplicate_parts.push_back(Package(
        "vehicles/duplicate/a.jbeam",
        "{\"same\":{\"slotType\":\"main\"}}"));
    duplicate_parts.push_back(Package(
        "vehicles/duplicate/b.jbeam",
        "{\"same\":{\"slotType\":\"other\"}}"));
    const JBeamPackageIndex part_index =
        RoR::BeamNG::BuildJBeamPackageIndex(duplicate_parts);
    CHECK(!part_index.IsValid());
    CHECK(part_index.parts.size() == 2);
    CHECK(
        FindDiagnostic(
            part_index.diagnostics,
            JBeamResolveDiagnosticCode::DUPLICATE_PART) != NULL);
    const JBeamResolvedGraph blocked =
        RoR::BeamNG::ResolveJBeamPartGraph(part_index);
    CHECK(!blocked.IsValid());
    CHECK(!blocked.root);
    CHECK(
        FindDiagnostic(
            blocked.diagnostics,
            JBeamResolveDiagnosticCode::INDEX_INVALID) != NULL);

    std::vector<JBeamPackageSource> duplicate_slots;
    duplicate_slots.push_back(Package(
        "vehicles/duplicate/slots.jbeam",
        "{"
        "\"main\":{"
        "\"slotType\":\"main\","
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"same\",\"\",\"First\"],"
        "[\"same\",\"\",\"Second\"]"
        "]"
        "}"
        "}"));
    const JBeamPackageIndex slot_index =
        RoR::BeamNG::BuildJBeamPackageIndex(duplicate_slots);
    CHECK(!slot_index.IsValid());
    CHECK(
        FindDiagnostic(
            slot_index.diagnostics,
            JBeamResolveDiagnosticCode::DUPLICATE_SLOT) != NULL);
}

void TestMultipleMainPartsRequireExplicitRoot()
{
    std::vector<JBeamPackageSource> sources;
    sources.push_back(Package(
        "vehicles/multi/main.jbeam",
        "{"
        "\"car_a\":{\"slotType\":\"main\"},"
        "\"car_b\":{\"slotType\":\"main\"}"
        "}"));
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(sources);
    CHECK(index.IsValid());

    const JBeamResolvedGraph ambiguous =
        RoR::BeamNG::ResolveJBeamPartGraph(index);
    CHECK(!ambiguous.IsValid());
    CHECK(!ambiguous.root);
    CHECK(
        FindDiagnostic(
            ambiguous.diagnostics,
            JBeamResolveDiagnosticCode::MULTIPLE_MAIN_PARTS) != NULL);

    RoR::BeamNG::JBeamResolveRequest request;
    request.root_part_name = "car_b";
    const JBeamResolvedGraph selected =
        RoR::BeamNG::ResolveJBeamPartGraph(index, request);
    CHECK(selected.IsValid());
    CHECK(selected.root != NULL);
    CHECK(selected.root->definition.name == "car_b");
}

void TestSlotDescriptionIsRequired()
{
    std::vector<JBeamPackageSource> sources;
    sources.push_back(Package(
        "vehicles/malformed/missing-description.jbeam",
        "{"
        "\"main\":{"
        "\"slotType\":\"main\","
        "\"slots\":["
        "[\"type\",\"default\"],"
        "[\"engine\",\"\"]"
        "]"
        "}"
        "}"));
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(sources);
    CHECK(!index.IsValid());
    CHECK(
        FindDiagnostic(
            index.diagnostics,
            JBeamResolveDiagnosticCode::MISSING_SLOT_FIELD) != NULL);
}

void TestIndexAndVariableLimits()
{
    const std::vector<JBeamPackageSource> sources = CompletePackage();
    JBeamResolverLimits limits;
    limits.max_indexed_parts = 2;
    const JBeamPackageIndex limited =
        RoR::BeamNG::BuildJBeamPackageIndex(sources, limits);
    CHECK(!limited.IsValid());
    CHECK(limited.parts.size() == 2);
    CHECK(
        FindDiagnostic(
            limited.diagnostics,
            JBeamResolveDiagnosticCode::INDEX_PART_LIMIT) != NULL);

    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(sources);
    const JBeamConfigurationResult configuration =
        StandardConfiguration();
    limits = JBeamResolverLimits();
    limits.max_variables_per_node = 3;
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, configuration.request, limits);
    CHECK(!graph.IsValid());
    CHECK(graph.root != NULL);
    const JBeamResolvedSlot* engine =
        FindSlot(*graph.root, "engine");
    CHECK(engine != NULL);
    CHECK(engine->status == JBeamResolvedSlotStatus::LIMIT_REJECTED);
    CHECK(
        FindDiagnostic(
            graph.diagnostics,
            JBeamResolveDiagnosticCode::RESOLVED_VARIABLE_LIMIT) !=
        NULL);
}

void TestMalformedConfigurationAndStructuralExpressions()
{
    const JBeamConfigurationResult malformed =
        RoR::BeamNG::ParseJBeamConfiguration(Value(
            "{\"parts\":[],\"vars\":{\"bad\":{},\"$alsoBad\":[]}}",
            "vehicles/test/bad.pc"));
    CHECK(!malformed.IsValid());
    CHECK(
        FindDiagnostic(
            malformed.diagnostics,
            JBeamResolveDiagnosticCode::
                INVALID_CONFIGURATION_SECTION) != NULL);
    CHECK(
        FindDiagnostic(
            malformed.diagnostics,
            JBeamResolveDiagnosticCode::
                INVALID_CONFIGURATION_VARIABLE) != NULL);

    std::vector<JBeamPackageSource> expression;
    expression.push_back(Package(
        "vehicles/expression/main.jbeam",
        "{\"main\":{\"slotType\":\"$=$type\"}}"));
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(expression);
    CHECK(!index.IsValid());
    CHECK(
        FindDiagnostic(
            index.diagnostics,
            JBeamResolveDiagnosticCode::INVALID_SLOT_TYPE) != NULL);

    std::vector<JBeamPackageSource> malformed_slots;
    malformed_slots.push_back(Package(
        "vehicles/expression/malformed.jbeam",
        "{"
        "\"main\":{"
        "\"slotType\":\"main\","
        "\"slots\":["
        "[\"type\",\"default\",\"default\"],"
        "[\"short\"]"
        "]"
        "}"
        "}"));
    const JBeamPackageIndex malformed_slot_index =
        RoR::BeamNG::BuildJBeamPackageIndex(malformed_slots);
    CHECK(!malformed_slot_index.IsValid());
    CHECK(
        FindDiagnostic(
            malformed_slot_index.diagnostics,
            JBeamResolveDiagnosticCode::INVALID_SLOT_TABLE) != NULL);
    CHECK(
        FindDiagnostic(
            malformed_slot_index.diagnostics,
            JBeamResolveDiagnosticCode::INVALID_SLOT_ROW) != NULL);
}

void TestInputEntryLimitCountsMalformedEntries()
{
    std::vector<JBeamPackageSource> sources;
    sources.push_back(Package(
        "vehicles/limits/input.jbeam",
        "{"
        "\"bad_number\":1,"
        "\"bad_array\":[],"
        "\"main\":{\"slotType\":\"main\"}"
        "}"));

    JBeamResolverLimits limits;
    limits.max_input_entries = 2U;
    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(sources, limits);
    CHECK(!index.IsValid());
    CHECK(index.parts.empty());
    CHECK(
        CountDiagnostic(
            index.diagnostics,
            JBeamResolveDiagnosticCode::PART_NOT_OBJECT) == 2U);
    CHECK(
        CountDiagnostic(
            index.diagnostics,
            JBeamResolveDiagnosticCode::
                INDEX_INPUT_ENTRY_LIMIT) == 1U);

    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(index);
    CHECK(!graph.IsValid());
    CHECK(!graph.root);
    CHECK(
        FindDiagnostic(
            graph.diagnostics,
            JBeamResolveDiagnosticCode::INDEX_INVALID) != NULL);

    sources.clear();
    sources.push_back(Package(
        "vehicles/limits/part-count.jbeam",
        "{"
        "\"first\":{\"slotType\":\"main\"},"
        "\"still_malformed\":false,"
        "\"second\":{\"slotType\":\"main\"}"
        "}"));
    limits = JBeamResolverLimits();
    limits.max_indexed_parts = 1U;
    const JBeamPackageIndex part_limited =
        RoR::BeamNG::BuildJBeamPackageIndex(sources, limits);
    CHECK(!part_limited.IsValid());
    CHECK(part_limited.parts.size() == 1U);
    CHECK(
        CountDiagnostic(
            part_limited.diagnostics,
            JBeamResolveDiagnosticCode::PART_NOT_OBJECT) == 1U);
    CHECK(
        CountDiagnostic(
            part_limited.diagnostics,
            JBeamResolveDiagnosticCode::INDEX_PART_LIMIT) == 1U);
}

void TestConfigurationAndRequestLimitsAreAtomic()
{
    JBeamResolverLimits limits;
    limits.max_request_selections = 2U;
    limits.max_request_variables = 2U;
    const JBeamConfigurationResult configuration =
        RoR::BeamNG::ParseJBeamConfiguration(
            Value(
                "{"
                "\"parts\":{"
                "\"invalid\":{},"
                "\"engine\":\"engine_v8\","
                "\"spoiler\":\"\""
                "},"
                "\"vars\":{"
                "\"invalid\":{},"
                "\"$b\":2,"
                "\"$c\":3"
                "}"
                "}",
                "vehicles/limits/too-many.pc"),
            limits);
    CHECK(!configuration.IsValid());
    CHECK(configuration.request.part_selections.empty());
    CHECK(configuration.request.variables.empty());
    CHECK(
        FindDiagnostic(
            configuration.diagnostics,
            JBeamResolveDiagnosticCode::PART_SELECTION_LIMIT) != NULL);
    CHECK(
        FindDiagnostic(
            configuration.diagnostics,
            JBeamResolveDiagnosticCode::REQUEST_VARIABLE_LIMIT) != NULL);

    const JBeamPackageIndex index =
        RoR::BeamNG::BuildJBeamPackageIndex(CompletePackage());
    CHECK(index.IsValid());

    const JBeamConfigurationResult standard =
        StandardConfiguration();
    limits = JBeamResolverLimits();
    limits.max_request_selections = 0U;
    const JBeamResolvedGraph selection_limited =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, standard.request, limits);
    CHECK(!selection_limited.IsValid());
    CHECK(!selection_limited.root);
    CHECK(selection_limited.request.part_selections.empty());
    CHECK(selection_limited.request.variables.empty());
    CHECK(
        FindDiagnostic(
            selection_limited.diagnostics,
            JBeamResolveDiagnosticCode::PART_SELECTION_LIMIT) != NULL);

    limits = JBeamResolverLimits();
    limits.max_request_variables = 1U;
    const JBeamResolvedGraph variable_limited =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, standard.request, limits);
    CHECK(!variable_limited.IsValid());
    CHECK(!variable_limited.root);
    CHECK(variable_limited.request.part_selections.empty());
    CHECK(variable_limited.request.variables.empty());
    CHECK(
        FindDiagnostic(
            variable_limited.diagnostics,
            JBeamResolveDiagnosticCode::REQUEST_VARIABLE_LIMIT) != NULL);

    limits = JBeamResolverLimits();
    limits.max_variables_per_node = 0U;
    const JBeamResolvedGraph child_variable_limited =
        RoR::BeamNG::ResolveJBeamPartGraph(
            index, RoR::BeamNG::JBeamResolveRequest(), limits);
    CHECK(!child_variable_limited.IsValid());
    CHECK(child_variable_limited.root != NULL);
    const JBeamResolvedSlot* engine =
        FindSlot(*child_variable_limited.root, "engine");
    CHECK(engine != NULL);
    CHECK(!engine->child);
    CHECK(
        engine->status ==
        JBeamResolvedSlotStatus::LIMIT_REJECTED);
    CHECK(
        FindDiagnostic(
            child_variable_limited.diagnostics,
            JBeamResolveDiagnosticCode::RESOLVED_VARIABLE_LIMIT) != NULL);
}

void TestDiagnosticLimitIsTerminalAndDeterministic()
{
    std::vector<JBeamPackageSource> sources;
    sources.push_back(Package(
        "vehicles/limits/b.jbeam",
        "{\"b0\":0,\"b1\":1,\"b2\":2}"));
    sources.push_back(Package(
        "vehicles/limits/a.jbeam",
        "{\"a0\":0,\"a1\":1,\"a2\":2}"));

    JBeamResolverLimits limits;
    limits.max_diagnostics = 3U;
    const JBeamPackageIndex forward =
        RoR::BeamNG::BuildJBeamPackageIndex(sources, limits);
    std::reverse(sources.begin(), sources.end());
    const JBeamPackageIndex reversed =
        RoR::BeamNG::BuildJBeamPackageIndex(sources, limits);
    CHECK(!forward.IsValid());
    CHECK(!reversed.IsValid());
    CHECK(forward.diagnostics.size() == 3U);
    CHECK(reversed.diagnostics.size() == 3U);
    CHECK(
        CountDiagnostic(
            forward.diagnostics,
            JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT) == 1U);
    CHECK(
        forward.diagnostics.back().code ==
        JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT);
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamPackageIndex(forward) ==
        RoR::BeamNG::SerializeCanonicalJBeamPackageIndex(reversed));

    limits.max_diagnostics = 0U;
    const JBeamPackageIndex zero_limit =
        RoR::BeamNG::BuildJBeamPackageIndex(sources, limits);
    CHECK(!zero_limit.IsValid());
    CHECK(zero_limit.diagnostics.size() == 1U);
    CHECK(
        zero_limit.diagnostics[0].code ==
        JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT);

    limits = JBeamResolverLimits();
    limits.max_diagnostics = 2U;
    const JBeamConfigurationResult configuration =
        RoR::BeamNG::ParseJBeamConfiguration(
            Value(
                "{"
                "\"parts\":{"
                "\"kept\":\"\","
                "\"bad0\":{},"
                "\"bad1\":[],"
                "\"bad2\":false"
                "}"
                "}",
                "vehicles/limits/diagnostics.pc"),
            limits);
    CHECK(!configuration.IsValid());
    CHECK(configuration.diagnostics.size() == 2U);
    CHECK(
        configuration.diagnostics.back().code ==
        JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT);
    CHECK(configuration.request.part_selections.empty());
    CHECK(configuration.request.variables.empty());

    RoR::BeamNG::JBeamResolveRequest request;
    for (std::size_t i = 0U; i < 4U; ++i)
    {
        RoR::BeamNG::JBeamPartSelection selection;
        selection.slot_name =
            std::string("unused") +
            static_cast<char>('0' + static_cast<int>(i));
        selection.part_name = std::string();
        request.part_selections.push_back(selection);
    }
    const JBeamPackageIndex valid_index =
        RoR::BeamNG::BuildJBeamPackageIndex(CompletePackage());
    const JBeamResolvedGraph graph =
        RoR::BeamNG::ResolveJBeamPartGraph(
            valid_index, request, limits);
    CHECK(!graph.IsValid());
    CHECK(graph.diagnostics.size() == 2U);
    CHECK(
        CountDiagnostic(
            graph.diagnostics,
            JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT) == 1U);
    CHECK(
        graph.diagnostics.back().code ==
        JBeamResolveDiagnosticCode::DIAGNOSTIC_LIMIT);
}

} // namespace

int main()
{
    TestCanonicalIndexAndRecursiveResolution();
    TestExplicitSelectionsAndUnusedSelection();
    TestConfigurationDuplicatesRemainOrdered();
    TestRequiredAndOptionalMissingParts();
    TestSlots2DenyTakesPriority();
    TestCyclesDepthAndPartLimits();
    TestDuplicatePartsAndSlotsAreRejected();
    TestMultipleMainPartsRequireExplicitRoot();
    TestSlotDescriptionIsRequired();
    TestIndexAndVariableLimits();
    TestMalformedConfigurationAndStructuralExpressions();
    TestInputEntryLimitCountsMalformedEntries();
    TestConfigurationAndRequestLimitsAreAtomic();
    TestDiagnosticLimitIsTerminalAndDeterministic();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " JBeam part resolver test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JBeam part resolver tests passed\n";
    return EXIT_SUCCESS;
}
