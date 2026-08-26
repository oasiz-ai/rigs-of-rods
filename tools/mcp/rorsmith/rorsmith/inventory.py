"""Material inventory and inspection.

The parse and the family/eligibility classification come from
tools/classify_cityworld_material_families.py; the roughness band comes from
tools/generate_cityworld_roughness_repair_edits.py. This module adds the
admission prediction and the honest labelling that separates a static
prediction from what the runtime census actually observed.
"""

from __future__ import annotations

import functools
import re
from pathlib import Path

from .paths import Layout, RorsmithError
from . import policy, wrapped

_PASS_EQUATION = re.compile(r"^PASS_(?P<index>\d+):(?P<body>.*)$")


@functools.lru_cache(maxsize=8)
def _classified(archive_path: str) -> dict:
    classifier = wrapped.classifier()
    try:
        return classifier.classify_archive(Path(archive_path))
    except Exception as exc:
        raise RorsmithError("archive_classification_failed", str(exc)) from exc


def _records(report: dict) -> list[dict]:
    for key in ("materials", "records", "material_records"):
        value = report.get(key)
        if isinstance(value, list):
            return value
    raise RorsmithError(
        "classification_shape_unexpected",
        f"no material list in report keys {sorted(report)}",
    )


def predict_admission(record: dict, limits: policy.StructuralLimits) -> dict[str, object]:
    """Predict the projection outcome using the engine's own refusal names.

    This is a STATIC prediction from script structure alone. It cannot see
    texture payloads, sampler state, or the managed-material authority, so it
    never claims a material projects - only that nothing in the script
    structure refuses it. verify_live is the ground truth.
    """
    structure = record.get("structure") or {}
    classification = record.get("classification") or {}
    reasons: list[str] = []

    # OgreNextDemoRequiresMatte(unit_count, has_program) decides whether the
    # section enters the denominator at all. Zero texture units and no program
    # is not a matte and not a projection - it is not a candidate, and the
    # census counts it nowhere. Reporting it as "matte" would be a lie.
    if not structure.get("has_authored_gpu_program") and not int(
        structure.get("texture_unit_count") or 0
    ):
        return {
            "source": "static_prediction",
            "predicted": "not_a_candidate",
            "refusal_tokens": [],
            "note": (
                "no texture units and no authored program: "
                "OgreNextDemoRequiresMatte() excludes this section from the "
                "candidate denominator entirely; it takes the untextured "
                "fallback path and appears in no census bucket"
            ),
        }

    if structure.get("has_authored_gpu_program"):
        reasons.append("material_authored_program_unsupported")
    if structure.get("has_cube_texture_source"):
        reasons.append("cube_texture")
    pass_count = int(structure.get("pass_count") or 0)
    if pass_count > limits.max_passes_per_material:
        reasons.append("material_multi_pass_unsupported")
    unit_count = int(structure.get("texture_unit_count") or 0)
    if unit_count > limits.max_texture_units_per_pass:
        reasons.append("material_texture_unit_layer_unsupported")
    if structure.get("anomalies"):
        reasons.append("material_structure_unsupported")

    # Only whole trailing PASSES can be overlay refusals. `PASS_0_UNIT_n`
    # entries are texture units inside pass 0 and must not be read as one.
    for equation in (str(e) for e in (structure.get("layer_equations") or [])):
        match = _PASS_EQUATION.match(equation)
        if match is None or int(match.group("index")) == 0:
            continue
        body = match.group("body").upper()
        if "ADD" in body:
            reasons.append("material_additive_overlay_pass_unsupported")
        elif "BLEND" in body or "MODULATE" in body:
            reasons.append("material_blended_overlay_pass_unsupported")

    status = str(classification.get("status") or "")
    if status and status != "ELIGIBLE" and not reasons:
        # The classifier refused it but structure alone names no specific
        # engine token; say so with the generic one rather than inventing.
        reasons.append("material_structure_unsupported")

    unique = list(dict.fromkeys(reasons))
    return {
        "source": "static_prediction",
        "predicted": "matte" if unique else "projection_candidate",
        "refusal_tokens": unique,
        "note": (
            "structure-only prediction; texture payload, sampler state and "
            "managed-material authority are only observable via verify_live"
        ),
    }


