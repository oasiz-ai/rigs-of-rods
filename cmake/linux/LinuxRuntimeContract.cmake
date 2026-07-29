# SPDX-License-Identifier: GPL-3.0-or-later

include_guard(GLOBAL)

function(
        ror_linux_ogre14_path_is_within
        output_result
        root_path
        candidate_path)
    get_filename_component(_ror_real_root "${root_path}" REALPATH)
    get_filename_component(_ror_real_candidate "${candidate_path}" REALPATH)
    file(
        RELATIVE_PATH
        _ror_relative_candidate
        "${_ror_real_root}"
        "${_ror_real_candidate}")
    if (IS_ABSOLUTE "${_ror_relative_candidate}"
            OR _ror_relative_candidate MATCHES "^\\.\\.(/|$)")
        set(_ror_is_within OFF)
    else ()
        set(_ror_is_within ON)
    endif ()
    set(${output_result} "${_ror_is_within}" PARENT_SCOPE)
endfunction()

function(
        ror_linux_ogre14_validate_symlink_chain
        root_directory
        source_path)
    if (NOT IS_ABSOLUTE "${root_directory}"
            OR NOT IS_DIRECTORY "${root_directory}")
        message(FATAL_ERROR
            "Linux OGRE 14 symlink root is not an absolute directory: "
            "${root_directory}")
    endif ()
    if (NOT IS_ABSOLUTE "${source_path}"
            OR (NOT EXISTS "${source_path}"
                AND NOT IS_SYMLINK "${source_path}"))
        message(FATAL_ERROR
            "Linux OGRE 14 symlink source is unavailable: ${source_path}")
    endif ()

    set(_ror_link_path "${source_path}")
    set(_ror_seen_links)
    while (IS_SYMLINK "${_ror_link_path}")
        list(FIND _ror_seen_links "${_ror_link_path}" _ror_seen_link_index)
        if (NOT _ror_seen_link_index EQUAL -1)
            message(FATAL_ERROR
                "Linux OGRE 14 shared-object symlink cycle starts at "
                "${source_path}")
        endif ()
        list(APPEND _ror_seen_links "${_ror_link_path}")
        file(READ_SYMLINK "${_ror_link_path}" _ror_link_target)
        if (IS_ABSOLUTE "${_ror_link_target}")
            message(FATAL_ERROR
                "Linux OGRE 14 shared-object symlink is absolute: "
                "${_ror_link_path} -> ${_ror_link_target}")
        endif ()
        get_filename_component(
            _ror_link_directory
            "${_ror_link_path}"
            DIRECTORY)
        get_filename_component(
            _ror_link_path
            "${_ror_link_directory}/${_ror_link_target}"
            ABSOLUTE)
        ror_linux_ogre14_path_is_within(
            _ror_link_is_contained
            "${root_directory}"
            "${_ror_link_path}")
        if (NOT _ror_link_is_contained)
            message(FATAL_ERROR
                "Linux OGRE 14 shared-object symlink escapes its root: "
                "${source_path}")
        endif ()
    endwhile ()
    if (NOT EXISTS "${_ror_link_path}"
            OR IS_DIRECTORY "${_ror_link_path}")
        message(FATAL_ERROR
            "Linux OGRE 14 shared-object symlink chain is broken: "
            "${source_path}")
    endif ()
endfunction()

