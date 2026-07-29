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

#include <algorithm>

#include <Overlay/OgreOverlayManager.h>
#include <Overlay/OgreOverlay.h>


#include "Application.h"
#include "ColoredTextAreaOverlayElementFactory.h"
#include "ErrorUtils.h"
#include "LegacyMaterialCompatibilityPlan.h"
#include "SoundScriptManager.h"
#include "SkinFileFormat.h"
#include "Language.h"
#include "LegacyMaterialScriptSanitizer.h"
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
#include <OgreDataStream.h>
#include <OgreException.h>
#include <OgreFileSystem.h>
#include <OgreGpuProgram.h>
#include <OgreMaterialManager.h>
#include <OgreMeshManager.h>
#include <OgrePass.h>
#include <OgreRenderSystem.h>
#include <OgreRenderSystemCapabilities.h>
#include <OgreRoot.h>
#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <regex>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <OgreMeshLodGenerator.h>

using namespace Ogre;
using namespace RoR;

namespace
{

std::string Sha256Bytes(const std::string& payload)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
    unsigned int digest_size = 0U;
    if (EVP_Digest(
            payload.data(),
            payload.size(),
            digest.data(),
            &digest_size,
            EVP_sha256(),
            nullptr) != 1 ||
        digest_size != 32U)
    {
        return std::string();
    }

    static const char HEX_DIGITS[] = "0123456789abcdef";
    std::string result(digest_size * 2U, '0');
    for (std::size_t index = 0U; index < digest_size; ++index)
    {
        result[index * 2U] = HEX_DIGITS[digest[index] >> 4U];
        result[index * 2U + 1U] =
            HEX_DIGITS[digest[index] & 0x0fU];
    }
    return result;
}

void AppendLittleEndian32(
    std::vector<unsigned char>& bytes,
    std::uint32_t value)
{
    bytes.push_back(static_cast<unsigned char>(value & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xffU));
}

std::vector<unsigned char> BuildProceduralFallbackDds(
    const LegacyMaterialColor& color)
{
    const std::uint32_t width = 4U;
    const std::uint32_t height = 4U;
    std::vector<unsigned char> bytes;
    bytes.reserve(128U + width * height * 4U);
    bytes.push_back('D');
    bytes.push_back('D');
    bytes.push_back('S');
    bytes.push_back(' ');
    AppendLittleEndian32(bytes, 124U);
    AppendLittleEndian32(bytes, 0x0000100fU);
    AppendLittleEndian32(bytes, height);
    AppendLittleEndian32(bytes, width);
    AppendLittleEndian32(bytes, width * 4U);
    AppendLittleEndian32(bytes, 0U);
    AppendLittleEndian32(bytes, 0U);
    for (std::size_t index = 0U; index < 11U; ++index)
    {
        AppendLittleEndian32(bytes, 0U);
    }
    AppendLittleEndian32(bytes, 32U);
    AppendLittleEndian32(bytes, 0x00000041U);
    AppendLittleEndian32(bytes, 0U);
    AppendLittleEndian32(bytes, 32U);
    AppendLittleEndian32(bytes, 0x00ff0000U);
    AppendLittleEndian32(bytes, 0x0000ff00U);
    AppendLittleEndian32(bytes, 0x000000ffU);
    AppendLittleEndian32(bytes, 0xff000000U);
    AppendLittleEndian32(bytes, 0x00001000U);
    AppendLittleEndian32(bytes, 0U);
    AppendLittleEndian32(bytes, 0U);
    AppendLittleEndian32(bytes, 0U);
    AppendLittleEndian32(bytes, 0U);

    for (std::uint32_t y = 0U; y < height; ++y)
    {
        for (std::uint32_t x = 0U; x < width; ++x)
        {
            const bool dark = ((x / 2U) + (y / 2U)) % 2U != 0U;
            const unsigned int scale = dark ? 3U : 4U;
            bytes.push_back(static_cast<unsigned char>(
                static_cast<unsigned int>(color.blue) * scale / 4U));
            bytes.push_back(static_cast<unsigned char>(
                static_cast<unsigned int>(color.green) * scale / 4U));
            bytes.push_back(static_cast<unsigned char>(
                static_cast<unsigned int>(color.red) * scale / 4U));
            bytes.push_back(255U);
        }
    }
    return bytes;
}

} // namespace

ContentManager::ContentManager():
    m_base_resource_loaded(false),
    m_resource_group_listener_registered(false)
{
}

