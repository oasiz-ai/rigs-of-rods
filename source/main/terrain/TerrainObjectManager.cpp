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

#include "TerrainObjectManager.h"

#include "Actor.h"
#include "Application.h"
#include "AutoPilot.h"
#include "CacheSystem.h"
#include "CameraManager.h"
#include "CityWorldNeoQ20Compatibility.h"
#include "CityWorldNeoQTreeCompatibility.h"
#include "Collisions.h"
#include "Console.h"
#include "ErrorUtils.h"
#include "Language.h"
#include "GameContext.h"
#include "GfxScene.h"
#include "GUIManager.h"
#include "GUI_LoadingWindow.h"
#include "MeshObject.h"
#include "ODefFileFormat.h"
#include "PlatformUtils.h"
#include "ProceduralRoad.h"
#include "ScriptEngine.h"
#include "SoundScriptManager.h"
#include "TerrainGeometryManager.h"
#include "Terrain.h"
#include "Terrn2FileFormat.h"
#include "TObjFileFormat.h"
#include "Utils.h"
#include "WriteTextToTexture.h"

#include <RTShaderSystem/OgreRTShaderSystem.h>
#include <Overlay/OgreFontManager.h>

#include <cstring>
#include <list>
#include <stdexcept>

#ifdef USE_ANGELSCRIPT
#    include "ExtinguishableFireAffector.h"
#endif // USE_ANGELSCRIPT

using namespace Ogre;
using namespace RoR;
#ifdef USE_PAGED
using namespace Forests;
#endif //USE_PAGED

//workaround for pagedgeometry
inline float getTerrainHeight(Real x, Real z, void* unused = 0)
{
    return App::GetGameContext()->GetTerrain()->getHeightAt(x, z);
}

TerrainObjectManager::TerrainObjectManager(Terrain* terrainManager) :
    terrainManager(terrainManager)
{
    m_terrn2_grouping_node = App::GetGfxScene()->GetSceneManager()->getRootSceneNode()->createChildSceneNode(fmt::format("Terrain: {}", terrainManager->GetDef()->name));

    m_procedural_manager = new ProceduralManager(m_terrn2_grouping_node->createChildSceneNode("Procedural Roads"));
}

TerrainObjectManager::~TerrainObjectManager()
{
    for (MeshObject* mo : m_mesh_objects)
    {
        if (mo)
            delete mo;
    }
#ifdef USE_PAGED
    for (auto geom : m_paged_geometry)
    {
        delete geom->getPageLoader();
        delete geom;
    }
#endif //USE_PAGED

    this->DestroyAllRegisteredLocalLights();
    App::GetGfxScene()->GetSceneManager()->destroyAllEntities();

    App::GetGfxScene()->GetSceneManager()->destroySceneNode(m_terrn2_grouping_node);
}

void GenerateGridAndPutToScene(Ogre::Vector3 position)
{
    Ogre::ColourValue background_color(Ogre::ColourValue::White);
    Ogre::ColourValue grid_color(0.2f, 0.2f, 0.2f, 1.0f);

    Ogre::ManualObject* mo = new Ogre::ManualObject("ReferenceGrid");

    mo->begin("BaseWhiteNoLighting", Ogre::RenderOperation::OT_LINE_LIST);

    const float step = 1.0f;
    const size_t count = 50;
    unsigned int halfCount = count / 2;
    const float half = (step * count) / 2;
    const float y = 0;
    Ogre::ColourValue c;
    for (size_t i=0; i < count+1; i++)
    {
        if (i == halfCount)
            c = Ogre::ColourValue(1.f, 0.f, 0.f, 1.f);
        else
            c = grid_color;

        mo->position(-half, y, -half+(step*i));
        mo->colour(background_color);
        mo->position(0, y, -half+(step*i));
        mo->colour(c);
        mo->position(0, y, -half+(step*i));
        mo->colour(c);
        mo->position(half, y, -half+(step*i));
        mo->colour(background_color);

        if (i == halfCount)
            c = Ogre::ColourValue(0,0,1,1.0f);
        else
            c = grid_color;

        mo->position(-half+(step*i), y, -half);
        mo->colour(background_color);
        mo->position(-half+(step*i), y, 0);
        mo->colour(c);
        mo->position(-half+(step*i), y, 0);
        mo->colour(c);
        mo->position(-half+(step*i), y, half);
        mo->colour(background_color);
    }

    mo->end();
    mo->setCastShadows(false);
    Ogre::SceneNode *n = App::GetGameContext()->GetTerrain()->getObjectManager()->getGroupingSceneNode()->createChildSceneNode();
    n->setPosition(position);
    n->attachObject(mo);
    n->setVisible(true);
}