function(
        ror_linux_ogre14_validate_plugins_config
        output_plugins
        config_path
        expected_plugin_folder
        expected_plugins)
    if (NOT IS_ABSOLUTE "${config_path}" OR NOT EXISTS "${config_path}")
        message(FATAL_ERROR
            "Linux OGRE 14 plugins config is not an absolute file: "
            "${config_path}")
    endif ()

    file(STRINGS "${config_path}" _ror_config_lines ENCODING UTF-8)
    set(_ror_plugin_folders)
    set(_ror_plugins)
    foreach (_ror_config_line IN LISTS _ror_config_lines)
        string(STRIP "${_ror_config_line}" _ror_config_line)
        if (_ror_config_line STREQUAL ""
                OR _ror_config_line MATCHES "^#")
            continue()
        endif ()

        if (_ror_config_line MATCHES
                "^PluginFolder[ \t]*=[ \t]*(.*)$")
            string(STRIP "${CMAKE_MATCH_1}" _ror_plugin_folder)
            list(APPEND _ror_plugin_folders "${_ror_plugin_folder}")
        elseif (_ror_config_line MATCHES
                "^Plugin[ \t]*=[ \t]*(.*)$")
            string(STRIP "${CMAKE_MATCH_1}" _ror_plugin)
            if (NOT _ror_plugin MATCHES "^[A-Za-z0-9_-]+$")
                message(FATAL_ERROR
                    "Linux OGRE 14 plugin token is not a safe basename: "
                    "${_ror_plugin}")
            endif ()
            list(FIND _ror_plugins "${_ror_plugin}" _ror_duplicate_index)
            if (NOT _ror_duplicate_index EQUAL -1)
                message(FATAL_ERROR
                    "Linux OGRE 14 plugin is active more than once: "
                    "${_ror_plugin}")
            endif ()
            list(APPEND _ror_plugins "${_ror_plugin}")
        else ()
            message(FATAL_ERROR
                "Linux OGRE 14 plugins config has an unsupported active "
                "directive: ${_ror_config_line}")
        endif ()
    endforeach ()

    list(LENGTH _ror_plugin_folders _ror_plugin_folder_count)
    if (NOT _ror_plugin_folder_count EQUAL 1)
        message(FATAL_ERROR
            "Linux OGRE 14 plugins config must contain exactly one active "
            "PluginFolder")
    endif ()
    list(GET _ror_plugin_folders 0 _ror_plugin_folder)
    if (NOT _ror_plugin_folder STREQUAL "${expected_plugin_folder}")
        message(FATAL_ERROR
            "Linux OGRE 14 PluginFolder must be package-relative "
            "'${expected_plugin_folder}', not '${_ror_plugin_folder}'")
    endif ()

    set(_ror_expected_plugins ${expected_plugins})
    set(_ror_sorted_plugins ${_ror_plugins})
    list(SORT _ror_expected_plugins)
    list(SORT _ror_sorted_plugins)
    if (NOT "${_ror_sorted_plugins}" STREQUAL
            "${_ror_expected_plugins}")
        message(FATAL_ERROR
            "Linux OGRE 14 active plugin set changed: expected "
            "'${_ror_expected_plugins}', found '${_ror_sorted_plugins}'")
    endif ()

    set(${output_plugins} "${_ror_plugins}" PARENT_SCOPE)
endfunction()

function(
        ror_linux_ogre14_resolve_plugin
        output_plugins
        output_real_plugin
        plugin_directory
        plugin_name)
    if (NOT IS_ABSOLUTE "${plugin_directory}"
            OR NOT IS_DIRECTORY "${plugin_directory}")
        message(FATAL_ERROR
            "Linux OGRE 14 plugin source is not an absolute directory: "
            "${plugin_directory}")
    endif ()
    if (NOT plugin_name MATCHES "^[A-Za-z0-9_-]+$")
        message(FATAL_ERROR
            "Linux OGRE 14 plugin token is not a safe basename: "
            "${plugin_name}")
    endif ()

    file(GLOB
        _ror_plugin_candidates
        LIST_DIRECTORIES FALSE
        "${plugin_directory}/${plugin_name}.so"
        "${plugin_directory}/${plugin_name}.so.*")
    if (NOT _ror_plugin_candidates)
        message(FATAL_ERROR
            "Linux OGRE 14 plugin '${plugin_name}' has no shared object "
            "in ${plugin_directory}")
    endif ()
    list(SORT _ror_plugin_candidates)

    set(_ror_plugin_reals)
    set(_ror_has_unversioned_name OFF)
    set(_ror_has_abi_name OFF)
    foreach (_ror_plugin_candidate IN LISTS _ror_plugin_candidates)
        get_filename_component(
            _ror_plugin_basename
            "${_ror_plugin_candidate}"
            NAME)
        if (NOT _ror_plugin_basename MATCHES
                "^${plugin_name}\\.so(\\.14\\.5(\\.[0-9]+)*)?$")
            message(FATAL_ERROR
                "Linux OGRE 14 plugin has an unexpected ABI name: "
                "${_ror_plugin_basename}")
        endif ()
        if (_ror_plugin_basename STREQUAL
                "${plugin_name}.so")
            set(_ror_has_unversioned_name ON)
        elseif (_ror_plugin_basename STREQUAL
                "${plugin_name}.so.14.5")
            set(_ror_has_abi_name ON)
        endif ()
        ror_linux_ogre14_validate_symlink_chain(
            "${plugin_directory}"
            "${_ror_plugin_candidate}")
        get_filename_component(
            _ror_real_plugin
            "${_ror_plugin_candidate}"
            REALPATH)
        ror_linux_ogre14_path_is_within(
            _ror_plugin_is_contained
            "${plugin_directory}"
            "${_ror_real_plugin}")
        if (NOT _ror_plugin_is_contained)
            message(FATAL_ERROR
                "Linux OGRE 14 plugin escapes its package directory: "
                "${_ror_plugin_candidate}")
        endif ()
        list(APPEND _ror_plugin_reals "${_ror_real_plugin}")
    endforeach ()
    list(REMOVE_DUPLICATES _ror_plugin_reals)
    list(LENGTH _ror_plugin_reals _ror_plugin_real_count)
    if (NOT _ror_plugin_real_count EQUAL 1)
        message(FATAL_ERROR
            "Linux OGRE 14 plugin '${plugin_name}' resolves to multiple "
            "shared objects in ${plugin_directory}")
    endif ()
    if (NOT _ror_has_unversioned_name OR NOT _ror_has_abi_name)
        message(FATAL_ERROR
            "Linux OGRE 14 plugin '${plugin_name}' is missing its "
            ".so or .so.14.5 loader name")
    endif ()
    list(GET _ror_plugin_reals 0 _ror_real_plugin)

    set(${output_plugins} "${_ror_plugin_candidates}" PARENT_SCOPE)
    set(${output_real_plugin} "${_ror_real_plugin}" PARENT_SCOPE)
