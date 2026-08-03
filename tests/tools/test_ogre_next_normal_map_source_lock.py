#!/usr/bin/env python3
"""Strict provenance checks for the RT4 normal-map Ogre source owners."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
RUNNER_SPEC = importlib.util.spec_from_file_location(
    "run_ogre_next_probe_for_normal_lock_tests", RUNNER_PATH
)
if RUNNER_SPEC is None or RUNNER_SPEC.loader is None:
    raise RuntimeError("could not load the OGRE-Next probe runner")
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
RUNNER_SPEC.loader.exec_module(RUNNER)


class OgreNextNormalMapSourceLockTests(unittest.TestCase):
    def write_lock(self, root: Path, lock: dict) -> Path:
        path = root / "normal-map.lock.json"
        path.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
        return path

    def load_mutation(self, lock: dict) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-normal-lock-") as temp:
            path = self.write_lock(Path(temp), lock)
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            with mock.patch.object(
                RUNNER, "NORMAL_MAP_SOURCE_LOCK_SHA256", digest
            ):
                RUNNER.load_normal_map_source_lock(path)

    def test_reviewed_lock_is_canonical_and_exact(self) -> None:
        lock = RUNNER.load_normal_map_source_lock()
        self.assertEqual(len(lock["sources"]), 23)
        self.assertTrue(
            {
                "normal_vertex_tbn_shader",
                "normal_uv_modifier",
                "metal_vertex_input",
                "glsl_vertex_input",
                "hlsl_vertex_input",
                "metal_sampling_precision",
                "glsl_sampling_precision",
                "hlsl_sampling_precision",
                "hlms_precision_default",
                "texture_box_row_layout",
                "image_api",
                "image_row_layout_implementation",
            }.issubset({entry["role"] for entry in lock["sources"]})
        )
        self.assertEqual(lock["contract"]["pbs_slot"], "PBSM_NORMAL")
        self.assertEqual(
            lock["contract"]["decoded_b_tolerance"],
            {"numerator": 1, "denominator": 255},
        )
        self.assertFalse(lock["contract"]["occlusion_admitted"])

    def test_duplicate_keys_fail_before_semantic_access(self) -> None:
        source = RUNNER.NORMAL_MAP_SOURCE_LOCK_PATH.read_text(encoding="utf-8")
        duplicate = source.replace(
            '  "schema": "ror.ogre_next_rt4_normal_map_source_lock.v1",',
            '  "schema": "ror.ogre_next_rt4_normal_map_source_lock.v1",\n'
            '  "schema": "ror.ogre_next_rt4_normal_map_source_lock.v1",',
            1,
        )
        with tempfile.TemporaryDirectory(prefix="ror-normal-duplicate-") as temp:
            path = Path(temp) / "duplicate.json"
            path.write_text(duplicate, encoding="utf-8")
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            with mock.patch.object(
                RUNNER, "NORMAL_MAP_SOURCE_LOCK_SHA256", digest
            ), self.assertRaisesRegex(RUNNER.ProbeError, "duplicate key"):
                RUNNER.load_normal_map_source_lock(path)

    def test_wrong_schema_and_json_scalar_types_fail_closed(self) -> None:
        lock = RUNNER.load_normal_map_source_lock()
        wrong_schema = copy.deepcopy(lock)
        wrong_schema["schema"] = "ror.ogre_next_rt4_normal_map_source_lock.v2"
        with self.assertRaisesRegex(RUNNER.ProbeError, "schema changed"):
            self.load_mutation(wrong_schema)

        wrong_type = copy.deepcopy(lock)
        wrong_type["contract"]["normal_scale"] = True
        with self.assertRaisesRegex(RUNNER.ProbeError, "semantic contract"):
            self.load_mutation(wrong_type)

    def test_duplicate_and_unsafe_owner_paths_fail_closed(self) -> None:
        lock = RUNNER.load_normal_map_source_lock()
        duplicate = copy.deepcopy(lock)
        duplicate["sources"][1]["role"] = duplicate["sources"][0]["role"]
        with self.assertRaisesRegex(RUNNER.ProbeError, "duplicated"):
            self.load_mutation(duplicate)

        unsafe = copy.deepcopy(lock)
        unsafe["sources"][0]["path"] = "../800.PixelShader_piece_ps.any"
        with self.assertRaisesRegex(RUNNER.ProbeError, "path is unsafe"):
            self.load_mutation(unsafe)

    def test_lock_and_owner_hashes_are_verified(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-normal-hash-") as temp:
            copied = Path(temp) / "normal-map.lock.json"
            copied.write_bytes(RUNNER.NORMAL_MAP_SOURCE_LOCK_PATH.read_bytes())
            RUNNER.load_normal_map_source_lock(copied)
            copied.write_bytes(copied.read_bytes() + b" ")
            with self.assertRaisesRegex(RUNNER.ProbeError, "lock changed"):
                RUNNER.load_normal_map_source_lock(copied)

        lock = RUNNER.load_normal_map_source_lock()
        with tempfile.TemporaryDirectory(prefix="ror-normal-source-") as temp:
            source_root = Path(temp)
            for owner in lock["sources"]:
                source = source_root / owner["path"]
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_bytes(owner["role"].encode("utf-8"))

            def mismatched_owner(path: Path) -> str:
                if path == RUNNER.NORMAL_MAP_SOURCE_LOCK_PATH:
                    return RUNNER.NORMAL_MAP_SOURCE_LOCK_SHA256
                return "0" * 64

            with mock.patch.object(
                RUNNER, "sha256_file", side_effect=mismatched_owner
            ), self.assertRaisesRegex(RUNNER.ProbeError, "hash mismatch"):
                RUNNER.load_normal_map_source_lock(source_root=source_root)

    def test_contract_only_matches_with_and_without_python_optimization(self) -> None:
        outputs = []
        for optimization in ([], ["-O"]):
            completed = subprocess.run(
                [
                    sys.executable,
                    *optimization,
                    str(RUNNER_PATH),
                    "--validate-contract-only",
                ],
                cwd=REPOSITORY_ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(completed.stdout)
            self.assertEqual(report["status"], "pass")
            self.assertEqual(
                report["normal_map_source_lock_sha256"],
                RUNNER.NORMAL_MAP_SOURCE_LOCK_SHA256,
            )
            self.assertEqual(report["normal_map_source_owner_count"], 23)
            outputs.append(report)
        self.assertEqual(outputs[0], outputs[1])


if __name__ == "__main__":
    unittest.main()
