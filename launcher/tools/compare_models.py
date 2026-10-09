"""Build the models' views of an install, render them, and diff them against the sheets.

Purpose:
    The acceptance fence of the model reader, the drawing and the object
    tables: for both views (``base`` reads the install as if Balance of
    Power were absent, ``bop`` as it is) and the three kinds (``file``,
    ``draw``, ``objects``), render the view and diff it against
    ``<answers>/<view>/<kind>.txt``; check each run's exit status in
    ``<answers>/status.txt``, every ``draw`` pass line's ``walk=same``,
    and two families of the game's log lines: one palette line per
    texture (``opaque_palette``, or ``alpha_palette`` with the count of
    indices that do not glow) and one ``render.node_ref_missing`` per name
    reference a pass walked that stands for nothing.

Flow:
    1. For each view, build ``models_view`` and ``objects_view``.
    2. For each kind, render, diff (``draw``: the pass line's ``meshes=``
       and ``walk=`` left out of the diff, ``meshes=`` counted apart), and
       check the status and the logs.
    3. Print one block per sheet that differs, then a summary line.

Invariants:
    - The answer folder and the install are required options; nothing about
      the game's files is written into this tool.
    - A sheet that cannot be read counts as a difference.
    - Exit status 0 only when all six sheets match and every check passes.

Call:
    ``python tools/compare_models.py --answers DIR --install DIR [--context N]``
"""

from __future__ import annotations

import argparse
import difflib
import logging
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
sys.path.insert(0, str(HERE.parents[1]))

from jedimaster.models import draw_model  # noqa: E402
from jedimaster.models import glow_of  # noqa: E402
from jedimaster.models import models_view  # noqa: E402
from jedimaster.models import ModelsView  # noqa: E402
from jedimaster.models import objects_view  # noqa: E402
from jedimaster.models import render_draw  # noqa: E402
from jedimaster.models import render_file  # noqa: E402
from jedimaster.models import render_objects  # noqa: E402

logger = logging.getLogger("compare_models")

VIEWS = ("base", "bop")
KINDS = ("file", "draw", "objects")
PASS_COUNTS = re.compile(r"^(pass .*) meshes=(\d+) (faces=\d+) walk=(\S+)$")
STATUS = re.compile(r"^(\w+) (\w+) exit=(-?\d+)$")


def mask_passes(text: str) -> tuple[list[str], list[str], list[str]]:
    """Return a draw sheet's lines without ``meshes=``/``walk=``, and both lists.

    The second list holds each pass line's ``meshes`` value, the third
    its ``walk`` value, in order. Other lines are kept as they are. Does
    not check the values.
    """
    lines, meshes, walks = [], [], []
    for line in text.splitlines():
        match = PASS_COUNTS.match(line)
        if match:
            lines.append(f"{match.group(1)} {match.group(3)}")
            meshes.append(match.group(2))
            walks.append(match.group(4))
        else:
            lines.append(line)
    return lines, meshes, walks


def statuses(answers: Path) -> dict[tuple[str, str], int]:
    """Return each run's exit status from ``status.txt``, by (view, kind).

    Returns ``{}`` when the file cannot be read. Does not check the kinds.
    """
    try:
        text = (answers / "status.txt").read_text(encoding="latin-1")
    except OSError:
        return {}
    found = {}
    for line in text.splitlines():
        match = STATUS.match(line.strip())
        if match:
            found[(match.group(1), match.group(2))] = int(match.group(3))
    return found


def palette_lines(view: ModelsView) -> list[str]:
    """Return the palette log line each texture of the view should give, in order.

    ``opaque_palette`` for a palette in which no index glows, else
    ``alpha_palette cleared=N`` with N the indices that do not glow, by
    each index's own result (so 0 for a palette in which every index
    glows, which as a whole has no glow). Does not read a log.
    """
    out = []
    for entry in view.models:
        if entry.model is None:
            continue
        for node in entry.model.nodes:
            if node.texture is None:
                continue
            lit = sum(glow_of(node.texture).mask)
            out.append(
                f"alpha_palette cleared={256 - lit}" if lit else "opaque_palette"
            )
    return out


def missing_counts(view: ModelsView) -> list[tuple[str, int]]:
    """Return (game name, references that stand for nothing) per model drawn.

    Summed over every pass the draw sheet prints; models with none are
    left out. Does not read a log.
    """
    out = []
    for entry in view.models:
        if entry.model is None:
            continue
        missing = sum(p.missing for p in draw_model(entry.model))
        if missing:
            out.append((entry.name, missing))
    return out


