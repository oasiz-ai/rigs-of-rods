/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

#include "Ogre14ProceduralRoadSource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

namespace RoR::Render {
namespace {

constexpr char kProceduralRoadObjectIdentityDomain[] =
    "ror.ogre14.procedural-road.object.v1";
constexpr char kProceduralRoadGeometryStateDomain[] =
    "ror.ogre14.procedural-road.geometry.v1";
constexpr char kProceduralRoadCanonicalMeshGroup[] =
    "RoRProceduralRoadCaptureV1";
constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;
constexpr float kNormalTolerance = 1.0e-3F;

ValidationResult Failure(ValidationCode code, const char *field,
                         const char *detail,
                         std::size_t index = ValidationResult::kNoElement) {
  return ValidationResult::Failure(code, field, detail, index);
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

void AppendFloat(std::string &bytes, float value) {
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "procedural-road capture requires binary32 float storage");
  static_assert(std::numeric_limits<float>::is_iec559,
                "procedural-road capture requires IEEE-754 floats");
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  AppendU32(bytes, bits);
}

void HashByte(std::uint64_t &hash, std::uint8_t byte) noexcept {
  hash ^= byte;
  hash *= kFnv1a64Prime;
}

bool IsUnit(const Float3 &value) noexcept {
  if (!IsFinite(value)) {
    return false;
  }
  const float length_squared =
      value.x * value.x + value.y * value.y + value.z * value.z;
  return std::isfinite(length_squared) &&
         std::fabs(length_squared - 1.0F) <= kNormalTolerance;
}

Float3 Subtract(const Float3 &lhs, const Float3 &rhs) noexcept {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Float3 Cross(const Float3 &lhs, const Float3 &rhs) noexcept {
  return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
          lhs.x * rhs.y - lhs.y * rhs.x};
}

bool FloatBitsEqual(float lhs, float rhs) noexcept {
  std::uint32_t lhs_bits = 0U;
  std::uint32_t rhs_bits = 0U;
  static_assert(sizeof(lhs_bits) == sizeof(lhs), "binary32 is required");
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  return lhs_bits == rhs_bits;
}

bool Float3BitsEqual(const Float3 &lhs, const Float3 &rhs) noexcept {
  return FloatBitsEqual(lhs.x, rhs.x) && FloatBitsEqual(lhs.y, rhs.y) &&
         FloatBitsEqual(lhs.z, rhs.z);
}

bool Float2BitsEqual(const Float2 &lhs, const Float2 &rhs) noexcept {
  return FloatBitsEqual(lhs.x, rhs.x) && FloatBitsEqual(lhs.y, rhs.y);
}

bool Float4BitsEqual(const Float4 &lhs, const Float4 &rhs) noexcept {
  return FloatBitsEqual(lhs.x, rhs.x) && FloatBitsEqual(lhs.y, rhs.y) &&
         FloatBitsEqual(lhs.z, rhs.z) && FloatBitsEqual(lhs.w, rhs.w);
}

bool IsStraightSourceOver(
    const Ogre14LegacyPipelineStateInput &pipeline) noexcept {
  return pipeline.source_color == Ogre14LegacyBlendFactor::SOURCE_ALPHA &&
         pipeline.destination_color ==
             Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA &&
         pipeline.source_alpha == Ogre14LegacyBlendFactor::ONE &&
         pipeline.destination_alpha ==
             Ogre14LegacyBlendFactor::ONE_MINUS_SOURCE_ALPHA;
}

bool CapturedCullEqualsTranslated(Ogre14GraphicsSceneMaterialCull captured,
                                  Ogre14LegacyCullMode translated) noexcept {
  switch (captured) {
  case Ogre14GraphicsSceneMaterialCull::NONE:
    return translated == Ogre14LegacyCullMode::NONE;
  case Ogre14GraphicsSceneMaterialCull::CLOCKWISE:
    return translated == Ogre14LegacyCullMode::CLOCKWISE;
  case Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE:
    return translated == Ogre14LegacyCullMode::ANTICLOCKWISE;
  }
  return false;
}

ValidationResult
ValidateExactRoadMaterialCapture(const Ogre14ProceduralRoadCapture &capture,
                                 const Ogre14LegacyMaterialClosure &closure) {
  if (!capture.native_material_audit_complete ||
      capture.exact_native_material_audit == nullptr) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "road.material.exact_native_audit",
                   "exact translated road activation requires an independent "
                   "native audit owner");
  }
  if (closure.material_audit == nullptr ||
      !EquivalentOgre14LegacyMaterialPipelineAudit(
          *capture.exact_native_material_audit, *closure.material_audit)) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "road.material.exact_native_audit",
                   "captured native pipeline audit differs bit-for-bit from "
                   "translated state");
  }
  const Ogre14LegacyMaterialPipelineAudit &audit = *closure.material_audit;
  const MaterialDescriptor &material =
      std::get<MaterialDescriptor>(*closure.assets.back().payload);
  const bool textured = audit.texture_source_asset_id != 0U;
  const bool expected_lighting =
      audit.base_color_semantic ==
      Ogre14LegacyBaseColorSemantic::ROUGH_DIELECTRIC_PBR;
  const Ogre14GraphicsSceneMaterialBlend expected_blend =
      IsStraightSourceOver(audit.pipeline)
          ? Ogre14GraphicsSceneMaterialBlend::STRAIGHT_ALPHA
          : Ogre14GraphicsSceneMaterialBlend::REPLACE;
  const Ogre14GraphicsSceneMaterialAlphaReject expected_reject =
      audit.pipeline.alpha_reject == Ogre14LegacyCompareOperation::GREATER_EQUAL
          ? Ogre14GraphicsSceneMaterialAlphaReject::GREATER_EQUAL
          : Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
  if (capture.material.pass_count != 1U ||
      capture.material.texture_unit_count != (textured ? 1U : 0U) ||
      capture.material.has_vertex_program ||
      capture.material.has_fragment_program ||
      capture.material.lighting_enabled != expected_lighting ||
      capture.material.blend != expected_blend ||
      capture.material.alpha_reject != expected_reject ||
      capture.material.alpha_reject_value !=
          audit.pipeline.alpha_reject_value ||
      !CapturedCullEqualsTranslated(capture.material.cull,
                                    audit.pipeline.cull) ||
      !Float4BitsEqual(capture.material.diffuse_linear,
                       material.base_color_factor) ||
      !Float3BitsEqual(capture.material.ambient_linear, Float3{}) ||
      !Float3BitsEqual(capture.material.specular_linear, Float3{}) ||
      !Float3BitsEqual(capture.material.emissive_linear, Float3{}) ||
      !FloatBitsEqual(capture.material.shininess, 0.0F)) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "road.material.native_semantics",
                   "captured base factor, model, alpha, blend, cull, or "
                   "program state differs from the exact translated material");
  }
  return ValidationResult::Success();
}

