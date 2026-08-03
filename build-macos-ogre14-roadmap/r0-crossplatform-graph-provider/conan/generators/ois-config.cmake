########## MACROS ###########################################################################
#############################################################################################

# Requires CMake > 3.15
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeDeps' generator only works with CMake >= 3.15")
endif()

if(ois_FIND_QUIETLY)
    set(ois_MESSAGE_MODE VERBOSE)
else()
    set(ois_MESSAGE_MODE STATUS)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/oisTargets.cmake)
include(CMakeFindDependencyMacro)

check_build_type_defined()

foreach(_DEPENDENCY ${ois_FIND_DEPENDENCY_NAMES} )
    # Check that we have not already called a find_package with the transitive dependency
    if(NOT ${_DEPENDENCY}_FOUND)
        find_dependency(${_DEPENDENCY} REQUIRED ${${_DEPENDENCY}_FIND_MODE})
    endif()
endforeach()

set(ois_VERSION_STRING "1.5.1")
set(ois_INCLUDE_DIRS ${ois_INCLUDE_DIRS_RELEASE} )
set(ois_INCLUDE_DIR ${ois_INCLUDE_DIRS_RELEASE} )
set(ois_LIBRARIES ${ois_LIBRARIES_RELEASE} )
set(ois_DEFINITIONS ${ois_DEFINITIONS_RELEASE} )


# Definition of extra CMake variables from cmake_extra_variables


# Only the last installed configuration BUILD_MODULES are included to avoid the collision
foreach(_BUILD_MODULE ${ois_BUILD_MODULES_PATHS_RELEASE} )
    message(${ois_MESSAGE_MODE} "Conan: Including build module from '${_BUILD_MODULE}'")
    include(${_BUILD_MODULE})
endforeach()


