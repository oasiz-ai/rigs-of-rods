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
        cls.main_cmake = (ROOT / "source/main/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.application_mode_header = (
            ROOT
            / "source/main/system/RendererCombinedApplicationMode.h"
        ).read_text(encoding="utf-8")
        cls.showcase_header = (
            ROOT
            / "source/main/gfx/render/NativeVisualShowcaseSceneSource.h"
        ).read_text(encoding="utf-8")

    def test_finder_launch_is_exact_cityworld_alexis_demo(self) -> None:
        start = self.main.index(
            "// The menu/HUD is transported as a GUI-RTT texture asset"
        )
        end = self.main.index("#else", start)
        block = self.main[start:end]
        self.assertIn(
            "if (!renderer_combined_native_visual_showcase && argc == 1)",
            block,
        )
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
        self.assertEqual(block.count("renderer_combined_demo_arguments ="), 1)

    def test_hud_overlay_capture_rides_the_joined_scene_boundary(self) -> None:
        # The transported menu/HUD is read back between UpdateScene (which
        # builds the complete DearIMGUI frame) and the joined-scene post, so
        # the readback rides the exact same joined boundary.
        update = self.main.index(
            "App::GetGfxScene()->UpdateScene(dt_sim); // Draws GUI as well"
        )
        capture = self.main.index(
            "renderer_combined_hud_capture->CaptureIfDirty();", update
        )
        post = self.main.index(
            "renderer_combined_session->PostUpdatedScene(", capture
        )
        self.assertLess(update, capture)
        self.assertLess(capture, post)
        self.assertIn(
            "renderer_combined_hud_capture =\n"
            "                    std::make_unique<Ogre14GuiOverlayCapture>();",
            self.main,
        )
        self.assertIn("gfx/Ogre14GuiOverlayCapture.{h,cpp}", self.main_cmake)

    def test_native_showcase_options_are_private_and_precede_normal_cli(self) -> None:
        resolve = self.main.index(
            "ResolveRendererCombinedApplicationArguments(argc, argv)"
        )
        adopt_argc = self.main.index(
            "argc = renderer_combined_arguments.argc();", resolve
        )
        adopt_argv = self.main.index(
            "argv = renderer_combined_arguments.argv();", adopt_argc
        )
        normal_cli = self.main.index(
            "App::GetConsole()->processCommandLine(argc, argv);"
        )
        self.assertLess(resolve, adopt_argc)
        self.assertLess(adopt_argc, adopt_argv)
        self.assertLess(adopt_argv, normal_cli)
        for option in (
            '"--native-visual-showcase"',
            '"--native-visual-showcase-a0"',
        ):
            with self.subTest(option=option):
                self.assertIn(option, self.application_mode_header)
                self.assertNotIn(option, self.main[normal_cli:])
        for scene in ("A0_LIGHTING_COUPON", "A1_NATIVE_COURSE"):
            with self.subTest(scene=scene):
                self.assertIn(scene, self.application_mode_header)

    def test_showcase_owns_renderer_neutral_source_and_posts_in_main_menu(self) -> None:
        self.assertIn(
            "std::unique_ptr<Render::IJoinedGraphicsSceneSource>\n"
            "        renderer_combined_scene_source;",
            self.main,
        )
        load = self.main.index("LoadNativeVisualShowcaseSceneSource(")
        assign = self.main.index(
            "renderer_combined_scene_source = std::move(loaded.source);",
            load,
        )
        loop = self.main.index("while (App::app_state", assign)
        showcase_branch = self.main.index(
            "renderer_combined_native_visual_showcase ||", loop
        )
        post = self.main.index("PostUpdatedScene(", showcase_branch)
        skip = self.main.index("SkipUpdatedScene();", post)
        self.assertLess(load, assign)
        self.assertLess(showcase_branch, post)
        self.assertLess(post, skip)
        self.assertIn(
            "if (!renderer_combined_native_visual_showcase)",
            self.main[showcase_branch:post],
        )

    def test_combined_sources_select_explicit_production_lighting(self) -> None:
        configure = self.main.index(
            "presenter_config.lighting_mode ="
        )
        prepare = self.main.index("PrepareWindow(presenter_config)", configure)
        selection = self.main.index(
            "if (renderer_combined_native_visual_showcase)", prepare
        )
        ordinary = self.main.index("else", selection)
        self.assertLess(configure, prepare)
        self.assertLess(prepare, selection)
        self.assertLess(selection, ordinary)
        selection_block = self.main[configure:prepare]
        self.assertIn("renderer_combined_native_visual_showcase", selection_block)
        self.assertIn("METAL_RT_SUN_VISIBILITY_V2", selection_block)
        self.assertIn("RASTER_HDR_PSSM", selection_block)
        showcase_log_start = self.main.index(
            "[RoR|RendererCombined|NativeShowcase] Selected exact"
        )
        showcase_log_end = self.main.index("else", showcase_log_start)
        showcase_log = self.main[showcase_log_start:showcase_log_end]
        self.assertIn("rt4_pbr_hdr_metal_sun_visibility_v2", showcase_log)
        self.assertIn("rt4_pbr_pssm_hdr_preview", showcase_log)
        self.assertIn("hdr=true", showcase_log)
        self.assertNotIn("hdr=false", showcase_log)
        self.assertIn("native_rt={}", showcase_log)

        frontend_configuration = self.presenter[
            self.presenter.index("OgreNextN1Configuration frontend_configuration;") :
            self.presenter.index("if (!CopyParameters(")
        ]
        self.assertIn(
            "OgreNextDirectionalShadowMode::DISABLED",
            frontend_configuration,
        )
        self.assertIn(
            "OgreNextDirectionalShadowMode::PSSM_3_CASCADE_V1",
            frontend_configuration,
        )
        self.assertIn(
            "frontend_configuration.enable_hdr_compositor = true;",
            frontend_configuration,
        )
        self.assertIn(
            "OgreNextHdrSceneTopology::DIRECTIONAL_SPLIT_V2",
            frontend_configuration,
        )
        self.assertIn(
            "OgreNextHdrSceneTopology::SINGLE_EVALUATION_PSSM_V1",
            frontend_configuration,
        )
        self.assertIn(
            "frontend_configuration.presentation.gpu_only_output = true;",
            frontend_configuration,
        )

    def test_metal_v2_presents_only_after_external_lighting_completion(self) -> None:
        render_start = self.presenter.index(
            "RenderOperationResult Render(const RenderFrameRequest &request,"
        )
        render_end = self.presenter.index(
            "RenderOperationResult\n  RetireFrameState", render_start
        )
        render = self.presenter[render_start:render_end]
        prepare = render.index("RenderFrameRequest raster_request = request;")
        defer = render.index("raster_request.present = false;", prepare)
        raster = render.index("frontend_->Render(raster_request, output)", defer)
        external = render.index("backend_.RenderSunVisibilityV2", raster)
        contract = render.index(
            "ValidateNativeSunVisibilityV2FrameContract", external
        )
        publish = render.index("output.presented = true;", contract)
        self.assertLess(prepare, defer)
        self.assertLess(defer, raster)
        self.assertLess(raster, external)
        self.assertLess(external, contract)
        self.assertLess(contract, publish)
        self.assertIn(
            "output.presented_view_id = request.presentation_view_id;",
            render[publish:],
        )
        self.assertNotIn("frontend_->Render(request, output)", render)

    def test_both_showcases_enable_profile_specific_audited_turntable_motion(self) -> None:
        load = self.main.index("LoadNativeVisualShowcaseSceneSource(")
        select = self.main.index(
            "Render::NativeVisualShowcaseMotionMode::TURN_TABLE", load
        )
        move = self.main.index(
            "renderer_combined_scene_source = std::move(loaded.source);",
            select,
        )
        post = self.main.index("PostUpdatedScene(", move)
        audit = self.main.index(
            '"NativeShowcase|Turntable] "', post
        )
        self.assertLess(load, select)
        self.assertLess(select, move)
        self.assertLess(move, post)
        self.assertLess(post, audit)
        audit_block = self.main[audit - 1800 : audit + 2800]
        for token in (
            'mode=\'{}\'',
            '"turntable_thin_glass_slab"',
            '"turntable_opaque_gate"',
            "frontend_frame_id",
            "scene_snapshot_id",
            "committed_simulation_tick()",
            "committed_turntable_angle_degrees()",
            "committed_gate_transform_revision()",
            "fixed_hz=60",
            "revolution_ticks={}",
            "opaque_motion_only={}",
            '"thin_parallel_slab_screen_space"',
            "motion_vectors=false",
        ):
            with self.subTest(token=token):
                self.assertIn(token, audit_block)
        self.assertIn("committed_tick / 90U", audit_block)
        self.assertIn(
            "kNativeVisualShowcaseTurntableTicksPerRevolution",
            audit_block,
        )
        profile_start = self.main.rindex(
            "const bool selects_a0", 0, load
        )
        profile_selection = self.main[profile_start:move]
        self.assertIn(
            "NativeVisualShowcaseProfile::A1_NATIVE_COURSE",
            profile_selection,
        )
        self.assertIn(
            "kNativeVisualShowcaseA1ExecutableResourceRelativePath",
            profile_selection,
        )
        self.assertEqual(
            profile_selection.count(
                "NativeVisualShowcaseMotionMode::TURN_TABLE"
            ),
            1,
        )
        self.assertIn(
            'selects_a0 ? "turntable_opaque_gate"',
            self.main[move:audit],
        )
        self.assertIn(
            ': "turntable_thin_glass_slab"',
            self.main[move:audit],
        )

    def test_lighting_receipt_maps_topology_and_zero_legacy_readbacks(self) -> None:
        audit_start = self.presenter.index(
            "RendererNativeLightingAudit\n  NativeLightingAudit() const noexcept"
        )
        audit_end = self.presenter.index(
            "static constexpr std::size_t kMaximumAxes", audit_start
        )
        audit = self.presenter[audit_start:audit_end]
        for mapping in (
            "output.production_content_readbacks =\n"
            "        audit.production_content_readbacks;",
            "output.production_framebuffer_readbacks =\n"
            "        audit.production_framebuffer_readbacks;",
            "output.ogre14_lighting_passes = audit.ogre14_lighting_passes;",
            "output.transmission_items = audit.last_transmission_items;",
            "output.thin_parallel_slab_refraction =\n"
            "        audit.thin_parallel_slab_refraction;",
            "output.physical_snell_refraction = audit.physical_snell_refraction;",
            "output.beer_lambert_attenuation = audit.beer_lambert_attenuation;",
            "output.screen_space_radiance_lookup =\n"
            "        audit.screen_space_radiance_lookup;",
            "output.refraction_scene_evaluations =\n"
            "        audit.refraction_scene_evaluations;",
            "output.hdr_scene_topology =\n"
            "        static_cast<std::uint32_t>(audit.hdr_scene_topology);",
            "output.pssm_finalized_with_populated_scene =\n"
            "        audit.pssm_finalized_with_populated_scene;",
            "output.raster_scene_evaluations = audit.raster_scene_evaluations;",
            "output.reflection_successful_capture_count =\n"
            "        reflection.successful_capture_count;",
            "output.reflection_probe_resolution = reflection.last_probe_resolution;",
            "output.reflection_blend_resolution = reflection.blend_resolution;",
            "output.reflection_native_execution_evidence =\n"
            "        reflection.native_execution_evidence;",
            "output.reflection_pcc_enabled = reflection.pcc_enabled;",
            "output.reflection_pbs_bound = reflection.pbs_bound;",
            "output.reflection_blend_texture_ready = reflection.blend_texture_ready;",
            "output.reflection_ui_free_capture = reflection.ui_free_capture;",
            "output.production_gpu_only = audit.production_gpu_only;",
            "output.no_ogre14_lighting = audit.no_ogre14_lighting;",
        ):
            with self.subTest(mapping=mapping):
                self.assertIn(mapping, audit)

        lighting_log_start = self.main.index(
            '"schema_version={} available={} "'
        )
        lighting_log_end = self.main.index(
            "if (lighting_audit_signature !=", lighting_log_start
        )
        lighting_log = self.main[lighting_log_start:lighting_log_end]
        for field in (
            "hdr_topology={}",
            "pssm_populated_finalize={}",
            "scene_evaluations={}",
            "reflection_captures={}",
            "reflection_probe_resolution={}",
            "reflection_blend_resolution={}",
            "reflection_native_evidence={}",
            "reflection_pcc={}",
            "reflection_pbs_bound={}",
            "reflection_blend_ready={}",
            "reflection_ui_free={}",
            "production_content_readbacks={}",
            "production_framebuffer_readbacks={}",
            "ogre14_lighting_passes={}",
            "no_ogre14_lighting={}",
        ):
            with self.subTest(field=field):
                self.assertIn(field, lighting_log)

    def test_showcase_packages_are_exact_and_staged_beside_executable_resources(self) -> None:
        packages = (
            (
                "99df00d857a8139f3d13c89be3af29d28ea0372f03fac327e4598b74daaf7a8e",
                "a0_road_tile_12m/rorng_a0_road_tile_12m.rornative",
            ),
            (
                "6399101c63ca8d5eff25ab499db215c45d89a4ce91cba08145692d025401505d",
                "a1_native_course_60m/rorng_a1_native_course_60m.rornative",
            ),
        )
        for expected_sha, relative_path in packages:
            with self.subTest(relative_path=relative_path):
                self.assertIn(expected_sha, self.main_cmake)
                self.assertIn(expected_sha, self.showcase_header)
                directory, filename = relative_path.split("/", 1)
                self.assertIn(directory, self.main_cmake)
                self.assertIn(filename, self.main_cmake)
        self.assertIn(
            "add_custom_target(ror_native_visual_showcase_package",
            self.main_cmake,
        )
        for token in (
            "kNativeVisualShowcaseExecutableResourceRelativePath",
            "kNativeVisualShowcaseA1ExecutableResourceRelativePath",
            "LoadNativeVisualShowcaseSceneSource(\n"
            "                    native_showcase_package_path,\n"
            "                    native_profile)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, self.main)
        package_path = self.main.index(
            "const std::string native_showcase_package_path = PathCombine("
        )
        self.assertIn(
            "App::sys_resources_dir->getStr()",
            self.main[package_path : package_path + 240],
        )

    def test_combined_entrypoint_has_no_bridge_child_or_transport_runtime(self) -> None:
        include_start = self.main.index(
            "#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)\n"
            '#include "RendererCombinedApplicationMode.h"'
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

    def test_scene_free_bootstrap_is_shown_before_loading_and_never_uses_ogre14(self) -> None:
        start = self.main.index(
            "renderer_combined_session->Start(combined_session_config)"
        )
        bootstrap = self.main.index(
            "renderer_combined_session->PresentBootstrapFrame()", start
        )
        install = self.main.index(
            "SetCombinedRendererLoadingPump(", bootstrap
        )
        loop = self.main.index("while (App::app_state", install)
        self.assertLess(start, bootstrap)
        self.assertLess(bootstrap, install)
        self.assertLess(install, loop)
        self.assertIn(
            "RendererInProcessSessionStatus::BOOTSTRAP_PRESENTED",
            self.main[bootstrap:install],
        )

        callback = self.main[
            self.main.index("bool PumpCombinedRendererLoadingWindow(") :
            self.main.index("#endif", self.main.index("bool PumpCombinedRendererLoadingWindow("))
        ]
        for token in (
            "session.asset_sequence() != 0U",
            "session.last_consumed_scene_snapshot_id() != 0U",
            "session.last_frontend_frame_id() != 0U",
            "session.PresentBootstrapFrame()",
            "PENDING_FRONTEND_SURFACE",
            "SHUTDOWN_REQUESTED",
        ):
            with self.subTest(token=token):
                self.assertIn(token, callback)
        self.assertNotIn("renderOneFrame", callback)
        self.assertNotIn("Ogre::Root", callback)

        loading_start = self.loading.index("void LoadingWindow::SetProgress")
        loading_end = self.loading.index(
            "void LoadingWindow::SetProgressNetConnect", loading_start
        )
        loading = self.loading[loading_start:loading_end]
        self.assertIn("render_frame = false;", loading)
        self.assertIn("g_combined_renderer_pump(", loading)
        self.assertIn(
            "std::this_thread::get_id() == g_combined_renderer_pump_thread",
            loading,
        )
        self.assertIn("std::chrono::milliseconds(16)", loading)
        self.assertLess(
            loading.index("render_frame = false;"),
            loading.index("g_combined_renderer_pump("),
        )
        self.assertNotIn(
            "Ogre::Root::getSingleton().renderOneFrame();",
            loading[
                loading.index("#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)") :
                loading.index("#endif", loading.index("#if defined(ROR_OGRE_NEXT_COMBINED_RUNTIME)"))
            ],
        )

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
