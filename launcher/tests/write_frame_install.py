"""Add made-up menu frame backgrounds to an install folder, for the browser tests.

Purpose:
    The browser tests that wear the menu frame need the twelve background
    bitmaps (``frontres/GAME*.BMP``) next to the pad and the activity
    frame; this script writes twelve made-up ones, each a 640 by 480
    picture of its own color with index 0 at the top left corner. It also
    adds a Combat menu whose one mission, ``SKIRM``, is the same mission as
    ``Train/ALPHA.TIE``, both rewritten with 16-character team names (so the
    team picker's label is 24 characters long): combat backgrounds differ by team (imperial for
    team 0, rebel for team 1), which a training mission's do not.

Flow:
    ``python tests/write_frame_install.py INSTALL`` writes the files into
    the existing folder (after ``write_briefing_install.py``) and prints
    nothing.

Invariants:
    - No game data: everything is made up.
    - Only the backgrounds and the Combat folder are written; the caller's
      other files stay.

Call:
    ``python tests/write_frame_install.py /tmp/XvT``
"""

from __future__ import annotations

import logging
import sys
from pathlib import Path

from bmpdata import bmp
from bmpdata import plain
from listdata import crlf
from textdata import write_files
from write_briefing_install import mission_file

from jedimaster.briefing.frame import BACKGROUNDS

logger = logging.getLogger(__name__)


def background_bytes(index: int) -> bytes:
    """Return a 640 by 480 bitmap of palette ``index`` with index 0 at (0, 0)."""
    rows = [bytearray((index,)) * 640 for _ in range(480)]
    rows[0][0] = 0
    return bmp(640, 480, plain([bytes(row) for row in rows]), compression=0)


def main(argv: list[str]) -> int:
    """Write the made-up frame backgrounds into the folder ``argv[0]``; return 0."""
    install = Path(argv[0])
    files = {
        f"frontres/{name}.BMP": background_bytes(40 + n)
        for n, name in enumerate(BACKGROUNDS)
    }
    files["Combat/MISSION.LST"] = crlf("[Basic]", "1", "SKIRM.TIE", "Skirmish")
    long_teams = mission_file(["Imaginary Wing A", "Imaginary Wing B"])
    files["Train/ALPHA.TIE"] = long_teams
    files["Combat/SKIRM.TIE"] = long_teams
    write_files(install, files)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
