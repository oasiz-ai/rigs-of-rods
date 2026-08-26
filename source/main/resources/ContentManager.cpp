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
#if OGRE_VERSION_MAJOR >= 14
#include "gfx/ogre14/Ogre14AuthenticatedArchiveLocationClosure.h"
#endif
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
#include <OgreArchiveManager.h>
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
#include <OgreTexture.h>
#include <OgreTextureManager.h>
#include <OgreZip.h>
#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <new>
#include <openssl/evp.h>
#include <type_traits>
#include <utility>
#include <vector>
#include <regex>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <OgreMeshLodGenerator.h>

using namespace Ogre;
using namespace RoR;

namespace
{

#if OGRE_VERSION_MAJOR >= 14
static_assert(
    kLegacyMaterialScriptRepairPlanVersion ==
    Render::kOgre14AuthenticatedMaterialScriptRepairPlanVersion);
static_assert(
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_ENTRIES ==
    Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates);
static_assert(
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_IDENTITY_BYTES ==
    Render::kOgre14AuthenticatedTextureMaximumArchiveMemberIdentityBytes);
static_assert(
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_BYTES ==
    Render::kOgre14AuthenticatedTextureMaximumSourceBytes);
static_assert(
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_TOTAL_MEMBER_BYTES ==
    Render::kOgre14AuthenticatedTextureMaximumRetainedBytes);
static_assert(
    TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_MEMBER_NAME_BYTES ==
    Render::kOgre14AuthenticatedTextureMaximumIdentifierBytes);
#endif

constexpr std::uint64_t
    MAX_AUTHENTICATED_EMBEDDED_ZIP_REGISTRATION_ATTEMPTS = 65536U;
constexpr std::uint64_t
    MAX_AUTHENTICATED_MATERIAL_SCRIPT_SOURCE_OPEN_ATTEMPTS = 128U;

std::uint64_t AllocateAuthenticatedEmbeddedZipRegistrationId()
{
    static std::mutex registration_mutex;
    static std::uint64_t registration_count = 0U;
    std::lock_guard<std::mutex> registration_lock(registration_mutex);
    if (registration_count ==
        MAX_AUTHENTICATED_EMBEDDED_ZIP_REGISTRATION_ATTEMPTS)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated EmbeddedZip registration-attempt space is "
            "exhausted",
            "AllocateAuthenticatedEmbeddedZipRegistrationId");
    }
    ++registration_count;
    return registration_count;
}

std::string Sha256Bytes(const void* payload, std::size_t payload_size)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
    unsigned int digest_size = 0U;
    if (EVP_Digest(
            payload,
            payload_size,
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

std::string Sha256Bytes(const std::string& payload)
{
    return Sha256Bytes(payload.data(), payload.size());
}

#if OGRE_VERSION_MAJOR >= 14
bool AddAuthenticatedMaterialScriptBytes(
    std::uint64_t current,
    std::uint64_t additional,
    std::uint64_t maximum,
    std::uint64_t& result) noexcept
{
    if (additional > maximum || current > maximum - additional)
    {
        return false;
    }
    result = current + additional;
    return true;
}

bool ResolveLiveArchiveManagerPointer(
    Ogre::ArchiveManager& archive_manager,
    const Ogre::Archive* expected_archive,
    Ogre::String& output_name)
{
    output_name.clear();
    if (expected_archive == nullptr)
    {
        return false;
    }
    std::size_t pointer_matches = 0U;
    Ogre::ArchiveManager::ArchiveMapIterator archives =
        archive_manager.getArchiveIterator();
    while (archives.hasMoreElements())
    {
        const Ogre::String archive_name = archives.peekNextKey();
        Ogre::Archive* archive = archives.getNext();
        if (archive == expected_archive)
        {
            output_name = archive_name;
            ++pointer_matches;
        }
    }
    return pointer_matches == 1U;
}

/// Resolves the one archive member an ordinary texture request may open, using
/// the same reviewed selection policy the authenticated texture path applies.
/// Case-sensitive archives still require one exact full-name match; a
/// case-insensitive archive requires one unambiguous folded full-name match,
/// and only then may fall back to the explicit Zip basename rule. Ambiguity,
/// an oversized index, or an absent member all resolve to false, which leaves
/// the caller with no receipt exactly as a genuine absence does.
///
/// When reviewed_file_info is supplied it receives the selected archive's own
/// index record for that member, so a caller which OGRE handed no record can
/// still attribute the member it opens to the archive's exact metadata. That
/// record must exist exactly once in the index or the resolution refuses.
bool ResolveReviewedSelectedTextureSourceMember(
    const Ogre::Archive& selected_archive,
    bool recursive,
    const Ogre::String& name,
    Ogre::String& reviewed_member,
    Ogre::FileInfo* reviewed_file_info = nullptr)
{
    reviewed_member.clear();
    if (reviewed_file_info != nullptr)
    {
        *reviewed_file_info = Ogre::FileInfo();
    }
    const Ogre::FileInfoListPtr selected_index =
        const_cast<Ogre::Archive&>(selected_archive)
            .findFileInfo("*", recursive, false);
    if (!selected_index || selected_index->empty() ||
        selected_index->size() >
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates)
    {
        return false;
    }

    Ogre::String requested_basename;
    Ogre::String requested_path;
    Ogre::StringUtil::splitFilename(name, requested_basename, requested_path);
    Ogre::String folded_full_name = name;
    Ogre::String folded_basename = requested_basename;
    Ogre::StringUtil::toLowerCase(folded_full_name);
    Ogre::StringUtil::toLowerCase(folded_basename);

    const bool archive_case_sensitive = selected_archive.isCaseSensitive();
    const bool allow_zip_basename_fallback =
        !archive_case_sensitive &&
        (selected_archive.getType() == "Zip" ||
         selected_archive.getType() == "EmbeddedZip");

    std::vector<Render::Ogre14AuthenticatedTextureArchiveMemberObservation>
        member_observations;
    member_observations.reserve(selected_index->size());
    for (const Ogre::FileInfo& indexed_file : *selected_index)
    {
        const Ogre::String exact_member =
            indexed_file.path + indexed_file.basename;
        if (exact_member.empty())
        {
            continue;
        }
        Ogre::String folded_member = exact_member;
        Ogre::StringUtil::toLowerCase(folded_member);
        Ogre::String indexed_basename = indexed_file.basename;
        Ogre::StringUtil::toLowerCase(indexed_basename);
        Render::Ogre14AuthenticatedTextureArchiveMemberObservation observation;
        observation.exact_member_name = exact_member;
        observation.exact_full_match = exact_member == name;
        observation.folded_full_match = folded_member == folded_full_name;
        observation.folded_basename_match =
            allow_zip_basename_fallback &&
            indexed_basename == folded_basename;
        member_observations.push_back(std::move(observation));
    }

    Ogre::String selected_member;
    if (!Render::SelectOgre14AuthenticatedTextureArchiveMember(
            archive_case_sensitive, allow_zip_basename_fallback,
            member_observations.data(), member_observations.size(),
            selected_member) ||
        selected_member.empty())
    {
        return false;
    }
    if (reviewed_file_info != nullptr)
    {
        const Ogre::FileInfo* selected_record = nullptr;
        std::size_t selected_record_count = 0U;
        for (const Ogre::FileInfo& indexed_file : *selected_index)
        {
            if (indexed_file.archive == &selected_archive &&
                indexed_file.path + indexed_file.basename == selected_member)
            {
                selected_record = &indexed_file;
                ++selected_record_count;
            }
        }
        if (selected_record == nullptr || selected_record_count != 1U)
        {
            return false;
        }
        *reviewed_file_info = *selected_record;
    }
    reviewed_member.swap(selected_member);
    return !reviewed_member.empty();
}

bool IsAuthenticatedMaterialScriptIdentifier(
    const Ogre::String& value, bool allow_empty = false) noexcept
{
    return (allow_empty || !value.empty()) &&
        value.size() <=
            Render::kOgre14AuthenticatedMaterialScriptMaximumIdentifierBytes &&
        value.find('\0') == Ogre::String::npos;
}

std::uint64_t AuthenticatedMaterialScriptPreopenIdentityBytes(
    const Ogre::String& group,
    const Ogre::String& root_script_request,
    const Ogre::String& compiler_file_identity,
    const Ogre::String& archive_source_identity,
    const Ogre::String& selected_archive_name,
    const Ogre::String& selected_archive_type,
    const Ogre::String& archive_sha256,
    const Ogre::FileInfo& file_info,
    const Ogre::String& exact_member) noexcept
{
    const std::array<const Ogre::String*, 11U> identifiers = {
        &group,
        &root_script_request,
        &compiler_file_identity,
        &archive_source_identity,
        &selected_archive_name,
        &selected_archive_type,
        &archive_sha256,
        &file_info.filename,
        &file_info.path,
        &file_info.basename,
        &exact_member};
    std::uint64_t total = 64U * 3U;
    for (std::size_t index = 0U; index < identifiers.size(); ++index)
    {
        if (!IsAuthenticatedMaterialScriptIdentifier(
                *identifiers[index], index == 8U) ||
            !AddAuthenticatedMaterialScriptBytes(
                total,
                static_cast<std::uint64_t>(identifiers[index]->size()),
                (std::numeric_limits<std::uint64_t>::max)(), total))
        {
            return (std::numeric_limits<std::uint64_t>::max)();
        }
    }
    return total;
}

std::uint64_t AuthenticatedMaterialScriptSourceIdentityBytes(
    const Render::Ogre14AuthenticatedMaterialScriptSourceMetadata& metadata)
    noexcept
{
    const std::array<const std::string*, 14U> identifiers = {
        &metadata.effective_group,
        &metadata.root_script_request,
        &metadata.compiler_file_identity,
        &metadata.archive_source_identity,
        &metadata.selected_archive_name,
        &metadata.selected_archive_type,
        &metadata.archive_sha256,
        &metadata.file_info_filename,
        &metadata.file_info_path,
        &metadata.file_info_basename,
        &metadata.exact_member_name,
        &metadata.original_sha256,
        &metadata.effective_sha256,
        &metadata.repair_plan_sha256};
    std::uint64_t total = 0U;
    for (const std::string* identifier : identifiers)
    {
        if (identifier->size() >
                Render::kOgre14AuthenticatedMaterialScriptMaximumIdentifierBytes ||
            identifier->find('\0') != std::string::npos ||
            !AddAuthenticatedMaterialScriptBytes(
                total, static_cast<std::uint64_t>(identifier->size()),
                (std::numeric_limits<std::uint64_t>::max)(), total))
        {
            return (std::numeric_limits<std::uint64_t>::max)();
        }
    }
    return total;
}

std::uint64_t AuthenticatedMaterialScriptBindingIdentityBytes(
    const Ogre::String& name,
    const Ogre::String& group,
    const Ogre::String& origin) noexcept
{
    std::uint64_t total = 0U;
    for (const Ogre::String* identifier : {&name, &group, &origin})
    {
        if (identifier->empty() ||
            identifier->size() >
                Render::kOgre14AuthenticatedMaterialScriptMaximumIdentifierBytes ||
            identifier->find('\0') != Ogre::String::npos ||
            !AddAuthenticatedMaterialScriptBytes(
                total, static_cast<std::uint64_t>(identifier->size()),
                (std::numeric_limits<std::uint64_t>::max)(), total))
        {
            return (std::numeric_limits<std::uint64_t>::max)();
        }
    }
    // The immutable registry retains group/name again in its ordered native
    // material key. Preflight the same physical accounting before creating an
    // OGRE Material so final publication cannot discover this cap too late.
    for (const Ogre::String* key_identifier : {&group, &name})
    {
        if (!AddAuthenticatedMaterialScriptBytes(
                total,
                static_cast<std::uint64_t>(key_identifier->size()),
                (std::numeric_limits<std::uint64_t>::max)(), total))
        {
            return (std::numeric_limits<std::uint64_t>::max)();
        }
    }
    return total;
}
#endif

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

#if OGRE_VERSION_MAJOR >= 14
struct ContentManager::AuthenticatedMaterialScriptGroupCandidate
{
    struct SourceStage
    {
        Render::Ogre14AuthenticatedMaterialScriptSourceInput input;
        Ogre::DataStreamPtr expected_stream;
        bool delivered = false;
    };

    struct MaterialStage
    {
        Render::Ogre14AuthenticatedMaterialScriptMaterialInput input;
        Ogre::MaterialPtr retained_material;
        std::uint64_t parse_token = 0U;
        bool finalized = false;
    };

    struct TextureStage
    {
        Render::Ogre14AuthenticatedTextureReceipt receipt;
        Ogre::TexturePtr retained_texture;
    };

    Ogre::String group;
    std::uint64_t generation = 0U;
    bool poisoned = false;
    bool parse_active = false;
    bool parse_root_open_observed = false;
    bool parse_root_untrusted = false;
    bool parse_has_authenticated_root = false;
    bool pending_import_open = false;
    std::uint64_t parse_token = 0U;
    std::uint64_t next_source_open_ordinal = 0U;
    std::uint64_t source_open_attempt_count = 0U;
    std::uint64_t next_event_ordinal = 0U;
    std::uint64_t staged_source_bytes = 0U;
    std::uint64_t staged_identity_bytes = 0U;
    std::size_t staged_source_count = 0U;
    std::size_t staged_receipt_count = 0U;
    Ogre::String root_script_request;
    Ogre::String pending_import_name;
    Ogre::String pending_import_group;
    std::vector<SourceStage> sources;
    std::vector<MaterialStage> materials;
    std::vector<TextureStage> textures;
    std::map<std::pair<std::uint64_t, Ogre::String>, std::size_t>
        source_by_compiler_file;
    Render::Ogre14AuthenticatedTextureReceiptRegistry texture_receipts;
    std::unordered_set<Ogre::String> package_materials;
    std::unordered_map<std::string, std::unordered_set<Ogre::String>>
        authenticated_materials;
    std::unordered_map<
        std::string,
        std::unordered_map<Ogre::String, Ogre::String>>
        generated_material_fallbacks;
    std::unordered_set<Ogre::String> generated_material_names;
    std::unordered_set<Ogre::String> reported_material_resolutions;
    std::unordered_map<Ogre::String, std::string>
        authorized_texture_fallbacks;
    std::unordered_set<Ogre::String> reported_texture_fallbacks;
};
#endif

ContentManager::ContentManager():
    m_base_resource_loaded(false),
    m_resource_group_listener_registered(false)
{
#if OGRE_VERSION_MAJOR >= 14
    const Render::ValidationResult texture_registry_initialization =
        Render::InitializeOgre14AuthenticatedTextureReceiptRegistry(
            m_authenticated_texture_receipt_configuration,
            m_authenticated_texture_receipts);
    if (!texture_registry_initialization)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Could not initialize authenticated source-texture receipt "
                "registry: {} ({})",
                texture_registry_initialization.detail,
                texture_registry_initialization.field),
            "ContentManager::ContentManager");
    }
    const Render::ValidationResult selected_texture_initialization =
        Render::InitializeOgre14SelectedTextureSourceRegistry(
            m_selected_texture_source_configuration,
            m_selected_texture_sources);
    if (!selected_texture_initialization)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Could not initialize selected source-texture receipt "
                "registry: {} ({})",
                selected_texture_initialization.detail,
                selected_texture_initialization.field),
            "ContentManager::ContentManager");
    }
    const Render::ValidationResult material_script_initialization =
        m_authenticated_material_scripts.Initialize(
            m_authenticated_material_script_configuration);
    if (!material_script_initialization)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Could not initialize authenticated material-script receipt "
                "registry: {} ({})",
                material_script_initialization.detail,
                material_script_initialization.field),
            "ContentManager::ContentManager");
    }
#endif
}

ContentManager::~ContentManager()
{
#if OGRE_VERSION_MAJOR >= 14
    m_authenticated_material_script_candidate.reset();
    m_aborted_material_script_candidate.reset();
#endif
    if (Ogre::ScriptCompilerManager::getSingletonPtr() != nullptr &&
        Ogre::ScriptCompilerManager::getSingleton().getListener() == this)
    {
        Ogre::ScriptCompilerManager::getSingleton().setListener(nullptr);
    }
    if (Ogre::ResourceGroupManager::getSingletonPtr() != nullptr &&
        Ogre::ResourceGroupManager::getSingleton().getLoadingListener() == this)
    {
        Ogre::ResourceGroupManager::getSingleton().setLoadingListener(nullptr);
    }
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

#if OGRE_VERSION_MAJOR >= 14
void ContentManager::BindAuthenticatedResourceThread()
{
    if (!m_authenticated_resource_thread_gate.BindCurrentThread())
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated resource lifecycle is bound to another serialized "
            "OGRE resource/render thread",
            "ContentManager::BindAuthenticatedResourceThread");
    }
}

bool ContentManager::IsAuthenticatedResourceThread() const noexcept
{
    return m_authenticated_resource_thread_gate.IsCurrentThreadOrUnbound();
}

void ContentManager::RequireAuthenticatedResourceThread(
    const char* operation) const
{
    if (!this->IsAuthenticatedResourceThread())
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated resource lifecycle crossed its serialized OGRE "
            "resource/render thread boundary",
            operation != nullptr ? operation :
                "ContentManager::RequireAuthenticatedResourceThread");
    }
}

Render::ValidationResult
ContentManager::CaptureAuthenticatedTextureAuthoritySnapshot(
    Render::Ogre14AuthenticatedTextureAuthoritySnapshot& snapshot) const
{
    this->RequireAuthenticatedResourceThread(
        "ContentManager::CaptureAuthenticatedTextureAuthoritySnapshot");
    try
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const std::uintptr_t resolver_pointer_token =
            reinterpret_cast<std::uintptr_t>(
                static_cast<const Render::IOgre14AuthenticatedTextureResolver*>(
                    this));
        Render::Ogre14AuthenticatedTextureAuthoritySnapshot candidate;
        const Render::ValidationResult mint =
            m_authenticated_texture_receipts.MintResolverAuthoritySnapshot(
                resolver_pointer_token, candidate);
        if (!mint)
        {
            return mint;
        }
        static_assert(std::is_nothrow_move_assignable_v<
            Render::Ogre14AuthenticatedTextureAuthoritySnapshot>);
        snapshot = std::move(candidate);
        return Render::ValidationResult::Success();
    }
    catch (const std::bad_alloc&)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::EMPTY_PAYLOAD,
            "texture_authority.allocation",
            "allocation failed before texture authority publication");
    }
    catch (...)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::UNSUPPORTED_FEATURE,
            "texture_authority.exception",
            "unexpected exception before texture authority publication");
    }
}

bool ContentManager::RequiresAuthenticatedTextureSource(
    Ogre::Texture& texture) const noexcept
{
    try
    {
        this->RequireAuthenticatedResourceThread(
            "ContentManager::RequiresAuthenticatedTextureSource");
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        Ogre::TextureManager* manager =
            Ogre::TextureManager::getSingletonPtr();
        if (manager == nullptr || texture.getCreator() != manager ||
            manager->getResourceType() != "Texture" || !texture.isLoaded() ||
            texture.getName().empty() || texture.getGroup().empty() ||
            !m_authenticated_texture_receipts.initialized())
        {
            // An invalid live identity or poisoned registry can never authorize
            // the less-trusted GPU path.
            return true;
        }
        const Ogre::ResourcePtr by_handle =
            manager->getByHandle(texture.getHandle());
        const Ogre::ResourcePtr by_name = manager->getResourceByName(
            texture.getName(), texture.getGroup());
        if (!by_handle || !by_name || by_handle.get() != &texture ||
            by_name.get() != &texture)
        {
            return true;
        }

        const auto authenticated_group =
            m_authenticated_package_archives_by_group.find(
                texture.getGroup());
        const auto authenticated_binding_group =
            m_authenticated_package_archive_bindings_by_group.find(
                texture.getGroup());
        const bool archive_map_absent =
            authenticated_group ==
            m_authenticated_package_archives_by_group.end();
        const bool binding_map_absent =
            authenticated_binding_group ==
            m_authenticated_package_archive_bindings_by_group.end();
        if (archive_map_absent && binding_map_absent)
        {
            return false;
        }
        // Any one-map, empty-map, generation, or archive-binding inconsistency
        // remains authenticated-required. Resolve() will then reject the exact
        // missing/corrupt receipt instead of laundering it through readback.
        // A normal first-location-wins resource is eligible for readback only
        // when both authenticated-package authority maps are consistently
        // absent for its exact group.
        return true;
    }
    catch (...)
    {
        return true;
    }
}

Render::ValidationResult ContentManager::ResolveAuthenticatedTexture(
    Ogre::Texture& texture,
    Render::Ogre14AuthenticatedTextureResolution& resolution) const
{
    this->RequireAuthenticatedResourceThread(
        "ContentManager::ResolveAuthenticatedTexture");
    // Pinned OGRE 14.5.2 increments Resource::mStateCount exactly once after
    // a successful load, but the counter itself is not atomic. This resolver
    // therefore authenticates a serialized render/resource-thread observation;
    // the mutex protects only RoR's immutable registry publication.
    try
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        Ogre::TextureManager* manager =
            Ogre::TextureManager::getSingletonPtr();
        if (manager == nullptr || texture.getCreator() != manager ||
            manager->getResourceType() != "Texture")
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_ASSET_REFERENCE,
                "texture_resolution.texture_manager",
                "loaded resource is not owned by the active TextureManager");
        }
        if (!texture.isLoaded())
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "texture_resolution.loaded",
                "authenticated texture must be completely loaded");
        }

        const Ogre::ResourcePtr by_handle =
            manager->getByHandle(texture.getHandle());
        const Ogre::ResourcePtr by_name = manager->getResourceByName(
            texture.getName(), texture.getGroup());
        if (!by_handle || !by_name || by_handle.get() != &texture ||
            by_name.get() != &texture)
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_HANDLE,
                "texture_resolution.texture_manager",
                "TextureManager indices do not resolve to the exact texture pointer");
        }

        const auto generation =
            m_legacy_material_group_generations.find(texture.getGroup());
        if (generation == m_legacy_material_group_generations.end())
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "texture_resolution.group_generation",
                "texture group has no active authenticated generation");
        }
        const std::size_t native_state_count = texture.getStateCount();
        const std::uint64_t loaded_state_count =
            static_cast<std::uint64_t>(native_state_count);
        if (static_cast<std::size_t>(loaded_state_count) !=
            native_state_count)
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "texture_resolution.resource_state_count",
                "texture state count exceeds the authenticated integer range");
        }

        const std::uintptr_t resolver_pointer_token =
            reinterpret_cast<std::uintptr_t>(
                static_cast<const Render::IOgre14AuthenticatedTextureResolver*>(
                    this));
        Render::Ogre14AuthenticatedTextureResolution candidate;
        const Render::ValidationResult mint =
            m_authenticated_texture_receipts.MintLoadedResourceResolution(
                texture.getGroup(), generation->second,
                reinterpret_cast<std::uintptr_t>(&texture),
                static_cast<std::uint64_t>(texture.getHandle()),
                texture.getName(), loaded_state_count,
                resolver_pointer_token, candidate);
        if (!mint)
        {
            return mint;
        }

        // Re-observe every live identity after the allocating mint. Publication
        // is allowed only if the exact manager indices and registry snapshot
        // are still current.
        const Ogre::ResourcePtr current_by_handle =
            manager->getByHandle(texture.getHandle());
        const Ogre::ResourcePtr current_by_name = manager->getResourceByName(
            texture.getName(), texture.getGroup());
        if (!texture.isLoaded() || current_by_handle.get() != &texture ||
            current_by_name.get() != &texture ||
            texture.getStateCount() != native_state_count ||
            !m_authenticated_texture_receipts.
                RevalidateLoadedResourceResolution(
                    candidate, resolver_pointer_token,
                    reinterpret_cast<std::uintptr_t>(&texture),
                    static_cast<std::uint64_t>(texture.getHandle()),
                    texture.getGroup(), texture.getName(),
                    loaded_state_count))
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "texture_resolution.revalidation",
                "texture or registry changed while minting its authenticated resolution");
        }

        resolution = std::move(candidate);
        return Render::ValidationResult::Success();
    }
    catch (const Ogre::Exception&)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::MISSING_REFERENCE,
            "texture_resolution.ogre_exception",
            "OGRE rejected the exact TextureManager identity lookup");
    }
    catch (const std::bad_alloc&)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::EMPTY_PAYLOAD,
            "texture_resolution.allocation",
            "allocation failed before texture resolution publication");
    }
    catch (...)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::UNSUPPORTED_FEATURE,
            "texture_resolution.exception",
            "unexpected exception before texture resolution publication");
    }
}

bool ContentManager::RevalidateAuthenticatedTexture(
    Ogre::Texture& texture,
    const Render::Ogre14AuthenticatedTextureResolution& resolution) const
    noexcept
{
    try
    {
        this->RequireAuthenticatedResourceThread(
            "ContentManager::RevalidateAuthenticatedTexture");
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        Ogre::TextureManager* manager =
            Ogre::TextureManager::getSingletonPtr();
        if (manager == nullptr || texture.getCreator() != manager ||
            manager->getResourceType() != "Texture" ||
            !texture.isLoaded())
        {
            return false;
        }
        const Ogre::ResourcePtr by_handle =
            manager->getByHandle(texture.getHandle());
        const Ogre::ResourcePtr by_name = manager->getResourceByName(
            texture.getName(), texture.getGroup());
        if (!by_handle || !by_name || by_handle.get() != &texture ||
            by_name.get() != &texture)
        {
            return false;
        }
        const std::size_t native_state_count = texture.getStateCount();
        const std::uint64_t loaded_state_count =
            static_cast<std::uint64_t>(native_state_count);
        if (static_cast<std::size_t>(loaded_state_count) !=
            native_state_count)
        {
            return false;
        }
        const std::uintptr_t resolver_pointer_token =
            reinterpret_cast<std::uintptr_t>(
                static_cast<const Render::IOgre14AuthenticatedTextureResolver*>(
                    this));
        return m_authenticated_texture_receipts.
            RevalidateLoadedResourceResolution(
                resolution, resolver_pointer_token,
                reinterpret_cast<std::uintptr_t>(&texture),
                static_cast<std::uint64_t>(texture.getHandle()),
                texture.getGroup(), texture.getName(), loaded_state_count);
    }
    catch (...)
    {
        return false;
    }
}

Render::ValidationResult ContentManager::ResolveSelectedTextureSource(
    Ogre::Texture& texture,
    Render::Ogre14SelectedTextureSourceResolution& resolution) const
{
    this->RequireAuthenticatedResourceThread(
        "ContentManager::ResolveSelectedTextureSource");
    try
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        Ogre::TextureManager* manager =
            Ogre::TextureManager::getSingletonPtr();
        if (manager == nullptr || texture.getCreator() != manager ||
            manager->getResourceType() != "Texture")
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_ASSET_REFERENCE,
                "selected_texture_resolution.texture_manager",
                "loaded resource is not owned by the active TextureManager");
        }
        if (!texture.isLoaded())
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "selected_texture_resolution.loaded",
                "selected source texture must be completely loaded");
        }
        const auto package_group =
            m_package_archives_by_group.find(texture.getGroup());
        if (m_authenticated_package_archives_by_group.find(
                texture.getGroup()) !=
                m_authenticated_package_archives_by_group.end() ||
            m_authenticated_package_archive_bindings_by_group.find(
                texture.getGroup()) !=
                m_authenticated_package_archive_bindings_by_group.end())
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_ASSET_REFERENCE,
                "selected_texture_resolution.source_mode",
                "texture group is not a live ordinary package source");
        }
        if (package_group == m_package_archives_by_group.end() ||
            package_group->second.empty())
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "selected_texture_resolution.package_marker",
                "texture group has no live ordinary package marker");
        }
        const Ogre::ResourcePtr by_handle =
            manager->getByHandle(texture.getHandle());
        const Ogre::ResourcePtr by_name = manager->getResourceByName(
            texture.getName(), texture.getGroup());
        if (!by_handle || !by_name || by_handle.get() != &texture ||
            by_name.get() != &texture)
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_HANDLE,
                "selected_texture_resolution.texture_manager",
                "TextureManager indices do not resolve to the exact texture pointer");
        }
        const auto generation =
            m_legacy_material_group_generations.find(texture.getGroup());
        if (generation == m_legacy_material_group_generations.end())
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "selected_texture_resolution.group_generation",
                "texture group has no active selected-source generation");
        }
        const std::size_t native_state_count = texture.getStateCount();
        const std::uint64_t loaded_state_count =
            static_cast<std::uint64_t>(native_state_count);
        if (static_cast<std::size_t>(loaded_state_count) !=
            native_state_count)
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "selected_texture_resolution.resource_state_count",
                "texture state count exceeds the selected-source integer range");
        }
        const std::uintptr_t resolver_pointer_token =
            reinterpret_cast<std::uintptr_t>(
                static_cast<const
                    Render::IOgre14SelectedTextureSourceResolver*>(this));
        Render::Ogre14SelectedTextureSourceResolution candidate;
        const Render::ValidationResult mint =
            m_selected_texture_sources.MintLoadedResourceResolution(
                texture.getGroup(), generation->second,
                reinterpret_cast<std::uintptr_t>(&texture),
                static_cast<std::uint64_t>(texture.getHandle()),
                texture.getName(), loaded_state_count,
                resolver_pointer_token, candidate);
        if (!mint)
        {
            return mint;
        }
        const auto* receipt = candidate.source_receipt();
        const auto* metadata =
            receipt != nullptr ? receipt->metadata() : nullptr;
        if (metadata == nullptr ||
            package_group->second.count(
                metadata->source.selected_archive_name) != 1U)
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_ASSET_REFERENCE,
                "selected_texture_resolution.package_archive",
                "selected source archive is not registered for the texture group");
        }
        const Ogre::ResourcePtr current_by_handle =
            manager->getByHandle(texture.getHandle());
        const Ogre::ResourcePtr current_by_name =
            manager->getResourceByName(
                texture.getName(), texture.getGroup());
        if (!texture.isLoaded() || current_by_handle.get() != &texture ||
            current_by_name.get() != &texture ||
            texture.getStateCount() != native_state_count ||
            !m_selected_texture_sources.RevalidateLoadedResourceResolution(
                candidate, resolver_pointer_token,
                reinterpret_cast<std::uintptr_t>(&texture),
                static_cast<std::uint64_t>(texture.getHandle()),
                texture.getGroup(), texture.getName(), loaded_state_count))
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "selected_texture_resolution.revalidation",
                "texture or registry changed while minting its selected-source resolution");
        }
        resolution = std::move(candidate);
        return Render::ValidationResult::Success();
    }
    catch (const Ogre::Exception&)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::MISSING_REFERENCE,
            "selected_texture_resolution.ogre_exception",
            "OGRE rejected the exact TextureManager identity lookup");
    }
    catch (const std::bad_alloc&)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::EMPTY_PAYLOAD,
            "selected_texture_resolution.allocation",
            "allocation failed before selected-source resolution publication");
    }
    catch (...)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::UNSUPPORTED_FEATURE,
            "selected_texture_resolution.exception",
            "unexpected exception before selected-source resolution publication");
    }
}