def logged_missing(log: str) -> list[tuple[str, int]]:
    """Return (game name, ``render.node_ref_missing`` lines) per loaded model.

    A line counts for the model of the ``models.file_loaded`` line before
    it; game names are lowercased. Models with none are left out. Does not
    read the lines' fields.
    """
    counts: dict[str, int] = {}
    current = ""
    for line in log.splitlines():
        match = re.search(r'models\.file_loaded file="([^"]*)"', line)
        if match:
            current = match.group(1).lower()
        elif "render.node_ref_missing" in line:
            counts[current] = counts.get(current, 0) + 1
    return list(counts.items())


def log_problems(kind: str, view: ModelsView, log: str) -> list[str]:
    """Return the differences between a sheet's log lines and the view.

    ``file``: the palette lines, in order; ``draw``: the missing references
    per model; ``objects``: none. Returns ``[]`` when they agree, else one
    message naming the first difference. Does not check other log lines.
    """
    if kind == "file":
        theirs = re.findall(r"models\.(opaque_palette|alpha_palette cleared=\d+)", log)
        mine = palette_lines(view)
    elif kind == "draw":
        theirs, mine = logged_missing(log), missing_counts(view)
    else:
        return []
    if theirs == mine:
        return []
    for i, (a, b) in enumerate(zip(mine, theirs, strict=False)):
        if a != b:
            return [f"log differs at {i}: view {a} log {b}"]
    return [f"log: view has {len(mine)}, log has {len(theirs)}"]


def render(kind: str, models: ModelsView, objects) -> str:
    """Return one kind's rendering of a view; does not diff it."""
    if kind == "file":
        return render_file(models)
    if kind == "draw":
        return render_draw(models)
    return render_objects(objects)


def check(
    answers: Path, view_name: str, kind: str, mine: str, extra: list[str]
) -> bool:
    """Diff one sheet against ``mine``; print what differs; return True when same.

    ``extra`` holds problems found before the diff (status, logs). A
    ``draw`` sheet is diffed without ``meshes=`` and ``walk=``, and each
    of its pass lines must say ``walk=same``. A sheet that cannot be read
    prints an ERROR line and returns False. Does not read the log itself.
    """
    path = answers / view_name / f"{kind}.txt"
    try:
        expected = path.read_text(encoding="latin-1")
    except OSError as exc:
        print(f"ERROR {view_name}/{kind}: {exc}")
        return False
    problems = list(extra)
    theirs, mine_lines = expected.splitlines(), mine.splitlines()
    if kind == "draw":
        theirs, their_meshes, walks = mask_passes(expected)
        mine_lines, my_meshes, _ = mask_passes(mine)
        problems += [f"pass {i}: walk={w}" for i, w in enumerate(walks) if w != "same"]
        same = sum(a == b for a, b in zip(their_meshes, my_meshes, strict=False))
        print(f"  {view_name}/draw: meshes= agrees in {same} of {len(their_meshes)}")
    diff = list(difflib.unified_diff(theirs, mine_lines, lineterm="", n=0))
    if not diff and not problems:
        logger.info("same: %s/%s", view_name, kind)
        return True
    print(f"DIFF {view_name}/{kind} ({len(diff)} diff lines, {len(problems)} problems)")
    for problem in problems:
        print(f"  {problem}")
    print("\n".join(line[:240] for line in diff[:40]))
    return False


def compare(answers: Path, install: Path) -> int:
    """Check the six sheets; print each difference; return how many match.

    Does not check ``sheets.md``.
    """
    status = statuses(answers)
    same = 0
    for view_name in VIEWS:
        bop = view_name == "bop"
        models = models_view(install, balance_of_power=bop)
        objects = objects_view(install, balance_of_power=bop)
        loaded = sum(m.model is not None for m in models.models)
        print(f"view {view_name}: {loaded} of {len(models.models)} models loaded")
        for kind in KINDS:
            extra = []
            if status.get((view_name, kind)) != 0:
                extra.append(f"exit status {status.get((view_name, kind))}")
            log_path = answers / view_name / f"{kind}.log"
            log = log_path.read_text(encoding="latin-1") if log_path.exists() else ""
            extra += log_problems(kind, models, log)
            same += check(
                answers, view_name, kind, render(kind, models, objects), extra
            )
    return same


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when all six sheets match, else 1.

    Does not write any file.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--answers", required=True, help="the answer sheets' folder")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.ERROR)
    same = compare(Path(args.answers), Path(args.install))
    total = len(VIEWS) * len(KINDS)
    print(f"{same} of {total} sheets match")
    return 0 if same == total else 1


if __name__ == "__main__":
    sys.exit(main())
