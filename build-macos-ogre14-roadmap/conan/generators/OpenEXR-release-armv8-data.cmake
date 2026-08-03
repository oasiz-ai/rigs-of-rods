########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

list(APPEND openexr_COMPONENT_NAMES OpenEXR::IlmImfConfig IlmBase::IlmBaseConfig IlmBase::Half IlmBase::Iex IlmBase::IexMath IlmBase::IlmThread IlmBase::IMath OpenEXR::IlmImf openexr::ilmbase_conan_pkgconfig OpenEXR::IlmImfUtil)
list(REMOVE_DUPLICATES openexr_COMPONENT_NAMES)
if(DEFINED openexr_FIND_DEPENDENCY_NAMES)
  list(APPEND openexr_FIND_DEPENDENCY_NAMES ZLIB)
  list(REMOVE_DUPLICATES openexr_FIND_DEPENDENCY_NAMES)
else()
  set(openexr_FIND_DEPENDENCY_NAMES ZLIB)
endif()
set(ZLIB_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(openexr_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/openeea8be881bad60/p")
set(openexr_BUILD_MODULES_PATHS_RELEASE )


set(openexr_INCLUDE_DIRS_RELEASE )
set(openexr_RES_DIRS_RELEASE )
set(openexr_DEFINITIONS_RELEASE )
set(openexr_SHARED_LINK_FLAGS_RELEASE )
set(openexr_EXE_LINK_FLAGS_RELEASE )
set(openexr_OBJECTS_RELEASE )
set(openexr_COMPILE_DEFINITIONS_RELEASE )
set(openexr_COMPILE_OPTIONS_C_RELEASE )
set(openexr_COMPILE_OPTIONS_CXX_RELEASE )
set(openexr_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_BIN_DIRS_RELEASE )
set(openexr_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_LIBS_RELEASE IlmImfUtil-2_5 IlmImf-2_5 Imath-2_5 IlmThread-2_5 IexMath-2_5 Iex-2_5 Half-2_5)
set(openexr_SYSTEM_LIBS_RELEASE c++)
set(openexr_FRAMEWORK_DIRS_RELEASE )
set(openexr_FRAMEWORKS_RELEASE )
set(openexr_BUILD_DIRS_RELEASE )
set(openexr_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(openexr_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_COMPILE_OPTIONS_C_RELEASE}>")
set(openexr_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_EXE_LINK_FLAGS_RELEASE}>")


set(openexr_COMPONENTS_RELEASE OpenEXR::IlmImfConfig IlmBase::IlmBaseConfig IlmBase::Half IlmBase::Iex IlmBase::IexMath IlmBase::IlmThread IlmBase::IMath OpenEXR::IlmImf openexr::ilmbase_conan_pkgconfig OpenEXR::IlmImfUtil)
########### COMPONENT OpenEXR::IlmImfUtil VARIABLES ############################################

set(openexr_OpenEXR_IlmImfUtil_INCLUDE_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_OpenEXR_IlmImfUtil_BIN_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_OpenEXR_IlmImfUtil_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_OpenEXR_IlmImfUtil_RES_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_DEFINITIONS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_OBJECTS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_COMPILE_DEFINITIONS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_OpenEXR_IlmImfUtil_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_OpenEXR_IlmImfUtil_LIBS_RELEASE IlmImfUtil-2_5)
set(openexr_OpenEXR_IlmImfUtil_SYSTEM_LIBS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_FRAMEWORK_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_FRAMEWORKS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_DEPENDENCIES_RELEASE OpenEXR::IlmImfConfig OpenEXR::IlmImf)
set(openexr_OpenEXR_IlmImfUtil_SHARED_LINK_FLAGS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_EXE_LINK_FLAGS_RELEASE )
set(openexr_OpenEXR_IlmImfUtil_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_OpenEXR_IlmImfUtil_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_OpenEXR_IlmImfUtil_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_OpenEXR_IlmImfUtil_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_OpenEXR_IlmImfUtil_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_OpenEXR_IlmImfUtil_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_OpenEXR_IlmImfUtil_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_OpenEXR_IlmImfUtil_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT openexr::ilmbase_conan_pkgconfig VARIABLES ############################################

set(openexr_openexr_ilmbase_conan_pkgconfig_INCLUDE_DIRS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_openexr_ilmbase_conan_pkgconfig_BIN_DIRS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_openexr_ilmbase_conan_pkgconfig_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_openexr_ilmbase_conan_pkgconfig_RES_DIRS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_DEFINITIONS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_OBJECTS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_COMPILE_DEFINITIONS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_openexr_ilmbase_conan_pkgconfig_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_openexr_ilmbase_conan_pkgconfig_LIBS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_SYSTEM_LIBS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_FRAMEWORK_DIRS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_FRAMEWORKS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_DEPENDENCIES_RELEASE IlmBase::IlmBaseConfig IlmBase::Half IlmBase::Iex IlmBase::IexMath IlmBase::IMath IlmBase::IlmThread)
set(openexr_openexr_ilmbase_conan_pkgconfig_SHARED_LINK_FLAGS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_EXE_LINK_FLAGS_RELEASE )
set(openexr_openexr_ilmbase_conan_pkgconfig_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_openexr_ilmbase_conan_pkgconfig_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_openexr_ilmbase_conan_pkgconfig_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_openexr_ilmbase_conan_pkgconfig_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_openexr_ilmbase_conan_pkgconfig_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_openexr_ilmbase_conan_pkgconfig_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_openexr_ilmbase_conan_pkgconfig_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_openexr_ilmbase_conan_pkgconfig_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OpenEXR::IlmImf VARIABLES ############################################

set(openexr_OpenEXR_IlmImf_INCLUDE_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImf_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_OpenEXR_IlmImf_BIN_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImf_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_OpenEXR_IlmImf_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_OpenEXR_IlmImf_RES_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImf_DEFINITIONS_RELEASE )
set(openexr_OpenEXR_IlmImf_OBJECTS_RELEASE )
set(openexr_OpenEXR_IlmImf_COMPILE_DEFINITIONS_RELEASE )
set(openexr_OpenEXR_IlmImf_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_OpenEXR_IlmImf_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_OpenEXR_IlmImf_LIBS_RELEASE IlmImf-2_5)
set(openexr_OpenEXR_IlmImf_SYSTEM_LIBS_RELEASE )
set(openexr_OpenEXR_IlmImf_FRAMEWORK_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImf_FRAMEWORKS_RELEASE )
set(openexr_OpenEXR_IlmImf_DEPENDENCIES_RELEASE OpenEXR::IlmImfConfig IlmBase::Iex IlmBase::Half IlmBase::IMath IlmBase::IlmThread ZLIB::ZLIB)
set(openexr_OpenEXR_IlmImf_SHARED_LINK_FLAGS_RELEASE )
set(openexr_OpenEXR_IlmImf_EXE_LINK_FLAGS_RELEASE )
set(openexr_OpenEXR_IlmImf_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_OpenEXR_IlmImf_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_OpenEXR_IlmImf_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_OpenEXR_IlmImf_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_OpenEXR_IlmImf_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_OpenEXR_IlmImf_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_OpenEXR_IlmImf_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_OpenEXR_IlmImf_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT IlmBase::IMath VARIABLES ############################################

set(openexr_IlmBase_IMath_INCLUDE_DIRS_RELEASE )
set(openexr_IlmBase_IMath_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_IlmBase_IMath_BIN_DIRS_RELEASE )
set(openexr_IlmBase_IMath_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_IlmBase_IMath_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_IlmBase_IMath_RES_DIRS_RELEASE )
set(openexr_IlmBase_IMath_DEFINITIONS_RELEASE )
set(openexr_IlmBase_IMath_OBJECTS_RELEASE )
set(openexr_IlmBase_IMath_COMPILE_DEFINITIONS_RELEASE )
set(openexr_IlmBase_IMath_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_IlmBase_IMath_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_IlmBase_IMath_LIBS_RELEASE Imath-2_5)
set(openexr_IlmBase_IMath_SYSTEM_LIBS_RELEASE )
set(openexr_IlmBase_IMath_FRAMEWORK_DIRS_RELEASE )
set(openexr_IlmBase_IMath_FRAMEWORKS_RELEASE )
set(openexr_IlmBase_IMath_DEPENDENCIES_RELEASE IlmBase::IlmBaseConfig IlmBase::Half IlmBase::IexMath)
set(openexr_IlmBase_IMath_SHARED_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_IMath_EXE_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_IMath_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_IlmBase_IMath_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_IlmBase_IMath_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_IlmBase_IMath_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_IlmBase_IMath_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_IlmBase_IMath_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_IlmBase_IMath_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_IlmBase_IMath_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT IlmBase::IlmThread VARIABLES ############################################

set(openexr_IlmBase_IlmThread_INCLUDE_DIRS_RELEASE )
set(openexr_IlmBase_IlmThread_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_IlmBase_IlmThread_BIN_DIRS_RELEASE )
set(openexr_IlmBase_IlmThread_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_IlmBase_IlmThread_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_IlmBase_IlmThread_RES_DIRS_RELEASE )
set(openexr_IlmBase_IlmThread_DEFINITIONS_RELEASE )
set(openexr_IlmBase_IlmThread_OBJECTS_RELEASE )
set(openexr_IlmBase_IlmThread_COMPILE_DEFINITIONS_RELEASE )
set(openexr_IlmBase_IlmThread_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_IlmBase_IlmThread_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_IlmBase_IlmThread_LIBS_RELEASE IlmThread-2_5)
set(openexr_IlmBase_IlmThread_SYSTEM_LIBS_RELEASE )
set(openexr_IlmBase_IlmThread_FRAMEWORK_DIRS_RELEASE )
set(openexr_IlmBase_IlmThread_FRAMEWORKS_RELEASE )
set(openexr_IlmBase_IlmThread_DEPENDENCIES_RELEASE IlmBase::IlmBaseConfig IlmBase::Iex)
set(openexr_IlmBase_IlmThread_SHARED_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_IlmThread_EXE_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_IlmThread_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_IlmBase_IlmThread_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_IlmBase_IlmThread_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_IlmBase_IlmThread_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_IlmBase_IlmThread_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_IlmBase_IlmThread_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_IlmBase_IlmThread_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_IlmBase_IlmThread_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT IlmBase::IexMath VARIABLES ############################################

set(openexr_IlmBase_IexMath_INCLUDE_DIRS_RELEASE )
set(openexr_IlmBase_IexMath_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_IlmBase_IexMath_BIN_DIRS_RELEASE )
set(openexr_IlmBase_IexMath_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_IlmBase_IexMath_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_IlmBase_IexMath_RES_DIRS_RELEASE )
set(openexr_IlmBase_IexMath_DEFINITIONS_RELEASE )
set(openexr_IlmBase_IexMath_OBJECTS_RELEASE )
set(openexr_IlmBase_IexMath_COMPILE_DEFINITIONS_RELEASE )
set(openexr_IlmBase_IexMath_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_IlmBase_IexMath_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_IlmBase_IexMath_LIBS_RELEASE IexMath-2_5)
set(openexr_IlmBase_IexMath_SYSTEM_LIBS_RELEASE )
set(openexr_IlmBase_IexMath_FRAMEWORK_DIRS_RELEASE )
set(openexr_IlmBase_IexMath_FRAMEWORKS_RELEASE )
set(openexr_IlmBase_IexMath_DEPENDENCIES_RELEASE IlmBase::IlmBaseConfig IlmBase::Iex)
set(openexr_IlmBase_IexMath_SHARED_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_IexMath_EXE_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_IexMath_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_IlmBase_IexMath_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_IlmBase_IexMath_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_IlmBase_IexMath_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_IlmBase_IexMath_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_IlmBase_IexMath_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_IlmBase_IexMath_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_IlmBase_IexMath_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT IlmBase::Iex VARIABLES ############################################

set(openexr_IlmBase_Iex_INCLUDE_DIRS_RELEASE )
set(openexr_IlmBase_Iex_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_IlmBase_Iex_BIN_DIRS_RELEASE )
set(openexr_IlmBase_Iex_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_IlmBase_Iex_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_IlmBase_Iex_RES_DIRS_RELEASE )
set(openexr_IlmBase_Iex_DEFINITIONS_RELEASE )
set(openexr_IlmBase_Iex_OBJECTS_RELEASE )
set(openexr_IlmBase_Iex_COMPILE_DEFINITIONS_RELEASE )
set(openexr_IlmBase_Iex_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_IlmBase_Iex_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_IlmBase_Iex_LIBS_RELEASE Iex-2_5)
set(openexr_IlmBase_Iex_SYSTEM_LIBS_RELEASE )
set(openexr_IlmBase_Iex_FRAMEWORK_DIRS_RELEASE )
set(openexr_IlmBase_Iex_FRAMEWORKS_RELEASE )
set(openexr_IlmBase_Iex_DEPENDENCIES_RELEASE IlmBase::IlmBaseConfig)
set(openexr_IlmBase_Iex_SHARED_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_Iex_EXE_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_Iex_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_IlmBase_Iex_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_IlmBase_Iex_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_IlmBase_Iex_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_IlmBase_Iex_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_IlmBase_Iex_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_IlmBase_Iex_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_IlmBase_Iex_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT IlmBase::Half VARIABLES ############################################

set(openexr_IlmBase_Half_INCLUDE_DIRS_RELEASE )
set(openexr_IlmBase_Half_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_IlmBase_Half_BIN_DIRS_RELEASE )
set(openexr_IlmBase_Half_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_IlmBase_Half_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_IlmBase_Half_RES_DIRS_RELEASE )
set(openexr_IlmBase_Half_DEFINITIONS_RELEASE )
set(openexr_IlmBase_Half_OBJECTS_RELEASE )
set(openexr_IlmBase_Half_COMPILE_DEFINITIONS_RELEASE )
set(openexr_IlmBase_Half_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_IlmBase_Half_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_IlmBase_Half_LIBS_RELEASE Half-2_5)
set(openexr_IlmBase_Half_SYSTEM_LIBS_RELEASE )
set(openexr_IlmBase_Half_FRAMEWORK_DIRS_RELEASE )
set(openexr_IlmBase_Half_FRAMEWORKS_RELEASE )
set(openexr_IlmBase_Half_DEPENDENCIES_RELEASE IlmBase::IlmBaseConfig)
set(openexr_IlmBase_Half_SHARED_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_Half_EXE_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_Half_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_IlmBase_Half_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_IlmBase_Half_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_IlmBase_Half_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_IlmBase_Half_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_IlmBase_Half_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_IlmBase_Half_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_IlmBase_Half_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT IlmBase::IlmBaseConfig VARIABLES ############################################

set(openexr_IlmBase_IlmBaseConfig_INCLUDE_DIRS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_IlmBase_IlmBaseConfig_BIN_DIRS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_IlmBase_IlmBaseConfig_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_IlmBase_IlmBaseConfig_RES_DIRS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_DEFINITIONS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_OBJECTS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_COMPILE_DEFINITIONS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_IlmBase_IlmBaseConfig_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_IlmBase_IlmBaseConfig_LIBS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_SYSTEM_LIBS_RELEASE c++)
set(openexr_IlmBase_IlmBaseConfig_FRAMEWORK_DIRS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_FRAMEWORKS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_DEPENDENCIES_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_SHARED_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_EXE_LINK_FLAGS_RELEASE )
set(openexr_IlmBase_IlmBaseConfig_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_IlmBase_IlmBaseConfig_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_IlmBase_IlmBaseConfig_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_IlmBase_IlmBaseConfig_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_IlmBase_IlmBaseConfig_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_IlmBase_IlmBaseConfig_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_IlmBase_IlmBaseConfig_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_IlmBase_IlmBaseConfig_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OpenEXR::IlmImfConfig VARIABLES ############################################

set(openexr_OpenEXR_IlmImfConfig_INCLUDE_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_LIB_DIRS_RELEASE "${openexr_PACKAGE_FOLDER_RELEASE}/lib")
set(openexr_OpenEXR_IlmImfConfig_BIN_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_LIBRARY_TYPE_RELEASE STATIC)
set(openexr_OpenEXR_IlmImfConfig_IS_HOST_WINDOWS_RELEASE 0)
set(openexr_OpenEXR_IlmImfConfig_RES_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_DEFINITIONS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_OBJECTS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_COMPILE_DEFINITIONS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_COMPILE_OPTIONS_C_RELEASE "")
set(openexr_OpenEXR_IlmImfConfig_COMPILE_OPTIONS_CXX_RELEASE "")
set(openexr_OpenEXR_IlmImfConfig_LIBS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_SYSTEM_LIBS_RELEASE c++)
set(openexr_OpenEXR_IlmImfConfig_FRAMEWORK_DIRS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_FRAMEWORKS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_DEPENDENCIES_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_SHARED_LINK_FLAGS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_EXE_LINK_FLAGS_RELEASE )
set(openexr_OpenEXR_IlmImfConfig_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(openexr_OpenEXR_IlmImfConfig_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${openexr_OpenEXR_IlmImfConfig_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${openexr_OpenEXR_IlmImfConfig_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${openexr_OpenEXR_IlmImfConfig_EXE_LINK_FLAGS_RELEASE}>
)
set(openexr_OpenEXR_IlmImfConfig_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${openexr_OpenEXR_IlmImfConfig_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${openexr_OpenEXR_IlmImfConfig_COMPILE_OPTIONS_C_RELEASE}>")