bool ContentManager::RevalidateSelectedTextureSource(
    Ogre::Texture& texture,
    const Render::Ogre14SelectedTextureSourceResolution& resolution) const
    noexcept
{
    try
    {
        this->RequireAuthenticatedResourceThread(
            "ContentManager::RevalidateSelectedTextureSource");
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        Ogre::TextureManager* manager =
            Ogre::TextureManager::getSingletonPtr();
        if (manager == nullptr || texture.getCreator() != manager ||
            manager->getResourceType() != "Texture" ||
            !texture.isLoaded())
        {
            return false;
        }
        const auto package_group =
            m_package_archives_by_group.find(texture.getGroup());
        if (package_group == m_package_archives_by_group.end() ||
            package_group->second.empty() ||
            m_authenticated_package_archives_by_group.find(
                texture.getGroup()) !=
                m_authenticated_package_archives_by_group.end() ||
            m_authenticated_package_archive_bindings_by_group.find(
                texture.getGroup()) !=
                m_authenticated_package_archive_bindings_by_group.end())
        {
            return false;
        }
        const Ogre::ResourcePtr by_handle =
            manager->getByHandle(texture.getHandle());
        const Ogre::ResourcePtr by_name = manager->getResourceByName(
            texture.getName(), texture.getGroup());
        if (!by_handle || !by_name || by_handle.get() != &texture ||
            by_name.get() != &texture)
        {
            return false;
        }
        const auto generation =
            m_legacy_material_group_generations.find(texture.getGroup());
        const auto* receipt = resolution.source_receipt();
        const auto* metadata =
            receipt != nullptr ? receipt->metadata() : nullptr;
        if (generation == m_legacy_material_group_generations.end() ||
            metadata == nullptr ||
            metadata->source.effective_resource_group != texture.getGroup() ||
            metadata->source.group_generation != generation->second ||
            package_group->second.count(
                metadata->source.selected_archive_name) != 1U)
        {
            return false;
        }
        const std::size_t native_state_count = texture.getStateCount();
        const std::uint64_t loaded_state_count =
            static_cast<std::uint64_t>(native_state_count);
        if (static_cast<std::size_t>(loaded_state_count) !=
            native_state_count)
        {
            return false;
        }
        const std::uintptr_t resolver_pointer_token =
            reinterpret_cast<std::uintptr_t>(
                static_cast<const
                    Render::IOgre14SelectedTextureSourceResolver*>(this));
        return m_selected_texture_sources.
            RevalidateLoadedResourceResolution(
                resolution, resolver_pointer_token,
                reinterpret_cast<std::uintptr_t>(&texture),
                static_cast<std::uint64_t>(texture.getHandle()),
                texture.getGroup(), texture.getName(), loaded_state_count);
    }
    catch (...)
    {
        return false;
    }
}

Render::ValidationResult
ContentManager::ResolveAuthenticatedMaterialScript(
    Ogre::Material& material,
    Render::Ogre14AuthenticatedMaterialScriptResolution& resolution) const
{
    this->RequireAuthenticatedResourceThread(
        "ContentManager::ResolveAuthenticatedMaterialScript");
    try
    {
        std::scoped_lock<std::mutex, std::mutex> material_lock(
            m_legacy_material_resolution_mutex,
            m_legacy_material_state_mutex);
        Ogre::MaterialManager* manager =
            Ogre::MaterialManager::getSingletonPtr();
        if (manager == nullptr || material.getCreator() != manager ||
            manager->getResourceType() != "Material")
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_ASSET_REFERENCE,
                "material_script_resolution.material_manager",
                "resource is not owned by the active MaterialManager");
        }
        const Ogre::ResourcePtr by_handle =
            manager->getByHandle(material.getHandle());
        const Ogre::ResourcePtr by_name = manager->getResourceByName(
            material.getName(), material.getGroup());
        if (!by_handle || !by_name || by_handle.get() != &material ||
            by_name.get() != &material)
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::INVALID_HANDLE,
                "material_script_resolution.material_manager",
                "MaterialManager indices do not resolve to the exact material pointer");
        }
        const auto generation =
            m_legacy_material_group_generations.find(material.getGroup());
        if (generation == m_legacy_material_group_generations.end())
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "material_script_resolution.group_generation",
                "material group has no current authenticated generation");
        }
        const std::uintptr_t resolver_pointer_token =
            reinterpret_cast<std::uintptr_t>(
                static_cast<const
                    Render::IOgre14AuthenticatedMaterialScriptResolver*>(
                        this));
        Render::Ogre14AuthenticatedMaterialScriptResolution candidate;
        const Render::ValidationResult mint =
            m_authenticated_material_scripts.MintResolution(
                material.getGroup(), generation->second,
                reinterpret_cast<std::uintptr_t>(&material),
                static_cast<std::uint64_t>(material.getHandle()),
                material.getName(), material.getOrigin(),
                resolver_pointer_token, candidate);
        if (!mint)
        {
            return mint;
        }
        const Ogre::ResourcePtr current_by_handle =
            manager->getByHandle(material.getHandle());
        const Ogre::ResourcePtr current_by_name = manager->getResourceByName(
            material.getName(), material.getGroup());
        if (current_by_handle.get() != &material ||
            current_by_name.get() != &material ||
            !m_authenticated_material_scripts.RevalidateResolution(
                candidate, resolver_pointer_token,
                reinterpret_cast<std::uintptr_t>(&material),
                static_cast<std::uint64_t>(material.getHandle()),
                material.getGroup(), material.getName(),
                material.getOrigin()))
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::REVISION_MISMATCH,
                "material_script_resolution.revalidation",
                "material or registry changed while minting its authenticated resolution");
        }
        resolution = std::move(candidate);
        return Render::ValidationResult::Success();
    }
    catch (const std::bad_alloc&)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::EMPTY_PAYLOAD,
            "material_script_resolution.allocation",
            "allocation failed before material resolution publication");
    }
    catch (...)
    {
        return Render::ValidationResult::Failure(
            Render::ValidationCode::UNSUPPORTED_FEATURE,
            "material_script_resolution.exception",
            "unexpected exception before material resolution publication");
    }
}

bool ContentManager::RevalidateAuthenticatedMaterialScript(
    Ogre::Material& material,
    const Render::Ogre14AuthenticatedMaterialScriptResolution& resolution)
    const noexcept
{
    try
    {
        this->RequireAuthenticatedResourceThread(
            "ContentManager::RevalidateAuthenticatedMaterialScript");
        std::scoped_lock<std::mutex, std::mutex> material_lock(
            m_legacy_material_resolution_mutex,
            m_legacy_material_state_mutex);
        Ogre::MaterialManager* manager =
            Ogre::MaterialManager::getSingletonPtr();
        if (manager == nullptr || material.getCreator() != manager ||
            manager->getResourceType() != "Material")
        {
            return false;
        }
        const Ogre::ResourcePtr by_handle =
            manager->getByHandle(material.getHandle());
        const Ogre::ResourcePtr by_name = manager->getResourceByName(
            material.getName(), material.getGroup());
        if (!by_handle || !by_name || by_handle.get() != &material ||
            by_name.get() != &material)
        {
            return false;
        }
        const std::uintptr_t resolver_pointer_token =
            reinterpret_cast<std::uintptr_t>(
                static_cast<const
                    Render::IOgre14AuthenticatedMaterialScriptResolver*>(
                        this));
        const Render::Ogre14AuthenticatedMaterialScriptReceipt* receipt =
            resolution.receipt();
        const Render::Ogre14AuthenticatedMaterialScriptSourceMetadata* source =
            receipt != nullptr ? receipt->source_metadata() : nullptr;
        const auto generation =
            m_legacy_material_group_generations.find(material.getGroup());
        if (source == nullptr ||
            source->effective_group != material.getGroup() ||
            generation == m_legacy_material_group_generations.end() ||
            generation->second != source->group_generation)
        {
            return false;
        }
        return m_authenticated_material_scripts.RevalidateResolution(
            resolution, resolver_pointer_token,
            reinterpret_cast<std::uintptr_t>(&material),
            static_cast<std::uint64_t>(material.getHandle()),
            material.getGroup(), material.getName(), material.getOrigin());
    }
    catch (...)
    {
        return false;
    }
}
#endif

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

#if OGRE_VERSION_MAJOR >= 14
void ContentManager::EraseSelectedTextureSourceStageLocked(
    const Ogre::Resource* resource) noexcept
{
    const auto stage = m_selected_texture_source_stages.find(resource);
    if (stage == m_selected_texture_source_stages.end())
    {
        return;
    }
    m_selected_texture_source_stages.erase(stage);
    // Recompute both exact aggregate charges from every surviving stage.
    // Saturation fails closed if an impossible accounting value is ever
    // observed; it must never make another pending stream appear cheaper.
    std::uint64_t recomputed_source = 0U;
    std::uint64_t recomputed_identity = 0U;
    for (const auto& remaining : m_selected_texture_source_stages)
    {
        const std::uint64_t remaining_source =
            remaining.second.retained_source_charge;
        if (recomputed_source >
            (std::numeric_limits<std::uint64_t>::max)() -
                remaining_source)
        {
            recomputed_source =
                (std::numeric_limits<std::uint64_t>::max)();
        }
        else
        {
            recomputed_source += remaining_source;
        }
        const std::uint64_t remaining_identity =
            remaining.second.retained_identity_charge;
        if (recomputed_identity >
            (std::numeric_limits<std::uint64_t>::max)() -
                remaining_identity)
        {
            recomputed_identity =
                (std::numeric_limits<std::uint64_t>::max)();
        }
        else
        {
            recomputed_identity += remaining_identity;
        }
    }
    m_selected_texture_source_staged_bytes = recomputed_source;
    m_selected_texture_source_staged_identity_bytes = recomputed_identity;
}

void ContentManager::EraseSelectedTextureSourceStagesForGroupLocked(
    const Ogre::String& resource_group) noexcept
{
    for (auto stage = m_selected_texture_source_stages.begin();
         stage != m_selected_texture_source_stages.end();)
    {
        const auto* metadata = stage->second.receipt.metadata();
        if (metadata == nullptr ||
            metadata->source.effective_resource_group == resource_group)
        {
            const Ogre::Resource* resource = stage->first;
            ++stage;
            this->EraseSelectedTextureSourceStageLocked(resource);
        }
        else
        {
            ++stage;
        }
    }
}
#endif

std::uint64_t ContentManager::AdvanceLegacyMaterialGroupGenerationLocked(
    const Ogre::String& resource_group)
{
#if OGRE_VERSION_MAJOR >= 14
    if (m_legacy_material_group_generations.find(resource_group) ==
            m_legacy_material_group_generations.end() &&
        m_legacy_material_group_generations.size() >=
            Render::kOgre14AuthenticatedTextureMaximumGroupRecords)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated resource-group record capacity is exhausted",
            "ContentManager::AdvanceLegacyMaterialGroupGenerationLocked");
    }
    if (m_next_legacy_material_group_generation ==
        (std::numeric_limits<std::uint64_t>::max)())
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated resource-group generation space is exhausted",
            "ContentManager::AdvanceLegacyMaterialGroupGenerationLocked");
    }
    const std::uint64_t next_generation =
        m_next_legacy_material_group_generation + 1U;
    Render::Ogre14AuthenticatedTextureReceiptRegistry texture_candidate =
        m_authenticated_texture_receipts;
    const Render::ValidationResult texture_transition =
        Render::AdvanceOgre14AuthenticatedTextureGroupGeneration(
            resource_group, next_generation, texture_candidate);
    if (!texture_transition)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated source-texture group '{}' could not advance "
                "to generation {}: {} ({})",
                resource_group,
                next_generation,
                texture_transition.detail,
                texture_transition.field),
            "ContentManager::AdvanceLegacyMaterialGroupGenerationLocked");
    }
    Render::Ogre14SelectedTextureSourceReceiptRegistry
        selected_texture_candidate = m_selected_texture_sources;
    const Render::ValidationResult selected_texture_transition =
        Render::AdvanceOgre14SelectedTextureSourceGroupGeneration(
            resource_group, next_generation, selected_texture_candidate);
    if (!selected_texture_transition)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Selected source-texture group '{}' could not advance to "
                "generation {}: {} ({})",
                resource_group,
                next_generation,
                selected_texture_transition.detail,
                selected_texture_transition.field),
            "ContentManager::AdvanceLegacyMaterialGroupGenerationLocked");
    }
    Render::Ogre14AuthenticatedMaterialScriptRegistry
        material_script_candidate = m_authenticated_material_scripts;
    const Render::ValidationResult material_script_transition =
        material_script_candidate.AdvanceGroupGeneration(
            resource_group, next_generation);
    if (!material_script_transition)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated material-script group '{}' could not advance "
                "to generation {}: {} ({})",
                resource_group,
                next_generation,
                material_script_transition.detail,
                material_script_transition.field),
            "ContentManager::AdvanceLegacyMaterialGroupGenerationLocked");
    }
    m_legacy_material_group_generations[resource_group] =
        next_generation;
    m_next_legacy_material_group_generation = next_generation;
    this->EraseSelectedTextureSourceStagesForGroupLocked(resource_group);
    m_authenticated_texture_receipts = std::move(texture_candidate);
    m_selected_texture_sources = std::move(selected_texture_candidate);
    m_authenticated_material_scripts =
        std::move(material_script_candidate);
    // Generation authority and all compatibility indexes transition under the
    // same state lock. Old fallbacks must never be interpreted as belonging to
    // the new generation, even if the caller later fails while parsing it.
    m_package_materials_by_group.erase(resource_group);
    m_authenticated_materials_by_group.erase(resource_group);
    m_generated_material_fallbacks_by_group.erase(resource_group);
    m_generated_material_names_by_group.erase(resource_group);
    m_generated_material_bindings_by_group.erase(resource_group);
    m_reported_material_resolutions_by_group.erase(resource_group);
    m_authorized_texture_fallbacks_by_group.erase(resource_group);
    m_reported_texture_fallbacks_by_group.erase(resource_group);
    m_committed_material_script_generations.erase(resource_group);
    if (m_authenticated_material_script_candidate != nullptr &&
        m_authenticated_material_script_candidate->group == resource_group)
    {
        m_authenticated_material_script_candidate.reset();
    }
    const auto authenticated_archive_group =
        m_authenticated_package_archive_bindings_by_group.find(
            resource_group);
    if (authenticated_archive_group !=
        m_authenticated_package_archive_bindings_by_group.end())
    {
        for (auto& archive_binding : authenticated_archive_group->second)
        {
            archive_binding.second.group_generation = next_generation;
        }
    }
#else
    ++m_next_legacy_material_group_generation;
    if (m_next_legacy_material_group_generation == 0U)
    {
        ++m_next_legacy_material_group_generation;
    }
    m_legacy_material_group_generations[resource_group] =
        m_next_legacy_material_group_generation;
#endif
    this->EraseAuthenticatedMeshBindingsForGroupLocked(resource_group);
    return m_next_legacy_material_group_generation;
}

void ContentManager::RegisterPackageResourceLocation(
    const Ogre::String& resource_group,
    const Ogre::String& archive_name)
{
#if OGRE_VERSION_MAJOR >= 14
    this->BindAuthenticatedResourceThread();
#endif
    std::scoped_lock<std::mutex, std::mutex> legacy_material_lock(
        m_legacy_material_resolution_mutex,
        m_legacy_material_state_mutex);
    auto package_archives_candidate = m_package_archives_by_group;
#if OGRE_VERSION_MAJOR >= 14
    if (package_archives_candidate.find(resource_group) ==
            package_archives_candidate.end() &&
        package_archives_candidate.size() >=
            Render::kOgre14SelectedTextureSourceMaximumGroupRecords)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Ordinary package resource-group capacity is exhausted",
            "ContentManager::RegisterPackageResourceLocation");
    }
#endif
    auto& archive_names = package_archives_candidate[resource_group];
#if OGRE_VERSION_MAJOR >= 14
    if (archive_names.count(archive_name) == 0U &&
        archive_names.size() >=
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Ordinary package archive-binding capacity is exhausted",
            "ContentManager::RegisterPackageResourceLocation");
    }
#endif
    archive_names.insert(archive_name);
    this->AdvanceLegacyMaterialGroupGenerationLocked(resource_group);
    static_assert(noexcept(m_package_archives_by_group.swap(
        package_archives_candidate)));
    m_package_archives_by_group.swap(package_archives_candidate);
}

void ContentManager::MountAuthenticatedPackageResourceLocation(
    const Ogre::String& resource_group,
    const TerrainBundleAuthenticatedArchiveSnapshot& archive_snapshot
#if OGRE_VERSION_MAJOR >= 14
    , Render::IOgre14AuthenticatedArchiveMountFaultInjector* fault_injector
#endif
    )
{
#if OGRE_VERSION_MAJOR >= 14
    if (resource_group.empty() ||
        !archive_snapshot.initialized() ||
        archive_snapshot.version() !=
            TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_SNAPSHOT_VERSION ||
        archive_snapshot.source_archive_identity().empty() ||
        !Render::IsLowercaseOgre14Sha256(
            archive_snapshot.archive_sha256()) ||
        archive_snapshot.bytes() == nullptr ||
        archive_snapshot.size() == 0U ||
        static_cast<std::uint64_t>(archive_snapshot.size()) >
            TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_BYTES)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALIDPARAMS,
            "Authenticated package mount requires one complete immutable "
            "archive snapshot",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }

    TerrainBundleAuthenticatedArchivePreflight archive_preflight;
    std::string archive_preflight_error;
    if (!BuildTerrainBundleAuthenticatedArchivePreflight(
            archive_snapshot,
            archive_preflight,
            archive_preflight_error))
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALIDPARAMS,
            fmt::format(
                "Authenticated package ZIP preflight failed before mount: {}",
                archive_preflight_error),
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    std::unordered_map<Ogre::String, std::size_t> preflight_members;
    preflight_members.reserve(archive_preflight.members.size());
    std::size_t preflight_file_member_count = 0U;
    for (std::size_t index = 0U;
         index < archive_preflight.members.size();
         ++index)
    {
        const TerrainBundleAuthenticatedArchiveMemberPreflight& member =
            archive_preflight.members[index];
        if (!preflight_members.emplace(
                 member.exact_member_name, index).second)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_DUPLICATE_ITEM,
                "Authenticated package ZIP preflight retained a duplicate "
                "member identity",
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }
        if (!member.directory)
        {
            ++preflight_file_member_count;
        }
    }
    if (preflight_file_member_count == 0U)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALIDPARAMS,
            "Authenticated package ZIP preflight contains no file members",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }

    // Pure caller-data validation above must not capture process-lifetime
    // thread authority. Bind immediately before the first authenticated state
    // or external OGRE observation.
    this->BindAuthenticatedResourceThread();

    std::scoped_lock<std::mutex, std::mutex, std::mutex>
        legacy_material_lock(
            m_legacy_material_archive_io_mutex,
            m_legacy_material_resolution_mutex,
            m_legacy_material_state_mutex);
    if (m_next_legacy_material_group_generation ==
        (std::numeric_limits<std::uint64_t>::max)())
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated package generation space is exhausted",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    if (m_authenticated_package_archive_retained_bytes >
        Render::kOgre14AuthenticatedTextureMaximumRetainedBytes -
            static_cast<std::uint64_t>(archive_snapshot.size()))
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated package archive snapshots exceed their retained "
            "byte cap",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    if (m_authenticated_package_archive_manifest_accounting.
                live_binding_count >=
            Render::kOgre14AuthenticatedTextureMaximumLiveReceipts ||
        m_authenticated_package_archive_manifest_accounting.
                live_member_count >=
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates ||
        m_authenticated_package_archive_manifest_accounting.
                retained_member_identity_bytes >=
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberIdentityBytes)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated package archive-manifest process capacity is "
            "exhausted",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }

    Ogre::ResourceGroupManager& resource_manager =
        Ogre::ResourceGroupManager::getSingleton();
    if (!resource_manager.resourceGroupExists(resource_group))
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_ITEM_NOT_FOUND,
            fmt::format(
                "Authenticated package target resource group '{}' does "
                "not exist",
                resource_group),
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }

    const std::uint64_t mount_generation =
        m_next_legacy_material_group_generation + 1U;
    const std::uint64_t registration_id =
        AllocateAuthenticatedEmbeddedZipRegistrationId();
    const Ogre::String selected_archive_name = fmt::format(
        "ror-authenticated-embedded-zip-v1-{}-g{}-r{}",
        archive_snapshot.archive_sha256(),
        mount_generation,
        registration_id);

    Ogre::ArchiveManager& archive_manager =
        Ogre::ArchiveManager::getSingleton();
    Ogre::ArchiveManager::ArchiveMapIterator existing_archives =
        archive_manager.getArchiveIterator();
    while (existing_archives.hasMoreElements())
    {
        if (existing_archives.peekNextKey() == selected_archive_name)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_DUPLICATE_ITEM,
                fmt::format(
                    "Authenticated synthetic archive identity '{}' already "
                    "exists",
                    existing_archives.peekNextKey()),
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }
        existing_archives.getNext();
    }

    if ((m_package_archives_by_group.find(resource_group) ==
             m_package_archives_by_group.end() &&
         m_package_archives_by_group.size() >=
             Render::kOgre14AuthenticatedTextureMaximumGroupRecords) ||
        (m_authenticated_package_archives_by_group.find(resource_group) ==
             m_authenticated_package_archives_by_group.end() &&
         m_authenticated_package_archives_by_group.size() >=
             Render::kOgre14AuthenticatedTextureMaximumGroupRecords) ||
        (m_authenticated_package_archive_bindings_by_group.find(
             resource_group) ==
             m_authenticated_package_archive_bindings_by_group.end() &&
         m_authenticated_package_archive_bindings_by_group.size() >=
             Render::kOgre14AuthenticatedTextureMaximumGroupRecords) ||
        (m_legacy_material_group_generations.find(resource_group) ==
             m_legacy_material_group_generations.end() &&
         m_legacy_material_group_generations.size() >=
             Render::kOgre14AuthenticatedTextureMaximumGroupRecords) ||
        m_authenticated_mesh_bindings.size() >
            Render::kOgre14AuthenticatedTextureMaximumLiveReceipts)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated package mount state exceeds its bounded group or "
            "resource limit",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }

    // Build every allocation-bearing state transition before exposing the
    // EmbeddedZip to OGRE. The one pointer-dependent node is allocated under
    // a null placeholder and re-keyed after ArchiveManager returns the exact
    // mounted pointer. Publishing below is then a sequence of no-throw swaps.
    auto package_archives_candidate = m_package_archives_by_group;
    auto authenticated_archives_candidate =
        m_authenticated_package_archives_by_group;
    auto authenticated_bindings_candidate =
        m_authenticated_package_archive_bindings_by_group;
    auto group_generations_candidate =
        m_legacy_material_group_generations;
    auto mesh_bindings_candidate = m_authenticated_mesh_bindings;
    auto package_materials_candidate = m_package_materials_by_group;
    auto authenticated_materials_candidate =
        m_authenticated_materials_by_group;
    auto generated_material_fallbacks_candidate =
        m_generated_material_fallbacks_by_group;
    auto generated_material_names_candidate =
        m_generated_material_names_by_group;
    auto generated_material_bindings_candidate =
        m_generated_material_bindings_by_group;
    auto reported_material_resolutions_candidate =
        m_reported_material_resolutions_by_group;
    auto authorized_texture_fallbacks_candidate =
        m_authorized_texture_fallbacks_by_group;
    auto reported_texture_fallbacks_candidate =
        m_reported_texture_fallbacks_by_group;
    auto committed_material_script_generations_candidate =
        m_committed_material_script_generations;
    Render::Ogre14AuthenticatedArchiveManifestAccounting
        manifest_accounting_candidate =
            m_authenticated_package_archive_manifest_accounting;
    if (!Render::TryAdmitOgre14AuthenticatedArchiveManifest(
            m_authenticated_package_archive_manifest_accounting,
            archive_preflight.members.size(),
            archive_preflight.retained_member_identity_bytes,
            Render::kOgre14AuthenticatedTextureMaximumLiveReceipts,
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates,
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberIdentityBytes,
            manifest_accounting_candidate))
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated EmbeddedZip preflight exceeds its process-wide "
            "binding, member, or identity cap",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    Render::Ogre14AuthenticatedTextureReceiptRegistry texture_candidate =
        m_authenticated_texture_receipts;
    Render::Ogre14SelectedTextureSourceReceiptRegistry
        selected_texture_candidate = m_selected_texture_sources;
    Render::Ogre14AuthenticatedMaterialScriptRegistry
        material_script_candidate = m_authenticated_material_scripts;

    auto& package_archive_names =
        package_archives_candidate[resource_group];
    auto& authenticated_archive_names =
        authenticated_archives_candidate[resource_group];
    auto& authenticated_archive_bindings =
        authenticated_bindings_candidate[resource_group];
    if (package_archives_candidate.size() >
            Render::kOgre14AuthenticatedTextureMaximumGroupRecords ||
        authenticated_archives_candidate.size() >
            Render::kOgre14AuthenticatedTextureMaximumGroupRecords ||
        authenticated_bindings_candidate.size() >
            Render::kOgre14AuthenticatedTextureMaximumGroupRecords ||
        group_generations_candidate.size() >
            Render::kOgre14AuthenticatedTextureMaximumGroupRecords)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated package mount candidate exceeds its group cap",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    if (package_archive_names.size() >=
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates ||
        authenticated_archive_names.size() >=
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates ||
        authenticated_archive_bindings.size() >=
            Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated package group exceeds its archive-binding cap",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    for (const auto& existing_binding : authenticated_archive_bindings)
    {
        if (existing_binding.first == nullptr ||
            existing_binding.second.source_archive_identity ==
                archive_snapshot.source_archive_identity())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_DUPLICATE_ITEM,
                "Authenticated package group already contains this source "
                "archive identity or an invalid placeholder",
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }
    }
    if (!package_archive_names.insert(selected_archive_name).second ||
        !authenticated_archive_names.emplace(
             selected_archive_name,
             archive_snapshot.archive_sha256()).second)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_DUPLICATE_ITEM,
            "Authenticated synthetic archive identity collides with group "
            "state",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    AuthenticatedPackageArchiveBinding pending_binding;
    pending_binding.source_archive_identity =
        archive_snapshot.source_archive_identity();
    pending_binding.selected_archive_name = selected_archive_name;
    pending_binding.selected_archive_type = "EmbeddedZip";
    pending_binding.archive_sha256 = archive_snapshot.archive_sha256();
    pending_binding.group_generation = mount_generation;
    pending_binding.immutable_archive = archive_snapshot;
    if (!authenticated_archive_bindings.emplace(
             nullptr, std::move(pending_binding)).second)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_DUPLICATE_ITEM,
            "Authenticated archive pointer placeholder collides",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    for (auto& existing_binding : authenticated_archive_bindings)
    {
        existing_binding.second.group_generation = mount_generation;
    }
    group_generations_candidate[resource_group] = mount_generation;
    const Render::ValidationResult texture_transition =
        Render::AdvanceOgre14AuthenticatedTextureGroupGeneration(
            resource_group, mount_generation, texture_candidate);
    if (!texture_transition)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated source-texture group '{}' could not advance "
                "to mount generation {}: {} ({})",
                resource_group,
                mount_generation,
                texture_transition.detail,
                texture_transition.field),
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    const Render::ValidationResult selected_texture_transition =
        Render::AdvanceOgre14SelectedTextureSourceGroupGeneration(
            resource_group, mount_generation, selected_texture_candidate);
    if (!selected_texture_transition)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Selected source-texture group '{}' could not advance to "
                "mount generation {}: {} ({})",
                resource_group,
                mount_generation,
                selected_texture_transition.detail,
                selected_texture_transition.field),
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    const Render::ValidationResult material_script_transition =
        material_script_candidate.AdvanceGroupGeneration(
            resource_group, mount_generation);
    if (!material_script_transition)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated material-script group '{}' could not advance "
                "to mount generation {}: {} ({})",
                resource_group,
                mount_generation,
                material_script_transition.detail,
                material_script_transition.field),
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    for (auto mesh_binding = mesh_bindings_candidate.begin();
         mesh_binding != mesh_bindings_candidate.end();)
    {
        if (mesh_binding->second.group == resource_group)
        {
            mesh_binding = mesh_bindings_candidate.erase(mesh_binding);
        }
        else
        {
            ++mesh_binding;
        }
    }
    package_materials_candidate.erase(resource_group);
    authenticated_materials_candidate.erase(resource_group);
    generated_material_fallbacks_candidate.erase(resource_group);
    generated_material_names_candidate.erase(resource_group);
    generated_material_bindings_candidate.erase(resource_group);
    reported_material_resolutions_candidate.erase(resource_group);
    authorized_texture_fallbacks_candidate.erase(resource_group);
    reported_texture_fallbacks_candidate.erase(resource_group);
    committed_material_script_generations_candidate.erase(resource_group);
    const bool reset_material_script_candidate =
        m_authenticated_material_script_candidate != nullptr &&
        m_authenticated_material_script_candidate->group == resource_group;

    const auto pending_insertion =
        m_authenticated_package_archive_pending_snapshots.emplace(
            selected_archive_name, archive_snapshot);
    if (!pending_insertion.second)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_DUPLICATE_ITEM,
            "Authenticated synthetic archive already has a pending owner",
            "ContentManager::MountAuthenticatedPackageResourceLocation");
    }
    m_authenticated_package_archive_retained_bytes +=
        static_cast<std::uint64_t>(archive_snapshot.size());

    Ogre::Archive* provisional_archive = nullptr;
    try
    {
        Ogre::EmbeddedZipArchiveFactory::addEmbbeddedFile(
            selected_archive_name,
            archive_snapshot.bytes(),
            archive_snapshot.size(),
            nullptr);
        Render::MaybeInjectOgre14AuthenticatedArchiveMountFault(
            Render::Ogre14AuthenticatedArchiveMountStage::
                AFTER_EMBEDDED_ZIP_REGISTRATION,
            fault_injector);
        resource_manager.addResourceLocation(
            selected_archive_name,
            "EmbeddedZip",
            resource_group,
            false,
            true);
        // Resolve ArchiveManager by the generated name first. ResourceLocation
        // pointers are borrowed and may be dangling after out-of-band manager
        // mutation, so none is dereferenced to discover its name.
        const Render::Ogre14AuthenticatedArchiveAuthorityProof
            archive_authority =
                Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                    archive_manager,
                    resource_manager,
                    resource_group,
                    selected_archive_name);
        Ogre::Archive* selected_archive = archive_authority.manager_archive;
        if (!archive_authority.AuthenticatesExclusive(selected_archive) ||
            selected_archive->getName() != selected_archive_name ||
            selected_archive->getType() != "EmbeddedZip" ||
            !selected_archive->isReadOnly())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated EmbeddedZip mount did not resolve to one "
                "pointer-exact archive instance",
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }
        provisional_archive = selected_archive;
        Render::MaybeInjectOgre14AuthenticatedArchiveMountFault(
            Render::Ogre14AuthenticatedArchiveMountStage::
                AFTER_RESOURCE_LOCATION_INSERTION,
            fault_injector);

        // Bind every selectable member to the immutable archive snapshot
        // before publishing pointer authority. Raw Archive* identity alone is
        // insufficient because an out-of-band unload/reload may reuse the
        // same address. The manifest makes every later script open prove exact
        // member bytes as well as manager/location identity.
        const Ogre::FileInfoListPtr member_index =
            selected_archive->findFileInfo("*", true, false);
        const Ogre::FileInfoListPtr directory_index =
            selected_archive->findFileInfo("*", true, true);
        if (!member_index || !directory_index ||
            member_index->size() != preflight_file_member_count ||
            member_index->size() + directory_index->size() !=
                preflight_members.size())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated EmbeddedZip member index is missing or "
                "disagrees with its bounded immutable preflight",
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }
        std::unordered_set<Ogre::String> native_directory_names;
        native_directory_names.reserve(directory_index->size());
        for (const Ogre::FileInfo& directory : *directory_index)
        {
            if (directory.archive != selected_archive ||
                directory.basename.empty() ||
                directory.path.size() >
                    Render::kOgre14AuthenticatedTextureMaximumIdentifierBytes ||
                directory.basename.size() >=
                    Render::kOgre14AuthenticatedTextureMaximumIdentifierBytes -
                        directory.path.size())
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Authenticated EmbeddedZip directory identity is invalid",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
            const Ogre::String exact_directory =
                directory.path + directory.basename + "/";
            const auto preflight_directory =
                preflight_members.find(exact_directory);
            if (preflight_directory == preflight_members.end() ||
                !archive_preflight.members.at(
                     preflight_directory->second).directory ||
                archive_preflight.members.at(
                     preflight_directory->second).uncompressed_size !=
                    static_cast<std::uint64_t>(directory.uncompressedSize) ||
                !native_directory_names.insert(exact_directory).second)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Authenticated EmbeddedZip directory index disagrees "
                    "with its immutable preflight",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
        }
        auto mutable_member_manifest = std::make_shared<
            AuthenticatedPackageArchiveBinding::MemberManifest>();
        mutable_member_manifest->reserve(member_index->size());
        std::uint64_t member_identity_bytes = 0U;
        std::uint64_t member_uncompressed_bytes = 0U;
        for (const Ogre::FileInfo& member : *member_index)
        {
            if (member.archive != selected_archive || member.basename.empty() ||
                member.path.size() >
                    Render::kOgre14AuthenticatedTextureMaximumIdentifierBytes ||
                member.basename.size() >
                    Render::kOgre14AuthenticatedTextureMaximumIdentifierBytes -
                        member.path.size())
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Authenticated EmbeddedZip member identity is invalid",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
            const Ogre::String exact_member =
                member.path + member.basename;
            const auto preflight_member =
                preflight_members.find(exact_member);
            if (preflight_member == preflight_members.end())
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Authenticated EmbeddedZip exposed a member absent from "
                    "its immutable preflight",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
            const TerrainBundleAuthenticatedArchiveMemberPreflight&
                expected_member = archive_preflight.members.at(
                    preflight_member->second);
            if (expected_member.directory ||
                expected_member.compressed_size !=
                    static_cast<std::uint64_t>(member.compressedSize) ||
                expected_member.uncompressed_size !=
                    static_cast<std::uint64_t>(member.uncompressedSize))
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Authenticated EmbeddedZip member metadata disagrees "
                    "with its immutable preflight",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
            constexpr std::uint64_t SHA256_IDENTITY_BYTES = 64U;
            if (static_cast<std::uint64_t>(exact_member.size()) >
                    Render::kOgre14AuthenticatedTextureMaximumArchiveMemberIdentityBytes -
                        SHA256_IDENTITY_BYTES ||
                member_identity_bytes >
                    Render::kOgre14AuthenticatedTextureMaximumArchiveMemberIdentityBytes -
                        static_cast<std::uint64_t>(exact_member.size()) -
                        SHA256_IDENTITY_BYTES ||
                static_cast<std::uint64_t>(member.uncompressedSize) >
                    Render::kOgre14AuthenticatedTextureMaximumSourceBytes ||
                member_uncompressed_bytes >
                    Render::kOgre14AuthenticatedTextureMaximumRetainedBytes -
                        static_cast<std::uint64_t>(member.uncompressedSize))
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Authenticated EmbeddedZip member manifest exceeds its "
                    "identity or decoded-byte cap",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
            member_identity_bytes +=
                static_cast<std::uint64_t>(exact_member.size()) +
                SHA256_IDENTITY_BYTES;
            member_uncompressed_bytes +=
                static_cast<std::uint64_t>(member.uncompressedSize);

            Ogre::DataStreamPtr member_stream =
                selected_archive->open(exact_member);
            if (!member_stream ||
                member_stream->size() != member.uncompressedSize)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Authenticated EmbeddedZip member stream did not match "
                    "its immutable index",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
            const std::string member_bytes = member_stream->getAsString();
            const std::string member_sha256 = Sha256Bytes(member_bytes);
            if (member_bytes.size() != member_stream->size() ||
                member_sha256.empty())
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Authenticated EmbeddedZip member could not be hashed",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
            AuthenticatedPackageArchiveBinding::MemberBinding
                member_binding;
            member_binding.compressed_size =
                static_cast<std::uint64_t>(member.compressedSize);
            member_binding.uncompressed_size =
                static_cast<std::uint64_t>(member.uncompressedSize);
            member_binding.sha256 = member_sha256;
            if (!mutable_member_manifest->emplace(
                     exact_member, std::move(member_binding)).second)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_DUPLICATE_ITEM,
                "Authenticated EmbeddedZip contains a duplicate exact "
                    "member identity",
                    "ContentManager::MountAuthenticatedPackageResourceLocation");
            }
        }
        if (mutable_member_manifest->empty())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated EmbeddedZip member manifest is empty after "
                "bounded preflight",
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }
        std::shared_ptr<const
            AuthenticatedPackageArchiveBinding::MemberManifest>
            immutable_member_manifest = mutable_member_manifest;

        auto& candidate_bindings =
            authenticated_bindings_candidate.at(resource_group);
        auto pointer_node = candidate_bindings.extract(nullptr);
        if (pointer_node.empty())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated archive pointer placeholder was lost",
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }
        pointer_node.key() = selected_archive;
        pointer_node.mapped().archive_pointer_token =
            reinterpret_cast<std::uintptr_t>(selected_archive);
        pointer_node.mapped().immutable_member_manifest =
            std::move(immutable_member_manifest);
        pointer_node.mapped().retained_manifest_member_count =
            archive_preflight.members.size();
        pointer_node.mapped().retained_manifest_file_count =
            mutable_member_manifest->size();
        pointer_node.mapped().retained_manifest_identity_bytes =
            archive_preflight.retained_member_identity_bytes;
        const auto pointer_insertion =
            candidate_bindings.insert(std::move(pointer_node));
        if (!pointer_insertion.inserted)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_DUPLICATE_ITEM,
                "Authenticated archive pointer collides with group state",
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }

        Render::MaybeInjectOgre14AuthenticatedArchiveMountFault(
            Render::Ogre14AuthenticatedArchiveMountStage::
                BEFORE_POINTER_BOUND_STATE_SWAP,
            fault_injector);

        // Remove the temporary owner from mutable pending state before the
        // first publication swap. Its node keeps the exact bytes alive until
        // the pointer-bound candidate owns them, and any extraction failure
        // still reaches the external-state rollback below.
        auto pending_owner_node =
            m_authenticated_package_archive_pending_snapshots.extract(
                pending_insertion.first);
        if (pending_owner_node.empty())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated archive pending owner was lost before "
                "publication",
                "ContentManager::MountAuthenticatedPackageResourceLocation");
        }

        // These assertions bind the atomic-publication claim to the exact
        // standard-library/container types compiled on every target. Readers
        // take m_legacy_material_state_mutex, so they observe either the old
        // state or the complete pointer-bound generation transition.
        static_assert(
            noexcept(m_package_archives_by_group.swap(
                package_archives_candidate)));
        static_assert(
            noexcept(m_authenticated_package_archives_by_group.swap(
                authenticated_archives_candidate)));
        static_assert(
            noexcept(m_authenticated_package_archive_bindings_by_group.swap(
                authenticated_bindings_candidate)));
        static_assert(
            noexcept(m_legacy_material_group_generations.swap(
                group_generations_candidate)));
        static_assert(
            noexcept(m_authenticated_mesh_bindings.swap(
                mesh_bindings_candidate)));
        static_assert(
            noexcept(m_package_materials_by_group.swap(
                package_materials_candidate)));
        static_assert(
            noexcept(m_authenticated_materials_by_group.swap(
                authenticated_materials_candidate)));
        static_assert(
            noexcept(m_generated_material_fallbacks_by_group.swap(
                generated_material_fallbacks_candidate)));
        static_assert(
            noexcept(m_generated_material_names_by_group.swap(
                generated_material_names_candidate)));
        static_assert(
            noexcept(m_generated_material_bindings_by_group.swap(
                generated_material_bindings_candidate)));
        static_assert(
            noexcept(m_reported_material_resolutions_by_group.swap(
                reported_material_resolutions_candidate)));
        static_assert(
            noexcept(m_authorized_texture_fallbacks_by_group.swap(
                authorized_texture_fallbacks_candidate)));
        static_assert(
            noexcept(m_reported_texture_fallbacks_by_group.swap(
                reported_texture_fallbacks_candidate)));
        static_assert(
            noexcept(m_committed_material_script_generations.swap(
                committed_material_script_generations_candidate)));
        static_assert(
            noexcept(m_authenticated_texture_receipts =
                         std::move(texture_candidate)));
        static_assert(
            noexcept(m_selected_texture_sources =
                         std::move(selected_texture_candidate)));
        static_assert(
            noexcept(m_authenticated_material_scripts =
                         std::move(material_script_candidate)));
        static_assert(
            noexcept(m_authenticated_material_script_candidate.reset()));
        static_assert(
            std::is_nothrow_assignable_v<std::uint64_t&, std::uint64_t>);
        static_assert(std::is_nothrow_assignable_v<
            Render::Ogre14AuthenticatedArchiveManifestAccounting&,
            const Render::Ogre14AuthenticatedArchiveManifestAccounting&>);
        static_assert(
            std::is_nothrow_destructible_v<
                decltype(package_archives_candidate)> &&
            std::is_nothrow_destructible_v<
                decltype(authenticated_archives_candidate)> &&
            std::is_nothrow_destructible_v<
                decltype(authenticated_bindings_candidate)> &&
            std::is_nothrow_destructible_v<
                decltype(group_generations_candidate)> &&
            std::is_nothrow_destructible_v<
                decltype(mesh_bindings_candidate)> &&
            std::is_nothrow_destructible_v<
                decltype(texture_candidate)> &&
            std::is_nothrow_destructible_v<
                decltype(selected_texture_candidate)> &&
            std::is_nothrow_destructible_v<
                decltype(material_script_candidate)> &&
            std::is_nothrow_destructible_v<
                decltype(pending_owner_node)>);

        m_package_archives_by_group.swap(package_archives_candidate);
        m_authenticated_package_archives_by_group.swap(
            authenticated_archives_candidate);
        m_authenticated_package_archive_bindings_by_group.swap(
            authenticated_bindings_candidate);
        m_legacy_material_group_generations.swap(
            group_generations_candidate);
        m_authenticated_mesh_bindings.swap(mesh_bindings_candidate);
        m_package_materials_by_group.swap(package_materials_candidate);
        m_authenticated_materials_by_group.swap(
            authenticated_materials_candidate);
        m_generated_material_fallbacks_by_group.swap(
            generated_material_fallbacks_candidate);
        m_generated_material_names_by_group.swap(
            generated_material_names_candidate);
        m_generated_material_bindings_by_group.swap(
            generated_material_bindings_candidate);
        m_reported_material_resolutions_by_group.swap(
            reported_material_resolutions_candidate);
        m_authorized_texture_fallbacks_by_group.swap(
            authorized_texture_fallbacks_candidate);
        m_reported_texture_fallbacks_by_group.swap(
            reported_texture_fallbacks_candidate);
        m_committed_material_script_generations.swap(
            committed_material_script_generations_candidate);
        this->EraseSelectedTextureSourceStagesForGroupLocked(resource_group);
        m_authenticated_texture_receipts = std::move(texture_candidate);
        m_selected_texture_sources = std::move(selected_texture_candidate);
        m_authenticated_material_scripts =
            std::move(material_script_candidate);
        if (reset_material_script_candidate)
        {
            m_authenticated_material_script_candidate.reset();
        }
        m_authenticated_package_archive_manifest_accounting =
            manifest_accounting_candidate;
        m_next_legacy_material_group_generation = mount_generation;
    }
    catch (...)
    {
        bool rollback_complete = false;
        try
        {
            const Render::Ogre14AuthenticatedArchiveAuthorityProof
                rollback_authority =
                    Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                        archive_manager,
                        resource_manager,
                        resource_group,
                        selected_archive_name);
            if (rollback_authority.manager_name_count == 0U &&
                rollback_authority.manager_archive == nullptr)
            {
                Render::Ogre14AuthenticatedArchiveLocationClosure
                    detached_location_closure;
                if (provisional_archive != nullptr)
                {
                    detached_location_closure =
                        Render::CaptureOgre14AuthenticatedArchiveLocationClosure(
                            resource_manager,
                            resource_group,
                            provisional_archive);
                }
                if (provisional_archive == nullptr ||
                    detached_location_closure.
                        IsAbsentFromAllLocations())
                {
                    Ogre::EmbeddedZipArchiveFactory::removeEmbbeddedFile(
                        selected_archive_name);
                    rollback_complete = true;
                }
            }
            else if (rollback_authority.AuthenticatesExclusive(
                         rollback_authority.manager_archive))
            {
                resource_manager.removeResourceLocation(
                    selected_archive_name, resource_group);
                rollback_complete = true;
            }
            else if (rollback_authority.AuthenticatesDetached(
                         rollback_authority.manager_archive))
            {
                archive_manager.unload(rollback_authority.manager_archive);
                rollback_complete = true;
            }
            if (rollback_complete)
            {
                Ogre::ArchiveManager::ArchiveMapIterator archives =
                    archive_manager.getArchiveIterator();
                while (archives.hasMoreElements())
                {
                    const Ogre::String archive_name =
                        archives.peekNextKey();
                    archives.getNext();
                    if (archive_name == selected_archive_name)
                    {
                        rollback_complete = false;
                        break;
                    }
                }
            }
            if (rollback_complete)
            {
                // ArchiveManager::unload() asks the EmbeddedZip factory to
                // erase this entry. Repeat the erase explicitly: it is
                // idempotent and makes the rollback postcondition independent
                // of that implementation detail.
                Ogre::EmbeddedZipArchiveFactory::removeEmbbeddedFile(
                    selected_archive_name);
            }
        }
        catch (...)
        {
            rollback_complete = false;
        }
        if (rollback_complete)
        {
            m_authenticated_package_archive_pending_snapshots.erase(
                selected_archive_name);
            m_authenticated_package_archive_retained_bytes -=
                static_cast<std::uint64_t>(archive_snapshot.size());
        }
        else
        {
            // The external OGRE registry can no longer be proven absent. Keep
            // the immutable bytes alive in pending state and stop the process:
            // returning would permit this synthetic archive to shadow a later
            // resource selection or survive into a nominally recoverable group
            // retry. This is deliberately not a recoverable ContentManager
            // state; process restart is the only reset boundary.
            std::terminate();
        }
        throw;
    }
