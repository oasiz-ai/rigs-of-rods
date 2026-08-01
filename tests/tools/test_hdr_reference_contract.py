#!/usr/bin/env python3
"""Fail-closed tests for the Ogre-Next HDR numerical-reference contract."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source" / "main" / "gfx" / "render"
PROBE_ROOT = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
MANIFEST_PATH = PROBE_ROOT / "ogre-next-hdr-reference.lock.json"
CANONICAL_LOCK_PATH = PROBE_ROOT / "ogre-next.lock.json"
VERIFY_PATH = REPOSITORY_ROOT / "tools" / "verify_hdr_reference_sources.py"
SPEC = importlib.util.spec_from_file_location("verify_hdr_reference_sources", VERIFY_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load HDR verifier module from {VERIFY_PATH}")
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


EXPECTED_PATHS = {
    "Samples/2.0/Common/src/Utils/HdrUtils.cpp",
    "Samples/Media/2.0/scripts/materials/HDR/HDR.compositor",
    *{
        f"Samples/Media/2.0/scripts/materials/HDR/{language}/{shader}.{extension}"
        for language, extension in (
            ("GLSL", "glsl"),
            ("HLSL", "hlsl"),
            ("Metal", "metal"),
        )
        for shader in (
            "BrightPass_Start_ps",
            "BoxBlurH_ps",
            "BoxBlurV_ps",
            "DownScale03_SumLumEnd_ps",
            "FinalToneMapping_ps",
        )
    },
}


class HdrReferenceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.canonical_lock = json.loads(
            CANONICAL_LOCK_PATH.read_text(encoding="utf-8")
        )
        cls.header = (RENDER_ROOT / "HdrReference.h").read_text(encoding="utf-8")

    def test_real_manifest_is_bound_to_canonical_lock_and_complete(self) -> None:
        self.assertEqual(VERIFY.verify(MANIFEST_PATH), 17)
        self.assertEqual(
            self.manifest["ogre_next_commit"], self.canonical_lock["commit"]
        )
        header_commit = re.search(
            r'kHdrReferenceOgreNextCommit\[\]\s*=\s*"([0-9a-f]{40})"',
            self.header,
        )
        self.assertIsNotNone(header_commit)
        if header_commit is None:
            self.fail("public HDR commit constant is missing")
        self.assertEqual(header_commit.group(1), self.canonical_lock["commit"])
        self.assertEqual(self.manifest["canonical_ogre_next_lock"], "ogre-next.lock.json")
        entries = self.manifest["files"]
        self.assertEqual({entry["path"] for entry in entries}, EXPECTED_PATHS)
        self.assertEqual(len({entry["role"] for entry in entries}), len(entries))
        self.assertEqual(len({entry["path"] for entry in entries}), len(entries))
        for entry in entries:
            self.assertRegex(entry["sha256"], r"^[0-9a-f]{64}$")

    def test_verifier_hashes_every_source_and_rejects_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source_root = root / "ogre"
            source_path = source_root / "Samples" / "HDR" / "shader.test"
            source_path.parent.mkdir(parents=True)
            source_path.write_bytes(b"pinned HDR source\n")
            digest = hashlib.sha256(source_path.read_bytes()).hexdigest()
            canonical = copy.deepcopy(self.canonical_lock)
            canonical["commit"] = "1" * 40
            (root / "ogre-next.lock.json").write_text(
                json.dumps(canonical), encoding="utf-8"
            )
            manifest = {
                "schema_version": 1,
                "name": "fixture",
                "canonical_ogre_next_lock": "ogre-next.lock.json",
                "ogre_next_commit": "1" * 40,
                "analytic_behavior_version": 1,
                "shader_behavior_version": 1,
                "files": [
                    {
                        "role": "fixture",
                        "path": "Samples/HDR/shader.test",
                        "sha256": digest,
                    }
                ],
            }
            manifest_path = root / "ogre-next-hdr-reference.lock.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(VERIFY.verify(manifest_path, source_root), 1)
            source_path.write_bytes(b"tampered\n")
            with self.assertRaisesRegex(VERIFY.VerificationError, "hash mismatch"):
                VERIFY.verify(manifest_path, source_root)

    def test_verifier_rejects_commit_drift_and_noncanonical_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "ogre-next.lock.json").write_text(
                CANONICAL_LOCK_PATH.read_text(encoding="utf-8"), encoding="utf-8"
            )
            manifest = copy.deepcopy(self.manifest)
            manifest["ogre_next_commit"] = "2" * 40
            manifest_path = root / "ogre-next-hdr-reference.lock.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "does not match"):
                VERIFY.verify(manifest_path)

            manifest = copy.deepcopy(self.manifest)
            manifest["schema_version"] = True
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "must be an integer"):
                VERIFY.verify(manifest_path)

            manifest = copy.deepcopy(self.manifest)
            manifest["files"][0]["path"] = "../escape"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "normalized relative"):
                VERIFY.verify(manifest_path)

    def test_every_numerical_reference_is_shipping_and_strict_fp(self) -> None:
        cmake = (REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )

        def cmake_set(name: str) -> str:
            marker = f"set({name}\n"
            start = cmake.index(marker) + len(marker)
            end = cmake.index("\n        )", start)
            return cmake[start:end]

        shipping = cmake_set("SOURCE_FILES")
        renderer_contract = cmake_set("ROR_RENDER_CONTRACT_SOURCES")
        strict_fp = cmake_set("ROR_RENDER_CONTRACT_STRICT_FP_SOURCES")
        references = sorted(RENDER_ROOT.glob("*Reference.cpp"))
        self.assertTrue(references, "expected at least one numerical reference")
        for path in references:
            relative = f"gfx/render/{path.name}"
            grouped = f"gfx/render/{path.stem}.{{h,cpp}}"
            self.assertTrue(
                relative in shipping or grouped in shipping,
                f"{path.name} is absent from the shipping source manifest",
            )
            self.assertIn(relative, renderer_contract)
            self.assertIn(relative, strict_fp)

    def test_public_api_distinguishes_analytic_shader_and_gamma2(self) -> None:
        for token in (
            "kHdrAnalyticReferenceVersion",
            "kHdrShaderReferenceVersion",
            "EvaluateHdrAnalyticAutoExposure",
            "EvaluateHdrShaderAutoExposure",
            "EvaluateHdrAnalyticFinalToneMap",
            "EvaluateHdrShaderFinalToneMap",
            "bloom_gamma2_encoded",
            "kHdrAnalyticShaderAbsoluteTolerance",
            "kHdrAnalyticShaderRelativeTolerance",
        ):
            self.assertIn(token, self.header)
        self.assertNotIn("bloom_srgb", self.header)
        self.assertNotIn("Exact pre-framebuffer RGB", self.header)


if __name__ == "__main__":
    unittest.main()
