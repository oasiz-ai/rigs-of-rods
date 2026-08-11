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

/// Contains no renderer or device ownership. AppContext routes ordered events
/// through existing GUI callbacks and InputEngine owns the final held state.
class RendererGameInputEngineTarget final
    : public IRendererGameInputTarget {
public:
  bool ActivateInput() noexcept override;
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
};

} // namespace RoR
