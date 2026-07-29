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
        # The existing Linux install contract stages runtime shared objects
        # flat under <prefix>/lib. Keep the generated config truthful until
        # the dedicated relocatable plugin-binary closure lands.
        set(_ror_install_plugin_folder "lib")
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

    set(${output_prefix}_PACKAGE_PLUGIN_SUBDIR
        "${_ror_package_plugin_subdir}" PARENT_SCOPE)
    set(${output_prefix}_RENDERER_PLUGIN
        "${_ror_renderer_plugin}" PARENT_SCOPE)
    set(${output_prefix}_INSTALL_PLUGIN_FOLDER
        "${_ror_install_plugin_folder}" PARENT_SCOPE)
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
    set(${output_prefix}_CG "# " PARENT_SCOPE)
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
