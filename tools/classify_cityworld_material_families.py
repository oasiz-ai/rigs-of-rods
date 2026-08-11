#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Classify user-supplied CityWorld OGRE material scripts without copying assets.

The classifier reads bounded ``.material`` members directly from a ZIP, parses
their authored scope structure, and emits review evidence.  It does not extract
or rewrite scripts, infer PBR maps from filenames, or admit a runtime material.
Any PBR result remains an explicit, reviewed modernization choice.
"""

from __future__ import annotations

import argparse
from bisect import bisect_right
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
from typing import Iterable, NoReturn, Sequence
import zipfile


TOOLS_ROOT = str(Path(__file__).resolve().parent)
if TOOLS_ROOT not in sys.path:
    sys.path.insert(0, TOOLS_ROOT)

from audit_cityworld_visuals import (  # noqa: E402
    AuditFailure,
    MAX_TEXT_BYTES,
    archive_sha256,
    validate_archive_members,
)


SCHEMA_ID = "ror.cityworld.material-modernization-report.v1"
REPORT_FORMAT = "ror-cityworld-material-modernization-report-v1"
CLASSIFIER_ALGORITHM = "ror-cityworld-material-family-classifier-v1"
SCHEMA_RELATIVE_PATH = (
    "tools/schemas/cityworld-material-modernization-report-v1.schema.json"
)
SCHEMA_PATH = (
    Path(__file__).resolve().parent
    / "schemas/cityworld-material-modernization-report-v1.schema.json"
)
MAX_MATERIAL_DEFINITIONS = 65_536
MAX_TOKENS_PER_SCRIPT = 1_000_000
MAX_TOTAL_MATERIAL_SCRIPT_BYTES = 256 * 1024 * 1024
MAX_TOTAL_TOKENS = 4_000_000
MAX_STATEMENTS_PER_SCOPE = 65_536
MAX_NESTING_DEPTH = 256

FAMILY_SPHERICAL = "SPHERICAL_BASE_SPEC_CURRENT_ALPHA_ENVIRONMENT"
FAMILY_EMISSIVE = "CLEAN_TWO_PASS_ALPHA_REJECTED_EMISSIVE"
FAMILY_BUS_STOP = "TRANSPARENT_SPHERICAL_BUS_STOP"
FAMILY_PLANAR_SURFACE = "PLANAR_SURFACE_METAL"
FAMILY_CIELO = "CIELO_PLANAR_WINDOW_COMPOSITE"
FAMILY_CUBE_PLANAR = "SUSPICIOUS_CUBE_PLANAR_ENVIRONMENT"
FAMILY_COMBINED_ENV_EMISSIVE = "COMBINED_ENVIRONMENT_EMISSIVE"
FAMILY_COMBINED_PLANAR_EMISSIVE = "COMBINED_PLANAR_EMISSIVE"
FAMILY_ADDITIVE_FURNITURE = "ADDITIVE_SPECULAR_FURNITURE"
FAMILY_PLANAR_OTHER = "PLANAR_ENVIRONMENT_OTHER"
FAMILY_SIMPLE = "SIMPLE_SINGLE_PASS"
FAMILY_SUSPICIOUS_B = "SUSPICIOUS_B_LIKE_ORPHAN_TEXTURE_UNIT"
FAMILY_SUSPICIOUS = "SUSPICIOUS_STRUCTURE"
FAMILY_UNSUPPORTED = "UNSUPPORTED_STRUCTURE"

FIDELITY_LEGACY = "LEGACY_SEMANTIC_EQUIVALENT"
FIDELITY_PBR = "DECLARED_PBR_MODERNIZATION"

CLASSIFICATION_ELIGIBLE = "ELIGIBLE"
CLASSIFICATION_REVIEW_BLOCKED = "REVIEW_BLOCKED"
CLASSIFICATION_UNSUPPORTED = "UNSUPPORTED"

BLOCK_KEYWORDS = frozenset(
    {
        "compute_program",
        "compute_program_ref",
        "default_params",
        "fragment_program",
        "fragment_program_ref",
        "geometry_program",
        "geometry_program_ref",
        "material",
        "pass",
        "shadow_caster_fragment_program_ref",
        "shadow_caster_vertex_program_ref",
        "shadow_receiver_fragment_program_ref",
        "shadow_receiver_vertex_program_ref",
        "technique",
        "tessellation_domain_program",
        "tessellation_domain_program_ref",
        "tessellation_hull_program",
        "tessellation_hull_program_ref",
        "texture_source",
        "texture_unit",
        "vertex_program",
        "vertex_program_ref",
    }
)

PROGRAM_BLOCKS = frozenset(
    name for name in BLOCK_KEYWORDS if "program" in name
)
UNIT_TRANSFORM_DIRECTIVES = frozenset(
    {
        "anim_texture",
        "projective_texturing",
        "rotate",
        "rotate_anim",
        "scale",
        "scroll",
        "scroll_anim",
        "transform",
        "wave_xform",
    }
)
LEGACY_MATERIAL_DIRECTIVES = frozenset(
    {
        "lod_distances",
        "receive_shadows",
        "set_texture_alias",
        "transparency_casts_shadows",
    }
)
LEGACY_TECHNIQUE_DIRECTIVES = frozenset(
    {
        "gpu_device_rule",
        "gpu_vendor_rule",
        "lod_index",
        "scheme",
        "shadow_caster_material",
        "shadow_receiver_material",
    }
)
LEGACY_PASS_DIRECTIVES = frozenset(
    {
        "alpha_rejection",
        "alpha_to_coverage",
        "ambient",
        "colour_write",
        "colour_write_alpha",
        "colour_write_blue",
        "colour_write_green",
        "colour_write_red",
        "cull_hardware",
        "cull_software",
        "depth_bias",
        "depth_check",
        "depth_func",
        "depth_write",
        "diffuse",
        "emissive",
        "fog_override",
        "illumination_stage",
        "iteration",
        "light_clip_planes",
        "light_mask",
        "light_scissor",
        "lighting",
        "max_lights",
        "point_attenuation",
        "point_size",
        "point_sprites",
        "polygon_mode",
        "polygon_mode_overrideable",
        "scene_blend",
        "scene_blend_op",
        "separate_scene_blend",
        "separate_scene_blend_op",
        "shading",
        "shininess",
        "specular",
        "start_light",
        "transparent_sorting",
        "transparent_sorting_force",
        "vertex_colour_tracking",
    }
)
LEGACY_UNIT_DIRECTIVES = frozenset(
    {
        "binding_type",
        "colour_op",
        "colour_op_ex",
        "content_type",
        "env_map",
        "filtering",
        "max_anisotropy",
        "mipmap_bias",
        "tex_address_mode",
        "tex_border_colour",
        "tex_coord_set",
        "texture",
        "texture_alias",
    }
)


class ClassificationFailure(AuditFailure):
    """A stable, fail-closed classifier or parser error."""


@dataclass(frozen=True, slots=True)
class SourceLocation:
    line: int
    column: int


@dataclass(frozen=True, slots=True)
class Token:
    text: str
    folded: str
    start: int
    end: int
    start_location: SourceLocation
    end_location: SourceLocation


@dataclass(frozen=True, slots=True)
class Directive:
    name: str
    arguments: tuple[str, ...]
    start_token: Token
    end_token: Token


@dataclass(frozen=True, slots=True)
class Scope:
    kind: str
    arguments: tuple[str, ...]
    start_token: Token
    open_token: Token
    close_token: Token
    directives: tuple[Directive, ...]
    children: tuple["Scope", ...]


@dataclass(frozen=True, slots=True)
class DecodedScript:
    text: str
    encoding: str
    raw_offsets: tuple[int, ...]
    line_starts: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class ParsedScript:
    archive_member: str
    payload: bytes
    sha256: str
    decoded: DecodedScript
    root: Scope
    token_count: int


def _fail(detail: str) -> NoReturn:
    raise ClassificationFailure(detail)


def canonical_json(value: object, *, pretty: bool = False) -> str:
    if pretty:
        return json.dumps(
            value, ensure_ascii=True, indent=2, sort_keys=True
        ) + "\n"
    return json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ) + "\n"


def _decode_script(payload: bytes, archive_member: str) -> DecodedScript:
    bom_bytes = 0
    codec = "utf-8"
    encoding = "UTF-8"
    undecoded = payload
    if payload.startswith(b"\xef\xbb\xbf"):
        bom_bytes = 3
        undecoded = payload[3:]
        encoding = "UTF-8-BOM"
    try:
        text = undecoded.decode("utf-8")
    except UnicodeDecodeError:
        bom_bytes = 0
        codec = "cp1252"
        encoding = "WINDOWS-1252"
        try:
            text = payload.decode(codec)
        except UnicodeDecodeError as error:
            raise ClassificationFailure(
                "material script is not UTF-8 or Windows-1252: "
                f"{archive_member!r}"
            ) from error
    if "\x00" in text:
        _fail(f"material script contains NUL: {archive_member!r}")

    raw_offsets = [bom_bytes]
    raw_offset = bom_bytes
    for character in text:
        raw_offset += len(character.encode(codec))
        raw_offsets.append(raw_offset)
    if raw_offset != len(payload):
        _fail(f"internal source-offset mismatch: {archive_member!r}")

    line_starts = [0]
    for index, character in enumerate(text):
        if character == "\n":
            line_starts.append(index + 1)
    return DecodedScript(
        text=text,
        encoding=encoding,
        raw_offsets=tuple(raw_offsets),
        line_starts=tuple(line_starts),
    )


def _location(decoded: DecodedScript, offset: int) -> SourceLocation:
    line_index = bisect_right(decoded.line_starts, offset) - 1
    return SourceLocation(
        line=line_index + 1,
        column=offset - decoded.line_starts[line_index] + 1,
    )


def _token(
    decoded: DecodedScript, text: str, start: int, end: int
) -> Token:
    return Token(
        text=text,
        folded=text.casefold(),
        start=start,
        end=end,
        start_location=_location(decoded, start),
        end_location=_location(decoded, end),
    )


def _tokenize(
    decoded: DecodedScript, archive_member: str
) -> tuple[Token, ...]:
    text = decoded.text
    tokens: list[Token] = []
    index = 0
    while index < len(text):
        character = text[index]
        if character.isspace():
            index += 1
            continue
        if text.startswith("//", index):
            newline = text.find("\n", index + 2)
            index = len(text) if newline < 0 else newline
            continue
        if text.startswith("/*", index):
            close = text.find("*/", index + 2)
            if close < 0:
                _fail(f"unterminated block comment: {archive_member!r}")
            index = close + 2
            continue
        if character in "{}":
            tokens.append(_token(decoded, character, index, index + 1))
            index += 1
        elif character == '"':
            start = index
            index += 1
            value: list[str] = []
            while index < len(text) and text[index] != '"':
                if text[index] == "\n":
                    _fail(
                        "newline in quoted material token: "
                        f"{archive_member!r}"
                    )
                if text[index] == "\\" and index + 1 < len(text):
                    escaped = text[index + 1]
                    if escaped in {'"', "\\"}:
                        value.append(escaped)
                        index += 2
                        continue
                value.append(text[index])
                index += 1
            if index >= len(text):
                _fail(
                    f"unterminated quoted material token: {archive_member!r}"
                )
            index += 1
            tokens.append(
                _token(decoded, "".join(value), start, index)
            )
        else:
            start = index
            while index < len(text):
                if text[index].isspace() or text[index] in "{}":
                    break
                if text.startswith("//", index) or text.startswith(
                    "/*", index
                ):
                    break
                index += 1
            if index == start:
                _fail(f"could not tokenize material script: {archive_member!r}")
            tokens.append(_token(decoded, text[start:index], start, index))
        if len(tokens) > MAX_TOKENS_PER_SCRIPT:
            _fail(f"material token count exceeds fixed cap: {archive_member!r}")
    return tuple(tokens)


class _ScopeParser:
    def __init__(
        self,
        tokens: Sequence[Token],
        archive_member: str,
    ) -> None:
        self._tokens = tokens
        self._archive_member = archive_member
        self._index = 0

    def parse(self) -> Scope:
        synthetic = Token(
            text="<root>",
            folded="<root>",
            start=0,
            end=0,
            start_location=SourceLocation(1, 1),
            end_location=SourceLocation(1, 1),
        )
        directives, children, close, recovered = self._parse_body(
            expect_close=False, depth=0, owner_kind="<root>"
        )
        if (
            self._index != len(self._tokens)
            or close is not None
            or recovered
        ):
            _fail(f"internal parser consumption error: {self._archive_member!r}")
        return Scope(
            kind="<root>",
            arguments=(),
            start_token=synthetic,
            open_token=synthetic,
            close_token=synthetic,
            directives=directives,
            children=children,
        )

    def _parse_body(
        self, *, expect_close: bool, depth: int, owner_kind: str
    ) -> tuple[
        tuple[Directive, ...], tuple[Scope, ...], Token | None, bool
    ]:
        if depth > MAX_NESTING_DEPTH:
            _fail(
                f"material nesting exceeds fixed cap: {self._archive_member!r}"
            )
        directives: list[Directive] = []
        children: list[Scope] = []
        statement_count = 0
        while self._index < len(self._tokens):
            token = self._tokens[self._index]
            if expect_close and token.folded == "material":
                previous = (
                    self._tokens[self._index - 1]
                    if self._index > 0
                    else token
                )
                directives.append(
                    Directive(
                        name="__missing_closing_brace_before_material__",
                        arguments=(owner_kind,),
                        start_token=previous,
                        end_token=previous,
                    )
                )
                return tuple(directives), tuple(children), previous, True
            if token.text == "}":
                if not expect_close:
                    # Some compatibility packages contain a recoverable extra
                    # root brace after a complete material.  Preserve it as
                    # explicit evidence; never consume it as an implicit fix.
                    directives.append(
                        Directive(
                            name="__stray_closing_brace__",
                            arguments=(),
                            start_token=token,
                            end_token=token,
                        )
                    )
                    self._index += 1
                    continue
                self._index += 1
                return tuple(directives), tuple(children), token, False
            if token.text == "{":
                # Keep anonymous malformed scopes in the AST so the report can
                # reject their material without pretending the brace was not
                # authored.
                self._index += 1
                (
                    child_directives,
                    child_children,
                    close_token,
                    child_recovered,
                ) = self._parse_body(
                    expect_close=True,
                    depth=depth + 1,
                    owner_kind="__anonymous_scope__",
                )
                if close_token is None:
                    _fail(
                        "unterminated anonymous scope at "
                        f"{self._archive_member}:{token.start_location.line}:"
                        f"{token.start_location.column}"
                    )
                children.append(
                    Scope(
                        kind="__anonymous_scope__",
                        arguments=(),
                        start_token=token,
                        open_token=token,
                        close_token=close_token,
                        directives=child_directives,
                        children=child_children,
                    )
                )
                if child_recovered:
                    return (
                        tuple(directives),
                        tuple(children),
                        close_token,
                        True,
                    )
                continue

            statement_count += 1
            if statement_count > MAX_STATEMENTS_PER_SCOPE:
                _fail(
                    f"scope statement count exceeds fixed cap: "
                    f"{self._archive_member!r}"
                )
            start = token
            words = [token]
            self._index += 1
            while self._index < len(self._tokens):
                candidate = self._tokens[self._index]
                if candidate.text in "{}":
                    break
                if candidate.start_location.line != start.start_location.line:
                    break
                words.append(candidate)
                self._index += 1

            opens_block = False
            if self._index < len(self._tokens):
                candidate = self._tokens[self._index]
                if candidate.text == "{":
                    opens_block = (
                        candidate.start_location.line
                        == start.start_location.line
                        or start.folded in BLOCK_KEYWORDS
                    )
            if opens_block:
                open_token = self._tokens[self._index]
                self._index += 1
                (
                    child_directives,
                    child_children,
                    close_token,
                    child_recovered,
                ) = self._parse_body(
                    expect_close=True,
                    depth=depth + 1,
                    owner_kind=start.folded,
                )
                if close_token is None:
                    _fail(
                        f"unterminated {start.text!r} scope: "
                        f"{self._archive_member}:{start.start_location.line}"
                    )
                children.append(
                    Scope(
                        kind=start.folded,
                        arguments=tuple(word.text for word in words[1:]),
                        start_token=start,
                        open_token=open_token,
                        close_token=close_token,
                        directives=child_directives,
                        children=child_children,
                    )
                )
                if child_recovered:
                    if owner_kind == "<root>":
                        continue
                    return (
                        tuple(directives),
                        tuple(children),
                        close_token,
                        True,
                    )
            else:
                directives.append(
                    Directive(
                        name=start.folded,
                        arguments=tuple(word.text for word in words[1:]),
                        start_token=start,
                        end_token=words[-1],
                    )
                )

        if expect_close:
            _fail(f"unterminated material scope: {self._archive_member!r}")
        return tuple(directives), tuple(children), None, False


def parse_script(payload: bytes, archive_member: str) -> ParsedScript:
    decoded = _decode_script(payload, archive_member)
    tokens = _tokenize(decoded, archive_member)
    root = _ScopeParser(tokens, archive_member).parse()
    return ParsedScript(
        archive_member=archive_member,
        payload=payload,
        sha256=hashlib.sha256(payload).hexdigest(),
        decoded=decoded,
        root=root,
        token_count=len(tokens),
    )


def _children(scope: Scope, kind: str) -> tuple[Scope, ...]:
    return tuple(child for child in scope.children if child.kind == kind)


def _directives(scope: Scope, name: str) -> tuple[Directive, ...]:
    aliases = {name.casefold()}
    if "colour" in name:
        aliases.add(name.replace("colour", "color").casefold())
    if "color" in name:
        aliases.add(name.replace("color", "colour").casefold())
    return tuple(
        directive
        for directive in scope.directives
        if directive.name in aliases
    )


def _flat_scopes(scope: Scope) -> Iterable[Scope]:
    yield scope
    for child in scope.children:
        yield from _flat_scopes(child)


def _flat_directives(scope: Scope) -> Iterable[Directive]:
    yield from scope.directives
    for child in scope.children:
        yield from _flat_directives(child)


def _material_name(scope: Scope) -> tuple[str, str | None]:
    if not scope.arguments:
        _fail(
            "material declaration has no name at "
            f"line {scope.start_token.start_location.line}"
        )
    arguments = list(scope.arguments)
    if len(arguments) == 1 and ":" in arguments[0]:
        name, inherited = arguments[0].split(":", 1)
        if name and inherited:
            return name, inherited
    if len(arguments) == 3 and arguments[1] == ":":
        return arguments[0], arguments[2]
    if len(arguments) != 1:
        _fail(
            "unsupported material declaration arguments at "
            f"line {scope.start_token.start_location.line}"
        )
    return arguments[0], None


def _normal_arguments(directive: Directive) -> tuple[str, ...]:
    return tuple(argument.casefold() for argument in directive.arguments)


def _one_directive(
    scope: Scope, name: str
) -> Directive | None:
    found = _directives(scope, name)
    return found[0] if len(found) == 1 else None


def _only_directive_names(scope: Scope, permitted: set[str]) -> bool:
    canonical = {
        name.replace("color", "colour").casefold() for name in permitted
    }
    return all(
        directive.name.replace("color", "colour") in canonical
        for directive in scope.directives
    )


def _named_texture(unit: Scope) -> str | None:
    texture = _one_directive(unit, "texture")
    if texture is None or len(texture.arguments) != 1:
        return None
    name = texture.arguments[0]
    if not name or "\x00" in name:
        return None
    return name


def _material_shape(
    material: Scope,
) -> tuple[tuple[Scope, ...], tuple[Scope, ...], tuple[Scope, ...]]:
    techniques = _children(material, "technique")
    passes = tuple(
        child
        for technique in techniques
        for child in _children(technique, "pass")
    )
    units = tuple(
        child
        for pass_scope in passes
        for child in _children(pass_scope, "texture_unit")
    )
    return techniques, passes, units


def _base_material_envelope_is_strict(
    material: Scope,
    technique: Scope,
) -> bool:
    if material.arguments and len(material.arguments) != 1:
        return False
    if material.children != (technique,):
        return False
    if technique.arguments or technique.directives:
        return False
    if not _only_directive_names(material, {"receive_shadows"}):
        return False
    if len(_directives(material, "receive_shadows")) > 1:
        return False
    for directive in material.directives:
        if directive.name == "receive_shadows" and _normal_arguments(
            directive
        ) not in {("on",), ("off",)}:
            return False
    return True


def _unit_has_no_nested_or_effects(unit: Scope) -> bool:
    return not unit.arguments and not unit.children and not any(
        directive.name in UNIT_TRANSFORM_DIRECTIVES
        or directive.name in {"cubic_texture", "texture_source"}
        for directive in unit.directives
    )


def _match_family_a(
    material: Scope, *, ignore_one_orphan: bool
) -> bool:
    techniques, passes, _ = _material_shape(material)
    if len(techniques) != 1 or len(passes) != 1:
        return False
    technique = techniques[0]
    pass_scope = passes[0]
    if not _base_material_envelope_is_strict(material, technique):
        return False
    if technique.children != (pass_scope,):
        return False
    if pass_scope.arguments or pass_scope.children != _children(
        pass_scope, "texture_unit"
    ):
        return False
    if pass_scope.directives:
        return False
    units = _children(pass_scope, "texture_unit")
    if len(units) != 3:
        return False
    base, specular, environment = units
    if not all(_unit_has_no_nested_or_effects(unit) for unit in units):
        return False

    base_names = {"texture", "colour_op"}
    if ignore_one_orphan:
        base_names.add("texture_unit")
    if not _only_directive_names(base, base_names):
        return False
    if ignore_one_orphan:
        orphans = _directives(base, "texture_unit")
        if len(orphans) != 1 or orphans[0].arguments:
            return False
    elif _directives(base, "texture_unit"):
        return False
    if _named_texture(base) is None:
        return False
    base_operations = _directives(base, "colour_op")
    if len(base_operations) > 1:
        return False
    if base_operations and _normal_arguments(base_operations[0]) != (
        "modulate",
    ):
        return False

    if not _only_directive_names(specular, {"texture", "colour_op"}):
        return False
    if _named_texture(specular) is None:
        return False
    specular_operation = _one_directive(specular, "colour_op")
    if specular_operation is None or _normal_arguments(
        specular_operation
    ) != ("alpha_blend",):
        return False

    if not _only_directive_names(
        environment, {"texture", "colour_op_ex", "env_map"}
    ):
        return False
    if _named_texture(environment) is None:
        return False
    environment_operation = _one_directive(environment, "colour_op_ex")
    environment_mapping = _one_directive(environment, "env_map")
    if environment_operation is None or _normal_arguments(
        environment_operation
    ) != ("blend_current_alpha", "src_texture", "src_current"):
        return False
    if environment_mapping is None or _normal_arguments(
        environment_mapping
    ) != ("spherical",):
        return False
    return not any(
        _directives(unit, "alpha_op_ex") or _directives(unit, "alpha_op")
        for unit in units
    )


def _depth_write(scope: Scope) -> str | None:
    directives = _directives(scope, "depth_write")
    if not directives:
        return "DEFAULT_ON"
    if len(directives) != 1 or len(directives[0].arguments) != 1:
        return None
    value = directives[0].arguments[0].casefold()
    if value == "on":
        return "EXPLICIT_ON"
    if value == "off":
        return "EXPLICIT_OFF"
    return None


def _decimal_is(value: str, expected: str) -> bool:
    try:
        return float(value) == float(expected)
    except (OverflowError, ValueError):
        return False


def _match_family_b(
    material: Scope, *, ignore_one_orphan: bool = False
) -> tuple[bool, str | None]:
    techniques, passes, _ = _material_shape(material)
    if len(techniques) != 1 or len(passes) != 2:
        return False, None
    technique = techniques[0]
    if not _base_material_envelope_is_strict(material, technique):
        return False, None
    if technique.children != passes:
        return False, None
    base_pass, emissive_pass = passes
    if base_pass.arguments or emissive_pass.arguments:
        return False, None
    base_units = _children(base_pass, "texture_unit")
    emissive_units = _children(emissive_pass, "texture_unit")
    if len(base_units) != 1 or len(emissive_units) != 1:
        return False, None
    if base_pass.children != base_units or emissive_pass.children != emissive_units:
        return False, None
    if not _only_directive_names(base_pass, {"depth_write"}):
        return False, None
    if _depth_write(base_pass) not in {"DEFAULT_ON", "EXPLICIT_ON"}:
        return False, None
    permitted_emissive_directives = {
        "alpha_rejection",
        "scene_blend",
        "emissive",
        "depth_write",
    }
    if ignore_one_orphan:
        permitted_emissive_directives.add("texture_unit")
    if not _only_directive_names(
        emissive_pass,
        permitted_emissive_directives,
    ):
        return False, None
    orphan_directives = _directives(emissive_pass, "texture_unit")
    if ignore_one_orphan:
        if len(orphan_directives) != 1 or orphan_directives[0].arguments:
            return False, None
    elif orphan_directives:
        return False, None
    authored_depth_write = _depth_write(emissive_pass)
    if authored_depth_write not in {
        "DEFAULT_ON",
        "EXPLICIT_ON",
        "EXPLICIT_OFF",
    }:
        return False, None
    rejection = _one_directive(emissive_pass, "alpha_rejection")
    blend = _one_directive(emissive_pass, "scene_blend")
    emissive = _one_directive(emissive_pass, "emissive")
    if rejection is None or _normal_arguments(rejection) not in {
        ("greater", "128"),
        ("greater", "192"),
    }:
        return False, None
    if blend is None or _normal_arguments(blend) != ("alpha_blend",):
        return False, None
    if emissive is None or len(emissive.arguments) not in {3, 4}:
        return False, None
    if not all(
        _decimal_is(value, "0.3") for value in emissive.arguments[:3]
    ):
        return False, None

    for unit in (base_units[0], emissive_units[0]):
        if not _unit_has_no_nested_or_effects(unit):
            return False, None
        if not _only_directive_names(unit, {"texture"}):
            return False, None
        if _named_texture(unit) is None:
            return False, None
    return True, authored_depth_write


def _environment_modes(material: Scope) -> tuple[str, ...]:
    values: list[str] = []
    for directive in _flat_directives(material):
        if directive.name == "env_map" and len(directive.arguments) == 1:
            values.append(directive.arguments[0].casefold())
    return tuple(values)


def _texture_references(material: Scope) -> tuple[str, ...]:
    values: list[str] = []
    for directive in _flat_directives(material):
        if directive.name in {"texture", "cubic_texture"}:
            values.extend(directive.arguments[:1])
    return tuple(values)


def _has_program(material: Scope) -> bool:
    return any(scope.kind in PROGRAM_BLOCKS for scope in _flat_scopes(material))


def _has_emissive_pass(material: Scope) -> bool:
    return any(
        directive.name == "emissive"
        for directive in _flat_directives(material)
    )


def _has_cube_source(material: Scope) -> bool:
    return any(
        directive.name == "cubic_texture"
        for directive in _flat_directives(material)
    )


def _orphan_texture_unit_directives(
    material: Scope,
) -> tuple[tuple[Scope, Directive], ...]:
    found: list[tuple[Scope, Directive]] = []
    for scope in _flat_scopes(material):
        for directive in scope.directives:
            if directive.name == "texture_unit":
                found.append((scope, directive))
    return tuple(found)


def _looks_like_bus_stop(material: Scope) -> bool:
    techniques, passes, units = _material_shape(material)
    if len(techniques) != 1 or len(passes) != 1 or len(units) != 2:
        return False
    pass_scope = passes[0]
    technique = techniques[0]
    if not _base_material_envelope_is_strict(material, technique):
        return False
    if technique.children != (pass_scope,) or pass_scope.children != units:
        return False
    if len(pass_scope.directives) != 4:
        return False
    required_pass = {
        directive.name: _normal_arguments(directive)
        for directive in pass_scope.directives
    }
    if required_pass != {
        "scene_blend": ("alpha_blend",),
        "depth_write": ("off",),
        "alpha_rejection": ("greater", "0"),
        "cull_hardware": ("none",),
    }:
        return False
    first, second = units
    if _named_texture(first) is None or _named_texture(second) is None:
        return False
    if not _unit_has_no_nested_or_effects(
        first
    ) or not _unit_has_no_nested_or_effects(second):
        return False
    if not _only_directive_names(first, {"texture"}):
        return False
    if not _only_directive_names(
        second, {"texture", "colour_op_ex", "env_map"}
    ):
        return False
    operation = _one_directive(second, "colour_op_ex")
    env = _one_directive(second, "env_map")
    return (
        operation is not None
        and _normal_arguments(operation)
        == ("blend_manual", "src_texture", "src_current", "0.1")
        and env is not None
        and _normal_arguments(env) == ("spherical",)
    )


def _legacy_equations_representable(material: Scope) -> bool:
    techniques, passes, _ = _material_shape(material)
    if len(techniques) != 1 or _has_program(material):
        return False
    _, inherited_from = _material_name(material)
    if inherited_from is not None or _material_structural_anomalies_for_scope(
        material
    ):
        return False
    technique = techniques[0]
    if material.children != (technique,):
        return False
    if any(
        directive.name not in LEGACY_MATERIAL_DIRECTIVES
        for directive in material.directives
    ):
        return False
    if any(
        directive.name not in LEGACY_TECHNIQUE_DIRECTIVES
        for directive in technique.directives
    ):
        return False
    if technique.children != passes:
        return False
    for pass_scope in passes:
        units = _children(pass_scope, "texture_unit")
        if pass_scope.children != units:
            return False
        if any(
            directive.name not in LEGACY_PASS_DIRECTIVES
            for directive in pass_scope.directives
        ):
            return False
        for unit in units:
            if unit.arguments or unit.children:
                return False
            if any(
                directive.name.replace("color", "colour")
                not in LEGACY_UNIT_DIRECTIVES
                for directive in unit.directives
            ):
                return False
            textures = _directives(unit, "texture")
            if len(textures) != 1 or len(textures[0].arguments) != 1:
                return False
            color = _directives(unit, "colour_op")
            color_ex = _directives(unit, "colour_op_ex")
            env = _directives(unit, "env_map")
            if len(color) > 1 or len(color_ex) > 1 or len(env) > 1:
                return False
            if color and color_ex:
                return False
            if env and _normal_arguments(env[0]) not in {
                ("planar",),
                ("spherical",),
            }:
                return False
    return all(
        "UNREPRESENTED" not in equation
        for equation in _layer_equations(material)
    )


def _material_structural_anomalies_for_scope(material: Scope) -> bool:
    if _orphan_texture_unit_directives(material):
        return True
    return any(
        scope.kind == "__anonymous_scope__"
        or any(
            directive.name.startswith("__")
            for directive in scope.directives
        )
        for scope in _flat_scopes(material)
    )


def _is_simple_single_pass(material: Scope) -> bool:
    techniques, passes, units = _material_shape(material)
    if len(techniques) != 1 or len(passes) != 1 or len(units) > 1:
        return False
    if _has_program(material) or _environment_modes(material):
        return False
    if any(
        directive.name in UNIT_TRANSFORM_DIRECTIVES
        for directive in _flat_directives(material)
    ):
        return False
    technique = techniques[0]
    pass_scope = passes[0]
    if material.children != (technique,) or technique.children != (pass_scope,):
        return False
    if pass_scope.children != _children(pass_scope, "texture_unit"):
        return False
    return all(not unit.children for unit in units)


def _is_additive_furniture(material: Scope) -> bool:
    techniques, passes, units = _material_shape(material)
    if len(techniques) != 1 or len(passes) != 2 or len(units) != 1:
        return False
    if len(_children(passes[0], "texture_unit")) != 1:
        return False
    if _children(passes[1], "texture_unit"):
        return False
    first_emissive = _one_directive(passes[0], "emissive")
    second_specular = _one_directive(passes[1], "specular")
    blend = _one_directive(passes[1], "scene_blend")
    return (
        first_emissive is not None
        and len(first_emissive.arguments) >= 3
        and all(
            _decimal_is(value, "0.0")
            for value in first_emissive.arguments[:3]
        )
        and second_specular is not None
        and blend is not None
        and _normal_arguments(blend) == ("add",)
    )


def _emissive_rgb_is(material: Scope, value: str) -> bool:
    emissive = tuple(
        directive
        for directive in _flat_directives(material)
        if directive.name == "emissive"
    )
    return len(emissive) == 1 and len(emissive[0].arguments) >= 3 and all(
        _decimal_is(component, value)
        for component in emissive[0].arguments[:3]
    )


def _source_span(
    script: ParsedScript,
    start_token: Token,
    end_token: Token,
) -> dict[str, object]:
    byte_start = script.decoded.raw_offsets[start_token.start]
    byte_end = script.decoded.raw_offsets[end_token.end]
    payload = script.payload[byte_start:byte_end]
    return {
        "byte_end_exclusive": byte_end,
        "byte_start": byte_start,
        "end_exclusive": {
            "column": end_token.end_location.column,
            "line": end_token.end_location.line,
        },
        "sha256": hashlib.sha256(payload).hexdigest(),
        "start": {
            "column": start_token.start_location.column,
            "line": start_token.start_location.line,
        },
    }


def _scope_parser_anomalies(
    script: ParsedScript, scope: Scope
) -> list[dict[str, object]]:
    anomalies: list[dict[str, object]] = []
    if scope.kind == "__anonymous_scope__":
        anomalies.append(
            {
                "code": "ANONYMOUS_SCOPE",
                "owning_scope": scope.kind,
                "source_span": _source_span(
                    script, scope.open_token, scope.close_token
                ),
            }
        )
    for directive in scope.directives:
        if directive.name == "__stray_closing_brace__":
            code = "STRAY_ROOT_CLOSING_BRACE"
        elif directive.name == "__missing_closing_brace_before_material__":
            code = "MISSING_CLOSING_BRACE_BEFORE_NEXT_MATERIAL"
        else:
            continue
        anomalies.append(
            {
                "code": code,
                "owning_scope": scope.kind,
                "source_span": _source_span(
                    script, directive.start_token, directive.end_token
                ),
            }
        )
    for child in scope.children:
        anomalies.extend(_scope_parser_anomalies(script, child))
    return anomalies


def _material_structural_anomalies(
    script: ParsedScript, material: Scope
) -> list[dict[str, object]]:
    anomalies = _scope_parser_anomalies(script, material)
    for owner, directive in _orphan_texture_unit_directives(material):
        if owner.kind == "texture_unit":
            code = "ORPHAN_NESTED_TEXTURE_UNIT_DIRECTIVE"
        else:
            code = "ORPHAN_PASS_LEVEL_TEXTURE_UNIT_DIRECTIVE"
        anomalies.append(
            {
                "code": code,
                "owning_scope": owner.kind,
                "source_span": _source_span(
                    script, directive.start_token, directive.end_token
                ),
            }
        )
    anomalies.sort(
        key=lambda item: (
            int(item["source_span"]["byte_start"]),
            str(item["code"]),
        )
    )
    return anomalies


def _material_id(
    script: ParsedScript, name: str, span: dict[str, object]
) -> str:
    identity = (
        f"{script.archive_member}\0{script.sha256}\0{name}\0"
        f"{span['byte_start']}\0{span['byte_end_exclusive']}\0{span['sha256']}"
    ).encode("utf-8")
    return hashlib.sha256(identity).hexdigest()


def _repair_plan(
    *,
    archive_digest: str,
    script: ParsedScript,
    material_id: str,
    material_span: dict[str, object],
    directive: Directive,
) -> dict[str, object]:
    token_span = _source_span(script, directive.start_token, directive.end_token)
    identity = (
        f"ORPHAN_NESTED_TEXTURE_UNIT_DIRECTIVE\0{archive_digest}\0"
        f"{script.archive_member}\0{script.sha256}\0{token_span['sha256']}\0"
        f"{token_span['byte_start']}\0{token_span['byte_end_exclusive']}"
    ).encode("utf-8")
    repair_id = hashlib.sha256(identity).hexdigest()
    return {
        "action": "REMOVE_ORPHAN_DIRECTIVE_TOKEN",
        "apply_automatically": False,
        "expected_family_after_review": FAMILY_SPHERICAL,
        "gate": {
            "archive_sha256": archive_digest,
            "material_source_span_sha256": material_span["sha256"],
            "script_member": script.archive_member,
            "script_sha256": script.sha256,
            "token_source_span": token_span,
        },
        "issue": "ORPHAN_NESTED_TEXTURE_UNIT_DIRECTIVE",
        "material_id": material_id,
        "repair_id": repair_id,
        "review_state": "PENDING_HUMAN_REVIEW",
    }


def _layer_equations(material: Scope) -> list[str]:
    equations: list[str] = []
    techniques, passes, _ = _material_shape(material)
    for pass_index, pass_scope in enumerate(passes):
        blend = _one_directive(pass_scope, "scene_blend")
        blend_args = _normal_arguments(blend) if blend is not None else ()
        if not blend_args:
            equations.append(f"PASS_{pass_index}:OPAQUE_REPLACE")
        elif blend_args == ("alpha_blend",):
            equations.append(f"PASS_{pass_index}:SOURCE_OVER")
        elif blend_args in {("add",), ("one", "one")}:
            equations.append(f"PASS_{pass_index}:ADDITIVE")
        else:
            equations.append(f"PASS_{pass_index}:UNREPRESENTED_BLEND")
        for unit_index, unit in enumerate(
            _children(pass_scope, "texture_unit")
        ):
            operation = _one_directive(unit, "colour_op")
            operation_ex = _one_directive(unit, "colour_op_ex")
            if operation_ex is not None:
                arguments = _normal_arguments(operation_ex)
                if arguments == (
                    "blend_current_alpha",
                    "src_texture",
                    "src_current",
                ):
                    equation = "BLEND_CURRENT_ALPHA(texture,current)"
                elif arguments == (
                    "blend_manual",
                    "src_texture",
                    "src_current",
                    "0.1",
                ):
                    equation = "BLEND_MANUAL(texture,current,0.1)"
                else:
                    equation = "UNREPRESENTED_COMBINE"
            elif operation is not None:
                arguments = _normal_arguments(operation)
                if arguments == ("modulate",):
                    equation = "MODULATE(texture,current)"
                elif arguments == ("alpha_blend",):
                    equation = "BLEND_TEXTURE_ALPHA(texture,current)"
                else:
                    equation = "UNREPRESENTED_COMBINE"
            else:
                equation = "DEFAULT_MODULATE(texture,current)"
            env = _one_directive(unit, "env_map")
            if env is not None and len(env.arguments) == 1:
                equation += f"@{env.arguments[0].casefold().upper()}_ENV"
            equations.append(
                f"PASS_{pass_index}_UNIT_{unit_index}:{equation}"
            )
    if not techniques:
        equations.append("NO_TECHNIQUE")
    return equations


def _classify_material(
    *,
    archive_digest: str,
    script: ParsedScript,
    material: Scope,
) -> tuple[dict[str, object], list[dict[str, object]]]:
    name, inherited_from = _material_name(material)
    material_span = _source_span(
        script, material.start_token, material.close_token
    )
    material_id = _material_id(script, name, material_span)
    techniques, passes, units = _material_shape(material)
    environments = _environment_modes(material)
    textures = _texture_references(material)
    orphans = _orphan_texture_unit_directives(material)
    structural_anomalies = _material_structural_anomalies(script, material)
    repair_plans: list[dict[str, object]] = []
    classification_status = CLASSIFICATION_ELIGIBLE
    family = FAMILY_UNSUPPORTED
    fidelity_label = FIDELITY_LEGACY
    legacy_eligible = False
    modernization_eligible = False
    blockers: list[str] = []
    authored_depth_write: str | None = None

    if _match_family_a(material, ignore_one_orphan=False):
        family = FAMILY_SPHERICAL
        fidelity_label = FIDELITY_PBR
        legacy_eligible = True
        modernization_eligible = True
    elif len(orphans) == 1 and _match_family_a(
        material, ignore_one_orphan=True
    ):
        family = FAMILY_SPHERICAL
        fidelity_label = FIDELITY_PBR
        classification_status = CLASSIFICATION_REVIEW_BLOCKED
        blockers.append("PENDING_HASH_AND_SPAN_GATED_REPAIR_REVIEW")
        repair_plans.append(
            _repair_plan(
                archive_digest=archive_digest,
                script=script,
                material_id=material_id,
                material_span=material_span,
                directive=orphans[0][1],
            )
        )
    else:
        family_b, authored_depth_write = _match_family_b(material)
        if family_b:
            family = FAMILY_EMISSIVE
            fidelity_label = FIDELITY_PBR
            legacy_eligible = True
            modernization_eligible = True
        elif (
            len(orphans) == 1
            and orphans[0][0].kind == "pass"
            and _match_family_b(material, ignore_one_orphan=True)[0]
        ):
            family = FAMILY_SUSPICIOUS_B
            classification_status = CLASSIFICATION_REVIEW_BLOCKED
            blockers.append("ORPHAN_PASS_LEVEL_TEXTURE_UNIT_DIRECTIVE")
        elif (
            structural_anomalies
            and "planar" in environments
            and any(
                texture.casefold() == "cielo.jpg" for texture in textures
            )
        ):
            family = FAMILY_CIELO
            classification_status = CLASSIFICATION_REVIEW_BLOCKED
            blockers.extend(
                sorted(
                    {
                        str(anomaly["code"])
                        for anomaly in structural_anomalies
                    }
                )
            )
        elif structural_anomalies:
            family = FAMILY_SUSPICIOUS
            classification_status = CLASSIFICATION_REVIEW_BLOCKED
            blockers.extend(
                sorted(
                    {
                        str(anomaly["code"])
                        for anomaly in structural_anomalies
                    }
                )
            )
        elif _looks_like_bus_stop(material):
            family = FAMILY_BUS_STOP
            legacy_eligible = True
        elif (
            "planar" in environments
            and _has_cube_source(material)
        ):
            family = FAMILY_CUBE_PLANAR
            classification_status = CLASSIFICATION_REVIEW_BLOCKED
            blockers.append("CUBE_SOURCE_WITH_PLANAR_ENVIRONMENT_COORDINATES")
        elif (
            "planar" in environments
            and any(texture.casefold() == "cielo.jpg" for texture in textures)
        ):
            family = FAMILY_CIELO
            legacy_eligible = _legacy_equations_representable(material)
        elif len(passes) == 2 and environments and _has_emissive_pass(material):
            if "planar" in environments and _emissive_rgb_is(
                material, "0.3"
            ):
                family = FAMILY_COMBINED_PLANAR_EMISSIVE
            else:
                family = FAMILY_COMBINED_ENV_EMISSIVE
            legacy_eligible = _legacy_equations_representable(material)
        elif (
            "planar" in environments
            and any(
                texture.casefold() == "superficie-metalica.jpg"
                for texture in textures
            )
        ):
            family = FAMILY_PLANAR_SURFACE
            legacy_eligible = _legacy_equations_representable(material)
        elif "planar" in environments:
            family = FAMILY_PLANAR_OTHER
            legacy_eligible = _legacy_equations_representable(material)
        elif _is_additive_furniture(material):
            family = FAMILY_ADDITIVE_FURNITURE
            classification_status = CLASSIFICATION_REVIEW_BLOCKED
            blockers.append("ADDITIVE_SPECULAR_MULTIPASS_REQUIRES_REVIEW")
        elif _is_simple_single_pass(material):
            family = FAMILY_SIMPLE
            legacy_eligible = _legacy_equations_representable(material)
        else:
            family = FAMILY_UNSUPPORTED
            classification_status = CLASSIFICATION_UNSUPPORTED
            blockers.append("NO_VERIFIED_STRUCTURAL_LOWERING")

    if inherited_from is not None:
        family = FAMILY_UNSUPPORTED
        classification_status = CLASSIFICATION_UNSUPPORTED
        legacy_eligible = False
        modernization_eligible = False
        if "SCRIPT_INHERITANCE_REQUIRES_NATIVE_RESOLUTION" not in blockers:
            blockers.append("SCRIPT_INHERITANCE_REQUIRES_NATIVE_RESOLUTION")
    if _has_program(material):
        family = FAMILY_UNSUPPORTED
        classification_status = CLASSIFICATION_UNSUPPORTED
        legacy_eligible = False
        modernization_eligible = False
        if "AUTHORED_GPU_PROGRAM_REQUIRES_NATIVE_RESOLUTION" not in blockers:
            blockers.append("AUTHORED_GPU_PROGRAM_REQUIRES_NATIVE_RESOLUTION")

    if (
        classification_status == CLASSIFICATION_ELIGIBLE
        and fidelity_label == FIDELITY_LEGACY
        and not legacy_eligible
    ):
        classification_status = CLASSIFICATION_UNSUPPORTED
        blockers.append("EXACT_LAYER_EQUATION_NOT_REPRESENTABLE")

    if classification_status != CLASSIFICATION_ELIGIBLE:
        modernization_eligible = False
    if fidelity_label == FIDELITY_PBR and modernization_eligible:
        blockers.append("EXPLICIT_REVIEWED_PBR_DECLARATION_REQUIRED")

    record: dict[str, object] = {
        "classification": {
            "family": family,
            "status": classification_status,
        },
        "fidelity": {
            "assigned_label": fidelity_label,
            "declared_pbr_modernization_eligible": modernization_eligible,
            "legacy_semantic_equivalent_eligible": legacy_eligible,
            "requirements_or_blockers": sorted(blockers),
        },
        "material_id": material_id,
        "name": name,
        "repair_plan_ids": sorted(
            str(plan["repair_id"]) for plan in repair_plans
        ),
        "source": {
            "script_member": script.archive_member,
            "script_sha256": script.sha256,
            "span": material_span,
        },
        "structure": {
            "anomalies": structural_anomalies,
            "authored_depth_write_second_pass": authored_depth_write,
            "environment_modes": list(environments),
            "has_authored_gpu_program": _has_program(material),
            "has_cube_texture_source": _has_cube_source(material),
            "inherited_from": inherited_from,
            "layer_equations": _layer_equations(material),
            "orphan_nested_texture_unit_directives": sum(
                owner.kind == "texture_unit" for owner, _ in orphans
            ),
            "pass_count": len(passes),
            "technique_count": len(techniques),
            "texture_references": list(textures),
            "texture_unit_count": len(units),
        },
    }
    return record, repair_plans


def _is_sha256(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(
        r"[0-9a-f]{64}", value
    ) is not None


def validate_report_contract(report: dict[str, object]) -> None:
    """Validate closed fields and cross-record bindings without JSON Schema."""

    expected_root = {
        "$schema",
        "archive",
        "classifier",
        "format",
        "materials",
        "repair_plans",
        "schema",
        "scripts",
        "summary",
    }
    if set(report) != expected_root:
        _fail("internal report root does not match schema v1")
    if report["$schema"] != SCHEMA_RELATIVE_PATH:
        _fail("internal report schema path mismatch")
    if report["schema"] != SCHEMA_ID or report["format"] != REPORT_FORMAT:
        _fail("internal report version mismatch")

    archive = report["archive"]
    classifier = report["classifier"]
    materials = report["materials"]
    plans = report["repair_plans"]
    scripts = report["scripts"]
    summary = report["summary"]
    if not all(
        isinstance(value, expected)
        for value, expected in (
            (archive, dict),
            (classifier, dict),
            (materials, list),
            (plans, list),
            (scripts, list),
            (summary, dict),
        )
    ):
        _fail("internal report has an invalid aggregate type")
    if not _is_sha256(archive.get("sha256")):
        _fail("internal report archive digest is invalid")
    if classifier != {
        "algorithm": CLASSIFIER_ALGORITHM,
        "schema_sha256": hashlib.sha256(SCHEMA_PATH.read_bytes()).hexdigest(),
        "tool_sha256": hashlib.sha256(
            Path(__file__).resolve().read_bytes()
        ).hexdigest(),
    }:
        _fail("internal classifier provenance mismatch")

    script_by_member: dict[str, dict[str, object]] = {}
    for script in scripts:
        if not isinstance(script, dict):
            _fail("internal script record is not an object")
        if set(script) != {
            "bytes",
            "encoding",
            "material_definitions",
            "member",
            "parser_anomalies",
            "sha256",
            "token_count",
        }:
            _fail("internal script record does not match schema v1")
        member = script.get("member")
        digest = script.get("sha256")
        token_count = script.get("token_count")
        if (
            not isinstance(member, str)
            or not member
            or not _is_sha256(digest)
            or isinstance(token_count, bool)
            or not isinstance(token_count, int)
            or token_count < 0
            or token_count > MAX_TOKENS_PER_SCRIPT
        ):
            _fail("internal script identity is invalid")
        if member in script_by_member:
            _fail("internal script member is duplicated")
        script_by_member[member] = script
    if list(script_by_member) != sorted(
        script_by_member, key=lambda value: value.encode("utf-8")
    ):
        _fail("internal script records are not canonical")

    material_by_id: dict[str, dict[str, object]] = {}
    repair_references: Counter[str] = Counter()
    previous_material_key: tuple[bytes, int, bytes] | None = None
    family_counts: Counter[str] = Counter()
    status_counts: Counter[str] = Counter()
    fidelity_counts: Counter[str] = Counter()
    names: dict[str, list[str]] = {}
    for material in materials:
        if not isinstance(material, dict):
            _fail("internal material record is not an object")
        material_id = material.get("material_id")
        name = material.get("name")
        source = material.get("source")
        classification = material.get("classification")
        fidelity = material.get("fidelity")
        repair_ids = material.get("repair_plan_ids")
        if (
            not _is_sha256(material_id)
            or not isinstance(name, str)
            or not name
            or not isinstance(source, dict)
            or not isinstance(classification, dict)
            or not isinstance(fidelity, dict)
            or not isinstance(repair_ids, list)
        ):
            _fail("internal material identity or aggregate is invalid")
        if material_id in material_by_id:
            _fail("internal material ID is duplicated")
        material_by_id[material_id] = material

        member = source.get("script_member")
        script_digest = source.get("script_sha256")
        span = source.get("span")
        if (
            not isinstance(member, str)
            or member not in script_by_member
            or script_digest != script_by_member[member]["sha256"]
            or not isinstance(span, dict)
            or not _is_sha256(span.get("sha256"))
        ):
            _fail("internal material source binding is invalid")
        byte_start = span.get("byte_start")
        byte_end = span.get("byte_end_exclusive")
        if (
            isinstance(byte_start, bool)
            or not isinstance(byte_start, int)
            or isinstance(byte_end, bool)
            or not isinstance(byte_end, int)
            or byte_start < 0
            or byte_end <= byte_start
            or byte_end > script_by_member[member]["bytes"]
        ):
            _fail("internal material byte span is invalid")
        material_key = (
            member.encode("utf-8"),
            byte_start,
            name.encode("utf-8"),
        )
        if previous_material_key is not None and material_key < previous_material_key:
            _fail("internal material records are not canonical")
        previous_material_key = material_key

        family = classification.get("family")
        status = classification.get("status")
        label = fidelity.get("assigned_label")
        modernization_eligible = fidelity.get(
            "declared_pbr_modernization_eligible"
        )
        legacy_eligible = fidelity.get(
            "legacy_semantic_equivalent_eligible"
        )
        requirements = fidelity.get("requirements_or_blockers")
        if not all(
            isinstance(value, str) for value in (family, status, label)
        ) or not isinstance(modernization_eligible, bool) or not isinstance(
            legacy_eligible, bool
        ):
            _fail("internal material classification is invalid")
        if not isinstance(requirements, list) or requirements != sorted(
            set(requirements)
        ):
            _fail("internal material blocker list is not canonical")
        if label == FIDELITY_PBR and family not in {
            FAMILY_SPHERICAL,
            FAMILY_EMISSIVE,
        }:
            _fail("internal PBR label is assigned outside strict families")
        if modernization_eligible and (
            label != FIDELITY_PBR
            or status != CLASSIFICATION_ELIGIBLE
            or "EXPLICIT_REVIEWED_PBR_DECLARATION_REQUIRED"
            not in requirements
        ):
            _fail("internal PBR eligibility is not declaration-gated")
        if status == CLASSIFICATION_ELIGIBLE and (
            (label == FIDELITY_PBR and not modernization_eligible)
            or (label == FIDELITY_LEGACY and not legacy_eligible)
        ):
            _fail("internal eligible status has no eligible fidelity path")
        for repair_id in repair_ids:
            if not _is_sha256(repair_id):
                _fail("internal material repair reference is invalid")
            repair_references[repair_id] += 1
        family_counts[family] += 1
        status_counts[status] += 1
        fidelity_counts[label] += 1
        names.setdefault(name, []).append(
            f"{member}:{span['start']['line']}"
        )

    plan_by_id: dict[str, dict[str, object]] = {}
    for plan in plans:
        if not isinstance(plan, dict):
            _fail("internal repair plan is not an object")
        plan_id = plan.get("repair_id")
        material_id = plan.get("material_id")
        gate = plan.get("gate")
        if (
            not _is_sha256(plan_id)
            or plan_id in plan_by_id
            or material_id not in material_by_id
            or not isinstance(gate, dict)
        ):
            _fail("internal repair-plan identity is invalid")
        material = material_by_id[material_id]
        source = material["source"]
        if (
            plan.get("apply_automatically") is not False
            or plan.get("review_state") != "PENDING_HUMAN_REVIEW"
            or material["classification"]["status"]
            != CLASSIFICATION_REVIEW_BLOCKED
            or gate.get("archive_sha256") != archive["sha256"]
            or gate.get("script_member") != source["script_member"]
            or gate.get("script_sha256") != source["script_sha256"]
            or gate.get("material_source_span_sha256")
            != source["span"]["sha256"]
        ):
            _fail("internal repair plan is not fail-closed")
        plan_by_id[plan_id] = plan
    if set(repair_references) != set(plan_by_id) or any(
        count != 1 for count in repair_references.values()
    ):
        _fail("internal repair-plan references are not one-to-one")

    expected_duplicates = [
        {"locations": locations, "name": name}
        for name, locations in sorted(names.items())
        if len(locations) > 1
    ]
    if (
        summary.get("material_definitions") != len(materials)
        or summary.get("script_files") != len(scripts)
        or summary.get("repair_plans_pending_review") != len(plans)
        or summary.get("family_counts") != dict(sorted(family_counts.items()))
        or summary.get("classification_status_counts")
        != dict(sorted(status_counts.items()))
        or summary.get("fidelity_label_counts")
        != dict(sorted(fidelity_counts.items()))
        or summary.get("duplicate_material_names") != expected_duplicates
    ):
        _fail("internal summary does not match report records")
    expected_ready = (
        not plans
        and not status_counts[CLASSIFICATION_REVIEW_BLOCKED]
        and not status_counts[CLASSIFICATION_UNSUPPORTED]
        and summary.get("parser_anomalies") == 0
        and not expected_duplicates
    )
    if summary.get("automatic_modernization_ready") is not expected_ready:
        _fail("internal automatic-modernization gate is inconsistent")


def classify_archive(
    path: Path, *, expected_sha256: str | None = None
) -> dict[str, object]:
    if not path.is_file():
        _fail("input archive does not exist or is not a file")
    if expected_sha256 is not None:
        expected = expected_sha256.casefold()
        if not re.fullmatch(r"[0-9a-f]{64}", expected):
            _fail("expected archive SHA-256 must be 64 hexadecimal characters")
    else:
        expected = None
    digest = archive_sha256(path)
    if expected is not None:
        if digest != expected:
            _fail(
                f"archive SHA-256 mismatch: expected {expected}, got {digest}"
            )

    script_records: list[dict[str, object]] = []
    material_records: list[dict[str, object]] = []
    repair_plans: list[dict[str, object]] = []
    total_script_bytes = 0
    total_tokens = 0
    try:
        with zipfile.ZipFile(path) as archive:
            infos = archive.infolist()
            expanded_bytes, _ = validate_archive_members(infos)
            material_infos = sorted(
                (
                    info
                    for info in infos
                    if not info.is_dir()
                    and PurePosixPath(info.filename).suffix.casefold()
                    == ".material"
                ),
                key=lambda info: info.filename.encode("utf-8"),
            )
            if not material_infos:
                _fail("archive contains no material scripts")
            for info in material_infos:
                if info.file_size > MAX_TEXT_BYTES:
                    _fail(
                        "material script exceeds bounded text limit: "
                        f"{info.filename!r}"
                    )
                total_script_bytes += info.file_size
                if total_script_bytes > MAX_TOTAL_MATERIAL_SCRIPT_BYTES:
                    _fail("aggregate material script bytes exceed fixed cap")
                payload = archive.read(info)
                if len(payload) != info.file_size:
                    _fail(
                        f"short read for material script: {info.filename!r}"
                    )
                parsed = parse_script(payload, info.filename)
                total_tokens += parsed.token_count
                if total_tokens > MAX_TOTAL_TOKENS:
                    _fail("aggregate material token count exceeds fixed cap")
                materials = _children(parsed.root, "material")
                if (
                    len(material_records) + len(materials)
                    > MAX_MATERIAL_DEFINITIONS
                ):
                    _fail("archive material definition count exceeds fixed cap")
                for material in materials:
                    record, plans = _classify_material(
                        archive_digest=digest,
                        script=parsed,
                        material=material,
                    )
                    material_records.append(record)
                    repair_plans.extend(plans)
                script_records.append(
                    {
                        "bytes": len(parsed.payload),
                        "encoding": parsed.decoded.encoding,
                        "material_definitions": len(materials),
                        "member": parsed.archive_member,
                        "parser_anomalies": _scope_parser_anomalies(
                            parsed, parsed.root
                        ),
                        "sha256": parsed.sha256,
                        "token_count": parsed.token_count,
                    }
                )
    except zipfile.BadZipFile as error:
        raise ClassificationFailure("input is not a valid ZIP archive") from error
    except NotImplementedError as error:
        raise ClassificationFailure(
            "input ZIP uses an unsupported compression or encryption feature"
        ) from error

    if not material_records:
        _fail("archive material scripts contain no material definitions")

    material_records.sort(
        key=lambda record: (
            str(record["source"]["script_member"]).encode("utf-8"),
            int(record["source"]["span"]["byte_start"]),
            str(record["name"]).encode("utf-8"),
        )
    )
    repair_plans.sort(key=lambda plan: str(plan["repair_id"]))
    family_counts = Counter(
        str(record["classification"]["family"])
        for record in material_records
    )
    status_counts = Counter(
        str(record["classification"]["status"])
        for record in material_records
    )
    fidelity_counts = Counter(
        str(record["fidelity"]["assigned_label"])
        for record in material_records
    )
    duplicate_names: dict[str, list[str]] = {}
    for record in material_records:
        location = (
            f"{record['source']['script_member']}:"
            f"{record['source']['span']['start']['line']}"
        )
        duplicate_names.setdefault(str(record["name"]), []).append(location)
    duplicate_name_records = [
        {"locations": locations, "name": name}
        for name, locations in sorted(duplicate_names.items())
        if len(locations) > 1
    ]

    scripts = script_records
    parser_anomaly_count = sum(
        len(script["parser_anomalies"]) for script in scripts
    )
    structural_trait_counts = Counter()
    for record in material_records:
        structure = record["structure"]
        environment_modes = structure["environment_modes"]
        texture_references = structure["texture_references"]
        if "planar" in environment_modes:
            structural_trait_counts["PLANAR_ENVIRONMENT"] += 1
        if "spherical" in environment_modes:
            structural_trait_counts["SPHERICAL_ENVIRONMENT"] += 1
        if "planar" in environment_modes and any(
            str(texture).casefold() == "superficie-metalica.jpg"
            for texture in texture_references
        ):
            structural_trait_counts["PLANAR_SUPERFICIE_METALICA"] += 1
        if "planar" in environment_modes and any(
            str(texture).casefold() == "cielo.jpg"
            for texture in texture_references
        ):
            structural_trait_counts["PLANAR_CIELO"] += 1
        for anomaly in structure["anomalies"]:
            structural_trait_counts[str(anomaly["code"])] += 1
    report = {
        "$schema": SCHEMA_RELATIVE_PATH,
        "archive": {
            "bytes": path.stat().st_size,
            "declared_expanded_bytes": expanded_bytes,
            "entry_count": len(infos),
            "sha256": digest,
        },
        "classifier": {
            "algorithm": CLASSIFIER_ALGORITHM,
            "schema_sha256": hashlib.sha256(
                SCHEMA_PATH.read_bytes()
            ).hexdigest(),
            "tool_sha256": hashlib.sha256(
                Path(__file__).resolve().read_bytes()
            ).hexdigest(),
        },
        "format": REPORT_FORMAT,
        "materials": material_records,
        "repair_plans": repair_plans,
        "schema": SCHEMA_ID,
        "scripts": scripts,
        "summary": {
            "automatic_modernization_ready": not repair_plans
            and not status_counts[CLASSIFICATION_REVIEW_BLOCKED]
            and not status_counts[CLASSIFICATION_UNSUPPORTED]
            and parser_anomaly_count == 0
            and not duplicate_name_records,
            "classification_status_counts": dict(sorted(status_counts.items())),
            "duplicate_material_names": duplicate_name_records,
            "family_counts": dict(sorted(family_counts.items())),
            "fidelity_label_counts": dict(sorted(fidelity_counts.items())),
            "material_definitions": len(material_records),
            "parser_anomalies": parser_anomaly_count,
            "repair_plans_pending_review": len(repair_plans),
            "script_files": len(script_records),
            "structural_trait_counts": dict(
                sorted(structural_trait_counts.items())
            ),
        },
    }
    validate_report_contract(report)
    return report


def _atomic_write(path: Path, rendered: str) -> None:
    parent = path.parent
    if not parent.is_dir():
        _fail(f"output parent does not exist: {parent}")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(rendered)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--expect-sha256")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--pretty", action="store_true")
    parser.add_argument(
        "--require-no-review-blockers",
        action="store_true",
        help="return failure after writing a report with blocked records",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        report = classify_archive(
            arguments.archive,
            expected_sha256=arguments.expect_sha256,
        )
        rendered = canonical_json(report, pretty=arguments.pretty)
        if arguments.output is None:
            sys.stdout.write(rendered)
        else:
            _atomic_write(arguments.output, rendered)
        if (
            arguments.require_no_review_blockers
            and not report["summary"]["automatic_modernization_ready"]
        ):
            print(
                "CityWorld material classification has review or unsupported "
                "blockers",
                file=sys.stderr,
            )
            return 2
        return 0
    except (AuditFailure, OSError, ValueError) as error:
        print(
            f"CityWorld material classification failed: {error}",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
