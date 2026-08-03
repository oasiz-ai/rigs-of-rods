########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

list(APPEND freeimage_COMPONENT_NAMES freeimage::FreeImage freeimage::FreeImagePlus)
list(REMOVE_DUPLICATES freeimage_COMPONENT_NAMES)
if(DEFINED freeimage_FIND_DEPENDENCY_NAMES)
  list(APPEND freeimage_FIND_DEPENDENCY_NAMES OpenJPEG PNG WebP OpenEXR libraw jxrlib TIFF JPEG ZLIB)
  list(REMOVE_DUPLICATES freeimage_FIND_DEPENDENCY_NAMES)
else()
  set(freeimage_FIND_DEPENDENCY_NAMES OpenJPEG PNG WebP OpenEXR libraw jxrlib TIFF JPEG ZLIB)
endif()
set(OpenJPEG_FIND_MODE "NO_MODULE")
set(PNG_FIND_MODE "NO_MODULE")
set(WebP_FIND_MODE "NO_MODULE")
set(OpenEXR_FIND_MODE "NO_MODULE")
set(libraw_FIND_MODE "NO_MODULE")
set(jxrlib_FIND_MODE "NO_MODULE")
set(TIFF_FIND_MODE "NO_MODULE")
set(JPEG_FIND_MODE "NO_MODULE")
set(ZLIB_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(freeimage_PACKAGE_FOLDER_RELEASE "/Users/beshoyhanna/.conan2/p/b/freei88375618a94cf/p")
set(freeimage_BUILD_MODULES_PATHS_RELEASE )


set(freeimage_INCLUDE_DIRS_RELEASE "${freeimage_PACKAGE_FOLDER_RELEASE}/include")
set(freeimage_RES_DIRS_RELEASE )
set(freeimage_DEFINITIONS_RELEASE "-DFREEIMAGE_LIB")
set(freeimage_SHARED_LINK_FLAGS_RELEASE )
set(freeimage_EXE_LINK_FLAGS_RELEASE )
set(freeimage_OBJECTS_RELEASE )
set(freeimage_COMPILE_DEFINITIONS_RELEASE "FREEIMAGE_LIB")
set(freeimage_COMPILE_OPTIONS_C_RELEASE )
set(freeimage_COMPILE_OPTIONS_CXX_RELEASE )
set(freeimage_LIB_DIRS_RELEASE "${freeimage_PACKAGE_FOLDER_RELEASE}/lib")
set(freeimage_BIN_DIRS_RELEASE )
set(freeimage_LIBRARY_TYPE_RELEASE STATIC)
set(freeimage_IS_HOST_WINDOWS_RELEASE 0)
set(freeimage_LIBS_RELEASE freeimageplus freeimage)
set(freeimage_SYSTEM_LIBS_RELEASE )
set(freeimage_FRAMEWORK_DIRS_RELEASE )
set(freeimage_FRAMEWORKS_RELEASE )
set(freeimage_BUILD_DIRS_RELEASE )
set(freeimage_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(freeimage_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${freeimage_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${freeimage_COMPILE_OPTIONS_C_RELEASE}>")
set(freeimage_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${freeimage_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${freeimage_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${freeimage_EXE_LINK_FLAGS_RELEASE}>")


set(freeimage_COMPONENTS_RELEASE freeimage::FreeImage freeimage::FreeImagePlus)
########### COMPONENT freeimage::FreeImagePlus VARIABLES ############################################

set(freeimage_freeimage_FreeImagePlus_INCLUDE_DIRS_RELEASE "${freeimage_PACKAGE_FOLDER_RELEASE}/include")
set(freeimage_freeimage_FreeImagePlus_LIB_DIRS_RELEASE "${freeimage_PACKAGE_FOLDER_RELEASE}/lib")
set(freeimage_freeimage_FreeImagePlus_BIN_DIRS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_LIBRARY_TYPE_RELEASE STATIC)
set(freeimage_freeimage_FreeImagePlus_IS_HOST_WINDOWS_RELEASE 0)
set(freeimage_freeimage_FreeImagePlus_RES_DIRS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_DEFINITIONS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_OBJECTS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_COMPILE_DEFINITIONS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_COMPILE_OPTIONS_C_RELEASE "")
set(freeimage_freeimage_FreeImagePlus_COMPILE_OPTIONS_CXX_RELEASE "")
set(freeimage_freeimage_FreeImagePlus_LIBS_RELEASE freeimageplus)
set(freeimage_freeimage_FreeImagePlus_SYSTEM_LIBS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_FRAMEWORK_DIRS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_FRAMEWORKS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_DEPENDENCIES_RELEASE freeimage::FreeImage)
set(freeimage_freeimage_FreeImagePlus_SHARED_LINK_FLAGS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_EXE_LINK_FLAGS_RELEASE )
set(freeimage_freeimage_FreeImagePlus_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(freeimage_freeimage_FreeImagePlus_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${freeimage_freeimage_FreeImagePlus_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${freeimage_freeimage_FreeImagePlus_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${freeimage_freeimage_FreeImagePlus_EXE_LINK_FLAGS_RELEASE}>
)
set(freeimage_freeimage_FreeImagePlus_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${freeimage_freeimage_FreeImagePlus_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${freeimage_freeimage_FreeImagePlus_COMPILE_OPTIONS_C_RELEASE}>")
########### COMPONENT freeimage::FreeImage VARIABLES ############################################

set(freeimage_freeimage_FreeImage_INCLUDE_DIRS_RELEASE "${freeimage_PACKAGE_FOLDER_RELEASE}/include")
set(freeimage_freeimage_FreeImage_LIB_DIRS_RELEASE "${freeimage_PACKAGE_FOLDER_RELEASE}/lib")
set(freeimage_freeimage_FreeImage_BIN_DIRS_RELEASE )
set(freeimage_freeimage_FreeImage_LIBRARY_TYPE_RELEASE STATIC)
set(freeimage_freeimage_FreeImage_IS_HOST_WINDOWS_RELEASE 0)
set(freeimage_freeimage_FreeImage_RES_DIRS_RELEASE )
set(freeimage_freeimage_FreeImage_DEFINITIONS_RELEASE "-DFREEIMAGE_LIB")
set(freeimage_freeimage_FreeImage_OBJECTS_RELEASE )
set(freeimage_freeimage_FreeImage_COMPILE_DEFINITIONS_RELEASE "FREEIMAGE_LIB")
set(freeimage_freeimage_FreeImage_COMPILE_OPTIONS_C_RELEASE "")
set(freeimage_freeimage_FreeImage_COMPILE_OPTIONS_CXX_RELEASE "")
set(freeimage_freeimage_FreeImage_LIBS_RELEASE freeimage)
set(freeimage_freeimage_FreeImage_SYSTEM_LIBS_RELEASE )
set(freeimage_freeimage_FreeImage_FRAMEWORK_DIRS_RELEASE )
set(freeimage_freeimage_FreeImage_FRAMEWORKS_RELEASE )
set(freeimage_freeimage_FreeImage_DEPENDENCIES_RELEASE ZLIB::ZLIB JPEG::JPEG openjp2 PNG::PNG libwebp::libwebp openexr::openexr libraw::libraw jxrlib::jxrlib TIFF::TIFF)
set(freeimage_freeimage_FreeImage_SHARED_LINK_FLAGS_RELEASE )
set(freeimage_freeimage_FreeImage_EXE_LINK_FLAGS_RELEASE )
set(freeimage_freeimage_FreeImage_NO_SONAME_MODE_RELEASE FALSE)

# COMPOUND VARIABLES
set(freeimage_freeimage_FreeImage_LINKER_FLAGS_RELEASE
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${freeimage_freeimage_FreeImage_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${freeimage_freeimage_FreeImage_SHARED_LINK_FLAGS_RELEASE}>
        $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${freeimage_freeimage_FreeImage_EXE_LINK_FLAGS_RELEASE}>
)
set(freeimage_freeimage_FreeImage_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${freeimage_freeimage_FreeImage_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${freeimage_freeimage_FreeImage_COMPILE_OPTIONS_C_RELEASE}>")