"""Repository and user-directory locations rorsmith is allowed to touch.

Everything here is discovered, never guessed at call time: a wrong repo root
silently reads a stale sanitizer and every band this server reports would be a
lie.  Discovery therefore fails loudly instead of falling back.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

# Marker files that identify the Rigs of Rods source tree.  All three must be
# present; any one of them alone also appears in unrelated checkouts.
_REPO_MARKERS = (
    Path("source/main/resources/LegacyMaterialCompatibilityPlan.h"),
    Path("source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.cpp"),
    Path("tools/generate_cityworld_roughness_repair_edits.py"),
)


class RorsmithError(RuntimeError):
    """A refusal with a truthful reason. Never raised for 'probably fine'."""

    def __init__(self, reason: str, detail: str = "") -> None:
        super().__init__(reason if not detail else f"{reason}: {detail}")
        self.reason = reason
        self.detail = detail


def _looks_like_repo(candidate: Path) -> bool:
    return all((candidate / marker).is_file() for marker in _REPO_MARKERS)


def find_repo_root() -> Path:
    """Locate the Rigs of Rods checkout this server speaks for."""
    explicit = os.environ.get("RORSMITH_REPO_ROOT")
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser())
    project_dir = os.environ.get("CLAUDE_PROJECT_DIR")
    if project_dir:
        candidates.append(Path(project_dir).expanduser())
    # This file lives at <root>/tools/mcp/rorsmith/rorsmith/paths.py.
    candidates.append(Path(__file__).resolve().parents[4])
    candidates.append(Path.cwd())
    candidates.extend(Path.cwd().parents)

    seen: set[Path] = set()
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if resolved in seen:
            continue
        seen.add(resolved)
        if _looks_like_repo(resolved):
            return resolved
    raise RorsmithError(
        "repo_root_not_found",
        "set RORSMITH_REPO_ROOT to a Rigs of Rods checkout containing "
        + ", ".join(str(marker) for marker in _REPO_MARKERS),
    )


@dataclass(frozen=True)
class Layout:
    """Every path rorsmith reads or (only on an explicit apply) writes."""

    repo_root: Path
    user_dir: Path
    mods_dirs: tuple[Path, ...]
    backup_dir: Path

    @property
    def sanitizer_cpp(self) -> Path:
        return self.repo_root / "source/main/resources/LegacyMaterialScriptSanitizer.cpp"

    @property
    def compatibility_plan_h(self) -> Path:
        return self.repo_root / "source/main/resources/LegacyMaterialCompatibilityPlan.h"

    @property
    def private_policy_cpp(self) -> Path:
        return self.repo_root / "source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.cpp"

    @property
    def private_policy_h(self) -> Path:
        return self.repo_root / "source/main/gfx/ogre14/detail/OgreNextDemoPrivatePolicy.h"

    @property
    def roughness_edit_generator(self) -> Path:
        return self.repo_root / "tools/generate_cityworld_roughness_repair_edits.py"

    @property
    def family_classifier(self) -> Path:
        return self.repo_root / "tools/classify_cityworld_material_families.py"

    @property
    def ground_coverage_tool(self) -> Path:
        return self.repo_root / "tools/derive_cityworld_ground_coverage.py"

    def resolve_archive(self, archive: str) -> Path:
        """Resolve an archive name or path to exactly one readable file."""
        given = Path(archive).expanduser()
        if given.is_absolute() or given.parent != Path("."):
            if not given.is_file():
                raise RorsmithError("archive_not_found", str(given))
            return given.resolve()
        hits = [d / given.name for d in self.mods_dirs if (d / given.name).is_file()]
        if not hits:
            raise RorsmithError(
                "archive_not_found",
                f"{given.name} is in none of "
                + ", ".join(str(d) for d in self.mods_dirs),
            )
        return hits[0].resolve()


def _user_dir() -> Path:
    override = os.environ.get("RORSMITH_ROR_USER_DIR")
    if override:
        return Path(override).expanduser()
    return Path.home() / "RigsOfRods"


def build_layout() -> Layout:
    repo_root = find_repo_root()
    user_dir = _user_dir()
    app_support = (
        Path.home() / "Library/Application Support/Rigs of Rods"
    )
    mods_dirs = tuple(
        d for d in (user_dir / "mods", app_support / "mods") if d.is_dir()
    )
    if not mods_dirs:
        raise RorsmithError(
            "mods_dir_not_found",
            f"neither {user_dir / 'mods'} nor {app_support / 'mods'} exists",
        )
    return Layout(
        repo_root=repo_root,
        user_dir=user_dir,
        mods_dirs=mods_dirs,
        backup_dir=user_dir / "mods-originals",
    )
