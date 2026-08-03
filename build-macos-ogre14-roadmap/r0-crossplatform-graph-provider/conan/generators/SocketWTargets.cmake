# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/SocketW-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${socketw_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${SocketW_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET SocketW::SocketW)
    add_library(SocketW::SocketW INTERFACE IMPORTED)
    message(${SocketW_MESSAGE_MODE} "Conan: Target declared 'SocketW::SocketW'")
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/SocketW-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()