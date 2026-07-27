#include "JBeamToRigDef.h"

#if defined(ROR_JBEAM_TO_RIGDEF_FULL_TEST)
// Keep this focused target independent from Actor/CacheSystem. The layout and
// conversion code use the production RigDef header; these narrow definitions
// provide only the constructors and intrusive-pointer endpoint it consumes.
namespace RoR {
class CacheEntry
{
public:
    void AddRef() {}
    void Release() {}
};
}

#include "RigDef_File.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#if defined(ROR_JBEAM_TO_RIGDEF_FULL_TEST)
namespace RigDef {

const char* ROOT_MODULE_NAME = "_Root_";

Node::Id::Id()
    : m_id_num(0U)
    , m_flags(0U)
{
}

Node::Id::Id(const std::string& id)
    : m_id_num(0U)
    , m_flags(0U)
{
    setStr(id);
}

void Node::Id::setStr(const std::string& id)
{
    m_id_num = 0U;
    m_id_str = id;
    BITMASK_SET_0(m_flags, IS_TYPE_NUMBERED);
    BITMASK_SET_1(m_flags, IS_TYPE_NAMED | IS_VALID);
}

Node::Ref::Ref()
    : m_id_as_number(0U)
    , m_flags(0U)
    , m_line_number(0U)
{
}

Node::Ref::Ref(
    const std::string& id,
    unsigned int number,
    unsigned int flags,
    unsigned int line)
    : m_id(id)
    , m_id_as_number(number)
    , m_flags(0U)
    , m_line_number(line)
{
    BITMASK_SET_1(m_flags, flags);
}

NodeDefaults::NodeDefaults()
    : load_weight(-1.0f)
    , friction(1.0f)
    , volume(1.0f)
    , surface(1.0f)
    , options(0U)
{
}

Document::Module::Module(const Ogre::String& module_name)
    : name(module_name)
{
}

Document::Document()
    : hide_in_chooser(false)
    , enable_advanced_deformation(false)
    , slide_nodes_connect_instantly(false)
    , rollon(false)
    , forward_commands(false)
    , import_commands(false)
    , lockgroup_default_nolock(false)
    , rescuer(false)
    , disable_default_sounds(false)
    , root_module(std::make_shared<Document::Module>(ROOT_MODULE_NAME))
{
}

} // namespace RigDef
#endif

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

double Binary64FromBits(std::uint64_t bits)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t),
        "hostile-value tests require binary64 doubles");
    static_assert(std::numeric_limits<double>::is_iec559,
        "hostile-value tests require IEC 60559 doubles");

    double value = 0.0;
    const volatile unsigned char* const source =
        reinterpret_cast<const volatile unsigned char*>(&bits);
    unsigned char* const destination =
        reinterpret_cast<unsigned char*>(&value);
    for (std::size_t i = 0U; i < sizeof(value); ++i)
    {
        destination[i] = source[i];
    }
    return value;
}

using RoR::BeamNG::JBeamRigDefBeamPlan;
using RoR::BeamNG::JBeamStructuralBeam;
using RoR::BeamNG::JBeamStructuralBeamStatus;
using RoR::BeamNG::JBeamStructuralDiagnostic;
using RoR::BeamNG::JBeamStructuralDiagnosticCode;
using RoR::BeamNG::JBeamStructuralIR;
using RoR::BeamNG::JBeamStructuralNode;
using RoR::BeamNG::JBeamStructuralPartIdentity;
using RoR::BeamNG::JBeamStructuralProvenance;
using RoR::BeamNG::JBeamStructuralRefFrame;
using RoR::BeamNG::JBeamStructuralSeverity;
using RoR::BeamNG::JBeamStructuralTriangle;
using RoR::BeamNG::JBeamStructuralTriangleOrigin;
using RoR::BeamNG::JBeamStructuralTriangleStatus;
using RoR::BeamNG::JBeamToRigDefDiagnostic;
using RoR::BeamNG::JBeamToRigDefDiagnosticCode;
using RoR::BeamNG::JBeamToRigDefLimits;
using RoR::BeamNG::JBeamToRigDefPreflightResult;

JBeamStructuralProvenance Provenance(
    const std::string& source_name,
    std::uint64_t line)
{
    JBeamStructuralProvenance provenance;
    std::shared_ptr<JBeamStructuralPartIdentity> part(
        new JBeamStructuralPartIdentity());
    part->part_preorder_index = 0U;
    part->part_name = "test_vehicle";
    part->package_path = source_name;
    provenance.part = part;
    provenance.source_name =
        std::shared_ptr<const std::string>(
            new std::string(source_name));
    provenance.begin.byte_offset = line * 10U;
    provenance.begin.line = line;
    provenance.begin.column = 1U;
    provenance.end = provenance.begin;
    provenance.end.column = 2U;
    return provenance;
}

JBeamStructuralNode Node(
    const std::string& id,
    double x,
    double y,
    double z,
    double mass,
    std::uint64_t line)
{
    JBeamStructuralNode node = JBeamStructuralNode();
    node.id = id;
    node.x = x;
    node.y = y;
    node.z = z;
    node.node_weight = mass;
    node.node_weight_authored = true;
    node.provenance = Provenance("vehicles/test/main.jbeam", line);
    return node;
}

JBeamStructuralBeam Beam(
    const JBeamStructuralIR& ir,
    std::size_t first,
    std::size_t second,
    std::uint64_t line)
{
    JBeamStructuralBeam beam;
    beam.node_a = ir.nodes[first].id;
    beam.node_b = ir.nodes[second].id;
    beam.node_a_index = first;
    beam.node_b_index = second;
    beam.beam_type = "NORMAL";
    beam.status = JBeamStructuralBeamStatus::ENABLED;
    beam.provenance = Provenance(
        "vehicles/test/main.jbeam", line);
    return beam;
}

