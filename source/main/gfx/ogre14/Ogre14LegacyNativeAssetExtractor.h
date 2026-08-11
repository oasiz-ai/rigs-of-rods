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

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace Ogre {
class Material;
}

namespace RoR::Render {

constexpr std::uint32_t kOgre14LegacyNativeAssetExtractorVersion = 2U;
constexpr std::uint32_t kOgre14LegacyNativeMaterialAuditReceiptVersion = 2U;
constexpr std::uint32_t
    kOgre14LegacyNativeMaterialDeclarationSerializationVersion = 1U;
constexpr std::size_t kOgre14LegacyNativeMaterialDeclarationDigestBytes = 32U;
constexpr std::size_t
    kOgre14LegacyNativeMaterialDeclarationMaximumCanonicalBytes = 64U * 1024U;
constexpr std::uint32_t
    kOgre14LegacyNativeMaterialCaptureSerializationVersion = 1U;

using Ogre14LegacyNativeMaterialDeclarationSha256 =
    std::array<std::uint8_t,
               kOgre14LegacyNativeMaterialDeclarationDigestBytes>;
using Ogre14LegacyNativeMaterialCaptureSha256 =
    std::array<std::uint8_t,
               kOgre14LegacyNativeMaterialDeclarationDigestBytes>;

enum class Ogre14LegacyNativeMaterialDeclarationDigestStage : std::uint8_t {
  AFTER_MATERIAL_IDENTITY = 0U,
  AFTER_PASS_STATE = 1U,
  AFTER_TEXTURE_UNIT = 2U,
  BEFORE_DIGEST_COMMIT = 3U,
  BEFORE_FRESHNESS_REVALIDATION = 4U,
};

class IOgre14LegacyNativeMaterialDeclarationDigestFaultInjector {
public:
  virtual ~IOgre14LegacyNativeMaterialDeclarationDigestFaultInjector() =
      default;
  /// Borrowed, current-thread test seam. Production never installs one.
  /// Implementations may throw; native capture remains transactional.
  virtual void BeforeNativeMaterialDeclarationDigestStage(
      Ogre14LegacyNativeMaterialDeclarationDigestStage) {}
};

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
/// Installs a borrowed fault injector for the calling thread only. Tests must
/// restore null before the injector's lifetime ends.
void SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting(
    IOgre14LegacyNativeMaterialDeclarationDigestFaultInjector *fault_injector)
    noexcept;
}

#if defined(ROR_OGRE14_NATIVE_MATERIAL_AUDIT_INTERNAL_TESTING)
namespace Testing {
class Ogre14LegacyNativeMaterialAuditTestAccess;
}
#endif

/// Opaque proof that the OGRE-native extractor allocated one exact immutable
/// audit owner and minted one canonical native-declaration digest. Only the
/// native capture functions can construct a nonempty production receipt.
/// Copying preserves authority, while reboxing the audit or altering the
/// declaration digest or authenticated public capture invalidates it. Digest
/// arrays are values, so an identical byte-for-byte copy is intentionally
/// equivalent rather than a distinct authority owner.
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
      const Ogre14LegacyNativeMaterialCapture &capture) const noexcept;
  [[nodiscard]] bool SharesNativeDeclarationAuthorityWith(
      const Ogre14LegacyNativeMaterialAuditReceipt &other) const noexcept {
    return version_ == kOgre14LegacyNativeMaterialAuditReceiptVersion &&
           other.version_ == kOgre14LegacyNativeMaterialAuditReceiptVersion &&
           declaration_serialization_version_ ==
               kOgre14LegacyNativeMaterialDeclarationSerializationVersion &&
           declaration_serialization_version_ ==
               other.declaration_serialization_version_ &&
           declaration_sha256_ == other.declaration_sha256_ &&
           capture_sha256_ == other.capture_sha256_ &&
           has_authenticated_texture_resolution_ ==
               other.has_authenticated_texture_resolution_ &&
           (!has_authenticated_texture_resolution_ ||
            authenticated_texture_resolution_
                .SharesLoadedResourceAuthorityWith(
                    other.authenticated_texture_resolution_)) &&
           owner_ != nullptr && other.owner_ != nullptr &&
           owner_.get() == other.owner_.get() &&
           !owner_.owner_before(other.owner_) &&
           !other.owner_.owner_before(owner_);
  }

