cmake_minimum_required(VERSION 3.24)
project(ror_ogre_next_nicemetal_visible LANGUAGES CXX)

if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    message(FATAL_ERROR "The first visible NiceMetal proof is macOS arm64 Metal only")
endif()
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "The visible NiceMetal proof requires CMAKE_BUILD_TYPE=Release")
endif()
if(NOT IS_ABSOLUTE "${OGRE_NEXT_PROBE_BUILD_ROOT}")
    message(FATAL_ERROR "OGRE_NEXT_PROBE_BUILD_ROOT must be absolute")
endif()

set(_ogre_source "${OGRE_NEXT_PROBE_BUILD_ROOT}/_deps/ogre_next-src")
set(_ogre_binary "${OGRE_NEXT_PROBE_BUILD_ROOT}/_deps/ogre_next-build")
set(_ogre_lib "${_ogre_binary}/lib/macosx")
foreach(_required IN ITEMS
        "${_ogre_source}/Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any"
        "${_ogre_binary}/include/OgreBuildSettings.h"
        "${_ogre_lib}/libOgreNextHlmsPbsStatic.a"
        "${_ogre_lib}/libRenderSystem_MetalStatic.a"
        "${_ogre_lib}/libOgreNextMainStatic.a")
    if(NOT EXISTS "${_required}" OR IS_SYMLINK "${_required}")
        message(FATAL_ERROR "Pinned Ogre-Next build input is missing or a symlink: ${_required}")
    endif()
endforeach()

get_filename_component(_repository_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(_custom_media "${CMAKE_CURRENT_LIST_DIR}/media")

add_executable(ror_ogre_next_nicemetal_visible_probe
    src/nicemetal_visible_probe.cpp
    "${_repository_root}/source/main/gfx/render/ogrenext/OgreNextUvAffinePbs.cpp")
target_compile_features(ror_ogre_next_nicemetal_visible_probe PRIVATE cxx_std_17)
target_compile_definitions(ror_ogre_next_nicemetal_visible_probe PRIVATE
    OGRE_FLEXIBILITY_LEVEL=0
    ROR_OGRE_NEXT_PROBE_METAL=1
    ROR_NICEMETAL_OGRE_MEDIA_ROOT="${_ogre_source}/Samples/Media"
    ROR_NICEMETAL_CUSTOM_MEDIA_ROOT="${_custom_media}")
target_include_directories(ror_ogre_next_nicemetal_visible_probe PRIVATE
    "${_repository_root}/source/main/gfx/render/ogrenext"
    "${_ogre_source}/OgreMain/include"
    "${_ogre_binary}/include"
    "${_ogre_source}/Components/Hlms/Common/include"
    "${_ogre_source}/Components/Hlms/Pbs/include"
    "${_ogre_source}/RenderSystems/Metal/include"
    "${_ogre_source}/RenderSystems/Metal/include/Windowing/OSX")
target_link_libraries(ror_ogre_next_nicemetal_visible_probe PRIVATE
    "${_ogre_lib}/libOgreNextHlmsPbsStatic.a"
    "${_ogre_lib}/libRenderSystem_MetalStatic.a"
    "${_ogre_lib}/libOgreNextMainStatic.a"
    "-framework Metal"
    "-framework AppKit"
    "-framework QuartzCore")
