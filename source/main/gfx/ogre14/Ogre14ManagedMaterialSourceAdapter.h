/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

/// @file
/// @brief OGRE 14 authority edge for neutral managed-material sources.

#pragma once

#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
#include "gfx/ogre14/Ogre14SelectedTextureSource.h"
#include "gfx/render/ManagedMaterialDeclaration.h"

#include <OgreTexture.h>
#include <OgreMaterial.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace RoR::Render {

enum class Ogre14ManagedMaterialSourceAuthorityKind : std::uint8_t {
  SELECTED_SOURCE = 1U,
  AUTHENTICATED_SOURCE = 2U,
};

/// Renderer-specific companion to one neutral source receipt. It retains the
/// exact strong TexturePtr and opaque registry-minted resolution, but neither
/// is serialized into or exposed as stable neutral identity. GfxScene can use
/// this narrow seam to revalidate current ContentManager authority after a
/// resource reload, group teardown, or actor lifetime transition.
class Ogre14ManagedMaterialSourceAuthorityBinding final {
public:
  Ogre14ManagedMaterialSourceAuthorityBinding() = default;
  ~Ogre14ManagedMaterialSourceAuthorityBinding() = default;
  Ogre14ManagedMaterialSourceAuthorityBinding(
      const Ogre14ManagedMaterialSourceAuthorityBinding &) noexcept = default;
  Ogre14ManagedMaterialSourceAuthorityBinding &operator=(
      const Ogre14ManagedMaterialSourceAuthorityBinding &) noexcept = default;
  Ogre14ManagedMaterialSourceAuthorityBinding(
      Ogre14ManagedMaterialSourceAuthorityBinding &&) noexcept = default;
  Ogre14ManagedMaterialSourceAuthorityBinding &operator=(
      Ogre14ManagedMaterialSourceAuthorityBinding &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] Ogre14ManagedMaterialSourceAuthorityKind kind() const
      noexcept;
  [[nodiscard]] RenderPayloadDigest neutral_source_identity_sha256() const
      noexcept;
  [[nodiscard]] bool Revalidate(
      const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
      const IOgre14SelectedTextureSourceResolver &selected_resolver) const
      noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14ManagedMaterialSourceAuthorityBinding &other) const noexcept;

private:
  struct State;
  explicit Ogre14ManagedMaterialSourceAuthorityBinding(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;

  friend class Ogre14ManagedMaterialSourceAdapter;
};

/// Runtime-only exact binding between one actor-composed OGRE material and its
/// neutral declaration. The strong MaterialPtr and source authority objects
/// are deliberately outside neutral serialization. Matching is valid only
/// while this binding revalidates against the current ContentManager.
class Ogre14ManagedMaterialDeclarationBinding final {
public:
  Ogre14ManagedMaterialDeclarationBinding() = default;
  ~Ogre14ManagedMaterialDeclarationBinding() = default;
  Ogre14ManagedMaterialDeclarationBinding(
      const Ogre14ManagedMaterialDeclarationBinding &) noexcept = default;
  Ogre14ManagedMaterialDeclarationBinding &operator=(
      const Ogre14ManagedMaterialDeclarationBinding &) noexcept = default;
  Ogre14ManagedMaterialDeclarationBinding(
      Ogre14ManagedMaterialDeclarationBinding &&) noexcept = default;
  Ogre14ManagedMaterialDeclarationBinding &operator=(
      Ogre14ManagedMaterialDeclarationBinding &&) noexcept = default;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const ManagedMaterialDeclaration *declaration() const noexcept;
  /// Tests only whether this binding owns the same exact Material object.
  /// Unlike MatchesExactMaterial(), this remains true after that object's
  /// state changes so a stale frame-reachable binding cannot be mistaken for
  /// an unmanaged material and routed through a generic fallback.
  [[nodiscard]] bool ReferencesExactMaterial(
      const Ogre::MaterialPtr &material) const noexcept;
  [[nodiscard]] bool MatchesExactMaterial(
      const Ogre::MaterialPtr &material) const noexcept;
  [[nodiscard]] bool Revalidate(
      const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
      const IOgre14SelectedTextureSourceResolver &selected_resolver) const
      noexcept;
  [[nodiscard]] bool SharesImmutableStateWith(
      const Ogre14ManagedMaterialDeclarationBinding &other) const noexcept;

  [[nodiscard]] static ValidationResult Build(
      const Ogre::MaterialPtr &material,
      const ManagedMaterialDeclaration &declaration,
      const std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
                       kManagedMaterialTextureSlotCount> &source_bindings,
      const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
      const IOgre14SelectedTextureSourceResolver &selected_resolver,
      Ogre14ManagedMaterialDeclarationBinding &output);

private:
  struct State;
  explicit Ogre14ManagedMaterialDeclarationBinding(
      std::shared_ptr<const State> state) noexcept;
  std::shared_ptr<const State> state_;
};

/// Revalidates one immutable actor publication against only the bindings whose
/// source-backed projections are roots of the current frame transaction. The
/// complete published binding set must still match the neutral snapshot
/// structurally, but source authority is deliberately checked only for
/// `reachable_bindings`. This prevents an unused or matte-only declaration
/// invalidated by a resource reload from rejecting an otherwise independent
/// frame while retaining fail-closed validation for every managed source that
/// can escape in that frame.
[[nodiscard]] ValidationResult ValidateOgre14ReachableManagedMaterialBindings(
    const ManagedMaterialDeclarationSnapshot &snapshot,
    const std::vector<Ogre14ManagedMaterialDeclarationBinding>
        &published_bindings,
    const std::vector<Ogre14ManagedMaterialDeclarationBinding>
        &reachable_bindings,
    const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
    const IOgre14SelectedTextureSourceResolver &selected_resolver);

/// The only selected/authenticated issuer for neutral V1 source receipts. Inputs must
/// be opaque resolutions minted by the current ContentManager resolver. Both
/// outputs are staged and assigned together only after a final current-source
/// revalidation; failure leaves both untouched.
class Ogre14ManagedMaterialSourceAdapter final {
public:
  [[nodiscard]] static ValidationResult BuildSelected(
      const Ogre::TexturePtr &texture,
      const IOgre14AuthenticatedTextureResolver &authenticated_classifier,
      const IOgre14SelectedTextureSourceResolver &selected_resolver,
      const Ogre14SelectedTextureSourceResolution &resolution,
      const ManagedMaterialDeclarationRegistryConfiguration &configuration,
      ManagedMaterialTextureSourceReceipt &receipt_output,
      Ogre14ManagedMaterialSourceAuthorityBinding &binding_output,
      const ManagedMaterialTextureSourceReceipt *reusable_receipt = nullptr,
      IManagedMaterialDeclarationFaultInjector *fault_injector = nullptr);

  [[nodiscard]] static ValidationResult BuildAuthenticated(
      const Ogre::TexturePtr &texture,
      const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
      const Ogre14AuthenticatedTextureResolution &resolution,
      const ManagedMaterialDeclarationRegistryConfiguration &configuration,
      ManagedMaterialTextureSourceReceipt &receipt_output,
      Ogre14ManagedMaterialSourceAuthorityBinding &binding_output,
      const ManagedMaterialTextureSourceReceipt *reusable_receipt = nullptr,
      IManagedMaterialDeclarationFaultInjector *fault_injector = nullptr);
};

} // namespace RoR::Render
