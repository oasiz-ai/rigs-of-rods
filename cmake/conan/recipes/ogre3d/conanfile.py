from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import (
    apply_conandata_patches,
    collect_libs,
    copy,
    export_conandata_patches,
    get,
    load,
    replace_in_file,
    rmdir,
)
from conan.tools.system.package_manager import Apt
from conan.tools.scm import Version

import os


MACOS_DEPLOYMENT_TARGET = "11.0"


class OGREConan(ConanFile):
    name = "ogre3d"
    required_conan_version = ">=2.31.1"
    license = "MIT"
    homepage = "https://www.ogre3d.org/"
    url = "https://github.com/AnotherFoxGuy/conan-OGRE"
    description = "Scene-oriented, flexible 3D engine written in C++"
    topics = ("ogre", "rendering", "graphics", "3d")
    settings = "os", "compiler", "build_type", "arch"

    options = {
        "resourcemanager_strict": ["off", "pedantic", "strict"],
        "nodeless_positioning": [True, False],
        "codec_rsimage": [True, False],
        "with_vulkan": [True, False],
        "profiling": [True, False],
        "profiling_remotery": [True, False],
    }

    default_options = {
        "resourcemanager_strict": "off",
        "nodeless_positioning": True,
        "codec_rsimage": False,
        "with_vulkan": False,
        "profiling": False,
        "profiling_remotery": False,
    }

    def export_sources(self):
        export_conandata_patches(self)

    def layout(self):
        cmake_layout(self)

    def validate(self):
        if str(self.settings.os) not in ("Linux", "Macos", "Windows"):
            raise ConanInvalidConfiguration(
                "The pinned R0 recipe supports Linux, macOS, and Windows only"
            )
        if self.options.with_vulkan:
            raise ConanInvalidConfiguration(
                "The pinned R0 graph does not yet publish a verified Vulkan "
                "SDK package; use the platform renderer for this milestone"
            )
        if self.settings.os == "Macos":
            deployment_target = self.settings.get_safe("os.version")
            if (
                deployment_target is None
                or Version(str(deployment_target))
                < Version(MACOS_DEPLOYMENT_TARGET)
            ):
                raise ConanInvalidConfiguration(
                    "R0 macOS packages require os.version>="
                    f"{MACOS_DEPLOYMENT_TARGET}; the setting must apply to "
                    "the complete host dependency graph"
                )

    def requirements(self):
        # Pin both versions and direct recipe revisions. Transitive recipe
        # revisions are frozen by the checked-in platform lockfile.
        self.requires(
            "zlib/1.3.2#1cb806da49011867778ffb6ac7190fcb"
        )
        self.requires(
            "freetype/2.14.3#4ee27b7918b546a96d7e6898e3a02b34"
        )
        self.requires(
            "freeimage/3.18.0@anotherfoxguy/stable"
            "#8b69961fa00ad36b37d77dd40502fcbf"
        )
        self.requires(
            "pugixml/1.16#aa265531e325d44acf11ae7903ec9410"
        )
        self.requires(
            "sdl/2.32.10#19432981a8779c918a13682d4186fa3b"
        )

    def system_requirements(self):
        if self.settings.os == "Linux":
            Apt(self).install(
                [
                    "libx11-dev",
                    "libxaw7-dev",
                    "libxrandr-dev",
                    "libglu1-mesa-dev",
                ],
                check=True,
            )

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["OGRE_BUILD_COMPONENT_BITES"] = "ON"
        tc.variables["OGRE_BUILD_COMPONENT_CSHARP"] = "OFF"
        tc.variables["OGRE_BUILD_COMPONENT_JAVA"] = "OFF"
        tc.variables["OGRE_BUILD_COMPONENT_OVERLAY_IMGUI"] = "OFF"
        tc.variables["OGRE_BUILD_COMPONENT_PYTHON"] = "OFF"
        tc.variables["OGRE_BUILD_COMPONENT_BULLET"] = "OFF"
        tc.variables["OGRE_BUILD_DEPENDENCIES"] = "OFF"
        tc.variables["OGRE_BUILD_LIBS_AS_FRAMEWORKS"] = "OFF"
        tc.variables["OGRE_BUILD_PLUGIN_CG"] = "OFF"
        tc.variables["OGRE_BUILD_PLUGIN_DOT_SCENE"] = "OFF"
        tc.variables["OGRE_BUILD_PLUGIN_ASSIMP"] = "OFF"
        tc.variables["OGRE_BUILD_PLUGIN_FREEIMAGE"] = "ON"
        # OGRE deliberately comments FreeImage from plugins.cfg when STBI is
        # enabled. RoR's existing media path requires Codec_FreeImage, so R0
        # selects one image codec instead of registering overlapping handlers.
        tc.variables["OGRE_BUILD_PLUGIN_STBI"] = "OFF"
        tc.variables["OGRE_BUILD_PLUGIN_GLSLANG"] = "OFF"
        tc.variables["OGRE_BUILD_PLUGIN_EXRCODEC"] = "OFF"
        tc.variables["OGRE_BUILD_RENDERSYSTEM_D3D11"] = (
            "ON" if self.settings.os == "Windows" else "OFF"
        )
        tc.variables["OGRE_BUILD_RENDERSYSTEM_D3D9"] = "OFF"
        tc.variables["OGRE_BUILD_RENDERSYSTEM_GL"] = "OFF"
        tc.variables["OGRE_BUILD_RENDERSYSTEM_GL3PLUS"] = (
            "OFF" if self.settings.os == "Windows" else "ON"
        )
        # The Linux Ogre 14 process is a hidden scene/resource producer in the
        # combined runtime. Upstream's EGL/X11 window maps unconditionally and
        # ignores the RenderWindow `hidden` creation parameter, while its GLX
        # backend applies that parameter before the first XFlush and reports
        # the resulting hidden state. Pin GLX so Ogre-Next remains the only
        # visible presentation owner; Wayland/EGL is not an admitted VM host.
        if self.settings.os == "Linux":
            tc.variables["OGRE_GLSUPPORT_USE_EGL"] = "OFF"
            tc.variables["OGRE_USE_WAYLAND"] = "OFF"
        tc.variables["OGRE_BUILD_RENDERSYSTEM_GLES2"] = "OFF"
        tc.variables["OGRE_BUILD_RENDERSYSTEM_METAL"] = (
            "ON" if self.settings.os == "Macos" else "OFF"
        )
        tc.variables["OGRE_BUILD_RENDERSYSTEM_TINY"] = "OFF"
        tc.variables["OGRE_BUILD_SAMPLES"] = "OFF"
        tc.variables["OGRE_COPY_DEPENDENCIES"] = "OFF"
        tc.variables["OGRE_INSTALL_DEPENDENCIES"] = "OFF"
        tc.variables["OGRE_INSTALL_SAMPLES"] = "OFF"
        tc.variables["OGRE_BUILD_PLUGIN_RSIMAGE"] = self.options.codec_rsimage
        tc.variables["OGRE_NODELESS_POSITIONING"] = self.options.nodeless_positioning
        tc.variables["OGRE_BUILD_RENDERSYSTEM_VULKAN"] = self.options.with_vulkan
        tc.variables["OGRE_PROFILING"] = self.options.profiling
        tc.variables["OGRE_PROFILING_REMOTERY"] = self.options.profiling_remotery

        if self.options.resourcemanager_strict == "off":
            tc.variables["OGRE_RESOURCEMANAGER_STRICT"] = 0
        elif self.options.resourcemanager_strict == "pedantic":
            tc.variables["OGRE_RESOURCEMANAGER_STRICT"] = 1
        else:
            tc.variables["OGRE_RESOURCEMANAGER_STRICT"] = 2

        if self.settings.os in ("Linux", "Macos"):
            source_prefix = str(self.source_folder).replace("\\", "/")
            allowed_prefix_characters = "/._-+@: "
            unsafe_characters = sorted(
                {
                    character
                    for character in source_prefix
                    if not (
                        character.isalnum()
                        or character in allowed_prefix_characters
                    )
                }
            )
            if unsafe_characters:
                raise ConanInvalidConfiguration(
                    "OGRE source path contains characters that cannot be "
                    "safely represented in compiler prefix maps: "
                    f"{unsafe_characters!r}"
                )
            # Conan's CMakeToolchain embeds extra flags inside a double-quoted
            # CMake string. Two backslashes here decode to one in the final
            # flags value, preserving each space as part of the compiler
            # argument instead of splitting the path.
            cmake_escaped_prefix = source_prefix.replace(" ", "\\\\ ")
            mapped_prefix = f"ogre3d-{self.version}"
            prefix_map_flags = [
                f"-fdebug-prefix-map={cmake_escaped_prefix}={mapped_prefix}",
                f"-ffile-prefix-map={cmake_escaped_prefix}={mapped_prefix}",
                f"-fmacro-prefix-map={cmake_escaped_prefix}={mapped_prefix}",
            ]
            tc.extra_cflags.extend(prefix_map_flags)
            tc.extra_cxxflags.extend(prefix_map_flags)

        if self.settings.os == "Windows":
            tc.extra_cxxflags.append(
                "-D_OGRE_FILESYSTEM_ARCHIVE_UNICODE"
            )
        elif self.settings.os == "Macos":
            # R0's declared native Apple Silicon deployment floor. Keeping the
            # dependency at the same floor prevents a new SDK from silently
            # producing an app bundle that cannot launch on supported systems.
            tc.variables[
                "CMAKE_OSX_DEPLOYMENT_TARGET"
            ] = str(self.settings.os.version)
        tc.generate()
        CMakeDeps(self).generate()

    def _patch_sources(self):
        apply_conandata_patches(self)
        replace_in_file(
            self,
            os.path.join(self.source_folder, "PlugIns/FreeImageCodec/CMakeLists.txt"),
            "${FreeImage_LIBRARIES}",
            "freeimage::FreeImage",
        )
        replace_in_file(
            self,
            os.path.join(self.source_folder, "Components/Overlay/CMakeLists.txt"),
            "${FREETYPE_LIBRARIES}",
            "freetype",
        )
        replace_in_file(
            self,
            os.path.join(self.source_folder, "CMake/Packages/FindDirectX11.cmake"),
            'find_path(DirectX11_INCLUDE_DIR NAMES d3d11.h HINTS "',
            'find_path(DirectX11_INCLUDE_DIR NO_CMAKE_PATH '
            'NO_CMAKE_ENVIRONMENT_PATH NAMES d3d11.h HINTS "',
        )

    def build(self):
        self._patch_sources()
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )
        package_prefix = str(self.package_folder).replace("\\", "/")
        if self.settings.os == "Linux":
            plugins_config = os.path.join(
                self.package_folder,
                "share",
                "OGRE-14.5",
                "plugins.cfg",
            )
            replace_in_file(
                self,
                plugins_config,
                f"PluginFolder={package_prefix}/lib/OGRE",
                "PluginFolder=../../lib/OGRE",
            )
        elif self.settings.os == "Macos":
            plugins_config = os.path.join(
                self.package_folder,
                "bin",
                "plugins.cfg",
            )
            replace_in_file(
                self,
                plugins_config,
                f"PluginFolder={package_prefix}/lib/OGRE",
                "PluginFolder=../lib/OGRE",
            )
        elif self.settings.os == "Windows":
            plugins_config = os.path.join(
                self.package_folder,
                "bin",
                "plugins.cfg",
            )
            # Upstream already emits "." on Windows; replacing an absolute
            # value here would make every native Windows package fail.
        plugins_config_text = load(self, plugins_config)
        expected_plugin_folder = {
            "Linux": "../../lib/OGRE",
            "Macos": "../lib/OGRE",
            "Windows": ".",
        }[str(self.settings.os)]
        active_plugin_folders = [
            line.strip().partition("=")[2].strip()
            for line in plugins_config_text.splitlines()
            if line.strip().startswith("PluginFolder=")
            and not line.strip().startswith("#")
        ]
        if active_plugin_folders != [expected_plugin_folder]:
            raise ConanInvalidConfiguration(
                "installed plugins.cfg has an unexpected PluginFolder: "
                f"{active_plugin_folders!r}"
            )
        if package_prefix in plugins_config_text:
            raise ConanInvalidConfiguration(
                "installed plugins.cfg retains the package build path"
            )
        pkgconfig_dir = os.path.join(self.package_folder, "lib", "pkgconfig")
        if os.path.isdir(pkgconfig_dir):
            for filename in sorted(os.listdir(pkgconfig_dir)):
                if filename.endswith(".pc"):
                    replace_in_file(
                        self,
                        os.path.join(pkgconfig_dir, filename),
                        f"prefix={package_prefix}",
                        "prefix=${pcfiledir}/../..",
                    )
        rmdir(self, os.path.join(self.package_folder, "CMake"))
        rmdir(self, os.path.join(self.package_folder, "Docs"))

    def package_info(self):
        self.cpp_info.set_property("cmake_module_file_name", "OGRE")
        self.cpp_info.set_property("cmake_module_target_name", "OGRE::OGRE")
        self.cpp_info.set_property("cmake_file_name", "OGRE")
        self.cpp_info.set_property("cmake_target_name", "OGRE::OGRE")
        include_directories = [
            "include",
            "include/OGRE",
            "include/OGRE/Bites",
            "include/OGRE/MeshLodGenerator",
            "include/OGRE/Overlay",
            "include/OGRE/Paging",
            "include/OGRE/Plugins",
            "include/OGRE/Property",
            "include/OGRE/RenderSystems",
            "include/OGRE/RTShaderSystem",
            "include/OGRE/Terrain",
            "include/OGRE/Threading",
            "include/OGRE/Volume",
        ]
        debug_suffix = (
            "_d"
            if (
                self.settings.os == "Windows"
                and self.settings.build_type == "Debug"
            )
            else ""
        )
        component_libraries = {
            "Main": f"OgreMain{debug_suffix}",
            "Bites": f"OgreBites{debug_suffix}",
            "MeshLodGenerator": (
                f"OgreMeshLodGenerator{debug_suffix}"
            ),
            "Overlay": f"OgreOverlay{debug_suffix}",
            "Paging": f"OgrePaging{debug_suffix}",
            "Property": f"OgreProperty{debug_suffix}",
            "RTShaderSystem": f"OgreRTShaderSystem{debug_suffix}",
            "Terrain": f"OgreTerrain{debug_suffix}",
            "Volume": f"OgreVolume{debug_suffix}",
        }
        component_requirements = {
            "Main": [],
            "Bites": ["Main", "Overlay", "RTShaderSystem"],
            "MeshLodGenerator": ["Main"],
            "Overlay": ["Main"],
            "Paging": ["Main"],
            "Property": ["Main"],
            "RTShaderSystem": ["Main"],
            "Terrain": ["Main", "Paging", "RTShaderSystem"],
            "Volume": ["Main"],
        }
        packaged_libraries = set(collect_libs(self))
        expected_libraries = set(component_libraries.values())
        if not expected_libraries.issubset(packaged_libraries):
            missing = sorted(expected_libraries - packaged_libraries)
            raise ConanInvalidConfiguration(
                f"OGRE package is missing component libraries: {missing}"
            )

        for component_name in sorted(component_libraries):
            component = self.cpp_info.components[component_name]
            component.set_property(
                "cmake_target_name",
                f"OGRE::{component_name}",
            )
            component.includedirs = include_directories
            component.libs = [component_libraries[component_name]]
            component.requires = component_requirements[component_name]