#else
    (void)resource_group;
    (void)archive_snapshot;
    OGRE_EXCEPT(
        Ogre::Exception::ERR_NOT_IMPLEMENTED,
        "Authenticated archive snapshots require OGRE 14",
        "ContentManager::MountAuthenticatedPackageResourceLocation");
#endif
}

bool ContentManager::IsAuthenticatedPackageSourceMounted(
    const Ogre::String& resource_group,
    const Ogre::String& source_archive_identity)
{
    if (resource_group.empty() || source_archive_identity.empty())
    {
        return false;
    }
    std::lock_guard<std::mutex> state_lock(
        m_legacy_material_state_mutex);
    const auto group =
        m_authenticated_package_archive_bindings_by_group.find(
            resource_group);
    if (group ==
        m_authenticated_package_archive_bindings_by_group.end())
    {
        return false;
    }
    for (const auto& archive_entry : group->second)
    {
        if (archive_entry.second.source_archive_identity ==
            source_archive_identity)
        {
            return true;
        }
    }
    return false;
}

bool ContentManager::IsExactAuthenticatedPackageSnapshotMounted(
    const Ogre::String& resource_group,
    const TerrainBundleAuthenticatedArchiveSnapshot& archive_snapshot) const
{
    if (resource_group.empty() || !archive_snapshot.initialized() ||
        archive_snapshot.source_archive_identity().empty() ||
        archive_snapshot.archive_sha256().empty() ||
        archive_snapshot.size() == 0U)
    {
        return false;
    }

    Ogre::ResourceGroupManager* const resource_manager =
        Ogre::ResourceGroupManager::getSingletonPtr();
    if (resource_manager == nullptr ||
        !resource_manager->resourceGroupExists(resource_group))
    {
        return false;
    }
    const Ogre::ResourceGroupManager::LocationList& live_locations =
        resource_manager->getResourceLocationList(resource_group);

    std::lock_guard<std::mutex> state_lock(
        m_legacy_material_state_mutex);
    const auto generation =
        m_legacy_material_group_generations.find(resource_group);
    const auto group =
        m_authenticated_package_archive_bindings_by_group.find(
            resource_group);
    if (generation == m_legacy_material_group_generations.end() ||
        generation->second == 0U ||
        group == m_authenticated_package_archive_bindings_by_group.end())
    {
        return false;
    }

    for (const auto& archive_entry : group->second)
    {
        const Ogre::Archive* const mounted_archive = archive_entry.first;
        const AuthenticatedPackageArchiveBinding& binding =
            archive_entry.second;
        bool exact_live_location = false;
        for (const Ogre::ResourceGroupManager::ResourceLocation& location :
             live_locations)
        {
            if (location.archive != mounted_archive)
            {
                continue;
            }
            if (exact_live_location || location.archive == nullptr ||
                location.archive->getName() !=
                    binding.selected_archive_name ||
                location.archive->getType() !=
                    binding.selected_archive_type)
            {
                exact_live_location = false;
                break;
            }
            exact_live_location = true;
        }
        if (mounted_archive != nullptr &&
            exact_live_location &&
            binding.archive_pointer_token ==
                reinterpret_cast<std::uintptr_t>(mounted_archive) &&
            binding.group_generation == generation->second &&
            binding.source_archive_identity ==
                archive_snapshot.source_archive_identity() &&
            binding.archive_sha256 == archive_snapshot.archive_sha256() &&
            binding.immutable_archive.size() == archive_snapshot.size() &&
            binding.immutable_archive.SharesImmutableStateWith(
                archive_snapshot))
        {
            return true;
        }
    }
    return false;
}

void ContentManager::UnregisterPackageResourceGroup(
    const Ogre::String& resource_group)
{
#if OGRE_VERSION_MAJOR >= 14
    this->RequireAuthenticatedResourceThread(
        "ContentManager::UnregisterPackageResourceGroup");
#endif
    std::scoped_lock<std::mutex, std::mutex, std::mutex>
        legacy_material_lock(
        m_legacy_material_archive_io_mutex,
        m_legacy_material_resolution_mutex,
        m_legacy_material_state_mutex);
#if OGRE_VERSION_MAJOR >= 14
    if (m_legacy_material_group_generations.find(resource_group) ==
            m_legacy_material_group_generations.end() &&
        m_authenticated_package_archive_bindings_by_group.find(
            resource_group) ==
            m_authenticated_package_archive_bindings_by_group.end() &&
        m_package_archives_by_group.find(resource_group) ==
            m_package_archives_by_group.end())
    {
        // Never create an authenticated generation merely to tear down an
        // unknown/ordinary group. This keeps ephemeral names from consuming
        // the bounded process-global group and identity budgets.
        m_authenticated_package_archives_by_group.erase(resource_group);
        m_package_materials_by_group.erase(resource_group);
        m_authenticated_materials_by_group.erase(resource_group);
        m_generated_material_fallbacks_by_group.erase(resource_group);
        m_generated_material_names_by_group.erase(resource_group);
        m_generated_material_bindings_by_group.erase(resource_group);
        m_reported_material_resolutions_by_group.erase(resource_group);
        m_authorized_texture_fallbacks_by_group.erase(resource_group);
        m_reported_texture_fallbacks_by_group.erase(resource_group);
        m_committed_material_script_generations.erase(resource_group);
        return;
    }
    if (m_next_legacy_material_group_generation ==
        (std::numeric_limits<std::uint64_t>::max)())
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Authenticated resource-group generation space is exhausted",
            "ContentManager::UnregisterPackageResourceGroup");
    }
    const std::uint64_t teardown_generation =
        m_next_legacy_material_group_generation + 1U;
    Render::Ogre14AuthenticatedTextureReceiptRegistry texture_candidate =
        m_authenticated_texture_receipts;
    const Render::ValidationResult texture_advance =
        Render::AdvanceOgre14AuthenticatedTextureGroupGeneration(
            resource_group, teardown_generation, texture_candidate);
    if (!texture_advance)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated source-texture group '{}' could not advance "
                "to teardown generation {}: {} ({})",
                resource_group,
                teardown_generation,
                texture_advance.detail,
                texture_advance.field),
            "ContentManager::UnregisterPackageResourceGroup");
    }
    const Render::ValidationResult texture_teardown =
        Render::TeardownOgre14AuthenticatedTextureGroup(
            resource_group, teardown_generation, texture_candidate);
    if (!texture_teardown)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated source-texture group '{}' generation {} "
                "could not tear down: {} ({})",
                resource_group,
                teardown_generation,
                texture_teardown.detail,
                texture_teardown.field),
            "ContentManager::UnregisterPackageResourceGroup");
    }
    Render::Ogre14SelectedTextureSourceReceiptRegistry
        selected_texture_candidate = m_selected_texture_sources;
    const Render::ValidationResult selected_texture_advance =
        Render::AdvanceOgre14SelectedTextureSourceGroupGeneration(
            resource_group, teardown_generation,
            selected_texture_candidate);
    if (!selected_texture_advance)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Selected source-texture group '{}' could not advance to "
                "teardown generation {}: {} ({})",
                resource_group,
                teardown_generation,
                selected_texture_advance.detail,
                selected_texture_advance.field),
            "ContentManager::UnregisterPackageResourceGroup");
    }
    const Render::ValidationResult selected_texture_teardown =
        Render::TeardownOgre14SelectedTextureSourceGroup(
            resource_group, teardown_generation,
            selected_texture_candidate);
    if (!selected_texture_teardown)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Selected source-texture group '{}' generation {} could "
                "not tear down: {} ({})",
                resource_group,
                teardown_generation,
                selected_texture_teardown.detail,
                selected_texture_teardown.field),
            "ContentManager::UnregisterPackageResourceGroup");
    }
    Render::Ogre14AuthenticatedMaterialScriptRegistry
        material_script_candidate = m_authenticated_material_scripts;
    const Render::ValidationResult material_script_advance =
        material_script_candidate.AdvanceGroupGeneration(
            resource_group, teardown_generation);
    if (!material_script_advance)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated material-script group '{}' could not advance "
                "to teardown generation {}: {} ({})",
                resource_group,
                teardown_generation,
                material_script_advance.detail,
                material_script_advance.field),
            "ContentManager::UnregisterPackageResourceGroup");
    }
    const Render::ValidationResult material_script_teardown =
        material_script_candidate.TeardownGroup(
            resource_group, teardown_generation);
    if (!material_script_teardown)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated material-script group '{}' generation {} "
                "could not release its retained record: {} ({})",
                resource_group,
                teardown_generation,
                material_script_teardown.detail,
                material_script_teardown.field),
            "ContentManager::UnregisterPackageResourceGroup");
    }

    // Prepare every allocation-bearing internal teardown before the first
    // external OGRE removal. Once an archive has been removed, only no-throw
    // swaps, moves, scalar assignments, and precomputed pointer resets are
    // permitted; a recoverable exception at that point could otherwise leave
    // live authority pointing at a destroyed Archive.
    auto group_generations_candidate =
        m_legacy_material_group_generations;
    auto package_archives_candidate = m_package_archives_by_group;
    auto authenticated_archives_candidate =
        m_authenticated_package_archives_by_group;
    auto authenticated_bindings_candidate =
        m_authenticated_package_archive_bindings_by_group;
    auto package_materials_candidate = m_package_materials_by_group;
    auto authenticated_materials_candidate =
        m_authenticated_materials_by_group;
    auto generated_material_fallbacks_candidate =
        m_generated_material_fallbacks_by_group;
    auto generated_material_names_candidate =
        m_generated_material_names_by_group;
    auto generated_material_bindings_candidate =
        m_generated_material_bindings_by_group;
    auto reported_material_resolutions_candidate =
        m_reported_material_resolutions_by_group;
    auto authorized_texture_fallbacks_candidate =
        m_authorized_texture_fallbacks_by_group;
    auto reported_texture_fallbacks_candidate =
        m_reported_texture_fallbacks_by_group;
    auto committed_material_script_generations_candidate =
        m_committed_material_script_generations;
    auto mesh_bindings_candidate = m_authenticated_mesh_bindings;
    Render::Ogre14AuthenticatedArchiveManifestAccounting
        manifest_accounting_candidate =
            m_authenticated_package_archive_manifest_accounting;

    group_generations_candidate.erase(resource_group);
    package_archives_candidate.erase(resource_group);
    authenticated_archives_candidate.erase(resource_group);
    authenticated_bindings_candidate.erase(resource_group);
    package_materials_candidate.erase(resource_group);
    authenticated_materials_candidate.erase(resource_group);
    generated_material_fallbacks_candidate.erase(resource_group);
    generated_material_names_candidate.erase(resource_group);
    generated_material_bindings_candidate.erase(resource_group);
    reported_material_resolutions_candidate.erase(resource_group);
    authorized_texture_fallbacks_candidate.erase(resource_group);
    reported_texture_fallbacks_candidate.erase(resource_group);
    committed_material_script_generations_candidate.erase(resource_group);
    for (auto binding = mesh_bindings_candidate.begin();
         binding != mesh_bindings_candidate.end();)
    {
        if (binding->second.group == resource_group)
        {
            binding = mesh_bindings_candidate.erase(binding);
        }
        else
        {
            ++binding;
        }
    }
    const bool reset_material_script_candidate =
        m_authenticated_material_script_candidate != nullptr &&
        m_authenticated_material_script_candidate->group == resource_group;
    const bool reset_aborted_material_script_candidate =
        m_aborted_material_script_candidate != nullptr &&
        m_aborted_material_script_candidate->group == resource_group;
    const bool clear_scripting_resource_group =
        m_scripting_resource_group == resource_group;

    std::uint64_t removed_archive_bytes = 0U;
    const auto authenticated_binding_group =
        m_authenticated_package_archive_bindings_by_group.find(
            resource_group);
    if (authenticated_binding_group !=
        m_authenticated_package_archive_bindings_by_group.end())
    {
        Ogre::ResourceGroupManager* resource_manager =
            Ogre::ResourceGroupManager::getSingletonPtr();
        Ogre::ArchiveManager* archive_manager =
            Ogre::ArchiveManager::getSingletonPtr();
        if (resource_manager == nullptr || archive_manager == nullptr)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated archive managers are unavailable during "
                "teardown",
                "ContentManager::UnregisterPackageResourceGroup");
        }
        // Validate the complete external closure and accounting before the
        // first irreversible OGRE removal. Internal authority remains live
        // until every selected archive is proven absent.
        for (const auto& archive_entry :
             authenticated_binding_group->second)
        {
            const Ogre::Archive* expected_archive = archive_entry.first;
            const AuthenticatedPackageArchiveBinding& binding =
                archive_entry.second;
            const Render::Ogre14AuthenticatedArchiveAuthorityProof
                archive_authority =
                    Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                        *archive_manager,
                        *resource_manager,
                        resource_group,
                        binding.selected_archive_name);
            Ogre::Archive* manager_archive =
                archive_authority.manager_archive;
            const std::uint64_t archive_size =
                static_cast<std::uint64_t>(
                    binding.immutable_archive.size());
            Render::Ogre14AuthenticatedArchiveManifestAccounting
                released_manifest_accounting;
            if (expected_archive == nullptr ||
                binding.archive_pointer_token !=
                    reinterpret_cast<std::uintptr_t>(expected_archive) ||
                !binding.immutable_archive.initialized() ||
                !binding.immutable_member_manifest ||
                binding.retained_manifest_member_count == 0U ||
                binding.retained_manifest_file_count == 0U ||
                binding.retained_manifest_file_count !=
                    binding.immutable_member_manifest->size() ||
                binding.retained_manifest_member_count <
                    binding.retained_manifest_file_count ||
                binding.retained_manifest_identity_bytes == 0U ||
                !Render::TryReleaseOgre14AuthenticatedArchiveManifest(
                    manifest_accounting_candidate,
                    binding.retained_manifest_member_count,
                    binding.retained_manifest_identity_bytes,
                    released_manifest_accounting) ||
                (!archive_authority.AuthenticatesExclusive(expected_archive) &&
                 !archive_authority.AuthenticatesDetached(expected_archive)) ||
                archive_size >
                    m_authenticated_package_archive_retained_bytes ||
                removed_archive_bytes >
                    m_authenticated_package_archive_retained_bytes -
                        archive_size)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Authenticated archive binding in group '{}' changed "
                        "before teardown",
                        resource_group),
                    "ContentManager::UnregisterPackageResourceGroup");
            }
            manifest_accounting_candidate =
                released_manifest_accounting;
            // The stored raw pointer is dereferenced only after ArchiveManager
            // proves that its current live entry is pointer-exact. An
            // out-of-band unload therefore fails closed without touching a
            // dangling Archive.
            if (manager_archive->getName() !=
                    binding.selected_archive_name ||
                manager_archive->getType() !=
                    binding.selected_archive_type ||
                binding.selected_archive_type != "EmbeddedZip")
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Authenticated archive binding in group '{}' no "
                        "longer matches its live manager entry",
                        resource_group),
                    "ContentManager::UnregisterPackageResourceGroup");
            }
            removed_archive_bytes += archive_size;
        }

        try
        {
            for (const auto& archive_entry :
                 authenticated_binding_group->second)
            {
                const Ogre::Archive* expected_archive = archive_entry.first;
                const AuthenticatedPackageArchiveBinding& binding =
                    archive_entry.second;
                bool location_removed = false;
                if (resource_manager->resourceGroupExists(resource_group))
                {
                    const Ogre::ResourceGroupManager::LocationList& locations =
                        resource_manager->getResourceLocationList(
                            resource_group);
                    for (const Ogre::ResourceGroupManager::ResourceLocation&
                             location : locations)
                    {
                        if (location.archive == expected_archive)
                        {
                            resource_manager->removeResourceLocation(
                                binding.selected_archive_name,
                                resource_group);
                            location_removed = true;
                            break;
                        }
                    }
                }
                if (!location_removed)
                {
                    archive_manager->unload(
                        const_cast<Ogre::Archive*>(expected_archive));
                }

                Ogre::ArchiveManager::ArchiveMapIterator remaining_archives =
                    archive_manager->getArchiveIterator();
                while (remaining_archives.hasMoreElements())
                {
                    const Ogre::String archive_name =
                        remaining_archives.peekNextKey();
                    Ogre::Archive* archive = remaining_archives.getNext();
                    if (archive_name == binding.selected_archive_name ||
                        archive == expected_archive)
                    {
                        std::terminate();
                    }
                }
                Ogre::EmbeddedZipArchiveFactory::removeEmbbeddedFile(
                    binding.selected_archive_name);
            }
        }
        catch (...)
        {
            // At least one external manager may already have removed an
            // archive. A recoverable return would retain a dangling immutable
            // binding or permit a shadowing retry, so process restart is the
            // only honest recovery boundary.
            std::terminate();
        }
    }

    // Every fallible candidate build and external validation/removal is now
    // complete. Publish the empty generation and revoke every compatibility
    // index using only operations proven not to throw.
    static_assert(
        noexcept(m_legacy_material_group_generations.swap(
            group_generations_candidate)));
    static_assert(
        noexcept(m_package_archives_by_group.swap(
            package_archives_candidate)));
    static_assert(
        noexcept(m_authenticated_package_archives_by_group.swap(
            authenticated_archives_candidate)));
    static_assert(
        noexcept(m_authenticated_package_archive_bindings_by_group.swap(
            authenticated_bindings_candidate)));
    static_assert(
        noexcept(m_package_materials_by_group.swap(
            package_materials_candidate)));
    static_assert(
        noexcept(m_authenticated_materials_by_group.swap(
            authenticated_materials_candidate)));
    static_assert(
        noexcept(m_generated_material_fallbacks_by_group.swap(
            generated_material_fallbacks_candidate)));
    static_assert(
        noexcept(m_generated_material_names_by_group.swap(
            generated_material_names_candidate)));
    static_assert(
        noexcept(m_generated_material_bindings_by_group.swap(
            generated_material_bindings_candidate)));
    static_assert(
        noexcept(m_reported_material_resolutions_by_group.swap(
            reported_material_resolutions_candidate)));
    static_assert(
        noexcept(m_authorized_texture_fallbacks_by_group.swap(
            authorized_texture_fallbacks_candidate)));
    static_assert(
        noexcept(m_reported_texture_fallbacks_by_group.swap(
            reported_texture_fallbacks_candidate)));
    static_assert(
        noexcept(m_committed_material_script_generations.swap(
            committed_material_script_generations_candidate)));
    static_assert(
        noexcept(m_authenticated_mesh_bindings.swap(
            mesh_bindings_candidate)));
    static_assert(
        noexcept(m_authenticated_texture_receipts =
                     std::move(texture_candidate)));
    static_assert(
        noexcept(m_selected_texture_sources =
                     std::move(selected_texture_candidate)));
    static_assert(
        noexcept(m_authenticated_material_scripts =
                     std::move(material_script_candidate)));
    static_assert(
        noexcept(m_authenticated_material_script_candidate.reset()));
    static_assert(
        noexcept(m_aborted_material_script_candidate.reset()));
    static_assert(noexcept(m_scripting_resource_group.clear()));
    static_assert(std::is_nothrow_assignable_v<
        Render::Ogre14AuthenticatedArchiveManifestAccounting&,
        const Render::Ogre14AuthenticatedArchiveManifestAccounting&>);

    m_legacy_material_group_generations.swap(group_generations_candidate);
    m_package_archives_by_group.swap(package_archives_candidate);
    m_authenticated_package_archives_by_group.swap(
        authenticated_archives_candidate);
    m_authenticated_package_archive_bindings_by_group.swap(
        authenticated_bindings_candidate);
    m_package_materials_by_group.swap(package_materials_candidate);
    m_authenticated_materials_by_group.swap(
        authenticated_materials_candidate);
    m_generated_material_fallbacks_by_group.swap(
        generated_material_fallbacks_candidate);
    m_generated_material_names_by_group.swap(
        generated_material_names_candidate);
    m_generated_material_bindings_by_group.swap(
        generated_material_bindings_candidate);
    m_reported_material_resolutions_by_group.swap(
        reported_material_resolutions_candidate);
    m_authorized_texture_fallbacks_by_group.swap(
        authorized_texture_fallbacks_candidate);
    m_reported_texture_fallbacks_by_group.swap(
        reported_texture_fallbacks_candidate);
    m_committed_material_script_generations.swap(
        committed_material_script_generations_candidate);
    m_authenticated_mesh_bindings.swap(mesh_bindings_candidate);
    m_next_legacy_material_group_generation = teardown_generation;
    this->EraseSelectedTextureSourceStagesForGroupLocked(resource_group);
    m_authenticated_texture_receipts = std::move(texture_candidate);
    m_selected_texture_sources = std::move(selected_texture_candidate);
    m_authenticated_material_scripts =
        std::move(material_script_candidate);
    m_authenticated_package_archive_retained_bytes -= removed_archive_bytes;
    m_authenticated_package_archive_manifest_accounting =
        manifest_accounting_candidate;
    if (reset_material_script_candidate)
    {
        m_authenticated_material_script_candidate.reset();
    }
    if (reset_aborted_material_script_candidate)
    {
        m_aborted_material_script_candidate.reset();
    }
    if (clear_scripting_resource_group)
    {
        m_scripting_resource_group.clear();
    }
