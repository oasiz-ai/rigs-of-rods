/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2016 Petr Ohlidal

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

#include "Terrain.h"

#include "Actor.h"
#include "ActorManager.h"
#include "CacheSystem.h"
#include "Collisions.h"
#include "ContentManager.h"
#include "GfxScene.h"
#include "GUIManager.h"
#include "GUI_LoadingWindow.h"
#include "GUI_SurveyMap.h"
#include "HydraxWater.h"
#include "Language.h"
#include "ScriptEngine.h"
#include "ShadowManager.h"
#include "SkyManager.h"
#include "SkyXManager.h"
#include "TerrainGeometryManager.h"
#include "TerrainObjectManager.h"
#include "Terrn2FileFormat.h"
#include "Utils.h"
#include "GfxWater.h"

#include <Terrain/OgreTerrainPaging.h>
#include <Terrain/OgreTerrainGroup.h>

#include <algorithm>

using namespace RoR;
using namespace Ogre;

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
namespace {

/// The combined runtime admits textured procedural roads only through the
/// exact legacy material closure, and the stock road2 state cannot pass the
/// authenticated native extractor: its DXT1 diffuse cannot be read back as
/// uncompressed RGBA8, the fixed-function ambient defaults to white, and the
/// translator rejects nonzero ambient/specular/emissive lobes. Prepare the
/// exact canonical state here, before any procedural road finalizes its
/// snapshot from this material. Every missing precondition logs and leaves
/// the material untouched, so a later capture fails closed with its own
/// exact reason instead of observing a half-prepared material.
void PrepareCombinedRuntimeRoadMaterial(
    const std::string& terrain_resource_group)
{
    const char* const road_texture_name = "cityworld_road2_basecolor.png";
    const Ogre::MaterialPtr road_material =
        Ogre::MaterialManager::getSingleton().getByName(
            "road2", "MaterialsRG");
    if (!road_material)
    {
        LOG("[RoR|Terrain|CombinedRoadMaterial] material road2 is not "
            "loaded; procedural roads will fail the joined capture closed");
        return;
    }
    road_material->load();
    if (road_material->getNumTechniques() != 1 ||
        road_material->getTechnique(0) == nullptr ||
        road_material->getTechnique(0)->getNumPasses() != 1 ||
        road_material->getTechnique(0)->getPass(0) == nullptr ||
        road_material->getTechnique(0)->getPass(0)
                ->getNumTextureUnitStates() != 1)
    {
        LOG(fmt::format(
            "[RoR|Terrain|CombinedRoadMaterial] road2 is not the authored "
            "single-technique/single-pass/single-unit material "
            "(techniques={}); leaving it untouched",
            road_material->getNumTechniques()));
        return;
    }
    if (!Ogre::ResourceGroupManager::getSingleton().resourceExists(
            terrain_resource_group, road_texture_name))
    {
        LOG(fmt::format(
            "[RoR|Terrain|CombinedRoadMaterial] '{}' is not part of terrain "
            "group '{}'; road2 keeps its stock DXT1 state",
            road_texture_name, terrain_resource_group));
        return;
    }
    Ogre::TexturePtr road_texture;
    try
    {
        // The authenticated terrain archive is the source authority: the
        // receipt for this texture is minted while its bytes are fetched
        // from the SHA-256-verified member, which is what the authenticated
        // capture overload later requires. Hardware gamma marks the base
        // color as sRGB exactly once.
        road_texture = Ogre::TextureManager::getSingleton().load(
            road_texture_name, terrain_resource_group, Ogre::TEX_TYPE_2D,
            Ogre::MIP_DEFAULT, 1.0f, Ogre::PF_UNKNOWN,
            /*hwGammaCorrection:*/ true);
    }
    catch (const Ogre::Exception& e)
    {
        LOG(fmt::format(
            "[RoR|Terrain|CombinedRoadMaterial] loading '{}' failed: {}",
            road_texture_name, e.getDescription()));
        return;
    }
    if (!road_texture)
    {
        LOG(fmt::format(
            "[RoR|Terrain|CombinedRoadMaterial] loading '{}' produced no "
            "texture", road_texture_name));
        return;
    }
    Ogre::Pass* const road_pass =
        road_material->getTechnique(0)->getPass(0);
    Ogre::TextureUnitState* const road_unit =
        road_pass->getTextureUnitState(0);
    road_unit->setTexture(road_texture);
    road_unit->setHardwareGammaEnabled(true);
    // Pin the complete canonical sampler. The first sampler setter detaches
    // the unit from the mutable shared default onto a fresh local sampler,
    // so the captured state stays independent of global filtering config.
    // OGRE's sampler default keeps CMPF_GREATER_EQUAL even with comparison
    // disabled; the translator requires always-pass for a base color.
    road_unit->setTextureCompareEnabled(false);
    road_unit->setTextureCompareFunction(Ogre::CMPF_ALWAYS_PASS);
    // Canonical anisotropic tuple: passes the extractor, the translator
    // ([2,16] with min/mag ANISOTROPIC), and the N1 policy (lowered to
    // all-linear + integral anisotropy), reaching Metal as true 4x aniso.
    road_unit->setTextureFiltering(
        Ogre::FO_ANISOTROPIC, Ogre::FO_ANISOTROPIC, Ogre::FO_LINEAR);
    road_unit->setTextureAnisotropy(4);
    road_unit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_WRAP);
    road_unit->setTextureMipmapBias(0.0f);
    road_pass->setAmbient(Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f));
    road_pass->setDiffuse(Ogre::ColourValue(1.0f, 1.0f, 1.0f, 1.0f));
    road_pass->setSpecular(Ogre::ColourValue(0.0f, 0.0f, 0.0f, 0.0f));
    road_pass->setSelfIllumination(Ogre::ColourValue(0.0f, 0.0f, 0.0f));
    road_pass->setShininess(0.0f);
    LOG(fmt::format(
        "[RoR|Terrain|CombinedRoadMaterial] road2 prepared: texture='{}' "
        "({}x{}, format={}, hw_gamma={}), lobes zeroed",
        road_texture_name, road_texture->getWidth(),
        road_texture->getHeight(),
        static_cast<int>(road_texture->getFormat()),
        road_texture->isHardwareGammaEnabled()));
}

} // namespace
#endif // ROR_OGRE_NEXT_COMBINED_RUNTIME

