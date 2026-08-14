#!/usr/bin/env python3
"""Validate immutable Conan recipe seeds used by hosted native builds."""

from __future__ import annotations

import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import tarfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/ogre14-native.yml"
SEEDS = {
    "discord-rpc-3.4.0-a2905f22-recipe.tgz": {
        "sha256": "55ee77d297157738dc009715eb748190eae1661e90fd1704931973e34ebccc84",
        "reference": "discord-rpc/3.4.0@anotherfoxguy/stable",
        "revision": "a2905f22ab84faeceebe54e488ff9195",
        "recipe_folder": "discob0fddef65fcff",
    },
    "freeimage-3.18.0-8b69961f-recipe.tgz": {
        "sha256": "93ccfa24b8a2c23c80125d3dd3effaafd00f72d95ed8322aae090c8acb86b5b9",
        "reference": "freeimage/3.18.0@anotherfoxguy/stable",
        "revision": "8b69961fa00ad36b37d77dd40502fcbf",
        "recipe_folder": "freeieee8d939e202d",
    },
    "socketw-3.11.0-6630840d-recipe.tgz": {
        "sha256": "d72d0235004f0e09271fea9d29eff1131116e7a73d574b1a5c133605064b142d",
        "reference": "socketw/3.11.0@anotherfoxguy/stable",
        "revision": "6630840d3f73fb6d6e60f6f88132d40a",
        "recipe_folder": "socke4809af999e653",
    },
}


class Ogre14ConanCacheSeedContractTests(unittest.TestCase):
    def test_seed_archives_are_exact_recipe_only_safe_paths(self) -> None:
        for name, expected in SEEDS.items():
            path = ROOT / "cmake/conan/cache-seeds" / name
            payload = path.read_bytes()
            with self.subTest(seed=name):
                self.assertEqual(
                    hashlib.sha256(payload).hexdigest(), expected["sha256"]
                )
                with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
                    members = archive.getmembers()
                    self.assertTrue(members)
                    for member in members:
                        member_path = PurePosixPath(member.name)
                        self.assertFalse(member_path.is_absolute())
                        self.assertNotIn("..", member_path.parts)
                        self.assertFalse(member.issym() or member.islnk())
                    pkglist = archive.extractfile("pkglist.json")
                    self.assertIsNotNone(pkglist)
                    catalog = json.loads(pkglist.read().decode("utf-8"))
                row = catalog[expected["reference"]]["revisions"][
                    expected["revision"]
                ]
                self.assertEqual(row["recipe_folder"], expected["recipe_folder"])
                self.assertFalse(any("/p/" in member.name for member in members))
                self.assertFalse(any("/s/" in member.name for member in members))

    def test_workflow_authenticates_restores_and_revalidates_seeds(self) -> None:
        text = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("'cmake/conan/cache-seeds/**'", text)
        for name, expected in SEEDS.items():
            with self.subTest(seed=name):
                self.assertIn(
                    f"{expected['sha256']}  cmake/conan/cache-seeds/{name}",
                    text,
                )
                self.assertIn(
                    f"cmake/conan/cache-seeds/{name}", text
                )
                self.assertIn(
                    f"{expected['reference']}#{expected['revision']}", text
                )


if __name__ == "__main__":
    unittest.main()
