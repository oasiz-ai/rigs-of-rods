if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(RoRPhysicsTests LANGUAGES CXX)
    enable_testing()
    find_package(OpenSSL REQUIRED)
    find_package(RapidJSON QUIET)
endif ()

get_filename_component(
    ROR_REPOSITORY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)

option(
    ROR_TEST_GAME_FAST_MATH
    "Exercise the physics kernel with the game's release floating-point mode"
    OFF
)

find_package(Threads REQUIRED)

set(ROR_WORLDMODEL_SOURCES
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/EpisodeArtifacts.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/EpisodeCaptureSink.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/EpisodeFormat.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/EpisodeProvenance.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/EpisodeValidator.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/EpisodeWriter.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/ObservationScheduler.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/WorldModelCaptureContract.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/WorldModelCaptureEncoding.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/WorldModelLiveCaptureConfig.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/WorldModelPlatformIdentity.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/WorldModelCaptureSession.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/WorldModelRuntimeBackend.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/worldmodel/WorldModelTelemetry.cpp"
)

add_library(ror_worldmodel_core STATIC ${ROR_WORLDMODEL_SOURCES})
target_include_directories(
    ror_worldmodel_core
    PUBLIC
        "${ROR_REPOSITORY_ROOT}/source/main/worldmodel"
        "${ROR_REPOSITORY_ROOT}/source/main/physics"
)

add_executable(
    ror_worldmodel_observation_scheduler_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/ObservationSchedulerTests.cpp"
)
add_executable(
    ror_worldmodel_capture_contract_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/WorldModelCaptureContractTests.cpp"
)
add_executable(
    ror_worldmodel_capture_session_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/WorldModelCaptureSessionTests.cpp"
)
add_executable(
    ror_worldmodel_episode_provenance_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/EpisodeProvenanceTests.cpp"
)
add_executable(
    ror_worldmodel_telemetry_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/WorldModelTelemetryTests.cpp"
)
add_executable(
    ror_worldmodel_runtime_backend_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/WorldModelRuntimeBackendTests.cpp"
)
add_executable(
    ror_worldmodel_episode_io_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/EpisodeIoTests.cpp"
)
add_executable(
    ror_worldmodel_episode_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/WorldModelEpisodeTests.cpp"
)
add_executable(
    ror_worldmodel_live_capture_config_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/WorldModelLiveCaptureConfigTests.cpp"
)
add_executable(
    ror_worldmodel_platform_identity_tests
    "${CMAKE_CURRENT_LIST_DIR}/worldmodel/WorldModelPlatformIdentityTests.cpp"
)
add_executable(
    ror_worldmodel_episode
    "${ROR_REPOSITORY_ROOT}/tools/ror_worldmodel_episode.cpp"
)

set(ROR_WORLDMODEL_TARGETS
    ror_worldmodel_core
    ror_worldmodel_observation_scheduler_tests
    ror_worldmodel_capture_contract_tests
    ror_worldmodel_capture_session_tests
    ror_worldmodel_episode_provenance_tests
    ror_worldmodel_runtime_backend_tests
    ror_worldmodel_telemetry_tests
    ror_worldmodel_episode_io_tests
    ror_worldmodel_episode_tests
    ror_worldmodel_live_capture_config_tests
    ror_worldmodel_platform_identity_tests
    ror_worldmodel_episode
)

foreach (worldmodel_target IN LISTS ROR_WORLDMODEL_TARGETS)
    set_target_properties(
        ${worldmodel_target}
        PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO
    )
    if (MSVC)
        target_compile_options(
            ${worldmodel_target}
            PRIVATE /W4 /WX /fp:strict
        )
    else ()
        target_compile_options(
            ${worldmodel_target}
            PRIVATE
                -Wall -Wextra -Werror -pedantic
                -fno-fast-math -ffp-contract=off
        )
    endif ()
endforeach ()

foreach (worldmodel_test IN ITEMS
    ror_worldmodel_observation_scheduler_tests
    ror_worldmodel_capture_contract_tests
    ror_worldmodel_capture_session_tests
    ror_worldmodel_episode_provenance_tests
    ror_worldmodel_runtime_backend_tests
    ror_worldmodel_telemetry_tests
    ror_worldmodel_episode_io_tests
    ror_worldmodel_episode_tests
    ror_worldmodel_live_capture_config_tests
    ror_worldmodel_platform_identity_tests
    ror_worldmodel_episode
)
    target_link_libraries(${worldmodel_test} PRIVATE ror_worldmodel_core)
endforeach ()

add_executable(
    ror_render_display_metrics_tests
    "${CMAKE_CURRENT_LIST_DIR}/platform/RenderDisplayMetricsTests.cpp"
)

add_executable(
    ror_sky_mode_availability_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/SkyModeAvailabilityTests.cpp"
)

add_executable(
    ror_v0_post_process_math_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/PostProcessMathTests.cpp"
)

add_executable(
    ror_post_process_policy_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/PostProcessPolicyTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/PostProcessPolicy.cpp"
)

add_executable(
    ror_post_process_runtime_contract_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/PostProcessRuntimeContractTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/PostProcessPolicy.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/PostProcessRuntimeContract.cpp"
)

