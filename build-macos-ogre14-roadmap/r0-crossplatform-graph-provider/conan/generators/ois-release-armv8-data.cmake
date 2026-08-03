########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(ois_COMPONENT_NAMES "")
if(DEFINED ois_FIND_DEPENDENCY_NAMES)
  list(APPEND ois_FIND_DEPENDENCY_NAMES )
  list(REMOVE_DUPLICATES ois_FIND_DEPENDENCY_NAMES)
else()
  set(ois_FIND_DEPENDENCY_NAMES )
endif()

########### VARIABLES #######################################################################
#############################################################################################
set(ois_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/ois3739cf1cb3a0a/p")
set(ois_BUILD_MODULES_PATHS_RELEASE )


set(ois_INCLUDE_DIRS_RELEASE "${ois_PACKAGE_FOLDER_RELEASE}/include")
set(ois_RES_DIRS_RELEASE )
set(ois_DEFINITIONS_RELEASE )
set(ois_SHARED_LINK_FLAGS_RELEASE )
set(ois_EXE_LINK_FLAGS_RELEASE )
set(ois_OBJECTS_RELEASE )
set(ois_COMPILE_DEFINITIONS_RELEASE )
set(ois_COMPILE_OPTIONS_C_RELEASE )
set(ois_COMPILE_OPTIONS_CXX_RELEASE )
set(ois_LIB_DIRS_RELEASE "${ois_PACKAGE_FOLDER_RELEASE}/lib")
set(ois_BIN_DIRS_RELEASE )
set(ois_LIBRARY_TYPE_RELEASE STATIC)
set(ois_IS_HOST_WINDOWS_RELEASE 0)
set(ois_LIBS_RELEASE OIS)
set(ois_SYSTEM_LIBS_RELEASE )
set(ois_FRAMEWORK_DIRS_RELEASE )
set(ois_FRAMEWORKS_RELEASE Foundation Cocoa IOKit AppKit CoreFoundation CoreGraphics)
set(ois_BUILD_DIRS_RELEASE )
set(ois_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(ois_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ois_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ois_COMPILE_OPTIONS_C_RELEASE}>")
set(ois_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ois_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ois_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ois_EXE_LINK_FLAGS_RELEASE}>")


set(ois_COMPONENTS_RELEASE )