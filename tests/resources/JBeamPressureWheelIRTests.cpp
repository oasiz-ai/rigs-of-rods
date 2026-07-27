#include "JBeamPressureWheelIR.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
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
        std::cerr << "line " << line
                  << ": check failed: "
                  << expression << '\n';
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

using RoR::BeamNG::JBeamObjectField;
using RoR::BeamNG::JBeamPackageIndex;
using RoR::BeamNG::JBeamPackageSource;
using RoR::BeamNG::JBeamParseResult;
using RoR::BeamNG::JBeamPressureWheel;
using RoR::BeamNG::JBeamPressureWheelAdmission;
using RoR::BeamNG::JBeamPressureWheelDiagnosticCode;
using RoR::BeamNG::JBeamPressureWheelField;
using RoR::BeamNG::JBeamPressureWheelFieldOrigin;
using RoR::BeamNG::JBeamPressureWheelIR;
using RoR::BeamNG::JBeamPressureWheelLimits;
using RoR::BeamNG::JBeamPressureWheelRuntimePolicy;
using RoR::BeamNG::JBeamPressureWheelSourceKind;
using RoR::BeamNG::JBeamResolvedGraph;
using RoR::BeamNG::JBeamResolvedSlot;
using RoR::BeamNG::JBeamValue;
using RoR::BeamNG::JBeamValueType;

JBeamPackageSource Package(
    const std::string& path,
    const std::string& source)
{
    const JBeamParseResult parsed =
        RoR::BeamNG::ParseJBeam(source, path);
    CHECK(parsed.IsValid());
    JBeamPackageSource result;
    result.package_path = path;
    result.document = parsed.root;
    return result;
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

JBeamResolvedGraph ResolveSingle(const std::string& body)
{
    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/wheels/main.jbeam",
        std::string(
            "{\"pressure_test\":{\"slotType\":\"main\",") +
            body + "}}"));
    return Resolve(packages);
}

std::string RequiredHeader()
{
    return
        "[\"name\",\"hubGroup\",\"group\","
        "\"node1:\",\"node2:\",\"nodeS\","
        "\"nodeArm:\",\"wheelDir\"]";
}

std::string RequiredDefaults(
    const std::string& extra = std::string())
{
    return
        "{\"radius\":0.35,\"hubRadius\":0.2,"
        "\"wheelOffset\":-0.075,\"tireWidth\":0.22,"
        "\"hubWidth\":0.18,\"hasTire\":true,"
        "\"numRays\":16" + extra + "}";
}

std::string WheelRow(
    const std::string& name,
    const std::string& node_s = "9999",
    const std::string& direction = "-1")
{
    return
        "[\"" + name + "\",\"hub_" + name +
        "\",\"tire_" + name +
        "\",\"n1_" + name +
        "\",\"n2_" + name +
        "\"," + node_s + ",\"arm_" + name +
        "\"," + direction + "]";
}

std::string ValidPressureSection(
    const std::string& extra_defaults = std::string(),
    const std::string& extra_entries = std::string())
{
    return
        "\"pressureWheels\":[" +
        RequiredHeader() + "," +
        RequiredDefaults(extra_defaults) + "," +
        WheelRow("FL") + extra_entries + "]";
}

std::size_t CountDiagnostic(
    const JBeamPressureWheelIR& ir,
    JBeamPressureWheelDiagnosticCode code)
{
    std::size_t count = 0U;
    for (std::size_t i = 0U;
         i < ir.diagnostics.size();
         ++i)
    {
        if (ir.diagnostics[i].code == code)
        {
            ++count;
        }
    }
    return count;
}

std::size_t CountDiagnosticForField(
    const JBeamPressureWheelIR& ir,
    JBeamPressureWheelDiagnosticCode code,
    const std::string& field_name)
{
    std::size_t count = 0U;
    for (std::size_t i = 0U;
         i < ir.diagnostics.size();
         ++i)
    {
        if (ir.diagnostics[i].code == code &&
            ir.diagnostics[i].field_name == field_name)
        {
            ++count;
        }
    }
    return count;
}

const JBeamPressureWheelField* FindField(
    const JBeamPressureWheel& wheel,
    const std::string& name)
{
    for (std::size_t i = 0U;
         i < wheel.effective_fields.size();
         ++i)
    {
        if (wheel.effective_fields[i].name == name)
        {
            return &wheel.effective_fields[i];
        }
    }
    return NULL;
}

JBeamObjectField* MutableLastField(
    JBeamValue& object,
    const std::string& name)
{
    for (std::size_t i = object.object_fields.size();
         i > 0U;
         --i)
    {
        if (object.object_fields[i - 1U].key == name)
        {
            return &object.object_fields[i - 1U];
        }
    }
    return NULL;
}

