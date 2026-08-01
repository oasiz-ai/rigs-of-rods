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
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
get_filename_component(ROR_REPOSITORY_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ROR_SOURCE_REPOSITORY "https://github.com/oasiz-ai/rigs-of-rods")
if (DEFINED ENV{ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT} AND
        NOT "$ENV{ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT}" STREQUAL "")
    set(ROR_SOURCE_COMMIT "$ENV{ROR_OGRE_NEXT_EXPECTED_ROR_COMMIT}")
    set(_ror_git_commit_result 0)
else ()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${ROR_REPOSITORY_ROOT}" rev-parse HEAD
        RESULT_VARIABLE _ror_git_commit_result
        OUTPUT_VARIABLE ROR_SOURCE_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
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
    ror_ogre_next_pssm_source_closure_verify
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/verify_pssm_shadow_source_closure.py"
        --lock
        "${CMAKE_CURRENT_SOURCE_DIR}/ogre-next-pssm-shadow-v1.lock.json"
        --canonical-lock
        "${CMAKE_CURRENT_SOURCE_DIR}/ogre-next.lock.json"
        --source-root "${ogre_next_SOURCE_DIR}"
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
        "${_ror_render_root}/ogrenext/OgreNextN1Policy.cpp"
        "${_ror_render_root}/ogrenext/OgreNextPssmShadowPolicy.cpp")
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
        target_compile_definitions(
            ror_ogre_next_frontend_n1
            PRIVATE ROR_OGRE_NEXT_N2_TEST_SEAM=1)
    elseif (WIN32)
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_D3D11=1)
    else ()
        set(_ror_n1_renderer_definition ROR_OGRE_NEXT_N1_VULKAN=1)
    endif ()
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
    set_target_properties(
        ror_ogre_next_frontend_n1
        PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES)

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
    add_dependencies(
        ror_ogre_next_frontend_n1
        ror_ogre_next_pssm_source_closure_verify)

    set(_ror_n1_targets
        ror_ogre_next_n1_contract
        ror_ogre_next_frontend_n1
        ror_ogre_next_frontend_n1_smoke
        ror_ogre_next_pssm_shadow_smoke)
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
            "${_ror_render_root}/ogrenext/OgreNextDxr7Contract.cpp")
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
            source/main/gfx/render
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
        execute_process(
            COMMAND git rev-parse --verify HEAD
            WORKING_DIRECTORY "${_ror_n2_repository_root}"
            RESULT_VARIABLE _ror_n2_commit_result
            OUTPUT_VARIABLE _ror_n2_source_commit
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if (NOT _ror_n2_commit_result EQUAL 0)
            set(_ror_n2_source_commit "unknown")
        endif ()
        execute_process(
            COMMAND git symbolic-ref --short HEAD
            WORKING_DIRECTORY "${_ror_n2_repository_root}"
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
                ROR_OGRE_NEXT_N2_TEST_SEAM=1
                ROR_OGRE_NEXT_N2_SOURCE_REPOSITORY="${ROR_SOURCE_REPOSITORY}"
                ROR_OGRE_NEXT_N2_SOURCE_COMMIT="${_ror_n2_source_commit}"
                ROR_OGRE_NEXT_N2_SOURCE_REF="${_ror_n2_source_ref}"
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
                ROR_OGRE_NEXT_N3_SOURCE_COMMIT="${_ror_n2_source_commit}"
                ROR_OGRE_NEXT_N3_SOURCE_REF="${_ror_n2_source_ref}"
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
        list(APPEND _ror_n1_targets
            ror_ogre_next_metal_n2_smoke
            ror_ogre_next_metal_n3_smoke)
    endif ()

    set(ROR_OGRE_NEXT_N1_PACKAGE_ROOT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-n1-package")
    set(ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE}")
    set(ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/bin/$<TARGET_FILE_NAME:ror_ogre_next_frontend_n1_smoke>")
    set(ROR_OGRE_NEXT_N1_PACKAGE_STAMP
        "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/.stage-v3")
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
                "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
        COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
                "${ROR_OGRE_NEXT_N1_PACKAGE_ROOT}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}")
    set(_ror_n1_package_dependencies
        "${CMAKE_CURRENT_SOURCE_DIR}/../../COPYING"
        "${ROR_OGRE_NEXT_PACKAGE_OGRE_LICENSE_SOURCE}"
        "${ROR_OGRE_NEXT_PACKAGE_RAPIDJSON_LICENSE_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}")
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
    set(ROR_OGRE_NEXT_RT4_PBR_CTEST_IMAGE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1-ctest.ppm")
    set(ROR_OGRE_NEXT_RT4_PBR_CTEST_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1-ctest-report.json")
    set(ROR_OGRE_NEXT_RT4_PBR_CTEST_EVIDENCE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-frontend-rt4-pbr-v1-ctest-isolation.bin")
    set(ROR_OGRE_NEXT_PSSM_REPORT
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-report.json")
    set(ROR_OGRE_NEXT_PSSM_EVIDENCE
        "${CMAKE_BINARY_DIR}/ror-ogre-next-pssm-shadow-isolation.bin")
    add_custom_target(
        ror_ogre_next_frontend_n1_report
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f
            "${ROR_OGRE_NEXT_N1_IMAGE}"
            "${ROR_OGRE_NEXT_N1_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_IMAGE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_EVIDENCE}"
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
        DEPENDS ror_ogre_next_frontend_n1_package
        BYPRODUCTS
            "${ROR_OGRE_NEXT_N1_IMAGE}"
            "${ROR_OGRE_NEXT_N1_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_IMAGE}"
            "${ROR_OGRE_NEXT_RT4_PBR_REPORT}"
            "${ROR_OGRE_NEXT_RT4_PBR_EVIDENCE}"
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
                --output "${ROR_OGRE_NEXT_RT4_PBR_CTEST_IMAGE}"
                --report "${ROR_OGRE_NEXT_RT4_PBR_CTEST_REPORT}"
                --evidence "${ROR_OGRE_NEXT_RT4_PBR_CTEST_EVIDENCE}")
    set_tests_properties(
        ror_ogre_next_frontend_rt4_pbr_v1_runtime
        PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ror_ogre_next_frontend_n1_media_tamper
        COMMAND "${CMAKE_COMMAND}"
            "-DN1_EXECUTABLE=${ROR_OGRE_NEXT_N1_PACKAGE_EXECUTABLE}"
            "-DN1_MEDIA_ROOT=${ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_ROOT}"
            "-DN1_TAMPER_RELATIVE=${ROR_OGRE_NEXT_N1_MEDIA_TAMPER_RELATIVE}"
            "-DN1_WORK_ROOT=${CMAKE_BINARY_DIR}/n1-media-tamper"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyN1MediaTamper.cmake")
    set_tests_properties(ror_ogre_next_frontend_n1_media_tamper PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ror_ogre_next_pssm_shadow_runtime
        COMMAND $<TARGET_FILE:ror_ogre_next_pssm_shadow_smoke>
                --media-root "${ROR_OGRE_NEXT_MEDIA_ROOT}"
                --report "${ROR_OGRE_NEXT_PSSM_REPORT}"
                --evidence "${ROR_OGRE_NEXT_PSSM_EVIDENCE}")
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
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir "${CMAKE_BINARY_DIR}"
            --build-config Release
            --output-on-failure
            -R "^ror_ogre_next_pssm_shadow_runtime$"
        DEPENDS ror_ogre_next_pssm_shadow_smoke
        BYPRODUCTS
            "${ROR_OGRE_NEXT_PSSM_REPORT}"
            "${ROR_OGRE_NEXT_PSSM_EVIDENCE}"
        COMMENT "Running the opt-in directional PSSM isolation proof"
        VERBATIM)
    add_dependencies(
        ror_ogre_next_frontend_n1_report
        ror_ogre_next_pssm_shadow_report)
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
