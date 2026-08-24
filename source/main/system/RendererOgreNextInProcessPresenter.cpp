/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "RendererOgreNextInProcessPresenter.h"

#include "OgreNextN1Frontend.h"
#include "OgreNextReflectionProbeRuntime.h"
#include "RendererOgreNextSdlWindowRuntime.h"

#if defined(__APPLE__)
#include "OgreNextMetalRayTracingBackend.h"
#include "OgreNextN1NativeInterop.h"
#endif

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace RoR {
namespace {

using namespace Render;

constexpr std::uint64_t kInitialSurfaceRevision = 1U;

RendererOgreNextWindowPlatform HostWindowPlatform() noexcept {
#if defined(__APPLE__)
  return RendererOgreNextWindowPlatform::MACOS_COCOA_METAL;
#elif defined(_WIN32)
  return RendererOgreNextWindowPlatform::WINDOWS_WIN32;
#elif defined(__linux__)
  return RendererOgreNextWindowPlatform::LINUX_X11_XCB;
#else
  return RendererOgreNextWindowPlatform::UNKNOWN;
#endif
}

NativeWindowHandle MakeWindowHandle(
    const RendererOgreNextSdlNativeWindow &native) noexcept {
  NativeWindowHandle result;
  result.generation = 1U;
#if defined(__APPLE__)
  result.system = NativeWindowSystem::COCOA;
  result.surface =
      reinterpret_cast<std::uintptr_t>(native.native_render_view);
#elif defined(_WIN32)
  result.system = NativeWindowSystem::WINDOWS;
  result.surface = native.native_window;
#elif defined(__linux__)
  result.system = NativeWindowSystem::X11;
  result.connection =
      reinterpret_cast<std::uintptr_t>(native.native_display);
  result.surface = native.native_window;
#endif
  return result;
}

Float2 ContentScale(const RendererOgreNextWindowMetrics &metrics) noexcept {
  return {static_cast<float>(metrics.content_scale_x),
          static_cast<float>(metrics.content_scale_y)};
}

FrontendSurfaceUpdate MakeSurface(
    const NativeWindowHandle &window,
    const RendererOgreNextWindowMetrics &metrics,
    std::uint64_t revision, bool suspended) noexcept {
  FrontendSurfaceUpdate update;
  update.surface_revision = revision;
  update.window = window;
  update.pixel_width = suspended ? 0U : metrics.drawable_width;
  update.pixel_height = suspended ? 0U : metrics.drawable_height;
  update.content_scale = ContentScale(metrics);
  update.suspended = suspended;
  return update;
}

bool SameSurface(const FrontendSurfaceUpdate &left,
                 const FrontendSurfaceUpdate &right) noexcept {
  return left.surface_revision == right.surface_revision &&
         left.window.system == right.window.system &&
         left.window.connection == right.window.connection &&
         left.window.surface == right.window.surface &&
         left.window.generation == right.window.generation &&
         left.pixel_width == right.pixel_width &&
         left.pixel_height == right.pixel_height &&
         left.content_scale == right.content_scale &&
         left.suspended == right.suspended;
}

template <std::size_t DestinationCapacity, std::size_t SourceCapacity>
bool CopyParameters(
    const std::array<RendererOgreNextWindowParameter, SourceCapacity> &source,
    std::size_t source_count,
    std::array<OgreNextN1PresentationParameter, DestinationCapacity>
        &destination,
    std::size_t &destination_count) {
  if (source_count > source.size() || source_count > destination.size()) {
    return false;
  }
  destination_count = source_count;
  for (std::size_t index = 0U; index < source_count; ++index) {
    destination[index].name = source[index].name;
    destination[index].value = source[index].value;
  }
  return true;
}

template <typename Value>
void SetPressed(std::vector<Value> &pressed, Value value, bool down) {
  const auto position = std::lower_bound(pressed.begin(), pressed.end(), value);
  if (down && (position == pressed.end() || *position != value)) {
    pressed.insert(position, value);
  } else if (!down && position != pressed.end() && *position == value) {
    pressed.erase(position);
  }
}

std::int32_t SaturatingAdd(std::int32_t left,
                           std::int32_t right) noexcept {
  const std::int64_t sum = static_cast<std::int64_t>(left) + right;
  return static_cast<std::int32_t>((std::max)(
      static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)()),
      (std::min)(
          static_cast<std::int64_t>(
              (std::numeric_limits<std::int32_t>::max)()),
          sum)));
}

std::int32_t ScaledPixels(int logical, double scale) noexcept {
  const double value = static_cast<double>(logical) * scale;
  if (!std::isfinite(value)) {
    return 0;
  }
  if (value <=
      static_cast<double>((std::numeric_limits<std::int32_t>::min)())) {
    return (std::numeric_limits<std::int32_t>::min)();
  }
  if (value >=
      static_cast<double>((std::numeric_limits<std::int32_t>::max)())) {
    return (std::numeric_limits<std::int32_t>::max)();
  }
  return static_cast<std::int32_t>(std::llround(value));
}

float SaturatingWheel(double value) noexcept {
  constexpr double kMaximum = 8192.0;
  if (!std::isfinite(value)) {
    return 0.0F;
  }
  return static_cast<float>((std::max)(-kMaximum,
                                        (std::min)(kMaximum, value)));
}

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

bool IsKnownPollPoint(RendererInProcessEventPollPoint point) noexcept {
  switch (point) {
  case RendererInProcessEventPollPoint::BEFORE_SIMULATION:
  case RendererInProcessEventPollPoint::BEFORE_PRESENT:
    return true;
  }
  return false;
}

#if defined(__APPLE__)

std::uint64_t StableSunVisibilityMeshId(RenderAssetId id) noexcept {
  constexpr std::uint64_t kOffset = UINT64_C(14695981039346656037);
  constexpr std::uint64_t kPrime = UINT64_C(1099511628211);
  std::uint64_t hash = kOffset;
  const auto append = [&](std::uint64_t word) {
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
      hash ^= (word >> (byte * 8U)) & UINT64_C(0xff);
      hash *= kPrime;
    }
  };
  append(id.high());
  append(id.low());
  return hash == 0U ? kOffset : hash;
}

ValidationResult BuildSunVisibilitySelection(
    const SceneSnapshot &snapshot, const RenderAssetRegistry &registry,
    const CameraViewRequest &view,
    std::vector<NativeSunVisibilityV2InstanceSelection> &output) {
  std::uint32_t directional_lights = 0U;
  bool shadow_enabled_sun = false;
  for (const LightDescriptor &light : snapshot.lights()) {
    if (light.type == LightType::DIRECTIONAL) {
      ++directional_lights;
      shadow_enabled_sun = shadow_enabled_sun || light.shadow_flags != 0U;
    }
  }
  if (directional_lights != 1U || !shadow_enabled_sun) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "sun_visibility_v2.directional_sun",
        "production V2 requires exactly one shadow-enabled directional sun");
  }
  if (snapshot.mesh_instances().empty() ||
      snapshot.mesh_instances().size() >
          kNativeSunVisibilityV2MaximumSelectedInstances) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "sun_visibility_v2.instances",
        "production V2 requires 1..256 explicitly classified instances");
  }

  std::vector<NativeSunVisibilityV2InstanceSelection> candidate;
  std::map<std::uint64_t, RenderAssetId> mesh_id_owners;
  try {
    candidate.reserve(snapshot.mesh_instances().size());
  } catch (...) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "sun_visibility_v2.instances",
        "production V2 could not reserve its bounded selection");
  }
  for (const MeshInstanceDescriptor &instance : snapshot.mesh_instances()) {
    const MeshResourceDescriptor *const mesh =
        registry.ResolveMesh(instance.mesh);
    const MaterialDescriptor *const material =
        registry.ResolveMaterial(instance.material);
    if (mesh == nullptr || material == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE,
          "sun_visibility_v2.instance_asset",
          "production V2 could not resolve an instance mesh or material");
    }
    const std::uint64_t mesh_id = StableSunVisibilityMeshId(instance.mesh.id);
    const auto owner = mesh_id_owners.find(mesh_id);
    if (owner != mesh_id_owners.end() && owner->second != instance.mesh.id) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER,
          "sun_visibility_v2.mesh_id",
          "stable mesh identity hash collision in bounded V2 selection");
    }
    mesh_id_owners.emplace(mesh_id, instance.mesh.id);

    NativeSunVisibilityV2InstanceSelection selected;
    selected.instance_id = instance.instance_id;
    selected.mesh_id = mesh_id;
    const bool visible =
        (instance.visibility_mask & view.visibility_mask) != 0U;
    const bool casts =
        (instance.flags & MESH_INSTANCE_CASTS_SHADOW) != 0U;
    const bool receives =
        (instance.flags & MESH_INSTANCE_RECEIVES_SHADOW) != 0U;
    if (!visible || material->model != MaterialModel::PBR_METALLIC_ROUGHNESS ||
        (!casts && !receives)) {
      selected.flags = NATIVE_SUN_VISIBILITY_V2_RT_INERT;
    } else if (material->alpha_test_mode !=
               MaterialAlphaTestMode::DISABLED) {
      selected.flags = NATIVE_SUN_VISIBILITY_V2_ALPHA_LAYER;
    } else if (material->blend_mode != MaterialBlendMode::REPLACE) {
      selected.flags = NATIVE_SUN_VISIBILITY_V2_DECAL;
    } else {
      selected.flags = NATIVE_SUN_VISIBILITY_V2_OPAQUE |
                       NATIVE_SUN_VISIBILITY_V2_RASTER_VISIBLE;
      if (casts) {
        selected.flags |= NATIVE_SUN_VISIBILITY_V2_CASTER;
      }
      if (receives) {
        selected.flags |= NATIVE_SUN_VISIBILITY_V2_RECEIVER;
      }
    }
    candidate.push_back(selected);
  }
  NativeSunVisibilityV2ScenePlan plan;
  const ValidationResult validation =
      TryBuildNativeSunVisibilityV2ScenePlan(candidate, plan);
  if (!validation) {
    return validation;
  }
  output = std::move(candidate);
  return ValidationResult::Success();
}

RenderOperationCode ToRenderOperationCode(
    NativeSunVisibilityV2Code code) noexcept {
  switch (code) {
  case NativeSunVisibilityV2Code::OK:
    return RenderOperationCode::OK;
  case NativeSunVisibilityV2Code::UNSUPPORTED:
    return RenderOperationCode::UNSUPPORTED;
  case NativeSunVisibilityV2Code::INVALID_ARGUMENT:
    return RenderOperationCode::INVALID_ARGUMENT;
  case NativeSunVisibilityV2Code::RESOURCE_STALE:
    return RenderOperationCode::RESOURCE_STALE;
  case NativeSunVisibilityV2Code::TIMEOUT:
    return RenderOperationCode::TIMEOUT;
  case NativeSunVisibilityV2Code::DEVICE_LOST:
    return RenderOperationCode::DEVICE_LOST;
  case NativeSunVisibilityV2Code::BACKEND_FAILURE:
    return RenderOperationCode::BACKEND_FAILURE;
  }
  return RenderOperationCode::BACKEND_FAILURE;
}

