"""Add solo lists, pilots and a stand-in engine to an install, for the browser tests.

Purpose:
    The browser tests start the launcher with a game program; this script
    writes what a battle needs beside the made-up install: solo lists, a
    folder of pilot files and an engine script whose behaviour is the word
    in the file ``behave`` (``hang`` until the file says ``done``).

Flow:
    ``python tests/write_battle_install.py INSTALL ROOT`` writes into the
    existing install, and ``ROOT/pilots``, ``ROOT/engine.py``,
    ``ROOT/behave``; it prints nothing.

Invariants:
    - No game data: everything is made up.
    - The install's network menus are not touched.

Call:
    ``python tests/write_battle_install.py /tmp/XvT /tmp/root``
"""

from __future__ import annotations

import logging
import sys
from pathlib import Path

from solodata import add_solo_lists
from solodata import make_engine
from solodata import make_pilots

logger = logging.getLogger(__name__)


def main(argv: list[str]) -> int:
    """Write the battle files for the install ``argv[0]`` under ``argv[1]``; return 0."""
    install, root = Path(argv[0]), Path(argv[1])
    add_solo_lists(install)
    (install / "Tourn" / "MISSION.LST").unlink()
    (install / "Campaign" / "REBEL.LST").unlink()
    make_pilots(root)
    (root / "behave").write_text("hang", encoding="utf-8")
    make_engine(root, root / "behave")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
