/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererGameInputTarget.h"

namespace RoR {

RendererGameKey
TranslateRendererSdlScancodeToGame(std::uint16_t scancode) noexcept {
  switch (scancode) {
  case 41U: return RendererGameKey::ESCAPE;
  case 30U: return RendererGameKey::DIGIT_1;
  case 31U: return RendererGameKey::DIGIT_2;
  case 32U: return RendererGameKey::DIGIT_3;
  case 33U: return RendererGameKey::DIGIT_4;
  case 34U: return RendererGameKey::DIGIT_5;
  case 35U: return RendererGameKey::DIGIT_6;
  case 36U: return RendererGameKey::DIGIT_7;
  case 37U: return RendererGameKey::DIGIT_8;
  case 38U: return RendererGameKey::DIGIT_9;
  case 39U: return RendererGameKey::DIGIT_0;
  case 45U: return RendererGameKey::MINUS;
  case 46U: return RendererGameKey::EQUALS;
  case 42U: return RendererGameKey::BACK;
  case 43U: return RendererGameKey::TAB;
  case 20U: return RendererGameKey::Q;
  case 26U: return RendererGameKey::W;
  case 8U: return RendererGameKey::E;
  case 21U: return RendererGameKey::R;
  case 23U: return RendererGameKey::T;
  case 28U: return RendererGameKey::Y;
  case 24U: return RendererGameKey::U;
  case 12U: return RendererGameKey::I;
  case 18U: return RendererGameKey::O;
  case 19U: return RendererGameKey::P;
  case 47U: return RendererGameKey::LEFT_BRACKET;
  case 48U: return RendererGameKey::RIGHT_BRACKET;
  case 40U:
  case 158U: return RendererGameKey::RETURN;
  case 224U: return RendererGameKey::LEFT_CONTROL;
  case 4U: return RendererGameKey::A;
  case 22U: return RendererGameKey::S;
  case 7U: return RendererGameKey::D;
  case 9U: return RendererGameKey::F;
  case 10U: return RendererGameKey::G;
  case 11U: return RendererGameKey::H;
  case 13U: return RendererGameKey::J;
  case 14U: return RendererGameKey::K;
  case 15U: return RendererGameKey::L;
  case 51U: return RendererGameKey::SEMICOLON;
  case 52U: return RendererGameKey::APOSTROPHE;
  case 53U: return RendererGameKey::GRAVE;
  case 225U: return RendererGameKey::LEFT_SHIFT;
  case 49U:
  case 50U: return RendererGameKey::BACKSLASH;
  case 29U: return RendererGameKey::Z;
  case 27U: return RendererGameKey::X;
  case 6U: return RendererGameKey::C;
  case 25U: return RendererGameKey::V;
  case 5U: return RendererGameKey::B;
  case 17U: return RendererGameKey::N;
  case 16U: return RendererGameKey::M;
  case 54U: return RendererGameKey::COMMA;
  case 55U: return RendererGameKey::PERIOD;
  case 56U: return RendererGameKey::SLASH;
  case 229U: return RendererGameKey::RIGHT_SHIFT;
  case 85U: return RendererGameKey::MULTIPLY;
  case 226U: return RendererGameKey::LEFT_ALT;
  case 44U: return RendererGameKey::SPACE;
  case 57U: return RendererGameKey::CAPITAL;
  case 58U: return RendererGameKey::F1;
  case 59U: return RendererGameKey::F2;
  case 60U: return RendererGameKey::F3;
  case 61U: return RendererGameKey::F4;
  case 62U: return RendererGameKey::F5;
  case 63U: return RendererGameKey::F6;
  case 64U: return RendererGameKey::F7;
  case 65U: return RendererGameKey::F8;
  case 66U: return RendererGameKey::F9;
  case 67U: return RendererGameKey::F10;
  case 83U: return RendererGameKey::NUM_LOCK;
  case 71U: return RendererGameKey::SCROLL_LOCK;
  case 95U: return RendererGameKey::NUMPAD_7;
  case 96U: return RendererGameKey::NUMPAD_8;
  case 97U: return RendererGameKey::NUMPAD_9;
  case 86U: return RendererGameKey::SUBTRACT;
  case 92U: return RendererGameKey::NUMPAD_4;
  case 93U: return RendererGameKey::NUMPAD_5;
  case 94U: return RendererGameKey::NUMPAD_6;
  case 87U: return RendererGameKey::ADD;
  case 89U: return RendererGameKey::NUMPAD_1;
  case 90U: return RendererGameKey::NUMPAD_2;
  case 91U: return RendererGameKey::NUMPAD_3;
  case 98U: return RendererGameKey::NUMPAD_0;
  case 99U: return RendererGameKey::DECIMAL;
  case 100U: return RendererGameKey::OEM_102;
  case 68U: return RendererGameKey::F11;
  case 69U: return RendererGameKey::F12;
  case 104U: return RendererGameKey::F13;
  case 105U: return RendererGameKey::F14;
  case 106U: return RendererGameKey::F15;
  case 135U: return RendererGameKey::ABNT_C1;
  case 136U:
  case 146U:
  case 147U: return RendererGameKey::KANA;
  case 137U: return RendererGameKey::YEN;
  case 138U: return RendererGameKey::CONVERT;
  case 139U: return RendererGameKey::NO_CONVERT;
  case 145U:
  case 148U: return RendererGameKey::KANJI;
  case 103U:
  case 134U: return RendererGameKey::NUMPAD_EQUALS;
  case 259U:
  case 285U: return RendererGameKey::PREVIOUS_TRACK;
  case 258U:
  case 286U: return RendererGameKey::NEXT_TRACK;
  case 88U: return RendererGameKey::NUMPAD_ENTER;
  case 228U: return RendererGameKey::RIGHT_CONTROL;
  case 127U:
  case 262U: return RendererGameKey::MUTE;
  case 266U: return RendererGameKey::CALCULATOR;
  case 261U: return RendererGameKey::PLAY_PAUSE;
  case 260U: return RendererGameKey::MEDIA_STOP;
  case 129U: return RendererGameKey::VOLUME_DOWN;
  case 128U: return RendererGameKey::VOLUME_UP;
  case 264U:
  case 269U: return RendererGameKey::WEB_HOME;
  case 133U:
  case 159U: return RendererGameKey::NUMPAD_COMMA;
  case 84U: return RendererGameKey::DIVIDE;
  case 70U:
  case 154U: return RendererGameKey::SYS_REQUEST;
  case 230U:
  case 257U: return RendererGameKey::RIGHT_ALT;
  case 72U: return RendererGameKey::PAUSE;
  case 74U: return RendererGameKey::HOME;
  case 82U: return RendererGameKey::UP;
  case 75U: return RendererGameKey::PAGE_UP;
  case 80U: return RendererGameKey::LEFT;
  case 79U: return RendererGameKey::RIGHT;
  case 77U: return RendererGameKey::END;
  case 81U: return RendererGameKey::DOWN;
  case 78U: return RendererGameKey::PAGE_DOWN;
  case 73U: return RendererGameKey::INSERT;
  case 76U: return RendererGameKey::DELETE_KEY;
  case 227U: return RendererGameKey::LEFT_GUI;
  case 231U: return RendererGameKey::RIGHT_GUI;
  case 101U:
  case 118U: return RendererGameKey::APPLICATION;
  case 102U: return RendererGameKey::POWER;
  case 282U: return RendererGameKey::SLEEP;
  case 268U: return RendererGameKey::WEB_SEARCH;
  case 274U: return RendererGameKey::WEB_FAVORITES;
  case 273U: return RendererGameKey::WEB_REFRESH;
  case 120U:
  case 272U: return RendererGameKey::WEB_STOP;
  case 271U: return RendererGameKey::WEB_FORWARD;
  case 270U: return RendererGameKey::WEB_BACK;
  case 267U: return RendererGameKey::MY_COMPUTER;
  case 265U: return RendererGameKey::MAIL;
  case 263U: return RendererGameKey::MEDIA_SELECT;
  default: return RendererGameKey::UNASSIGNED;
  }
}

bool TryTranslateRendererSdlMouseButtonToGame(
    std::uint8_t button, RendererGameMouseButton &translated) noexcept {
  switch (button) {
  case 1U: translated = RendererGameMouseButton::LEFT; return true;
  case 2U: translated = RendererGameMouseButton::MIDDLE; return true;
  case 3U: translated = RendererGameMouseButton::RIGHT; return true;
  case 4U: translated = RendererGameMouseButton::X1; return true;
  case 5U: translated = RendererGameMouseButton::X2; return true;
  default: return false;
  }
}

} // namespace RoR