#else
    this->AdvanceLegacyMaterialGroupGenerationLocked(resource_group);
    m_package_archives_by_group.erase(resource_group);
    m_authenticated_package_archives_by_group.erase(resource_group);
    m_authenticated_package_archive_bindings_by_group.erase(resource_group);
    m_package_materials_by_group.erase(resource_group);
    m_authenticated_materials_by_group.erase(resource_group);
    m_generated_material_fallbacks_by_group.erase(resource_group);
    m_generated_material_names_by_group.erase(resource_group);
    m_generated_material_bindings_by_group.erase(resource_group);
    m_reported_material_resolutions_by_group.erase(resource_group);
    m_authorized_texture_fallbacks_by_group.erase(resource_group);
    m_reported_texture_fallbacks_by_group.erase(resource_group);
    m_committed_material_script_generations.erase(resource_group);
    if (m_scripting_resource_group == resource_group)
    {
        m_scripting_resource_group.clear();
        m_current_script_name.clear();
        m_current_script_package_owned = false;
        m_current_script_authenticated_sha256.clear();
    }
#endif
}

#if OGRE_VERSION_MAJOR >= 14
void ContentManager::AbortAuthenticatedMaterialScriptGroup(
    const Ogre::String& resource_group) noexcept
{
    if (!this->IsAuthenticatedResourceThread())
    {
        std::terminate();
    }
    try
    {
        {
            std::scoped_lock<std::mutex, std::mutex> material_lock(
                m_legacy_material_resolution_mutex,
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr &&
                m_authenticated_material_script_candidate->group ==
                    resource_group)
            {
                if (m_aborted_material_script_candidate != nullptr)
                {
                    // One serialized resource thread cannot have two active
                    // abort closures. Leave both generations unpublishable.
                    m_authenticated_material_script_candidate->poisoned = true;
                    return;
                }
                m_aborted_material_script_candidate =
                    std::move(m_authenticated_material_script_candidate);
            }
            if (m_scripting_resource_group == resource_group)
            {
                m_scripting_resource_group.clear();
            }
            // Generation start already revoked every live compatibility map.
            // Repeat the no-allocation erasure as a lifecycle backstop.
            m_package_materials_by_group.erase(resource_group);
            m_authenticated_materials_by_group.erase(resource_group);
            m_generated_material_fallbacks_by_group.erase(resource_group);
            m_generated_material_names_by_group.erase(resource_group);
            m_generated_material_bindings_by_group.erase(resource_group);
            m_reported_material_resolutions_by_group.erase(resource_group);
            m_authorized_texture_fallbacks_by_group.erase(resource_group);
            m_reported_texture_fallbacks_by_group.erase(resource_group);
            m_committed_material_script_generations.erase(resource_group);
        }
        // Keep exact provisional identities until ResourceGroupManager emits
        // its removal callbacks. Unregister releases this quarantine only after
        // native teardown has completed.
    }
    catch (...)
    {
        // This boundary is a teardown backstop. Authority was never published;
        // callers proceed to destroy the exact resource group.
    }
}
#endif

void ContentManager::resourceGroupScriptingStarted(
    const Ogre::String& group_name,
    size_t script_count)
{
#if OGRE_VERSION_MAJOR >= 14
    this->RequireAuthenticatedResourceThread(
        "ContentManager::resourceGroupScriptingStarted");
#endif
    (void)script_count;
    std::scoped_lock<std::mutex, std::mutex> legacy_material_lock(
        m_legacy_material_resolution_mutex,
        m_legacy_material_state_mutex);
#if OGRE_VERSION_MAJOR >= 14
    if (m_aborted_material_script_candidate != nullptr)
    {
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Cannot begin a material-script generation while an aborted "
            "native-resource teardown remains quarantined",
            "ContentManager::resourceGroupScriptingStarted");
    }
#endif
    const std::uint64_t generation =
        this->AdvanceLegacyMaterialGroupGenerationLocked(group_name);
    m_scripting_resource_group = group_name;
#if OGRE_VERSION_MAJOR >= 14
    auto candidate =
        std::make_unique<AuthenticatedMaterialScriptGroupCandidate>();
    candidate->group = group_name;
    candidate->generation = generation;
    candidate->staged_source_bytes =
        m_authenticated_material_scripts.retained_source_bytes();
    candidate->staged_identity_bytes =
        m_authenticated_material_scripts.retained_identity_bytes();
    candidate->staged_source_count =
        m_authenticated_material_scripts.source_count();
    candidate->staged_receipt_count =
        m_authenticated_material_scripts.size();
    candidate->texture_receipts = m_authenticated_texture_receipts;
    m_authenticated_material_script_candidate = std::move(candidate);
#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
    if (m_force_next_resource_pack_generation_failure_for_testing)
    {
        m_force_next_resource_pack_generation_failure_for_testing = false;
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Injected resource-pack initialization failure after generation start",
            "ContentManager::resourceGroupScriptingStarted");
    }
#endif
#else
    m_current_script_name.clear();
    m_current_script_package_owned = false;
    m_current_script_authenticated_sha256.clear();
#endif
}

void ContentManager::scriptParseStarted(
    const Ogre::String& script_name,
    bool& skip_this_script)
{
#if OGRE_VERSION_MAJOR >= 14
    this->RequireAuthenticatedResourceThread(
        "ContentManager::scriptParseStarted");
#endif
    std::lock_guard<std::mutex> state_lock(
        m_legacy_material_state_mutex);
#if OGRE_VERSION_MAJOR >= 14
    AuthenticatedMaterialScriptGroupCandidate* candidate =
        m_authenticated_material_script_candidate.get();
    if (candidate == nullptr ||
        candidate->group != m_scripting_resource_group ||
        candidate->parse_active ||
        m_next_authenticated_material_script_parse_token ==
            (std::numeric_limits<std::uint64_t>::max)())
    {
        if (candidate != nullptr)
        {
            candidate->poisoned = true;
        }
        (void)skip_this_script;
        return;
    }
    ++m_next_authenticated_material_script_parse_token;
    candidate->parse_active = true;
    candidate->parse_root_open_observed = false;
    candidate->parse_root_untrusted = false;
    candidate->parse_has_authenticated_root = false;
    candidate->pending_import_open = false;
    candidate->parse_token =
        m_next_authenticated_material_script_parse_token;
    candidate->next_source_open_ordinal = 0U;
    candidate->source_open_attempt_count = 0U;
    candidate->next_event_ordinal = 0U;
    candidate->root_script_request = script_name;
    candidate->pending_import_name.clear();
    candidate->pending_import_group.clear();
#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
    if (m_force_next_resource_pack_script_parse_failure_for_testing)
    {
        m_force_next_resource_pack_script_parse_failure_for_testing = false;
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            "Injected resource-pack failure during native material-script parsing",
            "ContentManager::scriptParseStarted");
    }
#endif
#else
    m_current_script_name = script_name;
    m_current_script_package_owned = false;
    m_current_script_authenticated_sha256.clear();
#endif
    (void)skip_this_script;
}

void ContentManager::scriptParseEnded(
    const Ogre::String& script_name,
    bool skipped)
{
#if OGRE_VERSION_MAJOR >= 14
    this->RequireAuthenticatedResourceThread(
        "ContentManager::scriptParseEnded");
#endif
    std::scoped_lock<std::mutex, std::mutex> material_lock(
        m_legacy_material_resolution_mutex,
        m_legacy_material_state_mutex);
#if OGRE_VERSION_MAJOR >= 14
    AuthenticatedMaterialScriptGroupCandidate* candidate =
        m_authenticated_material_script_candidate.get();
    if (candidate == nullptr || !candidate->parse_active ||
        candidate->root_script_request != script_name)
    {
        if (candidate != nullptr)
        {
            candidate->poisoned = true;
        }
        return;
    }

    if (candidate->parse_has_authenticated_root)
    {
        try
        {
            if (skipped || candidate->pending_import_open)
            {
                candidate->poisoned = true;
            }
            for (const auto& source : candidate->sources)
            {
                if (source.input.metadata.parse_token ==
                        candidate->parse_token &&
                    !source.delivered)
                {
                    candidate->poisoned = true;
                }
            }
            Ogre::MaterialManager* manager =
                Ogre::MaterialManager::getSingletonPtr();
            for (auto& material : candidate->materials)
            {
                if (material.parse_token != candidate->parse_token)
                {
                    continue;
                }
                const auto& binding = material.input.binding;
                const Ogre::ResourcePtr by_handle =
                    manager != nullptr
                        ? manager->getByHandle(
                              static_cast<Ogre::ResourceHandle>(
                                  binding.material_handle))
                        : Ogre::ResourcePtr();
                const Ogre::ResourcePtr by_name =
                    manager != nullptr
                        ? manager->getResourceByName(
                              binding.exact_material_name,
                              binding.exact_group)
                        : Ogre::ResourcePtr();
                if (!material.retained_material || manager == nullptr ||
                    by_handle.get() != material.retained_material.get() ||
                    by_name.get() != material.retained_material.get() ||
                    material.retained_material->getCreator() != manager ||
                    material.retained_material->getHandle() !=
                        static_cast<Ogre::ResourceHandle>(
                            binding.material_handle) ||
                    material.retained_material->getName() !=
                        binding.exact_material_name ||
                    material.retained_material->getGroup() !=
                        binding.exact_group ||
                    material.retained_material->getOrigin() !=
                        binding.exact_origin)
                {
                    candidate->poisoned = true;
                }
                else
                {
                    material.finalized = true;
                }
            }
        }
        catch (...)
        {
            candidate->poisoned = true;
        }
    }
    candidate->parse_active = false;
    candidate->parse_root_open_observed = false;
    candidate->parse_root_untrusted = false;
    candidate->parse_has_authenticated_root = false;
    candidate->pending_import_open = false;
    candidate->parse_token = 0U;
    candidate->root_script_request.clear();
    candidate->pending_import_name.clear();
    candidate->pending_import_group.clear();
#else
    (void)script_name;
    (void)skipped;
    m_current_script_name.clear();
    m_current_script_package_owned = false;
    m_current_script_authenticated_sha256.clear();
#endif
}

void ContentManager::resourceGroupScriptingEnded(
    const Ogre::String& group_name)
{
#if OGRE_VERSION_MAJOR >= 14
    this->RequireAuthenticatedResourceThread(
        "ContentManager::resourceGroupScriptingEnded");
    bool publication_complete = false;
    std::uint64_t candidate_generation = 0U;
    {
        std::scoped_lock<std::mutex, std::mutex> material_lock(
            m_legacy_material_resolution_mutex,
            m_legacy_material_state_mutex);
        AuthenticatedMaterialScriptGroupCandidate* candidate =
            m_authenticated_material_script_candidate.get();
        if (candidate != nullptr && candidate->group == group_name)
        {
            candidate_generation = candidate->generation;
            try
            {
                const auto current_generation =
                    m_legacy_material_group_generations.find(group_name);
                if (candidate->parse_active ||
                    candidate->pending_import_open || candidate->poisoned ||
                    current_generation ==
                        m_legacy_material_group_generations.end() ||
                    current_generation->second != candidate->generation)
                {
                    candidate->poisoned = true;
                }

                std::vector<
                    Render::Ogre14AuthenticatedMaterialScriptSourceInput>
                        sources;
                std::vector<
                    Render::Ogre14AuthenticatedMaterialScriptMaterialInput>
                        materials;
                if (!candidate->poisoned)
                {
                    sources.reserve(candidate->sources.size());
                    for (const auto& source : candidate->sources)
                    {
                        if (!source.delivered || source.expected_stream)
                        {
                            candidate->poisoned = true;
                            break;
                        }
                        sources.push_back(source.input);
                    }
                }
                if (!candidate->poisoned)
                {
                    materials.reserve(candidate->materials.size());
                    Ogre::MaterialManager* manager =
                        Ogre::MaterialManager::getSingletonPtr();
                    for (const auto& material : candidate->materials)
                    {
                        const auto& binding = material.input.binding;
                        const Ogre::ResourcePtr by_handle =
                            manager != nullptr
                                ? manager->getByHandle(
                                      static_cast<Ogre::ResourceHandle>(
                                          binding.material_handle))
                                : Ogre::ResourcePtr();
                        const Ogre::ResourcePtr by_name =
                            manager != nullptr
                                ? manager->getResourceByName(
                                      binding.exact_material_name,
                                      binding.exact_group)
                                : Ogre::ResourcePtr();
                        if (!material.finalized ||
                            !material.retained_material ||
                            manager == nullptr ||
                            material.retained_material->getCreator() !=
                                manager ||
                            by_handle.get() !=
                                material.retained_material.get() ||
                            by_name.get() !=
                                material.retained_material.get() ||
                            material.retained_material->getName() !=
                                binding.exact_material_name ||
                            material.retained_material->getGroup() !=
                                binding.exact_group ||
                            material.retained_material->getOrigin() !=
                                binding.exact_origin)
                        {
                            candidate->poisoned = true;
                            break;
                        }
                        materials.push_back(material.input);
                    }
                }

                if (!candidate->poisoned)
                {
                    Ogre::TextureManager* manager =
                        Ogre::TextureManager::getSingletonPtr();
                    for (const auto& texture : candidate->textures)
                    {
                        const auto* metadata = texture.receipt.metadata();
                        if (metadata == nullptr ||
                            !texture.retained_texture || manager == nullptr ||
                            metadata->source.binding.resource_state_count ==
                                (std::numeric_limits<std::uint64_t>::max)())
                        {
                            candidate->poisoned = true;
                            break;
                        }
                        const auto& source = metadata->source;
                        const auto& binding = source.binding;
                        const std::uint64_t loaded_state_count =
                            binding.resource_state_count + 1U;
                        const Ogre::ResourcePtr by_handle =
                            manager->getByHandle(
                                static_cast<Ogre::ResourceHandle>(
                                    binding.resource_handle));
                        const Ogre::ResourcePtr by_name =
                            manager->getResourceByName(
                                binding.exact_resource_name,
                                source.effective_resource_group);
                        Render::Ogre14AuthenticatedTextureReceipt
                            registered_receipt;
                        const Render::ValidationResult registered =
                            candidate->texture_receipts.FindResource(
                                source.effective_resource_group,
                                source.group_generation,
                                binding.resource_pointer_token,
                                binding.resource_handle,
                                binding.exact_resource_name,
                                registered_receipt);
                        if (!registered ||
                            !registered_receipt.SharesImmutableStateWith(
                                texture.receipt) ||
                            texture.retained_texture->getCreator() != manager ||
                            !texture.retained_texture->isLoaded() ||
                            by_handle.get() != texture.retained_texture.get() ||
                            by_name.get() != texture.retained_texture.get() ||
                            reinterpret_cast<std::uintptr_t>(
                                texture.retained_texture.get()) !=
                                binding.resource_pointer_token ||
                            static_cast<std::uint64_t>(
                                texture.retained_texture->getHandle()) !=
                                binding.resource_handle ||
                            texture.retained_texture->getName() !=
                                binding.exact_resource_name ||
                            texture.retained_texture->getGroup() !=
                                source.effective_resource_group ||
                            static_cast<std::uint64_t>(
                                texture.retained_texture->getStateCount()) !=
                                loaded_state_count)
                        {
                            candidate->poisoned = true;
                            break;
                        }
                    }
                }

                Render::Ogre14AuthenticatedMaterialScriptRegistry
                    material_registry_candidate =
                        m_authenticated_material_scripts;
                if (!candidate->poisoned)
                {
                    const Render::ValidationResult publication =
                        material_registry_candidate.CommitWholeGroup(
                            group_name, candidate->generation,
                            sources, materials);
                    if (!publication)
                    {
                        candidate->poisoned = true;
                    }
                }

                auto package_materials_candidate =
                    m_package_materials_by_group;
                auto authenticated_materials_candidate =
                    m_authenticated_materials_by_group;
                auto generated_materials_candidate =
                    m_generated_material_fallbacks_by_group;
                auto generated_names_candidate =
                    m_generated_material_names_by_group;
                auto reported_materials_candidate =
                    m_reported_material_resolutions_by_group;
                auto authorized_textures_candidate =
                    m_authorized_texture_fallbacks_by_group;
                auto reported_textures_candidate =
                    m_reported_texture_fallbacks_by_group;
                auto committed_generations_candidate =
                    m_committed_material_script_generations;
                if (!candidate->poisoned)
                {
                    package_materials_candidate[group_name] =
                        candidate->package_materials;
                    authenticated_materials_candidate[group_name] =
                        candidate->authenticated_materials;
                    generated_materials_candidate[group_name] =
                        candidate->generated_material_fallbacks;
                    generated_names_candidate[group_name] =
                        candidate->generated_material_names;
                    reported_materials_candidate[group_name] =
                        candidate->reported_material_resolutions;
                    authorized_textures_candidate[group_name] =
                        candidate->authorized_texture_fallbacks;
                    reported_textures_candidate[group_name] =
                        candidate->reported_texture_fallbacks;
                    committed_generations_candidate[group_name] =
                        candidate->generation;

                    static_assert(noexcept(
                        m_authenticated_material_scripts =
                            std::move(material_registry_candidate)));
                    static_assert(noexcept(
                        m_authenticated_texture_receipts =
                            std::move(candidate->texture_receipts)));
                    static_assert(noexcept(
                        m_package_materials_by_group.swap(
                            package_materials_candidate)));
                    static_assert(noexcept(
                        m_authenticated_materials_by_group.swap(
                            authenticated_materials_candidate)));
                    static_assert(noexcept(
                        m_generated_material_fallbacks_by_group.swap(
                            generated_materials_candidate)));
                    static_assert(noexcept(
                        m_generated_material_names_by_group.swap(
                            generated_names_candidate)));
                    static_assert(noexcept(
                        m_reported_material_resolutions_by_group.swap(
                            reported_materials_candidate)));
                    static_assert(noexcept(
                        m_authorized_texture_fallbacks_by_group.swap(
                            authorized_textures_candidate)));
                    static_assert(noexcept(
                        m_reported_texture_fallbacks_by_group.swap(
                            reported_textures_candidate)));
                    static_assert(noexcept(
                        m_committed_material_script_generations.swap(
                            committed_generations_candidate)));

                    m_authenticated_material_scripts =
                        std::move(material_registry_candidate);
                    m_authenticated_texture_receipts =
                        std::move(candidate->texture_receipts);
                    m_package_materials_by_group.swap(
                        package_materials_candidate);
                    m_authenticated_materials_by_group.swap(
                        authenticated_materials_candidate);
                    m_generated_material_fallbacks_by_group.swap(
                        generated_materials_candidate);
                    m_generated_material_names_by_group.swap(
                        generated_names_candidate);
                    m_reported_material_resolutions_by_group.swap(
                        reported_materials_candidate);
                    m_authorized_texture_fallbacks_by_group.swap(
                        authorized_textures_candidate);
                    m_reported_texture_fallbacks_by_group.swap(
                        reported_textures_candidate);
                    m_committed_material_script_generations.swap(
                        committed_generations_candidate);
                    publication_complete = true;
                }
            }
            catch (...)
            {
                candidate->poisoned = true;
            }
            m_authenticated_material_script_candidate.reset();
        }
        else if (candidate != nullptr)
        {
            candidate->poisoned = true;
            m_authenticated_material_script_candidate.reset();
        }
        m_scripting_resource_group.clear();
    }

    if (!publication_complete)
    {
        LOG(fmt::format(
            "[RoR|ContentManager|AuthenticatedMaterialScript] Discarded "
            "incomplete group '{}' generation {}",
            group_name, candidate_generation));
        OGRE_EXCEPT(
            Ogre::Exception::ERR_INVALID_STATE,
            fmt::format(
                "Authenticated material-script group '{}' could not publish "
                "one complete generation",
                group_name),
            "ContentManager::resourceGroupScriptingEnded");
    }

    // Compatibility mutation is deliberately downstream of source-authority
    // publication. It may improve a legacy visual, but it cannot mint or alter
    // the receipt for the exact script bytes and native Material creation.
    try
    {
        this->ApplyShaderCompatibilityFallbacks(group_name);
    }
    catch (const std::exception& error)
    {
        LOG(fmt::format(
            "[RoR|ContentManager|AuthenticatedMaterialScript] Post-commit "
            "compatibility lowering for '{}' failed: {}",
            group_name, error.what()));
    }
    catch (...)
    {
        LOG(fmt::format(
            "[RoR|ContentManager|AuthenticatedMaterialScript] Post-commit "
            "compatibility lowering for '{}' failed",
            group_name));
    }
#else
    this->ApplyShaderCompatibilityFallbacks(group_name);
    m_scripting_resource_group.clear();
    m_current_script_name.clear();
    m_current_script_package_owned = false;
    m_current_script_authenticated_sha256.clear();
#endif
}

