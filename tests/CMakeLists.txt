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

add_executable(
    ror_beam_axial_response_tests
    "${CMAKE_CURRENT_LIST_DIR}/physics/BeamAxialResponseTests.cpp"
)

target_include_directories(ror_beam_axial_response_tests PRIVATE
    "${ROR_REPOSITORY_ROOT}/source/main/physics"
)

set_target_properties(ror_beam_axial_response_tests PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

if (MSVC)
    target_compile_options(ror_beam_axial_response_tests PRIVATE /W4 /WX)
    if (ROR_TEST_GAME_FAST_MATH)
        target_compile_options(ror_beam_axial_response_tests PRIVATE /fp:fast)
    endif ()
else ()
    target_compile_options(
        ror_beam_axial_response_tests
        PRIVATE -Wall -Wextra -Werror -pedantic
    )
    if (ROR_TEST_GAME_FAST_MATH)
        target_compile_options(ror_beam_axial_response_tests PRIVATE -ffast-math)
    endif ()
endif ()

add_test(
    NAME beam_axial_response
    COMMAND ror_beam_axial_response_tests
)
