#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / "cmake/conan/export_pinned_dependency_recipes.py"
WORKFLOW_PATH = ROOT / ".github/workflows/ogre14-native.yml"
BUILD_GAME_WORKFLOW_PATH = ROOT / ".github/workflows/build-game.yml"
MACOS_WORKFLOW_PATH = ROOT / ".github/workflows/macos-native.yml"

SPEC = importlib.util.spec_from_file_location(
    "export_pinned_dependency_recipes", SCRIPT_PATH
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("failed to load pinned recipe exporter")
EXPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORTER)


class PinnedDependencyRecipeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.original_bytes = EXPORTER.ARCHIVE_BYTES
        self.original_sha256 = EXPORTER.ARCHIVE_SHA256

    def tearDown(self) -> None:
        EXPORTER.ARCHIVE_BYTES = self.original_bytes
        EXPORTER.ARCHIVE_SHA256 = self.original_sha256

    def _write_archive(
        self, path: Path, members: list[tuple[str, bytes, str]]
    ) -> None:
        with tarfile.open(path, mode="w:gz") as archive:
            for name, payload, kind in members:
                info = tarfile.TarInfo(name)
                if kind == "file":
                    info.size = len(payload)
                    archive.addfile(info, io.BytesIO(payload))
                elif kind == "directory":
                    info.type = tarfile.DIRTYPE
                    archive.addfile(info)
                elif kind == "symlink":
                    info.type = tarfile.SYMTYPE
                    info.linkname = "target"
                    archive.addfile(info)
                else:
                    raise AssertionError(f"unknown test member kind: {kind}")
        EXPORTER.ARCHIVE_BYTES = path.stat().st_size
        EXPORTER.ARCHIVE_SHA256 = hashlib.sha256(path.read_bytes()).hexdigest()

    def test_public_archive_and_exact_recipe_revisions_are_pinned(self) -> None:
        self.assertEqual(
            EXPORTER.UPSTREAM_COMMIT,
            "d3568327ff541d62052fa8a97cc71a4e3f126d89",
        )
        self.assertEqual(len(EXPORTER.ARCHIVE_SHA256), 64)
        self.assertEqual(EXPORTER.ARCHIVE_BYTES, 40_157)
        self.assertEqual(
            [record[2] for record in EXPORTER.RECIPES],
            [
                "discord-rpc/3.4.0@anotherfoxguy/stable"
                "#a2905f22ab84faeceebe54e488ff9195",
                "socketw/3.11.0@anotherfoxguy/stable"
                "#6630840d3f73fb6d6e60f6f88132d40a",
                "freeimage/3.18.0@anotherfoxguy/stable"
                "#8b69961fa00ad36b37d77dd40502fcbf",
                "mygui/3.4.0@anotherfoxguy/stable"
                "#d544e344e389c9b287124fea8b567d01",
            ],
        )

    def test_safe_archive_shape_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "recipes.tar.gz"
            self._write_archive(
                archive,
                [
                    (EXPORTER.ARCHIVE_ROOT, b"", "directory"),
                    (
                        f"{EXPORTER.ARCHIVE_ROOT}/discord-rpc/all/"
                        "conanfile.py",
                        b"pass\n",
                        "file",
                    ),
                ],
            )
            members = EXPORTER.validate_archive(archive)
            self.assertEqual(len(members), 2)

    def test_archive_traversal_and_links_fail_closed(self) -> None:
        hostile_members = (
            (f"{EXPORTER.ARCHIVE_ROOT}/../escape", b"x", "file"),
            (f"{EXPORTER.ARCHIVE_ROOT}/link", b"", "symlink"),
            ("wrong-root/file", b"x", "file"),
        )
        for hostile in hostile_members:
            with self.subTest(hostile=hostile[0]):
                with tempfile.TemporaryDirectory() as temporary:
                    archive = Path(temporary) / "recipes.tar.gz"
                    self._write_archive(archive, [hostile])
                    with self.assertRaises(EXPORTER.RecipeExportError):
                        EXPORTER.validate_archive(archive)

    def test_native_workflow_uses_no_private_recipe_remote(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn(
            "python cmake/conan/export_pinned_dependency_recipes.py",
            workflow,
        )
        self.assertIn("pinned-conan-recipes.json", workflow)
        self.assertNotIn("nexus.anotherfoxguy.com", workflow)
        self.assertNotIn("rigs-of-rods-deps", workflow)

    def test_macos_workflow_bootstraps_pinned_recipes_without_private_remote(
        self,
    ) -> None:
        workflow = MACOS_WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertEqual(
            workflow.count(
                "python3 cmake/conan/export_pinned_dependency_recipes.py"
            ),
            1,
        )
        self.assertIn(
            "'cmake/conan/export_pinned_dependency_recipes.py'",
            workflow,
        )
        self.assertNotIn("nexus.anotherfoxguy.com", workflow)
        self.assertNotIn("rigs-of-rods-deps", workflow)

    def test_build_game_exports_pinned_recipes_on_both_platforms(self) -> None:
        workflow = BUILD_GAME_WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertEqual(
            workflow.count(
                "python cmake/conan/export_pinned_dependency_recipes.py"
            )
            + workflow.count(
                "python cmake\\conan\\export_pinned_dependency_recipes.py"
            ),
            2,
        )
        self.assertEqual(workflow.count("conan profile detect --force"), 2)
        self.assertEqual(
            workflow.count("ror-conan-recipes-d3568327.tar.gz"),
            2,
        )
        self.assertEqual(
            workflow.count("cmake/conan/export_pinned_dependency_recipes.py')"),
            2,
        )

    def test_build_game_linux_installs_ogre_next_vulkan_prerequisites(
        self,
    ) -> None:
        workflow = BUILD_GAME_WORKFLOW_PATH.read_text(encoding="utf-8")
        linux_job = workflow.split("  build-gcc:\n", 1)[1].split(
            "  build-msvc:\n", 1
        )[0]
        install_step = linux_job.split(
            "      - name: Install dependencies\n", 1
        )[1].split("\n      - name: Configure\n", 1)[0]
        installed_packages = {
            line.strip().removesuffix("\\").strip()
            for line in install_step.splitlines()
        }

        self.assertRegex(workflow, r"(?m)^on: \[ push, pull_request \]$")
        self.assertEqual(
            {
                "libvulkan-dev",
                "mesa-vulkan-drivers",
                "vulkan-tools",
            }
            - installed_packages,
            set(),
        )


if __name__ == "__main__":
    unittest.main()
