/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14ManagedMaterialSourceAdapter.h"

#include <cstdint>
#include <new>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

bool ParseLowercaseSha256(const std::string &text,
                         RenderPayloadDigest &digest) noexcept {
  if (text.size() != digest.size() * 2U) {
    return false;
  }
  auto nibble = [](char character, std::uint8_t &value) noexcept {
    if (character >= '0' && character <= '9') {
      value = static_cast<std::uint8_t>(character - '0');
      return true;
    }
    if (character >= 'a' && character <= 'f') {
      value = static_cast<std::uint8_t>(character - 'a' + 10);
      return true;
    }
    return false;
  };
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    std::uint8_t high = 0U;
    std::uint8_t low = 0U;
    if (!nibble(text[index * 2U], high) ||
        !nibble(text[index * 2U + 1U], low)) {
      return false;
    }
    digest[index] = static_cast<std::uint8_t>((high << 4U) | low);
  }
  return true;
}

bool ExactSelectedIdentity(
    const Ogre::TexturePtr &texture,
    const IOgre14SelectedTextureSourceResolver &resolver,
    const Ogre14SelectedTextureSourceResolution &resolution) noexcept {
  if (!texture || !resolution.initialized() ||
      !resolution.MatchesResolver(resolver)) {
    return false;
  }
  const std::size_t native_state_count = texture->getStateCount();
  const std::uint64_t state_count =
      static_cast<std::uint64_t>(native_state_count);
  return static_cast<std::size_t>(state_count) == native_state_count &&
         resolution.MatchesLoadedResourceIdentity(
             reinterpret_cast<std::uintptr_t>(texture.get()),
             static_cast<std::uint64_t>(texture->getHandle()),
             texture->getGroup(), texture->getName(), state_count);
}

bool ExactAuthenticatedIdentity(
    const Ogre::TexturePtr &texture,
    const IOgre14AuthenticatedTextureResolver &resolver,
    const Ogre14AuthenticatedTextureResolution &resolution) noexcept {
  if (!texture || !resolution.initialized() ||
      !resolution.MatchesResolver(resolver)) {
    return false;
  }
  const std::size_t native_state_count = texture->getStateCount();
  const std::uint64_t state_count =
      static_cast<std::uint64_t>(native_state_count);
  return static_cast<std::size_t>(state_count) == native_state_count &&
         resolution.MatchesLoadedResourceIdentity(
             reinterpret_cast<std::uintptr_t>(texture.get()),
             static_cast<std::uint64_t>(texture->getHandle()),
             texture->getGroup(), texture->getName(), state_count);
}

ValidationResult SelectedIdentity(
    const Ogre14SelectedTextureSourceReceipt &source,
    ManagedMaterialTextureSourceIdentity &identity) {
  const Ogre14SelectedTextureSourceReceiptMetadata *metadata =
      source.metadata();
  if (metadata == nullptr || source.source_bytes() == nullptr ||
      source.source_size() == 0U) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "managed_material_ogre14.selected_source",
                   "selected-source resolution has no immutable source bytes");
  }
  if (metadata->source.source_kind !=
      Ogre14SelectedTextureSourceKind::
          UNAUTHENTICATED_PACKAGE_ARCHIVE_MEMBER) {
    return Failure(ValidationCode::INVALID_ENUM,
                   "managed_material_ogre14.selected_source_kind",
                   "selected-source receipt trust kind is unsupported");
  }
  identity.trust =
      ManagedMaterialSourceTrust::OBSERVED_SELECTED_SOURCE;
  identity.group_generation = metadata->source.group_generation;
  identity.effective_resource_group =
      metadata->source.effective_resource_group;
  identity.archive_name = metadata->source.selected_archive_name;
  identity.archive_type = metadata->source.selected_archive_type;
  identity.exact_member_name = metadata->source.exact_member_name;
  identity.exact_resource_name = metadata->source.exact_resource_name;
  identity.byte_count = metadata->byte_count;
  if (!ParseLowercaseSha256(metadata->observed_bytes_sha256,
                           identity.source_sha256)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "managed_material_ogre14.selected_sha256",
                   "selected-source receipt SHA-256 is malformed");
  }
  return ValidationResult::Success();
}

