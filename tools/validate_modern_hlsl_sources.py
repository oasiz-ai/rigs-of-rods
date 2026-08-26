#!/usr/bin/env python3
"""Compile the complete desktop resource HLSL closure with Windows ``fxc``.

This is deliberately a source-compiler gate.  A successful receipt proves that
the declared entry points and standalone MyGUI sources compile with strict
Shader Model 4 settings.  It does not prove that Ogre loaded the programs,
bound their resources, rendered a frame, or produced a playable runtime.  The
nine source-tree RTShader HLSL libraries retain their combined-sampler ABI and
are inventoried but explicitly excluded; they are also distinct from OGRE 14's
pinned live RTShaderLib package.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
from typing import Iterable, Sequence


EVIDENCE_SCHEMA = "ror.modern-hlsl-source-compile-evidence.v2"
SCRIPT_SUFFIXES = frozenset({".material", ".program"})
IMMUTABLE_INPUT_SUFFIXES = SCRIPT_SUFFIXES | frozenset({".hlsl"})
MODERN_TARGETS = frozenset({"vs_4_0", "ps_4_0"})
EXPECTED_STAGE_TARGET = {"vertex": "vs_4_0", "fragment": "ps_4_0"}
EXPECTED_REQUIRED_CASES = {"caelum": 26, "managed_pssm": 5}
EXPECTED_DECLARED_CASE_COUNTS = {
    "caelum": 26,
    "fresnel": 3,
    "general": 11,
    "grass": 2,
    "managed-nicemetal": 10,
    "managed-pssm": 5,
    "nicemetal": 10,
    "postprocess": 2,
    "skyx": 19,
    "stdquad": 5,
}
EXPECTED_FAMILY_CASE_COUNTS = {
    **EXPECTED_DECLARED_CASE_COUNTS,
    "mygui": 4,
}
EXPECTED_DECLARED_CASE_COUNT = 93
EXPECTED_TOTAL_CASE_COUNT = 97
EXPECTED_HLSL_SOURCE_COUNT = 36
EXPECTED_COMPILED_HLSL_SOURCE_COUNT = 27
EXPECTED_EXCLUDED_RTSHADER_SOURCE_COUNT = 9
EXPECTED_MYGUI_STAGES = {
    "MyGUI_FP.hlsl": "fragment",
    "MyGUI_Ogre_FP.hlsl": "fragment",
    "MyGUI_Ogre_VP.hlsl": "vertex",
    "MyGUI_VP.hlsl": "vertex",
}
EXPECTED_RTSHADER_LIBRARIES = {
    "FFPLib_Common.hlsl",
    "FFPLib_Fog.hlsl",
    "FFPLib_Lighting.hlsl",
    "FFPLib_Texturing.hlsl",
    "FFPLib_Transform.hlsl",
    "SGXLib_IntegratedPSSM.hlsl",
    "SGXLib_NormalMapLighting.hlsl",
    "SGXLib_PerPixelLighting.hlsl",
    "SampleLib_ReflectionMap.hlsl",
}
MAX_COMPILER_OUTPUT_BYTES = 1024 * 1024
MAX_AGGREGATE_COMPILER_DIAGNOSTICS_BYTES = 1024 * 1024
AGGREGATE_COMPILER_FAILURE_METADATA_RESERVE_BYTES = 512
COMPILER_FAILURE_TRUNCATION_MARKER = (
    "\n<compiler failure detail truncated at aggregate byte limit>"
)
MAX_SOURCE_BYTES = 8 * 1024 * 1024
MAX_CASES = 4096

HLSL_PROGRAM_HEADER = re.compile(
    r"(?m)^[ \t]*(?P<kind>[A-Za-z_][A-Za-z0-9_]*)_program"
    r"[ \t]+(?P<name>[^\s{}]+)[ \t]+hlsl\b"
    r"[ \t\r]*(?P<open>\{)?[ \t\r]*$"
)
HLSL_PROGRAM_CANDIDATE = re.compile(
    r"(?m)^[ \t]*[A-Za-z_][A-Za-z0-9_]*_program"
    r"[ \t]+[^\s{}]+[ \t]+hlsl\b"
)
DIRECTIVE = re.compile(
    r"^(?P<key>source|entry_point|target|preprocessor_defines)"
    r"(?:[ \t]+(?P<value>.*?))?[ \t]*$"
)
DEFINE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:=[A-Za-z0-9_+.-]+)?$")
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
DIAGNOSTIC = re.compile(r"(?i)\b(?:warning|error)\b")


class ValidationFailure(RuntimeError):
    """A deterministic, fail-closed validation error."""


@dataclass(frozen=True)
class HlslCase:
    """One declared or standalone modern HLSL compiler case."""

    evidence_kind: str
    family: str
    program: str
    stage: str
    target: str
    entry_point: str
    defines: tuple[str, ...]
    script_path: Path
    script_relative: str
    source_path: Path
    source_relative: str
    script_sha256: str
    source_sha256: str

    @property
    def case_id(self) -> str:
        if self.evidence_kind == "standalone-resource-source-compile":
            return f"standalone:{self.family}:{self.source_path.stem}:{self.stage}"
        return f"{self.script_relative}::{self.program}"


@dataclass(frozen=True)
class ImmutableFile:
    """Pre-compilation bytes and symlink state for one guarded input."""

    path: Path
    label: str
    relative: str | None
    sha256: str
    size: int


@dataclass(frozen=True)
class ImmutableInputs:
    """The complete compiler/declaration/source inventory guarded during fxc."""

    repository_root: Path
    compiler: ImmutableFile
    repository_files: tuple[ImmutableFile, ...]
    repository_paths: tuple[str, ...]
    manifest_sha256: str


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _read_regular_file(path: Path, label: str, maximum_bytes: int | None = None) -> bytes:
    if path.is_symlink():
        raise ValidationFailure(f"{label} must not be a symlink: {path.name}")
    if not path.is_file():
        raise ValidationFailure(f"{label} is missing or not a regular file: {path.name}")
    payload = path.read_bytes()
    if not payload:
        raise ValidationFailure(f"{label} is empty: {path.name}")
    if maximum_bytes is not None and len(payload) > maximum_bytes:
        raise ValidationFailure(f"{label} exceeds {maximum_bytes} bytes: {path.name}")
    return payload


def _repository_input_paths(repository_root: Path) -> tuple[Path, ...]:
    resources = repository_root / "resources"
    return tuple(
        sorted(
            path.absolute()
            for path in resources.rglob("*")
            if path.suffix.lower() in IMMUTABLE_INPUT_SUFFIXES
        )
    )


def snapshot_immutable_inputs(
    repository_root: Path, compiler_path: Path, compiler_payload: bytes
) -> ImmutableInputs:
    """Capture every byte input that can affect the declared SM4 compilation."""

    repository_root = repository_root.expanduser().absolute()
    compiler_path = compiler_path.expanduser().absolute()
    compiler = ImmutableFile(
        path=compiler_path,
        label="fxc.exe",
        relative=None,
        sha256=_sha256_bytes(compiler_payload),
        size=len(compiler_payload),
    )
    repository_files: list[ImmutableFile] = []
    for path in _repository_input_paths(repository_root):
        relative = path.relative_to(repository_root).as_posix()
        _assert_no_repository_symlink(
            repository_root, path, f"immutable shader input {relative}"
        )
        payload = _read_regular_file(
            path,
            f"immutable shader input {relative}",
            maximum_bytes=MAX_SOURCE_BYTES if path.suffix.lower() == ".hlsl" else None,
        )
        repository_files.append(
            ImmutableFile(
                path=path,
                label=relative,
                relative=relative,
                sha256=_sha256_bytes(payload),
                size=len(payload),
            )
        )
    repository_paths = tuple(item.relative for item in repository_files)
    if any(relative is None for relative in repository_paths):
        raise ValidationFailure("repository input inventory contains an invalid path")
    manifest_payload = json.dumps(
        [
            {"path": item.relative, "sha256": item.sha256, "size": item.size}
            for item in repository_files
        ],
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    snapshot = ImmutableInputs(
        repository_root=repository_root,
        compiler=compiler,
        repository_files=tuple(repository_files),
        repository_paths=tuple(relative for relative in repository_paths if relative),
        manifest_sha256=_sha256_bytes(manifest_payload),
    )
    verify_immutable_inputs(snapshot, "before compilation")
    return snapshot


def verify_immutable_inputs(inputs: ImmutableInputs, phase: str) -> None:
    """Fail if guarded bytes, symlink state, or repository inventory changed."""

    current_paths = tuple(
        path.relative_to(inputs.repository_root).as_posix()
        for path in _repository_input_paths(inputs.repository_root)
    )
    if current_paths != inputs.repository_paths:
        raise ValidationFailure(
            f"immutable shader input inventory changed {phase}"
        )

    for item in (inputs.compiler, *inputs.repository_files):
        try:
            if item.relative is not None:
                _assert_no_repository_symlink(
                    inputs.repository_root,
                    item.path,
                    f"immutable shader input {item.relative}",
                )
            payload = _read_regular_file(item.path, f"immutable input {item.label}")
        except ValidationFailure as exc:
            raise ValidationFailure(
                f"immutable input changed {phase}: {item.label}"
            ) from exc
        if len(payload) != item.size or _sha256_bytes(payload) != item.sha256:
            raise ValidationFailure(
                f"immutable input bytes changed {phase}: {item.label}"
            )


def _assert_no_repository_symlink(repository_root: Path, path: Path, label: str) -> None:
    """Reject a symlink in any repository-relative component of ``path``."""

    try:
        relative = path.absolute().relative_to(repository_root.absolute())
    except ValueError as exc:
        raise ValidationFailure(f"{label} escapes the repository") from exc

    current = repository_root.absolute()
    if current.is_symlink():
        raise ValidationFailure("repository root must not be a symlink")
    for component in relative.parts:
        current /= component
        if current.is_symlink():
            raise ValidationFailure(
                f"{label} contains a symlink component: {relative.as_posix()}"
            )


def _strip_comments(text: str, script_relative: str) -> str:
    """Replace C/C++ comments with spaces while retaining newlines and offsets."""

    output: list[str] = []
    index = 0
    in_block = False
    while index < len(text):
        if in_block:
            if text.startswith("*/", index):
                output.extend((" ", " "))
                index += 2
                in_block = False
            else:
                output.append("\n" if text[index] == "\n" else " ")
                index += 1
            continue

        if text.startswith("/*", index):
            output.extend((" ", " "))
            index += 2
            in_block = True
            continue
        if text.startswith("//", index):
            while index < len(text) and text[index] != "\n":
                output.append(" ")
                index += 1
            continue
        output.append(text[index])
        index += 1

    if in_block:
        raise ValidationFailure(f"unterminated block comment in {script_relative}")
    return "".join(output)


def _matching_brace(text: str, opening_index: int, script_relative: str) -> int:
    depth = 0
    for index in range(opening_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
            if depth < 0:
                break
    raise ValidationFailure(f"unbalanced program block in {script_relative}")


def _top_level_directives(body: str, case_label: str) -> dict[str, str]:
    values: dict[str, str] = {}
    depth = 0
    for line in body.splitlines():
        stripped = line.strip()
        if depth == 0 and stripped:
            match = DIRECTIVE.fullmatch(stripped)
            if match:
                key = match.group("key")
                value = (match.group("value") or "").strip()
                if key in values:
                    raise ValidationFailure(f"ambiguous duplicate {key} in {case_label}")
                if not value:
                    raise ValidationFailure(f"empty {key} in {case_label}")
                values[key] = value
        depth += line.count("{") - line.count("}")
        if depth < 0:
            raise ValidationFailure(f"unbalanced nested block in {case_label}")
    if depth != 0:
        raise ValidationFailure(f"unbalanced nested block in {case_label}")
    return values


def _parse_defines(raw: str | None, case_label: str) -> tuple[str, ...]:
    if raw is None:
        return ()
    defines = tuple(part.strip() for part in raw.split(","))
    if not defines or any(not part for part in defines):
        raise ValidationFailure(f"empty preprocessor define in {case_label}")
    names: set[str] = set()
    for define in defines:
        if not DEFINE.fullmatch(define):
            raise ValidationFailure(
                f"invalid preprocessor define {define!r} in {case_label}"
            )
        name = define.partition("=")[0]
        if name in names:
            raise ValidationFailure(
                f"ambiguous duplicate preprocessor define {name} in {case_label}"
            )
        names.add(name)
    return defines


def _iter_hlsl_blocks(text: str, script_relative: str) -> Iterable[tuple[str, str, str]]:
    stripped = _strip_comments(text, script_relative)
    candidates = list(HLSL_PROGRAM_CANDIDATE.finditer(stripped))
    headers = list(HLSL_PROGRAM_HEADER.finditer(stripped))
    if [match.start() for match in candidates] != [match.start() for match in headers]:
        raise ValidationFailure(f"malformed HLSL program declaration in {script_relative}")
    previous_end = -1
    for match in headers:
        opening_index = match.start("open") if match.group("open") else match.end()
        if not match.group("open"):
            while opening_index < len(stripped) and stripped[opening_index].isspace():
                opening_index += 1
            if opening_index >= len(stripped) or stripped[opening_index] != "{":
                raise ValidationFailure(
                    f"missing program block opener for {match.group('name')} in "
                    f"{script_relative}"
                )
        closing_index = _matching_brace(stripped, opening_index, script_relative)
        if match.start() < previous_end:
            raise ValidationFailure(f"nested HLSL program declaration in {script_relative}")
        previous_end = closing_index
        yield (
            match.group("kind"),
            match.group("name"),
            stripped[opening_index + 1 : closing_index],
        )


def _required_group(script_relative: str) -> str | None:
    if script_relative.startswith("resources/caelum/"):
        return "caelum"
    if script_relative == (
        "resources/managed_materials/shadows/pssm/on/depthshadows.program"
    ):
        return "managed_pssm"
    return None


def _declared_family(script_relative: str) -> str:
    if script_relative.startswith("resources/caelum/"):
        return "caelum"
    exact = {
        "resources/OgreCore/StdQuad_vp.program": "stdquad",
        "resources/SkyX/SkyX.material": "skyx",
        "resources/managed_materials/nicemetal_mm.program": "managed-nicemetal",
        (
            "resources/managed_materials/shadows/pssm/on/"
            "depthshadows.program"
        ): "managed-pssm",
        "resources/materials/fresnel.material": "fresnel",
        "resources/materials/general.program": "general",
        "resources/materials/grass.material": "grass",
        "resources/materials/nicemetal.program": "nicemetal",
        "resources/postprocess/ror_postprocess_v0a.program": "postprocess",
    }
    family = exact.get(script_relative)
    if family is None:
        raise ValidationFailure(
            "explicit HLSL declaration is outside the reviewed family map: "
            f"{script_relative}"
        )
    return family


def discover_cases(repository_root: Path) -> list[HlslCase]:
    """Resolve the exact declared and standalone strict-SM4 compiler closure."""

    repository_root = repository_root.expanduser().absolute()
    if repository_root.is_symlink() or not repository_root.is_dir():
        raise ValidationFailure("repository root must be a real directory, not a symlink")
    resources = repository_root / "resources"
    _assert_no_repository_symlink(repository_root, resources, "resource root")
    if not resources.is_dir():
        raise ValidationFailure("repository resources directory is missing")

    hlsl_by_name: dict[str, list[Path]] = {}
    for path in sorted(resources.rglob("*.hlsl")):
        if not path.is_file() and not path.is_symlink():
            continue
        hlsl_by_name.setdefault(path.name, []).append(path)

    cases: list[HlslCase] = []
    seen_programs: dict[str, str] = {}
    hlsl_header_count = 0
    for script_path in sorted(resources.rglob("*")):
        if script_path.suffix.lower() not in SCRIPT_SUFFIXES:
            continue
        if not script_path.is_file() and not script_path.is_symlink():
            continue
        script_relative = script_path.relative_to(repository_root).as_posix()
        _assert_no_repository_symlink(
            repository_root, script_path, f"declaration script {script_relative}"
        )
        script_payload = _read_regular_file(script_path, "declaration script")
        try:
            script_text = script_payload.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValidationFailure(
                f"declaration script is not UTF-8: {script_relative}"
            ) from exc

        for kind, program, body in _iter_hlsl_blocks(script_text, script_relative):
            hlsl_header_count += 1
            case_label = f"{script_relative}::{program}"
            directives = _top_level_directives(body, case_label)
            for required in ("source", "entry_point", "target"):
                if required not in directives:
                    raise ValidationFailure(f"missing {required} in {case_label}")
            target = directives.get("target")
            if target not in MODERN_TARGETS:
                raise ValidationFailure(
                    f"legacy or unsupported HLSL target {target} in {case_label}"
                )
            if kind not in EXPECTED_STAGE_TARGET:
                raise ValidationFailure(f"unsupported SM4 shader stage {kind} in {case_label}")
            if target != EXPECTED_STAGE_TARGET[kind]:
                raise ValidationFailure(
                    f"stage/target mismatch in {case_label}: {kind} cannot use {target}"
                )

            source_token = directives["source"]
            source_posix = PurePosixPath(source_token)
            if (
                source_posix.is_absolute()
                or "\\" in source_token
                or any(part in {"", ".", ".."} for part in source_posix.parts)
            ):
                raise ValidationFailure(f"ambiguous or unsafe source path in {case_label}")
            if source_posix.suffix.lower() != ".hlsl":
                raise ValidationFailure(f"legacy or non-HLSL source in {case_label}")

            matches = hlsl_by_name.get(source_posix.name, [])
            if len(matches) != 1:
                raise ValidationFailure(
                    f"ambiguous or missing HLSL source {source_posix.name} in {case_label}"
                )
            expected_source = script_path.parent.joinpath(*source_posix.parts).absolute()
            source_path = matches[0].absolute()
            if source_path != expected_source:
                raise ValidationFailure(
                    f"source does not resolve beside its declaration in {case_label}"
                )
            source_relative = source_path.relative_to(repository_root).as_posix()
            _assert_no_repository_symlink(
                repository_root, source_path, f"HLSL source {source_relative}"
            )
            source_payload = _read_regular_file(
                source_path, "HLSL source", maximum_bytes=MAX_SOURCE_BYTES
            )

            entry_point = directives["entry_point"]
            if not IDENTIFIER.fullmatch(entry_point):
                raise ValidationFailure(f"invalid entry point in {case_label}")
            if program in seen_programs:
                raise ValidationFailure(
                    f"ambiguous duplicate SM4 program {program}: "
                    f"{seen_programs[program]} and {script_relative}"
                )
            seen_programs[program] = script_relative

            cases.append(
                HlslCase(
                    evidence_kind="declared-resource-program-source-compile",
                    family=_declared_family(script_relative),
                    program=program,
                    stage=kind,
                    target=target,
                    entry_point=entry_point,
                    defines=_parse_defines(
                        directives.get("preprocessor_defines"), case_label
                    ),
                    script_path=script_path,
                    script_relative=script_relative,
                    source_path=source_path,
                    source_relative=source_relative,
                    script_sha256=_sha256_bytes(script_payload),
                    source_sha256=_sha256_bytes(source_payload),
                )
            )

    if hlsl_header_count == 0:
        raise ValidationFailure("no HLSL program declarations were discovered")
    if not cases:
        raise ValidationFailure("no explicit Shader Model 4 HLSL cases were discovered")
    family_counts = {
        family: sum(case.family == family for case in cases)
        for family in EXPECTED_DECLARED_CASE_COUNTS
    }
    for family, expected in EXPECTED_DECLARED_CASE_COUNTS.items():
        actual = family_counts[family]
        if actual != expected:
            legacy_label = family.replace("-", "_")
            raise ValidationFailure(
                f"required {legacy_label} SM4 case closure is incomplete: "
                f"expected {expected}, found {actual}"
            )
    if len(cases) != EXPECTED_DECLARED_CASE_COUNT:
        raise ValidationFailure(
            "explicit resource HLSL closure is incomplete: "
            f"expected {EXPECTED_DECLARED_CASE_COUNT}, found {len(cases)}"
        )

    cases.extend(_discover_mygui_cases(resources, repository_root))
    _assert_hlsl_source_closure(resources, repository_root, cases)
    if len(cases) != EXPECTED_TOTAL_CASE_COUNT:
        raise ValidationFailure(
            f"desktop HLSL compiler closure must contain {EXPECTED_TOTAL_CASE_COUNT} "
            f"cases, found {len(cases)}"
        )
    if len(cases) > MAX_CASES:
        raise ValidationFailure(f"discovered more than {MAX_CASES} SM4 cases")

    cases.sort(key=lambda case: case.case_id)
    group_counts = {key: 0 for key in EXPECTED_REQUIRED_CASES}
    for case in cases:
        group = _required_group(case.script_relative)
        if group is not None:
            group_counts[group] += 1
    for group, expected in EXPECTED_REQUIRED_CASES.items():
        actual = group_counts[group]
        if actual != expected:
            raise ValidationFailure(
                f"required {group} SM4 case closure is incomplete: "
                f"expected {expected}, found {actual}"
            )
    return cases


def _discover_mygui_cases(resources: Path, repository_root: Path) -> list[HlslCase]:
    mygui_root = resources / "mygui"
    _assert_no_repository_symlink(repository_root, mygui_root, "MyGUI HLSL root")
    source_paths = sorted(mygui_root.glob("*.hlsl"))
    actual_names = {path.name for path in source_paths}
    if actual_names != set(EXPECTED_MYGUI_STAGES):
        raise ValidationFailure(
            "standalone MyGUI HLSL closure changed: "
            f"expected {sorted(EXPECTED_MYGUI_STAGES)}, found {sorted(actual_names)}"
        )

    cases: list[HlslCase] = []
    for source_path in source_paths:
        source_relative = source_path.relative_to(repository_root).as_posix()
        _assert_no_repository_symlink(
            repository_root, source_path, f"standalone HLSL source {source_relative}"
        )
        payload = _read_regular_file(
            source_path, "standalone MyGUI HLSL source", MAX_SOURCE_BYTES
        )
        try:
            payload.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValidationFailure(
                f"standalone MyGUI HLSL source is not UTF-8: {source_relative}"
            ) from exc
        stage = EXPECTED_MYGUI_STAGES[source_path.name]
        cases.append(
            HlslCase(
                evidence_kind="standalone-resource-source-compile",
                family="mygui",
                program=source_path.stem,
                stage=stage,
                target=EXPECTED_STAGE_TARGET[stage],
                entry_point="main",
                defines=(),
                script_path=source_path,
                script_relative="",
                source_path=source_path,
                source_relative=source_relative,
                script_sha256="",
                source_sha256=_sha256_bytes(payload),
            )
        )
    return cases


def _excluded_rtshader_sources(
    resources: Path, repository_root: Path
) -> tuple[Path, ...]:
    rtshader_root = resources / "rtshader"
    _assert_no_repository_symlink(
        repository_root, rtshader_root, "RTShader compatibility HLSL root"
    )
    source_paths = tuple(sorted(rtshader_root.glob("*.hlsl")))
    actual_names = {path.name for path in source_paths}
    if actual_names != EXPECTED_RTSHADER_LIBRARIES:
        raise ValidationFailure(
            "excluded RTShader HLSL compatibility closure changed: "
            f"expected {sorted(EXPECTED_RTSHADER_LIBRARIES)}, "
            f"found {sorted(actual_names)}"
        )
    for source_path in source_paths:
        relative = source_path.relative_to(repository_root).as_posix()
        _assert_no_repository_symlink(
            repository_root, source_path, f"excluded RTShader HLSL source {relative}"
        )
        payload = _read_regular_file(
            source_path, "excluded RTShader HLSL source", MAX_SOURCE_BYTES
        )
        try:
            payload.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValidationFailure(
                f"excluded RTShader HLSL source is not UTF-8: {relative}"
            ) from exc
    return source_paths


def _assert_hlsl_source_closure(
    resources: Path, repository_root: Path, cases: Sequence[HlslCase]
) -> None:
    excluded_paths = {
        path.absolute()
        for path in _excluded_rtshader_sources(resources, repository_root)
    }
    if len(excluded_paths) != EXPECTED_EXCLUDED_RTSHADER_SOURCE_COUNT:
        raise ValidationFailure("excluded RTShader HLSL source count changed")
    actual_paths = {
        path.absolute()
        for path in resources.rglob("*.hlsl")
        if path.is_file() or path.is_symlink()
    }
    if len(actual_paths) != EXPECTED_HLSL_SOURCE_COUNT:
        raise ValidationFailure(
            "resource HLSL inventory changed: "
            f"expected {EXPECTED_HLSL_SOURCE_COUNT}, found {len(actual_paths)}"
        )
    compiled_paths = {case.source_path.absolute() for case in cases}
    if len(compiled_paths) != EXPECTED_COMPILED_HLSL_SOURCE_COUNT:
        raise ValidationFailure(
            "compiled resource HLSL source closure changed: "
            f"expected {EXPECTED_COMPILED_HLSL_SOURCE_COUNT}, "
            f"found {len(compiled_paths)}"
        )
    if compiled_paths & excluded_paths:
        raise ValidationFailure("an excluded RTShader HLSL source entered the compiler cases")
    accounted_paths = compiled_paths | excluded_paths
    if accounted_paths != actual_paths:
        missing_cases = sorted(
            path.relative_to(repository_root).as_posix()
            for path in actual_paths - accounted_paths
        )
        missing_sources = sorted(
            path.relative_to(repository_root).as_posix()
            for path in accounted_paths - actual_paths
        )
        raise ValidationFailure(
            "resource HLSL source closure is not fully accounted: "
            f"missing compiler/exclusion cases={missing_cases}, "
            f"missing sources={missing_sources}"
        )


def _normalise_output(payload: bytes) -> str:
    if len(payload) > MAX_COMPILER_OUTPUT_BYTES:
        raise ValidationFailure(
            f"compiler output exceeds {MAX_COMPILER_OUTPUT_BYTES} bytes"
        )
    return payload.decode("utf-8", errors="replace").replace("\r\n", "\n").replace(
        "\r", "\n"
    )


def _utf8_prefix(text: str, maximum_bytes: int) -> str:
    """Return the longest valid UTF-8 prefix that fits the byte budget."""

    if maximum_bytes <= 0:
        return ""
    payload = text.encode("utf-8")
    if len(payload) <= maximum_bytes:
        return text
    return payload[:maximum_bytes].decode("utf-8", errors="ignore")


def _validate_compiler(fxc_path: Path) -> tuple[Path, bytes]:
    fxc_path = fxc_path.expanduser().absolute()
    if fxc_path.name.lower() != "fxc.exe":
        raise ValidationFailure("compiler must be an explicit fxc.exe path")
    if fxc_path.is_symlink():
        raise ValidationFailure("fxc.exe must not be a symlink")
    compiler_payload = _read_regular_file(fxc_path, "fxc.exe")
    if os.name != "nt" and not os.access(fxc_path, os.X_OK):
        raise ValidationFailure("fxc.exe is not executable")
    return fxc_path, compiler_payload


def _normalised_arguments(case: HlslCase) -> list[str]:
    arguments = [
        "/nologo",
        "/WX",
        "/Ges",
        "/T",
        case.target,
        "/E",
        case.entry_point,
    ]
    for define in case.defines:
        arguments.extend(("/D", define))
    arguments.extend(("/Fo", "<temporary-output>.cso", case.source_path.name))
    return arguments


def compile_cases(
    cases: Sequence[HlslCase],
    fxc_path: Path,
    immutable_inputs: ImmutableInputs,
    timeout_seconds: int = 60,
) -> list[dict[str, object]]:
    """Compile every case and return evidence only when all cases succeed."""

    if timeout_seconds < 1 or timeout_seconds > 600:
        raise ValidationFailure("compiler timeout must be between 1 and 600 seconds")
    results: list[dict[str, object]] = []
    compiler_failures: list[str] = []
    compiler_failure_count = 0
    aggregate_diagnostic_bytes = 0
    omitted_compiler_failure_count = 0
    aggregate_diagnostic_limit_reached = False
    aggregate_detail_limit = (
        MAX_AGGREGATE_COMPILER_DIAGNOSTICS_BYTES
        - AGGREGATE_COMPILER_FAILURE_METADATA_RESERVE_BYTES
    )
    with tempfile.TemporaryDirectory(prefix="ror-hlsl-fxc-") as temporary:
        temporary_root = Path(temporary)
        for index, case in enumerate(cases):
            verify_immutable_inputs(immutable_inputs, f"before {case.case_id}")
            output_path = temporary_root / f"{index:04d}.cso"
            normalised_arguments = _normalised_arguments(case)
            real_arguments = [
                str(output_path) if value == "<temporary-output>.cso" else value
                for value in normalised_arguments
            ]
            command = [str(fxc_path), *real_arguments]
            try:
                completed = subprocess.run(
                    command,
                    cwd=case.source_path.parent,
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=timeout_seconds,
                )
            except subprocess.TimeoutExpired as exc:
                verify_immutable_inputs(immutable_inputs, f"after {case.case_id}")
                raise ValidationFailure(f"fxc.exe timed out for {case.case_id}") from exc
            except OSError as exc:
                verify_immutable_inputs(immutable_inputs, f"after {case.case_id}")
                raise ValidationFailure(f"could not execute fxc.exe for {case.case_id}") from exc

            verify_immutable_inputs(immutable_inputs, f"after {case.case_id}")
            stdout = _normalise_output(completed.stdout)
            stderr = _normalise_output(completed.stderr)
            if completed.returncode != 0:
                diagnostics = "\n".join(
                    f"{label}:\n{output.strip()}"
                    for label, output in (("stdout", stdout), ("stderr", stderr))
                    if output.strip()
                )
                diagnostic_suffix = (
                    f"\ncompiler diagnostics:\n{diagnostics}"
                    if diagnostics
                    else "\ncompiler diagnostics: <empty>"
                )
                failure = (
                    f"fxc.exe failed with exit code {completed.returncode} "
                    f"for {case.case_id}{diagnostic_suffix}"
                )
                compiler_failure_count += 1
                failure_bytes = len(failure.encode("utf-8"))
                separator_bytes = 2 if compiler_failures else 0
                remaining_bytes = (
                    aggregate_detail_limit
                    - aggregate_diagnostic_bytes
                    - separator_bytes
                )
                if aggregate_diagnostic_limit_reached:
                    omitted_compiler_failure_count += 1
                elif failure_bytes <= remaining_bytes:
                    compiler_failures.append(failure)
                    aggregate_diagnostic_bytes += separator_bytes + failure_bytes
                else:
                    marker_bytes = len(
                        COMPILER_FAILURE_TRUNCATION_MARKER.encode("utf-8")
                    )
                    if remaining_bytes >= marker_bytes:
                        prefix = _utf8_prefix(
                            failure,
                            remaining_bytes - marker_bytes,
                        )
                        truncated_failure = (
                            prefix + COMPILER_FAILURE_TRUNCATION_MARKER
                        )
                        compiler_failures.append(truncated_failure)
                        aggregate_diagnostic_bytes += separator_bytes + len(
                            truncated_failure.encode("utf-8")
                        )
                    else:
                        omitted_compiler_failure_count += 1
                    aggregate_diagnostic_limit_reached = True
                continue
            if stderr.strip():
                raise ValidationFailure(f"fxc.exe wrote to stderr for {case.case_id}")
            if DIAGNOSTIC.search(stdout) or DIAGNOSTIC.search(stderr):
                raise ValidationFailure(
                    f"fxc.exe emitted a warning or error diagnostic for {case.case_id}"
                )
            if output_path.is_symlink() or not output_path.is_file():
                raise ValidationFailure(f"fxc.exe did not create bytecode for {case.case_id}")
            bytecode = output_path.read_bytes()
            if not bytecode:
                raise ValidationFailure(f"fxc.exe created empty bytecode for {case.case_id}")

            results.append(
                {
                    "arguments": normalised_arguments,
                    "bytecode_sha256": _sha256_bytes(bytecode),
                    "bytecode_size": len(bytecode),
                    "case_id": case.case_id,
                    "defines": list(case.defines),
                    "entry_point": case.entry_point,
                    "evidence_kind": case.evidence_kind,
                    "family": case.family,
                    "program": case.program,
                    "script": case.script_relative or None,
                    "script_sha256": case.script_sha256 or None,
                    "source": case.source_relative,
                    "source_sha256": case.source_sha256,
                    "stage": case.stage,
                    "stderr": stderr,
                    "stdout": stdout,
                    "target": case.target,
                }
            )
            output_path.unlink()
    verify_immutable_inputs(immutable_inputs, "after compilation")
    if compiler_failure_count:
        omitted = (
            "\n\n"
            f"{omitted_compiler_failure_count} additional compiler failure(s) "
            "omitted after the aggregate diagnostic byte limit"
            if omitted_compiler_failure_count
            else ""
        )
        failure_message = (
            f"fxc.exe failed for {compiler_failure_count} case(s):\n\n"
            + "\n\n".join(compiler_failures)
            + omitted
        )
        if (
            len(failure_message.encode("utf-8"))
            > MAX_AGGREGATE_COMPILER_DIAGNOSTICS_BYTES
        ):
            raise ValidationFailure(
                "internal compiler failure report exceeded its aggregate byte limit"
            )
        raise ValidationFailure(failure_message)
    return results


def validate_repository(
    repository_root: Path, fxc_path: Path, timeout_seconds: int = 60
) -> dict[str, object]:
    """Discover and compile the repository's complete explicit SM4 closure."""

    compiler_path, compiler_payload = _validate_compiler(fxc_path)
    cases = discover_cases(repository_root)
    immutable_inputs = snapshot_immutable_inputs(
        repository_root, compiler_path, compiler_payload
    )
    snapshot_by_relative = {
        item.relative: item for item in immutable_inputs.repository_files
    }
    for case in cases:
        source_snapshot = snapshot_by_relative.get(case.source_relative)
        script_changed = False
        if case.evidence_kind == "declared-resource-program-source-compile":
            script_snapshot = snapshot_by_relative.get(case.script_relative)
            script_changed = (
                script_snapshot is None
                or script_snapshot.sha256 != case.script_sha256
            )
        if (
            script_changed
            or source_snapshot is None
            or source_snapshot.sha256 != case.source_sha256
        ):
            raise ValidationFailure(
                f"shader inputs changed during discovery: {case.case_id}"
            )
    compiled_cases = compile_cases(
        cases, compiler_path, immutable_inputs, timeout_seconds
    )
    verify_immutable_inputs(immutable_inputs, "before evidence assembly")
    required_counts = {key: 0 for key in EXPECTED_REQUIRED_CASES}
    for case in cases:
        group = _required_group(case.script_relative)
        if group is not None:
            required_counts[group] += 1
    family_counts = {
        family: sum(case.family == family for case in cases)
        for family in EXPECTED_FAMILY_CASE_COUNTS
    }
    excluded_rtshader = []
    for path in _excluded_rtshader_sources(
        repository_root.absolute() / "resources", repository_root.absolute()
    ):
        relative = path.relative_to(repository_root.absolute()).as_posix()
        snapshot = snapshot_by_relative.get(relative)
        if snapshot is None:
            raise ValidationFailure(
                f"excluded RTShader HLSL input escaped immutable inventory: {relative}"
            )
        excluded_rtshader.append(
            {
                "path": relative,
                "reason": "combined-sampler ABI requires generator-coupled conversion",
                "sha256": snapshot.sha256,
            }
        )
    return {
        "case_count": len(compiled_cases),
        "cases": compiled_cases,
        "claims": {
            "hlsl_source_compile_proven": True,
            "rtshader_compatibility_hlsl_compile_proven": False,
            "live_ogre14_rtshader_package_compile_proven": False,
            "ogre_resource_load_proven": False,
            "rendered_frame_proven": False,
            "runtime_playability_proven": False,
        },
        "compiler": {
            "name": compiler_path.name,
            "sha256": _sha256_bytes(compiler_payload),
            "size": len(compiler_payload),
        },
        "excluded_sources": {
            "rtshader_combined_sampler_compatibility": excluded_rtshader,
        },
        "family_case_counts": family_counts,
        "input_integrity": {
            "compiler_reverified": True,
            "compiled_hlsl_source_count": len(
                {case.source_relative for case in cases}
            ),
            "declaration_script_count": sum(
                item.path.suffix.lower() in SCRIPT_SUFFIXES
                for item in immutable_inputs.repository_files
            ),
            "hlsl_source_count": sum(
                item.path.suffix.lower() == ".hlsl"
                for item in immutable_inputs.repository_files
            ),
            "repository_input_manifest_sha256": immutable_inputs.manifest_sha256,
            "reverified_before_and_after_each_compile": True,
        },
        "qualification_scope": (
            "declared-and-standalone-strict-sm4-hlsl-source-compile-only"
        ),
        "required_case_counts": required_counts,
        "schema": EVIDENCE_SCHEMA,
    }


