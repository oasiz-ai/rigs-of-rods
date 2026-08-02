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
#include <cstring>
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

constexpr std::array<RequiredField, 10U> kRequiredFields{{
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
constexpr char kOgre14TerrainPageIdentityDomain[] =
    "ror.ogre14.terrain.page.v1";
constexpr char kOgre14TerrainGeometryStateDomain[] =
    "ror.ogre14.terrain.geometry.state.v1";
constexpr std::uint32_t kOgre14MaximumPortableTerrainPageSize = 2049U;

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

void AppendI32(std::string &bytes, std::int32_t value) {
  AppendU32(bytes, static_cast<std::uint32_t>(value));
}

void AppendFloat(std::string &bytes, float value) {
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "terrain capture requires binary32 float storage");
  static_assert(std::numeric_limits<float>::is_iec559,
                "terrain capture requires IEEE-754 float semantics");
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(bytes, bits);
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

std::string BuildTerrainPageKey(
    const Ogre14GraphicsSceneTerrainPageIdentity &identity) {
  std::string key;
  key.reserve(sizeof(kOgre14TerrainPageIdentityDomain) +
              identity.exact_resource_group.size() +
              identity.exact_filename_prefix.size() +
              identity.exact_filename_extension.size() +
              identity.exact_slot_filename.size() + 48U);
  key.append(kOgre14TerrainPageIdentityDomain,
             sizeof(kOgre14TerrainPageIdentityDomain) - 1U);
  key.push_back('\0');
  AppendString(key, identity.exact_resource_group);
  AppendString(key, identity.exact_filename_prefix);
  AppendString(key, identity.exact_filename_extension);
  AppendString(key, identity.exact_slot_filename);
  AppendI32(key, identity.slot_x);
  AppendI32(key, identity.slot_y);
  return key;
}

void AppendReadablePart(std::string &output, std::string_view value) {
  output += std::to_string(value.size());
  output.push_back(':');
  output.append(value.data(), value.size());
}

std::string BuildTerrainPageMeshName(
    const Ogre14GraphicsSceneTerrainPageIdentity &identity) {
  std::string name = "terrain-page/";
  AppendReadablePart(name, identity.exact_resource_group);
  name.push_back('/');
  AppendReadablePart(name, identity.exact_filename_prefix);
  name.push_back('/');
  AppendReadablePart(name, identity.exact_filename_extension);
  name.push_back('/');
  AppendReadablePart(name, identity.exact_slot_filename);
  name += "/slot(" + std::to_string(identity.slot_x) + "," +
          std::to_string(identity.slot_y) + ")/lod0";
  return name;
}

std::string BuildTerrainPageDebugName(
    const Ogre14GraphicsSceneTerrainPageIdentity &identity) {
  return "terrain[" + std::to_string(identity.slot_x) + "," +
         std::to_string(identity.slot_y) + "]/lod0";
}

bool IsKnownTerrainAlignment(
    Ogre14GraphicsSceneTerrainAlignment alignment) noexcept {
  switch (alignment) {
  case Ogre14GraphicsSceneTerrainAlignment::X_Z:
  case Ogre14GraphicsSceneTerrainAlignment::X_Y:
  case Ogre14GraphicsSceneTerrainAlignment::Y_Z:
    return true;
  }
  return false;
}

bool IsKnownMaterialCull(
    Ogre14GraphicsSceneMaterialCull cull) noexcept;

bool IsPowerOfTwo(std::uint32_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

Float3 Add(const Float3 &lhs, const Float3 &rhs) noexcept {
  return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Float3 Subtract(const Float3 &lhs, const Float3 &rhs) noexcept {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Float3 Scale(const Float3 &value, float scale) noexcept {
  return {value.x * scale, value.y * scale, value.z * scale};
}

float DotProduct(const Float3 &lhs, const Float3 &rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Float3 CrossProduct(const Float3 &lhs, const Float3 &rhs) noexcept {
  return {lhs.y * rhs.z - lhs.z * rhs.y,
          lhs.z * rhs.x - lhs.x * rhs.z,
          lhs.x * rhs.y - lhs.y * rhs.x};
}

bool Normalize(Float3 &value) noexcept {
  const float length_squared = DotProduct(value, value);
  if (!std::isfinite(length_squared) || length_squared <= 0.0F) {
    return false;
  }
  const float inverse_length = 1.0F / std::sqrt(length_squared);
  value = Scale(value, inverse_length);
  return IsFinite(value);
}

bool NormalizeFaceOrKeepZero(Float3 &value) noexcept {
  const float length_squared = DotProduct(value, value);
  if (!std::isfinite(length_squared) || length_squared < 0.0F) {
    return false;
  }
  if (length_squared == 0.0F) {
    value = {};
    return true;
  }
  const float inverse_length = 1.0F / std::sqrt(length_squared);
  value = Scale(value, inverse_length);
  return IsFinite(value);
}

Float3 TerrainPointFromHeight(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    std::uint32_t x, std::uint32_t y, float height) noexcept {
  const float scale = input.world_size / static_cast<float>(input.size - 1U);
  const float base = input.world_size * -0.5F;
  switch (input.alignment) {
  case Ogre14GraphicsSceneTerrainAlignment::X_Z:
    return {static_cast<float>(x) * scale + base, height,
            static_cast<float>(y) * -scale - base};
  case Ogre14GraphicsSceneTerrainAlignment::X_Y:
    return {static_cast<float>(x) * scale + base,
            static_cast<float>(y) * scale + base, height};
  case Ogre14GraphicsSceneTerrainAlignment::Y_Z:
    return {height, static_cast<float>(y) * scale + base,
            static_cast<float>(x) * -scale - base};
  }
  return {};
}

Float3 TerrainSkirtOffset(
    Ogre14GraphicsSceneTerrainAlignment alignment,
    float skirt_size) noexcept {
  switch (alignment) {
  case Ogre14GraphicsSceneTerrainAlignment::X_Z:
    return {0.0F, -skirt_size, 0.0F};
  case Ogre14GraphicsSceneTerrainAlignment::X_Y:
    return {0.0F, 0.0F, -skirt_size};
  case Ogre14GraphicsSceneTerrainAlignment::Y_Z:
    return {-skirt_size, 0.0F, 0.0F};
  }
  return {};
}

ValidationResult AtTerrainPage(ValidationResult result, std::size_t index) {
  if (!result) {
    result.element_index = index;
    result.field = "terrain.pages." + result.field;
  }
  return result;
}

ValidationResult ValidateTerrainPageIdentity(
    const Ogre14GraphicsSceneTerrainPageIdentity &identity) {
  const std::array<std::string_view, 4U> strings{{
      identity.exact_resource_group, identity.exact_filename_prefix,
      identity.exact_filename_extension, identity.exact_slot_filename}};
  for (const std::string_view value : strings) {
    if (value.find('\0') != std::string_view::npos) {
      return ValidationResult::Failure(
          ValidationCode::INVALID_IDENTIFIER, "identity",
          "terrain page source strings must be NUL-free");
    }
  }
  if (identity.exact_filename_prefix.empty()) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "identity.filename_prefix",
        "terrain group filename prefix must identify its page source");
  }
  constexpr std::int32_t kMinimumOgreSlot = -32768;
  constexpr std::int32_t kMaximumOgreSlot = 32767;
  if (identity.slot_x < kMinimumOgreSlot ||
      identity.slot_x > kMaximumOgreSlot ||
      identity.slot_y < kMinimumOgreSlot ||
      identity.slot_y > kMaximumOgreSlot) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "identity.slot",
        "terrain slot coordinates exceed OGRE's signed 16-bit grid");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateTerrainGeometryInput(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input) {
  if (input.version != kOgre14TerrainCpuCaptureVersion) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_VERSION, "version",
        "unsupported OGRE 14 terrain CPU capture version");
  }
  ValidationResult validation = ValidateTerrainPageIdentity(input.identity);
  if (!validation) {
    return validation;
  }
  if (!IsKnownTerrainAlignment(input.alignment)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "alignment",
        "terrain page has an unknown OGRE alignment");
  }
  if (!IsKnownMaterialCull(input.material.cull)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "material.cull",
        "terrain material has an unknown cull mode");
  }
  if (input.size < 3U || input.size > kOgre14MaximumPortableTerrainPageSize ||
      !IsPowerOfTwo(input.size - 1U)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "size",
        "terrain page size must be 2^n+1 and no larger than 2049");
  }
  if (input.minimum_batch_size < 3U ||
      input.maximum_batch_size < input.minimum_batch_size ||
      input.maximum_batch_size > input.size ||
      !IsPowerOfTwo(input.minimum_batch_size - 1U) ||
      !IsPowerOfTwo(input.maximum_batch_size - 1U) ||
      (input.maximum_batch_size - 1U) %
              (input.minimum_batch_size - 1U) !=
          0U ||
      (input.size - 1U) % (input.maximum_batch_size - 1U) != 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_DIMENSIONS, "lod.batch_layout",
        "terrain min/max batches must be compatible 2^n+1 divisors");
  }
  std::uint32_t expected_leaf_lods = 1U;
  for (std::uint32_t ratio =
           (input.maximum_batch_size - 1U) /
           (input.minimum_batch_size - 1U);
       ratio > 1U; ratio >>= 1U) {
    ++expected_leaf_lods;
  }
  std::uint32_t expected_tree_depth = 0U;
  for (std::uint32_t ratio =
           (input.size - 1U) / (input.maximum_batch_size - 1U);
       ratio > 1U; ratio >>= 1U) {
    ++expected_tree_depth;
  }
  if (input.lod_levels_per_leaf != expected_leaf_lods ||
      input.lod_level_count != expected_leaf_lods + expected_tree_depth) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "lod.layout",
        "terrain LOD counts do not match its authored grid and batches");
  }
  if (input.highest_lod_prepared != 0) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "lod.full_resolution",
        "terrain capture requires complete CPU LOD0 height data");
  }
  if (input.highest_lod_loaded < 0 ||
      input.highest_lod_loaded >
          static_cast<std::int32_t>(input.lod_level_count) ||
      input.target_lod_level < 0 ||
      input.target_lod_level >=
          static_cast<std::int32_t>(input.lod_level_count)) {
    return ValidationResult::Failure(
        ValidationCode::VALUE_OUT_OF_RANGE, "lod.native_draw_state",
        "terrain loaded and target LOD metadata is outside OGRE bounds");
  }
  if (input.derived_data_update_in_progress) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "derived_data",
        "terrain derived data is changing during the capture boundary");
  }
  if (input.has_holes) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "holes",
        "terrain holes require explicit cut topology");
  }
  if (!std::isfinite(input.world_size) || input.world_size <= 0.0F ||
      !std::isfinite(input.skirt_size) || input.skirt_size < 0.0F ||
      !IsFinite(input.page_world_position)) {
    return ValidationResult::Failure(
        ValidationCode::NON_FINITE_VALUE, "geometry",
        "terrain world size, skirt, and page transform must be finite");
  }
  const std::size_t side = input.size;
  const std::size_t halo_side = side + 2U;
  if (side > (std::numeric_limits<std::size_t>::max)() / side ||
      halo_side > (std::numeric_limits<std::size_t>::max)() / halo_side ||
      input.height_samples.size() != side * side ||
      input.normal_neighbourhood_positions.size() !=
          halo_side * halo_side) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "height_grid",
        "terrain height and one-cell neighbourhood extents must be exact");
  }
  const float tolerance =
      (std::max)(1.0e-5F, input.world_size * 2.0e-6F);
  for (std::uint32_t y = 0U; y < input.size; ++y) {
    for (std::uint32_t x = 0U; x < input.size; ++x) {
      const std::size_t sample_index =
          static_cast<std::size_t>(y) * side + x;
      const std::size_t halo_index =
          static_cast<std::size_t>(y + 1U) * halo_side + x + 1U;
      const float height = input.height_samples[sample_index];
      const Float3 &captured =
          input.normal_neighbourhood_positions[halo_index];
      if (!std::isfinite(height) || !IsFinite(captured)) {
        return ValidationResult::Failure(
            ValidationCode::NON_FINITE_VALUE, "height_grid",
            "terrain CPU height and point samples must be finite",
            sample_index);
      }
      const Float3 expected = TerrainPointFromHeight(input, x, y, height);
      if (std::fabs(captured.x - expected.x) > tolerance ||
          std::fabs(captured.y - expected.y) > tolerance ||
          std::fabs(captured.z - expected.z) > tolerance) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH, "height_grid.point_mapping",
            "terrain height samples and aligned point mapping disagree",
            sample_index);
      }
    }
  }
  for (std::size_t index = 0U;
       index < input.normal_neighbourhood_positions.size(); ++index) {
    if (!IsFinite(input.normal_neighbourhood_positions[index])) {
      return ValidationResult::Failure(
          ValidationCode::NON_FINITE_VALUE,
          "normal_neighbourhood_positions",
          "terrain neighbour point samples must be finite", index);
    }
  }
  return ValidationResult::Success();
}

