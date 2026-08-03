# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/MyGUI-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${mygui_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${MyGUI_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET MyGUI::MyGUI)
    add_library(MyGUI::MyGUI INTERFACE IMPORTED)
    message(${MyGUI_MESSAGE_MODE} "Conan: Target declared 'MyGUI::MyGUI'")
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/MyGUI-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()