ValidationResult AuthenticatedIdentity(
    const Ogre14AuthenticatedTextureReceipt &source,
    ManagedMaterialTextureSourceIdentity &identity) {
  const Ogre14AuthenticatedTextureReceiptMetadata *metadata =
      source.metadata();
  if (metadata == nullptr || source.source_bytes() == nullptr ||
      source.source_size() == 0U) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "managed_material_ogre14.authenticated_source",
                   "authenticated resolution has no immutable source bytes");
  }
  switch (metadata->source.source_kind) {
  case Ogre14AuthenticatedTextureSourceKind::AUTHENTICATED_ARCHIVE_MEMBER:
    identity.trust =
        ManagedMaterialSourceTrust::AUTHENTICATED_ARCHIVE_MEMBER;
    break;
  case Ogre14AuthenticatedTextureSourceKind::VERSIONED_GENERATED_FALLBACK:
    identity.trust =
        ManagedMaterialSourceTrust::AUTHENTICATED_GENERATED_FALLBACK;
    break;
  default:
    return Failure(ValidationCode::INVALID_ENUM,
                   "managed_material_ogre14.authenticated_source_kind",
                   "authenticated receipt source kind is unsupported");
  }
  identity.group_generation = metadata->source.group_generation;
  identity.effective_resource_group =
      metadata->source.effective_resource_group;
  identity.archive_identity = metadata->source.archive_identity;
  identity.archive_name = metadata->source.archive_name;
  identity.archive_type = metadata->source.archive_type;
  identity.archive_sha256 = metadata->source.archive_sha256;
  identity.exact_member_name = metadata->source.exact_member_name;
  identity.exact_resource_name =
      metadata->source.binding.exact_resource_name;
  identity.generated_rule = metadata->source.generated_fallback_rule;
  identity.generated_rule_version =
      metadata->source.generated_fallback_rule_version;
  identity.byte_count = metadata->byte_count;
  if (!ParseLowercaseSha256(metadata->bytes_sha256,
                           identity.source_sha256)) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "managed_material_ogre14.authenticated_sha256",
                   "authenticated receipt SHA-256 is malformed");
  }
  return ValidationResult::Success();
}

bool CanReuseNeutralSource(
    const ManagedMaterialTextureSourceReceipt &receipt,
    const ManagedMaterialTextureSourceIdentity &identity,
    std::size_t byte_count,
    const ManagedMaterialDeclarationRegistryConfiguration &configuration)
    noexcept {
  const ManagedMaterialTextureSourceIdentity *existing = receipt.identity();
  return existing != nullptr && receipt.source_size() == byte_count &&
         static_cast<std::uint64_t>(byte_count) <=
             configuration.maximum_source_bytes &&
         static_cast<std::uint64_t>(byte_count) <=
             configuration.maximum_retained_source_bytes &&
         receipt.retained_identity_bytes() <=
             configuration.maximum_retained_identity_bytes &&
         existing->version == identity.version &&
         existing->trust == identity.trust &&
         existing->group_generation == identity.group_generation &&
         existing->effective_resource_group ==
             identity.effective_resource_group &&
         existing->archive_identity == identity.archive_identity &&
         existing->archive_name == identity.archive_name &&
         existing->archive_type == identity.archive_type &&
         existing->archive_sha256 == identity.archive_sha256 &&
         existing->exact_member_name == identity.exact_member_name &&
         existing->exact_resource_name == identity.exact_resource_name &&
         existing->generated_rule == identity.generated_rule &&
         existing->generated_rule_version == identity.generated_rule_version &&
         existing->byte_count == identity.byte_count &&
         existing->source_sha256 == identity.source_sha256;
}

} // namespace