RoR::Terrain::Terrain(CacheEntryPtr entry, Terrn2DocumentPtr def)
    : m_collisions(0)
    , m_geometry_manager(0)
    , m_object_manager(0)
    , m_shadow_manager(0)
    , m_sky_manager(0)
    , SkyX_manager(0)
    , m_sight_range(1000)
    , m_main_light(0)
    , m_paged_detail_factor(0.0f)
    , m_cur_gravity(DEFAULT_GRAVITY)
    , m_hydrax_water(nullptr)
    , m_cache_entry(entry)
    , m_def(def)
{
}

RoR::Terrain::~Terrain()
{
    if (!m_disposed)
    {
        this->dispose();
    }
}

void RoR::Terrain::dispose()
{
    // PSSM owns scene-manager cameras and RTSS projector state. It must be
    // disabled before any terrain light, geometry, or scene node disappears,
    // including the abbreviated application-shutdown path.
    if (m_shadow_manager != nullptr)
    {
        delete(m_shadow_manager);
        m_shadow_manager = nullptr;
    }

    if (App::app_state->getEnum<AppState>() == AppState::SHUTDOWN)
    {
        // Rush to exit
        return;
    }

    //I think that the order is important

#ifdef USE_CAELUM
    if (m_sky_manager != nullptr)
    {
        delete(m_sky_manager);
        m_sky_manager = nullptr;
    }
#endif // USE_CAELUM

    if (SkyX_manager != nullptr)
    {
        delete(SkyX_manager);
        SkyX_manager = nullptr;
    }

    if (m_hydrax_water != nullptr)
    {
        m_gfx_water.reset(); // TODO: Currently needed - research and get rid of this ~ only_a_ptr, 08/2018
    }

    if (m_object_manager != nullptr)
    {
        delete(m_object_manager);
        m_object_manager = nullptr;
    }

    // TerrainObjectManager owns every light created by an ODEF. Release those
    // registered lights before the scene-wide cleanup invalidates their
    // pointers; otherwise terrain unload double-destroys local lights.
    if (m_main_light != nullptr)
    {
        App::GetGfxScene()->GetSceneManager()->destroyAllLights();
        m_main_light = nullptr;
    }

    if (m_geometry_manager != nullptr)
    {
        delete(m_geometry_manager);
        m_geometry_manager = nullptr;
    }

    if (m_collisions != nullptr)
    {
        delete(m_collisions);
        m_collisions = nullptr;
    }

    if (m_wavefield)
    {
        m_wavefield.reset();
    }

    if (App::GetScriptEngine()->getTerrainScriptUnit() != SCRIPTUNITID_INVALID)
    {
        App::GetScriptEngine()->unloadScript(App::GetScriptEngine()->getTerrainScriptUnit());
    }

    m_disposed = true;
}