double DoubleFromBits(std::uint64_t bits)
{
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void TestDocumentationProvenanceAndLocalPolicy()
{
    const RoR::BeamNG::
        JBeamPressureWheelDocumentationProfile& profile =
            RoR::BeamNG::
                GetJBeamPressureWheelDocumentationProfile();
    CHECK(profile.profile_id ==
        "beamng-docs-0.38.5.0-2026-07-27");
    CHECK(profile.beamng_version == "0.38.5.0");
    CHECK(profile.wheel_documentation_url ==
        "https://documentation.beamng.com/modding/vehicle/"
        "sections/wheels/");
    CHECK(profile.wheel_documentation_last_modified ==
        "2026-06-09");
    CHECK(profile.jbeam_syntax_documentation_url.find(
        "intro_jbeam/jbeamsyntax/") != std::string::npos);
    CHECK(profile.node_documentation_url.find(
        "sections/nodes/") != std::string::npos);
    CHECK(profile.beam_documentation_url.find(
        "sections/beams/") != std::string::npos);
    CHECK(profile.vehicle_controller_documentation_url.find(
        "vehiclecontroller/") != std::string::npos);
    CHECK(!profile.manual_shift_logic_documentation_url.empty());
    CHECK(!profile.sequential_shift_logic_documentation_url.empty());
    CHECK(!profile.dct_shift_logic_documentation_url.empty());
    CHECK(profile.recommended_minimum_num_rays == 10U);
    CHECK(profile.recommended_maximum_num_rays == 20U);
    CHECK(!profile.pressure_wheel_count_has_documented_maximum);
    CHECK(profile.future_ror_lowering_maximum_wheels == 64U);

    const JBeamPressureWheelLimits limits;
    CHECK(limits.max_wheels > 64U);
}

void TestCurrentOfficialWheelFieldClassification()
{
    // Exact concrete names are sampled across every parameter group in the
    // official pressureWheels page. Beam wildcard families intentionally use
    // their per-family suffix sets rather than one permissive shared set.
    static const char* const documented_fields[] = {
        "speedo",
        "nodeCoupling",
        "torqueCoupling:",
        "torqueArm:",
        "torqueArm2:",
        "steerAxisUp:",
        "steerAxisDown:",
        "torqueJointNode1:",
        "torqueJointNode2:",
        "axleBeams",
        "disableMeshBreaking",
        "disableHubMeshBreaking",
        "propulsed",
        "selfCollision",
        "collision",
        "offsetFromNode",
        "nodeWeight",
        "hubNodeWeight",
        "hubWeight",
        "tireWeight",
        "hubWeightGainRatio",
        "hubFrictionCoef",
        "hubNodeMaterial",
        "frictionCoef",
        "slidingFrictionCoef",
        "stribeckVelMult",
        "stribeckExponent",
        "treadCoef",
        "noLoadCoef",
        "loadSensitivitySlope",
        "fullLoadCoef",
        "nodeMaterial",
        "softnessCoef",
        "enableTireReinfBeams",
        "enableTireLbeams",
        "enableTireSideReinfBeams",
        "enableTreadReinfBeams",
        "enableTirePeripheryReinfBeams",
        "enableTireSupportBeams",
        "hubBeamSpring",
        "hubTreadBeamDamp",
        "hubPeripheryBeamDeform",
        "hubSideBeamStrength",
        "hubReinfBeamDeform",
        "wheelSideBeamDampCutoffHz",
        "wheelSideBeamPrecompression",
        "wheelSideBeamSpringExpansion",
        "wheelSideBeamDampExpansion",
        "wheelSideReinfBeamDampCutoffHz",
        "wheelSideReinfBeamSpringExpansion",
        "wheelSideReinfBeamDampExpansion",
        "wheelReinfBeamPrecompression",
        "wheelTreadBeamPrecompression",
        "wheelTreadReinfBeamPrecompression",
        "wheelPeripheryBeamSpring",
        "wheelPeripheryBeamDampCutoffHz",
        "wheelPeripheryReinfBeamPrecompression",
        "tireSupportBeamDeform",
        "tireSupportBeamSidewallRatio",
        "tireSupportBeamLongExtent",
        "wheelSideTransitionZone",
        "pressurePSI",
        "maxPressurePSI",
        "triangleCollision",
        "treadTriangleCollision",
        "side1TriangleCollision",
        "side2TriangleCollision",
        "hubTriangleCollision",
        "hubSide1TriangleCollision",
        "hubSide2TriangleCollision",
        "disableTriangleBreaking",
        "dragCoef",
        "skinDragCoef",
        "enableHubcaps",
        "hubcapBreakGroup",
        "hubcapGroup",
        "hubcapCollision",
        "hubcapSelfCollision",
        "enableExtraHubcapBeams",
        "hubcapOffset",
        "hubcapWidth",
        "hubcapRadius",
        "hubcapBeamSpring",
        "hubcapAttachBeamDamp",
        "hubcapSupportBeamDeform",
        "hubcapNodeWeight",
        "hubcapCenterNodeWeight",
        "hubcapNodeMaterial",
        "hubcapFrictionCoef",
        "brakeTorque",
        "parkingTorque",
        "brakeSpring",
        "enableBrakeThermals",
        "brakeDiameter",
        "brakeMass",
        "brakeType",
        "rotorMaterial",
        "brakeVentingCoef",
        "padMaterial",
        "brakeInputSplit",
        "brakeSplitCoef",
        "squealCoefNatural",
        "squealCoefLowSpeed",
        "squealCoefGlazing",
        "enableABS",
        "absSlipRatioTarget",
        "absHz",
        "brakePressureInDelay",
        "brakePressureOutDelay",
        "hubRadiusSimple"
    };
    static const char* const stale_or_undocumented_fields[] = {
        "pressureGroup",
        "wheelMass",
        "tireSupportSidewallRatio",
        "tireSupportLongitudinalExtent",
        "hubBeamPrecompression",
        "hubTreadBeamDampCutoffHz",
        "hubcapBeamPrecompression",
        "wheelSideBeamDeformExpansion",
        "wheelSideReinfBeamPrecompression",
        "wheelSideReinfBeamStrengthExpansion",
        "wheelReinfBeamDampExpansion",
        "wheelTreadBeamDeformExpansion",
        "wheelTreadReinfBeamStrengthExpansion",
        "tireSupportBeamPrecompression"
    };
    std::ostringstream extras;
    for (std::size_t i = 0U;
         i < sizeof(documented_fields) /
             sizeof(documented_fields[0]);
         ++i)
    {
        extras << ",\"" << documented_fields[i] << "\":0";
    }
    for (std::size_t i = 0U;
         i < sizeof(stale_or_undocumented_fields) /
             sizeof(stale_or_undocumented_fields[0]);
         ++i)
    {
        extras << ",\"" << stale_or_undocumented_fields[i]
               << "\":0";
    }

    const JBeamPressureWheelIR ir =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                ValidPressureSection(extras.str())));
    CHECK(ir.IsValid());
    CHECK(ir.wheels.size() == 1U);
    CHECK(CountDiagnostic(
        ir,
        JBeamPressureWheelDiagnosticCode::
            DOCUMENTATION_AMBIGUITY_PRESERVED) == 7U);
    for (std::size_t i = 0U;
         i < sizeof(documented_fields) /
             sizeof(documented_fields[0]);
         ++i)
    {
        const std::string field(documented_fields[i]);
        const bool documented_ambiguity =
            field == "speedo" ||
            field == "axleBeams" ||
            field == "tireWeight" ||
            field == "hubcapNodeMaterial" ||
            field == "enableABS";
        CHECK(CountDiagnosticForField(
            ir,
            documented_ambiguity
                ? JBeamPressureWheelDiagnosticCode::
                    DOCUMENTATION_AMBIGUITY_PRESERVED
                : JBeamPressureWheelDiagnosticCode::
                    DOCUMENTED_BEHAVIOR_INERT,
            field) == 1U);
        CHECK(CountDiagnosticForField(
            ir,
            JBeamPressureWheelDiagnosticCode::
                UNKNOWN_FIELD_PRESERVED,
            field) == 0U);
    }
    for (std::size_t i = 0U;
         i < sizeof(stale_or_undocumented_fields) /
             sizeof(stale_or_undocumented_fields[0]);
         ++i)
    {
        const std::string field(
            stale_or_undocumented_fields[i]);
        CHECK(CountDiagnosticForField(
            ir,
            JBeamPressureWheelDiagnosticCode::
                UNKNOWN_FIELD_PRESERVED,
            field) == 1U);
        CHECK(CountDiagnosticForField(
            ir,
            JBeamPressureWheelDiagnosticCode::
                DOCUMENTED_BEHAVIOR_INERT,
            field) == 0U);
        CHECK(CountDiagnosticForField(
            ir,
            JBeamPressureWheelDiagnosticCode::
                DOCUMENTATION_AMBIGUITY_PRESERVED,
            field) == 0U);
    }
}

