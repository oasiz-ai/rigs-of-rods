/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief MTLFXTemporalScaler bridge for the Ogre-Next Metal frontend.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "OgreNextMetalFxUpscaler.h"

#include "OgreMetalDevice.h"
#include "OgreMetalRenderSystem.h"
#include "OgreMetalTextureGpu.h"
#include "OgreTextureGpu.h"

#include <cstdlib>
#include <string>

// MetalFX arrived in macOS 13. The project's deployment target is older, so
// the framework is weak-linked and every entry point is guarded by an
// @available check plus a nil-class probe: on an older system the symbols
// resolve to nil rather than aborting the process at load time.
#if defined(__has_include)
#if __has_include(<MetalFX/MetalFX.h>)
#import <MetalFX/MetalFX.h>
#define ROR_METALFX_SDK_AVAILABLE 1
#endif
#endif

namespace RoR::Render {

namespace {

/// Linear per-axis scales. Squared, these are the advertised area ratios:
/// 0.8165^2 = 0.667 (~1.5x) and 0.7071^2 = 0.500 (~2x).
constexpr float kOgreNextMetalFxQualityScale = 0.816497F;
constexpr float kOgreNextMetalFxPerformanceScale = 0.707107F;

/// Reads the documented Apple jitter convention unless explicitly overridden.
/// A wrong jitter sign is the one MetalFX integration error that produces a
/// plausible-but-soft image rather than an obvious failure, so the knob exists
/// to A/B it in a live session without a rebuild.
[[nodiscard]] bool JitterSignInverted() noexcept {
  static const bool inverted = [] {
    const char *raw = std::getenv("ROR_METALFX_JITTER_INVERT");
    return raw != nullptr && std::string(raw) == "1";
  }();
  return inverted;
}

[[nodiscard]] Ogre::MetalTextureGpu *DecodeTexture(std::uintptr_t handle) {
  if (handle == 0U) {
    return nullptr;
  }
  auto *texture = reinterpret_cast<Ogre::TextureGpu *>(handle);
  return dynamic_cast<Ogre::MetalTextureGpu *>(texture);
}

/// Resolves an Ogre texture to its live MTLTexture and independently verifies
/// the extent the caller declared. Returns nil on any disagreement.
id<MTLTexture> ResolveMetalTexture(std::uintptr_t handle,
                                   std::uint32_t expected_width,
                                   std::uint32_t expected_height,
                                   const char *role,
                                   std::string &failure_reason) {
  Ogre::MetalTextureGpu *const ogre_texture = DecodeTexture(handle);
  if (ogre_texture == nullptr) {
    failure_reason = std::string(role) + " is not a live Metal texture";
    return nil;
  }
  if (ogre_texture->getWidth() != expected_width ||
      ogre_texture->getHeight() != expected_height ||
      ogre_texture->getNumSlices() != 1U ||
      ogre_texture->getSampleDescription().getColourSamples() != 1U) {
    failure_reason = std::string(role) + " extent or sample count disagrees";
    return nil;
  }
  id<MTLTexture> texture = ogre_texture->getFinalTextureName();
  if (texture == nil) {
    failure_reason = std::string(role) + " has no resident Metal texture";
    return nil;
  }
  if (texture.width != expected_width || texture.height != expected_height) {
    failure_reason = std::string(role) + " native extent disagrees";
    return nil;
  }
  return texture;
}

#if defined(ROR_METALFX_SDK_AVAILABLE)

/// Every MetalFX input carries a required MTLTextureUsage the scaler reports
/// after construction. Ogre picks its own usage flags, so the binding is
/// verified rather than assumed: a missing bit degrades the frame instead of
/// handing Metal an invalid encode.
bool VerifyUsage(id<MTLTexture> texture, MTLTextureUsage required,
                 const char *role, std::string &failure_reason) {
  if ((texture.usage & required) != required) {
    failure_reason = std::string(role) + " usage 0x" +
                     std::to_string(static_cast<unsigned long>(texture.usage)) +
                     " lacks required 0x" +
                     std::to_string(static_cast<unsigned long>(required));
    return false;
  }
  return true;
}

class MetalFxUpscaler final : public OgreNextMetalFxUpscaler {
public:
  MetalFxUpscaler(Ogre::MetalDevice *device, std::uint32_t input_width,
                  std::uint32_t input_height, std::uint32_t output_width,
                  std::uint32_t output_height)
      : device_(device), input_width_(input_width), input_height_(input_height),
        output_width_(output_width), output_height_(output_height) {}

