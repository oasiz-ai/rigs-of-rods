/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "MacOSInputBridge.h"

#include <cstdlib>
#include <iostream>

namespace {

int Fail(const char* message)
{
    std::cerr << "macOS input bridge test failed: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    const struct
    {
        SDL_Scancode source;
        OIS::KeyCode expected;
    } cases[] = {
        {SDL_SCANCODE_W, OIS::KC_W},
        {SDL_SCANCODE_A, OIS::KC_A},
        {SDL_SCANCODE_S, OIS::KC_S},
        {SDL_SCANCODE_D, OIS::KC_D},
        {SDL_SCANCODE_UP, OIS::KC_UP},
        {SDL_SCANCODE_DOWN, OIS::KC_DOWN},
        {SDL_SCANCODE_LEFT, OIS::KC_LEFT},
        {SDL_SCANCODE_RIGHT, OIS::KC_RIGHT},
        {SDL_SCANCODE_PAGEUP, OIS::KC_PGUP},
        {SDL_SCANCODE_PAGEDOWN, OIS::KC_PGDOWN},
        {SDL_SCANCODE_F1, OIS::KC_F1},
        {SDL_SCANCODE_F12, OIS::KC_F12},
        {SDL_SCANCODE_F15, OIS::KC_F15},
        {SDL_SCANCODE_RETURN, OIS::KC_RETURN},
        {SDL_SCANCODE_ESCAPE, OIS::KC_ESCAPE},
        {SDL_SCANCODE_LSHIFT, OIS::KC_LSHIFT},
        {SDL_SCANCODE_RSHIFT, OIS::KC_RSHIFT},
        {SDL_SCANCODE_LCTRL, OIS::KC_LCONTROL},
        {SDL_SCANCODE_RCTRL, OIS::KC_RCONTROL},
        {SDL_SCANCODE_LALT, OIS::KC_LMENU},
        {SDL_SCANCODE_RALT, OIS::KC_RMENU},
        {SDL_SCANCODE_LGUI, OIS::KC_LWIN},
        {SDL_SCANCODE_RGUI, OIS::KC_RWIN},
        {SDL_SCANCODE_KP_ENTER, OIS::KC_NUMPADENTER},
        {SDL_SCANCODE_PRINTSCREEN, OIS::KC_SYSRQ},
        {SDL_SCANCODE_UNKNOWN, OIS::KC_UNASSIGNED},
    };

    for (const auto& test_case : cases)
    {
        if (RoR::MacOSInputBridge::TranslateScancode(test_case.source) !=
            test_case.expected)
        {
            return Fail("a required SDL scancode mapping changed");
        }
    }

    RoR::MacOSInputBridge::KeyState state;
    if (state.IsDown(OIS::KC_UP))
    {
        return Fail("new state started with a pressed key");
    }
    if (!state.Set(OIS::KC_UP, true) || !state.IsDown(OIS::KC_UP))
    {
        return Fail("initial key press was not recorded");
    }
    if (state.Set(OIS::KC_UP, true))
    {
        return Fail("repeated key press was reported as a transition");
    }
    const OIS::KeyCode modifiers[] = {
        OIS::KC_LSHIFT,
        OIS::KC_RSHIFT,
        OIS::KC_LCONTROL,
        OIS::KC_RCONTROL,
        OIS::KC_LMENU,
        OIS::KC_RMENU,
        OIS::KC_LWIN,
        OIS::KC_RWIN,
    };
    for (const OIS::KeyCode modifier : modifiers)
    {
        if (!state.Set(modifier, true) || !state.IsDown(modifier))
        {
            return Fail("modifier key press was not recorded");
        }
    }
    state.Reset();
    if (state.IsDown(OIS::KC_UP))
    {
        return Fail("focus-loss reset left a gameplay key pressed");
    }
    for (const OIS::KeyCode modifier : modifiers)
    {
        if (state.IsDown(modifier))
        {
            return Fail("focus-loss reset left a modifier pressed");
        }
    }
    if (state.Set(OIS::KC_UNASSIGNED, true) ||
        state.IsDown(OIS::KC_UNASSIGNED))
    {
        return Fail("unassigned scancode polluted keyboard state");
    }
    const OIS::KeyCode out_of_bounds = static_cast<OIS::KeyCode>(256);
    if (state.Set(out_of_bounds, true) || state.IsDown(out_of_bounds))
    {
        return Fail("out-of-bounds key polluted keyboard state");
    }

    std::cout << "macOS SDL/OIS input bridge verified\n";
    return EXIT_SUCCESS;
}
