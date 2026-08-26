#!/usr/bin/env python3
"""Fail closed if packaged resources regain a legacy Cg/ASM shader route."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "resources"
NATIVE_WORKFLOW = ROOT / ".github/workflows/ogre-next-combined-native.yml"
TSAN_WORKFLOW = ROOT / ".github/workflows/ogre-next-combined-tsan.yml"
GIT_ATTRIBUTES = ROOT / ".gitattributes"

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

    def test_byte_bound_shader_fixtures_trigger_native_validation(self) -> None:
        attributes = GIT_ATTRIBUTES.read_text(encoding="utf-8")
        native = NATIVE_WORKFLOW.read_text(encoding="utf-8")
        tsan = TSAN_WORKFLOW.read_text(encoding="utf-8")

        self.assertIn(
            "tests/fixtures/nicemetal_runtime/** text eol=lf", attributes
        )
        self.assertEqual(native.count("      - .gitattributes\n"), 2)
        self.assertEqual(tsan.count("      - .gitattributes\n"), 1)

    def test_success_artifacts_retain_compiler_receipts(self) -> None:
        native = NATIVE_WORKFLOW.read_text(encoding="utf-8")
        tsan = TSAN_WORKFLOW.read_text(encoding="utf-8")

        linux_stage = native.index("Stage the GLSL source compiler receipt")
        linux_inventory = native.index("Write deterministic package inventory")
        linux_upload = native.index("Upload qualified Linux RoR-Combined runtime")
        windows_stage = native.index("Stage the HLSL source compiler receipts")
        windows_inventory = native.index(
            "Write deterministic Windows package inventory"
        )
        windows_upload = native.index("Upload qualified Windows RoR-Combined runtime")
        self.assertLess(linux_stage, linux_inventory)
        self.assertLess(linux_inventory, linux_upload)
        self.assertLess(windows_stage, windows_inventory)
        self.assertLess(windows_inventory, windows_upload)
        self.assertIn(
            'source_receipt="$ROR_COMBINED_ARTIFACTS_DIR/'
            'linux-x86_64-modern-glsl-source-compile.json"',
            native[linux_stage:linux_inventory],
        )
        self.assertIn(
            'destination_receipt="$destination_dir/glsl-receipt.json"',
            native[linux_stage:linux_inventory],
        )
        self.assertIn(
            "'hlsl-receipt.json' = "
            "'windows-x86_64-modern-hlsl-source-compile.json'",
            native[windows_stage:windows_inventory],
        )
        self.assertIn(
            "'fxc-provenance.json' = 'windows-x86_64-fxc-provenance.json'",
            native[windows_stage:windows_inventory],
        )
        for required in (
            '"receipt": "qualification/shader-source-compile/glsl-receipt.json"',
            '"receipt": "qualification/shader-source-compile/hlsl-receipt.json"',
            '"compiler_provenance": "qualification/shader-source-compile/fxc-provenance.json"',
        ):
            self.assertIn(required, native)
        staged_upload_path = "path: ${{ runner.temp }}/ror-combined-stage/"
        self.assertEqual(native.count(staged_upload_path), 2)
        self.assertIn(staged_upload_path, native[linux_upload:windows_stage])
        self.assertIn(staged_upload_path, native[windows_upload:])

        tsan_compile = tsan.index("Compile the complete modern GLSL source closure")
        tsan_upload = tsan.index("Upload TSan evidence and failure diagnostics")
        self.assertLess(tsan_compile, tsan_upload)
        self.assertIn(
            'receipt="$ROR_TSAN_ARTIFACTS_DIR/'
            'linux-x86_64-modern-glsl-source-compile.json"',
            tsan[tsan_compile:tsan_upload],
        )
        self.assertIn(
            "path: ${{ runner.temp }}/ror-combined-tsan-artifacts",
            tsan[tsan_upload:],
        )


if __name__ == "__main__":
    unittest.main()
