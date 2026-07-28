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

#include "ContentManager.h"


#include <Overlay/OgreOverlayManager.h>
#include <Overlay/OgreOverlay.h>


#include "Application.h"
#include "ColoredTextAreaOverlayElementFactory.h"
#include "ErrorUtils.h"
#include "SoundScriptManager.h"
#include "SkinFileFormat.h"
#include "Language.h"
#include "PlatformUtils.h"
#include "ShaderCompatibilityPolicy.h"

#include "CacheSystem.h"

#include "OgreShaderParticleRenderer.h"

// Removed by Skybon as part of OGRE 1.9 port 
// Disabling temporarily for 1.8.1 as well. ~ only_a_ptr, 2015-11
// TODO: Study the system, then re-enable or remove entirely.
//#include "OgreBoxEmitterFactory.h"

#ifdef USE_ANGELSCRIPT
#include "FireExtinguisherAffectorFactory.h"
#include "ExtinguishableFireAffectorFactory.h"
#endif // USE_ANGELSCRIPT

#include "Utils.h"

#include <OgreArchive.h>
#include <OgreFileSystem.h>
#include <OgreGpuProgram.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgreRenderSystem.h>
#include <OgreRenderSystemCapabilities.h>
#include <OgreRoot.h>
#include <regex>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <OgreMeshLodGenerator.h>

using namespace Ogre;
using namespace RoR;

ContentManager::ContentManager():
    m_base_resource_loaded(false),
    m_resource_group_listener_registered(false)
{
}

ContentManager::~ContentManager()
{
    if (m_resource_group_listener_registered &&
        Ogre::ResourceGroupManager::getSingletonPtr() != nullptr)
    {
        Ogre::ResourceGroupManager::getSingleton().removeResourceGroupListener(this);
    }
}

void ContentManager::EnsureResourceGroupListener()
{
    if (!m_resource_group_listener_registered)
    {
        Ogre::ResourceGroupManager::getSingleton().addResourceGroupListener(this);
        m_resource_group_listener_registered = true;
    }
}

void ContentManager::RegisterPackageResourceLocation(
    const Ogre::String& resource_group,
    const Ogre::String& archive_name)
{
    m_package_archives_by_group[resource_group].insert(archive_name);
}

void ContentManager::resourceGroupScriptingStarted(
    const Ogre::String& group_name,
    size_t script_count)
{
    (void)script_count;
    m_scripting_resource_group = group_name;
    m_script_occurrences.clear();
    m_current_script_name.clear();
    m_current_script_package_owned = false;
    m_package_materials_by_group[group_name].clear();
}

void ContentManager::scriptParseStarted(
    const Ogre::String& script_name,
    bool& skip_this_script)
{
    m_current_script_name = script_name;
    m_current_script_package_owned = false;
    if (skip_this_script || m_scripting_resource_group.empty())
    {
        return;
    }

    const auto package_group =
        m_package_archives_by_group.find(m_scripting_resource_group);
    if (package_group == m_package_archives_by_group.end())
    {
        return;
    }

    // OGRE reports only the script name here. Match this callback to the
    // corresponding archive by following the same ordered resource-location
    // traversal used by parseResourceGroupScripts(). This remains exact when
    // a built-in script and a package script happen to share a filename.
    const std::size_t wanted_occurrence =
        m_script_occurrences[script_name]++;
    const Ogre::ResourceGroupManager::LocationList& locations =
        Ogre::ResourceGroupManager::getSingleton().getResourceLocationList(
            m_scripting_resource_group);
    std::vector<ScriptArchiveState> archive_states;
    archive_states.reserve(locations.size());
    for (const Ogre::ResourceGroupManager::ResourceLocation& location :
         locations)
    {
        std::size_t matching_script_count = 0;
        if (location.archive != nullptr)
        {
            const Ogre::FileInfoListPtr matching_scripts =
                location.archive->findFileInfo(
                    script_name, location.recursive, false);
            if (matching_scripts)
            {
                matching_script_count = matching_scripts->size();
            }
        }
        archive_states.push_back({
            matching_script_count,
            location.archive != nullptr &&
                package_group->second.count(location.archive->getName()) != 0});
    }
    m_current_script_package_owned = IsPackageOwnedScriptOccurrence(
        archive_states, wanted_occurrence);
}

void ContentManager::scriptParseEnded(
    const Ogre::String& script_name,
    bool skipped)
{
    (void)script_name;
    (void)skipped;
    m_current_script_name.clear();
    m_current_script_package_owned = false;
}

void ContentManager::resourceGroupScriptingEnded(
    const Ogre::String& group_name)
{
    this->ApplyShaderCompatibilityFallbacks(group_name);
    m_scripting_resource_group.clear();
    m_script_occurrences.clear();
    m_current_script_name.clear();
    m_current_script_package_owned = false;
}

