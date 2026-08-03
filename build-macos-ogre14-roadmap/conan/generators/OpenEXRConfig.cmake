########## MACROS ###########################################################################
#############################################################################################

# Requires CMake > 3.15
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeDeps' generator only works with CMake >= 3.15")
endif()

if(OpenEXR_FIND_QUIETLY)
    set(OpenEXR_MESSAGE_MODE VERBOSE)
else()
    set(OpenEXR_MESSAGE_MODE STATUS)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/OpenEXRTargets.cmake)
include(CMakeFindDependencyMacro)

check_build_type_defined()

foreach(_DEPENDENCY ${openexr_FIND_DEPENDENCY_NAMES} )
    # Check that we have not already called a find_package with the transitive dependency
    if(NOT ${_DEPENDENCY}_FOUND)
        find_dependency(${_DEPENDENCY} REQUIRED ${${_DEPENDENCY}_FIND_MODE})
    endif()
endforeach()

set(OpenEXR_VERSION_STRING "2.5.7")
set(OpenEXR_INCLUDE_DIRS ${openexr_INCLUDE_DIRS_RELEASE} )
set(OpenEXR_INCLUDE_DIR ${openexr_INCLUDE_DIRS_RELEASE} )
set(OpenEXR_LIBRARIES ${openexr_LIBRARIES_RELEASE} )
set(OpenEXR_DEFINITIONS ${openexr_DEFINITIONS_RELEASE} )


# Definition of extra CMake variables from cmake_extra_variables


# Only the last installed configuration BUILD_MODULES are included to avoid the collision
foreach(_BUILD_MODULE ${openexr_BUILD_MODULES_PATHS_RELEASE} )
    message(${OpenEXR_MESSAGE_MODE} "Conan: Including build module from '${_BUILD_MODULE}'")
    include(${_BUILD_MODULE})
endforeach()


