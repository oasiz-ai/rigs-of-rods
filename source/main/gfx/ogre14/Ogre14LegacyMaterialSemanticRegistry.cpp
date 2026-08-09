/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14LegacyMaterialSemanticRegistry.h"

#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace RoR::Render {
namespace {

constexpr std::uint64_t kFnvOffset64 = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime64 = 1099511628211ULL;

void HashByte(std::uint64_t &hash, std::uint8_t value) noexcept {
  hash ^= value;
  hash *= kFnvPrime64;
}

void HashU64(std::uint64_t &hash, std::uint64_t value) noexcept {
  for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
    HashByte(hash, static_cast<std::uint8_t>(value & 0xFFU));
    value >>= 8U;
  }
}

void HashString(std::uint64_t &hash, const std::string &value) noexcept {
  HashU64(hash, static_cast<std::uint64_t>(value.size()));
  for (const unsigned char byte : value) {
    HashByte(hash, byte);
  }
}

bool IsKnownSource(Ogre14LegacyMaterialSemanticSource source) noexcept {
  switch (source) {
  case Ogre14LegacyMaterialSemanticSource::CONTENT_METADATA:
  case Ogre14LegacyMaterialSemanticSource::VERSIONED_COMPATIBILITY_TABLE:
    return true;
  }
  return false;
}

bool IsKnownSemantic(Ogre14LegacyBaseColorSemantic semantic) noexcept {
  switch (semantic) {
  case Ogre14LegacyBaseColorSemantic::UNLIT:
  case Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR:
    return true;
  }
  return false;
}

bool IsKnownTextureRole(Ogre14LegacyTextureColorRole role) noexcept {
  switch (role) {
  case Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB:
  case Ogre14LegacyTextureColorRole::LINEAR_DATA:
    return true;
  }
  return false;
}

struct MaterialKeyLess final {
  bool operator()(const Ogre14LegacyAssetKey &lhs,
                  const Ogre14LegacyAssetKey &rhs) const noexcept {
    if (lhs.exact_resource_group != rhs.exact_resource_group) {
      return lhs.exact_resource_group < rhs.exact_resource_group;
    }
    return lhs.exact_name < rhs.exact_name;
  }
};

struct SemanticRecord final {
  Ogre14LegacyMaterialSemanticSource source =
      Ogre14LegacyMaterialSemanticSource::CONTENT_METADATA;
  std::uint64_t source_revision = 0U;
  Ogre14LegacyBaseColorSemantic base_color_semantic =
      Ogre14LegacyBaseColorSemantic::UNLIT;
  Ogre14LegacyTextureColorRole texture_color_role =
      Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB;
};

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail) {
  return ValidationResult::Failure(code, field, detail);
}

} // namespace

struct Ogre14LegacyMaterialSemanticRegistry::State final {
  Ogre14LegacyMaterialSemanticRegistryConfiguration configuration;
  std::map<Ogre14LegacyAssetKey, SemanticRecord, MaterialKeyLess> declarations;
  std::uint64_t content_fingerprint = 0U;
};

ValidationResult ValidateOgre14LegacyMaterialSemanticRegistryConfiguration(
    const Ogre14LegacyMaterialSemanticRegistryConfiguration &configuration) {
  if (configuration.version !=
      kOgre14LegacyMaterialSemanticRegistryVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "semantic_registry.configuration.version",
                   "unsupported semantic-registry configuration version");
  }
  if (configuration.maximum_declarations == 0U ||
      configuration.maximum_declarations >
          kDefaultOgre14LegacyMaximumSemanticDeclarations ||
      configuration.maximum_total_key_bytes == 0U ||
      configuration.maximum_total_key_bytes >
          kDefaultOgre14LegacyMaximumSemanticKeyBytes) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "semantic_registry.configuration.limits",
                   "semantic-registry limits are zero or exceed hard caps");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateOgre14LegacyMaterialSemanticDeclaration(
    const Ogre14LegacyMaterialSemanticDeclaration &declaration) {
  if (declaration.version !=
      kOgre14LegacyMaterialSemanticDeclarationVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "semantic_declaration.version",
                   "unsupported material semantic declaration version");
  }
  if (!IsKnownSource(declaration.source) ||
      !IsKnownSemantic(declaration.base_color_semantic) ||
      !IsKnownTextureRole(declaration.texture_color_role)) {
    return Failure(ValidationCode::INVALID_ENUM, "semantic_declaration",
                   "material semantic declaration contains an unknown enum");
  }
  if (declaration.source_revision == 0U ||
      declaration.source_revision ==
          (std::numeric_limits<std::uint64_t>::max)()) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "semantic_declaration.source_revision",
                   "explicit declaration source revision must be usable");
  }
  std::string stable_key;
  const ValidationResult key_validation = BuildOgre14LegacyStableAssetKey(
      RenderAssetKind::MATERIAL, declaration.material_key, stable_key);
  if (!key_validation) {
    return key_validation;
  }
  return ValidationResult::Success();
}

