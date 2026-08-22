/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer

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
/// @author Thomas Fischer (thomas{AT}thomasfischer{DOT}biz)
/// @date   1st of May 2010

#include "MeshObject.h"

#include "Actor.h"
#include "Application.h"
#include "GfxScene.h"
#include "Terrain.h"

#include <OgreDistanceLodStrategy.h>
#include <OgreMeshLodGenerator.h>
#include <OgreLodConfig.h>

using namespace Ogre;
using namespace RoR;

MeshObject::MeshObject(Ogre::String meshName, Ogre::String entityRG, Ogre::String entityName, Ogre::SceneNode *m_scene_node)
    : m_scene_node(m_scene_node), m_entity(nullptr), m_cast_shadows(true)
{
    this->createEntity(meshName, entityRG, entityName);
}

void MeshObject::setMaterialName(Ogre::String m)
{
    if (m_entity)
    {
        m_entity->setMaterialName(m);
    }
}

void MeshObject::setCastShadows(bool b)
{
    m_cast_shadows = b;
    if (m_scene_node && m_scene_node->numAttachedObjects())
    {
        m_scene_node->getAttachedObject(0)->setCastShadows(b);
    }
}

void MeshObject::setVisible(bool b)
{
    // Workaround: if the scenenode is not used (entity not attached) for some reason, try hiding the entity directly.
    if (m_scene_node && m_scene_node->getAttachedObjects().size() > 0)
        m_scene_node->setVisible(b);
    else if (m_entity)
        m_entity->setVisible(b);
}

