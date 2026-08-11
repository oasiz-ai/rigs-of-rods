foreach (_ror_required_variable IN ITEMS
        INCLUDE_DIR DOCUMENT_HEADER EXPECTED_SIZE EXPECTED_SHA256)
    if (NOT DEFINED ${_ror_required_variable} OR
            "${${_ror_required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "Missing RapidJSON verification input: ${_ror_required_variable}")
    endif ()
endforeach ()

string(LENGTH "${EXPECTED_SHA256}" _ror_expected_sha256_length)
if (NOT IS_ABSOLUTE "${INCLUDE_DIR}" OR
        NOT IS_DIRECTORY "${INCLUDE_DIR}" OR
        IS_SYMLINK "${INCLUDE_DIR}" OR
        NOT IS_ABSOLUTE "${DOCUMENT_HEADER}" OR
        NOT EXISTS "${DOCUMENT_HEADER}" OR
        IS_DIRECTORY "${DOCUMENT_HEADER}" OR
        IS_SYMLINK "${DOCUMENT_HEADER}" OR
        NOT DOCUMENT_HEADER STREQUAL
            "${INCLUDE_DIR}/rapidjson/document.h" OR
        NOT EXPECTED_SIZE MATCHES "^[0-9]+$" OR
        NOT _ror_expected_sha256_length EQUAL 64 OR
        NOT EXPECTED_SHA256 MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR
        "RapidJSON verification paths or expected identity are invalid")
endif ()

file(SIZE "${DOCUMENT_HEADER}" _ror_observed_size)
file(SHA256 "${DOCUMENT_HEADER}" _ror_observed_sha256)
if (NOT _ror_observed_size EQUAL EXPECTED_SIZE OR
        NOT _ror_observed_sha256 STREQUAL EXPECTED_SHA256)
    message(FATAL_ERROR
        "RapidJSON private header closure does not match the reviewed pin")
endif ()

message(STATUS
    "Verified RapidJSON private header: ${_ror_observed_size} bytes, ${_ror_observed_sha256}")
