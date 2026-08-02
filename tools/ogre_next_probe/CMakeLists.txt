cmake_minimum_required(VERSION 3.24)

project(ror_ogre_next_probe LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
    # SDL's Cocoa backend contains Objective-C (.m) translation units. Enable
    # OBJC explicitly so CMake does not route them through the Objective-C++
    # compiler and leak the probe's C++17 flag into plain Objective-C sources.
    enable_language(OBJC)
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
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
get_filename_component(ROR_REPOSITORY_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ROR_SOURCE_REPOSITORY "https://github.com/oasiz-ai/rigs-of-rods")
if (DEFINED ENV{ROR_OGRE_NEXT_EXPECTED_ROR_REPOSITORY} AND
        NOT "$ENV{ROR_OGRE_NEXT_EXPECTED_ROR_REPOSITORY}" STREQUAL
            "${ROR_SOURCE_REPOSITORY}")
    message(FATAL_ERROR
        "Expected RoR repository differs from the reviewed source identity")
endif ()
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${ROR_REPOSITORY_ROOT}" rev-parse HEAD
    RESULT_VARIABLE _ror_git_commit_result
    OUTPUT_VARIABLE _ror_checked_out_source_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if (DEFINED ENV{ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT} AND
        NOT "$ENV{ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT}" STREQUAL "")
    set(ROR_SOURCE_COMMIT "$ENV{ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT}")
else ()
    set(ROR_SOURCE_COMMIT "${_ror_checked_out_source_commit}")
endif ()
if (DEFINED ENV{ROR_OGRE_NEXT_EXPECTED_ROR_REF} AND
        NOT "$ENV{ROR_OGRE_NEXT_EXPECTED_ROR_REF}" STREQUAL "")
    set(ROR_SOURCE_REF "$ENV{ROR_OGRE_NEXT_EXPECTED_ROR_REF}")
    set(_ror_git_ref_result 0)
else ()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${ROR_REPOSITORY_ROOT}"
                rev-parse --abbrev-ref HEAD
        RESULT_VARIABLE _ror_git_ref_result
        OUTPUT_VARIABLE ROR_SOURCE_REF
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif ()
string(LENGTH "${ROR_SOURCE_COMMIT}" _ror_source_commit_length)
if (NOT _ror_git_commit_result EQUAL 0 OR
        NOT _ror_source_commit_length EQUAL 40 OR
        NOT ROR_SOURCE_COMMIT MATCHES "^[0-9a-f]+$" OR
        NOT "${ROR_SOURCE_COMMIT}" STREQUAL
            "${_ror_checked_out_source_commit}" OR
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
    "cmake/RendererLauncherPackageConfig.cmake"
    "source/main/gfx/RendererBackendPolicy.cpp"
    "source/main/gfx/RendererBackendPolicy.h"
    "source/main/gfx/GfxScene.cpp"
    "source/main/gfx/GfxScene.h"
    "source/main/gfx/RendererStartupHandoff.cpp"
    "source/main/gfx/RendererStartupHandoff.h"
    "source/main/gfx/RendererStartupPlan.cpp"
    "source/main/gfx/RendererStartupPlan.h"
    "source/main/main.cpp"
    "source/main/system/RendererBridgeChannel.cpp"
    "source/main/system/RendererBridgeChannel.h"
    "source/main/system/RendererBridgeEndpoint.cpp"
    "source/main/system/RendererBridgeEndpoint.h"
    "source/main/system/RendererBridgeLaunchPlan.cpp"
    "source/main/system/RendererBridgeLaunchPlan.h"
    "source/main/system/RendererBridgeProcessSupervisor.cpp"
    "source/main/system/RendererBridgeProcessSupervisor.h"
    "source/main/system/RendererChildIntent.cpp"
    "source/main/system/RendererChildIntent.h"
    "source/main/system/RendererChildLauncher.cpp"
    "source/main/system/RendererChildLauncher.h"
    "source/main/system/RendererOgre14GameBridge.cpp"
    "source/main/system/RendererOgre14GameBridge.h"
    "source/main/system/RendererOgre14GameHostSession.cpp"
    "source/main/system/RendererOgre14GameHostSession.h"
    "source/main/system/RendererSiblingPath.cpp"
    "source/main/system/RendererSiblingPath.h"
    "source/main/system/RendererPackagedMediaPath.cpp"
    "source/main/system/RendererPackagedMediaPath.h"
    "source/main/system/RendererPackageRuntimeProbe.cpp"
    "source/main/system/RendererPackageRuntimeProbe.h"
    "source/main/system/RendererOgreNextChild.cpp"
    "source/main/system/RendererOgreNextChild.h"
    "source/main/system/RendererOgreNextChildMain.cpp"
    "source/main/system/RendererOgreNextLiveSession.cpp"
    "source/main/system/RendererOgreNextLiveSession.h"
    "source/main/system/RendererOgreNextProductionSession.cpp"
    "source/main/system/RendererOgreNextProductionSession.h"
    "source/main/system/RendererOgreNextSdlWindowRuntime.cpp"
    "source/main/system/RendererOgreNextSdlWindowRuntime.h"
    "source/main/system/RendererOgreNextSdlWindowRuntimeCocoa.mm"
    "source/main/system/RendererOgreNextWindowHost.cpp"
    "source/main/system/RendererOgreNextWindowHost.h"
    "source/main/system/RendererLauncherMain.cpp"
    "source/main/system/RendererLauncherPackageConfig.h.in"
    "source/main/system/RendererPublicLauncher.cpp"
    "source/main/system/RendererPublicLauncher.h"
    "tests/gfx/RendererBackendPolicyTests.cpp"
    "tests/gfx/RendererBridgeEndpointTests.cpp"
    "tests/gfx/RendererBridgeLaunchPlanTests.cpp"
    "tests/gfx/RendererBridgeChannelTests.cpp"
    "tests/gfx/RendererBridgeProcessFakeChild.cpp"
    "tests/gfx/RendererBridgeProcessSupervisorTests.cpp"
    "tests/gfx/RendererChildIntentTests.cpp"
    "tests/gfx/RendererChildLauncherFakeChild.cpp"
    "tests/gfx/RendererChildLauncherTests.cpp"
    "tests/gfx/RendererOgre14GameBridgeTests.cpp"
    "tests/gfx/RendererOgre14GameHostSessionTests.cpp"
    "tests/gfx/RendererSiblingPathTests.cpp"
    "tests/gfx/RendererPackageRuntimeProbeTests.cpp"
    "tests/gfx/RendererOgreNextChildTests.cpp"
    "tests/gfx/RendererOgreNextLiveSessionTests.cpp"
    "tests/gfx/RendererOgreNextWindowHostTests.cpp"
    "tests/gfx/RendererPublicLauncherLegacyChild.cpp"
    "tests/gfx/RendererPublicLauncherTests.cpp"
    "tests/cmake/VerifyRendererPublicBridgeExit.cmake"
    "tests/gfx/RendererStartupHandoffTests.cpp"
    "tests/gfx/RendererStartupPlanTests.cpp"
    "tests/gfx/render/RenderBridgeControlTransportTests.cpp"
    "tests/gfx/render/RendererFrontendTransportDispatcherTests.cpp"
    "tests/gfx/render/Ogre14GraphicsSceneSourceTests.cpp"
    "tests/tools/test_ogre_next_child_runtime_contract.py"
    "tests/tools/test_ogre_next_window_host_contract.py"
    "tests/tools/test_ogre_next_window_presentation_contract.py"
    "tests/tools/test_ogre_next_window_run_loop_contract.py"
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
set(ROR_OGRE_NEXT_N1_PACKAGE_FREETYPE_LICENSE_RELATIVE
    "${ROR_FREETYPE_PACKAGE_LICENSE_PATH}")
set(ROR_OGRE_NEXT_N1_PACKAGE_FREETYPE_OVERVIEW_RELATIVE
    "${ROR_FREETYPE_PACKAGE_OVERVIEW_PATH}")
set(ROR_OGRE_NEXT_N1_PACKAGE_SHADER_NOTICE_RELATIVE
    "licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt")
set(ROR_OGRE_NEXT_N1_PACKAGE_IBLBAKER_NOTICE_RELATIVE
    "licenses/IBLBaker.txt")
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

# The production HDR path consumes Ogre-Next's compositor/material programs in
# addition to HLMS.  Keep the complete compositor, Common, and HDR directories
# instead of a hand-picked backend subset: unified programs select Metal,
# HLSL, or GLSL/Vulkan at runtime and every packaged platform therefore
# carries the same byte-exact media closure.
set(_ror_n1_hdr_media_roots
    "2.0/scripts/Compositors"
    "2.0/scripts/materials/Common"
    "2.0/scripts/materials/HDR")
set(_ror_n1_hdr_media_files "")
foreach (_ror_n1_hdr_root IN LISTS _ror_n1_hdr_media_roots)
    file(GLOB_RECURSE _ror_n1_hdr_root_files
        LIST_DIRECTORIES false
        RELATIVE "${ROR_OGRE_NEXT_MEDIA_ROOT}"
        "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_n1_hdr_root}/*")
    list(APPEND _ror_n1_hdr_media_files ${_ror_n1_hdr_root_files})
endforeach ()
list(REMOVE_DUPLICATES _ror_n1_hdr_media_files)
list(SORT _ror_n1_hdr_media_files)
if (NOT _ror_n1_hdr_media_files)
    message(FATAL_ERROR "The pinned Ogre-Next HDR media manifest is empty")
endif ()
set(ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_ENTRIES "")
set(_ror_n1_hdr_media_manifest_serialized "")
foreach (_ror_n1_hdr_relative IN LISTS _ror_n1_hdr_media_files)
    set(_ror_n1_hdr_file
        "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_n1_hdr_relative}")
    if (IS_SYMLINK "${_ror_n1_hdr_file}")
        message(FATAL_ERROR
            "Pinned Ogre-Next HDR manifest contains a symbolic link: "
            "${_ror_n1_hdr_relative}")
    endif ()
    if (_ror_n1_hdr_relative MATCHES "[;\\\\\"]")
        message(FATAL_ERROR
            "Pinned Ogre-Next HDR path cannot be represented exactly: "
            "${_ror_n1_hdr_relative}")
    endif ()
    file(SIZE "${_ror_n1_hdr_file}" _ror_n1_hdr_size)
    file(SHA256 "${_ror_n1_hdr_file}" _ror_n1_hdr_sha256)
    string(APPEND ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_ENTRIES
        "    {\"${_ror_n1_hdr_relative}\", "
        "static_cast<std::uint64_t>(${_ror_n1_hdr_size}), "
        "\"${_ror_n1_hdr_sha256}\"},\n")
    string(APPEND _ror_n1_hdr_media_manifest_serialized
        "${_ror_n1_hdr_relative}|${_ror_n1_hdr_size}|"
        "${_ror_n1_hdr_sha256}\n")
endforeach ()
list(LENGTH _ror_n1_hdr_media_files
    ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_COUNT)
list(GET _ror_n1_hdr_media_files 0
    ROR_OGRE_NEXT_N1_HDR_MEDIA_TAMPER_RELATIVE)
string(SHA256 ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_SHA256
    "${_ror_n1_hdr_media_manifest_serialized}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend_n1_media_manifest.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_n1_media_manifest.h"
    @ONLY)

# Native presentation registers a deliberately smaller resource group than
# HDR. Authenticate and stage only the fullscreen-quad and 4xFP32 copy closure
# selected by the reviewed lock; unrelated Common programs must not enter this
# resource group.
set(ROR_OGRE_NEXT_PRESENTATION_COPY_LOCK
    "${CMAKE_CURRENT_SOURCE_DIR}/ogre-next-presentation-copy-v1.lock.json")
file(SHA256 "${ROR_OGRE_NEXT_PRESENTATION_COPY_LOCK}"
    ROR_OGRE_NEXT_PRESENTATION_COPY_LOCK_SHA256)
file(READ "${ROR_OGRE_NEXT_PRESENTATION_COPY_LOCK}"
    _ror_presentation_copy_lock_json)
string(JSON _ror_presentation_copy_schema GET
    "${_ror_presentation_copy_lock_json}" schema)
string(JSON _ror_presentation_copy_ogre_commit GET
    "${_ror_presentation_copy_lock_json}" ogre_next_commit)
string(JSON _ror_presentation_copy_material GET
    "${_ror_presentation_copy_lock_json}" material)
string(JSON _ror_presentation_copy_file_count LENGTH
    "${_ror_presentation_copy_lock_json}" files)
if (NOT _ror_presentation_copy_schema STREQUAL
        "ror.ogre_next.presentation_copy_media.v1" OR
    NOT _ror_presentation_copy_ogre_commit STREQUAL ROR_OGRE_NEXT_COMMIT OR
    NOT _ror_presentation_copy_material STREQUAL "Ogre/Copy/4xFP32" OR
    NOT _ror_presentation_copy_file_count EQUAL 8)
    message(FATAL_ERROR "The Ogre-Next presentation copy lock changed")
endif ()
set(ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/presentation-media")
set(_ror_presentation_copy_stage
    "${ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT}/CommonCopy")
file(MAKE_DIRECTORY
    "${_ror_presentation_copy_stage}/GLSL"
    "${_ror_presentation_copy_stage}/HLSL"
    "${_ror_presentation_copy_stage}/Metal")
set(_ror_presentation_staged_files "")
math(EXPR _ror_presentation_copy_last
    "${_ror_presentation_copy_file_count} - 1")
foreach (_ror_presentation_copy_index RANGE 0 ${_ror_presentation_copy_last})
    string(JSON _ror_presentation_copy_path GET
        "${_ror_presentation_copy_lock_json}" files
        ${_ror_presentation_copy_index} path)
    string(JSON _ror_presentation_copy_size GET
        "${_ror_presentation_copy_lock_json}" files
        ${_ror_presentation_copy_index} size)
    string(JSON _ror_presentation_copy_sha256 GET
        "${_ror_presentation_copy_lock_json}" files
        ${_ror_presentation_copy_index} sha256)
    if (_ror_presentation_copy_path MATCHES
            "^tools/ogre_next_probe/presentation_media/CommonCopy/([^/]+)$")
        set(_ror_presentation_copy_source
            "${ROR_REPOSITORY_ROOT}/${_ror_presentation_copy_path}")
        set(_ror_presentation_copy_relative
            "CommonCopy/${CMAKE_MATCH_1}")
    elseif (_ror_presentation_copy_path MATCHES
            "^Samples/Media/2\\.0/scripts/materials/Common/(GLSL|HLSL|Metal)/([^/]+)$")
        set(_ror_presentation_copy_source
            "${ogre_next_SOURCE_DIR}/${_ror_presentation_copy_path}")
        set(_ror_presentation_copy_relative
            "CommonCopy/${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
    else ()
        message(FATAL_ERROR
            "Presentation copy lock contains an unreviewed path: "
            "${_ror_presentation_copy_path}")
    endif ()
    if (IS_SYMLINK "${_ror_presentation_copy_source}")
        message(FATAL_ERROR
            "Presentation copy source is an unreviewed symbolic link: "
            "${_ror_presentation_copy_path}")
    endif ()
    file(SIZE "${_ror_presentation_copy_source}"
        _ror_presentation_copy_observed_size)
    file(SHA256 "${_ror_presentation_copy_source}"
        _ror_presentation_copy_observed_sha256)
    if (NOT _ror_presentation_copy_observed_size EQUAL
            _ror_presentation_copy_size OR
        NOT _ror_presentation_copy_observed_sha256 STREQUAL
            _ror_presentation_copy_sha256)
        message(FATAL_ERROR
            "Presentation copy source bytes changed: "
            "${_ror_presentation_copy_path}")
    endif ()
    set(_ror_presentation_copy_destination
        "${ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT}/${_ror_presentation_copy_relative}")
    configure_file(
        "${_ror_presentation_copy_source}"
        "${_ror_presentation_copy_destination}"
        COPYONLY)
    list(APPEND _ror_presentation_staged_files
        "${_ror_presentation_copy_relative}")
endforeach ()
list(SORT _ror_presentation_staged_files)
set(ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_ENTRIES "")
set(_ror_presentation_media_manifest_serialized "")
foreach (_ror_presentation_relative IN LISTS _ror_presentation_staged_files)
    set(_ror_presentation_file
        "${ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT}/${_ror_presentation_relative}")
    file(SIZE "${_ror_presentation_file}" _ror_presentation_size)
    file(SHA256 "${_ror_presentation_file}" _ror_presentation_sha256)
    string(APPEND ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_ENTRIES
        "    {\"${_ror_presentation_relative}\", "
        "static_cast<std::uint64_t>(${_ror_presentation_size}), "
        "\"${_ror_presentation_sha256}\"},\n")
    string(APPEND _ror_presentation_media_manifest_serialized
        "${_ror_presentation_relative}|${_ror_presentation_size}|"
        "${_ror_presentation_sha256}\n")
endforeach ()
list(LENGTH _ror_presentation_staged_files
    ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_COUNT)
string(SHA256 ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_SHA256
    "${_ror_presentation_media_manifest_serialized}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend_presentation_media_manifest.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_presentation_media_manifest.h"
    @ONLY)
set(_ror_reflection_media_files
    "2.0/scripts/materials/Common/Any/PccDepthCompressor_ps.any"
    "2.0/scripts/materials/Common/GLSL/PccDepthCompressor_ps.glsl"
    "2.0/scripts/materials/Common/GLSL/QuadCameraDirNoUV_vs.glsl"
    "2.0/scripts/materials/Common/GLSL/QuadCameraDir_vs.glsl"
    "2.0/scripts/materials/Common/GLSL/Quad_vs.glsl"
    "2.0/scripts/materials/Common/HLSL/PccDepthCompressor_ps.hlsl"
    "2.0/scripts/materials/Common/HLSL/QuadCameraDirNoUV_vs.hlsl"
    "2.0/scripts/materials/Common/HLSL/QuadCameraDir_vs.hlsl"
    "2.0/scripts/materials/Common/HLSL/Quad_vs.hlsl"
    "2.0/scripts/materials/Common/Metal/PccDepthCompressor_ps.metal"
    "2.0/scripts/materials/Common/Metal/QuadCameraDirNoUV_vs.metal"
    "2.0/scripts/materials/Common/Metal/QuadCameraDir_vs.metal"
    "2.0/scripts/materials/Common/Metal/Quad_vs.metal"
    "2.0/scripts/materials/Common/PccDepthCompressor.material"
    "2.0/scripts/materials/Common/Quad.program"
    "2.0/scripts/materials/LocalCubemaps/BlendProjectCubemap.material"
    "2.0/scripts/materials/LocalCubemaps/CopyCubemap.material"
    "2.0/scripts/materials/LocalCubemaps/GLSL/BlendProjectCubemap_ps.glsl"
    "2.0/scripts/materials/LocalCubemaps/GLSL/BlendProjectCubemap_vs.glsl"
    "2.0/scripts/materials/LocalCubemaps/GLSL/CopyCubemap_ps.glsl"
    "2.0/scripts/materials/LocalCubemaps/HLSL/BlendProjectCubemap_ps.hlsl"
    "2.0/scripts/materials/LocalCubemaps/HLSL/BlendProjectCubemap_vs.hlsl"
    "2.0/scripts/materials/LocalCubemaps/HLSL/CopyCubemap_ps.hlsl"
    "2.0/scripts/materials/LocalCubemaps/Metal/BlendProjectCubemap_ps.metal"
    "2.0/scripts/materials/LocalCubemaps/Metal/BlendProjectCubemap_vs.metal"
    "2.0/scripts/materials/LocalCubemaps/Metal/CopyCubemap_ps.metal"
    "Compute/Algorithms/IBL/IBL.material.json"
    "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.glsl"
    "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.hlsl"
    "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.metal"
    "Compute/Algorithms/IBL/SpecularIblIntegrator_piece_cs.any"
    "Compute/Tools/Any/sRGB.any")
list(SORT _ror_reflection_media_files)
set(ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_ENTRIES "")
set(_ror_reflection_media_manifest_serialized "")
foreach (_ror_reflection_media_relative IN LISTS _ror_reflection_media_files)
    set(_ror_reflection_media_file
        "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_reflection_media_relative}")
    if (NOT EXISTS "${_ror_reflection_media_file}" OR
            IS_DIRECTORY "${_ror_reflection_media_file}" OR
            IS_SYMLINK "${_ror_reflection_media_file}")
        message(FATAL_ERROR
            "Pinned Ogre-Next reflection media is missing or indirect: "
            "${_ror_reflection_media_relative}")
    endif ()
    if (_ror_reflection_media_relative MATCHES "[;\\\\\"]")
        message(FATAL_ERROR
            "Pinned Ogre-Next reflection media path cannot be represented exactly: "
            "${_ror_reflection_media_relative}")
    endif ()
    file(SIZE "${_ror_reflection_media_file}" _ror_reflection_media_size)
    file(SHA256 "${_ror_reflection_media_file}" _ror_reflection_media_sha256)
    string(APPEND ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_ENTRIES
        "    {\"${_ror_reflection_media_relative}\", "
        "static_cast<std::uint64_t>(${_ror_reflection_media_size}), "
        "\"${_ror_reflection_media_sha256}\"},\n")
    string(APPEND _ror_reflection_media_manifest_serialized
        "${_ror_reflection_media_relative}|${_ror_reflection_media_size}|"
        "${_ror_reflection_media_sha256}\n")
endforeach ()
list(LENGTH _ror_reflection_media_files
    ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_COUNT)
set(ROR_OGRE_NEXT_REFLECTION_MEDIA_TAMPER_RELATIVE
    "Compute/Algorithms/IBL/SpecularIblIntegrator_piece_cs.any")
string(SHA256 ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_SHA256
    "${_ror_reflection_media_manifest_serialized}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend_reflection_media_manifest.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_reflection_media_manifest.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/frame_probe_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_frame_probe_config.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/frontend_n1_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_n1_config.h"
    @ONLY)
file(REAL_PATH "${ROR_OGRE_NEXT_MEDIA_ROOT}"
    _ror_ogre_next_child_media_real)
file(TO_CMAKE_PATH "${_ror_ogre_next_child_media_real}"
    ROR_OGRE_NEXT_CHILD_SHADER_MEDIA_ROOT)
if (NOT IS_ABSOLUTE "${ROR_OGRE_NEXT_CHILD_SHADER_MEDIA_ROOT}" OR
        NOT IS_DIRECTORY "${ROR_OGRE_NEXT_CHILD_SHADER_MEDIA_ROOT}" OR
        ROR_OGRE_NEXT_CHILD_SHADER_MEDIA_ROOT MATCHES "[;\"]")
    message(FATAL_ERROR
        "The probe-only Ogre-Next child requires one representable absolute pinned media root")
endif ()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer_ogre_next_child_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/renderer_ogre_next_child_config.generated.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan_rt5_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_vulkan_rt5_config.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/vulkan_rt6_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_vulkan_rt6_config.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ogre_next_build_contract.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-build-contract.json"
    @ONLY)

add_custom_target(
    ror_renderer_ogre_next_child_process_closure_verify
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/verify_child_runtime_process_closure.py"
        --ror-root "${ROR_REPOSITORY_ROOT}"
        --ogre-root "${ogre_next_SOURCE_DIR}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/verify_child_runtime_process_closure.py"
    COMMENT "Proving the probe-only Ogre-Next child has no descendant-process source calls"
    VERBATIM)

add_custom_target(
    ror_ogre_next_pssm_source_closure_verify
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/verify_pssm_shadow_source_closure.py"
        --lock
        "${CMAKE_CURRENT_SOURCE_DIR}/ogre-next-pssm-shadow-v1.lock.json"
        --canonical-lock
        "${CMAKE_CURRENT_SOURCE_DIR}/ogre-next.lock.json"
        --source-root "${ogre_next_SOURCE_DIR}"
        --platform-policy "${ROR_OGRE_NEXT_PLATFORM_POLICY}"
    COMMENT "Verifying the exact pinned Ogre-Next directional-shadow source closure"
    VERBATIM)

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
option(
    ROR_OGRE_NEXT_VULKAN_RT5
    "Expose the opt-in Linux Vulkan external-instance/device RT5 proof target"
    ON)
option(
    ROR_OGRE_NEXT_VULKAN_RT6
    "Expose the opt-in Linux Vulkan KHR ray-dispatch RT6 proof target"
    ON)
option(
    ROR_OGRE_NEXT_WINDOWS_DXR7
    "Expose the opt-in Windows D3D11On12/DXR RT7 proof target"
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
        "${_ror_render_root}/GraphicsSceneSnapshotProducer.cpp"
        "${_ror_render_root}/HdrReference.cpp"
        "${_ror_render_root}/InputEventTransport.cpp"
        "${_ror_render_root}/MaterialDescriptor.cpp"
        "${_ror_render_root}/Ogre14GraphicsSceneSource.cpp"
        "${_ror_render_root}/ReflectionProbeCaptureReceipt.cpp"
        "${_ror_render_root}/ReflectionProbeRuntime.cpp"
        "${_ror_render_root}/RenderAssetDeltaTransport.cpp"
        "${_ror_render_root}/RenderAssetId.cpp"
        "${_ror_render_root}/RenderAssetRegistry.cpp"
        "${_ror_render_root}/RenderBridgeControlTransport.cpp"
        "${_ror_render_root}/RenderBridgeSessionIdentity.cpp"
        "${_ror_render_root}/RenderFrame.cpp"
        "${_ror_render_root}/RenderResourceDescriptors.cpp"
        "${_ror_render_root}/RenderTransportEnvelope.cpp"
        "${_ror_render_root}/RenderTransportStream.cpp"
        "${_ror_render_root}/RendererFrontend.cpp"
        "${_ror_render_root}/RendererFrontendTransportDispatcher.cpp"
        "${_ror_render_root}/ResourceHandle.cpp"
        "${_ror_render_root}/SceneSnapshot.cpp"
        "${_ror_render_root}/SceneSnapshotTransport.cpp")
    target_include_directories(
        ror_ogre_next_n1_contract
        PUBLIC "${_ror_render_root}")

    set(_ror_ogre_next_frontend_n1_sources
        "${_ror_render_root}/ogrenext/NativeDirectionalShadowContract.cpp"
        "${_ror_render_root}/ogrenext/OgreNextHdrTemporalContract.cpp"
        "${_ror_render_root}/ogrenext/OgreNextN1Frontend.cpp"
        "${_ror_render_root}/ogrenext/OgreNextN1MediaIntegrity.cpp"
        "${_ror_render_root}/ogrenext/OgreNextN1Policy.cpp"
        "${_ror_render_root}/ogrenext/OgreNextReflectionProbeRuntime.cpp"
        "${_ror_render_root}/ogrenext/OgreNextPssmShadowPolicy.cpp")
    add_library(
        ror_ogre_next_frontend_n1 STATIC
        ${_ror_ogre_next_frontend_n1_sources})
    if (APPLE)
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_METAL=1)
        set(_ror_ogre_next_frontend_n1_platform_sources
            "${_ror_render_root}/ogrenext/OgreNextN2InteropState.cpp"
            "${_ror_render_root}/ogrenext/OgreNextMetalInterop.mm"
            "${_ror_render_root}/ogrenext/OgreNextMetalRayTracingBackend.mm")
        target_sources(
            ror_ogre_next_frontend_n1
            PRIVATE ${_ror_ogre_next_frontend_n1_platform_sources})
        target_compile_options(
            ror_ogre_next_frontend_n1
            PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        target_compile_definitions(
            ror_ogre_next_frontend_n1
            PRIVATE ROR_OGRE_NEXT_N2_TEST_SEAM=1)
    elseif (WIN32)
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_D3D11=1)
        set(_ror_ogre_next_frontend_n1_platform_sources "")
    else ()
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_VULKAN=1)
        set(_ror_ogre_next_frontend_n1_platform_sources "")
    endif ()

    # Keep the established fault-injection aggregate and staged N1 smoke
    # unchanged. The real child links a second build of the exact sources with
    # neither test seam defined anywhere in its compile closure.
    target_compile_definitions(
        ror_ogre_next_frontend_n1
        PUBLIC ROR_OGRE_NEXT_N1_TEXTURE_TEST_SEAM=1
        PRIVATE ${_ror_n1_renderer_definition})
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
            "${ogre_next_SOURCE_DIR}/Components/Hlms/Unlit/include"
            "${ogre_next_SOURCE_DIR}/Components/Overlay/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include")
    target_link_libraries(
        ror_ogre_next_frontend_n1
        PUBLIC ror_ogre_next_n1_contract
        PRIVATE
            OgreNextHlmsPbs
            OgreNextHlmsUnlit
            OgreNextOverlay
            ${ROR_OGRE_NEXT_RENDERER_TARGET}
            OgreNextMain)
    set_target_properties(
        ror_ogre_next_frontend_n1
        PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES)
    add_dependencies(
        ror_ogre_next_frontend_n1
        ror_ogre_next_pssm_source_closure_verify)

    add_library(
        ror_ogre_next_frontend_n1_runtime STATIC
        ${_ror_ogre_next_frontend_n1_sources})
    if (APPLE)
        target_sources(
            ror_ogre_next_frontend_n1_runtime
            PRIVATE ${_ror_ogre_next_frontend_n1_platform_sources})
        target_compile_options(
            ror_ogre_next_frontend_n1_runtime
            PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    endif ()
    target_compile_definitions(
        ror_ogre_next_frontend_n1_runtime
        PRIVATE ${_ror_n1_renderer_definition})
    target_include_directories(
        ror_ogre_next_frontend_n1_runtime
        PUBLIC "${_ror_render_root}/ogrenext"
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_include_directories(
        ror_ogre_next_frontend_n1_runtime
        SYSTEM PRIVATE
            "${ogre_next_SOURCE_DIR}/OgreMain/include"
            "${ogre_next_BINARY_DIR}/include"
            "${ogre_next_SOURCE_DIR}/Components/Hlms/Common/include"
            "${ogre_next_SOURCE_DIR}/Components/Hlms/Pbs/include"
            "${ogre_next_SOURCE_DIR}/Components/Hlms/Unlit/include"
            "${ogre_next_SOURCE_DIR}/Components/Overlay/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/include"
            "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include")
    target_link_libraries(
        ror_ogre_next_frontend_n1_runtime
        PUBLIC ror_ogre_next_n1_contract
        PRIVATE
            OgreNextHlmsPbs
            OgreNextHlmsUnlit
            OgreNextOverlay
            ${ROR_OGRE_NEXT_RENDERER_TARGET}
            OgreNextMain)
    set_target_properties(
        ror_ogre_next_frontend_n1_runtime
        PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES)
    add_dependencies(
        ror_ogre_next_frontend_n1_runtime
        ror_ogre_next_pssm_source_closure_verify)

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

    add_executable(
        ror_ogre_next_pssm_shadow_smoke
        src/pssm_shadow_smoke.cpp)
    target_include_directories(
        ror_ogre_next_pssm_shadow_smoke
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(
        ror_ogre_next_pssm_shadow_smoke
        PRIVATE ror_ogre_next_frontend_n1)
    set_target_properties(
        ror_ogre_next_pssm_shadow_smoke
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_executable(
        ror_renderer_backend_policy_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererBackendPolicyTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp")
    target_include_directories(
        ror_renderer_backend_policy_tests
        PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx")
    set_target_properties(
        ror_renderer_backend_policy_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_startup_plan_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererStartupPlanTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp")
    target_include_directories(
        ror_renderer_startup_plan_tests
        PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx")
    set_target_properties(
        ror_renderer_startup_plan_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_startup_handoff_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererStartupHandoffTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp")
    target_include_directories(
        ror_renderer_startup_handoff_tests
        PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/gfx")
    set_target_properties(
        ror_renderer_startup_handoff_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_child_intent_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererChildIntentTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp")
    target_include_directories(
        ror_renderer_child_intent_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")
    set_target_properties(
        ror_renderer_child_intent_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_bridge_channel_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererBridgeChannelTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeChannel.cpp")
    target_include_directories(
        ror_renderer_bridge_channel_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")
    target_link_libraries(
        ror_renderer_bridge_channel_tests
        PRIVATE Threads::Threads)
    set_target_properties(
        ror_renderer_bridge_channel_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_ogre14_game_bridge_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererOgre14GameBridgeTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeChannel.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgre14GameBridge.cpp")
    target_include_directories(
        ror_renderer_ogre14_game_bridge_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")
    target_link_libraries(
        ror_renderer_ogre14_game_bridge_tests
        PRIVATE Threads::Threads)
    set_target_properties(
        ror_renderer_ogre14_game_bridge_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_ogre14_game_host_session_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererOgre14GameHostSessionTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeChannel.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgre14GameBridge.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgre14GameHostSession.cpp")
    target_include_directories(
        ror_renderer_ogre14_game_host_session_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${_ror_render_root}"
            "${ROR_REPOSITORY_ROOT}/source/main/system")
    target_link_libraries(
        ror_renderer_ogre14_game_host_session_tests
        PRIVATE ror_ogre_next_n1_contract Threads::Threads)
    set_target_properties(
        ror_renderer_ogre14_game_host_session_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_child_launcher_fake_child
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererChildLauncherFakeChild.cpp")
    set_target_properties(
        ror_renderer_child_launcher_fake_child
        PROPERTIES
            OUTPUT_NAME "RoR-OgreNext"
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/renderer-child-launcher-tests")

    add_executable(
        ror_renderer_ogre_next_child_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererOgreNextChildTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextChild.cpp")
    target_include_directories(
        ror_renderer_ogre_next_child_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")
    set_target_properties(
        ror_renderer_ogre_next_child_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_ogre_next_live_session_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererOgreNextLiveSessionTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeChannel.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextLiveSession.cpp")
    target_include_directories(
        ror_renderer_ogre_next_live_session_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")
    target_link_libraries(
        ror_renderer_ogre_next_live_session_tests
        PRIVATE ror_ogre_next_n1_contract Threads::Threads)
    set_target_properties(
        ror_renderer_ogre_next_live_session_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_frontend_transport_dispatcher_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/render/RendererFrontendTransportDispatcherTests.cpp")
    target_link_libraries(
        ror_renderer_frontend_transport_dispatcher_tests
        PRIVATE ror_ogre_next_n1_contract)
    set_target_properties(
        ror_renderer_frontend_transport_dispatcher_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_ogre14_graphics_scene_source_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/render/Ogre14GraphicsSceneSourceTests.cpp")
    target_link_libraries(
        ror_ogre14_graphics_scene_source_tests
        PRIVATE ror_ogre_next_n1_contract)
    set_target_properties(
        ror_ogre14_graphics_scene_source_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_render_bridge_control_transport_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/render/RenderBridgeControlTransportTests.cpp")
    target_link_libraries(
        ror_render_bridge_control_transport_tests
        PRIVATE ror_ogre_next_n1_contract)
    set_target_properties(
        ror_render_bridge_control_transport_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_executable(
        ror_renderer_ogre_next_window_host_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererOgreNextWindowHostTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextWindowHost.cpp")
    target_include_directories(
        ror_renderer_ogre_next_window_host_tests
        PRIVATE "${ROR_REPOSITORY_ROOT}/source/main/system")
    target_link_libraries(
        ror_renderer_ogre_next_window_host_tests
        PRIVATE Threads::Threads)
    set_target_properties(
        ror_renderer_ogre_next_window_host_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_library(
        ror_renderer_ogre_next_sdl_window_runtime STATIC
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextWindowHost.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextSdlWindowRuntime.cpp")
    if (APPLE)
        target_sources(
            ror_renderer_ogre_next_sdl_window_runtime
            PRIVATE
                "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextSdlWindowRuntimeCocoa.mm")
        target_compile_options(
            ror_renderer_ogre_next_sdl_window_runtime
            PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        target_include_directories(
            ror_renderer_ogre_next_sdl_window_runtime
            SYSTEM PRIVATE
                "${ogre_next_SOURCE_DIR}/OgreMain/include"
                "${ogre_next_BINARY_DIR}/include"
                "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
                "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX")
        target_link_libraries(
            ror_renderer_ogre_next_sdl_window_runtime
            PRIVATE ${ROR_OGRE_NEXT_RENDERER_TARGET})
    endif ()
    target_include_directories(
        ror_renderer_ogre_next_sdl_window_runtime
        PUBLIC "${ROR_REPOSITORY_ROOT}/source/main/system")
    target_link_libraries(
        ror_renderer_ogre_next_sdl_window_runtime
        PUBLIC SDL2::SDL2-static Threads::Threads)
    set_target_properties(
        ror_renderer_ogre_next_sdl_window_runtime
        PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES)

    add_executable(
        ror_renderer_ogre_next_window_host_smoke
        src/window_host_smoke.cpp)
    target_link_libraries(
        ror_renderer_ogre_next_window_host_smoke
        PRIVATE ror_renderer_ogre_next_sdl_window_runtime)
    set_target_properties(
        ror_renderer_ogre_next_window_host_smoke
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    set(ROR_OGRE_NEXT_WINDOW_PRESENT_IMAGE
        "${CMAKE_BINARY_DIR}/ogre-next-window-present.ppm")
    set(ROR_OGRE_NEXT_WINDOW_PRESENT_REPORT
        "${CMAKE_BINARY_DIR}/ogre-next-window-present.json")
    add_executable(
        ror_ogre_next_window_present_smoke
        src/window_present_smoke.cpp)
    target_include_directories(
        ror_ogre_next_window_present_smoke
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(
        ror_ogre_next_window_present_smoke
        PRIVATE
            ror_ogre_next_frontend_n1
            ror_renderer_ogre_next_sdl_window_runtime)
    set_target_properties(
        ror_ogre_next_window_present_smoke
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    set(ROR_OGRE_NEXT_WINDOW_RUN_LOOP_IMAGE
        "${CMAKE_BINARY_DIR}/ogre-next-window-run-loop.ppm")
    set(ROR_OGRE_NEXT_WINDOW_RUN_LOOP_REPORT
        "${CMAKE_BINARY_DIR}/ogre-next-window-run-loop.json")
    add_executable(
        ror_ogre_next_window_run_loop_smoke
        src/window_run_loop_smoke.cpp)
    target_include_directories(
        ror_ogre_next_window_run_loop_smoke
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(
        ror_ogre_next_window_run_loop_smoke
        PRIVATE
            ror_ogre_next_frontend_n1
            ror_renderer_ogre_next_sdl_window_runtime)
    set_target_properties(
        ror_ogre_next_window_run_loop_smoke
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Real native entrypoint and frontend bootstrap, still confined to this
    # probe project. It is intentionally absent from all package/install and
    # product source/main CMake paths until presentation and the game bridge
    # pass their separate admission gates.
    set(_ror_renderer_ogre_next_child_runtime_output
        "${CMAKE_BINARY_DIR}/renderer-ogre-next-child-runtime")
    add_executable(
        ror_renderer_ogre_next_child_runtime
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextChildMain.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeChannel.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererSiblingPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPackagedMediaPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextLiveSession.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextProductionSession.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererOgreNextChild.cpp")
    target_include_directories(
        ror_renderer_ogre_next_child_runtime
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system"
            "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(
        ror_renderer_ogre_next_child_runtime
        PRIVATE
            ror_ogre_next_frontend_n1_runtime
            ror_renderer_ogre_next_sdl_window_runtime)
    set_target_properties(
        ror_renderer_ogre_next_child_runtime
        PROPERTIES
            OUTPUT_NAME "RoR-OgreNext"
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_ogre_next_child_runtime_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_ogre_next_child_runtime_output}/Debug"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_ogre_next_child_runtime_output}/Release"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_ogre_next_child_runtime_output}/RelWithDebInfo"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_ogre_next_child_runtime_output}/MinSizeRel")
    if (WIN32)
        set_target_properties(
            ror_renderer_ogre_next_child_runtime
            PROPERTIES WIN32_EXECUTABLE YES)
        target_compile_definitions(
            ror_renderer_ogre_next_child_runtime
            PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
        target_link_libraries(
            ror_renderer_ogre_next_child_runtime PRIVATE Shell32)
    endif ()
    if (MINGW)
        target_link_options(
            ror_renderer_ogre_next_child_runtime PRIVATE -municode)
    endif ()
    add_dependencies(
        ror_renderer_ogre_next_child_runtime
        ror_renderer_ogre_next_child_process_closure_verify)

    add_executable(
        ror_renderer_sibling_path_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererSiblingPathTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererSiblingPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPackagedMediaPath.cpp")
    target_include_directories(
        ror_renderer_sibling_path_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")

    add_executable(
        ror_renderer_package_runtime_probe_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererPackageRuntimeProbeTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererSiblingPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPackagedMediaPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPackageRuntimeProbe.cpp")
    target_include_directories(
        ror_renderer_package_runtime_probe_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")

    set(_ror_renderer_bridge_process_test_output
        "${CMAKE_BINARY_DIR}/renderer-bridge-process-tests")
    set(_ror_renderer_bridge_process_fake_common_sources
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererBridgeProcessFakeChild.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${_ror_render_root}/RenderTransportEnvelope.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp")
    add_executable(
        ror_renderer_bridge_process_fake_game
        ${_ror_renderer_bridge_process_fake_common_sources})
    target_compile_definitions(
        ror_renderer_bridge_process_fake_game
        PRIVATE ROR_RENDERER_BRIDGE_FAKE_GAME=1)
    add_executable(
        ror_renderer_bridge_process_fake_presentation
        ${_ror_renderer_bridge_process_fake_common_sources})
    target_compile_definitions(
        ror_renderer_bridge_process_fake_presentation
        PRIVATE ROR_RENDERER_BRIDGE_FAKE_PRESENTATION=1)
    foreach (_ror_bridge_fake_target IN ITEMS
        ror_renderer_bridge_process_fake_game
        ror_renderer_bridge_process_fake_presentation)
        target_include_directories(
            ${_ror_bridge_fake_target}
            PRIVATE
                "${ROR_REPOSITORY_ROOT}/source/main/gfx"
                "${_ror_render_root}"
                "${ROR_REPOSITORY_ROOT}/source/main/system")
    endforeach ()
    set_target_properties(
        ror_renderer_bridge_process_fake_game
        PROPERTIES
            OUTPUT_NAME "RoR-Ogre14"
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_bridge_process_test_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_bridge_process_test_output}/Debug"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_bridge_process_test_output}/Release"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_bridge_process_test_output}/RelWithDebInfo"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_bridge_process_test_output}/MinSizeRel")
    set_target_properties(
        ror_renderer_bridge_process_fake_presentation
        PROPERTIES
            OUTPUT_NAME "RoR-OgreNext"
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_bridge_process_test_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_bridge_process_test_output}/Debug"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_bridge_process_test_output}/Release"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_bridge_process_test_output}/RelWithDebInfo"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_bridge_process_test_output}/MinSizeRel")

    add_executable(
        ror_renderer_bridge_process_supervisor_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererBridgeProcessSupervisorTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeLaunchPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererSiblingPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeProcessSupervisor.cpp")
    target_include_directories(
        ror_renderer_bridge_process_supervisor_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")
    set_target_properties(
        ror_renderer_bridge_process_supervisor_tests
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_bridge_process_test_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_bridge_process_test_output}/Debug"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_bridge_process_test_output}/Release"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_bridge_process_test_output}/RelWithDebInfo"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_bridge_process_test_output}/MinSizeRel")
    add_dependencies(
        ror_renderer_bridge_process_supervisor_tests
        ror_renderer_bridge_process_fake_game
        ror_renderer_bridge_process_fake_presentation)

    add_executable(
        ror_renderer_child_launcher_tests
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererChildLauncherTests.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildLauncher.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererSiblingPath.cpp")
    target_include_directories(
        ror_renderer_child_launcher_tests
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system")
    set_target_properties(
        ror_renderer_child_launcher_tests
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/renderer-child-launcher-tests")
    add_dependencies(
        ror_renderer_child_launcher_tests
        ror_renderer_child_launcher_fake_child)
    if (MINGW)
        target_link_options(
            ror_renderer_child_launcher_fake_child
            PRIVATE -municode)
        target_link_options(
            ror_renderer_child_launcher_tests
            PRIVATE -municode)
        target_link_options(
            ror_renderer_bridge_process_fake_game
            PRIVATE -municode)
        target_link_options(
            ror_renderer_bridge_process_fake_presentation
            PRIVATE -municode)
        target_link_options(
            ror_renderer_bridge_process_supervisor_tests
            PRIVATE -municode)
    endif ()

    include(
        "${ROR_REPOSITORY_ROOT}/cmake/RendererLauncherPackageConfig.cmake")
    set(_ror_renderer_public_launcher_generated_include_dir
        "${CMAKE_CURRENT_BINARY_DIR}/renderer-public-launcher-generated")
    ror_configure_renderer_launcher_package_facts(
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererLauncherPackageConfig.h.in"
        "${_ror_renderer_public_launcher_generated_include_dir}"
        _ror_renderer_public_launcher_generated_header)

    # Real public entrypoint plus an exact legacy sibling, isolated from the
    # N1 package tree. This exercises POSIX main or Windows Unicode wWinMain
    # and verifies that the default launcher consumes no game arguments.
    set(_ror_renderer_public_entrypoint_test_output
        "${CMAKE_BINARY_DIR}/renderer-public-entrypoint-tests")
    add_executable(
        ror_renderer_public_launcher_legacy_child
        "${ROR_REPOSITORY_ROOT}/tests/gfx/RendererPublicLauncherLegacyChild.cpp")
    set_target_properties(
        ror_renderer_public_launcher_legacy_child
        PROPERTIES
            OUTPUT_NAME "RoR-Ogre14"
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_public_entrypoint_test_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_public_entrypoint_test_output}/Debug"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_public_entrypoint_test_output}/Release"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_public_entrypoint_test_output}/RelWithDebInfo"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_public_entrypoint_test_output}/MinSizeRel")

    add_executable(
        ror_renderer_public_launcher_entrypoint
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererLauncherMain.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeLaunchPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeProcessSupervisor.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildLauncher.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPackageRuntimeProbe.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPackagedMediaPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererSiblingPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPublicLauncher.cpp"
        "${_ror_renderer_public_launcher_generated_header}")
    target_include_directories(
        ror_renderer_public_launcher_entrypoint
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system"
            "${_ror_renderer_public_launcher_generated_include_dir}")
    set_target_properties(
        ror_renderer_public_launcher_entrypoint
        PROPERTIES
            OUTPUT_NAME "RoR"
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_public_entrypoint_test_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_public_entrypoint_test_output}/Debug"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_public_entrypoint_test_output}/Release"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_public_entrypoint_test_output}/RelWithDebInfo"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_public_entrypoint_test_output}/MinSizeRel")
    add_dependencies(
        ror_renderer_public_launcher_entrypoint
        ror_renderer_public_launcher_legacy_child)
    if (WIN32)
        set_target_properties(
            ror_renderer_public_launcher_entrypoint
            PROPERTIES WIN32_EXECUTABLE YES)
        target_compile_definitions(
            ror_renderer_public_launcher_entrypoint
            PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
        target_link_libraries(
            ror_renderer_public_launcher_entrypoint
            PRIVATE Shell32)
    endif ()
    if (MINGW)
        target_link_options(
            ror_renderer_public_launcher_legacy_child
            PRIVATE -municode)
        target_link_options(
            ror_renderer_public_launcher_entrypoint
            PRIVATE -municode)
    endif ()

    # Real two-process public-launcher path with test-only admitted facts. The
    # production package generator above remains immutable and fail-closed.
    set(_ror_renderer_public_bridge_generated_include_dir
        "${CMAKE_BINARY_DIR}/renderer-public-bridge-generated")
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(ROR_RENDERER_LAUNCHER_PACKAGE_PLATFORM "MACOS")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(ROR_RENDERER_LAUNCHER_PACKAGE_PLATFORM "LINUX")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(ROR_RENDERER_LAUNCHER_PACKAGE_PLATFORM "WINDOWS")
    else ()
        message(FATAL_ERROR
            "The renderer public bridge fixture has no platform contract")
    endif ()
    set(ROR_RENDERER_LAUNCHER_OGRE14_CHILD_PRESENT "true")
    set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_CHILD_PRESENT "true")
    set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PRODUCTION_READY "true")
    set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PSSM_ADMITTED "true")
    set(ROR_RENDERER_LAUNCHER_NATIVE_SHADOW_BACKEND "NONE")
    file(MAKE_DIRECTORY
        "${_ror_renderer_public_bridge_generated_include_dir}")
    set(_ror_renderer_public_bridge_generated_header
        "${_ror_renderer_public_bridge_generated_include_dir}/RendererLauncherPackageConfig.generated.h")
    configure_file(
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererLauncherPackageConfig.h.in"
        "${_ror_renderer_public_bridge_generated_header}"
        @ONLY)
    unset(ROR_RENDERER_LAUNCHER_PACKAGE_PLATFORM)
    unset(ROR_RENDERER_LAUNCHER_OGRE14_CHILD_PRESENT)
    unset(ROR_RENDERER_LAUNCHER_OGRE_NEXT_CHILD_PRESENT)
    unset(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PRODUCTION_READY)
    unset(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PSSM_ADMITTED)
    unset(ROR_RENDERER_LAUNCHER_NATIVE_SHADOW_BACKEND)

    set(_ror_renderer_public_bridge_test_output
        "${CMAKE_BINARY_DIR}/renderer-public-bridge-tests")
    if (APPLE)
        set(_ror_renderer_public_bridge_runtime_output
            "${_ror_renderer_public_bridge_test_output}/RoR.app/Contents/MacOS")
        foreach (_ror_public_bridge_config IN ITEMS
            DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            set(
                _ror_renderer_public_bridge_runtime_output_${_ror_public_bridge_config}
                "${_ror_renderer_public_bridge_runtime_output}")
        endforeach ()
        file(MAKE_DIRECTORY
            "${_ror_renderer_public_bridge_test_output}/RoR.app/Contents/Resources/ogrenext/Hlms"
            "${_ror_renderer_public_bridge_test_output}/RoR.app/Contents/Resources/ogrenext/Presentation")
    else ()
        set(_ror_renderer_public_bridge_runtime_output
            "${_ror_renderer_public_bridge_test_output}")
        foreach (_ror_public_bridge_config IN ITEMS
            DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            set(
                _ror_renderer_public_bridge_runtime_output_${_ror_public_bridge_config}
                "${_ror_renderer_public_bridge_test_output}/${_ror_public_bridge_config}")
        endforeach ()
        foreach (_ror_public_bridge_runtime_root IN ITEMS
            "${_ror_renderer_public_bridge_runtime_output}"
            "${_ror_renderer_public_bridge_runtime_output_DEBUG}"
            "${_ror_renderer_public_bridge_runtime_output_RELEASE}"
            "${_ror_renderer_public_bridge_runtime_output_RELWITHDEBINFO}"
            "${_ror_renderer_public_bridge_runtime_output_MINSIZEREL}")
            file(MAKE_DIRECTORY
                "${_ror_public_bridge_runtime_root}/resources/ogrenext/Hlms"
                "${_ror_public_bridge_runtime_root}/resources/ogrenext/Presentation")
        endforeach ()
    endif ()
    add_executable(
        ror_renderer_public_bridge_fake_game
        ${_ror_renderer_bridge_process_fake_common_sources})
    target_compile_definitions(
        ror_renderer_public_bridge_fake_game
        PRIVATE
            ROR_RENDERER_BRIDGE_FAKE_GAME=1
            ROR_RENDERER_BRIDGE_FAKE_ACCEPT_ANY_SESSION=1
            ROR_RENDERER_BRIDGE_FAKE_REQUIRE_PUBLIC_ARGUMENTS=1)
    add_executable(
        ror_renderer_public_bridge_fake_presentation
        ${_ror_renderer_bridge_process_fake_common_sources})
    target_compile_definitions(
        ror_renderer_public_bridge_fake_presentation
        PRIVATE
            ROR_RENDERER_BRIDGE_FAKE_PRESENTATION=1
            ROR_RENDERER_BRIDGE_FAKE_ACCEPT_ANY_SESSION=1
            ROR_RENDERER_BRIDGE_FAKE_REQUIRE_PUBLIC_ARGUMENTS=1)
    foreach (_ror_public_bridge_fake_target IN ITEMS
        ror_renderer_public_bridge_fake_game
        ror_renderer_public_bridge_fake_presentation)
        target_include_directories(
            ${_ror_public_bridge_fake_target}
            PRIVATE
                "${ROR_REPOSITORY_ROOT}/source/main/gfx"
                "${_ror_render_root}"
                "${ROR_REPOSITORY_ROOT}/source/main/system")
    endforeach ()
    set_target_properties(
        ror_renderer_public_bridge_fake_game
        PROPERTIES
            OUTPUT_NAME "RoR-Ogre14"
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_public_bridge_runtime_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_public_bridge_runtime_output_DEBUG}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_public_bridge_runtime_output_RELEASE}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_public_bridge_runtime_output_RELWITHDEBINFO}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_public_bridge_runtime_output_MINSIZEREL}")
    set_target_properties(
        ror_renderer_public_bridge_fake_presentation
        PROPERTIES
            OUTPUT_NAME "RoR-OgreNext"
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_public_bridge_runtime_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_public_bridge_runtime_output_DEBUG}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_public_bridge_runtime_output_RELEASE}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_public_bridge_runtime_output_RELWITHDEBINFO}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_public_bridge_runtime_output_MINSIZEREL}")

    add_executable(
        ror_renderer_public_bridge_entrypoint
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererLauncherMain.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererBackendPolicy.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/gfx/RendererStartupHandoff.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildIntent.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeEndpoint.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeLaunchPlan.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererBridgeProcessSupervisor.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererChildLauncher.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPackageRuntimeProbe.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPackagedMediaPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererSiblingPath.cpp"
        "${ROR_REPOSITORY_ROOT}/source/main/system/RendererPublicLauncher.cpp"
        "${_ror_renderer_public_bridge_generated_header}")
    target_include_directories(
        ror_renderer_public_bridge_entrypoint
        PRIVATE
            "${ROR_REPOSITORY_ROOT}/source/main/gfx"
            "${ROR_REPOSITORY_ROOT}/source/main/system"
            "${_ror_renderer_public_bridge_generated_include_dir}")
    set_target_properties(
        ror_renderer_public_bridge_entrypoint
        PROPERTIES
            OUTPUT_NAME "RoR"
            RUNTIME_OUTPUT_DIRECTORY
                "${_ror_renderer_public_bridge_runtime_output}"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG
                "${_ror_renderer_public_bridge_runtime_output_DEBUG}"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE
                "${_ror_renderer_public_bridge_runtime_output_RELEASE}"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
                "${_ror_renderer_public_bridge_runtime_output_RELWITHDEBINFO}"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
                "${_ror_renderer_public_bridge_runtime_output_MINSIZEREL}")
    add_dependencies(
        ror_renderer_public_bridge_entrypoint
        ror_renderer_public_bridge_fake_game
        ror_renderer_public_bridge_fake_presentation)
    if (WIN32)
        set_target_properties(
            ror_renderer_public_bridge_entrypoint
            PROPERTIES WIN32_EXECUTABLE YES)
        target_compile_definitions(
            ror_renderer_public_bridge_entrypoint
            PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
        target_link_libraries(
            ror_renderer_public_bridge_entrypoint PRIVATE Shell32)
    endif ()
    if (MINGW)
        target_link_options(
            ror_renderer_public_bridge_fake_game PRIVATE -municode)
        target_link_options(
            ror_renderer_public_bridge_fake_presentation PRIVATE -municode)
        target_link_options(
            ror_renderer_public_bridge_entrypoint PRIVATE -municode)
    endif ()

    set(_ror_n1_targets
        ror_ogre_next_n1_contract
        ror_ogre_next_frontend_n1
        ror_ogre_next_frontend_n1_runtime
        ror_ogre_next_frontend_n1_smoke
        ror_ogre_next_pssm_shadow_smoke
        ror_renderer_backend_policy_tests
        ror_renderer_startup_plan_tests
        ror_renderer_startup_handoff_tests
        ror_renderer_child_intent_tests
        ror_renderer_bridge_channel_tests
        ror_renderer_ogre14_game_bridge_tests
        ror_renderer_ogre14_game_host_session_tests
        ror_renderer_sibling_path_tests
        ror_renderer_package_runtime_probe_tests
        ror_renderer_bridge_process_fake_game
        ror_renderer_bridge_process_fake_presentation
        ror_renderer_bridge_process_supervisor_tests
        ror_renderer_ogre_next_child_tests
        ror_renderer_ogre_next_live_session_tests
        ror_renderer_frontend_transport_dispatcher_tests
        ror_ogre14_graphics_scene_source_tests
        ror_render_bridge_control_transport_tests
        ror_renderer_ogre_next_window_host_tests
        ror_renderer_ogre_next_sdl_window_runtime
        ror_renderer_ogre_next_window_host_smoke
        ror_ogre_next_window_present_smoke
        ror_ogre_next_window_run_loop_smoke
        ror_renderer_ogre_next_child_runtime
        ror_renderer_child_launcher_fake_child
        ror_renderer_child_launcher_tests
        ror_renderer_public_launcher_legacy_child
        ror_renderer_public_launcher_entrypoint
        ror_renderer_public_bridge_fake_game
        ror_renderer_public_bridge_fake_presentation
        ror_renderer_public_bridge_entrypoint)
    if (ROR_OGRE_NEXT_VULKAN_RT5 AND
            ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan")
        add_executable(
            ror_ogre_next_vulkan_rt5_smoke
            src/vulkan_rt5_smoke.cpp
            "${_ror_render_root}/ogrenext/OgreNextVulkanExternalDeviceBootstrap.cpp"
            "${_ror_render_root}/ogrenext/OgreNextVulkanRt5Contract.cpp")
        target_compile_definitions(
            ror_ogre_next_vulkan_rt5_smoke
            PRIVATE ROR_OGRE_NEXT_N1_VULKAN=1)
        target_include_directories(
            ror_ogre_next_vulkan_rt5_smoke
            PRIVATE
                "${CMAKE_CURRENT_BINARY_DIR}/generated"
                "${_ror_render_root}/ogrenext")
        target_include_directories(
            ror_ogre_next_vulkan_rt5_smoke
            SYSTEM PRIVATE
                "${ogre_next_SOURCE_DIR}/OgreMain/include"
                "${ogre_next_BINARY_DIR}/include"
                "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include"
                "${Vulkan_INCLUDE_DIR}")
        target_link_libraries(
            ror_ogre_next_vulkan_rt5_smoke
            PRIVATE RenderSystem_Vulkan OgreNextMain)
        set_target_properties(
            ror_ogre_next_vulkan_rt5_smoke
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        list(APPEND _ror_n1_targets ror_ogre_next_vulkan_rt5_smoke)
    endif ()
    if (ROR_OGRE_NEXT_VULKAN_RT6 AND
            ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan")
        add_executable(
            ror_ogre_next_vulkan_rt6_smoke
            src/vulkan_rt6_smoke.cpp
            "${_ror_render_root}/ogrenext/OgreNextVulkanRayTracingBootstrap.cpp"
            "${_ror_render_root}/ogrenext/OgreNextVulkanRt6Contract.cpp")
        target_compile_definitions(
            ror_ogre_next_vulkan_rt6_smoke
            PRIVATE ROR_OGRE_NEXT_N1_VULKAN=1)
        target_include_directories(
            ror_ogre_next_vulkan_rt6_smoke
            PRIVATE
                "${CMAKE_CURRENT_BINARY_DIR}/generated"
                "${_ror_render_root}/ogrenext")
        target_include_directories(
            ror_ogre_next_vulkan_rt6_smoke
            SYSTEM PRIVATE
                "${ogre_next_SOURCE_DIR}/OgreMain/include"
                "${ogre_next_BINARY_DIR}/include"
                "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include"
                "${shaderc_SOURCE_DIR}/libshaderc/include"
                "${Vulkan_INCLUDE_DIR}")
        target_link_libraries(
            ror_ogre_next_vulkan_rt6_smoke
            PRIVATE
                RenderSystem_Vulkan
                OgreNextMain
                shaderc_combined)
        set_target_properties(
            ror_ogre_next_vulkan_rt6_smoke
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        list(APPEND _ror_n1_targets ror_ogre_next_vulkan_rt6_smoke)
    endif ()
    if (ROR_OGRE_NEXT_WINDOWS_DXR7 AND WIN32)
        set(ROR_OGRE_NEXT_DXC_EXECUTABLE
            "$ENV{ROR_OGRE_NEXT_DXR7_DXC}" CACHE FILEPATH
            "Exact Windows SDK dxc.exe used for the DXR7 proof")
        set(ROR_OGRE_NEXT_DXC_SDK_BIN_ROOT
            "$ENV{ROR_OGRE_NEXT_DXR7_SDK_BIN_ROOT}" CACHE PATH
            "Exact Windows Kits/10/bin directory containing the DXC closure")
        if (NOT EXISTS "${ROR_OGRE_NEXT_DXC_EXECUTABLE}" OR
                IS_DIRECTORY "${ROR_OGRE_NEXT_DXC_EXECUTABLE}")
            message(FATAL_ERROR
                "ROR_OGRE_NEXT_DXC_EXECUTABLE must name an exact dxc.exe")
        endif ()
        if (ROR_OGRE_NEXT_DXC_SDK_BIN_ROOT STREQUAL "" OR
                NOT IS_DIRECTORY "${ROR_OGRE_NEXT_DXC_SDK_BIN_ROOT}")
            message(FATAL_ERROR
                "ROR_OGRE_NEXT_DXC_SDK_BIN_ROOT must name Windows Kits/10/bin")
        endif ()
        file(REAL_PATH "${ROR_OGRE_NEXT_DXC_EXECUTABLE}"
            _ror_dxc_real_path)
        file(REAL_PATH "${ROR_OGRE_NEXT_DXC_SDK_BIN_ROOT}"
            _ror_dxc_sdk_bin_root_real)
        set(_ror_windows_sdk_bin_root
            "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin")
        if ("$ENV{ProgramFiles\(x86\)}" STREQUAL "" OR
                NOT IS_DIRECTORY "${_ror_windows_sdk_bin_root}")
            message(FATAL_ERROR
                "The canonical Program Files (x86) Windows SDK bin root is unavailable")
        endif ()
        file(REAL_PATH "${_ror_windows_sdk_bin_root}"
            _ror_windows_sdk_bin_root_real)
        string(TOLOWER "${_ror_dxc_sdk_bin_root_real}"
            _ror_dxc_sdk_bin_root_normalized)
        string(TOLOWER "${_ror_windows_sdk_bin_root_real}"
            _ror_windows_sdk_bin_root_normalized)
        if (NOT _ror_dxc_sdk_bin_root_normalized STREQUAL
                _ror_windows_sdk_bin_root_normalized)
            message(FATAL_ERROR
                "DXC must come from Program Files (x86)/Windows Kits/10/bin")
        endif ()
        get_filename_component(_ror_dxc_filename
            "${_ror_dxc_real_path}" NAME)
        get_filename_component(ROR_WINDOWS_DXR7_DXC_X64_DIRECTORY
            "${_ror_dxc_real_path}" DIRECTORY)
        get_filename_component(_ror_dxc_arch_directory
            "${ROR_WINDOWS_DXR7_DXC_X64_DIRECTORY}" NAME)
        get_filename_component(_ror_dxc_sdk_version_directory
            "${ROR_WINDOWS_DXR7_DXC_X64_DIRECTORY}" DIRECTORY)
        get_filename_component(ROR_WINDOWS_DXR7_DXC_SDK_VERSION
            "${_ror_dxc_sdk_version_directory}" NAME)
        get_filename_component(_ror_dxc_observed_sdk_bin_root
            "${_ror_dxc_sdk_version_directory}" DIRECTORY)
        file(REAL_PATH "${_ror_dxc_observed_sdk_bin_root}"
            _ror_dxc_observed_sdk_bin_root_real)
        if (NOT _ror_dxc_filename STREQUAL "dxc.exe" OR
                NOT _ror_dxc_arch_directory STREQUAL "x64" OR
                NOT ROR_WINDOWS_DXR7_DXC_SDK_VERSION MATCHES
                    "^10\\.0\\.[0-9]+\\.[0-9]+$" OR
                NOT _ror_dxc_observed_sdk_bin_root_real STREQUAL
                    _ror_dxc_sdk_bin_root_real)
            message(FATAL_ERROR
                "DXC must be Windows Kits/10/bin/<version>/x64/dxc.exe")
        endif ()
        set(_ror_dxcompiler
            "${ROR_WINDOWS_DXR7_DXC_X64_DIRECTORY}/dxcompiler.dll")
        set(_ror_dxil_dll
            "${ROR_WINDOWS_DXR7_DXC_X64_DIRECTORY}/dxil.dll")
        foreach (_ror_dxc_component IN ITEMS
                "${_ror_dxc_real_path}" "${_ror_dxcompiler}" "${_ror_dxil_dll}")
            if (NOT EXISTS "${_ror_dxc_component}" OR
                    IS_DIRECTORY "${_ror_dxc_component}" OR
                    IS_SYMLINK "${_ror_dxc_component}")
                message(FATAL_ERROR
                    "The reviewed Windows SDK DXC x64 closure is incomplete")
            endif ()
        endforeach ()
        file(SHA256 "${_ror_dxc_real_path}"
            ROR_WINDOWS_DXR7_DXC_SHA256)
        file(SHA256 "${_ror_dxcompiler}"
            ROR_WINDOWS_DXR7_DXCOMPILER_SHA256)
        file(SHA256 "${_ror_dxil_dll}"
            ROR_WINDOWS_DXR7_DXIL_DLL_SHA256)
        execute_process(
            COMMAND "${_ror_dxc_real_path}" --version
            RESULT_VARIABLE _ror_dxc_version_result
            OUTPUT_VARIABLE ROR_WINDOWS_DXR7_DXC_VERSION
            ERROR_VARIABLE _ror_dxc_version_error
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (ROR_WINDOWS_DXR7_DXC_VERSION STREQUAL "")
            set(ROR_WINDOWS_DXR7_DXC_VERSION
                "${_ror_dxc_version_error}")
        endif ()
        string(LENGTH "${ROR_WINDOWS_DXR7_DXC_VERSION}"
            _ror_dxc_version_length)
        if (NOT _ror_dxc_version_result STREQUAL "0" OR
                ROR_WINDOWS_DXR7_DXC_VERSION STREQUAL "" OR
                _ror_dxc_version_length GREATER 512)
            message(FATAL_ERROR
                "The reviewed Windows SDK dxc.exe did not report a bounded version: "
                "${_ror_dxc_version_error}")
        endif ()
        string(REGEX REPLACE "[\r\n\t ]+" " "
            ROR_WINDOWS_DXR7_DXC_VERSION
            "${ROR_WINDOWS_DXR7_DXC_VERSION}")
        if (ROR_WINDOWS_DXR7_DXC_VERSION MATCHES "[^ -~]")
            message(FATAL_ERROR
                "The Windows SDK DXC version cannot be embedded safely")
        endif ()
        # Preserve the exact bounded printable SDK version in the runtime
        # evidence. Semicolons are part of Microsoft's normal `dxc --version`
        # output; only characters significant to a C string literal need an
        # escaped configure-file representation.
        set(ROR_WINDOWS_DXR7_DXC_VERSION_C_LITERAL
            "${ROR_WINDOWS_DXR7_DXC_VERSION}")
        string(REPLACE "\\" "\\\\"
            ROR_WINDOWS_DXR7_DXC_VERSION_C_LITERAL
            "${ROR_WINDOWS_DXR7_DXC_VERSION_C_LITERAL}")
        string(REPLACE "\"" "\\\""
            ROR_WINDOWS_DXR7_DXC_VERSION_C_LITERAL
            "${ROR_WINDOWS_DXR7_DXC_VERSION_C_LITERAL}")
        file(TO_CMAKE_PATH "${_ror_dxc_real_path}"
            ROR_WINDOWS_DXR7_DXC_PATH)
        file(TO_CMAKE_PATH "${ROR_WINDOWS_DXR7_DXC_X64_DIRECTORY}"
            ROR_WINDOWS_DXR7_DXC_X64_DIRECTORY)
        set(ROR_WINDOWS_DXR7_BINARY_MARKER
            "ror-ogre-next-dxr7-pe-v2:${ROR_SOURCE_COMMIT}:${ROR_SOURCE_MANIFEST_SHA256}")
        set(_ror_dxr7_hlsl
            "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_WINDOWS_DXR7_SHADER_PATH}")
        file(SHA256 "${_ror_dxr7_hlsl}"
            ROR_WINDOWS_DXR7_HLSL_SOURCE_SHA256)
        set(_ror_dxr7_dxil
            "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_windows_dxr7_probe.dxil")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/windows_dxr7_config.h.in"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/ror_ogre_next_windows_dxr7_config.h"
            @ONLY)
        add_custom_command(
            OUTPUT "${_ror_dxr7_dxil}"
            COMMAND
                "${_ror_dxc_real_path}"
                -T "${ROR_WINDOWS_DXR7_SHADER_TARGET}"
                -HV 2021 -O3 -Qstrip_debug -Qstrip_reflect
                -Fo "${_ror_dxr7_dxil}"
                "${_ror_dxr7_hlsl}"
            DEPENDS
                "${_ror_dxr7_hlsl}"
                "${ROR_OGRE_NEXT_WINDOWS_DXR7_LOCK_PATH}"
            COMMENT "Compiling the exact Windows DXR7 shader library"
            VERBATIM)
        add_custom_target(ror_ogre_next_windows_dxr7_shader
            DEPENDS "${_ror_dxr7_dxil}")
        add_executable(
            ror_ogre_next_windows_dxr7_smoke
            src/windows_dxr7_smoke.cpp
            "${_ror_render_root}/ogrenext/OgreNextD3D12DxrBootstrap.cpp"
            "${_ror_render_root}/ogrenext/OgreNextDxr7Contract.cpp"
            "${_ror_render_root}/ogrenext/NativeDirectionalShadowContract.cpp")
        add_dependencies(
            ror_ogre_next_windows_dxr7_smoke
            ror_ogre_next_windows_dxr7_shader)
        target_compile_definitions(
            ror_ogre_next_windows_dxr7_smoke
            PRIVATE ROR_OGRE_NEXT_N1_D3D11=1)
        target_include_directories(
            ror_ogre_next_windows_dxr7_smoke
            PRIVATE
                "${CMAKE_CURRENT_BINARY_DIR}/generated"
                "${_ror_render_root}/ogrenext")
        target_include_directories(
            ror_ogre_next_windows_dxr7_smoke
            SYSTEM PRIVATE
                "${ogre_next_SOURCE_DIR}/OgreMain/include"
                "${ogre_next_BINARY_DIR}/include"
                "${ogre_next_SOURCE_DIR}/Components/Hlms/Common/include"
                "${ogre_next_SOURCE_DIR}/Components/Hlms/Pbs/include"
                "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/include")
        target_link_libraries(
            ror_ogre_next_windows_dxr7_smoke
            PRIVATE
                RenderSystem_Direct3D11
                OgreNextMain
                OgreNextHlmsPbs
                d3d11
                d3d12
                d3dcompiler
                dxgi)
        set_target_properties(
            ror_ogre_next_windows_dxr7_smoke
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        list(APPEND _ror_n1_targets
            ror_ogre_next_windows_dxr7_smoke)
    endif ()
    if (APPLE)
        set(_ror_n2_repository_root
            "${CMAKE_CURRENT_SOURCE_DIR}/../..")
        set(_ror_n2_relevant_source_paths
            cmake/RendererLauncherPackageConfig.cmake
            source/main/gfx/RendererBackendPolicy.cpp
            source/main/gfx/RendererBackendPolicy.h
            source/main/gfx/GfxScene.cpp
            source/main/gfx/GfxScene.h
            source/main/gfx/RendererStartupHandoff.cpp
            source/main/gfx/RendererStartupHandoff.h
            source/main/gfx/RendererStartupPlan.cpp
            source/main/gfx/RendererStartupPlan.h
            source/main/main.cpp
            source/main/system/RendererBridgeChannel.cpp
            source/main/system/RendererBridgeChannel.h
            source/main/system/RendererBridgeEndpoint.cpp
            source/main/system/RendererBridgeEndpoint.h
            source/main/system/RendererBridgeLaunchPlan.cpp
            source/main/system/RendererBridgeLaunchPlan.h
            source/main/system/RendererBridgeProcessSupervisor.cpp
            source/main/system/RendererBridgeProcessSupervisor.h
            source/main/system/RendererChildIntent.cpp
            source/main/system/RendererChildIntent.h
            source/main/system/RendererChildLauncher.cpp
            source/main/system/RendererChildLauncher.h
            source/main/system/RendererOgre14GameBridge.cpp
            source/main/system/RendererOgre14GameBridge.h
            source/main/system/RendererOgre14GameHostSession.cpp
            source/main/system/RendererOgre14GameHostSession.h
            source/main/system/RendererSiblingPath.cpp
            source/main/system/RendererSiblingPath.h
            source/main/system/RendererPackagedMediaPath.cpp
            source/main/system/RendererPackagedMediaPath.h
            source/main/system/RendererPackageRuntimeProbe.cpp
            source/main/system/RendererPackageRuntimeProbe.h
            source/main/system/RendererOgreNextChild.cpp
            source/main/system/RendererOgreNextChild.h
            source/main/system/RendererOgreNextChildMain.cpp
            source/main/system/RendererLauncherMain.cpp
            source/main/system/RendererLauncherPackageConfig.h.in
            source/main/system/RendererPublicLauncher.cpp
            source/main/system/RendererPublicLauncher.h
            source/main/gfx/render
            tests/gfx/RendererBackendPolicyTests.cpp
            tests/gfx/RendererBridgeEndpointTests.cpp
            tests/gfx/RendererBridgeLaunchPlanTests.cpp
            tests/gfx/RendererBridgeChannelTests.cpp
            tests/gfx/RendererBridgeProcessFakeChild.cpp
            tests/gfx/RendererBridgeProcessSupervisorTests.cpp
            tests/gfx/RendererChildIntentTests.cpp
            tests/gfx/RendererChildLauncherFakeChild.cpp
            tests/gfx/RendererChildLauncherTests.cpp
            tests/gfx/RendererOgre14GameBridgeTests.cpp
            tests/gfx/RendererOgre14GameHostSessionTests.cpp
            tests/gfx/RendererSiblingPathTests.cpp
            tests/gfx/RendererPackageRuntimeProbeTests.cpp
            tests/gfx/RendererOgreNextChildTests.cpp
            tests/gfx/RendererPublicLauncherLegacyChild.cpp
            tests/gfx/RendererPublicLauncherTests.cpp
            tests/cmake/VerifyRendererPublicBridgeExit.cmake
            tests/gfx/RendererStartupHandoffTests.cpp
            tests/gfx/RendererStartupPlanTests.cpp
            tests/gfx/render/RenderBridgeControlTransportTests.cpp
            tests/gfx/render/RendererFrontendTransportDispatcherTests.cpp
            tests/gfx/render/Ogre14GraphicsSceneSourceTests.cpp
            tests/tools/test_ogre_next_child_runtime_contract.py
            tools/ogre_next_probe
            tools/run_ogre_next_probe.py
            tools/validate_ogre_next_frame_probe.py
            tools/verify_ogre_next_artifact_set.py)
        execute_process(
            COMMAND git status --porcelain=v1 --untracked-files=all --
                    ${_ror_n2_relevant_source_paths}
            WORKING_DIRECTORY "${_ror_n2_repository_root}"
            RESULT_VARIABLE _ror_n2_status_result
            OUTPUT_VARIABLE _ror_n2_status
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_VARIABLE _ror_n2_status_error)
        if (NOT _ror_n2_status_result EQUAL 0)
            message(FATAL_ERROR
                "Could not inspect Metal N2 relevant source state: "
                "${_ror_n2_status_error}")
        endif ()
        if (NOT _ror_n2_status STREQUAL "")
            message(FATAL_ERROR
                "Metal N2 provenance requires a clean relevant source set:\n"
                "${_ror_n2_status}")
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
                ROR_OGRE_NEXT_N2_TEST_SEAM=1
                ROR_OGRE_NEXT_N2_SOURCE_REPOSITORY="${ROR_SOURCE_REPOSITORY}"
                ROR_OGRE_NEXT_N2_SOURCE_COMMIT="${ROR_SOURCE_COMMIT}"
                ROR_OGRE_NEXT_N2_SOURCE_REF="${ROR_SOURCE_REF}"
                ROR_OGRE_NEXT_N2_RELEVANT_SOURCE_CLEAN=1
                ROR_OGRE_NEXT_N2_SOURCE_MANIFEST_SHA256="${ROR_SOURCE_MANIFEST_SHA256}")
        target_link_libraries(
            ror_ogre_next_metal_n2_smoke
            PRIVATE ror_ogre_next_frontend_n1)
        set_target_properties(
            ror_ogre_next_metal_n2_smoke
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        add_custom_command(
            TARGET ror_ogre_next_metal_n2_smoke PRE_LINK
            COMMAND
                "${CMAKE_COMMAND}"
                "-DN2_REPOSITORY_ROOT=${_ror_n2_repository_root}"
                "-DN2_EXPECTED_SOURCE_MANIFEST_SHA256=${ROR_SOURCE_MANIFEST_SHA256}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyN2SourceProvenance.cmake"
            COMMENT "Verifying clean Metal N2 source provenance"
            VERBATIM)
        add_executable(
            ror_ogre_next_metal_n3_smoke
            src/metal_n3_smoke.cpp)
        target_include_directories(
            ror_ogre_next_metal_n3_smoke
            PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_compile_definitions(
            ror_ogre_next_metal_n3_smoke
            PRIVATE
                ROR_OGRE_NEXT_N2_TEST_SEAM=1
                ROR_OGRE_NEXT_N3_SOURCE_REPOSITORY="${ROR_SOURCE_REPOSITORY}"
                ROR_OGRE_NEXT_N3_SOURCE_COMMIT="${ROR_SOURCE_COMMIT}"
                ROR_OGRE_NEXT_N3_SOURCE_REF="${ROR_SOURCE_REF}"
                ROR_OGRE_NEXT_N3_RELEVANT_SOURCE_CLEAN=1
                ROR_OGRE_NEXT_N3_SOURCE_MANIFEST_SHA256="${ROR_SOURCE_MANIFEST_SHA256}")
        target_link_libraries(
            ror_ogre_next_metal_n3_smoke
            PRIVATE ror_ogre_next_frontend_n1)
        set_target_properties(
            ror_ogre_next_metal_n3_smoke
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        add_custom_command(
            TARGET ror_ogre_next_metal_n3_smoke PRE_LINK
            COMMAND
                "${CMAKE_COMMAND}"
                "-DN2_REPOSITORY_ROOT=${_ror_n2_repository_root}"
                "-DN2_EXPECTED_SOURCE_MANIFEST_SHA256=${ROR_SOURCE_MANIFEST_SHA256}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyN2SourceProvenance.cmake"
            COMMENT "Verifying clean Metal N3 source provenance"
            VERBATIM)
        add_executable(
            ror_ogre_next_metal_n4_directional_shadow_smoke
            src/metal_n4_directional_shadow_smoke.cpp)
        target_include_directories(
            ror_ogre_next_metal_n4_directional_shadow_smoke
            PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_compile_definitions(
            ror_ogre_next_metal_n4_directional_shadow_smoke
            PRIVATE
                ROR_OGRE_NEXT_N2_TEST_SEAM=1
                ROR_OGRE_NEXT_N4_SOURCE_REPOSITORY="${ROR_SOURCE_REPOSITORY}"
                ROR_OGRE_NEXT_N4_SOURCE_COMMIT="${ROR_SOURCE_COMMIT}"
                ROR_OGRE_NEXT_N4_SOURCE_REF="${ROR_SOURCE_REF}"
                ROR_OGRE_NEXT_N4_RELEVANT_SOURCE_CLEAN=1
                ROR_OGRE_NEXT_N4_SOURCE_MANIFEST_SHA256="${ROR_SOURCE_MANIFEST_SHA256}")
        target_link_libraries(
            ror_ogre_next_metal_n4_directional_shadow_smoke
            PRIVATE ror_ogre_next_frontend_n1)
        set_target_properties(
            ror_ogre_next_metal_n4_directional_shadow_smoke
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        add_custom_command(
            TARGET ror_ogre_next_metal_n4_directional_shadow_smoke PRE_LINK
            COMMAND
                "${CMAKE_COMMAND}"
                "-DN2_REPOSITORY_ROOT=${_ror_n2_repository_root}"
                "-DN2_EXPECTED_SOURCE_MANIFEST_SHA256=${ROR_SOURCE_MANIFEST_SHA256}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyN2SourceProvenance.cmake"
            COMMENT "Verifying clean Metal N4 source provenance"
            VERBATIM)
        list(APPEND _ror_n1_targets
            ror_ogre_next_metal_n2_smoke
            ror_ogre_next_metal_n3_smoke
            ror_ogre_next_metal_n4_directional_shadow_smoke)
    endif ()

    set(ROR_OGRE_NEXT_N1_PACKAGE_ROOT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-n1-package")
    set(ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE}")
    set(ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/bin/$<TARGET_FILE_NAME:ror_ogre_next_frontend_n1_smoke>")
    set(ROR_OGRE_NEXT_N1_PACKAGE_STAMP
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/.stage-v10")
    set(_ror_n1_package_commands
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/../../COPYING"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/licenses/Rigs-of-Rods-GPL-3.0.txt"
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${CMAKE_CURRENT_SOURCE_DIR}/../../COPYING"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/licenses/Rigs-of-Rods-GPL-3.0.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ROR_OGRE_NEXT_PACKAGE_OGRE_LICENSE_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/licenses/Ogre-Next-MIT.txt"
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${ROR_OGRE_NEXT_PACKAGE_OGRE_LICENSE_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/licenses/Ogre-Next-MIT.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ROR_OGRE_NEXT_PACKAGE_RAPIDJSON_LICENSE_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/licenses/RapidJSON-license.txt"
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${ROR_OGRE_NEXT_PACKAGE_RAPIDJSON_LICENSE_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/licenses/RapidJSON-license.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ROR_OGRE_NEXT_PACKAGE_FREETYPE_LICENSE_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_FREETYPE_LICENSE_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${ROR_OGRE_NEXT_PACKAGE_FREETYPE_LICENSE_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_FREETYPE_LICENSE_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ROR_OGRE_NEXT_PACKAGE_FREETYPE_OVERVIEW_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_FREETYPE_OVERVIEW_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${ROR_OGRE_NEXT_PACKAGE_FREETYPE_OVERVIEW_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_FREETYPE_OVERVIEW_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ROR_OGRE_NEXT_PACKAGE_IBLBAKER_LICENSE_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_IBLBAKER_NOTICE_RELATIVE}"
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${ROR_OGRE_NEXT_PACKAGE_IBLBAKER_LICENSE_SOURCE}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_IBLBAKER_NOTICE_RELATIVE}")
    set(_ror_n1_package_dependencies
        ror_renderer_backend_policy_tests
        ror_renderer_startup_plan_tests
        ror_renderer_startup_handoff_tests
        ror_renderer_child_intent_tests
        ror_renderer_sibling_path_tests
        ror_renderer_package_runtime_probe_tests
        ror_renderer_bridge_process_supervisor_tests
        ror_renderer_ogre_next_child_tests
        ror_ogre14_graphics_scene_source_tests
        ror_renderer_child_launcher_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../COPYING"
        "${ROR_OGRE_NEXT_PACKAGE_OGRE_LICENSE_SOURCE}"
        "${ROR_OGRE_NEXT_PACKAGE_RAPIDJSON_LICENSE_SOURCE}"
        "${ROR_OGRE_NEXT_PACKAGE_FREETYPE_LICENSE_SOURCE}"
        "${ROR_OGRE_NEXT_PACKAGE_FREETYPE_OVERVIEW_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
        "${ROR_OGRE_NEXT_PACKAGE_IBLBAKER_LICENSE_SOURCE}")
    foreach (_ror_reflection_media_relative IN LISTS
            _ror_reflection_media_files)
        get_filename_component(_ror_reflection_media_directory
            "${_ror_reflection_media_relative}" DIRECTORY)
        list(APPEND _ror_n1_package_commands
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/${_ror_reflection_media_directory}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_reflection_media_relative}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/${_ror_reflection_media_relative}"
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                    "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_reflection_media_relative}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/${_ror_reflection_media_relative}")
        list(APPEND _ror_n1_package_dependencies
            "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_reflection_media_relative}")
    endforeach ()
    if (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan")
        list(APPEND _ror_n1_package_commands
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${ROR_OGRE_NEXT_LINUX_APACHE_NOTICE_SOURCE}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_LINUX_APACHE_NOTICE_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                    "${ROR_OGRE_NEXT_LINUX_APACHE_NOTICE_SOURCE}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_LINUX_APACHE_NOTICE_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${ROR_OGRE_NEXT_LINUX_GLSLANG_NOTICE_SOURCE}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_LINUX_GLSLANG_NOTICE_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                    "${ROR_OGRE_NEXT_LINUX_GLSLANG_NOTICE_SOURCE}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_LINUX_GLSLANG_NOTICE_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${ROR_OGRE_NEXT_LINUX_SPIRV_TOOLS_NOTICE_SOURCE}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_LINUX_SPIRV_TOOLS_NOTICE_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                    "${ROR_OGRE_NEXT_LINUX_SPIRV_TOOLS_NOTICE_SOURCE}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_LINUX_SPIRV_TOOLS_NOTICE_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${ROR_OGRE_NEXT_LINUX_SPIRV_HEADERS_NOTICE_SOURCE}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_LINUX_SPIRV_HEADERS_NOTICE_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                    "${ROR_OGRE_NEXT_LINUX_SPIRV_HEADERS_NOTICE_SOURCE}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_LINUX_SPIRV_HEADERS_NOTICE_PATH}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_PATH}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/provenance/ogre-next-linux-shader-toolchain.lock.json"
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                    "${ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_PATH}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/provenance/ogre-next-linux-shader-toolchain.lock.json"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${ROR_OGRE_NEXT_LINUX_STATIC_CLOSURE_MANIFEST}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/provenance/ogre-next-linux-static-closure.json"
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                    "${ROR_OGRE_NEXT_LINUX_STATIC_CLOSURE_MANIFEST}"
                    "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/provenance/ogre-next-linux-static-closure.json")
        list(APPEND _ror_n1_package_dependencies
            ror_ogre_next_linux_static_closure_manifest
            "${ROR_OGRE_NEXT_LINUX_APACHE_NOTICE_SOURCE}"
            "${ROR_OGRE_NEXT_LINUX_GLSLANG_NOTICE_SOURCE}"
            "${ROR_OGRE_NEXT_LINUX_SPIRV_TOOLS_NOTICE_SOURCE}"
            "${ROR_OGRE_NEXT_LINUX_SPIRV_HEADERS_NOTICE_SOURCE}"
            "${ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_PATH}"
            "${ROR_OGRE_NEXT_LINUX_STATIC_CLOSURE_MANIFEST}")
    endif ()
    add_custom_command(
        OUTPUT "${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/bin"
                "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/licenses"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/provenance"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_FILE:ror_ogre_next_frontend_n1_smoke>
                "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${ogre_next_SOURCE_DIR}/Samples/Media/Hlms"
                "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/Hlms"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${ogre_next_SOURCE_DIR}/Samples/Media/2.0/scripts/Compositors"
                "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/2.0/scripts/Compositors"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${ogre_next_SOURCE_DIR}/Samples/Media/2.0/scripts/materials/Common"
                "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/2.0/scripts/materials/Common"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${ogre_next_SOURCE_DIR}/Samples/Media/2.0/scripts/materials/HDR"
                "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}/2.0/scripts/materials/HDR"
        ${_ror_n1_package_commands}
        COMMAND "${CMAKE_COMMAND}" -E touch
                "${ROR_OGRE_NEXT_N1_PACKAGE_STAMP}"
        DEPENDS
            ror_ogre_next_frontend_n1_smoke
            ${_ror_n1_package_dependencies}
        COMMENT "Staging relocatable Ogre-Next N1 runtime, notices, and provenance"
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
    set_target_properties(
        ror_renderer_backend_policy_tests
        ror_renderer_startup_plan_tests
        ror_renderer_startup_handoff_tests
        ror_renderer_child_intent_tests
        ror_renderer_sibling_path_tests
        ror_renderer_package_runtime_probe_tests
        ror_renderer_ogre_next_child_tests
        ror_renderer_child_launcher_fake_child
        ror_renderer_child_launcher_tests
        ror_renderer_public_launcher_legacy_child
        ror_renderer_public_launcher_entrypoint
        ror_renderer_public_bridge_entrypoint
        PROPERTIES
            CXX_STANDARD 11
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO)
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
if (TARGET ror_renderer_public_launcher_entrypoint)
    add_dependencies(
        ror_ogre_next_probe_report
        ror_renderer_public_launcher_entrypoint)
endif ()
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
if (TARGET ror_renderer_backend_policy_tests)
    add_test(NAME ror_renderer_backend_policy
        COMMAND $<TARGET_FILE:ror_renderer_backend_policy_tests>)
endif ()
if (TARGET ror_renderer_startup_plan_tests)
    add_test(NAME ror_renderer_startup_plan
        COMMAND $<TARGET_FILE:ror_renderer_startup_plan_tests>)
endif ()
if (TARGET ror_renderer_startup_handoff_tests)
    add_test(NAME ror_renderer_startup_handoff
        COMMAND $<TARGET_FILE:ror_renderer_startup_handoff_tests>)
endif ()
if (TARGET ror_renderer_child_intent_tests)
    add_test(NAME ror_renderer_child_intent
        COMMAND $<TARGET_FILE:ror_renderer_child_intent_tests>)
endif ()
if (TARGET ror_renderer_bridge_channel_tests)
    add_test(NAME ror_renderer_bridge_channel
        COMMAND $<TARGET_FILE:ror_renderer_bridge_channel_tests>)
endif ()
if (TARGET ror_renderer_ogre14_game_bridge_tests)
    add_test(NAME ror_renderer_ogre14_game_bridge
        COMMAND $<TARGET_FILE:ror_renderer_ogre14_game_bridge_tests>)
endif ()
if (TARGET ror_renderer_ogre14_game_host_session_tests)
    add_test(NAME ror_renderer_ogre14_game_host_session
        COMMAND $<TARGET_FILE:ror_renderer_ogre14_game_host_session_tests>)
endif ()
if (TARGET ror_renderer_sibling_path_tests)
    add_test(NAME ror_renderer_sibling_path
        COMMAND $<TARGET_FILE:ror_renderer_sibling_path_tests>)
endif ()
if (TARGET ror_renderer_package_runtime_probe_tests)
    add_test(NAME ror_renderer_package_runtime_probe
        COMMAND $<TARGET_FILE:ror_renderer_package_runtime_probe_tests>)
endif ()
if (TARGET ror_renderer_bridge_process_supervisor_tests)
    add_test(NAME ror_renderer_bridge_process_supervisor
        COMMAND $<TARGET_FILE:ror_renderer_bridge_process_supervisor_tests>)
    set_tests_properties(
        ror_renderer_bridge_process_supervisor
        PROPERTIES TIMEOUT 30)
endif ()
if (TARGET ror_renderer_ogre_next_child_tests)
    add_test(NAME ror_renderer_ogre_next_child
        COMMAND $<TARGET_FILE:ror_renderer_ogre_next_child_tests>)
endif ()
if (TARGET ror_renderer_ogre_next_live_session_tests)
    add_test(NAME ror_renderer_ogre_next_live_session
        COMMAND $<TARGET_FILE:ror_renderer_ogre_next_live_session_tests>)
    set_tests_properties(
        ror_renderer_ogre_next_live_session
        PROPERTIES TIMEOUT 30)
endif ()
if (TARGET ror_renderer_frontend_transport_dispatcher_tests)
    add_test(NAME ror_renderer_frontend_transport_dispatcher
        COMMAND $<TARGET_FILE:ror_renderer_frontend_transport_dispatcher_tests>)
endif ()
if (TARGET ror_ogre14_graphics_scene_source_tests)
    add_test(NAME ror_ogre14_graphics_scene_source
        COMMAND $<TARGET_FILE:ror_ogre14_graphics_scene_source_tests>)
endif ()
if (TARGET ror_render_bridge_control_transport_tests)
    add_test(NAME ror_render_bridge_control_transport
        COMMAND $<TARGET_FILE:ror_render_bridge_control_transport_tests>)
endif ()
if (TARGET ror_renderer_ogre_next_window_host_tests)
    add_test(NAME ror_renderer_ogre_next_window_host
        COMMAND $<TARGET_FILE:ror_renderer_ogre_next_window_host_tests>)
endif ()
if (TARGET ror_renderer_ogre_next_window_host_smoke)
    add_test(NAME ror_renderer_ogre_next_window_host_smoke
        COMMAND $<TARGET_FILE:ror_renderer_ogre_next_window_host_smoke>)
    set_tests_properties(
        ror_renderer_ogre_next_window_host_smoke
        PROPERTIES
            RESOURCE_LOCK ror_ogre_next_n1_native_device
            SKIP_RETURN_CODE 77
            TIMEOUT 20)
endif ()
if (TARGET ror_ogre_next_window_present_smoke)
    add_test(NAME ror_ogre_next_window_present_smoke
        COMMAND $<TARGET_FILE:ror_ogre_next_window_present_smoke>
            --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            --output "${ROR_OGRE_NEXT_WINDOW_PRESENT_IMAGE}"
            --report "${ROR_OGRE_NEXT_WINDOW_PRESENT_REPORT}")
    set_tests_properties(
        ror_ogre_next_window_present_smoke
        PROPERTIES
            RESOURCE_LOCK ror_ogre_next_n1_native_device
            TIMEOUT 60)
endif ()
if (APPLE AND TARGET ror_ogre_next_window_run_loop_smoke)
    add_test(NAME ror_ogre_next_window_run_loop_smoke
        COMMAND $<TARGET_FILE:ror_ogre_next_window_run_loop_smoke>
            --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            --output "${ROR_OGRE_NEXT_WINDOW_RUN_LOOP_IMAGE}"
            --report "${ROR_OGRE_NEXT_WINDOW_RUN_LOOP_REPORT}")
    set_tests_properties(
        ror_ogre_next_window_run_loop_smoke
        PROPERTIES
            RESOURCE_LOCK ror_ogre_next_n1_native_device
            TIMEOUT 180)
endif ()
if (TARGET ror_renderer_ogre_next_child_runtime)
    add_test(
        NAME ror_renderer_ogre_next_child_runtime
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/run_child_runtime_receipt.py"
            --build-dir "${CMAKE_BINARY_DIR}"
            --child $<TARGET_FILE:ror_renderer_ogre_next_child_runtime>
            --timeout-seconds 110)
    set_tests_properties(
        ror_renderer_ogre_next_child_runtime
        PROPERTIES
            RESOURCE_LOCK ror_ogre_next_n1_native_device
            SKIP_RETURN_CODE 77
            TIMEOUT 120
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(
        NAME ror_renderer_ogre_next_child_runtime_receipt
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/validate_child_runtime_receipt.py"
            --build-dir "${CMAKE_BINARY_DIR}"
            --require-pass-or-skip)
    set_tests_properties(
        ror_renderer_ogre_next_child_runtime_receipt
        PROPERTIES
            DEPENDS ror_renderer_ogre_next_child_runtime
            TIMEOUT 10
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()
if (TARGET ror_renderer_child_launcher_tests)
    add_test(NAME ror_renderer_child_launcher
        COMMAND $<TARGET_FILE:ror_renderer_child_launcher_tests>)
endif ()
if (TARGET ror_renderer_public_launcher_entrypoint)
    add_test(NAME ror_renderer_public_launcher_entrypoint
        COMMAND $<TARGET_FILE:ror_renderer_public_launcher_entrypoint>)
    set_tests_properties(
        ror_renderer_public_launcher_entrypoint
        PROPERTIES TIMEOUT 10)
endif ()
if (TARGET ror_renderer_public_bridge_entrypoint)
    foreach (_ror_public_bridge_case IN ITEMS
        default-prefer
        explicit-require
        presentation-first)
        add_test(
            NAME "ror_renderer_public_bridge_${_ror_public_bridge_case}"
            COMMAND
                "${CMAKE_COMMAND}"
                "-DROR_RENDERER_PUBLIC_BRIDGE_LAUNCHER=$<TARGET_FILE:ror_renderer_public_bridge_entrypoint>"
                "-DROR_RENDERER_PUBLIC_BRIDGE_CASE=${_ror_public_bridge_case}"
                -P
                "${ROR_REPOSITORY_ROOT}/tests/cmake/VerifyRendererPublicBridgeExit.cmake")
        set_tests_properties(
            "ror_renderer_public_bridge_${_ror_public_bridge_case}"
            PROPERTIES TIMEOUT 20)
    endforeach ()
endif ()
add_test(NAME ror_ogre_next_probe_runtime
    COMMAND $<TARGET_FILE:ror_ogre_next_probe>)
add_test(NAME ror_ogre_next_frame_probe_runtime
    COMMAND $<TARGET_FILE:ror_ogre_next_frame_probe>
            --output "${ROR_OGRE_NEXT_CTEST_FRAME_IMAGE}"
            --report "${ROR_OGRE_NEXT_CTEST_FRAME_REPORT}")

if (TARGET ror_ogre_next_vulkan_rt5_smoke)
    set(ROR_OGRE_NEXT_RT5_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-vulkan-rt5-ctest-report.json")
    add_test(NAME ror_ogre_next_vulkan_rt5_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_vulkan_rt5_smoke>
                --report "${ROR_OGRE_NEXT_RT5_CTEST_REPORT}")
    set_tests_properties(
        ror_ogre_next_vulkan_rt5_runtime
        PROPERTIES
            TIMEOUT 120
            SKIP_RETURN_CODE 77
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()

if (TARGET ror_ogre_next_vulkan_rt6_smoke)
    set(ROR_OGRE_NEXT_RT6_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-vulkan-rt6-ctest-report.json")
    add_test(NAME ror_ogre_next_vulkan_rt6_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_vulkan_rt6_smoke>
                --report "${ROR_OGRE_NEXT_RT6_CTEST_REPORT}")
    set_tests_properties(
        ror_ogre_next_vulkan_rt6_runtime
        PROPERTIES
            TIMEOUT 180
            SKIP_RETURN_CODE 77
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()

if (TARGET ror_ogre_next_windows_dxr7_smoke)
    set(ROR_OGRE_NEXT_DXR7_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-windows-dxr7-ctest-report.json")
    set(ROR_OGRE_NEXT_DXR7_CTEST_IMAGE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-windows-dxr7-ctest-frame.ppm")
    set(ROR_OGRE_NEXT_DXR7_CTEST_NONCE
        "0000000000000000000000000000000000000000000000000000000000000000")
    add_test(NAME ror_ogre_next_windows_dxr7_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_windows_dxr7_smoke>
                --shader "${_ror_dxr7_dxil}"
                --report "${ROR_OGRE_NEXT_DXR7_CTEST_REPORT}"
                --image "${ROR_OGRE_NEXT_DXR7_CTEST_IMAGE}"
                --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                --execution-nonce "${ROR_OGRE_NEXT_DXR7_CTEST_NONCE}")
    add_test(NAME ror_ogre_next_windows_dxr7_runtime_repeat
        COMMAND $<TARGET_FILE:ror_ogre_next_windows_dxr7_smoke>
                --shader "${_ror_dxr7_dxil}"
                --report "${ROR_OGRE_NEXT_DXR7_CTEST_REPORT}"
                --image "${ROR_OGRE_NEXT_DXR7_CTEST_IMAGE}"
                --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                --execution-nonce "${ROR_OGRE_NEXT_DXR7_CTEST_NONCE}")
    set_tests_properties(
        ror_ogre_next_windows_dxr7_runtime
        ror_ogre_next_windows_dxr7_runtime_repeat
        PROPERTIES
            TIMEOUT 180
            SKIP_RETURN_CODE 77
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    set_tests_properties(
        ror_ogre_next_windows_dxr7_runtime_repeat
        PROPERTIES DEPENDS ror_ogre_next_windows_dxr7_runtime)
endif ()

if (TARGET ror_ogre_next_frontend_n1_smoke)
    set(ROR_OGRE_NEXT_N1_IMAGE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-n1.ppm")
    set(ROR_OGRE_NEXT_N1_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-n1-report.json")
    set(ROR_OGRE_NEXT_N1_CTEST_IMAGE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-n1-ctest.ppm")
    set(ROR_OGRE_NEXT_N1_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-n1-ctest-report.json")
    set(ROR_OGRE_NEXT_RT4_PBR_IMAGE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1.ppm")
    set(ROR_OGRE_NEXT_RT4_PBR_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1-report.json")
    set(ROR_OGRE_NEXT_RT4_PBR_EVIDENCE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin")
    set(ROR_OGRE_NEXT_RT4_PBR_REFLECTION_EVIDENCE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin")
    set(ROR_OGRE_NEXT_RT4_PBR_COMPOSITOR_EVIDENCE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin")
    set(ROR_OGRE_NEXT_RT4_PBR_REPEAT_ROOT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1-repeat")
    set(ROR_OGRE_NEXT_RT4_PBR_REPEAT_IMAGE
        "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_ROOT}/ror-ogre-next-frontend-rt4-pbr-v1.ppm")
    set(ROR_OGRE_NEXT_RT4_PBR_REPEAT_REPORT
        "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_ROOT}/ror-ogre-next-frontend-rt4-pbr-v1-report.json")
    set(ROR_OGRE_NEXT_RT4_PBR_REPEAT_EVIDENCE
        "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_ROOT}/ror-ogre-next-frontend-rt4-pbr-v1-isolation.bin")
    set(ROR_OGRE_NEXT_RT4_PBR_REPEAT_REFLECTION_EVIDENCE
        "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_ROOT}/ror-ogre-next-frontend-rt4-pbr-v1-reflection.bin")
    set(ROR_OGRE_NEXT_RT4_PBR_REPEAT_COMPOSITOR_EVIDENCE
        "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_ROOT}/ror-ogre-next-frontend-rt4-pbr-v1-hdr-compositor.bin")
    set(ROR_OGRE_NEXT_PSSM_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-report.json")
    set(ROR_OGRE_NEXT_PSSM_EVIDENCE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-isolation.bin")
    set(ROR_OGRE_NEXT_PSSM_EXECUTION_RECEIPT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-execution-receipt.json")
    set(ROR_OGRE_NEXT_PSSM_ATTESTATION
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-attestation.json")
    set(ROR_OGRE_NEXT_PSSM_ARTIFACT_MANIFEST
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-artifact-manifest.json")
    set(ROR_OGRE_NEXT_PSSM_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-ctest-report.json")
    set(ROR_OGRE_NEXT_PSSM_CTEST_EVIDENCE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-ctest-isolation.bin")
    add_custom_target(
        ror_ogre_next_frontend_n1_report
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${ROR_OGRE_NEXT_N1_IMAGE}"
            "${ROR_OGRE_NEXT_N1_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_IMAGE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_EVIDENCE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REFLECTION_EVIDENCE}"
            "${ROR_OGRE_NEXT_RT4_PBR_COMPOSITOR_EVIDENCE}"
        COMMAND
            "${CMAKE_COMMAND}" -E rm -rf
            "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_ROOT}"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_ROOT}"
        COMMAND
            "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
            --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            --output "${ROR_OGRE_NEXT_N1_IMAGE}"
            --report "${ROR_OGRE_NEXT_N1_REPORT}"
        COMMAND
            "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
            --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            --modern-pbr
            --output "${ROR_OGRE_NEXT_RT4_PBR_IMAGE}"
            --report "${ROR_OGRE_NEXT_RT4_PBR_REPORT}"
            --evidence "${ROR_OGRE_NEXT_RT4_PBR_EVIDENCE}"
            --reflection-evidence
                "${ROR_OGRE_NEXT_RT4_PBR_REFLECTION_EVIDENCE}"
            --compositor-evidence
                "${ROR_OGRE_NEXT_RT4_PBR_COMPOSITOR_EVIDENCE}"
        COMMAND
            "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
            --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            --modern-pbr
            --output "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_IMAGE}"
            --report "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REPORT}"
            --evidence "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_EVIDENCE}"
            --reflection-evidence
                "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REFLECTION_EVIDENCE}"
            --compositor-evidence
                "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_COMPOSITOR_EVIDENCE}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPRIMARY_REPORT=${ROR_OGRE_NEXT_RT4_PBR_REPORT}"
            "-DPRIMARY_IMAGE=${ROR_OGRE_NEXT_RT4_PBR_IMAGE}"
            "-DPRIMARY_ISOLATION=${ROR_OGRE_NEXT_RT4_PBR_EVIDENCE}"
            "-DPRIMARY_REFLECTION=${ROR_OGRE_NEXT_RT4_PBR_REFLECTION_EVIDENCE}"
            "-DPRIMARY_COMPOSITOR=${ROR_OGRE_NEXT_RT4_PBR_COMPOSITOR_EVIDENCE}"
            "-DREPEAT_REPORT=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REPORT}"
            "-DREPEAT_IMAGE=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_IMAGE}"
            "-DREPEAT_ISOLATION=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_EVIDENCE}"
            "-DREPEAT_REFLECTION=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REFLECTION_EVIDENCE}"
            "-DREPEAT_COMPOSITOR=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_COMPOSITOR_EVIDENCE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyRt4DeterministicRepeat.cmake"
        DEPENDS
            ror_ogre_next_frontend_n1_package
            ror_renderer_ogre_next_child_runtime
            ror_renderer_ogre_next_window_host_tests
            ror_renderer_ogre_next_window_host_smoke
            ror_ogre_next_window_present_smoke
            ror_ogre_next_window_run_loop_smoke
        BYPRODUCTS
            "${ROR_OGRE_NEXT_N1_IMAGE}"
            "${ROR_OGRE_NEXT_N1_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_IMAGE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_EVIDENCE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REFLECTION_EVIDENCE}"
            "${ROR_OGRE_NEXT_RT4_PBR_COMPOSITOR_EVIDENCE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_IMAGE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_EVIDENCE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REFLECTION_EVIDENCE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_COMPOSITOR_EVIDENCE}"
        COMMENT "Running isolated Ogre-Next N1 and RT4/V1 HDR/SDR frontend smokes"
        VERBATIM)
    add_test(NAME ror_ogre_next_frontend_n1_runtime
        COMMAND "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
                --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                --output "${ROR_OGRE_NEXT_N1_CTEST_IMAGE}"
                --report "${ROR_OGRE_NEXT_N1_CTEST_REPORT}")
    set_tests_properties(ror_ogre_next_frontend_n1_runtime PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ror_ogre_next_frontend_rt4_pbr_v1_runtime
        COMMAND "${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
                --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                --modern-pbr
                --output "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_IMAGE}"
                --report "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REPORT}"
                --evidence "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_EVIDENCE}"
                --reflection-evidence
                    "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REFLECTION_EVIDENCE}"
                --compositor-evidence
                    "${ROR_OGRE_NEXT_RT4_PBR_REPEAT_COMPOSITOR_EVIDENCE}")
    set_tests_properties(
        ror_ogre_next_frontend_rt4_pbr_v1_runtime
        PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ror_ogre_next_frontend_rt4_pbr_v1_repeat
        COMMAND "${CMAKE_COMMAND}"
            "-DPRIMARY_REPORT=${ROR_OGRE_NEXT_RT4_PBR_REPORT}"
            "-DPRIMARY_IMAGE=${ROR_OGRE_NEXT_RT4_PBR_IMAGE}"
            "-DPRIMARY_ISOLATION=${ROR_OGRE_NEXT_RT4_PBR_EVIDENCE}"
            "-DPRIMARY_REFLECTION=${ROR_OGRE_NEXT_RT4_PBR_REFLECTION_EVIDENCE}"
            "-DPRIMARY_COMPOSITOR=${ROR_OGRE_NEXT_RT4_PBR_COMPOSITOR_EVIDENCE}"
            "-DREPEAT_REPORT=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REPORT}"
            "-DREPEAT_IMAGE=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_IMAGE}"
            "-DREPEAT_ISOLATION=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_EVIDENCE}"
            "-DREPEAT_REFLECTION=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_REFLECTION_EVIDENCE}"
            "-DREPEAT_COMPOSITOR=${ROR_OGRE_NEXT_RT4_PBR_REPEAT_COMPOSITOR_EVIDENCE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyRt4DeterministicRepeat.cmake")
    set_tests_properties(
        ror_ogre_next_frontend_rt4_pbr_v1_repeat
        PROPERTIES
            DEPENDS ror_ogre_next_frontend_rt4_pbr_v1_runtime
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
    add_test(NAME ror_ogre_next_frontend_reflection_media_tamper
        COMMAND "${CMAKE_COMMAND}"
            "-DN1_EXECUTABLE=${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
            "-DN1_MEDIA_ROOT=${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            "-DN1_TAMPER_RELATIVE=${ROR_OGRE_NEXT_REFLECTION_MEDIA_TAMPER_RELATIVE}"
            "-DN1_WORK_ROOT=${CMAKE_BINARY_DIR}/reflection-media-tamper"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyReflectionMediaTamper.cmake")
    set_tests_properties(
        ror_ogre_next_frontend_reflection_media_tamper
        PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_test(NAME ror_ogre_next_pssm_shadow_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_pssm_shadow_smoke>
                --media-root "${ROR_OGRE_NEXT_MEDIA_ROOT}"
                --report "${ROR_OGRE_NEXT_PSSM_CTEST_REPORT}"
                --evidence "${ROR_OGRE_NEXT_PSSM_CTEST_EVIDENCE}"
                --execution-challenge
                "0000000000000000000000000000000000000000000000000000000000000000")
    set_tests_properties(
        ror_ogre_next_pssm_shadow_runtime
        PROPERTIES
            RESOURCE_LOCK ror_ogre_next_n1_native_device
            SKIP_RETURN_CODE 77
            TIMEOUT 180
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_custom_target(
        ror_ogre_next_pssm_shadow_report
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${ROR_OGRE_NEXT_PSSM_REPORT}"
            "${ROR_OGRE_NEXT_PSSM_EVIDENCE}"
            "${ROR_OGRE_NEXT_PSSM_EXECUTION_RECEIPT}"
            "${ROR_OGRE_NEXT_PSSM_ATTESTATION}"
            "${ROR_OGRE_NEXT_PSSM_ARTIFACT_MANIFEST}"
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/run_pssm_shadow.py"
            --build-dir "${CMAKE_BINARY_DIR}"
            --executable $<TARGET_FILE:ror_ogre_next_pssm_shadow_smoke>
            --media-root "${ROR_OGRE_NEXT_MEDIA_ROOT}"
            --build-contract
                "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-build-contract.json"
            --report "${ROR_OGRE_NEXT_PSSM_REPORT}"
            --evidence "${ROR_OGRE_NEXT_PSSM_EVIDENCE}"
            --execution-receipt "${ROR_OGRE_NEXT_PSSM_EXECUTION_RECEIPT}"
            --attestation "${ROR_OGRE_NEXT_PSSM_ATTESTATION}"
            --artifact-manifest "${ROR_OGRE_NEXT_PSSM_ARTIFACT_MANIFEST}"
        DEPENDS
            ror_ogre_next_pssm_shadow_smoke
            "${CMAKE_CURRENT_SOURCE_DIR}/run_pssm_shadow.py"
        BYPRODUCTS
            "${ROR_OGRE_NEXT_PSSM_REPORT}"
            "${ROR_OGRE_NEXT_PSSM_EVIDENCE}"
            "${ROR_OGRE_NEXT_PSSM_EXECUTION_RECEIPT}"
            "${ROR_OGRE_NEXT_PSSM_ATTESTATION}"
            "${ROR_OGRE_NEXT_PSSM_ARTIFACT_MANIFEST}"
        COMMENT "Running and atomically attesting the challenged directional PSSM isolation proof"
        VERBATIM)
    add_dependencies(
        ror_ogre_next_frontend_n1_report
        ror_ogre_next_pssm_shadow_report)
    add_test(NAME ror_ogre_next_frontend_hdr_media_tamper
        COMMAND "${CMAKE_COMMAND}"
            "-DN1_EXECUTABLE=${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
            "-DN1_MEDIA_ROOT=${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            "-DN1_TAMPER_RELATIVE=${ROR_OGRE_NEXT_N1_HDR_MEDIA_TAMPER_RELATIVE}"
            "-DN1_WORK_ROOT=${CMAKE_BINARY_DIR}/n1-hdr-media-tamper"
            -DN1_MODERN_PBR=ON
            "-DN1_EXPECTED_INTEGRITY_KIND=HDR compositor"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyN1MediaTamper.cmake")
    set_tests_properties(ror_ogre_next_frontend_hdr_media_tamper PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()

if (TARGET ror_ogre_next_metal_n2_smoke)
    set(ROR_OGRE_NEXT_N2_READBACK
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n2-probe.bin")
    set(ROR_OGRE_NEXT_N2_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n2-report.json")
    set(ROR_OGRE_NEXT_N2_CTEST_READBACK
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n2-ctest-probe.bin")
    set(ROR_OGRE_NEXT_N2_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n2-ctest-report.json")
    add_custom_target(
        ror_ogre_next_metal_n2_report
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${ROR_OGRE_NEXT_N2_READBACK}"
            "${ROR_OGRE_NEXT_N2_REPORT}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DN2_EXECUTABLE=$<TARGET_FILE:ror_ogre_next_metal_n2_smoke>"
            "-DN2_MEDIA_ROOT=${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            "-DN2_OUTPUT=${ROR_OGRE_NEXT_N2_READBACK}"
            "-DN2_REPORT=${ROR_OGRE_NEXT_N2_REPORT}"
            -P "${CMAKE_CURRENT_LIST_DIR}/cmake/RunN2Smoke.cmake"
        DEPENDS
            ror_ogre_next_metal_n2_smoke
            ror_ogre_next_frontend_n1_package
        BYPRODUCTS
            "${ROR_OGRE_NEXT_N2_READBACK}"
            "${ROR_OGRE_NEXT_N2_REPORT}"
        COMMENT "Running Ogre-Next Metal N2 same-device geometry/RT proof"
        VERBATIM)
    add_test(NAME ror_ogre_next_metal_n2_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_metal_n2_smoke>
                --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                --output "${ROR_OGRE_NEXT_N2_CTEST_READBACK}"
                --report "${ROR_OGRE_NEXT_N2_CTEST_REPORT}")
    set_tests_properties(
        ror_ogre_next_metal_n2_runtime
        PROPERTIES
            TIMEOUT 120
            SKIP_RETURN_CODE 77
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()

if (TARGET ror_ogre_next_metal_n3_smoke)
    set(ROR_OGRE_NEXT_N3_RASTER
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n3-raster.bin")
    set(ROR_OGRE_NEXT_N3_CONTRIBUTION
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n3-contribution.bin")
    set(ROR_OGRE_NEXT_N3_HYBRID
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n3-hybrid.bin")
    set(ROR_OGRE_NEXT_N3_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n3-report.json")
    set(ROR_OGRE_NEXT_N3_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n3-ctest-report.json")
    add_custom_target(
        ror_ogre_next_metal_n3_report
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${ROR_OGRE_NEXT_N3_RASTER}"
            "${ROR_OGRE_NEXT_N3_CONTRIBUTION}"
            "${ROR_OGRE_NEXT_N3_HYBRID}"
            "${ROR_OGRE_NEXT_N3_REPORT}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DN3_EXECUTABLE=$<TARGET_FILE:ror_ogre_next_metal_n3_smoke>"
            "-DN3_MEDIA_ROOT=${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            "-DN3_RASTER=${ROR_OGRE_NEXT_N3_RASTER}"
            "-DN3_CONTRIBUTION=${ROR_OGRE_NEXT_N3_CONTRIBUTION}"
            "-DN3_HYBRID=${ROR_OGRE_NEXT_N3_HYBRID}"
            "-DN3_REPORT=${ROR_OGRE_NEXT_N3_REPORT}"
            -P "${CMAKE_CURRENT_LIST_DIR}/cmake/RunN3Smoke.cmake"
        DEPENDS
            ror_ogre_next_metal_n3_smoke
            ror_ogre_next_frontend_n1_package
        BYPRODUCTS
            "${ROR_OGRE_NEXT_N3_RASTER}"
            "${ROR_OGRE_NEXT_N3_CONTRIBUTION}"
            "${ROR_OGRE_NEXT_N3_HYBRID}"
            "${ROR_OGRE_NEXT_N3_REPORT}"
        COMMENT "Running Ogre-Next Metal N3 view-dependent hybrid HDR proof"
        VERBATIM)
    add_test(NAME ror_ogre_next_metal_n3_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_metal_n3_smoke>
                --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
                --report "${ROR_OGRE_NEXT_N3_CTEST_REPORT}")
    set_tests_properties(
        ror_ogre_next_metal_n3_runtime
        PROPERTIES
            TIMEOUT 180
            SKIP_RETURN_CODE 77
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()

if (TARGET ror_ogre_next_metal_n4_directional_shadow_smoke)
    set(ROR_OGRE_NEXT_N4_RASTER
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n4-raster.bin")
    set(ROR_OGRE_NEXT_N4_VISIBILITY
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n4-visibility-r16.bin")
    set(ROR_OGRE_NEXT_N4_LINEAGE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n4-ray-lineage-r32.bin")
    set(ROR_OGRE_NEXT_N4_HYBRID
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n4-hybrid.bin")
    set(ROR_OGRE_NEXT_N4_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n4-directional-shadow-report.json")
    set(ROR_OGRE_NEXT_N4_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-metal-n4-directional-shadow-ctest-report.json")
    add_custom_target(
        ror_ogre_next_metal_n4_directional_shadow_report
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${ROR_OGRE_NEXT_N4_RASTER}"
            "${ROR_OGRE_NEXT_N4_VISIBILITY}"
            "${ROR_OGRE_NEXT_N4_LINEAGE}"
            "${ROR_OGRE_NEXT_N4_HYBRID}"
            "${ROR_OGRE_NEXT_N4_REPORT}"
        COMMAND
            "${CMAKE_COMMAND}"
            "-DN4_EXECUTABLE=$<TARGET_FILE:ror_ogre_next_metal_n4_directional_shadow_smoke>"
            "-DN4_MEDIA_ROOT=${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            "-DN4_RASTER=${ROR_OGRE_NEXT_N4_RASTER}"
            "-DN4_VISIBILITY=${ROR_OGRE_NEXT_N4_VISIBILITY}"
            "-DN4_LINEAGE=${ROR_OGRE_NEXT_N4_LINEAGE}"
            "-DN4_HYBRID=${ROR_OGRE_NEXT_N4_HYBRID}"
            "-DN4_REPORT=${ROR_OGRE_NEXT_N4_REPORT}"
            -P "${CMAKE_CURRENT_LIST_DIR}/cmake/RunN4Smoke.cmake"
        DEPENDS
            ror_ogre_next_metal_n4_directional_shadow_smoke
            ror_ogre_next_frontend_n1_package
        BYPRODUCTS
            "${ROR_OGRE_NEXT_N4_RASTER}"
            "${ROR_OGRE_NEXT_N4_VISIBILITY}"
            "${ROR_OGRE_NEXT_N4_LINEAGE}"
            "${ROR_OGRE_NEXT_N4_HYBRID}"
            "${ROR_OGRE_NEXT_N4_REPORT}"
        COMMENT "Running Ogre-Next Metal N4 directional hard-shadow proof"
        VERBATIM)
    add_test(NAME ror_ogre_next_metal_n4_directional_shadow_runtime
        COMMAND
            $<TARGET_FILE:ror_ogre_next_metal_n4_directional_shadow_smoke>
            --media-root "${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            --report "${ROR_OGRE_NEXT_N4_CTEST_REPORT}")
    set_tests_properties(
        ror_ogre_next_metal_n4_directional_shadow_runtime
        PROPERTIES
            TIMEOUT 180
            SKIP_RETURN_CODE 77
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif ()
