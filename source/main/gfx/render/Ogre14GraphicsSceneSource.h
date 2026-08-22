/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Fail-closed OGRE 14 joined-scene adapter for the render contract.

#pragma once

#include "GraphicsSceneSnapshotProducer.h"
#include "Ogre14LegacyMaterialClosure.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kOgre14GraphicsSceneSourceVersion = 2U;
constexpr std::size_t kMaximumOgre14GraphicsSceneStaticSections = 65536U;
constexpr std::size_t kMaximumOgre14GraphicsSceneStaticAssets = 65536U;
constexpr std::size_t kMaximumOgre14GraphicsSceneDynamicSections = 65536U;
constexpr std::size_t kMaximumOgre14GraphicsSceneDynamicAssets = 65536U;
/// Aggregate hostile-input bound shared with the default scene producer.
/// This caps source records before allocation, including exact duplicates
/// across static, deformable, and procedural-road inventories.
constexpr std::size_t kMaximumOgre14GraphicsSceneMergedAssets = 65536U;

enum class Ogre14GraphicsSceneAssetMergeFaultPoint : std::uint8_t {
  AFTER_FIRST_UNIQUE_ASSET = 0U,
};

/// Borrowed test-only exception seam. Production callers leave this null.
class IOgre14GraphicsSceneAssetMergeFaultInjector {
public:
  virtual ~IOgre14GraphicsSceneAssetMergeFaultInjector() = default;
  virtual void AtFaultPoint(
      Ogre14GraphicsSceneAssetMergeFaultPoint point) = 0;
};

/// Transactionally merges three complete domain inventories. Source identity
/// collisions require bit-exact payload and complete binding equivalence; an
/// exact duplicate keeps the first (static, then dynamic, then road) immutable
/// owner. Success publishes one source-ID-sorted vector. Failure, allocation
/// exceptions, and unexpected exceptions leave `assets` untouched.
[[nodiscard]] ValidationResult MergeOgre14GraphicsSceneAssets(
    const std::vector<GraphicsSceneAssetInput> &static_assets,
    const std::vector<GraphicsSceneAssetInput> &dynamic_assets,
    const std::vector<GraphicsSceneAssetInput> &road_assets,
    std::vector<GraphicsSceneAssetInput> &assets,
    IOgre14GraphicsSceneAssetMergeFaultInjector *fault_injector = nullptr);

/// Removes from `assets` every identity `retained` already carries, so the two
/// can be submitted as one disjoint union.
///
/// A source identity legitimately appears in more than one scene domain - the
/// merge above exists to collapse exactly that - so a domain inventory handed
/// beside a retained section still repeats identities the section owns. This
/// applies the merge's own rule to that pair: an identity in both must be the
/// same immutable asset, and a conflicting redefinition fails closed rather
/// than picking a winner. The resulting identity set is unchanged, so nothing
/// is omitted and nothing is tombstoned by the removal.
///
/// `retained` must be strictly increasing by source identity, which is proven
/// here rather than assumed. `assets` keeps its order and is untouched on
/// failure.
[[nodiscard]] ValidationResult SubtractRetainedOgre14GraphicsSceneAssets(
    const std::vector<GraphicsSceneAssetInput> &retained,
    std::vector<GraphicsSceneAssetInput> &assets);

/// Every bit names state which must come from the same completed
/// GfxScene::BufferSimulationData() boundary. An adapter may expose a partial
/// capture for diagnostics, but IJoinedGraphicsSceneSource publishes only when
/// every required bit is present.
enum class Ogre14GraphicsSceneCaptureField : std::uint32_t {
  JOINED_BUFFER_ATOMICITY = 1U << 0U,
  SIMULATION_TICK = 1U << 1U,
  SIMULATION_TIME_SECONDS = 1U << 2U,
  ABSOLUTE_WORLD_ORIGIN_METERS = 1U << 3U,
  ENVIRONMENT = 1U << 4U,
  ASSETS = 1U << 5U,
  STATIC_MESHES = 1U << 6U,
  LIGHTS = 1U << 7U,
  REFLECTION_PROBES = 1U << 8U,
  CAMERA = 1U << 9U,
  POST_UPDATE_SCENE_ATOMICITY = 1U << 10U,
  DYNAMIC_MESHES = 1U << 11U,
};

constexpr std::uint32_t Ogre14GraphicsSceneCaptureFieldBit(
    Ogre14GraphicsSceneCaptureField field) noexcept {
  return static_cast<std::uint32_t>(field);
}

constexpr std::uint32_t kOgre14GraphicsSceneRequiredFields =
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::JOINED_BUFFER_ATOMICITY) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::SIMULATION_TICK) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::SIMULATION_TIME_SECONDS) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::ABSOLUTE_WORLD_ORIGIN_METERS) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::ENVIRONMENT) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::ASSETS) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::STATIC_MESHES) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::LIGHTS) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::REFLECTION_PROBES) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::CAMERA) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::POST_UPDATE_SCENE_ATOMICITY) |
    Ogre14GraphicsSceneCaptureFieldBit(
        Ogre14GraphicsSceneCaptureField::DYNAMIC_MESHES);

struct Ogre14GraphicsSceneCapture {
  std::uint32_t version = kOgre14GraphicsSceneSourceVersion;
  /// Nonzero generation of the completed BufferSimulationData() call.
  std::uint64_t joined_buffer_epoch = 0U;
  /// Must exactly equal joined_buffer_epoch and is written only after every
  /// GfxScene::UpdateScene() Flex* task has joined and finalized its CPU/GPU
  /// staging state.
  std::uint64_t post_update_scene_epoch = 0U;
  std::uint32_t available_fields = 0U;
  GraphicsSceneFrameInput frame;
  /// Optional wall-clock nanoseconds the provider spent in each major section
  /// of one capture. Reading the OGRE 14 scene is the dominant cost of a
  /// combined-runtime frame, and the sections have very different remedies:
  /// a re-enumerated static inventory is cacheable in place, while a cost
  /// spread evenly across sections is inherent to sourcing from OGRE 14.
  /// A provider that does not measure itself leaves these zero.
  std::uint64_t terrain_ns = 0U;
  std::uint64_t static_meshes_ns = 0U;
  std::uint64_t dynamic_meshes_ns = 0U;
  std::uint64_t particles_ns = 0U;
  std::uint64_t materials_ns = 0U;
};

