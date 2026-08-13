/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "NativeVisualShowcaseSceneSource.h"

#include <fstream>
#include <limits>
#include <new>
#include <utility>
#include <variant>
#include <vector>

namespace RoR::Render {
namespace {

constexpr char kGateMeshDebugName[] = "rorng_a0_road_shadow_gate_mesh";
constexpr std::uint64_t kMaximumShowcaseSimulationTick =
    60ULL * 60ULL * 24ULL * 366ULL * 100ULL;

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

bool IsKnownGatePose(NativeVisualShowcaseGatePose pose) noexcept {
  switch (pose) {
  case NativeVisualShowcaseGatePose::HOME:
  case NativeVisualShowcaseGatePose::MOVED:
    return true;
  }
  return false;
}

Matrix4x4 MakeShowcaseProjection() noexcept {
  constexpr float kNearPlane = 0.1F;
  constexpr float kFarPlane = 50.0F;
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  // Exact binary32 constants derived from the checked composition's 50-degree
  // vertical field of view and the source's fixed 1920x1080 extent.
  projection.elements[0U] = 1.2062851190567017F;
  projection.elements[5U] = 2.1445069313049316F;
  const float depth_scale = kFarPlane / (kNearPlane - kFarPlane);
  projection.elements[10U] = depth_scale;
  projection.elements[11U] = -1.0F;
  projection.elements[14U] = kNearPlane * depth_scale;
  return projection;
}

GraphicsSceneFrameInput MakeBaseFrame(const NativeRenderAssetPackage &package) {
  GraphicsSceneFrameInput frame;
  frame.assets = package.assets;
  frame.static_meshes = package.static_meshes;

  frame.environment.ambient_radiance = {0.025F, 0.03F, 0.045F};
  frame.environment.environment_intensity = 1.0F;
  frame.environment.exposure_compensation_ev = 0.0F;
  frame.environment.analytic_sky.enabled = true;
  frame.environment.analytic_sky.sun_light_id = kNativeVisualShowcaseSunLightId;
  frame.environment.analytic_sky.zenith_radiance = {0.08F, 0.12F, 0.2F};
  frame.environment.analytic_sky.horizon_radiance = {0.3F, 0.24F, 0.18F};
  frame.environment.analytic_sky.ground_radiance = {0.01F, 0.009F, 0.008F};
  frame.environment.analytic_sky.sun_disk_radiance = {24.0F, 20.0F, 16.0F};
  frame.environment.analytic_sky.sun_angular_radius_radians = 0.00465047F;

  GraphicsSceneLightInput sun;
  sun.source_light_id = kNativeVisualShowcaseSunLightId;
  sun.type = LightType::DIRECTIONAL;
  sun.color_linear = {1.0F, 1.0F, 1.0F};
  sun.intensity = 110000.0F;
  sun.position = {};
  sun.direction = {0.6F, -0.64F, 0.48F};
  sun.range = 0.0F;
  sun.inner_cone_radians = 0.0F;
  sun.outer_cone_radians = 0.0F;
  sun.shadow_flags = LIGHT_SHADOW_DEFAULT_FLAGS;
  frame.lights.push_back(sun);

  frame.camera.view_id = kNativeVisualShowcaseCameraViewId;
  frame.camera.width = 1920U;
  frame.camera.height = 1080U;
  // Exact binary32 RH look-at transform derived from the checked composition:
  // eye (8,7,10), target (0,0,-0.2), up (0,1,0).
  frame.camera.view_from_render.elements = {{
      0.7868534326553345F,
      -0.2932322919368744F,
      0.5430253148078918F,
      0.0F,
      0.0F,
      0.8799063563346863F,
      0.47514718770980835F,
      0.0F,
      -0.6171399354934692F,
      -0.37387117743492126F,
      0.6923573017120361F,
      0.0F,
      -0.12342798709869385F,
      -0.07477423548698425F,
      -14.593806266784668F,
      1.0F,
  }};
  frame.camera.clip_from_view = MakeShowcaseProjection();
  frame.camera.temporal_jitter_pixels = {};
  frame.camera.near_plane = 0.1F;
  frame.camera.far_plane = 50.0F;
  frame.camera.exposure = 1.0F;
  frame.camera.visibility_mask = 0xFFFFFFFFU;
  return frame;
}

ValidationResult FindGateInstance(const NativeRenderAssetPackage &package,
                                  std::size_t &gate_instance_index) {
  std::uint64_t gate_mesh_source_id = 0U;
  for (const GraphicsSceneAssetInput &asset : package.assets) {
    const auto *mesh =
        asset.payload != nullptr
            ? std::get_if<MeshResourceDescriptor>(asset.payload.get())
            : nullptr;
    if (mesh == nullptr || mesh->debug_name != kGateMeshDebugName) {
      continue;
    }
    if (gate_mesh_source_id != 0U) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "native_showcase.gate_mesh",
                     "package contains more than one authored gate mesh");
    }
    gate_mesh_source_id = asset.source_asset_id;
  }
  if (gate_mesh_source_id == 0U) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "native_showcase.gate_mesh",
                   "package does not contain the authored gate mesh");
  }

  bool found_instance = false;
  for (std::size_t index = 0U; index < package.static_meshes.size(); ++index) {
    if (package.static_meshes[index].mesh_source_asset_id !=
        gate_mesh_source_id) {
      continue;
    }
    if (found_instance) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "native_showcase.gate_instance",
                     "package instantiates the authored gate more than once");
    }
    gate_instance_index = index;
    found_instance = true;
  }
  if (!found_instance) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "native_showcase.gate_instance",
                   "package does not instantiate the authored gate");
  }
  return ValidationResult::Success();
}

