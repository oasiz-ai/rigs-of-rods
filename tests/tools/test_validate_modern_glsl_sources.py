#!/usr/bin/env python3
"""Hostile orchestration tests for the modern GLSL compiler validator."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "tools/validate_modern_glsl_sources.py"
REAL_COMPILER = os.environ.get("ROR_TEST_GLSLANG_VALIDATOR")


FAKE_COMPILER = r"""#!/usr/bin/env python3
import sys

if sys.argv[1:] == ["--version"]:
    print("Glslang Version: 99.1.2 deterministic-test")
    raise SystemExit(0)

args = sys.argv[1:]
source = sys.stdin.read()
if "-l" not in args or "--stdin" not in args:
    print("missing fail-closed compile flags", file=sys.stderr)
    raise SystemExit(80)
try:
    stage = args[args.index("-S") + 1]
except (ValueError, IndexError):
    print("missing shader stage", file=sys.stderr)
    raise SystemExit(81)
if stage not in {"vert", "frag"}:
    print("invalid shader stage", file=sys.stderr)
    raise SystemExit(82)
if not source.startswith("#version 330"):
    print("source is not GLSL 330", file=sys.stderr)
    raise SystemExit(83)
if "void main" not in source:
    print("source has no selected main", file=sys.stderr)
    raise SystemExit(84)
if "-DCAELUM_PHASE_MOON_FRAGMENT=1" in args and "--fail-moon" in source:
    print("forced phase-moon compile failure", file=sys.stderr)
    raise SystemExit(85)
