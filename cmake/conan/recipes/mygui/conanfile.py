import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import (
    apply_conandata_patches,
    collect_libs,
    copy,
    export_conandata_patches,
    get,
    replace_in_file,
    rm,
)
from conan.tools.scm import Version


MACOS_DEPLOYMENT_TARGET = "11.0"
OGRE14_RECIPE_REVISION = "b95104379fb90b0514cdafbbcf255de0"
FREETYPE_RECIPE_REVISION = "4ee27b7918b546a96d7e6898e3a02b34"
SUPPORTED_TARGETS = {
    ("Linux", "x86_64"),
    ("Macos", "armv8"),
    ("Windows", "x86_64"),
}


class MyGUIConan(ConanFile):
    name = "mygui"
    required_conan_version = ">=2.31.1"
    license = "MIT"
    homepage = "https://mygui.info/"
    url = "https://github.com/MyGUI/mygui"
    description = "MyGUI built against the pinned RoR OGRE 14 migration package"
    topics = ("gui", "ogre", "rendering")
    settings = "os", "compiler", "build_type", "arch"

    def export_sources(self):
        export_conandata_patches(self)

    def layout(self):
        cmake_layout(self)

    def validate(self):
        target = (str(self.settings.os), str(self.settings.arch))
        if target not in SUPPORTED_TARGETS:
            raise ConanInvalidConfiguration(
                "The pinned MyGUI migration recipe supports only "
                "Linux/x86_64, Macos/armv8, and Windows/x86_64; "
                f"received {target[0]}/{target[1]}"
            )
        if str(self.settings.os) == "Macos":
            deployment_target = self.settings.get_safe("os.version")
            if (
                deployment_target is None
                or Version(str(deployment_target))
                < Version(MACOS_DEPLOYMENT_TARGET)
            ):
                raise ConanInvalidConfiguration(
                    "The MyGUI migration package requires os.version>=11.0"
                )

    def requirements(self):
        self.requires(
            f"ogre3d/14.5.2#{OGRE14_RECIPE_REVISION}"
        )
        self.requires(
            f"freetype/2.14.3#{FREETYPE_RECIPE_REVISION}"
        )
        self.requires("zlib/1.3.2", override=True)

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.22 <4]")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        if str(self.settings.os) == "Macos":
            tc.variables["CMAKE_OSX_DEPLOYMENT_TARGET"] = str(
                self.settings.os.version
            )
        tc.variables["MYGUI_BUILD_DEMOS"] = "OFF"
        tc.variables["MYGUI_BUILD_DOCS"] = "OFF"
        tc.variables["MYGUI_BUILD_TEST_APP"] = "OFF"
        tc.variables["MYGUI_BUILD_PLUGINS"] = "OFF"
        tc.variables["MYGUI_BUILD_TOOLS"] = "OFF"
        tc.variables["MYGUI_RENDERSYSTEM"] = "3"
        tc.variables["MYGUI_STATIC"] = "ON"

        if str(self.settings.os) in ("Linux", "Macos"):
            prefix_maps = [
                (
                    self.build_folder,
                    f"mygui-{self.version}/build",
                ),
                (
                    self.source_folder,
                    f"mygui-{self.version}/source",
                ),
                (
                    self.dependencies["ogre3d"].package_folder,
                    "ogre3d-14.5.2/package",
                ),
            ]
            prefix_map_flags = []
            seen_prefixes = set()
            for source_prefix, mapped_prefix in prefix_maps:
                normalized_prefix = str(source_prefix).replace("\\", "/")
                prefix_aliases = [normalized_prefix]
                # macOS exposes /tmp as a symlink to /private/tmp. Conan
                # reports the resolved path, while CMake may emit the lexical
                # /tmp path in __FILE__. Map both spellings identically.
                lexical_tmp = "/tmp"
                resolved_tmp = os.path.realpath(lexical_tmp).replace("\\", "/")
                if normalized_prefix.startswith(f"{resolved_tmp}/"):
                    prefix_aliases.append(
                        lexical_tmp + normalized_prefix[len(resolved_tmp) :]
                    )
                for prefix_alias in prefix_aliases:
                    if prefix_alias in seen_prefixes:
                        continue
                    seen_prefixes.add(prefix_alias)
                    allowed_prefix_characters = "/._-+@: "
                    unsafe_characters = sorted(
                        {
                            character
                            for character in prefix_alias
                            if not (
                                character.isalnum()
                                or character in allowed_prefix_characters
                            )
                        }
                    )
                    if unsafe_characters:
                        raise ConanInvalidConfiguration(
                            "MyGUI compiler prefix-map path contains "
                            "unsupported characters: "
                            f"{unsafe_characters!r}"
                        )
                    # CMakeToolchain stores these arguments in a double-quoted
                    # CMake string. Preserve spaces as part of the argument.
                    escaped_prefix = prefix_alias.replace(" ", "\\\\ ")
                    prefix_map_flags.extend(
                        [
                            f"-fdebug-prefix-map={escaped_prefix}={mapped_prefix}",
                            f"-ffile-prefix-map={escaped_prefix}={mapped_prefix}",
                            f"-fmacro-prefix-map={escaped_prefix}={mapped_prefix}",
                        ]
                    )
            tc.extra_cflags.extend(prefix_map_flags)
            tc.extra_cxxflags.extend(prefix_map_flags)
        tc.generate()
        CMakeDeps(self).generate()

    def _patch_sources(self):
        apply_conandata_patches(self)
        replace_in_file(
            self,
            os.path.join(self.source_folder, "MyGUIEngine/CMakeLists.txt"),
            "${FREETYPE_LIBRARIES}",
            "freetype",
        )
        replace_in_file(
            self,
            os.path.join(self.source_folder, "CMake/Dependencies.cmake"),
            "find_package(OGRE_Old)",
            "find_package(OGRE CONFIG)",
        )
        replace_in_file(
            self,
            os.path.join(
                self.source_folder,
                "Platforms/Ogre/OgrePlatform/CMakeLists.txt",
            ),
            "${OGRE_LIBRARIES}",
            "OGRE::OGRE",
        )
        replace_in_file(
            self,
            os.path.join(
                self.source_folder,
                "CMake/Utils/PrecompiledHeader.cmake",
            ),
            "if (MSVC)",
            """
            target_precompile_headers(${TARGET} PRIVATE ${SRC_FILE})
            if (0)
            """,
        )

    def build(self):
        self._patch_sources()
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        # Upstream's static pkg-config file only describes MyGUIEngineStatic;
        # it omits OgrePlatform and its OGRE dependency. Shipping that partial
        # link contract would silently produce broken consumers.
        rm(
            self,
            "MYGUIStatic.pc",
            os.path.join(self.package_folder, "lib", "pkgconfig"),
        )
        copy(
            self,
            "COPYING.MIT",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        self.cpp_info.set_property("cmake_module_file_name", "MyGUI")
        self.cpp_info.set_property("cmake_module_target_name", "MyGUI::MyGUI")
        self.cpp_info.set_property("cmake_file_name", "MyGUI")
        self.cpp_info.set_property("cmake_target_name", "MyGUI::MyGUI")
        self.cpp_info.includedirs = ["include/MYGUI"]
        if str(self.settings.os) == "Windows":
            self.cpp_info.libdirs = [
                os.path.join("lib", str(self.settings.build_type))
            ]
        else:
            self.cpp_info.libdirs = ["lib"]
        self.cpp_info.libs = sorted(collect_libs(self))
        self.cpp_info.defines = ["MYGUI_STATIC"]
        # MyGUIEngineStatic contains ResourceTrueTypeFont.cpp, whose archive
        # member calls FreeType directly. Keep both static-library dependencies
        # on the exported aggregate target so consumers need only MyGUI::MyGUI.
        self.cpp_info.requires = [
            "ogre3d::Main",
            "freetype::freetype",
        ]

    def package_id(self):
        self.info.requires["ogre3d"].full_recipe_mode()
        self.info.requires["freetype"].full_recipe_mode()
