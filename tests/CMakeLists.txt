if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(RoRPhysicsTests LANGUAGES CXX)
    enable_testing()
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
    ror_deterministic_counter_noise_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicCounterNoiseTests.cpp"
)

add_executable(
    ror_deterministic_contact_order_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/DeterministicContactOrderTests.cpp"
)

add_executable(
    ror_calibrated_beam_material_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/CalibratedBeamMaterialTests.cpp"
)

add_executable(
    ror_beamng_package_manifest_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/BeamNGPackageManifestTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/BeamNGPackageManifest.cpp"
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
    ror_jbeam_part_resolver_tests
    "${CMAKE_CURRENT_LIST_DIR}/resources/JBeamPartResolverTests.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamPartResolver.cpp"
    "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng/JBeamSyntax.cpp"
)

target_include_directories(
    ror_beamng_package_manifest_tests
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
    ror_jbeam_part_resolver_tests
    PRIVATE
        "${ROR_REPOSITORY_ROOT}/source/main/resources/beamng"
)

if (APPLE)
    add_executable(
        ror_apple_executable_path_tests
        "${CMAKE_CURRENT_LIST_DIR}/platform/AppleExecutablePathTests.cpp"
    )

    target_include_directories(
        ror_apple_executable_path_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/utils"
    )
endif ()

target_link_libraries(
    ror_deterministic_counter_noise_tests
    PRIVATE Threads::Threads
)

set(ROR_PHYSICS_TEST_TARGETS
    ror_beam_axial_response_tests
    ror_beamng_package_manifest_tests
    ror_calibrated_beam_material_tests
    ror_deterministic_contact_order_tests
    ror_deterministic_counter_noise_tests
    ror_jbeam_coordinate_transform_tests
    ror_jbeam_part_resolver_tests
    ror_jbeam_syntax_tests
)

if (APPLE)
    list(APPEND ROR_PHYSICS_TEST_TARGETS ror_apple_executable_path_tests)
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

add_test(
    NAME beam_axial_response
    COMMAND ror_beam_axial_response_tests
)

add_test(
    NAME deterministic_counter_noise
    COMMAND ror_deterministic_counter_noise_tests
)

add_test(
    NAME deterministic_contact_order
    COMMAND ror_deterministic_contact_order_tests
)

add_test(
    NAME calibrated_beam_material
    COMMAND ror_calibrated_beam_material_tests
)

add_test(
    NAME beamng_package_manifest
    COMMAND ror_beamng_package_manifest_tests
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
    NAME jbeam_part_resolver
    COMMAND ror_jbeam_part_resolver_tests
)

if (APPLE)
    add_test(
        NAME apple_executable_path
        COMMAND ror_apple_executable_path_tests
    )
endif ()
