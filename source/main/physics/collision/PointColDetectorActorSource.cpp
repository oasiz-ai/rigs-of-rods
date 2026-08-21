/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "PointColDetector.h"

#include "Application.h"
#include "Actor.h"
#include "ActorManager.h"
#include "GameContext.h"

#include <algorithm>
#include <vector>

using namespace RoR;

void PointColDetector::UpdateIntraPoint(bool contactables)
{
    std::vector<oracle_point_t>& points = m_actor_source_scratch;
    points.clear();
    const bool source_valid = m_actor != nullptr &&
        m_actor->ar_num_nodes >= 0 &&
        m_actor->ar_num_nodes <= static_cast<int>(NODENUM_INVALID);
    if (source_valid && m_actor->ar_num_nodes > 0)
    {
        points.reserve(static_cast<std::size_t>(m_actor->ar_num_nodes));
        for (int node_index = 0;
                node_index < m_actor->ar_num_nodes;
                ++node_index)
        {
            const node_t& node = m_actor->ar_nodes[node_index];
            if (node.nd_contacter ||
                    (contactables && node.nd_contactable))
            {
                oracle_point_t point;
                point.actorid = m_actor->ar_instance_id;
                point.nodenum = static_cast<NodeNum_t>(node_index);
                point.point = {{
                    node.AbsPosition.x,
                    node.AbsPosition.y,
                    node.AbsPosition.z}};
                points.push_back(point);
            }
        }
    }

    if (!source_valid || !replace_actor_source_snapshot(points))
    {
        // A non-finite or otherwise invalid actor source must not leave the
        // previous frame's nodes reachable through the broad phase.
        const std::vector<oracle_point_t> empty;
        replace_actor_source_snapshot(empty);
    }

    m_collision_partners.clear();
    if (m_actor != nullptr)
        m_collision_partners.push_back(m_actor->ar_instance_id);
}

void PointColDetector::UpdateInterPoint(bool ignorestate)
{
    if (m_actor == nullptr)
    {
        const std::vector<oracle_point_t> empty;
        replace_actor_source_snapshot(empty);
        m_collision_partners.clear();
        return;
    }

    std::vector<actor_source_t>& actor_sources = m_actor_sources_scratch;
    actor_sources.clear();
    for (ActorPtr& actor :
            App::GetGameContext()->GetActorManager()->GetActors())
    {
        if (actor != m_actor && (ignorestate || actor->ar_update_physics) &&
                m_actor->ar_bounding_box.intersects(actor->ar_bounding_box))
        {
            const bool is_linked = std::find(
                m_actor->ar_linked_actors.begin(),
                m_actor->ar_linked_actors.end(), actor) !=
                m_actor->ar_linked_actors.end();
            actor_sources.push_back({actor.GetRef(), is_linked});
        }
    }

    std::sort(
        actor_sources.begin(),
        actor_sources.end(),
        [](const actor_source_t& left, const actor_source_t& right)
        {
            return left.actor->ar_instance_id <
                right.actor->ar_instance_id;
        });

    std::vector<ActorInstanceID_t>& collision_partners =
        m_collision_partners_scratch;
    collision_partners.clear();
    std::vector<oracle_point_t>& points = m_actor_source_scratch;
    points.clear();
    collision_partners.reserve(actor_sources.size());
    bool source_valid = m_actor->ar_num_nodes >= 0 &&
        m_actor->ar_num_nodes <= static_cast<int>(NODENUM_INVALID);
    for (const actor_source_t& source : actor_sources)
    {
        Actor* const actor = source.actor;
        if (actor == nullptr || actor->ar_num_nodes < 0 ||
                actor->ar_num_nodes > static_cast<int>(NODENUM_INVALID))
        {
            source_valid = false;
            break;
        }
        collision_partners.push_back(actor->ar_instance_id);

        if (m_actor->ar_num_nodes > 0 && actor->ar_num_nodes > 0 &&
                m_actor->ar_nodes[0].Velocity.squaredDistance(
                    actor->ar_nodes[0].Velocity) > 16)
        {
            for (int i = 0; i < m_actor->ar_num_collcabs; ++i)
            {
                m_actor->ar_intra_collcabrate[i].rate = 0;
                m_actor->ar_inter_collcabrate[i].rate = 0;
            }
            for (int i = 0; i < actor->ar_num_collcabs; ++i)
            {
                actor->ar_intra_collcabrate[i].rate = 0;
                actor->ar_inter_collcabrate[i].rate = 0;
            }
        }

        for (int node_index = 0;
                node_index < actor->ar_num_nodes;
                ++node_index)
        {
            const node_t& node = actor->ar_nodes[node_index];
            if (node.nd_contacter ||
                    (!source.linked && node.nd_contactable))
            {
                oracle_point_t point;
                point.actorid = actor->ar_instance_id;
                point.nodenum = static_cast<NodeNum_t>(node_index);
                point.point = {{
                    node.AbsPosition.x,
                    node.AbsPosition.y,
                    node.AbsPosition.z}};
                points.push_back(point);
            }
        }
    }

    if (!source_valid || !replace_actor_source_snapshot(points))
    {
        const std::vector<oracle_point_t> empty;
        replace_actor_source_snapshot(empty);
        collision_partners.clear();
    }

    m_collision_partners.swap(collision_partners);
    m_actor->ar_collision_relevant = !points.empty() &&
        m_object_list_size > 0;
    // Retain vector capacity without retaining borrowed actor pointers across
    // actor removal/replacement between fixed steps.
    actor_sources.clear();
}
