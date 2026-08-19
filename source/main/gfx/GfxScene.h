/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2020 Petr Ohlidal

    For more information, see http://www.rigsofrods.org/

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3, as
    published by the Free Software Foundation.

    Rigs of Rods is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Rigs of Rods. If not, see <http://www.gnu.org/licenses/>.
*/

/// @file
/// @author Petr Ohlidal
/// @date   05/2018

#pragma once

#include "CameraManager.h"
#include "ForwardDeclarations.h"
#include "EnvironmentMap.h" // RoR::GfxEnvmap
#include "GfxActorCaptureInventory.h"
#include "GfxData.h"
#include "ogre14/Ogre14LegacyLiveMaterialCoordinator.h"
#include "ogre14/detail/Ogre14ToOgreNextTerrainSource.h"
#include "ogre14/detail/OgreNextDemoMaterialSource.h"
#include "render/Ogre14GraphicsSceneSource.h"
#include "render/Ogre14ProceduralRoadSource.h"
#include "SimBuffers.h"
#include "Skidmark.h"

#include <map>
#include <set>
#include <string>
#include <memory>

namespace RoR {

/// @addtogroup Gfx
/// @{

/// Provides a 3D graphical representation of the simulation
/// Idea: simulation runs at it's own constant rate, scene updates and rendering run asynchronously.
class GfxScene: public Render::IOgre14GraphicsSceneCaptureProvider
{
public:

    void           Init();

    /// @name Particles
    /// @{
    void           CreateDustPools();
    DustPool*      GetDustPool(const char* name);
    void           AdjustParticleSystemTimeFactor(Ogre::ParticleSystem* psys);
    void           SetParticlesVisible(bool visible);
    /// @}

    /// @name Freebeam Gfx
    /// @{
    void           AddFreeBeamGfx(FreeBeamGfxRequest* rq);
    void           ModifyFreeBeamGfx(FreeBeamGfxRequest* rq);
    void           RemoveFreeBeamGfx(FreeBeamGfxID_t id);
    FreeBeamGfxID_t GetFreeBeamGfxNextId() { return m_gfx_freebeam_next_id++; }
    void           UpdateFreeBeamGfx(float dt);
    void           OnFreeForceRemoved(FreeForceID_t id);
    void           OnFreeForceBroken(FreeForceID_t id);
    /// @}

