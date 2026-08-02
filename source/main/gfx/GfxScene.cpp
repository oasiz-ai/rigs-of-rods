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

#include "GfxScene.h"

#include "AppContext.h"
#include "Actor.h"
#include "ActorManager.h"
#include "ApproxMath.h"
#include "Console.h"
#include "DustPool.h"
#include "HydraxWater.h"
#include "GameContext.h"
#include "GUIManager.h"
#include "GUIUtils.h"
#include "GUI_DirectionArrow.h"
#include "OverlayWrapper.h"
#include "SkyManager.h"
#include "SkyXManager.h"
#include "TerrainGeometryManager.h"
#include "Terrain.h"
#include "TerrainObjectManager.h"
#include "Utils.h"

#include "imgui_internal.h"

#include <Ogre.h>

#include <cmath>
#include <limits>

using namespace Ogre;
using namespace RoR;

namespace
{

RoR::Render::Matrix4x4 ToRendererBoundaryMatrix(
    const Ogre::Matrix4& matrix)
{
    RoR::Render::Matrix4x4 converted;
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        for (std::size_t column = 0U; column < 4U; ++column)
        {
            converted.elements[column * 4U + row] =
                static_cast<float>(matrix[row][column]);
        }
    }
    return converted;
}

bool CaptureOgre14MainCamera(
    RoR::Render::GraphicsSceneCameraInput& output)
{
    if (RoR::App::GetCameraManager() == nullptr ||
        RoR::App::GetAppContext() == nullptr)
    {
        return false;
    }
    Ogre::Camera* const camera =
        RoR::App::GetCameraManager()->GetCamera();
    Ogre::Viewport* const viewport =
        RoR::App::GetAppContext()->GetViewport();
    if (camera == nullptr || viewport == nullptr ||
        camera->getViewport() != viewport ||
        camera->isCustomProjectionMatrixEnabled())
    {
        return false;
    }

    const Ogre::RealRect extents = camera->getFrustumExtents();
    RoR::Render::Ogre14CameraCaptureInput input;
    input.view_id = 1U;
    input.width = static_cast<std::uint32_t>(viewport->getActualWidth());
    input.height = static_cast<std::uint32_t>(viewport->getActualHeight());
    input.view_from_render =
        ToRendererBoundaryMatrix(camera->getViewMatrix(true));
    if (camera->getProjectionType() == Ogre::PT_PERSPECTIVE)
    {
        input.projection =
            RoR::Render::Ogre14CameraProjectionKind::PERSPECTIVE;
    }
    else if (camera->getProjectionType() == Ogre::PT_ORTHOGRAPHIC)
    {
        input.projection =
            RoR::Render::Ogre14CameraProjectionKind::ORTHOGRAPHIC;
    }
    else
    {
        return false;
    }
    input.left = static_cast<float>(extents.left);
    input.right = static_cast<float>(extents.right);
    input.top = static_cast<float>(extents.top);
    input.bottom = static_cast<float>(extents.bottom);
    input.near_plane =
        static_cast<float>(camera->getNearClipDistance());
    input.far_plane =
        static_cast<float>(camera->getFarClipDistance());
    // OGRE 14 has no scene-linear view-exposure state. Identity is therefore
    // exact here; optional display postprocessing remains outside the scene.
    input.exposure = 1.0F;
    input.visibility_mask = viewport->getVisibilityMask();
    return RoR::Render::BuildOgre14GraphicsSceneCamera(input, output).ok();
}

} // namespace

void GfxScene::CreateDustPools()
{
    ROR_ASSERT(m_dustpools.size() == 0);
    m_dustpools["dust"]   = new DustPool(m_scene_manager, "tracks/Dust",   20);
    m_dustpools["clump"]  = new DustPool(m_scene_manager, "tracks/Clump",  20);
    m_dustpools["sparks"] = new DustPool(m_scene_manager, "tracks/Sparks", 10);
    m_dustpools["drip"]   = new DustPool(m_scene_manager, "tracks/Drip",   50);
    m_dustpools["splash"] = new DustPool(m_scene_manager, "tracks/Splash", 20);
    m_dustpools["ripple"] = new DustPool(m_scene_manager, "tracks/Ripple", 20);
}