void ContentManager::resourceRemove(const Ogre::ResourcePtr& resource)
{
#if OGRE_VERSION_MAJOR >= 14
    if (!this->IsAuthenticatedResourceThread())
    {
        // ResourceManager invokes this after removing its native indices.
        // A cross-thread callback cannot be rolled back honestly, so fail-stop
        // instead of throwing into a caller which might attempt recovery with
        // stale authenticated authority.
        std::terminate();
    }
    if (!resource)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        if (m_aborted_material_script_candidate != nullptr &&
            resource->getGroup() ==
                m_aborted_material_script_candidate->group &&
            ((Ogre::TextureManager::getSingletonPtr() != nullptr &&
              resource->getCreator() ==
                  Ogre::TextureManager::getSingletonPtr()) ||
             (Ogre::MaterialManager::getSingletonPtr() != nullptr &&
              resource->getCreator() ==
                  Ogre::MaterialManager::getSingletonPtr())))
        {
            // This group generation never published either registry. Native
            // teardown is expected and must not poison unrelated live groups.
            return;
        }
    }
    if (
        Ogre::MeshManager::getSingletonPtr() != nullptr &&
        resource->getCreator() == Ogre::MeshManager::getSingletonPtr())
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        m_authenticated_mesh_bindings.erase(resource.get());
    }
    if (Ogre::TextureManager::getSingletonPtr() != nullptr &&
        resource->getCreator() == Ogre::TextureManager::getSingletonPtr())
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        this->EraseSelectedTextureSourceStageLocked(resource.get());
        const auto generation =
            m_legacy_material_group_generations.find(resource->getGroup());
        if (generation != m_legacy_material_group_generations.end())
        {
            const Render::ValidationResult selected_removal =
                Render::RemoveOgre14SelectedTextureSourceResource(
                    resource->getGroup(), generation->second,
                    reinterpret_cast<std::uintptr_t>(resource.get()),
                    static_cast<std::uint64_t>(resource->getHandle()),
                    resource->getName(), m_selected_texture_sources);
            if (!selected_removal)
            {
                // ResourceManager has already removed the external lookup
                // indices. A failed matching COW revocation is the one
                // boundary where ordinary selected-source authority must be
                // poisoned terminally.
                Render::PoisonOgre14SelectedTextureSourceRegistry(
                    m_selected_texture_sources);
                LOG(fmt::format(
                    "[RoR|ContentManager|SelectedTextureSource] Refused "
                    "stale resource removal for '{}' in group '{}'; "
                    "selected-source authority is terminally poisoned: {} "
                    "({})",
                    resource->getName(), resource->getGroup(),
                    selected_removal.detail, selected_removal.field));
            }
        }
        const Render::ValidationResult removal =
            Render::RemoveOgre14AuthenticatedTextureResource(
                resource->getGroup(),
                reinterpret_cast<std::uintptr_t>(resource.get()),
                static_cast<std::uint64_t>(resource->getHandle()),
                resource->getName(),
                m_authenticated_texture_receipts);
        if (!removal)
        {
            // OGRE has already removed the resource from its manager indices.
            // Keeping the old immutable receipt publication would let a later
            // frame authority authenticate a resource that no longer exists.
            // Poison first; logging and recovery paths are allowed to fail,
            // but current authority must never remain available.
            Render::PoisonOgre14AuthenticatedTextureReceiptRegistry(
                m_authenticated_texture_receipts);
            LOG(fmt::format(
                "[RoR|ContentManager|AuthenticatedTexture] Refused stale "
                "resource removal for '{}' in group '{}'; texture authority "
                "is terminally poisoned: {} ({})",
                resource->getName(),
                resource->getGroup(),
                removal.detail,
                removal.field));
        }
    }
    if (Ogre::MaterialManager::getSingletonPtr() != nullptr &&
        resource->getCreator() == Ogre::MaterialManager::getSingletonPtr())
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const Ogre::String& removed_group = resource->getGroup();
        const Ogre::String& removed_name = resource->getName();

        // Compatibility indexes are hints, not minting authorities. Revoke
        // every name-only hint before updating the immutable registry so a
        // foreign replacement with the same group/name can never inherit an
        // authenticated alias or generated-material identity.
        const auto package_group =
            m_package_materials_by_group.find(removed_group);
        if (package_group != m_package_materials_by_group.end())
        {
            package_group->second.erase(removed_name);
            if (package_group->second.empty())
            {
                m_package_materials_by_group.erase(package_group);
            }
        }
        const auto authenticated_group =
            m_authenticated_materials_by_group.find(removed_group);
        if (authenticated_group != m_authenticated_materials_by_group.end())
        {
            for (auto archive = authenticated_group->second.begin();
                 archive != authenticated_group->second.end();)
            {
                archive->second.erase(removed_name);
                if (archive->second.empty())
                {
                    archive = authenticated_group->second.erase(archive);
                }
                else
                {
                    ++archive;
                }
            }
            if (authenticated_group->second.empty())
            {
                m_authenticated_materials_by_group.erase(
                    authenticated_group);
            }
        }
        const auto generated_binding_group =
            m_generated_material_bindings_by_group.find(removed_group);
        if (generated_binding_group !=
            m_generated_material_bindings_by_group.end())
        {
            generated_binding_group->second.erase(removed_name);
            if (generated_binding_group->second.empty())
            {
                m_generated_material_bindings_by_group.erase(
                    generated_binding_group);
            }
        }
        const auto generated_name_group =
            m_generated_material_names_by_group.find(removed_group);
        if (generated_name_group != m_generated_material_names_by_group.end())
        {
            generated_name_group->second.erase(removed_name);
            if (generated_name_group->second.empty())
            {
                m_generated_material_names_by_group.erase(
                    generated_name_group);
            }
        }
        const auto generated_request_group =
            m_generated_material_fallbacks_by_group.find(removed_group);
        if (generated_request_group !=
            m_generated_material_fallbacks_by_group.end())
        {
            for (auto archive = generated_request_group->second.begin();
                 archive != generated_request_group->second.end();)
            {
                for (auto request = archive->second.begin();
                     request != archive->second.end();)
                {
                    if (request->second == removed_name)
                    {
                        request = archive->second.erase(request);
                    }
                    else
                    {
                        ++request;
                    }
                }
                if (archive->second.empty())
                {
                    archive = generated_request_group->second.erase(archive);
                }
                else
                {
                    ++archive;
                }
            }
            if (generated_request_group->second.empty())
            {
                m_generated_material_fallbacks_by_group.erase(
                    generated_request_group);
            }
        }
        const Render::ValidationResult removal =
            m_authenticated_material_scripts.RemoveMaterial(
                removed_group,
                reinterpret_cast<std::uintptr_t>(resource.get()),
                static_cast<std::uint64_t>(resource->getHandle()),
                removed_name, resource->getOrigin());
        if (!removal)
        {
            // MaterialManager has already erased its lookup indices. Never
            // retain an immutable registry snapshot which can still mint a
            // resolution for that removed native object.
            m_authenticated_material_scripts.Poison();
            LOG(fmt::format(
                "[RoR|ContentManager|AuthenticatedMaterialScript] Refused "
                "stale removal for '{}' in group '{}'; material-script "
                "authority is terminally poisoned: {} ({})",
                resource->getName(), resource->getGroup(),
                removal.detail, removal.field));
        }
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
    this->RequireAuthenticatedResourceThread(
        "ContentManager::processMaterialName");
    if (mesh == nullptr ||
        name == nullptr ||
        name->empty() ||
        Ogre::MaterialManager::getSingletonPtr() == nullptr)
    {
        return;
    }

    // Authenticated package deserialization is deliberately confined to the
    // bound OGRE resource/render thread. The mutex additionally serializes
    // re-entrant material lookup/creation and its generated-name transaction.
    std::lock_guard<std::mutex> resolution_lock(
        m_legacy_material_resolution_mutex);

    Ogre::String group;
    std::string resolution_archive_sha256;
    std::uint64_t resolution_generation = 0U;
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
        const auto committed_generation =
            m_committed_material_script_generations.find(
                authenticated_mesh->second.group);
        if (
            current_generation ==
                m_legacy_material_group_generations.end() ||
            committed_generation ==
                m_committed_material_script_generations.end() ||
            committed_generation->second != current_generation->second ||
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
        resolution_generation = current_generation->second;
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
    Ogre::MaterialManager& manager = Ogre::MaterialManager::getSingleton();
    Ogre::MaterialPtr resolved_material;
    Ogre::MaterialPtr created_material;
    Render::Ogre14AuthenticatedMaterialScriptResolution alias_authority;
    bool report_resolution = false;

    auto generated_materials_candidate =
        m_generated_material_fallbacks_by_group;
    auto generated_names_candidate =
        m_generated_material_names_by_group;
    auto generated_bindings_candidate =
        m_generated_material_bindings_by_group;
    auto reported_materials_candidate =
        m_reported_material_resolutions_by_group;
    const auto has_exact_generated_binding =
        [&](const Ogre::String& candidate_name,
            const Ogre::MaterialPtr& material) -> bool
    {
        if (!material)
        {
            return false;
        }
        const auto binding_group =
            generated_bindings_candidate.find(group);
        if (binding_group == generated_bindings_candidate.end())
        {
            return false;
        }
        const auto binding = binding_group->second.find(candidate_name);
        return binding != binding_group->second.end() &&
            binding->second.material_pointer_token ==
                reinterpret_cast<std::uintptr_t>(material.get()) &&
            binding->second.material_handle ==
                static_cast<Ogre::ResourceHandle>(material->getHandle()) &&
            binding->second.group_generation == resolution_generation &&
            binding->second.exact_origin == material->getOrigin();
    };
    try
    {
        if (resolution.disposition ==
            LegacyMaterialReferenceDisposition::ALIAS)
        {
            resolved_name = resolution.target_material;
            resolved_material = manager.getByName(resolved_name, group);
            const Ogre::ResourcePtr by_handle = resolved_material
                ? manager.getByHandle(resolved_material->getHandle())
                : Ogre::ResourcePtr();
            if (!resolved_material ||
                resolved_material->getCreator() != &manager ||
                resolved_material->getName() != resolved_name ||
                resolved_material->getGroup() != group ||
                by_handle.get() != resolved_material.get())
            {
                return;
            }
            {
                std::lock_guard<std::mutex> state_lock(
                    m_legacy_material_state_mutex);
                const auto current_generation =
                    m_legacy_material_group_generations.find(group);
                const auto committed_generation =
                    m_committed_material_script_generations.find(group);
                const std::uintptr_t resolver_pointer_token =
                    reinterpret_cast<std::uintptr_t>(
                        static_cast<const Render::
                            IOgre14AuthenticatedMaterialScriptResolver*>(
                                this));
                const Render::ValidationResult mint =
                    current_generation ==
                            m_legacy_material_group_generations.end() ||
                        committed_generation ==
                            m_committed_material_script_generations.end() ||
                        current_generation->second != resolution_generation ||
                        committed_generation->second != resolution_generation
                    ? Render::ValidationResult::Failure(
                          Render::ValidationCode::SEQUENCE_MISMATCH,
                          "legacy_material_alias.group_generation",
                          "alias target generation is not current")
                    : m_authenticated_material_scripts.MintResolution(
                          group, resolution_generation,
                          reinterpret_cast<std::uintptr_t>(
                              resolved_material.get()),
                          static_cast<std::uint64_t>(
                              resolved_material->getHandle()),
                          resolved_material->getName(),
                          resolved_material->getOrigin(),
                          resolver_pointer_token, alias_authority);
                const Render::Ogre14AuthenticatedMaterialScriptReceipt*
                    receipt = alias_authority.receipt();
                const Render::Ogre14AuthenticatedMaterialScriptSourceMetadata*
                    primary = receipt != nullptr
                        ? receipt->source_metadata()
                        : nullptr;
                if (!mint || primary == nullptr ||
                    primary->archive_sha256 != resolution_archive_sha256)
                {
                    return;
                }
            }
        }
        else
        {
            auto& generated_by_request =
                generated_materials_candidate[group]
                    [resolution_archive_sha256];
            const auto existing_generated =
                generated_by_request.find(requested);
            if (existing_generated != generated_by_request.end())
            {
                resolved_name = existing_generated->second;
                resolved_material = manager.getByName(resolved_name, group);
                const auto generated_group =
                    generated_names_candidate.find(group);
                if (!resolved_material ||
                    generated_group == generated_names_candidate.end() ||
                    generated_group->second.count(resolved_name) == 0U ||
                    !has_exact_generated_binding(
                        resolved_name, resolved_material))
                {
                    return;
                }
            }
            else
            {
                resolved_name = BuildLegacyMaterialFallbackResourceName(
                    resolution_archive_sha256, requested);
                resolved_material = manager.getByName(resolved_name, group);
                const auto generated_group =
                    generated_names_candidate.find(group);
                if (resolved_material &&
                    (generated_group == generated_names_candidate.end() ||
                     generated_group->second.count(resolved_name) == 0U ||
                     !has_exact_generated_binding(
                         resolved_name, resolved_material)))
                {
                    return;
                }
                generated_by_request.emplace(requested, resolved_name);
                generated_names_candidate[group].insert(resolved_name);
                if (!resolved_material)
                {
                    created_material = manager.create(resolved_name, group);
                    resolved_material = created_material;
                    if (!resolved_material ||
                        resolved_material->getCreator() != &manager ||
                        resolved_material->getNumTechniques() == 0U ||
                        resolved_material->getTechnique(0U)->getNumPasses() ==
                            0U)
                    {
                        throw std::runtime_error(
                            "generated material has no complete fixed-function pass");
                    }
                    Ogre::Pass* pass =
                        resolved_material->getTechnique(0U)->getPass(0U);
                    const float red =
                        static_cast<float>(resolution.color.red) / 255.0f;
                    const float green =
                        static_cast<float>(resolution.color.green) / 255.0f;
                    const float blue =
                        static_cast<float>(resolution.color.blue) / 255.0f;
                    pass->setLightingEnabled(true);
                    pass->setAmbient(
                        red * 0.45f, green * 0.45f, blue * 0.45f);
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
                    resolved_material->setReceiveShadows(true);
                    GeneratedMaterialBinding binding;
                    binding.material_pointer_token =
                        reinterpret_cast<std::uintptr_t>(
                            resolved_material.get());
                    binding.material_handle =
                        resolved_material->getHandle();
                    binding.group_generation = resolution_generation;
                    binding.exact_origin = resolved_material->getOrigin();
                    generated_bindings_candidate[group][resolved_name] =
                        std::move(binding);
                }
            }
        }

        report_resolution =
            reported_materials_candidate[group].insert(requested).second;

        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            const auto authenticated_mesh =
                m_authenticated_mesh_bindings.find(mesh);
            const auto current_generation =
                m_legacy_material_group_generations.find(group);
            const auto committed_generation =
                m_committed_material_script_generations.find(group);
            const Ogre::ResourcePtr by_handle = resolved_material
                ? manager.getByHandle(resolved_material->getHandle())
                : Ogre::ResourcePtr();
            const Ogre::ResourcePtr by_name =
                manager.getResourceByName(resolved_name, group);
            const std::uintptr_t resolver_pointer_token =
                reinterpret_cast<std::uintptr_t>(
                    static_cast<const Render::
                        IOgre14AuthenticatedMaterialScriptResolver*>(this));
            const bool exact_material_authority =
                resolution.disposition ==
                        LegacyMaterialReferenceDisposition::ALIAS
                    ? m_authenticated_material_scripts.RevalidateResolution(
                          alias_authority, resolver_pointer_token,
                          reinterpret_cast<std::uintptr_t>(
                              resolved_material.get()),
                          static_cast<std::uint64_t>(
                              resolved_material->getHandle()),
                          group, resolved_name,
                          resolved_material->getOrigin())
                    : has_exact_generated_binding(
                          resolved_name, resolved_material);
            if (authenticated_mesh ==
                    m_authenticated_mesh_bindings.end() ||
                current_generation ==
                    m_legacy_material_group_generations.end() ||
                committed_generation ==
                    m_committed_material_script_generations.end() ||
                committed_generation->second != current_generation->second ||
                authenticated_mesh->second.group_generation !=
                    current_generation->second ||
                authenticated_mesh->second.archive_sha256 !=
                    resolution_archive_sha256 ||
                !mesh->isLoading() ||
                authenticated_mesh->second.mesh_group != mesh->getGroup() ||
                authenticated_mesh->second.name != mesh->getName() ||
                authenticated_mesh->second.handle != mesh->getHandle() ||
                authenticated_mesh->second.state_count !=
                    mesh->getStateCount() ||
                !resolved_material ||
                resolved_material->getCreator() != &manager ||
                resolved_material->getName() != resolved_name ||
                resolved_material->getGroup() != group ||
                by_handle.get() != resolved_material.get() ||
                by_name.get() != resolved_material.get() ||
                !exact_material_authority)
            {
                throw std::runtime_error(
                    "material or mesh generation changed before publication");
            }

            Ogre::String published_name = resolved_name;
            static_assert(noexcept(name->swap(published_name)));
            static_assert(noexcept(
                m_generated_material_fallbacks_by_group.swap(
                    generated_materials_candidate)));
            static_assert(noexcept(
                m_generated_material_names_by_group.swap(
                    generated_names_candidate)));
            static_assert(noexcept(
                m_generated_material_bindings_by_group.swap(
                    generated_bindings_candidate)));
            static_assert(noexcept(
                m_reported_material_resolutions_by_group.swap(
                    reported_materials_candidate)));
            name->swap(published_name);
            m_generated_material_fallbacks_by_group.swap(
                generated_materials_candidate);
            m_generated_material_names_by_group.swap(
                generated_names_candidate);
            m_generated_material_bindings_by_group.swap(
                generated_bindings_candidate);
            m_reported_material_resolutions_by_group.swap(
                reported_materials_candidate);
        }
    }
    catch (...)
    {
        if (created_material)
        {
            try
            {
                const Ogre::ResourcePtr by_handle =
                    manager.getByHandle(created_material->getHandle());
                const Ogre::ResourcePtr by_name =
                    manager.getResourceByName(
                        created_material->getName(),
                        created_material->getGroup());
                if (by_handle.get() == created_material.get() &&
                    by_name.get() == created_material.get())
                {
                    manager.remove(created_material->getHandle());
                }
            }
            catch (...)
            {
            }
        }
        return;
    }

    if (report_resolution)
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
    this->RequireAuthenticatedResourceThread(
        "ContentManager::processMeshCompleted");
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
DECLARE_RESOURCE_PACK( POSTPROCESS,           "postprocess",          "PostProcessRG");
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

    const bool use_default_group = override_rgn.empty();
    Ogre::String rg_name;
    if (!use_default_group) // Custom RG defined?
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

    const bool resource_group_was_present =
        rgm.resourceGroupExists(rg_name);
    std::stringstream log_msg;
    log_msg << "[RoR|ContentManager] Loading resource pack \"" << resource_pack.name << "\" to group \"" << rg_name << "\"";
    std::string dir_path = PathCombine(App::sys_resources_dir->getStr(), resource_pack.name);
    std::string zip_path = dir_path + ".zip";
    std::string selected_source_location;
    Ogre::String selected_source_type;
    if (FileExists(zip_path))
    {
        log_msg << " (ZIP archive)";
        LOG(log_msg.str());
        selected_source_location = zip_path;
        selected_source_type = "Zip";
    }
    else
    {
        if (FolderExists(dir_path))
        {
            log_msg << " (directory)";
            LOG(log_msg.str());
            selected_source_location = dir_path;
            selected_source_type = "FileSystem";
        }
        else
        {
            log_msg << " failed, data not found.";
            throw std::runtime_error(log_msg.str());
        }
    }

    // Built-in material scripts are intentionally packaged separately from
    // their texture bytes. materials.zip owns ror.material while textures.zip
    // owns dashboard.dds, seat.dds, the wheel textures, and the other ordinary
    // sources it names; particles.zip similarly names smoke.dds. OGRE's global
    // group fallback can find those bytes without adding the archive to the
    // material's group, but the exact selected-source listener must refuse
    // that ambiguity. Mount and register the shared texture pack in each
    // built-in script-owning group before scripting starts. This is one
    // package relationship, not a list of special-cased material names.
    const bool add_builtin_texture_dependency =
        use_default_group && resource_pack.name != nullptr &&
        (std::string(resource_pack.name) == ResourcePack::MATERIALS.name ||
         std::string(resource_pack.name) == ResourcePack::PARTICLES.name);
    std::string builtin_texture_dependency_location;
    Ogre::String builtin_texture_dependency_type;
    if (add_builtin_texture_dependency)
    {
        const std::string dependency_dir = PathCombine(
            App::sys_resources_dir->getStr(), ResourcePack::TEXTURES.name);
        const std::string dependency_zip = dependency_dir + ".zip";
        if (FileExists(dependency_zip))
        {
            builtin_texture_dependency_location = dependency_zip;
            builtin_texture_dependency_type = "Zip";
        }
        else if (FolderExists(dependency_dir))
        {
            builtin_texture_dependency_location = dependency_dir;
            builtin_texture_dependency_type = "FileSystem";
        }
        else
        {
            throw std::runtime_error(
                "Built-in material scripts require the textures resource pack");
        }
    }

    Ogre::ArchiveManager& archive_manager =
        Ogre::ArchiveManager::getSingleton();
    const auto find_selected_archive = [&]() -> Ogre::Archive*
    {
        Ogre::Archive* selected = nullptr;
        Ogre::ArchiveManager::ArchiveMapIterator archives =
            archive_manager.getArchiveIterator();
        while (archives.hasMoreElements())
        {
            const Ogre::String archive_name = archives.peekNextKey();
            Ogre::Archive* archive = archives.getNext();
            if (archive_name != selected_source_location)
            {
                continue;
            }
            if (selected != nullptr || archive == nullptr ||
                archive->getName() != selected_source_location ||
                archive->getType() != selected_source_type)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Resource-pack source identity is already live with a different archive type or pointer",
                    "ContentManager::AddResourcePack");
            }
            selected = archive;
        }
        return selected;
    };
    Ogre::Archive* const selected_archive_was_live =
        find_selected_archive();
    const auto find_builtin_texture_dependency_archive =
        [&]() -> Ogre::Archive*
    {
        if (!add_builtin_texture_dependency)
        {
            return nullptr;
        }
        Ogre::Archive* selected = nullptr;
        Ogre::ArchiveManager::ArchiveMapIterator archives =
            archive_manager.getArchiveIterator();
        while (archives.hasMoreElements())
        {
            const Ogre::String archive_name = archives.peekNextKey();
            Ogre::Archive* archive = archives.getNext();
            if (archive_name != builtin_texture_dependency_location)
            {
                continue;
            }
            if (selected != nullptr || archive == nullptr ||
                archive->getName() != builtin_texture_dependency_location ||
                archive->getType() != builtin_texture_dependency_type)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Built-in texture dependency identity is already live with a different archive type or pointer",
                    "ContentManager::AddResourcePack");
            }
            selected = archive;
        }
        return selected;
    };
    Ogre::Archive* const builtin_texture_dependency_archive_was_live =
        find_builtin_texture_dependency_archive();
    std::size_t selected_source_location_count_before = 0U;
    if (resource_group_was_present)
    {
        const Ogre::ResourceGroupManager::LocationList& locations =
            rgm.getResourceLocationList(rg_name);
        for (const Ogre::ResourceGroupManager::ResourceLocation& location :
             locations)
        {
            if (location.archive == nullptr ||
                location.archive->getName() != selected_source_location)
            {
                continue;
            }
            if (selected_archive_was_live == nullptr ||
                location.archive != selected_archive_was_live ||
                location.archive->getType() != selected_source_type)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Resource-pack group contains a source location without exact live archive authority",
                    "ContentManager::AddResourcePack");
            }
            ++selected_source_location_count_before;
        }
    }
    const bool selected_source_location_was_present =
        selected_source_location_count_before != 0U;
#if OGRE_VERSION_MAJOR >= 14
    bool package_location_registered = false;
#endif
    const auto rollback_new_selected_archive = [&]() noexcept
    {
        if (selected_archive_was_live != nullptr)
        {
            return;
        }
        try
        {
            Ogre::Archive* const selected_archive =
                find_selected_archive();
            if (selected_archive == nullptr)
            {
                return;
            }
            const Ogre::StringVector groups = rgm.getResourceGroups();
            for (const Ogre::String& group : groups)
            {
                if (!rgm.resourceGroupExists(group))
                {
                    continue;
                }
                const Ogre::ResourceGroupManager::LocationList& locations =
                    rgm.getResourceLocationList(group);
                for (const Ogre::ResourceGroupManager::ResourceLocation&
                         location : locations)
                {
                    if (location.archive == selected_archive)
                    {
                        std::terminate();
                    }
                }
            }
            archive_manager.unload(selected_archive);
            if (find_selected_archive() != nullptr)
            {
                std::terminate();
            }
        }
        catch (...)
        {
            std::terminate();
        }
    };
    const auto rollback_new_builtin_texture_dependency_archive =
        [&]() noexcept
    {
        if (!add_builtin_texture_dependency ||
            builtin_texture_dependency_archive_was_live != nullptr)
        {
            return;
        }
        try
        {
            Ogre::Archive* const dependency_archive =
                find_builtin_texture_dependency_archive();
            if (dependency_archive == nullptr)
            {
                return;
            }
            const Ogre::StringVector groups = rgm.getResourceGroups();
            for (const Ogre::String& group : groups)
            {
                if (!rgm.resourceGroupExists(group))
                {
                    continue;
                }
                const Ogre::ResourceGroupManager::LocationList& locations =
                    rgm.getResourceLocationList(group);
                for (const Ogre::ResourceGroupManager::ResourceLocation&
                         location : locations)
                {
                    if (location.archive == dependency_archive)
                    {
                        std::terminate();
                    }
                }
            }
            archive_manager.unload(dependency_archive);
            if (find_builtin_texture_dependency_archive() != nullptr)
            {
                std::terminate();
            }
        }
        catch (...)
        {
            std::terminate();
        }
    };
    try
    {
        // Reusing an exact live location must be idempotent. OGRE appends a
        // second ResourceLocation for duplicate addResourceLocation() calls;
        // that would both alter caller-owned override groups and make exact
        // selected-archive cardinality fail closed.
        if (!selected_source_location_was_present)
        {
            rgm.addResourceLocation(
                selected_source_location, selected_source_type, rg_name);
        }

        Ogre::Archive* const selected_archive = find_selected_archive();
        std::size_t selected_source_location_count = 0U;
        if (selected_archive != nullptr && rgm.resourceGroupExists(rg_name))
        {
            const Ogre::ResourceGroupManager::LocationList& locations =
                rgm.getResourceLocationList(rg_name);
            for (const Ogre::ResourceGroupManager::ResourceLocation& location :
                 locations)
            {
                if (location.archive == nullptr ||
                    location.archive->getName() != selected_source_location)
                {
                    continue;
                }
                if (location.archive != selected_archive ||
                    location.archive->getType() != selected_source_type)
                {
                    OGRE_EXCEPT(
                        Ogre::Exception::ERR_INVALID_STATE,
                        "Resource-pack source location changed archive type or pointer during admission",
                        "ContentManager::AddResourcePack");
                }
                ++selected_source_location_count;
            }
        }
        const std::size_t expected_location_count =
            selected_source_location_count_before +
            (selected_source_location_was_present ? 0U : 1U);
        if (selected_archive == nullptr ||
            selected_source_location_count != expected_location_count)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Resource-pack source location cardinality changed during admission",
                "ContentManager::AddResourcePack");
        }

        if (add_builtin_texture_dependency)
        {
            std::size_t dependency_location_count = 0U;
            const Ogre::ResourceGroupManager::LocationList& locations =
                rgm.getResourceLocationList(rg_name);
            for (const Ogre::ResourceGroupManager::ResourceLocation& location :
                 locations)
            {
                if (location.archive != nullptr &&
                    location.archive->getName() ==
                        builtin_texture_dependency_location)
                {
                    if (location.archive->getType() !=
                        builtin_texture_dependency_type)
                    {
                        OGRE_EXCEPT(
                            Ogre::Exception::ERR_INVALID_STATE,
                            "Built-in texture dependency changed archive type",
                            "ContentManager::AddResourcePack");
                    }
                    ++dependency_location_count;
                }
            }
            if (dependency_location_count == 0U)
            {
                rgm.addResourceLocation(
                    builtin_texture_dependency_location,
                    builtin_texture_dependency_type, rg_name);
            }

            dependency_location_count = 0U;
            Ogre::Archive* dependency_archive = nullptr;
            const Ogre::ResourceGroupManager::LocationList&
                dependency_locations = rgm.getResourceLocationList(rg_name);
            for (const Ogre::ResourceGroupManager::ResourceLocation& location :
                 dependency_locations)
            {
                if (location.archive == nullptr ||
                    location.archive->getName() !=
                        builtin_texture_dependency_location)
                {
                    continue;
                }
                if (location.archive->getType() !=
                        builtin_texture_dependency_type ||
                    (dependency_archive != nullptr &&
                     dependency_archive != location.archive))
                {
                    OGRE_EXCEPT(
                        Ogre::Exception::ERR_INVALID_STATE,
                        "Built-in texture dependency is not one exact archive",
                        "ContentManager::AddResourcePack");
                }
                dependency_archive = location.archive;
                ++dependency_location_count;
            }
            if (dependency_archive == nullptr ||
                dependency_location_count != 1U)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Built-in texture dependency cardinality changed during admission",
                    "ContentManager::AddResourcePack");
            }
        }

#if OGRE_VERSION_MAJOR >= 14
#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
        if (m_force_next_resource_pack_registration_failure_for_testing)
        {
            m_force_next_resource_pack_registration_failure_for_testing = false;
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Injected resource-pack selected-source registration failure",
                "ContentManager::AddResourcePack");
        }
#endif
        // Built-in resource packs are ordinary observed sources, not
        // authenticated user-package sources. Register before any texture in
        // the group can load so the selected member bytes can be retained
        // without a GPU readback or a false authentication claim.
        this->RegisterPackageResourceLocation(
            rg_name, selected_source_location);
        package_location_registered = true;
        if (add_builtin_texture_dependency)
        {
            this->RegisterPackageResourceLocation(
                rg_name, builtin_texture_dependency_location);
        }
#endif

        if (use_default_group) // Only init the default RG
        {
#if OGRE_VERSION_MAJOR >= 14 && defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
            if (m_force_next_resource_pack_pre_scripting_failure_for_testing)
            {
                m_force_next_resource_pack_pre_scripting_failure_for_testing =
                    false;
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "Injected resource-pack failure before native scripting starts",
                    "ContentManager::AddResourcePack");
            }
#endif
            rgm.initialiseResourceGroup(rg_name);
        }
    }
    catch (...)
    {
        if (use_default_group)
        {
#if OGRE_VERSION_MAJOR >= 14
            if (package_location_registered)
            {
                // ResourceGroupManager can leave INITIALISING set when a
                // listener or script loader throws. Quarantine the exact
                // candidate before native resource removal callbacks begin.
                this->AbortAuthenticatedMaterialScriptGroup(rg_name);
            }
#endif
            try
            {
                if (rgm.resourceGroupExists(rg_name))
                {
                    rgm.destroyResourceGroup(rg_name);
                }
                if (rgm.resourceGroupExists(rg_name))
                {
                    std::terminate();
                }
            }
            catch (...)
            {
                std::terminate();
            }
#if OGRE_VERSION_MAJOR >= 14
            if (package_location_registered)
            {
                try
                {
                    // Teardown advances the process-global generation
                    // watermark, but removes the failed group's generation,
                    // receipts, stages, and package marker before retry.
                    this->UnregisterPackageResourceGroup(rg_name);
                }
                catch (...)
                {
                    std::terminate();
                }
            }
#endif
            rollback_new_builtin_texture_dependency_archive();
            rollback_new_selected_archive();
        }
        else
        {
            try
            {
                if (!resource_group_was_present)
                {
                    if (rgm.resourceGroupExists(rg_name))
                    {
                        rgm.destroyResourceGroup(rg_name);
                    }
                    if (rgm.resourceGroupExists(rg_name))
                    {
                        std::terminate();
                    }
                }
                else if (!selected_source_location_was_present &&
                         rgm.resourceLocationExists(
                             selected_source_location, rg_name))
                {
                    // OGRE's public removal API also unloads the process-wide
                    // Archive pointer. A location newly sharing a pre-call
                    // archive cannot be removed recoverably without dangling
                    // every other group that already owns that pointer.
                    if (selected_archive_was_live != nullptr)
                    {
                        std::terminate();
                    }
                    rgm.removeResourceLocation(
                        selected_source_location, rg_name);
                    if (rgm.resourceLocationExists(
                            selected_source_location, rg_name))
                    {
                        std::terminate();
                    }
                }
            }
            catch (...)
            {
                std::terminate();
            }
            rollback_new_selected_archive();
        }
        throw;
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
#if OGRE_VERSION_MAJOR >= 14
    this->RequireAuthenticatedResourceThread(
        "ContentManager::resourceLoading");
#endif
    std::string resolution_archive_sha256;
    bool resolution_is_candidate = false;
#if OGRE_VERSION_MAJOR >= 14
    const bool is_exact_texture_resource =
        resource != nullptr &&
        Ogre::TextureManager::getSingletonPtr() != nullptr &&
        resource->getCreator() == Ogre::TextureManager::getSingletonPtr() &&
        resource->getName() == name && resource->getGroup() == group;
#else
    const bool is_exact_texture_resource = false;
#endif
#if OGRE_VERSION_MAJOR >= 14
    if (is_exact_texture_resource)
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        this->EraseSelectedTextureSourceStageLocked(resource);
        const auto generation =
            m_legacy_material_group_generations.find(group);
        if (generation != m_legacy_material_group_generations.end())
        {
            const Render::ValidationResult removal =
                Render::RemoveOgre14SelectedTextureSourceResource(
                    group, generation->second,
                    reinterpret_cast<std::uintptr_t>(resource),
                    static_cast<std::uint64_t>(resource->getHandle()),
                    name, m_selected_texture_sources);
            if (!removal)
            {
                // No source load has completed yet, so reject this load
                // attempt without poisoning previously published ordinary
                // groups. Poison is reserved for a failed revocation after an
                // external OGRE mutation.
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Selected texture '{}' in group '{}' could not "
                        "revoke its prior source receipt before reload: {} "
                        "({})",
                        name, group, removal.detail, removal.field),
                    "ContentManager::resourceLoading");
            }
        }
    }

    // ScriptCompiler imports enter openResourceImpl with a null Resource
    // before archive selection. Stage that exact attempt now: a missing import
    // never reaches the pre-open seam, so an unconsumed marker must poison the
    // authenticated parse instead of silently publishing an incomplete
    // dependency closure.
    if (resource == nullptr)
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        AuthenticatedMaterialScriptGroupCandidate* candidate =
            m_authenticated_material_script_candidate.get();
        if (candidate != nullptr && candidate->parse_active &&
            candidate->parse_has_authenticated_root)
        {
            try
            {
                if (candidate->poisoned || candidate->group != group ||
                    group != m_scripting_resource_group || name.empty() ||
                    name.size() >
                        Render::kOgre14AuthenticatedMaterialScriptMaximumIdentifierBytes ||
                    group.size() >
                        Render::kOgre14AuthenticatedMaterialScriptMaximumIdentifierBytes ||
                    candidate->pending_import_open ||
                    candidate->source_by_compiler_file.find(
                        std::make_pair(candidate->parse_token, name)) !=
                        candidate->source_by_compiler_file.end() ||
                    candidate->source_open_attempt_count >=
                        MAX_AUTHENTICATED_MATERIAL_SCRIPT_SOURCE_OPEN_ATTEMPTS ||
                    candidate->source_open_attempt_count ==
                        (std::numeric_limits<std::uint64_t>::max)())
                {
                    candidate->poisoned = true;
                    return Ogre::DataStreamPtr();
                }
                Ogre::String pending_name = name;
                Ogre::String pending_group = group;
                candidate->pending_import_name.swap(pending_name);
                candidate->pending_import_group.swap(pending_group);
                candidate->pending_import_open = true;
                ++candidate->source_open_attempt_count;
            }
            catch (...)
            {
                candidate->poisoned = true;
                return Ogre::DataStreamPtr();
            }
        }
    }
