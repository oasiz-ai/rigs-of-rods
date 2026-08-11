/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "GraphicsSceneSnapshotProducer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace RoR::Render {
namespace {

constexpr std::uint64_t kFirstAssetSequence = 1U;
static_assert(static_cast<std::size_t>(MaterialTextureSlot::BASE_COLOR) == 0U);
static_assert(
    static_cast<std::size_t>(MaterialTextureSlot::METALLIC_ROUGHNESS) == 1U);
static_assert(static_cast<std::size_t>(MaterialTextureSlot::NORMAL) == 2U);
static_assert(static_cast<std::size_t>(MaterialTextureSlot::OCCLUSION) == 3U);
static_assert(static_cast<std::size_t>(MaterialTextureSlot::EMISSIVE) == 4U);
static_assert(static_cast<std::size_t>(MaterialTextureSlot::SPECULAR) == 5U);

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, index);
}

ValidationResult AtAsset(ValidationResult validation, std::size_t index) {
  if (!validation && validation.element_index == ValidationResult::kNoElement) {
    validation.element_index = index;
  }
  return validation;
}

std::array<TextureBinding *, kGraphicsSceneMaterialTextureSlotCount>
MaterialBindings(MaterialDescriptor &material) noexcept {
  return {{&material.base_color_texture,
           &material.metallic_roughness_texture,
           &material.normal_texture,
           &material.occlusion_texture,
           &material.emissive_texture,
           &material.specular_texture}};
}

std::array<const TextureBinding *, kGraphicsSceneMaterialTextureSlotCount>
MaterialBindings(const MaterialDescriptor &material) noexcept {
  return {{&material.base_color_texture,
           &material.metallic_roughness_texture,
           &material.normal_texture,
           &material.occlusion_texture,
           &material.emissive_texture,
           &material.specular_texture}};
}

bool BindingIsAbsent(const GraphicsSceneAssetBinding &binding) noexcept {
  return binding.texture_source_asset_id == 0U &&
         binding.sampler_source_asset_id == 0U;
}

bool BindingIsPaired(const GraphicsSceneAssetBinding &binding) noexcept {
  return (binding.texture_source_asset_id == 0U) ==
         (binding.sampler_source_asset_id == 0U);
}

bool AddByteCount(std::uint64_t amount, std::uint64_t &total) noexcept {
  if (amount > (std::numeric_limits<std::uint64_t>::max)() - total) {
    return false;
  }
  total += amount;
  return true;
}

bool AddElementBytes(std::size_t count, std::size_t element_size,
                     std::uint64_t &total) noexcept {
  if (element_size != 0U &&
      count > (std::numeric_limits<std::uint64_t>::max)() / element_size) {
    return false;
  }
  return AddByteCount(static_cast<std::uint64_t>(count) * element_size, total);
}

bool AddMeshPayloadBytes(const MeshResourceDescriptor &mesh,
                         std::uint64_t &total) noexcept {
  return AddElementBytes(mesh.debug_name.size(), sizeof(char), total) &&
         AddElementBytes(mesh.positions.size(), sizeof(Float3), total) &&
         AddElementBytes(mesh.normals.size(), sizeof(Float3), total) &&
         AddElementBytes(mesh.tangents.size(), sizeof(Float4), total) &&
         AddElementBytes(mesh.velocities.size(), sizeof(Float3), total) &&
         AddElementBytes(mesh.texture_coordinates_0.size(), sizeof(Float2),
                         total) &&
         AddElementBytes(mesh.texture_coordinates_1.size(), sizeof(Float2),
                         total) &&
         AddElementBytes(mesh.colors.size(), sizeof(Float4), total) &&
         AddElementBytes(mesh.indices.size(), sizeof(std::uint32_t), total);
}

bool AddTexturePayloadBytes(const TextureResourceDescriptor &texture,
                            std::uint64_t &total) noexcept {
  if (!AddElementBytes(texture.debug_name.size(), sizeof(char), total)) {
    return false;
  }
  for (const TextureMipLevelDescriptor &mip : texture.mip_levels) {
    if (!AddElementBytes(mip.bytes.size(), sizeof(std::uint8_t), total)) {
      return false;
    }
  }
  return true;
}

bool AddPayloadBytes(const RenderAssetPayload &payload,
                     std::uint64_t &total) noexcept {
  if (const auto *mesh = std::get_if<MeshResourceDescriptor>(&payload)) {
    return AddMeshPayloadBytes(*mesh, total);
  }
  if (const auto *texture =
          std::get_if<TextureResourceDescriptor>(&payload)) {
    return AddTexturePayloadBytes(*texture, total);
  }
  if (const auto *material = std::get_if<MaterialDescriptor>(&payload)) {
    return AddElementBytes(material->debug_name.size(), sizeof(char), total);
  }
  if (const auto *sampler =
          std::get_if<SamplerResourceDescriptor>(&payload)) {
    return AddElementBytes(sampler->debug_name.size(), sizeof(char), total);
  }
  return false;
}

struct IndexedAssetInput {
  const GraphicsSceneAssetInput *input = nullptr;
  std::size_t original_index = 0U;
};

struct IndexedStaticMeshInput {
  const GraphicsSceneStaticMeshInput *input = nullptr;
  std::size_t original_index = 0U;
};

struct IndexedDynamicMeshInput {
  const GraphicsSceneDynamicMeshInput *input = nullptr;
  std::size_t original_index = 0U;
};

struct IndexedMeshObjectInput {
  std::uint64_t source_object_id = 0U;
  const GraphicsSceneStaticMeshInput *static_input = nullptr;
  const GraphicsSceneDynamicMeshInput *dynamic_input = nullptr;
  std::size_t original_index = 0U;

  [[nodiscard]] bool dynamic() const noexcept {
    return dynamic_input != nullptr;
  }
};

struct IndexedLightInput {
  const GraphicsSceneLightInput *input = nullptr;
  std::size_t original_index = 0U;
};

struct IndexedReflectionProbeInput {
  const ReflectionProbeRuntimeDescriptor *input = nullptr;
  std::size_t original_index = 0U;
};

ValidationResult RemapSceneElementIndex(
    ValidationResult validation,
    const std::vector<IndexedMeshObjectInput> &sorted_objects,
    const std::vector<IndexedDynamicMeshInput> &sorted_dynamic_objects,
    const std::vector<IndexedLightInput> &sorted_lights,
    const std::vector<IndexedReflectionProbeInput> &sorted_probes) {
  if (validation ||
      validation.element_index == ValidationResult::kNoElement) {
    return validation;
  }
  if (validation.field.compare(0U, sizeof("mesh_instances") - 1U,
                               "mesh_instances") == 0 &&
      validation.element_index < sorted_objects.size()) {
    validation.element_index =
        sorted_objects[validation.element_index].original_index;
  } else if (validation.field.compare(
                 0U, sizeof("dynamic_mesh_updates") - 1U,
                 "dynamic_mesh_updates") == 0 &&
             validation.element_index < sorted_dynamic_objects.size()) {
    validation.element_index =
        sorted_dynamic_objects[validation.element_index].original_index;
  } else if (validation.field.compare(0U, sizeof("lights") - 1U, "lights") ==
                 0 &&
             validation.element_index < sorted_lights.size()) {
    validation.element_index =
        sorted_lights[validation.element_index].original_index;
  } else if (validation.field.compare(
                 0U, sizeof("reflection_probes") - 1U,
                 "reflection_probes") == 0 &&
             validation.element_index < sorted_probes.size()) {
    validation.element_index =
        sorted_probes[validation.element_index].original_index;
  }
  return validation;
}

bool EquivalentDynamicMeshStateContents(
    const GraphicsSceneDynamicMeshState &lhs,
    const GraphicsSceneDynamicMeshState &rhs) noexcept {
  return lhs.topology_revision == rhs.topology_revision &&
         lhs.positions == rhs.positions && lhs.normals == rhs.normals &&
         lhs.tangents == rhs.tangents && lhs.velocities == rhs.velocities &&
         lhs.updated_local_bounds.minimum == rhs.updated_local_bounds.minimum &&
         lhs.updated_local_bounds.maximum == rhs.updated_local_bounds.maximum;
}

bool HasExactTightBounds(const GraphicsSceneDynamicMeshState &state) noexcept {
  if (state.positions.empty() || !IsFinite(state.positions.front())) {
    return false;
  }
  Bounds3 tight;
  tight.minimum = state.positions.front();
  tight.maximum = state.positions.front();
  for (const Float3 &position : state.positions) {
    if (!IsFinite(position)) {
      return false;
    }
    tight.minimum.x = (std::min)(tight.minimum.x, position.x);
    tight.minimum.y = (std::min)(tight.minimum.y, position.y);
    tight.minimum.z = (std::min)(tight.minimum.z, position.z);
    tight.maximum.x = (std::max)(tight.maximum.x, position.x);
    tight.maximum.y = (std::max)(tight.maximum.y, position.y);
    tight.maximum.z = (std::max)(tight.maximum.z, position.z);
  }
  return tight.minimum == state.updated_local_bounds.minimum &&
         tight.maximum == state.updated_local_bounds.maximum;
}

bool ValidateSourceAssetMetadata(const GraphicsSceneAssetInput &asset,
                                 std::size_t index,
                                 ValidationResult &failure) {
  if (asset.source_asset_id == 0U) {
    failure = Failure(ValidationCode::INVALID_IDENTIFIER,
                      "assets.source_asset_id",
                      "source asset identity must be nonzero", index);
    return false;
  }
  if (asset.payload == nullptr) {
    failure = Failure(ValidationCode::EMPTY_PAYLOAD, "assets.payload",
                      "source asset requires a portable descriptor", index);
    return false;
  }
  const RenderAssetKind kind = RenderAssetPayloadKind(*asset.payload);
  if (kind == RenderAssetKind::INVALID) {
    failure = Failure(ValidationCode::EMPTY_PAYLOAD, "assets.payload",
                      "source asset requires a portable descriptor", index);
    return false;
  }

  if (kind == RenderAssetKind::MATERIAL) {
    const MaterialDescriptor &material =
        std::get<MaterialDescriptor>(*asset.payload);
    for (const TextureBinding *binding : MaterialBindings(material)) {
      if (!IsAbsentRenderAssetReference(binding->texture) ||
          !IsAbsentRenderAssetReference(binding->sampler)) {
        failure = Failure(
            ValidationCode::INVALID_ASSET_REFERENCE,
            "assets.material.texture_binding",
            "source materials must leave producer-owned asset references absent",
            index);
        return false;
      }
    }
  }

  for (std::size_t slot = 0U; slot < asset.material_bindings.size(); ++slot) {
    const GraphicsSceneAssetBinding &binding = asset.material_bindings[slot];
    if (!BindingIsPaired(binding)) {
      failure = Failure(
          ValidationCode::MISSING_REFERENCE, "assets.material_bindings",
          "texture and sampler source identities must be supplied together",
          index);
      return false;
    }
    if (kind != RenderAssetKind::MATERIAL && !BindingIsAbsent(binding)) {
      failure = Failure(
          ValidationCode::WRONG_ASSET_KIND, "assets.material_bindings",
          "only material assets may declare texture dependencies", index);
      return false;
    }
  }
  return true;
}