class NativeSunVisibilityV2ProductionFrontend final
    : public IRendererFrontend {
public:
  NativeSunVisibilityV2ProductionFrontend(
      OgreNextN1Configuration configuration, std::string shader_path)
      : frontend_(std::make_unique<OgreNextN1Frontend>(
            std::move(configuration),
            OgreNextNativeFeatureTier::
                METAL_RAY_TRACING_V2_SUN_VISIBILITY)),
        backend_(OgreNextMetalRayTracingMode::V2_SUN_VISIBILITY,
                 std::move(shader_path)) {}

  [[nodiscard]] OgreNextN1Frontend *native_frontend() noexcept {
    return frontend_.get();
  }

  [[nodiscard]] RendererNativeSunVisibilityV2Audit Audit() const noexcept {
    RendererNativeSunVisibilityV2Audit output;
    if (completed_frames_ == 0U) {
      return output;
    }
    const NativeSunVisibilityV2FrameContract &contract = last_contract_;
    output.version = contract.version;
    output.completed_frames = completed_frames_;
    output.frame_id = contract.frame_id;
    output.snapshot_id = contract.snapshot_id;
    output.view_id = contract.view_id;
    output.scene_plan_digest = contract.scene_plan_digest;
    output.selected_instances = contract.selected_instance_count;
    output.admitted_instances = contract.admitted_instance_count;
    output.excluded_instances = contract.excluded_instance_count;
    output.receivers = contract.receiver_count;
    output.casters = contract.caster_count;
    output.unique_meshes = contract.unique_mesh_count;
    output.blas_builds = contract.blas_build_count;
    output.blas_cache_hits = contract.blas_cache_hit_count;
    output.blas_refits = contract.blas_refit_count;
    output.tlas_builds = contract.tlas_build_count;
    output.tlas_cache_hits = contract.tlas_cache_hit_count;
    output.tlas_refits = contract.tlas_refit_count;
    output.primary_rays = contract.primary_ray_count;
    output.sun_visibility_rays =
        contract.secondary_sun_visibility_ray_count;
    output.visible_texels = contract.visible_visibility_texel_count;
    output.occluded_texels = contract.occluded_visibility_texel_count;
    output.gpu_execution_nanoseconds = contract.gpu_execution_nanoseconds;
    output.production_cpu_content_readbacks =
        contract.production_cpu_content_readbacks;
    output.production_gpu_content_readbacks =
        contract.production_gpu_content_readbacks;
    output.supports_raytracing = contract.capabilities.supports_raytracing;
    output.apple_family_9 = contract.capabilities.apple_family_9;
    output.same_ogre_device = contract.capabilities.same_ogre_device;
    output.same_ogre_queue = contract.capabilities.same_ogre_queue;
    output.same_ogre_timeline = contract.capabilities.same_ogre_timeline;
    output.shader_lock_verified = contract.shader_lock_verified;
    output.sun_direct_only_visibility_modulation =
        contract.sun_direct_only_visibility_modulation;
    output.submission_completed = contract.submission_completed;
    output.available = true;
    return output;
  }

  FrontendCapabilityReport QueryCapabilities() const override {
    return frontend_->QueryCapabilities();
  }

  RenderOperationResult
  Initialize(const FrontendInitializationRequest &request) override {
    if (frontend_initialized_ || backend_initialized_) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "production V2 frontend is already initialized");
    }
    RenderOperationResult result = frontend_->Initialize(request);
    if (!result) {
      return result;
    }
    frontend_initialized_ = true;
    NativeRenderInterop *const interop = frontend_->GetNativeInterop();
    if (interop == nullptr) {
      result = RenderOperationResult::Failure(
          RenderOperationCode::UNSUPPORTED,
          "production V2 frontend did not expose native interop");
    } else {
      result = backend_.Initialize(*interop);
    }
    if (result) {
      backend_initialized_ = true;
      return result;
    }
    const RenderOperationResult stopped =
        frontend_->Shutdown(UINT64_C(5) * UINT64_C(1000) *
                            UINT64_C(1000) * UINT64_C(1000));
    if (stopped || stopped.code == RenderOperationCode::DEVICE_LOST) {
      frontend_initialized_ = false;
    }
    return result;
  }

  RenderOperationResult PresentBootstrapFrame() override {
    return frontend_->PresentBootstrapFrame();
  }

  RenderOperationResult
  PresentUiOverlayFrame(const UiOverlayFrameRequest &request) override {
    // No registry_ bookkeeping: a GUI-only present consumes no portable asset,
    // snapshot, or frame identity, so this wrapper has nothing to mirror.
    return frontend_->PresentUiOverlayFrame(request);
  }

  RenderOperationResult
  UpdateSurface(const FrontendSurfaceUpdate &update, bool headless,
                std::uint64_t timeout_nanoseconds) override {
    return frontend_->UpdateSurface(update, headless, timeout_nanoseconds);
  }

  RenderOperationResult
  SynchronizeAssets(const RenderAssetDelta &delta) override {
    try {
      std::unique_ptr<RenderAssetRegistry> candidate = registry_
          ? std::make_unique<RenderAssetRegistry>(*registry_)
          : std::make_unique<RenderAssetRegistry>(delta.registry_id);
      const ValidationResult validation = candidate->Apply(delta);
      if (!validation) {
        return RenderOperationResult::Failure(
            RenderOperationCode::INVALID_ARGUMENT, validation.detail);
      }
      const RenderOperationResult result = frontend_->SynchronizeAssets(delta);
      if (result) {
        registry_ = std::move(candidate);
      }
      return result;
    } catch (const std::bad_alloc &) {
      return RenderOperationResult::Failure(
          RenderOperationCode::OUT_OF_MEMORY,
          "production V2 asset registry allocation failed");
    } catch (...) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "production V2 asset registry synchronization failed");
    }
  }

  RenderOperationResult
  ResetSceneGeneration(std::uint64_t next_generation) override {
    return frontend_->ResetSceneGeneration(next_generation);
  }

  RenderOperationResult ReleaseResource(ResourceHandle resource) override {
    return frontend_->ReleaseResource(resource);
  }

  RenderOperationResult Render(const RenderFrameRequest &request,
                               RenderFrameOutput &output) override {
    if (!backend_initialized_ || registry_ == nullptr ||
        request.views.size() != 1U || !request.present ||
        request.requested_outputs != FrameOutputMask::COLOR ||
        request.color_format != PixelFormat::RGBA16_FLOAT ||
        request.allow_async_compute || request.scene_snapshot == nullptr) {
      return RenderOperationResult::Failure(
          RenderOperationCode::INVALID_ARGUMENT,
          "production V2 requires one presented synchronous RGBA16 scene");
    }
    // The direct dispatcher authenticates the public wrapper it submits to.
    // The raster frontend is an implementation detail with a distinct object
    // identity, so forwarding that opaque receipt unchanged would make the
    // inner frontend correctly reject it as belonging to another frontend.
    // Authenticate it here against this exact wrapper, then deliberately drop
    // it so the inner frontend performs its complete scene-asset scan.
    if (request.in_process_scene_asset_validation != nullptr &&
        !request.in_process_scene_asset_validation->Authenticates(
            request.scene_snapshot, *this, registry_->registry_id(),
            registry_->sequence())) {
      return RenderOperationResult::Failure(
          RenderOperationCode::RESOURCE_STALE,
          "production V2 received stale direct-dispatch scene validation authority");
    }
    std::vector<NativeSunVisibilityV2InstanceSelection> selection;
    const ValidationResult selection_validation = BuildSunVisibilitySelection(
        *request.scene_snapshot, *registry_, request.views.front(), selection);
    if (!selection_validation) {
      return RenderOperationResult::Failure(
          selection_validation.code == ValidationCode::UNSUPPORTED_FEATURE
              ? RenderOperationCode::UNSUPPORTED
              : RenderOperationCode::INVALID_ARGUMENT,
          selection_validation.detail);
    }
    RenderFrameRequest raster_request = request;
    raster_request.in_process_scene_asset_validation.reset();
    raster_request.present = false;
    raster_request.presentation_view_id = 0U;
    raster_request.presentation_surface_revision = 0U;
    const RenderOperationResult rendered =
        frontend_->Render(raster_request, output);
    if (!rendered) {
      return rendered;
    }

    OgreNextMetalSunVisibilityV2FrameRequest native_request;
    native_request.ray_tracing.frame = raster_request;
    native_request.ray_tracing.samples_per_pixel = 1U;
    native_request.ray_tracing.maximum_bounces = 1U;
    native_request.ray_tracing.denoise = false;
    native_request.selection = std::move(selection);
    NativeSunVisibilityV2FrameContract contract;
    const NativeSunVisibilityV2Result result =
        backend_.RenderSunVisibilityV2(native_request, contract);
    if (result.code != NativeSunVisibilityV2Code::OK) {
      return RenderOperationResult::Failure(ToRenderOperationCode(result.code),
                                            result.detail);
    }
    const ValidationResult contract_validation =
        ValidateNativeSunVisibilityV2FrameContract(contract);
    if (!contract_validation) {
      return RenderOperationResult::Failure(
          RenderOperationCode::BACKEND_FAILURE,
          "production Metal V2 returned an invalid frame contract");
    }
    last_contract_ = std::move(contract);
    ++completed_frames_;
    output.presented = true;
    output.presented_view_id = request.presentation_view_id;
    return RenderOperationResult::Success();
  }

  RenderOperationResult
  RetireFrameState(const RenderFrameRequest &request) override {
    if (request.in_process_scene_asset_validation != nullptr) {
      if (registry_ == nullptr || request.scene_snapshot == nullptr ||
          !request.in_process_scene_asset_validation->Authenticates(
              request.scene_snapshot, *this, registry_->registry_id(),
              registry_->sequence())) {
        return RenderOperationResult::Failure(
            RenderOperationCode::RESOURCE_STALE,
            "production V2 received stale retired-scene validation authority");
      }
      RenderFrameRequest raster_request = request;
      raster_request.in_process_scene_asset_validation.reset();
      return frontend_->RetireFrameState(raster_request);
    }
    return frontend_->RetireFrameState(request);
  }

  bool IsFrameComplete(std::uint64_t frame_id) const noexcept override {
    return frontend_->IsFrameComplete(frame_id);
  }

  RenderOperationResult
  WaitForFrame(std::uint64_t frame_id,
               std::uint64_t timeout_nanoseconds) override {
    return frontend_->WaitForFrame(frame_id, timeout_nanoseconds);
  }

  NativeRenderInterop *GetNativeInterop() noexcept override {
    return frontend_->GetNativeInterop();
  }

  RenderOperationResult Shutdown(std::uint64_t timeout_nanoseconds) override {
    if (!frontend_initialized_ && !backend_initialized_) {
      return RenderOperationResult::Failure(
          RenderOperationCode::NOT_INITIALIZED,
          "production V2 frontend is not initialized");
    }
    if (backend_initialized_) {
      const RenderOperationResult stopped = backend_.Shutdown(timeout_nanoseconds);
      if (!stopped && stopped.code != RenderOperationCode::DEVICE_LOST) {
        return stopped;
      }
      backend_initialized_ = false;
    }
    if (frontend_initialized_) {
      const RenderOperationResult stopped =
          frontend_->Shutdown(timeout_nanoseconds);
      if (!stopped && stopped.code != RenderOperationCode::DEVICE_LOST) {
        return stopped;
      }
      frontend_initialized_ = false;
      registry_.reset();
      return stopped;
    }
    return RenderOperationResult::Success();
  }

private:
  std::unique_ptr<OgreNextN1Frontend> frontend_;
  OgreNextMetalRayTracingBackend backend_;
  std::unique_ptr<RenderAssetRegistry> registry_;
  NativeSunVisibilityV2FrameContract last_contract_;
  std::uint64_t completed_frames_ = 0U;
  bool frontend_initialized_ = false;
  bool backend_initialized_ = false;
};

#endif

} // namespace

class RendererOgreNextInProcessPresenter::Impl final {
public:
  struct InputDevice final {
    SDL_GameController *controller = nullptr;
    SDL_Joystick *joystick = nullptr;
    SDL_JoystickID instance_id = -1;
    std::uint64_t generation = 0U;
    std::size_t slot = 0U;
    std::string vendor;
    std::vector<std::int32_t> axes;
    std::vector<std::int32_t> relative_axes;
    std::vector<bool> buttons;
    std::vector<std::uint8_t> hats;
    std::size_t axis_count = 0U;
    std::size_t button_count = 0U;
    std::size_t hat_count = 0U;
    bool standardized = false;
  };

  struct RefreshedDeviceState final {
    std::vector<std::int32_t> axes;
    std::vector<std::int32_t> relative_axes;
    std::vector<bool> buttons;
    std::vector<std::uint8_t> hats;
  };

