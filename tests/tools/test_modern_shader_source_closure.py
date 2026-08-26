#!/usr/bin/env python3
"""Fail closed if packaged resources regain a legacy Cg/ASM shader route."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "resources"

LEGACY_SOURCE_SUFFIXES = {".asm", ".cg"}
MODERN_SOURCE_SUFFIXES = {".fragment", ".glsl", ".hlsl", ".metal", ".vertex"}
SCRIPT_SUFFIXES = {".compositor", ".material", ".program"}

LEGACY_PROGRAM_DECLARATION = re.compile(
    r"(?im)^\s*(?:vertex|fragment|geometry|hull|domain|compute)_program\s+"
    r"\S+\s+(?:asm|cg)\b"
)
LEGACY_SOURCE_REFERENCE = re.compile(
    r"(?im)^\s*source\s+[^\s{}]+\.(?:asm|cg)\s*$"
)
CONTINUED_GLSL_DIRECTIVE = re.compile(
    r"(?m)^\s*#(?:if|elif)\b[^\r\n]*\\\s*$"
)


class ModernShaderSourceClosureTests(unittest.TestCase):
    def test_packaged_resource_tree_contains_no_legacy_shader_sources(self) -> None:
        legacy_sources = sorted(
            path.relative_to(ROOT).as_posix()
            for path in RESOURCES.rglob("*")
            if path.is_file() and path.suffix.lower() in LEGACY_SOURCE_SUFFIXES
        )
        self.assertEqual(legacy_sources, [])

    def test_material_scripts_contain_no_legacy_program_route(self) -> None:
        declarations = []
        references = []
        for path in sorted(RESOURCES.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in SCRIPT_SUFFIXES:
                continue
            script = path.read_text(encoding="utf-8")
            relative = path.relative_to(ROOT).as_posix()
            declarations.extend(
                f"{relative}:{match.group(0).strip()}"
                for match in LEGACY_PROGRAM_DECLARATION.finditer(script)
            )
            references.extend(
                f"{relative}:{match.group(0).strip()}"
                for match in LEGACY_SOURCE_REFERENCE.finditer(script)
            )

        self.assertEqual(declarations, [])
        self.assertEqual(references, [])

    def test_modern_shader_sources_are_regular_nonempty_files(self) -> None:
        modern_sources = sorted(
            path
            for path in RESOURCES.rglob("*")
            if path.is_file() and path.suffix.lower() in MODERN_SOURCE_SUFFIXES
        )
        self.assertTrue(modern_sources)
        for path in modern_sources:
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertFalse(path.is_symlink())
                self.assertTrue(path.read_bytes().strip())

    def test_glsl_conditionals_do_not_use_directive_continuations(self) -> None:
        for path in sorted(RESOURCES.rglob("*.glsl")):
            with self.subTest(path=path.relative_to(ROOT)):
                source = path.read_text(encoding="utf-8")
                self.assertIsNone(CONTINUED_GLSL_DIRECTIVE.search(source))


if __name__ == "__main__":
    unittest.main()
