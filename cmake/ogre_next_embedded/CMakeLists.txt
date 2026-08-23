if (NOT ROR_OGRE_NEXT_COMBINED_RUNTIME OR
        NOT ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    message(FATAL_ERROR
        "The embedded provider is private to the explicit root combined runtime")
endif ()

option(
    ROR_OGRE_NEXT_ALLOW_DIRTY_DEVELOPMENT_BUILD
    "Allow a manifest-bound dirty combined-runtime build while marking every proof as development-only and unqualified"
    OFF)
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
        NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR
        "The embedded provider requires a 64-bit Release target")
endif ()
if (APPLE)
    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR
            NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" OR
            NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR
            "The embedded provider supports only the reviewed macOS arm64 target")
    endif ()
    set(_ror_root_conan_lock_platform "macos-arm64")
    set(_ror_root_sdl_expected_library_name "libSDL2.a")
elseif (WIN32)
    if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        message(FATAL_ERROR
            "The embedded provider supports only the reviewed Windows x64 target")
    endif ()
    set(_ror_root_conan_lock_platform "windows-x86_64")
    set(_ror_root_sdl_expected_library_name "SDL2-static.lib")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        message(FATAL_ERROR
            "The embedded provider supports only the reviewed Linux x86_64 target")
    endif ()
    set(_ror_root_conan_lock_platform "linux-x86_64")
    set(_ror_root_sdl_expected_library_name "libSDL2.a")
else ()
    message(FATAL_ERROR
        "The embedded provider has no reviewed target for this platform")
endif ()
if (NOT TARGET SDL2::SDL2 OR NOT TARGET Threads::Threads OR
        NOT OGRE_LIBRARIES OR NOT OGRE_INCLUDE_DIRS)
    message(FATAL_ERROR
        "The embedded provider requires the complete root OGRE14/SDL/thread graph")
endif ()

set(_ror_root_ogre14_libraries "${OGRE_LIBRARIES}")
set(_ror_root_ogre14_include_dirs "${OGRE_INCLUDE_DIRS}")
set(ROR_ROOT_CONAN_LOCK
    "${CMAKE_SOURCE_DIR}/cmake/conan/locks/ror-ogre14-${_ror_root_conan_lock_platform}-release.lock")
if (NOT EXISTS "${ROR_ROOT_CONAN_LOCK}" OR
        IS_DIRECTORY "${ROR_ROOT_CONAN_LOCK}" OR
        IS_SYMLINK "${ROR_ROOT_CONAN_LOCK}")
    message(FATAL_ERROR "The reviewed combined-runtime Conan lock is unavailable")
endif ()
file(SHA256 "${ROR_ROOT_CONAN_LOCK}" ROR_ROOT_CONAN_LOCK_SHA256)
file(READ "${ROR_ROOT_CONAN_LOCK}" _ror_root_conan_lock_json)
string(JSON _ror_root_conan_lock_version GET
    "${_ror_root_conan_lock_json}" version)
string(JSON _ror_root_conan_require_count LENGTH
    "${_ror_root_conan_lock_json}" requires)
set(ROR_ROOT_SDL_CONAN_REFERENCE "")
math(EXPR _ror_root_conan_require_last
    "${_ror_root_conan_require_count} - 1")
foreach (_ror_root_conan_index RANGE 0 ${_ror_root_conan_require_last})
    string(JSON _ror_root_conan_requirement GET
        "${_ror_root_conan_lock_json}" requires
        ${_ror_root_conan_index})
    if (_ror_root_conan_requirement MATCHES "^sdl/")
        if (ROR_ROOT_SDL_CONAN_REFERENCE)
            message(FATAL_ERROR "The selected Conan lock contains multiple SDL recipes")
        endif ()
        set(ROR_ROOT_SDL_CONAN_REFERENCE
            "${_ror_root_conan_requirement}")
    endif ()
endforeach ()
if (NOT _ror_root_conan_lock_version STREQUAL "0.5" OR
        NOT ROR_ROOT_SDL_CONAN_REFERENCE MATCHES
            "^sdl/2[.]32[.]10#19432981a8779c918a13682d4186fa3b%[0-9.]+$")
    message(FATAL_ERROR "The selected root SDL Conan identity changed")
endif ()

# Prove that the root target resolves to one exact locked static archive. The
# Conan micro-target is the authoritative imported artifact beneath the
# public SDL2::SDL2 interface; aliases or a second fetched SDL are rejected.
if (WIN32)
    set(_ror_root_sdl_expected_conan_library "SDL2-static")
else ()
    set(_ror_root_sdl_expected_conan_library "SDL2")
endif ()
if (NOT SDL2_VERSION_STRING STREQUAL "2.32.10" OR
        NOT sdl_PACKAGE_FOLDER_RELEASE OR
        NOT IS_ABSOLUTE "${sdl_PACKAGE_FOLDER_RELEASE}" OR
        NOT sdl_SDL2_SDL2_LIBRARY_TYPE_RELEASE STREQUAL "STATIC" OR
        NOT sdl_SDL2_SDL2_LIBS_RELEASE STREQUAL
            _ror_root_sdl_expected_conan_library OR
        NOT sdl_SDL2_SDL2_LIBRARIES_TARGETS)
    message(FATAL_ERROR "The root SDL2::SDL2 imported package contract changed")
endif ()
list(LENGTH sdl_SDL2_SDL2_LIBRARIES_TARGETS
    _ror_root_sdl_imported_target_count)
if (NOT _ror_root_sdl_imported_target_count EQUAL 1)
    message(FATAL_ERROR
        "SDL2::SDL2 must resolve to exactly one imported archive target")
endif ()
list(GET sdl_SDL2_SDL2_LIBRARIES_TARGETS 0
    ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET)
if (NOT TARGET ${ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET})
    message(FATAL_ERROR "The root SDL imported artifact target is missing")
endif ()
get_target_property(_ror_root_sdl_imported_target_type
    ${ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET} TYPE)
get_target_property(_ror_root_sdl_imported_target_flag
    ${ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET} IMPORTED)
if (NOT _ror_root_sdl_imported_target_type STREQUAL "STATIC_LIBRARY" OR
        NOT _ror_root_sdl_imported_target_flag)
    message(FATAL_ERROR
        "The root SDL archive target is not one imported static library")
endif ()
# Conan's reviewed Windows OGRE package exports its SDL micro-target with an
# optimized RelWithDebInfo label even though the locked package graph and the
# root consumer are Release. Linux and macOS export the ordinary Release
# label. Read only the imported properties themselves: LOCATION_<CONFIG>
# asks CMake to resolve a consumer configuration and fails before we can
# authenticate and normalize this known package metadata variation.
set(_ror_root_sdl_location_configs RELEASE RELWITHDEBINFO)
set(_ror_root_sdl_imported_locations "")
set(_ror_root_sdl_imported_location_configs "")
foreach (_ror_root_sdl_location_config IN LISTS
        _ror_root_sdl_location_configs)
    get_target_property(_ror_root_sdl_location_candidate
        ${ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET}
        "IMPORTED_LOCATION_${_ror_root_sdl_location_config}")
    if (_ror_root_sdl_location_candidate AND
            NOT _ror_root_sdl_location_candidate MATCHES "-NOTFOUND$")
        list(APPEND _ror_root_sdl_imported_locations
            "${_ror_root_sdl_location_candidate}")
        list(APPEND _ror_root_sdl_imported_location_configs
            "${_ror_root_sdl_location_config}")
    endif ()
endforeach ()
list(LENGTH _ror_root_sdl_imported_locations
    _ror_root_sdl_imported_location_count)
if (NOT _ror_root_sdl_imported_location_count EQUAL 1)
    message(FATAL_ERROR
        "The root SDL target must expose exactly one imported archive location")
endif ()
list(GET _ror_root_sdl_imported_locations 0
    ROR_ROOT_SDL_IMPORTED_ARTIFACT)
list(GET _ror_root_sdl_imported_location_configs 0
    ROR_ROOT_SDL_IMPORTED_ARTIFACT_CONFIG)
get_target_property(_ror_root_sdl_interface
    SDL2::SDL2 INTERFACE_LINK_LIBRARIES)
string(JOIN ";" _ror_root_sdl_interface_serialized
    ${_ror_root_sdl_interface})
string(FIND "${_ror_root_sdl_interface_serialized}"
    "${ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET}"
    _ror_root_sdl_interface_target_index)
set(_ror_root_sdl_expected_artifact
    "${sdl_PACKAGE_FOLDER_RELEASE}/lib/${_ror_root_sdl_expected_library_name}")
if (NOT EXISTS "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}" OR
        IS_DIRECTORY "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}" OR
        IS_SYMLINK "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}")
    message(FATAL_ERROR
        "SDL2::SDL2 imported artifact is not one regular package archive")
endif ()
if (NOT EXISTS "${_ror_root_sdl_expected_artifact}" OR
        IS_DIRECTORY "${_ror_root_sdl_expected_artifact}" OR
        IS_SYMLINK "${_ror_root_sdl_expected_artifact}")
    message(FATAL_ERROR "The exact locked SDL package archive is unavailable")
endif ()
file(REAL_PATH "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}"
    _ror_root_sdl_imported_artifact_real)
file(REAL_PATH "${_ror_root_sdl_expected_artifact}"
    _ror_root_sdl_expected_artifact_real)
set(_ror_root_sdl_imported_artifact_compare
    "${_ror_root_sdl_imported_artifact_real}")
set(_ror_root_sdl_expected_artifact_compare
    "${_ror_root_sdl_expected_artifact_real}")
if (WIN32)
    # The locked archive is on a case-insensitive filesystem, while Conan's
    # generated target and package-folder variables may preserve different
    # drive/path casing. Both paths were resolved and proven regular above.
    string(TOLOWER "${_ror_root_sdl_imported_artifact_compare}"
        _ror_root_sdl_imported_artifact_compare)
    string(TOLOWER "${_ror_root_sdl_expected_artifact_compare}"
        _ror_root_sdl_expected_artifact_compare)
endif ()
if (NOT "${_ror_root_sdl_imported_artifact_compare}" STREQUAL
            "${_ror_root_sdl_expected_artifact_compare}")
    message(FATAL_ERROR
        "SDL2::SDL2 imported archive is outside the exact locked package path")
endif ()
if (_ror_root_sdl_interface_target_index EQUAL -1)
    message(FATAL_ERROR
        "SDL2::SDL2 does not link its exact imported archive target")
endif ()
# Make the one authenticated optimized archive usable by the root Release
# consumer. This is a configuration-label normalization on the exact Conan
# micro-target, not a library substitution.
set_property(TARGET ${ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET}
    PROPERTY MAP_IMPORTED_CONFIG_RELEASE
    "${ROR_ROOT_SDL_IMPORTED_ARTIFACT_CONFIG}")
file(SIZE "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}"
    ROR_ROOT_SDL_IMPORTED_ARTIFACT_SIZE)
file(SHA256 "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}"
    ROR_ROOT_SDL_IMPORTED_ARTIFACT_SHA256)
set(ROR_ROOT_SDL_CONANINFO
    "${sdl_PACKAGE_FOLDER_RELEASE}/conaninfo.txt")
set(ROR_ROOT_SDL_CONANMANIFEST
    "${sdl_PACKAGE_FOLDER_RELEASE}/conanmanifest.txt")
foreach (_ror_root_sdl_metadata IN ITEMS
        ROR_ROOT_SDL_CONANINFO ROR_ROOT_SDL_CONANMANIFEST)
    if (NOT EXISTS "${${_ror_root_sdl_metadata}}" OR
            IS_DIRECTORY "${${_ror_root_sdl_metadata}}" OR
            IS_SYMLINK "${${_ror_root_sdl_metadata}}")
        message(FATAL_ERROR
            "Root SDL package metadata is missing: ${_ror_root_sdl_metadata}")
    endif ()
