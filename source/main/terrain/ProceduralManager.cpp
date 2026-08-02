/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2022 Petr Ohlidal

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

#include "ProceduralManager.h"

#include "Application.h"
#include "ProceduralRoad.h"

#include <algorithm>
#include <exception>
#include <utility>

using namespace Ogre;
using namespace RoR;

#pragma region ProceduralObject

ProceduralPointPtr ProceduralObject::getPoint(int pos)
{
    if (pos >= 0 && pos < (int)points.size())
    {
        return points[pos];
    }
    else
    {
        return ProceduralPointPtr();
    }
}

void ProceduralObject::insertPoint(int pos, ProceduralPointPtr p)
{
    if (pos >= 0 && pos < (int)points.size())
    {
        points.insert(points.begin() + pos, p);
    }
}

void ProceduralObject::deletePoint(int pos)
{
    if (pos >= 0 && pos < (int)points.size())
    {
        points.erase(points.begin() + pos);
    }
}

#pragma endregion

#pragma region ProceduralManager

ProceduralManager::ProceduralManager(Ogre::SceneNode* groupingSceneNode)
    : pGroupingSceneNode(groupingSceneNode)
{
}

ProceduralManager::~ProceduralManager()
{
    this->removeAllObjects();
}

ProceduralObjectPtr ProceduralManager::getObject(int pos)
{
    if (pos >= 0 && pos < (int)pObjects.size())
    {
        return pObjects[pos];
    }
    else
    {
        return ProceduralObjectPtr();
    }
}

void ProceduralManager::removeAllObjects()
{
    for (ProceduralObjectPtr obj : pObjects)
    {
        RoR::Render::ValidationResult validation =
            m_graphics_identity_allocator.Tombstone(
                obj->m_graphics_identity);
        if (!validation)
            LogMutationFailure("remove-all", validation);
        this->deleteObjectMesh(obj);
    }
    pObjects.clear(); // delete (unreference) all objects.
}

void ProceduralManager::deleteObjectMesh(ProceduralObjectPtr po)
{
    if (po != nullptr && po->road)
    {
        // loaded already, delete (unreference) old object
        po->road = ProceduralRoadPtr();
    }
}

void ProceduralManager::removeObject(ProceduralObjectPtr po)
{
    const RoR::Render::ValidationResult validation = TryRemoveObject(po);
    if (!validation)
        LogMutationFailure("remove", validation);
}

RoR::Render::ValidationResult ProceduralManager::TryRemoveObject(
    ProceduralObjectPtr po)
{
    if (po == nullptr)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_object",
            "cannot remove a null procedural object");
    }
    const auto found = std::find(pObjects.begin(), pObjects.end(), po);
    if (found == pObjects.end())
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_object.graphics_identity",
            "procedural object is not live in this manager");
    }
    if (std::find(found + 1, pObjects.end(), po) != pObjects.end())
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::DUPLICATE_IDENTIFIER,
            "procedural_object.graphics_identity",
            "manager contains duplicate references to one procedural object");
    }

    RoR::Render::Ogre14ProceduralRoadIdentityState identity_candidate =
        po->m_graphics_identity;
    RoR::Render::ValidationResult validation =
        m_graphics_identity_allocator.Tombstone(identity_candidate);
    if (!validation)
        return validation;

    deleteObjectMesh(po);
    po->m_graphics_identity = std::move(identity_candidate);
    pObjects.erase(found);
    return RoR::Render::ValidationResult::Success();
}

