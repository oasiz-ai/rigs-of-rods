#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static closure checks for the road-material coordinator activation.

The live material coordinator needs an OGRE device and ContentManager
authority to execute, so its GfxScene wiring is locked textually: the
authored road2 declaration, the authenticated coordinator construction, the
observation/prepare/build ordering, native-audit (never closure-audit)
assignment, and the commit/discard/reset lifecycle the coordinator's
exclusive translator lease depends on.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RoadMaterialCoordinatorWiringTests(unittest.TestCase):
    def setUp(self) -> None:
        self.scene = (ROOT / "source/main/gfx/GfxScene.cpp").read_text()
        self.header = (ROOT / "source/main/gfx/GfxScene.h").read_text()

    def test_declaration_is_authored_not_inferred(self) -> None:
        anchor = "GfxScene::EnsureOgre14RoadMaterialCoordinator()"
        start = self.scene.index(anchor)
        body = self.scene[start : start + 2400]
        for token in (
            '"MaterialsRG"',
            '"road2"',
            "VERSIONED_COMPATIBILITY_TABLE",
            "ROUGH_DIELECTRIC_PBR",
            "BASE_COLOR_SRGB",
            "BuildOgre14LegacyMaterialSemanticRegistry",
        ):
            self.assertIn(token, body, token)

    def test_coordinator_uses_the_authenticated_overload(self) -> None:
        # The plain factory leaves the texture authority provider null and a
        # null provider admits only untextured captures. road2 is textured,
        # so ContentManager must be passed as both resolver and provider.
        anchor = "CreateOgre14LegacyLiveMaterialCoordinator("
        start = self.scene.index(anchor)
        call = self.scene[start : start + 300]
        self.assertIn("*content_manager", call)
        self.assertIn("m_ogre14_road_material_coordinator", call)

    def test_observation_prepare_build_ordering(self) -> None:
        resolve = self.scene.index("ResolveMaterialSemantics(")
        capture = self.scene.index("Render::CaptureOgre14LegacyNativeMaterial(")
        prepare = self.scene.index("->PrepareFrame(")
        build = self.scene.index(
            "Render::BuildOgre14ProceduralRoadInventory(\n"
            "                    road_captures, *road_translated_frame,")
        self.assertLess(resolve, capture)
        self.assertLess(capture, prepare)
        self.assertLess(prepare, build)

    def test_source_sequence_advances_exactly_once(self) -> None:
        anchor = "->PrepareFrame("
        start = self.scene.index(anchor)
        window = self.scene[start - 300 : start + 300]
        self.assertIn("source_sequence()", window)
        self.assertIn("+ 1U", window)

    def test_road_captures_receive_the_native_audit_owner(self) -> None:
        # The closure's own audit must never be assigned: the coordinator and
        # the prepared-material lookup both reject a shared control block as
        # laundering. Only the extractor-minted native owner is valid.
        anchor = "road_capture.exact_native_material_audit ="
        start = self.scene.index(anchor)
        assignment = self.scene[start : start + 200]
        self.assertIn("native_material_audit", assignment)
        self.assertNotIn("closure", assignment)

    def test_prepared_frame_rides_the_pending_transaction(self) -> None:
        self.assertIn("Render::Ogre14LegacyPreparedMaterialFrame "
                      "road_material_frame;", self.scene)
        self.assertIn(
            "pending->road_material_frame = std::move(road_material_frame);",
            self.scene)
        self.assertIn("pending->has_road_material_frame = true;", self.scene)
        self.assertIn("road_material_frame;", self.header)
        self.assertIn("has_road_material_frame = false;", self.header)

    def test_commit_happens_only_after_accepted_exposure(self) -> None:
        commit_fn = self.scene.index(
            "void GfxScene::CommitOgre14GraphicsSceneCapture() noexcept")
        commit_call = self.scene.index(
            "CommitPreparedFrameAfterAcceptedExposure(")
        self.assertLess(commit_fn, commit_call)
        # A refused commit must not wedge the exclusive translator lease.
        refusal_window = self.scene[commit_call : commit_call + 1200]
        self.assertIn("DiscardPreparedFrame()", refusal_window)

    def test_every_discard_funnel_reaches_the_coordinator(self) -> None:
        discard_fn = self.scene.index(
            "void GfxScene::DiscardOgre14GraphicsSceneCapture() noexcept")
        discard_body = self.scene[discard_fn : discard_fn + 500]
        self.assertIn("DiscardPreparedFrame();", discard_body)
        self.assertIn("m_ogre14_pending_capture.reset();", discard_body)
        # The in-function guard covers early returns before the pending
        # stash; it is released exactly where the material guard is.
        self.assertIn("Ogre14RoadMaterialFramePendingGuard "
                      "road_material_frame_guard(", self.scene)
        self.assertIn("road_material_frame_guard.Arm();", self.scene)
        self.assertIn("road_material_frame_guard.Release();", self.scene)

    def test_generation_reset_destroys_the_coordinator(self) -> None:
        anchor = "void GfxScene::ResetOgre14GraphicsSceneGeneration()"
        start = self.scene.index(anchor)
        body = self.scene[start : start + 2600]
        self.assertIn("m_ogre14_road_material_coordinator.reset();", body)

    def test_empty_road_inventory_skips_material_translation(self) -> None:
        anchor = "procedural_roads_adapted && road_captures.empty()"
        self.assertEqual(self.scene.count(anchor), 1)

    def test_combined_producer_prepares_the_road_material(self) -> None:
        terrain = (ROOT / "source/main/terrain/Terrain.cpp").read_text()
        anchor = "void PrepareCombinedRuntimeRoadMaterial("
        start = terrain.index(anchor)
        body = terrain[start : start + 4200]
        for token in (
            '"road2", "MaterialsRG"',
            "cityworld_road2_basecolor.png",
            "hwGammaCorrection:",
            "setHardwareGammaEnabled(true)",
            "setAmbient",
            "setSpecular",
            "setSelfIllumination",
            "setShininess(0.0f)",
        ):
            self.assertIn(token, body, token)
        # Preparation precedes road finalization; RTSS prewarm is skipped in
        # the combined producer so no generated technique can reach either
        # the finish-time snapshot or the capture-time extractor.
        prepare_call = terrain.index(
            "PrepareCombinedRuntimeRoadMaterial(this->")
        objects = terrain.index("this->loadTerrainObjects();")
        self.assertLess(prepare_call, objects)
        prewarm = terrain.index("PrewarmRTShaderTechniques();")
        gate = terrain.rindex(
            "#if !defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)", 0, prewarm)
        self.assertLess(gate, prewarm)

    def test_combined_producer_never_registers_the_rtss_resolver(self) -> None:
        app_context = (ROOT / "source/main/AppContext.cpp").read_text()
        listener = app_context.index(
            "new OgreBites::SGTechniqueResolverListener")
        gate = app_context.rindex(
            "#if !defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)", 0, listener)
        self.assertLess(gate, listener)


if __name__ == "__main__":
    unittest.main()
