#!/usr/bin/env python3
"""Static closure for the transport-only OGRE 14 terrain-composite V2 receipt."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "source/main/gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.h"
SOURCE = ROOT / "source/main/gfx/ogre14/Ogre14TerrainCompositeCaptureReceipt.cpp"
NATIVE = ROOT / "source/main/gfx/ogre14/Ogre14TerrainCompositeNativeAdapter.cpp"
TEST = ROOT / "tests/gfx/ogre14/Ogre14TerrainCompositeCaptureReceiptTests.cpp"
NATIVE_TEST = ROOT / "tests/gfx/ogre14/Ogre14TerrainCompositeNativeReadbackTests.cpp"
DOC = ROOT / "doc/nextgen/OGRE14_TERRAIN_COMPOSITE_CAPTURE_RECEIPTS.md"
PATCH = (
    ROOT
    / "cmake/conan/recipes/ogre3d/patches/14.5.2/"
    "terrain-composite-revision-metal-readback.patch"
)
THIS_PATH = "tests/tools/test_ogre14_terrain_composite_capture_receipt_contract.py"


def executable_block(cmake: str, target: str) -> str:
    marker = f"add_executable(\n        {target}"
    start = cmake.index(marker)
    end = cmake.index("\n    )", start) + len("\n    )")
    return cmake[start:end]


class Ogre14TerrainCompositeCaptureReceiptContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.native = NATIVE.read_text(encoding="utf-8")
        cls.test = TEST.read_text(encoding="utf-8")
        cls.native_test = NATIVE_TEST.read_text(encoding="utf-8")
        cls.doc = DOC.read_text(encoding="utf-8")
        cls.patch = PATCH.read_text(encoding="utf-8")

    def test_v2_is_immutable_native_minted_and_production_has_no_callback(self) -> None:
        contract = self.header + self.source
        for token in (
            "kOgre14TerrainCompositeCaptureReceiptVersion = 2U",
            "kOgre14TerrainCompositeCaptureConfigurationVersion = 2U",
            "kOgre14TerrainCompositeNativeObservationVersion = 2U",
            "kOgre14TerrainCompositeSemanticContractVersion = 2U",
            "class Ogre14TerrainCompositeCaptureReceipt final",
            "std::shared_ptr<const State> state_",
            "friend class Ogre14TerrainCompositeNativeAdapter",
            "SharesImmutableStateWith",
            "std::is_nothrow_move_assignable<",
        ):
            self.assertIn(token, contract)
        public_capture = self.header.split(
            "class Ogre14TerrainCompositeNativeAdapter final", 1
        )[1].split("private:", 1)[0]
        self.assertNotIn("FaultInjector", public_capture)
        self.assertNotIn("fault_injector", public_capture)
        self.assertNotIn("MaybeInject", self.native)
        self.assertIn("production Capture exposes no callback", self.doc.replace("\n", " "))

    def test_caps_precede_update_and_every_capture_reads_all_mips(self) -> None:
        capture = self.native.split(
            "ValidationResult Ogre14TerrainCompositeNativeAdapter::Capture(", 1
        )[1]
        self.assertLess(
            capture.index("ValidateCaptureConfiguration(configuration)"),
            capture.index("terrain->updateCompositeMap();"),
        )
        self.assertLess(capture.index("CaptureObservation("),
                        capture.index("blitToMemory(destination)"))
        self.assertLess(capture.index("blitToMemory(destination)"),
                        capture.rindex("CaptureObservation("))
        self.assertIn("HasEveryV2Invariant(candidate_receipt)", self.source)
        self.assertIn("texture_resource_revision", self.source)
        for forbidden in (
            "prior_receipt",
            "TryReusePriorReceipt",
            "SameReceiptAuthority",
            "sealed_transport_v2",
        ):
            self.assertNotIn(forbidden, self.header + self.source + self.native)
        self.assertIn("Every capture brackets and reads every mip", self.doc)
        self.assertIn("pixel-buffer write paths do not all advance", self.doc)

    def test_full_mip_chain_bytes_digests_and_alpha_evidence_are_exact(self) -> None:
        contract = self.header + self.source + self.native
        for token in (
            '"RoR/Ogre14/TerrainComposite/MipRGBA/v2"',
            '"RoR/Ogre14/TerrainComposite/FullMipChain/v2"',
            "sizeof(kOgre14TerrainCompositeMipDigestDomain)",
            "sizeof(kOgre14TerrainCompositeMipChainDigestDomain)",
            "texture_additional_mip_count",
            "texture_mip_count",
            "FullMipLevelCount",
            "full_mip_chain_rgba_byte_count",
            "full_mip_chain_sha256",
            "ComputeMipChainDigest",
            "ComputeMipDigest(digest_identity, receipt.mip_rgba_bytes(index)",
            "LINEAR_SPECULAR_MASK",
        ):
            self.assertIn(token, contract)
        for token in (
            "missing final mip was accepted",
            "additional-mip semantics were ambiguous",
            "gapped mip numbering was accepted",
            "aliased mip buffers were accepted",
            "aggregate mip-byte cap was not enforced",
            "receipt did not retain an exact source mip",
        ):
            self.assertIn(token, self.test)
        self.assertIn("getNumMipmaps() == 1U", self.native_test)
        self.assertIn("levels additional to level zero", self.native)

    def test_transport_sampling_authority_is_direct_and_complete(self) -> None:
        contract = self.header + self.source + self.native
        for token in (
            "Ogre14TerrainCompositeSamplingObservation",
            "scene_manager_pointer_token",
            "texture_unit_pointer_token",
            "sampler_pointer_token",
            "bound_texture_pointer_token",
            "unit->_getTexturePtr()",
            "unit->isBlank()",
            "unit->isTextureLoadFailing()",
            "unit->getUnorderedAccessMipLevel()",
            "unordered_access_mip_level != -1",
            "NarrowCount(static_cast<std::size_t>(unit->getTextureCoordSet())",
            "unit->_deriveTexCoordCalcMethod() == Ogre::TEXCALC_NONE",
            "unit->getEffects().size()",
            "sampler->getAddressingMode()",
            "sampler->getFiltering(Ogre::FT_MIN)",
            "sampler->getMipmapBias()",
            "sampler->getCompareEnabled()",
            "sampler->getCompareFunction() != Ogre::CMPF_GREATER_EQUAL",
            "sampler->getBorderColour()",
            "unit->isHardwareGammaEnabled()",
            "texture.isHardwareGammaEnabled()",
            "scene_manager->getFogMode()",
        ):
            self.assertIn(token, contract)
        for token in (
            "v.sampling.unordered_access_mip_level = 0",
            "v.sampling.texcoord_calculation_none = false",
            "v.sampling.texture_effect_count = 1U",
            "v.sampling.texture_unit_is_blank = true",
            "v.sampling.texture_unit_load_failing = true",
            "v.sampling.border_colour[0U] = -0.0F",
            "v.sampling.border_colour[3U] = 0.0F",
            "v.sampling.texture_unit_hardware_gamma_enabled = false",
        ):
            self.assertIn(token, self.test)
        self.assertIn(
            "std::array<float, 4U>{{0.0F, 0.0F, 0.0F, 1.0F}}",
            self.source,
        )

    def test_full_native_authority_caps_and_rollback_have_hostile_coverage(self) -> None:
        revalidation = self.source.split("bool SameObservation(", 1)[1].split(
            "constexpr std::array<std::uint32_t, 64U>", 1
        )[0]
        for token in (
            "terrain_group_pointer_token",
            "terrain_slot_pointer_token",
            "terrain_pointer_token",
            "texture_pointer_token",
            "pixel_buffer_pointer_token",
            "texture_handle",
            "exact_texture_resource_group",
            "exact_texture_name",
            "packed_slot_key",
            "exact_definition_filename",
            "generated_save_filename",
            "terrain_world_position",
            "texture_hardware_gamma_enabled",
            "mip_chain",
            "SameSampling",
        ):
            self.assertIn(token, revalidation)
        for evidence in (
            "substituted TerrainGroup pointer was accepted during revalidation",
            "substituted Terrain slot pointer was accepted during revalidation",
            "substituted bound texture pointer was accepted during revalidation",
            "substituted level-zero pixel buffer was accepted during revalidation",
            "name-only texture substitution was accepted during revalidation",
            "texture resource-group substitution was accepted during revalidation",
            "packed terrain slot substitution was accepted during revalidation",
            "terrain page definition substitution was accepted during revalidation",
            "generated save identity substitution was accepted during revalidation",
            "coupled Texture/TUS gamma substitution was accepted during revalidation",
            "maximum_mip_levels below the complete chain was accepted",
            "null readback did not fail transactionally",
            "zero texture revision was accepted",
            "file-backed page without a definition identity was accepted",
            "updating Terrain page was accepted",
            "unloaded Terrain page was accepted",
            "invalid texture loading state was accepted",
            "padded mip-row layout was accepted",
            "overflowing additional-mip count was accepted",
            "consumed/runtime page identity was rejected or mislabeled",
        ):
            self.assertIn(evidence, self.test)

    def test_shader_pass_and_frontend_execution_authority_is_absent(self) -> None:
        production = self.header + self.source + self.native
        for forbidden in (
            "Ogre::RTShader",
            "getProgramSetForInspection",
            "TargetRenderState",
            "GpuProgram",
            "fragment_program_ir",
            "getBlendState",
            "getDepthCheckEnabled",
            "getVertexColourTracking",
            "effective_receive_shadows",
            "effective_lighting_enabled",
            "output_target_linear_colours",
            "MaterialDescriptor.h",
            "GfxScene",
        ):
            self.assertNotIn(forbidden, production)
        self.assertNotIn("low_lod", production)
        self.assertIn("It makes no Pass,", self.doc)
        self.assertIn("RTShader, generated-program", self.doc)

    def test_both_transfer_modes_lower_without_false_srgb_label(self) -> None:
        contract = self.header + self.source + self.test
        for token in (
            "BAKED_DIFFUSE",
            "DECODE_BEFORE_FILTER",
            "LEGACY_UNORM_DISPLAY_DOMAIN",
            "Ogre14TerrainCompositeOpaqueLowering",
            "candidate.rgb_transfer = metadata.rgb_transfer",
            "mip.rgba_bytes[alpha] = 255U",
            "candidate.sampler.maximum_lod =",
            "static_cast<float>(candidate.mip_chain.size() - 1U)",
            "LowerOgre14TerrainCompositeOpaque",
            "opaque lowering lost its explicit semantic or transfer",
            "lowering modified immutable source alpha evidence",
        ):
            self.assertIn(token, contract)
        self.assertNotIn("TextureColorSpace::SRGB", self.source)
        self.assertNotIn("TextureResourceDescriptor", self.header + self.source)
        self.assertIn("follow-on MaterialDescriptor V3", self.header)
        self.assertIn("SceneManager FOG_NONE only", self.source)
        self.assertIn("Float4{} value", self.source)

    def test_cpu_oracle_distinguishes_transfer_order(self) -> None:
        contract = self.header + self.source + self.test
        for token in (
            "EvaluateOgre14TerrainCompositeBilinearOracle",
            "DecodeSrgb",
            "0.3849123234F",
            "0.5960784314F",
            "decoded.rgba[0U] - legacy.rgba[0U]",
            "0.4382352941F",
            "oracle hostile failure was not transactional",
            "oracle failure construction must remain catchable",
        ):
            self.assertIn(token, contract)
        self.assertNotIn(
            "Ogre14TerrainCompositeOracleSample &sample) noexcept",
            self.header + self.source,
        )

    def test_native_probe_preserves_real_pinned_abi_surface(self) -> None:
        for token in (
            "OGRE_VERSION_MAJOR == 14",
            "OGRE_VERSION_MINOR == 5",
            "OGRE_VERSION_PATCH == 2",
            "class ExactMipTexture final",
            "mNumMipmaps = 1U",
            "mSurfaceList.push_back",
            "getBuffer(0U, 1U)",
            "getUnorderedAccessMipLevel() == -1",
            "public full-mip readback lost arbitrary alpha",
            "terrain-composite transport ABI probe passed",
        ):
            self.assertIn(token, self.native + self.native_test)
        for forbidden in (
            "OgreShader",
            "RTShader",
            "Metal/",
            "OgreMetal",
            "d3d11.h",
            "OgreD3D",
            "OgreGL",
            "windows.h",
        ):
            self.assertNotIn(forbidden, self.native_test)
        cmake = (ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
        native_block = executable_block(
            cmake, "ror_ogre14_terrain_composite_native_readback_tests"
        )
        self.assertIn("Ogre14TerrainCompositeNativeAdapter.cpp", native_block)
        self.assertIn("Ogre14TerrainCompositeNativeReadbackTests.cpp", native_block)
        self.assertIn("PRIVATE OGRE::Terrain", cmake)

    def test_existing_recipe_patch_and_cross_platform_provenance_remain(self) -> None:
        self.assertIn("without changing row orientation", self.patch)
        self.assertIn("sourceLevel:static_cast<NSUInteger>(mLevel)", self.patch)
        self.assertIn("mCompositeMap->_dirtyState();", self.patch)
        self.assertNotIn("getProgramSetForInspection", self.patch)
        manifests = (
            ROOT / "tools/run_ogre_next_probe.py",
            ROOT / "tools/verify_ogre_next_artifact_set.py",
            ROOT / "tools/ogre_next_probe/CMakeLists.txt",
            ROOT / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
        )
        for manifest in manifests:
            with self.subTest(manifest=manifest.name):
                self.assertIn(THIS_PATH, manifest.read_text(encoding="utf-8"))
        workflow = (ROOT / ".github/workflows/ogre-next-probe.yml").read_text(
            encoding="utf-8"
        )
        self.assertEqual(workflow.count(THIS_PATH), 2)
        self.assertIn("normal and `-o` modes", self.doc.lower().replace("\n", " "))


if __name__ == "__main__":
    unittest.main()