  [[nodiscard]] RendererContinuousParticleAudit
  ContinuousParticleAudit() const noexcept {
    RendererContinuousParticleAudit output;
    if (native_frontend == nullptr) {
      return output;
    }
    const OgreNextN1ParticleRuntimeAudit audit =
        native_frontend->QueryParticleRuntimeAudit();
    output.committed_source_sequence = audit.committed_source_sequence;
    output.create_commands = audit.create_commands;
    output.update_commands = audit.update_commands;
    output.stop_commands = audit.stop_commands;
    output.destroy_commands = audit.destroy_commands;
    output.live_systems = audit.live_systems;
    output.live_particles = audit.live_particles;
    output.lifetime_max_live_systems = audit.lifetime_max_live_systems;
    output.lifetime_max_live_particles = audit.lifetime_max_live_particles;
    output.source_backed_textures = audit.source_backed_textures;
    output.source_alpha_textures = audit.source_alpha_textures;
    output.lifetime_max_source_backed_textures =
        audit.lifetime_max_source_backed_textures;
    output.lifetime_max_source_alpha_textures =
        audit.lifetime_max_source_alpha_textures;
    output.gpu_readbacks = audit.gpu_readbacks;
    output.native_batch_creates = audit.native_batch_creates;
    output.native_batch_destroys = audit.native_batch_destroys;
    output.native_particles_submitted = audit.native_particles_submitted;
    output.native_state_readbacks = audit.native_state_readbacks;
    output.native_state_verifications = audit.native_state_verifications;
    output.available = true;
    return output;
  }

  [[nodiscard]] RendererAnalyticSkyAudit
  AnalyticSkyAudit() const noexcept {
    RendererAnalyticSkyAudit output;
    if (native_frontend == nullptr) {
      return output;
    }
    const OgreNextAnalyticSkyRuntimeAudit audit =
        native_frontend->QueryAnalyticSkyAudit();
    const OgreNextN1PresentationAudit presentation_audit =
        native_frontend->QueryPresentationAudit();
    output.completed_frames = audit.completed_frames;
    output.sun_light_id = audit.last_sun_light_id;
    output.cpu_geometry_fnv1a64 = audit.last_cpu_geometry_fnv1a64;
    output.native_gpu_content_readbacks =
        audit.native_gpu_content_readbacks;
    output.native_state_verifications = audit.native_state_verifications;
    output.native_ownership_balanced =
        audit.native_mesh_creates == audit.native_mesh_destroys &&
        audit.native_vertex_buffer_creates ==
            audit.native_vertex_buffer_destroys &&
        audit.native_index_buffer_creates ==
            audit.native_index_buffer_destroys &&
        audit.native_vao_creates == audit.native_vao_destroys &&
        audit.native_item_creates == audit.native_item_destroys &&
        audit.native_scene_node_creates ==
            audit.native_scene_node_destroys &&
        audit.native_datablock_creates ==
            audit.native_datablock_destroys &&
        audit.native_mesh_absence_checks == audit.native_mesh_destroys &&
        audit.native_item_absence_checks == audit.native_item_destroys &&
        audit.native_scene_node_absence_checks ==
            audit.native_scene_node_destroys &&
        audit.native_datablock_absence_checks ==
            audit.native_datablock_destroys;
    const bool multiplication_safe =
        audit.completed_frames <=
        (std::numeric_limits<std::uint64_t>::max)() / 2U;
    output.native_geometry_metadata_verified =
        audit.native_geometry_metadata_verified;
    output.cpu_geometry_digest_verified =
        audit.completed_frames > 0U && audit.last_cpu_geometry_fnv1a64 != 0U;
    output.production_gpu_readbacks_zero =
        audit.native_gpu_content_readbacks == 0U &&
        presentation_audit.enabled &&
        presentation_audit.source_readbacks == 0U &&
        presentation_audit.gpu_only_output_frames >= audit.completed_frames;
    output.expected_per_frame_ownership =
        multiplication_safe && audit.version == 2U &&
        audit.completed_frames > 0U &&
        audit.native_mesh_creates == audit.completed_frames * 2U &&
        audit.native_vertex_buffer_creates == audit.completed_frames * 2U &&
        audit.native_index_buffer_creates == audit.completed_frames * 2U &&
        audit.native_vao_creates == audit.completed_frames * 2U &&
        audit.native_item_creates == audit.completed_frames * 2U &&
        audit.native_scene_node_creates == audit.completed_frames &&
        audit.native_datablock_creates == audit.completed_frames * 2U &&
        output.production_gpu_readbacks_zero &&
        output.cpu_geometry_digest_verified &&
        output.native_geometry_metadata_verified &&
        audit.native_state_verifications == audit.completed_frames;
    output.exact_native_geometry_readback =
        audit.exact_native_geometry_readback;
    output.separate_sun_alpha_replace = audit.separate_sun_alpha_replace;
    output.available = true;
    return output;
  }

  [[nodiscard]] RendererUiOverlayPresentationAudit
  UiOverlayPresentationAudit() const noexcept {
    RendererUiOverlayPresentationAudit output;
    if (native_frontend == nullptr) {
      return output;
    }
    const OgreNextN1PresentationAudit audit =
        native_frontend->QueryPresentationAudit();
    output.version = audit.version;
    output.presented_frames = audit.ui_overlay_presented_frames;
    output.render_one_frame_calls = audit.ui_overlay_render_one_frame_calls;
    output.image_uploads = audit.ui_overlay_image_uploads;
    output.image_creates = audit.ui_overlay_image_creates;
    output.image_destroys = audit.ui_overlay_image_destroys;
    output.workspace_creates = audit.ui_overlay_workspace_creates;
    output.workspace_destroys = audit.ui_overlay_workspace_destroys;
    output.scene_presented_frames = audit.presented_frames;
    output.bootstrap_clear_passes = audit.bootstrap_clear_passes;
    output.last_width = audit.ui_overlay_last_width;
    output.last_height = audit.ui_overlay_last_height;
    output.available = true;
    return output;
  }

  [[nodiscard]] RendererNativeLightingAudit
  NativeLightingAudit() const noexcept {
    RendererNativeLightingAudit output;
    if (native_frontend == nullptr) {
      return output;
    }
    const OgreNextNativeLightingPassAudit audit =
        native_frontend->QueryNativeLightingPassAudit();
    output.version = audit.version;
    output.completed_frames = audit.completed_frames;
    output.last_frame_id = audit.last_frame_id;
    output.last_snapshot_id = audit.last_snapshot_id;
    output.native_state_verifications = audit.native_state_verifications;
    output.production_content_readbacks =
        audit.production_content_readbacks;
    output.production_framebuffer_readbacks =
        audit.production_framebuffer_readbacks;
    output.ogre14_lighting_passes = audit.ogre14_lighting_passes;
    output.material_descriptor_version =
        audit.last_material_descriptor_version;
    output.directional_lights = audit.last_directional_lights;
    output.point_lights = audit.last_point_lights;
    output.spot_lights = audit.last_spot_lights;
    output.forward_clustered = audit.forward_clustered_active;
    output.pbs_items = audit.last_pbs_items;
    output.transmission_items = audit.last_transmission_items;
    output.normal_mapped_items = audit.last_normal_mapped_items;
    output.emissive_items = audit.last_emissive_items;
    output.shadow_casters = audit.last_shadow_casters;
    output.shadow_receivers = audit.last_shadow_receivers;
    output.distance_lod_items = audit.last_distance_lod_items;
    output.distance_lod_reduced_items =
        audit.last_distance_lod_reduced_items;
    output.distance_lod_max_selected_level =
        audit.last_distance_lod_max_selected_level;
    output.distance_lod_selected_level_sum =
        audit.last_distance_lod_selected_level_sum;
    output.base_triangles = audit.last_base_triangles;
    output.selected_triangles = audit.last_selected_triangles;
    output.exact_native_distance_lod_state =
        audit.exact_native_distance_lod_state;
    output.hdr_scene_topology =
        static_cast<std::uint32_t>(audit.hdr_scene_topology);
    const OgreNextReflectionProbeAudit reflection =
        native_frontend->QueryReflectionProbeAudit();
    output.reflection_probe_audit_version = reflection.version;
    output.reflection_live_probe_count = reflection.live_probe_count;
    output.reflection_completed_face_count = reflection.completed_face_count;
    output.reflection_completed_mip_count = reflection.completed_mip_count;
    output.reflection_probe_resolution = reflection.last_probe_resolution;
    output.reflection_blend_resolution = reflection.blend_resolution;
    output.reflection_successful_capture_count =
        reflection.successful_capture_count;
    output.reflection_failed_capture_count = reflection.failed_capture_count;
    output.reflection_native_execution_evidence =
        reflection.native_execution_evidence;
    output.reflection_last_capture_frame_id =
        reflection.last_capture_frame_id;
    output.reflection_last_capture_simulation_tick =
        reflection.last_capture_simulation_tick;
    output.reflection_scene_reset_retired_probe_count =
        reflection.scene_reset_retired_probe_count;
    output.reflection_scene_reset_teardowns =
        reflection.scene_reset_teardowns;
    output.reflection_initialized = reflection.initialized;
    output.reflection_exact_resources_loaded =
        reflection.exact_resources_loaded;
    output.reflection_pcc_enabled = reflection.pcc_enabled;
    output.reflection_pbs_bound = reflection.pbs_bound;
    output.reflection_blend_texture_ready = reflection.blend_texture_ready;
    output.reflection_ui_free_capture = reflection.ui_free_capture;
    output.reflection_reserved_render_queue_excluded =
        reflection.reserved_render_queue_excluded;
    output.native_scene_lighting_pass = audit.native_scene_lighting_pass;
    output.pssm_finalized_with_populated_scene =
        audit.pssm_finalized_with_populated_scene;
    output.linear_rgba16_hdr_target = audit.linear_rgba16_hdr_target;
    output.separate_base_hdr_target = audit.separate_base_hdr_target;
    output.separate_unoccluded_sun_full_hdr_target =
        audit.separate_unoccluded_sun_full_hdr_target;
    output.separate_sun_direct_hdr_target =
        audit.separate_sun_direct_hdr_target;
    output.gpu_sun_direct_derivation = audit.gpu_sun_direct_derivation;
    output.transactional_directional_sun_toggle =
        audit.transactional_directional_sun_toggle;
    output.raster_lit_hdr_target = audit.raster_lit_hdr_target;
    output.single_step_hdr_history = audit.single_step_hdr_history;
    output.raster_scene_evaluations = audit.raster_scene_evaluations;
    output.calibrated_directional_lighting =
        audit.calibrated_directional_lighting;
    output.ambient_environment_lighting =
        audit.ambient_environment_lighting;
    output.ambient_sh_bound = audit.ambient_sh_bound;
    output.ambient_sh_gain = audit.ambient_sh_gain;
    output.ambient_sh_band0_luminance = audit.ambient_sh_band0_luminance;
    output.probe_sky_admission = audit.probe_sky_admission;
    output.analytic_sky_contribution = audit.analytic_sky_contribution;
    output.aerial_haze_applied = audit.aerial_haze_applied;
    {
      const OgreNextHdrCompositorAudit compositor =
          native_frontend->QueryHdrCompositorAudit();
      output.aerial_haze_workspace_verified =
          compositor.aerial_haze_workspace_verified;
      output.aerial_haze_constants_bound =
          compositor.aerial_haze_constants_bound;
      output.aerial_haze_depth_export_verified =
          compositor.opaque_depth_export_verified;
      output.aerial_haze_extinction_per_meter =
          compositor.aerial_haze_extinction_per_meter;
      output.aerial_haze_inscatter_r = compositor.aerial_haze_inscatter.x;
      output.aerial_haze_inscatter_g = compositor.aerial_haze_inscatter.y;
      output.aerial_haze_inscatter_b = compositor.aerial_haze_inscatter.z;
    }
    output.emissive_material_response = audit.emissive_material_response;
    output.pssm_shadow_response = audit.pssm_shadow_response;
    output.thin_parallel_slab_refraction =
        audit.thin_parallel_slab_refraction;
    output.physical_snell_refraction = audit.physical_snell_refraction;
    output.beer_lambert_attenuation = audit.beer_lambert_attenuation;
    output.screen_space_radiance_lookup =
        audit.screen_space_radiance_lookup;
    output.refraction_scene_evaluations =
        audit.refraction_scene_evaluations;
    output.hdr_auto_exposure = audit.hdr_auto_exposure;
    output.gpu_hdr_history_sequenced = audit.gpu_hdr_history_sequenced;
    output.hdr_bloom = audit.hdr_bloom;
    output.filmic_tone_map = audit.filmic_tone_map;
    output.srgb_presentation = audit.srgb_presentation;
    output.production_gpu_only = audit.production_gpu_only;
    output.no_ogre14_lighting = audit.no_ogre14_lighting;
    output.available = true;
    return output;
  }

