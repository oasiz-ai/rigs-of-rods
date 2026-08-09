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
#include "TerrainBundleArchiveVerifier.h"

#include <OgreResourceGroupManager.h>
#include <OgreMeshSerializer.h>
#include <OgreScriptCompiler.h>
#include <rapidjson/document.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#if OGRE_VERSION_MAJOR >= 14
#include "gfx/ogre14/Ogre14AuthenticatedArchiveLocationClosure.h"
#include "gfx/ogre14/Ogre14AuthenticatedResourceThreadGate.h"
#include "gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceipt.h"
#include "gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h"
#endif

namespace RoR {

#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
class ContentManagerNativeIntegrationTestAccess;
#endif

class ContentManager:
    public Ogre::ResourceLoadingListener, // Ogre::ResourceGroupManager::getSingleton().setLoadingListener()
    public Ogre::ScriptCompilerListener,  // Ogre::ScriptCompilerManager::getSingleton().setListener()
    private Ogre::MeshSerializerListener,
    private Ogre::ResourceGroupListener
#if OGRE_VERSION_MAJOR >= 14
    , public Render::IOgre14AuthenticatedTextureResolver
    , public Render::IOgre14AuthenticatedTextureAuthorityProvider
    , public Render::IOgre14AuthenticatedMaterialScriptResolver
#endif
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
        static const ResourcePack POSTPROCESS;
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
    /// A successful authenticated mount binds all authenticated package,
    /// script, material, texture, and resolver lifecycle callbacks to the same
    /// process-lifetime OGRE resource/render thread. Invalid pure-input mount
    /// attempts do not bind it. Crossing that boundary fails before taking a
    /// ContentManager or OGRE manager lock; background resource loading must be
    /// quiesced while authenticated package authority is active.
    void               MountAuthenticatedPackageResourceLocation(
                           const Ogre::String& resource_group,
                           const TerrainBundleAuthenticatedArchiveSnapshot&
                               archive_snapshot
#if OGRE_VERSION_MAJOR >= 14
                           , Render::IOgre14AuthenticatedArchiveMountFaultInjector*
                                 fault_injector = nullptr
#endif
                           );
    bool               IsAuthenticatedPackageSourceMounted(
                           const Ogre::String& resource_group,
                           const Ogre::String& source_archive_identity);
    void               UnregisterPackageResourceGroup(const Ogre::String& resource_group);
    void               InitContentManager();
    void               InitModCache(CacheValidity validity);
    void               LoadGameplayResources();  //!< Checks GVar settings and loads required resources.
    std::string        ListAllUserContent(); //!< Used by ModCache for quick detection of added/removed content
    bool               DeleteDiskFile(std::string const& filename, std::string const& rg_name);

#if OGRE_VERSION_MAJOR >= 14
    /// Exact source authority for the already-loaded Texture captured by the
    /// OGRE 14 native material extractor. Both calls must run on OGRE's
    /// serialized resource/render thread; only the registry access itself is
    /// synchronized for concurrent callers.
    [[nodiscard]] Render::ValidationResult ResolveAuthenticatedTexture(
        Ogre::Texture& texture,
        Render::Ogre14AuthenticatedTextureResolution& resolution) const
        override;
    [[nodiscard]] bool RevalidateAuthenticatedTexture(
        Ogre::Texture& texture,
        const Render::Ogre14AuthenticatedTextureResolution& resolution) const
        noexcept override;
    [[nodiscard]] Render::ValidationResult
    CaptureAuthenticatedTextureAuthoritySnapshot(
        Render::Ogre14AuthenticatedTextureAuthoritySnapshot& snapshot) const
        override;
    [[nodiscard]] Render::ValidationResult
    ResolveAuthenticatedMaterialScript(
        Ogre::Material& material,
        Render::Ogre14AuthenticatedMaterialScriptResolution& resolution) const
        override;
    [[nodiscard]] bool RevalidateAuthenticatedMaterialScript(
        Ogre::Material& material,
        const Render::Ogre14AuthenticatedMaterialScriptResolution& resolution)
        const noexcept override;
    /// Discards an in-flight whole-group source/material transaction before
    /// ResourceGroupManager tears down a parse which exited by exception.
    void AbortAuthenticatedMaterialScriptGroup(
        const Ogre::String& resource_group) noexcept;
#endif

    // JSON:
    bool               LoadAndParseJson(std::string const& filename, std::string const& rg_name, rapidjson::Document& j_doc);
    bool               SerializeAndWriteJson(std::string const& filename, std::string const& rg_name, rapidjson::Document& j_doc);

private:

#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
    friend class ContentManagerNativeIntegrationTestAccess;
#endif

    void EnsureResourceGroupListener();
    void ApplyShaderCompatibilityFallbacks(const Ogre::String& resource_group);
#if OGRE_VERSION_MAJOR >= 14
    void BindAuthenticatedResourceThread();
    void RequireAuthenticatedResourceThread(const char* operation) const;
    [[nodiscard]] bool IsAuthenticatedResourceThread() const noexcept;
#endif

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
#if OGRE_VERSION_MAJOR >= 14
    bool resourceStreamOpeningEnabled() const override;
    Ogre::DataStreamPtr resourceStreamOpening(
        const Ogre::String& name,
        const Ogre::String& group,
        Ogre::Resource* resource,
        const Ogre::Archive* selected_archive,
        const Ogre::FileInfo* exact_file_info,
        bool& handled) override;
#endif
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

