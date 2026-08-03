########## MACROS ###########################################################################
#############################################################################################

# Requires CMake > 3.15
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeDeps' generator only works with CMake >= 3.15")
endif()

if(libraw_FIND_QUIETLY)
    set(libraw_MESSAGE_MODE VERBOSE)
else()
    set(libraw_MESSAGE_MODE STATUS)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/librawTargets.cmake)
include(CMakeFindDependencyMacro)

check_build_type_defined()

foreach(_DEPENDENCY ${libraw_FIND_DEPENDENCY_NAMES} )
    # Check that we have not already called a find_package with the transitive dependency
    if(NOT ${_DEPENDENCY}_FOUND)
        find_dependency(${_DEPENDENCY} REQUIRED ${${_DEPENDENCY}_FIND_MODE})
    endif()
endforeach()

set(libraw_VERSION_STRING "0.20.2")
set(libraw_INCLUDE_DIRS ${libraw_INCLUDE_DIRS_RELEASE} )
set(libraw_INCLUDE_DIR ${libraw_INCLUDE_DIRS_RELEASE} )
set(libraw_LIBRARIES ${libraw_LIBRARIES_RELEASE} )
set(libraw_DEFINITIONS ${libraw_DEFINITIONS_RELEASE} )


# Definition of extra CMake variables from cmake_extra_variables


# Only the last installed configuration BUILD_MODULES are included to avoid the collision
foreach(_BUILD_MODULE ${libraw_BUILD_MODULES_PATHS_RELEASE} )
    message(${libraw_MESSAGE_MODE} "Conan: Including build module from '${_BUILD_MODULE}'")
    include(${_BUILD_MODULE})
endforeach()


