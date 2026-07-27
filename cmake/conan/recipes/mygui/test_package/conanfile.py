import os

from conan import ConanFile
from conan.errors import ConanException
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


EXPECTED_ARCHIVES = (
    "libMyGUI.OgrePlatform.a",
    "libMyGUIEngineStatic.a",
)
EXPECTED_TRANSITIVE_ARCHIVES = (
    "libfreetype.a",
)
EXPECTED_LIBRARIES = (
    "MyGUI.OgrePlatform",
    "MyGUIEngineStatic",
)


class MyGUITestPackage(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        dependency = self.dependencies["mygui"]
        represented_libraries = tuple(dependency.cpp_info.libs)
        for expected_library in EXPECTED_LIBRARIES:
            if expected_library not in represented_libraries:
                raise ConanException(
                    f"MyGUI package metadata omits {expected_library}: "
                    f"{represented_libraries!r}"
                )

        package_folder = dependency.package_folder
        for expected_archive in EXPECTED_ARCHIVES:
            archive_path = os.path.join(
                package_folder,
                "lib",
                expected_archive,
            )
            if not os.path.isfile(archive_path):
                raise ConanException(
                    f"MyGUI package omits static archive {expected_archive}"
                )
        incomplete_pc = os.path.join(
            package_folder,
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

        link_commands = []
        for root, _, filenames in os.walk(self.build_folder):
            if "link.txt" not in filenames:
                continue
            link_path = os.path.join(root, "link.txt")
            with open(link_path, encoding="utf-8") as link_file:
                link_commands.append(link_file.read())
        complete_link_command = "\n".join(link_commands)
        for expected_archive in EXPECTED_ARCHIVES:
            if expected_archive not in complete_link_command:
                raise ConanException(
                    "Consumer link command omits static archive "
                    f"{expected_archive}"
                )
        for expected_archive in EXPECTED_TRANSITIVE_ARCHIVES:
            if expected_archive not in complete_link_command:
                raise ConanException(
                    "MyGUI::MyGUI omits transitive static archive "
                    f"{expected_archive}"
                )

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
