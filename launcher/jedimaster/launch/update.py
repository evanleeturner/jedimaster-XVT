"""The ``update`` command: install a game build from CI and make it current.

Purpose:
    ``jedimaster update`` fetches the newest game build that passed CI, the
    one whose package carries its own AI program, and makes it the build the
    launcher flies battles in. It runs only when the player runs it: nothing
    in the launcher updates by itself.

Flow:
    ``add_parser`` adds the command; ``run`` dispatches ``--list``, ``--use
    COMMIT`` or the update. An update asks ``gh`` for the newest successful
    ``ci.yml`` run on ``main`` (or the run of ``--commit``), downloads its
    package for this system, unpacks it into ``builds/<12 of the commit>.partial``,
    checks it (``verify``), renames the folder to ``builds/<12>``, makes it
    current (``builds.set_current``) and removes the oldest installs.
    ``--use`` runs the same check on an installed build before making it
    current, so going back is one command.

Invariants:
    - A build becomes current only after ``verify``: its ``--version`` names
      the commit, and its AI version equals its packaged AI program's.
    - A failed install leaves no ``.partial`` folder and does not change
      ``current``.
    - Every refusal is one plain sentence on the log, exit status 1.
    - Nothing here restarts a running launcher: it reads ``current`` at the
      start of each battle.
    - ``gh`` is the only way out to the network.

Call:
    ``python -m jedimaster update`` or ``python -m jedimaster update --list``
"""

from __future__ import annotations

import argparse
import json
import logging
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path
from typing import Any

from . import builds as builds_module
from .ai_wire_check import check_pair
from .ai_wire_check import engine_says
from .ai_wire_check import PairRefused

logger = logging.getLogger(__name__)

DEFAULT_REPO = "evanleeturner/jedimaster-XVT"
WORKFLOW = "ci.yml"
BRANCH = "main"
GH_SECONDS = 600.0
SHORT = 12
FULL_SHA = re.compile(r"[0-9a-f]{40}")
COMMIT_TEXT = re.compile(r"[0-9a-fA-F]{4,40}")

NO_GH = (
    "The GitHub command line tool gh was not found: install it, run "
    '"gh auth login", then try again.'
)
NOT_LOGGED_IN = 'gh is not logged in to GitHub: run "gh auth login", then try again.'
MACOS = "Updating on macOS is not built yet."
OTHER_SYSTEM = "Updating is not built for this system yet."
NOT_A_COMMIT = "A commit is 4 to 40 letters a to f and digits."


class UpdateError(Exception):
    """An update that cannot go on; the text is the one sentence for the player."""


