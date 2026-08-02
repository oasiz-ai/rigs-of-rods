/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14GraphicsSceneSource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <set>
#include <utility>

namespace RoR::Render {
namespace {

struct RequiredField final {
  Ogre14GraphicsSceneCaptureField field;
  const char *name;
};

constexpr std::array<RequiredField, 12U> kRequiredFields{{
    {Ogre14GraphicsSceneCaptureField::JOINED_BUFFER_ATOMICITY,
     "joined_buffer_atomicity"},
    {Ogre14GraphicsSceneCaptureField::SIMULATION_TICK,
     "simulation_tick"},
    {Ogre14GraphicsSceneCaptureField::SIMULATION_TIME_SECONDS,
     "simulation_time_seconds"},
    {Ogre14GraphicsSceneCaptureField::ABSOLUTE_WORLD_ORIGIN_METERS,
     "absolute_world_origin_meters"},
    {Ogre14GraphicsSceneCaptureField::ENVIRONMENT, "environment"},
    {Ogre14GraphicsSceneCaptureField::ASSETS, "assets"},
    {Ogre14GraphicsSceneCaptureField::STATIC_MESHES, "static_meshes"},
    {Ogre14GraphicsSceneCaptureField::LIGHTS, "lights"},
    {Ogre14GraphicsSceneCaptureField::REFLECTION_PROBES,
     "reflection_probes"},
    {Ogre14GraphicsSceneCaptureField::CAMERA, "camera"},
    {Ogre14GraphicsSceneCaptureField::POST_UPDATE_SCENE_ATOMICITY,
     "post_update_scene_atomicity"},
    {Ogre14GraphicsSceneCaptureField::DYNAMIC_MESHES, "dynamic_meshes"},
}};

ValidationResult Failure(ValidationCode code, const char *field,
                         std::string detail) {
  ValidationResult result;
  result.code = code;
  result.field = field != nullptr ? field : "";
  result.detail = std::move(detail);
  return result;
}

bool IsKnownProjection(Ogre14CameraProjectionKind projection) noexcept {
  switch (projection) {
  case Ogre14CameraProjectionKind::PERSPECTIVE:
  case Ogre14CameraProjectionKind::ORTHOGRAPHIC:
    return true;
  }
  return false;
}

bool IsKnownLightKind(Ogre14GraphicsSceneLightKind kind) noexcept {
  switch (kind) {
  case Ogre14GraphicsSceneLightKind::POINT:
  case Ogre14GraphicsSceneLightKind::DIRECTIONAL:
  case Ogre14GraphicsSceneLightKind::SPOT:
  case Ogre14GraphicsSceneLightKind::RECTANGLE:
    return true;
  }
  return false;
}

constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;
constexpr char kOgre14LightIdentityDomain[] = "ror.ogre14.light.name.v1";
constexpr char kOgre14StaticMeshIdentityDomain[] =
    "ror.ogre14.static.mesh.asset.v1";
constexpr char kOgre14StaticMaterialIdentityDomain[] =
    "ror.ogre14.static.material.asset.v1";
constexpr char kOgre14StaticObjectIdentityDomain[] =
    "ror.ogre14.static.object.section.v1";
constexpr char kOgre14DynamicMeshIdentityDomain[] =
    "ror.ogre14.dynamic.mesh.asset.v1";
constexpr char kOgre14DynamicObjectIdentityDomain[] =
    "ror.ogre14.dynamic.object.section.v1";

void HashByte(std::uint64_t &hash, std::uint8_t byte) noexcept {
  hash ^= byte;
  hash *= kFnv1a64Prime;
}

void AppendU32(std::string &bytes, std::uint32_t value) {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

void AppendU64(std::string &bytes, std::uint64_t value) {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xFFU));
  }
}

void AppendString(std::string &bytes, std::string_view value) {
  AppendU64(bytes, static_cast<std::uint64_t>(value.size()));
  bytes.append(value.data(), value.size());
}

std::string BuildMeshAssetKey(
    const Ogre14GraphicsSceneMeshAssetIdentity &identity) {
  std::string key;
  key.reserve(sizeof(kOgre14StaticMeshIdentityDomain) +
              identity.exact_resource_group.size() +
              identity.exact_mesh_name.size() + 64U);
  key.append(kOgre14StaticMeshIdentityDomain,
             sizeof(kOgre14StaticMeshIdentityDomain) - 1U);
  key.push_back('\0');
  AppendString(key, identity.exact_resource_group);
  AppendString(key, identity.exact_mesh_name);
  AppendU32(key, identity.submesh_index);
  AppendU32(key, identity.vertex_start);
  AppendU32(key, identity.vertex_count);
  AppendU32(key, identity.index_start);
  AppendU32(key, identity.index_count);
  key.push_back(identity.reverse_winding ? '\1' : '\0');
  return key;
}

std::string BuildMaterialAssetKey(std::string_view exact_resource_group,
                                  std::string_view exact_name) {
  std::string key;
  key.reserve(sizeof(kOgre14StaticMaterialIdentityDomain) +
              exact_resource_group.size() + exact_name.size() + 24U);
  key.append(kOgre14StaticMaterialIdentityDomain,
             sizeof(kOgre14StaticMaterialIdentityDomain) - 1U);
  key.push_back('\0');
  AppendString(key, exact_resource_group);
  AppendString(key, exact_name);
  return key;
}

std::string BuildStaticObjectKey(std::uint64_t stable_object_id,
                                 std::uint32_t section_index) {
  std::string key;
  key.reserve(sizeof(kOgre14StaticObjectIdentityDomain) + 16U);
  key.append(kOgre14StaticObjectIdentityDomain,
             sizeof(kOgre14StaticObjectIdentityDomain) - 1U);
  key.push_back('\0');
  AppendU64(key, stable_object_id);
  AppendU32(key, section_index);
  return key;
}

bool IsKnownDynamicComponentKind(
    Ogre14GraphicsSceneDynamicComponentKind kind) noexcept {
  switch (kind) {
  case Ogre14GraphicsSceneDynamicComponentKind::CAB:
  case Ogre14GraphicsSceneDynamicComponentKind::FLEXBODY:
  case Ogre14GraphicsSceneDynamicComponentKind::FLEXMESH_WHEEL:
  case Ogre14GraphicsSceneDynamicComponentKind::MESHWHEEL_TIRE:
    return true;
  }
  return false;
}

std::string BuildDynamicSectionKey(
    const Ogre14GraphicsSceneDynamicSectionIdentity &identity,
    std::string_view domain) {
  std::string key;
  key.reserve(domain.size() + 32U);
  key.append(domain.data(), domain.size());
  key.push_back('\0');
  AppendU64(key, static_cast<std::uint64_t>(identity.actor_instance_id));
  key.push_back(static_cast<char>(identity.component_kind));
  AppendU32(key, identity.component_id);
  AppendU32(key, identity.section_index);
  return key;
}

bool EquivalentJoinedDynamicContents(
    const GraphicsSceneDynamicMeshState &lhs,
    const Ogre14GraphicsSceneJoinedDynamicState &rhs) noexcept {
  return lhs.topology_revision == rhs.topology_revision &&
         lhs.positions == rhs.positions && lhs.normals == rhs.normals &&
         lhs.tangents == rhs.tangents && lhs.velocities == rhs.velocities &&
         lhs.updated_local_bounds.minimum == rhs.updated_local_bounds.minimum &&
         lhs.updated_local_bounds.maximum == rhs.updated_local_bounds.maximum;
}

ValidationResult HashStableKey(std::string_view key, const char *field,
                               std::uint64_t &stable_id) {
  if (key.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, field,
        "stable OGRE 14 identity key must not be empty");
  }
  std::uint64_t candidate = kFnv1a64OffsetBasis;
  for (const char byte : key) {
    HashByte(candidate,
             static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
  }
  if (candidate == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, field,
        "OGRE 14 identity key hashed to the reserved zero identity");
  }
  stable_id = candidate;
  return ValidationResult::Success();
}