JBeamStructuralTriangle Triangle(
    const JBeamStructuralIR& ir,
    std::size_t first,
    std::size_t second,
    std::size_t third,
    std::uint64_t line)
{
    JBeamStructuralTriangle triangle =
        JBeamStructuralTriangle();
    triangle.node_a = ir.nodes[first].id;
    triangle.node_b = ir.nodes[second].id;
    triangle.node_c = ir.nodes[third].id;
    triangle.node_a_index = first;
    triangle.node_b_index = second;
    triangle.node_c_index = third;
    triangle.optional = false;
    triangle.status = JBeamStructuralTriangleStatus::ENABLED;
    triangle.origin = JBeamStructuralTriangleOrigin::TRIANGLE;
    triangle.authored_row_index = 0U;
    triangle.provenance = Provenance(
        "vehicles/test/main.jbeam", line);
    return triangle;
}

JBeamStructuralIR ValidIR()
{
    JBeamStructuralIR ir;
    // Ref is deliberately not first: the adapter must reorder it.
    ir.nodes.push_back(Node("back", 0.0, 1.0, 0.0, 2.0, 10U));
    ir.nodes.push_back(Node("ref", 0.0, 0.0, 0.0, 1.0, 11U));
    ir.nodes.push_back(Node("left", 1.0, 0.0, 0.0, 3.0, 12U));
    ir.nodes.push_back(Node("up", 0.0, 0.0, 1.0, 4.0, 13U));
    ir.nodes.push_back(Node(
        "leftCorner", 1.0, -1.0, 0.0, 5.0, 14U));
    ir.nodes.push_back(Node(
        "rightCorner", -1.0, -1.0, 0.0, 6.0, 15U));
    ir.nodes.push_back(Node("extra", 0.0, -2.0, 1.0, 7.0, 16U));

    JBeamStructuralRefFrame frame = JBeamStructuralRefFrame();
    frame.reference = "ref";
    frame.back = "back";
    frame.left = "left";
    frame.up = "up";
    frame.left_corner = "leftCorner";
    frame.right_corner = "rightCorner";
    frame.reference_index = 1U;
    frame.back_index = 0U;
    frame.left_index = 2U;
    frame.up_index = 3U;
    frame.left_corner_index = 4U;
    frame.right_corner_index = 5U;
    frame.provenance = Provenance(
        "vehicles/test/main.jbeam", 20U);
    ir.ref_frame = frame;
    ir.has_ref_frame = true;

    ir.beams.push_back(Beam(ir, 1U, 0U, 30U));
    JBeamStructuralBeam explicit_beam =
        Beam(ir, 2U, 3U, 31U);
    explicit_beam.has_spring = true;
    explicit_beam.spring = 123.5;
    explicit_beam.has_damping = true;
    explicit_beam.damping = 9.0;
    explicit_beam.has_deform = true;
    explicit_beam.deform_unbounded = true;
    explicit_beam.has_strength = true;
    explicit_beam.strength = 77.0;
    explicit_beam.has_precompression = true;
    explicit_beam.precompression = 0.75;
    ir.beams.push_back(explicit_beam);

    JBeamStructuralBeam disabled_special =
        Beam(ir, 0U, 6U, 32U);
    disabled_special.beam_type = "|BOUNDED";
    disabled_special.status =
        JBeamStructuralBeamStatus::
            PRESERVED_DISABLED_SPECIAL_TYPE;
    ir.beams.push_back(disabled_special);

    JBeamStructuralBeam disabled_optional =
        Beam(ir, 6U, 0U, 33U);
    disabled_optional.node_b = "missing";
    disabled_optional.node_b_index =
        std::numeric_limits<std::size_t>::max();
    disabled_optional.optional = true;
    disabled_optional.status =
        JBeamStructuralBeamStatus::
            PRESERVED_DISABLED_OPTIONAL_REFERENCE;
    ir.beams.push_back(disabled_optional);

    ir.triangles.push_back(Triangle(ir, 1U, 0U, 2U, 40U));
    JBeamStructuralTriangle disabled_triangle =
        Triangle(ir, 1U, 0U, 2U, 41U);
    disabled_triangle.node_c = "missing_surface_node";
    disabled_triangle.node_c_index =
        std::numeric_limits<std::size_t>::max();
    disabled_triangle.optional = true;
    disabled_triangle.status =
        JBeamStructuralTriangleStatus::
            PRESERVED_DISABLED_OPTIONAL_REFERENCE;
    ir.triangles.push_back(disabled_triangle);
    return ir;
}

const JBeamToRigDefDiagnostic* FindDiagnostic(
    const JBeamToRigDefPreflightResult& result,
    JBeamToRigDefDiagnosticCode code)
{
    for (std::size_t i = 0U;
         i < result.diagnostics.size();
         ++i)
    {
        if (result.diagnostics[i].code == code)
        {
            return &result.diagnostics[i];
        }
    }
    return NULL;
}

bool Near(float first, float second, float tolerance)
{
    return std::fabs(first - second) <= tolerance;
}

