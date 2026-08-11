/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "resources/ContentManager.h"

#include <OgreMaterialManager.h>
#include <OgreRoot.h>
#include <OgreScriptCompiler.h>
#include <OgreTextureManager.h>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace RoR {

int RunOgre14AuthenticatedMaterialScriptNativeIntegrationTests(
    int argc,
    char** argv);

// Compiled only into the internal RoR CTest host. Production ContentManager
// builds expose no test authority or public listener-registration bypass.
class ContentManagerNativeIntegrationTestAccess final
{
public:
    static void InstallListeners(ContentManager& content)
    {
        content.EnsureResourceGroupListener();
    }

    static void ForceNextScriptSourceUndelivered(ContentManager& content)
    {
        content.
            m_force_next_authenticated_material_script_source_undelivered_for_testing =
                true;
    }

    static void ForceNextMaterialEventNameEmpty(ContentManager& content)
    {
        content.m_force_next_authenticated_material_event_empty_for_testing =
            true;
    }

    static Render::ValidationResult FindSelectedTextureSourceReceipt(
        ContentManager& content,
        Ogre::Texture& texture,
        Render::Ogre14SelectedTextureSourceReceipt& receipt)
    {
        std::lock_guard<std::mutex> state_lock(
            content.m_legacy_material_state_mutex);
        const auto generation =
            content.m_legacy_material_group_generations.find(
                texture.getGroup());
        if (generation ==
            content.m_legacy_material_group_generations.end())
        {
            return Render::ValidationResult::Failure(
                Render::ValidationCode::MISSING_REFERENCE,
                "native_selected_texture.group_generation",
                "selected texture has no native test generation");
        }
        return content.m_selected_texture_sources.FindResource(
            texture.getGroup(), generation->second,
            reinterpret_cast<std::uintptr_t>(&texture),
            static_cast<std::uint64_t>(texture.getHandle()),
            texture.getName(), receipt);
    }

    static bool InstallAuthenticatedNameMapOnly(
        ContentManager& content,
        const Ogre::String& group)
    {
        std::lock_guard<std::mutex> state_lock(
            content.m_legacy_material_state_mutex);
        const bool bindings_absent =
            content.m_authenticated_package_archive_bindings_by_group.find(
                group) ==
            content.m_authenticated_package_archive_bindings_by_group.end();
        const auto inserted =
            content.m_authenticated_package_archives_by_group.try_emplace(
                group);
        return bindings_absent && inserted.second;
    }

    static void RemoveAuthenticatedNameMapOnly(
        ContentManager& content,
        const Ogre::String& group)
    {
        std::lock_guard<std::mutex> state_lock(
            content.m_legacy_material_state_mutex);
        content.m_authenticated_package_archives_by_group.erase(group);
    }
};

} // namespace RoR

namespace {

using ArchiveEntries = std::vector<std::pair<std::string, std::string>>;

constexpr std::size_t AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT = 128U;
constexpr const char* NATIVE_INTEGRATION_ARGUMENT =
    "--internal-ogre14-authenticated-material-script-native-integration";
constexpr const char* CYCLE_CHILD_ARGUMENT =
    "--cycle-child";

void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    AppendU32(bytes, static_cast<std::uint32_t>(value & UINT32_MAX));
    AppendU32(bytes, static_cast<std::uint32_t>(value >> 32U));
}

void PatchU16(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint16_t value)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 2U,
            "ZIP fixture 16-bit patch is out of bounds");
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] =
        static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void PatchU32(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint32_t value)
{
    PatchU16(bytes, offset, static_cast<std::uint16_t>(value & 0xffffU));
    PatchU16(
        bytes, offset + 2U, static_cast<std::uint16_t>(value >> 16U));
}

std::uint32_t ReadU32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 4U,
            "ZIP fixture 32-bit read is out of bounds");
    return static_cast<std::uint32_t>(bytes[offset]) |
        static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U |
        static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U |
        static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U;
}

std::size_t ClassicCentralOffset(const std::vector<std::uint8_t>& bytes)
{
    Require(bytes.size() >= 22U &&
                ReadU32(bytes, bytes.size() - 22U) == 0x06054b50U,
            "ZIP fixture has no classic end record");
    return static_cast<std::size_t>(
        ReadU32(bytes, bytes.size() - 22U + 16U));
}

