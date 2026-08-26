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

function(ror_ogre14_runtime_contract output_prefix system_name processor)
    string(TOLOWER "${system_name}" _ror_system)
    string(TOLOWER "${processor}" _ror_processor)

    if (_ror_system STREQUAL "darwin"
            AND _ror_processor MATCHES "^(arm64|aarch64)$")
        set(_ror_package_plugin_subdir "lib/OGRE")
        set(_ror_renderer_plugin "RenderSystem_GL3Plus")
        set(_ror_install_plugin_folder "../PlugIns")
        set(_ror_plugin_binaries_use_debug_suffix OFF)
    elseif (_ror_system STREQUAL "linux"
            AND _ror_processor MATCHES "^(x86_64|amd64)$")
        set(_ror_package_plugin_subdir "lib/OGRE")
        set(_ror_renderer_plugin "RenderSystem_GL3Plus")
        set(_ror_install_plugin_folder "lib/OGRE")
        set(_ror_plugin_binaries_use_debug_suffix OFF)
    elseif (_ror_system STREQUAL "windows"
            AND _ror_processor MATCHES "^(x86_64|amd64)$")
        set(_ror_package_plugin_subdir "bin")
        set(_ror_renderer_plugin "RenderSystem_Direct3D11")
        set(_ror_install_plugin_folder ".")
        set(_ror_plugin_binaries_use_debug_suffix ON)
    else ()
        message(FATAL_ERROR
            "ROR_OGRE14 has no runtime contract for "
            "${system_name}/${processor}")
    endif ()

    set(_ror_active_plugins
        Codec_FreeImage
        ${_ror_renderer_plugin}
        Plugin_ParticleFX
        Plugin_OctreeSceneManager)
    set(${output_prefix}_PACKAGE_PLUGIN_SUBDIR
        "${_ror_package_plugin_subdir}" PARENT_SCOPE)
    set(${output_prefix}_RENDERER_PLUGIN
        "${_ror_renderer_plugin}" PARENT_SCOPE)
    set(${output_prefix}_INSTALL_PLUGIN_FOLDER
        "${_ror_install_plugin_folder}" PARENT_SCOPE)
    set(${output_prefix}_ACTIVE_PLUGINS
        "${_ror_active_plugins}" PARENT_SCOPE)
    # OGRE appends this physical Windows Debug suffix itself. Plugin config
    # tokens remain unsuffixed on every platform.
    set(${output_prefix}_PLUGIN_BINARIES_USE_DEBUG_SUFFIX
        "${_ror_plugin_binaries_use_debug_suffix}" PARENT_SCOPE)
endfunction()

function(ror_ogre14_plugin_template_contract output_prefix renderer_plugin)
    set(_ror_comment_d3d9 "# ")
    set(_ror_comment_d3d11 "# ")
    set(_ror_comment_gl "# ")
    set(_ror_comment_gl3plus "# ")
    set(_ror_comment_metal "# ")

    if (renderer_plugin STREQUAL "RenderSystem_Direct3D11")
        set(_ror_comment_d3d11 "")
    elseif (renderer_plugin STREQUAL "RenderSystem_GL3Plus")
        set(_ror_comment_gl3plus "")
    else ()
        message(FATAL_ERROR
            "Unsupported OGRE 14 runtime renderer: ${renderer_plugin}")
    endif ()

    set(${output_prefix}_D3D9 "${_ror_comment_d3d9}" PARENT_SCOPE)
    set(${output_prefix}_D3D11 "${_ror_comment_d3d11}" PARENT_SCOPE)
    set(${output_prefix}_GL "${_ror_comment_gl}" PARENT_SCOPE)
    set(${output_prefix}_GL3PLUS "${_ror_comment_gl3plus}" PARENT_SCOPE)
    set(${output_prefix}_METAL "${_ror_comment_metal}" PARENT_SCOPE)
endfunction()