def _summarise(record: dict, limits: policy.StructuralLimits) -> dict[str, object]:
    structure = record.get("structure") or {}
    classification = record.get("classification") or {}
    name = str(record.get("name") or "")
    textures = [str(t) for t in (structure.get("texture_references") or [])]
    band_name = policy.classify_band(name, textures)
    band = policy.bands().get(band_name) if band_name else None
    return {
        "name": name,
        "script_member": (record.get("source") or {}).get("script_member"),
        "family": classification.get("family"),
        "eligibility": classification.get("status"),
        "fidelity_label": (record.get("fidelity") or {}).get("assigned_label"),
        "textures": textures,
        "pass_count": structure.get("pass_count"),
        "technique_count": structure.get("technique_count"),
        "texture_unit_count": structure.get("texture_unit_count"),
        "layer_equations": structure.get("layer_equations"),
        "inherited_from": structure.get("inherited_from"),
        "roughness_band": band_name,
        "roughness": round(band.roughness, 4) if band else None,
        "band_shininess": band.shininess if band else None,
        "band_specular": band.specular_rgb if band else None,
        "admission": predict_admission(record, limits),
        "material_id": record.get("material_id"),
    }


def list_materials(
    layout: Layout,
    archive: str,
    name_filter: str | None = None,
    band_filter: str | None = None,
    limit: int = 200,
) -> dict[str, object]:
    path = layout.resolve_archive(archive)
    report = _classified(str(path))
    limits = policy.structural_limits(str(layout.private_policy_h))
    rows = [_summarise(record, limits) for record in _records(report)]
    if name_filter:
        needle = name_filter.casefold()
        rows = [
            row
            for row in rows
            if needle in str(row["name"]).casefold()
            or any(needle in t.casefold() for t in row["textures"])
        ]
    if band_filter:
        rows = [row for row in rows if row["roughness_band"] == band_filter]
    total = len(rows)
    truncated = total > limit
    band_histogram: dict[str, int] = {}
    refusal_histogram: dict[str, int] = {}
    for row in rows:
        key = str(row["roughness_band"])
        band_histogram[key] = band_histogram.get(key, 0) + 1
        for token in row["admission"]["refusal_tokens"]:
            refusal_histogram[token] = refusal_histogram.get(token, 0) + 1
    return {
        "archive": str(path),
        "archive_sha256": report.get("archive", {}).get("sha256")
        if isinstance(report.get("archive"), dict)
        else report.get("archive_sha256"),
        "script_files": (report.get("summary") or {}).get("script_files"),
        "materials_total": (report.get("summary") or {}).get("material_definitions"),
        "materials_matched": total,
        "band_histogram": dict(sorted(band_histogram.items())),
        "predicted_refusal_histogram": dict(sorted(refusal_histogram.items())),
        "truncated": truncated,
        "materials": rows[:limit],
    }


def inspect_material(layout: Layout, archive: str, name: str) -> dict[str, object]:
    path = layout.resolve_archive(archive)
    report = _classified(str(path))
    limits = policy.structural_limits(str(layout.private_policy_h))
    matches = [r for r in _records(report) if str(r.get("name")) == name]
    if not matches:
        loose = [
            r for r in _records(report) if name.casefold() in str(r.get("name")).casefold()
        ]
        raise RorsmithError(
            "material_not_found",
            f"'{name}' is not in {path.name}"
            + (
                "; closest names: "
                + ", ".join(str(r.get("name")) for r in loose[:5])
                if loose
                else ""
            ),
        )
    record = matches[0]
    detail = _summarise(record, limits)
    structure = record.get("structure") or {}
    band_name = detail["roughness_band"]
    band = policy.bands().get(str(band_name)) if band_name else None

    sanitizer_edits = existing_sanitizer_edits(layout)
    member = str((record.get("source") or {}).get("script_member") or "")
    detail["sanitizer"] = {
        "script_member": member,
        "reviewed_edits_in_this_script": len(sanitizer_edits.get(member, {})),
        "band_assignment": band_name,
        "would_inject": (
            None
            if band is None
            else f"specular {band.specular_rgb} 1.0 {band.shininess}"
        ),
        "injection_rule": (
            "pass-open `specular R G B A shininess`; all-or-nothing per "
            "technique (base and overlay must agree or the material silently "
            "demotes); transparent pass 0, `lighting off` passes and curated "
            "asia.material are skipped"
        ),
    }
    detail["source_span"] = (record.get("source") or {}).get("span")
    detail["structural_anomalies"] = structure.get("anomalies")
    detail["fidelity"] = record.get("fidelity")
    detail["repair_plan_ids"] = record.get("repair_plan_ids")
    detail["refusal_vocabulary"] = list(
        policy.refusal_tokens(str(layout.private_policy_cpp))
    )
    return detail


@functools.lru_cache(maxsize=4)
def _existing_edits(sanitizer_cpp: str) -> dict[str, dict[int, str]]:
    module = wrapped.roughness_plan()
    return module.existing_edits_by_script(Path(sanitizer_cpp))


def existing_sanitizer_edits(layout: Layout) -> dict[str, dict[int, str]]:
    return _existing_edits(str(layout.sanitizer_cpp))