void TestInventoryOnlyStatusIsTruthful()
{
    const JBeamPressureWheelIR minimal =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ValidPressureSection()));
    CHECK(minimal.IsValid());
    CHECK(minimal.AllWheelsSchemaAdmissible());
    CHECK(minimal.wheels.size() == 1U);
    CHECK(minimal.wheels[0]
        .has_inert_or_unimplemented_behavior);

    const JBeamPressureWheelIR no_wheels =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"pressureWheels\":[[\"name\"]]"));
    CHECK(no_wheels.IsValid());
    CHECK(no_wheels.wheels.empty());
    CHECK(!no_wheels.AllWheelsSchemaAdmissible());
}

void TestScalingModifierPreservationIsInertAndBounded()
{
    const JBeamPressureWheelIR without_scale =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                ValidPressureSection(",\"dragCoef\":10")));
    const JBeamPressureWheelIR with_scale =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"scaledragCoef\":2.15," +
                ValidPressureSection(",\"dragCoef\":10")));
    CHECK(without_scale.IsValid());
    CHECK(with_scale.IsValid());
    CHECK(with_scale.source_records.size() == 2U);
    CHECK(with_scale.source_records[0].kind ==
        JBeamPressureWheelSourceKind::
            INERT_SCALING_MODIFIER);
    CHECK(with_scale.source_records[0].section_name ==
        "scaledragCoef");
    CHECK(static_cast<bool>(
        with_scale.source_records[0].raw_value));
    if (with_scale.source_records[0].raw_value)
    {
        CHECK(with_scale.source_records[0].raw_value->type ==
            JBeamValueType::NUMBER);
        CHECK(with_scale.source_records[0]
            .raw_value->number_value == 2.15);
    }
    CHECK(CountDiagnostic(
        with_scale,
        JBeamPressureWheelDiagnosticCode::
            INERT_SCALING_MODIFIER_PRESERVED) == 1U);
    CHECK(with_scale.wheels.size() == 1U);
    if (!with_scale.wheels.empty())
    {
        const JBeamPressureWheelField* drag =
            FindField(with_scale.wheels[0], "dragCoef");
        CHECK(drag != NULL);
        if (drag != NULL && drag->raw_value)
        {
            CHECK(drag->raw_value->number_value == 10.0);
        }
    }
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(with_scale) !=
        RoR::BeamNG::
            SerializeCanonicalJBeamPressureWheelIR(
                without_scale));

    const JBeamPressureWheelIR disabled_rows_remain_inert =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                ValidPressureSection(
                    ",\"disable\":true")));
    CHECK(disabled_rows_remain_inert.IsValid());
    CHECK(disabled_rows_remain_inert.wheels.size() == 1U);
    CHECK(CountDiagnosticForField(
        disabled_rows_remain_inert,
        JBeamPressureWheelDiagnosticCode::
            DOCUMENTED_BEHAVIOR_INERT,
        "disable") == 1U);
    CHECK(CountDiagnosticForField(
        disabled_rows_remain_inert,
        JBeamPressureWheelDiagnosticCode::
            UNKNOWN_FIELD_PRESERVED,
        "disable") == 0U);

    JBeamPressureWheelLimits source_limit;
    source_limit.max_source_records = 1U;
    const JBeamPressureWheelIR bounded =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"scaledragCoef\":2.15," +
                ValidPressureSection()),
            source_limit);
    CHECK(!bounded.IsValid());
    CHECK(CountDiagnostic(
        bounded,
        JBeamPressureWheelDiagnosticCode::
            SOURCE_RECORD_LIMIT) == 1U);
}