void GfxScene::ClearScene()
{
    m_ogre14_joined_buffer_ready = false;
    m_ogre14_joined_buffer_atomic = false;

    // Delete dustpools
    for (auto itor : m_dustpools)
    {
        itor.second->Discard(m_scene_manager);
        delete itor.second;
    }
    m_dustpools.clear();

    // Delete game elements
    m_all_gfx_actors.clear();
    m_all_gfx_characters.clear();

    // Wipe scene manager
    m_scene_manager->clearScene();
    m_gfx_freebeams_grouping_node = nullptr;

    // Recover from the wipe
    App::GetCameraManager()->ReCreateCameraNode();
    App::GetGuiManager()->DirectionArrow.CreateArrow();
    m_gfx_freebeams_grouping_node = m_scene_manager->getRootSceneNode()->createChildSceneNode("FreeBeam Visuals");
}

void GfxScene::Init()
{
    ROR_ASSERT(!m_scene_manager);
    m_scene_manager = App::GetAppContext()->GetOgreRoot()->createSceneManager();
    App::GetAppContext()->RegisterRTShaderSceneManager(m_scene_manager);
    m_gfx_freebeams_grouping_node = m_scene_manager->getRootSceneNode()->createChildSceneNode("FreeBeam Visuals");

    m_skidmark_conf.LoadDefaultSkidmarkDefs();
}

