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
  direct_display_metrics_active_ = false;
  direct_transition_failed_ = false;
  audit_ = RendererGameInputEngineAudit{};
  last_pressed_transition_ = 0U;
  last_released_transition_ = 0U;
  last_pressed_key_ = RendererGameKey::UNASSIGNED;
  last_released_key_ = RendererGameKey::UNASSIGNED;
  last_pressed_delivered_ = false;
  last_released_delivered_ = false;
  InputEngine *const input = App::GetInputEngine();
  const bool activated = input != nullptr && input->EnableRendererInput();
  audit_.available = activated;
  return activated;
}

bool RendererGameInputEngineTarget::DisplayMetricsChanged(
    const RendererGameDisplayMetrics &metrics) noexcept {
  direct_transition_failed_ = false;
  direct_display_metrics_active_ =
      App::GetAppContext()->InjectRendererInputDisplayMetrics(metrics);
  return direct_display_metrics_active_;
}

void RendererGameInputEngineTarget::KeyChanged(RendererGameKey key,
                                                 bool pressed) noexcept {
  ++audit_.key_transitions;
  const bool delivered = App::GetAppContext()->InjectRendererInputKey(
      static_cast<OIS::KeyCode>(key), pressed);
  InputEngine *const input = App::GetInputEngine();
  const bool accepted =
      delivered && input != nullptr && input->IsRendererInputActive();
  if (pressed) {
    last_pressed_transition_ = audit_.key_transitions;
    last_pressed_key_ = key;
    last_pressed_delivered_ = accepted;
  } else {
    last_released_transition_ = audit_.key_transitions;
    last_released_key_ = key;
    last_released_delivered_ = accepted;
  }
}

void RendererGameInputEngineTarget::MouseMoved(
    std::int32_t x, std::int32_t y, std::int32_t delta_x,
    std::int32_t delta_y) noexcept {
  const bool delivered = App::GetAppContext()->InjectRendererInputMouseMotion(
      x, y, delta_x, delta_y);
  direct_transition_failed_ =
      direct_transition_failed_ ||
      (direct_display_metrics_active_ && !delivered);
}

void RendererGameInputEngineTarget::MouseButtonChanged(
    RendererGameMouseButton button, bool pressed) noexcept {
  const bool delivered = App::GetAppContext()->InjectRendererInputMouseButton(
      static_cast<OIS::MouseButtonID>(button), pressed);
  direct_transition_failed_ =
      direct_transition_failed_ ||
      (direct_display_metrics_active_ && !delivered);
}

void RendererGameInputEngineTarget::MouseWheel(float delta_x,
                                                 float delta_y) noexcept {
  const bool delivered =
      App::GetAppContext()->InjectRendererInputMouseWheel(delta_x, delta_y);
  direct_transition_failed_ =
      direct_transition_failed_ ||
      (direct_display_metrics_active_ && !delivered);
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
  const bool reconciled =
      !direct_transition_failed_ && input != nullptr &&
      (input->IsRendererInputActive() || input->EnableRendererInput()) &&
      input->ApplyRendererInput(state);
  ++audit_.reconciliations;
  audit_.last_reconcile_succeeded = reconciled;
  if (reconciled) {
    audit_.reconciled_event_id = state.through_event_id;
    audit_.reconciled_key_transitions = audit_.key_transitions;
    audit_.reconciled_pressed_transition = last_pressed_transition_;
    audit_.reconciled_released_transition = last_released_transition_;
    audit_.reconciled_pressed_key = last_pressed_key_;
    audit_.reconciled_released_key = last_released_key_;
    audit_.reconciled_pressed_delivered = last_pressed_delivered_;
    audit_.reconciled_released_delivered = last_released_delivered_;
  }
  return reconciled;
}

} // namespace RoR
