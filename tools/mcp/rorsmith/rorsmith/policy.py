"""Renderer policy facts, read from the engine sources at call time.

Every value here is parsed out of the checked-in C++ rather than copied into
Python. A band table that drifts from LegacyMaterialScriptSanitizer.cpp would
make this whole server confidently wrong, so the only safe copy is no copy.
"""

from __future__ import annotations

import functools
import math
import re
from dataclasses import dataclass
from pathlib import Path

from .paths import Layout, RorsmithError
from . import wrapped

_EXCLUSION_ARRAY = re.compile(
    r"OgreNextDemoTextureProjectionExclusionName\s*\([^)]*\)\s*noexcept\s*\{"
    r".*?names\s*=\s*\{(?P<body>.*?)\};",
    re.DOTALL,
)
_QUOTED = re.compile(r'"([a-z0-9_]+)"')


@functools.lru_cache(maxsize=4)
def refusal_tokens(policy_cpp: str) -> tuple[str, ...]:
    """The exact matte/refusal names the live census histogram uses.

    Parsed from OgreNextDemoTextureProjectionExclusionName so a new refusal
    reason in the engine appears here without a rorsmith edit.
    """
    text = Path(policy_cpp).read_text(encoding="utf-8", errors="replace")
    match = _EXCLUSION_ARRAY.search(text)
    if match is None:
        raise RorsmithError(
            "refusal_vocabulary_unreadable",
            f"{policy_cpp} no longer defines the exclusion name table",
        )
    names = tuple(_QUOTED.findall(match.group("body")))
    if len(names) < 20 or names[0] != "none":
        raise RorsmithError(
            "refusal_vocabulary_unreadable",
            f"parsed {len(names)} names, first={names[:1]}",
        )
    return names


def _int_constant(text: str, name: str) -> int | None:
    match = re.search(rf"{re.escape(name)}\s*=\s*(\d+)", text)
    return int(match.group(1)) if match else None


@dataclass(frozen=True)
class StructuralLimits:
    max_texture_units_per_pass: int
    max_passes_per_material: int
    max_detail_layers: int


@functools.lru_cache(maxsize=4)
def structural_limits(policy_h: str) -> StructuralLimits:
    text = Path(policy_h).read_text(encoding="utf-8", errors="replace")
    units = _int_constant(text, "kOgreNextDemoMaximumLegacyLayeredTextureUnits")
    passes = _int_constant(text, "kOgreNextDemoMaximumLegacyTechniquePasses")
    if units is None or passes is None:
        raise RorsmithError(
            "structural_limits_unreadable",
            f"{policy_h} no longer pins the legacy unit/pass caps",
        )
    return StructuralLimits(units, passes, 4)


@dataclass(frozen=True)
class Band:
    """One reviewed F3 roughness band."""

    name: str
    shininess: float
    specular_rgb: str

    @property
    def roughness(self) -> float:
        # The sanitizer's own identity: roughness = sqrt(2 / (S + 2)).
        return math.sqrt(2.0 / (self.shininess + 2.0))


@functools.lru_cache(maxsize=1)
def bands() -> dict[str, Band]:
    """The F3 band table, imported live from the repair-plan generator."""
    module = wrapped.roughness_plan()
    table: dict[str, Band] = {}
    for attribute in dir(module):
        if not attribute.startswith("BAND_"):
            continue
        value = getattr(module, attribute)
        if not (isinstance(value, tuple) and len(value) == 3):
            continue
        name, shininess, specular = value
        table[name] = Band(str(name), float(shininess), str(specular))
    if not table:
        raise RorsmithError(
            "band_table_unreadable",
            "generate_cityworld_roughness_repair_edits exposes no BAND_* table",
        )
    return table


def classify_band(name: str, textures: list[str]) -> str | None:
    """The generator's own keyword/manual classification. Never re-derived."""
    return wrapped.roughness_plan().classify_band(name, list(textures))


_PIN = re.compile(
    r"constexpr\s+char\s+(?P<symbol>k\w+ArchiveSha256)\[\]\s*=\s*\n?\s*"
    r'"(?P<digest>[0-9a-f]{64})";'
)
_PIN_BYTES = re.compile(
    r"constexpr\s+std::uint64_t\s*\n?\s*(?P<symbol>k\w+ArchiveBytes)\s*=\s*"
    r"(?P<value>\d+)ULL;"
)


def compatibility_pins(layout: Layout) -> dict[str, dict[str, object]]:
    """The archive digest/size pins the runtime authenticates against."""
    text = layout.compatibility_plan_h.read_text(encoding="utf-8")
    pins: dict[str, dict[str, object]] = {}
    for match in _PIN.finditer(text):
        pins[match.group("symbol")] = {"sha256": match.group("digest")}
    for match in _PIN_BYTES.finditer(text):
        stem = match.group("symbol")[: -len("ArchiveBytes")]
        for symbol in pins:
            if symbol[: -len("ArchiveSha256")] == stem:
                pins[symbol]["bytes"] = int(match.group("value"))
    return pins


#: Archives whose overlay digest the runtime authenticates. Rewriting one of
#: these without repinning makes the runtime fall back to the unauthenticated
#: mount and every road capture fails closed.
PINNED_ARCHIVES = {
    "CityWorldNextLocalOverlay.zip": "kCityWorldNextLocalOverlayArchiveSha256",
    "CityWorld.zip": "kCityWorldLegacyMaterialCompatibilityArchiveSha256",
}


def repin_text(text: str, symbol: str, digest: str, size: int) -> str:
    """Rewrite one digest/size pin pair in LegacyMaterialCompatibilityPlan.h."""
    stem = symbol[: -len("ArchiveSha256")]
    digest_pattern = re.compile(
        rf'(constexpr\s+char\s+{re.escape(symbol)}\[\]\s*=\s*\n?\s*")[0-9a-f]{{64}}(")'
    )
    updated, count = digest_pattern.subn(rf"\g<1>{digest}\g<2>", text)
    if count != 1:
        raise RorsmithError(
            "repin_target_not_found", f"{symbol} matched {count} times"
        )
    bytes_pattern = re.compile(
        rf"(constexpr\s+std::uint64_t\s*\n?\s*{re.escape(stem)}ArchiveBytes\s*=\s*)"
        rf"\d+(ULL;)"
    )
    updated, count = bytes_pattern.subn(rf"\g<1>{size}\g<2>", updated)
    if count != 1:
        raise RorsmithError(
            "repin_target_not_found", f"{stem}ArchiveBytes matched {count} times"
        )
    return updated