/// Project-owned fallback probe for the bounded combined-runtime visual path.
/// This is not an authored map probe and does not reuse OGRE 14's vehicle-local
/// GfxEnvmap. Its first accepted camera position remains fixed for the map
/// generation; exact static-object inventory changes advance content revision
/// and the periodic mode refreshes changing analytic-sky radiance.
constexpr std::uint32_t
    kOgre14AutomaticReflectionProbePolicyVersion = 1U;
constexpr std::uint64_t kOgre14AutomaticReflectionProbeId =
    UINT64_C(0x524f525043430001);
constexpr std::uint64_t
    kOgre14AutomaticReflectionProbeUpdateIntervalSimulationTicks = 20000U;

struct Ogre14AutomaticReflectionProbeState {
  std::uint32_t version =
      kOgre14AutomaticReflectionProbePolicyVersion;
  std::uint64_t content_revision = 0U;
  Double3 absolute_world_position_meters{};
  std::vector<std::uint64_t> static_object_ids;
  bool initialized = false;
};

/// Builds either the exact empty pre-content inventory or one automatic probe.
/// Both output arguments remain unchanged on validation/allocation failure.
[[nodiscard]] ValidationResult BuildOgre14AutomaticReflectionProbe(
    const Double3 &current_camera_position_meters,
    const std::vector<GraphicsSceneStaticMeshInput> &static_meshes,
    const Ogre14AutomaticReflectionProbeState &committed_state,
    Ogre14AutomaticReflectionProbeState &candidate_state,
    std::vector<ReflectionProbeRuntimeDescriptor> &probes);

/// Narrow production seam implemented by GfxScene. It creates one candidate
/// from graphics-owned state and copied simulation buffers only. Partial
/// candidates return success so the source can report every unavailable field
/// without inventing default scene content.
class IOgre14GraphicsSceneCaptureProvider {
public:
  virtual ~IOgre14GraphicsSceneCaptureProvider() = default;
  [[nodiscard]] virtual ValidationResult CaptureOgre14GraphicsScene(
      Ogre14GraphicsSceneCapture &capture) = 0;
  virtual void CommitOgre14GraphicsSceneCapture() noexcept {}
  virtual void DiscardOgre14GraphicsSceneCapture() noexcept {}
};

/// Validates adapter metadata and reports the first missing field plus the
/// complete ordered missing-field list. It does not duplicate the semantic
/// GraphicsSceneFrameInput validation owned by GraphicsSceneSnapshotProducer.
[[nodiscard]] ValidationResult ValidateOgre14GraphicsSceneCapture(
    const Ogre14GraphicsSceneCapture &capture);
[[nodiscard]] std::string DescribeMissingOgre14GraphicsSceneFields(
    std::uint32_t available_fields);
[[nodiscard]] const char *ToString(
    Ogre14GraphicsSceneCaptureField field) noexcept;

/// Transactional IJoinedGraphicsSceneSource binding. Provider exceptions,
/// malformed metadata, and incomplete field sets leave `frame` untouched.
class Ogre14GraphicsSceneSource final : public IJoinedGraphicsSceneSource {
public:
  explicit Ogre14GraphicsSceneSource(
      IOgre14GraphicsSceneCaptureProvider &provider) noexcept;
  ~Ogre14GraphicsSceneSource() override;

  [[nodiscard]] ValidationResult CaptureJoinedGraphicsFrame(
      GraphicsSceneFrameInput &frame) override;
  void CommitJoinedGraphicsFrame() noexcept override;
  void DiscardJoinedGraphicsFrame() noexcept override;
  [[nodiscard]] std::uint64_t LastJoinedReadNanoseconds() const noexcept
      override {
    return last_joined_read_ns_;
  }
  [[nodiscard]] std::uint64_t LastJoinedValidateNanoseconds() const noexcept
      override {
    return last_joined_validate_ns_;
  }

private:
  IOgre14GraphicsSceneCaptureProvider &provider_;
  bool capture_pending_ = false;
  std::uint64_t last_joined_read_ns_ = 0U;
  std::uint64_t last_joined_validate_ns_ = 0U;
};

enum class Ogre14CameraProjectionKind : std::uint8_t {
  PERSPECTIVE = 0U,
  ORTHOGRAPHIC = 1U,
};

/// Renderer-neutral values read from one OGRE 14 Camera/Viewport. Perspective
/// extents are measured on the near plane; orthographic extents are world
/// units. Custom OGRE projection matrices are rejected by the live provider.
struct Ogre14CameraCaptureInput {
  std::uint64_t view_id = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  Matrix4x4 view_from_render;
  Ogre14CameraProjectionKind projection =
      Ogre14CameraProjectionKind::PERSPECTIVE;
  float left = 0.0F;
  float right = 0.0F;
  float top = 0.0F;
  float bottom = 0.0F;
  float near_plane = 0.0F;
  float far_plane = 0.0F;
  float exposure = 1.0F;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
};

/// Compatibility fallback, not a claim that fixed-function OGRE materials
/// have physically authored metallic/roughness values. Version two preserves
/// the first authored pass's renderer-linear diffuse/emissive factors,
/// the exact true/legacy alpha blend tuple, independent GREATER/GREATER_EQUAL
/// test and cutoff, depth-write state, lighting enable, and culling. Legacy
/// texture units, shader programs, and additional passes fail closed because
/// publishing factor data alone would silently discard authored visuals.
/// Ambient/specular lobes remain audited native metadata but intentionally
/// acquire no guessed portable PBR contribution.
constexpr std::uint32_t kOgre14StaticMaterialFallbackVersion = 2U;
constexpr std::uint32_t kOgre14TerrainCpuCaptureVersion = 1U;

enum class Ogre14GraphicsSceneMaterialBlend : std::uint8_t {
  REPLACE = 0U,
  STRAIGHT_SOURCE_OVER = 1U,
  LEGACY_STRAIGHT_ALPHA = 2U,
};

enum class Ogre14GraphicsSceneMaterialCull : std::uint8_t {
  NONE = 0U,
  CLOCKWISE = 1U,
  ANTICLOCKWISE = 2U,
};

