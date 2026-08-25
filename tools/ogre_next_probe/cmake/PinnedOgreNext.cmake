# Shared, fail-closed dependency, license, ABI, and platform policy for the
# pinned OGRE-Next targets. The default remains the isolated standalone probe;
# the root combined-runtime provider may opt in to the separately reviewed
# namespaced/co-resident path and reuse the root's exact SDL2 target.

if (NOT DEFINED ROR_OGRE_NEXT_STANDALONE_ROOT OR
        ROR_OGRE_NEXT_STANDALONE_ROOT STREQUAL "")
    message(FATAL_ERROR
        "ROR_OGRE_NEXT_STANDALONE_ROOT must identify the reviewed lock root")
endif ()
if (NOT DEFINED ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    set(ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER OFF)
endif ()
if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER AND
        NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE)
    message(FATAL_ERROR
        "The root OgreNext provider requires the embedded namespace fork")
endif ()
if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER AND
        NOT TARGET SDL2::SDL2)
    message(FATAL_ERROR
        "The root OgreNext provider requires the existing SDL2::SDL2 target")
endif ()
if (TARGET OgreMain AND NOT ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    message(FATAL_ERROR
        "Pinned OGRE-Next standalone targets cannot coexist with OGRE 1.x")
endif ()

include(FetchContent)
find_package(Git REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/FreeTypeArchivePolicy.cmake")

# Presentation is still non-admitted, but its SDL ABI/source must already be
# reproducible. Keep this independent from the product Conan graph so the
# standalone probe cannot silently accept a host SDL or a moving recipe.
set(ROR_SDL2_PRESENTATION_LOCK_PATH
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/renderer-presentation-sdl2.lock.json")
set(ROR_SDL2_PRESENTATION_LOCK_SHA256
    "965d992c2059b3ada9c1d2c03fd2fd4a4bb8aef15267d06146a9b9d21287ecc1")
file(SHA256 "${ROR_SDL2_PRESENTATION_LOCK_PATH}"
    _ror_sdl2_presentation_lock_sha256)
if (NOT _ror_sdl2_presentation_lock_sha256 STREQUAL
        ROR_SDL2_PRESENTATION_LOCK_SHA256)
    message(FATAL_ERROR "The reviewed SDL2 presentation source lock changed")
endif ()
file(READ "${ROR_SDL2_PRESENTATION_LOCK_PATH}"
    _ror_sdl2_presentation_lock_json)
string(JSON ROR_SDL2_PRESENTATION_SCHEMA GET
    "${_ror_sdl2_presentation_lock_json}" schema)
string(JSON ROR_SDL2_PRESENTATION_VERSION GET
    "${_ror_sdl2_presentation_lock_json}" version)
string(JSON ROR_SDL2_PRESENTATION_ARCHIVE_URL GET
    "${_ror_sdl2_presentation_lock_json}" archive_url)
string(JSON ROR_SDL2_PRESENTATION_ARCHIVE_SHA256 GET
    "${_ror_sdl2_presentation_lock_json}" archive_sha256)
string(JSON ROR_SDL2_PRESENTATION_LICENSE_SPDX GET
    "${_ror_sdl2_presentation_lock_json}" license spdx)
string(JSON ROR_SDL2_PRESENTATION_LICENSE_PATH GET
    "${_ror_sdl2_presentation_lock_json}" license path)
string(JSON ROR_SDL2_PRESENTATION_LICENSE_SHA256 GET
    "${_ror_sdl2_presentation_lock_json}" license sha256)
string(JSON ROR_SDL2_PRESENTATION_CONAN_REFERENCE GET
    "${_ror_sdl2_presentation_lock_json}" conan reference)
string(JSON ROR_SDL2_PRESENTATION_CONAN_RECIPE_REVISION GET
    "${_ror_sdl2_presentation_lock_json}" conan recipe_revision)
string(JSON ROR_SDL2_PRESENTATION_CMAKE_TARGET GET
    "${_ror_sdl2_presentation_lock_json}" conan cmake_target)
foreach (_ror_sdl2_scope_key IN ITEMS
        source_dependency_locked probe_linked live_window_smoke
        production_admitted packaged)
    string(JSON _ror_sdl2_scope_${_ror_sdl2_scope_key}_type TYPE
        "${_ror_sdl2_presentation_lock_json}" scope
        ${_ror_sdl2_scope_key})
    string(JSON _ror_sdl2_scope_${_ror_sdl2_scope_key} GET
        "${_ror_sdl2_presentation_lock_json}" scope
        ${_ror_sdl2_scope_key})
    if (NOT _ror_sdl2_scope_${_ror_sdl2_scope_key}_type STREQUAL "BOOLEAN")
        message(FATAL_ERROR "SDL2 presentation scope field has wrong type")
    endif ()
endforeach ()
if (NOT ROR_SDL2_PRESENTATION_SCHEMA STREQUAL
        "ror.renderer_presentation_sdl2_source.v1" OR
        NOT ROR_SDL2_PRESENTATION_VERSION STREQUAL "2.32.10" OR
        NOT ROR_SDL2_PRESENTATION_ARCHIVE_URL STREQUAL
            "https://www.libsdl.org/release/SDL2-2.32.10.tar.gz" OR
        NOT ROR_SDL2_PRESENTATION_ARCHIVE_SHA256 STREQUAL
            "5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165" OR
        NOT ROR_SDL2_PRESENTATION_LICENSE_SPDX STREQUAL "Zlib" OR
        NOT ROR_SDL2_PRESENTATION_LICENSE_PATH STREQUAL "LICENSE.txt" OR
        NOT ROR_SDL2_PRESENTATION_LICENSE_SHA256 STREQUAL
            "97f35b302b361680ec1e891e95d2d52097bb95abff361434916d99dc1305f127" OR
        NOT ROR_SDL2_PRESENTATION_CONAN_REFERENCE STREQUAL "sdl/2.32.10" OR
        NOT ROR_SDL2_PRESENTATION_CONAN_RECIPE_REVISION STREQUAL
            "19432981a8779c918a13682d4186fa3b" OR
        NOT ROR_SDL2_PRESENTATION_CMAKE_TARGET STREQUAL "SDL2::SDL2" OR
        NOT _ror_sdl2_scope_source_dependency_locked OR
        NOT _ror_sdl2_scope_probe_linked OR
        NOT _ror_sdl2_scope_live_window_smoke OR
        _ror_sdl2_scope_production_admitted OR
        _ror_sdl2_scope_packaged)
    message(FATAL_ERROR "The SDL2 presentation source contract changed")
endif ()
if (NOT ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    FetchContent_Declare(
        ror_sdl2
        URL "${ROR_SDL2_PRESENTATION_ARCHIVE_URL}"
        URL_HASH "SHA256=${ROR_SDL2_PRESENTATION_ARCHIVE_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP true)
endif ()

set(_ror_lock_path "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ogre-next.lock.json")
file(READ "${_ror_lock_path}" _ror_lock_json)
string(JSON ROR_OGRE_NEXT_LOCK_SCHEMA GET "${_ror_lock_json}" schema_version)
string(JSON ROR_OGRE_NEXT_REPOSITORY GET "${_ror_lock_json}" repository)
string(JSON ROR_OGRE_NEXT_BRANCH GET "${_ror_lock_json}" branch)
string(JSON ROR_OGRE_NEXT_COMMIT GET "${_ror_lock_json}" commit)
string(JSON ROR_OGRE_NEXT_ARCHIVE_URL GET "${_ror_lock_json}" archive_url)
string(JSON ROR_OGRE_NEXT_ARCHIVE_SHA256 GET "${_ror_lock_json}" archive_sha256)
string(JSON ROR_OGRE_NEXT_LICENSE_SPDX GET "${_ror_lock_json}" license spdx)
string(JSON ROR_OGRE_NEXT_LICENSE_PATH GET "${_ror_lock_json}" license path)
string(JSON ROR_OGRE_NEXT_LICENSE_SHA256 GET "${_ror_lock_json}" license sha256)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_ROOT GET "${_ror_lock_json}" shader_media root)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_LICENSE_EXPRESSION GET
    "${_ror_lock_json}" shader_media license_expression)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_LICENSE_REF GET
    "${_ror_lock_json}" shader_media third_party_notice license_ref)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_PATH GET
    "${_ror_lock_json}" shader_media third_party_notice source_path)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_SHA256 GET
    "${_ror_lock_json}" shader_media third_party_notice source_sha256)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH GET
    "${_ror_lock_json}" shader_media third_party_notice notice_path)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_SHA256 GET
    "${_ror_lock_json}" shader_media third_party_notice notice_sha256)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_UPSTREAM_SOURCE GET
    "${_ror_lock_json}" shader_media third_party_notice upstream_source)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_PAPER_REFERENCE GET
    "${_ror_lock_json}" shader_media third_party_notice paper_reference)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_REQUIRED GET
    "${_ror_lock_json}" shader_media third_party_notice source_and_binary_notice_required)
string(JSON ROR_OGRE_NEXT_SHADER_MEDIA_PAPER_REQUIRED GET
    "${_ror_lock_json}" shader_media third_party_notice paper_reference_required)
string(JSON ROR_OGRE_NEXT_REFLECTION_MEDIA_ROOT GET
    "${_ror_lock_json}" reflection_shader_media root)
string(JSON ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_EXPRESSION GET
    "${_ror_lock_json}" reflection_shader_media license_expression)
string(JSON ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_REF GET
    "${_ror_lock_json}" reflection_shader_media third_party_notice license_ref)
string(JSON ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_SOURCE_PATH GET
    "${_ror_lock_json}" reflection_shader_media third_party_notice source_path)
string(JSON ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_SOURCE_SHA256 GET
    "${_ror_lock_json}" reflection_shader_media third_party_notice source_sha256)
string(JSON ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_PACKAGE_PATH GET
    "${_ror_lock_json}" reflection_shader_media third_party_notice package_path)
string(JSON ROR_OGRE_NEXT_REFLECTION_MEDIA_NOTICE_REQUIRED GET
    "${_ror_lock_json}" reflection_shader_media third_party_notice source_and_binary_notice_required)
string(JSON ROR_OGRE_NEXT_PATCH_COUNT LENGTH "${_ror_lock_json}" patches)
string(JSON ROR_OGRE_NEXT_PATCH_PATH GET "${_ror_lock_json}" patches 0 path)
string(JSON ROR_OGRE_NEXT_PATCH_SHA256 GET "${_ror_lock_json}" patches 0 sha256)
string(JSON ROR_OGRE_NEXT_PATCH_REASON GET "${_ror_lock_json}" patches 0 reason)
string(JSON ROR_OGRE_NEXT_IBL_PATCH_PATH GET "${_ror_lock_json}" patches 1 path)
string(JSON ROR_OGRE_NEXT_IBL_PATCH_SHA256 GET "${_ror_lock_json}" patches 1 sha256)
string(JSON ROR_OGRE_NEXT_IBL_PATCH_REASON GET "${_ror_lock_json}" patches 1 reason)
string(JSON ROR_OGRE_NEXT_IBL_PATCH_SOURCE_PATH GET
    "${_ror_lock_json}" patches 1 source_path)
string(JSON ROR_OGRE_NEXT_IBL_PATCH_SOURCE_SHA256 GET
    "${_ror_lock_json}" patches 1 source_sha256)
string(JSON ROR_OGRE_NEXT_IBL_PATCHED_SHA256 GET
    "${_ror_lock_json}" patches 1 patched_sha256)
string(JSON ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_PATH GET
    "${_ror_lock_json}" patches 2 path)
string(JSON ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_SHA256 GET
    "${_ror_lock_json}" patches 2 sha256)
string(JSON ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_REASON GET
    "${_ror_lock_json}" patches 2 reason)
string(JSON ROR_OGRE_NEXT_METAL_ANISOTROPY_SOURCE_PATH GET
    "${_ror_lock_json}" patches 2 source_path)
string(JSON ROR_OGRE_NEXT_METAL_ANISOTROPY_SOURCE_SHA256 GET
    "${_ror_lock_json}" patches 2 source_sha256)
string(JSON ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCHED_SHA256 GET
    "${_ror_lock_json}" patches 2 patched_sha256)
string(JSON ROR_OGRE_NEXT_VULKAN_SKY_PATCH_PATH GET
    "${_ror_lock_json}" patches 3 path)
string(JSON ROR_OGRE_NEXT_VULKAN_SKY_PATCH_SHA256 GET
    "${_ror_lock_json}" patches 3 sha256)
string(JSON ROR_OGRE_NEXT_VULKAN_SKY_PATCH_REASON GET
    "${_ror_lock_json}" patches 3 reason)
string(JSON ROR_OGRE_NEXT_VULKAN_SKY_SOURCE_PATH GET
    "${_ror_lock_json}" patches 3 source_path)
string(JSON ROR_OGRE_NEXT_VULKAN_SKY_SOURCE_SHA256 GET
    "${_ror_lock_json}" patches 3 source_sha256)
string(JSON ROR_OGRE_NEXT_VULKAN_SKY_PATCHED_SHA256 GET
    "${_ror_lock_json}" patches 3 patched_sha256)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_PATH GET
    "${_ror_lock_json}" patches 4 path)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_SHA256 GET
    "${_ror_lock_json}" patches 4 sha256)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_REASON GET
    "${_ror_lock_json}" patches 4 reason)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATH GET
    "${_ror_lock_json}" patches 4 header_source_path)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_SOURCE_SHA256 GET
    "${_ror_lock_json}" patches 4 header_source_sha256)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATCHED_SHA256 GET
    "${_ror_lock_json}" patches 4 header_patched_sha256)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATH GET
    "${_ror_lock_json}" patches 4 implementation_source_path)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_SOURCE_SHA256 GET
    "${_ror_lock_json}" patches 4 implementation_source_sha256)
