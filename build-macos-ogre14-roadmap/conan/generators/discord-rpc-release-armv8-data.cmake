########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(discord-rpc_COMPONENT_NAMES "")
if(DEFINED discord-rpc_FIND_DEPENDENCY_NAMES)
  list(APPEND discord-rpc_FIND_DEPENDENCY_NAMES RapidJSON)
  list(REMOVE_DUPLICATES discord-rpc_FIND_DEPENDENCY_NAMES)
else()
  set(discord-rpc_FIND_DEPENDENCY_NAMES RapidJSON)
endif()
set(RapidJSON_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(discord-rpc_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/disco036a5e101ef84/p")
set(discord-rpc_BUILD_MODULES_PATHS_RELEASE )


set(discord-rpc_INCLUDE_DIRS_RELEASE "${discord-rpc_PACKAGE_FOLDER_RELEASE}/include")
set(discord-rpc_RES_DIRS_RELEASE )
set(discord-rpc_DEFINITIONS_RELEASE )
set(discord-rpc_SHARED_LINK_FLAGS_RELEASE )
set(discord-rpc_EXE_LINK_FLAGS_RELEASE )
set(discord-rpc_OBJECTS_RELEASE )
set(discord-rpc_COMPILE_DEFINITIONS_RELEASE )
set(discord-rpc_COMPILE_OPTIONS_C_RELEASE )
set(discord-rpc_COMPILE_OPTIONS_CXX_RELEASE )
set(discord-rpc_LIB_DIRS_RELEASE "${discord-rpc_PACKAGE_FOLDER_RELEASE}/lib")
set(discord-rpc_BIN_DIRS_RELEASE )
set(discord-rpc_LIBRARY_TYPE_RELEASE UNKNOWN)
set(discord-rpc_IS_HOST_WINDOWS_RELEASE 0)
set(discord-rpc_LIBS_RELEASE discord-rpc)
set(discord-rpc_SYSTEM_LIBS_RELEASE )
set(discord-rpc_FRAMEWORK_DIRS_RELEASE )
set(discord-rpc_FRAMEWORKS_RELEASE )
set(discord-rpc_BUILD_DIRS_RELEASE )
set(discord-rpc_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(discord-rpc_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${discord-rpc_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${discord-rpc_COMPILE_OPTIONS_C_RELEASE}>")
set(discord-rpc_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${discord-rpc_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${discord-rpc_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${discord-rpc_EXE_LINK_FLAGS_RELEASE}>")


set(discord-rpc_COMPONENTS_RELEASE )