enum class Ogre14GraphicsSceneMaterialAlphaReject : std::uint8_t {
  ALWAYS_PASS = 0U,
  GREATER = 1U,
  GREATER_EQUAL = 2U,
};

/// Renderer-neutral copy of the fixed-function state used by the portable
/// material fallback. `pass_count`, texture-unit count, and program flags are
/// validated as the exact factor-only eligibility gate. Ambient and specular
/// are retained to make the compatibility conversion explicit.
struct Ogre14GraphicsSceneMaterialCaptureInput {
  std::string exact_resource_group;
  std::string exact_name;
  std::uint32_t pass_count = 1U;
  std::uint32_t texture_unit_count = 0U;
  bool has_vertex_program = false;
  bool has_fragment_program = false;
  bool lighting_enabled = true;
  Float4 diffuse_linear{1.0F, 1.0F, 1.0F, 1.0F};
  Float3 ambient_linear{1.0F, 1.0F, 1.0F};
  Float3 specular_linear{};
  Float3 emissive_linear{};
  float shininess = 0.0F;
  Ogre14GraphicsSceneMaterialBlend blend =
      Ogre14GraphicsSceneMaterialBlend::REPLACE;
  Ogre14GraphicsSceneMaterialCull cull =
      Ogre14GraphicsSceneMaterialCull::CLOCKWISE;
  Ogre14GraphicsSceneMaterialAlphaReject alpha_reject =
      Ogre14GraphicsSceneMaterialAlphaReject::ALWAYS_PASS;
  std::uint8_t alpha_reject_value = 0U;
  bool depth_write = true;
};

/// Exact identity of one effective OGRE submesh draw range. A single OGRE
/// Mesh may therefore yield several independently material-bound portable mesh
/// assets. Reverse winding is part of the identity because OGRE supports both
/// clockwise- and anticlockwise-front materials while the portable contract
/// has one CCW convention.
struct Ogre14GraphicsSceneMeshAssetIdentity {
  std::string exact_resource_group;
  std::string exact_mesh_name;
  std::uint32_t submesh_index = 0U;
  std::uint32_t vertex_start = 0U;
  std::uint32_t vertex_count = 0U;
  std::uint32_t index_start = 0U;
  std::uint32_t index_count = 0U;
  bool reverse_winding = false;
};

/// CPU streams copied from one immutable OGRE 14 render operation. OGRE 14
/// and the portable contract share right-handed +Y-up object space, upper-left
/// UV origin, and CCW front faces for the default clockwise-cull mode. The
/// pure builder therefore preserves basis and UV values byte-for-byte and
/// swaps only triangle indices when reverse_winding is explicitly requested.
struct Ogre14GraphicsSceneCpuMeshSectionInput {
  std::string debug_name;
  MeshIndexFormat index_format = MeshIndexFormat::UINT32;
  std::uint64_t topology_revision = 1U;
  bool reverse_winding = false;
  std::vector<Float3> positions;
  std::vector<Float3> normals;
  std::vector<Float4> tangents;
  std::vector<Float2> texture_coordinates_0;
  std::vector<Float2> texture_coordinates_1;
  std::vector<Float4> colors;
  std::vector<std::uint32_t> indices;
};

/// One material-bound static section after native CPU extraction. The
/// graphics-owned object ID is monotonically allocated by
/// TerrainObjectManager and never derived from vector position or display
/// text. Mesh/material payloads and object transforms remain transactional.
struct Ogre14GraphicsSceneStaticSectionCaptureInput {
  std::uint64_t stable_object_id = 0U;
  std::uint32_t section_index = 0U;
  std::string exact_entity_name;
  Ogre14GraphicsSceneMeshAssetIdentity mesh_identity;
  std::shared_ptr<const RenderAssetPayload> mesh_payload;
  Ogre14GraphicsSceneMaterialCaptureInput material;
  /// Optional exact translated material transaction. When absent, the
  /// original factor-only compatibility path is used unchanged. When present,
  /// no factor fallback or semantic coercion is permitted.
  std::shared_ptr<const Ogre14LegacyMaterialClosure> resolved_material;
  Matrix4x4 render_from_object;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  bool visible = true;
  bool casts_shadows = true;
  bool receives_shadows = true;
  bool visible_in_reflections = true;
  /// Present only for a canonical terrain-page section. The exact binary key
  /// is collision-audited independently from the derived page ID so two page
  /// source identities can never alias before section IDs are derived.
  std::string exact_terrain_page_key;
};

/// Adapter-side immutable CPU cache entry. `native_mesh` is an opaque
/// identity token only; it is never dereferenced by renderer-neutral code.
struct Ogre14GraphicsSceneStaticMeshCacheEntry {
  const void *native_mesh = nullptr;
  std::size_t native_state_count = 0U;
  std::shared_ptr<const RenderAssetPayload> payload;
};

enum class Ogre14GraphicsSceneTerrainAlignment : std::uint8_t {
  X_Z = 0U,
  X_Y = 1U,
  Y_Z = 2U,
};

/// Exact TerrainGroup source locator for one slot. Slot coordinates are the
/// signed OGRE group coordinates, not iteration position or packed-map order.
struct Ogre14GraphicsSceneTerrainPageIdentity {
  std::string exact_resource_group;
  std::string exact_filename_prefix;
  std::string exact_filename_extension;
  std::string exact_slot_filename;
  std::int32_t slot_x = 0;
  std::int32_t slot_y = 0;
};

/// Complete native terrain texture/material audit. Version one can publish
/// only an exact factor-only material. Every authored terrain layer, blend,
/// global-colour, light, or composite texture therefore remains visible to a
/// stable fail-closed gate instead of being silently flattened.
struct Ogre14GraphicsSceneTerrainMaterialAuditInput {
  std::uint32_t layer_count = 0U;
  std::uint32_t sampler_count = 0U;
  std::vector<float> layer_world_sizes;
  /// Flattened layer-major, sampler-minor exact texture names.
  std::vector<std::string> layer_texture_names;
  std::uint32_t blend_texture_count = 0U;
  std::vector<std::string> blend_texture_names;
  bool global_colour_map_enabled = false;
  std::string exact_global_colour_map_name;
  bool has_lightmap = false;
  std::string exact_lightmap_name;
  bool has_composite_map = false;
  std::string exact_composite_map_name;
};

