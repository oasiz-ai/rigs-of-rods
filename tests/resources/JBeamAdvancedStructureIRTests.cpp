#include "JBeamAdvancedStructureIR.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
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

using RoR::BeamNG::JBeamAdvancedBehavior;
using RoR::BeamNG::JBeamAdvancedDiagnosticCode;
using RoR::BeamNG::JBeamAdvancedField;
using RoR::BeamNG::JBeamAdvancedFieldOrigin;
using RoR::BeamNG::JBeamAdvancedLimits;
using RoR::BeamNG::JBeamAdvancedSectionKind;
using RoR::BeamNG::JBeamAdvancedStructureIR;
using RoR::BeamNG::JBeamHydroActuatorAdmission;
using RoR::BeamNG::JBeamHydroActuatorAdmissionCode;
using RoR::BeamNG::JBeamHydroBeamPropertyAdmission;
using RoR::BeamNG::JBeamHydroBeamPropertyAdmissionCode;
using RoR::BeamNG::JBeamHydroRuntimePlan;
using RoR::BeamNG::JBeamHydroRuntimePlanCode;
using RoR::BeamNG::JBeamHydroRuntimePlanSet;
using RoR::BeamNG::JBeamHydroRuntimePlanSetCode;
using RoR::BeamNG::JBeamObjectField;
using RoR::BeamNG::JBeamPackageIndex;
using RoR::BeamNG::JBeamPackageSource;
using RoR::BeamNG::JBeamParseResult;
using RoR::BeamNG::JBeamResolvedGraph;
using RoR::BeamNG::JBeamResolvedPartNode;
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
        "vehicles/advanced/main.jbeam",
        std::string("{\"advanced_test\":{\"slotType\":\"main\",") +
            body + "}}"));
    return Resolve(packages);
}

std::string Nodes()
{
    return
        "\"nodes\":[[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"n1\",0,0,0],[\"n2\",1,0,0],"
        "[\"n3\",0,1,0],[\"n4\",1,1,0],"
        "[\"n5\",0,0,1],[\"n6\",1,0,1],"
        "[\"n7\",0,1,1],[\"n8\",1,1,1]]";
}

std::string FrameAndNodes()
{
    return
        "\"nodes\":[[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"ref\",0,0,0],[\"back\",0,1,0],"
        "[\"left\",1,0,0],[\"up\",0,0,1],"
        "[\"leftCorner\",1,-1,0],"
        "[\"rightCorner\",-1,-1,0]],"
        "\"refNodes\":[[\"ref:\",\"back:\",\"left:\","
        "\"up:\",\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]]";
}

std::string FrameAndFarNodes()
{
    return
        "\"nodes\":[[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"ref\",0,0,0],[\"back\",0,1,0],"
        "[\"left\",1,0,0],[\"up\",0,0,1],"
        "[\"leftCorner\",1,-1,0],"
        "[\"rightCorner\",-1,-1,0],"
        "[\"farA\",0,0,0],[\"farB\",2e38,0,0]],"
        "\"refNodes\":[[\"ref:\",\"back:\",\"left:\","
        "\"up:\",\"leftCorner:\",\"rightCorner:\"],"
        "[\"ref\",\"back\",\"left\",\"up\","
        "\"leftCorner\",\"rightCorner\"]]";
}

std::string AllOfficialSections()
{
    return Nodes() +
        ",\"hydros\":[[\"id1:\",\"id2:\"],"
        "{\"beamPrecompression\":1.0,\"beamSpring\":8001000,"
        "\"beamDamp\":50,\"beamDeform\":\"FLT_MAX\","
        "\"beamStrength\":125000},"
        "[\"n1\",\"n2\",{\"factor\":0.14,"
        "\"steeringWheelLock\":510,\"inRate\":1.25,"
        "\"outRate\":1.25}]]"
        ",\"rails\":{\"Rail1\":{\"links:\":[\"n1\",\"n2\"],"
        "\"broken:\":[],\"looped\":false,\"capped\":true}}"
        ",\"rails2\":[[\"id\",\"links:\",\"broken:\","
        "\"looped\",\"capped\"],"
        "[\"Rail2\",[\"n3\",\"n4\"],[],false,true]]"
        ",\"slidenodes\":[[\"id:\",\"railName\",\"attached\","
        "\"fixToRail\",\"tolerance\",\"spring\",\"strength\","
        "\"capStrength\"],"
        "[\"n5\",\"Rail1\",true,true,0,10000000,"
        "\"FLT_MAX\",\"FLT_MAX\"]]"
        ",\"thrusters\":[[\"id1:\",\"id2:\",\"factor\","
        "\"thrustLimit\",\"control\"],"
        "[\"n1\",\"n2\",50000,50000,\"jato\"]]"
        ",\"torsionbars\":[[\"id1:\",\"id2:\",\"id3:\","
        "\"id4:\"],"
        "{\"spring\":10000000,\"damp\":100,"
        "\"deform\":25000,\"strength\":100000},"
        "[\"n3\",\"n1\",\"n2\",\"n4\"]]";
}

std::size_t CountDiagnostic(
    const JBeamAdvancedStructureIR& ir,
    JBeamAdvancedDiagnosticCode code)
{
    std::size_t result = 0U;
    for (std::size_t i = 0U; i < ir.diagnostics.size(); ++i)
    {
        if (ir.diagnostics[i].code == code)
        {
            ++result;
        }
    }
    return result;
}

const JBeamAdvancedField* FindField(
    const std::vector<JBeamAdvancedField>& fields,
    const std::string& name)
{
    for (std::size_t i = 0U; i < fields.size(); ++i)
    {
        if (fields[i].name == name)
        {
            return &fields[i];
        }
    }
    return NULL;
}

