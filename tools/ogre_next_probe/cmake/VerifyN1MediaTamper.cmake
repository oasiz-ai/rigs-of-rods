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
set(_ror_tampered_file "${N1_WORK_ROOT}/Hlms/${N1_TAMPER_RELATIVE}")
if (NOT EXISTS "${_ror_tampered_file}")
    message(FATAL_ERROR "N1 tamper fixture did not copy its target")
endif ()
file(APPEND "${_ror_tampered_file}" "\nror-n1-integrity-tamper\n")

execute_process(
    COMMAND "${N1_EXECUTABLE}" --media-root "${N1_WORK_ROOT}"
    RESULT_VARIABLE _ror_result
    OUTPUT_VARIABLE _ror_stdout
    ERROR_VARIABLE _ror_stderr
    TIMEOUT 30)
if (_ror_result EQUAL 0)
    message(FATAL_ERROR "Tampered N1 shader media initialized successfully")
endif ()
set(_ror_output "${_ror_stdout}\n${_ror_stderr}")
if (NOT _ror_output MATCHES "shader media integrity failure")
    message(FATAL_ERROR
        "Tampered N1 shader media failed for the wrong reason: ${_ror_output}")
endif ()