void GfxScene::UpdateScene(float dt)
{
    // NOTE: The `dt` parameter here is simulation time (0 when paused), not real time!
    // ================================================================================

    // Actors - start threaded tasks
    for (GfxActor* gfx_actor: m_live_gfx_actors)
    {
        gfx_actor->UpdateFlexbodies(); // Push flexbody tasks to threadpool
        gfx_actor->UpdateWheelVisuals(); // Push flexwheel tasks to threadpool
    }

    // Var
    GfxActor* player_gfx_actor = nullptr;
    if (m_simbuf.simbuf_player_actor != nullptr)
    {
        player_gfx_actor = m_simbuf.simbuf_player_actor->GetGfxActor();
    }

    // FOV
    if (m_simbuf.simbuf_camera_behavior != CameraManager::CAMERA_BEHAVIOR_STATIC)
    {
        float fov = (m_simbuf.simbuf_camera_behavior == CameraManager::CAMERA_BEHAVIOR_VEHICLE_CINECAM)
            ? App::gfx_fov_internal->getFloat() : App::gfx_fov_external->getFloat();
        RoR::App::GetCameraManager()->GetCamera()->setFOVy(Ogre::Degree(fov));
    }

    // Particles
    if (App::gfx_particles_mode->getInt() == 1)
    {
        // Generate particles as needed
        for (GfxActor* gfx_actor: m_all_gfx_actors)
        {
            float dt_actor = (!gfx_actor->GetSimDataBuffer().simbuf_physics_paused) ? dt : 0.f;
            gfx_actor->UpdateParticles(dt_actor);
        }

        // Update particle movement
        for (auto itor : m_dustpools)
        {
            itor.second->update();
        }
    }

    // Realtime reflections on player vehicle
    // IMPORTANT: Toggles visibility of all meshes -> must be done before any other visibility control is evaluated (i.e. aero propellers)
    if (player_gfx_actor != nullptr)
    {
        // Safe to be called here, only modifies OGRE objects, doesn't read any physics state.
        m_envmap.UpdateEnvMap(player_gfx_actor->GetSimDataBuffer().simbuf_pos, player_gfx_actor);
    }

    // Terrain - animated meshes and paged geometry
    App::GetGameContext()->GetTerrain()->getObjectManager()->UpdateTerrainObjects(dt);

    // Terrain - lightmap; TODO: ported as-is from Terrain::update(), is it needed? ~ only_a_ptr, 05/2018
    App::GetGameContext()->GetTerrain()->getGeometryManager()->UpdateMainLightPosition(); // TODO: Is this necessary? I'm leaving it here just in case ~ only_a_ptr, 04/2017

    // Terrain - water
    auto water = App::GetGameContext()->GetTerrain()->getWater();
    auto gfx_water = App::GetGameContext()->GetTerrain()->getGfxWater();
    if (water)
    {
        if (player_gfx_actor != nullptr)
        {
            gfx_water->SetReflectionPlaneHeight(water->CalcWavesHeight(player_gfx_actor->GetSimDataBuffer().simbuf_pos));
        }
        else
        {
            gfx_water->SetReflectionPlaneHeight(water->GetStaticWaterHeight());
        }
        gfx_water->FrameStepWater(dt);
    }

    // Terrain - sky
#ifdef USE_CAELUM
    SkyManager* sky = App::GetGameContext()->GetTerrain()->getSkyManager();
    if (sky != nullptr)
    {
        sky->DetectSkyUpdate();
    }
#endif

    SkyXManager* skyx_man = App::GetGameContext()->GetTerrain()->getSkyXManager();
    if (skyx_man != nullptr)
    {
       skyx_man->update(dt); // Light update
    }

    // GUI - race
    if (m_simbuf.simbuf_race_in_progress != m_simbuf.simbuf_race_in_progress_prev)
    {
        if (m_simbuf.simbuf_race_in_progress) // Started
        {
            RoR::App::GetOverlayWrapper()->ShowRacingOverlay();
        }
        else // Ended
        {
            RoR::App::GetOverlayWrapper()->HideRacingOverlay();
        }
    }
    if (m_simbuf.simbuf_race_in_progress)
    {
        RoR::App::GetOverlayWrapper()->UpdateRacingGui(this);
    }

    // GUI - vehicle pressure
    if (m_simbuf.simbuf_player_actor)
    {
        App::GetOverlayWrapper()->UpdatePressureOverlay(m_simbuf.simbuf_player_actor->GetGfxActor());
    }

    // HUD - network labels (always update)
    for (GfxActor* gfx_actor: m_all_gfx_actors)
    {
        gfx_actor->UpdateNetLabels(dt);
    }

    // Player avatars
    for (GfxCharacter* a: m_all_gfx_characters)
    {
        a->UpdateCharacterInScene();
    }

    // Actors - update misc visuals
    for (GfxActor* gfx_actor: m_all_gfx_actors)
    {
        float dt_actor = (!gfx_actor->GetSimDataBuffer().simbuf_physics_paused) ? dt : 0.f;
        if (gfx_actor->IsActorLive())
        {
            gfx_actor->UpdateRods();
            gfx_actor->UpdateCabMesh();
            gfx_actor->UpdateWingMeshes();
            gfx_actor->UpdateAirbrakes();
            gfx_actor->UpdateCParticles();
            gfx_actor->UpdateExhausts();
            gfx_actor->UpdateAeroEngines();
            gfx_actor->UpdatePropAnimations(dt_actor);
        }
        // Beacon flares must always be updated
        gfx_actor->UpdateProps(dt_actor, (gfx_actor == player_gfx_actor));
        // Blinkers (turn signals) must always be updated
        gfx_actor->UpdateFlares(dt_actor, (gfx_actor == player_gfx_actor));
    }
    if (player_gfx_actor != nullptr)
    {
        float dt_actor = (!player_gfx_actor->GetSimDataBuffer().simbuf_physics_paused) ? dt : 0.f;
        player_gfx_actor->UpdateVideoCameras(dt_actor);

        // The old-style render-to-texture dashboard (based on OGRE overlays)
        if (m_simbuf.simbuf_player_actor->ar_driveable == TRUCK && m_simbuf.simbuf_player_actor->ar_engine != nullptr)
        {
            RoR::App::GetOverlayWrapper()->UpdateLandVehicleHUD(player_gfx_actor);
        }
        else if (m_simbuf.simbuf_player_actor->ar_driveable == AIRPLANE)
        {
            RoR::App::GetOverlayWrapper()->UpdateAerialHUD(player_gfx_actor);
        }
    }

    App::GetGuiManager()->DrawSimGuiBuffered(player_gfx_actor);

    App::GetGameContext()->GetSceneMouse().UpdateVisuals();

    this->UpdateFreeBeamGfx(dt);

    // Actors - finalize threaded tasks
    for (GfxActor* gfx_actor: m_live_gfx_actors)
    {
        gfx_actor->FinishWheelUpdates();
        gfx_actor->FinishFlexbodyTasks();
    }
}

void GfxScene::SetParticlesVisible(bool visible)
{
    for (auto itor : m_dustpools)
    {
        itor.second->setVisible(visible);
    }
}

DustPool* GfxScene::GetDustPool(const char* name)
{
    auto found = m_dustpools.find(name);
    if (found != m_dustpools.end())
    {
        return found->second;
    }
    else
    {
        return nullptr;
    }
}

void GfxScene::RegisterGfxActor(RoR::GfxActor* gfx_actor)
{
    m_all_gfx_actors.push_back(gfx_actor);
}