ContentManager::~ContentManager()
{
    if (m_mesh_serializer_listener_registered &&
        Ogre::MeshManager::getSingletonPtr() != nullptr &&
        Ogre::MeshManager::getSingleton().getListener() == this)
    {
        Ogre::MeshManager::getSingleton().setListener(nullptr);
    }
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

void ContentManager::EraseAuthenticatedMeshBindingsForGroupLocked(
    const Ogre::String& resource_group)
{
    for (auto binding = m_authenticated_mesh_bindings.begin();
         binding != m_authenticated_mesh_bindings.end();)
    {
        if (binding->second.group == resource_group)
        {
            binding = m_authenticated_mesh_bindings.erase(binding);
        }
        else
        {
            ++binding;
        }
    }
}

std::uint64_t ContentManager::AdvanceLegacyMaterialGroupGenerationLocked(
    const Ogre::String& resource_group)
{
    ++m_next_legacy_material_group_generation;
    if (m_next_legacy_material_group_generation == 0U)
    {
        ++m_next_legacy_material_group_generation;
    }
    m_legacy_material_group_generations[resource_group] =
        m_next_legacy_material_group_generation;
    this->EraseAuthenticatedMeshBindingsForGroupLocked(resource_group);
    return m_next_legacy_material_group_generation;
}

void ContentManager::RegisterPackageResourceLocation(
    const Ogre::String& resource_group,
    const Ogre::String& archive_name)
{
    std::scoped_lock<std::mutex, std::mutex> legacy_material_lock(
        m_legacy_material_resolution_mutex,
        m_legacy_material_state_mutex);
    m_package_archives_by_group[resource_group].insert(archive_name);
    this->AdvanceLegacyMaterialGroupGenerationLocked(resource_group);
}

void ContentManager::RegisterAuthenticatedPackageResourceLocation(
    const Ogre::String& resource_group,
    const Ogre::String& archive_name,
    const std::string& archive_sha256)
{
    std::scoped_lock<std::mutex, std::mutex> legacy_material_lock(
        m_legacy_material_resolution_mutex,
        m_legacy_material_state_mutex);
    m_package_archives_by_group[resource_group].insert(archive_name);
    m_authenticated_package_archives_by_group[resource_group][archive_name] =
        archive_sha256;
    this->AdvanceLegacyMaterialGroupGenerationLocked(resource_group);
}

void ContentManager::UnregisterPackageResourceGroup(
    const Ogre::String& resource_group)
{
    std::scoped_lock<std::mutex, std::mutex> legacy_material_lock(
        m_legacy_material_resolution_mutex,
        m_legacy_material_state_mutex);
    this->AdvanceLegacyMaterialGroupGenerationLocked(resource_group);
    m_package_archives_by_group.erase(resource_group);
    m_authenticated_package_archives_by_group.erase(resource_group);
    m_package_materials_by_group.erase(resource_group);
    m_authenticated_materials_by_group.erase(resource_group);
    m_generated_material_fallbacks_by_group.erase(resource_group);
    m_generated_material_names_by_group.erase(resource_group);
    m_reported_material_resolutions_by_group.erase(resource_group);
    m_authorized_texture_fallbacks_by_group.erase(resource_group);
    m_reported_texture_fallbacks_by_group.erase(resource_group);
    if (m_scripting_resource_group == resource_group)
    {
        m_scripting_resource_group.clear();
        m_current_script_name.clear();
        m_current_script_package_owned = false;
        m_current_script_authenticated_sha256.clear();
    }
}

void ContentManager::resourceGroupScriptingStarted(
    const Ogre::String& group_name,
    size_t script_count)
{
    (void)script_count;
    std::scoped_lock<std::mutex, std::mutex> legacy_material_lock(
        m_legacy_material_resolution_mutex,
        m_legacy_material_state_mutex);
    this->AdvanceLegacyMaterialGroupGenerationLocked(group_name);
    m_scripting_resource_group = group_name;
    m_current_script_name.clear();
    m_current_script_package_owned = false;
    m_current_script_authenticated_sha256.clear();
    m_package_materials_by_group[group_name].clear();
    m_authenticated_materials_by_group[group_name].clear();
    m_generated_material_fallbacks_by_group.erase(group_name);
    m_generated_material_names_by_group.erase(group_name);
    m_reported_material_resolutions_by_group.erase(group_name);
    m_authorized_texture_fallbacks_by_group.erase(group_name);
    m_reported_texture_fallbacks_by_group.erase(group_name);
}

void ContentManager::scriptParseStarted(
    const Ogre::String& script_name,
    bool& skip_this_script)
{
    std::lock_guard<std::mutex> state_lock(
        m_legacy_material_state_mutex);
    m_current_script_name = script_name;
    m_current_script_package_owned = false;
    m_current_script_authenticated_sha256.clear();
    (void)skip_this_script;
}

void ContentManager::scriptParseEnded(
    const Ogre::String& script_name,
    bool skipped)
{
    (void)script_name;
    (void)skipped;
    std::lock_guard<std::mutex> state_lock(
        m_legacy_material_state_mutex);
    m_current_script_name.clear();
    m_current_script_package_owned = false;
    m_current_script_authenticated_sha256.clear();
}

void ContentManager::resourceGroupScriptingEnded(
    const Ogre::String& group_name)
{
    this->ApplyShaderCompatibilityFallbacks(group_name);
    std::lock_guard<std::mutex> state_lock(
        m_legacy_material_state_mutex);
    m_scripting_resource_group.clear();
    m_current_script_name.clear();
    m_current_script_package_owned = false;
    m_current_script_authenticated_sha256.clear();
}

void ContentManager::resourceRemove(const Ogre::ResourcePtr& resource)
{
#if OGRE_VERSION_MAJOR >= 14
    if (resource &&
        Ogre::MeshManager::getSingletonPtr() != nullptr &&
        resource->getCreator() == Ogre::MeshManager::getSingletonPtr())
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        m_authenticated_mesh_bindings.erase(resource.get());
    }
#else
    (void)resource;
#endif
}

