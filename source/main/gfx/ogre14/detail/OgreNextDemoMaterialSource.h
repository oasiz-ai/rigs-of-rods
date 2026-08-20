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
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Ogre {
class Pass;
} // namespace Ogre

namespace RoR::Render {
class IOgre14AuthenticatedTextureResolver;
class IOgre14AuthenticatedTextureAuthorityProvider;
class IOgre14AuthenticatedMaterialScriptResolver;
class IOgre14SelectedTextureSourceResolver;
class Ogre14ManagedMaterialDeclarationBinding;
} // namespace RoR::Render

namespace RoR::Gfx::Detail {

/// MaterialSource exposes the renderer-neutral transactional accounting type.
using OgreNextDemoMaterialSourceCounters = OgreNextDemoTextureSourceCounters;

struct OgreNextDemoCuratedCityWorldCoverage final {
  std::size_t policy_entries =
      kOgreNextDemoCuratedCityWorldAsiaPolicyEntryCount;
  std::size_t observed_entries = 0U;
  std::size_t admitted_entries = 0U;
  std::size_t matte_entries = 0U;
  std::size_t environment_pending_entries = 0U;
  std::size_t uncurated_spherical_family_matte_materials = 0U;
};

/// Performance-first private bridge for the playable OgreNext demo. It is not
/// a legacy-material API: the generic path retains one narrowly eligible
/// opaque TUS0, while the separately authenticated three-row CityWorld slice
/// lowers exact TUS0+TUS1 source bytes and retains TUS2 as pending authority.
/// Each section's first observation freezes generic-factor identity plus
/// material/UV/cull state. Projected decisions and non-transient matte reasons
/// remain immutable. Source-unavailable mattes are recounted and retried on a
/// later capture so package/resource generation changes can promote them;
/// authentication inconsistency/demotion and projected authority changes stay
/// fail-closed. Generic factor values remain governed by the outer inventory.
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

  /// Binds ContentManager's material-script receipt authority. Generic TUS0
  /// projection does not depend on this seam; the curated CityWorld exception
  /// cannot admit anything unless the exact material-to-script resolution is
  /// current and authenticated.
  [[nodiscard]] bool BindAuthenticatedMaterialScriptResolver(
      const Render::IOgre14AuthenticatedMaterialScriptResolver
          &resolver) noexcept;

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

  [[nodiscard]] Render::ValidationResult
  TryProject(std::string_view exact_section_key,
             const Ogre::MaterialPtr &native_material,
             bool projection_candidate, bool has_authored_uv0,
             const Render::Ogre14ManagedMaterialDeclarationBinding
                 *managed_binding,
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
  [[nodiscard]] OgreNextDemoCuratedCityWorldCoverage
  CurrentCuratedCityWorldCoverage() const noexcept;

  /// Diagnostic per-material section census, accumulated for the whole session
  /// and never cleared by capture commit/discard.
  ///
  /// The aggregate `matte_by_reason` histogram says how many sections each
  /// refusal reason claimed, but not WHICH materials they were, so it cannot
  /// answer "how matte is this district" - districts are a property of the
  /// authoring material script, not of the counters. This returns one line per
  /// distinct material name with its projected/matte section split and its
  /// last refusal reason, which attributes every matte section to the script
  /// that declared it. Observation only: it records outcomes and never
  /// influences them.
  ///
  /// Returns an empty string when nothing has been observed yet.
  [[nodiscard]] std::string FormatMaterialSectionCensus() const noexcept;

  /// Number of distinct materials in the census above; lets a caller log the
  /// body only when it has actually grown.
  [[nodiscard]] std::size_t MaterialSectionCensusSize() const noexcept;

  void Commit() noexcept;
  void Discard() noexcept;
  void Reset() noexcept;

private:
  void EnsurePendingCacheWritable();

  /// Discharges the authored-texel clause of
  /// `kOgreNextDemoAdmitsAdditiveEquivalentGlowOverlayPasses`: decodes both
  /// passes' authored base-colour sources and proves that, over every texel the
  /// overlay's own alpha rejection keeps, the overlay is pass 0's artwork with
  /// alpha exactly 255 and RGB within
  /// `kOgreNextDemoGlowOverlayMaximumKeptTexelDelta`. The decode exists only
  /// for this comparison and is discarded; neither source is published,
  /// retained, or given an asset, so an admitted glow material costs no extra
  /// texture memory. The verdict is memoised per authored source pair and
  /// re-proved through the selected-source receipts on every reuse, so it can
  /// never outlive the bytes it was derived from. Fail-closed: any resolver,
  /// decode, container, or dimension surprise answers false.
  [[nodiscard]] bool VerifyAdditiveEquivalentGlowOverlayContent(
      const Ogre::Pass &base_pass, const Ogre::Pass &overlay_pass) noexcept;

  [[nodiscard]] bool TryProjectCurrent(
      const Ogre::MaterialPtr &native_material, bool has_authored_uv0,
      const Render::Ogre14ManagedMaterialDeclarationBinding *managed_binding,
      bool allow_continuous_dust,
      Render::Ogre14GraphicsSceneMaterialCaptureInput &input,
      std::string &selected_projection_key, bool allow_new_projection,
      OgreNextDemoTextureProjectionExclusion &exclusion,
      Render::ValidationResult &failure);

  /// One row of the diagnostic census above.
  struct MaterialSectionCensusEntry final {
    std::size_t projected_sections = 0U;
    std::size_t matte_sections = 0U;
    OgreNextDemoTextureProjectionExclusion last_exclusion =
        OgreNextDemoTextureProjectionExclusion::NONE;
  };

  struct State;
  std::unique_ptr<State> committed_;
  std::unique_ptr<State> pending_;
  const Render::IOgre14AuthenticatedTextureResolver *texture_resolver_ =
      nullptr;
  const Render::IOgre14AuthenticatedTextureAuthorityProvider
      *texture_authority_provider_ = nullptr;
  const Render::IOgre14SelectedTextureSourceResolver
      *ordinary_texture_source_resolver_ = nullptr;
  const Render::IOgre14AuthenticatedMaterialScriptResolver
      *material_script_resolver_ = nullptr;
  OgreNextDemoMaterialSourceCounters lifetime_counters_;
  std::map<std::string, MaterialSectionCensusEntry, std::less<>>
      material_section_census_;
};

} // namespace RoR::Gfx::Detail
