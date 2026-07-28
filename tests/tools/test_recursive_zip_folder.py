#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

import os
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
MACROS_FILE = REPO_ROOT / "cmake" / "Macros.cmake"


class RecursiveZipFolderTests(unittest.TestCase):
    def test_archive_has_explicit_unique_directory_records(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "source"
            build = root / "build"
            package = source / "payload" / "skeleton"

            fixtures = {
                "README.txt": "root file\n",
                "cache/empty": "",
                "config/input.map": "keyboard mapping\n",
                "nested/deep/value.txt": "nested value\n",
            }
            for relative_path, contents in fixtures.items():
                destination = package / relative_path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(contents, encoding="utf-8")

            source.mkdir(parents=True, exist_ok=True)
            (source / "CMakeLists.txt").write_text(
                "\n".join(
                    [
                        "cmake_minimum_required(VERSION 3.16)",
                        "project(recursive_zip_folder_test NONE)",
                        f'include("{MACROS_FILE.as_posix()}")',
                        "recursive_zip_folder(",
                        '  "${CMAKE_CURRENT_SOURCE_DIR}/payload"',
                        '  "${CMAKE_CURRENT_BINARY_DIR}/archives")',
                        "",
                    ]
                ),
                encoding="utf-8",
            )

            subprocess.run(
                ["cmake", "-S", str(source), "-B", str(build)],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run(
                [
                    "cmake",
                    "--build",
                    str(build),
                    "--target",
                    "zip_folder_payload",
                ],
                check=True,
                capture_output=True,
                text=True,
            )

            archive = build / "archives" / "skeleton.zip"
            with zipfile.ZipFile(archive) as packaged:
                entries = packaged.namelist()

            expected_entries = {
                "README.txt",
                "cache/",
                "cache/empty",
                "config/",
                "config/input.map",
                "nested/",
                "nested/deep/",
                "nested/deep/value.txt",
            }
            self.assertEqual(set(entries), expected_entries)
            self.assertEqual(len(entries), len(expected_entries))

            # A changed descendant must replace, rather than append to, the
            # archive and must keep every central-directory name unique.
            changed_file = package / "nested" / "deep" / "value.txt"
            changed_file.write_text("updated nested value\n", encoding="utf-8")
            changed_timestamp = archive.stat().st_mtime + 5
            os.utime(
                changed_file,
                (changed_timestamp, changed_timestamp),
            )
            subprocess.run(
                [
                    "cmake",
                    "--build",
                    str(build),
                    "--target",
                    "zip_folder_payload",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            with zipfile.ZipFile(archive) as packaged:
                rebuilt_entries = packaged.namelist()
                rebuilt_value = packaged.read("nested/deep/value.txt")

            self.assertEqual(rebuilt_entries, entries)
            self.assertEqual(
                len(rebuilt_entries),
                len(set(rebuilt_entries)),
            )
            self.assertEqual(rebuilt_value, b"updated nested value\n")


if __name__ == "__main__":
    unittest.main()
