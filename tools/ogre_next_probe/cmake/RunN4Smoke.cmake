if (NOT DEFINED N4_EXECUTABLE OR NOT DEFINED N4_MEDIA_ROOT OR
    NOT DEFINED N4_RASTER OR NOT DEFINED N4_VISIBILITY OR
    NOT DEFINED N4_LINEAGE OR NOT DEFINED N4_HYBRID OR
    NOT DEFINED N4_REPORT)
    message(FATAL_ERROR
        "RunN4Smoke requires executable, media, raster, visibility, lineage, hybrid, and report paths")
endif ()

execute_process(
    COMMAND "${N4_EXECUTABLE}"
            --media-root "${N4_MEDIA_ROOT}"
            --raster "${N4_RASTER}"
            --visibility "${N4_VISIBILITY}"
            --lineage "${N4_LINEAGE}"
            --hybrid "${N4_HYBRID}"
            --report "${N4_REPORT}"
    RESULT_VARIABLE _ror_n4_result)

if (NOT EXISTS "${N4_REPORT}")
    message(FATAL_ERROR
        "Metal N4 directional-shadow smoke did not write its capability report")
endif ()
if (_ror_n4_result EQUAL 77)
    message(STATUS
        "Metal N4 directional-shadow hardware probe skipped; see ${N4_REPORT}")
elseif (NOT _ror_n4_result EQUAL 0)
    message(FATAL_ERROR
        "Metal N4 directional-shadow smoke failed with exit code ${_ror_n4_result}")
endif ()
