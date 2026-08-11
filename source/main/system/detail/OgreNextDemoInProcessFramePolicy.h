/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Disposable first-demo policy adapter for the reusable direct session.

#pragma once

#include "../RendererInProcessSession.h"
#include "OgreNextDemoFrameNormalization.h"

#include <optional>

namespace RoR::Detail {

/// Keeps the first product demo's exact joined-capture surface, fixed camera
/// clip range, and one-shadow-sun admission outside RendererInProcessSession.
class OgreNextDemoInProcessFramePolicy final
    : public IRendererInProcessFramePolicy {
public:
  [[nodiscard]] Render::ValidationResult
  BeginCapture(std::uint32_t drawable_width,
               std::uint32_t drawable_height) override;
  void EndCapture() noexcept override;
  [[nodiscard]] Render::ValidationResult NormalizeAndValidate(
      Render::GraphicsSceneFrameInput &frame,
      std::uint32_t drawable_width,
      std::uint32_t drawable_height) override;

private:
  std::optional<OgreNextDemoCaptureSurfaceScope> capture_scope_;
};

} // namespace RoR::Detail
