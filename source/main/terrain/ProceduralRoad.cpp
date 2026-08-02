/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

#include "ProceduralRoad.h"

#include "Actor.h"
#include "Application.h"
#include "Collisions.h"
#include "Console.h"
#include "GameContext.h"
#include "GfxScene.h"
#include "Terrain.h"

#include <Ogre.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace Ogre;
using namespace RoR;

static int id_counter = 0;

namespace
{

RoR::Render::Float3 ToRoadCaptureFloat3(const Ogre::Vector3& value)
{
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z)};
}

RoR::Render::Float2 ToRoadCaptureFloat2(const Ogre::Vector2& value)
{
    return {static_cast<float>(value.x), static_cast<float>(value.y)};
}

RoR::Render::Float3 ToRoadCaptureFloat3(const Ogre::ColourValue& value)
{
    return {
        static_cast<float>(value.r),
        static_cast<float>(value.g),
        static_cast<float>(value.b)};
}

RoR::Render::Float4 ToRoadCaptureFloat4(const Ogre::ColourValue& value)
{
    return {
        static_cast<float>(value.r),
        static_cast<float>(value.g),
        static_cast<float>(value.b),
        static_cast<float>(value.a)};
}

RoR::Render::Matrix4x4 ToRoadCaptureMatrix(const Ogre::Matrix4& value)
{
    RoR::Render::Matrix4x4 converted;
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        for (std::size_t column = 0U; column < 4U; ++column)
        {
            converted.elements[column * 4U + row] =
                static_cast<float>(value[row][column]);
        }
    }
    return converted;
}

bool CaptureRoadMaterial(
    const Ogre::MaterialPtr& material,
    RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput& output)
{
    if (!material || material->getName().empty() ||
        material->getNumTechniques() == 0U)
    {
        return false;
    }
    Ogre::Technique* const technique = material->getTechnique(0U);
    if (technique == nullptr || technique->getNumPasses() == 0U)
    {
        return false;
    }
    Ogre::Pass* const pass = technique->getPass(0U);
    if (pass == nullptr)
    {
        return false;
    }

    RoR::Render::Ogre14GraphicsSceneMaterialCaptureInput candidate;
    candidate.exact_resource_group = material->getGroup();
    candidate.exact_name = material->getName();
    candidate.pass_count =
        static_cast<std::uint32_t>(technique->getNumPasses());
    candidate.texture_unit_count =
        static_cast<std::uint32_t>(pass->getNumTextureUnitStates());
    candidate.has_vertex_program = pass->hasVertexProgram();
    candidate.has_fragment_program = pass->hasFragmentProgram();
    candidate.lighting_enabled = pass->getLightingEnabled();
    candidate.diffuse_linear = ToRoadCaptureFloat4(pass->getDiffuse());
    candidate.ambient_linear = ToRoadCaptureFloat3(pass->getAmbient());
    candidate.specular_linear = ToRoadCaptureFloat3(pass->getSpecular());
    candidate.emissive_linear =
        ToRoadCaptureFloat3(pass->getSelfIllumination());
    candidate.shininess = static_cast<float>(pass->getShininess());

    bool write_red = false;
    bool write_green = false;
    bool write_blue = false;
    bool write_alpha = false;
    pass->getColourWriteEnabled(
        write_red, write_green, write_blue, write_alpha);
    if (!write_red || !write_green || !write_blue || !write_alpha ||
        pass->getSceneBlendingOperation() != Ogre::SBO_ADD ||
        pass->getSceneBlendingOperationAlpha() != Ogre::SBO_ADD)
    {
        return false;
    }

    const Ogre::SceneBlendFactor source = pass->getSourceBlendFactor();
    const Ogre::SceneBlendFactor destination = pass->getDestBlendFactor();
    const Ogre::SceneBlendFactor source_alpha =
        pass->getSourceBlendFactorAlpha();
    const Ogre::SceneBlendFactor destination_alpha =
        pass->getDestBlendFactorAlpha();
    const bool replace = source == Ogre::SBF_ONE &&
        destination == Ogre::SBF_ZERO &&
        source_alpha == Ogre::SBF_ONE &&
        destination_alpha == Ogre::SBF_ZERO;
    const bool straight_alpha = source == Ogre::SBF_SOURCE_ALPHA &&
        destination == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA &&
        ((source_alpha == Ogre::SBF_SOURCE_ALPHA &&
          destination_alpha == Ogre::SBF_ONE_MINUS_SOURCE_ALPHA) ||
         (source_alpha == Ogre::SBF_ONE &&
          destination_alpha == Ogre::SBF_ZERO));
    if (!replace && !straight_alpha)
    {
        return false;
    }
    candidate.blend = replace
        ? RoR::Render::Ogre14GraphicsSceneMaterialBlend::REPLACE
        : RoR::Render::Ogre14GraphicsSceneMaterialBlend::STRAIGHT_ALPHA;

    switch (pass->getCullingMode())
    {
    case Ogre::CULL_NONE:
        candidate.cull =
            RoR::Render::Ogre14GraphicsSceneMaterialCull::NONE;
        break;
    case Ogre::CULL_CLOCKWISE:
        candidate.cull =
            RoR::Render::Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
        break;
    case Ogre::CULL_ANTICLOCKWISE:
        candidate.cull =
            RoR::Render::Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
        break;
    default:
        return false;
    }

    switch (pass->getAlphaRejectFunction())
    {
    case Ogre::CMPF_ALWAYS_PASS:
        candidate.alpha_reject = RoR::Render::
            Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
        break;
    case Ogre::CMPF_GREATER_EQUAL:
        candidate.alpha_reject = RoR::Render::
            Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL;
        break;
    default:
        return false;
    }
    candidate.alpha_reject_value = pass->getAlphaRejectValue();
    output = std::move(candidate);
    return true;
}

// Pillartype 3 is deliberately a separate geometry path. These dimensions are
// part of the `bridge_side_pillars` content contract and must remain stable so
// authored terrain tools can predict each support's occupied volume.
constexpr float SIDE_PIER_COLUMN_HALF_EXTENT_M = 0.65f;
constexpr float SIDE_PIER_HAMMERHEAD_HALF_EXTENT_M = 0.75f;
constexpr float SIDE_PIER_HEAVY_TRUCK_CLEARANCE_M = 2.5f;
constexpr float SIDE_PIER_DECK_UNDERSIDE_M = 0.4f;
constexpr float SIDE_PIER_UNDERSIDE_GAP_M = 0.05f;
constexpr float SIDE_PIER_HAMMERHEAD_THICKNESS_M = 0.6f;
constexpr float SIDE_PIER_TERRAIN_EMBED_M = 0.25f;
constexpr float SIDE_PIER_MIN_VISIBLE_COLUMN_M = 0.1f;
constexpr float SIDE_PIER_EPSILON_M = 0.001f;