std::uint32_t Crc32(const std::string& value)
{
    std::uint32_t crc = 0xffffffffU;
    for (const unsigned char byte : value)
    {
        crc ^= static_cast<std::uint32_t>(byte);
        for (unsigned int bit = 0U; bit < 8U; ++bit)
        {
            const std::uint32_t mask =
                0U - static_cast<std::uint32_t>(crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return crc ^ 0xffffffffU;
}

std::vector<std::uint8_t> MakeStoredZip(const ArchiveEntries& entries)
{
    struct CentralRecord final
    {
        std::string name;
        std::uint32_t crc = 0U;
        std::uint32_t size = 0U;
        std::uint32_t local_offset = 0U;
    };

    Require(!entries.empty(), "ZIP fixture must contain at least one member");
    Require(entries.size() <=
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint16_t>::max)()),
            "ZIP fixture member count exceeds the classic ZIP bound");

    std::vector<std::uint8_t> bytes;
    std::vector<CentralRecord> central;
    central.reserve(entries.size());
    for (const auto& entry : entries)
    {
        Require(!entry.first.empty() &&
                    entry.first.size() <=
                        static_cast<std::size_t>(
                            (std::numeric_limits<std::uint16_t>::max)()) &&
                    entry.second.size() <=
                        static_cast<std::size_t>(
                            (std::numeric_limits<std::uint32_t>::max)()) &&
                    bytes.size() <=
                        static_cast<std::size_t>(
                            (std::numeric_limits<std::uint32_t>::max)()),
                "ZIP fixture member identity or payload exceeds its bound");
        CentralRecord record;
        record.name = entry.first;
        record.crc = Crc32(entry.second);
        record.size = static_cast<std::uint32_t>(entry.second.size());
        record.local_offset = static_cast<std::uint32_t>(bytes.size());

        AppendU32(bytes, 0x04034b50U);
        AppendU16(bytes, 20U);
        AppendU16(bytes, 0U);
        AppendU16(bytes, 0U); // Stored: no compressor is involved.
        AppendU16(bytes, 0U);
        AppendU16(bytes, 0U);
        AppendU32(bytes, record.crc);
        AppendU32(bytes, record.size);
        AppendU32(bytes, record.size);
        AppendU16(bytes, static_cast<std::uint16_t>(record.name.size()));
        AppendU16(bytes, 0U);
        bytes.insert(bytes.end(), record.name.begin(), record.name.end());
        bytes.insert(bytes.end(), entry.second.begin(), entry.second.end());
        central.push_back(std::move(record));
    }

    Require(bytes.size() <=
                static_cast<std::size_t>(
                    (std::numeric_limits<std::uint32_t>::max)()),
            "ZIP fixture local records exceed the classic ZIP bound");
    const std::uint32_t central_offset =
        static_cast<std::uint32_t>(bytes.size());
    for (const CentralRecord& record : central)
    {
        AppendU32(bytes, 0x02014b50U);
        AppendU16(bytes, 20U);
        AppendU16(bytes, 20U);
        AppendU16(bytes, 0U);
        AppendU16(bytes, 0U);
        AppendU16(bytes, 0U);
        AppendU16(bytes, 0U);
        AppendU32(bytes, record.crc);
        AppendU32(bytes, record.size);
        AppendU32(bytes, record.size);
        AppendU16(bytes, static_cast<std::uint16_t>(record.name.size()));
        AppendU16(bytes, 0U);
        AppendU16(bytes, 0U);
        AppendU16(bytes, 0U);
        AppendU16(bytes, 0U);
        AppendU32(bytes, 0U);
        AppendU32(bytes, record.local_offset);
        bytes.insert(bytes.end(), record.name.begin(), record.name.end());
    }
    Require(bytes.size() >= central_offset &&
                bytes.size() - central_offset <=
                    static_cast<std::size_t>(
                        (std::numeric_limits<std::uint32_t>::max)()),
            "ZIP fixture central directory exceeds the classic ZIP bound");
    const std::uint32_t central_size =
        static_cast<std::uint32_t>(bytes.size() - central_offset);

    AppendU32(bytes, 0x06054b50U);
    AppendU16(bytes, 0U);
    AppendU16(bytes, 0U);
    AppendU16(bytes, static_cast<std::uint16_t>(central.size()));
    AppendU16(bytes, static_cast<std::uint16_t>(central.size()));
    AppendU32(bytes, central_size);
    AppendU32(bytes, central_offset);
    AppendU16(bytes, 0U);
    return bytes;
}

std::string MakeOrdinaryDds(std::uint8_t red, std::uint8_t green,
                            std::uint8_t blue)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(144U);
    AppendU32(bytes, 0x20534444U); // DDS magic.
    AppendU32(bytes, 124U);
    AppendU32(bytes, 0x0000100fU);
    AppendU32(bytes, 2U);
    AppendU32(bytes, 2U);
    AppendU32(bytes, 8U);
    AppendU32(bytes, 0U);
    AppendU32(bytes, 0U);
    for (std::size_t index = 0U; index < 11U; ++index)
    {
        AppendU32(bytes, 0U);
    }
    AppendU32(bytes, 32U);
    AppendU32(bytes, 0x00000041U);
    AppendU32(bytes, 0U);
    AppendU32(bytes, 32U);
    AppendU32(bytes, 0x00ff0000U);
    AppendU32(bytes, 0x0000ff00U);
    AppendU32(bytes, 0x000000ffU);
    AppendU32(bytes, 0xff000000U);
    AppendU32(bytes, 0x00001000U);
    AppendU32(bytes, 0U);
    AppendU32(bytes, 0U);
    AppendU32(bytes, 0U);
    AppendU32(bytes, 0U);
    for (std::size_t pixel = 0U; pixel < 4U; ++pixel)
    {
        bytes.push_back(blue);
        bytes.push_back(green);
        bytes.push_back(red);
        bytes.push_back(255U);
    }
    Require(bytes.size() == 144U,
            "ordinary DDS fixture has an invalid byte count");
    return std::string(
        reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::filesystem::path WriteOrdinaryZip(
    const ArchiveEntries& entries,
    const std::string& label)
{
    static std::uint64_t sequence = 0U;
    const auto nonce = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("ror-ogre14-selected-texture-native-" + label + "-" +
         std::to_string(nonce) + "-" + std::to_string(++sequence) + ".zip");
    const std::vector<std::uint8_t> archive = MakeStoredZip(entries);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    Require(stream.good(), "could not create ordinary ZIP fixture");
    stream.write(
        reinterpret_cast<const char*>(archive.data()),
        static_cast<std::streamsize>(archive.size()));
    Require(stream.good(), "could not write ordinary ZIP fixture");
    return path;
}

class SelectedSourceTestTexture final : public Ogre::Texture
{
public:
    SelectedSourceTestTexture(
        Ogre::ResourceManager* creator,
        const Ogre::String& name,
        Ogre::ResourceHandle handle,
        const Ogre::String& group,
        bool is_manual,
        Ogre::ManualResourceLoader* loader)
        : Ogre::Texture(
              creator, name, handle, group, is_manual, loader)
    {
    }

    const std::string& observed_bytes() const noexcept
    {
        return m_observed_bytes;
    }

    void FailAfterNextSelectedStream() noexcept
    {
        m_fail_after_next_selected_stream = true;
    }

protected:
    void prepareImpl() override {}
    void unprepareImpl() override {}
    void loadImpl() override
    {
        Ogre::DataStreamPtr stream =
            Ogre::ResourceGroupManager::getSingleton().openResource(
                this->getName(), this->getGroup(), this, true);
        Require(stream != nullptr,
                "selected-source test texture received no stream");
        m_observed_bytes = stream->getAsString();
        if (m_fail_after_next_selected_stream)
        {
            m_fail_after_next_selected_stream = false;
            throw std::runtime_error(
                "intentional failure after selected stream delivery");
        }
    }
    void unloadImpl() override {}
    void createInternalResourcesImpl() override {}
    void freeInternalResourcesImpl() override {}

private:
    std::string m_observed_bytes;
    bool m_fail_after_next_selected_stream = false;
};

class SelectedSourceTestTextureManager final : public Ogre::TextureManager
{
public:
    Ogre::PixelFormat getNativeFormat(
        Ogre::TextureType,
        Ogre::PixelFormat format,
        int) override
    {
        return format;
    }

protected:
    Ogre::Resource* createImpl(
        const Ogre::String& name,
        Ogre::ResourceHandle handle,
        const Ogre::String& group,
        bool is_manual,
        Ogre::ManualResourceLoader* loader,
        const Ogre::NameValuePairList*) override
    {
        return OGRE_NEW SelectedSourceTestTexture(
            this, name, handle, group, is_manual, loader);
    }
};

std::vector<std::uint8_t> MakeZip64CountEnvelope(std::uint64_t entry_count)
{
    std::vector<std::uint8_t> bytes;
    AppendU32(bytes, UINT32_C(0x06064b50));
    AppendU64(bytes, 44U);
    AppendU16(bytes, 45U);
    AppendU16(bytes, 45U);
    AppendU32(bytes, 0U);
    AppendU32(bytes, 0U);
    AppendU64(bytes, entry_count);
    AppendU64(bytes, entry_count);
    AppendU64(bytes, 0U);
    AppendU64(bytes, 0U);
    AppendU32(bytes, UINT32_C(0x07064b50));
    AppendU32(bytes, 0U);
    AppendU64(bytes, 0U);
    AppendU32(bytes, 1U);
    AppendU32(bytes, UINT32_C(0x06054b50));
    AppendU16(bytes, 0U);
    AppendU16(bytes, 0U);
    AppendU16(bytes, UINT16_MAX);
    AppendU16(bytes, UINT16_MAX);
    AppendU32(bytes, 0U);
    AppendU32(bytes, 0U);
    AppendU16(bytes, 0U);
    return bytes;
}

std::string Sha256(const std::vector<std::uint8_t>& bytes)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0U;
    Require(EVP_Digest(
                bytes.data(), bytes.size(), digest.data(), &digest_size,
                EVP_sha256(), nullptr) == 1 &&
                digest_size == 32U,
            "could not hash ZIP fixture");
    static constexpr char HEX[] = "0123456789abcdef";
    std::string output(64U, '0');
    for (std::size_t index = 0U; index < 32U; ++index)
    {
        output[index * 2U] = HEX[digest[index] >> 4U];
        output[index * 2U + 1U] = HEX[digest[index] & 0x0fU];
    }
    return output;
}

RoR::TerrainBundleAuthenticatedArchiveSnapshot MakeRawSnapshot(
    const std::vector<std::uint8_t>& archive,
    const std::string& label)
{
    static std::uint64_t sequence = 0U;
    const auto nonce = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("ror-ogre14-authenticated-material-native-" + label + "-" +
         std::to_string(nonce) + "-" + std::to_string(++sequence) + ".zip");
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        Require(stream.good(), "could not create ZIP fixture");
        stream.write(
            reinterpret_cast<const char*>(archive.data()),
            static_cast<std::streamsize>(archive.size()));
        Require(stream.good(), "could not write ZIP fixture");
    }

    RoR::TerrainBundleAuthenticatedArchiveSnapshot snapshot;
    std::string observed;
    std::string error;
    const bool loaded = RoR::LoadAndVerifyTerrainBundleArchiveSnapshot(
        path.string(), Sha256(archive),
        RoR::TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_BYTES,
        snapshot, observed, error);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    Require(loaded, "could not authenticate ZIP fixture: " + error);
    Require(snapshot.initialized() && !std::filesystem::exists(path),
            "authenticated snapshot did not outlive its deleted source file");
    return snapshot;
}