void TestValidPlanUsesSpawnSemantics()
{
    const JBeamStructuralIR ir = ValidIR();
    const JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(ir, "test-car");
    CHECK(result.IsValid());
    CHECK(result.diagnostics.empty());
    CHECK(result.node_source_order.size() == 7U);
    CHECK(result.node_source_order[0] == 1U);
    CHECK(result.node_source_order[1] == 0U);
    CHECK(result.node_source_order[6] == 6U);

    // The single boundary transform is BeamNG (x,y,z) -> RoR (y,z,x).
    CHECK(result.transformed_nodes[0].x == 1.0f);
    CHECK(result.transformed_nodes[0].y == 0.0f);
    CHECK(result.transformed_nodes[0].z == 0.0f);
    CHECK(result.transformed_nodes[2].x == 0.0f);
    CHECK(result.transformed_nodes[2].y == 0.0f);
    CHECK(result.transformed_nodes[2].z == 1.0f);
    CHECK(result.transformed_nodes[6].x == -2.0f);
    CHECK(result.transformed_nodes[6].y == 1.0f);
    CHECK(result.transformed_nodes[6].z == 0.0f);
    CHECK(result.node_masses[0] == 2.0f);
    CHECK(result.node_masses[6] == 7.0f);

    CHECK(result.beams.size() == 2U);
    CHECK(result.beams[0].source_index == 0U);
    CHECK(result.beams[0].spring ==
        RoR::BeamNG::JBEAM_RIGDEF_DEFAULT_BEAM_SPRING);
    CHECK(result.beams[0].damping ==
        RoR::BeamNG::JBEAM_RIGDEF_DEFAULT_BEAM_DAMPING);
    CHECK(result.beams[0].deform ==
        RoR::BeamNG::JBEAM_RIGDEF_DEFAULT_BEAM_DEFORM);
    CHECK(result.beams[0].strength ==
        std::numeric_limits<float>::max());
    CHECK(result.beams[0].rest_length_scale == 1.0f);
    CHECK(result.beams[0].geometric_length == 1.0f);
    CHECK(result.beams[0].scaled_rest_length == 1.0f);

    const JBeamRigDefBeamPlan& explicit_plan = result.beams[1];
    CHECK(explicit_plan.source_index == 1U);
    CHECK(explicit_plan.spring == 123.5f);
    CHECK(explicit_plan.damping == 9.0f);
    CHECK(explicit_plan.deform ==
        std::numeric_limits<float>::max());
    CHECK(explicit_plan.strength == 77.0f);
    CHECK(explicit_plan.rest_length_scale == 0.75f);
    CHECK(Near(
        explicit_plan.scaled_rest_length,
        explicit_plan.geometric_length * 0.75f,
        1.0e-6f));

    CHECK(result.triangle_source_indices.size() == 1U);
    CHECK(result.triangle_source_indices[0] == 0U);
    CHECK(result.metrics.enabled_beam_count == 2U);
    CHECK(result.metrics.enabled_cab_triangle_count == 1U);
    CHECK(Near(
        result.metrics.runtime_total_beam_length,
        result.beams[0].scaled_rest_length +
            result.beams[1].scaled_rest_length,
        1.0e-6f));
    CHECK(result.metrics.total_mass_kg == 28.0);
    CHECK(result.metrics.runtime_total_mass_kg == 28.0f);
    CHECK(result.metrics.bounds_min.x == -2.0f);
    CHECK(result.metrics.bounds_min.y == 0.0f);
    CHECK(result.metrics.bounds_min.z == -1.0f);
    CHECK(result.metrics.bounds_max.x == 1.0f);
    CHECK(result.metrics.bounds_max.y == 1.0f);
    CHECK(result.metrics.bounds_max.z == 1.0f);
    CHECK(Near(
        result.metrics.center_of_mass.x,
        -23.0f / 28.0f,
        1.0e-6f));
    CHECK(Near(
        result.metrics.center_of_mass.y,
        11.0f / 28.0f,
        1.0e-6f));
    CHECK(Near(
        result.metrics.center_of_mass.z,
        2.0f / 28.0f,
        1.0e-6f));
}

void TestStateAndReferenceInvariantsFailClosed()
{
    JBeamStructuralIR bad_reference = ValidIR();
    bad_reference.beams[0].node_b_index = 2U;
    JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            bad_reference, "bad-reference");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_NODE_REFERENCE) != NULL);
    CHECK(result.node_source_order.empty());
    CHECK(result.transformed_nodes.empty());
    CHECK(result.beams.empty());

    JBeamStructuralIR bad_optional = ValidIR();
    bad_optional.beams[3].node_b = "back";
    bad_optional.beams[3].node_b_index = 0U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        bad_optional, "bad-optional");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE) != NULL);

    JBeamStructuralIR bad_special = ValidIR();
    bad_special.beams[2].beam_type = "NORMAL";
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        bad_special, "bad-special");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE) != NULL);

    JBeamStructuralIR bad_triangle = ValidIR();
    bad_triangle.triangles[1].node_c = "left";
    bad_triangle.triangles[1].node_c_index = 2U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        bad_triangle, "bad-triangle-state");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE) != NULL);
}

void TestFloatNarrowingAndRuntimeTopology()
{
    JBeamStructuralIR coordinate_overflow = ValidIR();
    coordinate_overflow.nodes[6].x =
        std::numeric_limits<double>::max();
    JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            coordinate_overflow, "coordinate-overflow");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::FLOAT_NARROWING) != NULL);

    JBeamStructuralIR mass_overflow = ValidIR();
    mass_overflow.nodes[6].node_weight =
        std::numeric_limits<double>::max();
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        mass_overflow, "mass-overflow");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_NODE_MASS) != NULL);

    JBeamStructuralIR collapsed_beam = ValidIR();
    collapsed_beam.nodes.push_back(Node(
        "roundA", 16777216.0, 0.0, 0.0, 1.0, 50U));
    collapsed_beam.nodes.push_back(Node(
        "roundB", 16777217.0, 0.0, 0.0, 1.0, 51U));
    collapsed_beam.beams.push_back(Beam(
        collapsed_beam,
        collapsed_beam.nodes.size() - 2U,
        collapsed_beam.nodes.size() - 1U,
        52U));
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        collapsed_beam, "collapsed-beam");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::DEGENERATE_BEAM) != NULL);

    JBeamStructuralIR subnormal_area = ValidIR();
    subnormal_area.nodes.push_back(Node(
        "tinyX", 1.0e-20, 0.0, 0.0, 1.0, 53U));
    subnormal_area.nodes.push_back(Node(
        "tinyY", 0.0, 1.0e-20, 0.0, 1.0, 54U));
    subnormal_area.triangles.push_back(Triangle(
        subnormal_area,
        1U,
        subnormal_area.nodes.size() - 2U,
        subnormal_area.nodes.size() - 1U,
        55U));
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        subnormal_area, "subnormal-area");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::DEGENERATE_TRIANGLE) != NULL);
}

