cmake_minimum_required(VERSION 3.16)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_SOURCE_DIR}/cmake/find-modules)

cmake_policy(SET CMP0127 OLD)

include(CMakeDependentOption)
include(Macros)
include(FeatureSummary)
include(FetchContent)
include(CPackComponent)
include(VerifyStbImageSource)
ror_verify_stb_image_source("${CMAKE_SOURCE_DIR}")

################################################################################
# Project definition
################################################################################
set(PROJECT_NAME_SHORT "RoR")
set(PROJECT_NAME_UNDERSCORE "Rigs_of_Rods")
set(PROJECT_NAME_LONG "Rigs of Rods")

set(REQUIRED_DEPS_VERSION 30)

# macOS 11.0 is the first release with native Apple Silicon support. Set the
# supported floor before project() so CMake's Conan dependency provider and all
# source-built dependencies receive the same deployment target. Callers may
# still raise it explicitly for local builds.
if ((NOT CMAKE_SYSTEM_NAME AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "Minimum supported macOS version")
    endif ()
endif ()

# The version is used by installer
project(${PROJECT_NAME_UNDERSCORE} VERSION 2026.01)

################################################################################
# Available build options
################################################################################
option(ROR_BUILD_DEV_VERSION "Disable this for official releases" ON)
option(ROR_BUILD_DOC_DOXYGEN "Build documentation from sources with Doxygen" OFF)
option(ROR_BUILD_TESTS "Build deterministic dependency-free physics tests" OFF)
option(ROR_USE_PCH "Use a Precompiled header for speeding up the build" ON)
option(ROR_CREATE_CONTENT_FOLDER "Create the base content folder" ON)
option(
    ROR_OGRE14
    "Build the cross-platform OGRE 14 simulation and compatibility host"
    ON)
option(
    ROR_RENDERER_PUBLIC_LAUNCHER
    "Build and package the Ogre-Next-first public renderer chooser with the admitted renderer siblings"
    ON)
option(
    ROR_OGRE_NEXT_PRODUCTION_PACKAGE
    "Build and package the verified real OgreNext renderer child"
    ON)
option(
    ROR_OGRE_NEXT_DEMO_ADMISSION
    "Admit the verified OgreNext child and PSSM for an explicit demo build"
    OFF)
option(
    ROR_OGRE_NEXT_COMBINED_RUNTIME
    "Build the first one-process namespaced OgreNext renderer runtime"
    OFF)
set(ROR_DEPENDENCY_DIR "${CMAKE_SOURCE_DIR}/dependencies" CACHE PATH "Path to the dependencies")

if (ROR_RENDERER_PUBLIC_LAUNCHER AND NOT ROR_OGRE14)
    message(FATAL_ERROR
        "ROR_RENDERER_PUBLIC_LAUNCHER requires ROR_OGRE14 because the "
        "Ogre-Next presentation child uses RoR-Ogre14 as its simulation "
        "host and bounded compatibility fallback")
endif ()
if (ROR_OGRE_NEXT_PRODUCTION_PACKAGE AND
        NOT ROR_RENDERER_PUBLIC_LAUNCHER)
    message(FATAL_ERROR
        "ROR_OGRE_NEXT_PRODUCTION_PACKAGE requires the public renderer launcher")
endif ()
if (ROR_OGRE_NEXT_DEMO_ADMISSION AND
        NOT ROR_OGRE_NEXT_PRODUCTION_PACKAGE)
    message(FATAL_ERROR
        "ROR_OGRE_NEXT_DEMO_ADMISSION requires "
        "ROR_OGRE_NEXT_PRODUCTION_PACKAGE=ON")
endif ()
if (ROR_OGRE_NEXT_COMBINED_RUNTIME)
    if (NOT ROR_OGRE14)
        message(FATAL_ERROR
            "ROR_OGRE_NEXT_COMBINED_RUNTIME requires the transitional OGRE14 resource host")
    endif ()
    if (ROR_RENDERER_PUBLIC_LAUNCHER OR
            ROR_OGRE_NEXT_PRODUCTION_PACKAGE OR
            ROR_OGRE_NEXT_DEMO_ADMISSION)
        message(FATAL_ERROR
            "The combined runtime replaces the launcher/child bridge; configure all public child-package options OFF")
    endif ()
    get_property(_ror_combined_multi_config GLOBAL
        PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (_ror_combined_multi_config OR
            NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR
            NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR
            "RoR-Combined requires a single-config 64-bit Release build")
    endif ()
    if (APPLE)
        if (NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR
                NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" OR
                NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
            message(FATAL_ERROR
                "RoR-Combined supports only the reviewed macOS arm64 target")
        endif ()
    elseif (WIN32)
        if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
            message(FATAL_ERROR
                "RoR-Combined supports only the reviewed Windows x64 target")
        endif ()
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
            message(FATAL_ERROR
                "RoR-Combined supports only the reviewed Linux x86_64 target")
        endif ()
    else ()
        message(FATAL_ERROR
            "RoR-Combined has no reviewed Ogre-Next provider for this platform")
    endif ()
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    unset(_ror_combined_multi_config)
endif ()

if (APPLE AND ROR_RENDERER_PUBLIC_LAUNCHER)
    # The signed RoR.app stager owns dependency rewriting, nested-code signing,
    # and both executable roles. A flat CMake install would bypass those gates
    # and leave the OGRE 14 child's @rpath dependencies unresolved.
    file(GLOB_RECURSE
        _ror_stale_macos_install_scripts
        LIST_DIRECTORIES FALSE
        "${CMAKE_BINARY_DIR}/cmake_install.cmake"
        "${CMAKE_BINARY_DIR}/*/cmake_install.cmake")
    file(REMOVE
        ${_ror_stale_macos_install_scripts}
        "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
        "${CMAKE_BINARY_DIR}/CPackSourceConfig.cmake"
        "${CMAKE_BINARY_DIR}/CMakeFiles/package.util"
        "${CMAKE_BINARY_DIR}/CMakeFiles/package_source.util")
    unset(_ror_stale_macos_install_scripts)
    set(CMAKE_SKIP_INSTALL_RULES ON)
endif ()

if (ROR_OGRE14)
    include(Ogre14Platform)
    get_property(
        _ror_ogre14_multi_config
        GLOBAL
        PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (_ror_ogre14_multi_config)
        # Conan's provider installs exactly these two graphs. Restrict the
        # generator before the first dependency lookup so Visual Studio and
        # Xcode cannot expose unsupported RelWithDebInfo/MinSizeRel targets.
        set(
            CMAKE_CONFIGURATION_TYPES
            "Debug;Release"
            CACHE STRING
            "Supported OGRE 14 multi-config build types"
            FORCE)
    endif ()
    ror_select_ogre14_lockfile(
        _ror_ogre14_lockfile
        "${CMAKE_SYSTEM_NAME}"
        "${CMAKE_SYSTEM_PROCESSOR}"
    )
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/${_ror_ogre14_lockfile}")
        message(FATAL_ERROR
            "Pinned OGRE 14 lockfile is missing: ${_ror_ogre14_lockfile}")
    endif ()
    find_program(CONAN_COMMAND "conan" REQUIRED)
    foreach (_ror_recipe IN ITEMS ogre3d mygui)
        if (_ror_recipe STREQUAL "ogre3d")
            set(_ror_recipe_version "14.5.2")
        else ()
            set(_ror_recipe_version "3.4.0")
        endif ()
        execute_process(
            COMMAND
                ${CONAN_COMMAND} export
                "${CMAKE_SOURCE_DIR}/cmake/conan/recipes/${_ror_recipe}"
                "--version=${_ror_recipe_version}"
            RESULT_VARIABLE _ror_recipe_export_result
        )
        if (NOT _ror_recipe_export_result EQUAL 0)
            message(FATAL_ERROR "Failed to export ${_ror_recipe} migration recipe")
        endif ()
    endforeach ()
    unset(_ror_recipe)
    unset(_ror_recipe_version)
    unset(_ror_recipe_export_result)

    set(_ror_ogre14_conan_args
        "-o=&:ogre14=True"
        "--lockfile=${CMAKE_SOURCE_DIR}/${_ror_ogre14_lockfile}"
        "-c:h=tools.cmake:configure_args=[\"-DCMAKE_POLICY_VERSION_MINIMUM=3.5\"]"
    )
    foreach (_ror_conan_arg IN LISTS _ror_ogre14_conan_args)
        list(FIND CONAN_INSTALL_ARGS "${_ror_conan_arg}" _ror_conan_arg_index)
        if (_ror_conan_arg_index EQUAL -1)
            list(APPEND CONAN_INSTALL_ARGS "${_ror_conan_arg}")
        endif ()
    endforeach ()
    set(CONAN_INSTALL_ARGS "${CONAN_INSTALL_ARGS}" CACHE STRING
        "Command line arguments for conan install" FORCE)
    unset(_ror_ogre14_conan_args)
    unset(_ror_ogre14_multi_config)
    unset(_ror_conan_arg)
    unset(_ror_conan_arg_index)
    unset(_ror_ogre14_lockfile)
endif ()

set(ROR_BUILD_INSTALLER "Off" CACHE STRING
    "Build the Windows installer. Possible values:
    Off       - Do not build installer
    Online    - Build fully online installer
    Offline   - build installer without contentpack
    Bundled   - build installer with contentpack bundled"
)
set_property(CACHE ROR_BUILD_INSTALLER PROPERTY STRINGS Off Online Offline Bundled)

message("Create installer: ${ROR_BUILD_INSTALLER}")

set(ROR_FEAT_TIMING OFF)
set(FETCHCONTENT_QUIET OFF)

# global cmake options
SET(BUILD_SHARED_LIBS ON)
SET(CMAKE_USE_RELATIVE_PATHS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# setup paths
SET(CMAKE_PREFIX_PATH ${ROR_DEPENDENCY_DIR} ${CMAKE_PREFIX_PATH})
SET(RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")
SET(LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")
SET(ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/")
SET(EXECUTABLE_OUTPUT_PATH ${RUNTIME_OUTPUT_DIRECTORY})
SET(LIBRARY_OUTPUT_PATH ${RUNTIME_OUTPUT_DIRECTORY})

set(CMAKE_DEBUG_POSTFIX _d)

if (NOT ROR_USE_PCH)
    find_program(SCCACHE_PROGRAM sccache)
    if (SCCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
        message("Using ${SCCACHE_PROGRAM} for speeding up build")
    endif ()
endif ()

# Check deps version
if (EXISTS "${ROR_DEPENDENCY_DIR}/version.cmake")
    include("${ROR_DEPENDENCY_DIR}/version.cmake")
    if (NOT ${REQUIRED_DEPS_VERSION} EQUAL ${DEPENDENCIES_VERSION})
        message(FATAL_ERROR "\n The ror-dependencies version is ${DEPENDENCIES_VERSION}, while RoR requires ${REQUIRED_DEPS_VERSION} \n Please update the dependencies")
    endif ()
endif ()

# hide some settings
mark_as_advanced(
        CMAKE_DEBUG_POSTFIX
        CMAKE_BACKWARDS_COMPATIBILITY
        EXECUTABLE_OUTPUT_PATH
        LIBRARY_OUTPUT_PATH
)

if (WIN32)
    add_definitions("/D_CRT_SECURE_NO_WARNINGS /wd4005 /wd4996 /wd4251 /wd4275 /wd4099 /nologo")

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}        /MP /GL /Ox /Ob2 /Oi /Ot /Oy /fp:fast /GS- /MP /Zi")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}     /MP /Zi")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MP /Od /Zi /Gy /fp:except /GF- /GS /Ob0")
    #if(ROR_USING_VISUAL_STUDIO_PROFESSIONAL)
    #  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /analyze") # /analyze only available when using VS professional
    #endif(ROR_USING_VISUAL_STUDIO_PROFESSIONAL)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}          /MP /Od /Zi /RTC1 /Gy /fp:except /GF- /GS /Ob0")
    # some general flags
    add_definitions("-D_CRT_SECURE_NO_WARNINGS")
    include_directories(${DirectX_INCLUDE_DIR})

    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG}          /DEBUG /LARGEADDRESSAWARE ")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}        /DEBUG /LARGEADDRESSAWARE /OPT:REF /RELEASE /LTCG ")
    set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL}     /DEBUG /LARGEADDRESSAWARE /OPT:REF /LTCG ")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG /LARGEADDRESSAWARE /OPT:NOREF /MAP /DYNAMICBASE:NO /FIXED /NXCOMPAT:NO ")

    # fix executable paths for windows
    SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${RUNTIME_OUTPUT_DIRECTORY})
    SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${RUNTIME_OUTPUT_DIRECTORY})
    SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${RUNTIME_OUTPUT_DIRECTORY})
    SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${RUNTIME_OUTPUT_DIRECTORY})

ELSEIF (UNIX)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}          -Og -g -Wall")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}        -O2 -ffast-math -DNDEBUG -Wno-deprecated-declarations -Wno-attributes")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -ffast-math -g -fno-omit-frame-pointer -Wno-deprecated-declarations -Wno-attributes")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}     -Os -ffast-math -DNDEBUG")

    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE}")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}")

    # add additional gdb debugging stuff
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -Og -ggdb")
    set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS_DEBUG} -Og -ggdb")
endif (WIN32)

include(DependenciesConfig)

if (ROR_OGRE_NEXT_COMBINED_RUNTIME)
    if (APPLE)
        enable_language(OBJC)
        enable_language(OBJCXX)
    endif ()
    include(OgreNextEmbeddedRuntime)
    ror_add_ogre_next_embedded_runtime()
endif ()

if (ROR_OGRE_NEXT_PRODUCTION_PACKAGE)
    include(OgreNextProductionPackage)
    ror_add_ogre_next_production_package()
endif ()

################################################################################
# Recurse into subdirectories
################################################################################
add_subdirectory(external/angelscript_addons)
add_subdirectory(source/version_info)
add_subdirectory(source/main)
add_subdirectory(doc)

if (ROR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

feature_summary(WHAT ALL)
