# Stage a closed, relocatable Linux x86_64 OGRE 14 runtime.
#
# Required inputs:
#   ROR_LINUX_BUILD_EXECUTABLE
#       Absolute build-tree RoR executable used to resolve its dependency
#       closure before install rpaths take effect.
#   ROR_LINUX_INSTALL_ROOT
#       Absolute DESTDIR-aware installation root containing the installed RoR
#       executable and plugins configuration.
#   ROR_LINUX_INSTALLED_EXECUTABLE_NAME
#       Package-root basename of the OGRE 14 game executable whose runtime
#       closure is being staged. Defaults to RoR for legacy single-process
#       packages and is RoR-Ogre14 for renderer-suite packages.
#   ROR_LINUX_PLUGIN_DIR
#       Absolute, configuration-specific Conan OGRE plugin directory.
#   ROR_LINUX_PLUGINS_CFG
#       Absolute installed-form plugins configuration.
#   ROR_LINUX_RUNTIME_SEARCH_DIRS
#       Conan-owned runtime library directories approved as closure sources.
#   ROR_LINUX_INSTALL_PLUGIN_FOLDER
#       Package-relative plugin directory. R0 requires lib/OGRE.
#   ROR_LINUX_CONTRACT_MODULE
#       Absolute path to LinuxRuntimeContract.cmake.
#
# Only config-selected plugins and ELF dependencies discovered from the RoR
# executable plus those plugins are copied. System libraries remain owned by
# the target distribution.

cmake_minimum_required(VERSION 3.16)

if (NOT DEFINED ROR_LINUX_INSTALLED_EXECUTABLE_NAME)
    set(ROR_LINUX_INSTALLED_EXECUTABLE_NAME "RoR")
endif ()

foreach (_ror_required_variable IN ITEMS
        ROR_LINUX_BUILD_EXECUTABLE
        ROR_LINUX_INSTALL_ROOT
        ROR_LINUX_PLUGIN_DIR
        ROR_LINUX_PLUGINS_CFG
        ROR_LINUX_RUNTIME_SEARCH_DIRS
        ROR_LINUX_INSTALL_PLUGIN_FOLDER
        ROR_LINUX_CONTRACT_MODULE)
    if (NOT DEFINED ${_ror_required_variable}
            OR "${${_ror_required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "Missing required Linux runtime variable: "
            "${_ror_required_variable}")
    endif ()
endforeach ()

if (NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "The Linux OGRE 14 runtime stager must execute on Linux")
endif ()
if (NOT ROR_LINUX_INSTALL_PLUGIN_FOLDER STREQUAL "lib/OGRE")
    message(FATAL_ERROR
        "The Linux OGRE 14 install plugin folder must be lib/OGRE")
endif ()
if (NOT ROR_LINUX_INSTALLED_EXECUTABLE_NAME MATCHES
        "^[A-Za-z0-9_.+-]+$")
    message(FATAL_ERROR
        "Unsafe installed Linux game executable name: "
        "${ROR_LINUX_INSTALLED_EXECUTABLE_NAME}")
endif ()
if (NOT IS_ABSOLUTE "${ROR_LINUX_CONTRACT_MODULE}"
        OR NOT EXISTS "${ROR_LINUX_CONTRACT_MODULE}")
    message(FATAL_ERROR
        "Linux runtime contract module is unavailable: "
        "${ROR_LINUX_CONTRACT_MODULE}")
endif ()
include("${ROR_LINUX_CONTRACT_MODULE}")

foreach (_ror_required_file IN ITEMS
        "${ROR_LINUX_BUILD_EXECUTABLE}"
        "${ROR_LINUX_PLUGINS_CFG}")
    if (NOT IS_ABSOLUTE "${_ror_required_file}"
            OR NOT EXISTS "${_ror_required_file}"
            OR IS_DIRECTORY "${_ror_required_file}")
        message(FATAL_ERROR
            "Linux runtime input is not an absolute file: "
            "${_ror_required_file}")
    endif ()
endforeach ()
if (NOT IS_ABSOLUTE "${ROR_LINUX_INSTALL_ROOT}"
        OR NOT IS_DIRECTORY "${ROR_LINUX_INSTALL_ROOT}")
    message(FATAL_ERROR
        "Linux runtime install root is not an absolute directory: "
        "${ROR_LINUX_INSTALL_ROOT}")