  [[nodiscard]] RendererRetainedSceneAudit
  RetainedSceneAudit() const noexcept {
    RendererRetainedSceneAudit output;
    if (native_frontend == nullptr) {
      return output;
    }
    const OgreNextRetainedSceneAudit audit =
        native_frontend->QueryRetainedSceneAudit();
    output.version = audit.version;
    output.generation = audit.generation;
    output.frames_diffed = audit.frames_diffed;
    output.retained_instances = audit.retained_instances;
    output.retained_lights = audit.retained_lights;
    output.bounds_entries = audit.bounds_entries;
    output.created = audit.created;
    output.updated = audit.updated;
    output.destroyed = audit.destroyed;
    output.dynamic_updates = audit.dynamic_updates;
    output.dynamic_buffer_updates = audit.dynamic_buffer_updates;
    output.dynamic_mesh_rebuilds = audit.dynamic_mesh_rebuilds;
    output.dynamic_vertex_upload_bytes =
        audit.dynamic_vertex_upload_bytes;
    output.verified = audit.verified;
    output.last_created = audit.last_created;
    output.last_updated = audit.last_updated;
    output.last_destroyed = audit.last_destroyed;
    output.last_dynamic_updates = audit.last_dynamic_updates;
    output.last_dynamic_buffer_updates =
        audit.last_dynamic_buffer_updates;
    output.last_dynamic_mesh_rebuilds = audit.last_dynamic_mesh_rebuilds;
    output.last_dynamic_vertex_upload_bytes =
        audit.last_dynamic_vertex_upload_bytes;
    output.last_verified = audit.last_verified;
    output.last_diff_used_retained_block_proof =
        audit.last_diff_used_retained_block_proof;
    output.verify_window = audit.verify_window;
    output.verify_cursor = audit.verify_cursor;
    output.recovery_teardowns = audit.recovery_teardowns;
    output.retired_light_teardowns = audit.retired_light_teardowns;
    output.last_validation_phase_microseconds =
        audit.last_validation_phase_microseconds;
    output.last_frame_prepare_phase_microseconds =
        audit.last_frame_prepare_phase_microseconds;
    output.last_light_phase_microseconds =
        audit.last_light_phase_microseconds;
    output.last_instance_phase_microseconds =
        audit.last_instance_phase_microseconds;
    output.last_native_prepare_phase_microseconds =
        audit.last_native_prepare_phase_microseconds;
    output.last_native_render_phase_microseconds =
        audit.last_native_render_phase_microseconds;
    output.last_post_render_phase_microseconds =
        audit.last_post_render_phase_microseconds;
    output.last_cleanup_phase_microseconds =
        audit.last_cleanup_phase_microseconds;
    output.last_publication_phase_microseconds =
        audit.last_publication_phase_microseconds;
    output.last_native_renderer_frame_id =
        audit.last_native_renderer_frame_id;
    output.last_native_frame_batches = audit.last_native_frame_batches;
    output.last_native_frame_draws = audit.last_native_frame_draws;
    output.last_native_frame_instances = audit.last_native_frame_instances;
    output.last_native_frame_faces = audit.last_native_frame_faces;
    output.last_native_frame_vertices = audit.last_native_frame_vertices;
    output.last_native_pre_hdr_draws = audit.last_native_pre_hdr_draws;
    output.last_native_shadow_draws = audit.last_native_shadow_draws;
    output.last_native_scene_draws = audit.last_native_scene_draws;
    output.last_native_hdr_post_draws = audit.last_native_hdr_post_draws;
    output.last_native_after_hdr_draws = audit.last_native_after_hdr_draws;
    output.last_native_shadow_instances =
        audit.last_native_shadow_instances;
    output.last_native_scene_instances = audit.last_native_scene_instances;
    output.last_native_shadow_faces = audit.last_native_shadow_faces;
    output.last_native_scene_faces = audit.last_native_scene_faces;
    output.last_native_pass_metrics_exact =
        audit.last_native_pass_metrics_exact;
    output.available = true;
    return output;
  }

  [[nodiscard]] RendererRenderBoundaryDegradeAudit
  RenderBoundaryDegradeAudit() const noexcept {
    RendererRenderBoundaryDegradeAudit output;
    if (native_frontend == nullptr) {
      return output;
    }
    const OgreNextN1RenderBoundaryDegradeAudit audit =
        native_frontend->QueryRenderBoundaryDegradeAudit();
    output.version = audit.version;
    output.post_submit_recoverable_failures =
        audit.post_submit_recoverable_failures;
    output.hud_extent_mismatch_frames = audit.hud_extent_mismatch_frames;
    output.particle_basis_rejections = audit.particle_basis_rejections;
    output.pssm_pose_renormalizations = audit.pssm_pose_renormalizations;
    output.non_uniform_scale_instance_rejections =
        audit.non_uniform_scale_instance_rejections;
    output.available = true;
    return output;
  }

  [[nodiscard]] RendererNativeSunVisibilityV2Audit
  NativeSunVisibilityAudit() const noexcept {
#if defined(__APPLE__)
    return native_sun_visibility_frontend != nullptr
               ? native_sun_visibility_frontend->Audit()
               : RendererNativeSunVisibilityV2Audit{};
#else
    return RendererNativeSunVisibilityV2Audit{};
#endif
  }

  static constexpr std::size_t kMaximumAxes = 32U;
  static constexpr std::size_t kMaximumHats = 4U;

  static bool ShowAfterWorkspaceReady(
      void *opaque, FrontendSurfaceUpdate *acknowledged_surface) {
    auto *self = static_cast<Impl *>(opaque);
    if (self == nullptr || acknowledged_surface == nullptr ||
        !self->prepared || self->quiesced ||
        self->host.Resume() != RendererOgreNextWindowHostStatus::COMPLETED ||
        self->host.Lifecycle() != RendererOgreNextWindowLifecycle::ACTIVE) {
      return false;
    }
    const RendererOgreNextWindowMetrics *metrics = self->host.Metrics();
    bool changed = false;
    if (metrics == nullptr ||
        !self->ObserveMetrics(*metrics, false, &changed)) {
      return false;
    }
    if (changed) {
      self->pending_surface_notification = self->surface;
    }
    *acknowledged_surface = self->surface;
    return true;
  }

  bool ObserveMetrics(const RendererOgreNextWindowMetrics &metrics,
                      bool suspended, bool *changed_out = nullptr) noexcept {
    if (surface_revision == 0U ||
        surface_revision == (std::numeric_limits<std::uint64_t>::max)()) {
      return false;
    }
    const bool changed = metrics_generation != metrics.generation ||
                         surface.suspended != suspended;
    if (changed) {
      ++surface_revision;
    }
    metrics_generation = metrics.generation;
    surface = MakeSurface(window, metrics, surface_revision, suspended);
    if (changed_out != nullptr) {
      *changed_out = changed;
    }
    return true;
  }

  RendererOgreNextInProcessPresenterStatus Prepare(
      const RendererOgreNextInProcessPresenterConfiguration &candidate) {
    if (prepared ||
        !IsValidRendererOgreNextInProcessPresenterConfiguration(candidate)) {
      return prepared
                 ? RendererOgreNextInProcessPresenterStatus::
                       REJECTED_LIFECYCLE
                 : RendererOgreNextInProcessPresenterStatus::
                       REJECTED_CONFIGURATION;
    }
    RendererOgreNextWindowRequest request;
    request.platform = HostWindowPlatform();
    request.logical_width = candidate.logical_width;
    request.logical_height = candidate.logical_height;
    request.fsaa_samples = 0U;
#if !defined(__APPLE__)
    request.vertical_sync = false;
    request.vertical_sync_interval = 0U;
#endif
    if (host.Initialize(request, runtime.Runtime()) !=
        RendererOgreNextWindowHostStatus::COMPLETED) {
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_WINDOW_INITIALIZATION;
    }
    if (candidate.exact_drawable_width != 0U &&
        host.ResizeToExactDrawable(candidate.exact_drawable_width,
                                   candidate.exact_drawable_height) !=
            RendererOgreNextWindowHostStatus::COMPLETED) {
      (void)host.Shutdown();
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_WINDOW_INITIALIZATION;
    }
    const RendererOgreNextWindowBinding *binding = host.Binding();
    const RendererOgreNextSdlNativeWindow *native = host.NativeWindow();
    const RendererOgreNextWindowMetrics *metrics = host.Metrics();
    if (binding == nullptr || native == nullptr || metrics == nullptr) {
      (void)host.Shutdown();
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_WINDOW_INITIALIZATION;
    }
    window = MakeWindowHandle(*native);
    sdl_window = native->sdl_window;
    if (!window.valid() || sdl_window == nullptr) {
      (void)host.Shutdown();
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_WINDOW_INITIALIZATION;
    }

    OgreNextN1Configuration frontend_configuration;
    frontend_configuration.shader_media_root = candidate.shader_media_root;
    frontend_configuration.raster_feature_tier =
        OgreNextRasterFeatureTier::MODERN_PBR_RT4_V1;
    const bool native_sun_visibility_v2 =
        candidate.lighting_mode ==
        RendererOgreNextInProcessLightingMode::
            METAL_RT_SUN_VISIBILITY_V2;
    // Raster gameplay uses the reviewed single-evaluation HDR/PSSM topology.
    // The bounded Metal V2 showcase instead needs the three-evaluation split
    // so BaseHdr and unoccluded SunDirectHdr remain independently available to
    // the hardware visibility pass before the one-shot LitHdr continuation.
    frontend_configuration.directional_shadow_mode = native_sun_visibility_v2
        ? OgreNextDirectionalShadowMode::DISABLED
        : OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1;
    if (!native_sun_visibility_v2) {
      // Stage 3: the combined raster session opts into the modern cascade
      // quality defaults (and their fail-closed environment knobs) before
      // the frontend resolves the immutable shadow configuration. The
      // standalone probe never calls this and keeps the byte-stable V1
      // checkpoint values.
      RoR::Render::RequestOgreNextPssmModernShadowQualityDefaults();
    }
    frontend_configuration.enable_hdr_compositor = true;
    frontend_configuration.hdr_scene_topology = native_sun_visibility_v2
        ? OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2
        : OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1;
    frontend_configuration.presentation.enabled = true;
    frontend_configuration.presentation.mode =
        OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP;
    frontend_configuration.presentation.gpu_only_output = true;
    frontend_configuration.presentation.shader_media_root =
        candidate.presentation_media_root;
    frontend_configuration.presentation.exact_window = window;
    if (!CopyParameters(
            binding->renderer_options, binding->renderer_option_count,
            frontend_configuration.presentation.renderer_options,
            frontend_configuration.presentation.renderer_option_count) ||
        !CopyParameters(
            binding->bootstrap_window_parameters,
            binding->bootstrap_window_parameter_count,
            frontend_configuration.presentation.bootstrap_window_parameters,
            frontend_configuration.presentation
                .bootstrap_window_parameter_count) ||
        !CopyParameters(
            binding->presentation_window_parameters,
            binding->presentation_window_parameter_count,
            frontend_configuration.presentation
                .presentation_window_parameters,
            frontend_configuration.presentation
                .presentation_window_parameter_count)) {
      (void)host.Shutdown();
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_FRONTEND_CONFIGURATION;
    }
    frontend_configuration.presentation.show_callback_context = this;
    frontend_configuration.presentation.show_after_workspace_ready =
        &ShowAfterWorkspaceReady;

    if (native_sun_visibility_v2) {
#if defined(__APPLE__)
      const std::string shader_path =
          candidate.shader_media_root +
          "/Hlms/RoR/SunVisibilityV2/SunVisibilityV2.metal";
      auto production =
          std::make_unique<NativeSunVisibilityV2ProductionFrontend>(
              std::move(frontend_configuration), shader_path);
      native_frontend = production->native_frontend();
      native_sun_visibility_frontend = production.get();
      frontend = std::move(production);
#else
      (void)host.Shutdown();
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_FRONTEND_CONFIGURATION;
#endif
    } else {
      auto raster = std::make_unique<OgreNextN1Frontend>(
          std::move(frontend_configuration));
      native_frontend = raster.get();
      frontend = std::move(raster);
    }
    configuration = candidate;
    surface_revision = kInitialSurfaceRevision;
    metrics_generation = metrics->generation;
    surface = MakeSurface(window, *metrics, surface_revision, false);
    initialization.initial_surface_revision = surface.surface_revision;
    initialization.window = surface.window;
    initialization.initial_width = surface.pixel_width;
    initialization.initial_height = surface.pixel_height;
    initialization.initial_content_scale = surface.content_scale;
    initialization.maximum_frames_in_flight = 1U;
    initialization.headless = false;
    initialization.vertical_sync = false;
    last_drawable_width = metrics->drawable_width;
    last_drawable_height = metrics->drawable_height;
    has_drawable_baseline = true;
    prepared = true;
    return RendererOgreNextInProcessPresenterStatus::COMPLETED;
  }