Ogre14LegacyMaterialSemanticRegistry::
    Ogre14LegacyMaterialSemanticRegistry(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14LegacyMaterialSemanticRegistry::initialized() const noexcept {
  return state_ != nullptr;
}

std::size_t Ogre14LegacyMaterialSemanticRegistry::size() const noexcept {
  return state_ != nullptr ? state_->declarations.size() : 0U;
}

std::uint64_t
Ogre14LegacyMaterialSemanticRegistry::content_fingerprint() const noexcept {
  return state_ != nullptr ? state_->content_fingerprint : 0U;
}

bool Ogre14LegacyMaterialSemanticRegistry::SharesImmutableStateWith(
    const Ogre14LegacyMaterialSemanticRegistry &other) const noexcept {
  return state_ != nullptr && state_ == other.state_;
}

ValidationResult Ogre14LegacyMaterialSemanticRegistry::Resolve(
    const Ogre14LegacyAssetKey &material_key,
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    Ogre14LegacyMaterialSemanticResolution &resolution) const {
  if (state_ == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE, "semantic_registry",
                   "semantic registry has not been initialized");
  }
  const ValidationResult configuration_validation =
      ValidateOgre14LegacyAssetTranslatorConfiguration(
          translator_configuration);
  if (!configuration_validation) {
    return configuration_validation;
  }
  std::string stable_key;
  const ValidationResult key_validation = BuildOgre14LegacyStableAssetKey(
      RenderAssetKind::MATERIAL, material_key, stable_key);
  if (!key_validation) {
    return key_validation;
  }
  const auto found = state_->declarations.find(material_key);
  if (found == state_->declarations.end()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "semantic_registry.material_key",
                   "exact material has no explicit semantic declaration");
  }
  try {
    Ogre14LegacyMaterialSemanticResolution candidate;
    candidate.material_key = found->first;
    candidate.source = found->second.source;
    candidate.source_revision = found->second.source_revision;
    candidate.registry_fingerprint = state_->content_fingerprint;
    candidate.native_declaration.base_color_semantic =
        found->second.base_color_semantic;
    candidate.native_declaration.texture_color_role =
        found->second.texture_color_role;
    candidate.native_declaration.translator_configuration =
        translator_configuration;
    resolution = std::move(candidate);
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_registry.resolution.allocation",
                   "allocation failed before semantic resolution commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_registry.resolution.allocation",
                   "semantic resolution exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_registry.resolution.exception",
                   "unexpected exception before semantic resolution commit");
  }
}

bool Ogre14LegacyMaterialSemanticResolutionMatchesKey(
    const Ogre14LegacyMaterialSemanticResolution &resolution,
    const Ogre14LegacyAssetKey &material_key) noexcept {
  return resolution.version ==
             kOgre14LegacyMaterialSemanticResolutionVersion &&
         resolution.material_key.exact_resource_group ==
             material_key.exact_resource_group &&
         resolution.material_key.exact_name == material_key.exact_name;
}

