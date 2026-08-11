/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief Disposable map-scoped OGRE 14 TUS0 projection for the OgreNext demo.

#pragma once

#include "OgreNextDemoPrivatePolicy.h"

#include "gfx/render/Ogre14GraphicsSceneSource.h"

#include <OgreMaterial.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace RoR::Render {
class IOgre14AuthenticatedTextureResolver;
class IOgre14AuthenticatedTextureAuthorityProvider;
class IOgre14SelectedTextureSourceResolver;
} // namespace RoR::Render

namespace RoR::Gfx::Detail {

/// MaterialSource exposes the renderer-neutral transactional accounting type.
using OgreNextDemoMaterialSourceCounters = OgreNextDemoTextureSourceCounters;

/// Performance-first private bridge for the playable OgreNext demo. It is not
/// a legacy-material API: one narrowly eligible opaque TUS0 is captured once
/// per map generation and lowered to conventional PBR assets. Each exact
/// section's first observation freezes generic-factor versus matte/projection
/// mode plus material/UV/cull identity. Generic factor values remain governed
/// by the outer inventory; a later mode/key change or projected
/// native-authority change fails the joined capture.
class OgreNextDemoMaterialSource final {
public:
  OgreNextDemoMaterialSource();
  ~OgreNextDemoMaterialSource();

  OgreNextDemoMaterialSource(const OgreNextDemoMaterialSource &) = delete;
  OgreNextDemoMaterialSource &
  operator=(const OgreNextDemoMaterialSource &) = delete;

  /// Binds the serialized ContentManager authority without including its
  /// OGRE-native receipt interface in this header. Rebinding the same resolver
  /// and provider pair is idempotent; replacement or a late first bind after
  /// capture/cache publication is rejected.
  [[nodiscard]] bool BindAuthenticatedTextureAuthority(
      const Render::IOgre14AuthenticatedTextureResolver &resolver,
      const Render::IOgre14AuthenticatedTextureAuthorityProvider
          &provider) noexcept;

  /// Binds the ordinary-package selected-source resolver used by automatic
  /// source-byte projection. A focused/authenticated-only caller may leave it
  /// absent and receives explicit mattes; the combined product binds it before
  /// capture. Replacement or late first binding is rejected.
  [[nodiscard]] bool BindOrdinarySelectedTextureSourceResolver(
      const Render::IOgre14SelectedTextureSourceResolver &resolver) noexcept;

  /// Starts one outer GfxScene capture transaction. The joined capture must
  /// fail if this source cannot open; it must never publish a first-frame matte
  /// merely because projected-asset allocation failed.
  [[nodiscard]] bool BeginCapture() noexcept;

  /// If eligible, replaces only the synthetic matte identity fields in input.
  /// exact_section_key is a map-generation-stable static or dynamic section
  /// identity, not a native pointer or material name.
  /// The generic inventory builders then mint the instance material reference
  /// that Apply() replaces with the cached PBR payload and bindings. The
  /// ValidationResult distinguishes an ordinary unsupported matte decision
  /// from a capture/authority error; projected reports whether replacement was
  /// selected for this frozen section decision.
  [[nodiscard]] Render::ValidationResult
  TryProject(std::string_view exact_section_key,
             const Ogre::MaterialPtr &native_material,
             bool projection_candidate, bool has_authored_uv0,
             Render::Ogre14GraphicsSceneMaterialCaptureInput &input,
             bool &projected) noexcept;

  /// Replaces matching synthetic matte material assets and appends their
  /// immutable texture/sampler owners. Input is unchanged on failure.
  [[nodiscard]] Render::ValidationResult
  Apply(std::vector<Render::GraphicsSceneAssetInput> &assets) noexcept;

  [[nodiscard]] std::size_t NewProjectionCount() const noexcept;
  [[nodiscard]] std::size_t UsedProjectionCount() const noexcept;
  [[nodiscard]] OgreNextDemoMaterialSourceCounters
  CurrentCaptureCounters() const noexcept;
  [[nodiscard]] OgreNextDemoMaterialSourceCounters
  LifetimeCounters() const noexcept;

  void Commit() noexcept;
  void Discard() noexcept;
  void Reset() noexcept;

private:
  void EnsurePendingCacheWritable();

  [[nodiscard]] bool TryProjectCurrent(
      const Ogre::MaterialPtr &native_material, bool has_authored_uv0,
      Render::Ogre14GraphicsSceneMaterialCaptureInput &input,
      std::string &selected_projection_key, bool allow_new_projection,
      OgreNextDemoTextureProjectionExclusion &exclusion,
      Render::ValidationResult &failure);

  struct State;
  std::unique_ptr<State> committed_;
  std::unique_ptr<State> pending_;
  const Render::IOgre14AuthenticatedTextureResolver *texture_resolver_ =
      nullptr;
  const Render::IOgre14AuthenticatedTextureAuthorityProvider
      *texture_authority_provider_ = nullptr;
  const Render::IOgre14SelectedTextureSourceResolver
      *ordinary_texture_source_resolver_ = nullptr;
  OgreNextDemoMaterialSourceCounters lifetime_counters_;
};

} // namespace RoR::Gfx::Detail