string(JSON ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATCHED_SHA256 GET
    "${_ror_lock_json}" patches 4 implementation_patched_sha256)
string(JSON ROR_OGRE_NEXT_BARRIER_PATCH_PATH GET
    "${_ror_lock_json}" patches 5 path)
string(JSON ROR_OGRE_NEXT_BARRIER_PATCH_SHA256 GET
    "${_ror_lock_json}" patches 5 sha256)
string(JSON ROR_OGRE_NEXT_BARRIER_PATCH_REASON GET
    "${_ror_lock_json}" patches 5 reason)
string(JSON ROR_OGRE_NEXT_BARRIER_HEADER_PATH GET
    "${_ror_lock_json}" patches 5 header_source_path)
string(JSON ROR_OGRE_NEXT_BARRIER_HEADER_SOURCE_SHA256 GET
    "${_ror_lock_json}" patches 5 header_source_sha256)
string(JSON ROR_OGRE_NEXT_BARRIER_HEADER_PATCHED_SHA256 GET
    "${_ror_lock_json}" patches 5 header_patched_sha256)
string(JSON ROR_OGRE_NEXT_BARRIER_IMPLEMENTATION_PATH GET
    "${_ror_lock_json}" patches 5 implementation_source_path)
string(JSON ROR_OGRE_NEXT_BARRIER_IMPLEMENTATION_SOURCE_SHA256 GET
    "${_ror_lock_json}" patches 5 implementation_source_sha256)
string(JSON ROR_OGRE_NEXT_BARRIER_IMPLEMENTATION_PATCHED_SHA256 GET
    "${_ror_lock_json}" patches 5 implementation_patched_sha256)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_NAME GET
    "${_ror_lock_json}" embedded_namespace namespace)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_CMAKE_OPTION GET
    "${_ror_lock_json}" embedded_namespace cmake_option)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_DEFAULT_TYPE TYPE
    "${_ror_lock_json}" embedded_namespace default_enabled)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_DEFAULT GET
    "${_ror_lock_json}" embedded_namespace default_enabled)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_PATH GET
    "${_ror_lock_json}" embedded_namespace patch path)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_SHA256 GET
    "${_ror_lock_json}" embedded_namespace patch sha256)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_REASON GET
    "${_ror_lock_json}" embedded_namespace patch reason)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP_PATH GET
    "${_ror_lock_json}" embedded_namespace remap_header path)
string(JSON ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP_SHA256 GET
    "${_ror_lock_json}" embedded_namespace remap_header sha256)
string(JSON ROR_RAPIDJSON_REPOSITORY GET "${_ror_lock_json}" dependencies rapidjson repository)
string(JSON ROR_RAPIDJSON_TAG GET "${_ror_lock_json}" dependencies rapidjson tag)
string(JSON ROR_RAPIDJSON_ARCHIVE_URL GET "${_ror_lock_json}" dependencies rapidjson archive_url)
string(JSON ROR_RAPIDJSON_ARCHIVE_SHA256 GET "${_ror_lock_json}" dependencies rapidjson archive_sha256)
string(JSON ROR_RAPIDJSON_LICENSE_SPDX GET "${_ror_lock_json}" dependencies rapidjson license_spdx)
string(JSON ROR_RAPIDJSON_COMPILED_HEADERS_SPDX GET "${_ror_lock_json}" dependencies rapidjson compiled_headers_spdx)
string(JSON ROR_RAPIDJSON_LICENSE_PATH GET "${_ror_lock_json}" dependencies rapidjson license_path)
string(JSON ROR_RAPIDJSON_LICENSE_SHA256 GET "${_ror_lock_json}" dependencies rapidjson license_sha256)
string(JSON ROR_FREETYPE_REPOSITORY GET "${_ror_lock_json}" dependencies freetype repository)
string(JSON ROR_FREETYPE_VERSION GET "${_ror_lock_json}" dependencies freetype version)
string(JSON ROR_FREETYPE_ARCHIVE_URL GET "${_ror_lock_json}" dependencies freetype archive_url)
string(JSON ROR_FREETYPE_ARCHIVE_FALLBACK_URL GET
    "${_ror_lock_json}" dependencies freetype archive_fallback_url)
string(JSON ROR_FREETYPE_ARCHIVE_SHA256 GET "${_ror_lock_json}" dependencies freetype archive_sha256)
string(JSON ROR_FREETYPE_LICENSE_EXPRESSION GET "${_ror_lock_json}" dependencies freetype license_expression)
string(JSON ROR_FREETYPE_SELECTED_LICENSE_SPDX GET "${_ror_lock_json}" dependencies freetype selected_license_spdx)
string(JSON ROR_FREETYPE_LICENSE_PATH GET "${_ror_lock_json}" dependencies freetype license_path)
string(JSON ROR_FREETYPE_LICENSE_SHA256 GET "${_ror_lock_json}" dependencies freetype license_sha256)
string(JSON ROR_FREETYPE_PACKAGE_LICENSE_PATH GET "${_ror_lock_json}" dependencies freetype package_license_path)
string(JSON ROR_FREETYPE_OVERVIEW_PATH GET "${_ror_lock_json}" dependencies freetype overview_path)
string(JSON ROR_FREETYPE_OVERVIEW_SHA256 GET "${_ror_lock_json}" dependencies freetype overview_sha256)
string(JSON ROR_FREETYPE_PACKAGE_OVERVIEW_PATH GET "${_ror_lock_json}" dependencies freetype package_overview_path)
string(JSON ROR_FREETYPE_STATIC_LINK_TYPE TYPE "${_ror_lock_json}" dependencies freetype static_link)
string(JSON ROR_FREETYPE_STATIC_LINK GET "${_ror_lock_json}" dependencies freetype static_link)
string(JSON ROR_FREETYPE_DISABLED_DEPENDENCIES_TYPE TYPE
    "${_ror_lock_json}" dependencies freetype disabled_optional_dependencies)
string(JSON ROR_FREETYPE_DISABLED_DEPENDENCY_COUNT LENGTH
    "${_ror_lock_json}" dependencies freetype disabled_optional_dependencies)
set(ROR_FREETYPE_DISABLED_DEPENDENCIES "")
if (ROR_FREETYPE_DISABLED_DEPENDENCY_COUNT GREATER 0)
    math(EXPR _ror_freetype_disabled_dependency_last
        "${ROR_FREETYPE_DISABLED_DEPENDENCY_COUNT} - 1")
    foreach (_ror_freetype_dependency_index RANGE 0
            ${_ror_freetype_disabled_dependency_last})
        string(JSON _ror_freetype_disabled_dependency GET
            "${_ror_lock_json}" dependencies freetype
            disabled_optional_dependencies
            ${_ror_freetype_dependency_index})
        list(APPEND ROR_FREETYPE_DISABLED_DEPENDENCIES
            "${_ror_freetype_disabled_dependency}")
    endforeach ()
endif ()