endif ()
set(_ror_installed_executable
    "${ROR_LINUX_INSTALL_ROOT}/${ROR_LINUX_INSTALLED_EXECUTABLE_NAME}")
if (NOT EXISTS "${_ror_installed_executable}"
        OR IS_DIRECTORY "${_ror_installed_executable}"
        OR IS_SYMLINK "${_ror_installed_executable}")
    message(FATAL_ERROR
        "Installed Linux RoR executable is unavailable or unsafe: "
        "${_ror_installed_executable}")
endif ()
ror_linux_ogre14_path_is_within(
    _ror_installed_executable_is_contained
    "${ROR_LINUX_INSTALL_ROOT}"
    "${_ror_installed_executable}")
if (NOT _ror_installed_executable_is_contained)
    message(FATAL_ERROR
        "Installed Linux RoR executable escapes its package: "
        "${_ror_installed_executable}")
endif ()

set(_ror_expected_plugins
    Codec_FreeImage
    RenderSystem_GL3Plus
    Plugin_ParticleFX
    Plugin_OctreeSceneManager)
ror_linux_ogre14_validate_plugins_config(
    _ror_active_plugins
    "${ROR_LINUX_PLUGINS_CFG}"
    "${ROR_LINUX_INSTALL_PLUGIN_FOLDER}"
    "${_ror_expected_plugins}")

set(_ror_plugin_sources)
set(_ror_plugin_reals)
foreach (_ror_plugin_name IN LISTS _ror_active_plugins)
    ror_linux_ogre14_resolve_plugin(
        _ror_plugin_source
        _ror_plugin_real
        "${ROR_LINUX_PLUGIN_DIR}"
        "${_ror_plugin_name}")
    list(APPEND _ror_plugin_sources ${_ror_plugin_source})
    list(APPEND _ror_plugin_reals "${_ror_plugin_real}")
endforeach ()
list(REMOVE_DUPLICATES _ror_plugin_reals)

set(_ror_runtime_search_directories
    ${ROR_LINUX_RUNTIME_SEARCH_DIRS}
    "${ROR_LINUX_PLUGIN_DIR}/..")
list(REMOVE_DUPLICATES _ror_runtime_search_directories)
ror_linux_ogre14_validate_runtime_paths(
    _ror_empty_runtime_paths
    "${_ror_runtime_search_directories}"
    "")

# The target distribution owns the ABI baseline. These paths are deliberately
# excluded rather than copied into the application package.
set(_ror_system_library_exclusions
    "^/lib/"
    "^/lib64/"
    "^/usr/lib/"
    "^/usr/lib64/")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${ROR_LINUX_BUILD_EXECUTABLE}"
    MODULES ${_ror_plugin_reals}
    DIRECTORIES ${_ror_runtime_search_directories}
    RESOLVED_DEPENDENCIES_VAR _ror_resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR _ror_unresolved_dependencies
    CONFLICTING_DEPENDENCIES_PREFIX _ror_conflicting_dependencies
    POST_EXCLUDE_REGEXES ${_ror_system_library_exclusions})

ror_linux_ogre14_assert_dependency_resolution(
    "${_ror_unresolved_dependencies}"
    "${_ror_conflicting_dependencies_FILENAMES}")
ror_linux_ogre14_validate_runtime_paths(
    _ror_validated_dependencies
    "${_ror_runtime_search_directories}"
    "${_ror_resolved_dependencies}")
ror_linux_ogre14_dependency_copy_roots(
    _ror_dependency_copy_roots
    "${_ror_validated_dependencies}")

find_program(ROR_LINUX_READELF readelf REQUIRED)
set(_ror_forbidden_loader_prefixes
    ${_ror_runtime_search_directories})
get_filename_component(
    _ror_build_directory
    "${ROR_LINUX_BUILD_EXECUTABLE}"
    DIRECTORY)
list(APPEND _ror_forbidden_loader_prefixes "${_ror_build_directory}")
list(REMOVE_DUPLICATES _ror_forbidden_loader_prefixes)

