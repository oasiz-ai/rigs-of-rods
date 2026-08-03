########## MACROS ###########################################################################
#############################################################################################

# Requires CMake > 3.15
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeDeps' generator only works with CMake >= 3.15")
endif()

if(freeimage_FIND_QUIETLY)
    set(freeimage_MESSAGE_MODE VERBOSE)
else()
    set(freeimage_MESSAGE_MODE STATUS)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/freeimageTargets.cmake)
include(CMakeFindDependencyMacro)

check_build_type_defined()

foreach(_DEPENDENCY ${freeimage_FIND_DEPENDENCY_NAMES} )
    # Check that we have not already called a find_package with the transitive dependency
    if(NOT ${_DEPENDENCY}_FOUND)
        find_dependency(${_DEPENDENCY} REQUIRED ${${_DEPENDENCY}_FIND_MODE})
    endif()
endforeach()

set(freeimage_VERSION_STRING "3.18.0")
set(freeimage_INCLUDE_DIRS ${freeimage_INCLUDE_DIRS_RELEASE} )
set(freeimage_INCLUDE_DIR ${freeimage_INCLUDE_DIRS_RELEASE} )
set(freeimage_LIBRARIES ${freeimage_LIBRARIES_RELEASE} )
set(freeimage_DEFINITIONS ${freeimage_DEFINITIONS_RELEASE} )


# Definition of extra CMake variables from cmake_extra_variables


# Only the last installed configuration BUILD_MODULES are included to avoid the collision
foreach(_BUILD_MODULE ${freeimage_BUILD_MODULES_PATHS_RELEASE} )
    message(${freeimage_MESSAGE_MODE} "Conan: Including build module from '${_BUILD_MODULE}'")
    include(${_BUILD_MODULE})
endforeach()