JBeamObjectField* MutableLastField(
    JBeamValue& object,
    const std::string& name)
{
    for (std::size_t i = object.object_fields.size(); i > 0U; --i)
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

void TestDocumentationProfile()
{
    const RoR::BeamNG::JBeamAdvancedDocumentationProfile& profile =
        RoR::BeamNG::GetJBeamAdvancedDocumentationProfile();
    CHECK(profile.profile_id ==
        "beamng-docs-0.38.5.0-2026-07-27");
    CHECK(profile.beamng_version == "0.38.5.0");
    CHECK(profile.hydros_url.find("sections/hydros/") !=
        std::string::npos);
    CHECK(profile.hydros_last_modified == "2025-01-24");
    CHECK(profile.electrics_url.find("sections/electrics/") !=
        std::string::npos);
    CHECK(profile.electrics_last_modified == "2025-04-02");
    CHECK(profile.rails_url.find("sections/rails/") !=
        std::string::npos);
    CHECK(profile.rails_last_modified == "2025-12-10");
    CHECK(profile.thrusters_last_modified == "2025-01-24");
    CHECK(profile.torsionbars_last_modified == "2025-04-09");
    CHECK(profile.hydro_input_source == "steering_input");
    CHECK(profile.hydro_out_limit == 2.0);
    CHECK(profile.hydro_in_limit == 1.0);
    CHECK(profile.hydro_input_factor == 1.0);
    CHECK(profile.hydro_input_center == 0.0);
    CHECK(profile.hydro_in_rate == 2.0);
    CHECK(profile.hydro_out_rate_inherits_in_rate);
    CHECK(profile.hydro_auto_center_rate_inherits_in_rate);
    CHECK(profile.hydro_input_in_limit == -1.0);
    CHECK(profile.hydro_input_out_limit == 1.0);
    CHECK(profile.slidenode_attached);
    CHECK(profile.slidenode_fix_to_rail);
    CHECK(!profile.rail_looped);
    CHECK(!profile.rail_capped);
    CHECK(profile.thruster_factor == 1.0);
    CHECK(profile.thruster_limit_is_flt_max);
    CHECK(profile.torsion_precompression_angle == 0.0);
    CHECK(profile.torsion_precompression_time == 0.0);
    CHECK(profile.torsion_spring2_inherits_spring);
    CHECK(profile.torsion_damp2_inherits_damp);
}

void TestOfficialExamplesAndClassification()
{
    const JBeamAdvancedStructureIR ir =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(AllOfficialSections()));
    CHECK(ir.IsValid());
    CHECK(ir.parts.size() == 1U);
    CHECK(ir.source_records.size() == 6U);
    CHECK(ir.modifiers.size() == 2U);
    CHECK(ir.hydros.size() == 1U);
    CHECK(ir.rails.size() == 2U);
    CHECK(ir.slidenodes.size() == 1U);
    CHECK(ir.thrusters.size() == 1U);
    CHECK(ir.torsionbars.size() == 1U);
    CHECK(ir.rejected_entries.empty());

    CHECK(ir.hydros[0].entry.behavior ==
        JBeamAdvancedBehavior::INVENTORY_ONLY);
    CHECK(ir.hydros[0].node1 == "n1");
    CHECK(ir.hydros[0].node2 == "n2");
    CHECK(ir.hydros[0].input_source == "steering_input");
    CHECK(ir.hydros[0].has_factor);
    CHECK(ir.hydros[0].factor == 0.14);
    CHECK(ir.hydros[0].out_limit == 2.0);
    CHECK(ir.hydros[0].in_limit == 1.0);
    CHECK(ir.hydros[0].input_factor == 1.0);
    CHECK(ir.hydros[0].input_center == 0.0);
    CHECK(ir.hydros[0].in_rate == 1.25);
    CHECK(ir.hydros[0].out_rate == 1.25);
    CHECK(ir.hydros[0].auto_center_rate == 1.25);
    CHECK(ir.hydros[0].steering_wheel_lock == 510.0);
    CHECK(ir.hydros[0].input_in_limit == -1.0);
    CHECK(ir.hydros[0].input_out_limit == 1.0);

    CHECK(ir.rails[0].entry.behavior ==
        JBeamAdvancedBehavior::NATIVE_READY_STATIC_GEOMETRY);
    CHECK(ir.rails[0].name == "Rail1");
    CHECK(ir.rails[0].links.size() == 2U);
    CHECK(!ir.rails[0].looped);
    CHECK(ir.rails[0].capped);
    CHECK(ir.rails[1].entry.behavior ==
        JBeamAdvancedBehavior::NATIVE_READY_STATIC_GEOMETRY);
    CHECK(ir.rails[1].name == "Rail2");
    CHECK(ir.rails[1].links[0] == "n3");
    CHECK(ir.rails[1].links[1] == "n4");

    CHECK(ir.slidenodes[0].entry.behavior ==
        JBeamAdvancedBehavior::INVENTORY_ONLY);
    CHECK(ir.slidenodes[0].attached);
    CHECK(ir.slidenodes[0].fix_to_rail);
    CHECK(ir.slidenodes[0].strength_is_flt_max);
    CHECK(ir.slidenodes[0].cap_strength_is_flt_max);

    CHECK(ir.thrusters[0].entry.behavior ==
        JBeamAdvancedBehavior::INVENTORY_ONLY);
    CHECK(ir.thrusters[0].direction_node == "n1");
    CHECK(ir.thrusters[0].force_node == "n2");
    CHECK(ir.thrusters[0].factor == 50000.0);
    CHECK(!ir.thrusters[0].thrust_limit_is_flt_max);
    CHECK(ir.thrusters[0].control == "jato");

    CHECK(ir.torsionbars[0].entry.behavior ==
        JBeamAdvancedBehavior::INVENTORY_ONLY);
    CHECK(ir.torsionbars[0].spring == 10000000.0);
    CHECK(ir.torsionbars[0].damp == 100.0);
    CHECK(ir.torsionbars[0].spring2 == 10000000.0);
    CHECK(ir.torsionbars[0].damp2 == 100.0);
    CHECK(!ir.torsionbars[0].anisotropic);
    CHECK(ir.torsionbars[0].precompression_angle == 0.0);
    CHECK(ir.torsionbars[0].precompression_time == 0.0);

    const std::string canonical =
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(ir);
    CHECK(!canonical.empty());
}

void TestDefaultsAndDependentDefaults()
{
    const JBeamAdvancedStructureIR ir =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"rails2\":[[\"id\",\"links:\"],"
                "[\"R\",[\"n1\",\"n2\"]]]"
                ",\"slidenodes\":[[\"id:\",\"railName\","
                "\"attached\",\"fixToRail\",\"tolerance\",\"spring\","
                "\"strength\",\"capStrength\"],"
                "[\"n3\",\"R\"]]"
                ",\"hydros\":[[\"id1:\",\"id2:\",\"inRate\","
                "\"outRate\",\"autoCenterRate\"],"
                "[\"n1\",\"n2\",3.5]]"
                ",\"thrusters\":[[\"id1:\",\"id2:\"],"
                "{\"control\":\"auto\"},[\"n1\",\"n2\"]]"
                ",\"torsionbars\":[[\"id1:\",\"id2:\",\"id3:\","
                "\"id4:\",\"spring\",\"damp\",\"spring2\",\"damp2\","
                "\"deform\",\"strength\",\"precompressionAngle\","
                "\"precompressionTime\",\"name\"],"
                "[\"n3\",\"n1\",\"n2\",\"n4\",12,3]]"));
    CHECK(ir.IsValid());
    CHECK(ir.rails[0].looped == false);
    CHECK(ir.rails[0].capped == false);
    CHECK(ir.slidenodes[0].attached);
    CHECK(ir.slidenodes[0].fix_to_rail);
    CHECK(!ir.slidenodes[0].has_tolerance);
    CHECK(ir.hydros[0].out_rate == 3.5);
    CHECK(ir.hydros[0].auto_center_rate == 3.5);
    CHECK(ir.thrusters[0].factor == 1.0);
    CHECK(ir.thrusters[0].thrust_limit_is_flt_max);
    CHECK(ir.torsionbars[0].spring2 == 12.0);
    CHECK(ir.torsionbars[0].damp2 == 3.0);
}