bool RoR::Terrain::DisposeForFatalShutdown() noexcept
{
    if (m_disposed)
    {
        return !m_fatal_dispose_failed;
    }

    try
    {
        this->dispose();
    }
    catch (...)
    {
        m_fatal_dispose_failed = true;
    }

    if (!m_disposed)
    {
        // A partial dispose (including the legacy SHUTDOWN fast path) must
        // never be retried after RendererRuntimeGuard has released Ogre::Root.
        m_fatal_dispose_failed = true;
        m_disposed = true;
    }
    return !m_fatal_dispose_failed;
}

bool RoR::Terrain::initialize()
{
    auto* loading_window = &App::GetGuiManager()->LoadingWindow;

    this->setGravity(this->m_def->gravity);

    loading_window->SetProgress(10, _L("Initializing Object Subsystem"));
    this->initObjects(); // *.odef files

    loading_window->SetProgress(17, _L("Initializing Geometry Subsystem"));
    this->m_geometry_manager = new TerrainGeometryManager(this);

    loading_window->SetProgress(23, _L("Initializing Camera Subsystem"));
    this->initCamera();

    // sky, must come after camera due to m_sight_range
    loading_window->SetProgress(25, _L("Initializing Sky Subsystem"));
    this->initSkySubSystem();

    loading_window->SetProgress(27, _L("Initializing Light Subsystem"));
    this->initLight();

    // The loading window renders intermediate frames. PSSM setup requires a
    // valid terrain camera and directional light before any such frame can be
    // rendered, otherwise OGRE may resolve an incomplete shadow projector.
    loading_window->SetProgress(28, _L("Initializing Shadow Subsystem"));
    this->initShadows();

    if (GetEffectiveGfxSkyMode() != GfxSkyMode::CAELUM) //Caelum has its own fog management
    {
        loading_window->SetProgress(29, _L("Initializing Fog Subsystem"));
        this->initFog();
    }

    loading_window->SetProgress(31, _L("Initializing Vegetation Subsystem"));
    this->initVegetation();

    this->fixCompositorClearColor();

    loading_window->SetProgress(40, _L("Loading Terrain Geometry"));
    if (!this->m_geometry_manager->InitTerrain(this->m_def->ogre_ter_conf_filename))
    {
        return false; // Error already reported
    }

    loading_window->SetProgress(60, _L("Initializing Collision Subsystem"));
    this->m_collisions = new Collisions(this->getMaxTerrainSize());

    loading_window->SetProgress(75, _L("Initializing Script Subsystem"));
    this->initScripting();
    this->initAiPresets();

    loading_window->SetProgress(77, _L("Initializing Water Subsystem"));
    this->initWater();

#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)
    // Must precede the first ProceduralRoad::finish so every finalized road
    // snapshot already observes the canonical closure-capable state.
    PrepareCombinedRuntimeRoadMaterial(this->getTerrainFileResourceGroup());
#endif

    loading_window->SetProgress(80, _L("Loading Terrain Objects"));
    this->loadTerrainObjects(); // *.tobj files

    // init things after loading the terrain
    this->initTerrainCollisions();

    loading_window->SetProgress(90, _L("Initializing terrain light properties"));
    this->m_geometry_manager->UpdateMainLightPosition(); // Initial update takes a while
    this->m_collisions->finishLoadingTerrain();

    this->LoadTelepoints(); // *.terrn2 file feature

    App::GetGfxScene()->CreateDustPools(); // Particle effects

    loading_window->SetProgress(92, _L("Initializing Overview Map Subsystem"));
    App::GetGuiManager()->SurveyMap.CreateTerrainTextures(); // Should be done before actors are loaded, otherwise they'd show up in the static texture

    LOG(" ===== LOADING TERRAIN ACTORS " + m_cache_entry->fname);
    loading_window->SetProgress(95, _L("Loading Terrain Actors"));
    this->LoadPredefinedActors();

    // Every material this map will show is now loaded. Generate their
    // RTShader techniques here rather than letting the resolver listener
    // create each one on first visibility, which resolves shadowing as the
    // camera enters new areas and is visible as flashing.
    loading_window->SetProgress(98, _L("Preparing shaders"));
    App::GetAppContext()->PrewarmRTShaderTechniques();

    LOG(" ===== TERRAIN LOADING DONE " + m_cache_entry->fname);

    App::sim_terrain_name->setStr(m_cache_entry->fname);
    App::sim_terrain_gui_name->setStr(this->m_def->name);

    return this;
}

