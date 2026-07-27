#!/usr/bin/env python3
"""Assert the native macOS arm64 shape of an OGRE 14 package."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Sequence


EXPECTED_VERSION = "14.5.2"
EXPECTED_ABI_VERSION = "14.5"
EXPECTED_DEPLOYMENT_TARGET = "11.0"
EXPECTED_PLUGIN_FOLDER = "../lib/OGRE"
EXPECTED_ACTIVE_PLUGINS = {
    "Codec_FreeImage",
    "Plugin_BSPSceneManager",
    "Plugin_OctreeSceneManager",
    "Plugin_OctreeZone",
    "Plugin_PCZSceneManager",
    "Plugin_ParticleFX",
    "RenderSystem_GL3Plus",
    "RenderSystem_Metal",
}
ALLOWED_SYSTEM_PREFIXES = (
    "/System/Library/",
    "/usr/lib/",
)
ALLOWED_RUNTIME_SEARCH_PATHS = {
    "@loader_path",
    "@loader_path/../lib",
    "@loader_path/..",
    "@loader_path/../../lib",
}


class VerificationError(RuntimeError):
    """A native package failed an assertion."""


def path_spelling_variants(path: Path) -> frozenset[str]:
    """Return lexical, resolved, and standard Darwin alias spellings."""

    absolute = Path(os.path.abspath(os.fspath(path)))
    candidates = {
        str(absolute),
        str(absolute.resolve(strict=False)),
    }
    for candidate in tuple(candidates):
        for private_prefix, public_prefix in (
            ("/private/tmp", "/tmp"),
            ("/private/var", "/var"),
            ("/private/etc", "/etc"),
        ):
            if candidate == private_prefix or candidate.startswith(
                private_prefix + "/"
            ):
                candidates.add(
                    public_prefix + candidate[len(private_prefix) :]
                )
            if candidate == public_prefix or candidate.startswith(
                public_prefix + "/"
            ):
                candidates.add(
                    private_prefix + candidate[len(public_prefix) :]
                )
    return frozenset(candidates)


def conan_cache_root(package_folder: Path) -> Path | None:
    """Recognize Conan 2's <home>/p/b/<hash>/p package layout."""

    if (
        package_folder.name == "p"
        and len(package_folder.parents) > 3
        and package_folder.parent.parent.name == "b"
        and package_folder.parent.parent.parent.name == "p"
    ):
        return package_folder.parents[3]
    return None


def forbidden_cache_prefixes(package_folder: Path) -> frozenset[str]:
    prefixes = set(path_spelling_variants(package_folder))
    cache_root = conan_cache_root(package_folder)
    if cache_root is not None:
        prefixes.update(path_spelling_variants(cache_root))
    return frozenset(prefixes)


def matching_forbidden_prefixes(
    text: str,
    forbidden_prefixes: Sequence[str],
) -> list[str]:
    return sorted(
        prefix for prefix in set(forbidden_prefixes) if prefix in text
    )


def assert_no_forbidden_prefixes(
    text: str,
    forbidden_prefixes: Sequence[str],
    *,
    context: str,
) -> None:
    matches = matching_forbidden_prefixes(text, forbidden_prefixes)
    if matches:
        raise VerificationError(
            f"{context} embeds build/package prefix {matches[0]!r}"
        )


def is_safe_rpath_dylib(reference: str) -> bool:
    prefix = "@rpath/"
    if not reference.startswith(prefix):
        return False
    basename = reference[len(prefix) :]
    return bool(
        basename
        and "/" not in basename
        and "\\" not in basename
        and basename not in (".", "..")
        and re.fullmatch(r"[A-Za-z0-9_.+-]+\.dylib", basename)
    )


def is_safe_system_dependency(reference: str) -> bool:
    if (
        "\\" in reference
        or "\0" in reference
        or not reference.startswith(ALLOWED_SYSTEM_PREFIXES)
        or not posixpath.isabs(reference)
    ):
        return False
    return posixpath.normpath(reference) == reference


def packaged_rpath_dependency(
    package_folder: Path,
    reference: str,
) -> bool:
    if not is_safe_rpath_dylib(reference):
        return False
    basename = reference.removeprefix("@rpath/")
    return any(
        candidate.is_file()
        for candidate in (
            package_folder / "lib" / basename,
            package_folder / "lib" / "OGRE" / basename,
        )
    )