  ~MetalFxUpscaler() override {
    if (@available(macOS 13.0, *)) {
      scaler_ = nil;
    }
  }

  [[nodiscard]] bool Encode(const OgreNextMetalFxFrameRequest &request,
                            std::string &failure_reason) override {
    if (@available(macOS 13.0, *)) {
      return EncodeAvailable(request, failure_reason);
    }
    failure_reason = "MetalFX requires macOS 13 or newer";
    return false;
  }

  [[nodiscard]] std::uint32_t input_width() const noexcept override {
    return input_width_;
  }
  [[nodiscard]] std::uint32_t input_height() const noexcept override {
    return input_height_;
  }
  [[nodiscard]] std::uint32_t output_width() const noexcept override {
    return output_width_;
  }
  [[nodiscard]] std::uint32_t output_height() const noexcept override {
    return output_height_;
  }

private:
  bool EncodeAvailable(const OgreNextMetalFxFrameRequest &request,
                       std::string &failure_reason) API_AVAILABLE(macos(13.0)) {
    id<MTLTexture> colour = ResolveMetalTexture(
        request.colour_texture, input_width_, input_height_, "colour",
        failure_reason);
    if (colour == nil) {
      return false;
    }
    id<MTLTexture> depth = ResolveMetalTexture(
        request.depth_texture, input_width_, input_height_, "depth",
        failure_reason);
    if (depth == nil) {
      return false;
    }
    id<MTLTexture> motion = ResolveMetalTexture(
        request.motion_texture, input_width_, input_height_, "motion",
        failure_reason);
    if (motion == nil) {
      return false;
    }
    id<MTLTexture> reactive = ResolveMetalTexture(
        request.reactive_texture, input_width_, input_height_, "reactive",
        failure_reason);
    if (reactive == nil) {
      return false;
    }
    id<MTLTexture> output = ResolveMetalTexture(
        request.output_texture, output_width_, output_height_, "output",
        failure_reason);
    if (output == nil) {
      return false;
    }

    if (!EnsureScaler(colour.pixelFormat, depth.pixelFormat,
                      motion.pixelFormat, reactive.pixelFormat,
                      output.pixelFormat, failure_reason)) {
      return false;
    }

    if (!VerifyUsage(colour, scaler_.colorTextureUsage, "colour",
                     failure_reason) ||
        !VerifyUsage(depth, scaler_.depthTextureUsage, "depth",
                     failure_reason) ||
        !VerifyUsage(motion, scaler_.motionTextureUsage, "motion",
                     failure_reason) ||
        !VerifyUsage(reactive, scaler_.reactiveTextureUsage, "reactive",
                     failure_reason) ||
        !VerifyUsage(output, scaler_.outputTextureUsage, "output",
                     failure_reason)) {
      return false;
    }

    id<MTLCommandBuffer> command_buffer = device_->mCurrentCommandBuffer;
    if (command_buffer == nil) {
      failure_reason = "Metal device has no current command buffer";
      return false;
    }

    scaler_.colorTexture = colour;
    scaler_.depthTexture = depth;
    scaler_.motionTexture = motion;
    scaler_.reactiveMaskTexture = reactive;
    scaler_.outputTexture = output;
    scaler_.inputContentWidth = input_width_;
    scaler_.inputContentHeight = input_height_;
    // The RoR depth convention is portable non-reversed [0, 1].
    scaler_.depthReversed = NO;
    // Motion is authored in input pixels with the same current-to-previous
    // direction MetalFX expects, so no rescale is applied.
    scaler_.motionVectorScaleX = 1.0F;
    scaler_.motionVectorScaleY = 1.0F;
    const float sign = JitterSignInverted() ? -1.0F : 1.0F;
    // Apple's convention is the negated projection offset. The RoR jitter is
    // +Y down while Metal clip space is +Y up, so Y arrives already negated
    // relative to X and the two signs differ here by construction.
    scaler_.jitterOffsetX = sign * -request.jitter_pixels_x;
    scaler_.jitterOffsetY = sign * request.jitter_pixels_y;
    scaler_.preExposure = request.pre_exposure;
    scaler_.reset = request.reset_history ? YES : NO;

    // Ogre owns an open render/blit/compute encoder at this point. MetalFX
    // installs its own encoders on the same command buffer, so Ogre's must be
    // closed first or Metal raises a validation abort.
    device_->endAllEncoders();
    [scaler_ encodeToCommandBuffer:command_buffer];
    return true;
  }