void TestHydroActuatorAdmission()
{
    const JBeamAdvancedStructureIR documented =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "[\"n1\",\"n2\",{\"factor\":0.14,"
            "\"steeringWheelLock\":510,\"inRate\":1.25,"
            "\"outRate\":1.5,\"autoCenterRate\":0.75}]]"));
    CHECK(documented.IsValid());
    const JBeamHydroActuatorAdmission admitted =
        RoR::BeamNG::AdmitJBeamHydroActuator(documented, 0U);
    CHECK(admitted.IsAdmitted());
    CHECK(admitted.code == JBeamHydroActuatorAdmissionCode::ADMITTED);
    CHECK(admitted.source_hydro_index == 0U);
    CHECK(admitted.node1 == "n1");
    CHECK(admitted.node2 == "n2");
    CHECK(admitted.input_source == "steering_input");
    CHECK(admitted.has_steering_wheel_lock);
    CHECK(admitted.steering_wheel_lock == 510.0);
    CHECK(admitted.config.has_factor);
    CHECK(admitted.config.factor == 0.14);
    CHECK(admitted.config.in_rate == 1.25);
    CHECK(admitted.config.out_rate == 1.5);
    CHECK(admitted.config.auto_center_rate == 0.75);

    const RoR::HydroActuatorStep positive =
        RoR::ResolveHydroActuatorTarget(admitted.config, 1.0);
    const RoR::HydroActuatorStep negative =
        RoR::ResolveHydroActuatorTarget(admitted.config, -1.0);
    CHECK(positive.valid);
    CHECK(negative.valid);
    CHECK(positive.target_ratio == 1.0 + admitted.config.factor);
    CHECK(negative.target_ratio == 1.0 - admitted.config.factor);

    const JBeamHydroActuatorAdmission missing =
        RoR::BeamNG::AdmitJBeamHydroActuator(documented, 1U);
    CHECK(!missing.IsAdmitted());
    CHECK(missing.code == JBeamHydroActuatorAdmissionCode::
        HYDRO_INDEX_OUT_OF_RANGE);

    const JBeamAdvancedStructureIR unknown =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\",\"mystery\"],"
            "[\"n1\",\"n2\",7]]"));
    CHECK(unknown.IsValid());
    CHECK(CountDiagnostic(
        unknown,
        JBeamAdvancedDiagnosticCode::UNKNOWN_FIELD_PRESERVED) == 1U);
    CHECK(RoR::BeamNG::AdmitJBeamHydroActuator(unknown, 0U).code ==
        JBeamHydroActuatorAdmissionCode::SOURCE_HAS_DIAGNOSTIC);

    const JBeamAdvancedStructureIR expression =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\",\"factor\"],"
            "[\"n1\",\"n2\",\"$=0.5\"]]"));
    CHECK(expression.IsValid());
    CHECK(expression.hydros[0].entry.behavior ==
        JBeamAdvancedBehavior::PRESERVED_DISABLED_INERT_EXPRESSION);
    CHECK(RoR::BeamNG::AdmitJBeamHydroActuator(expression, 0U).code ==
        JBeamHydroActuatorAdmissionCode::SOURCE_NOT_LITERAL_INVENTORY);

    const JBeamAdvancedStructureIR invalid_config =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\","
            "\"inputInLimit\",\"inputCenter\","
            "\"inputOutLimit\"],[\"n1\",\"n2\",1,0,-1]]"));
    CHECK(invalid_config.IsValid());
    CHECK(RoR::BeamNG::AdmitJBeamHydroActuator(
        invalid_config, 0U).code ==
        JBeamHydroActuatorAdmissionCode::INVALID_ACTUATOR_CONFIG);

    const JBeamAdvancedStructureIR invalid_source =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "[\"missing\",\"n2\"]]"));
    CHECK(!invalid_source.IsValid());
    CHECK(RoR::BeamNG::AdmitJBeamHydroActuator(
        invalid_source, 0U).code ==
        JBeamHydroActuatorAdmissionCode::INVALID_ADVANCED_IR);

    CHECK(std::string(
        RoR::BeamNG::JBeamHydroActuatorAdmissionCodeToString(
            JBeamHydroActuatorAdmissionCode::INVALID_ACTUATOR_CONFIG)) ==
        "invalid-actuator-config");
}

