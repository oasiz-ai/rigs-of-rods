# Deterministic transport selection for the pinned FreeType source archive.
# The canonical remote archive and its fallback are both lock-bound; a local
# override replaces the remote list only after its content hash is verified.

include_guard(GLOBAL)

function(ror_select_freetype_archive_urls
        _ror_output _ror_local_archive _ror_expected_sha256
        _ror_primary_url _ror_fallback_url)
    if (NOT ARGC EQUAL 5 OR "${_ror_output}" STREQUAL "" OR
            "${_ror_expected_sha256}" STREQUAL "" OR
            "${_ror_primary_url}" STREQUAL "" OR
            "${_ror_fallback_url}" STREQUAL "" OR
            "${_ror_primary_url}" STREQUAL "${_ror_fallback_url}")
        message(FATAL_ERROR
            "Pinned FreeType archive transport contract is invalid")
    endif ()

    if (NOT "${_ror_local_archive}" STREQUAL "")
        if (NOT EXISTS "${_ror_local_archive}" OR
                IS_DIRECTORY "${_ror_local_archive}")
            message(FATAL_ERROR
                "Pinned FreeType archive does not exist: ${_ror_local_archive}")
        endif ()
        file(SHA256 "${_ror_local_archive}" _ror_local_freetype_sha256)
        if (NOT _ror_local_freetype_sha256 STREQUAL _ror_expected_sha256)
            message(FATAL_ERROR
                "Pinned FreeType SHA-256 mismatch: expected "
                "${_ror_expected_sha256}, got ${_ror_local_freetype_sha256}")
        endif ()
        set(_ror_freetype_urls "${_ror_local_archive}")
    else ()
        set(_ror_freetype_urls
            "${_ror_primary_url}"
            "${_ror_fallback_url}")
    endif ()

    set(${_ror_output} "${_ror_freetype_urls}" PARENT_SCOPE)
endfunction()