void TerrainObjectManager::LoadTObjFile(Ogre::String tobj_name)
{
    ROR_ASSERT(this->terrainManager);
    ROR_ASSERT(this->terrainManager->getCacheEntry());
    ROR_ASSERT(this->terrainManager->getCacheEntry()->resource_group != "");

    TObjDocumentPtr tobj;
    std::vector<std::string> authored_dependencies(
        this->terrainManager->GetDef()->resource_bundle_dependencies.begin(),
        this->terrainManager->GetDef()->resource_bundle_dependencies.end());
    const bool inspect_cityworld_neoq20 =
        tobj_name == "CityWorld.tobj" &&
        HasCityWorldNeoQ20PinnedDependency(authored_dependencies);
    std::string observed_tobj_sha256;
    try
    {
        DataStreamPtr stream_ptr = ResourceGroupManager::getSingleton().openResource(
            tobj_name, this->terrainManager->getCacheEntry()->resource_group);
        static const std::size_t MAX_CITYWORLD_TOBJ_BYTES =
            32U * 1024U * 1024U;
        if (inspect_cityworld_neoq20 &&
            stream_ptr->size() <= MAX_CITYWORLD_TOBJ_BYTES)
        {
            const std::string payload = stream_ptr->getAsString();
            observed_tobj_sha256 =
                ComputeCityWorldNeoQ20Sha256(payload);
            stream_ptr->seek(0U);
        }
        TObjParser parser;
        parser.Prepare();
        parser.ProcessOgreStream(stream_ptr.get());
        tobj = parser.Finalize();

        if (inspect_cityworld_neoq20)
        {
            std::vector<CityWorldNeoQ20Placement> placements;
            placements.reserve(tobj->objects.size());
            for (const TObjEntry& entry : tobj->objects)
            {
                placements.push_back({
                    entry.source_line,
                    entry.odef_name,
                    entry.type,
                    entry.instance_name,
                    entry.position.x,
                    entry.position.y,
                    entry.position.z,
                    entry.rotation.x,
                    entry.rotation.y,
                    entry.rotation.z});
            }
            std::vector<CityWorldNeoQ20Telepoint> telepoints;
            telepoints.reserve(
                terrainManager->GetDef()->telepoints.size());
            for (const Terrn2Telepoint& source :
                 terrainManager->GetDef()->telepoints)
            {
                telepoints.push_back({
                    source.name,
                    source.position.x,
                    source.position.y,
                    source.position.z});
            }

            const CityWorldNeoQ20CompatibilityResult grounding =
                ApplyCityWorldNeoQ20Compatibility(
                    authored_dependencies,
                    tobj_name,
                    observed_tobj_sha256,
                    placements,
                    telepoints);
            const CityWorldNeoQTreeCompatibilityResult tree_replacement =
                ApplyCityWorldNeoQTreeCompatibility(
                    authored_dependencies,
                    tobj_name,
                    observed_tobj_sha256,
                    placements);
            bool tree_resources_available = tree_replacement.applied;
            std::size_t tree_resource_count = 0U;
            if (tree_resources_available)
            {
                const std::string resource_group =
                    terrainManager->getTerrainFileResourceGroup();
                for (const CityWorldNeoQ20Placement& placement :
                     placements)
                {
                    if (placement.source_line < 9U ||
                        placement.source_line > 26U)
                    {
                        continue;
                    }
                    ++tree_resource_count;
                    tree_resources_available &=
                        ResourceGroupManager::getSingleton().resourceExists(
                            resource_group,
                            placement.object_definition + ".odef");
                }
                tree_resources_available &=
                    tree_resource_count ==
                        tree_replacement.replacement_count;
            }
            bool tobj_cached = false;
            if (grounding.applied &&
                tree_replacement.applied &&
                tree_resources_available)
            {
                ROR_ASSERT(placements.size() == tobj->objects.size());
                bool names_fit = true;
                for (const CityWorldNeoQ20Placement& placement :
                     placements)
                {
                    names_fit &=
                        placement.instance_name.size() < TObj::STR_LEN &&
                        placement.object_definition.size() < TObj::STR_LEN;
                }
                if (!names_fit)
                {
                    throw std::runtime_error(
                        "NeoQ compatibility committed name exceeds "
                        "TObj::STR_LEN");
                }

                // Finish all potentially allocating work before mutating the
                // TObjDocument or terrain definition.
                std::list<Terrn2Telepoint> committed_telepoints;
                for (const CityWorldNeoQ20Telepoint& source :
                     telepoints)
                {
                    Terrn2Telepoint telepoint;
                    telepoint.name = source.name;
                    telepoint.position = Ogre::Vector3(
                        source.position_x,
                        source.position_y,
                        source.position_z);
                    committed_telepoints.push_back(telepoint);
                }

                const Terrn2DocumentPtr terrain_definition =
                    terrainManager->GetDef();
                CommitCityWorldNeoQ20RuntimeState(
                    [&]()
                    {
                        return fmt::format(
                            "[RoR|CityWorld|NeoQ20Grounding] Applied "
                            "placements={} renames={} telepoints={} "
                            "tree_replacements={} transactionally before "
                            "object instantiation "
                            "(tobj_sha256={})",
                            grounding.placement_changed_count,
                            grounding.renamed_instance_count,
                            grounding.telepoint_changed_count,
                            tree_replacement.replacement_count,
                            observed_tobj_sha256);
                    },
                    [&]()
                    {
                        // Cache insertion is the final potentially allocating
                        // prerequisite for the object-loading loops below. If
                        // reserve or insertion throws, the surrounding catch
                        // returns while authoritative state is untouched.
                        m_tobj_cache.reserve(m_tobj_cache.size() + 1U);
                        m_tobj_cache.push_back(tobj);
                    },
                    [&]() noexcept
                    {
                        // The same transformed entry is passed to
                        // LoadTerrainObject() below, so ODEF visuals and every
                        // collision primitive receive one identical transform.
                        for (std::size_t index = 0U;
                             index < placements.size();
                             ++index)
                        {
                            tobj->objects[index].position.y =
                                placements[index].position_y;
                            tobj->objects[index].rotation.x =
                                placements[index].rotation_x;
                            tobj->objects[index].rotation.y =
                                placements[index].rotation_y;
                            tobj->objects[index].rotation.z =
                                placements[index].rotation_z;
                            std::memset(
                                tobj->objects[index].odef_name,
                                0,
                                TObj::STR_LEN);
                            std::memcpy(
                                tobj->objects[index].odef_name,
                                placements[index]
                                    .object_definition.data(),
                                placements[index]
                                    .object_definition.size());
                            std::memset(
                                tobj->objects[index].instance_name,
                                0,
                                TObj::STR_LEN);
                            std::memcpy(
                                tobj->objects[index].instance_name,
                                placements[index].instance_name.data(),
                                placements[index].instance_name.size());
                        }
                        terrain_definition->telepoints.swap(
                            committed_telepoints);
                    },
                    [](const std::string& message)
                    {
                        LOG(message);
                    });
                tobj_cached = true;
            }
            else
            {
                LOG(fmt::format(
                    "[RoR|CityWorld|NeoQCompatibility] Preserved "
                    "CityWorld.tobj without a partial transform: "
                    "grounding='{}' trees='{}' tree_resources={} "
                    "(tobj_sha256={})",
                    grounding.applied
                        ? "ready"
                        : grounding.rejection_reason,
                    tree_replacement.applied
                        ? "ready"
                        : tree_replacement.rejection_reason,
                    tree_resources_available
                        ? "ready"
                        : "missing-or-incomplete",
                    observed_tobj_sha256.empty()
                        ? "unavailable"
                        : observed_tobj_sha256));
            }
            if (!tobj_cached)
            {
                m_tobj_cache.push_back(tobj);
            }
        }
        else
        {
            m_tobj_cache.push_back(tobj);
        }
    }
    catch (...)
    {
        HandleGenericException(fmt::format("Loading TObj file '{}'", tobj_name), HANDLEGENERICEXCEPTION_CONSOLE);
        return;
    }

    ROR_ASSERT(m_terrn2_grouping_node);
    m_tobj_grouping_node = m_terrn2_grouping_node->createChildSceneNode(tobj_name);

    int mapsizex = terrainManager->getGeometryManager()->getMaxTerrainSize().x;
    int mapsizez = terrainManager->getGeometryManager()->getMaxTerrainSize().z;

    // Section 'grid'
    if (tobj->grid_enabled)
    {
        GenerateGridAndPutToScene(tobj->grid_position);
    }

    // Section 'trees'
    if (App::gfx_vegetation_mode->getEnum<GfxVegetation>() != GfxVegetation::NONE)
    {
        for (TObjTree tree : tobj->trees)
        {
            try
            {
                this->ProcessTree(
                    tree.yaw_from, tree.yaw_to,
                    tree.scale_from, tree.scale_to,
                    tree.color_map, tree.density_map, tree.tree_mesh, tree.collision_mesh,
                    tree.grid_spacing, tree.high_density,
                    tree.min_distance, tree.max_distance, mapsizex, mapsizez);
            }
            catch (...)
            {
                RoR::HandleGenericException(fmt::format("Error processing 'trees' line (mesh: {}) from TOBJ file {}", tree.tree_mesh, tobj_name));
            }
        }
    }

    // Section 'grass' / 'grass2'
    if (App::gfx_vegetation_mode->getEnum<GfxVegetation>() != GfxVegetation::NONE)
    {
        for (TObjGrass grass : tobj->grass)
        {
            try
            {
                this->ProcessGrass(
                    grass.sway_speed, grass.sway_length, grass.sway_distrib, grass.density,
                    grass.min_x, grass.min_y, grass.min_h,
                    grass.max_x, grass.max_y, grass.max_h,
                    grass.material_name, grass.color_map_filename, grass.density_map_filename,
                    grass.grow_techniq, grass.technique, grass.range, mapsizex, mapsizez);
            }
            catch (...)
            {
                RoR::HandleGenericException(fmt::format("Error processing 'grass' line (material: {}) from TOBJ file {}", grass.material_name, tobj_name));
            }
        }
    }

    // Procedural roads
    for (ProceduralObjectPtr& po : tobj->proc_objects)
    {
        try
        {
            m_procedural_manager->addObject(po);
        }
        catch (...)
        {
            RoR::HandleGenericException(fmt::format("Error processing procedural road {} from TOBJ file {}", po->name, tobj_name));
        }
    }

    // Vehicles
    for (TObjVehicle const& veh : tobj->vehicles)
    {
        int tobj_cache_id = (int)m_tobj_cache.size() - 1;
        this->ProcessPredefinedActor(tobj_cache_id, veh.name, veh.position, veh.tobj_rotation, veh.type);
    }

    // Entries
    for (TObjEntry entry : tobj->objects)
    {
        try
        {
            m_tobj_cache_active_id = (int)m_tobj_cache.size() - 1;
            size_t num_editor_objects = m_editor_objects.size();
            this->LoadTerrainObject(entry.odef_name, entry.position, entry.rotation, entry.instance_name, entry.type, entry.rendering_distance);
            m_tobj_cache_active_id = -1;
            if (m_editor_objects.size() > num_editor_objects)
            {
                m_editor_objects.back()->tobj_comments = entry.comments;
            }
        }
        catch (...)
        {
            RoR::HandleGenericException(fmt::format("Error processing object line (ODEF: {}) from TOBJ file {}", entry.odef_name, tobj_name));
        }
    }

    if (App::diag_terrn_log_roads->getBool())
    {
        m_procedural_manager->logDiagnostics();
    }

    m_tobj_grouping_node = nullptr;
}