  RendererOgreNextInProcessPresenterStatus ProtectHidden(
      void *hidden_window) noexcept {
    if (!prepared || (quiesced && hidden_window != nullptr) ||
        (hidden_window != nullptr && hidden_window == sdl_window) ||
        !runtime.ValidateOwnerThread()) {
      return RendererOgreNextInProcessPresenterStatus::REJECTED_LIFECYCLE;
    }
    if (hidden_window != nullptr) {
      SDL_Window *const candidate = static_cast<SDL_Window *>(hidden_window);
      if (SDL_GetWindowID(candidate) == 0U) {
        return RendererOgreNextInProcessPresenterStatus::REJECTED_LIFECYCLE;
      }
      SDL_HideWindow(candidate);
      if ((SDL_GetWindowFlags(candidate) & SDL_WINDOW_HIDDEN) == 0U) {
        return RendererOgreNextInProcessPresenterStatus::REJECTED_LIFECYCLE;
      }
    }
    protected_hidden_window = hidden_window;
    return RendererOgreNextInProcessPresenterStatus::COMPLETED;
  }

  void CloseDevice(InputDevice &device) noexcept {
    if (device.controller != nullptr) {
      SDL_GameControllerClose(device.controller);
    } else if (device.joystick != nullptr) {
      SDL_JoystickClose(device.joystick);
    }
    device = InputDevice{};
  }

  void ShutdownControllers() noexcept {
    for (InputDevice &device : input_devices) {
      CloseDevice(device);
    }
    if (owns_controller_subsystem) {
      SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
      owns_controller_subsystem = false;
    }
    controllers_initialized = false;
  }

  bool FindDevice(SDL_JoystickID instance_id,
                  std::size_t &slot) const noexcept {
    for (std::size_t index = 0U; index < input_devices.size(); ++index) {
      if (input_devices[index].joystick != nullptr &&
          input_devices[index].instance_id == instance_id) {
        slot = index;
        return true;
      }
    }
    return false;
  }

  RefreshedDeviceState ReadDeviceState(const InputDevice &device) {
    RefreshedDeviceState refreshed;
    if (device.joystick == nullptr) {
      return refreshed;
    }
    refreshed.relative_axes.assign(device.axis_count, 0);
    refreshed.axes.resize(device.axis_count, 0);
    for (std::size_t axis = 0U; axis < device.axis_count; ++axis) {
      refreshed.axes[axis] = device.standardized
          ? SDL_GameControllerGetAxis(
                device.controller,
                static_cast<SDL_GameControllerAxis>(axis))
          : SDL_JoystickGetAxis(device.joystick,
                                static_cast<int>(axis));
    }
    refreshed.buttons.resize(device.button_count, false);
    for (std::size_t button = 0U; button < device.button_count; ++button) {
      refreshed.buttons[button] = device.standardized
          ? SDL_GameControllerGetButton(
                device.controller,
                static_cast<SDL_GameControllerButton>(button)) != 0U
          : SDL_JoystickGetButton(device.joystick,
                                  static_cast<int>(button)) != 0U;
    }
    refreshed.hats.resize(device.hat_count, 0U);
    if (!device.standardized) {
      for (std::size_t hat = 0U; hat < device.hat_count; ++hat) {
        refreshed.hats[hat] = SDL_JoystickGetHat(
            device.joystick, static_cast<int>(hat));
      }
    }
    return refreshed;
  }

  void CommitDeviceState(InputDevice &device,
                         RefreshedDeviceState refreshed) noexcept {
    device.axes.swap(refreshed.axes);
    device.relative_axes.swap(refreshed.relative_axes);
    device.buttons.swap(refreshed.buttons);
    device.hats.swap(refreshed.hats);
  }

  void RefreshDeviceState(InputDevice &device) {
    CommitDeviceState(device, ReadDeviceState(device));
  }

  void RefreshAllDeviceStates() {
    SDL_GameControllerUpdate();
    SDL_JoystickUpdate();
    std::array<RefreshedDeviceState, kRendererGameJoystickSlots> refreshed{};
    std::array<bool, kRendererGameJoystickSlots> active{};
    for (std::size_t index = 0U; index < input_devices.size(); ++index) {
      if (input_devices[index].joystick != nullptr) {
        refreshed[index] = ReadDeviceState(input_devices[index]);
        active[index] = true;
      }
    }
    for (std::size_t index = 0U; index < input_devices.size(); ++index) {
      if (active[index]) {
        CommitDeviceState(input_devices[index], std::move(refreshed[index]));
      }
    }
  }

  bool OpenDevice(int device_index) {
    if (!controllers_initialized || device_index < 0) {
      return false;
    }
    const SDL_JoystickID advertised =
        SDL_JoystickGetDeviceInstanceID(device_index);
    std::size_t existing = 0U;
    if (advertised >= 0 && FindDevice(advertised, existing)) {
      return true;
    }
    const auto free = std::find_if(
        input_devices.begin(), input_devices.end(),
        [](const InputDevice &device) { return device.joystick == nullptr; });
    if (free == input_devices.end() ||
        next_device_generation ==
            (std::numeric_limits<std::uint64_t>::max)()) {
      return false;
    }

    const SDL_JoystickType type = SDL_JoystickGetDeviceType(device_index);
    const bool specialized = type == SDL_JOYSTICK_TYPE_WHEEL ||
                             type == SDL_JOYSTICK_TYPE_FLIGHT_STICK ||
                             type == SDL_JOYSTICK_TYPE_THROTTLE;
    const bool standardized =
        !specialized && SDL_IsGameController(device_index) == SDL_TRUE;
    SDL_GameController *controller = nullptr;
    SDL_Joystick *joystick = nullptr;
    if (standardized) {
      controller = SDL_GameControllerOpen(device_index);
      if (controller != nullptr) {
        joystick = SDL_GameControllerGetJoystick(controller);
      }
    } else {
      joystick = SDL_JoystickOpen(device_index);
    }
    if (joystick == nullptr) {
      if (controller != nullptr) {
        SDL_GameControllerClose(controller);
      }
      return false;
    }
    const SDL_JoystickID instance_id = SDL_JoystickInstanceID(joystick);
    if (instance_id < 0 || FindDevice(instance_id, existing)) {
      if (controller != nullptr) {
        SDL_GameControllerClose(controller);
      } else {
        SDL_JoystickClose(joystick);
      }
      return instance_id >= 0;
    }

    int axes = SDL_CONTROLLER_AXIS_MAX;
    int buttons = SDL_CONTROLLER_BUTTON_MAX;
    int hats = 0;
    if (!standardized) {
      axes = SDL_JoystickNumAxes(joystick);
      buttons = SDL_JoystickNumButtons(joystick);
      hats = SDL_JoystickNumHats(joystick);
    }
    if (axes < 0 || buttons < 0 || hats < 0) {
      if (controller != nullptr) {
        SDL_GameControllerClose(controller);
      } else {
        SDL_JoystickClose(joystick);
      }
      return false;
    }

    InputDevice candidate;
    candidate.controller = controller;
    candidate.joystick = joystick;
    candidate.instance_id = instance_id;
    candidate.generation = ++next_device_generation;
    candidate.slot = static_cast<std::size_t>(free - input_devices.begin());
    candidate.axis_count = static_cast<std::size_t>((std::min)(
        axes, static_cast<int>(kMaximumAxes)));
    candidate.button_count = static_cast<std::size_t>((std::min)(
        buttons,
        static_cast<int>(kRendererGameMaximumJoystickButtons)));
    candidate.hat_count = static_cast<std::size_t>((std::min)(
        hats, static_cast<int>(kMaximumHats)));
    candidate.standardized = standardized;
    try {
      const char *name = standardized ? SDL_GameControllerName(controller)
                                      : SDL_JoystickName(joystick);
      candidate.vendor = name != nullptr ? name : "unknown";
      RefreshDeviceState(candidate);
      *free = std::move(candidate);
    } catch (...) {
      CloseDevice(candidate);
      throw;
    }
    return true;
  }

  void CloseDeviceById(SDL_JoystickID instance_id) noexcept {
    std::size_t slot = 0U;
    if (FindDevice(instance_id, slot)) {
      CloseDevice(input_devices[slot]);
    }
  }

