/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "NativeVisualShowcaseSceneSource.h"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <utility>
#include <variant>
#include <vector>

namespace RoR::Render {
namespace {

constexpr char kGateMeshDebugName[] = "rorng_a0_road_shadow_gate_mesh";
constexpr char kGlassMeshDebugName[] = "rorng_a1_glass_slab_mesh";
constexpr std::uint64_t kMaximumShowcaseSimulationTick =
    60ULL * 60ULL * 24ULL * 366ULL * 100ULL;

#include "NativeVisualShowcaseTurntableTable.inc"

static_assert(kNativeVisualShowcaseTurntableMatrixBits.size() ==
                  kNativeVisualShowcaseTurntableTicksPerRevolution,
              "native showcase turntable table lost a checked tick");

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

bool IsKnownMotionMode(NativeVisualShowcaseMotionMode mode) noexcept {
  switch (mode) {
  case NativeVisualShowcaseMotionMode::STATIC:
  case NativeVisualShowcaseMotionMode::TURN_TABLE:
    return true;
  }
  return false;
}

bool IsKnownProfile(NativeVisualShowcaseProfile profile) noexcept {
  switch (profile) {
  case NativeVisualShowcaseProfile::A0_LIGHTING_COUPON:
  case NativeVisualShowcaseProfile::A1_NATIVE_COURSE:
    return true;
  }
  return false;
}

struct NativeVisualShowcaseCheckpoint {
  const char *package_id = nullptr;
  const RenderPayloadDigest *package_sha256 = nullptr;
  bool supports_turntable = false;
};

const NativeVisualShowcaseCheckpoint *FindCheckpoint(
    NativeVisualShowcaseProfile profile) noexcept {
  static const NativeVisualShowcaseCheckpoint kA0{
      kNativeVisualShowcasePackageId,
      &kNativeVisualShowcasePackageSha256,
      true,
  };
  static const NativeVisualShowcaseCheckpoint kA1{
      kNativeVisualShowcaseA1PackageId,
      &kNativeVisualShowcaseA1PackageSha256,
      true,
  };
  switch (profile) {
  case NativeVisualShowcaseProfile::A0_LIGHTING_COUPON:
    return &kA0;
  case NativeVisualShowcaseProfile::A1_NATIVE_COURSE:
    return &kA1;
  }
  return nullptr;
}

float FloatFromBits(std::uint32_t bits) noexcept {
  float value = 0.0F;
  static_assert(sizeof(value) == sizeof(bits),
                "native showcase requires binary32 float storage");
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void AddFnvByte(std::uint64_t &digest, std::uint8_t byte) noexcept {
  digest ^= byte;
  digest *= UINT64_C(1099511628211);
}

Matrix4x4 MakeShowcaseProjection(
    NativeVisualShowcaseProfile profile) noexcept {
  constexpr float kNearPlane = 0.1F;
  const float far_plane =
      profile == NativeVisualShowcaseProfile::A1_NATIVE_COURSE
          ? 140.0F
          : 50.0F;
  Matrix4x4 projection;
  projection.elements.fill(0.0F);
  if (profile == NativeVisualShowcaseProfile::A1_NATIVE_COURSE) {
    // Checked A1 composition: 55-degree vertical FOV at 1920x1080.
    projection.elements[0U] = 1.0805524587631226F;
    projection.elements[5U] = 1.9209821224212646F;
  } else {
    // Checked A0 composition: 50-degree vertical FOV at 1920x1080.
    projection.elements[0U] = 1.2062851190567017F;
    projection.elements[5U] = 2.1445069313049316F;
  }
  const float depth_scale = far_plane / (kNearPlane - far_plane);
  projection.elements[10U] = depth_scale;
  projection.elements[11U] = -1.0F;
  projection.elements[14U] = kNearPlane * depth_scale;
  return projection;
}

GraphicsSceneFrameInput MakeBaseFrame(
    const NativeRenderAssetPackage &package,
    NativeVisualShowcaseProfile profile) {
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

  frame.camera.view_id =
      profile == NativeVisualShowcaseProfile::A1_NATIVE_COURSE
          ? kNativeVisualShowcaseA1CameraViewId
          : kNativeVisualShowcaseCameraViewId;
  frame.camera.width = 1920U;
  frame.camera.height = 1080U;
  if (profile == NativeVisualShowcaseProfile::A1_NATIVE_COURSE) {
    // Exact binary32 RH look-at transform for A1's checked composition:
    // eye (35,34,48), target (0,0.8,0), up (0,1,0).
    frame.camera.view_from_render.elements = {{
        0.808007538318634F,
        -0.28742972016334534F,
        0.5143033862113953F,
        0.0F,
        0.0F,
        0.8729255199432373F,
        0.4878535270690918F,
        0.0F,
        -0.5891721844673157F,
        -0.394189327955246F,
        0.7053303718566895F,
        0.0F,
        0.0F,
        -0.6983404159545898F,
        -68.44349670410156F,
        1.0F,
    }};
  } else {
    // Exact binary32 RH look-at transform for A0's checked composition:
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
  }
  frame.camera.clip_from_view = MakeShowcaseProjection(profile);
  frame.camera.temporal_jitter_pixels = {};
  frame.camera.near_plane = 0.1F;
  frame.camera.far_plane =
      profile == NativeVisualShowcaseProfile::A1_NATIVE_COURSE
          ? 140.0F
          : 50.0F;
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

ValidationResult FindGlassInstance(const NativeRenderAssetPackage &package,
                                   std::size_t &glass_instance_index) {
  std::uint64_t glass_mesh_source_id = 0U;
  for (const GraphicsSceneAssetInput &asset : package.assets) {
    const auto *mesh =
        asset.payload != nullptr
            ? std::get_if<MeshResourceDescriptor>(asset.payload.get())
            : nullptr;
    if (mesh == nullptr || mesh->debug_name != kGlassMeshDebugName) {
      continue;
    }
    if (glass_mesh_source_id != 0U) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "native_showcase.glass_mesh",
                     "package contains more than one authored glass mesh");
    }
    glass_mesh_source_id = asset.source_asset_id;
  }
  if (glass_mesh_source_id == 0U) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "native_showcase.glass_mesh",
                   "package does not contain the authored glass mesh");
  }
  bool found_instance = false;
  for (std::size_t index = 0U; index < package.static_meshes.size(); ++index) {
    if (package.static_meshes[index].mesh_source_asset_id !=
        glass_mesh_source_id) {
      continue;
    }
    if (found_instance) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "native_showcase.glass_instance",
                     "package instantiates the authored glass more than once");
    }
    glass_instance_index = index;
    found_instance = true;
  }
  return found_instance
             ? ValidationResult::Success()
             : Failure(ValidationCode::MISSING_REFERENCE,
                       "native_showcase.glass_instance",
                       "package does not instantiate the authored glass");
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

