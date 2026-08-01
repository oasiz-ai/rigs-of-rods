#!/usr/bin/env python3
"""Unit tests for the exact OGRE-Next CI artifact-set gate."""

from __future__ import annotations

import importlib.util
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


if __name__ == "__main__":
    unittest.main()
