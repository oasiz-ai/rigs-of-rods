########## MACROS ###########################################################################
#############################################################################################

# Requires CMake > 3.15
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeDeps' generator only works with CMake >= 3.15")
endif()

if(OGRE_FIND_QUIETLY)
    set(OGRE_MESSAGE_MODE VERBOSE)
else()
    set(OGRE_MESSAGE_MODE STATUS)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/OGRETargets.cmake)
include(CMakeFindDependencyMacro)

check_build_type_defined()

foreach(_DEPENDENCY ${ogre3d_FIND_DEPENDENCY_NAMES} )
    # Check that we have not already called a find_package with the transitive dependency
    if(NOT ${_DEPENDENCY}_FOUND)
        find_dependency(${_DEPENDENCY} REQUIRED ${${_DEPENDENCY}_FIND_MODE})
    endif()
endforeach()

set(OGRE_VERSION_STRING "14.5.2")
set(OGRE_INCLUDE_DIRS ${ogre3d_INCLUDE_DIRS_RELEASE} )
set(OGRE_INCLUDE_DIR ${ogre3d_INCLUDE_DIRS_RELEASE} )
set(OGRE_LIBRARIES ${ogre3d_LIBRARIES_RELEASE} )
set(OGRE_DEFINITIONS ${ogre3d_DEFINITIONS_RELEASE} )


# Definition of extra CMake variables from cmake_extra_variables


# Only the last installed configuration BUILD_MODULES are included to avoid the collision
foreach(_BUILD_MODULE ${ogre3d_BUILD_MODULES_PATHS_RELEASE} )
    message(${OGRE_MESSAGE_MODE} "Conan: Including build module from '${_BUILD_MODULE}'")
    include(${_BUILD_MODULE})
endforeach()


