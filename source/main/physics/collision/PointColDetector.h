/*
    This source file is part of Rigs of Rods
    Copyright 2009 Lefteris Stamatogiannakis

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

#pragma once

#if defined(ROR_POINT_COL_DETECTOR_STANDALONE)
#    include <cstdint>
#    include <limits>

namespace RoR {
typedef int ActorInstanceID_t;
static const ActorInstanceID_t ACTORINSTANCEID_INVALID = 0;
typedef int PointidID_t;
static const PointidID_t POINTIDID_INVALID = -1;
typedef int RefelemID_t;
static const RefelemID_t REFELEMID_INVALID = -1;
typedef std::uint16_t NodeNum_t;
static const NodeNum_t NODENUM_INVALID =
    std::numeric_limits<NodeNum_t>::max();
} // namespace RoR
#else
#    include "ForwardDeclarations.h"
#endif

#include <array>
#include <vector>

namespace RoR {

class Actor;

/// @addtogroup Physics
/// @{

/// @addtogroup Collisions
/// @{

class PointColDetector
{
public:

    struct pointid_t // use PointidID_t for indexing
    {
        ActorInstanceID_t actorid = ACTORINSTANCEID_INVALID;
        NodeNum_t nodenum = NODENUM_INVALID;
    };

    /// Test-only source record for the production broad-phase oracle. This
    /// bypasses actor discovery, but populates the same cached point list and
    /// exercises the same lazy KD-tree build, query, and canonical ordering as
    /// live collision detection.
    struct oracle_point_t
    {
        ActorInstanceID_t actorid = ACTORINSTANCEID_INVALID;
        NodeNum_t nodenum = NODENUM_INVALID;
        std::array<float, 3> point = {{0.f, 0.f, 0.f}};
    };

    std::vector<PointidID_t> hit_list;
    std::vector<pointid_t> hit_pointid_list;

    PointColDetector(): m_actor(nullptr), m_object_list_size(-1) {}
    explicit PointColDetector(Actor* actor):
        m_actor(actor), m_object_list_size(-1) {}

    void UpdateIntraPoint(bool contactables = false);
    void UpdateInterPoint(bool ignorestate = false);
    void query(
        const std::array<float, 3>& vec1,
        const std::array<float, 3>& vec2,
        const std::array<float, 3>& vec3,
        float enlargeBB);
    template <typename Vector3Like>
    void query(
        const Vector3Like& vec1,
        const Vector3Like& vec2,
        const Vector3Like& vec3,
        float enlargeBB)
    {
        query(
            {{static_cast<float>(vec1.x), static_cast<float>(vec1.y),
              static_cast<float>(vec1.z)}},
            {{static_cast<float>(vec2.x), static_cast<float>(vec2.y),
              static_cast<float>(vec2.z)}},
            {{static_cast<float>(vec3.x), static_cast<float>(vec3.y),
              static_cast<float>(vec3.z)}},
            enlargeBB);
    }

    /// Replaces the cached production broad-phase source transactionally.
    /// Empty fixtures model a legitimate no-source frame. Invalid actor/node
    /// identities and non-finite positions are rejected without changing the
    /// previously loaded fixture. The same private transaction is used by
    /// live actor discovery, so mutation/churn tests exercise the production
    /// source-cache behavior rather than a second KD-tree implementation.
    bool LoadProductionOracleFixture(
        const std::vector<oracle_point_t>& points);

private:

    struct refelem_t // use RefelemID_t for indexing
    {
        PointidID_t pidrefid = POINTIDID_INVALID;
        std::array<float, 3> point; // cached node AbsPosition
    };

    struct kdnode_t
    {
        float min;
        int end;
        float max;
        RefelemID_t refid = REFELEMID_INVALID;
        float middle;
        int begin;
    };

    struct actor_source_t
    {
        Actor* actor = nullptr; //!< Borrowed from ActorManager only during UpdateInterPoint().
        bool linked = false;
    };

    Actor*                   m_actor = nullptr; //!< Non-owning; owning Actor deletes both detectors.
    std::vector<ActorInstanceID_t>    m_collision_partners; //!< IntraPoint: always just owning actor; InterPoint: all colliding actors
    std::vector<ActorInstanceID_t>    m_collision_partners_scratch;
    std::vector<actor_source_t>       m_actor_sources_scratch;
    std::vector<oracle_point_t>       m_actor_source_scratch; //!< Retained staging; never authoritative until transaction commit
    std::vector<refelem_t> m_ref_list;
    
    std::vector<kdnode_t>  m_kdtree;
    std::array<float, 3>   m_bbmin = {{0.f, 0.f, 0.f}};
    std::array<float, 3>   m_bbmax = {{0.f, 0.f, 0.f}};
    int                    m_object_list_size = 0;

    void queryrec(int kdindex, int axis);
    void build_kdtree_incr(int axis, int index);
    void partintwo(const int start, const int median, const int end, const int axis, float& minex, float& maxex);
    bool replace_actor_source_snapshot(
        const std::vector<oracle_point_t>& points);
};

/// @} // addtogroup Collisions
/// @} // addtogroup Physics

} // namespace RoR
