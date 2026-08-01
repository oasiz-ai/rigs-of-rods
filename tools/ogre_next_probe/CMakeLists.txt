cmake_minimum_required(VERSION 3.24)

project(ror_ogre_next_probe LANGUAGES C CXX)

option(
    ROR_OGRE_NEXT_PROBE
    "Explicitly opt in to the standalone pinned OGRE-Next dependency probe"
    OFF)
if (NOT ROR_OGRE_NEXT_PROBE)
    message(FATAL_ERROR
        "The OGRE-Next probe is opt-in. Reconfigure with "
        "-DROR_OGRE_NEXT_PROBE=ON or use tools/run_ogre_next_probe.py.")
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR
        "The reviewed OGRE-Next probe contract requires CMAKE_BUILD_TYPE=Release")
endif ()

set(ROR_OGRE_NEXT_STANDALONE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PinnedOgreNext.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/probe_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_probe_config.h"
    @ONLY)
set(ROR_OGRE_NEXT_MEDIA_ROOT "${ogre_next_SOURCE_DIR}/Samples/Media")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/frame_probe_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_frame_probe_config.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend_n1_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_n1_config.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ogre_next_build_contract.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-build-contract.json"
    @ONLY)

add_executable(ror_ogre_next_probe src/main.cpp)
target_compile_definitions(
    ror_ogre_next_probe PRIVATE ${_ror_probe_renderer_definition})
target_include_directories(
    ror_ogre_next_probe
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${ogre_next_SOURCE_DIR}/OgreMain/include"
        "${ogre_next_BINARY_DIR}/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Common/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Pbs/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include")
target_link_libraries(
    ror_ogre_next_probe
    PRIVATE
        OgreNextHlmsPbs
        ${ROR_OGRE_NEXT_RENDERER_TARGET}
        OgreNextMain)