set(_ror_elf_sources
    "${_ror_installed_executable}"
    ${_ror_plugin_reals}
    ${_ror_validated_dependencies})
list(REMOVE_DUPLICATES _ror_elf_sources)
foreach (_ror_elf_source IN LISTS _ror_elf_sources)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "LC_ALL=C"
            "LANG=C"
            "${ROR_LINUX_READELF}" -h -d "${_ror_elf_source}"
        RESULT_VARIABLE _ror_readelf_result
        OUTPUT_VARIABLE _ror_readelf_output
        ERROR_VARIABLE _ror_readelf_error)
    if (NOT _ror_readelf_result EQUAL 0)
        message(FATAL_ERROR
            "readelf rejected Linux runtime file '${_ror_elf_source}': "
            "${_ror_readelf_error}")
    endif ()
    if (NOT _ror_readelf_output MATCHES
            "Machine:[ \t]*Advanced Micro Devices X86-64")
        message(FATAL_ERROR
            "Linux runtime file is not x86_64 ELF: ${_ror_elf_source}")
    endif ()
    ror_linux_ogre14_validate_loader_metadata(
        "${_ror_readelf_output}"
        "${_ror_elf_source}")
    foreach (_ror_forbidden_prefix IN LISTS
            _ror_forbidden_loader_prefixes)
        string(FIND
            "${_ror_readelf_output}"
            "${_ror_forbidden_prefix}"
            _ror_forbidden_prefix_index)
        if (NOT _ror_forbidden_prefix_index EQUAL -1)
            message(FATAL_ERROR
                "Linux runtime loader metadata retains a build/package "
                "prefix '${_ror_forbidden_prefix}' in "
                "${_ror_elf_source}")
        endif ()
    endforeach ()
endforeach ()

set(_ror_install_library_directory
    "${ROR_LINUX_INSTALL_ROOT}/lib")
set(_ror_install_plugin_directory
    "${ROR_LINUX_INSTALL_ROOT}/${ROR_LINUX_INSTALL_PLUGIN_FOLDER}")
foreach (_ror_owned_directory IN ITEMS
        "${_ror_install_library_directory}"
        "${_ror_install_plugin_directory}")
    if (IS_SYMLINK "${_ror_owned_directory}")
        message(FATAL_ERROR
            "Linux runtime staging directory must not be a symlink: "
            "${_ror_owned_directory}")
    endif ()
endforeach ()
file(MAKE_DIRECTORY
    "${_ror_install_library_directory}"
    "${_ror_install_plugin_directory}")

foreach (_ror_plugin_source IN LISTS _ror_plugin_sources)
    file(COPY
        "${_ror_plugin_source}"
        DESTINATION "${_ror_install_plugin_directory}"
        FOLLOW_SYMLINK_CHAIN)
endforeach ()
foreach (_ror_dependency IN LISTS _ror_dependency_copy_roots)
    file(COPY
        "${_ror_dependency}"
        DESTINATION "${_ror_install_library_directory}"
        FOLLOW_SYMLINK_CHAIN)
endforeach ()

ror_linux_ogre14_validate_installed_symlinks(
    "${ROR_LINUX_INSTALL_ROOT}"
    "${_ror_install_library_directory}")
ror_linux_ogre14_validate_installed_plugins(
    "${_ror_install_plugin_directory}"
    "${_ror_expected_plugins}")
foreach (_ror_installed_plugins_config IN ITEMS
        "${ROR_LINUX_INSTALL_ROOT}/plugins.cfg"
        "${ROR_LINUX_INSTALL_ROOT}/plugins_d.cfg")
    ror_linux_ogre14_validate_plugins_config(
        _ror_installed_active_plugins
        "${_ror_installed_plugins_config}"
        "${ROR_LINUX_INSTALL_PLUGIN_FOLDER}"
        "${_ror_expected_plugins}")
endforeach ()

list(LENGTH _ror_active_plugins _ror_active_plugin_count)
list(LENGTH
    _ror_validated_dependencies
    _ror_runtime_dependency_count)
message(STATUS
    "Staged Linux OGRE 14 runtime: "
    "${_ror_active_plugin_count} plugins, "
    "${_ror_runtime_dependency_count} runtime dependencies")
