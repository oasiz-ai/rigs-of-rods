/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "OgreNextDemoInProcessFramePolicy.h"

#include "OgreNextDemoFrameNormalization.h"

namespace RoR::Detail {

Render::ValidationResult OgreNextDemoInProcessFramePolicy::BeginCapture(
    std::uint32_t drawable_width, std::uint32_t drawable_height) {
  if (capture_scope_.has_value()) {
    return Render::ValidationResult::Failure(
        Render::ValidationCode::NON_DETERMINISTIC_ORDER,
        "ogre_next_demo.capture_scope",
        "capture policy cannot be entered recursively");
  }
  capture_scope_.emplace(drawable_width, drawable_height);
  return Render::ValidationResult::Success();
}

void OgreNextDemoInProcessFramePolicy::EndCapture() noexcept {
  capture_scope_.reset();
}

Render::ValidationResult
OgreNextDemoInProcessFramePolicy::NormalizeAndValidate(
    Render::GraphicsSceneFrameInput &frame,
    std::uint32_t drawable_width, std::uint32_t drawable_height) {
  Render::ValidationResult result = NormalizeOgreNextDemoCamera(
      frame.camera, drawable_width, drawable_height);
  if (result) {
    result = ValidateOgreNextDemoShadowLights(frame.lights);
  }
  return result;
}

} // namespace RoR::Detail
