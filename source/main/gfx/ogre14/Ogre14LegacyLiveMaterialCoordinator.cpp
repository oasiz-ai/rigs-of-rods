/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "Ogre14LegacyLiveMaterialCoordinator.h"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace RoR::Render {
namespace {

ValidationResult
Failure(ValidationCode code, const char *field, const char *detail,
        std::size_t index = (std::numeric_limits<std::size_t>::max)()) {
  return ValidationResult::Failure(code, field, detail, index);
}

bool CheckedAdd(std::uint64_t lhs, std::uint64_t rhs,
                std::uint64_t &output) noexcept {
  if (rhs > (std::numeric_limits<std::uint64_t>::max)() - lhs) {
    return false;
  }
  output = lhs + rhs;
  return true;
}

bool CheckedMultiply(std::uint64_t lhs, std::uint64_t rhs,
                     std::uint64_t &output) noexcept {
  if (lhs != 0U && rhs > (std::numeric_limits<std::uint64_t>::max)() / lhs) {
    return false;
  }
  output = lhs * rhs;
  return true;
}

bool SameMip(const Ogre14LegacyTextureMipInput &lhs,
             const Ogre14LegacyTextureMipInput &rhs) noexcept {
  return lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.row_pitch_bytes == rhs.row_pitch_bytes &&
         lhs.slice_pitch_bytes == rhs.slice_pitch_bytes &&
         lhs.bytes == rhs.bytes;
}

bool SameTexture(const Ogre14LegacyTextureInput &lhs,
                 const Ogre14LegacyTextureInput &rhs) noexcept {
  if (lhs.version != rhs.version || lhs.key != rhs.key ||
      lhs.source_revision != rhs.source_revision || lhs.type != rhs.type ||
      lhs.pixel_encoding != rhs.pixel_encoding ||
      lhs.color_role != rhs.color_role ||
      lhs.hardware_gamma_enabled != rhs.hardware_gamma_enabled ||
      lhs.compressed != rhs.compressed ||
      lhs.render_target != rhs.render_target ||
      lhs.generated != rhs.generated || lhs.procedural != rhs.procedural ||
      lhs.width != rhs.width || lhs.height != rhs.height ||
      lhs.mip_levels.size() != rhs.mip_levels.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < lhs.mip_levels.size(); ++index) {
    if (!SameMip(lhs.mip_levels[index], rhs.mip_levels[index])) {
      return false;
    }
  }
  return true;
}

ValidationResult ValidateObservation(
    const Ogre14LegacyMaterialObservation &observation,
    const Ogre14LegacyMaterialSemanticRegistry &semantic_registry,
    const Ogre14LegacyAssetTranslatorConfiguration &translator_configuration,
    std::uint64_t &observed_texture_bytes) {
  if (observation.version != kOgre14LegacyMaterialObservationVersion ||
      observation.native_capture.version !=
          kOgre14LegacyNativeAssetExtractorVersion) {
    return Failure(
        ValidationCode::UNSUPPORTED_VERSION, "material_observations.version",
        "unsupported material observation or native capture version");
  }
  if (observation.material_key != observation.native_capture.material.key) {
    return Failure(
        ValidationCode::REVISION_MISMATCH, "material_observations.material_key",
        "observation and native capture identify different materials");
  }
  Ogre14LegacyMaterialSemanticResolution resolution;
  ValidationResult validation = semantic_registry.Resolve(
      observation.material_key, translator_configuration, resolution);
  if (!validation) {
    return validation;
  }
  if (!Ogre14LegacyMaterialSemanticResolutionMatchesKey(
          resolution, observation.material_key) ||
      !Ogre14LegacyMaterialSemanticResolutionMatchesKey(
          observation.semantic_resolution, observation.material_key) ||
      !Ogre14LegacyMaterialSemanticResolutionAuthenticates(
          observation.semantic_resolution, resolution) ||
      observation.native_capture.material.base_color_semantic !=
          resolution.native_declaration.base_color_semantic) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "material_observations.semantic_resolution",
                   "native material capture disagrees with its exact semantic "
                   "declaration");
  }
  validation =
      ValidateOgre14LegacyMaterialInput(observation.native_capture.material);
  if (!validation) {
    return validation;
  }
  if (observation.native_capture.material.texture_units.size() !=
          observation.native_capture.textures.size() ||
      observation.native_capture.textures.size() > 1U) {
    return Failure(
        ValidationCode::SIZE_MISMATCH, "material_observations.textures",
        "native capture must carry exactly its referenced v1 textures");
  }
  std::uint64_t candidate_observed_texture_bytes = 0U;
  for (std::size_t index = 0U;
       index < observation.native_capture.textures.size(); ++index) {
    const Ogre14LegacyTextureInput &texture =
        observation.native_capture.textures[index];
    validation = ValidateOgre14LegacyTextureInput(texture);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    if (texture.type != Ogre14LegacyTextureType::TEXTURE_2D ||
        texture.pixel_encoding != Ogre14LegacyPixelEncoding::RGBA8_BYTES ||
        texture.compressed || texture.render_target || texture.generated ||
        texture.procedural) {
      return Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "material_observations.texture_payload",
          "live native texture capture must be canonical RGBA8 2D data", index);
    }
    std::uint64_t texture_bytes = 0U;
    for (const Ogre14LegacyTextureMipInput &mip : texture.mip_levels) {
      std::uint64_t tight_row_bytes = 0U;
      std::uint64_t tight_slice_bytes = 0U;
      std::uint64_t next_texture_bytes = 0U;
      if (!CheckedMultiply(static_cast<std::uint64_t>(mip.width), 4U,
                           tight_row_bytes) ||
          !CheckedMultiply(tight_row_bytes,
                           static_cast<std::uint64_t>(mip.height),
                           tight_slice_bytes) ||
          tight_slice_bytes > static_cast<std::uint64_t>(
                                  (std::numeric_limits<std::size_t>::max)()) ||
          mip.row_pitch_bytes != tight_row_bytes ||
          mip.slice_pitch_bytes != tight_slice_bytes ||
          mip.bytes.size() != static_cast<std::size_t>(tight_slice_bytes) ||
          !CheckedAdd(texture_bytes, tight_slice_bytes, next_texture_bytes)) {
        return Failure(ValidationCode::SIZE_MISMATCH,
                       "material_observations.texture_payload",
                       "live native texture mip bytes are not canonical and "
                       "tightly packed",
                       index);
      }
      texture_bytes = next_texture_bytes;
    }
    if (texture_bytes >
        translator_configuration.maximum_decoded_bytes_per_asset) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "material_observations.texture_payload",
          "live native texture exceeds the configured per-asset byte cap",
          index);
    }
    std::uint64_t next_observed_texture_bytes = 0U;
    if (!CheckedAdd(candidate_observed_texture_bytes, texture_bytes,
                    next_observed_texture_bytes)) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "material_observations.texture_payload",
                     "live native texture byte accounting overflowed", index);
    }
    candidate_observed_texture_bytes = next_observed_texture_bytes;
    if (texture.key != observation.native_capture.material.texture_units[index]
                           .texture_key ||
        texture.color_role !=
            resolution.native_declaration.texture_color_role) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "material_observations.texture_semantic",
                     "captured texture reference or color role disagrees with "
                     "explicit semantics",
                     index);
    }
  }
  observed_texture_bytes = candidate_observed_texture_bytes;
  return ValidationResult::Success();
}

} // namespace

