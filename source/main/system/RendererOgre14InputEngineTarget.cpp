/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "RendererOgre14InputEngineTarget.h"

#include "AppContext.h"
#include "Application.h"
#include "InputEngine.h"

namespace RoR {

static_assert(static_cast<std::uint16_t>(RendererOgre14LegacyKey::A) ==
              static_cast<std::uint16_t>(OIS::KC_A));
static_assert(static_cast<std::uint16_t>(
                  RendererOgre14LegacyKey::RIGHT_ALT) ==
              static_cast<std::uint16_t>(OIS::KC_RMENU));
static_assert(static_cast<std::uint8_t>(
                  RendererOgre14LegacyMouseButton::X2) ==
              static_cast<std::uint8_t>(OIS::MB_Button4));

bool RendererOgre14InputEngineTarget::ActivateTransport() noexcept {
  InputEngine *const input = App::GetInputEngine();
  return input != nullptr && input->EnableRendererTransportInput();
}

void RendererOgre14InputEngineTarget::KeyChanged(RendererOgre14LegacyKey key,
                                                 bool pressed) noexcept {
  App::GetAppContext()->InjectRendererBridgeKey(
      static_cast<OIS::KeyCode>(key), pressed);
}

void RendererOgre14InputEngineTarget::MouseMoved(
    std::int32_t x, std::int32_t y, std::int32_t delta_x,
    std::int32_t delta_y) noexcept {
  App::GetAppContext()->InjectRendererBridgeMouseMotion(x, y, delta_x,
                                                        delta_y);
}

void RendererOgre14InputEngineTarget::MouseButtonChanged(
    RendererOgre14LegacyMouseButton button, bool pressed) noexcept {
  App::GetAppContext()->InjectRendererBridgeMouseButton(
      static_cast<OIS::MouseButtonID>(button), pressed);
}

void RendererOgre14InputEngineTarget::MouseWheel(float delta_x,
                                                 float delta_y) noexcept {
  App::GetAppContext()->InjectRendererBridgeMouseWheel(delta_x, delta_y);
}

void RendererOgre14InputEngineTarget::TextInput(
    std::string_view utf8) noexcept {
  App::GetAppContext()->InjectRendererBridgeText(utf8);
}

void RendererOgre14InputEngineTarget::FocusChanged(bool focused) noexcept {
  App::GetAppContext()->InjectRendererBridgeFocus(focused);
}

void RendererOgre14InputEngineTarget::WindowCloseRequested() noexcept {
  App::GetAppContext()->InjectRendererBridgeWindowClose();
}

bool RendererOgre14InputEngineTarget::Reconcile(
    const RendererOgre14LegacyInputState &state) noexcept {
  InputEngine *const input = App::GetInputEngine();
  return input != nullptr &&
         (input->IsRendererTransportInputActive() ||
          input->EnableRendererTransportInput()) &&
         input->ApplyRendererTransportInput(state);
}

} // namespace RoR