RoR::TerrainBundleAuthenticatedArchiveSnapshot MakeSnapshot(
    const ArchiveEntries& entries,
    const std::string& label)
{
    return MakeRawSnapshot(MakeStoredZip(entries), label);
}

void InstallListeners(RoR::ContentManager& content)
{
    RoR::ContentManagerNativeIntegrationTestAccess::InstallListeners(content);
    Ogre::ResourceGroupManager::getSingleton().setLoadingListener(&content);
    Ogre::ScriptCompilerManager::getSingleton().setListener(&content);
}

Ogre::MaterialPtr FindMaterial(
    const Ogre::String& group,
    const Ogre::String& name)
{
    Ogre::MaterialManager* manager = Ogre::MaterialManager::getSingletonPtr();
    Require(manager != nullptr, "OGRE MaterialManager is unavailable");
    return manager->getByName(name, group);
}

std::size_t CountNativeArchives()
{
    Ogre::ArchiveManager::ArchiveMapIterator archives =
        Ogre::ArchiveManager::getSingleton().getArchiveIterator();
    std::size_t count = 0U;
    while (archives.hasMoreElements())
    {
        archives.getNext();
        ++count;
    }
    return count;
}

class CountingArchiveMountFault final
    : public RoR::Render::IOgre14AuthenticatedArchiveMountFaultInjector
{
public:
    void BeforeAuthenticatedArchiveMountStage(
        RoR::Render::Ogre14AuthenticatedArchiveMountStage) override
    {
        ++callback_count;
    }

    std::size_t callback_count = 0U;
};

void ExpectPreMountZipAdmissionRejection(
    RoR::ContentManager& content,
    const Ogre::String& group,
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot& snapshot)
{
    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    Require(!groups.resourceGroupExists(group),
            "pre-mount rejection group already exists");
    groups.createResourceGroup(group);
    const std::size_t archive_count_before = CountNativeArchives();
    const std::size_t location_count_before =
        groups.getResourceLocationList(group).size();
    CountingArchiveMountFault registration_counter;
    bool rejected = false;
    try
    {
        content.MountAuthenticatedPackageResourceLocation(
            group, snapshot, &registration_counter);
    }
    catch (...)
    {
        rejected = true;
    }
    Require(
        rejected && registration_counter.callback_count == 0U &&
            CountNativeArchives() == archive_count_before &&
            groups.getResourceLocationList(group).size() ==
                location_count_before &&
            !content.IsAuthenticatedPackageSourceMounted(
                group, snapshot.source_archive_identity()),
        "invalid ZIP reached EmbeddedZip, ArchiveManager, or resource-location "
        "publication before rejection");
    groups.destroyResourceGroup(group);
    Require(!groups.resourceGroupExists(group),
            "pre-mount rejection group survived native destruction");
}

std::string SelectedTextureReceiptBytes(
    const RoR::Render::Ogre14SelectedTextureSourceResolution& resolution)
{
    const auto* receipt = resolution.source_receipt();
    Require(receipt != nullptr && receipt->source_bytes() != nullptr &&
                receipt->source_size() != 0U,
            "selected texture resolution has no immutable source bytes");
    return std::string(
        reinterpret_cast<const char*>(receipt->source_bytes()),
        receipt->source_size());
}

