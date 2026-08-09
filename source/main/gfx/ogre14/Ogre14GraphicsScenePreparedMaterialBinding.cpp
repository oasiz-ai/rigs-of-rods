/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14GraphicsScenePreparedMaterialBinding.h"

#include <cstddef>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult
Failure(ValidationCode code, const char *field, const char *detail,
        std::size_t index = (std::numeric_limits<std::size_t>::max)()) {
  return ValidationResult::Failure(code, field, detail, index);
}

using AssetKeyView = std::pair<std::string_view, std::string_view>;

AssetKeyView MaterialKeyView(const Ogre14LegacyAssetKey &key) noexcept {
  return {key.exact_resource_group, key.exact_name};
}

AssetKeyView MaterialKeyView(
    const Ogre14GraphicsSceneMaterialCaptureInput &material) noexcept {
  return {material.exact_resource_group, material.exact_name};
}

bool SharesControlBlock(
    const std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit> &lhs,
    const std::shared_ptr<const Ogre14LegacyMaterialPipelineAudit>
        &rhs) noexcept {
  return lhs != nullptr && rhs != nullptr && !lhs.owner_before(rhs) &&
         !rhs.owner_before(lhs);
}

using PreparedMaterialIndex =
    std::map<AssetKeyView, const Ogre14LegacyPreparedMaterial *>;

ValidationResult
BuildPreparedMaterialIndex(const Ogre14LegacyPreparedMaterialFrame &frame,
                           PreparedMaterialIndex &material_index) {
  const Ogre14LegacyTranslatedFrame *translated = frame.translated_frame();
  if (!frame.initialized() ||
      frame.version() != kOgre14LegacyPreparedMaterialFrameVersion ||
      translated == nullptr || translated->source_sequence == 0U ||
      translated->catalog_sequence == 0U) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "prepared_material_binding.frame",
                   "prepared material frame is absent or malformed");
  }
  const std::vector<Ogre14LegacyPreparedMaterial> &materials =
      frame.materials();
  for (std::size_t index = 0U; index < materials.size(); ++index) {
    const Ogre14LegacyPreparedMaterial &material = materials[index];
    if (material.native_material_audit == nullptr ||
        material.closure == nullptr ||
        material.closure->material_audit == nullptr ||
        SharesControlBlock(material.native_material_audit,
                           material.closure->material_audit) ||
        !ValidateOgre14LegacyMaterialPipelineAudit(
             *material.native_material_audit)
             .ok() ||
        !EquivalentOgre14LegacyMaterialPipelineAudit(
            *material.native_material_audit,
            *material.closure->material_audit) ||
        !ValidateOgre14LegacyMaterialClosureForFrame(
             *translated, *material.closure, material.material_key)
             .ok()) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "prepared_material_binding.frame.material",
                     "prepared material owner or audit is not authentic",
                     index);
    }
    if (!material_index
             .emplace(MaterialKeyView(material.material_key), &material)
             .second) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "prepared_material_binding.frame.material_key",
                     "prepared frame repeats one exact material key", index);
    }
  }
  return ValidationResult::Success();
}

const Ogre14LegacyPreparedMaterial *
FindPreparedMaterial(const PreparedMaterialIndex &material_index,
                     const Ogre14GraphicsSceneMaterialCaptureInput &material) {
  const auto found = material_index.find(MaterialKeyView(material));
  return found != material_index.end() ? found->second : nullptr;
}

ValidationResult
ValidateFallback(const Ogre14GraphicsSceneMaterialCaptureInput &material,
                 std::size_t index, const char *field) {
  MaterialDescriptor fallback;
  const ValidationResult validation =
      BuildOgre14GraphicsSceneMaterialFallback(material, fallback);
  if (!validation) {
    return Failure(
        validation.code, field,
        "section has no prepared exact material and is not an eligible "
        "factor-only fallback",
        index);
  }
  return ValidationResult::Success();
}

} // namespace

struct Ogre14GraphicsScenePreparedMaterialBinding::State final {
  Ogre14LegacyPreparedMaterialFrame prepared_frame;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> static_sections;
  std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> dynamic_sections;
  Ogre14GraphicsSceneResolvedMaterialFrameLineage lineage;
};

