/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "MacOSControllerBackend.h"

#include <algorithm>

namespace RoR {

using MacOSControllerContract::ApplyResult;
using MacOSControllerContract::AttachResult;
using MacOSControllerContract::Event;
using MacOSControllerContract::EventType;
using MacOSControllerContract::Registry;
using MacOSControllerContract::Slot;

MacOSControllerBackend::~MacOSControllerBackend()
{
    this->Shutdown();
}

bool MacOSControllerBackend::Initialize()
{
    if (m_ready)
    {
        return true;
    }

    m_last_error.clear();
    m_owns_joystick_subsystem =
        SDL_WasInit(SDL_INIT_JOYSTICK) == 0;
    if (m_owns_joystick_subsystem &&
        SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0)
    {
        m_owns_joystick_subsystem = false;
        this->SetLastSDLError("SDL_InitSubSystem(SDL_INIT_JOYSTICK)");
        return false;
    }

    m_ready = true;
    if (SDL_JoystickEventState(SDL_ENABLE) != SDL_ENABLE)
    {
        this->SetLastSDLError("SDL_JoystickEventState(SDL_ENABLE)");
        this->Shutdown();
        return false;
    }

    const int device_count = SDL_NumJoysticks();
    if (device_count < 0)
    {
        this->SetLastSDLError("SDL_NumJoysticks");
        this->Shutdown();
        return false;
    }

    for (int device_index = 0; device_index < device_count; ++device_index)
    {
        if (m_registry.ActiveCount() >= Registry::MAX_DEVICES)
        {
            m_last_error = "macOS SDL controller slot limit reached";
            break;
        }
        std::size_t slot = Registry::MAX_DEVICES;
        if (!this->OpenDevice(device_index, slot))
        {
            this->Shutdown();
            return false;
        }
    }
    return true;
}

void MacOSControllerBackend::Shutdown()
{
    for (Device& device : m_devices)
    {
        if (device.handle != nullptr)
        {
            SDL_JoystickClose(device.handle);
        }
        device = Device();
    }

    m_registry = Registry();
    m_ready = false;

    if (m_owns_joystick_subsystem)
    {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        m_owns_joystick_subsystem = false;
    }
}

MacOSControllerBackend::Update MacOSControllerBackend::ProcessEvent(
    const SDL_Event& event)
{
    Update update;
    if (!m_ready)
    {
        return update;
    }

    if (event.type == SDL_JOYDEVICEADDED)
    {
        m_last_error.clear();
        std::size_t slot = Registry::MAX_DEVICES;
        if (this->OpenDevice(event.jdevice.which, slot))
        {
            update.kind = UpdateKind::DEVICE_ADDED;
            update.slot = slot;
        }
        else if (!m_last_error.empty())
        {
            update.kind = UpdateKind::DEVICE_ERROR;
        }
        return update;
    }

    if (event.type == SDL_JOYDEVICEREMOVED)
    {
        std::size_t slot = Registry::MAX_DEVICES;
        if (this->CloseDevice(event.jdevice.which, slot))
        {
            update.kind = UpdateKind::DEVICE_REMOVED;
            update.slot = slot;
        }
        return update;
    }

    Event contract_event;
    switch (event.type)
    {
    case SDL_JOYAXISMOTION:
        contract_event.type = EventType::AXIS;
        contract_event.instance_id = event.jaxis.which;
        contract_event.component = event.jaxis.axis;
        contract_event.value = event.jaxis.value;
        break;

    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        contract_event.type = EventType::BUTTON;
        contract_event.instance_id = event.jbutton.which;
        contract_event.component = event.jbutton.button;
        contract_event.value = event.jbutton.state;
        break;

    case SDL_JOYHATMOTION:
        contract_event.type = EventType::HAT;
        contract_event.instance_id = event.jhat.which;
        contract_event.component = event.jhat.hat;
        contract_event.value = event.jhat.value;
        break;

    default:
        return update;
    }

    std::size_t slot = Registry::MAX_DEVICES;
    if (!m_registry.FindSlot(contract_event.instance_id, slot))
    {
        return update;
    }

    if (m_registry.Apply(contract_event) == ApplyResult::APPLIED)
    {
        update.kind = UpdateKind::STATE_CHANGED;
        update.slot = slot;
        update.component_type = contract_event.type;
        update.component = contract_event.component;
    }
    return update;
}

void MacOSControllerBackend::ResetStates()
{
    m_registry.ResetStates();
}

void MacOSControllerBackend::RefreshStates()
{
    if (!m_ready)
    {
        return;
    }

    SDL_JoystickUpdate();
    m_registry.ResetStates();
    for (std::size_t slot = 0; slot < m_devices.size(); ++slot)
    {
        if (m_registry.Get(slot) != nullptr)
        {
            this->PopulateCurrentState(slot);
        }
    }
}

bool MacOSControllerBackend::IsConnected(std::size_t slot) const
{
    return m_registry.Get(slot) != nullptr;
}

std::size_t MacOSControllerBackend::ConnectedCount() const
{
    return m_registry.ActiveCount();
}

std::size_t MacOSControllerBackend::SlotLimit() const
{
    return m_registry.SlotLimit();
}

const Slot* MacOSControllerBackend::GetSlot(std::size_t slot) const
{
    return m_registry.Get(slot);
}

const std::string& MacOSControllerBackend::GetVendor(
    std::size_t slot) const
{
    static const std::string unknown("unknown");
    if (!this->IsConnected(slot))
    {
        return unknown;
    }
    return m_devices[slot].vendor;
}

bool MacOSControllerBackend::IsControllerEvent(Uint32 event_type)
{
    switch (event_type)
    {
    case SDL_JOYAXISMOTION:
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
    case SDL_JOYHATMOTION:
    case SDL_JOYDEVICEADDED:
    case SDL_JOYDEVICEREMOVED:
        return true;
    default:
        return false;
    }
}

bool MacOSControllerBackend::OpenDevice(
    int device_index,
    std::size_t& slot)
{
    if (!m_ready || device_index < 0)
    {
        return false;
    }

    const SDL_JoystickID advertised_instance_id =
        SDL_JoystickGetDeviceInstanceID(device_index);
    if (advertised_instance_id >= 0 &&
        m_registry.FindSlot(advertised_instance_id, slot))
    {
        return false;
    }

    SDL_Joystick* const joystick = SDL_JoystickOpen(device_index);
    if (joystick == nullptr)
    {
        this->SetLastSDLError("SDL_JoystickOpen");
        return false;
    }

    const SDL_JoystickID instance_id = SDL_JoystickInstanceID(joystick);
    if (instance_id < 0)
    {
        this->SetLastSDLError("SDL_JoystickInstanceID");
        SDL_JoystickClose(joystick);
        return false;
    }
    if (m_registry.FindSlot(instance_id, slot))
    {
        SDL_JoystickClose(joystick);
        return false;
    }

    const int reported_axis_count = SDL_JoystickNumAxes(joystick);
    if (reported_axis_count < 0)
    {
        this->SetLastSDLError("SDL_JoystickNumAxes");
        SDL_JoystickClose(joystick);
        return false;
    }
    const int reported_button_count = SDL_JoystickNumButtons(joystick);
    if (reported_button_count < 0)
    {
        this->SetLastSDLError("SDL_JoystickNumButtons");
        SDL_JoystickClose(joystick);
        return false;
    }
    const int reported_hat_count = SDL_JoystickNumHats(joystick);
    if (reported_hat_count < 0)
    {
        this->SetLastSDLError("SDL_JoystickNumHats");
        SDL_JoystickClose(joystick);
        return false;
    }

    const std::size_t axis_count = static_cast<std::size_t>(std::min(
        reported_axis_count,
        static_cast<int>(Slot::MAX_AXES)));
    const std::size_t button_count = static_cast<std::size_t>(std::min(
        reported_button_count,
        static_cast<int>(Slot::MAX_BUTTONS)));
    const std::size_t hat_count = static_cast<std::size_t>(std::min(
        reported_hat_count,
        static_cast<int>(Slot::MAX_HATS)));

    const AttachResult attach_result = m_registry.Attach(
        instance_id,
        axis_count,
        button_count,
        hat_count,
        slot);
    if (attach_result != AttachResult::ATTACHED)
    {
        if (attach_result == AttachResult::FULL)
        {
            m_last_error = "macOS SDL controller slot limit reached";
        }
        SDL_JoystickClose(joystick);
        return false;
    }

    Device& device = m_devices[slot];
    device.handle = joystick;
    const char* const name = SDL_JoystickName(joystick);
    device.vendor = name != nullptr ? name : "unknown";
    this->PopulateCurrentState(slot);
    return true;
}

bool MacOSControllerBackend::CloseDevice(
    std::int32_t instance_id,
    std::size_t& slot)
{
    if (!m_registry.FindSlot(instance_id, slot))
    {
        return false;
    }

    Device& device = m_devices[slot];
    if (device.handle != nullptr)
    {
        SDL_JoystickClose(device.handle);
    }
    device = Device();
    return m_registry.Detach(instance_id);
}

void MacOSControllerBackend::PopulateCurrentState(std::size_t slot)
{
    const Slot* const state = m_registry.Get(slot);
    SDL_Joystick* const joystick = m_devices[slot].handle;
    if (state == nullptr || joystick == nullptr)
    {
        return;
    }

    Event event;
    event.instance_id = state->instance_id;
    event.type = EventType::AXIS;
    for (std::size_t i = 0; i < state->axis_count; ++i)
    {
        event.component = i;
        event.value = SDL_JoystickGetAxis(
            joystick,
            static_cast<int>(i));
        m_registry.Apply(event);
    }

    event.type = EventType::BUTTON;
    for (std::size_t i = 0; i < state->button_count; ++i)
    {
        event.component = i;
        event.value = SDL_JoystickGetButton(
            joystick,
            static_cast<int>(i));
        m_registry.Apply(event);
    }

    event.type = EventType::HAT;
    for (std::size_t i = 0; i < state->hat_count; ++i)
    {
        event.component = i;
        event.value = SDL_JoystickGetHat(
            joystick,
            static_cast<int>(i));
        m_registry.Apply(event);
    }
}

void MacOSControllerBackend::SetLastSDLError(const char* operation)
{
    m_last_error = operation;
    const char* const sdl_error = SDL_GetError();
    if (sdl_error != nullptr && sdl_error[0] != '\0')
    {
        m_last_error += ": ";
        m_last_error += sdl_error;
    }
}

} // namespace RoR