ValidationResult ValidateTerrainMaterialAudit(
    const Ogre14GraphicsSceneTerrainMaterialAuditInput &audit) {
  if (audit.layer_count != audit.layer_world_sizes.size() ||
      (audit.layer_count != 0U &&
       audit.sampler_count >
           (std::numeric_limits<std::size_t>::max)() /
               audit.layer_count) ||
      audit.layer_texture_names.size() !=
          static_cast<std::size_t>(audit.layer_count) *
              audit.sampler_count ||
      audit.blend_texture_names.size() != audit.blend_texture_count) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "material.audit",
        "terrain layer and blend texture inventories are incomplete");
  }
  for (const float world_size : audit.layer_world_sizes) {
    if (!std::isfinite(world_size) || world_size <= 0.0F) {
      return ValidationResult::Failure(
          ValidationCode::VALUE_OUT_OF_RANGE,
          "material.layer_world_sizes",
          "terrain layer world sizes must be finite and positive");
    }
  }
  const auto valid_names = [](const std::vector<std::string> &names) {
    return std::all_of(names.begin(), names.end(), [](const std::string &name) {
      return name.find('\0') == std::string::npos;
    });
  };
  if (!valid_names(audit.layer_texture_names) ||
      !valid_names(audit.blend_texture_names) ||
      audit.exact_global_colour_map_name.find('\0') != std::string::npos ||
      audit.exact_lightmap_name.find('\0') != std::string::npos ||
      audit.exact_composite_map_name.find('\0') != std::string::npos) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "material.texture_names",
        "terrain texture identities must be NUL-free");
  }
  if (audit.global_colour_map_enabled !=
          !audit.exact_global_colour_map_name.empty() ||
      audit.has_lightmap != !audit.exact_lightmap_name.empty() ||
      audit.has_composite_map !=
          !audit.exact_composite_map_name.empty()) {
    return ValidationResult::Failure(
        ValidationCode::REVISION_MISMATCH, "material.texture_presence",
        "terrain texture presence flags and exact names disagree");
  }
  if (audit.layer_count != 0U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.layers",
        "terrain layers require exact texture and blend transport");
  }
  if (audit.blend_texture_count != 0U) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.blend_maps",
        "terrain blend maps require exact texture transport");
  }
  if (audit.global_colour_map_enabled) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE,
        "material.global_colour_map",
        "terrain global colour maps require exact texture transport");
  }
  if (audit.has_lightmap) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.lightmap",
        "terrain lightmaps require exact texture transport");
  }
  if (audit.has_composite_map) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "material.composite_map",
        "terrain composite maps require exact texture transport");
  }
  return ValidationResult::Success();
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
  return ValidationResult::Success();
}

