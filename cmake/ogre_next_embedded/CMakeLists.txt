if (NOT ROR_OGRE_NEXT_COMBINED_RUNTIME OR
        NOT ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    message(FATAL_ERROR
        "The embedded provider is private to the explicit root combined runtime")
endif ()
if (NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR
        NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" OR
        NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR
        NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
        NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR
        "The first embedded provider slice is Release macOS arm64 only")
endif ()
if (NOT TARGET SDL2::SDL2 OR NOT TARGET Threads::Threads OR
        NOT OGRE_LIBRARIES OR NOT OGRE_INCLUDE_DIRS)
    message(FATAL_ERROR
        "The embedded provider requires the complete root OGRE14/SDL/thread graph")
endif ()

set(_ror_root_ogre14_libraries "${OGRE_LIBRARIES}")
set(_ror_root_ogre14_include_dirs "${OGRE_INCLUDE_DIRS}")
set(ROR_ROOT_CONAN_LOCK
    "${CMAKE_SOURCE_DIR}/cmake/conan/locks/ror-ogre14-macos-arm64-release.lock")
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
if (NOT SDL2_VERSION_STRING STREQUAL "2.32.10" OR
        NOT sdl_PACKAGE_FOLDER_RELEASE OR
        NOT IS_ABSOLUTE "${sdl_PACKAGE_FOLDER_RELEASE}" OR
        NOT sdl_SDL2_SDL2_LIBRARY_TYPE_RELEASE STREQUAL "STATIC" OR
        NOT sdl_SDL2_SDL2_LIBS_RELEASE STREQUAL "SDL2" OR
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
get_target_property(ROR_ROOT_SDL_IMPORTED_ARTIFACT
    ${ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET} IMPORTED_LOCATION_RELEASE)
get_target_property(_ror_root_sdl_interface
    SDL2::SDL2 INTERFACE_LINK_LIBRARIES)
string(JOIN ";" _ror_root_sdl_interface_serialized
    ${_ror_root_sdl_interface})
string(FIND "${_ror_root_sdl_interface_serialized}"
    "${ROR_ROOT_SDL_IMPORTED_ARTIFACT_TARGET}"
    _ror_root_sdl_interface_target_index)
set(_ror_root_sdl_expected_artifact
    "${sdl_PACKAGE_FOLDER_RELEASE}/lib/libSDL2.a")
if (NOT "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}" STREQUAL
            "${_ror_root_sdl_expected_artifact}" OR
        _ror_root_sdl_interface_target_index EQUAL -1 OR
        NOT EXISTS "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}" OR
        IS_DIRECTORY "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}" OR
        IS_SYMLINK "${ROR_ROOT_SDL_IMPORTED_ARTIFACT}")
    message(FATAL_ERROR
        "SDL2::SDL2 does not own the exact locked package archive")
endif ()
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
set(ROR_ROOT_OGRE14_RUNTIME_DIRECTORIES
    "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib"
    "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib/OGRE")
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
        "${_ror_root_ogre14_runtime_dir}/*.dylib")
    foreach (_ror_root_ogre14_runtime IN LISTS
            _ror_root_ogre14_runtime_candidates)
        get_filename_component(_ror_root_ogre14_runtime_name
            "${_ror_root_ogre14_runtime}" NAME)
        if (IS_SYMLINK "${_ror_root_ogre14_runtime}")
            continue()
        endif ()
        if (NOT _ror_root_ogre14_runtime_name MATCHES
                "^(libOgre|Plugin_|Codec_|RenderSystem_).*[.]dylib$")
            message(FATAL_ERROR
                "Unexpected dylib in the root OGRE14 collision set: ${_ror_root_ogre14_runtime_name}")
        endif ()
        list(APPEND ROR_ROOT_OGRE14_RUNTIME_LIBRARIES
            "${_ror_root_ogre14_runtime}")
        if (_ror_root_ogre14_runtime_name MATCHES
                "^libOgreMain[.].*[.]dylib$")
            set(ROR_ROOT_OGRE14_MAIN_RUNTIME
                "${_ror_root_ogre14_runtime}")
        elseif (_ror_root_ogre14_runtime_name MATCHES
                "^libOgreBites[.].*[.]dylib$")
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
    CACHE INTERNAL "Exact OGRE14 host dylib for RoR-Combined" FORCE)
