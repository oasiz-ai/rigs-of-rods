/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14LegacyLiveMaterialCoordinator.h"
#include "resources/ContentManager.h"

#include <OgreMaterial.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index =
                             (std::numeric_limits<std::size_t>::max)()) {
  return ValidationResult::Failure(code, field, detail, index);
}

} // namespace

template <typename LiveAuthority>
ValidationResult Ogre14LegacyMaterialSemanticRuntimeAuthority::
    CaptureAndAdmitWithLiveAuthority(
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    Ogre::Material &material,
    LiveAuthority &live_authority,
    Ogre14LegacyMaterialSemanticAdmission &output,
    IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector
        *fault_injector
    ) const {
  try {
    if (!initialized()) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "semantic_runtime.live.authority",
                     "semantic runtime authority is not initialized");
    }
    const Ogre14LegacyAssetKey material_key{material.getGroup(),
                                            material.getName()};

    Ogre14AuthenticatedMaterialScriptResolution script_resolution;
    ValidationResult validation =
        live_authority.ResolveAuthenticatedMaterialScript(
            material, script_resolution);
    if (!validation) {
      return validation;
    }
    if (!script_resolution.MatchesResolver(live_authority)) {
      return Failure(
          ValidationCode::REVISION_MISMATCH,
          "semantic_runtime.live.script_resolver",
          "script resolution was forwarded from a foreign resolver authority");
    }

    Ogre14LegacyMaterialSemanticResolution semantic_resolution;
    validation = ResolveMaterialSemantics(
        material_key, translator_configuration, semantic_resolution);
    if (!validation) {
      return validation;
    }
    validation = ValidateScriptAndSemanticPrerequisites(
        script_resolution, semantic_resolution, material_key, fault_injector);
    if (!validation) {
      return validation;
    }

    Ogre14LegacyNativeMaterialCapture native_capture;
    validation = CaptureOgre14LegacyNativeMaterial(
        material, semantic_resolution.native_declaration, live_authority,
        native_capture);
    if (!validation) {
      return validation;
    }
    const auto *receipt = script_resolution.receipt();
    const auto *source = receipt != nullptr ? receipt->source_metadata() : nullptr;
    if (source == nullptr || source->group_generation == 0U) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "semantic_runtime.live.runtime_group_generation",
                     "resolved script has no current runtime generation");
    }
    validation = ValidateNativeCapture(
        material_key, source->group_generation, native_capture,
        fault_injector);
    if (!validation) {
      return validation;
    }

    Ogre14AuthenticatedTextureAuthoritySnapshot texture_authority;
    validation = live_authority.CaptureAuthenticatedTextureAuthoritySnapshot(
        texture_authority);
    if (!validation) {
      return validation;
    }
    for (const auto &texture_resolution :
         native_capture.authenticated_texture_resolutions) {
      if (!texture_authority.Authenticates(texture_resolution)) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "semantic_runtime.live.texture_authority",
                       "native capture carries a stale or foreign texture authority");
      }
    }

    // The live resolver owns this no-throw final check. It observes the exact
    // native Material pointer and manager indices after extractor readback.
    if (!live_authority.RevalidateAuthenticatedMaterialScript(
            material, script_resolution)) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "semantic_runtime.live.final_script_revalidation",
                     "material or script registry changed during native capture");
    }
    Ogre14AuthenticatedMaterialScriptAuthoritySnapshot script_authority;
    validation =
        live_authority.CaptureAuthenticatedMaterialScriptAuthoritySnapshot(
            script_authority);
    if (!validation) {
      return validation;
    }
    return PublishAdmission(
        script_resolution, script_authority, semantic_resolution,
        std::move(native_capture), output, fault_injector);
  } catch (const std::bad_alloc &) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   "semantic_runtime.live.allocation",
                   "allocation failed before live admission publication");
  } catch (...) {
    return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                   "semantic_runtime.live.exception",
                   "unexpected exception before live admission publication");
  }
}

ValidationResult CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(
    const Ogre14LegacyMaterialSemanticRuntimeAuthority &runtime_authority,
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    Ogre::Material &material,
    ::RoR::ContentManager &content_manager,
    Ogre14LegacyMaterialSemanticAdmission &output
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
    ,
    IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *fault_injector
#endif
    ) {
#if !defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector *fault_injector =
      nullptr;
#endif
  return runtime_authority.CaptureAndAdmitWithLiveAuthority(
      translator_configuration, material, content_manager, output,
      fault_injector);
}

