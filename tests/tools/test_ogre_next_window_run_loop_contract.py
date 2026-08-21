#!/usr/bin/env python3
"""Fail-closed source contract for persistent Ogre-Next presentation."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class OgreNextWindowRunLoopContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (
            ROOT / "source/main/gfx/render/ogrenext/OgreNextN1Frontend.h"
        ).read_text(encoding="utf-8")
        cls.frontend = (
            ROOT / "source/main/gfx/render/ogrenext/OgreNextN1Frontend.cpp"
        ).read_text(encoding="utf-8")
        cls.runtime_h = (
            ROOT / "source/main/system/RendererOgreNextSdlWindowRuntime.h"
        ).read_text(encoding="utf-8")
        cls.runtime = (
            ROOT / "source/main/system/RendererOgreNextSdlWindowRuntime.cpp"
        ).read_text(encoding="utf-8")
        cls.smoke = (
            ROOT / "tools/ogre_next_probe/src/window_run_loop_smoke.cpp"
        ).read_text(encoding="utf-8")
        cls.cmake = (ROOT / "tools/ogre_next_probe/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.workflow = (
            ROOT / ".github/workflows/ogre-next-probe.yml"
        ).read_text(encoding="utf-8")

    def test_production_mode_is_separate_and_not_the_default(self) -> None:
        self.assertIn("enum class OgreNextN1PresentationMode", self.header)
        self.assertIn("EXACT_ONE_FRAME_GATE = 0", self.header)
        self.assertIn("PRODUCTION_RUN_LOOP", self.header)
        configuration = self.header[
            self.header.index("struct OgreNextN1PresentationConfiguration") :
            self.header.index("struct OgreNextN1PresentationAudit")
        ]
        self.assertIn(
            "OgreNextN1PresentationMode::EXACT_ONE_FRAME_GATE", configuration
        )
        self.assertIn(
            "configuration.mode !=\n"
            "            OgreNextN1PresentationMode::EXACT_ONE_FRAME_GATE",
            self.frontend,
        )

    def test_exact_one_frame_rejection_remains_mode_guarded(self) -> None:
        render = self.frontend[self.frontend.index("OgreNextN1Frontend::Render(") :]
        rejection = render.index(
            "the first native presentation gate admits exactly one presented frame"
        )
        guarded = render.rfind("if (!production_presentation", 0, rejection)
        self.assertNotEqual(guarded, -1)
        self.assertIn(
            "impl_->presentation_audit.presented_frames != 0U",
            render[guarded:rejection],
        )

    def test_production_graph_has_stable_gpu_only_two_channel_shape(self) -> None:
        graph = self.frontend[
            self.frontend.index("RebindProductionPresentationWorkspace(") :
            self.frontend.index("[[nodiscard]] bool DestroyPresentationResources()")
        ]
        for token in (
            "kProductionPresentationTargetName",
            "kProductionPresentationNodeName",
            "kProductionPresentationWorkspaceName",
            "Ogre::TextureFlags::RenderToTexture",
            '"MainRT", 0U',
            '"PresentationRT", 1U',
            "node->setNumTargetPass(copy_only_hdr ? 1U : 2U)",
            "production_source_target = hdr_output_target",
            "if (!copy_only_hdr)",
            "main_target->addPass(Ogre::PASS_SCENE)",
            "scene->mIncludeOverlays = false",
            "presentation_target->addPass(Ogre::PASS_QUAD)",
            'copy->mMaterialName = "Ogre/Copy/4xFP32"',
            "channels.push_back(production_source_target)",
            "channels.push_back(window_texture)",
            "kProductionPresentationShadowNodeName",
            "CreateAndVerifyPssmShadowNode(",
            "BindAndVerifyPssmWorkspace(",
        ):
            with self.subTest(token=token):
                self.assertIn(token, graph)
        self.assertNotIn("memcpy", graph)

    def test_production_output_is_gpu_only_and_released_exactly_once(self) -> None:
        for token in (
            "bool gpu_only_output = false",
            "gpu_only_output_frames",
            "ResourceHandlePool production_output_handles",
            "production_output_handles.Allocate()",
            "attachment.gpu_resource = production_output_resource",
            "production_output_handles.IsLive(resource)",
            "production_output_handles.Release(resource)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header + self.frontend)
        gpu_branch = self.frontend[
            self.frontend.index("if (gpu_only_output) {") :
            self.frontend.index("if (UsesMetalImageInterop", self.frontend.index("if (gpu_only_output) {"))
        ]
        self.assertNotIn("convertFromTexture", gpu_branch.split("} else {", 1)[0])
        self.assertIn("convertFromTexture", gpu_branch.split("} else {", 1)[1])

    def test_production_pssm_is_persistent_and_balanced(self) -> None:
        graph = self.frontend[
            self.frontend.index("DestroyProductionPresentationGraph()") :
            self.frontend.index("[[nodiscard]] bool DestroyPresentationResources()")
        ]
        for token in (
            "production_shadow_node_definition_created",
            "production_shadow_visibility_mask",
            "removeShadowNodeDefinition(",
            "shadow_audit.shadow_node_creates",
            "shadow_audit.shadow_node_destroys",
            "production_workspace->findShadowNode(",
        ):
            with self.subTest(token=token):
                self.assertIn(token, graph)
        self.assertNotIn(
            "the first production presentation run loop requires directional shadows disabled",
            self.frontend,
        )

    def test_render_reuses_owned_graph_instead_of_frame_local_resources(self) -> None:
        render = self.frontend[self.frontend.index("OgreNextN1Frontend::Render(") :]
        production = render[
            render.index("if (production_presentation) {") :
            render.index("} else if (persistent_hdr) {")
        ]
        self.assertIn("EnsureProductionPresentationGraph", production)
        self.assertIn("impl_->production_source_target", production)
        self.assertIn("impl_->production_workspace", production)
        cleanup = render[
            render.index("const auto cleanup_scene") :
            render.index("const auto destroy_retained_target")
        ]
        self.assertGreaterEqual(
            cleanup.count("!persistent_hdr && !production_presentation"), 2
        )

    def test_surface_transitions_retire_before_graph_rebuild(self) -> None:
        update = self.frontend[
            self.frontend.index("OgreNextN1Frontend::UpdateSurface(") :
            self.frontend.index("OgreNextN1Frontend::SynchronizeAssets(")
        ]
        self.assertIn("retire_production_graph", update)
        self.assertIn("DestroyProductionPresentationGraph()", update)
        self.assertIn("suspended_surface_updates", update)
        self.assertIn("restored_surface_updates", update)
        self.assertIn("production_window_texture", update)
        cleanup = self.frontend[
            self.frontend.index("[[nodiscard]] bool CleanupBackend()") :
            self.frontend.index("Ogre::AbiCookie abi_cookie")
        ]
        self.assertLess(
            cleanup.index("DestroyProductionPresentationGraph()"),
            cleanup.index("destroySceneManager"),
        )

    def test_audit_tracks_lifetime_and_monotonic_frames(self) -> None:
        for token in (
            "monotonic_presented_frame_ids",
            "show_callback_calls",
            "source_target_creates",
            "source_target_destroys",
            "compositor_node_definition_creates",
            "compositor_node_definition_destroys",
            "compositor_workspace_creates",
            "compositor_workspace_destroys",
            "compositor_workspace_rebinds",
            "surface_graph_rebuilds",
            "first_presented_frame_id",
            "last_presented_frame_id",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.header)
                self.assertIn(f"presentation_audit.{token}", self.frontend)
        self.assertIn(
            "request.frame_id >\n"
            "                impl_->presentation_audit.last_presented_frame_id",
            self.frontend,
        )

    def test_sdl_event_batch_covers_window_and_retina_transitions(self) -> None:
        for token in (
            "kRendererOgreNextSdlWindowEventContractVersion = 1U",
            "RendererOgreNextSdlWindowEventBatch",
            "close_events",
            "focus_gained_events",
            "focus_lost_events",
            "resize_events",
            "minimize_events",
            "restore_events",
            "display_change_events",
            "drawable_size_changed",
            "PollWindowEvents",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.runtime_h)
        for token in (
            "SDL_PumpEvents()",
            "SDL_PeepEvents(",
            "SDL_WINDOWEVENT_CLOSE",
            "SDL_WINDOWEVENT_FOCUS_GAINED",
            "SDL_WINDOWEVENT_FOCUS_LOST",
            "SDL_WINDOWEVENT_RESIZED",
            "SDL_WINDOWEVENT_SIZE_CHANGED",
            "SDL_WINDOWEVENT_MINIMIZED",
            "SDL_WINDOWEVENT_RESTORED",
            "SDL_WINDOWEVENT_DISPLAY_CHANGED",
            "SDL_GetWindowSizeInPixels",
            "m_has_drawable_baseline",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.runtime)

    def test_native_smoke_requires_1000_frames_events_and_balanced_cleanup(self) -> None:
        for token in (
            "kRequiredPresentedFrames = 1000U",
            "OgreNextN1PresentationMode::PRODUCTION_RUN_LOOP",
            "frontend.PresentBootstrapFrame()",
            "bootstrap_audit.bootstrap_clear_only",
            "bootstrap_audit.bootstrap_presented_before_scene",
            "bootstrap_audit.presented_frames == 0U",
            "bootstrap_audit.source_scene_passes == 0U",
            "bootstrap_audit.bootstrap_window_swap_completions == 1U",
            "host.Resize(104U, 72U)",
            "resized_bootstrap_audit.bootstrap_window_swap_completions ==",
            "suspended_bootstrap.code ==",
            "restored_bootstrap_audit.bootstrap_window_swap_completions ==",
            "restored_bootstrap_audit.presented_frames == 0U",
            "restored_bootstrap_audit.source_scene_passes == 0U",
            "const RenderAssetDelta catalog = MakeCatalog()",
            "const std::shared_ptr<const SceneSnapshot> scene = MakeScene()",
            "for (std::uint64_t frame_id = 1U;",
            "host.Resize(112U, 80U)",
            "host.Suspend()",
            "host.Resume()",
            "host.RefreshMetrics()",
            "SDL_WINDOWEVENT_FOCUS_LOST",
            "SDL_WINDOWEVENT_FOCUS_GAINED",
            "SDL_WINDOWEVENT_MINIMIZED",
            "SDL_WINDOWEVENT_RESTORED",
            "SDL_WINDOWEVENT_DISPLAY_CHANGED",
            "SDL_WINDOWEVENT_CLOSE",
            "live_audit.show_callback_calls == 1U",
            "live_audit.source_target_creates <",
            "final_audit.source_target_creates ==",
            "final_audit.bootstrap_node_definition_creates ==",
            "final_audit.bootstrap_workspace_creates ==",
            "final_audit.compositor_workspace_creates ==",
            "ror.ogre_next_n1_production_run_loop.v1",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.smoke)
        self.assertNotIn("memcpy", self.smoke)
        initialize = self.smoke.index("frontend.Initialize(")
        bootstrap = self.smoke.index("frontend.PresentBootstrapFrame()")
        resize = self.smoke.index("host.Resize(104U, 72U)", bootstrap)
        resized_clear = self.smoke.index(
            "frontend.PresentBootstrapFrame()", resize
        )
        suspend = self.smoke.index("host.Suspend()", resized_clear)
        suspended_clear = self.smoke.index(
            "frontend.PresentBootstrapFrame()", suspend
        )
        restore = self.smoke.index("host.Resume()", suspended_clear)
        restored_clear = self.smoke.index(
            "frontend.PresentBootstrapFrame()", restore
        )
        catalog = self.smoke.index(
            "const RenderAssetDelta catalog = MakeCatalog()", restored_clear
        )
        synchronize = self.smoke.index(
            "frontend.SynchronizeAssets(catalog)", catalog
        )
        first_scene = self.smoke.index("frontend.Render(request, output)")
        self.assertLess(initialize, bootstrap)
        self.assertLess(bootstrap, resize)
        self.assertLess(resize, resized_clear)
        self.assertLess(resized_clear, suspend)
        self.assertLess(suspend, suspended_clear)
        self.assertLess(suspended_clear, restore)
        self.assertLess(restore, restored_clear)
        self.assertLess(restored_clear, catalog)
        self.assertLess(catalog, synchronize)
        self.assertLess(synchronize, first_scene)

    def test_windows_console_entrypoint_bypasses_sdl_main_rewrite(self) -> None:
        handled = self.smoke.index("#define SDL_MAIN_HANDLED")
        include = self.smoke.index("#include <SDL.h>")
        entrypoint = self.smoke.index("int main(int argc, char **argv)")
        ready = self.smoke.index("SDL_SetMainReady();", entrypoint)
        adapter = self.smoke.index(
            "RoR::RendererOgreNextSdlWindowRuntime adapter;", entrypoint
        )
        self.assertLess(handled, include)
        self.assertLess(entrypoint, ready)
        self.assertLess(ready, adapter)

    def test_strict_cross_platform_build_and_mac_runtime_gate_are_registered(self) -> None:
        for token in (
            "ror_ogre_next_window_run_loop_smoke",
            "src/window_run_loop_smoke.cpp",
            "ogre-next-window-run-loop.ppm",
            "ogre-next-window-run-loop.json",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.cmake)
        target = self.cmake[
            self.cmake.index("set(_ror_n1_targets") :
            self.cmake.index("if (ROR_OGRE_NEXT_VULKAN_RT5")
        ]
        self.assertIn("ror_ogre_next_window_run_loop_smoke", target)
        source_manifest = self.cmake[
            self.cmake.index("list(APPEND _ror_relevant_source_files") :
            self.cmake.index(
                "list(FILTER _ror_relevant_source_files EXCLUDE REGEX"
            )
        ]
        self.assertIn(
            '"tests/tools/test_ogre_next_window_run_loop_contract.py"',
            source_manifest,
        )
        runtime_gate_start = self.cmake.index(
            "if (APPLE AND TARGET ror_ogre_next_window_run_loop_smoke)"
        )
        runtime_gate = self.cmake[
            runtime_gate_start : self.cmake.index(
                "if (TARGET ror_renderer_ogre_next_child_runtime)",
                runtime_gate_start,
            )
        ]
        self.assertIn("TIMEOUT 180", runtime_gate)
        self.assertNotIn("SKIP_RETURN_CODE", runtime_gate)
        self.assertIn(
            "Require 1,000-frame production Ogre-Next run loop", self.workflow
        )
        self.assertIn("if: runner.os == 'macOS'", self.workflow)
        self.assertIn(
            "-R '^ror_ogre_next_window_run_loop_smoke$'", self.workflow
        )


if __name__ == "__main__":
    unittest.main()
