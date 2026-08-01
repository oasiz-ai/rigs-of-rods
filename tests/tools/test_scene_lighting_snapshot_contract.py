#!/usr/bin/env python3
"""Static contract gates for renderer-neutral lighting snapshot publication."""

from __future__ import annotations

import pathlib
import re
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
RENDER_ROOT = REPOSITORY_ROOT / "source" / "main" / "gfx" / "render"


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

    def test_versioned_photometric_schema_is_explicit(self) -> None:
        for token in (
            "kSceneSnapshotVersion = 3U",
            "kSceneLightingHashVersion = 1U",
            "struct AnalyticSkyDescriptor",
            "sun_light_id",
            "sun_disk_radiance",
            "exposure_compensation_ev",
            "LIGHT_SHADOW_STATIC_GEOMETRY",
            "LIGHT_SHADOW_DYNAMIC_GEOMETRY",
            "previous_position",
            "previous_direction",
            "lighting_environment_hash",
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
            "AddU64(static_cast<std::uint64_t>(descriptor.lights.size()))",
        ):
            self.assertIn(token, self.scene_source)
        self.assertNotRegex(
            self.scene_source,
            re.compile(r"reinterpret_cast\s*<[^>]*char[^>]*>"),
            "hash must not consume compiler-dependent structure bytes",
        )

    def test_producer_canonicalizes_history_and_publishes_release_acquire(self) -> None:
        for token in (
            "kGraphicsSceneSnapshotProducerVersion = 2U",
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
