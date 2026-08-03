# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(openexr_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(openexr_FRAMEWORKS_FOUND_RELEASE "${openexr_FRAMEWORKS_RELEASE}" "${openexr_FRAMEWORK_DIRS_RELEASE}")

set(openexr_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET openexr_DEPS_TARGET)
    add_library(openexr_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET openexr_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${openexr_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${openexr_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:OpenEXR::IlmImfConfig;IlmBase::Iex;IlmBase::Half;IlmBase::IMath;IlmBase::IlmThread;ZLIB::ZLIB;OpenEXR::IlmImf;IlmBase::IlmBaseConfig;IlmBase::IexMath>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### openexr_DEPS_TARGET to all of them
conan_package_library_targets("${openexr_LIBS_RELEASE}"    # libraries
                              "${openexr_LIB_DIRS_RELEASE}" # package_libdir
                              "${openexr_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_LIBRARY_TYPE_RELEASE}"
                              "${openexr_IS_HOST_WINDOWS_RELEASE}"
                              openexr_DEPS_TARGET
                              openexr_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "openexr"    # package_name
                              "${openexr_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${openexr_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## COMPONENTS TARGET PROPERTIES Release ########################################

    ########## COMPONENT OpenEXR::IlmImfUtil #############

        set(openexr_OpenEXR_IlmImfUtil_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_OpenEXR_IlmImfUtil_FRAMEWORKS_FOUND_RELEASE "${openexr_OpenEXR_IlmImfUtil_FRAMEWORKS_RELEASE}" "${openexr_OpenEXR_IlmImfUtil_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_OpenEXR_IlmImfUtil_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_OpenEXR_IlmImfUtil_DEPS_TARGET)
            add_library(openexr_OpenEXR_IlmImfUtil_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_OpenEXR_IlmImfUtil_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_OpenEXR_IlmImfUtil_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_OpenEXR_IlmImfUtil_LIBS_RELEASE}"
                              "${openexr_OpenEXR_IlmImfUtil_LIB_DIRS_RELEASE}"
                              "${openexr_OpenEXR_IlmImfUtil_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_OpenEXR_IlmImfUtil_LIBRARY_TYPE_RELEASE}"
                              "${openexr_OpenEXR_IlmImfUtil_IS_HOST_WINDOWS_RELEASE}"
                              openexr_OpenEXR_IlmImfUtil_DEPS_TARGET
                              openexr_OpenEXR_IlmImfUtil_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_OpenEXR_IlmImfUtil"
                              "${openexr_OpenEXR_IlmImfUtil_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OpenEXR::IlmImfUtil
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_LIBRARIES_TARGETS}>
                     )

        if("${openexr_OpenEXR_IlmImfUtil_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OpenEXR::IlmImfUtil
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_OpenEXR_IlmImfUtil_DEPS_TARGET)
        endif()

        set_property(TARGET OpenEXR::IlmImfUtil APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImfUtil APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImfUtil APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_LIB_DIRS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImfUtil APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImfUtil APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfUtil_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT openexr::ilmbase_conan_pkgconfig #############

        set(openexr_openexr_ilmbase_conan_pkgconfig_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_openexr_ilmbase_conan_pkgconfig_FRAMEWORKS_FOUND_RELEASE "${openexr_openexr_ilmbase_conan_pkgconfig_FRAMEWORKS_RELEASE}" "${openexr_openexr_ilmbase_conan_pkgconfig_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_openexr_ilmbase_conan_pkgconfig_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_openexr_ilmbase_conan_pkgconfig_DEPS_TARGET)
            add_library(openexr_openexr_ilmbase_conan_pkgconfig_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_openexr_ilmbase_conan_pkgconfig_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_openexr_ilmbase_conan_pkgconfig_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_openexr_ilmbase_conan_pkgconfig_LIBS_RELEASE}"
                              "${openexr_openexr_ilmbase_conan_pkgconfig_LIB_DIRS_RELEASE}"
                              "${openexr_openexr_ilmbase_conan_pkgconfig_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_openexr_ilmbase_conan_pkgconfig_LIBRARY_TYPE_RELEASE}"
                              "${openexr_openexr_ilmbase_conan_pkgconfig_IS_HOST_WINDOWS_RELEASE}"
                              openexr_openexr_ilmbase_conan_pkgconfig_DEPS_TARGET
                              openexr_openexr_ilmbase_conan_pkgconfig_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_openexr_ilmbase_conan_pkgconfig"
                              "${openexr_openexr_ilmbase_conan_pkgconfig_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET openexr::ilmbase_conan_pkgconfig
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_LIBRARIES_TARGETS}>
                     )

        if("${openexr_openexr_ilmbase_conan_pkgconfig_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET openexr::ilmbase_conan_pkgconfig
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_openexr_ilmbase_conan_pkgconfig_DEPS_TARGET)
        endif()

        set_property(TARGET openexr::ilmbase_conan_pkgconfig APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET openexr::ilmbase_conan_pkgconfig APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET openexr::ilmbase_conan_pkgconfig APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_LIB_DIRS_RELEASE}>)
        set_property(TARGET openexr::ilmbase_conan_pkgconfig APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET openexr::ilmbase_conan_pkgconfig APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_openexr_ilmbase_conan_pkgconfig_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OpenEXR::IlmImf #############

        set(openexr_OpenEXR_IlmImf_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_OpenEXR_IlmImf_FRAMEWORKS_FOUND_RELEASE "${openexr_OpenEXR_IlmImf_FRAMEWORKS_RELEASE}" "${openexr_OpenEXR_IlmImf_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_OpenEXR_IlmImf_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_OpenEXR_IlmImf_DEPS_TARGET)
            add_library(openexr_OpenEXR_IlmImf_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_OpenEXR_IlmImf_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_OpenEXR_IlmImf_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_OpenEXR_IlmImf_LIBS_RELEASE}"
                              "${openexr_OpenEXR_IlmImf_LIB_DIRS_RELEASE}"
                              "${openexr_OpenEXR_IlmImf_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_OpenEXR_IlmImf_LIBRARY_TYPE_RELEASE}"
                              "${openexr_OpenEXR_IlmImf_IS_HOST_WINDOWS_RELEASE}"
                              openexr_OpenEXR_IlmImf_DEPS_TARGET
                              openexr_OpenEXR_IlmImf_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_OpenEXR_IlmImf"
                              "${openexr_OpenEXR_IlmImf_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OpenEXR::IlmImf
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_LIBRARIES_TARGETS}>
                     )

        if("${openexr_OpenEXR_IlmImf_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OpenEXR::IlmImf
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_OpenEXR_IlmImf_DEPS_TARGET)
        endif()

        set_property(TARGET OpenEXR::IlmImf APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImf APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImf APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_LIB_DIRS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImf APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImf APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImf_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT IlmBase::IMath #############

        set(openexr_IlmBase_IMath_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_IlmBase_IMath_FRAMEWORKS_FOUND_RELEASE "${openexr_IlmBase_IMath_FRAMEWORKS_RELEASE}" "${openexr_IlmBase_IMath_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_IlmBase_IMath_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_IlmBase_IMath_DEPS_TARGET)
            add_library(openexr_IlmBase_IMath_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_IlmBase_IMath_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_IlmBase_IMath_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_IlmBase_IMath_LIBS_RELEASE}"
                              "${openexr_IlmBase_IMath_LIB_DIRS_RELEASE}"
                              "${openexr_IlmBase_IMath_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_IlmBase_IMath_LIBRARY_TYPE_RELEASE}"
                              "${openexr_IlmBase_IMath_IS_HOST_WINDOWS_RELEASE}"
                              openexr_IlmBase_IMath_DEPS_TARGET
                              openexr_IlmBase_IMath_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_IlmBase_IMath"
                              "${openexr_IlmBase_IMath_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET IlmBase::IMath
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_LIBRARIES_TARGETS}>
                     )

        if("${openexr_IlmBase_IMath_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET IlmBase::IMath
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_IlmBase_IMath_DEPS_TARGET)
        endif()

        set_property(TARGET IlmBase::IMath APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET IlmBase::IMath APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::IMath APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_LIB_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::IMath APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET IlmBase::IMath APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IMath_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT IlmBase::IlmThread #############

        set(openexr_IlmBase_IlmThread_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_IlmBase_IlmThread_FRAMEWORKS_FOUND_RELEASE "${openexr_IlmBase_IlmThread_FRAMEWORKS_RELEASE}" "${openexr_IlmBase_IlmThread_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_IlmBase_IlmThread_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_IlmBase_IlmThread_DEPS_TARGET)
            add_library(openexr_IlmBase_IlmThread_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_IlmBase_IlmThread_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_IlmBase_IlmThread_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_IlmBase_IlmThread_LIBS_RELEASE}"
                              "${openexr_IlmBase_IlmThread_LIB_DIRS_RELEASE}"
                              "${openexr_IlmBase_IlmThread_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_IlmBase_IlmThread_LIBRARY_TYPE_RELEASE}"
                              "${openexr_IlmBase_IlmThread_IS_HOST_WINDOWS_RELEASE}"
                              openexr_IlmBase_IlmThread_DEPS_TARGET
                              openexr_IlmBase_IlmThread_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_IlmBase_IlmThread"
                              "${openexr_IlmBase_IlmThread_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET IlmBase::IlmThread
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_LIBRARIES_TARGETS}>
                     )

        if("${openexr_IlmBase_IlmThread_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET IlmBase::IlmThread
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_IlmBase_IlmThread_DEPS_TARGET)
        endif()

        set_property(TARGET IlmBase::IlmThread APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET IlmBase::IlmThread APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::IlmThread APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_LIB_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::IlmThread APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET IlmBase::IlmThread APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmThread_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT IlmBase::IexMath #############

        set(openexr_IlmBase_IexMath_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_IlmBase_IexMath_FRAMEWORKS_FOUND_RELEASE "${openexr_IlmBase_IexMath_FRAMEWORKS_RELEASE}" "${openexr_IlmBase_IexMath_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_IlmBase_IexMath_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_IlmBase_IexMath_DEPS_TARGET)
            add_library(openexr_IlmBase_IexMath_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_IlmBase_IexMath_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_IlmBase_IexMath_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_IlmBase_IexMath_LIBS_RELEASE}"
                              "${openexr_IlmBase_IexMath_LIB_DIRS_RELEASE}"
                              "${openexr_IlmBase_IexMath_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_IlmBase_IexMath_LIBRARY_TYPE_RELEASE}"
                              "${openexr_IlmBase_IexMath_IS_HOST_WINDOWS_RELEASE}"
                              openexr_IlmBase_IexMath_DEPS_TARGET
                              openexr_IlmBase_IexMath_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_IlmBase_IexMath"
                              "${openexr_IlmBase_IexMath_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET IlmBase::IexMath
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_LIBRARIES_TARGETS}>
                     )

        if("${openexr_IlmBase_IexMath_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET IlmBase::IexMath
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_IlmBase_IexMath_DEPS_TARGET)
        endif()

        set_property(TARGET IlmBase::IexMath APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET IlmBase::IexMath APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::IexMath APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_LIB_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::IexMath APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET IlmBase::IexMath APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IexMath_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT IlmBase::Iex #############

        set(openexr_IlmBase_Iex_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_IlmBase_Iex_FRAMEWORKS_FOUND_RELEASE "${openexr_IlmBase_Iex_FRAMEWORKS_RELEASE}" "${openexr_IlmBase_Iex_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_IlmBase_Iex_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_IlmBase_Iex_DEPS_TARGET)
            add_library(openexr_IlmBase_Iex_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_IlmBase_Iex_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_IlmBase_Iex_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_IlmBase_Iex_LIBS_RELEASE}"
                              "${openexr_IlmBase_Iex_LIB_DIRS_RELEASE}"
                              "${openexr_IlmBase_Iex_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_IlmBase_Iex_LIBRARY_TYPE_RELEASE}"
                              "${openexr_IlmBase_Iex_IS_HOST_WINDOWS_RELEASE}"
                              openexr_IlmBase_Iex_DEPS_TARGET
                              openexr_IlmBase_Iex_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_IlmBase_Iex"
                              "${openexr_IlmBase_Iex_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET IlmBase::Iex
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_LIBRARIES_TARGETS}>
                     )

        if("${openexr_IlmBase_Iex_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET IlmBase::Iex
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_IlmBase_Iex_DEPS_TARGET)
        endif()

        set_property(TARGET IlmBase::Iex APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET IlmBase::Iex APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::Iex APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_LIB_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::Iex APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET IlmBase::Iex APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_Iex_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT IlmBase::Half #############

        set(openexr_IlmBase_Half_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_IlmBase_Half_FRAMEWORKS_FOUND_RELEASE "${openexr_IlmBase_Half_FRAMEWORKS_RELEASE}" "${openexr_IlmBase_Half_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_IlmBase_Half_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_IlmBase_Half_DEPS_TARGET)
            add_library(openexr_IlmBase_Half_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_IlmBase_Half_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_IlmBase_Half_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_IlmBase_Half_LIBS_RELEASE}"
                              "${openexr_IlmBase_Half_LIB_DIRS_RELEASE}"
                              "${openexr_IlmBase_Half_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_IlmBase_Half_LIBRARY_TYPE_RELEASE}"
                              "${openexr_IlmBase_Half_IS_HOST_WINDOWS_RELEASE}"
                              openexr_IlmBase_Half_DEPS_TARGET
                              openexr_IlmBase_Half_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_IlmBase_Half"
                              "${openexr_IlmBase_Half_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET IlmBase::Half
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_LIBRARIES_TARGETS}>
                     )

        if("${openexr_IlmBase_Half_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET IlmBase::Half
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_IlmBase_Half_DEPS_TARGET)
        endif()

        set_property(TARGET IlmBase::Half APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET IlmBase::Half APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::Half APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_LIB_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::Half APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET IlmBase::Half APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_Half_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT IlmBase::IlmBaseConfig #############

        set(openexr_IlmBase_IlmBaseConfig_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_IlmBase_IlmBaseConfig_FRAMEWORKS_FOUND_RELEASE "${openexr_IlmBase_IlmBaseConfig_FRAMEWORKS_RELEASE}" "${openexr_IlmBase_IlmBaseConfig_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_IlmBase_IlmBaseConfig_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_IlmBase_IlmBaseConfig_DEPS_TARGET)
            add_library(openexr_IlmBase_IlmBaseConfig_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_IlmBase_IlmBaseConfig_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_IlmBase_IlmBaseConfig_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_IlmBase_IlmBaseConfig_LIBS_RELEASE}"
                              "${openexr_IlmBase_IlmBaseConfig_LIB_DIRS_RELEASE}"
                              "${openexr_IlmBase_IlmBaseConfig_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_IlmBase_IlmBaseConfig_LIBRARY_TYPE_RELEASE}"
                              "${openexr_IlmBase_IlmBaseConfig_IS_HOST_WINDOWS_RELEASE}"
                              openexr_IlmBase_IlmBaseConfig_DEPS_TARGET
                              openexr_IlmBase_IlmBaseConfig_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_IlmBase_IlmBaseConfig"
                              "${openexr_IlmBase_IlmBaseConfig_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET IlmBase::IlmBaseConfig
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_LIBRARIES_TARGETS}>
                     )

        if("${openexr_IlmBase_IlmBaseConfig_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET IlmBase::IlmBaseConfig
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_IlmBase_IlmBaseConfig_DEPS_TARGET)
        endif()

        set_property(TARGET IlmBase::IlmBaseConfig APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET IlmBase::IlmBaseConfig APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::IlmBaseConfig APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_LIB_DIRS_RELEASE}>)
        set_property(TARGET IlmBase::IlmBaseConfig APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET IlmBase::IlmBaseConfig APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_IlmBase_IlmBaseConfig_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OpenEXR::IlmImfConfig #############

        set(openexr_OpenEXR_IlmImfConfig_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(openexr_OpenEXR_IlmImfConfig_FRAMEWORKS_FOUND_RELEASE "${openexr_OpenEXR_IlmImfConfig_FRAMEWORKS_RELEASE}" "${openexr_OpenEXR_IlmImfConfig_FRAMEWORK_DIRS_RELEASE}")

        set(openexr_OpenEXR_IlmImfConfig_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET openexr_OpenEXR_IlmImfConfig_DEPS_TARGET)
            add_library(openexr_OpenEXR_IlmImfConfig_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET openexr_OpenEXR_IlmImfConfig_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'openexr_OpenEXR_IlmImfConfig_DEPS_TARGET' to all of them
        conan_package_library_targets("${openexr_OpenEXR_IlmImfConfig_LIBS_RELEASE}"
                              "${openexr_OpenEXR_IlmImfConfig_LIB_DIRS_RELEASE}"
                              "${openexr_OpenEXR_IlmImfConfig_BIN_DIRS_RELEASE}" # package_bindir
                              "${openexr_OpenEXR_IlmImfConfig_LIBRARY_TYPE_RELEASE}"
                              "${openexr_OpenEXR_IlmImfConfig_IS_HOST_WINDOWS_RELEASE}"
                              openexr_OpenEXR_IlmImfConfig_DEPS_TARGET
                              openexr_OpenEXR_IlmImfConfig_LIBRARIES_TARGETS
                              "_RELEASE"
                              "openexr_OpenEXR_IlmImfConfig"
                              "${openexr_OpenEXR_IlmImfConfig_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OpenEXR::IlmImfConfig
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_LIBRARIES_TARGETS}>
                     )

        if("${openexr_OpenEXR_IlmImfConfig_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OpenEXR::IlmImfConfig
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         openexr_OpenEXR_IlmImfConfig_DEPS_TARGET)
        endif()

        set_property(TARGET OpenEXR::IlmImfConfig APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImfConfig APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImfConfig APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_LIB_DIRS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImfConfig APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OpenEXR::IlmImfConfig APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${openexr_OpenEXR_IlmImfConfig_COMPILE_OPTIONS_RELEASE}>)


    ########## AGGREGATED GLOBAL TARGET WITH THE COMPONENTS #####################
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES OpenEXR::IlmImfUtil)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES openexr::ilmbase_conan_pkgconfig)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES OpenEXR::IlmImf)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES IlmBase::IMath)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES IlmBase::IlmThread)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES IlmBase::IexMath)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES IlmBase::Iex)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES IlmBase::Half)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES IlmBase::IlmBaseConfig)
    set_property(TARGET openexr::openexr APPEND PROPERTY INTERFACE_LINK_LIBRARIES OpenEXR::IlmImfConfig)

########## For the modules (FindXXX)
set(openexr_LIBRARIES_RELEASE openexr::openexr)
