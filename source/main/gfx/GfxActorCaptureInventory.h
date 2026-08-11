/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace RoR {

class GfxActor;

enum class GfxActorCaptureLifecycle : std::uint8_t
{
    LIVE = 0U,
    HIDDEN = 1U,
    DESTROYED = 2U,
};

enum class GfxActorCaptureMutation : std::uint8_t
{
    APPLIED = 0U,
    INVALID_ARGUMENT,
    ALREADY_LIVE,
    ALREADY_HIDDEN,
    ALREADY_DESTROYED,
    OWNER_MISMATCH,
    UNKNOWN_IDENTITY,
};

/// Durable capture-side actor identity owner. The active vector is reserved
/// before a map insertion or lifecycle flip, so an allocation failure cannot
/// publish a record that the game-update inventory does not own.
template <typename Owner, typename Allocator = std::allocator<std::byte>>
class BasicGfxActorCaptureInventory
{
public:
    struct Record
    {
        Owner* owner = nullptr;
        GfxActorCaptureLifecycle lifecycle =
            GfxActorCaptureLifecycle::DESTROYED;
    };

private:
    using RecordValue = std::pair<const std::int64_t, Record>;
    using RecordAllocator = typename std::allocator_traits<Allocator>::
        template rebind_alloc<RecordValue>;
    using OwnerAllocator = typename std::allocator_traits<Allocator>::
        template rebind_alloc<Owner*>;

public:
    using RecordMap = std::map<std::int64_t, Record, std::less<std::int64_t>,
                               RecordAllocator>;
    using ActiveOwners = std::vector<Owner*, OwnerAllocator>;

    explicit BasicGfxActorCaptureInventory(
        const Allocator& allocator = Allocator{})
        : m_records(std::less<std::int64_t>{}, RecordAllocator{allocator})
        , m_active(OwnerAllocator{allocator})
    {
    }

    [[nodiscard]] GfxActorCaptureMutation Register(
        std::int64_t identity, Owner* owner)
    {
        if (identity < 0 || owner == nullptr)
            return GfxActorCaptureMutation::INVALID_ARGUMENT;
        if (m_active.size() == m_active.max_size())
            return GfxActorCaptureMutation::INVALID_ARGUMENT;

        auto record = m_records.find(identity);
        if (record != m_records.end())
        {
            if (record->second.lifecycle ==
                GfxActorCaptureLifecycle::DESTROYED)
            {
                return GfxActorCaptureMutation::ALREADY_DESTROYED;
            }
            if (record->second.owner != owner)
                return GfxActorCaptureMutation::OWNER_MISMATCH;
            if (record->second.lifecycle == GfxActorCaptureLifecycle::LIVE)
                return GfxActorCaptureMutation::ALREADY_LIVE;

            m_active.reserve(m_active.size() + 1U);
            m_active.push_back(owner);
            record->second.lifecycle = GfxActorCaptureLifecycle::LIVE;
            return GfxActorCaptureMutation::APPLIED;
        }

        // Reserve first: if this throws, the durable map is untouched. Map
        // insertion itself has the strong guarantee. The final pointer push
        // cannot allocate and pointer assignment is noexcept.
        m_active.reserve(m_active.size() + 1U);
        const auto inserted = m_records.emplace(
            identity, Record{owner, GfxActorCaptureLifecycle::LIVE});
        if (!inserted.second)
            return GfxActorCaptureMutation::OWNER_MISMATCH;
        m_active.push_back(owner);
        return GfxActorCaptureMutation::APPLIED;
    }

    [[nodiscard]] GfxActorCaptureMutation Hide(
        std::int64_t identity, Owner* owner) noexcept
    {
        auto record = m_records.find(identity);
        if (record == m_records.end())
            return GfxActorCaptureMutation::UNKNOWN_IDENTITY;
        if (record->second.lifecycle == GfxActorCaptureLifecycle::DESTROYED)
            return GfxActorCaptureMutation::ALREADY_DESTROYED;
        if (record->second.owner != owner)
            return GfxActorCaptureMutation::OWNER_MISMATCH;
        if (record->second.lifecycle == GfxActorCaptureLifecycle::HIDDEN)
            return GfxActorCaptureMutation::ALREADY_HIDDEN;

        EraseActive(owner);
        record->second.lifecycle = GfxActorCaptureLifecycle::HIDDEN;
        return GfxActorCaptureMutation::APPLIED;
    }

    [[nodiscard]] GfxActorCaptureMutation Destroy(
        std::int64_t identity, Owner* owner) noexcept
    {
        auto record = m_records.find(identity);
        if (record == m_records.end())
            return GfxActorCaptureMutation::UNKNOWN_IDENTITY;
        if (record->second.lifecycle == GfxActorCaptureLifecycle::DESTROYED)
            return GfxActorCaptureMutation::ALREADY_DESTROYED;
        if (record->second.owner != owner)
            return GfxActorCaptureMutation::OWNER_MISMATCH;

        EraseActive(owner);
        record->second.owner = nullptr;
        record->second.lifecycle = GfxActorCaptureLifecycle::DESTROYED;
        return GfxActorCaptureMutation::APPLIED;
    }

    void Clear() noexcept
    {
        m_active.clear();
        m_records.clear();
    }

    [[nodiscard]] const RecordMap& Records() const noexcept
    {
        return m_records;
    }
    [[nodiscard]] ActiveOwners& Active() noexcept { return m_active; }
    [[nodiscard]] const ActiveOwners& Active() const noexcept
    {
        return m_active;
    }

private:
    void EraseActive(Owner* owner) noexcept
    {
        m_active.erase(
            std::remove(m_active.begin(), m_active.end(), owner),
            m_active.end());
    }

    RecordMap m_records;
    ActiveOwners m_active;
};

using GfxActorCaptureInventory = BasicGfxActorCaptureInventory<GfxActor>;

[[nodiscard]] constexpr bool IsGfxActorCaptureEffectivelyVisible(
    GfxActorCaptureLifecycle actor_lifecycle,
    bool parent_in_scene_graph,
    bool entity_visible,
    bool section_visible) noexcept
{
    return actor_lifecycle == GfxActorCaptureLifecycle::LIVE &&
        parent_in_scene_graph && entity_visible && section_visible;
}

} // namespace RoR
