#!/usr/bin/env python3
"""Generate reviewed specular/shininess repair-plan edits for CityWorld.

Foundation F3 of the fidelity roadmap: nearly every legacy CityWorld material
authors no ``specular``/shininess, so the combined runtime's projection
``roughness_factor = sqrt(2 / (shininess + 2))`` collapses to 1.0 (fully
rough) for ~90% of admitted materials. This tool assigns a physically
plausible per-family shininess to every eligible authored pass and emits the
exact, line-anchored ``LegacyMaterialScriptEdit`` C++ table entries the
fail-closed sanitizer plan mechanism consumes
(source/main/resources/LegacyMaterialScriptSanitizer.cpp).

Design constraints honoured here:

* Line-anchored, fail-closed: every emitted edit replaces a pass-open ``{``
  that appears exactly once on its line, so the runtime anchor check refuses
  any drifted script byte-for-byte.
* Idempotent: pass-open lines already targeted by an edit in the sanitizer
  source (parsed from the checked-in C++) are skipped, so a re-run against an
  integrated sanitizer emits zero new edits.
* Minimal perturbation: the injected block is a single ``specular R G B A S``
  directive at the top of the pass. Authored directives later in the pass
  override it, so a pass that already authors specular state keeps it.
* Planar dead-zone floor (roadmap addendum A1): HlmsPbs squares perceptual
  roughness before the GGX NDF, so flat architectural surfaces keep
  perceptual roughness >= ~0.30. Families that would map below the floor are
  clamped and annotated.
* Out of scope: glass/windows (handled by the dedicated glass work), the
  curated asia.material rows (their repair plan is pinned by
  kOgreNextDemoCuratedCityWorldAppliedEditCount), metals-as-metallic
  (Stage 8), and passes that author ``lighting off`` (projected UNLIT).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import classify_cityworld_material_families as classifier

EXPECTED_ARCHIVE_SHA256 = (
    "ebeac2f0204f25ca1955f29ca1583b2afa4517a3a848feb1db203814acac2ef3"
)

# Scripts never touched by this tool.
SKIPPED_SCRIPTS = {
    # Curated CityWorld rows pin the exact three-edit repair plan
    # (kOgreNextDemoCuratedCityWorldAppliedEditCount); adding edits would
    # refuse every curated projection.
    "asia.material",
}

# roughness = sqrt(2 / (shininess + 2))  =>  shininess = 2 / r^2 - 2.
# The palette is quantized so review and verification deal with a handful of
# reviewed bands instead of hundreds of bespoke scalars.
BAND_POLISHED = ("polished", 20.0, "0.25 0.25 0.25")  # r 0.302 (floored)
BAND_CERAMIC = ("ceramic_wet", 12.0, "0.10 0.10 0.10")  # r 0.378
BAND_GLAZED_FACADE = ("glazed_facade", 10.0, "0.08 0.08 0.08")  # r 0.408
BAND_GLOSS_PAINT = ("gloss_paint", 10.0, "0.08 0.08 0.08")  # r 0.408
BAND_SEMIGLOSS = ("semigloss", 6.0, "0.06 0.06 0.06")  # r 0.500
BAND_SATIN = ("satin", 4.0, "0.05 0.05 0.05")  # r 0.577
BAND_WOOD = ("wood", 2.0, "0.04 0.04 0.04")  # r 0.707
BAND_MASONRY = ("masonry", 1.2, "0.04 0.04 0.04")  # r 0.791
BAND_ASPHALT = ("asphalt_stucco", 0.8, "0.03 0.03 0.03")  # r 0.845
BAND_FOLIAGE = ("foliage", 0.5, "0.02 0.02 0.02")  # r 0.894

# Keyword tables are matched against the lowercase material name (last path
# segment - legacy names carry `script/TEXFACE/` prefixes that would pollute
# keyword matching) and every lowercase texture reference. First match wins;
# order encodes priority.
#
# Materials whose textures/names read as glass become the `glazed_facade`
# band when their technique is OPAQUE: legacy CityWorld "window" content is
# almost entirely baked curtain-wall/window-grid facade textures on opaque
# walls, and those are exactly the glazed facades the fidelity program needs
# to catch the sun. Actually transparent glass (a `scene_blend` pass 0) is
# skipped entirely - the dedicated glass work owns transmission surfaces.
# 0.408 perceptual roughness intentionally sits at the shipped glass work's
# broad-lobe stand-in (~0.40) and above the planar dead-zone floor.
GLASS_KEYWORDS = (
    "glass",
    "window",
    "ventana",
    "cristal",
    "vidrio",
)

FAMILY_RULES: tuple[tuple[str, tuple[str, ...]], ...] = (
    # Polished trim / chrome-look dielectric gloss (floored at r 0.30).
    (
        "polished",
        (
            "chrome",
            "marble",
            "marmol",
            "granite",
            "granito",
            "mirror",
            "espejo",
            "metalica",
            "metallic",
            "aluminio",
            "aluminum",
            "stainless",
        ),
    ),
    # Wet-look / ceramic / tile.
    (
        "ceramic_wet",
        (
            "ceramic",
            "ceramica",
            "tile",
            "azulejo",
            "porcelain",
            "toldo",
        ),
    ),
    # Glossy printed signage, traffic lights, painted sheet metal.
    (
        "gloss_paint",
        (
            "sign",
            "senal",
            "letrero",
            "anuncio",
            "logo",
            "trafficlight",
            "semaforo",
            "stoplight",
            "billboard",
            "cartel",
            "placa",
        ),
    ),
    # Semi-gloss architectural paint / plastic street furniture.
    (
        "semigloss",
        (
            "plastic",
            "plastico",
            "umbrella",
            "chair",
            "table",
            "booth",
            "kiosk",
            "phone",
            "lamp",
            "farol",
            "pole",
            "poste",
            "rail",
            "barandal",
            "fence",
            "reja",
            "barrier",
            "guardrail",
            "bote",
            "trash",
            "basura",
        ),
    ),
    # Satin: generic painted facades, stucco-painted walls, vehicles' paint
    # stays out (no vehicle scripts here).
    (
        "satin",
        (
            "paint",
            "pintura",
            "metal",
            "steel",
            "iron",
            "fierro",
            "door",
            "puerta",
            "porton",
            "cornice",
            "cornisa",
            "roof",
            "techo",
            "awning",
        ),
    ),
    # Wood.
    (
        "wood",
        (
            "wood",
            "madera",
            "timber",
            "plank",
            "banca",
        ),
    ),
    # Masonry: brick and finished concrete.
    (
        "masonry",
        (
            "brick",
            "ladrillo",
            "tabique",
            "concrete",
            "concreto",
            "cemento",
            "cement",
            "crete",
            "block",
            "stone",
            "piedra",
            "cantera",
            "wall",
            "muro",
            "pared",
            "facade",
            "fachada",
            "build",
            "edificio",
            "casa",
            "tienda",
            "plaza",
            "iglesia",
            "colegio",
            "escuela",
            "hospital",
            "hotel",
            "banco",
            "torre",
            "skyscraper",
            "scraper",
        ),
    ),
    # Asphalt / stucco / raw ground surfaces.
    (
        "asphalt_stucco",
        (
            "asphalt",
            "asfalto",
            "road",
            "carretera",
            "calle",
            "pista",
            "runway",
            "airport",
            "street",
            "sidewalk",
            "banqueta",
            "curb",
            "pavement",
            "pav",
            "stucco",
            "estuco",
            "dirt",
            "tierra",
            "ground",
            "suelo",
            "terreno",
            "sand",
            "arena",
            "gravel",
            "grava",
            "rock",
            "roca",
        ),
    ),
    # Foliage.
    (
        "foliage",
        (
            "grass",
            "pasto",
            "cesped",
            "leaf",
            "hoja",
            "tree",
            "arbol",
            "arbush",
            "robles",
            "pinos",
            "ficus",
            "palm",
            "palma",
            "bush",
            "arbusto",
            "plant",
            "planta",
            "hedge",
            "seto",
            "vege",
            "flower",
            "flor",
            "jardin",
        ),
    ),
)

BANDS = {
    band[0]: band
    for band in (
        BAND_POLISHED,
        BAND_CERAMIC,
        BAND_GLAZED_FACADE,
        BAND_GLOSS_PAINT,
        BAND_SEMIGLOSS,
        BAND_SATIN,
        BAND_WOOD,
        BAND_MASONRY,
        BAND_ASPHALT,
        BAND_FOLIAGE,
    )
}

# Materials whose semantics the keyword tables cannot see (reviewed by hand
# while reading the scripts). Maps exact material name -> band name, or None
# to skip the material entirely.
MANUAL_ASSIGNMENTS: dict[str, str | None] = {
    # Invisible collision-proxy material.
    "ModeloColisionante": None,
    # NeoQueretaro authors this material twice; the reviewed plan removes the
    # duplicate copy (lines 1772-1784) and the review invariant keeps the
    # surviving copy (lines 1698-1710) byte-intact so that removal stays a
    # pure de-duplication. One material stays fully rough for that guarantee.
    "concretorojo": None,
    # Sky dome: a specular sun glint on the cloud layer would be wrong.
    "NQ-Sky-Clouds": None,
    # Flat water plane: broad-lobe stand-in at the planar floor.
    "NQ-water": "polished",
    # Painted road-closed sign, not road surface.
    "streetfurniture/TEXFACE/roadclosed.tga": "gloss_paint",
}

# The default band for a lit, textured surface with no recognizable keyword:
# generic weathered architectural masonry.
DEFAULT_BAND = "masonry"


@dataclass
class PassPlanEntry:
    script: str
    material: str
    band: str
    shininess: float
    directive_args: str
    line: int
    clamped: bool


@dataclass
class ScriptReport:
    member: str
    sha256: str
    crlf: bool
    total_materials: int = 0
    covered_passes: int = 0
    skipped_excluded_materials: int = 0
    skipped_transparent_techniques: int = 0
    skipped_unlit_passes: int = 0
    skipped_authored_techniques: int = 0
    skipped_mixed_techniques: int = 0
    skipped_existing_specular_techniques: int = 0
    skipped_anchor_techniques: int = 0
    skipped_program_passes: int = 0
    entries: list[PassPlanEntry] = field(default_factory=list)


def _lower_haystack(name: str, textures: list[str]) -> str:
    # Material names carry `script/TEXFACE/` prefixes; match only the last
    # segment so `dneroads/.../26-grass.dds` reads as grass, not road.
    short_name = name.rsplit("/", 1)[-1]
    return " ".join([short_name.lower()] + [t.lower() for t in textures])


def classify_band(name: str, textures: list[str]) -> str | None:
    if name in MANUAL_ASSIGNMENTS:
        return MANUAL_ASSIGNMENTS[name]
    hay = _lower_haystack(name, textures)
    for keyword in GLASS_KEYWORDS:
        if keyword in hay:
            return "glazed_facade"
    for band_name, keywords in FAMILY_RULES:
        for keyword in keywords:
            if keyword in hay:
                return band_name
    return DEFAULT_BAND


def existing_edits_by_script(sanitizer_cpp: Path) -> dict[str, dict[int, str]]:
    """Parse the checked-in sanitizer for edits already reviewed per script.

    Returns {script_member: {1-based line: edit text}} for every edit in
    every CityWorld plan, so this generator never targets a line an existing
    reviewed edit already owns and can recognize passes whose specular state
    an existing plan already authors.
    """
    text = sanitizer_cpp.read_text(encoding="utf-8")
    # Resolve named replacement constants (e.g. NEOQ20_FACADE_PASS_OPEN) so
    # "does this edit author specular state" sees through the indirection.
    constants = {
        m.group(1): m.group(2)
        for m in re.finditer(
            r"const char (\w+)\[\] =\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+);",
            text,
        )
    }
    array_edits: dict[str, dict[int, str]] = {}
    pattern = re.compile(
        r"const LegacyMaterialScriptEdit (\w+)\[\] = \{(.*?)\};",
        re.DOTALL,
    )
    entry_head = re.compile(
        r"\{LegacyMaterialScriptEditKind::\w+,\s*(\d+)U,"
    )
    for match in pattern.finditer(text):
        body = match.group(2)
        heads = list(entry_head.finditer(body))
        edits: dict[int, str] = {}
        for index, head in enumerate(heads):
            end = heads[index + 1].start() if index + 1 < len(heads) else len(
                body
            )
            edit_text = body[head.start():end]
            for name, value in constants.items():
                if name in edit_text:
                    edit_text += " " + value
            edits[int(head.group(1))] = edit_text
        array_edits[match.group(1)] = edits
    plan_pattern = re.compile(
        r"\{kCityWorldLegacyMaterialCompatibilityArchiveSha256,\s*"
        r'"([^"]+)",\s*"[0-9a-f]{64}",\s*(\w+),',
        re.DOTALL,
    )
    result: dict[str, dict[int, str]] = {}
    for match in plan_pattern.finditer(text):
        member, array_name = match.group(1), match.group(2)
        result[member] = dict(array_edits.get(array_name, {}))
    return result


@dataclass
class PassFacts:
    scope: classifier.Scope
    lighting_off: bool
    has_specular: bool
    has_program: bool
    has_scene_blend: bool
    open_line: int
    close_line: int


def pass_facts(pass_scope: classifier.Scope) -> PassFacts:
    lighting_off = False
    has_specular = False
    has_program = False
    has_scene_blend = False
    for directive in pass_scope.directives:
        if directive.name == "lighting" and directive.arguments and (
            directive.arguments[0].lower() == "off"
        ):
            lighting_off = True
        if directive.name == "specular":
            has_specular = True
        if directive.name in ("scene_blend", "separate_scene_blend"):
            has_scene_blend = True
        if directive.name in (
            "vertex_program_ref",
            "fragment_program_ref",
            "shadow_caster_vertex_program_ref",
            "shadow_receiver_vertex_program_ref",
        ):
            has_program = True
    for child in pass_scope.children:
        if child.kind in ("vertex_program_ref", "fragment_program_ref"):
            has_program = True
    return PassFacts(
        scope=pass_scope,
        lighting_off=lighting_off,
        has_specular=has_specular,
        has_program=has_program,
        has_scene_blend=has_scene_blend,
        open_line=pass_scope.open_token.start_location.line,
        close_line=pass_scope.close_token.start_location.line,
    )


def material_textures(material: classifier.Scope) -> list[str]:
    textures: list[str] = []
    for scope in classifier._flat_scopes(material):
        if scope.kind == "texture_unit":
            for directive in scope.directives:
                if directive.name in ("texture", "anim_texture",
                                      "cubic_texture") and directive.arguments:
                    textures.append(directive.arguments[0])
    return textures


def build_reports(
    archive: Path, sanitizer_cpp: Path
) -> tuple[list[ScriptReport], dict[str, str]]:
    existing = existing_edits_by_script(sanitizer_cpp)
    reports: list[ScriptReport] = []
    band_by_material: dict[str, str] = {}
    with zipfile.ZipFile(archive) as bundle:
        members = sorted(
            info.filename
            for info in bundle.infolist()
            if info.filename.lower().endswith(".material")
        )
        for member in members:
            payload = bundle.read(member)
            sha256 = hashlib.sha256(payload).hexdigest()
            crlf = b"\r\n" in payload
            report = ScriptReport(member=member, sha256=sha256, crlf=crlf)
            reports.append(report)
            if member in SKIPPED_SCRIPTS:
                continue
            try:
                parsed = classifier.parse_script(payload, member)
            except classifier.AuditFailure:
                continue
            lines = payload.decode("utf-8", errors="replace").split("\n")
            script_edits = existing.get(member, {})
            claimed: set[int] = set(script_edits)
            for material in parsed.root.children:
                if material.kind != "material":
                    continue
                report.total_materials += 1
                name = material.arguments[0] if material.arguments else ""
                textures = material_textures(material)
                band_name = classify_band(name, textures)
                if band_name is None:
                    report.skipped_excluded_materials += 1
                    continue
                band = BANDS[band_name]
                material_covered = False
                for technique in material.children:
                    if technique.kind != "technique":
                        continue
                    passes = [
                        pass_facts(child)
                        for child in technique.children
                        if child.kind == "pass"
                    ]
                    if not passes:
                        continue
                    # A transparent pass 0 means real glass / water / an
                    # additive light: transmission surfaces belong to the
                    # dedicated glass work, additive lights gain nothing.
                    if passes[0].has_scene_blend:
                        report.skipped_transparent_techniques += 1
                        continue
                    # An existing reviewed plan that already authors specular
                    # state inside this technique owns the values - but the
                    # trailing-overlay admission (HasAddedLightOnlyShading-
                    # Response) compares base and overlay specular/shininess
                    # for EQUALITY, so a technique the existing plan covered
                    # only partially (pass 0 injected, glow overlay left at
                    # zero) is completed here with the exact same directive.
                    technique_span = range(
                        technique.open_token.start_location.line,
                        technique.close_token.start_location.line + 1,
                    )
                    existing_specs = {
                        m.group(1).strip()
                        for line in technique_span
                        if line in script_edits
                        for m in [re.search(
                            r"specular ([0-9. ]+)", script_edits[line])]
                        if m is not None
                    }
                    match_existing: str | None = None
                    if existing_specs:
                        uncovered = [
                            facts
                            for facts in passes
                            if not facts.lighting_off and
                            not facts.has_program and
                            not facts.has_specular and
                            facts.open_line not in script_edits
                        ]
                        if len(existing_specs) == 1 and uncovered:
                            match_existing = existing_specs.pop()
                        else:
                            report.skipped_existing_specular_techniques += 1
                            continue
                    candidates = [
                        facts
                        for facts in passes
                        if not facts.lighting_off and not facts.has_program
                        and (match_existing is None or
                             facts.open_line not in script_edits)
                    ]
                    report.skipped_unlit_passes += sum(
                        1 for facts in passes if facts.lighting_off
                    )
                    report.skipped_program_passes += sum(
                        1 for facts in passes if facts.has_program
                    )
                    if not candidates:
                        continue
                    authored = [f for f in candidates if f.has_specular]
                    if authored:
                        if len(authored) == len(candidates):
                            report.skipped_authored_techniques += 1
                        else:
                            # Mixed authored/unauthored specular within one
                            # technique: hands off. Injecting only some
                            # passes could flip the trailing-overlay
                            # equal-shading admission
                            # (HasAddedLightOnlyShadingResponse) either way.
                            report.skipped_mixed_techniques += 1
                        continue

                    # All-or-nothing: the trailing-overlay admission compares
                    # base and overlay specular/shininess for equality, so
                    # either every lit program-free pass in the technique
                    # receives the identical directive or none does.
                    def anchor_ok(facts: PassFacts) -> bool:
                        if facts.open_line in claimed:
                            return False
                        if facts.open_line - 1 >= len(lines):
                            return False
                        content = lines[facts.open_line - 1].rstrip("\r")
                        return content.count("{") == 1 and "}" not in content

                    if not all(anchor_ok(f) for f in candidates):
                        report.skipped_anchor_techniques += 1
                        continue
                    if match_existing is not None:
                        entry_band = "match_existing"
                        entry_shininess = float(match_existing.split()[-1])
                        directive_args = match_existing
                    else:
                        entry_band = band[0]
                        entry_shininess = band[1]
                        directive_args = (
                            f"{band[2]} 1 {format_shininess(band[1])}"
                        )
                    for facts in candidates:
                        claimed.add(facts.open_line)
                        report.covered_passes += 1
                        report.entries.append(
                            PassPlanEntry(
                                script=member,
                                material=name,
                                band=entry_band,
                                shininess=entry_shininess,
                                directive_args=directive_args,
                                line=facts.open_line,
                                clamped=entry_band == "polished",
                            )
                        )
                    material_covered = True
                if material_covered:
                    band_by_material[name] = band_name
    return reports, band_by_material


def format_shininess(value: float) -> str:
    if value == int(value):
        return str(int(value))
    return f"{value:g}"


def cpp_identifier(member: str) -> str:
    stem = re.sub(r"[^A-Za-z0-9]+", "_", member.rsplit(".", 1)[0]).upper()
    return f"CITYWORLD_{stem}_ROUGHNESS_EDITS"


def emit_cpp(reports: list[ScriptReport]) -> str:
    out: list[str] = []
    out.append(
        "// Generated by tools/generate_cityworld_roughness_repair_edits.py\n"
        "// (Foundation F3). Each edit injects a family-banded"
        " `specular R G B A S`\n"
        "// as the first directive of an authored pass; authored directives"
        " later in\n"
        "// the pass override it. shininess S lowers to perceptual roughness\n"
        "// sqrt(2/(S+2)). The `polished` band is clamped to S=20"
        " (roughness 0.30):\n"
        "// the planar dead-zone floor - HlmsPbs squares perceptual roughness"
        " before\n"
        "// the GGX NDF, so a narrower lobe on flat architectural geometry"
        " is\n"
        "// all-or-nothing per facet and reads as pure black from almost"
        " every\n"
        "// azimuth.\n"
    )
    for report in reports:
        if not report.entries:
            continue
        newline = "\\r\\n" if report.crlf else "\\n"
        out.append(f"const LegacyMaterialScriptEdit {cpp_identifier(report.member)}[] = {{")
        rows = []
        for entry in sorted(report.entries, key=lambda e: e.line):
            comment = (
                f"    // {entry.material}: {entry.band}"
                f" -> roughness {(2.0 / (entry.shininess + 2.0)) ** 0.5:.3f}"
                + (" (planar floor clamp)" if entry.clamped else "")
            )
            replacement = (
                f"\"{{{newline}"
                f"          specular {entry.directive_args}\""
            )
            rows.append(
                comment + "\n"
                f"    {{LegacyMaterialScriptEditKind::REPLACE_TOKEN_ON_LINE,\n"
                f"     {entry.line}U, \"{{\",\n"
                f"     {replacement}}},"
            )
        body = "\n".join(rows)
        if body.endswith(","):
            body = body[:-1]
        out.append(body)
        out.append("};\n")
    return "\n".join(out)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument(
        "--sanitizer-cpp",
        type=Path,
        default=Path(__file__).resolve().parent.parent
        / "source/main/resources/LegacyMaterialScriptSanitizer.cpp",
    )
    parser.add_argument("--expect-sha256", default=EXPECTED_ARCHIVE_SHA256)
    parser.add_argument("--census-only", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json-report", type=Path)
    args = parser.parse_args(argv)

    digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
    if digest != args.expect_sha256:
        print(
            f"fatal: archive sha256 {digest} != expected {args.expect_sha256}",
            file=sys.stderr,
        )
        return 1

    reports, band_by_material = build_reports(
        args.archive, args.sanitizer_cpp
    )

    total_new = sum(len(r.entries) for r in reports)
    print(f"archive: {args.archive} sha256={digest}")
    print(f"scripts: {len(reports)}  new pass edits: {total_new}")
    for report in reports:
        print(
            f"  {report.member}: materials={report.total_materials} "
            f"new_edits={len(report.entries)} "
            f"excluded={report.skipped_excluded_materials} "
            f"transparent={report.skipped_transparent_techniques} "
            f"unlit_passes={report.skipped_unlit_passes} "
            f"authored={report.skipped_authored_techniques} "
            f"mixed={report.skipped_mixed_techniques} "
            f"existing_specular={report.skipped_existing_specular_techniques} "
            f"program_passes={report.skipped_program_passes} "
            f"anchor={report.skipped_anchor_techniques} crlf={report.crlf}"
        )
    from collections import Counter

    band_counts = Counter(
        entry.band for report in reports for entry in report.entries
    )
    print("band histogram (new edits):", dict(band_counts))

    if args.json_report:
        args.json_report.write_text(
            json.dumps(
                {
                    "archive_sha256": digest,
                    "new_edit_count": total_new,
                    "bands": {
                        report.member: [
                            {
                                "material": e.material,
                                "band": e.band,
                                "shininess": e.shininess,
                                "line": e.line,
                            }
                            for e in report.entries
                        ]
                        for report in reports
                        if report.entries
                    },
                },
                indent=1,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
    if args.census_only:
        return 0
    rendered = emit_cpp(reports)
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
        print(f"wrote {args.output}")
    else:
        print(rendered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