enum class SidePierBuildResult
{
    BUILT,
    MISSING_INCOMING_SEGMENT,
    INVALID_ORIENTATION,
    NON_FORWARD_SEGMENT,
    ROADWAY_SWEPT_PRISM_OVERLAP,
    NONFINITE_TERRAIN,
    INSUFFICIENT_COLUMN_HEIGHT
};

const char* SidePierBuildResultName(SidePierBuildResult result)
{
    switch (result)
    {
    case SidePierBuildResult::BUILT:
        return "built";
    case SidePierBuildResult::MISSING_INCOMING_SEGMENT:
        return "missing-incoming-segment";
    case SidePierBuildResult::INVALID_ORIENTATION:
        return "invalid-orientation";
    case SidePierBuildResult::NON_FORWARD_SEGMENT:
        return "non-forward-segment";
    case SidePierBuildResult::ROADWAY_SWEPT_PRISM_OVERLAP:
        return "roadway-swept-prism-overlap";
    case SidePierBuildResult::NONFINITE_TERRAIN:
        return "nonfinite-terrain";
    case SidePierBuildResult::INSUFFICIENT_COLUMN_HEIGHT:
        return "insufficient-column-height";
    }
    return "unknown";
}

void LogSidePierSkip(SidePierBuildResult result, Vector3 road_pos)
{
    const std::string message = fmt::format(
        "[RoR|ProceduralRoad|SidePiers] skip reason={} "
        "pos=({:.6f},{:.6f},{:.6f})",
        SidePierBuildResultName(result),
        road_pos.x,
        road_pos.y,
        road_pos.z);
    RoR::Log(message.c_str());
}

struct SidePierFrame
{
    Vector3 origin;
    Vector3 forward;
    Vector3 lateral;
    float negative_column_min;
    float negative_column_max;
    float positive_column_min;
    float positive_column_max;
    float underside_slope;
    float hammerhead_top_at_station;
    float hammerhead_top_back;
    float hammerhead_top_front;
    float hammerhead_bottom_back;
    float hammerhead_bottom_front;
};

Vector3 SidePierPoint(
    const SidePierFrame& frame,
    float forward_offset,
    float lateral_offset,
    float height)
{
    Vector3 point =
        frame.origin +
        frame.forward * forward_offset +
        frame.lateral * lateral_offset;
    point.y = height;
    return point;
}

void AddSidePierPrism(
    ProceduralRoad& road,
    const SidePierFrame& frame,
    float lateral_min,
    float lateral_max,
    float bottom_back,
    float bottom_front,
    float top_back,
    float top_front,
    float half_longitudinal,
    Vector3 road_pos,
    Vector3 previous_road_pos,
    float road_width)
{
    const float back = -half_longitudinal;
    const float front = half_longitudinal;

    const Vector3 back_min_bottom =
        SidePierPoint(frame, back, lateral_min, bottom_back);
    const Vector3 back_max_bottom =
        SidePierPoint(frame, back, lateral_max, bottom_back);
    const Vector3 front_min_bottom =
        SidePierPoint(frame, front, lateral_min, bottom_front);
    const Vector3 front_max_bottom =
        SidePierPoint(frame, front, lateral_max, bottom_front);
    const Vector3 back_min_top =
        SidePierPoint(frame, back, lateral_min, top_back);
    const Vector3 back_max_top =
        SidePierPoint(frame, back, lateral_max, top_back);
    const Vector3 front_min_top =
        SidePierPoint(frame, front, lateral_min, top_front);
    const Vector3 front_max_top =
        SidePierPoint(frame, front, lateral_max, top_front);

    // Closed, collision-bearing concrete prism. Winding points the cap normals
    // away from the solid for the standard forward/up/lateral road frame.
    road.addQuad(
        back_min_top, back_max_top, front_max_top, front_min_top,
        TextureFit::TEXFIT_CONCRETETOP,
        road_pos, previous_road_pos, road_width);
    road.addQuad(
        back_min_bottom, front_min_bottom, front_max_bottom, back_max_bottom,
        TextureFit::TEXFIT_CONCRETEUNDER,
        road_pos, previous_road_pos, road_width);
    road.addQuad(
        back_min_bottom, back_min_top, front_min_top, front_min_bottom,
        TextureFit::TEXFIT_CONCRETETOP,
        road_pos, previous_road_pos, road_width);
    road.addQuad(
        front_max_bottom, front_max_top, back_max_top, back_max_bottom,
        TextureFit::TEXFIT_CONCRETETOP,
        road_pos, previous_road_pos, road_width);
    road.addQuad(
        back_max_bottom, back_max_top, back_min_top, back_min_bottom,
        TextureFit::TEXFIT_CONCRETETOP,
        road_pos, previous_road_pos, road_width);
    road.addQuad(
        front_min_bottom, front_min_top, front_max_top, front_max_bottom,
        TextureFit::TEXFIT_CONCRETETOP,
        road_pos, previous_road_pos, road_width);
}

