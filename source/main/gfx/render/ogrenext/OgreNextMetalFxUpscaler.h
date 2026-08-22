/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Compile-isolated MetalFX temporal upscaling bridge.
///
/// Stage 5, second deliverable. MTLFXTemporalScaler is the Apple analogue of
/// DLSS/FSR: it consumes the same temporal inputs the RoR TAA node already
/// produces - a jittered lower-resolution linear HDR colour, the matching
/// depth, per-pixel motion vectors, and the exact sub-pixel jitter - and
/// resolves them into a higher-resolution image. Because the scaler performs
/// its own temporal accumulation it REPLACES the RoR TAA resolve rather than
/// stacking with it; both consume the identical jitter/velocity groundwork.
///
/// This header is renderer-neutral on purpose: no Objective-C, no Metal, and
/// no Ogre types cross it. Ogre textures are handed over as opaque
/// `Ogre::TextureGpu *` addresses which only the Apple ObjC++ target may
/// decode, exactly like OgreNextN1NativeInterop's image bindings.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace RoR::Render {

/// Shipping tiers. The scale is applied to BOTH axes, so the area ratio is
/// the square of the linear scale: quality renders ~1.5x fewer pixels,
/// performance ~2x fewer, at a matched output resolution.
enum class OgreNextMetalFxTier : std::uint8_t {
  /// No scaler. The scene renders at the output resolution and the RoR TAA
  /// resolve owns temporal accumulation.
  NATIVE = 0,
  /// Linear 0.8165 => 0.667 of the pixels (~1.5x area).
  QUALITY = 1,
  /// Linear 0.7071 => 0.500 of the pixels (~2x area).
  PERFORMANCE = 2,
};

/// ROR_METALFX = off|native|quality|performance|quality (default: off).
/// The knob is read once per process.
[[nodiscard]] OgreNextMetalFxTier OgreNextMetalFxRequestedTier() noexcept;

/// Linear per-axis render-scale for a tier. NATIVE is exactly 1.
[[nodiscard]] float OgreNextMetalFxTierScale(
    OgreNextMetalFxTier tier) noexcept;

/// Stable lowercase identity for logs and the frame signature.
[[nodiscard]] const char *OgreNextMetalFxTierName(
    OgreNextMetalFxTier tier) noexcept;

/// Everything one scaler invocation needs. Textures are
/// `reinterpret_cast<std::uintptr_t>(Ogre::TextureGpu *)`.
struct OgreNextMetalFxFrameRequest final {
  /// Jittered lower-resolution linear HDR radiance (RGBA16F).
  std::uintptr_t colour_texture = 0U;
  /// Lower-resolution non-reversed [0, 1] depth (D32/R32F),
  /// kOgreNextTaaDepthConvention.
  std::uintptr_t depth_texture = 0U;
  /// Lower-resolution RG16F motion in input pixels, jitter removed,
  /// kOgreNextTaaMotionVectorConvention (previous minus current, +X right,
  /// +Y down).
  std::uintptr_t motion_texture = 0U;
  /// Lower-resolution R32F per-pixel reactive coverage in [0, 1]. 0 keeps the
  /// scaler's normal history behaviour and 1 rejects history for that pixel -
  /// exactly the RoR TAA resolve's convention.
  std::uintptr_t reactive_texture = 0U;
  /// Full-resolution RGBA16F destination. Must be an Ogre UAV texture so its
  /// Metal usage carries MTLTextureUsageShaderWrite.
  std::uintptr_t output_texture = 0U;
  /// This frame's sub-pixel jitter in input pixels, +X right and +Y down,
  /// exactly the offset applied to the render camera's projection.
  float jitter_pixels_x = 0.0F;
  float jitter_pixels_y = 0.0F;
  /// Scale already applied to the linear HDR colour. The RoR split writes
  /// raw scene-referred HDR, so this is 1 today.
  float pre_exposure = 1.0F;
  /// Discards the scaler's temporal history for this frame (camera cut,
  /// extent change, first frame, or any upstream degrade).
  bool reset_history = false;
};

/// One live MTLFXTemporalScaler bound to a fixed input/output extent pair.
class OgreNextMetalFxUpscaler {
public:
  virtual ~OgreNextMetalFxUpscaler() = default;

  /// Encodes the scaler into the render system's current command buffer.
  /// Returns false and fills `failure_reason` without throwing; the caller
  /// degrades that frame to the un-upscaled path.
  [[nodiscard]] virtual bool Encode(
      const OgreNextMetalFxFrameRequest &request,
      std::string &failure_reason) = 0;

  [[nodiscard]] virtual std::uint32_t input_width() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t input_height() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t output_width() const noexcept = 0;
  [[nodiscard]] virtual std::uint32_t output_height() const noexcept = 0;
};

/// Creates a scaler from a live Ogre Metal render system. Returns nullptr and
/// fills `failure_reason` when MetalFX is unavailable for any reason - old
/// macOS, unsupported device, a rejected extent pair, or a descriptor the
/// runtime refuses. Never throws.
[[nodiscard]] std::unique_ptr<OgreNextMetalFxUpscaler>
CreateOgreNextMetalFxUpscaler(std::uintptr_t ogre_render_system,
                              std::uint32_t input_width,
                              std::uint32_t input_height,
                              std::uint32_t output_width,
                              std::uint32_t output_height,
                              std::string &failure_reason);

} // namespace RoR::Render
