#!/usr/bin/env python3
"""Contracts for OGRE 14.5.2 exact material-script pre-open authority."""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH_RELATIVE = (
    "cmake/conan/recipes/ogre3d/patches/14.5.2/"
    "exact-material-script-preopen.patch"
)
PATCH = ROOT / PATCH_RELATIVE
CONANDATA = ROOT / "cmake/conan/recipes/ogre3d/conandata.yml"
README = ROOT / "cmake/conan/recipes/ogre3d/README.md"
PROBE = (
    ROOT
    / "cmake/conan/recipes/ogre3d/test_package/src/"
    "ogre_material_script_preopen_probe.cpp"
)
TEST_CMAKE = ROOT / "cmake/conan/recipes/ogre3d/test_package/CMakeLists.txt"
TEST_RECIPE = ROOT / "cmake/conan/recipes/ogre3d/test_package/conanfile.py"
PROVENANCE = ROOT / "doc/nextgen/OGRE14_EXACT_MATERIAL_SCRIPT_PREOPEN.md"
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
EXPECTED_SOURCE_SHA256 = (
    "1949fe62f3e4b8043e82e4dc94f9b0ab412a5bffc9e10d3b1dddc80fe54fe1e3"
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


class Ogre14ExactMaterialScriptPreopenContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.patch = PATCH.read_text(encoding="utf-8")

    def test_patch_is_last_and_touches_only_the_exact_seam(self) -> None:
        conandata = CONANDATA.read_text(encoding="utf-8")
        patch_lines = re.findall(
            r"^\s*- patch_file: (\S+)$", conandata, re.MULTILINE
        )
        self.assertEqual(
            patch_lines[-2], PATCH_RELATIVE.split("ogre3d/", 1)[1]
        )
        self.assertEqual(
            patch_lines[-1],
            "patches/14.5.2/expose-shadow-material-declaration-names.patch",
        )
        touched = set(re.findall(r"^--- a/(.+)$", self.patch, re.MULTILINE))
        self.assertEqual(
            touched,
            {
                "OgreMain/include/OgreResourceGroupManager.h",
                "OgreMain/src/OgreResourceGroupManager.cpp",
            },
        )

    def test_listener_api_is_additive_const_and_default_compatible(self) -> None:
        for marker in (
            "virtual bool resourceStreamOpeningEnabled() const",
            "virtual DataStreamPtr resourceStreamOpening(",
            "const Archive *selectedArchive,",
            "const FileInfo *fileInfo,",
            "bool &handled)",
            "handled = false;",
            "return DataStreamPtr();",
            "path + basename",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.patch)
        collision = self.patch.index("virtual bool resourceCollision")
        enabled = self.patch.index(
            "virtual bool resourceStreamOpeningEnabled() const"
        )
        opening = self.patch.index("virtual DataStreamPtr resourceStreamOpening")
        self.assertLess(collision, enabled)
        self.assertLess(enabled, opening)

    def test_both_open_paths_are_preopen_and_handled_never_falls_back(self) -> None:
        self.assertEqual(
            self.patch.count("mLoadingListener->resourceStreamOpening("),
            2,
        )
        self.assertEqual(
            self.patch.count(
                "mLoadingListener->resourceStreamOpeningEnabled()"
            ),
            2,
        )
        self.assertEqual(self.patch.count("if (!handled)"), 2)
        ordinary_hook = self.patch.index(
            "mLoadingListener->resourceStreamOpening("
        )
        ordinary_fallback = self.patch.index(
            "stream = pArch->open(exactMember);", ordinary_hook
        )
        self.assertLess(ordinary_hook, ordinary_fallback)
        script_hook = self.patch.index(
            "mLoadingListener->resourceStreamOpening(", ordinary_hook + 1
        )
        script_fallback = self.patch.index(
            "stream = fii.archive->open(fii.path + fii.basename);",
            script_hook,
        )
        self.assertLess(script_hook, script_fallback)
        added_lines = "\n".join(
            line[1:]
            for line in self.patch.splitlines()
            if line.startswith("+") and not line.startswith("+++")
        )
        self.assertNotIn(
            "stream = fii.archive->open(fii.filename);", added_lines
        )

    def test_import_metadata_is_exact_or_explicitly_absent(self) -> None:
        for marker in (
            "pArch->findFileInfo(resourceName, true, false)",
            "pArch->listFileInfo(true, false)",
            "candidates->size() == 1",
            "candidates->front().archive == pArch",
            "candidates->front().path +",
            "candidates->front().basename == resourceName",
            "entry.path + entry.basename ==",
            "selectedFileInfo.path + selectedFileInfo.basename",
            "selectedFileInfoPtr, handled",
            "catch (...)",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, self.patch)

    def test_native_probe_covers_hostile_identity_and_no_fallback(self) -> None:
        probe = PROBE.read_text(encoding="utf-8")
        for marker in (
            "OGRE_VERSION != ((14 << 16) | (5 << 8) | 2)",
            "DefaultCompatibilityListener",
            "MetadataFallbackListener",
            "resourceStreamOpeningEnabled() const override",
            "opted-out listener caused ordinary-open metadata I/O",
            "synthetic non-OGRE findFileInfo metadata failure",
            "return_wrong_qualified_candidate",
            "IDENTICAL_HOSTILE_BYTES",
            "selected_owner_tokens.size() == 2U",
            "observed_filenames == std::set<Ogre::String>{\"shared.hostile\"}",
            "exact_file_info->path + exact_file_info->basename",
            "handled-null rejection fell back to Archive::open",
            "handled shadow/import path fell back to Archive::open",
            "IMPORT_SHADOW_ARCHIVE",
            "selected_fixture_archive_token !=",
            "ordinary import queried unselected shadow metadata",
            "ordinary handled-null did not return an intentional null",
            "import_observations.front().has_file_info",
            "import fixture did not reproduce basename-only FileInfo",
            "authenticated-material-script-preopen=ok",
            "same-bytes same-digest true-shadow-precedence",
            "handled-no-fallback handled-null-rejection",
            "ordinary-null",
            "import-openresource",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, probe)

        cmake = TEST_CMAKE.read_text(encoding="utf-8")
        test_recipe = TEST_RECIPE.read_text(encoding="utf-8")
        self.assertIn("add_executable(\n    ogre_material_script_preopen_probe", cmake)
        self.assertIn("OGRE::Main", cmake)
        self.assertIn("staged_preopen_executable", test_recipe)
        self.assertIn(
            "f'\"{staged_preopen_executable}\"'", test_recipe
        )

    def test_source_archive_and_patch_digest_are_documented(self) -> None:
        conandata = CONANDATA.read_text(encoding="utf-8")
        self.assertIn(EXPECTED_SOURCE_SHA256, conandata)
        patch_hash = hashlib.sha256(PATCH.read_bytes()).hexdigest()
        self.assertIn(patch_hash, README.read_text(encoding="utf-8"))
        self.assertIn(patch_hash, PROVENANCE.read_text(encoding="utf-8"))

    def test_root_mygui_and_all_platform_locks_share_revision(self) -> None:
        root_revision = recipe_revision(ROOT_RECIPE)
        self.assertEqual(root_revision, recipe_revision(MYGUI_RECIPE))
        pinned = f"ogre3d/14.5.2#{root_revision}"
        for lockfile in LOCKFILES:
            with self.subTest(lockfile=lockfile.name):
                self.assertIn(pinned, lockfile.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