set_target_properties(
    ror_ogre_next_probe
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(ror_ogre_next_frame_probe src/frame_main.cpp)
target_compile_definitions(
    ror_ogre_next_frame_probe PRIVATE ${_ror_probe_renderer_definition})
target_include_directories(
    ror_ogre_next_frame_probe
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${ogre_next_SOURCE_DIR}/OgreMain/include"
        "${ogre_next_BINARY_DIR}/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Common/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Pbs/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include")
target_link_libraries(
    ror_ogre_next_frame_probe
    PRIVATE
        OgreNextHlmsPbs
        ${ROR_OGRE_NEXT_RENDERER_TARGET}
        OgreNextMain)
set_target_properties(
    ror_ogre_next_frame_probe
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(
    ROR_OGRE_NEXT_FRONTEND_N1
    "Build the isolated static-PBR Ogre-Next N1 frontend"
    ON)
if (ROR_OGRE_NEXT_FRONTEND_N1)
    if (TARGET OgreMain)
        message(FATAL_ERROR
            "The Ogre-Next N1 target cannot coexist with an OGRE 1.x target")
    endif ()

    set(_ror_render_root
        "${CMAKE_CURRENT_SOURCE_DIR}/../../source/main/gfx/render")
    add_library(
        ror_ogre_next_n1_contract STATIC
        "${_ror_render_root}/MaterialDescriptor.cpp"
        "${_ror_render_root}/RenderAssetId.cpp"
        "${_ror_render_root}/RenderAssetRegistry.cpp"
        "${_ror_render_root}/RenderFrame.cpp"
        "${_ror_render_root}/RenderResourceDescriptors.cpp"
        "${_ror_render_root}/RendererFrontend.cpp"
        "${_ror_render_root}/ResourceHandle.cpp"
        "${_ror_render_root}/SceneSnapshot.cpp")
    target_include_directories(
        ror_ogre_next_n1_contract
        PUBLIC "${_ror_render_root}")

    add_library(
        ror_ogre_next_frontend_n1 STATIC
        "${_ror_render_root}/ogrenext/OgreNextN1Frontend.cpp"
        "${_ror_render_root}/ogrenext/OgreNextN1Policy.cpp")
    if (APPLE)
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_METAL=1)
    elseif (WIN32)
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_D3D11=1)
    else ()
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_VULKAN=1)
    endif ()
    target_compile_definitions(
        ror_ogre_next_frontend_n1 PRIVATE ${_ror_n1_renderer_definition})
    target_include_directories(
        ror_ogre_next_frontend_n1
        PUBLIC "${_ror_render_root}/ogrenext"
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_include_directories(
        ror_ogre_next_frontend_n1
        SYSTEM PRIVATE
            "${ogre_next_SOURCE_DIR}/OgreMain/include"
            "${ogre_next_BINARY_DIR}/include"
            "${ogre_next_SOURCE_DIR}/Components/Hlms/Common/include"
            "${ogre_next_SOURCE_DIR}/Components/Hlms/Pbs/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include")
    target_link_libraries(
        ror_ogre_next_frontend_n1
        PUBLIC ror_ogre_next_n1_contract
        PRIVATE
            OgreNextHlmsPbs
            ${ROR_OGRE_NEXT_RENDERER_TARGET}
            OgreNextMain)

    add_executable(
        ror_ogre_next_frontend_n1_smoke
        src/frontend_n1_smoke.cpp)
    target_include_directories(
        ror_ogre_next_frontend_n1_smoke
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(
        ror_ogre_next_frontend_n1_smoke
        PRIVATE ror_ogre_next_frontend_n1)
    set_target_properties(
        ror_ogre_next_frontend_n1_smoke
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    foreach (_ror_n1_target IN ITEMS
            ror_ogre_next_n1_contract ror_ogre_next_frontend_n1
            ror_ogre_next_frontend_n1_smoke)
        set_target_properties(
            ${_ror_n1_target}
            PROPERTIES
                CXX_STANDARD 17
                CXX_STANDARD_REQUIRED YES
                CXX_EXTENSIONS NO)
        if (MSVC)
            target_compile_options(
                ${_ror_n1_target}
                PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /fp:strict)
        else ()
            target_compile_options(
                ${_ror_n1_target}
                PRIVATE -Wall -Wextra -Werror -pedantic -fno-fast-math
                        -ffp-contract=off)
        endif ()
    endforeach ()
endif ()

set(ROR_OGRE_NEXT_REPORT
    "${CMAKE_BINARY_DIR}/ror-ogre-next-probe-report.json")
add_custom_target(
    ror_ogre_next_probe_report
    COMMAND
        $<TARGET_FILE:ror_ogre_next_probe>
        --output "${ROR_OGRE_NEXT_REPORT}"
    DEPENDS ror_ogre_next_probe
    BYPRODUCTS "${ROR_OGRE_NEXT_REPORT}"
    COMMENT "Running fail-closed OGRE-Next capability probe"
    VERBATIM)

set(ROR_OGRE_NEXT_FRAME_IMAGE
    "${CMAKE_BINARY_DIR}/ror-ogre-next-frame-probe.ppm")
set(ROR_OGRE_NEXT_FRAME_REPORT
    "${CMAKE_BINARY_DIR}/ror-ogre-next-frame-probe-report.json")
set(ROR_OGRE_NEXT_CTEST_FRAME_IMAGE
    "${CMAKE_BINARY_DIR}/ror-ogre-next-frame-probe-ctest.ppm")
set(ROR_OGRE_NEXT_CTEST_FRAME_REPORT
    "${CMAKE_BINARY_DIR}/ror-ogre-next-frame-probe-ctest-report.json")
add_custom_target(
    ror_ogre_next_frame_probe_report
    COMMAND
        "${CMAKE_COMMAND}" -E rm -f
        "${ROR_OGRE_NEXT_FRAME_IMAGE}"
        "${ROR_OGRE_NEXT_FRAME_REPORT}"
    COMMAND
        $<TARGET_FILE:ror_ogre_next_frame_probe>
        --output "${ROR_OGRE_NEXT_FRAME_IMAGE}"
        --report "${ROR_OGRE_NEXT_FRAME_REPORT}"
    DEPENDS ror_ogre_next_frame_probe
    BYPRODUCTS
        "${ROR_OGRE_NEXT_FRAME_IMAGE}"
        "${ROR_OGRE_NEXT_FRAME_REPORT}"
    COMMENT "Rendering and reading back one UI-free OGRE-Next PBS frame"
    VERBATIM)

enable_testing()
add_test(NAME ror_ogre_next_probe_runtime
    COMMAND $<TARGET_FILE:ror_ogre_next_probe>)
add_test(NAME ror_ogre_next_frame_probe_runtime
    COMMAND $<TARGET_FILE:ror_ogre_next_frame_probe>
            --output "${ROR_OGRE_NEXT_CTEST_FRAME_IMAGE}"
            --report "${ROR_OGRE_NEXT_CTEST_FRAME_REPORT}")

if (TARGET ror_ogre_next_frontend_n1_smoke)
    set(ROR_OGRE_NEXT_N1_IMAGE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-n1.ppm")
    set(ROR_OGRE_NEXT_N1_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-n1-report.json")
    set(ROR_OGRE_NEXT_N1_CTEST_IMAGE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-n1-ctest.ppm")
    set(ROR_OGRE_NEXT_N1_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-n1-ctest-report.json")
    add_custom_target(
        ror_ogre_next_frontend_n1_report
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${ROR_OGRE_NEXT_N1_IMAGE}"
            "${ROR_OGRE_NEXT_N1_REPORT}"
        COMMAND
            $<TARGET_FILE:ror_ogre_next_frontend_n1_smoke>
            --output "${ROR_OGRE_NEXT_N1_IMAGE}"
            --report "${ROR_OGRE_NEXT_N1_REPORT}"
        DEPENDS ror_ogre_next_frontend_n1_smoke
        BYPRODUCTS
            "${ROR_OGRE_NEXT_N1_IMAGE}"
            "${ROR_OGRE_NEXT_N1_REPORT}"
        COMMENT "Running isolated Ogre-Next N1 v2-VAO HDR/SDR frontend smoke"
        VERBATIM)
    add_test(NAME ror_ogre_next_frontend_n1_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_frontend_n1_smoke>
                --output "${ROR_OGRE_NEXT_N1_CTEST_IMAGE}"
                --report "${ROR_OGRE_NEXT_N1_CTEST_REPORT}")
endif ()