    void           DrawNetLabel(Ogre::Vector3 pos, float cam_dist, std::string const& nick, int colornum);
    void           UpdateScene(float dt);
    void           ClearScene();
    /// Releases every map-scoped capture owner after the renderer product has
    /// accepted the authoritative empty scene (or closed terminally), before
    /// full-scene actor/terrain/resource teardown. ClearScene() repeats this
    /// idempotently as the final native wipe guard. Same-map bundle reloads
    /// instead remove every reachable instance first; retained cache owners
    /// are inert until an exact fresh authenticated observation succeeds.
    void           ResetOgre14GraphicsSceneGeneration() noexcept;
    bool           RegisterGfxActor(RoR::GfxActor* gfx_actor);
    bool           HideGfxActor(RoR::GfxActor* gfx_actor);
    bool           UnhideGfxActor(RoR::GfxActor* gfx_actor);
    void           DestroyGfxActor(RoR::GfxActor* gfx_actor) noexcept;
    void           ForceUpdateSingleGfxActor(RoR::GfxActor* gfx_actor);
    /// Synchronizes all non-UI visuals needed by canonical capture from one
    /// joined simulation boundary. The supplied camera replaces mutable
    /// display-camera state for camera-facing effects during the update.
    bool           ForceUpdateSingleGfxActorForCapture(
                       RoR::GfxActor* gfx_actor,
                       float dt,
                       const Ogre::Vector3& camera_position,
                       const Ogre::Quaternion& camera_orientation);
    void           RegisterGfxCharacter(RoR::GfxCharacter* gfx_character);
    void           RemoveGfxCharacter(RoR::GfxCharacter* gfx_character);
    void           BufferSimulationData(); //!< Run this when simulation is halted
    /// Enables the disposable actual-game OgreNext demo source. The OGRE 14
    /// scene remains a private migration input; it is not a public renderer
    /// compatibility mode or a generalized material API.
    void           EnableOgreNextDemoCapture() noexcept
                   { m_ogre_next_demo_capture_enabled = true; }
    /// The hidden OGRE 14 scene is only an ingestion source in this mode.
    /// It must not automatically synthesize additional render-only mesh LODs;
    /// that path is unsafe for some legacy CityWorld meshes in pinned OGRE 14.
    [[nodiscard]] bool IsOgreNextDemoCaptureEnabled() const noexcept
                   { return m_ogre_next_demo_capture_enabled; }
    /// Reads only the completed simulation buffer and graphics-owned OGRE 14
    /// state. Incomplete renderer-neutral inventories are identified through
    /// available_fields rather than populated with guessed defaults.
    Render::ValidationResult CaptureOgre14GraphicsScene(
        Render::Ogre14GraphicsSceneCapture& capture) override;
    void CommitOgre14GraphicsSceneCapture() noexcept override;
    void DiscardOgre14GraphicsSceneCapture() noexcept override;
    /// Publishes the latest overlay-only GUI readback (see
    /// Ogre14GuiOverlayCapture). The retained input rides every subsequent
    /// joined capture as the optional HUD overlay; an unchanged content hash
    /// reuses the immutable byte owner so no asset revision advances.
    void SetOgre14HudOverlayReadback(
        Render::GraphicsSceneHudOverlayInput readback) noexcept
        { m_ogre14_hud_overlay_latest = std::move(readback); }
    GameContextSB&     GetSimDataBuffer() { return m_simbuf; }
    GfxEnvmap&     GetEnvMap() { return m_envmap; }
    RoR::SkidmarkConfig* GetSkidmarkConf () { return &m_skidmark_conf; }
    Ogre::SceneManager* GetSceneManager() { return m_scene_manager; }
    const std::vector<GfxActor*>& GetGfxActors() const
                   { return m_gfx_actor_inventory.Active(); }
    std::vector<GfxCharacter*>& GetGfxCharacters() { return m_all_gfx_characters; }

    static Ogre::Quaternion SpecialGetRotationTo(const Ogre::Vector3& src, const Ogre::Vector3& dest);

private:

    /// Creates the per-generation road material coordinator on first use,
    /// with the authored road2 compatibility declaration and ContentManager
    /// as the authenticated texture resolver and authority provider.
    Render::ValidationResult EnsureOgre14RoadMaterialCoordinator();

    Render::ValidationResult CaptureOgre14DynamicActorInventory(
        Render::Ogre14GraphicsSceneDynamicIdentityRegistry& identity_registry,
        std::map<std::string,
                 Render::Ogre14GraphicsSceneDynamicMeshCacheEntry,
                 std::less<>>& mesh_cache,
        std::vector<Render::GraphicsSceneAssetInput>& assets,
        std::vector<Render::GraphicsSceneDynamicMeshInput>& dynamic_meshes);

    std::map<std::string, DustPool *> m_dustpools;
    Ogre::SceneManager*               m_scene_manager = nullptr;
    std::vector<GfxActor*>            m_live_gfx_actors;
    // Hidden actors remain durably identified while leaving the legacy active
    // update list. Destroyed records retain a null-owner tombstone.
    GfxActorCaptureInventory           m_gfx_actor_inventory;
    std::vector<GfxCharacter*>        m_all_gfx_characters;
    RoR::GfxEnvmap                    m_envmap;
    GameContextSB                     m_simbuf;
    SkidmarkConfig                    m_skidmark_conf;

