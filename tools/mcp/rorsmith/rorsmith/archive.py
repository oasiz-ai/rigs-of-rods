"""Archive read/write on the established byte-exact procedure.

The ZIP primitives are imported from tools/apply_alexis_saber_paint.py; this
module only adds the surrounding procedure: dated backups that never overwrite
an existing one, a member-level diff with sha256 before/after, installation to
both mods directories, and the compatibility-plan repin for authenticated
overlays.
"""

from __future__ import annotations

import datetime as _dt
import hashlib
import os
import shutil
from dataclasses import dataclass, field
from pathlib import Path

from .paths import Layout, RorsmithError
from . import policy, wrapped


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


@dataclass
class MemberDiff:
    name: str
    action: str  # "added" | "changed" | "removed" | "identical"
    sha256_before: str | None
    sha256_after: str | None
    bytes_before: int | None
    bytes_after: int | None

    def as_dict(self) -> dict[str, object]:
        return {
            "member": self.name,
            "action": self.action,
            "sha256_before": self.sha256_before,
            "sha256_after": self.sha256_after,
            "bytes_before": self.bytes_before,
            "bytes_after": self.bytes_after,
        }


@dataclass
class ArchiveDiff:
    archive: str
    sha256_before: str
    sha256_after: str
    bytes_before: int
    bytes_after: int
    members: list[MemberDiff] = field(default_factory=list)

    @property
    def changed(self) -> list[MemberDiff]:
        return [m for m in self.members if m.action != "identical"]

    @property
    def idempotent_noop(self) -> bool:
        return not self.changed

    def as_dict(self) -> dict[str, object]:
        return {
            "archive": self.archive,
            "sha256_before": self.sha256_before,
            "sha256_after": self.sha256_after,
            "bytes_before": self.bytes_before,
            "bytes_after": self.bytes_after,
            "members_total": len(self.members),
            "members_changed": len(self.changed),
            "changed": [m.as_dict() for m in self.changed],
        }


def read_members(payload: bytes) -> dict[str, bytes]:
    """Decompressed member payloads, via the proven central-directory reader."""
    zipper = wrapped.archive_primitives()
    return {
        entry.name: zipper._member_payload(entry)
        for entry in zipper._read_entries(payload)
    }


def rewrite_members(payload: bytes, replacements: dict[str, bytes]) -> bytes:
    """Replace the named members; every other member stays byte-identical.

    Untouched members are re-emitted from their original local record, so
    their compressed bytes, extra field, and timestamps survive verbatim
    rather than being re-deflated to something merely equivalent.
    """
    zipper = wrapped.archive_primitives()
    entries = zipper._read_entries(payload)
    present = {entry.name for entry in entries}
    rebuilt = []
    for entry in entries:
        if entry.name in replacements:
            new_payload = replacements[entry.name]
            if zipper._member_payload(entry) == new_payload:
                rebuilt.append(entry)  # idempotent: keep the original bytes
            else:
                rebuilt.append(zipper._authored_entry(entry.name, new_payload))
        else:
            rebuilt.append(entry)
    for name in replacements:
        if name not in present:
            rebuilt.append(zipper._authored_entry(name, replacements[name]))
    return zipper._rebuild(rebuilt)


def diff(before: bytes, after: bytes, archive_name: str) -> ArchiveDiff:
    old = read_members(before)
    new = read_members(after)
    members: list[MemberDiff] = []
    for name in sorted(set(old) | set(new)):
        a, b = old.get(name), new.get(name)
        if a is None:
            action = "added"
        elif b is None:
            action = "removed"
        elif a != b:
            action = "changed"
        else:
            action = "identical"
        members.append(
            MemberDiff(
                name=name,
                action=action,
                sha256_before=sha256_bytes(a) if a is not None else None,
                sha256_after=sha256_bytes(b) if b is not None else None,
                bytes_before=len(a) if a is not None else None,
                bytes_after=len(b) if b is not None else None,
            )
        )
    return ArchiveDiff(
        archive=archive_name,
        sha256_before=sha256_bytes(before),
        sha256_after=sha256_bytes(after),
        bytes_before=len(before),
        bytes_after=len(after),
        members=members,
    )


def dated_backup_path(layout: Layout, archive: Path, label: str) -> Path:
    stamp = _dt.date.today().isoformat()
    safe = "".join(c if c.isalnum() or c in "-_" else "-" for c in label).strip("-")
    return layout.backup_dir / f"{archive.stem}.{safe or 'rorsmith'}-{stamp}.zip"


def make_backup(layout: Layout, archive: Path, label: str) -> dict[str, object]:
    """Copy the untouched archive aside. Never overwrites an existing backup."""
    target = dated_backup_path(layout, archive, label)
    if target.exists():
        existing = target.read_bytes()
        return {
            "path": str(target),
            "created": False,
            "reason": "backup_already_exists",
            "sha256": sha256_bytes(existing),
        }
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(archive, target)
    return {
        "path": str(target),
        "created": True,
        "sha256": sha256_bytes(target.read_bytes()),
    }


def install(layout: Layout, name: str, payload: bytes) -> list[dict[str, object]]:
    """Write the archive into every mods directory that already carries one."""
    results: list[dict[str, object]] = []
    for mods in layout.mods_dirs:
        target = mods / name
        if not target.exists():
            results.append(
                {"path": str(target), "written": False, "reason": "absent_from_this_mods_dir"}
            )
            continue
        temporary = target.with_suffix(target.suffix + ".rorsmith-tmp")
        temporary.write_bytes(payload)
        os.replace(temporary, target)
        results.append(
            {
                "path": str(target),
                "written": True,
                "sha256": sha256_bytes(payload),
                "bytes": len(payload),
            }
        )
    return results


def repin_plan(
    layout: Layout, archive_name: str, payload: bytes, apply: bool
) -> dict[str, object] | None:
    """Repin kCityWorld*ArchiveSha256 when an authenticated archive changes."""
    symbol = policy.PINNED_ARCHIVES.get(archive_name)
    if symbol is None:
        return None
    header = layout.compatibility_plan_h
    text = header.read_text(encoding="utf-8")
    digest = sha256_bytes(payload)
    size = len(payload)
    current = policy.compatibility_pins(layout).get(symbol, {})
    if current.get("sha256") == digest and current.get("bytes") == size:
        return {
            "symbol": symbol,
            "header": str(header),
            "action": "already_pinned",
            "sha256": digest,
            "bytes": size,
        }
    updated = policy.repin_text(text, symbol, digest, size)
    record = {
        "symbol": symbol,
        "header": str(header),
        "action": "repinned" if apply else "would_repin",
        "sha256_before": current.get("sha256"),
        "bytes_before": current.get("bytes"),
        "sha256_after": digest,
        "bytes_after": size,
    }
    if apply:
        header.write_text(updated, encoding="utf-8")
    return record


def guard_concurrent_edit(archive: Path) -> None:
    """Refuse to write an archive another agent is actively authoring."""
    if archive.name == "AlexisSaber.zip":
        raise RorsmithError(
            "archive_under_concurrent_edit",
            "AlexisSaber.zip is owned by the Alexis authoring agents this "
            "session; rorsmith reads it but will not write it",
        )