SidePierBuildResult BuildSidePierFrame(
    Vector3 road_pos,
    Vector3 previous_road_pos,
    Quaternion road_rot,
    const Vector3* road_points,
    const Vector3* previous_road_points,
    SidePierFrame* frame)
{
    Vector3 forward = road_rot * Vector3::UNIT_X;
    forward.y = 0.0f;
    const float forward_length = forward.length();
    if (!std::isfinite(forward_length) || forward_length <= SIDE_PIER_EPSILON_M)
        return SidePierBuildResult::INVALID_ORIENTATION;
    forward /= forward_length;

    Vector3 authored_lateral = road_rot * Vector3::UNIT_Z;
    authored_lateral.y = 0.0f;
    Vector3 lateral(-forward.z, 0.0f, forward.x);
    if (lateral.dotProduct(authored_lateral) < 0.0f)
        lateral = -lateral;

    Vector3 segment = road_pos - previous_road_pos;
    segment.y = 0.0f;
    const float signed_run = segment.dotProduct(forward);
    if (!std::isfinite(signed_run) || signed_run <= SIDE_PIER_EPSILON_M)
        return SidePierBuildResult::NON_FORWARD_SEGMENT;

    float current_lateral_min = std::numeric_limits<float>::max();
    float current_lateral_max = -std::numeric_limits<float>::max();
    float swept_lateral_min = std::numeric_limits<float>::max();
    float swept_lateral_max = -std::numeric_limits<float>::max();
    for (int point_index = 0; point_index < 8; ++point_index)
    {
        const float current_projection =
            (road_points[point_index] - road_pos).dotProduct(lateral);
        const float previous_projection =
            (previous_road_points[point_index] - road_pos).dotProduct(lateral);
        current_lateral_min =
            std::min(current_lateral_min, current_projection);
        current_lateral_max =
            std::max(current_lateral_max, current_projection);
        swept_lateral_min =
            std::min(swept_lateral_min, std::min(
                current_projection, previous_projection));
        swept_lateral_max =
            std::max(swept_lateral_max, std::max(
                current_projection, previous_projection));
    }

    const float negative_column_max =
        current_lateral_min - SIDE_PIER_HEAVY_TRUCK_CLEARANCE_M;
    const float positive_column_min =
        current_lateral_max + SIDE_PIER_HEAVY_TRUCK_CLEARANCE_M;

    // A sharply folded incoming segment can enter the otherwise-clear column
    // slab. Fail closed rather than silently relocating a support away from its
    // authored cross-section or allowing collision geometry into the roadway.
    if (swept_lateral_min <= negative_column_max + SIDE_PIER_EPSILON_M ||
        swept_lateral_max >= positive_column_min - SIDE_PIER_EPSILON_M)
    {
        return SidePierBuildResult::ROADWAY_SWEPT_PRISM_OVERLAP;
    }

    const float current_underside = std::min(
        road_points[0].y, road_points[7].y);
    const float previous_underside = std::min(
        previous_road_points[0].y, previous_road_points[7].y);
    const float underside_slope =
        (current_underside - previous_underside) / signed_run;
    const float hammerhead_top_at_station = std::min(
        road_pos.y -
            SIDE_PIER_DECK_UNDERSIDE_M -
            SIDE_PIER_UNDERSIDE_GAP_M,
        current_underside - SIDE_PIER_UNDERSIDE_GAP_M);

    frame->origin = road_pos;
    frame->forward = forward;
    frame->lateral = lateral;
    frame->negative_column_max = negative_column_max;
    frame->negative_column_min =
        negative_column_max - 2.0f * SIDE_PIER_COLUMN_HALF_EXTENT_M;
    frame->positive_column_min = positive_column_min;
    frame->positive_column_max =
        positive_column_min + 2.0f * SIDE_PIER_COLUMN_HALF_EXTENT_M;
    frame->underside_slope = underside_slope;
    frame->hammerhead_top_at_station = hammerhead_top_at_station;
    frame->hammerhead_top_back =
        hammerhead_top_at_station -
        underside_slope * SIDE_PIER_HAMMERHEAD_HALF_EXTENT_M;
    frame->hammerhead_top_front =
        hammerhead_top_at_station +
        underside_slope * SIDE_PIER_HAMMERHEAD_HALF_EXTENT_M;
    frame->hammerhead_bottom_back =
        frame->hammerhead_top_back -
        SIDE_PIER_HAMMERHEAD_THICKNESS_M;
    frame->hammerhead_bottom_front =
        frame->hammerhead_top_front -
        SIDE_PIER_HAMMERHEAD_THICKNESS_M;
    return SidePierBuildResult::BUILT;
}

SidePierBuildResult AddBridgeSidePiers(
    ProceduralRoad& road,
    Vector3 road_pos,
    Vector3 previous_road_pos,
    Quaternion road_rot,
    float road_width,
    const Vector3* road_points,
    const Vector3* previous_road_points)
{
    SidePierFrame frame;
    const SidePierBuildResult frame_result = BuildSidePierFrame(
        road_pos,
        previous_road_pos,
        road_rot,
        road_points,
        previous_road_points,
        &frame);
    if (frame_result != SidePierBuildResult::BUILT)
    {
        return frame_result;
    }

    const float forward_offsets[] = {
        -SIDE_PIER_COLUMN_HALF_EXTENT_M,
        SIDE_PIER_COLUMN_HALF_EXTENT_M};
    const float lateral_offsets[] = {
        frame.negative_column_min,
        frame.negative_column_max,
        frame.positive_column_min,
        frame.positive_column_max};
    float terrain_bottom = std::numeric_limits<float>::max();
    for (float forward_offset : forward_offsets)
    {
        for (float lateral_offset : lateral_offsets)
        {
            const Vector3 sample = SidePierPoint(
                frame, forward_offset, lateral_offset, 0.0f);
            const float terrain_height =
                App::GetGameContext()->GetTerrain()->getHeightAt(
                    sample.x, sample.z);
            if (!std::isfinite(terrain_height))
                return SidePierBuildResult::NONFINITE_TERRAIN;
            terrain_bottom = std::min(terrain_bottom, terrain_height);
        }
    }
    terrain_bottom -= SIDE_PIER_TERRAIN_EMBED_M;

    const float column_top_back =
        frame.hammerhead_top_at_station -
        frame.underside_slope * SIDE_PIER_COLUMN_HALF_EXTENT_M;
    const float column_top_front =
        frame.hammerhead_top_at_station +
        frame.underside_slope * SIDE_PIER_COLUMN_HALF_EXTENT_M;
    const float minimum_column_top = std::min(
        column_top_back,
        column_top_front);
    if (minimum_column_top - terrain_bottom <
        SIDE_PIER_MIN_VISIBLE_COLUMN_M)
    {
        return SidePierBuildResult::INSUFFICIENT_COLUMN_HEIGHT;
    }

    AddSidePierPrism(
        road,
        frame,
        frame.negative_column_min,
        frame.negative_column_max,
        terrain_bottom,
        terrain_bottom,
        column_top_back,
        column_top_front,
        SIDE_PIER_COLUMN_HALF_EXTENT_M,
        road_pos,
        previous_road_pos,
        road_width);
    AddSidePierPrism(
        road,
        frame,
        frame.positive_column_min,
        frame.positive_column_max,
        terrain_bottom,
        terrain_bottom,
        column_top_back,
        column_top_front,
        SIDE_PIER_COLUMN_HALF_EXTENT_M,
        road_pos,
        previous_road_pos,
        road_width);

    AddSidePierPrism(
        road,
        frame,
        frame.negative_column_min,
        frame.positive_column_max,
        frame.hammerhead_bottom_back,
        frame.hammerhead_bottom_front,
        frame.hammerhead_top_back,
        frame.hammerhead_top_front,
        SIDE_PIER_HAMMERHEAD_HALF_EXTENT_M,
        road_pos,
        previous_road_pos,
        road_width);
    return SidePierBuildResult::BUILT;
}

} // namespace

ProceduralRoad::ProceduralRoad()
{
    mid = id_counter++;
}

ProceduralRoad::~ProceduralRoad()
{
    if (m_entity)
    {
        App::GetGfxScene()->GetSceneManager()->destroyEntity(m_entity);
        m_entity = nullptr;
    }
    if (snode)
    {
        App::GetGfxScene()->GetSceneManager()->destroySceneNode(snode);
        snode = nullptr;
    }
    if (msh)
    {
        MeshManager::getSingleton().remove(msh->getName());
        msh.reset();
    }
    for (int number : registeredCollTris)
    {
        App::GetGameContext()->GetTerrain()->GetCollisions()->removeCollisionTri(number);
    }
}

bool ProceduralRoad::AssignFinalizedGraphicsLineage(
    std::uint64_t stable_graphics_id,
    std::uint64_t topology_revision) noexcept
{
    if (!m_finalized_graphics_snapshot.finalized ||
        stable_graphics_id == 0U || topology_revision == 0U)
    {
        return false;
    }
    m_finalized_graphics_snapshot.stable_graphics_id = stable_graphics_id;
    m_finalized_graphics_snapshot.topology_revision = topology_revision;
    return true;
}

