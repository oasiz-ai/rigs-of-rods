#!/usr/bin/env python3
"""Static contract tests for exact translated deformable materials."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER = REPOSITORY_ROOT / "source/main/gfx/render"
SCENE_HEADER = RENDER / "Ogre14GraphicsSceneSource.h"
SCENE_SOURCE = SCENE_HEADER.with_suffix(".cpp")
GFX_SCENE_SOURCE = REPOSITORY_ROOT / "source/main/gfx/GfxScene.cpp"
ACTOR_SOURCE = REPOSITORY_ROOT / "source/main/physics/Actor.cpp"
ACTOR_SPAWNER_SOURCE = REPOSITORY_ROOT / "source/main/physics/ActorSpawner.cpp"
CPP_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/render/Ogre14DynamicMaterialClosureTests.cpp"
)
MANAGED_SOURCE_CPP_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/ogre14/Ogre14ManagedMaterialSourceAdapterTests.cpp"
)
MANAGED_SOURCE_ADAPTER = (
    REPOSITORY_ROOT
    / "source/main/gfx/ogre14/Ogre14ManagedMaterialSourceAdapter.cpp"
)
MATERIAL_SOURCE_NATIVE_CPP_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/ogre14/OgreNextDemoMaterialSourceNativeTests.cpp"
)
MATERIAL_SOURCE = (
    REPOSITORY_ROOT
    / "source/main/gfx/ogre14/detail/OgreNextDemoMaterialSource.cpp"
)
README = RENDER / "README.md"
PRODUCER_DOC = (
    REPOSITORY_ROOT / "doc/nextgen/GRAPHICS_SCENE_SNAPSHOT_PRODUCER.md"
)


class Ogre14DynamicMaterialClosureContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.scene_header = SCENE_HEADER.read_text(encoding="utf-8")
        cls.scene_source = SCENE_SOURCE.read_text(encoding="utf-8")
        cls.gfx_scene_source = GFX_SCENE_SOURCE.read_text(encoding="utf-8")
        cls.actor_source = ACTOR_SOURCE.read_text(encoding="utf-8")
        cls.actor_spawner_source = ACTOR_SPAWNER_SOURCE.read_text(
            encoding="utf-8"
        )
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")
        cls.managed_source_cpp_test = MANAGED_SOURCE_CPP_TEST.read_text(
            encoding="utf-8"
        )
        cls.managed_source_adapter = MANAGED_SOURCE_ADAPTER.read_text(
            encoding="utf-8"
        )
        cls.material_source_native_cpp_test = (
            MATERIAL_SOURCE_NATIVE_CPP_TEST.read_text(encoding="utf-8")
        )
        cls.material_source = MATERIAL_SOURCE.read_text(encoding="utf-8")

    def test_dynamic_input_reuses_the_static_exact_closure_contract(self) -> None:
        for token in (
            "kMaximumOgre14GraphicsSceneDynamicSections = 65536U",
            "kMaximumOgre14GraphicsSceneDynamicAssets = 65536U",
            "std::shared_ptr<const Ogre14LegacyMaterialClosure> resolved_material",
            "bool mesh_reverse_winding = false",
            "Ogre14GraphicsSceneResolvedMaterialFrameLineage",
            "ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage",
            "IOgre14GraphicsSceneDynamicInventoryFaultInjector",
            "canonical_assets_by_asset_key_",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.scene_header)

    def test_dynamic_transaction_is_binding_aware_bounded_and_atomic(self) -> None:
        method = self.scene_source[
            self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneDynamicInventory"
            ) : self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneMaterialFallback"
            )
        ]
        for token in (
            "ValidateOgre14LegacyMaterialClosure",
            "resolved_source_sequence",
            "resolved_catalog_sequence",
            "resolved_material->material_source_asset_id",
            "resolved_material->requires_reverse_winding",
            "resolved_material->assets",
            "BuildOgre14LegacyStableAssetKey",
            "ValidateProducerBoundMaterialMeshCompatibility",
            "EquivalentGraphicsSceneAssetInput",
            "lhs.material_bindings == rhs.material_bindings",
            "canonical_assets_by_asset_key_",
            "AFTER_FIRST_RESOLVED_DEPENDENCY",
            "CheckedAddSize",
            "dynamic_inventory.assets",
            "dynamic_inventory.registry_lifetime",
            "catch (const std::bad_alloc &)",
            "dynamic_inventory.exception",
        ):
            with self.subTest(token=token):
                self.assertIn(token, method if token != "lhs.material_bindings == rhs.material_bindings" else self.scene_source)
        self.assertNotIn("inputs.size() *", method)
        self.assertIn(
            "if (resolved_material != nullptr) {\n"
            "      proposed_material_assets = resolved_material->assets;\n"
            "    } else {",
            method,
        )

    def test_joined_lineage_preflight_covers_static_and_dynamic(self) -> None:
        lineage = self.scene_source[
            self.scene_source.index(
                "ValidationResult ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage"
            ) : self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneDynamicInventory"
            )
        ]
        for token in (
            "static_inputs",
            "dynamic_inputs",
            "ValidateOgre14LegacyMaterialClosure",
            "candidate.source_sequence != closure->source_sequence",
            "candidate.catalog_sequence != closure->catalog_sequence",
            "lineage = candidate",
            "catch (const std::bad_alloc &)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, lineage)

    def test_cross_domain_merge_is_binding_aware_bounded_and_wired(self) -> None:
        for token in (
            "kMaximumOgre14GraphicsSceneMergedAssets = 65536U",
            "IOgre14GraphicsSceneAssetMergeFaultInjector",
            "MergeOgre14GraphicsSceneAssets",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.scene_header)
        method = self.scene_source[
            self.scene_source.index(
                "ValidationResult MergeOgre14GraphicsSceneAssets"
            ) : self.scene_source.index(
                "ValidationResult BuildOgre14GraphicsSceneDynamicInventory"
            )
        ]
        for token in (
            "CheckedAddSize(static_assets.size(), aggregate_count)",
            "CheckedAddSize(dynamic_assets.size(), aggregate_count)",
            "CheckedAddSize(road_assets.size(), aggregate_count)",
            "EquivalentGraphicsSceneAssetInput",
            "AFTER_FIRST_UNIQUE_ASSET",
            "assets.merge.allocation",
            "assets.merge.exception",
            "lhs.source_asset_id < rhs.source_asset_id",
        ):
            with self.subTest(token=token):
                self.assertIn(token, method)
        self.assertIn(
            "lhs.material_bindings == rhs.material_bindings",
            self.scene_source,
        )
        self.assertIn(
            "Render::MergeOgre14GraphicsSceneAssets(",
            self.gfx_scene_source,
        )
        self.assertNotIn(
            "ValidationResult MergeOgre14SceneAssets(",
            self.gfx_scene_source,
        )

    def test_native_section_reference_is_lean_and_fallback_stays_strict(self) -> None:
        reference = self.gfx_scene_source[
            self.gfx_scene_source.index(
                "struct Ogre14MaterialSectionReference"
            ) : self.gfx_scene_source.index(
                "ValidationResult CaptureOgre14MaterialFallbackInput"
            )
        ]
        for token in (
            "Ogre::MaterialPtr material",
            "std::string exact_resource_group",
            "std::string exact_name",
            "Ogre14GraphicsSceneMaterialCull cull",
            "bool reverse_winding",
            "CaptureOgre14MaterialSectionReference",
            "resolved.pass->getCullingMode()",
            "candidate.material = resolved.material",
        ):
            with self.subTest(token=token):
                self.assertIn(token, reference)
        for fallback_only_gate in (
            "getColourWriteEnabled",
            "getSceneBlendingOperation",
            "getSourceBlendFactor",
            "getAlphaRejectFunction",
        ):
            with self.subTest(fallback_only_gate=fallback_only_gate):
                self.assertNotIn(fallback_only_gate, reference)

        fallback = self.gfx_scene_source[
            self.gfx_scene_source.index(
                "ValidationResult CaptureOgre14MaterialFallbackInput"
            ) : self.gfx_scene_source.index(
                "ValidationResult ValidateOgre14StaticVertexDeclaration"
            )
        ]
        for token in (
            "ResolveOgre14MaterialFirstPass(material, resolved)",
            "pass->getColourWriteEnabled(",
            "pass->getSceneBlendingOperation() != Ogre::SBO_ADD",
            "portable fallback supports replace or straight-alpha blending",
            "CaptureOgre14MaterialSectionReference(resolved, reference)",
            "pass->getAlphaRejectFunction()",
            "portable fallback supports always-pass or greater-equal alpha",
            "output = std::move(candidate)",
            "reverse_winding = reference.reverse_winding",
        ):
            with self.subTest(token=token):
                self.assertIn(token, fallback)
        self.assertLess(
            fallback.index("pass->getColourWriteEnabled("),
            fallback.index(
                "CaptureOgre14MaterialSectionReference(resolved, reference)"
            ),
        )
        self.assertLess(
            fallback.index("output = std::move(candidate)"),
            fallback.index("reverse_winding = reference.reverse_winding"),
        )

    def test_dynamic_section_publishes_the_actual_winding_conversion(self) -> None:
        capture = self.gfx_scene_source[
            self.gfx_scene_source.index(
                "ValidationResult CaptureOgre14DynamicEntitySections"
            ) : self.gfx_scene_source.index(
                "ValidationResult CaptureOgre14StaticMeshObjects"
            )
        ]
        material_capture = capture.index(
            "CaptureOgre14MaterialFallbackInput("
        )
        section_winding = capture.index(
            "section.mesh_reverse_winding = reverse_winding"
        )
        cpu_winding = capture.index("base.reverse_winding = reverse_winding")
        publish = capture.index("sections.push_back(std::move(section))")
        self.assertLess(material_capture, section_winding)
        self.assertLess(section_winding, cpu_winding)
        self.assertLess(cpu_winding, publish)

    def test_managed_source_authority_is_frame_reachability_scoped(self) -> None:
        snapshot_current = self.actor_source[
            self.actor_source.index(
                "bool Actor::IsManagedMaterialDeclarationSnapshotCurrent("
            ) : self.actor_source.index(
                "Actor::ValidateManagedMaterialDeclarationSnapshotReachability("
            )
        ]
        self.assertIn("SharesImmutableStateWith", snapshot_current)
        self.assertNotIn("binding.Revalidate", snapshot_current)

        exact_resolution = self.actor_source[
            self.actor_source.index(
                "Actor::ResolveManagedMaterialDeclarationBinding("
            ) : self.actor_source.index(
                "bool Actor::FindReusableManagedMaterialSourceReceipt("
            )
        ]
        for token in (
            "ReferencesExactMaterial(exact_material)",
            "output_found = true",
            "output_found = false",
        ):
            with self.subTest(token=token):
                self.assertIn(token, exact_resolution)
        self.assertNotIn("binding.Revalidate", exact_resolution)

        capture = self.gfx_scene_source[
            self.gfx_scene_source.index(
                "ValidationResult CaptureOgre14DynamicEntitySections"
            ) : self.gfx_scene_source.index(
                "ValidationResult CaptureOgre14StaticMeshObjects"
            )
        ]
        for token in (
            "projected_managed_material_bindings",
            "managed_binding_found",
            "if (projected && managed_binding_ptr != nullptr)",
            "candidate.SharesImmutableStateWith(",
        ):
            with self.subTest(token=token):
                self.assertIn(token, capture)
        self.assertLess(
            capture.index("material_source.TryProject("),
            capture.index("if (projected && managed_binding_ptr != nullptr)"),
        )
        projection = self.material_source[
            self.material_source.index(
                "bool OgreNextDemoMaterialSource::TryProjectCurrent("
            ) : self.material_source.index(
                "Render::ValidationResult OgreNextDemoMaterialSource::TryProject("
            )
        ]
        structural_reference = projection.index(
            "managed_binding->ReferencesExactMaterial(native_material)"
        )
        semantic_exclusion = projection.index(
            "MANAGED_MATERIAL_SEMANTIC_UNSUPPORTED"
        )
        selected_source_authority = projection.index(
            "managed_binding->MatchesExactMaterial(native_material)"
        )
        self.assertLess(structural_reference, semantic_exclusion)
        self.assertLess(semantic_exclusion, selected_source_authority)
        inventory = self.gfx_scene_source[
            self.gfx_scene_source.index(
                "GfxScene::CaptureOgre14DynamicActorInventory("
            ) : self.gfx_scene_source.index(
                "GfxScene::CaptureOgre14GraphicsScene("
            )
        ]
        self.assertIn(
            "ValidateManagedMaterialDeclarationSnapshotReachability(",
            inventory,
        )
        self.assertNotIn(
            "IsManagedMaterialDeclarationSnapshotCurrent(", inventory
        )
        for marker in (
            "TestFrameReachabilityIgnoresOnlyStaleUnreachableBindings",
            "stale unreachable binding poisoned reachable frame",
            "stale frame-reachable binding escaped fail-closed validation",
            "reachability weakened the immutable publication-set invariant",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.managed_source_cpp_test)
        for marker in (
            "begin unprojected managed matte gate",
            "unprojected managed binding entered the source-backed closure",
            "source-backed stale managed binding escaped fail-closed projection",
            "restored managed authority did not recover after matte gate",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.material_source_native_cpp_test)

    def test_actor_final_batches_authority_after_material_setup(self) -> None:
        method = self.actor_spawner_source[
            self.actor_spawner_source.index(
                "void ActorSpawner::ProcessManagedMaterial("
            ) : self.actor_spawner_source.index(
                "void ActorSpawner::ProcessCollisionBox("
            )
        ]
        semantic_stage = method.index("staged_source_textures[slot] = texture")
        compile_material = method.index("material->compile();")
        fresh_batch = method.index("BuildFreshAuthorityBatch(")
        seal_declaration = method.index("BuildManagedMaterialDeclaration(")
        publish = method.index("PublishManagedMaterialDeclaration(")
        self.assertLess(semantic_stage, compile_material)
        self.assertLess(compile_material, fresh_batch)
        self.assertLess(fresh_batch, seal_declaration)
        self.assertLess(seal_declaration, publish)
        self.assertNotIn("ResolveSelectedTextureSource(",
                         method[:compile_material])
        self.assertNotIn("BuildSelected(", method[:compile_material])
        actor_publish = self.actor_source[
            self.actor_source.index(
                "Actor::PublishManagedMaterialDeclaration("
            ) : self.actor_source.index("#endif", self.actor_source.index(
                "Actor::PublishManagedMaterialDeclaration("))
        ]
        refresh_retained = actor_publish.index(
            "RefreshDeclarationAuthorityBatch("
        )
        neutral_commit = actor_publish.index(
            "CommitManagedMaterialDeclaration("
        )
        actor_swap = actor_publish.index(
            "m_managed_material_declaration_bindings.swap("
        )
        self.assertLess(refresh_retained, neutral_commit)
        self.assertLess(neutral_commit, actor_swap)
        capture_boundary = self.actor_source[
            self.actor_source.index(
                "Actor::CaptureManagedMaterialDeclarationSnapshot("
            ) : self.actor_source.index(
                "bool Actor::IsManagedMaterialDeclarationSnapshotCurrent("
            )
        ]
        capture_neutral = capture_boundary.index(
            "Render::CaptureManagedMaterialDeclarationSnapshot("
        )
        best_effort_refresh = capture_boundary.index(
            "RefreshStaleDeclarationAuthorityBestEffort("
        )
        refresh_swap = capture_boundary.index(
            "m_managed_material_declaration_bindings.swap("
        )
        snapshot_publish = capture_boundary.index(
            "output = std::move(staged_snapshot)"
        )
        self.assertLess(capture_neutral, best_effort_refresh)
        self.assertLess(best_effort_refresh, refresh_swap)
        self.assertLess(refresh_swap, snapshot_publish)
        self.assertIn(
            "IsManagedMaterialDeclarationSnapshotCurrent(", capture_boundary
        )
        self.assertIn("if (content_manager != nullptr)", capture_boundary)
        self.assertIn("if (result)", capture_boundary)
        best_effort = self.managed_source_adapter[
            self.managed_source_adapter.index(
                "RefreshStaleDeclarationAuthorityBestEffort("
            ) : self.managed_source_adapter.index(
                "ValidationResult ValidateOgre14ReachableManagedMaterialBindings("
            )
        ]
        stage_complete_set = best_effort.index(
            "staged =\n        retained_bindings"
        )
        refresh_single = best_effort.index(
            "RefreshDeclarationAuthorityBatch("
        )
        retain_on_failure = best_effort.index(
            "if (refresh && single_refreshed.size() == 1U)"
        )
        atomic_swap = best_effort.index("output.swap(staged)")
        self.assertLess(stage_complete_set, refresh_single)
        self.assertLess(refresh_single, retain_on_failure)
        self.assertLess(retain_on_failure, atomic_swap)
        self.assertEqual(best_effort.count("output.swap(staged)"), 1)
        for marker in (
            "TestFreshBatchAfterSuccessiveReceiptAndTusSetupMutations",
            "successive source commit did not stale pre-setup authority",
            "refresh complete retained actor publication",
            "changed neutral source rewrote retained actor publication",
            "authority change after final bind escaped fail-closed validation",
            "TestCaptureBoundaryRefreshAfterFailedAndLaterActorLoads",
            "new source load plus later material failure did not stale prior binding",
            "post-managed actor texture COW did not stale prior binding",
            "unavailable stale unprojected snapshot was rejected",
            "unavailable stale projected root escaped fail-closed validation",
            "capture boundary rewrote a changed neutral managed source",
            "changed stale unprojected snapshot was rejected",
            "changed stale projected root escaped fail-closed validation",
            "changed source COW did not stale the complete publication",
            "mixed refresh did not repair benign COW and retain changed source",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.managed_source_cpp_test)

    def test_resolved_closure_is_not_a_domain_tombstone(self) -> None:
        self.assertNotIn(
            "current_asset_keys.insert(resolved_asset_keys",
            self.scene_source,
        )
        self.assertIn(
            "resolved_material->assets[asset_index].source_asset_id",
            self.scene_source,
        )
        self.assertIn(
            "canonicalize(\n            resolved_asset_keys[asset_index]",
            self.scene_source,
        )

    def test_hostile_native_acceptance_matrix_is_present(self) -> None:
        for token in (
            "TestExactDynamicClosureSharedOwnersAndStaticEquivalence",
            "TestResolvedClosureLifecycleBelongsToTranslator",
            "TestFallbackRemainsExactAndTexturedFailClosed",
            "TestWindingAndJoinedLineageAreExact",
            "TestHostileClosuresCollisionsAndUvGate",
            "TestCapsAndExceptionRollback",
            "TestInjectedExceptionRollback",
            "static and dynamic payload/binding equivalence rules diverged",
            "different source epochs were merged",
            "cross-domain catalog mismatch",
            "forged dynamic dependency order",
            "conflicting payloads",
            "conflicting exact bindings",
            "cross-kind collision",
            "missing authored UV",
            "kMaximumOgre14GraphicsSceneDynamicSections + 1U",
            "dynamic lifetime cap+1 published partial state",
            "allocation exception changed deep registry/output owners or values",
            "unexpected exception changed deep registry/output owners or values",
            "post-fault dynamic deformation owner was not reusable",
            "distinct static B could not re-enter with the shared translator closure",
            "translator ownership weakened static A's permanent object tombstone",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cpp_test)

    def test_build_ci_provenance_and_docs_are_registered(self) -> None:
        test_cmake = (REPOSITORY_ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("Ogre14DynamicMaterialClosureTests.cpp", test_cmake)
        self.assertIn("ror_ogre14_dynamic_material_closure_tests", test_cmake)
        manifests = (
            REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt",
            REPOSITORY_ROOT
            / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
            REPOSITORY_ROOT / "tools/run_ogre_next_probe.py",
            REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py",
        )
        for manifest in manifests:
            text = manifest.read_text(encoding="utf-8")
            with self.subTest(manifest=manifest.name):
                self.assertIn(
                    "tests/gfx/render/Ogre14DynamicMaterialClosureTests.cpp",
                    text,
                )
                self.assertIn(
                    "tests/tools/test_ogre14_dynamic_material_closure_contract.py",
                    text,
                )
        for document in (README, PRODUCER_DOC):
            text = document.read_text(encoding="utf-8")
            with self.subTest(document=document.name):
                self.assertIn(
                    "ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage",
                    text,
                )
                self.assertIn("mesh_reverse_winding", text)
        self.assertIn(
            "MergeOgre14GraphicsSceneAssets", PRODUCER_DOC.read_text(encoding="utf-8")
        )


if __name__ == "__main__":
    unittest.main()