void GfxScene::BufferSimulationData()
{
    ActorManager* actor_manager = nullptr;
    std::uint64_t simulation_tick_before = 0U;
    float simulation_time_before = 0.0F;
    if (m_ogre14_scene_capture_enabled)
    {
        m_ogre14_joined_buffer_ready = false;
        m_ogre14_joined_buffer_atomic = false;
        actor_manager = App::GetGameContext()->GetActorManager();
        if (actor_manager == nullptr)
        {
            return;
        }
        simulation_tick_before =
            actor_manager->GetCompletedPhysicsSteps();
        simulation_time_before = actor_manager->GetTotalTime();
    }

    m_simbuf.simbuf_player_actor = App::GetGameContext()->GetPlayerActor();
    m_simbuf.simbuf_character_pos = App::GetGameContext()->GetPlayerCharacter()->getPosition();
    m_simbuf.simbuf_sim_paused = App::GetGameContext()->GetActorManager()->IsSimulationPaused();
    m_simbuf.simbuf_sim_speed = App::GetGameContext()->GetActorManager()->GetSimulationSpeed();
    m_simbuf.simbuf_camera_behavior = App::GetCameraManager()->GetCurrentBehavior();

    // Race system
    m_simbuf.simbuf_race_time = App::GetGameContext()->GetRaceSystem().GetRaceTime();
    m_simbuf.simbuf_race_best_time = App::GetGameContext()->GetRaceSystem().GetRaceBestTime();
    m_simbuf.simbuf_race_time_diff = App::GetGameContext()->GetRaceSystem().GetRaceTimeDiff();
    m_simbuf.simbuf_race_in_progress_prev = m_simbuf.simbuf_race_in_progress;
    m_simbuf.simbuf_race_in_progress = App::GetGameContext()->GetRaceSystem().IsRaceInProgress();
    m_simbuf.simbuf_dir_arrow_target = App::GetGameContext()->GetRaceSystem().GetDirArrowTarget();
    m_simbuf.simbuf_dir_arrow_text = App::GetGameContext()->GetRaceSystem().GetDirArrowText();
    m_simbuf.simbuf_dir_arrow_visible = App::GetGameContext()->GetRaceSystem().IsDirArrowVisible();

    m_live_gfx_actors.clear();
    for (GfxActor* a: m_all_gfx_actors)
    {
        if (a->IsActorLive() || !a->IsActorInitialized())
        {
            a->UpdateSimDataBuffer();
            m_live_gfx_actors.push_back(a);
            a->InitializeActor();
        }
    }

    for (GfxCharacter* a: m_all_gfx_characters)
    {
        a->BufferSimulationData();
    }

    if (!m_ogre14_scene_capture_enabled)
    {
        return;
    }
    const std::uint64_t simulation_tick_after =
        actor_manager->GetCompletedPhysicsSteps();
    const float simulation_time_after = actor_manager->GetTotalTime();
    if (m_ogre14_joined_buffer_epoch ==
        (std::numeric_limits<std::uint64_t>::max)())
    {
        return;
    }
    ++m_ogre14_joined_buffer_epoch;
    m_ogre14_joined_buffer_ready = true;
    m_ogre14_joined_buffer_atomic =
        simulation_tick_before == simulation_tick_after &&
        simulation_time_before == simulation_time_after &&
        std::isfinite(simulation_time_after) &&
        simulation_time_after >= 0.0F;
    if (m_ogre14_joined_buffer_atomic)
    {
        m_ogre14_simulation_tick = simulation_tick_after;
        m_ogre14_simulation_time_seconds =
            static_cast<double>(simulation_time_after);
    }
}

