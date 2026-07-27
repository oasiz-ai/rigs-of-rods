#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed, deterministic audit for redistributable content provenance.

The auditor intentionally uses only the Python standard library.  Its JSON
report contains no timestamps or host paths, and diagnostics are sorted before
serialization so identical inputs produce byte-identical output.
"""

from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import stat
import sys
import unicodedata
from urllib.parse import urlsplit


MANIFEST_FORMAT = "ror-content-provenance-v1"
INVENTORY_FORMAT = "ror-distributable-inventory-v1"
REPORT_FORMAT = "ror-content-audit-result-v1"
SPDX_LIST_VERSION = "3.28.0"

HARD_MAX_INPUT_BYTES = 16 * 1024 * 1024
HARD_MAX_ENTRIES = 100_000
HARD_MAX_FILESYSTEM_FILES = 100_000
HARD_MAX_HASHED_FILE_BYTES = 16 * 1024 * 1024 * 1024
HARD_MAX_TOTAL_HASH_BYTES = 64 * 1024 * 1024 * 1024
HARD_MAX_RETAINED_DIAGNOSTICS = 4096
HARD_MAX_DIAGNOSTIC_DETAIL_BYTES = 4 * 1024 * 1024
HARD_MAX_DIAGNOSTIC_FIELD_BYTES = 4096

# Public defaults currently use the immutable safety ceilings.  Keeping the
# names separate makes it explicit that future defaults may become stricter,
# while no caller-provided value may ever make the hard boundary looser.
DEFAULT_MAX_INPUT_BYTES = HARD_MAX_INPUT_BYTES
DEFAULT_MAX_ENTRIES = HARD_MAX_ENTRIES
DEFAULT_MAX_FILESYSTEM_FILES = HARD_MAX_FILESYSTEM_FILES
DEFAULT_MAX_HASHED_FILE_BYTES = HARD_MAX_HASHED_FILE_BYTES
DEFAULT_MAX_TOTAL_HASH_BYTES = HARD_MAX_TOTAL_HASH_BYTES
MAX_PATH_BYTES = 1024
MAX_PATH_COMPONENT_BYTES = 255
MAX_TEXT_BYTES = 4096
MAX_OPTIONS_BYTES = 16 * 1024
MAX_FILE_BYTES = (1 << 63) - 1
READ_CHUNK_BYTES = 1024 * 1024

SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
CONTENT_REVISION_RE = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
VERSION_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/+@:-]{6,127}$")
DNS_LABEL_RE = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$")
SPDX_TOKEN_RE = re.compile(
    r"\s*(\(|\)|AND(?:\s|$)|OR(?:\s|$)|WITH(?:\s|$)|"
    r"[A-Za-z0-9][A-Za-z0-9.+-]*)"
)

# A conservative, frozen subset of non-deprecated SPDX 3.28 identifiers used
# for project, art, audio, font, and documentation assets.  Unknown identifiers
# fail closed until this audited set is deliberately extended.
SPDX_LICENSE_IDS = frozenset(
    {
        "0BSD",
        "Apache-1.1",
        "Apache-2.0",
        "Artistic-2.0",
        "BSL-1.0",
        "BSD-1-Clause",
        "BSD-2-Clause",
        "BSD-2-Clause-Patent",
        "BSD-3-Clause",
        "BSD-3-Clause-Clear",
        "BSD-4-Clause",
        "CC-BY-1.0",
        "CC-BY-2.0",
        "CC-BY-2.5",
        "CC-BY-3.0",
        "CC-BY-4.0",
        "CC-BY-ND-1.0",
        "CC-BY-ND-2.0",
        "CC-BY-ND-2.5",
        "CC-BY-ND-3.0",
        "CC-BY-ND-4.0",
        "CC-BY-NC-1.0",
        "CC-BY-NC-2.0",
        "CC-BY-NC-2.5",
        "CC-BY-NC-3.0",
        "CC-BY-NC-4.0",
        "CC-BY-NC-ND-1.0",
        "CC-BY-NC-ND-2.0",
        "CC-BY-NC-ND-2.5",
        "CC-BY-NC-ND-3.0",
        "CC-BY-NC-ND-4.0",
        "CC-BY-NC-SA-1.0",
        "CC-BY-NC-SA-2.0",
        "CC-BY-NC-SA-2.5",
        "CC-BY-NC-SA-3.0",
        "CC-BY-NC-SA-4.0",
        "CC-BY-SA-1.0",
        "CC-BY-SA-2.0",
        "CC-BY-SA-2.5",
        "CC-BY-SA-3.0",
        "CC-BY-SA-4.0",
        "CC-PDDC",
        "CC0-1.0",
        "CDDL-1.0",
        "CDDL-1.1",
        "EPL-1.0",
        "EPL-2.0",
        "EUPL-1.1",
        "EUPL-1.2",
        "GFDL-1.1-only",
        "GFDL-1.1-or-later",
        "GFDL-1.2-only",
        "GFDL-1.2-or-later",
        "GFDL-1.3-only",
        "GFDL-1.3-or-later",
        "GPL-1.0-only",
        "GPL-1.0-or-later",
        "GPL-2.0-only",
        "GPL-2.0-or-later",
        "GPL-3.0-only",
        "GPL-3.0-or-later",
        "ISC",
        "LGPL-2.0-only",
        "LGPL-2.0-or-later",
        "LGPL-2.1-only",
        "LGPL-2.1-or-later",
        "LGPL-3.0-only",
        "LGPL-3.0-or-later",
        "MIT",
        "MIT-0",
        "MPL-1.1",
        "MPL-2.0",
        "MS-PL",
        "MS-RL",
        "OFL-1.0",
        "OFL-1.0-no-RFN",
        "OFL-1.0-RFN",
        "OFL-1.1",
        "OFL-1.1-no-RFN",
        "OFL-1.1-RFN",
        "OpenSSL",
        "Python-2.0",
        "Unlicense",
        "UPL-1.0",
        "W3C",
        "WTFPL",
        "X11",
        "Zlib",
    }
)

SPDX_EXCEPTION_IDS = frozenset(
    {
        "Autoconf-exception-2.0",
        "Autoconf-exception-3.0",
        "Bison-exception-2.2",
        "Classpath-exception-2.0",
        "FLTK-exception",
        "Font-exception-2.0",
        "GCC-exception-2.0",
        "GCC-exception-3.1",
        "LLVM-exception",
        "OpenJDK-assembly-exception-1.0",
        "Qt-GPL-exception-1.0",
        "Qt-LGPL-exception-1.1",
        "WxWindows-exception-3.1",
    }
)

ASSET_CLASSIFICATIONS = frozenset(
    {
        "project-authored",
        "third-party",
        "generated",
        "import-archive",
        "derived-cache",
    }
)
SOURCE_KINDS = frozenset({"repository", "url", "generator"})
ARCHIVE_SUFFIXES = (
    ".7z",
    ".rar",
    ".skinzip",
    ".tar",
    ".tar.bz2",
    ".tar.gz",
    ".tar.xz",
    ".tar.zst",
    ".tbz",
    ".tbz2",
    ".tgz",
    ".txz",
    ".zip",
)
CACHE_SUFFIXES = (".cache", ".importcache", ".tmp")
CACHE_COMPONENTS = frozenset(
    {".cache", "cache", "caches", "derived-cache", "import-cache"}
)
WINDOWS_RESERVED_NAMES = frozenset(
    {
        "aux",
        "clock$",
        "con",
        "conin$",
        "conout$",
        "nul",
        "prn",
        *(f"com{number}" for number in range(1, 10)),
        *(f"lpt{number}" for number in range(1, 10)),
        "com¹",
        "com²",
        "com³",
        "lpt¹",
        "lpt²",
        "lpt³",
    }
)
WINDOWS_FORBIDDEN_PATH_CHARACTERS = frozenset('<>:"\\|?*')


class InputFailure(Exception):
    """A stable, expected input-load failure."""

    def __init__(self, code: str, label: str, detail: str) -> None:
        super().__init__(detail)
        self.code = code
        self.label = label
        self.detail = detail


class DuplicateJsonKey(ValueError):
    pass


class InvalidJsonConstant(ValueError):
    pass


class Diagnostics:
    def __init__(self) -> None:
        self.items: list[dict[str, str]] = []
        self.total_count = 0
        self.detail_bytes = 0
        self.overflowed = False

    def add(
        self,
        code: str,
        *,
        pointer: str = "",
        path: str = "",
        message: str,
    ) -> None:
        self.total_count += 1
        item = {
            "code": _bounded_diagnostic_text(code),
            "message": _bounded_diagnostic_text(message),
            "path": _bounded_diagnostic_text(path),
            "pointer": _bounded_diagnostic_text(pointer),
            "severity": "error",
        }
        item_bytes = _diagnostic_detail_bytes(item)
        if (
            len(self.items) >= HARD_MAX_RETAINED_DIAGNOSTICS - 1
            or self.detail_bytes + item_bytes
            > HARD_MAX_DIAGNOSTIC_DETAIL_BYTES
            - _diagnostic_detail_bytes(_diagnostic_limit_marker())
        ):
            self.overflowed = True
            return
        self.items.append(item)
        self.detail_bytes += item_bytes

    def merge(self, other: "Diagnostics") -> None:
        for item in other.items:
            self.add(
                item["code"],
                message=item["message"],
                path=item["path"],
                pointer=item["pointer"],
            )
        omitted = other.total_count - len(other.items)
        if omitted > 0:
            self.total_count += omitted
            self.overflowed = True

    def sorted(self) -> list[dict[str, str]]:
        retained = list(self.items)
        if self.overflowed:
            retained.append(_diagnostic_limit_marker())
        return sorted(
            retained,
            key=lambda item: (
                item["code"],
                item["path"],
                item["pointer"],
                item["message"],
            ),
        )


def tightened_limit(value: object, hard_maximum: int) -> int:
    """Return a positive caller limit clamped to an immutable hard maximum.

    Zero is the fail-closed sentinel for invalid direct-API values.  The CLI
    rejects non-positive values during argument parsing.
    """

    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        return 0
    return min(value, hard_maximum)


def _bounded_diagnostic_text(value: object) -> str:
    text = value if isinstance(value, str) else str(value)
    encoded = text.encode("utf-8", errors="backslashreplace")
    if len(encoded) <= HARD_MAX_DIAGNOSTIC_FIELD_BYTES:
        return encoded.decode("utf-8")
    suffix = b"...[truncated]"
    prefix = encoded[: HARD_MAX_DIAGNOSTIC_FIELD_BYTES - len(suffix)]
    return prefix.decode("utf-8", errors="ignore") + suffix.decode("ascii")


def _diagnostic_detail_bytes(item: dict[str, str]) -> int:
    return sum(len(value.encode("utf-8")) for value in item.values())


def _diagnostic_limit_marker() -> dict[str, str]:
    return {
        "code": "DIAGNOSTIC_LIMIT_EXCEEDED",
        "message": (
            "additional diagnostics were omitted at the immutable output limit"
        ),
        "path": "",
        "pointer": "",
        "severity": "error",
    }


def _reject_constant(value: str) -> None:
    raise InvalidJsonConstant(value)


def _object_without_duplicates(
    pairs: list[tuple[str, object]],
) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateJsonKey(key)
        result[key] = value
    return result


def _same_file_identity(first: os.stat_result, second: os.stat_result) -> bool:
    try:
        return os.path.samestat(first, second)
    except (AttributeError, OSError, ValueError):
        return (
            getattr(first, "st_dev", None),
            getattr(first, "st_ino", None),
        ) == (
            getattr(second, "st_dev", None),
            getattr(second, "st_ino", None),
        )


def _stat_stability_key(info: os.stat_result) -> tuple[int, ...]:
    return (
        stat.S_IFMT(info.st_mode),
        info.st_size,
        getattr(info, "st_mtime_ns", int(info.st_mtime * 1_000_000_000)),
        getattr(info, "st_ctime_ns", int(info.st_ctime * 1_000_000_000)),
    )


def _same_directory_identity(
    first: os.stat_result,
    second: os.stat_result,
) -> bool:
    """Compare directory objects without treating mutable metadata as identity.

    Windows can refresh a directory's reported size and timestamps after child
    handles close even when the directory object and its entries are unchanged.
    Entry-set stability is checked by the mandatory second scan below; object
    replacement remains fail-closed through ``samestat`` and reparse checks.
    """

    return (
        stat.S_ISDIR(first.st_mode)
        and stat.S_ISDIR(second.st_mode)
        and not _is_symlink_or_reparse_point(first)
        and not _is_symlink_or_reparse_point(second)
        and _same_file_identity(first, second)
    )


def _is_symlink_or_reparse_point(info: os.stat_result) -> bool:
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    file_attributes = getattr(info, "st_file_attributes", 0) or 0
    return stat.S_ISLNK(info.st_mode) or bool(
        reparse_attribute and file_attributes & reparse_attribute
    )


def _valid_uri_host(
    hostname: str,
    authority: str,
    port: int | None,
) -> bool:
    if authority.startswith("["):
        closing_bracket = authority.find("]")
        raw_hostname = authority[1:closing_bracket]
    elif port is not None:
        raw_hostname = authority.rsplit(":", 1)[0]
    else:
        raw_hostname = authority
    if (
        not hostname
        or raw_hostname != raw_hostname.lower()
        or hostname.endswith(".")
        or "%" in hostname
    ):
        return False
    try:
        ipaddress.ip_address(hostname)
        return True
    except ValueError:
        pass
    if all(character in "0123456789." for character in hostname):
        return False
    try:
        ascii_hostname = hostname.encode("idna").decode("ascii")
    except (UnicodeError, UnicodeEncodeError):
        return False
    if len(ascii_hostname) > 253:
        return False
    labels = ascii_hostname.split(".")
    return bool(labels) and all(
        DNS_LABEL_RE.fullmatch(label) is not None for label in labels
    )


def read_json(path: Path, *, label: str, max_bytes: int) -> object:
    max_bytes = tightened_limit(max_bytes, HARD_MAX_INPUT_BYTES)
    if max_bytes == 0:
        raise InputFailure(
            "LIMIT_INVALID",
            label,
            "input byte limit must be a positive integer",
        )
    try:
        stream = path.open("rb")
    except OSError as error:
        raise InputFailure(
            "INPUT_UNREADABLE", label, type(error).__name__
        ) from error
    try:
        with stream:
            try:
                initial_info = os.fstat(stream.fileno())
            except OSError as error:
                raise InputFailure(
                    "INPUT_UNREADABLE", label, type(error).__name__
                ) from error
            if not stat.S_ISREG(initial_info.st_mode):
                raise InputFailure(
                    "INPUT_NOT_REGULAR", label, "not a regular file"
                )
            if initial_info.st_size > max_bytes:
                raise InputFailure(
                    "INPUT_TOO_LARGE",
                    label,
                    f"input exceeds {max_bytes} bytes",
                )

            chunks: list[bytes] = []
            total = 0
            try:
                while total <= max_bytes:
                    request_bytes = min(
                        READ_CHUNK_BYTES, max_bytes + 1 - total
                    )
                    chunk = stream.read(request_bytes)
                    if not chunk:
                        break
                    chunks.append(chunk)
                    total += len(chunk)
            except OSError as error:
                raise InputFailure(
                    "INPUT_UNREADABLE", label, type(error).__name__
                ) from error
            except MemoryError as error:
                raise InputFailure(
                    "INPUT_TOO_LARGE",
                    label,
                    f"input exceeds available memory within {max_bytes} bytes",
                ) from error
            if total > max_bytes:
                raise InputFailure(
                    "INPUT_TOO_LARGE",
                    label,
                    f"input exceeds {max_bytes} bytes",
                )
            try:
                final_info = os.fstat(stream.fileno())
                path_info = path.stat()
            except OSError as error:
                raise InputFailure(
                    "INPUT_CHANGED", label, type(error).__name__
                ) from error
            if (
                not _same_file_identity(initial_info, final_info)
                or not _same_file_identity(final_info, path_info)
                or _stat_stability_key(initial_info)
                != _stat_stability_key(final_info)
            ):
                raise InputFailure(
                    "INPUT_CHANGED",
                    label,
                    "input changed while it was being read",
                )
            try:
                payload = b"".join(chunks)
            except MemoryError as error:
                raise InputFailure(
                    "INPUT_TOO_LARGE",
                    label,
                    f"input exceeds available memory within {max_bytes} bytes",
                ) from error
    except InputFailure:
        raise
    except OSError as error:
        raise InputFailure(
            "INPUT_UNREADABLE", label, type(error).__name__
        ) from error
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise InputFailure(
            "INPUT_NOT_UTF8", label, "input is not valid UTF-8"
        ) from error
    except MemoryError as error:
        raise InputFailure(
            "INPUT_TOO_LARGE",
            label,
            f"input exceeds available memory within {max_bytes} bytes",
        ) from error
    try:
        return json.loads(
            text,
            object_pairs_hook=_object_without_duplicates,
            parse_constant=_reject_constant,
        )
    except DuplicateJsonKey as error:
        raise InputFailure(
            "JSON_DUPLICATE_KEY", label, f"duplicate key: {error}"
        ) from error
    except json.JSONDecodeError as error:
        raise InputFailure(
            "JSON_INVALID", label, "invalid JSON syntax"
        ) from error
    except (InvalidJsonConstant, RecursionError, ValueError) as error:
        raise InputFailure(
            "JSON_INVALID", label, type(error).__name__
        ) from error
    except MemoryError as error:
        raise InputFailure(
            "INPUT_TOO_LARGE",
            label,
            f"input exceeds available memory within {max_bytes} bytes",
        ) from error


def canonical_record_key(value: object) -> str:
    try:
        return json.dumps(
            value,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
    except (TypeError, ValueError, RecursionError):
        return f"<{type(value).__name__}>"


def check_exact_keys(
    value: dict[str, object],
    *,
    required: set[str],
    optional: set[str],
    pointer: str,
    diagnostics: Diagnostics,
) -> None:
    for missing in sorted(required - set(value)):
        diagnostics.add(
            "FIELD_MISSING",
            pointer=f"{pointer}/{missing}",
            message="required field is missing",
        )
    for unknown in sorted(set(value) - required - optional):
        diagnostics.add(
            "FIELD_UNKNOWN",
            pointer=f"{pointer}/{unknown}",
            message="field is not part of this format version",
        )


def checked_text(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
    allow_empty: bool = False,
    max_bytes: int = MAX_TEXT_BYTES,
) -> str | None:
    if not isinstance(value, str):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=pointer,
            message="field must be a string",
        )
        return None
    if value != value.strip():
        diagnostics.add(
            "TEXT_NOT_CANONICAL",
            pointer=pointer,
            message="string must not have leading or trailing whitespace",
        )
        return None
    if not allow_empty and not value:
        diagnostics.add(
            "TEXT_EMPTY",
            pointer=pointer,
            message="string must not be empty",
        )
        return None
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError:
        diagnostics.add(
            "TEXT_INVALID_UNICODE",
            pointer=pointer,
            message="string contains an unpaired Unicode surrogate",
        )
        return None
    if len(encoded) > max_bytes:
        diagnostics.add(
            "TEXT_TOO_LONG",
            pointer=pointer,
            message=f"string exceeds {max_bytes} UTF-8 bytes",
        )
        return None
    if any(
        ord(character) < 0x20
        or ord(character) == 0x7F
        or unicodedata.category(character) == "Cc"
        for character in value
    ):
        diagnostics.add(
            "TEXT_CONTROL_CHARACTER",
            pointer=pointer,
            message="string contains a control character",
        )
        return None
    return value


def checked_sha256(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> str | None:
    text = checked_text(
        value,
        pointer=pointer,
        diagnostics=diagnostics,
        max_bytes=64,
    )
    if text is not None and SHA256_RE.fullmatch(text) is None:
        diagnostics.add(
            "SHA256_INVALID",
            pointer=pointer,
            message="SHA-256 must be 64 lowercase hexadecimal characters",
        )
        return None
    return text


def path_problem(value: str) -> str | None:
    if not value:
        return "path is empty"
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError:
        return "path contains an unpaired Unicode surrogate"
    if len(encoded) > MAX_PATH_BYTES:
        return f"path exceeds {MAX_PATH_BYTES} UTF-8 bytes"
    if unicodedata.normalize("NFC", value) != value:
        return "path is not Unicode NFC"
    if value.startswith("/") or value.startswith("\\"):
        return "path is absolute"
    forbidden = sorted(
        {
            character
            for character in value
            if character in WINDOWS_FORBIDDEN_PATH_CHARACTERS
        }
    )
    if forbidden:
        return "path contains a Windows-forbidden character"
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in value):
        return "path contains a control character"
    components = value.split("/")
    if any(component in {"", ".", ".."} for component in components):
        return "path contains an empty, current, or parent component"
    for component in components:
        if component != component.strip():
            return "path component has leading or trailing whitespace"
        if component.endswith("."):
            return "path component has a Windows-unsafe trailing dot"
        if len(component.encode("utf-8")) > MAX_PATH_COMPONENT_BYTES:
            return (
                f"path component exceeds {MAX_PATH_COMPONENT_BYTES} UTF-8 bytes"
            )
        base_name = component.split(".", 1)[0].casefold()
        if base_name in WINDOWS_RESERVED_NAMES:
            return "path contains a reserved Windows device name"
    return None


def checked_path(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> str | None:
    if not isinstance(value, str):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=pointer,
            message="path must be a string",
        )
        return None
    problem = path_problem(value)
    if problem is not None:
        diagnostics.add(
            "PATH_UNSAFE",
            pointer=pointer,
            path=value[:MAX_PATH_BYTES],
            message=problem,
        )
        return None
    return value


def checked_https_uri(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> str | None:
    text = checked_text(
        value,
        pointer=pointer,
        diagnostics=diagnostics,
    )
    if text is None:
        return None
    try:
        parsed = urlsplit(text)
        if parsed.port is not None and not (1 <= parsed.port <= 65535):
            parsed = None
    except (UnicodeError, ValueError):
        parsed = None
    if (
        parsed is None
        or not text.startswith("https://")
        or parsed.scheme != "https"
        or not text.isascii()
        or not parsed.hostname
        or any(character.isspace() for character in text)
        or "\\" in text
        or re.search(r"%(?![0-9A-Fa-f]{2})", text) is not None
        or parsed.username is not None
        or parsed.password is not None
        or "#" in text
        or parsed.netloc.endswith(":")
        or not _valid_uri_host(parsed.hostname, parsed.netloc, parsed.port)
    ):
        diagnostics.add(
            "SOURCE_URI_INVALID",
            pointer=pointer,
            message=(
                "URI must be absolute HTTPS without credentials or fragment"
            ),
        )
        return None
    return text


class SpdxParser:
    def __init__(self, expression: str) -> None:
        self.tokens: list[str] = []
        offset = 0
        while offset < len(expression):
            match = SPDX_TOKEN_RE.match(expression, offset)
            if match is None:
                raise ValueError("invalid token")
            token = match.group(1).strip()
            self.tokens.append(token)
            offset = match.end()
        self.position = 0

    def current(self) -> str | None:
        if self.position >= len(self.tokens):
            return None
        return self.tokens[self.position]

    def consume(self, token: str | None = None) -> str:
        current = self.current()
        if current is None or (token is not None and current != token):
            raise ValueError("unexpected token")
        self.position += 1
        return current

    def parse(self) -> None:
        self.parse_or()
        if self.current() is not None:
            raise ValueError("trailing token")

    def parse_or(self) -> None:
        self.parse_and()
        while self.current() == "OR":
            self.consume("OR")
            self.parse_and()

    def parse_and(self) -> None:
        self.parse_with()
        while self.current() == "AND":
            self.consume("AND")
            self.parse_with()

    def parse_with(self) -> None:
        is_identifier = self.parse_primary()
        if self.current() == "WITH":
            if not is_identifier:
                raise ValueError("WITH must follow a license identifier")
            self.consume("WITH")
            exception = self.consume()
            if exception not in SPDX_EXCEPTION_IDS:
                raise ValueError("unknown SPDX exception")

    def parse_primary(self) -> bool:
        current = self.current()
        if current == "(":
            self.consume("(")
            self.parse_or()
            self.consume(")")
            return False
        if current is None or current in {")", "AND", "OR", "WITH"}:
            raise ValueError("license identifier expected")
        identifier = self.consume()
        if identifier not in SPDX_LICENSE_IDS:
            raise ValueError("unknown SPDX license identifier")
        return True


def checked_spdx(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> str | None:
    expression = checked_text(
        value,
        pointer=pointer,
        diagnostics=diagnostics,
        max_bytes=512,
    )
    if expression is None:
        return None
    try:
        SpdxParser(expression).parse()
    except ValueError:
        diagnostics.add(
            "SPDX_EXPRESSION_INVALID",
            pointer=pointer,
            message=(
                f"expression is not in the audited SPDX "
                f"{SPDX_LIST_VERSION} identifier subset"
            ),
        )
        return None
    return expression


def validate_source(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> str | None:
    if not isinstance(value, dict):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=pointer,
            message="source must be an object",
        )
        return None
    check_exact_keys(
        value,
        required={"kind", "uri"},
        optional={"revision", "sha256"},
        pointer=pointer,
        diagnostics=diagnostics,
    )
    kind = checked_text(
        value.get("kind"),
        pointer=f"{pointer}/kind",
        diagnostics=diagnostics,
    )
    if kind is not None and kind not in SOURCE_KINDS:
        diagnostics.add(
            "SOURCE_KIND_INVALID",
            pointer=f"{pointer}/kind",
            message="source kind is not recognized",
        )
    checked_https_uri(
        value.get("uri"),
        pointer=f"{pointer}/uri",
        diagnostics=diagnostics,
    )
    revision = value.get("revision")
    checksum = value.get("sha256")
    revision_is_pin = False
    checksum_is_pin = False
    if revision is not None:
        revision_text = checked_text(
            revision,
            pointer=f"{pointer}/revision",
            diagnostics=diagnostics,
            max_bytes=128,
        )
        if revision_text is not None:
            revision_is_pin = (
                CONTENT_REVISION_RE.fullmatch(revision_text) is not None
            )
            if not revision_is_pin:
                diagnostics.add(
                    "SOURCE_REVISION_INVALID",
                    pointer=f"{pointer}/revision",
                    message=(
                        "revision must be a 40- or 64-character lowercase "
                        "content-addressed identifier"
                    ),
                )
    if checksum is not None:
        checksum_is_pin = (
            checked_sha256(
                checksum,
                pointer=f"{pointer}/sha256",
                diagnostics=diagnostics,
            )
            is not None
        )
    if not revision_is_pin and not checksum_is_pin:
        diagnostics.add(
            "SOURCE_NOT_PINNED",
            pointer=pointer,
            message=(
                "source requires a valid content-addressed revision or SHA-256"
            ),
        )
    return kind


def validate_editable_source(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> tuple[str | None, str | None]:
    if not isinstance(value, dict):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=pointer,
            message="editable_source must be an object",
        )
        return None, None
    check_exact_keys(
        value,
        required={"path", "sha256"},
        optional=set(),
        pointer=pointer,
        diagnostics=diagnostics,
    )
    source_path = checked_path(
        value.get("path"),
        pointer=f"{pointer}/path",
        diagnostics=diagnostics,
    )
    source_hash = checked_sha256(
        value.get("sha256"),
        pointer=f"{pointer}/sha256",
        diagnostics=diagnostics,
    )
    return source_path, source_hash


def validate_redistribution(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> None:
    if not isinstance(value, dict):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=pointer,
            message="redistribution must be an object",
        )
        return
    check_exact_keys(
        value,
        required={"allowed", "evidence"},
        optional=set(),
        pointer=pointer,
        diagnostics=diagnostics,
    )
    allowed = value.get("allowed")
    if not isinstance(allowed, bool):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=f"{pointer}/allowed",
            message="allowed must be a boolean",
        )
    elif not allowed:
        diagnostics.add(
            "REDISTRIBUTION_NOT_ALLOWED",
            pointer=f"{pointer}/allowed",
            message="non-redistributable asset is present in inventory",
        )
    checked_https_uri(
        value.get("evidence"),
        pointer=f"{pointer}/evidence",
        diagnostics=diagnostics,
    )


def validate_import_metadata(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> str | None:
    if not isinstance(value, dict):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=pointer,
            message="import metadata must be an object",
        )
        return None
    check_exact_keys(
        value,
        required={
            "archive_sha256",
            "detected_version",
            "importer_schema",
            "conversion_options",
        },
        optional=set(),
        pointer=pointer,
        diagnostics=diagnostics,
    )
    archive_hash = checked_sha256(
        value.get("archive_sha256"),
        pointer=f"{pointer}/archive_sha256",
        diagnostics=diagnostics,
    )
    checked_text(
        value.get("detected_version"),
        pointer=f"{pointer}/detected_version",
        diagnostics=diagnostics,
        max_bytes=128,
    )
    schema = checked_text(
        value.get("importer_schema"),
        pointer=f"{pointer}/importer_schema",
        diagnostics=diagnostics,
        max_bytes=128,
    )
    if schema is not None and VERSION_RE.fullmatch(schema) is None:
        diagnostics.add(
            "IMPORTER_SCHEMA_INVALID",
            pointer=f"{pointer}/importer_schema",
            message="importer schema must be a pinned version identifier",
        )
    options = value.get("conversion_options")
    if not isinstance(options, dict):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=f"{pointer}/conversion_options",
            message="conversion_options must be an object",
        )
    else:
        try:
            encoded = json.dumps(
                options,
                ensure_ascii=True,
                sort_keys=True,
                separators=(",", ":"),
                allow_nan=False,
            ).encode("utf-8")
        except (TypeError, ValueError, RecursionError):
            diagnostics.add(
                "CONVERSION_OPTIONS_INVALID",
                pointer=f"{pointer}/conversion_options",
                message="conversion options are not canonical JSON data",
            )
        else:
            if len(encoded) > MAX_OPTIONS_BYTES:
                diagnostics.add(
                    "CONVERSION_OPTIONS_TOO_LARGE",
                    pointer=f"{pointer}/conversion_options",
                    message=(
                        f"conversion options exceed {MAX_OPTIONS_BYTES} bytes"
                    ),
                )
    return archive_hash


def suspicious_import_kind(path: str) -> str | None:
    lowered = path.casefold()
    if lowered.endswith(ARCHIVE_SUFFIXES):
        return "archive"
    components = lowered.split("/")
    if (
        any(component in CACHE_COMPONENTS for component in components)
        or lowered.endswith(CACHE_SUFFIXES)
    ):
        return "cache"
    return None


def validate_asset(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> dict[str, object] | None:
    if not isinstance(value, dict):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=pointer,
            message="asset entry must be an object",
        )
        return None
    check_exact_keys(
        value,
        required={
            "path",
            "sha256",
            "author",
            "license",
            "modified",
            "classification",
            "source",
            "editable_source",
            "redistribution",
        },
        optional={"import"},
        pointer=pointer,
        diagnostics=diagnostics,
    )
    asset_path = checked_path(
        value.get("path"),
        pointer=f"{pointer}/path",
        diagnostics=diagnostics,
    )
    asset_hash = checked_sha256(
        value.get("sha256"),
        pointer=f"{pointer}/sha256",
        diagnostics=diagnostics,
    )
    checked_text(
        value.get("author"),
        pointer=f"{pointer}/author",
        diagnostics=diagnostics,
    )
    checked_spdx(
        value.get("license"),
        pointer=f"{pointer}/license",
        diagnostics=diagnostics,
    )
    if not isinstance(value.get("modified"), bool):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=f"{pointer}/modified",
            message="modified must be a boolean",
        )
    classification = checked_text(
        value.get("classification"),
        pointer=f"{pointer}/classification",
        diagnostics=diagnostics,
    )
    if (
        classification is not None
        and classification not in ASSET_CLASSIFICATIONS
    ):
        diagnostics.add(
            "CLASSIFICATION_INVALID",
            pointer=f"{pointer}/classification",
            message="asset classification is not recognized",
        )
    if asset_path is not None:
        suspicious_kind = suspicious_import_kind(asset_path)
        required_classification = {
            "archive": "import-archive",
            "cache": "derived-cache",
        }.get(suspicious_kind)
        if (
            required_classification is not None
            and classification != required_classification
        ):
            diagnostics.add(
                "IMPORT_CLASSIFICATION_REQUIRED",
                pointer=f"{pointer}/classification",
                path=asset_path,
                message=(
                    f"{suspicious_kind}-looking paths require the "
                    f"{required_classification} classification"
                ),
            )
    source_kind = validate_source(
        value.get("source"),
        pointer=f"{pointer}/source",
        diagnostics=diagnostics,
    )
    if (
        classification == "generated"
        and source_kind is not None
        and source_kind != "generator"
    ):
        diagnostics.add(
            "GENERATED_SOURCE_KIND_INVALID",
            pointer=f"{pointer}/source/kind",
            path=asset_path or "",
            message="generated assets must identify their generator source",
        )
    if classification == "generated" and value.get("modified") is not True:
        diagnostics.add(
            "GENERATED_MODIFICATION_INVALID",
            pointer=f"{pointer}/modified",
            path=asset_path or "",
            message="generated assets must be marked modified",
        )
    editable_path, editable_hash = validate_editable_source(
        value.get("editable_source"),
        pointer=f"{pointer}/editable_source",
        diagnostics=diagnostics,
    )
    validate_redistribution(
        value.get("redistribution"),
        pointer=f"{pointer}/redistribution",
        diagnostics=diagnostics,
    )
    import_metadata = value.get("import")
    if classification in {"import-archive", "derived-cache"}:
        if import_metadata is None:
            diagnostics.add(
                "IMPORT_METADATA_MISSING",
                pointer=f"{pointer}/import",
                path=asset_path or "",
                message=(
                    "import archives and derived caches require import metadata"
                ),
            )
        else:
            archive_hash = validate_import_metadata(
                import_metadata,
                pointer=f"{pointer}/import",
                diagnostics=diagnostics,
            )
            if (
                classification == "import-archive"
                and asset_hash is not None
                and archive_hash is not None
                and asset_hash != archive_hash
            ):
                diagnostics.add(
                    "IMPORT_ARCHIVE_CHECKSUM_MISMATCH",
                    pointer=f"{pointer}/import/archive_sha256",
                    path=asset_path or "",
                    message=(
                        "import archive metadata must identify the shipped "
                        "archive bytes"
                    ),
                )
    elif import_metadata is not None:
        diagnostics.add(
            "IMPORT_METADATA_UNEXPECTED",
            pointer=f"{pointer}/import",
            path=asset_path or "",
            message="import metadata requires an import/cache classification",
        )
    return {
        "path": asset_path,
        "sha256": asset_hash,
        "classification": classification,
        "editable_path": editable_path,
        "editable_sha256": editable_hash,
    }


def validate_inventory_file(
    value: object,
    *,
    pointer: str,
    diagnostics: Diagnostics,
) -> dict[str, object] | None:
    if not isinstance(value, dict):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=pointer,
            message="inventory entry must be an object",
        )
        return None
    check_exact_keys(
        value,
        required={"path", "sha256", "size", "type"},
        optional=set(),
        pointer=pointer,
        diagnostics=diagnostics,
    )
    file_path = checked_path(
        value.get("path"),
        pointer=f"{pointer}/path",
        diagnostics=diagnostics,
    )
    file_hash = checked_sha256(
        value.get("sha256"),
        pointer=f"{pointer}/sha256",
        diagnostics=diagnostics,
    )
    size = value.get("size")
    if (
        isinstance(size, bool)
        or not isinstance(size, int)
        or size < 0
        or size > MAX_FILE_BYTES
    ):
        diagnostics.add(
            "FILE_SIZE_INVALID",
            pointer=f"{pointer}/size",
            path=file_path or "",
            message="size must be an integer from 0 through 2^63-1",
        )
        size = None
    entry_type = value.get("type")
    if entry_type != "file":
        diagnostics.add(
            "INVENTORY_TYPE_INVALID",
            pointer=f"{pointer}/type",
            path=file_path or "",
            message="distributable inventory may contain regular files only",
        )
    return {"path": file_path, "sha256": file_hash, "size": size}


def validate_root(
    value: object,
    *,
    expected_format: str,
    entries_key: str,
    label: str,
    max_entries: int,
    diagnostics: Diagnostics,
) -> list[object]:
    max_entries = tightened_limit(max_entries, HARD_MAX_ENTRIES)
    if not isinstance(value, dict):
        diagnostics.add(
            "ROOT_TYPE",
            pointer=f"/{label}",
            message="document root must be an object",
        )
        return []
    check_exact_keys(
        value,
        required={"format", entries_key},
        optional={"spdx_list_version"} if label == "manifest" else set(),
        pointer=f"/{label}",
        diagnostics=diagnostics,
    )
    if value.get("format") != expected_format:
        diagnostics.add(
            "FORMAT_UNSUPPORTED",
            pointer=f"/{label}/format",
            message=f"expected {expected_format}",
        )
    if label == "manifest" and value.get("spdx_list_version") != SPDX_LIST_VERSION:
        diagnostics.add(
            "SPDX_LIST_VERSION_UNSUPPORTED",
            pointer=f"/{label}/spdx_list_version",
            message=f"expected SPDX list version {SPDX_LIST_VERSION}",
        )
    entries = value.get(entries_key)
    if not isinstance(entries, list):
        diagnostics.add(
            "FIELD_TYPE",
            pointer=f"/{label}/{entries_key}",
            message=f"{entries_key} must be an array",
        )
        return []
    if len(entries) > max_entries:
        diagnostics.add(
            "ENTRY_LIMIT_EXCEEDED",
            pointer=f"/{label}/{entries_key}",
            message=f"entry count exceeds {max_entries}",
        )
        return []
    return sorted(entries, key=canonical_record_key)


def detect_duplicate_paths(
    records: list[dict[str, object]],
    *,
    label: str,
    diagnostics: Diagnostics,
) -> dict[str, dict[str, object]]:
    by_path: dict[str, dict[str, object]] = {}
    casefolded: dict[str, str] = {}
    for record in records:
        path = record.get("path")
        if not isinstance(path, str):
            continue
        if path in by_path:
            diagnostics.add(
                "PATH_DUPLICATE",
                pointer=f"/{label}",
                path=path,
                message=f"path appears more than once in {label}",
            )
            continue
        folded = path.casefold()
        collision = casefolded.get(folded)
        if collision is not None and collision != path:
            diagnostics.add(
                "PATH_CASE_COLLISION",
                pointer=f"/{label}",
                path=path,
                message=f"path collides case-insensitively with {collision}",
            )
        else:
            casefolded[folded] = path
        by_path[path] = record
    return by_path


def _supports_anchored_no_follow_open() -> bool:
    return (
        hasattr(os, "O_DIRECTORY")
        and hasattr(os, "O_NOFOLLOW")
        and os.open in getattr(os, "supports_dir_fd", set())
    )


def _path_chain_snapshot(
    root: Path,
    relative_path: str,
    *,
    diagnostics: Diagnostics,
    code_prefix: str,
) -> list[tuple[Path, os.stat_result]] | None:
    components = relative_path.split("/")
    paths = [root]
    current = root
    for component in components:
        current = current / component
        paths.append(current)

    snapshot: list[tuple[Path, os.stat_result]] = []
    for index, current_path in enumerate(paths):
        try:
            info = current_path.lstat()
        except OSError as error:
            diagnostics.add(
                f"{code_prefix}_UNREADABLE",
                path=relative_path,
                message=type(error).__name__,
            )
            return None
        if _is_symlink_or_reparse_point(info):
            diagnostics.add(
                f"{code_prefix}_SYMLINK",
                path=relative_path,
                message="symlinks and filesystem reparse points are not permitted",
            )
            return None
        is_final = index == len(paths) - 1
        if is_final:
            if not stat.S_ISREG(info.st_mode):
                diagnostics.add(
                    f"{code_prefix}_NOT_REGULAR",
                    path=relative_path,
                    message="path is not a regular file",
                )
                return None
        elif not stat.S_ISDIR(info.st_mode):
            diagnostics.add(
                f"{code_prefix}_NOT_REGULAR",
                path=relative_path,
                message="path traverses a non-directory component",
            )
            return None
        snapshot.append((current_path, info))
    return snapshot


def _snapshot_is_stable(
    snapshot: list[tuple[Path, os.stat_result]],
    *,
    relative_path: str,
    diagnostics: Diagnostics,
    code_prefix: str,
) -> bool:
    for current_path, expected in snapshot:
        try:
            actual = current_path.lstat()
        except OSError as error:
            diagnostics.add(
                f"{code_prefix}_CHANGED",
                path=relative_path,
                message=f"path changed during hashing: {type(error).__name__}",
            )
            return False
        if _is_symlink_or_reparse_point(actual):
            diagnostics.add(
                f"{code_prefix}_SYMLINK",
                path=relative_path,
                message="path became a symlink or filesystem reparse point",
            )
            return False
        if (
            not _same_file_identity(expected, actual)
            or _stat_stability_key(expected) != _stat_stability_key(actual)
        ):
            diagnostics.add(
                f"{code_prefix}_CHANGED",
                path=relative_path,
                message="path identity or metadata changed during hashing",
            )
            return False
    return True


def _hash_open_descriptor(
    descriptor: int,
    initial_info: os.stat_result,
    *,
    relative_path: str,
    diagnostics: Diagnostics,
    code_prefix: str,
    max_file_bytes: int,
    remaining_hash_bytes: list[int],
) -> tuple[int, str, os.stat_result] | None:
    if not stat.S_ISREG(initial_info.st_mode):
        diagnostics.add(
            f"{code_prefix}_NOT_REGULAR",
            path=relative_path,
            message="opened path is not a regular file",
        )
        return None
    if initial_info.st_size > max_file_bytes:
        diagnostics.add(
            f"{code_prefix}_TOO_LARGE",
            path=relative_path,
            message=f"file exceeds the {max_file_bytes}-byte hashing limit",
        )
        return None
    if initial_info.st_size > remaining_hash_bytes[0]:
        diagnostics.add(
            "HASH_BUDGET_EXCEEDED",
            path=relative_path,
            message="total hashing byte budget is exhausted",
        )
        return None

    digest = hashlib.sha256()
    size = 0
    starting_budget = remaining_hash_bytes[0]
    byte_limit = min(max_file_bytes, starting_budget)
    try:
        while size <= byte_limit:
            request_bytes = min(READ_CHUNK_BYTES, byte_limit + 1 - size)
            chunk = os.read(descriptor, request_bytes)
            if not chunk:
                break
            size += len(chunk)
            if size > max_file_bytes:
                diagnostics.add(
                    f"{code_prefix}_TOO_LARGE",
                    path=relative_path,
                    message=(
                        f"file exceeds the {max_file_bytes}-byte hashing limit"
                    ),
                )
                return None
            if size > starting_budget:
                diagnostics.add(
                    "HASH_BUDGET_EXCEEDED",
                    path=relative_path,
                    message="total hashing byte budget is exhausted",
                )
                return None
            digest.update(chunk)
        final_info = os.fstat(descriptor)
    except OSError as error:
        diagnostics.add(
            f"{code_prefix}_UNREADABLE",
            path=relative_path,
            message=type(error).__name__,
        )
        return None
    finally:
        remaining_hash_bytes[0] = max(0, starting_budget - size)
    return size, digest.hexdigest(), final_info


def hash_regular_file(
    root: Path,
    relative_path: str,
    *,
    diagnostics: Diagnostics,
    code_prefix: str,
    max_file_bytes: int,
    remaining_hash_bytes: list[int],
) -> tuple[int, str] | None:
    max_file_bytes = tightened_limit(
        max_file_bytes, HARD_MAX_HASHED_FILE_BYTES
    )
    if remaining_hash_bytes:
        remaining_hash_bytes[0] = tightened_limit(
            remaining_hash_bytes[0], HARD_MAX_TOTAL_HASH_BYTES
        )
    snapshot = _path_chain_snapshot(
        root,
        relative_path,
        diagnostics=diagnostics,
        code_prefix=code_prefix,
    )
    if snapshot is None:
        return None

    descriptor: int | None = None
    initial_info: os.stat_result | None = None
    if _supports_anchored_no_follow_open():
        base_flags = (
            os.O_RDONLY
            | getattr(os, "O_CLOEXEC", 0)
            | getattr(os, "O_NONBLOCK", 0)
        )
        try:
            descriptor = os.open(
                root,
                base_flags | os.O_DIRECTORY | os.O_NOFOLLOW,
            )
            opened_info = os.fstat(descriptor)
            if (
                not _same_file_identity(snapshot[0][1], opened_info)
                or _stat_stability_key(snapshot[0][1])
                != _stat_stability_key(opened_info)
            ):
                diagnostics.add(
                    f"{code_prefix}_CHANGED",
                    path=relative_path,
                    message="root identity changed before anchored traversal",
                )
                os.close(descriptor)
                descriptor = None
                return None
            for index, component in enumerate(relative_path.split("/"), start=1):
                is_final = index == len(snapshot) - 1
                flags = base_flags | os.O_NOFOLLOW
                if not is_final:
                    flags |= os.O_DIRECTORY
                next_descriptor = os.open(
                    component,
                    flags,
                    dir_fd=descriptor,
                )
                previous_descriptor = descriptor
                descriptor = next_descriptor
                os.close(previous_descriptor)
                opened_info = os.fstat(descriptor)
                expected_info = snapshot[index][1]
                if (
                    not _same_file_identity(expected_info, opened_info)
                    or _stat_stability_key(expected_info)
                    != _stat_stability_key(opened_info)
                ):
                    diagnostics.add(
                        f"{code_prefix}_CHANGED",
                        path=relative_path,
                        message=(
                            "path identity changed before anchored component open"
                        ),
                    )
                    os.close(descriptor)
                    descriptor = None
                    return None
            initial_info = opened_info
        except OSError as error:
            if descriptor is not None:
                try:
                    os.close(descriptor)
                except OSError:
                    pass
                descriptor = None
            verification = _path_chain_snapshot(
                root,
                relative_path,
                diagnostics=diagnostics,
                code_prefix=code_prefix,
            )
            if verification is not None:
                diagnostics.add(
                    f"{code_prefix}_UNREADABLE",
                    path=relative_path,
                    message=type(error).__name__,
                )
            return None
    else:
        # Windows' standard-library os.open lacks dir_fd/O_NOFOLLOW.  Reject
        # every reparse component, then bind the opened descriptor to the
        # before/after lstat identity.  Any instability fails the audit.
        open_flags = (
            os.O_RDONLY
            | getattr(os, "O_BINARY", 0)
            | getattr(os, "O_CLOEXEC", 0)
            | getattr(os, "O_NOINHERIT", 0)
        )
        try:
            descriptor = os.open(snapshot[-1][0], open_flags)
            initial_info = os.fstat(descriptor)
        except OSError as error:
            diagnostics.add(
                f"{code_prefix}_UNREADABLE",
                path=relative_path,
                message=type(error).__name__,
            )
            return None
        if (
            not _same_file_identity(snapshot[-1][1], initial_info)
            or _stat_stability_key(snapshot[-1][1])
            != _stat_stability_key(initial_info)
        ):
            diagnostics.add(
                f"{code_prefix}_CHANGED",
                path=relative_path,
                message="opened file identity differs from the checked path",
            )
            os.close(descriptor)
            descriptor = None
            return None

    try:
        if descriptor is None or initial_info is None:
            diagnostics.add(
                f"{code_prefix}_UNREADABLE",
                path=relative_path,
                message="secure file descriptor setup did not complete",
            )
            return None
        hashed = _hash_open_descriptor(
            descriptor,
            initial_info,
            relative_path=relative_path,
            diagnostics=diagnostics,
            code_prefix=code_prefix,
            max_file_bytes=max_file_bytes,
            remaining_hash_bytes=remaining_hash_bytes,
        )
        if hashed is None:
            return None
        size, digest, final_info = hashed
        if (
            not _same_file_identity(initial_info, final_info)
            or _stat_stability_key(initial_info)
            != _stat_stability_key(final_info)
        ):
            diagnostics.add(
                f"{code_prefix}_CHANGED",
                path=relative_path,
                message="opened file changed while it was being hashed",
            )
            return None
        if not _snapshot_is_stable(
            snapshot,
            relative_path=relative_path,
            diagnostics=diagnostics,
            code_prefix=code_prefix,
        ):
            return None
        return size, digest
    finally:
        if descriptor is not None:
            try:
                os.close(descriptor)
            except OSError:
                pass


def scan_package_root(
    root: Path,
    *,
    max_files: int,
    diagnostics: Diagnostics,
) -> tuple[set[str], bool]:
    max_files = tightened_limit(max_files, HARD_MAX_FILESYSTEM_FILES)
    try:
        root_info = root.lstat()
    except OSError:
        root_info = None
    if root_info is not None and _is_symlink_or_reparse_point(root_info):
        diagnostics.add(
            "PACKAGE_ROOT_SYMLINK",
            message="package root may not be a symlink or reparse point",
        )
        return set(), False
    if root_info is None or not stat.S_ISDIR(root_info.st_mode):
        diagnostics.add(
            "PACKAGE_ROOT_INVALID",
            message="package root is not a directory",
        )
        return set(), False
    found: set[str] = set()
    local_diagnostics = Diagnostics()
    visited_entries = 0
    pending_directories = [(root, root_info)]
    directory_snapshots: list[tuple[Path, os.stat_result]] = []
    while pending_directories:
        directory_path, expected_directory_info = pending_directories.pop()
        try:
            directory_info = directory_path.lstat()
        except OSError as error:
            local_diagnostics.add(
                "PACKAGE_SCAN_FAILED",
                path=directory_path.relative_to(root).as_posix(),
                message=type(error).__name__,
            )
            diagnostics.merge(local_diagnostics)
            return found, False
        if not _same_directory_identity(
            expected_directory_info,
            directory_info,
        ):
            local_diagnostics.add(
                "PACKAGE_SCAN_CHANGED",
                path=directory_path.relative_to(root).as_posix(),
                message="directory identity changed during package scan",
            )
            diagnostics.merge(local_diagnostics)
            return found, False
        directory_snapshots.append((directory_path, directory_info))
        try:
            iterator = os.scandir(directory_path)
        except OSError as error:
            local_diagnostics.add(
                "PACKAGE_SCAN_FAILED",
                path=directory_path.relative_to(root).as_posix(),
                message=type(error).__name__,
            )
            diagnostics.merge(local_diagnostics)
            return found, False
        try:
            with iterator:
                directory_entries: list[os.DirEntry[str]] = []
                for entry in iterator:
                    visited_entries += 1
                    if visited_entries > max_files:
                        diagnostics.merge(local_diagnostics)
                        diagnostics.add(
                            "FILESYSTEM_FILE_LIMIT_EXCEEDED",
                            message=(
                                f"package contains more than {max_files} "
                                "entries"
                            ),
                        )
                        return set(), False
                    directory_entries.append(entry)
                directory_entries.sort(key=lambda entry: entry.name)
                for entry in directory_entries:
                    candidate = Path(entry.path)
                    relative = candidate.relative_to(root).as_posix()
                    problem = path_problem(relative)
                    if problem is not None:
                        local_diagnostics.add(
                            "PACKAGE_PATH_UNSAFE",
                            path=relative[:MAX_PATH_BYTES],
                            message=problem,
                        )
                        continue
                    try:
                        info = entry.stat(follow_symlinks=False)
                    except OSError as error:
                        local_diagnostics.add(
                            "PACKAGE_ENTRY_UNREADABLE",
                            path=relative,
                            message=type(error).__name__,
                        )
                        continue
                    if _is_symlink_or_reparse_point(info):
                        local_diagnostics.add(
                            "PACKAGE_SYMLINK",
                            path=relative,
                            message=(
                                "symlinks and filesystem reparse points "
                                "are not permitted"
                            ),
                        )
                    elif stat.S_ISDIR(info.st_mode):
                        try:
                            child_directory_info = candidate.lstat()
                        except OSError as error:
                            local_diagnostics.add(
                                "PACKAGE_ENTRY_UNREADABLE",
                                path=relative,
                                message=type(error).__name__,
                            )
                            continue
                        if (
                            _is_symlink_or_reparse_point(child_directory_info)
                            or not stat.S_ISDIR(child_directory_info.st_mode)
                        ):
                            local_diagnostics.add(
                                "PACKAGE_SCAN_CHANGED",
                                path=relative,
                                message=(
                                    "directory changed while it was queued"
                                ),
                            )
                            continue
                        pending_directories.append(
                            (candidate, child_directory_info)
                        )
                    elif stat.S_ISREG(info.st_mode):
                        found.add(relative)
                    else:
                        local_diagnostics.add(
                            "PACKAGE_ENTRY_NOT_REGULAR",
                            path=relative,
                            message="entry is not a regular file",
                        )
        except OSError as error:
            local_diagnostics.add(
                "PACKAGE_SCAN_FAILED",
                path=directory_path.relative_to(root).as_posix(),
                message=type(error).__name__,
            )
            diagnostics.merge(local_diagnostics)
            return found, False
    for directory_path, expected_directory_info in directory_snapshots:
        try:
            actual_directory_info = directory_path.lstat()
        except OSError as error:
            local_diagnostics.add(
                "PACKAGE_SCAN_CHANGED",
                path=directory_path.relative_to(root).as_posix(),
                message=type(error).__name__,
            )
            diagnostics.merge(local_diagnostics)
            return found, False
        if not _same_directory_identity(
            expected_directory_info,
            actual_directory_info,
        ):
            local_diagnostics.add(
                "PACKAGE_SCAN_CHANGED",
                path=directory_path.relative_to(root).as_posix(),
                message="directory changed during package scan",
            )
            diagnostics.merge(local_diagnostics)
            return found, False
    diagnostics.merge(local_diagnostics)
    return found, True


def audit(
    manifest: object,
    inventory: object,
    *,
    package_root: Path | None = None,
    editable_root: Path | None = None,
    max_entries: int = DEFAULT_MAX_ENTRIES,
    max_filesystem_files: int = DEFAULT_MAX_FILESYSTEM_FILES,
    max_hashed_file_bytes: int = DEFAULT_MAX_HASHED_FILE_BYTES,
    max_total_hash_bytes: int = DEFAULT_MAX_TOTAL_HASH_BYTES,
    release_gate: bool = False,
) -> dict[str, object]:
    diagnostics = Diagnostics()
    limits = {
        "max_entries": (max_entries, HARD_MAX_ENTRIES),
        "max_filesystem_files": (
            max_filesystem_files,
            HARD_MAX_FILESYSTEM_FILES,
        ),
        "max_hashed_file_bytes": (
            max_hashed_file_bytes,
            HARD_MAX_HASHED_FILE_BYTES,
        ),
        "max_total_hash_bytes": (
            max_total_hash_bytes,
            HARD_MAX_TOTAL_HASH_BYTES,
        ),
    }
    effective_limits: dict[str, int] = {}
    for name, (value, hard_maximum) in limits.items():
        effective = tightened_limit(value, hard_maximum)
        effective_limits[name] = effective
        if effective == 0:
            diagnostics.add(
                "LIMIT_INVALID",
                pointer=f"/options/{name}",
                message="limit must be a positive integer",
            )
    max_entries = effective_limits["max_entries"]
    max_filesystem_files = effective_limits["max_filesystem_files"]
    max_hashed_file_bytes = effective_limits["max_hashed_file_bytes"]
    max_total_hash_bytes = effective_limits["max_total_hash_bytes"]

    if not isinstance(release_gate, bool):
        diagnostics.add(
            "FIELD_TYPE",
            pointer="/options/release_gate",
            message="release_gate must be a boolean",
        )
        release_gate = True
    if release_gate:
        if package_root is None:
            diagnostics.add(
                "RELEASE_PACKAGE_ROOT_REQUIRED",
                pointer="/options/package_root",
                message="release gate requires the distributable package root",
            )
        if editable_root is None:
            diagnostics.add(
                "RELEASE_EDITABLE_ROOT_REQUIRED",
                pointer="/options/editable_root",
                message="release gate requires the editable-source root",
            )

    raw_assets = validate_root(
        manifest,
        expected_format=MANIFEST_FORMAT,
        entries_key="assets",
        label="manifest",
        max_entries=max_entries,
        diagnostics=diagnostics,
    )
    raw_files = validate_root(
        inventory,
        expected_format=INVENTORY_FORMAT,
        entries_key="files",
        label="inventory",
        max_entries=max_entries,
        diagnostics=diagnostics,
    )
    assets = [
        record
        for index, value in enumerate(raw_assets)
        if (
            record := validate_asset(
                value,
                pointer=f"/manifest/assets/{index}",
                diagnostics=diagnostics,
            )
        )
        is not None
    ]
    files = [
        record
        for index, value in enumerate(raw_files)
        if (
            record := validate_inventory_file(
                value,
                pointer=f"/inventory/files/{index}",
                diagnostics=diagnostics,
            )
        )
        is not None
    ]
    assets_by_path = detect_duplicate_paths(
        assets, label="manifest/assets", diagnostics=diagnostics
    )
    files_by_path = detect_duplicate_paths(
        files, label="inventory/files", diagnostics=diagnostics
    )

    path_matches = 0
    checksum_matches = 0
    for file_path in sorted(files_by_path):
        file_record = files_by_path[file_path]
        asset_record = assets_by_path.get(file_path)
        if asset_record is None:
            suspicious = suspicious_import_kind(file_path)
            diagnostics.add(
                (
                    "UNTRACKED_IMPORT_ARTIFACT"
                    if suspicious is not None
                    else "PROVENANCE_MISSING"
                ),
                path=file_path,
                message=(
                    f"untracked {suspicious} may not be redistributed"
                    if suspicious is not None
                    else "inventory file has no provenance record"
                ),
            )
            continue
        path_matches += 1
        inventory_checksum = file_record.get("sha256")
        provenance_checksum = asset_record.get("sha256")
        if (
            isinstance(inventory_checksum, str)
            and isinstance(provenance_checksum, str)
            and inventory_checksum == provenance_checksum
        ):
            checksum_matches += 1
        elif (
            isinstance(inventory_checksum, str)
            and isinstance(provenance_checksum, str)
        ):
            diagnostics.add(
                "PROVENANCE_CHECKSUM_MISMATCH",
                path=file_path,
                message="manifest and inventory SHA-256 values differ",
            )

    for asset_path in sorted(set(assets_by_path) - set(files_by_path)):
        diagnostics.add(
            "PROVENANCE_STALE",
            path=asset_path,
            message="provenance record is absent from distributable inventory",
        )

    if package_root is not None:
        remaining_hash_bytes = [max_total_hash_bytes]
        package_scan_diagnostic_start = diagnostics.total_count
        package_files, package_scan_complete = scan_package_root(
            package_root,
            max_files=max_filesystem_files,
            diagnostics=diagnostics,
        )
        package_scan_clean = (
            diagnostics.total_count == package_scan_diagnostic_start
        )
        if package_scan_complete:
            for path in sorted(package_files - set(files_by_path)):
                suspicious = suspicious_import_kind(path)
                diagnostics.add(
                    (
                        "UNTRACKED_IMPORT_ARTIFACT"
                        if suspicious is not None
                        else "INVENTORY_MISSING_FILE"
                    ),
                    path=path,
                    message=(
                        f"untracked {suspicious} may not be redistributed"
                        if suspicious is not None
                        else "package file is absent from supplied inventory"
                    ),
                )
            for path in sorted(set(files_by_path) - package_files):
                diagnostics.add(
                    "INVENTORY_FILE_MISSING",
                    path=path,
                    message="inventory path is absent from package root",
                )
            for path in sorted(package_files & set(files_by_path)):
                actual = hash_regular_file(
                    package_root,
                    path,
                    diagnostics=diagnostics,
                    code_prefix="PACKAGE_FILE",
                    max_file_bytes=max_hashed_file_bytes,
                    remaining_hash_bytes=remaining_hash_bytes,
                )
                if actual is None:
                    continue
                actual_size, actual_hash = actual
                record = files_by_path[path]
                if (
                    record.get("size") is not None
                    and record["size"] != actual_size
                ):
                    diagnostics.add(
                        "INVENTORY_SIZE_MISMATCH",
                        path=path,
                        message="inventory size differs from package file",
                    )
                if (
                    record.get("sha256") is not None
                    and record["sha256"] != actual_hash
                ):
                    diagnostics.add(
                        "INVENTORY_CHECKSUM_MISMATCH",
                        path=path,
                        message="inventory SHA-256 differs from package file",
                    )
            if package_scan_clean:
                verification_diagnostics = Diagnostics()
                verification_files, verification_complete = scan_package_root(
                    package_root,
                    max_files=max_filesystem_files,
                    diagnostics=verification_diagnostics,
                )
                if (
                    not verification_complete
                    or verification_diagnostics.items
                    or verification_files != package_files
                ):
                    diagnostics.add(
                        "PACKAGE_SNAPSHOT_CHANGED",
                        message=(
                            "package paths changed between inventory scan "
                            "and checksum verification"
                        ),
                    )

    if editable_root is not None:
        try:
            editable_root_info = editable_root.lstat()
        except OSError:
            editable_root_info = None
        if (
            editable_root_info is not None
            and _is_symlink_or_reparse_point(editable_root_info)
        ):
            diagnostics.add(
                "EDITABLE_ROOT_SYMLINK",
                message=(
                    "editable-source root may not be a symlink or reparse point"
                ),
            )
        elif (
            editable_root_info is None
            or not stat.S_ISDIR(editable_root_info.st_mode)
        ):
            diagnostics.add(
                "EDITABLE_ROOT_INVALID",
                message="editable-source root is not a directory",
            )
        else:
            if package_root is None:
                remaining_hash_bytes = [max_total_hash_bytes]
            editable_records: dict[str, str] = {}
            for asset in assets:
                source_path = asset.get("editable_path")
                source_hash = asset.get("editable_sha256")
                if not isinstance(source_path, str) or not isinstance(
                    source_hash, str
                ):
                    continue
                previous = editable_records.get(source_path)
                if previous is not None and previous != source_hash:
                    diagnostics.add(
                        "EDITABLE_SOURCE_CONFLICT",
                        path=source_path,
                        message="editable source has conflicting checksums",
                    )
                else:
                    editable_records[source_path] = source_hash
            for source_path in sorted(editable_records):
                actual = hash_regular_file(
                    editable_root,
                    source_path,
                    diagnostics=diagnostics,
                    code_prefix="EDITABLE_SOURCE",
                    max_file_bytes=max_hashed_file_bytes,
                    remaining_hash_bytes=remaining_hash_bytes,
                )
                if (
                    actual is not None
                    and actual[1] != editable_records[source_path]
                ):
                    diagnostics.add(
                        "EDITABLE_SOURCE_CHECKSUM_MISMATCH",
                        path=source_path,
                        message="editable-source SHA-256 differs from manifest",
                    )

    sorted_diagnostics = diagnostics.sorted()
    return {
        "diagnostics": sorted_diagnostics,
        "format": REPORT_FORMAT,
        "ok": diagnostics.total_count == 0,
        "summary": {
            "checksum_matched_files": checksum_matches,
            "errors": diagnostics.total_count,
            "inventory_files": len(files_by_path),
            "manifest_assets": len(assets_by_path),
            "path_matched_files": path_matches,
        },
    }


def failure_report(error: InputFailure) -> dict[str, object]:
    diagnostics = Diagnostics()
    diagnostics.add(
        error.code,
        message=error.detail,
        pointer=f"/{error.label}",
    )
    return {
        "diagnostics": diagnostics.sorted(),
        "format": REPORT_FORMAT,
        "ok": False,
        "summary": {
            "checksum_matched_files": 0,
            "errors": diagnostics.total_count,
            "inventory_files": 0,
            "manifest_assets": 0,
            "path_matched_files": 0,
        },
    }


def write_report(report: dict[str, object]) -> None:
    sys.stdout.write(
        json.dumps(
            report,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    )


def positive_integer(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Audit a distributable inventory against content provenance."
    )
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--package-root", type=Path)
    parser.add_argument("--editable-root", type=Path)
    parser.add_argument(
        "--release-gate",
        action="store_true",
        help=(
            "require and verify both package and editable-source roots; "
            "without this flag the audit is metadata-only preflight"
        ),
    )
    parser.add_argument(
        "--max-input-bytes",
        type=positive_integer,
        default=DEFAULT_MAX_INPUT_BYTES,
    )
    parser.add_argument(
        "--max-entries",
        type=positive_integer,
        default=DEFAULT_MAX_ENTRIES,
    )
    parser.add_argument(
        "--max-filesystem-files",
        type=positive_integer,
        default=DEFAULT_MAX_FILESYSTEM_FILES,
    )
    parser.add_argument(
        "--max-hashed-file-bytes",
        type=positive_integer,
        default=DEFAULT_MAX_HASHED_FILE_BYTES,
    )
    parser.add_argument(
        "--max-total-hash-bytes",
        type=positive_integer,
        default=DEFAULT_MAX_TOTAL_HASH_BYTES,
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
    try:
        manifest = read_json(
            arguments.manifest,
            label="manifest",
            max_bytes=arguments.max_input_bytes,
        )
        inventory = read_json(
            arguments.inventory,
            label="inventory",
            max_bytes=arguments.max_input_bytes,
        )
    except InputFailure as error:
        write_report(failure_report(error))
        return 2
    report = audit(
        manifest,
        inventory,
        package_root=arguments.package_root,
        editable_root=arguments.editable_root,
        max_entries=arguments.max_entries,
        max_filesystem_files=arguments.max_filesystem_files,
        max_hashed_file_bytes=arguments.max_hashed_file_bytes,
        max_total_hash_bytes=arguments.max_total_hash_bytes,
        release_gate=arguments.release_gate,
    )
    write_report(report)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