ValidationResult ValidateGeometry(const Ogre14ProceduralRoadCapture &capture,
                                  bool require_lineage) {
  if (capture.version != kOgre14ProceduralRoadCaptureVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION, "road.version",
                   "unsupported procedural-road capture version");
  }
  if (!capture.finalized) {
    return Failure(ValidationCode::REVISION_MISMATCH, "road.finalized",
                   "procedural-road geometry must come after finish()");
  }
  if (require_lineage &&
      (capture.stable_graphics_id == 0U || capture.topology_revision == 0U)) {
    return Failure(
        ValidationCode::INVALID_IDENTIFIER, "road.lineage",
        "finalized road identity and topology revision must be nonzero");
  }
  if (capture.source_index_width_bits != 16U) {
    return Failure(
        ValidationCode::UNSUPPORTED_FEATURE, "road.source_index_width_bits",
        "procedural-road v1 requires the exact native uint16 stream");
  }
  const std::size_t vertex_count = capture.positions.size();
  const std::size_t index_count = capture.indices.size();
  if (vertex_count == 0U || index_count == 0U) {
    return Failure(ValidationCode::EMPTY_PAYLOAD,
                   vertex_count == 0U ? "road.positions" : "road.indices",
                   "procedural-road mesh requires indexed geometry");
  }
  if (vertex_count > kOgre14ProceduralRoadMaximumVertices ||
      vertex_count > 65536U ||
      index_count > kOgre14ProceduralRoadMaximumIndices) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE, "road.geometry_count",
                   "procedural-road geometry exceeds native lifetime bounds");
  }
  if (capture.exact_render_normals.size() != vertex_count ||
      capture.texture_coordinates_0.size() != vertex_count) {
    return Failure(ValidationCode::SIZE_MISMATCH, "road.vertex_streams",
                   "position, exact render normal, and UV0 counts must match");
  }
  if (index_count % 3U != 0U) {
    return Failure(
        ValidationCode::SIZE_MISMATCH, "road.indices",
        "procedural-road index stream must contain complete triangles");
  }

  for (std::size_t index = 0U; index < vertex_count; ++index) {
    if (!IsFinite(capture.positions[index]) ||
        !IsFinite(capture.texture_coordinates_0[index])) {
      return Failure(ValidationCode::NON_FINITE_VALUE, "road.vertex_streams",
                     "procedural-road positions and UV0 must be finite", index);
    }
    if (!IsUnit(capture.exact_render_normals[index])) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "road.exact_render_normals",
          "captured render normals must be finite unit vectors", index);
    }
  }

  for (std::size_t index = 0U; index < index_count; ++index) {
    if (capture.indices[index] > 65535U ||
        capture.indices[index] >= vertex_count) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "road.indices",
          "promoted uint16 index is outside the captured vertex range", index);
    }
  }

  // OGRE uploads the source triangle order and derives each vertex normal by
  // summing its normalized incident face normals. A positive dot for all
  // incident vertices proves that the copied normals and winding agree.
  for (std::size_t index = 0U; index < index_count; index += 3U) {
    const std::uint32_t i0 = capture.indices[index];
    const std::uint32_t i1 = capture.indices[index + 1U];
    const std::uint32_t i2 = capture.indices[index + 2U];
    const Float3 face =
        Cross(Subtract(capture.positions[i1], capture.positions[i0]),
              Subtract(capture.positions[i2], capture.positions[i0]));
    const float face_length_squared = Dot(face, face);
    if (!std::isfinite(face_length_squared) || face_length_squared <= 0.0F) {
      return Failure(ValidationCode::INVALID_BOUNDS, "road.triangle_winding",
                     "procedural-road triangles must have finite nonzero area",
                     index / 3U);
    }
    if (Dot(face, capture.exact_render_normals[i0]) <= 0.0F ||
        Dot(face, capture.exact_render_normals[i1]) <= 0.0F ||
        Dot(face, capture.exact_render_normals[i2]) <= 0.0F) {
      return Failure(ValidationCode::REVISION_MISMATCH, "road.triangle_winding",
                     "captured normals disagree with source triangle winding",
                     index / 3U);
    }
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateStandaloneWindingProof(const Ogre14ProceduralRoadWindingProof &proof) {
  if (proof.version != kOgre14ProceduralRoadWindingProofVersion) {
    return Failure(ValidationCode::UNSUPPORTED_VERSION,
                   "road.winding_proof.version",
                   "unsupported procedural-road winding proof version");
  }
  if (!proof.complete) {
    return Failure(ValidationCode::MISSING_REFERENCE,
                   "road.winding_proof.complete",
                   "procedural-road payload caching requires a complete "
                   "material-admission winding proof");
  }
  bool expected_reverse_winding = false;
  switch (proof.exact_native_cull) {
  case Ogre14GraphicsSceneMaterialCull::NONE:
  case Ogre14GraphicsSceneMaterialCull::CLOCKWISE:
    break;
  case Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE:
    expected_reverse_winding = true;
    break;
  default:
    return Failure(ValidationCode::INVALID_ENUM,
                   "road.winding_proof.exact_native_cull",
                   "winding proof contains an unknown native cull mode");
  }
  if (proof.reverse_winding != expected_reverse_winding) {
    return Failure(
        ValidationCode::REVISION_MISMATCH, "road.winding_proof.reverse_winding",
        "winding proof decision disagrees with its exact native cull mode");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateWindingProofForCapture(const Ogre14ProceduralRoadCapture &capture,
                               const Ogre14ProceduralRoadWindingProof &proof) {
  ValidationResult validation = ValidateStandaloneWindingProof(proof);
  if (!validation) {
    return validation;
  }
  if (proof.exact_native_cull != capture.material.cull) {
    return Failure(
        ValidationCode::REVISION_MISMATCH,
        "road.winding_proof.exact_native_cull",
        "winding proof does not belong to the admitted native road material");
  }
  return ValidationResult::Success();
}

ValidationResult
ValidateCachedPayloadWinding(const Ogre14ProceduralRoadCapture &capture,
                             const Ogre14ProceduralRoadCacheEntry &entry) {
  if (entry.mesh_payload == nullptr ||
      entry.mesh_payload->valueless_by_exception() ||
      !std::holds_alternative<MeshResourceDescriptor>(*entry.mesh_payload)) {
    return Failure(ValidationCode::WRONG_RESOURCE_KIND,
                   "road.cache.mesh_payload",
                   "cached procedural-road payload is not an immutable mesh");
  }
  const MeshResourceDescriptor &mesh =
      std::get<MeshResourceDescriptor>(*entry.mesh_payload);
  ValidationResult validation = ValidateMeshResourceDescriptor(mesh);
  if (!validation) {
    validation.field = "road.cache." + validation.field;
    return validation;
  }
  if (mesh.topology_revision != entry.topology_revision ||
      mesh.topology != MeshPrimitiveTopology::TRIANGLE_LIST || mesh.dynamic ||
      mesh.index_format != MeshIndexFormat::UINT16 ||
      mesh.positions.size() != capture.positions.size() ||
      mesh.normals.size() != capture.exact_render_normals.size() ||
      mesh.texture_coordinates_0.size() !=
          capture.texture_coordinates_0.size() ||
      mesh.indices.size() != capture.indices.size() || !mesh.tangents.empty() ||
      !mesh.velocities.empty() || !mesh.texture_coordinates_1.empty() ||
      !mesh.colors.empty()) {
    return Failure(ValidationCode::REVISION_MISMATCH, "road.cache.mesh_payload",
                   "cached procedural-road payload shape or topology lineage "
                   "is inconsistent");
  }
  for (std::size_t index = 0U; index < capture.positions.size(); ++index) {
    if (!Float3BitsEqual(mesh.positions[index], capture.positions[index]) ||
        !Float3BitsEqual(mesh.normals[index],
                         capture.exact_render_normals[index]) ||
        !Float2BitsEqual(mesh.texture_coordinates_0[index],
                         capture.texture_coordinates_0[index])) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "road.cache.mesh_payload.vertex_streams",
                     "cached procedural-road vertex bytes disagree with the "
                     "exact geometry key",
                     index);
    }
  }
  for (std::size_t index = 0U; index < capture.indices.size(); index += 3U) {
    const std::uint32_t expected_second =
        entry.exact_winding_proof.reverse_winding ? capture.indices[index + 2U]
                                                  : capture.indices[index + 1U];
    const std::uint32_t expected_third =
        entry.exact_winding_proof.reverse_winding ? capture.indices[index + 1U]
                                                  : capture.indices[index + 2U];
    if (mesh.indices[index] != capture.indices[index] ||
        mesh.indices[index + 1U] != expected_second ||
        mesh.indices[index + 2U] != expected_third) {
      return Failure(
          ValidationCode::REVISION_MISMATCH,
          "road.cache.mesh_payload.reverse_winding",
          "cached index bytes disagree with the retained exact winding proof",
          index / 3U);
    }
  }
  return ValidationResult::Success();
}

ValidationResult ValidateConfiguration(
    const Ogre14ProceduralRoadInventoryConfiguration &configuration) {
  if (configuration.maximum_live_roads == 0U ||
      configuration.maximum_lifetime_roads == 0U ||
      configuration.maximum_vertices_per_road == 0U ||
      configuration.maximum_indices_per_road == 0U ||
      configuration.maximum_payload_bytes == 0U) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "road_inventory.configuration",
                   "all procedural-road inventory bounds must be nonzero");
  }
  if (configuration.maximum_live_roads > configuration.maximum_lifetime_roads ||
      configuration.maximum_vertices_per_road >
          kOgre14ProceduralRoadMaximumVertices ||
      configuration.maximum_indices_per_road >
          kOgre14ProceduralRoadMaximumIndices) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "road_inventory.configuration",
                   "procedural-road inventory bounds exceed native limits");
  }
  return ValidationResult::Success();
}

