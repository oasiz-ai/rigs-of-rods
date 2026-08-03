# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/OGRE-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${ogre3d_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${OGRE_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET OGRE::OGRE)
    add_library(OGRE::OGRE INTERFACE IMPORTED)
    message(${OGRE_MESSAGE_MODE} "Conan: Target declared 'OGRE::OGRE'")
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/OGRE-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()