ValidationResult RegisterIdentity(
    std::string_view exact_key, std::uint64_t stable_id,
    std::map<std::uint64_t, std::string> &names_by_id,
    std::map<std::string, std::uint64_t, std::less<>> &ids_by_name,
    const char *key_field, const char *id_field) {
  if (exact_key.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, key_field,
        "OGRE 14 identity key must not be empty");
  }
  if (stable_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, id_field,
        "derived OGRE 14 identity must be nonzero");
  }
  const auto id_match = names_by_id.find(stable_id);
  if (id_match != names_by_id.end() && id_match->second != exact_key) {
    return ValidationResult::Failure(
        ValidationCode::DUPLICATE_IDENTIFIER, id_field,
        "distinct exact OGRE 14 identity keys collided");
  }
  const auto key_match = ids_by_name.find(exact_key);
  if (key_match != ids_by_name.end() && key_match->second != stable_id) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, key_field,
        "an exact OGRE 14 identity key changed stable identity");
  }
  if (id_match != names_by_id.end()) {
    return ValidationResult::Success();
  }

  auto inserted_name = names_by_id.emplace(stable_id, exact_key);
  try {
    const auto inserted_id = ids_by_name.emplace(exact_key, stable_id);
    if (!inserted_id.second) {
      names_by_id.erase(inserted_name.first);
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH, key_field,
          "an exact OGRE 14 identity key changed stable identity");
    }
  } catch (...) {
    names_by_id.erase(inserted_name.first);
    throw;
  }
  return ValidationResult::Success();
}

bool IsKnownMaterialBlend(
    Ogre14GraphicsSceneMaterialBlend blend) noexcept {
  switch (blend) {
  case Ogre14GraphicsSceneMaterialBlend::REPLACE:
  case Ogre14GraphicsSceneMaterialBlend::STRAIGHT_ALPHA:
    return true;
  }
  return false;
}

bool IsKnownMaterialCull(Ogre14GraphicsSceneMaterialCull cull) noexcept {
  switch (cull) {
  case Ogre14GraphicsSceneMaterialCull::NONE:
  case Ogre14GraphicsSceneMaterialCull::CLOCKWISE:
  case Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE:
    return true;
  }
  return false;
}

bool IsKnownMaterialAlphaReject(
    Ogre14GraphicsSceneMaterialAlphaReject alpha_reject) noexcept {
  switch (alpha_reject) {
  case Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS:
  case Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL:
    return true;
  }
  return false;
}

ValidationResult AtStaticSection(ValidationResult result,
                                 std::size_t index) {
  if (!result) {
    result.element_index = index;
    result.field = "static_meshes." + result.field;
  }
  return result;
}

ValidationResult AtDynamicSection(ValidationResult result,
                                  std::size_t index) {
  if (!result) {
    result.element_index = index;
    result.field = "dynamic_meshes." + result.field;
  }
  return result;
}

bool HasMirroredLinearTransform(const Matrix4x4 &transform) noexcept {
  return LinearDeterminant(transform) < 0.0F;
}

} // namespace

const char *ToString(Ogre14GraphicsSceneCaptureField field) noexcept {
  for (const RequiredField &required : kRequiredFields) {
    if (required.field == field) {
      return required.name;
    }
  }
  return "invalid";
}

std::string DescribeMissingOgre14GraphicsSceneFields(
    std::uint32_t available_fields) {
  std::string result;
  for (const RequiredField &required : kRequiredFields) {
    if ((available_fields & Ogre14GraphicsSceneCaptureFieldBit(
                                required.field)) != 0U) {
      continue;
    }
    if (!result.empty()) {
      result += ", ";
    }
    result += required.name;
  }
  return result;
}

ValidationResult ValidateOgre14GraphicsSceneCapture(
    const Ogre14GraphicsSceneCapture &capture) {
  if (capture.version != kOgre14GraphicsSceneSourceVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported OGRE 14 graphics-scene source version");
  }
  if ((capture.available_fields &
       ~kOgre14GraphicsSceneRequiredFields) != 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "available_fields",
        "OGRE 14 capture advertises unknown field bits");
  }
  const std::string missing =
      DescribeMissingOgre14GraphicsSceneFields(capture.available_fields);
  if (!missing.empty()) {
    const auto first_missing = std::find_if(
        kRequiredFields.begin(), kRequiredFields.end(),
        [&capture](const RequiredField &required) {
          return (capture.available_fields &
                  Ogre14GraphicsSceneCaptureFieldBit(required.field)) == 0U;
        });
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        first_missing != kRequiredFields.end() ? first_missing->name
                                               : "available_fields",
        "missing required OGRE 14 joined fields: " + missing);
  }
  if (capture.joined_buffer_epoch == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "joined_buffer_epoch",
        "joined buffer epoch must be nonzero");
  }
  if (capture.post_update_scene_epoch == 0U ||
      capture.post_update_scene_epoch != capture.joined_buffer_epoch) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH, "post_update_scene_epoch",
        "post-UpdateScene epoch must exactly match the joined buffer epoch");
  }
  return ValidationResult::Success();
}

Ogre14GraphicsSceneSource::Ogre14GraphicsSceneSource(
    IOgre14GraphicsSceneCaptureProvider &provider) noexcept
    : provider_(provider) {}

Ogre14GraphicsSceneSource::~Ogre14GraphicsSceneSource() {
  DiscardJoinedGraphicsFrame();
}

ValidationResult Ogre14GraphicsSceneSource::CaptureJoinedGraphicsFrame(
    GraphicsSceneFrameInput &frame) {
  if (capture_pending_) {
    return ValidationResult::Failure(
        ValidationCode::SEQUENCE_MISMATCH,
        "joined_graphics_source.pending_capture",
        "the preceding joined frame must be committed or discarded first");
  }
  try {
    Ogre14GraphicsSceneCapture capture;
    ValidationResult validation =
        provider_.CaptureOgre14GraphicsScene(capture);
    if (!validation) {
      provider_.DiscardOgre14GraphicsSceneCapture();
      return validation;
    }
    validation = ValidateOgre14GraphicsSceneCapture(capture);
    if (!validation) {
      provider_.DiscardOgre14GraphicsSceneCapture();
      return validation;
    }
    frame = std::move(capture.frame);
    capture_pending_ = true;
    return ValidationResult::Success();
  } catch (const std::exception &) {
    provider_.DiscardOgre14GraphicsSceneCapture();
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "joined_graphics_source",
        "OGRE 14 capture provider threw an exception");
  } catch (...) {
    provider_.DiscardOgre14GraphicsSceneCapture();
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "joined_graphics_source",
        "OGRE 14 capture provider threw a non-standard exception");
  }
}

void Ogre14GraphicsSceneSource::CommitJoinedGraphicsFrame() noexcept {
  if (!capture_pending_) {
    return;
  }
  provider_.CommitOgre14GraphicsSceneCapture();
  capture_pending_ = false;
}

void Ogre14GraphicsSceneSource::DiscardJoinedGraphicsFrame() noexcept {
  if (!capture_pending_) {
    return;
  }
  provider_.DiscardOgre14GraphicsSceneCapture();
  capture_pending_ = false;
}

ValidationResult Ogre14GraphicsSceneStaticIdentityRegistry::
    RegisterDerivedAssetIdentity(std::string_view exact_key,
                                 std::uint64_t stable_id) {
  return RegisterIdentity(exact_key, stable_id, asset_names_by_id_,
                          asset_ids_by_name_, "assets.exact_key",
                          "assets.source_asset_id");
}

ValidationResult Ogre14GraphicsSceneStaticIdentityRegistry::
    RegisterDerivedObjectIdentity(std::string_view exact_key,
                                  std::uint64_t stable_id) {
  return RegisterIdentity(exact_key, stable_id, object_names_by_id_,
                          object_ids_by_name_, "static_meshes.exact_key",
                          "static_meshes.source_object_id");
}

ValidationResult ValidateOgre14GraphicsSceneStaticCoverage(
    const Ogre14GraphicsSceneUnsupportedGeometry &unsupported) {
  if (unsupported.terrain) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "static_meshes.unsupported.terrain",
        "OGRE Terrain pages require a separate exact CPU terrain adapter");
  }
  if (unsupported.procedural) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "static_meshes.unsupported.procedural",
        "procedural road geometry is not an authored immutable MeshObject");
  }
  if (unsupported.deformable) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "static_meshes.unsupported.deformable",
        "actor, skeletal, or vertex-animated geometry requires a deformable "
        "stream");
  }
  if (unsupported.paged) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "static_meshes.unsupported.paged",
        "paged vegetation batches are camera-dependent generated geometry");
  }
  if (unsupported.animated) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "static_meshes.unsupported.animated",
        "animated or particle terrain-object visuals are not static "
        "snapshots");
  }
  return ValidationResult::Success();
}