std::string CanonicalRoadName(std::uint64_t manager_graphics_id) {
  return "procedural-road/" + std::to_string(manager_graphics_id);
}

} // namespace

class Ogre14ProceduralRoadInventoryTransaction final {
public:
  [[nodiscard]] static ValidationResult
  Build(const std::vector<Ogre14ProceduralRoadCapture> &captures,
        const Ogre14LegacyTranslatedFrame *authoritative_material_frame,
        Ogre14ProceduralRoadInventory &inventory,
        std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &sections);
};

Ogre14ProceduralRoadIdentityAllocator::Ogre14ProceduralRoadIdentityAllocator(
    std::uint64_t first_id, std::uint64_t maximum_id) noexcept
    : next_id_(first_id), maximum_id_(maximum_id) {}

ValidationResult Ogre14ProceduralRoadIdentityAllocator::Reserve(
    Ogre14ProceduralRoadIdentityState &state) {
  if (state.lifecycle_ !=
      Ogre14ProceduralRoadIdentityLifecycle::NEVER_REGISTERED) {
    return Failure(
        state.lifecycle_ == Ogre14ProceduralRoadIdentityLifecycle::TOMBSTONED
            ? ValidationCode::REVISION_MISMATCH
            : ValidationCode::DUPLICATE_IDENTIFIER,
        "procedural_object.graphics_identity",
        state.lifecycle_ == Ogre14ProceduralRoadIdentityLifecycle::TOMBSTONED
            ? "a removed procedural object may never be re-added"
            : "procedural object already has a manager-owned graphics "
              "identity");
  }
  if (next_id_ == 0U || maximum_id_ == 0U || next_id_ > maximum_id_) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "procedural_manager.next_graphics_id",
                   "procedural-road graphics identity space is exhausted");
  }

  Ogre14ProceduralRoadIdentityState candidate;
  candidate.stable_graphics_id_ = next_id_;
  candidate.lifecycle_ = Ogre14ProceduralRoadIdentityLifecycle::RESERVED;
  state = std::move(candidate);
  next_id_ = next_id_ == maximum_id_ ? 0U : next_id_ + 1U;
  return ValidationResult::Success();
}

