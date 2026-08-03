# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/freeimage-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${freeimage_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${freeimage_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET freeimage::freeimage)
    add_library(freeimage::freeimage INTERFACE IMPORTED)
    message(${freeimage_MESSAGE_MODE} "Conan: Target declared 'freeimage::freeimage'")
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/freeimage-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()