void TestHydroBeamPropertyAdmission()
{
    const JBeamAdvancedStructureIR defaults =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "[\"n1\",\"n2\"]]"));
    const JBeamHydroBeamPropertyAdmission admitted_defaults =
        RoR::BeamNG::AdmitJBeamHydroBeamProperties(defaults, 0U);
    CHECK(admitted_defaults.IsAdmitted());
    CHECK(admitted_defaults.actuator.IsAdmitted());
    CHECK(admitted_defaults.beam.spring == 4300000.0f);
    CHECK(admitted_defaults.beam.damping == 580.0f);
    CHECK(admitted_defaults.beam.deform == 220000.0f);
    CHECK(!admitted_defaults.beam.deform_is_flt_max);
    CHECK(admitted_defaults.beam.strength ==
        std::numeric_limits<float>::max());
    CHECK(admitted_defaults.beam.strength_is_flt_max);
    CHECK(admitted_defaults.beam.precompression == 1.0f);

    const JBeamAdvancedStructureIR explicit_properties =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "{\"beamType\":\"|NORMAL\",\"beamSpring\":8001000,"
            "\"beamDamp\":50,\"beamDeform\":\"FLT_MAX\","
            "\"beamStrength\":125000,\"beamPrecompression\":0.75},"
            "[\"n1\",\"n2\",{\"factor\":0.14}]]"));
    const JBeamHydroBeamPropertyAdmission admitted_explicit =
        RoR::BeamNG::AdmitJBeamHydroBeamProperties(
            explicit_properties, 0U);
    CHECK(admitted_explicit.IsAdmitted());
    CHECK(admitted_explicit.beam.spring == 8001000.0f);
    CHECK(admitted_explicit.beam.damping == 50.0f);
    CHECK(admitted_explicit.beam.deform ==
        std::numeric_limits<float>::max());
    CHECK(admitted_explicit.beam.deform_is_flt_max);
    CHECK(admitted_explicit.beam.strength == 125000.0f);
    CHECK(!admitted_explicit.beam.strength_is_flt_max);
    CHECK(admitted_explicit.beam.precompression == 0.75f);

    const JBeamAdvancedStructureIR bounded =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "{\"beamLongBound\":1},[\"n1\",\"n2\"]]"));
    CHECK(RoR::BeamNG::AdmitJBeamHydroBeamProperties(
        bounded, 0U).code ==
        JBeamHydroBeamPropertyAdmissionCode::
            UNSUPPORTED_BEAM_BEHAVIOR);

    const JBeamAdvancedStructureIR break_group =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "{\"breakGroup\":\"steering\"},[\"n1\",\"n2\"]]"));
    CHECK(RoR::BeamNG::AdmitJBeamHydroBeamProperties(
        break_group, 0U).code ==
        JBeamHydroBeamPropertyAdmissionCode::
            UNSUPPORTED_BEAM_BEHAVIOR);

    const JBeamAdvancedStructureIR wrong_type =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "{\"beamType\":\"BOUNDED\"},[\"n1\",\"n2\"]]"));
    CHECK(RoR::BeamNG::AdmitJBeamHydroBeamProperties(
        wrong_type, 0U).code ==
        JBeamHydroBeamPropertyAdmissionCode::UNSUPPORTED_BEAM_TYPE);

    const JBeamAdvancedStructureIR negative_spring =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "{\"beamSpring\":-1},[\"n1\",\"n2\"]]"));
    CHECK(RoR::BeamNG::AdmitJBeamHydroBeamProperties(
        negative_spring, 0U).code ==
        JBeamHydroBeamPropertyAdmissionCode::INVALID_BEAM_PROPERTY);

    const JBeamAdvancedStructureIR too_large =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "{\"beamSpring\":1e100},[\"n1\",\"n2\"]]"));
    CHECK(RoR::BeamNG::AdmitJBeamHydroBeamProperties(
        too_large, 0U).code ==
        JBeamHydroBeamPropertyAdmissionCode::FLOAT_NARROWING);

    JBeamAdvancedStructureIR duplicate_field = explicit_properties;
    duplicate_field.hydros[0].entry.effective_fields.push_back(
        duplicate_field.hydros[0].entry.effective_fields[0]);
    CHECK(RoR::BeamNG::AdmitJBeamHydroBeamProperties(
        duplicate_field, 0U).code ==
        JBeamHydroBeamPropertyAdmissionCode::
            MALFORMED_EFFECTIVE_FIELD);

    CHECK(std::string(
        RoR::BeamNG::JBeamHydroBeamPropertyAdmissionCodeToString(
            JBeamHydroBeamPropertyAdmissionCode::FLOAT_NARROWING)) ==
        "float-narrowing");
}

void TestHydroRuntimePlan()
{
    const JBeamResolvedGraph graph = ResolveSingle(
        FrameAndNodes() +
        ",\"hydros\":[[\"id1:\",\"id2:\"],"
        "{\"beamSpring\":8001000,\"beamDamp\":50,"
        "\"beamDeform\":220000,\"beamStrength\":125000,"
        "\"beamPrecompression\":0.75},"
        "[\"ref\",\"back\",{\"factor\":0.14,"
        "\"steeringWheelLock\":510,\"inRate\":1.25,"
        "\"outRate\":1.25}]]");
    const JBeamHydroRuntimePlan plan =
        RoR::BeamNG::BuildJBeamHydroRuntimePlan(graph, 0U);
    CHECK(plan.IsAdmitted());
    CHECK(plan.code == JBeamHydroRuntimePlanCode::ADMITTED);
    CHECK(plan.source_hydro_index == 0U);
    CHECK(plan.node1_source_index == 0U);
    CHECK(plan.node2_source_index == 1U);
    CHECK(plan.geometric_length == 1.0);
    CHECK(plan.initial_rest_length == 0.75);
    CHECK(plan.properties.beam.spring == 8001000.0f);
    CHECK(plan.runtime_config.response.factor == 0.14);
    CHECK(plan.runtime_config.has_steering_wheel_lock);
    CHECK(plan.runtime_config.steering_wheel_lock == 510.0);
    CHECK(RoR::IsValidJBeamHydroControlBinding(
        plan.control_binding, plan.runtime_config));
    CHECK(plan.control_binding.source_electrics ==
        RoR::JBEAM_HYDRO_SOURCE_ELECTRICS_STEERING_INPUT);
    CHECK(plan.control_binding.runtime_control_id ==
        RoR::JBEAM_HYDRO_RUNTIME_CONTROL_STEERING_COMMAND);
    CHECK(plan.initialized_runtime.valid);
    CHECK(plan.initialized_runtime.runtime_rest_length == 0.75f);

    const JBeamResolvedGraph unsupported_input = ResolveSingle(
        FrameAndNodes() +
        ",\"hydros\":[[\"id1:\",\"id2:\",\"inputSource\"],"
        "[\"ref\",\"back\",\"tilt\"]]");
    CHECK(RoR::BeamNG::BuildJBeamHydroRuntimePlan(
        unsupported_input, 0U).code ==
        JBeamHydroRuntimePlanCode::UNSUPPORTED_INPUT_SOURCE);

    const JBeamResolvedGraph missing_frame = ResolveSingle(
        Nodes() +
        ",\"hydros\":[[\"id1:\",\"id2:\"],"
        "[\"n1\",\"n2\"]]");
    CHECK(RoR::BeamNG::BuildJBeamHydroRuntimePlan(
        missing_frame, 0U).code ==
        JBeamHydroRuntimePlanCode::INVALID_STRUCTURAL_IR);

    const JBeamResolvedGraph rest_length_overflow = ResolveSingle(
        FrameAndFarNodes() +
        ","
        "\"hydros\":[[\"id1:\",\"id2:\"],"
        "{\"beamPrecompression\":2},[\"farA\",\"farB\"]]");
    const JBeamHydroRuntimePlan overflow_plan =
        RoR::BeamNG::BuildJBeamHydroRuntimePlan(
            rest_length_overflow, 0U);
    CHECK(overflow_plan.code ==
        JBeamHydroRuntimePlanCode::RUNTIME_INITIALIZATION_REJECTED);

    CHECK(std::string(RoR::BeamNG::JBeamHydroRuntimePlanCodeToString(
        JBeamHydroRuntimePlanCode::UNSUPPORTED_INPUT_SOURCE)) ==
        "unsupported-input-source");
}

