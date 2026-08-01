if (NOT DEFINED N1_EXECUTABLE OR NOT EXISTS "${N1_EXECUTABLE}")
    message(FATAL_ERROR "Reflection tamper test executable is missing")
endif ()
if (NOT DEFINED N1_MEDIA_ROOT OR NOT IS_DIRECTORY "${N1_MEDIA_ROOT}/Hlms")
    message(FATAL_ERROR "Reflection tamper test media root is missing")
endif ()
if (NOT DEFINED N1_TAMPER_RELATIVE OR N1_TAMPER_RELATIVE STREQUAL "")
    message(FATAL_ERROR "Reflection tamper test file is unspecified")
endif ()
if (NOT DEFINED N1_WORK_ROOT OR N1_WORK_ROOT STREQUAL "")
    message(FATAL_ERROR "Reflection tamper test work root is unspecified")
endif ()

file(REMOVE_RECURSE "${N1_WORK_ROOT}")
file(MAKE_DIRECTORY "${N1_WORK_ROOT}")
file(COPY "${N1_MEDIA_ROOT}/" DESTINATION "${N1_WORK_ROOT}")
set(_ror_tampered_file "${N1_WORK_ROOT}/${N1_TAMPER_RELATIVE}")
if (NOT EXISTS "${_ror_tampered_file}")
    message(FATAL_ERROR "Reflection tamper fixture did not copy its target")
endif ()
file(APPEND "${_ror_tampered_file}" "\nror-reflection-integrity-tamper\n")

execute_process(
    COMMAND "${N1_EXECUTABLE}"
            --media-root "${N1_WORK_ROOT}"
            --modern-pbr
            --evidence "${N1_WORK_ROOT}/must-not-exist.bin"
            --reflection-evidence
                "${N1_WORK_ROOT}/must-not-exist-reflection.bin"
    RESULT_VARIABLE _ror_result
    OUTPUT_VARIABLE _ror_stdout
    ERROR_VARIABLE _ror_stderr
    TIMEOUT 30)
if (_ror_result EQUAL 0)
    message(FATAL_ERROR "Tampered reflection media initialized successfully")
endif ()
set(_ror_output "${_ror_stdout}\n${_ror_stderr}")
if (NOT _ror_output MATCHES "reflection media integrity failure")
    message(FATAL_ERROR
        "Tampered reflection media failed for the wrong reason: ${_ror_output}")
endif ()