ValidationResult ReadPackageOnce(const std::string &package_path,
                                 std::vector<std::uint8_t> &bytes) {
  if (package_path.empty()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "native_showcase.package_path",
                   "native showcase package path is empty");
  }
  std::ifstream stream(package_path, std::ios::binary | std::ios::ate);
  if (!stream) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "native_showcase.package_path",
                   "native showcase package could not be opened");
  }
  const std::streamoff end = stream.tellg();
  if (end <= 0) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "native_showcase.package_bytes",
                   "native showcase package is empty or unreadable");
  }
  const auto byte_count = static_cast<std::uint64_t>(end);
  if (byte_count > kMaximumNativeRenderAssetPackageBytes ||
      byte_count > static_cast<std::uint64_t>(
                       (std::numeric_limits<std::size_t>::max)()) ||
      byte_count > static_cast<std::uint64_t>(
                       (std::numeric_limits<std::streamsize>::max)())) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "native_showcase.package_bytes",
                   "native showcase package exceeds the decoder bound");
  }
  stream.seekg(0, std::ios::beg);
  if (!stream) {
    return Failure(ValidationCode::SIZE_MISMATCH,
                   "native_showcase.package_bytes",
                   "native showcase package could not be rewound");
  }
  bytes.resize(static_cast<std::size_t>(byte_count));
  stream.read(reinterpret_cast<char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!stream ||
      stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
    bytes.clear();
    return Failure(
        ValidationCode::SIZE_MISMATCH, "native_showcase.package_bytes",
        "native showcase package changed or truncated while reading");
  }
  return ValidationResult::Success();
}

} // namespace

NativeVisualShowcaseSceneSource::NativeVisualShowcaseSceneSource(
    std::shared_ptr<const NativeRenderAssetPackage> package,
    std::string package_path, std::size_t gate_instance_index,
    GraphicsSceneFrameInput base_frame)
    : package_(std::move(package)), package_path_(std::move(package_path)),
      base_frame_(std::move(base_frame)),
      gate_instance_index_(gate_instance_index),
      gate_source_object_id_(
          base_frame_.static_meshes[gate_instance_index_].source_object_id) {}

