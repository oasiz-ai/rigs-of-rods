# SPDX-License-Identifier: GPL-3.0-or-later

if (NOT DEFINED ROR_RENDERER_PUBLIC_BRIDGE_LAUNCHER OR
        ROR_RENDERER_PUBLIC_BRIDGE_LAUNCHER STREQUAL "")
    message(FATAL_ERROR "The renderer public bridge launcher is missing")
endif ()
if (NOT DEFINED ROR_RENDERER_PUBLIC_BRIDGE_CASE)
    message(FATAL_ERROR "The renderer public bridge case is missing")
endif ()

set(_ror_public_bridge_owned_options)
set(_ror_public_bridge_mode "--bridge-test-game-exit=0")
set(_ror_public_bridge_expected_exit 0)
if (ROR_RENDERER_PUBLIC_BRIDGE_CASE STREQUAL "default-prefer")
    # No launcher-owned flags: OGRE_NEXT_PREFER is the public default.
elseif (ROR_RENDERER_PUBLIC_BRIDGE_CASE STREQUAL "explicit-require")
    list(APPEND _ror_public_bridge_owned_options
        "--renderer-frontend=ogre-next-require"
        "--renderer-directional-shadows=pssm")
    set(_ror_public_bridge_mode "--bridge-test-game-exit=37")
    set(_ror_public_bridge_expected_exit 37)
elseif (ROR_RENDERER_PUBLIC_BRIDGE_CASE STREQUAL "presentation-first")
    set(_ror_public_bridge_mode "--bridge-test-presentation-first")
    set(_ror_public_bridge_expected_exit 71)
elseif (ROR_RENDERER_PUBLIC_BRIDGE_CASE STREQUAL "pre-ready-fallback")
    set(_ror_public_bridge_mode "--bridge-test-pre-ready-fallback=41")
    set(_ror_public_bridge_expected_exit 41)
elseif (ROR_RENDERER_PUBLIC_BRIDGE_CASE STREQUAL
        "pre-ready-require-terminal")
    list(APPEND _ror_public_bridge_owned_options
        "--renderer-frontend=ogre-next-require"
        "--renderer-directional-shadows=pssm")
    set(_ror_public_bridge_mode "--bridge-test-pre-ready-fallback=41")
    set(_ror_public_bridge_expected_exit 71)
elseif (ROR_RENDERER_PUBLIC_BRIDGE_CASE STREQUAL "post-ready-terminal")
    set(_ror_public_bridge_mode "--bridge-test-post-ready-failure")
    set(_ror_public_bridge_expected_exit 71)
elseif (ROR_RENDERER_PUBLIC_BRIDGE_CASE STREQUAL "native-require-terminal")
    list(APPEND _ror_public_bridge_owned_options
        "--renderer-directional-shadows=require-native")
    set(_ror_public_bridge_mode "--bridge-test-pre-ready-fallback=41")
    set(_ror_public_bridge_expected_exit 70)
else ()
    message(FATAL_ERROR
        "Unknown renderer public bridge case: "
        "${ROR_RENDERER_PUBLIC_BRIDGE_CASE}")
endif ()

execute_process(
    COMMAND
        "${ROR_RENDERER_PUBLIC_BRIDGE_LAUNCHER}"
        ${_ror_public_bridge_owned_options}
        "--bridge-test-public-argv"
        "-map"
        "City World"
        "space and unicode Ω"
        "${_ror_public_bridge_mode}"
    RESULT_VARIABLE _ror_public_bridge_result
    TIMEOUT 15)
if (NOT "${_ror_public_bridge_result}" STREQUAL
        "${_ror_public_bridge_expected_exit}")
    message(FATAL_ERROR
        "Renderer public bridge case '${ROR_RENDERER_PUBLIC_BRIDGE_CASE}' "
        "returned '${_ror_public_bridge_result}', expected "
        "'${_ror_public_bridge_expected_exit}'")
endif ()
