#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prove a deterministic input record survives a real save/load boundary.

The gate performs two isolated native runs. The first records one exact input
stream, writes a schema-3 checkpoint at completed step 120, and continues to
step 240. The second loads that checkpoint through the product message path
and continues the retained record to the same terminal step. It authenticates
both state traces with ``ror_state_trace --inspect`` and requires byte-exact
input artifacts plus equal terminal physics records.

The tool performs no downloads and never uses the caller's ordinary RoR home.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Iterable, Mapping, Sequence
import zipfile


CONTENT_COMMIT = "34fefdd126784bf87b068fc283f812525d159dd7"
SCENARIO_ID = 2026082001
TARGET_ID = 2026082001001
SAVE_STEP = 120
FINAL_STEP = 240
TERRAIN = "simple2.terrn2"
VEHICLE = "95bbUID-agoras.truck"
CHECKPOINT = "d0_input_checkpoint.sav"
SAVE_SCRIPT = "example_deterministic_input_save_checkpoint.as"
RESUME_SCRIPT = "example_deterministic_input_resume_checkpoint.as"
FIXTURE_PREFIXES = ("agora/", "simple2-terrain/")

SAVE_SCRIPT_MARKERS = (
    "[RoR|D0|InputSave] START scenario=2026082001",
    "[RoR|D0|InputSave] ARMED first_step=0 throttle=0.375 gear=0 batch=1",
    "[RoR|D0|InputSave] CHECKPOINT file=d0_input_checkpoint.sav completed_step=120",
    "[RoR|D0|InputSave] PASS completed_step=240",
)
RESUME_SCRIPT_MARKERS = (
    "[RoR|D0|InputResume] START checkpoint=d0_input_checkpoint.sav",
    "[RoR|D0|InputResume] ARMED first_step=120 mode=record batch=1",
    "[RoR|D0|InputResume] PASS completed_step=240",
)
SAVE_ENGINE_MARKERS = (
    "[RoR|Determinism] Recording authenticated input",
    "scenario=2026082001, target=2026082001001, first-step=0, limit=240",
    "with 240 authenticated fixed-step records",
)
RESUME_ENGINE_MARKERS = (
    "[RoR|Determinism] Restored record input continuation at fixed step 120 after 120 authenticated records",
    "with 240 authenticated fixed-step records",
)
FATAL_MARKERS = (
    "[RoR|D0|InputSave] FAIL",
    "[RoR|D0|InputResume] FAIL",
    "Refusing input runtime",
    "Rejected deterministic input savegame continuation",
    "Invalid deterministic input continuation",
    "State trace snapshot failed",
    "State trace append failed",
    "could not be finished",
    "Validation Failed: Sampler error:",
    "GL_INVALID_",
    "RenderingAPIException",
    "OGRE EXCEPTION",
    "EXC_BAD_ACCESS",
    "Segmentation fault",
)
BASE64URL_NO_PADDING = re.compile(r"^[A-Za-z0-9_-]+$")


class ResumeFailure(RuntimeError):
    """Fail-closed diagnostic for the save/resume runtime gate."""


