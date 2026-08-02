/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.
*/

/// @file
/// @brief Transactional joined-graphics-scene to render-contract producer.

#pragma once

#include "RenderAssetRegistry.h"
#include "RenderFrame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace RoR::Render {

constexpr std::uint32_t kGraphicsSceneSnapshotProducerVersion = 4U;
constexpr std::size_t kGraphicsSceneMaterialTextureSlotCount = 5U;

/// Source identities belong to the joined graphics scene, not a renderer.
/// Zero is the canonical absent identity. A nonzero identity is never reused
/// for a different object or asset during one producer lifetime.
struct GraphicsSceneAssetBinding {
  std::uint64_t texture_source_asset_id = 0U;
  std::uint64_t sampler_source_asset_id = 0U;

  friend constexpr bool
  operator==(const GraphicsSceneAssetBinding &lhs,
             const GraphicsSceneAssetBinding &rhs) noexcept {
    return lhs.texture_source_asset_id == rhs.texture_source_asset_id &&
           lhs.sampler_source_asset_id == rhs.sampler_source_asset_id;
  }
  friend constexpr bool
  operator!=(const GraphicsSceneAssetBinding &lhs,
             const GraphicsSceneAssetBinding &rhs) noexcept {
    return !(lhs == rhs);
  }
};

/// One authoritative live asset from a joined graphics-side inventory.
///
/// Material payload references must be canonical absent. Their portable
/// texture/sampler references are resolved from material_bindings so the
/// producer, rather than an OGRE adapter, owns RenderAssetIds and revisions.
/// Slots use MaterialTextureSlot numeric order. Non-material assets require
/// every binding to be absent.
struct GraphicsSceneAssetInput {
  std::uint64_t source_asset_id = 0U;
  /// Immutable ownership lets a graphics-side resource cache submit the same
  /// large descriptor every frame without copying or comparing vertex/texel
  /// storage. Adapters should preserve this owner for an asset revision.
  std::shared_ptr<const RenderAssetPayload> payload;
  std::array<GraphicsSceneAssetBinding,
             kGraphicsSceneMaterialTextureSlotCount>
      material_bindings{};
};

/// One static MeshObject/terrain-object style instance. The referenced mesh
/// supplies exact local bounds and topology revision.
struct GraphicsSceneStaticMeshInput {
  std::uint64_t source_object_id = 0U;
  std::uint64_t mesh_source_asset_id = 0U;
  std::uint64_t material_source_asset_id = 0U;
  Matrix4x4 render_from_object;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  std::uint32_t flags = MESH_INSTANCE_DEFAULT_FLAGS;
};

/// One complete immutable CPU deformation state copied from a fully joined
/// graphics staging array. It never aliases simulation-owned nodes or solver
/// memory. An unchanged semantic state may reuse the same owner and revision;
/// changed contents require the next exact deformation revision.
struct GraphicsSceneDynamicMeshState {
  std::uint64_t topology_revision = 1U;
  std::uint64_t deformation_revision = 2U;
  std::vector<Float3> positions;
  std::vector<Float3> normals;
  std::vector<Float4> tangents;
  std::vector<Float3> velocities;
  Bounds3 updated_local_bounds;
};

/// One live deformable section. Base topology, immutable UV/color streams, and
/// material remain source assets; the state owner contains the full current
/// position/direction streams and tight local bounds. Source identities are
/// permanent for one producer lifetime and share the static-object namespace.
struct GraphicsSceneDynamicMeshInput {
  std::uint64_t source_object_id = 0U;
  std::uint64_t mesh_source_asset_id = 0U;
  std::uint64_t material_source_asset_id = 0U;
  Matrix4x4 render_from_object;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
  std::uint32_t flags = MESH_INSTANCE_DEFAULT_FLAGS;
  std::shared_ptr<const GraphicsSceneDynamicMeshState> state;
};

/// One authoritative analytic light. The source identity is preserved as the
/// portable light identity and may never change type or return after removal.
/// Current values use the frame's render origin; the producer owns previous
/// position/direction history and rebases local-light positions.
struct GraphicsSceneLightInput {
  std::uint64_t source_light_id = 0U;
  LightType type = LightType::DIRECTIONAL;
  Float3 color_linear{1.0F, 1.0F, 1.0F};
  float intensity = 1.0F;
  Float3 position{};
  Float3 direction{0.0F, -1.0F, 0.0F};
  float range = 0.0F;
  float inner_cone_radians = 0.0F;
  float outer_cone_radians = 0.0F;
  std::uint32_t shadow_flags = LIGHT_SHADOW_DEFAULT_FLAGS;
};

