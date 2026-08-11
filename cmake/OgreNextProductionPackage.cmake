# SPDX-License-Identifier: GPL-3.0-or-later

include_guard(GLOBAL)
include(ExternalProject)

# Build the pinned, static OgreNext child in an isolated CMake project.  OGRE
# 14 and OgreNext intentionally never share a target graph or ABI boundary;
# the only output consumed by the game package is the verified product stage.
function(ror_add_ogre_next_production_package)
    if (TARGET ror_ogre_next_product_external)
        message(FATAL_ERROR "The OgreNext production package was added twice")
    endif ()

    set(_ror_product_source "${CMAKE_SOURCE_DIR}/tools/ogre_next_probe")
    set(_ror_product_binary
        "${CMAKE_BINARY_DIR}/ogre-next-product-build")
    set(_ror_product_stage
        "${CMAKE_BINARY_DIR}/ogre-next-product-stage")
    if (WIN32)
        set(_ror_product_child_name "RoR-OgreNext.exe")
    else ()
        set(_ror_product_child_name "RoR-OgreNext")
    endif ()
    set(_ror_product_child
        "${_ror_product_stage}/${_ror_product_child_name}")
    set(_ror_product_completion
        "${_ror_product_stage}/.ror-ogre-next-product-complete.json")

    set(_ror_product_cmake_args
        "-DCMAKE_BUILD_TYPE=Release"
        # The pinned probe intentionally rejects an in-place CMake
        # reconfiguration because that would weaken its fresh-build source
        # closure. Ninja/Make must therefore never synthesize a regeneration
        # edge for this immutable ExternalProject after the one authoritative
        # configure step.
        "-DCMAKE_SUPPRESS_REGENERATION=ON"
        "-DROR_OGRE_NEXT_PROBE=ON"
        "-DROR_OGRE_NEXT_PRODUCT_STAGE=ON"
        "-DROR_OGRE_NEXT_PRODUCT_STAGE_ROOT=${_ror_product_stage}"
        "-DROR_OGRE_NEXT_FRONTEND_N1=ON"
        "-DROR_OGRE_NEXT_VULKAN_RT5=OFF"
        "-DROR_OGRE_NEXT_VULKAN_RT6=OFF"
        "-DROR_OGRE_NEXT_WINDOWS_DXR7=OFF")
    foreach (_ror_product_forward IN ITEMS
            CMAKE_C_COMPILER
            CMAKE_CXX_COMPILER
            CMAKE_TOOLCHAIN_FILE
            CMAKE_OSX_ARCHITECTURES
            CMAKE_OSX_DEPLOYMENT_TARGET
            CMAKE_SYSROOT)
        if (DEFINED ${_ror_product_forward} AND
                NOT "${${_ror_product_forward}}" STREQUAL "")
            list(APPEND _ror_product_cmake_args
                "-D${_ror_product_forward}=${${_ror_product_forward}}")
        endif ()
    endforeach ()

    foreach (_ror_archive_variable IN ITEMS
            ROR_OGRE_NEXT_ARCHIVE
            ROR_RAPIDJSON_ARCHIVE
            ROR_FREETYPE_ARCHIVE
            ROR_OGRE_NEXT_SHADERC_ARCHIVE
            ROR_OGRE_NEXT_GLSLANG_ARCHIVE
            ROR_OGRE_NEXT_SPIRV_TOOLS_ARCHIVE
            ROR_OGRE_NEXT_SPIRV_HEADERS_ARCHIVE)
        set(${_ror_archive_variable} "" CACHE FILEPATH
            "Optional local pinned archive forwarded to the OgreNext product build")
        if (NOT "${${_ror_archive_variable}}" STREQUAL "")
            if (NOT IS_ABSOLUTE "${${_ror_archive_variable}}" OR
                    NOT EXISTS "${${_ror_archive_variable}}" OR
                    IS_DIRECTORY "${${_ror_archive_variable}}")
                message(FATAL_ERROR
                    "${_ror_archive_variable} must name one absolute archive file")
            endif ()
            list(APPEND _ror_product_cmake_args
                "-D${_ror_archive_variable}=${${_ror_archive_variable}}")
        endif ()
    endforeach ()

    ExternalProject_Add(
        ror_ogre_next_product_external
        SOURCE_DIR "${_ror_product_source}"
        BINARY_DIR "${_ror_product_binary}"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        CMAKE_ARGS ${_ror_product_cmake_args}
        BUILD_COMMAND
            "${CMAKE_COMMAND}" --build <BINARY_DIR>
            --target ror_ogre_next_product_stage
            --config Release
        INSTALL_COMMAND ""
        TEST_COMMAND ""
        BUILD_ALWAYS TRUE
        BUILD_BYPRODUCTS
            "${_ror_product_child}"
            "${_ror_product_completion}"
        USES_TERMINAL_CONFIGURE TRUE
        USES_TERMINAL_BUILD TRUE)
    set_property(
        TARGET ror_ogre_next_product_external
        PROPERTY FOLDER "Packaging")

    set(ROR_OGRE_NEXT_PRODUCT_TARGET
        ror_ogre_next_product_external PARENT_SCOPE)
    set(ROR_OGRE_NEXT_PRODUCT_STAGE_ROOT
        "${_ror_product_stage}" PARENT_SCOPE)
    set(ROR_OGRE_NEXT_PRODUCT_CHILD
        "${_ror_product_child}" PARENT_SCOPE)
    set(ROR_OGRE_NEXT_PRODUCT_COMPLETION
        "${_ror_product_completion}" PARENT_SCOPE)
endfunction()
