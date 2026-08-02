# Stage and ad-hoc sign a relocatable, GL3Plus-only Rigs of Rods application.
#
# Required -D inputs:
#   ROR_BUNDLE             Absolute output path ending in ROR_BUNDLE_NAME.app.
#   ROR_EXECUTABLE         Absolute path to the external source Mach-O
#                          executable. It must not already be inside ROR_BUNDLE.
#   ROR_RESOURCES          Absolute path to the generated RoR resources directory.
#   ROR_LANGUAGES          Absolute path to the generated translations directory.
#   ROR_OGRE_PACKAGE_DIR   Absolute root of the pinned OGRE 14 package.
#   ROR_OGRE_PLUGIN_DIR    Absolute directory containing OGRE plugin dylibs.
#   ROR_OGRE_PLUGINS_CFG   Absolute path to the configured plugins.cfg.
#   ROR_OGRE_INSTALL_PLUGIN_FOLDER
#                          Bundle-relative PluginFolder from the R0 platform
#                          contract. It must resolve from Contents/MacOS.
#   ROR_RUNTIME_DYLIBS     Explicit semicolon-separated additional dylibs; pass
#                          an empty value when none are required.
#
# Optional -D inputs:
#   ROR_CONTENT                Absolute content directory or archive.
#   ROR_SIBLING_EXECUTABLES    Additional absolute executable paths staged
#                              beside the public executable.
#   ROR_RUNTIME_SEARCH_DIRS    Additional absolute dependency search directories.
#   ROR_INFO_PLIST_TEMPLATE    Defaults to the adjacent RoRInfo.plist.in.
#   ROR_DRY_RUN                Validate and print the plan without modifying output.
#   ROR_BUNDLE_NAME            Defaults to RoR.
#   ROR_BUNDLE_DISPLAY_NAME    Defaults to Rigs of Rods.
#   ROR_BUNDLE_EXECUTABLE_NAME Defaults to the ROR_EXECUTABLE filename.
#   ROR_BUNDLE_IDENTIFIER      Defaults to org.rigsofrods.RoR.
#   ROR_BUNDLE_SHORT_VERSION   Defaults to 2026.01.
#   ROR_BUNDLE_BUILD_VERSION   Defaults to 2026.1.0.
#   ROR_MACOS_DEPLOYMENT_TARGET Defaults to 11.0.
#   ROR_BUNDLE_COPYRIGHT       Defaults to the project copyright notice.

cmake_minimum_required(VERSION 3.21)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "StageMacOSBundle.cmake requires a macOS host")
endif()

set(_ror_stager_version 1)
set(_ror_script_dir "${CMAKE_CURRENT_LIST_DIR}")

function(_ror_require_defined variable_name)
    if(NOT DEFINED ${variable_name})
        message(FATAL_ERROR "Required input ${variable_name} is not defined")
    endif()
endfunction()

function(_ror_require_absolute_path variable_name expected_kind)
    _ror_require_defined("${variable_name}")
    set(_value "${${variable_name}}")
    if(_value STREQUAL "")
        message(FATAL_ERROR "Required input ${variable_name} is empty")
    endif()
    if(NOT IS_ABSOLUTE "${_value}")
        message(FATAL_ERROR "${variable_name} must be absolute: '${_value}'")
    endif()
    if(expected_kind STREQUAL "FILE" AND NOT EXISTS "${_value}")
        message(FATAL_ERROR "${variable_name} does not exist: '${_value}'")
    elseif(expected_kind STREQUAL "FILE" AND IS_DIRECTORY "${_value}")
        message(FATAL_ERROR "${variable_name} must be a file: '${_value}'")
    elseif(expected_kind STREQUAL "DIRECTORY" AND NOT IS_DIRECTORY "${_value}")
        message(FATAL_ERROR "${variable_name} must be a directory: '${_value}'")
    elseif(expected_kind STREQUAL "FILE_OR_DIRECTORY" AND NOT EXISTS "${_value}")
        message(FATAL_ERROR "${variable_name} does not exist: '${_value}'")
    endif()
endfunction()

function(_ror_real_path input_path output_variable)
    file(REAL_PATH "${input_path}" _resolved)
    set(${output_variable} "${_resolved}" PARENT_SCOPE)
endfunction()

function(_ror_xml_escape input_value output_variable)
    string(REPLACE "&" "&amp;" _escaped "${input_value}")
    string(REPLACE "<" "&lt;" _escaped "${_escaped}")
    string(REPLACE ">" "&gt;" _escaped "${_escaped}")
    set(${output_variable} "${_escaped}" PARENT_SCOPE)
endfunction()

function(_ror_path_is_within candidate root output_variable)
    string(LENGTH "${candidate}" _candidate_length)
    string(LENGTH "${root}" _root_length)
    set(_within FALSE)
    if(candidate STREQUAL root)
        set(_within TRUE)
    elseif(_candidate_length GREATER _root_length)
        string(SUBSTRING "${candidate}" 0 ${_root_length} _prefix)
        string(SUBSTRING "${candidate}" ${_root_length} 1 _separator)
        if(_prefix STREQUAL root AND _separator STREQUAL "/")
            set(_within TRUE)
        endif()
    endif()
    set(${output_variable} "${_within}" PARENT_SCOPE)
endfunction()

function(_ror_assert_no_recursive_copy input_path input_label bundle_path)
    _ror_path_is_within("${input_path}" "${bundle_path}" _input_in_bundle)
    _ror_path_is_within("${bundle_path}" "${input_path}" _bundle_in_input)
    if(_input_in_bundle OR _bundle_in_input)
        message(FATAL_ERROR
            "${input_label} and ROR_BUNDLE overlap; refusing a recursive or "
            "destructive copy ('${input_path}', '${bundle_path}')")
    endif()
endfunction()

function(_ror_find_tool output_variable)
    set(_names ${ARGN})
    find_program(_tool_path NAMES ${_names})
    if(NOT _tool_path)
        message(FATAL_ERROR "Required macOS tool not found: ${_names}")
    endif()
    set(${output_variable} "${_tool_path}" PARENT_SCOPE)
    unset(_tool_path CACHE)
endfunction()