ValidationResult
ValidateSourceAssetPayload(const GraphicsSceneAssetInput &asset,
                           std::size_t index) {
  const RenderAssetKind kind = RenderAssetPayloadKind(*asset.payload);
  ValidationResult validation;
  switch (kind) {
  case RenderAssetKind::MESH: {
    const MeshResourceDescriptor &mesh =
        std::get<MeshResourceDescriptor>(*asset.payload);
    validation = ValidateMeshResourceDescriptor(mesh);
    break;
  }
  case RenderAssetKind::TEXTURE:
    validation = ValidateTextureResourceDescriptor(
        std::get<TextureResourceDescriptor>(*asset.payload));
    break;
  case RenderAssetKind::MATERIAL: {
    const MaterialDescriptor &material =
        std::get<MaterialDescriptor>(*asset.payload);
    validation = ValidateMaterialDescriptor(material);
    break;
  }
  case RenderAssetKind::SAMPLER:
    validation = ValidateSamplerResourceDescriptor(
        std::get<SamplerResourceDescriptor>(*asset.payload));
    break;
  case RenderAssetKind::INVALID:
    return Failure(ValidationCode::EMPTY_PAYLOAD, "assets.payload",
                   "source asset requires a portable descriptor", index);
  }
  if (!validation) {
    return AtAsset(std::move(validation), index);
  }
  return ValidationResult::Success();
}

ValidationResult ValidateMeshRevisionLineage(
    const MeshResourceDescriptor &previous,
    const MeshResourceDescriptor &current, bool contents_changed,
    std::size_t input_index) {
  const std::uint64_t previous_revision = previous.topology_revision;
  if (current.topology_revision < previous_revision) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "assets.mesh.topology_revision",
                   "mesh topology revision moved backwards", input_index);
  }
  if (current.topology_revision != previous_revision) {
    if (previous_revision == (std::numeric_limits<std::uint64_t>::max)() ||
        current.topology_revision != previous_revision + 1U) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "assets.mesh.topology_revision",
                     "mesh topology revision must advance by exactly one",
                     input_index);
    }
  } else if (contents_changed) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "assets.mesh.topology_revision",
                   "changed mesh contents require a new topology revision",
                   input_index);
  }
  return ValidationResult::Success();
}

bool RebasePreviousObjectTransform(const Matrix4x4 &previous,
                                   const Double3 &previous_origin,
                                   const Double3 &current_origin,
                                   Matrix4x4 &rebased) noexcept {
  rebased = previous;
  const std::array<double, 3U> origin_delta{{
      previous_origin.x - current_origin.x,
      previous_origin.y - current_origin.y,
      previous_origin.z - current_origin.z,
  }};
  const std::array<double, 3U> translations{{
      origin_delta[0U] + static_cast<double>(previous.elements[12U]),
      origin_delta[1U] + static_cast<double>(previous.elements[13U]),
      origin_delta[2U] + static_cast<double>(previous.elements[14U]),
  }};
  for (std::size_t axis = 0U; axis < translations.size(); ++axis) {
    if (!std::isfinite(translations[axis]) ||
        std::fabs(translations[axis]) >
            static_cast<double>((std::numeric_limits<float>::max)())) {
      return false;
    }
    rebased.elements[12U + axis] = static_cast<float>(translations[axis]);
  }
  return HasInvertibleAffineTransform(rebased);
}

bool RebasePreviousPosition(const Float3 &previous,
                            const Double3 &previous_origin,
                            const Double3 &current_origin,
                            Float3 &rebased) noexcept {
  const std::array<double, 3U> origin_delta{{
      previous_origin.x - current_origin.x,
      previous_origin.y - current_origin.y,
      previous_origin.z - current_origin.z,
  }};
  const std::array<double, 3U> positions{{
      origin_delta[0U] + static_cast<double>(previous.x),
      origin_delta[1U] + static_cast<double>(previous.y),
      origin_delta[2U] + static_cast<double>(previous.z),
  }};
  for (std::size_t axis = 0U; axis < positions.size(); ++axis) {
    if (!std::isfinite(positions[axis]) ||
        std::fabs(positions[axis]) >
            static_cast<double>((std::numeric_limits<float>::max)())) {
      return false;
    }
  }
  rebased = {static_cast<float>(positions[0U]),
             static_cast<float>(positions[1U]),
             static_cast<float>(positions[2U])};
  return true;
}

bool RebasePreviousViewTransform(const Matrix4x4 &previous,
                                 const Double3 &previous_origin,
                                 const Double3 &current_origin,
                                 Matrix4x4 &rebased) noexcept {
  rebased = previous;
  const std::array<double, 3U> origin_delta{{
      current_origin.x - previous_origin.x,
      current_origin.y - previous_origin.y,
      current_origin.z - previous_origin.z,
  }};
  for (std::size_t row = 0U; row < 3U; ++row) {
    const double translated =
        static_cast<double>(previous.elements[12U + row]) +
        static_cast<double>(previous.elements[row]) * origin_delta[0U] +
        static_cast<double>(previous.elements[4U + row]) * origin_delta[1U] +
        static_cast<double>(previous.elements[8U + row]) * origin_delta[2U];
    if (!std::isfinite(translated) ||
        std::fabs(translated) >
            static_cast<double>((std::numeric_limits<float>::max)())) {
      return false;
    }
    rebased.elements[12U + row] = static_cast<float>(translated);
  }
  return HasRigidRightHandedAffineTransform(rebased);
}

} // namespace

class GraphicsSceneSnapshotProducer::Impl final {
public:
  struct AssetState {
    RenderAssetReference asset;
    /// Candidate transactions shallow-copy immutable payload owners. A
    /// transform/camera-only frame therefore never copies mesh or texel bytes.
    std::shared_ptr<const RenderAssetPayload> payload;
    /// Non-owning identity of the graphics cache's immutable source payload.
    /// Locking and pointer-comparing this avoids an O(payload bytes) equality
    /// scan on stable frames without retaining a second mesh/texture owner.
    std::weak_ptr<const RenderAssetPayload> source_identity;
    /// Materials alone retain their small unresolved source descriptor because
    /// their canonical payload contains producer-owned asset references.
    std::shared_ptr<const RenderAssetPayload> material_source_payload;
    std::array<GraphicsSceneAssetBinding,
               kGraphicsSceneMaterialTextureSlotCount>
        material_bindings{};
    bool live = false;
  };

  struct AssetCatalog {
    AssetCatalog(std::uint64_t registry_id, std::uint64_t first_asset_ordinal)
        : registry(registry_id), next_asset_ordinal(first_asset_ordinal) {}

    RenderAssetRegistry registry;
    std::map<std::uint64_t, AssetState> assets;
    std::uint64_t next_asset_ordinal = 1U;
    std::size_t lifetime_asset_records = 0U;
    bool asset_ordinal_exhausted = false;
  };

  struct ObjectState {
    std::uint64_t source_object_id = 0U;
    Matrix4x4 render_from_object;
    std::uint64_t mesh_source_asset_id = 0U;
    std::uint64_t material_source_asset_id = 0U;
    std::shared_ptr<const GraphicsSceneDynamicMeshState> deformation;
    bool dynamic = false;
    bool live = false;
  };

  struct LightState {
    std::uint64_t source_light_id = 0U;
    LightType type = LightType::DIRECTIONAL;
    Float3 position{};
    Float3 direction{0.0F, -1.0F, 0.0F};
    bool live = false;
  };

  struct ReflectionProbeState {
    std::uint64_t probe_id = 0U;
    std::uint64_t content_revision = 0U;
    ReflectionProbeRuntimeDescriptor descriptor;
    bool live = false;
  };

  struct ValidatedMeshAssetPair {
    RenderAssetReference mesh;
    RenderAssetReference material;

    friend bool operator==(const ValidatedMeshAssetPair &lhs,
                           const ValidatedMeshAssetPair &rhs) noexcept {
      return lhs.mesh == rhs.mesh && lhs.material == rhs.material;
    }
  };

  struct ValidatedEnvironmentAssets {
    RenderAssetReference texture;
    RenderAssetReference sampler;

    friend bool operator==(const ValidatedEnvironmentAssets &lhs,
                           const ValidatedEnvironmentAssets &rhs) noexcept {
      return lhs.texture == rhs.texture && lhs.sampler == rhs.sampler;
    }
  };

  struct CameraState {
    GraphicsSceneCameraInput camera;
    Double3 absolute_world_origin_meters{};
    bool initialized = false;
  };

