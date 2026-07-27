cmake_minimum_required(VERSION 3.16)
project(ogre_recipe_probe LANGUAGES CXX)

find_package(
    OGRE CONFIG REQUIRED
    COMPONENTS
        Main
        Bites
        MeshLodGenerator
        Overlay
        Paging
        Property
        RTShaderSystem
        Terrain
        Volume
)

add_executable(ogre_recipe_probe src/ogre_recipe_probe.cpp)
target_compile_features(ogre_recipe_probe PRIVATE cxx_std_17)
if(APPLE)
    set(
        ROR_OGRE_PROBE_RPATH
        "@loader_path/../lib;@loader_path/../lib/OGRE"
    )
elseif(UNIX)
    set(
        ROR_OGRE_PROBE_RPATH
        "$ORIGIN/../lib;$ORIGIN/../lib/OGRE"
    )
endif()
if(DEFINED ROR_OGRE_PROBE_RPATH)
    set_target_properties(
        ogre_recipe_probe
        PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "${ROR_OGRE_PROBE_RPATH}"
    )
endif()
target_link_libraries(
    ogre_recipe_probe
    PRIVATE
        OGRE::Main
        OGRE::Bites
        OGRE::MeshLodGenerator
        OGRE::Overlay
        OGRE::Paging
        OGRE::Property
        OGRE::RTShaderSystem
        OGRE::Terrain
        OGRE::Volume
)