void ProceduralRoad::finish(Ogre::SceneNode* groupingSceneNode)
{
    Vector3 pts[8];
    computePoints(pts, lastpos, lastrot, lasttype, lastwidth, lastbwidth, lastbheight);
    const bool longitudinal_collision = collision;
    if (!collision_endcaps)
        collision = false;
    addQuad(pts[7], pts[6], pts[5], pts[4], TextureFit::TEXFIT_NONE, lastpos, lastpos, lastwidth);
    addQuad(pts[7], pts[4], pts[3], pts[0], TextureFit::TEXFIT_NONE, lastpos, lastpos, lastwidth);
    addQuad(pts[3], pts[2], pts[1], pts[0], TextureFit::TEXFIT_NONE, lastpos, lastpos, lastwidth);
    collision = longitudinal_collision;

    if (side_pier_requested > 0)
    {
        const std::string message = fmt::format(
            "[RoR|ProceduralRoad|SidePiers] requested={} built={} skipped={}",
            side_pier_requested,
            side_pier_built,
            side_pier_skipped);
        RoR::Log(message.c_str());
    }

    createMesh();
    String entity_name = String("RoadSystem_Instance-").append(StringConverter::toString(mid));
    String mesh_name = String("RoadSystem-").append(StringConverter::toString(mid));
    Entity* ec = App::GetGfxScene()->GetSceneManager()->createEntity(entity_name, mesh_name);
    m_entity = ec;
    snode = groupingSceneNode->createChildSceneNode();
    snode->attachObject(ec);

    m_finalized_graphics_snapshot.exact_native_entity_name = ec->getName();
    m_finalized_graphics_snapshot.render_from_object =
        ToRoadCaptureMatrix(static_cast<const Ogre::Matrix4&>(
            ec->_getParentNodeFullTransform()));
    m_finalized_graphics_snapshot.visibility_mask = ec->getVisibilityFlags();
    m_finalized_graphics_snapshot.visible = ec->getVisible();
    m_finalized_graphics_snapshot.casts_shadows = ec->getCastShadows();
    m_finalized_graphics_snapshot.visible_in_reflections = true;
    m_finalized_graphics_snapshot.native_material_audit_complete = false;
    if (ec->getNumSubEntities() == 1U && ec->getSubEntity(0U) != nullptr)
    {
        Ogre::SubEntity* const sub_entity = ec->getSubEntity(0U);
        const Ogre::MaterialPtr material = sub_entity->getMaterial();
        if (material)
        {
            // Preserve exact identity even if a later unsupported native
            // state prevents the complete fallback audit from succeeding.
            m_finalized_graphics_snapshot.material.exact_resource_group =
                material->getGroup();
            m_finalized_graphics_snapshot.material.exact_name =
                material->getName();
            m_finalized_graphics_snapshot.receives_shadows =
                material->getReceiveShadows();
        }
        m_finalized_graphics_snapshot.visible =
            m_finalized_graphics_snapshot.visible && sub_entity->isVisible();
        m_finalized_graphics_snapshot.native_material_audit_complete =
            CaptureRoadMaterial(
                material, m_finalized_graphics_snapshot.material);
    }
    m_finalized_graphics_snapshot.finalized = true;

    if (collision && !registeredCollTris.empty())
    {
        App::GetGameContext()->GetTerrain()->GetCollisions()->registerCollisionMesh(
            "RoadSystem", mesh_name,
            ec->getBoundingBox().getCenter(), ec->getMesh()->getBounds(),
            /*groundmodel:*/nullptr, registeredCollTris[0], (int)registeredCollTris.size());
    }
}

