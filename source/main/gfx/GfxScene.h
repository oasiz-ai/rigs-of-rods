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
#include "GfxData.h"
#include "render/Ogre14GraphicsSceneSource.h"
#include "SimBuffers.h"
#include "Skidmark.h"

#include <map>
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
    void           RegisterGfxActor(RoR::GfxActor* gfx_actor);
    void           HideGfxActor(RoR::GfxActor* gfx_actor);
    void           UnhideGfxActor(RoR::GfxActor* gfx_actor);
    void           DestroyGfxActor(RoR::GfxActor* gfx_actor);
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
    void           EnableOgre14GraphicsSceneCapture() noexcept
                   { m_ogre14_scene_capture_enabled = true; }
    /// Reads only the completed simulation buffer and graphics-owned OGRE 14
    /// state. Incomplete renderer-neutral inventories are identified through
    /// available_fields rather than populated with guessed defaults.
    Render::ValidationResult CaptureOgre14GraphicsScene(
        Render::Ogre14GraphicsSceneCapture& capture) override;
    void CommitOgre14GraphicsSceneCapture() noexcept override;
    void DiscardOgre14GraphicsSceneCapture() noexcept override;
    GameContextSB&     GetSimDataBuffer() { return m_simbuf; }
    GfxEnvmap&     GetEnvMap() { return m_envmap; }
    RoR::SkidmarkConfig* GetSkidmarkConf () { return &m_skidmark_conf; }
    Ogre::SceneManager* GetSceneManager() { return m_scene_manager; }
    std::vector<GfxActor*>& GetGfxActors() { return m_all_gfx_actors; }
    std::vector<GfxCharacter*>& GetGfxCharacters() { return m_all_gfx_characters; }

    static Ogre::Quaternion SpecialGetRotationTo(const Ogre::Vector3& src, const Ogre::Vector3& dest);

private:

    Render::ValidationResult CaptureOgre14DynamicActorInventory(
        Render::Ogre14GraphicsSceneDynamicIdentityRegistry& identity_registry,
        std::map<std::string,
                 Render::Ogre14GraphicsSceneDynamicMeshCacheEntry,
                 std::less<>>& mesh_cache,
        std::vector<Render::GraphicsSceneAssetInput>& assets,
        std::vector<Render::GraphicsSceneDynamicMeshInput>& dynamic_meshes);

    std::map<std::string, DustPool *> m_dustpools;
    Ogre::SceneManager*               m_scene_manager = nullptr;
    std::vector<GfxActor*>            m_all_gfx_actors;
    std::vector<GfxActor*>            m_live_gfx_actors;
    struct GfxActorInventoryRecord
    {
        GfxActor* actor = nullptr;
        bool hidden = false;
    };
    // Ownership/lifecycle inventory is distinct from m_all_gfx_actors, which
    // remains the active legacy update list. Hidden network actors stay here
    // so capture preserves their identities; only destruction removes them.
    std::map<std::int64_t, GfxActorInventoryRecord>
                                       m_gfx_actor_inventory;
    std::set<std::int64_t>             m_destroyed_gfx_actor_ids;
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
    bool                               m_ogre14_joined_buffer_ready = false;
    bool                               m_ogre14_joined_buffer_atomic = false;
    bool                               m_ogre14_scene_capture_enabled = false;
    // Intentionally survives ClearScene(): exact OGRE light names retain one
    // collision-audited identity for this adapter lifetime.
    Render::Ogre14GraphicsSceneLightIdentityRegistry
                                       m_ogre14_light_identity_registry;
    Render::Ogre14GraphicsSceneStaticIdentityRegistry
                                       m_ogre14_static_identity_registry;
    // CPU extraction is performed only on a new/reloaded immutable OGRE mesh
    // draw-range key. Stable frames reuse these immutable payload owners.
    std::map<std::string,
             Render::Ogre14GraphicsSceneStaticMeshCacheEntry, std::less<>>
                                       m_ogre14_static_mesh_cache;
    Render::Ogre14GraphicsSceneDynamicIdentityRegistry
                                       m_ogre14_dynamic_identity_registry;
    std::map<std::string,
             Render::Ogre14GraphicsSceneDynamicMeshCacheEntry, std::less<>>
                                       m_ogre14_dynamic_mesh_cache;
    struct Ogre14PendingCaptureState
    {
        Render::Ogre14GraphicsSceneLightIdentityRegistry light_registry;
        Render::Ogre14GraphicsSceneStaticIdentityRegistry static_registry;
        std::map<std::string,
                 Render::Ogre14GraphicsSceneStaticMeshCacheEntry, std::less<>>
            static_mesh_cache;
        Render::Ogre14GraphicsSceneDynamicIdentityRegistry dynamic_registry;
        std::map<std::string,
                 Render::Ogre14GraphicsSceneDynamicMeshCacheEntry,
                 std::less<>> dynamic_mesh_cache;
    };
    std::unique_ptr<Ogre14PendingCaptureState> m_ogre14_pending_capture;

    // Free beams GFX:
    std::vector<FreeBeamGfx>          m_gfx_freebeams;
    FreeBeamGfxID_t                   m_gfx_freebeam_next_id = 0;
    Ogre::SceneNode*                  m_gfx_freebeams_grouping_node = nullptr; //!< Only for nicer scenegraph when viewing through Inspector gadget.
};

/// @} // addtogroup Gfx

} // namespace RoR
