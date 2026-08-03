########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

list(APPEND ogre3d_COMPONENT_NAMES OGRE::Main OGRE::MeshLodGenerator OGRE::Overlay OGRE::Paging OGRE::Property OGRE::RTShaderSystem OGRE::Volume OGRE::Bites OGRE::Terrain)
list(REMOVE_DUPLICATES ogre3d_COMPONENT_NAMES)
if(DEFINED ogre3d_FIND_DEPENDENCY_NAMES)
  list(APPEND ogre3d_FIND_DEPENDENCY_NAMES freetype freeimage pugixml SDL2 ZLIB)
  list(REMOVE_DUPLICATES ogre3d_FIND_DEPENDENCY_NAMES)
else()
  set(ogre3d_FIND_DEPENDENCY_NAMES freetype freeimage pugixml SDL2 ZLIB)
endif()
set(freetype_FIND_MODE "NO_MODULE")
set(freeimage_FIND_MODE "NO_MODULE")
set(pugixml_FIND_MODE "NO_MODULE")
set(SDL2_FIND_MODE "NO_MODULE")
set(ZLIB_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(ogre3d_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/ogre3b2e515bf3f232/p")
set(ogre3d_BUILD_MODULES_PATHS_RELEASE )


set(ogre3d_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_RES_DIRS_RELEASE )
set(ogre3d_DEFINITIONS_RELEASE )
set(ogre3d_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OBJECTS_RELEASE )
set(ogre3d_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_COMPILE_OPTIONS_C_RELEASE )
set(ogre3d_COMPILE_OPTIONS_CXX_RELEASE )
set(ogre3d_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_BIN_DIRS_RELEASE )
set(ogre3d_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_LIBS_RELEASE OgreTerrain OgreBites OgreVolume OgreRTShaderSystem OgreProperty OgrePaging OgreOverlay OgreMeshLodGenerator OgreMain)
set(ogre3d_SYSTEM_LIBS_RELEASE )
set(ogre3d_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_FRAMEWORKS_RELEASE )
set(ogre3d_BUILD_DIRS_RELEASE )
set(ogre3d_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(ogre3d_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_COMPILE_OPTIONS_C_RELEASE}>")
set(ogre3d_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_EXE_LINK_FLAGS_RELEASE}>")


set(ogre3d_COMPONENTS_RELEASE OGRE::Main OGRE::MeshLodGenerator OGRE::Overlay OGRE::Paging OGRE::Property OGRE::RTShaderSystem OGRE::Volume OGRE::Bites OGRE::Terrain)
########### COMPONENT OGRE::Terrain VARIABLES ############################################

set(ogre3d_OGRE_Terrain_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_Terrain_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_Terrain_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_Terrain_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_Terrain_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_Terrain_RES_DIRS_RELEASE )
set(ogre3d_OGRE_Terrain_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Terrain_OBJECTS_RELEASE )
set(ogre3d_OGRE_Terrain_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Terrain_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_Terrain_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_Terrain_LIBS_RELEASE OgreTerrain)
set(ogre3d_OGRE_Terrain_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_Terrain_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_Terrain_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_Terrain_DEPENDENCIES_RELEASE OGRE::Main OGRE::Paging OGRE::RTShaderSystem)
set(ogre3d_OGRE_Terrain_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Terrain_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Terrain_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_Terrain_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_Terrain_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_Terrain_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_Terrain_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_Terrain_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_Terrain_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_Terrain_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OGRE::Bites VARIABLES ############################################

set(ogre3d_OGRE_Bites_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_Bites_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_Bites_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_Bites_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_Bites_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_Bites_RES_DIRS_RELEASE )
set(ogre3d_OGRE_Bites_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Bites_OBJECTS_RELEASE )
set(ogre3d_OGRE_Bites_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Bites_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_Bites_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_Bites_LIBS_RELEASE OgreBites)
set(ogre3d_OGRE_Bites_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_Bites_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_Bites_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_Bites_DEPENDENCIES_RELEASE OGRE::Main OGRE::Overlay OGRE::RTShaderSystem)
set(ogre3d_OGRE_Bites_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Bites_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Bites_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_Bites_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_Bites_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_Bites_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_Bites_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_Bites_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_Bites_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_Bites_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OGRE::Volume VARIABLES ############################################

set(ogre3d_OGRE_Volume_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_Volume_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_Volume_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_Volume_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_Volume_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_Volume_RES_DIRS_RELEASE )
set(ogre3d_OGRE_Volume_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Volume_OBJECTS_RELEASE )
set(ogre3d_OGRE_Volume_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Volume_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_Volume_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_Volume_LIBS_RELEASE OgreVolume)
set(ogre3d_OGRE_Volume_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_Volume_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_Volume_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_Volume_DEPENDENCIES_RELEASE OGRE::Main)
set(ogre3d_OGRE_Volume_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Volume_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Volume_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_Volume_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_Volume_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_Volume_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_Volume_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_Volume_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_Volume_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_Volume_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OGRE::RTShaderSystem VARIABLES ############################################

set(ogre3d_OGRE_RTShaderSystem_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_RTShaderSystem_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_RTShaderSystem_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_RTShaderSystem_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_RTShaderSystem_RES_DIRS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_OBJECTS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_RTShaderSystem_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_RTShaderSystem_LIBS_RELEASE OgreRTShaderSystem)
set(ogre3d_OGRE_RTShaderSystem_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_DEPENDENCIES_RELEASE OGRE::Main)
set(ogre3d_OGRE_RTShaderSystem_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_RTShaderSystem_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_RTShaderSystem_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_RTShaderSystem_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_RTShaderSystem_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_RTShaderSystem_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_RTShaderSystem_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_RTShaderSystem_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_RTShaderSystem_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OGRE::Property VARIABLES ############################################

set(ogre3d_OGRE_Property_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_Property_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_Property_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_Property_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_Property_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_Property_RES_DIRS_RELEASE )
set(ogre3d_OGRE_Property_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Property_OBJECTS_RELEASE )
set(ogre3d_OGRE_Property_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Property_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_Property_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_Property_LIBS_RELEASE OgreProperty)
set(ogre3d_OGRE_Property_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_Property_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_Property_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_Property_DEPENDENCIES_RELEASE OGRE::Main)
set(ogre3d_OGRE_Property_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Property_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Property_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_Property_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_Property_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_Property_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_Property_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_Property_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_Property_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_Property_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OGRE::Paging VARIABLES ############################################

set(ogre3d_OGRE_Paging_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_Paging_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_Paging_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_Paging_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_Paging_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_Paging_RES_DIRS_RELEASE )
set(ogre3d_OGRE_Paging_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Paging_OBJECTS_RELEASE )
set(ogre3d_OGRE_Paging_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Paging_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_Paging_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_Paging_LIBS_RELEASE OgrePaging)
set(ogre3d_OGRE_Paging_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_Paging_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_Paging_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_Paging_DEPENDENCIES_RELEASE OGRE::Main)
set(ogre3d_OGRE_Paging_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Paging_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Paging_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_Paging_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_Paging_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_Paging_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_Paging_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_Paging_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_Paging_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_Paging_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OGRE::Overlay VARIABLES ############################################

set(ogre3d_OGRE_Overlay_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_Overlay_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_Overlay_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_Overlay_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_Overlay_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_Overlay_RES_DIRS_RELEASE )
set(ogre3d_OGRE_Overlay_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Overlay_OBJECTS_RELEASE )
set(ogre3d_OGRE_Overlay_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Overlay_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_Overlay_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_Overlay_LIBS_RELEASE OgreOverlay)
set(ogre3d_OGRE_Overlay_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_Overlay_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_Overlay_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_Overlay_DEPENDENCIES_RELEASE OGRE::Main)
set(ogre3d_OGRE_Overlay_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Overlay_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Overlay_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_Overlay_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_Overlay_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_Overlay_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_Overlay_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_Overlay_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_Overlay_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_Overlay_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OGRE::MeshLodGenerator VARIABLES ############################################

set(ogre3d_OGRE_MeshLodGenerator_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_MeshLodGenerator_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_MeshLodGenerator_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_MeshLodGenerator_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_MeshLodGenerator_RES_DIRS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_OBJECTS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_MeshLodGenerator_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_MeshLodGenerator_LIBS_RELEASE OgreMeshLodGenerator)
set(ogre3d_OGRE_MeshLodGenerator_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_DEPENDENCIES_RELEASE OGRE::Main)
set(ogre3d_OGRE_MeshLodGenerator_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_MeshLodGenerator_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_MeshLodGenerator_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_MeshLodGenerator_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_MeshLodGenerator_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_MeshLodGenerator_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_MeshLodGenerator_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_MeshLodGenerator_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_MeshLodGenerator_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT OGRE::Main VARIABLES ############################################

set(ogre3d_OGRE_Main_INCLUDE_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/include"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Bites"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/MeshLodGenerator"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Overlay"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Paging"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Plugins"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Property"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RenderSystems"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/RTShaderSystem"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Terrain"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Threading"
			"${ogre3d_PACKAGE_FOLDER_RELEASE}/include/OGRE/Volume")
set(ogre3d_OGRE_Main_LIB_DIRS_RELEASE "${ogre3d_PACKAGE_FOLDER_RELEASE}/lib")
set(ogre3d_OGRE_Main_BIN_DIRS_RELEASE )
set(ogre3d_OGRE_Main_LIBRARY_TYPE_RELEASE UNKNOWN)
set(ogre3d_OGRE_Main_IS_HOST_WINDOWS_RELEASE 0)
set(ogre3d_OGRE_Main_RES_DIRS_RELEASE )
set(ogre3d_OGRE_Main_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Main_OBJECTS_RELEASE )
set(ogre3d_OGRE_Main_COMPILE_DEFINITIONS_RELEASE )
set(ogre3d_OGRE_Main_COMPILE_OPTIONS_C_RELEASE "")
set(ogre3d_OGRE_Main_COMPILE_OPTIONS_CXX_RELEASE "")
set(ogre3d_OGRE_Main_LIBS_RELEASE OgreMain)
set(ogre3d_OGRE_Main_SYSTEM_LIBS_RELEASE )
set(ogre3d_OGRE_Main_FRAMEWORK_DIRS_RELEASE )
set(ogre3d_OGRE_Main_FRAMEWORKS_RELEASE )
set(ogre3d_OGRE_Main_DEPENDENCIES_RELEASE )
set(ogre3d_OGRE_Main_SHARED_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Main_EXE_LINK_FLAGS_RELEASE )
set(ogre3d_OGRE_Main_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(ogre3d_OGRE_Main_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${ogre3d_OGRE_Main_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${ogre3d_OGRE_Main_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${ogre3d_OGRE_Main_EXE_LINK_FLAGS_RELEASE}>
)
set(ogre3d_OGRE_Main_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${ogre3d_OGRE_Main_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${ogre3d_OGRE_Main_COMPILE_OPTIONS_C_RELEASE}>")