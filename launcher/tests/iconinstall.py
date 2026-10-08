"""Build a fake install holding an icon list and its sheets, for the icon tests.

Purpose:
    One synthetic install shared by the view, output and command-line
    tests: six 320 by 200 sheets that draw index 1 in the base install and
    index 2 in ``BalanceOfPower/``, one of them missing from that folder,
    and a compressed background image read for its header only.

Flow:
    ``make_install(root)`` writes the files with ``bmpdata`` and returns the
    root; ``CRAFT`` is the craft type whose box the sheets fill.

Invariants:
    - No game data: the sheets' pixels and palettes are made up; the box is
      read from the package's tables, never copied.

Call:
    ``root = make_install(tmp_path / "XvT")``
"""

from __future__ import annotations

import logging
from pathlib import Path

from bmpdata import bmp
from bmpdata import crlf_list
from bmpdata import icon_install
from bmpdata import sheet

from jedimaster.icons import Box
from jedimaster.icons import BOXES
from jedimaster.icons import CRAFT_BOXES

logger = logging.getLogger(__name__)

SHEET_W, SHEET_H = 320, 200
NAMES = ("mapicon0", "mapicon1", "mapicon2", "mapicon3", "mapicon4", "greyicon")
CRAFT = 2
"""A craft type the tests draw; its box is read from the package's tables."""


def icon_list() -> bytes:
    """Return an image list registering the six sheets and a background."""
    lines = ["// synthetic"]
    lines += [f"frontres\\s{i}.bmp {name} 0" for i, name in enumerate(NAMES)]
    lines.append("frontres\\back.bmp stars 1")
    return crlf_list(*lines)


def box_pixels(box: Box, value: int) -> dict[tuple[int, int], int]:
    """Return every pixel of ``box`` set to ``value``, one left at index 0."""
    pixels = {
        (x, y): value
        for y in range(box.top, box.bottom + 1)
        for x in range(box.left, box.right + 1)
    }
    pixels[(box.left + 1, box.top)] = 0
    return pixels


def make_install(root: Path, bop: tuple[int, ...] = (0, 1, 2, 4, 5)) -> Path:
    """Write a fake install: base sheets draw index 1, BoP's index 2.

    ``bop`` names the sheets (by position in ``NAMES``) Balance of Power
    has; the default leaves out ``mapicon3``.
    """
    box = BOXES[CRAFT_BOXES[CRAFT]]
    files = {
        "frontres/MAPICONS.LST": icon_list(),
        "frontres/BACK.BMP": bmp(8, 4, b""),
    }
    for i in range(len(NAMES)):
        files[f"frontres/S{i}.BMP"] = sheet(SHEET_W, SHEET_H, box_pixels(box, 1))
    for i in bop:
        files[f"BalanceOfPower/FRONTRES/s{i}.bmp"] = sheet(
            SHEET_W, SHEET_H, box_pixels(box, 2)
        )
    return icon_install(root, files)