struct Ogre14LegacyPreparedMaterialFrame::State final {
  std::uint32_t version = kOgre14LegacyPreparedMaterialFrameVersion;
  std::shared_ptr<const Ogre14LegacyTranslatedFrame> translated_frame;
  std::vector<Ogre14LegacyPreparedMaterial> materials;
};

Ogre14LegacyPreparedMaterialFrame::Ogre14LegacyPreparedMaterialFrame(
    std::shared_ptr<const State> state) noexcept
    : state_(std::move(state)) {}

bool Ogre14LegacyPreparedMaterialFrame::initialized() const noexcept {
  return state_ != nullptr && state_->translated_frame != nullptr;
}

std::uint32_t Ogre14LegacyPreparedMaterialFrame::version() const noexcept {
  return state_ != nullptr ? state_->version : 0U;
}

const Ogre14LegacyTranslatedFrame *
Ogre14LegacyPreparedMaterialFrame::translated_frame() const noexcept {
  return state_ != nullptr ? state_->translated_frame.get() : nullptr;
}

const std::vector<Ogre14LegacyPreparedMaterial> &
Ogre14LegacyPreparedMaterialFrame::materials() const noexcept {
  static const std::vector<Ogre14LegacyPreparedMaterial> empty;
  return state_ != nullptr ? state_->materials : empty;
}