void RoR::Terrain::initCamera()
{
    App::GetCameraManager()->GetCamera()->getViewport()->setBackgroundColour(m_def->ambient_color);
    App::GetCameraManager()->GetCameraNode()->setPosition(m_def->start_position);

    if (GetEffectiveGfxSkyMode() == GfxSkyMode::SKYX)
    {
        m_sight_range = 5000;  //Force unlimited for SkyX, lower settings are glitchy
    } 
    else
    {
        m_sight_range = App::gfx_sight_range->getInt();
    } 

    if (m_sight_range < UNLIMITED_SIGHTRANGE && GetEffectiveGfxSkyMode() != GfxSkyMode::SKYX)
    {
        App::GetCameraManager()->GetCamera()->setFarClipDistance(m_sight_range);
    }
    else
    {
        // disabled in global config
        if (App::gfx_water_mode->getEnum<GfxWaterMode>() != GfxWaterMode::HYDRAX)
            App::GetCameraManager()->GetCamera()->setFarClipDistance(0); //Unlimited
        else
            App::GetCameraManager()->GetCamera()->setFarClipDistance(9999 * 6); //Unlimited for hydrax and stuff
    }
}

void RoR::Terrain::initSkySubSystem()
{
#ifdef USE_CAELUM
    // Caelum skies
    if (GetEffectiveGfxSkyMode() == GfxSkyMode::CAELUM)
    {
        m_sky_manager = new SkyManager();

        // try to load caelum config
        if (!m_def->caelum_config.empty() && ResourceGroupManager::getSingleton().resourceExistsInAnyGroup(m_def->caelum_config))
        {
            // config provided and existing, use it :)
            m_sky_manager->LoadCaelumScript(m_def->caelum_config, m_def->caelum_fog_start, m_def->caelum_fog_end);
        }
        else
        {
            // no config provided, fall back to the default one
            m_sky_manager->LoadCaelumScript("ror_default_sky");
        }
    }
    else
#endif //USE_CAELUM
    // SkyX skies
    if (GetEffectiveGfxSkyMode() == GfxSkyMode::SKYX)
    {
         // try to load SkyX config
         if (!m_def->skyx_config.empty() && ResourceGroupManager::getSingleton().resourceExistsInAnyGroup(m_def->skyx_config))
            SkyX_manager = new SkyXManager(m_def->skyx_config);
         else
            SkyX_manager = new SkyXManager("SkyXDefault.skx");
    }
    else
    {
        if (!m_def->cubemap_config.empty())
        {
            // use custom
            App::GetGfxScene()->GetSceneManager()->setSkyBox(true, m_def->cubemap_config, 100, true);
        }
        else
        {
            // use default
            App::GetGfxScene()->GetSceneManager()->setSkyBox(true, "tracks/skyboxcol", 100, true);
        }
    }
}