void TestOrdinarySelectedTextureScope(
    RoR::ContentManager& content,
    SelectedSourceTestTextureManager& texture_manager)
{
    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    const Ogre::String member = "textures/scope.dds";

    const Ogre::String unregistered_group =
        "NativeUnregisteredSelectedTexture";
    const std::string unregistered_payload =
        MakeOrdinaryDds(12U, 34U, 56U);
    const std::filesystem::path unregistered_archive = WriteOrdinaryZip(
        {{member, unregistered_payload}}, "unregistered");
    groups.createResourceGroup(unregistered_group, false);
    groups.addResourceLocation(
        unregistered_archive.string(), "Zip", unregistered_group,
        false, true);
    Ogre::TexturePtr unregistered_texture =
        texture_manager.create(member, unregistered_group);
    unregistered_texture->load();
    auto* unregistered_native =
        dynamic_cast<SelectedSourceTestTexture*>(
            unregistered_texture.get());
    RoR::Render::Ogre14SelectedTextureSourceResolution
        unregistered_resolution;
    const RoR::Render::ValidationResult unregistered_resolved =
        content.ResolveSelectedTextureSource(
            *unregistered_texture, unregistered_resolution);
    Require(unregistered_native != nullptr &&
                unregistered_texture->isLoaded() &&
                unregistered_native->observed_bytes() ==
                    unregistered_payload &&
                !unregistered_resolved &&
                unregistered_resolved.code ==
                    RoR::Render::ValidationCode::MISSING_REFERENCE &&
                unregistered_resolved.field ==
                    "selected_texture_resolution.package_marker" &&
                !unregistered_resolution.initialized(),
            "unregistered texture did not load normally without selected-source authority");
    Require(
        RoR::ContentManagerNativeIntegrationTestAccess::
            InstallAuthenticatedNameMapOnly(content, unregistered_group),
        "could not stage authenticated map without an ordinary package marker");
    RoR::Render::Ogre14SelectedTextureSourceResolution
        authenticated_without_marker_resolution;
    const RoR::Render::ValidationResult authenticated_without_marker =
        content.ResolveSelectedTextureSource(
            *unregistered_texture,
            authenticated_without_marker_resolution);
    Require(!authenticated_without_marker &&
                authenticated_without_marker.code ==
                    RoR::Render::ValidationCode::INVALID_ASSET_REFERENCE &&
                authenticated_without_marker.field ==
                    "selected_texture_resolution.source_mode" &&
                !authenticated_without_marker_resolution.initialized(),
            "authenticated-map inconsistency was flattened into honest "
            "missing-package matte");
    RoR::ContentManagerNativeIntegrationTestAccess::
        RemoveAuthenticatedNameMapOnly(content, unregistered_group);
    groups.removeResourceLocation(
        unregistered_archive.string(), unregistered_group);
    unregistered_texture.setNull();
    groups.destroyResourceGroup(unregistered_group);
    std::error_code remove_error;
    Require(std::filesystem::remove(
                unregistered_archive, remove_error) &&
                !remove_error,
            "unregistered selected-texture fixture was not removed");

    const Ogre::String shared_group = "NativeSharedSelectedTexture";
    const std::string shared_payload =
        MakeOrdinaryDds(76U, 98U, 120U);
    const std::filesystem::path marker_archive = WriteOrdinaryZip(
        {{"package-marker.txt", "ordinary package marker"}}, "marker");
    const std::filesystem::path shared_archive = WriteOrdinaryZip(
        {{member, shared_payload}}, "shared");
    groups.createResourceGroup(shared_group, false);
    groups.addResourceLocation(
        marker_archive.string(), "Zip", shared_group, false, true);
    groups.addResourceLocation(
        shared_archive.string(), "Zip", shared_group, false, true);
    content.RegisterPackageResourceLocation(
        shared_group, marker_archive.string());
    Ogre::TexturePtr shared_texture =
        texture_manager.create(member, shared_group);
    shared_texture->load();
    auto* shared_native =
        dynamic_cast<SelectedSourceTestTexture*>(shared_texture.get());
    RoR::Render::Ogre14SelectedTextureSourceResolution shared_resolution;
    const RoR::Render::ValidationResult shared_resolved =
        content.ResolveSelectedTextureSource(
            *shared_texture, shared_resolution);
    const auto* shared_receipt = shared_resolution.source_receipt();
    const auto* shared_metadata =
        shared_receipt != nullptr ? shared_receipt->metadata() : nullptr;
    Require(shared_native != nullptr && shared_texture->isLoaded() &&
                shared_native->observed_bytes() == shared_payload &&
                shared_resolved && shared_metadata != nullptr &&
                shared_metadata->source.selected_archive_name ==
                    shared_archive.string() &&
                shared_metadata->source.selected_archive_name !=
                    marker_archive.string() &&
                shared_metadata->source.exact_member_name == member &&
                SelectedTextureReceiptBytes(shared_resolution) ==
                    shared_payload,
            "registered package marker did not capture the exact distinct attached archive");
    Require(
        RoR::ContentManagerNativeIntegrationTestAccess::
            InstallAuthenticatedNameMapOnly(content, shared_group),
        "could not stage a post-commit authenticated-map inconsistency");
    RoR::Render::Ogre14SelectedTextureSourceResolution blocked_resolution;
    const RoR::Render::ValidationResult blocked_resolve =
        content.ResolveSelectedTextureSource(
            *shared_texture, blocked_resolution);
    Require(!content.RevalidateSelectedTextureSource(
                *shared_texture, shared_resolution) &&
                !blocked_resolve && !blocked_resolution.initialized(),
            "post-commit authenticated-map inconsistency retained or minted ordinary authority");
    RoR::ContentManagerNativeIntegrationTestAccess::
        RemoveAuthenticatedNameMapOnly(content, shared_group);
    Require(content.RevalidateSelectedTextureSource(
                *shared_texture, shared_resolution),
            "restored ordinary source mode did not revalidate its unchanged exact receipt");
    content.UnregisterPackageResourceGroup(shared_group);
    groups.removeResourceLocation(
        shared_archive.string(), shared_group);
    groups.removeResourceLocation(
        marker_archive.string(), shared_group);
    shared_texture.setNull();
    groups.destroyResourceGroup(shared_group);
    remove_error.clear();
    const bool removed_shared =
        std::filesystem::remove(shared_archive, remove_error) &&
        !remove_error;
    remove_error.clear();
    const bool removed_marker =
        std::filesystem::remove(marker_archive, remove_error) &&
        !remove_error;
    Require(removed_shared && removed_marker,
            "shared selected-texture fixtures were not removed");

    const Ogre::String inconsistent_group =
        "NativeInconsistentSelectedTexture";
    const std::string inconsistent_payload =
        MakeOrdinaryDds(132U, 154U, 176U);
    const std::filesystem::path inconsistent_archive = WriteOrdinaryZip(
        {{member, inconsistent_payload}}, "inconsistent-auth-map");
    groups.createResourceGroup(inconsistent_group, false);
    groups.addResourceLocation(
        inconsistent_archive.string(), "Zip", inconsistent_group,
        false, true);
    content.RegisterPackageResourceLocation(
        inconsistent_group, inconsistent_archive.string());
    Require(
        RoR::ContentManagerNativeIntegrationTestAccess::
            InstallAuthenticatedNameMapOnly(content, inconsistent_group),
        "could not stage the one-map authenticated inconsistency");
    Ogre::TexturePtr inconsistent_texture =
        texture_manager.create(member, inconsistent_group);
    inconsistent_texture->load();
    auto* inconsistent_native =
        dynamic_cast<SelectedSourceTestTexture*>(
            inconsistent_texture.get());
    RoR::Render::Ogre14SelectedTextureSourceResolution
        inconsistent_resolution;
    const RoR::Render::ValidationResult inconsistent_resolved =
        content.ResolveSelectedTextureSource(
            *inconsistent_texture, inconsistent_resolution);
    Require(inconsistent_native != nullptr &&
                inconsistent_texture->isLoaded() &&
                inconsistent_native->observed_bytes() ==
                    inconsistent_payload &&
                !inconsistent_resolved &&
                !inconsistent_resolution.initialized(),
            "one-map authenticated inconsistency was laundered into ordinary selected-source authority");
    RoR::ContentManagerNativeIntegrationTestAccess::
        RemoveAuthenticatedNameMapOnly(content, inconsistent_group);
    content.UnregisterPackageResourceGroup(inconsistent_group);
    groups.removeResourceLocation(
        inconsistent_archive.string(), inconsistent_group);
    inconsistent_texture.setNull();
    groups.destroyResourceGroup(inconsistent_group);
    remove_error.clear();
    Require(std::filesystem::remove(
                inconsistent_archive, remove_error) &&
                !remove_error,
            "inconsistent selected-texture fixture was not removed");
}