ValidationResult Ogre14ProceduralRoadIdentityAllocator::FinalizeGeometry(
    Ogre14ProceduralRoadIdentityState &state,
    std::string exact_geometry_state_key) const {
  if (state.lifecycle_ != Ogre14ProceduralRoadIdentityLifecycle::RESERVED &&
      state.lifecycle_ != Ogre14ProceduralRoadIdentityLifecycle::LIVE) {
    return Failure(
        ValidationCode::REVISION_MISMATCH,
        "procedural_object.graphics_lifecycle",
        "only reserved or live procedural roads may finalize geometry");
  }
  if (state.stable_graphics_id_ == 0U || exact_geometry_state_key.empty()) {
    return Failure(
        ValidationCode::INVALID_IDENTIFIER,
        "procedural_object.geometry_state_key",
        "finalized road identity and exact geometry key must be nonzero");
  }

  std::uint64_t candidate_revision = state.topology_revision_;
  if (candidate_revision == 0U) {
    candidate_revision = 1U;
  } else if (state.exact_geometry_state_key_ != exact_geometry_state_key) {
    if (candidate_revision == (std::numeric_limits<std::uint64_t>::max)()) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "procedural_object.topology_revision",
                     "procedural-road topology revision is exhausted");
    }
    ++candidate_revision;
  }
  state.exact_geometry_state_key_ = std::move(exact_geometry_state_key);
  state.topology_revision_ = candidate_revision;
  state.lifecycle_ = Ogre14ProceduralRoadIdentityLifecycle::LIVE;
  return ValidationResult::Success();
}

ValidationResult Ogre14ProceduralRoadIdentityAllocator::Tombstone(
    Ogre14ProceduralRoadIdentityState &state) const {
  if (state.lifecycle_ != Ogre14ProceduralRoadIdentityLifecycle::RESERVED &&
      state.lifecycle_ != Ogre14ProceduralRoadIdentityLifecycle::LIVE) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "procedural_object.graphics_lifecycle",
                   state.lifecycle_ ==
                           Ogre14ProceduralRoadIdentityLifecycle::TOMBSTONED
                       ? "procedural-road identity is already tombstoned"
                       : "unregistered procedural object cannot be removed");
  }
  state.lifecycle_ = Ogre14ProceduralRoadIdentityLifecycle::TOMBSTONED;
  std::string{}.swap(state.exact_geometry_state_key_);
  return ValidationResult::Success();
}