/// Renderer-neutral copy of one fully loaded OGRE 14 Terrain page. Heights
/// are row-major from terrain point (0,0), matching getHeightData(). The
/// (size+2)^2 neighbourhood contains getPointFromSelfOrNeighbour(x-1,y-1)
/// positions relative to this page's centre and is used to reproduce OGRE's
/// eight-face normal derivation exactly at page boundaries without touching
/// mutable derived-data state. `highest_lod_prepared == 0` proves the complete
/// CPU grid is resident. Loaded/target LOD are audited native GPU/draw state;
/// they do not select canonical topology or enter its immutable state key.
struct Ogre14GraphicsSceneTerrainPageCaptureInput {
  std::uint32_t version = kOgre14TerrainCpuCaptureVersion;
  Ogre14GraphicsSceneTerrainPageIdentity identity;
  Ogre14GraphicsSceneTerrainAlignment alignment =
      Ogre14GraphicsSceneTerrainAlignment::X_Z;
  std::uint32_t size = 0U;
  std::uint32_t minimum_batch_size = 0U;
  std::uint32_t maximum_batch_size = 0U;
  std::uint32_t lod_level_count = 0U;
  std::uint32_t lod_levels_per_leaf = 0U;
  std::int32_t highest_lod_prepared = -1;
  std::int32_t highest_lod_loaded = -1;
  std::int32_t target_lod_level = -1;
  float world_size = 0.0F;
  float skirt_size = 0.0F;
  Float3 page_world_position{};
  std::vector<float> height_samples;
  std::vector<Float3> normal_neighbourhood_positions;
  bool derived_data_update_in_progress = false;
  bool has_holes = false;
  Ogre14GraphicsSceneTerrainMaterialAuditInput material_audit;
  Ogre14GraphicsSceneMaterialCaptureInput material;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  bool visible = true;
  bool casts_shadows = true;
  bool receives_shadows = true;
  bool visible_in_reflections = true;
};

/// Immutable full-page CPU cache. The exact state key contains every geometry
/// scalar and IEEE-754 sample byte, so equality never relies on a lossy hash.
struct Ogre14GraphicsSceneTerrainPageCacheEntry {
  std::string exact_geometry_state_key;
  std::uint64_t topology_revision = 0U;
  std::shared_ptr<const RenderAssetPayload> mesh_payload;
};



/// Adapter-side immutable topology cache for one actor deformable section.
/// The numeric resource handle is copied while the Mesh is live; no native
/// pointer survives actor removal. Dynamic positions/normals are deliberately
/// not cached here; every frame owns a fresh post-join CPU staging copy.
struct Ogre14GraphicsSceneDynamicMeshCacheEntry {
  std::uint64_t native_mesh_handle = 0U;
  std::size_t native_state_count = 0U;
  std::uint64_t cpu_topology_revision = 0U;
  std::uint32_t vertex_start = 0U;
  std::uint32_t vertex_count = 0U;
  std::uint32_t index_start = 0U;
  std::uint32_t index_count = 0U;
  bool reverse_winding = false;
  std::shared_ptr<const RenderAssetPayload> payload;
};

/// Every actor-owned draw family the dynamic inventory can carry. The value is
/// one byte of the section identity key, so a family added here can never
/// collide with an existing one; values are permanent and must not be reused.
///
/// CAB, FLEXBODY, FLEXMESH_WHEEL and MESHWHEEL_TIRE are deformables: their
/// motion lives in per-frame CPU staging and their scene node never moves.
/// MESHWHEEL_RIM, PROP and PROP_STEERING_WHEEL are rigid authored meshes whose
/// motion lives entirely in the scene node the actor re-poses each frame.
enum class Ogre14GraphicsSceneDynamicComponentKind : std::uint8_t {
  CAB = 0U,
  FLEXBODY = 1U,
  FLEXMESH_WHEEL = 2U,
  MESHWHEEL_TIRE = 3U,
  MESHWHEEL_RIM = 4U,
  PROP = 5U,
  PROP_STEERING_WHEEL = 6U,
};

/// Stable source identity of one actor-owned deformable draw section. Actor,
/// flexbody/cab/wheel, and section ordinals are creation identities, never
/// current vector positions or mutable OGRE display names.
struct Ogre14GraphicsSceneDynamicSectionIdentity {
  std::int64_t actor_instance_id = -1;
  Ogre14GraphicsSceneDynamicComponentKind component_kind =
      Ogre14GraphicsSceneDynamicComponentKind::CAB;
  std::uint32_t component_id = 0U;
  std::uint32_t section_index = 0U;
};

/// Complete copy of one post-join CPU staging range. This owner never aliases
/// NodeSB, Actor, solver, or OGRE hardware-buffer memory.
struct Ogre14GraphicsSceneJoinedDynamicState {
  std::uint64_t topology_revision = 1U;
  std::vector<Float3> positions;
  std::vector<Float3> normals;
  std::vector<Float4> tangents;
  std::vector<Float3> velocities;
  Bounds3 updated_local_bounds;
};

struct Ogre14GraphicsSceneDynamicSectionCaptureInput {
  Ogre14GraphicsSceneDynamicSectionIdentity identity;
  std::string exact_entity_name;
  std::shared_ptr<const RenderAssetPayload> mesh_payload;
  Ogre14GraphicsSceneMaterialCaptureInput material;
  /// Optional exact translated material transaction. This is the same
  /// closure contract consumed by static sections. When absent, the original
  /// factor-only compatibility path remains the sole material path. When
  /// present, dependencies and producer-owned bindings are published exactly;
  /// their catalog lifecycle remains owned by the translator.
  std::shared_ptr<const Ogre14LegacyMaterialClosure> resolved_material;
  /// Exact topology conversion proof used only with `resolved_material`.
  /// The live adapter sets this from the native draw's winding conversion;
  /// the required value comes from the translated pipeline audit, never from
  /// compatibility fallback state.
  bool mesh_reverse_winding = false;
  Matrix4x4 render_from_object;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  bool visible = true;
  bool casts_shadows = true;
  bool receives_shadows = true;
  bool visible_in_reflections = true;
  /// FlexBody blend colors are frame-varying and cannot be omitted silently.
  bool has_dynamic_vertex_colors = false;
  std::shared_ptr<const Ogre14GraphicsSceneJoinedDynamicState> state;
};