void TerrainObjectManager::ProcessTree(
    float yawfrom, float yawto,
    float scalefrom, float scaleto,
    char* ColorMap, char* DensityMap, char* treemesh, char* treeCollmesh,
    float gridspacing, float highdens,
    int minDist, int maxDist, int mapsizex, int mapsizez)
{
#ifdef USE_PAGED
    if (strnlen(ColorMap, 3) == 0)
    {
        LOG("tree ColorMap map zero!");
        return;
    }
    if (strnlen(DensityMap, 3) == 0)
    {
        LOG("tree DensityMap zero!");
        return;
    }
    Forests::DensityMap *densityMap = Forests::DensityMap::load(DensityMap, Forests::CHANNEL_COLOR);
    if (!densityMap)
    {
        LOG("could not load densityMap: "+String(DensityMap));
        return;
    }
    densityMap->setFilter(Forests::MAPFILTER_BILINEAR);
    //densityMap->setMapBounds(TRect(0, 0, mapsizex, mapsizez));

    PagedGeometry* geom = new PagedGeometry();
    geom->setTempDir(App::sys_cache_dir->getStr() + PATH_SLASH);
    geom->setCamera(App::GetCameraManager()->GetCamera());
    geom->setPageSize(50);
    geom->setInfinite();
    Ogre::TRect<Ogre::Real> bounds = TBounds(0, 0, mapsizex, mapsizez);
    geom->setBounds(bounds);

    //Set up LODs
    //trees->addDetailLevel<EntityPage>(50);
    float min = minDist * terrainManager->getPagedDetailFactor();
    if (min < 10)
        min = 10;
    geom->addDetailLevel<BatchPage>(min, min / 2);
    float max = maxDist * terrainManager->getPagedDetailFactor();
    if (max < 10)
        max = 10;

    // Check if farther details level is greater than closer
    if (max / 10 > min / 2)
    {
        geom->addDetailLevel<ImpostorPage>(max, max / 10);
    }

    TreeLoader2D *treeLoader = new TreeLoader2D(geom, TBounds(0, 0, mapsizex, mapsizez));
    treeLoader->setMinimumScale(scalefrom);
    treeLoader->setMaximumScale(scaleto);
    geom->setPageLoader(treeLoader);
    treeLoader->setHeightFunction(&getTerrainHeight);
    if (String(ColorMap) != "none")
    {
        treeLoader->setColorMap(ColorMap);
    }

    Entity* curTree = App::GetGfxScene()->GetSceneManager()->createEntity(String("paged_") + treemesh + TOSTRING(m_paged_geometry.size()), treemesh);

    if (gridspacing > 0)
    {
        // grid style
        for (float x=0; x < mapsizex; x += gridspacing)
        {
            for (float z=0; z < mapsizez; z += gridspacing)
            {
                float density = densityMap->_getDensityAt_Unfiltered(x, z, bounds);
                if (density < 0.8f) continue;
                float nx = x + gridspacing * 0.5f;
                float nz = z + gridspacing * 0.5f;
                float yaw = Math::RangeRandom(yawfrom, yawto);
                float scale = Math::RangeRandom(scalefrom, scaleto);
                Vector3 pos = Vector3(nx, 0, nz);
                treeLoader->addTree(curTree, pos, Degree(yaw), (Ogre::Real)scale);
                if (strlen(treeCollmesh))
                {
                    pos.y = terrainManager->getHeightAt(pos.x, pos.z);
                    scale *= 0.1f;
                    terrainManager->GetCollisions()->addCollisionMesh(curTree->getName(), String(treeCollmesh), pos, Quaternion(Degree(yaw), Vector3::UNIT_Y), Vector3(scale, scale, scale));
                }
            }
        }
    }
    else
    {
        float gridsize = 10;
        if (gridspacing < 0 && gridspacing != 0)
        {
            gridsize = -gridspacing;
        }
        float hd = highdens;
        // normal style, random
        for (float x=0; x < mapsizex; x += gridsize)
        {
            for (float z=0; z < mapsizez; z += gridsize)
            {
                if (highdens < 0) hd = Math::RangeRandom(0, -highdens);
                float density = densityMap->_getDensityAt_Unfiltered(x, z, bounds);
                int numTreesToPlace = (int)((float)(hd) * density * terrainManager->getPagedDetailFactor());
                float nx=0, nz=0;
                while(numTreesToPlace-->0)
                {
                    nx = Math::RangeRandom(x, x + gridsize);
                    nz = Math::RangeRandom(z, z + gridsize);
                    float yaw = Math::RangeRandom(yawfrom, yawto);
                    float scale = Math::RangeRandom(scalefrom, scaleto);
                    Vector3 pos = Vector3(nx, 0, nz);
                    treeLoader->addTree(curTree, pos, Degree(yaw), (Ogre::Real)scale);
                    if (strlen(treeCollmesh))
                    {
                        pos.y = terrainManager->getHeightAt(pos.x, pos.z);
                        terrainManager->GetCollisions()->addCollisionMesh(treemesh, String(treeCollmesh),pos, Quaternion(Degree(yaw), Vector3::UNIT_Y), Vector3(scale, scale, scale));
                    }
                }
            }
        }
    }
    m_paged_geometry.push_back(geom);
#endif //USE_PAGED
}

