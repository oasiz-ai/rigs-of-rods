/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "MacOSControllerContract.h"

#include <SDL.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using RoR::MacOSControllerContract::ApplyResult;
using RoR::MacOSControllerContract::AttachResult;
using RoR::MacOSControllerContract::Event;
using RoR::MacOSControllerContract::EventType;
using RoR::MacOSControllerContract::Registry;
using RoR::MacOSControllerContract::Slot;

int Fail(const std::string& message)
{
    std::cerr << "macOS controller contract test failed: " << message << '\n';
    return EXIT_FAILURE;
}

bool NearlyEqual(double lhs, double rhs)
{
    return std::fabs(lhs - rhs) < 1.0e-12;
}

ApplyResult ApplySDLEvent(Registry& registry, const SDL_Event& sdl_event)
{
    Event event;

    switch (sdl_event.type)
    {
    case SDL_JOYAXISMOTION:
        event.type = EventType::AXIS;
        event.instance_id = sdl_event.jaxis.which;
        event.component = sdl_event.jaxis.axis;
        event.value = sdl_event.jaxis.value;
        break;

    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        event.type = EventType::BUTTON;
        event.instance_id = sdl_event.jbutton.which;
        event.component = sdl_event.jbutton.button;
        event.value = sdl_event.jbutton.state;
        break;

    case SDL_JOYHATMOTION:
        event.type = EventType::HAT;
        event.instance_id = sdl_event.jhat.which;
        event.component = sdl_event.jhat.hat;
        event.value = sdl_event.jhat.value;
        break;

    default:
        return ApplyResult::NO_CHANGE;
    }

    return registry.Apply(event);
}

