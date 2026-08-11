/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RendererGameInputEngineTarget.h"

#include "AppContext.h"
#include "Application.h"
#include "InputEngine.h"

namespace RoR {

static_assert(static_cast<std::uint16_t>(RendererGameKey::A) ==
              static_cast<std::uint16_t>(OIS::KC_A));
static_assert(static_cast<std::uint16_t>(
                  RendererGameKey::RIGHT_ALT) ==
              static_cast<std::uint16_t>(OIS::KC_RMENU));
static_assert(static_cast<std::uint8_t>(
                  RendererGameMouseButton::X2) ==
              static_cast<std::uint8_t>(OIS::MB_Button4));

bool RendererGameInputEngineTarget::ActivateInput() noexcept {
  InputEngine *const input = App::GetInputEngine();
  return input != nullptr && input->EnableRendererInput();
}

void RendererGameInputEngineTarget::KeyChanged(RendererGameKey key,
                                                 bool pressed) noexcept {
  App::GetAppContext()->InjectRendererInputKey(
      static_cast<OIS::KeyCode>(key), pressed);
}

void RendererGameInputEngineTarget::MouseMoved(
    std::int32_t x, std::int32_t y, std::int32_t delta_x,
    std::int32_t delta_y) noexcept {
  App::GetAppContext()->InjectRendererInputMouseMotion(x, y, delta_x,
                                                        delta_y);
}

void RendererGameInputEngineTarget::MouseButtonChanged(
    RendererGameMouseButton button, bool pressed) noexcept {
  App::GetAppContext()->InjectRendererInputMouseButton(
      static_cast<OIS::MouseButtonID>(button), pressed);
}

void RendererGameInputEngineTarget::MouseWheel(float delta_x,
                                                 float delta_y) noexcept {
  App::GetAppContext()->InjectRendererInputMouseWheel(delta_x, delta_y);
}

void RendererGameInputEngineTarget::TextInput(
    std::string_view utf8) noexcept {
  App::GetAppContext()->InjectRendererInputText(utf8);
}

void RendererGameInputEngineTarget::FocusChanged(bool focused) noexcept {
  App::GetAppContext()->InjectRendererInputFocus(focused);
}

void RendererGameInputEngineTarget::WindowCloseRequested() noexcept {
  App::GetAppContext()->InjectRendererInputWindowClose();
}

bool RendererGameInputEngineTarget::Reconcile(
    const RendererGameInputState &state) noexcept {
  InputEngine *const input = App::GetInputEngine();
  return input != nullptr &&
         (input->IsRendererInputActive() ||
          input->EnableRendererInput()) &&
         input->ApplyRendererInput(state);
}

} // namespace RoR
