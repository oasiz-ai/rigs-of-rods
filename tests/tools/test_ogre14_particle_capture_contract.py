#!/usr/bin/env python3
"""Static closure tests for continuous OGRE 14 particle capture v1."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
HEADER = (
    REPOSITORY_ROOT
    / "source/main/gfx/render/Ogre14ParticleCaptureSource.h"
)
SOURCE = HEADER.with_suffix(".cpp")
CPP_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/render/Ogre14ParticleCaptureSourceTests.cpp"
)
README = REPOSITORY_ROOT / "source/main/gfx/render/README.md"
MAIN_CMAKE = REPOSITORY_ROOT / "source/main/CMakeLists.txt"
TEST_CMAKE = REPOSITORY_ROOT / "tests/CMakeLists.txt"
PROBE_CMAKE = REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt"
WORKFLOW = REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
RENDER_CONTRACTS = REPOSITORY_ROOT / "source/main/gfx/render/RenderContracts.h"
PROVENANCE_FILES = (
    REPOSITORY_ROOT / "tools/run_ogre_next_probe.py",
    REPOSITORY_ROOT / "tools/verify_ogre_next_artifact_set.py",
    REPOSITORY_ROOT
    / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
)


class Ogre14ParticleCaptureContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")
        cls.readme = README.read_text(encoding="utf-8")
        cls.main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        cls.test_cmake = TEST_CMAKE.read_text(encoding="utf-8")
        cls.probe_cmake = PROBE_CMAKE.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_boundary_is_versioned_and_renderer_sdk_free(self) -> None:
        for token in (
            "kOgre14ParticleCaptureVersion = 1U",
            "kOgre14ParticleCapturedFrameVersion = 1U",
            "kOgre14ParticleMaterialClosureReceiptVersion = 1U",
            "Ogre14JoinedParticleFrame",
            "Ogre14ParticleCapturedFrame",
            "IOgre14ParticleCaptureFaultInjector",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header)
        for text in (self.header, self.source):
            self.assertNotIn("#include <Ogre", text)
            self.assertNotIn("Ogre::", text)
        self.assertIn(
            '#include "Ogre14ParticleCaptureSource.h"',
            RENDER_CONTRACTS.read_text(encoding="utf-8"),
        )

    def test_material_closure_is_a_resolved_catalog_receipt(self) -> None:
        for token in (
            "Ogre14ParticleMaterialClosureReceipt",
            "material_catalog_registry_id",
            "material_catalog_sequence",
            "translation_source_sequence",
            "const RenderAssetRegistry &material_catalog",
            "material_catalog.ResolveMaterial(closure.material)",
            "material-closure receipt does not resolve",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header + self.source)
        self.assertNotIn("material_closure_complete", self.header)
        for hostile_proof in (
            "mismatched material-closure registry receipt",
            "stale material-closure catalog sequence",
            "forged closure for a missing material revision",
            "catalog view outside declared lineage",
        ):
            with self.subTest(hostile_proof=hostile_proof):
                self.assertIn(hostile_proof, self.cpp_test)

    def test_lifecycle_caps_and_transaction_proofs_are_explicit(self) -> None:
        for token in (
            "Ogre14ParticleLifecycleOperation::CREATE",
            "Ogre14ParticleLifecycleOperation::UPDATE",
            "Ogre14ParticleLifecycleOperation::STOP",
            "Ogre14ParticleLifecycleOperation::DESTROY",
            "a destroyed particle-system identity may never return",
            "a removed particle identity may never return",
            "same-sequence replay changed authoritative contents",
            "maximum_particles_per_system",
            "maximum_particles_per_frame",
            "maximum_lifetime_particles",
            "maximum_lifetime_events",
            "maximum_payload_bytes_per_frame",
            "impl_.swap(candidate)",
            "BEFORE_COMMIT",
        ):
            with self.subTest(production_token=token):
                self.assertIn(token, self.header + self.source)
        for token in (
            "THROW_BAD_ALLOC",
            "THROW_UNEXPECTED",
            "SameSentinelOutput",
            "live-system cap+1",
            "aggregate frame-particle cap+1",
            "frame-event cap+1",
            "lifetime-system cap+1",
            "lifetime-particle cap+1",
            "initial non-emitting system",
            "continuously stopped system minted a new particle identity",
            "stopped system did not restart as UPDATE",
        ):
            with self.subTest(test_proof_token=token):
                self.assertIn(token, self.cpp_test)

    def test_logical_byte_accounting_is_transparent(self) -> None:
        for token in (
            "kOgre14ParticleLogicalAssetReferenceBytes",
            "kOgre14ParticleLogicalClosureReceiptBytes",
            "static_assert(kOgre14ParticleLogicalClosureReceiptBytes == 53U)",
            "static_assert(kOgre14ParticleLogicalStateBytes == 80U)",
            "static_assert(kOgre14ParticleLogicalSystemBytes == 83U)",
            "static_assert(kOgre14ParticleLogicalEventBytes == 17U)",
            "AddMultipliedChecked",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header + self.source)

    def test_cmake_ci_docs_and_provenance_are_closed(self) -> None:
        self.assertIn(
            "gfx/render/Ogre14ParticleCaptureSource.{h,cpp}",
            self.main_cmake,
        )
        self.assertGreaterEqual(
            self.main_cmake.count("gfx/render/Ogre14ParticleCaptureSource.cpp"),
            2,
        )
        for token in (
            "source/main/gfx/render/Ogre14ParticleCaptureSource.cpp",
            "ror_ogre14_particle_capture_source_tests",
            "Ogre14ParticleCaptureSourceTests.cpp",
            "NAME ogre14_particle_capture_source",
        ):
            with self.subTest(test_cmake_token=token):
                self.assertIn(token, self.test_cmake)
        for token in (
            "Ogre14ParticleCaptureSource.cpp",
            "Ogre14ParticleCaptureSourceTests.cpp",
            "ror_ogre14_particle_capture_source_tests",
            "NAME ror_ogre14_particle_capture_source",
        ):
            with self.subTest(probe_cmake_token=token):
                self.assertIn(token, self.probe_cmake)
        self.assertIn(
            "-R '^ror_ogre14_particle_capture_source$'", self.workflow
        )
        for token in (
            "Continuous OGRE 14 particle capture v1",
            "cannot represent a continuously retained particle",
            "versioned material",
            "closure receipt naming",
            "borrowed const `RenderAssetRegistry` view",
            "keep the view quiescent",
            "previously stopped but not destroyed",
            "`UPDATE`, not a second `CREATE`",
            "checked arithmetic",
            "not yet wired",
        ):
            with self.subTest(doc_token=token):
                self.assertIn(token, self.readme)
        for path in PROVENANCE_FILES:
            text = path.read_text(encoding="utf-8")
            with self.subTest(provenance=path.name):
                self.assertIn(
                    "tests/gfx/render/Ogre14ParticleCaptureSourceTests.cpp",
                    text,
                )
                self.assertIn(
                    "tests/tools/test_ogre14_particle_capture_contract.py",
                    text,
                )


if __name__ == "__main__":
    unittest.main()