void TestHydroRuntimePlanSet()
{
    const JBeamResolvedGraph two_hydros = ResolveSingle(
        FrameAndNodes() +
        ",\"hydros\":[[\"id1:\",\"id2:\"],"
        "[\"ref\",\"back\",{\"factor\":0.14}],"
        "[\"left\",\"up\",{\"factor\":-0.2}]]");
    const JBeamHydroRuntimePlanSet admitted =
        RoR::BeamNG::BuildJBeamHydroRuntimePlanSet(two_hydros);
    CHECK(admitted.IsAdmitted());
    CHECK(admitted.code == JBeamHydroRuntimePlanSetCode::ADMITTED);
    CHECK(admitted.source_hydro_count == 2U);
    CHECK(admitted.plans.size() == 2U);
    CHECK(admitted.plans[0].source_hydro_index == 0U);
    CHECK(admitted.plans[1].source_hydro_index == 1U);
    CHECK(admitted.plans[0].runtime_config.response.factor == 0.14);
    CHECK(admitted.plans[1].runtime_config.response.factor == -0.2);

    const JBeamResolvedGraph rejected_row = ResolveSingle(
        FrameAndNodes() +
        ",\"hydros\":[[\"id1:\",\"id2:\"],"
        "[\"ref\",\"back\"],"
        "[\"left\",\"up\",{\"inputSource\":\"tilt\"}]]");
    const JBeamHydroRuntimePlanSet rejected =
        RoR::BeamNG::BuildJBeamHydroRuntimePlanSet(rejected_row);
    CHECK(!rejected.IsAdmitted());
    CHECK(rejected.code == JBeamHydroRuntimePlanSetCode::ROW_REJECTED);
    CHECK(rejected.source_hydro_count == 2U);
    CHECK(rejected.rejected_source_hydro_index == 1U);
    CHECK(rejected.rejected_plan_code ==
        JBeamHydroRuntimePlanCode::UNSUPPORTED_INPUT_SOURCE);
    CHECK(rejected.plans.empty());

    const JBeamResolvedGraph invalid_structural = ResolveSingle(
        Nodes() +
        ",\"hydros\":[[\"id1:\",\"id2:\"],"
        "[\"n1\",\"n2\"]]");
    const JBeamHydroRuntimePlanSet invalid =
        RoR::BeamNG::BuildJBeamHydroRuntimePlanSet(invalid_structural);
    CHECK(invalid.code ==
        JBeamHydroRuntimePlanSetCode::INVALID_STRUCTURAL_IR);
    CHECK(invalid.plans.empty());

    const JBeamHydroRuntimePlanSet empty =
        RoR::BeamNG::BuildJBeamHydroRuntimePlanSet(
            ResolveSingle(FrameAndNodes()));
    CHECK(empty.IsAdmitted());
    CHECK(empty.source_hydro_count == 0U);
    CHECK(empty.plans.empty());

    CHECK(std::string(
        RoR::BeamNG::JBeamHydroRuntimePlanSetCodeToString(
            JBeamHydroRuntimePlanSetCode::ROW_REJECTED)) ==
        "row-rejected");
}

void TestLegacyRailsAndRails2SemanticEquivalence()
{
    const JBeamAdvancedStructureIR legacy =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"rails\":{\"same\":{\"links:\":[\"n1\",\"n2\","
                "\"n4\"],\"broken:\":[],\"looped\":false,"
                "\"capped\":true}}"));
    const JBeamAdvancedStructureIR modern =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"rails2\":[[\"id\",\"links:\",\"broken:\","
                "\"looped\",\"capped\"],"
                "[\"same\",[\"n1\",\"n2\",\"n4\"],[],false,true]]"));
    CHECK(legacy.IsValid());
    CHECK(modern.IsValid());
    CHECK(legacy.rails.size() == 1U);
    CHECK(modern.rails.size() == 1U);
    CHECK(legacy.rails[0].name == modern.rails[0].name);
    CHECK(legacy.rails[0].links == modern.rails[0].links);
    CHECK(legacy.rails[0].looped == modern.rails[0].looped);
    CHECK(legacy.rails[0].capped == modern.rails[0].capped);
    CHECK(legacy.rails[0].has_legacy_broken_links ==
        modern.rails[0].has_legacy_broken_links);
    CHECK(legacy.rails[0].entry.behavior ==
        JBeamAdvancedBehavior::NATIVE_READY_STATIC_GEOMETRY);
    CHECK(modern.rails[0].entry.behavior ==
        JBeamAdvancedBehavior::NATIVE_READY_STATIC_GEOMETRY);
    // Exact authored syntax/provenance remains identity material.
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(
            legacy) !=
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(
            modern));
}

void TestModifiersUnknownsAndOwnership()
{
    JBeamAdvancedStructureIR ir;
    std::string canonical;
    {
        JBeamResolvedGraph graph = ResolveSingle(
            Nodes() +
            ",\"hydros\":[[\"id1:\",\"id2:\"],"
            "{\"inRate\":2,\"unknownBehavior\":{\"gain\":1}},"
            "{\"inRate\":4},"
            "[\"n1\",\"n2\",{\"outRate\":7}]]");
        ir = RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph);
        canonical =
            RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(ir);
        JBeamObjectField* hydros = MutableLastField(
            graph.root->definition.body, "hydros");
        CHECK(hydros != NULL);
        if (hydros != NULL && hydros->value)
        {
            std::shared_ptr<JBeamValue> table =
                std::const_pointer_cast<JBeamValue>(hydros->value);
            table->array_values[2].object_fields.reserve(1000U);
            std::shared_ptr<JBeamValue> rate =
                std::const_pointer_cast<JBeamValue>(
                    table->array_values[2].object_fields[0].value);
            rate->number_value = 99.0;
            rate->scalar_text = "99";
        }
        CHECK(
            RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(ir) ==
            canonical);
    }
    CHECK(ir.IsValid());
    CHECK(ir.modifiers.size() == 2U);
    CHECK(ir.modifiers[0].entry_index == 0U);
    CHECK(ir.modifiers[1].entry_index == 1U);
    CHECK(ir.hydros[0].in_rate == 4.0);
    CHECK(ir.hydros[0].out_rate == 7.0);
    const JBeamAdvancedField* rate =
        FindField(ir.hydros[0].entry.effective_fields, "inRate");
    CHECK(rate != NULL);
    if (rate != NULL)
    {
        CHECK(rate->origin ==
            JBeamAdvancedFieldOrigin::INHERITED_DEFAULT);
        CHECK(static_cast<bool>(rate->raw_value));
        if (rate->raw_value)
        {
            CHECK(rate->raw_value->number_value == 4.0);
        }
    }
    CHECK(CountDiagnostic(
        ir,
        JBeamAdvancedDiagnosticCode::UNKNOWN_FIELD_PRESERVED) == 1U);
}

