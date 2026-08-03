########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

list(APPEND libraw_COMPONENT_NAMES libraw::libraw_)
list(REMOVE_DUPLICATES libraw_COMPONENT_NAMES)
if(DEFINED libraw_FIND_DEPENDENCY_NAMES)
  list(APPEND libraw_FIND_DEPENDENCY_NAMES lcms Jasper JPEG)
  list(REMOVE_DUPLICATES libraw_FIND_DEPENDENCY_NAMES)
else()
  set(libraw_FIND_DEPENDENCY_NAMES lcms Jasper JPEG)
endif()
set(lcms_FIND_MODE "NO_MODULE")
set(Jasper_FIND_MODE "NO_MODULE")
set(JPEG_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(libraw_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/libra037391efcc0e3/p")
set(libraw_BUILD_MODULES_PATHS_RELEASE )


set(libraw_INCLUDE_DIRS_RELEASE )
set(libraw_RES_DIRS_RELEASE )
set(libraw_DEFINITIONS_RELEASE )
set(libraw_SHARED_LINK_FLAGS_RELEASE )
set(libraw_EXE_LINK_FLAGS_RELEASE )
set(libraw_OBJECTS_RELEASE )
set(libraw_COMPILE_DEFINITIONS_RELEASE )
set(libraw_COMPILE_OPTIONS_C_RELEASE )
set(libraw_COMPILE_OPTIONS_CXX_RELEASE )
set(libraw_LIB_DIRS_RELEASE "${libraw_PACKAGE_FOLDER_RELEASE}/lib")
set(libraw_BIN_DIRS_RELEASE )
set(libraw_LIBRARY_TYPE_RELEASE STATIC)
set(libraw_IS_HOST_WINDOWS_RELEASE 0)
set(libraw_LIBS_RELEASE raw)
set(libraw_SYSTEM_LIBS_RELEASE c++)
set(libraw_FRAMEWORK_DIRS_RELEASE )
set(libraw_FRAMEWORKS_RELEASE )
set(libraw_BUILD_DIRS_RELEASE )
set(libraw_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(libraw_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${libraw_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${libraw_COMPILE_OPTIONS_C_RELEASE}>")
set(libraw_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${libraw_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${libraw_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${libraw_EXE_LINK_FLAGS_RELEASE}>")


set(libraw_COMPONENTS_RELEASE libraw::libraw_)
########### COMPONENT libraw::libraw_ VARIABLES ############################################

set(libraw_libraw_libraw__INCLUDE_DIRS_RELEASE )
set(libraw_libraw_libraw__LIB_DIRS_RELEASE "${libraw_PACKAGE_FOLDER_RELEASE}/lib")
set(libraw_libraw_libraw__BIN_DIRS_RELEASE )
set(libraw_libraw_libraw__LIBRARY_TYPE_RELEASE STATIC)
set(libraw_libraw_libraw__IS_HOST_WINDOWS_RELEASE 0)
set(libraw_libraw_libraw__RES_DIRS_RELEASE )
set(libraw_libraw_libraw__DEFINITIONS_RELEASE )
set(libraw_libraw_libraw__OBJECTS_RELEASE )
set(libraw_libraw_libraw__COMPILE_DEFINITIONS_RELEASE )
set(libraw_libraw_libraw__COMPILE_OPTIONS_C_RELEASE "")
set(libraw_libraw_libraw__COMPILE_OPTIONS_CXX_RELEASE "")
set(libraw_libraw_libraw__LIBS_RELEASE raw)
set(libraw_libraw_libraw__SYSTEM_LIBS_RELEASE c++)
set(libraw_libraw_libraw__FRAMEWORK_DIRS_RELEASE )
set(libraw_libraw_libraw__FRAMEWORKS_RELEASE )
set(libraw_libraw_libraw__DEPENDENCIES_RELEASE JPEG::JPEG lcms::lcms Jasper::Jasper)
set(libraw_libraw_libraw__SHARED_LINK_FLAGS_RELEASE )
set(libraw_libraw_libraw__EXE_LINK_FLAGS_RELEASE )
set(libraw_libraw_libraw__NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(libraw_libraw_libraw__LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${libraw_libraw_libraw__SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${libraw_libraw_libraw__SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${libraw_libraw_libraw__EXE_LINK_FLAGS_RELEASE}>
)
set(libraw_libraw_libraw__COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${libraw_libraw_libraw__COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${libraw_libraw_libraw__COMPILE_OPTIONS_C_RELEASE}>")