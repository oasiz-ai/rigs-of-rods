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

include(FetchContent)
find_package(Git REQUIRED)

set(_ror_lock_path "${CMAKE_CURRENT_SOURCE_DIR}/ogre-next.lock.json")
file(READ "${_ror_lock_path}" _ror_lock_json)
string(JSON ROR_OGRE_NEXT_LOCK_SCHEMA GET "${_ror_lock_json}" schema_version)
string(JSON ROR_OGRE_NEXT_REPOSITORY GET "${_ror_lock_json}" repository)
string(JSON ROR_OGRE_NEXT_BRANCH GET "${_ror_lock_json}" branch)
string(JSON ROR_OGRE_NEXT_COMMIT GET "${_ror_lock_json}" commit)
string(JSON ROR_OGRE_NEXT_ARCHIVE_URL GET "${_ror_lock_json}" archive_url)
string(JSON ROR_OGRE_NEXT_ARCHIVE_SHA256 GET "${_ror_lock_json}" archive_sha256)
string(JSON ROR_OGRE_NEXT_LICENSE_SPDX GET "${_ror_lock_json}" license spdx)
string(JSON ROR_OGRE_NEXT_LICENSE_PATH GET "${_ror_lock_json}" license path)
string(JSON ROR_OGRE_NEXT_LICENSE_SHA256 GET "${_ror_lock_json}" license sha256)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_ROOT GET "${_ror_lock_json}" shader_media root)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_LICENSE_EXPRESSION GET
    "${_ror_lock_json}" shader_media license_expression)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_LICENSE_REF GET
    "${_ror_lock_json}" shader_media third_party_notice license_ref)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_PATH GET
    "${_ror_lock_json}" shader_media third_party_notice source_path)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_SHA256 GET
    "${_ror_lock_json}" shader_media third_party_notice source_sha256)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH GET
    "${_ror_lock_json}" shader_media third_party_notice notice_path)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_SHA256 GET
    "${_ror_lock_json}" shader_media third_party_notice notice_sha256)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_UPSTREAM_SOURCE GET
    "${_ror_lock_json}" shader_media third_party_notice upstream_source)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_PAPER_REFERENCE GET
    "${_ror_lock_json}" shader_media third_party_notice paper_reference)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_REQUIRED GET
    "${_ror_lock_json}" shader_media third_party_notice source_and_binary_notice_required)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_PAPER_REQUIRED GET
    "${_ror_lock_json}" shader_media third_party_notice paper_reference_required)
string(JSON ROR_OGRE_NEXT_PATCH_PATH GET "${_ror_lock_json}" patches 0 path)
string(JSON ROR_OGRE_NEXT_PATCH_SHA256 GET "${_ror_lock_json}" patches 0 sha256)
string(JSON ROR_RAPIDJSON_REPOSITORY GET "${_ror_lock_json}" dependencies rapidjson repository)
string(JSON ROR_RAPIDJSON_TAG GET "${_ror_lock_json}" dependencies rapidjson tag)
string(JSON ROR_RAPIDJSON_ARCHIVE_URL GET "${_ror_lock_json}" dependencies rapidjson archive_url)
string(JSON ROR_RAPIDJSON_ARCHIVE_SHA256 GET "${_ror_lock_json}" dependencies rapidjson archive_sha256)
string(JSON ROR_RAPIDJSON_LICENSE_SPDX GET "${_ror_lock_json}" dependencies rapidjson license_spdx)
string(JSON ROR_RAPIDJSON_COMPILED_HEADERS_SPDX GET "${_ror_lock_json}" dependencies rapidjson compiled_headers_spdx)
string(JSON ROR_RAPIDJSON_LICENSE_PATH GET "${_ror_lock_json}" dependencies rapidjson license_path)
string(JSON ROR_RAPIDJSON_LICENSE_SHA256 GET "${_ror_lock_json}" dependencies rapidjson license_sha256)