void TestMassCenterAndBoundsPreflight()
{
    JBeamStructuralIR total_overflow = ValidIR();
    for (std::size_t i = 0U; i < total_overflow.nodes.size(); ++i)
    {
        total_overflow.nodes[i].node_weight =
            static_cast<double>(std::numeric_limits<float>::max());
    }
    JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            total_overflow, "total-overflow");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::TOTAL_MASS_OVERFLOW) != NULL);

    JBeamStructuralIR bounds_overflow = ValidIR();
    bounds_overflow.nodes.push_back(Node(
        "positiveLimit",
        static_cast<double>(std::numeric_limits<float>::max()),
        0.0,
        0.0,
        1.0,
        60U));
    bounds_overflow.nodes.push_back(Node(
        "negativeLimit",
        -static_cast<double>(std::numeric_limits<float>::max()),
        0.0,
        0.0,
        1.0,
        61U));
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        bounds_overflow, "bounds-overflow");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_BOUNDS) != NULL);
}

void TestCameraFrameIsReverifiedAfterNarrowing()
{
    JBeamStructuralIR collapsed = ValidIR();
    const double offset = 1.0e20;
    for (std::size_t i = 0U; i < 6U; ++i)
    {
        collapsed.nodes[i].x += offset;
        collapsed.nodes[i].y += offset;
        collapsed.nodes[i].z += offset;
    }
    JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            collapsed, "collapsed-frame");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_REF_FRAME) != NULL);

    JBeamStructuralIR wrong_up = ValidIR();
    wrong_up.nodes[3].z = -1.0;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        wrong_up, "wrong-up");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::MISALIGNED_REF_FRAME) != NULL);

    JBeamStructuralIR wrong_corner = ValidIR();
    wrong_corner.nodes[4].x = -1.0;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        wrong_corner, "wrong-corner");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::MISALIGNED_REF_CORNERS) != NULL);

    JBeamStructuralIR runtime_underflow = ValidIR();
    const double runtime_min =
        static_cast<double>(std::numeric_limits<float>::min());
    for (std::size_t i = 0U; i < 6U; ++i)
    {
        runtime_underflow.nodes[i].x *= runtime_min;
        runtime_underflow.nodes[i].y *= runtime_min;
        runtime_underflow.nodes[i].z *= runtime_min;
    }
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        runtime_underflow, "runtime-frame-underflow");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_REF_FRAME) != NULL);

    JBeamStructuralIR corner_dot_underflow = ValidIR();
    corner_dot_underflow.nodes[0].y = 0.5;
    corner_dot_underflow.nodes[4].y = -runtime_min;
    corner_dot_underflow.nodes[5].y = -runtime_min;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        corner_dot_underflow, "runtime-corner-dot-underflow");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::MISALIGNED_REF_CORNERS) != NULL);
}