/// The one renderer-neutral main camera in the first producer slice. Current
/// matrices are unjittered and relative to the frame's absolute world origin;
/// the producer owns previous matrices and origin rebasing.
struct GraphicsSceneCameraInput {
  std::uint64_t view_id = 0U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  Matrix4x4 view_from_render;
  Matrix4x4 clip_from_view;
  Float2 temporal_jitter_pixels{};
  float near_plane = 0.1F;
  float far_plane = 10000.0F;
  float exposure = 1.0F;
  std::uint32_t visibility_mask = 0xFFFFFFFFU;
};

/// Complete authoritative joined graphics state for the supported slice.
/// Asset and object vectors may arrive in any order; the producer canonicalizes
/// them by source identity and rejects duplicates. Inputs omitted from a later
/// successful frame are permanently destroyed.
struct GraphicsSceneFrameInput {
  std::uint32_t version = kGraphicsSceneSnapshotProducerVersion;
  std::uint64_t simulation_tick = 0U;
  double simulation_time_seconds = 0.0;
  Double3 absolute_world_origin_meters{};
  SceneEnvironmentDescriptor environment;
  std::uint64_t environment_texture_source_asset_id = 0U;
  std::uint64_t environment_sampler_source_asset_id = 0U;
  std::vector<GraphicsSceneAssetInput> assets;
  std::vector<GraphicsSceneStaticMeshInput> static_meshes;
  std::vector<GraphicsSceneDynamicMeshInput> dynamic_meshes;
  /// May arrive in any order. analytic_sky.sun_light_id names one of these
  /// stable source identities directly.
  std::vector<GraphicsSceneLightInput> lights;
  /// Absolute-world authored probes may arrive in any order. The producer
  /// canonicalizes them, enforces content-revision lineage, and permanently
  /// tombstones removed identities before publishing snapshot version 4.
  std::vector<ReflectionProbeRuntimeDescriptor> reflection_probes;
  GraphicsSceneCameraInput camera;
};

/// Exact integration seam for GfxScene. Implementations capture only after
/// GfxScene::BufferSimulationData() has completed and may read copied
/// simbuffers plus graphics-owned static inventories. They must never read live
/// physics objects. A failure leaves the producer and all renderer state
/// untouched.
class IJoinedGraphicsSceneSource {
public:
  virtual ~IJoinedGraphicsSceneSource() = default;
  /// Prepares one immutable frame without advancing source-side identity,
  /// lifecycle, or semantic-revision state. Exactly one Commit or Discard
  /// follows every successful capture.
  [[nodiscard]] virtual ValidationResult
  CaptureJoinedGraphicsFrame(GraphicsSceneFrameInput &frame) = 0;
  virtual void CommitJoinedGraphicsFrame() noexcept {}
  virtual void DiscardJoinedGraphicsFrame() noexcept {}
};

struct GraphicsSceneSnapshotProducerConfiguration {
  std::uint64_t registry_id = 0U;
  std::uint64_t first_snapshot_id = 1U;
  std::uint64_t first_asset_ordinal = 1U;
  std::size_t maximum_asset_records = 65536U;
  std::size_t maximum_static_mesh_objects = 65536U;
  std::size_t maximum_dynamic_mesh_objects = 65536U;
  /// Aggregate complete position count and copied dynamic stream bytes in one
  /// candidate frame. Both are checked transactionally before publication.
  std::uint64_t maximum_dynamic_vertex_count = 16U * 1024U * 1024U;
  std::uint64_t maximum_dynamic_payload_bytes = 512U * 1024U * 1024U;
  std::size_t maximum_light_records = 4096U;
  std::size_t maximum_reflection_probe_records = 256U;
  /// Sum of descriptor-owned string, vertex/index, and texel bytes in one
  /// authoritative frame. Container overhead is intentionally excluded.
  std::uint64_t maximum_asset_payload_bytes = 512U * 1024U * 1024U;
};

