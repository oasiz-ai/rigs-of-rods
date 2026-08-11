/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererFrontendPresentationPolicy.h"

namespace RoR::Render {
namespace {

constexpr std::uint32_t kKnownFrameOutputBits =
    static_cast<std::uint32_t>(FrameOutputMask::COLOR) |
    static_cast<std::uint32_t>(FrameOutputMask::DEPTH) |
    static_cast<std::uint32_t>(FrameOutputMask::MOTION_VECTORS) |
    static_cast<std::uint32_t>(FrameOutputMask::OBJECT_ID) |
    static_cast<std::uint32_t>(FrameOutputMask::SURFACE_NORMAL) |
    static_cast<std::uint32_t>(FrameOutputMask::MATERIAL_ID);

} // namespace

ValidationResult ValidateRendererFrontendPresentationPolicy(
    const RendererFrontendPresentationPolicy &policy) {
  if (policy.version != kRendererFrontendPresentationPolicyVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported frontend presentation policy version");
  }
  const std::uint32_t outputs =
      static_cast<std::uint32_t>(policy.requested_outputs);
  if (outputs == 0U || (outputs & ~kKnownFrameOutputBits) != 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_OUTPUT_MASK, "requested_outputs",
        "presentation policy requires only known, nonzero outputs");
  }
  if (policy.color_format != PixelFormat::RGBA8_SRGB &&
      policy.color_format != PixelFormat::RGBA16_FLOAT) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "color_format",
        "presentation policy color must be SDR sRGB or linear HDR");
  }
  if (policy.retire_scene_without_render &&
      (policy.present || policy.presentation_surface_revision != 0U ||
       policy.presentation_drawable_width != 0U ||
       policy.presentation_drawable_height != 0U ||
       policy.retire_scene_on_presentation_extent_mismatch)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "retire_scene_without_render",
        "retired scene policy cannot name or present a native surface");
  }
  if (policy.present) {
    if (!HasFrameOutput(policy.requested_outputs, FrameOutputMask::COLOR)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_OUTPUT_MASK, "requested_outputs",
          "native presentation requires a color output");
    }
    if (policy.presentation_surface_revision == 0U) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER,
          "presentation_surface_revision",
          "native presentation requires the active surface revision");
    }
    if (policy.retire_scene_on_presentation_extent_mismatch &&
        (policy.presentation_drawable_width == 0U ||
         policy.presentation_drawable_height == 0U)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS,
          "presentation_drawable_extent",
          "extent-guarded presentation requires a nonzero drawable extent");
    }
    if (!policy.retire_scene_on_presentation_extent_mismatch &&
        (policy.presentation_drawable_width != 0U ||
         policy.presentation_drawable_height != 0U)) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_DIMENSIONS,
          "presentation_drawable_extent",
          "presentation drawable extent requires the stale-scene guard");
    }
  } else if (policy.presentation_surface_revision != 0U ||
             policy.presentation_drawable_width != 0U ||
             policy.presentation_drawable_height != 0U ||
             policy.retire_scene_on_presentation_extent_mismatch) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER,
        "presentation_surface_revision",
        "UI-free offscreen rendering cannot name a presentation surface or extent");
  }
  return ValidationResult::Success();
}

} // namespace RoR::Render
