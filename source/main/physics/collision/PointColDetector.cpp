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

#include "PointColDetector.h"

#include "DeterministicContactOrder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace Ogre;
using namespace RoR;

namespace {

bool IsFiniteBinary32(float value)
{
    // A direct float bit-cast may itself be folded under -ffinite-math-only.
    // Volatile byte reads make this an object-representation check even in
    // the game's release fast-math translation unit.
    const volatile unsigned char* source =
        reinterpret_cast<const volatile unsigned char*>(&value);
    unsigned char bytes[sizeof(value)] = {};
    for (std::size_t index = 0; index < sizeof(value); ++index)
        bytes[index] = source[index];
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "binary32 size mismatch");
    std::memcpy(&bits, bytes, sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

} // namespace

bool PointColDetector::LoadProductionOracleFixture(
    const std::vector<oracle_point_t>& points)
{
    if (!replace_actor_source_snapshot(points))
    {
        return false;
    }

    m_collision_partners.clear();
    return true;
}

bool PointColDetector::replace_actor_source_snapshot(
    const std::vector<oracle_point_t>& points)
{
    if (points.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max()))
    {
        return false;
    }

    for (const oracle_point_t& point : points)
    {
        if (point.actorid == ACTORINSTANCEID_INVALID ||
                point.nodenum == NODENUM_INVALID ||
                !IsFiniteBinary32(point.point[0]) ||
                !IsFiniteBinary32(point.point[1]) ||
                !IsFiniteBinary32(point.point[2]))
        {
            return false;
        }
    }

    const bool same_topology =
        points.size() == hit_pointid_list.size() &&
        std::equal(
            points.begin(),
            points.end(),
            hit_pointid_list.begin(),
            [](const oracle_point_t& point, const pointid_t& point_id)
            {
                return point.actorid == point_id.actorid &&
                    point.nodenum == point_id.nodenum;
            });

    if (same_topology && m_ref_list.size() == points.size())
    {
        for (const refelem_t& ref_element : m_ref_list)
        {
            if (ref_element.pidrefid < 0 ||
                    static_cast<std::size_t>(ref_element.pidrefid) >=
                        points.size())
            {
                return false;
            }
        }
        if (m_kdtree.empty())
        {
            std::vector<kdnode_t> kdtree(1);
            m_kdtree.swap(kdtree);
        }
        for (refelem_t& ref_element : m_ref_list)
        {
            ref_element.point =
                points[static_cast<std::size_t>(
                    ref_element.pidrefid)].point;
        }
        m_object_list_size = static_cast<int>(points.size());
        m_kdtree[0].refid = REFELEMID_INVALID;
        m_kdtree[0].begin = 0;
        m_kdtree[0].end = -m_object_list_size;
        hit_list.clear();
        return true;
    }

    std::size_t leaf_capacity = 1;
    while (leaf_capacity < points.size())
    {
        if (leaf_capacity > std::numeric_limits<std::size_t>::max() / 2)
            return false;
        leaf_capacity *= 2;
    }
    if (leaf_capacity > std::numeric_limits<std::size_t>::max() / 2)
        return false;

    std::vector<refelem_t> ref_list(points.size());
    std::vector<pointid_t> point_ids(points.size());
    std::vector<kdnode_t> kdtree(leaf_capacity * 2);
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        point_ids[index].actorid = points[index].actorid;
        point_ids[index].nodenum = points[index].nodenum;
        ref_list[index].pidrefid = static_cast<PointidID_t>(index);
        ref_list[index].point = points[index].point;
    }

    m_ref_list.swap(ref_list);
    hit_pointid_list.swap(point_ids);
    m_kdtree.swap(kdtree);
    m_object_list_size = static_cast<int>(points.size());
    m_kdtree[0].refid = REFELEMID_INVALID;
    m_kdtree[0].begin = 0;
    m_kdtree[0].end = -m_object_list_size;
    hit_list.clear();
    return true;
}

void PointColDetector::query(const Vector3 &vec1, const Vector3 &vec2, const Vector3 &vec3, float enlargeBB)
{
    m_bbmin = vec1;

    m_bbmin.x = std::min(vec2.x, m_bbmin.x);
    m_bbmin.x = std::min(vec3.x, m_bbmin.x);

    m_bbmin.y = std::min(vec2.y, m_bbmin.y);
    m_bbmin.y = std::min(vec3.y, m_bbmin.y);

    m_bbmin.z = std::min(vec2.z, m_bbmin.z);
    m_bbmin.z = std::min(vec3.z, m_bbmin.z);

    m_bbmin -= enlargeBB;

    m_bbmax = vec1;

    m_bbmax.x = std::max(m_bbmax.x, vec2.x);
    m_bbmax.x = std::max(m_bbmax.x, vec3.x);

    m_bbmax.y = std::max(m_bbmax.y, vec2.y);
    m_bbmax.y = std::max(m_bbmax.y, vec3.y);

    m_bbmax.z = std::max(m_bbmax.z, vec2.z);
    m_bbmax.z = std::max(m_bbmax.z, vec3.z);

    m_bbmax += enlargeBB;

    hit_list.clear();
    if (m_object_list_size <= 0 || m_kdtree.empty())
        return;
    queryrec(0, 0);
    DeterministicContactOrder::SortByKey(
        hit_list,
        [this](PointidID_t candidate)
        {
            const pointid_t& point_id = hit_pointid_list[candidate];
            DeterministicContactOrder::PointKey key;
            key.actor = point_id.actorid;
            key.node = point_id.nodenum;
            key.candidate = candidate;
            return key;
        });
}