void ContentManager::ApplyShaderCompatibilityFallbacks(
    const Ogre::String& resource_group)
{
#if OGRE_VERSION_MAJOR >= 14
    if (Ogre::MaterialManager::getSingletonPtr() == nullptr)
    {
        return;
    }

    // Only repair materials whose CreateMaterial event was attributed to a
    // registered package archive. Shared engine templates are mixed into the
    // same resource group, and changing those here would hide their legacy
    // programs from ActorSpawner's purpose-built RTSS material fallback.
    const auto package_material_group =
        m_package_materials_by_group.find(resource_group);
    if (package_material_group == m_package_materials_by_group.end() ||
        package_material_group->second.empty())
    {
        return;
    }

    std::size_t repaired_materials = 0;
    std::size_t repaired_passes = 0;
    bool renderer_requires_complete_graphics_pipeline = false;
    if (Ogre::Root::getSingletonPtr() != nullptr &&
        Ogre::Root::getSingleton().getRenderSystem() != nullptr &&
        Ogre::Root::getSingleton().getRenderSystem()->getCapabilities() !=
            nullptr)
    {
        renderer_requires_complete_graphics_pipeline =
            !Ogre::Root::getSingleton()
                 .getRenderSystem()
                 ->getCapabilities()
                 ->hasCapability(Ogre::RSC_FIXED_FUNCTION);
    }

    Ogre::ResourceManager::ResourceMapIterator resources =
        Ogre::MaterialManager::getSingleton().getResourceIterator();
    while (resources.hasMoreElements())
    {
        Ogre::MaterialPtr material =
            Ogre::static_pointer_cast<Ogre::Material>(resources.getNext());
        if (!material || material->getGroup() != resource_group)
        {
            continue;
        }
        if (package_material_group->second.count(material->getName()) == 0)
        {
            continue;
        }

        auto find_incompatible_programs =
            [renderer_requires_complete_graphics_pipeline](
               Ogre::Pass* pass,
               Ogre::StringVector* incompatible_programs) -> bool
        {
            bool found_incompatible_program = false;
            for (int program_index = 0;
                 program_index < Ogre::GPT_COUNT;
                 ++program_index)
            {
                const Ogre::GpuProgramType program_type =
                    static_cast<Ogre::GpuProgramType>(program_index);
                if (!pass->hasGpuProgram(program_type))
                {
                    continue;
                }

                Ogre::GpuProgramPtr program;
                bool resolution_failed = false;
                try
                {
                    program = pass->getGpuProgram(program_type);
                }
                catch (const Ogre::Exception&)
                {
                    // Some third-party scripts leave a program usage behind
                    // even when its named resource could not be resolved.
                    // Treat that stage exactly like an unavailable program.
                    resolution_failed = true;
                }

                bool load_failed = false;
                if (!resolution_failed && program &&
                    program->isSupported() &&
                    !program->hasCompileError())
                {
                    try
                    {
                        // A declaration can pass the initial renderer/profile
                        // check while its source is absent or fails only when
                        // the backend compiles it. Force that validation while
                        // the package is being initialized.
                        program->load();
                    }
                    catch (const Ogre::Exception&)
                    {
                        load_failed = true;
                    }
                }

                const ExplicitGpuProgramState state = {
                    true,
                    !resolution_failed && static_cast<bool>(program),
                    !resolution_failed && !load_failed && program &&
                        program->isSupported(),
                    !resolution_failed && program &&
                        (load_failed || program->hasCompileError())};
                if (!NeedsGeneratedShaderFallback(state))
                {
                    continue;
                }

                found_incompatible_program = true;
                if (incompatible_programs != nullptr)
                {
                    const Ogre::String& program_name =
                        pass->getGpuProgramName(program_type);
                    incompatible_programs->push_back(
                        !program_name.empty()
                            ? program_name
                            : Ogre::GpuProgram::getProgramTypeName(
                                  program_type));
                }
            }

            const ExplicitGraphicsProgramBindings bindings = {
                pass->isProgrammable(),
                pass->hasGpuProgram(Ogre::GPT_VERTEX_PROGRAM),
                pass->hasGpuProgram(Ogre::GPT_FRAGMENT_PROGRAM),
                pass->hasGpuProgram(Ogre::GPT_GEOMETRY_PROGRAM),
                pass->hasGpuProgram(Ogre::GPT_MESH_PROGRAM),
                pass->hasGpuProgram(Ogre::GPT_COMPUTE_PROGRAM)};
            if (NeedsGeneratedShaderFallbackForIncompletePipeline(
                    renderer_requires_complete_graphics_pipeline,
                    bindings))
            {
                found_incompatible_program = true;
                if (incompatible_programs != nullptr)
                {
                    incompatible_programs->push_back(
                        "incomplete graphics program pipeline");
                }
            }
            return found_incompatible_program;
        };

        std::vector<ShaderTechniqueCompatibility>
            technique_compatibilities;
        technique_compatibilities.reserve(material->getNumTechniques());
        for (std::size_t technique_index = 0;
             technique_index < material->getNumTechniques();
             ++technique_index)
        {
            Ogre::Technique* technique =
                material->getTechnique(
                    static_cast<unsigned short>(technique_index));
            bool technique_is_compatible =
                technique->getNumPasses() != 0;
            for (std::size_t pass_index = 0;
                 pass_index < technique->getNumPasses();
                 ++pass_index)
            {
                Ogre::Pass* pass =
                    technique->getPass(
                        static_cast<unsigned short>(pass_index));
                if (find_incompatible_programs(pass, nullptr))
                {
                    technique_is_compatible = false;
                    break;
                }
            }

            technique_compatibilities.push_back({
                technique->getSchemeName(),
                technique_is_compatible});
        }

        bool repaired_material = false;
        for (std::size_t technique_index = 0;
             technique_index < material->getNumTechniques();
             ++technique_index)
        {
            Ogre::Technique* technique =
                material->getTechnique(static_cast<unsigned short>(technique_index));
            const bool scheme_has_compatible_technique =
                HasCompatibleShaderTechniqueForScheme(
                    technique_compatibilities,
                    technique->getSchemeName());
            for (std::size_t pass_index = 0;
                 pass_index < technique->getNumPasses();
                 ++pass_index)
            {
                Ogre::Pass* pass =
                    technique->getPass(static_cast<unsigned short>(pass_index));
                Ogre::StringVector incompatible_programs;
                const bool pass_has_incompatible_program =
                    find_incompatible_programs(
                        pass, &incompatible_programs);
                if (!ShouldRepairIncompatibleShaderPass(
                        scheme_has_compatible_technique,
                        pass_has_incompatible_program))
                {
                    continue;
                }

                // A programmable pass must use one coherent pipeline. Keeping
                // a supported stage beside a missing/unsupported stage would
                // still leave the pass unusable, so hand the complete pass to
                // RTShaderSystem while retaining its authored render state and
                // texture units.
                for (int program_index = 0;
                     program_index < Ogre::GPT_COUNT;
                     ++program_index)
                {
                    const Ogre::GpuProgramType program_type =
                        static_cast<Ogre::GpuProgramType>(program_index);
                    if (pass->hasGpuProgram(program_type))
                    {
                        pass->setGpuProgram(
                            program_type, Ogre::GpuProgramPtr(), true);
                    }
                }

                std::stringstream program_list;
                for (std::size_t program_index = 0;
                     program_index < incompatible_programs.size();
                     ++program_index)
                {
                    if (program_index != 0)
                    {
                        program_list << ", ";
                    }
                    program_list << incompatible_programs[program_index];
                }
                LOG(fmt::format(
                    "[RoR|ContentManager] Material '{}' in group '{}', "
                    "technique {}, pass {} bound an unavailable shader "
                    "pipeline ({}); preserving its material state and using "
                    "RTShaderSystem generation",
                    material->getName(),
                    resource_group,
                    technique_index,
                    pass_index,
                    program_list.str()));
                repaired_material = true;
                ++repaired_passes;
            }
        }

        if (repaired_material)
        {
            ++repaired_materials;
        }
    }

    if (repaired_materials != 0)
    {
        LOG(fmt::format(
            "[RoR|ContentManager] Shader compatibility repaired {} "
            "material(s), {} pass(es) in resource group '{}'",
            repaired_materials,
            repaired_passes,
            resource_group));
    }
#else
    (void)resource_group;
#endif
}