void TestOrdinarySelectedTextureLifecycle(
    RoR::ContentManager& content,
    SelectedSourceTestTextureManager& texture_manager)
{
    const Ogre::String group = "NativeOrdinarySelectedTexture";
    const Ogre::String member = "textures/ordinary.dds";
    const std::string first_payload = MakeOrdinaryDds(220U, 40U, 25U);
    const std::string second_payload = MakeOrdinaryDds(20U, 80U, 230U);
    const std::filesystem::path first_archive = WriteOrdinaryZip(
        {{member, first_payload}}, "first");
    std::filesystem::path second_archive;

    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    Require(!groups.resourceGroupExists(group),
            "ordinary selected-texture group already exists");
    groups.createResourceGroup(group, false);
    groups.addResourceLocation(
        first_archive.string(), "Zip", group, false, true);
    content.RegisterPackageResourceLocation(
        group, first_archive.string());

    Ogre::TexturePtr texture = texture_manager.create(member, group);
    Require(texture != nullptr && !texture->isLoaded(),
            "ordinary selected-texture resource was not created unloaded");
    auto* native_texture =
        dynamic_cast<SelectedSourceTestTexture*>(texture.get());
    Require(native_texture != nullptr,
            "ordinary texture manager returned the wrong native resource");
    native_texture->FailAfterNextSelectedStream();
    bool first_load_failed = false;
    try
    {
        texture->load();
    }
    catch (...)
    {
        first_load_failed = true;
    }
    RoR::Render::Ogre14SelectedTextureSourceReceipt failed_receipt;
    const RoR::Render::ValidationResult failed_receipt_found =
        RoR::ContentManagerNativeIntegrationTestAccess::
            FindSelectedTextureSourceReceipt(
                content, *texture, failed_receipt);
    const auto* failed_metadata = failed_receipt.metadata();
    Require(first_load_failed && !texture->isLoaded() &&
                failed_receipt_found && failed_metadata != nullptr &&
                failed_metadata->source.opened_stream_pointer_token != 0U &&
                failed_receipt.ReplacementBytesMatch(
                    first_payload.data(), first_payload.size()),
            "failed native load did not retain its exact post-open receipt");
    const std::uint64_t failed_preload_state =
        failed_metadata->source.resource_state_count_before_load;

    texture->load();
    Require(texture->isLoaded() &&
                native_texture->observed_bytes() == first_payload,
            "ordinary texture retry did not consume the selected replacement bytes");

    RoR::Render::Ogre14SelectedTextureSourceResolution first_resolution;
    const RoR::Render::ValidationResult first_resolved =
        content.ResolveSelectedTextureSource(
            *texture, first_resolution);
    const auto* first_receipt = first_resolution.source_receipt();
    const auto* first_metadata =
        first_receipt != nullptr ? first_receipt->metadata() : nullptr;
    const std::vector<std::uint8_t> first_bytes(
        first_payload.begin(), first_payload.end());
    Require(first_resolved && first_resolution.initialized() &&
                first_metadata != nullptr &&
                first_metadata->source.source_kind ==
                    RoR::Render::Ogre14SelectedTextureSourceKind::
                        UNAUTHENTICATED_PACKAGE_ARCHIVE_MEMBER &&
                first_metadata->source.effective_resource_group == group &&
                first_metadata->source.exact_resource_name == member &&
                first_metadata->source.exact_member_name == member &&
                first_metadata->source.selected_archive_name ==
                    first_archive.string() &&
                first_metadata->source.selected_archive_type == "Zip" &&
                first_metadata->source.opened_stream_pointer_token != 0U &&
                !first_receipt->SharesImmutableStateWith(failed_receipt) &&
                first_metadata->source.resource_state_count_before_load ==
                    failed_preload_state &&
                first_metadata->byte_count == first_payload.size() &&
                first_metadata->observed_bytes_sha256 ==
                    Sha256(first_bytes) &&
                SelectedTextureReceiptBytes(first_resolution) ==
                    first_payload &&
                content.RevalidateSelectedTextureSource(
                    *texture, first_resolution),
            "ordinary selected-stream retry did not refresh exact ZIP/DDS stream attribution");

    groups.removeResourceLocation(first_archive.string(), group);
    std::error_code remove_error;
    Require(std::filesystem::remove(first_archive, remove_error) &&
                !remove_error &&
                texture->isLoaded() &&
                native_texture->observed_bytes() == first_payload &&
                SelectedTextureReceiptBytes(first_resolution) ==
                    first_payload &&
                content.RevalidateSelectedTextureSource(
                    *texture, first_resolution),
            "selected texture receipt did not outlive deleted archive bytes");

    texture->unload();
    Require(!content.RevalidateSelectedTextureSource(
                *texture, first_resolution),
            "unloaded texture retained a live selected-source resolution");
    second_archive = WriteOrdinaryZip(
        {{member, second_payload}}, "reload");
    groups.addResourceLocation(
        second_archive.string(), "Zip", group, false, true);
    content.RegisterPackageResourceLocation(
        group, second_archive.string());
    Require(!content.RevalidateSelectedTextureSource(
                *texture, first_resolution),
            "group reload did not revoke the prior selected-source snapshot");

    texture->load();
    Require(texture->isLoaded() &&
                native_texture->observed_bytes() == second_payload,
            "reloaded texture did not consume the replacement archive bytes");
    RoR::Render::Ogre14SelectedTextureSourceResolution second_resolution;
    const RoR::Render::ValidationResult second_resolved =
        content.ResolveSelectedTextureSource(
            *texture, second_resolution);
    Require(second_resolved && second_resolution.initialized() &&
                SelectedTextureReceiptBytes(second_resolution) ==
                    second_payload &&
                content.RevalidateSelectedTextureSource(
                    *texture, second_resolution) &&
                !second_resolution.SharesLoadedResourceAuthorityWith(
                    first_resolution),
            "ordinary texture reload reused stale selected-source authority");

    content.UnregisterPackageResourceGroup(group);
    Require(!content.RevalidateSelectedTextureSource(
                *texture, second_resolution) &&
                SelectedTextureReceiptBytes(first_resolution) ==
                    first_payload &&
                SelectedTextureReceiptBytes(second_resolution) ==
                    second_payload,
            "ordinary group teardown retained live authority or lost immutable receipts");
    groups.destroyResourceGroup(group);
    texture.setNull();
    remove_error.clear();
    Require(std::filesystem::remove(second_archive, remove_error) &&
                !remove_error && !groups.resourceGroupExists(group),
            "ordinary selected-texture teardown left its ZIP or group live");
}

std::string ReceiptBytes(
    const RoR::Render::Ogre14AuthenticatedMaterialScriptReceipt& receipt,
    std::size_t index,
    bool effective)
{
    const std::uint8_t* bytes = effective
        ? receipt.effective_bytes_at(index)
        : receipt.original_bytes_at(index);
    const std::size_t size = effective
        ? receipt.effective_size_at(index)
        : receipt.original_size_at(index);
    Require(bytes != nullptr || size == 0U,
            "receipt exposed an invalid immutable byte span");
    return std::string(
        reinterpret_cast<const char*>(bytes),
        reinterpret_cast<const char*>(bytes) + size);
}

void DestroyAndUnregister(
    RoR::ContentManager& content,
    const Ogre::String& group)
{
    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    content.AbortAuthenticatedMaterialScriptGroup(group);
    if (groups.resourceGroupExists(group))
    {
        groups.destroyResourceGroup(group);
    }
    Require(!groups.resourceGroupExists(group),
            "resource group survived native destruction");
    content.UnregisterPackageResourceGroup(group);
}

struct LoadedGeneration final
{
    Ogre::MaterialPtr first;
    Ogre::MaterialPtr second;
    RoR::Render::Ogre14AuthenticatedMaterialScriptResolution first_resolution;
    RoR::Render::Ogre14AuthenticatedMaterialScriptResolution second_resolution;
};