if (NOT ROR_OGRE_NEXT_LOCK_SCHEMA EQUAL 2 OR
        NOT ROR_OGRE_NEXT_REPOSITORY STREQUAL
        "https://github.com/OGRECave/ogre-next" OR
        NOT ROR_OGRE_NEXT_BRANCH STREQUAL "v3-0" OR
        NOT ROR_OGRE_NEXT_COMMIT STREQUAL
        "37149a802de747f6806996fa3067b0748ecc1084")
    message(FATAL_ERROR "The OGRE-Next lock moved without an integration review")
endif ()
if (NOT ROR_OGRE_NEXT_SHADER_MEDIA_ROOT STREQUAL "Samples/Media/Hlms" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_LICENSE_EXPRESSION STREQUAL
        "MIT AND LicenseRef-Heitz-LTC-Paper-Notice" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_LICENSE_REF STREQUAL
        "LicenseRef-Heitz-LTC-Paper-Notice" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_PATH STREQUAL
        "Samples/Media/Hlms/Pbs/Any/AreaLights_LTC_piece_ps.any" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_SHA256 STREQUAL
        "44146bd7eee4bd6a3bb9428352e89dc20d7690b32c609e62c5f9330678f3a124" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH STREQUAL
        "licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_SHA256 STREQUAL
        "cc942875917be271c92fdc1fdec7a17da92b45dadf42a979b69583003f38bba6" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_UPSTREAM_SOURCE STREQUAL
        "https://github.com/selfshadow/ltc_code/" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_PAPER_REFERENCE STREQUAL
        "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines, ACM TOG 35(4), 2016" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_REQUIRED OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_PAPER_REQUIRED)
    message(FATAL_ERROR
        "The reviewed OGRE-Next shader-media license contract changed")
endif ()
file(SHA256
    "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
    _ror_shader_media_notice_sha256)
if (NOT _ror_shader_media_notice_sha256 STREQUAL
        ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_SHA256)
    message(FATAL_ERROR "The checked-in shader-media notice changed")
endif ()
if (NOT ROR_OGRE_NEXT_ARCHIVE_URL STREQUAL
        "https://github.com/OGRECave/ogre-next/archive/37149a802de747f6806996fa3067b0748ecc1084.tar.gz" OR
        NOT ROR_OGRE_NEXT_LICENSE_PATH STREQUAL "COPYING")
    message(FATAL_ERROR "The OGRE-Next archive or license path contract changed")
endif ()
if (NOT ROR_OGRE_NEXT_LICENSE_SPDX STREQUAL "MIT")
    message(FATAL_ERROR "The OGRE-Next license contract changed")
endif ()
if (NOT ROR_OGRE_NEXT_ARCHIVE_SHA256 STREQUAL
        "1c0be064474da512606d02543be2630b36cdf99f359a9f23edc97eeb410e25b2" OR
        NOT ROR_OGRE_NEXT_LICENSE_SHA256 STREQUAL
        "df6294031f26c4401ce713be0b0b3c5da27c2f1b7278a0d9833d111273174183")
    message(FATAL_ERROR "The reviewed OGRE-Next archive or license hash changed")
endif ()
if (NOT ROR_RAPIDJSON_REPOSITORY STREQUAL
        "https://github.com/Tencent/rapidjson" OR
        NOT ROR_RAPIDJSON_TAG STREQUAL "v1.1.0" OR
        NOT ROR_RAPIDJSON_ARCHIVE_URL STREQUAL
        "https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz" OR
        NOT ROR_RAPIDJSON_LICENSE_PATH STREQUAL "license.txt" OR
        NOT ROR_RAPIDJSON_ARCHIVE_SHA256 STREQUAL
        "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e" OR
        NOT ROR_RAPIDJSON_LICENSE_SPDX STREQUAL
        "MIT AND BSD-3-Clause AND JSON" OR
        NOT ROR_RAPIDJSON_COMPILED_HEADERS_SPDX STREQUAL "MIT" OR
        NOT ROR_RAPIDJSON_LICENSE_SHA256 STREQUAL
        "a140e5d46fe734a1c78f1a3c3ef207871dd75648be71fdda8e309b23ab8b1f32")
    message(FATAL_ERROR "The reviewed RapidJSON pin or license contract changed")
endif ()
file(SHA256
    "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_PATCH_PATH}"
    _ror_patch_sha256)
