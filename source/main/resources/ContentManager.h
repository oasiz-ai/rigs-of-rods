/*
    This source file is part of Rigs of Rods
    Copyright 2005-2012 Pierre-Michel Ricordel
    Copyright 2007-2012 Thomas Fischer
    Copyright 2013-2018 Petr Ohlidal

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

#pragma once

#include "CacheSystem.h"
#include "Application.h"

#include <OgreResourceGroupManager.h>
#include <OgreMeshSerializer.h>
#include <OgreScriptCompiler.h>
#include <rapidjson/document.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace RoR {

class ContentManager:
    public Ogre::ResourceLoadingListener, // Ogre::ResourceGroupManager::getSingleton().setLoadingListener()
    public Ogre::ScriptCompilerListener,  // Ogre::ScriptCompilerManager::getSingleton().setListener()
    private Ogre::MeshSerializerListener,
    private Ogre::ResourceGroupListener
{
public:

    ContentManager();
    ~ContentManager() override;

    struct ResourcePack
    {
        ResourcePack(const char* name, const char* resource_group_name):
            name(name), resource_group_name(resource_group_name)
        {}

        static const ResourcePack OGRE_CORE;
        static const ResourcePack WALLPAPERS;
        static const ResourcePack AIRFOILS;
        static const ResourcePack BEAM_OBJECTS;
        static const ResourcePack CAELUM;
        static const ResourcePack CUBEMAPS;
        static const ResourcePack DASHBOARDS;
        static const ResourcePack FAMICONS;
        static const ResourcePack FLAGS;
        static const ResourcePack FONTS;
        static const ResourcePack HYDRAX;
        static const ResourcePack ICONS;
        static const ResourcePack MATERIALS;
        static const ResourcePack MESHES;
        static const ResourcePack MYGUI;
        static const ResourcePack OVERLAYS;
        static const ResourcePack PAGED;
        static const ResourcePack PARTICLES;
        static const ResourcePack PSSM;
        static const ResourcePack SKYX;
        static const ResourcePack RTSHADER;
        static const ResourcePack SCRIPTS;
        static const ResourcePack SOUNDS;
        static const ResourcePack TEXTURES;

        const char* name;
        const char* resource_group_name;
    };

                       /// Loads resources if not already loaded
                       /// @param override_rg If not set, the ResourcePack's RG is used -> resources won't unload until shutdown
    void               AddResourcePack(ResourcePack const& resource_pack, std::string const& override_rgn = "");
    void               InitManagedMaterials(std::string const & rg_name);
    void               RegisterPackageResourceLocation(const Ogre::String& resource_group, const Ogre::String& archive_name);
    void               RegisterAuthenticatedPackageResourceLocation(
                           const Ogre::String& resource_group,
                           const Ogre::String& archive_name,
                           const std::string& archive_sha256);
    void               UnregisterPackageResourceGroup(const Ogre::String& resource_group);
    void               InitContentManager();
    void               InitModCache(CacheValidity validity);
    void               LoadGameplayResources();  //!< Checks GVar settings and loads required resources.
    std::string        ListAllUserContent(); //!< Used by ModCache for quick detection of added/removed content
    bool               DeleteDiskFile(std::string const& filename, std::string const& rg_name);

    // JSON:
    bool               LoadAndParseJson(std::string const& filename, std::string const& rg_name, rapidjson::Document& j_doc);
    bool               SerializeAndWriteJson(std::string const& filename, std::string const& rg_name, rapidjson::Document& j_doc);

private:

    void EnsureResourceGroupListener();
    void ApplyShaderCompatibilityFallbacks(const Ogre::String& resource_group);

    // Ogre::ResourceGroupListener
    void resourceGroupScriptingStarted(const Ogre::String& group_name, size_t script_count) override;
    void scriptParseStarted(const Ogre::String& script_name, bool& skip_this_script) override;
    void scriptParseEnded(const Ogre::String& script_name, bool skipped) override;
    void resourceGroupScriptingEnded(const Ogre::String& group_name) override;
    void resourceRemove(const Ogre::ResourcePtr& resource) override;

    // Ogre::MeshSerializerListener
    void processMaterialName(Ogre::Mesh* mesh, Ogre::String* name) override;
    void processSkeletonName(Ogre::Mesh* mesh, Ogre::String* name) override;
    void processMeshCompleted(Ogre::Mesh* mesh) override;

    // Ogre::ResourceLoadingListener
    Ogre::DataStreamPtr resourceLoading(const Ogre::String& name, const Ogre::String& group, Ogre::Resource* resource) override;
    void resourceStreamOpened(const Ogre::String& name, const Ogre::String& group, Ogre::Resource* resource, Ogre::DataStreamPtr& dataStream) override;
    bool resourceCollision(Ogre::Resource* resource, Ogre::ResourceManager* resourceManager) override;

    // Ogre::ScriptCompilerListener
    bool handleEvent(Ogre::ScriptCompiler *compiler, Ogre::ScriptCompilerEvent *evt, void *retval) override;

    struct AuthenticatedMeshBinding
    {
        Ogre::String group;
        Ogre::String mesh_group;
        Ogre::String name;
        Ogre::ResourceHandle handle = 0U;
        std::size_t state_count = 0U;
        std::uint64_t group_generation = 0U;
        std::string archive_sha256;
    };

    void EraseAuthenticatedMeshBindingsForGroupLocked(
        const Ogre::String& resource_group);
    std::uint64_t AdvanceLegacyMaterialGroupGenerationLocked(
        const Ogre::String& resource_group);

    bool              m_base_resource_loaded;
    bool              m_resource_group_listener_registered;
    bool              m_mesh_serializer_listener_registered = false;
    std::mutex        m_legacy_material_archive_io_mutex;
    std::mutex        m_legacy_material_state_mutex;
    std::mutex        m_legacy_material_resolution_mutex;
    std::uint64_t     m_next_legacy_material_group_generation = 0U;
    std::unordered_map<Ogre::String, std::uint64_t>
        m_legacy_material_group_generations;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_package_archives_by_group;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<Ogre::String, std::string>>
        m_authenticated_package_archives_by_group;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_package_materials_by_group;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<std::string, std::unordered_set<Ogre::String>>>
        m_authenticated_materials_by_group;
    std::unordered_map<const Ogre::Resource*, AuthenticatedMeshBinding>
        m_authenticated_mesh_bindings;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<
            std::string,
            std::unordered_map<Ogre::String, Ogre::String>>>
        m_generated_material_fallbacks_by_group;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_generated_material_names_by_group;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_reported_material_resolutions_by_group;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<Ogre::String, std::string>>
        m_authorized_texture_fallbacks_by_group;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_reported_texture_fallbacks_by_group;
    Ogre::String      m_scripting_resource_group;
    Ogre::String      m_current_script_name;
    bool              m_current_script_package_owned = false;
    std::string       m_current_script_authenticated_sha256;
};

} // namespace RoR