def run(command: Sequence[str], *, env: dict[str, str] | None = None) -> str:
    try:
        result = subprocess.run(
            list(command),
            check=False,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=30,
        )
    except subprocess.TimeoutExpired as error:
        raise VerificationError(
            f"command timed out: {' '.join(command)}"
        ) from error
    if result.returncode != 0:
        raise VerificationError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result.stdout


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise VerificationError(f"required tool is unavailable: {name}")
    return path


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise VerificationError(f"cannot read {path}: {error}") from error


def active_plugins(config_text: str) -> set[str]:
    plugins: set[str] = set()
    for line in config_text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        key, separator, value = stripped.partition("=")
        if separator and key.strip() == "Plugin":
            plugin = value.strip()
            if not plugin:
                raise VerificationError("plugins.cfg has an empty Plugin entry")
            if plugin in plugins:
                raise VerificationError(
                    f"plugins.cfg activates {plugin!r} more than once"
                )
            plugins.add(plugin)
    return plugins


def plugin_folder(config_text: str) -> str:
    values: list[str] = []
    for line in config_text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        key, separator, value = stripped.partition("=")
        if separator and key.strip() == "PluginFolder":
            values.append(value.strip())
    if len(values) != 1:
        raise VerificationError(
            "plugins.cfg must have exactly one active PluginFolder"
        )
    return values[0]


def assert_text_metadata(
    package_folder: Path,
    forbidden_prefixes: Sequence[str],
) -> tuple[set[str], int]:
    plugins_path = package_folder / "bin" / "plugins.cfg"
    config_text = read_text(plugins_path)
    folder = plugin_folder(config_text)
    if folder != EXPECTED_PLUGIN_FOLDER:
        raise VerificationError(
            f"plugins.cfg is not package-relative: {folder!r}"
        )

    plugins = active_plugins(config_text)
    if plugins != EXPECTED_ACTIVE_PLUGINS:
        raise VerificationError(
            "plugins.cfg active set changed: "
            f"expected {sorted(EXPECTED_ACTIVE_PLUGINS)}, "
            f"found {sorted(plugins)}"
        )

    license_path = package_folder / "licenses" / "LICENSE"
    if not license_path.is_file() or license_path.is_symlink():
        raise VerificationError("package does not contain the OGRE license")
    if "MIT License" not in read_text(license_path):
        raise VerificationError("packaged OGRE license text is unexpected")

    plugin_dir = package_folder / "lib" / "OGRE"
    core_library = (
        package_folder
        / "lib"
        / f"libOgreMain.{EXPECTED_ABI_VERSION}.dylib"
    )
    if not core_library.is_file() or core_library.is_symlink():
        raise VerificationError(
            f"versioned OGRE core is absent: {core_library.name}"
        )
    for plugin in sorted(plugins):
        unversioned = plugin_dir / f"{plugin}.dylib"
        versioned = (
            plugin_dir
            / f"{plugin}.{EXPECTED_ABI_VERSION}.dylib"
        )
        if not unversioned.is_symlink() or not versioned.is_file():
            raise VerificationError(
                "active plugin lacks its versioned dylib/symlink pair: "
                f"{plugin}"
            )

    forbidden_named_files = [
        path
        for path in package_folder.rglob("*")
        if path.is_file()
        and not path.is_symlink()
        and "cgprogrammanager" in path.name.lower()
    ]
    if forbidden_named_files:
        raise VerificationError(
            "Cg plugin files are present: "
            + ", ".join(
                str(path.relative_to(package_folder))
                for path in forbidden_named_files
            )
        )

    pkgconfig_files = sorted(
        (package_folder / "lib" / "pkgconfig").glob("*.pc")
    )
    if not pkgconfig_files:
        raise VerificationError("package has no pkg-config metadata")
    for path in pkgconfig_files:
        text = read_text(path)
        if not text.startswith("prefix=${pcfiledir}/../..\n"):
            raise VerificationError(
                f"{path.name} does not use a relative prefix"
            )
        assert_no_forbidden_prefixes(
            text,
            forbidden_prefixes,
            context=path.name,
        )
    assert_no_forbidden_prefixes(
        config_text,
        forbidden_prefixes,
        context="plugins.cfg",
    )
    return plugins, len(pkgconfig_files)


