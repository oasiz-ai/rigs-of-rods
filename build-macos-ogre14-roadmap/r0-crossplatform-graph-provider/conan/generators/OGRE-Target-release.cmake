# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(ogre3d_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(ogre3d_FRAMEWORKS_FOUND_RELEASE "${ogre3d_FRAMEWORKS_RELEASE}" "${ogre3d_FRAMEWORK_DIRS_RELEASE}")

set(ogre3d_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET ogre3d_DEPS_TARGET)
    add_library(ogre3d_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET ogre3d_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${ogre3d_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${ogre3d_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:OGRE::Main;OGRE::Overlay;OGRE::RTShaderSystem;OGRE::Paging>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### ogre3d_DEPS_TARGET to all of them
conan_package_library_targets("${ogre3d_LIBS_RELEASE}"    # libraries
                              "${ogre3d_LIB_DIRS_RELEASE}" # package_libdir
                              "${ogre3d_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_DEPS_TARGET
                              ogre3d_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "ogre3d"    # package_name
                              "${ogre3d_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${ogre3d_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## COMPONENTS TARGET PROPERTIES Release ########################################

    ########## COMPONENT OGRE::Terrain #############

        set(ogre3d_OGRE_Terrain_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_Terrain_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_Terrain_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_Terrain_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_Terrain_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_Terrain_DEPS_TARGET)
            add_library(ogre3d_OGRE_Terrain_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_Terrain_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_Terrain_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_Terrain_LIBS_RELEASE}"
                              "${ogre3d_OGRE_Terrain_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_Terrain_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_Terrain_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_Terrain_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_Terrain_DEPS_TARGET
                              ogre3d_OGRE_Terrain_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_Terrain"
                              "${ogre3d_OGRE_Terrain_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::Terrain
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_Terrain_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::Terrain
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_Terrain_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::Terrain APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::Terrain APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Terrain APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Terrain APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::Terrain APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Terrain_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OGRE::Bites #############

        set(ogre3d_OGRE_Bites_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_Bites_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_Bites_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_Bites_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_Bites_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_Bites_DEPS_TARGET)
            add_library(ogre3d_OGRE_Bites_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_Bites_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_Bites_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_Bites_LIBS_RELEASE}"
                              "${ogre3d_OGRE_Bites_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_Bites_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_Bites_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_Bites_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_Bites_DEPS_TARGET
                              ogre3d_OGRE_Bites_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_Bites"
                              "${ogre3d_OGRE_Bites_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::Bites
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_Bites_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::Bites
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_Bites_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::Bites APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::Bites APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Bites APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Bites APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::Bites APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Bites_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OGRE::Volume #############

        set(ogre3d_OGRE_Volume_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_Volume_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_Volume_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_Volume_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_Volume_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_Volume_DEPS_TARGET)
            add_library(ogre3d_OGRE_Volume_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_Volume_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_Volume_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_Volume_LIBS_RELEASE}"
                              "${ogre3d_OGRE_Volume_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_Volume_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_Volume_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_Volume_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_Volume_DEPS_TARGET
                              ogre3d_OGRE_Volume_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_Volume"
                              "${ogre3d_OGRE_Volume_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::Volume
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_Volume_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::Volume
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_Volume_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::Volume APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::Volume APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Volume APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Volume APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::Volume APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Volume_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OGRE::RTShaderSystem #############

        set(ogre3d_OGRE_RTShaderSystem_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_RTShaderSystem_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_RTShaderSystem_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_RTShaderSystem_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_RTShaderSystem_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_RTShaderSystem_DEPS_TARGET)
            add_library(ogre3d_OGRE_RTShaderSystem_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_RTShaderSystem_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_RTShaderSystem_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_RTShaderSystem_LIBS_RELEASE}"
                              "${ogre3d_OGRE_RTShaderSystem_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_RTShaderSystem_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_RTShaderSystem_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_RTShaderSystem_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_RTShaderSystem_DEPS_TARGET
                              ogre3d_OGRE_RTShaderSystem_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_RTShaderSystem"
                              "${ogre3d_OGRE_RTShaderSystem_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::RTShaderSystem
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_RTShaderSystem_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::RTShaderSystem
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_RTShaderSystem_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::RTShaderSystem APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::RTShaderSystem APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::RTShaderSystem APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::RTShaderSystem APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::RTShaderSystem APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_RTShaderSystem_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OGRE::Property #############

        set(ogre3d_OGRE_Property_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_Property_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_Property_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_Property_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_Property_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_Property_DEPS_TARGET)
            add_library(ogre3d_OGRE_Property_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_Property_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_Property_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_Property_LIBS_RELEASE}"
                              "${ogre3d_OGRE_Property_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_Property_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_Property_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_Property_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_Property_DEPS_TARGET
                              ogre3d_OGRE_Property_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_Property"
                              "${ogre3d_OGRE_Property_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::Property
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_Property_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::Property
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_Property_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::Property APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::Property APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Property APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Property APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::Property APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Property_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OGRE::Paging #############

        set(ogre3d_OGRE_Paging_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_Paging_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_Paging_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_Paging_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_Paging_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_Paging_DEPS_TARGET)
            add_library(ogre3d_OGRE_Paging_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_Paging_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_Paging_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_Paging_LIBS_RELEASE}"
                              "${ogre3d_OGRE_Paging_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_Paging_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_Paging_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_Paging_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_Paging_DEPS_TARGET
                              ogre3d_OGRE_Paging_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_Paging"
                              "${ogre3d_OGRE_Paging_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::Paging
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_Paging_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::Paging
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_Paging_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::Paging APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::Paging APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Paging APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Paging APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::Paging APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Paging_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OGRE::Overlay #############

        set(ogre3d_OGRE_Overlay_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_Overlay_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_Overlay_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_Overlay_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_Overlay_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_Overlay_DEPS_TARGET)
            add_library(ogre3d_OGRE_Overlay_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_Overlay_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_Overlay_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_Overlay_LIBS_RELEASE}"
                              "${ogre3d_OGRE_Overlay_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_Overlay_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_Overlay_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_Overlay_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_Overlay_DEPS_TARGET
                              ogre3d_OGRE_Overlay_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_Overlay"
                              "${ogre3d_OGRE_Overlay_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::Overlay
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_Overlay_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::Overlay
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_Overlay_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::Overlay APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::Overlay APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Overlay APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Overlay APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::Overlay APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Overlay_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OGRE::MeshLodGenerator #############

        set(ogre3d_OGRE_MeshLodGenerator_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_MeshLodGenerator_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_MeshLodGenerator_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_MeshLodGenerator_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_MeshLodGenerator_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_MeshLodGenerator_DEPS_TARGET)
            add_library(ogre3d_OGRE_MeshLodGenerator_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_MeshLodGenerator_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_MeshLodGenerator_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_MeshLodGenerator_LIBS_RELEASE}"
                              "${ogre3d_OGRE_MeshLodGenerator_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_MeshLodGenerator_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_MeshLodGenerator_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_MeshLodGenerator_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_MeshLodGenerator_DEPS_TARGET
                              ogre3d_OGRE_MeshLodGenerator_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_MeshLodGenerator"
                              "${ogre3d_OGRE_MeshLodGenerator_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::MeshLodGenerator
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_MeshLodGenerator_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::MeshLodGenerator
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_MeshLodGenerator_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::MeshLodGenerator APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::MeshLodGenerator APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::MeshLodGenerator APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::MeshLodGenerator APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::MeshLodGenerator APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_MeshLodGenerator_COMPILE_OPTIONS_RELEASE}>)


    ########## COMPONENT OGRE::Main #############

        set(ogre3d_OGRE_Main_FRAMEWORKS_FOUND_RELEASE "")
        conan_find_apple_frameworks(ogre3d_OGRE_Main_FRAMEWORKS_FOUND_RELEASE "${ogre3d_OGRE_Main_FRAMEWORKS_RELEASE}" "${ogre3d_OGRE_Main_FRAMEWORK_DIRS_RELEASE}")

        set(ogre3d_OGRE_Main_LIBRARIES_TARGETS "")

        ######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
        if(NOT TARGET ogre3d_OGRE_Main_DEPS_TARGET)
            add_library(ogre3d_OGRE_Main_DEPS_TARGET INTERFACE IMPORTED)
        endif()

        set_property(TARGET ogre3d_OGRE_Main_DEPS_TARGET
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_FRAMEWORKS_FOUND_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_SYSTEM_LIBS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_DEPENDENCIES_RELEASE}>
                     )

        ####### Find the libraries declared in cpp_info.component["xxx"].libs,
        ####### create an IMPORTED target for each one and link the 'ogre3d_OGRE_Main_DEPS_TARGET' to all of them
        conan_package_library_targets("${ogre3d_OGRE_Main_LIBS_RELEASE}"
                              "${ogre3d_OGRE_Main_LIB_DIRS_RELEASE}"
                              "${ogre3d_OGRE_Main_BIN_DIRS_RELEASE}" # package_bindir
                              "${ogre3d_OGRE_Main_LIBRARY_TYPE_RELEASE}"
                              "${ogre3d_OGRE_Main_IS_HOST_WINDOWS_RELEASE}"
                              ogre3d_OGRE_Main_DEPS_TARGET
                              ogre3d_OGRE_Main_LIBRARIES_TARGETS
                              "_RELEASE"
                              "ogre3d_OGRE_Main"
                              "${ogre3d_OGRE_Main_NO_SONAME_MODE_RELEASE}")


        ########## TARGET PROPERTIES #####################################
        set_property(TARGET OGRE::Main
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_OBJECTS_RELEASE}>
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_LIBRARIES_TARGETS}>
                     )

        if("${ogre3d_OGRE_Main_LIBS_RELEASE}" STREQUAL "")
            # If the component is not declaring any "cpp_info.components['foo'].libs" the system, frameworks etc are not
            # linked to the imported targets and we need to do it to the global target
            set_property(TARGET OGRE::Main
                         APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                         ogre3d_OGRE_Main_DEPS_TARGET)
        endif()

        set_property(TARGET OGRE::Main APPEND PROPERTY INTERFACE_LINK_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_LINKER_FLAGS_RELEASE}>)
        set_property(TARGET OGRE::Main APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_INCLUDE_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Main APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_LIB_DIRS_RELEASE}>)
        set_property(TARGET OGRE::Main APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_COMPILE_DEFINITIONS_RELEASE}>)
        set_property(TARGET OGRE::Main APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                     $<$<CONFIG:Release>:${ogre3d_OGRE_Main_COMPILE_OPTIONS_RELEASE}>)


    ########## AGGREGATED GLOBAL TARGET WITH THE COMPONENTS #####################
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::Terrain)
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::Bites)
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::Volume)
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::RTShaderSystem)
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::Property)
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::Paging)
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::Overlay)
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::MeshLodGenerator)
    set_property(TARGET OGRE::OGRE APPEND PROPERTY INTERFACE_LINK_LIBRARIES OGRE::Main)

########## For the modules (FindXXX)
set(ogre3d_LIBRARIES_RELEASE OGRE::OGRE)