def decode_output(payload: bytes | str | None) -> str:
    if payload is None:
        return ""
    if isinstance(payload, str):
        return payload
    return payload.decode("utf-8", errors="replace")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_command(
    command: Sequence[str],
    timeout: int,
    *,
    cwd: Path | None = None,
    environment: Mapping[str, str] | None = None,
) -> subprocess.CompletedProcess[bytes]:
    try:
        return subprocess.run(
            list(command),
            cwd=None if cwd is None else str(cwd),
            env=None if environment is None else dict(environment),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise ResumeFailure(
            f"command exceeded {timeout} seconds: {' '.join(command)}"
        ) from exc


def git_output(repository: Path, arguments: Sequence[str]) -> str:
    result = run_command(("git", "-C", str(repository), *arguments), 30)
    output = decode_output(result.stdout)
    if result.returncode != 0:
        raise ResumeFailure(
            f"git {' '.join(arguments)} failed in {repository}: {output}"
        )
    return output.strip()


def verify_repository_content(repository: Path) -> tuple[Path, tuple[str, ...]]:
    content = repository / "content"
    commit = git_output(content, ("rev-parse", "HEAD"))
    if commit != CONTENT_COMMIT:
        raise ResumeFailure(
            f"content commit drift: expected {CONTENT_COMMIT}, got {commit}"
        )
    tracked = tuple(
        sorted(
            path
            for path in git_output(content, ("ls-files", "-z")).split("\0")
            if path.startswith(FIXTURE_PREFIXES)
        )
    )
    for required in (
        "agora/95bbUID-agoras.truck",
        "simple2-terrain/simple2.terrn2",
    ):
        if required not in tracked:
            raise ResumeFailure(f"pinned fixture is missing {required}")
    if not tracked:
        raise ResumeFailure("pinned save/resume fixture inventory is empty")
    return content, tracked


def verify_runtime_content(
    source_content: Path,
    runtime_content: Path,
    tracked_paths: Iterable[str],
) -> None:
    members_by_prefix: dict[str, set[str]] = {}
    tracked = tuple(tracked_paths)
    for relative in tracked:
        prefix, separator, member = relative.partition("/")
        if not separator or not prefix or not member:
            raise ResumeFailure(f"invalid fixture path: {relative}")
        members_by_prefix.setdefault(prefix, set()).add(member)

    archives: dict[str, zipfile.ZipFile] = {}
    try:
        for prefix, expected in members_by_prefix.items():
            archive_path = runtime_content / f"{prefix}.zip"
            if not archive_path.is_file():
                continue
            try:
                archive = zipfile.ZipFile(archive_path, "r")
                actual = {
                    name for name in archive.namelist()
                    if name and not name.endswith("/")
                }
            except (OSError, zipfile.BadZipFile) as exc:
                raise ResumeFailure(
                    f"runtime fixture archive is invalid: {archive_path}"
                ) from exc
            if actual != expected:
                archive.close()
                raise ResumeFailure(
                    f"runtime archive inventory differs for {prefix}: "
                    f"missing={sorted(expected - actual)}, "
                    f"unexpected={sorted(actual - expected)}"
                )
            archives[prefix] = archive

        for relative in tracked:
            source = source_content / relative
            runtime = runtime_content / relative
            if not source.is_file():
                raise ResumeFailure(f"tracked fixture is not a file: {source}")
            if runtime.is_file():
                size = runtime.stat().st_size
                digest = sha256_file(runtime)
            else:
                prefix, _, member = relative.partition("/")
                archive = archives.get(prefix)
                if archive is None:
                    raise ResumeFailure(
                        f"runtime fixture file or archive is missing: {relative}"
                    )
                try:
                    payload = archive.read(member)
                except KeyError as exc:
                    raise ResumeFailure(
                        f"runtime fixture member is missing: {relative}"
                    ) from exc
                size = len(payload)
                digest = hashlib.sha256(payload).hexdigest()
            if source.stat().st_size != size or sha256_file(source) != digest:
                raise ResumeFailure(
                    f"runtime fixture differs from pinned source: {relative}"
                )
    finally:
        for archive in archives.values():
            archive.close()


def runtime_layout(isolated_home: Path, executable: Path) -> tuple[Path, Path]:
    if (executable.parent / "config").exists():
        raise ResumeFailure(
            "portable executable config would escape the isolated-home gate"
        )
    if sys.platform == "darwin":
        marker = ".app/Contents/MacOS/"
        if marker in executable.as_posix():
            user = (
                isolated_home
                / "Library"
                / "Application Support"
                / "Rigs of Rods"
            )
            logs = isolated_home / "Library" / "Logs" / "Rigs of Rods"
            return user, logs
        user = isolated_home / "RigsOfRods"
        return user, user / "logs"
    if os.name == "nt":
        user = isolated_home / "My Games" / "Rigs of Rods"
        return user, user / "logs"
    user = isolated_home / ".rigsofrods"
    return user, user / "logs"


def write_runtime_config(user_directory: Path, workers: int, purge: bool) -> Path:
    config = user_directory / "config"
    config.mkdir(parents=True, exist_ok=True)
    path = config / "RoR.cfg"
    path.write_text(
        "\n".join(
            (
                "; Generated by tools/run_deterministic_savegame_resume.py",
                "app_config_long_names=false",
                f"app_num_workers={workers}",
                "app_async_physics=false",
                "app_disable_online_api=true",
                "app_force_cache_update=" + ("true" if purge else "false"),
                "audio_master_volume=0",
                "gfx_fps_limit=0",
                "gfx_shadow_type=No shadows (fastest)",
                "gfx_sky_mode=Sandstorm (fastest)",
                "gfx_water_mode=None (fastest)",
                "sim_gearbox_mode=Fully Manual: sequential shift",
                "sim_spawn_running=true",
                "",
            )
        ),
        encoding="utf-8",
    )
    (config / "ogre.cfg").write_text(
        "\n".join(
            (
                "Render System=OpenGL 3+ Rendering Subsystem",
                "",
                "[OpenGL 3+ Rendering Subsystem]",
                "Colour Depth=32",
                "Content Scaling Factor=1",
                "Debug Layer=Off",
                "Display Frequency=N/A",
                "FSAA= 0",
                "Full Screen=No",
                "Reversed Z-Buffer=No",
                "Separate Shader Objects=Yes",
                "VSync=No",
                "VSync Interval=1",
                "Video Mode=1280 x 720",
                "sRGB Gamma Conversion=No",
                "",
            )
        ),
        encoding="utf-8",
    )
    return path


def build_command(executable: Path, script: str, initial: bool) -> tuple[str, ...]:
    command = [str(executable)]
    if sys.platform == "darwin":
        command.extend(("-ApplePersistenceIgnoreState", "YES"))
    if initial:
        command.extend(("-map", TERRAIN, "-truck", VEHICLE, "-enter"))
    command.extend(("-runscript", script))
    return tuple(command)


def prepare_logs(log_directory: Path) -> tuple[Path, Path]:
    log_directory.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.rortrace", "*.rorinput"):
        for artifact in log_directory.glob(pattern):
            artifact.unlink()
    engine = log_directory / "RoR.log"
    script = log_directory / "Angelscript.log"
    for path in (engine, script):
        try:
            path.unlink()
        except FileNotFoundError:
            pass
    return engine, script


def read_required(path: Path, label: str) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError as exc:
        raise ResumeFailure(f"{label} was not created: {path}") from exc


def read_json_object(path: Path, label: str) -> dict[str, object]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ResumeFailure(f"{label} is not strict JSON: {path}") from exc
    if not isinstance(payload, dict):
        raise ResumeFailure(f"{label} is not an object: {path}")
    return payload


def autosave_physics_projection(payload: Mapping[str, object]) -> dict[str, object]:
    actors = payload.get("actors")
    if not isinstance(actors, list):
        raise ResumeFailure("autosave actors are not an array")
    projected_actors = []
    exact_actor_fields = (
        "filename",
        "sim_state",
        "physics_paused",
        "deterministic_seed",
        "physics_step",
        "engine_update_step",
        "physics_origin",
        "deterministic_runtime_flags_v1",
        "nodes",
        "beams",
        "calibrated_beam_state_v1",
        "jbeam_hydro_state_v1",
    )
    for actor in actors:
        if not isinstance(actor, dict):
            raise ResumeFailure("autosave actor is not an object")
        projected_actors.append(
            {field: actor[field] for field in exact_actor_fields if field in actor}
        )
    return {
        "completed_physics_steps": payload.get("completed_physics_steps"),
        "physics_paused": payload.get("physics_paused"),
        "actors": projected_actors,
    }


def first_json_difference(
    left: object,
    right: object,
    path: str = "$",
) -> dict[str, object] | None:
    if isinstance(left, dict) and isinstance(right, dict):
        left_keys = set(left)
        right_keys = set(right)
        if left_keys != right_keys:
            return {
                "path": path,
                "kind": "object_keys",
                "left_only": sorted(left_keys - right_keys),
                "right_only": sorted(right_keys - left_keys),
            }
        for key in sorted(left_keys):
            difference = first_json_difference(
                left[key], right[key], f"{path}.{key}"
            )
            if difference is not None:
                return difference
        return None
    if isinstance(left, list) and isinstance(right, list):
        if len(left) != len(right):
            return {
                "path": path,
                "kind": "array_length",
                "left": len(left),
                "right": len(right),
            }
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            difference = first_json_difference(
                left_item, right_item, f"{path}[{index}]"
            )
            if difference is not None:
                return difference
        return None
    if left != right or type(left) is not type(right):
        return {
            "path": path,
            "kind": "value",
            "left": left,
            "right": right,
        }
    return None


def format_github_error(message: str) -> str:
    escaped = (
        message.replace("%", "%25")
        .replace("\r", "%0D")
        .replace("\n", "%0A")
    )
    return f"::error title=Deterministic save-resume gate::{escaped}"


def find_single(log_directory: Path, pattern: str, label: str) -> Path:
    matches = sorted(log_directory.glob(pattern))
    if len(matches) != 1:
        raise ResumeFailure(
            f"expected exactly one {label}, found {len(matches)} in "
            f"{log_directory}"
        )
    if matches[0].stat().st_size == 0:
        raise ResumeFailure(f"{label} is empty: {matches[0]}")
    return matches[0]


def validate_logs(
    returncode: int,
    stdout: str,
    engine_log: str,
    script_log: str,
    *,
    resumed: bool,
) -> None:
    if returncode != 0:
        raise ResumeFailure(f"RoR exited with {returncode}")
    for marker in (
        RESUME_SCRIPT_MARKERS if resumed else SAVE_SCRIPT_MARKERS
    ):
        if marker not in script_log:
            raise ResumeFailure(f"AngelScript log missed marker: {marker}")
    for marker in (
        RESUME_ENGINE_MARKERS if resumed else SAVE_ENGINE_MARKERS
    ):
        if marker not in engine_log:
            raise ResumeFailure(f"engine log missed marker: {marker}")
    combined = "\n".join((stdout, engine_log, script_log))
    for marker in FATAL_MARKERS:
        if marker in combined:
            raise ResumeFailure(f"runtime logged a fatal marker: {marker}")


def validate_checkpoint(path: Path) -> dict[str, object]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ResumeFailure(f"checkpoint is not strict JSON: {path}") from exc
    if not isinstance(payload, dict) or payload.get("format_version") != 3:
        raise ResumeFailure("checkpoint format_version is not exactly 3")
    if payload.get("terrain_name") != TERRAIN:
        raise ResumeFailure("checkpoint terrain identity differs")
    if payload.get("completed_physics_steps") != SAVE_STEP:
        raise ResumeFailure("checkpoint fixed-step cursor differs")
    if payload.get("physics_paused") is not False:
        raise ResumeFailure("checkpoint did not preserve running state")
    continuation = payload.get("deterministic_input_continuation_v1")
    if (
        not isinstance(continuation, str)
        or not continuation
        or BASE64URL_NO_PADDING.fullmatch(continuation) is None
    ):
        raise ResumeFailure("checkpoint continuation is not canonical base64url")
    actors = payload.get("actors")
    if not isinstance(actors, list) or len(actors) != 1:
        raise ResumeFailure("checkpoint must contain exactly one local actor")
    actor = actors[0]
    if not isinstance(actor, dict):
        raise ResumeFailure("checkpoint actor is not an object")
    if actor.get("player_actor") is not True:
        raise ResumeFailure("checkpoint actor is not the exact player")
    if actor.get("physics_step") != SAVE_STEP:
        raise ResumeFailure("checkpoint player step differs")
    origin = actor.get("physics_origin")
    if (
        not isinstance(origin, list)
        or len(origin) != 3
        or any(
            isinstance(component, bool)
            or not isinstance(component, (int, float))
            or not math.isfinite(component)
            for component in origin
        )
    ):
        raise ResumeFailure("checkpoint physics origin is invalid")
    runtime_flags = actor.get("deterministic_runtime_flags_v1")
    if (
        not isinstance(runtime_flags, dict)
        or set(runtime_flags) != {
            "update_physics",
            "collision_relevant",
            "ongoing_reset",
        }
        or any(not isinstance(value, bool) for value in runtime_flags.values())
    ):
        raise ResumeFailure("checkpoint deterministic runtime flags are invalid")
    solver = actor.get("deterministic_solver_state_v1")
    if not isinstance(solver, dict) or set(solver) != {
        "wheels",
        "wheel_differentials",
        "axle_differentials",
        "intra_collision_cadence",
        "inter_collision_cadence",
        "actor",
    }:
        raise ResumeFailure("checkpoint deterministic solver state is invalid")

    def finite_number(value: object) -> bool:
        return (
            not isinstance(value, bool)
            and isinstance(value, (int, float))
            and math.isfinite(value)
        )

    wheels = solver.get("wheels")
    if (
        not isinstance(wheels, list)
        or not wheels
        or any(
            not isinstance(row, list)
            or len(row) != 8
            or any(not finite_number(value) for value in row)
            for row in wheels
        )
    ):
        raise ResumeFailure("checkpoint deterministic wheel state is invalid")
    for field in ("wheel_differentials", "axle_differentials"):
        values = solver.get(field)
        if not isinstance(values, list) or any(
            not finite_number(value) for value in values
        ):
            raise ResumeFailure("checkpoint differential state is invalid")
    for field in ("intra_collision_cadence", "inter_collision_cadence"):
        values = solver.get(field)
        if not isinstance(values, list) or any(
            not isinstance(row, list)
            or len(row) != 2
            or any(isinstance(value, bool) or not isinstance(value, int) for value in row)
            for row in values
        ):
            raise ResumeFailure("checkpoint collision cadence is invalid")
    solver_actor = solver.get("actor")
    if not isinstance(solver_actor, dict) or set(solver_actor) != {
        "fusedrag",
        "sleep_counter",
        "stabilizer_shock_sleep",
        "stabilizer_shock_ratio",
        "stabilizer_shock_request",
        "tc_timer",
        "tc_pulse_state",
        "alb_timer",
        "alb_pulse_state",
        "anim_previous_crank",
    }:
        raise ResumeFailure("checkpoint deterministic actor solver state is invalid")
    fusedrag = solver_actor.get("fusedrag")
    if (
        not isinstance(fusedrag, list)
        or len(fusedrag) != 3
        or any(not finite_number(value) for value in fusedrag)
        or any(
            not finite_number(solver_actor.get(field))
            for field in (
                "sleep_counter",
                "stabilizer_shock_sleep",
                "stabilizer_shock_ratio",
                "tc_timer",
                "alb_timer",
                "anim_previous_crank",
            )
        )
        or isinstance(solver_actor.get("stabilizer_shock_request"), bool)
        or not isinstance(solver_actor.get("stabilizer_shock_request"), int)
        or not isinstance(solver_actor.get("tc_pulse_state"), bool)
        or not isinstance(solver_actor.get("alb_pulse_state"), bool)
    ):
        raise ResumeFailure("checkpoint deterministic actor solver values are invalid")
    nodes = actor.get("nodes")
    if (
        not isinstance(nodes, list)
        or not nodes
        or any(
            not isinstance(node, list)
            or len(node) < 29
            or any(
                isinstance(component, bool)
                or not isinstance(component, (int, float))
                or not math.isfinite(component)
                for component in node[9:15] + node[17:29]
            )
            or not isinstance(node[15], bool)
            or not isinstance(node[16], bool)
            for node in nodes
        )
    ):
        raise ResumeFailure("checkpoint node-local state is invalid")
    beams = actor.get("beams")
    if (
        not isinstance(beams, list)
        or not beams
        or any(
            not isinstance(beam, list)
            or len(beam) < 10
            or isinstance(beam[9], bool)
            or not isinstance(beam[9], (int, float))
            or not math.isfinite(beam[9])
            for beam in beams
        )
    ):
        raise ResumeFailure("checkpoint beam stress state is invalid")
    filename = actor.get("filename")
    if not isinstance(filename, str) or not filename.endswith(":" + VEHICLE):
        raise ResumeFailure("checkpoint player content identity differs")
    return payload


def inspect_trace(trace_tool: Path, trace: Path, timeout: int) -> dict[str, object]:
    result = run_command((str(trace_tool), "--inspect", str(trace)), timeout)
    output = decode_output(result.stdout)
    try:
        payload = json.loads(output)
    except json.JSONDecodeError as exc:
        raise ResumeFailure(
            f"state-trace inspector emitted invalid JSON: {output}"
        ) from exc
    if result.returncode != 0:
        raise ResumeFailure(f"state-trace inspection failed: {output}")
    return validate_inspection(payload)


def validate_inspection(payload: object) -> dict[str, object]:
    if not isinstance(payload, dict):
        raise ResumeFailure("state-trace inspection is not an object")
    if payload.get("format") != "ror-d0-state-trace-inspection-v2":
        raise ResumeFailure("state-trace inspection format differs")
    if payload.get("status") != "valid":
        raise ResumeFailure("state-trace inspection is not valid")
    metadata = payload.get("metadata")
    final = payload.get("final_step")
    if not isinstance(metadata, dict) or not isinstance(final, dict):
        raise ResumeFailure("state-trace inspection omitted evidence")
    if metadata.get("scenario_id") != SCENARIO_ID:
        raise ResumeFailure("state trace scenario identity differs")
    if metadata.get("physics_step_numerator") != 1 or metadata.get(
        "physics_step_denominator"
    ) != 2000:
        raise ResumeFailure("state trace is not on the 2 kHz clock")
    if payload.get("has_final_step") is not True:
        raise ResumeFailure("state trace has no terminal record")
    if final.get("physics_step") != FINAL_STEP - 1:
        raise ResumeFailure("state trace terminal cursor differs")
    digest = final.get("state_digest")
    if (
        not isinstance(digest, str)
        or re.fullmatch(r"[0-9a-f]{64}", digest) is None
    ):
        raise ResumeFailure("state trace terminal digest is invalid")
    return payload


def validate_trace_span(payload: Mapping[str, object], *, resumed: bool) -> None:
    metadata = payload["metadata"]
    if not isinstance(metadata, dict):
        raise ResumeFailure("state trace metadata is not an object")
    expected_first = SAVE_STEP if resumed else 0
    expected_count = FINAL_STEP - expected_first
    if metadata.get("first_physics_step") != expected_first:
        raise ResumeFailure("state trace first step differs")
    if payload.get("step_count") != expected_count:
        raise ResumeFailure("state trace record count differs")


def validate_final_equivalence(
    baseline: Mapping[str, object],
    resumed: Mapping[str, object],
) -> None:
    left = baseline.get("final_step")
    right = resumed.get("final_step")
    if not isinstance(left, dict) or not isinstance(right, dict):
        raise ResumeFailure("terminal state evidence is absent")
    fields = ("physics_step", "actor_count", "contact_count", "state_digest")
    if any(left.get(field) != right.get(field) for field in fields):
        raise ResumeFailure("resumed terminal physics state diverged")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--trace-tool", required=True, type=Path)
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--runtime-content", type=Path)
    parser.add_argument("--artifact-dir", required=True, type=Path)
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args(argv)
    if args.workers <= 0:
        parser.error("--workers must be positive")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    executable = args.executable.resolve()
    trace_tool = args.trace_tool.resolve()
    repository = args.repository.resolve()
    artifact_dir = args.artifact_dir.resolve()
    if not executable.is_file():
        raise ResumeFailure(f"RoR executable does not exist: {executable}")
    if not trace_tool.is_file():
        raise ResumeFailure(f"state-trace tool does not exist: {trace_tool}")
    if artifact_dir.exists():
        raise ResumeFailure(
            f"artifact directory already exists: {artifact_dir}"
        )
    artifact_dir.mkdir(parents=True)

    source_content, tracked = verify_repository_content(repository)
    runtime_content = (
        executable.parent / "content"
        if args.runtime_content is None
        else args.runtime_content.resolve()
    )
    if not runtime_content.is_dir():
        raise ResumeFailure(f"runtime content is missing: {runtime_content}")
    verify_runtime_content(source_content, runtime_content, tracked)

    isolated_home = artifact_dir / "work" / "runtime-home"
    user_directory, log_directory = runtime_layout(isolated_home, executable)
    save_directory = user_directory / "savegames"
    diagnostics = artifact_dir / "diagnostics"
    artifacts = artifact_dir / "artifacts"
    diagnostics.mkdir(parents=True)
    artifacts.mkdir(parents=True)
    environment = os.environ.copy()
    environment["ROR_D0_SCENE_HOME"] = str(isolated_home)
    environment["ALSOFT_DRIVERS"] = "null"
    environment["ALSOFT_LOGLEVEL"] = "0"

    observations: dict[str, dict[str, object]] = {}
    for label, script, initial in (
        ("record", SAVE_SCRIPT, True),
        ("resume", RESUME_SCRIPT, False),
    ):
        write_runtime_config(user_directory, args.workers, initial)
        engine_path, script_path = prepare_logs(log_directory)
        autosave_path = save_directory / "autosave.sav"
        try:
            autosave_path.unlink()
        except FileNotFoundError:
            pass
        completed = run_command(
            build_command(executable, script, initial),
            args.timeout,
            cwd=executable.parent,
            environment=environment,
        )
        stdout = decode_output(completed.stdout)
        engine_log = read_required(engine_path, "RoR engine log")
        script_log = read_required(script_path, "AngelScript log")
        validate_logs(
            completed.returncode,
            stdout,
            engine_log,
            script_log,
            resumed=not initial,
        )

        trace = find_single(log_directory, "*.rortrace", "state trace")
        input_trace = find_single(
            log_directory, "*.rorinput", "authenticated input trace"
        )
        inspection = inspect_trace(trace_tool, trace, args.timeout)
        validate_trace_span(inspection, resumed=not initial)
        copied_trace = artifacts / f"{label}.rortrace"
        copied_input = artifacts / f"{label}.rorinput"
        copied_autosave = artifacts / f"{label}.autosave.sav"
        shutil.copy2(trace, copied_trace)
        shutil.copy2(input_trace, copied_input)
        autosave = read_json_object(autosave_path, f"{label} autosave")
        shutil.copy2(autosave_path, copied_autosave)
        (diagnostics / f"{label}.stdout").write_text(stdout, encoding="utf-8")
        (diagnostics / f"{label}.RoR.log").write_text(
            engine_log, encoding="utf-8"
        )
        (diagnostics / f"{label}.Angelscript.log").write_text(
            script_log, encoding="utf-8"
        )
        observations[label] = {
            "input": str(copied_input.relative_to(artifact_dir)),
            "input_sha256": sha256_file(copied_input),
            "inspection": inspection,
            "trace": str(copied_trace.relative_to(artifact_dir)),
            "trace_sha256": sha256_file(copied_trace),
            "autosave": str(copied_autosave.relative_to(artifact_dir)),
            "autosave_sha256": sha256_file(copied_autosave),
        }

        if initial:
            checkpoint = save_directory / CHECKPOINT
            validate_checkpoint(checkpoint)
            copied_checkpoint = artifacts / CHECKPOINT
            shutil.copy2(checkpoint, copied_checkpoint)
            observations[label]["checkpoint"] = str(
                copied_checkpoint.relative_to(artifact_dir)
            )
            observations[label]["checkpoint_sha256"] = sha256_file(
                copied_checkpoint
            )

    try:
        validate_final_equivalence(
            observations["record"]["inspection"],
            observations["resume"]["inspection"],
        )
    except ResumeFailure as exc:
        record_autosave = read_json_object(
            artifact_dir / str(observations["record"]["autosave"]),
            "record autosave artifact",
        )
        resume_autosave = read_json_object(
            artifact_dir / str(observations["resume"]["autosave"]),
            "resume autosave artifact",
        )
        difference = first_json_difference(
            autosave_physics_projection(record_autosave),
            autosave_physics_projection(resume_autosave),
        )
        raise ResumeFailure(
            f"{exc}; autosave physics difference="
            f"{json.dumps(difference, sort_keys=True)}"
        ) from exc
    if observations["record"]["input_sha256"] != observations["resume"][
        "input_sha256"
    ]:
        raise ResumeFailure("continued input artifact differs from baseline")
    record_input = artifact_dir / str(observations["record"]["input"])
    resume_input = artifact_dir / str(observations["resume"]["input"])
    if record_input.read_bytes() != resume_input.read_bytes():
        raise ResumeFailure("continued input bytes differ despite SHA report")

    repository_status = git_output(repository, ("status", "--porcelain"))
    report = {
        "checkpoint_step": SAVE_STEP,
        "content_commit": CONTENT_COMMIT,
        "executable": str(executable),
        "executable_sha256": sha256_file(executable),
        "final_state_digest": observations["record"]["inspection"][
            "final_step"
        ]["state_digest"],
        "final_step": FINAL_STEP,
        "format": "ror-d0-deterministic-savegame-resume-report-v1",
        "input_artifacts_exact": True,
        "machine": platform.machine(),
        "observations": observations,
        "platform": platform.platform(),
        "repository_commit": git_output(repository, ("rev-parse", "HEAD")),
        "repository_dirty": bool(repository_status),
        "runtime_content": str(runtime_content),
        "scenario_id": SCENARIO_ID,
        "target_id": TARGET_ID,
        "terminal_state_exact": True,
        "workers": args.workers,
    }
    temporary = artifact_dir / "report.json.tmp"
    report_path = artifact_dir / "report.json"
    temporary.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, report_path)
    print(f"D0 deterministic save/resume gate passed: {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ResumeFailure as exc:
        message = f"deterministic save/resume gate failed: {exc}"
        print(message, file=sys.stderr)
        if os.environ.get("GITHUB_ACTIONS") == "true":
            print(format_github_error(message), file=sys.stderr)
        raise SystemExit(1)
