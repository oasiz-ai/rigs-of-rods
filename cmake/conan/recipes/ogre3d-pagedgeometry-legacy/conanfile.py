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
)
from conan.tools.scm import Version

from pagedgeometry_package_audit import (
    find_forbidden_cg_package_entries,
    find_removed_cpp17_source_entries,
)


MACOS_DEPLOYMENT_TARGET = "11.0"
OGRE_LEGACY_REFERENCE = (
    "ogre3d/1.11.6.1@anotherfoxguy/stable"
    "#14a5ef79ac748a7159824954dc1b8a43"
)
SUPPORTED_TARGETS = {
    ("Linux", "x86_64"),
    ("Macos", "armv8"),
    ("Windows", "x86_64"),
}


class PagedGeometryLegacyConan(ConanFile):
    name = "ogre3d-pagedgeometry"
    version = "1.2.0"
    required_conan_version = ">=2.31.1"
    license = "Zlib"
    homepage = "https://github.com/OGRECave/ogre-pagedgeometry"
    url = "https://github.com/RigsOfRods/rigs-of-rods"
    description = "Pinned C++17 PagedGeometry package for the Ogre 1.11 host"
    settings = "os", "compiler", "build_type", "arch"
    exports = "pagedgeometry_package_audit.py"

    def export_sources(self):
        export_conandata_patches(self)

    def layout(self):
        cmake_layout(self)

    def validate(self):
        target = (str(self.settings.os), str(self.settings.arch))
        if target not in SUPPORTED_TARGETS:
            raise ConanInvalidConfiguration(
                "The pinned PagedGeometry recipe supports only "
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
                    "The pinned PagedGeometry package requires "
                    f"os.version>={MACOS_DEPLOYMENT_TARGET}"
                )
        if str(self.settings.build_type) != "Release":
            raise ConanInvalidConfiguration(
                "The pinned PagedGeometry package is qualified only for "
                "Release builds"
            )

    def requirements(self):
        self.requires(OGRE_LEGACY_REFERENCE)

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.22 <4]")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)
        apply_conandata_patches(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["PAGEDGEOMETRY_BUILD_SAMPLES"] = False
        toolchain.variables["CMAKE_CXX_STANDARD"] = "17"
        toolchain.variables["CMAKE_CXX_STANDARD_REQUIRED"] = True
        toolchain.variables["CMAKE_CXX_EXTENSIONS"] = False
        toolchain.cache_variables["CMAKE_POLICY_VERSION_MINIMUM"] = "3.5"
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        removed_tokens = find_removed_cpp17_source_entries(self.source_folder)
        if removed_tokens:
            raise ConanInvalidConfiguration(
                "The PagedGeometry source retains removed C++17 library "
                "features: " + ", ".join(removed_tokens)
            )
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()
        copy(
            self,
            "zlib.txt",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

        removed_tokens = find_removed_cpp17_source_entries(
            self.package_folder, relative_trees=("include",)
        )
        forbidden_cg_entries = find_forbidden_cg_package_entries(
            self.package_folder
        )
        if removed_tokens or forbidden_cg_entries:
            details = sorted(set(removed_tokens + forbidden_cg_entries))
            raise ConanInvalidConfiguration(
                "The PagedGeometry package contains removed C++17 features "
                "or forbidden Cg runtime artifacts: " + ", ".join(details)
            )

    def package_info(self):
        self.cpp_info.set_property(
            "cmake_module_file_name", "PagedGeometry"
        )
        self.cpp_info.set_property(
            "cmake_module_target_name", "PagedGeometry::PagedGeometry"
        )
        self.cpp_info.set_property("cmake_file_name", "PagedGeometry")
        self.cpp_info.set_property(
            "cmake_target_name", "PagedGeometry::PagedGeometry"
        )
        self.cpp_info.includedirs = ["include", "include/PagedGeometry"]
        self.cpp_info.libs = collect_libs(self)

    def package_id(self):
        self.info.requires["ogre3d"].full_package_mode()