bool Ogre14LegacyPreparedMaterialFrame::SharesImmutableStateWith(
    const Ogre14LegacyPreparedMaterialFrame &other) const noexcept {
  return state_ != nullptr && state_.get() == other.state_.get();
}

struct Ogre14LegacyLiveMaterialCoordinator::PendingFrame final {
  Ogre14LegacyAssetTranslatorCommittableTransaction transaction;
  Ogre14LegacyPreparedMaterialFrame prepared;
};

Ogre14LegacyLiveMaterialCoordinator::~Ogre14LegacyLiveMaterialCoordinator() =
    default;

Ogre14LegacyLiveMaterialCoordinator::Ogre14LegacyLiveMaterialCoordinator(
    Ogre14LegacyLiveMaterialCoordinatorConfiguration configuration,
    Ogre14LegacyMaterialSemanticRegistry semantic_registry,
    std::unique_ptr<Ogre14LegacyAssetTranslator> translator) noexcept
    : configuration_(std::move(configuration)),
      semantic_registry_(std::move(semantic_registry)),
      translator_(std::move(translator)) {}

std::uint64_t
Ogre14LegacyLiveMaterialCoordinator::source_sequence() const noexcept {
  return translator_ != nullptr ? translator_->source_sequence() : 0U;
}

std::uint64_t
Ogre14LegacyLiveMaterialCoordinator::catalog_sequence() const noexcept {
  return translator_ != nullptr ? translator_->catalog_sequence() : 0U;
}

bool Ogre14LegacyLiveMaterialCoordinator::has_pending_frame() const noexcept {
  return pending_ != nullptr;
}

ValidationResult Ogre14LegacyLiveMaterialCoordinator::ResolveMaterialSemantics(
    const Ogre14LegacyAssetKey &material_key,
    Ogre14LegacyMaterialSemanticResolution &output) const {
  if (translator_ == nullptr || !semantic_registry_.initialized()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_coordinator.state",
                   "coordinator has no translator or semantic registry");
  }
  return semantic_registry_.Resolve(material_key, configuration_.translator,
                                    output);
}