endfunction()

function(
        ror_linux_ogre14_validate_runtime_paths
        output_paths
        runtime_search_directories
        runtime_paths)
    set(_ror_search_directories)
    foreach (_ror_search_directory IN LISTS runtime_search_directories)
        if (NOT IS_ABSOLUTE "${_ror_search_directory}"
                OR NOT IS_DIRECTORY "${_ror_search_directory}")
            message(FATAL_ERROR
                "Linux OGRE 14 runtime search root is not an absolute "
                "directory: ${_ror_search_directory}")
        endif ()
        get_filename_component(
            _ror_real_search_directory
            "${_ror_search_directory}"
            REALPATH)
        if (_ror_real_search_directory MATCHES
                "^/(lib|lib64|usr/lib|usr/lib64)(/|$)")
            message(FATAL_ERROR
                "Linux OGRE 14 runtime search root must not be a host "
                "system library directory: ${_ror_search_directory}")
        endif ()
        list(APPEND
            _ror_search_directories
            "${_ror_real_search_directory}")
    endforeach ()
    list(REMOVE_DUPLICATES _ror_search_directories)
    if (NOT _ror_search_directories)
        message(FATAL_ERROR
            "Linux OGRE 14 runtime closure has no approved package roots")
    endif ()

    set(_ror_validated_paths)
    foreach (_ror_runtime_path IN LISTS runtime_paths)
        if (NOT IS_ABSOLUTE "${_ror_runtime_path}"
                OR NOT EXISTS "${_ror_runtime_path}"
                OR IS_DIRECTORY "${_ror_runtime_path}")
            message(FATAL_ERROR
                "Linux OGRE 14 dependency is not an absolute file: "
                "${_ror_runtime_path}")
        endif ()
        get_filename_component(
            _ror_real_runtime_path
            "${_ror_runtime_path}"
            REALPATH)
        set(_ror_runtime_path_is_approved OFF)
        foreach (_ror_search_directory IN LISTS _ror_search_directories)
            ror_linux_ogre14_path_is_within(
                _ror_runtime_path_is_approved
                "${_ror_search_directory}"
                "${_ror_real_runtime_path}")
            if (_ror_runtime_path_is_approved)
                break()
            endif ()
        endforeach ()
        if (NOT _ror_runtime_path_is_approved)
            message(FATAL_ERROR
                "Linux OGRE 14 dependency is outside the approved Conan "
                "runtime roots: ${_ror_runtime_path}")
        endif ()
        list(APPEND _ror_validated_paths "${_ror_runtime_path}")
    endforeach ()
    list(REMOVE_DUPLICATES _ror_validated_paths)

    set(${output_paths} "${_ror_validated_paths}" PARENT_SCOPE)
endfunction()