void TestCapacityHistoryDoesNotAffectCanonicalIdentity()
{
    JBeamResolvedGraph ordinary =
        ResolveSingle(ValidPressureSection());
    JBeamResolvedGraph over_reserved =
        ResolveSingle(ValidPressureSection());
    JBeamObjectField* pressure = MutableLastField(
        over_reserved.root->definition.body,
        "pressureWheels");
    CHECK(pressure != NULL);
    if (pressure != NULL && pressure->value)
    {
        std::shared_ptr<JBeamValue> table =
            std::const_pointer_cast<JBeamValue>(
                pressure->value);
        table->array_values.reserve(
            table->array_values.capacity() + 1024U);
        CHECK(table->array_values.size() > 1U);
        if (table->array_values.size() > 1U)
        {
            table->array_values[1].object_fields.reserve(
                table->array_values[1]
                    .object_fields.capacity() + 1024U);
        }
    }

    const JBeamPressureWheelIR first =
        RoR::BeamNG::BuildJBeamPressureWheelIR(ordinary);
    const JBeamPressureWheelIR second =
        RoR::BeamNG::BuildJBeamPressureWheelIR(over_reserved);
    CHECK(first.IsValid());
    CHECK(second.IsValid());
    CHECK(first.retained_byte_count ==
        second.retained_byte_count);
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(first) ==
        RoR::BeamNG::
            SerializeCanonicalJBeamPressureWheelIR(second));
}

void TestValidInventoryPreservationAndInertSources()
{
    JBeamPressureWheelIR ir;
    std::string canonical;
    {
        JBeamResolvedGraph graph = ResolveSingle(
            ValidPressureSection(
                ",\"stribeckExponent\":1.75,"
                "\"mysteryBehavior\":{\"gain\":2}") +
            ",\"controller\":{\"fileName\":\"vehicleController\"}"
            ",\"powertrain\":[[\"type\",\"name\"],"
            "[\"combustionEngine\",\"mainEngine\"]]"
            ",\"vehicleLua\":{\"script\":\"unsafe.lua\"}");
        ir = RoR::BeamNG::BuildJBeamPressureWheelIR(graph);
        canonical =
            RoR::BeamNG::
                SerializeCanonicalJBeamPressureWheelIR(ir);
        JBeamObjectField* pressure = MutableLastField(
            graph.root->definition.body,
            "pressureWheels");
        CHECK(pressure != NULL);
        if (pressure != NULL && pressure->value)
        {
            std::shared_ptr<JBeamValue> table =
                std::const_pointer_cast<JBeamValue>(
                    pressure->value);
            JBeamObjectField* source_radius =
                MutableLastField(
                    table->array_values[1],
                    "radius");
            CHECK(source_radius != NULL);
            if (source_radius != NULL &&
                source_radius->value)
            {
                std::const_pointer_cast<JBeamValue>(
                    source_radius->value)->number_value = 9.0;
                std::const_pointer_cast<JBeamValue>(
                    source_radius->value)->scalar_text = "9";
            }
        }
        CHECK(RoR::BeamNG::
            SerializeCanonicalJBeamPressureWheelIR(ir) ==
            canonical);
    }
    CHECK(ir.IsValid());
    CHECK(ir.AllWheelsSchemaAdmissible());
    CHECK(ir.documentation_profile_id ==
        "beamng-docs-0.38.5.0-2026-07-27");
    CHECK(ir.runtime_policy ==
        JBeamPressureWheelRuntimePolicy::
            INVENTORY_ONLY_NEVER_LOWER);
    CHECK(ir.parts.size() == 1U);
    CHECK(ir.source_records.size() == 4U);
    CHECK(ir.wheels.size() == 1U);
    CHECK(!canonical.empty());
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(ir) == canonical);

    const JBeamPressureWheel& wheel = ir.wheels[0];
    CHECK(wheel.admission ==
        JBeamPressureWheelAdmission::
            SCHEMA_ADMISSIBLE_INVENTORY_ONLY);
    CHECK(wheel.runtime_policy ==
        JBeamPressureWheelRuntimePolicy::
            INVENTORY_ONLY_NEVER_LOWER);
    CHECK(wheel.has_inert_or_unimplemented_behavior);
    CHECK(wheel.name == "FL");
    CHECK(wheel.hub_group == "hub_FL");
    CHECK(wheel.group == "tire_FL");
    CHECK(wheel.node1 == "n1_FL");
    CHECK(wheel.node2 == "n2_FL");
    CHECK(wheel.node_s == "9999");
    CHECK(wheel.node_s_disables_legacy_stabilizer);
    CHECK(wheel.node_arm == "arm_FL");
    CHECK(wheel.wheel_direction == -1);
    CHECK(wheel.radius == 0.35);
    CHECK(wheel.hub_radius == 0.2);
    CHECK(wheel.wheel_offset == -0.075);
    CHECK(wheel.tire_width == 0.22);
    CHECK(wheel.hub_width == 0.18);
    CHECK(wheel.has_tire);
    CHECK(wheel.num_rays == 16U);
    CHECK(wheel.approximation_generated_nodes == 64U);
    CHECK(wheel.approximation_base_generated_beams == 384U);
    CHECK(wheel.approximation_stabilizer_beams == 0U);
    CHECK(wheel.approximation_generated_beams == 384U);
    CHECK(ir.approximation_generated_node_count == 64U);
    CHECK(ir.approximation_generated_beam_count == 384U);
    CHECK(static_cast<bool>(wheel.raw_row));
    CHECK(wheel.raw_row->type == JBeamValueType::ARRAY);

    const JBeamPressureWheelField* radius =
        FindField(wheel, "radius");
    CHECK(radius != NULL);
    if (radius != NULL)
    {
        CHECK(radius->origin ==
            JBeamPressureWheelFieldOrigin::
                INHERITED_DEFAULT);
        CHECK(static_cast<bool>(radius->raw_value));
        CHECK(radius->raw_value->scalar_text == "0.35");
    }
    const JBeamPressureWheelField* name =
        FindField(wheel, "name");
    CHECK(name != NULL);
    if (name != NULL)
    {
        CHECK(name->origin ==
            JBeamPressureWheelFieldOrigin::
                POSITIONAL_CELL);
    }
    const JBeamPressureWheelField* mystery =
        FindField(wheel, "mysteryBehavior");
    CHECK(mystery != NULL);
    if (mystery != NULL)
    {
        CHECK(static_cast<bool>(mystery->raw_value));
        CHECK(mystery->raw_value->type ==
            JBeamValueType::OBJECT);
    }
    CHECK(CountDiagnostic(
        ir,
        JBeamPressureWheelDiagnosticCode::
            DOCUMENTED_BEHAVIOR_INERT) == 1U);
    CHECK(CountDiagnostic(
        ir,
        JBeamPressureWheelDiagnosticCode::
            UNKNOWN_FIELD_PRESERVED) == 1U);
    CHECK(CountDiagnostic(
        ir,
        JBeamPressureWheelDiagnosticCode::
            INERT_SECTION_PRESERVED) == 3U);
    CHECK(ir.source_records[0].kind ==
        JBeamPressureWheelSourceKind::PRESSURE_WHEELS);
    CHECK(ir.source_records[1].kind ==
        JBeamPressureWheelSourceKind::
            INERT_CONTROLLER_OR_POWERTRAIN);
    CHECK(ir.source_records[3].kind ==
        JBeamPressureWheelSourceKind::INERT_LUA);
    CHECK(static_cast<bool>(
        ir.source_records[1].raw_value));
}