LoadedGeneration LoadSuccessfulGeneration(
    RoR::ContentManager& content,
    const Ogre::String& group,
    const RoR::TerrainBundleAuthenticatedArchiveSnapshot& snapshot,
    const ArchiveEntries& expected_entries)
{
    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    Require(!groups.resourceGroupExists(group),
            "successful fixture group already exists");
    groups.createResourceGroup(group);
    content.MountAuthenticatedPackageResourceLocation(group, snapshot);
    Require(content.IsAuthenticatedPackageSourceMounted(
                group, snapshot.source_archive_identity()),
            "authenticated snapshot was not mounted by source identity");
    groups.initialiseResourceGroup(group);

    LoadedGeneration loaded;
    loaded.first = FindMaterial(group, "NativeAcceptedA");
    loaded.second = FindMaterial(group, "NativeAcceptedB");
    Require(loaded.first && loaded.second &&
                loaded.first.get() != loaded.second.get(),
            "whole-group parse did not create both native materials");
    const RoR::Render::ValidationResult first_resolved =
        content.ResolveAuthenticatedMaterialScript(
            *loaded.first, loaded.first_resolution);
    const RoR::Render::ValidationResult second_resolved =
        content.ResolveAuthenticatedMaterialScript(
            *loaded.second, loaded.second_resolution);
    Require(first_resolved && second_resolved &&
                loaded.first_resolution.initialized() &&
                loaded.second_resolution.initialized(),
            "whole-group publication did not resolve both native materials");
    Require(content.RevalidateAuthenticatedMaterialScript(
                *loaded.first, loaded.first_resolution) &&
                content.RevalidateAuthenticatedMaterialScript(
                    *loaded.second, loaded.second_resolution),
            "fresh native material resolution did not revalidate");

    const auto* first_receipt = loaded.first_resolution.receipt();
    const auto* second_receipt = loaded.second_resolution.receipt();
    Require(first_receipt != nullptr && second_receipt != nullptr &&
                first_receipt->source_count() == expected_entries.size() &&
                second_receipt->source_count() == expected_entries.size() &&
                first_receipt->SharesSourceStateWith(*second_receipt),
            "native materials did not share one complete parse closure");

    std::map<std::string, std::string> expected;
    for (const auto& entry : expected_entries)
    {
        expected.emplace(entry.first, entry.second);
    }
    std::map<std::string, std::size_t> observed;
    for (std::size_t index = 0U; index < first_receipt->source_count(); ++index)
    {
        const auto* metadata = first_receipt->source_metadata_at(index);
        Require(metadata != nullptr,
                "receipt source metadata is unexpectedly absent");
        const auto expected_source = expected.find(metadata->exact_member_name);
        Require(expected_source != expected.end(),
                "receipt contains an unexpected archive member");
        const std::string original = ReceiptBytes(*first_receipt, index, false);
        const std::string effective = ReceiptBytes(*first_receipt, index, true);
        Require(original == expected_source->second &&
                    effective == expected_source->second &&
                    metadata->original_byte_count == original.size() &&
                    metadata->effective_byte_count == effective.size() &&
                    metadata->archive_sha256 == snapshot.archive_sha256(),
                "post-open receipt bytes or source authority changed");
        const auto* source_snapshot =
            first_receipt->authenticated_archive_snapshot_at(index);
        Require(source_snapshot != nullptr &&
                    source_snapshot->SharesImmutableStateWith(snapshot),
                "receipt lost the exact authenticated archive owner");
        ++observed[metadata->exact_member_name];
    }
    Require(observed.size() == expected.size(),
            "receipt omitted part of the root/import closure");
    for (const auto& entry : observed)
    {
        Require(entry.second == 1U,
                "repeated import minted duplicate source authority");
    }
    return loaded;
}

void ExpectRejectedGeneration(
    RoR::ContentManager& content,
    const Ogre::String& group,
    const ArchiveEntries& entries,
    const std::string& label,
    const Ogre::String& forbidden_material)
{
    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    const auto snapshot = MakeSnapshot(entries, label);
    groups.createResourceGroup(group);
    content.MountAuthenticatedPackageResourceLocation(group, snapshot);
    bool rejected = false;
    try
    {
        groups.initialiseResourceGroup(group);
    }
    catch (...)
    {
        rejected = true;
    }
    Require(rejected,
            "hostile material-script generation was not rejected: " + label);
    Require(!FindMaterial(group, forbidden_material),
            "rejected generation leaked its fallback material: " + label);
    DestroyAndUnregister(content, group);
}

ArchiveEntries SuccessfulEntries(unsigned int generation)
{
    const std::string marker = "// generation " +
        std::to_string(generation) + "\n";
    return {
        {"root.material",
         marker +
             "import * from \"includes/level1.inc\"\n"
             "import * from \"includes/level1.inc\"\n"
             "material NativeAcceptedA : NativeAbstractLevel1 {}\n"
             "material NativeAcceptedB : NativeAbstractLevel1 {}\n"},
        {"includes/level1.inc",
         "import * from \"includes/level2.inc\"\n"
         "abstract material NativeAbstractLevel1 : "
         "NativeAbstractLevel2 {}\n"},
        {"includes/level2.inc",
         "abstract material NativeAbstractLevel2\n"
         "{\n"
         "    technique { pass { diffuse 0.25 0.5 0.75 1 } }\n"
         "}\n"},
    };
}

ArchiveEntries AttemptLimitEntries()
{
    ArchiveEntries entries;
    entries.reserve(AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT + 1U);
    std::string root;
    for (std::size_t index = 0U;
         index < AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT;
         ++index)
    {
        const std::string suffix = std::to_string(index);
        const std::string member = "attempts/dependency-" + suffix + ".inc";
        root += "import * from \"" + member + "\"\n";
        entries.emplace_back(
            member,
            "abstract material NativeAttempt" + suffix + " {}\n");
    }
    root += "material NativeAttemptMustNotPublish {}\n";
    entries.insert(entries.begin(), {"root.material", std::move(root)});
    return entries;
}

ArchiveEntries DeepestSafeEntries()
{
    ArchiveEntries entries;
    entries.reserve(AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT);
    entries.emplace_back(
        "root.material",
        "import * from \"depth/dependency-0.inc\"\n"
        "material NativeAcceptedA {}\n"
        "material NativeAcceptedB {}\n");
    for (std::size_t index = 0U;
         index + 1U < AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT;
         ++index)
    {
        std::string payload;
        if (index + 2U < AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT)
        {
            payload =
                "import * from \"depth/dependency-" +
                std::to_string(index + 1U) + ".inc\"\n";
        }
        payload += "abstract material NativeDepth" +
            std::to_string(index) + " {}\n";
        entries.emplace_back(
            "depth/dependency-" + std::to_string(index) + ".inc",
            std::move(payload));
    }
    Require(entries.size() == AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT,
            "deepest-safe fixture has the wrong exact open count");
    return entries;
}

