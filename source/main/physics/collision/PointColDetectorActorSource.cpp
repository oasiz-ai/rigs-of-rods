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
#include <cmath>

using namespace RoR;

void PointColDetector::UpdateIntraPoint(bool contactables)
{
    const int contacters_size = contactables ?
        m_actor->ar_num_contactable_nodes : m_actor->ar_num_contacters;

    if (contacters_size != m_object_list_size)
    {
        m_collision_partners.clear();
        m_collision_partners.push_back(m_actor->ar_instance_id);
        m_object_list_size = contacters_size;
        update_structures_for_contacters(contactables);
    }
    else
    {
        refresh_node_positions();
    }

    m_kdtree[0].refid = REFELEMID_INVALID;
    m_kdtree[0].begin = 0;
    m_kdtree[0].end = -m_object_list_size;
}

void PointColDetector::UpdateInterPoint(bool ignorestate)
{
    int contacters_size = 0;
    std::vector<ActorInstanceID_t> collision_partners;
    for (ActorPtr& actor :
            App::GetGameContext()->GetActorManager()->GetActors())
    {
        if (actor != m_actor && (ignorestate || actor->ar_update_physics) &&
                m_actor->ar_bounding_box.intersects(actor->ar_bounding_box))
        {
            collision_partners.push_back(actor->ar_instance_id);
            const bool is_linked = std::find(
                m_actor->ar_linked_actors.begin(),
                m_actor->ar_linked_actors.end(), actor) !=
                m_actor->ar_linked_actors.end();
            contacters_size += is_linked ?
                actor->ar_num_contacters : actor->ar_num_contactable_nodes;
            if (m_actor->ar_nodes[0].Velocity.squaredDistance(
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
        }
    }

    std::sort(collision_partners.begin(), collision_partners.end());

    m_actor->ar_collision_relevant = (contacters_size > 0);

    if (collision_partners != m_collision_partners ||
            contacters_size != m_object_list_size)
    {
        m_collision_partners = collision_partners;
        m_object_list_size = contacters_size;
        update_structures_for_contacters(false);
    }
    else
    {
        refresh_node_positions();
    }

    m_kdtree[0].refid = REFELEMID_INVALID;
    m_kdtree[0].begin = 0;
    m_kdtree[0].end = -m_object_list_size;
}

void PointColDetector::update_structures_for_contacters(bool ignoreinternal)
{
    m_ref_list.resize(m_object_list_size);
    hit_pointid_list.resize(m_object_list_size);

    int refi = 0;
    for (ActorInstanceID_t actorid : m_collision_partners)
    {
        const ActorPtr& actor =
            App::GetGameContext()->GetActorManager()->GetActorById(actorid);

        const bool is_linked = std::find(
            m_actor->ar_linked_actors.begin(),
            m_actor->ar_linked_actors.end(), actor) !=
            m_actor->ar_linked_actors.end();
        const bool internal_collision = !ignoreinternal &&
            ((actorid == m_actor->ar_instance_id) || is_linked);
        for (int i = 0; i < actor->ar_num_nodes; ++i)
        {
            if (actor->ar_nodes[i].nd_contacter ||
                    (!internal_collision &&
                        actor->ar_nodes[i].nd_contactable))
            {
                hit_pointid_list[refi].actorid = actor->ar_instance_id;
                hit_pointid_list[refi].nodenum = static_cast<NodeNum_t>(i);
                m_ref_list[refi].pidrefid = refi;
                m_ref_list[refi].setPoint(actor->ar_nodes[i].AbsPosition);
                ++refi;
            }
        }
    }

    m_kdtree.resize(std::max(
        1.0,
        std::pow(2, std::ceil(std::log2(m_object_list_size)) + 1)));
}

void PointColDetector::refresh_node_positions()
{
    // The reference list caches node positions, so refresh it every tick.
    // Looping actors first avoids repeated actor-manager lookups.
    for (ActorPtr& actor :
            App::GetGameContext()->GetActorManager()->GetActors())
    {
        for (refelem_t& refelem : m_ref_list)
        {
            const pointid_t& point_id =
                hit_pointid_list[refelem.pidrefid];
            if (point_id.actorid == actor->ar_instance_id)
            {
                refelem.setPoint(
                    actor->ar_nodes[point_id.nodenum].AbsPosition);
            }
        }
    }
}
