########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(mygui_COMPONENT_NAMES "")
if(DEFINED mygui_FIND_DEPENDENCY_NAMES)
  list(APPEND mygui_FIND_DEPENDENCY_NAMES OGRE freetype)
  list(REMOVE_DUPLICATES mygui_FIND_DEPENDENCY_NAMES)
else()
  set(mygui_FIND_DEPENDENCY_NAMES OGRE freetype)
endif()
set(OGRE_FIND_MODE "NO_MODULE")
set(freetype_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(mygui_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/mygui1303eda8a8050/p")
set(mygui_BUILD_MODULES_PATHS_RELEASE )


set(mygui_INCLUDE_DIRS_RELEASE "${mygui_PACKAGE_FOLDER_RELEASE}/include/MYGUI")
set(mygui_RES_DIRS_RELEASE )
set(mygui_DEFINITIONS_RELEASE "-DMYGUI_STATIC")
set(mygui_SHARED_LINK_FLAGS_RELEASE )
set(mygui_EXE_LINK_FLAGS_RELEASE )
set(mygui_OBJECTS_RELEASE )
set(mygui_COMPILE_DEFINITIONS_RELEASE "MYGUI_STATIC")
set(mygui_COMPILE_OPTIONS_C_RELEASE )
set(mygui_COMPILE_OPTIONS_CXX_RELEASE )
set(mygui_LIB_DIRS_RELEASE "${mygui_PACKAGE_FOLDER_RELEASE}/lib")
set(mygui_BIN_DIRS_RELEASE )
set(mygui_LIBRARY_TYPE_RELEASE UNKNOWN)
set(mygui_IS_HOST_WINDOWS_RELEASE 0)
set(mygui_LIBS_RELEASE MyGUI.OgrePlatform MyGUIEngineStatic)
set(mygui_SYSTEM_LIBS_RELEASE )
set(mygui_FRAMEWORK_DIRS_RELEASE )
set(mygui_FRAMEWORKS_RELEASE )
set(mygui_BUILD_DIRS_RELEASE )
set(mygui_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(mygui_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${mygui_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${mygui_COMPILE_OPTIONS_C_RELEASE}>")
set(mygui_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${mygui_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${mygui_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${mygui_EXE_LINK_FLAGS_RELEASE}>")


set(mygui_COMPONENTS_RELEASE )