std::vector<JBeamPackageSource> ComposedPackages()
{
    std::vector<JBeamPackageSource> packages;
    packages.push_back(Package(
        "vehicles/composed/main.jbeam",
        "{"
        "\"composed_main\":{"
        "\"slotType\":\"main\","
        "\"slots\":["
        "[\"type\",\"default\",\"description\"],"
        "[\"wheelset\",\"composed_wheels\",\"Wheels\"]"
        "],"
        "\"pressureWheels\":["
        "[\"name\"],"
        "{\"radius\":0.4,\"hubRadius\":0.25,"
        "\"wheelOffset\":0,\"tireWidth\":0.24,"
        "\"hubWidth\":0.2,\"hasTire\":false,"
        "\"numRays\":20}"
        "]"
        "}"
        "}"));
    packages.push_back(Package(
        "vehicles/composed/wheels.jbeam",
        "{"
        "\"composed_wheels\":{"
        "\"slotType\":\"wheelset\","
        "\"pressureWheels\":["
        "[\"name\",\"hubGroup\",\"group\","
        "\"node1\",\"node2\",\"nodeS\","
        "\"nodeArm\",\"wheelDir\"],"
        "[\"FR\",\"hub_FR\",\"tire_FR\","
        "\"a\",\"b\",\"stabilizer\",\"arm\",1]"
        "]"
        "}"
        "}"));
    return packages;
}

void TestCrossPartDefaultsAndDeterministicOrder()
{
    std::vector<JBeamPackageSource> forward =
        ComposedPackages();
    std::vector<JBeamPackageSource> reverse = forward;
    std::reverse(reverse.begin(), reverse.end());
    const JBeamPressureWheelIR first =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            Resolve(forward));
    const JBeamPressureWheelIR permuted =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            Resolve(reverse));
    CHECK(first.IsValid());
    CHECK(first.AllWheelsSchemaAdmissible());
    CHECK(first.parts.size() == 2U);
    CHECK(first.wheels.size() == 1U);
    CHECK(first.wheels[0].name == "FR");
    CHECK(first.wheels[0].radius == 0.4);
    CHECK(!first.wheels[0].has_tire);
    CHECK(first.wheels[0].num_rays == 20U);
    CHECK(!first.wheels[0]
        .node_s_disables_legacy_stabilizer);
    CHECK(first.wheels[0]
        .approximation_generated_nodes == 80U);
    CHECK(first.wheels[0]
        .approximation_base_generated_beams == 480U);
    CHECK(first.wheels[0]
        .approximation_stabilizer_beams == 20U);
    CHECK(first.wheels[0]
        .approximation_generated_beams == 500U);
    const JBeamPressureWheelField* radius =
        FindField(first.wheels[0], "radius");
    CHECK(radius != NULL);
    if (radius != NULL)
    {
        CHECK(radius->provenance.PartName() ==
            "composed_main");
    }
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(first) ==
        RoR::BeamNG::
            SerializeCanonicalJBeamPressureWheelIR(permuted));

    const JBeamPressureWheelIR defaults_before =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ValidPressureSection()));
    const JBeamPressureWheelIR defaults_after =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"pressureWheels\":[" +
                RequiredHeader() + "," +
                WheelRow("FL") + "," +
                RequiredDefaults() + "]"));
    CHECK(defaults_before.IsValid());
    CHECK(!defaults_after.IsValid());
    CHECK(defaults_after.wheels.size() == 1U);
    CHECK(defaults_after.wheels[0].admission ==
        JBeamPressureWheelAdmission::
            PRESERVED_NOT_ADMISSIBLE);
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(
            defaults_before) !=
        RoR::BeamNG::
            SerializeCanonicalJBeamPressureWheelIR(
                defaults_after));
}

