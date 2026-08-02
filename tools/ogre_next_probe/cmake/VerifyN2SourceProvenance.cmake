# This source file is part of Rigs of Rods.
# Fail closed if the source compiled into the native Metal N2 proof is not the
# exact clean relevant-source manifest recorded at configure time.

if (NOT DEFINED N2_REPOSITORY_ROOT OR
    NOT DEFINED N2_EXPECTED_SOURCE_MANIFEST_SHA256)
    message(FATAL_ERROR "Metal N2 source provenance inputs are missing")
endif ()

set(_ror_n2_relevant_source_paths
    source/main/gfx/RendererBackendPolicy.cpp
    source/main/gfx/RendererBackendPolicy.h
    source/main/gfx/RendererStartupHandoff.cpp
    source/main/gfx/RendererStartupHandoff.h
    source/main/gfx/RendererStartupPlan.cpp
    source/main/gfx/RendererStartupPlan.h
    source/main/gfx/render
    tests/gfx/RendererBackendPolicyTests.cpp
    tests/gfx/RendererStartupHandoffTests.cpp
    tests/gfx/RendererStartupPlanTests.cpp
    tools/ogre_next_probe
    tools/run_ogre_next_probe.py
    tools/validate_ogre_next_frame_probe.py
    tools/verify_ogre_next_artifact_set.py)
execute_process(
    COMMAND git status --porcelain=v1 --untracked-files=all --
            ${_ror_n2_relevant_source_paths}
    WORKING_DIRECTORY "${N2_REPOSITORY_ROOT}"
    RESULT_VARIABLE _ror_n2_status_result
    OUTPUT_VARIABLE _ror_n2_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE _ror_n2_status_error)
if (NOT _ror_n2_status_result EQUAL 0)
    message(FATAL_ERROR
        "Could not inspect Metal N2 relevant source state: "
        "${_ror_n2_status_error}")
endif ()
if (NOT _ror_n2_status STREQUAL "")
    message(FATAL_ERROR
        "Metal N2 provenance requires a clean relevant source set:\n"
        "${_ror_n2_status}")
endif ()

file(GLOB_RECURSE _ror_n2_relevant_source_files
    LIST_DIRECTORIES false
    RELATIVE "${N2_REPOSITORY_ROOT}"
    "${N2_REPOSITORY_ROOT}/source/main/gfx/render/*"
    "${N2_REPOSITORY_ROOT}/tools/ogre_next_probe/*")
list(APPEND _ror_n2_relevant_source_files
    "source/main/gfx/RendererBackendPolicy.cpp"
    "source/main/gfx/RendererBackendPolicy.h"
    "source/main/gfx/RendererStartupHandoff.cpp"
    "source/main/gfx/RendererStartupHandoff.h"
    "source/main/gfx/RendererStartupPlan.cpp"
    "source/main/gfx/RendererStartupPlan.h"
    "tests/gfx/RendererBackendPolicyTests.cpp"
    "tests/gfx/RendererStartupHandoffTests.cpp"
    "tests/gfx/RendererStartupPlanTests.cpp"
    "tools/run_ogre_next_probe.py"
    "tools/validate_ogre_next_frame_probe.py"
    "tools/verify_ogre_next_artifact_set.py")
list(FILTER _ror_n2_relevant_source_files EXCLUDE REGEX
    "(^|/)__pycache__/|\\.py[co]$|(^|/)\\.DS_Store$")
list(REMOVE_DUPLICATES _ror_n2_relevant_source_files)
list(SORT _ror_n2_relevant_source_files)
set(_ror_n2_source_manifest "")
foreach (_ror_n2_relative IN LISTS _ror_n2_relevant_source_files)
    set(_ror_n2_file "${N2_REPOSITORY_ROOT}/${_ror_n2_relative}")
    if (NOT EXISTS "${_ror_n2_file}" OR IS_SYMLINK "${_ror_n2_file}")
        message(FATAL_ERROR
            "Metal N2 relevant source is missing or indirect: "
            "${_ror_n2_relative}")
    endif ()
    if (_ror_n2_relative MATCHES "[;\\\\\"]")
        message(FATAL_ERROR
            "Metal N2 relevant source path cannot be attested: "
            "${_ror_n2_relative}")
    endif ()
    file(SIZE "${_ror_n2_file}" _ror_n2_source_size)
    file(SHA256 "${_ror_n2_file}" _ror_n2_source_sha256)
    string(APPEND _ror_n2_source_manifest
        "${_ror_n2_relative}|${_ror_n2_source_size}|"
        "${_ror_n2_source_sha256}\n")
endforeach ()
if (NOT _ror_n2_relevant_source_files)
    message(FATAL_ERROR "Metal N2 relevant-source manifest is empty")
endif ()
string(SHA256 _ror_n2_source_manifest_sha256 "${_ror_n2_source_manifest}")
if (NOT "${_ror_n2_source_manifest_sha256}" STREQUAL
    "${N2_EXPECTED_SOURCE_MANIFEST_SHA256}")
    message(FATAL_ERROR
        "Metal N2 relevant-source manifest changed after configuration")
endif ()
