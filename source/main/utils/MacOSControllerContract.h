/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RoR {
namespace MacOSControllerContract {

// This dependency-free state contract is the migration seam for a future SDL
// controller backend. The production macOS runtime still uses OIS.
enum class EventType
{
    AXIS,
    BUTTON,
    HAT
};

enum class ApplyResult
{
    APPLIED,
    NO_CHANGE,
    UNKNOWN_DEVICE,
    INVALID_COMPONENT,
    INVALID_VALUE
};

enum class AttachResult
{
    ATTACHED,
    DUPLICATE,
    INVALID_SHAPE,
    FULL
};

struct Event
{
    EventType    type = EventType::AXIS;
    std::int32_t instance_id = -1;
    std::size_t  component = 0;
    std::int32_t value = 0;
};

struct Slot
{
    enum : std::size_t
    {
        MAX_AXES = 32,
        MAX_BUTTONS = 128,
        MAX_HATS = 4
    };

    bool                             connected = false;
    std::int32_t                     instance_id = -1;
    std::size_t                      axis_count = 0;
    std::size_t                      button_count = 0;
    std::size_t                      hat_count = 0;
    std::array<std::int16_t, MAX_AXES> axes = {};
    std::array<bool, MAX_BUTTONS>      buttons = {};
    std::array<std::uint8_t, MAX_HATS> hats = {};
};

class Registry
{
public:
    enum : std::size_t
    {
        MAX_DEVICES = 10
    };

    AttachResult Attach(
        std::int32_t instance_id,
        std::size_t axis_count,
        std::size_t button_count,
        std::size_t hat_count,
        std::size_t& slot_index)
    {
        if (instance_id < 0 ||
            axis_count > Slot::MAX_AXES ||
            button_count > Slot::MAX_BUTTONS ||
            hat_count > Slot::MAX_HATS)
        {
            return AttachResult::INVALID_SHAPE;
        }

        if (Find(instance_id) != nullptr)
        {
            return AttachResult::DUPLICATE;
        }

        for (std::size_t i = 0; i < m_slots.size(); ++i)
        {
            if (!m_slots[i].connected)
            {
                m_slots[i] = Slot();
                m_slots[i].connected = true;
                m_slots[i].instance_id = instance_id;
                m_slots[i].axis_count = axis_count;
                m_slots[i].button_count = button_count;
                m_slots[i].hat_count = hat_count;
                slot_index = i;
                return AttachResult::ATTACHED;
            }
        }

        return AttachResult::FULL;
    }

    bool Detach(std::int32_t instance_id)
    {
        Slot* const slot = Find(instance_id);
        if (slot == nullptr)
        {
            return false;
        }

        *slot = Slot();
        return true;
    }

    ApplyResult Apply(const Event& event)
    {
        Slot* const slot = Find(event.instance_id);
        if (slot == nullptr)
        {
            return ApplyResult::UNKNOWN_DEVICE;
        }

        switch (event.type)
        {
        case EventType::AXIS:
            if (event.component >= slot->axis_count)
            {
                return ApplyResult::INVALID_COMPONENT;
            }
            if (event.value < -32768 || event.value > 32767)
            {
                return ApplyResult::INVALID_VALUE;
            }
            return SetValue(
                slot->axes[event.component],
                static_cast<std::int16_t>(event.value));

        case EventType::BUTTON:
            if (event.component >= slot->button_count)
            {
                return ApplyResult::INVALID_COMPONENT;
            }
            if (event.value != 0 && event.value != 1)
            {
                return ApplyResult::INVALID_VALUE;
            }
            return SetValue(slot->buttons[event.component], event.value == 1);

        case EventType::HAT:
            if (event.component >= slot->hat_count)
            {
                return ApplyResult::INVALID_COMPONENT;
            }
            if (event.value < 0 || (event.value & ~0x0f) != 0)
            {
                return ApplyResult::INVALID_VALUE;
            }
            return SetValue(
                slot->hats[event.component],
                static_cast<std::uint8_t>(event.value));
        }

        return ApplyResult::INVALID_VALUE;
    }

    void ResetStates()
    {
        for (Slot& slot : m_slots)
        {
            if (slot.connected)
            {
                slot.axes.fill(0);
                slot.buttons.fill(false);
                slot.hats.fill(0);
            }
        }
    }

    const Slot* Get(std::size_t slot_index) const
    {
        if (slot_index >= m_slots.size() || !m_slots[slot_index].connected)
        {
            return nullptr;
        }
        return &m_slots[slot_index];
    }

    const Slot* Find(std::int32_t instance_id) const
    {
        for (const Slot& slot : m_slots)
        {
            if (slot.connected && slot.instance_id == instance_id)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    static double NormalizeAxis(std::int16_t value)
    {
        if (value < 0)
        {
            return static_cast<double>(value) / 32768.0;
        }
        return static_cast<double>(value) / 32767.0;
    }

private:
    template <typename T>
    static ApplyResult SetValue(T& destination, const T& value)
    {
        if (destination == value)
        {
            return ApplyResult::NO_CHANGE;
        }
        destination = value;
        return ApplyResult::APPLIED;
    }

    Slot* Find(std::int32_t instance_id)
    {
        for (Slot& slot : m_slots)
        {
            if (slot.connected && slot.instance_id == instance_id)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    std::array<Slot, MAX_DEVICES> m_slots = {};
};

} // namespace MacOSControllerContract
} // namespace RoR
