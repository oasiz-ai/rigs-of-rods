/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Forward-native project-original visual-showcase scene source.

#pragma once

#include "NativeRenderAssetPackage.h"

#include <cstdint>
#include <memory>
#include <string>

namespace RoR::Render {

constexpr std::uint32_t kNativeVisualShowcaseSceneSourceVersion = 1U;
enum class NativeVisualShowcaseProfile : std::uint8_t {
  A0_LIGHTING_COUPON = 0U,
  A1_NATIVE_COURSE = 1U,
};

/// A0 remains the bounded regression coupon used by RT off/on/moved tests.
constexpr char kNativeVisualShowcasePackageRelativePath[] =
    "resources/nextgen/native/a0_road_tile_12m/"
    "rorng_a0_road_tile_12m.rornative";
constexpr char kNativeVisualShowcaseExecutableResourceRelativePath[] =
    "nextgen/native/a0_road_tile_12m/"
    "rorng_a0_road_tile_12m.rornative";
constexpr char kNativeVisualShowcasePackageId[] = "rorng_a0_road_tile_12m";
constexpr char kNativeVisualShowcasePackageSha256Hex[] =
    "5f91c134231d5b86cd0c291d30018aa2f8aa4958c8e9267ec1c9068a0ea9bc05";
constexpr RenderPayloadDigest kNativeVisualShowcasePackageSha256{{
    0x5FU, 0x91U, 0xC1U, 0x34U, 0x23U, 0x1DU, 0x5BU, 0x86U, 0xCDU, 0x0CU, 0x29U,
    0x1DU, 0x30U, 0x01U, 0x8AU, 0xA2U, 0xF8U, 0xAAU, 0x49U, 0x58U, 0xC8U, 0xE9U,
    0x26U, 0x7EU, 0xC1U, 0xC9U, 0x06U, 0x8AU, 0x0EU, 0xA9U, 0xBCU, 0x05U,
}};
/// A1 is the current project-original 60 m visual course. It intentionally
/// uses `.rornative` v3 for its authored distance-LOD ladder while retaining
/// the v2 thin-slab transmission record; collision and driveability are
/// separate gates.
constexpr char kNativeVisualShowcaseA1PackageRelativePath[] =
    "resources/nextgen/native/a1_native_course_60m/"
    "rorng_a1_native_course_60m.rornative";
constexpr char kNativeVisualShowcaseA1ExecutableResourceRelativePath[] =
    "nextgen/native/a1_native_course_60m/"
    "rorng_a1_native_course_60m.rornative";
constexpr char kNativeVisualShowcaseA1PackageId[] =
    "rorng_a1_native_course_60m";
constexpr char kNativeVisualShowcaseA1PackageSha256Hex[] =
    "e420438797a77e4e49b91e3c6c930f39d340f99a4772ef989182a62605f2d53b";
constexpr RenderPayloadDigest kNativeVisualShowcaseA1PackageSha256{{
    0xE4U, 0x20U, 0x43U, 0x87U, 0x97U, 0xA7U, 0x7EU, 0x4EU, 0x49U, 0xB9U, 0x1EU,
    0x3CU, 0x6CU, 0x93U, 0x0FU, 0x39U, 0xD3U, 0x40U, 0xF9U, 0x9AU, 0x47U, 0x72U,
    0xEFU, 0x98U, 0x91U, 0x82U, 0xA6U, 0x26U, 0x05U, 0xF2U, 0xD5U, 0x3BU,
}};
constexpr std::uint64_t kNativeVisualShowcaseSunLightId = 0x524F524E4753554EULL;
constexpr std::uint64_t kNativeVisualShowcaseCameraViewId =
    0x524F524E47564945ULL;
constexpr std::uint64_t kNativeVisualShowcaseA1CameraViewId =
    0x524F524E47413156ULL;
constexpr double kNativeVisualShowcaseFixedStepSeconds = 1.0 / 60.0;
constexpr float kNativeVisualShowcaseMovedGateOffsetMeters = 1.5F;
constexpr std::uint32_t
    kNativeVisualShowcaseTurntableTicksPerRevolution = 360U;
constexpr std::uint32_t kNativeVisualShowcaseTurntableDegreesPerTick = 1U;
constexpr std::uint64_t kNativeVisualShowcaseTurntableTableFnv1a64 =
    UINT64_C(0xDFDD0F72F7539A65);

/// Two deliberately bounded poses used by raster/RT off-on-moved evidence.
/// This changes only the authored shadow-gate instance transform; it does not
/// mutate or regenerate package assets.
enum class NativeVisualShowcaseGatePose : std::uint8_t {
  HOME = 0U,
  MOVED = 1U,
};

/// Motion is opt-in so the reusable HOME/MOVED evidence captures retain their
/// exact bounded transforms. TURN_TABLE rotates only the authored opaque gate
/// around its vertical centerline; it is angular lighting/shadow evidence, not
/// refraction or motion-vector evidence.
enum class NativeVisualShowcaseMotionMode : std::uint8_t {
  STATIC = 0U,
  TURN_TABLE = 1U,
};

/// Returns one exact checked binary32 turntable matrix for a committed 60 Hz
/// simulation tick. Tick 360 is bit-identical to tick zero.
[[nodiscard]] Matrix4x4 NativeVisualShowcaseTurntableTransform(
    std::uint64_t simulation_tick) noexcept;
[[nodiscard]] Matrix4x4 NativeVisualShowcaseCenteredTurntableTransform(
    std::uint64_t simulation_tick) noexcept;

/// Canonical little-endian FNV-1a digest of all 360 checked matrix bit rows.
[[nodiscard]] std::uint64_t
NativeVisualShowcaseTurntableTableDigest() noexcept;

/// Stable FNV-1a revision of the exact matrix bytes used by renderer evidence.
[[nodiscard]] std::uint64_t NativeVisualShowcaseTransformRevision(
    const Matrix4x4 &transform) noexcept;

class NativeVisualShowcaseSceneSource;

struct NativeVisualShowcaseSceneSourceLoadResult {
  ValidationResult validation;
  std::unique_ptr<NativeVisualShowcaseSceneSource> source;

