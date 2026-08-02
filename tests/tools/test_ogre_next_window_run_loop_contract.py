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
            "node->setNumTargetPass(2U)",
            "main_target->addPass(Ogre::PASS_SCENE)",
            "scene->mIncludeOverlays = false",
            "presentation_target->addPass(Ogre::PASS_QUAD)",
            'copy->mMaterialName = "Ogre/Copy/4xFP32"',
            "channels.push_back(production_source_target)",
            "channels.push_back(window_texture)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, graph)
        self.assertNotIn("memcpy", graph)

    def test_render_reuses_owned_graph_instead_of_frame_local_resources(self) -> None:
        render = self.frontend[self.frontend.index("OgreNextN1Frontend::Render(") :]
        production = render[
            render.index("if (production_presentation) {") :
            render.index("} else if (!persistent_hdr)")
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
            "final_audit.compositor_workspace_creates ==",
            "ror.ogre_next_n1_production_run_loop.v1",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.smoke)
        self.assertNotIn("memcpy", self.smoke)

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
        runtime_gate = self.cmake[
            self.cmake.index(
                "if (APPLE AND TARGET ror_ogre_next_window_run_loop_smoke)"
            ) :
            self.cmake.index(
                "if (TARGET ror_renderer_ogre_next_child_runtime)"
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