void TestAllAdmissionLimitsApplyBeforePlans()
{
    const JBeamStructuralIR ir = ValidIR();
    JBeamToRigDefLimits exact;
    exact.max_nodes = ir.nodes.size();
    exact.max_beams = 2U;
    exact.max_cab_triangles = 1U;
    const JBeamToRigDefPreflightResult exact_result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            ir, "exact-limits", exact);
    CHECK(exact_result.IsValid());
    CHECK(exact_result.node_source_order.size() == exact.max_nodes);
    CHECK(exact_result.beams.size() == exact.max_beams);
    CHECK(exact_result.triangle_source_indices.size() ==
        exact.max_cab_triangles);

    JBeamToRigDefLimits limits;
    limits.max_nodes = 6U;
    JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            ir, "node-limit", limits);
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result, JBeamToRigDefDiagnosticCode::NODE_LIMIT) != NULL);
    CHECK(result.transformed_nodes.empty());

    limits = JBeamToRigDefLimits();
    limits.max_beams = 1U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        ir, "beam-limit", limits);
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result, JBeamToRigDefDiagnosticCode::BEAM_LIMIT) != NULL);
    CHECK(result.transformed_nodes.empty());

    JBeamStructuralIR no_beams = ValidIR();
    no_beams.beams.clear();
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        no_beams, "missing-beam");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::MISSING_STRUCTURAL_BEAM) != NULL);
    CHECK(result.transformed_nodes.empty());

    limits = JBeamToRigDefLimits();
    limits.max_cab_triangles = 0U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        ir, "cab-limit", limits);
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result, JBeamToRigDefDiagnosticCode::CAB_LIMIT) != NULL);
    CHECK(result.transformed_nodes.empty());

    const JBeamToRigDefLimits defaults;
    CHECK(defaults.max_input_records ==
        RoR::BeamNG::JBEAM_RIGDEF_INPUT_RECORD_LIMIT);
    CHECK(defaults.max_work_units ==
        RoR::BeamNG::JBEAM_RIGDEF_WORK_UNIT_LIMIT);
    CHECK(defaults.max_diagnostics ==
        RoR::BeamNG::JBEAM_RIGDEF_DIAGNOSTIC_LIMIT);
    CHECK(defaults.max_diagnostic_detail_bytes ==
        RoR::BeamNG::JBEAM_RIGDEF_DIAGNOSTIC_DETAIL_BYTE_LIMIT);
    CHECK(defaults.max_nodes ==
        RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_NODE_LIMIT);
    CHECK(defaults.max_beams ==
        RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_BEAM_LIMIT);
    CHECK(defaults.max_cab_triangles ==
        RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_CAB_LIMIT);

    JBeamStructuralIR runtime_nodes = ValidIR();
    runtime_nodes.nodes.reserve(
        RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_NODE_LIMIT + 1U);
    for (std::size_t i = runtime_nodes.nodes.size();
         i < RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_NODE_LIMIT;
         ++i)
    {
        runtime_nodes.nodes.push_back(Node(
            std::string("limitNode") + std::to_string(i),
            static_cast<double>(i),
            2.0,
            3.0,
            1.0,
            70U));
    }
    limits = JBeamToRigDefLimits();
    limits.max_nodes = std::numeric_limits<std::size_t>::max();
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        runtime_nodes, "runtime-node-limit-exact", limits);
    CHECK(result.IsValid());
    CHECK(result.node_source_order.size() ==
        RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_NODE_LIMIT);

    runtime_nodes.nodes.push_back(Node(
        "runtimeNodeOverflow",
        static_cast<double>(
            RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_NODE_LIMIT),
        2.0,
        3.0,
        1.0,
        70U));
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        runtime_nodes, "runtime-node-limit-overflow", limits);
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result, JBeamToRigDefDiagnosticCode::NODE_LIMIT) != NULL);
    CHECK(result.transformed_nodes.empty());

    JBeamStructuralIR runtime_cabs = ValidIR();
    runtime_cabs.triangles.clear();
    const JBeamStructuralTriangle triangle =
        Triangle(runtime_cabs, 1U, 0U, 2U, 80U);
    runtime_cabs.triangles.assign(
        RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_CAB_LIMIT,
        triangle);
    limits = JBeamToRigDefLimits();
    limits.max_cab_triangles =
        std::numeric_limits<std::size_t>::max();
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        runtime_cabs, "runtime-cab-limit-exact", limits);
    CHECK(result.IsValid());
    CHECK(result.triangle_source_indices.size() ==
        RoR::BeamNG::JBEAM_RIGDEF_RUNTIME_CAB_LIMIT);

    runtime_cabs.triangles.push_back(triangle);
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        runtime_cabs, "runtime-cab-limit-overflow", limits);
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result, JBeamToRigDefDiagnosticCode::CAB_LIMIT) != NULL);
    CHECK(result.transformed_nodes.empty());
}

