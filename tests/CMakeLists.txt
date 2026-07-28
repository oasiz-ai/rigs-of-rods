if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(RoRPhysicsTests LANGUAGES CXX)
    enable_testing()
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
    ror_beamng_package_manifest_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/BeamNGPackageManifestTests.cpp"
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
    ror_beam_axial_response_tests
    ror_beam_rest_length_scale_tests
    ror_beamng_package_manifest_tests
    ror_beamng_zip_archive_index_tests
    ror_beamng_zip_index
    ror_calibrated_beam_material_adapter_tests
    ror_calibrated_beam_material_tests
    ror_calibrated_beam_step_sensitivity_tests
    ror_calibrated_beam_savegame_tests
    ror_rigdef_calibrated_beam_material_tests
    ror_deterministic_contact_order_tests
    ror_deterministic_counter_noise_tests
    ror_deterministic_fixed_step_cadence_tests
    ror_deterministic_input_trace_tests
    ror_deterministic_state_digest_tests
    ror_deterministic_state_trace_tests
    ror_state_trace
    ror_hydro_actuator_response_tests
    ror_shader_compatibility_policy_tests
    ror_jbeam_advanced_structure_ir_tests
    ror_jbeam_coordinate_transform_tests
    ror_jbeam_expression_evaluator_tests
    ror_jbeam_part_resolver_tests
    ror_jbeam_pressure_wheel_ir_tests
    ror_jbeam_structural_ir_tests
    ror_jbeam_syntax_tests
    ror_jbeam_to_rigdef_tests
)

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

set_target_properties(ror_jbeam_to_rigdef_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_test(
    NAME beam_axial_response
    COMMAND ror_beam_axial_response_tests
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
    NAME beamng_package_manifest
    COMMAND ror_beamng_package_manifest_tests
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
