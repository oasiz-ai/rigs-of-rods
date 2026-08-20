if (NOT DEFINED N1_EXECUTABLE OR NOT EXISTS "${N1_EXECUTABLE}")
    message(FATAL_ERROR "N1 tamper test executable is missing")
endif ()
if (NOT DEFINED N1_MEDIA_ROOT OR
        NOT IS_DIRECTORY "${N1_MEDIA_ROOT}/Hlms")
    message(FATAL_ERROR "N1 tamper test media root is missing")
endif ()
if (NOT DEFINED N1_TAMPER_RELATIVE OR N1_TAMPER_RELATIVE STREQUAL "")
    message(FATAL_ERROR "N1 tamper test file is unspecified")
endif ()
if (NOT DEFINED N1_WORK_ROOT OR N1_WORK_ROOT STREQUAL "")
    message(FATAL_ERROR "N1 tamper test work root is unspecified")
endif ()

file(REMOVE_RECURSE "${N1_WORK_ROOT}")
file(MAKE_DIRECTORY "${N1_WORK_ROOT}")
file(COPY "${N1_MEDIA_ROOT}/Hlms" DESTINATION "${N1_WORK_ROOT}")
if (N1_MODERN_PBR)
    if (NOT IS_DIRECTORY "${N1_MEDIA_ROOT}/2.0")
        message(FATAL_ERROR "N1 HDR tamper media root is incomplete")
    endif ()
    file(MAKE_DIRECTORY "${N1_WORK_ROOT}/2.0/scripts/materials")
    file(COPY "${N1_MEDIA_ROOT}/2.0/scripts/Compositors"
        DESTINATION "${N1_WORK_ROOT}/2.0/scripts")
    # RoRHaze is a manifest scan root like Common and HDR, so the fixture must
    # copy it too: otherwise the run would fail on a missing file instead of on
    # the single byte this test deliberately tampers with.
    file(COPY
        "${N1_MEDIA_ROOT}/2.0/scripts/materials/Common"
        "${N1_MEDIA_ROOT}/2.0/scripts/materials/HDR"
        "${N1_MEDIA_ROOT}/2.0/scripts/materials/RoRHaze"
        DESTINATION "${N1_WORK_ROOT}/2.0/scripts/materials")
    set(_ror_tampered_file "${N1_WORK_ROOT}/${N1_TAMPER_RELATIVE}")
else ()
    set(_ror_tampered_file "${N1_WORK_ROOT}/Hlms/${N1_TAMPER_RELATIVE}")
endif ()
if (NOT EXISTS "${_ror_tampered_file}")
    message(FATAL_ERROR "N1 tamper fixture did not copy its target")
endif ()
file(APPEND "${_ror_tampered_file}" "\nror-n1-integrity-tamper\n")

set(_ror_command "${N1_EXECUTABLE}" --media-root "${N1_WORK_ROOT}")
if (N1_MODERN_PBR)
    list(APPEND _ror_command
        --modern-pbr
        --output "${N1_WORK_ROOT}/tamper.ppm"
        --report "${N1_WORK_ROOT}/tamper.json"
        --evidence "${N1_WORK_ROOT}/tamper.bin"
        --reflection-evidence "${N1_WORK_ROOT}/tamper-reflection.bin"
        --compositor-evidence "${N1_WORK_ROOT}/tamper-hdr.bin")
endif ()
execute_process(
    COMMAND ${_ror_command}
    RESULT_VARIABLE _ror_result
    OUTPUT_VARIABLE _ror_stdout
    ERROR_VARIABLE _ror_stderr
    TIMEOUT 30)
if (_ror_result EQUAL 0)
    message(FATAL_ERROR "Tampered N1 shader media initialized successfully")
endif ()
set(_ror_output "${_ror_stdout}\n${_ror_stderr}")
if (N1_EXPECTED_INTEGRITY_KIND)
    set(_ror_integrity_pattern
        "${N1_EXPECTED_INTEGRITY_KIND} media integrity failure")
else ()
    set(_ror_integrity_pattern "shader media integrity failure")
endif ()
if (NOT _ror_output MATCHES "${_ror_integrity_pattern}")
    message(FATAL_ERROR
        "Tampered N1 shader media failed for the wrong reason: ${_ror_output}")
endif ()