void TestValidationAndNoGuessedGeometry()
{
    const JBeamPressureWheelIR invalid =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"pressureWheels\":["
                "[\"name\",\"hubGroup\",\"group\","
                "\"node1:\",\"node1\",\"node2:\","
                "\"nodeS\",\"nodeArm:\",\"wheelDir\"],"
                "{\"radius\":0,\"hubRadius\":0.2,"
                "\"wheelOffset\":\"$=0\","
                "\"tireWidth\":0.22,\"hubWidth\":0.18,"
                "\"hasTire\":1,\"numRays\":11},"
                "[\"FL\",\"hub\",\"tire\",\"a\",\"other\","
                "\"b\",9998,\"arm\",\"1\"]"
                "]"));
    CHECK(!invalid.IsValid());
    CHECK(invalid.wheels.size() == 1U);
    CHECK(invalid.wheels[0].admission ==
        JBeamPressureWheelAdmission::
            PRESERVED_NOT_ADMISSIBLE);
    CHECK(CountDiagnostic(
        invalid,
        JBeamPressureWheelDiagnosticCode::
            AMBIGUOUS_REQUIRED_COLUMN) == 0U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamPressureWheelDiagnosticCode::
            AMBIGUOUS_REQUIRED_FIELD) >= 1U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamPressureWheelDiagnosticCode::
            INVALID_GEOMETRY) == 1U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamPressureWheelDiagnosticCode::
            EXPRESSION_DISABLED) == 1U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamPressureWheelDiagnosticCode::
            INVALID_FIELD_TYPE) >= 2U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamPressureWheelDiagnosticCode::
            INVALID_WHEEL_DIRECTION) == 1U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamPressureWheelDiagnosticCode::
            INVALID_NUM_RAYS) == 1U);

    const JBeamPressureWheelIR missing_geometry =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"pressureWheels\":[" +
                RequiredHeader() + "," +
                WheelRow("FL") + "]"));
    CHECK(!missing_geometry.IsValid());
    CHECK(CountDiagnostic(
        missing_geometry,
        JBeamPressureWheelDiagnosticCode::
            MISSING_REQUIRED_FIELD) == 7U);

    const JBeamPressureWheelIR row_override =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"pressureWheels\":[" +
                RequiredHeader() + "," +
                RequiredDefaults() + "," +
                "[\"FL\",\"hub\",\"tire\",\"a\",\"b\","
                "9999,\"arm\",-1,{\"radius\":0.5,"
                "\"numRays\":10}]"
                "]"));
    CHECK(row_override.IsValid());
    CHECK(row_override.wheels[0].radius == 0.5);
    CHECK(row_override.wheels[0].num_rays == 10U);
    const JBeamPressureWheelField* overridden =
        FindField(row_override.wheels[0], "radius");
    CHECK(overridden != NULL);
    if (overridden != NULL)
    {
        CHECK(overridden->origin ==
            JBeamPressureWheelFieldOrigin::
                ROW_LOCAL_OVERRIDE);
    }

    const JBeamPressureWheelIR modifier_supplied =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"pressureWheels\":["
                "[\"name\"],"
                "{\"hubGroup\":\"hub_default\","
                "\"group\":\"tire_default\","
                "\"node1:\":\"a\",\"node2:\":\"b\","
                "\"nodeS\":9999,\"nodeArm:\":\"arm\","
                "\"wheelDir\":1,"
                "\"radius\":0.35,\"hubRadius\":0.2,"
                "\"wheelOffset\":0,\"tireWidth\":0.22,"
                "\"hubWidth\":0.18,\"hasTire\":true,"
                "\"numRays\":22},"
                "[\"FL\"]"
                "]"));
    CHECK(modifier_supplied.IsValid());
    CHECK(modifier_supplied.AllWheelsSchemaAdmissible());
    CHECK(modifier_supplied.wheels.size() == 1U);
    CHECK(modifier_supplied.wheels[0].num_rays == 22U);
    CHECK(modifier_supplied.wheels[0]
        .has_inert_or_unimplemented_behavior);
    const JBeamPressureWheelField* inherited_node =
        FindField(modifier_supplied.wheels[0], "node1:");
    CHECK(inherited_node != NULL);
    if (inherited_node != NULL)
    {
        CHECK(inherited_node->origin ==
            JBeamPressureWheelFieldOrigin::
                INHERITED_DEFAULT);
    }
    CHECK(CountDiagnostic(
        modifier_supplied,
        JBeamPressureWheelDiagnosticCode::
            MISSING_REQUIRED_COLUMN) == 0U);
}

void TestNonFiniteDefenseAndMalformedValueGraphs()
{
    static const std::uint64_t hostile_numbers[] = {
        UINT64_C(0x7ff8000000000001),
        UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000)
    };
    for (std::size_t i = 0U;
         i < sizeof(hostile_numbers) /
             sizeof(hostile_numbers[0]);
         ++i)
    {
        JBeamResolvedGraph graph =
            ResolveSingle(ValidPressureSection());
        JBeamObjectField* pressure = MutableLastField(
            graph.root->definition.body,
            "pressureWheels");
        CHECK(pressure != NULL);
        if (pressure != NULL && pressure->value)
        {
            std::shared_ptr<JBeamValue> table =
                std::const_pointer_cast<JBeamValue>(
                    pressure->value);
            JBeamValue& defaults = table->array_values[1];
            JBeamObjectField* radius =
                MutableLastField(defaults, "radius");
            CHECK(radius != NULL);
            if (radius != NULL && radius->value)
            {
                std::shared_ptr<JBeamValue> value =
                    std::const_pointer_cast<JBeamValue>(
                        radius->value);
                value->number_value =
                    DoubleFromBits(hostile_numbers[i]);
            }
        }
        const JBeamPressureWheelIR nonfinite =
            RoR::BeamNG::BuildJBeamPressureWheelIR(graph);
        CHECK(!nonfinite.IsValid());
        CHECK(CountDiagnostic(
            nonfinite,
            JBeamPressureWheelDiagnosticCode::
                NON_FINITE_NUMBER) == 1U);
    }

    JBeamResolvedGraph cyclic_value_graph =
        ResolveSingle(ValidPressureSection());
    std::shared_ptr<JBeamValue> hostile_cycle;
    JBeamObjectField* cyclic_pressure = MutableLastField(
        cyclic_value_graph.root->definition.body,
        "pressureWheels");
    CHECK(cyclic_pressure != NULL);
    if (cyclic_pressure != NULL &&
        cyclic_pressure->value)
    {
        std::shared_ptr<JBeamValue> table =
            std::const_pointer_cast<JBeamValue>(
                cyclic_pressure->value);
        JBeamValue& defaults = table->array_values[1];
        hostile_cycle.reset(new JBeamValue());
        hostile_cycle->type = JBeamValueType::OBJECT;
        JBeamObjectField self;
        self.key = "self";
        self.value = hostile_cycle;
        hostile_cycle->object_fields.push_back(self);
        JBeamObjectField hostile;
        hostile.key = "hostileCycle";
        hostile.value = hostile_cycle;
        defaults.object_fields.push_back(hostile);
    }
    const JBeamPressureWheelIR cycle_rejected =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            cyclic_value_graph);
    CHECK(!cycle_rejected.IsValid());
    CHECK(CountDiagnostic(
        cycle_rejected,
        JBeamPressureWheelDiagnosticCode::
            PRESERVED_VALUE_LIMIT) == 1U);
    if (hostile_cycle)
    {
        hostile_cycle->object_fields.clear();
    }

    JBeamResolvedGraph cyclic_parts =
        ResolveSingle(ValidPressureSection());
    JBeamResolvedSlot loop;
    loop.child = cyclic_parts.root;
    cyclic_parts.root->slots.push_back(loop);
    const JBeamPressureWheelIR part_cycle =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            cyclic_parts);
    CHECK(!part_cycle.IsValid());
    CHECK(CountDiagnostic(
        part_cycle,
        JBeamPressureWheelDiagnosticCode::
            RESOLVED_GRAPH_CYCLE) == 1U);
    cyclic_parts.root->slots.clear();
}

