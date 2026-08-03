########## MACROS ###########################################################################
#############################################################################################

# Requires CMake > 3.15
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeDeps' generator only works with CMake >= 3.15")
endif()

if(MyGUI_FIND_QUIETLY)
    set(MyGUI_MESSAGE_MODE VERBOSE)
else()
    set(MyGUI_MESSAGE_MODE STATUS)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/MyGUITargets.cmake)
include(CMakeFindDependencyMacro)

check_build_type_defined()

foreach(_DEPENDENCY ${mygui_FIND_DEPENDENCY_NAMES} )
    # Check that we have not already called a find_package with the transitive dependency
    if(NOT ${_DEPENDENCY}_FOUND)
        find_dependency(${_DEPENDENCY} REQUIRED ${${_DEPENDENCY}_FIND_MODE})
    endif()
endforeach()

set(MyGUI_VERSION_STRING "3.4.0")
set(MyGUI_INCLUDE_DIRS ${mygui_INCLUDE_DIRS_RELEASE} )
set(MyGUI_INCLUDE_DIR ${mygui_INCLUDE_DIRS_RELEASE} )
set(MyGUI_LIBRARIES ${mygui_LIBRARIES_RELEASE} )
set(MyGUI_DEFINITIONS ${mygui_DEFINITIONS_RELEASE} )


# Definition of extra CMake variables from cmake_extra_variables


# Only the last installed configuration BUILD_MODULES are included to avoid the collision
foreach(_BUILD_MODULE ${mygui_BUILD_MODULES_PATHS_RELEASE} )
    message(${MyGUI_MESSAGE_MODE} "Conan: Including build module from '${_BUILD_MODULE}'")
    include(${_BUILD_MODULE})
endforeach()