Ogre14ProceduralRoadInventory::Ogre14ProceduralRoadInventory(
    Ogre14ProceduralRoadInventoryConfiguration configuration)
    : configuration_(std::move(configuration)) {}

ValidationResult Ogre14ProceduralRoadInventory::RegisterDerivedIdentityForAudit(
    std::uint64_t manager_graphics_id, std::uint64_t derived_object_id) {
  if (manager_graphics_id == 0U || derived_object_id == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "road_inventory.identity",
                   "manager and derived procedural-road IDs must be nonzero");
  }
  const auto manager = derived_id_by_manager_id_.find(manager_graphics_id);
  if (manager != derived_id_by_manager_id_.end() &&
      manager->second != derived_object_id) {
    return Failure(ValidationCode::REVISION_MISMATCH,
                   "road_inventory.manager_graphics_id",
                   "one manager road ID mapped to multiple renderer IDs");
  }
  const auto derived = manager_id_by_derived_id_.find(derived_object_id);
  if (derived != manager_id_by_derived_id_.end() &&
      derived->second != manager_graphics_id) {
    return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                   "road_inventory.derived_object_id",
                   "procedural-road renderer identity collision detected");
  }
  derived_id_by_manager_id_[manager_graphics_id] = derived_object_id;
  manager_id_by_derived_id_[derived_object_id] = manager_graphics_id;
  return ValidationResult::Success();
}

ValidationResult
DeriveOgre14ProceduralRoadObjectId(std::uint64_t manager_graphics_id,
                                   std::uint64_t &derived_object_id) {
  if (manager_graphics_id == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER,
                   "road.manager_graphics_id",
                   "procedural-road manager identity must be nonzero");
  }
  std::uint64_t candidate = kFnv1a64OffsetBasis;
  for (const char byte :
       std::string_view(kProceduralRoadObjectIdentityDomain,
                        sizeof(kProceduralRoadObjectIdentityDomain) - 1U)) {
    HashByte(candidate, static_cast<std::uint8_t>(byte));
  }
  HashByte(candidate, 0U);
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
    HashByte(candidate,
             static_cast<std::uint8_t>((manager_graphics_id >> shift) & 0xFFU));
  }
  if (candidate == 0U) {
    return Failure(ValidationCode::INVALID_IDENTIFIER, "road.derived_object_id",
                   "procedural-road identity derived reserved zero");
  }
  derived_object_id = candidate;
  return ValidationResult::Success();
}