private:
  explicit Ogre14LegacyNativeMaterialAuditReceipt(
      std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> owner,
      std::uint32_t declaration_serialization_version,
      Ogre14LegacyNativeMaterialDeclarationSha256 declaration_sha256,
      Ogre14LegacyNativeMaterialCaptureSha256 capture_sha256,
      const Ogre14AuthenticatedTextureResolution
          *authenticated_texture_resolution) noexcept
      : owner_(std::move(owner)),
        declaration_serialization_version_(
            declaration_serialization_version),
        declaration_sha256_(std::move(declaration_sha256)),
        capture_sha256_(std::move(capture_sha256)),
        authenticated_texture_resolution_(
            authenticated_texture_resolution != nullptr
                ? *authenticated_texture_resolution
                : Ogre14AuthenticatedTextureResolution{}),
        has_authenticated_texture_resolution_(
            authenticated_texture_resolution != nullptr) {}

  std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> owner_;
  std::uint32_t declaration_serialization_version_ = 0U;
  Ogre14LegacyNativeMaterialDeclarationSha256 declaration_sha256_{};
  Ogre14LegacyNativeMaterialCaptureSha256 capture_sha256_{};
  Ogre14AuthenticatedTextureResolution authenticated_texture_resolution_;
  bool has_authenticated_texture_resolution_ = false;
  std::uint32_t version_ = kOgre14LegacyNativeMaterialAuditReceiptVersion;

  friend ValidationResult CaptureOgre14LegacyNativeMaterial(
      const Ogre::Material &, const Ogre14LegacyNativeMaterialDeclaration &,
      Ogre14LegacyNativeMaterialCapture &);
  friend ValidationResult CaptureOgre14LegacyNativeMaterial(
      const Ogre::Material &, const Ogre14LegacyNativeMaterialDeclaration &,
      const IOgre14AuthenticatedTextureResolver &,
      Ogre14LegacyNativeMaterialCapture &);
#if defined(ROR_OGRE14_NATIVE_MATERIAL_AUDIT_INTERNAL_TESTING)
  friend class Testing::Ogre14LegacyNativeMaterialAuditTestAccess;
#endif
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
  /// Canonical little-endian declaration format and SHA-256 minted directly
  /// from the admitted OGRE Material/Technique/Pass/TextureUnitState graph.
  /// The zero/default pair carries no authority; only the receipt below can
  /// authenticate a nonempty native result.
  std::uint32_t native_material_declaration_serialization_version = 0U;
  Ogre14LegacyNativeMaterialDeclarationSha256
      native_material_declaration_sha256{};
  /// Opaque receipt bound to the exact audit owner and declaration digest
  /// above. Reboxing/replacing the audit or altering the public digest or any
  /// authenticated capture field invalidates the capture, including a
  /// translated closure owner with an equal value. An identical copied digest
  /// array retains its ordinary value equality.
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

/// Computes the version-1 renderer-neutral projection authenticated inside the
/// opaque native receipt. It covers every mutable material and texture field,
/// including exact mip bytes, but mints no authority. Failure leaves `sha256`
/// untouched.
[[nodiscard]] ValidationResult ComputeOgre14LegacyNativeMaterialCaptureSha256(
    const Ogre14LegacyNativeMaterialCapture &capture,
    Ogre14LegacyNativeMaterialCaptureSha256 &sha256) noexcept;

