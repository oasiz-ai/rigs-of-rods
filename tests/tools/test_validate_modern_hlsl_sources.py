#!/usr/bin/env python3
"""Hostile tests for the fail-closed Shader Model 4 source compiler gate."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/validate_modern_hlsl_sources.py"
SPEC = importlib.util.spec_from_file_location("validate_modern_hlsl_sources", TOOL)
assert SPEC is not None and SPEC.loader is not None
VALIDATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


FAKE_FXC = r'''#!/usr/bin/env python3
import hashlib
import json
import os
from pathlib import Path
import sys

arguments = sys.argv[1:]
mode = os.environ.get("FAKE_FXC_MODE", "pass")
if mode == "error":
    print("error X9999: hostile compiler failure", file=sys.stderr)
    raise SystemExit(2)

try:
    output_index = arguments.index("/Fo") + 1
    target_index = arguments.index("/T") + 1
    entry_index = arguments.index("/E") + 1
except (ValueError, IndexError):
    print("error X9998: required flag missing", file=sys.stderr)
    raise SystemExit(3)

output_path = Path(arguments[output_index])
source_path = Path(arguments[-1])
defines = [arguments[index + 1] for index, value in enumerate(arguments[:-1]) if value == "/D"]
record = {
    "arguments": [
        "<temporary-output>.cso" if index == output_index else value
        for index, value in enumerate(arguments)
    ],
    "cwd_source_exists": source_path.is_file(),
    "defines": defines,
    "entry_point": arguments[entry_index],
    "target": arguments[target_index],
}
log_path = os.environ.get("FAKE_FXC_LOG")
if log_path:
    with Path(log_path).open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, sort_keys=True) + "\n")

if mode == "warning":
    print("warning X3557: hostile warning with successful exit")
if mode != "no-output":
    stable_arguments = list(arguments)
    stable_arguments[output_index] = "<temporary-output>.cso"
    payload = json.dumps(stable_arguments, separators=(",", ":")).encode("utf-8")
    payload += b"\0" + source_path.read_bytes()
    output_path.write_bytes(b"FAKE-DXBC\0" + hashlib.sha256(payload).digest())

mutation_path = os.environ.get("FAKE_FXC_MUTATE_PATH")
if mutation_path:
    target = Path(mutation_path)
    mutation_kind = os.environ.get("FAKE_FXC_MUTATE_KIND", "bytes")
    if mutation_kind == "bytes":
        with target.open("ab") as handle:
            handle.write(b"\n// hostile compiler mutation\n")
    elif mutation_kind == "symlink":
        replacement = target.with_name(target.name + ".hostile-real")
        target.rename(replacement)
        target.symlink_to(replacement.name)
    elif mutation_kind == "create":
        target.write_text("// hostile new shader input\n", encoding="utf-8")
    else:
        raise SystemExit(4)
'''


def _program(
    name: str,
    source: str,
    stage: str,
    entry_point: str,
    target: str,
    defines: tuple[str, ...] = (),
) -> str:
    kind = "vertex_program" if stage == "vertex" else "fragment_program"
    define_line = (
        f"    preprocessor_defines {','.join(defines)}\n" if defines else ""
    )
    return textwrap.dedent(
        f"""\
        {kind} {name} hlsl
        {{
            source {source}
            entry_point {entry_point}
            target {target}
        {define_line}}}
        """
    )


class ModernHlslValidatorTests(unittest.TestCase):
    maxDiff = None

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="ror-hlsl-validator-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.repository = self.base / "repository"
        self.repository.mkdir()
        self.compiler = self.base / "fxc.exe"
        self.compiler.write_text(FAKE_FXC, encoding="utf-8")
        self.compiler.chmod(self.compiler.stat().st_mode | stat.S_IXUSR)
        self.log = self.base / "fxc-invocations.ndjson"
        self.evidence = self.base / "evidence.json"
        self._write_complete_fixture()

    def _write_complete_fixture(self) -> None:
        caelum = self.repository / "resources/caelum"
        pssm = (
            self.repository
            / "resources/managed_materials/shadows/pssm/on"
        )
        other = self.repository / "resources/materials"
        caelum.mkdir(parents=True)
        pssm.mkdir(parents=True)
        other.mkdir(parents=True)
        (caelum / "caelum.hlsl").write_text(
            "float4 MainVS(float4 p : POSITION) : SV_POSITION { return p; }\n"
            "float4 MainPS() : SV_TARGET { return 1; }\n",
            encoding="utf-8",
        )
        (pssm / "pssm.hlsl").write_text(
            "float4 MainVS(float4 p : POSITION) : SV_POSITION { return p; }\n"
            "float4 MainPS() : SV_TARGET { return 1; }\n",
            encoding="utf-8",
        )
        (other / "other.hlsl").write_text(
            "float4 MainVS(float4 p : POSITION) : SV_POSITION { return p; }\n",
            encoding="utf-8",
        )
        caelum_blocks = []
        for index in range(26):
            stage = "vertex" if index % 2 == 0 else "fragment"
            target = "vs_4_0" if stage == "vertex" else "ps_4_0"
            entry = "MainVS" if stage == "vertex" else "MainPS"
            caelum_blocks.append(
                _program(
                    f"Caelum/Test{index}/D3D11",
                    "caelum.hlsl",
                    stage,
                    entry,
                    target,
                    (f"CAELUM_TEST_{index}=1", "SHARED=1"),
                )
            )
        (caelum / "all.program").write_text("\n".join(caelum_blocks), encoding="utf-8")

        pssm_blocks = []
        for index in range(5):
            stage = "vertex" if index in {0, 3} else "fragment"
            target = "vs_4_0" if stage == "vertex" else "ps_4_0"
            entry = "MainVS" if stage == "vertex" else "MainPS"
            pssm_blocks.append(
                _program(
                    f"PSSM/Test{index}/D3D11",
                    "pssm.hlsl",
                    stage,
                    entry,
                    target,
                    (f"PSSM_TEST_{index}=1",),
                )
            )
        (pssm / "depthshadows.program").write_text(
            "\n".join(pssm_blocks), encoding="utf-8"
        )
        (other / "other.program").write_text(
            _program(
                "Other/NoDefines/D3D11",
                "other.hlsl",
                "vertex",
                "MainVS",
                "vs_4_0",
            ),
            encoding="utf-8",
        )

    def _run(
        self,
        *,
        repository: Path | None = None,
        compiler: Path | None = None,
        mode: str = "pass",
        evidence: Path | None = None,
        mutate_path: Path | None = None,
        mutate_kind: str = "bytes",
    ) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment["FAKE_FXC_LOG"] = str(self.log)
        environment["FAKE_FXC_MODE"] = mode
        if mutate_path is not None:
            environment["FAKE_FXC_MUTATE_PATH"] = str(mutate_path)
            environment["FAKE_FXC_MUTATE_KIND"] = mutate_kind
        return subprocess.run(
            [
                sys.executable,
                str(TOOL),
                "--repository-root",
                str(repository or self.repository),
                "--fxc",
                str(compiler or self.compiler),
                "--evidence-out",
                str(evidence or self.evidence),
            ],
            check=False,
            capture_output=True,
            text=True,
            env=environment,
        )

    def test_compiles_complete_closure_and_emits_deterministic_limited_claims(self) -> None:
        first = self._run()
        self.assertEqual(first.returncode, 0, first.stderr)
        first_bytes = self.evidence.read_bytes()
        receipt = json.loads(first_bytes)
        self.assertEqual(receipt["schema"], VALIDATOR.EVIDENCE_SCHEMA)
        self.assertEqual(receipt["case_count"], 32)
        self.assertEqual(receipt["required_case_counts"], {"caelum": 26, "managed_pssm": 5})
        self.assertEqual(
            receipt["claims"],
            {
                "hlsl_source_compile_proven": True,
                "ogre_resource_load_proven": False,
                "rendered_frame_proven": False,
                "runtime_playability_proven": False,
            },
        )
        self.assertEqual(
            receipt["input_integrity"],
            {
                "compiler_reverified": True,
                "declaration_script_count": 3,
                "hlsl_source_count": 3,
                "repository_input_manifest_sha256": receipt["input_integrity"][
                    "repository_input_manifest_sha256"
                ],
                "reverified_before_and_after_each_compile": True,
            },
        )
        self.assertEqual(
            receipt["compiler"]["sha256"],
            VALIDATOR._sha256_bytes(self.compiler.read_bytes()),
        )
        self.assertTrue(all(case["stdout"] == "" for case in receipt["cases"]))
        self.assertTrue(all(case["stderr"] == "" for case in receipt["cases"]))
        self.assertFalse(list(self.repository.rglob("*.cso")))

        self.log.unlink()
        second_evidence = self.base / "evidence-second.json"
        second = self._run(evidence=second_evidence)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(second_evidence.read_bytes(), first_bytes)

        records = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual(len(records), 32)
        for record in records:
            arguments = record["arguments"]
            self.assertEqual(
                arguments[:7],
                [
                    "/nologo",
                    "/WX",
                    "/Ges",
                    "/T",
                    record["target"],
                    "/E",
                    record["entry_point"],
                ],
            )
            self.assertIn("/Fo", arguments)
            self.assertTrue(record["cwd_source_exists"])
        defined = next(record for record in records if record["defines"])
        self.assertGreaterEqual(defined["arguments"].count("/D"), 1)
        no_defines = next(record for record in records if not record["defines"])
        self.assertNotIn("/D", no_defines["arguments"])

    def test_current_repository_discovers_and_fake_compiles_every_explicit_sm4_route(self) -> None:
        evidence = VALIDATOR.validate_repository(ROOT, self.compiler)
        self.assertEqual(
            evidence["required_case_counts"], {"caelum": 26, "managed_pssm": 5}
        )
        self.assertEqual(evidence["case_count"], 74)
        self.assertEqual(
            len({case["case_id"] for case in evidence["cases"]}),
            evidence["case_count"],
        )

    def test_crlf_declaration_is_not_silently_dropped(self) -> None:
        script = self.repository / "resources/materials/other.program"
        script.write_bytes(script.read_bytes().replace(b"\n", b"\r\n"))
        result = self._run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.evidence.read_text())["case_count"], 32)

    def test_missing_required_case_fails_before_compiler_execution(self) -> None:
        script = self.repository / "resources/caelum/all.program"
        text = script.read_text(encoding="utf-8")
        first_close = text.index("}") + 1
        script.write_text(text[first_close:], encoding="utf-8")
        result = self._run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected 26, found 25", result.stderr)
        self.assertFalse(self.evidence.exists())
        self.assertFalse(self.log.exists())

    def test_missing_and_symlinked_compilers_fail_closed(self) -> None:
        missing_path = self.base / "missing" / "fxc.exe"
        missing = self._run(compiler=missing_path)
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("missing or not a regular file", missing.stderr)
        self.assertFalse(self.evidence.exists())

        if os.name == "nt":
            self.skipTest("ordinary Windows test users cannot reliably create symlinks")
        linked = self.base / "linked-fxc.exe"
        linked.symlink_to(self.compiler)
        symlinked = self._run(compiler=linked)
        self.assertNotEqual(symlinked.returncode, 0)
        self.assertIn("must be an explicit fxc.exe path", symlinked.stderr)
        exact_name_link = self.base / "linked" / "fxc.exe"
        exact_name_link.parent.mkdir()
        exact_name_link.symlink_to(self.compiler)
        symlinked = self._run(compiler=exact_name_link)
        self.assertNotEqual(symlinked.returncode, 0)
        self.assertIn("must not be a symlink", symlinked.stderr)

    def test_legacy_source_and_stage_target_mismatch_fail_closed(self) -> None:
        script = self.repository / "resources/caelum/all.program"
        original = script.read_text(encoding="utf-8")
        script.write_text(
            original.replace("source caelum.hlsl", "source caelum.cg", 1),
            encoding="utf-8",
        )
        legacy = self._run()
        self.assertNotEqual(legacy.returncode, 0)
        self.assertIn("legacy or non-HLSL source", legacy.stderr)
        self.assertFalse(self.log.exists())

        script.write_text(original.replace("target vs_4_0", "target ps_4_0", 1), encoding="utf-8")
        mismatch = self._run()
        self.assertNotEqual(mismatch.returncode, 0)
        self.assertIn("stage/target mismatch", mismatch.stderr)
        self.assertFalse(self.log.exists())

    def test_malformed_declaration_and_missing_target_fail_closed(self) -> None:
        script = self.repository / "resources/materials/other.program"
        original = script.read_text(encoding="utf-8")
        script.write_text(original.replace(" hlsl\n", " hlsl unexpected\n"), encoding="utf-8")
        malformed = self._run()
        self.assertNotEqual(malformed.returncode, 0)
        self.assertIn("malformed HLSL program declaration", malformed.stderr)
        self.assertFalse(self.log.exists())

        script.write_text(
            original.replace("    target vs_4_0\n", ""), encoding="utf-8"
        )
        missing = self._run()
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("missing target", missing.stderr)
        self.assertFalse(self.log.exists())

    def test_ambiguous_source_and_duplicate_directive_fail_closed(self) -> None:
        duplicate = self.repository / "resources/duplicate"
        duplicate.mkdir()
        (duplicate / "caelum.hlsl").write_text("duplicate", encoding="utf-8")
        ambiguous = self._run()
        self.assertNotEqual(ambiguous.returncode, 0)
        self.assertIn("ambiguous or missing HLSL source", ambiguous.stderr)
        self.assertFalse(self.log.exists())

        (duplicate / "caelum.hlsl").unlink()
        script = self.repository / "resources/caelum/all.program"
        text = script.read_text(encoding="utf-8")
        script.write_text(
            text.replace(
                "source caelum.hlsl",
                "source caelum.hlsl\n    source caelum.hlsl",
                1,
            ),
            encoding="utf-8",
        )
        duplicated = self._run()
        self.assertNotEqual(duplicated.returncode, 0)
        self.assertIn("ambiguous duplicate source", duplicated.stderr)
        self.assertFalse(self.log.exists())

    def test_warning_error_and_missing_bytecode_never_emit_success_evidence(
        self,
    ) -> None:
        for mode, expected in (
            ("warning", "warning or error diagnostic"),
            ("error", "failed with exit code 2"),
            ("no-output", "did not create bytecode"),
        ):
            with self.subTest(mode=mode):
                self.evidence.unlink(missing_ok=True)
                self.log.unlink(missing_ok=True)
                result = self._run(mode=mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stderr)
                self.assertFalse(self.evidence.exists())
                self.assertFalse(list(self.repository.rglob("*.cso")))

    def test_existing_evidence_is_preserved_without_compiler_execution(self) -> None:
        sentinel = b"caller-owned evidence must remain byte-exact\n"
        self.evidence.write_bytes(sentinel)
        result = self._run(mode="error")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("evidence output already exists", result.stderr)
        self.assertEqual(self.evidence.read_bytes(), sentinel)
        self.assertFalse(self.log.exists())

    def test_atomic_publication_loses_race_without_overwriting_winner(self) -> None:
        sentinel = b"concurrent winner must remain byte-exact\n"
        real_link = os.link

        def inject_concurrent_winner(
            source: str | os.PathLike[str],
            destination: str | os.PathLike[str],
            *,
            follow_symlinks: bool = True,
        ) -> None:
            Path(destination).write_bytes(sentinel)
            real_link(source, destination, follow_symlinks=follow_symlinks)

        with mock.patch.object(
            VALIDATOR.os, "link", side_effect=inject_concurrent_winner
        ):
            with self.assertRaisesRegex(
                VALIDATOR.ValidationFailure, "evidence output already exists"
            ):
                VALIDATOR.write_evidence(
                    self.evidence,
                    {"schema": VALIDATOR.EVIDENCE_SCHEMA, "status": "passed"},
                )
        self.assertEqual(self.evidence.read_bytes(), sentinel)
        self.assertFalse(list(self.evidence.parent.glob(f".{self.evidence.name}.*")))

    def test_compiler_byte_mutation_during_compile_fails_closed(self) -> None:
        result = self._run(mutate_path=self.compiler)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("immutable input bytes changed after", result.stderr)
        self.assertIn("fxc.exe", result.stderr)
        self.assertFalse(self.evidence.exists())
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_declaration_byte_mutation_during_compile_fails_closed(self) -> None:
        declaration = self.repository / "resources/caelum/all.program"
        result = self._run(mutate_path=declaration)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("immutable input bytes changed after", result.stderr)
        self.assertIn("resources/caelum/all.program", result.stderr)
        self.assertFalse(self.evidence.exists())
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_hlsl_byte_mutation_during_compile_fails_closed(self) -> None:
        source = self.repository / "resources/caelum/caelum.hlsl"
        result = self._run(mutate_path=source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("immutable input bytes changed after", result.stderr)
        self.assertIn("resources/caelum/caelum.hlsl", result.stderr)
        self.assertFalse(self.evidence.exists())
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_shader_input_inventory_growth_during_compile_fails_closed(self) -> None:
        added_source = self.repository / "resources/caelum/hostile.hlsl"
        result = self._run(mutate_path=added_source, mutate_kind="create")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("immutable shader input inventory changed after", result.stderr)
        self.assertFalse(self.evidence.exists())
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_hlsl_symlink_swap_during_compile_fails_closed(self) -> None:
        if os.name == "nt":
            self.skipTest("ordinary Windows test users cannot reliably create symlinks")
        source = self.repository / "resources/caelum/caelum.hlsl"
        result = self._run(mutate_path=source, mutate_kind="symlink")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("immutable input changed after", result.stderr)
        self.assertIn("resources/caelum/caelum.hlsl", result.stderr)
        self.assertFalse(self.evidence.exists())
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_shader_source_symlink_and_evidence_symlink_fail_closed(self) -> None:
        if os.name == "nt":
            self.skipTest("ordinary Windows test users cannot reliably create symlinks")
        source = self.repository / "resources/caelum/caelum.hlsl"
        real_source = source.with_name("caelum-real.hlsl")
        source.rename(real_source)
        source.symlink_to(real_source.name)
        linked_source = self._run()
        self.assertNotEqual(linked_source.returncode, 0)
        self.assertIn("symlink", linked_source.stderr)
        self.assertFalse(self.evidence.exists())

        source.unlink()
        real_source.rename(source)
        target = self.base / "existing-evidence.json"
        target.write_text("do not overwrite", encoding="utf-8")
        self.evidence.symlink_to(target)
        linked_evidence = self._run()
        self.assertNotEqual(linked_evidence.returncode, 0)
        self.assertIn("evidence output must not be a symlink", linked_evidence.stderr)
        self.assertEqual(target.read_text(encoding="utf-8"), "do not overwrite")


if __name__ == "__main__":
    unittest.main()