set(ROR_ROOT_OGRE14_SDL_PROVIDER_RUNTIME
    "${ROR_ROOT_OGRE14_SDL_PROVIDER_RUNTIME}"
    CACHE INTERNAL "Exact OGRE14 Bites/SDL provider dylib for RoR-Combined" FORCE)
set(ROR_OGRE_NEXT_STANDALONE_ROOT
    "${CMAKE_SOURCE_DIR}/tools/ogre_next_probe")
set(ROR_OGRE_NEXT_EMBEDDED_NAMESPACE ON)
set(ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER ON)
include("${ROR_OGRE_NEXT_STANDALONE_ROOT}/cmake/PinnedOgreNext.cmake")
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

if (NOT ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "macos-arm64-metal" OR
        NOT ROR_OGRE_NEXT_RENDERER_TARGET STREQUAL "RenderSystem_Metal" OR
        NOT ROR_OGRE_NEXT_PRESENTATION_SDL_TARGET STREQUAL "SDL2::SDL2")
    message(FATAL_ERROR "The embedded provider platform/SDL policy changed")
endif ()

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
    target_include_directories(${_ror_rapidjson_consumer}
        SYSTEM PRIVATE "${ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR}")
    get_target_property(_ror_rapidjson_private_usage
        ${_ror_rapidjson_consumer} INCLUDE_DIRECTORIES)
    set(_ror_rapidjson_private_usage_count 0)
    foreach (_ror_rapidjson_usage IN LISTS _ror_rapidjson_private_usage)
        if (_ror_rapidjson_usage STREQUAL
                ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR)
            math(EXPR _ror_rapidjson_private_usage_count
                "${_ror_rapidjson_private_usage_count} + 1")
        endif ()
    endforeach ()
    get_target_property(_ror_rapidjson_interface_usage
        ${_ror_rapidjson_consumer} INTERFACE_INCLUDE_DIRECTORIES)
    if (NOT _ror_rapidjson_private_usage_count EQUAL 1 OR
            ROR_OGRE_NEXT_RAPIDJSON_INCLUDE_DIR IN_LIST
                _ror_rapidjson_interface_usage)
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
    ".github/workflows/ogre-next-probe.yml"
    "cmake/DependenciesConfig.cmake"
    "cmake/VerifyStbImageSource.cmake"
    "cmake/conan/locks/ror-ogre14-macos-arm64-release.lock"
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
    "tools/verify_ogre_next_combined_binary_closure.py"
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
string(SHA256 _ror_source_manifest_sha256
    "${_ror_provider_manifest_serialized}")
set(ROR_SOURCE_MANIFEST_FILE_COUNT
    "${_ror_source_manifest_file_count}"
    CACHE INTERNAL "Exact combined provider source manifest entry count" FORCE)
set(ROR_SOURCE_MANIFEST_SHA256
    "${_ror_source_manifest_sha256}"
    CACHE INTERNAL "Exact combined provider source manifest digest" FORCE)
set(ROR_SOURCE_MANIFEST
    "${CMAKE_CURRENT_BINARY_DIR}/ogre-next-combined-provider-sources.txt"
    CACHE INTERNAL "Exact combined provider source manifest" FORCE)
file(WRITE "${ROR_SOURCE_MANIFEST}"
    "${_ror_provider_manifest_serialized}")

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
set(_ror_stage_tuples "")
set(_ror_stage_sources "")

file(GLOB_RECURSE _ror_n1_hlms_files
    LIST_DIRECTORIES FALSE
    RELATIVE "${_ror_n1_hlms_root}"
    "${_ror_n1_hlms_root}/*")
list(APPEND _ror_n1_hlms_files
    "${ROR_OGRE_NEXT_DISPLAY_DOMAIN_MEDIA_RELATIVE}")
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
list(REMOVE_DUPLICATES _ror_n1_hdr_media_files)
list(SORT _ror_n1_hdr_media_files)
if (NOT _ror_n1_hdr_media_files)
    message(FATAL_ERROR "The combined HDR media manifest is empty")