Render::ValidationResult GfxScene::CaptureOgre14GraphicsScene(
    Render::Ogre14GraphicsSceneCapture& capture)
{
    Render::Ogre14GraphicsSceneCapture candidate;
    candidate.joined_buffer_epoch = m_ogre14_joined_buffer_epoch;
    if (m_ogre14_joined_buffer_ready && m_ogre14_joined_buffer_atomic)
    {
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    JOINED_BUFFER_ATOMICITY);
        candidate.frame.simulation_tick = m_ogre14_simulation_tick;
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::SIMULATION_TICK);
        candidate.frame.simulation_time_seconds =
            m_ogre14_simulation_time_seconds;
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    SIMULATION_TIME_SECONDS);

        // OGRE 14 stores the live world directly in simulation coordinates;
        // it has no floating render-origin rebase in this process.
        candidate.frame.absolute_world_origin_meters = {};
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    ABSOLUTE_WORLD_ORIGIN_METERS);

        if (m_scene_manager != nullptr)
        {
            const Ogre::ColourValue ambient =
                m_scene_manager->getAmbientLight();
            const Render::Float3 native_ambient{
                static_cast<float>(ambient.r),
                static_cast<float>(ambient.g),
                static_cast<float>(ambient.b)};
            if (Render::BuildOgre14GraphicsSceneEnvironment(
                    native_ambient, candidate.frame.environment).ok())
            {
                candidate.available_fields |=
                    Render::Ogre14GraphicsSceneCaptureFieldBit(
                        Render::Ogre14GraphicsSceneCaptureField::
                            ENVIRONMENT);
            }
        }

        // OGRE 14 has no authored reflection-probe registry. Its dynamic
        // GfxEnvmap is a vehicle-local compatibility reflection and cannot be
        // promoted to a world-space parallax-corrected probe. The complete
        // authored probe inventory is therefore exactly empty.
        candidate.frame.reflection_probes.clear();
        candidate.available_fields |=
            Render::Ogre14GraphicsSceneCaptureFieldBit(
                Render::Ogre14GraphicsSceneCaptureField::
                    REFLECTION_PROBES);

        if (CaptureOgre14MainCamera(candidate.frame.camera))
        {
            candidate.available_fields |=
                Render::Ogre14GraphicsSceneCaptureFieldBit(
                    Render::Ogre14GraphicsSceneCaptureField::CAMERA);
        }
    }

    // Deliberately unavailable in this first production slice:
    // - assets/static_meshes: no complete stable CPU inventory yet separates
    //   terrain MeshObjects from deformable actor geometry;
    // - lights: stable photometric lux/candela values are not authored;
    // Their bits remain clear so no empty or unit-valued substitutes publish.
    capture = std::move(candidate);
    return Render::ValidationResult::Success();
}

void GfxScene::RemoveGfxActor(RoR::GfxActor* remove_me)
{
    auto itor = std::remove(m_all_gfx_actors.begin(), m_all_gfx_actors.end(), remove_me);
    if (itor != m_all_gfx_actors.end())
    {
        m_all_gfx_actors.erase(itor, m_all_gfx_actors.end());
    }
}

void GfxScene::ForceUpdateSingleGfxActor(RoR::GfxActor* gfx_actor)
{
    // Do the work `UpdateScene()` would, but for a single actor.
    // Needed for i.e. terrain editor mode.
    // ------------------------------------------------------

    // Start threaded stuff
    gfx_actor->UpdateFlexbodies(); // Push flexbody tasks to threadpool
    gfx_actor->UpdateWheelVisuals(); // Push flexwheel tasks to threadpool

    // Do sync stuff
    gfx_actor->UpdateRods();
    gfx_actor->UpdateCabMesh();
    gfx_actor->UpdateWingMeshes();
    gfx_actor->UpdateAirbrakes();

    // Finish threaded stuff
    gfx_actor->FinishWheelUpdates();
    gfx_actor->FinishFlexbodyTasks();
}