def regular_macho_files(
    package_folder: Path,
    file_tool: str,
) -> list[Path]:
    candidates = sorted(
        path
        for path in package_folder.rglob("*")
        if path.is_file() and not path.is_symlink()
    )
    macho_files: list[Path] = []
    for path in candidates:
        output = run([file_tool, "-b", str(path)]).strip()
        if output.startswith("Mach-O"):
            if (
                "64-bit" not in output
                or "arm64" not in output
                or "x86_64" in output
            ):
                raise VerificationError(
                    f"{path} is not a native arm64 Mach-O: {output}"
                )
            macho_files.append(path)
    if not macho_files:
        raise VerificationError("package contains no Mach-O files")
    return macho_files


def parse_build_version(output: str, path: Path) -> tuple[str, str]:
    platform_match = re.search(
        r"(?m)^\s*platform\s+([A-Za-z0-9_]+)\s*$",
        output,
    )
    minos_match = re.search(
        r"(?m)^\s*minos\s+([0-9.]+)\s*$",
        output,
    )
    if platform_match is None or minos_match is None:
        raise VerificationError(
            f"vtool did not report one build version for {path}"
        )
    return platform_match.group(1), minos_match.group(1)


def dependency_paths(output: str) -> list[str]:
    paths: list[str] = []
    for line in output.splitlines()[1:]:
        stripped = line.strip()
        if not stripped:
            continue
        path, separator, _ = stripped.partition(" (")
        if not separator:
            raise VerificationError(
                f"cannot parse otool dependency line: {line!r}"
            )
        paths.append(path)
    return paths


def otool_payload(output: str) -> str:
    """Drop otool's first-line input filename before prefix inspection."""

    lines = output.splitlines()
    return "\n".join(lines[1:]) if lines else ""


def runtime_search_paths(output: str) -> list[str]:
    lines = output.splitlines()
    paths: list[str] = []
    for index, line in enumerate(lines):
        if line.strip() != "cmd LC_RPATH":
            continue
        for candidate in lines[index + 1 : index + 6]:
            match = re.match(r"^\s*path\s+(.+?)\s+\(offset\s+\d+\)\s*$", candidate)
            if match is not None:
                paths.append(match.group(1))
                break
        else:
            raise VerificationError(
                "otool reported LC_RPATH without a parseable path"
            )
    return paths


def resolve_runtime_search_path(
    binary_path: Path,
    runtime_path: str,
) -> Path:
    if runtime_path == "@loader_path":
        relative_path = "."
    elif runtime_path.startswith("@loader_path/"):
        relative_path = runtime_path.removeprefix("@loader_path/")
    else:
        raise VerificationError(
            f"{binary_path} has an unsupported LC_RPATH: {runtime_path}"
        )
    return (binary_path.parent / relative_path).resolve(strict=False)


def assert_runtime_search_paths(
    package_folder: Path,
    binary_path: Path,
    runtime_paths: Sequence[str],
) -> None:
    unexpected_runtime_paths = (
        set(runtime_paths) - ALLOWED_RUNTIME_SEARCH_PATHS
    )
    if unexpected_runtime_paths:
        raise VerificationError(
            f"{binary_path} embeds unsafe LC_RPATH entries: "
            f"{sorted(unexpected_runtime_paths)}"
        )

    package_resolved = package_folder.resolve(strict=True)
    for runtime_path in runtime_paths:
        resolved_path = resolve_runtime_search_path(
            binary_path,
            runtime_path,
        )
        if (
            resolved_path != package_resolved
            and package_resolved not in resolved_path.parents
        ):
            raise VerificationError(
                f"{binary_path} has LC_RPATH {runtime_path!r}, which "
                f"resolves outside the package to {resolved_path}"
            )