void TerrainObjectManager::ProcessGrass(
        float SwaySpeed, float SwayLength, float SwayDistribution, float Density,
        float minx, float miny, float minH, float maxx, float maxy, float maxH,
        char* grassmat, char* colorMapFilename, char* densityMapFilename,
        int growtechnique, int techn, int range,
        int mapsizex, int mapsizez)
{
#ifdef USE_PAGED
    //Initialize the PagedGeometry engine
    try
    {
        PagedGeometry *grass = new PagedGeometry(App::GetCameraManager()->GetCamera(), 30);
        //Set up LODs

        grass->addDetailLevel<GrassPage>(range * terrainManager->getPagedDetailFactor()); // original value: 80

        //Set up a GrassLoader for easy use
        GrassLoader *grassLoader = new GrassLoader(grass);
        grass->setPageLoader(grassLoader);
        grassLoader->setHeightFunction(&getTerrainHeight);

        // render grass at first
        grassLoader->setRenderQueueGroup(RENDER_QUEUE_MAIN-1);

        GrassLayer* grassLayer = grassLoader->addLayer(grassmat);
        grassLayer->setHeightRange(minH, maxH);
        grassLayer->setLightingEnabled(true);

        grassLayer->setAnimationEnabled((SwaySpeed>0));
        grassLayer->setSwaySpeed(SwaySpeed);
        grassLayer->setSwayLength(SwayLength);
        grassLayer->setSwayDistribution(SwayDistribution);

        grassLayer->setDensity(Density * terrainManager->getPagedDetailFactor());
        if (techn>10)
            grassLayer->setRenderTechnique(static_cast<GrassTechnique>(techn-10), true);
        else
            grassLayer->setRenderTechnique(static_cast<GrassTechnique>(techn), false);

        grassLayer->setMapBounds(TBounds(0, 0, mapsizex, mapsizez));

        if (strcmp(colorMapFilename,"none") != 0)
        {
            grassLayer->setColorMap(colorMapFilename);
            grassLayer->setColorMapFilter(MAPFILTER_BILINEAR);
        }

        if (strcmp(densityMapFilename,"none") != 0)
        {
            grassLayer->setDensityMap(densityMapFilename);
            grassLayer->setDensityMapFilter(MAPFILTER_BILINEAR);
        }

        grassLayer->setMinimumSize(minx, miny);
        grassLayer->setMaximumSize(maxx, maxy);

        // growtechnique
        if (growtechnique == 0)
            grassLayer->setFadeTechnique(FADETECH_GROW);
        else if (growtechnique == 1)
            grassLayer->setFadeTechnique(FADETECH_ALPHAGROW);
        else if (growtechnique == 2)
            grassLayer->setFadeTechnique(FADETECH_ALPHA);

        m_paged_geometry.push_back(grass);
    } 
    catch(...)
    {
        LOG("error loading grass!");
    }
#endif //USE_PAGED
}

void TerrainObjectManager::ProcessPredefinedActor(int tobj_cache_id, const std::string& name, const Ogre::Vector3 position, const Ogre::Vector3 rotation, const TObjSpecialObject type)
{
    // Transform TOBJ actor records to EditorObject-s (to be spawned later, if conditions are met).
    // NOTE: The filename may be in "Bundle-qualified" format, i.e. "mybundle.zip:myactor.truck"
    // -----------------------------------------------------------------------------------------
    
    TerrainEditorObjectPtr dst = new TerrainEditorObject();
    dst->position = position;
    dst->rotation = rotation;
    dst->special_object_type = type;
    dst->name = name;
    dst->tobj_cache_id = tobj_cache_id;
    m_editor_objects.push_back(dst);
    m_has_predefined_actors = true;
}

void TerrainObjectManager::moveObjectVisuals(const String& instancename, const Ogre::Vector3& pos)
{
    // Obsolete function kept for backwards-compatibility; does the same as `TerrainEditorObject::setPosition()`
    // -------------------------------------------------------------------------------------------------------

    TerrainEditorObjectID_t id = FindEditorObjectByInstanceName(instancename);
    if (id == TERRAINEDITOROBJECTID_INVALID)
    {
        LOG(fmt::format("[RoR] `moveObjectVisuals()`: instance name '{}' not found!", instancename));
        return;
    }

    m_editor_objects[id]->setPosition(pos);
}

void TerrainObjectManager::destroyObject(const String& instancename)
{
    TerrainEditorObjectID_t id = FindEditorObjectByInstanceName(instancename);
    if (id == -1)
    {
        LOG(fmt::format("[RoR] `destroyObject()`: instance name '{}' not found!", instancename));
        return;
    }

    TerrainEditorObjectPtr object = m_editor_objects[id];

    if (object->getSpecialObjectType() != TObjSpecialObject::NONE)
    {
        // Preloaded actor: despawn it.
        ROR_ASSERT(!object->static_object_node);
        ROR_ASSERT(!object->static_collision_tris.size());
        ROR_ASSERT(!object->static_collision_boxes.size());
        ActorPtr actor = App::GetGameContext()->GetActorManager()->GetActorById(object->actor_instance_id);
        if (actor)
        {
            App::GetGameContext()->PushMessage(Message(MSG_SIM_DELETE_ACTOR_REQUESTED, new ActorPtr(actor)));
        }
    }
    else
    {
        // Static object: Destroy the scene node and everything attached to it.
        ROR_ASSERT(object->static_object_node);
        this->UnregisterLocalLightsForOwner(object->static_object_node);
        for (Ogre::MovableObject* mova : object->static_object_node->getAttachedObjects())
        {
            App::GetGfxScene()->GetSceneManager()->destroyMovableObject(mova);
        }
        App::GetGfxScene()->GetSceneManager()->destroySceneNode(object->static_object_node);

        // Undo static collisions
        for (int tri : object->static_collision_tris)
        {
            terrainManager->GetCollisions()->removeCollisionTri(tri);
        }
        for (int box : object->static_collision_boxes)
        {
            terrainManager->GetCollisions()->removeCollisionBox(box);
        }
    }

    // Release the object from editor, if active.
    if (id == App::GetGameContext()->GetTerrain()->GetTerrainEditor()->GetSelectedObjectID())
    {
        App::GetGameContext()->GetTerrain()->GetTerrainEditor()->ClearSelectedObject();
    }

    // Forget the object ever existed.
    m_editor_objects.erase(m_editor_objects.begin() + id);
}

ODefDocument* TerrainObjectManager::FetchODef(std::string const & odef_name)
{
    // Consult cache first
    auto search_res = m_odef_cache.find(odef_name);
    if (search_res != m_odef_cache.end())
    {
        return search_res->second.get();
    }

    // Search for the file
    const std::string filename = odef_name + ".odef";
    std::string group_name;
    try
    {
        group_name = Ogre::ResourceGroupManager::getSingleton().findGroupContainingResource(filename);
    }
    catch (...) // This means "not found"
    {
        LOG(fmt::format("[ODEF] Could not find {} in any resource group", filename));
        return nullptr;
    }

    try
    {
        // Load and parse the file
        Ogre::DataStreamPtr ds = ResourceGroupManager::getSingleton().openResource(filename, group_name);
        ODefParser parser;
        parser.Prepare();
        parser.ProcessOgreStream(ds.get());
        std::shared_ptr<ODefDocument> odef = parser.Finalize();

        // Add to cache and return
        m_odef_cache.insert(std::make_pair(odef_name, odef));
        return odef.get();
    }
    catch (...)
    {
        LOG(fmt::format("[ODEF] An exception occurred when loading or parsing {}", filename));
        return nullptr;
    }
}