    // Exact joined-boundary identity for the OGRE 14 scene adapter. These are
    // copied only inside BufferSimulationData(), before the next physics batch
    // may start; the adapter never reads ActorManager directly.
    std::uint64_t                      m_ogre14_joined_buffer_epoch = 0U;
    std::uint64_t                      m_ogre14_post_update_scene_epoch = 0U;
    std::uint64_t                      m_ogre14_simulation_tick = 0U;
    double                             m_ogre14_simulation_time_seconds = 0.0;
    struct Ogre14ParticleUpdateTiming
    {
        std::uint64_t native_update_count = 0U;
        float latest_effective_interval_seconds = 0.0F;
        bool valid = true;
    };
    // Exact per-system native update count plus the one-update Ogre::Real
    // interval. Multiple updates between captures mint new particle
    // IDs; no repeated float subtraction is summarized as ordinary doubles.
    std::map<std::string, Ogre14ParticleUpdateTiming, std::less<>>
                                       m_ogre14_particle_update_timings;
    bool                               m_ogre14_joined_buffer_ready = false;
    bool                               m_ogre14_joined_buffer_atomic = false;
    bool                               m_ogre_next_demo_capture_enabled = false;
    Gfx::Detail::Ogre14ToOgreNextTerrainSource
                                       m_ogre_next_demo_terrain_source;
    Gfx::Detail::OgreNextDemoMaterialSource
                                       m_ogre_next_demo_material_source;
    // Stable active-coverage digest. Identical cached matte/projected frames
    // do not flood the log; the first accepted inventory and any promotion or
    // denominator/reason change are still emitted exactly once.
    std::string                        m_ogre_next_demo_material_coverage_log_snapshot;
    // Last committed policy-v1 sky descriptor telemetry. The candidate text
    // is staged with the joined capture and swapped only from Commit(), so a
    // rejected capture cannot advertise unpresented sky authority.
    std::string                        m_ogre_next_demo_analytic_sky_log_snapshot;
    // Map-generation identities reset at the explicit full-scene generation
    // release (and idempotently again in ClearScene), after the product session
    // has sequenced the preceding authoritative empty scene or terminal close.
    Render::Ogre14GraphicsSceneLightIdentityRegistry
                                       m_ogre14_light_identity_registry;
    Render::Ogre14GraphicsSceneStaticIdentityRegistry
                                       m_ogre14_static_identity_registry;
    // CPU extraction is performed only on a new/reloaded immutable OGRE mesh
    // draw-range key. Stable frames reuse these immutable payload owners.
    std::map<std::string,
             Render::Ogre14GraphicsSceneStaticMeshCacheEntry, std::less<>>
                                       m_ogre14_static_mesh_cache;
    // Admission grows monotonically within one map generation. Once a static
    // object enters the demo camera envelope it never disappears, so renderer
    // object/asset tombstones can never be resurrected while driving.
    std::set<std::uint64_t>             m_ogre_next_demo_admitted_static_objects;