bool GfxScene::ForceUpdateSingleGfxActorForCapture(
    RoR::GfxActor* gfx_actor,
    float dt,
    const Ogre::Vector3& camera_position,
    const Ogre::Quaternion& camera_orientation)
{
    if (gfx_actor == nullptr ||
        !std::isfinite(dt) ||
        dt < 0.0f ||
        App::GetCameraManager() == nullptr ||
        App::GetCameraManager()->GetCameraNode() == nullptr ||
        App::gfx_particles_mode == nullptr ||
        App::gfx_flares_mode == nullptr ||
        App::gfx_enable_videocams == nullptr ||
        App::gfx_particles_mode->getInt() != 0 ||
        App::gfx_flares_mode->getEnum<GfxFlaresMode>() !=
            GfxFlaresMode::NONE ||
        App::gfx_enable_videocams->getBool())
    {
        return false;
    }

    Ogre::SceneNode* const display_camera_node =
        App::GetCameraManager()->GetCameraNode();
    const Ogre::Vector3 saved_position =
        display_camera_node->getPosition();
    const Ogre::Quaternion saved_orientation =
        display_camera_node->getOrientation();
    const int saved_cinecam =
        gfx_actor->GetSimDataBuffer().simbuf_cur_cinecam;

    struct RestoreCaptureOverrides
    {
        Ogre::SceneNode* node;
        Ogre::Vector3 position;
        Ogre::Quaternion orientation;
        ActorSB* sim_buffer;
        int cinecam;

        ~RestoreCaptureOverrides()
        {
            if (sim_buffer != nullptr)
                sim_buffer->simbuf_cur_cinecam = cinecam;
            if (node != nullptr)
            {
                node->setPosition(position);
                node->setOrientation(orientation);
            }
        }
    } restore = {
        display_camera_node,
        saved_position,
        saved_orientation,
        &gfx_actor->GetSimDataBuffer(),
        saved_cinecam};

    try
    {
        display_camera_node->setPosition(camera_position);
        display_camera_node->setOrientation(camera_orientation);
        // Schema 1 always renders cinecam 0 visibility, regardless of what
        // the interactive display currently shows.
        gfx_actor->GetSimDataBuffer().simbuf_cur_cinecam = 0;

        gfx_actor->UpdateFlexbodies();
        gfx_actor->UpdateWheelVisuals();

        // Particle, flare and render-to-texture video-camera state advances on
        // the display frame. The capture profile rejects those systems rather
        // than inheriting their most recently displayed state.
        gfx_actor->UpdateRods();
        gfx_actor->UpdateCabMesh();
        gfx_actor->UpdateWingMeshes();
        gfx_actor->UpdateAirbrakes();
        gfx_actor->UpdateAeroEngines();
        gfx_actor->UpdatePropAnimations(dt);
        gfx_actor->UpdateProps(0.0f, true);

        // A driver character may be visible inside the truck. Synchronize it
        // from the joined simulation buffer instead of rendering whichever
        // pose the interactive display happened to update last.
        for (GfxCharacter* character : m_all_gfx_characters)
        {
            if (character == nullptr)
                return false;
            character->BufferSimulationData();
            character->UpdateCharacterInScene();
        }

        gfx_actor->FinishWheelUpdates();
        gfx_actor->FinishFlexbodyTasks();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void GfxScene::RegisterGfxCharacter(RoR::GfxCharacter* gfx_character)
{
    m_all_gfx_characters.push_back(gfx_character);
}

void GfxScene::RemoveGfxCharacter(RoR::GfxCharacter* remove_me)
{
    auto itor = std::remove(m_all_gfx_characters.begin(), m_all_gfx_characters.end(), remove_me);
    if (itor != m_all_gfx_characters.end())
    {
        m_all_gfx_characters.erase(itor, m_all_gfx_characters.end());
    }
}

void GfxScene::DrawNetLabel(Ogre::Vector3 scene_pos, float cam_dist, std::string const& nick, int colornum)
{
#if USE_SOCKETW

        // this ensures that the nickname is always in a readable size
        float font_size = std::max(0.6, cam_dist / 40.0);
        std::string caption;
        if (cam_dist > 1000) // 1000 ... vlen
        {
            caption =
                nick + " (" + TOSTRING((float)(ceil(cam_dist / 100) / 10.0) ) + " km)";
        }
        else if (cam_dist > 20) // 20 ... vlen ... 1000
        {
            caption =
                nick + " (" + TOSTRING((int)cam_dist) + " m)";
        }
        else // 0 ... vlen ... 20
        {
            caption = nick;
        }

        // draw with DearIMGUI

    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    World2ScreenConverter world2screen(
        App::GetCameraManager()->GetCamera()->getViewMatrix(true), App::GetCameraManager()->GetCamera()->getProjectionMatrix(), Ogre::Vector2(screen_size.x, screen_size.y));

    Ogre::Vector3 pos_xyz = world2screen.Convert(scene_pos);

    // only draw when in front of camera
    if (pos_xyz.z < 0.f)
    {
        // Align position to whole pixels, to minimize jitter.
        ImVec2 pos((int)pos_xyz.x+0.5, (int)pos_xyz.y+0.5);

        ImVec2 text_size = ImGui::CalcTextSize(caption.c_str());
        GUIManager::GuiTheme const& theme = App::GetGuiManager()->GetTheme();

        ImDrawList* drawlist = GetImDummyFullscreenWindow();
        ImGuiContext* g = ImGui::GetCurrentContext();

        ImVec2 text_pos(pos.x - ((text_size.x / 2)), pos.y - ((text_size.y / 2)));

        // Draw background rectangle
        const float PADDING = 4.f;
        drawlist->AddRectFilled(
            text_pos - ImVec2(PADDING, PADDING),
            text_pos + text_size + ImVec2(PADDING, PADDING),
            ImColor(theme.semitransparent_window_bg),
            ImGui::GetStyle().WindowRounding);

        // draw colored text
        Ogre::ColourValue color = App::GetNetwork()->GetPlayerColor(colornum);
        ImVec4 text_color(color.r, color.g, color.b, 1.f);
        drawlist->AddText(g->Font, g->FontSize, text_pos, ImColor(text_color), caption.c_str());
    }

#endif // USE_SOCKETW
}

void GfxScene::AdjustParticleSystemTimeFactor(Ogre::ParticleSystem* psys)
{
    float speed_factor = 0.f;
    if (App::sim_state->getEnum<SimState>() == SimState::RUNNING && !App::GetGameContext()->GetActorManager()->IsSimulationPaused())
    {
        speed_factor = m_simbuf.simbuf_sim_speed;
    }

    psys->setSpeedFactor(speed_factor);
}

void GfxScene::AddFreeBeamGfx(FreeBeamGfxRequest* rq)
{
    auto itor = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [rq](const FreeBeamGfx& obj) { return obj.fbx_id == rq->fbr_id; });
    if (itor != m_gfx_freebeams.end())
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_WARNING,
            fmt::format("FreeBeamGfx with ID %d already exists, ignoring request.",rq->fbr_id));
        return;
    }

    FreeBeamGfx obj;
    obj.fbx_id = rq->fbr_id;
    obj.fbx_freeforce_primary = rq->fbr_freeforce_primary;
    obj.fbx_freeforce_secondary = rq->fbr_freeforce_secondary;
    obj.fbx_diameter = rq->fbr_diameter;

    Ogre::Entity* e = m_scene_manager->createEntity(fmt::format("FreeBeamGfx_{}", rq->fbr_id), rq->fbr_mesh_name);
    e->setMaterialName(rq->fbr_material_name);

    obj.fbx_scenenode = m_gfx_freebeams_grouping_node->createChildSceneNode(fmt::format("FreeBeamGfx_{}", rq->fbr_id));
    obj.fbx_scenenode->setScale(rq->fbr_diameter, -1, rq->fbr_diameter);
    obj.fbx_scenenode->attachObject(e);

    m_gfx_freebeams.push_back(obj);
}