  bool EnsureScaler(MTLPixelFormat colour_format, MTLPixelFormat depth_format,
                    MTLPixelFormat motion_format,
                    MTLPixelFormat reactive_format,
                    MTLPixelFormat output_format,
                    std::string &failure_reason) API_AVAILABLE(macos(13.0)) {
    if (scaler_ != nil && colour_format == colour_format_ &&
        depth_format == depth_format_ && motion_format == motion_format_ &&
        reactive_format == reactive_format_ &&
        output_format == output_format_) {
      return true;
    }
    scaler_ = nil;
    MTLFXTemporalScalerDescriptor *descriptor =
        [[MTLFXTemporalScalerDescriptor alloc] init];
    descriptor.colorTextureFormat = colour_format;
    descriptor.depthTextureFormat = depth_format;
    descriptor.motionTextureFormat = motion_format;
    descriptor.outputTextureFormat = output_format;
    // The reactive mask is the per-pixel history-rejection hook the RoR TAA
    // contract already declares as consumed, so it is required rather than
    // optional: a runtime that refuses it must fail the tier, not silently
    // drop the guarantee.
    if (@available(macOS 14.4, *)) {
      descriptor.reactiveMaskTextureEnabled = YES;
      descriptor.reactiveMaskTextureFormat = reactive_format;
    } else {
      failure_reason = "MetalFX reactive masks require macOS 14.4 or newer";
      return false;
    }
    descriptor.inputWidth = input_width_;
    descriptor.inputHeight = input_height_;
    descriptor.outputWidth = output_width_;
    descriptor.outputHeight = output_height_;
    // The RoR HDR split writes raw scene-referred radiance and the stock HDR
    // metering downstream owns exposure entirely, so the scaler is told not to
    // evaluate exposure itself. NOTE: toggling this made no measured
    // difference to the known exposure regression under upscaling (scene mean
    // sRGB 0.0354 with YES vs 0.0355 with NO, against 0.2480 at the NATIVE
    // tier), so the crush is NOT the scaler's exposure handling - see the
    // outstanding metering-hazard investigation.
    descriptor.autoExposureEnabled = NO;
    descriptor.requiresSynchronousInitialization = YES;
    scaler_ = [descriptor newTemporalScalerWithDevice:device_->mDevice];
    if (scaler_ == nil) {
      failure_reason = "MTLFXTemporalScaler construction was refused";
      return false;
    }
    colour_format_ = colour_format;
    depth_format_ = depth_format;
    motion_format_ = motion_format;
    reactive_format_ = reactive_format;
    output_format_ = output_format;
    return true;
  }