def artifact_for(platform: str) -> tuple[str, str]:
    """Return (the CI artifact's name, the package's suffix) for ``platform``.

    Raises UpdateError on macOS and on a system with no package.
    """
    if platform.startswith("linux"):
        return "openxvt-linux-x86_64-release", ".tar.xz"
    if platform.startswith("win"):
        return "openxvt-windows-x86_64-release", ".zip"
    raise UpdateError(MACOS if platform == "darwin" else OTHER_SYSTEM)


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``update`` command to ``sub``. Returns None; parses nothing."""
    update = sub.add_parser(
        "update",
        help="install the newest CI game build, with its AI program, and make it current",
    )
    update.add_argument("--commit", help="install the build of this commit instead")
    update.add_argument(
        "--repo",
        default=DEFAULT_REPO,
        help=f"the GitHub repository (default {DEFAULT_REPO})",
    )
    update.add_argument(
        "--builds", help="the folder of installed builds (default: the user's own)"
    )
    update.add_argument(
        "--list", action="store_true", help="list the installed builds and exit"
    )
    update.add_argument(
        "--use",
        metavar="COMMIT",
        help="make an installed build current again, after checking it",
    )


def _gh(arguments: list[str]) -> str:
    """Run ``gh`` with ``arguments``; return its output or raise UpdateError."""
    command = ["gh", *arguments]
    logger.debug("running %s", command)
    try:
        done = subprocess.run(
            command, capture_output=True, text=True, timeout=GH_SECONDS, check=False
        )
    except subprocess.TimeoutExpired:
        raise UpdateError(f"gh did not finish within {GH_SECONDS:g} seconds.") from None
    except OSError:
        raise UpdateError(NO_GH) from None
    if done.returncode != 0:
        detail = (done.stderr or "").strip().splitlines()
        reason = detail[-1].rstrip(".") if detail else f"exit status {done.returncode}"
        raise UpdateError(f"gh could not do that: {reason}.")
    return done.stdout


def _need_gh() -> None:
    """Raise UpdateError unless ``gh`` is installed and logged in."""
    if shutil.which("gh") is None:
        raise UpdateError(NO_GH)
    try:
        done = subprocess.run(
            ["gh", "auth", "status"],
            capture_output=True,
            text=True,
            timeout=60.0,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        raise UpdateError(NO_GH) from None
    if done.returncode != 0:
        raise UpdateError(NOT_LOGGED_IN)


def find_run(repo: str, commit: str | None) -> tuple[str, str]:
    """Return (run id, full commit) of the CI run to install.

    With ``commit``: its successful ``ci.yml`` run. Without: the newest
    successful push run on ``main``. Raises UpdateError when there is none.
    """
    base = f"repos/{repo}/actions/workflows/{WORKFLOW}/runs?status=success&per_page=1"
    if commit is None:
        query = f"{base}&branch={BRANCH}&event=push"
        wanted = "No successful Package CI run was found on main."
    else:
        full = commit.lower()
        if not FULL_SHA.fullmatch(full):
            full = _gh(["api", f"repos/{repo}/commits/{full}", "--jq", ".sha"]).strip()
        query = f"{base}&head_sha={full}"
        wanted = f"No successful Package CI run was found for commit {commit}."
    runs: list[dict[str, Any]] = json.loads(_gh(["api", query]) or "{}").get(
        "workflow_runs", []
    )
    if not runs:
        raise UpdateError(wanted)
    return str(runs[0]["id"]), str(runs[0]["head_sha"])


def _safe_member(name: str) -> bool:
    """Say whether an archive member's path stays inside the folder it unpacks into."""
    path = Path(name)
    return not path.is_absolute() and ".." not in path.parts


def unpack(package: Path, into: Path) -> None:
    """Unpack the ``.tar.xz`` or ``.zip`` at ``package`` into ``into``.

    Raises UpdateError for a package that cannot be read or holds a path
    that leaves the folder. A single top folder is lifted up one level, so
    the engine program sits directly in ``into``.
    """
    into.mkdir(parents=True)
    try:
        if package.name.endswith(".zip"):
            with zipfile.ZipFile(package) as archive:
                if not all(_safe_member(n) for n in archive.namelist()):
                    raise UpdateError("The package holds a path outside its folder.")
                archive.extractall(into)
        else:
            with tarfile.open(package, "r:xz") as archive:
                members = archive.getmembers()
                if not all(
                    _safe_member(m.name)
                    and (not (m.issym() or m.islnk()) or _safe_member(m.linkname))
                    and not m.isdev()
                    for m in members
                ):
                    raise UpdateError("The package holds a path outside its folder.")
                if hasattr(tarfile, "data_filter"):
                    archive.extractall(into, members=members, filter="data")
                else:  # before Python 3.11.4 the paths were checked above
                    archive.extractall(into, members=members)
    except (tarfile.TarError, zipfile.BadZipFile, OSError, EOFError) as exc:
        raise UpdateError(f"The package could not be unpacked ({exc}).") from None
    children = list(into.iterdir())
    if len(children) == 1 and children[0].is_dir():
        top = children[0]
        for item in list(top.iterdir()):
            item.rename(into / item.name)
        top.rmdir()


