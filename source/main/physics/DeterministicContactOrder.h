/*
    This source file is part of Rigs of Rods
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

/// @file
/// @brief Dependency-free stable keys and task-buffer merge for contacts.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

namespace RoR {
namespace DeterministicContactOrder {

/// Canonical key for a broad-phase point candidate. `candidate` is a final
/// stable tie-breaker for malformed content that repeats an actor/node pair.
struct PointKey
{
    std::int32_t actor = 0;
    std::uint32_t node = 0;
    std::int32_t candidate = 0;

    PointKey() = default;
    PointKey(
        std::int32_t actor_id,
        std::uint32_t node_id,
        std::int32_t candidate_id):
        actor(actor_id),
        node(node_id),
        candidate(candidate_id)
    {
    }

    bool operator<(const PointKey& other) const
    {
        if (actor != other.actor)
            return actor < other.actor;
        if (node != other.node)
            return node < other.node;
        return candidate < other.candidate;
    }

    bool operator==(const PointKey& other) const
    {
        return actor == other.actor &&
            node == other.node &&
            candidate == other.candidate;
    }
};

/// Canonical key for one point-versus-triangle inter-actor contact.
struct InterActorKey
{
    std::int32_t surface_actor = 0;
    std::uint32_t surface_contact = 0;
    std::int32_t hit_actor = 0;
    std::uint32_t hit_node = 0;

    InterActorKey() = default;
    InterActorKey(
        std::int32_t surface_actor_id,
        std::uint32_t surface_contact_id,
        std::int32_t hit_actor_id,
        std::uint32_t hit_node_id):
        surface_actor(surface_actor_id),
        surface_contact(surface_contact_id),
        hit_actor(hit_actor_id),
        hit_node(hit_node_id)
    {
    }

    bool operator<(const InterActorKey& other) const
    {
        if (surface_actor != other.surface_actor)
            return surface_actor < other.surface_actor;
        if (surface_contact != other.surface_contact)
            return surface_contact < other.surface_contact;
        if (hit_actor != other.hit_actor)
            return hit_actor < other.hit_actor;
        return hit_node < other.hit_node;
    }

    bool operator==(const InterActorKey& other) const
    {
        return surface_actor == other.surface_actor &&
            surface_contact == other.surface_contact &&
            hit_actor == other.hit_actor &&
            hit_node == other.hit_node;
    }
};

/// Streaming assertion helper for bounded-memory fallbacks that cannot sort a
/// complete contact set. The producer must emit nondecreasing keys.
template <typename Key>
class CanonicalOrderValidator
{
public:
    bool Observe(const Key& key)
    {
        const bool canonical = !m_has_previous || !(key < m_previous);
        m_previous = key;
        m_has_previous = true;
        return canonical;
    }

private:
    bool m_has_previous = false;
    Key m_previous;
};

/// Maximum number of inter-actor contacts retained by the parallel fast path.
/// Overflow never drops a contact: it selects the serial bounded-memory path.
static const std::size_t INTER_ACTOR_CONTACT_BUDGET = 65536;

/// Splits a fixed storage budget across tasks in stable task order. Earlier
/// tasks receive the remainder one item at a time.
inline std::vector<std::size_t> AllocateTaskQuotas(
    std::size_t task_count,
    std::size_t item_budget)
{
    if (task_count == 0)
        return std::vector<std::size_t>();

    const std::size_t base_quota = item_budget / task_count;
    const std::size_t remainder = item_budget % task_count;
    std::vector<std::size_t> quotas(task_count, base_quota);
    for (std::size_t task_index = 0;
            task_index < remainder;
            ++task_index)
    {
        ++quotas[task_index];
    }
    return quotas;
}

/// A task-private vector with a hard item quota. `TryPush()` keeps scanning
/// after overflow while refusing allocations beyond the assigned quota.
template <typename Item>
class BoundedTaskBuffer
{
public:
    BoundedTaskBuffer() = default;

    explicit BoundedTaskBuffer(std::size_t quota):
        m_quota(quota)
    {
    }

    bool TryPush(const Item& item)
    {
        if (m_allocation_failed || m_items.size() >= m_quota)
        {
            m_overflowed = true;
            return false;
        }
        try
        {
            m_items.push_back(item);
        }
        catch (const std::bad_alloc&)
        {
            // Discovery can run on a worker. Convert allocation failure into
            // the same whole-step serial fallback instead of letting an
            // exception escape the thread-pool task.
            m_allocation_failed = true;
            m_overflowed = true;
            return false;
        }
        return true;
    }

    std::size_t GetQuota() const { return m_quota; }
    bool HasOverflowed() const { return m_overflowed; }
    bool HasAllocationFailed() const { return m_allocation_failed; }
    const std::vector<Item>& GetItems() const { return m_items; }
    std::vector<Item>& GetMutableItems() { return m_items; }

    /// Reuse already-reserved task storage on the next physics step.
    void Reset(std::size_t quota)
    {
        m_quota = quota;
        m_items.clear();
        m_overflowed = false;
        m_allocation_failed = false;
    }

private:
    std::size_t m_quota = 0;
    bool m_overflowed = false;
    bool m_allocation_failed = false;
    std::vector<Item> m_items;
};

/// Canonicalizes candidates whose discovery order may depend on a tree layout
/// or worker schedule.
template <typename Item, typename KeyProvider>
void SortByKey(std::vector<Item>& items, const KeyProvider& key_provider)
{
    std::sort(
        items.begin(),
        items.end(),
        [&key_provider](const Item& left, const Item& right)
        {
            return key_provider(left) < key_provider(right);
        });
}

template <typename Item>
bool AnyTaskBufferOverflowed(
    const std::vector<BoundedTaskBuffer<Item>>& buffers)
{
    for (const BoundedTaskBuffer<Item>& buffer : buffers)
    {
        if (buffer.HasOverflowed())
            return true;
    }
    return false;
}

template <typename Item>
bool AnyTaskBufferAllocationFailed(
    const std::vector<BoundedTaskBuffer<Item>>& buffers)
{
    for (const BoundedTaskBuffer<Item>& buffer : buffers)
    {
        if (buffer.HasAllocationFailed())
            return true;
    }
    return false;
}

/// Runs exactly one path. The fallback owns contact enumeration because a
/// partial fast-path buffer must never be consumed after any task overflows.
template <
    typename Item,
    typename KeyProvider,
    typename FastPath,
    typename FallbackPath>
bool ProcessTaskBuffersOrFallback(
    std::vector<BoundedTaskBuffer<Item>>& buffers,
    const KeyProvider& key_provider,
    const FastPath& fast_path,
    const FallbackPath& fallback_path)
{
    if (AnyTaskBufferOverflowed(buffers))
    {
        fallback_path();
        return false;
    }

    // Sort within the already quota-bounded storage, then verify that stable
    // task order also provides globally nondecreasing key ranges. Production
    // assigns one buffer per sorted surface actor; a violated range contract
    // safely selects the serial enumerator instead of allocating a full copy.
    const Item* previous = nullptr;
    for (BoundedTaskBuffer<Item>& buffer : buffers)
    {
        std::vector<Item>& items = buffer.GetMutableItems();
        SortByKey(items, key_provider);
        for (const Item& item : items)
        {
            if (previous != nullptr &&
                    key_provider(item) < key_provider(*previous))
            {
                fallback_path();
                return false;
            }
            previous = &item;
        }
    }

    fast_path(buffers);
    return true;
}

} // namespace DeterministicContactOrder
} // namespace RoR
