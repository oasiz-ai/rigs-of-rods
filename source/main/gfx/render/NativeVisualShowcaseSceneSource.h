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
constexpr char kNativeVisualShowcasePackageRelativePath[] =
    "resources/nextgen/native/a0_road_tile_12m/"
    "rorng_a0_road_tile_12m.rornative";
constexpr char kNativeVisualShowcaseExecutableResourceRelativePath[] =
    "nextgen/native/a0_road_tile_12m/"
    "rorng_a0_road_tile_12m.rornative";
constexpr char kNativeVisualShowcasePackageId[] = "rorng_a0_road_tile_12m";
constexpr char kNativeVisualShowcasePackageSha256Hex[] =
    "226d2450c4a4612d873d15cbc124e2a4bbcc67fe9b2cbded82dcfa21427f62e2";
constexpr RenderPayloadDigest kNativeVisualShowcasePackageSha256{{
    0x22U, 0x6DU, 0x24U, 0x50U, 0xC4U, 0xA4U, 0x61U, 0x2DU, 0x87U, 0x3DU, 0x15U,
    0xCBU, 0xC1U, 0x24U, 0xE2U, 0xA4U, 0xBBU, 0xCCU, 0x67U, 0xFEU, 0x9BU, 0x2CU,
    0xBDU, 0xEDU, 0x82U, 0xDCU, 0xFAU, 0x21U, 0x42U, 0x7FU, 0x62U, 0xE2U,
}};
constexpr std::uint64_t kNativeVisualShowcaseSunLightId = 0x524F524E4753554EULL;
constexpr std::uint64_t kNativeVisualShowcaseCameraViewId =
    0x524F524E47564945ULL;
constexpr double kNativeVisualShowcaseFixedStepSeconds = 1.0 / 60.0;
constexpr float kNativeVisualShowcaseMovedGateOffsetMeters = 1.5F;

/// Two deliberately bounded poses used by raster/RT off-on-moved evidence.
/// This changes only the authored shadow-gate instance transform; it does not
/// mutate or regenerate package assets.
enum class NativeVisualShowcaseGatePose : std::uint8_t {
  HOME = 0U,
  MOVED = 1U,
};

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

  [[nodiscard]] const std::shared_ptr<const NativeRenderAssetPackage> &
  package_owner() const noexcept {
    return package_;
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
  [[nodiscard]] std::uint64_t next_simulation_tick() const noexcept {
    return next_simulation_tick_;
  }
  [[nodiscard]] NativeVisualShowcaseGatePose
  requested_gate_pose() const noexcept {
    return requested_gate_pose_;
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
      std::string package_path, std::size_t gate_instance_index,
      GraphicsSceneFrameInput base_frame);

  std::shared_ptr<const NativeRenderAssetPackage> package_;
  std::string package_path_;
  GraphicsSceneFrameInput base_frame_;
  std::size_t gate_instance_index_ = 0U;
  std::uint64_t gate_source_object_id_ = 0U;
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
  bool capture_pending_ = false;
  bool simulation_exhausted_ = false;

  friend NativeVisualShowcaseSceneSourceLoadResult
  LoadNativeVisualShowcaseSceneSource(const std::string &) noexcept;
};

} // namespace RoR::Render