    struct GeneratedMaterialBinding
    {
        std::uintptr_t material_pointer_token = 0U;
        Ogre::ResourceHandle material_handle = 0U;
        std::uint64_t group_generation = 0U;
        Ogre::String exact_origin;
    };

    struct AuthenticatedPackageArchiveBinding
    {
        struct MemberBinding
        {
            std::uint64_t compressed_size = 0U;
            std::uint64_t uncompressed_size = 0U;
            std::string sha256;
        };
        using MemberManifest =
            std::unordered_map<Ogre::String, MemberBinding>;

        Ogre::String source_archive_identity;
        Ogre::String selected_archive_name;
        Ogre::String selected_archive_type;
        std::string archive_sha256;
        std::uintptr_t archive_pointer_token = 0U;
        std::uint64_t group_generation = 0U;
        TerrainBundleAuthenticatedArchiveSnapshot immutable_archive;
        std::shared_ptr<const MemberManifest> immutable_member_manifest;
        std::size_t retained_manifest_member_count = 0U;
        std::size_t retained_manifest_file_count = 0U;
        std::uint64_t retained_manifest_identity_bytes = 0U;
    };

#if OGRE_VERSION_MAJOR >= 14
    struct AuthenticatedMaterialScriptGroupCandidate;
#endif

    void EraseAuthenticatedMeshBindingsForGroupLocked(
        const Ogre::String& resource_group);
    std::uint64_t AdvanceLegacyMaterialGroupGenerationLocked(
        const Ogre::String& resource_group);

    bool              m_base_resource_loaded;
    bool              m_resource_group_listener_registered;
    bool              m_mesh_serializer_listener_registered = false;
    std::mutex        m_legacy_material_archive_io_mutex;
    mutable std::mutex m_legacy_material_state_mutex;
    mutable std::mutex m_legacy_material_resolution_mutex;
    std::uint64_t     m_next_legacy_material_group_generation = 0U;
    std::unordered_map<Ogre::String, std::uint64_t>
        m_legacy_material_group_generations;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_package_archives_by_group;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<Ogre::String, std::string>>
        m_authenticated_package_archives_by_group;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<
            const Ogre::Archive*,
            AuthenticatedPackageArchiveBinding>>
        m_authenticated_package_archive_bindings_by_group;
    std::unordered_map<
        Ogre::String,
        TerrainBundleAuthenticatedArchiveSnapshot>
        m_authenticated_package_archive_pending_snapshots;
    std::uint64_t m_authenticated_package_archive_retained_bytes = 0U;
#if OGRE_VERSION_MAJOR >= 14
    Render::Ogre14AuthenticatedArchiveManifestAccounting
        m_authenticated_package_archive_manifest_accounting;
#endif
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_package_materials_by_group;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<std::string, std::unordered_set<Ogre::String>>>
        m_authenticated_materials_by_group;
    std::unordered_map<const Ogre::Resource*, AuthenticatedMeshBinding>
        m_authenticated_mesh_bindings;
#if OGRE_VERSION_MAJOR >= 14
    Render::Ogre14AuthenticatedTextureRegistryConfiguration
        m_authenticated_texture_receipt_configuration;
    Render::Ogre14AuthenticatedTextureReceiptRegistry
        m_authenticated_texture_receipts;
    Render::Ogre14AuthenticatedMaterialScriptRegistryConfiguration
        m_authenticated_material_script_configuration;
    Render::Ogre14AuthenticatedMaterialScriptRegistry
        m_authenticated_material_scripts;
    std::unique_ptr<AuthenticatedMaterialScriptGroupCandidate>
        m_authenticated_material_script_candidate;
    // Exact aborted generation retained until ResourceGroupManager has emitted
    // all native removal callbacks. It quarantines provisional resources from
    // the process-global authenticated registries.
    std::unique_ptr<AuthenticatedMaterialScriptGroupCandidate>
        m_aborted_material_script_candidate;
    std::uint64_t m_next_authenticated_material_script_parse_token = 0U;
#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
    bool m_force_next_authenticated_material_script_source_undelivered_for_testing =
        false;
    bool m_force_next_authenticated_material_event_empty_for_testing = false;
#endif
    mutable Render::Ogre14AuthenticatedResourceThreadGate
        m_authenticated_resource_thread_gate;
#endif
    std::unordered_map<
        Ogre::String,
        std::unordered_map<
            std::string,
            std::unordered_map<Ogre::String, Ogre::String>>>
        m_generated_material_fallbacks_by_group;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_generated_material_names_by_group;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<Ogre::String, GeneratedMaterialBinding>>
        m_generated_material_bindings_by_group;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_reported_material_resolutions_by_group;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<Ogre::String, std::string>>
        m_authorized_texture_fallbacks_by_group;
    std::unordered_map<Ogre::String, std::unordered_set<Ogre::String>>
        m_reported_texture_fallbacks_by_group;
    std::unordered_map<Ogre::String, std::uint64_t>
        m_committed_material_script_generations;
    Ogre::String      m_scripting_resource_group;
#if OGRE_VERSION_MAJOR < 14
    Ogre::String      m_current_script_name;
    bool              m_current_script_package_owned = false;
    std::string       m_current_script_authenticated_sha256;
#endif
};

} // namespace RoR