  Ogre::MetalDevice *device_ = nullptr;
  std::uint32_t input_width_ = 0U;
  std::uint32_t input_height_ = 0U;
  std::uint32_t output_width_ = 0U;
  std::uint32_t output_height_ = 0U;
  id<MTLFXTemporalScaler> scaler_ API_AVAILABLE(macos(13.0)) = nil;
  MTLPixelFormat colour_format_ = MTLPixelFormatInvalid;
  MTLPixelFormat depth_format_ = MTLPixelFormatInvalid;
  MTLPixelFormat motion_format_ = MTLPixelFormatInvalid;
  MTLPixelFormat reactive_format_ = MTLPixelFormatInvalid;
  MTLPixelFormat output_format_ = MTLPixelFormatInvalid;
};

#endif // ROR_METALFX_SDK_AVAILABLE

} // namespace

OgreNextMetalFxTier OgreNextMetalFxRequestedTier() noexcept {
  static const OgreNextMetalFxTier tier = [] {
    const char *raw = std::getenv("ROR_METALFX");
    if (raw == nullptr) {
      return OgreNextMetalFxTier::NATIVE;
    }
    const std::string value(raw);
    if (value == "quality") {
      return OgreNextMetalFxTier::QUALITY;
    }
    if (value == "performance") {
      return OgreNextMetalFxTier::PERFORMANCE;
    }
    return OgreNextMetalFxTier::NATIVE;
  }();
  return tier;
}

float OgreNextMetalFxTierScale(OgreNextMetalFxTier tier) noexcept {
  switch (tier) {
  case OgreNextMetalFxTier::QUALITY:
    return kOgreNextMetalFxQualityScale;
  case OgreNextMetalFxTier::PERFORMANCE:
    return kOgreNextMetalFxPerformanceScale;
  case OgreNextMetalFxTier::NATIVE:
    break;
  }
  return 1.0F;
}

const char *OgreNextMetalFxTierName(OgreNextMetalFxTier tier) noexcept {
  switch (tier) {
  case OgreNextMetalFxTier::QUALITY:
    return "quality";
  case OgreNextMetalFxTier::PERFORMANCE:
    return "performance";
  case OgreNextMetalFxTier::NATIVE:
    break;
  }
  return "native";
}

std::unique_ptr<OgreNextMetalFxUpscaler> CreateOgreNextMetalFxUpscaler(
    std::uintptr_t ogre_render_system, std::uint32_t input_width,
    std::uint32_t input_height, std::uint32_t output_width,
    std::uint32_t output_height, std::string &failure_reason) {
#if !defined(ROR_METALFX_SDK_AVAILABLE)
  (void)ogre_render_system;
  (void)input_width;
  (void)input_height;
  (void)output_width;
  (void)output_height;
  failure_reason = "this build has no MetalFX SDK";
  return nullptr;
#else
  if (@available(macOS 13.0, *)) {
    if (ogre_render_system == 0U || input_width == 0U || input_height == 0U ||
        output_width < input_width || output_height < input_height) {
      failure_reason = "MetalFX extent pair is not a valid upscale";
      return nullptr;
    }
    auto *render_system =
        reinterpret_cast<Ogre::MetalRenderSystem *>(ogre_render_system);
    Ogre::MetalDevice *device = render_system->getActiveDevice();
    if (device == nullptr || device->mDevice == nil) {
      failure_reason = "Ogre Metal device is not live";
      return nullptr;
    }
    if (![MTLFXTemporalScalerDescriptor supportsDevice:device->mDevice]) {
      failure_reason = "MetalFX temporal scaling is unsupported on this device";
      return nullptr;
    }
    return std::make_unique<MetalFxUpscaler>(device, input_width, input_height,
                                             output_width, output_height);
  }
  (void)input_width;
  (void)input_height;
  (void)output_width;
  (void)output_height;
  failure_reason = "MetalFX requires macOS 13 or newer";
  return nullptr;
#endif
}

} // namespace RoR::Render
