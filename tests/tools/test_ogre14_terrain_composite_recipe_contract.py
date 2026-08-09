#!/usr/bin/env python3
"""Contracts for the pinned OGRE terrain-composite capture prerequisite."""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH_RELATIVE = (
    "cmake/conan/recipes/ogre3d/patches/14.5.2/"
    "terrain-composite-revision-metal-readback.patch"
)
PATCH = ROOT / PATCH_RELATIVE
CONANDATA = ROOT / "cmake/conan/recipes/ogre3d/conandata.yml"
PROBE = (
    ROOT
    / "cmake/conan/recipes/ogre3d/test_package/src/ogre_recipe_probe.cpp"
)
README = ROOT / "cmake/conan/recipes/ogre3d/README.md"
ROOT_RECIPE = ROOT / "conanfile.py"
MYGUI_RECIPE = ROOT / "cmake/conan/recipes/mygui/conanfile.py"
LOCKFILES = tuple(
    ROOT / "cmake/conan/locks" / name
    for name in (
        "ogre3d-14.5.2-linux-x86_64-release.lock",
        "ogre3d-14.5.2-macos-arm64-release.lock",
        "ogre3d-14.5.2-windows-x86_64-release.lock",
        "ror-ogre14-linux-x86_64-release.lock",
        "ror-ogre14-macos-arm64-release.lock",
        "ror-ogre14-windows-x86_64-release.lock",
    )
)


def recipe_revision(path: Path) -> str:
    match = re.search(
        r'^OGRE14_RECIPE_REVISION\s*=\s*"([0-9a-f]{32})"$',
        path.read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"missing OGRE14_RECIPE_REVISION in {path}")
    return match.group(1)


class Ogre14TerrainCompositeRecipeContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.patch = PATCH.read_text(encoding="utf-8")
        self.probe = PROBE.read_text(encoding="utf-8")

    def test_patch_is_registered_and_scope_is_exact(self) -> None:
        self.assertIn(
            "patches/14.5.2/"
            "terrain-composite-revision-metal-readback.patch",
            CONANDATA.read_text(encoding="utf-8"),
        )
        touched = set(re.findall(r"^--- a/(.+)$", self.patch, re.MULTILINE))
        self.assertEqual(
            touched,
            {
                "RenderSystems/Metal/src/OgreMetalHardwarePixelBuffer.mm",
                "Components/Terrain/src/OgreTerrain.cpp",
            },
        )
        self.assertNotIn("RenderSystems/Direct3D", self.patch)
        self.assertNotIn("RenderSystems/GL", self.patch)

    def test_metal_upload_and_readback_are_exact(self) -> None:
        added_lines = "\n".join(
            line[1:]
            for line in self.patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )
        for marker in (
            "Texture::createSurfaceList already supplies the dimensions",
            "mWidth = width;",
            "mHeight = height;",
            "(mUsage & TU_AUTOMIPMAP) && mLevel == 0",
            "generateMipmapsForTexture:mTexture",
            "sourceSlice:static_cast<NSUInteger>(mFace)",
            "sourceLevel:static_cast<NSUInteger>(mLevel)",
            "mipmapLevel:static_cast<NSUInteger>(mLevel)",
            "slice:static_cast<NSUInteger>(mFace)",
            "renderSystem->getActiveDevice()->stall();",
            "without changing row orientation",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.patch)
        self.assertNotIn("sourceSlice:0", added_lines)
        self.assertNotIn("sourceLevel:0", added_lines)
        self.assertNotIn("mipmapLevel:0", added_lines)

    def test_terrain_revisions_follow_successful_content_changes(self) -> None:
        self.assertEqual(self.patch.count("mCompositeMap->_dirtyState();"), 2)
        update_call = self.patch.index(
            "mMaterialGenerator->updateCompositeMap(this, mCompositeMapDirtyRect);"
        )
        update_dirty = self.patch.index(
            "mCompositeMap->_dirtyState();", update_call
        )
        clear_dirty = self.patch.index(
            "mCompositeMapDirtyRect.setNull();", update_dirty
        )
        self.assertLess(update_call, update_dirty)
        self.assertLess(update_dirty, clear_dirty)

        unlock = self.patch.rindex("buf->unlock();")
        initial_dirty = self.patch.index("mCompositeMap->_dirtyState();", unlock)
        destroy_branch = self.patch.index(
            "else if (!mCompositeMapRequired && mCompositeMap)", initial_dirty
        )
        self.assertLess(unlock, initial_dirty)
        self.assertLess(initial_dirty, destroy_branch)

    def test_native_probe_is_asymmetric_and_fail_closed(self) -> None:
        for marker in (
            "quadrant_pixels",
            "level-zero RGBA row-orientation/alpha readback",
            "level-one automatic mip readback",
            "level-one Metal buffer dimensions are incorrect",
            "failed readback rollback",
            "post-failure texture readback",
            "texture-capture-contract=ok",
            "texture->_dirtyState();",
            "first_revision != initial_state + 1U",
            "second_revision != first_revision + 1U",
            "width + 1U",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.probe)
        self.assertEqual(self.probe.count("texture->_dirtyState();"), 2)

    def test_root_mygui_and_all_platform_locks_share_recipe_revision(self) -> None:
        root_revision = recipe_revision(ROOT_RECIPE)
        self.assertEqual(root_revision, recipe_revision(MYGUI_RECIPE))
        pinned = f"ogre3d/14.5.2#{root_revision}"
        for lockfile in LOCKFILES:
            with self.subTest(lockfile=lockfile.name):
                self.assertIn(pinned, lockfile.read_text(encoding="utf-8"))

    def test_documented_patch_hash_matches_bytes(self) -> None:
        patch_hash = hashlib.sha256(PATCH.read_bytes()).hexdigest()
        self.assertIn(patch_hash, README.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
