import os

from conan import ConanFile
from conan.errors import ConanException
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


BASE_LIBRARIES = (
    "MyGUI.OgrePlatform",
    "MyGUIEngineStatic",
)
EXPECTED_REQUIREMENTS = (
    "ogre3d::Main",
    "freetype::freetype",
)


def expected_libraries(os_name, build_type):
    debug_suffix = (
        "_d"
        if str(build_type) == "Debug" and str(os_name) != "Macos"
        else ""
    )
    return tuple(f"{library}{debug_suffix}" for library in BASE_LIBRARIES)


def expected_static_archives(os_name, libraries):
    if str(os_name) == "Windows":
        return tuple(f"{library}.lib" for library in libraries)
    return tuple(f"lib{library}.a" for library in libraries)


class MyGUITestPackage(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        expected_library_names = expected_libraries(
            self.settings.os,
            self.settings.build_type,
        )
        expected_archives = expected_static_archives(
            self.settings.os,
            expected_library_names,
        )
        dependency = self.dependencies["mygui"]
        represented_libraries = tuple(dependency.cpp_info.libs)
        for expected_library in expected_library_names:
            if expected_library not in represented_libraries:
                raise ConanException(
                    f"MyGUI package metadata omits {expected_library}: "
                    f"{represented_libraries!r}"
                )
        represented_requirements = tuple(dependency.cpp_info.requires)
        for expected_requirement in EXPECTED_REQUIREMENTS:
            if expected_requirement not in represented_requirements:
                raise ConanException(
                    "MyGUI package metadata omits transitive requirement "
                    f"{expected_requirement}: {represented_requirements!r}"
                )

        for expected_archive in expected_archives:
            archive_paths = [
                os.path.join(library_directory, expected_archive)
                for library_directory in dependency.cpp_info.libdirs
            ]
            if not any(os.path.isfile(path) for path in archive_paths):
                raise ConanException(
                    "MyGUI package metadata cannot resolve static archive "
                    f"{expected_archive}: {archive_paths!r}"
                )
        incomplete_pc = os.path.join(
            dependency.package_folder,
            "lib",
            "pkgconfig",
            "MYGUIStatic.pc",
        )
        if os.path.exists(incomplete_pc):
            raise ConanException(
                "MyGUI package retained incomplete MYGUIStatic.pc"
            )

        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not can_run(self):
            return
        executable_name = (
            "mygui_recipe_probe.exe"
            if self.settings.os == "Windows"
            else "mygui_recipe_probe"
        )
        executable = os.path.join(
            self.cpp.build.bindirs[0],
            executable_name,
        )
        self.run(f'"{executable}"', env="conanrun")