void TestSuccessfulLifecycleAndReload(RoR::ContentManager& content)
{
    const Ogre::String group = "NativeAuthenticatedMaterialSuccess";
    const ArchiveEntries first_entries = SuccessfulEntries(1U);
    const auto first_snapshot = MakeSnapshot(first_entries, "success-1");
    LoadedGeneration first = LoadSuccessfulGeneration(
        content, group, first_snapshot, first_entries);

    DestroyAndUnregister(content, group);
    Require(!content.IsAuthenticatedPackageSourceMounted(
                group, first_snapshot.source_archive_identity()),
            "teardown retained authenticated archive source authority");
    Require(!content.RevalidateAuthenticatedMaterialScript(
                *first.first, first.first_resolution) &&
                !content.RevalidateAuthenticatedMaterialScript(
                    *first.second, first.second_resolution),
            "teardown did not revoke native material resolutions");

    const ArchiveEntries second_entries = SuccessfulEntries(2U);
    const auto second_snapshot = MakeSnapshot(second_entries, "success-2");
    LoadedGeneration second = LoadSuccessfulGeneration(
        content, group, second_snapshot, second_entries);
    Require(!content.RevalidateAuthenticatedMaterialScript(
                *first.first, first.first_resolution) &&
                !first.first_resolution.SharesCurrentAuthorityWith(
                    second.first_resolution),
            "reload admitted a stale previous-generation resolution");
    Require(content.RevalidateAuthenticatedMaterialScript(
                *second.first, second.first_resolution),
            "reload did not publish fresh native material authority");
    DestroyAndUnregister(content, group);
    Require(!content.RevalidateAuthenticatedMaterialScript(
                *second.first, second.first_resolution),
            "final teardown retained fresh native material authority");
}

void TestMissingImportRejection(RoR::ContentManager& content)
{
    ExpectRejectedGeneration(
        content,
        "NativeAuthenticatedMaterialMissingImport",
        {{"root.material",
          "import * from \"missing/not-present.inc\"\n"
          "material NativeMissingMustNotPublish {}\n"}},
        "missing-import",
        "NativeMissingMustNotPublish");
}

void TestUndeliveredSourceEventRejection(RoR::ContentManager& content)
{
    RoR::ContentManagerNativeIntegrationTestAccess::
        ForceNextScriptSourceUndelivered(content);
    ExpectRejectedGeneration(
        content,
        "NativeAuthenticatedMaterialUndeliveredSource",
        {{"root.material", "material NativeUndeliveredMustNotPublish {}\n"}},
        "undelivered-source-event",
        "NativeUndeliveredMustNotPublish");
}

void TestEmptyMaterialNameRejection(RoR::ContentManager& content)
{
    RoR::ContentManagerNativeIntegrationTestAccess::
        ForceNextMaterialEventNameEmpty(content);
    ExpectRejectedGeneration(
        content,
        "NativeAuthenticatedMaterialEmptyName",
        {{"root.material",
          "material NativeAfterEmptyMustNotPublish {}\n"}},
        "empty-material-name",
        "NativeAfterEmptyMustNotPublish");
}

void TestDuplicateExistingNameRejection(RoR::ContentManager& content)
{
    const Ogre::String group = "NativeAuthenticatedMaterialDuplicateExisting";
    const Ogre::String material_name = "NativeDuplicateMustNotBeReused";
    const ArchiveEntries entries = {
        {"root.material", "material " + material_name + " {}\n"}};
    const auto snapshot = MakeSnapshot(entries, "duplicate-existing-name");
    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    Ogre::MaterialManager& materials = Ogre::MaterialManager::getSingleton();
    groups.createResourceGroup(group);
    content.MountAuthenticatedPackageResourceLocation(group, snapshot);
    Ogre::MaterialPtr existing = materials.create(material_name, group);
    Require(existing && existing->getNumTechniques() == 0U,
            "duplicate-name fixture could not create its foreign sentinel");
    const Ogre::ResourceHandle existing_handle = existing->getHandle();

    bool rejected = false;
    try
    {
        groups.initialiseResourceGroup(group);
    }
    catch (...)
    {
        rejected = true;
    }
    const Ogre::MaterialPtr after = FindMaterial(group, material_name);
    Require(rejected && after && after.get() == existing.get() &&
                after->getHandle() == existing_handle &&
                after->getNumTechniques() == 0U,
            "authenticated duplicate-name rejection reused or mutated the "
            "foreign sentinel material");
    DestroyAndUnregister(content, group);
}

void TestPreMountZipAdmissionRejection(RoR::ContentManager& content)
{
    const auto count_snapshot = MakeRawSnapshot(
        MakeZip64CountEnvelope(
            RoR::TERRAIN_BUNDLE_AUTHENTICATED_ARCHIVE_MAXIMUM_ENTRIES + 1U),
        "zip64-count-cap");
    ExpectPreMountZipAdmissionRejection(
        content,
        "NativeAuthenticatedZip64CountRejected",
        count_snapshot);

    const auto alias_snapshot = MakeSnapshot(
        {{"materials/A.material", "first"},
         {"materials/a.material", "other"}},
        "lookup-alias");
    ExpectPreMountZipAdmissionRejection(
        content,
        "NativeAuthenticatedLookupAliasRejected",
        alias_snapshot);

    std::vector<std::uint8_t> encrypted_bytes = MakeStoredZip(
        {{"root.material", "material NativeEncryptedMustNotMount {}\n"}});
    const std::size_t encrypted_central =
        ClassicCentralOffset(encrypted_bytes);
    PatchU16(encrypted_bytes, 6U, 1U);
    PatchU16(encrypted_bytes, encrypted_central + 8U, 1U);
    ExpectPreMountZipAdmissionRejection(
        content,
        "NativeAuthenticatedEncryptedZipRejected",
        MakeRawSnapshot(encrypted_bytes, "encrypted-member"));

    std::vector<std::uint8_t> zero_compressed_deflate_bytes =
        MakeStoredZip(
            {{"root.material",
              "material NativeZeroCompressedDeflateMustNotMount {}\n"}});
    const std::size_t zero_compressed_deflate_central =
        ClassicCentralOffset(zero_compressed_deflate_bytes);
    PatchU16(zero_compressed_deflate_bytes, 8U, 8U);
    PatchU16(
        zero_compressed_deflate_bytes,
        zero_compressed_deflate_central + 10U,
        8U);
    PatchU32(zero_compressed_deflate_bytes, 18U, 0U);
    PatchU32(
        zero_compressed_deflate_bytes,
        zero_compressed_deflate_central + 20U,
        0U);
    ExpectPreMountZipAdmissionRejection(
        content,
        "NativeAuthenticatedZeroCompressedDeflateRejected",
        MakeRawSnapshot(
            zero_compressed_deflate_bytes, "zero-compressed-deflate"));

    std::vector<std::uint8_t> attribute_directory_bytes =
        MakeStoredZip({{"attribute-directory", ""}});
    const std::size_t attribute_directory_central =
        ClassicCentralOffset(attribute_directory_bytes);
    PatchU32(
        attribute_directory_bytes,
        attribute_directory_central + 38U,
        0x10U);
    ExpectPreMountZipAdmissionRejection(
        content,
        "NativeAuthenticatedAttributeDirectoryRejected",
        MakeRawSnapshot(
            attribute_directory_bytes, "attribute-directory-without-slash"));
}

void TestNativeDirectoryIndexParity(RoR::ContentManager& content)
{
    const Ogre::String group = "NativeAuthenticatedDirectoryIndexParity";
    const auto snapshot = MakeSnapshot(
        {{"materials/", ""},
         {"materials/root.material", "material NativeDirectoryIndex {}\n"}},
        "native-directory-index");
    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    groups.createResourceGroup(group);
    content.MountAuthenticatedPackageResourceLocation(group, snapshot);
    Require(content.IsAuthenticatedPackageSourceMounted(
                group, snapshot.source_archive_identity()),
            "native file-plus-directory index did not match its preflight");
    DestroyAndUnregister(content, group);
}

