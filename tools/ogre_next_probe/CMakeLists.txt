cmake_minimum_required(VERSION 3.24)

project(ror_ogre_next_probe LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

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
get_filename_component(ROR_REPOSITORY_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ROR_SOURCE_REPOSITORY "https://github.com/oasiz-ai/rigs-of-rods")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${ROR_REPOSITORY_ROOT}" rev-parse HEAD
    RESULT_VARIABLE _ror_git_commit_result
    OUTPUT_VARIABLE ROR_SOURCE_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${ROR_REPOSITORY_ROOT}"
            rev-parse --abbrev-ref HEAD
    RESULT_VARIABLE _ror_git_ref_result
    OUTPUT_VARIABLE ROR_SOURCE_REF
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
string(LENGTH "${ROR_SOURCE_COMMIT}" _ror_source_commit_length)
if (NOT _ror_git_commit_result EQUAL 0 OR
        NOT _ror_source_commit_length EQUAL 40 OR
        NOT ROR_SOURCE_COMMIT MATCHES "^[0-9a-f]+$" OR
        NOT _ror_git_ref_result EQUAL 0 OR
        NOT ROR_SOURCE_REF MATCHES "^[A-Za-z0-9._/-]+$")
    message(FATAL_ERROR "RoR source Git identity cannot be attested")
endif ()
file(GLOB_RECURSE _ror_relevant_source_files
    LIST_DIRECTORIES false
    RELATIVE "${ROR_REPOSITORY_ROOT}"
    "${ROR_REPOSITORY_ROOT}/source/main/gfx/render/*"
    "${ROR_REPOSITORY_ROOT}/tools/ogre_next_probe/*")
list(APPEND _ror_relevant_source_files
    "tools/run_ogre_next_probe.py"
    "tools/validate_ogre_next_frame_probe.py"
    "tools/verify_ogre_next_artifact_set.py")
list(FILTER _ror_relevant_source_files EXCLUDE REGEX
    "(^|/)__pycache__/|\\.py[co]$|(^|/)\\.DS_Store$")
list(REMOVE_DUPLICATES _ror_relevant_source_files)
list(SORT _ror_relevant_source_files)
set(_ror_source_manifest_serialized "")
foreach (_ror_source_relative IN LISTS _ror_relevant_source_files)
    set(_ror_source_file "${ROR_REPOSITORY_ROOT}/${_ror_source_relative}")
    if (NOT EXISTS "${_ror_source_file}" OR IS_SYMLINK "${_ror_source_file}")
        message(FATAL_ERROR
            "RoR relevant source is missing or indirect: ${_ror_source_relative}")
    endif ()
    if (_ror_source_relative MATCHES "[;\\\\\"]")
        message(FATAL_ERROR
            "RoR relevant source path cannot be attested: ${_ror_source_relative}")
    endif ()
    file(SIZE "${_ror_source_file}" _ror_source_size)
    file(SHA256 "${_ror_source_file}" _ror_source_sha256)
    string(APPEND _ror_source_manifest_serialized
        "${_ror_source_relative}|${_ror_source_size}|${_ror_source_sha256}\n")
endforeach ()
list(LENGTH _ror_relevant_source_files ROR_SOURCE_MANIFEST_FILE_COUNT)
string(SHA256 ROR_SOURCE_MANIFEST_SHA256
    "${_ror_source_manifest_serialized}")
set(ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE
    "share/rigsofrods/ogre-next/Samples/Media")
set(ROR_OGRE_NEXT_N1_PACKAGE_ROR_LICENSE_RELATIVE
    "licenses/Rigs-of-Rods-GPL-3.0.txt")
set(ROR_OGRE_NEXT_N1_PACKAGE_OGRE_LICENSE_RELATIVE
    "licenses/Ogre-Next-MIT.txt")
set(ROR_OGRE_NEXT_N1_PACKAGE_RAPIDJSON_LICENSE_RELATIVE
    "licenses/RapidJSON-license.txt")
set(ROR_OGRE_NEXT_N1_PACKAGE_SHADER_NOTICE_RELATIVE
    "licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/probe_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_probe_config.h"
    @ONLY)
set(ROR_OGRE_NEXT_MEDIA_ROOT "${ogre_next_SOURCE_DIR}/Samples/Media")
set(_ror_n1_hlms_root "${ROR_OGRE_NEXT_MEDIA_ROOT}/Hlms")
file(GLOB_RECURSE _ror_n1_hlms_files
    LIST_DIRECTORIES false
    RELATIVE "${_ror_n1_hlms_root}"
    "${_ror_n1_hlms_root}/*")
list(SORT _ror_n1_hlms_files)
if (NOT _ror_n1_hlms_files)
    message(FATAL_ERROR "The pinned Ogre-Next HLMS file manifest is empty")
endif ()
set(ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_ENTRIES "")
set(_ror_n1_media_manifest_serialized "")
foreach (_ror_n1_hlms_relative IN LISTS _ror_n1_hlms_files)
    set(_ror_n1_hlms_file
        "${_ror_n1_hlms_root}/${_ror_n1_hlms_relative}")
    if (IS_SYMLINK "${_ror_n1_hlms_file}")
        message(FATAL_ERROR
            "Pinned Ogre-Next HLMS manifest contains a symbolic link: "
            "${_ror_n1_hlms_relative}")
    endif ()
    if (_ror_n1_hlms_relative MATCHES "[;\\\\\"]")
        message(FATAL_ERROR
            "Pinned Ogre-Next HLMS path cannot be represented exactly: "
            "${_ror_n1_hlms_relative}")
    endif ()
    file(SIZE "${_ror_n1_hlms_file}" _ror_n1_hlms_size)
    file(SHA256 "${_ror_n1_hlms_file}" _ror_n1_hlms_sha256)
    string(APPEND ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_ENTRIES
        "    {\"${_ror_n1_hlms_relative}\", "
        "static_cast<std::uint64_t>(${_ror_n1_hlms_size}), "
        "\"${_ror_n1_hlms_sha256}\"},\n")
    string(APPEND _ror_n1_media_manifest_serialized
        "${_ror_n1_hlms_relative}|${_ror_n1_hlms_size}|"
        "${_ror_n1_hlms_sha256}\n")
endforeach ()
list(LENGTH _ror_n1_hlms_files ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_COUNT)
list(GET _ror_n1_hlms_files 0 ROR_OGRE_NEXT_N1_MEDIA_TAMPER_RELATIVE)
string(SHA256 ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_SHA256
    "${_ror_n1_media_manifest_serialized}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend_n1_media_manifest.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_n1_media_manifest.h"
    @ONLY)
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
        "${_ror_render_root}/ogrenext/OgreNextN1MediaIntegrity.cpp"
        "${_ror_render_root}/ogrenext/OgreNextN1Policy.cpp")
    if (APPLE)
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_METAL=1)
        target_sources(
            ror_ogre_next_frontend_n1
            PRIVATE
                "${_ror_render_root}/ogrenext/OgreNextN2InteropState.cpp"
                "${_ror_render_root}/ogrenext/OgreNextMetalInterop.mm"
                "${_ror_render_root}/ogrenext/OgreNextMetalRayTracingBackend.mm")
        target_compile_options(
            ror_ogre_next_frontend_n1
            PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
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

    set(_ror_n1_targets
        ror_ogre_next_n1_contract
        ror_ogre_next_frontend_n1
        ror_ogre_next_frontend_n1_smoke)
    if (APPLE)
        execute_process(
            COMMAND git rev-parse --verify HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../.."
            RESULT_VARIABLE _ror_n2_commit_result
            OUTPUT_VARIABLE _ror_n2_source_commit
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if (NOT _ror_n2_commit_result EQUAL 0)
            set(_ror_n2_source_commit "unknown")
        endif ()
        execute_process(
            COMMAND git symbolic-ref --short HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../.."
            RESULT_VARIABLE _ror_n2_ref_result
            OUTPUT_VARIABLE _ror_n2_source_ref
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if (NOT _ror_n2_ref_result EQUAL 0)
            set(_ror_n2_source_ref "detached")
        endif ()
        add_executable(
            ror_ogre_next_metal_n2_smoke
            src/metal_n2_smoke.cpp)
        target_include_directories(
            ror_ogre_next_metal_n2_smoke
            PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_compile_definitions(
            ror_ogre_next_metal_n2_smoke
            PRIVATE
                ROR_OGRE_NEXT_N2_SOURCE_COMMIT="${_ror_n2_source_commit}"
                ROR_OGRE_NEXT_N2_SOURCE_REF="${_ror_n2_source_ref}")
        target_link_libraries(
            ror_ogre_next_metal_n2_smoke
            PRIVATE ror_ogre_next_frontend_n1)
        set_target_properties(
            ror_ogre_next_metal_n2_smoke
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        list(APPEND _ror_n1_targets ror_ogre_next_metal_n2_smoke)
    endif ()

    set(ROR_OGRE_NEXT_N1_PACKAGE_ROOT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-n1-package")
    set(ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE}")
    set(ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/bin/$<TARGET_FILE_NAME:ror_ogre_next_frontend_n1_smoke>")
    set(ROR_OGRE_NEXT_N1_PACKAGE_STAMP
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/.stage-v1")
    add_custom_command(
        OUTPUT "${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/bin"
                "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/licenses"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_FILE:ror_ogre_next_frontend_n1_smoke>
                "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${ogre_next_SOURCE_DIR}/Samples/Media/Hlms"
                "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/Hlms"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_SHADER_NOTICE_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/../../COPYING"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_ROR_LICENSE_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_LICENSE_PATH}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_OGRE_LICENSE_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${rapidjson_SOURCE_DIR}/${ROR_RAPIDJSON_LICENSE_PATH}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_RAPIDJSON_LICENSE_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E touch
                "${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}"
        DEPENDS
            ror_ogre_next_frontend_n1_smoke
            "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../COPYING"
            "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_LICENSE_PATH}"
            "${rapidjson_SOURCE_DIR}/${ROR_RAPIDJSON_LICENSE_PATH}"
        COMMENT "Staging relocatable Ogre-Next N1 runtime and shader media"
        VERBATIM)
    add_custom_target(ror_ogre_next_frontend_n1_package ALL
        DEPENDS "${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}")

    foreach (_ror_n1_target IN LISTS _ror_n1_targets)
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
            "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
            --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            --output "${ROR_OGRE_NEXT_N1_IMAGE}"
            --report "${ROR_OGRE_NEXT_N1_REPORT}"
        DEPENDS ror_ogre_next_frontend_n1_package
        BYPRODUCTS
            "${ROR_OGRE_NEXT_N1_IMAGE}"
            "${ROR_OGRE_NEXT_N1_REPORT}"
        COMMENT "Running isolated Ogre-Next N1 v2-VAO HDR/SDR frontend smoke"
        VERBATIM)
    add_test(NAME ror_ogre_next_frontend_n1_runtime
        COMMAND "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
                --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                --output "${ROR_OGRE_NEXT_N1_CTEST_IMAGE}"
                --report "${ROR_OGRE_NEXT_N1_CTEST_REPORT}")
    set_tests_properties(ror_ogre_next_frontend_n1_runtime PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ror_ogre_next_frontend_n1_media_tamper
        COMMAND "${CMAKE_COMMAND}"
            "-DN1_EXECUTABLE=${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
            "-DN1_MEDIA_ROOT=${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            "-DN1_TAMPER_RELATIVE=${ROR_OGRE_NEXT_N1_MEDIA_TAMPER_RELATIVE}"
            "-DN1_WORK_ROOT=${CMAKE_BINARY_DIR}/n1-media-tamper"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyN1MediaTamper.cmake")
    set_tests_properties(ror_ogre_next_frontend_n1_media_tamper PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()

if (TARGET ror_ogre_next_metal_n2_smoke)
    set(ROR_OGRE_NEXT_N2_READBACK
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n2.rgba")
    set(ROR_OGRE_NEXT_N2_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n2-report.json")
    set(ROR_OGRE_NEXT_N2_CTEST_READBACK
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n2-ctest.rgba")
    set(ROR_OGRE_NEXT_N2_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n2-ctest-report.json")
    add_custom_target(
        ror_ogre_next_metal_n2_report
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${ROR_OGRE_NEXT_N2_READBACK}"
            "${ROR_OGRE_NEXT_N2_REPORT}"
        COMMAND
            $<TARGET_FILE:ror_ogre_next_metal_n2_smoke>
            --output "${ROR_OGRE_NEXT_N2_READBACK}"
            --report "${ROR_OGRE_NEXT_N2_REPORT}"
        DEPENDS ror_ogre_next_metal_n2_smoke
        BYPRODUCTS
            "${ROR_OGRE_NEXT_N2_READBACK}"
            "${ROR_OGRE_NEXT_N2_REPORT}"
        COMMENT "Running Ogre-Next Metal N2 same-device geometry/RT proof"
        VERBATIM)
    add_test(NAME ror_ogre_next_metal_n2_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_metal_n2_smoke>
                --output "${ROR_OGRE_NEXT_N2_CTEST_READBACK}"
                --report "${ROR_OGRE_NEXT_N2_CTEST_REPORT}")
    set_tests_properties(
        ror_ogre_next_metal_n2_runtime PROPERTIES TIMEOUT 120)
endif ()