  bool InitializeControllers() {
    if (controllers_initialized) {
      return true;
    }
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0",
                            SDL_HINT_OVERRIDE);
    owns_controller_subsystem =
        SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0U;
    if (owns_controller_subsystem &&
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
      owns_controller_subsystem = false;
      return false;
    }
    if (SDL_GameControllerEventState(SDL_ENABLE) != SDL_ENABLE ||
        SDL_JoystickEventState(SDL_ENABLE) != SDL_ENABLE) {
      ShutdownControllers();
      return false;
    }
    controllers_initialized = true;
    const int count = SDL_NumJoysticks();
    if (count < 0) {
      ShutdownControllers();
      return false;
    }
    for (int index = 0; index < count; ++index) {
      const std::size_t active = static_cast<std::size_t>(std::count_if(
          input_devices.begin(), input_devices.end(),
          [](const InputDevice &device) { return device.joystick != nullptr; }));
      if (active == input_devices.size()) {
        break;
      }
      // An unsupported or temporarily unavailable physical device must not
      // disable keyboard/mouse or already-open wheels. Hotplug can retry it.
      (void)OpenDevice(index);
    }
    return true;
  }

  RendererOgreNextInProcessPresenterStatus Attach(
      IRendererGameInputTarget &candidate) noexcept {
    if (!prepared || quiesced || target != nullptr ||
        !runtime.ValidateOwnerThread()) {
      return RendererOgreNextInProcessPresenterStatus::REJECTED_LIFECYCLE;
    }
    try {
      if (!InitializeControllers()) {
        return RendererOgreNextInProcessPresenterStatus::
            FAILED_INPUT_ACTIVATION;
      }
    } catch (const std::bad_alloc &) {
      ShutdownControllers();
      return RendererOgreNextInProcessPresenterStatus::FAILED_ALLOCATION;
    } catch (const std::length_error &) {
      ShutdownControllers();
      return RendererOgreNextInProcessPresenterStatus::FAILED_ALLOCATION;
    } catch (...) {
      ShutdownControllers();
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_INPUT_ACTIVATION;
    }
    if (!candidate.ActivateInput()) {
      ShutdownControllers();
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_INPUT_ACTIVATION;
    }
    target = &candidate;
    return RendererOgreNextInProcessPresenterStatus::COMPLETED;
  }

  ValidationResult ApplySurfaceEvents(
      const RendererOgreNextSdlWindowEventBatch &events,
      std::optional<FrontendSurfaceUpdate> &update) {
    RendererOgreNextWindowLifecycle lifecycle = host.Lifecycle();
    if ((events.minimized || events.hidden) &&
        lifecycle == RendererOgreNextWindowLifecycle::ACTIVE) {
      if (host.AdoptExternalVisibility(false) !=
          RendererOgreNextWindowHostStatus::COMPLETED) {
        return Failure(ValidationCode::INVALID_HANDLE,
                       "in_process_presenter.surface",
                       "failed to adopt the externally suspended native "
                       "presentation window");
      }
      const RendererOgreNextWindowMetrics *metrics = host.Metrics();
      if (metrics == nullptr || !ObserveMetrics(*metrics, true)) {
        return Failure(ValidationCode::INVALID_DIMENSIONS,
                       "in_process_presenter.surface",
                       "invalid suspended presentation metrics");
      }
      update = surface;
      return ValidationResult::Success();
    }
    if (!events.minimized && !events.hidden &&
        lifecycle == RendererOgreNextWindowLifecycle::SUSPENDED) {
      if (host.AdoptExternalVisibility(true) !=
          RendererOgreNextWindowHostStatus::COMPLETED) {
        return Failure(ValidationCode::INVALID_HANDLE,
                       "in_process_presenter.surface",
                       "failed to adopt the externally restored native "
                       "presentation window");
      }
      lifecycle = host.Lifecycle();
    }
    if (events.resize_events != 0U &&
        (lifecycle == RendererOgreNextWindowLifecycle::ACTIVE ||
         lifecycle == RendererOgreNextWindowLifecycle::READY_HIDDEN)) {
      if (host.AdoptExternalResize(events.logical_width,
                                   events.logical_height) !=
          RendererOgreNextWindowHostStatus::COMPLETED) {
        return Failure(ValidationCode::INVALID_DIMENSIONS,
                       "in_process_presenter.surface",
                       "native presentation resize adoption failed");
      }
    } else if (events.drawable_size_changed &&
               (lifecycle == RendererOgreNextWindowLifecycle::ACTIVE ||
                lifecycle == RendererOgreNextWindowLifecycle::READY_HIDDEN) &&
               host.RefreshMetrics() !=
                   RendererOgreNextWindowHostStatus::COMPLETED) {
      return Failure(ValidationCode::INVALID_DIMENSIONS,
                     "in_process_presenter.surface",
                     "native drawable metrics refresh failed");
    }
    const RendererOgreNextWindowMetrics *metrics = host.Metrics();
    if (metrics == nullptr) {
      return Failure(ValidationCode::INVALID_DIMENSIONS,
                     "in_process_presenter.surface",
                     "native presentation metrics are unavailable");
    }
    const RendererOgreNextWindowLifecycle observed_lifecycle =
        host.Lifecycle();
    if (observed_lifecycle != RendererOgreNextWindowLifecycle::ACTIVE &&
        observed_lifecycle !=
            RendererOgreNextWindowLifecycle::READY_HIDDEN &&
        observed_lifecycle != RendererOgreNextWindowLifecycle::SUSPENDED) {
      return Failure(ValidationCode::INVALID_HANDLE,
                     "in_process_presenter.surface",
                     "native presentation window entered a failed state");
    }
    // READY_HIDDEN is intentionally renderable: the first N1 Render creates
    // the workspace and invokes ShowAfterWorkspaceReady. Marking it suspended
    // here would deadlock before the only operation that can make it visible.
    const bool suspended = observed_lifecycle ==
                           RendererOgreNextWindowLifecycle::SUSPENDED;
    if (metrics_generation != metrics->generation ||
        surface.suspended != suspended) {
      if (!ObserveMetrics(*metrics, suspended)) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "in_process_presenter.surface_revision",
                       "surface revision overflow");
      }
      update = surface;
    }
    return ValidationResult::Success();
  }

  bool AdvanceInputEvent() noexcept {
    if (next_event_id == 0U ||
        next_event_id == (std::numeric_limits<std::uint64_t>::max)()) {
      return false;
    }
    ++next_event_id;
    return true;
  }

  bool CanAdvanceInputEvent() const noexcept {
    return next_event_id != 0U &&
           next_event_id != (std::numeric_limits<std::uint64_t>::max)();
  }

  ValidationResult AdvanceInputEventOrFailure() {
    return AdvanceInputEvent()
        ? ValidationResult::Success()
        : Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                  "in_process_presenter.input_event_id",
                  "direct input event identity overflow");
  }

  ValidationResult DispatchFocus(bool next_focused) {
    if (focus_observed && input_gate.focused() == next_focused) {
      return ValidationResult::Success();
    }
    ValidationResult reserved = AdvanceInputEventOrFailure();
    if (!reserved) {
      return reserved;
    }
    focus_observed = true;
    input_gate.ObserveFocus(next_focused);
    if (!input_gate.focused()) {
      pressed_keys.clear();
      pressed_mouse_buttons.clear();
      SDL_StopTextInput();
    } else {
      RefreshAllDeviceStates();
      SDL_StartTextInput();
    }
    target->FocusChanged(input_gate.focused());
    return ValidationResult::Success();
  }

  ValidationResult DispatchClose() {
    if (close_requested) {
      return ValidationResult::Success();
    }
    ValidationResult reserved = AdvanceInputEventOrFailure();
    if (!reserved) {
      return reserved;
    }
    close_requested = true;
    target->WindowCloseRequested();
    return ValidationResult::Success();
  }

  ValidationResult BuildJoystickState(RendererGameInputState &state,
                                      bool controls_active) {
    state.joysticks.clear();
    state.joysticks.reserve(input_devices.size());
    for (InputDevice &device : input_devices) {
      if (device.joystick == nullptr) {
        continue;
      }
      RendererGameJoystickState joystick;
      joystick.slot = device.slot;
      joystick.raw_device = !device.standardized;
      joystick.device_id =
          static_cast<std::uint64_t>(
              static_cast<std::uint32_t>(device.instance_id)) +
          1U;
      joystick.connection_generation = device.generation;
      joystick.vendor = device.vendor;
      if (controls_active) {
        joystick.axes_absolute = device.axes;
        joystick.axes_relative = device.relative_axes;
        joystick.buttons = device.buttons;
        joystick.hats = device.hats;
      } else {
        joystick.axes_absolute.assign(device.axis_count, 0);
        joystick.axes_relative.assign(device.axis_count, 0);
        joystick.buttons.assign(device.button_count, false);
        joystick.hats.assign(device.hat_count, 0U);
      }
      state.joysticks.push_back(std::move(joystick));
      std::fill(device.relative_axes.begin(), device.relative_axes.end(), 0);
    }
    return ValidationResult::Success();
  }

  ValidationResult ObservePresentationWindow(
      RendererOgreNextSdlWindowEventBatch &window_events,
      bool reconcile_input_gate) {
    SDL_Window *const presented = static_cast<SDL_Window *>(sdl_window);
    int logical_width = 0;
    int logical_height = 0;
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GetWindowSize(presented, &logical_width, &logical_height);
    SDL_GetWindowSizeInPixels(presented, &drawable_width, &drawable_height);
    const Uint32 flags = SDL_GetWindowFlags(presented);
    window_events.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0U;
    window_events.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0U;
    window_events.hidden = (flags & SDL_WINDOW_HIDDEN) != 0U;
    if (reconcile_input_gate) {
      input_gate.ObserveWindowSuppressed(
          window_events.hidden || window_events.minimized);
    }
    if (logical_width <= 0 || logical_height <= 0 || drawable_width < 0 ||
        drawable_height < 0 ||
        ((drawable_width == 0 || drawable_height == 0) &&
         !window_events.minimized && !window_events.hidden)) {
      return Failure(ValidationCode::INVALID_DIMENSIONS,
                     "in_process_presenter.window_metrics",
                     "SDL reported invalid presentation metrics");
    }
    window_events.logical_width =
        static_cast<std::uint32_t>(logical_width);
    window_events.logical_height =
        static_cast<std::uint32_t>(logical_height);
    window_events.drawable_width =
        static_cast<std::uint32_t>(drawable_width);
    window_events.drawable_height =
        static_cast<std::uint32_t>(drawable_height);
    const RendererOgreNextWindowMetrics *const committed = host.Metrics();
    if (committed == nullptr) {
      return Failure(ValidationCode::INVALID_DIMENSIONS,
                     "in_process_presenter.window_metrics",
                     "native presentation metrics are unavailable");
    }
    if (committed->logical_width != window_events.logical_width ||
        committed->logical_height != window_events.logical_height) {
      window_events.resize_events =
          (std::max)(window_events.resize_events, std::uint64_t{1U});
    }
    if (drawable_width > 0 && drawable_height > 0) {
      window_events.drawable_size_changed =
          has_drawable_baseline &&
          (last_drawable_width != window_events.drawable_width ||
           last_drawable_height != window_events.drawable_height);
      last_drawable_width = window_events.drawable_width;
      last_drawable_height = window_events.drawable_height;
      has_drawable_baseline = true;
    }
    return ValidationResult::Success();
  }

  ValidationResult PollOrderedSdl(
      RendererOgreNextSdlWindowEventBatch &window_events,
      RendererGameInputState &state) {
    if (target == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "in_process_presenter.input_target",
                     "direct game input target is not attached");
    }
    const Uint32 presented_window_id =
        SDL_GetWindowID(static_cast<SDL_Window *>(sdl_window));
    if (presented_window_id == 0U) {
      return Failure(ValidationCode::INVALID_HANDLE,
                     "in_process_presenter.input_window",
                     "SDL presentation window identity is invalid");
    }
    const Uint32 protected_window_id = protected_hidden_window == nullptr
        ? 0U
        : SDL_GetWindowID(
              static_cast<SDL_Window *>(protected_hidden_window));
    if (protected_hidden_window != nullptr && protected_window_id == 0U) {
      return Failure(ValidationCode::INVALID_HANDLE,
                     "in_process_presenter.hidden_window",
                     "protected SDL resource window identity is invalid");
    }

    window_events = RendererOgreNextSdlWindowEventBatch{};
    for (InputDevice &device : input_devices) {
      std::fill(device.relative_axes.begin(), device.relative_axes.end(), 0);
    }
    SDL_PumpEvents();
    int logical_width = 0;
    int logical_height = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    SDL_GetWindowSize(static_cast<SDL_Window *>(sdl_window), &logical_width,
                      &logical_height);
    SDL_GetWindowSizeInPixels(static_cast<SDL_Window *>(sdl_window),
                              &pixel_width, &pixel_height);
    const RendererOgreNextWindowMetrics *const committed_metrics =
        host.Metrics();
    RendererGameDisplayMetrics game_metrics;
    game_metrics.logical_width = logical_width > 0
        ? static_cast<std::uint32_t>(logical_width)
        : committed_metrics == nullptr ? 0U : committed_metrics->logical_width;
    game_metrics.logical_height = logical_height > 0
        ? static_cast<std::uint32_t>(logical_height)
        : committed_metrics == nullptr ? 0U : committed_metrics->logical_height;
    // Minimized Cocoa windows may report a zero drawable. Keep the last
    // committed nonzero backing domain while input is suppressed; restore or
    // resize will publish the newly queried domain before callbacks resume.
    game_metrics.pixel_width = pixel_width > 0
        ? static_cast<std::uint32_t>(pixel_width)
        : committed_metrics == nullptr ? 0U : committed_metrics->drawable_width;
    game_metrics.pixel_height = pixel_height > 0
        ? static_cast<std::uint32_t>(pixel_height)
        : committed_metrics == nullptr ? 0U : committed_metrics->drawable_height;
    if (!game_metrics.valid() ||
        !target->DisplayMetricsChanged(game_metrics)) {
      return Failure(ValidationCode::INVALID_DIMENSIONS,
                     "in_process_presenter.game_display_metrics",
                     "game input target rejected the visible presentation "
                     "coordinate domains");
    }
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      ++window_events.polled_events;
      if (event.type == SDL_QUIT) {
        ++window_events.close_events;
        window_events.close_requested = true;
        ValidationResult result = DispatchClose();
        if (!result) {
          return result;
        }
        continue;
      }
      if (event.type == SDL_WINDOWEVENT) {
        if (event.window.windowID == protected_window_id &&
            (event.window.event == SDL_WINDOWEVENT_SHOWN ||
             event.window.event == SDL_WINDOWEVENT_RESTORED ||
             event.window.event == SDL_WINDOWEVENT_MAXIMIZED)) {
          SDL_HideWindow(
              static_cast<SDL_Window *>(protected_hidden_window));
          continue;
        }
        if (event.window.windowID != presented_window_id) {
          continue;
        }
        ++window_events.matched_window_events;
        ValidationResult transition = ValidationResult::Success();
        switch (event.window.event) {
        case SDL_WINDOWEVENT_CLOSE:
          ++window_events.close_events;
          window_events.close_requested = true;
          transition = DispatchClose();
          break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
          ++window_events.focus_gained_events;
          transition = DispatchFocus(true);
          break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
          ++window_events.focus_lost_events;
          transition = DispatchFocus(false);
          break;
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
          ++window_events.resize_events;
          break;
        case SDL_WINDOWEVENT_MINIMIZED:
          ++window_events.minimize_events;
          input_gate.ObserveWindowSuppressed(true);
          transition = DispatchFocus(false);
          break;
        case SDL_WINDOWEVENT_HIDDEN:
          input_gate.ObserveWindowSuppressed(true);
          transition = DispatchFocus(false);
          break;
        case SDL_WINDOWEVENT_SHOWN:
          input_gate.ObserveWindowSuppressed(false);
          break;
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_MAXIMIZED:
          ++window_events.restore_events;
          input_gate.ObserveWindowSuppressed(false);
          break;
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
          ++window_events.display_change_events;
          break;
        default:
          break;
        }
        if (!transition) {
          return transition;
        }
        continue;
      }
      if (event.type == SDL_JOYDEVICEADDED) {
        const SDL_JoystickID advertised =
            SDL_JoystickGetDeviceInstanceID(event.jdevice.which);
        std::size_t slot = 0U;
        if (advertised >= 0 && FindDevice(advertised, slot)) {
          continue;
        }
        const bool full = std::none_of(
            input_devices.begin(), input_devices.end(),
            [](const InputDevice &device) {
              return device.joystick == nullptr;
            });
        if (full) {
          continue;
        }
        if (!CanAdvanceInputEvent()) {
          return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                         "in_process_presenter.input_event_id",
                         "direct input event identity overflow");
        }
        if (OpenDevice(event.jdevice.which)) {
          (void)AdvanceInputEvent();
        }
        continue;
      }
      if (event.type == SDL_JOYDEVICEREMOVED ||
          event.type == SDL_CONTROLLERDEVICEREMOVED) {
        const SDL_JoystickID instance_id = event.type == SDL_JOYDEVICEREMOVED
            ? event.jdevice.which
            : event.cdevice.which;
        std::size_t slot = 0U;
        if (!FindDevice(instance_id, slot)) {
          continue;
        }
        ValidationResult reserved = AdvanceInputEventOrFailure();
        if (!reserved) {
          return reserved;
        }
        CloseDevice(input_devices[slot]);
        continue;
      }
      if (event.type == SDL_CONTROLLERDEVICEADDED) {
        // SDL also emits JOYDEVICEADDED for this physical device; the raw
        // family owns deterministic open/slot assignment.
        continue;
      }
      if (event.type == SDL_CONTROLLERDEVICEREMAPPED) {
        std::size_t slot = 0U;
        if (!FindDevice(event.cdevice.which, slot) ||
            !input_devices[slot].standardized) {
          continue;
        }
        ValidationResult reserved = AdvanceInputEventOrFailure();
        if (!reserved) {
          return reserved;
        }
        RefreshDeviceState(input_devices[slot]);
        continue;
      }
      if (event.type == SDL_JOYAXISMOTION ||
          event.type == SDL_JOYBUTTONDOWN ||
          event.type == SDL_JOYBUTTONUP ||
          event.type == SDL_JOYHATMOTION ||
          event.type == SDL_CONTROLLERAXISMOTION ||
          event.type == SDL_CONTROLLERBUTTONDOWN ||
          event.type == SDL_CONTROLLERBUTTONUP) {
        SDL_JoystickID instance_id = -1;
        std::size_t component = 0U;
        std::int32_t value = 0;
        enum class Component { AXIS, BUTTON, HAT } component_kind =
            Component::AXIS;
        const bool standardized_event =
            event.type == SDL_CONTROLLERAXISMOTION ||
            event.type == SDL_CONTROLLERBUTTONDOWN ||
            event.type == SDL_CONTROLLERBUTTONUP;
        switch (event.type) {
        case SDL_JOYAXISMOTION:
          instance_id = event.jaxis.which;
          component = event.jaxis.axis;
          value = event.jaxis.value;
          component_kind = Component::AXIS;
          break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
          instance_id = event.jbutton.which;
          component = event.jbutton.button;
          value = event.jbutton.state;
          component_kind = Component::BUTTON;
          break;
        case SDL_JOYHATMOTION:
          instance_id = event.jhat.which;
          component = event.jhat.hat;
          value = event.jhat.value;
          component_kind = Component::HAT;
          break;
        case SDL_CONTROLLERAXISMOTION:
          instance_id = event.caxis.which;
          component = event.caxis.axis;
          value = event.caxis.value;
          component_kind = Component::AXIS;
          break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
          instance_id = event.cbutton.which;
          component = event.cbutton.button;
          value = event.cbutton.state;
          component_kind = Component::BUTTON;
          break;
        default:
          break;
        }
        std::size_t slot = 0U;
        if (!input_gate.AcceptsPhysicalInput() ||
            !FindDevice(instance_id, slot)) {
          continue;
        }
        InputDevice &device = input_devices[slot];
        if (device.standardized != standardized_event) {
          continue;
        }
        bool valid = false;
        switch (component_kind) {
        case Component::AXIS:
          valid = component < device.axes.size() && value >= -32768 &&
                  value <= 32767;
          break;
        case Component::BUTTON:
          valid = component < device.buttons.size() &&
                  (value == 0 || value == 1);
          break;
        case Component::HAT: {
          const bool opposite_vertical =
              (value & 1) != 0 && (value & 4) != 0;
          const bool opposite_horizontal =
              (value & 2) != 0 && (value & 8) != 0;
          valid = component < device.hats.size() && value >= 0 &&
                  (value & ~0x0f) == 0 && !opposite_vertical &&
                  !opposite_horizontal;
          break;
        }
        }
        if (!valid) {
          continue;
        }
        ValidationResult reserved = AdvanceInputEventOrFailure();
        if (!reserved) {
          return reserved;
        }
        switch (component_kind) {
        case Component::AXIS: {
          const std::int32_t previous = device.axes[component];
          device.axes[component] = value;
          device.relative_axes[component] = SaturatingAdd(
              device.relative_axes[component], value - previous);
          break;
        }
        case Component::BUTTON:
          device.buttons[component] = value != 0;
          break;
        case Component::HAT:
          device.hats[component] = static_cast<std::uint8_t>(value);
          break;
        }
        continue;
      }

      bool belongs_to_presenter = false;
      switch (event.type) {
      case SDL_KEYDOWN:
      case SDL_KEYUP:
        belongs_to_presenter = event.key.windowID == presented_window_id;
        break;
      case SDL_TEXTINPUT:
        belongs_to_presenter = event.text.windowID == presented_window_id;
        break;
      case SDL_MOUSEMOTION:
        belongs_to_presenter = event.motion.windowID == presented_window_id;
        break;
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP:
        belongs_to_presenter = event.button.windowID == presented_window_id;
        break;
      case SDL_MOUSEWHEEL:
        belongs_to_presenter = event.wheel.windowID == presented_window_id;
        break;
      default:
        break;
      }
      if (!belongs_to_presenter ||
          !input_gate.AcceptsKeyboardTextMouse()) {
        continue;
      }
      switch (event.type) {
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        if (event.type == SDL_KEYDOWN && event.key.repeat != 0U) {
          break;
        }
        const RendererGameKey key = TranslateRendererSdlScancodeToGame(
            static_cast<std::uint16_t>(event.key.keysym.scancode));
        if (key == RendererGameKey::UNASSIGNED) {
          break;
        }
        ValidationResult result = AdvanceInputEventOrFailure();
        if (!result) {
          return result;
        }
        const bool down = event.type == SDL_KEYDOWN;
        SetPressed(pressed_keys, key, down);
        target->KeyChanged(key, down);
        break;
      }
      case SDL_TEXTINPUT:
        if (event.text.text[0] != '\0') {
          ValidationResult result = AdvanceInputEventOrFailure();
          if (!result) {
            return result;
          }
          target->TextInput(event.text.text);
        }
        break;
      case SDL_MOUSEMOTION: {
        const float content_scale_x =
            static_cast<float>(game_metrics.pixel_width) /
            static_cast<float>(game_metrics.logical_width);
        const float content_scale_y =
            static_cast<float>(game_metrics.pixel_height) /
            static_cast<float>(game_metrics.logical_height);
        const std::int32_t delta_x =
            ScaledPixels(event.motion.xrel, content_scale_x);
        const std::int32_t delta_y =
            ScaledPixels(event.motion.yrel, content_scale_y);
        ValidationResult result = AdvanceInputEventOrFailure();
        if (!result) {
          return result;
        }
        mouse_x_pixels =
            ScaledPixels(event.motion.x, content_scale_x);
        mouse_y_pixels =
            ScaledPixels(event.motion.y, content_scale_y);
        state.mouse_delta_x_pixels =
            SaturatingAdd(state.mouse_delta_x_pixels, delta_x);
        state.mouse_delta_y_pixels =
            SaturatingAdd(state.mouse_delta_y_pixels, delta_y);
        target->MouseMoved(mouse_x_pixels, mouse_y_pixels, delta_x, delta_y);
        break;
      }
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        RendererGameMouseButton button = RendererGameMouseButton::LEFT;
        if (!TryTranslateRendererSdlMouseButtonToGame(event.button.button,
                                                      button)) {
          break;
        }
        ValidationResult result = AdvanceInputEventOrFailure();
        if (!result) {
          return result;
        }
        const bool down = event.type == SDL_MOUSEBUTTONDOWN;
        SetPressed(pressed_mouse_buttons, button, down);
        target->MouseButtonChanged(button, down);
        break;
      }
      case SDL_MOUSEWHEEL: {
        double delta_x = event.wheel.preciseX;
        double delta_y = event.wheel.preciseY;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
          delta_x = -delta_x;
          delta_y = -delta_y;
        }
        const float wheel_x = SaturatingWheel(delta_x);
        const float wheel_y = SaturatingWheel(delta_y);
        ValidationResult result = AdvanceInputEventOrFailure();
        if (!result) {
          return result;
        }
        state.wheel_delta_x = SaturatingWheel(
            static_cast<double>(state.wheel_delta_x) + wheel_x);
        state.wheel_delta_y = SaturatingWheel(
            static_cast<double>(state.wheel_delta_y) + wheel_y);
        target->MouseWheel(wheel_x, wheel_y);
        break;
      }
      default:
        break;
      }
    }

    ValidationResult observed =
        ObservePresentationWindow(window_events, true);
    if (!observed) {
      return observed;
    }
    ValidationResult focus = DispatchFocus(
        window_events.focused && !input_gate.window_suppressed());
    if (!focus) {
      return focus;
    }
    if (window_events.close_requested) {
      ValidationResult close = DispatchClose();
      if (!close) {
        return close;
      }
    }

    state.through_event_id = next_event_id - 1U;
    state.focused = input_gate.focused();
    state.window_close_requested = close_requested;
    state.pressed_keys = pressed_keys;
    state.pressed_mouse_buttons = pressed_mouse_buttons;
    state.mouse_x_pixels = mouse_x_pixels;
    state.mouse_y_pixels = mouse_y_pixels;
    ValidationResult devices = BuildJoystickState(
        state, input_gate.AcceptsPhysicalInput());
    if (!devices) {
      return devices;
    }
    if (!target->Reconcile(state)) {
      return Failure(ValidationCode::INVALID_HANDLE,
                     "in_process_presenter.input_reconcile",
                     "game input target rejected authoritative state");
    }
    return ValidationResult::Success();
  }

  ValidationResult Poll(RendererInProcessEventPollPoint point,
                        RendererInProcessEventObservation &observation) {
    if (!prepared || quiesced || target == nullptr || frontend == nullptr ||
        !IsKnownPollPoint(point) || !runtime.ValidateOwnerThread()) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "in_process_presenter.lifecycle",
                     "event poll is outside the active presenter lifetime");
    }
    observation = RendererInProcessEventObservation{};
    RendererOgreNextSdlWindowEventBatch events;
    ValidationResult polled = ValidationResult::Success();
    if (point == RendererInProcessEventPollPoint::BEFORE_SIMULATION) {
      RendererGameInputState state;
      polled = PollOrderedSdl(events, state);
    } else {
      // The second poll protects surface/camera consistency after capture, but
      // it must not consume relative input before the next InputEngine frame.
      // Pump SDL so Cocoa metrics are current, leave the FIFO untouched, and
      // observe only authoritative window state.
      SDL_PumpEvents();
      polled = ObservePresentationWindow(events, false);
    }
    if (!polled) {
      return polled;
    }
    std::optional<FrontendSurfaceUpdate> callback_acknowledged_surface;
    if (pending_surface_notification.has_value()) {
      callback_acknowledged_surface = pending_surface_notification;
      observation.surface_update = pending_surface_notification;
      pending_surface_notification.reset();
    }
    ValidationResult surface_result =
        ApplySurfaceEvents(events, observation.surface_update);
    if (!surface_result) {
      return surface_result;
    }
    observation.surface_update_already_committed_to_frontend =
        callback_acknowledged_surface.has_value() &&
        observation.surface_update.has_value() &&
        SameSurface(*callback_acknowledged_surface,
                    *observation.surface_update);
    observation.shutdown_requested = close_requested;
    return ValidationResult::Success();
  }

  void Quiesce() noexcept {
    if (quiesced || !prepared || !runtime.ValidateOwnerThread()) {
      return;
    }
    SDL_StopTextInput();
    ShutdownControllers();
    target = nullptr;
    quiesced = true;
  }

  RendererOgreNextInProcessPresenterStatus Shutdown() noexcept {
    if (!prepared || !quiesced || protected_hidden_window != nullptr ||
        !runtime.ValidateOwnerThread()) {
      return RendererOgreNextInProcessPresenterStatus::REJECTED_LIFECYCLE;
    }
    frontend.reset();
    native_frontend = nullptr;
#if defined(__APPLE__)
    native_sun_visibility_frontend = nullptr;
#endif
    RendererOgreNextWindowHostStatus shutdown = host.Shutdown();
    if (shutdown != RendererOgreNextWindowHostStatus::COMPLETED) {
      shutdown = host.Shutdown();
    }
    if (shutdown != RendererOgreNextWindowHostStatus::COMPLETED) {
      return RendererOgreNextInProcessPresenterStatus::
          FAILED_WINDOW_SHUTDOWN;
    }
    prepared = false;
    sdl_window = nullptr;
    window = NativeWindowHandle{};
    surface = FrontendSurfaceUpdate{};
    return RendererOgreNextInProcessPresenterStatus::COMPLETED;
  }

  RendererOgreNextSdlWindowRuntime runtime;
  RendererOgreNextWindowHost host;
  std::unique_ptr<IRendererFrontend> frontend;
  OgreNextN1Frontend *native_frontend = nullptr;