void TestExpressionsArePreservedDisabled()
{
    const JBeamAdvancedStructureIR ir =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"rails2\":[[\"id\",\"links:\"],"
                "[\"$= $prefix .. 'r'\",[\"n1\",\"n2\"]]]"
                ",\"hydros\":[[\"id1:\",\"id2:\",\"factor\"],"
                "[\"n1\",\"n2\",\"$factor\"]]"
                ",\"thrusters\":[[\"id1:\",\"id2:\",\"control\"],"
                "[\"n1\",\"n2\",\"$control\"]]"));
    CHECK(ir.IsValid());
    CHECK(ir.rails[0].entry.behavior ==
        JBeamAdvancedBehavior::
            PRESERVED_DISABLED_INERT_EXPRESSION);
    CHECK(ir.hydros[0].entry.behavior ==
        JBeamAdvancedBehavior::
            PRESERVED_DISABLED_INERT_EXPRESSION);
    CHECK(ir.thrusters[0].entry.behavior ==
        JBeamAdvancedBehavior::
            PRESERVED_DISABLED_INERT_EXPRESSION);
    CHECK(CountDiagnostic(
        ir,
        JBeamAdvancedDiagnosticCode::EXPRESSION_DISABLED) == 3U);
}

void TestInvalidReferencesTypesAndGeometry()
{
    const JBeamAdvancedStructureIR invalid =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"rails2\":[[\"id\",\"links:\"],"
                "[\"short\",[\"n1\"]],"
                "[\"dup\",[\"n1\",\"n2\"]],"
                "[\"dup\",[\"n3\",\"n4\"]]]"
                ",\"slidenodes\":[[\"id:\",\"railName\"],"
                "[\"missing\",\"none\"]]"
                ",\"hydros\":[[\"id1:\",\"id2:\"],"
                "[\"n1\",\"n1\"]]"
                ",\"thrusters\":[[\"id1:\",\"id2:\",\"control\"],"
                "[\"n1\",\"n2\",42]]"
                ",\"torsionbars\":[[\"id1:\",\"id2:\",\"id3:\","
                "\"id4:\"],[\"n1\",\"n1\",\"n2\",\"n4\"]]"));
    CHECK(!invalid.IsValid());
    CHECK(CountDiagnostic(
        invalid,
        JBeamAdvancedDiagnosticCode::INVALID_RAIL_LINKS) == 1U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamAdvancedDiagnosticCode::DUPLICATE_RAIL_NAME) == 2U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamAdvancedDiagnosticCode::MISSING_RAIL_REFERENCE) == 1U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamAdvancedDiagnosticCode::MISSING_NODE_REFERENCE) >= 1U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamAdvancedDiagnosticCode::DUPLICATE_NODE_REFERENCE) >= 2U);
    CHECK(CountDiagnostic(
        invalid,
        JBeamAdvancedDiagnosticCode::INVALID_FIELD_TYPE) == 1U);
}

void TestHeaderAndInheritedBeamValidation()
{
    const JBeamAdvancedStructureIR missing_header =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"thrusters\":[[\"id1:\",\"id2:\"],"
                "[\"n1\",\"n2\"]]"));
    CHECK(!missing_header.IsValid());
    CHECK(CountDiagnostic(
        missing_header,
        JBeamAdvancedDiagnosticCode::MISSING_REQUIRED_FIELD) == 1U);

    const JBeamAdvancedStructureIR bad_hydro_beam =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"hydros\":[[\"id1:\",\"id2:\"],"
                "{\"beamSpring\":\"not-a-number\"},"
                "[\"n1\",\"n2\"]]"));
    CHECK(!bad_hydro_beam.IsValid());
    CHECK(CountDiagnostic(
        bad_hydro_beam,
        JBeamAdvancedDiagnosticCode::INVALID_FIELD_TYPE) == 1U);

    const JBeamAdvancedStructureIR bad_broken =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"rails\":{\"r\":{\"links:\":[\"n1\",\"n2\"],"
                "\"broken:\":[42]}}"));
    CHECK(!bad_broken.IsValid());
    CHECK(CountDiagnostic(
        bad_broken,
        JBeamAdvancedDiagnosticCode::INVALID_FIELD_TYPE) == 1U);

    const JBeamAdvancedStructureIR undocumented_flt_max =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                Nodes() +
                ",\"torsionbars\":[[\"id1:\",\"id2:\",\"id3:\","
                "\"id4:\",\"strength\"],"
                "[\"n3\",\"n1\",\"n2\",\"n4\",\"FLT_MAX\"]]"));
    CHECK(!undocumented_flt_max.IsValid());
    CHECK(CountDiagnostic(
        undocumented_flt_max,
        JBeamAdvancedDiagnosticCode::INVALID_FIELD_TYPE) == 1U);
}

void TestCollinearTorsionAndCoincidentRail()
{
    const JBeamAdvancedStructureIR invalid =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            ResolveSingle(
                "\"nodes\":[[\"id\",\"posX\",\"posY\",\"posZ\"],"
                "[\"a\",0,0,0],[\"b\",1,0,0],"
                "[\"c\",2,0,0],[\"d\",3,0,0],"
                "[\"e\",0,0,0]]"
                ",\"rails2\":[[\"id\",\"links:\"],"
                "[\"same\",[\"a\",\"e\"]]]"
                ",\"torsionbars\":[[\"id1:\",\"id2:\",\"id3:\","
                "\"id4:\"],[\"a\",\"b\",\"c\",\"d\"]]"));
    CHECK(!invalid.IsValid());
    CHECK(CountDiagnostic(
        invalid,
        JBeamAdvancedDiagnosticCode::DEGENERATE_NODE_GEOMETRY) == 2U);
}

void TestNonFiniteDefenseUnderFastMath()
{
    const std::uint64_t hostile[] = {
        UINT64_C(0x7ff8000000000001),
        UINT64_C(0x7ff0000000000000),
        UINT64_C(0xfff0000000000000)
    };
    for (std::size_t i = 0U;
         i < sizeof(hostile) / sizeof(hostile[0]);
         ++i)
    {
        JBeamResolvedGraph graph = ResolveSingle(
            Nodes() +
            ",\"thrusters\":[[\"id1:\",\"id2:\",\"factor\","
            "\"control\"],[\"n1\",\"n2\",10,\"jato\"]]");
        JBeamObjectField* section = MutableLastField(
            graph.root->definition.body, "thrusters");
        CHECK(section != NULL);
        if (section != NULL && section->value)
        {
            std::shared_ptr<JBeamValue> table =
                std::const_pointer_cast<JBeamValue>(section->value);
            table->array_values[1].array_values[2].number_value =
                DoubleFromBits(hostile[i]);
        }
        const JBeamAdvancedStructureIR ir =
            RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph);
        CHECK(!ir.IsValid());
        CHECK(CountDiagnostic(
            ir,
            JBeamAdvancedDiagnosticCode::NON_FINITE_NUMBER) == 1U);
    }
}

