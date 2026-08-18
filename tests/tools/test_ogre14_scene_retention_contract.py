#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static closure checks for the retained static scene and road admission.

The retained static scene and the procedural-road adapter both live inside the
combined runtime's capture transaction, which needs a live Metal device to
execute. These checks lock the wiring the C++ suites cannot see: the retention
gate's exact conditions, the zero-growth refresh rule, the commit-skip
discipline, the generation-reset invalidation, and the single-call rule that
keeps roads and authored objects in one static inventory transaction.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RetainedStaticSceneContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.scene = (ROOT / "source/main/gfx/GfxScene.cpp").read_text()
        self.header = (ROOT / "source/main/gfx/GfxScene.h").read_text()

    def test_retention_gate_checks_every_invalidating_condition(self) -> None:
        anchor = "bool retention_hit ="
        start = self.scene.index(anchor)
        gate = self.scene[start : start + 1200]
        for condition in (
            "m_ogre14_static_retention_valid",
            "retention_objects != nullptr",
            "!m_ogre14_static_retention_meshes.empty()",
            "m_ogre14_static_retention_inventory",
            "m_ogre14_static_retention_cache_size",
            "new_frozen_material_decisions",
            "m_ogre14_static_retention_projections",
            "live_identity_count()",
            "cached_mesh_count()",
        ):
            self.assertIn(condition, gate, condition)

    def test_gate_scans_unadmitted_bounds_against_the_camera(self) -> None:
        # Full admission is unreachable on a 12 km map, so the gate must be a
        # bounds scan: reuse only when a walk could admit nothing new, and a
        # classifier fault falls back to the full walk.
        anchor = "for (const auto& unadmitted :"
        start = self.scene.index(anchor)
        scan = self.scene[start : start + 800]
        self.assertIn("ClassifyOgreNextDemoStaticBounds", scan)
        self.assertIn("if (!classified || within_capture_radius)", scan)
        self.assertIn("retention_hit = false", scan)

    def test_refresh_is_applied_only_on_commit(self) -> None:
        # A frame a later section discards must never leave the retained
        # scene describing state that was never published: the walk stashes
        # the refresh on the pending transaction and commit applies it.
        self.assertIn("pending->has_retention_refresh = true;", self.scene)
        commit = self.scene.index(
            "if (m_ogre14_pending_capture->has_retention_refresh)")
        guard = self.scene.index(
            "if (!m_ogre14_pending_capture->static_state_retained)")
        self.assertLess(guard, commit)
        self.assertIn(
            "m_ogre14_static_retention_valid = true;",
            self.scene[commit : commit + 1600],
        )

    def test_commit_skips_static_members_when_retained(self) -> None:
        anchor = "if (!m_ogre14_pending_capture->static_state_retained)"
        start = self.scene.index(anchor)
        block = self.scene[start : start + 800]
        for member in (
            "m_ogre14_static_identity_registry",
            "m_ogre14_static_mesh_cache",
            "m_ogre_next_demo_admitted_static_objects",
            "m_ogre14_procedural_road_inventory",
        ):
            self.assertIn(member, block, member)

    def test_generation_reset_invalidates_retention_and_roads(self) -> None:
        anchor = "void GfxScene::ResetOgre14GraphicsSceneGeneration()"
        start = self.scene.index(anchor)
        block = self.scene[start : start + 2200]
        for cleared in (
            "m_ogre14_static_retention_valid = false;",
            "m_ogre14_static_retention_assets.clear();",
            "m_ogre14_static_retention_meshes.clear();",
            "m_ogre14_static_retention_unadmitted.clear();",
            "m_ogre14_procedural_road_inventory =",
        ):
            self.assertIn(cleared, block, cleared)

    def test_roads_flow_through_the_single_inventory_call(self) -> None:
        # BuildOgre14GraphicsSceneStaticInventory replaces the registry's
        # live-key sets wholesale; a second call would tombstone the first
        # call's objects. Road sections must therefore be appended to the
        # object sections before the one call.
        insert = self.scene.index(
            "sections.insert(sections.end(), adapted_road_sections.begin()")
        build = self.scene.index(
            "BuildOgre14GraphicsSceneStaticInventory(\n"
            "        sections, identity_registry, assets, static_meshes)")
        self.assertLess(insert, build)
        self.assertEqual(
            self.scene.count(
                "RoR::Render::BuildOgre14GraphicsSceneStaticInventory("),
            1,
        )

    def test_unfinalized_roads_fail_the_capture_closed(self) -> None:
        self.assertIn(
            "static_meshes.procedural.unfinalized", self.scene)
        anchor = "HasFinalizedGraphicsSnapshot()"
        start = self.scene.index(anchor)
        window = self.scene[start - 400 : start + 600]
        self.assertIn("ValidationResult::Failure", window)

    def test_unadapted_procedural_geometry_stays_unsupported(self) -> None:
        anchor = "unsupported.procedural = !procedural_roads_adapted &&"
        self.assertEqual(self.scene.count(anchor), 1)

    def test_retention_state_is_declared_and_reset_together(self) -> None:
        for member in (
            "m_ogre14_static_retention_valid",
            "m_ogre14_static_retention_inventory",
            "m_ogre14_static_retention_cache_size",
            "m_ogre14_static_retention_frozen_decisions",
            "m_ogre14_static_retention_projections",
            "m_ogre14_static_retention_road_live",
            "m_ogre14_static_retention_road_cached",
            "m_ogre14_static_retention_unadmitted",
            "m_ogre14_procedural_road_inventory",
        ):
            self.assertIn(member, self.header, member)


if __name__ == "__main__":
    unittest.main()