# RT4's first normal-map slice is coupled to exact upstream shader,
# datablock, and pixel-format owners. The whole-file digest makes duplicate
# keys or schema edits fail before CMake's JSON accessor can normalize them;
# the extracted-source loop below independently verifies every owner hash.
set(ROR_OGRE_NEXT_NORMAL_MAP_SOURCE_LOCK_PATH
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/ogre-next-normal-map-source.lock.json")
set(ROR_OGRE_NEXT_NORMAL_MAP_SOURCE_LOCK_SHA256
    "7d180c54c54e7cc26b0081753c621b7164551d2b631c1127f818fbb22645f682")
file(SHA256 "${ROR_OGRE_NEXT_NORMAL_MAP_SOURCE_LOCK_PATH}"
    _ror_normal_map_source_lock_sha256)
if (NOT _ror_normal_map_source_lock_sha256 STREQUAL
        ROR_OGRE_NEXT_NORMAL_MAP_SOURCE_LOCK_SHA256)
    message(FATAL_ERROR "The reviewed normal-map source lock changed")
endif ()
file(READ "${ROR_OGRE_NEXT_NORMAL_MAP_SOURCE_LOCK_PATH}"
    _ror_normal_map_source_lock_json)
string(JSON ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SCHEMA TYPE
    "${_ror_normal_map_source_lock_json}" schema)
string(JSON ROR_OGRE_NEXT_NORMAL_MAP_LOCK_COMMIT_TYPE TYPE
    "${_ror_normal_map_source_lock_json}" ogre_next_commit)
string(JSON ROR_OGRE_NEXT_NORMAL_MAP_LOCK_CONTRACT_TYPE TYPE
    "${_ror_normal_map_source_lock_json}" contract)
string(JSON ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SOURCES_TYPE TYPE
    "${_ror_normal_map_source_lock_json}" sources)
string(JSON ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SCHEMA_VALUE GET
    "${_ror_normal_map_source_lock_json}" schema)
string(JSON ROR_OGRE_NEXT_NORMAL_MAP_LOCK_COMMIT GET
    "${_ror_normal_map_source_lock_json}" ogre_next_commit)
string(JSON ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SOURCE_COUNT LENGTH
    "${_ror_normal_map_source_lock_json}" sources)
if (NOT ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SCHEMA STREQUAL "STRING" OR
        NOT ROR_OGRE_NEXT_NORMAL_MAP_LOCK_COMMIT_TYPE STREQUAL "STRING" OR
        NOT ROR_OGRE_NEXT_NORMAL_MAP_LOCK_CONTRACT_TYPE STREQUAL "OBJECT" OR
        NOT ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SOURCES_TYPE STREQUAL "ARRAY" OR
        NOT ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SCHEMA_VALUE STREQUAL
        "ror.ogre_next_rt4_normal_map_source_lock.v1" OR
        NOT ROR_OGRE_NEXT_NORMAL_MAP_LOCK_COMMIT STREQUAL
        "37149a802de747f6806996fa3067b0748ecc1084" OR
        NOT ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SOURCE_COUNT EQUAL 23)
    message(FATAL_ERROR "The normal-map source lock schema changed")
endif ()
set(_ror_normal_map_source_paths "")
set(_ror_normal_map_source_roles "")
set(_ror_normal_map_source_hashes "")
math(EXPR _ror_normal_map_source_last
    "${ROR_OGRE_NEXT_NORMAL_MAP_LOCK_SOURCE_COUNT} - 1")
foreach (_ror_normal_map_source_index RANGE 0
        ${_ror_normal_map_source_last})
    foreach (_ror_normal_map_field IN ITEMS role path sha256)
        string(JSON _ror_normal_map_field_type TYPE
            "${_ror_normal_map_source_lock_json}" sources
            ${_ror_normal_map_source_index} ${_ror_normal_map_field})
        if (NOT _ror_normal_map_field_type STREQUAL "STRING")
            message(FATAL_ERROR
                "Normal-map source lock owner field has the wrong type")
        endif ()
    endforeach ()
    string(JSON _ror_normal_map_source_role GET
        "${_ror_normal_map_source_lock_json}" sources
        ${_ror_normal_map_source_index} role)
    string(JSON _ror_normal_map_source_path GET
        "${_ror_normal_map_source_lock_json}" sources
        ${_ror_normal_map_source_index} path)
    string(JSON _ror_normal_map_source_sha256 GET
        "${_ror_normal_map_source_lock_json}" sources
        ${_ror_normal_map_source_index} sha256)
    if (_ror_normal_map_source_role STREQUAL "" OR
            _ror_normal_map_source_path STREQUAL "" OR
            IS_ABSOLUTE "${_ror_normal_map_source_path}" OR
            _ror_normal_map_source_path MATCHES "(^|/)\\.\\.(/|$)" OR
            _ror_normal_map_source_path MATCHES "[;\\\\]" OR
            NOT _ror_normal_map_source_sha256 MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "Normal-map source lock owner is unsafe")
    endif ()
    string(LENGTH "${_ror_normal_map_source_sha256}"
        _ror_normal_map_source_sha256_length)
    if (NOT _ror_normal_map_source_sha256_length EQUAL 64)
        message(FATAL_ERROR "Normal-map source owner hash has the wrong length")
    endif ()
    list(FIND _ror_normal_map_source_roles "${_ror_normal_map_source_role}"
        _ror_duplicate_normal_map_role)
    list(FIND _ror_normal_map_source_paths "${_ror_normal_map_source_path}"
        _ror_duplicate_normal_map_path)
    if (NOT _ror_duplicate_normal_map_role EQUAL -1 OR
            NOT _ror_duplicate_normal_map_path EQUAL -1)
        message(FATAL_ERROR "Normal-map source lock has a duplicate owner")
    endif ()
    list(APPEND _ror_normal_map_source_roles
        "${_ror_normal_map_source_role}")
    list(APPEND _ror_normal_map_source_paths
        "${_ror_normal_map_source_path}")
    list(APPEND _ror_normal_map_source_hashes
        "${_ror_normal_map_source_sha256}")
endforeach ()

# Linux shader compilation is a separate source lock because its compiled
# archives are platform-specific while the OGRE/RapidJSON pin is shared by all
# three native policies. The whole-file digest makes every source, notice, and
# closure-target metadata change require an explicit integration review.
set(ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_PATH
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/linux-shader-toolchain.lock.json")
set(ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_SHA256
    "c2a5309582e2bc08267e517e7451f242689f1262005f22d0db218a081c81262a")
file(SHA256 "${ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_PATH}"
    _ror_linux_toolchain_lock_sha256)
if (NOT _ror_linux_toolchain_lock_sha256 STREQUAL
        ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_SHA256)
    message(FATAL_ERROR "The reviewed Linux shader source lock changed")
endif ()
file(READ "${ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_PATH}"
    _ror_linux_toolchain_lock_json)

# The Windows native-RT adaptation is independently locked so the shared
# OGRE archive/ABI lock and the unmodified Metal/Vulkan patch sets remain
# byte-for-byte stable.
set(ROR_OGRE_NEXT_WINDOWS_DXR7_LOCK_PATH
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/windows-dxr7.lock.json")
set(ROR_OGRE_NEXT_WINDOWS_DXR7_LOCK_SHA256
    "1f54ee0b94978ccefb46753fd9b943c91a126e16b6dd28c83c641759351d9820")
file(SHA256 "${ROR_OGRE_NEXT_WINDOWS_DXR7_LOCK_PATH}"
    _ror_windows_dxr7_lock_sha256)
if (NOT _ror_windows_dxr7_lock_sha256 STREQUAL
        ROR_OGRE_NEXT_WINDOWS_DXR7_LOCK_SHA256)
    message(FATAL_ERROR "The reviewed Windows DXR7 source lock changed")
endif ()
file(READ "${ROR_OGRE_NEXT_WINDOWS_DXR7_LOCK_PATH}"
    _ror_windows_dxr7_lock_json)
string(JSON ROR_WINDOWS_DXR7_LOCK_SCHEMA GET
    "${_ror_windows_dxr7_lock_json}" schema)
string(JSON ROR_WINDOWS_DXR7_PLATFORM_POLICY GET
    "${_ror_windows_dxr7_lock_json}" platform_policy)
string(JSON ROR_WINDOWS_DXR7_OGRE_COMMIT GET
    "${_ror_windows_dxr7_lock_json}" ogre_next_commit)
string(JSON ROR_WINDOWS_DXR7_PATCH_PATH GET
    "${_ror_windows_dxr7_lock_json}" adaptation_patch path)
string(JSON ROR_WINDOWS_DXR7_PATCH_SHA256 GET
    "${_ror_windows_dxr7_lock_json}" adaptation_patch sha256)
string(JSON ROR_WINDOWS_DXR7_SHADER_PATH GET
    "${_ror_windows_dxr7_lock_json}" shader path)
string(JSON ROR_WINDOWS_DXR7_SHADER_SHA256 GET
    "${_ror_windows_dxr7_lock_json}" shader sha256)
string(JSON ROR_WINDOWS_DXR7_SHADER_TARGET GET
    "${_ror_windows_dxr7_lock_json}" shader target)
string(JSON ROR_LINUX_SHADER_LOCK_SCHEMA GET
    "${_ror_linux_toolchain_lock_json}" schema)
string(JSON ROR_LINUX_SHADER_PLATFORM_POLICY GET
    "${_ror_linux_toolchain_lock_json}" platform_policy)
string(JSON ROR_LINUX_SHADER_PROVIDER GET
    "${_ror_linux_toolchain_lock_json}" provider)

foreach (_ror_component IN ITEMS SHADERC GLSLANG SPIRV_TOOLS SPIRV_HEADERS)
    if (_ror_component STREQUAL "SHADERC")
        set(_ror_component_path shaderc_release)
    elseif (_ror_component STREQUAL "GLSLANG")
        set(_ror_component_path dependencies glslang)
    elseif (_ror_component STREQUAL "SPIRV_TOOLS")
        set(_ror_component_path dependencies spirv_tools)
    else ()
        set(_ror_component_path dependencies spirv_headers)
    endif ()
    string(JSON ROR_LINUX_${_ror_component}_REPOSITORY GET
        "${_ror_linux_toolchain_lock_json}" ${_ror_component_path} repository)
    string(JSON ROR_LINUX_${_ror_component}_COMMIT GET
        "${_ror_linux_toolchain_lock_json}" ${_ror_component_path} commit)
    string(JSON ROR_LINUX_${_ror_component}_ARCHIVE_URL GET
        "${_ror_linux_toolchain_lock_json}" ${_ror_component_path} archive_url)
    string(JSON ROR_LINUX_${_ror_component}_ARCHIVE_SHA256 GET
        "${_ror_linux_toolchain_lock_json}" ${_ror_component_path} archive_sha256)
    string(JSON ROR_LINUX_${_ror_component}_LICENSE_PATH GET
        "${_ror_linux_toolchain_lock_json}" ${_ror_component_path} license_path)
    string(JSON ROR_LINUX_${_ror_component}_LICENSE_SHA256 GET
        "${_ror_linux_toolchain_lock_json}" ${_ror_component_path} license_sha256)
endforeach ()
string(JSON ROR_LINUX_SHADERC_DEPS_PATH GET
    "${_ror_linux_toolchain_lock_json}" shaderc_release dependency_manifest_path)
string(JSON ROR_LINUX_SHADERC_DEPS_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" shaderc_release dependency_manifest_sha256)
string(JSON ROR_LINUX_SHADERC_PATCH_PATH GET
    "${_ror_linux_toolchain_lock_json}" shaderc_release compatibility_patch path)
string(JSON ROR_LINUX_SHADERC_PATCH_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" shaderc_release compatibility_patch sha256)
string(JSON ROR_LINUX_GLSLANG_NOTICE_PATH GET
    "${_ror_linux_toolchain_lock_json}" dependencies glslang package_notice_path)
string(JSON ROR_LINUX_GLSLANG_NOTICE_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" dependencies glslang package_notice_sha256)
string(JSON ROR_LINUX_SPIRV_TOOLS_NOTICE_PATH GET
    "${_ror_linux_toolchain_lock_json}" dependencies spirv_tools package_notice_path)
string(JSON ROR_LINUX_SPIRV_TOOLS_NOTICE_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" dependencies spirv_tools package_notice_sha256)
string(JSON ROR_LINUX_SPIRV_HEADERS_NOTICE_PATH GET
    "${_ror_linux_toolchain_lock_json}" dependencies spirv_headers package_notice_path)
string(JSON ROR_LINUX_SPIRV_HEADERS_NOTICE_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" dependencies spirv_headers package_notice_sha256)
string(JSON ROR_LINUX_APACHE_NOTICE_PATH GET
    "${_ror_linux_toolchain_lock_json}" shaderc_release package_notice_path)
string(JSON ROR_LINUX_APACHE_NOTICE_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" shaderc_release package_notice_sha256)
string(JSON ROR_LINUX_OGRE_GLSLANG_PATCH_PATH GET
    "${_ror_linux_toolchain_lock_json}" ogre_compatibility_patch path)
string(JSON ROR_LINUX_OGRE_GLSLANG_PATCH_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" ogre_compatibility_patch sha256)
string(JSON ROR_LINUX_SPIRV_REFLECT_SOURCE_PATH GET
    "${_ror_linux_toolchain_lock_json}" ogre_embedded_components spirv_reflect source_path)
string(JSON ROR_LINUX_SPIRV_REFLECT_SOURCE_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" ogre_embedded_components spirv_reflect source_sha256)
string(JSON ROR_LINUX_SPIRV_REFLECT_HEADER_PATH GET
    "${_ror_linux_toolchain_lock_json}" ogre_embedded_components spirv_reflect header_path)
string(JSON ROR_LINUX_SPIRV_REFLECT_HEADER_SHA256 GET
    "${_ror_linux_toolchain_lock_json}" ogre_embedded_components spirv_reflect header_sha256)

if (NOT ROR_OGRE_NEXT_LOCK_SCHEMA EQUAL 6 OR
        NOT ROR_OGRE_NEXT_REPOSITORY STREQUAL
        "https://github.com/OGRECave/ogre-next" OR
        NOT ROR_OGRE_NEXT_BRANCH STREQUAL "v3-0" OR
        NOT ROR_OGRE_NEXT_COMMIT STREQUAL
        "37149a802de747f6806996fa3067b0748ecc1084")
    message(FATAL_ERROR "The OGRE-Next lock moved without an integration review")
endif ()
if (NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_NAME STREQUAL "RoROgreNext" OR
        NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_CMAKE_OPTION STREQUAL
            "ROR_OGRE_NEXT_EMBEDDED_NAMESPACE" OR
        NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_DEFAULT_TYPE STREQUAL
            "BOOLEAN" OR
        ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_DEFAULT OR
        NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_PATH STREQUAL
            "patches/0006-embedded-namespace-plugin-symbols.patch" OR
        NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_SHA256 STREQUAL
            "0df3dfdd1d97848eddf04d5fe64fcd2e70f65cb9059a5d8f1dd78ff63c5d8fec" OR
        NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP_PATH STREQUAL
            "embedded_namespace/RoROgreNextNamespaceRemap.h" OR
        NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP_SHA256 STREQUAL
            "bbb329c68e98a9a8e8c61783601d219d6f5ac2545fe8f4f346be0445b302d47d")
    message(FATAL_ERROR
        "The reviewed embedded OgreNext namespace contract changed")
endif ()
if (NOT ROR_WINDOWS_DXR7_LOCK_SCHEMA STREQUAL
        "ror.ogre_next_windows_dxr7_toolchain.v3" OR
        NOT ROR_WINDOWS_DXR7_PLATFORM_POLICY STREQUAL
        "windows-x64-d3d11on12-dxr" OR
        NOT ROR_WINDOWS_DXR7_OGRE_COMMIT STREQUAL
        ROR_OGRE_NEXT_COMMIT OR
        NOT ROR_WINDOWS_DXR7_SHADER_TARGET STREQUAL "lib_6_5")
    message(FATAL_ERROR "The Windows DXR7 toolchain contract changed")
endif ()
if (NOT ROR_LINUX_SHADER_LOCK_SCHEMA STREQUAL
        "ror.ogre_next_linux_shader_toolchain.v1" OR
        NOT ROR_LINUX_SHADER_PLATFORM_POLICY STREQUAL
        "linux-x86_64-vulkan" OR
        NOT ROR_LINUX_SHADER_PROVIDER STREQUAL "pinned-source")
    message(FATAL_ERROR "The Linux shader source policy changed")
endif ()
if (NOT ROR_OGRE_NEXT_SHADER_MEDIA_ROOT STREQUAL "Samples/Media/Hlms" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_LICENSE_EXPRESSION STREQUAL
        "MIT AND LicenseRef-Heitz-LTC-Paper-Notice" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_LICENSE_REF STREQUAL
        "LicenseRef-Heitz-LTC-Paper-Notice" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_PATH STREQUAL
        "Samples/Media/Hlms/Pbs/Any/AreaLights_LTC_piece_ps.any" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_SHA256 STREQUAL
        "44146bd7eee4bd6a3bb9428352e89dc20d7690b32c609e62c5f9330678f3a124" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH STREQUAL
        "licenses/LicenseRef-Heitz-LTC-Paper-Notice.txt" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_SHA256 STREQUAL
        "cc942875917be271c92fdc1fdec7a17da92b45dadf42a979b69583003f38bba6" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_UPSTREAM_SOURCE STREQUAL
        "https://github.com/selfshadow/ltc_code/" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_PAPER_REFERENCE STREQUAL
        "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines, ACM TOG 35(4), 2016" OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_REQUIRED OR
        NOT ROR_OGRE_NEXT_SHADER_MEDIA_PAPER_REQUIRED)
    message(FATAL_ERROR
        "The reviewed OGRE-Next shader-media license contract changed")
endif ()
if (NOT ROR_OGRE_NEXT_REFLECTION_MEDIA_ROOT STREQUAL
        "Samples/Media/Compute/Algorithms/IBL" OR
        NOT ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_EXPRESSION STREQUAL
        "LicenseRef-IBLBaker" OR
        NOT ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_REF STREQUAL
        "LicenseRef-IBLBaker" OR
        NOT ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_SOURCE_PATH STREQUAL
        "Docs/licenses/IBLBaker.txt" OR
        NOT ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_SOURCE_SHA256 STREQUAL
        "c66291524d9d111ed44349d4217dda31bdb33c6203a14b2d7682d805c9166a8e" OR
        NOT ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_PACKAGE_PATH STREQUAL
        "licenses/IBLBaker.txt" OR
        NOT ROR_OGRE_NEXT_REFLECTION_MEDIA_NOTICE_REQUIRED)
    message(FATAL_ERROR
        "The reviewed OGRE-Next reflection-media license contract changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_PATH}"
    _ror_shader_media_notice_sha256)
if (NOT _ror_shader_media_notice_sha256 STREQUAL
        ROR_OGRE_NEXT_SHADER_MEDIA_NOTICE_SHA256)
    message(FATAL_ERROR "The checked-in shader-media notice changed")
endif ()
if (NOT ROR_OGRE_NEXT_ARCHIVE_URL STREQUAL
        "https://github.com/OGRECave/ogre-next/archive/37149a802de747f6806996fa3067b0748ecc1084.tar.gz" OR
        NOT ROR_OGRE_NEXT_LICENSE_PATH STREQUAL "COPYING")
    message(FATAL_ERROR "The OGRE-Next archive or license path contract changed")
endif ()
if (NOT ROR_OGRE_NEXT_LICENSE_SPDX STREQUAL "MIT")
    message(FATAL_ERROR "The OGRE-Next license contract changed")
endif ()
if (NOT ROR_OGRE_NEXT_ARCHIVE_SHA256 STREQUAL
        "1c0be064474da512606d02543be2630b36cdf99f359a9f23edc97eeb410e25b2" OR
        NOT ROR_OGRE_NEXT_LICENSE_SHA256 STREQUAL
        "df6294031f26c4401ce713be0b0b3c5da27c2f1b7278a0d9833d111273174183")
    message(FATAL_ERROR "The reviewed OGRE-Next archive or license hash changed")
endif ()
if (NOT ROR_RAPIDJSON_REPOSITORY STREQUAL
        "https://github.com/Tencent/rapidjson" OR
        NOT ROR_RAPIDJSON_TAG STREQUAL "v1.1.0" OR
        NOT ROR_RAPIDJSON_ARCHIVE_URL STREQUAL
        "https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz" OR
        NOT ROR_RAPIDJSON_LICENSE_PATH STREQUAL "license.txt" OR
        NOT ROR_RAPIDJSON_ARCHIVE_SHA256 STREQUAL
        "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e" OR
        NOT ROR_RAPIDJSON_LICENSE_SPDX STREQUAL
        "MIT AND BSD-3-Clause AND JSON" OR
        NOT ROR_RAPIDJSON_COMPILED_HEADERS_SPDX STREQUAL "MIT" OR
        NOT ROR_RAPIDJSON_LICENSE_SHA256 STREQUAL
        "a140e5d46fe734a1c78f1a3c3ef207871dd75648be71fdda8e309b23ab8b1f32")
    message(FATAL_ERROR "The reviewed RapidJSON pin or license contract changed")
endif ()
if (NOT ROR_FREETYPE_REPOSITORY STREQUAL
        "https://gitlab.freedesktop.org/freetype/freetype" OR
        NOT ROR_FREETYPE_VERSION STREQUAL "2.14.3" OR
        NOT ROR_FREETYPE_ARCHIVE_URL STREQUAL
        "https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz" OR
        NOT ROR_FREETYPE_ARCHIVE_FALLBACK_URL STREQUAL
        "https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz" OR
        NOT ROR_FREETYPE_ARCHIVE_SHA256 STREQUAL
        "36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f" OR
        NOT ROR_FREETYPE_LICENSE_EXPRESSION STREQUAL
        "FTL OR GPL-2.0-or-later" OR
        NOT ROR_FREETYPE_SELECTED_LICENSE_SPDX STREQUAL
        "GPL-2.0-or-later" OR
        NOT ROR_FREETYPE_LICENSE_PATH STREQUAL "docs/GPLv2.TXT" OR
        NOT ROR_FREETYPE_LICENSE_SHA256 STREQUAL
        "c4120c6752c910c299e3bd9cb3a46ff262c268303ca2069b61f92f10a5656c18" OR
        NOT ROR_FREETYPE_PACKAGE_LICENSE_PATH STREQUAL
        "licenses/FreeType-GPLv2.txt" OR
        NOT ROR_FREETYPE_OVERVIEW_PATH STREQUAL "LICENSE.TXT" OR
        NOT ROR_FREETYPE_OVERVIEW_SHA256 STREQUAL
        "bd36c8b474855fa294c2ec5c184544478ef3720aad37d65a6296a4f264fd2d3b" OR
        NOT ROR_FREETYPE_PACKAGE_OVERVIEW_PATH STREQUAL
        "licenses/FreeType-LICENSE.txt" OR
        NOT ROR_FREETYPE_STATIC_LINK_TYPE STREQUAL "BOOLEAN" OR
        NOT ROR_FREETYPE_STATIC_LINK OR
        NOT ROR_FREETYPE_DISABLED_DEPENDENCIES_TYPE STREQUAL "ARRAY" OR
        NOT "${ROR_FREETYPE_DISABLED_DEPENDENCIES}" STREQUAL
        "BZip2;Brotli;HarfBuzz;PNG;ZLIB")
    message(FATAL_ERROR "The reviewed FreeType pin or license contract changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_PATCH_PATH}"
    _ror_patch_sha256)
if (NOT ROR_OGRE_NEXT_PATCH_SHA256 STREQUAL
        "84916d0d1abf61a15d19d2c89a7d9b1a445f1a37a5067a9f8b558395fe10ead1" OR
        NOT _ror_patch_sha256 STREQUAL ROR_OGRE_NEXT_PATCH_SHA256)
    message(FATAL_ERROR "The pinned OGRE-Next adaptation patch changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_IBL_PATCH_PATH}"
    _ror_ibl_patch_sha256)
if (NOT ROR_OGRE_NEXT_PATCH_COUNT EQUAL 6 OR
        NOT ROR_OGRE_NEXT_IBL_PATCH_PATH STREQUAL
        "patches/0005-metal-typed-ibl-uav-conversions.patch" OR
        NOT ROR_OGRE_NEXT_IBL_PATCH_SHA256 STREQUAL
        "82c91dbfc224579053817f3c88fade248037f7c7ce8e7b80916bf9a62b35384d" OR
        NOT _ror_ibl_patch_sha256 STREQUAL ROR_OGRE_NEXT_IBL_PATCH_SHA256 OR
        NOT ROR_OGRE_NEXT_IBL_PATCH_SOURCE_PATH STREQUAL
        "Samples/Media/Compute/Algorithms/IBL/SpecularIblIntegrator_piece_cs.any" OR
        NOT ROR_OGRE_NEXT_IBL_PATCH_SOURCE_SHA256 STREQUAL
        "68884256ab318116833bf2efe19518833459cc461fb8dd4f8e2c253f8c352165" OR
        NOT ROR_OGRE_NEXT_IBL_PATCHED_SHA256 STREQUAL
        "b33067159f8c358919bdb59d361a155333575f69081dcd53cf3da199966f9a6f")
    message(FATAL_ERROR "The pinned OGRE-Next IBL adaptation changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_PATH}"
    _ror_metal_anisotropy_patch_sha256)
if (NOT ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_PATH STREQUAL
        "patches/0008-metal-report-anisotropy-limit.patch" OR
        NOT ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_SHA256 STREQUAL
        "f7c5356f5f2025bbc7daf5e0788b7820244ed1ad8c3d45dd5ac73f381d800a22" OR
        NOT _ror_metal_anisotropy_patch_sha256 STREQUAL
        ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_SHA256 OR
        NOT ROR_OGRE_NEXT_METAL_ANISOTROPY_SOURCE_PATH STREQUAL
        "RenderSystems/Metal/src/OgreMetalRenderSystem.mm" OR
        NOT ROR_OGRE_NEXT_METAL_ANISOTROPY_SOURCE_SHA256 STREQUAL
        "bebe97dd2cb318d6aa2331eaaf0f8b181e18ac66660b02d4160802e0ed8ed0eb" OR
        NOT ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCHED_SHA256 STREQUAL
        "56bb59e7e8d7be5b9efe10e724e5385583618a12e2bb49482e0472d273dc1222")
    message(FATAL_ERROR
        "The pinned OGRE-Next Metal anisotropy capability adaptation changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_VULKAN_SKY_PATCH_PATH}"
    _ror_vulkan_sky_patch_sha256)
if (NOT ROR_OGRE_NEXT_VULKAN_SKY_PATCH_PATH STREQUAL
        "patches/0009-vulkan-use-sky-array-slice.patch" OR
        NOT ROR_OGRE_NEXT_VULKAN_SKY_PATCH_SHA256 STREQUAL
        "37d19c4c8fe808a17ff2d6d2eef2f575cab7e14d75e85f1865e274c9a5227a9e" OR
        NOT _ror_vulkan_sky_patch_sha256 STREQUAL
        ROR_OGRE_NEXT_VULKAN_SKY_PATCH_SHA256 OR
        NOT ROR_OGRE_NEXT_VULKAN_SKY_SOURCE_PATH STREQUAL
        "Samples/Media/2.0/scripts/materials/Common/GLSL/SkyEquirectangular_ps.glsl" OR
        NOT ROR_OGRE_NEXT_VULKAN_SKY_SOURCE_SHA256 STREQUAL
        "b749834d2dfdf0457cdcffbeffd3b2b4fb8ace7e9c5b6b61f026f9729c82ce0c" OR
        NOT ROR_OGRE_NEXT_VULKAN_SKY_PATCHED_SHA256 STREQUAL
        "793f66f9777a134970cf2b7dad44ee7da5204331cfe2e3db85544b3d8f8b8d62")
    message(FATAL_ERROR
        "The pinned OGRE-Next Vulkan sky array-slice adaptation changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_PATH}"
    _ror_texture_shutdown_patch_sha256)
if (NOT ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_PATH STREQUAL
        "patches/0010-texture-streaming-shutdown-atomic.patch" OR
        NOT ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_SHA256 STREQUAL
        "a3200b9038561ef1508a125eceb1b889bd95100905edc8d7017ec83e77f67b12" OR
        NOT _ror_texture_shutdown_patch_sha256 STREQUAL
        ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_SHA256 OR
        NOT ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATH STREQUAL
        "OgreMain/include/OgreTextureGpuManager.h" OR
        NOT ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_SOURCE_SHA256 STREQUAL
        "413e19db7aef3f32bcdf717c69277d1010d77b7ec432382aed4cecae2a9eb91a" OR
        NOT ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATCHED_SHA256 STREQUAL
        "de05f16c0ec931e42d46fdcd55557269f6b9ccf9b2be0b2c4c99baa0c098a100" OR
        NOT ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATH STREQUAL
        "OgreMain/src/OgreTextureGpuManager.cpp" OR
        NOT ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_SOURCE_SHA256 STREQUAL
        "9db903623cea3e61db10caace8eb8e16ca109cb0ca6f3503a42074f4e1c07226" OR
        NOT ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATCHED_SHA256 STREQUAL
        "e05b007104f5eb7877ffb2842fe0b0bca631585d948dfee501396afec994ce38")
    message(FATAL_ERROR
        "The pinned OGRE-Next texture shutdown synchronization changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_BARRIER_PATCH_PATH}"
    _ror_barrier_patch_sha256)
if (NOT ROR_OGRE_NEXT_BARRIER_PATCH_PATH STREQUAL
        "patches/0011-apple-reusable-pthread-barrier.patch" OR
        NOT ROR_OGRE_NEXT_BARRIER_PATCH_SHA256 STREQUAL
        "9785e7f3e77337f9e24368ed21cadd968d91d3de5848948bbe328078637dabd7" OR
        NOT _ror_barrier_patch_sha256 STREQUAL
        ROR_OGRE_NEXT_BARRIER_PATCH_SHA256 OR
        NOT ROR_OGRE_NEXT_BARRIER_HEADER_PATH STREQUAL
        "OgreMain/include/Threading/OgreBarrier.h" OR
        NOT ROR_OGRE_NEXT_BARRIER_HEADER_SOURCE_SHA256 STREQUAL
        "4c32f9ac41a886d3cffb4320c7a0ac4867b034647bd1e2a75cf2f260f0d40a3f" OR
        NOT ROR_OGRE_NEXT_BARRIER_HEADER_PATCHED_SHA256 STREQUAL
        "2270c0696dc12747e13baad8f44a983bbda37251cff3592489c3d582c562d567" OR
        NOT ROR_OGRE_NEXT_BARRIER_IMPLEMENTATION_PATH STREQUAL
        "OgreMain/src/Threading/OgreBarrierPThreads.cpp" OR
        NOT ROR_OGRE_NEXT_BARRIER_IMPLEMENTATION_SOURCE_SHA256 STREQUAL
        "0eb6e775ffc5c0e4647fb280f8855ea8dfab2dc275bf91d612fdaeec7cc9871f" OR
        NOT ROR_OGRE_NEXT_BARRIER_IMPLEMENTATION_PATCHED_SHA256 STREQUAL
        "edd2f66b8e831bf6ec918fb38efdab457ee83f1c2bf0ea095f8d482b5b476f4d")
    message(FATAL_ERROR
        "The pinned OGRE-Next reusable pthread barrier adaptation changed")
endif ()
set(ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_PATH}")
set(ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP_PATH}")
file(SHA256 "${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH}"
    _ror_embedded_namespace_patch_sha256)
file(SHA256 "${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP}"
    _ror_embedded_namespace_remap_sha256)
if (NOT _ror_embedded_namespace_patch_sha256 STREQUAL
        ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH_SHA256 OR
        NOT _ror_embedded_namespace_remap_sha256 STREQUAL
        ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP_SHA256)
    message(FATAL_ERROR
        "The reviewed embedded OgreNext namespace fork inputs changed")
endif ()
if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    set(ROR_OGRE_NEXT_ROOT_PROVIDER_INSTALL_PATCH
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/patches/0007-root-provider-install-dependencies-list.patch")
    if (NOT EXISTS "${ROR_OGRE_NEXT_ROOT_PROVIDER_INSTALL_PATCH}")
        message(FATAL_ERROR
            "The reviewed root-provider install compatibility patch is missing")
    endif ()
    file(SHA256 "${ROR_OGRE_NEXT_ROOT_PROVIDER_INSTALL_PATCH}"
        _ror_root_provider_install_patch_sha256)
    if (NOT _ror_root_provider_install_patch_sha256 STREQUAL
            "ba203f6b05f41472da700dbf48ca5eabc519c7fc1e04dd8adc53753e1265bd86")
        message(FATAL_ERROR
            "The reviewed root-provider install compatibility patch changed")
    endif ()
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_WINDOWS_DXR7_PATCH_PATH}"
    _ror_windows_dxr7_patch_sha256)
if (NOT _ror_windows_dxr7_patch_sha256 STREQUAL
        ROR_WINDOWS_DXR7_PATCH_SHA256)
    message(FATAL_ERROR "The pinned Windows D3D11 adoption patch changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_WINDOWS_DXR7_SHADER_PATH}"
    _ror_windows_dxr7_shader_sha256)
if (NOT _ror_windows_dxr7_shader_sha256 STREQUAL
        ROR_WINDOWS_DXR7_SHADER_SHA256)
    message(FATAL_ERROR "The pinned Windows DXR7 shader source changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_LINUX_OGRE_GLSLANG_PATCH_PATH}"
    _ror_linux_ogre_glslang_patch_sha256)
if (NOT _ror_linux_ogre_glslang_patch_sha256 STREQUAL
        ROR_LINUX_OGRE_GLSLANG_PATCH_SHA256)
    message(FATAL_ERROR "The pinned OGRE/glslang ABI patch changed")
endif ()
file(SHA256
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_LINUX_SHADERC_PATCH_PATH}"
    _ror_linux_shaderc_patch_sha256)
if (NOT _ror_linux_shaderc_patch_sha256 STREQUAL
        ROR_LINUX_SHADERC_PATCH_SHA256)
    message(FATAL_ERROR "The pinned shaderc CMake patch changed")
endif ()

set(ROR_OGRE_NEXT_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned OGRE-Next archive")
set(ROR_RAPIDJSON_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned RapidJSON archive")
set(ROR_FREETYPE_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned FreeType archive")
set(ROR_OGRE_NEXT_SHADERC_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned shaderc source archive")
set(ROR_OGRE_NEXT_GLSLANG_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned glslang source archive")
set(ROR_OGRE_NEXT_SPIRV_TOOLS_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned SPIRV-Tools source archive")
set(ROR_OGRE_NEXT_SPIRV_HEADERS_ARCHIVE "" CACHE FILEPATH
    "Optional local copy of the pinned SPIRV-Headers source archive")
if (DEFINED FETCHCONTENT_SOURCE_DIR_OGRE_NEXT AND
        NOT FETCHCONTENT_SOURCE_DIR_OGRE_NEXT STREQUAL "")
    message(FATAL_ERROR
        "FETCHCONTENT_SOURCE_DIR_OGRE_NEXT bypasses archive verification and is prohibited")
endif ()
if (DEFINED FETCHCONTENT_SOURCE_DIR_RAPIDJSON AND
        NOT FETCHCONTENT_SOURCE_DIR_RAPIDJSON STREQUAL "")
    message(FATAL_ERROR
        "FETCHCONTENT_SOURCE_DIR_RAPIDJSON bypasses archive verification and is prohibited")
endif ()
if (DEFINED FETCHCONTENT_SOURCE_DIR_ROR_FREETYPE AND
        NOT FETCHCONTENT_SOURCE_DIR_ROR_FREETYPE STREQUAL "")
    message(FATAL_ERROR
        "FETCHCONTENT_SOURCE_DIR_ROR_FREETYPE bypasses archive verification and is prohibited")
endif ()
if (NOT ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER AND
        DEFINED FETCHCONTENT_SOURCE_DIR_ROR_SDL2 AND
        NOT FETCHCONTENT_SOURCE_DIR_ROR_SDL2 STREQUAL "")
    message(FATAL_ERROR
        "FETCHCONTENT_SOURCE_DIR_ROR_SDL2 bypasses archive verification and is prohibited")
endif ()
foreach (_ror_content_name IN ITEMS
        SHADERC ROR_GLSLANG_SOURCE ROR_SPIRV_TOOLS_SOURCE
        ROR_SPIRV_HEADERS_SOURCE)
    if (DEFINED FETCHCONTENT_SOURCE_DIR_${_ror_content_name} AND
            NOT FETCHCONTENT_SOURCE_DIR_${_ror_content_name} STREQUAL "")
        message(FATAL_ERROR
            "FETCHCONTENT_SOURCE_DIR_${_ror_content_name} bypasses the "
            "Linux shader source lock and is prohibited")
    endif ()
endforeach ()

function(_ror_resolve_linux_source_archive
        _ror_cache_variable _ror_expected_sha256 _ror_archive_url
        _ror_description _ror_output)
    if (NOT "${${_ror_cache_variable}}" STREQUAL "")
        if (NOT EXISTS "${${_ror_cache_variable}}")
            message(FATAL_ERROR
                "Pinned ${_ror_description} archive does not exist: "
                "${${_ror_cache_variable}}")
        endif ()
        file(SHA256 "${${_ror_cache_variable}}" _ror_actual_sha256)
        if (NOT _ror_actual_sha256 STREQUAL _ror_expected_sha256)
            message(FATAL_ERROR
                "Pinned ${_ror_description} archive SHA-256 mismatch: "
                "expected ${_ror_expected_sha256}, got ${_ror_actual_sha256}")
        endif ()
        set(${_ror_output} "${${_ror_cache_variable}}" PARENT_SCOPE)
    else ()
        set(${_ror_output} "${_ror_archive_url}" PARENT_SCOPE)
    endif ()
endfunction()

_ror_resolve_linux_source_archive(
    ROR_OGRE_NEXT_SHADERC_ARCHIVE "${ROR_LINUX_SHADERC_ARCHIVE_SHA256}"
    "${ROR_LINUX_SHADERC_ARCHIVE_URL}" "shaderc" _ror_shaderc_url)
_ror_resolve_linux_source_archive(
    ROR_OGRE_NEXT_GLSLANG_ARCHIVE "${ROR_LINUX_GLSLANG_ARCHIVE_SHA256}"
    "${ROR_LINUX_GLSLANG_ARCHIVE_URL}" "glslang" _ror_glslang_url)
_ror_resolve_linux_source_archive(
    ROR_OGRE_NEXT_SPIRV_TOOLS_ARCHIVE
    "${ROR_LINUX_SPIRV_TOOLS_ARCHIVE_SHA256}"
    "${ROR_LINUX_SPIRV_TOOLS_ARCHIVE_URL}" "SPIRV-Tools"
    _ror_spirv_tools_url)
_ror_resolve_linux_source_archive(
    ROR_OGRE_NEXT_SPIRV_HEADERS_ARCHIVE
    "${ROR_LINUX_SPIRV_HEADERS_ARCHIVE_SHA256}"
    "${ROR_LINUX_SPIRV_HEADERS_ARCHIVE_URL}" "SPIRV-Headers"
    _ror_spirv_headers_url)

if (ROR_OGRE_NEXT_ARCHIVE)
    if (NOT EXISTS "${ROR_OGRE_NEXT_ARCHIVE}")
        message(FATAL_ERROR "Pinned archive does not exist: ${ROR_OGRE_NEXT_ARCHIVE}")
    endif ()
    file(SHA256 "${ROR_OGRE_NEXT_ARCHIVE}" _ror_local_archive_sha256)
    if (NOT _ror_local_archive_sha256 STREQUAL ROR_OGRE_NEXT_ARCHIVE_SHA256)
        message(FATAL_ERROR
            "Pinned archive SHA-256 mismatch: expected "
            "${ROR_OGRE_NEXT_ARCHIVE_SHA256}, got ${_ror_local_archive_sha256}")
    endif ()
    set(_ror_ogre_next_url "${ROR_OGRE_NEXT_ARCHIVE}")
else ()
    set(_ror_ogre_next_url "${ROR_OGRE_NEXT_ARCHIVE_URL}")
endif ()
if (ROR_RAPIDJSON_ARCHIVE)
    if (NOT EXISTS "${ROR_RAPIDJSON_ARCHIVE}")
        message(FATAL_ERROR "Pinned RapidJSON archive does not exist: ${ROR_RAPIDJSON_ARCHIVE}")
    endif ()
    file(SHA256 "${ROR_RAPIDJSON_ARCHIVE}" _ror_local_rapidjson_sha256)
    if (NOT _ror_local_rapidjson_sha256 STREQUAL ROR_RAPIDJSON_ARCHIVE_SHA256)
        message(FATAL_ERROR
            "Pinned RapidJSON SHA-256 mismatch: expected "
            "${ROR_RAPIDJSON_ARCHIVE_SHA256}, got ${_ror_local_rapidjson_sha256}")
    endif ()
    set(_ror_rapidjson_url "${ROR_RAPIDJSON_ARCHIVE}")
else ()
    set(_ror_rapidjson_url "${ROR_RAPIDJSON_ARCHIVE_URL}")
endif ()
ror_select_freetype_archive_urls(
    _ror_freetype_urls "${ROR_FREETYPE_ARCHIVE}"
    "${ROR_FREETYPE_ARCHIVE_SHA256}"
    "${ROR_FREETYPE_ARCHIVE_URL}"
    "${ROR_FREETYPE_ARCHIVE_FALLBACK_URL}")

if (APPLE)
    if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR
            "OGRE-Next probe policy supports only macOS arm64, got "
            "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
    if (CMAKE_OSX_ARCHITECTURES AND
            NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR
            "OGRE-Next macOS probe must be arm64-only, got "
            "${CMAKE_OSX_ARCHITECTURES}")
    endif ()
    set(ROR_OGRE_NEXT_PLATFORM_POLICY "macos-arm64-metal")
    set(ROR_OGRE_NEXT_RENDERER_TARGET "RenderSystem_Metal")
    set(ROR_OGRE_NEXT_RENDERER_NAME "Metal Rendering Subsystem")
    set(ROR_OGRE_NEXT_DEVICE_OPTION_NAME "Rendering Device")
    set(ROR_OGRE_NEXT_SHADER_SYNTAX "Metal")
    set(ROR_OGRE_NEXT_FRAME_SURFACE_MODE "macos-hidden-native")
    set(ROR_OGRE_NEXT_SIMD_NEON ON)
    set(ROR_OGRE_NEXT_SIMD_SSE2 OFF)
    set(ROR_OGRE_NEXT_SIMD_FAMILY "neon")
    set(_ror_probe_renderer_definition ROR_OGRE_NEXT_PROBE_METAL=1)
elseif (WIN32)
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
            NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        message(FATAL_ERROR
            "OGRE-Next probe policy supports only Windows x64, got "
            "${CMAKE_SYSTEM_PROCESSOR} (${CMAKE_SIZEOF_VOID_P}-byte pointers)")
    endif ()
    set(ROR_OGRE_NEXT_PLATFORM_POLICY "windows-x64-d3d11")
    set(ROR_OGRE_NEXT_RENDERER_TARGET "RenderSystem_Direct3D11")
    set(ROR_OGRE_NEXT_RENDERER_NAME "Direct3D11 Rendering Subsystem")
    set(ROR_OGRE_NEXT_DEVICE_OPTION_NAME "Rendering Device")
    set(ROR_OGRE_NEXT_SHADER_SYNTAX "HLSL")
    set(ROR_OGRE_NEXT_FRAME_SURFACE_MODE "windows-hidden-native")
    set(ROR_OGRE_NEXT_SIMD_NEON OFF)
    set(ROR_OGRE_NEXT_SIMD_SSE2 ON)
    set(ROR_OGRE_NEXT_SIMD_FAMILY "sse2")
    set(_ror_probe_renderer_definition ROR_OGRE_NEXT_PROBE_D3D11=1)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
            NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        message(FATAL_ERROR
            "OGRE-Next probe policy supports only Linux x86_64, got "
            "${CMAKE_SYSTEM_PROCESSOR} (${CMAKE_SIZEOF_VOID_P}-byte pointers)")
    endif ()
    set(ROR_OGRE_NEXT_PLATFORM_POLICY "linux-x86_64-vulkan")
    set(ROR_OGRE_NEXT_RENDERER_TARGET "RenderSystem_Vulkan")
    set(ROR_OGRE_NEXT_RENDERER_NAME "Vulkan Rendering Subsystem")
    set(ROR_OGRE_NEXT_DEVICE_OPTION_NAME "Device")
    set(ROR_OGRE_NEXT_SHADER_SYNTAX "GLSL")
    set(ROR_OGRE_NEXT_FRAME_SURFACE_MODE "linux-null-window-offscreen")
    set(ROR_OGRE_NEXT_SIMD_NEON OFF)
    set(ROR_OGRE_NEXT_SIMD_SSE2 ON)
    set(ROR_OGRE_NEXT_SIMD_FAMILY "sse2")
    set(_ror_probe_renderer_definition ROR_OGRE_NEXT_PROBE_VULKAN=1)
else ()
    message(FATAL_ERROR
        "No reviewed OGRE-Next probe policy for ${CMAKE_SYSTEM_NAME}/"
        "${CMAKE_SYSTEM_PROCESSOR}")
endif ()

if (ROR_OGRE_NEXT_SIMD_NEON)
    set(ROR_OGRE_NEXT_SIMD_NEON_JSON true)
else ()
    set(ROR_OGRE_NEXT_SIMD_NEON_JSON false)
endif ()
if (ROR_OGRE_NEXT_SIMD_SSE2)
    set(ROR_OGRE_NEXT_SIMD_SSE2_JSON true)
else ()
    set(ROR_OGRE_NEXT_SIMD_SSE2_JSON false)
endif ()

# ABI-relevant choices are forced because the probe's output is only meaningful
# when every platform compiles the same reviewed OGRE-Next contract.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OGRE_USE_NEW_PROJECT_NAME ON CACHE BOOL "" FORCE)
set(OGRE_STATIC ON CACHE BOOL "" FORCE)
set(OGRE_BUILD_LIBS_AS_FRAMEWORKS OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_DOUBLE OFF CACHE BOOL "" FORCE)
set(OGRE_DEBUG_LEVEL_DEBUG 3 CACHE STRING "" FORCE)
set(OGRE_DEBUG_LEVEL_RELEASE 0 CACHE STRING "" FORCE)
set(OGRE_EMBED_DEBUG_MODE auto CACHE STRING "" FORCE)
set(OGRE_ASSERT_MODE 0 CACHE STRING "" FORCE)
set(OGRE_CONFIG_ALLOCATOR 0 CACHE STRING "" FORCE)
set(OGRE_CONFIG_CONTAINERS_USE_CUSTOM_ALLOCATOR OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_STRING_USE_CUSTOM_ALLOCATOR OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_MEMTRACK_DEBUG OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_MEMTRACK_RELEASE OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_THREADS 0 CACHE STRING "" FORCE)
set(OGRE_CONFIG_THREAD_PROVIDER none CACHE STRING "" FORCE)
set(OGRE_IDSTRING_USE_128 OFF CACHE BOOL "" FORCE)
set(OGRE_IDSTRING_ALWAYS_READABLE OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_NODE_INHERIT_TRANSFORM OFF CACHE BOOL "" FORCE)
set(OGRE_RESTRICT_ALIASING ON CACHE BOOL "" FORCE)
set(OGRE_SIMD_NEON ${ROR_OGRE_NEXT_SIMD_NEON} CACHE BOOL "" FORCE)
set(OGRE_SIMD_SSE2 ${ROR_OGRE_NEXT_SIMD_SSE2} CACHE BOOL "" FORCE)
add_compile_definitions(OGRE_FLEXIBILITY_LEVEL=0)

# Keep the dependency checkpoint narrow: core + Compositor2, one reviewed
# renderer, and HLMS PBS. Existing RoR/OGRE 14 targets are not part of this
# standalone CMake project.
set(OGRE_BUILD_RENDERSYSTEM_D3D11 OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_RENDERSYSTEM_GL3PLUS OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_RENDERSYSTEM_GLES2 OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_RENDERSYSTEM_METAL OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_RENDERSYSTEM_VULKAN OFF CACHE BOOL "" FORCE)
if (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "macos-arm64-metal")
    set(OGRE_BUILD_RENDERSYSTEM_METAL ON CACHE BOOL "" FORCE)
elseif (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "windows-x64-d3d11")
    set(OGRE_BUILD_RENDERSYSTEM_D3D11 ON CACHE BOOL "" FORCE)
else ()
    set(OGRE_BUILD_RENDERSYSTEM_VULKAN ON CACHE BOOL "" FORCE)
    set(OGRE_CONFIG_UNIX_NO_X11 OFF CACHE BOOL "" FORCE)
    set(OGRE_VULKAN_WINDOW_NULL ON CACHE BOOL "" FORCE)
    set(OGRE_VULKAN_WINDOW_XCB ON CACHE BOOL "" FORCE)

    # Never mix host-package C++ archives into OGRE's direct glslang API.
    # shaderc v2025.3 owns an exact DEPS family; it is populated below and its
    # single combined archive carries the complete glslang/SPIR-V closure.
    foreach (_ror_forbidden_target IN ITEMS
            shaderc_combined shaderc shaderc_util glslang SPIRV
            SPIRV-Tools-opt SPIRV-Tools SPIRV-Tools-static)
        if (TARGET ${_ror_forbidden_target})
            message(FATAL_ERROR
                "A pre-existing ${_ror_forbidden_target} target would bypass "
                "the pinned Linux shader source closure")
        endif ()
    endforeach ()
    set(SHADERC_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
    set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
    set(SHADERC_ENABLE_WERROR_COMPILE OFF CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
    # shaderc is nested below OGRE, whose dependency configuration may leave
    # normal (non-cache) option bindings in an ancestor scope. CMake resolves
    # those before cache entries, so bind both scopes before shaderc adds
    # glslang. The shaderc compatibility patch repeats this at the immediate
    # add_subdirectory owner.
    set(GLSLANG_TESTS OFF)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
    set(ENABLE_SPIRV ON CACHE BOOL "" FORCE)
    set(ENABLE_OPT ON)
    set(ENABLE_OPT ON CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL OFF)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
endif ()

set(OGRE_BUILD_COMPONENT_HLMS_PBS ON CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_HLMS_UNLIT ON CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_ATMOSPHERE OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_PLANAR_REFLECTIONS OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_PAGING OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_MESHLODGENERATOR OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_VOLUME OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_PROPERTY OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_OVERLAY ON CACHE BOOL "" FORCE)
set(OGRE_BUILD_COMPONENT_SCENE_FORMAT OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_PLUGIN_PFX OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_SAMPLES2 OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OGRE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_SAMPLES OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_SAMPLES_SOURCE OFF CACHE BOOL "" FORCE)
set(OGRE_COPY_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(OGRE_INSTALL_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_FREEIMAGE OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_STBI OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_ZIP OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_JSON ON CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_MESHLOD ON CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_DDS ON CACHE BOOL "" FORCE)
set(OGRE_CONFIG_ENABLE_TBB_SCHEDULER OFF CACHE BOOL "" FORCE)
set(OGRE_CONFIG_RENDERDOC_INTEGRATION OFF CACHE BOOL "" FORCE)
set(OGRE_PROFILING_PROVIDER none CACHE STRING "" FORCE)

# OGRE-Next v3-0 still declares a pre-CMake-4 policy baseline internally.
# This compatibility floor is scoped to this standalone dependency build.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# FetchContent verifies the archive only while populating it. Reconfiguring an
# existing build directory could otherwise reuse a locally modified extracted
# source tree while still reporting the pinned archive identity. This guard is
# written before population so failed and successful configurations both
# require an explicit clean recovery.
set(_ror_fresh_configure_guard
    "${CMAKE_BINARY_DIR}/.ror-ogre-next-fresh-configured-v1")
if (EXISTS "${_ror_fresh_configure_guard}")
    message(FATAL_ERROR
        "The verified OGRE-Next probe requires a fresh build directory. "
        "Use tools/run_ogre_next_probe.py --clean-build-dir to recover.")
endif ()
file(WRITE "${_ror_fresh_configure_guard}"
    "ror-ogre-next-fresh-configured-v1\n")

# `git apply` run inside an enclosing git work tree resolves patch paths in that
# repository's context. When this probe is configured from a build directory
# that happens to live inside a checkout, every patch is reported as "Skipped
# patch", `git apply` still exits 0, and the dependency is built entirely
# unpatched. Only the reviewed-byte gates below would notice. Stop repository
# discovery at this build tree so the patch transaction is identical whether or
# not the build directory sits inside a checkout.
set(ROR_OGRE_NEXT_PATCH_ENV
    "${CMAKE_COMMAND}" -E env
    "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}")


if (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan")
    # Populate the exact dependency commits selected by shaderc's reviewed
    # DEPS file. SOURCE_SUBDIR deliberately names a nonexistent directory so
    # FetchContent verifies and extracts each archive without independently
    # configuring it; shaderc then owns their add_subdirectory order.
    FetchContent_Declare(
        ror_spirv_headers_source
        URL "${_ror_spirv_headers_url}"
        URL_HASH "SHA256=${ROR_LINUX_SPIRV_HEADERS_ARCHIVE_SHA256}"
        SOURCE_SUBDIR ror-pinned-source-only
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(
        ror_spirv_tools_source
        URL "${_ror_spirv_tools_url}"
        URL_HASH "SHA256=${ROR_LINUX_SPIRV_TOOLS_ARCHIVE_SHA256}"
        SOURCE_SUBDIR ror-pinned-source-only
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(
        ror_glslang_source
        URL "${_ror_glslang_url}"
        URL_HASH "SHA256=${ROR_LINUX_GLSLANG_ARCHIVE_SHA256}"
        SOURCE_SUBDIR ror-pinned-source-only
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(
        ror_spirv_headers_source ror_spirv_tools_source ror_glslang_source)

    set(SHADERC_SPIRV_HEADERS_DIR
        "${ror_spirv_headers_source_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(SHADERC_SPIRV_TOOLS_DIR
        "${ror_spirv_tools_source_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(SHADERC_GLSLANG_DIR
        "${ror_glslang_source_SOURCE_DIR}" CACHE PATH "" FORCE)
    FetchContent_Declare(
        shaderc
        URL "${_ror_shaderc_url}"
        URL_HASH "SHA256=${ROR_LINUX_SHADERC_ARCHIVE_SHA256}"
        PATCH_COMMAND
            ${ROR_OGRE_NEXT_PATCH_ENV}
            "${GIT_EXECUTABLE}" apply --unidiff-zero --whitespace=nowarn
            "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_LINUX_SHADERC_PATCH_PATH}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(shaderc)

    file(SHA256 "${shaderc_SOURCE_DIR}/${ROR_LINUX_SHADERC_DEPS_PATH}"
        _ror_shaderc_deps_sha256)
    if (NOT _ror_shaderc_deps_sha256 STREQUAL ROR_LINUX_SHADERC_DEPS_SHA256)
        message(FATAL_ERROR "shaderc's reviewed DEPS manifest changed")
    endif ()
    foreach (_ror_component IN ITEMS SHADERC GLSLANG SPIRV_TOOLS SPIRV_HEADERS)
        if (_ror_component STREQUAL "SHADERC")
            set(_ror_source_dir "${shaderc_SOURCE_DIR}")
        elseif (_ror_component STREQUAL "GLSLANG")
            set(_ror_source_dir "${ror_glslang_source_SOURCE_DIR}")
        elseif (_ror_component STREQUAL "SPIRV_TOOLS")
            set(_ror_source_dir "${ror_spirv_tools_source_SOURCE_DIR}")
        else ()
            set(_ror_source_dir "${ror_spirv_headers_source_SOURCE_DIR}")
        endif ()
        file(SHA256
            "${_ror_source_dir}/${ROR_LINUX_${_ror_component}_LICENSE_PATH}"
            _ror_extracted_component_license_sha256)
        if (NOT _ror_extracted_component_license_sha256 STREQUAL
                ROR_LINUX_${_ror_component}_LICENSE_SHA256)
            message(FATAL_ERROR
                "The extracted ${_ror_component} license changed")
        endif ()
    endforeach ()

    set(_ror_linux_static_targets
        shaderc_combined shaderc shaderc_util glslang SPIRV
        SPIRV-Tools-opt SPIRV-Tools-static)
    foreach (_ror_static_target IN LISTS _ror_linux_static_targets)
        if (NOT TARGET ${_ror_static_target})
            message(FATAL_ERROR
                "Pinned shader source target is unavailable: "
                "${_ror_static_target}")
        endif ()
        get_target_property(_ror_static_target_type
            ${_ror_static_target} TYPE)
        if (NOT _ror_static_target_type STREQUAL "STATIC_LIBRARY")
            message(FATAL_ERROR
                "Pinned shader closure target is not static: "
                "${_ror_static_target} (${_ror_static_target_type})")
        endif ()
    endforeach ()
    get_target_property(_ror_shaderc_combined_sources
        shaderc_combined SOURCES)
    foreach (_ror_combined_member IN ITEMS
            shaderc shaderc_util glslang SPIRV SPIRV-Tools-opt
            SPIRV-Tools-static)
        list(FIND _ror_shaderc_combined_sources
            "$<TARGET_OBJECTS:${_ror_combined_member}>"
            _ror_combined_member_index)
        if (_ror_combined_member_index EQUAL -1)
            message(FATAL_ERROR
                "shaderc_combined no longer owns ${_ror_combined_member}")
        endif ()
    endforeach ()

    # OGRE's legacy FindVulkan accepts cache values before it runs. A CMake
    # target name is intentional here: it supplies the build dependency and
    # one archive containing the full closure, without host archive discovery.
    set(_ror_glslang_to_spv_header
        "${ror_glslang_source_SOURCE_DIR}/SPIRV/GlslangToSpv.h")
    if (NOT EXISTS "${_ror_glslang_to_spv_header}")
        message(FATAL_ERROR
            "Pinned glslang source layout lacks SPIRV/GlslangToSpv.h")
    endif ()
    set(Vulkan_SHADERC_INCLUDE_DIR
        "${shaderc_SOURCE_DIR}/libshaderc/include;${ror_glslang_source_SOURCE_DIR}"
        CACHE STRING "Pinned shaderc and glslang include roots" FORCE)
    set(Vulkan_SHADERC_LIB_REL shaderc_combined
        CACHE STRING "Pinned source-built shaderc closure target" FORCE)
    set(Vulkan_SHADERC_LIB_DBG shaderc_combined
        CACHE STRING "Pinned source-built shaderc closure target" FORCE)

    set(ROR_OGRE_NEXT_LINUX_APACHE_NOTICE_SOURCE
        "${shaderc_SOURCE_DIR}/${ROR_LINUX_SHADERC_LICENSE_PATH}")
    set(ROR_OGRE_NEXT_LINUX_GLSLANG_NOTICE_SOURCE
        "${ror_glslang_source_SOURCE_DIR}/${ROR_LINUX_GLSLANG_LICENSE_PATH}")
    set(ROR_OGRE_NEXT_LINUX_SPIRV_TOOLS_NOTICE_SOURCE
        "${ror_spirv_tools_source_SOURCE_DIR}/${ROR_LINUX_SPIRV_TOOLS_LICENSE_PATH}")
    set(ROR_OGRE_NEXT_LINUX_SPIRV_HEADERS_NOTICE_SOURCE
        "${ror_spirv_headers_source_SOURCE_DIR}/${ROR_LINUX_SPIRV_HEADERS_LICENSE_PATH}")

    set(ROR_OGRE_NEXT_LINUX_STATIC_CLOSURE_MANIFEST
        "${CMAKE_BINARY_DIR}/ogre-next-linux-static-closure.json")
    set(_ror_static_manifest_script
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/cmake/WriteLinuxStaticClosureManifest.cmake")
    set(_ror_static_manifest_arguments
        "-DOUTPUT=${ROR_OGRE_NEXT_LINUX_STATIC_CLOSURE_MANIFEST}"
        "-DLOCK_PATH=${ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_PATH}"
        "-DEXPECTED_LOCK_SHA256=${ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_SHA256}"
        "-DCOMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
        "-DCOMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}"
        "-DSYSTEM_NAME=${CMAKE_SYSTEM_NAME}"
        "-DSYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}"
        "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DARTIFACT_COUNT=7"
        "-DARTIFACT_0_NAME=shaderc_combined"
        "-DARTIFACT_0_PATH=$<TARGET_FILE:shaderc_combined>"
        "-DARTIFACT_1_NAME=shaderc"
        "-DARTIFACT_1_PATH=$<TARGET_FILE:shaderc>"
        "-DARTIFACT_2_NAME=shaderc_util"
        "-DARTIFACT_2_PATH=$<TARGET_FILE:shaderc_util>"
        "-DARTIFACT_3_NAME=glslang"
        "-DARTIFACT_3_PATH=$<TARGET_FILE:glslang>"
        "-DARTIFACT_4_NAME=SPIRV"
        "-DARTIFACT_4_PATH=$<TARGET_FILE:SPIRV>"
        "-DARTIFACT_5_NAME=SPIRV-Tools-opt"
        "-DARTIFACT_5_PATH=$<TARGET_FILE:SPIRV-Tools-opt>"
        "-DARTIFACT_6_NAME=SPIRV-Tools-static"
        "-DARTIFACT_6_PATH=$<TARGET_FILE:SPIRV-Tools-static>")
    add_custom_command(
        OUTPUT "${ROR_OGRE_NEXT_LINUX_STATIC_CLOSURE_MANIFEST}"
        COMMAND "${CMAKE_COMMAND}" ${_ror_static_manifest_arguments}
                -P "${_ror_static_manifest_script}"
        DEPENDS
            ${_ror_linux_static_targets}
            "${ROR_OGRE_NEXT_LINUX_TOOLCHAIN_LOCK_PATH}"
            "${_ror_static_manifest_script}"
        COMMENT "Recording exact Linux shader static-archive provenance"
        VERBATIM)
    add_custom_target(ror_ogre_next_linux_static_closure_manifest
        DEPENDS "${ROR_OGRE_NEXT_LINUX_STATIC_CLOSURE_MANIFEST}")
    add_custom_target(ror_ogre_next_linux_static_closure_verify
        COMMAND "${CMAKE_COMMAND}" ${_ror_static_manifest_arguments}
                -DVERIFY_EXISTING=ON -P "${_ror_static_manifest_script}"
        DEPENDS ror_ogre_next_linux_static_closure_manifest
        COMMENT "Rehashing the exact Linux shader static-archive closure"
        VERBATIM)
endif ()

FetchContent_Declare(
    rapidjson
    URL "${_ror_rapidjson_url}"
    URL_HASH "SHA256=${ROR_RAPIDJSON_ARCHIVE_SHA256}"
    SOURCE_SUBDIR ror-header-only-no-cmake
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(rapidjson)
file(SHA256
    "${rapidjson_SOURCE_DIR}/${ROR_RAPIDJSON_LICENSE_PATH}"
    _ror_extracted_rapidjson_license_sha256)
if (NOT _ror_extracted_rapidjson_license_sha256 STREQUAL ROR_RAPIDJSON_LICENSE_SHA256)
    message(FATAL_ERROR
        "The extracted RapidJSON license does not match the pinned contract")
endif ()
set(Rapidjson_HOME "${rapidjson_SOURCE_DIR}" CACHE PATH "" FORCE)

# Overlay is part of the cross-platform HDR/UI-isolation contract. The
# standalone probe builds the reviewed static source closure. The root
# one-process provider instead reuses the exact static FreeType target already
# admitted by the locked OGRE14 Conan graph, while still extracting and hashing
# the independent source/license archive below.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    FetchContent_Declare(
        ror_freetype
        URL ${_ror_freetype_urls}
        URL_HASH "SHA256=${ROR_FREETYPE_ARCHIVE_SHA256}"
        SOURCE_SUBDIR ror-source-only-no-cmake
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
else ()
    FetchContent_Declare(
        ror_freetype
        URL ${_ror_freetype_urls}
        URL_HASH "SHA256=${ROR_FREETYPE_ARCHIVE_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif ()
FetchContent_MakeAvailable(ror_freetype)
if (NOT FT_DISABLE_ZLIB OR NOT FT_DISABLE_BZIP2 OR
        NOT FT_DISABLE_PNG OR NOT FT_DISABLE_HARFBUZZ OR
        NOT FT_DISABLE_BROTLI)
    message(FATAL_ERROR "The pinned FreeType feature closure changed")
endif ()
if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    if (NOT TARGET Freetype::Freetype OR
            NOT FREETYPE_VERSION_STRING STREQUAL ROR_FREETYPE_VERSION OR
            NOT freetype_LIBRARY_TYPE_RELEASE STREQUAL "STATIC" OR
            NOT freetype_PACKAGE_FOLDER_RELEASE OR
            NOT FREETYPE_INCLUDE_DIRS)
        message(FATAL_ERROR
            "The root provider lacks the locked static FreeType interface")
    endif ()
    set(ROR_OGRE_NEXT_FREETYPE_TARGET Freetype::Freetype)
    set(ROR_FREETYPE_TARGET_TYPE "ROOT_LOCKED_STATIC_IMPORT")
    foreach (_ror_root_freetype_include IN LISTS FREETYPE_INCLUDE_DIRS)
        cmake_path(IS_PREFIX freetype_PACKAGE_FOLDER_RELEASE
            "${_ror_root_freetype_include}" NORMALIZE
            _ror_root_freetype_include_is_locked)
        if (NOT _ror_root_freetype_include_is_locked)
            message(FATAL_ERROR
                "Root FreeType include escaped its locked package")
        endif ()
    endforeach ()
else ()
    if (NOT TARGET freetype OR BUILD_SHARED_LIBS)
        message(FATAL_ERROR "The pinned static FreeType closure changed")
    endif ()
    get_target_property(ROR_FREETYPE_TARGET_TYPE freetype TYPE)
    if (NOT ROR_FREETYPE_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR
            "The pinned FreeType target is not a derived static library")
    endif ()
    set(ROR_OGRE_NEXT_FREETYPE_TARGET freetype)
endif ()
set(ROR_FREETYPE_STATIC_LINK_JSON true)
file(SHA256
    "${ror_freetype_SOURCE_DIR}/${ROR_FREETYPE_LICENSE_PATH}"
    _ror_extracted_freetype_license_sha256)
file(SHA256
    "${ror_freetype_SOURCE_DIR}/${ROR_FREETYPE_OVERVIEW_PATH}"
    _ror_extracted_freetype_overview_sha256)
if (NOT _ror_extracted_freetype_license_sha256 STREQUAL
        ROR_FREETYPE_LICENSE_SHA256 OR
        NOT _ror_extracted_freetype_overview_sha256 STREQUAL
        ROR_FREETYPE_OVERVIEW_SHA256)
    message(FATAL_ERROR
        "The extracted FreeType license contract does not match the pin")
endif ()

# Ogre-Next v3-0 uses its legacy FindFreetype module. Seed that interface with
# the selected locked target and include roots so it cannot probe a host path.
if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    set(FREETYPE_HOME "${freetype_PACKAGE_FOLDER_RELEASE}" CACHE PATH "" FORCE)
    list(GET FREETYPE_INCLUDE_DIRS 0 _ror_freetype_primary_include)
    set(FREETYPE_INCLUDE_DIR "${_ror_freetype_primary_include}"
        CACHE PATH "" FORCE)
    set(FREETYPE_FT2BUILD_INCLUDE_DIR "${_ror_freetype_primary_include}"
        CACHE PATH "" FORCE)
    set(FREETYPE_INCLUDE_DIRS "${FREETYPE_INCLUDE_DIRS}"
        CACHE STRING "" FORCE)
else ()
    set(FREETYPE_HOME "${ror_freetype_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(FREETYPE_INCLUDE_DIR "${ror_freetype_SOURCE_DIR}/include"
        CACHE PATH "" FORCE)
    set(FREETYPE_FT2BUILD_INCLUDE_DIR "${ror_freetype_SOURCE_DIR}/include"
        CACHE PATH "" FORCE)
    set(FREETYPE_INCLUDE_DIRS
        "${ror_freetype_BINARY_DIR}/include;${ror_freetype_SOURCE_DIR}/include"
        CACHE STRING "" FORCE)
endif ()
set(FREETYPE_FOUND TRUE CACHE BOOL "" FORCE)
set(FREETYPE_LIBRARIES "${ROR_OGRE_NEXT_FREETYPE_TARGET}"
    CACHE STRING "" FORCE)

set(_ror_ogre_next_patch_paths
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_PATCH_PATH}"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_IBL_PATCH_PATH}"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCH_PATH}"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_VULKAN_SKY_PATCH_PATH}"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_PATCH_PATH}"
    "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_OGRE_NEXT_BARRIER_PATCH_PATH}")
if (ROR_OGRE_NEXT_EMBEDDED_NAMESPACE)
    list(APPEND _ror_ogre_next_patch_paths
        "${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_PATCH}")
endif ()
if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    list(APPEND _ror_ogre_next_patch_paths
        "${ROR_OGRE_NEXT_ROOT_PROVIDER_INSTALL_PATCH}")
endif ()
if (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "windows-x64-d3d11")
    list(APPEND _ror_ogre_next_patch_paths
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_WINDOWS_DXR7_PATCH_PATH}")
elseif (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan")
    list(APPEND _ror_ogre_next_patch_paths
        "${ROR_OGRE_NEXT_STANDALONE_ROOT}/${ROR_LINUX_OGRE_GLSLANG_PATCH_PATH}")
endif ()

# Hosted Windows Git defaults to core.autocrlf=true. Override it for the
# archive patch transaction so reviewed shader bytes stay LF-identical.
FetchContent_Declare(
    ogre_next
    URL "${_ror_ogre_next_url}"
    URL_HASH "SHA256=${ROR_OGRE_NEXT_ARCHIVE_SHA256}"
    PATCH_COMMAND
        ${ROR_OGRE_NEXT_PATCH_ENV}
        "${GIT_EXECUTABLE}" -c core.autocrlf=false apply --unidiff-zero
        --whitespace=nowarn
        ${_ror_ogre_next_patch_paths}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ogre_next)

function(ror_ogre_next_enable_embedded_namespace _ror_target)
    if (NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE)
        return()
    endif ()
    if (NOT TARGET ${_ror_target})
        message(FATAL_ERROR
            "Cannot namespace missing OgreNext target: ${_ror_target}")
    endif ()
    cmake_parse_arguments(PARSE_ARGV 1 _ror_namespace "" "" "LANGUAGES")
    if (_ror_namespace_UNPARSED_ARGUMENTS OR
            _ror_namespace_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "Invalid embedded namespace arguments for ${_ror_target}")
    endif ()
    if (NOT _ror_namespace_LANGUAGES)
        set(_ror_namespace_LANGUAGES CXX OBJCXX)
    endif ()
    foreach (_ror_namespace_language IN LISTS _ror_namespace_LANGUAGES)
        if (NOT _ror_namespace_language STREQUAL "CXX" AND
                NOT _ror_namespace_language STREQUAL "OBJCXX")
            message(FATAL_ERROR
                "Unsupported namespace language for ${_ror_target}: "
                "${_ror_namespace_language}")
        endif ()
    endforeach ()
    list(REMOVE_DUPLICATES _ror_namespace_LANGUAGES)
    list(SORT _ror_namespace_LANGUAGES)
    string(JOIN "," _ror_namespace_language_expression
        ${_ror_namespace_LANGUAGES})
    get_target_property(_ror_namespace_applied ${_ror_target}
        ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_APPLIED)
    if (_ror_namespace_applied)
        message(FATAL_ERROR
            "Embedded namespace was registered twice for ${_ror_target}")
    endif ()
    if (MSVC)
        set(_ror_force_include
            "/FI${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP}")
    else ()
        set(_ror_force_include
            "-include${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_REMAP}")
    endif ()
    target_compile_options(${_ror_target} PRIVATE
        "$<$<COMPILE_LANGUAGE:${_ror_namespace_language_expression}>:${_ror_force_include}>")
    set_property(TARGET ${_ror_target} PROPERTY
        ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_APPLIED TRUE)
    set_property(TARGET ${_ror_target} PROPERTY
        ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_LANGUAGES
        "${_ror_namespace_LANGUAGES}")
endfunction()

function(_ror_ogre_next_collect_targets _ror_directory _ror_output)
    get_property(_ror_local_targets DIRECTORY "${_ror_directory}"
        PROPERTY BUILDSYSTEM_TARGETS)
    set(_ror_targets ${_ror_local_targets})
    get_property(_ror_subdirectories DIRECTORY "${_ror_directory}"
        PROPERTY SUBDIRECTORIES)
    foreach (_ror_subdirectory IN LISTS _ror_subdirectories)
        _ror_ogre_next_collect_targets("${_ror_subdirectory}"
            _ror_nested_targets)
        list(APPEND _ror_targets ${_ror_nested_targets})
    endforeach ()
    set(${_ror_output} "${_ror_targets}" PARENT_SCOPE)
endfunction()

if (ROR_OGRE_NEXT_EMBEDDED_NAMESPACE)
    _ror_ogre_next_collect_targets("${ogre_next_SOURCE_DIR}"
        ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_TARGETS)
    list(REMOVE_DUPLICATES ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_TARGETS)
    list(SORT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_TARGETS)
    if (NOT ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_TARGETS)
        message(FATAL_ERROR
            "No pinned OgreNext targets were found for namespace isolation")
    endif ()
    foreach (_ror_ogre_target IN LISTS
            ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_TARGETS)
        get_target_property(_ror_ogre_target_type ${_ror_ogre_target} TYPE)
        if (NOT _ror_ogre_target_type STREQUAL "UTILITY" AND
                NOT _ror_ogre_target_type STREQUAL "INTERFACE_LIBRARY")
            ror_ogre_next_enable_embedded_namespace(
                ${_ror_ogre_target} LANGUAGES CXX OBJCXX)
        endif ()
    endforeach ()
    message(STATUS
        "Embedded OgreNext namespace targets: "
        "${ROR_OGRE_NEXT_EMBEDDED_NAMESPACE_TARGETS}")
endif ()

if (ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    # DependenciesConfig.cmake already required the exact 2.32.10 Conan
    # package. Never declare, populate, or link a second SDL implementation in
    # the one-process runtime.
    set(ROR_OGRE_NEXT_PRESENTATION_SDL_TARGET SDL2::SDL2)
    get_target_property(_ror_root_sdl_imported SDL2::SDL2 IMPORTED)
    if (NOT _ror_root_sdl_imported)
        message(FATAL_ERROR
            "The combined runtime requires root SDL2::SDL2 to be imported")
    endif ()
else ()
    # Compile the exact SDL source only for the standalone native-window probe.
    # X11 is intentional on Linux: pinned Ogre-Next has an XCB external-window
    # bridge and no reviewed Wayland bridge. SDL2main/install/tests/shared
    # output stay outside this source closure.
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_RPATH OFF CACHE BOOL "" FORCE)
    if (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan")
        set(SDL_X11 ON CACHE BOOL "" FORCE)
        set(SDL_X11_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
    endif ()
    FetchContent_MakeAvailable(ror_sdl2)
    if (NOT TARGET SDL2::SDL2 OR NOT TARGET SDL2::SDL2-static)
        message(FATAL_ERROR "Pinned SDL2 static CMake targets are unavailable")
    endif ()
    file(SHA256 "${ror_sdl2_SOURCE_DIR}/${ROR_SDL2_PRESENTATION_LICENSE_PATH}"
        _ror_sdl2_license_sha256)
    if (NOT _ror_sdl2_license_sha256 STREQUAL
            ROR_SDL2_PRESENTATION_LICENSE_SHA256)
        message(FATAL_ERROR "Pinned SDL2 license hash changed")
    endif ()
    set(ROR_OGRE_NEXT_PRESENTATION_SDL_TARGET SDL2::SDL2-static)
endif ()

# Ogre-Next's legacy finder is allowed to populate diagnostic cache entries,
# but the actual include and link interfaces must retain the selected locked
# target exactly.
if (NOT "${FREETYPE_LIBRARIES}" STREQUAL
            "${ROR_OGRE_NEXT_FREETYPE_TARGET}" OR
        NOT FREETYPE_INCLUDE_DIRS)
    message(FATAL_ERROR
        "OGRE-Next did not retain the locked FreeType interface")
endif ()
if (NOT ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER)
    foreach (_ror_freetype_include IN LISTS FREETYPE_INCLUDE_DIRS)
        cmake_path(IS_PREFIX ror_freetype_SOURCE_DIR
            "${_ror_freetype_include}" NORMALIZE _ror_include_in_freetype_source)
        cmake_path(IS_PREFIX ror_freetype_BINARY_DIR
            "${_ror_freetype_include}" NORMALIZE _ror_include_in_freetype_binary)
        if (NOT _ror_include_in_freetype_source AND
                NOT _ror_include_in_freetype_binary)
            message(FATAL_ERROR
                "OGRE-Next selected a host FreeType include directory: "
                "${_ror_freetype_include}")
        endif ()
    endforeach ()
endif ()
get_target_property(_ror_overlay_link_libraries OgreNextOverlay LINK_LIBRARIES)
list(FIND _ror_overlay_link_libraries
    "${ROR_OGRE_NEXT_FREETYPE_TARGET}" _ror_overlay_freetype_index)
if (_ror_overlay_freetype_index LESS 0)
    message(FATAL_ERROR
        "OgreNextOverlay does not link the pinned FreeType target")
endif ()
foreach (_ror_overlay_link_library IN LISTS _ror_overlay_link_libraries)
    string(TOLOWER "${_ror_overlay_link_library}"
        _ror_overlay_link_library_lower)
    if (_ror_overlay_link_library_lower MATCHES "freetype" AND
            NOT _ror_overlay_link_library STREQUAL
                "${ROR_OGRE_NEXT_FREETYPE_TARGET}")
        message(FATAL_ERROR
            "OgreNextOverlay selected an unpinned FreeType link input: "
            "${_ror_overlay_link_library}")
    endif ()
endforeach ()
set(ROR_FREETYPE_OVERLAY_LINK_TARGET_JSON true)

file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_IBL_PATCH_SOURCE_PATH}"
    _ror_extracted_ibl_shader_sha256)
if (NOT _ror_extracted_ibl_shader_sha256 STREQUAL
        ROR_OGRE_NEXT_IBL_PATCHED_SHA256)
    message(FATAL_ERROR
        "The pinned OGRE-Next IBL shader patch did not produce reviewed bytes: "
        "expected ${ROR_OGRE_NEXT_IBL_PATCHED_SHA256}, got "
        "${_ror_extracted_ibl_shader_sha256}. When the observed hash equals the "
        "recorded source hash the patch transaction did not run at all; check "
        "that git did not resolve the patch inside an enclosing repository.")
endif ()

file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_METAL_ANISOTROPY_SOURCE_PATH}"
    _ror_extracted_metal_anisotropy_source_sha256)
if (NOT _ror_extracted_metal_anisotropy_source_sha256 STREQUAL
        ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCHED_SHA256)
    message(FATAL_ERROR
        "The pinned OGRE-Next Metal anisotropy patch did not produce reviewed bytes: "
        "expected ${ROR_OGRE_NEXT_METAL_ANISOTROPY_PATCHED_SHA256}, got "
        "${_ror_extracted_metal_anisotropy_source_sha256}")
endif ()

file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_VULKAN_SKY_SOURCE_PATH}"
    _ror_extracted_vulkan_sky_source_sha256)
if (NOT _ror_extracted_vulkan_sky_source_sha256 STREQUAL
        ROR_OGRE_NEXT_VULKAN_SKY_PATCHED_SHA256)
    message(FATAL_ERROR
        "The pinned OGRE-Next Vulkan sky shader patch did not produce reviewed bytes: "
        "expected ${ROR_OGRE_NEXT_VULKAN_SKY_PATCHED_SHA256}, got "
        "${_ror_extracted_vulkan_sky_source_sha256}")
endif ()

file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATH}"
    _ror_extracted_texture_shutdown_header_sha256)
file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATH}"
    _ror_extracted_texture_shutdown_implementation_sha256)
if (NOT _ror_extracted_texture_shutdown_header_sha256 STREQUAL
        ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_HEADER_PATCHED_SHA256 OR
        NOT _ror_extracted_texture_shutdown_implementation_sha256 STREQUAL
        ROR_OGRE_NEXT_TEXTURE_SHUTDOWN_IMPLEMENTATION_PATCHED_SHA256)
    message(FATAL_ERROR
        "The pinned OGRE-Next texture shutdown patch did not produce reviewed bytes")
endif ()

file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_BARRIER_HEADER_PATH}"
    _ror_extracted_barrier_header_sha256)
file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_BARRIER_IMPLEMENTATION_PATH}"
    _ror_extracted_barrier_implementation_sha256)
if (NOT _ror_extracted_barrier_header_sha256 STREQUAL
        ROR_OGRE_NEXT_BARRIER_HEADER_PATCHED_SHA256 OR
        NOT _ror_extracted_barrier_implementation_sha256 STREQUAL
        ROR_OGRE_NEXT_BARRIER_IMPLEMENTATION_PATCHED_SHA256)
    message(FATAL_ERROR
        "The pinned OGRE-Next reusable pthread barrier patch did not produce reviewed bytes")
endif ()

foreach (_ror_normal_map_source_index RANGE 0
        ${_ror_normal_map_source_last})
    list(GET _ror_normal_map_source_paths ${_ror_normal_map_source_index}
        _ror_normal_map_source_path)
    set(_ror_normal_map_source_file
        "${ogre_next_SOURCE_DIR}/${_ror_normal_map_source_path}")
    if (NOT EXISTS "${_ror_normal_map_source_file}" OR
            IS_SYMLINK "${_ror_normal_map_source_file}")
        message(FATAL_ERROR
            "Pinned normal-map source owner is missing or indirect: "
            "${_ror_normal_map_source_path}")
    endif ()
    file(SHA256 "${_ror_normal_map_source_file}"
        _ror_normal_map_source_actual_sha256)
    list(GET _ror_normal_map_source_hashes ${_ror_normal_map_source_index}
        _ror_normal_map_source_expected_sha256)
    if (NOT _ror_normal_map_source_actual_sha256 STREQUAL
            _ror_normal_map_source_expected_sha256)
        message(FATAL_ERROR
            "Pinned normal-map source owner changed: "
            "${_ror_normal_map_source_path}")
    endif ()
endforeach ()

file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_LICENSE_PATH}"
    _ror_extracted_license_sha256)
if (NOT _ror_extracted_license_sha256 STREQUAL ROR_OGRE_NEXT_LICENSE_SHA256)
    message(FATAL_ERROR
        "The extracted OGRE-Next license does not match the pinned contract")
endif ()
file(SHA256
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_PATH}"
    _ror_extracted_shader_media_source_sha256)
if (NOT _ror_extracted_shader_media_source_sha256 STREQUAL
        ROR_OGRE_NEXT_SHADER_MEDIA_SOURCE_SHA256)
    message(FATAL_ERROR
        "The extracted OGRE-Next shader-media notice source changed")
endif ()

set(ROR_OGRE_NEXT_PACKAGE_OGRE_LICENSE_SOURCE
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_LICENSE_PATH}")
set(ROR_OGRE_NEXT_PACKAGE_RAPIDJSON_LICENSE_SOURCE
    "${rapidjson_SOURCE_DIR}/${ROR_RAPIDJSON_LICENSE_PATH}")
set(ROR_OGRE_NEXT_PACKAGE_FREETYPE_LICENSE_SOURCE
    "${ror_freetype_SOURCE_DIR}/${ROR_FREETYPE_LICENSE_PATH}")
set(ROR_OGRE_NEXT_PACKAGE_FREETYPE_OVERVIEW_SOURCE
    "${ror_freetype_SOURCE_DIR}/${ROR_FREETYPE_OVERVIEW_PATH}")
set(ROR_OGRE_NEXT_PACKAGE_IBLBAKER_LICENSE_SOURCE
    "${ogre_next_SOURCE_DIR}/${ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_SOURCE_PATH}")
file(SHA256 "${ROR_OGRE_NEXT_PACKAGE_IBLBAKER_LICENSE_SOURCE}"
    _ror_extracted_iblbaker_license_sha256)
if (NOT _ror_extracted_iblbaker_license_sha256 STREQUAL
        ROR_OGRE_NEXT_REFLECTION_MEDIA_LICENSE_SOURCE_SHA256)
    message(FATAL_ERROR
        "The extracted IBLBaker notice does not match the pinned contract")
endif ()
if (ROR_OGRE_NEXT_PLATFORM_POLICY STREQUAL "linux-x86_64-vulkan")
    file(SHA256
        "${ogre_next_SOURCE_DIR}/${ROR_LINUX_SPIRV_REFLECT_SOURCE_PATH}"
        _ror_spirv_reflect_source_sha256)
    file(SHA256
        "${ogre_next_SOURCE_DIR}/${ROR_LINUX_SPIRV_REFLECT_HEADER_PATH}"
        _ror_spirv_reflect_header_sha256)
    if (NOT _ror_spirv_reflect_source_sha256 STREQUAL
            ROR_LINUX_SPIRV_REFLECT_SOURCE_SHA256 OR
            NOT _ror_spirv_reflect_header_sha256 STREQUAL
            ROR_LINUX_SPIRV_REFLECT_HEADER_SHA256)
        message(FATAL_ERROR
            "OGRE's embedded SPIRV-Reflect source provenance changed")
    endif ()
    file(READ
        "${ogre_next_SOURCE_DIR}/RenderSystems/Vulkan/src/OgreVulkanProgram.cpp"
        _ror_patched_vulkan_program)
    if (NOT _ror_patched_vulkan_program MATCHES
            "SPIRV/GlslangToSpv.h" OR
            _ror_patched_vulkan_program MATCHES
            "glslang/SPIRV/GlslangToSpv.h" OR
            _ror_patched_vulkan_program MATCHES "struct SpvOptions")
        message(FATAL_ERROR
            "OGRE did not consume the pinned glslang SpvOptions definition")
    endif ()
endif ()

foreach (_ror_required_target IN ITEMS
        OgreNextMain
        OgreNextHlmsPbs
        OgreNextHlmsUnlit
        OgreNextOverlay
        ${ROR_OGRE_NEXT_RENDERER_TARGET})
    if (NOT TARGET ${_ror_required_target})
        message(FATAL_ERROR
            "Required OGRE-Next capability target is unavailable: "
            "${_ror_required_target}")
    endif ()
endforeach ()

if (NOT OGRE_STATIC OR NOT OGRE_USE_NEW_PROJECT_NAME OR OGRE_CONFIG_DOUBLE OR
        NOT OGRE_BUILD_COMPONENT_HLMS_PBS OR
        NOT OGRE_BUILD_COMPONENT_HLMS_UNLIT OR
        NOT OGRE_BUILD_COMPONENT_OVERLAY)
    message(FATAL_ERROR "OGRE-Next changed a required ABI/capability option")
endif ()