#if defined(__APPLE__)
  NativeSunVisibilityV2ProductionFrontend *native_sun_visibility_frontend =
      nullptr;
#endif
  RendererOgreNextInProcessPresenterConfiguration configuration;
  FrontendInitializationRequest initialization;
  FrontendSurfaceUpdate surface;
  std::optional<FrontendSurfaceUpdate> pending_surface_notification;
  NativeWindowHandle window;
  IRendererGameInputTarget *target = nullptr;
  void *sdl_window = nullptr;
  void *protected_hidden_window = nullptr;
  std::uint64_t surface_revision = 0U;
  std::uint64_t metrics_generation = 0U;
  std::uint64_t next_event_id = 1U;
  std::uint64_t next_device_generation = 0U;
  Detail::RendererOgreNextInProcessInputGate input_gate;
  std::array<InputDevice, kRendererGameJoystickSlots> input_devices{};
  std::vector<RendererGameKey> pressed_keys;
  std::vector<RendererGameMouseButton> pressed_mouse_buttons;
  std::int32_t mouse_x_pixels = 0;
  std::int32_t mouse_y_pixels = 0;
  std::uint32_t last_drawable_width = 0U;
  std::uint32_t last_drawable_height = 0U;
  bool has_drawable_baseline = false;
  bool owns_controller_subsystem = false;
  bool controllers_initialized = false;
  bool focus_observed = false;
  bool close_requested = false;
  bool prepared = false;
  bool quiesced = false;
};