endif ()
set(ROR_OGRE_NEXT_N1_HDR_MEDIA_MANIFEST_ENTRIES "")
set(_ror_n1_hdr_media_manifest_serialized "")
foreach (_ror_hdr_relative IN LISTS _ror_n1_hdr_media_files)
    _ror_record_media_file(
        "${ROR_OGRE_NEXT_MEDIA_ROOT}/${_ror_hdr_relative}"
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
    "${CMAKE_BINARY_DIR}/ror-ogre-next-combined-resources"
    CACHE INTERNAL "Authenticated build-tree resources for RoR-Combined" FORCE)
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
    "${_ror_render_root}/ogrenext/OgreNextHdrTemporalContract.cpp"
    "${_ror_render_root}/ogrenext/OgreNextTaaContract.cpp"
    "${_ror_render_root}/ogrenext/OgreNextDisplayDomainUnlit.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN1Frontend.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN1MediaIntegrity.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN1ParticleRuntime.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN1Policy.cpp"
    "${_ror_render_root}/ogrenext/OgreNextReflectionProbeRuntime.cpp"
    "${_ror_render_root}/ogrenext/OgreNextPssmShadowPolicy.cpp"
    "${_ror_render_root}/ogrenext/OgreNextN2InteropState.cpp"
    "${_ror_render_root}/ogrenext/OgreNextMetalInterop.mm"
    "${_ror_render_root}/ogrenext/OgreNextMetalRayTracingBackend.mm")
add_library(ror_ogre_next_embedded_n1_runtime STATIC
    ${_ror_embedded_n1_sources})
target_compile_definitions(ror_ogre_next_embedded_n1_runtime
    PRIVATE ROR_OGRE_NEXT_N1_METAL=1)
