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

static_assert(
    SDL_CONTROLLER_AXIS_MAX <= static_cast<int>(Slot::MAX_AXES),
    "SDL GameController axes exceed the bounded controller contract");
static_assert(
    SDL_CONTROLLER_BUTTON_MAX <= static_cast<int>(Slot::MAX_BUTTONS),
    "SDL GameController buttons exceed the bounded controller contract");

MacOSControllerBackend::~MacOSControllerBackend()
{
    this->Shutdown();
}

bool MacOSControllerBackend::Initialize(
    const std::string& gamecontroller_mapping_file)
{
    if (m_ready)
    {
        return true;
    }

    m_last_error.clear();
    // RoR's versioned mapping profile is position based. In particular, a
    // Nintendo controller's south/east/west/north buttons must retain the
    // same indices as Xbox and PlayStation controllers.
    SDL_SetHintWithPriority(
        SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS,
        "0",
        SDL_HINT_OVERRIDE);
    if (!gamecontroller_mapping_file.empty())
    {
        // This is an optional user-supplied extension to SDL's pinned built-in
        // controller database. SDL requires the hint before subsystem init.
        SDL_SetHintWithPriority(
            SDL_HINT_GAMECONTROLLERCONFIG_FILE,
            gamecontroller_mapping_file.c_str(),
            SDL_HINT_OVERRIDE);
    }

    m_owns_gamecontroller_subsystem =
        SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0;
    if (m_owns_gamecontroller_subsystem &&
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
    {
        m_owns_gamecontroller_subsystem = false;
        this->SetLastSDLError(
            "SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)");
        return false;
    }

    m_ready = true;
    if (SDL_GameControllerEventState(SDL_ENABLE) != SDL_ENABLE)
    {
        this->SetLastSDLError(
            "SDL_GameControllerEventState(SDL_ENABLE)");
        this->Shutdown();
        return false;
    }
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
        this->CloseHandle(device);
    }

    m_registry = Registry();
    m_ready = false;

    if (m_owns_gamecontroller_subsystem)
    {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        m_owns_gamecontroller_subsystem = false;
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

    if (event.type == SDL_CONTROLLERDEVICEREMAPPED)
    {
        std::size_t slot = Registry::MAX_DEVICES;
        if (m_registry.FindSlot(event.cdevice.which, slot) &&
            m_devices[slot].standardized)
        {
            this->RefreshStates();
            update.kind = UpdateKind::DEVICE_REMAPPED;
            update.slot = slot;
        }
        return update;
    }

    Event contract_event;
    std::int32_t event_instance_id = -1;
    bool standardized_event = false;
    switch (event.type)
    {
    case SDL_JOYAXISMOTION:
        contract_event.type = EventType::AXIS;
        event_instance_id = event.jaxis.which;
        contract_event.component = event.jaxis.axis;
        contract_event.value = event.jaxis.value;
        break;

    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        contract_event.type = EventType::BUTTON;
        event_instance_id = event.jbutton.which;
        contract_event.component = event.jbutton.button;
        contract_event.value = event.jbutton.state;
        break;

    case SDL_JOYHATMOTION:
        contract_event.type = EventType::HAT;
        event_instance_id = event.jhat.which;
        contract_event.component = event.jhat.hat;
        contract_event.value = event.jhat.value;
        break;

    case SDL_CONTROLLERAXISMOTION:
        standardized_event = true;
        contract_event.type = EventType::AXIS;
        event_instance_id = event.caxis.which;
        contract_event.component = event.caxis.axis;
        contract_event.value = event.caxis.value;
        break;

    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        standardized_event = true;
        contract_event.type = EventType::BUTTON;
        event_instance_id = event.cbutton.which;
        contract_event.component = event.cbutton.button;
        contract_event.value = event.cbutton.state;
        break;

    default:
        return update;
    }

    std::size_t slot = Registry::MAX_DEVICES;
    if (!m_registry.FindSlot(event_instance_id, slot))
    {
        return update;
    }

    // SDL produces both raw joystick and standardized controller events for
    // mapped gamepads. Apply exactly one event family per open handle or raw
    // device order can overwrite the stable semantic profile.
    if (m_devices[slot].standardized != standardized_event)
    {
        return update;
    }

    contract_event.instance_id = event_instance_id;
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

    SDL_GameControllerUpdate();
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

const std::string& MacOSControllerBackend::GetMappingProfile(
    std::size_t slot) const
{
    static const std::string unknown("unknown");
    if (!this->IsConnected(slot))
    {
        return unknown;
    }
    return m_devices[slot].mapping_profile;
}

bool MacOSControllerBackend::IsStandardGameController(
    std::size_t slot) const
{
    return this->IsConnected(slot) && m_devices[slot].standardized;
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
    case SDL_CONTROLLERAXISMOTION:
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
    case SDL_CONTROLLERDEVICEREMAPPED:
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

    const SDL_JoystickType joystick_type =
        SDL_JoystickGetDeviceType(device_index);
    const bool specialized_device =
        joystick_type == SDL_JOYSTICK_TYPE_WHEEL ||
        joystick_type == SDL_JOYSTICK_TYPE_FLIGHT_STICK ||
        joystick_type == SDL_JOYSTICK_TYPE_THROTTLE;
    const bool standardized =
        !specialized_device &&
        SDL_IsGameController(device_index) == SDL_TRUE;

    SDL_GameController* game_controller = nullptr;
    SDL_Joystick* joystick = nullptr;
    if (standardized)
    {
        game_controller = SDL_GameControllerOpen(device_index);
        if (game_controller == nullptr)
        {
            this->SetLastSDLError("SDL_GameControllerOpen");
            return false;
        }
        joystick = SDL_GameControllerGetJoystick(game_controller);
        if (joystick == nullptr)
        {
            this->SetLastSDLError("SDL_GameControllerGetJoystick");
            SDL_GameControllerClose(game_controller);
            return false;
        }
    }
    else
    {
        joystick = SDL_JoystickOpen(device_index);
    }

    if (joystick == nullptr)
    {
        this->SetLastSDLError("SDL_JoystickOpen");
        return false;
    }

    const SDL_JoystickID instance_id = SDL_JoystickInstanceID(joystick);
    if (instance_id < 0)
    {
        this->SetLastSDLError("SDL_JoystickInstanceID");
        if (game_controller != nullptr)
        {
            SDL_GameControllerClose(game_controller);
        }
        else
        {
            SDL_JoystickClose(joystick);
        }
        return false;
    }
    if (m_registry.FindSlot(instance_id, slot))
    {
        if (game_controller != nullptr)
        {
            SDL_GameControllerClose(game_controller);
        }
        else
        {
            SDL_JoystickClose(joystick);
        }
        return false;
    }

    int reported_axis_count = SDL_CONTROLLER_AXIS_MAX;
    int reported_button_count = SDL_CONTROLLER_BUTTON_MAX;
    int reported_hat_count = 0;
    if (!standardized)
    {
        reported_axis_count = SDL_JoystickNumAxes(joystick);
        if (reported_axis_count < 0)
        {
            this->SetLastSDLError("SDL_JoystickNumAxes");
        }
        reported_button_count = SDL_JoystickNumButtons(joystick);
        if (reported_button_count < 0 && m_last_error.empty())
        {
            this->SetLastSDLError("SDL_JoystickNumButtons");
        }
        reported_hat_count = SDL_JoystickNumHats(joystick);
        if (reported_hat_count < 0 && m_last_error.empty())
        {
            this->SetLastSDLError("SDL_JoystickNumHats");
        }
        if (reported_axis_count < 0 ||
            reported_button_count < 0 ||
            reported_hat_count < 0)
        {
            SDL_JoystickClose(joystick);
            return false;
        }
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
        if (game_controller != nullptr)
        {
            SDL_GameControllerClose(game_controller);
        }
        else
        {
            SDL_JoystickClose(joystick);
        }
        return false;
    }

    Device& device = m_devices[slot];
    device.joystick = joystick;
    device.game_controller = game_controller;
    device.standardized = standardized;
    const char* const name = standardized ?
        SDL_GameControllerName(game_controller) :
        SDL_JoystickName(joystick);
    device.vendor = name != nullptr ? name : "unknown";
    device.mapping_profile = standardized ?
        STANDARD_MAPPING_PROFILE :
        device.vendor;
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
    this->CloseHandle(device);
    return m_registry.Detach(instance_id);
}

void MacOSControllerBackend::CloseHandle(Device& device)
{
    if (device.game_controller != nullptr)
    {
        SDL_GameControllerClose(device.game_controller);
    }
    else if (device.joystick != nullptr)
    {
        SDL_JoystickClose(device.joystick);
    }
    device = Device();
}

void MacOSControllerBackend::PopulateCurrentState(std::size_t slot)
{
    const Slot* const state = m_registry.Get(slot);
    const Device& device = m_devices[slot];
    SDL_Joystick* const joystick = device.joystick;
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
        event.value = device.standardized ?
            SDL_GameControllerGetAxis(
                device.game_controller,
                static_cast<SDL_GameControllerAxis>(i)) :
            SDL_JoystickGetAxis(
                joystick,
                static_cast<int>(i));
        m_registry.Apply(event);
    }

    event.type = EventType::BUTTON;
    for (std::size_t i = 0; i < state->button_count; ++i)
    {
        event.component = i;
        event.value = device.standardized ?
            SDL_GameControllerGetButton(
                device.game_controller,
                static_cast<SDL_GameControllerButton>(i)) :
            SDL_JoystickGetButton(
                joystick,
                static_cast<int>(i));
        m_registry.Apply(event);
    }

    if (device.standardized)
    {
        return;
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
