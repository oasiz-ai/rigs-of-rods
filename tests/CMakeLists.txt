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

target_link_libraries(
    ror_deterministic_counter_noise_tests
    PRIVATE Threads::Threads
)

set(ROR_PHYSICS_TEST_TARGETS
    ror_beam_axial_response_tests
    ror_deterministic_counter_noise_tests
)

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