#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
ValidationResult CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(
    const Ogre14LegacyMaterialSemanticRuntimeAuthority &runtime_authority,
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    Ogre::Material &material,
    IOgre14LegacyMaterialRuntimeLiveAuthority &live_authority,
    Ogre14LegacyMaterialSemanticAdmission &output,
    IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector
        *fault_injector) {
  return runtime_authority.CaptureAndAdmitWithLiveAuthority(
      translator_configuration, material, live_authority, output,
      fault_injector);
}
#endif

ValidationResult CreateOgre14LegacyAuthenticatedMaterialCoordinator(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration,
    const Ogre14LegacyMaterialSemanticRuntimeAuthority &runtime_authority,
    ::RoR::ContentManager &content_manager,
    std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &output
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
    , IOgre14LegacyAssetTranslatorFaultInjector *translator_fault_injector
#endif
    ) try {
#if !defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  IOgre14LegacyAssetTranslatorFaultInjector *translator_fault_injector =
      nullptr;
#endif
  ValidationResult validation =
      ValidateOgre14LegacyLiveMaterialCoordinatorConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  const Ogre14LegacyMaterialSemanticRegistry *exact_registry =
      runtime_authority.semantic_registry();
  if (!runtime_authority.initialized() || exact_registry == nullptr ||
      !exact_registry->initialized()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_coordinator.semantic_runtime_authority",
                   "authenticated factory requires an initialized exact runtime authority");
  }
  auto translator = std::make_unique<Ogre14LegacyAssetTranslator>(
      configuration.translator, configuration.transaction,
      translator_fault_injector);
  auto candidate = std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator>(
      new Ogre14LegacyLiveMaterialCoordinator(
          configuration, *exact_registry, std::move(translator),
          static_cast<IOgre14AuthenticatedTextureAuthorityProvider *>(
              &content_manager),
          runtime_authority, &content_manager
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
          , nullptr
#endif
          ));
  if (!candidate->semantic_registry_.SharesImmutableStateWith(
          *exact_registry) ||
      !candidate->semantic_runtime_authority_.SharesImmutableStateWith(
          runtime_authority)) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "material_coordinator.semantic_runtime_authority",
                   "authenticated factory detached the approved catalog registry owner");
  }
  output = std::move(candidate);
  return ValidationResult::Success();
} catch (const std::bad_alloc &) {
  return Failure(ValidationCode::EMPTY_PAYLOAD,
                 "material_coordinator.authenticated_allocation",
                 "allocation failed before authenticated coordinator publication");
} catch (...) {
  return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                 "material_coordinator.authenticated_exception",
                 "unexpected exception before authenticated coordinator publication");
}