struct Ogre14GraphicsSceneResolvedMaterialFrameLineage {
  std::uint64_t source_sequence = 0U;
  std::uint64_t catalog_sequence = 0U;

  [[nodiscard]] bool empty() const noexcept {
    return source_sequence == 0U && catalog_sequence == 0U;
  }
};

/// Revalidates every detached exact closure across both static and dynamic
/// candidate inventories and proves that all resolved materials came from one
/// source/catalog frame. The joined caller must run this before merging the
/// two asset vectors. Failure leaves `lineage` untouched.
[[nodiscard]] ValidationResult
ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage(
    const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput>
        &static_inputs,
    const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput>
        &dynamic_inputs,
    Ogre14GraphicsSceneResolvedMaterialFrameLineage &lineage);

/// Collision-audited, transactional identity/lifecycle and semantic-revision
/// owner for the actor deformable inventory. Removed identities are permanent
/// tombstones for this adapter lifetime.
class Ogre14GraphicsSceneDynamicIdentityRegistry final {
public:
  [[nodiscard]] ValidationResult RegisterDerivedAssetIdentity(
      std::string_view exact_key, std::uint64_t stable_id);

  [[nodiscard]] std::size_t asset_identity_count() const noexcept {
    return asset_names_by_id_.size();
  }
  [[nodiscard]] std::size_t object_identity_count() const noexcept {
    return object_names_by_id_.size();
  }
  void Reset() noexcept {
    asset_names_by_id_.clear();
    asset_ids_by_name_.clear();
    object_names_by_id_.clear();
    object_ids_by_name_.clear();
    canonical_assets_by_asset_key_.clear();
    object_states_.clear();
    known_asset_keys_.clear();
    live_asset_keys_.clear();
    known_object_keys_.clear();
    live_object_keys_.clear();
  }

private:
  struct ObjectState {
    std::string exact_entity_name;
    std::string mesh_key;
    std::string material_key;
    std::shared_ptr<const GraphicsSceneDynamicMeshState> deformation;
  };

  friend ValidationResult BuildOgre14GraphicsSceneDynamicInventory(
      const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> &,
      Ogre14GraphicsSceneDynamicIdentityRegistry &,
      std::vector<GraphicsSceneAssetInput> &,
      std::vector<GraphicsSceneDynamicMeshInput> &,
      class IOgre14GraphicsSceneDynamicInventoryFaultInjector *);

  std::map<std::uint64_t, std::string> asset_names_by_id_;
  std::map<std::string, std::uint64_t, std::less<>> asset_ids_by_name_;
  std::map<std::uint64_t, std::string> object_names_by_id_;
  std::map<std::string, std::uint64_t, std::less<>> object_ids_by_name_;
  std::map<std::string, GraphicsSceneAssetInput, std::less<>>
      canonical_assets_by_asset_key_;
  std::map<std::string, ObjectState, std::less<>> object_states_;
  std::set<std::string, std::less<>> known_asset_keys_;
  std::set<std::string, std::less<>> live_asset_keys_;
  std::set<std::string, std::less<>> known_object_keys_;
  std::set<std::string, std::less<>> live_object_keys_;
};

enum class Ogre14GraphicsSceneDynamicInventoryFaultPoint : std::uint8_t {
  AFTER_FIRST_RESOLVED_DEPENDENCY = 0U,
};

/// Borrowed test-only exception seam. Production callers leave this null.
class IOgre14GraphicsSceneDynamicInventoryFaultInjector {
public:
  virtual ~IOgre14GraphicsSceneDynamicInventoryFaultInjector() = default;
  virtual void AtFaultPoint(
      Ogre14GraphicsSceneDynamicInventoryFaultPoint point) = 0;
};

[[nodiscard]] ValidationResult DeriveOgre14GraphicsSceneDynamicMeshAssetId(
    const Ogre14GraphicsSceneDynamicSectionIdentity &identity,
    std::uint64_t &stable_id);
[[nodiscard]] ValidationResult DeriveOgre14GraphicsSceneDynamicSectionId(
    const Ogre14GraphicsSceneDynamicSectionIdentity &identity,
    std::uint64_t &stable_id);

/// Builds immutable base topology/UV/color/index storage for a deformable
/// allocation. Failure leaves the caller's owner untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneDynamicMeshPayload(
    const Ogre14GraphicsSceneCpuMeshSectionInput &input,
    std::shared_ptr<const RenderAssetPayload> &payload);

/// Canonicalizes the complete actor deformable inventory, owns semantic
/// deformation revisions and immutable owner reuse, and commits lifecycle
/// state only after every section succeeds.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneDynamicInventory(
    const std::vector<Ogre14GraphicsSceneDynamicSectionCaptureInput> &inputs,
    Ogre14GraphicsSceneDynamicIdentityRegistry &identity_registry,
    std::vector<GraphicsSceneAssetInput> &assets,
    std::vector<GraphicsSceneDynamicMeshInput> &dynamic_meshes,
    IOgre14GraphicsSceneDynamicInventoryFaultInjector *fault_injector =
        nullptr);

struct Ogre14GraphicsSceneUnsupportedGeometry {
  bool terrain = false;
  bool procedural = false;
  /// Skeletal/deformable geometry outside the supported GfxActor dynamic
  /// inventory. GfxCharacter avatars are a separately declared legacy-only
  /// domain and must not be routed through this static-coverage blocker.
  bool unadapted_deformable = false;
  bool paged = false;
  bool animated = false;
};

class IOgre14GraphicsSceneStaticInventoryFaultInjector;

/// Collision-audited source identity and lifecycle state for all static assets
/// and section instances. A complete successful inventory commits atomically;
/// omission tombstones an identity, and later resurrection fails closed.
class Ogre14GraphicsSceneStaticIdentityRegistry final {
public:
  [[nodiscard]] ValidationResult RegisterDerivedAssetIdentity(
      std::string_view exact_key, std::uint64_t stable_id);
  [[nodiscard]] ValidationResult RegisterDerivedObjectIdentity(
      std::string_view exact_key, std::uint64_t stable_id);