    // Retained static scene. Static objects do not move, yet every capture
    // walked them all through OGRE and deep-copied the registries. Once every
    // object is admitted and no new material decision has committed, the
    // previous static capture is byte-equivalent to what a fresh walk would
    // produce, so it is reused and the walk skipped. Any condition failing
    // falls back to the full walk, which refreshes the retention.
    bool m_ogre14_static_retention_valid = false;
    std::uint32_t m_ogre14_static_retention_miss_stage = 0U;
    std::uint64_t m_ogre14_static_retention_hits = 0U;
    std::uint64_t m_ogre14_static_retention_misses = 0U;
    std::size_t m_ogre14_static_retention_inventory = 0U;
    std::size_t m_ogre14_static_retention_cache_size = 0U;
    std::uint64_t m_ogre14_static_retention_frozen_decisions = 0U;
    std::uint64_t m_ogre14_static_retention_projections = 0U;
    std::vector<Render::GraphicsSceneAssetInput>
        m_ogre14_static_retention_assets;
    std::vector<Render::GraphicsSceneStaticMeshInput>
        m_ogre14_static_retention_meshes;
    /// Bounds of every not-yet-admitted static object, refreshed by each
    /// committed full walk. Static objects do not move, so scanning these
    /// against the camera decides in microseconds whether a walk could admit
    /// anything new; when it cannot, the retained scene is byte-equivalent.
    std::vector<std::pair<std::uint64_t, Render::Bounds3>>
        m_ogre14_static_retention_unadmitted;
    std::size_t m_ogre14_static_retention_road_live = 0U;
    std::size_t m_ogre14_static_retention_road_cached = 0U;
    Render::Ogre14ProceduralRoadInventory m_ogre14_procedural_road_inventory;
    // Per-generation legacy material coordinator. Textured road2 admission
    // observes the live material through the authenticated native extractor
    // and resolves the exact translated closure the road inventory requires;
    // a fresh translator/catalog identity is minted per scene generation.
    std::unique_ptr<Render::Ogre14LegacyLiveMaterialCoordinator>
        m_ogre14_road_material_coordinator;
    // Observation cache keyed by material state count: the authenticated
    // capture reads back and hashes every texture byte, which is walk-frame
    // cost the unchanged material does not owe twice. A stale cache fails
    // PrepareFrame cleanly and is rebuilt once from the live material.
    struct Ogre14RoadMaterialObservationCacheEntry
    {
        std::uint64_t material_state_count = 0U;
        Render::Ogre14LegacyMaterialObservation observation;
    };
    std::map<std::string, Ogre14RoadMaterialObservationCacheEntry,
             std::less<>> m_ogre14_road_observation_cache;
    // Committed-capture section cost accounting, logged every 300 commits.
    std::uint64_t m_ogre14_section_log_captures = 0U;
    std::uint64_t m_ogre14_section_log_terrain_ns = 0U;
    std::uint64_t m_ogre14_section_log_static_ns = 0U;
    std::uint64_t m_ogre14_section_log_dynamic_ns = 0U;
    std::uint64_t m_ogre14_section_log_other_ns = 0U;
    // Full-resolution terrain payload owners are keyed by exact TerrainGroup
    // page identity; each entry retains its collision-free byte state. The
    // cache commits at its own map-generation boundary, before unrelated
    // joined scene domains, and survives their rejected captures.
    std::map<std::string,
             Render::Ogre14GraphicsSceneTerrainPageCacheEntry, std::less<>>
                                       m_ogre14_terrain_page_cache;
    Render::Ogre14GraphicsSceneDynamicIdentityRegistry
                                       m_ogre14_dynamic_identity_registry;
    std::map<std::string,
             Render::Ogre14GraphicsSceneDynamicMeshCacheEntry, std::less<>>
                                       m_ogre14_dynamic_mesh_cache;
    Render::Ogre14AutomaticReflectionProbeState
                                       m_ogre14_automatic_reflection_probe_state;
    // Latest overlay-only GUI readback for the transported menu/HUD. Reset at
    // the scene-generation boundary so a new map never republishes stale GUI
    // pixels before the first fresh capture.
    std::optional<Render::GraphicsSceneHudOverlayInput>
                                       m_ogre14_hud_overlay_latest;
    struct Ogre14DustParticleIdentity
    {
        std::uint64_t particle_id = 0U;
        float age_seconds = 0.0F;
        float lifetime_seconds = 0.0F;
        float remaining_seconds = 0.0F;
    };
    struct Ogre14DustSystemIdentity
    {
        std::uint64_t system_id = 0U;
        std::uint64_t next_particle_id = 1U;
        std::uint64_t last_native_update_count = 0U;
        std::map<std::uintptr_t, Ogre14DustParticleIdentity> active_particles;
    };
    struct Ogre14ContinuousParticleCaptureState
    {
        std::uint64_t next_source_sequence = 1U;
        std::uint64_t next_system_id = 1U;
        std::uint64_t next_event_id = 1U;
        std::map<std::string, Ogre14DustSystemIdentity, std::less<>> systems;
        std::map<std::uint64_t,
                 Render::Ogre14ParticleSourceSystemCapture> live_systems;
        std::uint64_t captured_systems = 0U;
        std::uint64_t captured_particles = 0U;
        std::uint64_t observed_systems = 0U;
        std::uint64_t observed_particles = 0U;
        std::uint64_t deferred_inactive_systems = 0U;
        std::uint64_t excluded_systems = 0U;
        std::uint64_t excluded_particles = 0U;
        std::uint64_t excluded_non_dust_systems = 0U;
        std::uint64_t excluded_sparks_systems = 0U;
        std::uint64_t excluded_ripple_systems = 0U;
        std::uint64_t excluded_other_non_dust_systems = 0U;
        std::uint64_t excluded_billboard_modes = 0U;
        std::uint64_t excluded_local_space_systems = 0U;
        std::uint64_t excluded_animated_systems = 0U;
        std::uint64_t excluded_sorted_systems = 0U;
        std::uint64_t excluded_timing_modes = 0U;
        std::uint64_t source_backed_textures = 0U;
        std::uint64_t source_alpha_textures = 0U;
        std::uint64_t gpu_readbacks = 0U;
        std::uint64_t lifetime_max_captured_systems = 0U;
        std::uint64_t lifetime_max_captured_particles = 0U;
    };
    Ogre14ContinuousParticleCaptureState m_ogre14_particle_capture_state;
    std::string m_ogre14_particle_coverage_log_snapshot;
    struct Ogre14PendingCaptureState
    {
        Render::Ogre14GraphicsSceneLightIdentityRegistry light_registry;
        Render::Ogre14GraphicsSceneStaticIdentityRegistry static_registry;
        std::map<std::string,
                 Render::Ogre14GraphicsSceneStaticMeshCacheEntry, std::less<>>
            static_mesh_cache;
        std::set<std::uint64_t> admitted_static_objects;
        Render::Ogre14ProceduralRoadInventory procedural_road_inventory;
        Render::Ogre14GraphicsSceneDynamicIdentityRegistry dynamic_registry;
        std::map<std::string,
                 Render::Ogre14GraphicsSceneDynamicMeshCacheEntry,
                 std::less<>> dynamic_mesh_cache;
        /// True when this capture reused the retained static scene and left
        /// the static registry, mesh cache, and admitted set untouched, so
        /// commit must not swap those members against empty pending copies.
        bool static_state_retained = false;
        /// Retention refresh produced by a full static walk. Applied only on
        /// commit, so a frame a later section discards can never leave the
        /// retained scene describing state that was never published.
        bool has_retention_refresh = false;
        std::vector<Render::GraphicsSceneAssetInput> retention_assets;
        std::vector<Render::GraphicsSceneStaticMeshInput> retention_meshes;
        std::vector<std::pair<std::uint64_t, Render::Bounds3>>
            retention_unadmitted;
        std::size_t retention_inventory = 0U;
        std::size_t retention_cache_size = 0U;
        std::size_t retention_road_live = 0U;
        std::size_t retention_road_cached = 0U;
        std::uint64_t retention_frozen_decisions = 0U;
        std::uint64_t retention_projections = 0U;
        Ogre14ContinuousParticleCaptureState particle_capture_state;
        Render::Ogre14AutomaticReflectionProbeState
            automatic_reflection_probe_state;
        std::size_t new_material_projection_count = 0U;
        std::size_t active_material_projection_count = 0U;
        Gfx::Detail::OgreNextDemoMaterialSourceCounters
            material_source_counters;
        Gfx::Detail::OgreNextDemoCuratedCityWorldCoverage
            curated_cityworld_material_coverage;
        std::string analytic_sky_log_snapshot;
        /// Prepared road-material frame backing this capture's authoritative
        /// road inventory. The coordinator commits it only after the joined
        /// transaction is accepted; every earlier discard funnels through
        /// DiscardOgre14GraphicsSceneCapture.
        Render::Ogre14LegacyPreparedMaterialFrame road_material_frame;
        bool has_road_material_frame = false;
        /// This capture's per-section traversal cost, logged from commit so
        /// only accepted exposures are counted.
        std::uint64_t section_terrain_ns = 0U;
        std::uint64_t section_static_ns = 0U;
        std::uint64_t section_dynamic_ns = 0U;
        std::uint64_t section_other_ns = 0U;
    };
    std::unique_ptr<Ogre14PendingCaptureState> m_ogre14_pending_capture;

    // Free beams GFX:
    std::vector<FreeBeamGfx>          m_gfx_freebeams;
    FreeBeamGfxID_t                   m_gfx_freebeam_next_id = 0;
    Ogre::SceneNode*                  m_gfx_freebeams_grouping_node = nullptr; //!< Only for nicer scenegraph when viewing through Inspector gadget.
};

/// @} // addtogroup Gfx

} // namespace RoR
