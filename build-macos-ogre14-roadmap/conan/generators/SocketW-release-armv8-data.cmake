########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(socketw_COMPONENT_NAMES "")
if(DEFINED socketw_FIND_DEPENDENCY_NAMES)
  list(APPEND socketw_FIND_DEPENDENCY_NAMES OpenSSL)
  list(REMOVE_DUPLICATES socketw_FIND_DEPENDENCY_NAMES)
else()
  set(socketw_FIND_DEPENDENCY_NAMES OpenSSL)
endif()
set(OpenSSL_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(socketw_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/socke9aefaf25306ac/p")
set(socketw_BUILD_MODULES_PATHS_RELEASE )


set(socketw_INCLUDE_DIRS_RELEASE "${socketw_PACKAGE_FOLDER_RELEASE}/include")
set(socketw_RES_DIRS_RELEASE )
set(socketw_DEFINITIONS_RELEASE )
set(socketw_SHARED_LINK_FLAGS_RELEASE )
set(socketw_EXE_LINK_FLAGS_RELEASE )
set(socketw_OBJECTS_RELEASE )
set(socketw_COMPILE_DEFINITIONS_RELEASE )
set(socketw_COMPILE_OPTIONS_C_RELEASE )
set(socketw_COMPILE_OPTIONS_CXX_RELEASE )
set(socketw_LIB_DIRS_RELEASE "${socketw_PACKAGE_FOLDER_RELEASE}/lib")
set(socketw_BIN_DIRS_RELEASE )
set(socketw_LIBRARY_TYPE_RELEASE UNKNOWN)
set(socketw_IS_HOST_WINDOWS_RELEASE 0)
set(socketw_LIBS_RELEASE SocketW)
set(socketw_SYSTEM_LIBS_RELEASE )
set(socketw_FRAMEWORK_DIRS_RELEASE )
set(socketw_FRAMEWORKS_RELEASE )
set(socketw_BUILD_DIRS_RELEASE )
set(socketw_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(socketw_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${socketw_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${socketw_COMPILE_OPTIONS_C_RELEASE}>")
set(socketw_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${socketw_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${socketw_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${socketw_EXE_LINK_FLAGS_RELEASE}>")


set(socketw_COMPONENTS_RELEASE )