// ================================================================================
// Static variables
// ================================================================================

#define DECLARE_RESOURCE_PACK(_FIELD_, _NAME_, _RESOURCE_GROUP_) \
    const ContentManager::ResourcePack ContentManager::ResourcePack::_FIELD_(_NAME_, _RESOURCE_GROUP_);

DECLARE_RESOURCE_PACK( OGRE_CORE,             "OgreCore",             "OgreCoreRG");
DECLARE_RESOURCE_PACK( WALLPAPERS,            "wallpapers",           "Wallpapers");
DECLARE_RESOURCE_PACK( AIRFOILS,              "airfoils",             "AirfoilsRG");
DECLARE_RESOURCE_PACK( CAELUM,                "caelum",               "CaelumRG");
DECLARE_RESOURCE_PACK( CUBEMAPS,              "cubemaps",             "CubemapsRG");
DECLARE_RESOURCE_PACK( DASHBOARDS,            "dashboards",           "DashboardsRG");
DECLARE_RESOURCE_PACK( FAMICONS,              "famicons",             "FamiconsRG");
DECLARE_RESOURCE_PACK( FLAGS,                 "flags",                "FlagsRG");
DECLARE_RESOURCE_PACK( FONTS,                 "fonts",                "FontsRG");
DECLARE_RESOURCE_PACK( HYDRAX,                "hydrax",               "HydraxRG");
DECLARE_RESOURCE_PACK( ICONS,                 "icons",                "IconsRG");
DECLARE_RESOURCE_PACK( MATERIALS,             "materials",            "MaterialsRG");
DECLARE_RESOURCE_PACK( MESHES,                "meshes",               "MeshesRG");
DECLARE_RESOURCE_PACK( MYGUI,                 "mygui",                "MyGuiRG");
DECLARE_RESOURCE_PACK( OVERLAYS,              "overlays",             "OverlaysRG");
DECLARE_RESOURCE_PACK( PAGED,                 "paged",                "PagedRG");
DECLARE_RESOURCE_PACK( PARTICLES,             "particles",            "ParticlesRG");
DECLARE_RESOURCE_PACK( PSSM,                  "pssm",                 "PssmRG");
DECLARE_RESOURCE_PACK( RTSHADER,              "rtshader",             "RtShaderRG");
DECLARE_RESOURCE_PACK( SCRIPTS,               "scripts",              "ScriptsRG");
DECLARE_RESOURCE_PACK( SOUNDS,                "sounds",               "SoundsRG");
DECLARE_RESOURCE_PACK( TEXTURES,              "textures",             "TexturesRG");
DECLARE_RESOURCE_PACK( SKYX,                  "SkyX",                 "SkyXRG");

