#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import binascii
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zipfile
import zlib


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_cityworld_bridge_scene.py"
FIXTURE_ROOT = (
    REPOSITORY_ROOT / "tests/fixtures/cityworld_bridge_runtime"
)
PLATFORM_UTILS = REPOSITORY_ROOT / "source/main/utils/PlatformUtils.cpp"
APP_CONTEXT = REPOSITORY_ROOT / "source/main/AppContext.cpp"

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_bridge_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld bridge runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCENE)

EXPECTED_GL3PLUS_CONFIG = "\n".join(
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
)
EXPECTED_D3D11_CONFIG = "\n".join(
    (
        "Render System=Direct3D11 Rendering Subsystem",
        "",
        "[Direct3D11 Rendering Subsystem]",
        "Allow NVPerfHUD=No",
        "Debug Layer=Off",
        "Driver type=Hardware",
        "FSAA=1",
        "Full Screen=No",
        "Information Queue Exceptions Bottom Level="
        "No information queue exceptions",
        "Max Requested Feature Levels=11.0",
        "Min Requested Feature Levels=9.1",
        "Rendering Device=(default)",
        "Reversed Z-Buffer=No",
        "VSync=No",
        "VSync Interval=1",
        "Video Mode=1280 x 720 @ 32-bit colour",
        "sRGB Gamma Conversion=No",
        "",
    )
)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = binascii.crc32(kind)
    checksum = binascii.crc32(payload, checksum) & 0xFFFFFFFF
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", checksum)
    )


def rgb_png(width: int = 1280, height: int = 720, *, flat: bool = False) -> bytes:
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            if flat:
                rows.extend((100, 100, 100))
            else:
                rows.extend(
                    (
                        x % 256,
                        y % 256,
                        (x * 3 + y * 5) % 256,
                    )
                )
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
        + png_chunk(b"IEND", b"")
    )


def valid_logs() -> tuple[str, str]:
    engine = "\n".join(SCENE.ENGINE_MARKERS)
    script = "\n".join(
        (
            *SCENE.SCRIPT_MARKERS[:-1],
            "[RoR|CW2|BridgeRuntime] PASS spans=3 seams=2 "
            "distance_m=90.1281 min_y=0.69451 max_y=1.50233 "
            "lateral_error=0.640167 speed=16.9444 physics_steps=20260",
        )
    )
    return engine, script


def valid_pssm_marker() -> str:
    return (
        "[RoR|Shadow|PSSM] enabled quality=2 cascades=3 "
        "rtss_receiver=1 format=PF_DEPTH16 "
        "sizes=3072x3072/2048x2048/2048x2048 "
        "lambda=0.970000 near=0.500000 far=350.000000 "
        "splits=0.500000/7.816331/45.241116/350.000000"
    )


def valid_postprocess_marker(
    mode: str,
    target_platform: str,
) -> str:
    requested = SCENE.POSTPROCESS_MODES[mode]
    effective = 0 if mode == "none" else 1
    status = "requested_none" if mode == "none" else "enabled"
    stage = "bypassed" if mode == "none" else "attached"
    renderer = (
        "Direct3D11 Rendering Subsystem"
        if target_platform == "win32"
        else "OpenGL 3+ Rendering Subsystem"
    )
    return (
        "[RoR|PostProcess] event=scene_ready "
        f"requested={requested} effective={effective} "
        f"backend={SCENE.POSTPROCESS_BACKENDS[target_platform]} "
        f"status={status} stage={stage} backing=1280x720 "
        f"renderer={renderer} detail=none"
    )


