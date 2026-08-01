#!/usr/bin/env python3
"""Unit tests for the exact OGRE-Next CI artifact-set gate."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
SPEC = importlib.util.spec_from_file_location("verify_ogre_next_artifacts", SCRIPT_PATH)
assert SPEC and SPEC.loader
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


class OgreNextArtifactSetTests(unittest.TestCase):
    def write_baseline(self, root: Path) -> None:
        for name in VERIFY.REQUIRED_ARTIFACTS:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"baseline")

    def write_metal_n2(self, root: Path, status: str) -> None:
        executable_path = root / VERIFY.METAL_N2_REQUIRED_ARTIFACTS[2]
        executable_path.parent.mkdir(parents=True, exist_ok=True)
        executable_path.write_bytes(b"attested-metal-n2")
        source = {
            "ror_commit": "1" * 40,
            "ror_ref": "codex/test",
            "relevant_source_clean": True,
            "relevant_source_manifest_sha256": "2" * 64,
        }
        report = {
            "schema": "ror.ogre_next_metal_rt_n2.v3",
            "status": status,
            "provenance": {
                **source,
                "build_artifact": executable_path.name,
                "build_artifact_bytes": executable_path.stat().st_size,
                "build_artifact_sha256": VERIFY.sha256_file(executable_path),
            },
        }
        report_path = root / VERIFY.METAL_N2_REQUIRED_ARTIFACTS[0]
        report_path.write_text(json.dumps(report) + "\n", encoding="utf-8")
        probe_path = root / VERIFY.METAL_N2_PROBE_ARTIFACT
        if status == "pass":
            probe_path.write_bytes(b"12345678")

        def entry(path: Path) -> dict[str, object]:
            return {
                "path": path.name,
                "bytes": path.stat().st_size,
                "sha256": VERIFY.sha256_file(path),
            }

        attestation = {
            "schema": "ror.ogre_next_metal_rt_n2.attestation.v2",
            "status": status,
            "source": source,
            "executable": entry(executable_path),
            "report": entry(report_path),
            "probe": entry(probe_path) if status == "pass" else None,
        }
        (root / VERIFY.METAL_N2_REQUIRED_ARTIFACTS[1]).write_text(
            json.dumps(attestation) + "\n", encoding="utf-8"
        )

    def test_requires_every_exact_nonempty_regular_file(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-artifacts-") as temp:
            root = Path(temp)
            for index, name in enumerate(VERIFY.REQUIRED_ARTIFACTS, start=1):
                (root / name).write_bytes(bytes((index,)))
            manifest = VERIFY.verify_artifact_set(root)
            self.assertEqual(
                [entry["path"] for entry in manifest],
                list(VERIFY.REQUIRED_ARTIFACTS),
            )
            missing = root / VERIFY.REQUIRED_ARTIFACTS[-1]
            missing.unlink()
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "missing"):
                VERIFY.verify_artifact_set(root)

    def test_rejects_empty_artifact(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-artifacts-") as temp:
            root = Path(temp)
            for name in VERIFY.REQUIRED_ARTIFACTS:
                (root / name).write_bytes(b"x")
            (root / VERIFY.REQUIRED_ARTIFACTS[0]).write_bytes(b"")
            with self.assertRaisesRegex(VERIFY.ArtifactSetError, "empty"):
                VERIFY.verify_artifact_set(root)

    def test_metal_n2_gate_cross_checks_pass_attestation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n2-artifacts-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n2(root, "pass")
            manifest = VERIFY.verify_artifact_set(
                root, verify_metal_n2_evidence=True
            )
            self.assertIn(
                VERIFY.METAL_N2_PROBE_ARTIFACT,
                [entry["path"] for entry in manifest],
            )
            (root / VERIFY.METAL_N2_PROBE_ARTIFACT).write_bytes(b"tampered")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "probe attestation mismatch"
            ):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n2_evidence=True
                )

    def test_metal_n2_gate_accepts_skip_but_rejects_stale_probe(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ror-ogre-n2-skip-") as temp:
            root = Path(temp)
            self.write_baseline(root)
            self.write_metal_n2(root, "skip")
            VERIFY.verify_artifact_set(root, verify_metal_n2_evidence=True)
            (root / VERIFY.METAL_N2_PROBE_ARTIFACT).write_bytes(b"stale")
            with self.assertRaisesRegex(
                VERIFY.ArtifactSetError, "stale probe"
            ):
                VERIFY.verify_artifact_set(
                    root, verify_metal_n2_evidence=True
                )


if __name__ == "__main__":
    unittest.main()
