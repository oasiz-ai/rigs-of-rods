#!/usr/bin/env python3
"""Reject process-spawn calls from the probe-only child source closure."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


SCHEMA = "ror.ogre_next_child_runtime_single_process_closure.v1"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".m", ".mm"}
FORBIDDEN_CALL = re.compile(
    r"(?<![A-Za-z0-9_])(?:"
    r"fork|vfork|posix_spawn|posix_spawnp|"
    r"execl|execle|execlp|execv|execve|execvp|"
    r"CreateProcessA|CreateProcessW|ShellExecuteA|ShellExecuteW|WinExec|"
    r"_popen|popen|system"
    r")\s*\("
)
RAW_STRING_PREFIX = re.compile(
    r'(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\v\f\r\n]{0,16})\('
)
ROR_PATHS = (
    "source/main/gfx/render",
    "source/main/gfx/RendererBackendPolicy.cpp",
    "source/main/gfx/RendererStartupPlan.cpp",
    "source/main/gfx/RendererStartupHandoff.cpp",
    "source/main/system/RendererChildIntent.cpp",
    "source/main/system/RendererBridgeEndpoint.cpp",
    "source/main/system/RendererBridgeEndpoint.h",
    "source/main/system/RendererOgreNextChild.cpp",
    "source/main/system/RendererOgreNextChildMain.cpp",
)
OGRE_PATHS = (
    "OgreMain/include",
    "OgreMain/src",
    "Components/Hlms/Common",
    "Components/Hlms/Pbs",
    "Components/Hlms/Unlit",
    "Components/Overlay",
    "RenderSystems/Metal",
    "RenderSystems/Direct3D11",
    "RenderSystems/Vulkan",
)


class ProcessClosureError(RuntimeError):
    """Raised when the child could create an uncontained descendant."""


def _blank_non_newlines(value: str) -> str:
    return "".join(character if character in "\r\n" else " " for character in value)


def _scrub_comments_and_literals(source: str) -> str:
    """Blank lexical comments/literals without allowing cross-token erasure.

    The output preserves byte-for-code-point offsets and line endings so a
    forbidden-call match can still be located.  A state machine is required:
    deleting block comments before strings lets a literal containing ``/*``
    consume real code through a later literal containing ``*/``.
    """

    output = list(source)
    length = len(source)
    index = 0

    def blank(start: int, end: int) -> None:
        output[start:end] = _blank_non_newlines(source[start:end])

    while index < length:
        if source.startswith("//", index):
            start = index
            index += 2
            while index < length:
                if source[index] == "\\" and index + 1 < length:
                    if source[index + 1] == "\n":
                        index += 2
                        continue
                    if (
                        source[index + 1] == "\r"
                        and index + 2 < length
                        and source[index + 2] == "\n"
                    ):
                        index += 3
                        continue
                if source[index] in "\r\n":
                    break
                index += 1
            blank(start, index)
            continue

        if source.startswith("/*", index):
            start = index
            close = source.find("*/", index + 2)
            if close < 0:
                raise ProcessClosureError(
                    "unterminated block comment in source closure"
                )
            index = close + 2
            blank(start, index)
            continue

        raw = RAW_STRING_PREFIX.match(source, index)
        if raw is not None and (
            index == 0 or not (source[index - 1].isalnum() or source[index - 1] == "_")
        ):
            terminator = ")" + raw.group("delimiter") + '"'
            close = source.find(terminator, raw.end())
            if close < 0:
                raise ProcessClosureError(
                    "unterminated raw string literal in source closure"
                )
            end = close + len(terminator)
            blank(index, end)
            index = end
            continue

        if (
            source[index] == "'"
            and index > 0
            and index + 1 < length
            and source[index - 1] in "0123456789abcdefABCDEF"
            and source[index + 1] in "0123456789abcdefABCDEF"
        ):
            index += 1
            continue

        if source[index] in "\"'":
            quote = source[index]
            start = index
            index += 1
            terminated = False
            while index < length:
                character = source[index]
                if character == "\\":
                    if index + 1 >= length:
                        break
                    index += 2
                    continue
                if character == quote:
                    index += 1
                    terminated = True
                    break
                if character in "\r\n":
                    break
                index += 1
            if not terminated:
                raise ProcessClosureError(
                    "unterminated quoted literal in source closure"
                )
            blank(start, index)
            continue

        index += 1

    return "".join(output)


def _source_files(root: Path, relative_paths: tuple[str, ...]) -> list[Path]:
    files: set[Path] = set()
    for relative in relative_paths:
        path = root / relative
        if path.is_symlink() or not path.exists():
            raise ProcessClosureError(
                f"process-closure source is missing or indirect: {relative}"
            )
        if path.is_file():
            if path.suffix.lower() in SOURCE_SUFFIXES:
                files.add(path)
            continue
        if not path.is_dir():
            raise ProcessClosureError(
                f"process-closure source is not a file or directory: {relative}"
            )
        for candidate in path.rglob("*"):
            if candidate.is_symlink():
                raise ProcessClosureError(
                    "process-closure source tree contains a symbolic link: "
                    + candidate.relative_to(root).as_posix()
                )
            if candidate.is_file() and candidate.suffix.lower() in SOURCE_SUFFIXES:
                files.add(candidate)
    return sorted(files, key=lambda item: item.relative_to(root).as_posix())


def verify_process_closure(ror_root: Path, ogre_root: Path) -> dict[str, object]:
    ror = ror_root.expanduser().resolve(strict=True)
    ogre = ogre_root.expanduser().resolve(strict=True)
    if not ror.is_dir() or not ogre.is_dir():
        raise ProcessClosureError("process-closure roots must be directories")
    entries: list[str] = []
    failures: list[str] = []
    for label, root, paths in (
        ("ror", ror, ROR_PATHS),
        ("ogre-next", ogre, OGRE_PATHS),
    ):
        for path in _source_files(root, paths):
            relative = path.relative_to(root).as_posix()
            try:
                payload = path.read_bytes()
                source = payload.decode("utf-8")
            except (OSError, UnicodeError) as error:
                raise ProcessClosureError(
                    f"could not read process-closure source {label}:{relative}: {error}"
                ) from error
            try:
                scrubbed = _scrub_comments_and_literals(source)
            except ProcessClosureError as error:
                raise ProcessClosureError(f"{error}: {label}:{relative}") from error
            match = FORBIDDEN_CALL.search(scrubbed)
            if match is not None:
                failures.append(f"{label}:{relative}:{match.group(0).rstrip()}")
            entries.append(
                f"{label}:{relative}|{len(payload)}|"
                f"{hashlib.sha256(payload).hexdigest()}\n"
            )
    if failures:
        raise ProcessClosureError(
            "process-spawn call entered the child source closure: "
            + ", ".join(failures)
        )
    serialized = "".join(entries).encode("utf-8")
    return {
        "schema": SCHEMA,
        "status": "pass",
        "process_model": "single-process-reviewed-source-closure-v1",
        "file_count": len(entries),
        "manifest_sha256": hashlib.sha256(serialized).hexdigest(),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ror-root", type=Path, required=True)
    parser.add_argument("--ogre-root", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        report = verify_process_closure(args.ror_root, args.ogre_root)
    except (OSError, ProcessClosureError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
