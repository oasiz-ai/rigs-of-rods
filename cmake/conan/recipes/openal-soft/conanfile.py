from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import stdcpp_library
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs, copy, get, rmdir, save

import os
import textwrap


required_conan_version = ">=2.31.1"


class OpenALSoftTsanConan(ConanFile):
    name = "openal-soft"
    description = (
        "Pinned OpenAL Soft build used only by RoR's full-runtime "
        "ThreadSanitizer gate."
    )
    homepage = "https://openal-soft.org/"
    license = "LGPL-2.0-or-later"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "thread_sanitizer": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "thread_sanitizer": True,
    }

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        # OpenAL exposes a C API, while its implementation uses the selected
        # C++ runtime. The upstream ConanCenter recipe intentionally removes
        # cppstd from the package identity for the same reason.
        self.settings.rm_safe("compiler.cppstd")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        self.requires("libalsa/1.2.10")

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.21]")

    def validate(self):
        if str(self.settings.os) != "Linux" or str(self.settings.arch) != "x86_64":
            raise ConanInvalidConfiguration(
                "The pinned OpenAL ThreadSanitizer package is Linux x86_64 only"
            )
        if str(self.settings.compiler) != "gcc":
            raise ConanInvalidConfiguration(
                "The pinned OpenAL ThreadSanitizer package requires GCC"
            )
        if self.options.shared:
            raise ConanInvalidConfiguration(
                "The pinned OpenAL ThreadSanitizer package must remain static"
            )
        if not self.options.thread_sanitizer:
            raise ConanInvalidConfiguration(
                "The pinned OpenAL ThreadSanitizer recipe cannot emit an "
                "uninstrumented package"
            )

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["LIBTYPE"] = "STATIC"
        tc.variables["ALSOFT_UTILS"] = False
        tc.variables["ALSOFT_EXAMPLES"] = False
        tc.variables["ALSOFT_TESTS"] = False
        tc.variables["CMAKE_DISABLE_FIND_PACKAGE_SoundIO"] = True
        sanitizer_flags = [
            "-g",
            "-fsanitize=thread",
            "-fno-omit-frame-pointer",
            "-fno-optimize-sibling-calls",
        ]
        tc.extra_cflags.extend(sanitizer_flags)
        tc.extra_cxxflags.extend(sanitizer_flags)
        tc.extra_exelinkflags.append("-fsanitize=thread")
        tc.extra_sharedlinkflags.append("-fsanitize=thread")
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(
            self,
            "COPYING",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )
        cmake = CMake(self)
        cmake.install()
        rmdir(self, os.path.join(self.package_folder, "share"))
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))
        self._create_cmake_module_variables(
            os.path.join(self.package_folder, self._module_file_rel_path)
        )

    def _create_cmake_module_variables(self, module_file):
        content = textwrap.dedent(
            f"""\
            set(OPENAL_FOUND TRUE)
            if(DEFINED OpenAL_INCLUDE_DIR)
                set(OPENAL_INCLUDE_DIR ${{OpenAL_INCLUDE_DIR}})
            endif()
            if(DEFINED OpenAL_LIBRARIES)
                set(OPENAL_LIBRARY ${{OpenAL_LIBRARIES}})
            endif()
            set(OPENAL_VERSION_STRING {self.version})
            """
        )
        save(self, module_file, content)

    @property
    def _module_file_rel_path(self):
        return os.path.join(
            "lib", "cmake", f"conan-official-{self.name}-variables.cmake"
        )

    def package_info(self):
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.set_property("cmake_file_name", "OpenAL")
        self.cpp_info.set_property("cmake_target_name", "OpenAL::OpenAL")
        self.cpp_info.set_property(
            "cmake_build_modules", [self._module_file_rel_path]
        )
        self.cpp_info.set_property("pkg_config_name", "openal")
        self.cpp_info.libs = collect_libs(self)
        self.cpp_info.includedirs.append(os.path.join("include", "AL"))
        self.cpp_info.system_libs.extend(["dl", "m"])
        libcxx = stdcpp_library(self)
        if libcxx:
            self.cpp_info.system_libs.append(libcxx)
        self.cpp_info.system_libs.append("atomic")
        self.cpp_info.defines.append("AL_LIBTYPE_STATIC")
