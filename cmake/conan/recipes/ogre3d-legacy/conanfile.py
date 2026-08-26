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
    rename,
    replace_in_file,
    rmdir,
)
from conan.tools.scm import Version
from conan.tools.system.package_manager import Apt

from cg_package_audit import (
    contains_forbidden_cg_token,
    contains_forbidden_legacy_directx_token,
    find_forbidden_cg_package_entries,
    find_forbidden_legacy_directx_package_entries,
    is_trusted_windows_kits_include_path,
    is_trusted_windows_kits_library_path,
)


MACOS_DEPLOYMENT_TARGET = "11.0"
SUPPORTED_TARGETS = {
    ("Linux", "x86_64"),
    ("Macos", "armv8"),
    ("Windows", "x86_64"),
}


class OGRELegacyConan(ConanFile):
    name = "ogre3d"
    required_conan_version = ">=2.31.1"
    license = "MIT"
    homepage = "https://www.ogre3d.org/"
    url = "https://github.com/RigsOfRods/rigs-of-rods"
    description = "Pinned Cg-free Ogre 1.11 compatibility package for RoR"
    topics = ("ogre", "rendering", "graphics", "legacy-compatibility")
    settings = "os", "compiler", "build_type", "arch"
    exports = "cg_package_audit.py"

    options = {
        "resourcemanager_strict": ["off", "pedantic", "strict"],
        "nodeless_positioning": [True, False],
        "profiling": [True, False],
        "profiling_remotery": [True, False],
    }

    default_options = {
        "resourcemanager_strict": "off",
        "nodeless_positioning": True,
        "profiling": False,
        "profiling_remotery": False,
    }

    def export_sources(self):
        export_conandata_patches(self)

    def layout(self):
        cmake_layout(self)

    def validate(self):
        target = (str(self.settings.os), str(self.settings.arch))
        if target not in SUPPORTED_TARGETS:
            raise ConanInvalidConfiguration(
                "The pinned Ogre 1.11 compatibility recipe supports only "
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
                    "The Ogre 1.11 compatibility package requires "
                    f"os.version>={MACOS_DEPLOYMENT_TARGET}"
                )
        if str(self.settings.build_type) != "Release":
            raise ConanInvalidConfiguration(
                "The Ogre 1.11 compatibility package is qualified only for "
                "Release builds"
            )

    def requirements(self):
        # Direct inputs are revision-pinned. The complete application graph is
        # frozen by the checked-in per-platform legacy lockfiles.
        self.requires(
            "zziplib/0.13.78#a702ebdfc849d51f40651cfd8010aecb"
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
            "libpng/1.6.58#19cb72905ae54f54948401f753faa2c1",
            override=True,
        )
        self.requires(
            "libwebp/1.6.0#eb5f8e35fc95980e32b5544a33a270b4",
            override=True,
        )
        self.requires(
            "zlib/1.3.2#1cb806da49011867778ffb6ac7190fcb",
            force=True,
        )

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.22 <4]")

    def system_requirements(self):
        if str(self.settings.os) == "Linux":
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
        tc.variables["OGRE_BUILD_COMPONENT_PYTHON"] = "OFF"
        tc.variables["OGRE_BUILD_DEPENDENCIES"] = "OFF"
        tc.variables["OGRE_BUILD_LIBS_AS_FRAMEWORKS"] = "OFF"
        # Cg is retired even in the explicit Ogre 1.11 developer lane. Do not
        # add a toolkit package or allow a host-installed toolkit to reactivate
        # Plugin_CgProgramManager.
        tc.variables["OGRE_BUILD_PLUGIN_CG"] = "OFF"
        tc.variables["OGRE_BUILD_PLUGIN_STBI"] = "ON"
        tc.variables["OGRE_BUILD_PLUGIN_EXRCODEC"] = "OFF"
        tc.variables["OGRE_BUILD_RENDERSYSTEM_D3D11"] = (
            "ON" if str(self.settings.os) == "Windows" else "OFF"
        )
        tc.variables["OGRE_BUILD_RENDERSYSTEM_D3D9"] = "OFF"
        tc.variables["OGRE_BUILD_RENDERSYSTEM_GL3PLUS"] = "OFF"
        tc.variables["OGRE_BUILD_SAMPLES"] = "OFF"
        tc.variables["OGRE_INSTALL_SAMPLES"] = "OFF"
        tc.variables["OGRE_RESOURCEMANAGER_STRICT"] = 0
        tc.variables["OGRE_NODELESS_POSITIONING"] = (
            self.options.nodeless_positioning
        )
        tc.variables["OGRE_PROFILING"] = self.options.profiling
        tc.variables["OGRE_PROFILING_REMOTERY"] = (
            self.options.profiling_remotery
        )

        if self.options.resourcemanager_strict == "off":
            tc.variables["OGRE_RESOURCEMANAGER_STRICT"] = 0
        elif self.options.resourcemanager_strict == "pedantic":
            tc.variables["OGRE_RESOURCEMANAGER_STRICT"] = 1
        else:
            tc.variables["OGRE_RESOURCEMANAGER_STRICT"] = 2

        if str(self.settings.os) == "Windows":
            tc.cache_variables["CMAKE_DISABLE_FIND_PACKAGE_DirectX"] = True
            tc.cache_variables["CMAKE_REQUIRE_FIND_PACKAGE_DirectX11"] = True
            tc.extra_cxxflags.append("-D_OGRE_FILESYSTEM_ARCHIVE_UNICODE")
        elif str(self.settings.os) == "Macos":
            tc.variables["CMAKE_OSX_DEPLOYMENT_TARGET"] = str(
                self.settings.os.version
            )
        tc.cache_variables["CMAKE_POLICY_VERSION_MINIMUM"] = "3.5"
        tc.generate()
        CMakeDeps(self).generate()

    def _patch_sources(self):
        apply_conandata_patches(self)
        rename(
            self,
            os.path.join(self.source_folder, "CMake", "FeatureSummary.cmake"),
            os.path.join(
                self.source_folder,
                "CMake",
                "OgreFeatureSummary.cmake",
            ),
        )
        replace_in_file(
            self,
            os.path.join(self.source_folder, "CMakeLists.txt"),
            "include(FeatureSummary)",
            "include(OgreFeatureSummary)",
        )

    def _validate_windows_sdk_d3d11_cache(self):
        if str(self.settings.os) != "Windows":
            return

        cache_path = os.path.join(self.build_folder, "CMakeCache.txt")
        if not os.path.isfile(cache_path):
            raise ConanInvalidConfiguration(
                "The Ogre 1.11 Windows build has no generated CMake cache"
            )
        with open(cache_path, encoding="utf-8") as cache_file:
            cache = cache_file.read()

        expected_entries = (
            "OGRE_BUILD_RENDERSYSTEM_D3D11:BOOL=ON",
            "OGRE_BUILD_RENDERSYSTEM_D3D9:BOOL=OFF",
        )
        missing = [entry for entry in expected_entries if entry not in cache]
        if missing:
            raise ConanInvalidConfiguration(
                "The Ogre 1.11 Windows build is not D3D11-only: "
                + ", ".join(missing)
            )

        def cache_path(name):
            prefixes = (f"{name}:PATH=", f"{name}:FILEPATH=")
            values = [
                line.split("=", 1)[1]
                for line in cache.splitlines()
                if line.startswith(prefixes)
            ]
            if (
                len(values) != 1
                or not values[0]
                or values[0].endswith("-NOTFOUND")
            ):
                raise ConanInvalidConfiguration(
                    "The Ogre 1.11 Windows build did not resolve exactly one "
                    + name
                )
            return os.path.realpath(values[0])

        resolved_include_path = cache_path("DirectX11_INCLUDE_DIR")
        if not is_trusted_windows_kits_include_path(resolved_include_path):
            raise ConanInvalidConfiguration(
                "The Ogre 1.11 D3D11 headers are not from the Windows SDK: "
                + resolved_include_path
            )
        if not os.path.isfile(os.path.join(resolved_include_path, "d3d11.h")):
            raise ConanInvalidConfiguration(
                "The resolved Windows SDK D3D11 include has no d3d11.h: "
                + resolved_include_path
            )

        expected_libraries = {
            "DirectX11_D3D11_LIBRARY": "d3d11.lib",
            "DirectX11_DXGI_LIBRARY": "dxgi.lib",
            "DirectX11_DXGUID_LIBRARY": "dxguid.lib",
        }
        for cache_name, expected_filename in expected_libraries.items():
            library_path = cache_path(cache_name)
            if not is_trusted_windows_kits_library_path(
                library_path, expected_filename
            ):
                raise ConanInvalidConfiguration(
                    "The Ogre 1.11 D3D11 import library is not from the "
                    f"Windows SDK: {library_path}"
                )
            if not os.path.isfile(library_path):
                raise ConanInvalidConfiguration(
                    "The resolved Windows SDK D3D11 import library does not "
                    f"exist: {library_path}"
                )

        for retired_name in (
            "DirectX11_D3DX11_LIBRARY",
            "DirectX11_DXERR_LIBRARY",
        ):
            resolved_retired = [
                line.split("=", 1)[1]
                for line in cache.splitlines()
                if line.startswith(
                    (f"{retired_name}:PATH=", f"{retired_name}:FILEPATH=")
                )
                and not line.endswith("-NOTFOUND")
            ]
            if resolved_retired:
                raise ConanInvalidConfiguration(
                    "The Ogre 1.11 Windows build resolved a retired DirectX "
                    f"SDK library: {retired_name}={resolved_retired[0]}"
                )

    def build(self):
        self._patch_sources()
        cmake = CMake(self)
        cmake.configure()
        self._validate_windows_sdk_d3d11_cache()
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
        rmdir(self, os.path.join(self.package_folder, "CMake"))
        rmdir(self, os.path.join(self.package_folder, "Docs"))

        packaged_libraries = collect_libs(self)
        forbidden_libraries = [
            library
            for library in packaged_libraries
            if contains_forbidden_cg_token(library)
        ]
        forbidden_entries = find_forbidden_cg_package_entries(
            self.package_folder
        )
        forbidden_legacy_directx_libraries = [
            library
            for library in packaged_libraries
            if contains_forbidden_legacy_directx_token(library)
        ]
        forbidden_legacy_directx_entries = (
            find_forbidden_legacy_directx_package_entries(
                self.package_folder
            )
        )
        if (
            forbidden_libraries
            or forbidden_entries
            or forbidden_legacy_directx_libraries
            or forbidden_legacy_directx_entries
        ):
            details = sorted(
                set(
                    forbidden_libraries
                    + forbidden_entries
                    + forbidden_legacy_directx_libraries
                    + forbidden_legacy_directx_entries
                )
            )
            raise ConanInvalidConfiguration(
                "The Ogre 1.11 package contains forbidden Cg or legacy "
                "DirectX artifacts: "
                + ", ".join(details)
            )
        if str(self.settings.os) == "Windows" and not any(
            "rendersystem_direct3d11" in library.lower()
            for library in packaged_libraries
        ):
            raise ConanInvalidConfiguration(
                "The Ogre 1.11 Windows package has no D3D11 render system"
            )

    def package_info(self):
        self.cpp_info.set_property("cmake_module_file_name", "OGRE")
        self.cpp_info.set_property("cmake_module_target_name", "OGRE::OGRE")
        self.cpp_info.set_property("cmake_file_name", "OGRE")
        self.cpp_info.set_property("cmake_target_name", "OGRE::OGRE")
        self.cpp_info.includedirs = [
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
        self.cpp_info.libs = collect_libs(self)