void ContentManager::processMaterialName(
    Ogre::Mesh* mesh,
    Ogre::String* name)
{
#if OGRE_VERSION_MAJOR >= 14
    if (mesh == nullptr ||
        name == nullptr ||
        name->empty() ||
        Ogre::MaterialManager::getSingletonPtr() == nullptr)
    {
        return;
    }

    // OGRE may deserialize multiple meshes in parallel. Serialize material
    // lookup/creation and the accompanying generated-name caches as one
    // transaction, while keeping archive I/O outside this critical section.
    std::lock_guard<std::mutex> resolution_lock(
        m_legacy_material_resolution_mutex);

    Ogre::String group;
    std::string resolution_archive_sha256;
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const auto authenticated_mesh =
            m_authenticated_mesh_bindings.find(mesh);
        if (authenticated_mesh == m_authenticated_mesh_bindings.end())
        {
            return;
        }
        const auto current_generation =
            m_legacy_material_group_generations.find(
                authenticated_mesh->second.group);
        if (
            current_generation ==
                m_legacy_material_group_generations.end() ||
            !mesh->isLoading() ||
            authenticated_mesh->second.mesh_group !=
                mesh->getGroup() ||
            authenticated_mesh->second.name != mesh->getName() ||
            authenticated_mesh->second.handle != mesh->getHandle() ||
            authenticated_mesh->second.state_count !=
                mesh->getStateCount() ||
            authenticated_mesh->second.group_generation !=
                current_generation->second)
        {
            if (authenticated_mesh !=
                m_authenticated_mesh_bindings.end())
            {
                m_authenticated_mesh_bindings.erase(authenticated_mesh);
            }
            return;
        }
        group = authenticated_mesh->second.group;
        resolution_archive_sha256 =
            authenticated_mesh->second.archive_sha256;
    }

    if (Ogre::MaterialManager::getSingleton().getByName(*name, group))
    {
        return;
    }

    const LegacyMaterialReferenceResolution resolution =
        ResolveLegacyMaterialReference(
            resolution_archive_sha256, *name);
    if (resolution.disposition ==
        LegacyMaterialReferenceDisposition::NONE)
    {
        return;
    }

    const Ogre::String requested = *name;
    Ogre::String resolved_name;
    if (resolution.disposition ==
        LegacyMaterialReferenceDisposition::ALIAS)
    {
        bool target_is_authenticated = false;
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            const auto material_group =
                m_authenticated_materials_by_group.find(group);
            if (material_group !=
                m_authenticated_materials_by_group.end())
            {
                const auto material_archive =
                    material_group->second.find(
                        resolution_archive_sha256);
                target_is_authenticated =
                    material_archive != material_group->second.end() &&
                    material_archive->second.count(
                        resolution.target_material) != 0U;
            }
        }
        if (!target_is_authenticated ||
            !Ogre::MaterialManager::getSingleton().getByName(
                resolution.target_material, group))
        {
            LOG(fmt::format(
                "[RoR|ContentManager|LegacyMaterialResolver] Material '{}' "
                "in group '{}' rejected alias '{}' because the exact "
                "authenticated target is unavailable "
                "(archive_sha256={})",
                requested,
                group,
                resolution.target_material,
                resolution_archive_sha256));
            return;
        }
        resolved_name = resolution.target_material;
    }
    else
    {
        auto& generated_by_request =
            m_generated_material_fallbacks_by_group[
                group][resolution_archive_sha256];
        const auto existing_generated =
            generated_by_request.find(requested);
        if (existing_generated != generated_by_request.end())
        {
            resolved_name = existing_generated->second;
        }
        else
        {
            resolved_name = BuildLegacyMaterialFallbackResourceName(
                resolution_archive_sha256, requested);
            Ogre::MaterialPtr material =
                Ogre::MaterialManager::getSingleton().getByName(
                    resolved_name, group);
            if (material &&
                m_generated_material_names_by_group[group].count(
                    resolved_name) == 0U)
            {
                LOG(fmt::format(
                    "[RoR|ContentManager|LegacyMaterialResolver] Generated "
                    "fallback name '{}' collides with package content in "
                    "group '{}'; preserving material '{}' unchanged",
                    resolved_name,
                    group,
                    requested));
                return;
            }
            if (!material)
            {
                material = Ogre::MaterialManager::getSingleton().create(
                    resolved_name, group);
                if (!material ||
                    material->getNumTechniques() == 0U ||
                    material->getTechnique(0U)->getNumPasses() == 0U)
                {
                    LOG(fmt::format(
                        "[RoR|ContentManager|LegacyMaterialResolver] Could "
                        "not create generated fallback '{}' in group '{}'; "
                        "preserving material '{}' unchanged",
                        resolved_name,
                        group,
                        requested));
                    return;
                }
                Ogre::Pass* pass =
                    material->getTechnique(0U)->getPass(0U);
                const float red =
                    static_cast<float>(resolution.color.red) / 255.0f;
                const float green =
                    static_cast<float>(resolution.color.green) / 255.0f;
                const float blue =
                    static_cast<float>(resolution.color.blue) / 255.0f;
                pass->setLightingEnabled(true);
                pass->setAmbient(
                    red * 0.45f,
                    green * 0.45f,
                    blue * 0.45f);
                pass->setDiffuse(red, green, blue, 1.0f);
                if (resolution.color.high_specular)
                {
                    pass->setSpecular(0.75f, 0.78f, 0.82f, 1.0f);
                    pass->setShininess(72.0f);
                }
                else
                {
                    pass->setSpecular(0.08f, 0.08f, 0.08f, 1.0f);
                    pass->setShininess(8.0f);
                }
                material->setReceiveShadows(true);
                m_generated_material_names_by_group[group].insert(
                    resolved_name);
            }
            generated_by_request[requested] = resolved_name;
        }
    }

    *name = resolved_name;
    if (m_reported_material_resolutions_by_group[group].insert(requested).second)
    {
        LOG(fmt::format(
            "[RoR|ContentManager|LegacyMaterialResolver] Material '{}' in "
            "mesh '{}' group '{}' resolved to '{}' via {} "
            "(archive_sha256={})",
            requested,
            mesh->getName(),
            group,
            resolved_name,
            resolution.disposition ==
                    LegacyMaterialReferenceDisposition::ALIAS
                ? "reviewed alias"
                : "generated lit fallback",
            resolution_archive_sha256));
    }