void RoR::Terrain::initLight()
{
    if (GetEffectiveGfxSkyMode() == GfxSkyMode::CAELUM)
    {
#ifdef USE_CAELUM
        m_main_light = m_sky_manager->GetSkyMainLight();
#endif
    }
    else if (GetEffectiveGfxSkyMode() == GfxSkyMode::SKYX)
    {
        m_main_light = SkyX_manager->getMainLight();
    }
    else
    {
        // screw caelum, we will roll our own light

        // The terrain definition supplies the fallback sky tint, but older
        // code only applied it to the directional light. Keep a bounded
        // ambient contribution so surfaces facing away from the sun do not
        // render against OGRE's black default ambient.
        constexpr float FALLBACK_AMBIENT_SCALE = 0.35f;
        const ColourValue fallback_ambient(
            std::clamp(m_def->ambient_color.r, 0.0f, 1.0f)
                * FALLBACK_AMBIENT_SCALE,
            std::clamp(m_def->ambient_color.g, 0.0f, 1.0f)
                * FALLBACK_AMBIENT_SCALE,
            std::clamp(m_def->ambient_color.b, 0.0f, 1.0f)
                * FALLBACK_AMBIENT_SCALE);
        SceneManager* scene_manager =
            App::GetGfxScene()->GetSceneManager();
        scene_manager->setAmbientLight(fallback_ambient);

        // Create a light
        m_main_light = scene_manager->createLight("MainLight");
        //directional light for shadow
        m_main_light->setType(Light::LT_DIRECTIONAL);
        m_main_light->setDirection(Ogre::Vector3(0.785, -0.423, 0.453).normalisedCopy());

        m_main_light->setDiffuseColour(m_def->ambient_color);
        m_main_light->setSpecularColour(m_def->ambient_color);
        m_main_light->setCastShadows(true);
        m_main_light->setShadowFarDistance(1000.0f);
        m_main_light->setShadowNearClipDistance(-1);
        LOG(fmt::format(
            "[RoR|Terrain|Lighting] policy=fallback-v1 "
            "ambient_scale={:.3f} directional_shadow_casters={} "
            "ambient_rgb={:.3f},{:.3f},{:.3f}",
            FALLBACK_AMBIENT_SCALE,
            m_main_light->getCastShadows() ? 1 : 0,
            fallback_ambient.r,
            fallback_ambient.g,
            fallback_ambient.b));
    }
}

void RoR::Terrain::initFog()
{
    if (m_sight_range >= UNLIMITED_SIGHTRANGE)
        App::GetGfxScene()->GetSceneManager()->setFog(FOG_NONE);
    else
        App::GetGfxScene()->GetSceneManager()->setFog(FOG_LINEAR, m_def->ambient_color, 0.000f, m_sight_range * 0.65f, m_sight_range*0.9);
}

