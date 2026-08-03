# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/discord-rpc-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${discord-rpc_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${discord-rpc_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET discord-rpc::discord-rpc)
    add_library(discord-rpc::discord-rpc INTERFACE IMPORTED)
    message(${discord-rpc_MESSAGE_MODE} "Conan: Target declared 'discord-rpc::discord-rpc'")
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/discord-rpc-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()