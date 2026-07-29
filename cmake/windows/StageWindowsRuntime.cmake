# SPDX-License-Identifier: GPL-3.0-or-later

foreach (_ror_required_variable IN ITEMS
        ROR_WINDOWS_RUNTIME_OUTPUT_DIRECTORY
        ROR_WINDOWS_EXPECTED_PLUGINS
        ROR_WINDOWS_PLUGIN_BINARIES_USE_DEBUG_SUFFIX)
    if (NOT DEFINED ${_ror_required_variable}
            OR "${${_ror_required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "StageWindowsRuntime requires ${_ror_required_variable}")
    endif ()
endforeach ()

if (NOT IS_ABSOLUTE "${ROR_WINDOWS_RUNTIME_OUTPUT_DIRECTORY}"
        OR NOT IS_DIRECTORY "${ROR_WINDOWS_RUNTIME_OUTPUT_DIRECTORY}")
    message(FATAL_ERROR
        "ROR_WINDOWS_RUNTIME_OUTPUT_DIRECTORY is not an absolute directory: "
        "${ROR_WINDOWS_RUNTIME_OUTPUT_DIRECTORY}")
endif ()
if (NOT ROR_WINDOWS_PLUGIN_BINARIES_USE_DEBUG_SUFFIX)
    message(FATAL_ERROR
        "The OGRE 14 Windows runtime contract requires Debug plugin suffixes")
endif ()
if (NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL "Debug"
        AND NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL "Release")
    message(FATAL_ERROR
        "The OGRE 14 Windows runtime supports only Debug and Release, not "
        "${CMAKE_INSTALL_CONFIG_NAME}")
endif ()

set(_ror_plugin_suffix "")
if (CMAKE_INSTALL_CONFIG_NAME STREQUAL "Debug")
    set(_ror_plugin_suffix "_d")
endif ()

set(_ror_expected_plugin_dlls)
foreach (_ror_plugin IN LISTS ROR_WINDOWS_EXPECTED_PLUGINS)
    if (NOT _ror_plugin MATCHES "^[A-Za-z0-9_-]+$")
        message(FATAL_ERROR
            "Unsafe OGRE plugin family in Windows runtime contract: "
            "${_ror_plugin}")
    endif ()
    list(APPEND _ror_expected_plugin_dlls
        "${_ror_plugin}${_ror_plugin_suffix}.dll")
endforeach ()
list(REMOVE_DUPLICATES _ror_expected_plugin_dlls)

file(GLOB _ror_runtime_dlls
    LIST_DIRECTORIES FALSE
    "${ROR_WINDOWS_RUNTIME_OUTPUT_DIRECTORY}/*.dll")
set(_ror_found_plugin_dlls)
set(_ror_runtime_dlls_to_install)
foreach (_ror_runtime_dll IN LISTS _ror_runtime_dlls)
    get_filename_component(_ror_runtime_dll_name
        "${_ror_runtime_dll}" NAME)
    if (_ror_runtime_dll_name MATCHES
            "^(Codec_|Plugin_|RenderSystem_).+\\.dll$")
        list(FIND _ror_expected_plugin_dlls
            "${_ror_runtime_dll_name}" _ror_expected_plugin_index)
        if (_ror_expected_plugin_index EQUAL -1)
            continue ()
        endif ()
        list(APPEND _ror_found_plugin_dlls
            "${_ror_runtime_dll_name}")
    endif ()
    list(APPEND _ror_runtime_dlls_to_install
        "${_ror_runtime_dll}")
endforeach ()

list(SORT _ror_expected_plugin_dlls)
list(SORT _ror_found_plugin_dlls)
if (NOT _ror_found_plugin_dlls STREQUAL _ror_expected_plugin_dlls)
    message(FATAL_ERROR
        "Windows OGRE plugin DLL set is incomplete: expected "
        "'${_ror_expected_plugin_dlls}', found '${_ror_found_plugin_dlls}'")
endif ()

set(_ror_windows_install_root
    "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
foreach (_ror_runtime_dll IN LISTS _ror_runtime_dlls_to_install)
    file(INSTALL
        DESTINATION "${_ror_windows_install_root}"
        TYPE FILE
        FILES "${_ror_runtime_dll}")
endforeach ()