void TestCyclicImportRejection(RoR::ContentManager& content)
{
    ExpectRejectedGeneration(
        content,
        "NativeAuthenticatedMaterialCycle",
        {{"root.material",
          "import * from \"cycle/a.inc\"\n"
          "material NativeCycleMustNotPublish {}\n"},
         {"cycle/a.inc",
          "import * from \"cycle/b.inc\"\n"
          "abstract material NativeCycleA {}\n"},
         {"cycle/b.inc",
          "import * from \"cycle/a.inc\"\n"
          "abstract material NativeCycleB {}\n"}},
        "cyclic-import",
        "NativeCycleMustNotPublish");
}

void TestDeepestSafeOpenClosure(RoR::ContentManager& content)
{
    const Ogre::String group = "NativeAuthenticatedMaterialDeepestSafe";
    const ArchiveEntries entries = DeepestSafeEntries();
    const auto snapshot = MakeSnapshot(entries, "deepest-safe");
    LoadedGeneration loaded =
        LoadSuccessfulGeneration(content, group, snapshot, entries);
    Require(loaded.first_resolution.receipt() != nullptr &&
                loaded.first_resolution.receipt()->source_count() ==
                    AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT,
            "deepest-safe parse did not publish exactly 128 source opens");
    DestroyAndUnregister(content, group);
}

void TestAttemptLimitRejection(RoR::ContentManager& content)
{
    ExpectRejectedGeneration(
        content,
        "NativeAuthenticatedMaterialAttemptLimit",
        AttemptLimitEntries(),
        "attempt-limit",
        "NativeAttemptMustNotPublish");
}

struct ChildProcessResult final
{
    bool launched = false;
    bool completed = false;
    bool timed_out = false;
    int exit_code = -1;
};

#if defined(_WIN32)
std::string QuoteWindowsArgument(const std::string& argument)
{
    std::string quoted(1U, '"');
    std::size_t backslash_count = 0U;
    for (const char value : argument)
    {
        if (value == '\\')
        {
            ++backslash_count;
            continue;
        }
        if (value == '"')
        {
            quoted.append(backslash_count * 2U + 1U, '\\');
            quoted.push_back('"');
            backslash_count = 0U;
            continue;
        }
        quoted.append(backslash_count, '\\');
        backslash_count = 0U;
        quoted.push_back(value);
    }
    quoted.append(backslash_count * 2U, '\\');
    quoted.push_back('"');
    return quoted;
}
#endif

ChildProcessResult RunCycleChildProcess(const std::string& executable)
{
    ChildProcessResult result;
#if defined(_WIN32)
    std::string command =
        QuoteWindowsArgument(executable) + " " +
        NATIVE_INTEGRATION_ARGUMENT + " " + CYCLE_CHILD_ARGUMENT;
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessA(
            executable.c_str(), mutable_command.data(), nullptr, nullptr,
            FALSE, 0U, nullptr, nullptr, &startup, &process) == FALSE)
    {
        return result;
    }
    result.launched = true;
    const DWORD wait_result = WaitForSingleObject(process.hProcess, 10000U);
    if (wait_result == WAIT_TIMEOUT)
    {
        result.timed_out = true;
        TerminateProcess(process.hProcess, 125U);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    else if (wait_result == WAIT_OBJECT_0)
    {
        DWORD child_exit_code = 0U;
        if (GetExitCodeProcess(process.hProcess, &child_exit_code) != FALSE)
        {
            result.completed = true;
            result.exit_code = static_cast<int>(child_exit_code);
        }
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    const pid_t child = fork();
    if (child < 0)
    {
        return result;
    }
    if (child == 0)
    {
        execl(
            executable.c_str(), executable.c_str(),
            NATIVE_INTEGRATION_ARGUMENT, CYCLE_CHILD_ARGUMENT,
            static_cast<char*>(nullptr));
        _exit(127);
    }
    result.launched = true;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child)
        {
            result.completed = true;
            if (WIFEXITED(status))
            {
                result.exit_code = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                result.exit_code = 128 + WTERMSIG(status);
            }
            return result;
        }
        if (waited < 0 && errno != EINTR)
        {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    result.timed_out = true;
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
    {
    }
#endif
    return result;
}

int RunCycleChild()
{
    try
    {
        Ogre::Root root("", "", "");
        RoR::ContentManager content;
        InstallListeners(content);
        TestCyclicImportRejection(content);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "cycle-child-failure: " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "cycle-child-failure: unknown exception\n";
    }
    return EXIT_FAILURE;
}

} // namespace

int RoR::RunOgre14AuthenticatedMaterialScriptNativeIntegrationTests(
    int argc,
    char** argv)
{
    if (argc == 3 && std::string(argv[1]) == NATIVE_INTEGRATION_ARGUMENT &&
        std::string(argv[2]) == CYCLE_CHILD_ARGUMENT)
    {
        return RunCycleChild();
    }
    try
    {
        Require(
            argc == 2 && argv != nullptr && argv[0] != nullptr &&
                argv[1] != nullptr &&
                std::string(argv[1]) == NATIVE_INTEGRATION_ARGUMENT,
            "native integration test requires its exact internal argument");
        Ogre::Root root("", "", "");
        Require(Ogre::ResourceGroupManager::getSingletonPtr() != nullptr &&
                    Ogre::ArchiveManager::getSingletonPtr() != nullptr &&
                    Ogre::ScriptCompilerManager::getSingletonPtr() != nullptr &&
                    Ogre::MaterialManager::getSingletonPtr() != nullptr &&
                    Ogre::TextureManager::getSingletonPtr() == nullptr,
                "pinned OGRE did not construct the required native managers");

        SelectedSourceTestTextureManager texture_manager;
        Require(Ogre::TextureManager::getSingletonPtr() == &texture_manager,
                "native selected-source TextureManager was not installed");
        RoR::ContentManager content;
        InstallListeners(content);
        TestOrdinarySelectedTextureScope(content, texture_manager);
        TestOrdinarySelectedTextureLifecycle(content, texture_manager);
        TestPreMountZipAdmissionRejection(content);
        TestNativeDirectoryIndexParity(content);
        TestSuccessfulLifecycleAndReload(content);
        TestMissingImportRejection(content);
        TestUndeliveredSourceEventRejection(content);
        TestEmptyMaterialNameRejection(content);
        TestDuplicateExistingNameRejection(content);
        TestDeepestSafeOpenClosure(content);
        TestAttemptLimitRejection(content);

        const std::string executable =
            std::filesystem::absolute(argv[0]).lexically_normal().string();
        const ChildProcessResult cycle = RunCycleChildProcess(executable);
        Require(cycle.launched && cycle.completed && !cycle.timed_out &&
                    cycle.exit_code == EXIT_SUCCESS,
                "cyclic import was not rejected inside the 10-second native "
                "boundary (child exit " +
                    std::to_string(cycle.exit_code) + ")");

        std::cout
            << "ogre14-authenticated-material-script-native-integration=ok\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "FAIL: unknown native integration failure\n";
    }
    return EXIT_FAILURE;
}