void ProceduralRoad::addBlock(Vector3 pos, Quaternion rot, RoadType type, float width, float bwidth, float bheight, int pillartype)
{
    if (type == RoadType::ROAD_AUTOMATIC)
    {
        width = 10.0;
        bwidth = 1.4;
        bheight = 0.2;
        //define type
        Vector3 leftv = pos + rot * Vector3(0, 0, bwidth + width / 2.0);
        Vector3 rightv = pos + rot * Vector3(0, 0, -bwidth - width / 2.0);
        float dleft = leftv.y - RoR::App::GetGameContext()->GetTerrain()->getHeightAt(leftv.x, leftv.z);
        float dright = rightv.y - RoR::App::GetGameContext()->GetTerrain()->getHeightAt(rightv.x, rightv.z);
        if (dleft < bheight + 0.1 && dright < bheight + 0.1)
            type = RoadType::ROAD_FLAT;
        if (dleft < bheight + 0.1 && dright >= bheight + 0.1 && dright < 4.0)
            type = RoadType::ROAD_LEFT;
        if (dleft >= bheight + 0.1 && dleft < 4.0 && dright < bheight + 0.1)
            type = RoadType::ROAD_RIGHT;
        if (dleft >= bheight + 0.1 && dleft < 4.0 && dright >= bheight + 0.1 && dright < 4.0)
            type = RoadType::ROAD_BOTH;
        if (type == RoadType::ROAD_AUTOMATIC)
            type = RoadType::ROAD_BRIDGE;
        if (type != RoadType::ROAD_FLAT)
        {
            width = 10.0;
            bwidth = 0.4;
            bheight = 0.5;
        };
    }

    Vector3 pts[8];
    if (!first)
    {
        Vector3 lpts[8];
        if (type == RoadType::ROAD_MONORAIL)
            pos.y += 2;

        computePoints(pts, pos, rot, type, width, bwidth, bheight);
        computePoints(lpts, lastpos, lastrot, lasttype, lastwidth, lastbwidth, lastbheight);

        //tarmac
        if (type == RoadType::ROAD_MONORAIL)
            addQuad(pts[4], lpts[4], lpts[3], pts[3], TextureFit::TEXFIT_CONCRETETOP, pos, lastpos, width);
        else
            addQuad(pts[4], lpts[4], lpts[3], pts[3], TextureFit::TEXFIT_ROAD, pos, lastpos, width);

        if (type == RoadType::ROAD_FLAT && lasttype == RoadType::ROAD_FLAT)
        {
            //sides (close)
            addQuad(pts[5], lpts[5], lpts[4], pts[4], TextureFit::TEXFIT_ROADS3, pos, lastpos, width);
            addQuad(pts[3], lpts[3], lpts[2], pts[2], TextureFit::TEXFIT_ROADS2, pos, lastpos, width);
            //sides (far)
            addQuad(pts[6], lpts[6], lpts[5], pts[5], TextureFit::TEXFIT_ROADS4, pos, lastpos, width);
            addQuad(pts[2], lpts[2], lpts[1], pts[1], TextureFit::TEXFIT_ROADS1, pos, lastpos, width);
        }
        else
        {
            //sides (close)
            addQuad(pts[5], lpts[5], lpts[4], pts[4], TextureFit::TEXFIT_CONCRETEWALLI, pos, lastpos, width, (type == RoadType::ROAD_FLAT || type == RoadType::ROAD_LEFT));
            addQuad(pts[3], lpts[3], lpts[2], pts[2], TextureFit::TEXFIT_CONCRETEWALLI, pos, lastpos, width, !(type == RoadType::ROAD_FLAT || type == RoadType::ROAD_RIGHT));
            //sides (far)
            addQuad(pts[6], lpts[6], lpts[5], pts[5], TextureFit::TEXFIT_CONCRETETOP, pos, lastpos, width, (type == RoadType::ROAD_FLAT || type == RoadType::ROAD_LEFT));
            addQuad(pts[2], lpts[2], lpts[1], pts[1], TextureFit::TEXFIT_CONCRETETOP, pos, lastpos, width, !(type == RoadType::ROAD_FLAT || type == RoadType::ROAD_RIGHT));
        }
        if (type == RoadType::ROAD_BRIDGE || lasttype == RoadType::ROAD_BRIDGE || type == RoadType::ROAD_MONORAIL || lasttype == RoadType::ROAD_MONORAIL)
        {
            //walls
            addQuad(pts[1], lpts[1], lpts[0], pts[0], TextureFit::TEXFIT_CONCRETEWALL, pos, lastpos, width);
            addQuad(lpts[6], pts[6], pts[7], lpts[7], TextureFit::TEXFIT_CONCRETEWALL, pos, lastpos, width);
            //underside - we flip the underside so it folds gracefully with the top
            addQuad(pts[0], lpts[0], lpts[7], pts[7], TextureFit::TEXFIT_CONCRETEUNDER, pos, lastpos, width, true);
        }
        else
        {
            //walls
            addQuad(pts[1], lpts[1], lpts[0], pts[0], TextureFit::TEXFIT_BRICKWALL, pos, lastpos, width);
            addQuad(lpts[6], pts[6], pts[7], lpts[7], TextureFit::TEXFIT_BRICKWALL, pos, lastpos, width);
        }
        if (type == RoadType::ROAD_BRIDGE &&
            pillartype == ROAD_PILLAR_TYPE_BRIDGE_SIDES)
        {
            ++side_pier_requested;
            const SidePierBuildResult result = AddBridgeSidePiers(
                *this,
                pos,
                lastpos,
                rot,
                width,
                pts,
                lpts);
            if (result == SidePierBuildResult::BUILT)
            {
                ++side_pier_built;
            }
            else
            {
                ++side_pier_skipped;
                LogSidePierSkip(result, pos);
            }
        }
        else if ((type == RoadType::ROAD_BRIDGE || type == RoadType::ROAD_MONORAIL) && pillartype > 0)
        {
            /* this is the basic bridge pillar mod.
             * it will create on pillar for each segment!
             * @todo: create only a few pillars instead of so much!
             */
            // construct the pillars
            Vector3 leftv = pos + rot * Vector3(0, 0, bwidth + width / 2.0);
            Vector3 rightv = pos + rot * Vector3(0, 0, -bwidth - width / 2.0);
            Vector3 middle = lpts[0] - ((lpts[0] + (pts[1] - lpts[0]) / 2) -
                (lpts[7] + (pts[6] - lpts[7]) / 2)) * 0.5;
            float heightleft = RoR::App::GetGameContext()->GetTerrain()->getHeightAt(leftv.x, leftv.z);
            float heightright = RoR::App::GetGameContext()->GetTerrain()->getHeightAt(rightv.x, rightv.z);
            float heightmiddle = RoR::App::GetGameContext()->GetTerrain()->getHeightAt(middle.x, middle.z);

            bool builtpillars = true;

            float sidefactor = 0.5; // 0.5 = middle
            // only re-position short pillars! (< 10 meters)
            // so big bridge pillars do not get repositioned
            if (pos.y - heightmiddle < 10)
            {
                if (heightleft >= heightright)
                    sidefactor = 0.8;
                else
                    sidefactor = 0.2;
            }

            static int pillarcounter = 0;
            pillarcounter++;

            if (pillartype == 2)
            {
                // always in the middle
                sidefactor = 0.5;
                // only build every fifth pillar
                if (pillarcounter % 5)
                    builtpillars = false;
            }

            middle = lpts[0] - ((lpts[0] + (pts[1] - lpts[0]) / 2) -
                (lpts[7] + (pts[6] - lpts[7]) / 2)) * sidefactor;
            float len = middle.y - RoR::App::GetGameContext()->GetTerrain()->getHeightAt(middle.x, middle.z) + 5;
            float width2 = len / 30;

            if (pillartype == 2 && len > 20)
            // no over-long pillars
                builtpillars = false;

            // do not draw too small pillars, the bridge may hold without them ;)
            if (width2 > 5)
                width2 = 5;

            if (pillartype == 2)
                width2 = 0.2;

            if (width2 >= 0.2 && builtpillars)
            {
                //sides
                addQuad(middle + Vector3(-width2, -len, -width2),
                    middle + Vector3(-width2, 0, -width2),
                    middle + Vector3(width2, 0, -width2),
                    middle + Vector3(width2, -len, -width2),
                    TextureFit::TEXFIT_CONCRETETOP, pos, lastpos, width2);

                addQuad(middle + Vector3(width2, -len, width2),
                    middle + Vector3(width2, 0, width2),
                    middle + Vector3(-width2, 0, width2),
                    middle + Vector3(-width2, -len, width2),
                    TextureFit::TEXFIT_CONCRETETOP, pos, lastpos, width2);

                addQuad(middle + Vector3(-width2, -len, width2),
                    middle + Vector3(-width2, 0, width2),
                    middle + Vector3(-width2, 0, -width2),
                    middle + Vector3(-width2, -len, -width2),
                    TextureFit::TEXFIT_CONCRETETOP, pos, lastpos, width2);

                addQuad(middle + Vector3(width2, -len, -width2),
                    middle + Vector3(width2, 0, -width2),
                    middle + Vector3(width2, 0, width2),
                    middle + Vector3(width2, -len, width2),
                    TextureFit::TEXFIT_CONCRETETOP, pos, lastpos, width2);
            }
        }
    }
    else
    {
        first = false;
        computePoints(pts, pos, rot, type, width, bwidth, bheight);
        const bool longitudinal_collision = collision;
        if (!collision_endcaps)
            collision = false;
        addQuad(pts[0], pts[1], pts[2], pts[3], TextureFit::TEXFIT_NONE, pos, pos, width);
        addQuad(pts[0], pts[3], pts[4], pts[7], TextureFit::TEXFIT_NONE, pos, pos, width);
        addQuad(pts[4], pts[5], pts[6], pts[7], TextureFit::TEXFIT_NONE, pos, pos, width);
        collision = longitudinal_collision;
        if (type == RoadType::ROAD_BRIDGE &&
            pillartype == ROAD_PILLAR_TYPE_BRIDGE_SIDES)
        {
            ++side_pier_requested;
            ++side_pier_skipped;
            LogSidePierSkip(
                SidePierBuildResult::MISSING_INCOMING_SEGMENT,
                pos);
        }
    }
    lastpos = pos;
    lastrot = rot;
    lastwidth = width;
    lastbwidth = bwidth;
    lastbheight = bheight;
    lasttype = type;

    if (App::diag_terrn_log_roads->getBool())
    {
        Str<2000> msg; msg << "[RoR] Road Block |";
        msg << " pos=(" << pos.x << " " << pos.y << " " << pos.z << ")";
        msg << " rot=(" << rot.x << " " << rot.y << " " << rot.z << ")";
        msg << " width=" << width;
        msg << " bwidth=" << bwidth;
        msg << " bheight=" << bheight;
        msg << " type=" << (int)type;
        for (int i = 0; i < 8; ++i)
        {
            msg << "\n\t Point#" << i << ": " << pts[i].x << " " << pts[i].y << " " << pts[i].z;
        }
        Log(msg.ToCStr());
    }
}

