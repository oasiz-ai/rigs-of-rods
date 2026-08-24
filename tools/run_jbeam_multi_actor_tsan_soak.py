#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the continuous D0 full-runtime multi-actor ThreadSanitizer soak.

The tool performs no downloads. It packages one project-original JBeam
fixture, cycles colliding actor pairs for at least ten wall-clock minutes in
RoR-Combined, validates Ogre-Next presentation ownership, and rejects any
ThreadSanitizer diagnostic. It proves bounded concurrency/lifetime coverage,
not BeamNG.drive force parity, broad mod compatibility, or end-to-end
playability.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
from typing import Mapping, Sequence

import run_calibrated_beam_soak as support
import run_jbeam_spawn_soak as package_support


PROFILE_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_multi_actor_tsan_soak/fixture-profile.json"
)
JBEAM_RELATIVE = Path(
    "tests/fixtures/beamng/jbeam_spawn_soak/"
    "vehicles/ror_jbeam_spawn/main.jbeam"
)
SCRIPT_RELATIVE = Path(
    "resources/scripts/example_jbeam_multi_actor_tsan_soak.as"
)
JBEAM_MEMBER = "vehicles/ror_jbeam_spawn/main.jbeam"
JBEAM_ARCHIVE = "RoRJBeamMultiActorTSanSoak.zip"
SCRIPT_MEMBER = SCRIPT_RELATIVE.name
TERRAIN = "simple2.terrn2"
EXPECTED_MINIMUM_SECONDS = 600.0
EXPECTED_CYCLE_STEPS = 2000
EXPECTED_MINIMUM_STEPS = 20000
EXPECTED_MINIMUM_CYCLES = 10
EXPECTED_LLVMPipe_THREADS = "0"

START_MARKER = (
    "[RoR|D0|TSanSoak] START vehicle=ror_jbeam_spawn_fixture.jbeam "
    "target_seconds=600 cycle_steps=2000 minimum_cycles=10 "
    "minimum_total_steps=20000 actors_per_cycle=2"
)
ARM_MARKER = (
    "[RoR|D0|TSanSoak] ARMED actors=2 nodes=12 beams=32 "
    "cab_triangles=10 collision_cabs=10 hydros=2 batch=10"
)
PASS_PATTERN = re.compile(
    r"\[RoR\|D0\|TSanSoak\] PASS "
    r"elapsed_seconds=(?P<elapsed>[0-9]+(?:\.[0-9]+)?) "
    r"physics_steps=(?P<steps>[0-9]+) "
    r"completed_cycles=(?P<cycles>[0-9]+) "
    r"collision_responses=(?P<responses>[0-9]+) "
    r"actor_spawns=(?P<spawns>[0-9]+) "
    r"actor_deletes=(?P<deletes>[0-9]+)"
)
PRESENTATION_MARKERS = (
    "[RoR|RendererCombined|Startup] presentation_owner=ogre-next "
    "visible_window=true legacy_visible_fallback=false "
    "backend=ogre-next-vulkan",
    "[RoR|RendererCombined|Startup] resource_host=ogre14 "
    "visible_window=false protected=true",
    "[RoR|RendererCombined|Startup] Transport-free OgreNext "
    "session ready after authenticated bootstrap presentation",
)
FATAL_MARKERS = (
    "[RoR|D0|TSanSoak] FAIL",
    "[RoR|ModCache|JBeam] Refused",
    "[RoR|ModCache|JBeam] Rejected",
    "Validation Failed: Sampler error:",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
    "WARNING: ThreadSanitizer",
    "SUMMARY: ThreadSanitizer",
    "FATAL: ThreadSanitizer",
)
REQUIRED_TSAN_OPTIONS = {
    "exitcode": "66",
    "halt_on_error": "1",
    "history_size": "7",
    "second_deadlock_stack": "1",
}
SCRIPT_COMPILE_ERROR_PATTERN = re.compile(
    re.escape(SCRIPT_MEMBER) + r" \([0-9]+, [0-9]+\): Error = "
)


class TSanSoakFailure(RuntimeError):
    """Fail-closed diagnostic for invalid soak input, runtime, or evidence."""


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def parse_option_map(value: str) -> dict[str, str]:
    options: dict[str, str] = {}
    for token in value.split(":"):
        if not token:
            continue
        name, separator, setting = token.partition("=")
        if not separator or not name or name in options:
            raise TSanSoakFailure("TSAN_OPTIONS is not an exact option map")
        options[name] = setting
    return options


