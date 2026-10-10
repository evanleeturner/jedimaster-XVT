"""Paint the briefing panel in a real browser and diff it against the game's screens.

Purpose:
    The acceptance fence of the briefing map's pixels: for each contact-sheet
    list and each moment of a mission, the game's own screen cropped to the
    panel (360 by 240) is compared, pixel for pixel in 16-bit 5-6-5, with
    what the page's painter draws in Chromium.

Flow:
    1. Read the moments (mission, directory, team, frames) and the lists.
    2. Build one bundle per mission (and one for the lists) from the install,
       write the jobs for ``page/scripts/pixels.ts`` and run it on the built
       page (``npm run build``); it paints in Chromium and writes the packed
       pixels.
    3. Crop each engine screen to the panel and compare; print one line per
       item and one summary line.

Invariants:
    - The engine's folder, the lists, the moments and the install are
      required; nothing from the game is written into this tool.
    - An item that cannot be read or painted counts as different.
    - ``--diffs`` writes one picture per differing item and refuses a folder
      inside this repository: game pictures never go into the tree.
    - Frames from 21 on and every list must match for exit status 0; frame
      2 (the text still fading in on the game's screen) is reported only.

Call:
    ``python tools/compare_pixels.py --engine DIR --lists DIR --moments FILE --install DIR``
"""

from __future__ import annotations

import argparse
import base64
import json
import logging
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve()
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT))

from jedimaster.briefing import build_bundle  # noqa: E402
from jedimaster.icons.png import rgba_png  # noqa: E402
from jedimaster.mission import MissionFormatError  # noqa: E402
from jedimaster.mission import read_mission  # noqa: E402

logger = logging.getLogger("compare_pixels")

PAGE = ROOT / "page"
SCRIPT = "scripts/pixels.ts"
SCREEN_WIDTH = 640
SCREEN_HEIGHT = 480
PANEL_LEFT = 84
PANEL_TOP = 96
PANEL_WIDTH = 360
PANEL_HEIGHT = 240
FADE_FRAMES = 20
REPORTED_FRAME = 2
SHOWN_POSITIONS = 4
DIRECTORY_TYPES = {
    "train": "training",
    "melee": "melee",
    "tourn": "tournament",
    "combat": "combat",
    "battle": "battle",
}


@dataclass(frozen=True)
class Moment:
    """One mission of the moments file: its path, directory, team and frames."""

    mission: str
    directory: str
    team: int
    frames: tuple[int, ...]


def read_moments(path: Path) -> list[Moment]:
    """Return the moments in ``path``: ``mission directory team frames...`` a line.

    Blank lines and lines starting with ``#`` are skipped. Raises
    ``ValueError`` for a line with fewer than four words, an unknown
    directory or a number that is not whole.
    """
    moments = []
    for line in path.read_text().splitlines():
        words = line.split()
        if not words or words[0].startswith("#"):
            continue
        if len(words) < 4 or words[1] not in DIRECTORY_TYPES:
            raise ValueError(f"not a moments line: {line!r}")
        moments.append(
            Moment(words[0], words[1], int(words[2]), tuple(int(w) for w in words[3:]))
        )
    return moments


def moment_key(moment: Moment) -> str:
    """Return the engine folder name of a moment: the path with ``_``, then ``.tN``."""
    return f"{moment.mission.replace('/', '_')}.t{moment.team}"


def crop_panel(raw: bytes) -> bytes:
    """Return the panel's 360 by 240 pixels (two bytes each) of a 640 by 480 screen.

    Raises ``ValueError`` when ``raw`` is not one whole screen.
    """
    if len(raw) != SCREEN_WIDTH * SCREEN_HEIGHT * 2:
        raise ValueError(f"{len(raw)} bytes are not a 640x480 screen")
    rows = []
    for row in range(PANEL_HEIGHT):
        start = ((PANEL_TOP + row) * SCREEN_WIDTH + PANEL_LEFT) * 2
        rows.append(raw[start : start + PANEL_WIDTH * 2])
    return b"".join(rows)


def pixel_at(panel: bytes, at: int) -> int:
    """Return pixel number ``at`` (row-major) of a packed panel."""
    return panel[at * 2] | panel[at * 2 + 1] << 8