ValidationResult DeriveOgre14GraphicsSceneMeshAssetId(
    const Ogre14GraphicsSceneMeshAssetIdentity &identity,
    std::uint64_t &stable_id) {
  if (identity.exact_mesh_name.empty() ||
      identity.exact_mesh_name.find('\0') != std::string::npos ||
      identity.exact_resource_group.find('\0') != std::string::npos ||
      identity.vertex_count == 0U || identity.index_count == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "assets.mesh.exact_identity",
        "mesh resource identity requires exact NUL-free names and nonzero "
        "draw counts");
  }
  return HashStableKey(BuildMeshAssetKey(identity),
                       "assets.mesh.source_asset_id", stable_id);
}

ValidationResult DeriveOgre14GraphicsSceneMaterialAssetId(
    std::string_view exact_resource_group, std::string_view exact_name,
    std::uint64_t &stable_id) {
  if (exact_name.empty() || exact_name.find('\0') != std::string_view::npos ||
      exact_resource_group.find('\0') != std::string_view::npos) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER,
        "assets.material.exact_identity",
        "material resource identity requires an exact nonempty NUL-free "
        "name");
  }
  return HashStableKey(
      BuildMaterialAssetKey(exact_resource_group, exact_name),
      "assets.material.source_asset_id", stable_id);
}

ValidationResult DeriveOgre14GraphicsSceneStaticSectionId(
    std::uint64_t stable_object_id, std::uint32_t section_index,
    std::uint64_t &stable_id) {
  if (stable_object_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER,
        "static_meshes.stable_object_id",
        "terrain static-object identity must be nonzero");
  }
  return HashStableKey(BuildStaticObjectKey(stable_object_id, section_index),
                       "static_meshes.source_object_id", stable_id);
}

ValidationResult BuildOgre14GraphicsSceneStaticMeshPayload(
    const Ogre14GraphicsSceneCpuMeshSectionInput &input,
    std::shared_ptr<const RenderAssetPayload> &payload) {
  if (input.positions.empty()) {
    return ValidationResult::Failure(
        ValidationCode::EMPTY_PAYLOAD, "mesh.positions",
        "OGRE 14 static submesh requires CPU position data");
  }
  for (std::size_t index = 0U; index < input.positions.size(); ++index) {
    if (!IsFinite(input.positions[index])) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE, "mesh.positions",
          "OGRE 14 CPU positions must be finite", index);
    }
  }

  MeshResourceDescriptor descriptor;
  descriptor.debug_name = input.debug_name;
  descriptor.topology = MeshPrimitiveTopology::TRIANGLE_LIST;
  descriptor.index_format = input.index_format;
  descriptor.topology_revision = input.topology_revision;
  descriptor.dynamic = false;
  descriptor.positions = input.positions;
  descriptor.normals = input.normals;
  descriptor.tangents = input.tangents;
  descriptor.texture_coordinates_0 = input.texture_coordinates_0;
  descriptor.texture_coordinates_1 = input.texture_coordinates_1;
  descriptor.colors = input.colors;
  descriptor.indices = input.indices;
  if (input.reverse_winding) {
    if (descriptor.indices.size() % 3U != 0U) {
      return ValidationResult::Failure(
          ValidationCode::SIZE_MISMATCH, "mesh.indices",
          "triangle winding conversion requires complete triangles");
    }
    for (std::size_t index = 0U; index < descriptor.indices.size();
         index += 3U) {
      std::swap(descriptor.indices[index + 1U],
                descriptor.indices[index + 2U]);
    }
  }

  descriptor.local_bounds.minimum = descriptor.positions.front();
  descriptor.local_bounds.maximum = descriptor.positions.front();
  for (const Float3 &position : descriptor.positions) {
    descriptor.local_bounds.minimum.x =
        (std::min)(descriptor.local_bounds.minimum.x, position.x);
    descriptor.local_bounds.minimum.y =
        (std::min)(descriptor.local_bounds.minimum.y, position.y);
    descriptor.local_bounds.minimum.z =
        (std::min)(descriptor.local_bounds.minimum.z, position.z);
    descriptor.local_bounds.maximum.x =
        (std::max)(descriptor.local_bounds.maximum.x, position.x);
    descriptor.local_bounds.maximum.y =
        (std::max)(descriptor.local_bounds.maximum.y, position.y);
    descriptor.local_bounds.maximum.z =
        (std::max)(descriptor.local_bounds.maximum.z, position.z);
  }

  const ValidationResult validation =
      ValidateMeshResourceDescriptor(descriptor);
  if (!validation) {
    return validation;
  }
  auto candidate = std::make_shared<const RenderAssetPayload>(
      std::move(descriptor));
  payload = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult DeriveOgre14GraphicsSceneDynamicMeshAssetId(
    const Ogre14GraphicsSceneDynamicSectionIdentity &identity,
    std::uint64_t &stable_id) {
  if (identity.actor_instance_id < 0 ||
      !IsKnownDynamicComponentKind(identity.component_kind)) {
    return ValidationResult::Failure(
        identity.actor_instance_id < 0 ? ValidationCode::INVALID_IDENTIFIER
                                       : ValidationCode::INVALID_ENUM,
        "assets.dynamic_mesh.exact_identity",
        "dynamic mesh identity requires a nonnegative actor and known "
        "component kind");
  }
  return HashStableKey(
      BuildDynamicSectionKey(identity, kOgre14DynamicMeshIdentityDomain),
      "assets.dynamic_mesh.source_asset_id", stable_id);
}

ValidationResult DeriveOgre14GraphicsSceneDynamicSectionId(
    const Ogre14GraphicsSceneDynamicSectionIdentity &identity,
    std::uint64_t &stable_id) {
  if (identity.actor_instance_id < 0 ||
      !IsKnownDynamicComponentKind(identity.component_kind)) {
    return ValidationResult::Failure(
        identity.actor_instance_id < 0 ? ValidationCode::INVALID_IDENTIFIER
                                       : ValidationCode::INVALID_ENUM,
        "dynamic_meshes.exact_identity",
        "dynamic section identity requires a nonnegative actor and known "
        "component kind");
  }
  return HashStableKey(
      BuildDynamicSectionKey(identity, kOgre14DynamicObjectIdentityDomain),
      "dynamic_meshes.source_object_id", stable_id);
}