void GfxScene::ModifyFreeBeamGfx(FreeBeamGfxRequest* rq)
{
    auto itor = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [rq](const FreeBeamGfx& obj) { return obj.fbx_id == rq->fbr_id; });
    if (itor == m_gfx_freebeams.end())
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_WARNING,
            fmt::format("FreeBeamGfx with ID %d not found, ignoring request.", rq->fbr_id));
        return;
    }

    FreeBeamGfx& obj = *itor;
    this->RemoveFreeBeamGfx(rq->fbr_id);
    this->AddFreeBeamGfx(rq);
}

void GfxScene::RemoveFreeBeamGfx(FreeBeamGfxID_t id)
{
    auto itor = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [id](const FreeBeamGfx& obj) { return obj.fbx_id == id; });
    if (itor == m_gfx_freebeams.end())
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_INFO, Console::CONSOLE_SYSTEM_WARNING,
            fmt::format("FreeBeamGfx with ID %d not found, ignoring request.", id));
        return;
    }

    FreeBeamGfx& obj = *itor;
    m_scene_manager->destroyEntity((Ogre::Entity*)obj.fbx_scenenode->getAttachedObject(0));
    m_gfx_freebeams_grouping_node->removeChild(obj.fbx_scenenode);
    m_gfx_freebeams.erase(itor);
}

