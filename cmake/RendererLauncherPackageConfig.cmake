# SPDX-License-Identifier: GPL-3.0-or-later

# Configure the immutable package facts consumed by the public renderer
# launcher. Ordinary renderer-suite packages admit only the real OGRE 14 game
# child. A bounded demo package may admit the verified OgreNext/PSSM child with
# one explicit build-system argument; there is no runtime admission channel.
function(ror_configure_renderer_launcher_package_facts
        template_path output_directory output_header_variable
        admit_ogre_next_demo)
    if (NOT IS_ABSOLUTE "${template_path}"
            OR NOT EXISTS "${template_path}"
            OR IS_DIRECTORY "${template_path}")
        message(FATAL_ERROR
            "Renderer launcher package-fact template is unavailable: "
            "${template_path}")
    endif ()
    if (NOT IS_ABSOLUTE "${output_directory}")
        message(FATAL_ERROR
            "Renderer launcher generated include directory must be absolute: "
            "${output_directory}")
    endif ()
    if (NOT "${admit_ogre_next_demo}" STREQUAL "ON" AND
            NOT "${admit_ogre_next_demo}" STREQUAL "OFF")
        message(FATAL_ERROR
            "Renderer launcher OgreNext demo admission must be exactly ON or "
            "OFF, not '${admit_ogre_next_demo}'")
    endif ()

    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(ROR_RENDERER_LAUNCHER_PACKAGE_PLATFORM "MACOS")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(ROR_RENDERER_LAUNCHER_PACKAGE_PLATFORM "LINUX")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(ROR_RENDERER_LAUNCHER_PACKAGE_PLATFORM "WINDOWS")
    else ()
        message(FATAL_ERROR
            "The public renderer launcher has no package contract for "
            "CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'")
    endif ()

    # These are intentionally ordinary function-local values, not cache
    # settings or environment/config-file inputs. The generated header is the
    # package record for this bounded phase. Default intent is distinct from
    # admission: the public executable always requests OgreNext first, while
    # the explicit build-system argument controls this demo-only admission.
    set(ROR_RENDERER_LAUNCHER_DEFAULT_FRONTEND "OGRE_NEXT_PREFER")
    set(ROR_RENDERER_LAUNCHER_DEFAULT_DIRECTIONAL_SHADOWS "PSSM")
    set(ROR_RENDERER_LAUNCHER_OGRE14_CHILD_PRESENT "true")
    set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_CHILD_PRESENT "false")
    set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PRODUCTION_READY "false")
    set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PSSM_ADMITTED "false")
    set(ROR_RENDERER_LAUNCHER_NATIVE_SHADOW_BACKEND "NONE")
    if ("${admit_ogre_next_demo}" STREQUAL "ON")
        set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_CHILD_PRESENT "true")
        set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PRODUCTION_READY "true")
        set(ROR_RENDERER_LAUNCHER_OGRE_NEXT_PSSM_ADMITTED "true")
    endif ()

    file(MAKE_DIRECTORY "${output_directory}")
    set(_ror_renderer_launcher_generated_header
        "${output_directory}/RendererLauncherPackageConfig.generated.h")
    configure_file(
        "${template_path}"
        "${_ror_renderer_launcher_generated_header}"
        @ONLY)
    set(
        ${output_header_variable}
        "${_ror_renderer_launcher_generated_header}"
        PARENT_SCOPE)
endfunction()