RoR::Render::ValidationResult ProceduralManager::BuildObjectMeshCandidate(
    ProceduralObjectPtr po,
    ProceduralRoadPtr& road,
    std::string& exact_geometry_state_key)
{
    if (po == nullptr || pGroupingSceneNode == nullptr)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            po == nullptr ? "procedural_object" : "procedural_scene_node",
            po == nullptr
                ? "cannot build a null procedural object"
                : "procedural-road grouping scene node is unavailable");
    }
    if (po->points.empty())
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_object.points",
            "a procedural road requires at least one control point");
    }
    for (std::size_t point_index = 0U;
         point_index < po->points.size(); ++point_index)
    {
        if (po->points[point_index] == nullptr)
        {
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::MISSING_REFERENCE,
                "procedural_object.points",
                "procedural-road control points must be non-null",
                point_index);
        }
    }

    ProceduralRoadPtr candidate = new ProceduralRoad();
    candidate->setCollisionEnabled(po->collision_enabled);
    candidate->setEndCapCollisionEnabled(po->collision_endcaps_enabled);

    Ogre::SimpleSpline spline;
    if (po->smoothing_num_splits > 0)
    {
        // Init smoothing
        spline.setAutoCalculate(false);
        for (ProceduralPointPtr& pp : po->points)
        {
            spline.addPoint(pp->position);
        }
        spline.recalcTangents();
    }

    for (int i_point = 0; i_point < po->getNumPoints(); i_point++)
    {
        ProceduralPointPtr pp = po->getPoint(i_point);
        if (po->smoothing_num_splits > 0)
        {
            const int num_segments = po->smoothing_num_splits + 1;

            // smoothing on
            for (int i_seg = 1; i_seg <= num_segments; i_seg++)
            {
                if (i_point == 0)
                {
                    candidate->addBlock(pp->position, pp->rotation, pp->type, pp->width, pp->bwidth, pp->bheight, pp->pillartype);
                }
                else
                {
                    const float progress = static_cast<float>(i_seg) / static_cast<float>(num_segments);
                    ProceduralPointPtr prev_pp = po->getPoint(i_point - 1);

                    const Ogre::Vector3 smooth_pos = spline.interpolate(i_point - 1, progress);
                    const Ogre::Quaternion smooth_rot = Quaternion::nlerp(progress, prev_pp->rotation, pp->rotation);
                    const float smooth_width = Math::lerp(prev_pp->width, pp->width, progress);
                    const float smooth_bwidth = Math::lerp(prev_pp->bwidth, pp->bwidth, progress);
                    const float smooth_bheight = Math::lerp(prev_pp->bheight, pp->bheight, progress);

                    candidate->addBlock(smooth_pos, smooth_rot, pp->type, smooth_width, smooth_bwidth, smooth_bheight, pp->pillartype);
                }
            }
        }
        else
        {
            // smoothing off
            candidate->addBlock(pp->position, pp->rotation, pp->type, pp->width, pp->bwidth, pp->bheight, pp->pillartype);
        }
    }
    candidate->finish(pGroupingSceneNode);
    const RoR::Render::Ogre14ProceduralRoadCapture snapshot =
        candidate->CopyFinalizedGraphicsSnapshot();
    std::string state_key;
    RoR::Render::ValidationResult validation = RoR::Render::
        BuildOgre14ProceduralRoadGeometryStateKey(snapshot, state_key);
    if (!validation)
        return validation;

    road = candidate;
    exact_geometry_state_key = std::move(state_key);
    return RoR::Render::ValidationResult::Success();
}

void ProceduralManager::rebuildObjectMesh(ProceduralObjectPtr po)
{
    const RoR::Render::ValidationResult validation =
        TryRebuildObjectMesh(po);
    if (!validation)
        LogMutationFailure("rebuild", validation);
}