  [[nodiscard]] std::size_t asset_identity_count() const noexcept {
    return asset_names_by_id_.size();
  }
  [[nodiscard]] std::size_t object_identity_count() const noexcept {
    return object_names_by_id_.size();
  }
  [[nodiscard]] std::size_t terrain_page_identity_count() const noexcept {
    return terrain_page_names_by_id_.size();
  }
  [[nodiscard]] ValidationResult RegisterDerivedTerrainPageIdentity(
      std::string_view exact_key, std::uint64_t stable_id);

  /// Static sections filtered out of the capture because their transform
  /// carries a non-uniform scale the pinned PBS tangent path cannot represent.
  /// Refusing to draw such a section is correct; refusing to draw the whole
  /// terrain is not, so the section is dropped and counted rather than failing
  /// the inventory. Deliberately monotonic for the process lifetime -- a
  /// degrade counter that a generation reset zeroes hides the degrade.
  [[nodiscard]] std::uint64_t
  non_uniform_scale_sections_filtered() const noexcept {
    return non_uniform_scale_sections_filtered_;
  }

  void Reset() noexcept {
    asset_names_by_id_.clear();
    asset_ids_by_name_.clear();
    object_names_by_id_.clear();
    object_ids_by_name_.clear();
    canonical_assets_by_asset_key_.clear();
    known_asset_keys_.clear();
    live_asset_keys_.clear();
    known_object_keys_.clear();
    live_object_keys_.clear();
    terrain_page_names_by_id_.clear();
    terrain_page_ids_by_name_.clear();
    known_terrain_page_keys_.clear();
    live_terrain_page_keys_.clear();
  }

private:
  friend ValidationResult BuildOgre14GraphicsSceneStaticInventory(
      const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &,
      Ogre14GraphicsSceneStaticIdentityRegistry &,
      std::vector<GraphicsSceneAssetInput> &,
      std::vector<GraphicsSceneStaticMeshInput> &,
      IOgre14GraphicsSceneStaticInventoryFaultInjector *);

  std::map<std::uint64_t, std::string> asset_names_by_id_;
  std::map<std::string, std::uint64_t, std::less<>> asset_ids_by_name_;
  std::map<std::uint64_t, std::string> object_names_by_id_;
  std::map<std::string, std::uint64_t, std::less<>> object_ids_by_name_;
  std::map<std::string, GraphicsSceneAssetInput, std::less<>>
      canonical_assets_by_asset_key_;
  std::set<std::string, std::less<>> known_asset_keys_;
  std::set<std::string, std::less<>> live_asset_keys_;
  std::set<std::string, std::less<>> known_object_keys_;
  std::set<std::string, std::less<>> live_object_keys_;
  std::map<std::uint64_t, std::string> terrain_page_names_by_id_;
  std::map<std::string, std::uint64_t, std::less<>>
      terrain_page_ids_by_name_;
  std::set<std::string, std::less<>> known_terrain_page_keys_;
  std::set<std::string, std::less<>> live_terrain_page_keys_;
  std::uint64_t non_uniform_scale_sections_filtered_ = 0U;
};

enum class Ogre14GraphicsSceneStaticInventoryFaultPoint : std::uint8_t {
  AFTER_FIRST_RESOLVED_DEPENDENCY = 0U,
};

/// Borrowed test-only exception seam. Production callers leave this null.
class IOgre14GraphicsSceneStaticInventoryFaultInjector {
public:
  virtual ~IOgre14GraphicsSceneStaticInventoryFaultInjector() = default;
  virtual void AtFaultPoint(
      Ogre14GraphicsSceneStaticInventoryFaultPoint point) = 0;
};

[[nodiscard]] ValidationResult ValidateOgre14GraphicsSceneStaticCoverage(
    const Ogre14GraphicsSceneUnsupportedGeometry &unsupported);

[[nodiscard]] ValidationResult DeriveOgre14GraphicsSceneMeshAssetId(
    const Ogre14GraphicsSceneMeshAssetIdentity &identity,
    std::uint64_t &stable_id);
[[nodiscard]] ValidationResult DeriveOgre14GraphicsSceneMaterialAssetId(
    std::string_view exact_resource_group, std::string_view exact_name,
    std::uint64_t &stable_id);
[[nodiscard]] ValidationResult DeriveOgre14GraphicsSceneStaticSectionId(
    std::uint64_t stable_object_id, std::uint32_t section_index,
    std::uint64_t &stable_id);

[[nodiscard]] ValidationResult DeriveOgre14GraphicsSceneTerrainPageId(
    const Ogre14GraphicsSceneTerrainPageIdentity &identity,
    std::uint64_t &stable_id);

/// Validates the complete native texture/material audit and the factor-only
/// fallback before a caller allocates the potentially large canonical mesh.
[[nodiscard]] ValidationResult
ValidateOgre14GraphicsSceneTerrainMaterialCapture(
    const Ogre14GraphicsSceneTerrainMaterialAuditInput &audit,
    const Ogre14GraphicsSceneMaterialCaptureInput &material);

/// Validates a complete page inventory, exact slot uniqueness, common group
/// geometry, material audit, and every present east/north shared edge. Failure
/// leaves callers' outputs and lifecycle state untouched.
[[nodiscard]] ValidationResult ValidateOgre14GraphicsSceneTerrainPageSet(
    const std::vector<Ogre14GraphicsSceneTerrainPageCaptureInput> &pages);

/// Builds a collision-free byte key for native immutable-payload caching.
/// Failure leaves `key` untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneTerrainGeometryStateKey(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    std::string &key);

/// Builds the full authored LOD0 grid plus page-perimeter skirts as one
/// immutable CCW triangle-list mesh. Internal quadtree skirts and morph deltas
/// are intentionally absent because this canonical topology has no internal
/// LOD draw boundaries. Failure leaves `payload` untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneTerrainMeshPayload(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    std::uint64_t topology_revision,
    std::shared_ptr<const RenderAssetPayload> &payload);

/// Resolves one immutable terrain payload against an optional prior exact
/// cache entry. Same-state pages reuse the owner and revision; changed pages
/// advance once. Failure leaves `entry` untouched.
[[nodiscard]] ValidationResult
ResolveOgre14GraphicsSceneTerrainPageCacheEntry(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    const Ogre14GraphicsSceneTerrainPageCacheEntry *previous,
    Ogre14GraphicsSceneTerrainPageCacheEntry &entry);