int VerifyPureContract()
{
    Registry registry;
    std::size_t first_slot = Registry::MAX_DEVICES;
    std::size_t second_slot = Registry::MAX_DEVICES;

    if (registry.Attach(100, 2, 3, 1, first_slot) !=
            AttachResult::ATTACHED ||
        first_slot != 0)
    {
        return Fail("the first controller did not receive stable slot zero");
    }
    if (registry.Attach(101, 1, 1, 0, second_slot) !=
            AttachResult::ATTACHED ||
        second_slot != 1)
    {
        return Fail("the second controller did not receive stable slot one");
    }
    if (registry.Attach(100, 2, 3, 1, second_slot) !=
        AttachResult::DUPLICATE)
    {
        return Fail("a duplicate SDL instance id was accepted");
    }
    if (registry.Attach(
            102,
            Slot::MAX_AXES + 1,
            0,
            0,
            second_slot) != AttachResult::INVALID_SHAPE)
    {
        return Fail("an over-cap controller shape was accepted");
    }
    if (!registry.Detach(100))
    {
        return Fail("a connected controller could not be detached");
    }
    if (registry.Get(first_slot) != nullptr)
    {
        return Fail("detaching a controller did not clear its slot");
    }
    if (registry.Attach(102, 2, 2, 1, first_slot) !=
            AttachResult::ATTACHED ||
        first_slot != 0)
    {
        return Fail("the first free slot was not reused deterministically");
    }

    Event event;
    event.instance_id = 102;
    event.type = EventType::AXIS;
    event.component = 0;
    event.value = -32768;
    if (registry.Apply(event) != ApplyResult::APPLIED)
    {
        return Fail("an axis transition was not applied");
    }
    if (registry.Apply(event) != ApplyResult::NO_CHANGE)
    {
        return Fail("a repeated axis value was reported as a transition");
    }
    const Slot* slot = registry.Get(first_slot);
    if (slot == nullptr || slot->axes[0] != -32768 ||
        !NearlyEqual(Registry::NormalizeAxis(slot->axes[0]), -1.0) ||
        !NearlyEqual(Registry::NormalizeAxis(0), 0.0) ||
        !NearlyEqual(Registry::NormalizeAxis(32767), 1.0))
    {
        return Fail("the signed axis range was not preserved and normalized");
    }

    event.value = 32768;
    if (registry.Apply(event) != ApplyResult::INVALID_VALUE)
    {
        return Fail("an out-of-range axis value was accepted");
    }
    event.component = 2;
    event.value = 1;
    if (registry.Apply(event) != ApplyResult::INVALID_COMPONENT)
    {
        return Fail("an out-of-range axis index was accepted");
    }

    event.type = EventType::BUTTON;
    event.component = 1;
    event.value = 1;
    if (registry.Apply(event) != ApplyResult::APPLIED ||
        registry.Get(first_slot) == nullptr ||
        !registry.Get(first_slot)->buttons[1])
    {
        return Fail("a button press was not recorded");
    }
    event.value = 0;
    if (registry.Apply(event) != ApplyResult::APPLIED ||
        registry.Get(first_slot)->buttons[1])
    {
        return Fail("a button release was not recorded");
    }
    event.value = 2;
    if (registry.Apply(event) != ApplyResult::INVALID_VALUE)
    {
        return Fail("a non-binary button value was accepted");
    }

    event.type = EventType::HAT;
    event.component = 0;
    event.value = SDL_HAT_RIGHTUP;
    if (registry.Apply(event) != ApplyResult::APPLIED ||
        registry.Get(first_slot)->hats[0] != SDL_HAT_RIGHTUP)
    {
        return Fail("a diagonal hat transition was not recorded");
    }
    event.value = 0x10;
    if (registry.Apply(event) != ApplyResult::INVALID_VALUE)
    {
        return Fail("a hat value outside SDL's low nibble was accepted");
    }

    event.instance_id = 999;
    event.value = SDL_HAT_CENTERED;
    if (registry.Apply(event) != ApplyResult::UNKNOWN_DEVICE)
    {
        return Fail("an event from an unknown instance id was accepted");
    }

    registry.ResetStates();
    slot = registry.Get(first_slot);
    if (slot == nullptr || !slot->connected ||
        slot->axis_count != 2 || slot->button_count != 2 ||
        slot->hat_count != 1 || slot->axes[0] != 0 ||
        slot->buttons[1] || slot->hats[0] != SDL_HAT_CENTERED)
    {
        return Fail("focus reset did not clear state while preserving shape");
    }

    Registry full_registry;
    for (std::size_t i = 0; i < Registry::MAX_DEVICES; ++i)
    {
        std::size_t slot_index = Registry::MAX_DEVICES;
        if (full_registry.Attach(
                static_cast<std::int32_t>(200 + i),
                0,
                0,
                0,
                slot_index) != AttachResult::ATTACHED ||
            slot_index != i)
        {
            return Fail("controller slots were not allocated in stable order");
        }
    }
    if (full_registry.Attach(999, 0, 0, 0, second_slot) !=
        AttachResult::FULL)
    {
        return Fail("a controller was accepted after the registry filled");
    }

    return EXIT_SUCCESS;
}