#endif
    std::uint64_t group_generation = 0U;
    std::unordered_map<Ogre::String, std::string>
        authenticated_archives;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<Ogre::String, std::string>>
        authenticated_archive_groups;
    std::unordered_map<
        const Ogre::Archive*,
        AuthenticatedPackageArchiveBinding>
        authenticated_archive_bindings;
    std::unordered_map<
        Ogre::String,
        std::unordered_map<
            const Ogre::Archive*,
            AuthenticatedPackageArchiveBinding>>
        authenticated_archive_binding_groups;
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
            authenticated_archive_binding_groups =
                m_authenticated_package_archive_bindings_by_group;
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
            const auto authenticated_binding_group =
                m_authenticated_package_archive_bindings_by_group.find(group);
            if (authenticated_binding_group !=
                m_authenticated_package_archive_bindings_by_group.end())
            {
                authenticated_archive_bindings =
                    authenticated_binding_group->second;
            }
        }

        AuthenticatedMaterialScriptGroupCandidate* candidate =
            m_authenticated_material_script_candidate.get();
        if (is_exact_texture_resource && candidate != nullptr &&
            candidate->group == group &&
            candidate->generation == group_generation)
        {
            const auto authorized_texture =
                candidate->authorized_texture_fallbacks.find(name);
            if (authorized_texture !=
                candidate->authorized_texture_fallbacks.end())
            {
                if (candidate->poisoned)
                {
                    OGRE_EXCEPT(
                        Ogre::Exception::ERR_INVALID_STATE,
                        "Provisional texture authorization belongs to a "
                        "poisoned material-script group",
                        "ContentManager::resourceLoading");
                }
                resolution_archive_sha256 =
                    authorized_texture->second;
                resolution_is_candidate = true;
            }
        }
        if (is_exact_texture_resource &&
            resolution_archive_sha256.empty())
        {
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
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            const std::unordered_map<Ogre::String, std::string>*
                authorizations = nullptr;
            if (resolution_is_candidate && candidate != nullptr &&
                !candidate->poisoned && candidate->group == group &&
                candidate->generation == group_generation)
            {
                authorizations = &candidate->authorized_texture_fallbacks;
            }
            else if (!resolution_is_candidate)
            {
                const auto authorized_group =
                    m_authorized_texture_fallbacks_by_group.find(group);
                if (authorized_group !=
                    m_authorized_texture_fallbacks_by_group.end())
                {
                    authorizations = &authorized_group->second;
                }
            }
            if (authorizations == nullptr)
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
                authorizations->find(name);
            const auto current_generation =
                m_legacy_material_group_generations.find(group);
            if (authorized_texture ==
                    authorizations->end() ||
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
            if (resolution_is_candidate)
            {
                candidate->reported_texture_fallbacks.insert(name);
            }
            else
            {
                report_fallback =
                    m_reported_texture_fallbacks_by_group[group]
                        .insert(name)
                        .second;
            }
        }

        // An authenticated script already selected this generated name. Never
        // delegate it back to OGRE's resource lookup: a later same-named
        // untrusted location must not replace the authorized procedural bytes.
        Ogre::DataStreamPtr replacement;
#if OGRE_VERSION_MAJOR >= 14
        const bool is_texture_resource = is_exact_texture_resource;
        if (is_texture_resource)
        {
            if (resource->getName() != name ||
                resource->getGroup() != group)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Generated texture '{}' in group '{}' does not match "
                        "the exact loading resource identity",
                        name,
                        group),
                    "ContentManager::resourceLoading");
            }

            Render::Ogre14AuthenticatedTextureCaptureInput capture_input;
            capture_input.source_kind =
                Render::Ogre14AuthenticatedTextureSourceKind::
                    VERSIONED_GENERATED_FALLBACK;
            capture_input.effective_resource_group = group;
            capture_input.group_generation = group_generation;
            capture_input.archive_sha256 = resolution_archive_sha256;
            capture_input.exact_member_name = name;
            capture_input.generated_fallback_rule =
                Render::kOgre14GeneratedTextureFallbackRule;
            capture_input.generated_fallback_rule_version =
                Render::kOgre14GeneratedTextureFallbackRuleVersion;
            capture_input.binding.resource_pointer_token =
                reinterpret_cast<std::uintptr_t>(resource);
            capture_input.binding.resource_handle =
                static_cast<std::uint64_t>(resource->getHandle());
            capture_input.binding.resource_state_count =
                static_cast<std::uint64_t>(resource->getStateCount());
            capture_input.binding.exact_resource_name =
                resource->getName();

            Render::Ogre14AuthenticatedTextureReceipt receipt;
            const Render::ValidationResult capture =
                Render::BuildOgre14AuthenticatedTextureReceipt(
                    m_authenticated_texture_receipt_configuration,
                    capture_input,
                    dds.data(),
                    dds.size(),
                    receipt);
            if (!capture)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Generated texture '{}' in group '{}' could not "
                        "produce an authenticated byte receipt: {} ({})",
                        name,
                        group,
                        capture.detail,
                        capture.field),
                    "ContentManager::resourceLoading");
            }

            Ogre::DataStreamPtr replacement_stream(
                OGRE_NEW Ogre::MemoryDataStream(
                    name, receipt.source_size(), true, false));
            replacement_stream->write(
                receipt.source_bytes(), receipt.source_size());
            replacement_stream->seek(0U);
            replacement = replacement_stream;

            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            const auto current_generation =
                m_legacy_material_group_generations.find(group);
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            const std::unordered_map<Ogre::String, std::string>*
                authorizations = nullptr;
            if (resolution_is_candidate && candidate != nullptr &&
                !candidate->poisoned && candidate->group == group &&
                candidate->generation == group_generation)
            {
                authorizations = &candidate->authorized_texture_fallbacks;
            }
            else if (!resolution_is_candidate)
            {
                const auto current_authorized_group =
                    m_authorized_texture_fallbacks_by_group.find(group);
                if (current_authorized_group !=
                    m_authorized_texture_fallbacks_by_group.end())
                {
                    authorizations = &current_authorized_group->second;
                }
            }
            if (current_generation ==
                    m_legacy_material_group_generations.end() ||
                current_generation->second != group_generation ||
                authorizations == nullptr)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Generated texture '{}' in group '{}' changed "
                        "authorization before receipt commit",
                        name,
                        group),
                    "ContentManager::resourceLoading");
            }
            const auto current_authorization =
                authorizations->find(name);
            if (current_authorization ==
                    authorizations->end() ||
                current_authorization->second !=
                    resolution_archive_sha256 ||
                resource->getName() != name ||
                resource->getGroup() != group)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Generated texture '{}' in group '{}' lost its exact "
                        "resource or archive authorization",
                        name,
                        group),
                    "ContentManager::resourceLoading");
            }
            Render::Ogre14AuthenticatedTextureReceiptRegistry*
                receipt_registry = resolution_is_candidate
                    ? &candidate->texture_receipts
                    : &m_authenticated_texture_receipts;
            Render::Ogre14AuthenticatedTextureReceiptRegistry
                receipt_registry_candidate = *receipt_registry;
            const Render::ValidationResult commit =
                Render::CommitOgre14AuthenticatedTextureReceipt(
                    receipt, receipt_registry_candidate);
            if (!commit)
            {
                if (resolution_is_candidate)
                {
                    candidate->poisoned = true;
                }
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Generated texture '{}' in group '{}' could not "
                        "commit its authenticated receipt: {} ({})",
                        name,
                        group,
                        commit.detail,
                        commit.field),
                    "ContentManager::resourceLoading");
            }
            if (resolution_is_candidate)
            {
                try
                {
                    const Ogre::ResourcePtr retained_resource =
                        Ogre::TextureManager::getSingleton().getByHandle(
                            resource->getHandle());
                    Ogre::TexturePtr retained_texture =
                        Ogre::static_pointer_cast<Ogre::Texture>(
                            retained_resource);
                    if (!retained_texture ||
                        retained_texture.get() != resource)
                    {
                        candidate->poisoned = true;
                        OGRE_EXCEPT(
                            Ogre::Exception::ERR_INVALID_STATE,
                            "Generated texture could not retain its exact "
                            "provisional native resource",
                            "ContentManager::resourceLoading");
                    }
                    candidate->textures.push_back(
                        {receipt, std::move(retained_texture)});
                }
                catch (...)
                {
                    candidate->poisoned = true;
                    throw;
                }
            }
            *receipt_registry = std::move(receipt_registry_candidate);
        }
        else
#endif
        {
            Ogre::DataStreamPtr replacement_stream(
                OGRE_NEW Ogre::MemoryDataStream(
                    name, dds.size(), true, false));
            replacement_stream->write(dds.data(), dds.size());
            replacement_stream->seek(0U);
            replacement = replacement_stream;
        }
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
        return replacement;
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
          authenticated_archive_bindings.empty() ||
          group_generation == 0U)) ||
        (uses_autodetect_group &&
         (authenticated_archive_groups.empty() ||
          authenticated_archive_binding_groups.empty())))
    {
        return Ogre::DataStreamPtr();
    }

    Ogre::String effective_group = group;
    Ogre::DataStreamPtr authenticated_stream;
    Ogre::String selected_archive_name;
    Ogre::String selected_archive_identity;
    Ogre::String selected_archive_type;
    std::uintptr_t selected_archive_pointer_token = 0U;
    const Ogre::Archive* selected_archive_instance = nullptr;
    Ogre::String selected_member_name;
    AuthenticatedPackageArchiveBinding::MemberBinding
        selected_member_binding;
    std::shared_ptr<const AuthenticatedPackageArchiveBinding::MemberManifest>
        selected_member_manifest_owner;
    std::string selected_archive_sha256;
    bool change_resource_group = false;
    const bool is_texture_resource =
        Ogre::TextureManager::getSingletonPtr() != nullptr &&
        resource->getCreator() ==
            Ogre::TextureManager::getSingletonPtr();
    const bool is_mesh_resource =
        Ogre::MeshManager::getSingletonPtr() != nullptr &&
        resource->getCreator() == Ogre::MeshManager::getSingletonPtr();
    {
        std::lock_guard<std::mutex> archive_lock(
            m_legacy_material_archive_io_mutex);
        Ogre::ResourceGroupManager& live_resource_manager =
            Ogre::ResourceGroupManager::getSingleton();
        Ogre::ArchiveManager& live_archive_manager =
            Ogre::ArchiveManager::getSingleton();
        if (uses_autodetect_group)
        {
            bool found_effective_group = false;
            const Ogre::StringVector candidate_groups =
                live_resource_manager.getResourceGroups();
            if (candidate_groups.size() >
                Render::kOgre14AuthenticatedTextureMaximumGroupRecords)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    "AUTODETECT resource selection exceeds its bounded group "
                    "scan",
                    "ContentManager::resourceLoading");
            }
            for (const Ogre::String& candidate_group : candidate_groups)
            {
                if (!live_resource_manager.resourceGroupExists(
                        candidate_group))
                {
                    continue;
                }
                const Ogre::ResourceGroupManager::LocationList&
                    candidate_locations =
                        live_resource_manager.getResourceLocationList(
                            candidate_group);
                if (candidate_locations.size() >
                    Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates)
                {
                    OGRE_EXCEPT(
                        Ogre::Exception::ERR_INVALID_STATE,
                        "AUTODETECT resource selection exceeds its bounded "
                        "location scan",
                        "ContentManager::resourceLoading");
                }
                const auto authenticated_binding_group =
                    authenticated_archive_binding_groups.find(
                        candidate_group);
                for (const Ogre::ResourceGroupManager::ResourceLocation&
                         location : candidate_locations)
                {
                    if (location.archive == nullptr)
                    {
                        continue;
                    }
                    const AuthenticatedPackageArchiveBinding* binding =
                        nullptr;
                    if (authenticated_binding_group !=
                        authenticated_archive_binding_groups.end())
                    {
                        const auto found_binding =
                            authenticated_binding_group->second.find(
                                location.archive);
                        if (found_binding !=
                            authenticated_binding_group->second.end())
                        {
                            binding = &found_binding->second;
                        }
                    }
                    if (binding != nullptr)
                    {
                        const Render::
                            Ogre14AuthenticatedArchiveAuthorityProof proof =
                                Render::
                                    CaptureOgre14AuthenticatedArchiveAuthorityProof(
                                        live_archive_manager,
                                        live_resource_manager,
                                        candidate_group,
                                        binding->selected_archive_name);
                        if (!proof.AuthenticatesExclusive(location.archive))
                        {
                            OGRE_EXCEPT(
                                Ogre::Exception::ERR_INVALID_STATE,
                                "Authenticated AUTODETECT location lost its "
                                "live manager authority",
                                "ContentManager::resourceLoading");
                        }
                    }
                    else
                    {
                        Ogre::String live_archive_name;
                        if (!ResolveLiveArchiveManagerPointer(
                                live_archive_manager,
                                location.archive,
                                live_archive_name))
                        {
                            OGRE_EXCEPT(
                                Ogre::Exception::ERR_INVALID_STATE,
                                "AUTODETECT location contains a non-live "
                                "archive pointer",
                                "ContentManager::resourceLoading");
                        }
                    }
                    if (location.archive->exists(name))
                    {
                        effective_group = candidate_group;
                        found_effective_group = true;
                        break;
                    }
                }
                if (found_effective_group)
                {
                    break;
                }
            }
            if (!found_effective_group)
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
            const auto authenticated_binding_group =
                authenticated_archive_binding_groups.find(effective_group);
            if (authenticated_binding_group ==
                authenticated_archive_binding_groups.end())
            {
                return Ogre::DataStreamPtr();
            }
            authenticated_archive_bindings =
                authenticated_binding_group->second;
            group_generation =
                effective_generation->second;
            // Resource::load() resolves OgreAutodetect after prepareImpl().
            // Because returning a stream here bypasses ResourceGroupManager's
            // normal selection branch, mirror that eventual ownership
            // transition now for both private and global-pool groups.
            change_resource_group = true;
        }

        // Texture selection is derived from one bounded archive index pass per
        // location. In OGRE's non-strict Zip mode exists() and FileInfo::filename
        // both discard information needed to distinguish full paths and case
        // collisions, so neither is an authentication decision. Meshes retain
        // the established ResourceGroupManager-compatible selection below.
        const Ogre::ResourceGroupManager::LocationList& locations =
            live_resource_manager.getResourceLocationList(effective_group);
        const auto prove_location_before_dereference =
            [&](const Ogre::ResourceGroupManager::ResourceLocation& location)
            {
                if (location.archive == nullptr)
                {
                    return;
                }
                const auto binding =
                    authenticated_archive_bindings.find(location.archive);
                if (binding != authenticated_archive_bindings.end())
                {
                    const Render::Ogre14AuthenticatedArchiveAuthorityProof
                        proof = Render::
                            CaptureOgre14AuthenticatedArchiveAuthorityProof(
                                live_archive_manager,
                                live_resource_manager,
                                effective_group,
                                binding->second.selected_archive_name);
                    if (!proof.AuthenticatesExclusive(location.archive))
                    {
                        OGRE_EXCEPT(
                            Ogre::Exception::ERR_INVALID_STATE,
                            "Authenticated resource location lost its live "
                            "ArchiveManager authority before selection",
                            "ContentManager::resourceLoading");
                    }
                    return;
                }
                Ogre::String live_archive_name;
                if (!ResolveLiveArchiveManagerPointer(
                        live_archive_manager,
                        location.archive,
                        live_archive_name))
                {
                    OGRE_EXCEPT(
                        Ogre::Exception::ERR_INVALID_STATE,
                        "Resource location contains an archive pointer that "
                        "is not live in ArchiveManager",
                        "ContentManager::resourceLoading");
                }
            };
        const Ogre::Archive* selected_archive = nullptr;
        if (is_texture_resource)
        {
            Ogre::String requested_basename;
            Ogre::String requested_path;
            Ogre::StringUtil::splitFilename(
                name, requested_basename, requested_path);
            Ogre::String folded_full_name = name;
            Ogre::String folded_basename = requested_basename;
            Ogre::StringUtil::toLowerCase(folded_full_name);
            Ogre::StringUtil::toLowerCase(folded_basename);

            for (const Ogre::ResourceGroupManager::ResourceLocation& location :
                 locations)
            {
                if (location.archive == nullptr)
                {
                    continue;
                }
                prove_location_before_dereference(location);
                const Ogre::FileInfoListPtr selected_index =
                    location.archive->findFileInfo(
                        "*", location.recursive, false);
                if (!selected_index)
                {
                    OGRE_EXCEPT(
                        Ogre::Exception::ERR_INVALID_STATE,
                        fmt::format(
                            "Archive '{}' did not expose an index while "
                            "selecting authenticated texture '{}'",
                            location.archive->getName(),
                            name),
                        "ContentManager::resourceLoading");
                }
                if (selected_index->size() >
                    Render::kOgre14AuthenticatedTextureMaximumArchiveMemberCandidates)
                {
                    OGRE_EXCEPT(
                        Ogre::Exception::ERR_INVALID_STATE,
                        fmt::format(
                            "Archive '{}' exceeds the authenticated texture "
                            "member-count cap",
                            location.archive->getName()),
                        "ContentManager::resourceLoading");
                }

                const bool archive_case_sensitive =
                    location.archive->isCaseSensitive();
                const bool allow_zip_basename_fallback =
                    !archive_case_sensitive &&
                    (location.archive->getType() == "Zip" ||
                     location.archive->getType() == "EmbeddedZip");
                std::vector<
                    Render::Ogre14AuthenticatedTextureArchiveMemberObservation>
                    member_observations;
                member_observations.reserve(selected_index->size());
                std::uint64_t observed_identity_bytes = 0U;
                for (const Ogre::FileInfo& indexed_file : *selected_index)
                {
                    if (indexed_file.path.size() >
                            Render::kOgre14AuthenticatedTextureMaximumIdentifierBytes ||
                        indexed_file.basename.size() >
                            Render::kOgre14AuthenticatedTextureMaximumIdentifierBytes -
                                indexed_file.path.size())
                    {
                        OGRE_EXCEPT(
                            Ogre::Exception::ERR_INVALID_STATE,
                            fmt::format(
                                "Archive '{}' contains an overlong texture "
                                "member identity",
                                location.archive->getName()),
                            "ContentManager::resourceLoading");
                    }
                    const Ogre::String exact_member =
                        indexed_file.path + indexed_file.basename;
                    if (exact_member.empty())
                    {
                        continue;
                    }
                    if (observed_identity_bytes >
                        Render::kOgre14AuthenticatedTextureMaximumArchiveMemberIdentityBytes -
                            static_cast<std::uint64_t>(exact_member.size()))
                    {
                        OGRE_EXCEPT(
                            Ogre::Exception::ERR_INVALID_STATE,
                            fmt::format(
                                "Archive '{}' exceeds the authenticated "
                                "texture member-identity byte cap",
                                location.archive->getName()),
                            "ContentManager::resourceLoading");
                    }
                    observed_identity_bytes +=
                        static_cast<std::uint64_t>(exact_member.size());
                    Ogre::String folded_member = exact_member;
                    Ogre::StringUtil::toLowerCase(folded_member);
                    Ogre::String indexed_basename = indexed_file.basename;
                    Ogre::StringUtil::toLowerCase(indexed_basename);
                    Render::Ogre14AuthenticatedTextureArchiveMemberObservation
                        observation;
                    observation.exact_member_name = exact_member;
                    observation.exact_full_match = exact_member == name;
                    observation.folded_full_match =
                        folded_member == folded_full_name;
                    observation.folded_basename_match =
                        allow_zip_basename_fallback &&
                        indexed_basename == folded_basename;
                    member_observations.push_back(std::move(observation));
                }

                Ogre::String exact_member;
                const Render::ValidationResult member_selection =
                    Render::SelectOgre14AuthenticatedTextureArchiveMember(
                        archive_case_sensitive,
                        allow_zip_basename_fallback,
                        member_observations.data(),
                        member_observations.size(),
                        exact_member);
                if (member_selection)
                {
                    selected_archive = location.archive;
                    selected_member_name = std::move(exact_member);
                    break;
                }
                if (member_selection.code !=
                    Render::ValidationCode::MISSING_REFERENCE)
                {
                    OGRE_EXCEPT(
                        Ogre::Exception::ERR_DUPLICATE_ITEM,
                        fmt::format(
                            "Archive '{}' could not resolve one exact "
                            "case-sensitive member for texture resource "
                            "'{}': {} ({})",
                            location.archive->getName(),
                            name,
                            member_selection.detail,
                            member_selection.field),
                        "ContentManager::resourceLoading");
                }
            }
        }
        else
        {
            for (const Ogre::ResourceGroupManager::ResourceLocation& location :
                 locations)
            {
                if (location.archive != nullptr)
                {
                    prove_location_before_dereference(location);
                    if (location.archive->exists(name))
                    {
                        selected_archive = location.archive;
                        selected_member_name = name;
                        break;
                    }
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
                    prove_location_before_dereference(location);
                    const Ogre::FileInfoListPtr indexed_files =
                        location.archive->findFileInfo(
                            "*", location.recursive, false);
                    if (!indexed_files)
                    {
                        continue;
                    }
                    Ogre::String exact_folded_member;
                    for (const Ogre::FileInfo& indexed_file : *indexed_files)
                    {
                        Ogre::String folded_candidate =
                            indexed_file.filename;
                        Ogre::StringUtil::toLowerCase(folded_candidate);
                        if (folded_candidate == folded_name &&
                            exact_folded_member.empty())
                        {
                            exact_folded_member = indexed_file.filename;
                        }
                    }
                    if (!exact_folded_member.empty())
                    {
                        selected_archive = location.archive;
                        selected_member_name = exact_folded_member;
                        break;
                    }
                }
            }
#endif
        }
        if (selected_archive == nullptr)
        {
            return Ogre::DataStreamPtr();
        }

        const auto authenticated_binding =
            authenticated_archive_bindings.find(selected_archive);
        if (authenticated_binding ==
            authenticated_archive_bindings.end())
        {
            // Preserve OGRE's first-location-wins behavior without allowing a
            // same-named untrusted archive to inherit later authority.
            return Ogre::DataStreamPtr();
        }
        const AuthenticatedPackageArchiveBinding& selected_binding =
            authenticated_binding->second;
        const Render::Ogre14AuthenticatedArchiveAuthorityProof
            selected_archive_authority =
                Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                    live_archive_manager,
                    live_resource_manager,
                    effective_group,
                    selected_binding.selected_archive_name);
        if (!selected_archive_authority.AuthenticatesExclusive(
                selected_archive))
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Selected authenticated archive lost manager/location "
                "authority before metadata access",
                "ContentManager::resourceLoading");
        }
        selected_archive_name = selected_archive->getName();
        selected_archive_type = selected_archive->getType();
        selected_archive_identity =
            selected_binding.source_archive_identity;
        selected_archive_pointer_token =
            selected_binding.archive_pointer_token;
        selected_archive_instance = selected_archive;
        if (selected_binding.group_generation != group_generation ||
            selected_binding.selected_archive_name != selected_archive_name ||
            selected_binding.selected_archive_type != selected_archive_type ||
            selected_archive_pointer_token !=
                reinterpret_cast<std::uintptr_t>(selected_archive) ||
            selected_binding.archive_sha256 !=
                selected_binding.immutable_archive.archive_sha256() ||
            selected_archive_identity !=
                selected_binding.immutable_archive.source_archive_identity() ||
            !selected_binding.immutable_archive.initialized())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                fmt::format(
                    "Selected archive '{}' for resource '{}' in group '{}' "
                    "lost its immutable pointer-exact registration",
                    selected_archive_name,
                    name,
                    effective_group),
                "ContentManager::resourceLoading");
        }
        if (!selected_binding.immutable_member_manifest)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Selected authenticated archive has no immutable member "
                "manifest",
                "ContentManager::resourceLoading");
        }
        const auto selected_manifest_member =
            selected_binding.immutable_member_manifest->find(
                selected_member_name);
        if (selected_manifest_member ==
                selected_binding.immutable_member_manifest->end() ||
            !Render::IsLowercaseOgre14Sha256(
                selected_manifest_member->second.sha256))
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Selected resource member is not present in the immutable "
                "archive manifest",
                "ContentManager::resourceLoading");
        }
        selected_member_binding = selected_manifest_member->second;
        selected_member_manifest_owner =
            selected_binding.immutable_member_manifest;
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
        if (!Render::IsLowercaseOgre14Sha256(
                selected_archive_sha256) ||
            selected_archive_sha256 != selected_binding.archive_sha256 ||
            selected_archive_identity.empty() ||
            selected_archive_pointer_token == 0U ||
            selected_member_name.empty())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                fmt::format(
                    "Selected archive '{}' for resource '{}' in group '{}' "
                    "does not carry one authenticated identity/member",
                    selected_archive_name,
                    name,
                    effective_group),
                "ContentManager::resourceLoading");
        }
        // Existing authenticated mesh loading keeps its compatible selection
        // rules. Both meshes and source textures open the one exact selected
        // member; no AUTODETECT/name reopen can occur after capture.
        authenticated_stream = selected_archive->open(selected_member_name);
        if (!authenticated_stream ||
            authenticated_stream->size() !=
                selected_member_binding.uncompressed_size)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Selected resource stream size does not match the immutable "
                "archive manifest",
                "ContentManager::resourceLoading");
        }
    }

    if (!authenticated_stream)
    {
        return Ogre::DataStreamPtr();
    }
    if (change_resource_group)
    {
        resource->changeGroupOwnership(effective_group);
    }
    const Ogre::String expected_resource_group =
        change_resource_group ? effective_group : group;
    const auto revalidate_selected_archive_authority = [&]()
    {
        std::lock_guard<std::mutex> archive_lock(
            m_legacy_material_archive_io_mutex);
        Ogre::ArchiveManager* archive_manager =
            Ogre::ArchiveManager::getSingletonPtr();
        Ogre::ResourceGroupManager* resource_manager =
            Ogre::ResourceGroupManager::getSingletonPtr();
        if (archive_manager == nullptr || resource_manager == nullptr)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Archive managers disappeared before authenticated resource "
                "publication",
                "ContentManager::resourceLoading");
        }
        const Render::Ogre14AuthenticatedArchiveAuthorityProof proof =
            Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                *archive_manager,
                *resource_manager,
                effective_group,
                selected_archive_name);
        if (!proof.AuthenticatesExclusive(selected_archive_instance) ||
            proof.manager_archive->getName() != selected_archive_name ||
            proof.manager_archive->getType() != selected_archive_type ||
            !proof.manager_archive->isReadOnly())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Selected archive authority changed before authenticated "
                "resource publication",
                "ContentManager::resourceLoading");
        }
    };

    if (is_texture_resource)
    {
        if (resource->getName() != name ||
            resource->getGroup() != expected_resource_group)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                fmt::format(
                    "Authenticated texture '{}' in group '{}' does not "
                    "match the exact selected loading resource",
                    name,
                    effective_group),
                "ContentManager::resourceLoading");
        }
        const std::size_t source_size = authenticated_stream->size();
        if (source_size == 0U ||
            static_cast<std::uint64_t>(source_size) >
                m_authenticated_texture_receipt_configuration
                    .maximum_source_bytes)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                fmt::format(
                    "Authenticated texture '{}' in group '{}' has invalid "
                    "source byte count {}",
                    name,
                    effective_group,
                    source_size),
                "ContentManager::resourceLoading");
        }
        std::vector<std::uint8_t> source_bytes(source_size);
        authenticated_stream->seek(0U);
        const std::size_t observed_size = authenticated_stream->read(
            source_bytes.data(), source_bytes.size());
        if (observed_size != source_bytes.size())
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                fmt::format(
                    "Authenticated texture '{}' in group '{}' returned {} "
                    "of {} selected source bytes",
                    name,
                    effective_group,
                    observed_size,
                    source_bytes.size()),
                "ContentManager::resourceLoading");
        }
        const std::string selected_source_sha256 = Sha256Bytes(
            source_bytes.data(), source_bytes.size());
        if (selected_source_sha256 != selected_member_binding.sha256)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated texture bytes do not match the immutable "
                "archive member manifest",
                "ContentManager::resourceLoading");
        }
        revalidate_selected_archive_authority();

        Render::Ogre14AuthenticatedTextureCaptureInput capture_input;
        capture_input.effective_resource_group = effective_group;
        capture_input.group_generation = group_generation;
        capture_input.archive_identity = selected_archive_identity;
        capture_input.archive_name = selected_archive_name;
        capture_input.archive_type = selected_archive_type;
        capture_input.archive_sha256 = selected_archive_sha256;
        capture_input.archive_pointer_token =
            selected_archive_pointer_token;
        capture_input.exact_member_name = selected_member_name;
        capture_input.binding.resource_pointer_token =
            reinterpret_cast<std::uintptr_t>(resource);
        capture_input.binding.resource_handle =
            static_cast<std::uint64_t>(resource->getHandle());
        capture_input.binding.resource_state_count =
            static_cast<std::uint64_t>(resource->getStateCount());
        capture_input.binding.exact_resource_name = resource->getName();

        Render::Ogre14AuthenticatedTextureReceipt receipt;
        const Render::ValidationResult capture =
            Render::BuildOgre14AuthenticatedTextureReceipt(
                m_authenticated_texture_receipt_configuration,
                capture_input,
                source_bytes.data(),
                source_bytes.size(),
                receipt);
        if (!capture)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                fmt::format(
                    "Authenticated texture '{}' in group '{}' could not "
                    "produce a source-byte receipt: {} ({})",
                    name,
                    effective_group,
                    capture.detail,
                    capture.field),
                "ContentManager::resourceLoading");
        }

        Ogre::DataStreamPtr replacement(
            OGRE_NEW Ogre::MemoryDataStream(
                name, receipt.source_size(), true, false));
        replacement->write(
            receipt.source_bytes(), receipt.source_size());
        replacement->seek(0U);

        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            const auto current_generation =
                m_legacy_material_group_generations.find(effective_group);
            const auto current_authenticated_group =
                m_authenticated_package_archives_by_group.find(
                    effective_group);
            const auto current_authenticated_binding_group =
                m_authenticated_package_archive_bindings_by_group.find(
                    effective_group);
            if (current_generation ==
                    m_legacy_material_group_generations.end() ||
                current_generation->second != group_generation ||
                current_authenticated_group ==
                    m_authenticated_package_archives_by_group.end() ||
                current_authenticated_binding_group ==
                    m_authenticated_package_archive_bindings_by_group.end())
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Authenticated texture '{}' in group '{}' changed "
                        "generation/archive authority before receipt commit",
                        name,
                        effective_group),
                    "ContentManager::resourceLoading");
            }
            const auto current_archive =
                current_authenticated_group->second.find(
                    selected_archive_name);
            const auto current_archive_binding =
                current_authenticated_binding_group->second.find(
                    selected_archive_instance);
            if (current_archive ==
                    current_authenticated_group->second.end() ||
                current_archive->second != selected_archive_sha256 ||
                current_archive_binding ==
                    current_authenticated_binding_group->second.end() ||
                current_archive_binding->second.group_generation !=
                    group_generation ||
                current_archive_binding->second.source_archive_identity !=
                    selected_archive_identity ||
                current_archive_binding->second.selected_archive_name !=
                    selected_archive_name ||
                current_archive_binding->second.selected_archive_type !=
                    selected_archive_type ||
                current_archive_binding->second.archive_sha256 !=
                    selected_archive_sha256 ||
                current_archive_binding->second.archive_pointer_token !=
                    selected_archive_pointer_token ||
                current_archive_binding->second.immutable_member_manifest !=
                    selected_member_manifest_owner ||
                current_archive_binding->second.retained_manifest_file_count !=
                    selected_member_manifest_owner->size() ||
                current_archive_binding->second.retained_manifest_member_count <
                    current_archive_binding->second.retained_manifest_file_count ||
                current_archive_binding->second.immutable_member_manifest->
                        find(selected_member_name) ==
                    current_archive_binding->second.immutable_member_manifest->
                        end() ||
                current_archive_binding->second.immutable_member_manifest->
                        at(selected_member_name).sha256 !=
                    selected_member_binding.sha256 ||
                selected_archive_pointer_token !=
                    reinterpret_cast<std::uintptr_t>(
                        selected_archive_instance) ||
                resource->getName() != name ||
                resource->getGroup() != expected_resource_group)
            {
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Authenticated texture '{}' in group '{}' lost its "
                        "exact resource or selected archive identity",
                        name,
                        effective_group),
                    "ContentManager::resourceLoading");
            }
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            const bool publication_is_candidate =
                candidate != nullptr && !candidate->poisoned &&
                candidate->group == effective_group &&
                candidate->generation == group_generation;
            Render::Ogre14AuthenticatedTextureReceiptRegistry*
                receipt_registry = publication_is_candidate
                    ? &candidate->texture_receipts
                    : &m_authenticated_texture_receipts;
            Render::Ogre14AuthenticatedTextureReceiptRegistry
                receipt_registry_candidate = *receipt_registry;
            const Render::ValidationResult commit =
                Render::CommitOgre14AuthenticatedTextureReceipt(
                    receipt, receipt_registry_candidate);
            if (!commit)
            {
                if (publication_is_candidate)
                {
                    candidate->poisoned = true;
                }
                OGRE_EXCEPT(
                    Ogre::Exception::ERR_INVALID_STATE,
                    fmt::format(
                        "Authenticated texture '{}' in group '{}' could not "
                        "commit its source-byte receipt: {} ({})",
                        name,
                        effective_group,
                        commit.detail,
                        commit.field),
                    "ContentManager::resourceLoading");
            }
            if (publication_is_candidate)
            {
                try
                {
                    const Ogre::ResourcePtr retained_resource =
                        Ogre::TextureManager::getSingleton().getByHandle(
                            resource->getHandle());
                    Ogre::TexturePtr retained_texture =
                        Ogre::static_pointer_cast<Ogre::Texture>(
                            retained_resource);
                    if (!retained_texture ||
                        retained_texture.get() != resource)
                    {
                        candidate->poisoned = true;
                        OGRE_EXCEPT(
                            Ogre::Exception::ERR_INVALID_STATE,
                            "Authenticated texture could not retain its "
                            "exact provisional native resource",
                            "ContentManager::resourceLoading");
                    }
                    candidate->textures.push_back(
                        {receipt, std::move(retained_texture)});
                }
                catch (...)
                {
                    candidate->poisoned = true;
                    throw;
                }
            }
            *receipt_registry = std::move(receipt_registry_candidate);
        }
        // OGRE parses only this replacement over the receipt-owned bytes. The
        // archive stream is never handed onward or reopened by name.
        return replacement;
    }

    static const std::size_t MAX_AUTHENTICATED_MESH_BYTES =
        512U * 1024U * 1024U;
    if (is_mesh_resource)
    {
        const std::size_t mesh_source_size = authenticated_stream->size();
        if (mesh_source_size == 0U ||
            mesh_source_size > MAX_AUTHENTICATED_MESH_BYTES)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated mesh source exceeds its exact byte cap",
                "ContentManager::resourceLoading");
        }
        std::vector<std::uint8_t> mesh_source_bytes(mesh_source_size);
        authenticated_stream->seek(0U);
        const std::size_t observed_mesh_bytes = authenticated_stream->read(
            mesh_source_bytes.data(), mesh_source_bytes.size());
        if (observed_mesh_bytes != mesh_source_bytes.size() ||
            Sha256Bytes(
                mesh_source_bytes.data(), mesh_source_bytes.size()) !=
                selected_member_binding.sha256)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated mesh bytes do not match the immutable archive "
                "member manifest",
                "ContentManager::resourceLoading");
        }
        revalidate_selected_archive_authority();

        Ogre::DataStreamPtr replacement(
            OGRE_NEW Ogre::MemoryDataStream(
                name, mesh_source_bytes.size(), true, false));
        replacement->write(
            mesh_source_bytes.data(), mesh_source_bytes.size());
        replacement->seek(0U);

        Ogre::Mesh* mesh = static_cast<Ogre::Mesh*>(resource);
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const auto current_generation =
            m_legacy_material_group_generations.find(effective_group);
        const auto current_authenticated_group =
            m_authenticated_package_archives_by_group.find(effective_group);
        const auto current_binding_group =
            m_authenticated_package_archive_bindings_by_group.find(
                effective_group);
        const auto current_archive = current_authenticated_group !=
                m_authenticated_package_archives_by_group.end()
            ? current_authenticated_group->second.find(selected_archive_name)
            : std::unordered_map<Ogre::String, std::string>::const_iterator();
        const auto current_binding = current_binding_group !=
                m_authenticated_package_archive_bindings_by_group.end()
            ? current_binding_group->second.find(selected_archive_instance)
            : std::unordered_map<
                  const Ogre::Archive*,
                  AuthenticatedPackageArchiveBinding>::const_iterator();
        if (current_generation ==
                m_legacy_material_group_generations.end() ||
            current_generation->second != group_generation ||
            current_authenticated_group ==
                m_authenticated_package_archives_by_group.end() ||
            current_archive == current_authenticated_group->second.end() ||
            current_archive->second != selected_archive_sha256 ||
            current_binding_group ==
                m_authenticated_package_archive_bindings_by_group.end() ||
            current_binding == current_binding_group->second.end() ||
            current_binding->second.group_generation != group_generation ||
            current_binding->second.archive_pointer_token !=
                selected_archive_pointer_token ||
            current_binding->second.immutable_member_manifest !=
                selected_member_manifest_owner ||
            mesh->getName() != name ||
            mesh->getGroup() != expected_resource_group)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                "Authenticated mesh lost its exact archive/resource binding "
                "before publication",
                "ContentManager::resourceLoading");
        }
        auto mesh_bindings_candidate = m_authenticated_mesh_bindings;
        mesh_bindings_candidate[resource] = {
            effective_group,
            expected_resource_group,
            name,
            mesh->getHandle(),
            mesh->getStateCount(),
            group_generation,
            selected_archive_sha256};
        static_assert(noexcept(m_authenticated_mesh_bindings.swap(
            mesh_bindings_candidate)));
        m_authenticated_mesh_bindings.swap(mesh_bindings_candidate);
        return replacement;
    }
    return authenticated_stream;