  explicit Impl(GraphicsSceneSnapshotProducerConfiguration producer_config)
      : configuration(std::move(producer_config)),
        asset_catalog(std::make_shared<const AssetCatalog>(
            configuration.registry_id, configuration.first_asset_ordinal)),
        next_snapshot_id(configuration.first_snapshot_id) {
    if (configuration.registry_id == 0U) {
      configuration_validation =
          Failure(ValidationCode::INVALID_IDENTIFIER, "registry_id",
                  "producer registry identity must be nonzero");
    } else if (configuration.first_snapshot_id == 0U) {
      configuration_validation =
          Failure(ValidationCode::INVALID_IDENTIFIER, "first_snapshot_id",
                  "first snapshot identity must be nonzero");
    } else if (configuration.first_asset_ordinal == 0U) {
      configuration_validation =
          Failure(ValidationCode::INVALID_IDENTIFIER, "first_asset_ordinal",
                  "first asset ordinal must be nonzero");
    } else if (configuration.maximum_asset_records == 0U ||
               configuration.maximum_static_mesh_objects == 0U ||
               configuration.maximum_dynamic_mesh_objects == 0U ||
               configuration.maximum_dynamic_vertex_count == 0U ||
               configuration.maximum_dynamic_payload_bytes == 0U ||
               configuration.maximum_light_records == 0U ||
               configuration.maximum_reflection_probe_records == 0U ||
               configuration.maximum_asset_payload_bytes == 0U) {
      configuration_validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "configuration.limits",
          "producer limits must all be nonzero");
    }
  }

  [[nodiscard]] bool HasValidatedSourceIdentity(
      const GraphicsSceneAssetInput &input) const {
    if (input.source_asset_id == 0U || input.payload == nullptr) {
      return false;
    }
    const auto current = asset_catalog->assets.find(input.source_asset_id);
    if (current == asset_catalog->assets.end() || !current->second.live ||
        current->second.asset.kind != RenderAssetPayloadKind(*input.payload)) {
      return false;
    }
    return current->second.source_identity.lock() == input.payload;
  }

  [[nodiscard]] bool AssetCatalogSourceIdentitiesMatch(
      const std::vector<IndexedAssetInput> &sorted_assets) const {
    auto current = asset_catalog->assets.begin();
    for (const IndexedAssetInput &indexed_input : sorted_assets) {
      const GraphicsSceneAssetInput &input = *indexed_input.input;
      while (current != asset_catalog->assets.end() &&
             !current->second.live) {
        ++current;
      }
      if (current == asset_catalog->assets.end()) {
        return false;
      }
      const std::shared_ptr<const RenderAssetPayload> source_identity =
          current->second.source_identity.lock();
      if (current->first != input.source_asset_id ||
          source_identity != input.payload ||
          current->second.asset.kind !=
              RenderAssetPayloadKind(*input.payload) ||
          current->second.material_bindings != input.material_bindings) {
        return false;
      }
      ++current;
    }
    while (current != asset_catalog->assets.end() && !current->second.live) {
      ++current;
    }
    return current == asset_catalog->assets.end();
  }

  [[nodiscard]] static bool RecordPayloadValidation(
      std::uint64_t candidate_bytes,
      GraphicsSceneSnapshotProduction::Diagnostics &diagnostics) noexcept {
    if (diagnostics.asset_payload_full_validations ==
            (std::numeric_limits<std::uint64_t>::max)() ||
        !AddByteCount(candidate_bytes,
                      diagnostics.asset_payload_candidate_bytes_validated)) {
      return false;
    }
    ++diagnostics.asset_payload_full_validations;
    return true;
  }

  [[nodiscard]] static bool RecordPayloadFallback(
      const RenderAssetPayload &candidate,
      GraphicsSceneSnapshotProduction::Diagnostics &diagnostics) noexcept {
    if (diagnostics.asset_payload_fallback_comparisons ==
        (std::numeric_limits<std::uint64_t>::max)()) {
      return false;
    }
    ++diagnostics.asset_payload_fallback_comparisons;
    return AddPayloadBytes(
        candidate, diagnostics.asset_payload_candidate_bytes_compared);
  }

  [[nodiscard]] GraphicsSceneSnapshotProduceResult
  Produce(const GraphicsSceneFrameInput &frame,
          bool finalize_scene_generation = false) {
    GraphicsSceneSnapshotProduceResult result;
    if (!configuration_validation) {
      result.validation = configuration_validation;
      return result;
    }
    if (frame.version != kGraphicsSceneSnapshotProducerVersion) {
      result.validation =
          Failure(ValidationCode::UNSUPPORTED_VERSION, "version",
                  "unsupported joined graphics frame version");
      return result;
    }
    if (!IsFinite(frame.simulation_time_seconds) ||
        frame.simulation_time_seconds < 0.0) {
      result.validation = Failure(
          IsFinite(frame.simulation_time_seconds)
              ? ValidationCode::VALUE_OUT_OF_RANGE
              : ValidationCode::NON_FINITE_VALUE,
          "simulation_time_seconds",
          "simulation time must be finite and nonnegative");
      return result;
    }
    if (!IsFinite(frame.absolute_world_origin_meters)) {
      result.validation =
          Failure(ValidationCode::NON_FINITE_VALUE,
                  "absolute_world_origin_meters",
                  "absolute render origin must be finite");
      return result;
    }
    if (time_lineage_initialized &&
        (frame.simulation_tick < last_simulation_tick ||
         frame.simulation_time_seconds < last_simulation_time_seconds)) {
      result.validation = Failure(
          ValidationCode::SEQUENCE_MISMATCH, "simulation_time",
          "joined graphics frames may not move simulation time backwards");
      return result;
    }
    if (snapshot_id_exhausted) {
      result.validation =
          Failure(ValidationCode::VALUE_OUT_OF_RANGE, "snapshot_id",
                  "producer snapshot identity space is exhausted");
      return result;
    }
    if (frame.assets.size() > configuration.maximum_asset_records) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "assets",
          "live source asset count exceeds the configured bound");
      return result;
    }
    if (frame.static_meshes.size() >
        configuration.maximum_static_mesh_objects) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "static_meshes",
          "live static object count exceeds the configured bound");
      return result;
    }
    if (frame.dynamic_meshes.size() >
        configuration.maximum_dynamic_mesh_objects) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "dynamic_meshes",
          "live dynamic object count exceeds the configured bound");
      return result;
    }
    if (frame.lights.size() > configuration.maximum_light_records) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights",
          "live light count exceeds the configured bound");
      return result;
    }
    if (frame.reflection_probes.size() >
        configuration.maximum_reflection_probe_records) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "reflection_probes",
          "live reflection-probe count exceeds the configured bound");
      return result;
    }
    if (!IsAbsentRenderAssetReference(
            frame.environment.environment_texture) ||
        !IsAbsentRenderAssetReference(
            frame.environment.environment_sampler)) {
      result.validation = Failure(
          ValidationCode::INVALID_ASSET_REFERENCE, "environment",
          "source environment must leave producer-owned references absent");
      return result;
    }
    if ((frame.environment_texture_source_asset_id == 0U) !=
        (frame.environment_sampler_source_asset_id == 0U)) {
      result.validation = Failure(
          ValidationCode::MISSING_REFERENCE, "environment",
          "environment texture and sampler source identities are paired");
      return result;
    }

    std::vector<IndexedAssetInput> sorted_assets;
    sorted_assets.reserve(frame.assets.size());
    std::uint64_t payload_bytes = 0U;
    for (std::size_t index = 0U; index < frame.assets.size(); ++index) {
      const GraphicsSceneAssetInput &input = frame.assets[index];
      // Avoid constructing two successful std::strings per asset. MSVC's
      // checked-iterator Debug STL allocates a proxy for each such string,
      // which would turn the stable-catalog path into O(asset count) heap
      // allocation calls despite doing no logical per-asset allocation.
      if (!ValidateSourceAssetMetadata(input, index, result.validation)) {
        return result;
      }
      std::uint64_t candidate_payload_bytes = 0U;
      if (!AddPayloadBytes(*input.payload, candidate_payload_bytes)) {
        result.validation = Failure(
            ValidationCode::SIZE_MISMATCH, "assets.payload_bytes",
            "asset payload byte count overflowed", index);
        return result;
      }
      if (!HasValidatedSourceIdentity(input)) {
        if (!RecordPayloadValidation(candidate_payload_bytes,
                                     result.production.diagnostics)) {
          result.validation = Failure(
              ValidationCode::SIZE_MISMATCH, "assets.payload_bytes",
              "validated payload byte count overflowed", index);
          return result;
        }
        result.validation = ValidateSourceAssetPayload(input, index);
        if (!result.validation) {
          return result;
        }
      }
      if (!AddByteCount(candidate_payload_bytes, payload_bytes) ||
          payload_bytes > configuration.maximum_asset_payload_bytes) {
        result.validation = Failure(
            ValidationCode::SIZE_MISMATCH, "assets.payload_bytes",
            "asset payload byte count overflowed or exceeded its bound",
            index);
        return result;
      }
      sorted_assets.push_back(IndexedAssetInput{&input, index});
    }
    std::sort(sorted_assets.begin(), sorted_assets.end(),
              [](const IndexedAssetInput &lhs,
                 const IndexedAssetInput &rhs) {
                if (lhs.input->source_asset_id != rhs.input->source_asset_id) {
                  return lhs.input->source_asset_id <
                         rhs.input->source_asset_id;
                }
                return lhs.original_index < rhs.original_index;
              });
    for (std::size_t index = 1U; index < sorted_assets.size(); ++index) {
      if (sorted_assets[index - 1U].input->source_asset_id ==
          sorted_assets[index].input->source_asset_id) {
        result.validation = Failure(
            ValidationCode::DUPLICATE_IDENTIFIER, "assets.source_asset_id",
            "source asset identity is duplicated",
            sorted_assets[index].original_index);
        return result;
      }
    }

    std::vector<IndexedStaticMeshInput> sorted_objects;
    sorted_objects.reserve(frame.static_meshes.size());
    for (std::size_t index = 0U; index < frame.static_meshes.size(); ++index) {
      const GraphicsSceneStaticMeshInput &object = frame.static_meshes[index];
      if (object.source_object_id == 0U) {
        result.validation = Failure(
            ValidationCode::INVALID_IDENTIFIER,
            "static_meshes.source_object_id",
            "source object identity must be nonzero", index);
        return result;
      }
      if (object.mesh_source_asset_id == 0U ||
          object.material_source_asset_id == 0U) {
        result.validation = Failure(
            ValidationCode::MISSING_REFERENCE, "static_meshes.assets",
            "static object requires mesh and material source identities",
            index);
        return result;
      }
      sorted_objects.push_back(IndexedStaticMeshInput{&object, index});
    }
    std::sort(sorted_objects.begin(), sorted_objects.end(),
              [](const IndexedStaticMeshInput &lhs,
                 const IndexedStaticMeshInput &rhs) {
                if (lhs.input->source_object_id != rhs.input->source_object_id) {
                  return lhs.input->source_object_id <
                         rhs.input->source_object_id;
                }
                return lhs.original_index < rhs.original_index;
              });
    for (std::size_t index = 1U; index < sorted_objects.size(); ++index) {
      if (sorted_objects[index - 1U].input->source_object_id ==
          sorted_objects[index].input->source_object_id) {
        result.validation = Failure(
            ValidationCode::DUPLICATE_IDENTIFIER,
            "static_meshes.source_object_id",
            "source object identity is duplicated",
            sorted_objects[index].original_index);
        return result;
      }
    }

    std::vector<IndexedDynamicMeshInput> sorted_dynamic_objects;
    sorted_dynamic_objects.reserve(frame.dynamic_meshes.size());
    std::uint64_t dynamic_vertex_count = 0U;
    std::uint64_t dynamic_payload_bytes = 0U;
    for (std::size_t index = 0U; index < frame.dynamic_meshes.size(); ++index) {
      const GraphicsSceneDynamicMeshInput &object =
          frame.dynamic_meshes[index];
      if (object.source_object_id == 0U) {
        result.validation = Failure(
            ValidationCode::INVALID_IDENTIFIER,
            "dynamic_meshes.source_object_id",
            "source object identity must be nonzero", index);
        return result;
      }
      if (object.mesh_source_asset_id == 0U ||
          object.material_source_asset_id == 0U) {
        result.validation = Failure(
            ValidationCode::MISSING_REFERENCE, "dynamic_meshes.assets",
            "dynamic object requires mesh and material source identities",
            index);
        return result;
      }
      if (object.state == nullptr) {
        result.validation = Failure(
            ValidationCode::EMPTY_PAYLOAD, "dynamic_meshes.state",
            "dynamic object requires one immutable joined CPU state", index);
        return result;
      }
      if (object.state->positions.size() >
              (std::numeric_limits<std::uint64_t>::max)() -
                  dynamic_vertex_count) {
        result.validation = Failure(
            ValidationCode::SIZE_MISMATCH,
            "dynamic_meshes.state.vertex_count",
            "aggregate dynamic vertex count overflowed", index);
        return result;
      }
      dynamic_vertex_count +=
          static_cast<std::uint64_t>(object.state->positions.size());
      if (dynamic_vertex_count > configuration.maximum_dynamic_vertex_count ||
          !AddElementBytes(object.state->positions.size(), sizeof(Float3),
                           dynamic_payload_bytes) ||
          !AddElementBytes(object.state->normals.size(), sizeof(Float3),
                           dynamic_payload_bytes) ||
          !AddElementBytes(object.state->tangents.size(), sizeof(Float4),
                           dynamic_payload_bytes) ||
          !AddElementBytes(object.state->velocities.size(), sizeof(Float3),
                           dynamic_payload_bytes) ||
          dynamic_payload_bytes >
              configuration.maximum_dynamic_payload_bytes) {
        result.validation = Failure(
            ValidationCode::VALUE_OUT_OF_RANGE,
            dynamic_vertex_count > configuration.maximum_dynamic_vertex_count
                ? "dynamic_meshes.state.vertex_count"
                : "dynamic_meshes.state.payload_bytes",
            "aggregate dynamic staging exceeds the configured frame bound",
            index);
        return result;
      }
      sorted_dynamic_objects.push_back(
          IndexedDynamicMeshInput{&object, index});
    }
    std::sort(sorted_dynamic_objects.begin(), sorted_dynamic_objects.end(),
              [](const IndexedDynamicMeshInput &lhs,
                 const IndexedDynamicMeshInput &rhs) {
                if (lhs.input->source_object_id !=
                    rhs.input->source_object_id) {
                  return lhs.input->source_object_id <
                         rhs.input->source_object_id;
                }
                return lhs.original_index < rhs.original_index;
              });
    for (std::size_t index = 1U; index < sorted_dynamic_objects.size();
         ++index) {
      if (sorted_dynamic_objects[index - 1U].input->source_object_id ==
          sorted_dynamic_objects[index].input->source_object_id) {
        result.validation = Failure(
            ValidationCode::DUPLICATE_IDENTIFIER,
            "dynamic_meshes.source_object_id",
            "source object identity is duplicated",
            sorted_dynamic_objects[index].original_index);
        return result;
      }
    }

    std::vector<IndexedMeshObjectInput> sorted_mesh_objects;
    sorted_mesh_objects.reserve(sorted_objects.size() +
                                sorted_dynamic_objects.size());
    for (const IndexedStaticMeshInput &object : sorted_objects) {
      sorted_mesh_objects.push_back(IndexedMeshObjectInput{
          object.input->source_object_id, object.input, nullptr,
          object.original_index});
    }
    for (const IndexedDynamicMeshInput &object : sorted_dynamic_objects) {
      sorted_mesh_objects.push_back(IndexedMeshObjectInput{
          object.input->source_object_id, nullptr, object.input,
          object.original_index});
    }
    std::sort(sorted_mesh_objects.begin(), sorted_mesh_objects.end(),
              [](const IndexedMeshObjectInput &lhs,
                 const IndexedMeshObjectInput &rhs) {
                if (lhs.source_object_id != rhs.source_object_id) {
                  return lhs.source_object_id < rhs.source_object_id;
                }
                return lhs.dynamic() < rhs.dynamic();
              });
    for (std::size_t index = 1U; index < sorted_mesh_objects.size(); ++index) {
      if (sorted_mesh_objects[index - 1U].source_object_id ==
          sorted_mesh_objects[index].source_object_id) {
        result.validation = Failure(
            ValidationCode::DUPLICATE_IDENTIFIER,
            "mesh_objects.source_object_id",
            "static and dynamic objects share one source identity",
            sorted_mesh_objects[index].original_index);
        return result;
      }
    }

    std::vector<IndexedLightInput> sorted_lights;
    sorted_lights.reserve(frame.lights.size());
    for (std::size_t index = 0U; index < frame.lights.size(); ++index) {
      const GraphicsSceneLightInput &light = frame.lights[index];
      if (light.source_light_id == 0U) {
        result.validation = Failure(
            ValidationCode::INVALID_IDENTIFIER, "lights.source_light_id",
            "source light identity must be nonzero", index);
        return result;
      }
      if (!IsKnownLightType(light.type)) {
        result.validation = Failure(ValidationCode::INVALID_ENUM,
                                    "lights.type", "unknown light type",
                                    index);
        return result;
      }
      sorted_lights.push_back(IndexedLightInput{&light, index});
    }
    std::sort(sorted_lights.begin(), sorted_lights.end(),
              [](const IndexedLightInput &lhs,
                 const IndexedLightInput &rhs) {
                if (lhs.input->source_light_id !=
                    rhs.input->source_light_id) {
                  return lhs.input->source_light_id <
                         rhs.input->source_light_id;
                }
                return lhs.original_index < rhs.original_index;
              });
    for (std::size_t index = 1U; index < sorted_lights.size(); ++index) {
      if (sorted_lights[index - 1U].input->source_light_id ==
          sorted_lights[index].input->source_light_id) {
        result.validation = Failure(
            ValidationCode::DUPLICATE_IDENTIFIER,
            "lights.source_light_id", "source light identity is duplicated",
            sorted_lights[index].original_index);
        return result;
      }
    }

    std::vector<IndexedReflectionProbeInput> sorted_probes;
    sorted_probes.reserve(frame.reflection_probes.size());
    for (std::size_t index = 0U; index < frame.reflection_probes.size();
         ++index) {
      const ReflectionProbeRuntimeDescriptor &probe =
          frame.reflection_probes[index];
      ValidationResult probe_validation =
          ValidateReflectionProbeRuntimeDescriptor(probe);
      if (!probe_validation) {
        probe_validation.field =
            "reflection_probes." + probe_validation.field;
        probe_validation.element_index = index;
        result.validation = std::move(probe_validation);
        return result;
      }
      sorted_probes.push_back(IndexedReflectionProbeInput{&probe, index});
    }
    std::sort(sorted_probes.begin(), sorted_probes.end(),
              [](const IndexedReflectionProbeInput &lhs,
                 const IndexedReflectionProbeInput &rhs) {
                if (lhs.input->probe_id != rhs.input->probe_id) {
                  return lhs.input->probe_id < rhs.input->probe_id;
                }
                return lhs.original_index < rhs.original_index;
              });
    for (std::size_t index = 1U; index < sorted_probes.size(); ++index) {
      if (sorted_probes[index - 1U].input->probe_id ==
          sorted_probes[index].input->probe_id) {
        result.validation = Failure(
            ValidationCode::DUPLICATE_IDENTIFIER,
            "reflection_probes.probe_id",
            "source reflection-probe identity is duplicated",
            sorted_probes[index].original_index);
        return result;
      }
    }

    std::shared_ptr<AssetCatalog> staged_asset_catalog;
    std::shared_ptr<const AssetCatalog> candidate_asset_catalog = asset_catalog;
    std::optional<RenderAssetDelta> asset_delta;
    if (!initialized || !AssetCatalogSourceIdentitiesMatch(sorted_assets)) {
      staged_asset_catalog = std::make_shared<AssetCatalog>(*asset_catalog);
      AssetCatalog &candidate = *staged_asset_catalog;
      const auto &assets = asset_catalog->assets;

      for (const IndexedAssetInput &indexed_input : sorted_assets) {
        const GraphicsSceneAssetInput &input = *indexed_input.input;
        const std::size_t input_index = indexed_input.original_index;
        const RenderAssetKind input_kind =
            RenderAssetPayloadKind(*input.payload);
        auto current = candidate.assets.find(input.source_asset_id);
        if (current != candidate.assets.end()) {
          if (!current->second.live) {
            result.validation = Failure(
                ValidationCode::REVISION_MISMATCH,
                "assets.source_asset_id",
                "a destroyed source asset identity may never be reused",
                input_index);
            return result;
          }
          if (current->second.asset.kind != input_kind) {
            result.validation = Failure(
                ValidationCode::WRONG_ASSET_KIND, "assets.payload",
                "a source asset identity may never change kind", input_index);
            return result;
          }
          continue;
        }
        if (candidate.lifetime_asset_records >=
            configuration.maximum_asset_records) {
          result.validation = Failure(
              ValidationCode::VALUE_OUT_OF_RANGE, "assets",
              "lifetime asset record count exceeds the configured bound",
              input_index);
          return result;
        }
        if (candidate.asset_ordinal_exhausted) {
          result.validation = Failure(
              ValidationCode::VALUE_OUT_OF_RANGE, "asset_id",
              "producer asset identity space is exhausted", input_index);
          return result;
        }
        const RenderAssetId asset_id = RenderAssetId::FromWords(
            configuration.registry_id, candidate.next_asset_ordinal);
        AssetState state;
        state.asset =
            RenderAssetReference::Create(input_kind, asset_id, 1U);
        state.source_identity = input.payload;
        if (input_kind == RenderAssetKind::MATERIAL) {
          state.material_source_payload = input.payload;
        }
        state.material_bindings = input.material_bindings;
        state.live = true;
        candidate.assets.emplace(input.source_asset_id, std::move(state));
        ++candidate.lifetime_asset_records;
        if (candidate.next_asset_ordinal ==
            (std::numeric_limits<std::uint64_t>::max)()) {
          candidate.asset_ordinal_exhausted = true;
        } else {
          ++candidate.next_asset_ordinal;
        }
      }

      std::size_t source_index = 0U;
      for (auto &entry : candidate.assets) {
        while (source_index < sorted_assets.size() &&
               sorted_assets[source_index].input->source_asset_id <
                   entry.first) {
          ++source_index;
        }
        const bool present =
            source_index < sorted_assets.size() &&
            sorted_assets[source_index].input->source_asset_id == entry.first;
        const auto was_live = assets.find(entry.first);
        if (was_live == assets.end() || !was_live->second.live || present) {
          continue;
        }
        if (entry.second.asset.revision ==
            (std::numeric_limits<std::uint64_t>::max)()) {
          result.validation = Failure(
              ValidationCode::REVISION_MISMATCH, "assets.revision",
              "destroyed asset revision would overflow");
          return result;
        }
        ++entry.second.asset.revision;
        entry.second.payload.reset();
        entry.second.source_identity.reset();
        entry.second.material_source_payload.reset();
        entry.second.material_bindings = {};
        entry.second.live = false;
      }

      for (const IndexedAssetInput &indexed_input : sorted_assets) {
        const GraphicsSceneAssetInput &input = *indexed_input.input;
        const std::size_t input_index = indexed_input.original_index;
        if (RenderAssetPayloadKind(*input.payload) ==
            RenderAssetKind::MATERIAL) {
          continue;
        }
        AssetState &state = candidate.assets.at(input.source_asset_id);
        const auto previous = assets.find(input.source_asset_id);
        state.source_identity = input.payload;
        if (previous == assets.end()) {
          state.payload = input.payload;
          continue;
        }
        const std::shared_ptr<const RenderAssetPayload> previous_source =
            previous->second.source_identity.lock();
        if (previous_source == input.payload) {
          state.payload = previous->second.payload;
          continue;
        }
        if (const auto *mesh =
                std::get_if<MeshResourceDescriptor>(input.payload.get())) {
          const auto *previous_mesh =
              previous->second.payload != nullptr
                  ? std::get_if<MeshResourceDescriptor>(
                        previous->second.payload.get())
                  : nullptr;
          if (previous_mesh == nullptr) {
            result.validation = Failure(
                ValidationCode::WRONG_ASSET_KIND, "assets.payload",
                "stored mesh payload has the wrong kind", input_index);
            return result;
          }
          bool contents_changed = true;
          bool payload_equivalent = false;
          if (previous_mesh->topology_revision == mesh->topology_revision) {
            if (!RecordPayloadFallback(
                    *input.payload, result.production.diagnostics)) {
              result.validation = Failure(
                  ValidationCode::SIZE_MISMATCH, "assets.payload_bytes",
                  "fallback comparison byte count overflowed", input_index);
              return result;
            }
            contents_changed =
                !EquivalentMeshResourceContents(*previous_mesh, *mesh);
            payload_equivalent =
                !contents_changed &&
                previous_mesh->debug_name == mesh->debug_name;
          }
          if (payload_equivalent) {
            state.payload = previous->second.payload;
            continue;
          }
          ValidationResult lineage = ValidateMeshRevisionLineage(
              *previous_mesh, *mesh, contents_changed, input_index);
          if (!lineage) {
            result.validation = std::move(lineage);
            return result;
          }
        } else {
          if (!RecordPayloadFallback(
                  *input.payload, result.production.diagnostics)) {
            result.validation = Failure(
                ValidationCode::SIZE_MISMATCH, "assets.payload_bytes",
                "fallback comparison byte count overflowed", input_index);
            return result;
          }
          if (previous->second.payload != nullptr &&
              EquivalentRenderAssetPayload(*previous->second.payload,
                                           *input.payload)) {
            state.payload = previous->second.payload;
            continue;
          }
        }
        if (state.asset.revision ==
            (std::numeric_limits<std::uint64_t>::max)()) {
          result.validation = Failure(
              ValidationCode::REVISION_MISMATCH, "assets.revision",
              "updated asset revision would overflow", input_index);
          return result;
        }
        ++state.asset.revision;
        state.payload = input.payload;
      }

      for (const IndexedAssetInput &indexed_input : sorted_assets) {
        const GraphicsSceneAssetInput &input = *indexed_input.input;
        const std::size_t input_index = indexed_input.original_index;
        if (RenderAssetPayloadKind(*input.payload) !=
            RenderAssetKind::MATERIAL) {
          continue;
        }
        MaterialDescriptor material =
            std::get<MaterialDescriptor>(*input.payload);
        const auto bindings = MaterialBindings(material);
        for (std::size_t slot = 0U; slot < bindings.size(); ++slot) {
          const GraphicsSceneAssetBinding &source_binding =
              input.material_bindings[slot];
          if (BindingIsAbsent(source_binding)) {
            continue;
          }
          const auto texture =
              candidate.assets.find(source_binding.texture_source_asset_id);
          const auto sampler =
              candidate.assets.find(source_binding.sampler_source_asset_id);
          if (texture == candidate.assets.end() || !texture->second.live ||
              sampler == candidate.assets.end() || !sampler->second.live) {
            result.validation = Failure(
                ValidationCode::MISSING_REFERENCE,
                "assets.material_bindings",
                "material dependency is absent or permanently destroyed",
                input_index);
            return result;
          }
          if (texture->second.asset.kind != RenderAssetKind::TEXTURE ||
              sampler->second.asset.kind != RenderAssetKind::SAMPLER) {
            result.validation = Failure(
                ValidationCode::WRONG_ASSET_KIND,
                "assets.material_bindings",
                "material dependency source identity has the wrong kind",
                input_index);
            return result;
          }
          bindings[slot]->texture = texture->second.asset;
          bindings[slot]->sampler = sampler->second.asset;
        }
        ValidationResult validation = ValidateMaterialDescriptor(material);
        if (!validation) {
          result.validation = AtAsset(std::move(validation), input_index);
          return result;
        }
        AssetState &state = candidate.assets.at(input.source_asset_id);
        const RenderAssetPayload canonical_payload{std::move(material)};
        const auto previous = assets.find(input.source_asset_id);
        const AssetState *previous_state =
            previous == assets.end() ? nullptr : &previous->second;
        state.source_identity = input.payload;
        state.material_source_payload = input.payload;
        state.material_bindings = input.material_bindings;
        if (previous == assets.end()) {
          state.payload =
              std::make_shared<const RenderAssetPayload>(canonical_payload);
          continue;
        }
        bool source_payload_equivalent = true;
        const std::shared_ptr<const RenderAssetPayload> previous_source =
            previous_state->source_identity.lock();
        if (previous_source != input.payload) {
          if (!RecordPayloadFallback(
                  *input.payload, result.production.diagnostics)) {
            result.validation = Failure(
                ValidationCode::SIZE_MISMATCH, "assets.payload_bytes",
                "fallback comparison byte count overflowed", input_index);
            return result;
          }
          if (previous_state->material_source_payload == nullptr) {
            result.validation = Failure(
                ValidationCode::EMPTY_PAYLOAD, "assets.material_source",
                "stored material source descriptor is absent", input_index);
            return result;
          }
          source_payload_equivalent = EquivalentRenderAssetPayload(
              *previous_state->material_source_payload, *input.payload);
        }
        if (source_payload_equivalent && previous->second.payload != nullptr &&
            EquivalentRenderAssetPayload(*previous->second.payload,
                                         canonical_payload)) {
          state.payload = previous->second.payload;
          continue;
        }
        if (state.asset.revision ==
            (std::numeric_limits<std::uint64_t>::max)()) {
          result.validation = Failure(
              ValidationCode::REVISION_MISMATCH, "assets.revision",
              "updated material revision would overflow", input_index);
          return result;
        }
        ++state.asset.revision;
        state.payload =
            std::make_shared<const RenderAssetPayload>(canonical_payload);
      }

      std::vector<RenderAssetMutation> mutations;
      mutations.reserve(candidate.assets.size());
      for (const auto &entry : candidate.assets) {
        const auto previous = assets.find(entry.first);
        const bool newly_created = previous == assets.end();
        const bool changed =
            newly_created || previous->second.asset != entry.second.asset ||
            previous->second.live != entry.second.live;
        if (!changed) {
          continue;
        }
        RenderAssetMutation mutation;
        mutation.type = entry.second.live ? RenderAssetMutationType::UPSERT
                                          : RenderAssetMutationType::DESTROY;
        mutation.asset = entry.second.asset;
        mutation.payload =
            entry.second.live && entry.second.payload != nullptr
                ? *entry.second.payload
                : RenderAssetPayload{};
        mutations.push_back(std::move(mutation));
      }
      std::sort(mutations.begin(), mutations.end(),
                [](const RenderAssetMutation &lhs,
                   const RenderAssetMutation &rhs) {
                  return lhs.asset.id < rhs.asset.id;
                });

      if (!initialized || !mutations.empty()) {
        if (candidate.registry.sequence() ==
            (std::numeric_limits<std::uint64_t>::max)()) {
          result.validation = Failure(
              ValidationCode::SEQUENCE_MISMATCH, "asset_sequence",
              "asset transaction sequence is exhausted");
          return result;
        }
        RenderAssetDelta delta;
        delta.registry_id = configuration.registry_id;
        delta.full_snapshot = !initialized;
        delta.base_sequence = initialized ? candidate.registry.sequence() : 0U;
        delta.sequence = initialized ? candidate.registry.sequence() + 1U
                                     : kFirstAssetSequence;
        delta.mutations = std::move(mutations);
        ValidationResult validation = candidate.registry.Apply(delta);
        if (!validation) {
          result.validation = std::move(validation);
          return result;
        }
        // The registry already owns immutable copies of every changed payload.
        // Alias those owners so producer state and catalog do not retain two
        // complete mesh/texture copies after the transaction commits.
        for (auto &entry : candidate.assets) {
          if (!entry.second.live) {
            entry.second.payload.reset();
            continue;
          }
          const RenderAssetRecord *record =
              candidate.registry.Find(entry.second.asset.id);
          if (record == nullptr || !record->live() ||
              record->asset != entry.second.asset) {
            result.validation = Failure(
                ValidationCode::MISSING_REFERENCE, "assets.registry",
                "applied asset transaction could not be resolved");
            return result;
          }
          entry.second.payload = record->payload;
        }
        asset_delta = std::move(delta);
      }
      candidate_asset_catalog = staged_asset_catalog;
    }

    const AssetCatalog &candidate_catalog = *candidate_asset_catalog;
    SceneEnvironmentDescriptor environment = frame.environment;
    if (frame.environment_texture_source_asset_id != 0U) {
      const auto texture = candidate_catalog.assets.find(
          frame.environment_texture_source_asset_id);
      const auto sampler = candidate_catalog.assets.find(
          frame.environment_sampler_source_asset_id);
      if (texture == candidate_catalog.assets.end() ||
          !texture->second.live ||
          sampler == candidate_catalog.assets.end() ||
          !sampler->second.live) {
        result.validation = Failure(
            ValidationCode::MISSING_REFERENCE, "environment",
            "environment dependency is absent or permanently destroyed");
        return result;
      }
      if (texture->second.asset.kind != RenderAssetKind::TEXTURE ||
          sampler->second.asset.kind != RenderAssetKind::SAMPLER) {
        result.validation = Failure(
            ValidationCode::WRONG_ASSET_KIND, "environment",
            "environment dependency source identity has the wrong kind");
        return result;
      }
      environment.environment_texture = texture->second.asset;
      environment.environment_sampler = sampler->second.asset;
    }
    const ValidatedEnvironmentAssets candidate_environment_assets{
        environment.environment_texture, environment.environment_sampler};

    SceneSnapshotDescriptor descriptor;
    descriptor.snapshot_id = next_snapshot_id;
    descriptor.asset_registry_id = candidate_catalog.registry.registry_id();
    descriptor.asset_sequence = candidate_catalog.registry.sequence();
    descriptor.simulation_tick = frame.simulation_tick;
    descriptor.simulation_time_seconds = frame.simulation_time_seconds;
    descriptor.absolute_world_origin_meters =
        frame.absolute_world_origin_meters;
    descriptor.environment = std::move(environment);

    descriptor.lights.reserve(sorted_lights.size());
    std::size_t light_scan = 0U;
    std::size_t new_light_count = 0U;
    for (const IndexedLightInput &indexed_input : sorted_lights) {
      const std::uint64_t source_light_id =
          indexed_input.input->source_light_id;
      while (light_scan < lights.size() &&
             lights[light_scan].source_light_id < source_light_id) {
        ++light_scan;
      }
      if (light_scan < lights.size() &&
          lights[light_scan].source_light_id == source_light_id) {
        ++light_scan;
      } else {
        ++new_light_count;
      }
    }
    if (new_light_count >
        (std::numeric_limits<std::size_t>::max)() - lights.size()) {
      result.validation = Failure(
          ValidationCode::SIZE_MISMATCH, "lights",
          "staged light-history capacity would overflow");
      return result;
    }
    if (lights.size() > configuration.maximum_light_records ||
        new_light_count > configuration.maximum_light_records - lights.size()) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights",
          "lifetime light record count exceeds the configured bound");
      return result;
    }

    std::vector<LightState> candidate_lights;
    candidate_lights.reserve(lights.size() + new_light_count);
    std::size_t prior_light_index = 0U;
    for (const IndexedLightInput &indexed_input : sorted_lights) {
      const GraphicsSceneLightInput &input = *indexed_input.input;
      const std::size_t input_index = indexed_input.original_index;
      while (prior_light_index < lights.size() &&
             lights[prior_light_index].source_light_id <
                 input.source_light_id) {
        LightState removed = lights[prior_light_index];
        removed.live = false;
        candidate_lights.push_back(std::move(removed));
        ++prior_light_index;
      }
      const LightState *prior_light =
          prior_light_index < lights.size() &&
                  lights[prior_light_index].source_light_id ==
                      input.source_light_id
              ? &lights[prior_light_index]
              : nullptr;
      if (prior_light != nullptr && !prior_light->live) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH, "lights.source_light_id",
            "a destroyed source light identity may never be reused",
            input_index);
        return result;
      }
      if (prior_light != nullptr && prior_light->type != input.type) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH, "lights.type",
            "a source light identity may never change type", input_index);
        return result;
      }
      if (prior_light != nullptr) {
        ++prior_light_index;
      }

      LightDescriptor light;
      light.light_id = input.source_light_id;
      light.type = input.type;
      light.color_linear = input.color_linear;
      light.intensity = input.intensity;
      light.position = input.position;
      light.direction = input.direction;
      light.range = input.range;
      light.inner_cone_radians = input.inner_cone_radians;
      light.outer_cone_radians = input.outer_cone_radians;
      light.shadow_flags = input.shadow_flags;
      if (prior_light != nullptr) {
        if (input.type == LightType::DIRECTIONAL) {
          light.previous_position = prior_light->position;
        } else if (!RebasePreviousPosition(
                       prior_light->position,
                       last_absolute_world_origin_meters,
                       frame.absolute_world_origin_meters,
                       light.previous_position)) {
          result.validation = Failure(
              ValidationCode::VALUE_OUT_OF_RANGE,
              "lights.previous_position",
              "previous local-light position could not be rebased into this "
              "origin",
              input_index);
          return result;
        }
        light.previous_direction = prior_light->direction;
      } else {
        light.previous_position = input.position;
        light.previous_direction = input.direction;
      }
      descriptor.lights.push_back(light);
      candidate_lights.push_back(LightState{input.source_light_id, input.type,
                                            input.position, input.direction,
                                            true});
    }
    while (prior_light_index < lights.size()) {
      LightState removed = lights[prior_light_index];
      removed.live = false;
      candidate_lights.push_back(std::move(removed));
      ++prior_light_index;
    }

    descriptor.reflection_probes.reserve(sorted_probes.size());
    std::size_t probe_scan = 0U;
    std::size_t new_probe_count = 0U;
    for (const IndexedReflectionProbeInput &indexed_input : sorted_probes) {
      const std::uint64_t probe_id = indexed_input.input->probe_id;
      while (probe_scan < reflection_probes.size() &&
             reflection_probes[probe_scan].probe_id < probe_id) {
        ++probe_scan;
      }
      if (probe_scan < reflection_probes.size() &&
          reflection_probes[probe_scan].probe_id == probe_id) {
        ++probe_scan;
      } else {
        ++new_probe_count;
      }
    }
    if (new_probe_count >
        (std::numeric_limits<std::size_t>::max)() -
            reflection_probes.size()) {
      result.validation = Failure(
          ValidationCode::SIZE_MISMATCH, "reflection_probes",
          "staged reflection-probe lineage capacity would overflow");
      return result;
    }
    if (reflection_probes.size() >
            configuration.maximum_reflection_probe_records ||
        new_probe_count >
            configuration.maximum_reflection_probe_records -
                reflection_probes.size()) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "reflection_probes",
          "lifetime reflection-probe record count exceeds the configured bound");
      return result;
    }

    std::vector<ReflectionProbeState> candidate_reflection_probes;
    candidate_reflection_probes.reserve(reflection_probes.size() +
                                        new_probe_count);
    std::size_t prior_probe_index = 0U;
    for (const IndexedReflectionProbeInput &indexed_input : sorted_probes) {
      const ReflectionProbeRuntimeDescriptor &input = *indexed_input.input;
      const std::size_t input_index = indexed_input.original_index;
      while (prior_probe_index < reflection_probes.size() &&
             reflection_probes[prior_probe_index].probe_id < input.probe_id) {
        ReflectionProbeState removed =
            reflection_probes[prior_probe_index];
        removed.live = false;
        candidate_reflection_probes.push_back(std::move(removed));
        ++prior_probe_index;
      }
      const ReflectionProbeState *prior_probe =
          prior_probe_index < reflection_probes.size() &&
                  reflection_probes[prior_probe_index].probe_id ==
                      input.probe_id
              ? &reflection_probes[prior_probe_index]
              : nullptr;
      if (prior_probe != nullptr && !prior_probe->live) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH,
            "reflection_probes.probe_id",
            "a destroyed reflection-probe identity may never be reused",
            input_index);
        return result;
      }
      if (prior_probe != nullptr &&
          input.content_revision < prior_probe->content_revision) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH,
            "reflection_probes.content_revision",
            "reflection-probe content revision moved backwards", input_index);
        return result;
      }
      if (prior_probe != nullptr &&
          input.content_revision == prior_probe->content_revision &&
          !AreReflectionProbeRuntimeDescriptorsEquivalent(
              input, prior_probe->descriptor)) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH,
            "reflection_probes.content_revision",
            "changed reflection-probe contents require a newer revision",
            input_index);
        return result;
      }
      if (prior_probe != nullptr) {
        ++prior_probe_index;
      }
      descriptor.reflection_probes.push_back(input);
      candidate_reflection_probes.push_back(ReflectionProbeState{
          input.probe_id, input.content_revision, input, true});
    }
    while (prior_probe_index < reflection_probes.size()) {
      ReflectionProbeState removed = reflection_probes[prior_probe_index];
      removed.live = false;
      candidate_reflection_probes.push_back(std::move(removed));
      ++prior_probe_index;
    }

    descriptor.mesh_instances.reserve(sorted_mesh_objects.size());
    descriptor.dynamic_mesh_updates.reserve(sorted_dynamic_objects.size());
    std::vector<ValidatedMeshAssetPair> candidate_mesh_asset_pairs;
    candidate_mesh_asset_pairs.reserve(sorted_mesh_objects.size());

    std::size_t object_scan = 0U;
    std::size_t new_static_object_count = 0U;
    std::size_t new_dynamic_object_count = 0U;
    for (const IndexedMeshObjectInput &indexed_input : sorted_mesh_objects) {
      while (object_scan < objects.size() &&
             objects[object_scan].source_object_id <
                 indexed_input.source_object_id) {
        ++object_scan;
      }
      if (object_scan < objects.size() &&
          objects[object_scan].source_object_id ==
              indexed_input.source_object_id) {
        ++object_scan;
      } else if (indexed_input.dynamic()) {
        ++new_dynamic_object_count;
      } else {
        ++new_static_object_count;
      }
    }
    const std::size_t prior_static_object_count =
        static_cast<std::size_t>(std::count_if(
            objects.begin(), objects.end(),
            [](const ObjectState &object) { return !object.dynamic; }));
    const std::size_t prior_dynamic_object_count =
        objects.size() - prior_static_object_count;
    if (new_static_object_count >
            (std::numeric_limits<std::size_t>::max)() -
                prior_static_object_count ||
        new_dynamic_object_count >
            (std::numeric_limits<std::size_t>::max)() -
                prior_dynamic_object_count) {
      result.validation = Failure(
          ValidationCode::SIZE_MISMATCH, "mesh_objects",
          "staged object-history capacity would overflow");
      return result;
    }
    if (prior_static_object_count >
            configuration.maximum_static_mesh_objects ||
        new_static_object_count >
            configuration.maximum_static_mesh_objects -
                prior_static_object_count) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "static_meshes",
          "lifetime static object record count exceeds the configured bound");
      return result;
    }
    if (prior_dynamic_object_count >
            configuration.maximum_dynamic_mesh_objects ||
        new_dynamic_object_count >
            configuration.maximum_dynamic_mesh_objects -
                prior_dynamic_object_count) {
      result.validation = Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "dynamic_meshes",
          "lifetime dynamic object record count exceeds the configured bound");
      return result;
    }
    if (new_static_object_count >
            (std::numeric_limits<std::size_t>::max)() -
                new_dynamic_object_count ||
        new_static_object_count + new_dynamic_object_count >
            (std::numeric_limits<std::size_t>::max)() - objects.size()) {
      result.validation = Failure(
          ValidationCode::SIZE_MISMATCH, "mesh_objects",
          "combined object-history capacity would overflow");
      return result;
    }

    std::vector<ObjectState> candidate_objects;
    candidate_objects.reserve(objects.size() + new_static_object_count +
                              new_dynamic_object_count);
    std::uint64_t candidate_next_dynamic_update_sequence =
        next_dynamic_update_sequence;
    bool candidate_dynamic_update_sequence_exhausted =
        dynamic_update_sequence_exhausted;
    std::size_t prior_index = 0U;
    for (const IndexedMeshObjectInput &indexed_input : sorted_mesh_objects) {
      const bool dynamic = indexed_input.dynamic();
      const GraphicsSceneStaticMeshInput *const static_input =
          indexed_input.static_input;
      const GraphicsSceneDynamicMeshInput *const dynamic_input =
          indexed_input.dynamic_input;
      const std::uint64_t source_object_id =
          indexed_input.source_object_id;
      const std::uint64_t mesh_source_asset_id =
          dynamic ? dynamic_input->mesh_source_asset_id
                  : static_input->mesh_source_asset_id;
      const std::uint64_t material_source_asset_id =
          dynamic ? dynamic_input->material_source_asset_id
                  : static_input->material_source_asset_id;
      const Matrix4x4 &render_from_object =
          dynamic ? dynamic_input->render_from_object
                  : static_input->render_from_object;
      const std::uint32_t visibility_mask =
          dynamic ? dynamic_input->visibility_mask
                  : static_input->visibility_mask;
      const std::uint32_t flags =
          dynamic ? dynamic_input->flags : static_input->flags;
      const std::size_t input_index = indexed_input.original_index;

      while (prior_index < objects.size() &&
             objects[prior_index].source_object_id < source_object_id) {
        ObjectState removed = objects[prior_index];
        removed.live = false;
        candidate_objects.push_back(std::move(removed));
        ++prior_index;
      }
      const ObjectState *prior_object =
          prior_index < objects.size() &&
                  objects[prior_index].source_object_id == source_object_id
              ? &objects[prior_index]
              : nullptr;
      if (prior_object != nullptr && !prior_object->live) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH,
            dynamic ? "dynamic_meshes.source_object_id"
                    : "static_meshes.source_object_id",
            "a destroyed source object identity may never be reused",
            input_index);
        return result;
      }
      if (prior_object != nullptr && prior_object->dynamic != dynamic) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH,
            "mesh_objects.source_object_id",
            "a source object identity may never change static/dynamic kind",
            input_index);
        return result;
      }
      if (prior_object != nullptr && dynamic &&
          (prior_object->mesh_source_asset_id != mesh_source_asset_id ||
           prior_object->material_source_asset_id !=
               material_source_asset_id)) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH,
            dynamic ? "dynamic_meshes.assets" : "static_meshes.assets",
            "a live source object may not change its mesh/material identity",
            input_index);
        return result;
      }
      if (prior_object != nullptr) {
        ++prior_index;
      }

      const auto mesh_asset =
          candidate_catalog.assets.find(mesh_source_asset_id);
      const auto material_asset =
          candidate_catalog.assets.find(material_source_asset_id);
      if (mesh_asset == candidate_catalog.assets.end() ||
          !mesh_asset->second.live ||
          material_asset == candidate_catalog.assets.end() ||
          !material_asset->second.live) {
        result.validation = Failure(
            ValidationCode::MISSING_REFERENCE,
            dynamic ? "dynamic_meshes.assets" : "static_meshes.assets",
            "mesh object references an absent or destroyed source asset",
            input_index);
        return result;
      }
      if (mesh_asset->second.asset.kind != RenderAssetKind::MESH ||
          material_asset->second.asset.kind != RenderAssetKind::MATERIAL) {
        result.validation = Failure(
            ValidationCode::WRONG_ASSET_KIND,
            dynamic ? "dynamic_meshes.assets" : "static_meshes.assets",
            "mesh object source asset has the wrong kind", input_index);
        return result;
      }
      const auto *mesh =
          mesh_asset->second.payload != nullptr
              ? std::get_if<MeshResourceDescriptor>(
                    mesh_asset->second.payload.get())
              : nullptr;
      if (mesh == nullptr || mesh->dynamic != dynamic) {
        result.validation = Failure(
            mesh == nullptr ? ValidationCode::WRONG_ASSET_KIND
                            : ValidationCode::REVISION_MISMATCH,
            dynamic ? "dynamic_meshes.mesh" : "static_meshes.mesh",
            dynamic
                ? "resolved dynamic object requires a dynamic mesh allocation"
                : "resolved static object requires a static mesh allocation",
            input_index);
        return result;
      }

      MeshInstanceDescriptor instance;
      instance.instance_id = source_object_id;
      instance.mesh = mesh_asset->second.asset;
      instance.material = material_asset->second.asset;
      instance.topology_revision = mesh->topology_revision;
      instance.render_from_object = render_from_object;
      instance.visibility_mask = visibility_mask;
      instance.flags = flags;
      std::shared_ptr<const GraphicsSceneDynamicMeshState>
          committed_deformation;
      if (dynamic) {
        const GraphicsSceneDynamicMeshState &state = *dynamic_input->state;
        if (state.topology_revision != mesh->topology_revision) {
          result.validation = Failure(
              ValidationCode::REVISION_MISMATCH,
              "dynamic_meshes.state.topology_revision",
              "joined deformation topology differs from its base mesh",
              input_index);
          return result;
        }
        if (state.deformation_revision <= 1U) {
          result.validation = Failure(
              ValidationCode::VALUE_OUT_OF_RANGE,
              "dynamic_meshes.state.deformation_revision",
              "live joined deformation revisions begin at two", input_index);
          return result;
        }
        if (!HasExactTightBounds(state)) {
          result.validation = Failure(
              ValidationCode::INVALID_BOUNDS,
              "dynamic_meshes.state.updated_local_bounds",
              "joined deformation bounds must be the exact tight position "
              "bounds",
              input_index);
          return result;
        }
        if (prior_object == nullptr) {
          if (state.deformation_revision != 2U) {
            result.validation = Failure(
                ValidationCode::REVISION_MISMATCH,
                "dynamic_meshes.state.deformation_revision",
                "a new deformable object must begin at revision two",
                input_index);
            return result;
          }
          committed_deformation = dynamic_input->state;
        } else {
          if (prior_object->deformation == nullptr) {
            result.validation = Failure(
                ValidationCode::MISSING_REFERENCE,
                "dynamic_meshes.state",
                "stored deformable object has no prior immutable state",
                input_index);
            return result;
          }
          const bool equivalent =
              prior_object->deformation == dynamic_input->state ||
              EquivalentDynamicMeshStateContents(
                  *prior_object->deformation, state);
          const std::uint64_t prior_revision =
              prior_object->deformation->deformation_revision;
          if ((equivalent && state.deformation_revision != prior_revision) ||
              (!equivalent &&
               (prior_revision ==
                    (std::numeric_limits<std::uint64_t>::max)() ||
                state.deformation_revision != prior_revision + 1U))) {
            result.validation = Failure(
                ValidationCode::REVISION_MISMATCH,
                "dynamic_meshes.state.deformation_revision",
                equivalent
                    ? "unchanged deformable contents changed revision"
                    : "changed deformable contents require the next revision",
                input_index);
            return result;
          }
          committed_deformation =
              equivalent ? prior_object->deformation : dynamic_input->state;
        }
        if (candidate_dynamic_update_sequence_exhausted) {
          result.validation = Failure(
              ValidationCode::SEQUENCE_MISMATCH,
              "dynamic_mesh_updates.update_sequence",
              "dynamic update sequence is exhausted", input_index);
          return result;
        }
        DynamicMeshUpdateDescriptor update;
        update.update_sequence = candidate_next_dynamic_update_sequence;
        update.instance_id = source_object_id;
        update.mesh = instance.mesh;
        update.topology_revision = state.topology_revision;
        update.deformation_revision = state.deformation_revision;
        update.positions = state.positions;
        update.normals = state.normals;
        update.tangents = state.tangents;
        update.velocities = state.velocities;
        update.has_updated_bounds = true;
        update.updated_local_bounds = state.updated_local_bounds;
        descriptor.dynamic_mesh_updates.push_back(std::move(update));
        instance.deformation_revision = state.deformation_revision;
        instance.local_bounds = state.updated_local_bounds;
        if (candidate_next_dynamic_update_sequence ==
            (std::numeric_limits<std::uint64_t>::max)()) {
          candidate_dynamic_update_sequence_exhausted = true;
        } else {
          ++candidate_next_dynamic_update_sequence;
        }
      } else {
        instance.deformation_revision = 1U;
        instance.local_bounds = mesh->local_bounds;
      }

      if (prior_object != nullptr) {
        if (!RebasePreviousObjectTransform(
                prior_object->render_from_object,
                last_absolute_world_origin_meters,
                frame.absolute_world_origin_meters,
                instance.previous_render_from_object)) {
          result.validation = Failure(
              ValidationCode::VALUE_OUT_OF_RANGE,
              dynamic ? "dynamic_meshes.previous_transform"
                      : "static_meshes.previous_transform",
              "previous transform could not be rebased into this origin",
              input_index);
          return result;
        }
      } else {
        instance.previous_render_from_object = render_from_object;
      }
      descriptor.mesh_instances.push_back(instance);
      candidate_mesh_asset_pairs.push_back(
          ValidatedMeshAssetPair{instance.mesh, instance.material});
      ObjectState candidate_object;
      candidate_object.source_object_id = source_object_id;
      candidate_object.render_from_object = render_from_object;
      candidate_object.mesh_source_asset_id = mesh_source_asset_id;
      candidate_object.material_source_asset_id = material_source_asset_id;
      candidate_object.deformation = std::move(committed_deformation);
      candidate_object.dynamic = dynamic;
      candidate_object.live = true;
      candidate_objects.push_back(std::move(candidate_object));
    }
    while (prior_index < objects.size()) {
      ObjectState removed = objects[prior_index];
      removed.live = false;
      candidate_objects.push_back(std::move(removed));
      ++prior_index;
    }
    std::sort(candidate_mesh_asset_pairs.begin(),
              candidate_mesh_asset_pairs.end(),
              [](const ValidatedMeshAssetPair &lhs,
                 const ValidatedMeshAssetPair &rhs) {
                if (lhs.mesh.id != rhs.mesh.id) {
                  return lhs.mesh.id < rhs.mesh.id;
                }
                if (lhs.mesh.kind != rhs.mesh.kind) {
                  return static_cast<std::uint8_t>(lhs.mesh.kind) <
                         static_cast<std::uint8_t>(rhs.mesh.kind);
                }
                if (lhs.mesh.revision != rhs.mesh.revision) {
                  return lhs.mesh.revision < rhs.mesh.revision;
                }
                if (lhs.material.id != rhs.material.id) {
                  return lhs.material.id < rhs.material.id;
                }
                if (lhs.material.kind != rhs.material.kind) {
                  return static_cast<std::uint8_t>(lhs.material.kind) <
                         static_cast<std::uint8_t>(rhs.material.kind);
                }
                return lhs.material.revision < rhs.material.revision;
              });
    candidate_mesh_asset_pairs.erase(
        std::unique(candidate_mesh_asset_pairs.begin(),
                    candidate_mesh_asset_pairs.end()),
        candidate_mesh_asset_pairs.end());

    const SceneSnapshotCreateResult created =
        CreateSceneSnapshot(std::move(descriptor));
    if (!created) {
      result.validation = RemapSceneElementIndex(
          created.validation, sorted_mesh_objects, sorted_dynamic_objects,
          sorted_lights, sorted_probes);
      return result;
    }
    const bool requires_full_asset_compatibility_validation =
        !asset_compatibility_cache_initialized ||
        candidate_mesh_asset_pairs != validated_mesh_asset_pairs ||
        !sorted_dynamic_objects.empty() ||
        !(candidate_environment_assets == validated_environment_assets);
    if (requires_full_asset_compatibility_validation) {
      result.production.diagnostics
          .scene_asset_compatibility_full_validations = 1U;
      ValidationResult validation =
          ValidateSceneSnapshotAssets(*created.snapshot,
                                      candidate_catalog.registry);
      if (!validation) {
        result.validation = RemapSceneElementIndex(
            std::move(validation), sorted_mesh_objects,
            sorted_dynamic_objects, sorted_lights, sorted_probes);
        return result;
      }
    }

    CameraViewRequest camera;
    camera.view_id = frame.camera.view_id;
    camera.width = frame.camera.width;
    camera.height = frame.camera.height;
    camera.view_from_render = frame.camera.view_from_render;
    camera.clip_from_view = frame.camera.clip_from_view;
    camera.temporal_jitter_pixels = frame.camera.temporal_jitter_pixels;
    camera.near_plane = frame.camera.near_plane;
    camera.far_plane = frame.camera.far_plane;
    camera.exposure = frame.camera.exposure;
    camera.visibility_mask = frame.camera.visibility_mask;
    if (camera_state.initialized) {
      if (camera_state.camera.view_id != frame.camera.view_id) {
        result.validation = Failure(
            ValidationCode::REVISION_MISMATCH, "camera.view_id",
            "the bounded main-camera identity may never change");
        return result;
      }
      if (!RebasePreviousViewTransform(
              camera_state.camera.view_from_render,
              camera_state.absolute_world_origin_meters,
              frame.absolute_world_origin_meters,
              camera.previous_view_from_render)) {
        result.validation = Failure(
            ValidationCode::VALUE_OUT_OF_RANGE,
            "camera.previous_view_from_render",
            "previous camera could not be rebased into this origin");
        return result;
      }
      camera.previous_clip_from_view =
          camera_state.camera.near_plane == frame.camera.near_plane &&
                  camera_state.camera.far_plane == frame.camera.far_plane
              ? camera_state.camera.clip_from_view
              : frame.camera.clip_from_view;
    } else {
      camera.previous_view_from_render = frame.camera.view_from_render;
      camera.previous_clip_from_view = frame.camera.clip_from_view;
    }

    RenderFrameRequest camera_validation_request;
    camera_validation_request.frame_id = created.snapshot->snapshot_id();
    camera_validation_request.scene_snapshot = created.snapshot;
    camera_validation_request.views.push_back(camera);
    camera_validation_request.requested_outputs = FrameOutputMask::COLOR;
    camera_validation_request.color_format = PixelFormat::RGBA8_SRGB;
    camera_validation_request.present = false;
    ValidationResult validation =
        ValidateRenderFrameRequest(camera_validation_request);
    if (!validation) {
      result.validation = std::move(validation);
      return result;
    }

    // Allocate the only remaining fallible finalization owner before the
    // particle source commits its replay-protected durable lifecycle. After
    // Capture() succeeds, publication below consists only of nonthrowing
    // clears, scalar assignments, and moves.
    if (finalize_scene_generation && staged_asset_catalog == nullptr) {
      staged_asset_catalog =
          std::make_shared<AssetCatalog>(*candidate_asset_catalog);
      candidate_asset_catalog = staged_asset_catalog;
    }

    std::shared_ptr<Ogre14ParticleCapturedFrame> captured_particles;
    if (frame.continuous_particles.has_value()) {
      const Ogre14JoinedParticleSourceFrame &source_particles =
          *frame.continuous_particles;
      if (source_particles.simulation_tick != frame.simulation_tick ||
          source_particles.simulation_time_seconds !=
              frame.simulation_time_seconds ||
          source_particles.absolute_world_origin_meters !=
              frame.absolute_world_origin_meters) {
        result.validation = Failure(
            ValidationCode::SEQUENCE_MISMATCH,
            "continuous_particles.simulation_state",
            "continuous particles must come from the exact joined scene boundary");
        return result;
      }
      Ogre14JoinedParticleFrame joined;
      joined.source_sequence = source_particles.source_sequence;
      joined.material_catalog_registry_id =
          candidate_catalog.registry.registry_id();
      joined.material_catalog_sequence = candidate_catalog.registry.sequence();
      joined.simulation_tick = source_particles.simulation_tick;
      joined.simulation_time_seconds =
          source_particles.simulation_time_seconds;
      joined.absolute_world_origin_meters =
          source_particles.absolute_world_origin_meters;
      joined.joined_buffer_epoch = source_particles.joined_buffer_epoch;
      joined.post_physics_epoch = source_particles.post_physics_epoch;
      joined.complete_inventory = source_particles.complete_inventory;
      joined.events = source_particles.events;
      joined.systems.reserve(source_particles.systems.size());
      for (std::size_t index = 0U;
           index < source_particles.systems.size(); ++index) {
        const Ogre14ParticleSourceSystemCapture &source_system =
            source_particles.systems[index];
        const Ogre14ParticleMaterialSourceClosure &source_closure =
            source_system.material_closure;
        const auto material = candidate_catalog.assets.find(
            source_closure.material_source_asset_id);
        const auto texture = candidate_catalog.assets.find(
            source_closure.texture_source_asset_id);
        const auto sampler = candidate_catalog.assets.find(
            source_closure.sampler_source_asset_id);
        if (material == candidate_catalog.assets.end() ||
            texture == candidate_catalog.assets.end() ||
            sampler == candidate_catalog.assets.end() ||
            !material->second.live || !texture->second.live ||
            !sampler->second.live ||
            material->second.asset.kind != RenderAssetKind::MATERIAL ||
            texture->second.asset.kind != RenderAssetKind::TEXTURE ||
            sampler->second.asset.kind != RenderAssetKind::SAMPLER) {
          result.validation = Failure(
              ValidationCode::MISSING_REFERENCE,
              "continuous_particles.material_closure",
              "particle material, source texture, or sampler is absent from the candidate catalog",
              index);
          return result;
        }
        const GraphicsSceneAssetBinding &base_binding =
            material->second.material_bindings[static_cast<std::size_t>(
                MaterialTextureSlot::BASE_COLOR)];
        if (base_binding.texture_source_asset_id !=
                source_closure.texture_source_asset_id ||
            base_binding.sampler_source_asset_id !=
                source_closure.sampler_source_asset_id) {
          result.validation = Failure(
              ValidationCode::REVISION_MISMATCH,
              "continuous_particles.material_closure",
              "particle source texture and sampler differ from the exact material binding",
              index);
          return result;
        }
        Ogre14ParticleSystemCapture system;
        system.system_id = source_system.system_id;
        system.effect = source_system.effect;
        system.billboard_mode = source_system.billboard_mode;
        system.billboard_rotation_mode =
            source_system.billboard_rotation_mode;
        system.particles_are_world_space =
            source_system.particles_are_world_space;
        system.requires_frontend_emitter_evaluation =
            source_system.requires_frontend_emitter_evaluation;
        system.requires_frontend_affector_evaluation =
            source_system.requires_frontend_affector_evaluation;
        system.requires_frontend_sorting =
            source_system.requires_frontend_sorting;
        system.requires_texture_animation =
            source_system.requires_texture_animation;
        system.system_visible = source_system.system_visible;
        system.parent_visible = source_system.parent_visible;
        system.emitting = source_system.emitting;
        system.particles = source_system.particles;
        system.material_closure.material_catalog_registry_id =
            joined.material_catalog_registry_id;
        system.material_closure.material_catalog_sequence =
            joined.material_catalog_sequence;
        system.material_closure.material = material->second.asset;
        system.material_closure.source_texture = texture->second.asset;
        system.material_closure.sampler = sampler->second.asset;
        system.material_closure.translation_source_sequence =
            source_particles.source_sequence;
        system.material_closure.blend = source_closure.blend;
        system.material_closure.alpha_reject = source_closure.alpha_reject;
        system.material_closure.alpha_reject_threshold =
            source_closure.alpha_reject_threshold;
        system.material_closure.sort_policy = source_closure.sort_policy;
        system.material_closure.depth_check = source_closure.depth_check;
        system.material_closure.depth_write = source_closure.depth_write;
        system.material_closure.lighting_enabled =
            source_closure.lighting_enabled;
        system.material_closure.receives_shadows =
            source_closure.receives_shadows;
        system.material_closure.casts_shadows =
            source_closure.casts_shadows;
        system.material_closure.vertex_color_modulation =
            source_closure.vertex_color_modulation;
        system.material_closure.source_backed_texture =
            source_closure.source_backed_texture;
        system.material_closure.gpu_readback_used =
            source_closure.gpu_readback_used;
        joined.systems.push_back(std::move(system));
      }
      captured_particles = std::make_shared<Ogre14ParticleCapturedFrame>();
      validation = particle_capture_source.Capture(
          joined, candidate_catalog.registry, *captured_particles);
      if (!validation) {
        result.validation = std::move(validation);
        return result;
      }
    } else if (finalize_scene_generation &&
               particle_capture_source.last_source_sequence() != 0U) {
      // The asset delta is synchronized before this final scene. Publish
      // explicit particle tombstones against that new empty catalog sequence
      // so N1 never retains a live SmokeMat reference after its assets retire.
      captured_particles = std::make_shared<Ogre14ParticleCapturedFrame>();
      validation = particle_capture_source.FinalizeSceneGeneration(
          candidate_catalog.registry, frame.simulation_tick,
          frame.simulation_time_seconds, frame.absolute_world_origin_meters,
          *captured_particles);
      if (!validation) {
        result.validation = std::move(validation);
        return result;
      }
    }

    if (finalize_scene_generation) {
      // Even an already-empty source generation may still alias the producer's
      // original const catalog owner. The fresh candidate was allocated above
      // before the particle lifecycle could commit; never cast away constness
      // after publication.
      staged_asset_catalog->assets.clear();
      candidate_objects.clear();
      candidate_lights.clear();
      candidate_reflection_probes.clear();
      candidate_mesh_asset_pairs.clear();
    }

    asset_catalog = std::move(candidate_asset_catalog);
    objects = std::move(candidate_objects);
    lights = std::move(candidate_lights);
    reflection_probes = std::move(candidate_reflection_probes);
    validated_mesh_asset_pairs = std::move(candidate_mesh_asset_pairs);
    next_dynamic_update_sequence =
        candidate_next_dynamic_update_sequence;
    dynamic_update_sequence_exhausted =
        candidate_dynamic_update_sequence_exhausted;
    validated_environment_assets = finalize_scene_generation
                                       ? ValidatedEnvironmentAssets{}
                                       : candidate_environment_assets;
    asset_compatibility_cache_initialized = !finalize_scene_generation;
    if (finalize_scene_generation) {
      camera_state = {};
      last_simulation_tick = 0U;
      last_simulation_time_seconds = 0.0;
      last_absolute_world_origin_meters = {};
      time_lineage_initialized = false;
    } else {
      camera_state.camera = frame.camera;
      camera_state.absolute_world_origin_meters =
          frame.absolute_world_origin_meters;
      camera_state.initialized = true;
      last_simulation_tick = frame.simulation_tick;
      last_simulation_time_seconds = frame.simulation_time_seconds;
      last_absolute_world_origin_meters = frame.absolute_world_origin_meters;
      time_lineage_initialized = true;
    }
    initialized = true;
    scene_generation_open = !finalize_scene_generation;
    if (next_snapshot_id ==
        (std::numeric_limits<std::uint64_t>::max)()) {
      snapshot_id_exhausted = true;
    } else {
      ++next_snapshot_id;
    }

    result.production.asset_delta = std::move(asset_delta);
    result.production.scene_snapshot = created.snapshot;
    result.production.continuous_particles = std::move(captured_particles);
    result.production.camera = camera;
    result.validation = ValidationResult::Success();
    std::atomic_store_explicit(&published_snapshot, created.snapshot,
                               std::memory_order_release);
    return result;
  }

  [[nodiscard]] GraphicsSceneSnapshotProduceResult FinalizeSceneGeneration() {
    if (!initialized || !scene_generation_open || !camera_state.initialized ||
        published_snapshot == nullptr) {
      GraphicsSceneSnapshotProduceResult result;
      result.validation = Failure(
          ValidationCode::MISSING_REFERENCE, "scene_generation",
          "a scene generation cannot be finalized before first publication");
      return result;
    }

    GraphicsSceneFrameInput empty;
    empty.simulation_tick = last_simulation_tick;
    empty.simulation_time_seconds = last_simulation_time_seconds;
    empty.absolute_world_origin_meters = last_absolute_world_origin_meters;
    empty.camera = camera_state.camera;
    return Produce(empty, true);
  }

  GraphicsSceneSnapshotProducerConfiguration configuration;
  ValidationResult configuration_validation;
  std::shared_ptr<const AssetCatalog> asset_catalog;
  std::vector<ObjectState> objects;
  std::vector<LightState> lights;
  std::vector<ReflectionProbeState> reflection_probes;
  std::vector<ValidatedMeshAssetPair> validated_mesh_asset_pairs;
  ValidatedEnvironmentAssets validated_environment_assets;
  CameraState camera_state;
  std::uint64_t next_snapshot_id = 1U;
  std::uint64_t last_simulation_tick = 0U;
  double last_simulation_time_seconds = 0.0;
  Double3 last_absolute_world_origin_meters{};
  bool initialized = false;
  bool time_lineage_initialized = false;
  bool scene_generation_open = false;
  bool snapshot_id_exhausted = false;
  std::uint64_t next_dynamic_update_sequence = 1U;
  bool dynamic_update_sequence_exhausted = false;
  bool asset_compatibility_cache_initialized = false;
  std::shared_ptr<const SceneSnapshot> published_snapshot;
  Ogre14ParticleCaptureSource particle_capture_source;
};