void GfxScene::UpdateFreeBeamGfx(float dt)
{
    for (FreeBeamGfx& freebeam : m_gfx_freebeams)
    {
        // Sanity checks - primary freeforce
        ROR_ASSERT(freebeam.fbx_id != FREEBEAMGFXID_INVALID);
        ROR_ASSERT(freebeam.fbx_freeforce_primary != FREEFORCEID_INVALID);
        ActorManager::FreeForceVec_t::iterator itor;
        const bool exists = App::GetGameContext()->GetActorManager()->FindFreeForce(freebeam.fbx_freeforce_primary, itor);
        ROR_ASSERT(exists);
        if (!exists)
        {
            continue;
        }
        FreeForce& freeforce = *itor;
        
        // Sanity checks - base actor
        ROR_ASSERT(freeforce.ffc_base_actor);
        ROR_ASSERT(freeforce.ffc_base_actor->ar_state != ActorState::DISPOSED);
        GfxActor* gfx_actor_base = freeforce.ffc_base_actor->GetGfxActor();
        ROR_ASSERT(gfx_actor_base);
        ROR_ASSERT(freeforce.ffc_base_node != NODENUM_INVALID);
        ROR_ASSERT(freeforce.ffc_base_node < freeforce.ffc_base_actor->ar_num_nodes);

        // Sanity checks - target actor
        ROR_ASSERT(freeforce.ffc_target_actor);
        ROR_ASSERT(freeforce.ffc_target_actor->ar_state != ActorState::DISPOSED);
        GfxActor* gfx_actor_target = freeforce.ffc_target_actor->GetGfxActor();
        ROR_ASSERT(gfx_actor_target);
        ROR_ASSERT(freeforce.ffc_target_node != NODENUM_INVALID);
        ROR_ASSERT(freeforce.ffc_target_node < freeforce.ffc_target_actor->ar_num_nodes);

        // Get node positions
        Ogre::Vector3 basenode_pos = gfx_actor_base->GetSimNodeBuffer()[freeforce.ffc_base_node].AbsPosition;
        Ogre::Vector3 targetnode_pos = gfx_actor_target->GetSimNodeBuffer()[freeforce.ffc_target_node].AbsPosition;

        // Do the transforms
        freebeam.fbx_scenenode->setPosition(basenode_pos.midPoint(targetnode_pos));
        freebeam.fbx_scenenode->setOrientation(GfxScene::SpecialGetRotationTo(Ogre::Vector3::UNIT_Y, (basenode_pos - targetnode_pos)));
        freebeam.fbx_scenenode->setScale(freebeam.fbx_diameter, basenode_pos.distance(targetnode_pos), freebeam.fbx_diameter);
    }
}

void GfxScene::OnFreeForceRemoved(FreeForceID_t id)
{
    auto itor_secondary = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [id](const FreeBeamGfx& obj) { return obj.fbx_freeforce_secondary == id; });
    if (itor_secondary != m_gfx_freebeams.end())
    {
        // Just clear the freeforce ID
        itor_secondary->fbx_freeforce_secondary = FREEFORCEID_INVALID;
    }
    else
    {
        auto itor_primary = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
            [id](const FreeBeamGfx& obj) { return obj.fbx_freeforce_primary == id; });
        if (itor_primary != m_gfx_freebeams.end())
        {
            // Remove the whole freebeam
            this->RemoveFreeBeamGfx(itor_primary->fbx_id);
        }
    }
}

void GfxScene::OnFreeForceBroken(FreeForceID_t id)
{
    auto itor = std::find_if(m_gfx_freebeams.begin(), m_gfx_freebeams.end(),
        [id](const FreeBeamGfx& obj) { return obj.fbx_freeforce_primary == id || obj.fbx_freeforce_secondary == id; });
    if (itor != m_gfx_freebeams.end())
    {
        // Remove the whole freebeam if either freeforce broke
        this->RemoveFreeBeamGfx(itor->fbx_id);
    }
}

Ogre::Quaternion RoR::GfxScene::SpecialGetRotationTo(const Ogre::Vector3& src, const Ogre::Vector3& dest)
{
    // Based on Stan Melax's article in Game Programming Gems
    Ogre::Quaternion q;
    // Copy, since cannot modify local
    Ogre::Vector3 v0 = src;
    Ogre::Vector3 v1 = dest;
    v0.normalise();
    v1.normalise();

    // NB if the crossProduct approaches zero, we get unstable because ANY axis will do
    // when v0 == -v1
    Ogre::Real d = v0.dotProduct(v1);
    // If dot == 1, vectors are the same
    if (d >= 1.0f)
    {
        return Ogre::Quaternion::IDENTITY;
    }
    if (d < (1e-6f - 1.0f))
    {
        // Generate an axis
        Ogre::Vector3 axis = Ogre::Vector3::UNIT_X.crossProduct(src);
        if (axis.isZeroLength()) // pick another if colinear
            axis = Ogre::Vector3::UNIT_Y.crossProduct(src);
        axis.normalise();
        q.FromAngleAxis(Ogre::Radian(Ogre::Math::PI), axis);
    }
    else
    {
        Ogre::Real s = fast_sqrt((1 + d) * 2);
        if (s == 0)
            return Ogre::Quaternion::IDENTITY;

        Ogre::Vector3 c = v0.crossProduct(v1);
        Ogre::Real invs = 1 / s;

        q.x = c.x * invs;
        q.y = c.y * invs;
        q.z = c.z * invs;
        q.w = s * 0.5;
    }
    return q;
}