function(ror_ogre14_package_roots
        output_prefix is_multi_config build_type configuration_types)
    if (is_multi_config)
        if (configuration_types STREQUAL "")
            message(FATAL_ERROR
                "A multi-config OGRE 14 build has no configurations")
        endif ()
        foreach (_ror_config IN LISTS configuration_types)
            if (NOT _ror_config STREQUAL "Debug"
                    AND NOT _ror_config STREQUAL "Release")
                message(FATAL_ERROR
                    "ROR_OGRE14 multi-config builds support only Debug and "
                    "Release, not ${_ror_config}")
            endif ()
        endforeach ()
        foreach (_ror_config IN ITEMS RELEASE DEBUG)
            set(_ror_package_variable
                "ogre3d_PACKAGE_FOLDER_${_ror_config}")
            if (NOT DEFINED ${_ror_package_variable})
                message(FATAL_ERROR
                    "Conan CMakeDeps did not expose "
                    "${_ror_package_variable}")
            endif ()
            set(_ror_package_root "${${_ror_package_variable}}")
            if (NOT IS_ABSOLUTE "${_ror_package_root}"
                    OR NOT IS_DIRECTORY "${_ror_package_root}")
                message(FATAL_ERROR
                    "${_ror_package_variable} is not an absolute package "
                    "directory: ${_ror_package_root}")
            endif ()
            set(_ror_package_root_${_ror_config}
                "${_ror_package_root}")
        endforeach ()
        if ("${_ror_package_root_RELEASE}"
                STREQUAL "${_ror_package_root_DEBUG}")
            message(FATAL_ERROR
                "Conan CMakeDeps exposed the same OGRE 14 package for "
                "Release and Debug")
        endif ()
        set(_ror_media_root "${_ror_package_root_RELEASE}")
    else ()
        if (build_type STREQUAL "")
            message(FATAL_ERROR
                "A single-config OGRE 14 build requires CMAKE_BUILD_TYPE")
        endif ()
        string(TOUPPER "${build_type}" _ror_config)
        set(_ror_package_variable
            "ogre3d_PACKAGE_FOLDER_${_ror_config}")
        if (NOT DEFINED ${_ror_package_variable})
            message(FATAL_ERROR
                "Conan CMakeDeps did not expose ${_ror_package_variable}")
        endif ()
        set(_ror_package_root "${${_ror_package_variable}}")
        if (NOT IS_ABSOLUTE "${_ror_package_root}"
                OR NOT IS_DIRECTORY "${_ror_package_root}")
            message(FATAL_ERROR
                "${_ror_package_variable} is not an absolute package "
                "directory: ${_ror_package_root}")
        endif ()
        set(_ror_package_root_RELEASE "${_ror_package_root}")
        set(_ror_package_root_DEBUG "${_ror_package_root}")
        set(_ror_media_root "${_ror_package_root}")
    endif ()

    set(${output_prefix}_RELEASE
        "${_ror_package_root_RELEASE}" PARENT_SCOPE)
    set(${output_prefix}_DEBUG
        "${_ror_package_root_DEBUG}" PARENT_SCOPE)
    set(${output_prefix}_MEDIA
        "${_ror_media_root}" PARENT_SCOPE)
endfunction()

# Collect the runtime-library search roots exported by Conan 2 CMakeDeps.
#
# CMakeDeps deliberately exposes one immutable package root per package and
# configuration.  It does not provide the aggregate CONAN_RUNTIME_LIB_DIRS
# variable emitted by older Conan generators.  Enumerating the loaded
# *_PACKAGE_FOLDER_<CONFIG> variables keeps the dependency closure tied to the
# exact locked graph that find_package() resolved, while still including
# transitive shared-library packages.
function(ror_ogre14_cmakedeps_runtime_search_dirs
        output_variable build_type)
    if ("${output_variable}" STREQUAL "")
        message(FATAL_ERROR
            "A CMakeDeps runtime search-directory output is required")
    endif ()

    string(TOUPPER "${build_type}" _ror_config)
    if (NOT _ror_config MATCHES "^[A-Z][A-Z0-9_]*$")
        message(FATAL_ERROR
            "An exact CMakeDeps build configuration is required")
    endif ()

    get_cmake_property(_ror_all_variables VARIABLES)
    list(SORT _ror_all_variables)
    set(_ror_package_root_pattern
        "^[A-Za-z0-9_][A-Za-z0-9_.+-]*_PACKAGE_FOLDER_${_ror_config}$")
    set(_ror_package_root_count 0)
    set(_ror_runtime_search_dirs)
    foreach (_ror_variable IN LISTS _ror_all_variables)
        if (NOT _ror_variable MATCHES "${_ror_package_root_pattern}")
            continue()
        endif ()

        set(_ror_package_root_values "${${_ror_variable}}")
        list(LENGTH _ror_package_root_values _ror_package_root_value_count)
        if (NOT _ror_package_root_value_count EQUAL 1)
            message(FATAL_ERROR
                "CMakeDeps package root ${_ror_variable} must contain "
                "exactly one path")
        endif ()
        set(_ror_package_root "${${_ror_variable}}")
        if (NOT IS_ABSOLUTE "${_ror_package_root}"
                OR NOT IS_DIRECTORY "${_ror_package_root}"
                OR IS_SYMLINK "${_ror_package_root}")
            message(FATAL_ERROR
                "CMakeDeps package root ${_ror_variable} is not an "
                "absolute, non-symlink directory: ${_ror_package_root}")
        endif ()
        math(EXPR _ror_package_root_count
            "${_ror_package_root_count} + 1")

        foreach (_ror_runtime_subdirectory IN ITEMS lib bin)
            set(_ror_runtime_directory
                "${_ror_package_root}/${_ror_runtime_subdirectory}")
            if (EXISTS "${_ror_runtime_directory}"
                    AND NOT IS_DIRECTORY "${_ror_runtime_directory}")
                message(FATAL_ERROR
                    "CMakeDeps runtime path is not a directory: "
                    "${_ror_runtime_directory}")
            endif ()
            if (IS_DIRECTORY "${_ror_runtime_directory}")
                if (IS_SYMLINK "${_ror_runtime_directory}")
                    message(FATAL_ERROR
                        "CMakeDeps runtime directory must not be a symlink: "
                        "${_ror_runtime_directory}")
                endif ()
                list(APPEND _ror_runtime_search_dirs
                    "${_ror_runtime_directory}")
            endif ()
        endforeach ()
    endforeach ()

    if (_ror_package_root_count EQUAL 0)
        message(FATAL_ERROR
            "Conan CMakeDeps exposed no package roots for ${_ror_config}")
    endif ()
    if (NOT _ror_runtime_search_dirs)
        message(FATAL_ERROR
            "The ${_ror_config} CMakeDeps graph has no lib or bin runtime "
            "directories")
    endif ()
    list(REMOVE_DUPLICATES _ror_runtime_search_dirs)
    set(${output_variable}
        "${_ror_runtime_search_dirs}" PARENT_SCOPE)