void TestInputWorkAndDiagnosticEnvelopes()
{
    JBeamStructuralIR disabled_records = ValidIR();
    const JBeamStructuralBeam disabled_beam =
        disabled_records.beams[2];
    for (std::size_t i = 0U; i < 64U; ++i)
    {
        disabled_records.beams.push_back(disabled_beam);
    }
    const std::size_t input_records =
        disabled_records.nodes.size() +
        disabled_records.beams.size() +
        disabled_records.triangles.size();
    const std::size_t exact_work_units =
        2U + 2U * input_records;

    JBeamToRigDefLimits limits;
    limits.max_input_records = input_records;
    limits.max_work_units = exact_work_units;
    JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            disabled_records, "disabled-record-boundary", limits);
    CHECK(result.IsValid());
    CHECK(result.beams.size() == 2U);

    limits.max_input_records = input_records - 1U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        disabled_records, "disabled-record-overflow", limits);
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INPUT_RECORD_LIMIT) != NULL);
    CHECK(result.transformed_nodes.empty());

    limits.max_input_records = input_records;
    limits.max_work_units = exact_work_units - 1U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        disabled_records, "disabled-work-overflow", limits);
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::WORK_LIMIT) != NULL);
    CHECK(result.transformed_nodes.empty());

    JBeamStructuralDiagnostic upstream_error;
    upstream_error.code =
        JBeamStructuralDiagnosticCode::MISSING_NODE_REFERENCE;
    upstream_error.severity = JBeamStructuralSeverity::ERROR;
    upstream_error.provenance = Provenance(
        "vehicles/upstream/bounded.jbeam", 100U);
    upstream_error.detail = "bounded upstream error";

    JBeamStructuralIR exact_diagnostics = ValidIR();
    exact_diagnostics.diagnostics.assign(2U, upstream_error);
    limits = JBeamToRigDefLimits();
    limits.max_diagnostics = 2U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        exact_diagnostics, "diagnostic-boundary", limits);
    CHECK(!result.IsValid());
    CHECK(result.diagnostics.size() == 2U);
    CHECK(result.diagnostics[0].code ==
        JBeamToRigDefDiagnosticCode::INVALID_STRUCTURAL_IR);
    CHECK(result.diagnostics[1].code ==
        JBeamToRigDefDiagnosticCode::INVALID_STRUCTURAL_IR);

    exact_diagnostics.diagnostics.push_back(upstream_error);
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        exact_diagnostics, "diagnostic-overflow", limits);
    CHECK(!result.IsValid());
    CHECK(result.diagnostics.size() == 1U);
    CHECK(result.diagnostics[0].code ==
        JBeamToRigDefDiagnosticCode::DIAGNOSTIC_LIMIT);
    CHECK(result.transformed_nodes.empty());

    limits = JBeamToRigDefLimits();
    limits.max_diagnostics = 0U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        ValidIR(), std::string(), limits);
    CHECK(!result.IsValid());
    CHECK(result.diagnostics.size() == 1U);
    CHECK(result.diagnostics[0].code ==
        JBeamToRigDefDiagnosticCode::DIAGNOSTIC_LIMIT);
    CHECK(result.diagnostics[0].detail.empty());

    JBeamStructuralDiagnostic upstream_warning = upstream_error;
    upstream_warning.severity = JBeamStructuralSeverity::WARNING;
    upstream_warning.detail.clear();
    JBeamStructuralIR hard_diagnostics = ValidIR();
    hard_diagnostics.diagnostics.assign(
        RoR::BeamNG::JBEAM_RIGDEF_DIAGNOSTIC_LIMIT,
        upstream_warning);
    limits = JBeamToRigDefLimits();
    limits.max_diagnostics =
        std::numeric_limits<std::size_t>::max();
    limits.max_work_units =
        std::numeric_limits<std::size_t>::max();
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        hard_diagnostics, "hard-diagnostic-boundary", limits);
    CHECK(result.IsValid());
    hard_diagnostics.diagnostics.push_back(upstream_warning);
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        hard_diagnostics, "hard-diagnostic-overflow", limits);
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::DIAGNOSTIC_LIMIT) != NULL);

    JBeamStructuralIR retained_detail = ValidIR();
    upstream_error.section.clear();
    upstream_error.field_name.clear();
    upstream_error.detail.clear();
    retained_detail.diagnostics.push_back(upstream_error);
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        retained_detail, "detail-measure");
    CHECK(!result.IsValid());
    CHECK(result.diagnostics.size() == 1U);
    const std::size_t empty_detail_size =
        result.diagnostics[0].detail.size();
    const std::size_t hard_detail_limit =
        RoR::BeamNG::JBEAM_RIGDEF_DIAGNOSTIC_DETAIL_BYTE_LIMIT;
    CHECK(empty_detail_size + 2U < hard_detail_limit);

    retained_detail.diagnostics[0].detail.assign(
        hard_detail_limit - empty_detail_size - 2U,
        'x');
    limits = JBeamToRigDefLimits();
    limits.max_diagnostic_detail_bytes =
        std::numeric_limits<std::size_t>::max();
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        retained_detail, "hard-detail-boundary", limits);
    CHECK(!result.IsValid());
    CHECK(result.diagnostics.size() == 1U);
    CHECK(result.diagnostics[0].code ==
        JBeamToRigDefDiagnosticCode::INVALID_STRUCTURAL_IR);
    CHECK(result.diagnostics[0].detail.size() == hard_detail_limit);

    retained_detail.diagnostics[0].detail.push_back('x');
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        retained_detail, "hard-detail-overflow", limits);
    CHECK(!result.IsValid());
    CHECK(result.diagnostics.size() == 1U);
    CHECK(result.diagnostics[0].code ==
        JBeamToRigDefDiagnosticCode::DIAGNOSTIC_DETAIL_LIMIT);
    CHECK(result.diagnostics[0].detail.empty());

    JBeamStructuralIR generated_diagnostics = ValidIR();
    for (std::size_t i = 0U; i < 5U; ++i)
    {
        generated_diagnostics.nodes[i].id.clear();
    }
    limits = JBeamToRigDefLimits();
    limits.max_diagnostics = 2U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        generated_diagnostics, "generated-diagnostic-limit", limits);
    CHECK(!result.IsValid());
    CHECK(result.diagnostics.size() == 2U);
    CHECK(result.diagnostics.back().code ==
        JBeamToRigDefDiagnosticCode::DIAGNOSTIC_LIMIT);
}

void TestDiagnosticsRetainSourceTrace()
{
    JBeamStructuralIR ir = ValidIR();
    ir.beams[0].node_b_index = 2U;
    const JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            ir, "source-trace");
    const JBeamToRigDefDiagnostic* diagnostic = FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_NODE_REFERENCE);
    CHECK(diagnostic != NULL);
    CHECK(diagnostic->source_index == 0U);
    CHECK(diagnostic->provenance.SourceName() ==
        "vehicles/test/main.jbeam");
    CHECK(diagnostic->provenance.begin.line == 30U);

    JBeamStructuralIR upstream = ValidIR();
    JBeamStructuralDiagnostic structural;
    structural.code =
        JBeamStructuralDiagnosticCode::MISSING_NODE_REFERENCE;
    structural.severity = JBeamStructuralSeverity::ERROR;
    structural.provenance = Provenance(
        "vehicles/upstream/bad.jbeam", 99U);
    structural.section = "beams";
    structural.row_index = 4U;
    structural.detail = "upstream reference failure";
    upstream.diagnostics.push_back(structural);
    const JBeamToRigDefPreflightResult upstream_result =
        RoR::BeamNG::PreflightJBeamToRigDef(
            upstream, "upstream-error");
    diagnostic = FindDiagnostic(
        upstream_result,
        JBeamToRigDefDiagnosticCode::INVALID_STRUCTURAL_IR);
    CHECK(diagnostic != NULL);
    CHECK(diagnostic->provenance.SourceName() ==
        "vehicles/upstream/bad.jbeam");
    CHECK(diagnostic->provenance.begin.line == 99U);
    CHECK(diagnostic->detail.find("upstream reference failure") !=
        std::string::npos);
}

void TestDocumentAndSourceLineValidation()
{
    JBeamStructuralIR ir = ValidIR();
    JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(ir, std::string());
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_DOCUMENT_NAME) != NULL);

    ir = ValidIR();
    ir.beams[0].provenance.begin.line =
        static_cast<std::uint64_t>(
            std::numeric_limits<unsigned int>::max()) + 1U;
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        ir, "line-overflow");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::SOURCE_LINE_LIMIT) != NULL);

    ir = ValidIR();
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        ir, std::string("embedded\0name", 13U));
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_DOCUMENT_NAME) != NULL);
}

