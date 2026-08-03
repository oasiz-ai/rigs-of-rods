########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(jxrlib_COMPONENT_NAMES "")
if(DEFINED jxrlib_FIND_DEPENDENCY_NAMES)
  list(APPEND jxrlib_FIND_DEPENDENCY_NAMES )
  list(REMOVE_DUPLICATES jxrlib_FIND_DEPENDENCY_NAMES)
else()
  set(jxrlib_FIND_DEPENDENCY_NAMES )
endif()

########### VARIABLES #######################################################################
#############################################################################################
set(jxrlib_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/jxrli621b59e859d36/p")
set(jxrlib_BUILD_MODULES_PATHS_RELEASE )


set(jxrlib_INCLUDE_DIRS_RELEASE )
set(jxrlib_RES_DIRS_RELEASE )
set(jxrlib_DEFINITIONS_RELEASE "-D__ANSI__")
set(jxrlib_SHARED_LINK_FLAGS_RELEASE )
set(jxrlib_EXE_LINK_FLAGS_RELEASE )
set(jxrlib_OBJECTS_RELEASE )
set(jxrlib_COMPILE_DEFINITIONS_RELEASE "__ANSI__")
set(jxrlib_COMPILE_OPTIONS_C_RELEASE )
set(jxrlib_COMPILE_OPTIONS_CXX_RELEASE )
set(jxrlib_LIB_DIRS_RELEASE "${jxrlib_PACKAGE_FOLDER_RELEASE}/lib")
set(jxrlib_BIN_DIRS_RELEASE )
set(jxrlib_LIBRARY_TYPE_RELEASE STATIC)
set(jxrlib_IS_HOST_WINDOWS_RELEASE 0)
set(jxrlib_LIBS_RELEASE jxrglue jpegxr)
set(jxrlib_SYSTEM_LIBS_RELEASE )
set(jxrlib_FRAMEWORK_DIRS_RELEASE )
set(jxrlib_FRAMEWORKS_RELEASE )
set(jxrlib_BUILD_DIRS_RELEASE )
set(jxrlib_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(jxrlib_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${jxrlib_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${jxrlib_COMPILE_OPTIONS_C_RELEASE}>")
set(jxrlib_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${jxrlib_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${jxrlib_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${jxrlib_EXE_LINK_FLAGS_RELEASE}>")


set(jxrlib_COMPONENTS_RELEASE )