NativeVisualShowcaseSceneSourceLoadResult
LoadNativeVisualShowcaseSceneSource(const std::string &package_path) noexcept {
  NativeVisualShowcaseSceneSourceLoadResult result;
  try {
    std::vector<std::uint8_t> bytes;
    result.validation = ReadPackageOnce(package_path, bytes);
    if (!result.validation) {
      return result;
    }
    NativeRenderAssetPackageDecodeResult decoded =
        DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                       kNativeVisualShowcasePackageSha256);
    if (!decoded.ok()) {
      result.validation = std::move(decoded.validation);
      return result;
    }
    if (decoded.package->package_sha256 != kNativeVisualShowcasePackageSha256 ||
        decoded.package->package_id != kNativeVisualShowcasePackageId ||
        decoded.package->origin_class != "project_original") {
      result.validation = Failure(ValidationCode::REVISION_MISMATCH,
                                  "native_showcase.package_checkpoint",
                                  "decoded package does not match the reviewed "
                                  "project-original checkpoint");
      return result;
    }

    std::size_t gate_instance_index = 0U;
    result.validation = FindGateInstance(*decoded.package, gate_instance_index);
    if (!result.validation) {
      return result;
    }
    GraphicsSceneFrameInput base_frame = MakeBaseFrame(*decoded.package);
    result.source = std::unique_ptr<NativeVisualShowcaseSceneSource>(
        new NativeVisualShowcaseSceneSource(std::move(decoded.package),
                                            package_path, gate_instance_index,
                                            std::move(base_frame)));
    result.validation = ValidationResult::Success();
    return result;
  } catch (const std::bad_alloc &) {
    result.source.reset();
    result.validation.code = ValidationCode::SIZE_MISMATCH;
    result.validation.element_index = ValidationResult::kNoElement;
    result.validation.field.clear();
    result.validation.detail.clear();
    return result;
  } catch (...) {
    result.source.reset();
    result.validation.code = ValidationCode::UNSUPPORTED_FEATURE;
    result.validation.element_index = ValidationResult::kNoElement;
    result.validation.field.clear();
    result.validation.detail.clear();
    return result;
  }
}

ValidationResult NativeVisualShowcaseSceneSource::SetGatePose(
    NativeVisualShowcaseGatePose pose) {
  if (!IsKnownGatePose(pose)) {
    return Failure(ValidationCode::INVALID_ENUM, "native_showcase.gate_pose",
                   "unknown native showcase gate pose");
  }
  if (capture_pending_) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "native_showcase.capture_transaction",
                   "gate pose cannot change while a capture is pending");
  }
  requested_gate_pose_ = pose;
  return ValidationResult::Success();
}

ValidationResult NativeVisualShowcaseSceneSource::CaptureJoinedGraphicsFrame(
    GraphicsSceneFrameInput &frame) {
  if (capture_pending_) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "native_showcase.capture_transaction",
                   "a prior native showcase capture is still pending");
  }
  if (simulation_exhausted_) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "native_showcase.simulation_tick",
                   "native showcase simulation lineage is exhausted");
  }
  if (capture_count_ == (std::numeric_limits<std::uint64_t>::max)()) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "native_showcase.capture_count",
                   "native showcase capture counter is exhausted");
  }

  GraphicsSceneFrameInput candidate = base_frame_;
  candidate.simulation_tick = next_simulation_tick_;
  candidate.simulation_time_seconds =
      static_cast<double>(next_simulation_tick_) *
      kNativeVisualShowcaseFixedStepSeconds;
  if (requested_gate_pose_ == NativeVisualShowcaseGatePose::MOVED) {
    candidate.static_meshes[gate_instance_index_]
        .render_from_object.elements[12U] +=
        kNativeVisualShowcaseMovedGateOffsetMeters;
  }

  frame = std::move(candidate);
  pending_gate_pose_ = requested_gate_pose_;
  capture_pending_ = true;
  ++capture_count_;
  return ValidationResult::Success();
}

void NativeVisualShowcaseSceneSource::CommitJoinedGraphicsFrame() noexcept {
  if (!capture_pending_) {
    return;
  }
  committed_gate_pose_ = pending_gate_pose_;
  capture_pending_ = false;
  ++commit_count_;
  if (next_simulation_tick_ == kMaximumShowcaseSimulationTick) {
    simulation_exhausted_ = true;
  } else {
    ++next_simulation_tick_;
  }
}

void NativeVisualShowcaseSceneSource::DiscardJoinedGraphicsFrame() noexcept {
  if (!capture_pending_) {
    return;
  }
  capture_pending_ = false;
  ++discard_count_;
}

} // namespace RoR::Render