ValidationResult BuildOgre14GraphicsSceneDynamicMeshPayload(
    const Ogre14GraphicsSceneCpuMeshSectionInput &input,
    std::shared_ptr<const RenderAssetPayload> &payload) {
  std::shared_ptr<const RenderAssetPayload> static_payload;
  ValidationResult validation =
      BuildOgre14GraphicsSceneStaticMeshPayload(input, static_payload);
  if (!validation) {
    return validation;
  }
  MeshResourceDescriptor mesh =
      std::get<MeshResourceDescriptor>(*static_payload);
  mesh.dynamic = true;
  validation = ValidateMeshResourceDescriptor(mesh);
  if (!validation) {
    return validation;
  }
  payload =
      std::make_shared<const RenderAssetPayload>(std::move(mesh));
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneDynamicInventory(
    const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> &inputs,
    Ogre14GraphicsSceneDynamicIdentityRegistry &identity_registry,
    std::vector<GraphicsSceneAssetInput> &assets,
    std::vector<GraphicsSceneDynamicMeshInput> &dynamic_meshes) {
  Ogre14GraphicsSceneDynamicIdentityRegistry candidate_registry =
      identity_registry;
  std::vector<GraphicsSceneAssetInput> candidate_assets;
  std::vector<GraphicsSceneDynamicMeshInput> candidate_meshes;
  candidate_assets.reserve(inputs.size() * 2U +
                           identity_registry.live_asset_keys_.size());
  candidate_meshes.reserve(inputs.size());
  std::map<std::uint64_t, std::size_t> asset_indices;
  std::set<std::uint64_t> object_ids;
  std::set<std::string, std::less<>> current_asset_keys;
  std::set<std::string, std::less<>> current_object_keys;

  const auto add_asset =
      [&](std::uint64_t source_id,
          const std::shared_ptr<const RenderAssetPayload> &payload_owner)
      -> ValidationResult {
    const auto existing = asset_indices.find(source_id);
    if (existing != asset_indices.end()) {
      const GraphicsSceneAssetInput &prior =
          candidate_assets[existing->second];
      if (!EquivalentRenderAssetPayload(*prior.payload, *payload_owner)) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH, "assets.payload",
            "one OGRE 14 dynamic asset identity produced conflicting "
            "payloads");
      }
      return ValidationResult::Success();
    }
    GraphicsSceneAssetInput asset;
    asset.source_asset_id = source_id;
    asset.payload = payload_owner;
    asset_indices.emplace(source_id, candidate_assets.size());
    candidate_assets.push_back(std::move(asset));
    return ValidationResult::Success();
  };

  for (std::size_t input_index = 0U; input_index < inputs.size();
       ++input_index) {
    const Ogre14GraphicsSceneDynamicSectionCaptureInput &input =
        inputs[input_index];
    if (input.exact_entity_name.empty() ||
        input.exact_entity_name.find('\0') != std::string::npos) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER,
          "dynamic_meshes.exact_entity_name",
          "actor deformable Entity requires an exact nonempty NUL-free name",
          input_index);
    }
    if (input.mesh_payload == nullptr ||
        input.mesh_payload->valueless_by_exception() ||
        RenderAssetPayloadKind(*input.mesh_payload) != RenderAssetKind::MESH) {
      return ValidationResult::Failure(
          input.mesh_payload == nullptr ? ValidationCode::EMPTY_PAYLOAD
                                        : ValidationCode::WRONG_ASSET_KIND,
          "dynamic_meshes.mesh_payload",
          "dynamic section requires an immutable base mesh payload",
          input_index);
    }
    if (input.state == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::EMPTY_PAYLOAD, "dynamic_meshes.state",
          "dynamic section requires one copied post-join CPU staging owner",
          input_index);
    }
    if (input.has_dynamic_vertex_colors) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "dynamic_meshes.dynamic_vertex_colors",
          "frame-varying FlexBody blend colors require an explicit update "
          "stream",
          input_index);
    }
    if (!HasInvertibleAffineTransform(input.render_from_object)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "dynamic_meshes.render_from_object",
          "dynamic section transform must be finite affine and invertible",
          input_index);
    }
    if (HasMirroredLinearTransform(input.render_from_object)) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "dynamic_meshes.render_from_object.mirrored",
          "mirrored deformable transforms require canonical mesh rebasing",
          input_index);
    }

    const MeshResourceDescriptor &mesh =
        std::get<MeshResourceDescriptor>(*input.mesh_payload);
    ValidationResult validation = ValidateMeshResourceDescriptor(mesh);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    if (!mesh.dynamic) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "dynamic_meshes.mesh_payload.dynamic",
          "deformable base mesh must allocate dynamic storage", input_index);
    }
    if (input.state->topology_revision != mesh.topology_revision) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "dynamic_meshes.state.topology_revision",
          "joined staging topology differs from its immutable base mesh",
          input_index);
    }

    DynamicMeshUpdateDescriptor compatibility_update;
    compatibility_update.topology_revision = input.state->topology_revision;
    compatibility_update.positions = input.state->positions;
    compatibility_update.normals = input.state->normals;
    compatibility_update.tangents = input.state->tangents;
    compatibility_update.velocities = input.state->velocities;
    compatibility_update.has_updated_bounds = true;
    compatibility_update.updated_local_bounds = input.state->updated_local_bounds;
    validation =
        ValidateDynamicMeshUpdateCompatibility(mesh, compatibility_update);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    Bounds3 tight;
    tight.minimum = input.state->positions.front();
    tight.maximum = input.state->positions.front();
    for (const Float3 &position : input.state->positions) {
      tight.minimum.x = (std::min)(tight.minimum.x, position.x);
      tight.minimum.y = (std::min)(tight.minimum.y, position.y);
      tight.minimum.z = (std::min)(tight.minimum.z, position.z);
      tight.maximum.x = (std::max)(tight.maximum.x, position.x);
      tight.maximum.y = (std::max)(tight.maximum.y, position.y);
      tight.maximum.z = (std::max)(tight.maximum.z, position.z);
    }
    if (tight.minimum != input.state->updated_local_bounds.minimum ||
        tight.maximum != input.state->updated_local_bounds.maximum) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_BOUNDS,
          "dynamic_meshes.state.updated_local_bounds",
          "joined staging bounds must be exact and tight", input_index);
    }

    const std::string mesh_key = BuildDynamicSectionKey(
        input.identity, kOgre14DynamicMeshIdentityDomain);
    const std::string object_key = BuildDynamicSectionKey(
        input.identity, kOgre14DynamicObjectIdentityDomain);
    const std::string material_key = BuildMaterialAssetKey(
        input.material.exact_resource_group, input.material.exact_name);
    std::uint64_t mesh_id = 0U;
    std::uint64_t material_id = 0U;
    std::uint64_t object_id = 0U;
    validation =
        DeriveOgre14GraphicsSceneDynamicMeshAssetId(input.identity, mesh_id);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    validation = DeriveOgre14GraphicsSceneMaterialAssetId(
        input.material.exact_resource_group, input.material.exact_name,
        material_id);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    validation =
        DeriveOgre14GraphicsSceneDynamicSectionId(input.identity, object_id);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    if (!object_ids.insert(object_id).second ||
        !current_object_keys.insert(object_key).second) {
      return ValidationResult::Failure(
          ValidationCode::DUPLICATE_IDENTIFIER,
          "dynamic_meshes.source_object_id",
          "dynamic-section identity is duplicated", input_index);
    }
    if (identity_registry.known_object_keys_.find(object_key) !=
            identity_registry.known_object_keys_.end() &&
        identity_registry.live_object_keys_.find(object_key) ==
            identity_registry.live_object_keys_.end()) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "dynamic_meshes.source_object_id",
          "a removed dynamic-section identity may never return", input_index);
    }

    validation = RegisterIdentity(
        mesh_key, mesh_id, candidate_registry.asset_names_by_id_,
        candidate_registry.asset_ids_by_name_, "assets.dynamic_mesh.exact_key",
        "assets.dynamic_mesh.source_asset_id");
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    validation = RegisterIdentity(
        material_key, material_id, candidate_registry.asset_names_by_id_,
        candidate_registry.asset_ids_by_name_, "assets.material.exact_key",
        "assets.material.source_asset_id");
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    validation = RegisterIdentity(
        object_key, object_id, candidate_registry.object_names_by_id_,
        candidate_registry.object_ids_by_name_,
        "dynamic_meshes.exact_key", "dynamic_meshes.source_object_id");
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }

    const auto prior_object = identity_registry.object_states_.find(object_key);
    if (prior_object != identity_registry.object_states_.end() &&
        (prior_object->second.exact_entity_name != input.exact_entity_name ||
         prior_object->second.mesh_key != mesh_key ||
         prior_object->second.material_key != material_key)) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "dynamic_meshes.identity_binding",
          "a live dynamic identity changed Entity, mesh, or material binding",
          input_index);
    }

    MaterialDescriptor material;
    validation =
        BuildOgre14GraphicsSceneMaterialFallback(input.material, material);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    validation = ValidateMaterialMeshCompatibility(material, mesh);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    std::shared_ptr<const RenderAssetPayload> material_payload =
        std::make_shared<const RenderAssetPayload>(std::move(material));
    const auto canonicalize =
        [&candidate_registry](
            const std::string &key,
            std::shared_ptr<const RenderAssetPayload> proposed) {
          const auto prior =
              candidate_registry.canonical_payloads_by_asset_key_.find(key);
          if (prior !=
                  candidate_registry.canonical_payloads_by_asset_key_.end() &&
              EquivalentRenderAssetPayload(*prior->second, *proposed)) {
            return prior->second;
          }
          candidate_registry.canonical_payloads_by_asset_key_[key] = proposed;
          return proposed;
        };
    const std::shared_ptr<const RenderAssetPayload> canonical_mesh =
        canonicalize(mesh_key, input.mesh_payload);
    const std::shared_ptr<const RenderAssetPayload> canonical_material =
        canonicalize(material_key, std::move(material_payload));
    validation = add_asset(mesh_id, canonical_mesh);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    validation = add_asset(material_id, canonical_material);
    if (!validation) {
      return AtDynamicSection(std::move(validation), input_index);
    }
    current_asset_keys.insert(mesh_key);
    current_asset_keys.insert(material_key);

    std::shared_ptr<const GraphicsSceneDynamicMeshState> deformation;
    if (prior_object != identity_registry.object_states_.end()) {
      if (prior_object->second.deformation == nullptr) {
        return ValidationResult::Failure(
            ValidationCode::MISSING_REFERENCE, "dynamic_meshes.state",
            "stored dynamic identity has no immutable deformation owner",
            input_index);
      }
      if (EquivalentJoinedDynamicContents(
              *prior_object->second.deformation, *input.state)) {
        deformation = prior_object->second.deformation;
      } else {
        const std::uint64_t prior_revision =
            prior_object->second.deformation->deformation_revision;
        if (prior_revision == (std::numeric_limits<std::uint64_t>::max)()) {
          return ValidationResult::Failure(
              ValidationCode::REVISION_MISMATCH,
              "dynamic_meshes.state.deformation_revision",
              "dynamic deformation revision would overflow", input_index);
        }
        auto next = std::make_shared<GraphicsSceneDynamicMeshState>();
        next->topology_revision = input.state->topology_revision;
        next->deformation_revision = prior_revision + 1U;
        next->positions = input.state->positions;
        next->normals = input.state->normals;
        next->tangents = input.state->tangents;
        next->velocities = input.state->velocities;
        next->updated_local_bounds = input.state->updated_local_bounds;
        deformation = std::move(next);
      }
    } else {
      auto first = std::make_shared<GraphicsSceneDynamicMeshState>();
      first->topology_revision = input.state->topology_revision;
      first->deformation_revision = 2U;
      first->positions = input.state->positions;
      first->normals = input.state->normals;
      first->tangents = input.state->tangents;
      first->velocities = input.state->velocities;
      first->updated_local_bounds = input.state->updated_local_bounds;
      deformation = std::move(first);
    }

    Ogre14GraphicsSceneDynamicIdentityRegistry::ObjectState object_state;
    object_state.exact_entity_name = input.exact_entity_name;
    object_state.mesh_key = mesh_key;
    object_state.material_key = material_key;
    object_state.deformation = deformation;
    candidate_registry.object_states_[object_key] = std::move(object_state);

    GraphicsSceneDynamicMeshInput instance;
    instance.source_object_id = object_id;
    instance.mesh_source_asset_id = mesh_id;
    instance.material_source_asset_id = material_id;
    instance.render_from_object = input.render_from_object;
    instance.visibility_mask = input.visible ? input.visibility_mask : 0U;
    instance.flags = 0U;
    if (input.casts_shadows) {
      instance.flags |= MESH_INSTANCE_CASTS_SHADOW;
    }
    if (input.receives_shadows) {
      instance.flags |= MESH_INSTANCE_RECEIVES_SHADOW;
    }
    if (input.visible_in_reflections) {
      instance.flags |= MESH_INSTANCE_VISIBLE_IN_REFLECTIONS;
    }
    instance.state = std::move(deformation);
    candidate_meshes.push_back(std::move(instance));
  }

  // Dynamic base assets remain owned for the adapter lifetime. This prevents
  // a shared material or immutable topology from being tombstoned merely
  // because its actor was removed, while object identities remain permanent
  // tombstones.
  for (const std::string &key : identity_registry.live_asset_keys_) {
    if (current_asset_keys.find(key) != current_asset_keys.end()) {
      continue;
    }
    const auto id = candidate_registry.asset_ids_by_name_.find(key);
    const auto payload =
        candidate_registry.canonical_payloads_by_asset_key_.find(key);
    if (id == candidate_registry.asset_ids_by_name_.end() ||
        payload == candidate_registry.canonical_payloads_by_asset_key_.end() ||
        payload->second == nullptr) {
      return ValidationResult::Failure(
          ValidationCode::MISSING_REFERENCE, "assets.dynamic_cache",
          "retained dynamic asset has incomplete identity or payload state");
    }
    ValidationResult validation = add_asset(id->second, payload->second);
    if (!validation) {
      return validation;
    }
    current_asset_keys.insert(key);
  }

  std::sort(candidate_assets.begin(), candidate_assets.end(),
            [](const GraphicsSceneAssetInput &lhs,
               const GraphicsSceneAssetInput &rhs) {
              return lhs.source_asset_id < rhs.source_asset_id;
            });
  std::sort(candidate_meshes.begin(), candidate_meshes.end(),
            [](const GraphicsSceneDynamicMeshInput &lhs,
               const GraphicsSceneDynamicMeshInput &rhs) {
              return lhs.source_object_id < rhs.source_object_id;
            });
  candidate_registry.known_asset_keys_.insert(current_asset_keys.begin(),
                                               current_asset_keys.end());
  candidate_registry.live_asset_keys_ = std::move(current_asset_keys);
  candidate_registry.known_object_keys_.insert(current_object_keys.begin(),
                                                current_object_keys.end());
  candidate_registry.live_object_keys_ = std::move(current_object_keys);

  identity_registry = std::move(candidate_registry);
  assets = std::move(candidate_assets);
  dynamic_meshes = std::move(candidate_meshes);
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneMaterialFallback(
    const Ogre14GraphicsSceneMaterialCaptureInput &input,
    MaterialDescriptor &material) {
  if (input.exact_name.empty() ||
      input.exact_name.find('\0') != std::string::npos ||
      input.exact_resource_group.find('\0') != std::string::npos) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "material.exact_identity",
        "OGRE 14 material requires an exact nonempty NUL-free name");
  }
  if (input.pass_count == 0U) {
    return ValidationResult::Failure(
        ValidationCode::MISSING_REFERENCE, "material.pass_count",
        "OGRE 14 material requires at least one authored pass");
  }
  if (input.pass_count != 1U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.pass_count",
        "factor-only fallback requires exactly one authored pass");
  }
  if (input.texture_unit_count != 0U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.texture_units",
        "factor-only fallback cannot discard authored texture units");
  }
  if (input.has_vertex_program || input.has_fragment_program) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.programs",
        "factor-only fallback cannot discard authored shader programs");
  }
  if (!IsKnownMaterialBlend(input.blend) ||
      !IsKnownMaterialCull(input.cull) ||
      !IsKnownMaterialAlphaReject(input.alpha_reject)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "material.native_state",
        "unknown OGRE 14 material blend, cull, or alpha-test state");
  }
  if (!IsFinite(input.diffuse_linear) ||
      !IsFinite(input.ambient_linear) ||
      !IsFinite(input.specular_linear) ||
      !IsFinite(input.emissive_linear) || !IsFinite(input.shininess)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "material.native_state",
        "all captured OGRE 14 material factors must be finite");
  }
  if (!IsNormalizedColor(input.diffuse_linear) ||
      !IsNonNegative(input.ambient_linear) ||
      !IsNonNegative(input.specular_linear) ||
      !IsNonNegative(input.emissive_linear) || input.shininess < 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "material.native_state",
        "OGRE 14 material factors are outside the portable fallback range");
  }
  if (input.blend == Ogre14GraphicsSceneMaterialBlend::STRAIGHT_ALPHA &&
      input.alpha_reject !=
          Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.alpha_mode",
        "portable fallback cannot combine legacy alpha blending and rejection");
  }

  MaterialDescriptor candidate;
  candidate.debug_name = input.exact_resource_group.empty()
                             ? input.exact_name
                             : input.exact_resource_group + "/" +
                                   input.exact_name;
  candidate.model = input.lighting_enabled
                        ? MaterialModel::PBR_METALLIC_ROUGHNESS
                        : MaterialModel::UNLIT;
  if (input.blend == Ogre14GraphicsSceneMaterialBlend::STRAIGHT_ALPHA) {
    candidate.alpha_mode = MaterialAlphaMode::BLEND;
  } else if (input.alpha_reject ==
             Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL) {
    candidate.alpha_mode = MaterialAlphaMode::MASK;
  } else {
    candidate.alpha_mode = MaterialAlphaMode::OPAQUE;
  }
  candidate.double_sided =
      input.cull == Ogre14GraphicsSceneMaterialCull::NONE;
  candidate.base_color_factor = input.diffuse_linear;
  candidate.metallic_factor = 0.0F;
  candidate.roughness_factor = input.lighting_enabled
                                   ? std::sqrt(2.0F /
                                               (input.shininess + 2.0F))
                                   : 1.0F;
  candidate.emissive_factor =
      input.lighting_enabled ? input.emissive_linear : Float3{};
  candidate.emissive_strength = 1.0F;
  candidate.alpha_cutoff =
      static_cast<float>(input.alpha_reject_value) / 255.0F;

  const ValidationResult validation = ValidateMaterialDescriptor(candidate);
  if (!validation) {
    return validation;
  }
  material = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneStaticInventory(
    const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &inputs,
    Ogre14GraphicsSceneStaticIdentityRegistry &identity_registry,
    std::vector<GraphicsSceneAssetInput> &assets,
    std::vector<GraphicsSceneStaticMeshInput> &static_meshes) {
  Ogre14GraphicsSceneStaticIdentityRegistry candidate_registry =
      identity_registry;
  std::vector<GraphicsSceneAssetInput> candidate_assets;
  std::vector<GraphicsSceneStaticMeshInput> candidate_meshes;
  candidate_assets.reserve(inputs.size() * 2U);
  candidate_meshes.reserve(inputs.size());
  std::map<std::uint64_t, std::size_t> asset_indices;
  std::set<std::uint64_t> object_ids;
  std::map<std::string, std::uint64_t, std::less<>> entity_object_ids;
  std::set<std::string, std::less<>> current_asset_keys;
  std::set<std::string, std::less<>> current_object_keys;

  for (std::size_t input_index = 0U; input_index < inputs.size();
       ++input_index) {
    const Ogre14GraphicsSceneStaticSectionCaptureInput &input =
        inputs[input_index];
    if (input.exact_entity_name.empty() ||
        input.exact_entity_name.find('\0') != std::string::npos) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER,
          "static_meshes.exact_entity_name",
          "managed terrain Entity requires an exact nonempty NUL-free name",
          input_index);
    }
    const auto entity_identity =
        entity_object_ids.emplace(input.exact_entity_name,
                                  input.stable_object_id);
    if (!entity_identity.second &&
        entity_identity.first->second != input.stable_object_id) {
      return ValidationResult::Failure(
          ValidationCode::DUPLICATE_IDENTIFIER,
          "static_meshes.exact_entity_name",
          "one exact managed Entity name identifies multiple static objects",
          input_index);
    }
    if (input.mesh_payload == nullptr ||
        input.mesh_payload->valueless_by_exception() ||
        RenderAssetPayloadKind(*input.mesh_payload) != RenderAssetKind::MESH) {
      return ValidationResult::Failure(
          input.mesh_payload == nullptr ? ValidationCode::EMPTY_PAYLOAD
                                        : ValidationCode::WRONG_ASSET_KIND,
          "static_meshes.mesh_payload",
          "static section requires an immutable mesh payload", input_index);
    }
    const MeshResourceDescriptor &mesh =
        std::get<MeshResourceDescriptor>(*input.mesh_payload);
    ValidationResult validation = ValidateMeshResourceDescriptor(mesh);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    if (mesh.dynamic || !mesh.velocities.empty()) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "static_meshes.mesh_payload.dynamic",
          "static MeshObject inventory cannot contain deformable streams",
          input_index);
    }
    if (input.mesh_identity.reverse_winding !=
        (input.material.cull ==
         Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE)) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "static_meshes.mesh_winding",
          "mesh winding conversion does not match the material front face",
          input_index);
    }
    if (!HasInvertibleAffineTransform(input.render_from_object)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "static_meshes.render_from_object",
          "static MeshObject transform must be finite affine and invertible",
          input_index);
    }
    if (HasMirroredLinearTransform(input.render_from_object)) {
      return ValidationResult::Failure(
          ValidationCode::UNSUPPORTED_FEATURE,
          "static_meshes.render_from_object.mirrored",
          "mirrored MeshObject transforms require canonical mesh rebasing",
          input_index);
    }

    const std::string mesh_key = BuildMeshAssetKey(input.mesh_identity);
    const std::string material_key = BuildMaterialAssetKey(
        input.material.exact_resource_group, input.material.exact_name);
    const std::string object_key =
        BuildStaticObjectKey(input.stable_object_id, input.section_index);
    std::uint64_t mesh_id = 0U;
    std::uint64_t material_id = 0U;
    std::uint64_t object_id = 0U;
    validation = DeriveOgre14GraphicsSceneMeshAssetId(input.mesh_identity,
                                                       mesh_id);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    validation = DeriveOgre14GraphicsSceneMaterialAssetId(
        input.material.exact_resource_group, input.material.exact_name,
        material_id);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    validation = DeriveOgre14GraphicsSceneStaticSectionId(
        input.stable_object_id, input.section_index, object_id);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }

    const auto reject_resurrection = [&](const std::string &key,
                                         bool object) -> ValidationResult {
      const auto &known = object ? identity_registry.known_object_keys_
                                 : identity_registry.known_asset_keys_;
      const auto &live = object ? identity_registry.live_object_keys_
                                : identity_registry.live_asset_keys_;
      if (known.find(key) != known.end() && live.find(key) == live.end()) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH,
            object ? "static_meshes.source_object_id"
                   : "assets.source_asset_id",
            object ? "a removed static-section identity may never return"
                   : "a removed static-asset identity may never return");
      }
      return ValidationResult::Success();
    };
    validation = reject_resurrection(mesh_key, false);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    validation = reject_resurrection(material_key, false);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    validation = reject_resurrection(object_key, true);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }

    validation = candidate_registry.RegisterDerivedAssetIdentity(mesh_key,
                                                                  mesh_id);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    validation = candidate_registry.RegisterDerivedAssetIdentity(
        material_key, material_id);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    validation = candidate_registry.RegisterDerivedObjectIdentity(object_key,
                                                                   object_id);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }

    MaterialDescriptor material;
    validation = BuildOgre14GraphicsSceneMaterialFallback(input.material,
                                                          material);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    validation = ValidateMaterialMeshCompatibility(material, mesh);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    std::shared_ptr<const RenderAssetPayload> material_payload =
        std::make_shared<const RenderAssetPayload>(std::move(material));

    const auto canonicalize =
        [&candidate_registry](
            const std::string &key,
            std::shared_ptr<const RenderAssetPayload> proposed) {
          const auto prior =
              candidate_registry.canonical_payloads_by_asset_key_.find(key);
          if (prior !=
                  candidate_registry.canonical_payloads_by_asset_key_.end() &&
              EquivalentRenderAssetPayload(*prior->second, *proposed)) {
            return prior->second;
          }
          candidate_registry.canonical_payloads_by_asset_key_[key] = proposed;
          return proposed;
        };
    const std::shared_ptr<const RenderAssetPayload> canonical_mesh =
        canonicalize(mesh_key, input.mesh_payload);
    const std::shared_ptr<const RenderAssetPayload> canonical_material =
        canonicalize(material_key, std::move(material_payload));

    const auto add_asset = [&](std::uint64_t source_id,
                               const std::shared_ptr<const RenderAssetPayload>
                                   &payload_owner) -> ValidationResult {
      const auto existing = asset_indices.find(source_id);
      if (existing != asset_indices.end()) {
        const GraphicsSceneAssetInput &prior =
            candidate_assets[existing->second];
        if (!EquivalentRenderAssetPayload(*prior.payload, *payload_owner)) {
          return ValidationResult::Failure(
              ValidationCode::REVISION_MISMATCH, "assets.payload",
              "one exact OGRE 14 asset key produced conflicting payloads");
        }
        return ValidationResult::Success();
      }
      GraphicsSceneAssetInput asset;
      asset.source_asset_id = source_id;
      asset.payload = payload_owner;
      asset_indices.emplace(source_id, candidate_assets.size());
      candidate_assets.push_back(std::move(asset));
      return ValidationResult::Success();
    };
    validation = add_asset(mesh_id, canonical_mesh);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    validation = add_asset(material_id, canonical_material);
    if (!validation) {
      return AtStaticSection(std::move(validation), input_index);
    }
    if (!object_ids.insert(object_id).second ||
        !current_object_keys.insert(object_key).second) {
      return ValidationResult::Failure(
          ValidationCode::DUPLICATE_IDENTIFIER,
          "static_meshes.source_object_id",
          "static-section identity is duplicated", input_index);
    }
    current_asset_keys.insert(mesh_key);
    current_asset_keys.insert(material_key);

    GraphicsSceneStaticMeshInput instance;
    instance.source_object_id = object_id;
    instance.mesh_source_asset_id = mesh_id;
    instance.material_source_asset_id = material_id;
    instance.render_from_object = input.render_from_object;
    instance.visibility_mask = input.visible ? input.visibility_mask : 0U;
    instance.flags = 0U;
    if (input.casts_shadows) {
      instance.flags |= MESH_INSTANCE_CASTS_SHADOW;
    }
    if (input.receives_shadows) {
      instance.flags |= MESH_INSTANCE_RECEIVES_SHADOW;
    }
    if (input.visible_in_reflections) {
      instance.flags |= MESH_INSTANCE_VISIBLE_IN_REFLECTIONS;
    }
    candidate_meshes.push_back(std::move(instance));
  }

  std::sort(candidate_assets.begin(), candidate_assets.end(),
            [](const GraphicsSceneAssetInput &lhs,
               const GraphicsSceneAssetInput &rhs) {
              return lhs.source_asset_id < rhs.source_asset_id;
            });
  std::sort(candidate_meshes.begin(), candidate_meshes.end(),
            [](const GraphicsSceneStaticMeshInput &lhs,
               const GraphicsSceneStaticMeshInput &rhs) {
              return lhs.source_object_id < rhs.source_object_id;
            });
  candidate_registry.known_asset_keys_.insert(current_asset_keys.begin(),
                                               current_asset_keys.end());
  candidate_registry.known_object_keys_.insert(current_object_keys.begin(),
                                                current_object_keys.end());
  candidate_registry.live_asset_keys_ = std::move(current_asset_keys);
  candidate_registry.live_object_keys_ = std::move(current_object_keys);

  identity_registry = std::move(candidate_registry);
  assets = std::move(candidate_assets);
  static_meshes = std::move(candidate_meshes);
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneEnvironment(
    const Float3 &native_ambient_linear,
    SceneEnvironmentDescriptor &environment) {
  if (!IsFinite(native_ambient_linear)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "environment.ambient_radiance",
        "OGRE 14 ambient color must be finite");
  }
  if (!IsNonNegative(native_ambient_linear)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE,
        "environment.ambient_radiance",
        "OGRE 14 ambient color must be nonnegative");
  }

  SceneEnvironmentDescriptor candidate;
  candidate.ambient_radiance = {
      native_ambient_linear.x * kOgre14AmbientNativeUnitRadiance,
      native_ambient_linear.y * kOgre14AmbientNativeUnitRadiance,
      native_ambient_linear.z * kOgre14AmbientNativeUnitRadiance};
  candidate.environment_intensity = 1.0F;
  candidate.analytic_sky = {};
  candidate.exposure_compensation_ev = 0.0F;
  environment = candidate;
  return ValidationResult::Success();
}

