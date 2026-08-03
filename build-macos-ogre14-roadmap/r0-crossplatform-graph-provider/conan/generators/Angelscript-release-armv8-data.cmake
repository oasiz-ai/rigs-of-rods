########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

list(APPEND angelscript_COMPONENT_NAMES Angelscript::angelscript)
list(REMOVE_DUPLICATES angelscript_COMPONENT_NAMES)
if(DEFINED angelscript_FIND_DEPENDENCY_NAMES)
  list(APPEND angelscript_FIND_DEPENDENCY_NAMES )
  list(REMOVE_DUPLICATES angelscript_FIND_DEPENDENCY_NAMES)
else()
  set(angelscript_FIND_DEPENDENCY_NAMES )
endif()

########### VARIABLES #######################################################################
#############################################################################################
set(angelscript_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/angelaa06a8a278fae/p")
set(angelscript_BUILD_MODULES_PATHS_RELEASE )


set(angelscript_INCLUDE_DIRS_RELEASE "${angelscript_PACKAGE_FOLDER_RELEASE}/include")
set(angelscript_RES_DIRS_RELEASE )
set(angelscript_DEFINITIONS_RELEASE )
set(angelscript_SHARED_LINK_FLAGS_RELEASE )
set(angelscript_EXE_LINK_FLAGS_RELEASE )
set(angelscript_OBJECTS_RELEASE )
set(angelscript_COMPILE_DEFINITIONS_RELEASE )
set(angelscript_COMPILE_OPTIONS_C_RELEASE )
set(angelscript_COMPILE_OPTIONS_CXX_RELEASE )
set(angelscript_LIB_DIRS_RELEASE "${angelscript_PACKAGE_FOLDER_RELEASE}/lib")
set(angelscript_BIN_DIRS_RELEASE )
set(angelscript_LIBRARY_TYPE_RELEASE STATIC)
set(angelscript_IS_HOST_WINDOWS_RELEASE 0)
set(angelscript_LIBS_RELEASE angelscript)
set(angelscript_SYSTEM_LIBS_RELEASE )
set(angelscript_FRAMEWORK_DIRS_RELEASE )
set(angelscript_FRAMEWORKS_RELEASE )
set(angelscript_BUILD_DIRS_RELEASE )
set(angelscript_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(angelscript_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${angelscript_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${angelscript_COMPILE_OPTIONS_C_RELEASE}>")
set(angelscript_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${angelscript_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${angelscript_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${angelscript_EXE_LINK_FLAGS_RELEASE}>")


set(angelscript_COMPONENTS_RELEASE Angelscript::angelscript)
########### COMPONENT Angelscript::angelscript VARIABLES ############################################

set(angelscript_Angelscript_angelscript_INCLUDE_DIRS_RELEASE "${angelscript_PACKAGE_FOLDER_RELEASE}/include")
set(angelscript_Angelscript_angelscript_LIB_DIRS_RELEASE "${angelscript_PACKAGE_FOLDER_RELEASE}/lib")
set(angelscript_Angelscript_angelscript_BIN_DIRS_RELEASE )
set(angelscript_Angelscript_angelscript_LIBRARY_TYPE_RELEASE STATIC)
set(angelscript_Angelscript_angelscript_IS_HOST_WINDOWS_RELEASE 0)
set(angelscript_Angelscript_angelscript_RES_DIRS_RELEASE )
set(angelscript_Angelscript_angelscript_DEFINITIONS_RELEASE )
set(angelscript_Angelscript_angelscript_OBJECTS_RELEASE )
set(angelscript_Angelscript_angelscript_COMPILE_DEFINITIONS_RELEASE )
set(angelscript_Angelscript_angelscript_COMPILE_OPTIONS_C_RELEASE "")
set(angelscript_Angelscript_angelscript_COMPILE_OPTIONS_CXX_RELEASE "")
set(angelscript_Angelscript_angelscript_LIBS_RELEASE angelscript)
set(angelscript_Angelscript_angelscript_SYSTEM_LIBS_RELEASE )
set(angelscript_Angelscript_angelscript_FRAMEWORK_DIRS_RELEASE )
set(angelscript_Angelscript_angelscript_FRAMEWORKS_RELEASE )
set(angelscript_Angelscript_angelscript_DEPENDENCIES_RELEASE )
set(angelscript_Angelscript_angelscript_SHARED_LINK_FLAGS_RELEASE )
set(angelscript_Angelscript_angelscript_EXE_LINK_FLAGS_RELEASE )
set(angelscript_Angelscript_angelscript_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(angelscript_Angelscript_angelscript_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${angelscript_Angelscript_angelscript_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${angelscript_Angelscript_angelscript_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${angelscript_Angelscript_angelscript_EXE_LINK_FLAGS_RELEASE}>
)
set(angelscript_Angelscript_angelscript_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${angelscript_Angelscript_angelscript_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${angelscript_Angelscript_angelscript_COMPILE_OPTIONS_C_RELEASE}>")