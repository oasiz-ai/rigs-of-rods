"""verify_live: run the combined runtime in an isolated home and read the census.

This is the trust anchor. Every other rorsmith tool makes a claim; this one
goes and looks.

ISOLATION IS NOT OPTIONAL. The user plays on
/Users/beshoyhanna/RigsOfRods/logs/RoR.log and every session truncates it at
startup, so a session launched here MUST bind ROR_D0_SCENE_HOME to a private
tree and MUST read the log inside that tree. The log path is verified to be
inside the isolated home and to have been created by this launch before a
single number is parsed; otherwise we would be reporting another agent's
census as our own.
"""

from __future__ import annotations

import os
import re
import shutil
import signal
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

from .paths import Layout, RorsmithError

#: The runtime refuses the isolated home unless the exact drawable extent is
#: supplied alongside it (main.cpp pairs the two).
DEFAULT_EXTENT = (1280, 720)

_MATERIAL_SOURCE = re.compile(r"\[RoR\|OgreNextDemo\|MaterialSource\] (?P<body>.*)")
_SECTION_CENSUS = re.compile(
    r"material=(?P<name>.+?) projected=(?P<projected>\d+) "
    r"matte=(?P<matte>\d+) reason=(?P<reason>\w+)"
)
_NATIVE_ROUGHNESS = re.compile(
    r"\[RoR\|OgreNext\|NativeRoughnessCensus\] name=(?P<name>\S+) "
    r"roughness=(?P<roughness>[0-9.]+) workflow=(?P<workflow>\w+)"
)
_FIELD = re.compile(r"(?P<key>[a-z0-9_]+)=(?P<value>\d+)")
_REASONS = re.compile(r"matte_by_reason=\[(?P<body>[^\]]*)\]")