add_executable(
    ror_renderer_backend_policy_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/RendererBackendPolicyTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
)
target_include_directories(
    ror_renderer_backend_policy_tests
    PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx"
)

set(ROR_RENDER_CONTRACT_SOURCES
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/GraphicsSceneSnapshotProducer.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/HdrReference.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/MaterialDescriptor.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/RenderAssetId.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/RenderAssetRegistry.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/RenderFrame.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/RenderResourceDescriptors.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/RendererFrontend.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ResourceHandle.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/SceneSnapshot.cpp"
)

add_library(ror_render_contract STATIC ${ROR_RENDER_CONTRACT_SOURCES})
target_include_directories(
    ror_render_contract
    PUBLIC "${ROR_REPOSITORY_ROOT}/source/main/gfx/render"
)

add_executable(
    ror_graphics_scene_snapshot_producer_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/GraphicsSceneSnapshotProducerTests.cpp"
)
add_executable(
    ror_render_resource_handle_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/ResourceHandleTests.cpp"
)
add_executable(
    ror_render_material_descriptor_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/MaterialDescriptorTests.cpp"
)
add_executable(
    ror_render_hdr_reference_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/HdrReferenceTests.cpp"
)
add_executable(
    ror_render_asset_registry_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/RenderAssetRegistryTests.cpp"
)
add_executable(
    ror_render_scene_snapshot_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/SceneSnapshotTests.cpp"
)
add_executable(
    ror_render_resource_descriptor_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/RenderResourceDescriptorTests.cpp"
)
add_executable(
    ror_renderer_frontend_contract_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/RendererFrontendContractTests.cpp"
)
add_executable(
    ror_renderer_boundary_header_isolation_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/RendererBoundaryHeaderIsolationTests.cpp"
)
add_executable(
    ror_ogre_next_n1_policy_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/OgreNextN1PolicyTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ogrenext/OgreNextN1Policy.cpp"
)
add_executable(
    ror_ogre_next_n2_interop_state_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/OgreNextN2InteropStateTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ogrenext/OgreNextN2InteropState.cpp"
)
add_executable(
    ror_ogre_next_vulkan_rt5_contract_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/OgreNextVulkanRt5ContractTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ogrenext/OgreNextVulkanRt5Contract.cpp"
)
add_executable(
    ror_ogre_next_vulkan_rt6_contract_tests
    "${CMAKE_CURRENT_LIST_DIR}/gfx/render/OgreNextVulkanRt6ContractTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ogrenext/OgreNextVulkanRt6Contract.cpp"
)
target_include_directories(
    ror_ogre_next_n1_policy_tests
    PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ogrenext"
)
target_include_directories(
    ror_ogre_next_n2_interop_state_tests
    PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ogrenext"
)
target_include_directories(
    ror_ogre_next_vulkan_rt5_contract_tests
    PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ogrenext"
)
target_include_directories(
    ror_ogre_next_vulkan_rt6_contract_tests
    PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/ogrenext"
)
target_include_directories(
    ror_renderer_boundary_header_isolation_tests
    PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx/render"
)

set(ROR_RENDER_CONTRACT_TEST_TARGETS
    ror_graphics_scene_snapshot_producer_tests
    ror_render_hdr_reference_tests
    ror_render_resource_handle_tests
    ror_render_material_descriptor_tests
    ror_render_asset_registry_tests
    ror_render_scene_snapshot_tests
    ror_render_resource_descriptor_tests
    ror_renderer_frontend_contract_tests
    ror_ogre_next_n1_policy_tests
    ror_ogre_next_n2_interop_state_tests
    ror_ogre_next_vulkan_rt5_contract_tests
    ror_ogre_next_vulkan_rt6_contract_tests
)
foreach (render_contract_test IN LISTS ROR_RENDER_CONTRACT_TEST_TARGETS)
    target_link_libraries(${render_contract_test} PRIVATE ror_render_contract)
endforeach ()

set(ROR_RENDER_CONTRACT_BUILD_TARGETS
    ror_render_contract
    ${ROR_RENDER_CONTRACT_TEST_TARGETS}
    ror_renderer_boundary_header_isolation_tests
)
foreach (render_contract_target IN LISTS ROR_RENDER_CONTRACT_BUILD_TARGETS)
    set_target_properties(
        ${render_contract_target}
        PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO
    )
    if (MSVC)
        target_compile_options(
            ${render_contract_target}
            PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /fp:strict
        )
    else ()
        target_compile_options(
            ${render_contract_target}
            PRIVATE
                -Wall -Wextra -Werror -pedantic
                -fno-fast-math -ffp-contract=off
        )
    endif ()
endforeach ()

# This test intentionally replaces global new/delete with one malloc/free pair
# so it can count allocations. GCC 11's interprocedural analysis diagnoses the
# replacement delete at unrelated allocator call sites even though the source
# defines the paired replacement new. Keep every other warning fatal.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(
        ror_graphics_scene_snapshot_producer_tests
        PRIVATE -Wno-mismatched-new-delete
    )
endif ()

add_executable(
    ror_local_light_budget_tests
    "${CMAKE_CURRENT_LIST_DIR}/terrain/LocalLightBudgetTests.cpp"
)