Matrix4x4 NativeVisualShowcaseTurntableTransform(
    std::uint64_t simulation_tick) noexcept {
  const std::size_t row = static_cast<std::size_t>(
      simulation_tick % kNativeVisualShowcaseTurntableTicksPerRevolution);
  Matrix4x4 transform;
  for (std::size_t index = 0U; index < transform.elements.size(); ++index) {
    transform.elements[index] =
        FloatFromBits(kNativeVisualShowcaseTurntableMatrixBits[row][index]);
  }
  return transform;
}

Matrix4x4 NativeVisualShowcaseCenteredTurntableTransform(
    std::uint64_t simulation_tick) noexcept {
  Matrix4x4 transform =
      NativeVisualShowcaseTurntableTransform(simulation_tick);
  transform.elements[12U] = 0.0F;
  transform.elements[13U] = 0.0F;
  transform.elements[14U] = 0.0F;
  return transform;
}

std::uint64_t NativeVisualShowcaseTurntableTableDigest() noexcept {
  std::uint64_t digest = UINT64_C(14695981039346656037);
  for (const auto &row : kNativeVisualShowcaseTurntableMatrixBits) {
    for (const std::uint32_t word : row) {
      AddFnvByte(digest, static_cast<std::uint8_t>(word & 0xFFU));
      AddFnvByte(digest, static_cast<std::uint8_t>((word >> 8U) & 0xFFU));
      AddFnvByte(digest, static_cast<std::uint8_t>((word >> 16U) & 0xFFU));
      AddFnvByte(digest, static_cast<std::uint8_t>((word >> 24U) & 0xFFU));
    }
  }
  return digest;
}

