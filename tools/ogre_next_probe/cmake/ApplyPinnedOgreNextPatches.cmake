# Apply the reviewed OGRE-Next patch set through one CMake-owned transaction.
# FetchContent forwards PATCH_COMMAND through ExternalProject, whose handling of
# repeated COMMAND separators has varied across hosted CMake/platform pairs.
# Keeping the loop here makes every patch an individually checked process and
# verifies the two cross-platform media postconditions before configuration.

foreach (_ror_required IN ITEMS
        ROR_GIT_EXECUTABLE
        ROR_OGRE_SOURCE_DIR
        ROR_PATCH_MANIFEST
        ROR_EXPECTED_PATCH_COUNT
        ROR_IBL_SOURCE_PATH
        ROR_IBL_PATCHED_SHA256
        ROR_METAL_ANISOTROPY_SOURCE_PATH
        ROR_METAL_ANISOTROPY_PATCHED_SHA256)
    if (NOT DEFINED ${_ror_required} OR "${${_ror_required}}" STREQUAL "")
        message(FATAL_ERROR
            "Pinned OGRE-Next patch transaction is missing ${_ror_required}")
    endif ()
endforeach ()

foreach (_ror_required_file IN ITEMS
        ROR_GIT_EXECUTABLE ROR_PATCH_MANIFEST)
    if (NOT EXISTS "${${_ror_required_file}}" OR
            IS_DIRECTORY "${${_ror_required_file}}")
        message(FATAL_ERROR
            "Pinned OGRE-Next patch input is not a file: "
            "${${_ror_required_file}}")
    endif ()
endforeach ()
if (NOT IS_DIRECTORY "${ROR_OGRE_SOURCE_DIR}")
    message(FATAL_ERROR
        "Pinned OGRE-Next source directory is unavailable: "
        "${ROR_OGRE_SOURCE_DIR}")
endif ()
if (NOT ROR_EXPECTED_PATCH_COUNT MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "Pinned OGRE-Next patch count is not canonical")
endif ()

file(READ "${ROR_PATCH_MANIFEST}" _ror_patch_manifest)
if (_ror_patch_manifest STREQUAL "" OR _ror_patch_manifest MATCHES "\r")
    message(FATAL_ERROR
        "Pinned OGRE-Next patch manifest must be nonempty LF-only text")
endif ()
string(REPLACE "\n" ";" _ror_patch_paths "${_ror_patch_manifest}")
list(LENGTH _ror_patch_paths _ror_patch_count)
if (NOT _ror_patch_count EQUAL ROR_EXPECTED_PATCH_COUNT)
    message(FATAL_ERROR
        "Pinned OGRE-Next patch manifest count changed: expected "
        "${ROR_EXPECTED_PATCH_COUNT}, got ${_ror_patch_count}")
endif ()

foreach (_ror_patch_path IN LISTS _ror_patch_paths)
    if (NOT IS_ABSOLUTE "${_ror_patch_path}" OR
            NOT EXISTS "${_ror_patch_path}" OR
            IS_DIRECTORY "${_ror_patch_path}" OR
            IS_SYMLINK "${_ror_patch_path}")
        message(FATAL_ERROR
            "Pinned OGRE-Next patch is unavailable or indirect: ${_ror_patch_path}")
    endif ()
endforeach ()

# Apply the complete manifest in one Git transaction. This is both atomic and
# avoids the hosted-Windows CMake process-loop behavior that reported success
# for later invocations while leaving their target bytes unchanged.
execute_process(
    COMMAND
        "${ROR_GIT_EXECUTABLE}" -c core.autocrlf=false apply
        --unidiff-zero --whitespace=nowarn --verbose ${_ror_patch_paths}
    WORKING_DIRECTORY "${ROR_OGRE_SOURCE_DIR}"
    RESULT_VARIABLE _ror_patch_result
    OUTPUT_VARIABLE _ror_patch_stdout
    ERROR_VARIABLE _ror_patch_stderr)
if (NOT _ror_patch_result EQUAL 0)
    message(FATAL_ERROR
        "Pinned OGRE-Next patch transaction failed: "
        "${_ror_patch_stderr}${_ror_patch_stdout}")
endif ()

foreach (_ror_postcondition IN ITEMS IBL METAL_ANISOTROPY)
    if (_ror_postcondition STREQUAL "IBL")
        set(_ror_relative_path "${ROR_IBL_SOURCE_PATH}")
        set(_ror_expected_sha256 "${ROR_IBL_PATCHED_SHA256}")
    else ()
        set(_ror_relative_path "${ROR_METAL_ANISOTROPY_SOURCE_PATH}")
        set(_ror_expected_sha256 "${ROR_METAL_ANISOTROPY_PATCHED_SHA256}")
    endif ()
    set(_ror_source_file "${ROR_OGRE_SOURCE_DIR}/${_ror_relative_path}")
    if (NOT EXISTS "${_ror_source_file}" OR
            IS_DIRECTORY "${_ror_source_file}" OR
            IS_SYMLINK "${_ror_source_file}")
        message(FATAL_ERROR
            "Pinned OGRE-Next patch postcondition source is unavailable or indirect: "
            "${_ror_relative_path}")
    endif ()
    file(SHA256 "${_ror_source_file}" _ror_actual_sha256)
    if (NOT _ror_actual_sha256 STREQUAL _ror_expected_sha256)
        message(FATAL_ERROR
            "Pinned OGRE-Next ${_ror_postcondition} patch postcondition changed: "
            "expected ${_ror_expected_sha256}, got ${_ror_actual_sha256}")
    endif ()
endforeach ()