class CityWorldBridgeSceneTests(unittest.TestCase):
    def test_runtime_log_gate_requires_mesh_shader_and_physics_evidence(
        self,
    ) -> None:
        engine, script = valid_logs()
        metrics = SCENE.validate_runtime_logs(0, "", engine, script)
        self.assertEqual(metrics["physics_steps"], 20260)
        self.assertAlmostEqual(metrics["distance_m"], 90.1281)

        for marker in SCENE.ENGINE_MARKERS:
            with self.subTest(engine_marker=marker):
                with self.assertRaises(SCENE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0, "", engine.replace(marker, ""), script
                    )
        for marker in SCENE.SCRIPT_MARKERS[:-1]:
            with self.subTest(script_marker=marker):
                with self.assertRaises(SCENE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0, "", engine, script.replace(marker, "")
                    )
        for marker in SCENE.FATAL_MARKERS:
            with self.subTest(fatal_marker=marker):
                with self.assertRaises(SCENE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(0, marker, engine, script)

    def test_runtime_log_gate_rejects_out_of_contract_metrics(self) -> None:
        engine, script = valid_logs()
        replacements = (
            ("distance_m=90.1281", "distance_m=40"),
            ("min_y=0.69451", "min_y=nan"),
            ("max_y=1.50233", "max_y=9"),
            ("lateral_error=0.640167", "lateral_error=2.1"),
            ("speed=16.9444", "speed=0"),
            ("physics_steps=20260", "physics_steps=30001"),
        )
        for old, new in replacements:
            with self.subTest(value=new):
                with self.assertRaises(SCENE.BridgeSceneFailure):
                    SCENE.validate_runtime_logs(
                        0, "", engine, script.replace(old, new)
                    )

    def test_png_gate_fully_decodes_rgb_and_rejects_corruption(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "frame.png"
            image.write_bytes(rgb_png())
            record = SCENE.validate_rgb_png(image)
            self.assertEqual(record["width"], 1280)
            self.assertEqual(record["height"], 720)
            self.assertGreater(record["sampled_colours"], 16)
            decoded_record, pixels = SCENE.decode_rgb_png(image)
            self.assertEqual(decoded_record, record)
            self.assertEqual(len(pixels), 1280 * 720 * 3)
            self.assertEqual(pixels[:6], bytes((0, 0, 0, 1, 0, 3)))

            corrupted = bytearray(image.read_bytes())
            corrupted[-1] ^= 0xFF
            image.write_bytes(corrupted)
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.validate_rgb_png(image)

            image.write_bytes(rgb_png(width=1279))
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.validate_rgb_png(image)

            image.write_bytes(rgb_png(flat=True))
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.validate_rgb_png(image)

    def test_runtime_pack_is_flat_complete_and_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository = root / "repository"
            source = root / "content"
            fixture = repository / SCENE.FIXTURE_DIRECTORY
            fixture.mkdir(parents=True)
            simple = source / "simple2-terrain"
            simple.mkdir(parents=True)
            for name in SCENE.SIMPLE2_FILES:
                (simple / name).write_bytes(("simple-" + name).encode())
            for name in SCENE.FIXTURE_FILES:
                (fixture / name).write_bytes(("fixture-" + name).encode())
            compiled = []
            for name in ("bridge.material", "bridge.odef", "bridge.mesh"):
                path = root / "compiled" / name
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(("compiled-" + name).encode())
                compiled.append(path)

            first = root / "first.zip"
            second = root / "second.zip"
            first_inventory, first_hash = SCENE.build_runtime_pack(
                repository, source, compiled, first
            )
            second_inventory, second_hash = SCENE.build_runtime_pack(
                repository, source, reversed(compiled), second
            )
            self.assertEqual(first_inventory, second_inventory)
            self.assertEqual(first_hash, second_hash)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first, "r") as archive:
                self.assertEqual(archive.namelist(), sorted(first_inventory))
                self.assertIsNone(archive.testzip())
                for info in archive.infolist():
                    self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))

    def test_vehicle_archive_must_match_pinned_rig(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            runtime = root / "runtime"
            (source / "dafsemi").mkdir(parents=True)
            runtime.mkdir()
            truck = source / "dafsemi" / SCENE.VEHICLE_ENTRY
            truck.write_bytes(b"pinned truck")
            archive_path = runtime / SCENE.VEHICLE_ARCHIVE
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr(SCENE.VEHICLE_ENTRY, b"pinned truck")
            self.assertEqual(
                SCENE.verify_vehicle_archive(source, runtime),
                archive_path,
            )
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr(SCENE.VEHICLE_ENTRY, b"changed truck")
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.verify_vehicle_archive(source, runtime)

    def test_runtime_layout_is_native_and_isolated(self) -> None:
        root = Path("/isolated")
        mac = SCENE.runtime_layout(root, "darwin")
        linux = SCENE.runtime_layout(root, "linux")
        windows = SCENE.runtime_layout(root, "win32")
        self.assertEqual(
            mac["screenshots"],
            root
            / "Library"
            / "Application Support"
            / "Rigs of Rods"
            / "screenshots",
        )
        self.assertEqual(mac["logs"], root / "Library/Logs/Rigs of Rods")
        self.assertEqual(linux["user"], root / ".rigsofrods")
        self.assertEqual(
            windows["user"], root / "My Games" / "Rigs of Rods"
        )
        for layout in (mac, linux, windows):
            for path in layout.values():
                self.assertTrue(path.is_relative_to(root))
        with self.assertRaisesRegex(
            SCENE.BridgeSceneFailure,
            "unsupported CityWorld runtime platform",
        ):
            SCENE.runtime_layout(root, "freebsd14")

    def test_process_diagnostics_survive_windows_access_violation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact_dir = root / "artifact"
            artifact_dir.mkdir()
            logs = root / "logs"
            configs = root / "config"
            logs.mkdir()
            configs.mkdir()
            engine_log = logs / "RoR.log"
            script_log = logs / "Angelscript.log"
            engine_log.write_text(
                "engine start\nlast engine phase\n",
                encoding="utf-8",
            )
            script_log.write_text(
                "frame=500\nframe=600\n",
                encoding="utf-8",
            )
            effective_ror = configs / "RoR.cfg"
            effective_ogre = configs / "ogre.cfg"
            effective_ror.write_bytes(b"effective ror\n")
            effective_ogre.write_bytes(b"effective ogre\n")

            document = SCENE.persist_runtime_process_diagnostics(
                artifact_dir,
                SCENE.subprocess.CompletedProcess(
                    ["RoR.exe"],
                    0xC0000005,
                    b"native stdout\xff\n",
                    b"native stderr\xfe\n",
                ),
                engine_log,
                script_log,
                {
                    "RoR.cfg": b"requested ror\n",
                    "ogre.cfg": b"requested ogre\n",
                },
                {
                    "RoR.cfg": effective_ror,
                    "ogre.cfg": effective_ogre,
                },
                "win32",
            )

            self.assertEqual(
                document["format"],
                SCENE.PROCESS_DIAGNOSTIC_FORMAT,
            )
            self.assertEqual(
                document["termination"],
                {
                    "kind": "windows_ntstatus",
                    "meaning": "access_violation",
                    "ntstatus_hex": "0xC0000005",
                    "returncode": 0xC0000005,
                    "unsigned_returncode": 0xC0000005,
                },
            )
            self.assertEqual(
                document["last_lines"]["Angelscript.log"],
                "frame=600",
            )
            self.assertEqual(
                document["last_lines"]["runtime.stdout"],
                "native stdout\ufffd",
            )
            self.assertEqual(
                document["last_lines"]["runtime.stderr"],
                "native stderr\ufffd",
            )
            self.assertEqual(
                (
                    artifact_dir
                    / "diagnostics"
                    / "runtime.stdout"
                ).read_bytes(),
                b"native stdout\xff\n",
            )
            self.assertEqual(
                (
                    artifact_dir
                    / "diagnostics"
                    / "runtime.stderr"
                ).read_bytes(),
                b"native stderr\xfe\n",
            )
            for name in (
                "RoR.log",
                "Angelscript.log",
                "requested-RoR.cfg",
                "effective-RoR.cfg",
                "requested-ogre.cfg",
                "effective-ogre.cfg",
                "runtime-process.json",
            ):
                with self.subTest(name=name):
                    self.assertTrue(
                        (artifact_dir / "diagnostics" / name).is_file()
                    )
            self.assertFalse(
                (
                    artifact_dir
                    / "diagnostics"
                    / "runtime-process.json.tmp"
                ).exists()
            )

        self.assertEqual(
            SCENE.process_termination_record(-11, "linux"),
            {"kind": "signal", "returncode": -11, "signal": 11},
        )

    def test_isolated_environment_outranks_snap_and_portable_config(
        self,
    ) -> None:
        isolated = Path("/isolated-scene-home")
        with mock.patch.dict(
            SCENE.os.environ,
            {
                "KEEP_ME": "yes",
                "ROR_D0_EXACT_WINDOW_EXTENT": "640x360",
                "SNAP_USER_COMMON": "/real/snap/home",
            },
            clear=True,
        ):
            environment = SCENE.isolated_runtime_environment(isolated)
        self.assertEqual(environment["KEEP_ME"], "yes")
        self.assertNotIn("SNAP_USER_COMMON", environment)
        self.assertEqual(
            environment["ROR_D0_SCENE_HOME"],
            str(isolated),
        )
        self.assertEqual(
            environment["ROR_D0_EXACT_WINDOW_EXTENT"],
            "1280x720",
        )
        self.assertEqual(environment["ALSOFT_DRIVERS"], "null")
        self.assertEqual(environment["ALSOFT_LOGLEVEL"], "0")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "bin" / "RoR"
            executable.parent.mkdir()
            (executable.parent / "config").mkdir()
            for target in ("linux", "win32"):
                with self.subTest(target=target):
                    with self.assertRaisesRegex(
                        SCENE.BridgeSceneFailure,
                        "portable executable config",
                    ):
                        SCENE.require_isolated_runtime_executable(
                            executable,
                            target,
                        )
            SCENE.require_isolated_runtime_executable(
                executable,
                "darwin",
            )

    def test_main_rejects_unknown_platform_before_creating_artifacts(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact_dir = root / "artifacts"
            with mock.patch.object(SCENE.sys, "platform", "freebsd14"):
                with self.assertRaisesRegex(
                    SCENE.BridgeSceneFailure,
                    "unsupported CityWorld runtime platform",
                ):
                    SCENE.main(
                        (
                            "--executable",
                            str(root / "missing-ror"),
                            "--artifact-dir",
                            str(artifact_dir),
                        )
                    )
            self.assertFalse(artifact_dir.exists())

    def test_generated_config_locks_resolution_and_renderer(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ror_payloads: list[str] = []
            for target_platform in ("darwin", "linux"):
                with self.subTest(target_platform=target_platform):
                    ror, ogre = SCENE.write_runtime_config(
                        root / target_platform,
                        target_platform=target_platform,
                    )
                    ror_text = ror.read_text(encoding="utf-8")
                    ror_payloads.append(ror_text)
                    self.assertIn(
                        "app_force_cache_update=true",
                        ror_text,
                    )
                    self.assertIn("app_async_physics=true", ror_text)
                    self.assertIn(
                        "gfx_shadow_type=No shadows (fastest)",
                        ror_text,
                    )
                    self.assertIn("gfx_postprocess_mode=0", ror_text)
                    self.assertIn("gfx_shadow_quality=2", ror_text)
                    self.assertEqual(
                        ogre.read_text(encoding="utf-8"),
                        EXPECTED_GL3PLUS_CONFIG,
                    )

            windows_ror, windows_ogre = SCENE.write_runtime_config(
                root / "win32",
                target_platform="win32",
            )
            ror_payloads.append(
                windows_ror.read_text(encoding="utf-8")
            )
            d3d11_text = windows_ogre.read_text(encoding="utf-8")
            self.assertEqual(d3d11_text, EXPECTED_D3D11_CONFIG)
            for gl_only_option in (
                "Colour Depth=",
                "Content Scaling Factor=",
                "Display Frequency=",
                "Separate Shader Objects=",
            ):
                with self.subTest(gl_only_option=gl_only_option):
                    self.assertNotIn(gl_only_option, d3d11_text)
            self.assertEqual(len(set(ror_payloads)), 1)

            sync_ror, _ = SCENE.write_runtime_config(
                root / "win32-sync",
                physics_mode="sync",
                target_platform="win32",
            )
            sync_text = sync_ror.read_text(encoding="utf-8")
            self.assertIn("app_num_workers=1", sync_text)
            self.assertIn("app_async_physics=false", sync_text)
            self.assertNotIn("app_async_physics=true", sync_text)
            self.assertEqual(
                SCENE.validate_physics_config(
                    sync_ror.read_bytes(),
                    "sync",
                ),
                {"async_physics": False, "num_workers": 1},
            )

            effective_sync = sync_text.replace(
                "app_async_physics=false",
                "app_async_physics=No",
            )
            self.assertEqual(
                SCENE.validate_physics_config(
                    effective_sync.encode("utf-8"),
                    "sync",
                ),
                {"async_physics": False, "num_workers": 1},
            )
            effective_async = sync_text.replace(
                "app_async_physics=false",
                "app_async_physics=Yes",
            )
            self.assertEqual(
                SCENE.validate_physics_config(
                    effective_async.encode("utf-8"),
                    "async",
                ),
                {"async_physics": True, "num_workers": 1},
            )

            invalid_physics_configs = (
                sync_text.replace(
                    "app_async_physics=false",
                    "app_async_physics=true",
                ),
                sync_text.replace("app_num_workers=1", "app_num_workers=2"),
                sync_text + "app_async_physics=false\n",
                effective_sync + "app_async_physics=No\n",
                sync_text.replace(
                    "app_async_physics=false",
                    "app_async_physics=no",
                ),
                sync_text.replace(
                    "app_async_physics=false",
                    "app_async_physics=0",
                ),
            )
            for invalid in invalid_physics_configs:
                with self.subTest(invalid=invalid):
                    with self.assertRaises(SCENE.BridgeSceneFailure):
                        SCENE.validate_physics_config(
                            invalid.encode("utf-8"),
                            "sync",
                        )

            self.assertEqual(
                SCENE.parse_args(
                    (
                        "--executable",
                        "RoR.exe",
                        "--artifact-dir",
                        "artifact",
                        "--physics-mode",
                        "sync",
                    )
                ).physics_mode,
                "sync",
            )

            unsupported = root / "freebsd14"
            with self.assertRaisesRegex(
                SCENE.BridgeSceneFailure,
                "unsupported CityWorld runtime platform",
            ):
                SCENE.write_runtime_config(
                    unsupported,
                    target_platform="freebsd14",
                )
            self.assertFalse(unsupported.exists())
            with self.assertRaisesRegex(
                SCENE.BridgeSceneFailure,
                "unsupported physics mode",
            ):
                SCENE.write_runtime_config(
                    root / "invalid-physics-mode",
                    physics_mode="parallel",
                    target_platform="win32",
                )

    def test_generated_config_can_lock_high_quality_pssm(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target_platform in ("darwin", "linux", "win32"):
                with self.subTest(target_platform=target_platform):
                    ror, _ = SCENE.write_runtime_config(
                        root / target_platform,
                        shadow_mode="pssm",
                        shadow_quality=2,
                        target_platform=target_platform,
                    )
                    ror_text = ror.read_text(encoding="utf-8")
                    self.assertIn(
                        "gfx_shadow_type=Parallel-split Shadow Maps",
                        ror_text,
                    )
                    self.assertIn("gfx_shadow_quality=2", ror_text)
                    self.assertIn("gfx_postprocess_mode=0", ror_text)
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.write_runtime_config(
                    root / "bad-mode",
                    shadow_mode="raytraced",
                    target_platform="darwin",
                )
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.write_runtime_config(
                    root / "bad-quality",
                    shadow_mode="pssm",
                    shadow_quality=4,
                    target_platform="win32",
                )
            v0a, _ = SCENE.write_runtime_config(
                root / "v0a",
                postprocess_mode="v0a",
                target_platform="darwin",
            )
            self.assertIn(
                "gfx_postprocess_mode=1",
                v0a.read_text(encoding="utf-8"),
            )
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.write_runtime_config(
                    root / "bad-postprocess",
                    postprocess_mode="hdr",
                    target_platform="darwin",
                )

    def test_pssm_marker_gate_checks_complete_effective_configuration(
        self,
    ) -> None:
        marker = valid_pssm_marker()
        record = SCENE.validate_pssm_log(marker, "pssm", 2)
        self.assertIsNotNone(record)
        self.assertEqual(record["format"], "PF_DEPTH16")
        self.assertEqual(
            record["sizes"],
            [[3072, 3072], [2048, 2048], [2048, 2048]],
        )
        self.assertEqual(
            SCENE.validate_pssm_log("", "none", 2),
            None,
        )
        invalid_markers = (
            marker.replace("rtss_receiver=1", "rtss_receiver=0"),
            marker.replace("PF_DEPTH16", "PF_FLOAT32_R"),
            marker.replace("3072x3072", "2048x2048"),
            marker.replace("lambda=0.970000", "lambda=0.500000"),
            marker.replace("45.241116", "7.000000"),
            marker + "\n" + marker,
            marker.replace("format=PF_DEPTH16 ", ""),
        )
        for invalid in invalid_markers:
            with self.subTest(marker=invalid):
                with self.assertRaises(SCENE.BridgeSceneFailure):
                    SCENE.validate_pssm_log(invalid, "pssm", 2)
        with self.assertRaises(SCENE.BridgeSceneFailure):
            SCENE.validate_pssm_log(marker, "none", 2)

    def test_postprocess_marker_gate_proves_platform_effective_mode(
        self,
    ) -> None:
        for mode in ("none", "v0a"):
            for target_platform in ("darwin", "linux", "win32"):
                with self.subTest(
                    mode=mode,
                    target_platform=target_platform,
                ):
                    marker = valid_postprocess_marker(
                        mode,
                        target_platform,
                    )
                    record = SCENE.validate_postprocess_log(
                        marker,
                        mode,
                        target_platform,
                    )
                    self.assertEqual(
                        record["requested_mode"],
                        SCENE.POSTPROCESS_MODES[mode],
                    )
                    self.assertEqual(
                        record["backend"],
                        SCENE.POSTPROCESS_BACKENDS[target_platform],
                    )
                    self.assertEqual(record["backing_width"], 1280)
                    self.assertEqual(record["backing_height"], 720)

        marker = valid_postprocess_marker("v0a", "linux")
        invalid_markers = (
            marker.replace("requested=1", "requested=0"),
            marker.replace("effective=1", "effective=0"),
            marker.replace("status=enabled", "status=program_unavailable"),
            marker.replace("stage=attached", "stage=failed"),
            marker.replace("backing=1280x720", "backing=640x360"),
            marker.replace("backend=gl3plus_glsl330", "backend=d3d11_sm4"),
            marker.replace(
                "renderer=OpenGL 3+ Rendering Subsystem",
                "renderer=other",
            ),
            marker + "\n" + marker,
            marker + "\n[RoR|PostProcess] incomplete",
            marker
            + "\n"
            + marker.replace(
                "event=scene_ready requested=1 effective=1 "
                "backend=gl3plus_glsl330 status=enabled stage=attached",
                "event=main_window_readback requested=1 effective=0 "
                "backend=gl3plus_glsl330 "
                "status=program_unavailable stage=failed",
            ),
        )
        for invalid in invalid_markers:
            with self.subTest(marker=invalid):
                with self.assertRaises(SCENE.BridgeSceneFailure):
                    SCENE.validate_postprocess_log(
                        invalid,
                        "v0a",
                        "linux",
                    )
        with self.assertRaises(SCENE.BridgeSceneFailure):
            SCENE.validate_postprocess_log(
                marker,
                "none",
                "linux",
            )
        with self.assertRaises(SCENE.BridgeSceneFailure):
            SCENE.validate_postprocess_log(
                marker,
                "v0a",
                "freebsd14",
            )

    def test_renderer_identity_and_shadow_config_are_fail_closed(self) -> None:
        gl3plus_log = "\n".join(
            (
                "GL_VERSION = 4.1.0.0",
                "RenderSystem Name: OpenGL 3+ Rendering Subsystem",
                "GPU Vendor: apple",
                "Device Name: Apple M5",
            )
        )
        self.assertEqual(
            SCENE.parse_renderer_identity(gl3plus_log, "darwin"),
            {
                "api_version": "4.1.0.0",
                "device": "Apple M5",
                "render_system": "OpenGL 3+ Rendering Subsystem",
                "vendor": "apple",
            },
        )
        self.assertEqual(
            SCENE.parse_renderer_identity(gl3plus_log, "linux"),
            {
                "api_version": "4.1.0.0",
                "device": "Apple M5",
                "render_system": "OpenGL 3+ Rendering Subsystem",
                "vendor": "apple",
            },
        )
        egl_gl3plus_log = "\n".join(
            (
                "03:25:57: EGL_VERSION = 1.5",
                "03:25:57: GL_VERSION = 4.5.0.0",
                "03:25:57: RenderSystem Name: "
                "OpenGL 3+ Rendering Subsystem",
                "03:25:57: GPU Vendor: unknown",
                "03:25:57: Device Name: llvmpipe",
            )
        )
        self.assertEqual(
            SCENE.parse_renderer_identity(egl_gl3plus_log, "linux"),
            {
                "api_version": "4.5.0.0",
                "device": "llvmpipe",
                "render_system": "OpenGL 3+ Rendering Subsystem",
                "vendor": "unknown",
            },
        )
        d3d11_log = "\n".join(
            (
                'D3D11: Requested "(default)", selected '
                '"NVIDIA GeForce RTX 4070"',
                "D3D11: Device Feature Level 11.0",
                "RenderSystem Name: Direct3D11 Rendering Subsystem",
                "GPU Vendor: nvidia",
                "Device Name: NVIDIA GeForce RTX 4070",
            )
        )
        self.assertEqual(
            SCENE.parse_renderer_identity(d3d11_log, "win32"),
            {
                "api_version": "11.0",
                "device": "NVIDIA GeForce RTX 4070",
                "render_system": "Direct3D11 Rendering Subsystem",
                "vendor": "nvidia",
            },
        )

        invalid_renderer_logs = (
            (
                "duplicate GL API",
                gl3plus_log + "\nGL_VERSION = 4.1.0.0",
                "darwin",
            ),
            (
                "duplicate GL device",
                gl3plus_log + "\nDevice Name: other",
                "linux",
            ),
            (
                "missing GL API",
                gl3plus_log.replace("GL_VERSION = 4.1.0.0\n", ""),
                "darwin",
            ),
            (
                "wrong GL renderer",
                gl3plus_log.replace(
                    "OpenGL 3+ Rendering Subsystem",
                    "Direct3D11 Rendering Subsystem",
                ),
                "linux",
            ),
            (
                "duplicate D3D feature level",
                d3d11_log + "\nD3D11: Device Feature Level 11.0",
                "win32",
            ),
            (
                "missing D3D feature level",
                d3d11_log.replace(
                    "D3D11: Device Feature Level 11.0\n",
                    "",
                ),
                "win32",
            ),
            (
                "wrong D3D renderer",
                d3d11_log.replace(
                    "Direct3D11 Rendering Subsystem",
                    "OpenGL 3+ Rendering Subsystem",
                ),
                "win32",
            ),
        )
        for name, engine_log, target_platform in invalid_renderer_logs:
            with self.subTest(name=name):
                with self.assertRaises(SCENE.BridgeSceneFailure):
                    SCENE.parse_renderer_identity(
                        engine_log,
                        target_platform,
                    )

        with self.assertRaises(SCENE.BridgeSceneFailure):
            SCENE.parse_renderer_identity(gl3plus_log, "freebsd14")

        no_shadows = (
            b"gfx_shadow_type=No shadows (fastest)\n"
            b"gfx_shadow_quality=2\n"
        )
        pssm = (
            b"gfx_shadow_type=Parallel-split Shadow Maps\n"
            b"gfx_shadow_quality=2\n"
        )
        self.assertEqual(
            SCENE.normalize_shadow_config(no_shadows),
            SCENE.normalize_shadow_config(pssm),
        )
        with self.assertRaises(SCENE.BridgeSceneFailure):
            SCENE.normalize_shadow_config(b"gfx_shadow_quality=2\n")

        no_postprocess = b"gfx_postprocess_mode=0\n"
        v0a_postprocess = b"gfx_postprocess_mode=1\n"
        self.assertEqual(
            SCENE.normalize_postprocess_config(no_postprocess),
            SCENE.normalize_postprocess_config(v0a_postprocess),
        )
        with self.assertRaises(SCENE.BridgeSceneFailure):
            SCENE.normalize_postprocess_config(
                b"gfx_shadow_quality=2\n"
            )

    def test_fixture_locks_exact_connectors_and_collision_run(self) -> None:
        script = (FIXTURE_ROOT / "cityworld_bridge_runtime.as").read_text(
            encoding="utf-8"
        )
        tobj = (FIXTURE_ROOT / "cityworld_bridge_runtime.tobj").read_text(
            encoding="utf-8"
        )
        for marker in (
            'const uint64 MAX_PHYSICS_STEPS = 30000;',
            '"sim_deterministic_fixed_steps_per_frame", "20"',
            '"sim_no_collisions", "false"',
            '"sim_no_self_collisions", "false"',
            "SEAM index=0",
            "SEAM index=1",
            "MSG_APP_SCREENSHOT_REQUESTED",
            "quaternion(radian(1.57079633f)",
        ):
            with self.subTest(script_marker=marker):
                self.assertIn(marker, script)
        object_lines = [
            line
            for line in tobj.splitlines()
            if line and not line.startswith("//")
        ]
        self.assertEqual(len(object_lines), 3)
        self.assertEqual(
            [line.split()[2] for line in object_lines],
            ["492,", "512,", "532,"],
        )
        for line in object_lines:
            self.assertIn(
                ", rorng_city_bridge_span_20m - bridge_span_", line
            )
            self.assertNotIn("rorng_city_bridge_span_20m,", line)

    def test_windows_honors_the_scene_home_override(self) -> None:
        source = PLATFORM_UTILS.read_text(encoding="utf-8")
        windows_start = source.index(
            "// -------------------------- File/path utils for MS Windows"
        )
        windows_end = source.index(
            "// -------------------------- File/path utils for Linux",
            windows_start,
        )
        windows_source = source[windows_start:windows_end]
        override = windows_source.index('getenv("ROR_D0_SCENE_HOME")')
        known_folder = windows_source.index("SHGetFolderPathW")
        self.assertLess(override, known_folder)
        self.assertIn("IsAbsolutePath(d0_scene_home)", windows_source)

    def test_windows_exact_extent_is_isolated_and_fail_closed(self) -> None:
        source = APP_CONTEXT.read_text(encoding="utf-8")
        windows_start = source.index(
            "#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32",
            source.index("// Validate rendering resolution"),
        )
        windows_end = source.index("#endif", windows_start)
        windows_source = source[windows_start:windows_end]
        for required in (
            'std::getenv("ROR_D0_SCENE_HOME")',
            'std::getenv("ROR_D0_EXACT_WINDOW_EXTENT")',
            "IsAbsolutePath(d0_scene_home)",
            "selected_extent == d0_exact_window_extent",
            'ropts["Full Screen"].currentValue == "No"',
            "!App::diag_allow_window_resize->getBool()",
            'miscParams["border"] = "none"',
            'miscParams["outerDimensions"] = "true"',
        ):
            with self.subTest(required=required):
                self.assertIn(required, windows_source)
        self.assertNotIn("ROR_D0_EXACT_WINDOW_EXTENT", source[:windows_start])


if __name__ == "__main__":
    unittest.main()