if (NOT ROR_OGRE_NEXT_PATCH_SHA256 STREQUAL
        "84916d0d1abf61a15d19d2c89a7d9b1a445f1a37a5067a9f8b558395fe10ead1" OR
        NOT _ror_patch_sha256 STREQUAL ROR_OGRE_NEXT_PATCH_SHA256)
    message(FATAL_ERROR "The pinned OGRE-Next adaptation patch changed")
endif ()

set(ROR_OGRE_NEXT_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned OGRE-Next archive")
set(ROR_RAPIDJSON_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned RapidJSON archive")
if (DEFINED FETCHCONTENT_SOURCE_DIR_OGRE_NEXT AND
        NOT FETCHCONTENT_SOURCE_DIR_OGRE_NEXT STREQUAL "")
    message(FATAL_ERROR
        "FETCHCONTENT_SOURCE_DIR_OGRE_NEXT bypasses archive verification and is prohibited")
endif ()
if (DEFINED FETCHCONTENT_SOURCE_DIR_RAPIDJSON AND
        NOT FETCHCONTENT_SOURCE_DIR_RAPIDJSON STREQUAL "")
    message(FATAL_ERROR
        "FETCHCONTENT_SOURCE_DIR_RAPIDJSON bypasses archive verification and is prohibited")
endif ()

if (ROR_OGRE_NEXT_ARCHIVE)
    if (NOT EXISTS "${ROR_OGRE_NEXT_ARCHIVE}")
        message(FATAL_ERROR "Pinned archive does not exist: ${ROR_OGRE_NEXT_ARCHIVE}")
    endif ()
    file(SHA256 "${ROR_OGRE_NEXT_ARCHIVE}" _ror_local_archive_sha256)
    if (NOT _ror_local_archive_sha256 STREQUAL ROR_OGRE_NEXT_ARCHIVE_SHA256)
        message(FATAL_ERROR
            "Pinned archive SHA-256 mismatch: expected "
            "${ROR_OGRE_NEXT_ARCHIVE_SHA256}, got ${_ror_local_archive_sha256}")
    endif ()
    set(_ror_ogre_next_url "${ROR_OGRE_NEXT_ARCHIVE}")
else ()
    set(_ror_ogre_next_url "${ROR_OGRE_NEXT_ARCHIVE_URL}")
endif ()
if (ROR_RAPIDJSON_ARCHIVE)
    if (NOT EXISTS "${ROR_RAPIDJSON_ARCHIVE}")
        message(FATAL_ERROR "Pinned RapidJSON archive does not exist: ${ROR_RAPIDJSON_ARCHIVE}")
    endif ()
    file(SHA256 "${ROR_RAPIDJSON_ARCHIVE}" _ror_local_rapidjson_sha256)
    if (NOT _ror_local_rapidjson_sha256 STREQUAL ROR_RAPIDJSON_ARCHIVE_SHA256)
        message(FATAL_ERROR
            "Pinned RapidJSON SHA-256 mismatch: expected "
            "${ROR_RAPIDJSON_ARCHIVE_SHA256}, got ${_ror_local_rapidjson_sha256}")
    endif ()
    set(_ror_rapidjson_url "${ROR_RAPIDJSON_ARCHIVE}")
else ()
    set(_ror_rapidjson_url "${ROR_RAPIDJSON_ARCHIVE_URL}")
endif ()

if (APPLE)
    if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR
            "OGRE-Next probe policy supports only macOS arm64, got "
            "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
    if (CMAKE_OSX_ARCHITECTURES AND
            NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR
            "OGRE-Next macOS probe must be arm64-only, got "
            "${CMAKE_OSX_ARCHITECTURES}")
    endif ()
    set(ROR_OGRE_NEXT_PLATFORM_POLICY "macos-arm64-metal")
    set(ROR_OGRE_NEXT_RENDERER_TARGET "RenderSystem_Metal")
    set(ROR_OGRE_NEXT_RENDERER_NAME "Metal Rendering Subsystem")
    set(ROR_OGRE_NEXT_DEVICE_OPTION_NAME "Rendering Device")
    set(ROR_OGRE_NEXT_SHADER_SYNTAX "Metal")
    set(ROR_OGRE_NEXT_FRAME_SURFACE_MODE "macos-hidden-native")
    set(ROR_OGRE_NEXT_SIMD_NEON ON)
    set(ROR_OGRE_NEXT_SIMD_SSE2 OFF)
    set(ROR_OGRE_NEXT_SIMD_FAMILY "neon")
    set(_ror_probe_renderer_definition ROR_OGRE_NEXT_PROBE_METAL=1)
elseif (WIN32)
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
            NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        message(FATAL_ERROR
            "OGRE-Next probe policy supports only Windows x64, got "
            "${CMAKE_SYSTEM_PROCESSOR} (${CMAKE_SIZEOF_VOID_P}-byte pointers)")
    endif ()
    set(ROR_OGRE_NEXT_PLATFORM_POLICY "windows-x64-d3d11")
    set(ROR_OGRE_NEXT_RENDERER_TARGET "RenderSystem_Direct3D11")
    set(ROR_OGRE_NEXT_RENDERER_NAME "Direct3D11 Rendering Subsystem")
    set(ROR_OGRE_NEXT_DEVICE_OPTION_NAME "Rendering Device")
    set(ROR_OGRE_NEXT_SHADER_SYNTAX "HLSL")
    set(ROR_OGRE_NEXT_FRAME_SURFACE_MODE "windows-hidden-native")
    set(ROR_OGRE_NEXT_SIMD_NEON OFF)
    set(ROR_OGRE_NEXT_SIMD_SSE2 ON)
    set(ROR_OGRE_NEXT_SIMD_FAMILY "sse2")
    set(_ror_probe_renderer_definition ROR_OGRE_NEXT_PROBE_D3D11=1)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
            NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        message(FATAL_ERROR
            "OGRE-Next probe policy supports only Linux x86_64, got "
            "${CMAKE_SYSTEM_PROCESSOR} (${CMAKE_SIZEOF_VOID_P}-byte pointers)")
    endif ()
    set(ROR_OGRE_NEXT_PLATFORM_POLICY "linux-x86_64-vulkan")
    set(ROR_OGRE_NEXT_RENDERER_TARGET "RenderSystem_Vulkan")
    set(ROR_OGRE_NEXT_RENDERER_NAME "Vulkan Rendering Subsystem")
    set(ROR_OGRE_NEXT_DEVICE_OPTION_NAME "Device")
    set(ROR_OGRE_NEXT_SHADER_SYNTAX "GLSL")
    set(ROR_OGRE_NEXT_FRAME_SURFACE_MODE "linux-null-window-offscreen")
    set(ROR_OGRE_NEXT_SIMD_NEON OFF)
    set(ROR_OGRE_NEXT_SIMD_SSE2 ON)
    set(ROR_OGRE_NEXT_SIMD_FAMILY "sse2")
    set(_ror_probe_renderer_definition ROR_OGRE_NEXT_PROBE_VULKAN=1)
else ()
    message(FATAL_ERROR
        "No reviewed OGRE-Next probe policy for ${CMAKE_SYSTEM_NAME}/"
        "${CMAKE_SYSTEM_PROCESSOR}")
endif ()

if (ROR_OGRE_NEXT_SIMD_NEON)
    set(ROR_OGRE_NEXT_SIMD_NEON_JSON true)
else ()
    set(ROR_OGRE_NEXT_SIMD_NEON_JSON false)
endif ()
if (ROR_OGRE_NEXT_SIMD_SSE2)
    set(ROR_OGRE_NEXT_SIMD_SSE2_JSON true)
else ()
    set(ROR_OGRE_NEXT_SIMD_SSE2_JSON false)
endif ()

# ABI-relevant choices are forced because the probe's output is only meaningful
# when every platform compiles the same reviewed OGRE-Next contract.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OGRE_USE_NEW_PROJECT_NAME ON CACHE BOOL "" FORCE)
set(OGRE_STATIC ON CACHE BOOL "" FORCE)
set(OGRE_BUILD_LIBS_AS_FRAMEWORKS OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_DOUBLE OFF CACHE BOOL "" FORCE)
set(OGRE_DEBUG_LEVEL_DEBUG 3 CACHE STRING "" FORCE)
set(OGRE_DEBUG_LEVEL_RELEASE 0 CACHE STRING "" FORCE)
set(OGRE_EMBED_DEBUG_MODE auto CACHE STRING "" FORCE)
set(OGRE_ASSERT_MODE 0 CACHE STRING "" FORCE)
set(OGRE_CONFIG_ALLOCATOR 0 CACHE STRING "" FORCE)
set(OGRE_CONFIG_CONTAINERS_USE_CUSTOM_ALLOCATOR OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_STRING_USE_CUSTOM_ALLOCATOR OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_MEMTRACK_DEBUG OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_MEMTRACK_RELEASE OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_THREADS 0 CACHE STRING "" FORCE)
set(OGRE_CONFIG_THREAD_PROVIDER none CACHE STRING "" FORCE)
set(OGRE_IDSTRING_USE_128 OFF CACHE BOOL "" FORCE)
set(OGRE_IDSTRING_ALWAYS_READABLE OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_NODE_INHERIT_TRANSFORM OFF CACHE BOOL "" FORCE)
set(OGRE_RESTRICT_ALIASING ON CACHE BOOL "" FORCE)
set(OGRE_SIMD_NEON ${ROR_OGRE_NEXT_SIMD_NEON} CACHE BOOL "" FORCE)
set(OGRE_SIMD_SSE2 ${ROR_OGRE_NEXT_SIMD_SSE2} CACHE BOOL "" FORCE)
add_compile_definitions(OGRE_FLEXIBILITY_LEVEL=0)

# Keep the dependency checkpoint narrow: core + Compositor2, one reviewed
# renderer, and HLMS PBS. Existing RoR/OGRE 14 targets are not part of this
# standalone CMake project.
set(OGRE_BUILD_RENDERSYSTEM_D3D11 OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_RENDERSYSTEM_GL3PLUS OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_RENDERSYSTEM_GLES2 OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_RENDERSYSTEM_METAL OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_RENDERSYSTEM_VULKAN OFF CACHE BOOL "" FORCE)
if (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "macos-arm64-metal")
    set(OGRE_BUILD_RENDERSYSTEM_METAL ON CACHE BOOL "" FORCE)
elseif (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "windows-x64-d3d11")
    set(OGRE_BUILD_RENDERSYSTEM_D3D11 ON CACHE BOOL "" FORCE)
else ()
    set(OGRE_BUILD_RENDERSYSTEM_VULKAN ON CACHE BOOL "" FORCE)
    set(OGRE_CONFIG_UNIX_NO_X11 ON CACHE BOOL "" FORCE)
    set(OGRE_VULKAN_WINDOW_NULL ON CACHE BOOL "" FORCE)
    set(OGRE_VULKAN_WINDOW_XCB OFF CACHE BOOL "" FORCE)

    # Ubuntu packages shaderc as a static archive whose glslang dependency is
    # not carried by OGRE-Next's legacy FindVulkan module. Resolve the packaged
    # CMake target explicitly so every Vulkan probe executable gets the full
    # static dependency closure instead of relying on linker-specific defaults.
    find_package(glslang CONFIG REQUIRED)
    if (NOT TARGET glslang::glslang)
        message(FATAL_ERROR
            "The Linux Vulkan probe requires the packaged glslang target")
    endif ()
endif ()

set(OGRE_BUILD_COMPONENT_HLMS_PBS ON CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_HLMS_UNLIT OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_ATMOSPHERE OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_PLANAR_REFLECTIONS OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_PAGING OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_MESHLODGENERATOR OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_VOLUME OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_PROPERTY OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_OVERLAY OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_SCENE_FORMAT OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_PLUGIN_PFX OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_SAMPLES2 OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_SAMPLES OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_SAMPLES_SOURCE OFF CACHE BOOL "" FORCE)
set(OGRE_COPY_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_FREEIMAGE OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_STBI OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_ZIP OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_JSON ON CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_MESHLOD ON CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_DDS ON CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_TBB_SCHEDULER OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_RENDERDOC_INTEGRATION OFF CACHE BOOL "" FORCE)
set(OGRE_PROFILING_PROVIDER none CACHE STRING "" FORCE)

# OGRE-Next v3-0 still declares a pre-CMake-4 policy baseline internally.
# This compatibility floor is scoped to this standalone dependency build.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# FetchContent verifies the archive only while populating it. Reconfiguring an
# existing build directory could otherwise reuse a locally modified extracted
# source tree while still reporting the pinned archive identity. This guard is
# written before population so failed and successful configurations both
# require an explicit clean recovery.
set(_ror_fresh_configure_guard
    "${CMAKE_BINARY_DIR}/.ror-ogre-next-fresh-configured-v1")
if (EXISTS "${_ror_fresh_configure_guard}")
    message(FATAL_ERROR
        "The verified OGRE-Next probe requires a fresh build directory. "
        "Use tools/run_ogre_next_probe.py --clean-build-dir to recover.")
endif ()
file(WRITE "${_ror_fresh_configure_guard}"
    "ror-ogre-next-fresh-configured-v1\n")

FetchContent_Declare(
    rapidjson
    URL "${_ror_rapidjson_url}"
    URL_HASH "SHA256=${ROR_RAPIDJSON_ARCHIVE_SHA256}"
    SOURCE_SUBDIR ror-header-only-no-cmake
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(rapidjson)
file(SHA256
    "${rapidjson_SOURCE_DIR}/${ROR_RAPIDJSON_LICENSE_PATH}"
    _ror_extracted_rapidjson_license_sha256)
if (NOT _ror_extracted_rapidjson_license_sha256 STREQUAL ROR_RAPIDJSON_LICENSE_SHA256)
    message(FATAL_ERROR
        "The extracted RapidJSON license does not match the pinned contract")
endif ()
set(Rapidjson_HOME "${rapidjson_SOURCE_DIR}" CACHE PATH "" FORCE)

FetchContent_Declare(
    ogre_next
    URL "${_ror_ogre_next_url}"
    URL_HASH "SHA256=${ROR_OGRE_NEXT_ARCHIVE_SHA256}"
    PATCH_COMMAND
        "${GIT_EXECUTABLE}" apply --unidiff-zero --whitespace=nowarn
        "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_PATCH_PATH}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ogre_next)

if (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan")
    # Keep glslang after Vulkan/shaderc on the final static link line. Appending
    # to the renderer's public closure also covers every present and future
    # probe executable without duplicating a platform-only library list.
    set_property(
        TARGET ${ROR_OGRE_NEXT_RENDERER_TARGET}
        APPEND PROPERTY INTERFACE_LINK_LIBRARIES glslang::glslang)
endif ()

file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_LICENSE_PATH}"
    _ror_extracted_license_sha256)
if (NOT _ror_extracted_license_sha256 STREQUAL ROR_OGRE_NEXT_LICENSE_SHA256)
    message(FATAL_ERROR
        "The extracted OGRE-Next license does not match the pinned contract")
endif ()
file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_PATH}"
    _ror_extracted_shader_media_source_sha256)
if (NOT _ror_extracted_shader_media_source_sha256 STREQUAL
        ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_SHA256)
    message(FATAL_ERROR
        "The extracted OGRE-Next shader-media notice source changed")
endif ()

foreach (_ror_required_target IN ITEMS
        OgreNextMain OgreNextHlmsPbs ${ROR_OGRE_NEXT_RENDERER_TARGET})
    if (NOT TARGET ${_ror_required_target})
        message(FATAL_ERROR
            "Required OGRE-Next capability target is unavailable: "
            "${_ror_required_target}")
    endif ()
endforeach ()

if (NOT OGRE_STATIC OR NOT OGRE_USE_NEW_PROJECT_NAME OR OGRE_CONFIG_DOUBLE OR
        NOT OGRE_BUILD_COMPONENT_HLMS_PBS)
    message(FATAL_ERROR "OGRE-Next changed a required ABI/capability option")
endif ()

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