  [[nodiscard]] bool ok() const noexcept {
    return validation.ok() && source != nullptr;
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Loads and authenticates one exact forward-native package checkpoint, then
/// retains its immutable owner for the entire source lifetime. The path is
/// opened and consumed only by this factory; captures never touch storage.
[[nodiscard]] NativeVisualShowcaseSceneSourceLoadResult
LoadNativeVisualShowcaseSceneSource(const std::string &package_path) noexcept;

/// Profile-explicit overload used by the combined runtime. It authenticates
/// the selected package and binds its reviewed camera/clip composition; profile
/// mismatch is a hard revision failure, never a best-effort fallback.
[[nodiscard]] NativeVisualShowcaseSceneSourceLoadResult
LoadNativeVisualShowcaseSceneSource(
    const std::string &package_path,
    NativeVisualShowcaseProfile profile) noexcept;

/// Small renderer-neutral source for the project-original lighting coupon.
/// Capture prepares a frame transaction without advancing time or pose.
/// Exactly one Commit or Discard must follow each successful capture.
class NativeVisualShowcaseSceneSource final
    : public IJoinedGraphicsSceneSource {
public:
  ~NativeVisualShowcaseSceneSource() override = default;

  NativeVisualShowcaseSceneSource(const NativeVisualShowcaseSceneSource &) =
      delete;
  NativeVisualShowcaseSceneSource &
  operator=(const NativeVisualShowcaseSceneSource &) = delete;
  NativeVisualShowcaseSceneSource(NativeVisualShowcaseSceneSource &&) = delete;
  NativeVisualShowcaseSceneSource &
  operator=(NativeVisualShowcaseSceneSource &&) = delete;

  [[nodiscard]] ValidationResult
  CaptureJoinedGraphicsFrame(GraphicsSceneFrameInput &frame) override;
  void CommitJoinedGraphicsFrame() noexcept override;
  void DiscardJoinedGraphicsFrame() noexcept override;

  /// Selects the pose for the next capture. A pending capture must first be
  /// committed or discarded so its immutable candidate cannot be rewritten.
  [[nodiscard]] ValidationResult SetGatePose(NativeVisualShowcaseGatePose pose);

  /// Selects motion for subsequent captures. The default is STATIC; a pending
  /// capture must first be committed or discarded. TURN_TABLE is mutually
  /// exclusive with the reusable MOVED evidence pose.
  [[nodiscard]] ValidationResult
  SetMotionMode(NativeVisualShowcaseMotionMode mode);

  [[nodiscard]] const std::shared_ptr<const NativeRenderAssetPackage> &
  package_owner() const noexcept {
    return package_;
  }
  [[nodiscard]] NativeVisualShowcaseProfile profile() const noexcept {
    return profile_;
  }
  [[nodiscard]] bool supports_turntable_motion() const noexcept {
    return true;
  }
  [[nodiscard]] const std::string &package_path() const noexcept {
    return package_path_;
  }
  [[nodiscard]] std::uint64_t package_load_count() const noexcept {
    return package_load_count_;
  }
  [[nodiscard]] std::uint64_t gate_source_object_id() const noexcept {
    return gate_source_object_id_;
  }
  [[nodiscard]] std::uint64_t motion_source_object_id() const noexcept {
    return motion_source_object_id_;
  }
  [[nodiscard]] std::uint64_t next_simulation_tick() const noexcept {
    return next_simulation_tick_;
  }
  [[nodiscard]] NativeVisualShowcaseGatePose
  requested_gate_pose() const noexcept {
    return requested_gate_pose_;
  }
  [[nodiscard]] NativeVisualShowcaseMotionMode
  requested_motion_mode() const noexcept {
    return requested_motion_mode_;
  }
  [[nodiscard]] NativeVisualShowcaseMotionMode
  committed_motion_mode() const noexcept {
    return committed_motion_mode_;
  }
  [[nodiscard]] bool has_committed_capture() const noexcept {
    return has_committed_capture_;
  }
  [[nodiscard]] std::uint64_t
  committed_simulation_tick() const noexcept {
    return committed_simulation_tick_;
  }
  [[nodiscard]] std::uint32_t
  committed_turntable_angle_degrees() const noexcept {
    return committed_turntable_angle_degrees_;
  }
  [[nodiscard]] std::uint64_t
  committed_gate_transform_revision() const noexcept {
    return committed_gate_transform_revision_;
  }
  [[nodiscard]] NativeVisualShowcaseGatePose
  committed_gate_pose() const noexcept {
    return committed_gate_pose_;
  }
  [[nodiscard]] bool has_pending_capture() const noexcept {
    return capture_pending_;
  }
  [[nodiscard]] std::uint64_t capture_count() const noexcept {
    return capture_count_;
  }
  [[nodiscard]] std::uint64_t commit_count() const noexcept {
    return commit_count_;
  }
  [[nodiscard]] std::uint64_t discard_count() const noexcept {
    return discard_count_;
  }

private:
  NativeVisualShowcaseSceneSource(
      std::shared_ptr<const NativeRenderAssetPackage> package,
      std::string package_path, NativeVisualShowcaseProfile profile,
      std::size_t gate_instance_index, std::size_t motion_instance_index,
      GraphicsSceneFrameInput base_frame);

  std::shared_ptr<const NativeRenderAssetPackage> package_;
  std::string package_path_;
  NativeVisualShowcaseProfile profile_ =
      NativeVisualShowcaseProfile::A0_LIGHTING_COUPON;
  GraphicsSceneFrameInput base_frame_;
  std::size_t gate_instance_index_ = 0U;
  std::size_t motion_instance_index_ = 0U;
  std::uint64_t gate_source_object_id_ = 0U;
  std::uint64_t motion_source_object_id_ = 0U;
  std::uint64_t package_load_count_ = 1U;
  std::uint64_t next_simulation_tick_ = 0U;
  std::uint64_t capture_count_ = 0U;
  std::uint64_t commit_count_ = 0U;
  std::uint64_t discard_count_ = 0U;
  NativeVisualShowcaseGatePose requested_gate_pose_ =
      NativeVisualShowcaseGatePose::HOME;
  NativeVisualShowcaseGatePose committed_gate_pose_ =
      NativeVisualShowcaseGatePose::HOME;
  NativeVisualShowcaseGatePose pending_gate_pose_ =
      NativeVisualShowcaseGatePose::HOME;
  NativeVisualShowcaseMotionMode requested_motion_mode_ =
      NativeVisualShowcaseMotionMode::STATIC;
  NativeVisualShowcaseMotionMode committed_motion_mode_ =
      NativeVisualShowcaseMotionMode::STATIC;
  NativeVisualShowcaseMotionMode pending_motion_mode_ =
      NativeVisualShowcaseMotionMode::STATIC;
  std::uint64_t committed_simulation_tick_ = 0U;
  std::uint64_t pending_simulation_tick_ = 0U;
  std::uint32_t committed_turntable_angle_degrees_ = 0U;
  std::uint32_t pending_turntable_angle_degrees_ = 0U;
  std::uint64_t committed_gate_transform_revision_ = 0U;
  std::uint64_t pending_gate_transform_revision_ = 0U;
  bool capture_pending_ = false;
  bool has_committed_capture_ = false;
  bool simulation_exhausted_ = false;

  friend NativeVisualShowcaseSceneSourceLoadResult
  LoadNativeVisualShowcaseSceneSource(const std::string &) noexcept;
  friend NativeVisualShowcaseSceneSourceLoadResult
  LoadNativeVisualShowcaseSceneSource(
      const std::string &, NativeVisualShowcaseProfile) noexcept;
};

} // namespace RoR::Render