int VerifySDLVirtualController()
{
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0)
    {
        return Fail(std::string("SDL joystick initialization failed: ") +
            SDL_GetError());
    }

    SDL_VirtualJoystickDesc description = {};
    description.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    description.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    description.naxes = 2;
    description.nbuttons = 4;
    description.nhats = 1;
    description.name = "RoR macOS controller contract";

    const int device_index = SDL_JoystickAttachVirtualEx(&description);
    if (device_index < 0)
    {
        const std::string error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS);
        return Fail(std::string("SDL virtual joystick attach failed: ") + error);
    }

    SDL_Joystick* const joystick = SDL_JoystickOpen(device_index);
    if (joystick == nullptr)
    {
        const std::string error = SDL_GetError();
        SDL_JoystickDetachVirtual(device_index);
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS);
        return Fail(std::string("SDL virtual joystick open failed: ") + error);
    }

    int result = EXIT_SUCCESS;
    do
    {
        if (SDL_JoystickIsVirtual(device_index) != SDL_TRUE ||
            SDL_JoystickNumAxes(joystick) != description.naxes ||
            SDL_JoystickNumButtons(joystick) != description.nbuttons ||
            SDL_JoystickNumHats(joystick) != description.nhats)
        {
            result = Fail("SDL did not expose the declared virtual shape");
            break;
        }

        const SDL_JoystickID instance_id = SDL_JoystickInstanceID(joystick);
        if (instance_id < 0)
        {
            result = Fail("SDL did not assign a virtual instance id");
            break;
        }

        Registry registry;
        std::size_t slot_index = Registry::MAX_DEVICES;
        if (registry.Attach(
                instance_id,
                static_cast<std::size_t>(SDL_JoystickNumAxes(joystick)),
                static_cast<std::size_t>(SDL_JoystickNumButtons(joystick)),
                static_cast<std::size_t>(SDL_JoystickNumHats(joystick)),
                slot_index) != AttachResult::ATTACHED)
        {
            result = Fail("the SDL virtual device did not attach to the contract");
            break;
        }

        SDL_JoystickEventState(SDL_ENABLE);
        SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_JOYDEVICEREMOVED);

        if (SDL_JoystickSetVirtualAxis(joystick, 0, 16384) != 0 ||
            SDL_JoystickSetVirtualButton(joystick, 1, SDL_PRESSED) != 0 ||
            SDL_JoystickSetVirtualHat(
                joystick,
                0,
                SDL_HAT_LEFTDOWN) != 0)
        {
            result = Fail(std::string("SDL rejected a virtual transition: ") +
                SDL_GetError());
            break;
        }

        SDL_JoystickUpdate();
        SDL_Event sdl_event;
        int applied_events = 0;
        while (SDL_PollEvent(&sdl_event) != 0)
        {
            if (ApplySDLEvent(registry, sdl_event) == ApplyResult::APPLIED)
            {
                ++applied_events;
            }
        }

        const Slot* slot = registry.Get(slot_index);
        if (applied_events < 3 || slot == nullptr ||
            slot->axes[0] != 16384 || !slot->buttons[1] ||
            slot->hats[0] != SDL_HAT_LEFTDOWN)
        {
            result = Fail("SDL events did not reproduce the virtual device state");
            break;
        }

        if (SDL_JoystickSetVirtualAxis(joystick, 0, -32768) != 0 ||
            SDL_JoystickSetVirtualButton(joystick, 1, SDL_RELEASED) != 0 ||
            SDL_JoystickSetVirtualHat(
                joystick,
                0,
                SDL_HAT_CENTERED) != 0)
        {
            result = Fail(std::string("SDL rejected a reset transition: ") +
                SDL_GetError());
            break;
        }

        SDL_JoystickUpdate();
        while (SDL_PollEvent(&sdl_event) != 0)
        {
            ApplySDLEvent(registry, sdl_event);
        }

        slot = registry.Get(slot_index);
        if (slot == nullptr || slot->axes[0] != -32768 ||
            slot->buttons[1] || slot->hats[0] != SDL_HAT_CENTERED ||
            !NearlyEqual(Registry::NormalizeAxis(slot->axes[0]), -1.0))
        {
            result = Fail("SDL reset events did not reproduce the virtual state");
            break;
        }

        if (!registry.Detach(instance_id) ||
            registry.Get(slot_index) != nullptr)
        {
            result = Fail("the virtual disconnect did not clear its stable slot");
            break;
        }
    }
    while (false);

    SDL_JoystickClose(joystick);
    if (SDL_JoystickDetachVirtual(device_index) != 0 && result == EXIT_SUCCESS)
    {
        result = Fail(std::string("SDL virtual joystick detach failed: ") +
            SDL_GetError());
    }
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS);
    return result;
}

} // namespace

int main()
{
    const int pure_contract_result = VerifyPureContract();
    if (pure_contract_result != EXIT_SUCCESS)
    {
        return pure_contract_result;
    }

    const int sdl_result = VerifySDLVirtualController();
    if (sdl_result != EXIT_SUCCESS)
    {
        return sdl_result;
    }

    std::cout << "macOS SDL virtual-controller migration contract verified\n";
    return EXIT_SUCCESS;
}