Ogre14GraphicsSceneSource::Ogre14GraphicsSceneSource(
    IOgre14GraphicsSceneCaptureProvider &provider) noexcept
    : provider_(provider) {}

ValidationResult Ogre14GraphicsSceneSource::CaptureJoinedGraphicsFrame(
    GraphicsSceneFrameInput &frame) {
  try {
    Ogre14GraphicsSceneCapture capture;
    ValidationResult validation =
        provider_.CaptureOgre14GraphicsScene(capture);
    if (!validation) {
      return validation;
    }
    validation = ValidateOgre14GraphicsSceneCapture(capture);
    if (!validation) {
      return validation;
    }
    frame = std::move(capture.frame);
    return ValidationResult::Success();
  } catch (const std::exception &) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "joined_graphics_source",
        "OGRE 14 capture provider threw an exception");
  } catch (...) {
    return ValidationResult::Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "joined_graphics_source",
        "OGRE 14 capture provider threw a non-standard exception");
  }
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

ValidationResult Ogre14GraphicsSceneStaticIdentityRegistry::
    RegisterDerivedTerrainPageIdentity(std::string_view exact_key,
                                       std::uint64_t stable_id) {
  return RegisterIdentity(exact_key, stable_id, terrain_page_names_by_id_,
                          terrain_page_ids_by_name_,
                          "terrain.pages.exact_key",
                          "terrain.pages.source_page_id");
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

ValidationResult DeriveOgre14GraphicsSceneTerrainPageId(
    const Ogre14GraphicsSceneTerrainPageIdentity &identity,
    std::uint64_t &stable_id) {
  const ValidationResult validation = ValidateTerrainPageIdentity(identity);
  if (!validation) {
    return validation;
  }
  return HashStableKey(BuildTerrainPageKey(identity),
                       "terrain.pages.source_page_id", stable_id);
}

ValidationResult ValidateOgre14GraphicsSceneTerrainMaterialCapture(
    const Ogre14GraphicsSceneTerrainMaterialAuditInput &audit,
    const Ogre14GraphicsSceneMaterialCaptureInput &material) {
  ValidationResult validation = ValidateTerrainMaterialAudit(audit);
  if (!validation) {
    return validation;
  }
  MaterialDescriptor portable_material;
  return BuildOgre14GraphicsSceneMaterialFallback(material,
                                                   portable_material);
}

ValidationResult ValidateOgre14GraphicsSceneTerrainPageSet(
    const std::vector<Ogre14GraphicsSceneTerrainPageCaptureInput> &pages) {
  std::map<std::pair<std::int32_t, std::int32_t>, std::size_t> slots;
  std::set<std::string, std::less<>> exact_page_keys;
  for (std::size_t index = 0U; index < pages.size(); ++index) {
    ValidationResult validation = ValidateTerrainGeometryInput(pages[index]);
    if (!validation) {
      return AtTerrainPage(std::move(validation), index);
    }
    validation = ValidateOgre14GraphicsSceneTerrainMaterialCapture(
        pages[index].material_audit, pages[index].material);
    if (!validation) {
      return AtTerrainPage(std::move(validation), index);
    }
    const std::string exact_key = BuildTerrainPageKey(pages[index].identity);
    if (!exact_page_keys.insert(exact_key).second ||
        !slots.emplace(std::make_pair(pages[index].identity.slot_x,
                                      pages[index].identity.slot_y),
                       index)
             .second) {
      return ValidationResult::Failure(
          ValidationCode::DUPLICATE_IDENTIFIER,
          "terrain.pages.identity",
          "terrain page inventory contains a duplicate exact slot", index);
    }
    if (index != 0U) {
      const auto &first = pages.front();
      const auto &page = pages[index];
      if (page.identity.exact_resource_group !=
              first.identity.exact_resource_group ||
          page.identity.exact_filename_prefix !=
              first.identity.exact_filename_prefix ||
          page.identity.exact_filename_extension !=
              first.identity.exact_filename_extension ||
          page.alignment != first.alignment || page.size != first.size ||
          page.minimum_batch_size != first.minimum_batch_size ||
          page.maximum_batch_size != first.maximum_batch_size ||
          page.world_size != first.world_size) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH,
            "terrain.pages.group_layout",
            "one TerrainGroup must have one exact source and grid layout",
            index);
      }
    }
  }

  const auto world_point = [](const Ogre14GraphicsSceneTerrainPageCaptureInput
                                  &page,
                              std::uint32_t x, std::uint32_t y) {
    const std::size_t halo_side = page.size + 2U;
    return Add(page.page_world_position,
               page.normal_neighbourhood_positions[
                   static_cast<std::size_t>(y + 1U) * halo_side + x + 1U]);
  };
  const auto edge_matches = [&](const Ogre14GraphicsSceneTerrainPageCaptureInput
                                    &first,
                                const Ogre14GraphicsSceneTerrainPageCaptureInput
                                    &second,
                                bool east_west) {
    const float tolerance =
        (std::max)(1.0e-5F, first.world_size * 2.0e-6F);
    for (std::uint32_t offset = 0U; offset < first.size; ++offset) {
      const Float3 lhs = east_west
                             ? world_point(first, first.size - 1U, offset)
                             : world_point(first, offset, first.size - 1U);
      const Float3 rhs = east_west ? world_point(second, 0U, offset)
                                   : world_point(second, offset, 0U);
      if (std::fabs(lhs.x - rhs.x) > tolerance ||
          std::fabs(lhs.y - rhs.y) > tolerance ||
          std::fabs(lhs.z - rhs.z) > tolerance) {
        return false;
      }
    }
    return true;
  };

  for (const auto &slot : slots) {
    const auto &page = pages[slot.second];
    if (slot.first.first < 32767) {
      const auto east = slots.find(
          {slot.first.first + 1, slot.first.second});
      if (east != slots.end() &&
          !edge_matches(page, pages[east->second], true)) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH,
            "terrain.pages.shared_edge",
            "adjacent east/west terrain page edges do not match",
            slot.second);
      }
    }
    if (slot.first.second < 32767) {
      const auto north = slots.find(
          {slot.first.first, slot.first.second + 1});
      if (north != slots.end() &&
          !edge_matches(page, pages[north->second], false)) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH,
            "terrain.pages.shared_edge",
            "adjacent north/south terrain page edges do not match",
            slot.second);
      }
    }
  }
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneTerrainGeometryStateKey(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    std::string &key) {
  ValidationResult validation = ValidateTerrainGeometryInput(input);
  if (!validation) {
    return validation;
  }
  std::string candidate;
  candidate.reserve(sizeof(kOgre14TerrainGeometryStateDomain) +
                    input.height_samples.size() * sizeof(float) +
                    input.normal_neighbourhood_positions.size() *
                        sizeof(Float3) +
                    80U);
  candidate.append(kOgre14TerrainGeometryStateDomain,
                   sizeof(kOgre14TerrainGeometryStateDomain) - 1U);
  candidate.push_back('\0');
  candidate.push_back(static_cast<char>(input.alignment));
  AppendU32(candidate, input.size);
  AppendU32(candidate, input.minimum_batch_size);
  AppendU32(candidate, input.maximum_batch_size);
  AppendU32(candidate, input.lod_level_count);
  AppendU32(candidate, input.lod_levels_per_leaf);
  AppendFloat(candidate, input.world_size);
  AppendFloat(candidate, input.skirt_size);
  candidate.push_back(
      input.material.cull ==
              Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE
          ? '\1'
          : '\0');
  AppendU64(candidate,
            static_cast<std::uint64_t>(input.height_samples.size()));
  for (const float height : input.height_samples) {
    AppendFloat(candidate, height);
  }
  AppendU64(candidate, static_cast<std::uint64_t>(
                           input.normal_neighbourhood_positions.size()));
  for (const Float3 &position : input.normal_neighbourhood_positions) {
    AppendFloat(candidate, position.x);
    AppendFloat(candidate, position.y);
    AppendFloat(candidate, position.z);
  }
  key = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneTerrainMeshPayload(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    std::uint64_t topology_revision,
    std::shared_ptr<const RenderAssetPayload> &payload) {
  ValidationResult validation = ValidateTerrainGeometryInput(input);
  if (!validation) {
    return validation;
  }
  if (topology_revision == 0U) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_IDENTIFIER, "mesh.topology_revision",
        "terrain mesh topology revision must be nonzero");
  }
  if (!IsKnownMaterialCull(input.material.cull)) {
    return ValidationResult::Failure(
        ValidationCode::INVALID_ENUM, "material.cull",
        "terrain material has an unknown cull mode");
  }

  const std::size_t side = input.size;
  const std::size_t halo_side = side + 2U;
  const std::size_t main_vertex_count = side * side;
  const std::size_t total_vertex_count = main_vertex_count + 4U * side;
  if (total_vertex_count >
      (std::numeric_limits<std::uint32_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "mesh.positions",
        "terrain page vertex count exceeds uint32");
  }

  Ogre14GraphicsSceneCpuMeshSectionInput mesh;
  mesh.debug_name = BuildTerrainPageDebugName(input.identity);
  mesh.index_format = total_vertex_count <= 65536U
                          ? MeshIndexFormat::UINT16
                          : MeshIndexFormat::UINT32;
  mesh.topology_revision = topology_revision;
  mesh.reverse_winding =
      input.material.cull ==
      Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
  mesh.positions.reserve(total_vertex_count);
  mesh.normals.reserve(total_vertex_count);
  mesh.tangents.reserve(total_vertex_count);
  mesh.texture_coordinates_0.reserve(total_vertex_count);

  const auto point = [&](std::int32_t x, std::int32_t y) -> const Float3 & {
    return input.normal_neighbourhood_positions[
        static_cast<std::size_t>(y + 1) * halo_side +
        static_cast<std::size_t>(x + 1)];
  };
  for (std::uint32_t y = 0U; y < input.size; ++y) {
    for (std::uint32_t x = 0U; x < input.size; ++x) {
      const Float3 &centre = point(static_cast<std::int32_t>(x),
                                   static_cast<std::int32_t>(y));
      const std::array<Float3, 8U> adjacent{{
          point(static_cast<std::int32_t>(x) + 1,
                static_cast<std::int32_t>(y)),
          point(static_cast<std::int32_t>(x) + 1,
                static_cast<std::int32_t>(y) + 1),
          point(static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y) + 1),
          point(static_cast<std::int32_t>(x) - 1,
                static_cast<std::int32_t>(y) + 1),
          point(static_cast<std::int32_t>(x) - 1,
                static_cast<std::int32_t>(y)),
          point(static_cast<std::int32_t>(x) - 1,
                static_cast<std::int32_t>(y) - 1),
          point(static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y) - 1),
          point(static_cast<std::int32_t>(x) + 1,
                static_cast<std::int32_t>(y) - 1),
      }};
      Float3 normal{};
      for (std::size_t neighbour = 0U; neighbour < adjacent.size();
           ++neighbour) {
        Float3 face = CrossProduct(
            Subtract(adjacent[neighbour], centre),
            Subtract(adjacent[(neighbour + 1U) % adjacent.size()], centre));
        // Ogre::Math::calculateBasicFaceNormal leaves a zero vector intact.
        // This is required at an outer page boundary, where a missing
        // neighbour makes getPointFromSelfOrNeighbour clamp some samples.
        if (!NormalizeFaceOrKeepZero(face)) {
          return ValidationResult::Failure(
              ValidationCode::NON_FINITE_VALUE, "mesh.normals",
              "terrain normal neighbourhood produced a nonfinite face",
              static_cast<std::size_t>(y) * side + x);
        }
        normal = Add(normal, face);
      }
      if (!Normalize(normal)) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "mesh.normals",
            "terrain normal sum is degenerate",
            static_cast<std::size_t>(y) * side + x);
      }

      Float3 tangent = Subtract(
          point(static_cast<std::int32_t>(x) + 1,
                static_cast<std::int32_t>(y)),
          point(static_cast<std::int32_t>(x) - 1,
                static_cast<std::int32_t>(y)));
      tangent = Subtract(tangent,
                         Scale(normal, DotProduct(normal, tangent)));
      if (!Normalize(tangent)) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "mesh.tangents",
            "terrain increasing-U tangent is degenerate",
            static_cast<std::size_t>(y) * side + x);
      }
      const Float3 increasing_v = Subtract(
          point(static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y) - 1),
          point(static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y) + 1));
      const float handedness_measure =
          DotProduct(CrossProduct(normal, tangent), increasing_v);
      if (!std::isfinite(handedness_measure) ||
          std::fabs(handedness_measure) <= 1.0e-12F) {
        return ValidationResult::Failure(
            ValidationCode::VALUE_OUT_OF_RANGE, "mesh.tangents",
            "terrain UV handedness is degenerate",
            static_cast<std::size_t>(y) * side + x);
      }

      mesh.positions.push_back(centre);
      mesh.normals.push_back(normal);
      mesh.tangents.push_back(
          {tangent.x, tangent.y, tangent.z,
           handedness_measure > 0.0F ? 1.0F : -1.0F});
      mesh.texture_coordinates_0.push_back(
          {static_cast<float>(x) / static_cast<float>(input.size - 1U),
           1.0F - static_cast<float>(y) /
                      static_cast<float>(input.size - 1U)});
    }
  }

  const Float3 skirt_offset =
      TerrainSkirtOffset(input.alignment, input.skirt_size);
  const auto append_skirt_vertex = [&](std::uint32_t main_index) {
    mesh.positions.push_back(Add(mesh.positions[main_index], skirt_offset));
    mesh.normals.push_back(mesh.normals[main_index]);
    mesh.tangents.push_back(mesh.tangents[main_index]);
    mesh.texture_coordinates_0.push_back(
        mesh.texture_coordinates_0[main_index]);
  };
  for (std::uint32_t x = 0U; x < input.size; ++x) {
    append_skirt_vertex(x);
  }
  for (std::uint32_t x = 0U; x < input.size; ++x) {
    append_skirt_vertex((input.size - 1U) * input.size + x);
  }
  for (std::uint32_t y = 0U; y < input.size; ++y) {
    append_skirt_vertex(y * input.size);
  }
  for (std::uint32_t y = 0U; y < input.size; ++y) {
    append_skirt_vertex(y * input.size + input.size - 1U);
  }

  std::vector<std::uint32_t> strip;
  const std::size_t strip_index_count =
      (side * 2U + 1U) * (side - 1U) + (side - 1U) * 8U + 2U;
  strip.reserve(strip_index_count);
  std::int64_t current_vertex = static_cast<std::int64_t>(side - 1U);
  bool right_to_left = true;
  for (std::uint32_t row = 0U; row < input.size - 1U; ++row) {
    for (std::uint32_t column = 0U; column < input.size; ++column) {
      strip.push_back(static_cast<std::uint32_t>(current_vertex));
      strip.push_back(static_cast<std::uint32_t>(current_vertex + side));
      if (column + 1U < input.size) {
        current_vertex += right_to_left ? -1 : 1;
      }
    }
    right_to_left = !right_to_left;
    current_vertex += static_cast<std::int64_t>(side);
    strip.push_back(static_cast<std::uint32_t>(current_vertex));
  }
  const auto skirt_index = [&](std::uint32_t main_index, bool column) {
    const std::uint32_t row = main_index / input.size;
    const std::uint32_t col = main_index % input.size;
    const std::uint32_t base = input.size * input.size;
    if (column) {
      return base + 2U * input.size +
             input.size * (col / (input.size - 1U)) + row;
    }
    return base + input.size * (row / (input.size - 1U)) + col;
  };
  for (std::uint32_t side_index = 0U; side_index < 4U; ++side_index) {
    std::int64_t edge_increment = 0;
    std::int64_t skirt_increment = 0;
    switch (side_index) {
    case 0U:
      edge_increment = -1;
      skirt_increment = -1;
      break;
    case 1U:
      edge_increment = -static_cast<std::int64_t>(side);
      skirt_increment = -1;
      break;
    case 2U:
      edge_increment = 1;
      skirt_increment = 1;
      break;
    case 3U:
      edge_increment = static_cast<std::int64_t>(side);
      skirt_increment = 1;
      break;
    }
    std::int64_t current_skirt = skirt_index(
        static_cast<std::uint32_t>(current_vertex),
        (side_index % 2U) != 0U);
    for (std::uint32_t edge = 0U; edge < input.size - 1U; ++edge) {
      strip.push_back(static_cast<std::uint32_t>(current_vertex));
      strip.push_back(static_cast<std::uint32_t>(current_skirt));
      current_vertex += edge_increment;
      current_skirt += skirt_increment;
    }
    if (side_index == 3U) {
      strip.push_back(static_cast<std::uint32_t>(current_vertex));
      strip.push_back(static_cast<std::uint32_t>(current_skirt));
      current_vertex += edge_increment;
    }
  }
  if (strip.size() != strip_index_count) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "mesh.indices",
        "canonical terrain strip construction changed index count");
  }
  mesh.indices.reserve((strip.size() - 2U) * 3U);
  for (std::size_t index = 2U; index < strip.size(); ++index) {
    std::uint32_t first = strip[index - 2U];
    std::uint32_t second = strip[index - 1U];
    const std::uint32_t third = strip[index];
    if ((index & 1U) != 0U) {
      std::swap(first, second);
    }
    if (first == second || first == third || second == third) {
      continue;
    }
    mesh.indices.push_back(first);
    mesh.indices.push_back(second);
    mesh.indices.push_back(third);
  }
  return BuildOgre14GraphicsSceneStaticMeshPayload(mesh, payload);
}