#else
    (void)mesh;
    (void)name;
#endif
}

void ContentManager::processSkeletonName(
    Ogre::Mesh* mesh,
    Ogre::String* name)
{
    (void)mesh;
    (void)name;
}

void ContentManager::processMeshCompleted(Ogre::Mesh* mesh)
{
#if OGRE_VERSION_MAJOR >= 14
    if (mesh != nullptr)
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        m_authenticated_mesh_bindings.erase(mesh);
    }
#else
    (void)mesh;
#endif
}

void ContentManager::ApplyShaderCompatibilityFallbacks(
    const Ogre::String& resource_group)
{
#if OGRE_VERSION_MAJOR >= 14
    if (Ogre::MaterialManager::getSingletonPtr() == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> resolution_lock(
        m_legacy_material_resolution_mutex);

    // Only repair materials whose CreateMaterial event was attributed to a
    // registered package archive. Shared engine templates are mixed into the
    // same resource group, and changing those here would hide their legacy
    // programs from ActorSpawner's purpose-built RTSS material fallback.
    std::unordered_set<Ogre::String> package_materials;
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const auto package_material_group =
            m_package_materials_by_group.find(resource_group);
        if (package_material_group == m_package_materials_by_group.end() ||
            package_material_group->second.empty())
        {
            return;
        }
        package_materials = package_material_group->second;
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
        if (package_materials.count(material->getName()) == 0)
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

    if (Ogre::MeshManager::getSingletonPtr() != nullptr)
    {
        Ogre::MeshSerializerListener* existing_listener =
            Ogre::MeshManager::getSingleton().getListener();
        if (existing_listener == nullptr || existing_listener == this)
        {
            Ogre::MeshManager::getSingleton().setListener(this);
            m_mesh_serializer_listener_registered = true;
        }
        else
        {
            LOG(
                "[RoR|ContentManager|LegacyMaterialResolver] An existing "
                "mesh serializer listener prevented authenticated legacy "
                "material compatibility registration");
        }
    }

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
    std::string resolution_archive_sha256;
    std::uint64_t group_generation = 0U;
    std::unordered_map<Ogre::String, std::string>
        authenticated_archives;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<Ogre::String, std::string>>
        authenticated_archive_groups;
    std::unordered_map<Ogre::String, std::uint64_t>
        group_generations;
    const bool uses_autodetect_group =
        group ==
        Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME;
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        if (uses_autodetect_group)
        {
            authenticated_archive_groups =
                m_authenticated_package_archives_by_group;
            group_generations =
                m_legacy_material_group_generations;
        }
        else
        {
            const auto current_generation =
                m_legacy_material_group_generations.find(group);
            if (current_generation !=
                m_legacy_material_group_generations.end())
            {
                group_generation = current_generation->second;
            }
            const auto authenticated_group =
                m_authenticated_package_archives_by_group.find(group);
            if (authenticated_group !=
                m_authenticated_package_archives_by_group.end())
            {
                authenticated_archives =
                    authenticated_group->second;
            }
        }

        const auto authorized_group =
            m_authorized_texture_fallbacks_by_group.find(group);
        if (authorized_group !=
            m_authorized_texture_fallbacks_by_group.end())
        {
            const auto authorized_texture =
                authorized_group->second.find(name);
            if (authorized_texture != authorized_group->second.end())
            {
                resolution_archive_sha256 =
                    authorized_texture->second;
            }
        }
    }

    if (!resolution_archive_sha256.empty())
    {
        LegacyMaterialColor color = {0U, 0U, 0U, false};
        if (!ResolveLegacyMissingTexture(
                resolution_archive_sha256, name, color))
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                fmt::format(
                    "Authorized procedural texture '{}' in group '{}' no "
                    "longer matches its authenticated compatibility plan",
                    name,
                    group),
                "ContentManager::resourceLoading");
        }

        const std::vector<unsigned char> dds =
            BuildProceduralFallbackDds(color);
        bool report_fallback = false;
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            const auto authorized_group =
                m_authorized_texture_fallbacks_by_group.find(group);
            if (authorized_group ==
                m_authorized_texture_fallbacks_by_group.end())
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Authorization for procedural texture '{}' in group "
                        "'{}' was revoked while the resource was loading",
                        name,
                        group),
                    "ContentManager::resourceLoading");
            }
            const auto authorized_texture =
                authorized_group->second.find(name);
            const auto current_generation =
                m_legacy_material_group_generations.find(group);
            if (authorized_texture ==
                    authorized_group->second.end() ||
                authorized_texture->second !=
                    resolution_archive_sha256 ||
                current_generation ==
                    m_legacy_material_group_generations.end() ||
                current_generation->second != group_generation)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Authorization for procedural texture '{}' in group "
                        "'{}' changed while the resource was loading",
                        name,
                        group),
                    "ContentManager::resourceLoading");
            }
            report_fallback =
                m_reported_texture_fallbacks_by_group[group]
                    .insert(name)
                    .second;
        }

        // An authenticated script already selected this generated name. Never
        // delegate it back to OGRE's resource lookup: a later same-named
        // untrusted location must not replace the authorized procedural bytes.
        Ogre::MemoryDataStream* replacement =
            OGRE_NEW Ogre::MemoryDataStream(
                name, dds.size(), true, false);
        replacement->write(dds.data(), dds.size());
        replacement->seek(0U);
        if (report_fallback)
        {
            LOG(fmt::format(
                "[RoR|ContentManager|LegacyTextureResolver] "
                "Texture '{}' in group '{}' used a procedural 4x4 "
                "compatibility fallback (archive_sha256={})",
                name,
                group,
                resolution_archive_sha256));
        }
        return Ogre::DataStreamPtr(replacement);
    }

