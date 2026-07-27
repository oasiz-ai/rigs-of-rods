/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "MacOSControllerBackend.h"
#include "MacOSControllerContract.h"

#include <SDL.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
    std::size_t found_slot = Registry::MAX_DEVICES;
    if (!registry.FindSlot(101, found_slot) ||
        found_slot != second_slot ||
        registry.ActiveCount() != 2 ||
        registry.SlotLimit() != 2)
    {
        return Fail("registry occupancy metadata did not match attached slots");
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
    if (registry.ActiveCount() != 1 || registry.SlotLimit() != 2)
    {
        return Fail("a sparse stable slot was hidden by the occupancy limit");
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

using RoR::MacOSControllerBackend;

struct BackendEvents
{
    int added = 0;
    int removed = 0;
    int remapped = 0;
    int errors = 0;
    int changed = 0;
};

BackendEvents DrainControllerEvents(MacOSControllerBackend& backend)
{
    BackendEvents summary;
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0)
    {
        if (!MacOSControllerBackend::IsControllerEvent(event.type))
        {
            continue;
        }

        const MacOSControllerBackend::Update update =
            backend.ProcessEvent(event);
        switch (update.kind)
        {
        case MacOSControllerBackend::UpdateKind::DEVICE_ADDED:
            ++summary.added;
            break;
        case MacOSControllerBackend::UpdateKind::DEVICE_REMOVED:
            ++summary.removed;
            break;
        case MacOSControllerBackend::UpdateKind::DEVICE_REMAPPED:
            ++summary.remapped;
            break;
        case MacOSControllerBackend::UpdateKind::DEVICE_ERROR:
            ++summary.errors;
            break;
        case MacOSControllerBackend::UpdateKind::STATE_CHANGED:
            ++summary.changed;
            break;
        case MacOSControllerBackend::UpdateKind::IGNORED:
            break;
        }
    }
    return summary;
}

bool FindBackendSlot(
    const MacOSControllerBackend& backend,
    SDL_JoystickID instance_id,
    std::size_t& slot_index)
{
    for (std::size_t i = 0; i < Registry::MAX_DEVICES; ++i)
    {
        const Slot* const slot = backend.GetSlot(i);
        if (slot != nullptr && slot->instance_id == instance_id)
        {
            slot_index = i;
            return true;
        }
    }
    return false;
}

bool AttachVirtualController(
    const char* name,
    SDL_JoystickID& instance_id)
{
    SDL_VirtualJoystickDesc description = {};
    description.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    // This exercises the raw joystick/wheel compatibility path. A separate
    // test below covers SDL's standardized GameController semantics.
    description.type = SDL_JOYSTICK_TYPE_UNKNOWN;
    description.naxes = 2;
    description.nbuttons = 4;
    description.nhats = 1;
    description.name = name;

    const int device_index = SDL_JoystickAttachVirtualEx(&description);
    if (device_index < 0)
    {
        return false;
    }
    instance_id = SDL_JoystickGetDeviceInstanceID(device_index);
    return instance_id >= 0;
}

bool AttachVirtualGameController(
    const char* name,
    SDL_JoystickID& instance_id)
{
    SDL_VirtualJoystickDesc description = {};
    description.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    description.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    description.naxes = SDL_CONTROLLER_AXIS_MAX;
    description.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    description.nhats = 0;
    description.axis_mask =
        (static_cast<Uint32>(1) << SDL_CONTROLLER_AXIS_MAX) - 1u;
    description.button_mask =
        (static_cast<Uint32>(1) << SDL_CONTROLLER_BUTTON_MAX) - 1u;
    description.vendor_id = 0x1209;
    description.product_id = 0x524f;
    description.name = name;

    const int device_index = SDL_JoystickAttachVirtualEx(&description);
    if (device_index < 0 ||
        SDL_IsGameController(device_index) != SDL_TRUE)
    {
        if (device_index >= 0)
        {
            SDL_JoystickDetachVirtual(device_index);
        }
        return false;
    }
    instance_id = SDL_JoystickGetDeviceInstanceID(device_index);
    return instance_id >= 0;
}

bool DetachVirtualController(SDL_JoystickID instance_id)
{
    const int device_count = SDL_NumJoysticks();
    for (int device_index = 0;
         device_index < device_count;
         ++device_index)
    {
        if (SDL_JoystickGetDeviceInstanceID(device_index) == instance_id)
        {
            return SDL_JoystickDetachVirtual(device_index) == 0;
        }
    }
    return true;
}

int VerifySDLVirtualController()
{
    const Uint32 test_subsystems = SDL_INIT_JOYSTICK | SDL_INIT_EVENTS;
    if (SDL_InitSubSystem(test_subsystems) != 0)
    {
        return Fail(std::string("SDL joystick initialization failed: ") +
            SDL_GetError());
    }

    std::vector<SDL_JoystickID> virtual_devices;
    SDL_JoystickID first_id = -1;
    if (!AttachVirtualController(
            "RoR macOS controller startup",
            first_id))
    {
        const std::string error = SDL_GetError();
        SDL_QuitSubSystem(test_subsystems);
        return Fail(std::string("SDL virtual joystick attach failed: ") +
            error);
    }
    virtual_devices.push_back(first_id);

    int result = EXIT_SUCCESS;
    MacOSControllerBackend backend;
    do
    {
        if (!backend.Initialize())
        {
            result = Fail(std::string(
                "the production SDL backend did not initialize: ") +
                backend.GetLastError());
            break;
        }

        std::size_t first_slot = Registry::MAX_DEVICES;
        if (!FindBackendSlot(backend, first_id, first_slot))
        {
            result = Fail(
                "startup enumeration missed the virtual controller");
            break;
        }

        const Slot* slot = backend.GetSlot(first_slot);
        if (slot == nullptr ||
            slot->axis_count != 2 ||
            slot->button_count != 4 ||
            slot->hat_count != 1 ||
            backend.IsStandardGameController(first_slot) ||
            backend.GetMappingProfile(first_slot) !=
                "RoR macOS controller startup" ||
            backend.GetVendor(first_slot) !=
                "RoR macOS controller startup")
        {
            result = Fail(
                "the backend did not preserve the SDL controller shape");
            break;
        }

        // Discard the duplicate add event queued before startup enumeration.
        DrainControllerEvents(backend);
        SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_JOYDEVICEREMOVED);

        SDL_Joystick* const first_joystick =
            SDL_JoystickFromInstanceID(first_id);
        if (first_joystick == nullptr ||
            SDL_JoystickSetVirtualAxis(first_joystick, 0, -32768) != 0 ||
            SDL_JoystickSetVirtualButton(
                first_joystick,
                1,
                SDL_PRESSED) != 0 ||
            SDL_JoystickSetVirtualHat(
                first_joystick,
                0,
                SDL_HAT_LEFTDOWN) != 0)
        {
            result = Fail(std::string(
                "SDL rejected a virtual transition: ") +
                SDL_GetError());
            break;
        }

        SDL_JoystickUpdate();
        const BackendEvents transitions = DrainControllerEvents(backend);
        slot = backend.GetSlot(first_slot);
        if (transitions.changed < 3 ||
            slot == nullptr ||
            slot->axes[0] != -32768 ||
            !slot->buttons[1] ||
            slot->hats[0] != SDL_HAT_LEFTDOWN ||
            !NearlyEqual(Registry::NormalizeAxis(slot->axes[0]), -1.0))
        {
            result = Fail(
                "production event handling lost virtual controller state");
            break;
        }

        backend.ResetStates();
        slot = backend.GetSlot(first_slot);
        if (slot == nullptr ||
            slot->axis_count != 2 ||
            slot->button_count != 4 ||
            slot->hat_count != 1 ||
            slot->axes[0] != 0 ||
            slot->buttons[1] ||
            slot->hats[0] != SDL_HAT_CENTERED)
        {
            result = Fail(
                "focus loss did not neutralize state and preserve shape");
            break;
        }

        backend.RefreshStates();
        slot = backend.GetSlot(first_slot);
        if (slot == nullptr ||
            slot->axes[0] != -32768 ||
            !slot->buttons[1] ||
            slot->hats[0] != SDL_HAT_LEFTDOWN)
        {
            result = Fail(
                "focus regain did not recover held physical state");
            break;
        }
        DrainControllerEvents(backend);

        SDL_JoystickID second_id = -1;
        const std::size_t before_hotplug = backend.ConnectedCount();
        if (!AttachVirtualController(
                "RoR macOS controller hotplug",
                second_id))
        {
            result = Fail(std::string(
                "SDL hotplug attach failed: ") + SDL_GetError());
            break;
        }
        virtual_devices.push_back(second_id);
        const BackendEvents hotplug_events =
            DrainControllerEvents(backend);
        std::size_t second_slot = Registry::MAX_DEVICES;
        if (hotplug_events.added != 1 ||
            backend.ConnectedCount() != before_hotplug + 1 ||
            !FindBackendSlot(backend, second_id, second_slot) ||
            second_slot == first_slot)
        {
            result = Fail(
                "the production backend did not hotplug a new controller");
            break;
        }

        if (!DetachVirtualController(first_id))
        {
            result = Fail(std::string(
                "SDL hot-unplug failed: ") + SDL_GetError());
            break;
        }
        const BackendEvents removal_events =
            DrainControllerEvents(backend);
        std::size_t detached_slot = Registry::MAX_DEVICES;
        if (removal_events.removed != 1 ||
            FindBackendSlot(backend, first_id, detached_slot) ||
            backend.GetSlot(first_slot) != nullptr ||
            !backend.IsConnected(second_slot) ||
            backend.SlotLimit() != second_slot + 1)
        {
            result = Fail(
                "hot-unplug did not preserve sparse stable slot occupancy");
            break;
        }

        SDL_JoystickID replacement_id = -1;
        if (!AttachVirtualController(
                "RoR macOS controller replacement",
                replacement_id))
        {
            result = Fail(std::string(
                "SDL replacement attach failed: ") + SDL_GetError());
            break;
        }
        virtual_devices.push_back(replacement_id);
        const BackendEvents replacement_events =
            DrainControllerEvents(backend);
        std::size_t replacement_slot = Registry::MAX_DEVICES;
        if (replacement_events.added != 1 ||
            !FindBackendSlot(
                backend,
                replacement_id,
                replacement_slot) ||
            replacement_slot != first_slot ||
            !backend.IsConnected(second_slot))
        {
            result = Fail(
                "hotplug did not deterministically reuse the first free slot");
            break;
        }

        int filler_number = 0;
        while (backend.ConnectedCount() < Registry::MAX_DEVICES)
        {
            const std::string name =
                "RoR macOS controller filler " +
                std::to_string(filler_number++);
            SDL_JoystickID filler_id = -1;
            if (!AttachVirtualController(name.c_str(), filler_id))
            {
                result = Fail(std::string(
                    "SDL filler attach failed: ") + SDL_GetError());
                break;
            }
            virtual_devices.push_back(filler_id);
            const BackendEvents filler_events =
                DrainControllerEvents(backend);
            std::size_t filler_slot = Registry::MAX_DEVICES;
            if (filler_events.added != 1 ||
                !FindBackendSlot(backend, filler_id, filler_slot))
            {
                result = Fail(
                    "the backend lost a controller below its slot bound");
                break;
            }
        }
        if (result != EXIT_SUCCESS)
        {
            break;
        }

        SDL_JoystickID overflow_id = -1;
        if (!AttachVirtualController(
                "RoR macOS controller overflow",
                overflow_id))
        {
            result = Fail(std::string(
                "SDL overflow attach failed: ") + SDL_GetError());
            break;
        }
        virtual_devices.push_back(overflow_id);
        const BackendEvents overflow_events =
            DrainControllerEvents(backend);
        std::size_t overflow_slot = Registry::MAX_DEVICES;
        if (overflow_events.errors != 1 ||
            backend.ConnectedCount() != Registry::MAX_DEVICES ||
            backend.SlotLimit() != Registry::MAX_DEVICES ||
            FindBackendSlot(backend, overflow_id, overflow_slot) ||
            backend.GetLastError() !=
                "macOS SDL controller slot limit reached")
        {
            result = Fail(
                "the production backend exceeded its bounded slot registry");
            break;
        }
    }
    while (false);

    backend.Shutdown();
    if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0 &&
        result == EXIT_SUCCESS)
    {
        result = Fail(
            "backend shutdown quit an SDL subsystem owned by its caller");
    }

    for (const SDL_JoystickID instance_id : virtual_devices)
    {
        if (!DetachVirtualController(instance_id) &&
            result == EXIT_SUCCESS)
        {
            result = Fail(std::string(
                "SDL virtual joystick cleanup failed: ") +
                SDL_GetError());
        }
    }
    SDL_QuitSubSystem(test_subsystems);

    if (result == EXIT_SUCCESS)
    {
        if (SDL_WasInit(SDL_INIT_JOYSTICK) != 0)
        {
            result = Fail(
                "test teardown did not release its SDL joystick subsystem");
        }
        else
        {
            MacOSControllerBackend owning_backend;
            if (!owning_backend.Initialize() ||
                SDL_WasInit(SDL_INIT_JOYSTICK) == 0)
            {
                result = Fail(
                    "backend did not initialize its owned SDL subsystem");
            }
            owning_backend.Shutdown();
            if (SDL_WasInit(SDL_INIT_JOYSTICK) != 0 &&
                result == EXIT_SUCCESS)
            {
                result = Fail(
                    "backend did not release its owned SDL subsystem");
            }
        }
    }
    return result;
}

