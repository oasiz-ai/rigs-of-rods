#!/usr/bin/env python3
"""Offline fail-closed contract for directional PSSM_3_CASCADE_V1."""

from __future__ import annotations

import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source/main/gfx/render/ogrenext"
PROBE_ROOT = REPOSITORY_ROOT / "tools/ogre_next_probe"
LOCK_PATH = PROBE_ROOT / "ogre-next-pssm-shadow-v1.lock.json"
CANONICAL_LOCK_PATH = PROBE_ROOT / "ogre-next.lock.json"
VERIFIER_PATH = PROBE_ROOT / "verify_pssm_shadow_source_closure.py"
SPEC = importlib.util.spec_from_file_location("verify_pssm_shadow_closure", VERIFIER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load the PSSM source-closure verifier")
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)


class OgreNextPssmShadowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        cls.policy_header = (RENDER_ROOT / "OgreNextPssmShadowPolicy.h").read_text(
            encoding="utf-8"
        )
        cls.policy = (RENDER_ROOT / "OgreNextPssmShadowPolicy.cpp").read_text(
            encoding="utf-8"
        )
        cls.frontend_header = (RENDER_ROOT / "OgreNextN1Frontend.h").read_text(
            encoding="utf-8"
        )
        cls.frontend = (RENDER_ROOT / "OgreNextN1Frontend.cpp").read_text(
            encoding="utf-8"
        )
        cls.smoke = (PROBE_ROOT / "src/pssm_shadow_smoke.cpp").read_text(
            encoding="utf-8"
        )
        cls.cmake = (PROBE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.workflow = (
            REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")

    def write_lock(self, value: object, root: Path) -> Path:
        path = root / "mutated.lock.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_source_closure_is_exact_and_matches_canonical_pin(self) -> None:
        validated = VERIFIER.validate_lock(LOCK_PATH, CANONICAL_LOCK_PATH)
        self.assertEqual(validated["ogre_next_commit"], VERIFIER.OGRE_NEXT_COMMIT)
        self.assertEqual(len(validated["sources"]), 27)
        self.assertEqual(
            validated["platform_policies"], VERIFIER.PLATFORM_POLICIES
        )
        reference = os.environ.get("ROR_OGRE_NEXT_REFERENCE_SOURCE_ROOT")
        if reference:
            VERIFIER.verify_source_root(validated, Path(reference))

    def test_lock_rejects_unknown_keys_reordering_and_hash_drift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-pssm-lock-") as temporary:
            root = Path(temporary)
            for label, mutate in (
                (
                    "unknown key",
                    lambda value: value.update(unreviewed=True),
                ),
                (
                    "source reordering",
                    lambda value: value["sources"].reverse(),
                ),
                (
                    "uppercase hash",
                    lambda value: value["sources"][0].update(
                        sha256=value["sources"][0]["sha256"].upper()
                    ),
                ),
                (
                    "path traversal",
                    lambda value: value["sources"][0].update(path="../escape"),
                ),
            ):
                with self.subTest(label=label):
                    value = copy.deepcopy(self.lock)
                    mutate(value)
                    with self.assertRaises(VERIFIER.VerificationError):
                        VERIFIER.validate_lock(
                            self.write_lock(value, root), CANONICAL_LOCK_PATH
                        )

    def test_shadow_mode_is_opt_in_and_rt4_only(self) -> None:
        self.assertRegex(
            self.frontend_header,
            r"directional_shadow_mode\s*=\s*\n?\s*OgreNextDirectionalShadowMode::DISABLED",
        )
        for token in (
            "PSSM_3_CASCADE_V1",
            "MODERN_PBR_RT4_V1",
            "requires exactly one shadow-casting directional light",
            "does not substitute local-light shadows",
            "requires a nonzero static/dynamic geometry mask",
            "requires exact 0.5 m near and 350 m far clip distances",
        ):
            self.assertIn(token, self.policy)
        self.assertIn("shadow_flags != 0U", self.policy)

    def test_reviewed_cascade_and_filter_values_are_literal_and_read_back(self) -> None:
        for declaration in (
            "kOgreNextPssmCascadeCount = 3U",
            "kOgreNextPssmNearMeters = 0.5F",
            "kOgreNextPssmFarMeters = 350.0F",
            "kOgreNextPssmLambda = 0.97F",
            "kOgreNextPssmSplitBlend = 0.125F",
            "kOgreNextPssmSplitPaddingMeters = 1.0F",
            "kOgreNextPssmStableCascadeCount = 1U",
            "kOgreNextPssmPcfKernelWidth = 4U",
            "kOgreNextPssmAtlasWidth = 2048U",
            "kOgreNextPssmAtlasHeight = 3072U",
            "{2048U, 2048U, 0U, 0U}",
            "{1024U, 1024U, 0U, 2048U}",
            "{1024U, 1024U, 1024U, 2048U}",
        ):
            self.assertIn(declaration, self.policy_header)
        for token in (
            "Ogre::PFG_D32_FLOAT",
            "Ogre::HlmsPbs::PCF_4x4",
            "createShadowNodeWithSettings",
            "validateAllNodes()",
            "SHADOW_NODE_FIRST_ONLY",
            "getPssmSplits(0U)",
            "getPssmBlends(0U)",
            "getPssmFade(0U)",
            "getNumActiveShadowCastingLights() != 1U",
        ):
            self.assertIn(token, self.frontend)

    def test_mesh_light_masks_receivers_and_ui_free_output_are_explicit(self) -> None:
        for token in (
            "MeshInstanceCastsShadowForLight",
            "ClassifyShadowGeometry",
            "MESH_INSTANCE_RECEIVES_SHADOW",
        ):
            self.assertIn(token, self.policy)
        for token in (
            "setCastShadows(casts_shadow)",
            "setReceiveShadows(false)",
            "mIncludeOverlays = false",
            "PixelFormat::RGBA16_FLOAT",
            "PixelFormat::RGBA8_SRGB",
        ):
            self.assertTrue(token in self.frontend or token in self.smoke)

    def test_native_isolation_proof_toggles_only_casting_and_fails_closed(self) -> None:
        for token in (
            '\\"only_changed_input\\": \\"occluder_instance_casts_shadow\\"',
            '\\"changed_pixels_outside_receiver\\": 0',
            '\\"changed_visible_occluder_pixels\\": 0',
            '\\"shadow_disabled_default_equals_explicit\\": true',
            "disabled_default == disabled_explicit",
            '\\"backend_substitution\\": false',
            "return kUnsupportedExitCode",
        ):
            self.assertIn(token, self.smoke)
        self.assertNotIn("fallback", self.smoke.lower())

    def test_standalone_build_and_three_platform_ci_are_required(self) -> None:
        for token in (
            "ror_ogre_next_pssm_shadow_smoke",
            "ror_ogre_next_pssm_shadow_report",
            "ror_ogre_next_pssm_source_closure_verify",
            "SKIP_RETURN_CODE 77",
            "verify_pssm_shadow_source_closure.py",
            "ogre-next-pssm-shadow-v1.lock.json",
        ):
            self.assertIn(token, self.cmake)
        for policy in VERIFIER.PLATFORM_POLICIES:
            self.assertIn(f"platform: {policy}", self.workflow)
        self.assertEqual(
            self.workflow.count("test_ogre_next_pssm_shadow_contract.py"), 2
        )
        for artifact in (
            "ror-ogre-next-pssm-shadow-report.json",
            "ror-ogre-next-pssm-shadow-isolation.bin",
            "bin/ror_ogre_next_pssm_shadow_smoke",
        ):
            self.assertIn(artifact, self.workflow)


if __name__ == "__main__":
    unittest.main()
