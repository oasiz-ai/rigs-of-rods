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


EXPECTED_ROLE_PATHS = {
    "exposure_parameter_construction": "Samples/2.0/Common/src/Utils/HdrUtils.cpp",
    "render_target_formats_and_pass_order": (
        "Samples/Media/2.0/scripts/materials/HDR/HDR.compositor"
    ),
    "shader_bindings_parameters_and_sampling": (
        "Samples/Media/2.0/scripts/materials/HDR/HDR.material"
    ),
    **{
        f"{role}_{language.lower()}": (
            f"Samples/Media/2.0/scripts/materials/HDR/{language}/{shader}.{extension}"
        )
        for language, extension in (
            ("GLSL", "glsl"),
            ("HLSL", "hlsl"),
            ("Metal", "metal"),
        )
        for role, shader in (
            ("bright_pass_gamma2_encode", "BrightPass_Start_ps"),
            ("horizontal_gamma2_blur", "BoxBlurH_ps"),
            ("vertical_gamma2_blur", "BoxBlurV_ps"),
            ("downscale03", "DownScale03_SumLumEnd_ps"),
            ("final_tone_mapping", "FinalToneMapping_ps"),
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

    def write_valid_fixture(self, root: Path) -> tuple[Path, Path]:
        source_root = root / "ogre"
        canonical = copy.deepcopy(self.canonical_lock)
        canonical["commit"] = "1" * 40
        (root / "ogre-next.lock.json").write_text(
            json.dumps(canonical), encoding="utf-8"
        )
        manifest = copy.deepcopy(self.manifest)
        manifest["ogre_next_commit"] = "1" * 40
        for entry in manifest["files"]:
            source_path = source_root / entry["path"]
            source_path.parent.mkdir(parents=True, exist_ok=True)
            payload = f"pinned fixture for {entry['role']}\n".encode()
            source_path.write_bytes(payload)
            entry["sha256"] = hashlib.sha256(payload).hexdigest()
        manifest_path = root / "ogre-next-hdr-reference.lock.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return manifest_path, source_root

    def test_real_manifest_is_bound_to_canonical_lock_and_complete(self) -> None:
        self.assertEqual(VERIFY.verify(MANIFEST_PATH), 18)
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
        self.assertEqual(
            {entry["role"]: entry["path"] for entry in entries},
            EXPECTED_ROLE_PATHS,
        )
        self.assertEqual(len({entry["role"] for entry in entries}), len(entries))
        self.assertEqual(len({entry["path"] for entry in entries}), len(entries))
        for entry in entries:
            self.assertRegex(entry["sha256"], r"^[0-9a-f]{64}$")

    def test_verifier_hashes_every_source_and_rejects_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest_path, source_root = self.write_valid_fixture(root)
            self.assertEqual(VERIFY.verify(manifest_path, source_root), 18)
            source_path = source_root / next(iter(EXPECTED_ROLE_PATHS.values()))
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

    def test_verifier_rejects_schema_and_role_forgery(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest_path, _ = self.write_valid_fixture(root)
            valid = json.loads(manifest_path.read_text(encoding="utf-8"))

            malformed = copy.deepcopy(valid)
            malformed["unexpected_attestation"] = "trusted"
            manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "root fields"):
                VERIFY.verify(manifest_path)

            malformed = copy.deepcopy(valid)
            malformed["name"] = "lookalike"
            manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "lock name"):
                VERIFY.verify(manifest_path)

            malformed = copy.deepcopy(valid)
            malformed["canonical_ogre_next_lock"] = "lookalike.json"
            manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "canonical sibling"):
                VERIFY.verify(manifest_path)

            malformed = copy.deepcopy(valid)
            malformed["files"][0]["role"] = "forged_semantics"
            manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "role/path mapping"):
                VERIFY.verify(manifest_path)

            malformed = copy.deepcopy(valid)
            malformed["files"][0]["role"], malformed["files"][1]["role"] = (
                malformed["files"][1]["role"],
                malformed["files"][0]["role"],
            )
            manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "role/path mapping"):
                VERIFY.verify(manifest_path)

    def test_verifier_rejects_duplicate_keys_and_exact_type_confusion(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest_path, _ = self.write_valid_fixture(root)
            valid_text = manifest_path.read_text(encoding="utf-8")
            duplicate_manifest = valid_text.replace(
                '"schema_version": 1,',
                '"schema_version": 1, "schema_version": 1,',
                1,
            )
            manifest_path.write_text(duplicate_manifest, encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "duplicate JSON"):
                VERIFY.verify(manifest_path)

            manifest_path, _ = self.write_valid_fixture(root)
            canonical_path = root / "ogre-next.lock.json"
            duplicate_canonical = canonical_path.read_text(encoding="utf-8").replace(
                '"commit": "1111111111111111111111111111111111111111",',
                '"commit": "1111111111111111111111111111111111111111", '
                '"commit": "1111111111111111111111111111111111111111",',
                1,
            )
            canonical_path.write_text(duplicate_canonical, encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "duplicate JSON"):
                VERIFY.verify(manifest_path)

            for field in (
                "schema_version",
                "analytic_behavior_version",
                "shader_behavior_version",
            ):
                manifest_path, _ = self.write_valid_fixture(root)
                malformed = json.loads(manifest_path.read_text(encoding="utf-8"))
                malformed[field] = True
                manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
                with self.assertRaisesRegex(VERIFY.VerificationError, "integer"):
                    VERIFY.verify(manifest_path)

            for field, value, message in (
                ("name", True, "nonempty string"),
                ("canonical_ogre_next_lock", True, "nonempty string"),
                ("ogre_next_commit", True, "nonempty string"),
                ("files", {}, "nonempty array"),
            ):
                manifest_path, _ = self.write_valid_fixture(root)
                malformed = json.loads(manifest_path.read_text(encoding="utf-8"))
                malformed[field] = value
                manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
                with self.assertRaisesRegex(VERIFY.VerificationError, message):
                    VERIFY.verify(manifest_path)

            for field in ("role", "path", "sha256"):
                manifest_path, _ = self.write_valid_fixture(root)
                malformed = json.loads(manifest_path.read_text(encoding="utf-8"))
                malformed["files"][0][field] = True
                manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
                with self.assertRaisesRegex(
                    VERIFY.VerificationError, "nonempty string"
                ):
                    VERIFY.verify(manifest_path)

            manifest_path, _ = self.write_valid_fixture(root)
            malformed = json.loads(manifest_path.read_text(encoding="utf-8"))
            malformed["files"][0] = True
            manifest_path.write_text(json.dumps(malformed), encoding="utf-8")
            with self.assertRaisesRegex(VERIFY.VerificationError, "must be an object"):
                VERIFY.verify(manifest_path)

            manifest_path, _ = self.write_valid_fixture(root)
            canonical_path = root / "ogre-next.lock.json"
            malformed_canonical = json.loads(canonical_path.read_text(encoding="utf-8"))
            malformed_canonical["commit"] = True
            canonical_path.write_text(
                json.dumps(malformed_canonical), encoding="utf-8"
            )
            with self.assertRaisesRegex(VERIFY.VerificationError, "nonempty string"):
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
            "CompareHdrAutoExposureReferences",
            "CompareHdrFinalToneMapReferences",
            "HdrCrossPrecisionComparison",
            "bloom_gamma2_encoded",
            "kHdrAnalyticShaderAbsoluteTolerance",
            "kHdrAnalyticShaderRelativeTolerance",
            "kHdrBinary32Gamma5",
        ):
            self.assertIn(token, self.header)
        self.assertNotIn("bloom_srgb", self.header)
        self.assertNotIn("Exact pre-framebuffer RGB", self.header)


if __name__ == "__main__":
    unittest.main()