void ProceduralRoad::computePoints(Vector3* pts, Vector3 pos, Quaternion rot, RoadType type, float width, float bwidth, float bheight)
{
    if (type == RoadType::ROAD_FLAT)
    {
        pts[1] = pos + rot * Vector3(0, -bheight, bwidth + width / 2.0);
        pts[0] = baseOf(pts[1]);
        pts[2] = pos + rot * Vector3(0, -bheight / 4.0, bwidth / 3.0 + width / 2.0);
        pts[3] = pos + rot * Vector3(0, 0, width / 2.0);
        pts[4] = pos + rot * Vector3(0, 0, -width / 2.0);
        pts[5] = pos + rot * Vector3(0, -bheight / 4.0, -bwidth / 3.0 - width / 2.0);
        pts[6] = pos + rot * Vector3(0, -bheight, -bwidth - width / 2.0);
        pts[7] = baseOf(pts[6]);
    }
    if (type == RoadType::ROAD_BOTH)
    {
        pts[1] = pos + rot * Vector3(0, bheight, bwidth + width / 2.0);
        pts[0] = baseOf(pts[1]);
        pts[2] = pos + rot * Vector3(0, bheight, width / 2.0);
        pts[3] = pos + rot * Vector3(0, 0, width / 2.0);
        pts[4] = pos + rot * Vector3(0, 0, -width / 2.0);
        pts[5] = pos + rot * Vector3(0, bheight, -width / 2.0);
        pts[6] = pos + rot * Vector3(0, bheight, -bwidth - width / 2.0);
        pts[7] = baseOf(pts[6]);
    }
    if (type == RoadType::ROAD_LEFT)
    {
        pts[1] = pos + rot * Vector3(0, -bheight, bwidth + width / 2.0);
        pts[0] = baseOf(pts[1]);
        pts[2] = pos + rot * Vector3(0, -bheight / 4.0, bwidth / 3.0 + width / 2.0);
        pts[3] = pos + rot * Vector3(0, 0, width / 2.0);
        pts[4] = pos + rot * Vector3(0, 0, -width / 2.0);
        pts[5] = pos + rot * Vector3(0, bheight, -width / 2.0);
        pts[6] = pos + rot * Vector3(0, bheight, -bwidth - width / 2.0);
        pts[7] = baseOf(pts[6]);
    }
    if (type == RoadType::ROAD_RIGHT)
    {
        pts[1] = pos + rot * Vector3(0, bheight, bwidth + width / 2.0);
        pts[0] = baseOf(pts[1]);
        pts[2] = pos + rot * Vector3(0, bheight, width / 2.0);
        pts[3] = pos + rot * Vector3(0, 0, width / 2.0);
        pts[4] = pos + rot * Vector3(0, 0, -width / 2.0);
        pts[5] = pos + rot * Vector3(0, -bheight / 4.0, -bwidth / 3.0 - width / 2.0);
        pts[6] = pos + rot * Vector3(0, -bheight, -bwidth - width / 2.0);
        pts[7] = baseOf(pts[6]);
    }
    if (type == RoadType::ROAD_BRIDGE)
    {
        pts[0] = pos + rot * Vector3(0, -0.4, bwidth + width / 2.0);
        pts[1] = pos + rot * Vector3(0, bheight, bwidth + width / 2.0);
        pts[2] = pos + rot * Vector3(0, bheight, width / 2.0);
        pts[3] = pos + rot * Vector3(0, 0, width / 2.0);
        pts[4] = pos + rot * Vector3(0, 0, -width / 2.0);
        pts[5] = pos + rot * Vector3(0, bheight, -width / 2.0);
        pts[6] = pos + rot * Vector3(0, bheight, -bwidth - width / 2.0);
        pts[7] = pos + rot * Vector3(0, -0.4, -bwidth - width / 2.0);
    }
    if (type == RoadType::ROAD_MONORAIL)
    {
        pts[0] = pos + rot * Vector3(0, -1.4, bwidth + width / 2.0);
        pts[1] = pos + rot * Vector3(0, bheight, bwidth + width / 2.0);
        pts[2] = pos + rot * Vector3(0, bheight, width / 2.0);
        pts[3] = pos + rot * Vector3(0, 0, width / 2.0);
        pts[4] = pos + rot * Vector3(0, 0, -width / 2.0);
        pts[5] = pos + rot * Vector3(0, bheight, -width / 2.0);
        pts[6] = pos + rot * Vector3(0, bheight, -bwidth - width / 2.0);
        pts[7] = pos + rot * Vector3(0, -1.4, -bwidth - width / 2.0);
    }
}

inline Vector3 ProceduralRoad::baseOf(Vector3 p)
{
    float y = RoR::App::GetGameContext()->GetTerrain()->getHeightAt(p.x, p.z) - 0.01;

    if (y > p.y)
    {
        y = p.y - 0.01;
    }

    return Vector3(p.x, y, p.z);
}