RoR::Render::ValidationResult ProceduralManager::TryRebuildObjectMesh(
    ProceduralObjectPtr po)
{
    if (po == nullptr)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_object",
            "cannot rebuild a null procedural object");
    }
    const auto found = std::find(pObjects.begin(), pObjects.end(), po);
    if (found == pObjects.end())
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_object.graphics_identity",
            "only a live manager-owned procedural object may be rebuilt");
    }
    if (std::find(found + 1, pObjects.end(), po) != pObjects.end())
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::DUPLICATE_IDENTIFIER,
            "procedural_object.graphics_identity",
            "manager contains duplicate references to one procedural object");
    }

    try
    {
        ProceduralRoadPtr candidate_road;
        std::string state_key;
        RoR::Render::ValidationResult validation =
            BuildObjectMeshCandidate(po, candidate_road, state_key);
        if (!validation)
            return validation;

        RoR::Render::Ogre14ProceduralRoadIdentityState identity_candidate =
            po->m_graphics_identity;
        validation = m_graphics_identity_allocator.FinalizeGeometry(
            identity_candidate, std::move(state_key));
        if (!validation)
            return validation;
        if (!candidate_road->AssignFinalizedGraphicsLineage(
                identity_candidate.stable_graphics_id(),
                identity_candidate.topology_revision()))
        {
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "procedural_road.finalized_lineage",
                "candidate road rejected its finalized manager lineage");
        }

        // Both assignments are non-allocating moves. The old road (including
        // its collision triangles and OGRE resources) remains intact until
        // every candidate validation has succeeded.
        po->road = candidate_road;
        po->m_graphics_identity = std::move(identity_candidate);
        return RoR::Render::ValidationResult::Success();
    }
    catch (const std::exception& exception)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_road.native_build",
            exception.what());
    }
    catch (...)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_road.native_build",
            "unknown exception while rebuilding procedural road");
    }
}

void ProceduralManager::addObject(ProceduralObjectPtr po)
{
    const RoR::Render::ValidationResult validation = TryAddObject(po);
    if (!validation)
        LogMutationFailure("add", validation);
}

RoR::Render::ValidationResult ProceduralManager::TryAddObject(
    ProceduralObjectPtr po)
{
    if (po == nullptr)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_object",
            "cannot add a null procedural object");
    }
    if (po->road)
    {
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::DUPLICATE_IDENTIFIER,
            "procedural_object.road",
            "an unregistered procedural object must not own a native road");
    }
    RoR::Render::Ogre14ProceduralRoadIdentityState identity_candidate =
        po->m_graphics_identity;
    bool identity_reserved = false;
    try
    {
        pObjects.reserve(pObjects.size() + 1U);
        RoR::Render::ValidationResult validation =
            m_graphics_identity_allocator.Reserve(identity_candidate);
        if (!validation)
            return validation;
        identity_reserved = true;

        ProceduralRoadPtr candidate_road;
        std::string state_key;
        validation = BuildObjectMeshCandidate(
            po, candidate_road, state_key);
        if (!validation)
        {
            const RoR::Render::ValidationResult tombstone =
                m_graphics_identity_allocator.Tombstone(identity_candidate);
            if (tombstone)
                po->m_graphics_identity = std::move(identity_candidate);
            return validation;
        }
        validation = m_graphics_identity_allocator.FinalizeGeometry(
            identity_candidate, std::move(state_key));
        if (!validation ||
            !candidate_road->AssignFinalizedGraphicsLineage(
                identity_candidate.stable_graphics_id(),
                identity_candidate.topology_revision()))
        {
            const RoR::Render::ValidationResult tombstone =
                m_graphics_identity_allocator.Tombstone(identity_candidate);
            if (tombstone)
                po->m_graphics_identity = std::move(identity_candidate);
            if (!validation)
                return validation;
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "procedural_road.finalized_lineage",
                "candidate road rejected its finalized manager lineage");
        }

        po->road = candidate_road;
        po->m_graphics_identity = std::move(identity_candidate);
        pObjects.push_back(po);
        identity_reserved = false;
        return RoR::Render::ValidationResult::Success();
    }
    catch (const std::exception& exception)
    {
        if (identity_reserved)
        {
            const RoR::Render::ValidationResult tombstone =
                m_graphics_identity_allocator.Tombstone(identity_candidate);
            if (tombstone)
                po->m_graphics_identity = std::move(identity_candidate);
            deleteObjectMesh(po);
        }
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_road.native_build",
            exception.what());
    }
    catch (...)
    {
        if (identity_reserved)
        {
            const RoR::Render::ValidationResult tombstone =
                m_graphics_identity_allocator.Tombstone(identity_candidate);
            if (tombstone)
                po->m_graphics_identity = std::move(identity_candidate);
            deleteObjectMesh(po);
        }
        return RoR::Render::ValidationResult::Failure(
            RoR::Render::ValidationCode::MISSING_REFERENCE,
            "procedural_road.native_build",
            "unknown exception while adding procedural road");
    }
}