function(_ror_run_checked description)
    set(_command ${ARGN})
    execute_process(
        COMMAND ${_command}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    if(NOT _result EQUAL 0)
        string(STRIP "${_stdout}" _stdout)
        string(STRIP "${_stderr}" _stderr)
        message(FATAL_ERROR
            "${description} failed (${_result})\n"
            "command: ${_command}\n"
            "stdout: ${_stdout}\n"
            "stderr: ${_stderr}")
    endif()
endfunction()

function(_ror_assert_macho path expected_kind)
    execute_process(
        COMMAND "${_ror_file_tool}" "${path}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _description
        ERROR_VARIABLE _error
    )
    if(NOT _result EQUAL 0 OR NOT _description MATCHES "Mach-O")
        message(FATAL_ERROR "Expected a Mach-O ${expected_kind}: '${path}' (${_description}${_error})")
    endif()
    if(expected_kind STREQUAL "EXECUTABLE" AND
            NOT _description MATCHES "executable")
        message(FATAL_ERROR "Expected a Mach-O executable: '${path}' (${_description})")
    elseif(expected_kind STREQUAL "DYLIB" AND
            NOT _description MATCHES "dynamically linked shared library")
        message(FATAL_ERROR "Expected a Mach-O dylib: '${path}' (${_description})")
    endif()
endfunction()

function(_ror_resolve_plugin plugin_entry output_source output_name)
    get_filename_component(_entry_name "${plugin_entry}" NAME)
    if(NOT _entry_name STREQUAL plugin_entry OR
            NOT _entry_name MATCHES "^[A-Za-z0-9_.+-]+$")
        message(FATAL_ERROR "Unsafe plugin entry in ${ROR_OGRE_PLUGINS_CFG}: '${plugin_entry}'")
    endif()

    string(REGEX REPLACE "\\.dylib$" "" _plugin_name "${_entry_name}")
    set(_candidates
        "${_ror_ogre_plugin_dir}/${_entry_name}"
        "${_ror_ogre_plugin_dir}/${_plugin_name}.dylib")
    set(_source "")
    foreach(_candidate IN LISTS _candidates)
        if(EXISTS "${_candidate}" AND NOT IS_DIRECTORY "${_candidate}")
            set(_source "${_candidate}")
            break()
        endif()
    endforeach()
    if(_source STREQUAL "")
        file(GLOB _versioned_candidates
            LIST_DIRECTORIES FALSE
            "${_ror_ogre_plugin_dir}/${_plugin_name}.*.dylib")
        list(SORT _versioned_candidates COMPARE NATURAL ORDER DESCENDING)
        list(LENGTH _versioned_candidates _candidate_count)
        if(_candidate_count GREATER 0)
            list(GET _versioned_candidates 0 _source)
        endif()
    endif()
    if(_source STREQUAL "")
        message(FATAL_ERROR
            "Configured OGRE plugin '${plugin_entry}' was not found in "
            "ROR_OGRE_PLUGIN_DIR='${_ror_ogre_plugin_dir}'")
    endif()

    _ror_real_path("${_source}" _real_source)
    _ror_assert_macho("${_real_source}" "DYLIB")
    set(${output_source} "${_real_source}" PARENT_SCOPE)
    set(${output_name} "${_plugin_name}" PARENT_SCOPE)
endfunction()

function(_ror_get_dependencies binary output_variable)
    execute_process(
        COMMAND "${_ror_otool}" -L "${binary}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "otool -L failed for '${binary}': ${_error}")
    endif()
    string(REPLACE "\r\n" "\n" _output "${_output}")
    string(REPLACE "\n" ";" _lines "${_output}")
    set(_dependencies)
    set(_first_line TRUE)
    foreach(_line IN LISTS _lines)
        if(_first_line)
            set(_first_line FALSE)
            continue()
        endif()
        string(STRIP "${_line}" _line)
        if(_line MATCHES "^(.+)[ \t]+\\(compatibility version")
            list(APPEND _dependencies "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _dependencies)
    set(${output_variable} "${_dependencies}" PARENT_SCOPE)
endfunction()

function(_ror_get_rpaths binary output_variable)
    execute_process(
        COMMAND "${_ror_otool}" -l "${binary}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "otool -l failed for '${binary}': ${_error}")
    endif()
    string(REGEX MATCHALL "path[ \t]+[^\r\n]+[ \t]+\\(offset[ \t]+[0-9]+\\)" _matches "${_output}")
    set(_rpaths)
    foreach(_match IN LISTS _matches)
        string(REGEX REPLACE
            "^path[ \t]+(.+)[ \t]+\\(offset[ \t]+[0-9]+\\)$"
            "\\1" _rpath "${_match}")
        string(STRIP "${_rpath}" _rpath)
        list(APPEND _rpaths "${_rpath}")
    endforeach()
    list(REMOVE_DUPLICATES _rpaths)
    set(${output_variable} "${_rpaths}" PARENT_SCOPE)
endfunction()

function(_ror_set_exact_rpaths binary)
    set(_desired ${ARGN})
    list(REMOVE_DUPLICATES _desired)
    _ror_get_rpaths("${binary}" _existing)

    # Reuse existing load commands before adding new ones. This is friendlier
    # to third-party Mach-Os that were not linked with a large header pad.
    foreach(_wanted IN LISTS _desired)
        list(FIND _existing "${_wanted}" _wanted_index)
        if(NOT _wanted_index EQUAL -1)
            continue()
        endif()
        set(_replace "")
        foreach(_old IN LISTS _existing)
            list(FIND _desired "${_old}" _old_is_desired)
            if(_old_is_desired EQUAL -1)
                set(_replace "${_old}")
                break()
            endif()
        endforeach()
        if(NOT _replace STREQUAL "")
            _ror_run_checked(
                "Replacing an LC_RPATH in ${binary}"
                "${_ror_install_name_tool}" -rpath "${_replace}" "${_wanted}" "${binary}")
            list(REMOVE_ITEM _existing "${_replace}")
            list(APPEND _existing "${_wanted}")
        else()
            _ror_run_checked(
                "Adding an LC_RPATH to ${binary}"
                "${_ror_install_name_tool}" -add_rpath "${_wanted}" "${binary}")
            list(APPEND _existing "${_wanted}")
        endif()
    endforeach()

    foreach(_old IN LISTS _existing)
        list(FIND _desired "${_old}" _old_is_desired)
        if(_old_is_desired EQUAL -1)
            _ror_run_checked(
                "Deleting a non-bundle LC_RPATH from ${binary}"
                "${_ror_install_name_tool}" -delete_rpath "${_old}" "${binary}")
        endif()
    endforeach()
endfunction()

function(_ror_find_staged_name dependency_name output_path)
    list(FIND _ror_staged_names "${dependency_name}" _index)
    if(_index EQUAL -1)
        set(${output_path} "" PARENT_SCOPE)
    else()
        list(GET _ror_staged_paths ${_index} _path)
        set(${output_path} "${_path}" PARENT_SCOPE)
    endif()
endfunction()

function(_ror_rewrite_dependencies binary)
    _ror_get_dependencies("${binary}" _dependencies)
    foreach(_dependency IN LISTS _dependencies)
        if(_dependency MATCHES "^/System/Library/" OR
                _dependency MATCHES "^/usr/lib/")
            continue()
        endif()
        get_filename_component(_dependency_name "${_dependency}" NAME)
        _ror_find_staged_name("${_dependency_name}" _staged_path)
        if(_staged_path STREQUAL "")
            message(FATAL_ERROR
                "Non-system dependency '${_dependency}' from '${binary}' was "
                "not staged. Add its dylib or search directory explicitly.")
        endif()
        set(_new_dependency "@rpath/${_dependency_name}")
        if(NOT _dependency STREQUAL _new_dependency)
            _ror_run_checked(
                "Rewriting a dependency in ${binary}"
                "${_ror_install_name_tool}" -change
                "${_dependency}" "${_new_dependency}" "${binary}")
        endif()
    endforeach()
endfunction()

function(_ror_verify_binary binary expected_kind)
    _ror_assert_macho("${binary}" "${expected_kind}")
    _ror_get_dependencies("${binary}" _dependencies)
    foreach(_dependency IN LISTS _dependencies)
        if(_dependency MATCHES "^/System/Library/" OR
                _dependency MATCHES "^/usr/lib/")
            continue()
        endif()
        if(NOT _dependency MATCHES "^@rpath/[^/]+$")
            message(FATAL_ERROR
                "Non-relocatable dependency remains in '${binary}': '${_dependency}'")
        endif()
        get_filename_component(_dependency_name "${_dependency}" NAME)
        _ror_find_staged_name("${_dependency_name}" _staged_path)
        if(_staged_path STREQUAL "")
            message(FATAL_ERROR
                "Unresolved bundle dependency remains in '${binary}': '${_dependency}'")
        endif()
    endforeach()

    _ror_get_rpaths("${binary}" _actual_rpaths)
    set(_expected_rpaths ${ARGN})
    list(REMOVE_DUPLICATES _expected_rpaths)
    foreach(_rpath IN LISTS _actual_rpaths)
        list(FIND _expected_rpaths "${_rpath}" _expected_index)
        if(_expected_index EQUAL -1)
            message(FATAL_ERROR "Unexpected LC_RPATH in '${binary}': '${_rpath}'")
        endif()
        if(_rpath MATCHES "^/")
            message(FATAL_ERROR "Absolute LC_RPATH remains in '${binary}': '${_rpath}'")
        endif()
    endforeach()
    foreach(_rpath IN LISTS _expected_rpaths)
        list(FIND _actual_rpaths "${_rpath}" _actual_index)
        if(_actual_index EQUAL -1)
            message(FATAL_ERROR "Required LC_RPATH missing in '${binary}': '${_rpath}'")
        endif()
    endforeach()

    if(expected_kind STREQUAL "DYLIB")
        execute_process(
            COMMAND "${_ror_otool}" -D "${binary}"
            RESULT_VARIABLE _id_result
            OUTPUT_VARIABLE _id_output
            ERROR_VARIABLE _id_error
        )
        if(NOT _id_result EQUAL 0)
            message(FATAL_ERROR "otool -D failed for '${binary}': ${_id_error}")
        endif()
        get_filename_component(_binary_name "${binary}" NAME)
        if(NOT _id_output MATCHES "@rpath/${_binary_name}([\r\n]|$)")
            message(FATAL_ERROR
                "Dylib install name is not bundle-relative in '${binary}': ${_id_output}")
        endif()
    endif()
endfunction()

foreach(_required_variable IN ITEMS
        ROR_BUNDLE
        ROR_EXECUTABLE
        ROR_RESOURCES
        ROR_LANGUAGES
        ROR_OGRE_PACKAGE_DIR
        ROR_OGRE_PLUGIN_DIR
        ROR_OGRE_PLUGINS_CFG
        ROR_OGRE_INSTALL_PLUGIN_FOLDER
        ROR_RUNTIME_DYLIBS)
    _ror_require_defined("${_required_variable}")
endforeach()

if(NOT DEFINED ROR_DRY_RUN)
    set(ROR_DRY_RUN OFF)
endif()
if(NOT DEFINED ROR_BUNDLE_NAME)
    set(ROR_BUNDLE_NAME "RoR")
endif()
if(NOT DEFINED ROR_BUNDLE_DISPLAY_NAME)
    set(ROR_BUNDLE_DISPLAY_NAME "Rigs of Rods")
endif()
if(NOT DEFINED ROR_BUNDLE_EXECUTABLE_NAME)
    get_filename_component(ROR_BUNDLE_EXECUTABLE_NAME "${ROR_EXECUTABLE}" NAME)
endif()
if(NOT DEFINED ROR_BUNDLE_IDENTIFIER)
    set(ROR_BUNDLE_IDENTIFIER "org.rigsofrods.RoR")
endif()
if(NOT DEFINED ROR_BUNDLE_SHORT_VERSION)
    set(ROR_BUNDLE_SHORT_VERSION "2026.01")
endif()
if(NOT DEFINED ROR_BUNDLE_BUILD_VERSION)
    set(ROR_BUNDLE_BUILD_VERSION "2026.1.0")
endif()
if(NOT DEFINED ROR_MACOS_DEPLOYMENT_TARGET)
    set(ROR_MACOS_DEPLOYMENT_TARGET "11.0")
endif()
if(NOT DEFINED ROR_BUNDLE_COPYRIGHT)
    set(ROR_BUNDLE_COPYRIGHT "Copyright 2005-2026 Rigs of Rods contributors")
endif()
if(NOT DEFINED ROR_INFO_PLIST_TEMPLATE)
    set(ROR_INFO_PLIST_TEMPLATE "${_ror_script_dir}/RoRInfo.plist.in")
endif()
if(NOT DEFINED ROR_RUNTIME_SEARCH_DIRS)
    set(ROR_RUNTIME_SEARCH_DIRS "")
endif()
if(NOT DEFINED ROR_SIBLING_EXECUTABLES)
    set(ROR_SIBLING_EXECUTABLES "")
endif()

foreach(_name_variable IN ITEMS ROR_BUNDLE_NAME ROR_BUNDLE_EXECUTABLE_NAME)
    if("${${_name_variable}}" STREQUAL "" OR
            NOT "${${_name_variable}}" MATCHES "^[A-Za-z0-9_.+ -]+$")
        message(FATAL_ERROR "Unsafe ${_name_variable}: '${${_name_variable}}'")
    endif()
endforeach()
if(NOT ROR_BUNDLE_IDENTIFIER MATCHES "^[A-Za-z0-9.-]+$")
    message(FATAL_ERROR "Invalid ROR_BUNDLE_IDENTIFIER: '${ROR_BUNDLE_IDENTIFIER}'")
endif()
if(NOT ROR_OGRE_INSTALL_PLUGIN_FOLDER STREQUAL "../PlugIns")
    message(FATAL_ERROR
        "The macOS OGRE 14 runtime contract must stage plugins at "
        "../PlugIns, got '${ROR_OGRE_INSTALL_PLUGIN_FOLDER}'")
endif()
foreach(_version_variable IN ITEMS
        ROR_BUNDLE_SHORT_VERSION
        ROR_BUNDLE_BUILD_VERSION
        ROR_MACOS_DEPLOYMENT_TARGET)
    if(NOT "${${_version_variable}}" MATCHES "^[0-9]+(\\.[0-9]+)*$")
        message(FATAL_ERROR "Invalid ${_version_variable}: '${${_version_variable}}'")
    endif()
endforeach()
_ror_xml_escape("${ROR_BUNDLE_DISPLAY_NAME}" ROR_BUNDLE_DISPLAY_NAME)
_ror_xml_escape("${ROR_BUNDLE_COPYRIGHT}" ROR_BUNDLE_COPYRIGHT)

_ror_require_absolute_path("ROR_EXECUTABLE" "FILE")
_ror_require_absolute_path("ROR_RESOURCES" "DIRECTORY")
_ror_require_absolute_path("ROR_LANGUAGES" "DIRECTORY")
_ror_require_absolute_path("ROR_OGRE_PACKAGE_DIR" "DIRECTORY")
_ror_require_absolute_path("ROR_OGRE_PLUGIN_DIR" "DIRECTORY")
_ror_require_absolute_path("ROR_OGRE_PLUGINS_CFG" "FILE")
_ror_require_absolute_path("ROR_INFO_PLIST_TEMPLATE" "FILE")

set(_ror_sibling_executables)
set(_ror_sibling_executable_names)
foreach(_ror_sibling IN LISTS ROR_SIBLING_EXECUTABLES)
    if(_ror_sibling STREQUAL "")
        continue()
    endif()
    if(NOT IS_ABSOLUTE "${_ror_sibling}" OR
            NOT EXISTS "${_ror_sibling}" OR
            IS_DIRECTORY "${_ror_sibling}")
        message(FATAL_ERROR
            "ROR_SIBLING_EXECUTABLES contains a missing or non-file path: "
            "'${_ror_sibling}'")
    endif()
    get_filename_component(_ror_sibling_name "${_ror_sibling}" NAME)
    if(_ror_sibling_name STREQUAL "" OR
            NOT _ror_sibling_name MATCHES "^[A-Za-z0-9_.+-]+$")
        message(FATAL_ERROR
            "Unsafe sibling executable basename: '${_ror_sibling_name}'")
    endif()
    if(_ror_sibling_name STREQUAL ROR_BUNDLE_EXECUTABLE_NAME)
        message(FATAL_ERROR
            "Sibling executable collides with the public executable: "
            "'${_ror_sibling_name}'")
    endif()
    list(FIND _ror_sibling_executable_names
        "${_ror_sibling_name}" _ror_sibling_name_index)
    if(NOT _ror_sibling_name_index EQUAL -1)
        message(FATAL_ERROR
            "Duplicate sibling executable basename: '${_ror_sibling_name}'")
    endif()
    _ror_real_path("${_ror_sibling}" _ror_sibling_real)
    list(APPEND _ror_sibling_executables "${_ror_sibling_real}")
    list(APPEND _ror_sibling_executable_names "${_ror_sibling_name}")
endforeach()

if(NOT IS_ABSOLUTE "${ROR_BUNDLE}")
    message(FATAL_ERROR "ROR_BUNDLE must be absolute: '${ROR_BUNDLE}'")
endif()
get_filename_component(_ror_bundle_parent "${ROR_BUNDLE}" DIRECTORY)
get_filename_component(_ror_bundle_filename "${ROR_BUNDLE}" NAME)
if(NOT IS_DIRECTORY "${_ror_bundle_parent}")
    message(FATAL_ERROR "ROR_BUNDLE parent must already exist: '${_ror_bundle_parent}'")
endif()
if(NOT _ror_bundle_filename STREQUAL "${ROR_BUNDLE_NAME}.app")
    message(FATAL_ERROR
        "ROR_BUNDLE must name the exact configured app "
        "'${ROR_BUNDLE_NAME}.app', got '${_ror_bundle_filename}'")
endif()
_ror_real_path("${_ror_bundle_parent}" _ror_bundle_parent)
set(_ror_bundle "${_ror_bundle_parent}/${_ror_bundle_filename}")
if(IS_SYMLINK "${_ror_bundle}")
    message(FATAL_ERROR "ROR_BUNDLE must not be a symlink: '${_ror_bundle}'")
endif()
if(EXISTS "${_ror_bundle}" AND NOT IS_DIRECTORY "${_ror_bundle}")
    message(FATAL_ERROR "ROR_BUNDLE exists but is not a directory: '${_ror_bundle}'")
endif()

_ror_real_path("${ROR_EXECUTABLE}" _ror_executable)
_ror_real_path("${ROR_RESOURCES}" _ror_resources)
_ror_real_path("${ROR_LANGUAGES}" _ror_languages)
_ror_real_path("${ROR_OGRE_PACKAGE_DIR}" _ror_ogre_package_dir)
_ror_real_path("${ROR_OGRE_PLUGIN_DIR}" _ror_ogre_plugin_dir)
_ror_real_path("${ROR_OGRE_PLUGINS_CFG}" _ror_ogre_plugins_cfg)
_ror_real_path("${ROR_INFO_PLIST_TEMPLATE}" ROR_INFO_PLIST_TEMPLATE)

_ror_find_tool(_ror_file_tool file)
_ror_find_tool(_ror_otool otool)
_ror_find_tool(_ror_install_name_tool install_name_tool)
_ror_find_tool(_ror_codesign codesign)
_ror_find_tool(_ror_plutil plutil)
_ror_assert_macho("${_ror_executable}" "EXECUTABLE")
foreach(_ror_sibling IN LISTS _ror_sibling_executables)
    if(_ror_sibling STREQUAL _ror_executable)
        message(FATAL_ERROR
            "A sibling executable resolves to ROR_EXECUTABLE: "
            "'${_ror_sibling}'")
    endif()
    _ror_assert_macho("${_ror_sibling}" "EXECUTABLE")
endforeach()

set(_ror_contents "${_ror_bundle}/Contents")
set(_ror_macos "${_ror_contents}/MacOS")
set(_ror_frameworks "${_ror_contents}/Frameworks")
set(_ror_plugins "${_ror_contents}/PlugIns")
set(_ror_bundle_resources "${_ror_contents}/Resources")
set(_ror_destination_executable
    "${_ror_macos}/${ROR_BUNDLE_EXECUTABLE_NAME}")
set(_ror_destination_sibling_executables)
foreach(_ror_sibling_name IN LISTS _ror_sibling_executable_names)
    list(APPEND _ror_destination_sibling_executables
        "${_ror_macos}/${_ror_sibling_name}")
endforeach()

# Validate every input and the exact destination before any output mutation.
foreach(_input_pair IN ITEMS
        "${_ror_resources}|ROR_RESOURCES"
        "${_ror_languages}|ROR_LANGUAGES"
        "${_ror_ogre_package_dir}|ROR_OGRE_PACKAGE_DIR"
        "${_ror_ogre_plugin_dir}|ROR_OGRE_PLUGIN_DIR")
    string(REPLACE "|" ";" _pair "${_input_pair}")
    list(GET _pair 0 _input_path)
    list(GET _pair 1 _input_label)
    _ror_assert_no_recursive_copy("${_input_path}" "${_input_label}" "${_ror_bundle}")
endforeach()
if(DEFINED ROR_CONTENT AND NOT ROR_CONTENT STREQUAL "")
    _ror_require_absolute_path("ROR_CONTENT" "FILE_OR_DIRECTORY")
    _ror_real_path("${ROR_CONTENT}" _ror_content)
    _ror_assert_no_recursive_copy("${_ror_content}" "ROR_CONTENT" "${_ror_bundle}")
    if(IS_DIRECTORY "${_ror_content}")
        file(GLOB_RECURSE _ror_content_files
            LIST_DIRECTORIES FALSE
            "${_ror_content}/*")
        if(NOT _ror_content_files)
            message(FATAL_ERROR
                "ROR_CONTENT directory contains no regular files: "
                "'${_ror_content}'. Refusing to produce an app bundle that "
                "silently omits starter content.")
        endif()
    endif()
else()
    set(_ror_content "")
endif()

_ror_path_is_within("${_ror_executable}" "${_ror_bundle}" _executable_in_bundle)
if(_executable_in_bundle)
    message(FATAL_ERROR
        "ROR_EXECUTABLE must be an external source so ROR_BUNDLE can be "
        "recreated without preserving stale runtime state: '${_ror_executable}'")
endif()
foreach(_ror_sibling IN LISTS _ror_sibling_executables)
    _ror_path_is_within(
        "${_ror_sibling}" "${_ror_bundle}" _sibling_in_bundle)
    if(_sibling_in_bundle)
        message(FATAL_ERROR
            "ROR_SIBLING_EXECUTABLES must contain only external sources: "
            "'${_ror_sibling}'")
    endif()
endforeach()

if(EXISTS "${_ror_bundle}")
    foreach(_protected_path IN ITEMS
            "${_ror_contents}"
            "${_ror_macos}"
            "${_ror_frameworks}"
            "${_ror_plugins}"
            "${_ror_bundle_resources}")
        if(IS_SYMLINK "${_protected_path}")
            message(FATAL_ERROR "Refusing bundle with symlinked structural path: '${_protected_path}'")
        endif()
    endforeach()

    file(GLOB _existing_bundle_entries
        LIST_DIRECTORIES TRUE
        "${_ror_bundle}/*")
    if(_existing_bundle_entries)
        set(_existing_marker
            "${_ror_bundle_resources}/.ror-bundle-stager")
        set(_expected_marker
            "format=${_ror_stager_version}\nrenderer=GL3Plus\n")
        if(NOT EXISTS "${_existing_marker}" OR
                IS_DIRECTORY "${_existing_marker}" OR
                IS_SYMLINK "${_existing_marker}")
            message(FATAL_ERROR
                "Refusing to replace a non-empty bundle without the exact "
                "stager ownership marker: '${_ror_bundle}'")
        endif()
        file(READ "${_existing_marker}" _existing_marker_contents)
        if(NOT _existing_marker_contents STREQUAL _expected_marker)
            message(FATAL_ERROR
                "Refusing to replace a bundle with an incompatible stager "
                "ownership marker: '${_existing_marker}'")
        endif()

        if(NOT EXISTS "${_ror_destination_executable}" OR
                IS_DIRECTORY "${_ror_destination_executable}" OR
                IS_SYMLINK "${_ror_destination_executable}")
            message(FATAL_ERROR
                "Owned bundle is missing its exact executable destination: "
                "'${_ror_destination_executable}'")
        endif()

        set(_existing_plist "${_ror_contents}/Info.plist")
        if(NOT EXISTS "${_existing_plist}" OR
                IS_DIRECTORY "${_existing_plist}" OR
                IS_SYMLINK "${_existing_plist}")
            message(FATAL_ERROR
                "Owned bundle is missing its regular Info.plist: "
                "'${_existing_plist}'")
        endif()
        execute_process(
            COMMAND "${_ror_plutil}" -extract CFBundleExecutable raw
                -o - "${_existing_plist}"
            RESULT_VARIABLE _plist_result
            OUTPUT_VARIABLE _plist_executable
            ERROR_VARIABLE _plist_error
        )
        string(STRIP "${_plist_executable}" _plist_executable)
        if(NOT _plist_result EQUAL 0 OR
                NOT _plist_executable STREQUAL ROR_BUNDLE_EXECUTABLE_NAME)
            message(FATAL_ERROR
                "Existing app is not the exact requested bundle: "
                "CFBundleExecutable='${_plist_executable}', expected "
                "'${ROR_BUNDLE_EXECUTABLE_NAME}' (${_plist_error})")
        endif()
    endif()
endif()

set(_ror_ogre_media "${_ror_ogre_package_dir}/Media")
foreach(_media_directory IN ITEMS Main RTShaderLib Terrain)
    if(NOT IS_DIRECTORY "${_ror_ogre_media}/${_media_directory}")
        message(FATAL_ERROR
            "Pinned OGRE package is missing Media/${_media_directory}: "
            "'${_ror_ogre_package_dir}'")
    endif()
endforeach()

# Parse only active Plugin= entries. The input PluginFolder is deliberately
# ignored; the output is regenerated relative to Contents/MacOS/plugins.cfg.
file(STRINGS "${_ror_ogre_plugins_cfg}" _ror_plugin_cfg_lines ENCODING UTF-8)
set(_ror_plugin_names)
set(_ror_plugin_sources)
foreach(_line IN LISTS _ror_plugin_cfg_lines)
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "" OR _line MATCHES "^#")
        continue()
    endif()
    if(_line MATCHES "^Plugin[ \t]*=[ \t]*(.+)$")
        set(_entry "${CMAKE_MATCH_1}")
        string(REGEX REPLACE "[ \t]+#.*$" "" _entry "${_entry}")
        string(STRIP "${_entry}" _entry)
        _ror_resolve_plugin("${_entry}" _source _plugin_name)
        list(FIND _ror_plugin_names "${_plugin_name}" _duplicate_index)
        if(_duplicate_index EQUAL -1)
            list(APPEND _ror_plugin_names "${_plugin_name}")
            list(APPEND _ror_plugin_sources "${_source}")
        endif()
    endif()
endforeach()
list(FIND _ror_plugin_names "RenderSystem_GL3Plus" _gl3plus_index)
if(_gl3plus_index EQUAL -1)
    message(FATAL_ERROR
        "ROR_OGRE_PLUGINS_CFG must actively configure RenderSystem_GL3Plus")
endif()
foreach(_plugin_name IN LISTS _ror_plugin_names)
    if(_plugin_name MATCHES "^RenderSystem_" AND
            NOT _plugin_name STREQUAL "RenderSystem_GL3Plus")
        message(FATAL_ERROR
            "GL3Plus bundle cannot contain another active render system: '${_plugin_name}'")
    endif()
    if(_plugin_name MATCHES "Cg")
        message(FATAL_ERROR "Cg plugins are forbidden in the macOS OGRE 14 bundle")
    endif()
endforeach()

set(_ror_explicit_runtime_dylibs)
foreach(_dylib IN LISTS ROR_RUNTIME_DYLIBS)
    if(_dylib STREQUAL "")
        continue()
    endif()
    if(NOT IS_ABSOLUTE "${_dylib}" OR NOT EXISTS "${_dylib}" OR IS_DIRECTORY "${_dylib}")
        message(FATAL_ERROR "Invalid entry in ROR_RUNTIME_DYLIBS: '${_dylib}'")
    endif()
    _ror_real_path("${_dylib}" _dylib)
    _ror_path_is_within("${_dylib}" "${_ror_bundle}" _dylib_in_bundle)
    if(_dylib_in_bundle)
        message(FATAL_ERROR
            "ROR_RUNTIME_DYLIBS must name external source files, not staged "
            "bundle files: '${_dylib}'")
    endif()
    _ror_assert_macho("${_dylib}" "DYLIB")
    list(APPEND _ror_explicit_runtime_dylibs "${_dylib}")
endforeach()

set(_ror_runtime_search_dirs
    "${_ror_ogre_package_dir}/lib"
    "${_ror_ogre_plugin_dir}")
foreach(_search_dir IN LISTS ROR_RUNTIME_SEARCH_DIRS)
    if(_search_dir STREQUAL "")
        continue()
    endif()
    if(NOT IS_ABSOLUTE "${_search_dir}" OR NOT IS_DIRECTORY "${_search_dir}")
        message(FATAL_ERROR "Invalid entry in ROR_RUNTIME_SEARCH_DIRS: '${_search_dir}'")
    endif()
    _ror_real_path("${_search_dir}" _search_dir)
    _ror_path_is_within("${_search_dir}" "${_ror_bundle}" _search_dir_in_bundle)
    if(_search_dir_in_bundle)
        message(FATAL_ERROR
            "ROR_RUNTIME_SEARCH_DIRS must not search inside ROR_BUNDLE: "
            "'${_search_dir}'")
    endif()
    list(APPEND _ror_runtime_search_dirs "${_search_dir}")
endforeach()
foreach(_dylib IN LISTS _ror_explicit_runtime_dylibs)
    get_filename_component(_dylib_dir "${_dylib}" DIRECTORY)
    list(APPEND _ror_runtime_search_dirs "${_dylib_dir}")
endforeach()
list(REMOVE_DUPLICATES _ror_runtime_search_dirs)

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${_ror_executable}" ${_ror_sibling_executables}
    LIBRARIES ${_ror_plugin_sources} ${_ror_explicit_runtime_dylibs}
    DIRECTORIES ${_ror_runtime_search_dirs}
    RESOLVED_DEPENDENCIES_VAR _ror_resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR _ror_unresolved_dependencies
    CONFLICTING_DEPENDENCIES_PREFIX _ror_conflicts
    POST_EXCLUDE_REGEXES
        "^/System/Library/"
        "^/usr/lib/")
if(_ror_unresolved_dependencies)
    list(JOIN _ror_unresolved_dependencies "\n  " _unresolved_text)
    message(FATAL_ERROR "Unresolved runtime dependencies:\n  ${_unresolved_text}")
endif()
if(_ror_conflicts_FILENAMES)
    list(JOIN _ror_conflicts_FILENAMES "\n  " _conflict_text)
    message(FATAL_ERROR "Conflicting runtime dependency basenames:\n  ${_conflict_text}")
endif()

set(_ror_runtime_sources
    ${_ror_explicit_runtime_dylibs}
    ${_ror_resolved_dependencies})
list(REMOVE_DUPLICATES _ror_runtime_sources)
set(_ror_runtime_names)
set(_ror_runtime_real_sources)
foreach(_source IN LISTS _ror_runtime_sources)
    if(_source MATCHES "^/System/Library/" OR _source MATCHES "^/usr/lib/")
        continue()
    endif()
    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "Resolved dependency disappeared: '${_source}'")
    endif()
    get_filename_component(_runtime_name "${_source}" NAME)
    _ror_real_path("${_source}" _real_source)
    _ror_path_is_within("${_real_source}" "${_ror_bundle}" _source_in_bundle)
    if(_source_in_bundle)
        # An idempotent re-run with ROR_EXECUTABLE already inside the app can
        # make GET_RUNTIME_DEPENDENCIES prefer the previously staged copy.
        # Replace it with an explicitly searchable external source before the
        # owned Frameworks directory is cleared.
        set(_external_source "")
        foreach(_search_dir IN LISTS _ror_runtime_search_dirs)
            set(_candidate "${_search_dir}/${_runtime_name}")
            if(EXISTS "${_candidate}" AND NOT IS_DIRECTORY "${_candidate}")
                _ror_real_path("${_candidate}" _candidate)
                _ror_path_is_within(
                    "${_candidate}" "${_ror_bundle}" _candidate_in_bundle)
                if(NOT _candidate_in_bundle)
                    set(_external_source "${_candidate}")
                    break()
                endif()
            endif()
        endforeach()
        if(_external_source STREQUAL "")
            message(FATAL_ERROR
                "Dependency '${_runtime_name}' resolved only inside ROR_BUNDLE. "
                "Add its external directory to ROR_RUNTIME_SEARCH_DIRS.")
        endif()
        set(_real_source "${_external_source}")
    endif()
    if(NOT _runtime_name MATCHES "\\.dylib$")
        message(FATAL_ERROR
            "Non-system runtime dependency is not a standalone dylib: '${_source}'. "
            "Bundle its complete framework explicitly before using this stager.")
    endif()
    _ror_assert_macho("${_real_source}" "DYLIB")
    list(FIND _ror_runtime_names "${_runtime_name}" _existing_index)
    if(_existing_index EQUAL -1)
        list(APPEND _ror_runtime_names "${_runtime_name}")
        list(APPEND _ror_runtime_real_sources "${_real_source}")
    else()
        list(GET _ror_runtime_real_sources ${_existing_index} _existing_source)
        if(NOT _existing_source STREQUAL _real_source)
            message(FATAL_ERROR
                "Runtime dylib basename collision for '${_runtime_name}': "
                "'${_existing_source}' and '${_real_source}'")
        endif()
    endif()
endforeach()

set(_ror_staged_names)
set(_ror_staged_paths)
foreach(_runtime_name IN LISTS _ror_runtime_names)
    list(APPEND _ror_staged_names "${_runtime_name}")
    list(APPEND _ror_staged_paths "${_ror_frameworks}/${_runtime_name}")
endforeach()
foreach(_plugin_name IN LISTS _ror_plugin_names)
    set(_plugin_filename "${_plugin_name}.dylib")
    list(FIND _ror_staged_names "${_plugin_filename}" _collision_index)
    if(NOT _collision_index EQUAL -1)
        message(FATAL_ERROR "Plugin/runtime destination collision: '${_plugin_filename}'")
    endif()
    list(APPEND _ror_staged_names "${_plugin_filename}")
    list(APPEND _ror_staged_paths "${_ror_plugins}/${_plugin_filename}")
endforeach()

list(LENGTH _ror_plugin_names _ror_plugin_count)
list(LENGTH _ror_runtime_names _ror_runtime_count)
message(STATUS
    "Validated ${ROR_BUNDLE_NAME}.app staging plan: "
    "${_ror_plugin_count} OGRE plugins, ${_ror_runtime_count} runtime dylibs")
if(ROR_DRY_RUN)
    message(STATUS "Dry run: no bundle files were modified")
    return()
endif()

# All destructive/replacement operations start below this line, after the full
# input graph and exact output bundle have been validated.
if(EXISTS "${_ror_bundle}")
    file(REMOVE_RECURSE "${_ror_bundle}")
endif()
file(MAKE_DIRECTORY
    "${_ror_bundle}"
    "${_ror_contents}"
    "${_ror_macos}"
    "${_ror_bundle_resources}")
foreach(_owned_directory IN ITEMS
        "${_ror_macos}"
        "${_ror_frameworks}"
        "${_ror_plugins}"
        "${_ror_bundle_resources}/resources"
        "${_ror_bundle_resources}/languages"
        "${_ror_bundle_resources}/OGRE"
        "${_ror_bundle_resources}/content")
    if(EXISTS "${_owned_directory}" OR IS_SYMLINK "${_owned_directory}")
        file(REMOVE_RECURSE "${_owned_directory}")
    endif()
endforeach()
file(MAKE_DIRECTORY
    "${_ror_macos}"
    "${_ror_frameworks}"
    "${_ror_plugins}"
    "${_ror_bundle_resources}/resources"
    "${_ror_bundle_resources}/languages"
    "${_ror_bundle_resources}/OGRE")

if(NOT _ror_executable STREQUAL _ror_destination_executable)
    file(COPY_FILE
        "${_ror_executable}" "${_ror_destination_executable}"
        ONLY_IF_DIFFERENT)
endif()
file(CHMOD "${_ror_destination_executable}"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
foreach(_ror_sibling IN LISTS _ror_sibling_executables)
    get_filename_component(_ror_sibling_name "${_ror_sibling}" NAME)
    set(_ror_sibling_destination "${_ror_macos}/${_ror_sibling_name}")
    file(COPY_FILE
        "${_ror_sibling}" "${_ror_sibling_destination}"
        ONLY_IF_DIFFERENT)
    file(CHMOD "${_ror_sibling_destination}"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endforeach()

_ror_run_checked(
    "Copying RoR resources"
    "${CMAKE_COMMAND}" -E copy_directory
    "${_ror_resources}" "${_ror_bundle_resources}/resources")
_ror_run_checked(
    "Copying RoR languages"
    "${CMAKE_COMMAND}" -E copy_directory
    "${_ror_languages}" "${_ror_bundle_resources}/languages")
foreach(_media_directory IN ITEMS Main RTShaderLib Terrain)
    _ror_run_checked(
        "Copying OGRE ${_media_directory} media"
        "${CMAKE_COMMAND}" -E copy_directory
        "${_ror_ogre_media}/${_media_directory}"
        "${_ror_bundle_resources}/OGRE/${_media_directory}")
endforeach()

# AppContext currently resolves resources relative to Contents/MacOS. Keep the
# canonical data in Contents/Resources while exposing stable relative links.
set(_ror_ogre14_link "${_ror_bundle_resources}/resources/ogre14")
if(EXISTS "${_ror_ogre14_link}" OR IS_SYMLINK "${_ror_ogre14_link}")
    file(REMOVE_RECURSE "${_ror_ogre14_link}")
endif()
_ror_run_checked(
    "Linking RoR's OGRE 14 resource view"
    "${CMAKE_COMMAND}" -E create_symlink "../OGRE" "${_ror_ogre14_link}")

foreach(_resource_name IN ITEMS
        resources
        languages
        content
        plugins.cfg
        plugins_d.cfg)
    set(_link "${_ror_macos}/${_resource_name}")
    if(EXISTS "${_link}" OR IS_SYMLINK "${_link}")
        file(REMOVE_RECURSE "${_link}")
    endif()
endforeach()
_ror_run_checked(
    "Linking bundle resources beside the executable"
    "${CMAKE_COMMAND}" -E create_symlink
    "../Resources/resources" "${_ror_macos}/resources")
_ror_run_checked(
    "Linking bundle languages beside the executable"
    "${CMAKE_COMMAND}" -E create_symlink
    "../Resources/languages" "${_ror_macos}/languages")

if(NOT _ror_content STREQUAL "")
    file(MAKE_DIRECTORY "${_ror_bundle_resources}/content")
    if(IS_DIRECTORY "${_ror_content}")
        _ror_run_checked(
            "Copying optional content"
            "${CMAKE_COMMAND}" -E copy_directory
            "${_ror_content}" "${_ror_bundle_resources}/content")
    else()
        _ror_run_checked(
            "Copying optional content archive"
            "${CMAKE_COMMAND}" -E copy_if_different
            "${_ror_content}" "${_ror_bundle_resources}/content/")
    endif()
    _ror_run_checked(
        "Linking optional content beside the executable"
        "${CMAKE_COMMAND}" -E create_symlink
        "../Resources/content" "${_ror_macos}/content")
endif()

string(CONCAT _generated_plugins_cfg
    "# Generated by StageMacOSBundle.cmake; bundle-relative by design.\n"
    "PluginFolder=${ROR_OGRE_INSTALL_PLUGIN_FOLDER}\n\n")
foreach(_plugin_name IN LISTS _ror_plugin_names)
    string(APPEND _generated_plugins_cfg "Plugin=${_plugin_name}\n")
endforeach()
foreach(_plugins_config IN ITEMS plugins.cfg plugins_d.cfg)
    file(WRITE
        "${_ror_bundle_resources}/${_plugins_config}"
        "${_generated_plugins_cfg}")
    _ror_run_checked(
        "Linking ${_plugins_config} beside the executable"
        "${CMAKE_COMMAND}" -E create_symlink
        "../Resources/${_plugins_config}"
        "${_ror_macos}/${_plugins_config}")
endforeach()

list(LENGTH _ror_runtime_names _runtime_count)
if(_runtime_count GREATER 0)
    math(EXPR _runtime_last "${_runtime_count} - 1")
    foreach(_index RANGE 0 ${_runtime_last})
        list(GET _ror_runtime_names ${_index} _runtime_name)
        list(GET _ror_runtime_real_sources ${_index} _runtime_source)
        file(COPY_FILE
            "${_runtime_source}" "${_ror_frameworks}/${_runtime_name}"
            ONLY_IF_DIFFERENT)
    endforeach()
endif()

list(LENGTH _ror_plugin_names _plugin_count)
if(_plugin_count GREATER 0)
    math(EXPR _plugin_last "${_plugin_count} - 1")
    foreach(_index RANGE 0 ${_plugin_last})
        list(GET _ror_plugin_names ${_index} _plugin_name)
        list(GET _ror_plugin_sources ${_index} _plugin_source)
        file(COPY_FILE
            "${_plugin_source}" "${_ror_plugins}/${_plugin_name}.dylib"
            ONLY_IF_DIFFERENT)
    endforeach()
endif()

configure_file(
    "${ROR_INFO_PLIST_TEMPLATE}"
    "${_ror_contents}/Info.plist"
    @ONLY
    NEWLINE_STYLE UNIX)
_ror_run_checked(
    "Validating generated Info.plist"
    "${_ror_plutil}" -lint "${_ror_contents}/Info.plist")

set(_ror_framework_binaries)
foreach(_runtime_name IN LISTS _ror_runtime_names)
    list(APPEND _ror_framework_binaries "${_ror_frameworks}/${_runtime_name}")
endforeach()
set(_ror_plugin_binaries)
foreach(_plugin_name IN LISTS _ror_plugin_names)
    list(APPEND _ror_plugin_binaries "${_ror_plugins}/${_plugin_name}.dylib")
endforeach()

foreach(_binary IN LISTS _ror_framework_binaries _ror_plugin_binaries)
    get_filename_component(_binary_name "${_binary}" NAME)
    _ror_run_checked(
        "Setting a bundle-relative dylib ID for ${_binary_name}"
        "${_ror_install_name_tool}" -id "@rpath/${_binary_name}" "${_binary}")
endforeach()

foreach(_binary IN LISTS _ror_framework_binaries)
    _ror_rewrite_dependencies("${_binary}")
    _ror_set_exact_rpaths("${_binary}" "@loader_path")
endforeach()
foreach(_binary IN LISTS _ror_plugin_binaries)
    _ror_rewrite_dependencies("${_binary}")
    _ror_set_exact_rpaths(
        "${_binary}"
        "@loader_path"
        "@loader_path/../Frameworks")
endforeach()
set(_ror_bundle_executables
    "${_ror_destination_executable}"
    ${_ror_destination_sibling_executables})
foreach(_binary IN LISTS _ror_bundle_executables)
    _ror_rewrite_dependencies("${_binary}")
    _ror_set_exact_rpaths(
        "${_binary}"
        "@executable_path/../Frameworks"
        "@executable_path/../PlugIns")
endforeach()

foreach(_binary IN LISTS _ror_framework_binaries)
    _ror_verify_binary("${_binary}" "DYLIB" "@loader_path")
endforeach()
foreach(_binary IN LISTS _ror_plugin_binaries)
    _ror_verify_binary(
        "${_binary}" "DYLIB"
        "@loader_path"
        "@loader_path/../Frameworks")
endforeach()
foreach(_binary IN LISTS _ror_bundle_executables)
    _ror_verify_binary(
        "${_binary}" "EXECUTABLE"
        "@executable_path/../Frameworks"
        "@executable_path/../PlugIns")
endforeach()

# Sign nested code from the inside out. Do not use --deep for signing; every
# Mach-O is signed intentionally before the outer bundle seal is created.
foreach(_binary IN LISTS
        _ror_framework_binaries
        _ror_plugin_binaries
        _ror_bundle_executables)
    _ror_run_checked(
        "Ad-hoc signing ${_binary}"
        "${_ror_codesign}" --force --sign - --timestamp=none "${_binary}")
endforeach()
file(WRITE
    "${_ror_bundle_resources}/.ror-bundle-stager"
    "format=${_ror_stager_version}\nrenderer=GL3Plus\n")
_ror_run_checked(
    "Ad-hoc signing ${ROR_BUNDLE_NAME}.app"
    "${_ror_codesign}" --force --sign - --timestamp=none "${_ror_bundle}")
_ror_run_checked(
    "Verifying ${ROR_BUNDLE_NAME}.app signature"
    "${_ror_codesign}" --verify --deep --strict --verbose=2 "${_ror_bundle}")

# Re-check relocation invariants after signing so a successful result is a
# directly launchable artifact rather than merely a completed copy operation.
foreach(_binary IN LISTS _ror_framework_binaries)
    _ror_verify_binary("${_binary}" "DYLIB" "@loader_path")
endforeach()
foreach(_binary IN LISTS _ror_plugin_binaries)
    _ror_verify_binary(
        "${_binary}" "DYLIB"
        "@loader_path"
        "@loader_path/../Frameworks")
endforeach()
foreach(_binary IN LISTS _ror_bundle_executables)
    _ror_verify_binary(
        "${_binary}" "EXECUTABLE"
        "@executable_path/../Frameworks"
        "@executable_path/../PlugIns")
endforeach()

message(STATUS "Staged and verified relocatable bundle: ${_ror_bundle}")