// ================================================================================
// Functions
// ================================================================================

void ContentManager::AddResourcePack(ResourcePack const& resource_pack, std::string const& override_rgn)
{
    this->EnsureResourceGroupListener();

    Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();

    Ogre::String rg_name;
    if (!override_rgn.empty()) // Custom RG defined?
    {
        rg_name = override_rgn;
    }
    else // Use default RG
    {
        if (rgm.resourceGroupExists(resource_pack.resource_group_name)) // Already loaded?
        {
            return; // Nothing to do, nothing to report
        }
        rg_name = resource_pack.resource_group_name;
    }

    std::stringstream log_msg;
    log_msg << "[RoR|ContentManager] Loading resource pack \"" << resource_pack.name << "\" to group \"" << rg_name << "\"";
    std::string dir_path = PathCombine(App::sys_resources_dir->getStr(), resource_pack.name);
    std::string zip_path = dir_path + ".zip";
    if (FileExists(zip_path))
    {
        log_msg << " (ZIP archive)";
        LOG(log_msg.str());
        rgm.addResourceLocation(zip_path, "Zip", rg_name);
    }
    else
    {
        if (FolderExists(dir_path))
        {
            log_msg << " (directory)";
            LOG(log_msg.str());
            rgm.addResourceLocation(dir_path, "FileSystem", rg_name);
        }
        else
        {
            log_msg << " failed, data not found.";
            throw std::runtime_error(log_msg.str());
        }
    }

    if (override_rgn.empty()) // Only init the default RG
    {
        rgm.initialiseResourceGroup(rg_name);
    }
}

