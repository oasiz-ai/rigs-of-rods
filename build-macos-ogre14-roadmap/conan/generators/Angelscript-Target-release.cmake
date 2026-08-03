# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(angelscript_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(angelscript_FRAMEWORKS_FOUND_RELEASE "${angelscript_FRAMEWORKS_RELEASE}" "${angelscript_FRAMEWORK_DIRS_RELEASE}")

set(angelscript_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET angelscript_DEPS_TARGET)
    add_library(angelscript_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET angelscript_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${angelscript_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${angelscript_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### angelscript_DEPS_TARGET to all of them
conan_package_library_targets("${angelscript_LIBS_RELEASE}"    # libraries
                              "${angelscript_LIB_DIRS_RELEASE}" # package_libdir
                              "${angelscript_BIN_DIRS_RELEASE}" # package_bindir
                              "${angelscript_LIBRARY_TYPE_RELEASE}"
                              "${angelscript_IS_HOST_WINDOWS_RELEASE}"
                              angelscript_DEPS_TARGET
                              angelscript_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "angelscript"    # package_name
                              "${angelscript_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${angelscript_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## COMPONENTS TARGET PROPERTIES Release ########################################

    ########## COMPONENT Angelscript::angelscript #############

        set(angelscript_Angelscript_angelscript_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(angelscript_Angelscript_angelscript_FRAMEWORKS_FOUND_RELEASE "${angelscript_Angelscript_angelscript_FRAMEWORKS_RELEASE}" "${angelscript_Angelscript_angelscript_FRAMEWORK_DIRS_RELEASE}")

        set(angelscript_Angelscript_angelscript_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET angelscript_Angelscript_angelscript_DEPS_TARGET)
            add_library(angelscript_Angelscript_angelscript_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET angelscript_Angelscript_angelscript_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'angelscript_Angelscript_angelscript_DEPS_TARGET' to all of them
        conan_package_library_targets("${angelscript_Angelscript_angelscript_LIBS_RELEASE}"
                              "${angelscript_Angelscript_angelscript_LIB_DIRS_RELEASE}"
                              "${angelscript_Angelscript_angelscript_BIN_DIRS_RELEASE}" # package_bindir
                              "${angelscript_Angelscript_angelscript_LIBRARY_TYPE_RELEASE}"
                              "${angelscript_Angelscript_angelscript_IS_HOST_WINDOWS_RELEASE}"
                              angelscript_Angelscript_angelscript_DEPS_TARGET
                              angelscript_Angelscript_angelscript_LIBRARIES_TARGETS
                              "_RELEASE"
                              "angelscript_Angelscript_angelscript"
                              "${angelscript_Angelscript_angelscript_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET Angelscript::angelscript
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_LIBRARIES_TARGETS}>
                     )

        if("${angelscript_Angelscript_angelscript_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET Angelscript::angelscript
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         angelscript_Angelscript_angelscript_DEPS_TARGET)
        endif()

        set_property(TARGET Angelscript::angelscript APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET Angelscript::angelscript APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET Angelscript::angelscript APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_LIB_DIRS_RELEASE}>)
        set_property(TARGET Angelscript::angelscript APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET Angelscript::angelscript APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${angelscript_Angelscript_angelscript_COMPILE_OPTIONS_RELEASE}>)


    ########## AGGREGATED GLOBAL TARGET WITH THE COMPONENTS #####################
    set_property(TARGET Angelscript::angelscript APPEND PROPERTY INTERFACE_LINK_LIBRARIES Angelscript::angelscript)

########## For the modules (FindXXX)
set(angelscript_LIBRARIES_RELEASE Angelscript::angelscript)
