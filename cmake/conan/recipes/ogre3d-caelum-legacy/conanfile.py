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
import os

from caelum_package_audit import (
    contains_forbidden_cg_token,
    find_forbidden_caelum_package_entries,
)


OGRE_LEGACY_REFERENCE = (
    "ogre3d/1.11.6.1@anotherfoxguy/stable"
    "#49bf28ca81e5f1d7c9aba93095ae9a82"
)


class CaelumLegacyConan(ConanFile):
    name = "ogre3d-caelum"
    version = "0.6.3.1"
    license = "LGPL-2.1-or-later"
    url = "https://github.com/RigsOfRods/ogre-caelum"
    description = "Caelum sky library pinned to the Cg-free Ogre 1.11 host"
    settings = "os", "compiler", "build_type", "arch"
    exports = "caelum_package_audit.py"

    def layout(self):
        cmake_layout(self)

    def validate(self):
        if str(self.settings.build_type) != "Release":
            raise ConanInvalidConfiguration(
                "the pinned legacy Caelum package supports Release only"
            )

    def requirements(self):
        self.requires(OGRE_LEGACY_REFERENCE)

    def export_sources(self):
        export_conandata_patches(self)

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)
        apply_conandata_patches(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["CMAKE_DEBUG_POSTFIX"] = "d"
        toolchain.cache_variables["CMAKE_POLICY_VERSION_MINIMUM"] = "3.5"
        toolchain.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()
        copy(
            self,
            "LICENSE*",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
            keep_path=False,
        )
        packaged_libraries = collect_libs(self)
        forbidden_libraries = [
            library
            for library in packaged_libraries
            if contains_forbidden_cg_token(library)
        ]
        forbidden_entries = find_forbidden_caelum_package_entries(
            self.package_folder
        )
        if forbidden_libraries or forbidden_entries:
            details = sorted(set(forbidden_libraries + forbidden_entries))
            raise ConanInvalidConfiguration(
                "The Caelum package contains forbidden Cg runtime artifacts: "
                + ", ".join(details)
            )

    def package_info(self):
        self.cpp_info.set_property("cmake_module_file_name", "Caelum")
        self.cpp_info.set_property("cmake_module_target_name", "Caelum::Caelum")
        self.cpp_info.set_property("cmake_file_name", "Caelum")
        self.cpp_info.set_property("cmake_target_name", "Caelum::Caelum")
        self.cpp_info.includedirs = ["include", "include/Caelum"]
        self.cpp_info.libs = collect_libs(self)

    def package_id(self):
        # Caelum is a compiled Ogre plugin. Its binary identity must include
        # the exact Ogre package ID and options, not only the Ogre recipe
        # revision, or Conan could reuse an ABI-incompatible plugin.
        self.info.requires["ogre3d"].full_package_mode()
