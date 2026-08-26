# SPDX-License-Identifier: GPL-3.0-or-later

function(ror_disable_ogre14_dependent_options)
    foreach (_ror_option IN ITEMS
            ROR_RENDERER_PUBLIC_LAUNCHER
            ROR_OGRE_NEXT_PRODUCTION_PACKAGE
            ROR_OGRE_NEXT_DEMO_ADMISSION)
        get_property(_ror_option_help CACHE ${_ror_option} PROPERTY HELPSTRING)
        if (NOT _ror_option_help)
            set(_ror_option_help
                "Disabled by the explicit ROR_OGRE14=OFF developer lane")
        endif ()
        set(${_ror_option} OFF CACHE BOOL
            "${_ror_option_help}"
            FORCE)
        set(${_ror_option} OFF PARENT_SCOPE)
    endforeach ()
    unset(_ror_option_help)
endfunction()

function(_ror_is_ogre_graph_conan_option output_variable option_value)
    if (option_value MATCHES "^[^:]*:ogre14=(True|False)$"
            OR option_value MATCHES
                "^openal-soft(/[^:]*)?:thread_sanitizer=(True|False)$")
        set(${output_variable} TRUE PARENT_SCOPE)
    else ()
        set(${output_variable} FALSE PARENT_SCOPE)
    endif ()
endfunction()

function(_ror_is_ogre_graph_lockfile output_variable lockfile_value)
    if (lockfile_value MATCHES
            "(^|[/\\\\])ror-ogre(11|14)-[^/\\\\]+\\.lock$")
        set(${output_variable} TRUE PARENT_SCOPE)
    else ()
        set(${output_variable} FALSE PARENT_SCOPE)
    endif ()
endfunction()

function(ror_sanitize_ogre_graph_conan_install_args output_variable)
    set(_ror_sanitized_args)
    set(_ror_pending_flag)

    foreach (_ror_arg IN LISTS ARGN)
        if (_ror_pending_flag)
            if (_ror_pending_flag STREQUAL "--lockfile")
                _ror_is_ogre_graph_lockfile(_ror_drop_arg "${_ror_arg}")
            else ()
                _ror_is_ogre_graph_conan_option(_ror_drop_arg "${_ror_arg}")
            endif ()

            if (NOT _ror_drop_arg)
                list(APPEND _ror_sanitized_args
                    "${_ror_pending_flag}" "${_ror_arg}")
            endif ()
            unset(_ror_pending_flag)
            unset(_ror_drop_arg)
            continue()
        endif ()

        if (_ror_arg STREQUAL "--lockfile"
                OR _ror_arg STREQUAL "-o"
                OR _ror_arg STREQUAL "--options")
            set(_ror_pending_flag "${_ror_arg}")
        elseif (_ror_arg MATCHES "^--lockfile=(.*)$")
            _ror_is_ogre_graph_lockfile(
                _ror_drop_arg "${CMAKE_MATCH_1}")
            if (NOT _ror_drop_arg)
                list(APPEND _ror_sanitized_args "${_ror_arg}")
            endif ()
            unset(_ror_drop_arg)
        elseif (_ror_arg MATCHES "^(-o|--options)=(.*)$")
            _ror_is_ogre_graph_conan_option(
                _ror_drop_arg "${CMAKE_MATCH_2}")
            if (NOT _ror_drop_arg)
                list(APPEND _ror_sanitized_args "${_ror_arg}")
            endif ()
            unset(_ror_drop_arg)
        else ()
            list(APPEND _ror_sanitized_args "${_ror_arg}")
        endif ()
    endforeach ()

    # Preserve an incomplete caller-provided pair. Conan will diagnose it;
    # silently deleting an unrelated malformed argument would hide evidence.
    if (_ror_pending_flag)
        list(APPEND _ror_sanitized_args "${_ror_pending_flag}")
    endif ()

    set(${output_variable} "${_ror_sanitized_args}" PARENT_SCOPE)
endfunction()

function(ror_validate_ogre_legacy_configuration
        generator_is_multi_config build_type)
    if (generator_is_multi_config)
        message(FATAL_ERROR
            "The Ogre 1.11 developer lane requires a single-config generator")
    endif ()
    if (NOT "${build_type}" STREQUAL "Release")
        message(FATAL_ERROR
            "The Ogre 1.11 developer lane requires CMAKE_BUILD_TYPE=Release")
    endif ()
endfunction()

function(ror_select_ogre_legacy_lockfile output_variable system_name processor)
    string(TOLOWER "${system_name}" _ror_system)
    string(TOLOWER "${processor}" _ror_processor)

    if (_ror_system STREQUAL "darwin"
            AND _ror_processor MATCHES "^(arm64|aarch64)$")
        set(_ror_lockfile
            "cmake/conan/locks/ror-ogre11-macos-arm64-release.lock")
    elseif (_ror_system STREQUAL "linux"
            AND _ror_processor MATCHES "^(x86_64|amd64)$")
        set(_ror_lockfile
            "cmake/conan/locks/ror-ogre11-linux-x86_64-release.lock")
    elseif (_ror_system STREQUAL "windows"
            AND _ror_processor MATCHES "^(x86_64|amd64)$")
        set(_ror_lockfile
            "cmake/conan/locks/ror-ogre11-windows-x86_64-release.lock")
    else ()
        message(FATAL_ERROR
            "The Ogre 1.11 developer lane has no pinned dependency graph for "
            "${system_name}/${processor}")
    endif ()

    set(${output_variable} "${_ror_lockfile}" PARENT_SCOPE)
endfunction()