std::uint64_t NativeVisualShowcaseTransformRevision(
    const Matrix4x4 &transform) noexcept {
  std::uint64_t digest = UINT64_C(14695981039346656037);
  for (const float element : transform.elements) {
    std::uint32_t word = 0U;
    std::memcpy(&word, &element, sizeof(word));
    AddFnvByte(digest, static_cast<std::uint8_t>(word & 0xFFU));
    AddFnvByte(digest, static_cast<std::uint8_t>((word >> 8U) & 0xFFU));
    AddFnvByte(digest, static_cast<std::uint8_t>((word >> 16U) & 0xFFU));
    AddFnvByte(digest, static_cast<std::uint8_t>((word >> 24U) & 0xFFU));
  }
  return digest == 0U ? UINT64_C(14695981039346656037) : digest;
}

NativeVisualShowcaseSceneSource::NativeVisualShowcaseSceneSource(
    std::shared_ptr<const NativeRenderAssetPackage> package,
    std::string package_path, NativeVisualShowcaseProfile profile,
    std::size_t gate_instance_index, std::size_t motion_instance_index,
    GraphicsSceneFrameInput base_frame)
    : package_(std::move(package)), package_path_(std::move(package_path)),
      profile_(profile),
      base_frame_(std::move(base_frame)),
      gate_instance_index_(gate_instance_index),
      motion_instance_index_(motion_instance_index),
      gate_source_object_id_(
          base_frame_.static_meshes[gate_instance_index_].source_object_id),
      motion_source_object_id_(
          base_frame_.static_meshes[motion_instance_index_].source_object_id) {}

NativeVisualShowcaseSceneSourceLoadResult
LoadNativeVisualShowcaseSceneSource(const std::string &package_path) noexcept {
  return LoadNativeVisualShowcaseSceneSource(
      package_path, NativeVisualShowcaseProfile::A0_LIGHTING_COUPON);
}

