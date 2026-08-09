/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#pragma once

#include <OgreArchive.h>
#include <OgreArchiveManager.h>
#include <OgreResourceGroupManager.h>

#include <cstddef>
#include <cstdint>

namespace RoR::Render {

/// Process-wide ResourceGroupManager closure for one live Archive pointer.
/// ArchiveManager is name-keyed and not reference counted, so removing a
/// location is safe only while the archive is exclusive to the target group.
struct Ogre14AuthenticatedArchiveLocationClosure final
{
    bool target_group_exists = false;
    std::size_t process_location_count = 0U;
    std::size_t target_location_count = 0U;

    [[nodiscard]] bool IsExclusivelyAttachedToTarget() const noexcept
    {
        return target_group_exists && process_location_count == 1U &&
            target_location_count == 1U;
    }

    [[nodiscard]] bool IsDetachedAfterTargetDestruction() const noexcept
    {
        return !target_group_exists && process_location_count == 0U &&
            target_location_count == 0U;
    }

    [[nodiscard]] bool IsAbsentFromAllLocations() const noexcept
    {
        return process_location_count == 0U && target_location_count == 0U;
    }
};

/// Captures exact pointer references across every current resource group.
/// This function intentionally never dereferences `expected_archive`; callers
/// must first prove it is the one current ArchiveManager entry before reading
/// any Archive fields.
inline Ogre14AuthenticatedArchiveLocationClosure
CaptureOgre14AuthenticatedArchiveLocationClosure(
    Ogre::ResourceGroupManager& manager,
    const Ogre::String& target_group,
    const Ogre::Archive* expected_archive)
{
    Ogre14AuthenticatedArchiveLocationClosure closure;
    closure.target_group_exists =
        manager.resourceGroupExists(target_group);
    const Ogre::StringVector groups = manager.getResourceGroups();
    for (const Ogre::String& group : groups)
    {
        if (!manager.resourceGroupExists(group))
        {
            continue;
        }
        const Ogre::ResourceGroupManager::LocationList& locations =
            manager.getResourceLocationList(group);
        for (const Ogre::ResourceGroupManager::ResourceLocation& location :
             locations)
        {
            if (location.archive == expected_archive)
            {
                ++closure.process_location_count;
                if (group == target_group)
                {
                    ++closure.target_location_count;
                }
            }
        }
    }
    return closure;
}

/// One name-selected live ArchiveManager entry plus its process-wide
/// ResourceGroupManager location closure. Capturing this proof never
/// dereferences a ResourceLocation's borrowed Archive pointer. Callers may
/// read Archive fields only after one of the authentication predicates below
/// succeeds.
struct Ogre14AuthenticatedArchiveAuthorityProof final
{
    Ogre::Archive* manager_archive = nullptr;
    std::size_t manager_name_count = 0U;
    Ogre14AuthenticatedArchiveLocationClosure location_closure;

    [[nodiscard]] bool AuthenticatesExclusive(
        const Ogre::Archive* expected_archive) const noexcept
    {
        return expected_archive != nullptr && manager_name_count == 1U &&
            manager_archive == expected_archive &&
            location_closure.IsExclusivelyAttachedToTarget();
    }

    [[nodiscard]] bool AuthenticatesDetached(
        const Ogre::Archive* expected_archive) const noexcept
    {
        return expected_archive != nullptr && manager_name_count == 1U &&
            manager_archive == expected_archive &&
            location_closure.IsDetachedAfterTargetDestruction();
    }
};

inline Ogre14AuthenticatedArchiveAuthorityProof
CaptureOgre14AuthenticatedArchiveAuthorityProof(
    Ogre::ArchiveManager& archive_manager,
    Ogre::ResourceGroupManager& resource_group_manager,
    const Ogre::String& target_group,
    const Ogre::String& expected_archive_name)
{
    Ogre14AuthenticatedArchiveAuthorityProof proof;
    Ogre::ArchiveManager::ArchiveMapIterator archives =
        archive_manager.getArchiveIterator();
    while (archives.hasMoreElements())
    {
        const Ogre::String archive_name = archives.peekNextKey();
        Ogre::Archive* archive = archives.getNext();
        if (archive_name == expected_archive_name)
        {
            proof.manager_archive = archive;
            ++proof.manager_name_count;
        }
    }
    if (proof.manager_name_count == 1U && proof.manager_archive != nullptr)
    {
        proof.location_closure =
            CaptureOgre14AuthenticatedArchiveLocationClosure(
                resource_group_manager,
                target_group,
                proof.manager_archive);
    }
    return proof;
}

/// Process-global retained manifest accounting. The values describe only
/// published archive authority; a provisional mount is charged atomically
/// with its pointer-bound archive map and never during rollback.
struct Ogre14AuthenticatedArchiveManifestAccounting final
{
    std::size_t live_binding_count = 0U;
    std::size_t live_member_count = 0U;
    std::uint64_t retained_member_identity_bytes = 0U;
};

inline bool TryAdmitOgre14AuthenticatedArchiveManifest(
    const Ogre14AuthenticatedArchiveManifestAccounting& current,
    std::size_t additional_members,
    std::uint64_t additional_identity_bytes,
    std::size_t maximum_bindings,
    std::size_t maximum_members,
    std::uint64_t maximum_identity_bytes,
    Ogre14AuthenticatedArchiveManifestAccounting& output) noexcept
{
    if (maximum_bindings == 0U || maximum_members == 0U ||
        maximum_identity_bytes == 0U ||
        current.live_binding_count >= maximum_bindings ||
        additional_members > maximum_members ||
        current.live_member_count > maximum_members - additional_members ||
        additional_identity_bytes > maximum_identity_bytes ||
        current.retained_member_identity_bytes >
            maximum_identity_bytes - additional_identity_bytes)
    {
        return false;
    }
    output = current;
    ++output.live_binding_count;
    output.live_member_count += additional_members;
    output.retained_member_identity_bytes += additional_identity_bytes;
    return true;
}

inline bool TryReleaseOgre14AuthenticatedArchiveManifest(
    const Ogre14AuthenticatedArchiveManifestAccounting& current,
    std::size_t removed_members,
    std::uint64_t removed_identity_bytes,
    Ogre14AuthenticatedArchiveManifestAccounting& output) noexcept
{
    if (current.live_binding_count == 0U ||
        removed_members > current.live_member_count ||
        removed_identity_bytes > current.retained_member_identity_bytes)
    {
        return false;
    }
    output = current;
    --output.live_binding_count;
    output.live_member_count -= removed_members;
    output.retained_member_identity_bytes -= removed_identity_bytes;
    return true;
}

} // namespace RoR::Render
