#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import binascii
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile
import zlib


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = REPOSITORY_ROOT / "tools/run_cityworld_bridge_scene.py"
FIXTURE_ROOT = (
    REPOSITORY_ROOT / "tests/fixtures/cityworld_bridge_runtime"
)
PLATFORM_UTILS = REPOSITORY_ROOT / "source/main/utils/PlatformUtils.cpp"

SPEC = importlib.util.spec_from_file_location(
    "run_cityworld_bridge_scene",
    TOOL_PATH,
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load CityWorld bridge runtime tool")
SCENE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SCENE)


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

    def test_generated_config_locks_resolution_and_renderer(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory)
            ror, ogre = SCENE.write_runtime_config(config)
            ror_text = ror.read_text(encoding="utf-8")
            self.assertIn(
                "app_force_cache_update=true",
                ror_text,
            )
            self.assertIn(
                "gfx_shadow_type=No shadows (fastest)",
                ror_text,
            )
            self.assertIn("gfx_shadow_quality=2", ror_text)
            ogre_text = ogre.read_text(encoding="utf-8")
            self.assertIn("Content Scaling Factor=1", ogre_text)
            self.assertIn("Video Mode=1280 x 720", ogre_text)
            self.assertIn(
                "Render System=OpenGL 3+ Rendering Subsystem", ogre_text
            )

    def test_generated_config_can_lock_high_quality_pssm(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory)
            ror, _ = SCENE.write_runtime_config(
                config,
                shadow_mode="pssm",
                shadow_quality=2,
            )
            ror_text = ror.read_text(encoding="utf-8")
            self.assertIn(
                "gfx_shadow_type=Parallel-split Shadow Maps",
                ror_text,
            )
            self.assertIn("gfx_shadow_quality=2", ror_text)
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.write_runtime_config(
                    config / "bad-mode",
                    shadow_mode="raytraced",
                )
            with self.assertRaises(SCENE.BridgeSceneFailure):
                SCENE.write_runtime_config(
                    config / "bad-quality",
                    shadow_mode="pssm",
                    shadow_quality=4,
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

    def test_renderer_identity_and_shadow_config_are_fail_closed(self) -> None:
        engine_log = "\n".join(
            (
                "GL_VERSION = 4.1.0.0",
                "RenderSystem Name: OpenGL 3+ Rendering Subsystem",
                "GPU Vendor: apple",
                "Device Name: Apple M5",
            )
        )
        self.assertEqual(
            SCENE.parse_renderer_identity(engine_log),
            {
                "api_version": "4.1.0.0",
                "device": "Apple M5",
                "render_system": "OpenGL 3+ Rendering Subsystem",
                "vendor": "apple",
            },
        )
        with self.assertRaises(SCENE.BridgeSceneFailure):
            SCENE.parse_renderer_identity(engine_log + "\nDevice Name: other")
        with self.assertRaises(SCENE.BridgeSceneFailure):
            SCENE.parse_renderer_identity(
                engine_log.replace("GL_VERSION = 4.1.0.0\n", "")
            )

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


if __name__ == "__main__":
    unittest.main()