bool TerrainObjectManager::LoadTerrainObject(const Ogre::String& name, const Ogre::Vector3& pos, const Ogre::Vector3& rot, const Ogre::String& instancename, const Ogre::String& type, float rendering_distance /* = 0 */, bool enable_collisions /* = true */, int scripthandler /* = -1 */, bool uniquifyMaterial /* = false */)
{
    if (type == "grid")
    {
        // some fast grid object hacks :)
        for (int x = 0; x < 500; x += 50)
        {
            for (int z = 0; z < 500; z += 50)
            {
                const String notype = "";
                LoadTerrainObject(name, pos + Vector3(x, 0.0f, z), rot, name, notype, /*rendering_distance:*/0, enable_collisions, scripthandler, uniquifyMaterial);
            }
        }
        return true;
    }

    const std::string odefname = name + ".odef"; // for logging
    ODefDocument* odef = this->FetchODef(name);
    if (odef == nullptr)
    {
        // Only log to console if requested from Console UI or script (debug message to RoR.log is written anyway).
        if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
        {
            App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_TERRN, Console::CONSOLE_SYSTEM_ERROR,
                fmt::format(_L("Could not load file '{}'"), odefname));
        }
        return false;
    }

    SceneNode* tenode = this->getGroupingSceneNode()->createChildSceneNode();

    MeshObject* mo = nullptr;
    if (odef->header.mesh_name != "none")
    {
        Str<100> ebuf; ebuf << m_entity_counter++ << "-" << odef->header.mesh_name;
        mo = new MeshObject(odef->header.mesh_name, terrainManager->getTerrainFileResourceGroup(), ebuf.ToCStr(), tenode);
        if (mo->getEntity())
        {
            mo->getEntity()->setCastShadows(odef->header.cast_shadows);
            mo->getEntity()->setRenderingDistance(rendering_distance);
            m_mesh_objects.push_back(mo);
        }
        else
        {
            delete mo;
            // Only log to console if requested from Console UI or script (debug message to RoR.log is written anyway).
            if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
            {
                App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_TERRN, Console::CONSOLE_SYSTEM_WARNING,
                    fmt::format(_L("Could not load mesh '{}' (used by object '{}')"), odef->header.mesh_name, odefname));
            }
        }
    }

    tenode->setScale(odef->header.scale);
    tenode->setPosition(pos);
    Quaternion rotation = Quaternion(Degree(rot.x), Vector3::UNIT_X) * Quaternion(Degree(rot.y), Vector3::UNIT_Y) * Quaternion(Degree(rot.z), Vector3::UNIT_Z);
    tenode->rotate(rotation);
    tenode->pitch(Degree(-90));
    tenode->setVisible(true);

    TerrainEditorObjectPtr object = new TerrainEditorObject();
    object->name = name;
    object->instance_name = instancename;
    object->type = type;
    object->position = pos;
    object->rotation = rot;
    object->initial_position = pos;
    object->initial_rotation = rot;
    object->static_object_node = tenode;
    object->enable_collisions = enable_collisions;
    object->script_handler = scripthandler;
    object->tobj_cache_id = m_tobj_cache_active_id;
    m_editor_objects.push_back(object);

    if (mo && uniquifyMaterial && !instancename.empty())
    {
        for (unsigned int i = 0; i < mo->getEntity()->getNumSubEntities(); i++)
        {
            SubEntity* se = mo->getEntity()->getSubEntity(i);
            String matname = se->getMaterialName();
            String newmatname = matname + "/" + instancename;
            se->getMaterial()->clone(newmatname);
            se->setMaterialName(newmatname);
        }
    }

    for (LocalizerType type : odef->localizers)
    {
        Localizer loc;
        loc.position = Vector3(pos.x, pos.y, pos.z);
        loc.rotation = rotation;
        loc.type = type;
        m_localizers.push_back(loc);
    }

    if (odef->mode_standard)
    {
        tenode->pitch(Degree(90));
    }

#ifdef USE_OPENAL
    if (!App::GetSoundScriptManager()->isDisabled())
    {
        for (std::string& snd_name : odef->sounds)
        {
            SoundScriptInstancePtr sound = App::GetSoundScriptManager()->createInstance(snd_name, SoundScriptInstance::ACTOR_ID_TERRAIN_OBJECT);
            sound->setPosition(tenode->getPosition());
            sound->start();
        }
    }