endforeach ()
file(SHA256 "${ROR_ROOT_SDL_CONANINFO}"
    ROR_ROOT_SDL_CONANINFO_SHA256)
file(SHA256 "${ROR_ROOT_SDL_CONANMANIFEST}"
    ROR_ROOT_SDL_CONANMANIFEST_SHA256)
string(SHA256 ROR_ROOT_SDL_PACKAGE_IDENTITY_SHA256
    "${ROR_ROOT_SDL_CONAN_REFERENCE}|${ROR_ROOT_SDL_CONANINFO_SHA256}|${ROR_ROOT_SDL_CONANMANIFEST_SHA256}|${ROR_ROOT_SDL_IMPORTED_ARTIFACT_SIZE}|${ROR_ROOT_SDL_IMPORTED_ARTIFACT_SHA256}")

if (NOT ogre3d_PACKAGE_FOLDER_RELEASE OR
        NOT IS_ABSOLUTE "${ogre3d_PACKAGE_FOLDER_RELEASE}")
    message(FATAL_ERROR "The root OGRE14 package root is unavailable")
endif ()
if (WIN32)
    set(ROR_ROOT_OGRE14_RUNTIME_DIRECTORIES
        "${ogre3d_PACKAGE_FOLDER_RELEASE}/bin")
    set(_ror_root_ogre14_runtime_glob "*.dll")
    set(_ror_root_ogre14_runtime_pattern
        "^(Ogre|Plugin_|Codec_|RenderSystem_).*[.]dll$")
    set(_ror_root_ogre14_main_pattern "^OgreMain.*[.]dll$")
    set(_ror_root_ogre14_sdl_pattern "^OgreBites.*[.]dll$")
elseif (APPLE)
    set(ROR_ROOT_OGRE14_RUNTIME_DIRECTORIES
        "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib"
        "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib/OGRE")
    set(_ror_root_ogre14_runtime_glob "*.dylib")
    set(_ror_root_ogre14_runtime_pattern
        "^(libOgre|Plugin_|Codec_|RenderSystem_).*[.]dylib$")
    set(_ror_root_ogre14_main_pattern "^libOgreMain[.].*[.]dylib$")
    set(_ror_root_ogre14_sdl_pattern "^libOgreBites[.].*[.]dylib$")
else ()
    set(ROR_ROOT_OGRE14_RUNTIME_DIRECTORIES
        "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib"
        "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib/OGRE")
    set(_ror_root_ogre14_runtime_glob "*.so*")
    set(_ror_root_ogre14_runtime_pattern
        "^(libOgre|Plugin_|Codec_|RenderSystem_).*[.]so([.][0-9]+)*$")
    set(_ror_root_ogre14_main_pattern
        "^libOgreMain[.]so([.][0-9]+)*$")
    set(_ror_root_ogre14_sdl_pattern
        "^libOgreBites[.]so([.][0-9]+)*$")
endif ()
set(ROR_ROOT_OGRE14_RUNTIME_LIBRARIES "")
set(ROR_ROOT_OGRE14_MAIN_RUNTIME "")
set(ROR_ROOT_OGRE14_SDL_PROVIDER_RUNTIME "")
foreach (_ror_root_ogre14_runtime_dir IN LISTS
        ROR_ROOT_OGRE14_RUNTIME_DIRECTORIES)
    if (NOT IS_DIRECTORY "${_ror_root_ogre14_runtime_dir}" OR
            IS_SYMLINK "${_ror_root_ogre14_runtime_dir}")
        message(FATAL_ERROR
            "The root OGRE14 runtime directory is missing or indirect: ${_ror_root_ogre14_runtime_dir}")
    endif ()
    file(GLOB _ror_root_ogre14_runtime_candidates
        LIST_DIRECTORIES FALSE
        "${_ror_root_ogre14_runtime_dir}/${_ror_root_ogre14_runtime_glob}")
    foreach (_ror_root_ogre14_runtime IN LISTS
            _ror_root_ogre14_runtime_candidates)
        get_filename_component(_ror_root_ogre14_runtime_name
            "${_ror_root_ogre14_runtime}" NAME)
        if (IS_SYMLINK "${_ror_root_ogre14_runtime}")
            continue()
        endif ()
        if (NOT _ror_root_ogre14_runtime_name MATCHES
                "${_ror_root_ogre14_runtime_pattern}")
            message(FATAL_ERROR
                "Unexpected library in the root OGRE14 collision set: ${_ror_root_ogre14_runtime_name}")
        endif ()
        list(APPEND ROR_ROOT_OGRE14_RUNTIME_LIBRARIES
            "${_ror_root_ogre14_runtime}")
        if (_ror_root_ogre14_runtime_name MATCHES
                "${_ror_root_ogre14_main_pattern}")
            set(ROR_ROOT_OGRE14_MAIN_RUNTIME
                "${_ror_root_ogre14_runtime}")
        elseif (_ror_root_ogre14_runtime_name MATCHES
                "${_ror_root_ogre14_sdl_pattern}")
            set(ROR_ROOT_OGRE14_SDL_PROVIDER_RUNTIME
                "${_ror_root_ogre14_runtime}")
        endif ()
    endforeach ()
endforeach ()
list(REMOVE_DUPLICATES ROR_ROOT_OGRE14_RUNTIME_LIBRARIES)
list(SORT ROR_ROOT_OGRE14_RUNTIME_LIBRARIES)
list(LENGTH ROR_ROOT_OGRE14_RUNTIME_LIBRARIES
    ROR_ROOT_OGRE14_RUNTIME_LIBRARY_COUNT)
if (ROR_ROOT_OGRE14_RUNTIME_LIBRARY_COUNT LESS 3 OR
        NOT ROR_ROOT_OGRE14_MAIN_RUNTIME OR
        NOT ROR_ROOT_OGRE14_SDL_PROVIDER_RUNTIME)
    message(FATAL_ERROR
        "The complete OGRE14 main/plugin runtime collision set is unavailable")
endif ()
set(_ror_root_ogre14_runtime_manifest "")
foreach (_ror_root_ogre14_runtime IN LISTS
        ROR_ROOT_OGRE14_RUNTIME_LIBRARIES)
    file(RELATIVE_PATH _ror_root_ogre14_runtime_relative
        "${ogre3d_PACKAGE_FOLDER_RELEASE}"
        "${_ror_root_ogre14_runtime}")
    file(SIZE "${_ror_root_ogre14_runtime}"
        _ror_root_ogre14_runtime_size)
    file(SHA256 "${_ror_root_ogre14_runtime}"
        _ror_root_ogre14_runtime_sha256)
    string(APPEND _ror_root_ogre14_runtime_manifest
        "${_ror_root_ogre14_runtime_relative}|${_ror_root_ogre14_runtime_size}|${_ror_root_ogre14_runtime_sha256}\n")
endforeach ()
string(SHA256 ROR_ROOT_OGRE14_RUNTIME_MANIFEST_SHA256
    "${_ror_root_ogre14_runtime_manifest}")
set(ROR_ROOT_OGRE14_RUNTIME_PACKAGE_ROOT
    "${ogre3d_PACKAGE_FOLDER_RELEASE}"
    CACHE INTERNAL "Exact OGRE14 runtime package root for RoR-Combined" FORCE)
set(ROR_ROOT_OGRE14_MAIN_RUNTIME
    "${ROR_ROOT_OGRE14_MAIN_RUNTIME}"
    CACHE INTERNAL "Exact OGRE14 host runtime for RoR-Combined" FORCE)
set(ROR_ROOT_OGRE14_SDL_PROVIDER_RUNTIME
    "${ROR_ROOT_OGRE14_SDL_PROVIDER_RUNTIME}"
    CACHE INTERNAL "Exact OGRE14 Bites/SDL provider runtime for RoR-Combined" FORCE)
set(ROR_OGRE_NEXT_STANDALONE_ROOT
    "${CMAKE_SOURCE_DIR}/tools/ogre_next_probe")
set(ROR_OGRE_NEXT_EMBEDDED_NAMESPACE ON)
set(ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER ON)
include("${ROR_OGRE_NEXT_STANDALONE_ROOT}/cmake/PinnedOgreNext.cmake")
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

set(_ror_combined_platform_contract
    "macos-arm64-metal|RenderSystem_Metal"
    "windows-x64-d3d11|RenderSystem_Direct3D11"
    "linux-x86_64-vulkan|RenderSystem_Vulkan")
set(_ror_combined_platform_pair
    "${ROR_OGRE_NEXT_PLATFORM_POLICY}|${ROR_OGRE_NEXT_RENDERER_TARGET}")
if (NOT _ror_combined_platform_pair IN_LIST _ror_combined_platform_contract OR
        NOT ROR_OGRE_NEXT_PRESENTATION_SDL_TARGET STREQUAL "SDL2::SDL2")
    message(FATAL_ERROR "The embedded provider platform/SDL policy changed")
endif ()
set(ROR_OGRE_NEXT_RENDERER_TARGET
    "${ROR_OGRE_NEXT_RENDERER_TARGET}"
    CACHE INTERNAL "Selected embedded Ogre-Next renderer target" FORCE)

# The host's optimized configuration globally enables -ffast-math.  Pinned
# Ogre-Next's scalar Aabb implementation explicitly compares against infinity
# before transforming half extents; Apple Clang may compile those ternaries as
# unconditional local-half copies under finite-math-only assumptions.  Keep
# the host policy out of every fetched Ogre-Next C++/Objective-C++ target.  The
# final options intentionally follow the inherited configuration flags.
set(ROR_OGRE_NEXT_UPSTREAM_STRICT_FP_TARGETS "")
foreach (_ror_ogre_next_target IN LISTS
        ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_TARGETS)
    get_target_property(_ror_ogre_next_target_type
        ${_ror_ogre_next_target} TYPE)
    if (_ror_ogre_next_target_type STREQUAL "UTILITY" OR
            _ror_ogre_next_target_type STREQUAL "INTERFACE_LIBRARY")
        continue()
    endif ()
    get_target_property(_ror_ogre_next_strict_fp_applied
        ${_ror_ogre_next_target} ROR_OGRE_NEXT_STRICT_FP_APPLIED)
    if (_ror_ogre_next_strict_fp_applied)
        message(FATAL_ERROR
            "OgreNext strict FP was registered twice: ${_ror_ogre_next_target}")
    endif ()
    if (MSVC)
        target_compile_options(${_ror_ogre_next_target}
            PRIVATE
                "$<$<COMPILE_LANGUAGE:CXX>:/fp:strict>"
                "$<$<COMPILE_LANGUAGE:OBJCXX>:/fp:strict>")
    else ()
        target_compile_options(${_ror_ogre_next_target}
            PRIVATE
                "$<$<COMPILE_LANGUAGE:CXX>:-fno-fast-math>"
                "$<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>"
                "$<$<COMPILE_LANGUAGE:OBJCXX>:-fno-fast-math>"
                "$<$<COMPILE_LANGUAGE:OBJCXX>:-ffp-contract=off>")
    endif ()
    set_property(TARGET ${_ror_ogre_next_target} PROPERTY
        ROR_OGRE_NEXT_STRICT_FP_APPLIED TRUE)
    list(APPEND ROR_OGRE_NEXT_UPSTREAM_STRICT_FP_TARGETS
        ${_ror_ogre_next_target})