ValidationResult ResolveOgre14GraphicsSceneTerrainPageCacheEntry(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    const Ogre14GraphicsSceneTerrainPageCacheEntry *previous,
    Ogre14GraphicsSceneTerrainPageCacheEntry &entry) {
  std::string geometry_state_key;
  ValidationResult validation =
      BuildOgre14GraphicsSceneTerrainGeometryStateKey(input,
                                                       geometry_state_key);
  if (!validation) {
    return validation;
  }

  std::uint64_t topology_revision = 1U;
  if (previous != nullptr) {
    if (previous->exact_geometry_state_key.empty() ||
        previous->topology_revision == 0U ||
        previous->mesh_payload == nullptr ||
        previous->mesh_payload->valueless_by_exception() ||
        RenderAssetPayloadKind(*previous->mesh_payload) !=
            RenderAssetKind::MESH) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH, "terrain.pages.cache",
          "prior terrain cache entry is incomplete");
    }
    const MeshResourceDescriptor &prior_mesh =
        std::get<MeshResourceDescriptor>(*previous->mesh_payload);
    validation = ValidateMeshResourceDescriptor(prior_mesh);
    if (!validation) {
      validation.field = "terrain.pages.cache." + validation.field;
      return validation;
    }
    if (prior_mesh.topology_revision != previous->topology_revision) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "terrain.pages.cache.topology_revision",
          "prior terrain cache owner and revision disagree");
    }
    if (previous->exact_geometry_state_key == geometry_state_key) {
      entry = *previous;
      return ValidationResult::Success();
    }
    if (previous->topology_revision ==
        (std::numeric_limits<std::uint64_t>::max)()) {
      return ValidationResult::Failure(
          ValidationCode::REVISION_MISMATCH,
          "terrain.pages.topology_revision",
          "terrain page topology revision would overflow");
    }
    topology_revision = previous->topology_revision + 1U;
  }

  std::shared_ptr<const RenderAssetPayload> payload;
  validation = BuildOgre14GraphicsSceneTerrainMeshPayload(
      input, topology_revision, payload);
  if (!validation) {
    return validation;
  }
  Ogre14GraphicsSceneTerrainPageCacheEntry candidate;
  candidate.exact_geometry_state_key = std::move(geometry_state_key);
  candidate.topology_revision = topology_revision;
  candidate.mesh_payload = std::move(payload);
  entry = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult BuildOgre14GraphicsSceneTerrainSection(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    const std::shared_ptr<const RenderAssetPayload> &mesh_payload,
    Ogre14GraphicsSceneStaticSectionCaptureInput &section) {
  ValidationResult validation = ValidateTerrainGeometryInput(input);
  if (!validation) {
    return validation;
  }
  validation = ValidateOgre14GraphicsSceneTerrainMaterialCapture(
      input.material_audit, input.material);
  if (!validation) {
    return validation;
  }
  MaterialDescriptor portable_material;
  validation = BuildOgre14GraphicsSceneMaterialFallback(input.material,
                                                         portable_material);
  if (!validation) {
    return validation;
  }
  if (mesh_payload == nullptr || mesh_payload->valueless_by_exception() ||
      RenderAssetPayloadKind(*mesh_payload) != RenderAssetKind::MESH) {
    return ValidationResult::Failure(
        ValidationCode::WRONG_ASSET_KIND, "mesh_payload",
        "terrain section requires an immutable mesh payload");
  }
  const MeshResourceDescriptor &mesh =
      std::get<MeshResourceDescriptor>(*mesh_payload);
  validation = ValidateMeshResourceDescriptor(mesh);
  if (!validation) {
    return validation;
  }
  validation = ValidateMaterialMeshCompatibility(portable_material, mesh);
  if (!validation) {
    return validation;
  }
  if (mesh.positions.size() >
          (std::numeric_limits<std::uint32_t>::max)() ||
      mesh.indices.size() >
          (std::numeric_limits<std::uint32_t>::max)()) {
    return ValidationResult::Failure(
        ValidationCode::SIZE_MISMATCH, "mesh_payload",
        "terrain mesh draw ranges exceed uint32");
  }

  Ogre14GraphicsSceneStaticSectionCaptureInput candidate;
  validation = DeriveOgre14GraphicsSceneTerrainPageId(
      input.identity, candidate.stable_object_id);
  if (!validation) {
    return validation;
  }
  candidate.section_index = 0U;
  candidate.exact_entity_name = BuildTerrainPageDebugName(input.identity);
  candidate.mesh_identity.exact_resource_group =
      input.identity.exact_resource_group;
  candidate.mesh_identity.exact_mesh_name =
      BuildTerrainPageMeshName(input.identity);
  candidate.mesh_identity.vertex_start = 0U;
  candidate.mesh_identity.vertex_count =
      static_cast<std::uint32_t>(mesh.positions.size());
  candidate.mesh_identity.index_start = 0U;
  candidate.mesh_identity.index_count =
      static_cast<std::uint32_t>(mesh.indices.size());
  candidate.mesh_identity.reverse_winding =
      input.material.cull ==
      Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
  candidate.mesh_payload = mesh_payload;
  candidate.material = input.material;
  candidate.render_from_object.elements[12U] = input.page_world_position.x;
  candidate.render_from_object.elements[13U] = input.page_world_position.y;
  candidate.render_from_object.elements[14U] = input.page_world_position.z;
  candidate.visibility_mask = input.visibility_mask;
  candidate.visible = input.visible;
  candidate.casts_shadows = input.casts_shadows;
  candidate.receives_shadows = input.receives_shadows;
  candidate.visible_in_reflections = input.visible_in_reflections;
  candidate.exact_terrain_page_key = BuildTerrainPageKey(input.identity);
  section = std::move(candidate);
  return ValidationResult::Success();
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
  std::set<std::string, std::less<>> current_terrain_page_keys;
  std::map<std::string, std::uint64_t, std::less<>> terrain_page_ids;

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
    if (!input.exact_terrain_page_key.empty()) {
      const auto page_identity = terrain_page_ids.emplace(
          input.exact_terrain_page_key, input.stable_object_id);
      if (!page_identity.second &&
          page_identity.first->second != input.stable_object_id) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH,
            "terrain.pages.source_page_id",
            "one exact terrain page key changed stable identity",
            input_index);
      }
      if (identity_registry.known_terrain_page_keys_.find(
              input.exact_terrain_page_key) !=
              identity_registry.known_terrain_page_keys_.end() &&
          identity_registry.live_terrain_page_keys_.find(
              input.exact_terrain_page_key) ==
              identity_registry.live_terrain_page_keys_.end()) {
        return ValidationResult::Failure(
            ValidationCode::REVISION_MISMATCH,
            "terrain.pages.source_page_id",
            "a removed terrain page identity may never return", input_index);
      }
      validation = candidate_registry.RegisterDerivedTerrainPageIdentity(
          input.exact_terrain_page_key, input.stable_object_id);
      if (!validation) {
        return AtStaticSection(std::move(validation), input_index);
      }
      current_terrain_page_keys.insert(input.exact_terrain_page_key);
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
  candidate_registry.known_terrain_page_keys_.insert(
      current_terrain_page_keys.begin(), current_terrain_page_keys.end());
  candidate_registry.live_asset_keys_ = std::move(current_asset_keys);
  candidate_registry.live_object_keys_ = std::move(current_object_keys);
  candidate_registry.live_terrain_page_keys_ =
      std::move(current_terrain_page_keys);

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