#endif //USE_OPENAL

    for (std::string& gmodel_file: odef->groundmodel_files)
    {
        terrainManager->GetCollisions()->loadGroundModelsConfigFile(gmodel_file);
    }

    bool race_event = !object->instance_name.compare(0, 10, "checkpoint") ||
                        !object->instance_name.compare(0,  4, "race");

    if (race_event)
    {
        String type = "checkpoint";
        auto res = StringUtil::split(object->instance_name, "|");
        if ((res.size() == 4 && res[2] == "0") || !object->instance_name.compare(0, 4, "race"))
        {
            type = "racestart";
        }
        int race_id = res.size() > 1 ? StringConverter::parseInt(res[1], -1) : -1;
        m_map_entities.push_back(SurveyMapEntity(type, /*caption:*/type, fmt::format("icon_{}.dds", type), /*resource_group:*/"", object->position, Ogre::Radian(0), race_id));
    }
    else if (object->type != "" && object->type != "-")
    {
        String caption = "";
        if (object->type == "station" || object->type == "hotel" || object->type == "village" ||
                object->type == "observatory" || object->type == "farm" || object->type == "ship" || object->type == "sign")
        {
            caption = object->instance_name + " " + object->type;
        }
        m_map_entities.push_back(SurveyMapEntity(object->type, caption, fmt::format("icon_{}.dds", object->type), /*resource_group:*/"", object->position, Ogre::Radian(0), -1));
    }

    this->ProcessODefCollisionBoxes(object, odef, object, race_event);

    for (ODefCollisionMesh& cmesh : odef->collision_meshes)
    {
        if (cmesh.mesh_name == "")
        {
            LOG("[ODEF] Skipping collision mesh with empty name. Object: " + odefname);
            continue;
        }

        auto gm = terrainManager->GetCollisions()->getGroundModelByString(cmesh.groundmodel_name);
        terrainManager->GetCollisions()->addCollisionMesh(
            odefname,
            cmesh.mesh_name, pos, tenode->getOrientation(),
            cmesh.scale, gm, &(object->static_collision_tris));
    }

    for (ODefParticleSys& psys : odef->particle_systems)
    {

        // hacky: prevent duplicates
        String paname = String(psys.instance_name);
        while (App::GetGfxScene()->GetSceneManager()->hasParticleSystem(paname))
            paname += "_";

        // create particle system
        ParticleSystem* pParticleSys = App::GetGfxScene()->GetSceneManager()->createParticleSystem(paname, String(psys.template_name));
        pParticleSys->setCastShadows(false);
        pParticleSys->setVisibilityFlags(DEPTHMAP_DISABLED); // disable particles in depthmap

        // Some affectors may need its instance name (e.g. for script feedback purposes)
#ifdef USE_ANGELSCRIPT
        unsigned short affCount = pParticleSys->getNumAffectors();
        ParticleAffector* pAff;
        for (unsigned short i = 0; i < affCount; ++i)
        {
            pAff = pParticleSys->getAffector(i);
            if (pAff->getType() == "ExtinguishableFire")
            {
                ((ExtinguishableFireAffector*)pAff)->setInstanceName(object->instance_name);
            }
        }
#endif // USE_ANGELSCRIPT

        SceneNode* sn = tenode->createChildSceneNode();
        sn->attachObject(pParticleSys);
        sn->pitch(Degree(90));

        ParticleEffectObject peo;
        peo.node = sn;
        peo.psys = pParticleSys;
        m_particle_effect_objects.push_back(peo);
    }

    if (!odef->mat_name.empty())
    {
        if (mo->getEntity())
        {
            mo->getEntity()->setMaterialName(odef->mat_name);
        }
    }

    if (odef->mat_name_generate != "")
    {
        Ogre::MaterialPtr mat = Ogre::MaterialManager::getSingleton().create(odef->mat_name_generate,"generatedMaterialShaders");
        Ogre::RTShader::ShaderGenerator::getSingleton().createShaderBasedTechnique(*mat, Ogre::MaterialManager::DEFAULT_SCHEME_NAME, Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
        Ogre::RTShader::ShaderGenerator::getSingleton().invalidateMaterial(RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME, String(odef->mat_name_generate));
    }

    for (ODefAnimation& anim : odef->animations)
    {
        if (tenode && mo->getEntity())
        {
            AnimationStateSet *s = mo->getEntity()->getAllAnimationStates();
            String anim_name_str(anim.name);
            if (!s->hasAnimationState(anim_name_str))
            {
                LOG("[ODEF] animation '" + anim_name_str + "' for mesh: '" + odef->header.mesh_name + "' in odef file '" + name + ".odef' not found!");
                //continue;
            }
            AnimatedObject ao;
            ao.node = tenode;
            ao.ent = mo->getEntity();
            ao.speedfactor = anim.speed_min;
            if (anim.speed_min != anim.speed_max)
                ao.speedfactor = Math::RangeRandom(anim.speed_min, anim.speed_max);
            ao.anim = 0;
            try
            {
                ao.anim = mo->getEntity()->getAnimationState(anim_name_str);
            } catch (...)
            {
                ao.anim = 0;
            }
            if (!ao.anim)
            {
                LOG("[ODEF] animation '" + anim_name_str + "' for mesh: '" + odef->header.mesh_name + "' in odef file '" + name + ".odef' not found!");
                continue;
            }
            ao.anim->setEnabled(true);
            m_animated_objects.push_back(ao);
        }
    }

    for (ODefTexPrint& tex_print : odef->texture_prints)
    {
        if (!mo->getEntity())
            continue;
        String matName = mo->getEntity()->getSubEntity(0)->getMaterialName();
        MaterialPtr m = MaterialManager::getSingleton().getByName(matName);
        if (m.get() == 0)
        {
            LOG("[ODEF] problem with drawTextOnMeshTexture command: mesh material not found: "+odefname+" : "+matName);
            continue;
        }
        String texName = m->getTechnique(0)->getPass(0)->getTextureUnitState(0)->getTextureName();
        Texture* background = (Texture *)TextureManager::getSingleton().getByName(texName).get();
        if (!background)
        {
            LOG("[ODEF] problem with drawTextOnMeshTexture command: mesh texture not found: "+odefname+" : "+texName);
            continue;
        }

        static int textureNumber = 0;
        textureNumber++;
        char tmpTextName[256] = "", tmpMatName[256] = "";
        sprintf(tmpTextName, "TextOnTexture_%d_Texture", textureNumber);
        sprintf(tmpMatName, "TextOnTexture_%d_Material", textureNumber); // Make sure the texture is not WRITE_ONLY, we need to read the buffer to do the blending with the font (get the alpha for example)
        TexturePtr texture = TextureManager::getSingleton().createManual(tmpTextName, ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, TEX_TYPE_2D, (Ogre::uint)background->getWidth(), (Ogre::uint)background->getHeight(), MIP_UNLIMITED, PF_X8R8G8B8, Ogre::TU_STATIC | Ogre::TU_AUTOMIPMAP);
        if (texture.get() == 0)
        {
            LOG("[ODEF] problem with drawTextOnMeshTexture command: could not create texture: "+odefname+" : "+tmpTextName);
            continue;
        }

        Str<200> text_buf; text_buf << tex_print.text;

        // check if we got a template argument
        if (!strncmp(text_buf.GetBuffer(), "{{argument1}}", 13))
        {
            text_buf.Clear();
            text_buf << instancename;
        }

        // replace '_' with ' '
        char *text_pointer = text_buf.GetBuffer();
        while (*text_pointer!=0) {if (*text_pointer=='_') *text_pointer=' ';text_pointer++;};

        String font_name_str(tex_print.font_name);
        Ogre::Font* font = (Ogre::Font *)FontManager::getSingleton().getByName(font_name_str).get();
        if (!font)
        {
            LOG("[ODEF] problem with drawTextOnMeshTexture command: font not found: "+odefname+" : "+font_name_str);
            continue;
        }

        //Draw the background to the new texture
        texture->getBuffer()->blit(background->getBuffer());

        float x = background->getWidth()  * tex_print.x;
        float y = background->getHeight() * tex_print.y;
        float w = background->getWidth()  * tex_print.w;
        float h = background->getHeight() * tex_print.h;

        ColourValue color(tex_print.r, tex_print.g, tex_print.b, tex_print.a);
        Ogre::Box box = Ogre::Box((size_t)x, (size_t)y, (size_t)(x+w), (size_t)(y+h));
        WriteToTexture(text_buf.ToCStr(), texture, box, font, color, tex_print.font_size, tex_print.font_dpi, tex_print.option);

        // we can save it to disc for debug purposes:
        //SaveImage(texture, "test.png");

        m->clone(tmpMatName);
        MaterialPtr mNew = MaterialManager::getSingleton().getByName(tmpMatName);
        mNew->getTechnique(0)->getPass(0)->getTextureUnitState(0)->setTextureName(tmpTextName);

        mo->getEntity()->setMaterialName(String(tmpMatName));
    }

    size_t local_shadow_casters = 0;
    for (ODefSpotlight& spotl: odef->spotlights)
    {
        Light* spotLight = App::GetGfxScene()->GetSceneManager()->createLight();

        spotLight->setType(Light::LT_SPOTLIGHT);
        spotLight->setCastShadows(false);
        spotLight->setPosition(spotl.pos);
        spotLight->setDirection(spotl.dir);
        spotLight->setAttenuation(spotl.range, 1.0, 0.3, 0.0);
        spotLight->setDiffuseColour(spotl.color);
        spotLight->setSpecularColour(spotl.color);
        spotLight->setSpotlightRange(Degree(spotl.angle_inner), Degree(spotl.angle_outer));
        local_shadow_casters += spotLight->getCastShadows() ? 1 : 0;

        BillboardSet* lflare = App::GetGfxScene()->GetSceneManager()->createBillboardSet(1);
        lflare->createBillboard(spotl.pos, spotl.color);
        lflare->setMaterialName("tracks/flare");
        lflare->setVisibilityFlags(DEPTHMAP_DISABLED);

        float fsize = Math::Clamp(spotl.range / 10, 0.2f, 2.0f);
        lflare->setDefaultDimensions(fsize, fsize);

        SceneNode *sn = tenode->createChildSceneNode();
        sn->attachObject(spotLight);
        sn->attachObject(lflare);
        this->RegisterLocalLight(spotLight, lflare, sn, tenode);
    }

    for (ODefPointLight& plight : odef->point_lights)
    {
        Light* pointlight = App::GetGfxScene()->GetSceneManager()->createLight();

        pointlight->setType(Light::LT_POINT);
        pointlight->setCastShadows(false);
        pointlight->setPosition(plight.pos);
        pointlight->setDirection(plight.dir);
        pointlight->setAttenuation(plight.range, 1.0, 0.3, 0.0);
        pointlight->setDiffuseColour(plight.color);
        pointlight->setSpecularColour(plight.color);
        local_shadow_casters += pointlight->getCastShadows() ? 1 : 0;

        BillboardSet* lflare = App::GetGfxScene()->GetSceneManager()->createBillboardSet(1);
        lflare->createBillboard(plight.pos, plight.color);
        lflare->setMaterialName("tracks/flare");
        lflare->setVisibilityFlags(DEPTHMAP_DISABLED);

        float fsize = Math::Clamp(plight.range / 10, 0.2f, 2.0f);
        lflare->setDefaultDimensions(fsize, fsize);

        SceneNode *sn = tenode->createChildSceneNode();
        sn->attachObject(pointlight);
        sn->attachObject(lflare);
        this->RegisterLocalLight(pointlight, lflare, sn, tenode);
    }
    if (!odef->spotlights.empty() || !odef->point_lights.empty())
    {
        LOG(fmt::format(
            "[RoR|TerrainObject|Lights] odef={} spotlights={} "
            "point_lights={} local_shadow_casters={}",
            odefname,
            odef->spotlights.size(),
            odef->point_lights.size(),
            local_shadow_casters));
    }

    return true;
}

