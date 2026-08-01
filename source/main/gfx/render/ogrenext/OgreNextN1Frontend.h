/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Isolated Ogre-Next N1 static-PBR offscreen frontend.

#pragma once

#include "../RendererFrontend.h"

#include <memory>
#include <string>

namespace RoR::Render {

/// Runtime-owned Ogre shader media. The root is an absolute UTF-8 path containing
/// the pinned `Hlms` directory; packaging code resolves its own relative
/// resource layout before constructing the frontend.
struct OgreNextN1Configuration final {
  std::string shader_media_root;
};

/// First production adapter behind the renderer-neutral boundary.
///
/// Ogre headers and native objects are confined to the private implementation.
/// This class must only be built in the standalone Ogre-Next target; it must
/// never be linked into the OGRE 1.14 RoR executable.
class OgreNextN1Frontend final : public IRendererFrontend {
public:
  explicit OgreNextN1Frontend(OgreNextN1Configuration configuration);
  ~OgreNextN1Frontend() override;

  OgreNextN1Frontend(const OgreNextN1Frontend &) = delete;
  OgreNextN1Frontend &operator=(const OgreNextN1Frontend &) = delete;
  OgreNextN1Frontend(OgreNextN1Frontend &&) = delete;
  OgreNextN1Frontend &operator=(OgreNextN1Frontend &&) = delete;

  [[nodiscard]] FrontendCapabilityReport QueryCapabilities() const override;
  RenderOperationResult
  Initialize(const FrontendInitializationRequest &request) override;
  RenderOperationResult
  UpdateSurface(const FrontendSurfaceUpdate &update, bool headless,
                std::uint64_t timeout_nanoseconds) override;
  RenderOperationResult
  SynchronizeAssets(const RenderAssetDelta &delta) override;
  RenderOperationResult ReleaseResource(ResourceHandle resource) override;
  RenderOperationResult Render(const RenderFrameRequest &request,
                               RenderFrameOutput &output) override;
  [[nodiscard]] bool
  IsFrameComplete(std::uint64_t frame_id) const noexcept override;
  RenderOperationResult
  WaitForFrame(std::uint64_t frame_id,
               std::uint64_t timeout_nanoseconds) override;
  [[nodiscard]] NativeRenderInterop *GetNativeInterop() noexcept override;
  RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
