# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(discord-rpc_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(discord-rpc_FRAMEWORKS_FOUND_RELEASE "${discord-rpc_FRAMEWORKS_RELEASE}" "${discord-rpc_FRAMEWORK_DIRS_RELEASE}")

set(discord-rpc_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET discord-rpc_DEPS_TARGET)
    add_library(discord-rpc_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET discord-rpc_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${discord-rpc_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${discord-rpc_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:rapidjson>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### discord-rpc_DEPS_TARGET to all of them
conan_package_library_targets("${discord-rpc_LIBS_RELEASE}"    # libraries
                              "${discord-rpc_LIB_DIRS_RELEASE}" # package_libdir
                              "${discord-rpc_BIN_DIRS_RELEASE}" # package_bindir
                              "${discord-rpc_LIBRARY_TYPE_RELEASE}"
                              "${discord-rpc_IS_HOST_WINDOWS_RELEASE}"
                              discord-rpc_DEPS_TARGET
                              discord-rpc_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "discord-rpc"    # package_name
                              "${discord-rpc_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${discord-rpc_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## GLOBAL TARGET PROPERTIES Release ########################################
    set_property(TARGET discord-rpc::discord-rpc
                 APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                 $<$<CONFIG:Release>:${discord-rpc_OBJECTS_RELEASE}>
                 $<$<CONFIG:Release>:${discord-rpc_LIBRARIES_TARGETS}>
                 )

    if("${discord-rpc_LIBS_RELEASE}" STREQUAL "")
        # If the package is not declaring any "cpp_info.libs" the package deps, system libs,
        # frameworks etc are not linked to the imported targets and we need to do it to the
        # global target
        set_property(TARGET discord-rpc::discord-rpc
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     discord-rpc_DEPS_TARGET)
    endif()

    set_property(TARGET discord-rpc::discord-rpc
                 APPEND PROPERTY INTERFACE_LINK_OPTIONS
                 $<$<CONFIG:Release>:${discord-rpc_LINKER_FLAGS_RELEASE}>)
    set_property(TARGET discord-rpc::discord-rpc
                 APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                 $<$<CONFIG:Release>:${discord-rpc_INCLUDE_DIRS_RELEASE}>)
    # Necessary to find LINK shared libraries in Linux
    set_property(TARGET discord-rpc::discord-rpc
                 APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                 $<$<CONFIG:Release>:${discord-rpc_LIB_DIRS_RELEASE}>)
    set_property(TARGET discord-rpc::discord-rpc
                 APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                 $<$<CONFIG:Release>:${discord-rpc_COMPILE_DEFINITIONS_RELEASE}>)
    set_property(TARGET discord-rpc::discord-rpc
                 APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                 $<$<CONFIG:Release>:${discord-rpc_COMPILE_OPTIONS_RELEASE}>)

########## For the modules (FindXXX)
set(discord-rpc_LIBRARIES_RELEASE discord-rpc::discord-rpc)