void ProceduralRoad::addQuad(Vector3 p1, Vector3 p2, Vector3 p3, Vector3 p4, TextureFit texfit, Vector3 pos, Vector3 lastpos, float width, bool flip)
{
    if (vertexcount + 3 >= MAX_VERTEX || tricount * 3 + 3 + 2 >= MAX_TRIS * 3)
        return;
    Vector2 texf[4];
    textureFit(p1, p2, p3, p4, texfit, texf, pos, lastpos, width);
    //vertexes
    vertex[vertexcount] = p1;
    tex[vertexcount] = texf[0];
    vertex[vertexcount + 1] = p2;
    tex[vertexcount + 1] = texf[1];
    vertex[vertexcount + 2] = p3;
    tex[vertexcount + 2] = texf[2];
    vertex[vertexcount + 3] = p4;
    tex[vertexcount + 3] = texf[3];
    //tris
    if (flip)
    {
        tris[tricount * 3] = vertexcount;
        tris[tricount * 3 + 1] = vertexcount + 1;
        tris[tricount * 3 + 2] = vertexcount + 3;
        tris[tricount * 3 + 3] = vertexcount + 1;
        tris[tricount * 3 + 3 + 1] = vertexcount + 2;
        tris[tricount * 3 + 3 + 2] = vertexcount + 3;
    }
    else
    {
        tris[tricount * 3] = vertexcount;
        tris[tricount * 3 + 1] = vertexcount + 1;
        tris[tricount * 3 + 2] = vertexcount + 2;
        tris[tricount * 3 + 3] = vertexcount;
        tris[tricount * 3 + 3 + 1] = vertexcount + 2;
        tris[tricount * 3 + 3 + 2] = vertexcount + 3;
    }
    if (collision)
    {
        ground_model_t* gm = App::GetGameContext()->GetTerrain()->GetCollisions()->getGroundModelByString("concrete");
        if (texfit == TextureFit::TEXFIT_ROAD || texfit == TextureFit::TEXFIT_ROADS1 || texfit == TextureFit::TEXFIT_ROADS2 || texfit == TextureFit::TEXFIT_ROADS3 || texfit == TextureFit::TEXFIT_ROADS4)
            gm = App::GetGameContext()->GetTerrain()->GetCollisions()->getGroundModelByString("asphalt");
        addCollisionQuad(p1, p2, p3, p4, gm, flip);
    }
    tricount += 2;
    vertexcount += 4;
}

void ProceduralRoad::textureFit(Vector3 p1, Vector3 p2, Vector3 p3, Vector3 p4, TextureFit texfit, Vector2* texc, Vector3 pos, Vector3 lastpos, float width)
{
    int i;

    if (texfit == TextureFit::TEXFIT_BRICKWALL || texfit == TextureFit::TEXFIT_CONCRETEWALL || texfit == TextureFit::TEXFIT_CONCRETEWALLI)
    {
        Vector3 ps[4];
        ps[0] = p1;
        ps[1] = p2;
        ps[2] = p3;
        ps[3] = p4;
        Vector3 pref1 = pos;
        Vector3 pref2 = lastpos;
        //make matrix
        Vector3 bx = pref2 - pref1;
        bx.normalise();
        Vector3 by = Vector3::UNIT_Y;
        Vector3 bz = bx.crossProduct(by);
        //coordinates change matrix
        Matrix3 reverse;
        reverse.SetColumn(0, bx);
        reverse.SetColumn(1, by);
        reverse.SetColumn(2, bz);
        Matrix3 forward;
        forward = reverse.Inverse();
        //transpose
        for (i = 0; i < 4; i++)
        {
            Vector3 trv = forward * (ps[i] - pref1);
            if (texfit == TextureFit::TEXFIT_BRICKWALL)
            {
                float ty = 0.746 - trv.y * 0.25 / 4.5;
                // fix overlapping
                if (ty > 1)
                    ty = 1;
                texc[i] = Vector2(trv.x / 10.0, ty);
            }
            if (texfit == TextureFit::TEXFIT_CONCRETEWALL)
            {
                // fix overlapping
                float ty = 0.496 - (trv.y - 0.7) * 0.25 / 4.5;
                if (ty > 1)
                    ty = 1;
                texc[i] = Vector2(trv.x / 10.0, ty);
            }
            if (texfit == TextureFit::TEXFIT_CONCRETEWALLI)
            {
                float ty = 0.496 + trv.y * 0.25 / 4.5;
                // fix overlapping
                if (ty > 1)
                    ty = 1;
                texc[i] = Vector2(trv.x / 10.0, ty);
            }
        }
        return;
    }
    if (texfit == TextureFit::TEXFIT_ROAD || texfit == TextureFit::TEXFIT_ROADS1 || texfit == TextureFit::TEXFIT_ROADS2 || texfit == TextureFit::TEXFIT_ROADS3 || texfit == TextureFit::TEXFIT_ROADS4 || texfit == TextureFit::TEXFIT_CONCRETETOP || texfit == TextureFit::TEXFIT_CONCRETEUNDER)
    {
        Vector3 ps[4];
        ps[0] = p1;
        ps[1] = p2;
        ps[2] = p3;
        ps[3] = p4;
        Vector3 pref1 = pos;
        Vector3 pref2 = lastpos;
        //project
        for (i = 0; i < 4; i++)
            ps[i].y = 0;
        pref1.y = 0;
        pref2.y = 0;
        //make matrix
        Vector3 bx = pref2 - pref1;
        bx.normalise();
        Vector3 by = Vector3::UNIT_Y;
        Vector3 bz = bx.crossProduct(by);
        //coordinates change matrix
        Matrix3 reverse;
        reverse.SetColumn(0, bx);
        reverse.SetColumn(1, by);
        reverse.SetColumn(2, bz);
        Matrix3 forward;
        forward = reverse.Inverse();
        //transpose
        float trvrefz = 0.0;
        for (i = 0; i < 4; i++)
        {
            Vector3 trv = forward * (ps[i] - pref1);
            if (texfit == TextureFit::TEXFIT_CONCRETETOP)
            {
                if (i == 0)
                    trvrefz = trv.z;
                texc[i] = Vector2(trv.x / 10.0, 0.621 + (trv.z - trvrefz) * 0.25 / 4.5);
            }
            else
            {
                float v1 = 0.072;
                float v2 = 0.423;
                if (texfit == TextureFit::TEXFIT_ROADS1)
                {
                    v1 = 0.001;
                    v2 = 0.036;
                };
                if (texfit == TextureFit::TEXFIT_ROADS2)
                {
                    v1 = 0.036;
                    v2 = 0.072;
                };
                if (texfit == TextureFit::TEXFIT_ROADS3)
                {
                    v1 = 0.423;
                    v2 = 0.458;
                };
                if (texfit == TextureFit::TEXFIT_ROADS4)
                {
                    v1 = 0.458;
                    v2 = 0.493;
                };
                if (texfit == TextureFit::TEXFIT_CONCRETEUNDER)
                {
                    v1 = 0.496;
                    v2 = 0.745;
                };
                if (i < 2)
                    texc[i] = Vector2(trv.x / 10.0, v1);
                else
                    texc[i] = Vector2(trv.x / 10.0, v2);
            }
        }
        return;
    }
    //default
    for (i = 0; i < 4; i++)
        texc[i] = Vector2(0, 0);
}