GraphicsSceneSnapshotProducer::GraphicsSceneSnapshotProducer(
    GraphicsSceneSnapshotProducerConfiguration configuration)
    : impl_(std::make_unique<Impl>(std::move(configuration))) {}

GraphicsSceneSnapshotProducer::~GraphicsSceneSnapshotProducer() = default;

GraphicsSceneSnapshotProduceResult GraphicsSceneSnapshotProducer::Produce(
    const GraphicsSceneFrameInput &frame) {
  return impl_->Produce(frame);
}

GraphicsSceneSnapshotProduceResult
GraphicsSceneSnapshotProducer::ProduceJoinedFrame(
    IJoinedGraphicsSceneSource &source) {
  GraphicsSceneFrameInput frame;
  const ValidationResult capture = source.CaptureJoinedGraphicsFrame(frame);
  if (!capture) {
    GraphicsSceneSnapshotProduceResult result;
    result.validation = capture;
    return result;
  }
  try {
    GraphicsSceneSnapshotProduceResult result = Produce(frame);
    if (result) {
      source.CommitJoinedGraphicsFrame();
    } else {
      source.DiscardJoinedGraphicsFrame();
    }
    return result;
  } catch (...) {
    source.DiscardJoinedGraphicsFrame();
    throw;
  }
}