endfunction()

# Serialize a validated CMake list into install(CODE) without allowing quotes,
# dollar expansions, or bracket terminators in paths to alter the generated
# install program.  The source is named instead of expanded through a function
# argument so every list item remains independently auditable.  The bracket
# delimiter grows until it cannot occur in the value being serialized.
function(ror_ogre14_install_set_list_code
        output_variable installed_variable source_list_variable)
    if (NOT ARGC EQUAL 3)
        message(FATAL_ERROR
            "Install list serializer requires exactly one source list "
            "variable")
    endif ()
    if (NOT "${installed_variable}" MATCHES
            "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
            "Unsafe install-time CMake variable name: ${installed_variable}")
    endif ()
    if (NOT "${source_list_variable}" MATCHES
            "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
            "Unsafe or undefined install-time source list: "
            "${source_list_variable}")
    endif ()
    if (NOT DEFINED ${source_list_variable})
        message(FATAL_ERROR
            "Unsafe or undefined install-time source list: "
            "${source_list_variable}")
    endif ()
    if ("${${source_list_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "Install-time list ${installed_variable} must not be empty")
    endif ()

    set(_ror_install_code "set(${installed_variable}\n")
    foreach (_ror_install_value IN LISTS ${source_list_variable})
        if ("${_ror_install_value}" STREQUAL "")
            message(FATAL_ERROR
                "Install-time list ${installed_variable} contains an "
                "empty value")
        endif ()
        if ("${_ror_install_value}" MATCHES ";")
            message(FATAL_ERROR
                "Install-time list ${installed_variable} contains a "
                "semicolon and cannot be serialized safely")
        endif ()
        set(_ror_bracket_equals "=")
        while (TRUE)
            set(_ror_bracket_close "]${_ror_bracket_equals}]")
            string(FIND
                "${_ror_install_value}"
                "${_ror_bracket_close}"
                _ror_bracket_close_position)
            if (_ror_bracket_close_position EQUAL -1)
                break()
            endif ()
            string(APPEND _ror_bracket_equals "=")
        endwhile ()
        string(APPEND _ror_install_code
            "    [${_ror_bracket_equals}[${_ror_install_value}]"
            "${_ror_bracket_equals}]\n")
    endforeach ()
    string(APPEND _ror_install_code ")\n")
    set(${output_variable} "${_ror_install_code}" PARENT_SCOPE)
endfunction()

function(ror_ogre14_media_root
        output_variable package_root system_name)
    if (NOT IS_ABSOLUTE "${package_root}"
            OR NOT IS_DIRECTORY "${package_root}")
        message(FATAL_ERROR
            "The OGRE 14 media package root is not an absolute directory: "
            "${package_root}")
    endif ()

    string(TOLOWER "${system_name}" _ror_system)
    if (_ror_system STREQUAL "linux")
        # Upstream installs Unix data below its versioned shared-data prefix.
        # Keep the version explicit so an OGRE upgrade cannot silently mix
        # incompatible shader libraries into the runtime.
        set(_ror_media_root
            "${package_root}/share/OGRE-14.5/Media")
    elseif (_ror_system STREQUAL "darwin"
            OR _ror_system STREQUAL "windows")
        set(_ror_media_root "${package_root}/Media")
    else ()
        message(FATAL_ERROR
            "ROR_OGRE14 has no media layout for ${system_name}")
    endif ()

    set(_ror_missing_media)
    foreach (_ror_component IN ITEMS Main RTShaderLib Terrain)
        if (NOT IS_DIRECTORY "${_ror_media_root}/${_ror_component}")
            list(APPEND _ror_missing_media "${_ror_component}")
        endif ()
    endforeach ()
    if (_ror_missing_media)
        list(JOIN _ror_missing_media ", " _ror_missing_media_text)
        message(FATAL_ERROR
            "The pinned OGRE 14 package media root "
            "${_ror_media_root} is missing: ${_ror_missing_media_text}")
    endif ()

    set(${output_variable} "${_ror_media_root}" PARENT_SCOPE)
endfunction()
