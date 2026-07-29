#!/usr/bin/env python3
"""Runtime contract tests for the platform-specific OGRE 14 lock selector."""

from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PLATFORM_MODULE = REPOSITORY_ROOT / "cmake" / "Ogre14Platform.cmake"


def select_lockfile(system_name: str, processor: str) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory(prefix="ror-ogre14-platform-") as directory:
        output_path = Path(directory) / "selected.txt"
        script_path = Path(directory) / "select.cmake"
        script_path.write_text(
            f'include("{PLATFORM_MODULE.as_posix()}")\n'
            "ror_select_ogre14_lockfile(\n"
            f'    selected "{system_name}" "{processor}")\n'
            f'file(WRITE "{output_path.as_posix()}" "${{selected}}")\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            ["cmake", "-P", str(script_path)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode == 0:
            result.selected_lockfile = output_path.read_text(encoding="utf-8")
        return result


class Ogre14PlatformContractTests(unittest.TestCase):
    def test_supported_platforms_select_exact_pinned_lockfiles(self) -> None:
        cases = (
            (
                "Darwin",
                "arm64",
                "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
            ),
            (
                "Darwin",
                "aarch64",
                "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
            ),
            (
                "Linux",
                "x86_64",
                "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
            ),
            (
                "Linux",
                "AMD64",
                "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
            ),
            (
                "Windows",
                "AMD64",
                "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
            ),
            (
                "Windows",
                "x86_64",
                "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
            ),
        )
        for system_name, processor, expected in cases:
            with self.subTest(system=system_name, processor=processor):
                result = select_lockfile(system_name, processor)
                self.assertEqual(
                    result.returncode,
                    0,
                    msg=result.stdout + result.stderr,
                )
                self.assertEqual(result.selected_lockfile, expected)

    def test_unsupported_targets_fail_closed(self) -> None:
        for system_name, processor in (
            ("Darwin", "x86_64"),
            ("Linux", "arm64"),
            ("Windows", "ARM64"),
            ("FreeBSD", "x86_64"),
            ("", ""),
        ):
            with self.subTest(system=system_name, processor=processor):
                result = select_lockfile(system_name, processor)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "has no pinned dependency graph",
                    result.stdout + result.stderr,
                )


if __name__ == "__main__":
    unittest.main()