endforeach ()
list(REMOVE_DUPLICATES ROR_OGRE_NEXT_UPSTREAM_STRICT_FP_TARGETS)
list(SORT ROR_OGRE_NEXT_UPSTREAM_STRICT_FP_TARGETS)
list(LENGTH ROR_OGRE_NEXT_UPSTREAM_STRICT_FP_TARGETS
    ROR_OGRE_NEXT_UPSTREAM_STRICT_FP_TARGET_COUNT)
if (NOT ROR_OGRE_NEXT_UPSTREAM_STRICT_FP_TARGET_COUNT GREATER 0)
    message(FATAL_ERROR "No fetched OgreNext target received strict FP")
endif ()

# The root Conan toolchain intentionally prefers config packages. On the
# reviewed case-insensitive macOS filesystem, Ogre-Next's find_package(
# Rapidjson) therefore resolves the root RapidJSONConfig.cmake, which exports
# RapidJSON_INCLUDE_DIRS while upstream Ogre-Next consumes
# Rapidjson_INCLUDE_DIRS. Keep that case mismatch out of the target graph:
# attach the already pinned/extracted header closure directly and privately to
# the three targets which compile Ogre's JSON implementation. Do not link the
# root Conan `rapidjson` facade or export these headers to RoR/OGRE14 consumers.
set(ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR
    "${rapidjson_SOURCE_DIR}/include")
set(ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER
    "${ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR}/rapidjson/document.h")
set(ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER_SIZE 113794)
set(ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER_SHA256
    "da02371e1747e869c56fbe0a66d66630aefbefee6d615740f2895342c924fc8c")
if (NOT rapidjson_SOURCE_DIR OR
        NOT IS_ABSOLUTE "${rapidjson_SOURCE_DIR}" OR
        NOT IS_DIRECTORY "${rapidjson_SOURCE_DIR}" OR
        IS_SYMLINK "${rapidjson_SOURCE_DIR}" OR
        NOT IS_DIRECTORY "${ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR}" OR
        IS_SYMLINK "${ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR}" OR
        NOT EXISTS "${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER}" OR
        IS_DIRECTORY "${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER}" OR
        IS_SYMLINK "${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER}")
    message(FATAL_ERROR
        "The pinned RapidJSON private header closure is unavailable or indirect")
endif ()
file(SIZE "${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER}"
    _ror_ogre_next_rapidjson_document_header_size)
file(SHA256 "${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER}"
    _ror_ogre_next_rapidjson_document_header_sha256)
if (NOT _ror_ogre_next_rapidjson_document_header_size EQUAL
            ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER_SIZE OR
        NOT _ror_ogre_next_rapidjson_document_header_sha256 STREQUAL
            ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER_SHA256)
    message(FATAL_ERROR
        "The pinned RapidJSON document header bytes changed")
endif ()

set(ROR_OGRE_NEXT_RAPIDJSON_PRIVATE_CONSUMERS
    OgreNextMain
    OgreNextHlmsPbs
    OgreNextHlmsUnlit)
set(ROR_OGRE_NEXT_RAPIDJSON_NAMESPACE "RoROgreNextRapidJson")
file(REAL_PATH "${ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR}"
    _ror_rapidjson_canonical_include)
foreach (_ror_rapidjson_consumer IN LISTS
        ROR_OGRE_NEXT_RAPIDJSON_PRIVATE_CONSUMERS)
    if (NOT TARGET ${_ror_rapidjson_consumer})
        message(FATAL_ERROR
            "Missing RapidJSON consumer target: ${_ror_rapidjson_consumer}")
    endif ()
    get_target_property(_ror_rapidjson_consumer_type
        ${_ror_rapidjson_consumer} TYPE)
    if (NOT _ror_rapidjson_consumer_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR
            "RapidJSON consumer is not the reviewed static closure: ${_ror_rapidjson_consumer}")
    endif ()
    get_target_property(_ror_rapidjson_existing_private_usage
        ${_ror_rapidjson_consumer} INCLUDE_DIRECTORIES)
    set(_ror_rapidjson_existing_private_usage_count 0)
    foreach (_ror_rapidjson_usage IN LISTS
            _ror_rapidjson_existing_private_usage)
        if (IS_ABSOLUTE "${_ror_rapidjson_usage}" AND
                IS_DIRECTORY "${_ror_rapidjson_usage}")
            file(REAL_PATH "${_ror_rapidjson_usage}"
                _ror_rapidjson_canonical_usage)
            if (_ror_rapidjson_canonical_usage STREQUAL
                    _ror_rapidjson_canonical_include)
                math(EXPR _ror_rapidjson_existing_private_usage_count
                    "${_ror_rapidjson_existing_private_usage_count} + 1")
            endif ()
        endif ()
    endforeach ()
    if (_ror_rapidjson_existing_private_usage_count EQUAL 0)
        target_include_directories(${_ror_rapidjson_consumer}
            SYSTEM PRIVATE "${ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR}")
    endif ()
    get_target_property(_ror_rapidjson_private_usage
        ${_ror_rapidjson_consumer} INCLUDE_DIRECTORIES)
    set(_ror_rapidjson_private_usage_count 0)
    foreach (_ror_rapidjson_usage IN LISTS _ror_rapidjson_private_usage)
        if (IS_ABSOLUTE "${_ror_rapidjson_usage}" AND
                IS_DIRECTORY "${_ror_rapidjson_usage}")
            file(REAL_PATH "${_ror_rapidjson_usage}"
                _ror_rapidjson_canonical_usage)
            if (_ror_rapidjson_canonical_usage STREQUAL
                    _ror_rapidjson_canonical_include)
                math(EXPR _ror_rapidjson_private_usage_count
                    "${_ror_rapidjson_private_usage_count} + 1")
            endif ()
        endif ()
    endforeach ()
    get_target_property(_ror_rapidjson_interface_usage
        ${_ror_rapidjson_consumer} INTERFACE_INCLUDE_DIRECTORIES)
    set(_ror_rapidjson_interface_leaked FALSE)
    foreach (_ror_rapidjson_usage IN LISTS _ror_rapidjson_interface_usage)
        if (IS_ABSOLUTE "${_ror_rapidjson_usage}" AND
                IS_DIRECTORY "${_ror_rapidjson_usage}")
            file(REAL_PATH "${_ror_rapidjson_usage}"
                _ror_rapidjson_canonical_usage)
            if (_ror_rapidjson_canonical_usage STREQUAL
                    _ror_rapidjson_canonical_include)
                set(_ror_rapidjson_interface_leaked TRUE)
            endif ()
        endif ()
    endforeach ()
    if (NOT _ror_rapidjson_private_usage_count EQUAL 1 OR
            _ror_rapidjson_interface_leaked)
        message(FATAL_ERROR
            "RapidJSON must appear exactly once in private-only usage for ${_ror_rapidjson_consumer}")
    endif ()
endforeach ()

set(_ror_rapidjson_verify_script
    "${CMAKE_CURRENT_SOURCE_DIR}/VerifyRapidJsonPrivateClosure.cmake")
add_custom_target(ror_ogre_next_rapidjson_private_closure_verify
    COMMAND "${CMAKE_COMMAND}"
        "-DINCLUDE_DIR=${ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR}"
        "-DDOCUMENT_HEADER=${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER}"
        "-DEXPECTED_SIZE=${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER_SIZE}"
        "-DEXPECTED_SHA256=${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER_SHA256}"
        -P "${_ror_rapidjson_verify_script}"
    DEPENDS
        "${ROR_OGRE_NEXT_RAPIDJSON_DOCUMENT_HEADER}"
        "${_ror_rapidjson_verify_script}"
    COMMENT "Rehashing the private OgreNext RapidJSON header closure"
    VERBATIM)
foreach (_ror_rapidjson_consumer IN LISTS
        ROR_OGRE_NEXT_RAPIDJSON_PRIVATE_CONSUMERS)
    add_dependencies(${_ror_rapidjson_consumer}
        ror_ogre_next_rapidjson_private_closure_verify)
endforeach ()

set(_ror_render_root "${CMAKE_SOURCE_DIR}/source/main/gfx/render")
set(_ror_system_root "${CMAKE_SOURCE_DIR}/source/main/system")
set(_ror_generated_root "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_ror_generated_root}")

# Bind every renderer build identity to the checked-out source and to a
# byte-exact manifest of the renderer/provider inputs. A dirty integration
# worktree is representable because the content manifest, not a clean-claim,
# is the authority for the exact bytes compiled.
set(ROR_SOURCE_REPOSITORY "https://github.com/oasiz-ai/rigs-of-rods")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE _ror_source_commit_result
    OUTPUT_VARIABLE ROR_SOURCE_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}"
            rev-parse --abbrev-ref HEAD
    RESULT_VARIABLE _ror_source_ref_result
    OUTPUT_VARIABLE ROR_SOURCE_REF
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(LENGTH "${ROR_SOURCE_COMMIT}" _ror_source_commit_length)
if (NOT _ror_source_commit_result EQUAL 0 OR
        NOT _ror_source_ref_result EQUAL 0 OR
        NOT _ror_source_commit_length EQUAL 40 OR
        NOT ROR_SOURCE_COMMIT MATCHES "^[0-9a-f]+$" OR
        NOT ROR_SOURCE_REF MATCHES "^[A-Za-z0-9._/-]+$")
    message(FATAL_ERROR "The combined provider source identity is invalid")
endif ()

