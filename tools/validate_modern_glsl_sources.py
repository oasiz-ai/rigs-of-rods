#!/usr/bin/env python3
"""Compile the modern GLSL source closure with an explicit glslang binary.

This validator intentionally proves only preprocessing, parsing, single-stage
semantic analysis, and entry-point linking. It does not load OGRE resources,
exercise a graphics driver, render a frame, or prove playability.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
from typing import Any, Iterable, Sequence


SCHEMA = "ror.modern-glsl-source-compile@1"
EVIDENCE_SCOPE = "glsl-source-preprocess-parse-semantic-link-only"
DOES_NOT_PROVE = [
    "ogre-resource-script-loading",
    "graphics-driver-compilation",
    "ogre-next-rendering",
    "rendered-frame-correctness",
    "runtime-readiness",
    "playability",
]

EXPECTED_DECLARED_CASE_COUNTS = {"caelum": 26, "managed-pssm": 5}
EXPECTED_RTSHADER_LIBRARIES = {
    "FFPLib_Common.glsl",
    "FFPLib_Fog.glsl",
    "FFPLib_Lighting.glsl",
    "FFPLib_Texturing.glsl",
    "FFPLib_Transform.glsl",
    "SGXLib_IntegratedPSSM.glsl",
    "SGXLib_NormalMapLighting.glsl",
    "SGXLib_PerPixelLighting.glsl",
    "SampleLib_ReflectionMap.glsl",
}

SCRIPT_SUFFIXES = {".material", ".program"}
LEGACY_SUFFIXES = {".asm", ".cg"}
STAGES = {"vertex": "vert", "fragment": "frag"}
PROGRAM_HEADER = re.compile(
    r"(?m)^[ \t]*(?P<kind>[A-Za-z_]+)_program[ \t]+"
    r"(?P<name>[^\s{}]+)[ \t]+(?P<language>[A-Za-z0-9_]+)"
    r"[ \t]*(?:\{)?[ \t]*$"
)
SOURCE_DIRECTIVE = re.compile(r"(?m)^[ \t]*source[ \t]+([^\s{}]+)[ \t]*$")
SYNTAX_DIRECTIVE = re.compile(r"(?m)^[ \t]*syntax[ \t]+([^\s{}]+)[ \t]*$")
DEFINE_DIRECTIVE = re.compile(
    r"(?m)^[ \t]*preprocessor_defines[ \t]+([^\r\n{}]+?)[ \t]*$"
)
ENTRY_POINT_DIRECTIVE = re.compile(
    r"(?m)^[ \t]*entry_point[ \t]+([^\s{}]+)[ \t]*$"
)
LEGACY_DECLARATION = re.compile(
    r"(?im)^[ \t]*(?:[A-Za-z_]+)_program[ \t]+[^\s{}]+[ \t]+(?:asm|cg)\b"
)
LEGACY_REFERENCE = re.compile(
    r"(?im)^[ \t]*source[ \t]+[^\s{}]+\.(?:asm|cg)[ \t]*$"
)
MACRO = re.compile(r"^[A-Za-z_]\w*(?:=[A-Za-z0-9_.+\-]+)?$")
FUNCTION = re.compile(
    r"\bvoid[ \t\r\n]+(?P<name>[A-Za-z_]\w*)[ \t\r\n]*"
    r"\((?P<parameters>.*?)\)[ \t\r\n]*\{",
    re.DOTALL,
)
PARAMETER = re.compile(
    r"^(?:(?P<direction>in|out|inout)\s+)?"
    r"(?:(?:const|highp|mediump|lowp)\s+)*"
    r"(?P<type>[A-Za-z_]\w*)\s+(?P<name>[A-Za-z_]\w*)$"
)


class ValidationFailure(Exception):
    def __init__(self, code: str, message: str, **context: Any) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.context = context


@dataclass(frozen=True)
class Snapshot:
    path: Path
    relative_path: str
    data: bytes
    sha256: str


@dataclass(frozen=True)
class DeclaredCase:
    family: str
    program_name: str
    stage: str
    declaration: Snapshot
    source: Snapshot
    defines: tuple[str, ...]

    @property
    def case_id(self) -> str:
        return f"declared:{self.family}:{self.program_name}"


@dataclass(frozen=True)
class RTShaderCase:
    stage: str
    source: Snapshot
    wrapper: bytes
    probe_function: str

    @property
    def case_id(self) -> str:
        return f"rtshader-wrapper:{self.source.path.stem}:{self.stage}"


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json_print(value: dict[str, Any]) -> None:
    print(json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True))


def _relative(path: Path, repository_root: Path) -> str:
    try:
        return path.relative_to(repository_root).as_posix()
    except ValueError as exc:
        raise ValidationFailure(
            "path_escape",
            "A shader input resolved outside the repository root",
            path=str(path),
        ) from exc


def _snapshot(path: Path, repository_root: Path) -> Snapshot:
    if path.is_symlink():
        raise ValidationFailure(
            "symlink_input",
            "Shader evidence inputs must not be symbolic links",
            path=_relative(path, repository_root),
        )
    try:
        mode = path.stat().st_mode
    except FileNotFoundError as exc:
        raise ValidationFailure(
            "missing_input",
            "A shader evidence input is missing",
            path=_relative(path, repository_root),
        ) from exc
    if not stat.S_ISREG(mode):
        raise ValidationFailure(
            "non_regular_input",
            "Shader evidence inputs must be regular files",
            path=_relative(path, repository_root),
        )
    data = path.read_bytes()
    if not data.strip():
        raise ValidationFailure(
            "empty_input",
            "Shader evidence inputs must not be empty",
            path=_relative(path, repository_root),
        )
    return Snapshot(path, _relative(path, repository_root), data, _sha256(data))


def _strip_comments(source: str) -> str:
    def preserve_lines(match: re.Match[str]) -> str:
        return "\n" * match.group(0).count("\n")

    without_blocks = re.sub(r"/\*.*?\*/", preserve_lines, source, flags=re.DOTALL)
    return re.sub(r"//[^\r\n]*", "", without_blocks)


def _matching_brace(source: str, opening: int, path: str) -> int:
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    raise ValidationFailure(
        "malformed_program_script",
        "A shader program declaration has an unclosed body",
        path=path,
    )


def _single_directive(
    pattern: re.Pattern[str], body: str, directive: str, path: str, program: str
) -> str | None:
    values = pattern.findall(body)
    if len(values) > 1:
        raise ValidationFailure(
            "ambiguous_program_directive",
            f"A GL3Plus program has multiple {directive} directives",
            path=path,
            program=program,
        )
    return values[0].strip() if values else None


def _validate_tree_inputs(target_root: Path, repository_root: Path) -> None:
    if target_root.is_symlink():
        raise ValidationFailure(
            "symlink_input",
            "Shader evidence input roots must not be symbolic links",
            path=_relative(target_root, repository_root),
        )
    if not target_root.is_dir():
        raise ValidationFailure(
            "missing_input_root",
            "A required shader input directory is missing",
            path=_relative(target_root, repository_root),
        )
    for path in sorted(target_root.rglob("*")):
        if path.is_symlink():
            raise ValidationFailure(
                "symlink_input",
                "Shader evidence input trees must not contain symbolic links",
                path=_relative(path, repository_root),
            )
        if path.is_file() and path.suffix.lower() in LEGACY_SUFFIXES:
            raise ValidationFailure(
                "legacy_source",
                "The modern GLSL evidence scope contains a legacy shader source",
                path=_relative(path, repository_root),
            )


def _protected_tree_manifest(
    target_root: Path, repository_root: Path
) -> tuple[str, ...]:
    _validate_tree_inputs(target_root, repository_root)
    entries: list[str] = []
    for path in sorted(target_root.rglob("*")):
        relative_path = _relative(path, repository_root)
        if path.is_dir():
            entries.append(f"directory:{relative_path}")
        elif path.is_file():
            entries.append(f"file:{relative_path}")
        else:
            raise ValidationFailure(
                "non_regular_input",
                "Protected shader trees may contain only directories and regular files",
                path=relative_path,
            )
    return tuple(entries)


def _parse_declared_cases(
    target_root: Path,
    repository_root: Path,
    family: str,
) -> tuple[list[DeclaredCase], list[Snapshot]]:
    _validate_tree_inputs(target_root, repository_root)
    scripts = sorted(
        path
        for path in target_root.rglob("*")
        if path.is_file() and path.suffix.lower() in SCRIPT_SUFFIXES
    )
    if not scripts:
        raise ValidationFailure(
            "missing_cases",
            "No material/program scripts were found for a required GLSL family",
            family=family,
        )

    cases: list[DeclaredCase] = []
    declarations: list[Snapshot] = []
    seen_programs: set[str] = set()
    for script_path in scripts:
        declaration = _snapshot(script_path, repository_root)
        declarations.append(declaration)
        try:
            script_text = declaration.data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValidationFailure(
                "invalid_utf8",
                "Shader program scripts must be UTF-8",
                path=declaration.relative_path,
            ) from exc
        clean = _strip_comments(script_text)
        if LEGACY_DECLARATION.search(clean) or LEGACY_REFERENCE.search(clean):
            raise ValidationFailure(
                "legacy_source",
                "A material/program script retains a legacy Cg/ASM route",
                path=declaration.relative_path,
            )

        for header in PROGRAM_HEADER.finditer(clean):
            program_name = header.group("name")
            if not program_name.endswith("/GL3Plus"):
                continue
            language = header.group("language").lower()
            if language != "glsl":
                raise ValidationFailure(
                    "invalid_gl3plus_language",
                    "A GL3Plus program must use the GLSL language",
                    path=declaration.relative_path,
                    program=program_name,
                    language=language,
                )
            kind = header.group("kind").lower()
            if kind not in STAGES:
                raise ValidationFailure(
                    "unsupported_gl3plus_stage",
                    "The validator found an unsupported GL3Plus program stage",
                    path=declaration.relative_path,
                    program=program_name,
                    stage=kind,
                )
            if program_name in seen_programs:
                raise ValidationFailure(
                    "duplicate_program",
                    "A GL3Plus program name is declared more than once",
                    program=program_name,
                )
            seen_programs.add(program_name)

            opening = clean.find("{", header.start(), header.end())
            if opening < 0:
                cursor = header.end()
                while cursor < len(clean) and clean[cursor].isspace():
                    cursor += 1
                if cursor >= len(clean) or clean[cursor] != "{":
                    raise ValidationFailure(
                        "malformed_program_script",
                        "A GL3Plus program declaration has no body",
                        path=declaration.relative_path,
                        program=program_name,
                    )
                opening = cursor
            closing = _matching_brace(clean, opening, declaration.relative_path)
            body = clean[opening + 1 : closing]

            source_name = _single_directive(
                SOURCE_DIRECTIVE,
                body,
                "source",
                declaration.relative_path,
                program_name,
            )
            if source_name is None:
                raise ValidationFailure(
                    "missing_source_directive",
                    "A GL3Plus program has no source directive",
                    path=declaration.relative_path,
                    program=program_name,
                )
            if Path(source_name).suffix.lower() in LEGACY_SUFFIXES:
                raise ValidationFailure(
                    "legacy_source",
                    "A GL3Plus program references a legacy shader source",
                    path=declaration.relative_path,
                    program=program_name,
                    source=source_name,
                )
            if Path(source_name).suffix.lower() != ".glsl":
                raise ValidationFailure(
                    "invalid_glsl_source",
                    "A GL3Plus GLSL program must reference a .glsl source",
                    path=declaration.relative_path,
                    program=program_name,
                    source=source_name,
                )

            syntax = _single_directive(
                SYNTAX_DIRECTIVE,
                body,
                "syntax",
                declaration.relative_path,
                program_name,
            )
            if syntax != "glsl330":
                raise ValidationFailure(
                    "invalid_glsl_syntax",
                    "A converted GL3Plus program must declare syntax glsl330",
                    path=declaration.relative_path,
                    program=program_name,
                    syntax=syntax,
                )
            entry_point = _single_directive(
                ENTRY_POINT_DIRECTIVE,
                body,
                "entry_point",
                declaration.relative_path,
                program_name,
            )
            if entry_point not in (None, "main"):
                raise ValidationFailure(
                    "unsupported_entry_point",
                    "GLSL source validation currently requires the main entry point",
                    path=declaration.relative_path,
                    program=program_name,
                    entryPoint=entry_point,
                )

            defines_text = _single_directive(
                DEFINE_DIRECTIVE,
                body,
                "preprocessor_defines",
                declaration.relative_path,
                program_name,
            )
            defines: tuple[str, ...] = ()
            if defines_text is not None:
                parsed = tuple(
                    item.strip() for item in defines_text.split(",") if item.strip()
                )
                if not parsed or any(not MACRO.fullmatch(item) for item in parsed):
                    raise ValidationFailure(
                        "invalid_preprocessor_defines",
                        "A GL3Plus program has invalid preprocessor definitions",
                        path=declaration.relative_path,
                        program=program_name,
                        definitions=defines_text,
                    )
                if len(set(parsed)) != len(parsed):
                    raise ValidationFailure(
                        "duplicate_preprocessor_define",
                        "A GL3Plus program repeats a preprocessor definition",
                        path=declaration.relative_path,
                        program=program_name,
                    )
                defines = parsed

            unresolved_source = script_path.parent / source_name
            try:
                resolved_source = unresolved_source.resolve(strict=True)
            except FileNotFoundError as exc:
                raise ValidationFailure(
                    "missing_input",
                    "A GL3Plus program source is missing",
                    path=declaration.relative_path,
                    program=program_name,
                    source=source_name,
                ) from exc
            if unresolved_source.is_symlink():
                raise ValidationFailure(
                    "symlink_input",
                    "GL3Plus source references must not be symbolic links",
                    path=_relative(unresolved_source, repository_root),
                )
            try:
                resolved_source.relative_to(target_root.resolve())
            except ValueError as exc:
                raise ValidationFailure(
                    "path_escape",
                    "A GL3Plus source resolves outside its shader family",
                    path=declaration.relative_path,
                    program=program_name,
                    source=source_name,
                ) from exc
            source = _snapshot(resolved_source, repository_root)
            cases.append(
                DeclaredCase(
                    family=family,
                    program_name=program_name,
                    stage=STAGES[kind],
                    declaration=declaration,
                    source=source,
                    defines=defines,
                )
            )

    expected_count = EXPECTED_DECLARED_CASE_COUNTS[family]
    if len(cases) != expected_count:
        raise ValidationFailure(
            "missing_cases",
            "The parsed GL3Plus declaration count does not match the fail-closed contract",
            family=family,
            expectedCaseCount=expected_count,
            actualCaseCount=len(cases),
        )

    referenced_sources = {case.source.path for case in cases}
    actual_sources = set(target_root.rglob("*.glsl"))
    if referenced_sources != actual_sources:
        raise ValidationFailure(
            "source_closure_mismatch",
            "Every converted GLSL source must be referenced by a parsed GL3Plus program",
            family=family,
            missingReferences=sorted(
                _relative(path, repository_root)
                for path in actual_sources - referenced_sources
            ),
            missingSources=sorted(
                _relative(path, repository_root)
                for path in referenced_sources - actual_sources
            ),
        )
    return cases, declarations


def _initial_value(glsl_type: str) -> str:
    if glsl_type == "float":
        return "0.5"
    if glsl_type in {"int", "uint"}:
        return "1"
    if glsl_type == "bool":
        return "false"
    if re.fullmatch(r"[biu]?vec[234]", glsl_type):
        scalar = "0.5" if glsl_type.startswith("vec") else "1"
        return f"{glsl_type}({scalar})"
    if re.fullmatch(r"mat[234](?:x[234])?", glsl_type):
        return f"{glsl_type}(1.0)"
    raise ValidationFailure(
        "unsupported_rtshader_probe_type",
        "An RTShader probe function uses an unsupported GLSL parameter type",
        parameterType=glsl_type,
    )


def _first_function_probe(source: str, source_path: str) -> tuple[str, str, str]:
    clean = _strip_comments(source)
    match = FUNCTION.search(clean)
    if match is None:
        raise ValidationFailure(
            "missing_cases",
            "An RTShader dependency library has no callable function",
            path=source_path,
        )
    function_name = match.group("name")
    raw_parameters = match.group("parameters")
    pieces = [piece.strip() for piece in raw_parameters.split(",") if piece.strip()]
    declarations: list[str] = []
    uniforms: list[str] = []
    arguments: list[str] = []
    for index, piece in enumerate(pieces):
        normalized = " ".join(piece.split())
        parameter = PARAMETER.fullmatch(normalized)
        if parameter is None:
            raise ValidationFailure(
                "unsupported_rtshader_probe_signature",
                "The first RTShader function has an unsupported parameter declaration",
                path=source_path,
                function=function_name,
                parameter=normalized,
            )
        glsl_type = parameter.group("type")
        argument_name = f"rorProbeArg{index}"
        if glsl_type.startswith("sampler"):
            if parameter.group("direction") in {"out", "inout"}:
                raise ValidationFailure(
                    "unsupported_rtshader_probe_signature",
                    "Sampler outputs cannot be synthesized for an RTShader probe",
                    path=source_path,
                    function=function_name,
                )
            uniforms.append(f"uniform {glsl_type} {argument_name};")
        else:
            declarations.append(
                f"    {glsl_type} {argument_name} = {_initial_value(glsl_type)};"
            )
        arguments.append(argument_name)
    call = f"    {function_name}({', '.join(arguments)});"
    signature = " ".join(match.group(0).rsplit("{", 1)[0].split())
    return function_name, "\n".join(uniforms), "\n".join([*declarations, call]), signature


def _rtshader_cases(
    rtshader_root: Path, repository_root: Path
) -> tuple[list[RTShaderCase], list[Snapshot]]:
    _validate_tree_inputs(rtshader_root, repository_root)
    source_paths = sorted(rtshader_root.glob("*.glsl"))
    actual_names = {path.name for path in source_paths}
    if actual_names != EXPECTED_RTSHADER_LIBRARIES:
        raise ValidationFailure(
            "missing_cases",
            "The RTShader GLSL dependency-library closure is not the required nine files",
            expected=sorted(EXPECTED_RTSHADER_LIBRARIES),
            actual=sorted(actual_names),
        )

    snapshots = [_snapshot(path, repository_root) for path in source_paths]
    cases: list[RTShaderCase] = []
    for source in snapshots:
        try:
            text = source.data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValidationFailure(
                "invalid_utf8",
                "RTShader GLSL dependency libraries must be UTF-8",
                path=source.relative_path,
            ) from exc
        if re.search(r"(?m)^[ \t]*#version\b", text):
            raise ValidationFailure(
                "rtshader_embedded_version",
                "RTShader dependencies must inherit the generated program's GLSL version",
                path=source.relative_path,
            )
        function_name, uniforms, probe_body, _signature = _first_function_probe(
            text, source.relative_path
        )
        for stage in ("vert", "frag"):
            stage_declaration = ""
            stage_result = ""
            if stage == "vert":
                stage_result = "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);"
            else:
                stage_declaration = "layout(location = 0) out vec4 rorProbeColour;\n"
                stage_result = "    rorProbeColour = vec4(0.0, 0.0, 0.0, 1.0);"
            wrapper_text = (
                "#version 330 core\n"
                "// Exact RTShader dependency bytes follow.\n"
                f"{text.rstrip()}\n"
                "// Synthetic entry point calls one exported symbol; all dependency\n"
                "// function bodies remain subject to glslang semantic analysis.\n"
                f"{uniforms}\n"
                f"{stage_declaration}"
                "void main()\n"
                "{\n"
                f"{probe_body}\n"
                f"{stage_result}\n"
                "}\n"
            )
            cases.append(
                RTShaderCase(
                    stage=stage,
                    source=source,
                    wrapper=wrapper_text.encode("utf-8"),
                    probe_function=function_name,
                )
            )
    if len(cases) != 18:
        raise ValidationFailure(
            "missing_cases",
            "Every RTShader GLSL dependency must have vertex and fragment probes",
            expectedCaseCount=18,
            actualCaseCount=len(cases),
        )
    return cases, snapshots


def _first_symlink_component(path: Path) -> Path | None:
    absolute = path if path.is_absolute() else Path.cwd() / path
    for component in (absolute, *absolute.parents):
        if component.is_symlink():
            return component
    return None


def _validate_compiler(path_text: str) -> tuple[Path, str]:
    supplied = Path(path_text).expanduser()
    symlink_component = _first_symlink_component(supplied)
    if symlink_component is not None:
        raise ValidationFailure(
            "symlink_compiler",
            "The explicit glslangValidator path must not contain symbolic links",
            path=str(supplied),
            symlinkComponent=str(symlink_component),
        )
    try:
        path = supplied.resolve(strict=True)
    except FileNotFoundError as exc:
        raise ValidationFailure(
            "missing_compiler",
            "The explicit glslangValidator path does not exist",
            path=str(supplied),
        ) from exc
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValidationFailure(
            "invalid_compiler",
            "The explicit glslangValidator path is not a regular file",
            path=str(path),
        )
    if not os.access(path, os.X_OK):
        raise ValidationFailure(
            "compiler_not_executable",
            "The explicit glslangValidator path is not executable",
            path=str(path),
        )
    data = path.read_bytes()
    return path, _sha256(data)


def _summary(data: bytes) -> list[str]:
    text = data.decode("utf-8", errors="replace").replace("\r\n", "\n")
    return [line[:240] for line in text.strip().splitlines()[:8]]


def _run_compiler(
    compiler: Path,
    logical_argv: Sequence[str],
    actual_argv: Sequence[str],
    source: bytes,
    timeout_seconds: float,
) -> tuple[dict[str, Any], bool]:
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    environment["LANG"] = "C"
    try:
        completed = subprocess.run(
            [str(compiler), *actual_argv],
            input=source,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=timeout_seconds,
            env=environment,
        )
    except subprocess.TimeoutExpired as exc:
        raise ValidationFailure(
            "compiler_timeout",
            "glslangValidator exceeded the per-command timeout",
            command=["glslangValidator", *logical_argv],
            timeoutSeconds=timeout_seconds,
        ) from exc
    except OSError as exc:
        raise ValidationFailure(
            "compiler_execution_error",
            "glslangValidator could not be executed",
            command=["glslangValidator", *logical_argv],
            osError=str(exc),
        ) from exc

    result = {
        "command": ["glslangValidator", *logical_argv],
        "exitCode": completed.returncode,
        "stderrSha256": _sha256(completed.stderr),
        "stderrSummary": _summary(completed.stderr),
        "stdoutSha256": _sha256(completed.stdout),
        "stdoutSummary": _summary(completed.stdout),
    }
    return result, completed.returncode == 0


def _file_records(snapshots: Iterable[Snapshot]) -> list[dict[str, str]]:
    unique = {snapshot.relative_path: snapshot for snapshot in snapshots}
    return [
        {"path": path, "sha256": unique[path].sha256}
        for path in sorted(unique)
    ]


def _assert_snapshots_unchanged(snapshots: Iterable[Snapshot]) -> None:
    for snapshot in snapshots:
        if snapshot.path.is_symlink() or not snapshot.path.is_file():
            raise ValidationFailure(
                "input_changed",
                "A shader evidence input changed during validation",
                path=snapshot.relative_path,
            )
        if _sha256(snapshot.path.read_bytes()) != snapshot.sha256:
            raise ValidationFailure(
                "input_changed",
                "A shader evidence input changed during validation",
                path=snapshot.relative_path,
            )


def _late_failure(
    failure: ValidationFailure,
    executed_cases: Sequence[dict[str, Any]],
    planned_case_count: int,
) -> ValidationFailure:
    context = dict(failure.context)
    context["executedCases"] = list(executed_cases)
    context["plannedCaseCount"] = planned_case_count
    return ValidationFailure(failure.code, failure.message, **context)


def validate(
    repository_root: Path,
    compiler_text: str,
    timeout_seconds: float,
) -> dict[str, Any]:
    compiler, compiler_sha = _validate_compiler(compiler_text)
    version_result, version_ok = _run_compiler(
        compiler,
        ["--version"],
        ["--version"],
        b"",
        timeout_seconds,
    )
    if not version_ok:
        raise ValidationFailure(
            "compiler_version_error",
            "glslangValidator --version failed",
            compilerSha256=compiler_sha,
            result=version_result,
        )
    version_lines = [
        *version_result["stdoutSummary"],
        *version_result["stderrSummary"],
    ]
    if not version_lines:
        raise ValidationFailure(
            "compiler_version_error",
            "glslangValidator returned no version text",
            compilerSha256=compiler_sha,
        )

    caelum_root = repository_root / "resources/caelum"
    pssm_root = repository_root / "resources/managed_materials/shadows/pssm/on"
    rtshader_root = repository_root / "resources/rtshader"
    protected_roots = {
        "caelum": caelum_root,
        "managed-pssm": pssm_root,
        "rtshader": rtshader_root,
    }
    protected_tree_manifests = {
        family: _protected_tree_manifest(target, repository_root)
        for family, target in protected_roots.items()
    }
    declared_cases: list[DeclaredCase] = []
    declaration_snapshots: list[Snapshot] = []
    for target, family in ((caelum_root, "caelum"), (pssm_root, "managed-pssm")):
        family_cases, family_declarations = _parse_declared_cases(
            target, repository_root, family
        )
        declared_cases.extend(family_cases)
        declaration_snapshots.extend(family_declarations)
    rtshader_cases, rtshader_snapshots = _rtshader_cases(
        rtshader_root, repository_root
    )

    all_case_ids = [case.case_id for case in [*declared_cases, *rtshader_cases]]
    if len(all_case_ids) != len(set(all_case_ids)):
        raise ValidationFailure(
            "duplicate_case_id",
            "Shader compiler evidence case IDs must be unique",
        )
    planned_case_count = len(all_case_ids)
    if planned_case_count != 49:
        raise ValidationFailure(
            "missing_cases",
            "The complete modern GLSL compile matrix must contain 49 cases",
            expectedCaseCount=49,
            actualCaseCount=planned_case_count,
        )

    results: list[dict[str, Any]] = []
    for case in declared_cases:
        actual_argv = ["-l", "--stdin", "-S", case.stage]
        logical_argv = ["-l", "--stdin", "-S", case.stage]
        for definition in case.defines:
            actual_argv.append(f"-D{definition}")
            logical_argv.append(f"-D{definition}")
        compiler_result, passed = _run_compiler(
            compiler,
            logical_argv,
            actual_argv,
            case.source.data,
            timeout_seconds,
        )
        case_result: dict[str, Any] = {
            "caseId": case.case_id,
            "declarationPath": case.declaration.relative_path,
            "declarationSha256": case.declaration.sha256,
            "defines": list(case.defines),
            "evidenceKind": "declared-gl3plus-program-source-compile",
            "family": case.family,
            "programName": case.program_name,
            "sourcePath": case.source.relative_path,
            "sourceSha256": case.source.sha256,
            "stage": case.stage,
            "stdinSha256": case.source.sha256,
            **compiler_result,
        }
        case_result["status"] = "passed" if passed else "failed"
        results.append(case_result)
        if not passed:
            raise ValidationFailure(
                "compile_error",
                "A declared GL3Plus shader variant failed source compilation",
                caseId=case.case_id,
                caseResult=case_result,
                executedCases=results,
                plannedCaseCount=planned_case_count,
            )

    for case in rtshader_cases:
        argv = ["-l", "--stdin", "-S", case.stage]
        compiler_result, passed = _run_compiler(
            compiler,
            argv,
            argv,
            case.wrapper,
            timeout_seconds,
        )
        wrapper_sha = _sha256(case.wrapper)
        case_result = {
            "caseId": case.case_id,
            "defines": [],
            "evidenceKind": "rtshader-dependency-synthetic-wrapper-source-compile",
            "family": "rtshader",
            "probeFunction": case.probe_function,
            "sourcePath": case.source.relative_path,
            "sourceSha256": case.source.sha256,
            "stage": case.stage,
            "stdinSha256": wrapper_sha,
            "wrapperSha256": wrapper_sha,
            **compiler_result,
        }
        case_result["status"] = "passed" if passed else "failed"
        results.append(case_result)
        if not passed:
            raise ValidationFailure(
                "compile_error",
                "An RTShader dependency wrapper failed source compilation",
                caseId=case.case_id,
                caseResult=case_result,
                executedCases=results,
                plannedCaseCount=planned_case_count,
            )

    input_snapshots = [
        *declaration_snapshots,
        *(case.source for case in declared_cases),
        *rtshader_snapshots,
    ]
    try:
        _assert_snapshots_unchanged(input_snapshots)
        for family, target in protected_roots.items():
            current_manifest = _protected_tree_manifest(target, repository_root)
            initial_manifest = protected_tree_manifests[family]
            if current_manifest != initial_manifest:
                initial_entries = set(initial_manifest)
                current_entries = set(current_manifest)
                raise ValidationFailure(
                    "input_tree_changed",
                    "A protected shader input tree changed during validation",
                    family=family,
                    root=_relative(target, repository_root),
                    addedEntries=sorted(current_entries - initial_entries),
                    removedEntries=sorted(initial_entries - current_entries),
                )
    except ValidationFailure as failure:
        raise _late_failure(failure, results, planned_case_count) from failure
    except OSError as exc:
        failure = ValidationFailure(
            "input_changed",
            "A protected shader input could not be revalidated",
            osError=str(exc),
        )
        raise _late_failure(failure, results, planned_case_count) from exc

    try:
        compiler_changed = (
            _first_symlink_component(compiler) is not None
            or _sha256(compiler.read_bytes()) != compiler_sha
        )
    except OSError:
        compiler_changed = True
    if compiler_changed:
        raise ValidationFailure(
            "compiler_changed",
            "The glslangValidator executable changed during validation",
            path=str(compiler),
            executedCases=results,
            plannedCaseCount=planned_case_count,
        )

    family_counts = {
        "caelum": sum(case.family == "caelum" for case in declared_cases),
        "managed-pssm": sum(
            case.family == "managed-pssm" for case in declared_cases
        ),
        "rtshader": len(rtshader_cases),
    }
    return {
        "caseCount": planned_case_count,
        "cases": results,
        "compiler": {
            "invocation": "explicit-path",
            "sha256": compiler_sha,
            "version": version_lines,
            "versionCommandResult": version_result,
        },
        "doesNotProve": DOES_NOT_PROVE,
        "evidenceScope": EVIDENCE_SCOPE,
        "executedCaseCount": len(results),
        "familyCaseCounts": family_counts,
        "inputs": {
            "declarationFiles": _file_records(declaration_snapshots),
            "protectedTrees": [
                {
                    "entryCount": len(protected_tree_manifests[family]),
                    "manifestSha256": _sha256(
                        ("\n".join(protected_tree_manifests[family]) + "\n").encode(
                            "utf-8"
                        )
                    ),
                    "root": _relative(protected_roots[family], repository_root),
                }
                for family in sorted(protected_roots)
            ],
            "sourceFiles": _file_records(
                [*(case.source for case in declared_cases), *rtshader_snapshots]
            ),
        },
        "result": "passed",
        "schema": SCHEMA,
    }


def _failure_receipt(failure: ValidationFailure) -> dict[str, Any]:
    context = dict(failure.context)
    executed = context.pop("executedCases", [])
    planned = context.pop("plannedCaseCount", None)
    receipt: dict[str, Any] = {
        "doesNotProve": DOES_NOT_PROVE,
        "error": {
            "code": failure.code,
            "context": context,
            "message": failure.message,
        },
        "evidenceScope": EVIDENCE_SCOPE,
        "executedCaseCount": len(executed),
        "result": "failed",
        "schema": SCHEMA,
    }
    if executed:
        receipt["cases"] = executed
    if planned is not None:
        receipt["caseCount"] = planned
    return receipt


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--glslang-validator",
        required=True,
        help="Explicit path to a trusted glslangValidator executable",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Repository root (defaults to the validator's checkout)",
    )
    parser.add_argument(
        "--timeout-seconds",
        type=float,
        default=30.0,
        help="Per-compiler-invocation timeout",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if args.timeout_seconds <= 0:
            raise ValidationFailure(
                "invalid_timeout", "The compiler timeout must be positive"
            )
        if args.repo_root.is_symlink():
            raise ValidationFailure(
                "symlink_repository_root",
                "The repository root must not be a symbolic link",
                path=str(args.repo_root),
            )
        repository_root = args.repo_root.resolve(strict=True)
        if not repository_root.is_dir():
            raise ValidationFailure(
                "invalid_repository_root",
                "The repository root is not a directory",
                path=str(repository_root),
            )
        receipt = validate(
            repository_root,
            args.glslang_validator,
            args.timeout_seconds,
        )
    except ValidationFailure as failure:
        _json_print(_failure_receipt(failure))
        return 1
    except (OSError, UnicodeError, ValueError) as exc:
        failure = ValidationFailure(
            "unexpected_validation_error",
            "Modern GLSL validation could not complete",
            exceptionType=type(exc).__name__,
            detail=str(exc),
        )
        _json_print(_failure_receipt(failure))
        return 1
    _json_print(receipt)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
