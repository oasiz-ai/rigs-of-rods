# Conan automatically generated toolchain file
# DO NOT EDIT MANUALLY, it will be overwritten

# Avoid including toolchain file several times (bad if appending to variables like
#   CMAKE_CXX_FLAGS. See https://github.com/android/ndk/issues/323
include_guard()
message(STATUS "Using Conan toolchain: ${CMAKE_CURRENT_LIST_FILE}")
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeToolchain' generator only works with CMake >= 3.15")
endif()

########## 'user_toolchain' block #############
# Include one or more CMake user toolchain from tools.cmake.cmaketoolchain:user_toolchain



########## 'generic_system' block #############
# Definition of system, platform and toolset





########## 'compilers' block #############

set(CMAKE_C_COMPILER "/usr/bin/cc")
set(CMAKE_CXX_COMPILER "/usr/bin/c++")


########## 'apple_system' block #############
# Define Apple architectures, sysroot, deployment target, bitcode, etc

# Set the architectures for which to build.
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
# Setting CMAKE_OSX_SYSROOT SDK, when using Xcode generator the name is enough
# but full path is necessary for others
set(CMAKE_OSX_SYSROOT macosx CACHE STRING "" FORCE)
# Setting CMAKE_OSX_DEPLOYMENT_TARGET if "os.version" is defined by the used conan profile
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(BITCODE "")
set(FOBJC_ARC "")
set(VISIBILITY "")
#Check if Xcode generator is used, since that will handle these flags automagically
if(CMAKE_GENERATOR MATCHES "Xcode")
  message(DEBUG "Not setting any manual command-line buildflags, since Xcode is selected as generator.")
else()
    string(APPEND CONAN_C_FLAGS " ${BITCODE} ${VISIBILITY}")
    string(APPEND CONAN_CXX_FLAGS " ${BITCODE} ${VISIBILITY}")
    # Objective-C/C++ specific flags
    string(APPEND CONAN_OBJC_FLAGS " ${BITCODE} ${VISIBILITY} ${FOBJC_ARC}")
    string(APPEND CONAN_OBJCXX_FLAGS " ${BITCODE} ${VISIBILITY} ${FOBJC_ARC}")
endif()


########## 'rpath_link_flags' block #############
# Pass -rpath-link pointing to all directories with runtime libraries


########## 'libcxx' block #############
# Definition of libcxx from 'compiler.libcxx' setting, defining the
# right CXX_FLAGS for that libcxx

message(STATUS "Conan toolchain: Defining libcxx as C++ flags: -stdlib=libc++")
string(APPEND CONAN_CXX_FLAGS " -stdlib=libc++")


########## 'cppstd' block #############
# Define the C++ and C standards from 'compiler.cppstd' and 'compiler.cstd'

function(conan_modify_std_watch variable access value current_list_file stack)
    set(conan_watched_std_variable "17")
    if (${variable} STREQUAL "CMAKE_C_STANDARD")
        set(conan_watched_std_variable "")
    endif()
    if ("${access}" STREQUAL "MODIFIED_ACCESS" AND NOT "${value}" STREQUAL "${conan_watched_std_variable}")
        message(STATUS "Warning: Standard ${variable} value defined in conan_toolchain.cmake to ${conan_watched_std_variable} has been modified to ${value} by ${current_list_file}")
    endif()
    unset(conan_watched_std_variable)
endfunction()

message(STATUS "Conan toolchain: C++ Standard 17 with extensions OFF")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
variable_watch(CMAKE_CXX_STANDARD conan_modify_std_watch)


########## 'extra_flags' block #############
# Include extra C++, C and linker flags from configuration tools.build:<type>flags
# and from CMakeToolchain.extra_<type>_flags

# Conan conf flags start: 
# Conan conf flags end


########## 'cmake_flags_init' block #############
# Define CMAKE_<XXX>_FLAGS from CONAN_<XXX>_FLAGS

foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER ${config} config)
    if(DEFINED CONAN_CXX_FLAGS_${config})
      string(APPEND CMAKE_CXX_FLAGS_${config}_INIT " ${CONAN_CXX_FLAGS_${config}}")
    endif()
    if(DEFINED CONAN_C_FLAGS_${config})
      string(APPEND CMAKE_C_FLAGS_${config}_INIT " ${CONAN_C_FLAGS_${config}}")
    endif()
    if(DEFINED CONAN_ASM_FLAGS_${config})
      string(APPEND CMAKE_ASM_FLAGS_${config}_INIT " ${CONAN_ASM_FLAGS_${config}}")
    endif()
    if(DEFINED CONAN_SHARED_LINKER_FLAGS_${config})
      string(APPEND CMAKE_SHARED_LINKER_FLAGS_${config}_INIT " ${CONAN_SHARED_LINKER_FLAGS_${config}}")
    endif()
    if(DEFINED CONAN_EXE_LINKER_FLAGS_${config})
      string(APPEND CMAKE_EXE_LINKER_FLAGS_${config}_INIT " ${CONAN_EXE_LINKER_FLAGS_${config}}")
    endif()
    if(DEFINED CONAN_RC_FLAGS_${config})
      string(APPEND CMAKE_RC_FLAGS_${config}_INIT " ${CONAN_RC_FLAGS_${config}}")
    endif()
endforeach()

if(DEFINED CONAN_CXX_FLAGS)
  string(APPEND CMAKE_CXX_FLAGS_INIT " ${CONAN_CXX_FLAGS}")
endif()
if(DEFINED CONAN_C_FLAGS)
  string(APPEND CMAKE_C_FLAGS_INIT " ${CONAN_C_FLAGS}")
endif()
if(DEFINED CONAN_ASM_FLAGS)
  string(APPEND CMAKE_ASM_FLAGS_INIT " ${CONAN_ASM_FLAGS}")
endif()
if(DEFINED CONAN_SHARED_LINKER_FLAGS)
  string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " ${CONAN_SHARED_LINKER_FLAGS}")
endif()
if(DEFINED CONAN_EXE_LINKER_FLAGS)
  string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${CONAN_EXE_LINKER_FLAGS}")
endif()
if(DEFINED CONAN_RC_FLAGS)
  string(APPEND CMAKE_RC_FLAGS_INIT " ${CONAN_RC_FLAGS}")
endif()
if(DEFINED CONAN_OBJCXX_FLAGS)
  string(APPEND CMAKE_OBJCXX_FLAGS_INIT " ${CONAN_OBJCXX_FLAGS}")
endif()
if(DEFINED CONAN_OBJC_FLAGS)
  string(APPEND CMAKE_OBJC_FLAGS_INIT " ${CONAN_OBJC_FLAGS}")
endif()


########## 'extra_variables' block #############
# Definition of extra CMake variables from tools.cmake.cmaketoolchain:extra_variables



########## 'try_compile' block #############
# Blocks after this one will not be added when running CMake try/checks
get_property( _CMAKE_IN_TRY_COMPILE GLOBAL PROPERTY IN_TRY_COMPILE )
if(_CMAKE_IN_TRY_COMPILE)
    message(STATUS "Running toolchain IN_TRY_COMPILE")
    return()
endif()


########## 'find_paths' block #############
# Define paths to find packages, programs, libraries, etc.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/conan_cmakedeps_paths.cmake")
  message(STATUS "Conan toolchain: Including CMakeConfigDeps generated conan_cmakedeps_paths.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/conan_cmakedeps_paths.cmake")
else()

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# Definition of CMAKE_MODULE_PATH
list(PREPEND CMAKE_MODULE_PATH "/Users/beshoyhanna/.conan2/p/b/openjab5daeada01fd/p/lib/cmake" "/Users/beshoyhanna/.conan2/p/b/opensb4cefc207c363/p/lib/cmake")
# the generators folder (where conan generates files, like this toolchain)
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