void ProceduralRoad::addCollisionQuad(Vector3 p1, Vector3 p2, Vector3 p3, Vector3 p4, ground_model_t* gm, bool flip)
{
    int triID = 0;
    if (flip)
    {
        triID = App::GetGameContext()->GetTerrain()->GetCollisions()->addCollisionTri(p1, p2, p4, gm);
        if (triID >= 0)
            registeredCollTris.push_back(triID);

        triID = App::GetGameContext()->GetTerrain()->GetCollisions()->addCollisionTri(p4, p2, p3, gm);
        if (triID >= 0)
            registeredCollTris.push_back(triID);
    }
    else
    {
        triID = App::GetGameContext()->GetTerrain()->GetCollisions()->addCollisionTri(p1, p2, p3, gm);
        if (triID >= 0)
            registeredCollTris.push_back(triID);

        triID = App::GetGameContext()->GetTerrain()->GetCollisions()->addCollisionTri(p1, p3, p4, gm);
        if (triID >= 0)
            registeredCollTris.push_back(triID);
    }
}

void ProceduralRoad::addCollisionQuad(Ogre::Vector3 p1, Ogre::Vector3 p2, Ogre::Vector3 p3, Ogre::Vector3 p4, std::string const& gm_name, bool flip /*= false*/)
{
    ground_model_t* gm = App::GetGameContext()->GetTerrain()->GetCollisions()->getGroundModelByString(gm_name);
    if (gm)
    {
        this->addCollisionQuad(p1, p2, p3, p4, gm, flip);
    }
    else
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_TERRN, Console::CONSOLE_SYSTEM_WARNING, 
            fmt::format("ProceduralRoad::addCollisionQuad() - ground model '{}' does not exist", gm_name));
    }
}

void ProceduralRoad::createMesh()
{
    AxisAlignedBox aab;
    union
    {
        float* vertices;
        CoVertice_t* covertices;
    };
    /// Create the mesh via the MeshManager
    Ogre::String mesh_name = Ogre::String("RoadSystem-").append(Ogre::StringConverter::toString(mid));
    msh = MeshManager::getSingleton().createManual(mesh_name, ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    m_finalized_graphics_snapshot =
        RoR::Render::Ogre14ProceduralRoadCapture{};
    m_finalized_graphics_snapshot.exact_native_mesh_resource_group =
        msh->getGroup();
    m_finalized_graphics_snapshot.exact_native_mesh_name = msh->getName();

    mainsub = msh->createSubMesh();
    mainsub->setMaterialName("road2");

    /// Define the vertices
    size_t vbufCount = (2 * 3 + 2) * vertexcount;
    vertices = (float*)malloc(vbufCount * sizeof(float));
    int i;
    //fill values
    for (i = 0; i < vertexcount; i++)
    {
        covertices[i].texcoord = tex[i];
        covertices[i].vertex = vertex[i];
        //normals are computed later
        covertices[i].normal = Vector3::ZERO;
        aab.merge(vertex[i]);
    }

    /// Define triangles
    size_t ibufCount = 3 * tricount;

    //compute normals
    for (i = 0; i < tricount && i * 3 + 2 < MAX_TRIS * 3; i++)
    {
        Vector3 v1, v2;
        v1 = covertices[tris[i * 3 + 1]].vertex - covertices[tris[i * 3]].vertex;
        v2 = covertices[tris[i * 3 + 2]].vertex - covertices[tris[i * 3]].vertex;
        v1 = v1.crossProduct(v2);
        v1.normalise();
        covertices[tris[i * 3]].normal += v1;
        covertices[tris[i * 3 + 1]].normal += v1;
        covertices[tris[i * 3 + 2]].normal += v1;
    }
    //normalize
    for (i = 0; i < vertexcount; i++)
    {
        covertices[i].normal.normalise();
    }

    m_finalized_graphics_snapshot.positions.reserve(
        static_cast<std::size_t>(vertexcount));
    m_finalized_graphics_snapshot.exact_render_normals.reserve(
        static_cast<std::size_t>(vertexcount));
    m_finalized_graphics_snapshot.texture_coordinates_0.reserve(
        static_cast<std::size_t>(vertexcount));
    for (i = 0; i < vertexcount; ++i)
    {
        m_finalized_graphics_snapshot.positions.push_back(
            ToRoadCaptureFloat3(covertices[i].vertex));
        m_finalized_graphics_snapshot.exact_render_normals.push_back(
            ToRoadCaptureFloat3(covertices[i].normal));
        m_finalized_graphics_snapshot.texture_coordinates_0.push_back(
            ToRoadCaptureFloat2(covertices[i].texcoord));
    }
    m_finalized_graphics_snapshot.indices.reserve(ibufCount);
    for (std::size_t index = 0U; index < ibufCount; ++index)
    {
        m_finalized_graphics_snapshot.indices.push_back(
            static_cast<std::uint32_t>(tris[index]));
    }

    /// Create vertex data structure for vertices shared between sub meshes
    msh->sharedVertexData = new VertexData();
    msh->sharedVertexData->vertexCount = vertexcount;

    /// Create declaration (memory format) of vertex data
    VertexDeclaration* decl = msh->sharedVertexData->vertexDeclaration;
    size_t offset = 0;
    decl->addElement(0, offset, VET_FLOAT3, VES_POSITION);
    offset += VertexElement::getTypeSize(VET_FLOAT3);
    decl->addElement(0, offset, VET_FLOAT3, VES_NORMAL);
    offset += VertexElement::getTypeSize(VET_FLOAT3);
    decl->addElement(0, offset, VET_FLOAT2, VES_TEXTURE_COORDINATES, 0);
    offset += VertexElement::getTypeSize(VET_FLOAT2);

    /// Allocate vertex buffer of the requested number of vertices (vertexCount)
    /// and bytes per vertex (offset)
    HardwareVertexBufferSharedPtr vbuf =
        HardwareBufferManager::getSingleton().createVertexBuffer(
            offset, msh->sharedVertexData->vertexCount, HardwareBuffer::HBU_STATIC_WRITE_ONLY);

    /// Upload the vertex data to the card
    vbuf->writeData(0, vbuf->getSizeInBytes(), vertices, true);

    /// Set vertex buffer binding so buffer 0 is bound to our vertex buffer
    VertexBufferBinding* bind = msh->sharedVertexData->vertexBufferBinding;
    bind->setBinding(0, vbuf);

    //for the face
    /// Allocate index buffer of the requested number of vertices (ibufCount)
    HardwareIndexBufferSharedPtr ibuf = HardwareBufferManager::getSingleton().
        createIndexBuffer(
            HardwareIndexBuffer::IT_16BIT,
            ibufCount,
            HardwareBuffer::HBU_STATIC_WRITE_ONLY);

    /// Upload the index data to the card
    ibuf->writeData(0, ibuf->getSizeInBytes(), tris, true);

    /// Set parameters of the submesh
    mainsub->useSharedVertices = true;
    mainsub->indexData->indexBuffer = ibuf;
    mainsub->indexData->indexCount = ibufCount;
    mainsub->indexData->indexStart = 0;

    msh->_setBounds(aab, true);

    /// Notify Mesh object that it has been loaded
    msh->load();

    free(vertices);
};