std::string LuaPayloadSection(
    const std::string& name,
    std::size_t value_count)
{
    std::ostringstream source;
    source << '"' << name << "\":{\"payload\":[";
    for (std::size_t i = 0U; i < value_count; ++i)
    {
        if (i != 0U)
        {
            source << ',';
        }
        source << i;
    }
    source << "]}";
    return source.str();
}

void TestAggregateWorkAndHardDepthBounds()
{
    JBeamPressureWheelLimits work_limits;
    work_limits.max_preserved_value_work_units = 130U;
    const std::string first =
        LuaPayloadSection("vehicleLuaA", 40U);
    const std::string second =
        LuaPayloadSection("vehicleLuaB", 40U);
    const JBeamPressureWheelIR one_record =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(first),
            work_limits);
    CHECK(one_record.IsValid());
    CHECK(one_record.source_records.size() == 1U);

    const JBeamPressureWheelIR aggregate_rejected =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(first + "," + second),
            work_limits);
    CHECK(!aggregate_rejected.IsValid());
    CHECK(CountDiagnostic(
        aggregate_rejected,
        JBeamPressureWheelDiagnosticCode::
            PRESERVED_VALUE_LIMIT) == 1U);

    JBeamResolvedGraph deep_graph =
        ResolveSingle(
            ValidPressureSection() +
            ",\"vehicleLua\":{}");
    JBeamObjectField* lua = MutableLastField(
        deep_graph.root->definition.body,
        "vehicleLua");
    CHECK(lua != NULL);
    if (lua != NULL && lua->value)
    {
        std::shared_ptr<JBeamValue> current =
            std::const_pointer_cast<JBeamValue>(
                lua->value);
        for (std::size_t depth = 0U;
             depth < 70U;
             ++depth)
        {
            std::shared_ptr<JBeamValue> child(
                new JBeamValue());
            child->type = JBeamValueType::OBJECT;
            JBeamObjectField edge;
            edge.key = "next";
            edge.value = child;
            current->object_fields.push_back(edge);
            current = child;
        }
    }
    JBeamPressureWheelLimits raised_depth;
    raised_depth.max_preserved_value_depth =
        std::numeric_limits<std::size_t>::max();
    const JBeamPressureWheelIR deep_rejected =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            deep_graph,
            raised_depth);
    CHECK(!deep_rejected.IsValid());
    CHECK(CountDiagnostic(
        deep_rejected,
        JBeamPressureWheelDiagnosticCode::
            PRESERVED_VALUE_LIMIT) == 1U);

    JBeamPressureWheelIR manual =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ValidPressureSection()));
    CHECK(manual.IsValid());
    manual.canonical_value_depth_limit =
        std::numeric_limits<std::size_t>::max();
    if (lua != NULL)
    {
        manual.source_records[0].raw_value = lua->value;
    }
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(
            manual).empty());
}

std::string ManyWheelSection(std::size_t count)
{
    std::ostringstream source;
    source << "\"pressureWheels\":["
           << RequiredHeader() << ','
           << RequiredDefaults();
    for (std::size_t i = 0U; i < count; ++i)
    {
        std::ostringstream name;
        name << 'W' << i;
        source << ',' << WheelRow(name.str());
    }
    source << ']';
    return source.str();
}

JBeamResolvedGraph RepeatedWheelGraph(std::size_t count)
{
    JBeamResolvedGraph graph =
        ResolveSingle(ValidPressureSection());
    JBeamObjectField* pressure = MutableLastField(
        graph.root->definition.body,
        "pressureWheels");
    CHECK(pressure != NULL);
    if (pressure == NULL || !pressure->value ||
        count == 0U)
    {
        return graph;
    }
    std::shared_ptr<JBeamValue> table =
        std::const_pointer_cast<JBeamValue>(
            pressure->value);
    const JBeamValue row = table->array_values.back();
    table->array_values.reserve(count + 2U);
    for (std::size_t i = 1U; i < count; ++i)
    {
        table->array_values.push_back(row);
    }
    return graph;
}

