"""Add made-up menu frame backgrounds to an install folder, for the browser tests.

Purpose:
    The browser tests that wear the menu frame need the twelve background
    bitmaps (``frontres/GAME*.BMP``) next to the pad and the activity
    frame; this script writes twelve made-up ones, each a 640 by 480
    picture of its own color with index 0 at the top left corner. It also
    adds a Combat menu (five missions, one locked in the network menu, and a Rebel and an
    Imperial solo list; the Rebel list also holds the locked mission, offered, and
    one mission the network menu lacks) whose first mission, ``SKIRM``, is the same mission as
    ``Train/ALPHA.TIE``, both rewritten with 16-character team names (so the
    team picker's label is 24 characters long): combat backgrounds differ by
    team (imperial for team 0, rebel for team 1), which a training mission's
    do not. It also writes the 48 made-up side button sprites with the two
    image lists that name them: the base install's ``frontres/icons.lst``
    holds every sprite but the Campaign button's, the eighth light and the
    sixth button's lit pieces, which only Balance of Power's
    ``frontres/side.lst`` holds (as in the game).

Flow:
    ``python tests/write_frame_install.py INSTALL`` writes the files into
    the existing folder (after ``write_briefing_install.py``) and prints
    nothing.

Invariants:
    - No game data: everything is made up.
    - Only the backgrounds, the Combat folder and the side sprites with
      their lists are written; the caller's other files stay.
    - A sprite is 44 by 360 with index 0 everywhere but a block of index 1
      (its own color) at the place the 1997 screen draws that button or
      light.

Call:
    ``python tests/write_frame_install.py /tmp/XvT``
"""

from __future__ import annotations

import logging
import sys
from pathlib import Path

from bmpdata import bmp
from bmpdata import plain
from bmpdata import ramp
from listdata import crlf
from textdata import write_files
from write_briefing_install import mission_file

from jedimaster.briefing.frame import BACKGROUNDS
from jedimaster.briefing.side import SIDE_SPRITE_NAMES

logger = logging.getLogger(__name__)


def background_bytes(index: int) -> bytes:
    """Return a 640 by 480 bitmap of palette ``index`` with index 0 at (0, 0)."""
    rows = [bytearray((index,)) * 640 for _ in range(480)]
    rows[0][0] = 0
    return bmp(640, 480, plain([bytes(row) for row in rows]), compression=0)


SLOT_TOPS = {1: 114, 2: 142, 3: 170, 4: 198, 5: 226, 8: 254, 6: 306, 7: 334}
"""The top row of each side button on the 640 by 480 screen."""
ONLY_IN_BALANCE_OF_POWER = (
    "game8u",
    "game8d",
    "active8",
    "6lita0",
    "6lita1",
    "8lita0",
    "8lita1",
)
"""The sprites only Balance of Power's ``side.lst`` names."""


def sprite_color(name: str) -> tuple[int, int, int]:
    """Return the color of index 1 in sprite ``name``, different for each name."""
    at = SIDE_SPRITE_NAMES.index(name)
    return (20 + 4 * at, 250 - 4 * at, 100 + at)


def sprite_bytes(name: str) -> bytes:
    """Return the made-up 44 by 360 bitmap for side sprite ``name``.

    A button sprite (``game``/``reg``) has a 20 by 24 block at x 22 and its
    button's top; an ``active``/lit sprite has a 4 by 4 block at x 2 and
    that slot's top. Everything else is index 0.
    """
    digits = [c for c in name if c.isdigit()]
    slot = int(digits[0])
    rows = [bytearray(44) for _ in range(360)]
    button = name.startswith(("game", "reg"))
    left, width, height = (22, 20, 24) if button else (2, 4, 4)
    for y in range(SLOT_TOPS[slot], SLOT_TOPS[slot] + height):
        rows[y][left : left + width] = bytes((1,)) * width
    palette = [ramp(i) for i in range(256)]
    palette[1] = sprite_color(name)
    return bmp(44, 360, plain([bytes(r) for r in rows]), compression=0, palette=palette)


def side_files() -> dict[str, bytes]:
    """Return the sprite files and the two lists, by install-relative path."""
    files: dict[str, bytes] = {}
    base_lines = ["// made-up image list, first line is skipped"]
    bop_lines = ["// made-up image list, first line is skipped"]
    for name in SIDE_SPRITE_NAMES:
        if name in ONLY_IN_BALANCE_OF_POWER:
            files[f"BalanceOfPower/frontres/{name}.BMP"] = sprite_bytes(name)
            bop_lines.append(f"frontres\\{name}.bmp {name} 1  ")
        else:
            files[f"frontres/{name}.BMP"] = sprite_bytes(name)
            base_lines.append(f"frontres\\{name}.bmp {name} 1  ")
    base_lines.insert(2, "// a comment line in the middle")
    files["frontres/icons.lst"] = crlf(*base_lines)
    files["BalanceOfPower/frontres/side.lst"] = crlf(*bop_lines)
    return files


def main(argv: list[str]) -> int:
    """Write the made-up frame backgrounds into the folder ``argv[0]``; return 0."""
    install = Path(argv[0])
    files = {
        f"frontres/{name}.BMP": background_bytes(40 + n)
        for n, name in enumerate(BACKGROUNDS)
    }
    files["Combat/MISSION.LST"] = crlf(
        "[Basic]", "1", "SKIRM.TIE", "Skirmish",
        "2", "& GHOST.TIE", "Ghost Fight",
        "3", "RAID.TIE", "Raid",
        "4", "SWEEP.TIE", "Sweep",
        "5", "PATROL.TIE", "Patrol",
    )  # fmt: skip
    files["Combat/REBEL.LST"] = crlf(
        "[Basic]", "1", "SKIRM.TIE", "Skirmish",
        "2", "GHOST.TIE", "Ghost Fight",
        "3", "RAID.TIE", "Raid",
        "5", "PATROL.TIE", "Patrol",
        "6", "RIDGE.TIE", "Ridge Run",
    )  # fmt: skip
    files["Combat/IMPERIAL.LST"] = crlf(
        "[Basic]", "4", "SWEEP.TIE", "Sweep", "3", "RAID.TIE", "Raid"
    )  # fmt: skip
    long_teams = mission_file(["Imaginary Wing A", "Imaginary Wing B"])
    files["Train/ALPHA.TIE"] = long_teams
    for name in ("SKIRM", "RAID", "SWEEP", "PATROL"):
        files[f"Combat/{name}.TIE"] = long_teams
    # Missions only a solo list or a locked menu entry reaches carry their own
    # team names, so a briefing that loaded is told from the one before it.
    for folder, name, wing in (
        ("Combat", "GHOST", "Ghost"),
        ("Combat", "RIDGE", "Ridge"),
        ("Train", "CHARLIE", "Charlie"),
    ):
        files[f"{folder}/{name}.TIE"] = mission_file(
            [f"{wing} Wing A", f"{wing} Wing B"]
        )
    files.update(side_files())
    write_files(install, files)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