Ogre14GraphicsScenePreparedMaterialBinding::
    Ogre14GraphicsScenePreparedMaterialBinding(
        std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14GraphicsScenePreparedMaterialBinding::initialized() const noexcept {
  return state_ != nullptr;
}

std::uint32_t
Ogre14GraphicsScenePreparedMaterialBinding::version() const noexcept {
  return state_ != nullptr ? kOgre14GraphicsScenePreparedMaterialBindingVersion
                           : 0U;
}

const Ogre14LegacyPreparedMaterialFrame *
Ogre14GraphicsScenePreparedMaterialBinding::prepared_frame() const noexcept {
  return state_ != nullptr ? &state_->prepared_frame : nullptr;
}

const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &
Ogre14GraphicsScenePreparedMaterialBinding::static_sections() const noexcept {
  static const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> empty;
  return state_ != nullptr ? state_->static_sections : empty;
}

const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> &
Ogre14GraphicsScenePreparedMaterialBinding::dynamic_sections() const noexcept {
  static const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> empty;
  return state_ != nullptr ? state_->dynamic_sections : empty;
}

const Ogre14GraphicsSceneResolvedMaterialFrameLineage *
Ogre14GraphicsScenePreparedMaterialBinding::lineage() const noexcept {
  return state_ != nullptr ? &state_->lineage : nullptr;
}

bool Ogre14GraphicsScenePreparedMaterialBinding::SharesImmutableStateWith(
    const Ogre14GraphicsScenePreparedMaterialBinding &other) const noexcept {
  return state_ != nullptr && state_.get() == other.state_.get() &&
         !state_.owner_before(other.state_) &&
         !other.state_.owner_before(state_);
}

ValidationResult BindOgre14GraphicsScenePreparedMaterials(
    const Ogre14LegacyPreparedMaterialFrame &prepared_frame,
    const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput>
        &static_sections,
    const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput>
        &dynamic_sections,
    Ogre14GraphicsScenePreparedMaterialBinding &output,
    IOgre14GraphicsScenePreparedMaterialBindingFaultInjector
        *fault_injector) try {
  if (static_sections.size() > kMaximumOgre14GraphicsSceneStaticSections ||
      dynamic_sections.size() > kMaximumOgre14GraphicsSceneDynamicSections) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "prepared_material_binding.sections",
        "prepared material binding preflight exceeds fixed section caps");
  }
  PreparedMaterialIndex material_index;
  const ValidationResult frame_validation =
      BuildPreparedMaterialIndex(prepared_frame, material_index);
  if (!frame_validation) {
    return frame_validation;
  }
  auto candidate =
      std::make_shared<Ogre14GraphicsScenePreparedMaterialBinding::State>();
  candidate->prepared_frame = prepared_frame;
  candidate->static_sections.reserve(static_sections.size());
  candidate->dynamic_sections.reserve(dynamic_sections.size());
  bool injected_after_first = false;

  for (std::size_t index = 0U; index < static_sections.size(); ++index) {
    const Ogre14GraphicsSceneStaticSectionCaptureInput &input =
        static_sections[index];
    if (input.resolved_material != nullptr) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "prepared_material_binding.static.resolved_material",
                     "caller supplied a closure before authenticated binding",
                     index);
    }
    const Ogre14LegacyPreparedMaterial *material =
        FindPreparedMaterial(material_index, input.material);
    if (material == nullptr) {
      const ValidationResult fallback = ValidateFallback(
          input.material, index, "prepared_material_binding.static.fallback");
      if (!fallback) {
        return fallback;
      }
      candidate->static_sections.push_back(input);
      continue;
    }
    if (input.mesh_identity.reverse_winding !=
        material->closure->requires_reverse_winding) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "prepared_material_binding.static.winding",
                     "static mesh conversion does not match exact native cull",
                     index);
    }
    Ogre14GraphicsSceneStaticSectionCaptureInput section = input;
    section.resolved_material = material->closure;
    candidate->static_sections.push_back(std::move(section));
    if (!injected_after_first && fault_injector != nullptr) {
      injected_after_first = true;
      fault_injector->AtFaultPoint(
          Ogre14GraphicsScenePreparedMaterialBindingFaultPoint::
              AFTER_FIRST_EXACT_BINDING);
    }
  }

  for (std::size_t index = 0U; index < dynamic_sections.size(); ++index) {
    const Ogre14GraphicsSceneDynamicSectionCaptureInput &input =
        dynamic_sections[index];
    if (input.resolved_material != nullptr) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "prepared_material_binding.dynamic.resolved_material",
                     "caller supplied a closure before authenticated binding",
                     index);
    }
    const Ogre14LegacyPreparedMaterial *material =
        FindPreparedMaterial(material_index, input.material);
    if (material == nullptr) {
      const ValidationResult fallback = ValidateFallback(
          input.material, index, "prepared_material_binding.dynamic.fallback");
      if (!fallback) {
        return fallback;
      }
      candidate->dynamic_sections.push_back(input);
      continue;
    }
    if (input.mesh_reverse_winding !=
        material->closure->requires_reverse_winding) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "prepared_material_binding.dynamic.winding",
                     "dynamic mesh conversion does not match exact native cull",
                     index);
    }
    Ogre14GraphicsSceneDynamicSectionCaptureInput section = input;
    section.resolved_material = material->closure;
    candidate->dynamic_sections.push_back(std::move(section));
    if (!injected_after_first && fault_injector != nullptr) {
      injected_after_first = true;
      fault_injector->AtFaultPoint(
          Ogre14GraphicsScenePreparedMaterialBindingFaultPoint::
              AFTER_FIRST_EXACT_BINDING);
    }
  }

  const ValidationResult lineage_validation =
      ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage(
          candidate->static_sections, candidate->dynamic_sections,
          candidate->lineage);
  if (!lineage_validation) {
    return lineage_validation;
  }
  if (fault_injector != nullptr) {
    fault_injector->AtFaultPoint(
        Ogre14GraphicsScenePreparedMaterialBindingFaultPoint::
            BEFORE_BINDING_COMMIT);
  }
  output = Ogre14GraphicsScenePreparedMaterialBinding(std::move(candidate));
  return ValidationResult::Success();
} catch (const std::bad_alloc &) {
  return Failure(ValidationCode::EMPTY_PAYLOAD,
                 "prepared_material_binding.allocation",
                 "allocation failed before material binding commit");
} catch (const std::length_error &) {
  return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                 "prepared_material_binding.allocation",
                 "material binding exceeded implementation limits");
} catch (...) {
  return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                 "prepared_material_binding.exception",
                 "unexpected exception before material binding commit");
}

static_assert(std::is_nothrow_copy_assignable<
                  Ogre14GraphicsScenePreparedMaterialBinding>::value,
              "retaining an accepted binding must not throw");
static_assert(std::is_nothrow_move_assignable<
                  Ogre14GraphicsScenePreparedMaterialBinding>::value,
              "publishing a material binding must not throw");
static_assert(
    std::is_nothrow_default_constructible<
        std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput>>::value,
    "empty static binding view must not throw");
static_assert(
    std::is_nothrow_default_constructible<
        std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput>>::value,
    "empty dynamic binding view must not throw");

} // namespace RoR::Render
