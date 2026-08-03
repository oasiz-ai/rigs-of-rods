# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/jxrlib-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${jxrlib_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${jxrlib_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET jxrlib::jxrlib)
    add_library(jxrlib::jxrlib INTERFACE IMPORTED)
    message(${jxrlib_MESSAGE_MODE} "Conan: Target declared 'jxrlib::jxrlib'")
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/jxrlib-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()