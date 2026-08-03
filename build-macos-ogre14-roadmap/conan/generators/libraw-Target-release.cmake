# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(libraw_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(libraw_FRAMEWORKS_FOUND_RELEASE "${libraw_FRAMEWORKS_RELEASE}" "${libraw_FRAMEWORK_DIRS_RELEASE}")

set(libraw_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET libraw_DEPS_TARGET)
    add_library(libraw_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET libraw_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${libraw_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${libraw_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:JPEG::JPEG;lcms::lcms;Jasper::Jasper>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### libraw_DEPS_TARGET to all of them
conan_package_library_targets("${libraw_LIBS_RELEASE}"    # libraries
                              "${libraw_LIB_DIRS_RELEASE}" # package_libdir
                              "${libraw_BIN_DIRS_RELEASE}" # package_bindir
                              "${libraw_LIBRARY_TYPE_RELEASE}"
                              "${libraw_IS_HOST_WINDOWS_RELEASE}"
                              libraw_DEPS_TARGET
                              libraw_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "libraw"    # package_name
                              "${libraw_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${libraw_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## COMPONENTS TARGET PROPERTIES Release ########################################

    ########## COMPONENT libraw::libraw_ #############

        set(libraw_libraw_libraw__FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(libraw_libraw_libraw__FRAMEWORKS_FOUND_RELEASE "${libraw_libraw_libraw__FRAMEWORKS_RELEASE}" "${libraw_libraw_libraw__FRAMEWORK_DIRS_RELEASE}")

        set(libraw_libraw_libraw__LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET libraw_libraw_libraw__DEPS_TARGET)
            add_library(libraw_libraw_libraw__DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET libraw_libraw_libraw__DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'libraw_libraw_libraw__DEPS_TARGET' to all of them
        conan_package_library_targets("${libraw_libraw_libraw__LIBS_RELEASE}"
                              "${libraw_libraw_libraw__LIB_DIRS_RELEASE}"
                              "${libraw_libraw_libraw__BIN_DIRS_RELEASE}" # package_bindir
                              "${libraw_libraw_libraw__LIBRARY_TYPE_RELEASE}"
                              "${libraw_libraw_libraw__IS_HOST_WINDOWS_RELEASE}"
                              libraw_libraw_libraw__DEPS_TARGET
                              libraw_libraw_libraw__LIBRARIES_TARGETS
                              "_RELEASE"
                              "libraw_libraw_libraw_"
                              "${libraw_libraw_libraw__NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET libraw::libraw_
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__LIBRARIES_TARGETS}>
                     )

        if("${libraw_libraw_libraw__LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET libraw::libraw_
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         libraw_libraw_libraw__DEPS_TARGET)
        endif()

        set_property(TARGET libraw::libraw_ APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__LINKER_FLAGS_RELEASE}>)
        set_property(TARGET libraw::libraw_ APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET libraw::libraw_ APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__LIB_DIRS_RELEASE}>)
        set_property(TARGET libraw::libraw_ APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET libraw::libraw_ APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${libraw_libraw_libraw__COMPILE_OPTIONS_RELEASE}>)


    ########## AGGREGATED GLOBAL TARGET WITH THE COMPONENTS #####################
    set_property(TARGET libraw::libraw APPEND PROPERTY INTERFACE_LINK_LIBRARIES libraw::libraw_)

########## For the modules (FindXXX)
set(libraw_LIBRARIES_RELEASE libraw::libraw)
