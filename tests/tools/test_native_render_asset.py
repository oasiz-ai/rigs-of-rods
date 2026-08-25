#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from typing import Any, Callable


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = REPOSITORY_ROOT / "tools/validate_native_render_asset.py"
COMPILER = REPOSITORY_ROOT / "tools/compile_native_render_asset.py"
GENERATOR_RELATIVE = Path(
    "tools/blender/native_render/generate_a0_road_tile.py"
)
GENERATOR = REPOSITORY_ROOT / GENERATOR_RELATIVE
MANIFEST_RELATIVE = Path(
    "content-source/native_render/a0_road_tile_12m/"
    "rorng_a0_road_tile_12m.native.json"
)
PACKAGE_RELATIVE = Path(
    "resources/nextgen/native/a0_road_tile_12m/"
    "rorng_a0_road_tile_12m.rornative"
)
REPORT_RELATIVE = Path(
    "resources/nextgen/native/a0_road_tile_12m/"
    "rorng_a0_road_tile_12m.compile.json"
)
LEDGER = REPOSITORY_ROOT / "doc/nextgen/FORWARD_NATIVE_ASSET_LEDGER.md"


def canonical_pretty(value: dict[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=True, indent=2, sort_keys=True) + "\n"


def write_canonical_pretty(path: Path, value: dict[str, Any]) -> None:
    """Write exact LF-delimited fixture bytes on every host platform."""

    path.write_bytes(canonical_pretty(value).encode("ascii"))


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_glb(path: Path) -> tuple[dict[str, Any], bytearray]:
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF" or version != 2 or length != len(data):
        raise AssertionError("test GLB is malformed")
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise AssertionError("test GLB lacks JSON")
    json_start = 20
    json_end = json_start + json_length
    binary_length, binary_type = struct.unpack_from("<II", data, json_end)
    if binary_type != 0x004E4942:
        raise AssertionError("test GLB lacks BIN")
    document = json.loads(data[json_start:json_end].rstrip(b" \x00"))
    binary = bytearray(data[json_end + 8 : json_end + 8 + binary_length])
    return document, binary


def write_glb(
    path: Path,
    document: dict[str, Any],
    binary: bytes,
    *,
    canonical: bool = True,
) -> None:
    if canonical:
        json_payload = json.dumps(
            document,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("ascii")
    else:
        json_payload = json.dumps(
            document,
            ensure_ascii=True,
            sort_keys=False,
            separators=(", ", ": "),
        ).encode("ascii")
    json_payload += b" " * (-len(json_payload) % 4)
    binary_payload = bytes(binary) + b"\x00" * (-len(binary) % 4)
    chunks = (
        struct.pack("<II", len(json_payload), 0x4E4F534A)
        + json_payload
        + struct.pack("<II", len(binary_payload), 0x004E4942)
        + binary_payload
    )
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks)


class NativeRenderAssetToolTests(unittest.TestCase):
    maxDiff = None

    def copy_sources(self, root: Path, *, include_checked: bool = False) -> Path:
        manifest = json.loads(
            (REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="utf-8")
        )
        paths = {
            MANIFEST_RELATIVE.as_posix(),
            manifest["source"]["composition"]["path"],
            manifest["source"]["glb"]["path"],
            manifest["source"]["generator"]["path"],
        }
        composition = json.loads(
            (REPOSITORY_ROOT / manifest["source"]["composition"]["path"]).read_text(
                encoding="utf-8"
            )
        )
        paths.add(composition["preview"]["path"])
        for texture in manifest["textures"]:
            paths.update(mip["path"] for mip in texture["mips"])
        if include_checked:
            paths.update(
                {PACKAGE_RELATIVE.as_posix(), REPORT_RELATIVE.as_posix()}
            )
        for relative in sorted(paths):
            source = REPOSITORY_ROOT / relative
            destination = root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        return root / MANIFEST_RELATIVE

    def run_validator(
        self, root: Path, manifest: Path, *, optimized: bool = False
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, Any]]:
        command = [sys.executable]
        if optimized:
            command.append("-O")
        command.extend(
            [str(VALIDATOR), str(manifest), "--repo-root", str(root)]
        )
        result = subprocess.run(command, check=False, capture_output=True, text=True)
        return result, json.loads(result.stdout)

    def run_compiler(
        self,
        root: Path,
        manifest: Path,
        *,
        optimized: bool = False,
        output: Path | None = None,
        report: Path | None = None,
        validate_checked: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        command = [sys.executable]
        if optimized:
            command.append("-O")
        command.extend(
            [str(COMPILER), str(manifest), "--repo-root", str(root)]
        )
        if output is not None:
            command.extend(["--output", str(output)])
        if report is not None:
            command.extend(["--report", str(report)])
        if validate_checked:
            command.append("--validate-checked")
        return subprocess.run(command, check=False, capture_output=True, text=True)

    @staticmethod
    def rewrite_manifest(path: Path, mutate: Callable[[dict[str, Any]], None]) -> None:
        value = json.loads(path.read_text(encoding="utf-8"))
        mutate(value)
        write_canonical_pretty(path, value)

    @staticmethod
    def codes(report: dict[str, Any]) -> set[str]:
        return {diagnostic["code"] for diagnostic in report["diagnostics"]}

    def refresh_glb_hash(self, manifest_path: Path) -> None:
        value = json.loads(manifest_path.read_text(encoding="utf-8"))
        glb_path = manifest_path.parents[3] / value["source"]["glb"]["path"]
        value["source"]["glb"]["sha256"] = sha256_file(glb_path)
        write_canonical_pretty(manifest_path, value)

    def refresh_composition_hash(self, manifest_path: Path) -> None:
        value = json.loads(manifest_path.read_text(encoding="utf-8"))
        composition_path = (
            manifest_path.parents[3] / value["source"]["composition"]["path"]
        )
        value["source"]["composition"]["sha256"] = sha256_file(composition_path)
        write_canonical_pretty(manifest_path, value)

    def test_checked_source_and_package_pass_normal_and_optimized_gates(self) -> None:
        for optimized in (False, True):
            with self.subTest(optimized=optimized):
                result, report = self.run_validator(
                    REPOSITORY_ROOT,
                    REPOSITORY_ROOT / MANIFEST_RELATIVE,
                    optimized=optimized,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(report["summary"]["valid"])
                self.assertEqual(
                    report["summary"],
                    {
                        "diagnostic_count": 0,
                        "indices": 162,
                        "instances": 5,
                        "materials": 4,
                        "meshes": 5,
                        "samplers": 2,
                        "texture_bytes": 8390984,
                        "textures": 10,
                        "triangles": 54,
                        "valid": True,
                        "vertices": 108,
                    },
                )
                compiled = self.run_compiler(
                    REPOSITORY_ROOT,
                    REPOSITORY_ROOT / MANIFEST_RELATIVE,
                    optimized=optimized,
                    validate_checked=True,
                )
                self.assertEqual(compiled.returncode, 0, compiled.stderr)

    def test_generator_reproduces_every_checked_editable_source_byte(self) -> None:
        for optimized in (False, True):
            with self.subTest(optimized=optimized), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                copied_generator = root / GENERATOR_RELATIVE
                copied_generator.parent.mkdir(parents=True)
                shutil.copy2(GENERATOR, copied_generator)
                command = [sys.executable]
                if optimized:
                    command.append("-O")
                command.extend(
                    [str(copied_generator), "--repo-root", str(root)]
                )
                result = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                checked_root = REPOSITORY_ROOT / MANIFEST_RELATIVE.parent
                generated_root = root / MANIFEST_RELATIVE.parent
                checked = {
                    path.relative_to(checked_root).as_posix(): path.read_bytes()
                    for path in checked_root.rglob("*")
                    if path.is_file()
                }
                generated = {
                    path.relative_to(generated_root).as_posix(): path.read_bytes()
                    for path in generated_root.rglob("*")
                    if path.is_file()
                }
                self.assertEqual(generated, checked)

    def test_checked_normal_mips_are_vector_filtered_and_rg_canonical(self) -> None:
        manifest = json.loads(
            (REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="ascii")
        )

        def read_tga(relative: str) -> tuple[int, int, tuple[tuple[int, int, int, int], ...]]:
            payload = (REPOSITORY_ROOT / relative).read_bytes()
            width, height = struct.unpack_from("<HH", payload, 12)
            self.assertEqual(payload[2], 2)
            self.assertEqual(payload[16:18], bytes((32, 0x28)))
            bgra = payload[18:]
            self.assertEqual(len(bgra), width * height * 4)
            pixels = tuple(
                (bgra[index + 2], bgra[index + 1], bgra[index], bgra[index + 3])
                for index in range(0, len(bgra), 4)
            )
            return width, height, pixels

        def decode_rg(pixel: tuple[int, int, int, int]) -> tuple[float, float, float]:
            x = 2.0 * pixel[0] / 255.0 - 1.0
            y = 2.0 * pixel[1] / 255.0 - 1.0
            return x, y, math.sqrt(max(0.0, 1.0 - x * x - y * y))

        def encode_average(
            samples: tuple[tuple[int, int, int, int], ...]
        ) -> tuple[int, int, int, int]:
            vector = tuple(
                sum(decode_rg(sample)[axis] for sample in samples) / len(samples)
                for axis in range(3)
            )
            length = math.sqrt(sum(component * component for component in vector))
            normalized = tuple(component / length for component in vector)
            red = min(255, max(0, int((normalized[0] + 1.0) * 127.5 + 0.5)))
            green = min(255, max(0, int((normalized[1] + 1.0) * 127.5 + 0.5)))
            quantized_x = 2.0 * red / 255.0 - 1.0
            quantized_y = 2.0 * green / 255.0 - 1.0
            quantized_z = math.sqrt(
                max(0.0, 1.0 - quantized_x * quantized_x - quantized_y * quantized_y)
            )
            blue = min(255, max(128, int((quantized_z + 1.0) * 127.5 + 0.5)))
            return red, green, blue, 255

        for texture in manifest["textures"]:
            if texture["role"] != "normal":
                continue
            previous_width = 0
            previous_height = 0
            previous: tuple[tuple[int, int, int, int], ...] = ()
            for level, mip in enumerate(texture["mips"]):
                width, height, pixels = read_tga(mip["path"])
                for pixel in pixels:
                    x, y, reconstructed_z = decode_rg(pixel)
                    del x, y
                    decoded_blue = 2.0 * pixel[2] / 255.0 - 1.0
                    self.assertLessEqual(
                        abs(decoded_blue - reconstructed_z), 1.0 / 255.0
                    )
                    self.assertEqual(pixel[3], 255)
                if level:
                    for y in range(height):
                        for x in range(width):
                            samples = tuple(
                                previous[
                                    min(previous_height - 1, y * 2 + dy)
                                    * previous_width
                                    + min(previous_width - 1, x * 2 + dx)
                                ]
                                for dy in range(2)
                                for dx in range(2)
                            )
                            self.assertEqual(
                                pixels[y * width + x], encode_average(samples)
                            )
                previous_width, previous_height, previous = width, height, pixels

    def test_a0_surface_maps_keep_512_base_full_mips_and_authored_response(self) -> None:
        manifest = json.loads(
            (REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="ascii")
        )
        textures = {texture["id"]: texture for texture in manifest["textures"]}
        surface_ids = (
            "rorng_a0_road_base",
            "rorng_a0_road_metallic_roughness",
            "rorng_a0_road_normal",
            "rorng_a0_wet_base",
            "rorng_a0_wet_normal",
            "rorng_a0_wet_specular",
        )
        expected_dimensions = tuple(
            (max(1, 512 >> level), max(1, 512 >> level))
            for level in range(10)
        )
        for identifier in surface_ids:
            with self.subTest(identifier=identifier):
                texture = textures[identifier]
                self.assertEqual(len(texture["mips"]), 10)
                self.assertEqual(
                    tuple((mip["width"], mip["height"]) for mip in texture["mips"]),
                    expected_dimensions,
                )

        def base_pixels(identifier: str) -> tuple[tuple[int, int, int, int], ...]:
            payload = (REPOSITORY_ROOT / textures[identifier]["mips"][0]["path"]).read_bytes()
            self.assertEqual(struct.unpack_from("<HH", payload, 12), (512, 512))
            bgra = payload[18:]
            self.assertEqual(len(bgra), 512 * 512 * 4)
            return tuple(
                (bgra[index + 2], bgra[index + 1], bgra[index], bgra[index + 3])
                for index in range(0, len(bgra), 4)
            )

        road_base = base_pixels("rorng_a0_road_base")
        road_roughness = base_pixels("rorng_a0_road_metallic_roughness")
        road_normal = base_pixels("rorng_a0_road_normal")
        wet_base = base_pixels("rorng_a0_wet_base")
        wet_normal = base_pixels("rorng_a0_wet_normal")
        wet_specular = base_pixels("rorng_a0_wet_specular")
        self.assertLessEqual(min(pixel[0] for pixel in road_base), 24)
        self.assertGreaterEqual(max(pixel[0] for pixel in road_base), 70)
        self.assertGreaterEqual(len({pixel[0] for pixel in road_base}), 40)
        dry_roughness = tuple(pixel[1] for pixel in road_roughness)
        self.assertGreaterEqual(min(dry_roughness), 198)
        self.assertGreaterEqual(max(dry_roughness) - min(dry_roughness), 35)
        self.assertTrue(all(pixel[2] == 0 for pixel in road_roughness))
        self.assertGreaterEqual(max(pixel[0] for pixel in road_normal), 160)
        self.assertLessEqual(min(pixel[0] for pixel in road_normal), 96)
        self.assertGreaterEqual(max(pixel[1] for pixel in wet_base), 38)
        self.assertLessEqual(min(pixel[1] for pixel in wet_base), 24)
        self.assertGreaterEqual(max(pixel[0] for pixel in wet_normal), 137)
        self.assertLessEqual(min(pixel[0] for pixel in wet_normal), 118)
        self.assertGreaterEqual(len({pixel[:2] for pixel in wet_normal}), 200)
        self.assertGreaterEqual(min(pixel[0] for pixel in wet_specular), 220)

        wet_base_green = tuple(pixel[1] for pixel in wet_base)
        wet_specular_red = tuple(pixel[0] for pixel in wet_specular)
        base_mean = sum(wet_base_green) / len(wet_base_green)
        specular_mean = sum(wet_specular_red) / len(wet_specular_red)
        shared_field_covariance = sum(
            (base - base_mean) * (specular - specular_mean)
            for base, specular in zip(wet_base_green, wet_specular_red)
        ) / len(wet_base_green)
        self.assertLess(shared_field_covariance, -5.0)

        def normal_delta(left: tuple[int, ...], right: tuple[int, ...]) -> int:
            return sum(abs(left[channel] - right[channel]) for channel in range(3))

        horizontal_seam = sum(
            normal_delta(wet_normal[y * 512], wet_normal[y * 512 + 511])
            for y in range(512)
        ) / 512
        horizontal_neighbor = sum(
            normal_delta(wet_normal[y * 512 + x], wet_normal[y * 512 + x - 1])
            for y in range(512)
            for x in range(1, 512)
        ) / (512 * 511)
        vertical_seam = sum(
            normal_delta(wet_normal[x], wet_normal[511 * 512 + x])
            for x in range(512)
        ) / 512
        vertical_neighbor = sum(
            normal_delta(wet_normal[y * 512 + x], wet_normal[(y - 1) * 512 + x])
            for y in range(1, 512)
            for x in range(512)
        ) / (511 * 512)
        self.assertLess(horizontal_seam, horizontal_neighbor * 1.25)
        self.assertLess(vertical_seam, vertical_neighbor * 1.25)
        repeated_at_64 = sum(
            wet_normal[y * 512 + x]
            == wet_normal[y * 512 + ((x + 64) % 512)]
            for y in range(512)
            for x in range(512)
        ) / (512 * 512)
        self.assertLess(repeated_at_64, 0.1)

        materials = {material["id"]: material for material in manifest["materials"]}
        wet_roughness = materials["rorng_a0_wet_asphalt_material"]["roughness_factor"]
        self.assertEqual(wet_roughness, 0.08)
        self.assertGreater(min(dry_roughness) / 255.0, wet_roughness + 0.65)

        repeat_sampler = next(
            sampler
            for sampler in manifest["samplers"]
            if sampler["id"] == "rorng_a0_mipped_repeat_sampler"
        )
        self.assertEqual(repeat_sampler["maximum_lod"], 9.0)
        self.assertEqual(repeat_sampler["maximum_anisotropy"], 4.0)
        self.assertTrue(repeat_sampler["anisotropy_enabled"])
        self.assertEqual(
            struct.pack("<f", repeat_sampler["mip_lod_bias"]),
            b"\x00\x00\x00\x00",
        )

    def test_normal_and_optimized_compilers_are_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            normal_package = root / "normal.rornative"
            normal_report = root / "normal.compile.json"
            optimized_package = root / "optimized.rornative"
            optimized_report = root / "optimized.compile.json"
            normal = self.run_compiler(
                root,
                manifest,
                output=normal_package,
                report=normal_report,
            )
            optimized = self.run_compiler(
                root,
                manifest,
                optimized=True,
                output=optimized_package,
                report=optimized_report,
            )
            self.assertEqual(normal.returncode, 0, normal.stderr)
            self.assertEqual(optimized.returncode, 0, optimized.stderr)
            self.assertEqual(normal_package.read_bytes(), optimized_package.read_bytes())
            self.assertEqual(normal_report.read_bytes(), optimized_report.read_bytes())

    def test_container_header_records_hashes_and_direct_native_scope(self) -> None:
        package = (REPOSITORY_ROOT / PACKAGE_RELATIVE).read_bytes()
        (
            magic,
            version,
            header_bytes,
            flags,
            record_count,
            asset_count,
            instance_count,
            package_bytes,
            body_hash,
            reserved,
        ) = struct.unpack_from("<8sIIIIIIQ32s8s", package, 0)
        self.assertEqual(magic, b"RORNAT1\x00")
        self.assertEqual(
            (version, header_bytes, flags, record_count, asset_count, instance_count),
            (1, 80, 0, 27, 21, 5),
        )
        self.assertEqual(package_bytes, len(package))
        self.assertEqual(body_hash, hashlib.sha256(package[80:]).digest())
        self.assertEqual(reserved, b"\x00" * 8)
        offset = 80
        records: list[tuple[int, int]] = []
        while offset < len(package):
            record_type, record_flags, source_id, payload_bytes = struct.unpack_from(
                "<IIQQ", package, offset
            )
            self.assertEqual(record_flags, 0)
            records.append((record_type, source_id))
            offset += 24 + payload_bytes
        self.assertEqual(offset, len(package))
        self.assertEqual(records[0], (1, 0))
        self.assertEqual([record[0] for record in records[1:22]].count(2), 5)
        self.assertEqual([record[0] for record in records[1:22]].count(3), 10)
        self.assertEqual([record[0] for record in records[1:22]].count(4), 4)
        self.assertEqual([record[0] for record in records[1:22]].count(5), 2)
        self.assertEqual([record[0] for record in records[22:]], [6] * 5)
        self.assertEqual(
            [record[1] for record in records[1:22]],
            sorted(record[1] for record in records[1:22]),
        )
        self.assertEqual(
            [record[1] for record in records[22:]],
            sorted(record[1] for record in records[22:]),
        )
        for forbidden in (b".mesh", b".material", b".odef", b"RenderAssetDelta"):
            self.assertNotIn(forbidden, package)

    def test_a0_origin_and_bounded_nonclaims_are_explicit(self) -> None:
        manifest = json.loads(
            (REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="utf-8")
        )
        self.assertEqual(manifest["package"]["origin_class"], "project_original")
        self.assertEqual(manifest["package"]["license"], "GPL-3.0-or-later")
        self.assertIn(
            "no geometry, texture, material-script, or shader bytes were copied",
            manifest["package"]["creation_attestation"].lower(),
        )
        self.assertEqual(
            manifest["claims"],
            {
                "ambient_occlusion": False,
                "collision": False,
                "lods": False,
                "native_terrain": False,
                "visual_only": True,
            },
        )
        self.assertEqual(manifest["package"]["dimensions_m"], [6.0, 1.45, 12.0])
        self.assertTrue(all("sampler" in binding for material in manifest["materials"] for binding in material["textures"].values()))
        roles = {texture["role"]: texture["color_space"] for texture in manifest["textures"]}
        self.assertEqual(roles["base_color"], "srgb")
        self.assertEqual(roles["emissive"], "srgb")
        for role in ("normal", "metallic_roughness", "specular"):
            self.assertEqual(roles[role], "linear")
        materials = {material["id"]: material for material in manifest["materials"]}
        rough = materials["rorng_a0_road_surface_material"]
        wet = materials["rorng_a0_wet_asphalt_material"]
        reflector = materials["rorng_a0_reflector_material"]
        self.assertEqual(rough["workflow"], "metallic_roughness")
        self.assertIn("metallic_roughness", rough["textures"])
        self.assertIn("normal", rough["textures"])
        self.assertEqual(wet["workflow"], "specular")
        self.assertEqual(wet["roughness_factor"], 0.08)
        self.assertIn("normal", wet["textures"])
        self.assertIn("specular", wet["textures"])
        self.assertGreater(reflector["emissive_strength"], 1.0)
        shadow_gate = next(
            mesh for mesh in manifest["meshes"]
            if mesh["id"] == "rorng_a0_road_shadow_gate_mesh"
        )
        self.assertEqual(shadow_gate["material"], "rorng_a0_road_surface_material")
        instance_flags = {
            mesh["id"]: mesh["instance_flags"] for mesh in manifest["meshes"]
        }
        self.assertEqual(instance_flags["rorng_a0_lane_decal_mesh"], [])
        self.assertEqual(
            instance_flags["rorng_a0_reflector_mesh"],
            [],
        )
        self.assertEqual(
            instance_flags["rorng_a0_road_shadow_gate_mesh"],
            ["casts_shadow", "receives_shadow", "visible_in_reflections"],
        )
        for receiver in (
            "rorng_a0_road_surface_mesh",
            "rorng_a0_wet_asphalt_mesh",
        ):
            self.assertEqual(
                instance_flags[receiver],
                ["receives_shadow", "visible_in_reflections"],
            )

    def test_forward_native_ledger_binds_current_a0_source_and_package(self) -> None:
        report = json.loads(
            (REPOSITORY_ROOT / REPORT_RELATIVE).read_text(encoding="utf-8")
        )
        ledger = LEDGER.read_text(encoding="utf-8")
        self.assertIn("NATIVE-A0-001", ledger)
        self.assertIn("project_original", ledger)
        self.assertIn(report["source"]["manifest_sha256"], ledger)
        self.assertIn(report["source"]["glb"]["sha256"], ledger)
        self.assertIn(report["source"]["composition"]["sha256"], ledger)
        composition = json.loads(
            (
                REPOSITORY_ROOT
                / report["source"]["composition"]["path"]
            ).read_text(encoding="utf-8")
        )
        self.assertIn(composition["preview"]["sha256"], ledger)
        self.assertIn(report["output"]["sha256"], ledger)
        for nonclaim in ("No AO", "LOD", "collision", "native-terrain"):
            self.assertIn(nonclaim, ledger)

    def test_build_wiring_keeps_decoder_in_shipping_direct_contracts(self) -> None:
        main_cmake = (
            REPOSITORY_ROOT / "source/main/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        tests_cmake = (REPOSITORY_ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        embedded_cmake = (
            REPOSITORY_ROOT / "cmake/ogre_next_embedded/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        probe_cmake = (
            REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        umbrella = (
            REPOSITORY_ROOT / "source/main/gfx/render/RenderContracts.h"
        ).read_text(encoding="utf-8")
        self.assertIn("gfx/render/NativeRenderAssetPackage.{h,cpp}", main_cmake)
        self.assertGreaterEqual(
            main_cmake.count("gfx/render/NativeRenderAssetPackage.cpp"), 2
        )
        self.assertIn(
            "source/main/gfx/render/NativeRenderAssetPackage.cpp", tests_cmake
        )
        self.assertIn("ror_native_render_asset_package_tests", tests_cmake)
        self.assertIn("ROR_NATIVE_RENDER_ASSET_FIXTURE", tests_cmake)
        self.assertIn("NativeRenderAssetPackage.cpp", embedded_cmake)
        self.assertGreaterEqual(probe_cmake.count("NativeRenderAssetPackage.cpp"), 2)
        self.assertIn('#include "NativeRenderAssetPackage.h"', umbrella)

    def test_checked_validation_is_read_only_and_stale_output_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root, include_checked=True)
            before = {
                path.relative_to(root).as_posix(): path.read_bytes()
                for path in root.rglob("*")
                if path.is_file()
            }
            valid = self.run_compiler(root, manifest, validate_checked=True)
            self.assertEqual(valid.returncode, 0, valid.stderr)
            after = {
                path.relative_to(root).as_posix(): path.read_bytes()
                for path in root.rglob("*")
                if path.is_file()
            }
            self.assertEqual(after, before)
            package = root / PACKAGE_RELATIVE
            stale_bytes = bytearray(package.read_bytes())
            stale_bytes[-1] ^= 1
            package.write_bytes(stale_bytes)
            stale = self.run_compiler(root, manifest, validate_checked=True)
            self.assertNotEqual(stale.returncode, 0)
            self.assertIn("checked package is stale", stale.stderr)

    def test_manifest_parser_rejects_duplicate_noncanonical_unknown_and_nonfinite(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            original = manifest.read_text(encoding="utf-8")
            manifest.write_text(original.replace('{\n  "claims"', '{\n  "format": "ror-native-render-source-v1",\n  "claims"', 1), encoding="utf-8")
            result, report = self.run_validator(root, manifest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("MANIFEST_INVALID", self.codes(report))

            manifest.write_text(original.replace("  \"claims\"", " \"claims\"", 1), encoding="utf-8")
            result, report = self.run_validator(root, manifest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("MANIFEST_NONCANONICAL", self.codes(report))

            manifest.write_text(original, encoding="utf-8")
            self.rewrite_manifest(manifest, lambda value: value.update({"unknown": 1}))
            result, report = self.run_validator(root, manifest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("FIELD_UNKNOWN", self.codes(report))

            manifest.write_text(original.replace("1.45", "NaN", 1), encoding="utf-8")
            result, report = self.run_validator(root, manifest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("MANIFEST_INVALID", self.codes(report))

    def test_paths_hashes_origins_and_scope_fail_closed(self) -> None:
        mutations: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
            (
                "path traversal",
                lambda value: value["source"]["glb"].update({"path": "../escape.glb"}),
                "PATH_INVALID",
            ),
            (
                "leading dot path segment",
                lambda value: value["source"]["glb"].update(
                    {"path": "./ntent-source/native_render/fixture.glb"}
                ),
                "PATH_INVALID",
            ),
            (
                "embedded dot path segment",
                lambda value: value["source"]["glb"].update(
                    {"path": "content-source/./native_render/fixture.glb"}
                ),
                "PATH_INVALID",
            ),
            (
                "empty path segment",
                lambda value: value["source"]["glb"].update(
                    {"path": "content-source//native_render/fixture.glb"}
                ),
                "PATH_INVALID",
            ),
            (
                "absolute path",
                lambda value: value["source"]["glb"].update(
                    {"path": "/content-source/native_render/fixture.glb"}
                ),
                "PATH_INVALID",
            ),
            (
                "backslash path",
                lambda value: value["source"]["glb"].update(
                    {"path": "content-source\\native_render/fixture.glb"}
                ),
                "PATH_INVALID",
            ),
            (
                "source hash",
                lambda value: value["source"]["glb"].update({"sha256": "0" * 64}),
                "SOURCE_HASH_MISMATCH",
            ),
            (
                "origin",
                lambda value: value["package"].update({"origin_class": "downloaded"}),
                "ORIGIN_CLASS_INVALID",
            ),
            (
                "AO claim",
                lambda value: value["claims"].update({"ambient_occlusion": True}),
                "V1_SCOPE_CLAIM",
            ),
            (
                "LOD claim",
                lambda value: value["claims"].update({"lods": True}),
                "V1_SCOPE_CLAIM",
            ),
            (
                "legacy output",
                lambda value: value["outputs"].update({"package_path": "bad.mesh"}),
                "OUTPUT_PATH_INVALID",
            ),
            (
                "unknown instance flag",
                lambda value: value["meshes"][0].update(
                    {"instance_flags": ["opaque_rt_participant"]}
                ),
                "INSTANCE_FLAGS_INVALID",
            ),
            (
                "duplicate instance flag",
                lambda value: value["meshes"][1].update(
                    {"instance_flags": ["casts_shadow", "casts_shadow"]}
                ),
                "INSTANCE_FLAGS_INVALID",
            ),
            (
                "unsorted instance flags",
                lambda value: value["meshes"][2].update(
                    {
                        "instance_flags": [
                            "visible_in_reflections",
                            "casts_shadow",
                        ]
                    }
                ),
                "INSTANCE_FLAGS_INVALID",
            ),
        ]
        for label, mutate, code in mutations:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.copy_sources(root)
                self.rewrite_manifest(manifest, mutate)
                result, report = self.run_validator(root, manifest)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(code, self.codes(report))
                compiled = self.run_compiler(root, manifest)
                self.assertNotEqual(compiled.returncode, 0)

    def test_texture_sampler_material_and_reference_semantics_fail_closed(self) -> None:
        mutations: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
            (
                "base gamma",
                lambda value: value["textures"][0].update({"color_space": "linear"}),
                "TEXTURE_COLOR_SPACE",
            ),
            (
                "sampler enum",
                lambda value: value["samplers"][0].update({"mip_filter": "implicit"}),
                "ENUM_INVALID",
            ),
            (
                "sampler order",
                lambda value: value["samplers"].reverse(),
                "ORDER_INVALID",
            ),
            (
                "dangling texture",
                lambda value: value["materials"][0]["textures"]["base_color"].update({"texture": "rorng_missing"}),
                "REFERENCE_MISSING",
            ),
            (
                "wrong role",
                lambda value: value["materials"][2]["textures"]["normal"].update({"texture": "rorng_a0_road_base"}),
                "TEXTURE_ROLE_MISMATCH",
            ),
            (
                "workflow conflict",
                lambda value: value["materials"][1].update({"workflow": "metallic_roughness"}),
                "MATERIAL_WORKFLOW",
            ),
            (
                "alpha binding",
                lambda value: value["materials"][0].update({"textures": {}}),
                "MATERIAL_ALPHA",
            ),
            (
                "incomplete mips",
                lambda value: value["textures"][4].update({"mips": value["textures"][4]["mips"][:1]}),
                "TEXTURE_MIP_CHAIN",
            ),
        ]
        for label, mutate, code in mutations:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.copy_sources(root)
                self.rewrite_manifest(manifest, mutate)
                result, report = self.run_validator(root, manifest)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(code, self.codes(report))

    def test_tga_profile_and_symlink_sources_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            value = json.loads(manifest.read_text(encoding="utf-8"))
            mip = value["textures"][0]["mips"][0]
            path = root / mip["path"]
            data = bytearray(path.read_bytes())
            data[17] = 0
            path.write_bytes(data)
            mip["sha256"] = sha256_file(path)
            write_canonical_pretty(manifest, value)
            result, report = self.run_validator(root, manifest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("TEXTURE_SOURCE_INVALID", self.codes(report))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            value = json.loads(manifest.read_text(encoding="utf-8"))
            glb = root / value["source"]["glb"]["path"]
            replacement = glb.with_suffix(".real.glb")
            glb.rename(replacement)
            try:
                glb.symlink_to(replacement.name)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")
            result, report = self.run_validator(root, manifest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("SOURCE_UNREADABLE", self.codes(report))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            alias = manifest.with_name("alias.native.json")
            try:
                alias.symlink_to(manifest.name)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")
            result, report = self.run_validator(root, alias)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("MANIFEST_INVALID", self.codes(report))
            compiled = self.run_compiler(root, alias)
            self.assertNotEqual(compiled.returncode, 0)

    def test_output_overrides_reject_wrong_suffix_and_symlink_components(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            wrong_suffix = self.run_compiler(
                root,
                manifest,
                output=root / "native-package.bin",
                report=root / "native-package.compile.json",
            )
            self.assertNotEqual(wrong_suffix.returncode, 0)
            self.assertIn("must end with .rornative", wrong_suffix.stderr)

            real = root / "real-output"
            real.mkdir()
            alias = root / "linked-output"
            try:
                alias.symlink_to(real, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")
            linked = self.run_compiler(
                root,
                manifest,
                output=alias / "native-package.rornative",
                report=root / "native-package.compile.json",
            )
            self.assertNotEqual(linked.returncode, 0)
            self.assertIn("must not traverse a symlink", linked.stderr)

    def test_glb_extensions_layout_nonfinite_indices_and_transforms_fail_closed(self) -> None:
        def extension(document: dict[str, Any], _binary: bytearray) -> None:
            document["extensionsUsed"] = ["KHR_materials_unlit"]

        def nonfinite(document: dict[str, Any], binary: bytearray) -> None:
            accessor_index = document["meshes"][0]["primitives"][0]["attributes"]["POSITION"]
            accessor = document["accessors"][accessor_index]
            view = document["bufferViews"][accessor["bufferView"]]
            struct.pack_into("<I", binary, view["byteOffset"], 0x7FC00000)

        def bad_index(document: dict[str, Any], binary: bytearray) -> None:
            accessor_index = document["meshes"][0]["primitives"][0]["indices"]
            accessor = document["accessors"][accessor_index]
            view = document["bufferViews"][accessor["bufferView"]]
            struct.pack_into("<H", binary, view["byteOffset"], 65535)

        def transform(document: dict[str, Any], _binary: bytearray) -> None:
            document["nodes"][0]["translation"] = [0.0, 0.0, 0.0]

        mutations = [
            ("extension", extension, True, "GLB_DOCUMENT_PROFILE"),
            ("noncanonical", lambda _d, _b: None, False, "GLB_JSON_NONCANONICAL"),
            ("nonfinite", nonfinite, True, "GLB_VERTEX_NONFINITE"),
            ("index", bad_index, True, "GLB_INDEX_RANGE"),
            ("transform", transform, True, "GLB_NODE_PROFILE"),
        ]
        for label, mutate, canonical, code in mutations:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.copy_sources(root)
                value = json.loads(manifest.read_text(encoding="utf-8"))
                glb = root / value["source"]["glb"]["path"]
                document, binary = parse_glb(glb)
                mutate(document, binary)
                if any(
                    isinstance(buffer, dict) for buffer in document.get("buffers", [])
                ):
                    document["buffers"][0]["byteLength"] = len(binary.rstrip(b"\x00"))
                    # The checked binary already has no semantic end padding; preserve its
                    # exact declared length for mutations that do not resize it.
                    document["buffers"][0]["byteLength"] = parse_glb(
                        REPOSITORY_ROOT
                        / json.loads((REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text())["source"]["glb"]["path"]
                    )[0]["buffers"][0]["byteLength"]
                write_glb(glb, document, binary, canonical=canonical)
                value["source"]["glb"]["sha256"] = sha256_file(glb)
                write_canonical_pretty(manifest, value)
                result, report = self.run_validator(root, manifest)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(code, self.codes(report))

    def test_instance_policy_is_exactly_rt_inert_receivers_and_gate_caster(self) -> None:
        package = (REPOSITORY_ROOT / PACKAGE_RELATIVE).read_bytes()
        offset = 80
        flags: list[int] = []
        while offset < len(package):
            record_type, _record_flags, _source_id, payload_bytes = struct.unpack_from(
                "<IIQQ", package, offset
            )
            payload_start = offset + 24
            if record_type == 6:
                self.assertEqual(payload_bytes, 92)
                flags.append(struct.unpack_from("<I", package, payload_start + 88)[0])
            offset = payload_start + payload_bytes
        self.assertEqual(sorted(flags), [0, 0, 6, 6, 7])
        self.assertEqual(set(flags), {0, 6, 7})

    def test_uv_derived_tangent_u_direction_and_handedness_fail_closed(self) -> None:
        def mutate_tangent(
            root: Path,
            manifest: Path,
            mutation: Callable[[tuple[float, float, float, float]], tuple[float, float, float, float]],
        ) -> None:
            value = json.loads(manifest.read_text(encoding="utf-8"))
            glb_path = root / value["source"]["glb"]["path"]
            document, binary = parse_glb(glb_path)
            accessor_index = document["meshes"][0]["primitives"][0]["attributes"]["TANGENT"]
            accessor = document["accessors"][accessor_index]
            view = document["bufferViews"][accessor["bufferView"]]
            offset = view["byteOffset"]
            authored = struct.unpack_from("<4f", binary, offset)
            struct.pack_into("<4f", binary, offset, *mutation(authored))
            write_glb(glb_path, document, binary)
            value["source"]["glb"]["sha256"] = sha256_file(glb_path)
            write_canonical_pretty(manifest, value)

        cases = (
            (
                "opposite U",
                lambda tangent: tuple(
                    -component if component != 0.0 else 0.0
                    for component in tangent[:3]
                )
                + (tangent[3],),
                "GLB_TANGENT_U_DIRECTION",
            ),
            (
                "flipped handedness",
                lambda tangent: (tangent[0], tangent[1], tangent[2], -tangent[3]),
                "GLB_TANGENT_HANDEDNESS",
            ),
        )
        for label, mutation, expected in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.copy_sources(root)
                mutate_tangent(root, manifest, mutation)
                for optimized in (False, True):
                    result, report = self.run_validator(
                        root, manifest, optimized=optimized
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected, self.codes(report))

    def test_triangle_winding_must_match_every_authored_vertex_normal(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            value = json.loads(manifest.read_text(encoding="utf-8"))
            glb_path = root / value["source"]["glb"]["path"]
            document, binary = parse_glb(glb_path)
            primitive = document["meshes"][0]["primitives"][0]
            accessor = document["accessors"][primitive["indices"]]
            view = document["bufferViews"][accessor["bufferView"]]
            index_format = {5123: "H", 5125: "I"}[accessor["componentType"]]
            first, second, third = struct.unpack_from(
                f"<3{index_format}", binary, view["byteOffset"]
            )
            struct.pack_into(
                f"<3{index_format}",
                binary,
                view["byteOffset"],
                first,
                third,
                second,
            )
            write_glb(glb_path, document, binary)
            value["source"]["glb"]["sha256"] = sha256_file(glb_path)
            write_canonical_pretty(manifest, value)

            for optimized in (False, True):
                with self.subTest(optimized=optimized):
                    result, report = self.run_validator(
                        root, manifest, optimized=optimized
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("GLB_TRIANGLE_WINDING", self.codes(report))
                    compiled = self.run_compiler(
                        root, manifest, optimized=optimized
                    )
                    self.assertNotEqual(compiled.returncode, 0)
                    self.assertIn("GLB_TRIANGLE_WINDING", compiled.stderr)

    def test_transform_determinant_uses_ordered_binary32_threshold(self) -> None:
        # Each JSON component first becomes binary32. Binary64 evaluation of
        # this diagonal determinant is 1.0000000168623835e-8 and the old
        # validator accepted it. RenderMath's ordered binary32 multiplies yield
        # exactly 0x322bcc77 (1e-8F), which is on the rejected threshold.
        near_threshold = [
            100000000.0,
            0.0,
            0.0,
            0.0,
            0.0,
            1.0e-16,
            0.0,
            0.0,
            0.0,
            0.0,
            1.0,
            0.0,
            0.0,
            0.0,
            0.0,
            1.0,
        ]
        binary32_scale = struct.unpack("<f", struct.pack("<f", 1.0e-16))[0]
        self.assertGreater(100000000.0 * binary32_scale, 1.0e-8)
        determinant32 = struct.unpack(
            "<f", struct.pack("<f", 100000000.0 * binary32_scale)
        )[0]
        threshold32 = struct.unpack("<f", struct.pack("<f", 1.0e-8))[0]
        self.assertEqual(determinant32, threshold32)
        self.assertEqual(struct.pack("<f", determinant32).hex(), "77cc2b32")

        for optimized in (False, True):
            with self.subTest(optimized=optimized), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.copy_sources(root)
                self.rewrite_manifest(
                    manifest,
                    lambda value: value["meshes"][0].update(
                        {"render_from_object": near_threshold}
                    ),
                )
                result, report = self.run_validator(
                    root, manifest, optimized=optimized
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("TRANSFORM_INVALID", self.codes(report))
                compiled = self.run_compiler(root, manifest, optimized=optimized)
                self.assertNotEqual(compiled.returncode, 0)
                self.assertIn("TRANSFORM_INVALID", compiled.stderr)

                next_binary32 = struct.unpack(
                    "<f", struct.pack("<I", 0x24E69596)
                )[0]
                accepted = list(near_threshold)
                accepted[5] = next_binary32
                self.rewrite_manifest(
                    manifest,
                    lambda value: value["meshes"][0].update(
                        {"render_from_object": accepted}
                    ),
                )
                result, report = self.run_validator(
                    root, manifest, optimized=optimized
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(report["summary"]["valid"])
                compiled = self.run_compiler(root, manifest, optimized=optimized)
                self.assertEqual(compiled.returncode, 0, compiled.stderr)

    def test_binary32_numbers_negative_zero_and_material_invariants_fail_stably(self) -> None:
        cases: tuple[tuple[str, Callable[[dict[str, Any]], None], str], ...] = (
            (
                "overflow exponent",
                lambda value: value["materials"][0].update(
                    {"roughness_factor": 1e300}
                ),
                "NUMBER_FLOAT32",
            ),
            (
                "four thousand digit integer",
                lambda value: value["samplers"][0].update(
                    {"maximum_anisotropy": 10**3999}
                ),
                "NUMBER_FLOAT32",
            ),
            (
                "negative zero",
                lambda value: value["samplers"][0].update(
                    {"mip_lod_bias": -0.0}
                ),
                "NUMBER_NEGATIVE_ZERO",
            ),
            (
                "binary32 underflow scale",
                lambda value: value["materials"][0]["textures"]["base_color"].update(
                    {"scale": [1e-50, 1.0]}
                ),
                "NUMBER_FLOAT32",
            ),
            (
                "specular workflow metallic factor",
                lambda value: value["materials"][1].update(
                    {"metallic_factor": 0.25}
                ),
                "MATERIAL_WORKFLOW",
            ),
            (
                "metallic roughness unused specular factor",
                lambda value: value["materials"][2].update(
                    {"specular_factor": [0.9, 1.0, 1.0]}
                ),
                "MATERIAL_WORKFLOW",
            ),
        )
        for label, mutate, expected in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.copy_sources(root)
                self.rewrite_manifest(manifest, mutate)
                diagnostics: list[list[dict[str, str]]] = []
                for optimized in (False, True):
                    result, report = self.run_validator(
                        root, manifest, optimized=optimized
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected, self.codes(report))
                    diagnostics.append(report["diagnostics"])
                    compiled = self.run_compiler(
                        root, manifest, optimized=optimized
                    )
                    self.assertNotEqual(compiled.returncode, 0)
                    self.assertIn(expected, compiled.stderr)
                self.assertEqual(diagnostics[0], diagnostics[1])

    def test_aggregate_asset_and_declared_texture_working_set_preflight(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            self.rewrite_manifest(
                manifest,
                lambda value: value["textures"].extend([None] * 4076),
            )
            result, report = self.run_validator(root, manifest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("ASSET_LIMIT_EXCEEDED", self.codes(report))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = self.copy_sources(root)
            value = json.loads(manifest.read_text(encoding="utf-8"))
            prototype = value["textures"][0]["mips"][0]
            dimensions: list[tuple[int, int]] = []
            width = height = 16384
            while True:
                dimensions.append((width, height))
                if width == 1 and height == 1:
                    break
                width, height = max(1, width // 2), max(1, height // 2)
            value["textures"][0]["mips"] = [
                {
                    "height": mip_height,
                    "path": prototype["path"],
                    "sha256": prototype["sha256"],
                    "width": mip_width,
                }
                for mip_width, mip_height in dimensions
            ]
            write_canonical_pretty(manifest, value)
            result, report = self.run_validator(root, manifest)
            self.assertNotEqual(result.returncode, 0)
            codes = self.codes(report)
            self.assertIn("TEXTURE_WORKING_SET_EXCEEDED", codes)
            # The declared cumulative source+RGBA budget is rejected before the
            # first hostile texture path is opened or decoded.
            self.assertNotIn("SOURCE_UNREADABLE", codes)
            self.assertNotIn("SOURCE_HASH_MISMATCH", codes)
            self.assertNotIn("TEXTURE_SOURCE_INVALID", codes)

    def test_index_packer_is_chunked_and_binary32_compiler_guard_is_stable(self) -> None:
        script = f"""
import struct, sys
sys.path.insert(0, {str((REPOSITORY_ROOT / 'tools')).__repr__()})
from compile_native_render_asset import CompileFailure, canonical_float, pack_uint32_values
values = list(range(40000))
packed = pack_uint32_values(values)
assert len(packed) == len(values) * 4
assert struct.unpack_from('<I', packed, 39999 * 4)[0] == 39999
for value in (-0.0, 1e300, 10**3999):
    try:
        canonical_float(value)
    except CompileFailure:
        pass
    else:
        raise AssertionError('non-canonical binary32 passed')
"""
        result = subprocess.run(
            [sys.executable, "-c", script],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        compiler_source = COMPILER.read_text(encoding="utf-8")
        self.assertIn("len(chunk) == 16384", compiler_source)
        self.assertIn("except MemoryError:", compiler_source)
        self.assertIn("except (OverflowError, struct.error):", compiler_source)

    def test_checked_composition_has_normalized_sun_exact_shadow_and_camera_framing(self) -> None:
        manifest = json.loads(
            (REPOSITORY_ROOT / MANIFEST_RELATIVE).read_text(encoding="utf-8")
        )
        composition_path = REPOSITORY_ROOT / manifest["source"]["composition"]["path"]
        composition = json.loads(composition_path.read_text(encoding="utf-8"))
        self.assertEqual(composition["format"], "ror-native-render-composition-v1")
        self.assertEqual(composition["package_id"], manifest["package"]["id"])
        self.assertEqual(
            sha256_file(composition_path), manifest["source"]["composition"]["sha256"]
        )
        direction = composition["sun"]["direction_toward_scene"]
        self.assertAlmostEqual(sum(component * component for component in direction), 1.0)
        self.assertLess(direction[1], 0.0)

        document, binary = parse_glb(REPOSITORY_ROOT / manifest["source"]["glb"]["path"])
        gate = next(
            mesh
            for mesh in document["meshes"]
            if mesh["name"] == "rorng_a0_road_shadow_gate_mesh"
        )
        position_accessor_index = gate["primitives"][0]["attributes"]["POSITION"]
        accessor = document["accessors"][position_accessor_index]
        view = document["bufferViews"][accessor["bufferView"]]
        positions = [
            struct.unpack_from("<3f", binary, view["byteOffset"] + index * 12)
            for index in range(accessor["count"])
        ]
        gate_min = tuple(min(position[axis] for position in positions) for axis in range(3))
        gate_max = tuple(max(position[axis] for position in positions) for axis in range(3))
        receiver_y = composition["shadow_roi"]["receiver_surface_y_m"]
        projected = []
        for x in (gate_min[0], gate_max[0]):
            for y in (gate_min[1], gate_max[1]):
                for z in (gate_min[2], gate_max[2]):
                    parameter = (receiver_y - y) / direction[1]
                    projected.append(
                        (x + parameter * direction[0], z + parameter * direction[2])
                    )
        expected_min = [min(point[axis] for point in projected) for axis in range(2)]
        expected_max = [max(point[axis] for point in projected) for axis in range(2)]
        for actual, expected in zip(composition["shadow_roi"]["minimum_xz_m"], expected_min):
            self.assertAlmostEqual(actual, expected, places=5)
        for actual, expected in zip(composition["shadow_roi"]["maximum_xz_m"], expected_max):
            self.assertAlmostEqual(actual, expected, places=5)

        preview = composition["preview"]
        preview_bytes = (REPOSITORY_ROOT / preview["path"]).read_bytes()
        self.assertEqual(hashlib.sha256(preview_bytes).hexdigest(), preview["sha256"])
        header = f"P6\n{preview['width']} {preview['height']}\n255\n".encode("ascii")
        self.assertTrue(preview_bytes.startswith(header))
        self.assertEqual(len(preview_bytes) - len(header), preview["width"] * preview["height"] * 3)

        camera = composition["camera"]
        position = tuple(camera["position_m"])
        target = tuple(camera["target_m"])
        up = tuple(camera["up"])
        normalize = lambda vector: tuple(
            component / math.sqrt(sum(value * value for value in vector))
            for component in vector
        )
        dot = lambda left, right: sum(a * b for a, b in zip(left, right))
        cross = lambda left, right: (
            left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0],
        )
        forward = normalize(tuple(target[axis] - position[axis] for axis in range(3)))
        right = normalize(cross(forward, up))
        actual_up = cross(right, forward)
        tangent = math.tan(math.radians(camera["vertical_fov_degrees"]) * 0.5)
        aspect = preview["width"] / preview["height"]
        world = composition["world_aabb"]
        for x in (world["minimum_m"][0], world["maximum_m"][0]):
            for y in (world["minimum_m"][1], world["maximum_m"][1]):
                for z in (world["minimum_m"][2], world["maximum_m"][2]):
                    relative = (x - position[0], y - position[1], z - position[2])
                    depth = dot(relative, forward)
                    self.assertGreaterEqual(depth, camera["near_clip_m"])
                    self.assertLessEqual(depth, camera["far_clip_m"])
                    self.assertLessEqual(abs(dot(relative, right) / (depth * tangent * aspect)), 1.0)
                    self.assertLessEqual(abs(dot(relative, actual_up) / (depth * tangent)), 1.0)

    def test_composition_sun_shadow_framing_and_preview_mutations_fail_closed(self) -> None:
        def mutate_descriptor(
            root: Path,
            manifest_path: Path,
            mutate: Callable[[dict[str, Any], Path], None],
        ) -> None:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            composition_path = root / manifest["source"]["composition"]["path"]
            composition = json.loads(composition_path.read_text(encoding="utf-8"))
            mutate(composition, root)
            write_canonical_pretty(composition_path, composition)
            self.refresh_composition_hash(manifest_path)

        def corrupt_preview(composition: dict[str, Any], root: Path) -> None:
            preview_path = root / composition["preview"]["path"]
            payload = preview_path.read_bytes()
            header_end = payload.index(b"\n255\n") + len(b"\n255\n")
            preview_path.write_bytes(payload[:header_end] + b"\x00" * (len(payload) - header_end))
            composition["preview"]["sha256"] = sha256_file(preview_path)

        cases: tuple[
            tuple[str, Callable[[dict[str, Any], Path], None], str], ...
        ] = (
            (
                "unnormalized sun",
                lambda value, _root: value["sun"].update(
                    {"direction_toward_scene": [0.6, -0.63, 0.48]}
                ),
                "COMPOSITION_SUN_DIRECTION",
            ),
            (
                "shadow projection mismatch",
                lambda value, _root: value["shadow_roi"].update(
                    {"maximum_xz_m": [3.0, -0.2625]}
                ),
                "COMPOSITION_SHADOW_GEOMETRY",
            ),
            (
                "camera misses world",
                lambda value, _root: value["camera"].update(
                    {"position_m": [0.0, 0.1, 0.0]}
                ),
                "COMPOSITION_CAMERA_FRAMING",
            ),
            ("preview content", corrupt_preview, "COMPOSITION_PREVIEW_CONTENT"),
        )
        for label, mutate, expected in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                manifest = self.copy_sources(root)
                mutate_descriptor(root, manifest, mutate)
                result, report = self.run_validator(root, manifest)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, self.codes(report))

    def test_forward_native_gitattributes_pin_text_lf_and_binary_payloads(self) -> None:
        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(encoding="utf-8")
        required = {
            "tools/blender/native_render/*.py text eol=lf",
            "tools/compile_native_render_asset.py text eol=lf",
            "tools/validate_native_render_asset.py text eol=lf",
            "content-source/native_render/**/*.json text eol=lf",
            "content-source/native_render/**/*.glb -text",
            "content-source/native_render/**/*.tga -text",
            "content-source/native_render/**/*.ppm -text",
            "resources/nextgen/native/**/*.json text eol=lf",
            "resources/nextgen/native/**/*.rornative -text",
        }
        self.assertTrue(required.issubset(set(attributes.splitlines())))
        checked_paths = (
            MANIFEST_RELATIVE,
            Path("tools/compile_native_render_asset.py"),
            Path("content-source/native_render/a0_road_tile_12m/rorng_a0_road_tile_12m.glb"),
            PACKAGE_RELATIVE,
        )
        result = subprocess.run(
            ["git", "check-attr", "text", "eol", "--", *(path.as_posix() for path in checked_paths)],
            cwd=REPOSITORY_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertIn(f"{MANIFEST_RELATIVE.as_posix()}: text: set", lines)
        self.assertIn(f"{MANIFEST_RELATIVE.as_posix()}: eol: lf", lines)
        self.assertIn(f"{PACKAGE_RELATIVE.as_posix()}: text: unset", lines)


if __name__ == "__main__":
    unittest.main()