#if OGRE_VERSION_MAJOR >= 14
    // OGRE 14 documents ZipArchive::open() as non-thread-safe, while its
    // default thread-support mode 3 compiles the archive mutexes out. Open
    // every resource selected from an authenticated package here so all such
    // ZIP access is serialized. Returning the stream also prevents
    // ResourceGroupManager from reopening the same archive behind the
    // listener.
    if (resource == nullptr ||
        (!uses_autodetect_group &&
         (authenticated_archives.empty() ||
          group_generation == 0U)) ||
        (uses_autodetect_group &&
         authenticated_archive_groups.empty()))
    {
        return Ogre::DataStreamPtr();
    }

    Ogre::String effective_group = group;
    Ogre::DataStreamPtr authenticated_stream;
    Ogre::String selected_archive_name;
    std::string selected_archive_sha256;
    bool change_resource_group = false;
    {
        std::lock_guard<std::mutex> archive_lock(
            m_legacy_material_archive_io_mutex);
        if (uses_autodetect_group)
        {
            try
            {
                effective_group =
                    Ogre::ResourceGroupManager::getSingleton()
                        .findGroupContainingResource(name);
            }
            catch (const Ogre::Exception&)
            {
                return Ogre::DataStreamPtr();
            }
            const auto authenticated_group =
                authenticated_archive_groups.find(effective_group);
            const auto effective_generation =
                group_generations.find(effective_group);
            if (authenticated_group ==
                    authenticated_archive_groups.end() ||
                effective_generation == group_generations.end())
            {
                return Ogre::DataStreamPtr();
            }
            authenticated_archives =
                authenticated_group->second;
            group_generation =
                effective_generation->second;
            // Resource::load() resolves OgreAutodetect after prepareImpl().
            // Because returning a stream here bypasses ResourceGroupManager's
            // normal selection branch, mirror that eventual ownership
            // transition now for both private and global-pool groups.
            change_resource_group = true;
        }

        // Mirror ResourceGroupManager's exact-case index preference before
        // its case-insensitive fallback. Authenticated terrain dependencies
        // are ZIP archives, whose exists() lookup is case-sensitive even on
        // Windows. Only enumerate the archive index for the uncommon
        // case-insensitive fallback.
        const Ogre::ResourceGroupManager::LocationList& locations =
            Ogre::ResourceGroupManager::getSingleton()
                .getResourceLocationList(effective_group);
        const Ogre::Archive* selected_archive = nullptr;
        for (const Ogre::ResourceGroupManager::ResourceLocation& location :
             locations)
        {
            if (location.archive != nullptr &&
                location.archive->exists(name))
            {
                selected_archive = location.archive;
                break;
            }
        }
#if !OGRE_RESOURCEMANAGER_STRICT
        if (selected_archive == nullptr)
        {
            Ogre::String folded_name = name;
            Ogre::StringUtil::toLowerCase(folded_name);
            for (const Ogre::ResourceGroupManager::ResourceLocation& location :
                 locations)
            {
                if (location.archive == nullptr)
                {
                    continue;
                }
                const Ogre::FileInfoListPtr indexed_files =
                    location.archive->findFileInfo(
                        "*", location.recursive, false);
                if (!indexed_files)
                {
                    continue;
                }
                for (const Ogre::FileInfo& indexed_file : *indexed_files)
                {
                    Ogre::String folded_candidate =
                        indexed_file.filename;
                    Ogre::StringUtil::toLowerCase(folded_candidate);
                    if (folded_candidate == folded_name)
                    {
                        selected_archive = location.archive;
                        break;
                    }
                }
                if (selected_archive != nullptr)
                {
                    break;
                }
            }
        }
#endif
        if (selected_archive == nullptr)
        {
            return Ogre::DataStreamPtr();
        }

        selected_archive_name = selected_archive->getName();
        const auto authenticated_archive =
            authenticated_archives.find(selected_archive_name);
        if (authenticated_archive == authenticated_archives.end())
        {
            // Preserve OGRE's first-location-wins behavior. A same-named
            // resource from an earlier untrusted location must never inherit
            // the later authenticated package's compatibility policy.
            return Ogre::DataStreamPtr();
        }
        selected_archive_sha256 =
            authenticated_archive->second;
        authenticated_stream = selected_archive->open(name);
    }

    if (!authenticated_stream)
    {
        return Ogre::DataStreamPtr();
    }
    if (change_resource_group)
    {
        resource->changeGroupOwnership(effective_group);
    }
    const Ogre::String expected_mesh_group =
        change_resource_group ? effective_group : group;

    static const std::size_t MAX_AUTHENTICATED_MESH_BYTES =
        512U * 1024U * 1024U;
    if (Ogre::MeshManager::getSingletonPtr() != nullptr &&
        resource->getCreator() ==
            Ogre::MeshManager::getSingletonPtr() &&
        authenticated_stream->size() <=
            MAX_AUTHENTICATED_MESH_BYTES)
    {
        Ogre::Mesh* mesh = static_cast<Ogre::Mesh*>(resource);
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const auto current_generation =
            m_legacy_material_group_generations.find(
                effective_group);
        const auto current_authenticated_group =
            m_authenticated_package_archives_by_group.find(
                effective_group);
        if (current_generation !=
                m_legacy_material_group_generations.end() &&
            current_generation->second == group_generation &&
            current_authenticated_group !=
                m_authenticated_package_archives_by_group.end())
        {
            const auto current_archive =
                current_authenticated_group->second.find(
                    selected_archive_name);
            if (current_archive !=
                    current_authenticated_group->second.end() &&
                current_archive->second ==
                    selected_archive_sha256 &&
                mesh->getName() == name &&
                mesh->getGroup() == expected_mesh_group)
            {
                m_authenticated_mesh_bindings[resource] = {
                    effective_group,
                    expected_mesh_group,
                    name,
                    mesh->getHandle(),
                    mesh->getStateCount(),
                    group_generation,
                    selected_archive_sha256};
            }
        }
    }
    return authenticated_stream;