def assert_binary_metadata(
    package_folder: Path,
    macho_files: Sequence[Path],
    *,
    forbidden_prefixes: Sequence[str],
    otool: str,
    strings_tool: str,
    vtool: str,
) -> list[str]:
    source_path_files: list[str] = []
    for path in macho_files:
        build_output = run([vtool, "-show-build", str(path)])
        platform_name, minimum_os = parse_build_version(
            build_output,
            path,
        )
        if platform_name != "MACOS":
            raise VerificationError(
                f"{path} targets {platform_name}, not MACOS"
            )
        if minimum_os != EXPECTED_DEPLOYMENT_TARGET:
            raise VerificationError(
                f"{path} has minos {minimum_os}, expected "
                f"{EXPECTED_DEPLOYMENT_TARGET}"
            )

        linked_output = run([otool, "-L", str(path)])
        assert_no_forbidden_prefixes(
            otool_payload(linked_output),
            forbidden_prefixes,
            context=f"{path} dependency metadata",
        )
        for dependency in dependency_paths(linked_output):
            if is_safe_rpath_dylib(dependency):
                if not packaged_rpath_dependency(
                    package_folder,
                    dependency,
                ):
                    raise VerificationError(
                        f"{path} references an absent package dylib: "
                        f"{dependency}"
                    )
                continue
            if is_safe_system_dependency(dependency):
                continue
            raise VerificationError(
                f"{path} has a non-relocatable dependency: {dependency}"
            )

        load_commands = run([otool, "-l", str(path)])
        assert_no_forbidden_prefixes(
            otool_payload(load_commands),
            forbidden_prefixes,
            context=f"{path} load commands",
        )
        runtime_paths = runtime_search_paths(load_commands)
        assert_runtime_search_paths(
            package_folder,
            path,
            runtime_paths,
        )

        if path.suffix == ".dylib":
            install_name_output = run([otool, "-D", str(path)])
            assert_no_forbidden_prefixes(
                otool_payload(install_name_output),
                forbidden_prefixes,
                context=f"{path} install-name metadata",
            )
            id_lines = [
                line.strip()
                for line in install_name_output.splitlines()[1:]
                if line.strip()
            ]
            if len(id_lines) != 1:
                raise VerificationError(
                    f"{path} has {len(id_lines)} install names"
                )
            expected_install_name = f"@rpath/{path.name}"
            if (
                not is_safe_rpath_dylib(id_lines[0])
                or id_lines[0] != expected_install_name
            ):
                raise VerificationError(
                    f"{path} has unexpected install name {id_lines[0]!r}; "
                    f"expected {expected_install_name!r}"
                )

        embedded_text = run([strings_tool, str(path)])
        if matching_forbidden_prefixes(
            embedded_text,
            forbidden_prefixes,
        ):
            source_path_files.append(
                path.relative_to(package_folder).as_posix()
            )
    return source_path_files


def assert_symlinks(package_folder: Path) -> int:
    symlinks = sorted(
        path for path in package_folder.rglob("*") if path.is_symlink()
    )
    package_resolved = package_folder.resolve()
    for path in symlinks:
        target_text = os.readlink(path)
        if os.path.isabs(target_text):
            raise VerificationError(
                f"package symlink has an absolute target: {path}"
            )
        try:
            resolved = path.resolve(strict=True)
        except OSError as error:
            raise VerificationError(
                f"package symlink is broken: {path}: {error}"
            ) from error
        if (
            resolved != package_resolved
            and package_resolved not in resolved.parents
        ):
            raise VerificationError(
                f"package symlink escapes package root: {path}"
            )
    return len(symlinks)


def assert_relocated_runtime(
    package_folder: Path,
    plugins: set[str],
) -> None:
    plugin_dir = package_folder / "lib" / "OGRE"
    env = os.environ.copy()
    env.pop("DYLD_LIBRARY_PATH", None)
    env.pop("DYLD_FALLBACK_LIBRARY_PATH", None)
    source = (
        "import ctypes, sys; "
        "ctypes.CDLL(sys.argv[1], mode=ctypes.RTLD_LOCAL)"
    )
    for plugin in sorted(plugins):
        run(
            [
                sys.executable,
                "-c",
                source,
                str(plugin_dir / f"{plugin}.dylib"),
            ],
            env=env,
        )
    installed_tool = (
        package_folder / "bin" / "macosx" / "OgreMeshUpgrader"
    )
    if not installed_tool.is_file():
        raise VerificationError(
            "package has no installed OgreMeshUpgrader relocation probe"
        )
    try:
        tool_result = subprocess.run(
            [str(installed_tool), "--help"],
            check=False,
            cwd=package_folder / "bin",
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=30,
        )
    except subprocess.TimeoutExpired as error:
        raise VerificationError(
            "relocated OgreMeshUpgrader timed out"
        ) from error
    combined_output = tool_result.stdout + tool_result.stderr
    if (
        tool_result.returncode != 1
        or "Invalid option --help" not in combined_output
        or "dyld" in combined_output.lower()
    ):
        raise VerificationError(
            "relocated OgreMeshUpgrader did not reach its argument parser: "
            f"status={tool_result.returncode}, output={combined_output!r}"
        )


