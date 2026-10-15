"""Build what the update tests need: a stand-in ``gh`` and stand-in packages.

Purpose:
    ``jedimaster update`` talks to GitHub only through the ``gh`` command.
    Tests put a script called ``gh`` first on ``PATH`` that answers from a
    JSON file, and build packages shaped like the CI's (one top folder, the
    engine program, the AI program inside ``ai/``), so no network is used.

Flow:
    ``make_gh`` writes the script and its state; ``make_package`` writes a
    ``.tar.xz`` whose engine says the given version and AI version;
    ``add_run`` lists a CI run with its package. The script logs each
    call to ``calls.log`` beside the state.

Invariants:
    - No network and no real build; every commit and name is made up.

Call:
    ``gh = make_gh(tmp_path, monkeypatch)``
"""

from __future__ import annotations

import io
import json
import logging
import stat
import sys
import tarfile
from pathlib import Path

logger = logging.getLogger(__name__)

GH = f"""#!{sys.executable}
import json, pathlib, shutil, sys
here = pathlib.Path(__file__).parent
state = json.loads((here / "state.json").read_text())
args = sys.argv[1:]
with open(here / "calls.log", "a") as log:
    log.write(json.dumps(args) + "\\n")
if args[:2] == ["auth", "status"]:
    if not state["logged_in"]:
        sys.stderr.write("You are not logged in to any GitHub hosts.\\n")
    sys.exit(0 if state["logged_in"] else 1)
if args[0] == "api" and "/commits/" in args[1]:
    wanted = args[1].rsplit("/", 1)[1]
    found = [c for c in state["commits"] if c.startswith(wanted)]
    if not found:
        sys.stderr.write("gh: Not Found (HTTP 404)\\n")
        sys.exit(1)
    print(found[0])
    sys.exit(0)
if args[0] == "api" and "/actions/workflows/ci.yml/runs?" in args[1]:
    query = dict(p.split("=") for p in args[1].split("?", 1)[1].split("&"))
    runs = [
        r for r in state["runs"]
        if (("branch" not in query) or r["branch"] == query["branch"])
        and (("event" not in query) or r["event"] == query["event"])
        and (("head_sha" not in query) or r["head_sha"] == query["head_sha"])
    ]
    print(json.dumps({{"workflow_runs": [
        {{"id": r["id"], "head_sha": r["head_sha"]}} for r in runs[: int(query["per_page"])]
    ]}}))
    sys.exit(0)
if args[:2] == ["run", "download"]:
    run = args[2]
    name = args[args.index("--name") + 1]
    folder = pathlib.Path(args[args.index("--dir") + 1])
    package = state["packages"].get(run)
    if package is None or name != state["artifact"]:
        sys.stderr.write("no artifact matches the given name\\n")
        sys.exit(1)
    shutil.copy(package, folder / pathlib.Path(package).name)
    sys.exit(0)
sys.stderr.write("stand-in gh: unexpected " + repr(args) + "\\n")
sys.exit(3)
"""


def make_gh(root: Path, monkeypatch, logged_in: bool = True) -> Path:
    """Put a stand-in ``gh`` first on ``PATH``; return the folder holding it and its state."""
    folder = root / "ghbin"
    folder.mkdir()
    script = folder / "gh"
    script.write_text(GH, encoding="utf-8")
    script.chmod(script.stat().st_mode | stat.S_IXUSR)
    state = {
        "logged_in": logged_in,
        "artifact": "openxvt-linux-x86_64-release",
        "commits": [],
        "runs": [],
        "packages": {},
    }
    (folder / "state.json").write_text(json.dumps(state), encoding="utf-8")
    monkeypatch.setenv("PATH", f"{folder}:/usr/bin:/bin")
    return folder


def read_state(gh: Path) -> dict:
    """Return the stand-in's state."""
    return json.loads((gh / "state.json").read_text(encoding="utf-8"))


def calls(gh: Path) -> list[list[str]]:
    """Return every call the stand-in answered, in order."""
    path = gh / "calls.log"
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text().splitlines()]


def _file(archive: tarfile.TarFile, name: str, text: str, mode: int) -> None:
    data = text.encode("utf-8")
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    archive.addfile(info, io.BytesIO(data))


def make_package(
    root: Path,
    commit: str,
    shown: str | None = None,
    engine_wire: int = 11,
    ai_wire: int | None = 11,
) -> Path:
    """Write a CI-shaped ``.tar.xz`` of ``commit``; return its path.

    The engine says ``OpenXvT <shown>`` (default: the commit's first 7
    characters) and ``ai_wire=<engine_wire>``; the packaged AI program says
    ``ai_wire=<ai_wire>``, or is left out when ``ai_wire`` is None.
    """
    shown = commit[:7] if shown is None else shown
    top = f"openxvt-{commit[:7]}-linux-x86_64-release"
    engine = (
        f"#!{sys.executable}\n"
        "import sys\n"
        f"if sys.argv[1:] == ['--version']:\n"
        f"    print('OpenXvT {shown}')\n"
        f"    print('ai_wire={engine_wire}')\n"
        "    sys.exit(0)\n"
        "sys.exit(7)\n"
    )
    program = f"import sys\nprint('ai_wire={ai_wire}')\nsys.exit(0)\n"
    path = root / f"{top}.tar.xz"
    with tarfile.open(path, "w:xz") as archive:
        _file(archive, f"{top}/OpenXvT", engine, 0o755)
        _file(archive, f"{top}/README.md", "readme\n", 0o644)
        if ai_wire is not None:
            _file(archive, f"{top}/ai/jedimaster_ai/__main__.py", program, 0o644)
    return path


def add_run(
    gh: Path,
    run_id: str,
    commit: str,
    package: Path | None,
    branch: str = "main",
    event: str = "push",
) -> None:
    """List a successful CI run of ``commit`` in the stand-in, newest listed first."""
    state = read_state(gh)
    state["commits"].append(commit)
    state["runs"].insert(
        0, {"id": run_id, "head_sha": commit, "branch": branch, "event": event}
    )
    if package is not None:
        state["packages"][run_id] = str(package)
    (gh / "state.json").write_text(json.dumps(state), encoding="utf-8")
