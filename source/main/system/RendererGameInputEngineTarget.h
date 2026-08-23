/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Product binding from renderer input to RoR input/UI.

#pragma once

#include "RendererGameInputTarget.h"

namespace RoR {

/// Monotonic evidence from the product binding between the visible
/// Ogre-Next SDL event drain and InputEngine.  It deliberately records no
/// serialized input payload: the game loop consumes this audit only to bind a
/// native window transition to the authoritative player-control state and a
/// later presented frame.
struct RendererGameInputEngineAudit final {
  std::uint32_t version = 2U;
  std::uint64_t key_transitions = 0U;
  std::uint64_t reconciliations = 0U;
  std::uint64_t reconciled_event_id = 0U;
  std::uint64_t reconciled_key_transitions = 0U;
  std::uint64_t reconciled_pressed_transition = 0U;
  std::uint64_t reconciled_released_transition = 0U;
  RendererGameKey reconciled_pressed_key = RendererGameKey::UNASSIGNED;
  RendererGameKey reconciled_released_key = RendererGameKey::UNASSIGNED;
  bool reconciled_pressed_delivered = false;
  bool reconciled_released_delivered = false;
  bool last_reconcile_succeeded = false;
  bool available = false;
};

/// Contains no renderer or device ownership. AppContext routes ordered events
/// through existing GUI callbacks and InputEngine owns the final held state.
class RendererGameInputEngineTarget final
    : public IRendererGameInputTarget {
public:
  bool ActivateInput() noexcept override;
  bool DisplayMetricsChanged(
      const RendererGameDisplayMetrics &metrics) noexcept override;
  void KeyChanged(RendererGameKey key,
                  bool pressed) noexcept override;
  void MouseMoved(std::int32_t x, std::int32_t y, std::int32_t delta_x,
                  std::int32_t delta_y) noexcept override;
  void MouseButtonChanged(RendererGameMouseButton button,
                          bool pressed) noexcept override;
  void MouseWheel(float delta_x, float delta_y) noexcept override;
  void TextInput(std::string_view utf8) noexcept override;
  void FocusChanged(bool focused) noexcept override;
  void WindowCloseRequested() noexcept override;
  bool Reconcile(
      const RendererGameInputState &state) noexcept override;
  [[nodiscard]] RendererGameInputEngineAudit Audit() const noexcept {
    return audit_;
  }

private:
  bool direct_display_metrics_active_ = false;
  bool direct_transition_failed_ = false;
  RendererGameInputEngineAudit audit_;
  std::uint64_t last_pressed_transition_ = 0U;
  std::uint64_t last_released_transition_ = 0U;
  RendererGameKey last_pressed_key_ = RendererGameKey::UNASSIGNED;
  RendererGameKey last_released_key_ = RendererGameKey::UNASSIGNED;
  bool last_pressed_delivered_ = false;
  bool last_released_delivered_ = false;
};

} // namespace RoR