void TestHostileEnumsAndNonFiniteValues()
{
    JBeamStructuralIR ir = ValidIR();
    ir.nodes[6].x =
        Binary64FromBits(UINT64_C(0x7ff8000000000042));
    JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(ir, "nan-coordinate");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::NON_FINITE_VALUE) != NULL);

    ir = ValidIR();
    ir.nodes[6].node_weight =
        Binary64FromBits(UINT64_C(0x7ff0000000000000));
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        ir, "infinite-mass");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_NODE_MASS) != NULL);

    ir = ValidIR();
    ir.beams[0].status =
        static_cast<JBeamStructuralBeamStatus>(999);
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        ir, "invalid-beam-state");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE) != NULL);
    CHECK(result.transformed_nodes.empty());

    ir = ValidIR();
    ir.triangles[0].status =
        static_cast<JBeamStructuralTriangleStatus>(999);
    result = RoR::BeamNG::PreflightJBeamToRigDef(
        ir, "invalid-triangle-state");
    CHECK(!result.IsValid());
    CHECK(FindDiagnostic(
        result,
        JBeamToRigDefDiagnosticCode::INVALID_ENTITY_STATE) != NULL);
    CHECK(result.transformed_nodes.empty());
}

void TestTransformPreservesTriangleHandedness()
{
    const JBeamStructuralIR ir = ValidIR();
    const JBeamToRigDefPreflightResult result =
        RoR::BeamNG::PreflightJBeamToRigDef(ir, "handedness");
    CHECK(result.IsValid());

    const JBeamStructuralTriangle& triangle = ir.triangles[0];
    const JBeamStructuralNode& a = ir.nodes[triangle.node_a_index];
    const JBeamStructuralNode& b = ir.nodes[triangle.node_b_index];
    const JBeamStructuralNode& c = ir.nodes[triangle.node_c_index];
    const double source_ux = b.x - a.x;
    const double source_uy = b.y - a.y;
    const double source_vx = c.x - a.x;
    const double source_vy = c.y - a.y;
    const double source_up_component =
        source_ux * source_vy - source_uy * source_vx;

    const RoR::BeamNG::JBeamRigDefPoint3& transformed_a =
        result.transformed_nodes[triangle.node_a_index];
    const RoR::BeamNG::JBeamRigDefPoint3& transformed_b =
        result.transformed_nodes[triangle.node_b_index];
    const RoR::BeamNG::JBeamRigDefPoint3& transformed_c =
        result.transformed_nodes[triangle.node_c_index];
    const float transformed_ux = transformed_b.x - transformed_a.x;
    const float transformed_uz = transformed_b.z - transformed_a.z;
    const float transformed_vx = transformed_c.x - transformed_a.x;
    const float transformed_vz = transformed_c.z - transformed_a.z;
    // In RoR axes +Y is up. A determinant +1 axis permutation must preserve
    // the source winding's sign when the normal is expressed along +Y.
    const float transformed_up_component =
        transformed_uz * transformed_vx -
        transformed_ux * transformed_vz;
    CHECK(source_up_component != 0.0);
    CHECK(transformed_up_component != 0.0f);
    CHECK((source_up_component > 0.0) ==
        (transformed_up_component > 0.0f));
}

#if defined(ROR_JBEAM_TO_RIGDEF_FULL_TEST)

float MirrorActorSpawnerDeformationThreshold(
    const RigDef::BeamDefaults& defaults)
{
    float default_deform = BEAM_DEFORM;
    float beam_creak = BEAM_CREAK_DEFAULT;
    if (defaults._is_user_defined)
    {
        default_deform = defaults.deformation_threshold;
        if (!defaults._enable_advanced_deformation &&
            default_deform < BEAM_DEFORM)
        {
            default_deform = BEAM_DEFORM;
        }
        if (defaults._is_plastic_deform_coef_user_defined &&
            defaults.plastic_deform_coef >=
                BEAM_PLASTIC_COEF_DEFAULT)
        {
            beam_creak = 0.0f;
        }
    }
    if (default_deform < beam_creak)
    {
        default_deform = beam_creak;
    }
    return default_deform *
        defaults.scale.deformation_threshold_constant;
}

void TestLowBeamNGDeformSurvivesActorSpawner()
{
    JBeamStructuralIR ir = ValidIR();
    ir.beams[0].has_deform = true;
    ir.beams[0].deform_unbounded = false;
    ir.beams[0].deform = 75000.0;

    std::vector<JBeamToRigDefDiagnostic> diagnostics;
    const RigDef::DocumentPtr document =
        RoR::BeamNG::ConvertJBeamToRigDef(
            ir, "low-beamng-deform", diagnostics);
    CHECK(document != nullptr);
    CHECK(diagnostics.empty());
    CHECK(document->root_module != nullptr);
    CHECK(document->root_module->beams.size() == 2U);
    const std::shared_ptr<RigDef::BeamDefaults>& defaults =
        document->root_module->beams[0].defaults;
    CHECK(defaults != nullptr);
    CHECK(defaults->deformation_threshold == 75000.0f);
    CHECK(defaults->plastic_deform_coef ==
        BEAM_PLASTIC_COEF_DEFAULT);
    CHECK(defaults->_is_plastic_deform_coef_user_defined);
    CHECK(MirrorActorSpawnerDeformationThreshold(*defaults) ==
        75000.0f);
}

