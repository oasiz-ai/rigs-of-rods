#!/usr/bin/env python3
"""Audit and smoke-test a relocated native OGRE 14 RoR installation."""

from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
import json
import ntpath
import os
from pathlib import Path
import posixpath
import re
import shutil
import subprocess
import sys
from typing import Mapping, Sequence


EXPECTED_PLUGINS = {
    "linux-x86_64": (
        "Codec_FreeImage",
        "RenderSystem_GL3Plus",
        "Plugin_ParticleFX",
        "Plugin_OctreeSceneManager",
    ),
    "windows-x86_64": (
        "Codec_FreeImage",
        "RenderSystem_Direct3D11",
        "Plugin_ParticleFX",
        "Plugin_OctreeSceneManager",
    ),
}
EXPECTED_PLUGIN_FOLDERS = {
    "linux-x86_64": "lib/OGRE",
    "windows-x86_64": ".",
}
WINDOWS_SYSTEM_DLLS = frozenset(
    {
        "advapi32.dll",
        "bcrypt.dll",
        "cfgmgr32.dll",
        "comctl32.dll",
        "comdlg32.dll",
        "crypt32.dll",
        "d3d11.dll",
        "d3dcompiler_47.dll",
        "dbghelp.dll",
        "dinput8.dll",
        "dwmapi.dll",
        "dxgi.dll",
        "gdi32.dll",
        "imm32.dll",
        "iphlpapi.dll",
        "kernel32.dll",
        "msvcp140.dll",
        "normaliz.dll",
        "ntdll.dll",
        "ole32.dll",
        "oleaut32.dll",
        "opengl32.dll",
        "powrprof.dll",
        "psapi.dll",
        "rpcrt4.dll",
        "secur32.dll",
        "setupapi.dll",
        "shell32.dll",
        "shlwapi.dll",
        "ucrtbase.dll",
        "user32.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "version.dll",
        "winhttp.dll",
        "winmm.dll",
        "ws2_32.dll",
    }
)
SYSTEM_ELF_PREFIXES = (
    Path("/lib"),
    Path("/lib64"),
    Path("/usr/lib"),
    Path("/usr/lib64"),
)
PLUGIN_TOKEN_RE = re.compile(r"^[A-Za-z0-9_-]+$")
WINDOWS_DLL_RE = re.compile(r"^[A-Za-z0-9_.+-]+\.dll$", re.IGNORECASE)


class AuditError(RuntimeError):
    """The relocated package violated the native runtime contract."""


@dataclass(frozen=True)
class PluginConfig:
    folder: str
    plugins: tuple[str, ...]


def normalized_text(value: str) -> str:
    """Normalize path separators and case for hostile-prefix checks."""

    return value.replace("\\", "/").casefold()


def is_within(root: Path, candidate: Path) -> bool:
    """Return whether an existing/resolved candidate stays inside root."""

    root = root.resolve(strict=True)
    candidate = candidate.resolve(strict=True)
    return candidate == root or root in candidate.parents


def assert_clean_text(
    text: str,
    *,
    context: str,
    forbidden_prefixes: Sequence[str],
) -> None:
    """Reject cache/build paths and caller-provided prefixes in metadata."""

    if "\x00" in text:
        raise AuditError(f"{context} contains a NUL byte")
    normalized = normalized_text(text)
    forbidden_tokens = (
        "/.conan2/",
        "/.ci-conan/",
        "/build-ogre14-",
        "/stage-ogre14-",
    )
    for token in forbidden_tokens:
        if token in normalized:
            raise AuditError(f"{context} retains forbidden path token {token}")
    for prefix in forbidden_prefixes:
        if not prefix:
            continue
        expanded_prefix = os.path.expanduser(prefix)
        normalized_prefixes = {
            normalized_text(expanded_prefix).rstrip("/"),
            normalized_text(os.path.abspath(expanded_prefix)).rstrip("/"),
        }
        for normalized_prefix in normalized_prefixes:
            if normalized_prefix and normalized_prefix in normalized:
                raise AuditError(
                    f"{context} retains forbidden prefix {prefix!r}"
                )


