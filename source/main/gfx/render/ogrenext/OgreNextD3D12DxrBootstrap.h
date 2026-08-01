/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#pragma once

#include "OgreNextDxr7Contract.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Ogre {
class RenderSystem;
}

namespace RoR::Render {

enum class Dxr7BootstrapCode : std::uint8_t {
  READY = 0,
  UNSUPPORTED,
  FAILURE,
};

struct Dxr7BootstrapResult {
  Dxr7BootstrapCode code = Dxr7BootstrapCode::FAILURE;
  std::string message;

  bool ready() const noexcept { return code == Dxr7BootstrapCode::READY; }
  bool unsupported() const noexcept {
    return code == Dxr7BootstrapCode::UNSUPPORTED;
  }
};

struct Dxr7BootstrapEvidence {
  Dxr7CandidateContract candidate;
  Dxr7CandidateDecision candidate_decision =
      Dxr7CandidateDecision::NO_HARDWARE_ADAPTER;
  std::string adapter_name;
  std::string adapter_luid;
  std::uint32_t vendor_id = 0U;
  std::uint32_t device_id = 0U;
  std::uint32_t d3d12_feature_level = 0U;
  std::uint32_t d3d11_feature_level = 0U;
  bool software_adapter = false;
  bool app_owned_d3d12_device = false;
  bool app_owned_direct_queue = false;
  bool app_owned_fence = false;
  bool d3d11on12_device_created = false;
  bool d3d11on12_created_with_exact_direct_queue = false;
  bool d3d11on12_underlying_d3d12_device_exact = false;
  bool d3d11on12_adapter_luid_exact = false;
  bool ogre_external_device_option_used = false;
  bool ogre_d3d11_device_exact = false;
  bool ogre_external_device_active = false;
  bool ogre_native_window_created = false;
  bool ogre_pbs_material_created = false;
  bool ogre_compositor_workspace_created = false;
  bool ogre_frame_submitted = false;
  bool ogre_frame_readback_completed = false;
  bool ogre_frame_nonblank = false;
  bool ogre_frame_ui_free = false;
  bool ogre_frame_resources_destroyed = false;
  Dxr7OgreTeardownContract ogre_teardown;
  std::uint32_t ogre_frame_width = 0U;
  std::uint32_t ogre_frame_height = 0U;
  std::uint32_t ogre_frame_distinct_pixels = 0U;
  std::uint32_t ogre_frame_non_background_pixels = 0U;
  std::uint64_t ogre_frame_fnv1a64 = 0U;
  bool blas_built = false;
  bool tlas_built = false;
  bool state_object_created = false;
  bool shader_identifiers_resolved = false;
  bool dispatch_rays_called = false;
  std::uint32_t dispatch_width = 0U;
  std::uint32_t dispatch_height = 0U;
  std::uint32_t dispatch_depth = 0U;
  std::uint32_t readback_value = 0U;
  bool closest_hit_readback_exact = false;
  std::uint64_t fence_before_dispatch = 0U;
  std::uint64_t fence_after_dispatch = 0U;
  std::uint64_t fence_after_ogre = 0U;
  bool ogre_shutdown_before_d3d11_release = false;
  bool d3d11_context_flushed_before_release = false;
  bool d3d11_released_before_d3d12_queue = false;
  bool d3d12_queue_released_before_device = false;
  bool shutdown_completed = false;
};

class OgreNextD3D12DxrBootstrap final {
 public:
  OgreNextD3D12DxrBootstrap();
  ~OgreNextD3D12DxrBootstrap();

  OgreNextD3D12DxrBootstrap(const OgreNextD3D12DxrBootstrap&) = delete;
  OgreNextD3D12DxrBootstrap& operator=(
      const OgreNextD3D12DxrBootstrap&) = delete;

  Dxr7BootstrapResult Initialize();
  Dxr7BootstrapResult ProveFenceBeforeDispatch();
  Dxr7BootstrapResult DispatchProbe(
      const std::filesystem::path& dxil_library);

  std::uintptr_t external_d3d11_device_address() const noexcept;
  Dxr7BootstrapResult MarkOgreAttached() noexcept;
  Dxr7BootstrapResult VerifyOgreAdoption(
      Ogre::RenderSystem* render_system) noexcept;
  Dxr7BootstrapResult RecordOgreFrameProof(
      std::uint32_t width, std::uint32_t height,
      std::uint32_t distinct_pixels,
      std::uint32_t non_background_pixels,
      std::uint64_t fnv1a64, bool ui_free,
      const Dxr7OgreTeardownContract& teardown) noexcept;
  Dxr7BootstrapResult MarkOgreDetached() noexcept;
  Dxr7BootstrapResult ProveFenceAfterOgre();

  Dxr7BootstrapResult Shutdown() noexcept;
  Dxr7BootstrapResult AbortAfterFailure() noexcept;

  const Dxr7BootstrapEvidence& evidence() const noexcept;
  Dxr7PassContract pass_contract() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace RoR::Render