ValidationResult Ogre14LegacyLiveMaterialCoordinator::PrepareFrame(
    std::uint64_t source_sequence,
    const std::vector<Ogre14LegacyMaterialObservation> &observations,
    Ogre14LegacyPreparedMaterialFrame &output,
    IOgre14LegacyLiveMaterialCoordinatorFaultInjector *fault_injector) try {
  if (pending_ != nullptr) {
    return Failure(
        ValidationCode::SEQUENCE_MISMATCH, "material_coordinator.pending",
        "the preceding material frame must be committed or discarded");
  }
  if (translator_ == nullptr || !semantic_registry_.initialized()) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "material_coordinator.state",
                   "coordinator has no translator or semantic registry");
  }
  if (observations.size() > configuration_.maximum_material_observations) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_observations.count",
                   "material observation count exceeds the configured cap");
  }
  if (source_sequence == 0U ||
      translator_->source_sequence() ==
          (std::numeric_limits<std::uint64_t>::max)() ||
      source_sequence != translator_->source_sequence() + 1U) {
    return Failure(ValidationCode::SEQUENCE_MISMATCH,
                   "material_coordinator.source_sequence",
                   "material frame source sequence must advance exactly once");
  }

  ValidationResult validation = ValidationResult::Success();

  struct IndexedObservation final {
    std::string material_stable_key;
    const Ogre14LegacyMaterialObservation *observation = nullptr;
  };
  std::vector<IndexedObservation> ordered;
  ordered.reserve(observations.size());
  std::uint64_t observed_texture_bytes = 0U;
  for (std::size_t index = 0U; index < observations.size(); ++index) {
    std::uint64_t observation_texture_bytes = 0U;
    validation = ValidateObservation(observations[index], semantic_registry_,
                                     configuration_.translator,
                                     observation_texture_bytes);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    std::uint64_t next_observed_texture_bytes = 0U;
    if (!CheckedAdd(observed_texture_bytes, observation_texture_bytes,
                    next_observed_texture_bytes) ||
        next_observed_texture_bytes >
            configuration_.translator.maximum_decoded_bytes_per_frame) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "material_observations.texture_bytes",
          "observed native texture bytes exceed the configured frame cap",
          index);
    }
    observed_texture_bytes = next_observed_texture_bytes;
    IndexedObservation indexed;
    indexed.observation = &observations[index];
    validation = BuildOgre14LegacyStableAssetKey(
        RenderAssetKind::MATERIAL, observations[index].material_key,
        indexed.material_stable_key);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    ordered.push_back(std::move(indexed));
    if (index == 0U && fault_injector != nullptr) {
      fault_injector->AtFaultPoint(
          Ogre14LegacyLiveMaterialCoordinatorFaultPoint::
              AFTER_FIRST_OBSERVATION);
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const IndexedObservation &lhs,
               const IndexedObservation &rhs) noexcept {
              return lhs.material_stable_key < rhs.material_stable_key;
            });
  for (std::size_t index = 1U; index < ordered.size(); ++index) {
    if (ordered[index - 1U].material_stable_key ==
        ordered[index].material_stable_key) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "material_observations.material_key",
                     "material observation set duplicates an exact material",
                     index);
    }
  }

  Ogre14LegacyAssetFrameInput frame_input;
  frame_input.source_sequence = source_sequence;
  frame_input.materials.reserve(ordered.size());
  std::map<std::string, const Ogre14LegacyTextureInput *, std::less<>> textures;
  std::uint64_t sampler_count = 0U;
  for (const IndexedObservation &indexed : ordered) {
    const Ogre14LegacyNativeMaterialCapture &capture =
        indexed.observation->native_capture;
    frame_input.materials.push_back(capture.material);
    std::uint64_t next_sampler_count = 0U;
    if (!CheckedAdd(
            sampler_count,
            static_cast<std::uint64_t>(capture.material.texture_units.size()),
            next_sampler_count)) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "material_observations.sampler_count",
                     "derived native sampler count overflowed");
    }
    sampler_count = next_sampler_count;
    for (const Ogre14LegacyTextureInput &texture : capture.textures) {
      std::string stable_key;
      validation = BuildOgre14LegacyStableAssetKey(RenderAssetKind::TEXTURE,
                                                   texture.key, stable_key);
      if (!validation) {
        return validation;
      }
      const auto existing = textures.find(stable_key);
      if (existing != textures.end()) {
        if (existing->second == nullptr ||
            !SameTexture(*existing->second, texture)) {
          return Failure(
              ValidationCode::REVISION_MISMATCH,
              "material_observations.shared_texture",
              "one exact texture key has conflicting captured state");
        }
      } else {
        if (textures.size() >=
            configuration_.translator.maximum_texture_inputs_per_frame) {
          return Failure(
              ValidationCode::VALUE_OUT_OF_RANGE,
              "material_observations.texture_count",
              "unique native texture count exceeds the configured cap");
        }
        textures.emplace(std::move(stable_key), &texture);
      }
    }
  }
  std::uint64_t derived_live_asset_count = 0U;
  if (!CheckedAdd(static_cast<std::uint64_t>(textures.size()), sampler_count,
                  derived_live_asset_count) ||
      !CheckedAdd(derived_live_asset_count,
                  static_cast<std::uint64_t>(ordered.size()),
                  derived_live_asset_count) ||
      derived_live_asset_count >
          configuration_.translator.maximum_live_assets_per_frame) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "material_observations.live_asset_count",
                   "derived native asset count exceeds the configured cap");
  }
  frame_input.textures.reserve(textures.size());
  for (const auto &entry : textures) {
    if (entry.second == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_observations.textures",
                     "canonical native texture index contains no input");
    }
    frame_input.textures.push_back(*entry.second);
  }

  auto candidate_pending = std::make_unique<PendingFrame>();
  validation =
      translator_->BeginCommittableTransaction(candidate_pending->transaction);
  if (!validation) {
    return validation;
  }

  Ogre14LegacyTranslatedFrame translated;
  validation = candidate_pending->transaction.candidate()->Translate(
      frame_input, translated);
  if (!validation) {
    return validation;
  }
  validation =
      candidate_pending->transaction.candidate()->BuildFullSnapshot(translated);
  if (!validation) {
    return validation;
  }

  std::vector<Ogre14LegacyMaterialClosureRequest> requests;
  requests.reserve(ordered.size());
  for (const IndexedObservation &indexed : ordered) {
    Ogre14LegacyMaterialClosureRequest request;
    validation = MakeOgre14LegacyMaterialClosureRequest(
        translated, indexed.observation->material_key, request);
    if (!validation) {
      return validation;
    }
    requests.push_back(std::move(request));
  }
  Ogre14LegacyMaterialClosureBatch batch;
  validation =
      ResolveOgre14LegacyMaterialClosureBatch(translated, requests, batch);
  if (!validation) {
    return validation;
  }

  auto prepared_state =
      std::make_shared<Ogre14LegacyPreparedMaterialFrame::State>();
  prepared_state->translated_frame =
      std::make_shared<const Ogre14LegacyTranslatedFrame>(
          std::move(translated));
  prepared_state->materials.reserve(batch.closures.size());
  for (Ogre14LegacyMaterialClosure &closure : batch.closures) {
    if (closure.asset_keys.empty()) {
      return Failure(ValidationCode::MISSING_REFERENCE,
                     "material_closures.material_key",
                     "resolved closure omitted its exact material key");
    }
    Ogre14LegacyPreparedMaterial material;
    material.material_key = closure.asset_keys.back();
    material.closure =
        std::make_shared<const Ogre14LegacyMaterialClosure>(std::move(closure));
    prepared_state->materials.push_back(std::move(material));
  }
  Ogre14LegacyPreparedMaterialFrame prepared(
      std::shared_ptr<const Ogre14LegacyPreparedMaterialFrame::State>(
          std::move(prepared_state)));
  if (fault_injector != nullptr) {
    fault_injector->AtFaultPoint(Ogre14LegacyLiveMaterialCoordinatorFaultPoint::
                                     BEFORE_PREPARED_FRAME_PUBLISH);
  }
  static_assert(std::is_nothrow_move_assignable<decltype(pending_)>::value,
                "publishing the pending material transaction must not throw");
  candidate_pending->prepared = prepared;
  output = std::move(prepared);
  pending_ = std::move(candidate_pending);
  return ValidationResult::Success();
} catch (const std::bad_alloc &) {
  return Failure(ValidationCode::EMPTY_PAYLOAD,
                 "material_coordinator.allocation",
                 "allocation failed before the material frame was published");
} catch (...) {
  return Failure(
      ValidationCode::UNSUPPORTED_FEATURE, "material_coordinator.exception",
      "unexpected exception before the material frame was published");
}