def parse_plugins_config(
    text: str,
    *,
    expected_folder: str,
    expected_plugins: Sequence[str],
    context: str,
    forbidden_prefixes: Sequence[str] = (),
) -> PluginConfig:
    """Parse only active OGRE plugin entries and enforce the exact contract."""

    assert_clean_text(
        text,
        context=context,
        forbidden_prefixes=forbidden_prefixes,
    )
    folders: list[str] = []
    plugins: list[str] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator:
            raise AuditError(
                f"{context} has malformed active directive {line!r}"
            )
        key = key.strip()
        value = value.strip()
        if key == "PluginFolder":
            folders.append(value)
        elif key == "Plugin":
            if not PLUGIN_TOKEN_RE.fullmatch(value):
                raise AuditError(
                    f"{context} has unsafe plugin token {value!r}"
                )
            if value in plugins:
                raise AuditError(
                    f"{context} activates plugin more than once: {value}"
                )
            plugins.append(value)
        else:
            raise AuditError(
                f"{context} has unknown active directive {key!r}"
            )
    if folders != [expected_folder]:
        raise AuditError(
            f"{context} PluginFolder changed: expected "
            f"{expected_folder!r}, found {folders!r}"
        )
    if tuple(plugins) != tuple(expected_plugins):
        raise AuditError(
            f"{context} active plugins changed: expected "
            f"{tuple(expected_plugins)!r}, found {tuple(plugins)!r}"
        )
    return PluginConfig(folder=folders[0], plugins=tuple(plugins))


def validate_symlinks(root: Path) -> int:
    """Reject broken, absolute, and package-escaping symlink targets."""

    root = root.resolve(strict=True)
    count = 0
    for path in sorted(root.rglob("*")):
        if not path.is_symlink():
            continue
        count += 1
        target = os.readlink(path)
        if posixpath.isabs(target) or ntpath.isabs(target):
            raise AuditError(f"absolute package symlink: {path} -> {target}")
        try:
            resolved = path.resolve(strict=True)
        except OSError as error:
            raise AuditError(f"broken package symlink: {path}: {error}") from error
        if not is_within(root, resolved):
            raise AuditError(f"package symlink escapes root: {path} -> {target}")
    return count