ValidationResult BuildOgre14LegacyMaterialSemanticRegistry(
    const Ogre14LegacyMaterialSemanticRegistryConfiguration &configuration,
    const std::vector<Ogre14LegacyMaterialSemanticDeclaration> &declarations,
    Ogre14LegacyMaterialSemanticRegistry &output,
    IOgre14LegacyMaterialSemanticRegistryFaultInjector *fault_injector) {
  const ValidationResult configuration_validation =
      ValidateOgre14LegacyMaterialSemanticRegistryConfiguration(configuration);
  if (!configuration_validation) {
    return configuration_validation;
  }
  if (declarations.size() > configuration.maximum_declarations) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "semantic_registry.declarations",
                   "semantic declaration count exceeds the configured cap");
  }

  try {
    auto candidate = std::make_shared<
        Ogre14LegacyMaterialSemanticRegistry::State>();
    candidate->configuration = configuration;
    std::uint64_t total_key_bytes = 0U;
    bool injected_after_first = false;
    for (const Ogre14LegacyMaterialSemanticDeclaration &declaration :
         declarations) {
      const ValidationResult validation =
          ValidateOgre14LegacyMaterialSemanticDeclaration(declaration);
      if (!validation) {
        return validation;
      }
      const std::uint64_t group_bytes =
          static_cast<std::uint64_t>(declaration.material_key
                                         .exact_resource_group.size());
      const std::uint64_t name_bytes = static_cast<std::uint64_t>(
          declaration.material_key.exact_name.size());
      if (group_bytes >
          (std::numeric_limits<std::uint64_t>::max)() - name_bytes) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "semantic_registry.key_bytes",
                       "semantic declaration key byte count overflowed");
      }
      const std::uint64_t declaration_key_bytes = group_bytes + name_bytes;
      if (total_key_bytes >
              (std::numeric_limits<std::uint64_t>::max)() -
                  declaration_key_bytes ||
          total_key_bytes + declaration_key_bytes >
              configuration.maximum_total_key_bytes) {
        return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                       "semantic_registry.key_bytes",
                       "semantic declaration keys exceed the byte cap");
      }
      total_key_bytes += declaration_key_bytes;
      SemanticRecord record;
      record.source = declaration.source;
      record.source_revision = declaration.source_revision;
      record.base_color_semantic = declaration.base_color_semantic;
      record.texture_color_role = declaration.texture_color_role;
      const auto inserted = candidate->declarations.emplace(
          declaration.material_key, record);
      if (!inserted.second) {
        return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                       "semantic_registry.material_key",
                       "one exact material key has multiple declarations");
      }
      if (!injected_after_first && fault_injector != nullptr) {
        injected_after_first = true;
        fault_injector->BeforeRegistryBuildStage(
            Ogre14LegacyMaterialSemanticRegistryBuildStage::
                AFTER_FIRST_DECLARATION);
      }
    }

    std::uint64_t fingerprint = kFnvOffset64;
    static constexpr char kDomain[] =
        "RoR.OGRE14.LegacyMaterialSemanticRegistry.v1";
    for (std::size_t index = 0U; index + 1U < sizeof(kDomain); ++index) {
      HashByte(fingerprint, static_cast<std::uint8_t>(kDomain[index]));
    }
    for (const auto &entry : candidate->declarations) {
      HashString(fingerprint, entry.first.exact_resource_group);
      HashString(fingerprint, entry.first.exact_name);
      HashByte(fingerprint, static_cast<std::uint8_t>(entry.second.source));
      HashU64(fingerprint, entry.second.source_revision);
      HashByte(fingerprint,
               static_cast<std::uint8_t>(entry.second.base_color_semantic));
      HashByte(fingerprint,
               static_cast<std::uint8_t>(entry.second.texture_color_role));
    }
    candidate->content_fingerprint = fingerprint == 0U ? 1U : fingerprint;

    if (fault_injector != nullptr) {
      fault_injector->BeforeRegistryBuildStage(
          Ogre14LegacyMaterialSemanticRegistryBuildStage::BEFORE_COMMIT);
    }
    output = Ogre14LegacyMaterialSemanticRegistry(std::move(candidate));
    return ValidationResult::Success();
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_registry.allocation",
                   "allocation failed before semantic registry commit");
  } catch (const std::length_error &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_registry.allocation",
                   "semantic registry allocation exceeded implementation limits");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_registry.exception",
                   "unexpected exception before semantic registry commit");
  }
}

} // namespace RoR::Render