struct Ogre14ManagedMaterialSourceAuthorityBinding::State final {
  Ogre14ManagedMaterialSourceAuthorityKind kind =
      Ogre14ManagedMaterialSourceAuthorityKind::SELECTED_SOURCE;
  RenderPayloadDigest neutral_source_identity_sha256{};
  Ogre::TexturePtr texture;
  Ogre14SelectedTextureSourceResolution selected_resolution;
  Ogre14AuthenticatedTextureResolution authenticated_resolution;
};

Ogre14ManagedMaterialSourceAuthorityBinding::
    Ogre14ManagedMaterialSourceAuthorityBinding(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14ManagedMaterialSourceAuthorityBinding::initialized() const
    noexcept {
  return state_ != nullptr;
}

Ogre14ManagedMaterialSourceAuthorityKind
Ogre14ManagedMaterialSourceAuthorityBinding::kind() const noexcept {
  return state_ != nullptr ? state_->kind
                           : Ogre14ManagedMaterialSourceAuthorityKind::
                                 SELECTED_SOURCE;
}

RenderPayloadDigest Ogre14ManagedMaterialSourceAuthorityBinding::
    neutral_source_identity_sha256() const noexcept {
  return state_ != nullptr ? state_->neutral_source_identity_sha256
                           : RenderPayloadDigest{};
}

bool Ogre14ManagedMaterialSourceAuthorityBinding::Revalidate(
    const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
    const IOgre14SelectedTextureSourceResolver &selected_resolver) const
    noexcept {
  try {
    if (state_ == nullptr || !state_->texture) {
      return false;
    }
    switch (state_->kind) {
    case Ogre14ManagedMaterialSourceAuthorityKind::SELECTED_SOURCE:
      return !authenticated_resolver.RequiresAuthenticatedTextureSource(
                 *state_->texture) &&
             ExactSelectedIdentity(state_->texture, selected_resolver,
                                   state_->selected_resolution) &&
             selected_resolver.RevalidateSelectedTextureSource(
                 *state_->texture, state_->selected_resolution);
    case Ogre14ManagedMaterialSourceAuthorityKind::AUTHENTICATED_SOURCE:
      return authenticated_resolver.RequiresAuthenticatedTextureSource(
                 *state_->texture) &&
             ExactAuthenticatedIdentity(state_->texture,
                                        authenticated_resolver,
                                        state_->authenticated_resolution) &&
             authenticated_resolver.RevalidateAuthenticatedTexture(
                 *state_->texture, state_->authenticated_resolution);
    }
    return false;
  } catch (...) {
    return false;
  }
}

bool Ogre14ManagedMaterialSourceAuthorityBinding::SharesImmutableStateWith(
    const Ogre14ManagedMaterialSourceAuthorityBinding &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

struct Ogre14ManagedMaterialDeclarationBinding::State final {
  Ogre::MaterialPtr material;
  std::uint64_t material_handle = 0U;
  std::uint64_t material_state_count = 0U;
  std::string material_name;
  std::string material_group;
  ManagedMaterialDeclaration declaration;
  std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
             kManagedMaterialTextureSlotCount>
      source_bindings{};
};

Ogre14ManagedMaterialDeclarationBinding::
    Ogre14ManagedMaterialDeclarationBinding(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14ManagedMaterialDeclarationBinding::initialized() const noexcept {
  return state_ != nullptr;
}

const ManagedMaterialDeclaration *
Ogre14ManagedMaterialDeclarationBinding::declaration() const noexcept {
  return state_ != nullptr ? &state_->declaration : nullptr;
}

bool Ogre14ManagedMaterialDeclarationBinding::MatchesExactMaterial(
    const Ogre::MaterialPtr &material) const noexcept {
  try {
    if (state_ == nullptr || !state_->material || !material ||
        state_->material.get() != material.get()) {
      return false;
    }
    const std::size_t native_state_count = material->getStateCount();
    const std::uint64_t state_count =
        static_cast<std::uint64_t>(native_state_count);
    return static_cast<std::size_t>(state_count) == native_state_count &&
           static_cast<std::uint64_t>(material->getHandle()) ==
               state_->material_handle &&
           state_count == state_->material_state_count &&
           material->getName() == state_->material_name &&
           material->getGroup() == state_->material_group;
  } catch (...) {
    return false;
  }
}

bool Ogre14ManagedMaterialDeclarationBinding::Revalidate(
    const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
    const IOgre14SelectedTextureSourceResolver &selected_resolver) const
    noexcept {
  try {
    if (state_ == nullptr || !MatchesExactMaterial(state_->material)) {
      return false;
    }
    const ManagedMaterialDeclarationMetadata *metadata =
        state_->declaration.metadata();
    if (metadata == nullptr) {
      return false;
    }
    for (std::size_t slot = 0U; slot < kManagedMaterialTextureSlotCount;
         ++slot) {
      const bool source_present =
          metadata->textures[slot].source_receipt_present;
      const auto &binding = state_->source_bindings[slot];
      if (source_present != binding.initialized()) {
        return false;
      }
      if (!source_present) {
        continue;
      }
      const ManagedMaterialTextureSourceReceipt *receipt =
          state_->declaration.source_receipt(
              static_cast<ManagedMaterialTextureSlot>(slot));
      if (receipt == nullptr || receipt->identity() == nullptr ||
          receipt->identity()->trust == ManagedMaterialSourceTrust::
                                             CALLER_SUPPLIED_UNAUTHENTICATED_BYTES ||
          binding.neutral_source_identity_sha256() !=
              receipt->canonical_identity_sha256() ||
          !binding.Revalidate(authenticated_resolver, selected_resolver)) {
        return false;
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool Ogre14ManagedMaterialDeclarationBinding::SharesImmutableStateWith(
    const Ogre14ManagedMaterialDeclarationBinding &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult Ogre14ManagedMaterialDeclarationBinding::Build(
    const Ogre::MaterialPtr &material,
    const ManagedMaterialDeclaration &declaration,
    const std::array<Ogre14ManagedMaterialSourceAuthorityBinding,
                     kManagedMaterialTextureSlotCount> &source_bindings,
    const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
    const IOgre14SelectedTextureSourceResolver &selected_resolver,
    Ogre14ManagedMaterialDeclarationBinding &output) {
  try {
    const ManagedMaterialDeclarationMetadata *metadata =
        declaration.metadata();
    if (!material || metadata == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "managed_material_ogre14.declaration_binding",
                     "material and neutral declaration are required");
    }
    for (std::size_t slot = 0U; slot < kManagedMaterialTextureSlotCount;
         ++slot) {
      const bool source_present =
          metadata->textures[slot].source_receipt_present;
      if (source_present != source_bindings[slot].initialized()) {
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "managed_material_ogre14.source_binding",
                       "source receipt and live authority binding sets differ");
      }
      if (!source_present) {
        continue;
      }
      const ManagedMaterialTextureSourceReceipt *receipt =
          declaration.source_receipt(
              static_cast<ManagedMaterialTextureSlot>(slot));
      if (receipt == nullptr || receipt->identity() == nullptr ||
          receipt->identity()->trust == ManagedMaterialSourceTrust::
                                             CALLER_SUPPLIED_UNAUTHENTICATED_BYTES ||
          source_bindings[slot].neutral_source_identity_sha256() !=
              receipt->canonical_identity_sha256()) {
        return Failure(ValidationCode::INVALID_ASSET_REFERENCE,
                       "managed_material_ogre14.source_binding",
                       "source authority does not match the sealed neutral receipt");
      }
    }

    const std::size_t native_state_count = material->getStateCount();
    const std::uint64_t state_count =
        static_cast<std::uint64_t>(native_state_count);
    if (static_cast<std::size_t>(state_count) != native_state_count ||
        material->getName().empty() || material->getGroup().empty() ||
        material->getName().size() > kManagedMaterialMaximumIdentifierBytes ||
        material->getGroup().size() > kManagedMaterialMaximumIdentifierBytes) {
      return Failure(ValidationCode::INVALID_IDENTIFIER,
                     "managed_material_ogre14.material_identity",
                     "exact material runtime identity is invalid");
    }

    auto candidate =
        std::make_shared<Ogre14ManagedMaterialDeclarationBinding::State>();
    candidate->material = material;
    candidate->material_handle =
        static_cast<std::uint64_t>(material->getHandle());
    candidate->material_state_count = state_count;
    candidate->material_name = material->getName();
    candidate->material_group = material->getGroup();
    candidate->declaration = declaration;
    candidate->source_bindings = source_bindings;
    Ogre14ManagedMaterialDeclarationBinding staged(
        std::shared_ptr<const Ogre14ManagedMaterialDeclarationBinding::State>(
            std::move(candidate)));
    if (!staged.Revalidate(authenticated_resolver, selected_resolver)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "managed_material_ogre14.declaration_revalidation",
                     "material or texture source authority changed before publication");
    }
    output = std::move(staged);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_ogre14.declaration_allocation",
                   "allocation failed before declaration binding publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_ogre14.declaration_exception",
                   "unexpected exception before declaration binding publication");
  }
}

ValidationResult Ogre14ManagedMaterialSourceAdapter::BuildSelected(
    const Ogre::TexturePtr &texture,
    const IOgre14AuthenticatedTextureResolver &authenticated_classifier,
    const IOgre14SelectedTextureSourceResolver &selected_resolver,
    const Ogre14SelectedTextureSourceResolution &resolution,
    const ManagedMaterialDeclarationRegistryConfiguration &configuration,
    ManagedMaterialTextureSourceReceipt &receipt_output,
    Ogre14ManagedMaterialSourceAuthorityBinding &binding_output,
    const ManagedMaterialTextureSourceReceipt *reusable_receipt,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  try {
    const ValidationResult configuration_validation =
        ValidateManagedMaterialDeclarationRegistryConfiguration(configuration);
    if (!configuration_validation) {
      return configuration_validation;
    }
    if (!texture ||
        authenticated_classifier.RequiresAuthenticatedTextureSource(*texture) ||
        !ExactSelectedIdentity(texture, selected_resolver, resolution) ||
        !selected_resolver.RevalidateSelectedTextureSource(*texture,
                                                           resolution)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "managed_material_ogre14.selected_authority",
                     "selected-source resolution is not current for the exact loaded texture");
    }
    const Ogre14SelectedTextureSourceReceipt *source =
        resolution.source_receipt();
    if (source == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "managed_material_ogre14.selected_resolution",
                     "selected-source resolution contains no source receipt");
    }
    ManagedMaterialTextureSourceIdentity identity;
    const ValidationResult identity_result = SelectedIdentity(*source, identity);
    if (!identity_result) {
      return identity_result;
    }
    ManagedMaterialTextureSourceReceipt staged_receipt;
    if (reusable_receipt != nullptr &&
        CanReuseNeutralSource(*reusable_receipt, identity,
                              source->source_size(), configuration)) {
      staged_receipt = *reusable_receipt;
    } else {
      const ValidationResult build =
          ManagedMaterialTextureSourceReceipt::BuildFromTrustedIssuer(
              configuration, identity, source->source_bytes(),
              source->source_size(), staged_receipt, fault_injector);
      if (!build) {
        return build;
      }
    }
    auto state =
        std::make_shared<Ogre14ManagedMaterialSourceAuthorityBinding::State>();
    state->kind = Ogre14ManagedMaterialSourceAuthorityKind::SELECTED_SOURCE;
    state->neutral_source_identity_sha256 =
        staged_receipt.canonical_identity_sha256();
    state->texture = texture;
    state->selected_resolution = resolution;
    Ogre14ManagedMaterialSourceAuthorityBinding staged_binding(
        std::shared_ptr<
            const Ogre14ManagedMaterialSourceAuthorityBinding::State>(
            std::move(state)));
    if (!staged_binding.Revalidate(authenticated_classifier,
                                   selected_resolver)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "managed_material_ogre14.selected_revalidation",
                     "selected source changed before neutral publication");
    }
    receipt_output = std::move(staged_receipt);
    binding_output = std::move(staged_binding);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_ogre14.selected_allocation",
                   "allocation failed before selected-source publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_ogre14.selected_exception",
                   "unexpected exception before selected-source publication");
  }
}