#if defined(ROR_OGRE14_NATIVE_MATERIAL_AUDIT_INTERNAL_TESTING)
namespace Testing {
/// Compile-only synthetic authority. This type and its seal entry point do not
/// exist in production translation units, so external code cannot define a
/// same-named friend and construct caller-authored receipt authority.
class Ogre14LegacyNativeMaterialAuditTestAccess final {
public:
  [[nodiscard]] static ValidationResult
  SealSyntheticCapture(Ogre14LegacyNativeMaterialCapture &capture) noexcept {
    try {
      Ogre14LegacyMaterialPipelineAudit value;
      ValidationResult validation =
          DeriveOgre14LegacyMaterialPipelineAudit(capture.material, value);
      if (!validation) {
        return validation;
      }
      capture.exact_native_material_audit =
          std::make_shared<const Ogre14LegacyMaterialPipelineAudit>(
              std::move(value));
      capture.native_material_declaration_serialization_version =
          kOgre14LegacyNativeMaterialDeclarationSerializationVersion;
      capture.native_material_declaration_sha256.fill(0U);
      capture.native_material_declaration_sha256.front() = 0xA5U;
      return SealExistingSyntheticCapture(capture);
    } catch (const std::bad_alloc &) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "native_capture.synthetic_allocation",
          "allocation failed while sealing a synthetic native capture");
    } catch (...) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "native_capture.synthetic_exception",
          "unexpected exception while sealing a synthetic native capture");
    }
  }

  [[nodiscard]] static ValidationResult SealExistingSyntheticCapture(
      Ogre14LegacyNativeMaterialCapture &capture) noexcept {
    if (capture.authenticated_texture_resolutions.size() > 1U) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH,
          "native_capture.authenticated_texture_resolutions",
          "v1 synthetic native capture has more than one resolution");
    }
    Ogre14LegacyNativeMaterialCaptureSha256 capture_sha256;
    ValidationResult validation =
        ComputeOgre14LegacyNativeMaterialCaptureSha256(capture,
                                                       capture_sha256);
    if (!validation) {
      return validation;
    }
    capture.native_material_audit_receipt =
        Ogre14LegacyNativeMaterialAuditReceipt(
            capture.exact_native_material_audit,
            capture.native_material_declaration_serialization_version,
            capture.native_material_declaration_sha256,
            std::move(capture_sha256),
            capture.authenticated_texture_resolutions.empty()
                ? nullptr
                : &capture.authenticated_texture_resolutions.front());
    return ValidationResult::Success();
  }

  [[nodiscard]] static ValidationResult
  SealOpaqueSentinel(Ogre14LegacyNativeMaterialCapture &capture) noexcept {
    try {
      capture.exact_native_material_audit =
          std::make_shared<const Ogre14LegacyMaterialPipelineAudit>();
      capture.native_material_declaration_serialization_version =
          kOgre14LegacyNativeMaterialDeclarationSerializationVersion;
      capture.native_material_declaration_sha256.fill(0x5AU);
      return SealExistingSyntheticCapture(capture);
    } catch (const std::bad_alloc &) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "native_capture.synthetic_allocation",
          "allocation failed while sealing a synthetic sentinel");
    } catch (...) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "native_capture.synthetic_exception",
          "unexpected exception while sealing a synthetic sentinel");
    }
  }

  Ogre14LegacyNativeMaterialAuditTestAccess() = delete;
};
} // namespace Testing
#endif

/// Reads one already-loaded immutable Material and its optional already-loaded
/// 2D texture. It performs CPU readback through OGRE's PixelUtil/PF_BYTE_RGBA
/// path, which defines RGBA byte order on either host endianness. Exceptions,
/// unsupported native state, mutation between the exact before/after canonical
/// observations, or readback failures leave `capture` untouched. The caller
/// must execute on the serialized OGRE resource/render owner thread and exclude
/// Material/Technique/Pass/TextureUnitState/Sampler/Texture mutation for the
/// full call.
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
