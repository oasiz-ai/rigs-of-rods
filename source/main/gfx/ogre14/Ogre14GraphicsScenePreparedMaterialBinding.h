/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Immutable binding of one prepared material frame to scene sections.

#pragma once

#include "Ogre14LegacyLiveMaterialCoordinator.h"
#include "gfx/render/Ogre14GraphicsSceneSource.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14GraphicsScenePreparedMaterialBindingVersion = 1U;

enum class Ogre14GraphicsScenePreparedMaterialBindingFaultPoint : std::uint8_t {
  AFTER_FIRST_EXACT_BINDING = 0U,
  BEFORE_BINDING_COMMIT = 1U,
};

class IOgre14GraphicsScenePreparedMaterialBindingFaultInjector {
public:
  virtual ~IOgre14GraphicsScenePreparedMaterialBindingFaultInjector() = default;
  /// Borrowed test seam. Production passes null. Implementations may throw.
  virtual void
  AtFaultPoint(Ogre14GraphicsScenePreparedMaterialBindingFaultPoint point) = 0;
};

/// One immutable candidate presented to the joined scene transaction. It
/// retains the exact prepared-frame identity required by the coordinator's
/// final commit and owns the section copies carrying canonical closure owners.
class Ogre14GraphicsScenePreparedMaterialBinding final {
public:
  Ogre14GraphicsScenePreparedMaterialBinding() noexcept = default;
  ~Ogre14GraphicsScenePreparedMaterialBinding() = default;
  Ogre14GraphicsScenePreparedMaterialBinding(
      const Ogre14GraphicsScenePreparedMaterialBinding &) noexcept = default;
  Ogre14GraphicsScenePreparedMaterialBinding &operator=(
      const Ogre14GraphicsScenePreparedMaterialBinding &) noexcept = default;
  Ogre14GraphicsScenePreparedMaterialBinding(
      Ogre14GraphicsScenePreparedMaterialBinding &&) noexcept = default;
  Ogre14GraphicsScenePreparedMaterialBinding &
  operator=(Ogre14GraphicsScenePreparedMaterialBinding &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t version() const noexcept;
  [[nodiscard]] const Ogre14LegacyPreparedMaterialFrame *
  prepared_frame() const noexcept;
  [[nodiscard]] const std::vector<
      Ogre14GraphicsSceneStaticSectionCaptureInput> &
  static_sections() const noexcept;
  [[nodiscard]] const std::vector<
      Ogre14GraphicsSceneDynamicSectionCaptureInput> &
  dynamic_sections() const noexcept;
  [[nodiscard]] const Ogre14GraphicsSceneResolvedMaterialFrameLineage *
  lineage() const noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14GraphicsScenePreparedMaterialBinding &other) const noexcept;

private:
  struct State;
  explicit Ogre14GraphicsScenePreparedMaterialBinding(
      std::shared_ptr<const State> state) noexcept;

  std::shared_ptr<const State> state_;

  friend ValidationResult BindOgre14GraphicsScenePreparedMaterials(
      const Ogre14LegacyPreparedMaterialFrame &,
      const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &,
      const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> &,
      Ogre14GraphicsScenePreparedMaterialBinding &,
      IOgre14GraphicsScenePreparedMaterialBindingFaultInjector *);
};

/// Copies the candidate section inventories, rejects caller-supplied closure
/// substitution, binds exact prepared owners by material key, validates actual
/// winding conversion and common frame lineage, and publishes only after the
/// complete candidate succeeds. Sections without a prepared entry must pass
/// the unchanged factor-only fallback gate.
[[nodiscard]] ValidationResult BindOgre14GraphicsScenePreparedMaterials(
    const Ogre14LegacyPreparedMaterialFrame &prepared_frame,
    const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput>
        &static_sections,
    const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput>
        &dynamic_sections,
    Ogre14GraphicsScenePreparedMaterialBinding &output,
    IOgre14GraphicsScenePreparedMaterialBindingFaultInjector *fault_injector =
        nullptr);

} // namespace RoR::Render