ValidationResult Ogre14ManagedMaterialSourceAdapter::BuildAuthenticated(
    const Ogre::TexturePtr &texture,
    const IOgre14AuthenticatedTextureResolver &authenticated_resolver,
    const Ogre14AuthenticatedTextureResolution &resolution,
    const ManagedMaterialDeclarationRegistryConfiguration &configuration,
    ManagedMaterialTextureSourceReceipt &receipt_output,
    Ogre14ManagedMaterialSourceAuthorityBinding &binding_output,
    const ManagedMaterialTextureSourceReceipt *reusable_receipt,
    IManagedMaterialDeclarationFaultInjector *fault_injector) {
  try {
    const ValidationResult configuration_validation =
        ValidateManagedMaterialDeclarationRegistryConfiguration(configuration);
    if (!configuration_validation) {
      return configuration_validation;
    }
    if (!texture ||
        !authenticated_resolver.RequiresAuthenticatedTextureSource(*texture) ||
        !ExactAuthenticatedIdentity(texture, authenticated_resolver,
                                    resolution) ||
        !authenticated_resolver.RevalidateAuthenticatedTexture(*texture,
                                                               resolution)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "managed_material_ogre14.authenticated_authority",
                     "authenticated resolution is not current for the exact loaded texture");
    }
    const Ogre14AuthenticatedTextureReceipt *source =
        resolution.source_receipt();
    if (source == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "managed_material_ogre14.authenticated_resolution",
                     "authenticated resolution contains no source receipt");
    }
    ManagedMaterialTextureSourceIdentity identity;
    const ValidationResult identity_result =
        AuthenticatedIdentity(*source, identity);
    if (!identity_result) {
      return identity_result;
    }
    ManagedMaterialTextureSourceReceipt staged_receipt;
    if (reusable_receipt != nullptr &&
        CanReuseNeutralSource(*reusable_receipt, identity,
                              source->source_size(), configuration)) {
      staged_receipt = *reusable_receipt;
    } else {
      const ValidationResult build =
          ManagedMaterialTextureSourceReceipt::BuildFromTrustedIssuer(
              configuration, identity, source->source_bytes(),
              source->source_size(), staged_receipt, fault_injector);
      if (!build) {
        return build;
      }
    }
    auto state =
        std::make_shared<Ogre14ManagedMaterialSourceAuthorityBinding::State>();
    state->kind =
        Ogre14ManagedMaterialSourceAuthorityKind::AUTHENTICATED_SOURCE;
    state->neutral_source_identity_sha256 =
        staged_receipt.canonical_identity_sha256();
    state->texture = texture;
    state->authenticated_resolution = resolution;
    Ogre14ManagedMaterialSourceAuthorityBinding staged_binding(
        std::shared_ptr<
            const Ogre14ManagedMaterialSourceAuthorityBinding::State>(
            std::move(state)));
    // The exact resolver performs the final allocation-free registry and
    // loaded-resource revalidation after every neutral byte/identity copy.
    if (!authenticated_resolver.RequiresAuthenticatedTextureSource(*texture) ||
        !ExactAuthenticatedIdentity(texture, authenticated_resolver,
                                    resolution) ||
        !authenticated_resolver.RevalidateAuthenticatedTexture(*texture,
                                                               resolution)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "managed_material_ogre14.authenticated_revalidation",
                     "authenticated source changed before neutral publication");
    }
    receipt_output = std::move(staged_receipt);
    binding_output = std::move(staged_binding);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "managed_material_ogre14.authenticated_allocation",
                   "allocation failed before authenticated-source publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "managed_material_ogre14.authenticated_exception",
                   "unexpected exception before authenticated-source publication");
  }
}

} // namespace RoR::Render