file(GLOB_RECURSE _ror_provider_manifest_files
    LIST_DIRECTORIES FALSE
    RELATIVE "${CMAKE_SOURCE_DIR}"
    "${_ror_render_root}/*"
    "${CMAKE_SOURCE_DIR}/cmake/ogre_next_embedded/*"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/*.json"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/*.h.in"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/*.py"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/cmake/*"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/embedded_namespace/*"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/licenses/*"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/media/*"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/patches/*"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/presentation_media/*"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/shaders/*"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/*")
list(APPEND _ror_provider_manifest_files
    "CMakeLists.txt"
    ".github/workflows/ogre-next-combined-native.yml"
    ".github/workflows/ogre-next-probe.yml"
    "cmake/DependenciesConfig.cmake"
    "cmake/linux/LinuxRuntimeContract.cmake"
    "cmake/linux/StageLinuxRuntime.cmake"
    "cmake/VerifyStbImageSource.cmake"
    "cmake/conan/locks/ror-ogre14-linux-x86_64-release.lock"
    "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock"
    "cmake/conan/locks/ror-ogre14-windows-x86_64-release.lock"
    "conanfile.py"
    "source/main/CMakeLists.txt"
    "source/main/main.cpp"
    "source/main/ogre-next-combined-executable-contract.json.in"
    "tests/CMakeLists.txt"
    "tests/gfx/render/RenderPayloadDigestTests.cpp"
    "tests/gfx/render/Ogre14SourceTextureDecoderTests.cpp"
    "tests/tools/test_ogre14_source_texture_decoder_contract.py"
    "tests/tools/test_ogre_next_combined_provider_contract.py"
    "cmake/OgreNextEmbeddedRuntime.cmake"
    "tools/stage_ogre_next_combined_resources.py"
    "tools/linux/RunRoR-combined"
    "tools/verify_ogre_next_combined_binary_closure.py"
    "tools/verify_ogre_next_combined_elf_closure.py"
    "tools/verify_ogre_next_combined_pe_closure.py"
    "tools/ogre_next_probe/audit_embedded_namespace.py"
    "tools/ogre_next_probe/CMakeLists.txt"
    "source/main/system/RendererGameInputTarget.cpp"
    "source/main/system/RendererGameInputTarget.h"
    "source/main/system/RendererInProcessSession.cpp"
    "source/main/system/RendererInProcessSession.h"
    "source/main/system/RendererOgreNextInProcessPresenter.cpp"
    "source/main/system/RendererOgreNextInProcessPresenter.h"
    "source/main/system/RendererOgreNextSdlWindowRuntime.cpp"
    "source/main/system/RendererOgreNextSdlWindowRuntime.h"
    "source/main/system/RendererOgreNextSdlWindowRuntimeCocoa.mm"
    "source/main/system/RendererOgreNextWindowHost.cpp"
    "source/main/system/RendererOgreNextWindowHost.h")
list(REMOVE_DUPLICATES _ror_provider_manifest_files)
list(SORT _ror_provider_manifest_files)
set(_ror_provider_manifest_serialized "")
foreach (_ror_provider_relative IN LISTS _ror_provider_manifest_files)
    set(_ror_provider_file "${CMAKE_SOURCE_DIR}/${_ror_provider_relative}")
    if (NOT EXISTS "${_ror_provider_file}" OR
            IS_DIRECTORY "${_ror_provider_file}" OR
            IS_SYMLINK "${_ror_provider_file}" OR
            _ror_provider_relative MATCHES "[;\\\"|]")
        message(FATAL_ERROR
            "Combined provider source is missing, indirect, or unsafe: ${_ror_provider_relative}")
    endif ()
    file(SIZE "${_ror_provider_file}" _ror_provider_size)
    file(SHA256 "${_ror_provider_file}" _ror_provider_sha256)
    string(APPEND _ror_provider_manifest_serialized
        "${_ror_provider_relative}|${_ror_provider_size}|${_ror_provider_sha256}\n")
endforeach ()
list(LENGTH _ror_provider_manifest_files _ror_source_manifest_file_count)
set(ROR_SOURCE_MANIFEST
    "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-combined-provider-sources.txt"
    CACHE INTERNAL "Exact combined provider source manifest" FORCE)
file(WRITE "${ROR_SOURCE_MANIFEST}"
    "${_ror_provider_manifest_serialized}")
file(SHA256 "${ROR_SOURCE_MANIFEST}" _ror_source_manifest_sha256)
set(ROR_SOURCE_MANIFEST_FILE_COUNT
    "${_ror_source_manifest_file_count}"
    CACHE INTERNAL "Exact combined provider source manifest entry count" FORCE)
set(ROR_SOURCE_MANIFEST_SHA256
    "${_ror_source_manifest_sha256}"
    CACHE INTERNAL "Exact combined provider source manifest digest" FORCE)

function(_ror_record_media_file _ror_source _ror_manifest_relative
        _ror_stage_relative _ror_entries_variable _ror_serialized_variable)
    if (NOT EXISTS "${_ror_source}" OR IS_DIRECTORY "${_ror_source}" OR
            IS_SYMLINK "${_ror_source}" OR
            NOT IS_ABSOLUTE "${_ror_source}" OR
            _ror_source MATCHES "[;\\\"|]" OR
            _ror_manifest_relative MATCHES "(^|/)\.\.(/|$)|[;\\\"|]" OR
            _ror_stage_relative MATCHES "(^|/)\.\.(/|$)|[;\\\"|]")
        message(FATAL_ERROR
            "Combined media source/path is missing, indirect, or unsafe: ${_ror_source}")
    endif ()
    file(SIZE "${_ror_source}" _ror_media_size)
    file(SHA256 "${_ror_source}" _ror_media_sha256)
    set(_ror_entries "${${_ror_entries_variable}}")
    string(APPEND _ror_entries
        "    {\"${_ror_manifest_relative}\", "
        "static_cast<std::uint64_t>(${_ror_media_size}), "
        "\"${_ror_media_sha256}\"},\n")
    set(${_ror_entries_variable} "${_ror_entries}" PARENT_SCOPE)
    set(_ror_serialized "${${_ror_serialized_variable}}")
    string(APPEND _ror_serialized
        "${_ror_manifest_relative}|${_ror_media_size}|${_ror_media_sha256}\n")
    set(${_ror_serialized_variable} "${_ror_serialized}" PARENT_SCOPE)
    if (NOT "${_ror_stage_relative}" STREQUAL "")
        if (NOT _ror_stage_relative MATCHES
                "^[A-Za-z0-9_./+@()~-]+$")
            message(FATAL_ERROR
                "Combined media stage path is not canonically sortable: ${_ror_stage_relative}")
        endif ()
        set(_ror_tuples "${_ror_stage_tuples}")
        # CMake sorts a string which ends at a prefix after strings which
        # continue with '.'. '!' is lower than every admitted path byte, so
        # this non-path sentinel makes prefix names match the Python verifier's
        # strict bytewise ordering without weakening its canonical-order gate.
        list(APPEND _ror_tuples
            "${_ror_stage_relative}!|${_ror_source}|${_ror_media_size}|${_ror_media_sha256}")
        set(_ror_stage_tuples "${_ror_tuples}" PARENT_SCOPE)
        set(_ror_sources "${_ror_stage_sources}")
        list(APPEND _ror_sources "${_ror_source}")
        set(_ror_stage_sources "${_ror_sources}" PARENT_SCOPE)
    endif ()
endfunction()

set(ROR_OGRE_NEXT_MEDIA_ROOT "${ogre_next_SOURCE_DIR}/Samples/Media")
set(_ror_n1_hlms_root "${ROR_OGRE_NEXT_MEDIA_ROOT}/Hlms")
set(ROR_OGRE_NEXT_DISPLAY_DOMAIN_MEDIA_RELATIVE
    "RoR/DisplayDomain/DisplayDomain_piece_ps.any")
set(ROR_OGRE_NEXT_DISPLAY_DOMAIN_MEDIA_SOURCE
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/media/Hlms/${ROR_OGRE_NEXT_DISPLAY_DOMAIN_MEDIA_RELATIVE}")
set(ROR_OGRE_NEXT_UV_AFFINE_PBS_MEDIA_RELATIVE
    "RoR/UvAffinePbs/UvAffinePbs_piece_ps.any")
set(ROR_OGRE_NEXT_UV_AFFINE_PBS_MEDIA_SOURCE
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/media/Hlms/${ROR_OGRE_NEXT_UV_AFFINE_PBS_MEDIA_RELATIVE}")
set(ROR_OGRE_NEXT_INDIRECT_ALPHA_MEDIA_RELATIVE
    "RoR/IndirectAlpha/IndirectAlpha_piece_ps.any")
set(ROR_OGRE_NEXT_INDIRECT_ALPHA_MEDIA_SOURCE
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/media/Hlms/${ROR_OGRE_NEXT_INDIRECT_ALPHA_MEDIA_RELATIVE}")
set(ROR_OGRE_NEXT_SUN_VISIBILITY_V2_MEDIA_RELATIVE
    "RoR/SunVisibilityV2/SunVisibilityV2.metal")
set(ROR_OGRE_NEXT_SUN_VISIBILITY_V2_MEDIA_SOURCE
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/media/Hlms/${ROR_OGRE_NEXT_SUN_VISIBILITY_V2_MEDIA_RELATIVE}")
set(ROR_OGRE_NEXT_SUN_VISIBILITY_V2_SHADER_LOCK
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/metal-sun-visibility-v2.lock.json")
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/verify_sun_visibility_v2_shader.py"
            --root "${ROR_OGRE_NEXT_STANDALONE_ROOT}"
            --lock "${ROR_OGRE_NEXT_SUN_VISIBILITY_V2_SHADER_LOCK}"
    RESULT_VARIABLE _ror_sun_visibility_v2_shader_result
    ERROR_VARIABLE _ror_sun_visibility_v2_shader_error)
if (NOT _ror_sun_visibility_v2_shader_result EQUAL 0)
    message(FATAL_ERROR
        "Metal sun-visibility V2 shader verification failed: "
        "${_ror_sun_visibility_v2_shader_error}")
endif ()
# RoR-owned aerial-perspective (haze) compositor media. These are 2.0 material
# scripts, not HLMS pieces, so they join the HDR manifest below; the pinned
# Ogre-Next glob cannot see them, so each file is appended explicitly.
set(ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE
    "2.0/scripts/materials/RoRHaze")
set(ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_RELATIVE
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/RoRAerialHaze.material"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRAerialHaze_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRAerialHaze_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRAerialHaze_ps.metal"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/RoRMetering.material"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRMeteringSumLumStart_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRMeteringSumLumStart_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRMeteringSumLumStart_ps.metal"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/RoRFinalToneMapping.material"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRFinalToneMapping_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRFinalToneMapping_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRFinalToneMapping_ps.metal"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/RoRTemporalAa.material"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRTaaMotionVectors_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRTaaMotionVectors_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRTaaMotionVectors_ps.metal"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRTaaResolve_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRTaaResolve_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRTaaResolve_ps.metal"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRTaaDepthStore_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRTaaDepthStore_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRTaaDepthStore_ps.metal"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/RoRScreenShade.material"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRScreenShadeAO_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRScreenShadeAO_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRScreenShadeAO_ps.metal"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRScreenShadeBlur_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRScreenShadeBlur_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRScreenShadeBlur_ps.metal"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/GLSL/RoRScreenShadeApply_ps.glsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/HLSL/RoRScreenShadeApply_ps.hlsl"
    "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_ROOT_RELATIVE}/Metal/RoRScreenShadeApply_ps.metal")
set(ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_SOURCE_ROOT
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/media")
set(ROR_OGRE_NEXT_AERIAL_HAZE_SHADER_LOCK
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ror-aerial-haze-v1.lock.json")
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/verify_aerial_haze_shader.py"
            --root "${ROR_OGRE_NEXT_STANDALONE_ROOT}"
            --lock "${ROR_OGRE_NEXT_AERIAL_HAZE_SHADER_LOCK}"
    RESULT_VARIABLE _ror_aerial_haze_shader_result
    ERROR_VARIABLE _ror_aerial_haze_shader_error)
if (NOT _ror_aerial_haze_shader_result EQUAL 0)
    message(FATAL_ERROR
        "RoR aerial-haze media verification failed: "
        "${_ror_aerial_haze_shader_error}")
endif ()
set(_ror_stage_tuples "")
set(_ror_stage_sources "")

file(GLOB_RECURSE _ror_n1_hlms_files
    LIST_DIRECTORIES FALSE
    RELATIVE "${_ror_n1_hlms_root}"
    "${_ror_n1_hlms_root}/*")
list(APPEND _ror_n1_hlms_files
    "${ROR_OGRE_NEXT_DISPLAY_DOMAIN_MEDIA_RELATIVE}"
    "${ROR_OGRE_NEXT_UV_AFFINE_PBS_MEDIA_RELATIVE}"
    "${ROR_OGRE_NEXT_INDIRECT_ALPHA_MEDIA_RELATIVE}"
    "${ROR_OGRE_NEXT_SUN_VISIBILITY_V2_MEDIA_RELATIVE}")
list(REMOVE_DUPLICATES _ror_n1_hlms_files)
list(SORT _ror_n1_hlms_files)
if (NOT _ror_n1_hlms_files)
    message(FATAL_ERROR "The combined shader manifest is empty")
endif ()
set(ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_ENTRIES "")
set(_ror_n1_media_manifest_serialized "")
foreach (_ror_hlms_relative IN LISTS _ror_n1_hlms_files)
    if (_ror_hlms_relative STREQUAL
            ROR_OGRE_NEXT_DISPLAY_DOMAIN_MEDIA_RELATIVE)
        set(_ror_hlms_source "${ROR_OGRE_NEXT_DISPLAY_DOMAIN_MEDIA_SOURCE}")
    elseif (_ror_hlms_relative STREQUAL
            ROR_OGRE_NEXT_UV_AFFINE_PBS_MEDIA_RELATIVE)
        set(_ror_hlms_source "${ROR_OGRE_NEXT_UV_AFFINE_PBS_MEDIA_SOURCE}")
    elseif (_ror_hlms_relative STREQUAL
            ROR_OGRE_NEXT_INDIRECT_ALPHA_MEDIA_RELATIVE)
        set(_ror_hlms_source
            "${ROR_OGRE_NEXT_INDIRECT_ALPHA_MEDIA_SOURCE}")
    elseif (_ror_hlms_relative STREQUAL
            ROR_OGRE_NEXT_SUN_VISIBILITY_V2_MEDIA_RELATIVE)
        set(_ror_hlms_source
            "${ROR_OGRE_NEXT_SUN_VISIBILITY_V2_MEDIA_SOURCE}")
    else ()
        set(_ror_hlms_source "${_ror_n1_hlms_root}/${_ror_hlms_relative}")
    endif ()
    _ror_record_media_file(
        "${_ror_hlms_source}" "${_ror_hlms_relative}"
        "ShaderMedia/Hlms/${_ror_hlms_relative}"
        ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_ENTRIES
        _ror_n1_media_manifest_serialized)
endforeach ()
list(LENGTH _ror_n1_hlms_files ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_COUNT)
string(SHA256 ROR_OGRE_NEXT_N1_MEDIA_MANIFEST_SHA256
    "${_ror_n1_media_manifest_serialized}")

set(_ror_n1_hdr_media_roots
    "2.0/scripts/Compositors"
    "2.0/scripts/materials/Common"
    "2.0/scripts/materials/HDR")
set(_ror_n1_hdr_media_files "")
foreach (_ror_hdr_root IN LISTS _ror_n1_hdr_media_roots)
    file(GLOB_RECURSE _ror_hdr_files
        LIST_DIRECTORIES FALSE
        RELATIVE "${ROR_OGRE_NEXT_MEDIA_ROOT}"
        "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_hdr_root}/*")
    list(APPEND _ror_n1_hdr_media_files ${_ror_hdr_files})
endforeach ()
list(APPEND _ror_n1_hdr_media_files
    ${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_RELATIVE})
list(REMOVE_DUPLICATES _ror_n1_hdr_media_files)
list(SORT _ror_n1_hdr_media_files)
if (NOT _ror_n1_hdr_media_files)
    message(FATAL_ERROR "The combined HDR media manifest is empty")
endif ()
set(ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_ENTRIES "")
set(_ror_n1_hdr_media_manifest_serialized "")
foreach (_ror_hdr_relative IN LISTS _ror_n1_hdr_media_files)
    # RoR-owned haze media lives in the standalone probe tree, not in the
    # pinned Ogre-Next media root; everything else resolves as before.
    list(FIND ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_RELATIVE
        "${_ror_hdr_relative}" _ror_hdr_haze_index)
    if (_ror_hdr_haze_index GREATER_EQUAL 0)
        set(_ror_hdr_media_source
            "${ROR_OGRE_NEXT_AERIAL_HAZE_MEDIA_SOURCE_ROOT}/${_ror_hdr_relative}")
    else ()
        set(_ror_hdr_media_source
            "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_hdr_relative}")
    endif ()
    _ror_record_media_file(
        "${_ror_hdr_media_source}"
        "${_ror_hdr_relative}" "ShaderMedia/${_ror_hdr_relative}"
        ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_ENTRIES
        _ror_n1_hdr_media_manifest_serialized)
endforeach ()
list(LENGTH _ror_n1_hdr_media_files
    ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_COUNT)
string(SHA256 ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_SHA256
    "${_ror_n1_hdr_media_manifest_serialized}")

set(_ror_reflection_media_files
    "2.0/scripts/materials/Common/Any/PccDepthCompressor_ps.any"
    "2.0/scripts/materials/Common/GLSL/PccDepthCompressor_ps.glsl"
    "2.0/scripts/materials/Common/GLSL/QuadCameraDirNoUV_vs.glsl"
    "2.0/scripts/materials/Common/GLSL/QuadCameraDir_vs.glsl"
    "2.0/scripts/materials/Common/GLSL/Quad_vs.glsl"
    "2.0/scripts/materials/Common/HLSL/PccDepthCompressor_ps.hlsl"
    "2.0/scripts/materials/Common/HLSL/QuadCameraDirNoUV_vs.hlsl"
    "2.0/scripts/materials/Common/HLSL/QuadCameraDir_vs.hlsl"
    "2.0/scripts/materials/Common/HLSL/Quad_vs.hlsl"
    "2.0/scripts/materials/Common/Metal/PccDepthCompressor_ps.metal"
    "2.0/scripts/materials/Common/Metal/QuadCameraDirNoUV_vs.metal"
    "2.0/scripts/materials/Common/Metal/QuadCameraDir_vs.metal"
    "2.0/scripts/materials/Common/Metal/Quad_vs.metal"
    "2.0/scripts/materials/Common/PccDepthCompressor.material"
    "2.0/scripts/materials/Common/Quad.program"
    "2.0/scripts/materials/LocalCubemaps/BlendProjectCubemap.material"
    "2.0/scripts/materials/LocalCubemaps/CopyCubemap.material"
    "2.0/scripts/materials/LocalCubemaps/GLSL/BlendProjectCubemap_ps.glsl"
    "2.0/scripts/materials/LocalCubemaps/GLSL/BlendProjectCubemap_vs.glsl"
    "2.0/scripts/materials/LocalCubemaps/GLSL/CopyCubemap_ps.glsl"
    "2.0/scripts/materials/LocalCubemaps/HLSL/BlendProjectCubemap_ps.hlsl"
    "2.0/scripts/materials/LocalCubemaps/HLSL/BlendProjectCubemap_vs.hlsl"
    "2.0/scripts/materials/LocalCubemaps/HLSL/CopyCubemap_ps.hlsl"
    "2.0/scripts/materials/LocalCubemaps/Metal/BlendProjectCubemap_ps.metal"
    "2.0/scripts/materials/LocalCubemaps/Metal/BlendProjectCubemap_vs.metal"
    "2.0/scripts/materials/LocalCubemaps/Metal/CopyCubemap_ps.metal"
    "Compute/Algorithms/IBL/IBL.material.json"
    "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.glsl"
    "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.hlsl"
    "Compute/Algorithms/IBL/SpecularIblIntegrator_cs.metal"
    "Compute/Algorithms/IBL/SpecularIblIntegrator_piece_cs.any"
    "Compute/Tools/Any/sRGB.any")
list(SORT _ror_reflection_media_files)
set(ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_ENTRIES "")
set(_ror_reflection_media_manifest_serialized "")
foreach (_ror_reflection_relative IN LISTS _ror_reflection_media_files)
    if (_ror_reflection_relative MATCHES
            "^2[.]0/scripts/materials/Common/")
        set(_ror_reflection_stage_relative "")
    else ()
        set(_ror_reflection_stage_relative
            "ShaderMedia/${_ror_reflection_relative}")
    endif ()
    _ror_record_media_file(
        "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_reflection_relative}"
        "${_ror_reflection_relative}" "${_ror_reflection_stage_relative}"
        ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_ENTRIES
        _ror_reflection_media_manifest_serialized)
endforeach ()
list(LENGTH _ror_reflection_media_files
    ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_COUNT)
string(SHA256 ROR_OGRE_NEXT_REFLECTION_MEDIA_MANIFEST_SHA256
    "${_ror_reflection_media_manifest_serialized}")

# Authenticate the deliberately minimal native presentation resource group
# against its independent source lock before generating either headers or the
# build-tree stage.
set(ROR_OGRE_NEXT_PRESENTATION_COPY_LOCK
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ogre-next-presentation-copy-v1.lock.json")
file(SHA256 "${ROR_OGRE_NEXT_PRESENTATION_COPY_LOCK}"
    ROR_OGRE_NEXT_PRESENTATION_COPY_LOCK_SHA256)
file(READ "${ROR_OGRE_NEXT_PRESENTATION_COPY_LOCK}"
    _ror_presentation_copy_lock_json)
string(JSON _ror_presentation_copy_schema GET
    "${_ror_presentation_copy_lock_json}" schema)
string(JSON _ror_presentation_copy_ogre_commit GET
    "${_ror_presentation_copy_lock_json}" ogre_next_commit)
string(JSON _ror_presentation_copy_material GET
    "${_ror_presentation_copy_lock_json}" material)
string(JSON _ror_presentation_copy_file_count LENGTH
    "${_ror_presentation_copy_lock_json}" files)
if (NOT _ror_presentation_copy_schema STREQUAL
        "ror.ogre_next.presentation_copy_media.v1" OR
        NOT _ror_presentation_copy_ogre_commit STREQUAL
            ROR_OGRE_NEXT_COMMIT OR
        NOT _ror_presentation_copy_material STREQUAL "Ogre/Copy/4xFP32" OR
        NOT _ror_presentation_copy_file_count EQUAL 8)
    message(FATAL_ERROR "The combined presentation copy lock changed")
endif ()
set(ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/presentation-media-authenticated")
file(MAKE_DIRECTORY
    "${ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT}/CommonCopy/GLSL"
    "${ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT}/CommonCopy/HLSL"
    "${ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT}/CommonCopy/Metal")
set(_ror_presentation_staged_files "")
math(EXPR _ror_presentation_copy_last
    "${_ror_presentation_copy_file_count} - 1")
foreach (_ror_presentation_index RANGE 0 ${_ror_presentation_copy_last})
    string(JSON _ror_presentation_path GET
        "${_ror_presentation_copy_lock_json}" files
        ${_ror_presentation_index} path)
    string(JSON _ror_presentation_size GET
        "${_ror_presentation_copy_lock_json}" files
        ${_ror_presentation_index} size)
    string(JSON _ror_presentation_sha256 GET
        "${_ror_presentation_copy_lock_json}" files
        ${_ror_presentation_index} sha256)
    if (_ror_presentation_path MATCHES
            "^tools/ogre_next_probe/presentation_media/CommonCopy/([^/]+)$")
        set(_ror_presentation_source
            "${CMAKE_SOURCE_DIR}/${_ror_presentation_path}")
        set(_ror_presentation_relative "CommonCopy/${CMAKE_MATCH_1}")
    elseif (_ror_presentation_path MATCHES
            "^Samples/Media/2[.]0/scripts/materials/Common/(GLSL|HLSL|Metal)/([^/]+)$")
        set(_ror_presentation_source
            "${ogre_next_SOURCE_DIR}/${_ror_presentation_path}")
        set(_ror_presentation_relative
            "CommonCopy/${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
    else ()
        message(FATAL_ERROR
            "Presentation copy lock contains an unreviewed path: ${_ror_presentation_path}")
    endif ()
    if (NOT EXISTS "${_ror_presentation_source}" OR
            IS_DIRECTORY "${_ror_presentation_source}" OR
            IS_SYMLINK "${_ror_presentation_source}")
        message(FATAL_ERROR
            "Presentation source is missing or indirect: ${_ror_presentation_path}")
    endif ()
    file(SIZE "${_ror_presentation_source}" _ror_observed_size)
    file(SHA256 "${_ror_presentation_source}" _ror_observed_sha256)
    if (NOT _ror_observed_size EQUAL _ror_presentation_size OR
            NOT _ror_observed_sha256 STREQUAL _ror_presentation_sha256)
        message(FATAL_ERROR
            "Presentation source bytes changed: ${_ror_presentation_path}")
    endif ()
    set(_ror_presentation_authenticated
        "${ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT}/${_ror_presentation_relative}")
    configure_file(
        "${_ror_presentation_source}"
        "${_ror_presentation_authenticated}"
        COPYONLY)
    list(APPEND _ror_presentation_staged_files
        "${_ror_presentation_relative}")
endforeach ()

# VerifyMediaManifest() compares its bytewise-sorted runtime path inventory
# directly with this generated array. The source lock is intentionally grouped
# by authored root files and backend family, so canonicalize the manifest here
# instead of weakening the runtime integrity comparison.
list(SORT _ror_presentation_staged_files)
set(ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_ENTRIES "")
set(_ror_presentation_media_manifest_serialized "")
foreach (_ror_presentation_relative IN LISTS _ror_presentation_staged_files)
    set(_ror_presentation_authenticated
        "${ROR_OGRE_NEXT_PRESENTATION_MEDIA_ROOT}/${_ror_presentation_relative}")
    _ror_record_media_file(
        "${_ror_presentation_authenticated}"
        "${_ror_presentation_relative}"
        "Presentation/${_ror_presentation_relative}"
        ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_ENTRIES
        _ror_presentation_media_manifest_serialized)
endforeach ()
set(ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_COUNT
    "${_ror_presentation_copy_file_count}")
string(SHA256 ROR_OGRE_NEXT_N1_PRESENTATION_MEDIA_MANIFEST_SHA256
    "${_ror_presentation_media_manifest_serialized}")

configure_file(
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/frontend_n1_media_manifest.h.in"
    "${_ror_generated_root}/ror_ogre_next_n1_media_manifest.h"
    @ONLY)
configure_file(
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/frontend_presentation_media_manifest.h.in"
    "${_ror_generated_root}/ror_ogre_next_presentation_media_manifest.h"
    @ONLY)
configure_file(
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/frontend_reflection_media_manifest.h.in"
    "${_ror_generated_root}/ror_ogre_next_reflection_media_manifest.h"
    @ONLY)
set(ROR_OGRE_NEXT_N1_PACKAGE_MEDIA_RELATIVE
    "resources/ogrenext/ShaderMedia")
configure_file(
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/frontend_n1_config.h.in"
    "${_ror_generated_root}/ror_ogre_next_n1_config.h"
    @ONLY)

set(ROR_OGRE_NEXT_COMBINED_RESOURCE_ROOT
    "${RUNTIME_OUTPUT_DIRECTORY}/resources/ogrenext"
    CACHE INTERNAL "Authenticated executable-relative resources for RoR-Combined" FORCE)
set(ROR_OGRE_NEXT_COMBINED_SHADER_MEDIA_ROOT
    "${ROR_OGRE_NEXT_COMBINED_RESOURCE_ROOT}/ShaderMedia"
    CACHE INTERNAL "Authenticated OgreNext shader media root" FORCE)
set(ROR_OGRE_NEXT_COMBINED_PRESENTATION_MEDIA_ROOT
    "${ROR_OGRE_NEXT_COMBINED_RESOURCE_ROOT}/Presentation"
    CACHE INTERNAL "Authenticated OgreNext presentation media root" FORCE)
foreach (_ror_exported_media_path IN ITEMS
        ROR_OGRE_NEXT_COMBINED_RESOURCE_ROOT
        ROR_OGRE_NEXT_COMBINED_SHADER_MEDIA_ROOT
        ROR_OGRE_NEXT_COMBINED_PRESENTATION_MEDIA_ROOT)
    if (NOT IS_ABSOLUTE "${${_ror_exported_media_path}}" OR
            "${${_ror_exported_media_path}}" MATCHES "[;\\\"]")
        message(FATAL_ERROR
            "Combined media path is not immutable/representable: ${_ror_exported_media_path}")
    endif ()
endforeach ()

list(REMOVE_DUPLICATES _ror_stage_tuples)
list(SORT _ror_stage_tuples)
list(REMOVE_DUPLICATES _ror_stage_sources)
set(_ror_resource_manifest_json
    "{\n  \"schema\": \"ror.ogre_next_combined_resource_manifest.v1\",\n")
string(APPEND _ror_resource_manifest_json
    "  \"ror_commit\": \"${ROR_SOURCE_COMMIT}\",\n"
    "  \"ror_source_manifest_sha256\": \"${ROR_SOURCE_MANIFEST_SHA256}\",\n"
    "  \"ogre_next_commit\": \"${ROR_OGRE_NEXT_COMMIT}\",\n"
    "  \"ogre_next_archive_sha256\": \"${ROR_OGRE_NEXT_ARCHIVE_SHA256}\",\n"
    "  \"files\": [\n")
list(LENGTH _ror_stage_tuples _ror_stage_tuple_count)
set(_ror_stage_tuple_index 0)
foreach (_ror_stage_tuple IN LISTS _ror_stage_tuples)
    string(REPLACE "|" ";" _ror_stage_fields "${_ror_stage_tuple}")
    list(LENGTH _ror_stage_fields _ror_stage_field_count)
    if (NOT _ror_stage_field_count EQUAL 4)
        message(FATAL_ERROR "Combined resource tuple shape changed")
    endif ()
    list(GET _ror_stage_fields 0 _ror_stage_sort_key)
    if (NOT _ror_stage_sort_key MATCHES
            "^[A-Za-z0-9_./+@()~-]+!$")
        message(FATAL_ERROR "Combined resource sort key changed shape")
    endif ()
    string(REGEX REPLACE "!$" "" _ror_stage_path
        "${_ror_stage_sort_key}")
    list(GET _ror_stage_fields 1 _ror_stage_source)
    list(GET _ror_stage_fields 2 _ror_stage_size)
    list(GET _ror_stage_fields 3 _ror_stage_sha256)
    math(EXPR _ror_stage_tuple_index "${_ror_stage_tuple_index} + 1")
    if (_ror_stage_tuple_index EQUAL _ror_stage_tuple_count)
        set(_ror_stage_comma "")
    else ()
        set(_ror_stage_comma ",")
    endif ()
    string(APPEND _ror_resource_manifest_json
        "    {\"path\": \"${_ror_stage_path}\", "
        "\"source\": \"${_ror_stage_source}\", "
        "\"size\": ${_ror_stage_size}, "
        "\"sha256\": \"${_ror_stage_sha256}\"}${_ror_stage_comma}\n")
endforeach ()
string(APPEND _ror_resource_manifest_json "  ]\n}\n")
set(ROR_OGRE_NEXT_COMBINED_RESOURCE_MANIFEST
    "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-combined-resource-manifest.json")
file(WRITE "${ROR_OGRE_NEXT_COMBINED_RESOURCE_MANIFEST}"
    "${_ror_resource_manifest_json}")
set(_ror_combined_resource_completion
    "${ROR_OGRE_NEXT_COMBINED_RESOURCE_ROOT}/.complete.json")
add_custom_command(
    OUTPUT "${_ror_combined_resource_completion}"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/stage_ogre_next_combined_resources.py"
        --manifest "${ROR_OGRE_NEXT_COMBINED_RESOURCE_MANIFEST}"
        --build-root "${CMAKE_BINARY_DIR}"
        --output "${ROR_OGRE_NEXT_COMBINED_RESOURCE_ROOT}"
    DEPENDS
        "${ROR_OGRE_NEXT_COMBINED_RESOURCE_MANIFEST}"
        "${CMAKE_SOURCE_DIR}/tools/stage_ogre_next_combined_resources.py"
        ${_ror_stage_sources}
    COMMENT "Staging authenticated OgreNext media for RoR-Combined"
    VERBATIM)
add_custom_target(ror_ogre_next_combined_resources ALL
    DEPENDS "${_ror_combined_resource_completion}")

function(_ror_combined_strict_target _ror_target)
    if (MSVC)
        target_compile_options(${_ror_target} PRIVATE /fp:strict)
    else ()
        target_compile_options(${_ror_target}
            PRIVATE -fno-fast-math -ffp-contract=off)
    endif ()
    set_target_properties(${_ror_target}
        PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES)
endfunction()

add_library(ror_render_payload_digest STATIC
    "${_ror_render_root}/RenderPayloadDigest.cpp")
target_include_directories(ror_render_payload_digest
    PUBLIC "${_ror_render_root}")
_ror_combined_strict_target(ror_render_payload_digest)

set(_ror_embedded_direct_contract_sources
    "${_ror_render_root}/GraphicsSceneSnapshotProducer.cpp"
    "${_ror_render_root}/HdrReference.cpp"
    "${_ror_render_root}/MaterialDescriptor.cpp"
    "${_ror_render_root}/NativeRenderAssetPackage.cpp"
    "${_ror_render_root}/NativeVisualShowcaseSceneSource.cpp"
    "${_ror_render_root}/Ogre14ParticleCaptureSource.cpp"
    "${_ror_render_root}/ReflectionProbeCaptureReceipt.cpp"
    "${_ror_render_root}/ReflectionProbeRuntime.cpp"
    "${_ror_render_root}/RenderAssetId.cpp"
    "${_ror_render_root}/RenderAssetRegistry.cpp"
    "${_ror_render_root}/RenderFrame.cpp"
    "${_ror_render_root}/RendererFrontend.cpp"
    "${_ror_render_root}/RendererFrontendDirectDispatcher.cpp"
    "${_ror_render_root}/RendererFrontendPresentationPolicy.cpp"
    "${_ror_render_root}/RenderResourceDescriptors.cpp"
    "${_ror_render_root}/ResourceHandle.cpp"
    "${_ror_render_root}/SceneSnapshot.cpp"
    "${_ror_system_root}/RendererInProcessSession.cpp")
set(_ror_combined_owned_sources
    "${_ror_render_root}/RenderPayloadDigest.cpp"
    ${_ror_embedded_direct_contract_sources}
    "${_ror_system_root}/RendererGameInputTarget.cpp")
foreach (_ror_direct_source IN LISTS _ror_combined_owned_sources)
    get_filename_component(_ror_direct_name "${_ror_direct_source}" NAME)
    if (_ror_direct_name MATCHES "(Bridge|Transport|Envelope)" OR
            _ror_direct_source MATCHES "/detail/")
        message(FATAL_ERROR
            "Combined direct runtime imported forbidden source: ${_ror_direct_name}")
    endif ()
endforeach ()
set(ROR_OGRE_NEXT_COMBINED_OWNED_SOURCES
    "${_ror_combined_owned_sources}"
    CACHE INTERNAL
    "Sources owned exactly once by RoR::OgreNextEmbeddedRuntime" FORCE)

add_library(ror_ogre_next_embedded_direct_contract STATIC
    ${_ror_embedded_direct_contract_sources})
target_include_directories(ror_ogre_next_embedded_direct_contract
    PUBLIC
        "${_ror_render_root}/.."
        "${_ror_render_root}"
        "${_ror_system_root}")
target_link_libraries(ror_ogre_next_embedded_direct_contract
    PUBLIC ror_render_payload_digest)
_ror_combined_strict_target(ror_ogre_next_embedded_direct_contract)

add_custom_target(
    ror_ogre_next_embedded_pssm_source_closure_verify
    COMMAND
        "${Python3_EXECUTABLE}"
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/verify_pssm_shadow_source_closure.py"
        --lock
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ogre-next-pssm-shadow-v1.lock.json"
        --canonical-lock
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ogre-next.lock.json"
        --source-root "${ogre_next_SOURCE_DIR}"
        --platform-policy "${ROR_OGRE_NEXT_PLATFORM_POLICY}"
    DEPENDS
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/verify_pssm_shadow_source_closure.py"
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ogre-next-pssm-shadow-v1.lock.json"
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ogre-next.lock.json"
    COMMENT "Verifying embedded OgreNext directional-shadow source closure"
    VERBATIM)

set(_ror_embedded_n1_sources
    "${_ror_render_root}/ogrenext/NativeDirectionalShadowContract.cpp"
    "${_ror_render_root}/ogrenext/NativeSunVisibilityV2Contract.cpp"
    "${_ror_render_root}/ogrenext/OgreNextSunVisibilityV2Interop.cpp"
    "${_ror_render_root}/ogrenext/OgreNextSunVisibilityV2InteropState.cpp"
    "${_ror_render_root}/ogrenext/OgreNextHdrTemporalContract.cpp"
    "${_ror_render_root}/ogrenext/OgreNextTaaContract.cpp"
    "${_ror_render_root}/ogrenext/OgreNextDisplayDomainUnlit.cpp"
    "${_ror_render_root}/ogrenext/OgreNextUvAffinePbs.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN1Frontend.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN1MediaIntegrity.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN1ParticleRuntime.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN1Policy.cpp"
    "${_ror_render_root}/ogrenext/OgreNextReflectionProbeRuntime.cpp"
    "${_ror_render_root}/ogrenext/OgreNextPssmShadowPolicy.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN2InteropState.cpp")
set(_ror_ogre_next_renderer_include_dirs
    "${ogre_next_SOURCE_DIR}/RenderSystems")
if (APPLE)
    list(APPEND _ror_embedded_n1_sources
        "${_ror_render_root}/ogrenext/OgreNextMetalInterop.mm"
        "${_ror_render_root}/ogrenext/OgreNextMetalFxUpscaler.mm"
        "${_ror_render_root}/ogrenext/OgreNextMetalRayTracingBackend.mm")
    list(APPEND _ror_ogre_next_renderer_include_dirs
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX")
    set(_ror_ogre_next_n1_definition ROR_OGRE_NEXT_N1_METAL=1)
elseif (WIN32)
    list(APPEND _ror_ogre_next_renderer_include_dirs
        "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/include")
    set(_ror_ogre_next_n1_definition ROR_OGRE_NEXT_N1_D3D11=1)
else ()
    list(APPEND _ror_ogre_next_renderer_include_dirs
        "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include")
    set(_ror_ogre_next_n1_definition ROR_OGRE_NEXT_N1_VULKAN=1)
endif ()
add_library(ror_ogre_next_embedded_n1_runtime STATIC
    ${_ror_embedded_n1_sources})
target_compile_definitions(ror_ogre_next_embedded_n1_runtime
    PRIVATE ${_ror_ogre_next_n1_definition})
if (APPLE)
    target_compile_options(ror_ogre_next_embedded_n1_runtime
        PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif ()
target_include_directories(ror_ogre_next_embedded_n1_runtime
    PUBLIC
        "${_ror_render_root}"
        "${_ror_render_root}/ogrenext"
    PRIVATE "${_ror_generated_root}")
target_include_directories(ror_ogre_next_embedded_n1_runtime
    SYSTEM PRIVATE
        "${ogre_next_SOURCE_DIR}/OgreMain/include"
        "${ogre_next_BINARY_DIR}/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Common/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Pbs/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Unlit/include"
        "${ogre_next_SOURCE_DIR}/Components/Overlay/include"
        ${_ror_ogre_next_renderer_include_dirs})
target_link_libraries(ror_ogre_next_embedded_n1_runtime
    PUBLIC ror_ogre_next_embedded_direct_contract
    PRIVATE
        OgreNextHlmsPbs
        OgreNextHlmsUnlit
        OgreNextOverlay
        ${ROR_OGRE_NEXT_RENDERER_TARGET}
        OgreNextMain
        Threads::Threads)
if (APPLE)
    # MetalFX arrived in macOS 13 while the deployment target is 11.0, so the
    # framework is weak-linked: on an older system its symbols resolve to nil
    # and the runtime @available guards keep the scaler unarmed, instead of
    # dyld refusing to load the binary at all.
    target_link_options(ror_ogre_next_embedded_n1_runtime
        INTERFACE "LINKER:-weak_framework,MetalFX")
endif ()
_ror_combined_strict_target(ror_ogre_next_embedded_n1_runtime)
if (APPLE)
    ror_ogre_next_enable_embedded_namespace(
        ror_ogre_next_embedded_n1_runtime LANGUAGES CXX OBJCXX)
else ()
    ror_ogre_next_enable_embedded_namespace(
        ror_ogre_next_embedded_n1_runtime LANGUAGES CXX)
endif ()
add_dependencies(ror_ogre_next_embedded_n1_runtime
    ror_ogre_next_embedded_pssm_source_closure_verify)

set(_ror_ogre_next_sdl_window_sources
    "${_ror_system_root}/RendererOgreNextWindowHost.cpp"
    "${_ror_system_root}/RendererOgreNextSdlWindowRuntime.cpp")
if (APPLE)
    list(APPEND _ror_ogre_next_sdl_window_sources
        "${_ror_system_root}/RendererOgreNextSdlWindowRuntimeCocoa.mm")
endif ()
add_library(ror_ogre_next_embedded_sdl_window_runtime STATIC
    ${_ror_ogre_next_sdl_window_sources})
if (APPLE)
    target_compile_options(ror_ogre_next_embedded_sdl_window_runtime
        PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif ()
target_include_directories(ror_ogre_next_embedded_sdl_window_runtime
    PUBLIC "${_ror_system_root}"
    SYSTEM PRIVATE
        "${ogre_next_SOURCE_DIR}/OgreMain/include"
        "${ogre_next_BINARY_DIR}/include"
        ${_ror_ogre_next_renderer_include_dirs})
target_link_libraries(ror_ogre_next_embedded_sdl_window_runtime
    PUBLIC SDL2::SDL2 Threads::Threads
    PRIVATE ${ROR_OGRE_NEXT_RENDERER_TARGET} OgreNextMain)
_ror_combined_strict_target(ror_ogre_next_embedded_sdl_window_runtime)
if (APPLE)
    ror_ogre_next_enable_embedded_namespace(
        ror_ogre_next_embedded_sdl_window_runtime LANGUAGES OBJCXX)
endif ()

add_library(ror_ogre_next_embedded_game_input_target STATIC
    "${_ror_system_root}/RendererGameInputTarget.cpp")
target_include_directories(ror_ogre_next_embedded_game_input_target
    PUBLIC "${_ror_system_root}")
_ror_combined_strict_target(ror_ogre_next_embedded_game_input_target)

add_library(ror_ogre_next_in_process_presenter STATIC
    "${_ror_system_root}/RendererOgreNextInProcessPresenter.cpp")
target_compile_definitions(ror_ogre_next_in_process_presenter
    PRIVATE ${_ror_ogre_next_n1_definition})
target_include_directories(ror_ogre_next_in_process_presenter
    PUBLIC
        "${_ror_render_root}/.."
        "${_ror_render_root}"
        "${_ror_system_root}"
    SYSTEM PRIVATE
        "${ogre_next_SOURCE_DIR}/OgreMain/include"
        "${ogre_next_BINARY_DIR}/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Common/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Pbs/include"
        "${ogre_next_SOURCE_DIR}/Components/Hlms/Unlit/include"
        "${ogre_next_SOURCE_DIR}/Components/Overlay/include"
        ${_ror_ogre_next_renderer_include_dirs})
target_link_libraries(ror_ogre_next_in_process_presenter
    PUBLIC
        ror_ogre_next_embedded_n1_runtime
        ror_ogre_next_embedded_direct_contract
        ror_ogre_next_embedded_game_input_target
        ror_ogre_next_embedded_sdl_window_runtime
        SDL2::SDL2
        Threads::Threads)
add_dependencies(ror_ogre_next_in_process_presenter
    ror_ogre_next_combined_resources)
_ror_combined_strict_target(ror_ogre_next_in_process_presenter)
ror_ogre_next_enable_embedded_namespace(
    ror_ogre_next_in_process_presenter LANGUAGES CXX)

add_library(ror_ogre_next_embedded_runtime INTERFACE)
target_link_libraries(ror_ogre_next_embedded_runtime INTERFACE
    ror_ogre_next_in_process_presenter)
add_library(RoR::OgreNextEmbeddedRuntime ALIAS
    ror_ogre_next_embedded_runtime)

add_library(ror_ogre_next_root_next_adapter OBJECT
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/next_adapter.cpp")
target_include_directories(ror_ogre_next_root_next_adapter PRIVATE
    "${ogre_next_SOURCE_DIR}/OgreMain/include"
    "${ogre_next_BINARY_DIR}/include")
target_link_libraries(ror_ogre_next_root_next_adapter PRIVATE OgreNextMain)
ror_ogre_next_enable_embedded_namespace(
    ror_ogre_next_root_next_adapter LANGUAGES CXX)

add_library(ror_ogre_next_root_ogre14_adapter OBJECT
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/ogre14_adapter.cpp")
target_include_directories(ror_ogre_next_root_ogre14_adapter
    SYSTEM PRIVATE ${_ror_root_ogre14_include_dirs})
target_link_libraries(ror_ogre_next_root_ogre14_adapter
    PRIVATE ${_ror_root_ogre14_libraries})

add_executable(ror_ogre_next_root_provider_link_smoke
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/main.cpp"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/n1_session_adapter.cpp"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/presenter_link_adapter.cpp"
    $<TARGET_OBJECTS:ror_ogre_next_root_next_adapter>
    $<TARGET_OBJECTS:ror_ogre_next_root_ogre14_adapter>)
target_include_directories(ror_ogre_next_root_provider_link_smoke PRIVATE
    "${_ror_render_root}/.."
    "${_ror_render_root}"
    "${_ror_system_root}")
target_link_libraries(ror_ogre_next_root_provider_link_smoke PRIVATE
    RoR::OgreNextEmbeddedRuntime
    ${_ror_root_ogre14_libraries})
_ror_combined_strict_target(ror_ogre_next_root_provider_link_smoke)
set_target_properties(ror_ogre_next_root_provider_link_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(_ror_namespace_audit_executable_link_map_arguments "")
if (MSVC)
    set(_ror_ogre_next_root_provider_link_map
        "${CMAKE_CURRENT_BINARY_DIR}/ror-ogre-next-root-provider-link-smoke.map")
    target_link_options(ror_ogre_next_root_provider_link_smoke PRIVATE
        "/MAP:${_ror_ogre_next_root_provider_link_map}"
        "/INCLUDE:ror_embedded_ogre_next_root_address"
        "/INCLUDE:ror_ogre14_root_address"
        "/INCLUDE:ror_embedded_ogre_next_n1_session_lifecycle"
        "/INCLUDE:ror_embedded_ogre_next_presenter_lifecycle")
    set_property(TARGET ror_ogre_next_root_provider_link_smoke APPEND PROPERTY
        ADDITIONAL_CLEAN_FILES "${_ror_ogre_next_root_provider_link_map}")
    list(APPEND _ror_namespace_audit_executable_link_map_arguments
        --executable-link-map "${_ror_ogre_next_root_provider_link_map}")
endif ()

if (APPLE)
    set(_ror_ogre_next_plugin_export_source
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/metal_plugin_export_probe.mm")
    set(_ror_ogre_next_plugin_export_definition "")
    set(_ror_ogre_next_plugin_export_language OBJCXX)
    set(_ror_ogre_next_plugin_export_include_dirs
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/src"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX")
elseif (WIN32)
    set(_ror_ogre_next_plugin_export_source
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/plugin_export_probe.cpp")
    set(_ror_ogre_next_plugin_export_definition
        ROR_OGRE_NEXT_PLUGIN_EXPORT_D3D11=1
        OGRED3DENGINEDLL_EXPORTS=1)
    set(_ror_ogre_next_plugin_export_language CXX)
    set(_ror_ogre_next_plugin_export_include_dirs
        "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/src"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Direct3D11/include")
else ()
    set(_ror_ogre_next_plugin_export_source
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/plugin_export_probe.cpp")
    set(_ror_ogre_next_plugin_export_definition
        ROR_OGRE_NEXT_PLUGIN_EXPORT_VULKAN=1)
    set(_ror_ogre_next_plugin_export_language CXX)
    set(_ror_ogre_next_plugin_export_include_dirs
        "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/src"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/include")
endif ()
if (WIN32)
    add_library(ror_ogre_next_root_plugin_export_probe STATIC
        "${_ror_ogre_next_plugin_export_source}")
    set(_ror_ogre_next_plugin_export_audit_input
        "$<TARGET_FILE:ror_ogre_next_root_plugin_export_probe>")
else ()
    add_library(ror_ogre_next_root_plugin_export_probe OBJECT
        "${_ror_ogre_next_plugin_export_source}")
    set(_ror_ogre_next_plugin_export_audit_input
        "$<TARGET_OBJECTS:ror_ogre_next_root_plugin_export_probe>")
endif ()
target_include_directories(ror_ogre_next_root_plugin_export_probe PRIVATE
    ${_ror_ogre_next_plugin_export_include_dirs}
    "${ogre_next_SOURCE_DIR}/OgreMain/include"
    "${ogre_next_BINARY_DIR}/include")
if (_ror_ogre_next_plugin_export_definition)
    target_compile_definitions(ror_ogre_next_root_plugin_export_probe PRIVATE
        ${_ror_ogre_next_plugin_export_definition})
endif ()
target_link_libraries(ror_ogre_next_root_plugin_export_probe PRIVATE
    ${ROR_OGRE_NEXT_RENDERER_TARGET} OgreNextMain)
if (APPLE)
    target_compile_options(ror_ogre_next_root_plugin_export_probe PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endif ()
_ror_combined_strict_target(ror_ogre_next_root_plugin_export_probe)
ror_ogre_next_enable_embedded_namespace(
    ror_ogre_next_root_plugin_export_probe
    LANGUAGES ${_ror_ogre_next_plugin_export_language})

set(_ror_namespace_audit_build_contract
    "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-namespace-audit-build-contract.json")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/namespace-audit-build-contract.json.in"
    "${_ror_namespace_audit_build_contract}"
    @ONLY)
set(_ror_namespace_audit_legacy_arguments "")
foreach (_ror_root_ogre14_runtime IN LISTS
        ROR_ROOT_OGRE14_RUNTIME_LIBRARIES)
    list(APPEND _ror_namespace_audit_legacy_arguments
        --legacy-library "${_ror_root_ogre14_runtime}")
endforeach ()
foreach (_ror_root_ogre14_runtime_directory IN LISTS
        ROR_ROOT_OGRE14_RUNTIME_DIRECTORIES)
    list(APPEND _ror_namespace_audit_legacy_arguments
        --legacy-runtime-directory
        "${_ror_root_ogre14_runtime_directory}")
endforeach ()
set(_ror_namespace_audit_embedded_source_arguments "")
foreach (_ror_embedded_n1_source IN LISTS _ror_embedded_n1_sources)
    list(APPEND _ror_namespace_audit_embedded_source_arguments
        --embedded-source "${_ror_embedded_n1_source}")
endforeach ()
set(_ror_namespace_audit_direct_source_arguments "")
foreach (_ror_embedded_direct_source IN LISTS
        _ror_embedded_direct_contract_sources)
    list(APPEND _ror_namespace_audit_direct_source_arguments
        --direct-source "${_ror_embedded_direct_source}")
endforeach ()
set(_ror_namespace_audit_next_archive_targets
    OgreNextMain
    ${ROR_OGRE_NEXT_RENDERER_TARGET}
    OgreNextHlmsPbs
    OgreNextHlmsUnlit
    OgreNextOverlay
    ror_render_payload_digest
    ror_ogre_next_in_process_presenter
    ror_ogre_next_embedded_sdl_window_runtime
    ror_ogre_next_embedded_game_input_target)
set(_ror_namespace_audit_next_archive_arguments "")
foreach (_ror_namespace_audit_target IN LISTS
        _ror_namespace_audit_next_archive_targets)
    list(APPEND _ror_namespace_audit_next_archive_arguments
        --next-archive "$<TARGET_FILE:${_ror_namespace_audit_target}>")
endforeach ()
set(_ror_namespace_audit_namespaced_sources
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/next_adapter.cpp"
    "${_ror_ogre_next_plugin_export_source}"
    "${_ror_render_root}/ogrenext/OgreNextN1Frontend.cpp"
    "${_ror_render_root}/ogrenext/OgreNextDisplayDomainUnlit.cpp"
    "${_ror_render_root}/ogrenext/OgreNextReflectionProbeRuntime.cpp"
    "${_ror_system_root}/RendererOgreNextInProcessPresenter.cpp")
if (APPLE)
    list(APPEND _ror_namespace_audit_namespaced_sources
        "${_ror_render_root}/ogrenext/OgreNextMetalInterop.mm"
        "${_ror_render_root}/ogrenext/OgreNextMetalFxUpscaler.mm"
        "${_ror_system_root}/RendererOgreNextSdlWindowRuntimeCocoa.mm")
endif ()
set(_ror_namespace_audit_namespaced_source_arguments "")
foreach (_ror_namespace_audit_source IN LISTS
        _ror_namespace_audit_namespaced_sources)
    list(APPEND _ror_namespace_audit_namespaced_source_arguments
        --namespaced-source "${_ror_namespace_audit_source}")
endforeach ()
set(_ror_namespace_audit_dirty_source_arguments "")
if (ROR_OGRE_NEXT_ALLOW_DIRTY_DEVELOPMENT_BUILD)
    list(APPEND _ror_namespace_audit_dirty_source_arguments
        --allow-dirty-source)
    message(STATUS
        "Dirty combined-runtime development admission enabled; generated receipts are not qualification evidence")
endif ()
set(ROR_OGRE_NEXT_COMBINED_NAMESPACE_AUDIT_REPORT
    "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-combined-namespace-audit.json"
    CACHE INTERNAL "Full combined namespace audit report" FORCE)
set(ROR_OGRE_NEXT_COMBINED_BINARY_CLOSURE_RECEIPT
    "${CMAKE_BINARY_DIR}/source/main/RoR-Combined.binary-closure.json"
    CACHE INTERNAL "Stable RoR-Combined binary closure receipt" FORCE)
add_custom_target(ror_ogre_next_combined_binary_receipt_invalidate
    COMMAND
        "${CMAKE_COMMAND}" -E remove -f
        "${ROR_OGRE_NEXT_COMBINED_BINARY_CLOSURE_RECEIPT}"
    COMMENT "Invalidating any previous RoR-Combined binary proof"
    VERBATIM)
add_custom_target(ror_ogre_next_root_namespace_audit
    COMMAND
        "${Python3_EXECUTABLE}"
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/audit_embedded_namespace.py"
        --platform-policy "${ROR_OGRE_NEXT_PLATFORM_POLICY}"
        ${_ror_namespace_audit_next_archive_arguments}
        --embedded-runtime-archive
            "$<TARGET_FILE:ror_ogre_next_embedded_n1_runtime>"
        --direct-contract-archive
            "$<TARGET_FILE:ror_ogre_next_embedded_direct_contract>"
        --plugin-object
            "${_ror_ogre_next_plugin_export_audit_input}"
        --next-plugin-linkage static
        --legacy-main-library "${ROR_ROOT_OGRE14_MAIN_RUNTIME}"
        ${_ror_namespace_audit_legacy_arguments}
        --executable
            "$<TARGET_FILE:ror_ogre_next_root_provider_link_smoke>"
        ${_ror_namespace_audit_executable_link_map_arguments}
        --compile-commands "${CMAKE_BINARY_DIR}/compile_commands.json"
        --require-upstream-strict-fp
        --next-source-root "${ogre_next_SOURCE_DIR}"
        --source-root "${CMAKE_SOURCE_DIR}"
        --expected-source-commit "${ROR_SOURCE_COMMIT}"
        ${_ror_namespace_audit_dirty_source_arguments}
        --build-contract "${_ror_namespace_audit_build_contract}"
        --canonical-lock
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ogre-next.lock.json"
        --patch "${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH}"
        --next-adapter
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/next_adapter.cpp"
        --legacy-adapter
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/ogre14_adapter.cpp"
        --main-source
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/main.cpp"
        --session-adapter
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/n1_session_adapter.cpp"
        --presenter-adapter
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/presenter_link_adapter.cpp"
        --link-smoke-target-name
            ror_ogre_next_root_provider_link_smoke
        --embedded-target-name ror_ogre_next_embedded_n1_runtime
        ${_ror_namespace_audit_embedded_source_arguments}
        --direct-target-name ror_ogre_next_embedded_direct_contract
        ${_ror_namespace_audit_direct_source_arguments}
        --remap-header "${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP}"
        --legacy-include
            "${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
        ${_ror_namespace_audit_namespaced_source_arguments}
        --neutral-source
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/ogre14_adapter.cpp"
        --neutral-source
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/main.cpp"
        --neutral-source
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/n1_session_adapter.cpp"
        --neutral-source
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/presenter_link_adapter.cpp"
        --neutral-source
            "${_ror_render_root}/RenderPayloadDigest.cpp"
        --neutral-source
            "${_ror_system_root}/RendererOgreNextSdlWindowRuntime.cpp"
        --neutral-source
            "${_ror_system_root}/RendererOgreNextWindowHost.cpp"
        --neutral-source
            "${_ror_system_root}/RendererGameInputTarget.cpp"
        --isolated-consumer-source "${CMAKE_SOURCE_DIR}/source/main/main.cpp"
        --isolated-consumer-target-name RoR-Combined
        --stb-decoder-source
            "${_ror_render_root}/Ogre14SourceTextureDecoder.cpp"
        --stb-decoder-target-name RoR-Combined
        --report "${ROR_OGRE_NEXT_COMBINED_NAMESPACE_AUDIT_REPORT}"
    BYPRODUCTS "${ROR_OGRE_NEXT_COMBINED_NAMESPACE_AUDIT_REPORT}"
    DEPENDS
        ror_ogre_next_root_provider_link_smoke
        ror_ogre_next_root_plugin_export_probe
        ror_render_payload_digest
        ror_ogre_next_embedded_direct_contract
        ror_ogre_next_embedded_n1_runtime
        ror_ogre_next_embedded_sdl_window_runtime
        ror_ogre_next_embedded_game_input_target
        ror_ogre_next_in_process_presenter
        OgreNextMain
        ${ROR_OGRE_NEXT_RENDERER_TARGET}
        OgreNextHlmsPbs
        OgreNextHlmsUnlit
        OgreNextOverlay
        "${_ror_namespace_audit_build_contract}"
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/audit_embedded_namespace.py"
        "${_ror_render_root}/Ogre14SourceTextureDecoder.cpp"
        "${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH}"
        "${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP}"
        ${ROR_ROOT_OGRE14_RUNTIME_LIBRARIES}
    COMMENT "Auditing full root OGRE14/OgreNext namespace collision closure"
    COMMAND_EXPAND_LISTS
    VERBATIM)
add_dependencies(ror_ogre_next_root_namespace_audit
    ror_ogre_next_combined_binary_receipt_invalidate)

set(ROR_OGRE_NEXT_COMBINED_PROVIDER_CONTRACT
    "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-combined-provider-contract.json"
    CACHE INTERNAL "Root embedded OgreNext provider contract" FORCE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/provider-contract.json.in"
    "${ROR_OGRE_NEXT_COMBINED_PROVIDER_CONTRACT}"
    @ONLY)
add_custom_target(ror_ogre_next_embedded_provider_contract ALL
    DEPENDS
        ror_ogre_next_root_namespace_audit
        ror_ogre_next_root_provider_link_smoke
        ror_ogre_next_combined_resources
        "${ROR_SOURCE_MANIFEST}"
        "${ROR_OGRE_NEXT_COMBINED_PROVIDER_CONTRACT}")
