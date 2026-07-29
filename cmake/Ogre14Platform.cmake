# SPDX-License-Identifier: GPL-3.0-or-later

function(ror_select_ogre14_lockfile output_variable system_name processor)
    string(TOLOWER "${system_name}" _ror_system)
    string(TOLOWER "${processor}" _ror_processor)

    if (_ror_system STREQUAL "darwin"
            AND _ror_processor MATCHES "^(arm64|aarch64)$")
        set(_ror_lockfile
            "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock")
    elseif (_ror_system STREQUAL "linux"
            AND _ror_processor MATCHES "^(x86_64|amd64)$")
        set(_ror_lockfile
            "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock")
    elseif (_ror_system STREQUAL "windows"
            AND _ror_processor MATCHES "^(x86_64|amd64)$")
        set(_ror_lockfile
            "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock")
    else ()
        message(FATAL_ERROR
            "ROR_OGRE14 has no pinned dependency graph for "
            "${system_name}/${processor}")
    endif ()

    set(${output_variable} "${_ror_lockfile}" PARENT_SCOPE)
endfunction()
