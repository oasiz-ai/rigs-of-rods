if (NOT DEFINED N2_EXECUTABLE OR NOT DEFINED N2_OUTPUT OR
    NOT DEFINED N2_REPORT)
    message(FATAL_ERROR "RunN2Smoke requires executable, output, and report paths")
endif ()

execute_process(
    COMMAND "${N2_EXECUTABLE}" --output "${N2_OUTPUT}" --report "${N2_REPORT}"
    RESULT_VARIABLE _ror_n2_result)

if (NOT EXISTS "${N2_REPORT}")
    message(FATAL_ERROR "Metal N2 smoke did not write its capability report")
endif ()
if (_ror_n2_result EQUAL 77)
    message(STATUS "Metal N2 hardware probe skipped; see ${N2_REPORT}")
elseif (NOT _ror_n2_result EQUAL 0)
    message(FATAL_ERROR "Metal N2 smoke failed with exit code ${_ror_n2_result}")
endif ()