int VerifySDLVirtualGameController()
{
    const Uint32 test_subsystems =
        SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS;
    if (SDL_InitSubSystem(test_subsystems) != 0)
    {
        return Fail(
            std::string("SDL GameController initialization failed: ") +
            SDL_GetError());
    }

    SDL_JoystickID instance_id = -1;
    if (!AttachVirtualGameController(
            "RoR macOS standardized gamepad",
            instance_id))
    {
        const std::string error = SDL_GetError();
        SDL_QuitSubSystem(test_subsystems);
        return Fail(
            std::string("SDL virtual GameController attach failed: ") +
            error);
    }

    int result = EXIT_SUCCESS;
    MacOSControllerBackend backend;
    do
    {
        if (!backend.Initialize())
        {
            result = Fail(
                std::string(
                    "the standardized backend did not initialize: ") +
                backend.GetLastError());
            break;
        }

        std::size_t slot_index = Registry::MAX_DEVICES;
        if (!FindBackendSlot(backend, instance_id, slot_index) ||
            !backend.IsStandardGameController(slot_index))
        {
            result = Fail(
                "the virtual GameController used the raw joystick path");
            break;
        }

        const Slot* slot = backend.GetSlot(slot_index);
        if (slot == nullptr ||
            slot->axis_count != SDL_CONTROLLER_AXIS_MAX ||
            slot->button_count != SDL_CONTROLLER_BUTTON_MAX ||
            slot->hat_count != 0 ||
            backend.GetMappingProfile(slot_index) !=
                MacOSControllerBackend::STANDARD_MAPPING_PROFILE ||
            backend.GetVendor(slot_index) !=
                "RoR macOS standardized gamepad")
        {
            result = Fail(
                "the standardized controller ABI or mapping profile changed");
            break;
        }

        // Discard add/state events generated during attach and startup.
        DrainControllerEvents(backend);
        SDL_FlushEvents(
            SDL_JOYAXISMOTION,
            SDL_CONTROLLERDEVICEREMAPPED);

        SDL_Joystick* const joystick =
            SDL_JoystickFromInstanceID(instance_id);
        if (joystick == nullptr ||
            SDL_JoystickSetVirtualAxis(
                joystick,
                SDL_CONTROLLER_AXIS_LEFTX,
                SDL_JOYSTICK_AXIS_MIN) != 0 ||
            SDL_JoystickSetVirtualAxis(
                joystick,
                SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                SDL_JOYSTICK_AXIS_MAX) != 0 ||
            SDL_JoystickSetVirtualButton(
                joystick,
                SDL_CONTROLLER_BUTTON_DPAD_UP,
                SDL_PRESSED) != 0)
        {
            result = Fail(
                std::string(
                    "SDL rejected a standardized virtual transition: ") +
                SDL_GetError());
            break;
        }

        SDL_GameControllerUpdate();
        SDL_JoystickUpdate();
        const BackendEvents transitions = DrainControllerEvents(backend);
        slot = backend.GetSlot(slot_index);
        if (transitions.changed < 3 ||
            slot == nullptr ||
            slot->axes[SDL_CONTROLLER_AXIS_LEFTX] !=
                SDL_JOYSTICK_AXIS_MIN ||
            slot->axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] !=
                SDL_JOYSTICK_AXIS_MAX ||
            !slot->buttons[SDL_CONTROLLER_BUTTON_DPAD_UP])
        {
            result = Fail(
                "standard semantic axes/buttons did not reach the contract");
            break;
        }

        // Mapped GameControllers also emit raw JOY events. Those raw indices
        // must never overwrite the stable semantic controller state.
        SDL_Event duplicate_raw = {};
        duplicate_raw.type = SDL_JOYAXISMOTION;
        duplicate_raw.jaxis.which = instance_id;
        duplicate_raw.jaxis.axis = SDL_CONTROLLER_AXIS_LEFTX;
        duplicate_raw.jaxis.value = 12345;
        if (backend.ProcessEvent(duplicate_raw).kind !=
                MacOSControllerBackend::UpdateKind::IGNORED ||
            backend.GetSlot(slot_index) == nullptr ||
            backend.GetSlot(slot_index)->
                axes[SDL_CONTROLLER_AXIS_LEFTX] !=
                    SDL_JOYSTICK_AXIS_MIN)
        {
            result = Fail(
                "a duplicate raw joystick event corrupted GameController state");
            break;
        }

        SDL_Event remapped = {};
        remapped.type = SDL_CONTROLLERDEVICEREMAPPED;
        remapped.cdevice.which = instance_id;
        if (backend.ProcessEvent(remapped).kind !=
                MacOSControllerBackend::UpdateKind::DEVICE_REMAPPED ||
            backend.GetSlot(slot_index) == nullptr ||
            !backend.GetSlot(slot_index)->
                buttons[SDL_CONTROLLER_BUTTON_DPAD_UP])
        {
            result = Fail(
                "a controller remap did not refresh semantic state");
            break;
        }

        backend.ResetStates();
        slot = backend.GetSlot(slot_index);
        if (slot == nullptr ||
            slot->axes[SDL_CONTROLLER_AXIS_LEFTX] != 0 ||
            slot->axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] != 0 ||
            slot->buttons[SDL_CONTROLLER_BUTTON_DPAD_UP])
        {
            result = Fail(
                "focus loss did not neutralize standardized state");
            break;
        }

        backend.RefreshStates();
        slot = backend.GetSlot(slot_index);
        if (slot == nullptr ||
            slot->axes[SDL_CONTROLLER_AXIS_LEFTX] !=
                SDL_JOYSTICK_AXIS_MIN ||
            slot->axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] !=
                SDL_JOYSTICK_AXIS_MAX ||
            !slot->buttons[SDL_CONTROLLER_BUTTON_DPAD_UP])
        {
            result = Fail(
                "focus regain did not restore standardized held state");
            break;
        }

        if (SDL_JoystickSetVirtualAxis(
                joystick,
                SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                SDL_JOYSTICK_AXIS_MIN) != 0 ||
            SDL_JoystickSetVirtualButton(
                joystick,
                SDL_CONTROLLER_BUTTON_DPAD_UP,
                SDL_RELEASED) != 0)
        {
            result = Fail(
                "SDL rejected a standardized release transition");
            break;
        }
        SDL_GameControllerUpdate();
        SDL_JoystickUpdate();
        DrainControllerEvents(backend);
        slot = backend.GetSlot(slot_index);
        if (slot == nullptr ||
            slot->axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] != 0 ||
            slot->buttons[SDL_CONTROLLER_BUTTON_DPAD_UP])
        {
            result = Fail(
                "standard trigger/button release semantics were not preserved");
            break;
        }
    }
    while (false);

    backend.Shutdown();
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0 &&
        result == EXIT_SUCCESS)
    {
        result = Fail(
            "backend shutdown quit a GameController subsystem owned by its caller");
    }
    if (!DetachVirtualController(instance_id) &&
        result == EXIT_SUCCESS)
    {
        result = Fail(
            std::string("SDL virtual GameController cleanup failed: ") +
            SDL_GetError());
    }
    SDL_QuitSubSystem(test_subsystems);
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0 &&
        result == EXIT_SUCCESS)
    {
        result = Fail(
            "test teardown did not release the GameController subsystem");
    }
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

    const int gamecontroller_result = VerifySDLVirtualGameController();
    if (gamecontroller_result != EXIT_SUCCESS)
    {
        return gamecontroller_result;
    }

    std::cout
        << "macOS SDL raw and standardized controller contracts verified\n";
    return EXIT_SUCCESS;
}