ValidationResult Ogre14GraphicsSceneLightIdentityRegistry::
    RegisterDerivedIdentity(std::string_view exact_name,
                            std::uint64_t stable_id) {
  if (exact_name.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.exact_name",
        "OGRE 14 light name must not be empty");
  }
  if (stable_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.source_light_id",
        "derived OGRE 14 light identity must be nonzero");
  }

  const auto id_match = names_by_id_.find(stable_id);
  if (id_match != names_by_id_.end() && id_match->second != exact_name) {
    return ValidationResult::Failure(
        ValidationCode::DUPLICATE_IDENTIFIER, "lights.source_light_id",
        "distinct exact OGRE 14 light names collided on one stable identity");
  }
  const auto name_match = ids_by_name_.find(exact_name);
  if (name_match != ids_by_name_.end() && name_match->second != stable_id) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "lights.exact_name",
        "an exact OGRE 14 light name changed stable identity");
  }
  if (id_match != names_by_id_.end()) {
    return ValidationResult::Success();
  }

  auto inserted_name = names_by_id_.emplace(stable_id, exact_name);
  try {
    const auto inserted_id = ids_by_name_.emplace(exact_name, stable_id);
    if (!inserted_id.second) {
      names_by_id_.erase(inserted_name.first);
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH, "lights.exact_name",
          "an exact OGRE 14 light name changed stable identity");
    }
  } catch (...) {
    names_by_id_.erase(inserted_name.first);
    throw;
  }
  return ValidationResult::Success();
}