struct GraphicsSceneSnapshotProduction {
  struct Diagnostics {
    /// Full descriptor validations after immutable-owner identity misses.
    /// Same-owner stable frames report zero exactly.
    std::uint64_t asset_payload_full_validations = 0U;
    /// Sum of descriptor-owned candidate bytes covered by full validation.
    std::uint64_t asset_payload_candidate_bytes_validated = 0U;
    /// Full scene-to-registry compatibility passes. Exact previously validated
    /// mesh/material and environment revisions let stable frames report zero.
    std::uint64_t scene_asset_compatibility_full_validations = 0U;
    /// Deep source-payload equivalence fallbacks after immutable-owner
    /// identity misses. Same-owner stable frames report zero exactly.
    std::uint64_t asset_payload_fallback_comparisons = 0U;
    /// Sum of descriptor-owned candidate bytes presented to those fallbacks.
    /// This is an upper bound on bytes equality may inspect, not a timer.
    std::uint64_t asset_payload_candidate_bytes_compared = 0U;
  };

  /// Present on first production (a full snapshot) and whenever the logical
  /// catalog changes. Frames containing transform/camera-only changes reuse the
  /// preceding asset sequence and carry no delta.
  std::optional<RenderAssetDelta> asset_delta;
  std::shared_ptr<const SceneSnapshot> scene_snapshot;
  CameraViewRequest camera;
  Diagnostics diagnostics;
};

struct GraphicsSceneSnapshotProduceResult {
  GraphicsSceneSnapshotProduction production;
  ValidationResult validation;

  [[nodiscard]] bool ok() const noexcept {
    return production.scene_snapshot != nullptr && validation.ok();
  }
  explicit operator bool() const noexcept { return ok(); }
};

struct GraphicsSceneAssetRecoveryResult {
  RenderAssetDelta full_snapshot;
  ValidationResult validation;

  [[nodiscard]] bool ok() const noexcept {
    return full_snapshot.sequence != 0U && validation.ok();
  }
  explicit operator bool() const noexcept { return ok(); }
};

/// Owns stable renderer-neutral asset identity, revision lineage, static object
/// lifecycle, prior transforms/camera, and immutable scene snapshots.
/// The asset catalog is copy-on-write and object history is sorted contiguous
/// storage, so stable-catalog frames use a constant number of contiguous
/// allocations independent of asset/object count. Reused immutable source
/// owners and exact asset-revision pairs also bypass repeated payload scans.
/// Produce() is fail-closed and transactional: allocation, validation, asset
/// application, snapshot creation, and camera validation all complete before
/// any producer state advances. One graphics thread owns an instance; callers
/// serialize Produce(), recovery, and destruction.
class GraphicsSceneSnapshotProducer final {
public:
  explicit GraphicsSceneSnapshotProducer(
      GraphicsSceneSnapshotProducerConfiguration configuration);
  ~GraphicsSceneSnapshotProducer();

  GraphicsSceneSnapshotProducer(const GraphicsSceneSnapshotProducer &) =
      delete;
  GraphicsSceneSnapshotProducer &
  operator=(const GraphicsSceneSnapshotProducer &) = delete;
  GraphicsSceneSnapshotProducer(GraphicsSceneSnapshotProducer &&) = delete;
  GraphicsSceneSnapshotProducer &
  operator=(GraphicsSceneSnapshotProducer &&) = delete;

  [[nodiscard]] GraphicsSceneSnapshotProduceResult
  Produce(const GraphicsSceneFrameInput &frame);
  [[nodiscard]] GraphicsSceneSnapshotProduceResult
  ProduceJoinedFrame(IJoinedGraphicsSceneSource &source);

  /// Complete live catalog plus permanent tombstones for a fresh or
  /// device-recovered frontend. Invalid before the first successful Produce().
  [[nodiscard]] GraphicsSceneAssetRecoveryResult
  BuildRecoveryAssetSnapshot() const;

  [[nodiscard]] std::uint64_t registry_id() const noexcept;
  [[nodiscard]] std::uint64_t asset_sequence() const noexcept;

  /// Acquire-loads the last fully validated immutable production. A successful
  /// Produce() release-publishes the exact owner returned in its result only
  /// after every producer state transition commits. Rejected frames leave the
  /// publication unchanged. Any number of render/readback threads may load;
  /// one externally serialized graphics thread remains the sole producer.
  /// Calls into this producer, including LoadPublishedSnapshot(), must quiesce
  /// before destruction begins. Snapshot owners already acquired by readers
  /// remain valid independently after producer destruction.
  /// This observer seam carries only the scene owner; frontend submission must
  /// still consume the returned production so its asset delta and camera stay
  /// ordered with that scene.
  [[nodiscard]] std::shared_ptr<const SceneSnapshot>
  LoadPublishedSnapshot() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace RoR::Render