#else
    (void)resource;
    return Ogre::DataStreamPtr();
#endif
}

void ContentManager::resourceStreamOpened(const Ogre::String& name, const Ogre::String& group, Ogre::Resource* resource, Ogre::DataStreamPtr& dataStream)
{
    static const std::size_t MAX_PACKAGE_MATERIAL_SCRIPT_BYTES =
        16U * 1024U * 1024U;

#if OGRE_VERSION_MAJOR >= 14
    if (resource != nullptr &&
        Ogre::MeshManager::getSingletonPtr() != nullptr &&
        resource->getCreator() == Ogre::MeshManager::getSingletonPtr())
    {
        // Authenticated package resources are returned directly from
        // resourceLoading(), so reaching this callback means OGRE selected an
        // untrusted or shadowing location. Erase any stale pointer reuse and
        // keep the authored material references unchanged.
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        m_authenticated_mesh_bindings.erase(resource);
        return;
    }
#endif

    if (resource != nullptr ||
        !dataStream ||
        !Ogre::StringUtil::endsWith(name, ".material", true))
    {
        return;
    }

    std::unordered_set<Ogre::String> package_archives;
    std::unordered_map<Ogre::String, std::string>
        authenticated_archives;
    std::uint64_t group_generation = 0U;
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        if (group != m_scripting_resource_group ||
            name != m_current_script_name)
        {
            return;
        }
        const auto package_group =
            m_package_archives_by_group.find(group);
        const auto current_generation =
            m_legacy_material_group_generations.find(group);
        if (package_group == m_package_archives_by_group.end() ||
            current_generation ==
                m_legacy_material_group_generations.end())
        {
            return;
        }
        package_archives = package_group->second;
        group_generation = current_generation->second;
        const auto authenticated_group =
            m_authenticated_package_archives_by_group.find(group);
        if (authenticated_group !=
            m_authenticated_package_archives_by_group.end())
        {
            authenticated_archives = authenticated_group->second;
        }
    }

    if (dataStream->size() > MAX_PACKAGE_MATERIAL_SCRIPT_BYTES)
    {
        LOG(fmt::format(
            "[RoR|ContentManager|LegacyMaterialSanitizer] Package material "
            "script '{}' in group '{}' exceeds the {} byte identity and "
            "repair limit; preserving the original stream",
            name,
            group,
            MAX_PACKAGE_MATERIAL_SCRIPT_BYTES));
        return;
    }

    const std::string original = dataStream->getAsString();
    dataStream->seek(0U);

    // OGRE's script callbacks expose the script filename but not the FileInfo
    // (and exact-name lookups are not valid while its prebuilt script list is
    // being consumed). Authenticate the opened stream by comparing it with
    // the same member opened directly from each registered package archive.
    // This remains fail-closed for duplicate filenames and never trusts the
    // resource-location iteration order.
    std::vector<std::string> authenticated_archive_hashes;
    bool package_owned = false;
    {
        std::lock_guard<std::mutex> archive_lock(
            m_legacy_material_archive_io_mutex);
        const Ogre::ResourceGroupManager::LocationList& locations =
            Ogre::ResourceGroupManager::getSingleton()
                .getResourceLocationList(group);
        for (const Ogre::ResourceGroupManager::ResourceLocation& location :
             locations)
        {
            if (location.archive == nullptr)
            {
                continue;
            }
            const Ogre::String archive_name =
                location.archive->getName();
            if (package_archives.count(archive_name) == 0U ||
                !location.archive->exists(name))
            {
                continue;
            }

            try
            {
                const Ogre::DataStreamPtr package_stream =
                    location.archive->open(name);
                if (!package_stream ||
                    package_stream->size() != original.size() ||
                    package_stream->getAsString() != original)
                {
                    continue;
                }
            }
            catch (...)
            {
                continue;
            }

            package_owned = true;
            const auto authenticated_archive =
                authenticated_archives.find(archive_name);
            if (authenticated_archive !=
                authenticated_archives.end())
            {
                authenticated_archive_hashes.push_back(
                    authenticated_archive->second);
            }
        }
    }

    std::sort(
        authenticated_archive_hashes.begin(),
        authenticated_archive_hashes.end());
    authenticated_archive_hashes.erase(
        std::unique(
            authenticated_archive_hashes.begin(),
            authenticated_archive_hashes.end()),
        authenticated_archive_hashes.end());
    const std::string authenticated_archive_sha256 =
        authenticated_archive_hashes.size() == 1U
            ? authenticated_archive_hashes.front()
            : std::string();
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const auto current_generation =
            m_legacy_material_group_generations.find(group);
        if (current_generation ==
                m_legacy_material_group_generations.end() ||
            current_generation->second != group_generation ||
            group != m_scripting_resource_group ||
            name != m_current_script_name)
        {
            return;
        }
        m_current_script_package_owned = package_owned;
        m_current_script_authenticated_sha256 =
            authenticated_archive_sha256;
    }

    if (authenticated_archive_hashes.size() > 1U)
    {
        LOG(fmt::format(
            "[RoR|ContentManager|LegacyMaterialSanitizer] Material script "
            "'{}' in group '{}' matched {} authenticated package archives; "
            "preserving the original stream",
            name,
            group,
            authenticated_archive_hashes.size()));
        return;
    }

    if (authenticated_archive_sha256.empty())
    {
        return;
    }

    const LegacyMaterialScriptEditPlan* edit_plan =
        FindLegacyMaterialScriptEditPlan(
            authenticated_archive_sha256,
            name);
    if (edit_plan == nullptr)
    {
        return;
    }

    const std::string observed_script_sha256 = Sha256Bytes(original);
    if (observed_script_sha256.empty())
    {
        LOG(fmt::format(
            "[RoR|ContentManager|LegacyMaterialSanitizer] Authenticated "
            "material script '{}' in group '{}' was not repaired because "
            "SHA-256 could not be computed "
            "(archive_sha256={})",
            name,
            group,
            authenticated_archive_sha256));
        return;
    }

    const LegacyMaterialScriptPlanApplication patched =
        ApplyLegacyMaterialScriptEditPlan(
            *edit_plan,
            observed_script_sha256,
            original);
    if (!patched.safe)
    {
        LOG(fmt::format(
            "[RoR|ContentManager|LegacyMaterialSanitizer] Authenticated "
            "material script '{}' in group '{}' rejected its exact repair "
            "plan: {} (archive_sha256={}, script_sha256={})",
            name,
            group,
            patched.rejection_reason,
            authenticated_archive_sha256,
            observed_script_sha256));
        return;
    }

    std::vector<Ogre::String> texture_fallback_names;
    for (std::size_t edit_index = 0U;
         edit_index < edit_plan->edit_count;
         ++edit_index)
    {
        const char* replacement_token =
            edit_plan->edits[edit_index].replacement;
        if (replacement_token == nullptr)
        {
            continue;
        }
        const std::string replacement(replacement_token);
        static const std::string TEXTURE_DIRECTIVE_PREFIX = "texture ";
        if (replacement.compare(
                0U,
                TEXTURE_DIRECTIVE_PREFIX.size(),
                TEXTURE_DIRECTIVE_PREFIX) != 0)
        {
            continue;
        }

        const Ogre::String fallback_name =
            replacement.substr(TEXTURE_DIRECTIVE_PREFIX.size());
        LegacyMaterialColor fallback_color = {0U, 0U, 0U, false};
        if (!ResolveLegacyMissingTexture(
                authenticated_archive_sha256,
                fallback_name,
                fallback_color))
        {
            continue;
        }

        bool conflicting_authorization = false;
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            const auto authorized_group =
                m_authorized_texture_fallbacks_by_group.find(group);
            if (authorized_group !=
                m_authorized_texture_fallbacks_by_group.end())
            {
                const auto existing_fallback =
                    authorized_group->second.find(fallback_name);
                conflicting_authorization =
                    existing_fallback !=
                        authorized_group->second.end() &&
                    existing_fallback->second !=
                        authenticated_archive_sha256;
            }
        }
        if (Ogre::ResourceGroupManager::getSingleton().resourceExists(
                group, fallback_name) ||
            conflicting_authorization)
        {
            LOG(fmt::format(
                "[RoR|ContentManager|LegacyTextureResolver] Authenticated "
                "material script '{}' in group '{}' rejected its exact "
                "repair plan because generated texture '{}' collides with "
                "existing content or another archive identity",
                name,
                group,
                fallback_name));
            return;
        }
        texture_fallback_names.push_back(fallback_name);
    }

    Ogre::DataStreamPtr replacement(
        OGRE_NEW Ogre::MemoryDataStream(
            name,
            patched.payload.size(),
            true,
            false));
    if (!patched.payload.empty())
    {
        replacement->write(
            patched.payload.data(),
            patched.payload.size());
    }
    replacement->seek(0U);
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const auto current_generation =
            m_legacy_material_group_generations.find(group);
        if (current_generation ==
                m_legacy_material_group_generations.end() ||
            current_generation->second != group_generation ||
            group != m_scripting_resource_group ||
            name != m_current_script_name ||
            m_current_script_authenticated_sha256 !=
                authenticated_archive_sha256)
        {
            return;
        }

        auto& authorized_fallbacks =
            m_authorized_texture_fallbacks_by_group[group];
        for (const Ogre::String& fallback_name : texture_fallback_names)
        {
            const auto existing_fallback =
                authorized_fallbacks.find(fallback_name);
            if (existing_fallback != authorized_fallbacks.end() &&
                existing_fallback->second !=
                    authenticated_archive_sha256)
            {
                return;
            }
        }
        for (const Ogre::String& fallback_name : texture_fallback_names)
        {
            authorized_fallbacks[fallback_name] =
                authenticated_archive_sha256;
        }
    }
    dataStream = replacement;

    LOG(fmt::format(
        "[RoR|ContentManager|LegacyMaterialSanitizer] Authenticated "
        "material script '{}' in group '{}' applied {} exact compatibility "
        "edit(s) (archive_sha256={}, script_sha256={})",
        name,
        group,
        patched.applied_edit_count,
        authenticated_archive_sha256,
        observed_script_sha256));
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
        std::lock_guard<std::mutex> resolution_lock(
            m_legacy_material_resolution_mutex);
        const Ogre::MaterialPtr existing_material =
            Ogre::MaterialManager::getSingleton().getByName(
                matEvent->mName, matEvent->mResourceGroup);
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_current_script_package_owned &&
                matEvent->mResourceGroup == m_scripting_resource_group &&
                matEvent->mFile == m_current_script_name &&
                (!existing_material ||
                 existing_material->getGroup() !=
                     matEvent->mResourceGroup))
            {
                // Record the accepted first definition only. Later scripts
                // with a colliding material name are rejected by
                // resourceCollision(), so they must not change the original
                // material's package ownership.
                m_package_materials_by_group[
                    matEvent->mResourceGroup]
                    .insert(matEvent->mName);
                if (!m_current_script_authenticated_sha256.empty())
                {
                    m_authenticated_materials_by_group[
                        matEvent->mResourceGroup]
                        [m_current_script_authenticated_sha256]
                            .insert(matEvent->mName);
                }
            }
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

    if (GetEffectiveGfxSkyMode() == GfxSkyMode::CAELUM)
        this->AddResourcePack(ContentManager::ResourcePack::CAELUM);

    if (GetEffectiveGfxSkyMode() == GfxSkyMode::SKYX)
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
