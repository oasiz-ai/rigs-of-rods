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

    file(RELATIVE_PATH
        _ror_source_relative_path
        "${root_directory}"
        "${source_path}")
    if (IS_ABSOLUTE "${_ror_source_relative_path}"
            OR _ror_source_relative_path MATCHES "^\\.\\.(/|$)")
        message(FATAL_ERROR
            "Linux OGRE 14 shared-object source escapes its root: "
            "${source_path}")
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
        file(RELATIVE_PATH
            _ror_link_relative_path
            "${root_directory}"
            "${_ror_link_path}")
        if (IS_ABSOLUTE "${_ror_link_relative_path}"
                OR _ror_link_relative_path MATCHES "^\\.\\.(/|$)")
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
    ror_linux_ogre14_path_is_within(
        _ror_terminal_link_is_contained
        "${root_directory}"
        "${_ror_link_path}")
    if (NOT _ror_terminal_link_is_contained)
        message(FATAL_ERROR
            "Linux OGRE 14 shared-object symlink escapes its root: "
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
            OR NOT IS_DIRECTORY "${plugin_directory}"
            OR IS_SYMLINK "${plugin_directory}")
        message(FATAL_ERROR
            "Linux OGRE 14 plugin source is not an absolute directory: "
            "${plugin_directory}")
    endif ()
    if (NOT plugin_name MATCHES "^[A-Za-z0-9_-]+$")
        message(FATAL_ERROR
            "Linux OGRE 14 plugin token is not a safe basename: "
            "${plugin_name}")
    endif ()

    set(_ror_plugin_unversioned
        "${plugin_directory}/${plugin_name}.so")
    set(_ror_plugin_abi
        "${plugin_directory}/${plugin_name}.so.14.5")
    set(_ror_plugin_versioned
        "${plugin_directory}/${plugin_name}.so.14.5.2")

    # Inspect only this exact allowlisted family.  The pinned upstream OGRE
    # plugin target uses VERSION=14.5 and therefore installs a two-member
    # source chain.  A package that already carries the canonical 14.5.2
    # target is accepted too; every other suffix is rejected.
    file(GLOB
        _ror_plugin_candidates
        LIST_DIRECTORIES TRUE
        "${plugin_directory}/${plugin_name}.so*")
    list(SORT _ror_plugin_candidates)
    set(_ror_allowed_plugin_candidates
        "${_ror_plugin_unversioned}"
        "${_ror_plugin_abi}"
        "${_ror_plugin_versioned}")
    foreach (_ror_plugin_candidate IN LISTS _ror_plugin_candidates)
        list(FIND
            _ror_allowed_plugin_candidates
            "${_ror_plugin_candidate}"
            _ror_allowed_plugin_index)
        if (_ror_allowed_plugin_index EQUAL -1)
            get_filename_component(
                _ror_plugin_basename
                "${_ror_plugin_candidate}"
                NAME)
            message(FATAL_ERROR
                "Linux OGRE 14 plugin has an unexpected ABI entry: "
                "${_ror_plugin_basename}")
        endif ()
    endforeach ()

    foreach (_ror_required_plugin_path IN ITEMS
            "${_ror_plugin_unversioned}"
            "${_ror_plugin_abi}")
        if (NOT EXISTS "${_ror_required_plugin_path}"
                AND NOT IS_SYMLINK "${_ror_required_plugin_path}")
            message(FATAL_ERROR
                "Linux OGRE 14 plugin '${plugin_name}' is missing its "
                ".so or .so.14.5 loader name")
        endif ()
        ror_linux_ogre14_validate_symlink_chain(
            "${plugin_directory}"
            "${_ror_required_plugin_path}")
    endforeach ()

    if (NOT IS_SYMLINK "${_ror_plugin_unversioned}")
        message(FATAL_ERROR
            "Linux OGRE 14 plugin '${plugin_name}' has no canonical .so "
            "loader symlink")
    endif ()
    file(READ_SYMLINK
        "${_ror_plugin_unversioned}"
        _ror_plugin_unversioned_target)
    get_filename_component(
        _ror_plugin_abi_name
        "${_ror_plugin_abi}"
        NAME)
    if (NOT _ror_plugin_unversioned_target STREQUAL
            _ror_plugin_abi_name)
        message(FATAL_ERROR
            "Linux OGRE 14 plugin '${plugin_name}' has a non-canonical "
            ".so loader target: ${_ror_plugin_unversioned_target}")
    endif ()

    if (EXISTS "${_ror_plugin_versioned}"
            OR IS_SYMLINK "${_ror_plugin_versioned}")
        ror_linux_ogre14_validate_symlink_chain(
            "${plugin_directory}"
            "${_ror_plugin_versioned}")
        if (IS_SYMLINK "${_ror_plugin_versioned}"
                OR IS_DIRECTORY "${_ror_plugin_versioned}")
            message(FATAL_ERROR
                "Linux OGRE 14 plugin '${plugin_name}' has no regular "
                "14.5.2 binary")
        endif ()
        if (NOT IS_SYMLINK "${_ror_plugin_abi}")
            message(FATAL_ERROR
                "Linux OGRE 14 plugin '${plugin_name}' has no canonical "
                ".so.14.5 loader symlink")
        endif ()
        file(READ_SYMLINK
            "${_ror_plugin_abi}"
            _ror_plugin_abi_target)
        get_filename_component(
            _ror_plugin_versioned_name
            "${_ror_plugin_versioned}"
            NAME)
        if (NOT _ror_plugin_abi_target STREQUAL
                _ror_plugin_versioned_name)
            message(FATAL_ERROR
                "Linux OGRE 14 plugin '${plugin_name}' has a "
                "non-canonical .so.14.5 loader target: "
                "${_ror_plugin_abi_target}")
        endif ()
        set(_ror_expected_plugin_candidates
            "${_ror_plugin_unversioned}"
            "${_ror_plugin_abi}"
            "${_ror_plugin_versioned}")
        set(_ror_expected_real_plugin "${_ror_plugin_versioned}")
    else ()
        if (IS_SYMLINK "${_ror_plugin_abi}"
                OR IS_DIRECTORY "${_ror_plugin_abi}")
            message(FATAL_ERROR
                "Linux OGRE 14 plugin '${plugin_name}' has a broken "
                "upstream .so.14.5 binary")
        endif ()
        set(_ror_expected_plugin_candidates
            "${_ror_plugin_unversioned}"
            "${_ror_plugin_abi}")
        set(_ror_expected_real_plugin "${_ror_plugin_abi}")
    endif ()

    if (NOT "${_ror_plugin_candidates}" STREQUAL
            "${_ror_expected_plugin_candidates}")
        message(FATAL_ERROR
            "Linux OGRE 14 plugin '${plugin_name}' source chain changed")
    endif ()

    get_filename_component(
        _ror_real_plugin
        "${_ror_plugin_unversioned}"
        REALPATH)
    get_filename_component(
        _ror_expected_real_plugin
        "${_ror_expected_real_plugin}"
        REALPATH)
    if (NOT _ror_real_plugin STREQUAL _ror_expected_real_plugin)
        message(FATAL_ERROR
            "Linux OGRE 14 plugin '${plugin_name}' resolves to an "
            "unexpected binary: ${_ror_real_plugin}")
    endif ()
    ror_linux_ogre14_path_is_within(
        _ror_plugin_is_contained
        "${plugin_directory}"
        "${_ror_real_plugin}")
    if (NOT _ror_plugin_is_contained)
        message(FATAL_ERROR
            "Linux OGRE 14 plugin escapes its package directory: "
            "${_ror_plugin_unversioned}")
    endif ()

    set(${output_plugins}
        "${_ror_expected_plugin_candidates}" PARENT_SCOPE)
    set(${output_real_plugin} "${_ror_real_plugin}" PARENT_SCOPE)