/// Binds one validated page payload to its exact page/section/material
/// identities and transform. The current factor-only terrain material gate is
/// explicit and transactional.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneTerrainSection(
    const Ogre14GraphicsSceneTerrainPageCaptureInput &input,
    const std::shared_ptr<const RenderAssetPayload> &mesh_payload,
    Ogre14GraphicsSceneStaticSectionCaptureInput &section);

/// Builds an immutable tight-bounds triangle-list payload. Failure leaves the
/// caller's owner untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneStaticMeshPayload(
    const Ogre14GraphicsSceneCpuMeshSectionInput &input,
    std::shared_ptr<const RenderAssetPayload> &payload);

/// Builds the versioned factor-only portable fallback. Failure leaves the
/// material untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneMaterialFallback(
    const Ogre14GraphicsSceneMaterialCaptureInput &input,
    MaterialDescriptor &material);

/// Converts the complete supported static-section inventory, deduplicates
/// shared mesh/material resources, validates every mesh/material pairing,
/// canonicalizes stable ordering, and commits identity/lifecycle state only
/// after the entire candidate succeeds. All outputs remain unchanged on
/// failure.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneStaticInventory(
    const std::vector<Ogre14GraphicsSceneStaticSectionCaptureInput> &inputs,
    Ogre14GraphicsSceneStaticIdentityRegistry &identity_registry,
    std::vector<GraphicsSceneAssetInput> &assets,
    std::vector<GraphicsSceneStaticMeshInput> &static_meshes,
    IOgre14GraphicsSceneStaticInventoryFaultInjector *fault_injector =
        nullptr);

/// OGRE 14's ambient scene color is already consumed as a renderer-linear
/// multiplier. The bridge defines one native ambient unit as one canonical
/// radiance unit so Ogre-Next receives the same numeric linear RGB without a
/// display-gamma round trip or an unaudited exposure multiplier.
constexpr float kOgre14AmbientNativeUnitRadiance = 1.0F;

/// Compatibility calibration, not a claim of measured physical photometry.
/// One unit of OGRE 14's renderer-linear `diffuse * powerScale` becomes 1024
/// canonical lux (directional) or candela (local). Ogre-Next RT4 applies the
/// exact reciprocal 1/1024 native-power scale, reproducing the legacy direct
/// RGB term before either renderer's distance/cone attenuation.
constexpr float kOgre14LegacyDiffusePowerToCanonicalIntensity = 1024.0F;
constexpr std::uint32_t kOgre14LightCompatibilityCalibrationVersion = 1U;

/// SkyX's native shader is azimuth-dependent and can operate after its own
/// LDR exposure curve, so its pixels cannot be copied exactly into the
/// renderer-neutral azimuth-independent linear gradient contract. Policy v1
/// deliberately derives a bounded-shape modern sky from only the joined live
/// ambient value and the exact captured main-light direction/chromaticity/
/// power. Changing any coefficient is therefore a reviewed policy revision.
constexpr std::uint32_t kOgre14ModernAnalyticSkyPolicyVersion = 4U;
constexpr float kOgre14ModernAnalyticSunAngularRadiusRadians = 0.00465047F;
/// Policy v2: the pinned Ogre-Next HDR tonemap was reviewed against scenes
/// several stops brighter than the native OGRE 14 lighting domain, so the
/// whole joined scene rendered in the filmic toe - light albedos survived
/// as dim gray while dark albedos (city asphalt) crushed to black. The
/// analytic-sky environment declares the compensation that seats native
/// mid-tones on the curve's linear section; the sky gradient then lands
/// just below the filmic white point. This scales every surface equally
/// and cannot change shadow-to-sunlit ratios.
constexpr float kOgre14ModernAnalyticSkyExposureCompensationEv = 3.5F;
/// Policy v2 also derives the hemisphere ambient from the sun instead of
/// forwarding the legacy scene ambient: OGRE 14 content authors ambient
/// nearly equal to the sun (0.7 vs 0.867), which lights every face almost
/// identically - a flat, washed look with no directional contrast. Under
/// HlmsPbs AmbientFixed, shadow/sunlit = f/(1+f).
///
/// 0.15 seated fully shadowed surfaces at ~13% of sunlit, which is clear-day
/// photometry for an OPEN horizontal surface seeing the whole sky. It is too
/// low for the shadow that dominates this content. A street between facades
/// sees little sky but is filled by inter-reflection off those facades, and
/// facade inter-reflection remains unmodeled: Foundation F2 gave the
/// presenter SH-9 sky irradiance and a seated sky-carrying reflection probe,
/// both of which derive their absolute level from THIS term, but neither adds
/// bounce energy beyond it. This one term therefore still stands in for sky
/// AND bounce, not sky alone.
///
/// Being a fraction of the key light it stays correct as the sun moves, and
/// it lifts shadowed surfaces far more than sunlit ones, so it opens shadow
/// without flattening directional contrast. 0.25 seated fully shadowed
/// surfaces at 20% of sunlit and measured imperceptible in live play - the
/// F2 SH/probe directionality rides on this level, so a level too low hides
/// the entire indirect stack. 0.40 (shadow at ~29% of sunlit) is the
/// calibrated raise toward the user's directed realism target: measured to
/// keep open grass green-dominant (G>R>B, the withdrawn-hemisphere wash-out
/// gate) while making the canyon's sky-tinted directional fill plainly
/// visible.
constexpr float kOgre14ModernAnalyticSkyAmbientSunFraction = 0.40F;
/// Policy v3 adds the deterministic cloud layer. Coverage scales with the
/// same smoothstepped daylight term as the gradient so clouds fade out with
/// the sun instead of floating over a night sky; the cloud radiance sits
/// between the horizon band and a sun-tinted white so the layer reads as lit
/// vapour rather than as a second gradient. The drift rate is radians of
/// pattern phase per simulation second - simulation time is the only clock so
/// replayed captures reproduce identical cloud geometry.
constexpr float kOgre14ModernAnalyticSkyCloudCoverageDaylightFraction = 0.45F;
constexpr float kOgre14ModernAnalyticSkyCloudHorizonFraction = 0.5F;
constexpr float kOgre14ModernAnalyticSkyCloudSunFraction = 0.10F;
constexpr float kOgre14ModernAnalyticSkyCloudPhaseRadiansPerSecond = 0.004F;
/// Policy v4 adds aerial perspective. The camera far plane reaches 12 km, so
/// every admitted city block is visible and - without an atmosphere - reads as
/// an unhazed cutout against the sky gradient. Visibility is the Koschmieder
/// meteorological range for a clear day: extinction = 3.912 / visibility, so
/// 40 km gives sigma ~= 9.78e-5 /m and transmittance 0.91 at 1 km, 0.46 at
/// 8 km, 0.31 at the far plane. The night fraction keeps 55% of that daytime
/// extinction so distant lit content stays readable after dark instead of
/// dissolving into an unlit horizon. The scale height is the standard aerosol
/// value; it makes an elevated camera exponentially clearer and re-accumulates
/// haze on the closed-form slant path when looking back down into the layer.
/// These three numbers are the whole aerial-perspective policy - the presenter
/// derives nothing and there is no user-facing configuration.
constexpr float kOgre14ModernAnalyticSkyHazeVisibilityMeters = 40000.0F;
constexpr float kOgre14ModernAnalyticSkyHazeNightFraction = 0.55F;
constexpr float kOgre14ModernAnalyticSkyHazeScaleHeightMeters = 1200.0F;

