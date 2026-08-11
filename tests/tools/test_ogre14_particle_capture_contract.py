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
SNAPSHOT_PRODUCER = (
    REPOSITORY_ROOT
    / "source/main/gfx/render/GraphicsSceneSnapshotProducer.cpp"
)
CPP_TEST = (
    REPOSITORY_ROOT
    / "tests/gfx/render/Ogre14ParticleCaptureSourceTests.cpp"
)
README = REPOSITORY_ROOT / "source/main/gfx/render/README.md"
MAIN_CMAKE = REPOSITORY_ROOT / "source/main/CMakeLists.txt"
TEST_CMAKE = REPOSITORY_ROOT / "tests/CMakeLists.txt"
PROBE_CMAKE = REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt"
WORKFLOW = REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
OGRE14_WORKFLOW = REPOSITORY_ROOT / ".github/workflows/ogre14-native.yml"
MACOS_WORKFLOW = REPOSITORY_ROOT / ".github/workflows/macos-native.yml"
GFX_SCENE = REPOSITORY_ROOT / "source/main/gfx/GfxScene.cpp"
DUST_SCRIPT = REPOSITORY_ROOT / "resources/particles/dust.particle"
MATERIAL_SOURCE = (
    REPOSITORY_ROOT
    / "source/main/gfx/ogre14/detail/OgreNextDemoMaterialSource.cpp"
)
PRESENTER_HEADER = (
    REPOSITORY_ROOT
    / "source/main/system/RendererOgreNextInProcessPresenter.h"
)
PRESENTER_SOURCE = PRESENTER_HEADER.with_suffix(".cpp")
MAIN_SOURCE = REPOSITORY_ROOT / "source/main/main.cpp"
EMBEDDED_CMAKE = REPOSITORY_ROOT / "cmake/ogre_next_embedded/CMakeLists.txt"
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
        cls.snapshot_producer = SNAPSHOT_PRODUCER.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")
        cls.readme = README.read_text(encoding="utf-8")
        cls.main_cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        cls.test_cmake = TEST_CMAKE.read_text(encoding="utf-8")
        cls.probe_cmake = PROBE_CMAKE.read_text(encoding="utf-8")
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")
        cls.ogre14_workflow = OGRE14_WORKFLOW.read_text(encoding="utf-8")
        cls.macos_workflow = MACOS_WORKFLOW.read_text(encoding="utf-8")
        cls.gfx_scene = GFX_SCENE.read_text(encoding="utf-8")
        cls.dust_script = DUST_SCRIPT.read_text(encoding="utf-8")
        cls.material_source = MATERIAL_SOURCE.read_text(encoding="utf-8")
        cls.presenter = (
            PRESENTER_HEADER.read_text(encoding="utf-8")
            + PRESENTER_SOURCE.read_text(encoding="utf-8")
        )
        cls.main_source = MAIN_SOURCE.read_text(encoding="utf-8")
        cls.embedded_cmake = EMBEDDED_CMAKE.read_text(encoding="utf-8")

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
            "FinalizeSceneGeneration",
            "final particle tombstones exceed the event identity cap",
        ):
            with self.subTest(production_token=token):
                self.assertIn(token, self.header + self.source)
        for token in (
            "particle_capture_source.FinalizeSceneGeneration(",
            "explicit particle tombstones against that new empty catalog sequence",
        ):
            with self.subTest(finalization_token=token):
                self.assertIn(token, self.snapshot_producer)
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
            "static_assert(kOgre14ParticleLogicalClosureReceiptBytes == 118U)",
            "static_assert(kOgre14ParticleLogicalStateBytes == 80U)",
            "static_assert(kOgre14ParticleLogicalSystemBytes == 149U)",
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
        self.assertIn(
            "-R '^ror_ogre_next_n1_particle_runtime$'", self.workflow
        )
        for native_workflow in (
            self.ogre14_workflow,
            self.macos_workflow,
        ):
            self.assertIn(
                "-R '^ogre_next_n1_particle_runtime$'", native_workflow
            )
        self.assertIn(
            '"${_ror_render_root}/ogrenext/OgreNextN1ParticleRuntime.cpp"',
            self.embedded_cmake,
        )
        self.assertIn(
            '"${_ror_render_root}/Ogre14ParticleCaptureSource.cpp"',
            self.embedded_cmake,
        )
        self.assertGreaterEqual(
            self.probe_cmake.count(
                '"${_ror_render_root}/Ogre14ParticleCaptureSource.cpp"'
            ),
            2,
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
            "no emitter definition",
            "Sparks, ripple,",
            "texture pixels are never read back",
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

    def test_live_gfx_scene_capture_is_realized_and_joined(self) -> None:
        update_begin = self.gfx_scene.index("void GfxScene::UpdateScene(")
        capture_begin = self.gfx_scene.index(
            "GfxScene::CaptureOgre14GraphicsScene(", update_begin
        )
        update = self.gfx_scene[update_begin:capture_begin]
        particle_update = update.index(
            "particle_system->_update(particle_frame_seconds);"
        )
        joined_publish = update.index(
            "m_ogre14_post_update_scene_epoch = "
            "m_ogre14_joined_buffer_epoch;"
        )
        self.assertLess(particle_update, joined_publish)
        for token in (
            "getMovableObjects(Ogre::MOT_PARTICLE_SYSTEM)",
            '"Dust tracks/Dust "',
            '"tracks/SmokeMat"',
            "Ogre::BBT_POINT",
            "Ogre::BBR_TEXCOORD",
            "getUseAccurateFacing()",
            "getKeepParticlesInLocalSpace()",
            "getTextureStacksAndSlices()",
            "excluded_sparks_systems",
            "excluded_ripple_systems",
            "excluded_timing_modes",
            "m_ogre14_particle_update_timings",
            "latest_effective_interval_seconds",
            "getDefaultIterationInterval()",
            "CanRetainOgre14ParticlePoolIdentity(",
            "native_particle->mTimeToLive",
            "DecodeOgre14ParticleColourBytes(",
            "candidate.frame.continuous_particles",
        ):
            with self.subTest(gfx_token=token):
                self.assertIn(token, self.gfx_scene)
        for token in (
            'exact_section_key == "particle/tracks/Dust"',
            "allow_continuous_dust",
            "owner.exact_continuous_dust != observed_continuous_dust",
        ):
            with self.subTest(material_source_token=token):
                self.assertIn(token, self.material_source)
        self.assertNotIn(
            "setAsRGBA(native_particle->mColour)", self.gfx_scene
        )

    def test_inactive_pool_defers_first_identity_and_material_atomically(
        self,
    ) -> None:
        for token in (
            "Ogre14ParticleSystemAdmissionDecision",
            "DEFER_INACTIVE_FIRST_OBSERVATION",
            "ADMIT_FIRST_ACTIVITY",
            "RETAIN_ADMITTED",
            "ClassifyOgre14ParticleSystemAdmission(",
        ):
            with self.subTest(admission_token=token):
                self.assertIn(token, self.header + self.source)
        for proof in (
            "unseen stopped empty system was not deferred",
            "unseen emitting empty system was not admitted",
            "unseen stopped system with a live particle was not admitted",
            "admitted stopped empty system was not retained",
            "empty deferred inventory minted first-activity lifecycle state",
            "failed first CREATE consumed identity, sequence, event, or output state",
            "retry did not atomically publish exact zero-readback first CREATE",
            "stopped empty system did not resume as UPDATE under one identity",
        ):
            with self.subTest(test_proof=proof):
                self.assertIn(proof, self.cpp_test)

        capture_begin = self.gfx_scene.index(
            "GfxScene::CaptureOgre14GraphicsScene("
        )
        commit_begin = self.gfx_scene.index(
            "void GfxScene::CommitOgre14GraphicsSceneCapture()", capture_begin
        )
        capture = self.gfx_scene[capture_begin:commit_begin]
        emitting = capture.index("bool native_emitting = false;")
        classification = capture.index(
            "ClassifyOgre14ParticleSystemAdmission(", emitting
        )
        deferral = capture.index(
            "DEFER_INACTIVE_FIRST_OBSERVATION", classification
        )
        identity_allocation = capture.index(".next_system_id++", deferral)
        material_gate = capture.index(
            "if (!captured_dust_systems.empty())", identity_allocation
        )
        material_projection = capture.index(
            '.TryProject("particle/tracks/Dust"', material_gate
        )
        pending_publication = capture.index(
            "m_ogre14_pending_capture = std::move(pending);",
            material_projection,
        )
        self.assertLess(emitting, classification)
        self.assertLess(classification, deferral)
        self.assertLess(deferral, identity_allocation)
        self.assertLess(identity_allocation, material_gate)
        self.assertLess(material_gate, material_projection)
        self.assertLess(material_projection, pending_publication)
        self.assertIn("deferred_inactive_systems = 0U;", capture)
        self.assertIn(".deferred_inactive_systems;", capture)
        coverage = capture[
            capture.index(
                "if (pending->particle_capture_state.captured_systems +"
            ) : capture.index(
                "if (pending->particle_capture_state.next_source_sequence =="
            )
        ]
        self.assertIn(".deferred_inactive_systems +", coverage)
        self.assertIn("deferred_inactive_systems={} ", self.gfx_scene)

        commit = self.gfx_scene[commit_begin:]
        self.assertIn(
            "swap(m_ogre14_particle_capture_state,", commit
        )
        self.assertIn(
            "m_ogre14_pending_capture->particle_capture_state", commit
        )
        self.assertIn(
            "void GfxScene::DiscardOgre14GraphicsSceneCapture() noexcept",
            commit,
        )
        self.assertIn("m_ogre14_pending_capture.reset();", commit)

    def test_shipped_dust_fixture_uses_default_texcoord_rotator(self) -> None:
        dust = self.dust_script.split(
            "particle_system tracks/Dust", 1
        )[1].split("// sparks", 1)[0]
        self.assertRegex(dust, r"billboard_type\s+point")
        self.assertIn("affector Rotator", dust)
        self.assertNotIn("billboard_rotation_type", dust)
        runtime_source = (
            REPOSITORY_ROOT
            / "source/main/gfx/render/ogrenext/"
            "OgreNextN1ParticleRuntime.cpp"
        ).read_text(encoding="utf-8")
        runtime_test = (
            REPOSITORY_ROOT
            / "tests/gfx/render/OgreNextN1ParticleRuntimeTests.cpp"
        ).read_text(encoding="utf-8")
        proof = (
            self.header
            + self.source
            + self.cpp_test
            + self.gfx_scene
            + runtime_source
            + runtime_test
        )
        for token in (
            "Ogre14ParticleBillboardRotationMode::TEXTURE_COORDINATES",
            "BuildOgre14ParticleTextureCoordinateQuad(",
            "native RGBA bytes changed channel order or particle alpha",
            "asymmetric Rotator UV golden changed",
        ):
            with self.subTest(token=token):
                self.assertIn(token, proof)

    def test_combined_frontend_exposes_actual_lifetime_audit(self) -> None:
        for token in (
            "ContinuousParticleAudit() const noexcept",
            "frontend->QueryParticleRuntimeAudit()",
            "native_batch_creates",
            "native_particles_submitted",
            "native_state_readbacks",
        ):
            with self.subTest(presenter_token=token):
                self.assertIn(token, self.presenter)
        for token in (
            ".ContinuousParticleAudit()",
            '"distinct_source_textures={} "',
            '"native_batch_creates={} "',
            '"native_state_readbacks={} "',
        ):
            with self.subTest(main_token=token):
                self.assertIn(token, self.main_source)
        self.assertNotIn("reported_by_frontend", self.main_source)


if __name__ == "__main__":
    unittest.main()