void TestLimitsAndCyclicValues()
{
    const JBeamResolvedGraph graph = ResolveSingle(
        Nodes() +
        ",\"thrusters\":[[\"id1:\",\"id2:\",\"control\"],"
        "[\"n1\",\"n2\",\"a\"],[\"n3\",\"n4\",\"b\"]]");
    JBeamAdvancedLimits exact;
    exact.max_entries = 2U;
    const JBeamAdvancedStructureIR admitted =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, exact);
    CHECK(admitted.IsValid());
    CHECK(admitted.authored_entry_count == 2U);

    JBeamAdvancedLimits short_limit = exact;
    short_limit.max_entries = 1U;
    const JBeamAdvancedStructureIR bounded =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, short_limit);
    CHECK(!bounded.IsValid());
    CHECK(CountDiagnostic(
        bounded,
        JBeamAdvancedDiagnosticCode::ENTRY_LIMIT) == 1U);

    JBeamAdvancedLimits no_diagnostics;
    no_diagnostics.max_diagnostics = 0U;
    no_diagnostics.max_entries = 0U;
    const JBeamAdvancedStructureIR terminal =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(
            graph, no_diagnostics);
    CHECK(!terminal.IsValid());
    CHECK(terminal.diagnostics.size() == 1U);
    CHECK(terminal.diagnostics[0].code ==
        JBeamAdvancedDiagnosticCode::DIAGNOSTIC_LIMIT);

    JBeamAdvancedLimits tiny_bytes;
    tiny_bytes.max_retained_bytes = 1U;
    const JBeamAdvancedStructureIR bytes =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, tiny_bytes);
    CHECK(!bytes.IsValid());
    CHECK(CountDiagnostic(
        bytes,
        JBeamAdvancedDiagnosticCode::RETAINED_BYTE_LIMIT) == 1U);

    JBeamAdvancedStructureIR canonical = admitted;
    canonical.canonical_output_byte_limit = 8U;
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(
            canonical).empty());
    canonical = admitted;
    canonical.canonical_work_unit_limit = 1U;
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(
            canonical).empty());

    JBeamResolvedGraph cyclic = ResolveSingle(
        Nodes() +
        ",\"hydros\":[[\"id1:\",\"id2:\"],"
        "[\"n1\",\"n2\"]]");
    std::shared_ptr<JBeamValue> injected_cycle;
    JBeamObjectField* section = MutableLastField(
        cyclic.root->definition.body, "hydros");
    CHECK(section != NULL);
    if (section != NULL && section->value)
    {
        std::shared_ptr<JBeamValue> table =
            std::const_pointer_cast<JBeamValue>(section->value);
        std::shared_ptr<JBeamValue> self(new JBeamValue());
        self->type = JBeamValueType::OBJECT;
        JBeamObjectField loop;
        loop.key = "self";
        loop.value = self;
        self->object_fields.push_back(loop);
        injected_cycle = self;
        table->array_values.insert(
            table->array_values.begin() + 1U, *self);
    }
    const JBeamAdvancedStructureIR rejected =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(cyclic);
    CHECK(!rejected.IsValid());
    CHECK(CountDiagnostic(
        rejected,
        JBeamAdvancedDiagnosticCode::PRESERVED_VALUE_LIMIT) == 1U);
    // The cycle is intentional hostile input for the traversal guard, but it
    // must not remain owned after the assertion or LeakSanitizer would report
    // the test fixture itself as an engine leak.
    if (injected_cycle)
    {
        injected_cycle->object_fields.clear();
    }
}

void TestResolvedGraphDepthAndCycleBounds()
{
    JBeamResolvedGraph deep = ResolveSingle(AllOfficialSections());
    std::shared_ptr<JBeamResolvedPartNode> child1(
        new JBeamResolvedPartNode(*deep.root));
    child1->slots.clear();
    std::shared_ptr<JBeamResolvedPartNode> child2(
        new JBeamResolvedPartNode(*deep.root));
    child2->slots.clear();
    JBeamResolvedSlot first_edge;
    first_edge.child = child1;
    deep.root->slots.push_back(first_edge);
    JBeamResolvedSlot second_edge;
    second_edge.child = child2;
    child1->slots.push_back(second_edge);
    JBeamAdvancedLimits shallow;
    shallow.max_graph_depth = 2U;
    const JBeamAdvancedStructureIR depth_rejected =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(deep, shallow);
    CHECK(!depth_rejected.IsValid());
    CHECK(CountDiagnostic(
        depth_rejected,
        JBeamAdvancedDiagnosticCode::RESOLVED_GRAPH_DEPTH_LIMIT) == 1U);

    JBeamResolvedGraph cycle = ResolveSingle(AllOfficialSections());
    JBeamResolvedSlot cycle_edge;
    cycle_edge.child = cycle.root;
    cycle.root->slots.push_back(cycle_edge);
    const JBeamAdvancedStructureIR cycle_rejected =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(cycle);
    CHECK(!cycle_rejected.IsValid());
    CHECK(CountDiagnostic(
        cycle_rejected,
        JBeamAdvancedDiagnosticCode::RESOLVED_GRAPH_CYCLE) == 1U);
    // Break the deliberately injected shared-ownership cycle after the
    // rejection behavior has been observed.
    cycle.root->slots.pop_back();
}