void PointColDetector::queryrec(int kdindex, int axis)
{
    for (;;)
    {
        if (m_kdtree[kdindex].end < 0)
        {
            build_kdtree_incr(axis, kdindex);
        }

        if (m_kdtree[kdindex].refid != REFELEMID_INVALID)
        {
            const std::array<float, 3> point = m_ref_list[m_kdtree[kdindex].refid].point;
            if (point[0] >= m_bbmin.x && point[0] <= m_bbmax.x &&
                point[1] >= m_bbmin.y && point[1] <= m_bbmax.y &&
                point[2] >= m_bbmin.z && point[2] <= m_bbmax.z)
            {
                hit_list.push_back(m_ref_list[m_kdtree[kdindex].refid].pidrefid);
            }
            return;
        }

        if (m_bbmax[axis] >= m_kdtree[kdindex].middle)
        {
            if (m_bbmin[axis] > m_kdtree[kdindex].max)
            {
                return;
            }

            int newaxis = axis + 1;

            if (newaxis >= 3)
            {
                newaxis = 0;
            }

            int newindex = kdindex + kdindex + 1;

            if (m_bbmin[axis] <= m_kdtree[kdindex].middle)
            {
                queryrec(newindex, newaxis);
            }

            kdindex = newindex + 1;
            axis = newaxis;
        }
        else
        {
            if (m_bbmax[axis] < m_kdtree[kdindex].min)
            {
                return;
            }

            kdindex = 2 * kdindex + 1;
            axis++;

            if (axis >= 3)
            {
                axis = 0;
            }
        }
    }
}

void PointColDetector::build_kdtree_incr(int axis, int index)
{
    int end = -m_kdtree[index].end;
    m_kdtree[index].end = end;
    RefelemID_t begin = m_kdtree[index].begin;
    RefelemID_t median;
    int slice_size = end - begin;
    if (slice_size != 1)
    {
        int newindex=index+index+1;
        if (slice_size == 2)
        {
            median = begin+1;
            if (m_ref_list[begin].point[axis] > m_ref_list[median].point[axis])
            {
                std::swap(m_ref_list[begin], m_ref_list[median]);
            }

            m_kdtree[index].min = m_ref_list[begin].point[axis];
            m_kdtree[index].max = m_ref_list[median].point[axis];
            m_kdtree[index].middle = m_kdtree[index].max;
            m_kdtree[index].refid = REFELEMID_INVALID;

            axis++;
            if (axis >= 3)
            {
                axis = 0;
            }

            m_kdtree[newindex].refid = begin;
            m_kdtree[newindex].middle = m_ref_list[m_kdtree[newindex].refid].point[axis];
            m_kdtree[newindex].min = m_kdtree[newindex].middle;
            m_kdtree[newindex].max = m_kdtree[newindex].middle;
            m_kdtree[newindex].end = median;
            newindex++;

            m_kdtree[newindex].refid = median;
            m_kdtree[newindex].middle = m_ref_list[m_kdtree[newindex].refid].point[axis];
            m_kdtree[newindex].min = m_kdtree[newindex].middle;
            m_kdtree[newindex].max = m_kdtree[newindex].middle;
            m_kdtree[newindex].end = end;
            return;
        }
        else
        {
            median = begin + (slice_size / 2);
            partintwo(begin, median, end, axis, m_kdtree[index].min, m_kdtree[index].max);
        }

        m_kdtree[index].middle = m_ref_list[median].point[axis];
        m_kdtree[index].refid = REFELEMID_INVALID;

        m_kdtree[newindex].begin = begin;
        m_kdtree[newindex].end = -median;

        newindex++;
        m_kdtree[newindex].begin = median;
        m_kdtree[newindex].end = -end;

    }
    else
    {
        m_kdtree[index].refid = begin;
        m_kdtree[index].middle = m_ref_list[m_kdtree[index].refid].point[axis];
        m_kdtree[index].min = m_kdtree[index].middle;
        m_kdtree[index].max = m_kdtree[index].middle;
    }
}

void PointColDetector::partintwo(const int start, const int median, const int end, const int axis, float &minex, float &maxex)
{
    int i, j, l, m;
    int k = median;
    l = start;
    m = end - 1;

    float x = m_ref_list[k].point[axis];
    while (l < m)
    {
        i = l;
        j = m;
        while (!(j < k || k < i))
        {
            while (m_ref_list[i].point[axis] < x)
            {
                i++;
            }
            while (x < m_ref_list[j].point[axis])
            {
                j--;
            }

            std::swap(m_ref_list[i], m_ref_list[j]);
            i++;
            j--;
        }
        if (j < k)
        {
            l = i;
        }
        if (k < i)
        {
            m = j;
        }
        x = m_ref_list[k].point[axis];
    }

    minex = x;
    maxex = x;
    for (int left_index = start; left_index < median; ++left_index)
    {
        minex = std::min(m_ref_list[left_index].point[axis], minex);
    }
    for (int right_index = median + 1; right_index < end; ++right_index)
    {
        maxex = std::max(maxex, m_ref_list[right_index].point[axis]);
    }
}