NativeVisualShowcaseSceneSourceLoadResult
LoadNativeVisualShowcaseSceneSource(
    const std::string &package_path,
    NativeVisualShowcaseProfile profile) noexcept {
  NativeVisualShowcaseSceneSourceLoadResult result;
  try {
    const NativeVisualShowcaseCheckpoint *const checkpoint =
        FindCheckpoint(profile);
    if (!IsKnownProfile(profile) || checkpoint == nullptr ||
        checkpoint->package_id == nullptr ||
        checkpoint->package_sha256 == nullptr) {
      result.validation = Failure(
          ValidationCode::INVALID_ENUM,
          "native_showcase.profile",
          "unknown native showcase package profile");
      return result;
    }
    std::vector<std::uint8_t> bytes;
    result.validation = ReadPackageOnce(package_path, bytes);
    if (!result.validation) {
      return result;
    }
    NativeRenderAssetPackageDecodeResult decoded =
        DecodeNativeRenderAssetPackage(bytes.data(), bytes.size(),
                                       *checkpoint->package_sha256);
    if (!decoded.ok()) {
      result.validation = std::move(decoded.validation);
      return result;
    }
    if (decoded.package->package_sha256 != *checkpoint->package_sha256 ||
        decoded.package->package_id != checkpoint->package_id ||
        decoded.package->origin_class != "project_original") {
      result.validation = Failure(ValidationCode::REVISION_MISMATCH,
                                  "native_showcase.package_checkpoint",
                                  "decoded package does not match the reviewed "
                                  "project-original checkpoint");
      return result;
    }
    if (checkpoint->supports_turntable &&
        NativeVisualShowcaseTurntableTableDigest() !=
        kNativeVisualShowcaseTurntableTableFnv1a64) {
      result.validation = Failure(
          ValidationCode::REVISION_MISMATCH,
          "native_showcase.turntable_table",
          "checked turntable matrix table digest changed");
      return result;
    }

    std::size_t gate_instance_index = 0U;
    result.validation = FindGateInstance(*decoded.package, gate_instance_index);
    if (!result.validation) {
      return result;
    }
    std::size_t motion_instance_index = gate_instance_index;
    if (profile == NativeVisualShowcaseProfile::A1_NATIVE_COURSE) {
      result.validation =
          FindGlassInstance(*decoded.package, motion_instance_index);
      if (!result.validation) {
        return result;
      }
    }
    GraphicsSceneFrameInput base_frame =
        MakeBaseFrame(*decoded.package, profile);
    result.source = std::unique_ptr<NativeVisualShowcaseSceneSource>(
        new NativeVisualShowcaseSceneSource(std::move(decoded.package),
                                            package_path, profile,
                                            gate_instance_index,
                                            motion_instance_index,
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
  if (requested_motion_mode_ ==
          NativeVisualShowcaseMotionMode::TURN_TABLE &&
      pose != NativeVisualShowcaseGatePose::HOME) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "native_showcase.gate_pose",
                   "turntable motion is mutually exclusive with moved evidence");
  }
  requested_gate_pose_ = pose;
  return ValidationResult::Success();
}

ValidationResult NativeVisualShowcaseSceneSource::SetMotionMode(
    NativeVisualShowcaseMotionMode mode) {
  if (!IsKnownMotionMode(mode)) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "native_showcase.motion_mode",
                   "unknown native showcase motion mode");
  }
  if (capture_pending_) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "native_showcase.capture_transaction",
                   "motion mode cannot change while a capture is pending");
  }
  if (mode == NativeVisualShowcaseMotionMode::TURN_TABLE &&
      requested_gate_pose_ != NativeVisualShowcaseGatePose::HOME) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "native_showcase.motion_mode",
                   "turntable motion requires the home evidence pose");
  }
  if (mode == NativeVisualShowcaseMotionMode::TURN_TABLE &&
      !supports_turntable_motion()) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "native_showcase.motion_mode",
        "selected native scene has no reviewed turntable transform table");
  }
  requested_motion_mode_ = mode;
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
  } else if (requested_motion_mode_ ==
             NativeVisualShowcaseMotionMode::TURN_TABLE) {
    candidate.static_meshes[motion_instance_index_].render_from_object =
        profile_ == NativeVisualShowcaseProfile::A1_NATIVE_COURSE
            ? NativeVisualShowcaseCenteredTurntableTransform(
                  next_simulation_tick_)
            : NativeVisualShowcaseTurntableTransform(next_simulation_tick_);
  }

  frame = std::move(candidate);
  pending_gate_pose_ = requested_gate_pose_;
  pending_motion_mode_ = requested_motion_mode_;
  pending_simulation_tick_ = next_simulation_tick_;
  pending_turntable_angle_degrees_ =
      requested_motion_mode_ == NativeVisualShowcaseMotionMode::TURN_TABLE
          ? static_cast<std::uint32_t>(
                next_simulation_tick_ %
                kNativeVisualShowcaseTurntableTicksPerRevolution)
          : 0U;
  pending_gate_transform_revision_ = NativeVisualShowcaseTransformRevision(
      frame.static_meshes[motion_instance_index_].render_from_object);
  capture_pending_ = true;
  ++capture_count_;
  return ValidationResult::Success();
}

void NativeVisualShowcaseSceneSource::CommitJoinedGraphicsFrame() noexcept {
  if (!capture_pending_) {
    return;
  }
  committed_gate_pose_ = pending_gate_pose_;
  committed_motion_mode_ = pending_motion_mode_;
  committed_simulation_tick_ = pending_simulation_tick_;
  committed_turntable_angle_degrees_ = pending_turntable_angle_degrees_;
  committed_gate_transform_revision_ = pending_gate_transform_revision_;
  has_committed_capture_ = true;
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
