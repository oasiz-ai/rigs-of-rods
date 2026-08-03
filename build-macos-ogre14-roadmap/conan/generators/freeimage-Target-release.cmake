# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(freeimage_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(freeimage_FRAMEWORKS_FOUND_RELEASE "${freeimage_FRAMEWORKS_RELEASE}" "${freeimage_FRAMEWORK_DIRS_RELEASE}")

set(freeimage_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET freeimage_DEPS_TARGET)
    add_library(freeimage_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET freeimage_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${freeimage_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${freeimage_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:ZLIB::ZLIB;JPEG::JPEG;openjp2;PNG::PNG;libwebp::libwebp;openexr::openexr;libraw::libraw;jxrlib::jxrlib;TIFF::TIFF;freeimage::FreeImage>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### freeimage_DEPS_TARGET to all of them
conan_package_library_targets("${freeimage_LIBS_RELEASE}"    # libraries
                              "${freeimage_LIB_DIRS_RELEASE}" # package_libdir
                              "${freeimage_BIN_DIRS_RELEASE}" # package_bindir
                              "${freeimage_LIBRARY_TYPE_RELEASE}"
                              "${freeimage_IS_HOST_WINDOWS_RELEASE}"
                              freeimage_DEPS_TARGET
                              freeimage_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "freeimage"    # package_name
                              "${freeimage_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${freeimage_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## COMPONENTS TARGET PROPERTIES Release ########################################

    ########## COMPONENT freeimage::FreeImagePlus #############

        set(freeimage_freeimage_FreeImagePlus_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(freeimage_freeimage_FreeImagePlus_FRAMEWORKS_FOUND_RELEASE "${freeimage_freeimage_FreeImagePlus_FRAMEWORKS_RELEASE}" "${freeimage_freeimage_FreeImagePlus_FRAMEWORK_DIRS_RELEASE}")

        set(freeimage_freeimage_FreeImagePlus_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET freeimage_freeimage_FreeImagePlus_DEPS_TARGET)
            add_library(freeimage_freeimage_FreeImagePlus_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET freeimage_freeimage_FreeImagePlus_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'freeimage_freeimage_FreeImagePlus_DEPS_TARGET' to all of them
        conan_package_library_targets("${freeimage_freeimage_FreeImagePlus_LIBS_RELEASE}"
                              "${freeimage_freeimage_FreeImagePlus_LIB_DIRS_RELEASE}"
                              "${freeimage_freeimage_FreeImagePlus_BIN_DIRS_RELEASE}" # package_bindir
                              "${freeimage_freeimage_FreeImagePlus_LIBRARY_TYPE_RELEASE}"
                              "${freeimage_freeimage_FreeImagePlus_IS_HOST_WINDOWS_RELEASE}"
                              freeimage_freeimage_FreeImagePlus_DEPS_TARGET
                              freeimage_freeimage_FreeImagePlus_LIBRARIES_TARGETS
                              "_RELEASE"
                              "freeimage_freeimage_FreeImagePlus"
                              "${freeimage_freeimage_FreeImagePlus_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET freeimage::FreeImagePlus
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_LIBRARIES_TARGETS}>
                     )

        if("${freeimage_freeimage_FreeImagePlus_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET freeimage::FreeImagePlus
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         freeimage_freeimage_FreeImagePlus_DEPS_TARGET)
        endif()

        set_property(TARGET freeimage::FreeImagePlus APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET freeimage::FreeImagePlus APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET freeimage::FreeImagePlus APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_LIB_DIRS_RELEASE}>)
        set_property(TARGET freeimage::FreeImagePlus APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET freeimage::FreeImagePlus APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImagePlus_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT freeimage::FreeImage #############

        set(freeimage_freeimage_FreeImage_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(freeimage_freeimage_FreeImage_FRAMEWORKS_FOUND_RELEASE "${freeimage_freeimage_FreeImage_FRAMEWORKS_RELEASE}" "${freeimage_freeimage_FreeImage_FRAMEWORK_DIRS_RELEASE}")

        set(freeimage_freeimage_FreeImage_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET freeimage_freeimage_FreeImage_DEPS_TARGET)
            add_library(freeimage_freeimage_FreeImage_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET freeimage_freeimage_FreeImage_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'freeimage_freeimage_FreeImage_DEPS_TARGET' to all of them
        conan_package_library_targets("${freeimage_freeimage_FreeImage_LIBS_RELEASE}"
                              "${freeimage_freeimage_FreeImage_LIB_DIRS_RELEASE}"
                              "${freeimage_freeimage_FreeImage_BIN_DIRS_RELEASE}" # package_bindir
                              "${freeimage_freeimage_FreeImage_LIBRARY_TYPE_RELEASE}"
                              "${freeimage_freeimage_FreeImage_IS_HOST_WINDOWS_RELEASE}"
                              freeimage_freeimage_FreeImage_DEPS_TARGET
                              freeimage_freeimage_FreeImage_LIBRARIES_TARGETS
                              "_RELEASE"
                              "freeimage_freeimage_FreeImage"
                              "${freeimage_freeimage_FreeImage_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET freeimage::FreeImage
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_LIBRARIES_TARGETS}>
                     )

        if("${freeimage_freeimage_FreeImage_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET freeimage::FreeImage
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         freeimage_freeimage_FreeImage_DEPS_TARGET)
        endif()

        set_property(TARGET freeimage::FreeImage APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET freeimage::FreeImage APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET freeimage::FreeImage APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_LIB_DIRS_RELEASE}>)
        set_property(TARGET freeimage::FreeImage APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET freeimage::FreeImage APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${freeimage_freeimage_FreeImage_COMPILE_OPTIONS_RELEASE}>)


    ########## AGGREGATED GLOBAL TARGET WITH THE COMPONENTS #####################
    set_property(TARGET freeimage::freeimage APPEND PROPERTY INTERFACE_LINK_LIBRARIES freeimage::FreeImagePlus)
    set_property(TARGET freeimage::freeimage APPEND PROPERTY INTERFACE_LINK_LIBRARIES freeimage::FreeImage)

########## For the modules (FindXXX)
set(freeimage_LIBRARIES_RELEASE freeimage::freeimage)
