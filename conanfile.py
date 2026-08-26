import os
from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMakeToolchain, CMakeDeps
from conan.tools.files import copy


OGRE14_RECIPE_REVISION = "eed82ba79eff2ce2ad4135d539f83929"
ANGELSCRIPT_RECIPE_REVISION = "8c9b8d736d0176a6e69c64a4501eeeb1"
OGRE_LEGACY_RECIPE_REVISION = "14a5ef79ac748a7159824954dc1b8a43"
CAELUM_LEGACY_RECIPE_REVISION = "3bb39b61a989f2f7deaecbab2d4177db"
MYGUI_LEGACY_RECIPE_REVISION = "d544e344e389c9b287124fea8b567d01"
PAGED_GEOMETRY_LEGACY_RECIPE_REVISION = "f87be81bcfb192a40b575cd651bf516c"
OIS_RECIPE_REVISION = "d5025190ec611a8e0851cb16a07437a2"
SDL2_RECIPE_REVISION = "19432981a8779c918a13682d4186fa3b"
SUPPORTED_OGRE14_TARGETS = {
    ("Linux", "x86_64"),
    ("Macos", "armv8"),
    ("Windows", "x86_64"),
}


class RoR(ConanFile):
    name = "Rigs of Rods"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "ogre14": [True, False],
    }
    default_options = {
        # The public product is OgreNext-first. OGRE 14 supplies the hidden
        # simulation/content host only and is never a visible fallback.
        "ogre14": True,
        "ogre3d*:resourcemanager_strict": "off",
        "ogre3d/1.11.*:profiling": "True",
    }

    def layout(self):
        self.folders.generators = os.path.join(self.folders.build, "generators")

    def validate(self):
        target = (str(self.settings.os), str(self.settings.arch))
        if self.options.ogre14 and target not in SUPPORTED_OGRE14_TARGETS:
            raise ConanInvalidConfiguration(
                "The OgreNext-first product suite supports only "
                "Linux/x86_64, Macos/armv8, and Windows/x86_64; "
                f"received {target[0]}/{target[1]}"
            )

    def requirements(self):
        self.requires(
            f"angelscript/2.38.0#{ANGELSCRIPT_RECIPE_REVISION}"
        )
        self.requires(f"ois/1.5.1#{OIS_RECIPE_REVISION}")

        self.requires("discord-rpc/3.4.0@anotherfoxguy/stable")
        self.requires("libcurl/8.2.1")
        self.requires("fmt/12.1.0")
        if self.options.ogre14:
            self.requires("mygui/3.4.0")
            # Keep SDL a direct application dependency instead of relying on
            # OGRE Bites to expose it transitively on any platform.
            self.requires(f"sdl/2.32.10#{SDL2_RECIPE_REVISION}")
            self.requires(
                f"ogre3d/14.5.2#{OGRE14_RECIPE_REVISION}",
                force=True,
            )
        else:
            self.requires(
                "mygui/3.4.0@anotherfoxguy/stable"
                f"#{MYGUI_LEGACY_RECIPE_REVISION}"
            )
            self.requires(
                "ogre3d-caelum/0.6.3.1@anotherfoxguy/stable"
                f"#{CAELUM_LEGACY_RECIPE_REVISION}"
            )
            self.requires(
                "ogre3d-pagedgeometry/1.2.0@anotherfoxguy/stable"
                f"#{PAGED_GEOMETRY_LEGACY_RECIPE_REVISION}"
            )
            self.requires(
                "ogre3d/1.11.6.1@anotherfoxguy/stable"
                f"#{OGRE_LEGACY_RECIPE_REVISION}",
                force=True,
            )
        self.requires("openal-soft/1.24.3")
        self.requires("openssl/3.6.3", force=True)
        self.requires("rapidjson/cci.20211112", force=True)
        self.requires("socketw/3.11.0@anotherfoxguy/stable")

        self.requires("jasper/4.2.4", override=True)
        self.requires("libpng/1.6.58", override=True)
        self.requires(
            "libwebp/1.3.2" if self.options.ogre14 else "libwebp/1.6.0",
            override=True,
        )
        self.requires("zlib/1.3.2", override=True)
        if self.options.ogre14:
            # Retained for the Ogre 14 lock boundary; no active package in the
            # supported graph consumes this compatibility override.
            self.requires(
                "zziplib/0.13.78@anotherfoxguy/stable",
                override=True,
            )
        else:
            self.requires(
                "zziplib/0.13.78#a702ebdfc849d51f40651cfd8010aecb",
                override=True,
            )

    def generate(self):
        tc = CMakeToolchain(self)
        build_renderer_suite = bool(self.options.ogre14)
        tc.variables["ROR_OGRE14"] = build_renderer_suite
        # Keep the supported no-option Conan path on the complete
        # OgreNext-first suite. The legacy dependency graph is an explicit
        # developer opt-out and must disable both dependent product targets
        # rather than relying on CMake's default propagation.
        tc.variables["ROR_RENDERER_PUBLIC_LAUNCHER"] = build_renderer_suite
        tc.variables["ROR_OGRE_NEXT_PRODUCTION_PACKAGE"] = (
            build_renderer_suite
        )
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()
        if self.settings.os == "Windows" and self.settings.build_type == "Release":
            deps.configuration = "RelWithDebInfo"
            deps.generate()

        for dep in self.dependencies.values():
            for f in dep.cpp_info.bindirs:
                self.cp_data(f)
            for f in dep.cpp_info.libdirs:
                self.cp_data(f)

    def cp_data(self, src):
        bindir = os.path.join(self.build_folder, "bin")
        copy(self, "*.dll", src, bindir, False)
        copy(self, "*.dylib*", src, bindir, False)
        copy(self, "*.so*", src, bindir, False)