ValidationResult Ogre14LegacyLiveMaterialCoordinator::
    PrepareFreshContentManagerAdmissionsImpl(
        std::uint64_t source_sequence,
        const std::vector<Ogre14LegacyMaterialSemanticAdmission> &admissions,
        Ogre14LegacyAdmittedPreparedMaterialFrame &output,
        IOgre14LegacyLiveMaterialCoordinatorFaultInjector *fault_injector) try {
  const PrepareStartStatus start =
      CheckPrepareStart(source_sequence, admissions.size());
  if (start != PrepareStartStatus::READY) {
    return PrepareStartFailure(start);
  }
  if (!semantic_runtime_authority_.initialized() ||
      content_manager_ == nullptr ||
      texture_authority_provider_ !=
          static_cast<IOgre14AuthenticatedTextureAuthorityProvider *>(
              content_manager_)) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_coordinator.authenticated_authority",
                   "coordinator was not created by the concrete ContentManager factory");
  }
  auto capability =
      std::make_shared<Ogre14LegacyAdmittedPreparedMaterialFrame::State>();
  capability->runtime_authority = semantic_runtime_authority_;
  capability->admissions = admissions;

  Ogre14AuthenticatedMaterialScriptAuthoritySnapshot initial_script;
  ValidationResult validation =
      content_manager_->CaptureAuthenticatedMaterialScriptAuthoritySnapshot(
          initial_script);
  if (!validation) {
    return validation;
  }
  Ogre14AuthenticatedTextureAuthoritySnapshot initial_texture;
  validation =
      content_manager_->CaptureAuthenticatedTextureAuthoritySnapshot(
          initial_texture);
  if (!validation) {
    return validation;
  }

  std::vector<ObservationView> observations;
  observations.reserve(admissions.size());
  for (std::size_t index = 0U; index < admissions.size(); ++index) {
    validation = semantic_runtime_authority_.RevalidateAdmission(
        admissions[index], initial_script, initial_texture);
    const auto *key = admissions[index].material_key();
    const auto *semantics = admissions[index].semantic_resolution();
    const auto *capture = admissions[index].native_capture();
    if (!validation || key == nullptr || semantics == nullptr ||
        capture == nullptr) {
      if (validation) {
        validation = Failure(ValidationCode::MISSING_REFERENCE,
                             "material_admissions.state",
                             "opaque material admission is incomplete", index);
      } else {
        validation.element_index = index;
      }
      return validation;
    }
    for (const auto &texture_resolution :
         capture->authenticated_texture_resolutions) {
      if (!initial_texture.Authenticates(texture_resolution)) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "material_admissions.texture_authority",
                       "admission texture authority is stale or foreign",
                       index);
      }
    }
    observations.push_back(ObservationView{
        kOgre14LegacyMaterialObservationVersion, *key, *semantics, *capture});
  }

  Ogre14LegacyPreparedMaterialFrame prepared;
  validation = PrepareFrameViewsImpl(source_sequence, observations, prepared,
                                     fault_injector);
  if (!validation) {
    return validation;
  }
  try {
    if (fault_injector != nullptr) {
      fault_injector->AtFaultPoint(
          Ogre14LegacyLiveMaterialCoordinatorFaultPoint::
              AFTER_ADMITTED_INNER_PREPARE);
    }

    Ogre14AuthenticatedMaterialScriptAuthoritySnapshot final_script;
    validation =
        content_manager_->CaptureAuthenticatedMaterialScriptAuthoritySnapshot(
            final_script);
    if (!validation) {
      pending_.reset();
      return validation;
    }
    Ogre14AuthenticatedTextureAuthoritySnapshot final_texture;
    validation =
        content_manager_->CaptureAuthenticatedTextureAuthoritySnapshot(
            final_texture);
    if (!validation) {
      pending_.reset();
      return validation;
    }
    for (std::size_t index = 0U; index < admissions.size(); ++index) {
      validation = semantic_runtime_authority_.RevalidateAdmission(
          admissions[index], final_script, final_texture);
      const auto *capture = admissions[index].native_capture();
      if (!validation || capture == nullptr) {
        pending_.reset();
        if (!validation) {
          validation.element_index = index;
          return validation;
        }
        return Failure(ValidationCode::MISSING_REFERENCE,
                       "material_admissions.state",
                       "admission lost its native capture before publication",
                       index);
      }
      for (const auto &texture_resolution :
           capture->authenticated_texture_resolutions) {
        if (!final_texture.Authenticates(texture_resolution)) {
          pending_.reset();
          return Failure(ValidationCode::REVISION_MISMATCH,
                         "material_admissions.final_texture_authority",
                         "texture registry changed before frame publication",
                         index);
        }
      }
    }

    capability->script_authority = std::move(final_script);
    capability->texture_authority = std::move(final_texture);
    capability->prepared = prepared;
    Ogre14LegacyAdmittedPreparedMaterialFrame admitted(
        std::shared_ptr<const Ogre14LegacyAdmittedPreparedMaterialFrame::State>(
            std::move(capability)));
    pending_->admitted = admitted;
    output = std::move(admitted);
    return ValidationResult::Success();
  } catch (...) {
    pending_.reset();
    throw;
  }
} catch (const std::bad_alloc &) {
  pending_.reset();
  return Failure(ValidationCode::EMPTY_PAYLOAD,
                 "material_coordinator.admitted_allocation",
                 "allocation failed before admitted frame publication");
} catch (...) {
  pending_.reset();
  return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                 "material_coordinator.admitted_exception",
                 "unexpected exception before admitted frame publication");
}