RendererOgreNextInProcessPresenter::RendererOgreNextInProcessPresenter()
    : impl_(std::make_unique<Impl>()) {}

RendererOgreNextInProcessPresenter::~RendererOgreNextInProcessPresenter() =
    default;

RendererOgreNextInProcessPresenterStatus
RendererOgreNextInProcessPresenter::PrepareWindow(
    const RendererOgreNextInProcessPresenterConfiguration &configuration)
    noexcept {
  try {
    return impl_->Prepare(configuration);
  } catch (const std::bad_alloc &) {
    impl_->frontend.reset();
    (void)impl_->host.Shutdown();
    return RendererOgreNextInProcessPresenterStatus::FAILED_ALLOCATION;
  } catch (...) {
    impl_->frontend.reset();
    (void)impl_->host.Shutdown();
    return RendererOgreNextInProcessPresenterStatus::FAILED_INTERNAL;
  }
}

RendererOgreNextInProcessPresenterStatus
RendererOgreNextInProcessPresenter::ProtectHiddenResourceWindow(
    void *sdl_window) noexcept {
  return impl_->ProtectHidden(sdl_window);
}

RendererOgreNextInProcessPresenterStatus
RendererOgreNextInProcessPresenter::AttachInputTarget(
    IRendererGameInputTarget &target) noexcept {
  return impl_->Attach(target);
}

IRendererFrontend *RendererOgreNextInProcessPresenter::Frontend() noexcept {
  return impl_->frontend.get();
}

const IRendererFrontend *
RendererOgreNextInProcessPresenter::Frontend() const noexcept {
  return impl_->frontend.get();
}

FrontendInitializationRequest
RendererOgreNextInProcessPresenter::InitialFrontendRequest() const noexcept {
  return impl_->initialization;
}

FrontendSurfaceUpdate
RendererOgreNextInProcessPresenter::CurrentSurface() const noexcept {
  return impl_->surface;
}

RendererContinuousParticleAudit
RendererOgreNextInProcessPresenter::ContinuousParticleAudit() const noexcept {
  return impl_->ContinuousParticleAudit();
}

RendererAnalyticSkyAudit
RendererOgreNextInProcessPresenter::AnalyticSkyAudit() const noexcept {
  return impl_->AnalyticSkyAudit();
}

RendererNativeLightingAudit
RendererOgreNextInProcessPresenter::NativeLightingAudit() const noexcept {
  return impl_->NativeLightingAudit();
}

RendererRetainedSceneAudit
RendererOgreNextInProcessPresenter::RetainedSceneAudit() const noexcept {
  return impl_->RetainedSceneAudit();
}

RendererRenderBoundaryDegradeAudit
RendererOgreNextInProcessPresenter::RenderBoundaryDegradeAudit() const
    noexcept {
  return impl_->RenderBoundaryDegradeAudit();
}

RendererNativeSunVisibilityV2Audit
RendererOgreNextInProcessPresenter::NativeSunVisibilityV2Audit() const
    noexcept {
  return impl_->NativeSunVisibilityAudit();
}

RendererUiOverlayPresentationAudit
RendererOgreNextInProcessPresenter::UiOverlayPresentationAudit() const
    noexcept {
  return impl_->UiOverlayPresentationAudit();
}

ValidationResult RendererOgreNextInProcessPresenter::PollEvents(
    RendererInProcessEventPollPoint point,
    RendererInProcessEventObservation &observation) {
  return impl_->Poll(point, observation);
}

void RendererOgreNextInProcessPresenter::ShutdownEventPump() noexcept {
  impl_->Quiesce();
}

RendererOgreNextInProcessPresenterStatus
RendererOgreNextInProcessPresenter::ShutdownWindow() noexcept {
  return impl_->Shutdown();
}

bool RendererOgreNextInProcessPresenter::prepared() const noexcept {
  return impl_->prepared;
}

bool RendererOgreNextInProcessPresenter::input_attached() const noexcept {
  return impl_->target != nullptr;
}

bool RendererOgreNextInProcessPresenter::quiesced() const noexcept {
  return impl_->quiesced;
}

bool IsKnownRendererOgreNextInProcessPresenterStatus(
    RendererOgreNextInProcessPresenterStatus status) noexcept {
  switch (status) {
  case RendererOgreNextInProcessPresenterStatus::COMPLETED:
  case RendererOgreNextInProcessPresenterStatus::REJECTED_CONFIGURATION:
  case RendererOgreNextInProcessPresenterStatus::REJECTED_LIFECYCLE:
  case RendererOgreNextInProcessPresenterStatus::FAILED_WINDOW_INITIALIZATION:
  case RendererOgreNextInProcessPresenterStatus::FAILED_FRONTEND_CONFIGURATION:
  case RendererOgreNextInProcessPresenterStatus::FAILED_INPUT_ACTIVATION:
  case RendererOgreNextInProcessPresenterStatus::FAILED_WINDOW_SHUTDOWN:
  case RendererOgreNextInProcessPresenterStatus::FAILED_ALLOCATION:
  case RendererOgreNextInProcessPresenterStatus::FAILED_INTERNAL:
    return true;
  }
  return false;
}

const char *ToString(
    RendererOgreNextInProcessPresenterStatus status) noexcept {
  switch (status) {
  case RendererOgreNextInProcessPresenterStatus::COMPLETED:
    return "completed";
  case RendererOgreNextInProcessPresenterStatus::REJECTED_CONFIGURATION:
    return "rejected-configuration";
  case RendererOgreNextInProcessPresenterStatus::REJECTED_LIFECYCLE:
    return "rejected-lifecycle";
  case RendererOgreNextInProcessPresenterStatus::FAILED_WINDOW_INITIALIZATION:
    return "failed-window-initialization";
  case RendererOgreNextInProcessPresenterStatus::FAILED_FRONTEND_CONFIGURATION:
    return "failed-frontend-configuration";
  case RendererOgreNextInProcessPresenterStatus::FAILED_INPUT_ACTIVATION:
    return "failed-input-activation";
  case RendererOgreNextInProcessPresenterStatus::FAILED_WINDOW_SHUTDOWN:
    return "failed-window-shutdown";
  case RendererOgreNextInProcessPresenterStatus::FAILED_ALLOCATION:
    return "failed-allocation";
  case RendererOgreNextInProcessPresenterStatus::FAILED_INTERNAL:
    return "failed-internal";
  }
  return "unknown";
}

} // namespace RoR