ValidationResult DeriveOgre14GraphicsSceneLightId(
    std::string_view exact_name, std::uint64_t &stable_id) {
  if (exact_name.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.exact_name",
        "OGRE 14 light name must not be empty");
  }

  std::uint64_t candidate = kFnv1a64OffsetBasis;
  for (std::size_t index = 0U;
       index + 1U < sizeof(kOgre14LightIdentityDomain); ++index) {
    HashByte(candidate,
             static_cast<std::uint8_t>(kOgre14LightIdentityDomain[index]));
  }
  HashByte(candidate, 0U);
  for (const char byte : exact_name) {
    HashByte(candidate,
             static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
  }
  if (candidate == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.source_light_id",
        "exact OGRE 14 light name hashed to the reserved zero identity");
  }
  stable_id = candidate;
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneLight(
    const Ogre14GraphicsSceneLightCaptureInput &input,
    GraphicsSceneLightInput &light) {
  if (input.exact_name.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "lights.exact_name",
        "OGRE 14 light name must not be empty");
  }
  if (!IsKnownLightKind(input.kind)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "lights.type",
        "unknown OGRE 14 light type");
  }
  if (input.kind == Ogre14GraphicsSceneLightKind::RECTANGLE) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "lights.type",
        "OGRE 14 rectangle lights have no portable scene-schema type");
  }
  if (!IsFinite(input.diffuse_linear) ||
      !IsFinite(input.specular_linear) || !IsFinite(input.power_scale) ||
      !IsFinite(input.derived_position) ||
      !IsFinite(input.derived_direction) ||
      !IsFinite(input.attenuation_range) ||
      !IsFinite(input.attenuation_constant) ||
      !IsFinite(input.attenuation_linear) ||
      !IsFinite(input.attenuation_quadratic) ||
      !IsFinite(input.inner_cone_radians) ||
      !IsFinite(input.outer_cone_radians) ||
      !IsFinite(input.spot_falloff)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "lights.native_state",
        "all captured OGRE 14 light values must be finite");
  }
  if (!IsNonNegative(input.diffuse_linear) ||
      !IsNonNegative(input.specular_linear) || input.power_scale < 0.0F ||
      input.attenuation_range < 0.0F ||
      input.attenuation_constant < 0.0F ||
      input.attenuation_linear < 0.0F ||
      input.attenuation_quadratic < 0.0F || input.spot_falloff < 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lights.native_state",
        "OGRE 14 light photometry and attenuation must be nonnegative");
  }

  GraphicsSceneLightInput candidate;
  ValidationResult identity = DeriveOgre14GraphicsSceneLightId(
      input.exact_name, candidate.source_light_id);
  if (!identity) {
    return identity;
  }
  if (!NormalizePhotometricColorLinear(input.diffuse_linear,
                                       candidate.color_linear)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lights.diffuse_linear",
        "OGRE 14 diffuse RGB must have positive finite Rec.709 luminance");
  }

  const double native_luminance =
      ComputeLinearSrgbRec709D65Luminance(input.diffuse_linear);
  const double calibrated_intensity =
      native_luminance * static_cast<double>(input.power_scale) *
      static_cast<double>(
          kOgre14LegacyDiffusePowerToCanonicalIntensity);
  if (!std::isfinite(calibrated_intensity) ||
      calibrated_intensity >
          static_cast<double>((std::numeric_limits<float>::max)())) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lights.intensity",
        "calibrated OGRE 14 light intensity is not representable");
  }
  const float active_intensity = static_cast<float>(calibrated_intensity);
  if (calibrated_intensity > 0.0 && active_intensity == 0.0F) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lights.intensity",
        "calibrated OGRE 14 light intensity underflows binary32");
  }
  candidate.intensity = input.visible ? active_intensity : 0.0F;
  candidate.shadow_flags = input.visible && input.casts_shadows
                               ? LIGHT_SHADOW_DEFAULT_FLAGS
                               : 0U;

  switch (input.kind) {
  case Ogre14GraphicsSceneLightKind::DIRECTIONAL:
    if (!IsNormalized(input.derived_direction)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.direction",
          "OGRE 14 directional-light direction must be unit length");
    }
    candidate.type = LightType::DIRECTIONAL;
    candidate.position = {};
    candidate.direction = input.derived_direction;
    candidate.range = 0.0F;
    candidate.inner_cone_radians = 0.0F;
    candidate.outer_cone_radians = 0.0F;
    break;
  case Ogre14GraphicsSceneLightKind::POINT:
    if (!(input.attenuation_range > 0.0F) ||
        !(input.attenuation_constant > 0.0F ||
          input.attenuation_linear > 0.0F ||
          input.attenuation_quadratic > 0.0F)) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.range",
          "OGRE 14 local-light range and attenuation denominator must be "
          "positive");
    }
    candidate.type = LightType::POINT;
    candidate.position = input.derived_position;
    candidate.direction = {0.0F, -1.0F, 0.0F};
    candidate.range = input.attenuation_range;
    candidate.inner_cone_radians = 0.0F;
    candidate.outer_cone_radians = 0.0F;
    break;
  case Ogre14GraphicsSceneLightKind::SPOT: {
    constexpr float kPi = 3.14159265358979323846F;
    if (!(input.attenuation_range > 0.0F) ||
        !(input.attenuation_constant > 0.0F ||
          input.attenuation_linear > 0.0F ||
          input.attenuation_quadratic > 0.0F) ||
        !IsNormalized(input.derived_direction) ||
        input.inner_cone_radians < 0.0F ||
        input.outer_cone_radians < input.inner_cone_radians ||
        input.outer_cone_radians > kPi) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "lights.cone",
          "OGRE 14 spot range/direction/full cones are invalid");
    }
    candidate.type = LightType::SPOT;
    candidate.position = input.derived_position;
    candidate.direction = input.derived_direction;
    candidate.range = input.attenuation_range;
    candidate.inner_cone_radians = input.inner_cone_radians * 0.5F;
    candidate.outer_cone_radians = input.outer_cone_radians * 0.5F;
    break;
  }
  case Ogre14GraphicsSceneLightKind::RECTANGLE:
    break;
  }

  light = candidate;
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneLights(
    const std::vector<Ogre14GraphicsSceneLightCaptureInput> &inputs,
    Ogre14GraphicsSceneLightIdentityRegistry &identity_registry,
    std::vector<GraphicsSceneLightInput> &lights) {
  Ogre14GraphicsSceneLightIdentityRegistry candidate_registry =
      identity_registry;
  std::vector<GraphicsSceneLightInput> candidate_lights;
  candidate_lights.reserve(inputs.size());
  std::set<std::string, std::less<>> current_names;

  for (std::size_t index = 0U; index < inputs.size(); ++index) {
    const Ogre14GraphicsSceneLightCaptureInput &input = inputs[index];
    if (!current_names.emplace(input.exact_name).second) {
      return ValidationResult::Failure(
          ValidationCode::DUPLICATE_IDENTIFIER, "lights.exact_name",
          "complete OGRE 14 light inventory contains a duplicate exact name",
          index);
    }
    GraphicsSceneLightInput converted;
    ValidationResult validation =
        BuildOgre14GraphicsSceneLight(input, converted);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    validation = candidate_registry.RegisterDerivedIdentity(
        input.exact_name, converted.source_light_id);
    if (!validation) {
      validation.element_index = index;
      return validation;
    }
    candidate_lights.push_back(converted);
  }

  std::sort(candidate_lights.begin(), candidate_lights.end(),
            [](const GraphicsSceneLightInput &lhs,
               const GraphicsSceneLightInput &rhs) {
              return lhs.source_light_id < rhs.source_light_id;
            });
  identity_registry = std::move(candidate_registry);
  lights = std::move(candidate_lights);
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneCamera(
    const Ogre14CameraCaptureInput &input,
    GraphicsSceneCameraInput &camera) {
  if (!IsKnownProjection(input.projection)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "camera.projection",
        "unknown OGRE 14 camera projection kind");
  }
  if (input.view_id == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "camera.view_id",
        "main camera view identity must be nonzero");
  }
  if (input.width == 0U || input.height == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "camera.dimensions",
        "main camera viewport dimensions must be nonzero");
  }
  if (!IsFinite(input.view_from_render) || !IsFinite(input.left) ||
      !IsFinite(input.right) || !IsFinite(input.top) ||
      !IsFinite(input.bottom) || !IsFinite(input.near_plane) ||
      !IsFinite(input.far_plane) || !IsFinite(input.exposure)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "camera",
        "OGRE 14 camera values must be finite");
  }
  if (!(input.left < input.right) || !(input.bottom < input.top)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "camera.frustum_extents",
        "camera frustum extents must be strictly ordered");
  }
  if (!(input.near_plane > 0.0F) ||
      !(input.far_plane > input.near_plane) ||
      !(input.exposure > 0.0F) || input.visibility_mask == 0U) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "camera",
        "camera clipping, exposure, and visibility must be positive");
  }

  GraphicsSceneCameraInput candidate;
  candidate.view_id = input.view_id;
  candidate.width = input.width;
  candidate.height = input.height;
  candidate.view_from_render = input.view_from_render;
  candidate.near_plane = input.near_plane;
  candidate.far_plane = input.far_plane;
  candidate.exposure = input.exposure;
  candidate.visibility_mask = input.visibility_mask;
  candidate.clip_from_view.elements.fill(0.0F);

  const float width = input.right - input.left;
  const float height = input.top - input.bottom;
  if (input.projection == Ogre14CameraProjectionKind::PERSPECTIVE) {
    candidate.clip_from_view.elements[0U] =
        2.0F * input.near_plane / width;
    candidate.clip_from_view.elements[5U] =
        2.0F * input.near_plane / height;
    candidate.clip_from_view.elements[8U] =
        (input.right + input.left) / width;
    candidate.clip_from_view.elements[9U] =
        (input.top + input.bottom) / height;
    candidate.clip_from_view.elements[10U] =
        input.far_plane / (input.near_plane - input.far_plane);
    candidate.clip_from_view.elements[11U] = -1.0F;
    candidate.clip_from_view.elements[14U] =
        input.near_plane * candidate.clip_from_view.elements[10U];
  } else {
    candidate.clip_from_view.elements[0U] = 2.0F / width;
    candidate.clip_from_view.elements[5U] = 2.0F / height;
    candidate.clip_from_view.elements[10U] =
        1.0F / (input.near_plane - input.far_plane);
    candidate.clip_from_view.elements[12U] =
        -(input.right + input.left) / width;
    candidate.clip_from_view.elements[13U] =
        -(input.top + input.bottom) / height;
    candidate.clip_from_view.elements[14U] =
        input.near_plane * candidate.clip_from_view.elements[10U];
    candidate.clip_from_view.elements[15U] = 1.0F;
  }

  CameraViewRequest validation_view;
  validation_view.view_id = candidate.view_id;
  validation_view.width = candidate.width;
  validation_view.height = candidate.height;
  validation_view.view_from_render = candidate.view_from_render;
  validation_view.clip_from_view = candidate.clip_from_view;
  validation_view.previous_view_from_render = candidate.view_from_render;
  validation_view.previous_clip_from_view = candidate.clip_from_view;
  validation_view.temporal_jitter_pixels =
      candidate.temporal_jitter_pixels;
  validation_view.near_plane = candidate.near_plane;
  validation_view.far_plane = candidate.far_plane;
  validation_view.exposure = candidate.exposure;
  validation_view.visibility_mask = candidate.visibility_mask;
  const ValidationResult validation =
      ValidateCameraViewRequest(validation_view);
  if (!validation) {
    return validation;
  }
  camera = candidate;
  return ValidationResult::Success();
}

} // namespace RoR::Render