void TestHostileLimitsAndCanonicalBounds()
{
    const JBeamPressureWheelIR more_than_future_lowering =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ManyWheelSection(65U)));
    CHECK(more_than_future_lowering.IsValid());
    CHECK(more_than_future_lowering.wheels.size() == 65U);
    CHECK(CountDiagnostic(
        more_than_future_lowering,
        JBeamPressureWheelDiagnosticCode::
            WHEEL_LIMIT) == 0U);

    JBeamPressureWheelLimits configured_wheel_limit;
    configured_wheel_limit.max_wheels = 64U;
    const JBeamPressureWheelIR configured_rejected =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ManyWheelSection(65U)),
            configured_wheel_limit);
    CHECK(!configured_rejected.IsValid());
    CHECK(configured_rejected.wheels.empty());
    CHECK(CountDiagnostic(
        configured_rejected,
        JBeamPressureWheelDiagnosticCode::
            WHEEL_LIMIT) == 1U);

    JBeamPressureWheelLimits topology_limits;
    topology_limits.max_approximation_generated_nodes = 63U;
    const JBeamPressureWheelIR topology_rejected =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ValidPressureSection()),
            topology_limits);
    CHECK(!topology_rejected.IsValid());
    CHECK(topology_rejected.wheels.empty());
    CHECK(CountDiagnostic(
        topology_rejected,
        JBeamPressureWheelDiagnosticCode::
            TOPOLOGY_NODE_LIMIT) == 1U);

    JBeamPressureWheelLimits entry_limits;
    entry_limits.max_entries = 1U;
    const JBeamPressureWheelIR entry_rejected =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ValidPressureSection()),
            entry_limits);
    CHECK(!entry_rejected.IsValid());
    CHECK(CountDiagnostic(
        entry_rejected,
        JBeamPressureWheelDiagnosticCode::
            ENTRY_LIMIT) == 1U);

    JBeamPressureWheelLimits byte_limits;
    byte_limits.max_retained_bytes = 1U;
    const JBeamPressureWheelIR byte_rejected =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ValidPressureSection()),
            byte_limits);
    CHECK(!byte_rejected.IsValid());
    CHECK(CountDiagnostic(
        byte_rejected,
        JBeamPressureWheelDiagnosticCode::
            RETAINED_BYTE_LIMIT) == 1U);

    JBeamPressureWheelLimits diagnostic_limits;
    diagnostic_limits.max_diagnostics = 0U;
    const JBeamPressureWheelIR diagnostic_rejected =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                "\"pressureWheels\":42"),
            diagnostic_limits);
    CHECK(!diagnostic_rejected.IsValid());
    CHECK(diagnostic_rejected.diagnostics.size() == 1U);
    CHECK(diagnostic_rejected.diagnostics[0].code ==
        JBeamPressureWheelDiagnosticCode::
            DIAGNOSTIC_LIMIT);

    JBeamPressureWheelLimits canonical_limits;
    canonical_limits.max_canonical_output_bytes = 16U;
    const JBeamPressureWheelIR bounded =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ValidPressureSection()),
            canonical_limits);
    CHECK(bounded.IsValid());
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(
            bounded).empty());

    JBeamPressureWheelIR manual =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(ValidPressureSection()));
    CHECK(manual.IsValid());
    std::shared_ptr<JBeamValue> cycle(new JBeamValue());
    cycle->type = JBeamValueType::OBJECT;
    JBeamObjectField self;
    self.key = "self";
    self.value = cycle;
    cycle->object_fields.push_back(self);
    manual.source_records[0].raw_value = cycle;
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(
            manual).empty());
    cycle->object_fields.clear();

    JBeamPressureWheelIR overflow_manual;
    overflow_manual.canonical_output_byte_limit = 512U;
    overflow_manual.canonical_value_depth_limit = 64U;
    RoR::BeamNG::JBeamPressureWheelSourceRecord
        overflow_record;
    std::shared_ptr<JBeamValue> many_null_fields(
        new JBeamValue());
    many_null_fields->type = JBeamValueType::OBJECT;
    many_null_fields->object_fields.reserve(16384U);
    for (std::size_t i = 0U; i < 16384U; ++i)
    {
        JBeamObjectField null_field;
        null_field.key = "x";
        many_null_fields->object_fields.push_back(
            null_field);
    }
    overflow_record.raw_value = many_null_fields;
    overflow_manual.source_records.push_back(
        overflow_record);
    CHECK(RoR::BeamNG::
        SerializeCanonicalJBeamPressureWheelIR(
            overflow_manual).empty());
}

void TestDuplicateSectionsAndHardCallerCap()
{
    const JBeamPressureWheelIR duplicate =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            ResolveSingle(
                ValidPressureSection() + "," +
                ValidPressureSection(
                    std::string(),
                    std::string("," + WheelRow("RR")))));
    CHECK(!duplicate.IsValid());
    CHECK(duplicate.wheels.size() == 3U);
    CHECK(CountDiagnostic(
        duplicate,
        JBeamPressureWheelDiagnosticCode::
            DUPLICATE_SECTION) == 2U);
    for (std::size_t i = 0U;
         i < duplicate.wheels.size();
         ++i)
    {
        CHECK(duplicate.wheels[i].admission ==
            JBeamPressureWheelAdmission::
                PRESERVED_NOT_ADMISSIBLE);
    }

    JBeamPressureWheelLimits caller_cannot_raise_hard_cap;
    caller_cannot_raise_hard_cap.max_wheels =
        std::numeric_limits<std::size_t>::max();
    const JBeamPressureWheelIR hard_cap =
        RoR::BeamNG::BuildJBeamPressureWheelIR(
            RepeatedWheelGraph(4097U),
            caller_cannot_raise_hard_cap);
    CHECK(!hard_cap.IsValid());
    CHECK(CountDiagnostic(
        hard_cap,
        JBeamPressureWheelDiagnosticCode::
            WHEEL_LIMIT) == 1U);
}

} // namespace

int main()
{
    TestDocumentationProvenanceAndLocalPolicy();
    TestCurrentOfficialWheelFieldClassification();
    TestInventoryOnlyStatusIsTruthful();
    TestScalingModifierPreservationIsInertAndBounded();
    TestCapacityHistoryDoesNotAffectCanonicalIdentity();
    TestValidInventoryPreservationAndInertSources();
    TestCrossPartDefaultsAndDeterministicOrder();
    TestValidationAndNoGuessedGeometry();
    TestNonFiniteDefenseAndMalformedValueGraphs();
    TestAggregateWorkAndHardDepthBounds();
    TestHostileLimitsAndCanonicalBounds();
    TestDuplicateSectionsAndHardCallerCap();

    if (g_failures != 0)
    {
        std::cerr << g_failures
                  << " JBeam pressure-wheel IR test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JBeam pressure-wheel IR tests passed\n";
    return EXIT_SUCCESS;
}