print("compiled", stage)
"""


class ModernGLSLCompilerValidatorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.temp_path = Path(self.temporary.name).resolve()
        self.compiler = self.temp_path / "glslangValidator"
        self.compiler.write_text(FAKE_COMPILER, encoding="utf-8")
        self.compiler.chmod(0o755)

    def _run(
        self,
        repository_root: Path = ROOT,
        compiler: Path | None = None,
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        completed = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--repo-root",
                str(repository_root),
                "--glslang-validator",
                str(compiler or self.compiler),
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(completed.stderr, "")
        return completed, json.loads(completed.stdout)

    def _copy_shader_fixture(self, name: str = "fixture") -> Path:
        fixture = self.temp_path / name
        copies = [
            (ROOT / "resources/caelum", fixture / "resources/caelum"),
            (
                ROOT / "resources/managed_materials/shadows/pssm/on",
                fixture / "resources/managed_materials/shadows/pssm/on",
            ),
            (ROOT / "resources/rtshader", fixture / "resources/rtshader"),
        ]
        for source, destination in copies:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(source, destination)
        return fixture

    def _write_mutating_compiler(self, name: str, mutation: str) -> Path:
        compiler = self.temp_path / name
        compiler.write_text(
            "#!/usr/bin/env python3\n"
            "from pathlib import Path\n"
            "import sys\n"
            "\n"
            "if sys.argv[1:] == ['--version']:\n"
            "    print('Glslang Version: hostile-mutation-test')\n"
            "    raise SystemExit(0)\n"
            "\n"
            "source = sys.stdin.read()\n"
            "if '-l' not in sys.argv or '--stdin' not in sys.argv:\n"
            "    raise SystemExit(90)\n"
            "if '-S' not in sys.argv or 'void main' not in source:\n"
            "    raise SystemExit(91)\n"
            f"{mutation}\n"
            "print('compiled')\n",
            encoding="utf-8",
        )
        compiler.chmod(0o755)
        return compiler

    def test_real_declarations_drive_a_deterministic_49_case_receipt(self) -> None:
        first, first_receipt = self._run()
        second, second_receipt = self._run()
        relocated_compiler = self.temp_path / "relocated-glslangValidator"
        shutil.copy2(self.compiler, relocated_compiler)
        relocated, relocated_receipt = self._run(compiler=relocated_compiler)

        self.assertEqual(first.returncode, 0, first.stdout)
        self.assertEqual(second.returncode, 0, second.stdout)
        self.assertEqual(relocated.returncode, 0, relocated.stdout)
        self.assertEqual(first.stdout, second.stdout)
        self.assertEqual(first.stdout, relocated.stdout)
        self.assertEqual(first_receipt, second_receipt)
        self.assertEqual(first_receipt, relocated_receipt)
        self.assertEqual(first_receipt["schema"], "ror.modern-glsl-source-compile@1")
        self.assertEqual(first_receipt["result"], "passed")
        self.assertEqual(first_receipt["caseCount"], 49)
        self.assertEqual(first_receipt["executedCaseCount"], 49)
        self.assertEqual(
            first_receipt["familyCaseCounts"],
            {"caelum": 26, "managed-pssm": 5, "rtshader": 18},
        )
        cases = first_receipt["cases"]
        self.assertEqual(len(cases), 49)
        self.assertEqual(len({case["caseId"] for case in cases}), 49)
        self.assertEqual(
            sum(
                case["evidenceKind"]
                == "declared-gl3plus-program-source-compile"
                for case in cases
            ),
            31,
        )
        wrappers = [
            case
            for case in cases
            if case["evidenceKind"]
            == "rtshader-dependency-synthetic-wrapper-source-compile"
        ]
        self.assertEqual(len(wrappers), 18)
        self.assertEqual({case["stage"] for case in wrappers}, {"vert", "frag"})
        self.assertTrue(all(case["probeFunction"] for case in wrappers))
        self.assertIn("ogre-next-rendering", first_receipt["doesNotProve"])
        self.assertIn("playability", first_receipt["doesNotProve"])

    def test_missing_compiler_fails_with_json_evidence(self) -> None:
        completed, receipt = self._run(compiler=self.temp_path / "missing")
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(receipt["result"], "failed")
        self.assertEqual(receipt["error"]["code"], "missing_compiler")
        self.assertEqual(receipt["executedCaseCount"], 0)

    def test_missing_declared_case_fails_before_compilation(self) -> None:
        fixture = self._copy_shader_fixture()
        (fixture / "resources/caelum/moon.material").unlink()
        completed, receipt = self._run(repository_root=fixture)
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(receipt["result"], "failed")
        self.assertEqual(receipt["error"]["code"], "missing_cases")
        self.assertEqual(receipt["error"]["context"]["family"], "caelum")

    def test_legacy_source_fails_before_compilation(self) -> None:
        fixture = self._copy_shader_fixture()
        legacy = fixture / "resources/caelum/hostile.cg"
        legacy.write_text("void main() {}\n", encoding="utf-8")
        completed, receipt = self._run(repository_root=fixture)
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(receipt["error"]["code"], "legacy_source")
        self.assertEqual(
            receipt["error"]["context"]["path"],
            "resources/caelum/hostile.cg",
        )

    def test_symlinked_source_fails_before_compilation(self) -> None:
        fixture = self._copy_shader_fixture()
        source = fixture / "resources/caelum/CaelumPhaseMoon_gl3plus.glsl"
        external = self.temp_path / "external.glsl"
        external.write_bytes(source.read_bytes())
        source.unlink()
        try:
            source.symlink_to(external)
        except OSError as exc:
            self.skipTest(f"symbolic links unavailable: {exc}")
        completed, receipt = self._run(repository_root=fixture)
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(receipt["error"]["code"], "symlink_input")
        self.assertEqual(
            receipt["error"]["context"]["path"],
            "resources/caelum/CaelumPhaseMoon_gl3plus.glsl",
        )

    def test_compile_error_is_not_promoted_to_success(self) -> None:
        fixture = self._copy_shader_fixture()
        source = fixture / "resources/caelum/CaelumPhaseMoon_gl3plus.glsl"
        source.write_text(
            source.read_text(encoding="utf-8") + "\n// --fail-moon\n",
            encoding="utf-8",
        )
        completed, receipt = self._run(repository_root=fixture)
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(receipt["result"], "failed")
        self.assertEqual(receipt["error"]["code"], "compile_error")
        failed = receipt["error"]["context"]["caseResult"]
        self.assertEqual(failed["status"], "failed")
        self.assertEqual(failed["exitCode"], 85)
        self.assertIn(
            "forced phase-moon compile failure", failed["stderrSummary"]
        )
        self.assertLess(receipt["executedCaseCount"], receipt["caseCount"])

    def test_late_tree_additions_fail_with_truthful_execution_counts(self) -> None:
        additions = (
            ("legacy", "late-hostile.cg", "legacy_source"),
            ("unexpected", "late-hostile.glsl", "input_tree_changed"),
        )
        for fixture_name, filename, expected_error in additions:
            with self.subTest(filename=filename):
                fixture = self._copy_shader_fixture(f"fixture-{fixture_name}")
                target = fixture / "resources/caelum" / filename
                mutation = (
                    f"target = Path({str(target)!r})\n"
                    "if not target.exists():\n"
                    "    target.write_text('void main() {}\\n', encoding='utf-8')"
                )
                compiler = self._write_mutating_compiler(
                    f"tree-mutator-{fixture_name}", mutation
                )
                completed, receipt = self._run(
                    repository_root=fixture, compiler=compiler
                )
                self.assertNotEqual(completed.returncode, 0)
                self.assertEqual(receipt["result"], "failed")
                self.assertEqual(receipt["error"]["code"], expected_error)
                self.assertEqual(receipt["caseCount"], 49)
                self.assertEqual(receipt["executedCaseCount"], 49)
                self.assertEqual(len(receipt["cases"]), 49)

    def test_late_source_and_compiler_mutations_report_executed_cases(self) -> None:
        source_fixture = self._copy_shader_fixture("fixture-source-mutation")
        source_target = (
            source_fixture
            / "resources/caelum/CaelumPhaseMoon_gl3plus.glsl"
        )
        source_mutation = (
            f"target = Path({str(source_target)!r})\n"
            "marker = '// hostile source mutation\\n'\n"
            "if not target.read_text(encoding='utf-8').endswith(marker):\n"
            "    with target.open('a', encoding='utf-8') as stream:\n"
            "        stream.write(marker)"
        )
        source_compiler = self._write_mutating_compiler(
            "source-mutator", source_mutation
        )
        source_completed, source_receipt = self._run(
            repository_root=source_fixture, compiler=source_compiler
        )

        compiler_fixture = self._copy_shader_fixture("fixture-compiler-mutation")
        compiler_mutation = (
            "target = Path(__file__)\n"
            "with target.open('a', encoding='utf-8') as stream:\n"
            "    stream.write('# hostile compiler mutation\\n')"
        )
        mutating_compiler = self._write_mutating_compiler(
            "compiler-mutator", compiler_mutation
        )
        compiler_completed, compiler_receipt = self._run(
            repository_root=compiler_fixture, compiler=mutating_compiler
        )

        for completed, receipt, expected_error in (
            (source_completed, source_receipt, "input_changed"),
            (compiler_completed, compiler_receipt, "compiler_changed"),
        ):
            with self.subTest(error=expected_error):
                self.assertNotEqual(completed.returncode, 0)
                self.assertEqual(receipt["result"], "failed")
                self.assertEqual(receipt["error"]["code"], expected_error)
                self.assertEqual(receipt["caseCount"], 49)
                self.assertEqual(receipt["executedCaseCount"], 49)
                self.assertEqual(len(receipt["cases"]), 49)

    def test_compiler_path_with_symlinked_ancestor_fails(self) -> None:
        real_parent = self.temp_path / "real-compiler-parent"
        real_parent.mkdir()
        compiler = real_parent / "glslangValidator"
        shutil.copy2(self.compiler, compiler)
        linked_parent = self.temp_path / "linked-compiler-parent"
        try:
            linked_parent.symlink_to(real_parent, target_is_directory=True)
        except OSError as exc:
            self.skipTest(f"symbolic links unavailable: {exc}")

        completed, receipt = self._run(
            compiler=linked_parent / "glslangValidator"
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(receipt["result"], "failed")
        self.assertEqual(receipt["error"]["code"], "symlink_compiler")
        self.assertEqual(receipt["executedCaseCount"], 0)
        self.assertEqual(
            receipt["error"]["context"]["symlinkComponent"],
            str(linked_parent),
        )

    @unittest.skipUnless(
        REAL_COMPILER,
        "set ROR_TEST_GLSLANG_VALIDATOR to run the explicit real-compiler smoke",
    )
    def test_explicit_real_compiler_smoke(self) -> None:
        completed, receipt = self._run(compiler=Path(REAL_COMPILER))
        self.assertEqual(completed.returncode, 0, completed.stdout)
        self.assertEqual(receipt["result"], "passed")
        self.assertEqual(receipt["caseCount"], 49)


if __name__ == "__main__":
    unittest.main()
