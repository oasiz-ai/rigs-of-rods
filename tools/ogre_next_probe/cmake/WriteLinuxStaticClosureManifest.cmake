cmake_minimum_required(VERSION 3.24)

foreach (_ror_required IN ITEMS
        OUTPUT LOCK_PATH EXPECTED_LOCK_SHA256 COMPILER_ID COMPILER_VERSION
        SYSTEM_NAME SYSTEM_PROCESSOR BUILD_TYPE ARTIFACT_COUNT)
    if (NOT DEFINED ${_ror_required} OR "${${_ror_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required manifest input: ${_ror_required}")
    endif ()
endforeach ()

if (NOT BUILD_TYPE STREQUAL "Release" OR
        NOT SYSTEM_NAME STREQUAL "Linux" OR
        NOT SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
    message(FATAL_ERROR
        "Static closure manifest requires reviewed Linux x86_64 Release policy")
endif ()
if (NOT ARTIFACT_COUNT EQUAL 7)
    message(FATAL_ERROR "Static closure target inventory changed")
endif ()

file(SHA256 "${LOCK_PATH}" _ror_lock_sha256)
if (NOT _ror_lock_sha256 STREQUAL EXPECTED_LOCK_SHA256)
    message(FATAL_ERROR
        "Linux shader toolchain lock changed: expected ${EXPECTED_LOCK_SHA256}, "
        "got ${_ror_lock_sha256}")
endif ()
file(READ "${LOCK_PATH}" _ror_lock_json)
string(JSON _ror_lock_schema GET "${_ror_lock_json}" schema)
string(JSON _ror_lock_policy GET "${_ror_lock_json}" platform_policy)
string(JSON _ror_lock_provider GET "${_ror_lock_json}" provider)
if (NOT _ror_lock_schema STREQUAL
        "ror.ogre_next_linux_shader_toolchain.v1" OR
        NOT _ror_lock_policy STREQUAL "linux-x86_64-vulkan" OR
        NOT _ror_lock_provider STREQUAL "pinned-source")
    message(FATAL_ERROR "Linux shader source lock policy changed")
endif ()

function(_ror_json_quote _ror_value _ror_output)
    string(REPLACE "\\" "\\\\" _ror_escaped "${_ror_value}")
    string(REPLACE "\"" "\\\"" _ror_escaped "${_ror_escaped}")
    string(REPLACE "\n" "\\n" _ror_escaped "${_ror_escaped}")
    set(${_ror_output} "\"${_ror_escaped}\"" PARENT_SCOPE)
endfunction()

function(_ror_source_record _ror_json _ror_path _ror_component _ror_output)
    string(JSON _ror_repository GET "${_ror_json}" ${_ror_path} repository)
    if (_ror_component STREQUAL "shaderc")
        string(JSON _ror_version GET "${_ror_json}" ${_ror_path} tag)
    else ()
        string(JSON _ror_version GET "${_ror_json}" ${_ror_path} version)
    endif ()
    string(JSON _ror_commit GET "${_ror_json}" ${_ror_path} commit)
    string(JSON _ror_archive_sha GET "${_ror_json}" ${_ror_path} archive_sha256)
    string(JSON _ror_license_expression GET
        "${_ror_json}" ${_ror_path} license_expression)
    string(JSON _ror_license_sha GET
        "${_ror_json}" ${_ror_path} license_sha256)
    string(JSON _ror_notice_path GET
        "${_ror_json}" ${_ror_path} package_notice_path)
    string(JSON _ror_notice_sha GET
        "${_ror_json}" ${_ror_path} package_notice_sha256)
    foreach (_ror_field IN ITEMS repository version commit archive_sha
            license_expression license_sha notice_path notice_sha)
        _ror_json_quote("${_ror_${_ror_field}}" _ror_${_ror_field}_json)
    endforeach ()
    _ror_json_quote("${_ror_component}" _ror_component_json)
    string(CONCAT _ror_record
        "    {\n"
        "      \"component\": ${_ror_component_json},\n"
        "      \"repository\": ${_ror_repository_json},\n"
        "      \"version\": ${_ror_version_json},\n"
        "      \"commit\": ${_ror_commit_json},\n"
        "      \"archive_sha256\": ${_ror_archive_sha_json},\n"
        "      \"license_expression\": ${_ror_license_expression_json},\n"
        "      \"license_sha256\": ${_ror_license_sha_json},\n"
        "      \"package_notice_path\": ${_ror_notice_path_json},\n"
        "      \"package_notice_sha256\": ${_ror_notice_sha_json}\n"
        "    }")
    set(${_ror_output} "${_ror_record}" PARENT_SCOPE)
endfunction()

_ror_source_record("${_ror_lock_json}" "shaderc_release" "shaderc"
    _ror_shaderc_record)
_ror_source_record("${_ror_lock_json}" "dependencies;glslang" "glslang"
    _ror_glslang_record)
_ror_source_record("${_ror_lock_json}" "dependencies;spirv_tools" "spirv-tools"
    _ror_spirv_tools_record)
_ror_source_record("${_ror_lock_json}" "dependencies;spirv_headers" "spirv-headers"
    _ror_spirv_headers_record)

set(_ror_expected_targets
    shaderc_combined
    shaderc
    shaderc_util
    glslang
    SPIRV
    SPIRV-Tools-opt
    SPIRV-Tools-static)
set(_ror_expected_files
    libshaderc_combined.a
    libshaderc.a
    libshaderc_util.a
    libglslang.a
    libSPIRV.a
    libSPIRV-Tools-opt.a
    libSPIRV-Tools.a)
set(_ror_artifact_records "")
math(EXPR _ror_last_artifact "${ARTIFACT_COUNT} - 1")
foreach (_ror_index RANGE 0 ${_ror_last_artifact})
    set(_ror_name_var "ARTIFACT_${_ror_index}_NAME")
    set(_ror_path_var "ARTIFACT_${_ror_index}_PATH")
    if (NOT DEFINED ${_ror_name_var} OR NOT DEFINED ${_ror_path_var})
        message(FATAL_ERROR "Static closure artifact ${_ror_index} is incomplete")
    endif ()
    list(GET _ror_expected_targets ${_ror_index} _ror_expected_target)
    list(GET _ror_expected_files ${_ror_index} _ror_expected_file)
    if (NOT "${${_ror_name_var}}" STREQUAL "${_ror_expected_target}")
        message(FATAL_ERROR
            "Static closure artifact order changed at ${_ror_index}")
    endif ()
    if (NOT EXISTS "${${_ror_path_var}}")
        message(FATAL_ERROR
            "Static closure artifact is missing: ${${_ror_path_var}}")
    endif ()
    get_filename_component(_ror_file_name "${${_ror_path_var}}" NAME)
    if (NOT _ror_file_name STREQUAL _ror_expected_file)
        message(FATAL_ERROR
            "Unexpected static closure file for ${_ror_expected_target}: "
            "${_ror_file_name}")
    endif ()
    file(SHA256 "${${_ror_path_var}}" _ror_artifact_sha256)
    _ror_json_quote("${_ror_expected_target}" _ror_target_json)
    _ror_json_quote("${_ror_file_name}" _ror_file_json)
    _ror_json_quote("${_ror_artifact_sha256}" _ror_artifact_sha_json)
    if (_ror_index GREATER 0)
        string(APPEND _ror_artifact_records ",\n")
    endif ()
    string(APPEND _ror_artifact_records
        "    {\n"
        "      \"target\": ${_ror_target_json},\n"
        "      \"file\": ${_ror_file_json},\n"
        "      \"sha256\": ${_ror_artifact_sha_json}\n"
        "    }")
endforeach ()

foreach (_ror_field IN ITEMS lock_schema lock_policy lock_provider lock_sha256
        COMPILER_ID COMPILER_VERSION SYSTEM_NAME SYSTEM_PROCESSOR BUILD_TYPE)
    if (_ror_field MATCHES "^[A-Z]")
        _ror_json_quote("${${_ror_field}}" _ror_${_ror_field}_json)
    else ()
        _ror_json_quote("${_ror_${_ror_field}}" _ror_${_ror_field}_json)
    endif ()
endforeach ()

string(CONCAT _ror_manifest
    "{\n"
    "  \"schema\": \"ror.ogre_next_linux_static_closure.v1\",\n"
    "  \"status\": \"pass\",\n"
    "  \"provider\": ${_ror_lock_provider_json},\n"
    "  \"platform_policy\": ${_ror_lock_policy_json},\n"
    "  \"source_lock\": {\n"
    "    \"schema\": ${_ror_lock_schema_json},\n"
    "    \"sha256\": ${_ror_lock_sha256_json},\n"
    "    \"package_path\": \"provenance/ogre-next-linux-shader-toolchain.lock.json\"\n"
    "  },\n"
    "  \"compiler\": {\n"
    "    \"id\": ${_ror_COMPILER_ID_json},\n"
    "    \"version\": ${_ror_COMPILER_VERSION_json},\n"
    "    \"build_type\": ${_ror_BUILD_TYPE_json}\n"
    "  },\n"
    "  \"host\": {\n"
    "    \"system\": ${_ror_SYSTEM_NAME_json},\n"
    "    \"processor\": ${_ror_SYSTEM_PROCESSOR_json}\n"
    "  },\n"
    "  \"sources\": [\n"
    "${_ror_shaderc_record},\n"
    "${_ror_glslang_record},\n"
    "${_ror_spirv_tools_record},\n"
    "${_ror_spirv_headers_record}\n"
    "  ],\n"
    "  \"artifacts\": [\n"
    "${_ror_artifact_records}\n"
    "  ],\n"
    "  \"host_dynamic_boundary\": \"Vulkan-Loader\"\n"
    "}\n")

get_filename_component(_ror_output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_ror_output_dir}")
if (DEFINED VERIFY_EXISTING AND VERIFY_EXISTING)
    if (NOT EXISTS "${OUTPUT}")
        message(FATAL_ERROR "Static closure manifest is missing: ${OUTPUT}")
    endif ()
    file(READ "${OUTPUT}" _ror_existing_manifest)
    if (NOT _ror_existing_manifest STREQUAL _ror_manifest)
        message(FATAL_ERROR
            "Static closure manifest no longer matches the linked archives")
    endif ()
else ()
    set(_ror_temporary "${OUTPUT}.tmp")
    file(WRITE "${_ror_temporary}" "${_ror_manifest}")
    file(RENAME "${_ror_temporary}" "${OUTPUT}")
endif ()
