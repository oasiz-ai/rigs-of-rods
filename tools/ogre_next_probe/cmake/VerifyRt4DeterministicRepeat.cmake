foreach (_label IN ITEMS REPORT IMAGE ISOLATION REFLECTION COMPOSITOR)
    if (NOT DEFINED PRIMARY_${_label} OR
            NOT DEFINED REPEAT_${_label} OR
            PRIMARY_${_label} STREQUAL "" OR
            REPEAT_${_label} STREQUAL "")
        message(FATAL_ERROR
            "RT4 deterministic repeat paths are incomplete for ${_label}")
    endif ()
    foreach (_kind IN ITEMS PRIMARY REPEAT)
        set(_path "${${_kind}_${_label}}")
        if (NOT EXISTS "${_path}" OR IS_DIRECTORY "${_path}" OR
                IS_SYMLINK "${_path}")
            message(FATAL_ERROR
                "RT4 deterministic ${_kind} ${_label} is not a regular direct file")
        endif ()
        file(SIZE "${_path}" _size)
        if (_size EQUAL 0)
            message(FATAL_ERROR
                "RT4 deterministic ${_kind} ${_label} is empty")
        endif ()
        file(SHA256 "${_path}" ${_kind}_${_label}_SHA256)
    endforeach ()
    if (NOT PRIMARY_${_label}_SHA256 STREQUAL
            REPEAT_${_label}_SHA256)
        message(FATAL_ERROR
            "RT4 deterministic repeat ${_label} differs from the canonical run")
    endif ()
endforeach ()

message(STATUS
    "RT4 deterministic repeat report, compositor frame, texture isolation, reflection, and HDR evidence are byte-identical")