function(
        ror_linux_ogre14_dependency_copy_roots
        output_copy_roots
        runtime_paths)
    set(_ror_copy_roots)
    foreach (_ror_runtime_path IN LISTS runtime_paths)
        get_filename_component(
            _ror_real_runtime_path
            "${_ror_runtime_path}"
            REALPATH)
        get_filename_component(
            _ror_runtime_directory
            "${_ror_runtime_path}"
            DIRECTORY)
        get_filename_component(
            _ror_runtime_basename
            "${_ror_runtime_path}"
            NAME)
        if (NOT _ror_runtime_basename MATCHES "^(.+\\.so)(\\..*)?$")
            message(FATAL_ERROR
                "Linux OGRE 14 dependency has no shared-object basename: "
                "${_ror_runtime_path}")
        endif ()
        set(_ror_runtime_link_name "${CMAKE_MATCH_1}")
        file(GLOB
            _ror_runtime_candidates
            LIST_DIRECTORIES FALSE
            "${_ror_runtime_directory}/${_ror_runtime_link_name}"
            "${_ror_runtime_directory}/${_ror_runtime_link_name}.*")
        set(_ror_matching_candidates)
        foreach (_ror_runtime_candidate IN LISTS _ror_runtime_candidates)
            get_filename_component(
                _ror_real_runtime_candidate
                "${_ror_runtime_candidate}"
                REALPATH)
            if (_ror_real_runtime_candidate STREQUAL
                    _ror_real_runtime_path)
                ror_linux_ogre14_validate_symlink_chain(
                    "${_ror_runtime_directory}"
                    "${_ror_runtime_candidate}")
                list(APPEND
                    _ror_matching_candidates
                    "${_ror_runtime_candidate}")
            endif ()
        endforeach ()
        if (NOT _ror_matching_candidates)
            message(FATAL_ERROR
                "Linux OGRE 14 dependency has no stageable SONAME chain: "
                "${_ror_runtime_path}")
        endif ()
        list(APPEND _ror_copy_roots ${_ror_matching_candidates})
    endforeach ()
    list(REMOVE_DUPLICATES _ror_copy_roots)
    set(${output_copy_roots} "${_ror_copy_roots}" PARENT_SCOPE)
endfunction()

function(
        ror_linux_ogre14_validate_installed_plugins
        plugin_directory
        expected_plugins)
    if (NOT IS_ABSOLUTE "${plugin_directory}"
            OR NOT IS_DIRECTORY "${plugin_directory}")
        message(FATAL_ERROR
            "Installed Linux OGRE 14 plugin directory is unavailable: "
            "${plugin_directory}")
    endif ()
    file(GLOB _ror_installed_plugins "${plugin_directory}/*")
    foreach (_ror_installed_plugin IN LISTS _ror_installed_plugins)
        if (IS_DIRECTORY "${_ror_installed_plugin}"
                AND NOT IS_SYMLINK "${_ror_installed_plugin}")
            message(FATAL_ERROR
                "Installed Linux OGRE 14 plugin directory contains a "
                "nested directory: ${_ror_installed_plugin}")
        endif ()
        get_filename_component(
            _ror_installed_plugin_name
            "${_ror_installed_plugin}"
            NAME)
        set(_ror_installed_plugin_is_expected OFF)
        foreach (_ror_expected_plugin IN LISTS expected_plugins)
            if (_ror_installed_plugin_name MATCHES
                    "^${_ror_expected_plugin}\\.so(\\.14\\.5(\\.[0-9]+)*)?$")
                set(_ror_installed_plugin_is_expected ON)
                break()
            endif ()
        endforeach ()
        if (NOT _ror_installed_plugin_is_expected)
            message(FATAL_ERROR
                "Installed Linux OGRE 14 plugin set contains an "
                "unexpected entry: ${_ror_installed_plugin_name}")
        endif ()
    endforeach ()
    foreach (_ror_expected_plugin IN LISTS expected_plugins)
        if (NOT EXISTS "${plugin_directory}/${_ror_expected_plugin}.so"
                OR NOT EXISTS
                    "${plugin_directory}/${_ror_expected_plugin}.so.14.5")
            message(FATAL_ERROR
                "Installed Linux OGRE 14 plugin is missing its loader "
                "links: ${_ror_expected_plugin}")
        endif ()
    endforeach ()
endfunction()