void ContentManager::InitContentManager()
{
    this->EnsureResourceGroupListener();

    ResourceGroupManager::getSingleton().addResourceLocation(
        App::sys_config_dir->getStr(), "FileSystem", RGN_CONFIG, /*recursive=*/false, /*readOnly=*/false);
    ResourceGroupManager::getSingleton().addResourceLocation(
        App::sys_savegames_dir->getStr(), "FileSystem", RGN_SAVEGAMES, /*recursive=*/false, /*readOnly=*/false);
    ResourceGroupManager::getSingleton().addResourceLocation(
        App::sys_scripts_dir->getStr(), "FileSystem", RGN_SCRIPTS, /*recursive:*/false, /*readonly:*/false);
    ResourceGroupManager::getSingleton().addResourceLocation(
        App::sys_logs_dir->getStr(), "FileSystem", RGN_LOGS, /*recursive:*/false, /*readonly:*/false);

    Ogre::ScriptCompilerManager::getSingleton().setListener(this);

    // Initialize "managed materials" first
    //   These are base materials referenced by user content
    //   They must be initialized before any content is loaded,
    //   otherwise material links are unresolved and loading ends with an exception
    this->InitManagedMaterials(RGN_MANAGED_MATS);

    // set listener if none has already been set
    if (!Ogre::ResourceGroupManager::getSingleton().getLoadingListener())
        Ogre::ResourceGroupManager::getSingleton().setLoadingListener(this);

    // by default, display everything in the depth map
    Ogre::MovableObject::setDefaultVisibilityFlags(DEPTHMAP_ENABLED);


    this->AddResourcePack(ResourcePack::MYGUI);
    this->AddResourcePack(ResourcePack::DASHBOARDS);


#ifdef _WIN32
    // TODO: FIX UNDER LINUX!
    // register particle classes
    LOG("RoR|ContentManager: Registering Particle Box Emitter");
    ParticleSystemRendererFactory* mParticleSystemRendererFact = OGRE_NEW ShaderParticleRendererFactory();
    ParticleSystemManager::getSingleton().addRendererFactory(mParticleSystemRendererFact);

    // Removed by Skybon as part of OGRE 1.9 port 
    // Disabling temporarily for 1.8.1 as well.  ~ only_a_ptr, 2015-11
    //ParticleEmitterFactory *mParticleEmitterFact = OGRE_NEW BoxEmitterFactory();
    //ParticleSystemManager::getSingleton().addEmitterFactory(mParticleEmitterFact);

#endif // _WIN32

#ifdef USE_ANGELSCRIPT
    // FireExtinguisherAffector
    ParticleAffectorFactory* pAffFact = OGRE_NEW FireExtinguisherAffectorFactory();
    ParticleSystemManager::getSingleton().addAffectorFactory(pAffFact);

    // ExtinguishableFireAffector
    pAffFact = OGRE_NEW ExtinguishableFireAffectorFactory();
    ParticleSystemManager::getSingleton().addAffectorFactory(pAffFact);
#endif // USE_ANGELSCRIPT

    // sound is a bit special as we mark the base sounds so we don't clear them accidentally later on
#ifdef USE_OPENAL
    LOG("RoR|ContentManager: Creating Sound Manager");
    App::CreateSoundScriptManager();
    App::GetSoundScriptManager()->setLoadingBaseSounds(true);
#endif // USE_OPENAL

    AddResourcePack(ResourcePack::SOUNDS);

    // streams path, to be processed later by the cache system
    LOG("RoR|ContentManager: Loading filesystems");

    LOG("RoR|ContentManager: Registering colored text overlay factory");
    ColoredTextAreaOverlayElementFactory* pCT = new ColoredTextAreaOverlayElementFactory();
    OverlayManager::getSingleton().addOverlayElementFactory(pCT);

    // set default mipmap level (NB some APIs ignore this)
    if (TextureManager::getSingletonPtr())
        TextureManager::getSingleton().setDefaultNumMipmaps(5);

    TextureFilterOptions tfo = TFO_NONE;
    switch (App::gfx_texture_filter->getEnum<GfxTexFilter>())
    {
    case GfxTexFilter::ANISOTROPIC: tfo = TFO_ANISOTROPIC;        break;
    case GfxTexFilter::TRILINEAR:   tfo = TFO_TRILINEAR;          break;
    case GfxTexFilter::BILINEAR:    tfo = TFO_BILINEAR;           break;
    case GfxTexFilter::NONE:        tfo = TFO_NONE;               break;
    }
    MaterialManager::getSingleton().setDefaultAnisotropy(Math::Clamp(App::gfx_anisotropy->getInt(), 1, 16));
    MaterialManager::getSingleton().setDefaultTextureFiltering(tfo);

    // load all resources now, so the zip files are also initiated
    LOG("RoR|ContentManager: Calling initialiseAllResourceGroups()");
    try
    {
        ResourceGroupManager::getSingleton().initialiseAllResourceGroups();
    }
    catch (Ogre::Exception& e)
    {
        LOG("RoR|ContentManager: catched error while initializing Resource groups: " + e.getFullDescription());
    }
#ifdef USE_OPENAL
    App::GetSoundScriptManager()->setLoadingBaseSounds(false);
#endif // USE_OPENAL

    new Ogre::MeshLodGenerator();
}

