#!/usr/bin/env python3
"""Offline contract for the opt-in OGRE-Next cross-platform CI matrix."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_PATH = REPOSITORY_ROOT / ".github/workflows/ogre-next-probe.yml"
SELF_PATH = "tests/tools/test_ogre_next_probe_workflow.py"
FATAL_SHUTDOWN_PROVENANCE_PATHS = (
    "source/main/Application.cpp",
    "source/main/GameContext.cpp",
    "source/main/GameContext.h",
    "source/main/main.cpp",
    "source/main/physics/Actor.cpp",
    "source/main/physics/collision/Collisions.cpp",
    "source/main/system/ApplicationFatalError.h",
    "source/main/terrain/Terrain.cpp",
    "source/main/terrain/Terrain.h",
    "tests/system/ApplicationFatalShutdownContractTests.cpp",
    "tests/tools/test_ogre14_native_workflow_contract.py",
)
DEFORMABLE_CAPTURE_PROVENANCE_PATHS = (
    "source/main/GameContext.cpp",
    "source/main/gfx/GfxActorCaptureInventory.h",
    "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.cpp",
    "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h",
    "source/main/physics/ActorManager.cpp",
    "source/main/physics/ActorManager.h",
    "source/main/physics/ActorSpawner.cpp",
    "source/main/physics/ActorSpawner.h",
    "source/main/physics/ActorSpawnerFlow.cpp",
    "source/main/physics/flex/FlexBody.cpp",
    "source/main/physics/flex/FlexBody.h",
    "source/main/physics/flex/FlexFactory.cpp",
    "source/main/physics/flex/FlexFactory.h",
    "source/main/physics/flex/FlexMesh.cpp",
    "source/main/physics/flex/FlexMesh.h",
    "source/main/physics/flex/FlexMeshTopology.h",
    "source/main/physics/flex/FlexMeshWheel.cpp",
    "source/main/physics/flex/FlexMeshWheel.h",
    "source/main/physics/flex/FlexObj.cpp",
    "source/main/physics/flex/FlexObj.h",
    "source/main/physics/flex/Flexable.h",
    "source/main/system/RendererOgre14InputAdapter.cpp",
    "source/main/system/RendererOgre14InputAdapter.h",
    "source/main/system/RendererOgre14ProductSession.cpp",
    "source/main/system/RendererOgre14ProductSession.h",
    "tests/gfx/GfxActorCaptureInventoryTests.cpp",
    "tests/gfx/render/GraphicsSceneSnapshotProducerTests.cpp",
    "tests/physics/FlexMeshTopologyTests.cpp",
    "tests/physics/Ogre14FlexShadowLoadTests.cpp",
    "tests/physics/Ogre14MetalFlexShadowReadContractTests.cpp",
    "tests/tools/test_ogre_next_metal_n2_contract.py",
)
DEFORMABLE_CAPTURE_WORKFLOW_PATHS = (
    "source/main/GameContext.cpp",
    "source/main/gfx/GfxActorCaptureInventory.h",
    "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.*",
    "source/main/physics/ActorManager.*",
    "source/main/physics/ActorSpawner.*",
    "source/main/physics/ActorSpawnerFlow.cpp",
    "source/main/physics/flex/**",
    "tests/gfx/GfxActorCaptureInventoryTests.cpp",
    "tests/gfx/ogre14/Ogre14LegacyNativeAssetExtractorCompileTests.cpp",
    "tests/physics/**",
    "tests/tools/test_ogre14_*.py",
)
OGRE_NEXT_DEMO_PROVENANCE_PATHS = (
    "doc/nextgen/OGRE_NEXT_DEMO_PRIVATE_BRIDGE.md",
    "source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.cpp",
    "source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.h",
    "source/main/gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.cpp",
    "source/main/gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.h",
    "source/main/system/detail/OgreNextDemoFrameNormalization.cpp",
    "source/main/system/detail/OgreNextDemoFrameNormalization.h",
    "source/main/physics/Savegame.cpp",
    "tests/tools/test_renderer_suite_packaging_contract.py",
    "tests/gfx/ogre14/OgreNextDemoPrivatePolicyTests.cpp",
    "tests/tools/test_ogre_next_probe_contract.py",
)
OGRE_NEXT_DEMO_WORKFLOW_PATHS = (
    "doc/nextgen/OGRE_NEXT_DEMO_PRIVATE_BRIDGE.md",
    "source/main/gfx/ogre14/detail/OgreNextDemo*",
    "source/main/gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.*",
    "source/main/system/detail/OgreNextDemo*",
    "source/main/physics/Savegame.cpp",
    "tests/gfx/ogre14/OgreNextDemoPrivatePolicyTests.cpp",
    "tests/tools/test_renderer_suite_packaging_contract.py",
)


class OgreNextProbeWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW_PATH.read_text(encoding="utf-8")

    def test_matrix_is_explicit_and_fail_closed(self) -> None:
        self.assertIn("fail-fast: false", self.workflow)
        self.assertIn("timeout-minutes: 120", self.workflow)
        for runner, policy in (
            ("macos-15", "macos-arm64-metal"),
            ("windows-2022", "windows-x64-d3d11"),
            ("ubuntu-24.04", "linux-x86_64-vulkan"),
        ):
            with self.subTest(runner=runner):
                self.assertIn(f"runner: {runner}", self.workflow)
                self.assertIn(f"platform: {policy}", self.workflow)
        self.assertIn('toolset: "14.44"', self.workflow)
        self.assertNotIn("surface:", self.workflow)

    def test_every_action_is_pinned_to_an_immutable_commit(self) -> None:
        uses = re.findall(r"^\s*-?\s*uses:\s*([^\s#]+)", self.workflow, re.MULTILINE)
        self.assertGreaterEqual(len(uses), 4)
        for action in uses:
            with self.subTest(action=action):
                self.assertRegex(action, r"^[^@]+@[0-9a-f]{40}$")

    def test_all_renderer_policy_inputs_trigger_the_probe(self) -> None:
        for path in (
            ".gitattributes",
            "CMakeLists.txt",
            "source/main/CMakeLists.txt",
            "source/main/main.cpp",
            "tests/CMakeLists.txt",
            *FATAL_SHUTDOWN_PROVENANCE_PATHS,
            "cmake/RendererLauncherPackageConfig.cmake",
            "source/main/gfx/GfxScene.*",
            "source/main/gfx/RendererBackendPolicy.*",
            "source/main/gfx/RendererStartupHandoff.*",
            "source/main/gfx/RendererStartupPlan.*",
            "source/main/terrain/ProceduralManager.*",
            "source/main/terrain/ProceduralRoad.*",
            "source/main/system/RendererBridge*",
            "source/main/system/RendererChildIntent.*",
            "source/main/system/RendererChildLauncher.*",
            "source/main/system/RendererOgre14GameBridge.*",
            "source/main/system/RendererOgre14GameHostSession.*",
            "source/main/system/RendererOgre14InputAdapter.*",
            "source/main/system/RendererOgre14InputEngineTarget.*",
            "source/main/system/RendererOgre14ProductSession.*",
            "source/main/system/RendererOgre14RuntimeOwnership.*",
            "source/main/system/RendererSiblingPath.*",
            "source/main/system/RendererPackagedMediaPath.*",
            "source/main/system/RendererPackageRuntimeProbe.*",
            "source/main/system/RendererBridgeEndpoint.*",
            "source/main/system/RendererBridgeLaunchPlan.*",
            "source/main/system/RendererBridgeProcessSupervisor.*",
            "source/main/system/RendererOgreNextChild.*",
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererLauncherPackageConfig.h.in",
            "source/main/system/RendererPublicLauncher.*",
            "source/main/gfx/render/**",
            "tests/gfx/RendererBackendPolicyTests.cpp",
            "tests/gfx/RendererChildIntentTests.cpp",
            "tests/gfx/RendererChildLauncherFakeChild.cpp",
            "tests/gfx/RendererChildLauncherTests.cpp",
            "tests/gfx/RendererBridge*Tests.cpp",
            "tests/gfx/RendererOgre14GameBridgeTests.cpp",
            "tests/gfx/RendererOgre14GameHostSessionTests.cpp",
            "tests/gfx/RendererOgre14InputAdapterTests.cpp",
            "tests/gfx/RendererOgre14RuntimeOwnershipTests.cpp",
            "tests/gfx/RendererSiblingPathTests.cpp",
            "tests/gfx/RendererPackageRuntimeProbeTests.cpp",
            "tests/gfx/RendererBridgeEndpointTests.cpp",
            "tests/gfx/RendererBridgeLaunchPlanTests.cpp",
            "tests/gfx/RendererBridgeProcessFakeChild.cpp",
            "tests/gfx/RendererBridgeProcessSupervisorTests.cpp",
            "tests/gfx/RendererOgreNextChildTests.cpp",
            "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
            "tests/gfx/RendererPublicLauncherTests.cpp",
            "tests/cmake/VerifyRendererPublicBridgeExit.cmake",
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "tests/gfx/RendererStartupPlanTests.cpp",
            "tests/gfx/render/**",
            *DEFORMABLE_CAPTURE_WORKFLOW_PATHS,
            *OGRE_NEXT_DEMO_WORKFLOW_PATHS,
        ):
            with self.subTest(path=path):
                self.assertEqual(self.workflow.count(f"- {path}"), 2)

    def test_deformable_capture_path_filter_omission_fails_contract(self) -> None:
        def require_exact_filters(workflow: str) -> None:
            for required_path in DEFORMABLE_CAPTURE_WORKFLOW_PATHS:
                self.assertEqual(workflow.count(f"- {required_path}"), 2)

        require_exact_filters(self.workflow)
        for path in DEFORMABLE_CAPTURE_WORKFLOW_PATHS:
            with self.subTest(omitted_path=path):
                omitted = self.workflow.replace(f"      - {path}\n", "")
                with self.assertRaises(AssertionError):
                    require_exact_filters(omitted)

    def test_private_demo_bridge_is_built_hashed_and_fail_closed(self) -> None:
        for path in OGRE_NEXT_DEMO_WORKFLOW_PATHS:
            with self.subTest(workflow_path=path):
                self.assertEqual(self.workflow.count(f"- {path}"), 2)

        inventory_paths = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt",
            REPOSITORY_ROOT
            / "tools"
            / "ogre_next_probe"
            / "cmake"
            / "VerifyN2SourceProvenance.cmake",
            REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py",
            REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py",
        )
        for inventory_path in inventory_paths:
            inventory = inventory_path.read_text(encoding="utf-8")
            for path in OGRE_NEXT_DEMO_PROVENANCE_PATHS:
                with self.subTest(inventory=inventory_path.name, path=path):
                    self.assertIn(path, inventory)

        cmake = inventory_paths[0].read_text(encoding="utf-8")
        for token in (
            "ror_ogre_next_demo_private_policy_tests",
            "tests/gfx/ogre14/OgreNextDemoPrivatePolicyTests.cpp",
            "source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.cpp",
            "source/main/system/detail/OgreNextDemoFrameNormalization.cpp",
            "add_test(NAME ror_ogre_next_demo_private_policy",
        ):
            self.assertIn(token, cmake)

        product_target = cmake[
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_ogre14_game_host_session_tests"
            ) : cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_ogre14_game_host_session_tests"
            )
        ]
        self.assertIn(
            "source/main/system/detail/OgreNextDemoFrameNormalization.cpp",
            product_target,
        )

        product_cmake = (
            REPOSITORY_ROOT / "source/main/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        strict_fp = product_cmake[
            product_cmake.index("set(ROR_RENDER_CONTRACT_STRICT_FP_SOURCES") :
            product_cmake.index("if (ROR_OGRE14)", product_cmake.index(
                "set(ROR_RENDER_CONTRACT_STRICT_FP_SOURCES"
            ))
        ]
        for strict_source in (
            "gfx/ogre14/detail/OgreNextDemoPrivatePolicy.cpp",
            "system/detail/OgreNextDemoFrameNormalization.cpp",
        ):
            with self.subTest(strict_fp_source=strict_source):
                self.assertIn(strict_source, strict_fp)
        ogre14_strict_fp = product_cmake[
            product_cmake.index("if (ROR_OGRE14)", product_cmake.index(
                "set(ROR_RENDER_CONTRACT_STRICT_FP_SOURCES"
            )) : product_cmake.index("if (MSVC)", product_cmake.index(
                "set(ROR_RENDER_CONTRACT_STRICT_FP_SOURCES"
            ))
        ]
        self.assertIn(
            "gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.cpp",
            ogre14_strict_fp,
        )

        terrain_source = (
            REPOSITORY_ROOT
            / "source/main/gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.cpp"
        ).read_text(encoding="utf-8")
        gfx_scene = (REPOSITORY_ROOT / "source/main/gfx/GfxScene.cpp").read_text(
            encoding="utf-8"
        )
        native_page = terrain_source[
            terrain_source.index("Render::ValidationResult CaptureNativePage(") :
        ]
        self.assertLess(
            native_page.index("waitForDerivedProcesses()"),
            native_page.index("isDerivedDataUpdateInProgress()"),
        )
        self.assertEqual(
            terrain_source.count("blitToMemory(destination)"), 1
        )
        self.assertIn("const NativeMip &native_base = mips.front();",
                      terrain_source)
        self.assertNotIn("native_mip.buffer->blitToMemory", terrain_source)
        private_policy = (
            REPOSITORY_ROOT
            / "source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("texture.mip_levels.size() != 1U", private_policy)
        terrain_pages = gfx_scene[
            gfx_scene.index("CaptureOgre14TerrainPages(") :
        ]
        self.assertLess(
            terrain_pages.index("waitForDerivedProcesses()"),
            terrain_pages.index("isDerivedDataUpdateInProgress()"),
        )
        self.assertIn("NormalizeOgreNextDemoMatteMesh", gfx_scene)
        self.assertIn("BuildOgreNextDemoMatteTangents", gfx_scene)

        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(
            encoding="utf-8"
        )
        for token in (
            "source/main/gfx/ogre14/detail/OgreNextDemo* text eol=lf",
            "source/main/gfx/ogre14/detail/Ogre14ToOgreNextTerrainSource.* text eol=lf",
            "source/main/system/detail/OgreNextDemo* text eol=lf",
            "tests/gfx/ogre14/OgreNextDemoPrivatePolicyTests.cpp text eol=lf",
            "doc/nextgen/OGRE_NEXT_DEMO_PRIVATE_BRIDGE.md text eol=lf",
            "source/main/physics/Savegame.cpp text eol=lf",
            "tests/tools/test_renderer_suite_packaging_contract.py text eol=lf",
        ):
            self.assertIn(token, attributes)

    def test_product_session_and_deformable_gates_are_linked_and_built(self) -> None:
        def require_once(block: str, token: str) -> None:
            self.assertEqual(block.count(token), 1)

        cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        product_target = cmake[
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_ogre14_game_host_session_tests"
            ) : cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_ogre14_game_host_session_tests"
            )
        ]
        for source in (
            "source/main/system/RendererOgre14InputAdapter.cpp",
            "source/main/system/RendererOgre14ProductSession.cpp",
            "source/main/system/detail/OgreNextDemoFrameNormalization.cpp",
        ):
            with self.subTest(product_source=source):
                require_once(product_target, source)
                omitted = product_target.replace(source, "")
                with self.assertRaises(AssertionError):
                    require_once(omitted, source)

        build_start = self.workflow.index(
            "- name: Build deformable capture and product-session gates"
        )
        build_end = self.workflow.index(
            "- name: Prove deformable capture transactions on the host ABI"
        )
        build_step = self.workflow[build_start:build_end]
        for target in (
            "ror_graphics_scene_snapshot_producer_tests",
            "ror_flex_mesh_topology_tests",
            "ror_ogre14_metal_flex_shadow_read_contract_tests",
            "ror_gfx_actor_capture_inventory_tests",
            "ror_application_fatal_shutdown_contract_tests",
            "ror_renderer_ogre14_game_host_session_tests",
        ):
            with self.subTest(built_target=target):
                require_once(build_step, target)
                omitted = build_step.replace(target, "")
                with self.assertRaises(AssertionError):
                    require_once(omitted, target)
        self.assertIn("--config Release", build_step)
        self.assertIn("--parallel ${{ matrix.jobs }}", build_step)

        run_start = self.workflow.index(
            "- name: Prove deformable capture transactions on the host ABI"
        )
        run_end = self.workflow.index(
            "- name: Prove exact translated deformable materials on the host ABI"
        )
        run_step = self.workflow[run_start:run_end]
        require_once(run_step, "ror_application_fatal_shutdown_contract")
        self.assertIn("--build-config Release", run_step)
        self.assertIn("--output-on-failure", run_step)

    def test_relevant_source_manifests_are_exactly_equivalent(self) -> None:
        runner = (
            REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py"
        ).read_text(encoding="utf-8")
        verifier = (
            REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
        ).read_text(encoding="utf-8")
        cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        prelink = (
            REPOSITORY_ROOT
            / "tools"
            / "ogre_next_probe"
            / "cmake"
            / "VerifyN2SourceProvenance.cmake"
        ).read_text(encoding="utf-8")

        def quoted_paths(block: str) -> list[str]:
            return re.findall(r'"([^"]+)"', block)

        def clean_paths(block: str) -> list[str]:
            result = []
            for line in block.splitlines()[1:]:
                path = line.strip()
                if path.endswith(")"):
                    path = path[:-1]
                if path:
                    result.append(path)
            return result

        runner_paths = quoted_paths(
            runner[
                runner.index("RELEVANT_SOURCE_PATHS = (") :
                runner.index("\n)\n\n\nclass ProbeError")
            ]
        )
        verifier_paths = quoted_paths(
            verifier[
                verifier.index("RELEVANT_SOURCE_PATHS = (") :
                verifier.index("\n)\nRT4_ATTESTATION_SCHEMA")
            ]
        )
        cmake_manifest = quoted_paths(
            cmake[
                cmake.index("list(APPEND _ror_relevant_source_files") :
                cmake.index("list(FILTER _ror_relevant_source_files")
            ]
        ) + ["source/main/gfx/render", "tools/ogre_next_probe"]
        cmake_clean = clean_paths(
            cmake[
                cmake.index("set(_ror_n2_relevant_source_paths") :
                cmake.index(
                    "execute_process(",
                    cmake.index("set(_ror_n2_relevant_source_paths"),
                )
            ]
        )
        prelink_clean = clean_paths(
            prelink[
                prelink.index("set(_ror_n2_relevant_source_paths") :
                prelink.index("execute_process(")
            ]
        )
        prelink_manifest = quoted_paths(
            prelink[
                prelink.index("list(APPEND _ror_n2_relevant_source_files") :
                prelink.index("list(FILTER _ror_n2_relevant_source_files")
            ]
        ) + ["source/main/gfx/render", "tools/ogre_next_probe"]

        expected = set(runner_paths)
        for required in (
            "source/main/gfx/render/ogrenext/OgreNextDisplayDomainUnlit.cpp",
            "source/main/gfx/render/ogrenext/OgreNextDisplayDomainUnlit.h",
            "tools/ogre_next_probe/media/Hlms/RoR/DisplayDomain/DisplayDomain_piece_ps.any",
        ):
            with self.subTest(display_domain_source=required):
                self.assertIn(required, expected)
        manifests = {
            "runner": runner_paths,
            "artifact verifier": verifier_paths,
            "probe configure manifest": cmake_manifest,
            "Metal configure clean set": cmake_clean,
            "Metal pre-link clean set": prelink_clean,
            "Metal pre-link manifest": prelink_manifest,
        }
        for name, paths in manifests.items():
            with self.subTest(manifest=name):
                self.assertEqual(len(paths), len(set(paths)))
                self.assertEqual(set(paths), expected)
                omitted = set(paths)
                omitted.remove("source/main/physics/flex/FlexBody.cpp")
                self.assertNotEqual(omitted, expected)

    def test_every_probe_layer_is_required_in_normal_and_optimized_python(self) -> None:
        for test_path in (
            "tests/tools/test_ogre_next_probe_contract.py",
            "tests/tools/test_ogre_next_frame_probe.py",
            "tests/tools/test_ogre_next_frontend_n1_contract.py",
            "tests/tools/test_ogre_next_pssm_shadow_contract.py",
            "tests/tools/test_ogre_next_child_runtime_contract.py",
            "tests/tools/test_ogre_next_metal_n2_contract.py",
            "tests/tools/test_ogre_next_vulkan_rt5_contract.py",
            "tests/tools/test_ogre_next_linux_static_closure.py",
            "tests/tools/test_ogre_next_metal_n3_contract.py",
            "tests/tools/test_ogre_next_metal_n4_contract.py",
            SELF_PATH,
            "tests/tools/test_verify_ogre_next_artifact_set.py",
        ):
            with self.subTest(test_path=test_path):
                self.assertEqual(self.workflow.count(test_path), 2)
        self.assertIn("--validate-contract-only", self.workflow)
        self.assertIn("--output-on-failure", self.workflow)
        self.assertIn(
            "- name: Run fail-closed offline contracts\n        shell: bash",
            self.workflow,
        )

    def test_linux_uses_a_declared_software_vulkan_device(self) -> None:
        for required in (
            "libvulkan-dev",
            "libx11-dev",
            "libxcb-randr0-dev",
            "libxt-dev",
            "libxaw7-dev",
            "mesa-vulkan-drivers",
            "vulkaninfo --summary",
            "VK_ICD_FILENAMES",
            "LIBGL_ALWAYS_SOFTWARE=1",
            "Linux x86_64 Vulkan null bootstrap plus XCB window-host",
        ):
            with self.subTest(required=required):
                self.assertIn(required, self.workflow)

        for prohibited in (
            "libfreetype6-dev",
            "libshaderc-dev",
            "glslang-dev",
            "spirv-tools",
        ):
            with self.subTest(prohibited=prohibited):
                self.assertNotIn(prohibited, self.workflow)

    def test_linux_builds_and_audits_the_pinned_static_shader_closure(self) -> None:
        probe_root = REPOSITORY_ROOT / "tools" / "ogre_next_probe"
        cmake = (probe_root / "CMakeLists.txt").read_text(encoding="utf-8")
        cmake += (probe_root / "cmake" / "PinnedOgreNext.cmake").read_text(
            encoding="utf-8"
        )
        self.assertIn("linux-shader-toolchain.lock.json", cmake)
        self.assertIn("FetchContent_MakeAvailable(shaderc)", cmake)
        self.assertIn("set(Vulkan_SHADERC_LIB_REL shaderc_combined", cmake)
        self.assertIn("ror_ogre_next_linux_static_closure_verify", cmake)
        self.assertIn("set(Vulkan_SHADERC_LIB_REL", cmake)
        self.assertIn("set(Vulkan_SHADERC_LIB_DBG", cmake)
        self.assertNotIn("find_package(glslang", cmake)
        self.assertNotIn("ROR_OGRE_NEXT_SHADERC_SHARED_LIBRARY", cmake)
        self.assertNotIn("glslang-dev", self.workflow)
        self.assertNotIn("libshaderc-dev", self.workflow)
        self.assertIn("cmp tools/ogre_next_probe/linux-shader-toolchain.lock.json", self.workflow)
        self.assertIn("lib(freetype|shaderc|glslang", self.workflow)
        self.assertIn("MachineIndependent|GenericCodeGen|OSDependent", self.workflow)
        self.assertIn("ror_ogre_next_frame_probe", self.workflow)
        self.assertIn("ror_ogre_next_frontend_n1_smoke", self.workflow)
        for artifact in (
            "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-repeat/",
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, self.workflow)

    def test_startup_plan_is_built_and_run_on_every_probe_platform(self) -> None:
        cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        runner = (REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py").read_text(
            encoding="utf-8"
        )
        verifier = (
            REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
        ).read_text(encoding="utf-8")
        prelink = (
            REPOSITORY_ROOT
            / "tools"
            / "ogre_next_probe"
            / "cmake"
            / "VerifyN2SourceProvenance.cmake"
        ).read_text(encoding="utf-8")
        target_block = cmake[
            cmake.index("add_executable(\n        ror_renderer_startup_plan_tests") :
            cmake.index("target_include_directories(\n        ror_renderer_startup_plan_tests")
        ]
        handoff_target_block = cmake[
            cmake.index(
                "add_executable(\n        ror_renderer_startup_handoff_tests"
            ) :
            cmake.index(
                "target_include_directories(\n        ror_renderer_startup_handoff_tests"
            )
        ]
        intent_target_block = cmake[
            cmake.index(
                "add_executable(\n        ror_renderer_child_intent_tests"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_child_intent_tests"
            )
        ]
        launcher_target_block = cmake[
            cmake.index(
                "add_executable(\n        ror_renderer_child_launcher_tests"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_child_launcher_tests"
            )
        ]
        bridge_fake_common_block = cmake[
            cmake.index("set(_ror_renderer_bridge_process_fake_common_sources") :
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_bridge_process_fake_game"
            )
        ]
        bridge_supervisor_target_block = cmake[
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_bridge_process_supervisor_tests"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_bridge_process_supervisor_tests"
            )
        ]
        ogre_next_child_target_block = cmake[
            cmake.index(
                "add_executable(\n        ror_renderer_ogre_next_child_tests"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_ogre_next_child_tests"
            )
        ]
        runtime_probe_target_block = cmake[
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_package_runtime_probe_tests"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_package_runtime_probe_tests"
            )
        ]
        public_child_target_block = cmake[
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_public_launcher_legacy_child"
            ) :
            cmake.index(
                "set_target_properties(\n"
                "        ror_renderer_public_launcher_legacy_child"
            )
        ]
        public_entrypoint_target_block = cmake[
            cmake.index(
                "add_executable(\n"
                "        ror_renderer_public_launcher_entrypoint"
            ) :
            cmake.index(
                "target_include_directories(\n"
                "        ror_renderer_public_launcher_entrypoint"
            )
        ]
        policy_language_marker = (
            "set_target_properties(\n"
            "        ror_renderer_backend_policy_tests\n"
            "        ror_renderer_startup_plan_tests"
        )
        policy_language_start = cmake.index(policy_language_marker)
        policy_language_block = cmake[
            policy_language_start : cmake.index(
                "endif ()", policy_language_start
            )
        ]
        cmake_manifest = cmake[
            cmake.index("list(APPEND _ror_relevant_source_files") :
            cmake.index("list(FILTER _ror_relevant_source_files")
        ]
        clean_paths = cmake[
            cmake.index("set(_ror_n2_relevant_source_paths") :
            cmake.index("execute_process(", cmake.index("set(_ror_n2_relevant_source_paths"))
        ]
        runner_manifest = runner[
            runner.index("RELEVANT_SOURCE_PATHS = (") :
            runner.index("\n)\n\n\nclass ProbeError")
        ]
        verifier_manifest = verifier[
            verifier.index("RELEVANT_SOURCE_PATHS = (") :
            verifier.index("\n)\nRT4_ATTESTATION_SCHEMA")
        ]
        prelink_clean_paths = prelink[
            prelink.index("set(_ror_n2_relevant_source_paths") :
            prelink.index("execute_process(")
        ]
        prelink_manifest = prelink[
            prelink.index("list(APPEND _ror_n2_relevant_source_files") :
            prelink.index("list(FILTER _ror_n2_relevant_source_files")
        ]
        def exact_cmake_path_count(block: str, path: str) -> int:
            return sum(
                line.strip().strip('"') == path
                for line in block.splitlines()
            )
        for token in (
            "ror_renderer_backend_policy_tests",
            "tests/gfx/RendererBackendPolicyTests.cpp",
            "add_test(NAME ror_renderer_backend_policy",
            "ror_renderer_startup_plan_tests",
            "tests/gfx/RendererStartupPlanTests.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
            "add_test(NAME ror_renderer_startup_plan",
            "ror_renderer_startup_handoff_tests",
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "add_test(NAME ror_renderer_startup_handoff",
            "ror_renderer_child_intent_tests",
            "tests/gfx/RendererChildIntentTests.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "add_test(NAME ror_renderer_child_intent",
            "ror_renderer_sibling_path_tests",
            "tests/gfx/RendererSiblingPathTests.cpp",
            "source/main/system/RendererSiblingPath.cpp",
            "add_test(NAME ror_renderer_sibling_path",
            "ror_renderer_package_runtime_probe_tests",
            "tests/gfx/RendererPackageRuntimeProbeTests.cpp",
            "source/main/system/RendererPackageRuntimeProbe.cpp",
            "source/main/system/RendererPackagedMediaPath.cpp",
            "add_test(NAME ror_renderer_package_runtime_probe",
            "ror_renderer_bridge_process_fake_game",
            "ror_renderer_bridge_process_fake_presentation",
            "tests/gfx/RendererBridgeProcessFakeChild.cpp",
            'OUTPUT_NAME "RoR-Ogre14"',
            'OUTPUT_NAME "RoR-OgreNext"',
            "ror_renderer_bridge_process_supervisor_tests",
            "tests/gfx/RendererBridgeProcessSupervisorTests.cpp",
            "source/main/system/RendererBridgeEndpoint.cpp",
            "source/main/system/RendererBridgeLaunchPlan.cpp",
            "source/main/system/RendererBridgeProcessSupervisor.cpp",
            "add_test(NAME ror_renderer_bridge_process_supervisor",
            "PROPERTIES TIMEOUT 30",
            "ror_renderer_ogre_next_child_tests",
            "tests/gfx/RendererOgreNextChildTests.cpp",
            "source/main/system/RendererOgreNextChild.cpp",
            "add_test(NAME ror_renderer_ogre_next_child",
            "_ror_n1_package_dependencies\n"
            "        ror_renderer_backend_policy_tests",
            "ror_renderer_child_launcher_fake_child",
            "tests/gfx/RendererChildLauncherFakeChild.cpp",
            "ror_renderer_child_launcher_tests",
            "tests/gfx/RendererChildLauncherTests.cpp",
            "source/main/system/RendererChildLauncher.cpp",
            "add_test(NAME ror_renderer_child_launcher",
            "ror_renderer_ogre14_game_bridge_tests",
            "tests/gfx/RendererOgre14GameBridgeTests.cpp",
            "source/main/system/RendererOgre14GameBridge.cpp",
            "add_test(NAME ror_renderer_ogre14_game_bridge",
            "ror_renderer_bridge_channel_tests",
            "tests/gfx/RendererBridgeChannelTests.cpp",
            "add_test(NAME ror_renderer_bridge_channel",
            "ror_render_bridge_control_transport_tests",
            "tests/gfx/render/RenderBridgeControlTransportTests.cpp",
            "add_test(NAME ror_render_bridge_control_transport",
            "ror_renderer_ogre14_game_host_session_tests",
            "tests/gfx/RendererOgre14GameHostSessionTests.cpp",
            "source/main/system/RendererOgre14GameHostSession.cpp",
            "add_test(NAME ror_renderer_ogre14_game_host_session",
            "ror_renderer_public_launcher_legacy_child",
            "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
            "ror_renderer_public_launcher_entrypoint",
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererPublicLauncher.cpp",
            "cmake/RendererLauncherPackageConfig.cmake",
            "source/main/system/RendererLauncherPackageConfig.h.in",
            "add_test(NAME ror_renderer_public_launcher_entrypoint",
            "ror_renderer_public_bridge_fake_game",
            "ror_renderer_public_bridge_fake_presentation",
            "ror_renderer_public_bridge_entrypoint",
            "ROR_RENDERER_BRIDGE_FAKE_ACCEPT_ANY_SESSION=1",
            "ROR_RENDERER_BRIDGE_FAKE_REQUIRE_PUBLIC_ARGUMENTS=1",
            "tests/cmake/VerifyRendererPublicBridgeExit.cmake",
            'NAME "ror_renderer_public_bridge_${_ror_public_bridge_case}"',
            "default-prefer",
            "explicit-require",
            "presentation-first",
            "pre-ready-fallback",
            "pre-ready-require-terminal",
            "post-ready-terminal",
            "native-require-terminal",
            "ror_application_fatal_shutdown_contract_tests",
            "tests/system/ApplicationFatalShutdownContractTests.cpp",
            "source/main/system/ApplicationFatalError.h",
            "add_test(NAME ror_application_fatal_shutdown_contract",
        ):
            self.assertIn(token, cmake)
        for path in (
            "cmake/RendererLauncherPackageConfig.cmake",
            "source/main/gfx/GfxScene.cpp",
            "source/main/gfx/GfxScene.h",
            "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.cpp",
            "source/main/gfx/ogre14/Ogre14LegacyNativeAssetExtractor.h",
            "source/main/gfx/RendererBackendPolicy.cpp",
            "source/main/gfx/RendererBackendPolicy.h",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupHandoff.h",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererStartupPlan.h",
            "source/main/terrain/ProceduralManager.cpp",
            "source/main/terrain/ProceduralManager.h",
            "source/main/terrain/ProceduralRoad.cpp",
            "source/main/terrain/ProceduralRoad.h",
            "source/main/terrain/TerrainObjectManager.cpp",
            "source/main/terrain/TerrainObjectManager.h",
            "source/main/main.cpp",
            *FATAL_SHUTDOWN_PROVENANCE_PATHS,
            "source/main/system/RendererChildIntent.cpp",
            "source/main/system/RendererChildIntent.h",
            "source/main/system/RendererChildLauncher.cpp",
            "source/main/system/RendererChildLauncher.h",
            "source/main/system/RendererBridgeChannel.cpp",
            "source/main/system/RendererBridgeChannel.h",
            "source/main/system/RendererBridgeEndpoint.cpp",
            "source/main/system/RendererBridgeEndpoint.h",
            "source/main/system/RendererOgre14GameBridge.cpp",
            "source/main/system/RendererOgre14GameBridge.h",
            "source/main/system/RendererOgre14GameHostSession.cpp",
            "source/main/system/RendererOgre14GameHostSession.h",
            "source/main/system/RendererSiblingPath.cpp",
            "source/main/system/RendererSiblingPath.h",
            "source/main/system/RendererPackagedMediaPath.cpp",
            "source/main/system/RendererPackagedMediaPath.h",
            "source/main/system/RendererPackageRuntimeProbe.cpp",
            "source/main/system/RendererPackageRuntimeProbe.h",
            "source/main/system/RendererBridgeEndpoint.cpp",
            "source/main/system/RendererBridgeEndpoint.h",
            "source/main/system/RendererBridgeLaunchPlan.cpp",
            "source/main/system/RendererBridgeLaunchPlan.h",
            "source/main/system/RendererBridgeProcessSupervisor.cpp",
            "source/main/system/RendererBridgeProcessSupervisor.h",
            "source/main/system/RendererOgreNextChild.cpp",
            "source/main/system/RendererOgreNextChild.h",
            "source/main/system/RendererOgreNextChildMain.cpp",
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererLauncherPackageConfig.h.in",
            "source/main/system/RendererPublicLauncher.cpp",
            "source/main/system/RendererPublicLauncher.h",
            "tests/gfx/RendererBackendPolicyTests.cpp",
            "tests/gfx/RendererChildIntentTests.cpp",
            "tests/gfx/RendererChildLauncherFakeChild.cpp",
            "tests/gfx/RendererChildLauncherTests.cpp",
            "tests/gfx/RendererOgre14GameBridgeTests.cpp",
            "tests/gfx/RendererOgre14GameHostSessionTests.cpp",
            "tests/gfx/RendererBridgeChannelTests.cpp",
            "tests/gfx/RendererSiblingPathTests.cpp",
            "tests/gfx/RendererPackageRuntimeProbeTests.cpp",
            "tests/gfx/RendererBridgeEndpointTests.cpp",
            "tests/gfx/RendererBridgeLaunchPlanTests.cpp",
            "tests/gfx/RendererBridgeProcessFakeChild.cpp",
            "tests/gfx/RendererBridgeProcessSupervisorTests.cpp",
            "tests/gfx/RendererOgreNextChildTests.cpp",
            "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
            "tests/gfx/RendererPublicLauncherTests.cpp",
            "tests/cmake/VerifyRendererPublicBridgeExit.cmake",
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "tests/gfx/RendererStartupPlanTests.cpp",
            "tests/gfx/render/Ogre14GraphicsSceneSourceTests.cpp",
            "tests/gfx/render/Ogre14LegacyAssetTranslatorTests.cpp",
            "tests/gfx/render/Ogre14LegacyMaterialClosureTests.cpp",
            "tests/gfx/ogre14/Ogre14LegacyNativeAssetExtractorCompileTests.cpp",
            "tests/gfx/render/Ogre14ParticleCaptureSourceTests.cpp",
            "tests/gfx/render/Ogre14ProceduralRoadSourceTests.cpp",
            "tests/gfx/render/Ogre14DynamicMaterialClosureTests.cpp",
            "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.cpp",
            "source/main/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistry.h",
            "tests/gfx/ogre14/Ogre14LegacyMaterialSemanticRegistryTests.cpp",
            "tests/gfx/render/Ogre14SourceTextureDecoderTests.cpp",
            "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.cpp",
            "source/main/gfx/ogre14/Ogre14AuthenticatedTextureReceipt.h",
            "conanfile.py",
            "cmake/conan/locks/ogre3d-14.5.2-linux-x86_64-release.lock",
            "cmake/conan/locks/ogre3d-14.5.2-macos-arm64-release.lock",
            "cmake/conan/locks/ogre3d-14.5.2-windows-x86_64-release.lock",
            "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock",
            "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock",
            "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock",
            "cmake/conan/recipes/mygui/conanfile.py",
            "cmake/conan/recipes/ogre3d/conandata.yml",
            "cmake/conan/recipes/ogre3d/patches/14.5.2/archive-manager-load-rollback.patch",
            "cmake/conan/recipes/ogre3d/patches/14.5.2/terrain-composite-revision-metal-readback.patch",
            "cmake/conan/recipes/ogre3d/test_package/src/ogre_recipe_probe.cpp",
            "source/main/resources/CacheSystem.cpp",
            "source/main/resources/ContentManager.cpp",
            "source/main/resources/ContentManager.h",
            "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.cpp",
            "source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.h",
            "tests/gfx/ogre14/Ogre14AuthenticatedTextureReceiptTests.cpp",
            "tests/resources/TerrainBundleArchiveVerifierTests.cpp",
            "tests/tools/assert_ogre_recipe_graph.py",
            "tests/gfx/render/RenderBridgeControlTransportTests.cpp",
            "tests/tools/test_ogre14_particle_capture_contract.py",
            "tests/tools/test_ogre14_dynamic_material_closure_contract.py",
            "tests/tools/test_ogre14_legacy_asset_translator_contract.py",
            "tests/tools/test_ogre14_legacy_material_closure_contract.py",
            "tests/tools/test_ogre14_material_semantic_registry_contract.py",
            "tests/tools/test_ogre14_source_texture_decoder_contract.py",
            "tests/tools/test_ogre14_authenticated_texture_receipt_contract.py",
            "tests/tools/test_ogre14_terrain_composite_recipe_contract.py",
            "tests/tools/test_ogre_next_child_runtime_contract.py",
            *DEFORMABLE_CAPTURE_PROVENANCE_PATHS,
        ):
            with self.subTest(provenance_path=path):
                self.assertEqual(cmake_manifest.count(f'"{path}"'), 1)
                self.assertEqual(exact_cmake_path_count(clean_paths, path), 1)
                self.assertEqual(runner_manifest.count(f'"{path}"'), 1)
                self.assertEqual(verifier_manifest.count(f'"{path}"'), 1)
                self.assertEqual(
                    exact_cmake_path_count(prelink_clean_paths, path), 1
                )
                self.assertEqual(prelink_manifest.count(f'"{path}"'), 1)
        self.assertEqual(runner_manifest, verifier_manifest)
        for source in (
            "tests/gfx/RendererStartupPlanTests.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(target_source=source):
                self.assertEqual(target_block.count(source), 1)
        for source in (
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(handoff_target_source=source):
                self.assertEqual(handoff_target_block.count(source), 1)
        for source in (
            "tests/gfx/RendererChildIntentTests.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(intent_target_source=source):
                self.assertEqual(intent_target_block.count(source), 1)
        self.assertNotIn("RendererChildLauncher.cpp", intent_target_block)
        for source in (
            "tests/gfx/RendererPackageRuntimeProbeTests.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
            "source/main/system/RendererSiblingPath.cpp",
            "source/main/system/RendererPackagedMediaPath.cpp",
            "source/main/system/RendererPackageRuntimeProbe.cpp",
        ):
            with self.subTest(runtime_probe_target_source=source):
                self.assertEqual(runtime_probe_target_block.count(source), 1)
        for source in (
            "tests/gfx/RendererBridgeProcessFakeChild.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "RenderTransportEnvelope.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/system/RendererBridgeEndpoint.cpp",
        ):
            with self.subTest(bridge_fake_source=source):
                self.assertEqual(bridge_fake_common_block.count(source), 1)
        for source in (
            "tests/gfx/RendererBridgeProcessSupervisorTests.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/system/RendererBridgeEndpoint.cpp",
            "source/main/system/RendererBridgeLaunchPlan.cpp",
            "source/main/system/RendererSiblingPath.cpp",
            "source/main/system/RendererBridgeProcessSupervisor.cpp",
        ):
            with self.subTest(bridge_supervisor_source=source):
                self.assertEqual(bridge_supervisor_target_block.count(source), 1)
        for source in (
            "tests/gfx/RendererOgreNextChildTests.cpp",
            "source/main/system/RendererOgreNextChild.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(ogre_next_child_target_source=source):
                self.assertEqual(ogre_next_child_target_block.count(source), 1)
        for prohibited in (
            "RendererChildLauncher.cpp",
            "source/main/main.cpp",
            "AppContext",
            "SDL",
            "OIS",
            "MyGUI",
        ):
            with self.subTest(ogre_next_child_target_exclusion=prohibited):
                self.assertNotIn(prohibited, ogre_next_child_target_block)
        package_dependencies = cmake[
            cmake.index("set(_ror_n1_package_dependencies") :
            cmake.index(")", cmake.index("set(_ror_n1_package_dependencies"))
        ]
        self.assertEqual(
            package_dependencies.count("ror_renderer_child_intent_tests"), 1
        )
        self.assertEqual(
            package_dependencies.count("ror_renderer_sibling_path_tests"), 1
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_renderer_package_runtime_probe_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_renderer_bridge_process_supervisor_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count("ror_renderer_ogre_next_child_tests"), 1
        )
        for source in (
            "tests/gfx/RendererChildLauncherTests.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/system/RendererChildLauncher.cpp",
            "source/main/system/RendererSiblingPath.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(launcher_target_source=source):
                self.assertEqual(launcher_target_block.count(source), 1)
        self.assertEqual(
            public_child_target_block.count(
                "tests/gfx/RendererPublicLauncherLegacyChild.cpp"
            ),
            1,
        )
        for source in (
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererPublicLauncher.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/system/RendererBridgeEndpoint.cpp",
            "source/main/system/RendererBridgeLaunchPlan.cpp",
            "source/main/system/RendererBridgeProcessSupervisor.cpp",
            "source/main/system/RendererChildLauncher.cpp",
            "source/main/system/RendererPackageRuntimeProbe.cpp",
            "source/main/system/RendererPackagedMediaPath.cpp",
            "source/main/system/RendererSiblingPath.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(public_entrypoint_target_source=source):
                self.assertEqual(public_entrypoint_target_block.count(source), 1)
        for target in (
            "ror_renderer_backend_policy_tests",
            "ror_renderer_startup_plan_tests",
            "ror_renderer_startup_handoff_tests",
            "ror_renderer_child_intent_tests",
            "ror_renderer_sibling_path_tests",
            "ror_renderer_package_runtime_probe_tests",
            "ror_renderer_ogre_next_child_tests",
            "ror_renderer_child_launcher_fake_child",
            "ror_renderer_child_launcher_tests",
            "ror_renderer_public_launcher_legacy_child",
            "ror_renderer_public_launcher_entrypoint",
            "ror_renderer_public_bridge_entrypoint",
        ):
            with self.subTest(cxx11_policy_target=target):
                self.assertEqual(policy_language_block.count(target), 1)
        self.assertIn("CXX_STANDARD 11", policy_language_block)
        self.assertIn("CXX_STANDARD_REQUIRED YES", policy_language_block)
        self.assertIn("CXX_EXTENSIONS NO", policy_language_block)
        n1_target_list = cmake[
            cmake.index("set(_ror_n1_targets") :
            cmake.index(")", cmake.index("set(_ror_n1_targets"))
        ]
        for target in (
            "ror_renderer_bridge_process_fake_game",
            "ror_renderer_bridge_process_fake_presentation",
            "ror_renderer_bridge_process_supervisor_tests",
            "ror_renderer_public_bridge_fake_game",
            "ror_renderer_public_bridge_fake_presentation",
            "ror_renderer_bridge_channel_tests",
            "ror_render_bridge_control_transport_tests",
            "ror_renderer_ogre14_game_host_session_tests",
            "ror_application_fatal_shutdown_contract_tests",
        ):
            with self.subTest(cxx17_bridge_target=target):
                self.assertEqual(n1_target_list.count(target), 1)
                self.assertNotIn(target, policy_language_block)

    def test_public_launcher_entrypoint_stays_outside_the_n1_package(self) -> None:
        cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        entrypoint_start = cmake.index(
            "add_executable(\n        ror_renderer_public_launcher_entrypoint"
        )
        entrypoint_end = cmake.index("\n    set(_ror_n1_targets", entrypoint_start)
        entrypoint = cmake[entrypoint_start:entrypoint_end]
        package_start = cmake.index(
            "add_custom_command(\n        OUTPUT "
            '"${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}"'
        )
        package_end = cmake.index(
            "add_custom_target(ror_ogre_next_frontend_n1_package",
            package_start,
        )
        package = cmake[package_start:package_end]

        for token in (
            'OUTPUT_NAME "RoR"',
            'OUTPUT_NAME "RoR-Ogre14"',
            "renderer-public-entrypoint-tests",
            "WIN32_EXECUTABLE YES",
            "WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE",
            "PRIVATE Shell32",
            "PRIVATE -municode",
        ):
            with self.subTest(entrypoint_contract=token):
                self.assertIn(token, cmake)
        for prohibited in (
            "ror_renderer_public_launcher_entrypoint",
            "ror_renderer_public_launcher_legacy_child",
            "ror_renderer_public_bridge_entrypoint",
            "ror_renderer_public_bridge_fake_game",
            "ror_renderer_public_bridge_fake_presentation",
            "renderer-public-entrypoint-tests",
            "renderer-public-bridge-tests",
            "RoR-Ogre14",
        ):
            with self.subTest(n1_package_exclusion=prohibited):
                self.assertNotIn(prohibited, package)
        self.assertIn(
            "ror_renderer_public_launcher_legacy_child", entrypoint
        )

    def test_renderer_child_launcher_fails_closed_at_process_boundary(self) -> None:
        source = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "system"
            / "RendererChildLauncher.cpp"
        ).read_text(encoding="utf-8")
        sibling_path_source = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "system"
            / "RendererSiblingPath.cpp"
        ).read_text(encoding="utf-8")
        tests = (
            REPOSITORY_ROOT
            / "tests"
            / "gfx"
            / "RendererChildLauncherTests.cpp"
        ).read_text(encoding="utf-8")
        launch = source[
            source.index(
                "RendererChildLaunchFailure LaunchRendererChildAndPropagateExit("
            ) :
        ]
        self.assertLess(
            launch.index("handoff.package_platform != host_platform"),
            launch.index("RendererFrontendChildExecutableName(handoff)"),
        )
        for token in (
            "GetFinalPathNameByHandleW",
            "IsSupportedFinalDosExecutablePath",
        ):
            with self.subTest(sibling_path_token=token):
                self.assertIn(token, sibling_path_source)
        for token in (
            "PROC_THREAD_ATTRIBUTE_HANDLE_LIST",
            "STARTF_USESTDHANDLES",
            "absent_handle_sentinel",
            "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE",
            "CREATE_SUSPENDED",
            "AssignProcessToJobObject",
            "ResumeThread",
            "ExitProcess(child_exit_code)",
            "execv(child_path.c_str()",
        ):
            with self.subTest(process_boundary_token=token):
                self.assertIn(token, source)
        self.assertLess(
            source.index("AssignProcessToJobObject"),
            source.index("ResumeThread"),
        )
        self.assertNotIn("CreateProcessA", source)
        self.assertNotIn("system(", source)
        self.assertNotIn("execvp", source)
        for token in (
            "foreign_platforms_rejected == 2U",
            "RoR-OgreNext.exe",
            "TestPosixExecFailureRestoresCloseOnExec",
            "--invoke-launcher-null-stdio",
            "--invoke-launcher-invalid-stdio",
            "CreateSymbolicLinkW",
            "regression skipped; CreateSymbolicLinkW error=",
        ):
            with self.subTest(launcher_test_token=token):
                self.assertIn(token, tests)

    def test_renderer_bridge_supervisor_fails_closed_at_process_boundary(
        self,
    ) -> None:
        source = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "system"
            / "RendererBridgeProcessSupervisor.cpp"
        ).read_text(encoding="utf-8")
        tests = (
            REPOSITORY_ROOT
            / "tests"
            / "gfx"
            / "RendererBridgeProcessSupervisorTests.cpp"
        ).read_text(encoding="utf-8")
        for token in (
            "PROC_THREAD_ATTRIBUTE_HANDLE_LIST",
            "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE",
            "CREATE_SUSPENDED",
            "AssignProcessToJobObject",
            "ResumeThread",
            "TerminateProcess",
            "FD_CLOEXEC",
            "pipe2(descriptors, O_CLOEXEC)",
            "setpgid",
            "execv(path.c_str()",
            "kill(-process_group, SIGTERM)",
            "kill(-process_group, SIGKILL)",
            "waitpid(-game, &first_status, 0)",
        ):
            with self.subTest(supervisor_boundary_token=token):
                self.assertIn(token, source)
        self.assertLess(
            source.index("AssignProcessToJobObject"),
            source.index("ResumeThread"),
        )
        self.assertLess(source.index("setpgid"), source.index("execv"))
        self.assertNotIn("CreateProcessA", source)
        self.assertNotIn("system(", source)
        self.assertNotIn("execvp", source)
        for token in (
            "TestPreflightFailuresCreateNoChildren",
            "TestExactSiblingBridgeAndGameExit",
            "TestPresentationFirstTerminatesGame",
            "TestSignalExitAndPropagation",
            "TestPartialStartupFailsClosed",
            "RequireNoChildProcesses",
        ):
            with self.subTest(supervisor_test_token=token):
                self.assertIn(token, tests)

    def test_byte_hashed_probe_inputs_are_checkout_stable(self) -> None:
        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(
            encoding="utf-8"
        )
        for path in (
            "cmake/RendererLauncherPackageConfig.cmake",
            "source/main/gfx/GfxScene.*",
            "source/main/gfx/RendererBackendPolicy.*",
            "source/main/gfx/RendererStartupHandoff.*",
            "source/main/gfx/RendererStartupPlan.*",
            "source/main/system/RendererBridge*",
            "source/main/system/RendererChildIntent.*",
            "source/main/system/RendererChildLauncher.*",
            "source/main/system/RendererOgre14GameBridge.*",
            "source/main/system/RendererOgre14GameHostSession.*",
            "source/main/system/RendererSiblingPath.*",
            "source/main/system/RendererPackagedMediaPath.*",
            "source/main/system/RendererPackageRuntimeProbe.*",
            "source/main/system/RendererBridgeEndpoint.*",
            "source/main/system/RendererBridgeLaunchPlan.*",
            "source/main/system/RendererBridgeProcessSupervisor.*",
            "source/main/system/RendererOgreNextChild.*",
            "source/main/system/RendererLauncherMain.cpp",
            "source/main/system/RendererLauncherPackageConfig.h.in",
            "source/main/system/RendererPublicLauncher.*",
            "source/main/gfx/render/**",
            "tests/gfx/RendererBackendPolicyTests.cpp",
            "tests/gfx/RendererChildIntentTests.cpp",
            "tests/gfx/RendererChildLauncherFakeChild.cpp",
            "tests/gfx/RendererChildLauncherTests.cpp",
            "tests/gfx/RendererBridge*Tests.cpp",
            "tests/gfx/RendererOgre14GameBridgeTests.cpp",
            "tests/gfx/RendererOgre14GameHostSessionTests.cpp",
            "tests/gfx/RendererSiblingPathTests.cpp",
            "tests/gfx/RendererPackageRuntimeProbeTests.cpp",
            "tests/gfx/RendererBridgeEndpointTests.cpp",
            "tests/gfx/RendererBridgeLaunchPlanTests.cpp",
            "tests/gfx/RendererBridgeProcessFakeChild.cpp",
            "tests/gfx/RendererBridgeProcessSupervisorTests.cpp",
            "tests/gfx/RendererOgreNextChildTests.cpp",
            "tests/gfx/RendererPublicLauncherLegacyChild.cpp",
            "tests/gfx/RendererPublicLauncherTests.cpp",
            "tests/cmake/VerifyRendererPublicBridgeExit.cmake",
            "tests/gfx/RendererStartupHandoffTests.cpp",
            "tests/gfx/RendererStartupPlanTests.cpp",
            "tests/gfx/render/Ogre14GraphicsSceneSourceTests.cpp",
            "tools/ogre_next_probe/**",
            "tools/run_ogre_next_probe.py",
            "tools/validate_ogre_next_frame_probe.py",
            "tools/verify_ogre_next_artifact_set.py",
            "tests/tools/test_ogre_next_child_runtime_contract.py",
            "doc/nextgen/evidence/OGRE_NEXT_METAL_*",
        ):
            with self.subTest(path=path):
                self.assertIn(f"{path} text eol=lf", attributes)
        self.assertIn(
            "source/main/terrain/TerrainObjectManager.* "
            "whitespace=cr-at-eol",
            attributes,
        )
        self.assertIn(
            "source/main/main.cpp -text whitespace=cr-at-eol", attributes
        )

    def test_frontend_transport_dispatcher_is_cross_platform_and_attested(self) -> None:
        probe_cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        native_cmake = (REPOSITORY_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        prelink = (
            REPOSITORY_ROOT
            / "tools"
            / "ogre_next_probe"
            / "cmake"
            / "VerifyN2SourceProvenance.cmake"
        ).read_text(encoding="utf-8")
        runner = (REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py").read_text(
            encoding="utf-8"
        )
        verifier = (
            REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
        ).read_text(encoding="utf-8")
        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(
            encoding="utf-8"
        )
        test_path = "tests/gfx/render/RendererFrontendTransportDispatcherTests.cpp"
        for manifest in (probe_cmake, prelink, runner, verifier):
            with self.subTest(manifest_size=len(manifest)):
                self.assertIn(test_path, manifest)
        self.assertIn(f"{test_path} text eol=lf", attributes)
        for source in (
            "RenderAssetDeltaTransport.cpp",
            "RenderBridgeSessionIdentity.cpp",
            "RenderTransportEnvelope.cpp",
            "RenderTransportStream.cpp",
            "RendererFrontendTransportDispatcher.cpp",
            "SceneSnapshotTransport.cpp",
        ):
            with self.subTest(probe_contract_source=source):
                self.assertIn(source, probe_cmake)
                self.assertIn(source, native_cmake)
        for token in (
            "add_executable(\n        ror_renderer_frontend_transport_dispatcher_tests",
            "PRIVATE ror_ogre_next_n1_contract",
            "add_test(NAME ror_renderer_frontend_transport_dispatcher",
        ):
            self.assertIn(token, probe_cmake)
        self.assertIn(
            "-R '^ror_renderer_frontend_transport_dispatcher$'", self.workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_graphics_scene_source$'", self.workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_particle_capture_source$'", self.workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_procedural_road_source$'", self.workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_road_material_transaction$'", self.workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_dynamic_material_closure$'", self.workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_material_semantic_registry$'", self.workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_source_texture_decoder$'", self.workflow
        )
        self.assertIn(
            "-R '^ror_ogre14_authenticated_texture_receipt$'", self.workflow
        )
        self.assertIn(
            "ror_renderer_frontend_transport_dispatcher_tests",
            native_cmake,
        )

    def test_ogre14_scene_adapter_is_exactly_tapped_and_fail_closed(self) -> None:
        main = (REPOSITORY_ROOT / "source/main/main.cpp").read_text(
            encoding="utf-8"
        )
        gfx_header = (
            REPOSITORY_ROOT / "source/main/gfx/GfxScene.h"
        ).read_text(encoding="utf-8")
        gfx_source = (
            REPOSITORY_ROOT / "source/main/gfx/GfxScene.cpp"
        ).read_text(encoding="utf-8")
        adapter = (
            REPOSITORY_ROOT
            / "source/main/gfx/render/Ogre14GraphicsSceneSource.cpp"
        ).read_text(encoding="utf-8")
        terrain_objects = (
            REPOSITORY_ROOT / "source/main/terrain/TerrainObjectManager.cpp"
        ).read_text(encoding="utf-8")
        terrain_objects_header = (
            REPOSITORY_ROOT / "source/main/terrain/TerrainObjectManager.h"
        ).read_text(encoding="utf-8")
        probe_cmake = (
            REPOSITORY_ROOT / "tools/ogre_next_probe/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        native_cmake = (REPOSITORY_ROOT / "tests/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        app_context = (
            REPOSITORY_ROOT / "source/main/AppContext.cpp"
        ).read_text(encoding="utf-8")
        input_engine = (
            REPOSITORY_ROOT / "source/main/utils/InputEngine.cpp"
        ).read_text(encoding="utf-8")
        product_header = (
            REPOSITORY_ROOT
            / "source/main/system/RendererOgre14ProductSession.h"
        ).read_text(encoding="utf-8")
        product_source = (
            REPOSITORY_ROOT
            / "source/main/system/RendererOgre14ProductSession.cpp"
        ).read_text(encoding="utf-8")
        producer_header = (
            REPOSITORY_ROOT
            / "source/main/gfx/render/GraphicsSceneSnapshotProducer.h"
        ).read_text(encoding="utf-8")
        host_header = (
            REPOSITORY_ROOT
            / "source/main/system/RendererOgre14GameHostSession.h"
        ).read_text(encoding="utf-8")
        host_source = (
            REPOSITORY_ROOT
            / "source/main/system/RendererOgre14GameHostSession.cpp"
        ).read_text(encoding="utf-8")

        buffer_call = "App::GetGfxScene()->BufferSimulationData();"
        scene_update_call = "App::GetGfxScene()->UpdateScene(dt_sim);"
        producer_call = "PostUpdatedScene("
        self.assertEqual(main.count(buffer_call), 1)
        self.assertEqual(main.count(scene_update_call), 1)
        self.assertEqual(main.count(producer_call), 1)
        self.assertLess(main.index(buffer_call), main.index(scene_update_call))
        self.assertLess(main.index(scene_update_call), main.index(producer_call))
        self.assertLess(main.index(producer_call), main.index("renderOneFrame()"))
        self.assertIn("if (renderer_game_bridge.active())", main)
        self.assertIn("EnableOgreNextDemoCapture();", main)

        main_compact = "".join(main.split())
        self.assertIn(
            "SetUpRendering(renderer_runtime_ownership)",
            main_compact,
        )
        self.assertEqual(
            main_compact.count(
                "SetUpInput(renderer_runtime_ownership)"
            ),
            2,
        )
        presentation_guard = main.index("legacy_frame_presentation_enabled")
        self.assertEqual(main.count("renderOneFrame()"), 1)
        self.assertLess(presentation_guard, main.index("renderOneFrame()"))
        self.assertIn(
            "system/RendererOgre14RuntimeOwnership.{h,cpp}",
            (REPOSITORY_ROOT / "source/main/CMakeLists.txt").read_text(
                encoding="utf-8"
            ),
        )
        self.assertIn(
            "ror_renderer_ogre14_runtime_ownership_tests", native_cmake
        )
        self.assertIn('miscParams["hidden"] = "true";', app_context)
        self.assertIn(
            "App::CreateInputEngine(enable_physical_input);", app_context
        )
        self.assertIn("if (m_physical_input_enabled)", input_engine)
        self.assertIn(
            "EnableRendererTransportInput()", input_engine
        )

        unload_body = main[
            main.index("case MSG_SIM_UNLOAD_TERRN_REQUESTED") :
            main.index("case MSG_SIM_LOAD_SAVEGAME_REQUESTED")
        ]
        for teardown in (
            "EndPostProcessScene();",
            "CleanUpSimulation();",
            "DeleteAllCharacters();",
            "UnloadTerrain();",
            "ClearScene();",
        ):
            with self.subTest(teardown=teardown):
                self.assertLess(
                    unload_body.index("ResetSceneGeneration();"),
                    unload_body.index(teardown),
                )
        self.assertIn("FinalizeSceneGeneration()", producer_header)
        self.assertIn("ResetSceneGeneration()", product_header)
        self.assertIn("CompleteSceneGeneration(", host_header)
        self.assertIn("SCENE_GENERATION_BOUNDARY_V1", host_source)
        product_reset = product_source[
            product_source.index(
                "RendererOgre14ProductSession::ResetSceneGeneration()"
            ) :
            product_source.index(
                "RendererOgre14ProductSession::Shutdown()"
            )
        ]
        self.assertLess(
            product_reset.index("producer_->FinalizeSceneGeneration()"),
            product_reset.index("pending_ = std::move(pending)"),
        )
        self.assertIn("CompleteSceneGenerationReset()", product_reset)
        self.assertLess(
            product_source.index("host_.PostPhysicsCapturedAtSurface("),
            product_source.index("host_.CompleteSceneGeneration("),
        )
        self.assertIn(
            "renderer_bridge_product_session->\n"
            "                                            Shutdown();",
            unload_body,
        )
        self.assertLess(
            unload_body.index("Shutdown();"),
            unload_body.index("ClearScene();"),
        )
        self.assertLess(
            unload_body.index("ClearScene();"),
            unload_body.index("MSG_APP_SHUTDOWN_REQUESTED"),
        )

        self.assertIn(
            "bool                               "
            "m_ogre_next_demo_capture_enabled = false;",
            gfx_header,
        )
        buffer_body = gfx_source[
            gfx_source.index("void GfxScene::BufferSimulationData()") :
            gfx_source.index("Render::ValidationResult "
                             "GfxScene::CaptureOgre14GraphicsScene")
        ]
        self.assertIn("if (!m_ogre_next_demo_capture_enabled)", buffer_body)
        self.assertLess(
            buffer_body.index("a->BufferSimulationData();"),
            buffer_body.index("if (!m_ogre_next_demo_capture_enabled)"),
        )

        capture_body = gfx_source[
            gfx_source.index("Render::ValidationResult "
                             "GfxScene::CaptureOgre14GraphicsScene") :
            gfx_source.index(
                "void GfxScene::CommitOgre14GraphicsSceneCapture")
        ]
        capture_compact = "".join(capture_body.split())
        self.assertNotIn("!m_all_gfx_characters.empty()", capture_body)
        self.assertIn("legacy player/network avatar domain", capture_body)
        self.assertIn("entity->hasSkeleton()", gfx_source)
        for available in (
            "ENVIRONMENT",
            "ASSETS",
            "STATIC_MESHES",
            "LIGHTS",
            "REFLECTION_PROBES",
        ):
            with self.subTest(available=available):
                self.assertIn(
                    f"Ogre14GraphicsSceneCaptureField::{available}",
                    capture_compact,
                )
        self.assertIn(
            "getMovableObjects(Ogre::MOT_LIGHT)", gfx_source
        )
        demo_light = gfx_source[
            gfx_source.index(
                "CaptureOgreNextDemoMainShadowLight("
            ) : gfx_source.index("NativeStaticFailure(")
        ]
        self.assertIn("light->getVisible()", demo_light)
        self.assertIn("light->getCastShadows()", demo_light)
        self.assertIn("candidate_count != 1U", demo_light)
        self.assertIn("candidate != terrain_main_light", demo_light)
        self.assertIn("input.visible = true;", demo_light)
        self.assertIn("input.casts_shadows = true;", demo_light)
        self.assertNotIn("light->isVisible()", gfx_source)
        self.assertIn("BuildOgre14GraphicsSceneLights(", gfx_source)
        self.assertIn("CaptureOgre14StaticMeshObjects(", gfx_source)
        self.assertIn("CaptureOgre14TerrainPages(", gfx_source)
        for terrain_tap in (
            "group->getTerrainSlots()",
            "terrain->getHeightData()",
            "terrain->getPointFromSelfOrNeighbour",
            "terrain->getHighestLodPrepared()",
            "terrain->getHighestLodLoaded()",
            "terrain->getTargetLodLevel()",
            "ResolveOgre14GraphicsSceneTerrainPageCacheEntry(",
            "OgreNextDemoTerrainCapture terrain_capture",
            "m_ogre_next_demo_terrain_source.Capture(",
        ):
            with self.subTest(terrain_tap=terrain_tap):
                self.assertIn(terrain_tap, gfx_source)
        self.assertIn("BuildOgre14GraphicsSceneStaticInventory(", gfx_source)
        dynamic_index_copy = gfx_source[
            gfx_source.index(
                "RoR::Render::ValidationResult CopyOgre14DynamicIndices") :
            gfx_source.index("using JoinedVertexRange")
        ]
        self.assertNotIn("HardwareBufferLockGuard", dynamic_index_copy)
        self.assertNotIn("HBL_READ_ONLY", dynamic_index_copy)
        self.assertNotIn("readData(", dynamic_index_copy)
        self.assertIn("output = topology.indices;", dynamic_index_copy)
        self.assertIn("getCpuTopologySections()", gfx_source)
        self.assertIn("object_manager->GetStaticGraphicsObjects()", gfx_source)
        self.assertIn("entity->getVisible() &&", gfx_source)
        self.assertIn("sub_entity->isVisible()", gfx_source)
        self.assertIn("entity->getRenderingDistance()", gfx_source)
        self.assertIn("_getRenderOperation(operation, 0U)", gfx_source)
        for diagnostic in (
            "static_meshes.unsupported.terrain",
            "static_meshes.unsupported.procedural",
            "static_meshes.unsupported.deformable",
            "static_meshes.unsupported.paged",
            "static_meshes.unsupported.animated",
        ):
            with self.subTest(static_diagnostic=diagnostic):
                self.assertIn(diagnostic, adapter)
        self.assertIn("m_next_static_graphics_object_id", terrain_objects)
        self.assertIn("m_static_graphics_objects.push_back", terrain_objects)
        self.assertIn("struct StaticGraphicsObject", terrain_objects_header)
        self.assertIn("GetStaticGraphicsObjects() const", terrain_objects_header)
        self.assertIn(
            "m_ogre14_light_identity_registry", gfx_header
        )
        self.assertIn("m_ogre14_static_identity_registry", gfx_header)
        self.assertIn("m_ogre14_static_mesh_cache", gfx_header)
        self.assertIn("m_ogre14_terrain_page_cache", gfx_header)
        reset_body = gfx_source[
            gfx_source.index(
                "void GfxScene::ResetOgre14GraphicsSceneGeneration()"
            ) :
            gfx_source.index("void GfxScene::Init()")
        ]
        for reset_token in (
            "DiscardOgre14GraphicsSceneCapture();",
            "m_ogre14_joined_buffer_epoch = 0U;",
            "m_ogre14_light_identity_registry.Reset();",
            "m_ogre14_static_identity_registry.Reset();",
            "m_ogre14_dynamic_identity_registry.Reset();",
            "m_ogre14_static_mesh_cache.clear();",
            "m_ogre14_terrain_page_cache.clear();",
            "m_ogre14_dynamic_mesh_cache.clear();",
            "m_gfx_actor_inventory.Clear();",
            "m_all_gfx_characters.clear();",
        ):
            with self.subTest(reset_token=reset_token):
                self.assertIn(reset_token, reset_body)
        for terrain_contract in (
            "kOgre14TerrainCpuCaptureVersion",
            "ValidateOgre14GraphicsSceneTerrainPageSet",
            "BuildOgre14GraphicsSceneTerrainGeometryStateKey",
            "BuildOgre14GraphicsSceneTerrainMeshPayload",
            "ResolveOgre14GraphicsSceneTerrainPageCacheEntry",
            "RegisterDerivedTerrainPageIdentity",
            "terrain_page_names_by_id_",
            "known_terrain_page_keys_",
            "live_terrain_page_keys_",
        ):
            with self.subTest(terrain_contract=terrain_contract):
                self.assertIn(terrain_contract, adapter)
        self.assertIn(
            "kOgre14LegacyDiffusePowerToCanonicalIntensity", adapter
        )
        self.assertIn(
            "missing required OGRE 14 joined fields: " + '" + missing',
            adapter,
        )
        for cmake in (probe_cmake, native_cmake):
            self.assertIn("Ogre14GraphicsSceneSource.cpp", cmake)
            self.assertIn("Ogre14LegacyAssetTranslator.cpp", cmake)
            self.assertIn("Ogre14LegacyMaterialClosure.cpp", cmake)
            self.assertIn("Ogre14GraphicsSceneSourceTests.cpp", cmake)
            self.assertIn("ror_ogre14_graphics_scene_source_tests", cmake)
            self.assertIn("Ogre14ParticleCaptureSource.cpp", cmake)
            self.assertIn("Ogre14ParticleCaptureSourceTests.cpp", cmake)
            self.assertIn("ror_ogre14_particle_capture_source_tests", cmake)
            self.assertIn("Ogre14ProceduralRoadSource.cpp", cmake)
            self.assertIn("Ogre14ProceduralRoadSourceTests.cpp", cmake)
            self.assertIn(
                "ror_ogre14_procedural_road_source_tests", cmake
            )
            self.assertIn(
                "Ogre14RoadMaterialTransactionTests.cpp", cmake
            )
            self.assertIn(
                "ror_ogre14_road_material_transaction_tests", cmake
            )
            self.assertIn(
                "Ogre14DynamicMaterialClosureTests.cpp", cmake
            )
            self.assertIn(
                "ror_ogre14_dynamic_material_closure_tests", cmake
            )
            self.assertIn(
                "Ogre14LegacyMaterialSemanticRegistryTests.cpp", cmake
            )
            self.assertIn(
                "ror_ogre14_material_semantic_registry_tests", cmake
            )
            self.assertIn("Ogre14SourceTextureDecoder.cpp", cmake)
            self.assertIn("Ogre14SourceTextureDecoderTests.cpp", cmake)
            self.assertIn(
                "ror_ogre14_source_texture_decoder_tests", cmake
            )
            self.assertIn(
                "Ogre14AuthenticatedTextureReceiptTests.cpp", cmake
            )
            self.assertIn(
                "ror_ogre14_authenticated_texture_receipt_tests", cmake
            )
        package_dependencies = probe_cmake[
            probe_cmake.index("set(_ror_n1_package_dependencies") :
            probe_cmake.index(")", probe_cmake.index(
                "set(_ror_n1_package_dependencies"
            ))
        ]
        self.assertEqual(
            package_dependencies.count(
                "ror_ogre14_graphics_scene_source_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_ogre14_particle_capture_source_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_ogre14_procedural_road_source_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_ogre14_road_material_transaction_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_ogre14_dynamic_material_closure_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_ogre14_material_semantic_registry_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_ogre14_source_texture_decoder_tests"
            ),
            1,
        )
        self.assertEqual(
            package_dependencies.count(
                "ror_ogre14_authenticated_texture_receipt_tests"
            ),
            1,
        )

    def test_procedural_road_combined_static_contract_is_registered(
        self,
    ) -> None:
        road_tests = (
            REPOSITORY_ROOT
            / "tests/gfx/render/Ogre14ProceduralRoadSourceTests.cpp"
        ).read_text(encoding="utf-8")
        road_source = (
            REPOSITORY_ROOT
            / "source/main/gfx/render/Ogre14ProceduralRoadSource.cpp"
        ).read_text(encoding="utf-8")
        method = road_tests[
            road_tests.index("void TestCombinedAuthoritativeStaticTransaction") :
            road_tests.index("void TestInventoryFailureGatesAndBounds")
        ]
        self.assertLess(
            method.index("BuildOgre14ProceduralRoadInventory("),
            method.index("BuildOgre14GraphicsSceneStaticInventory("),
        )
        for contract in (
            "durable_road_inventory.known_identity_count() == 0U",
            "ValidationCode::DUPLICATE_IDENTIFIER",
            "candidate_static_registry.object_identity_count() == 0U",
            "std::is_sorted",
            "SameSharedOwner",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, method)
        self.assertIn(
            "TestCombinedAuthoritativeStaticTransaction();", road_tests
        )
        self.assertIn(
            "BuildOgre14GraphicsSceneMaterialFallback(", road_source
        )
        self.assertNotIn("Ogre14LegacyAssetTranslator", road_source)

    def test_game_host_stream_is_strict_cross_platform_and_attested(self) -> None:
        probe_cmake = (
            REPOSITORY_ROOT / "tools" / "ogre_next_probe" / "CMakeLists.txt"
        ).read_text(encoding="utf-8")
        native_cmake = (REPOSITORY_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        prelink = (
            REPOSITORY_ROOT
            / "tools"
            / "ogre_next_probe"
            / "cmake"
            / "VerifyN2SourceProvenance.cmake"
        ).read_text(encoding="utf-8")
        runner = (REPOSITORY_ROOT / "tools" / "run_ogre_next_probe.py").read_text(
            encoding="utf-8"
        )
        verifier = (
            REPOSITORY_ROOT / "tools" / "verify_ogre_next_artifact_set.py"
        ).read_text(encoding="utf-8")
        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(
            encoding="utf-8"
        )
        channel_header = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "system"
            / "RendererBridgeChannel.h"
        ).read_text(encoding="utf-8")
        session_source = (
            REPOSITORY_ROOT
            / "source"
            / "main"
            / "system"
            / "RendererOgre14GameHostSession.cpp"
        ).read_text(encoding="utf-8")
        session_tests = (
            REPOSITORY_ROOT
            / "tests"
            / "gfx"
            / "RendererOgre14GameHostSessionTests.cpp"
        ).read_text(encoding="utf-8")
        paths = (
            "source/main/system/RendererOgre14GameHostSession.cpp",
            "source/main/system/RendererOgre14GameHostSession.h",
            "tests/gfx/RendererBridgeChannelTests.cpp",
            "tests/gfx/RendererOgre14GameHostSessionTests.cpp",
            "tests/gfx/render/RenderBridgeControlTransportTests.cpp",
        )
        for manifest in (probe_cmake, prelink, runner, verifier):
            for path in paths:
                with self.subTest(manifest_size=len(manifest), path=path):
                    self.assertIn(path, manifest)
        for source in (
            "InputEventTransport.cpp",
            "RenderBridgeControlTransport.cpp",
            "SceneGenerationBoundaryTransport.cpp",
            "RenderTransportEnvelope.cpp",
            "RenderTransportStream.cpp",
        ):
            with self.subTest(probe_contract_source=source):
                self.assertIn(source, probe_cmake)
                self.assertIn(source, native_cmake)
        for target in (
            "ror_render_bridge_control_transport_tests",
            "ror_renderer_bridge_channel_tests",
            "ror_renderer_ogre14_game_host_session_tests",
        ):
            with self.subTest(target=target):
                self.assertIn(target, probe_cmake)
                self.assertIn(target, native_cmake)
        self.assertIn(
            "Prove the game-host stream and native pipe half-close contract",
            self.workflow,
        )
        self.assertIn("ror_render_bridge_control_transport", self.workflow)
        self.assertIn("ror_renderer_bridge_channel", self.workflow)
        self.assertIn("ror_renderer_ogre14_game_host_session", self.workflow)
        for token in (
            "TryReadSome",
            "EnableNonblockingOutbound",
            "TryWriteSome",
            "CloseInbound",
            "CloseOutbound",
        ):
            with self.subTest(channel_seam=token):
                self.assertIn(token, channel_header)
        for token in (
            "PumpReverse(read_buffer",
            "reverse_queue.size() >= config.maximum_reverse_messages",
            "channel->TryWriteSome",
            "EnableNonblockingOutbound",
        ):
            with self.subTest(host_duplex_contract=token):
                self.assertIn(token, session_source)
        for token in (
            "TestReverseCapacityPausesForwardWrites",
            "TestCloseIsBoundedWhenPeerDoesNotDrainForwardPipe",
            "TestAcknowledgedSceneCanBePresentedByALaterAck",
            "TestQueuedSceneRetiresAcrossSurfaceBarrier",
        ):
            with self.subTest(host_duplex_test=token):
                self.assertIn(token, session_tests)
        for pattern in (
            "source/main/system/RendererOgre14GameHostSession.* text eol=lf",
            "tests/gfx/RendererOgre14GameHostSessionTests.cpp text eol=lf",
            "tests/gfx/render/RenderBridgeControlTransportTests.cpp text eol=lf",
        ):
            self.assertIn(pattern, attributes)

    def test_ogre_next_child_core_runs_in_the_normal_native_suite(self) -> None:
        native_cmake = (REPOSITORY_ROOT / "tests" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        target = native_cmake[
            native_cmake.index(
                "add_executable(\n    ror_renderer_ogre_next_child_tests"
            ) :
            native_cmake.index(
                "target_include_directories(\n"
                "    ror_renderer_ogre_next_child_tests"
            )
        ]
        for source in (
            "gfx/RendererOgreNextChildTests.cpp",
            "source/main/system/RendererOgreNextChild.cpp",
            "source/main/system/RendererChildIntent.cpp",
            "source/main/gfx/RendererStartupHandoff.cpp",
            "source/main/gfx/RendererStartupPlan.cpp",
            "source/main/gfx/RendererBackendPolicy.cpp",
        ):
            with self.subTest(native_child_source=source):
                self.assertEqual(target.count(source), 1)
        target_list = native_cmake[
            native_cmake.index("set(ROR_PHYSICS_TEST_TARGETS") :
            native_cmake.index(
                ")", native_cmake.index("set(ROR_PHYSICS_TEST_TARGETS")
            )
        ]
        self.assertEqual(
            target_list.count("ror_renderer_ogre_next_child_tests"), 1
        )
        self.assertIn("NAME renderer_ogre_next_child", native_cmake)

    def test_reports_and_exact_frame_are_always_retained(self) -> None:
        self.assertIn("if: always()", self.workflow)
        self.assertIn("actions/upload-artifact@043fb46d", self.workflow)
        self.assertIn("if-no-files-found: error", self.workflow)
        for artifact in (
            "ogre-next-build-contract.json",
            "ror-ogre-next-probe-report.json",
            "ror-ogre-next-frame-probe-report.json",
            "ror-ogre-next-frame-probe.ppm",
            "ror-ogre-next-frontend-n1-report.json",
            "ror-ogre-next-frontend-n1.ppm",
            "ror-ogre-next-frontend-rt4-pbr-v1-report.json",
            "ror-ogre-next-frontend-rt4-pbr-v1.ppm",
            "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-repeat/",
            "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json",
            "ror-ogre-next-pssm-shadow-report.json",
            "ror-ogre-next-pssm-shadow-isolation.bin",
            "bin/ror_ogre_next_pssm_shadow_smoke",
            "ror-ogre-next-n1-package/bin/ror_ogre_next_frontend_n1_smoke",
            "ror-ogre-next-n1-package/share/",
            "ror-ogre-next-metal-n2-report.json",
            "ror-ogre-next-metal-n2-probe.bin",
            "ror-ogre-next-metal-n2-attestation.json",
            "bin/ror_ogre_next_metal_n2_smoke",
            "ror-ogre-next-metal-n3-report.json",
            "ror-ogre-next-metal-n3-raster.bin",
            "ror-ogre-next-metal-n3-contribution.bin",
            "ror-ogre-next-metal-n3-hybrid.bin",
            "ror-ogre-next-metal-n3-attestation.json",
            "bin/ror_ogre_next_metal_n3_smoke",
            "ror-ogre-next-metal-n4-directional-shadow-report.json",
            "ror-ogre-next-metal-n4-raster.bin",
            "ror-ogre-next-metal-n4-visibility-r16.bin",
            "ror-ogre-next-metal-n4-ray-lineage-r32.bin",
            "ror-ogre-next-metal-n4-hybrid.bin",
            "bin/ror_ogre_next_metal_n4_directional_shadow_smoke",
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, self.workflow)
        lifecycle = self.workflow.index("- name: Re-run native lifecycle tests")
        revalidate = self.workflow.index(
            "- name: Revalidate the exact artifacts selected for upload"
        )
        complete = self.workflow.index(
            "- name: Require every exact upload artifact"
        )
        n2_complete = self.workflow.index(
            "- name: Verify attested Apple Metal N2 pass or skip evidence"
        )
        n3_complete = self.workflow.index(
            "- name: Verify attested Apple Metal N3 pass or skip evidence"
        )
        n4_complete = self.workflow.index(
            "- name: Verify Apple Metal N4 directional-shadow pass or skip evidence"
        )
        upload = self.workflow.index("- name: Upload exact reports and UI-free frame")
        self.assertLess(lifecycle, revalidate)
        self.assertLess(revalidate, complete)
        self.assertLess(complete, n2_complete)
        self.assertLess(n2_complete, n3_complete)
        self.assertLess(n3_complete, n4_complete)
        self.assertLess(n4_complete, upload)
        self.assertIn("verify_ogre_next_artifact_set.py", self.workflow)
        self.assertIn("--verify-metal-n2-evidence", self.workflow)
        self.assertIn("--verify-metal-n3-evidence", self.workflow)
        self.assertIn("--verify-metal-n4-evidence", self.workflow)
        for anchor in (
            "ROR_OGRE_NEXT_EXPECTED_ROR_REPOSITORY",
            "ROR_OGRE_NEXT_EXPECTED_ROR_REF",
            "ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT",
            "github.sha",
        ):
            self.assertIn(anchor, self.workflow)

    def test_n3_upload_is_a_self_contained_verifier_bundle(self) -> None:
        start = self.workflow.index(
            "- name: Upload attested Apple Metal N3 hybrid evidence"
        )
        end = self.workflow.index(
            "- name: Upload attested Vulkan RT5 external-device evidence"
        )
        bundle = self.workflow[start:end]
        for artifact in (
            "ogre-next-build-contract.json",
            "ror-ogre-next-frontend-rt4-pbr-v1-report.json",
            "ror-ogre-next-frontend-rt4-pbr-v1.ppm",
            "ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin",
            "ror-ogre-next-frontend-rt4-pbr-v1-attestation.json",
            "ror-ogre-next-n1-package/bin/ror_ogre_next_frontend_n1_smoke",
            "ror-ogre-next-n1-package/share/",
            "ror-ogre-next-metal-n3-report.json",
            "ror-ogre-next-metal-n3-attestation.json",
            "bin/ror_ogre_next_metal_n3_smoke",
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, bundle)

    def test_n4_upload_retains_report_executable_and_exact_readbacks(self) -> None:
        start = self.workflow.index(
            "- name: Upload verified Apple Metal N4 directional-shadow evidence"
        )
        end = self.workflow.index(
            "- name: Upload attested Vulkan RT5 external-device evidence"
        )
        bundle = self.workflow[start:end]
        for artifact in (
            "ogre-next-build-contract.json",
            "ror-ogre-next-metal-n4-directional-shadow-report.json",
            "ror-ogre-next-metal-n4-raster.bin",
            "ror-ogre-next-metal-n4-visibility-r16.bin",
            "ror-ogre-next-metal-n4-ray-lineage-r32.bin",
            "ror-ogre-next-metal-n4-hybrid.bin",
            "bin/ror_ogre_next_metal_n4_directional_shadow_smoke",
        ):
            with self.subTest(artifact=artifact):
                self.assertIn(artifact, bundle)

    def test_n1_is_independent_of_legacy_frame_runtime(self) -> None:
        n1 = self.workflow.index(
            "- name: Build, render, and validate the independent N1 frontend"
        )
        legacy = self.workflow.index(
            "- name: Build, render, read back, and validate the legacy probes"
        )
        n2 = self.workflow.index(
            "- name: Build and validate the independent Apple Metal N2 proof"
        )
        n3 = self.workflow.index(
            "- name: Build and validate the independent Apple Metal N3 hybrid proof"
        )
        n4 = self.workflow.index(
            "- name: Build and validate the Apple Metal N4 directional-shadow proof"
        )
        n1_native = self.workflow.index(
            "- name: Prove N1 lifecycle and media-integrity failures independently"
        )
        self.assertLess(n1, n1_native)
        self.assertLess(n1_native, n2)
        self.assertLess(n2, n3)
        self.assertLess(n3, n4)
        self.assertLess(n4, legacy)
        self.assertIn("--checkpoint n1", self.workflow)
        self.assertIn("--checkpoint n2", self.workflow)
        self.assertIn("--checkpoint n3", self.workflow)
        self.assertIn(
            "--target ror_ogre_next_metal_n4_directional_shadow_report",
            self.workflow,
        )
        self.assertIn("--checkpoint legacy", self.workflow)
        self.assertIn("--reuse-build-dir", self.workflow)
        self.assertIn(
            "-R '^ror_ogre_next_frontend_(n1_|rt4_)'", self.workflow
        )
        self.assertGreaterEqual(
            self.workflow.count("tools/validate_ogre_next_frame_probe.py"), 2
        )
        self.assertIn(
            "if: always() && runner.os == 'macOS'", self.workflow
        )
        self.assertIn(
            "Upload attested Apple Metal N2 capability evidence", self.workflow
        )
        self.assertIn(
            "Upload attested Apple Metal N3 hybrid evidence", self.workflow
        )
        self.assertIn(
            "Require directional PSSM native pass",
            self.workflow,
        )

    def test_verified_wrapper_owns_source_and_build_lifecycle(self) -> None:
        self.assertIn("tools/run_ogre_next_probe.py", self.workflow)
        self.assertNotIn("FETCHCONTENT_SOURCE_DIR_OGRE_NEXT", self.workflow)
        self.assertNotIn("git clone", self.workflow)
        self.assertNotIn("ogre-next master", self.workflow.lower())


if __name__ == "__main__":
    unittest.main()
