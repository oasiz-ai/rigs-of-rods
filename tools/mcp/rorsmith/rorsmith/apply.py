"""apply_to_archive: the established archive mutation procedure.

Order is load-bearing and matches what the in-tree Alexis tools do:
  1. read the archive whole,
  2. rewrite only the intended members, every other member byte-identical,
  3. diff and report sha256 before/after per member,
  4. dry run stops here (this is the DEFAULT),
  5. dated backup alongside mods-originals/, never overwriting an existing one,
  6. install into BOTH mods directories,
  7. repin kCityWorld*ArchiveSha256 in LegacyMaterialCompatibilityPlan.h when
     an authenticated archive changed - otherwise the runtime falls back to
     the unauthenticated mount and every road capture fails closed.
"""

from __future__ import annotations

import base64
from pathlib import Path

from . import archive as _archive
from .paths import Layout, RorsmithError


def _decode(value: object, member: str) -> bytes:
    if isinstance(value, str):
        if value.startswith("file:"):
            path = Path(value[5:]).expanduser()
            if not path.is_file():
                raise RorsmithError("change_source_not_found", str(path))
            return path.read_bytes()
        if value.startswith("base64:"):
            try:
                return base64.b64decode(value[7:], validate=True)
            except Exception as exc:
                raise RorsmithError("change_base64_invalid", f"{member}: {exc}") from exc
        return value.encode("utf-8")
    raise RorsmithError(
        "change_value_unsupported",
        f"{member}: give a string, 'file:<path>', or 'base64:<data>'",
    )


def apply_to_archive(
    layout: Layout,
    archive: str,
    changes: dict[str, object],
    dry_run: bool = True,
    backup_label: str = "rorsmith",
    install: bool = True,
    repin: bool = True,
) -> dict[str, object]:
    path = layout.resolve_archive(archive)
    if not changes:
        raise RorsmithError("no_changes", "apply_to_archive needs at least one member")
    if not dry_run:
        _archive.guard_concurrent_edit(path)

    original = path.read_bytes()
    replacements = {member: _decode(value, member) for member, value in changes.items()}
    patched = _archive.rewrite_members(original, replacements)
    diff = _archive.diff(original, patched, path.name)

    report: dict[str, object] = {
        "archive": str(path),
        "dry_run": dry_run,
        "diff": diff.as_dict(),
        "idempotent_noop": diff.idempotent_noop,
        "requested_members": sorted(replacements),
        "untouched_members_byte_identical": len(diff.members) - len(diff.changed),
    }
    unrequested = [m.name for m in diff.changed if m.name not in replacements]
    if unrequested:
        raise RorsmithError(
            "unintended_member_change",
            f"{unrequested} changed but were not requested; refusing to write",
        )

    if diff.idempotent_noop:
        report["result"] = "no_change_required"
        report["note"] = "re-running this call reports zero changes; nothing was written"
        # Still report what the pin would be, so a drifted pin is visible.
        if repin:
            report["repin"] = _archive.repin_plan(layout, path.name, original, apply=False)
        return report

    if dry_run:
        report["result"] = "dry_run"
        report["would_backup"] = str(
            _archive.dated_backup_path(layout, path, backup_label)
        )
        report["would_install"] = [str(d / path.name) for d in layout.mods_dirs]
        if repin:
            report["repin"] = _archive.repin_plan(layout, path.name, patched, apply=False)
        return report

    report["backup"] = _archive.make_backup(layout, path, backup_label)
    path.write_bytes(patched)
    report["written"] = {
        "path": str(path),
        "bytes": len(patched),
        "sha256": _archive.sha256_bytes(patched),
    }
    if install:
        report["installed"] = _archive.install(layout, path.name, patched)
    if repin:
        report["repin"] = _archive.repin_plan(layout, path.name, patched, apply=True)
    report["result"] = "applied"
    report["verify_next"] = (
        "re-run this call to prove idempotency (expect no_change_required), "
        "then verify_live to prove the census still admits the material"
    )
    return report