Ogre14LegacyPreparedMaterialCommitResult
Ogre14LegacyLiveMaterialCoordinator::CommitPreparedFrameAfterAcceptedExposure(
    const Ogre14LegacyPreparedMaterialFrame &accepted_frame) noexcept {
  if (pending_ == nullptr) {
    return Ogre14LegacyPreparedMaterialCommitResult::NO_PENDING_FRAME;
  }
  if (!pending_->prepared.SharesImmutableStateWith(accepted_frame)) {
    return Ogre14LegacyPreparedMaterialCommitResult::PREPARED_FRAME_MISMATCH;
  }
  const Ogre14LegacyAssetTranslatorExclusiveCommitResult result =
      pending_->transaction.CommitAfterAcceptedExposure();
  pending_.reset();
  return result == Ogre14LegacyAssetTranslatorExclusiveCommitResult::COMMITTED
             ? Ogre14LegacyPreparedMaterialCommitResult::COMMITTED
             : Ogre14LegacyPreparedMaterialCommitResult::
                   TRANSLATOR_INVARIANT_BROKEN;
}

void Ogre14LegacyLiveMaterialCoordinator::DiscardPreparedFrame() noexcept {
  pending_.reset();
}

ValidationResult ValidateOgre14LegacyLiveMaterialCoordinatorConfiguration(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration) {
  if (configuration.version != kOgre14LegacyLiveMaterialCoordinatorVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "material_coordinator.configuration.version",
                   "unsupported live material coordinator version");
  }
  ValidationResult validation =
      ValidateOgre14LegacyAssetTranslatorConfiguration(
          configuration.translator);
  if (!validation) {
    return validation;
  }
  validation = ValidateOgre14LegacyAssetTranslatorTransactionConfiguration(
      configuration.transaction);
  if (!validation) {
    return validation;
  }
  if (configuration.maximum_material_observations == 0U ||
      configuration.maximum_material_observations >
          configuration.translator.maximum_material_inputs_per_frame ||
      configuration.maximum_material_observations >
          kDefaultOgre14LegacyMaximumMaterialObservations) {
    return Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "material_coordinator.configuration.observations",
        "material observation cap is zero or exceeds translator limits");
  }
  return ValidationResult::Success();
}

