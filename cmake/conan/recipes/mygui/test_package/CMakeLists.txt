cmake_minimum_required(VERSION 3.16)
project(mygui_recipe_probe LANGUAGES CXX)

find_package(MyGUI CONFIG REQUIRED)

if(NOT TARGET MyGUI::MyGUI)
    message(FATAL_ERROR "Conan did not provide MyGUI::MyGUI")
endif()

add_executable(mygui_recipe_probe src/mygui_recipe_probe.cpp)
target_compile_features(mygui_recipe_probe PRIVATE cxx_std_17)
target_link_libraries(
    mygui_recipe_probe
    PRIVATE
        MyGUI::MyGUI
)
