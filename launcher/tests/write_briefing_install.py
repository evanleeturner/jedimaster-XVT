"""Add a made-up briefing to an install folder, for the browser tests.

Purpose:
    The browser tests start the launcher on an install of made-up menus;
    this script adds the art and the one mission that install needs for a
    briefing: icon sheets, a font, a front text, sounds, and the mission
    ``Train/ALPHA.TIE`` with two teams, a caption, a label and a marker.

Flow:
    ``python tests/write_briefing_install.py INSTALL`` writes the files into
    the existing folder and prints nothing.

Invariants:
    - No game data: everything is made up.
    - The menus are the caller's; this script does not touch them.

Call:
    ``python tests/write_briefing_install.py /tmp/XvT``
"""

from __future__ import annotations

import logging
import sys
from pathlib import Path

from briefingdata import add_files
from briefingdata import briefing_points
from briefingdata import flagged
from briefingdata import mission_bytes
from briefingdata import point
from iconinstall import make_install as make_icon_install

logger = logging.getLogger(__name__)


def main(argv: list[str]) -> int:
    """Write the made-up briefing files into the folder ``argv[0]``; return 0."""
    install = Path(argv[0])
    make_icon_install(install)
    add_files(install)
    groups = [
        {
            "name": "Alpha Wing",
            "craft_type": 2,
            "iff": 0,
            "team": 0,
            "player_number": 1,
            "waypoints": briefing_points(point(40, 30)),
        },
        {
            "name": "Bravo Wing",
            "craft_type": 3,
            "iff": 1,
            "team": 1,
            "player_number": 1,
            "waypoints": briefing_points(point(-60, -20)),
        },
    ]
    brief = {
        "viewed_by_team": flagged(0, 1),
        "running_time": 200,
        "events": [
            (0, 6, 0, 0),
            (0, 7, 64, 64),
            (0, 5, 1),
            (3, 9, 0),
            (6, 18, 0, 20, 20, 1),
            (9999, 34),
        ],
        "tags": ["Rally point"],
        "strings": ["", ">Intro$Brief words [here] to read."],
    }
    data = mission_bytes(groups, [brief], ["Red", "Blue"])
    (install / "Train" / "ALPHA.TIE").write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
