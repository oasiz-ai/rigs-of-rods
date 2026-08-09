/*
    This source file is part of Rigs of Rods

    Rigs of Rods is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3.
*/

#include "gfx/ogre14/Ogre14AuthenticatedArchiveLocationClosure.h"

#include <OgreRoot.h>

#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void Finish(int code, const char* message)
{
    std::cerr << message << std::endl;
    // This fixture deliberately demonstrates an OGRE 14 lifetime defect: two
    // groups share one non-refcounted Archive*. Let the OS reclaim this
    // isolated test process instead of asking Root to traverse the known-
    // hostile duplicated location closure during stack unwinding.
    std::_Exit(code);
}

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        Finish(EXIT_FAILURE, message);
    }
}

void TestManifestAccounting()
{
    using RoR::Render::Ogre14AuthenticatedArchiveManifestAccounting;
    Ogre14AuthenticatedArchiveManifestAccounting empty;
    Ogre14AuthenticatedArchiveManifestAccounting first;
    Require(
        RoR::Render::TryAdmitOgre14AuthenticatedArchiveManifest(
            empty, 2U, 96U, 2U, 4U, 192U, first) &&
            first.live_binding_count == 1U &&
            first.live_member_count == 2U &&
            first.retained_member_identity_bytes == 96U,
        "first manifest admission did not publish exact accounting");

    Ogre14AuthenticatedArchiveManifestAccounting second;
    Require(
        RoR::Render::TryAdmitOgre14AuthenticatedArchiveManifest(
            first, 2U, 96U, 2U, 4U, 192U, second) &&
            second.live_binding_count == 2U &&
            second.live_member_count == 4U &&
            second.retained_member_identity_bytes == 192U,
        "just-below manifest admission did not reach the exact caps");

    Ogre14AuthenticatedArchiveManifestAccounting sentinel = first;
    Require(
        !RoR::Render::TryAdmitOgre14AuthenticatedArchiveManifest(
            second, 1U, 1U, 2U, 4U, 192U, sentinel) &&
            sentinel.live_binding_count == first.live_binding_count &&
            sentinel.live_member_count == first.live_member_count &&
            sentinel.retained_member_identity_bytes ==
                first.retained_member_identity_bytes,
        "cap+1 manifest admission changed its sentinel");

    Ogre14AuthenticatedArchiveManifestAccounting released;
    Require(
        RoR::Render::TryReleaseOgre14AuthenticatedArchiveManifest(
            second, 2U, 96U, released) &&
            released.live_binding_count == first.live_binding_count &&
            released.live_member_count == first.live_member_count &&
            released.retained_member_identity_bytes ==
                first.retained_member_identity_bytes,
        "manifest teardown did not recover exact process capacity");
    Ogre14AuthenticatedArchiveManifestAccounting recovered;
    Require(
        RoR::Render::TryAdmitOgre14AuthenticatedArchiveManifest(
            released, 2U, 96U, 2U, 4U, 192U, recovered) &&
            recovered.live_binding_count == second.live_binding_count &&
            recovered.live_member_count == second.live_member_count &&
            recovered.retained_member_identity_bytes ==
                second.retained_member_identity_bytes,
        "released manifest capacity was not reusable");
}

} // namespace

int main(int argc, char** argv)
{
    Require(argc == 2, "expected one FileSystem archive fixture directory");
    TestManifestAccounting();
    Ogre::Root root("", "", "");
    Ogre::ResourceGroupManager& groups =
        Ogre::ResourceGroupManager::getSingleton();
    const Ogre::String target = "AuthenticatedArchiveTarget";
    const Ogre::String sibling = "AuthenticatedArchiveSibling";

    groups.createResourceGroup(target);
    groups.addResourceLocation(argv[1], "FileSystem", target, false, true);
    const auto& initial_locations = groups.getResourceLocationList(target);
    Require(initial_locations.size() == 1U &&
                initial_locations.front().archive != nullptr,
            "target group did not receive one live archive");
    Ogre::Archive* const shared_archive =
        initial_locations.front().archive;
    Ogre::ArchiveManager& archives = Ogre::ArchiveManager::getSingleton();
    const Ogre::String archive_name = shared_archive->getName();

    const RoR::Render::Ogre14AuthenticatedArchiveLocationClosure attached =
        RoR::Render::CaptureOgre14AuthenticatedArchiveLocationClosure(
            groups, target, shared_archive);
    Require(attached.IsExclusivelyAttachedToTarget() &&
                !attached.IsDetachedAfterTargetDestruction(),
            "single target location was not admitted");
    const RoR::Render::Ogre14AuthenticatedArchiveAuthorityProof
        attached_authority =
            RoR::Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                archives, groups, target, archive_name);
    Require(attached_authority.AuthenticatesExclusive(shared_archive),
            "manager-selected target authority was not admitted");

    groups.destroyResourceGroup(target);
    const RoR::Render::Ogre14AuthenticatedArchiveLocationClosure detached =
        RoR::Render::CaptureOgre14AuthenticatedArchiveLocationClosure(
            groups, target, shared_archive);
    Require(detached.IsDetachedAfterTargetDestruction() &&
                !detached.IsExclusivelyAttachedToTarget(),
            "destroyed target did not produce the accepted detached closure");
    const RoR::Render::Ogre14AuthenticatedArchiveAuthorityProof
        detached_authority =
            RoR::Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                archives, groups, target, archive_name);
    Require(detached_authority.AuthenticatesDetached(shared_archive),
            "manager-selected detached authority was not admitted");

    groups.createResourceGroup(target);
    groups.createResourceGroup(sibling);
    groups.addResourceLocation(argv[1], "FileSystem", target, false, true);
    groups.addResourceLocation(argv[1], "FileSystem", sibling, false, true);
    const auto& target_locations = groups.getResourceLocationList(target);
    const auto& sibling_locations = groups.getResourceLocationList(sibling);
    Require(target_locations.size() == 1U &&
                sibling_locations.size() == 1U &&
                target_locations.front().archive == shared_archive &&
                sibling_locations.front().archive == shared_archive,
            "pinned OGRE did not reproduce shared Archive pointer reuse");

    const RoR::Render::Ogre14AuthenticatedArchiveLocationClosure shared =
        RoR::Render::CaptureOgre14AuthenticatedArchiveLocationClosure(
            groups, target, shared_archive);
    Require(shared.process_location_count == 2U &&
                shared.target_location_count == 1U &&
                !shared.IsExclusivelyAttachedToTarget() &&
                !shared.IsDetachedAfterTargetDestruction(),
            "cross-group shared archive closure was not rejected");
    const RoR::Render::Ogre14AuthenticatedArchiveAuthorityProof
        shared_authority =
            RoR::Render::CaptureOgre14AuthenticatedArchiveAuthorityProof(
                archives, groups, target, archive_name);
    Require(!shared_authority.AuthenticatesExclusive(shared_archive) &&
                !shared_authority.AuthenticatesDetached(shared_archive),
            "manager proof admitted a cross-group shared archive");

    Finish(EXIT_SUCCESS,
           "authenticated-archive-location-closure=ok");
}