target_compile_options(ror_ogre_next_embedded_n1_runtime
    PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
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
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX")
target_link_libraries(ror_ogre_next_embedded_n1_runtime
    PUBLIC ror_ogre_next_embedded_direct_contract
    PRIVATE
        OgreNextHlmsPbs
        OgreNextHlmsUnlit
        RenderSystem_Metal
        OgreNextMain
        Threads::Threads)
_ror_combined_strict_target(ror_ogre_next_embedded_n1_runtime)
ror_ogre_next_enable_embedded_namespace(
    ror_ogre_next_embedded_n1_runtime LANGUAGES CXX OBJCXX)
add_dependencies(ror_ogre_next_embedded_n1_runtime
    ror_ogre_next_embedded_pssm_source_closure_verify)

add_library(ror_ogre_next_embedded_sdl_window_runtime STATIC
    "${_ror_system_root}/RendererOgreNextWindowHost.cpp"
    "${_ror_system_root}/RendererOgreNextSdlWindowRuntime.cpp"
    "${_ror_system_root}/RendererOgreNextSdlWindowRuntimeCocoa.mm")
target_compile_options(ror_ogre_next_embedded_sdl_window_runtime
    PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_include_directories(ror_ogre_next_embedded_sdl_window_runtime
    PUBLIC "${_ror_system_root}"
    SYSTEM PRIVATE
        "${ogre_next_SOURCE_DIR}/OgreMain/include"
        "${ogre_next_BINARY_DIR}/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX")
target_link_libraries(ror_ogre_next_embedded_sdl_window_runtime
    PUBLIC SDL2::SDL2 Threads::Threads
    PRIVATE RenderSystem_Metal OgreNextMain)
_ror_combined_strict_target(ror_ogre_next_embedded_sdl_window_runtime)
ror_ogre_next_enable_embedded_namespace(
    ror_ogre_next_embedded_sdl_window_runtime LANGUAGES OBJCXX)

add_library(ror_ogre_next_embedded_game_input_target STATIC
    "${_ror_system_root}/RendererGameInputTarget.cpp")
target_include_directories(ror_ogre_next_embedded_game_input_target
    PUBLIC "${_ror_system_root}")
_ror_combined_strict_target(ror_ogre_next_embedded_game_input_target)

add_library(ror_ogre_next_combined_build_paths INTERFACE)
target_compile_definitions(ror_ogre_next_combined_build_paths INTERFACE
    "ROR_OGRE_NEXT_COMBINED_SHADER_MEDIA_ROOT=\"${ROR_OGRE_NEXT_COMBINED_SHADER_MEDIA_ROOT}\""
    "ROR_OGRE_NEXT_COMBINED_PRESENTATION_MEDIA_ROOT=\"${ROR_OGRE_NEXT_COMBINED_PRESENTATION_MEDIA_ROOT}\"")
add_dependencies(ror_ogre_next_combined_build_paths
    ror_ogre_next_combined_resources)

add_library(ror_ogre_next_in_process_presenter STATIC
    "${_ror_system_root}/RendererOgreNextInProcessPresenter.cpp")
target_compile_definitions(ror_ogre_next_in_process_presenter
    PRIVATE ROR_OGRE_NEXT_N1_METAL=1)
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
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
        "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX")
target_link_libraries(ror_ogre_next_in_process_presenter
    PUBLIC
        ror_ogre_next_embedded_n1_runtime
        ror_ogre_next_embedded_direct_contract
        ror_ogre_next_embedded_game_input_target
        ror_ogre_next_embedded_sdl_window_runtime
        ror_ogre_next_combined_build_paths
        SDL2::SDL2
        Threads::Threads)
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

add_library(ror_ogre_next_root_plugin_export_probe OBJECT
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/metal_plugin_export_probe.mm")
target_include_directories(ror_ogre_next_root_plugin_export_probe PRIVATE
    "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/src"
    "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include"
    "${ogre_next_SOURCE_DIR}/RenderSystems/Metal/include/Windowing/OSX"
    "${ogre_next_SOURCE_DIR}/OgreMain/include"
    "${ogre_next_BINARY_DIR}/include")
target_link_libraries(ror_ogre_next_root_plugin_export_probe PRIVATE
    RenderSystem_Metal OgreNextMain)
target_compile_options(ror_ogre_next_root_plugin_export_probe PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
_ror_combined_strict_target(ror_ogre_next_root_plugin_export_probe)
ror_ogre_next_enable_embedded_namespace(
    ror_ogre_next_root_plugin_export_probe LANGUAGES OBJCXX)

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
        --next-archive "$<TARGET_FILE:OgreNextMain>"
        --next-archive "$<TARGET_FILE:RenderSystem_Metal>"
        --next-archive "$<TARGET_FILE:OgreNextHlmsPbs>"
        --next-archive "$<TARGET_FILE:OgreNextHlmsUnlit>"
        --next-archive "$<TARGET_FILE:OgreNextOverlay>"
        --next-archive "$<TARGET_FILE:ror_render_payload_digest>"
        --next-archive
            "$<TARGET_FILE:ror_ogre_next_in_process_presenter>"
        --next-archive
            "$<TARGET_FILE:ror_ogre_next_embedded_sdl_window_runtime>"
        --next-archive
            "$<TARGET_FILE:ror_ogre_next_embedded_game_input_target>"
        --embedded-runtime-archive
            "$<TARGET_FILE:ror_ogre_next_embedded_n1_runtime>"
        --direct-contract-archive
            "$<TARGET_FILE:ror_ogre_next_embedded_direct_contract>"
        --plugin-object
            "$<TARGET_OBJECTS:ror_ogre_next_root_plugin_export_probe>"
        --legacy-main-library "${ROR_ROOT_OGRE14_MAIN_RUNTIME}"
        ${_ror_namespace_audit_legacy_arguments}
        --executable
            "$<TARGET_FILE:ror_ogre_next_root_provider_link_smoke>"
        --compile-commands "${CMAKE_BINARY_DIR}/compile_commands.json"
        --require-upstream-strict-fp
        --next-source-root "${ogre_next_SOURCE_DIR}"
        --source-root "${CMAKE_SOURCE_DIR}"
        --expected-source-commit "${ROR_SOURCE_COMMIT}"
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
        --namespaced-source
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/next_adapter.cpp"
        --namespaced-source
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/src/embedded_namespace/metal_plugin_export_probe.mm"
        --namespaced-source
            "${_ror_render_root}/ogrenext/OgreNextN1Frontend.cpp"
        --namespaced-source
            "${_ror_render_root}/ogrenext/OgreNextDisplayDomainUnlit.cpp"
        --namespaced-source
            "${_ror_render_root}/ogrenext/OgreNextReflectionProbeRuntime.cpp"
        --namespaced-source
            "${_ror_render_root}/ogrenext/OgreNextMetalInterop.mm"
        --namespaced-source
            "${_ror_system_root}/RendererOgreNextSdlWindowRuntimeCocoa.mm"
        --namespaced-source
            "${_ror_system_root}/RendererOgreNextInProcessPresenter.cpp"
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
        RenderSystem_Metal
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