add_executable(
    ror_command_line_info_tests
    "${CMAKE_CURRENT_LIST_DIR}/system/CommandLineInfoTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/system/CommandLineInfo.cpp"
)
target_include_directories(
    ror_command_line_info_tests
    PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/system"
)
if (MSVC)
    target_compile_definitions(
        ror_command_line_info_tests
        PRIVATE _CRT_SECURE_NO_WARNINGS
    )
endif ()

add_executable(
    ror_beam_axial_response_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/BeamAxialResponseTests.cpp"
)

add_executable(
    ror_beam_rest_length_scale_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/BeamRestLengthScaleTests.cpp"
)

add_executable(
    ror_deterministic_counter_noise_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicCounterNoiseTests.cpp"
)

add_executable(
    ror_deterministic_fixed_step_cadence_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicFixedStepCadenceTests.cpp"
)

add_executable(
    ror_fixed_step_capture_bridge_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/FixedStepCaptureBridgeTests.cpp"
)

add_executable(
    ror_deterministic_scenario_schedule_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicScenarioScheduleTests.cpp"
)

add_executable(
    ror_deterministic_contact_order_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicContactOrderTests.cpp"
)

add_executable(
    ror_deterministic_state_digest_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicStateDigestTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicStateDigest.cpp"
)

add_executable(
    ror_deterministic_state_trace_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicStateTraceTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicStateDigest.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicStateTrace.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicStateTraceCli.cpp"
)

add_executable(
    ror_deterministic_input_trace_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicInputTraceTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicInputTrace.cpp"
)

add_executable(
    ror_deterministic_input_trace_runtime_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicInputTraceRuntimeTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicInputTrace.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicInputTraceRuntime.cpp"
)

add_executable(
    ror_deterministic_vehicle_input_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicVehicleInputTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicInputTrace.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicInputTraceRuntime.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicVehicleInput.cpp"
)

# The production build deliberately excludes authenticated input-trace
# translation units from global fast-math. Mirror that boundary here while
# leaving each test driver and the actual physics kernels in the requested
# game floating-point mode.
set(ROR_DETERMINISTIC_INPUT_SOURCES
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicInputTrace.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicInputTraceRuntime.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicVehicleInput.cpp"
)
if (MSVC)
    set_source_files_properties(
        ${ROR_DETERMINISTIC_INPUT_SOURCES}
        PROPERTIES COMPILE_OPTIONS "/fp:precise"
    )