function(
        ror_linux_ogre14_validate_installed_symlinks
        install_root
        library_root)
    if (NOT IS_ABSOLUTE "${install_root}"
            OR NOT IS_DIRECTORY "${install_root}"
            OR NOT IS_ABSOLUTE "${library_root}"
            OR NOT IS_DIRECTORY "${library_root}"
            OR IS_SYMLINK "${library_root}")
        message(FATAL_ERROR
            "Installed Linux runtime library root is unavailable or unsafe: "
            "${library_root}")
    endif ()
    ror_linux_ogre14_path_is_within(
        _ror_library_root_is_contained
        "${install_root}"
        "${library_root}")
    if (NOT _ror_library_root_is_contained)
        message(FATAL_ERROR
            "Installed Linux runtime library root escapes its package: "
            "${library_root}")
    endif ()

    file(GLOB_RECURSE _ror_installed_entries "${library_root}/*")
    foreach (_ror_installed_entry IN LISTS _ror_installed_entries)
        if (IS_SYMLINK "${_ror_installed_entry}")
            file(READ_SYMLINK
                "${_ror_installed_entry}"
                _ror_installed_link_target)
            if (IS_ABSOLUTE "${_ror_installed_link_target}")
                message(FATAL_ERROR
                    "Installed Linux runtime symlink is absolute: "
                    "${_ror_installed_entry} -> "
                    "${_ror_installed_link_target}")
            endif ()
            ror_linux_ogre14_validate_symlink_chain(
                "${library_root}"
                "${_ror_installed_entry}")
        endif ()
        ror_linux_ogre14_path_is_within(
            _ror_installed_entry_is_contained
            "${library_root}"
            "${_ror_installed_entry}")
        if (NOT _ror_installed_entry_is_contained)
            message(FATAL_ERROR
                "Installed Linux runtime entry escapes its library root: "
                "${_ror_installed_entry}")
        endif ()
    endforeach ()
endfunction()

function(ror_linux_ogre14_validate_loader_metadata metadata source_path)
    string(REGEX MATCHALL
        "\\((RPATH|RUNPATH)\\)[^\n]*\\[[^]]*\\]"
        _ror_runtime_path_lines
        "${metadata}")
    set(_ror_allowed_runtime_paths
        "$ORIGIN"
        "$ORIGIN/.."
        "$ORIGIN/../lib"
        "$ORIGIN/lib"
        "$ORIGIN/lib/OGRE"
        "\${ORIGIN}"
        "\${ORIGIN}/.."
        "\${ORIGIN}/../lib"
        "\${ORIGIN}/lib"
        "\${ORIGIN}/lib/OGRE")
    foreach (_ror_runtime_path_line IN LISTS _ror_runtime_path_lines)
        string(REGEX REPLACE
            "^.*\\[([^]]*)\\].*$"
            "\\1"
            _ror_runtime_path_list
            "${_ror_runtime_path_line}")
        string(REPLACE ":" ";" _ror_runtime_paths
            "${_ror_runtime_path_list}")
        foreach (_ror_runtime_path IN LISTS _ror_runtime_paths)
            if (_ror_runtime_path STREQUAL "")
                continue()
            endif ()
            list(FIND
                _ror_allowed_runtime_paths
                "${_ror_runtime_path}"
                _ror_runtime_path_index)
            if (_ror_runtime_path_index EQUAL -1)
                message(FATAL_ERROR
                    "Linux runtime has an unsafe RPATH/RUNPATH entry "
                    "'${_ror_runtime_path}' in ${source_path}")
            endif ()
        endforeach ()
    endforeach ()

    string(REGEX MATCHALL
        "\\(NEEDED\\)[^\n]*\\[[^]]+\\]"
        _ror_needed_lines
        "${metadata}")
    foreach (_ror_needed_line IN LISTS _ror_needed_lines)
        string(REGEX REPLACE
            "^.*\\[([^]]+)\\].*$"
            "\\1"
            _ror_needed_name
            "${_ror_needed_line}")
        if (_ror_needed_name MATCHES "[/\\\\]"
                OR _ror_needed_name MATCHES "(^|/)\\.\\.(/|$)")
            message(FATAL_ERROR
                "Linux runtime has a non-basename DT_NEEDED entry "
                "'${_ror_needed_name}' in ${source_path}")
        endif ()
    endforeach ()
endfunction()

function(
        ror_linux_ogre14_assert_dependency_resolution
        unresolved_dependencies
        conflicting_filenames)
    if (unresolved_dependencies)
        message(FATAL_ERROR
            "Linux OGRE 14 runtime closure has unresolved dependencies: "
            "${unresolved_dependencies}")
    endif ()
    if (conflicting_filenames)
        message(FATAL_ERROR
            "Linux OGRE 14 runtime closure has conflicting dependency "
            "basenames: ${conflicting_filenames}")
    endif ()
endfunction()
