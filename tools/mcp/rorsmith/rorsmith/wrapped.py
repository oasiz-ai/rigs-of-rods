"""Imports of the proven in-tree tools rorsmith wraps.

Nothing in this file reimplements procedure. The archive primitives, the
Ogre material-script parser, and the F3 roughness band table all come from
the checked-in tools that already encode the reviewed behaviour, so a change
there changes rorsmith in the same commit instead of silently diverging.
"""

from __future__ import annotations

import importlib
import sys
from pathlib import Path
from types import ModuleType

from .paths import RorsmithError, find_repo_root

_CACHE: dict[str, ModuleType] = {}


def _tools_dir() -> Path:
    return find_repo_root() / "tools"


def load(module_name: str) -> ModuleType:
    """Import a `tools/` module by name, with `tools/` on sys.path."""
    if module_name in _CACHE:
        return _CACHE[module_name]
    tools = str(_tools_dir())
    if tools not in sys.path:
        sys.path.insert(0, tools)
    try:
        module = importlib.import_module(module_name)
    except Exception as exc:  # pragma: no cover - surfaced as a refusal
        raise RorsmithError(
            "wrapped_tool_import_failed", f"{module_name}: {exc}"
        ) from exc
    _CACHE[module_name] = module
    return module


def classifier() -> ModuleType:
    """tools/classify_cityworld_material_families.py

    Supplies the fail-closed Ogre `.material` tokenizer/scope parser
    (`parse_script`) and the family classification contract.
    """
    return load("classify_cityworld_material_families")


def roughness_plan() -> ModuleType:
    """tools/generate_cityworld_roughness_repair_edits.py

    Supplies the F3 band table (`BAND_*`), `classify_band()`, and
    `existing_edits_by_script()` which reads the reviewed edits already
    integrated into LegacyMaterialScriptSanitizer.cpp.
    """
    return load("generate_cityworld_roughness_repair_edits")


def archive_primitives() -> ModuleType:
    """tools/apply_alexis_saber_paint.py

    Supplies the byte-exact ZIP member rewrite: `_read_entries`,
    `_member_payload`, `_authored_entry`, `_rebuild`, `compare_archives`.
    Untouched members keep their original local record verbatim, so their
    bytes are identical rather than merely equivalent.
    """
    return load("apply_alexis_saber_paint")


def ground_coverage() -> ModuleType:
    """tools/derive_cityworld_ground_coverage.py"""
    return load("derive_cityworld_ground_coverage")