#else
    (void)resource;
    return Ogre::DataStreamPtr();
#endif
}

#if OGRE_VERSION_MAJOR >= 14
Ogre::DataStreamPtr ContentManager::OpenSelectedTextureSourceStream(
    const Ogre::String& name,
    const Ogre::String& group,
    Ogre::Resource* resource,
    const Ogre::Archive* selected_archive,
    const Ogre::FileInfo* exact_file_info,
    bool& handled)
{
    handled = false;
    Ogre::TextureManager* texture_manager =
        Ogre::TextureManager::getSingletonPtr();
    // A null exact_file_info is not fatal here. OGRE's pre-open seam resolves a
    // record only when the archive spells the member exactly as the resource
    // was declared, so a declaration differing only in case arrives with no
    // record at all. That case is admitted below through the reviewed
    // member-selection policy, which rebuilds the record from the selected
    // archive's own index and refuses ambiguity and absence.
    if (resource == nullptr || texture_manager == nullptr ||
        resource->getCreator() != texture_manager ||
        texture_manager->getResourceType() != "Texture" ||
        resource->getName() != name || resource->getGroup() != group ||
        selected_archive == nullptr)
    {
        return Ogre::DataStreamPtr();
    }

    std::uint64_t group_generation = 0U;
    std::uint64_t state_count_before_load = 0U;
    std::unordered_set<Ogre::String> package_archive_names;
    const std::uintptr_t resource_pointer_token =
        reinterpret_cast<std::uintptr_t>(resource);
    const std::uint64_t resource_handle =
        static_cast<std::uint64_t>(resource->getHandle());
    const std::uintptr_t selected_archive_pointer_token =
        reinterpret_cast<std::uintptr_t>(selected_archive);

    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        const auto package_group = m_package_archives_by_group.find(group);
        const auto generation =
            m_legacy_material_group_generations.find(group);
        const bool authenticated_names_absent =
            m_authenticated_package_archives_by_group.find(group) ==
            m_authenticated_package_archives_by_group.end();
        const bool authenticated_bindings_absent =
            m_authenticated_package_archive_bindings_by_group.find(group) ==
            m_authenticated_package_archive_bindings_by_group.end();
        if (package_group == m_package_archives_by_group.end() ||
            package_group->second.empty() ||
            generation == m_legacy_material_group_generations.end() ||
            !authenticated_names_absent ||
            !authenticated_bindings_absent ||
            !m_authenticated_texture_receipts.initialized() ||
            !m_selected_texture_sources.initialized() ||
            resource->isLoaded())
        {
            return Ogre::DataStreamPtr();
        }
        const Ogre::ResourcePtr by_handle =
            texture_manager->getByHandle(resource->getHandle());
        const Ogre::ResourcePtr by_name =
            texture_manager->getResourceByName(name, group);
        if (!by_handle || !by_name || by_handle.get() != resource ||
            by_name.get() != resource)
        {
            return Ogre::DataStreamPtr();
        }
        const std::size_t native_state_count = resource->getStateCount();
        state_count_before_load =
            static_cast<std::uint64_t>(native_state_count);
        if (static_cast<std::size_t>(state_count_before_load) !=
                native_state_count ||
            state_count_before_load ==
                (std::numeric_limits<std::uint64_t>::max)())
        {
            return Ogre::DataStreamPtr();
        }
        group_generation = generation->second;
        package_archive_names = package_group->second;

        // Defensive duplicate callback closure. resourceLoading() normally
        // performs this exact revocation first; repeating it here prevents a
        // direct pre-open callback from retaining stale authority.
        this->EraseSelectedTextureSourceStageLocked(resource);
        const Render::ValidationResult removal =
            Render::RemoveOgre14SelectedTextureSourceResource(
                group, group_generation, resource_pointer_token,
                resource_handle, name, m_selected_texture_sources);
        if (!removal)
        {
            OGRE_EXCEPT(
                Ogre::Exception::ERR_INVALID_STATE,
                fmt::format(
                    "Selected texture '{}' in group '{}' could not revoke "
                    "its prior receipt before exact archive selection: {} "
                    "({})",
                    name, group, removal.detail, removal.field),
                "ContentManager::OpenSelectedTextureSourceStream");
        }

        Render::Ogre14AuthenticatedTextureReceipt authenticated_receipt;
        const Render::ValidationResult authenticated_lookup =
            m_authenticated_texture_receipts.FindResource(
                group, group_generation, resource_pointer_token,
                resource_handle, name, authenticated_receipt);
        if (authenticated_lookup ||
            authenticated_lookup.code !=
                Render::ValidationCode::MISSING_REFERENCE ||
            authenticated_lookup.field !=
                "texture_registry.resource_lookup")
        {
            // A live authenticated binding, poisoned registry, or generation
            // inconsistency is terminal for ordinary attribution. Never
            // relabel that state as an unauthenticated observation.
            return Ogre::DataStreamPtr();
        }
        Render::Ogre14SelectedTextureSourceReceipt selected_receipt;
        const Render::ValidationResult selected_lookup =
            m_selected_texture_sources.FindResource(
                group, group_generation, resource_pointer_token,
                resource_handle, name, selected_receipt);
        if (selected_lookup ||
            selected_lookup.code !=
                Render::ValidationCode::MISSING_REFERENCE ||
            selected_lookup.field !=
                "selected_texture_registry.resource_lookup")
        {
            return Ogre::DataStreamPtr();
        }
    }

    Ogre::DataStreamPtr opened_stream;
    Ogre::DataStreamPtr replacement;
    Ogre::MemoryDataStream* replacement_memory = nullptr;
    std::uintptr_t opened_stream_pointer_token = 0U;
    Ogre::String selected_archive_name;
    Ogre::String selected_archive_type;
    Ogre::String file_info_filename;
    Ogre::String file_info_path;
    Ogre::String file_info_basename;
    Ogre::String exact_member;
    Ogre::String opened_stream_name;
    std::uint64_t compressed_size = 0U;
    std::uint64_t uncompressed_size = 0U;
    std::uint64_t opened_stream_size = 0U;
    bool member_resolved_by_reviewed_selection = false;
    // Owns the record when OGRE supplied none. Declared at function scope so
    // effective_file_info stays valid through the final re-verification.
    Ogre::FileInfo reviewed_file_info;
    const Ogre::FileInfo* effective_file_info = exact_file_info;
    try
    {
        std::lock_guard<std::mutex> archive_lock(
            m_legacy_material_archive_io_mutex);
        Ogre::ResourceGroupManager* resource_manager =
            Ogre::ResourceGroupManager::getSingletonPtr();
        if (resource_manager == nullptr ||
            !resource_manager->resourceGroupExists(group))
        {
            return Ogre::DataStreamPtr();
        }
        std::size_t matching_location_count = 0U;
        bool selected_location_recursive = false;
        const Ogre::ResourceGroupManager::LocationList& locations =
            resource_manager->getResourceLocationList(group);
        for (const Ogre::ResourceGroupManager::ResourceLocation& location :
             locations)
        {
            if (location.archive == selected_archive)
            {
                selected_location_recursive = location.recursive;
                ++matching_location_count;
            }
        }
        if (matching_location_count != 1U)
        {
            return Ogre::DataStreamPtr();
        }
        if (effective_file_info == nullptr)
        {
            // OGRE compares FileInfo path + basename against the requested
            // resource name byte for byte before offering a record
            // (OgreResourceGroupManager.cpp, exact-material-script-preopen).
            // A material that declares 'alexissabergrillesspec.png' against
            // the archive member 'AlexisSabergrillesspec.png' therefore
            // reaches this callback with no record, even though the
            // case-insensitive archive open that follows succeeds. Rebuild the
            // record from the archive's own index through the reviewed
            // member-selection policy: it demands one unambiguous member, so a
            // genuinely absent texture and a case-only member collision both
            // still leave this ordinary source without a receipt.
            Ogre::String reviewed_member;
            if (!ResolveReviewedSelectedTextureSourceMember(
                    *selected_archive, selected_location_recursive, name,
                    reviewed_member, &reviewed_file_info) ||
                reviewed_member.empty() ||
                reviewed_file_info.path + reviewed_file_info.basename !=
                    reviewed_member)
            {
                LOG(fmt::format(
                    "[RoR|ContentManager|SelectedTextureSource] "
                    "Ordinary stage refused for '{}' group='{}': OGRE "
                    "offered no exact archive record and the reviewed "
                    "member policy resolved no single member",
                    name, group));
                return Ogre::DataStreamPtr();
            }
            effective_file_info = &reviewed_file_info;
            member_resolved_by_reviewed_selection = true;
        }
        if (effective_file_info->archive != selected_archive)
        {
            return Ogre::DataStreamPtr();
        }
        Ogre::String live_archive_name;
        if (!ResolveLiveArchiveManagerPointer(
                Ogre::ArchiveManager::getSingleton(), selected_archive,
                live_archive_name) ||
            package_archive_names.count(live_archive_name) != 1U)
        {
            return Ogre::DataStreamPtr();
        }
        selected_archive_name = live_archive_name;
        selected_archive_type = selected_archive->getType();
        file_info_filename = effective_file_info->filename;
        file_info_path = effective_file_info->path;
        file_info_basename = effective_file_info->basename;
        exact_member = file_info_path + file_info_basename;
        compressed_size =
            static_cast<std::uint64_t>(effective_file_info->compressedSize);
        uncompressed_size =
            static_cast<std::uint64_t>(effective_file_info->uncompressedSize);
        if (selected_archive_name.empty() ||
            selected_archive_type.empty() || exact_member.empty() ||
            compressed_size == 0U ||
            uncompressed_size == 0U ||
            uncompressed_size >
                m_selected_texture_source_configuration.maximum_source_bytes ||
            uncompressed_size >
                static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)()))
        {
            return Ogre::DataStreamPtr();
        }
        if (exact_member != name && !member_resolved_by_reviewed_selection)
        {
            // OGRE resolved this request through its case-insensitive archive
            // lookup, so the member it opened is spelled differently from the
            // requested resource name. Admit that only when the reviewed
            // member-selection policy resolves the identical member without
            // ambiguity; the receipt then names the member actually opened,
            // and a genuinely absent member still mints nothing.
            //
            // The pinned OGRE 14.5.2 pre-open seam never reaches this branch,
            // because it withholds its record entirely when path + basename
            // differs from the requested name; that case is admitted above
            // instead. This clause stays as the same guarantee for a record
            // OGRE offers under a looser future match rule.
            Ogre::String reviewed_member;
            if (!ResolveReviewedSelectedTextureSourceMember(
                    *selected_archive, selected_location_recursive, name,
                    reviewed_member) ||
                reviewed_member != exact_member)
            {
                return Ogre::DataStreamPtr();
            }
            member_resolved_by_reviewed_selection = true;
        }

        opened_stream = const_cast<Ogre::Archive*>(selected_archive)->open(
            exact_member);
        if (!opened_stream || opened_stream->size() !=
                static_cast<std::size_t>(uncompressed_size) ||
            opened_stream->getName().empty())
        {
            return Ogre::DataStreamPtr();
        }
        opened_stream_pointer_token =
            reinterpret_cast<std::uintptr_t>(opened_stream.get());
        opened_stream_name = opened_stream->getName();
        opened_stream_size =
            static_cast<std::uint64_t>(opened_stream->size());

        replacement_memory = OGRE_NEW Ogre::MemoryDataStream(
            name, static_cast<std::size_t>(uncompressed_size), true, false);
        replacement = Ogre::DataStreamPtr(replacement_memory);
        if (opened_stream->read(
                replacement_memory->getPtr(),
                static_cast<std::size_t>(uncompressed_size)) !=
                static_cast<std::size_t>(uncompressed_size) ||
            opened_stream->tell() !=
                static_cast<std::size_t>(uncompressed_size))
        {
            return Ogre::DataStreamPtr();
        }
        replacement->seek(0U);
    }
    catch (...)
    {
        // Ordinary observations are opportunistic. Archive, allocation, and
        // stream failures preserve OGRE's normal selected-location load.
        return Ogre::DataStreamPtr();
    }

    Render::Ogre14SelectedTextureSourceReceipt receipt;
    try
    {
        Render::Ogre14SelectedTextureSourceCaptureInput capture_input;
        capture_input.effective_resource_group = group;
        capture_input.group_generation = group_generation;
        capture_input.selected_archive_name = selected_archive_name;
        capture_input.selected_archive_type = selected_archive_type;
        capture_input.selected_archive_pointer_token =
            selected_archive_pointer_token;
        capture_input.file_info_archive_pointer_token =
            selected_archive_pointer_token;
        capture_input.file_info_filename = file_info_filename;
        capture_input.file_info_path = file_info_path;
        capture_input.file_info_basename = file_info_basename;
        capture_input.exact_member_name = exact_member;
        capture_input.file_info_compressed_size = compressed_size;
        capture_input.file_info_uncompressed_size = uncompressed_size;
        capture_input.opened_stream_pointer_token =
            opened_stream_pointer_token;
        capture_input.opened_stream_name = opened_stream_name;
        capture_input.opened_stream_size = opened_stream_size;
        capture_input.resource_pointer_token = resource_pointer_token;
        capture_input.resource_handle = resource_handle;
        capture_input.exact_resource_name = name;
        capture_input.resource_state_count_before_load =
            state_count_before_load;

        const Render::ValidationResult built =
            Render::BuildOgre14SelectedTextureSourceReceipt(
                m_selected_texture_source_configuration,
                capture_input,
                replacement_memory->getPtr(),
                static_cast<std::size_t>(uncompressed_size),
                receipt);
        if (!built)
        {
            return Ogre::DataStreamPtr();
        }
    }
    catch (...)
    {
        // Identity snapshots are opportunistic for ordinary locations. An
        // allocation failure before publication must leave OGRE's normal
        // archive load path available.
        return Ogre::DataStreamPtr();
    }

    try
    {
        std::scoped_lock<std::mutex, std::mutex> capture_lock(
            m_legacy_material_archive_io_mutex,
            m_legacy_material_state_mutex);
        Ogre::ResourceGroupManager* resource_manager =
            Ogre::ResourceGroupManager::getSingletonPtr();
        std::size_t matching_location_count = 0U;
        if (resource_manager != nullptr &&
            resource_manager->resourceGroupExists(group))
        {
            const Ogre::ResourceGroupManager::LocationList& locations =
                resource_manager->getResourceLocationList(group);
            for (const Ogre::ResourceGroupManager::ResourceLocation& location :
                 locations)
            {
                if (location.archive == selected_archive)
                {
                    ++matching_location_count;
                }
            }
        }
        const auto package_group = m_package_archives_by_group.find(group);
        const auto generation =
            m_legacy_material_group_generations.find(group);
        const Ogre::ResourcePtr by_handle =
            texture_manager->getByHandle(resource->getHandle());
        const Ogre::ResourcePtr by_name =
            texture_manager->getResourceByName(name, group);
        if (matching_location_count != 1U ||
            package_group == m_package_archives_by_group.end() ||
            package_group->second.empty() ||
            package_group->second.count(selected_archive_name) != 1U ||
            generation == m_legacy_material_group_generations.end() ||
            generation->second != group_generation ||
            m_authenticated_package_archives_by_group.find(group) !=
                m_authenticated_package_archives_by_group.end() ||
            m_authenticated_package_archive_bindings_by_group.find(group) !=
                m_authenticated_package_archive_bindings_by_group.end() ||
            !m_authenticated_texture_receipts.initialized() ||
            !m_selected_texture_sources.initialized() ||
            selected_archive->getName() != selected_archive_name ||
            selected_archive->getType() != selected_archive_type ||
            effective_file_info == nullptr ||
            // Re-reads the borrowed OGRE record to catch a mutation during the
            // archive read. When the record was rebuilt from the archive index
            // above, effective_file_info owns a private copy and the next six
            // comparisons are necessarily true; nothing is lost, because a
            // copy has no other writer.
            effective_file_info->archive != selected_archive ||
            effective_file_info->filename != file_info_filename ||
            effective_file_info->path != file_info_path ||
            effective_file_info->basename != file_info_basename ||
            static_cast<std::uint64_t>(effective_file_info->compressedSize) !=
                compressed_size ||
            static_cast<std::uint64_t>(
                effective_file_info->uncompressedSize) != uncompressed_size ||
            exact_member != file_info_path + file_info_basename ||
            (exact_member != name &&
             !member_resolved_by_reviewed_selection) ||
            // OGRE 14 invokes resourceStreamOpening() while the Resource is
            // still unloaded; resourceStreamOpened() is the later seam that
            // must observe the exact owner in LOADSTATE_LOADING before this
            // staged source receipt can be committed.
            resource->isLoaded() ||
            resource->getStateCount() !=
                static_cast<std::size_t>(state_count_before_load) ||
            resource->getHandle() !=
                static_cast<Ogre::ResourceHandle>(resource_handle) ||
            resource->getName() != name || resource->getGroup() != group ||
            !by_handle || !by_name || by_handle.get() != resource ||
            by_name.get() != resource ||
            replacement.get() != replacement_memory ||
            replacement->getName() != name || replacement->tell() != 0U ||
            replacement->size() !=
                static_cast<std::size_t>(uncompressed_size) ||
            !receipt.ReplacementBytesMatch(
                replacement_memory->getPtr(), replacement->size()))
        {
            return Ogre::DataStreamPtr();
        }

        const std::uint64_t source_bytes =
            static_cast<std::uint64_t>(receipt.source_size());
        if (source_bytes >
                (std::numeric_limits<std::uint64_t>::max)() / 2U)
        {
            return Ogre::DataStreamPtr();
        }
        const std::uint64_t stage_charge = source_bytes * 2U;
        const std::size_t native_replacement_name_bytes =
            replacement->getName().size();
        const std::uint64_t replacement_name_bytes =
            static_cast<std::uint64_t>(native_replacement_name_bytes);
        if (static_cast<std::size_t>(replacement_name_bytes) !=
                native_replacement_name_bytes ||
            receipt.identity_size() >
                (std::numeric_limits<std::uint64_t>::max)() -
                    replacement_name_bytes)
        {
            return Ogre::DataStreamPtr();
        }
        const std::uint64_t identity_charge =
            receipt.identity_size() + replacement_name_bytes;
        if (m_selected_texture_source_stages.size() >=
                m_selected_texture_source_configuration.
                    maximum_live_receipts ||
            stage_charge >
                m_selected_texture_source_configuration.
                    maximum_retained_source_bytes ||
            m_selected_texture_source_staged_bytes >
                m_selected_texture_source_configuration.
                        maximum_retained_source_bytes -
                    stage_charge ||
            identity_charge >
                m_selected_texture_source_configuration.
                    maximum_total_identity_bytes ||
            m_selected_texture_source_staged_identity_bytes >
                m_selected_texture_source_configuration.
                        maximum_total_identity_bytes -
                    identity_charge ||
            m_selected_texture_source_stages.find(resource) !=
                m_selected_texture_source_stages.end())
        {
            return Ogre::DataStreamPtr();
        }

        SelectedTextureSourceStage stage;
        stage.receipt = receipt;
        stage.expected_stream = replacement;
        stage.expected_memory_stream = replacement_memory;
        stage.retained_source_charge = stage_charge;
        stage.retained_identity_charge = identity_charge;
        const auto inserted = m_selected_texture_source_stages.emplace(
            resource, std::move(stage));
        if (!inserted.second)
        {
            return Ogre::DataStreamPtr();
        }
        m_selected_texture_source_staged_bytes += stage_charge;
        m_selected_texture_source_staged_identity_bytes += identity_charge;
        handled = true;
        return replacement;
    }
    catch (...)
    {
        // No stage or registry state was published before the final emplace.
        // A failed emplace is strongly exception-safe; normal OGRE loading
        // remains authoritative for this ordinary source.
        return Ogre::DataStreamPtr();
    }
}

bool ContentManager::resourceStreamOpeningEnabled() const
{
    return true;
}

