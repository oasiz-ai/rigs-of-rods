#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

from pathlib import Path
import re
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RUNTIME_TEST_PATH = "tests/tools/test_postprocess_runtime_contract.py"


def _read(relative_path: str) -> str:
    return (REPOSITORY_ROOT / relative_path).read_text(encoding="utf-8")


def _between(source: str, start: str, end: str) -> str:
    start_offset = source.index(start)
    end_offset = source.index(end, start_offset + len(start))
    return source[start_offset:end_offset]


class PostProcessRuntimeSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.application_h = _read("source/main/Application.h")
        cls.application_cpp = _read("source/main/Application.cpp")
        cls.cvar_cpp = _read("source/main/system/CVar.cpp")
        cls.app_context_h = _read("source/main/AppContext.h")
        cls.app_context_cpp = _read("source/main/AppContext.cpp")
        cls.main_cpp = _read("source/main/main.cpp")
        cls.content_h = _read("source/main/resources/ContentManager.h")
        cls.content_cpp = _read(
            "source/main/resources/ContentManager.cpp"
        )
        cls.runtime_h = _read(
            "source/main/gfx/PostProcessRuntime.h"
        )
        cls.runtime_cpp = _read(
            "source/main/gfx/PostProcessRuntime.cpp"
        )
        cls.runtime_contract_h = _read(
            "source/main/gfx/PostProcessRuntimeContract.h"
        )
        cls.runtime_contract_cpp = _read(
            "source/main/gfx/PostProcessRuntimeContract.cpp"
        )
        cls.source_cmake = _read("source/main/CMakeLists.txt")
        cls.tests_cmake = _read("tests/CMakeLists.txt")
        cls.root_cmake = _read("CMakeLists.txt")
        cls.workflow = _read(
            ".github/workflows/content-provenance.yml"
        )
        cls.workflow_contract = _read(
            "tests/tools/test_content_provenance_workflow_contract.py"
        )
        cls.compositor = _read(
            "resources/postprocess/ror_postprocess_v0a.compositor"
        )
        cls.postprocess_doc = _read(
            "doc/nextgen/POST_PROCESSING.md"
        )

    def test_archived_integer_cvar_is_exactly_default_off(self) -> None:
        declaration = "extern CVar* gfx_postprocess_mode;"
        definition = "CVar* gfx_postprocess_mode;"
        creation_pattern = re.compile(
            r'App::gfx_postprocess_mode\s*=\s*this->cVarCreate\('
            r'"gfx_postprocess_mode",\s*"Post-process mode",\s*'
            r'CVAR_ARCHIVE\s*\|\s*CVAR_TYPE_INT,\s*"0"\);'
        )
        self.assertEqual(self.application_h.count(declaration), 1)
        self.assertEqual(self.application_cpp.count(definition), 1)
        self.assertEqual(len(creation_pattern.findall(self.cvar_cpp)), 1)
        self.assertNotIn(
            "gfx_postprocess_mode = 1",
            _read("resources/skeleton/config/RoR.cfg"),
        )

    def test_resource_pack_is_dedicated_opt_in_and_packaged(self) -> None:
        self.assertEqual(
            self.content_h.count(
                "static const ResourcePack POSTPROCESS;"
            ),
            1,
        )
        self.assertEqual(
            self.content_cpp.count(
                'DECLARE_RESOURCE_PACK( POSTPROCESS,'
                '           "postprocess",          "PostProcessRG");'
            ),
            1,
        )
        self.assertEqual(
            self.runtime_cpp.count(
                "ContentManager::ResourcePack::POSTPROCESS"
            ),
            1,
        )
        gameplay_loader = _between(
            self.content_cpp,
            "void ContentManager::LoadGameplayResources()",
            "std::string ContentManager::ListAllUserContent()",
        )
        self.assertNotIn("POSTPROCESS", gameplay_loader)
        self.assertIn(
            'recursive_zip_folder("${CMAKE_SOURCE_DIR}/resources" '
            '"${RUNTIME_OUTPUT_DIRECTORY}/resources")',
            self.source_cmake,
        )
        self.assertIn(
            "install(DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY}/resources/ "
            'DESTINATION resources COMPONENT "Base_Game")',
            self.source_cmake,
        )

    def test_scene_attach_detach_and_order_maintenance_are_sequenced(
        self,
    ) -> None:
        load_block = _between(
            self.main_cpp,
            "if (App::GetGameContext()->LoadTerrain(m.description))",
            "else\n                        {",
        )
        load_offsets = [
            load_block.index("BeginPostProcessScene();"),
            load_block.index("CreatePlayerCharacter();"),
            load_block.index("CreateOverlayWrapper();"),
        ]
        self.assertEqual(load_offsets, sorted(load_offsets))

        unload_block = _between(
            self.main_cpp,
            "case MSG_SIM_UNLOAD_TERRN_REQUESTED:",
            "case MSG_SIM_LOAD_SAVEGAME_REQUESTED:",
        )
        unload_offsets = [
            unload_block.index("EndPostProcessScene();"),
            unload_block.index('SaveScene("autosave.sav")'),
            unload_block.index("UnloadTerrain();"),
            unload_block.index("ClearScene();"),
        ]
        self.assertEqual(unload_offsets, sorted(unload_offsets))

        render_window_block = _between(
            self.main_cpp,
            "Ogre::RenderWindow* render_window =",
            "} // Render block",
        )
        self.assertLess(
            render_window_block.index(
                "MaintainPostProcessSceneOrder();"
            ),
            render_window_block.index("renderOneFrame();"),
        )

    def test_app_context_owns_resize_readback_and_shutdown_hooks(
        self,
    ) -> None:
        for declaration in (
            "void                 BeginPostProcessScene();",
            "void                 EndPostProcessScene();",
            "void                 MaintainPostProcessSceneOrder();",
            "PostProcessRuntime   m_postprocess_runtime;",
        ):
            self.assertEqual(self.app_context_h.count(declaration), 1)

        destructor = _between(
            self.app_context_cpp,
            "AppContext::~AppContext()",
            "// --------------------------\n// Input handling",
        )
        self.assertLess(
            destructor.index("m_postprocess_runtime.Shutdown();"),
            destructor.index("destroyRenderTarget(m_render_window)"),
        )

        resize = _between(
            self.app_context_cpp,
            "void AppContext::windowResized",
            "void AppContext::windowFocusChange",
        )
        self.assertLess(
            resize.index("RefreshRenderDisplayMetrics"),
            resize.index("OnMainViewportResized"),
        )

        screenshot = _between(
            self.app_context_cpp,
            "void AppContext::CaptureScreenshot()",
            "void AppContext::FinishPendingScreenshot()",
        )
        self.assertLess(
            screenshot.index("FinishPendingScreenshot();"),
            screenshot.index("BeforeMainWindowReadback();"),
        )
        self.assertNotIn("setCompositorEnabled", screenshot)

    def test_only_main_target_viewport_zero_can_receive_v0a(self) -> None:
        self.assertIn(
            "m_main_viewport->getTarget() != m_main_render_target",
            self.runtime_cpp,
        )
        self.assertIn(
            "m_main_render_target->getViewport(0U) != "
            "m_main_viewport",
            self.runtime_cpp,
        )
        begin_wrapper = _between(
            self.app_context_cpp,
            "void AppContext::BeginPostProcessScene()",
            "void AppContext::EndPostProcessScene()",
        )
        self.assertIn(
            "m_postprocess_runtime.BeginScene(\n"
            "        m_viewport,\n"
            "        m_render_window,",
            begin_wrapper,
        )
        self.assertNotIn("addViewport", self.runtime_cpp)
        self.assertNotIn("CreateCustomRenderWindow", self.runtime_cpp)
        self.assertEqual(
            self.runtime_cpp.count("manager.addCompositor("),
            1,
        )

    def test_v0a_is_last_scene_compositor_and_never_toggles_ui(
        self,
    ) -> None:
        self.assertIn(
            "manager.addCompositor(\n"
            "                m_main_viewport,\n"
            "                V0A_COMPOSITOR_NAME,\n"
            "                -1)",
            self.runtime_cpp,
        )
        self.assertIn(
            "instances.back() != instance",
            self.runtime_cpp,
        )
        self.assertIn(
            "SCENE_COMPOSITOR_CHAIN_CHANGED",
            self.runtime_cpp,
        )
        self.assertNotIn("setOverlaysEnabled", self.runtime_cpp)
        self.assertIn(
            "getOverlaysEnabled() != overlays_before",
            self.runtime_cpp,
        )

        squashed_compositor = re.sub(
            r"\s+", " ", self.compositor
        )
        self.assertIn(
            "texture ror_v0a_scene target_width target_height "
            "PF_BYTE_RGBA no_fsaa",
            squashed_compositor,
        )
        self.assertIn("target ror_v0a_scene { input previous }",
                      squashed_compositor)
        self.assertNotIn("render_scene", self.compositor.lower())
        self.assertNotIn("overlay", self.compositor.lower())

    def test_backend_resource_and_error_gates_fail_closed(self) -> None:
        for exact_renderer in (
            '"opengl 3+ rendering subsystem"',
            '"direct3d11 rendering subsystem"',
        ):
            self.assertIn(exact_renderer, self.runtime_contract_cpp)
        for syntax in ("glsl330", "vs_4_0", "ps_4_0"):
            self.assertIn(
                f'isSyntaxSupported("{syntax}")',
                self.runtime_cpp,
            )
        for resource_name in (
            "ror_postprocess_v0a.compositor",
            "ror_postprocess_v0a.material",
            "ror_postprocess_v0a.program",
            "ror_postprocess_v0a_d3d11.hlsl",
            "ror_postprocess_v0a_gl3plus.frag",
            "ror_postprocess_v0a_gl3plus.vert",
        ):
            self.assertEqual(self.runtime_cpp.count(resource_name), 1)
        self.assertIn(
            "PostProcessLifecycleEventType::ADAPTER_FAILED",
            self.runtime_cpp,
        )
        self.assertIn("DetachNoThrow();", self.runtime_cpp)
        self.assertIn(
            "PostProcessPolicyStatus::PROGRAM_UNAVAILABLE",
            self.runtime_cpp,
        )
        self.assertIn("program->hasCompileError()", self.runtime_cpp)
        self.assertIn(
            "BoundPostProcessDiagnosticDetail(detail, 160U)",
            self.runtime_cpp,
        )
        self.assertIn(
            "Diagnostics must never turn an optional effect into a crash.",
            self.runtime_cpp,
        )

    def test_dependency_light_cpp_contract_is_built_and_registered(
        self,
    ) -> None:
        for source_entry in (
            "gfx/PostProcessPolicy.{h,cpp}",
            "gfx/PostProcessRuntime.{h,cpp}",
            "gfx/PostProcessRuntimeContract.{h,cpp}",
        ):
            self.assertEqual(self.source_cmake.count(source_entry), 1)
        self.assertIn(
            "ror_post_process_runtime_contract_tests",
            self.tests_cmake,
        )
        self.assertIn(
            "NAME post_process_runtime_contract",
            self.tests_cmake,
        )
        self.assertIn(
            "ResolvePostProcessLifecycleTransition",
            self.runtime_contract_h,
        )
        self.assertIn(
            "ClassifyPostProcessBackend",
            self.runtime_contract_h,
        )
        self.assertIn("set(CMAKE_CXX_STANDARD 17)",
                      self.root_cmake)

    def test_portable_source_contract_runs_normal_and_optimized(self) -> None:
        self.assertEqual(self.workflow.count(RUNTIME_TEST_PATH), 2)
        self.assertEqual(
            self.workflow_contract.count(
                "POSTPROCESS_RUNTIME_TEST_PATH"
            ),
            2,
        )

    def test_docs_keep_v0a_scope_and_remaining_gates_honest(self) -> None:
        squashed_doc = re.sub(
            r"\s+", " ", self.postprocess_doc
        )
        for statement in (
            "`gfx_postprocess_mode = 0` is the default",
            "`gfx_postprocess_mode = 1`",
            "V0A is not V0 completion",
            "no bloom, HDR, PBR, ray tracing, or AirSim-parity claim",
            "Native GL3Plus and D3D11 image/performance acceptance",
        ):
            self.assertIn(statement, squashed_doc)


if __name__ == "__main__":
    unittest.main()