GraphicsSceneSnapshotProduceResult
GraphicsSceneSnapshotProducer::FinalizeSceneGeneration() {
  return impl_->FinalizeSceneGeneration();
}

GraphicsSceneAssetRecoveryResult
GraphicsSceneSnapshotProducer::BuildRecoveryAssetSnapshot() const {
  GraphicsSceneAssetRecoveryResult result;
  if (!impl_->configuration_validation) {
    result.validation = impl_->configuration_validation;
    return result;
  }
  if (impl_->asset_catalog->registry.sequence() == 0U) {
    result.validation = Failure(
        ValidationCode::SEQUENCE_MISMATCH, "asset_sequence",
        "asset recovery is unavailable before first successful production");
    return result;
  }
  result.full_snapshot = impl_->asset_catalog->registry.BuildFullSnapshot();
  result.validation = ValidateRenderAssetDelta(result.full_snapshot);
  if (!result.validation) {
    return result;
  }
  RenderAssetRegistry replay(impl_->asset_catalog->registry.registry_id());
  result.validation = replay.Apply(result.full_snapshot);
  return result;
}

std::uint64_t GraphicsSceneSnapshotProducer::registry_id() const noexcept {
  return impl_->asset_catalog->registry.registry_id();
}

std::uint64_t GraphicsSceneSnapshotProducer::asset_sequence() const noexcept {
  return impl_->asset_catalog->registry.sequence();
}

bool GraphicsSceneSnapshotProducer::has_open_scene_generation() const
    noexcept {
  return impl_->scene_generation_open;
}

std::shared_ptr<const SceneSnapshot>
GraphicsSceneSnapshotProducer::LoadPublishedSnapshot() const noexcept {
  return std::atomic_load_explicit(&impl_->published_snapshot,
                                   std::memory_order_acquire);
}

} // namespace RoR::Render