bool TerrainObjectManager::LoadTerrainScript(const Ogre::String& filename)
{
    ROR_ASSERT(!m_angelscript_grouping_node);

    m_angelscript_grouping_node = m_terrn2_grouping_node->createChildSceneNode(filename);
    ScriptUnitID_t result = App::GetScriptEngine()->loadScript(filename);
    m_angelscript_grouping_node = nullptr;
    
    return result != SCRIPTUNITID_INVALID;
}

void TerrainObjectManager::UpdateAnimatedObjects(float dt)
{
    if (m_animated_objects.size() == 0)
        return;

    std::vector<AnimatedObject>::iterator it;

    for (it = m_animated_objects.begin(); it != m_animated_objects.end(); it++)
    {
        if (it->anim && it->speedfactor != 0)
        {
            Real time = dt * it->speedfactor;
            it->anim->addTime(time);
        }
    }
}

void TerrainObjectManager::UpdateParticleEffectObjects()
{
    for (ParticleEffectObject& peo : m_particle_effect_objects)
    {
        if (peo.psys)
        {
            App::GetGfxScene()->AdjustParticleSystemTimeFactor(peo.psys);
        }
    }
}

void TerrainObjectManager::LoadTelepoints()
{
    for (Terrn2Telepoint& telepoint: terrainManager->GetDef()->telepoints)
    {
        m_map_entities.push_back(SurveyMapEntity("telepoint", telepoint.name, "icon_telepoint.dds", /*resource_group:*/"", telepoint.position, Ogre::Radian(0), -1));
    }
}

bool TerrainObjectManager::GetEditorObjectFlagRotYXZ(TerrainEditorObjectPtr const& object)
{
    // We need the 'rot_yxz' flag - look up the TOBJ document in cache
    if (object->tobj_cache_id == -1 || object->tobj_cache_id >= (int)m_tobj_cache.size())
    {
        App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_TERRN, Console::CONSOLE_SYSTEM_WARNING,
            fmt::format("Assuming no 'rot_yxz' when spawning preselected actor '{}' - TOBJ document not found", object->getName()));
        return false;
    }
    else
    {
        return m_tobj_cache[object->tobj_cache_id]->rot_yxz;
    }
}


void TerrainObjectManager::SpawnSinglePredefinedActor(TerrainEditorObjectPtr const& object)
{
    // For terrain editor to work, all preloaded actors must be spawned.
    // Most will spawn with terrain, however, some may be excluded for reasons.
    // -----------------------------------------------------------------------

    const bool rot_yxz = GetEditorObjectFlagRotYXZ(object);

    // Check if already spawned.
    if (object->actor_instance_id == ACTORINSTANCEID_INVALID)
    {
        // Not spawned yet - assign custom ID so that Terrain Editor can reset and move the actor.
        object->actor_instance_id = App::GetGameContext()->GetActorManager()->GetActorNextInstanceId();
    }
    else
    {
        // Spawned before; check if still existing and respawn if not.
        const ActorPtr& actor = App::GetGameContext()->GetActorManager()->GetActorById(
            object->actor_instance_id);
        if (actor != ActorManager::ACTORPTR_NULL)
        {
            return; // We're done.
        }
    }

    ActorSpawnRequest* rq = new ActorSpawnRequest;
    rq->asr_instance_id = object->actor_instance_id;
    rq->asr_position = object->position;
    rq->asr_filename = object->name;
    rq->asr_rotation = TObjParser::CalcRotation(object->rotation, rot_yxz);
    rq->asr_origin = ActorSpawnRequest::Origin::TERRN_DEF;
    rq->asr_free_position = (object->special_object_type == TObjSpecialObject::TRUCK2);
    rq->asr_terrn_machine = (object->special_object_type == TObjSpecialObject::MACHINE);
    App::GetGameContext()->PushMessage(Message(MSG_SIM_SPAWN_ACTOR_REQUESTED, (void*)rq));
}

void TerrainObjectManager::LoadPredefinedActors()
{
    // in netmode, don't load other actors!
    if (RoR::App::mp_state->getEnum<MpState>() == RoR::MpState::CONNECTED)
    {
        return;
    }

    for (TerrainEditorObjectPtr object : m_editor_objects)
    {
        if (object->special_object_type == TObjSpecialObject::NONE)
        {
            continue; // Skip static objects
        }

        if ((object->special_object_type == TObjSpecialObject::BOAT) && (terrainManager->getWater() == nullptr))
        {
            continue; // Don't spawn boats if there's no water.
        }

        this->SpawnSinglePredefinedActor(object);
    }
}

void TerrainObjectManager::RegisterLocalLight(
    Ogre::Light* light,
    Ogre::BillboardSet* flare,
    Ogre::SceneNode* light_node,
    Ogre::SceneNode* owner_node)
{
    ROR_ASSERT(light);
    ROR_ASSERT(flare);
    ROR_ASSERT(light_node);
    ROR_ASSERT(owner_node);

    RegisteredLocalLight registered;
    registered.light = light;
    registered.flare = flare;
    registered.light_node = light_node;
    registered.owner_node = owner_node;
    registered.stable_id = m_next_local_light_id++;
    registered.active =
        m_registered_local_lights.size() < LOCAL_LIGHT_ACTIVE_BUDGET;

    // This registration path is shared by every terrain-local light source.
    // Enforce the no-shadow policy here as well as at parser call sites so
    // future world-space TOBJ lights cannot accidentally consume shadow maps.
    light->setCastShadows(false);
    light->setVisible(registered.active);
    flare->setVisible(registered.active);

    m_registered_local_lights.push_back(registered);
    m_local_light_candidates.resize(m_registered_local_lights.size());
    m_local_light_rank_scratch.resize(m_registered_local_lights.size());
    m_local_light_selection.resize(m_registered_local_lights.size());
}

void TerrainObjectManager::DestroyAllRegisteredLocalLights()
{
    Ogre::SceneManager* scene_manager =
        App::GetGfxScene()->GetSceneManager();
    for (RegisteredLocalLight& registered : m_registered_local_lights)
    {
        scene_manager->destroyMovableObject(registered.light);
        scene_manager->destroyMovableObject(registered.flare);
        scene_manager->destroySceneNode(registered.light_node);
    }
    m_registered_local_lights.clear();
    m_local_light_candidates.clear();
    m_local_light_rank_scratch.clear();
    m_local_light_selection.clear();
}

