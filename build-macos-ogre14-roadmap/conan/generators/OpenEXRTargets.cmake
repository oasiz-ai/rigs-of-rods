# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/OpenEXR-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${openexr_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${OpenEXR_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET openexr::openexr)
    add_library(openexr::openexr INTERFACE IMPORTED)
    message(${OpenEXR_MESSAGE_MODE} "Conan: Target declared 'openexr::openexr'")
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/OpenEXR-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()