def differences(engine: bytes, page: bytes) -> list[int]:
    """Return the pixel numbers where two packed panels differ, lowest first.

    Raises ``ValueError`` when the panels differ in size.
    """
    if len(engine) != len(page):
        raise ValueError(f"panels of {len(engine)} and {len(page)} bytes")
    return [
        at
        for at in range(len(engine) // 2)
        if pixel_at(engine, at) != pixel_at(page, at)
    ]


def describe(engine: bytes, page: bytes, where: list[int]) -> str:
    """Return the first few differing positions with both values, as text."""
    shown = []
    for at in where[:SHOWN_POSITIONS]:
        x, y = at % PANEL_WIDTH, at // PANEL_WIDTH
        shown.append(
            f"({x},{y}) engine {pixel_at(engine, at):04x} page {pixel_at(page, at):04x}"
        )
    return "; ".join(shown)


def widen(value: int) -> tuple[int, int, int]:
    """Return a 5-6-5 pixel as 8-bit channels (top bits repeated)."""
    red, green, blue = value >> 11, (value >> 5) & 63, value & 31
    return red << 3 | red >> 2, green << 2 | green >> 4, blue << 3 | blue >> 2


def difference_picture(engine: bytes, where: list[int]) -> bytes:
    """Return a PNG of the engine's panel, dimmed, with the differing pixels red."""
    marked = set(where)
    rgba = bytearray()
    for at in range(PANEL_WIDTH * PANEL_HEIGHT):
        if at in marked:
            rgba += bytes((255, 0, 0, 255))
        else:
            red, green, blue = widen(pixel_at(engine, at))
            rgba += bytes((red // 3, green // 3, blue // 3, 255))
    return rgba_png(PANEL_WIDTH, PANEL_HEIGHT, bytes(rgba))


def outside_repository(folder: Path) -> bool:
    """Return True when ``folder`` lies outside this repository's tree."""
    return not folder.resolve().is_relative_to(ROOT.parent)


def run_page(request: dict, work: Path) -> dict[str, bytes]:
    """Run the page's painter on ``request``; return each job's packed pixels.

    Raises ``RuntimeError`` when Node fails or a job's answer is missing.
    """
    request_path, out_path = work / "request.json", work / "painted.json"
    request_path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
    proc = subprocess.run(
        ["node", SCRIPT, str(request_path), str(out_path)],
        cwd=PAGE,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError((proc.stderr or proc.stdout).strip()[-600:])
    painted = json.loads(out_path.read_text())
    return {name: base64.b64decode(text) for name, text in painted.items()}


def build_jobs(
    moments: list[Moment], lists: list[Path], install: Path, only: str | None
) -> tuple[list[dict], list[dict], dict[str, tuple[Path, bool]]]:
    """Return (bundles, jobs, expected) for the items whose name holds ``only``.

    ``expected`` maps a job's name to its engine screen's path and whether it
    must match (False for the fading frame). Raises ``OSError``,
    ``ValueError`` or ``MissionFormatError`` when a mission cannot be built.
    """
    bundles: list[dict] = []
    jobs: list[dict] = []
    expected: dict[str, tuple[Path, bool]] = {}
    index: dict[str, int] = {}
    for moment in moments:
        directory = DIRECTORY_TYPES[moment.directory]
        index[moment.mission] = len(bundles)
        mission = read_mission(install / moment.mission)
        bundles.append(build_bundle(install, mission, directory))
    if not bundles:
        raise ValueError("the moments file names no mission")
    for path in lists:
        name = path.stem
        if only is None or only in name:
            text = path.read_text(encoding="latin-1")
            jobs.append({"name": name, "kind": "list", "bundle": 0, "text": text})
            expected[name] = (Path(f"pieces/{name}.raw"), True)
    for moment in moments:
        for frame in moment.frames:
            name = f"{moment_key(moment)}/frame-{frame}"
            if only is not None and only not in name:
                continue
            jobs.append(
                {
                    "name": name,
                    "kind": "frame",
                    "bundle": index[moment.mission],
                    "team": moment.team,
                    "frame": frame,
                }
            )
            expected[name] = (Path(f"frames/{name}.raw"), frame > FADE_FRAMES)
    return bundles, jobs, expected


def main(argv: list[str] | None = None) -> int:
    """Run the comparison; return 0 when every list and every frame from 21 on matches."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--engine", required=True, help="the engine's screens folder")
    parser.add_argument("--lists", required=True, help="the contact-sheet lists folder")
    parser.add_argument("--moments", required=True, help="the moments file")
    parser.add_argument("--install", required=True, help="the install folder")
    parser.add_argument("--only", help="only items whose name contains this text")
    parser.add_argument(
        "--diffs", help="write a difference picture per differing item here"
    )
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.ERROR)
    engine, install = Path(args.engine), Path(args.install)
    diffs = Path(args.diffs) if args.diffs else None
    if diffs is not None and not outside_repository(diffs):
        print(f"--diffs must be a folder outside {ROOT.parent}", file=sys.stderr)
        return 2
    try:
        moments = read_moments(Path(args.moments))
        lists = sorted(Path(args.lists).glob("*.txt"))
        bundles, jobs, expected = build_jobs(moments, lists, install, args.only)
        with tempfile.TemporaryDirectory() as folder:
            request = {
                "python": sys.executable,
                "install": str(install),
                "bundles": bundles,
                "jobs": jobs,
            }
            painted = run_page(request, Path(folder)) if jobs else {}
    except (OSError, ValueError, RuntimeError, MissionFormatError) as exc:
        print(f"ERROR {exc}")
        return 1
    if diffs is not None:
        diffs.mkdir(parents=True, exist_ok=True)
    counts = {"list": [0, 0], "frame": [0, 0], "fade": [0, 0]}
    fade_pixels: list[int] = []
    for job in jobs:
        name = job["name"]
        path, required = expected[name]
        kind = "list" if job["kind"] == "list" else ("frame" if required else "fade")
        counts[kind][1] += 1
        try:
            screen = crop_panel((engine / path).read_bytes())
            page = painted[name]
            where = differences(screen, page)
        except (OSError, ValueError, KeyError) as exc:
            print(f"DIFF {name} ERROR {exc}")
            continue
        if not where:
            counts[kind][0] += 1
            print(f"SAME {name}")
            continue
        if kind == "fade":
            fade_pixels.append(len(where))
        print(f"DIFF {name} {len(where)} {describe(screen, page, where)}")
        if diffs is not None:
            target = diffs / (name.replace("/", "_") + ".png")
            target.write_bytes(difference_picture(screen, where))
    lists_same, lists_all = counts["list"]
    frames_same, frames_all = counts["frame"]
    fade_same, fade_all = counts["fade"]
    print(
        f"lists {lists_same} of {lists_all} identical; "
        f"frames from {FADE_FRAMES + 1} on {frames_same} of {frames_all} identical; "
        f"frame {REPORTED_FRAME} {fade_same} of {fade_all} identical"
        + (f" (differing pixels {fade_pixels})" if fade_pixels else "")
    )
    ok = lists_same == lists_all and frames_same == frames_all
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
