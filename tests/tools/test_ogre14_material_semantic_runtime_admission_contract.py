import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
EXPECTED_FIXTURE_SHA256 = (
    "e41391acb8f5e13232f2515a5d6cac6b9e8c486c3d54825d0dfe18894384d2ff"
)
CONTRACT_PATH = (
    "tests/tools/test_ogre14_material_semantic_runtime_admission_contract.py"
)
DOC_PATH = "doc/nextgen/OGRE14_MATERIAL_SEMANTIC_RUNTIME_ADMISSION.md"
NATIVE_PATH = (
    "source/main/gfx/ogre14/"
    "Ogre14LegacyMaterialSemanticRuntimeAdmissionNative.cpp"
)


class MaterialSemanticRuntimeAdmissionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        def read(path):
            return (ROOT / path).read_text(encoding="utf-8")

        cls.header = read(
            "source/main/gfx/ogre14/"
            "Ogre14LegacyMaterialSemanticRuntimeAdmission.h"
        )
        cls.source = read(
            "source/main/gfx/ogre14/"
            "Ogre14LegacyMaterialSemanticRuntimeAdmission.cpp"
        )
        cls.native = read(
            "source/main/gfx/ogre14/"
            "Ogre14LegacyMaterialSemanticRuntimeAdmissionNative.cpp"
        )
        cls.coordinator_header = read(
            "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.h"
        )
        cls.coordinator = read(
            "source/main/gfx/ogre14/Ogre14LegacyLiveMaterialCoordinator.cpp"
        )
        cls.content_header = read("source/main/resources/ContentManager.h")
        cls.capture_header = read(
            "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h"
        )
        cls.capture_source = read(
            "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.cpp"
        )
        cls.capture_authority = read(
            "source/main/gfx/ogre14/"
            "Ogre14LegacyNativeMaterialCaptureAuthority.cpp"
        )
        cls.script_header = read(
            "source/main/gfx/ogre14/"
            "Ogre14AuthenticatedMaterialScriptReceipt.h"
        )
        cls.script_source = read(
            "source/main/gfx/ogre14/"
            "Ogre14AuthenticatedMaterialScriptReceipt.cpp"
        )
        cls.runtime_tests = read(
            "tests/gfx/ogre14/"
            "Ogre14LegacyMaterialSemanticRuntimeAdmissionTests.cpp"
        )
        cls.native_integration_tests = read(
            "tests/gfx/ogre14/"
            "Ogre14AuthenticatedMaterialScriptNativeIntegrationTests.cpp"
        )
        cls.fixture = read(
            "tests/fixtures/gfx/ogre14/"
            "material-semantic-runtime-admission.synthetic.json"
        )
        cls.documentation = read(DOC_PATH)
        cls.test_cmake = read("tests/CMakeLists.txt")
        cls.probe_cmake = read("tools/ogre_next_probe/CMakeLists.txt")
        cls.runner = read("tools/run_ogre_next_probe.py")
        cls.verifier = read("tools/verify_ogre_next_artifact_set.py")
        cls.prelink = read(
            "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake"
        )
        cls.workflow = read(".github/workflows/ogre-next-probe.yml")

    def test_approved_manifest_has_no_production_mint_or_friend_injection(self):
        manifest_start = self.header.index(
            "class Ogre14LegacyMaterialSemanticApprovedManifest final"
        )
        manifest = self.header[
            manifest_start : self.header.index(
                "enum class Ogre14LegacyMaterialSemanticRuntimeAdmissionStage"
            )
        ]
        self.assertIn("private:", manifest)
        self.assertIn("MintFromTrustedDescription", manifest)
        self.assertIn(
            "ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING", manifest
        )
        self.assertNotIn("ApprovedManifestLoader", self.header)
        self.assertNotIn("friend class Ogre14LegacyMaterialSemanticApproved", manifest)
        callback = self.header.index(
            "class IOgre14LegacyMaterialSemanticRuntimeAdmissionFaultInjector {"
        )
        testing_gate = self.header.rfind(
            "#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)",
            0,
            callback,
        )
        self.assertGreater(
            testing_gate,
            self.header.index("private:", manifest_start),
        )

    def test_activation_order_is_external_hash_then_parser_scope_registry(self):
        start = self.source.index(
            "ValidationResult AuthenticateOgre14LegacyMaterialSemanticRuntime("
        )
        block = self.source[start:]
        tokens = (
            "ComputeSha256(catalog_file_bytes",
            "ParseOgre14LegacyMaterialSemanticCatalogV2(",
            "semantic_runtime.trusted_scope",
            "BuildOgre14LegacyMaterialSemanticRegistryFromCatalogV2(",
            "BEFORE_RUNTIME_AUTHORITY_PUBLICATION",
        )
        positions = [block.index(token) for token in tokens]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("REVIEWED_PACKAGE_REVISION_V1", self.header)
        self.assertNotIn(
            "reviewed_resource_generation == source->group_generation",
            self.source,
        )

    def test_native_live_admission_has_exact_order_and_one_authority(self):
        start = self.native.index(
            "CaptureAndAdmitWithLiveAuthority("
        )
        end = self.native.index(
            "ValidationResult CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(",
            start,
        )
        block = self.native[start:end]
        tokens = (
            "live_authority.ResolveAuthenticatedMaterialScript(",
            "script_resolution.MatchesResolver(live_authority)",
            "ResolveMaterialSemantics(",
            "ValidateScriptAndSemanticPrerequisites(",
            "CaptureOgre14LegacyNativeMaterial(",
            "ValidateNativeCapture(",
            "live_authority.CaptureAuthenticatedTextureAuthoritySnapshot(",
            "texture_authority.Authenticates(texture_resolution)",
            "live_authority.RevalidateAuthenticatedMaterialScript(",
            "live_authority.CaptureAuthenticatedMaterialScriptAuthoritySnapshot(",
            "PublishAdmission(",
            "std::move(native_capture)",
        )
        positions = [block.index(token) for token in tokens]
        self.assertEqual(positions, sorted(positions))
        self.assertNotIn("IOgre14AuthenticatedTextureResolver &", block)
        for exact_edge in (
            "live_authority.ResolveAuthenticatedMaterialScript(",
            "script_resolution.MatchesResolver(live_authority)",
            "live_authority.CaptureAuthenticatedTextureAuthoritySnapshot(",
            "live_authority.RevalidateAuthenticatedMaterialScript(",
            "live_authority.CaptureAuthenticatedMaterialScriptAuthoritySnapshot(",
        ):
            self.assertIn(exact_edge, block)
        self.assertIn(
            "semantic_resolution.native_declaration, live_authority,",
            block,
        )
        self.assertIn("MatchesResolver(", self.script_header)
        self.assertIn("resolver_pointer_token", self.script_source)
        wrapper_call = self.native.index(
            "return runtime_authority.CaptureAndAdmitWithLiveAuthority(", end
        )
        production_wrapper = self.native[
            end : self.native.index(
                "#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)",
                wrapper_call,
            )
        ]
        self.assertIn("::RoR::ContentManager &content_manager", production_wrapper)
        self.assertIn(
            "translator_configuration, material, content_manager, output,",
            production_wrapper,
        )
        self.assertNotIn(
            "IOgre14LegacyMaterialRuntimeLiveAuthority", production_wrapper
        )

    def test_production_live_authority_is_final_content_manager(self):
        self.assertIn("class ContentManager final:", self.content_header)
        for base in (
            "public Render::IOgre14AuthenticatedMaterialScriptResolver",
            "public Render::IOgre14AuthenticatedMaterialScriptAuthorityProvider",
            "public Render::IOgre14AuthenticatedTextureResolver",
            "public Render::IOgre14AuthenticatedTextureAuthorityProvider",
        ):
            self.assertIn(base, self.content_header)
        for method in (
            "ResolveAuthenticatedTexture(",
            "RevalidateAuthenticatedTexture(",
            "CaptureAuthenticatedTextureAuthoritySnapshot(",
            "ResolveAuthenticatedMaterialScript(",
            "RevalidateAuthenticatedMaterialScript(",
            "CaptureAuthenticatedMaterialScriptAuthoritySnapshot(",
        ):
            start = self.content_header.index(method)
            self.assertIn("final;", self.content_header[start : start + 420])

        production_live = self.header[
            self.header.index("/// Production live path.") :
        ]
        production_live = production_live.split(
            "#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)", 1
        )[0]
        self.assertIn("::RoR::ContentManager &content_manager", production_live)
        self.assertNotIn(
            "IOgre14LegacyMaterialRuntimeLiveAuthority", production_live
        )
        abstract_position = self.header.index(
            "class IOgre14LegacyMaterialRuntimeLiveAuthority"
        )
        abstract_gate = self.header.rfind(
            "#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)",
            0,
            abstract_position,
        )
        self.assertGreaterEqual(abstract_gate, 0)
        self.assertGreater(
            self.header.find("#endif", abstract_gate), abstract_position
        )

        factory = self.coordinator_header[
            self.coordinator_header.index(
                "/// Authenticated production factory."
            ) :
        ]
        production_factory = factory.split(
            "#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)", 1
        )[0]
        self.assertIn("::RoR::ContentManager &content_manager", production_factory)
        self.assertNotIn(
            "IOgre14LegacyMaterialRuntimeLiveAuthority", production_factory
        )
        self.assertIn(
            "::RoR::ContentManager *content_manager_ = nullptr",
            self.coordinator_header,
        )
        for evidence in (
            "std::is_final<RoR::ContentManager>",
            "SEALED_CAPTURE",
            "SEALED_COORDINATOR_FACTORY",
            "IndependentLiveAuthority&",
            "sealed ContentManager production admission/factory path",
        ):
            self.assertIn(evidence, self.native_integration_tests)

    def test_production_entry_points_expose_no_fault_callbacks(self):
        for signature in (
            "AuthenticateOgre14LegacyMaterialSemanticRuntime(",
            "CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime(",
        ):
            start = self.header.index(signature)
            block = self.header[start : start + 950]
            before_test_gate = block.split(
                "#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)",
                1,
            )[0]
            self.assertNotIn("fault_injector", before_test_gate)
        prepare = self.coordinator_header[
            self.coordinator_header.index("ValidationResult PrepareAdmittedFrame(") :
        ]
        self.assertNotIn(
            "fault_injector",
            prepare.split(
                "#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)",
                1,
            )[0],
        )
        factory = self.coordinator_header[
            self.coordinator_header.index(
                "CreateOgre14LegacyAuthenticatedMaterialCoordinator("
            ) :
        ]
        self.assertNotIn(
            "translator_fault_injector",
            factory.split(
                "#if defined(ROR_OGRE14_SEMANTIC_RUNTIME_ADMISSION_TESTING)",
                1,
            )[0],
        )
        digest_macro = (
            "ROR_OGRE14_NATIVE_MATERIAL_DECLARATION_DIGEST_TESTING"
        )
        for surface in (
            "Ogre14LegacyNativeMaterialDeclarationDigestStage",
            "IOgre14LegacyNativeMaterialDeclarationDigestFaultInjector",
            "SetOgre14LegacyNativeMaterialDeclarationDigestFaultInjectorForTesting",
        ):
            position = self.capture_header.index(surface)
            gate = self.capture_header.rfind(
                f"#if defined({digest_macro})", 0, position
            )
            end = self.capture_header.find("#endif", gate)
            self.assertGreaterEqual(gate, 0)
            self.assertGreater(end, position)
        self.assertIn(
            f"#if defined({digest_macro})\nnamespace {{\n\nthread_local",
            self.capture_source,
        )
        callback_position = self.capture_source.index(
            "void BeforeNativeMaterialDeclarationDigestStage("
        )
        callback_gate = self.capture_source.rfind(
            f"#if defined({digest_macro})", 0, callback_position
        )
        self.assertGreaterEqual(callback_gate, 0)
        self.assertGreater(
            self.capture_source.find("#endif", callback_gate),
            callback_position,
        )
        native_target = self.test_cmake[
            self.test_cmake.index(
                "ror_ogre14_legacy_native_asset_extractor_compile_tests"
            ) :
        ]
        native_target = native_target[: native_target.index("if (TARGET OgreMain)")]
        self.assertIn(f"{digest_macro}=1", native_target)

    def test_production_preparation_requires_fresh_live_materials(self):
        public_start = self.coordinator_header.index(
            "ValidationResult PrepareAdmittedFrame("
        )
        public_block = self.coordinator_header[public_start : public_start + 900]
        self.assertIn("const std::vector<Ogre::Material *> &live_materials", public_block)
        self.assertNotIn(
            "const std::vector<Ogre14LegacyMaterialSemanticAdmission>",
            public_block.split("#if defined", 1)[0],
        )
        self.assertIn("PreparePreviouslyAdmittedFrameForTesting", public_block)
        prepare_start = self.native.index(
            "ValidationResult Ogre14LegacyLiveMaterialCoordinator::"
            "PrepareAdmittedFrame("
        )
        prepare_block = self.native[prepare_start:]
        self.assertLess(
            prepare_block.index("CheckPrepareStart("),
            prepare_block.index(
                "CaptureAndAdmitOgre14LegacyMaterialSemanticRuntime("
            ),
        )
        self.assertLess(
            prepare_block.index("maximum_live_assets_per_frame"),
            prepare_block.index("std::vector<Ogre::Material *> unique_identities"),
        )
        self.assertIn("std::sort(unique_identities.begin()", self.native)
        self.assertIn("live_materials.texture_bytes", self.native)
        self.assertIn("unique_texture_resolutions", self.native)
        self.assertIn("admitted_sampler_count", self.native)
        self.assertIn(
            "SharesLoadedResourceAuthorityWith(resolution)", self.native
        )
        self.assertIn(
            "live_asset_cap - admitted_sampler_count -", self.native
        )
        self.assertIn(
            "current_material_count = live_materials.size()", self.native
        )
        self.assertIn("PrepareFrameViewsImpl", self.coordinator)
        self.assertNotIn(
            "observation.native_capture = *capture", self.coordinator
        )

    def test_initial_surface_binds_complete_texture_projection(self):
        for token in (
            "catalog_unit.exact_unit_name != native_unit.exact_unit_name",
            "color_manual_one_f32_bits ==",
            "color_manual_two_f32_bits ==",
            "color_manual_factor_f32_bits == 0U",
            "alpha_manual_one_f32_bits == 0U",
            "alpha_manual_two_f32_bits == 0U",
            "alpha_manual_factor_f32_bits == 0U",
            "Ogre14LegacyTextureColorRole::BASE_COLOR_SRGB",
            "record.texture_units.size() > 1U",
        ):
            self.assertIn(token, self.source)
        self.assertIn("ProjectCatalogPass(record.pass)", self.source)
        self.assertIn("ProjectCatalogSampler(", self.source)
        self.assertIn("ValidateOgre14LegacyMaterialInput(", self.source)
        self.assertIn("RORNCP2", self.capture_header)
        self.assertIn(
            "'R', 'O', 'R', 'N', 'C', 'P', '2', '\\0'",
            self.capture_authority,
        )
        self.assertIn(
            "kOgre14LegacyNativeMaterialCaptureSerializationVersion = 2U",
            self.capture_header,
        )
        self.assertIn("writer.AppendString(unit.exact_unit_name)", self.capture_authority)

    def test_closure_and_repair_domain_are_fail_closed(self):
        for token in (
            "source_index == 0U",
            "Ogre14MaterialScriptSourceRole::ROOT_SCRIPT",
            "Ogre14MaterialScriptSourceRole::COMPILER_DEPENDENCY",
            "kOgre14AuthenticatedMaterialScriptRepairPlanVersion",
            "IsSupportedRepairState",
            "primary_source_index",
        ):
            self.assertIn(token, self.source)
        self.assertIn("primary_source_index() == 1U", self.runtime_tests)
        self.assertIn("unsupported catalog repair-plan version activated", self.runtime_tests)

    def test_fixture_hash_and_hostile_coverage_are_pinned(self):
        self.assertEqual(self.fixture.count('"material_name"'), 3)
        for name in ("RuntimeAccepted", "RuntimeImported", "RuntimeTextured"):
            self.assertIn(name, self.fixture)
        self.assertIn(EXPECTED_FIXTURE_SHA256, self.runtime_tests)
        self.assertIn(EXPECTED_FIXTURE_SHA256, self.documentation)
        for phrase in (
            "exact texture-unit-name projection mismatch",
            "sampler mismatch",
            "pass projection mismatch",
            "texture archive-member mismatch",
            "texture package SHA mismatch",
            "texture runtime-generation mismatch",
            "generated texture source kind",
            "untextured LINEAR_DATA",
            "textured LINEAR_DATA",
            "unsupported catalog pass activated",
            "catalog/native matched-but-unsupported sampler activated",
            "unsupported catalog sampler filter activated",
            "unsupported catalog sampler LOD activated",
        ):
            self.assertIn(phrase, self.runtime_tests)

    def test_static_contract_and_document_are_in_every_provenance_set(self):
        for manifest in (
            self.probe_cmake,
            self.runner,
            self.verifier,
            self.prelink,
            self.workflow,
        ):
            self.assertIn(CONTRACT_PATH, manifest)
            self.assertIn(DOC_PATH, manifest)
        self.assertIn(
            "test_ogre14_material_semantic_runtime_admission_contract.py",
            self.test_cmake,
        )
        self.assertEqual(
            self.workflow.count(f"- {NATIVE_PATH}"),
            2,
            "push and pull_request filters must name the native TU exactly",
        )


if __name__ == "__main__":
    unittest.main()
