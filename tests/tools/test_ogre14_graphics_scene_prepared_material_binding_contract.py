#!/usr/bin/env python3
"""Static contract for immutable prepared-material scene binding."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = (
    ROOT
    / "source/main/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBinding.h"
)
SOURCE = (
    ROOT
    / "source/main/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBinding.cpp"
)
CPP_TEST = (
    ROOT
    / "tests/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBindingTests.cpp"
)
PATHS = (
    "source/main/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBinding.cpp",
    "source/main/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBinding.h",
    "tests/gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBindingTests.cpp",
    "tests/tools/test_ogre14_graphics_scene_prepared_material_binding_contract.py",
)


class Ogre14PreparedMaterialBindingContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")
        cls.cpp_test = CPP_TEST.read_text(encoding="utf-8")

    def test_binding_is_immutable_and_retains_exact_prepared_state(self) -> None:
        for token in (
            "class Ogre14GraphicsScenePreparedMaterialBinding final",
            "struct State;",
            "std::shared_ptr<const State> state_",
            "Ogre14LegacyPreparedMaterialFrame prepared_frame",
            "SharesImmutableStateWith(",
            "std::is_nothrow_copy_assignable<",
            "std::is_nothrow_move_assignable<",
        ):
            self.assertIn(token, self.header + self.source)

    def test_caps_and_prepared_owners_are_validated_before_section_copy(self) -> None:
        cap_index = self.source.index(
            "static_sections.size() > kMaximumOgre14GraphicsSceneStaticSections"
        )
        frame_index = self.source.index(
            "BuildPreparedMaterialIndex(prepared_frame, material_index)"
        )
        candidate_index = self.source.index(
            "std::make_shared<Ogre14GraphicsScenePreparedMaterialBinding::State>"
        )
        copy_index = self.source.index(
            "candidate->static_sections.push_back(input)"
        )
        self.assertLess(cap_index, candidate_index)
        self.assertLess(frame_index, candidate_index)
        self.assertLess(candidate_index, copy_index)
        for token in (
            "PreparedMaterialIndex",
            "ValidateOgre14LegacyMaterialPipelineAudit(",
            "ValidateOgre14LegacyMaterialClosureForFrame(",
            "EquivalentOgre14LegacyMaterialPipelineAudit(",
            "SharesControlBlock(",
            "prepared frame repeats one exact material key",
        ):
            self.assertIn(token, self.source)
        self.assertNotIn("FindOgre14LegacyPreparedMaterial(", self.source)

    def test_only_canonical_exact_closures_or_unchanged_fallbacks_publish(self) -> None:
        for token in (
            "caller supplied a closure before authenticated binding",
            "BuildOgre14GraphicsSceneMaterialFallback(",
            "static mesh conversion does not match exact native cull",
            "dynamic mesh conversion does not match exact native cull",
            "section.resolved_material = material->closure",
            "ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage(",
            "output = Ogre14GraphicsScenePreparedMaterialBinding",
            "catch (const std::bad_alloc &)",
            "catch (...) ",
        ):
            self.assertIn(token, self.source)
        lineage_index = self.source.index(
            "ValidateOgre14GraphicsSceneResolvedMaterialFrameLineage("
        )
        publish_index = self.source.index(
            "output = Ogre14GraphicsScenePreparedMaterialBinding"
        )
        self.assertLess(lineage_index, publish_index)

    def test_runtime_gate_covers_join_and_transactional_failure(self) -> None:
        for token in (
            "bound sections did not reuse canonical closure and payload owners",
            "binding did not retain the exact committable prepared frame",
            "anticlockwise exact material did not retain reversed winding",
            "exact and eligible fallback materials did not coexist",
            "unprepared textured fallback mutated the prior binding",
            "static winding mismatch mutated the prior binding",
            "dynamic winding mismatch mutated the prior binding",
            "caller-supplied closure substitution mutated the prior binding",
            "static cap+1 preflight copied input or mutated the prior binding",
            "dynamic cap+1 preflight copied input or mutated the prior binding",
            "bad_alloc after exact binding changed the sentinel",
            "unexpected precommit exception changed the sentinel",
            "empty authoritative frame did not preserve factor fallback",
            "!lhs.owner_before(rhs)",
            "!rhs.owner_before(lhs)",
        ):
            self.assertIn(token, self.cpp_test)

    def test_native_cross_platform_and_provenance_gates_are_registered(self) -> None:
        native_cmake = (ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        probe_cmake = (
            ROOT / "tools/ogre_next_probe/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        main_cmake = (ROOT / "source/main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        workflow = (
            ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")
        for cmake in (native_cmake, probe_cmake):
            self.assertIn(
                "ror_ogre14_graphics_scene_prepared_material_binding_tests",
                cmake,
            )
            self.assertIn(
                "Ogre14GraphicsScenePreparedMaterialBindingTests.cpp", cmake
            )
            self.assertIn(
                "Ogre14GraphicsScenePreparedMaterialBinding.cpp", cmake
            )
        for token in (
            "ror_ogre14_prepared_binding_capture_obj OBJECT",
            "$<TARGET_OBJECTS:ror_ogre14_prepared_binding_capture_obj>",
        ):
            self.assertIn(token, probe_cmake)
        capture_object_index = probe_cmake.index(
            "ror_ogre14_prepared_binding_capture_obj OBJECT"
        )
        prepared_executable_index = probe_cmake.index(
            "ror_ogre14_graphics_scene_prepared_material_binding_tests",
            capture_object_index,
        )
        self.assertLess(capture_object_index, prepared_executable_index)
        self.assertIn(
            "gfx/ogre14/Ogre14GraphicsScenePreparedMaterialBinding.{h,cpp}",
            main_cmake,
        )
        self.assertIn(
            "ror_ogre14_graphics_scene_prepared_material_binding_tests",
            workflow,
        )
        self.assertIn(
            "-R '^ror_ogre14_graphics_scene_prepared_material_binding$'",
            workflow,
        )
        for manifest_path in (
            ROOT / "tools/run_ogre_next_probe.py",
            ROOT / "tools/verify_ogre_next_artifact_set.py",
            ROOT / "tools/ogre_next_probe/CMakeLists.txt",
            ROOT
            / "tools/ogre_next_probe/cmake/VerifyN2SourceProvenance.cmake",
        ):
            manifest = manifest_path.read_text(encoding="utf-8")
            for path in PATHS:
                with self.subTest(manifest=manifest_path.name, path=path):
                    self.assertIn(path, manifest)


if __name__ == "__main__":
    unittest.main()