void MeshObject::createEntity(Ogre::String meshName, Ogre::String entityRG, Ogre::String entityName)
{
    if (!m_scene_node)
        return;

    try
    {
        m_mesh = Ogre::MeshManager::getSingleton().getByName(meshName, entityRG);

        // Resource-group initialization may have already created or loaded the
        // Mesh resource before the first MeshObject reaches it.  LOD setup is
        // a property of the mesh, not of how it was first discovered, so do
        // not hide it behind the null-resource path.
        if (m_mesh == nullptr)
        {
            m_mesh = Ogre::MeshManager::getSingleton().load(meshName, entityRG);
        }
        else if (!m_mesh->isLoaded())
        {
            m_mesh->load();
        }

        // Generate a ladder exactly once, before this entity is created.  A
        // preloaded mesh with only its base level is the normal path in the
        // combined Ogre-Next runtime; previously it silently stayed at full
        // detail forever because only newly registered resources entered this
        // block.
        if (m_mesh->getNumLodLevels() <= 1U)
        {

#if OGRE_VERSION_MAJOR >= 14
            // RoR exposes only texture shadows (PSSM) or no shadows, so legacy
            // stencil-shadow edge lists are unused. Some large v1.40 meshes
            // contain a serialized base edge list; OGRE's LOD generator sees
            // it, frees it, then attempts to rebuild edge data for every
            // generated LOD. The rebuild can read outside the generated
            // vertex buffers (CityWorld's 203k-vertex section B is a
            // reproducible example). Discarding the unused imported edge list
            // before LOD generation prevents that unsafe rebuild while
            // preserving both authored and generated mesh LOD levels.
            m_mesh->freeEdgeList();
            m_mesh->setAutoBuildEdgeLists(false);
#endif

            // important: you need to add the LODs before creating the entity
            // now find possible LODs, needs to be done before calling createEntity()
            String basename, ext;
            StringUtil::splitBaseFilename(meshName, basename, ext);

            // An authored ladder may name its most detailed level explicitly,
            // so the placed mesh is already "<stem>_lod0.mesh". Searching for
            // siblings of that full name looks for "<stem>_lod0_lod*.mesh" and
            // matches nothing, which silently discards the authored reductions
            // and leaves full detail at every range. Search the family stem
            // instead, and skip level zero below because it is this mesh.
            const String lod_zero_suffix = "_lod0";
            const bool base_mesh_is_lod_zero =
                basename.size() > lod_zero_suffix.size() &&
                basename.compare(basename.size() - lod_zero_suffix.size(),
                                 lod_zero_suffix.size(), lod_zero_suffix) == 0;
            const String lod_family_stem =
                base_mesh_is_lod_zero
                    ? basename.substr(0, basename.size() - lod_zero_suffix.size())
                    : basename;

            bool lod_available = false;
            Ogre::LodConfig config(
                m_mesh, Ogre::DistanceLodSphereStrategy::getSingletonPtr());

            // the classic LODs
            FileInfoListPtr files = ResourceGroupManager::getSingleton().findResourceFileInfo(entityRG, lod_family_stem + "_lod*.mesh");
            for (FileInfoList::iterator iterFiles = files->begin(); iterFiles != files->end(); ++iterFiles)
            {
                String format = lod_family_stem + "_lod%d.mesh";
                int i = -1;
                int r = sscanf(iterFiles->filename.c_str(), format.c_str(), &i);
                // Level zero is the placed mesh itself, never a reduction.
                if (r > 0 && i == 0)
                    continue;

                if (r <= 0 || i < 0)
                    continue;

                float distance = 3;

                // we need to tune this according to our sightrange
                if (App::gfx_sight_range->getInt() > Terrain::UNLIMITED_SIGHTRANGE)
                {
                    // unlimited
                    if (i == 1)
                        distance = 200;
                    else if (i == 2)
                        distance = 600;
                    else if (i == 3)
                        distance = 2000;
                    else if (i == 4)
                        distance = 5000;
                }
                else
                {
                    // limited
                    int sightrange = App::gfx_sight_range->getInt();
                    if (i == 1)
                        distance = std::max(20.0f, sightrange * 0.1f);
                    else if (i == 2)
                        distance = std::max(20.0f, sightrange * 0.2f);
                    else if (i == 3)
                        distance = std::max(20.0f, sightrange * 0.3f);
                    else if (i == 4)
                        distance = std::max(20.0f, sightrange * 0.4f);
                }
                config.createManualLodLevel(distance, iterFiles->filename);
                lod_available = true;
            }

            // the custom LODs
            FileInfoListPtr files2 = ResourceGroupManager::getSingleton().findResourceFileInfo(entityRG, lod_family_stem + "_clod_*.mesh");
            for (FileInfoList::iterator iterFiles = files2->begin(); iterFiles != files2->end(); ++iterFiles)
            {
                // and custom LODs
                String format = lod_family_stem + "_clod_%d.mesh";
                int i = -1;
                int r = sscanf(iterFiles->filename.c_str(), format.c_str(), &i);
                if (r <= 0 || i < 0)
                    continue;

                config.createManualLodLevel(i, iterFiles->filename);
                lod_available = true;
            }

            if (lod_available)
                Ogre::MeshLodGenerator::getSingleton().generateLodLevels(config);
            else if (App::gfx_auto_lod->getBool())
            {
                // OGRE's generic autoconfig chooses the camera-dependent
                // pixel_count strategy.  The Ogre-Next scene contract carries
                // exact world-space activation distances so selection stays
                // deterministic across renderer APIs and resolutions.  Reuse
                // autoconfig's reviewed collapse-cost reductions, but bind
                // them to an explicit distance-sphere ladder before baking.
                Ogre::LodConfig automatic_config;
                Ogre::MeshLodGenerator::getSingleton().getAutoconfig(
                    m_mesh, automatic_config);
                automatic_config.strategy =
                    Ogre::DistanceLodSphereStrategy::getSingletonPtr();
                const bool unlimited_sight =
                    App::gfx_sight_range->getInt() >
                    Terrain::UNLIMITED_SIGHTRANGE;
                const float sight_range = static_cast<float>(
                    App::gfx_sight_range->getInt());
                const float unlimited_distances[] = {
                    200.0F, 600.0F, 2000.0F, 5000.0F};
                const float limited_fractions[] = {
                    0.1F, 0.2F, 0.3F, 0.4F};
                const std::size_t distance_count =
                    sizeof(unlimited_distances) /
                    sizeof(unlimited_distances[0]);
                if (automatic_config.levels.size() > distance_count)
                    automatic_config.levels.resize(distance_count);
                for (std::size_t level_index = 0U;
                     level_index < automatic_config.levels.size();
                     ++level_index)
                {
                    automatic_config.levels[level_index].distance =
                        unlimited_sight
                            ? unlimited_distances[level_index]
                            : std::max(
                                  20.0F,
                                  sight_range * limited_fractions[level_index]);
                }
                Ogre::MeshLodGenerator::getSingleton().generateLodLevels(
                    automatic_config);
            }

        }

        // now create an entity around the mesh and attach it to the scene graph
        m_entity = App::GetGfxScene()->GetSceneManager()->createEntity(entityName, meshName, entityRG);
        m_entity->setCastShadows(m_cast_shadows);

        m_scene_node->attachObject(m_entity);
        m_scene_node->setVisible(true);
    }
    catch (Ogre::Exception &e)
    {
        RoR::LogFormat("[RoR] Error creating entity of mesh '%s' (group: '%s'), message: %s",
                       meshName.c_str(), entityRG.c_str(), e.getFullDescription().c_str());
        return;
    }
}
