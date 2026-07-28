if (NOT DEFINED ROR_BEAMNG_ZIP_INDEX_TOOL OR
    NOT EXISTS "${ROR_BEAMNG_ZIP_INDEX_TOOL}")
    message(FATAL_ERROR
        "ROR_BEAMNG_ZIP_INDEX_TOOL must name the built index executable")
endif ()

set(formulacoupe_archive "$ENV{ROR_BEAMNG_FORMULACOUPE_ZIP}")
if (formulacoupe_archive STREQUAL "")
    message("SKIP: FormulaCOUPE fixture is not explicitly supplied")
    return ()
endif ()
if (NOT EXISTS "${formulacoupe_archive}" OR
    IS_DIRECTORY "${formulacoupe_archive}")
    message(FATAL_ERROR
        "ROR_BEAMNG_FORMULACOUPE_ZIP is not a readable archive file")
endif ()

set(expected_sha256
    "f0ecff776eeb8962ed039ca02695713972f1839d754edd3385d47bb597a2cbcd")
file(SHA256 "${formulacoupe_archive}" actual_sha256)
if (NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR
        "FormulaCOUPE fixture version drift: expected SHA-256 "
        "${expected_sha256}, got ${actual_sha256}")
endif ()

execute_process(
    COMMAND
        "${ROR_BEAMNG_ZIP_INDEX_TOOL}"
        "${formulacoupe_archive}"
    RESULT_VARIABLE index_result
    OUTPUT_VARIABLE index_output
    ERROR_VARIABLE index_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)
if (NOT index_result EQUAL 0)
    message(FATAL_ERROR
        "FormulaCOUPE J0 index failed (${index_result}): "
        "${index_output} ${index_error}")
endif ()

set(expected_output
    "{\"status\":\"valid\","
    "\"zip_profile\":\"pkware-appnote:6.3.10-classic-single-disk-index-v1\","
    "\"package_profile\":\"beamng-docs:0.38.5.0-2026-07-27\","
    "\"archive_bytes\":223853684,"
    "\"entry_count\":460,"
    "\"total_expanded_bytes\":642023303,"
    "\"pc_configuration_count\":39}")
string(CONCAT expected_output ${expected_output})
if (NOT index_output STREQUAL expected_output)
    message(FATAL_ERROR
        "FormulaCOUPE J0 result drift: expected ${expected_output}, "
        "got ${index_output}")
endif ()

message(STATUS
    "FormulaCOUPE v0.9.7 J0 metadata fixture passed: ${index_output}")