Ogre::DataStreamPtr ContentManager::resourceStreamOpening(
    const Ogre::String& name,
    const Ogre::String& group,
    Ogre::Resource* resource,
    const Ogre::Archive* selected_archive,
    const Ogre::FileInfo* exact_file_info,
    bool& handled)
{
    this->RequireAuthenticatedResourceThread(
        "ContentManager::resourceStreamOpening");
    handled = false;
    if (resource != nullptr)
    {
        return this->OpenSelectedTextureSourceStream(
            name, group, resource, selected_archive,
            exact_file_info, handled);
    }

    std::uint64_t parse_token = 0U;
    std::uint64_t group_generation = 0U;
    Ogre::String root_script_request;
    bool parse_has_authenticated_root = false;
    AuthenticatedPackageArchiveBinding archive_binding;
    bool selected_archive_is_authenticated = false;
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        AuthenticatedMaterialScriptGroupCandidate* candidate =
            m_authenticated_material_script_candidate.get();
        if (candidate == nullptr || !candidate->parse_active ||
            candidate->group != group ||
            group != m_scripting_resource_group)
        {
            return Ogre::DataStreamPtr();
        }
        if (candidate->poisoned)
        {
            // Once an authenticated root has been observed, every subsequent
            // compiler open must remain fail-closed. Returning handled=false
            // would make pinned OGRE fall back to raw archive bytes and lets a
            // cyclic import recurse after the attempt guard has fired.
            handled = candidate->parse_has_authenticated_root;
            return Ogre::DataStreamPtr();
        }
        const bool consumes_pending_import =
            candidate->pending_import_open;
        if (consumes_pending_import)
        {
            if (candidate->pending_import_name != name ||
                candidate->pending_import_group != group)
            {
                candidate->poisoned = true;
                handled = true;
                return Ogre::DataStreamPtr();
            }
            candidate->pending_import_open = false;
            candidate->pending_import_name.clear();
            candidate->pending_import_group.clear();
        }
        else
        {
            // Root script enumeration does not pass resourceLoading(), so its
            // exact pre-open owns one attempt here. Import attempts were
            // already charged when openResourceImpl entered resourceLoading.
            if (candidate->source_open_attempt_count >=
                    MAX_AUTHENTICATED_MATERIAL_SCRIPT_SOURCE_OPEN_ATTEMPTS ||
                candidate->source_open_attempt_count ==
                    (std::numeric_limits<std::uint64_t>::max)())
            {
                candidate->poisoned = true;
                handled = true;
                return Ogre::DataStreamPtr();
            }
            ++candidate->source_open_attempt_count;
        }
        parse_token = candidate->parse_token;
        group_generation = candidate->generation;
        root_script_request = candidate->root_script_request;
        parse_has_authenticated_root =
            candidate->parse_has_authenticated_root;
        if (candidate->parse_root_untrusted)
        {
            return Ogre::DataStreamPtr();
        }
        const auto archive_group =
            m_authenticated_package_archive_bindings_by_group.find(group);
        if (archive_group !=
                m_authenticated_package_archive_bindings_by_group.end() &&
            selected_archive != nullptr)
        {
            const auto binding =
                archive_group->second.find(selected_archive);
            if (binding != archive_group->second.end())
            {
                archive_binding = binding->second;
                selected_archive_is_authenticated = true;
            }
        }
        if (!selected_archive_is_authenticated)
        {
            if (!candidate->parse_root_open_observed &&
                !parse_has_authenticated_root &&
                name == candidate->root_script_request)
            {
                candidate->parse_root_open_observed = true;
                candidate->parse_root_untrusted = true;
            }
            else if (parse_has_authenticated_root)
            {
                candidate->poisoned = true;
                handled = true;
            }
            else if (!candidate->parse_root_untrusted)
            {
                candidate->poisoned = true;
                handled = true;
            }
            return Ogre::DataStreamPtr();
        }
        if (!candidate->parse_root_open_observed)
        {
            if (name != candidate->root_script_request)
            {
                candidate->poisoned = true;
                handled = true;
                return Ogre::DataStreamPtr();
            }
            candidate->parse_root_open_observed = true;
        }
        if (selected_archive == nullptr || exact_file_info == nullptr ||
            exact_file_info->archive != selected_archive ||
            archive_binding.archive_pointer_token !=
                reinterpret_cast<std::uintptr_t>(selected_archive) ||
            archive_binding.group_generation != group_generation ||
            !archive_binding.immutable_archive.initialized() ||
            !archive_binding.immutable_member_manifest ||
            archive_binding.immutable_archive.source_archive_identity() !=
                archive_binding.source_archive_identity ||
            archive_binding.immutable_archive.archive_sha256() !=
                archive_binding.archive_sha256)
        {
            candidate->poisoned = true;
            handled = true;
            return Ogre::DataStreamPtr();
        }
    }

    try
    {
        static constexpr std::size_t MAX_SCRIPT_BYTES =
            static_cast<std::size_t>(
                Render::kOgre14AuthenticatedMaterialScriptMaximumSourceBytes);
        const Ogre::String exact_member =
            exact_file_info->path + exact_file_info->basename;
        const auto manifest_member =
            archive_binding.immutable_member_manifest->find(exact_member);
        if (exact_file_info->basename.empty() ||
            manifest_member ==
                archive_binding.immutable_member_manifest->end() ||
            manifest_member->second.compressed_size !=
                static_cast<std::uint64_t>(
                    exact_file_info->compressedSize) ||
            manifest_member->second.uncompressed_size !=
                static_cast<std::uint64_t>(
                    exact_file_info->uncompressedSize) ||
            !Render::IsLowercaseOgre14Sha256(
                manifest_member->second.sha256))
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
            handled = true;
            return Ogre::DataStreamPtr();
        }
        const LegacyMaterialScriptEditPlan* edit_plan =
            FindLegacyMaterialScriptEditPlan(
                archive_binding.archive_sha256, exact_member);
        std::uint64_t maximum_effective_bytes =
            static_cast<std::uint64_t>(exact_file_info->uncompressedSize);
        bool valid_plan_bound = true;
        if (edit_plan != nullptr)
        {
            valid_plan_bound = edit_plan->edits != nullptr &&
                edit_plan->edit_count != 0U &&
                edit_plan->edit_count <=
                    kLegacyMaterialScriptMaximumRepairPlanEdits;
            for (std::size_t edit_index = 0U;
                 valid_plan_bound && edit_index < edit_plan->edit_count;
                 ++edit_index)
            {
                const LegacyMaterialScriptEdit& edit =
                    edit_plan->edits[edit_index];
                if (edit.expected == nullptr || edit.replacement == nullptr)
                {
                    valid_plan_bound = false;
                    break;
                }
                const std::size_t expected_size =
                    std::char_traits<char>::length(edit.expected);
                const std::size_t replacement_size =
                    std::char_traits<char>::length(edit.replacement);
                if (replacement_size > expected_size &&
                    !AddAuthenticatedMaterialScriptBytes(
                        maximum_effective_bytes,
                        static_cast<std::uint64_t>(
                            replacement_size - expected_size),
                        static_cast<std::uint64_t>(MAX_SCRIPT_BYTES),
                        maximum_effective_bytes))
                {
                    valid_plan_bound = false;
                }
            }
        }
        const std::uint64_t identity_bytes =
            AuthenticatedMaterialScriptPreopenIdentityBytes(
                group, root_script_request, name,
                archive_binding.source_archive_identity,
                archive_binding.selected_archive_name,
                archive_binding.selected_archive_type,
                archive_binding.archive_sha256,
                *exact_file_info, exact_member);
        std::uint64_t source_bytes = 0U;
        if (exact_file_info->uncompressedSize == 0U ||
            exact_file_info->uncompressedSize > MAX_SCRIPT_BYTES ||
            !valid_plan_bound ||
            identity_bytes == (std::numeric_limits<std::uint64_t>::max)() ||
            !AddAuthenticatedMaterialScriptBytes(
                static_cast<std::uint64_t>(exact_file_info->uncompressedSize),
                maximum_effective_bytes,
                m_authenticated_material_script_configuration.
                    maximum_retained_source_bytes,
                source_bytes))
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
            handled = true;
            return Ogre::DataStreamPtr();
        }
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            const auto key = std::make_pair(parse_token, name);
            const bool is_new_source = candidate != nullptr &&
                candidate->source_by_compiler_file.find(key) ==
                    candidate->source_by_compiler_file.end();
            std::uint64_t retained_after = 0U;
            std::uint64_t identities_after = 0U;
            if (candidate == nullptr || candidate->poisoned ||
                !candidate->parse_active ||
                candidate->parse_token != parse_token ||
                candidate->generation != group_generation ||
                candidate->group != group ||
                (is_new_source &&
                 (candidate->next_source_open_ordinal >= 4096U ||
                  candidate->staged_source_count >=
                      m_authenticated_material_script_configuration.
                          maximum_live_sources ||
                  !AddAuthenticatedMaterialScriptBytes(
                      candidate->staged_source_bytes, source_bytes,
                      m_authenticated_material_script_configuration.
                          maximum_retained_source_bytes,
                      retained_after) ||
                  !AddAuthenticatedMaterialScriptBytes(
                      candidate->staged_identity_bytes, identity_bytes,
                      m_authenticated_material_script_configuration.
                          maximum_total_identity_bytes,
                      identities_after))))
            {
                if (candidate != nullptr)
                {
                    candidate->poisoned = true;
                }
                handled = true;
                return Ogre::DataStreamPtr();
            }
        }

    std::string original;
    try
    {
        std::lock_guard<std::mutex> archive_lock(
            m_legacy_material_archive_io_mutex);
        Ogre::ArchiveManager* archive_manager =
            Ogre::ArchiveManager::getSingletonPtr();
        Ogre::ResourceGroupManager* resource_manager =
            Ogre::ResourceGroupManager::getSingletonPtr();
        Render::Ogre14AuthenticatedArchiveAuthorityProof archive_authority;
        if (archive_manager != nullptr && resource_manager != nullptr)
        {
            archive_authority =
                Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                    *archive_manager,
                    *resource_manager,
                    group,
                    archive_binding.selected_archive_name);
        }
        Ogre::Archive* live_archive = archive_authority.manager_archive;
        if (archive_manager == nullptr || resource_manager == nullptr ||
            !archive_authority.AuthenticatesExclusive(selected_archive) ||
            live_archive->getName() !=
                archive_binding.selected_archive_name ||
            live_archive->getType() !=
                archive_binding.selected_archive_type ||
            archive_binding.selected_archive_type != "EmbeddedZip" ||
            !live_archive->isReadOnly())
        {
            throw std::runtime_error(
                "authenticated material-script archive authority changed");
        }
        Ogre::DataStreamPtr archive_stream =
            live_archive->open(exact_member);
        if (!archive_stream || archive_stream->size() !=
                exact_file_info->uncompressedSize ||
            archive_stream->size() > MAX_SCRIPT_BYTES)
        {
            throw std::runtime_error(
                "authenticated material-script member size mismatch");
        }
        original = archive_stream->getAsString();
        if (original.size() != archive_stream->size())
        {
            throw std::runtime_error(
                "authenticated material-script member read mismatch");
        }
    }
    catch (...)
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        if (m_authenticated_material_script_candidate != nullptr)
        {
            m_authenticated_material_script_candidate->poisoned = true;
        }
        handled = true;
        return Ogre::DataStreamPtr();
    }

    const std::string original_sha256 = Sha256Bytes(original);
    if (original_sha256.empty() ||
        original_sha256 != manifest_member->second.sha256)
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        if (m_authenticated_material_script_candidate != nullptr)
        {
            m_authenticated_material_script_candidate->poisoned = true;
        }
        handled = true;
        return Ogre::DataStreamPtr();
    }

    std::string effective = original;
    std::string repair_plan_sha256;
    Render::Ogre14MaterialScriptRepairState repair_state =
        Render::Ogre14MaterialScriptRepairState::NONE;
    std::uint64_t applied_edit_count = 0U;
    std::vector<Ogre::String> texture_fallback_names;
    if (edit_plan != nullptr)
    {
        const LegacyMaterialScriptPlanApplication patched =
            ApplyLegacyMaterialScriptEditPlan(
                *edit_plan, original_sha256, original);
        if (!patched.safe || !patched.applicable ||
            patched.applied_edit_count != edit_plan->edit_count ||
            !ComputeLegacyMaterialScriptAppliedRepairPlanSha256(
                *edit_plan, exact_member, original_sha256,
                repair_plan_sha256))
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
            handled = true;
            return Ogre::DataStreamPtr();
        }
        effective = patched.payload;
        repair_state = Render::Ogre14MaterialScriptRepairState::APPLIED;
        applied_edit_count = patched.applied_edit_count;
        for (std::size_t edit_index = 0U;
             edit_index < edit_plan->edit_count; ++edit_index)
        {
            const char* replacement_token =
                edit_plan->edits[edit_index].replacement;
            if (replacement_token == nullptr)
            {
                continue;
            }
            const std::string replacement(replacement_token);
            static const std::string TEXTURE_PREFIX = "texture ";
            if (replacement.compare(
                    0U, TEXTURE_PREFIX.size(), TEXTURE_PREFIX) != 0)
            {
                continue;
            }
            const Ogre::String fallback_name =
                replacement.substr(TEXTURE_PREFIX.size());
            LegacyMaterialColor fallback_color = {0U, 0U, 0U, false};
            if (ResolveLegacyMissingTexture(
                    archive_binding.archive_sha256,
                    fallback_name, fallback_color))
            {
                texture_fallback_names.push_back(fallback_name);
            }
        }
    }
    else if (!ComputeLegacyMaterialScriptNoRepairPlanSha256(
                 archive_binding.archive_sha256, exact_member,
                 original_sha256, repair_plan_sha256))
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        if (m_authenticated_material_script_candidate != nullptr)
        {
            m_authenticated_material_script_candidate->poisoned = true;
        }
        handled = true;
        return Ogre::DataStreamPtr();
    }

    const std::string effective_sha256 = Sha256Bytes(effective);
    if (effective_sha256.empty() || effective.size() > MAX_SCRIPT_BYTES)
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        if (m_authenticated_material_script_candidate != nullptr)
        {
            m_authenticated_material_script_candidate->poisoned = true;
        }
        handled = true;
        return Ogre::DataStreamPtr();
    }
    for (const Ogre::String& fallback_name : texture_fallback_names)
    {
        if (Ogre::ResourceGroupManager::getSingleton().resourceExists(
                group, fallback_name))
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
            handled = true;
            return Ogre::DataStreamPtr();
        }
    }

    Render::Ogre14AuthenticatedMaterialScriptSourceInput source_input;
    auto& metadata = source_input.metadata;
    metadata.parse_token = parse_token;
    metadata.group_generation = group_generation;
    metadata.effective_group = group;
    metadata.root_script_request = root_script_request;
    metadata.compiler_file_identity = name;
    metadata.archive_source_identity =
        archive_binding.source_archive_identity;
    metadata.selected_archive_name =
        archive_binding.selected_archive_name;
    metadata.selected_archive_type =
        archive_binding.selected_archive_type;
    metadata.archive_sha256 = archive_binding.archive_sha256;
    metadata.archive_pointer_token =
        archive_binding.archive_pointer_token;
    metadata.file_info_filename = exact_file_info->filename;
    metadata.file_info_path = exact_file_info->path;
    metadata.file_info_basename = exact_file_info->basename;
    metadata.exact_member_name = exact_member;
    metadata.compressed_size =
        static_cast<std::uint64_t>(exact_file_info->compressedSize);
    metadata.uncompressed_size =
        static_cast<std::uint64_t>(exact_file_info->uncompressedSize);
    metadata.original_byte_count = original.size();
    metadata.effective_byte_count = effective.size();
    metadata.original_sha256 = original_sha256;
    metadata.effective_sha256 = effective_sha256;
    metadata.repair_plan_version =
        kLegacyMaterialScriptRepairPlanVersion;
    metadata.repair_plan_sha256 = repair_plan_sha256;
    metadata.repair_state = repair_state;
    metadata.applied_edit_count = applied_edit_count;
    source_input.authenticated_archive_snapshot =
        archive_binding.immutable_archive;
    source_input.original_bytes =
        std::make_shared<const std::vector<std::uint8_t>>(
            original.begin(), original.end());
    source_input.effective_bytes =
        std::make_shared<const std::vector<std::uint8_t>>(
            effective.begin(), effective.end());

    Ogre::DataStreamPtr replacement(
        OGRE_NEW Ogre::MemoryDataStream(
            name, effective.size(), true, false));
    if (!effective.empty())
    {
        replacement->write(effective.data(), effective.size());
    }
    replacement->seek(0U);

    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        AuthenticatedMaterialScriptGroupCandidate* candidate =
            m_authenticated_material_script_candidate.get();
        if (candidate == nullptr || candidate->poisoned ||
            !candidate->parse_active || candidate->parse_token != parse_token ||
            candidate->generation != group_generation ||
            candidate->group != group)
        {
            handled = true;
            return Ogre::DataStreamPtr();
        }
        const auto key = std::make_pair(parse_token, name);
        const auto existing = candidate->source_by_compiler_file.find(key);
        if (existing != candidate->source_by_compiler_file.end())
        {
            auto& staged = candidate->sources.at(existing->second);
            const auto& staged_metadata = staged.input.metadata;
            if (staged_metadata.archive_pointer_token !=
                    metadata.archive_pointer_token ||
                staged_metadata.exact_member_name !=
                    metadata.exact_member_name ||
                staged_metadata.original_sha256 != metadata.original_sha256 ||
                staged_metadata.effective_sha256 !=
                    metadata.effective_sha256 ||
                staged_metadata.repair_plan_sha256 !=
                    metadata.repair_plan_sha256 ||
                !staged.input.authenticated_archive_snapshot.
                    SharesImmutableStateWith(
                        source_input.authenticated_archive_snapshot) ||
                !staged.input.original_bytes ||
                !staged.input.effective_bytes ||
                *staged.input.original_bytes != *source_input.original_bytes ||
                *staged.input.effective_bytes != *source_input.effective_bytes)
            {
                candidate->poisoned = true;
                handled = true;
                return Ogre::DataStreamPtr();
            }
            staged.expected_stream = replacement;
            staged.delivered = false;
        }
        else
        {
            if (candidate->next_source_open_ordinal ==
                    (std::numeric_limits<std::uint64_t>::max)() ||
                candidate->next_source_open_ordinal >= 4096U ||
                candidate->staged_source_count >=
                    m_authenticated_material_script_configuration.
                        maximum_live_sources)
            {
                candidate->poisoned = true;
                handled = true;
                return Ogre::DataStreamPtr();
            }
            std::uint64_t source_byte_count = 0U;
            std::uint64_t prospective_source_bytes = 0U;
            std::uint64_t prospective_identity_bytes = 0U;
            const std::uint64_t source_identity_bytes =
                AuthenticatedMaterialScriptSourceIdentityBytes(metadata);
            if (!AddAuthenticatedMaterialScriptBytes(
                    static_cast<std::uint64_t>(original.size()),
                    static_cast<std::uint64_t>(effective.size()),
                    m_authenticated_material_script_configuration.
                        maximum_retained_source_bytes,
                    source_byte_count) ||
                source_identity_bytes ==
                    (std::numeric_limits<std::uint64_t>::max)() ||
                !AddAuthenticatedMaterialScriptBytes(
                    candidate->staged_source_bytes, source_byte_count,
                    m_authenticated_material_script_configuration.
                        maximum_retained_source_bytes,
                    prospective_source_bytes) ||
                !AddAuthenticatedMaterialScriptBytes(
                    candidate->staged_identity_bytes, source_identity_bytes,
                    m_authenticated_material_script_configuration.
                        maximum_total_identity_bytes,
                    prospective_identity_bytes))
            {
                candidate->poisoned = true;
                handled = true;
                return Ogre::DataStreamPtr();
            }
            ++candidate->next_source_open_ordinal;
            metadata.source_open_ordinal =
                candidate->next_source_open_ordinal;
            if (!candidate->parse_has_authenticated_root)
            {
                if (name != candidate->root_script_request ||
                    metadata.source_open_ordinal != 1U)
                {
                    candidate->poisoned = true;
                    handled = true;
                    return Ogre::DataStreamPtr();
                }
                metadata.source_role =
                    Render::Ogre14MaterialScriptSourceRole::ROOT_SCRIPT;
                candidate->parse_has_authenticated_root = true;
            }
            else
            {
                metadata.source_role = Render::
                    Ogre14MaterialScriptSourceRole::COMPILER_DEPENDENCY;
            }
            AuthenticatedMaterialScriptGroupCandidate::SourceStage stage;
            stage.input = std::move(source_input);
            stage.expected_stream = replacement;
            const std::size_t source_index = candidate->sources.size();
            candidate->sources.push_back(std::move(stage));
            candidate->source_by_compiler_file.emplace(key, source_index);
            candidate->staged_source_bytes = prospective_source_bytes;
            candidate->staged_identity_bytes = prospective_identity_bytes;
            ++candidate->staged_source_count;
        }
        auto& authorized_fallbacks =
            candidate->authorized_texture_fallbacks;
        for (const Ogre::String& fallback_name : texture_fallback_names)
        {
            const auto found = authorized_fallbacks.find(fallback_name);
            if (found != authorized_fallbacks.end() &&
                found->second != archive_binding.archive_sha256)
            {
                candidate->poisoned = true;
                handled = true;
                return Ogre::DataStreamPtr();
            }
        }
        for (const Ogre::String& fallback_name : texture_fallback_names)
        {
            authorized_fallbacks[fallback_name] =
                archive_binding.archive_sha256;
        }
    }
        handled = true;
        return replacement;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        AuthenticatedMaterialScriptGroupCandidate* candidate =
            m_authenticated_material_script_candidate.get();
        if (candidate != nullptr && candidate->parse_active &&
            candidate->parse_token == parse_token &&
            candidate->generation == group_generation &&
            candidate->group == group)
        {
            candidate->poisoned = true;
        }
        handled = true;
        return Ogre::DataStreamPtr();
    }
}
#endif

void ContentManager::resourceStreamOpened(const Ogre::String& name, const Ogre::String& group, Ogre::Resource* resource, Ogre::DataStreamPtr& dataStream)
{
#if OGRE_VERSION_MAJOR >= 14
    this->RequireAuthenticatedResourceThread(
        "ContentManager::resourceStreamOpened");
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
    if (resource != nullptr &&
        Ogre::TextureManager::getSingletonPtr() != nullptr &&
        resource->getCreator() == Ogre::TextureManager::getSingletonPtr())
    {
        Render::Ogre14SelectedTextureSourceReceipt committed_receipt;
        bool committed_selected_source = false;
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            const auto staged =
                m_selected_texture_source_stages.find(resource);
            if (staged != m_selected_texture_source_stages.end())
            {
                const auto* metadata = staged->second.receipt.metadata();
                Ogre::TextureManager* manager =
                    Ogre::TextureManager::getSingletonPtr();
                const auto generation =
                    m_legacy_material_group_generations.find(group);
                const auto package_group =
                    m_package_archives_by_group.find(group);
                const Ogre::ResourcePtr by_handle =
                    manager->getByHandle(resource->getHandle());
                const Ogre::ResourcePtr by_name =
                    manager->getResourceByName(name, group);
                const bool exact_delivery =
                    metadata != nullptr && dataStream &&
                    staged->second.expected_stream &&
                    staged->second.expected_memory_stream != nullptr &&
                    staged->second.expected_stream.get() ==
                        dataStream.get() &&
                    staged->second.expected_memory_stream ==
                        dataStream.get() &&
                    dataStream->getName() == name &&
                    dataStream->size() ==
                        staged->second.receipt.source_size() &&
                    dataStream->tell() == 0U &&
                    staged->second.receipt.ReplacementBytesMatch(
                        staged->second.expected_memory_stream->getPtr(),
                        dataStream->size()) &&
                    metadata->source.effective_resource_group == group &&
                    metadata->source.group_generation != 0U &&
                    generation !=
                        m_legacy_material_group_generations.end() &&
                    generation->second ==
                        metadata->source.group_generation &&
                    package_group != m_package_archives_by_group.end() &&
                    !package_group->second.empty() &&
                    package_group->second.count(
                        metadata->source.selected_archive_name) == 1U &&
                    m_authenticated_package_archives_by_group.find(group) ==
                        m_authenticated_package_archives_by_group.end() &&
                    m_authenticated_package_archive_bindings_by_group.find(
                        group) ==
                        m_authenticated_package_archive_bindings_by_group.end() &&
                    resource->getName() == name &&
                    resource->getGroup() == group &&
                    reinterpret_cast<std::uintptr_t>(resource) ==
                        metadata->source.resource_pointer_token &&
                    static_cast<std::uint64_t>(resource->getHandle()) ==
                        metadata->source.resource_handle &&
                    resource->getStateCount() ==
                        static_cast<std::size_t>(
                            metadata->source.
                                resource_state_count_before_load) &&
                    // The pinned pre-open and stream-opened callbacks both run
                    // inside ResourceGroupManager::openResource(), before
                    // Resource::load() publishes LOADSTATE_LOADING. Retain only
                    // an unloaded exact owner here; loaded resolution later
                    // requires state_count_before_load + 1, proving the one
                    // successful native load that consumed these bytes.
                    !resource->isLoaded() &&
                    by_handle && by_name && by_handle.get() == resource &&
                    by_name.get() == resource;

                if (exact_delivery)
                {
                    Render::Ogre14SelectedTextureSourceReceiptRegistry
                        registry_candidate = m_selected_texture_sources;
                    const Render::ValidationResult commit =
                        Render::CommitOgre14SelectedTextureSourceReceipt(
                            staged->second.receipt,
                            registry_candidate);
                    if (commit)
                    {
                        committed_receipt = staged->second.receipt;
                        m_selected_texture_sources =
                            std::move(registry_candidate);
                        committed_selected_source = true;
                    }
                }
                if (!committed_selected_source)
                {
                    // Name the exact clause that refused, once per texture.
                    // The predicate above is a long conjunction and a silent
                    // miss here costs the material its receipt downstream
                    // (selected_texture_registry.resource_lookup reports only
                    // "absent", which cannot distinguish these causes).
                    if (metadata != nullptr)
                    {
                        LOG(fmt::format(
                            "[RoR|ContentManager|SelectedTextureSource] "
                            "Ordinary receipt refused for '{}' group='{}': "
                            "stream={} gen={} pkg={} name={} group_match={} "
                            "ptr={} handle={} state={} loading={} loaded={} "
                            "by_handle={} by_name={}",
                            name, group,
                            static_cast<bool>(dataStream),
                            generation != m_legacy_material_group_generations.end() &&
                                generation->second == metadata->source.group_generation,
                            package_group != m_package_archives_by_group.end() &&
                                !package_group->second.empty() &&
                                package_group->second.count(
                                    metadata->source.selected_archive_name) == 1U,
                            resource->getName() == name,
                            resource->getGroup() == group,
                            reinterpret_cast<std::uintptr_t>(resource) ==
                                metadata->source.resource_pointer_token,
                            static_cast<std::uint64_t>(resource->getHandle()) ==
                                metadata->source.resource_handle,
                            resource->getStateCount() ==
                                static_cast<std::size_t>(
                                    metadata->source.resource_state_count_before_load),
                            resource->isLoading(), resource->isLoaded(),
                            static_cast<bool>(by_handle) && by_handle.get() == resource,
                            static_cast<bool>(by_name) && by_name.get() == resource));
                    }
                    // The archive stream is already open but the native
                    // Texture has not yet completed its load. Drop the
                    // pre-publication candidate and allow ordinary OGRE
                    // loading to continue without a selected-source receipt.
                    this->EraseSelectedTextureSourceStageLocked(resource);
                    return;
                }
                this->EraseSelectedTextureSourceStageLocked(resource);
            }
            else
            {
                const auto generation =
                    m_legacy_material_group_generations.find(
                        resource->getGroup());
                if (generation !=
                    m_legacy_material_group_generations.end())
                {
                    const Render::ValidationResult selected_removal =
                        Render::RemoveOgre14SelectedTextureSourceResource(
                            resource->getGroup(), generation->second,
                            reinterpret_cast<std::uintptr_t>(resource),
                            static_cast<std::uint64_t>(
                                resource->getHandle()),
                            resource->getName(),
                            m_selected_texture_sources);
                    if (!selected_removal)
                    {
                        // The Texture has not loaded yet. Throwing aborts the
                        // external mutation; the selected registry remains
                        // unpoisoned and retains its prior publication.
                        OGRE_EXCEPT(
                            Ogre::Exception::ERR_INVALID_STATE,
                            fmt::format(
                                "Selected texture '{}' in group '{}' could "
                                "not revoke a stale receipt at stream open: "
                                "{} ({})",
                                resource->getName(), resource->getGroup(),
                                selected_removal.detail,
                                selected_removal.field),
                            "ContentManager::resourceStreamOpened");
                    }
                }

                // A tracked authenticated texture is returned directly from
                // resourceLoading(). Reaching this callback without an exact
                // ordinary stage means OGRE opened another location, so
                // remove only an exact stale authenticated binding.
                const Render::ValidationResult removal =
                    Render::RemoveOgre14AuthenticatedTextureResource(
                        resource->getGroup(),
                        reinterpret_cast<std::uintptr_t>(resource),
                        static_cast<std::uint64_t>(resource->getHandle()),
                        resource->getName(),
                        m_authenticated_texture_receipts);
                if (!removal)
                {
                    // This callback proves OGRE selected an untrusted stream
                    // after authenticated authority was minted. Invalidate
                    // all current authenticated texture authority before any
                    // fallible diagnostic formatting.
                    Render::PoisonOgre14AuthenticatedTextureReceiptRegistry(
                        m_authenticated_texture_receipts);
                    LOG(fmt::format(
                        "[RoR|ContentManager|AuthenticatedTexture] Refused "
                        "stale stream-open binding removal for '{}' in group "
                        "'{}'; texture authority is terminally poisoned: {} "
                        "({})",
                        resource->getName(),
                        resource->getGroup(),
                        removal.detail,
                        removal.field));
                }
                return;
            }
        }

        if (committed_selected_source)
        {
            try
            {
                const auto* metadata = committed_receipt.metadata();
                if (metadata != nullptr)
                {
                    LOG(fmt::format(
                        "[RoR|ContentManager|SelectedTextureSource] "
                        "Committed ordinary selected-stream receipt "
                        "group='{}' texture='{}' member='{}' bytes={} "
                        "observed_bytes_sha256={}",
                        metadata->source.effective_resource_group,
                        metadata->source.exact_resource_name,
                        metadata->source.exact_member_name,
                        metadata->byte_count,
                        metadata->observed_bytes_sha256));
                }
            }
            catch (...)
            {
                // Diagnostics are never part of receipt authority.
            }
        }
        return;
    }
#endif

#if OGRE_VERSION_MAJOR >= 14
    if (resource != nullptr || !dataStream)
    {
        return;
    }
    try
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        AuthenticatedMaterialScriptGroupCandidate* candidate =
            m_authenticated_material_script_candidate.get();
        if (candidate == nullptr || !candidate->parse_active ||
            candidate->group != group || candidate->poisoned)
        {
            return;
        }
        const auto source = candidate->source_by_compiler_file.find(
            std::make_pair(candidate->parse_token, name));
        if (source == candidate->source_by_compiler_file.end())
        {
            if (candidate->parse_has_authenticated_root)
            {
                candidate->poisoned = true;
            }
            return;
        }
        auto& staged = candidate->sources.at(source->second);
        if (!staged.expected_stream ||
            staged.expected_stream.get() != dataStream.get() ||
            dataStream->getName() != name || dataStream->tell() != 0U ||
            !staged.input.effective_bytes ||
            dataStream->size() != staged.input.effective_bytes->size())
        {
            candidate->poisoned = true;
            return;
        }
        const std::string delivered = dataStream->getAsString();
        dataStream->seek(0U);
        if (delivered.size() != staged.input.effective_bytes->size() ||
            !std::equal(
                delivered.begin(), delivered.end(),
                staged.input.effective_bytes->begin()))
        {
            candidate->poisoned = true;
            return;
        }
        staged.delivered = true;
#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
        if (m_force_next_authenticated_material_script_source_undelivered_for_testing)
        {
            // Hostile native fixture: exercise the exact event-source gate
            // after a byte-identical post-open callback without exposing a
            // production mint or mutable receipt surface.
            staged.delivered = false;
            m_force_next_authenticated_material_script_source_undelivered_for_testing =
                false;
        }
#endif
        // OGRE retains the callback stream for the remainder of parsing. Drop
        // our verification-only owner immediately so retained-byte accounting
        // covers exactly the immutable source owner kept by the candidate.
        staged.expected_stream.setNull();
    }
    catch (...)
    {
        std::lock_guard<std::mutex> state_lock(
            m_legacy_material_state_mutex);
        if (m_authenticated_material_script_candidate != nullptr)
        {
            m_authenticated_material_script_candidate->poisoned = true;
        }
    }
    return;
#else
    static const std::size_t MAX_PACKAGE_MATERIAL_SCRIPT_BYTES =
        16U * 1024U * 1024U;
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
#endif
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
#if OGRE_VERSION_MAJOR >= 14
    this->RequireAuthenticatedResourceThread(
        "ContentManager::handleEvent");
#endif
    if (evt->mType == CreateMaterialScriptCompilerEvent::eventType)
    {
        // Workaround for OGRE script compiler not properly checking that material name is not empty.
        // See https://github.com/RigsOfRods/rigs-of-rods/issues/2349
        auto* matEvent = static_cast<CreateMaterialScriptCompilerEvent*>(evt);
#if defined(ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING)
        if (m_force_next_authenticated_material_event_empty_for_testing)
        {
            m_force_next_authenticated_material_event_empty_for_testing = false;
            matEvent->mName.clear();
        }
#endif
        if (retval != nullptr)
        {
            *static_cast<Ogre::Material**>(retval) = nullptr;
        }
        if (matEvent->mName.empty())
        {
#if OGRE_VERSION_MAJOR >= 14
            {
                std::lock_guard<std::mutex> state_lock(
                    m_legacy_material_state_mutex);
                if (m_authenticated_material_script_candidate != nullptr &&
                    m_authenticated_material_script_candidate->parse_active &&
                    m_authenticated_material_script_candidate->
                        parse_has_authenticated_root &&
                    m_authenticated_material_script_candidate->group ==
                        matEvent->mResourceGroup &&
                    m_authenticated_material_script_candidate->group ==
                        m_scripting_resource_group)
                {
                    m_authenticated_material_script_candidate->poisoned = true;
                }
            }
#endif
            RoR::LogFormat("[RoR] Got malformed material (empty name) from file: '%s' - forcing OGRE to fail loading.",
                matEvent->mFile.c_str());
            // Report "handled" but create nothing -> OGRE will interrupt the loading
            //   with message "failed to find or create material" [in MaterialTranslator::translate()]
            return true;
        }

#if OGRE_VERSION_MAJOR >= 14
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            if (candidate != nullptr && candidate->parse_active &&
                candidate->parse_has_authenticated_root &&
                candidate->group == matEvent->mResourceGroup &&
                candidate->group == m_scripting_resource_group &&
                (candidate->poisoned || candidate->pending_import_open))
            {
                // Once exact source authority has failed, do not delegate a
                // later material event back to OGRE. Returning false here
                // would create an unauthenticated native fallback after the
                // group transaction was already doomed.
                if (retval != nullptr)
                {
                    *static_cast<Ogre::Material**>(retval) = nullptr;
                }
                candidate->poisoned = true;
                return true;
            }
        }
        std::lock_guard<std::mutex> resolution_lock(
            m_legacy_material_resolution_mutex);
        Ogre::MaterialManager* manager =
            Ogre::MaterialManager::getSingletonPtr();
        if (manager == nullptr)
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            if (candidate != nullptr && candidate->parse_active &&
                candidate->parse_has_authenticated_root &&
                candidate->group == matEvent->mResourceGroup &&
                candidate->group == m_scripting_resource_group)
            {
                candidate->poisoned = true;
                return true;
            }
            return false;
        }
        if (retval != nullptr)
        {
            *static_cast<Ogre::Material**>(retval) = nullptr;
        }
        std::uint64_t parse_token = 0U;
        std::uint64_t event_ordinal = 0U;
        std::size_t source_index = 0U;
        std::string archive_sha256;
        std::uint64_t binding_identity_bytes =
            AuthenticatedMaterialScriptBindingIdentityBytes(
                matEvent->mName, matEvent->mResourceGroup, matEvent->mFile);
        bool authenticated_event = false;
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            if (candidate != nullptr && candidate->parse_active &&
                !candidate->poisoned &&
                candidate->parse_has_authenticated_root &&
                candidate->group == matEvent->mResourceGroup &&
                candidate->group == m_scripting_resource_group)
            {
                std::uint64_t prospective_identity_bytes = 0U;
                if (binding_identity_bytes ==
                        (std::numeric_limits<std::uint64_t>::max)() ||
                    candidate->next_event_ordinal ==
                        (std::numeric_limits<std::uint64_t>::max)() ||
                    candidate->next_event_ordinal >=
                        m_authenticated_material_script_configuration.
                            maximum_live_receipts ||
                    candidate->staged_receipt_count >=
                        m_authenticated_material_script_configuration.
                            maximum_live_receipts ||
                    !AddAuthenticatedMaterialScriptBytes(
                        candidate->staged_identity_bytes,
                        binding_identity_bytes,
                        m_authenticated_material_script_configuration.
                            maximum_total_identity_bytes,
                        prospective_identity_bytes))
                {
                    candidate->poisoned = true;
                    if (retval != nullptr)
                    {
                        *static_cast<Ogre::Material**>(retval) = nullptr;
                    }
                    return true;
                }
                event_ordinal = candidate->next_event_ordinal + 1U;
                const auto source =
                    candidate->source_by_compiler_file.find(
                        std::make_pair(
                            candidate->parse_token, matEvent->mFile));
                if (source ==
                        candidate->source_by_compiler_file.end() ||
                    !candidate->sources.at(source->second).delivered)
                {
                    candidate->poisoned = true;
                    if (retval != nullptr)
                    {
                        *static_cast<Ogre::Material**>(retval) = nullptr;
                    }
                    return true;
                }
                parse_token = candidate->parse_token;
                source_index = source->second;
                archive_sha256 = candidate->sources.at(source_index)
                    .input.metadata.archive_sha256;
                authenticated_event = true;
            }
        }
        if (!authenticated_event)
        {
            return false;
        }
        if (retval == nullptr ||
            manager->getByName(
                matEvent->mName, matEvent->mResourceGroup))
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
            return true;
        }

        AuthenticatedMaterialScriptGroupCandidate::MaterialStage stage;
        try
        {
            stage.input.source_index = source_index;
            stage.input.binding.event_ordinal = event_ordinal;
            stage.input.binding.exact_material_name = matEvent->mName;
            stage.input.binding.exact_group = matEvent->mResourceGroup;
            stage.input.binding.exact_origin = matEvent->mFile;
            stage.parse_token = parse_token;
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            if (candidate == nullptr || candidate->poisoned ||
                !candidate->parse_active ||
                candidate->parse_token != parse_token ||
                candidate->group != matEvent->mResourceGroup ||
                candidate->next_event_ordinal + 1U != event_ordinal ||
                source_index >= candidate->sources.size())
            {
                if (candidate != nullptr)
                {
                    candidate->poisoned = true;
                }
                return true;
            }
            candidate->materials.reserve(candidate->materials.size() + 1U);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
            return true;
        }

        Ogre::MaterialPtr created;
        try
        {
            created = manager->create(
                matEvent->mName, matEvent->mResourceGroup);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
            return true;
        }
        if (!created || created->getCreator() != manager ||
            created->getName() != matEvent->mName ||
            created->getGroup() != matEvent->mResourceGroup)
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
            return true;
        }

        try
        {
            stage.input.binding.material_pointer_token =
                reinterpret_cast<std::uintptr_t>(created.get());
            stage.input.binding.material_handle =
                static_cast<std::uint64_t>(created->getHandle());
            stage.retained_material = created;
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            AuthenticatedMaterialScriptGroupCandidate* candidate =
                m_authenticated_material_script_candidate.get();
            if (candidate == nullptr || candidate->poisoned ||
                !candidate->parse_active ||
                candidate->parse_token != parse_token ||
                candidate->group != matEvent->mResourceGroup ||
                candidate->next_event_ordinal + 1U != event_ordinal ||
                source_index >= candidate->sources.size())
            {
                if (candidate != nullptr)
                {
                    candidate->poisoned = true;
                }
                return true;
            }
            std::uint64_t prospective_identity_bytes = 0U;
            if (!AddAuthenticatedMaterialScriptBytes(
                    candidate->staged_identity_bytes,
                    binding_identity_bytes,
                    m_authenticated_material_script_configuration.
                        maximum_total_identity_bytes,
                    prospective_identity_bytes))
            {
                candidate->poisoned = true;
                return true;
            }
            candidate->materials.push_back(std::move(stage));
            candidate->next_event_ordinal = event_ordinal;
            candidate->package_materials.insert(matEvent->mName);
            candidate->authenticated_materials[archive_sha256]
                .insert(matEvent->mName);
            candidate->staged_identity_bytes = prospective_identity_bytes;
            ++candidate->staged_receipt_count;
            *static_cast<Ogre::Material**>(retval) = created.get();
        }
        catch (...)
        {
            std::lock_guard<std::mutex> state_lock(
                m_legacy_material_state_mutex);
            if (m_authenticated_material_script_candidate != nullptr)
            {
                m_authenticated_material_script_candidate->poisoned = true;
            }
        }
        return true;
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

    // OGRE 14's programmable-only renderers generate the receiver programs
    // through RTShader System. Keep the explicit GL3Plus/D3D11 "on" programs
    // reserved for the older managed-material route instead of double-binding
    // a generated receiver in OGRE 14.
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
    // The older managed-material route uses the explicit GL3Plus/D3D11 PSSM
    // programs shipped in the "on" tree.
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