RoR::Render::ValidationResult
ProceduralManager::CopyFinalizedGraphicsSnapshots(
    std::vector<RoR::Render::Ogre14ProceduralRoadCapture>& snapshots) const
{
    std::vector<RoR::Render::Ogre14ProceduralRoadCapture> candidate;
    candidate.reserve(pObjects.size());
    for (std::size_t index = 0U; index < pObjects.size(); ++index)
    {
        const ProceduralObjectPtr& object = pObjects[index];
        if (object == nullptr || !object->road ||
            object->m_graphics_identity.lifecycle() != RoR::Render::
                Ogre14ProceduralRoadIdentityLifecycle::LIVE ||
            !object->road->HasFinalizedGraphicsSnapshot())
        {
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::MISSING_REFERENCE,
                "procedural_roads.finalized_snapshot",
                "live procedural-road inventory contains incomplete graphics state",
                index);
        }
        RoR::Render::Ogre14ProceduralRoadCapture snapshot =
            object->road->CopyFinalizedGraphicsSnapshot();
        if (snapshot.stable_graphics_id !=
                object->m_graphics_identity.stable_graphics_id() ||
            snapshot.topology_revision !=
                object->m_graphics_identity.topology_revision())
        {
            return RoR::Render::ValidationResult::Failure(
                RoR::Render::ValidationCode::REVISION_MISMATCH,
                "procedural_roads.finalized_snapshot.lineage",
                "road snapshot lineage disagrees with its manager identity",
                index);
        }
        candidate.push_back(std::move(snapshot));
    }
    std::sort(candidate.begin(), candidate.end(),
        [](const auto& first, const auto& second)
        {
            return first.stable_graphics_id < second.stable_graphics_id;
        });
    snapshots = std::move(candidate);
    return RoR::Render::ValidationResult::Success();
}

void ProceduralManager::LogMutationFailure(
    const char* operation,
    const RoR::Render::ValidationResult& validation) const
{
    LogFormat(
        "[RoR|ProceduralManager|GraphicsIdentity] operation=%s field=%s detail=%s",
        operation != nullptr ? operation : "unknown",
        validation.field.c_str(), validation.detail.c_str());
}

void ProceduralManager::logDiagnostics()
{
    Log("[RoR] Procedural road diagnostic.\n"
        "    types: 0=ROAD_AUTOMATIC, 1=ROAD_FLAT, 2=ROAD_LEFT, 3=ROAD_RIGHT, 4=ROAD_BOTH, 5=ROAD_BRIDGE, 6=ROAD_MONORAIL\n"
        "    pillartypes: 0=none, 1=road bridge, 2=monorail, 3=road bridge side piers");
    for (int i=0; i< (int) pObjects.size(); ++i)
    {
        LogFormat("~~~~~~ ProceduralObject %d ~~~~~~", i);
        ProceduralObjectPtr& po=pObjects[i];
        for (int j = 0; j<(int)po->points.size(); ++j)
        {
            ProceduralPointPtr& pp = po->points[j];
            LogFormat("\t Point [%d] posXYZ %f %f %f, type %d, width %f, bwidth %f, bheight %f, pillartype %i",
                                j,   pp->position.x, pp->position.y, pp->position.z,
                                                   pp->type, pp->width, pp->bwidth, pp->bheight, pp->pillartype);
        }
    }
}

#pragma endregion