void ContentManager::InitModCache(CacheValidity validity)
{
    // Sets up RGN_CONTENT which encompasses all mods, scans it for changes and deletes it again.
    // IMPORTANT NOTE ON 'readOnly' FLAG:
    //   We need mods in subdirs to be writable for the Tuning menu to work.
    //   Apart from `Resources` and resource groups, OGRE also keeps `Archives` in `ArchiveManager`
    //   These aren't unloaded on destroying resource groups, and keep a 'readOnly' flag (defaults to true).
    //   Upon loading/creating new resource groups, OGRE complains (=assert on Debug, exception on Release) if the submitted flag doesn't match.
    //   It's possible to manually unload archives to reset the flag, but for simplicity we just always load subdirs as 'writable', even during modcache update.
    // ------------------------------------------------------------------------------------------

    ResourceGroupManager::getSingleton().addResourceLocation(
        App::sys_cache_dir->getStr(), "FileSystem", RGN_CACHE, /*recursive=*/false, /*readOnly=*/false);
    ResourceGroupManager::getSingleton().addResourceLocation(
        App::sys_thumbnails_dir->getStr(), "FileSystem", RGN_THUMBNAILS, /*recursive=*/false, /*readOnly=*/false);
    ResourceGroupManager::getSingleton().addResourceLocation(
        App::sys_repo_attachments_dir->getStr(), "FileSystem", RGN_REPO_ATTACHMENTS, /*recursive=*/false, /*readOnly=*/false);

    // Add top-level ZIPs/directories to RGN_CONTENT (non-recursive)

    if (!App::app_extra_mod_path->getStr().empty())
    {
        std::string extra_mod_path = App::app_extra_mod_path->getStr();
        ResourceGroupManager::getSingleton().addResourceLocation(extra_mod_path           , "FileSystem", RGN_CONTENT);
    }
    for (const std::string& dirname : App::GetCacheSystem()->GetContentDirs())
    {
        ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(App::sys_user_dir->getStr(), dirname), "FileSystem", RGN_CONTENT);
    }
    ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(App::sys_process_dir->getStr(), "content") , "FileSystem", RGN_CONTENT);
    std::string objects = PathCombine("resources", "beamobjects.zip");
    ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(App::sys_process_dir->getStr(), objects)   , "Zip"       , RGN_CONTENT);
    std::string dashboards = PathCombine("resources", "dashboards.zip");
    ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(App::sys_process_dir->getStr(), dashboards), "Zip", RGN_CONTENT);
    std::string gadgets = PathCombine("resources", "gadgets.zip");
    ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(App::sys_process_dir->getStr(), gadgets), "Zip", RGN_CONTENT);
    
    // Create RGN_TEMP in recursive mode to find all subdirectories.

    ResourceGroupManager::getSingleton().createResourceGroup(RGN_TEMP, false);
    if (!App::app_extra_mod_path->getStr().empty())
    {
        std::string extra_mod_path = App::app_extra_mod_path->getStr();
        ResourceGroupManager::getSingleton().addResourceLocation(extra_mod_path           , "FileSystem", RGN_TEMP, true);
    }
    for (const std::string& dirname : App::GetCacheSystem()->GetContentDirs())
    {
        ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(App::sys_user_dir->getStr(), dirname), "FileSystem", RGN_TEMP, true);
    }
    ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(App::sys_process_dir->getStr(), "content") , "FileSystem", RGN_TEMP, true);

    // Traverse RGN_TEMP and add all subdirectories to RGN_CONTENT.
    // (TBD: why not just make RGN_CONTENT itself recursive? -- ohlidalp, 10/2023)

    FileInfoListPtr dirs = ResourceGroupManager::getSingleton().findResourceFileInfo(RGN_TEMP, "*", /*dirs:*/true);
    for (const auto& dir_fileinfo : *dirs)
    {
        if (!dir_fileinfo.archive)
            continue;
        String fullpath = PathCombine(dir_fileinfo.archive->getName(), dir_fileinfo.filename);
        ResourceGroupManager::getSingleton().addResourceLocation(fullpath, "FileSystem", RGN_CONTENT, /*recursive:*/false, /*readonly:*/false);
    }
    ResourceGroupManager::getSingleton().destroyResourceGroup(RGN_TEMP);

    // Traverse RGN_CONTENT and detect updates

    if (validity == CacheValidity::UNKNOWN)
    {
        validity = App::GetCacheSystem()->EvaluateCacheValidity(); // Must be called while RGN_CONTENT is alive.
    }
    App::GetCacheSystem()->LoadModCache(validity);

    ResourceGroupManager::getSingleton().destroyResourceGroup(RGN_CONTENT);
    
}

Ogre::DataStreamPtr ContentManager::resourceLoading(const Ogre::String& name, const Ogre::String& group, Ogre::Resource* resource)
{
    return Ogre::DataStreamPtr();
}

void ContentManager::resourceStreamOpened(const Ogre::String& name, const Ogre::String& group, Ogre::Resource* resource, Ogre::DataStreamPtr& dataStream)
{
}

bool ContentManager::resourceCollision(Ogre::Resource* resource, Ogre::ResourceManager* resourceManager)
{
    // RoR loads each resource bundle (see CacheSystem.h for info)
    // into dedicated resource group outside the global pool [see CacheSystem::LoadResource()]
    // This means resource collision is pretty much content creator's fault, with 2 exceptions:
    // * asset packs (introduced 2024) are mixed into the requesting mod's resource group.
    // * bundled resources (e.g. beamobjects.zip) are also mixed into the mod's resource group.
    RoR::LogFormat("[RoR|ContentManager] Skipping resource with duplicate name: '%s' (origin: '%s')",
        resource->getName().c_str(), resource->getOrigin().c_str());
    return false; // Instruct OGRE to drop the new resource and keep the original.
}