ValidationResult Ogre14LegacyLiveMaterialCoordinator::PrepareAdmittedFrame(
    std::uint64_t source_sequence,
    const std::vector<Ogre::Material *> &live_materials,
    Ogre14LegacyAdmittedPreparedMaterialFrame &output
#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
    , IOgre14LegacyLiveMaterialCoordinatorFaultInjector *fault_injector
#endif
    ) try {
#if !defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)
  IOgre14LegacyLiveMaterialCoordinatorFaultInjector *fault_injector = nullptr;
#endif
  const PrepareStartStatus start =
      CheckPrepareStart(source_sequence, live_materials.size());
  if (start != PrepareStartStatus::READY) {
    return PrepareStartFailure(start);
  }
  // This is a no-allocation lower-bound gate: every live material necessarily
  // produces one material asset, before textures or material-owned samplers
  // are considered. Reject an impossible frame before pointer indexing or any
  // extractor readback.
  if (live_materials.size() >
      configuration_.translator.maximum_live_assets_per_frame) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "live_materials.live_asset_count",
        "live material count alone exceeds the configured live-asset cap");
  }
  if (live_materials.size() >
      configuration_.translator.maximum_material_inputs_per_frame) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "live_materials.material_count",
        "live material count exceeds the configured material-input cap");
  }
  const Ogre14LegacyMaterialSemanticRegistry *const exact_registry =
      semantic_runtime_authority_.semantic_registry();
  if (!semantic_runtime_authority_.initialized() ||
      exact_registry == nullptr || !exact_registry->initialized() ||
      translator_ == nullptr || !semantic_registry_.initialized() ||
      !semantic_registry_.SharesImmutableStateWith(*exact_registry) ||
      content_manager_ == nullptr ||
      texture_authority_provider_ !=
          static_cast<IOgre14AuthenticatedTextureAuthorityProvider *>(
              content_manager_)) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_coordinator.authenticated_authority",
                   "coordinator was not created by the authenticated factory");
  }
  for (Ogre::Material *material : live_materials) {
    if (material == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "live_materials.material",
                     "authenticated preparation received a null live material");
    }
  }
  std::vector<Ogre::Material *> unique_identities = live_materials;
  std::sort(unique_identities.begin(), unique_identities.end(),
            std::less<Ogre::Material *>{});
  if (std::adjacent_find(unique_identities.begin(), unique_identities.end()) !=
      unique_identities.end()) {
    return Failure(
        ValidationCode::DUPLICATE_IDENTIFIER, "live_materials.pointer",
        "authenticated preparation requires unique live material pointers");
  }
  std::sort(unique_identities.begin(), unique_identities.end(),
            [](const Ogre::Material *lhs, const Ogre::Material *rhs) {
              return lhs->getGroup() != rhs->getGroup()
                         ? lhs->getGroup() < rhs->getGroup()
                         : lhs->getName() < rhs->getName();
            });
  for (std::size_t index = 1U; index < unique_identities.size(); ++index) {
    if (unique_identities[index - 1U]->getGroup() ==
            unique_identities[index]->getGroup() &&
        unique_identities[index - 1U]->getName() ==
            unique_identities[index]->getName()) {
      return Failure(
          ValidationCode::DUPLICATE_IDENTIFIER, "live_materials.key",
          "authenticated preparation requires unique exact material keys");
    }
  }

  // This entire call executes on the caller's serialized OGRE
  // resource/render thread. No caller-supplied or previously minted admission
  // is accepted: the coordinator's exact combined live authority is used at
  // every resolver/provider edge and the resulting captures flow immediately
  // into the inner transactional preparation.
  std::vector<Ogre14LegacyMaterialSemanticAdmission> fresh_admissions;
  fresh_admissions.reserve(live_materials.size());
  std::uint64_t admitted_texture_bytes = 0U;
  std::size_t admitted_sampler_count = 0U;
  std::map<std::string, Ogre14AuthenticatedTextureResolution, std::less<>>
      unique_texture_resolutions;
  for (std::size_t index = 0U; index < live_materials.size(); ++index) {
    Ogre::Material *const material = live_materials[index];
    Ogre14LegacyMaterialSemanticAdmission admission;
    ValidationResult validation =
        CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(
            semantic_runtime_authority_, configuration_.translator, *material,
            *content_manager_, admission);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    const Ogre14LegacyNativeMaterialCapture *const capture =
        admission.native_capture();
    if (capture == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "live_materials.admission_capture",
                     "fresh admission omitted its native capture");
    }
    if (capture->textures.size() !=
        capture->authenticated_texture_resolutions.size()) {
      return Failure(
          ValidationCode::SIZE_MISMATCH,
          "live_materials.texture_resolution_count",
          "fresh admission texture inputs and authenticated resolutions differ");
    }
    for (const auto &texture : capture->textures) {
      for (const auto &mip : texture.mip_levels) {
        const std::uint64_t mip_bytes =
            static_cast<std::uint64_t>(mip.bytes.size());
        const std::uint64_t frame_byte_cap =
            configuration_.translator.maximum_decoded_bytes_per_frame;
        if (mip_bytes > frame_byte_cap ||
            admitted_texture_bytes > frame_byte_cap - mip_bytes) {
          return Failure(
              ValidationCode::VALUE_OUT_OF_RANGE,
              "live_materials.texture_bytes",
              "fresh native texture bytes exceed the configured frame cap");
        }
        admitted_texture_bytes += mip_bytes;
      }
    }
    const std::size_t new_sampler_count =
        capture->material.texture_units.size();
    const std::size_t live_asset_cap =
        configuration_.translator.maximum_live_assets_per_frame;
    if (new_sampler_count > live_asset_cap - admitted_sampler_count) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "live_materials.sampler_count",
                     "fresh material sampler count exceeds the configured live-asset cap");
    }
    admitted_sampler_count += new_sampler_count;
    for (std::size_t texture_index = 0U;
         texture_index < capture->textures.size(); ++texture_index) {
      const Ogre14LegacyTextureInput &texture =
          capture->textures[texture_index];
      const Ogre14AuthenticatedTextureResolution &resolution =
          capture->authenticated_texture_resolutions[texture_index];
      std::string stable_texture_key;
      validation = BuildOgre14LegacyStableAssetKey(
          RenderAssetKind::TEXTURE, texture.key, stable_texture_key);
      if (!validation) {
        validation.element_index = index;
        return validation;
      }
      const auto existing =
          unique_texture_resolutions.find(stable_texture_key);
      if (existing != unique_texture_resolutions.end()) {
        if (!existing->second.SharesLoadedResourceAuthorityWith(resolution)) {
          return Failure(
              ValidationCode::REVISION_MISMATCH,
              "live_materials.shared_texture_authority",
              "one exact texture key resolved through conflicting live authorities");
        }
        continue;
      }
      if (unique_texture_resolutions.size() >=
          configuration_.translator.maximum_texture_inputs_per_frame) {
        return Failure(
            ValidationCode::VALUE_OUT_OF_RANGE,
            "live_materials.texture_count",
            "unique fresh texture count exceeds the configured texture-input cap");
      }
      unique_texture_resolutions.emplace(std::move(stable_texture_key),
                                         resolution);
    }
    // Materials are unique by the pre-capture pointer/key sort. Textures are
    // charged once per stable key; samplers remain material-owned and are
    // charged once per texture unit, matching the inner authoritative count.
    // Every input pointer already proved unique before capture, so all known
    // material assets are charged now. This lets the first textured capture
    // reject a frame whose remaining materials alone consume the residual cap
    // instead of performing one redundant readback per not-yet-captured input.
    const std::size_t current_material_count = live_materials.size();
    if (admitted_sampler_count > live_asset_cap ||
        unique_texture_resolutions.size() >
            live_asset_cap - admitted_sampler_count ||
        current_material_count >
            live_asset_cap - admitted_sampler_count -
                unique_texture_resolutions.size()) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "live_materials.live_asset_count",
          "fresh unique textures, samplers, and materials exceed the configured live-asset cap");
    }
    fresh_admissions.push_back(std::move(admission));
  }
  return PrepareFreshContentManagerAdmissionsImpl(
      source_sequence, fresh_admissions, output, fault_injector);
} catch (const std::bad_alloc &) {
  return Failure(ValidationCode::EMPTY_PAYLOAD,
                 "material_coordinator.live_admitted_allocation",
                 "allocation failed before live authenticated preparation");
} catch (...) {
  return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                 "material_coordinator.live_admitted_exception",
                 "unexpected exception before live authenticated preparation");
}

} // namespace RoR::Render