enum class Ogre14GraphicsSceneLightKind : std::uint8_t {
  POINT = 0U,
  DIRECTIONAL = 1U,
  SPOT = 2U,
  RECTANGLE = 3U,
};

/// Renderer-neutral copy of every OGRE 14 value read for one managed Light.
/// `inner/outer_cone_radians` are OGRE's authored full cone angles. The
/// specular color, visibility/light masks, attenuation coefficients, and
/// spotlight falloff are retained here so the adapter audits native state
/// explicitly; portable scene schema v4 can preserve only diffuse
/// chromaticity/power, range, cones, and shadow enable from those properties.
struct Ogre14GraphicsSceneLightCaptureInput {
  std::string exact_name;
  Ogre14GraphicsSceneLightKind kind =
      Ogre14GraphicsSceneLightKind::POINT;
  Float3 diffuse_linear{1.0F, 1.0F, 1.0F};
  Float3 specular_linear{};
  float power_scale = 1.0F;
  bool visible = true;
  std::uint32_t visibility_flags = 0xFFFFFFFFU;
  std::uint32_t light_mask = 0xFFFFFFFFU;
  Float3 derived_position{};
  Float3 derived_direction{0.0F, -1.0F, 0.0F};
  float attenuation_range = 0.0F;
  float attenuation_constant = 1.0F;
  float attenuation_linear = 0.0F;
  float attenuation_quadratic = 0.0F;
  float inner_cone_radians = 0.0F;
  float outer_cone_radians = 0.0F;
  float spot_falloff = 1.0F;
  bool casts_shadows = true;
};

/// Retains the exact-name/u64 bijection for one map generation. The registry
/// catches both a hash collision and inconsistent identity reuse; the ordered
/// scene-generation boundary resets it only after the prior empty scene has
/// been admitted to the persistent product transport.
class Ogre14GraphicsSceneLightIdentityRegistry final {
public:
  /// Registers a caller-derived stable identity. Exposed as a narrow pure-data
  /// seam so collision behavior is testable without constructing OGRE objects.
  /// Failure leaves the registry unchanged.
  [[nodiscard]] ValidationResult RegisterDerivedIdentity(
      std::string_view exact_name, std::uint64_t stable_id);

  [[nodiscard]] std::size_t size() const noexcept {
    return names_by_id_.size();
  }
  void Reset() noexcept {
    names_by_id_.clear();
    ids_by_name_.clear();
  }

private:
  std::map<std::uint64_t, std::string> names_by_id_;
  std::map<std::string, std::uint64_t, std::less<>> ids_by_name_;
};

/// Domain-separated FNV-1a-64 over the exact OGRE Light name bytes. Empty
/// names and the reserved zero identity fail closed and leave `stable_id`
/// untouched.
[[nodiscard]] ValidationResult DeriveOgre14GraphicsSceneLightId(
    std::string_view exact_name, std::uint64_t &stable_id);

/// Pure conversion of one native light. Failure leaves `light` untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneLight(
    const Ogre14GraphicsSceneLightCaptureInput &input,
    GraphicsSceneLightInput &light);

/// Converts a complete authoritative inventory, rejects duplicate exact names
/// and identity collisions, and sorts by stable identity. Both output and
/// registry commit only if every record converts.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneLights(
    const std::vector<Ogre14GraphicsSceneLightCaptureInput> &inputs,
    Ogre14GraphicsSceneLightIdentityRegistry &identity_registry,
    std::vector<GraphicsSceneLightInput> &lights);

/// Converts the complete constant-ambient state supported by OGRE 14. The
/// legacy bridge has no compatible authored linear-float equirectangular
/// environment asset or scene-level exposure value; those optional fields
/// remain canonically absent and identity-valued. This base conversion does
/// not invent sky state; the explicitly versioned live main-light policy below
/// may extend its staged candidate. Failure leaves `environment` untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneEnvironment(
    const Float3 &native_ambient_linear,
    SceneEnvironmentDescriptor &environment);

/// Adds versioned analytic sky state to the exact constant-ambient
/// conversion. `sun` must be the matching converted live directional light;
/// its stable identity is referenced directly, never recreated.
/// `simulation_time_seconds` must be the joined finite nonnegative simulation
/// time; it drives only the deterministic cloud phase, never a wall clock.
/// Failure leaves `environment` untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneAnalyticSkyEnvironment(
    const Float3 &native_ambient_linear,
    const GraphicsSceneLightInput &sun,
    double simulation_time_seconds,
    SceneEnvironmentDescriptor &environment);

/// Builds the canonical right-handed, [0,1]-depth camera contract without
/// consuming an API-specific OGRE projection matrix. Failure leaves `camera`
/// untouched.
[[nodiscard]] ValidationResult BuildOgre14GraphicsSceneCamera(
    const Ogre14CameraCaptureInput &input,
    GraphicsSceneCameraInput &camera);

} // namespace RoR::Render