def run(
    command: Sequence[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    timeout: int = 60,
) -> subprocess.CompletedProcess[str]:
    """Run a bounded command and surface complete diagnostics on failure."""

    try:
        result = subprocess.run(
            list(command),
            check=False,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AuditError(f"command failed to run: {command!r}: {error}") from error
    if result.returncode != 0:
        raise AuditError(
            f"command exited {result.returncode}: {command!r}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def resolve_origin_search_path(binary: Path, entry: str, root: Path) -> Path:
    """Resolve one ELF RPATH/RUNPATH entry and keep it package-local."""

    if "\\" in entry:
        raise AuditError(f"{binary} has backslash in ELF search path {entry!r}")
    match = re.fullmatch(r"(?:\$ORIGIN|\$\{ORIGIN\})(?P<suffix>/.*)?", entry)
    if match is None:
        raise AuditError(f"{binary} has non-relative search path {entry!r}")
    suffix = (match.group("suffix") or "").lstrip("/")
    if "$" in suffix:
        raise AuditError(f"{binary} has expanded variable in search path {entry!r}")
    candidate = (binary.parent / suffix).resolve(strict=False)
    root = root.resolve(strict=True)
    if candidate != root and root not in candidate.parents:
        raise AuditError(f"{binary} search path escapes package: {entry!r}")
    return candidate


def parse_elf_dynamic_paths(output: str) -> tuple[list[str], list[str]]:
    """Return DT_NEEDED basenames and RPATH/RUNPATH entries."""

    needed = re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", output)
    search_paths: list[str] = []
    for payload in re.findall(
        r"\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]",
        output,
    ):
        search_paths.extend(entry for entry in payload.split(":") if entry)
    for dependency in needed:
        if (
            dependency in {".", ".."}
            or "/" in dependency
            or "\\" in dependency
        ):
            raise AuditError(f"ELF DT_NEEDED is not a basename: {dependency!r}")
    return needed, search_paths


def parse_ldd_paths(output: str) -> tuple[list[Path], list[str]]:
    """Parse resolved paths and unresolved basenames from ldd output."""

    paths: list[Path] = []
    unresolved: list[str] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("linux-vdso"):
            continue
        if "=> not found" in line:
            unresolved.append(line.partition("=>")[0].strip())
            continue
        if "=>" in line:
            resolved = line.partition("=>")[2].strip().split(maxsplit=1)[0]
        elif line.startswith("/"):
            resolved = line.split(maxsplit=1)[0]
        else:
            continue
        if resolved.startswith("/"):
            paths.append(Path(resolved))
    return paths, unresolved


def is_system_elf_path(path: Path) -> bool:
    """Recognize only the target distribution's conventional ABI roots."""

    absolute = path.resolve(strict=True)
    for prefix in SYSTEM_ELF_PREFIXES:
        try:
            absolute.relative_to(prefix.resolve(strict=True))
            return True
        except (FileNotFoundError, ValueError):
            continue
    return False


def linux_elf_files(root: Path, executable: Path) -> list[Path]:
    """Find unique real ELF candidates in the installed runtime closure."""

    candidates = [executable]
    public_executable = root / "RoR"
    if public_executable.is_file() and public_executable != executable:
        candidates.append(public_executable)
    ogre_next_executable = root / "RoR-OgreNext"
    if ogre_next_executable.is_file() and ogre_next_executable not in candidates:
        candidates.append(ogre_next_executable)
    library_root = root / "lib"
    if library_root.is_dir():
        candidates.extend(
            path
            for path in library_root.rglob("*")
            if path.is_file() and ".so" in path.name
        )
    unique: dict[Path, Path] = {}
    for candidate in candidates:
        real = candidate.resolve(strict=True)
        unique.setdefault(real, candidate)
    return sorted(unique)


def linux_loader_environment(
    root: Path,
    base_environment: dict[str, str] | None = None,
) -> dict[str, str]:
    """Build a deterministic loader environment rooted in the relocation."""

    loader_env = dict(
        os.environ if base_environment is None else base_environment
    )
    for variable in (
        "LD_AUDIT",
        "LD_DEBUG",
        "LD_DEBUG_OUTPUT",
        "LD_PRELOAD",
    ):
        loader_env.pop(variable, None)
    loader_env["LANG"] = "C"
    loader_env["LC_ALL"] = "C"
    loader_env["LD_LIBRARY_PATH"] = str(root.resolve(strict=True) / "lib")
    return loader_env


def audit_linux_elf(
    root: Path,
    executable: Path,
    *,
    forbidden_prefixes: Sequence[str],
) -> dict[str, int]:
    """Verify x86_64 ELF identity, metadata, and ldd closure."""

    readelf = shutil.which("readelf")
    ldd = shutil.which("ldd")
    if readelf is None or ldd is None:
        raise AuditError("Linux runtime audit requires readelf and ldd")
    files = linux_elf_files(root, executable)
    if not files:
        raise AuditError("installed package contains no ELF runtime files")
    package_resolved = root.resolve(strict=True)
    loader_env = linux_loader_environment(root)
    dependency_edges = 0
    for path in files:
        metadata = run(
            [readelf, "-h", "-d", str(path)],
            env=loader_env,
        ).stdout
        if not re.search(
            r"Machine:\s+Advanced Micro Devices X86-64",
            metadata,
        ):
            raise AuditError(f"{path} is not an x86_64 ELF binary")
        assert_clean_text(
            metadata,
            context=f"{path} ELF loader metadata",
            forbidden_prefixes=forbidden_prefixes,
        )
        needed, search_paths = parse_elf_dynamic_paths(metadata)
        dependency_edges += len(needed)
        for entry in search_paths:
            resolve_origin_search_path(path, entry, root)

        ldd_result = run([ldd, str(path)], env=loader_env)
        resolved_paths, unresolved = parse_ldd_paths(
            ldd_result.stdout + ldd_result.stderr
        )
        if unresolved:
            raise AuditError(
                f"{path} has unresolved ELF dependencies: {unresolved}"
            )
        for dependency in resolved_paths:
            dependency_resolved = dependency.resolve(strict=True)
            if (
                dependency_resolved == package_resolved
                or package_resolved in dependency_resolved.parents
                or is_system_elf_path(dependency_resolved)
            ):
                continue
            raise AuditError(
                f"{path} resolves a dependency outside package/system roots: "
                f"{dependency}"
            )
    return {"elf_files": len(files), "elf_dependency_edges": dependency_edges}


def parse_dumpbin_dependents(output: str) -> tuple[str, ...]:
    """Extract safe DLL basenames from dumpbin dependency/import output."""

    dependencies: list[str] = []
    for line in output.splitlines():
        candidate = line.strip()
        if WINDOWS_DLL_RE.fullmatch(candidate):
            dependencies.append(candidate)
    return tuple(dict.fromkeys(dependencies))


def dumpbin_payload(output: str) -> str:
    """Remove dumpbin's command-input header before metadata path checks."""

    return "\n".join(
        line
        for line in output.splitlines()
        if not line.lstrip().casefold().startswith("dump of file ")
    )


def assert_amd64_pe(output: str, path: Path) -> None:
    """Require dumpbin's AMD64/8664 machine identity."""

    if not re.search(
        r"(?im)^\s*8664 machine \(x64\)\s*$",
        output,
    ):
        raise AuditError(f"{path} is not an AMD64 PE binary")


def is_windows_system_dependency(name: str, *, system_root: Path | None) -> bool:
    """Recognize Windows API-set or known operating-system DLLs."""

    lowered = name.casefold()
    if not WINDOWS_DLL_RE.fullmatch(name):
        return False
    if lowered.startswith(("api-ms-win-", "ext-ms-win-")):
        return True
    if lowered in WINDOWS_SYSTEM_DLLS:
        return True
    if system_root is not None:
        return (system_root / "System32" / name).is_file()
    return False


def assert_windows_dependency_closure(
    path: Path,
    dependencies: Sequence[str],
    packaged_dlls: Mapping[str, Path],
    *,
    system_root: Path | None,
) -> None:
    """Require every normal or delay-load import in package or System32."""

    for dependency in dependencies:
        if dependency.casefold() in packaged_dlls:
            continue
        if is_windows_system_dependency(
            dependency,
            system_root=system_root,
        ):
            continue
        raise AuditError(
            f"{path} imports unpackaged non-system DLL {dependency}"
        )


def windows_pe_files(root: Path) -> list[Path]:
    """Return every regular executable or DLL in the flat Windows package."""

    files = sorted(
        path
        for path in root.rglob("*")
        if path.is_file() and path.suffix.casefold() in {".exe", ".dll"}
    )
    nested = [
        path.relative_to(root).as_posix()
        for path in files
        if path.parent.resolve(strict=True) != root.resolve(strict=True)
    ]
    if nested:
        raise AuditError(
            "Windows PE runtime files are not flat beside RoR.exe: "
            f"{nested}"
        )
    return files


def audit_windows_pe(
    root: Path,
    *,
    forbidden_prefixes: Sequence[str],
) -> dict[str, int]:
    """Verify AMD64 PE identity and the complete packaged import closure."""

    dumpbin = shutil.which("dumpbin")
    if dumpbin is None:
        raise AuditError("Windows runtime audit requires dumpbin")
    files = windows_pe_files(root)
    if not files:
        raise AuditError("installed package contains no PE runtime files")
    dlls: dict[str, Path] = {}
    for path in files:
        if path.suffix.casefold() != ".dll":
            continue
        lowered = path.name.casefold()
        if lowered in dlls:
            raise AuditError(f"duplicate packaged DLL basename: {path.name}")
        dlls[lowered] = path
    system_root_text = os.environ.get("SystemRoot")
    system_root = Path(system_root_text) if system_root_text else None
    dependency_edges = 0
    for path in files:
        headers = run([dumpbin, "/nologo", "/headers", str(path)]).stdout
        assert_amd64_pe(headers, path)
        dependents_output = run(
            [dumpbin, "/nologo", "/dependents", str(path)]
        ).stdout
        imports_output = run(
            [dumpbin, "/nologo", "/imports", str(path)]
        ).stdout
        for label, output in (
            ("dependent", dependents_output),
            ("normal/delay import", imports_output),
        ):
            assert_clean_text(
                dumpbin_payload(output),
                context=f"{path} PE {label} metadata",
                forbidden_prefixes=forbidden_prefixes,
            )
        dependencies = tuple(
            dict.fromkeys(
                (
                    *parse_dumpbin_dependents(dependents_output),
                    *parse_dumpbin_dependents(imports_output),
                )
            )
        )
        dependency_edges += len(dependencies)
        assert_windows_dependency_closure(
            path,
            dependencies,
            dlls,
            system_root=system_root,
        )
    return {"pe_files": len(files), "pe_dependency_edges": dependency_edges}


def is_forbidden_plugin_binary_name(name: str, platform: str) -> bool:
    """Recognize renderer/Cg/debug binaries outside the selected contract."""

    lowered = name.casefold()
    if "_d_d" in lowered or "cgprogrammanager" in lowered:
        return True
    if platform == "linux-x86_64":
        if "rendersystem_direct3d" in lowered or "rendersystem_metal" in lowered:
            return True
        return re.match(r"^rendersystem_gl(?:\.|_d\.)", lowered) is not None
    return "rendersystem_gl" in lowered or "rendersystem_metal" in lowered


def assert_no_forbidden_plugin_binaries(root: Path, platform: str) -> None:
    """Reject debug-double-suffix and renderer/Cg drift."""

    offenders = [
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file()
        and is_forbidden_plugin_binary_name(path.name, platform)
    ]
    if offenders:
        raise AuditError(f"forbidden plugin binaries present: {offenders}")


def assert_exact_plugin_binary_families(
    root: Path,
    platform: str,
    expected_plugins: Sequence[str],
) -> int:
    """Allow only configured Release plugin families and Linux SONAME links."""

    if platform == "linux-x86_64":
        plugin_dir = root / "lib" / "OGRE"
        if not plugin_dir.is_dir():
            raise AuditError(f"active plugin directory is absent: {plugin_dir}")
        entries = sorted(plugin_dir.iterdir())
        expected_names = {
            filename
            for plugin in expected_plugins
            for filename in (
                f"{plugin}.so",
                f"{plugin}.so.14.5",
                f"{plugin}.so.14.5.2",
            )
        }
        found_names = {path.name for path in entries}
        if found_names != expected_names:
            raise AuditError(
                "Linux OGRE 14.5.2 plugin SONAME set changed: expected "
                f"{sorted(expected_names)}, found {sorted(found_names)}"
            )
        for plugin in expected_plugins:
            unversioned = plugin_dir / f"{plugin}.so"
            soname = plugin_dir / f"{plugin}.so.14.5"
            real = plugin_dir / f"{plugin}.so.14.5.2"
            for link, expected_target in (
                (unversioned, soname.name),
                (soname, real.name),
            ):
                if (
                    not link.is_symlink()
                    or os.readlink(link) != expected_target
                ):
                    raise AuditError(
                        "Linux OGRE plugin has a non-canonical relative "
                        f"SONAME link: {link}"
                    )
            if not real.is_file() or real.is_symlink():
                raise AuditError(
                    "Linux OGRE plugin has no regular 14.5.2 binary: "
                    f"{real}"
                )
        return len(entries)

    expected_dlls = {
        f"{plugin}.dll".casefold() for plugin in expected_plugins
    }
    plugin_dlls = [
        path
        for path in root.iterdir()
        if path.is_file()
        and path.suffix.casefold() == ".dll"
        and path.name.casefold().startswith(
            ("codec_", "rendersystem_", "plugin_")
        )
    ]
    unexpected = sorted(
        path.name
        for path in plugin_dlls
        if path.name.casefold() not in expected_dlls
    )
    if unexpected:
        raise AuditError(
            "unexpected Windows OGRE plugin families or variants: "
            f"{unexpected}"
        )
    found_dlls = {path.name.casefold() for path in plugin_dlls}
    if found_dlls != expected_dlls:
        raise AuditError(
            "Windows OGRE plugin DLL set changed: expected "
            f"{sorted(expected_dlls)}, found {sorted(found_dlls)}"
        )
    return len(plugin_dlls)


def active_plugin_paths(
    root: Path,
    platform: str,
    plugins: Sequence[str],
) -> tuple[Path, ...]:
    """Resolve the exact active plugin binaries without accepting aliases."""

    if platform == "linux-x86_64":
        plugin_dir = root / "lib" / "OGRE"
        extension = ".so"
    else:
        plugin_dir = root
        extension = ".dll"
    if not plugin_dir.is_dir():
        raise AuditError(f"active plugin directory is absent: {plugin_dir}")
    active: list[Path] = []
    for plugin in plugins:
        path = plugin_dir / f"{plugin}{extension}"
        if not path.is_file():
            raise AuditError(f"active plugin binary is absent: {path}")
        resolved = path.resolve(strict=True)
        if not is_within(plugin_dir, resolved):
            raise AuditError(f"active plugin escapes plugin directory: {path}")
        active.append(path)
    return tuple(active)


def load_active_plugins(
    root: Path,
    platform: str,
    plugins: Sequence[Path],
) -> None:
    """Ask the native loader to resolve every active plugin dependency."""

    handles: list[object] = []
    try:
        if platform == "windows-x86_64":
            if os.name != "nt":
                raise AuditError("Windows plugin loading requires Windows")
            add_directory = getattr(os, "add_dll_directory", None)
            if add_directory is None:
                raise AuditError("Python has no secure DLL-directory API")
            with add_directory(str(root)):
                for path in plugins:
                    handles.append(ctypes.WinDLL(str(path)))
        else:
            if not sys.platform.startswith("linux"):
                raise AuditError("Linux plugin loading requires Linux")
            for path in plugins:
                handles.append(
                    ctypes.CDLL(str(path), mode=ctypes.RTLD_LOCAL)
                )
    except OSError as error:
        raise AuditError(f"native loader rejected active plugin: {error}") from error
    if len(handles) != len(plugins):
        raise AuditError("native loader did not retain every active plugin")


def smoke_command(
    command: Sequence[str],
    *,
    label: str,
    expected_markers: Sequence[str],
    root: Path,
    log_dir: Path,
) -> None:
    """Run a cwd-independent, UI-free CLI smoke and persist diagnostics."""

    log_dir.mkdir(parents=True, exist_ok=True)
    smoke_home = log_dir / "user-home"
    smoke_cwd = log_dir / "outside-package-cwd"
    smoke_home.mkdir(exist_ok=True)
    smoke_cwd.mkdir(exist_ok=True)
    env = os.environ.copy()
    for variable in (
        "LD_LIBRARY_PATH",
        "DYLD_LIBRARY_PATH",
        "DYLD_FALLBACK_LIBRARY_PATH",
    ):
        env.pop(variable, None)
    env["HOME"] = str(smoke_home)
    if os.name == "nt":
        env["APPDATA"] = str(smoke_home / "AppData" / "Roaming")
        env["LOCALAPPDATA"] = str(smoke_home / "AppData" / "Local")
    try:
        result = subprocess.run(
            list(command),
            check=False,
            cwd=smoke_cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AuditError(f"{label} smoke failed to run: {error}") from error
    (log_dir / f"{label}.stdout.log").write_text(
        result.stdout,
        encoding="utf-8",
    )
    (log_dir / f"{label}.stderr.log").write_text(
        result.stderr,
        encoding="utf-8",
    )
    combined = result.stdout + result.stderr
    if result.returncode != 0:
        raise AuditError(
            f"{label} smoke exited {result.returncode}: {combined!r}"
        )
    missing = [marker for marker in expected_markers if marker not in combined]
    if missing:
        raise AuditError(f"{label} smoke omitted markers: {missing}")
    if root.resolve(strict=True) == smoke_cwd.resolve(strict=True):
        raise AuditError(f"{label} smoke did not run outside package root")


def run_smokes(root: Path, platform: str, log_dir: Path) -> None:
    """Exercise commands that terminate before window/renderer creation."""

    executable = (
        root / "RunRoR"
        if platform == "linux-x86_64"
        else root / "RoR.exe"
    )
    smoke_command(
        [str(executable), "--help"],
        label="help",
        expected_markers=("Command Line Arguments", "--help (this)"),
        root=root,
        log_dir=log_dir,
    )
    smoke_command(
        [str(executable), "-version"],
        label="version",
        expected_markers=("Version Information: Rigs of Rods", "protocol version"),
        root=root,
        log_dir=log_dir,
    )


def audit(
    root: Path,
    platform: str,
    *,
    forbidden_prefixes: Sequence[str],
    load_plugins: bool,
    smoke: bool,
    log_dir: Path,
) -> dict[str, object]:
    """Run the complete platform-specific relocated runtime audit."""

    input_root = Path(os.path.abspath(os.fspath(root)))
    if not input_root.is_dir():
        raise AuditError(f"runtime root is not a directory: {input_root}")
    root = input_root.resolve(strict=True)
    expected_plugins = EXPECTED_PLUGINS[platform]
    expected_folder = EXPECTED_PLUGIN_FOLDERS[platform]
    configs: list[PluginConfig] = []
    for filename in ("plugins.cfg", "plugins_d.cfg"):
        path = root / filename
        if not path.is_file():
            raise AuditError(f"runtime config is absent: {path}")
        configs.append(
            parse_plugins_config(
                path.read_text(encoding="utf-8"),
                expected_folder=expected_folder,
                expected_plugins=expected_plugins,
                context=filename,
                forbidden_prefixes=forbidden_prefixes,
            )
        )
    if configs[0] != configs[1]:
        raise AuditError("Release and Debug plugin contracts differ")

    symlinks = validate_symlinks(root)
    plugin_binary_files = assert_exact_plugin_binary_families(
        root,
        platform,
        expected_plugins,
    )
    assert_no_forbidden_plugin_binaries(root, platform)
    plugins = active_plugin_paths(root, platform, expected_plugins)
    if platform == "linux-x86_64":
        public_executable = root / "RoR"
        compatibility_executable = root / "RoR-Ogre14"
        executable = (
            compatibility_executable
            if compatibility_executable.exists()
            else public_executable
        )
        launcher = root / "RunRoR"
        if not public_executable.is_file() or not os.access(
            public_executable, os.X_OK
        ):
            raise AuditError("installed Linux public RoR executable is unavailable")
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise AuditError("installed Linux OGRE 14 executable is unavailable")
        if not launcher.is_file() or not os.access(launcher, os.X_OK):
            raise AuditError("installed Linux RunRoR launcher is unavailable")
        metadata = audit_linux_elf(
            root,
            executable,
            forbidden_prefixes=forbidden_prefixes,
        )
    else:
        executable = root / "RoR.exe"
        if not executable.is_file():
            raise AuditError("installed Windows RoR.exe is unavailable")
        compatibility_executable = root / "RoR-Ogre14.exe"
        if (
            compatibility_executable.exists()
            and not compatibility_executable.is_file()
        ):
            raise AuditError("installed Windows RoR-Ogre14.exe is not a file")
        metadata = audit_windows_pe(
            root,
            forbidden_prefixes=forbidden_prefixes,
        )

    if load_plugins:
        load_active_plugins(root, platform, plugins)
    if smoke:
        run_smokes(root, platform, log_dir)
    return {
        "active_plugins": list(expected_plugins),
        "load_plugins": load_plugins,
        "platform": platform,
        "plugin_binary_files": plugin_binary_files,
        "plugin_folder": expected_folder,
        "root": str(root),
        "smoke": smoke,
        "symlinks": symlinks,
        **metadata,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument(
        "--platform",
        choices=sorted(EXPECTED_PLUGINS),
        required=True,
    )
    parser.add_argument(
        "--forbidden-prefix",
        action="append",
        default=[],
        help="path that must not occur in config or loader metadata",
    )
    parser.add_argument("--load-plugins", action="store_true")
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--log-dir", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    arguments = parser.parse_args()
    try:
        report = audit(
            arguments.root,
            arguments.platform,
            forbidden_prefixes=arguments.forbidden_prefix,
            load_plugins=arguments.load_plugins,
            smoke=arguments.smoke,
            log_dir=arguments.log_dir,
        )
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if arguments.report is not None:
            arguments.report.parent.mkdir(parents=True, exist_ok=True)
            arguments.report.write_text(payload, encoding="utf-8")
        print(payload, end="")
    except (AuditError, OSError, UnicodeError) as error:
        print(f"OGRE 14 runtime audit failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
