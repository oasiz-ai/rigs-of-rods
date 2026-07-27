/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#pragma once

#include <OISKeyboard.h>
#include <SDL_scancode.h>

#include <array>
#include <cstddef>

namespace RoR {
namespace MacOSInputBridge {

inline OIS::KeyCode TranslateScancode(SDL_Scancode scancode)
{
    switch (scancode)
    {
    case SDL_SCANCODE_ESCAPE: return OIS::KC_ESCAPE;
    case SDL_SCANCODE_1: return OIS::KC_1;
    case SDL_SCANCODE_2: return OIS::KC_2;
    case SDL_SCANCODE_3: return OIS::KC_3;
    case SDL_SCANCODE_4: return OIS::KC_4;
    case SDL_SCANCODE_5: return OIS::KC_5;
    case SDL_SCANCODE_6: return OIS::KC_6;
    case SDL_SCANCODE_7: return OIS::KC_7;
    case SDL_SCANCODE_8: return OIS::KC_8;
    case SDL_SCANCODE_9: return OIS::KC_9;
    case SDL_SCANCODE_0: return OIS::KC_0;
    case SDL_SCANCODE_MINUS: return OIS::KC_MINUS;
    case SDL_SCANCODE_EQUALS: return OIS::KC_EQUALS;
    case SDL_SCANCODE_BACKSPACE: return OIS::KC_BACK;
    case SDL_SCANCODE_TAB: return OIS::KC_TAB;
    case SDL_SCANCODE_Q: return OIS::KC_Q;
    case SDL_SCANCODE_W: return OIS::KC_W;
    case SDL_SCANCODE_E: return OIS::KC_E;
    case SDL_SCANCODE_R: return OIS::KC_R;
    case SDL_SCANCODE_T: return OIS::KC_T;
    case SDL_SCANCODE_Y: return OIS::KC_Y;
    case SDL_SCANCODE_U: return OIS::KC_U;
    case SDL_SCANCODE_I: return OIS::KC_I;
    case SDL_SCANCODE_O: return OIS::KC_O;
    case SDL_SCANCODE_P: return OIS::KC_P;
    case SDL_SCANCODE_LEFTBRACKET: return OIS::KC_LBRACKET;
    case SDL_SCANCODE_RIGHTBRACKET: return OIS::KC_RBRACKET;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_RETURN2: return OIS::KC_RETURN;
    case SDL_SCANCODE_LCTRL: return OIS::KC_LCONTROL;
    case SDL_SCANCODE_A: return OIS::KC_A;
    case SDL_SCANCODE_S: return OIS::KC_S;
    case SDL_SCANCODE_D: return OIS::KC_D;
    case SDL_SCANCODE_F: return OIS::KC_F;
    case SDL_SCANCODE_G: return OIS::KC_G;
    case SDL_SCANCODE_H: return OIS::KC_H;
    case SDL_SCANCODE_J: return OIS::KC_J;
    case SDL_SCANCODE_K: return OIS::KC_K;
    case SDL_SCANCODE_L: return OIS::KC_L;
    case SDL_SCANCODE_SEMICOLON: return OIS::KC_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE: return OIS::KC_APOSTROPHE;
    case SDL_SCANCODE_GRAVE: return OIS::KC_GRAVE;
    case SDL_SCANCODE_LSHIFT: return OIS::KC_LSHIFT;
    case SDL_SCANCODE_BACKSLASH:
    case SDL_SCANCODE_NONUSHASH: return OIS::KC_BACKSLASH;
    case SDL_SCANCODE_Z: return OIS::KC_Z;
    case SDL_SCANCODE_X: return OIS::KC_X;
    case SDL_SCANCODE_C: return OIS::KC_C;
    case SDL_SCANCODE_V: return OIS::KC_V;
    case SDL_SCANCODE_B: return OIS::KC_B;
    case SDL_SCANCODE_N: return OIS::KC_N;
    case SDL_SCANCODE_M: return OIS::KC_M;
    case SDL_SCANCODE_COMMA: return OIS::KC_COMMA;
    case SDL_SCANCODE_PERIOD: return OIS::KC_PERIOD;
    case SDL_SCANCODE_SLASH: return OIS::KC_SLASH;
    case SDL_SCANCODE_RSHIFT: return OIS::KC_RSHIFT;
    case SDL_SCANCODE_KP_MULTIPLY: return OIS::KC_MULTIPLY;
    case SDL_SCANCODE_LALT: return OIS::KC_LMENU;
    case SDL_SCANCODE_SPACE: return OIS::KC_SPACE;
    case SDL_SCANCODE_CAPSLOCK: return OIS::KC_CAPITAL;
    case SDL_SCANCODE_F1: return OIS::KC_F1;
    case SDL_SCANCODE_F2: return OIS::KC_F2;
    case SDL_SCANCODE_F3: return OIS::KC_F3;
    case SDL_SCANCODE_F4: return OIS::KC_F4;
    case SDL_SCANCODE_F5: return OIS::KC_F5;
    case SDL_SCANCODE_F6: return OIS::KC_F6;
    case SDL_SCANCODE_F7: return OIS::KC_F7;
    case SDL_SCANCODE_F8: return OIS::KC_F8;
    case SDL_SCANCODE_F9: return OIS::KC_F9;
    case SDL_SCANCODE_F10: return OIS::KC_F10;
    case SDL_SCANCODE_NUMLOCKCLEAR: return OIS::KC_NUMLOCK;
    case SDL_SCANCODE_SCROLLLOCK: return OIS::KC_SCROLL;
    case SDL_SCANCODE_KP_7: return OIS::KC_NUMPAD7;
    case SDL_SCANCODE_KP_8: return OIS::KC_NUMPAD8;
    case SDL_SCANCODE_KP_9: return OIS::KC_NUMPAD9;
    case SDL_SCANCODE_KP_MINUS: return OIS::KC_SUBTRACT;
    case SDL_SCANCODE_KP_4: return OIS::KC_NUMPAD4;
    case SDL_SCANCODE_KP_5: return OIS::KC_NUMPAD5;
    case SDL_SCANCODE_KP_6: return OIS::KC_NUMPAD6;
    case SDL_SCANCODE_KP_PLUS: return OIS::KC_ADD;
    case SDL_SCANCODE_KP_1: return OIS::KC_NUMPAD1;
    case SDL_SCANCODE_KP_2: return OIS::KC_NUMPAD2;
    case SDL_SCANCODE_KP_3: return OIS::KC_NUMPAD3;
    case SDL_SCANCODE_KP_0: return OIS::KC_NUMPAD0;
    case SDL_SCANCODE_KP_PERIOD: return OIS::KC_DECIMAL;
    case SDL_SCANCODE_NONUSBACKSLASH: return OIS::KC_OEM_102;
    case SDL_SCANCODE_F11: return OIS::KC_F11;
    case SDL_SCANCODE_F12: return OIS::KC_F12;
    case SDL_SCANCODE_F13: return OIS::KC_F13;
    case SDL_SCANCODE_F14: return OIS::KC_F14;
    case SDL_SCANCODE_F15: return OIS::KC_F15;
    case SDL_SCANCODE_INTERNATIONAL1: return OIS::KC_ABNT_C1;
    case SDL_SCANCODE_INTERNATIONAL2:
    case SDL_SCANCODE_LANG3:
    case SDL_SCANCODE_LANG4: return OIS::KC_KANA;
    case SDL_SCANCODE_INTERNATIONAL3: return OIS::KC_YEN;
    case SDL_SCANCODE_INTERNATIONAL4: return OIS::KC_CONVERT;
    case SDL_SCANCODE_INTERNATIONAL5: return OIS::KC_NOCONVERT;
    case SDL_SCANCODE_LANG2:
    case SDL_SCANCODE_LANG5: return OIS::KC_KANJI;
    case SDL_SCANCODE_KP_EQUALS:
    case SDL_SCANCODE_KP_EQUALSAS400: return OIS::KC_NUMPADEQUALS;
    case SDL_SCANCODE_AUDIOPREV:
    case SDL_SCANCODE_AUDIOREWIND: return OIS::KC_PREVTRACK;
    case SDL_SCANCODE_AUDIONEXT:
    case SDL_SCANCODE_AUDIOFASTFORWARD: return OIS::KC_NEXTTRACK;
    case SDL_SCANCODE_KP_ENTER: return OIS::KC_NUMPADENTER;
    case SDL_SCANCODE_RCTRL: return OIS::KC_RCONTROL;
    case SDL_SCANCODE_MUTE:
    case SDL_SCANCODE_AUDIOMUTE: return OIS::KC_MUTE;
    case SDL_SCANCODE_CALCULATOR: return OIS::KC_CALCULATOR;
    case SDL_SCANCODE_AUDIOPLAY: return OIS::KC_PLAYPAUSE;
    case SDL_SCANCODE_AUDIOSTOP: return OIS::KC_MEDIASTOP;
    case SDL_SCANCODE_VOLUMEDOWN: return OIS::KC_VOLUMEDOWN;
    case SDL_SCANCODE_VOLUMEUP: return OIS::KC_VOLUMEUP;
    case SDL_SCANCODE_WWW:
    case SDL_SCANCODE_AC_HOME: return OIS::KC_WEBHOME;
    case SDL_SCANCODE_KP_COMMA:
    case SDL_SCANCODE_SEPARATOR: return OIS::KC_NUMPADCOMMA;
    case SDL_SCANCODE_KP_DIVIDE: return OIS::KC_DIVIDE;
    case SDL_SCANCODE_PRINTSCREEN:
    case SDL_SCANCODE_SYSREQ: return OIS::KC_SYSRQ;
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_MODE: return OIS::KC_RMENU;
    case SDL_SCANCODE_PAUSE: return OIS::KC_PAUSE;
    case SDL_SCANCODE_HOME: return OIS::KC_HOME;
    case SDL_SCANCODE_UP: return OIS::KC_UP;
    case SDL_SCANCODE_PAGEUP: return OIS::KC_PGUP;
    case SDL_SCANCODE_LEFT: return OIS::KC_LEFT;
    case SDL_SCANCODE_RIGHT: return OIS::KC_RIGHT;
    case SDL_SCANCODE_END: return OIS::KC_END;
    case SDL_SCANCODE_DOWN: return OIS::KC_DOWN;
    case SDL_SCANCODE_PAGEDOWN: return OIS::KC_PGDOWN;
    case SDL_SCANCODE_INSERT: return OIS::KC_INSERT;
    case SDL_SCANCODE_DELETE: return OIS::KC_DELETE;
    case SDL_SCANCODE_LGUI: return OIS::KC_LWIN;
    case SDL_SCANCODE_RGUI: return OIS::KC_RWIN;
    case SDL_SCANCODE_APPLICATION:
    case SDL_SCANCODE_MENU: return OIS::KC_APPS;
    case SDL_SCANCODE_POWER: return OIS::KC_POWER;
    case SDL_SCANCODE_SLEEP: return OIS::KC_SLEEP;
    case SDL_SCANCODE_AC_SEARCH: return OIS::KC_WEBSEARCH;
    case SDL_SCANCODE_AC_BOOKMARKS: return OIS::KC_WEBFAVORITES;
    case SDL_SCANCODE_AC_REFRESH: return OIS::KC_WEBREFRESH;
    case SDL_SCANCODE_STOP:
    case SDL_SCANCODE_AC_STOP: return OIS::KC_WEBSTOP;
    case SDL_SCANCODE_AC_FORWARD: return OIS::KC_WEBFORWARD;
    case SDL_SCANCODE_AC_BACK: return OIS::KC_WEBBACK;
    case SDL_SCANCODE_COMPUTER: return OIS::KC_MYCOMPUTER;
    case SDL_SCANCODE_MAIL: return OIS::KC_MAIL;
    case SDL_SCANCODE_MEDIASELECT: return OIS::KC_MEDIASELECT;
    default: return OIS::KC_UNASSIGNED;
    }
}

class KeyState
{
public:
    bool Set(OIS::KeyCode key, bool down)
    {
        const std::size_t index = static_cast<std::size_t>(key);
        if (key == OIS::KC_UNASSIGNED || index >= m_keys.size())
        {
            return false;
        }

        const bool changed = m_keys[index] != down;
        m_keys[index] = down;
        return changed;
    }

    bool IsDown(OIS::KeyCode key) const
    {
        const std::size_t index = static_cast<std::size_t>(key);
        return key != OIS::KC_UNASSIGNED &&
            index < m_keys.size() &&
            m_keys[index];
    }

    void Reset()
    {
        m_keys.fill(false);
    }

private:
    std::array<bool, 256> m_keys = {};
};

} // namespace MacOSInputBridge
} // namespace RoR