ValidationResult BuildOgre14ProceduralRoadGeometryStateKey(
    const Ogre14ProceduralRoadCapture &capture, std::string &key) {
  ValidationResult validation = ValidateGeometry(capture, false);
  if (!validation) {
    return validation;
  }

  std::string candidate;
  const std::size_t vertex_bytes = capture.positions.size() * 32U;
  const std::size_t index_bytes =
      capture.indices.size() * sizeof(std::uint32_t);
  candidate.reserve(sizeof(kProceduralRoadGeometryStateDomain) + vertex_bytes +
                    index_bytes + 48U);
  candidate.append(kProceduralRoadGeometryStateDomain,
                   sizeof(kProceduralRoadGeometryStateDomain) - 1U);
  candidate.push_back('\0');
  AppendU32(candidate, capture.version);
  AppendU32(candidate, capture.source_index_width_bits);
  AppendU64(candidate, static_cast<std::uint64_t>(capture.positions.size()));
  AppendU64(candidate, static_cast<std::uint64_t>(capture.indices.size()));
  for (std::size_t index = 0U; index < capture.positions.size(); ++index) {
    const Float3 &position = capture.positions[index];
    const Float3 &normal = capture.exact_render_normals[index];
    const Float2 &uv = capture.texture_coordinates_0[index];
    AppendFloat(candidate, position.x);
    AppendFloat(candidate, position.y);
    AppendFloat(candidate, position.z);
    AppendFloat(candidate, normal.x);
    AppendFloat(candidate, normal.y);
    AppendFloat(candidate, normal.z);
    AppendFloat(candidate, uv.x);
    AppendFloat(candidate, uv.y);
  }
  for (const std::uint32_t index : capture.indices) {
    AppendU32(candidate, index);
  }
  key = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult BuildOgre14ProceduralRoadMeshPayload(
    const Ogre14ProceduralRoadCapture &capture,
    const Ogre14ProceduralRoadWindingProof &winding_proof,
    std::shared_ptr<const RenderAssetPayload> &payload) {
  ValidationResult validation = ValidateGeometry(capture, true);
  if (!validation) {
    return validation;
  }
  validation = ValidateWindingProofForCapture(capture, winding_proof);
  if (!validation) {
    return validation;
  }
  Ogre14GraphicsSceneCpuMeshSectionInput input;
  input.debug_name = CanonicalRoadName(capture.stable_graphics_id) + "/mesh";
  input.index_format = MeshIndexFormat::UINT16;
  input.topology_revision = capture.topology_revision;
  input.reverse_winding = winding_proof.reverse_winding;
  input.positions = capture.positions;
  input.normals = capture.exact_render_normals;
  input.texture_coordinates_0 = capture.texture_coordinates_0;
  input.indices = capture.indices;
  return BuildOgre14GraphicsSceneStaticMeshPayload(input, payload);
}

ValidationResult ResolveOgre14ProceduralRoadCacheEntry(
    const Ogre14ProceduralRoadCapture &capture,
    const Ogre14ProceduralRoadWindingProof &winding_proof,
    const Ogre14ProceduralRoadCacheEntry *previous,
    Ogre14ProceduralRoadCacheEntry &entry) {
  std::string state_key;
  ValidationResult validation =
      BuildOgre14ProceduralRoadGeometryStateKey(capture, state_key);
  if (!validation) {
    return validation;
  }
  validation = ValidateWindingProofForCapture(capture, winding_proof);
  if (!validation) {
    return validation;
  }
  if (capture.stable_graphics_id == 0U || capture.topology_revision == 0U) {
    return Failure(
        ValidationCode::INVALID_IDENTIFIER, "road.lineage",
        "cache requires nonzero road identity and topology revision");
  }

  if (previous != nullptr) {
    if (previous->exact_geometry_state_key.empty() ||
        previous->topology_revision == 0U ||
        previous->mesh_payload == nullptr) {
      return Failure(ValidationCode::MISSING_REFERENCE, "road.cache",
                     "prior procedural-road cache entry is incomplete");
    }
    validation = ValidateStandaloneWindingProof(previous->exact_winding_proof);
    if (!validation) {
      validation.field = "road.cache." + validation.field;
      return validation;
    }
    if (previous->mesh_payload->valueless_by_exception() ||
        !std::holds_alternative<MeshResourceDescriptor>(
            *previous->mesh_payload)) {
      return Failure(ValidationCode::WRONG_RESOURCE_KIND,
                     "road.cache.mesh_payload",
                     "prior procedural-road cache payload is not a mesh");
    }
    const MeshResourceDescriptor &previous_mesh =
        std::get<MeshResourceDescriptor>(*previous->mesh_payload);
    validation = ValidateMeshResourceDescriptor(previous_mesh);
    if (!validation) {
      validation.field = "road.cache." + validation.field;
      return validation;
    }
    if (previous_mesh.topology_revision != previous->topology_revision) {
      return Failure(
          ValidationCode::REVISION_MISMATCH,
          "road.cache.mesh_payload.topology_revision",
          "prior cached payload disagrees with its retained topology lineage");
    }
    if (previous->exact_geometry_state_key == state_key) {
      if (capture.topology_revision != previous->topology_revision) {
        return Failure(ValidationCode::REVISION_MISMATCH,
                       "road.topology_revision",
                       "unchanged road geometry must retain its revision");
      }
      validation = ValidateCachedPayloadWinding(capture, *previous);
      if (!validation) {
        return validation;
      }
      if (previous->exact_winding_proof.reverse_winding ==
          winding_proof.reverse_winding) {
        Ogre14ProceduralRoadCacheEntry candidate = *previous;
        candidate.exact_winding_proof = winding_proof;
        entry = std::move(candidate);
        return ValidationResult::Success();
      }
    }
    if (previous->exact_geometry_state_key != state_key &&
        (previous->topology_revision ==
             (std::numeric_limits<std::uint64_t>::max)() ||
         capture.topology_revision != previous->topology_revision + 1U)) {
      return Failure(
          ValidationCode::REVISION_MISMATCH, "road.topology_revision",
          "changed road geometry must advance revision exactly once");
    }
  }

  std::shared_ptr<const RenderAssetPayload> mesh_payload;
  validation = BuildOgre14ProceduralRoadMeshPayload(capture, winding_proof,
                                                    mesh_payload);
  if (!validation) {
    return validation;
  }
  Ogre14ProceduralRoadCacheEntry candidate;
  candidate.exact_geometry_state_key = std::move(state_key);
  candidate.topology_revision = capture.topology_revision;
  candidate.exact_winding_proof = winding_proof;
  candidate.mesh_payload = std::move(mesh_payload);
  entry = std::move(candidate);
  return ValidationResult::Success();
}

ValidationResult Ogre14ProceduralRoadInventoryTransaction::Build(
    const std::vector<Ogre14ProceduralRoadCapture> &captures,
    const Ogre14LegacyTranslatedFrame *authoritative_material_frame,
    Ogre14ProceduralRoadInventory &inventory,
    std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &sections) try {
  ValidationResult validation = ValidateConfiguration(inventory.configuration_);
  if (!validation) {
    return validation;
  }
  if (captures.size() > inventory.configuration_.maximum_live_roads) {
    return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                   "road_inventory.live_roads",
                   "procedural-road live inventory exceeds configured bound");
  }

  std::vector<const Ogre14ProceduralRoadCapture *> ordered;
  ordered.reserve(captures.size());
  for (const Ogre14ProceduralRoadCapture &capture : captures) {
    ordered.push_back(&capture);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto *lhs, const auto *rhs) {
              return lhs->stable_graphics_id < rhs->stable_graphics_id;
            });

  Ogre14ProceduralRoadInventory candidate_inventory = inventory;
  std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> candidate_sections;
  candidate_sections.reserve(ordered.size());
  std::set<std::uint64_t> current_manager_ids;
  std::map<std::uint64_t, Ogre14ProceduralRoadCacheEntry> current_cache;
  std::map<std::string, std::shared_ptr<const Ogre14LegacyMaterialClosure>,
           std::less<>>
      resolved_materials;
  std::uint64_t candidate_payload_bytes = 0U;

  for (std::size_t ordered_index = 0U; ordered_index < ordered.size();
       ++ordered_index) {
    const Ogre14ProceduralRoadCapture &capture = *ordered[ordered_index];
    validation = ValidateGeometry(capture, true);
    if (!validation) {
      validation.element_index = ordered_index;
      return validation;
    }
    if (ordered_index != 0U &&
        ordered[ordered_index - 1U]->stable_graphics_id ==
            capture.stable_graphics_id) {
      return Failure(ValidationCode::DUPLICATE_IDENTIFIER,
                     "road_inventory.manager_graphics_id",
                     "complete road inventory contains a duplicate source ID",
                     ordered_index);
    }
    if (capture.exact_native_mesh_resource_group.find('\0') !=
            std::string::npos ||
        capture.exact_native_mesh_name.empty() ||
        capture.exact_native_mesh_name.find('\0') != std::string::npos ||
        capture.exact_native_entity_name.empty() ||
        capture.exact_native_entity_name.find('\0') != std::string::npos) {
      return Failure(
          ValidationCode::INVALID_IDENTIFIER, "road.native_resource_identity",
          "native road mesh/entity identities must be NUL-free and nonempty",
          ordered_index);
    }
    if (!capture.native_material_audit_complete) {
      return Failure(
          ValidationCode::UNSUPPORTED_FEATURE, "road.material.native_audit",
          "native road material state could not be represented exactly",
          ordered_index);
    }
    if (capture.material.exact_name != "road2") {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "road.material.exact_name",
                     "procedural-road v1 requires exact legacy material road2",
                     ordered_index);
    }
    std::shared_ptr<const Ogre14LegacyMaterialClosure> resolved_road_material;
    Ogre14ProceduralRoadWindingProof winding_proof;
    winding_proof.exact_native_cull = capture.material.cull;
    if (authoritative_material_frame != nullptr) {
      const Ogre14LegacyAssetKey material_key{
          capture.material.exact_resource_group, capture.material.exact_name};
      std::string stable_material_key;
      validation = BuildOgre14LegacyStableAssetKey(
          RenderAssetKind::MATERIAL, material_key, stable_material_key);
      if (!validation) {
        validation.element_index = ordered_index;
        return validation;
      }
      const auto existing = resolved_materials.find(stable_material_key);
      if (existing == resolved_materials.end()) {
        Ogre14LegacyMaterialClosure closure;
        validation = ResolveOgre14LegacyMaterialClosure(
            *authoritative_material_frame, material_key, closure);
        if (!validation) {
          validation.element_index = ordered_index;
          validation.field = "road." + validation.field;
          return validation;
        }
        if (closure.source_sequence !=
                authoritative_material_frame->source_sequence ||
            closure.catalog_sequence !=
                authoritative_material_frame->catalog_sequence) {
          return Failure(ValidationCode::SEQUENCE_MISMATCH,
                         "road.material.closure_sequence",
                         "resolved closure does not carry the authoritative "
                         "full-frame lineage",
                         ordered_index);
        }
        resolved_road_material =
            std::make_shared<const Ogre14LegacyMaterialClosure>(
                std::move(closure));
        resolved_materials.emplace(std::move(stable_material_key),
                                   resolved_road_material);
      } else {
        resolved_road_material = existing->second;
      }
      validation =
          ValidateExactRoadMaterialCapture(capture, *resolved_road_material);
      if (!validation) {
        validation.element_index = ordered_index;
        return validation;
      }
      winding_proof.reverse_winding =
          resolved_road_material->requires_reverse_winding;
    } else {
      MaterialDescriptor audited_material;
      validation = BuildOgre14GraphicsSceneMaterialFallback(capture.material,
                                                            audited_material);
      if (!validation) {
        validation.element_index = ordered_index;
        validation.field = "road." + validation.field;
        return validation;
      }
      winding_proof.reverse_winding =
          capture.material.cull ==
          Ogre14GraphicsSceneMaterialCull::ANTICLOCKWISE;
    }
    winding_proof.complete = true;
    validation = ValidateWindingProofForCapture(capture, winding_proof);
    if (!validation) {
      validation.element_index = ordered_index;
      return validation;
    }
    if (!HasInvertibleAffineTransform(capture.render_from_object) ||
        LinearDeterminant(capture.render_from_object) < 0.0F) {
      return Failure(
          LinearDeterminant(capture.render_from_object) < 0.0F
              ? ValidationCode::UNSUPPORTED_FEATURE
              : ValidationCode::VALUE_OUT_OF_RANGE,
          "road.render_from_object",
          "road transform must be finite, affine, invertible and non-mirrored",
          ordered_index);
    }
    if (capture.positions.size() >
            inventory.configuration_.maximum_vertices_per_road ||
        capture.indices.size() >
            inventory.configuration_.maximum_indices_per_road) {
      return Failure(
          ValidationCode::VALUE_OUT_OF_RANGE, "road_inventory.geometry_count",
          "road geometry exceeds configured per-road bounds", ordered_index);
    }
    const std::uint64_t vertex_bytes =
        static_cast<std::uint64_t>(capture.positions.size()) * 32U;
    const std::uint64_t index_bytes =
        static_cast<std::uint64_t>(capture.indices.size()) * 4U;
    if (vertex_bytes >
            (std::numeric_limits<std::uint64_t>::max)() - index_bytes ||
        candidate_payload_bytes > (std::numeric_limits<std::uint64_t>::max)() -
                                      (vertex_bytes + index_bytes) ||
        candidate_payload_bytes + vertex_bytes + index_bytes >
            inventory.configuration_.maximum_payload_bytes) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "road_inventory.payload_bytes",
                     "road inventory exceeds configured exact stream bytes",
                     ordered_index);
    }
    candidate_payload_bytes += vertex_bytes + index_bytes;

    if (inventory.known_manager_ids_.find(capture.stable_graphics_id) !=
            inventory.known_manager_ids_.end() &&
        inventory.live_manager_ids_.find(capture.stable_graphics_id) ==
            inventory.live_manager_ids_.end()) {
      return Failure(ValidationCode::REVISION_MISMATCH,
                     "road_inventory.manager_graphics_id",
                     "a removed procedural-road identity may never return",
                     ordered_index);
    }
    if (inventory.known_manager_ids_.find(capture.stable_graphics_id) ==
            inventory.known_manager_ids_.end() &&
        candidate_inventory.known_manager_ids_.size() >=
            inventory.configuration_.maximum_lifetime_roads) {
      return Failure(ValidationCode::VALUE_OUT_OF_RANGE,
                     "road_inventory.lifetime_roads",
                     "procedural-road lifetime identity bound is exhausted",
                     ordered_index);
    }

    std::uint64_t derived_object_id = 0U;
    validation = DeriveOgre14ProceduralRoadObjectId(capture.stable_graphics_id,
                                                    derived_object_id);
    if (!validation) {
      validation.element_index = ordered_index;
      return validation;
    }
    validation = candidate_inventory.RegisterDerivedIdentityForAudit(
        capture.stable_graphics_id, derived_object_id);
    if (!validation) {
      validation.element_index = ordered_index;
      return validation;
    }

    const auto previous =
        inventory.cache_by_manager_id_.find(capture.stable_graphics_id);
    Ogre14ProceduralRoadCacheEntry cache_entry;
    validation = ResolveOgre14ProceduralRoadCacheEntry(
        capture, winding_proof,
        previous != inventory.cache_by_manager_id_.end() ? &previous->second
                                                         : nullptr,
        cache_entry);
    if (!validation) {
      validation.element_index = ordered_index;
      return validation;
    }

    Ogre14GraphicsSceneStaticSectionCaptureInput section;
    section.stable_object_id = derived_object_id;
    section.section_index = 0U;
    section.exact_entity_name = CanonicalRoadName(capture.stable_graphics_id);
    section.mesh_identity.exact_resource_group =
        kProceduralRoadCanonicalMeshGroup;
    section.mesh_identity.exact_mesh_name =
        CanonicalRoadName(capture.stable_graphics_id) + "/mesh";
    section.mesh_identity.vertex_count =
        static_cast<std::uint32_t>(capture.positions.size());
    section.mesh_identity.index_count =
        static_cast<std::uint32_t>(capture.indices.size());
    section.mesh_identity.reverse_winding = winding_proof.reverse_winding;
    section.mesh_payload = cache_entry.mesh_payload;
    section.material = capture.material;
    section.resolved_material = std::move(resolved_road_material);
    section.render_from_object = capture.render_from_object;
    section.visibility_mask = capture.visibility_mask;
    section.visible = capture.visible;
    section.casts_shadows = capture.casts_shadows;
    section.receives_shadows = capture.receives_shadows;
    section.visible_in_reflections = capture.visible_in_reflections;
    candidate_sections.push_back(std::move(section));
    current_manager_ids.insert(capture.stable_graphics_id);
    current_cache.emplace(capture.stable_graphics_id, std::move(cache_entry));
    candidate_inventory.known_manager_ids_.insert(capture.stable_graphics_id);
  }

  std::sort(candidate_sections.begin(), candidate_sections.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.stable_object_id < rhs.stable_object_id;
            });
  candidate_inventory.live_manager_ids_ = std::move(current_manager_ids);
  candidate_inventory.cache_by_manager_id_ = std::move(current_cache);
  inventory = std::move(candidate_inventory);
  sections = std::move(candidate_sections);
  return ValidationResult::Success();
} catch (const std::bad_alloc &) {
  return Failure(ValidationCode::EMPTY_PAYLOAD, "road_inventory.allocation",
                 "allocation failed before the road inventory was published");
} catch (...) {
  return Failure(
      ValidationCode::UNSUPPORTED_FEATURE, "road_inventory.exception",
      "unexpected exception before the road inventory was published");
}

ValidationResult BuildOgre14ProceduralRoadInventory(
    const std::vector<Ogre14ProceduralRoadCapture> &captures,
    Ogre14ProceduralRoadInventory &inventory,
    std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &sections) {
  return Ogre14ProceduralRoadInventoryTransaction::Build(captures, nullptr,
                                                         inventory, sections);
}

ValidationResult BuildOgre14ProceduralRoadInventory(
    const std::vector<Ogre14ProceduralRoadCapture> &captures,
    const Ogre14LegacyTranslatedFrame &authoritative_material_frame,
    Ogre14ProceduralRoadInventory &inventory,
    std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &sections) {
  return Ogre14ProceduralRoadInventoryTransaction::Build(
      captures, &authoritative_material_frame, inventory, sections);
}

} // namespace RoR::Render