bool ContentManager::handleEvent(ScriptCompiler *compiler, ScriptCompilerEvent *evt, void *retval)
{
    if (evt->mType == CreateMaterialScriptCompilerEvent::eventType)
    {
        // Workaround for OGRE script compiler not properly checking that material name is not empty.
        // See https://github.com/RigsOfRods/rigs-of-rods/issues/2349
        auto* matEvent = static_cast<CreateMaterialScriptCompilerEvent*>(evt);
        if (matEvent->mName.empty())
        {
            RoR::LogFormat("[RoR] Got malformed material (empty name) from file: '%s' - forcing OGRE to fail loading.",
                matEvent->mFile.c_str());
            // Report "handled" but create nothing -> OGRE will interrupt the loading
            //   with message "failed to find or create material" [in MaterialTranslator::translate()]
            return true;
        }

#if OGRE_VERSION_MAJOR >= 14
        const Ogre::MaterialPtr existing_material =
            Ogre::MaterialManager::getSingleton().getByName(
                matEvent->mName, matEvent->mResourceGroup);
        if (m_current_script_package_owned &&
            matEvent->mResourceGroup == m_scripting_resource_group &&
            matEvent->mFile == m_current_script_name &&
            (!existing_material ||
             existing_material->getGroup() != matEvent->mResourceGroup))
        {
            // Record the accepted first definition only. Later scripts with a
            // colliding material name are rejected by resourceCollision(), so
            // they must not change the original material's package ownership.
            m_package_materials_by_group[matEvent->mResourceGroup].insert(
                matEvent->mName);
        }
#endif
    }
    else if (evt->mType == CreateParticleSystemScriptCompilerEvent::eventType)
    {
        // Workaround for OGRE ignoring resource groups when registering particle templates
        // See https://github.com/RigsOfRods/rigs-of-rods/pull/2398
        auto* particleEvent = static_cast<CreateParticleSystemScriptCompilerEvent*>(evt);
        if (Ogre::ParticleSystemManager::getSingleton().getTemplate(particleEvent->mName) != nullptr)
        {
            // Duplicate name -> OGRE would throw exception and fail initializing whole resource group
            RoR::LogFormat("[RoR] Duplicate particle system name '%s' in file: '%s' - forcing OGRE to fail loading.",
                particleEvent->mName.c_str(), particleEvent->mFile.c_str());
            return true; // Instruct OGRE to skip the particle system
        }
    }

    return false; // Report "not handled"
}

void ContentManager::InitManagedMaterials(std::string const & rg_name)
{
    Ogre::String managed_materials_dir = PathCombine(App::sys_resources_dir->getStr(), "managed_materials");

    // OGRE 14's programmable-only renderers use RTShader System for the
    // receiver programs. Loading the legacy "on" directory there would bind
    // Cg-only programs and leave every inheriting material unsupported.
#if OGRE_VERSION_MAJOR >= 14
    if (App::gfx_shadow_type->getEnum<GfxShadowType>() == GfxShadowType::PSSM)
    {
        ResourceGroupManager::getSingleton().addResourceLocation(
            PathCombine(managed_materials_dir, "shadows/pssm/rtss"),
            "FileSystem", rg_name);
    }
    else
    {
        ResourceGroupManager::getSingleton().addResourceLocation(
            PathCombine(managed_materials_dir, "shadows/pssm/off"),
            "FileSystem", rg_name);
    }
#else
    // Legacy PSSM materials use the Cg programs shipped in the "on" tree.
    if (App::gfx_shadow_type->getEnum<GfxShadowType>() == GfxShadowType::PSSM)
    {
        if (rg_name == RGN_MANAGED_MATS) // Only load shared resources on startup
        {
            ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(managed_materials_dir, "shadows/pssm/on/shared"), "FileSystem", rg_name);
        }
        ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(managed_materials_dir, "shadows/pssm/on"), "FileSystem", rg_name);
    }
    else
    {
        ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(managed_materials_dir,"shadows/pssm/off"), "FileSystem", rg_name);
    }
#endif

    ResourceGroupManager::getSingleton().addResourceLocation(PathCombine(managed_materials_dir, "texture"), "FileSystem", rg_name);

    // Last
    ResourceGroupManager::getSingleton().addResourceLocation(managed_materials_dir, "FileSystem", rg_name);

    if (rg_name == RGN_MANAGED_MATS) // Only initialize the global resource group
        ResourceGroupManager::getSingleton().initialiseResourceGroup(rg_name);
}

