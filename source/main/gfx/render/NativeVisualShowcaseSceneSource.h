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
    "ef96537179799cd1166f871e67657bdd94d750886c24f8aac37ff09aa5fef648";
constexpr RenderPayloadDigest kNativeVisualShowcasePackageSha256{{
    0xBDU, 0x37U, 0x10U, 0x2FU, 0x9AU, 0xBFU, 0x1FU, 0x91U, 0x49U, 0x10U, 0xC2U,
    0xD7U, 0xD5U, 0x9AU, 0x0EU, 0x9FU, 0x5AU, 0x4AU, 0x5BU, 0xC9U, 0x6AU, 0xDDU,
    0x0BU, 0xDBU, 0x95U, 0x71U, 0x86U, 0xA8U, 0x51U, 0x4DU, 0x19U, 0xC6U,
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
