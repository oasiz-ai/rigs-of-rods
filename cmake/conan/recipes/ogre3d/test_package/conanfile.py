import os
import shutil
import tempfile

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class OgreTestPackage(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not can_run(self):
            return
        package_folder = self.dependencies["ogre3d"].package_folder
        with tempfile.TemporaryDirectory(
            prefix="ogre-relocated-package-"
        ) as temporary_directory:
            relocated_package = os.path.join(
                temporary_directory,
                "ogre3d",
            )
            shutil.copytree(
                package_folder,
                relocated_package,
                symlinks=True,
            )
            if self.settings.os == "Linux":
                plugins_config = os.path.join(
                    relocated_package,
                    "share",
                    "OGRE-14.5",
                    "plugins.cfg",
                )
                expected_renderer = "OpenGL 3+"
            elif self.settings.os == "Macos":
                plugins_config = os.path.join(
                    relocated_package,
                    "bin",
                    "plugins.cfg",
                )
                expected_renderer = "Metal"
            else:
                plugins_config = os.path.join(
                    relocated_package,
                    "bin",
                    "plugins.cfg",
                )
                expected_renderer = "Direct3D11"
            executable_name = (
                "ogre_recipe_probe.exe"
                if self.settings.os == "Windows"
                else "ogre_recipe_probe"
            )
            executable = os.path.join(
                self.cpp.build.bindirs[0],
                executable_name,
            )
            staged_executable = os.path.join(
                relocated_package,
                "bin",
                executable_name,
            )
            shutil.copy2(executable, staged_executable)
            if self.settings.os == "Macos":
                clear_loader_environment = (
                    "env -u DYLD_LIBRARY_PATH "
                    "-u DYLD_FALLBACK_LIBRARY_PATH "
                    "-u OGRE_PLUGIN_DIR "
                )
            elif self.settings.os == "Linux":
                clear_loader_environment = (
                    "env -u LD_LIBRARY_PATH -u OGRE_PLUGIN_DIR "
                )
            else:
                clear_loader_environment = 'set "OGRE_PLUGIN_DIR=" && '
            self.run(
                f'{clear_loader_environment}"{staged_executable}" '
                f'"{plugins_config}" "{expected_renderer}" '
                f'"{relocated_package}"',
                env="",
            )