void TestProductionDocumentIsFreshAndSpawnReady()
{
    const JBeamStructuralIR ir = ValidIR();
    std::vector<JBeamToRigDefDiagnostic> diagnostics;
    const RigDef::DocumentPtr first =
        RoR::BeamNG::ConvertJBeamToRigDef(
            ir, "spawn-ready", diagnostics);
    CHECK(first != nullptr);
    CHECK(diagnostics.empty());
    const RigDef::DocumentPtr second =
        RoR::BeamNG::ConvertJBeamToRigDef(
            ir, "spawn-ready", diagnostics);
    CHECK(second != nullptr);
    CHECK(first != second);
    CHECK(first->name == "spawn-ready");
    CHECK(first->enable_advanced_deformation);
    CHECK(first->root_module != nullptr);
    const RigDef::Document::Module& module = *first->root_module;
    const JBeamToRigDefPreflightResult plan =
        RoR::BeamNG::PreflightJBeamToRigDef(ir, "spawn-ready");
    CHECK(plan.IsValid());
    CHECK(module.nodes.size() == 7U);
    CHECK(module.nodes[0].id.IsTypeNamed());
    CHECK(module.nodes[0].id.Str() == "ref");
    CHECK(module.nodes[1].id.Str() == "back");
    for (std::size_t i = 0U; i < module.nodes.size(); ++i)
    {
        CHECK(module.nodes[i].node_defaults != nullptr);
        CHECK(module.nodes[i].node_defaults->load_weight == -1.0f);
        CHECK(module.nodes[i].default_minimass != nullptr);
        CHECK(module.nodes[i].default_minimass->min_mass_Kg == 0.0f);
        CHECK((module.nodes[i].options &
            RigDef::Node::OPTION_l_LOAD_WEIGHT) != 0U);
        CHECK(module.nodes[i].options ==
            RigDef::Node::OPTION_l_LOAD_WEIGHT);
        CHECK(module.nodes[i]._has_load_weight_override);
        const std::size_t source_index = plan.node_source_order[i];
        CHECK(module.nodes[i].id.IsTypeNamed());
        CHECK(module.nodes[i].id.Str() ==
            ir.nodes[source_index].id);
        CHECK(module.nodes[i].position.x ==
            plan.transformed_nodes[source_index].x);
        CHECK(module.nodes[i].position.y ==
            plan.transformed_nodes[source_index].y);
        CHECK(module.nodes[i].position.z ==
            plan.transformed_nodes[source_index].z);
        CHECK(module.nodes[i].load_weight_override ==
            static_cast<float>(ir.nodes[source_index].node_weight));
    }
    CHECK(module.nodes[0].load_weight_override == 1.0f);
    CHECK(module.nodes[1].load_weight_override == 2.0f);

    CHECK(module.beams.size() == 2U);
    CHECK(module.beams[0].nodes[0].Str() == "ref");
    CHECK(module.beams[0].nodes[1].Str() == "back");
    CHECK((module.beams[0].options &
        RigDef::Beam::OPTION_i_INVISIBLE) != 0U);
    CHECK(module.beams[0].defaults != nullptr);
    CHECK(module.beams[0].defaults->springiness ==
        RoR::BeamNG::JBEAM_RIGDEF_DEFAULT_BEAM_SPRING);
    CHECK(module.beams[0].defaults->damping_constant ==
        RoR::BeamNG::JBEAM_RIGDEF_DEFAULT_BEAM_DAMPING);
    CHECK(module.beams[0].defaults->deformation_threshold ==
        RoR::BeamNG::JBEAM_RIGDEF_DEFAULT_BEAM_DEFORM);
    CHECK(module.beams[0].defaults->_is_user_defined);
    CHECK(module.beams[0].defaults->_enable_advanced_deformation);
    CHECK(module.beams[0].defaults->plastic_deform_coef ==
        BEAM_PLASTIC_COEF_DEFAULT);
    CHECK(module.beams[0].defaults->
        _is_plastic_deform_coef_user_defined);
    CHECK(module.beams[0].defaults->breaking_threshold ==
        std::numeric_limits<float>::max());
    CHECK(module.beams[0].defaults != module.beams[1].defaults);
    CHECK(module.beams[1]._rest_length_scale == 0.75f);

    CHECK(module.submeshes.size() == 1U);
    CHECK(module.submeshes[0].texcoords.empty());
    CHECK(module.submeshes[0].cab_triangles.size() == 1U);
    CHECK(module.submeshes[0].cab_triangles[0].options == 0U);
    CHECK(module.submeshes[0].cab_triangles[0].nodes[0].Str() ==
        "ref");
    CHECK(module.submeshes[0].cab_triangles[0].nodes[1].Str() ==
        "back");
    CHECK(module.submeshes[0].cab_triangles[0].nodes[2].Str() ==
        "left");

    CHECK(module.cameras.size() == 1U);
    CHECK(module.cameras[0].center_node.Str() == "ref");
    CHECK(module.cameras[0].back_node.Str() == "back");
    CHECK(module.cameras[0].left_node.Str() == "left");
    CHECK(module.globals.size() == 1U);
    CHECK(module.globals[0].dry_mass == 0.0f);
    CHECK(module.globals[0].cargo_mass == 0.0f);
    CHECK(module.globals[0].material_name.empty());
}

#endif

} // namespace

int main()
{
    TestValidPlanUsesSpawnSemantics();
    TestStateAndReferenceInvariantsFailClosed();
    TestFloatNarrowingAndRuntimeTopology();
    TestMassCenterAndBoundsPreflight();
    TestCameraFrameIsReverifiedAfterNarrowing();
    TestAllAdmissionLimitsApplyBeforePlans();
    TestInputWorkAndDiagnosticEnvelopes();
    TestDiagnosticsRetainSourceTrace();
    TestDocumentAndSourceLineValidation();
    TestHostileEnumsAndNonFiniteValues();
    TestTransformPreservesTriangleHandedness();
#if defined(ROR_JBEAM_TO_RIGDEF_FULL_TEST)
    TestLowBeamNGDeformSurvivesActorSpawner();
    TestProductionDocumentIsFreshAndSpawnReady();
#endif
    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JBeamToRigDef tests passed\n";
    return EXIT_SUCCESS;
}