def read_profile(
    repository: Path,
) -> tuple[dict[str, object], bytes, bytes, bytes]:
    profile_path = repository / PROFILE_RELATIVE
    jbeam_path = repository / JBEAM_RELATIVE
    script_path = repository / SCRIPT_RELATIVE
    for path, label in (
        (profile_path, "fixture profile"),
        (jbeam_path, "JBeam source"),
        (script_path, "scenario script"),
    ):
        if not path.is_file() or path.is_symlink():
            raise TSanSoakFailure(f"{label} is missing or indirect: {path}")
    try:
        profile_bytes = support.canonical_lf_text(
            profile_path.read_bytes(), "fixture profile"
        )
        profile = json.loads(profile_bytes)
        jbeam = support.canonical_lf_text(
            jbeam_path.read_bytes(), "JBeam source"
        )
        script = support.canonical_lf_text(
            script_path.read_bytes(), "scenario script"
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise TSanSoakFailure("fixture inputs are not canonical JSON/text") from error
    except support.SoakFailure as error:
        raise TSanSoakFailure(str(error)) from error

    expected_keys = {
        "authorship",
        "documentationProfile",
        "execution",
        "expectedRuntime",
        "fixtureId",
        "jbeamSource",
        "license",
        "prohibitedInputs",
        "rootPart",
        "scenarioScript",
        "schema",
    }
    expected_runtime = {
        "actorsPerCycle": 2,
        "cabTrianglesPerActor": 5,
        "collisionCabsPerActor": 5,
        "cycleFixedSteps": EXPECTED_CYCLE_STEPS,
        "durationClock": "ogre-monotonic-timer",
        "fixedStepDenominator": 2000,
        "fixedStepNumerator": 1,
        "jbeamHydrosPerActor": 1,
        "minimumCompletedCycles": EXPECTED_MINIMUM_CYCLES,
        "minimumDurationSeconds": int(EXPECTED_MINIMUM_SECONDS),
        "minimumTotalFixedSteps": EXPECTED_MINIMUM_STEPS,
        "nodesPerActor": 6,
        "runtimeBeamsPerActor": 16,
    }
    if (
        not isinstance(profile, dict)
        or set(profile) != expected_keys
        or profile.get("schema") != 1
        or profile.get("fixtureId")
        != "ror-d0-full-runtime-multi-actor-tsan-soak-v1"
        or profile.get("authorship") != "original-clean-room"
        or profile.get("license") != "GPL-3.0-or-later"
        or profile.get("documentationProfile")
        != "ror-jbeam-conservative-subset-v1"
        or profile.get("execution")
        != "threadsanitizer-instrumented-ogre-next-combined-product-path"
        or profile.get("rootPart") != "ror_jbeam_spawn_fixture"
        or profile.get("expectedRuntime") != expected_runtime
        or profile.get("jbeamSource")
        != {
            "path": JBEAM_RELATIVE.as_posix(),
            "sha256": sha256_bytes(jbeam),
        }
        or profile.get("scenarioScript")
        != {
            "path": SCRIPT_RELATIVE.as_posix(),
            "sha256": sha256_bytes(script),
        }
        or profile.get("prohibitedInputs")
        != [
            "lua-execution",
            "ogre-script-execution",
            "network",
            "external-assets",
            "third-party-mod-data",
        ]
    ):
        raise TSanSoakFailure("fixture profile does not bind the exact soak")
    return profile, profile_bytes, jbeam, script


def build_command(executable: Path) -> tuple[str, ...]:
    return (
        str(executable),
        "-checkcache",
        "-map",
        TERRAIN,
        "-enter",
        "-runscript",
        SCRIPT_MEMBER,
    )


def run_audit_command(command: Sequence[str]) -> str:
    result = subprocess.run(
        list(command),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=30,
        check=False,
    )
    output = support.decode_output(result.stdout)
    if result.returncode != 0:
        raise TSanSoakFailure(
            f"instrumentation audit failed: {' '.join(command)}\n{output}"
        )
    return output


def audit_tsan_instrumentation(
    executable: Path,
    environment: Mapping[str, str],
) -> dict[str, str]:
    if sys.platform != "linux":
        raise TSanSoakFailure("ThreadSanitizer product soak is Linux-only")
    if environment.get("LP_NUM_THREADS") != EXPECTED_LLVMPipe_THREADS:
        raise TSanSoakFailure(
            "the hidden OGRE14 llvmpipe resource host must render "
            "synchronously with LP_NUM_THREADS=0"
        )
    raw_options = environment.get("TSAN_OPTIONS", "")
    options = parse_option_map(raw_options)
    for name, expected in REQUIRED_TSAN_OPTIONS.items():
        if options.get(name) != expected:
            raise TSanSoakFailure(
                f"TSAN_OPTIONS must set {name}={expected}"
            )
    if "suppressions" in options:
        raise TSanSoakFailure("TSan suppressions are prohibited in this gate")
    log_path = options.get("log_path", "")
    if not log_path or not Path(log_path).is_absolute():
        raise TSanSoakFailure("TSAN_OPTIONS must use an absolute log_path")

    readelf = shutil.which("readelf")
    nm = shutil.which("nm")
    if readelf is None or nm is None:
        raise TSanSoakFailure("readelf and nm are required for TSan audit")
    dynamic = run_audit_command((readelf, "--dynamic", str(executable)))
    symbols = run_audit_command((nm, "--dynamic", str(executable)))
    if re.search(r"Shared library: \[libtsan\.so(?:\.[0-9]+)+\]", dynamic) is None:
        raise TSanSoakFailure("RoR-Combined does not require a TSan runtime")
    if re.search(r"\bU\s+__tsan_init(?:@|$)", symbols, re.MULTILINE) is None:
        raise TSanSoakFailure("RoR-Combined has no __tsan_init reference")
    return {
        "dynamic": dynamic,
        "log_path": log_path,
        "options": raw_options,
        "symbols": symbols,
    }


def build_runtime_environment(isolated_home: Path) -> dict[str, str]:
    environment = os.environ.copy()
    environment.pop("SNAP_USER_COMMON", None)
    environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
    environment["ALSOFT_DRIVERS"] = "null"
    environment["ALSOFT_LOGLEVEL"] = "0"
    # Mesa documents zero as fully synchronous llvmpipe rendering. The hidden
    # OGRE14 resource host does not need a raster worker pool in this physics
    # and lifetime soak, while the visible Ogre-Next Vulkan renderer remains
    # the sole presentation owner. This is deliberately not a TSan suppression.
    environment["LP_NUM_THREADS"] = EXPECTED_LLVMPipe_THREADS
    return environment


def capture_runtime_diagnostics(
    layout: Mapping[str, Path],
    diagnostics: Path,
    command: Sequence[str],
    stdout: str,
    returncode: int | None,
    failure: str | None = None,
) -> None:
    """Retain live product logs even when the bounded command times out."""
    (diagnostics / "stdout.log").write_text(stdout, encoding="utf-8")
    process_result: dict[str, object] = {
        "command": list(command),
        "returncode": returncode,
    }
    if failure is not None:
        process_result["failure"] = failure
    (diagnostics / "process-result.json").write_text(
        json.dumps(process_result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    for source, destination in (
        (layout["logs"] / "RoR.log", diagnostics / "RoR.log"),
        (
            layout["logs"] / "Angelscript.log",
            diagnostics / "Angelscript.log",
        ),
    ):
        if source.is_file():
            shutil.copy2(source, destination)


def capture_sanitizer_reports(
    instrumentation: Mapping[str, str] | None,
    diagnostics: Path,
) -> tuple[Path, ...]:
    if instrumentation is None:
        return ()
    log_path = Path(instrumentation["log_path"])
    reports = tuple(
        path
        for path in log_path.parent.glob(log_path.name + "*")
        if path.is_file() and path.stat().st_size > 0
    )
    for report_path in reports:
        shutil.copy2(report_path, diagnostics / report_path.name)
    return reports


def terminate_product(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def run_product_command(
    command: Sequence[str],
    timeout: int,
    *,
    cwd: Path,
    environment: Mapping[str, str],
    script_log: Path,
    stdout_path: Path,
) -> subprocess.CompletedProcess[bytes]:
    """Run the product while failing promptly on scenario compilation errors."""
    launch = list(command)
    if not launch:
        raise support.SoakFailure("cannot run an empty product command")
    started = time.monotonic()
    with stdout_path.open("wb") as stdout_stream:
        process = subprocess.Popen(
            launch,
            cwd=str(cwd),
            env=dict(environment),
            stdout=stdout_stream,
            stderr=subprocess.STDOUT,
        )
        try:
            while process.poll() is None:
                if script_log.is_file():
                    script_text = script_log.read_text(
                        encoding="utf-8", errors="replace"
                    )
                    if SCRIPT_COMPILE_ERROR_PATTERN.search(script_text):
                        raise support.SoakFailure(
                            "AngelScript rejected the exact TSan scenario"
                        )
                if time.monotonic() - started >= timeout:
                    raise support.SoakFailure(
                        f"command exceeded {timeout} seconds: "
                        f"{' '.join(command)}"
                    )
                time.sleep(0.25)
        except BaseException:
            terminate_product(process)
            raise
        returncode = process.wait()
    return subprocess.CompletedProcess(
        launch,
        returncode,
        stdout_path.read_bytes(),
    )


def validate_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    archive_sha256: str,
) -> dict[str, float | int]:
    if returncode != 0:
        raise TSanSoakFailure(f"RoR-Combined TSan soak exited with {returncode}")
    for marker in (START_MARKER, ARM_MARKER):
        if marker not in script_log:
            raise TSanSoakFailure(f"AngelScript log missed marker: {marker}")
    for marker in (
        "[RoR|ModCache|JBeam] Mounted exact archive",
        f"archive_sha256={archive_sha256}",
        "roots=1",
        "GL_RENDERER = llvmpipe",
        *PRESENTATION_MARKERS,
    ):
        if marker not in engine_log:
            raise TSanSoakFailure(f"engine log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise TSanSoakFailure(f"runtime logged a fatal marker: {marker}")
    matches = list(PASS_PATTERN.finditer(script_log))
    if len(matches) != 1:
        raise TSanSoakFailure(
            f"expected one TSan soak PASS receipt, found {len(matches)}"
        )
    values: dict[str, float | int] = {
        "elapsed_seconds": float(matches[0].group("elapsed")),
        "physics_steps": int(matches[0].group("steps")),
        "completed_cycles": int(matches[0].group("cycles")),
        "collision_responses": int(matches[0].group("responses")),
        "actor_spawns": int(matches[0].group("spawns")),
        "actor_deletes": int(matches[0].group("deletes")),
    }
    cycles = int(values["completed_cycles"])
    if (
        float(values["elapsed_seconds"]) < EXPECTED_MINIMUM_SECONDS
        or int(values["physics_steps"]) < EXPECTED_MINIMUM_STEPS
        or cycles < EXPECTED_MINIMUM_CYCLES
        or int(values["collision_responses"]) != cycles
        or int(values["actor_spawns"]) != cycles * 2
        or int(values["actor_deletes"]) != (cycles - 1) * 2
    ):
        raise TSanSoakFailure("TSan soak PASS receipt did not perform minimum work")
    return values


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--runtime-content", type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--require-tsan", action="store_true")
    args = parser.parse_args(argv)
    if args.workers <= 0 or args.timeout <= 0:
        parser.error("--workers and --timeout must be positive")
    if args.timeout <= int(EXPECTED_MINIMUM_SECONDS):
        parser.error("--timeout must exceed the ten-minute soak duration")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    executable = args.executable.resolve()
    repository = args.repository.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file() or executable.is_symlink():
        raise TSanSoakFailure(f"executable is missing or indirect: {executable}")
    if artifact_dir.exists():
        raise TSanSoakFailure(f"artifact directory already exists: {artifact_dir}")
    artifact_dir.mkdir(parents=True)

    profile, profile_bytes, jbeam, script = read_profile(repository)
    runtime_content = (
        support.infer_runtime_content(executable)
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise TSanSoakFailure(f"runtime content is missing: {runtime_content}")
    try:
        package_support.verify_runtime_terrain(runtime_content)
    except package_support.SpawnSoakFailure as error:
        raise TSanSoakFailure(str(error)) from error

    isolated_home = artifact_dir / "work" / "tsan-soak-home"
    layout = support.runtime_layout(isolated_home, sys.platform, executable)
    for key in ("config", "logs", "mods"):
        layout[key].mkdir(parents=True, exist_ok=True)
    support.write_runtime_config(layout["config"], args.workers, True)
    try:
        archive_sha256 = package_support.write_archive(
            layout["mods"] / JBEAM_ARCHIVE,
            {JBEAM_MEMBER: jbeam},
        )
    except package_support.SpawnSoakFailure as error:
        raise TSanSoakFailure(str(error)) from error
    scripts = layout["user"] / "scripts"
    scripts.mkdir(parents=True, exist_ok=True)
    runtime_script = scripts / SCRIPT_MEMBER
    runtime_script.write_bytes(script)
    if support.sha256_file(runtime_script) != sha256_bytes(script):
        raise TSanSoakFailure("trusted runtime script staging changed bytes")

    diagnostics = artifact_dir / "diagnostics"
    diagnostics.mkdir()
    environment = build_runtime_environment(isolated_home)
    instrumentation: dict[str, str] | None = None
    if args.require_tsan:
        instrumentation = audit_tsan_instrumentation(executable, environment)
        (diagnostics / "RoR-Combined.readelf-dynamic.txt").write_text(
            instrumentation["dynamic"], encoding="utf-8"
        )
        (diagnostics / "RoR-Combined.nm-dynamic.txt").write_text(
            instrumentation["symbols"], encoding="utf-8"
        )

    command = build_command(executable)
    stdout_path = diagnostics / "stdout.log"
    try:
        completed = run_product_command(
            command,
            args.timeout,
            cwd=executable.parent,
            environment=environment,
            script_log=layout["logs"] / "Angelscript.log",
            stdout_path=stdout_path,
        )
    except support.SoakFailure as error:
        cause = error.__cause__
        captured_stdout = support.decode_output(
            getattr(cause, "stdout", None)
        )
        if not captured_stdout and stdout_path.is_file():
            captured_stdout = stdout_path.read_text(
                encoding="utf-8", errors="replace"
            )
        capture_runtime_diagnostics(
            layout,
            diagnostics,
            command,
            captured_stdout,
            None,
            str(error),
        )
        capture_sanitizer_reports(instrumentation, diagnostics)
        raise TSanSoakFailure(str(error)) from error
    stdout = support.decode_output(completed.stdout)
    capture_runtime_diagnostics(
        layout,
        diagnostics,
        command,
        stdout,
        completed.returncode,
    )

    sanitizer_reports = capture_sanitizer_reports(
        instrumentation, diagnostics
    )
    if sanitizer_reports:
        names = ", ".join(str(path) for path in sanitizer_reports)
        raise TSanSoakFailure(f"ThreadSanitizer emitted reports: {names}")
    if completed.returncode != 0:
        raise TSanSoakFailure(
            f"RoR-Combined exited before log validation with "
            f"{completed.returncode}"
        )

    try:
        engine_log = support.read_required(layout["logs"] / "RoR.log", "RoR log")
        script_log = support.read_required(
            layout["logs"] / "Angelscript.log", "AngelScript log"
        )
    except support.SoakFailure as error:
        raise TSanSoakFailure(str(error)) from error
    telemetry = validate_logs(
        completed.returncode,
        stdout,
        engine_log,
        script_log,
        archive_sha256,
    )

    report = {
        "artifact_claim": "sanitizer-evidence-not-qualified-runtime-package",
        "duration_clock": "ogre-monotonic-timer",
        "executable": str(executable),
        "executable_sha256": support.sha256_file(executable),
        "fixture_id": profile["fixtureId"],
        "fixture_profile_sha256": sha256_bytes(profile_bytes),
        "format": "ror-d0-full-runtime-tsan-soak-v1",
        "jbeam_archive_sha256": archive_sha256,
        "jbeam_source_sha256": sha256_bytes(jbeam),
        "machine": platform.machine(),
        "platform": platform.platform(),
        "presentation_ownership": {
            "legacy_visible_fallback": False,
            "presentation_owner": "ogre-next",
            "resource_host": "ogre14",
            "resource_host_protected": True,
            "resource_host_visible": False,
            "visible_render_system": "ogre-next-vulkan",
            "visible_window": True,
        },
        "hidden_resource_host_rasterizer": {
            "driver": "llvmpipe",
            "lp_num_threads": 0,
            "mode": "synchronous",
        },
        "repository_commit": support.git_output(repository, ("rev-parse", "HEAD")),
        "sanitizer": {
            "enabled": instrumentation is not None,
            "name": "thread",
            "reports": 0,
            "suppressions": False,
        },
        "scope": "continuous-clean-room-multi-actor-contact-and-lifetime-mutation",
        "script_sha256": sha256_bytes(script),
        "telemetry": telemetry,
        "workers": args.workers,
    }
    temporary = artifact_dir / "report.json.tmp"
    final = artifact_dir / "report.json"
    temporary.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, final)
    print(f"D0 full-runtime ThreadSanitizer soak passed: {final}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except TSanSoakFailure as error:
        print(f"D0 full-runtime TSan soak failed: {error}", file=sys.stderr)
        raise SystemExit(1)