void RoR::Terrain::initVegetation()
{
    switch (App::gfx_vegetation_mode->getEnum<GfxVegetation>())
    {
    case GfxVegetation::x20PERC:
        m_paged_detail_factor = 0.2f;
        break;
    case GfxVegetation::x50PERC:
        m_paged_detail_factor = 0.5f;
        break;
    case GfxVegetation::FULL:
        m_paged_detail_factor = 1.0f;
        break;
    default:
        m_paged_detail_factor = 0.0f;
        break;
    }
}

void RoR::Terrain::fixCompositorClearColor()
{
    // hack
    // now with extensive error checking
    if (CompositorManager::getSingleton().hasCompositorChain(App::GetCameraManager()->GetCamera()->getViewport()))
    {
        CompositorInstance* co = CompositorManager::getSingleton().getCompositorChain(App::GetCameraManager()->GetCamera()->getViewport())->_getOriginalSceneCompositor();
        if (co)
        {
            CompositionTechnique* ct = co->getTechnique();
            if (ct)
            {
                CompositionTargetPass* ctp = ct->getOutputTargetPass();
                if (ctp)
                {
                    ROR_ASSERT(ctp->getPasses().size() > 0);
                    CompositionPass* p = ctp->getPasses()[0];
                    if (p)
                    {
                        p->setClearColour(Ogre::ColourValue::Black);
                    }
                }
            }
        }
    }
}

void RoR::Terrain::initWater()
{
    // disabled in global config
    if (App::gfx_water_mode->getEnum<GfxWaterMode>() == GfxWaterMode::NONE)
        return;

    // disabled in map config
    if (!m_def->has_water)
    {
        return;
    }

    m_wavefield = std::unique_ptr<Wavefield>(new Wavefield(this->getMaxTerrainSize()));
    m_wavefield->SetStaticWaterHeight(m_def->water_height);

    if (App::gfx_water_mode->getEnum<GfxWaterMode>() == GfxWaterMode::HYDRAX)
    {
        // try to load hydrax config
        if (!m_def->hydrax_conf_file.empty() && ResourceGroupManager::getSingleton().resourceExistsInAnyGroup(m_def->hydrax_conf_file))
        {
            m_hydrax_water = new HydraxWater(m_def->water_height, m_def->hydrax_conf_file);
        }
        else
        {
            // no config provided, fall back to the default one
            m_hydrax_water = new HydraxWater(m_def->water_height);
        }

        m_gfx_water = std::unique_ptr<IGfxWater>(m_hydrax_water);

        //Apply depth technique to the terrain
        TerrainGroup::TerrainIterator ti = m_geometry_manager->getTerrainGroup()->getTerrainIterator();
        while (ti.hasMoreElements())
        {
            Ogre::Terrain* t = ti.getNext()->instance;
            MaterialPtr ptr = t->getMaterial();
            m_hydrax_water->GetHydrax()->getMaterialManager()->addDepthTechnique(ptr->createTechnique());
        }
    }
    else
    {
        m_gfx_water = std::unique_ptr<IGfxWater>(new GfxWater(this->getMaxTerrainSize(), m_def->water_height));
        m_gfx_water->SetWaterBottomHeight(m_def->water_bottom_height);
    }
}

void RoR::Terrain::initShadows()
{
    m_shadow_manager = new ShadowManager();
    m_shadow_manager->loadConfiguration();
}

void RoR::Terrain::loadTerrainObjects()
{
    for (std::string tobj_filename : m_def->tobj_files)
    {
        m_object_manager->LoadTObjFile(tobj_filename);
    }
}

void RoR::Terrain::initTerrainCollisions()
{
    if (!m_def->traction_map_file.empty())
    {
        m_collisions->setupLandUse(m_def->traction_map_file.c_str());
    }
}