endfunction()

function(
        ror_linux_ogre14_stage_plugin_chain
        plugin_directory
        destination_directory
        plugin_name)
    if (NOT IS_ABSOLUTE "${destination_directory}"
            OR NOT IS_DIRECTORY "${destination_directory}"
            OR IS_SYMLINK "${destination_directory}")
        message(FATAL_ERROR
            "Linux OGRE 14 plugin destination is unavailable or unsafe: "
            "${destination_directory}")
    endif ()

    ror_linux_ogre14_resolve_plugin(
        _ror_source_plugin_chain
        _ror_source_plugin_real
        "${plugin_directory}"
        "${plugin_name}")

    file(GLOB
        _ror_existing_destination_entries
        LIST_DIRECTORIES TRUE
        "${destination_directory}/${plugin_name}.so*")
    if (_ror_existing_destination_entries)
        message(FATAL_ERROR
            "Linux OGRE 14 plugin destination already contains the "
            "'${plugin_name}' family")
    endif ()

    set(_ror_destination_unversioned_name "${plugin_name}.so")
    set(_ror_destination_abi_name "${plugin_name}.so.14.5")
    set(_ror_destination_versioned_name "${plugin_name}.so.14.5.2")
    set(_ror_destination_unversioned
        "${destination_directory}/${_ror_destination_unversioned_name}")
    set(_ror_destination_abi
        "${destination_directory}/${_ror_destination_abi_name}")
    set(_ror_destination_versioned
        "${destination_directory}/${_ror_destination_versioned_name}")

    get_filename_component(
        _ror_source_plugin_real_name
        "${_ror_source_plugin_real}"
        NAME)
    file(COPY
        "${_ror_source_plugin_real}"
        DESTINATION "${destination_directory}")
    set(_ror_copied_plugin_real
        "${destination_directory}/${_ror_source_plugin_real_name}")
    if (NOT _ror_copied_plugin_real STREQUAL
            _ror_destination_versioned)
        file(RENAME
            "${_ror_copied_plugin_real}"
            "${_ror_destination_versioned}")
    endif ()
    if (NOT EXISTS "${_ror_destination_versioned}"
            OR IS_DIRECTORY "${_ror_destination_versioned}"
            OR IS_SYMLINK "${_ror_destination_versioned}")
        message(FATAL_ERROR
            "Linux OGRE 14 plugin staging did not produce a regular "
            "14.5.2 binary: ${plugin_name}")
    endif ()
    file(SHA256 "${_ror_source_plugin_real}" _ror_source_plugin_sha256)
    file(SHA256 "${_ror_destination_versioned}"
        _ror_destination_plugin_sha256)
    if (NOT _ror_destination_plugin_sha256 STREQUAL
            _ror_source_plugin_sha256)
        message(FATAL_ERROR
            "Linux OGRE 14 staged plugin binary differs from its "
            "validated source: ${plugin_name}")
    endif ()

    file(CREATE_LINK
        "${_ror_destination_versioned_name}"
        "${_ror_destination_abi}"
        SYMBOLIC
        RESULT _ror_plugin_abi_link_result)
    if (NOT _ror_plugin_abi_link_result STREQUAL "0")
        message(FATAL_ERROR
            "Could not create Linux OGRE 14 ABI plugin link: "
            "${_ror_plugin_abi_link_result}")
    endif ()
    file(CREATE_LINK
        "${_ror_destination_abi_name}"
        "${_ror_destination_unversioned}"
        SYMBOLIC
        RESULT _ror_plugin_unversioned_link_result)
    if (NOT _ror_plugin_unversioned_link_result STREQUAL "0")
        message(FATAL_ERROR
            "Could not create Linux OGRE 14 unversioned plugin link: "
            "${_ror_plugin_unversioned_link_result}")
    endif ()

    ror_linux_ogre14_resolve_plugin(
        _ror_staged_plugin_chain
        _ror_staged_plugin_real
        "${destination_directory}"
        "${plugin_name}")
    get_filename_component(
        _ror_destination_versioned_real
        "${_ror_destination_versioned}"
        REALPATH)
    if (NOT _ror_staged_plugin_real STREQUAL
            _ror_destination_versioned_real)
        message(FATAL_ERROR
            "Linux OGRE 14 staged plugin chain resolves incorrectly: "
            "${plugin_name}")
    endif ()
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
            OR NOT IS_DIRECTORY "${plugin_directory}"
            OR IS_SYMLINK "${plugin_directory}")
        message(FATAL_ERROR
            "Installed Linux OGRE 14 plugin directory is unavailable: "
            "${plugin_directory}")
    endif ()
    file(GLOB _ror_installed_plugins "${plugin_directory}/*")
    set(_ror_expected_installed_plugin_names)
    foreach (_ror_expected_plugin IN LISTS expected_plugins)
        list(APPEND _ror_expected_installed_plugin_names
            "${_ror_expected_plugin}.so"
            "${_ror_expected_plugin}.so.14.5"
            "${_ror_expected_plugin}.so.14.5.2")
    endforeach ()
    list(SORT _ror_expected_installed_plugin_names)

    set(_ror_installed_plugin_names)
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
        list(FIND
            _ror_expected_installed_plugin_names
            "${_ror_installed_plugin_name}"
            _ror_installed_plugin_name_index)
        if (_ror_installed_plugin_name_index EQUAL -1)
            message(FATAL_ERROR
                "Installed Linux OGRE 14 plugin set contains an "
                "unexpected entry: ${_ror_installed_plugin_name}")
        endif ()
        list(APPEND
            _ror_installed_plugin_names
            "${_ror_installed_plugin_name}")
    endforeach ()
    list(SORT _ror_installed_plugin_names)
    if (NOT "${_ror_installed_plugin_names}" STREQUAL
            "${_ror_expected_installed_plugin_names}")
        message(FATAL_ERROR
            "Installed Linux OGRE 14 plugin set is incomplete: expected "
            "'${_ror_expected_installed_plugin_names}', found "
            "'${_ror_installed_plugin_names}'")
    endif ()

    foreach (_ror_expected_plugin IN LISTS expected_plugins)
        set(_ror_installed_unversioned
            "${plugin_directory}/${_ror_expected_plugin}.so")
        set(_ror_installed_abi
            "${plugin_directory}/${_ror_expected_plugin}.so.14.5")
        set(_ror_installed_versioned
            "${plugin_directory}/${_ror_expected_plugin}.so.14.5.2")
        if (NOT IS_SYMLINK "${_ror_installed_unversioned}"
                OR NOT IS_SYMLINK "${_ror_installed_abi}"
                OR NOT EXISTS "${_ror_installed_versioned}"
                OR IS_DIRECTORY "${_ror_installed_versioned}"
                OR IS_SYMLINK "${_ror_installed_versioned}")
            message(FATAL_ERROR
                "Installed Linux OGRE 14 plugin has no canonical "
                "14.5.2 chain: ${_ror_expected_plugin}")
        endif ()
        file(READ_SYMLINK
            "${_ror_installed_unversioned}"
            _ror_installed_unversioned_target)
        file(READ_SYMLINK
            "${_ror_installed_abi}"
            _ror_installed_abi_target)
        if (NOT _ror_installed_unversioned_target STREQUAL
                "${_ror_expected_plugin}.so.14.5"
                OR NOT _ror_installed_abi_target STREQUAL
                    "${_ror_expected_plugin}.so.14.5.2")
            message(FATAL_ERROR
                "Installed Linux OGRE 14 plugin has non-canonical "
                "relative loader links: ${_ror_expected_plugin}")
        endif ()
        ror_linux_ogre14_validate_symlink_chain(
            "${plugin_directory}"
            "${_ror_installed_unversioned}")
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
