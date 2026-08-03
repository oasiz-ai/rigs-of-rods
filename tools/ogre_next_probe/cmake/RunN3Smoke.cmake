if (NOT DEFINED N3_EXECUTABLE OR NOT DEFINED N3_MEDIA_ROOT OR
    NOT DEFINED N3_RASTER OR NOT DEFINED N3_CONTRIBUTION OR
    NOT DEFINED N3_HYBRID OR NOT DEFINED N3_REPORT)
    message(FATAL_ERROR
        "RunN3Smoke requires executable, media, raster, contribution, hybrid, and report paths")
endif ()

execute_process(
    COMMAND "${N3_EXECUTABLE}"
            --media-root "${N3_MEDIA_ROOT}"
            --raster "${N3_RASTER}"
            --contribution "${N3_CONTRIBUTION}"
            --hybrid "${N3_HYBRID}"
            --report "${N3_REPORT}"
    RESULT_VARIABLE _ror_n3_result)

if (NOT EXISTS "${N3_REPORT}")
    message(FATAL_ERROR "Metal N3 smoke did not write its capability report")
endif ()
if (_ror_n3_result EQUAL 77)
    message(STATUS "Metal N3 hardware probe skipped; see ${N3_REPORT}")
elseif (NOT _ror_n3_result EQUAL 0)
    message(FATAL_ERROR "Metal N3 smoke failed with exit code ${_ror_n3_result}")
endif ()