def verify(package_folder: Path, *, load_plugins: bool) -> dict[str, object]:
    if sys.platform != "darwin":
        raise VerificationError(
            "native package assertion must run on macOS"
        )
    input_package_folder = Path(os.path.abspath(os.fspath(package_folder)))
    original_forbidden_prefixes = forbidden_cache_prefixes(
        input_package_folder
    )
    package_folder = input_package_folder.resolve(strict=True)
    if not package_folder.is_dir():
        raise VerificationError(
            f"package folder is not a directory: {package_folder}"
        )

    plugins, pkgconfig_count = assert_text_metadata(
        package_folder,
        original_forbidden_prefixes,
    )
    symlink_count = assert_symlinks(package_folder)
    file_tool = require_tool("file")
    otool = require_tool("otool")
    strings_tool = require_tool("strings")
    vtool = require_tool("vtool")
    macho_files = regular_macho_files(package_folder, file_tool)
    embedded_source_path_files = assert_binary_metadata(
        package_folder,
        macho_files,
        forbidden_prefixes=original_forbidden_prefixes,
        otool=otool,
        strings_tool=strings_tool,
        vtool=vtool,
    )
    relocation_checked = False
    if load_plugins:
        with tempfile.TemporaryDirectory(
            prefix="ogre-native-relocation-"
        ) as temporary_directory:
            relocated_package = (
                Path(temporary_directory) / "relocated-ogre3d"
            )
            shutil.copytree(
                package_folder,
                relocated_package,
                symlinks=True,
            )
            relocated_macho_files = regular_macho_files(
                relocated_package,
                file_tool,
            )
            if len(relocated_macho_files) != len(macho_files):
                raise VerificationError(
                    "relocated package changed the Mach-O file count"
                )
            relocated_forbidden_prefixes = frozenset(
                set(original_forbidden_prefixes)
                | set(path_spelling_variants(relocated_package))
            )
            relocated_source_path_files = assert_binary_metadata(
                relocated_package,
                relocated_macho_files,
                forbidden_prefixes=relocated_forbidden_prefixes,
                otool=otool,
                strings_tool=strings_tool,
                vtool=vtool,
            )
            if relocated_source_path_files != embedded_source_path_files:
                raise VerificationError(
                    "relocation changed the non-runtime embedded source-path "
                    "inventory"
                )
            assert_relocated_runtime(relocated_package, plugins)
            relocation_checked = True

    return {
        "architecture": "arm64",
        "deployment_target": EXPECTED_DEPLOYMENT_TARGET,
        "embedded_nonruntime_source_path_files": (
            embedded_source_path_files
        ),
        "macho_file_count": len(macho_files),
        "ogre_version": EXPECTED_VERSION,
        "package_folder": str(package_folder),
        "pkgconfig_file_count": pkgconfig_count,
        "plugin_load_checked": load_plugins,
        "plugins": sorted(plugins),
        "relocation_checked": relocation_checked,
        "status": "ok",
        "symlink_count": symlink_count,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_folder", type=Path)
    parser.add_argument(
        "--skip-plugin-load",
        action="store_true",
        help="skip isolated dlopen checks (metadata checks still run)",
    )
    arguments = parser.parse_args()
    try:
        report = verify(
            arguments.package_folder,
            load_plugins=not arguments.skip_plugin_load,
        )
    except (OSError, VerificationError) as error:
        print(
            f"OGRE native package assertion failed: {error}",
            file=sys.stderr,
        )
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
