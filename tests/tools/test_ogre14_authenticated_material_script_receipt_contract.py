import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AuthenticatedMaterialScriptReceiptContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (
            ROOT
            / "source/main/gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceipt.h"
        ).read_text(encoding="utf-8")
        cls.source = (
            ROOT
            / "source/main/gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceipt.cpp"
        ).read_text(encoding="utf-8")
        cls.content_header = (
            ROOT / "source/main/resources/ContentManager.h"
        ).read_text(encoding="utf-8")
        cls.content_source = (
            ROOT / "source/main/resources/ContentManager.cpp"
        ).read_text(encoding="utf-8")
        cls.cache_source = (
            ROOT / "source/main/resources/CacheSystem.cpp"
        ).read_text(encoding="utf-8")
        cls.dependency_header = (
            ROOT
            / "source/main/resources/terrn2_fileformat/"
            "TerrainBundleDependency.h"
        ).read_text(encoding="utf-8")
        cls.dependency_test = (
            ROOT / "tests/resources/TerrainBundleDependencyTests.cpp"
        ).read_text(encoding="utf-8")
        cls.archive_verifier_source = (
            ROOT
            / "source/main/resources/terrn2_fileformat/"
            "TerrainBundleArchiveVerifier.cpp"
        ).read_text(encoding="utf-8")
        cls.archive_verifier_test = (
            ROOT / "tests/resources/TerrainBundleArchiveVerifierTests.cpp"
        ).read_text(encoding="utf-8")
        cls.main_source = (ROOT / "source/main/main.cpp").read_text(
            encoding="utf-8"
        )
        cls.native_integration_test = (
            ROOT
            / "tests/gfx/ogre14/"
            "Ogre14AuthenticatedMaterialScriptNativeIntegrationTests.cpp"
        ).read_text(encoding="utf-8")
        cls.sanitizer = (
            ROOT / "source/main/resources/LegacyMaterialScriptSanitizer.cpp"
        ).read_text(encoding="utf-8")
        cls.sanitizer_tests = (
            ROOT / "tests/resources/LegacyMaterialScriptSanitizerTests.cpp"
        ).read_text(encoding="utf-8")
        cls.thread_gate = (
            ROOT
            / "source/main/gfx/ogre14/Ogre14AuthenticatedResourceThreadGate.h"
        ).read_text(encoding="utf-8")
        cls.documentation = (
            ROOT
            / "doc/nextgen/OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_RECEIPT.md"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "source/main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.test_cmake = (ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.probe_cmake = (
            ROOT / "tools/ogre_next_probe/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")
        cls.prelink = (
            ROOT
            / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake"
        ).read_text(encoding="utf-8")
        cls.runner = (ROOT / "tools/run_ogre_next_probe.py").read_text(
            encoding="utf-8"
        )
        cls.verifier = (
            ROOT / "tools/verify_ogre_next_artifact_set.py"
        ).read_text(encoding="utf-8")

    def test_receipts_are_private_mint_immutable_handles(self):
        self.assertIn(
            "class Ogre14AuthenticatedMaterialScriptReceipt final",
            self.header,
        )
        self.assertIn("std::shared_ptr<const State> state_", self.header)
        self.assertIn("friend class ::RoR::ContentManager", self.header)
        self.assertIn("void Poison() noexcept", self.header)
        self.assertNotIn("public:\n  explicit Ogre14AuthenticatedMaterialScriptReceipt(", self.header)

    def test_import_closure_is_retained_and_ordered(self):
        for token in (
            "source_count() const noexcept",
            "primary_source_index() const noexcept",
            "source_metadata_at(std::size_t index)",
            "SourceClosureState",
            "source_open_ordinal != closure_index + 1U",
            "metadata.compiler_file_identity != root_request",
            "SharesSourceStateWith",
        ):
            self.assertIn(token, self.header + self.source)

    def test_preopen_uses_exact_selected_archive_member(self):
        for token in (
            "resourceStreamOpeningEnabled() const override",
            "const Ogre::Archive* selected_archive",
            "const Ogre::FileInfo* exact_file_info",
            "exact_file_info->archive != selected_archive",
            "exact_file_info->path + exact_file_info->basename",
            "archive_binding.immutable_member_manifest->find(exact_member)",
            "live_archive->open(exact_member)",
            "original_sha256 != manifest_member->second.sha256",
            "CaptureOgre14AuthenticatedArchiveAuthorityProof(",
            "archive_authority.AuthenticatesExclusive(selected_archive)",
            "archive_binding.archive_pointer_token",
            "handled = true",
        ):
            self.assertIn(token, self.content_header + self.content_source)
        opening_start = self.content_source.index(
            "Ogre::DataStreamPtr ContentManager::resourceStreamOpening"
        )
        opened_start = self.content_source.index(
            "void ContentManager::resourceStreamOpened", opening_start
        )
        opening = self.content_source[opening_start:opened_start]
        proof = opening.index(
            "CaptureOgre14AuthenticatedArchiveAuthorityProof("
        )
        self.assertLess(proof, opening.index("live_archive->getName()"))
        self.assertLess(proof, opening.index("live_archive->open(exact_member)"))

    def test_postopen_revalidates_exact_replacement(self):
        postopen = self.content_source.index(
            "void ContentManager::resourceStreamOpened"
        )
        collision = self.content_source.index(
            "bool ContentManager::resourceCollision", postopen
        )
        block = self.content_source[postopen:collision]
        for token in (
            "staged.expected_stream.get() != dataStream.get()",
            "dataStream->getName() != name",
            "dataStream->tell() != 0U",
            "staged.input.effective_bytes",
            "staged.delivered = true",
            "staged.expected_stream.setNull()",
        ):
            self.assertIn(token, block)
        self.assertLess(
            block.index("staged.delivered = true"),
            block.index("staged.expected_stream.setNull()"),
        )

    def test_content_manager_owns_creation_and_finalizes_origin(self):
        event = self.content_source.index(
            "bool ContentManager::handleEvent"
        )
        tail = self.content_source[event:]
        for token in (
            "created = manager->create(",
            "*static_cast<Ogre::Material**>(retval) = created.get()",
            "stage.retained_material = created",
            "material.retained_material->getOrigin() !=",
            "binding.exact_origin",
            "CommitWholeGroup(",
        ):
            self.assertIn(token, self.content_source + tail)

    def test_removal_failure_poisons_before_logging(self):
        removal = self.content_source.index(
            "m_authenticated_material_scripts.RemoveMaterial("
        )
        poison = self.content_source.index(
            "m_authenticated_material_scripts.Poison();", removal
        )
        log = self.content_source.index("LOG(fmt::format(", poison)
        self.assertLess(removal, poison)
        self.assertLess(poison, log)

    def test_mount_advances_material_authority_before_external_publication(self):
        mount_start = self.content_source.index(
            "void ContentManager::MountAuthenticatedPackageResourceLocation"
        )
        mount_end = self.content_source.index(
            "bool ContentManager::IsAuthenticatedPackageSourceMounted",
            mount_start,
        )
        mount = self.content_source[mount_start:mount_end]
        for token in (
            "material_script_candidate = m_authenticated_material_scripts",
            "material_script_candidate.AdvanceGroupGeneration(",
            "m_authenticated_material_scripts =",
            "std::move(material_script_candidate)",
            "m_authenticated_material_script_candidate.reset()",
        ):
            self.assertIn(token, mount)
        self.assertLess(
            mount.index("material_script_candidate.AdvanceGroupGeneration("),
            mount.index("Ogre::EmbeddedZipArchiveFactory::addEmbbeddedFile("),
        )
        preflight = mount.index(
            "BuildTerrainBundleAuthenticatedArchivePreflight("
        )
        bind = mount.index("this->BindAuthenticatedResourceThread()")
        manifest_admission = mount.index(
            "TryAdmitOgre14AuthenticatedArchiveManifest("
        )
        embedded_registration = mount.index(
            "Ogre::EmbeddedZipArchiveFactory::addEmbbeddedFile("
        )
        resource_location = mount.index(
            "resource_manager.addResourceLocation("
        )
        self.assertLess(preflight, bind)
        self.assertLess(bind, manifest_admission)
        self.assertLess(manifest_admission, embedded_registration)
        self.assertLess(embedded_registration, resource_location)
        external_mount = mount.index(
            "Ogre::EmbeddedZipArchiveFactory::addEmbbeddedFile("
        )
        for token in (
            "package_materials_candidate.erase(resource_group)",
            "authenticated_materials_candidate.erase(resource_group)",
            "generated_material_bindings_candidate.erase(resource_group)",
            "committed_material_script_generations_candidate.erase(resource_group)",
        ):
            self.assertLess(mount.index(token), external_mount)
        first_publication = mount.index(
            "m_package_archives_by_group.swap(package_archives_candidate)"
        )
        publication = mount[first_publication:]
        self.assertNotIn(".erase(resource_group)", publication)
        for token in (
            "CaptureOgre14AuthenticatedArchiveAuthorityProof(",
            "archive_authority.AuthenticatesExclusive(selected_archive)",
            "selected_archive->findFileInfo(\"*\", true, false)",
            "selected_archive->findFileInfo(\"*\", true, true)",
            "member_uncompressed_bytes",
            "member_binding.sha256 = member_sha256",
            "immutable_member_manifest",
            "TryAdmitOgre14AuthenticatedArchiveManifest(",
            "m_authenticated_package_archive_manifest_accounting =",
        ):
            self.assertIn(token, mount)
        mount_proof = mount.index(
            "CaptureOgre14AuthenticatedArchiveAuthorityProof("
        )
        self.assertLess(
            mount_proof,
            mount.index("selected_archive->findFileInfo(\"*\", true, false)"),
        )
        for token in (
            "TestPreMountZipAdmissionRejection(content)",
            "MakeZip64CountEnvelope(",
            "materials/A.material",
            "materials/a.material",
            "CountingArchiveMountFault",
            "registration_counter.callback_count == 0U",
            "NativeAuthenticatedEncryptedZipRejected",
            "NativeAuthenticatedZeroCompressedDeflateRejected",
            "NativeAuthenticatedAttributeDirectoryRejected",
            "TestNativeDirectoryIndexParity(content)",
            "CountNativeArchives() == archive_count_before",
            "groups.getResourceLocationList(group).size() ==",
            "!content.IsAuthenticatedPackageSourceMounted(",
        ):
            self.assertIn(token, self.native_integration_test)
        for token in (
            "ZIP_UNSUPPORTED_FLAGS",
            "local_flags != flags || local_method != method",
            "std::memcmp(",
            "ResolveZipDataDescriptor(",
            "ZIP_DOS_DIRECTORY_ATTRIBUTE",
            "compressed == 0U",
            "ZIP local member spans overlap",
        ):
            self.assertIn(token, self.archive_verifier_source)
        for token in (
            "TestZip64MemberAndDataDescriptorPreflight",
            "TestUnsupportedAndHostileLocalMetadataFailsBeforeMount",
            "MakeStoredZip64(",
            "MakeStoredZip64WithDescriptor(",
            "MakeStoredZip64OffsetWithDescriptor(",
            "zero_compressed_deflate",
            "local_extra_overflow",
            "zip64_compressed_overflow",
        ):
            self.assertIn(token, self.archive_verifier_test)

    def test_repair_digest_is_versioned_domain_separated_and_exact(self):
        for token in (
            "kLegacyMaterialScriptRepairPlanVersion",
            "ror.ogre14.material-script-repair.applied",
            "ror.ogre14.material-script-repair.none",
            "exact_member_name != plan.script_name",
            "AppendLittleEndian64",
            "EVP_sha256()",
        ):
            self.assertIn(token, self.sanitizer)
        self.assertIn(
            "94950a0d8dd46673d5003fa7995dd436354be4596222d551d38e3099cf352c35",
            self.sanitizer_tests,
        )

    def test_group_publication_is_atomic_and_compatibility_is_downstream(self):
        start = self.content_source.index(
            "void ContentManager::resourceGroupScriptingEnded"
        )
        end = self.content_source.index(
            "void ContentManager::resourceRemove", start
        )
        block = self.content_source[start:end]
        for token in (
            "candidate->texture_receipts.FindResource(",
            "material_registry_candidate.CommitWholeGroup(",
            "m_authenticated_texture_receipts =",
            "m_committed_material_script_generations.swap(",
            "m_authenticated_material_script_candidate.reset()",
            "ApplyShaderCompatibilityFallbacks(group_name)",
        ):
            self.assertIn(token, block)
        self.assertLess(
            block.index("m_committed_material_script_generations.swap("),
            block.index("ApplyShaderCompatibilityFallbacks(group_name)"),
        )

    def test_import_fallback_and_cyclic_attempts_fail_closed_before_io(self):
        loading_start = self.content_source.index(
            "Ogre::DataStreamPtr ContentManager::resourceLoading"
        )
        opening_start = self.content_source.index(
            "Ogre::DataStreamPtr ContentManager::resourceStreamOpening",
            loading_start,
        )
        loading = self.content_source[loading_start:opening_start]
        self.assertIn("is_exact_texture_resource", loading)
        self.assertGreaterEqual(
            loading.count("is_exact_texture_resource &&"), 2
        )
        for token in (
            "resource == nullptr",
            "candidate->pending_import_open = true",
            "candidate->pending_import_name.swap(pending_name)",
            "candidate->source_by_compiler_file.find(",
            "++candidate->source_open_attempt_count",
        ):
            self.assertIn(token, loading)

        opened_start = self.content_source.index(
            "void ContentManager::resourceStreamOpened", opening_start
        )
        opening = self.content_source[opening_start:opened_start]
        self.assertIn("source_open_attempt_count", opening)
        self.assertIn(
            "MAX_AUTHENTICATED_MATERIAL_SCRIPT_SOURCE_OPEN_ATTEMPTS = 128U",
            self.content_source,
        )
        self.assertIn(
            "MAX_AUTHENTICATED_MATERIAL_SCRIPT_SOURCE_OPEN_ATTEMPTS",
            opening,
        )
        self.assertIn(
            "handled = candidate->parse_has_authenticated_root", opening
        )
        self.assertIn("const bool consumes_pending_import", opening)
        self.assertIn("candidate->pending_import_name != name", opening)
        self.assertIn("candidate->pending_import_open = false", opening)
        self.assertLess(
            opening.index("source_open_attempt_count"),
            opening.index("live_archive->open(exact_member)"),
        )
        parse_end = self.content_source.index(
            "void ContentManager::scriptParseEnded"
        )
        group_end = self.content_source.index(
            "void ContentManager::resourceGroupScriptingEnded", parse_end
        )
        parse_block = self.content_source[parse_end:group_end]
        self.assertIn("skipped || candidate->pending_import_open", parse_block)

        event_start = self.content_source.index(
            "bool ContentManager::handleEvent"
        )
        event_end = self.content_source.index(
            "void ContentManager::InitManagedMaterials", event_start
        )
        event = self.content_source[event_start:event_end]
        for token in (
            "candidate->poisoned || candidate->pending_import_open",
            "*static_cast<Ogre::Material**>(retval) = nullptr",
            "m_force_next_authenticated_material_event_empty_for_testing",
            "candidate->poisoned = true",
            "return true",
            "created = manager->create(",
        ):
            self.assertIn(token, event)
        self.assertIn(
            "ForceNextMaterialEventNameEmpty(content)",
            self.native_integration_test,
        )
        material_event = event.index(
            "auto* matEvent = static_cast<CreateMaterialScriptCompilerEvent*>(evt)"
        )
        first_null_output = event.index(
            "*static_cast<Ogre::Material**>(retval) = nullptr",
            material_event,
        )
        self.assertLess(first_null_output, event.index("matEvent->mName.empty()"))
        manager_null_start = event.index("if (manager == nullptr)")
        manager_null_end = event.index(
            "std::uint64_t parse_token", manager_null_start
        )
        manager_null = event[manager_null_start:manager_null_end]
        for token in (
            "candidate->parse_active",
            "candidate->parse_has_authenticated_root",
            "candidate->group == matEvent->mResourceGroup",
            "candidate->group == m_scripting_resource_group",
            "candidate->poisoned = true",
            "return true",
            "return false",
        ):
            self.assertIn(token, manager_null)
        self.assertLess(
            event.index("candidate->poisoned || candidate->pending_import_open"),
            event.index("created = manager->create("),
        )
        for rejected_condition in (
            "!candidate->sources.at(source->second).delivered",
            "manager->getByName(\n"
            "                matEvent->mName, matEvent->mResourceGroup)",
        ):
            rejected = event.index(rejected_condition)
            create = event.index("created = manager->create(")
            self.assertLess(rejected, create)
            rejection_block = event[rejected:create]
            self.assertIn("candidate->poisoned = true", rejection_block)
            self.assertIn("return true", rejection_block)

    def test_dependency_abort_is_terminal_until_exact_teardown_is_proven(self):
        for token in (
            "TerrainBundleDependencyTeardownMustFailStop(",
            "authenticated_mount_published",
            "resource_group_destroyed",
            "authenticated_archive_unregistered",
        ):
            self.assertIn(token, self.dependency_header)
        start = self.cache_source.index("const auto abandon_target_group")
        end = self.cache_source.index("\n        };\n\n    try", start)
        block = self.cache_source[start:end]
        for token in (
            "AbortAuthenticatedMaterialScriptGroup(",
            "destroyResourceGroup(resource_group)",
            "UnregisterPackageResourceGroup(",
            "TerrainBundleDependencyTeardownMustFailStop(",
            "std::terminate()",
            "authenticated_mount_published = false",
        ):
            self.assertIn(token, block)
        self.assertLess(
            block.index("UnregisterPackageResourceGroup("),
            block.index("authenticated_mount_published = false"),
        )
        for token in (
            "TerrainBundleDependencyTeardownMustFailStop(true, false, false)",
            "TerrainBundleDependencyTeardownMustFailStop(true, true, false)",
            "TerrainBundleDependencyTeardownMustFailStop(true, true, true)",
        ):
            self.assertIn(token, self.dependency_test)

    def test_alias_and_generated_reuse_require_exact_native_authority(self):
        start = self.content_source.index(
            "void ContentManager::processMaterialName"
        )
        end = self.content_source.index(
            "void ContentManager::processSkeletonName", start
        )
        block = self.content_source[start:end]
        for token in (
            "m_authenticated_material_scripts.MintResolution(",
            "m_authenticated_material_scripts.RevalidateResolution(",
            "GeneratedMaterialBinding binding",
            "has_exact_generated_binding",
            "m_generated_material_bindings_by_group.swap(",
        ):
            self.assertIn(token, block)
        state_lock = block.rindex(
            "std::lock_guard<std::mutex> state_lock("
        )
        self.assertGreater(
            block.index("m_generated_material_bindings_by_group.swap("),
            state_lock,
        )

    def test_material_removal_revokes_name_hints_and_exact_bindings(self):
        start = self.content_source.index(
            "void ContentManager::resourceRemove"
        )
        end = self.content_source.index(
            "void ContentManager::processMaterialName", start
        )
        block = self.content_source[start:end]
        for token in (
            "m_package_materials_by_group.erase(",
            "m_authenticated_materials_by_group.erase(",
            "m_generated_material_bindings_by_group.erase(",
            "request->second == removed_name",
            "m_authenticated_material_scripts.RemoveMaterial(",
        ):
            self.assertIn(token, block)

    def test_unregister_removes_external_closure_before_internal_publication(self):
        start = self.content_source.index(
            "void ContentManager::UnregisterPackageResourceGroup"
        )
        end = self.content_source.index(
            "void ContentManager::AbortAuthenticatedMaterialScriptGroup",
            start,
        )
        block = self.content_source[start:end]
        for token in (
            "removed_archive_bytes",
            "removeResourceLocation(",
            "group_generations_candidate.erase(resource_group)",
            "m_legacy_material_group_generations.swap(",
            "m_authenticated_package_archive_retained_bytes -=",
            "std::terminate()",
        ):
            self.assertIn(token, block)
        self.assertLess(
            block.index("removeResourceLocation("),
            block.index("m_legacy_material_group_generations.swap("),
        )
        external_remove = block.index("removeResourceLocation(")
        for token in (
            "group_generations_candidate.erase(resource_group)",
            "authenticated_bindings_candidate.erase(resource_group)",
            "generated_material_bindings_candidate.erase(resource_group)",
            "committed_material_script_generations_candidate.erase(resource_group)",
            "CaptureOgre14AuthenticatedArchiveAuthorityProof(",
            "archive_authority.AuthenticatesExclusive(expected_archive)",
            "archive_authority.AuthenticatesDetached(expected_archive)",
            "TryReleaseOgre14AuthenticatedArchiveManifest(",
        ):
            self.assertLess(block.index(token), external_remove)
        publication = block.index(
            "m_legacy_material_group_generations.swap("
        )
        ogre14_end = block.index("#else", publication)
        self.assertNotIn(
            ".erase(resource_group)", block[publication:ogre14_end]
        )
        manager_proof = block.index(
            "CaptureOgre14AuthenticatedArchiveAuthorityProof("
        )
        archive_dereference = block.index("manager_archive->getName()")
        self.assertLess(manager_proof, archive_dereference)

    def test_texture_mesh_and_autodetect_use_live_archive_and_manifest_proofs(self):
        loading_start = self.content_source.index(
            "Ogre::DataStreamPtr ContentManager::resourceLoading"
        )
        opening_start = self.content_source.index(
            "Ogre::DataStreamPtr ContentManager::resourceStreamOpening",
            loading_start,
        )
        block = self.content_source[loading_start:opening_start]
        self.assertNotIn("findGroupContainingResource(name)", block)
        for token in (
            "CaptureOgre14AuthenticatedArchiveAuthorityProof(",
            "proof.AuthenticatesExclusive(location.archive)",
            "ResolveLiveArchiveManagerPointer(",
            "location.archive->exists(name)",
            "prove_location_before_dereference(location)",
            "selected_binding.immutable_member_manifest->find(",
            "selected_member_binding.sha256",
            "revalidate_selected_archive_authority();",
            "const std::string selected_source_sha256 = Sha256Bytes(",
            "Sha256Bytes(\n                mesh_source_bytes.data(),",
        ):
            self.assertIn(token, block)
        autodetect_proof = block.index(
            "CaptureOgre14AuthenticatedArchiveAuthorityProof("
        )
        self.assertLess(
            autodetect_proof, block.index("location.archive->exists(name)")
        )
        selected_proof = block.index(
            "selected_archive_authority =\n"
            "                Render::CaptureOgre14AuthenticatedArchiveAuthorityProof("
        )
        self.assertLess(
            selected_proof, block.index("selected_archive->getName()")
        )

    def test_serialized_resource_thread_contract_is_cross_platform_and_explicit(self):
        for token in (
            "static thread_local unsigned char token",
            "compare_exchange_strong",
            "std::memory_order_acq_rel",
            "IsCurrentThreadOrUnbound",
        ):
            self.assertIn(token, self.thread_gate)
        self.assertIn("process-lifetime resource/render thread", self.documentation)
        self.assertIn("background resource loading must be", self.content_header)
        source_text = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (ROOT / "source/main").rglob("*")
            if path.is_file() and path.suffix in {".h", ".cpp", ".cc", ".mm"}
        )
        self.assertNotIn("Ogre::ResourceBackgroundQueue", source_text)
        self.assertNotIn("setBackgroundLoaded(true", source_text)

    def test_build_and_runtime_tests_are_registered(self):
        self.assertIn(
            "gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceipt.{h,cpp}",
            self.cmake,
        )
        self.assertIn(
            "gfx/ogre14/Ogre14AuthenticatedResourceThreadGate.h",
            self.cmake,
        )
        self.assertIn(
            "gfx/ogre14/Ogre14AuthenticatedArchiveLocationClosure.h",
            self.cmake,
        )
        for token in (
            "ror_ogre14_authenticated_material_script_receipt_tests",
            "Ogre14AuthenticatedMaterialScriptReceiptTests.cpp",
            "NAME ogre14_authenticated_material_script_receipt",
            "PRIVATE OpenSSL::Crypto",
            "Threads::Threads",
            "ror_ogre14_authenticated_archive_location_closure_tests",
            "Ogre14AuthenticatedArchiveLocationClosureTests.cpp",
            "NAME ogre14_authenticated_archive_location_closure",
            "Ogre14AuthenticatedMaterialScriptNativeIntegrationTests.cpp",
            "ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING=1",
            "NAME ogre14_authenticated_material_script_native_integration",
            "$<TARGET_FILE:RoR>",
            "--internal-ogre14-authenticated-material-script-native-integration",
        ):
            self.assertIn(token, self.test_cmake)

        for token in (
            "ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_NATIVE_TESTING",
            "RunOgre14AuthenticatedMaterialScriptNativeIntegrationTests(",
            "--internal-ogre14-authenticated-material-script-native-integration",
        ):
            self.assertIn(token, self.main_source)
        self.assertLess(
            self.main_source.index("renderer_game_bridge.Initialize(argc, argv)"),
            self.main_source.index(
                "RunOgre14AuthenticatedMaterialScriptNativeIntegrationTests(\n"
                "            argc, argv)"
            ),
        )
        self.assertIn("if (ROR_OGRE14 AND TARGET RoR)", self.test_cmake)
        self.assertIn("PROPERTIES TIMEOUT 60", self.test_cmake)
        for token in (
            "TestSuccessfulLifecycleAndReload(content)",
            "TestMissingImportRejection(content)",
            "TestUndeliveredSourceEventRejection(content)",
            "TestDuplicateExistingNameRejection(content)",
            "TestDeepestSafeOpenClosure(content)",
            "TestAttemptLimitRejection(content)",
            "TestCyclicImportRejection(content)",
            "AUTHENTICATED_SOURCE_OPEN_ATTEMPT_LIMIT = 128U",
            "--cycle-child",
        ):
            self.assertIn(token, self.native_integration_test)

        target_start = self.probe_cmake.index(
            "add_executable(\n"
            "        ror_ogre14_authenticated_material_script_receipt_tests"
        )
        target_end = self.probe_cmake.index(
            "\n    add_executable(", target_start + 1
        )
        target_block = self.probe_cmake[target_start:target_end]
        for token in (
            "Ogre14AuthenticatedMaterialScriptReceiptTests.cpp",
            "Ogre14AuthenticatedMaterialScriptReceipt.cpp",
            "LegacyMaterialScriptSanitizer.cpp",
            "TerrainBundleArchiveVerifier.cpp",
            "ROR_OGRE14_AUTHENTICATED_MATERIAL_SCRIPT_TESTING=1",
            "OpenSSL::Crypto",
            "Threads::Threads",
        ):
            self.assertIn(token, target_block)
        self.assertIn(
            "add_test(NAME ror_ogre14_authenticated_material_script_receipt",
            self.probe_cmake,
        )
        command = (
            "python tests/tools/"
            "test_ogre14_authenticated_material_script_receipt_contract.py"
        )
        optimized = command.replace("python ", "python -O ")
        self.assertEqual(self.workflow.count(command), 1)
        self.assertEqual(self.workflow.count(optimized), 1)

        provenance_paths = (
            "source/main/resources/LegacyMaterialScriptSanitizer.cpp",
            "source/main/resources/LegacyMaterialScriptSanitizer.h",
            "source/main/resources/terrn2_fileformat/TerrainBundleDependency.cpp",
            "source/main/resources/terrn2_fileformat/TerrainBundleDependency.h",
            "tests/gfx/ogre14/Ogre14AuthenticatedMaterialScriptReceiptTests.cpp",
            "tests/gfx/ogre14/"
            "Ogre14AuthenticatedMaterialScriptNativeIntegrationTests.cpp",
            "tests/resources/LegacyMaterialScriptSanitizerTests.cpp",
            "tests/resources/TerrainBundleDependencyTests.cpp",
        )

        def exact_manifest_path_count(text, path):
            return sum(
                line.strip().rstrip(",").strip('"') == path
                for line in text.splitlines()
            )

        for path in provenance_paths:
            self.assertEqual(self.workflow.count(f"- {path}"), 2)
            self.assertEqual(
                exact_manifest_path_count(self.probe_cmake, path), 2
            )
            self.assertEqual(
                exact_manifest_path_count(self.prelink, path), 2
            )
            self.assertEqual(
                exact_manifest_path_count(self.runner, path), 1
            )
            self.assertEqual(
                exact_manifest_path_count(self.verifier, path), 1
            )


if __name__ == "__main__":
    unittest.main()
