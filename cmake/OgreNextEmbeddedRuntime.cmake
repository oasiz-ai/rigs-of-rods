# Root-project entry point for the one-process, namespaced OgreNext runtime.
# The implementation lives in its own CMake directory so upstream's forced
# static ABI settings and namespace force-include never leak into RoR/OGRE14.

include_guard(GLOBAL)

function(ror_add_ogre_next_embedded_runtime)
    if (NOT ROR_OGRE_NEXT_COMBINED_RUNTIME)
        message(FATAL_ERROR
            "ror_add_ogre_next_embedded_runtime requires the explicit combined-runtime option")
    endif ()
    if (TARGET ror_ogre_next_embedded_runtime)
        message(FATAL_ERROR "The embedded OgreNext runtime was added twice")
    endif ()
    if (NOT TARGET SDL2::SDL2)
        message(FATAL_ERROR
            "DependenciesConfig must create SDL2::SDL2 before the embedded runtime")
    endif ()

    set(ROR_OGRE_NEXT_EMBEDDED_ROOT_PROVIDER ON)

    add_subdirectory(
        "${CMAKE_SOURCE_DIR}/cmake/ogre_next_embedded"
        "${CMAKE_BINARY_DIR}/ogre-next-embedded")

    foreach (_ror_required_target IN ITEMS
            ror_render_payload_digest
            ror_ogre_next_embedded_direct_contract
            ror_ogre_next_embedded_n1_runtime
            ror_ogre_next_embedded_sdl_window_runtime
            ror_ogre_next_embedded_game_input_target
            ror_ogre_next_in_process_presenter
            ror_ogre_next_embedded_runtime
            ror_ogre_next_combined_resources
            ror_ogre_next_combined_binary_receipt_invalidate
            ror_ogre_next_root_namespace_audit
            ror_ogre_next_embedded_provider_contract)
        if (NOT TARGET ${_ror_required_target})
            message(FATAL_ERROR
                "Embedded OgreNext provider omitted ${_ror_required_target}")
        endif ()
    endforeach ()
endfunction()