void TerrainObjectManager::UnregisterLocalLightsForOwner(
    Ogre::SceneNode* owner_node)
{
    std::size_t destination = 0;
    for (std::size_t source = 0;
         source < m_registered_local_lights.size();
         ++source)
    {
        if (m_registered_local_lights[source].owner_node == owner_node)
        {
            RegisteredLocalLight& registered =
                m_registered_local_lights[source];
            Ogre::SceneManager* scene_manager =
                App::GetGfxScene()->GetSceneManager();
            scene_manager->destroyMovableObject(registered.light);
            scene_manager->destroyMovableObject(registered.flare);
            scene_manager->destroySceneNode(registered.light_node);
            continue;
        }
        if (destination != source)
        {
            m_registered_local_lights[destination] =
                m_registered_local_lights[source];
        }
        ++destination;
    }

    if (destination == m_registered_local_lights.size())
    {
        return;
    }

    m_registered_local_lights.resize(destination);
    m_local_light_candidates.resize(destination);
    m_local_light_rank_scratch.resize(destination);
    m_local_light_selection.resize(destination);
}

void TerrainObjectManager::UpdateLocalLightBudget()
{
    const std::size_t discovered = m_registered_local_lights.size();
    if (discovered == 0)
    {
        if (m_last_logged_local_light_discovered !=
                static_cast<std::size_t>(-1) &&
            m_last_logged_local_light_discovered != 0)
        {
            LOG(fmt::format(
                "{} discovered=0 active=0 budget={}",
                LOCAL_LIGHT_BUDGET_LOG_MARKER,
                LOCAL_LIGHT_ACTIVE_BUDGET));
        }
        m_last_logged_local_light_discovered = 0;
        m_last_logged_local_light_active = 0;
        return;
    }

    CameraManager* camera_manager = App::GetCameraManager();
    Ogre::Camera* camera =
        camera_manager != nullptr ? camera_manager->GetCamera() : nullptr;

    LocalLightPosition camera_position;
    if (camera != nullptr)
    {
        const Ogre::Vector3& position = camera->getDerivedPosition();
        camera_position = {
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z)};
    }
    else
    {
        // A non-finite camera makes the pure selector fail closed.
        std::uint64_t bits = UINT64_C(0x7ff8000000000001);
        std::memcpy(&camera_position.x, &bits, sizeof(bits));
    }

    for (std::size_t index = 0; index < discovered; ++index)
    {
        RegisteredLocalLight& registered = m_registered_local_lights[index];
        LocalLightCandidate& candidate = m_local_light_candidates[index];
        candidate.stable_id = registered.stable_id;
        if (registered.light != nullptr)
        {
            const Ogre::Vector3& position =
                registered.light->getDerivedPosition();
            candidate.position = {
                static_cast<double>(position.x),
                static_cast<double>(position.y),
                static_cast<double>(position.z)};
        }
    }

    const std::size_t active = SelectLocalLights(
        m_local_light_candidates.data(),
        m_local_light_candidates.size(),
        camera_position,
        LOCAL_LIGHT_ACTIVE_BUDGET,
        m_local_light_selection.data(),
        m_local_light_rank_scratch.data());

    for (std::size_t index = 0; index < discovered; ++index)
    {
        RegisteredLocalLight& registered = m_registered_local_lights[index];
        const bool should_be_active = m_local_light_selection[index] != 0;
        if (registered.active == should_be_active)
        {
            continue;
        }
        registered.active = should_be_active;
        registered.light->setVisible(should_be_active);
        registered.flare->setVisible(should_be_active);
    }

    if (m_last_logged_local_light_discovered != discovered ||
        m_last_logged_local_light_active != active)
    {
        LOG(fmt::format(
            "{} discovered={} active={} budget={}",
            LOCAL_LIGHT_BUDGET_LOG_MARKER,
            discovered,
            active,
            LOCAL_LIGHT_ACTIVE_BUDGET));
        m_last_logged_local_light_discovered = discovered;
        m_last_logged_local_light_active = active;
    }
}

bool TerrainObjectManager::UpdateTerrainObjects(float dt)
{
#ifdef USE_PAGED
    for (auto geom : m_paged_geometry)
    {
        geom->update();
    }
#endif //USE_PAGED
    this->UpdateAnimatedObjects(dt);
    this->UpdateParticleEffectObjects();
    this->UpdateLocalLightBudget();

    return true;
}

bool TerrainObjectManager::HasTimeVaryingVisuals() const
{
    if (!m_animated_objects.empty() ||
        !m_particle_effect_objects.empty())
    {
        return true;
    }
#ifdef USE_PAGED
    if (!m_paged_geometry.empty())
        return true;
#endif
    return false;
}

void TerrainObjectManager::ProcessODefCollisionBoxes(TerrainEditorObjectPtr obj, ODefDocument* odef, const TerrainEditorObjectPtr& params, bool race_event)
{
    if (race_event && !App::sim_races_enabled->getBool())
        m_worldmodel_collision_profile_canonical = false;

    for (ODefCollisionBox& cbox : odef->collision_boxes)
    {
        if (params->enable_collisions && (App::sim_races_enabled->getBool() || !race_event))
        {
            // Validate AABB (minimum corners must be less or equal to maximum corners)
            if (cbox.aabb_min.x > cbox.aabb_max.x || cbox.aabb_min.y > cbox.aabb_max.y || cbox.aabb_min.z > cbox.aabb_max.z)
            {
                // Only log to console if invoked from Console UI or script.
                std::string msg = "Skipping invalid collision box, min: " + TOSTRING(cbox.aabb_min) + ", max: " + TOSTRING(cbox.aabb_max);
                if (App::app_state->getEnum<AppState>() == AppState::SIMULATION)
                {
                    App::GetConsole()->putMessage(Console::CONSOLE_MSGTYPE_TERRN, Console::CONSOLE_SYSTEM_WARNING, msg);
                }
                else
                {
                    LOG(fmt::format("[ODEF] {}", msg));
                }
                continue;
            }

            int boxnum = terrainManager->GetCollisions()->addCollisionBox(
                cbox.is_rotating, cbox.is_virtual, params->position, params->rotation,
                cbox.aabb_min, cbox.aabb_max, cbox.box_rot, cbox.event_name,
                params->instance_name, cbox.reverb_preset_name, cbox.force_cam_pos,
                cbox.cam_pos, cbox.scale, cbox.direction, cbox.event_filter,
                params->script_handler);

            obj->static_collision_boxes.push_back(boxnum);
        }
    }
}

Ogre::SceneNode* TerrainObjectManager::getGroupingSceneNode()
{
    // This has no effect on rendering, it just helps users to diagnose the scene graph.
    // --------------------------------------------------------------------------------

    if (m_angelscript_grouping_node)
        return m_angelscript_grouping_node;
    else if (m_tobj_grouping_node)
        return m_tobj_grouping_node;
    else if (m_terrn2_grouping_node)
        return m_terrn2_grouping_node;
    else
        return App::GetGfxScene()->GetSceneManager()->getRootSceneNode();
}

TerrainEditorObjectID_t TerrainObjectManager::FindEditorObjectByInstanceName(std::string const& needle_instance_name)
{
    // Is this the right 'ModernC++' approach? :/
    auto itor = std::find_if(m_editor_objects.begin(), m_editor_objects.end(),
        [needle_instance_name](TerrainEditorObjectPtr& obj) { return obj->instance_name == needle_instance_name; });
    if (itor != m_editor_objects.end())
    {
        return static_cast<int>(std::distance(m_editor_objects.begin(), itor));
    }
    else
    {
        return TERRAINEDITOROBJECTID_INVALID;
    }
}