def _evidence_bytes(evidence: dict[str, object]) -> bytes:
    return (json.dumps(evidence, indent=2, sort_keys=True) + "\n").encode("utf-8")


def write_evidence(output_path: Path, evidence: dict[str, object]) -> None:
    """Atomically publish a successful receipt at a fresh, caller-owned path."""

    output_path = output_path.expanduser().absolute()
    if output_path.is_symlink():
        raise ValidationFailure("evidence output must not be a symlink")
    if not output_path.parent.is_dir():
        raise ValidationFailure("evidence output parent directory is missing")
    if output_path.exists():
        raise ValidationFailure("evidence output already exists")
    with tempfile.NamedTemporaryFile(
        mode="wb", prefix=f".{output_path.name}.", dir=output_path.parent, delete=False
    ) as handle:
        temporary_path = Path(handle.name)
        try:
            handle.write(_evidence_bytes(evidence))
            handle.flush()
            os.fsync(handle.fileno())
        except BaseException:
            temporary_path.unlink(missing_ok=True)
            raise
    try:
        # A same-directory hard link publishes the fully written receipt in one
        # filesystem operation and, unlike replace/rename, cannot clobber a path
        # another process created after the preflight check.
        os.link(temporary_path, output_path, follow_symlinks=False)
    except FileExistsError as exc:
        raise ValidationFailure("evidence output already exists") from exc
    except OSError as exc:
        raise ValidationFailure(
            "could not atomically publish the fresh evidence output"
        ) from exc
    finally:
        temporary_path.unlink(missing_ok=True)


def prepare_evidence_output(output_path: Path) -> None:
    """Fail before compilation unless the caller supplied a fresh output path."""

    output_path = output_path.expanduser().absolute()
    if output_path.is_symlink():
        raise ValidationFailure("evidence output must not be a symlink")
    if not output_path.parent.is_dir():
        raise ValidationFailure("evidence output parent directory is missing")
    if output_path.exists():
        raise ValidationFailure("evidence output already exists")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repository-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Rigs of Rods checkout containing resources/ (default: script checkout)",
    )
    parser.add_argument(
        "--fxc",
        type=Path,
        required=True,
        help="explicit path to the Windows SDK fxc.exe compiler",
    )
    parser.add_argument(
        "--evidence-out", type=Path, required=True, help="JSON receipt output path"
    )
    parser.add_argument(
        "--timeout-seconds",
        type=int,
        default=60,
        help="per-case compiler timeout (1-600 seconds; default: 60)",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        prepare_evidence_output(args.evidence_out)
        evidence = validate_repository(
            args.repository_root, args.fxc, args.timeout_seconds
        )
        write_evidence(args.evidence_out, evidence)
    except ValidationFailure as exc:
        print(f"HLSL source validation failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps({"case_count": evidence["case_count"], "status": "passed"}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
