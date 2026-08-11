#!/usr/bin/env python3
"""Static fail-closed contract for the one-process combined game loop."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RendererCombinedGameWiringContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main = (ROOT / "source/main/main.cpp").read_text(encoding="utf-8")
        cls.context_h = (ROOT / "source/main/AppContext.h").read_text(
            encoding="utf-8"
        )
        cls.context = (ROOT / "source/main/AppContext.cpp").read_text(
            encoding="utf-8"
        )
        cls.presenter = (
            ROOT
            / "source/main/system/RendererOgreNextInProcessPresenter.cpp"
        ).read_text(encoding="utf-8")
        cls.input_engine = (
            ROOT / "source/main/utils/InputEngine.cpp"
        ).read_text(encoding="utf-8")
        cls.loading = (
            ROOT / "source/main/gui/panels/GUI_LoadingWindow.cpp"
        ).read_text(encoding="utf-8")

    def test_finder_launch_is_exact_cityworld_alexis_demo(self) -> None:
        start = self.main.index(
            "// There is intentionally no transported menu/HUD"
        )
        end = self.main.index("#else", start)
        block = self.main[start:end]
        self.assertIn("if (argc == 1)", block)
        for argument in (
            '"-checkcache"',
            '"-map"',
            '"CityWorld.terrn2"',
            '"-truck"',
            '"AlexisSaber.truck"',
            '"-enter"',
        ):
            self.assertEqual(block.count(argument), 1)
        self.assertIn("argc = 7;", block)
        self.assertIn("argv = renderer_combined_demo_arguments.data();", block)
        self.assertEqual(block.count("if (argc"), 1)

    def test_combined_entrypoint_has_no_bridge_child_or_transport_runtime(self) -> None:
        include_start = self.main.index(
            "#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)\n"
            '#include "RendererInProcessSession.h"'
        )
        include_end = self.main.index("#else", include_start)
        combined_includes = self.main[include_start:include_end]
        for forbidden in ("GameBridge", "ProductSession", "Transport"):
            self.assertNotIn(forbidden, combined_includes)

        entry_start = self.main.index(
            "// The combined executable has no bridge endpoint"
        )
        entry_end = self.main.index("#else", entry_start)
        combined_entry = self.main[entry_start:entry_end]
        for forbidden in (
            "RendererOgre14GameBridge",
            "RendererOgre14ProductSession",
            "renderer_game_bridge",
            "renderer_bridge_product_session",
        ):
            self.assertNotIn(forbidden, combined_entry)

    def test_combined_path_owns_sdl_input_and_frame_order(self) -> None:
        loop = self.main[self.main.index("while (App::app_state") :]
        self.assertRegex(
            loop,
            r"#if !defined\(ROR_OGRE_NEXT_COMBINED_RUNTIME\)\s*"
            r"OgreBites::WindowEventUtilities::messagePump\(\);\s*"
            r"App::GetAppContext\(\)->ProcessWindowEvents\(\);",
        )
        capture = loop.index("App::GetInputEngine()->Capture();")
        pump = loop.index("PumpEventsBeforeSimulation();", capture)
        post = loop.index("PostUpdatedScene(", pump)
        update = loop.rfind("UpdateScene(dt_sim)", pump, post)
        self.assertLess(capture, pump)
        self.assertLess(update, post)
        self.assertIn("SkipUpdatedScene();", loop[post:])
        self.assertIn("if (!renderer_combined_simulation_granted)", loop)

    def test_presenter_precedes_hidden_host_and_outlives_it(self) -> None:
        presenter = self.main.index(
            "RendererOgreNextInProcessPresenter renderer_combined_presenter"
        )
        presenter_guard = self.main.index(
            "CombinedPresenterWindowGuard renderer_combined_window_guard",
            presenter,
        )
        legacy_guard = self.main.index(
            "RendererRuntimeGuard renderer_runtime_guard", presenter_guard
        )
        prepare = self.main.index("PrepareWindow(presenter_config)")
        setup = self.main.index("SetUpRendering(\n                renderer_runtime_ownership)")
        protect = self.main.index("ProtectHiddenResourceWindow(", setup)
        self.assertLess(presenter, presenter_guard)
        self.assertLess(presenter_guard, legacy_guard)
        self.assertLess(prepare, setup)
        self.assertLess(setup, protect)
        self.assertIn(
            "m_presenter.ProtectHiddenResourceWindow(nullptr)", self.main
        )
        self.assertIn("m_presenter.ShutdownWindow()", self.main)

    def test_media_provider_pair_and_bundle_fallback_are_fail_closed(self) -> None:
        self.assertIn(
            "ROR_OGRE_NEXT_COMBINED_SHADER_MEDIA_ROOT", self.main
        )
        self.assertIn(
            "ROR_OGRE_NEXT_COMBINED_PRESENTATION_MEDIA_ROOT", self.main
        )
        self.assertIn(
            '#error "combined renderer media roots must be defined as an exact pair"',
            self.main,
        )
        self.assertIn('"ogrenext"', self.main)
        self.assertIn("FolderExists(shader_media_root)", self.main)
        self.assertIn("FolderExists(presentation_media_root)", self.main)

    def test_shutdown_is_proven_before_any_owner_is_released(self) -> None:
        close_start = self.main.index(
            "CloseCombinedRendererSession("
        )
        close_end = self.main.index("#endif", close_start)
        close_helper = self.main[close_start:close_end]
        self.assertIn(
            "RendererInProcessSessionStatus::FAILED_FRONTEND_SHUTDOWN",
            close_helper,
        )
        self.assertIn("Render::RenderOperationCode::TIMEOUT", close_helper)
        self.assertIn("session.Shutdown()", close_helper)
        self.assertIn(
            "kCombinedRendererShutdownAttemptNanoseconds =\n"
            "    500'000'000ULL;",
            self.main,
        )
        self.assertIn(
            "combined_session_config.shutdown_timeout_nanoseconds =\n"
            "            kCombinedRendererShutdownAttemptNanoseconds;",
            self.main,
        )
        normal = self.main[
            self.main.index("[RoR|RendererCombined|Shutdown]") :
            self.main.index("App::ShutdownWorldModelCapture();", self.main.index("[RoR|RendererCombined|Shutdown]"))
        ]
        self.assertIn("RendererInProcessSessionStatus::CLOSED", normal)
        self.assertIn("FailStopApplication(EXIT_FAILURE)", normal)
        self.assertLess(
            normal.index("RendererInProcessSessionStatus::CLOSED"),
            normal.index("renderer_combined_session.reset()"),
        )
        fatal = self.main[self.main.index("RunApplicationFatalShutdownSequence(") :]
        self.assertRegex(
            fatal,
            r"renderer_shutdown\.status !=\s*"
            r"RendererInProcessSessionStatus::CLOSED[\s\S]*?return false;",
        )

    def test_legacy_loading_and_sdl_drains_are_compile_gated(self) -> None:
        self.assertIn(
            "void*  GetCombinedRendererResourceWindow() const noexcept;",
            self.context_h,
        )
        process = self.context[
            self.context.index("void AppContext::ProcessWindowEvents()") :
            self.context.index("void AppContext::RegisterRTShaderSceneManager")
        ]
        self.assertRegex(
            process,
            r"#if defined\(ROR_OGRE_NEXT_COMBINED_RUNTIME\)[\s\S]*?"
            r"return;[\s\S]*?#elif OGRE_VERSION_MAJOR",
        )
        loading_start = self.loading.index("void LoadingWindow::SetProgress")
        loading = self.loading[loading_start:]
        gate = loading.index("render_frame = false;")
        legacy_present = loading.index(
            "Ogre::Root::getSingleton().renderOneFrame();"
        )
        self.assertLess(gate, legacy_present)
        self.assertIn("#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)", loading)

    def test_visible_metrics_and_mouse_state_precede_direct_callbacks(self) -> None:
        poll_start = self.presenter.index("ValidationResult PollOrderedSdl(")
        poll_end = self.presenter.index("ValidationResult Poll(", poll_start)
        poll = self.presenter[poll_start:poll_end]
        pump = poll.index("SDL_PumpEvents();")
        metrics = poll.index("target->DisplayMetricsChanged(game_metrics)")
        drain = poll.index("while (SDL_PollEvent(&event) != 0)")
        callback = poll.index("target->MouseMoved(")
        reconcile = poll.index("target->Reconcile(state)")
        self.assertLess(pump, metrics)
        self.assertLess(metrics, drain)
        self.assertLess(drain, callback)
        self.assertLess(callback, reconcile)
        self.assertIn("game_metrics.pixel_width", poll)
        self.assertIn("game_metrics.logical_width", poll)

        metric_injection = self.context[
            self.context.index("InjectRendererInputDisplayMetrics(") :
            self.context.index("void AppContext::InjectRendererInputKey")
        ]
        self.assertIn("ResolveRenderDisplayMetrics(", metric_injection)
        self.assertIn("m_display_metrics = next", metric_injection)

        stage = self.input_engine[
            self.input_engine.index("StageRendererInputMouseMotion(") :
            self.input_engine.index("bool InputEngine::ApplyRendererInput(")
        ]
        self.assertIn("Detail::StageRendererGameMouseMotion", stage)
        self.assertIn("Detail::StageRendererGameMouseButton", stage)
        self.assertIn("Detail::StageRendererGameMouseWheel", stage)
        self.assertIn("RendererGameLogicalCoordinate(", stage)
        self.assertIn("callback_state = mouseState", stage)
        reconcile = self.input_engine[
            self.input_engine.index("bool InputEngine::ApplyRendererInput(") :
            self.input_engine.index("void InputEngine::ProcessKeyPress")
        ]
        self.assertIn("m_renderer_display_metrics_active", reconcile)
        self.assertIn("RendererGameLogicalCoordinate(", reconcile)

    def test_renderer_focus_loss_uses_the_native_imgui_reset(self) -> None:
        reset_start = self.context.index(
            "void AppContext::ResetInputStateForFocusTransition()"
        )
        reset_end = self.context.index(
            "void AppContext::windowFocusChange", reset_start
        )
        reset = self.context[reset_start:reset_end]
        self.assertIn("ResetAllMouseButtons()", reset)
        self.assertIn("io.KeysDown", reset)
        self.assertIn("io.KeyCtrl = false", reset)
        focus_start = self.context.index(
            "void AppContext::InjectRendererInputFocus"
        )
        focus_end = self.context.index(
            "void AppContext::InjectRendererInputWindowClose", focus_start
        )
        self.assertIn(
            "this->ResetInputStateForFocusTransition()",
            self.context[focus_start:focus_end],
        )


if __name__ == "__main__":
    unittest.main()
