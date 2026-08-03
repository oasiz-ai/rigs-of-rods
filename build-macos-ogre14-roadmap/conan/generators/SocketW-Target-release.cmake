# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(socketw_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(socketw_FRAMEWORKS_FOUND_RELEASE "${socketw_FRAMEWORKS_RELEASE}" "${socketw_FRAMEWORK_DIRS_RELEASE}")

set(socketw_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET socketw_DEPS_TARGET)
    add_library(socketw_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET socketw_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${socketw_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${socketw_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:openssl::openssl>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### socketw_DEPS_TARGET to all of them
conan_package_library_targets("${socketw_LIBS_RELEASE}"    # libraries
                              "${socketw_LIB_DIRS_RELEASE}" # package_libdir
                              "${socketw_BIN_DIRS_RELEASE}" # package_bindir
                              "${socketw_LIBRARY_TYPE_RELEASE}"
                              "${socketw_IS_HOST_WINDOWS_RELEASE}"
                              socketw_DEPS_TARGET
                              socketw_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "socketw"    # package_name
                              "${socketw_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${socketw_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## GLOBAL TARGET PROPERTIES Release ########################################
    set_property(TARGET SocketW::SocketW
                 APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                 $<$<CONFIG:Release>:${socketw_OBJECTS_RELEASE}>
                 $<$<CONFIG:Release>:${socketw_LIBRARIES_TARGETS}>
                 )

    if("${socketw_LIBS_RELEASE}" STREQUAL "")
        # If the package is not declaring any "cpp_info.libs" the package deps, system libs,
        # frameworks etc are not linked to the imported targets and we need to do it to the
        # global target
        set_property(TARGET SocketW::SocketW
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     socketw_DEPS_TARGET)
    endif()

    set_property(TARGET SocketW::SocketW
                 APPEND PROPERTY INTERFACE_LINK_OPTIONS
                 $<$<CONFIG:Release>:${socketw_LINKER_FLAGS_RELEASE}>)
    set_property(TARGET SocketW::SocketW
                 APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                 $<$<CONFIG:Release>:${socketw_INCLUDE_DIRS_RELEASE}>)
    # Necessary to find LINK shared libraries in Linux
    set_property(TARGET SocketW::SocketW
                 APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                 $<$<CONFIG:Release>:${socketw_LIB_DIRS_RELEASE}>)
    set_property(TARGET SocketW::SocketW
                 APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                 $<$<CONFIG:Release>:${socketw_COMPILE_DEFINITIONS_RELEASE}>)
    set_property(TARGET SocketW::SocketW
                 APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                 $<$<CONFIG:Release>:${socketw_COMPILE_OPTIONS_RELEASE}>)

########## For the modules (FindXXX)
set(socketw_LIBRARIES_RELEASE SocketW::SocketW)
