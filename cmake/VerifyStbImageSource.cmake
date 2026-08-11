# Verify the exact renderer-neutral PNG/JPEG decoder source before any target
# can compile it. This intentionally does not fetch or repair vendored bytes.

set(_ROR_VERIFY_STB_IMAGE_SOURCE_CMAKE "${CMAKE_CURRENT_LIST_FILE}")

function(_ror_stb_extract_json_object json key output_variable)
    string(REGEX MATCH
        "\"${key}\"[ \t\r\n]*:[ \t\r\n]*\\{([^}]*)\\}"
        _ror_stb_json_match "${json}")
    if (_ror_stb_json_match STREQUAL "")
        message(FATAL_ERROR
            "Pinned stb_image source lock lacks object field: ${key}")
    endif ()
    set(${output_variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(_ror_stb_extract_json_string json key output_variable)
    string(REGEX MATCH
        "\"${key}\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]*)\""
        _ror_stb_json_match "${json}")
    if (_ror_stb_json_match STREQUAL "")
        message(FATAL_ERROR
            "Pinned stb_image source lock lacks string field: ${key}")
    endif ()
    set(${output_variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(_ror_stb_extract_json_unsigned json key output_variable)
    string(REGEX MATCH
        "\"${key}\"[ \t\r\n]*:[ \t\r\n]*([0-9]+)"
        _ror_stb_json_match "${json}")
    if (_ror_stb_json_match STREQUAL "")
        message(FATAL_ERROR
            "Pinned stb_image source lock lacks unsigned field: ${key}")
    endif ()
    set(${output_variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(_ror_stb_extract_json_string_array json key output_variable)
    string(REGEX MATCH
        "\"${key}\"[ \t\r\n]*:[ \t\r\n]*\\[([^]]*)\\]"
        _ror_stb_json_match "${json}")
    if (_ror_stb_json_match STREQUAL "")
        message(FATAL_ERROR
            "Pinned stb_image source lock lacks string-array field: ${key}")
    endif ()
    set(_ror_stb_json_array_body "${CMAKE_MATCH_1}")
    string(REGEX MATCHALL "\"[^\"]*\"" _ror_stb_json_strings
        "${_ror_stb_json_array_body}")
    set(_ror_stb_json_values "")
    foreach (_ror_stb_json_string IN LISTS _ror_stb_json_strings)
        string(REGEX REPLACE "^\"|\"$" "" _ror_stb_json_value
            "${_ror_stb_json_string}")
        list(APPEND _ror_stb_json_values "${_ror_stb_json_value}")
    endforeach ()
    set(${output_variable} "${_ror_stb_json_values}" PARENT_SCOPE)
endfunction()

function(ror_verify_stb_image_source repository_root)
    set(_ror_stb_root
        "${repository_root}/source/main/gfx/render/third_party/stb")
    set(_ror_stb_header "${_ror_stb_root}/stb_image.h")
    set(_ror_stb_lock "${_ror_stb_root}/stb-image-source.lock.json")
    set(_ror_stb_license "${_ror_stb_root}/LICENSE.txt")
    set(_ror_stb_decoder
        "${repository_root}/source/main/gfx/render/Ogre14SourceTextureDecoder.cpp")

    foreach (_ror_stb_path IN ITEMS
            "${_ror_stb_header}"
            "${_ror_stb_lock}"
            "${_ror_stb_license}"
            "${_ror_stb_decoder}")
        if (NOT EXISTS "${_ror_stb_path}" OR IS_DIRECTORY "${_ror_stb_path}" OR
                IS_SYMLINK "${_ror_stb_path}")
            message(FATAL_ERROR
                "Pinned stb_image source input is missing, not a regular file, or is a symlink: ${_ror_stb_path}")
        endif ()
    endforeach ()

    file(SIZE "${_ror_stb_header}" _ror_stb_header_bytes)
    file(SIZE "${_ror_stb_license}" _ror_stb_license_bytes)
    file(SHA256 "${_ror_stb_header}" _ror_stb_header_sha256)
    file(SHA256 "${_ror_stb_lock}" _ror_stb_lock_sha256)
    file(SHA256 "${_ror_stb_license}" _ror_stb_license_sha256)
    if (NOT _ror_stb_header_bytes EQUAL 283010 OR
            NOT _ror_stb_header_sha256 STREQUAL
                "594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3" OR
            NOT _ror_stb_lock_sha256 STREQUAL
                "9902dd2891f8d8733d24cc06316ec98e23eac5b108ccca6c1a519cc94ddf61b6" OR
            NOT _ror_stb_license_bytes EQUAL 2362 OR
            NOT _ror_stb_license_sha256 STREQUAL
                "771d43eb5017cb859978ad3ddb027fb80ea6119681f286950053404d95b21707")
        message(FATAL_ERROR
            "Pinned stb_image header, source lock, or license digest differs from the reviewed source identity")
    endif ()

    # CMake 3.16 predates string(JSON). The whole-file digest above fixes the
    # JSON spelling and rejects duplicate-key substitutions; these narrow
    # extractors then reconcile every consumed semantic field with the bytes
    # and translation-unit policy that this function actually verified.
    file(READ "${_ror_stb_lock}" _ror_stb_lock_json)
    _ror_stb_extract_json_string("${_ror_stb_lock_json}" "schema"
        _ror_stb_lock_schema)
    _ror_stb_extract_json_object("${_ror_stb_lock_json}" "dependency"
        _ror_stb_dependency_json)
    _ror_stb_extract_json_string("${_ror_stb_dependency_json}" "name"
        _ror_stb_dependency_name)
    _ror_stb_extract_json_string("${_ror_stb_dependency_json}" "repository"
        _ror_stb_dependency_repository)
    _ror_stb_extract_json_string("${_ror_stb_dependency_json}" "commit"
        _ror_stb_dependency_commit)
    _ror_stb_extract_json_string("${_ror_stb_dependency_json}" "source_path"
        _ror_stb_dependency_source_path)
    _ror_stb_extract_json_string("${_ror_stb_dependency_json}" "source_url"
        _ror_stb_dependency_source_url)
    _ror_stb_extract_json_string("${_ror_stb_dependency_json}" "vendored_path"
        _ror_stb_dependency_vendored_path)
    _ror_stb_extract_json_unsigned("${_ror_stb_dependency_json}" "bytes"
        _ror_stb_dependency_bytes)
    _ror_stb_extract_json_string("${_ror_stb_dependency_json}" "sha256"
        _ror_stb_dependency_sha256)

    _ror_stb_extract_json_object("${_ror_stb_lock_json}" "license"
        _ror_stb_license_json)
    _ror_stb_extract_json_string("${_ror_stb_license_json}" "expression"
        _ror_stb_license_expression)
    _ror_stb_extract_json_string("${_ror_stb_license_json}" "source_path"
        _ror_stb_license_source_path)
    _ror_stb_extract_json_string("${_ror_stb_license_json}" "notice_path"
        _ror_stb_license_notice_path)
    _ror_stb_extract_json_unsigned("${_ror_stb_license_json}" "notice_bytes"
        _ror_stb_license_notice_bytes)
    _ror_stb_extract_json_string("${_ror_stb_license_json}" "notice_sha256"
        _ror_stb_license_notice_sha256)

    _ror_stb_extract_json_object("${_ror_stb_lock_json}" "compile_contract"
        _ror_stb_compile_contract_json)
    _ror_stb_extract_json_string("${_ror_stb_compile_contract_json}"
        "implementation_linkage" _ror_stb_implementation_linkage)
    _ror_stb_extract_json_string_array("${_ror_stb_compile_contract_json}"
        "formats" _ror_stb_lock_formats)
    _ror_stb_extract_json_string_array("${_ror_stb_compile_contract_json}"
        "definitions" _ror_stb_lock_definitions)
    _ror_stb_extract_json_string("${_ror_stb_compile_contract_json}"
        "product_input" _ror_stb_product_input)

    set(_ror_stb_expected_header_relative
        "source/main/gfx/render/third_party/stb/stb_image.h")
    set(_ror_stb_expected_license_relative
        "source/main/gfx/render/third_party/stb/LICENSE.txt")
    if (NOT _ror_stb_lock_schema STREQUAL
            "ror.ogre14_source_image_codec.v1" OR
            NOT _ror_stb_dependency_name STREQUAL "stb_image" OR
            NOT _ror_stb_dependency_repository STREQUAL
                "https://github.com/nothings/stb" OR
            NOT _ror_stb_dependency_commit STREQUAL
                "2c980bb59875b0d32144a71867fbdebb2f77cd20" OR
            NOT _ror_stb_dependency_source_path STREQUAL "stb_image.h" OR
            NOT _ror_stb_dependency_source_url STREQUAL
                "https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h" OR
            NOT _ror_stb_dependency_vendored_path STREQUAL
                _ror_stb_expected_header_relative OR
            NOT _ror_stb_dependency_bytes EQUAL _ror_stb_header_bytes OR
            NOT _ror_stb_dependency_sha256 STREQUAL
                _ror_stb_header_sha256 OR
            NOT _ror_stb_license_expression STREQUAL "MIT OR Unlicense" OR
            NOT _ror_stb_license_source_path STREQUAL
                _ror_stb_expected_header_relative OR
            NOT _ror_stb_license_notice_path STREQUAL
                _ror_stb_expected_license_relative OR
            NOT _ror_stb_license_notice_bytes EQUAL _ror_stb_license_bytes OR
            NOT _ror_stb_license_notice_sha256 STREQUAL
                _ror_stb_license_sha256 OR
            NOT _ror_stb_implementation_linkage STREQUAL "private-static" OR
            NOT "${_ror_stb_lock_formats}" STREQUAL "PNG;JPEG" OR
            NOT _ror_stb_product_input STREQUAL
                "authenticated-source-bytes-only")
        message(FATAL_ERROR
            "Pinned stb_image source-lock semantics differ from the verified source, license, or product policy")
    endif ()

    file(READ "${_ror_stb_header}" _ror_stb_header_source)
    file(READ "${_ror_stb_decoder}" _ror_stb_decoder_source)
    set(_ror_stb_expected_definitions
"#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 8192")
    file(STRINGS "${_ror_stb_decoder}" _ror_stb_actual_definition_lines
        REGEX "^[ \t]*#[ \t]*define[ \t]+ST(B_IMAGE|BI)_[A-Za-z0-9_]+")
    list(LENGTH _ror_stb_actual_definition_lines
        _ror_stb_actual_definition_count)
    set(_ror_stb_actual_canonical_definition_lines "")
    set(_ror_stb_actual_lock_definitions "")
    foreach (_ror_stb_actual_definition_line IN LISTS
            _ror_stb_actual_definition_lines)
        string(REGEX REPLACE "^[ \t]*#[ \t]*define[ \t]+" "#define "
            _ror_stb_actual_canonical_definition_line
            "${_ror_stb_actual_definition_line}")
        list(APPEND _ror_stb_actual_canonical_definition_lines
            "${_ror_stb_actual_canonical_definition_line}")
        string(REGEX REPLACE "^#define[ \t]+" ""
            _ror_stb_actual_lock_definition
            "${_ror_stb_actual_canonical_definition_line}")
        string(REGEX REPLACE "[ \t]+" "="
            _ror_stb_actual_lock_definition
            "${_ror_stb_actual_lock_definition}")
        list(APPEND _ror_stb_actual_lock_definitions
            "${_ror_stb_actual_lock_definition}")
    endforeach ()
    string(JOIN "\n" _ror_stb_actual_definitions
        ${_ror_stb_actual_canonical_definition_lines})
    string(FIND "${_ror_stb_decoder_source}"
        "${_ror_stb_expected_definitions}" _ror_stb_definition_offset)
    string(FIND "${_ror_stb_decoder_source}"
        "#include \"third_party/stb/stb_image.h\"" _ror_stb_include_offset)
    string(FIND "${_ror_stb_decoder_source}"
        "#if defined(STBIDEF)" _ror_stb_guard_offset)
    string(FIND "${_ror_stb_decoder_source}"
        "#include" _ror_stb_first_include_offset)
    if (NOT _ror_stb_actual_definition_count EQUAL 10 OR
            NOT _ror_stb_actual_definitions STREQUAL
                "${_ror_stb_expected_definitions}" OR
            _ror_stb_definition_offset LESS 0 OR
            _ror_stb_include_offset LESS 0 OR
            _ror_stb_guard_offset LESS 0 OR
            _ror_stb_first_include_offset LESS 0 OR
            NOT _ror_stb_guard_offset LESS _ror_stb_definition_offset OR
            NOT _ror_stb_definition_offset LESS _ror_stb_include_offset OR
            NOT _ror_stb_include_offset EQUAL _ror_stb_first_include_offset OR
            NOT "${_ror_stb_lock_definitions}" STREQUAL
                "${_ror_stb_actual_lock_definitions}")
        message(FATAL_ERROR
            "stb_image must retain its injection guard and exact reviewed private/static macro set before every include")
    endif ()

    # Reconcile the guard with every stb-prefixed macro that the exact pinned
    # header consults in a preprocessor conditional. This makes toolchain -D
    # and forced-include policy injection fail in the translation unit, while
    # the header digest above prevents the consulted set from drifting.
    string(REGEX MATCHALL
        "(defined[ \t\r\n]*\\([ \t\r\n]*|#[ \t]*(ifdef|ifndef)[ \t]+)(STBIDEF|STB_IMAGE_[A-Z0-9_]+|STBI_[A-Z0-9_]+)"
        _ror_stb_consulted_macro_matches "${_ror_stb_header_source}")
    set(_ror_stb_consulted_macros "")
    foreach (_ror_stb_consulted_macro_match IN LISTS
            _ror_stb_consulted_macro_matches)
        string(REGEX REPLACE
            "^(defined[ \t\r\n]*\\([ \t\r\n]*|#[ \t]*(ifdef|ifndef)[ \t]+)" ""
            _ror_stb_consulted_macro "${_ror_stb_consulted_macro_match}")
        list(APPEND _ror_stb_consulted_macros "${_ror_stb_consulted_macro}")
    endforeach ()
    list(REMOVE_DUPLICATES _ror_stb_consulted_macros)
    list(SORT _ror_stb_consulted_macros)
    string(SUBSTRING "${_ror_stb_decoder_source}" ${_ror_stb_guard_offset}
        ${_ror_stb_definition_offset} _ror_stb_guard_source)
    string(REGEX MATCHALL
        "defined[ \t\r\n]*\\([ \t\r\n]*(STBIDEF|STB_IMAGE_[A-Z0-9_]+|STBI_[A-Z0-9_]+)"
        _ror_stb_guard_macro_matches "${_ror_stb_guard_source}")
    set(_ror_stb_guarded_macros "")
    foreach (_ror_stb_guard_macro_match IN LISTS _ror_stb_guard_macro_matches)
        string(REGEX REPLACE "^defined[ \t\r\n]*\\([ \t\r\n]*" ""
            _ror_stb_guarded_macro "${_ror_stb_guard_macro_match}")
        list(APPEND _ror_stb_guarded_macros "${_ror_stb_guarded_macro}")
    endforeach ()
    list(REMOVE_DUPLICATES _ror_stb_guarded_macros)
    list(SORT _ror_stb_guarded_macros)
    if (NOT "${_ror_stb_consulted_macros}" STREQUAL
            "${_ror_stb_guarded_macros}")
        message(FATAL_ERROR
            "stb_image injection guard differs from the exact pinned header's externally consulted macro set")
    endif ()

    string(REGEX MATCHALL
        "#include[ \t]+\"third_party/stb/stb_image\\.h\""
        _ror_stb_includes "${_ror_stb_decoder_source}")
    list(LENGTH _ror_stb_includes _ror_stb_include_count)
    if (NOT _ror_stb_include_count EQUAL 1)
        message(FATAL_ERROR
            "stb_image implementation must be confined to one reviewed translation-unit include")
    endif ()

    file(GLOB_RECURSE _ror_stb_policy_sources LIST_DIRECTORIES false
        "${repository_root}/source/*.c"
        "${repository_root}/source/*.cc"
        "${repository_root}/source/*.cpp"
        "${repository_root}/source/*.h"
        "${repository_root}/source/*.hh"
        "${repository_root}/source/*.hpp"
        "${repository_root}/source/*.inl"
        "${repository_root}/source/*.m"
        "${repository_root}/source/*.mm"
        "${repository_root}/tests/*.c"
        "${repository_root}/tests/*.cc"
        "${repository_root}/tests/*.cpp"
        "${repository_root}/tests/*.h"
        "${repository_root}/tests/*.hh"
        "${repository_root}/tests/*.hpp"
        "${repository_root}/tests/*.inl"
        "${repository_root}/tools/*.c"
        "${repository_root}/tools/*.cc"
        "${repository_root}/tools/*.cpp"
        "${repository_root}/tools/*.h"
        "${repository_root}/tools/*.hh"
        "${repository_root}/tools/*.hpp"
        "${repository_root}/tools/*.inl")
    set(_ror_stb_implementation_owners "")
    foreach (_ror_stb_policy_source IN LISTS _ror_stb_policy_sources)
        string(FIND "${_ror_stb_policy_source}" "${_ror_stb_root}/"
            _ror_stb_vendor_prefix)
        if (_ror_stb_vendor_prefix EQUAL 0)
            continue()
        endif ()
        file(STRINGS "${_ror_stb_policy_source}"
            _ror_stb_policy_definition_lines
            REGEX "^[ \t]*#[ \t]*define[ \t]+(STBIDEF|STB_IMAGE_[A-Z0-9_]+|STBI_[A-Z0-9_]+)([ \t]|$)")
        if (_ror_stb_policy_definition_lines AND
                NOT _ror_stb_policy_source STREQUAL "${_ror_stb_decoder}")
            message(FATAL_ERROR
                "stb_image configuration macro was defined outside the reviewed decoder translation unit: ${_ror_stb_policy_source}")
        endif ()
        file(STRINGS "${_ror_stb_policy_source}"
            _ror_stb_implementation_lines
            REGEX "^[ \t]*#[ \t]*define[ \t]+STB_IMAGE_IMPLEMENTATION([ \t]|$)")
        if (_ror_stb_implementation_lines)
            list(APPEND _ror_stb_implementation_owners "${_ror_stb_policy_source}")
        endif ()
    endforeach ()
    list(LENGTH _ror_stb_implementation_owners
        _ror_stb_implementation_owner_count)
    if (NOT _ror_stb_implementation_owner_count EQUAL 1 OR
            NOT _ror_stb_implementation_owners STREQUAL
                "${_ror_stb_decoder}")
        message(FATAL_ERROR
            "STB_IMAGE_IMPLEMENTATION must have exactly one repository translation-unit owner")
    endif ()

    # Reject repository-owned target/global compile-definition injection. The
    # source guard above independently catches command-line/toolchain and
    # forced-include injection when the decoder is actually compiled.
    set(_ror_stb_cmake_inputs "${repository_root}/CMakeLists.txt")
    file(GLOB_RECURSE _ror_stb_nested_cmake_inputs LIST_DIRECTORIES false
        "${repository_root}/cmake/*.cmake"
        "${repository_root}/source/CMakeLists.txt"
        "${repository_root}/source/*/CMakeLists.txt"
        "${repository_root}/tests/CMakeLists.txt"
        "${repository_root}/tests/*.cmake"
        "${repository_root}/tools/CMakeLists.txt"
        "${repository_root}/tools/*.cmake")
    list(APPEND _ror_stb_cmake_inputs ${_ror_stb_nested_cmake_inputs})
    list(REMOVE_DUPLICATES _ror_stb_cmake_inputs)
    foreach (_ror_stb_cmake_input IN LISTS _ror_stb_cmake_inputs)
        if (_ror_stb_cmake_input STREQUAL
                "${_ROR_VERIFY_STB_IMAGE_SOURCE_CMAKE}")
            continue()
        endif ()
        file(STRINGS "${_ror_stb_cmake_input}" _ror_stb_cmake_policy_lines
            REGEX "(^|[^A-Za-z0-9_])(STBIDEF|STB_IMAGE_(IMPLEMENTATION|STATIC)|STBI_[A-Z0-9_]+)([^A-Za-z0-9_]|$)")
        if (_ror_stb_cmake_policy_lines)
            message(FATAL_ERROR
                "stb_image configuration appeared in repository CMake outside its verifier: ${_ror_stb_cmake_input}")
        endif ()
    endforeach ()

    # Export only after every byte, identity, and translation-unit policy check
    # has passed. Combined-runtime provider receipts consume these cache values
    # directly, avoiding a second parser or an independently maintained lock.
    set(ROR_STB_IMAGE_SOURCE_SCHEMA
        "${_ror_stb_lock_schema}" CACHE INTERNAL
        "Verified stb_image source-lock schema" FORCE)
    set(ROR_STB_IMAGE_UPSTREAM_COMMIT
        "${_ror_stb_dependency_commit}" CACHE INTERNAL
        "Verified stb_image upstream commit" FORCE)
    set(ROR_STB_IMAGE_SOURCE_LOCK_RELATIVE_PATH
        "source/main/gfx/render/third_party/stb/stb-image-source.lock.json"
        CACHE INTERNAL "Verified stb_image source-lock relative path" FORCE)
    set(ROR_STB_IMAGE_SOURCE_LOCK_PATH "${_ror_stb_lock}" CACHE INTERNAL
        "Verified stb_image source-lock absolute path" FORCE)
    set(ROR_STB_IMAGE_SOURCE_LOCK_SHA256
        "${_ror_stb_lock_sha256}"
        CACHE INTERNAL "Verified stb_image source-lock digest" FORCE)
    set(ROR_STB_IMAGE_HEADER_RELATIVE_PATH
        "${_ror_stb_dependency_vendored_path}" CACHE INTERNAL
        "Verified stb_image header relative path" FORCE)
    set(ROR_STB_IMAGE_HEADER_PATH "${_ror_stb_header}" CACHE INTERNAL
        "Verified stb_image header absolute path" FORCE)
    set(ROR_STB_IMAGE_HEADER_SIZE "${_ror_stb_dependency_bytes}" CACHE INTERNAL
        "Verified stb_image header byte count" FORCE)
    set(ROR_STB_IMAGE_HEADER_SHA256
        "${_ror_stb_dependency_sha256}"
        CACHE INTERNAL "Verified stb_image header digest" FORCE)
    set(ROR_STB_IMAGE_LICENSE_RELATIVE_PATH
        "${_ror_stb_license_notice_path}" CACHE INTERNAL
        "Verified stb_image license relative path" FORCE)
    set(ROR_STB_IMAGE_LICENSE_PATH "${_ror_stb_license}" CACHE INTERNAL
        "Verified stb_image license absolute path" FORCE)
    set(ROR_STB_IMAGE_LICENSE_SHA256
        "${_ror_stb_license_notice_sha256}"
        CACHE INTERNAL "Verified stb_image license digest" FORCE)
    set(ROR_STB_IMAGE_LICENSE_SIZE "${_ror_stb_license_notice_bytes}"
        CACHE INTERNAL "Verified stb_image license byte count" FORCE)
    set(ROR_STB_IMAGE_MACRO_CONTRACT_VERIFIED TRUE CACHE INTERNAL
        "Exact private/static stb_image macro contract was verified" FORCE)
    set(ROR_STB_IMAGE_MACRO_CONTRACT_VERIFIED_JSON true CACHE INTERNAL
        "JSON spelling of verified stb_image macro-contract state" FORCE)
endfunction()
