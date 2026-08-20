#!/usr/bin/env python3
"""Static contract gates for renderer-neutral lighting snapshot publication."""

from __future__ import annotations

import pathlib
import re
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source" / "main" / "gfx" / "render"
SCENE_LIGHTING_SCHEMA_INTRODUCED = 3
PRODUCER_LIGHTING_SCHEMA_INTRODUCED = 2


class SceneLightingSnapshotContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.scene_header = (RENDER_ROOT / "SceneSnapshot.h").read_text(
            encoding="utf-8"
        )
        cls.scene_source = (RENDER_ROOT / "SceneSnapshot.cpp").read_text(
            encoding="utf-8"
        )
        cls.producer_header = (
            RENDER_ROOT / "GraphicsSceneSnapshotProducer.h"
        ).read_text(encoding="utf-8")
        cls.producer_source = (
            RENDER_ROOT / "GraphicsSceneSnapshotProducer.cpp"
        ).read_text(encoding="utf-8")

    def explicit_u32_constant(self, source: str, name: str) -> int:
        matches = re.findall(
            rf"^constexpr std::uint32_t {re.escape(name)} = ([0-9]+)U;$",
            source,
            flags=re.MULTILINE,
        )
        self.assertEqual(
            len(matches),
            1,
            f"{name} must have one explicit unsigned uint32 definition",
        )
        return int(matches[0])

    def test_versioned_photometric_schema_is_explicit(self) -> None:
        scene_version = self.explicit_u32_constant(
            self.scene_header, "kSceneSnapshotVersion"
        )
        self.assertGreaterEqual(
            scene_version,
            SCENE_LIGHTING_SCHEMA_INTRODUCED,
            "the scene schema cannot predate canonical analytic lighting",
        )
        self.assertIn(
            "std::uint32_t version = kSceneSnapshotVersion;",
            self.scene_header,
        )
        self.assertIn(
            "descriptor.version != kSceneSnapshotVersion",
            self.scene_source,
        )
        for token in (
            "kSceneLightingHashVersion = 4U",
            "struct AnalyticSkyDescriptor",
            "sun_light_id",
            "sun_disk_radiance",
            "cloud_coverage",
            "cloud_radiance",
            "cloud_phase_radians",
            "haze_extinction_per_meter",
            "haze_inverse_scale_height_per_meter",
            "haze_base_height_meters",
            "exposure_compensation_ev",
            "LIGHT_SHADOW_STATIC_GEOMETRY",
            "LIGHT_SHADOW_DYNAMIC_GEOMETRY",
            "previous_position",
            "previous_direction",
            "lighting_environment_hash",
            "kLinearSrgbRec709D65RedLuminance",
            "IsViewDirectionInsideAnalyticSunDisk",
            "ClassifyShadowGeometry",
            "ComputePortableEffectiveExposure",
        ):
            self.assertIn(token, self.scene_header)
        self.assertIn("illuminance in", self.scene_header)
        self.assertIn("luminous intensity in candela", self.scene_header)

    def test_hash_encoding_has_no_native_layout_dependency(self) -> None:
        for token in (
            "CanonicalLightingHasher",
            "kOffsetBasis = 14695981039346656037ULL",
            "kPrime = 1099511628211ULL",
            "std::memcpy(&bits, &value, sizeof(bits))",
            "if (value == 0.0F)",
            "AddAssetReference",
            "hasher.AddU64(descriptor.asset_registry_id)",
            "AddU64(static_cast<std::uint64_t>(descriptor.lights.size()))",
        ):
            self.assertIn(token, self.scene_source)
        self.assertNotRegex(
            self.scene_source,
            re.compile(r"reinterpret_cast\s*<[^>]*char[^>]*>"),
            "hash must not consume compiler-dependent structure bytes",
        )

    def test_photometry_sun_and_shadow_rules_are_canonical(self) -> None:
        for token in (
            "IsCanonicalPhotometricColorLinear(light.color_linear)",
            "kRoundsToOneMinimum",
            "kRoundsToOneMaximum",
            "sun.type != LightType::DIRECTIONAL",
            "center_dot_view >= boundary",
            "return mesh.dynamic ? ShadowGeometryClass::DYNAMIC",
            "MeshInstanceCastsShadowForLight",
        ):
            self.assertIn(token, self.scene_source)
        self.assertIn("origin_delta[0U] + static_cast<double>(previous.x)",
                      self.producer_source)
        self.assertIn("origin_delta[0U] +", self.producer_source)

    def test_producer_canonicalizes_history_and_publishes_release_acquire(self) -> None:
        producer_version = self.explicit_u32_constant(
            self.producer_header, "kGraphicsSceneSnapshotProducerVersion"
        )
        self.assertGreaterEqual(
            producer_version,
            PRODUCER_LIGHTING_SCHEMA_INTRODUCED,
            "the producer schema cannot predate canonical analytic lighting",
        )
        self.assertIn(
            "std::uint32_t version = kGraphicsSceneSnapshotProducerVersion;",
            self.producer_header,
        )
        self.assertIn(
            "frame.version != kGraphicsSceneSnapshotProducerVersion",
            self.producer_source,
        )
        for token in (
            "struct GraphicsSceneLightInput",
            "std::vector<GraphicsSceneLightInput> lights",
            "maximum_light_records",
            "LoadPublishedSnapshot",
        ):
            self.assertIn(token, self.producer_header)
        for token in (
            "std::sort(sorted_lights.begin()",
            "RebasePreviousPosition",
            "a destroyed source light identity may never be reused",
            "std::atomic_store_explicit(&published_snapshot, created.snapshot",
            "std::memory_order_release",
            "std::atomic_load_explicit(&impl_->published_snapshot",
            "std::memory_order_acquire",
        ):
            self.assertIn(token, self.producer_source)
        store = self.producer_source.index("std::atomic_store_explicit")
        success = self.producer_source.index(
            "result.validation = ValidationResult::Success()", store - 500
        )
        self.assertLess(success, store)

    def test_dependency_free_cpp_tests_run_on_all_supported_hosts(self) -> None:
        workflow = (
            REPOSITORY_ROOT / ".github" / "workflows" / "physics-core.yml"
        ).read_text(encoding="utf-8")
        for runner in ("ubuntu-22.04", "windows-2025", "macos-15"):
            self.assertIn(runner, workflow)
        self.assertIn("cmake -S tests", workflow)
        cmake = (REPOSITORY_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("graphics_scene_snapshot_producer", cmake)
        self.assertIn("render_scene_snapshot", cmake)
        self.assertIn("Threads::Threads", cmake)


if __name__ == "__main__":
    unittest.main()