void TestExactAggregateBoundaries()
{
    const JBeamResolvedGraph graph =
        ResolveSingle(AllOfficialSections());
    const JBeamAdvancedStructureIR baseline =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph);
    CHECK(baseline.IsValid());
    CHECK(baseline.node_coordinate_row_count == 8U);
    CHECK(baseline.effective_field_count > 0U);
    CHECK(baseline.retained_byte_count > 0U);
    CHECK(baseline.work_unit_count > 0U);
    CHECK(baseline.preserved_value_work_unit_count > 0U);

    JBeamAdvancedLimits exact;
    exact.max_node_coordinates = baseline.node_coordinate_row_count;
    exact.max_effective_fields = baseline.effective_field_count;
    exact.max_retained_bytes = baseline.retained_byte_count;
    exact.max_work_units = baseline.work_unit_count;
    exact.max_preserved_value_work_units =
        baseline.preserved_value_work_unit_count;
    const JBeamAdvancedStructureIR admitted =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, exact);
    CHECK(admitted.IsValid());
    CHECK(admitted.retained_byte_count ==
        baseline.retained_byte_count);
    CHECK(admitted.work_unit_count == baseline.work_unit_count);

    JBeamAdvancedLimits node_short;
    node_short.max_node_coordinates =
        baseline.node_coordinate_row_count - 1U;
    const JBeamAdvancedStructureIR node_rejected =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, node_short);
    CHECK(!node_rejected.IsValid());
    CHECK(CountDiagnostic(
        node_rejected,
        JBeamAdvancedDiagnosticCode::NODE_COORDINATE_LIMIT) == 1U);

    JBeamAdvancedLimits field_short;
    field_short.max_effective_fields =
        baseline.effective_field_count - 1U;
    const JBeamAdvancedStructureIR field_rejected =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, field_short);
    CHECK(!field_rejected.IsValid());
    CHECK(CountDiagnostic(
        field_rejected,
        JBeamAdvancedDiagnosticCode::EFFECTIVE_FIELD_LIMIT) == 1U);

    JBeamAdvancedLimits byte_short;
    byte_short.max_retained_bytes =
        baseline.retained_byte_count - 1U;
    const JBeamAdvancedStructureIR byte_rejected =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, byte_short);
    CHECK(!byte_rejected.IsValid());
    CHECK(CountDiagnostic(
        byte_rejected,
        JBeamAdvancedDiagnosticCode::RETAINED_BYTE_LIMIT) == 1U);

    JBeamAdvancedLimits work_short;
    work_short.max_work_units = baseline.work_unit_count - 1U;
    const JBeamAdvancedStructureIR work_rejected =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, work_short);
    CHECK(!work_rejected.IsValid());
    CHECK(CountDiagnostic(
        work_rejected,
        JBeamAdvancedDiagnosticCode::WORK_LIMIT) == 1U);

    JBeamAdvancedLimits value_short;
    value_short.max_preserved_value_work_units =
        baseline.preserved_value_work_unit_count - 1U;
    const JBeamAdvancedStructureIR value_rejected =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(graph, value_short);
    CHECK(!value_rejected.IsValid());
    CHECK(CountDiagnostic(
        value_rejected,
        JBeamAdvancedDiagnosticCode::PRESERVED_VALUE_LIMIT) == 1U);
}

void TestCanonicalIdentityIgnoresCapacityAndPackageEnumeration()
{
    JBeamResolvedGraph ordinary = ResolveSingle(AllOfficialSections());
    JBeamResolvedGraph reserved = ResolveSingle(AllOfficialSections());
    JBeamObjectField* rails = MutableLastField(
        reserved.root->definition.body, "rails2");
    CHECK(rails != NULL);
    if (rails != NULL && rails->value)
    {
        std::shared_ptr<JBeamValue> table =
            std::const_pointer_cast<JBeamValue>(rails->value);
        table->array_values.reserve(2048U);
        table->array_values[0].array_values.reserve(2048U);
    }
    const JBeamAdvancedStructureIR first =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(ordinary);
    JBeamAdvancedStructureIR second =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(reserved);
    CHECK(first.IsValid());
    CHECK(second.IsValid());
    second.source_records.reserve(2048U);
    second.rails.reserve(2048U);
    second.diagnostics.reserve(2048U);
    CHECK(first.retained_byte_count == second.retained_byte_count);
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(first) ==
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(second));

    const std::string root =
        "{\"root\":{\"slotType\":\"main\","
        "\"slots\":[[\"type\",\"default\",\"description\"],"
        "[\"addon\",\"addon_part\",\"Addon\"]]," +
        Nodes() +
        ",\"rails2\":[[\"id\",\"links:\"],"
        "[\"rootRail\",[\"n1\",\"n2\"]]]}}";
    const std::string addon =
        "{\"addon_part\":{\"slotType\":\"addon\","
        "\"nodes\":[[\"id\",\"posX\",\"posY\",\"posZ\"],"
        "[\"a1\",0,2,0],[\"a2\",1,2,0]],"
        "\"rails2\":[[\"id\",\"links:\"],"
        "[\"addonRail\",[\"a1\",\"a2\"]]]}}";
    std::vector<JBeamPackageSource> forward;
    forward.push_back(Package("vehicles/root.jbeam", root));
    forward.push_back(Package("vehicles/addon.jbeam", addon));
    std::vector<JBeamPackageSource> reverse;
    reverse.push_back(Package("vehicles/addon.jbeam", addon));
    reverse.push_back(Package("vehicles/root.jbeam", root));
    const JBeamAdvancedStructureIR a =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(Resolve(forward));
    const JBeamAdvancedStructureIR b =
        RoR::BeamNG::BuildJBeamAdvancedStructureIR(Resolve(reverse));
    CHECK(a.IsValid());
    CHECK(b.IsValid());
    CHECK(
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(a) ==
        RoR::BeamNG::SerializeCanonicalJBeamAdvancedStructureIR(b));
}

void TestDiagnosticNames()
{
    CHECK(std::string(
        RoR::BeamNG::JBeamAdvancedDiagnosticCodeToString(
            JBeamAdvancedDiagnosticCode::EXPRESSION_DISABLED)) ==
        "expression-disabled");
    CHECK(std::string(
        RoR::BeamNG::JBeamAdvancedDiagnosticCodeToString(
            JBeamAdvancedDiagnosticCode::MISSING_RAIL_REFERENCE)) ==
        "missing-rail-reference");
}

} // namespace

int main()
{
    TestDocumentationProfile();
    TestOfficialExamplesAndClassification();
    TestDefaultsAndDependentDefaults();
    TestHydroActuatorAdmission();
    TestHydroBeamPropertyAdmission();
    TestHydroRuntimePlan();
    TestHydroRuntimePlanSet();
    TestLegacyRailsAndRails2SemanticEquivalence();
    TestModifiersUnknownsAndOwnership();
    TestExpressionsArePreservedDisabled();
    TestInvalidReferencesTypesAndGeometry();
    TestHeaderAndInheritedBeamValidation();
    TestCollinearTorsionAndCoincidentRail();
    TestNonFiniteDefenseUnderFastMath();
    TestLimitsAndCyclicValues();
    TestResolvedGraphDepthAndCycleBounds();
    TestExactAggregateBoundaries();
    TestCanonicalIdentityIgnoresCapacityAndPackageEnumeration();
    TestDiagnosticNames();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JBeamAdvancedStructureIR tests passed\n";
    return EXIT_SUCCESS;
}
