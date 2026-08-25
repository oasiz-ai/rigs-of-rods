#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_PATH = (
    REPOSITORY_ROOT / ".github/workflows/content-provenance.yml"
)
SELF_PATH = "tests/tools/test_content_provenance_workflow_contract.py"
POSTPROCESS_RESOURCE_TEST_PATH = (
    "tests/tools/test_postprocess_resources.py"
)
POSTPROCESS_RUNTIME_TEST_PATH = (
    "tests/tools/test_postprocess_runtime_contract.py"
)
MATERIAL_SCRIPT_AUDIT_TEST_PATH = (
    "tests/tools/test_audit_ogre14_material_scripts.py"
)
MATERIAL_FAMILY_CLASSIFIER_TEST_PATH = (
    "tests/tools/test_classify_cityworld_material_families.py"
)
NATIVE_RENDER_ASSET_TEST_PATH = "tests/tools/test_native_render_asset.py"
NATIVE_A1_COURSE_TEST_PATH = "tests/tools/test_native_a1_course.py"
NATIVE_RENDER_ASSET_SOURCE_PATH = (
    "content-source/native_render/a0_road_tile_12m/"
    "rorng_a0_road_tile_12m.native.json"
)
NATIVE_A1_COURSE_SOURCE_PATH = (
    "content-source/native_render/a1_native_course_60m/"
    "rorng_a1_native_course_60m.native.json"
)


class ContentProvenanceWorkflowContractTests(unittest.TestCase):
    def test_full_matrix_has_a_bounded_viable_timeout(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")

        self.assertEqual(workflow.count("timeout-minutes: 90"), 1)
        self.assertNotIn("timeout-minutes: 40", workflow)
        self.assertNotIn("timeout-minutes: 10", workflow)
        self.assertEqual(workflow.count(SELF_PATH), 2)
        self.assertEqual(
            workflow.count(POSTPROCESS_RESOURCE_TEST_PATH),
            2,
        )
        self.assertEqual(
            workflow.count(POSTPROCESS_RUNTIME_TEST_PATH),
            2,
        )
        self.assertEqual(
            workflow.count(MATERIAL_SCRIPT_AUDIT_TEST_PATH),
            2,
        )
        self.assertEqual(
            workflow.count(MATERIAL_FAMILY_CLASSIFIER_TEST_PATH),
            2,
        )
        self.assertEqual(workflow.count(NATIVE_RENDER_ASSET_TEST_PATH), 2)
        self.assertEqual(workflow.count(NATIVE_A1_COURSE_TEST_PATH), 2)
        for runner in ("ubuntu-22.04", "windows-2025", "macos-15"):
            self.assertIn(f"- {runner}", workflow)
        for version in ('- "3.11"', '- "3.14"'):
            self.assertIn(version, workflow)
        self.assertIn(
            "Run optimized provenance, asset, and renderer tests",
            workflow,
        )
        self.assertIn(
            "Verify checked-in CityWorld provenance and release gate",
            workflow,
        )
        self.assertIn("Verify checked forward-native A0 package", workflow)
        self.assertIn("Verify checked forward-native A1 package", workflow)
        self.assertEqual(
            workflow.count("tools/validate_native_render_asset.py"),
            2,
        )
        self.assertEqual(
            workflow.count("tools/compile_native_render_asset.py"),
            2,
        )
        self.assertEqual(workflow.count(NATIVE_RENDER_ASSET_SOURCE_PATH), 2)
        self.assertEqual(workflow.count(NATIVE_A1_COURSE_SOURCE_PATH), 2)
        self.assertEqual(workflow.count("--validate-checked"), 13)


if __name__ == "__main__":
    unittest.main()