void ContentManager::LoadGameplayResources()
{
    if (!m_base_resource_loaded)
    {
        this->AddResourcePack(ContentManager::ResourcePack::AIRFOILS);
        this->AddResourcePack(ContentManager::ResourcePack::TEXTURES);
        this->AddResourcePack(ContentManager::ResourcePack::FAMICONS);
        this->AddResourcePack(ContentManager::ResourcePack::MATERIALS);
        this->AddResourcePack(ContentManager::ResourcePack::MESHES);
        this->AddResourcePack(ContentManager::ResourcePack::OVERLAYS);
        this->AddResourcePack(ContentManager::ResourcePack::PARTICLES);

        m_base_resource_loaded = true;
    }

    if (App::gfx_water_mode->getEnum<GfxWaterMode>() == GfxWaterMode::HYDRAX)
        this->AddResourcePack(ContentManager::ResourcePack::HYDRAX);

    if (App::gfx_sky_mode->getEnum<GfxSkyMode>() == GfxSkyMode::CAELUM)
        this->AddResourcePack(ContentManager::ResourcePack::CAELUM);

    if (App::gfx_sky_mode->getEnum<GfxSkyMode>() == GfxSkyMode::SKYX)
        this->AddResourcePack(ContentManager::ResourcePack::SKYX);

    if (App::gfx_vegetation_mode->getEnum<GfxVegetation>() != RoR::GfxVegetation::NONE)
        this->AddResourcePack(ContentManager::ResourcePack::PAGED);
}

std::string ContentManager::ListAllUserContent()
{
    std::stringstream buf;

    auto dir_list = Ogre::ResourceGroupManager::getSingleton().listResourceFileInfo(RGN_CONTENT, true);
    for (auto dir: *dir_list)
    {
        buf << dir.filename << std::endl;
    }

    // Any filename + listed extensions, ignore case
    std::regex file_whitelist("^.\\.(airplane|boat|car|fixed|load|machine|skin|terrn2|train|truck)$", std::regex::icase);

    auto file_list = Ogre::ResourceGroupManager::getSingleton().listResourceFileInfo(RGN_CONTENT, false);
    for (auto file: *file_list)
    {
        if ((file.archive != nullptr) || std::regex_match(file.filename, file_whitelist))
        {
            buf << file.filename << std::endl;
        }
    }

    return buf.str();
}

bool ContentManager::LoadAndParseJson(std::string const& filename, std::string const& rg_name, rapidjson::Document& j_doc)
{
    try
    {
        Ogre::DataStreamPtr stream = Ogre::ResourceGroupManager::getSingleton().openResource(filename, rg_name);
        Ogre::String json_str = stream->getAsString();
        rapidjson::MemoryStream j_stream(json_str.data(), json_str.length());
        j_doc.ParseStream<rapidjson::kParseNanAndInfFlag>(j_stream);
    }
    catch (Ogre::FileNotFoundException)
    {
        return false; // Error already logged by OGRE
    }
    catch (std::exception& e)
    {
        RoR::LogFormat("[RoR] Failed to open or read json file '%s' (resource group '%s'), message: '%s'",
                       filename.c_str(), rg_name.c_str(), e.what());
        return false;
    }

    if (j_doc.HasParseError())
    {
        RoR::LogFormat("[RoR] Error parsing JSON file '%s' (resource group '%s')",
                       filename.c_str(), rg_name.c_str());
        return false;
    }

    return true;
}

bool ContentManager::SerializeAndWriteJson(std::string const& filename, std::string const& rg_name, rapidjson::Document& j_doc)
{
    // Serialize JSON to string
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF8<>, rapidjson::UTF8<>,
                      rapidjson::CrtAllocator, rapidjson::kWriteNanAndInfFlag>
                      writer(buffer);
    j_doc.Accept(writer);

    // Write JSON to file
    try
    {
        Ogre::DataStreamPtr stream
            = Ogre::ResourceGroupManager::getSingleton().createResource(
                filename, rg_name, /*overwrite=*/true);
        size_t written = stream->write(buffer.GetString(), buffer.GetSize());
        if (written < buffer.GetSize())
        {
            RoR::LogFormat("[RoR] Error writing JSON file '%s' (resource group '%s'), ",
                           "only written %u out of %u bytes!",
                           filename.c_str(), rg_name.c_str(), written, buffer.GetSize());
            return false;
        }
        return true;
    }
    catch (std::exception& e)
    {
        RoR::LogFormat("[RoR] Error writing JSON file '%s' (resource group '%s'), message: '%s'",
                       filename.c_str(), rg_name.c_str(), e.what());
        return false;
    }
}

bool ContentManager::DeleteDiskFile(std::string const& filename, std::string const& rg_name)
{
    try
    {
        Ogre::ResourceGroupManager::getSingleton().deleteResource(filename, rg_name);
        return true;
    }
    catch (std::exception& e)
    {
        RoR::LogFormat("[RoR|ModCache] Error deleting file '%s' (resource group '%s'), message: '%s'",
                        filename.c_str(), rg_name.c_str(), e.what());
        return false;
    }
}