# Definition of CMAKE_PREFIX_PATH, CMAKE_XXXXX_PATH
# The explicitly defined "builddirs" of "host" context dependencies must be in PREFIX_PATH
list(PREPEND CMAKE_PREFIX_PATH "/Users/beshoyhanna/.conan2/p/b/openjab5daeada01fd/p/lib/cmake" "/Users/beshoyhanna/.conan2/p/b/opensb4cefc207c363/p/lib/cmake")
# The Conan local "generators" folder, where this toolchain is saved.
list(PREPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_LIST_DIR} )
list(PREPEND CMAKE_LIBRARY_PATH "/Users/beshoyhanna/.conan2/p/b/angelaa06a8a278fae/p/lib" "/Users/beshoyhanna/.conan2/p/b/ois3739cf1cb3a0a/p/lib" "/Users/beshoyhanna/.conan2/p/b/disco036a5e101ef84/p/lib" "/Users/beshoyhanna/.conan2/p/b/libcuc55624cf07b03/p/lib" "/Users/beshoyhanna/.conan2/p/b/fmtae2fe62f159ef/p/lib" "/Users/beshoyhanna/.conan2/p/b/mygui1303eda8a8050/p/lib" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/lib" "/Users/beshoyhanna/.conan2/p/b/freet46b7a36be6efe/p/lib" "/Users/beshoyhanna/.conan2/p/b/bzip2882232f81d459/p/lib" "/Users/beshoyhanna/.conan2/p/b/brotlab986c0a74553/p/lib" "/Users/beshoyhanna/.conan2/p/b/freei88375618a94cf/p/lib" "/Users/beshoyhanna/.conan2/p/b/openjab5daeada01fd/p/lib" "/Users/beshoyhanna/.conan2/p/b/libpn3c9b1bae41bc3/p/lib" "/Users/beshoyhanna/.conan2/p/b/libwece6d336903d23/p/lib" "/Users/beshoyhanna/.conan2/p/b/openeea8be881bad60/p/lib" "/Users/beshoyhanna/.conan2/p/b/libra037391efcc0e3/p/lib" "/Users/beshoyhanna/.conan2/p/b/lcms20a6273fc7bf2/p/lib" "/Users/beshoyhanna/.conan2/p/b/jaspec1fc01243311c/p/lib" "/Users/beshoyhanna/.conan2/p/b/jxrli621b59e859d36/p/lib" "/Users/beshoyhanna/.conan2/p/b/libtid517f37ba445a/p/lib" "/Users/beshoyhanna/.conan2/p/b/xz_uta4e3f22b37efb/p/lib" "/Users/beshoyhanna/.conan2/p/b/libjp5141fbb0c3041/p/lib" "/Users/beshoyhanna/.conan2/p/b/pugix6959a6eaf005a/p/lib" "/Users/beshoyhanna/.conan2/p/b/sdl2fdf16dddfba1/p/lib" "/Users/beshoyhanna/.conan2/p/b/openae5daa33de76ba/p/lib" "/Users/beshoyhanna/.conan2/p/b/socke9aefaf25306ac/p/lib" "/Users/beshoyhanna/.conan2/p/b/opensb4cefc207c363/p/lib" "/Users/beshoyhanna/.conan2/p/b/zlibb67c47b1d4c41/p/lib")
list(PREPEND CMAKE_INCLUDE_PATH "/Users/beshoyhanna/.conan2/p/b/angelaa06a8a278fae/p/include" "/Users/beshoyhanna/.conan2/p/b/ois3739cf1cb3a0a/p/include" "/Users/beshoyhanna/.conan2/p/b/disco036a5e101ef84/p/include" "/Users/beshoyhanna/.conan2/p/b/libcuc55624cf07b03/p/include" "/Users/beshoyhanna/.conan2/p/b/fmtae2fe62f159ef/p/include" "/Users/beshoyhanna/.conan2/p/b/mygui1303eda8a8050/p/include/MYGUI" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/Bites" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/MeshLodGenerator" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/Overlay" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/Paging" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/Plugins" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/Property" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/RenderSystems" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/RTShaderSystem" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/Terrain" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/Threading" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/include/OGRE/Volume" "/Users/beshoyhanna/.conan2/p/b/freet46b7a36be6efe/p/include" "/Users/beshoyhanna/.conan2/p/b/freet46b7a36be6efe/p/include/freetype2" "/Users/beshoyhanna/.conan2/p/b/bzip2882232f81d459/p/include" "/Users/beshoyhanna/.conan2/p/b/brotlab986c0a74553/p/include" "/Users/beshoyhanna/.conan2/p/b/freei88375618a94cf/p/include" "/Users/beshoyhanna/.conan2/p/b/openjab5daeada01fd/p/include" "/Users/beshoyhanna/.conan2/p/b/openjab5daeada01fd/p/include/openjpeg-2.5" "/Users/beshoyhanna/.conan2/p/b/libpn3c9b1bae41bc3/p/include" "/Users/beshoyhanna/.conan2/p/b/libwece6d336903d23/p/include" "/Users/beshoyhanna/.conan2/p/b/openeea8be881bad60/p/include" "/Users/beshoyhanna/.conan2/p/b/openeea8be881bad60/p/include/OpenEXR" "/Users/beshoyhanna/.conan2/p/b/libra037391efcc0e3/p/include" "/Users/beshoyhanna/.conan2/p/b/libra037391efcc0e3/p/include/libraw" "/Users/beshoyhanna/.conan2/p/b/lcms20a6273fc7bf2/p/include" "/Users/beshoyhanna/.conan2/p/b/jaspec1fc01243311c/p/include" "/Users/beshoyhanna/.conan2/p/b/jxrli621b59e859d36/p/include" "/Users/beshoyhanna/.conan2/p/b/libtid517f37ba445a/p/include" "/Users/beshoyhanna/.conan2/p/b/xz_uta4e3f22b37efb/p/include" "/Users/beshoyhanna/.conan2/p/b/libjp5141fbb0c3041/p/include" "/Users/beshoyhanna/.conan2/p/b/pugix6959a6eaf005a/p/include" "/Users/beshoyhanna/.conan2/p/b/sdl2fdf16dddfba1/p/include" "/Users/beshoyhanna/.conan2/p/b/sdl2fdf16dddfba1/p/include/SDL2" "/Users/beshoyhanna/.conan2/p/b/openae5daa33de76ba/p/include" "/Users/beshoyhanna/.conan2/p/b/openae5daa33de76ba/p/include/AL" "/Users/beshoyhanna/.conan2/p/rapid26f7aed6a7601/p/include" "/Users/beshoyhanna/.conan2/p/b/socke9aefaf25306ac/p/include" "/Users/beshoyhanna/.conan2/p/b/opensb4cefc207c363/p/include" "/Users/beshoyhanna/.conan2/p/b/zlibb67c47b1d4c41/p/include")
set(CONAN_RUNTIME_LIB_DIRS "/Users/beshoyhanna/.conan2/p/b/angelaa06a8a278fae/p/lib" "/Users/beshoyhanna/.conan2/p/b/ois3739cf1cb3a0a/p/lib" "/Users/beshoyhanna/.conan2/p/b/disco036a5e101ef84/p/lib" "/Users/beshoyhanna/.conan2/p/b/libcuc55624cf07b03/p/lib" "/Users/beshoyhanna/.conan2/p/b/fmtae2fe62f159ef/p/lib" "/Users/beshoyhanna/.conan2/p/b/mygui1303eda8a8050/p/lib" "/Users/beshoyhanna/.conan2/p/b/ogre3637f0f0e53424/p/lib" "/Users/beshoyhanna/.conan2/p/b/freet46b7a36be6efe/p/lib" "/Users/beshoyhanna/.conan2/p/b/bzip2882232f81d459/p/lib" "/Users/beshoyhanna/.conan2/p/b/brotlab986c0a74553/p/lib" "/Users/beshoyhanna/.conan2/p/b/freei88375618a94cf/p/lib" "/Users/beshoyhanna/.conan2/p/b/openjab5daeada01fd/p/lib" "/Users/beshoyhanna/.conan2/p/b/libpn3c9b1bae41bc3/p/lib" "/Users/beshoyhanna/.conan2/p/b/libwece6d336903d23/p/lib" "/Users/beshoyhanna/.conan2/p/b/openeea8be881bad60/p/lib" "/Users/beshoyhanna/.conan2/p/b/libra037391efcc0e3/p/lib" "/Users/beshoyhanna/.conan2/p/b/lcms20a6273fc7bf2/p/lib" "/Users/beshoyhanna/.conan2/p/b/jaspec1fc01243311c/p/lib" "/Users/beshoyhanna/.conan2/p/b/jxrli621b59e859d36/p/lib" "/Users/beshoyhanna/.conan2/p/b/libtid517f37ba445a/p/lib" "/Users/beshoyhanna/.conan2/p/b/xz_uta4e3f22b37efb/p/lib" "/Users/beshoyhanna/.conan2/p/b/libjp5141fbb0c3041/p/lib" "/Users/beshoyhanna/.conan2/p/b/pugix6959a6eaf005a/p/lib" "/Users/beshoyhanna/.conan2/p/b/sdl2fdf16dddfba1/p/lib" "/Users/beshoyhanna/.conan2/p/b/openae5daa33de76ba/p/lib" "/Users/beshoyhanna/.conan2/p/b/socke9aefaf25306ac/p/lib" "/Users/beshoyhanna/.conan2/p/b/opensb4cefc207c363/p/lib" "/Users/beshoyhanna/.conan2/p/b/zlibb67c47b1d4c41/p/lib" )

endif()


########## 'pkg_config' block #############
# Define pkg-config from 'tools.gnu:pkg_config' executable and paths

if (DEFINED ENV{PKG_CONFIG_PATH})
set(ENV{PKG_CONFIG_PATH} "${CMAKE_CURRENT_LIST_DIR}:$ENV{PKG_CONFIG_PATH}")
else()
set(ENV{PKG_CONFIG_PATH} "${CMAKE_CURRENT_LIST_DIR}:")
endif()


########## 'rpath' block #############
# Defining CMAKE_SKIP_RPATH



########## 'output_dirs' block #############
# Definition of CMAKE_INSTALL_XXX folders

# Ensure export(PACKAGE) honors CMAKE_EXPORT_PACKAGE_REGISTRY even if the
# project sets cmake_minimum_required() lower than 3.15.
cmake_policy(SET CMP0090 NEW)
if(NOT DEFINED CMAKE_EXPORT_PACKAGE_REGISTRY)
    set(CMAKE_EXPORT_PACKAGE_REGISTRY OFF)
endif()

set(CMAKE_INSTALL_BINDIR "bin")
set(CMAKE_INSTALL_SBINDIR "bin")
set(CMAKE_INSTALL_LIBEXECDIR "bin")
set(CMAKE_INSTALL_LIBDIR "lib")
set(CMAKE_INSTALL_INCLUDEDIR "include")
set(CMAKE_INSTALL_OLDINCLUDEDIR "include")


########## 'variables' block #############
# Definition of CMake variables from CMakeToolchain.variables values

# Variables
set(ROR_OGRE14 ON CACHE BOOL "Variable ROR_OGRE14 conan-toolchain defined")
# Variables  per configuration



########## 'preprocessor' block #############
# Preprocessor definitions from CMakeToolchain.preprocessor_definitions values

# Preprocessor definitions per configuration



if(CMAKE_POLICY_DEFAULT_CMP0091)  # Avoid unused and not-initialized warnings
endif()