def verify(build: Path, name: str) -> None:
    """Raise UpdateError unless the build in ``build`` is the commit ``name`` and matches itself.

    Its ``--version`` first line must name the commit (as a start of ``name``
    or the other way round), and its AI version must equal that of the AI
    program packaged with it.
    """
    engine = builds_module.engine_in(build)
    if engine is None:
        raise UpdateError(f"The build {name} holds no game program.")
    text, _ = engine_says(engine)
    if text is None:
        raise UpdateError(f"The build {name} did not say its version.")
    if not (name.startswith(text) or text.startswith(name)):
        raise UpdateError(
            f"The build {name} says it is version {text}, a different commit."
        )
    try:
        check_pair(engine, None)
    except PairRefused as exc:
        raise UpdateError(f"The build {name} did not pass its check: {exc}") from None


def _install(folder: Path, repo: str, commit: str | None) -> str:
    """Download and install the build; return its folder name, or the said one is current."""
    _need_gh()
    artifact, suffix = artifact_for(sys.platform)
    run_id, sha = find_run(repo, commit)
    name = sha[:SHORT]
    if (folder / name).is_dir() and builds_module.engine_in(folder / name):
        logger.info("build %s is already installed", name)
        return name
    folder.mkdir(parents=True, exist_ok=True)
    partial = folder / f"{name}.partial"
    shutil.rmtree(partial, ignore_errors=True)
    try:
        with tempfile.TemporaryDirectory(prefix="download-", dir=folder) as scratch:
            _gh(
                ["run", "download", run_id, "--repo", repo, "--name", artifact]
                + ["--dir", scratch]
            )
            found = sorted(Path(scratch).rglob(f"*{suffix}"))
            if not found:
                raise UpdateError(f"The CI run {run_id} holds no {artifact} package.")
            unpack(found[0], partial)
        (partial / builds_module.INSTALLED_MARKER).write_text(
            sha + "\n", encoding="utf-8"
        )
        verify(partial, name)
        shutil.rmtree(folder / name, ignore_errors=True)
        partial.rename(folder / name)
    finally:
        shutil.rmtree(partial, ignore_errors=True)
    return name


def _installed_match(folder: Path, text: str) -> str:
    """Return the one installed build whose name starts with ``text``; raise UpdateError."""
    if not COMMIT_TEXT.fullmatch(text):
        raise UpdateError(NOT_A_COMMIT)
    found = [n for n in builds_module.installed(folder) if n.startswith(text.lower())]
    if len(found) != 1:
        which = "No installed build" if not found else "More than one installed build"
        raise UpdateError(f"{which} starts with {text}. See: jedimaster update --list")
    return found[0]


def _list(folder: Path) -> int:
    """Print the installed builds, the current one marked; return 0."""
    names = builds_module.installed(folder)
    if not names:
        print("No builds are installed. Run: jedimaster update")
        return 0
    current = builds_module.current_name(folder)
    for name in names:
        print(f"{'*' if name == current else ' '} {name}")
    return 0


def run(args: argparse.Namespace) -> int:
    """Run the ``update`` command; return the exit status (0 done, 1 refused)."""
    folder = Path(args.builds) if args.builds else builds_module.default_builds_dir()
    if args.list:
        return _list(folder)
    try:
        if args.use:
            name = _installed_match(folder, args.use)
            verify(folder / name, name)
            message = f"Now using build {name}."
        else:
            if args.commit and not COMMIT_TEXT.fullmatch(args.commit):
                raise UpdateError(NOT_A_COMMIT)
            before = builds_module.current_name(folder)
            name = _install(folder, args.repo, args.commit)
            if name == before:
                print(f"Build {name} is already current.")
                return 0
            message = f"Build {name} is installed and current."
        builds_module.set_current(folder, name)
        builds_module.prune(folder)
    except UpdateError as exc:
        logger.error("%s", exc)
        return 1
    print(message)
    return 0