void RoR::Terrain::initScripting()
{
#ifdef USE_ANGELSCRIPT
    // suspend AS logging, so we dont spam the users screen with initialization messages
    App::GetScriptEngine()->setForwardScriptLogToConsole(false);

    bool loaded = false;

    for (std::string as_filename : m_def->as_files)
    {
        loaded |= this->getObjectManager()->LoadTerrainScript(as_filename);
    }

    if (!loaded)
    {
        // load a default script that does the most basic things
        this->getObjectManager()->LoadTerrainScript(DEFAULT_TERRAIN_SCRIPT);
    }

    // finally resume AS logging
    App::GetScriptEngine()->setForwardScriptLogToConsole(true);
#endif //USE_ANGELSCRIPT
}

void RoR::Terrain::initAiPresets()
{
    // Load 'bundled' AI presets - see section `[AI Presets]` in terrn2 file format
    // ----------------------------------------------------------------------------

    App::GetGuiManager()->TopMenubar.LoadBundledAiPresets(this);
}

void RoR::Terrain::setGravity(float value)
{
    m_cur_gravity = value;
}

void RoR::Terrain::initObjects()
{
    m_object_manager = new TerrainObjectManager(this);
}

Ogre::AxisAlignedBox RoR::Terrain::getTerrainCollisionAAB()
{
    return m_collisions->getCollisionAAB();
}

Ogre::Vector3 RoR::Terrain::getMaxTerrainSize()
{
    if (!m_geometry_manager)
        return Vector3::ZERO;
    return m_geometry_manager->getMaxTerrainSize();
}

float RoR::Terrain::getHeightAt(float x, float z)
{
    return m_geometry_manager->getHeightAt(x, z);
}

Ogre::Vector3 RoR::Terrain::GetNormalAt(float x, float y, float z)
{
    return m_geometry_manager->getNormalAt(x, y, z);
}

SkyManager* RoR::Terrain::getSkyManager()
{
    return m_sky_manager;
}

bool RoR::Terrain::isFlat()
{
    if (m_disposed)
        return false;
    else
        return m_geometry_manager->isFlat();
}

void RoR::Terrain::LoadTelepoints()
{
    if (m_object_manager)
        m_object_manager->LoadTelepoints();
}

void RoR::Terrain::LoadPredefinedActors()
{
    if (m_object_manager)
        m_object_manager->LoadPredefinedActors();
}

bool RoR::Terrain::HasPredefinedActors()
{
    if (m_object_manager)
        return m_object_manager->HasPredefinedActors();
    return false;
}

ProceduralManagerPtr RoR::Terrain::getProceduralManager()
{
    return m_object_manager->getProceduralManager();
}

std::string RoR::Terrain::getTerrainFileName()
{
    return m_cache_entry->fname;
}

std::string RoR::Terrain::getTerrainFileResourceGroup()
{
    return m_cache_entry->resource_group;
}

void RoR::Terrain::addSurveyMapEntity(const std::string& type, const std::string& filename, const std::string& resource_group, const std::string& caption, const Ogre::Vector3& pos, float angle, int id)
{
    m_object_manager->m_map_entities.push_back(SurveyMapEntity(type, caption, filename, resource_group, pos, Ogre::Radian(angle), id));
}

void RoR::Terrain::delSurveyMapEntities(int id)
{
    EraseIf(m_object_manager->m_map_entities, [id](const SurveyMapEntity& e) { return e.id == id; });
}

SurveyMapEntityVec& RoR::Terrain::getSurveyMapEntities()
{
    return m_object_manager->m_map_entities;
}

Terrn2DocumentPtr RoR::Terrain::GetDef() { return m_def; }

std::string   RoR::Terrain::getTerrainName() const { return m_def->name; }

std::string   RoR::Terrain::getGUID() const { return m_def->guid; }

int           RoR::Terrain::getVersion() const { return m_def->version; }

CacheEntryPtr RoR::Terrain::getCacheEntry() { return m_cache_entry; }

Ogre::Vector3 RoR::Terrain::getSpawnPos() { return m_def->start_position; }

Ogre::Degree RoR::Terrain::getSpawnRot() { return m_def->start_rotation; }

float         RoR::Terrain::getWaterHeight() const { return m_def->water_height; }