def find_binary(layout: Layout, requested: str | None) -> Path:
    if requested:
        path = Path(requested).expanduser()
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RorsmithError("binary_not_executable", str(path))
        return path.resolve()
    candidates = sorted(
        layout.repo_root.glob("build-*/bin/RoR-Combined"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        raise RorsmithError(
            "combined_binary_not_found",
            f"no build-*/bin/RoR-Combined under {layout.repo_root}",
        )
    return candidates[0].resolve()


@dataclass
class IsolatedHome:
    root: Path
    user_dir: Path
    log_path: Path

    def as_dict(self) -> dict[str, str]:
        return {
            "home": str(self.root),
            "user_dir": str(self.user_dir),
            "log": str(self.log_path),
        }


def stage_home(layout: Layout, base: Path) -> IsolatedHome:
    """Build a private RoR user tree: linked mods, copied config, empty cache.

    The cache is deliberately left empty: bundle paths inside a cloned cache
    are absolute, and a stale one makes terrain dependencies ambiguous.
    """
    user = base / "RigsOfRods"
    (user / "logs").mkdir(parents=True, exist_ok=True)
    (user / "cache").mkdir(parents=True, exist_ok=True)
    (user / "screenshots").mkdir(parents=True, exist_ok=True)
    mods = user / "mods"
    mods.mkdir(parents=True, exist_ok=True)
    for source_dir in layout.mods_dirs:
        for archive in source_dir.glob("*.zip"):
            target = mods / archive.name
            if not target.exists():
                target.symlink_to(archive)
    config = user / "config"
    config.mkdir(parents=True, exist_ok=True)
    source_config = layout.user_dir / "config"
    if source_config.is_dir():
        for entry in source_config.iterdir():
            if entry.is_file() and not (config / entry.name).exists():
                shutil.copy2(entry, config / entry.name)
    return IsolatedHome(base, user, user / "logs" / "RoR.log")


def _parse_counters(line: str) -> dict[str, object]:
    fields: dict[str, object] = {}
    for match in _FIELD.finditer(line):
        fields.setdefault(match.group("key"), int(match.group("value")))
    reasons: dict[str, int] = {}
    matches = _REASONS.findall(line)
    if matches:
        # The line carries capture then lifetime; the last is the lifetime one.
        for pair in matches[-1].split(","):
            if "=" not in pair:
                continue
            key, _, value = pair.partition("=")
            try:
                count = int(value)
            except ValueError:
                continue
            if count:
                reasons[key] = count
    fields["matte_by_reason"] = reasons
    return fields


def parse_log(text: str) -> dict[str, object]:
    counters: dict[str, object] = {}
    for match in _MATERIAL_SOURCE.finditer(text):
        counters = _parse_counters(match.group("body"))
    sections = [
        {
            "material": m.group("name"),
            "projected": int(m.group("projected")),
            "matte": int(m.group("matte")),
            "reason": m.group("reason"),
        }
        for m in _SECTION_CENSUS.finditer(text)
    ]
    roughness = [
        {
            "name": m.group("name"),
            "roughness": float(m.group("roughness")),
            "workflow": m.group("workflow"),
        }
        for m in _NATIVE_ROUGHNESS.finditer(text)
    ]
    bins: dict[str, int] = {}
    for row in roughness:
        key = f"{min(9, int(row['roughness'] * 10)) / 10:.1f}"
        bins[key] = bins.get(key, 0) + 1
    return {
        "counters": {
            key: counters.get(key)
            for key in (
                "candidate_sections",
                "projected_sections",
                "matte_excluded_sections",
                "distinct_eligible_texture_keys",
                "distinct_projected_texture_keys",
                "distinct_matte_only_texture_keys",
                "source_decode_rejections",
                "source_exclusions",
                "projections",
                "active_projections",
                "committed_new_projections",
            )
        },
        "matte_by_reason": counters.get("matte_by_reason", {}),
        "material_sections": sections,
        "native_roughness_samples": len(roughness),
        "native_roughness_bins": dict(sorted(bins.items())),
        "capture_rejected": text.count("capture_rejected"),
        "mounted_exact_primary": text.count("Mounted exact primary"),
    }


def verify_live(
    layout: Layout,
    terrain: str = "CityWorldNextLocalOverlay.terrn2",
    truck: str | None = None,
    binary: str | None = None,
    timeout_seconds: float = 420.0,
    width: int = DEFAULT_EXTENT[0],
    height: int = DEFAULT_EXTENT[1],
    keep_home: bool = False,
    material_filter: str | None = None,
) -> dict[str, object]:
    executable = find_binary(layout, binary)
    base = Path(tempfile.mkdtemp(prefix="rorsmith-live-"))
    home = stage_home(layout, base)

    # Belt and braces: the log we will read must be inside our private tree
    # and must not exist yet. Anything else means we are about to read a
    # session that is not ours.
    if layout.user_dir in home.log_path.parents:
        raise RorsmithError(
            "isolation_violation",
            f"{home.log_path} is inside the user's own RoR tree",
        )
    if home.log_path.exists():
        home.log_path.unlink()

    environment = os.environ.copy()
    environment["ROR_D0_SCENE_HOME"] = str(home.root)
    environment["ROR_D0_EXACT_WINDOW_EXTENT"] = f"{width}x{height}"
    environment["ROR_SCENE_CENSUS"] = "1"
    environment["ALSOFT_DRIVERS"] = "null"
    environment.setdefault("ALSOFT_LOGLEVEL", "0")

    command = [
        str(executable),
        "-ApplePersistenceIgnoreState",
        "YES",
        "-checkcache",
        "-map",
        terrain,
    ]
    if truck:
        command.extend(["-truck", truck, "-enter"])

    started = time.time()
    process = subprocess.Popen(
        command,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=str(executable.parent),
        start_new_session=True,
    )
    stderr_tail: list[str] = []
    census_seen = False
    exit_code: int | None = None
    try:
        deadline = started + timeout_seconds
        while time.time() < deadline:
            exit_code = process.poll()
            text = ""
            if home.log_path.exists():
                text = home.log_path.read_text(encoding="utf-8", errors="replace")
            if "[RoR|OgreNextDemo|MaterialSectionCensus]" in text and (
                "[RoR|OgreNextDemo|MaterialSource]" in text
            ):
                census_seen = True
                break
            if exit_code is not None:
                break
            time.sleep(1.0)
    finally:
        if process.poll() is None:
            try:
                os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                process.terminate()
            try:
                process.wait(timeout=25)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(os.getpgid(process.pid), signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    process.kill()
        if process.stdout is not None:
            try:
                raw = process.stdout.read() or b""
            except ValueError:
                raw = b""
            stderr_tail = raw.decode("utf-8", "replace").splitlines()

    log_text = ""
    if home.log_path.exists():
        log_text = home.log_path.read_text(encoding="utf-8", errors="replace")
    # stderr carries the NativeRoughnessCensus lines; the log carries the rest.
    parsed = parse_log(log_text + "\n" + "\n".join(stderr_tail))

    if material_filter:
        needle = material_filter.casefold()
        parsed["material_sections"] = [
            row
            for row in parsed["material_sections"]
            if needle in row["material"].casefold()
        ]
    else:
        parsed["material_sections"] = parsed["material_sections"][:80]

    result: dict[str, object] = {
        "binary": str(executable),
        "terrain": terrain,
        "truck": truck,
        "isolated": home.as_dict(),
        "census_observed": census_seen,
        "elapsed_seconds": round(time.time() - started, 1),
        "runtime_exit_code": exit_code,
        "log_bytes": len(log_text),
        **parsed,
    }
    if not census_seen:
        result["refusal"] = {
            "reason": "census_not_observed",
            "detail": (
                "the session did not emit both MaterialSectionCensus and "
                "MaterialSource before the timeout; the numbers below are "
                "incomplete and must not be quoted as a verification"
            ),
            "stderr_tail": stderr_tail[-40:],
        }
    if not keep_home:
        shutil.rmtree(base, ignore_errors=True)
        result["isolated"] = {**home.as_dict(), "removed": True}
    return result