else ()
    set_source_files_properties(
        ${ROR_DETERMINISTIC_INPUT_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-fno-fast-math"
    )
endif ()

add_executable(
    ror_state_trace
    "${ROR_REPOSITORY_ROOT}/tools/ror_state_trace.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicStateDigest.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicStateTrace.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/physics/DeterministicStateTraceCli.cpp"
)

add_executable(
    ror_calibrated_beam_material_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/CalibratedBeamMaterialTests.cpp"
)

add_executable(
    ror_calibrated_beam_material_adapter_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/CalibratedBeamMaterialAdapterTests.cpp"
)

add_executable(
    ror_calibrated_beam_step_sensitivity_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/CalibratedBeamStepSensitivityTests.cpp"
)

add_executable(
    ror_calibrated_beam_mesh_refinement_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/CalibratedBeamMeshRefinementTests.cpp"
)

add_executable(
    ror_calibrated_beam_savegame_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/CalibratedBeamSavegameTests.cpp"
)

if (RapidJSON_INCLUDE_DIRS)
    add_executable(
        ror_calibrated_beam_savegame_json_tests
        "${CMAKE_CURRENT_LIST_DIR}/physics/CalibratedBeamSavegameJsonTests.cpp"
    )
    target_include_directories(
        ror_calibrated_beam_savegame_json_tests
        SYSTEM PRIVATE ${RapidJSON_INCLUDE_DIRS}
    )
endif ()

add_executable(
    ror_rigdef_calibrated_beam_material_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/RigDefCalibratedBeamMaterialTests.cpp"
)

add_executable(
    ror_hydro_actuator_response_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/HydroActuatorResponseTests.cpp"
)

add_executable(
    ror_shader_compatibility_policy_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/ShaderCompatibilityPolicyTests.cpp"
)

add_executable(
    ror_legacy_material_script_sanitizer_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/LegacyMaterialScriptSanitizerTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/LegacyMaterialScriptSanitizer.cpp"
)
if (MSVC)
    # The pinned-fixture opt-in uses the standard getenv API. Keep this
    # upstream-only test portable under MSVC's /WX deprecation policy.
    target_compile_definitions(
        ror_legacy_material_script_sanitizer_tests
        PRIVATE _CRT_SECURE_NO_WARNINGS
    )
endif ()

add_executable(
    ror_legacy_material_compatibility_plan_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/LegacyMaterialCompatibilityPlanTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/LegacyMaterialCompatibilityPlan.cpp"
)

add_executable(
    ror_terrain_bundle_dependency_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/TerrainBundleDependencyTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/terrn2_fileformat/TerrainBundleDependency.cpp"
)

add_executable(
    ror_terrain_bundle_archive_verifier_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/TerrainBundleArchiveVerifierTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/terrn2_fileformat/TerrainBundleArchiveVerifier.cpp"
)
target_link_libraries(
    ror_terrain_bundle_archive_verifier_tests
    PRIVATE OpenSSL::Crypto
)

add_executable(
    ror_cityworld_neoq20_compatibility_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/CityWorldNeoQ20CompatibilityTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/tobj_fileformat/CityWorldNeoQ20Compatibility.cpp"
)
target_include_directories(
    ror_cityworld_neoq20_compatibility_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/tobj_fileformat"
)
target_link_libraries(
    ror_cityworld_neoq20_compatibility_tests
    PRIVATE OpenSSL::Crypto
)

add_executable(
    ror_cityworld_neoq_tree_compatibility_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/CityWorldNeoQTreeCompatibilityTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/tobj_fileformat/CityWorldNeoQ20Compatibility.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/tobj_fileformat/CityWorldNeoQTreeCompatibility.cpp"
)
target_include_directories(
    ror_cityworld_neoq_tree_compatibility_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/tobj_fileformat"
)
target_link_libraries(
    ror_cityworld_neoq_tree_compatibility_tests
    PRIVATE OpenSSL::Crypto
)

add_executable(
    ror_cityworld_penguin_road_compatibility_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/CityWorldPenguinRoadCompatibilityTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/tobj_fileformat/CityWorldNeoQ20Compatibility.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/tobj_fileformat/CityWorldPenguinRoadCompatibility.cpp"
)
target_include_directories(
    ror_cityworld_penguin_road_compatibility_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/tobj_fileformat"
)
target_link_libraries(
    ror_cityworld_penguin_road_compatibility_tests
    PRIVATE OpenSSL::Crypto
)

if (NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(
        ror_terrain_bundle_config_syntax_tests
        "${CMAKE_CURRENT_LIST_DIR}/resources/TerrainBundleConfigSyntaxTests.cpp"
    )
    target_include_directories(
        ror_terrain_bundle_config_syntax_tests
        SYSTEM PRIVATE ${OGRE_INCLUDE_DIRS}
    )
    if (TARGET OgreMain)
        target_link_libraries(
            ror_terrain_bundle_config_syntax_tests
            PRIVATE OgreMain
        )
    else ()
        target_link_libraries(
            ror_terrain_bundle_config_syntax_tests
            PRIVATE ${OGRE_LIBRARIES}
        )
    endif ()
endif ()

add_executable(
    ror_beamng_package_manifest_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/BeamNGPackageManifestTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/BeamNGPackageManifest.cpp"
)

add_executable(
    ror_beamng_material_inventory_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/BeamNGMaterialInventoryTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/BeamNGMaterialInventory.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamSyntax.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/BeamNGPackageManifest.cpp"
)

add_executable(
    ror_beamng_zip_archive_index_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/BeamNGZipArchiveIndexTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/BeamNGZipArchiveIndex.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/BeamNGPackageManifest.cpp"
)

add_executable(
    ror_beamng_zip_index
    "${ROR_REPOSITORY_ROOT}/tools/ror_beamng_zip_index.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/BeamNGZipArchiveIndex.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/BeamNGPackageManifest.cpp"
)

add_executable(
    ror_jbeam_expression_evaluator_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamExpressionEvaluatorTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamExpressionEvaluator.cpp"
)

add_executable(
    ror_jbeam_syntax_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamSyntaxTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamSyntax.cpp"
)

add_executable(
    ror_jbeam_coordinate_transform_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamCoordinateTransformTests.cpp"
)

add_executable(
    ror_jbeam_advanced_structure_ir_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamAdvancedStructureIRTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamAdvancedStructureIR.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamPartResolver.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamSyntax.cpp"
)

add_executable(
    ror_jbeam_part_resolver_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamPartResolverTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamPartResolver.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamSyntax.cpp"
)

add_executable(
    ror_jbeam_pressure_wheel_ir_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamPressureWheelIRTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamPressureWheelIR.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamPartResolver.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamSyntax.cpp"
)

add_executable(
    ror_jbeam_structural_ir_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamStructuralIRTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamExpressionEvaluator.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamStructuralIR.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamPartResolver.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamSyntax.cpp"
)

add_executable(
    ror_jbeam_to_rigdef_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamToRigDefTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamToRigDef.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamExpressionEvaluator.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamStructuralIR.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamPartResolver.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamSyntax.cpp"
)

target_include_directories(
    ror_beamng_package_manifest_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_beamng_material_inventory_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_beamng_zip_archive_index_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_beamng_zip_index
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_shader_compatibility_policy_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources"
)

target_include_directories(
    ror_legacy_material_compatibility_plan_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources"
)

target_include_directories(
    ror_legacy_material_script_sanitizer_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources"
)

target_include_directories(
    ror_terrain_bundle_dependency_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/terrn2_fileformat"
)

target_include_directories(
    ror_terrain_bundle_archive_verifier_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/terrn2_fileformat"
)

target_include_directories(
    ror_rigdef_calibrated_beam_material_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/rig_def_fileformat"
)

target_include_directories(
    ror_jbeam_expression_evaluator_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_jbeam_syntax_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_jbeam_coordinate_transform_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_jbeam_advanced_structure_ir_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_jbeam_part_resolver_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_jbeam_pressure_wheel_ir_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_jbeam_structural_ir_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_jbeam_to_rigdef_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

target_include_directories(
    ror_render_display_metrics_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/gfx"
)

target_include_directories(
    ror_sky_mode_availability_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/gfx"
)

target_include_directories(
    ror_v0_post_process_math_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/gfx"
)

target_include_directories(
    ror_post_process_policy_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/gfx"
)

target_include_directories(
    ror_post_process_runtime_contract_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/gfx"
)

target_include_directories(
    ror_local_light_budget_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/terrain"
)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    target_compile_definitions(
        ror_jbeam_to_rigdef_tests
        PRIVATE ROR_JBEAM_TO_RIGDEF_PREFLIGHT_ONLY
    )
else ()
    target_compile_definitions(
        ror_jbeam_to_rigdef_tests
        PRIVATE ROR_JBEAM_TO_RIGDEF_FULL_TEST
    )
    target_include_directories(
        ror_jbeam_to_rigdef_tests
        SYSTEM PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main"
            "${ROR_REPOSITORY_ROOT}/source/main/system"
            "${ROR_REPOSITORY_ROOT}/source/main/utils"
            "${ROR_REPOSITORY_ROOT}/source/main/utils/memory"
            "${ROR_REPOSITORY_ROOT}/source/main/resources/rig_def_fileformat"
            ${OGRE_INCLUDE_DIRS}
    )
    if (TARGET OgreMain)
        target_link_libraries(
            ror_jbeam_to_rigdef_tests
            PRIVATE OgreMain
        )
    else ()
        target_link_libraries(
            ror_jbeam_to_rigdef_tests
            PRIVATE ${OGRE_LIBRARIES}
        )
    endif ()
    target_link_libraries(
        ror_jbeam_to_rigdef_tests
        PRIVATE fmt::fmt
    )
    if (TARGET Angelscript::angelscript)
        target_link_libraries(
            ror_jbeam_to_rigdef_tests
            PRIVATE Angelscript::angelscript
        )
    endif ()
endif ()

if (APPLE)
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(
            CMAKE_OSX_DEPLOYMENT_TARGET
            "11.0"
            CACHE STRING
            "Minimum supported macOS version"
            FORCE
        )
    endif ()
    enable_language(OBJCXX)
    find_library(ROR_FOUNDATION_FRAMEWORK Foundation)
    find_library(ROR_METAL_FRAMEWORK Metal)
    if (NOT ROR_FOUNDATION_FRAMEWORK OR NOT ROR_METAL_FRAMEWORK)
        message(FATAL_ERROR
            "Metal RT probe requires the Foundation and Metal frameworks")
    endif ()

    add_executable(
        ror_metal_rt_probe
        "${CMAKE_CURRENT_LIST_DIR}/gfx/MetalRayTracingProbe.mm"
    )
    target_link_libraries(
        ror_metal_rt_probe
        PRIVATE
            "${ROR_FOUNDATION_FRAMEWORK}"
            "${ROR_METAL_FRAMEWORK}"
    )
    set_target_properties(
        ror_metal_rt_probe
        PROPERTIES
            OBJCXX_STANDARD 17
            OBJCXX_STANDARD_REQUIRED YES
            OBJCXX_EXTENSIONS NO
    )
    target_compile_options(
        ror_metal_rt_probe
        PRIVATE -fobjc-arc -Wall -Wextra -Werror -pedantic
    )

    add_executable(
        ror_apple_executable_path_tests
        "${CMAKE_CURRENT_LIST_DIR}/platform/AppleExecutablePathTests.cpp"
    )

    add_executable(
        ror_macos_user_directory_layout_tests
        "${CMAKE_CURRENT_LIST_DIR}/platform/MacOSUserDirectoryLayoutTests.cpp"
    )

    target_include_directories(
        ror_apple_executable_path_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/utils"
    )

    target_include_directories(
        ror_macos_user_directory_layout_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/utils"
    )

    if (ROR_OGRE14)
        add_executable(
            ror_macos_input_bridge_tests
            "${CMAKE_CURRENT_LIST_DIR}/platform/MacOSInputBridgeTests.cpp"
        )

        add_executable(
            ror_macos_controller_contract_tests
            "${CMAKE_CURRENT_LIST_DIR}/platform/MacOSControllerContractTests.cpp"
            "${ROR_REPOSITORY_ROOT}/source/main/utils/MacOSControllerBackend.cpp"
        )

        target_include_directories(
            ror_macos_input_bridge_tests
            PRIVATE
                "${ROR_REPOSITORY_ROOT}/source/main/utils"
        )

        target_include_directories(
            ror_macos_controller_contract_tests
            PRIVATE
                "${ROR_REPOSITORY_ROOT}/source/main/utils"
        )

        target_link_libraries(
            ror_macos_input_bridge_tests
            PRIVATE
                SDL2::SDL2
                ois::ois
        )

        target_link_libraries(
            ror_macos_controller_contract_tests
            PRIVATE SDL2::SDL2
        )
    endif ()

    if (ROR_USE_OPENAL)
        add_executable(
            ror_macos_openal_smoke_tests
            "${CMAKE_CURRENT_LIST_DIR}/platform/MacOSOpenALSmokeTests.cpp"
        )

        target_link_libraries(
            ror_macos_openal_smoke_tests
            PRIVATE OpenAL::OpenAL
        )
    endif ()
endif ()

target_link_libraries(
    ror_deterministic_counter_noise_tests
    PRIVATE Threads::Threads
)

set(ROR_PHYSICS_TEST_TARGETS
    ror_render_display_metrics_tests
    ror_sky_mode_availability_tests
    ror_v0_post_process_math_tests
    ror_post_process_policy_tests
    ror_post_process_runtime_contract_tests
    ror_renderer_backend_policy_tests
    ror_local_light_budget_tests
    ror_command_line_info_tests
    ror_beam_axial_response_tests
    ror_beam_rest_length_scale_tests
    ror_beamng_material_inventory_tests
    ror_beamng_package_manifest_tests
    ror_beamng_zip_archive_index_tests
    ror_beamng_zip_index
    ror_calibrated_beam_material_adapter_tests
    ror_calibrated_beam_material_tests
    ror_calibrated_beam_mesh_refinement_tests
    ror_calibrated_beam_step_sensitivity_tests
    ror_calibrated_beam_savegame_tests
    ror_rigdef_calibrated_beam_material_tests
    ror_deterministic_contact_order_tests
    ror_deterministic_counter_noise_tests
    ror_deterministic_fixed_step_cadence_tests
    ror_fixed_step_capture_bridge_tests
    ror_deterministic_scenario_schedule_tests
    ror_deterministic_input_trace_tests
    ror_deterministic_input_trace_runtime_tests
    ror_deterministic_vehicle_input_tests
    ror_deterministic_state_digest_tests
    ror_deterministic_state_trace_tests
    ror_state_trace
    ror_hydro_actuator_response_tests
    ror_legacy_material_compatibility_plan_tests
    ror_legacy_material_script_sanitizer_tests
    ror_shader_compatibility_policy_tests
    ror_terrain_bundle_archive_verifier_tests
    ror_terrain_bundle_dependency_tests
    ror_cityworld_neoq20_compatibility_tests
    ror_cityworld_neoq_tree_compatibility_tests
    ror_cityworld_penguin_road_compatibility_tests
    ror_jbeam_advanced_structure_ir_tests
    ror_jbeam_coordinate_transform_tests
    ror_jbeam_expression_evaluator_tests
    ror_jbeam_part_resolver_tests
    ror_jbeam_pressure_wheel_ir_tests
    ror_jbeam_structural_ir_tests
    ror_jbeam_syntax_tests
    ror_jbeam_to_rigdef_tests
)

if (TARGET ror_terrain_bundle_config_syntax_tests)
    list(APPEND ROR_PHYSICS_TEST_TARGETS
        ror_terrain_bundle_config_syntax_tests)
endif ()

if (TARGET ror_calibrated_beam_savegame_json_tests)
    list(APPEND ROR_PHYSICS_TEST_TARGETS
        ror_calibrated_beam_savegame_json_tests)
endif ()

if (APPLE)
    list(APPEND ROR_PHYSICS_TEST_TARGETS ror_apple_executable_path_tests)
    list(APPEND ROR_PHYSICS_TEST_TARGETS
        ror_macos_user_directory_layout_tests)
    if (ROR_OGRE14)
        list(APPEND ROR_PHYSICS_TEST_TARGETS
            ror_macos_controller_contract_tests
            ror_macos_input_bridge_tests
        )
    endif ()
    if (ROR_USE_OPENAL)
        list(APPEND ROR_PHYSICS_TEST_TARGETS
            ror_macos_openal_smoke_tests
        )
    endif ()
endif ()

foreach (test_target IN LISTS ROR_PHYSICS_TEST_TARGETS)
    target_include_directories(${test_target} PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/physics"
    )

    set_target_properties(${test_target} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    if (MSVC)
        target_compile_options(${test_target} PRIVATE /W4 /WX)
        if (ROR_TEST_GAME_FAST_MATH)
            target_compile_options(${test_target} PRIVATE /fp:fast)
        endif ()
    else ()
        target_compile_options(
            ${test_target}
            PRIVATE -Wall -Wextra -Werror -pedantic
        )
        if (ROR_TEST_GAME_FAST_MATH)
            target_compile_options(${test_target} PRIVATE -ffast-math)
        endif ()
    endif ()
endforeach ()

if (TARGET ror_terrain_bundle_config_syntax_tests)
    # OGRE 14's public vector headers use the relaxed constexpr constructor
    # rules introduced by C++14. Keep the pure physics oracles on C++11, but
    # compile this OGRE-linked integration test against its dependency's
    # minimum language contract.
    set_target_properties(
        ror_terrain_bundle_config_syntax_tests
        PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
endif ()

set_target_properties(
    ror_beamng_material_inventory_tests
    ror_jbeam_to_rigdef_tests
    PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_test(
    NAME render_display_metrics
    COMMAND ror_render_display_metrics_tests
)

add_test(
    NAME sky_mode_availability
    COMMAND ror_sky_mode_availability_tests
)

add_test(
    NAME v0_post_process_math
    COMMAND ror_v0_post_process_math_tests
)

add_test(
    NAME post_process_policy
    COMMAND ror_post_process_policy_tests
)

add_test(
    NAME post_process_runtime_contract
    COMMAND ror_post_process_runtime_contract_tests
)

add_test(
    NAME renderer_backend_policy
    COMMAND ror_renderer_backend_policy_tests
)

add_test(
    NAME graphics_scene_snapshot_producer
    COMMAND ror_graphics_scene_snapshot_producer_tests
)

add_test(
    NAME render_resource_handle
    COMMAND ror_render_resource_handle_tests
)

add_test(
    NAME render_material_descriptor
    COMMAND ror_render_material_descriptor_tests
)

add_test(
    NAME render_hdr_reference
    COMMAND ror_render_hdr_reference_tests
)

add_test(
    NAME render_asset_registry
    COMMAND ror_render_asset_registry_tests
)

add_test(
    NAME render_scene_snapshot
    COMMAND ror_render_scene_snapshot_tests
)

add_test(
    NAME render_resource_descriptors
    COMMAND ror_render_resource_descriptor_tests
)

add_test(
    NAME renderer_frontend_contract
    COMMAND ror_renderer_frontend_contract_tests
)

add_test(
    NAME renderer_boundary_header_isolation
    COMMAND ror_renderer_boundary_header_isolation_tests
)

add_test(
    NAME ogre_next_n1_policy
    COMMAND ror_ogre_next_n1_policy_tests
)

add_test(
    NAME ogre_next_n2_interop_state
    COMMAND ror_ogre_next_n2_interop_state_tests
)

add_test(
    NAME ogre_next_vulkan_rt5_contract
    COMMAND ror_ogre_next_vulkan_rt5_contract_tests
)

add_test(
    NAME ogre_next_vulkan_rt6_contract
    COMMAND ror_ogre_next_vulkan_rt6_contract_tests
)

add_test(
    NAME local_light_budget
    COMMAND ror_local_light_budget_tests
)

add_test(
    NAME command_line_info
    COMMAND ror_command_line_info_tests
)

add_test(
    NAME beam_axial_response
    COMMAND ror_beam_axial_response_tests
)

add_test(
    NAME worldmodel_observation_scheduler
    COMMAND ror_worldmodel_observation_scheduler_tests
)

add_test(
    NAME worldmodel_capture_contract
    COMMAND ror_worldmodel_capture_contract_tests
)

add_test(
    NAME worldmodel_capture_session
    COMMAND ror_worldmodel_capture_session_tests
)

add_test(
    NAME worldmodel_episode_provenance
    COMMAND ror_worldmodel_episode_provenance_tests
)

add_test(
    NAME worldmodel_runtime_backend
    COMMAND ror_worldmodel_runtime_backend_tests
)

add_test(
    NAME worldmodel_telemetry
    COMMAND ror_worldmodel_telemetry_tests
)

add_test(
    NAME worldmodel_episode_io
    COMMAND ror_worldmodel_episode_io_tests
)

add_test(
    NAME worldmodel_episode
    COMMAND ror_worldmodel_episode_tests
)

add_test(
    NAME worldmodel_live_capture_config
    COMMAND ror_worldmodel_live_capture_config_tests
)

add_test(
    NAME worldmodel_platform_identity
    COMMAND ror_worldmodel_platform_identity_tests
)

set_tests_properties(
    worldmodel_episode_io
    worldmodel_platform_identity
    PROPERTIES TIMEOUT 30
)

add_test(
    NAME beam_rest_length_scale
    COMMAND ror_beam_rest_length_scale_tests
)

add_test(
    NAME deterministic_counter_noise
    COMMAND ror_deterministic_counter_noise_tests
)

add_test(
    NAME deterministic_fixed_step_cadence
    COMMAND ror_deterministic_fixed_step_cadence_tests
)

add_test(
    NAME fixed_step_capture_bridge
    COMMAND ror_fixed_step_capture_bridge_tests
)

add_test(
    NAME deterministic_scenario_schedule
    COMMAND ror_deterministic_scenario_schedule_tests
)

add_test(
    NAME deterministic_contact_order
    COMMAND ror_deterministic_contact_order_tests
)

add_test(
    NAME deterministic_state_digest
    COMMAND ror_deterministic_state_digest_tests
)

add_test(
    NAME deterministic_state_trace
    COMMAND ror_deterministic_state_trace_tests
)

add_test(
    NAME deterministic_input_trace
    COMMAND ror_deterministic_input_trace_tests
)

add_test(
    NAME deterministic_input_trace_runtime
    COMMAND ror_deterministic_input_trace_runtime_tests
)

add_test(
    NAME deterministic_vehicle_input
    COMMAND ror_deterministic_vehicle_input_tests
)

add_test(
    NAME calibrated_beam_material
    COMMAND ror_calibrated_beam_material_tests
)

add_test(
    NAME calibrated_beam_material_adapter
    COMMAND ror_calibrated_beam_material_adapter_tests
)

add_test(
    NAME calibrated_beam_step_sensitivity
    COMMAND ror_calibrated_beam_step_sensitivity_tests
)

add_test(
    NAME calibrated_beam_mesh_refinement
    COMMAND ror_calibrated_beam_mesh_refinement_tests
)

add_test(
    NAME calibrated_beam_savegame
    COMMAND ror_calibrated_beam_savegame_tests
)

if (TARGET ror_calibrated_beam_savegame_json_tests)
    add_test(
        NAME calibrated_beam_savegame_json
        COMMAND ror_calibrated_beam_savegame_json_tests
    )
endif ()

add_test(
    NAME rigdef_calibrated_beam_material
    COMMAND ror_rigdef_calibrated_beam_material_tests
)

if (TARGET RoR)
    add_test(
        NAME rigdef_calibrated_beam_material_roundtrip_integration
        COMMAND
            $<TARGET_FILE:RoR>
            --internal-rigdef-calibrated-material-roundtrip
            "${CMAKE_CURRENT_LIST_DIR}/fixtures/rigdef/calibrated_beam_material_roundtrip.truck"
    )
endif ()

add_test(
    NAME hydro_actuator_response
    COMMAND ror_hydro_actuator_response_tests
)

add_test(
    NAME shader_compatibility_policy
    COMMAND ror_shader_compatibility_policy_tests
)

add_test(
    NAME legacy_material_script_sanitizer
    COMMAND ror_legacy_material_script_sanitizer_tests
)

add_test(
    NAME legacy_material_compatibility_plan
    COMMAND ror_legacy_material_compatibility_plan_tests
)

add_test(
    NAME terrain_bundle_archive_verifier
    COMMAND ror_terrain_bundle_archive_verifier_tests
)

add_test(
    NAME terrain_bundle_dependency
    COMMAND ror_terrain_bundle_dependency_tests
)

add_test(
    NAME cityworld_neoq20_compatibility
    COMMAND ror_cityworld_neoq20_compatibility_tests
)

add_test(
    NAME cityworld_neoq_tree_compatibility
    COMMAND ror_cityworld_neoq_tree_compatibility_tests
)

add_test(
    NAME cityworld_penguin_road_compatibility
    COMMAND ror_cityworld_penguin_road_compatibility_tests
)

if (TARGET ror_terrain_bundle_config_syntax_tests)
    add_test(
        NAME terrain_bundle_config_syntax
        COMMAND ror_terrain_bundle_config_syntax_tests
    )
endif ()

add_test(
    NAME beamng_package_manifest
    COMMAND ror_beamng_package_manifest_tests
)

add_test(
    NAME beamng_material_inventory
    COMMAND ror_beamng_material_inventory_tests
)

add_test(
    NAME beamng_zip_archive_index
    COMMAND ror_beamng_zip_archive_index_tests
)

add_test(
    NAME beamng_zip_index_help
    COMMAND ror_beamng_zip_index --help
)

add_test(
    NAME beamng_formulacoupe_opt_in
    COMMAND
        "${CMAKE_COMMAND}"
        "-DROR_BEAMNG_ZIP_INDEX_TOOL=$<TARGET_FILE:ror_beamng_zip_index>"
        -P
        "${CMAKE_CURRENT_LIST_DIR}/resources/FormulaCoupeOptIn.cmake"
)
set_tests_properties(
    beamng_formulacoupe_opt_in
    PROPERTIES
        SKIP_REGULAR_EXPRESSION
            "SKIP: FormulaCOUPE fixture is not explicitly supplied"
)

add_test(
    NAME jbeam_expression_evaluator
    COMMAND ror_jbeam_expression_evaluator_tests
)

add_test(
    NAME jbeam_syntax
    COMMAND ror_jbeam_syntax_tests
)

add_test(
    NAME jbeam_coordinate_transform
    COMMAND ror_jbeam_coordinate_transform_tests
)

add_test(
    NAME jbeam_advanced_structure_ir
    COMMAND ror_jbeam_advanced_structure_ir_tests
)

add_test(
    NAME jbeam_part_resolver
    COMMAND ror_jbeam_part_resolver_tests
)

add_test(
    NAME jbeam_pressure_wheel_ir
    COMMAND ror_jbeam_pressure_wheel_ir_tests
)

add_test(
    NAME jbeam_structural_ir
    COMMAND ror_jbeam_structural_ir_tests
)

add_test(
    NAME jbeam_to_rigdef
    COMMAND ror_jbeam_to_rigdef_tests
)

if (APPLE)
    add_test(
        NAME metal_rt_probe
        COMMAND ror_metal_rt_probe
    )
    set_tests_properties(
        metal_rt_probe
        PROPERTIES
            LABELS "gfx;native-rt"
            SKIP_RETURN_CODE 77
            TIMEOUT 30
    )

    add_test(
        NAME apple_executable_path
        COMMAND ror_apple_executable_path_tests
    )
    add_test(
        NAME macos_user_directory_layout
        COMMAND ror_macos_user_directory_layout_tests
    )
    if (ROR_OGRE14)
        add_test(
            NAME macos_controller_contract
            COMMAND ror_macos_controller_contract_tests
        )
        add_test(
            NAME macos_input_bridge
            COMMAND ror_macos_input_bridge_tests
        )
    endif ()
    if (ROR_USE_OPENAL)
        add_test(
            NAME macos_openal_smoke
            COMMAND ror_macos_openal_smoke_tests
        )
    endif ()
endif ()
