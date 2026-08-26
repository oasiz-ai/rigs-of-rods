#!/usr/bin/env python3
"""Contracts for modernized legacy dependencies on MSVC."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MAIN_CMAKE = REPOSITORY_ROOT / "source" / "main" / "CMakeLists.txt"
DEPENDENCY_CMAKE = REPOSITORY_ROOT / "cmake" / "DependenciesConfig.cmake"


class MsvcLegacyDependencyContractTests(unittest.TestCase):
    def test_removed_auto_ptr_transition_switch_is_not_reintroduced(
        self,
    ) -> None:
        source = MAIN_CMAKE.read_text(encoding="utf-8")
        self.assertNotIn("_HAS_AUTO_PTR_ETC", source)
        self.assertNotIn("std::auto_ptr", source)

    def test_ogre14_keeps_affected_legacy_dependencies_disabled(
        self,
    ) -> None:
        source = DEPENDENCY_CMAKE.read_text(encoding="utf-8")

        for option_name in ("ROR_USE_CAELUM", "ROR_USE_PAGED"):
            with self.subTest(option=option_name):
                pattern = re.compile(
                    r"if \(ROR_OGRE14\)\s+"
                    rf"set\({option_name} OFF CACHE BOOL [^\n]+ FORCE\)",
                    re.MULTILINE,
                )
                self.assertRegex(source, pattern)


if __name__ == "__main__":
    unittest.main()
