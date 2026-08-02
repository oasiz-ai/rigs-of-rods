/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Product binding from Ogre-Next input transport to RoR input/UI.

#pragma once

#include "RendererOgre14InputAdapter.h"

namespace RoR {

/// Contains no renderer or device ownership. AppContext routes ordered events
/// through existing GUI callbacks and InputEngine owns the final held state.
class RendererOgre14InputEngineTarget final
    : public IRendererOgre14InputTarget {
public:
  bool ActivateTransport() noexcept override;
  void KeyChanged(RendererOgre14LegacyKey key,
                  bool pressed) noexcept override;
  void MouseMoved(std::int32_t x, std::int32_t y, std::int32_t delta_x,
                  std::int32_t delta_y) noexcept override;
  void MouseButtonChanged(RendererOgre14LegacyMouseButton button,
                          bool pressed) noexcept override;
  void MouseWheel(float delta_x, float delta_y) noexcept override;
  void TextInput(std::string_view utf8) noexcept override;
  void FocusChanged(bool focused) noexcept override;
  void WindowCloseRequested() noexcept override;
  bool Reconcile(
      const RendererOgre14LegacyInputState &state) noexcept override;
};

} // namespace RoR