ValidationResult CreateOgre14LegacyLiveMaterialCoordinator(
    const Ogre14LegacyLiveMaterialCoordinatorConfiguration &configuration,
    const Ogre14LegacyMaterialSemanticRegistry &semantic_registry,
    std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator> &output,
    IOgre14LegacyAssetTranslatorFaultInjector *translator_fault_injector) try {
  ValidationResult validation =
      ValidateOgre14LegacyLiveMaterialCoordinatorConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  if (!semantic_registry.initialized()) {
    return Failure(
        ValidationCode::MISSING_REFERENCE,
        "material_coordinator.semantic_registry",
        "live coordinator requires an initialized semantic registry");
  }
  auto translator = std::make_unique<Ogre14LegacyAssetTranslator>(
      configuration.translator, configuration.transaction,
      translator_fault_injector);
  auto candidate = std::unique_ptr<Ogre14LegacyLiveMaterialCoordinator>(
      new Ogre14LegacyLiveMaterialCoordinator(configuration, semantic_registry,
                                              std::move(translator)));
  output = std::move(candidate);
  return ValidationResult::Success();
} catch (const std::bad_alloc &) {
  return Failure(ValidationCode::EMPTY_PAYLOAD,
                 "material_coordinator.allocation",
                 "allocation failed before the coordinator was published");
} catch (...) {
  return Failure(ValidationCode::UNSUPPORTED_FEATURE,
                 "material_coordinator.exception",
                 "unexpected exception before the coordinator was published");
}

const Ogre14LegacyMaterialClosure *FindOgre14LegacyPreparedMaterialClosure(
    const Ogre14LegacyPreparedMaterialFrame &frame,
    const Ogre14LegacyAssetKey &material_key) noexcept {
  try {
    const Ogre14LegacyTranslatedFrame *translated_frame =
        frame.translated_frame();
    if (frame.version() != kOgre14LegacyPreparedMaterialFrameVersion ||
        translated_frame == nullptr ||
        material_key.exact_resource_group.empty() ||
        material_key.exact_name.empty()) {
      return nullptr;
    }
    for (const Ogre14LegacyPreparedMaterial &material : frame.materials()) {
      if (material.material_key == material_key &&
          material.closure != nullptr &&
          ValidateOgre14LegacyMaterialClosureForFrame(
              *translated_frame, *material.closure, material_key)
              .ok()) {
        return material.closure.get();
      }
    }
    return nullptr;
  } catch (...) {
    return nullptr;
  }
}

static_assert(std::is_nothrow_destructible<
                  Ogre14LegacyAssetTranslatorCommittableTransaction>::value,
              "discarding an unaccepted material transaction must not throw");
static_assert(
    std::is_nothrow_move_assignable<Ogre14LegacyPreparedMaterialFrame>::value,
    "publishing a prepared material frame must not throw");
static_assert(
    std::is_nothrow_copy_assignable<Ogre14LegacyPreparedMaterialFrame>::value,
    "retaining the exact accepted material frame must not throw");
static_assert(
    std::is_nothrow_default_constructible<
        std::vector<Ogre14LegacyPreparedMaterial>>::value,
    "the immutable empty material view must not throw during construction");
static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t),
              "material byte accounting requires size_t to fit in uint64_t");

} // namespace RoR::Render
