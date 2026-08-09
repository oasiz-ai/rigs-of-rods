/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief OGRE 14.5.2-native capture edge for the pure legacy translator.

#pragma once

#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
#include "gfx/render/Ogre14LegacyAssetTranslator.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Ogre {
class Material;
}

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyNativeAssetExtractorVersion = 2U;
constexpr std::uint32_t kOgre14LegacyNativeMaterialAuditReceiptVersion = 1U;

/// PBR intent and texture color role must come from explicit content metadata
/// or a versioned compatibility table. The native extractor never guesses
/// either value from a resource name or legacy specular state.
struct Ogre14LegacyNativeMaterialDeclaration {
  std::uint32_t version = kOgre14LegacyNativeAssetExtractorVersion;
  Ogre14LegacyBaseColorSemantic base_color_semantic =
      Ogre14LegacyBaseColorSemantic::UNLIT;
  Ogre14LegacyTextureColorRole texture_color_role =
      Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
  /// Must match the translator which consumes this capture. Native readback
  /// applies the per-asset decoded-byte cap before allocating mip storage.
  Ogre14LegacyAssetTranslatorConfiguration translator_configuration;
};

struct Ogre14LegacyNativeMaterialCapture;
namespace Testing {
class Ogre14LegacyNativeMaterialAuditTestAccess;
}

/// Opaque proof that the OGRE-native extractor allocated one exact immutable
/// audit owner. Only the native capture function (and its focused test fixture)
/// can construct a nonempty receipt. Copying preserves authority, while
/// replacing or reboxing the audit invalidates it.
class Ogre14LegacyNativeMaterialAuditReceipt final {
public:
  Ogre14LegacyNativeMaterialAuditReceipt() noexcept = default;
  Ogre14LegacyNativeMaterialAuditReceipt(
      const Ogre14LegacyNativeMaterialAuditReceipt &) noexcept = default;
  Ogre14LegacyNativeMaterialAuditReceipt &
  operator=(const Ogre14LegacyNativeMaterialAuditReceipt &) noexcept = default;
  Ogre14LegacyNativeMaterialAuditReceipt(
      Ogre14LegacyNativeMaterialAuditReceipt &&) noexcept = default;
  Ogre14LegacyNativeMaterialAuditReceipt &
  operator=(Ogre14LegacyNativeMaterialAuditReceipt &&) noexcept = default;
  ~Ogre14LegacyNativeMaterialAuditReceipt() = default;

  [[nodiscard]] bool has_value() const noexcept { return owner_ != nullptr; }
  [[nodiscard]] std::uint32_t version() const noexcept { return version_; }
  [[nodiscard]] bool Authenticates(
      const std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> &audit)
      const noexcept {
    return version_ == kOgre14LegacyNativeMaterialAuditReceiptVersion &&
           owner_ != nullptr && audit != nullptr &&
           owner_.get() == audit.get() && !owner_.owner_before(audit) &&
           !audit.owner_before(owner_);
  }

private:
  explicit Ogre14LegacyNativeMaterialAuditReceipt(
      std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> owner) noexcept
      : owner_(std::move(owner)) {}

  std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> owner_;
  std::uint32_t version_ = kOgre14LegacyNativeMaterialAuditReceiptVersion;

  friend ValidationResult CaptureOgre14LegacyNativeMaterial(
      const Ogre::Material &, const Ogre14LegacyNativeMaterialDeclaration &,
      Ogre14LegacyNativeMaterialCapture &);
  friend ValidationResult CaptureOgre14LegacyNativeMaterial(
      const Ogre::Material &, const Ogre14LegacyNativeMaterialDeclaration &,
      const IOgre14AuthenticatedTextureResolver &,
      Ogre14LegacyNativeMaterialCapture &);
  friend class Testing::Ogre14LegacyNativeMaterialAuditTestAccess;
};

struct Ogre14LegacyNativeMaterialCapture {
  std::uint32_t version = kOgre14LegacyNativeAssetExtractorVersion;
  Ogre14LegacyMaterialInput material;
  std::vector<Ogre14LegacyTextureInput> textures;
  /// Independently allocated from the exact captured material/texture/sampler
  /// state before this capture is published. The translated closure owns a
  /// distinct audit with an equal value; downstream consumers retain this
  /// source owner as their proof of native state.
  std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit>
      exact_native_material_audit;
  /// Opaque receipt bound to the exact owner above. Reboxing or replacing the
  /// audit, including substituting a translated closure owner, invalidates it.
  Ogre14LegacyNativeMaterialAuditReceipt native_material_audit_receipt;
  /// Populated only by the authenticated overload below. When a texture is
  /// present this vector is exactly aligned 1:1 with `textures` and retains
  /// the registry snapshot plus the exact source-receipt control block. The
  /// compatibility overload deliberately leaves it empty, even for textured
  /// captures, so downstream code cannot confuse GPU readback with source
  /// authentication.
  std::vector<Ogre14AuthenticatedTextureResolution>
      authenticated_texture_resolutions;
};

/// Reads one already-loaded immutable Material and its optional already-loaded
/// 2D texture. It performs CPU readback through OGRE's PixelUtil/PF_BYTE_RGBA
/// path, which defines RGBA byte order on either host endianness. Exceptions,
/// unsupported native state, or readback failures leave `capture` untouched.
[[nodiscard]] ValidationResult CaptureOgre14LegacyNativeMaterial(
    const Ogre::Material &material,
    const Ogre14LegacyNativeMaterialDeclaration &declaration,
    Ogre14LegacyNativeMaterialCapture &capture);

/// Authenticated capture variant. After native readback it resolves the exact
/// TextureUnitState::_getTexturePtr(), reacquires that same pointer, and asks
/// the same registry-bound resolver for a final no-throw revalidation
/// immediately before publishing `capture`. Textured success always carries
/// one aligned resolution per texture; untextured success carries none.
[[nodiscard]] ValidationResult CaptureOgre14LegacyNativeMaterial(
    const Ogre::Material &material,
    const Ogre14LegacyNativeMaterialDeclaration &declaration,
    const IOgre14AuthenticatedTextureResolver &texture_resolver,
    Ogre14LegacyNativeMaterialCapture &capture);

} // namespace RoR::Render
