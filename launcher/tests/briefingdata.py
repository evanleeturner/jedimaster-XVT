"""Build what the briefing tests need: a made-up install and made-up missions.

Purpose:
    One synthetic install shared by the briefing tests: two training
    missions in a menu, a font, a front text, the icon sheets, a sound list
    with four tiny WAV files; and a mission builder that sets the fields a
    bundle reads.

Flow:
    ``make_install`` writes the files and returns the install folder;
    ``mission_bytes`` builds a mission file with the groups and briefings a
    test names.

Invariants:
    - No game data: every name, text, picture and sound is made up.
    - The page word is a made-up line 640 of the front text.

Call:
    ``install = make_install(tmp_path / "XvT")``
"""

from __future__ import annotations

import logging
import struct
from pathlib import Path
from typing import Any

from builder import build_mission
from iconinstall import make_install as make_icon_install
from listdata import crlf
from textdata import font_bytes
from textdata import write_files

logger = logging.getLogger(__name__)

PAGE_WORD = "Pg."
SOUNDS = {
    "jewelsound": "jewel",
    "sfxTarget1": "t1",
    "sfxTarget2": "t2",
    "sfxText": "tx",
}
FONT_SHAPE = {65: (3, [b"\x41\x02\x00", b"\x81\x00\x40"]), 66: (1, [b"\x02\x00", b""])}


def wav_bytes(tag: int) -> bytes:
    """Return a tiny WAV file (8-bit mono, 4 samples) whose samples are ``tag``."""
    data = bytes([tag]) * 4
    head = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, 8000, 8000, 1, 8)
    body = b"WAVE" + head + b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", len(body)) + body


def point(x: int, y: int, enabled: int = 1) -> tuple[int, int, int, int]:
    """Return one waypoint tuple ``(x, y, z, enabled)``."""
    return (x, y, 0, enabled)


def briefing_points(
    *points: tuple[int, int, int, int],
) -> list[tuple[int, int, int, int]]:
    """Return 22 waypoints: 14 unused, then the given briefing points, then unused."""
    unused = point(0, 0, 0)
    return [unused] * 14 + list(points) + [unused] * (8 - len(points))


def mission_bytes(
    groups: list[dict[str, Any]] | None = None,
    briefings: list[dict[str, Any]] | None = None,
    team_names: list[str] | None = None,
) -> bytes:
    """Return a mission file with the given groups, briefings and team names."""
    teams = [{"name": name} for name in (team_names or [])]
    return build_mission(
        flight_groups=groups or [], briefings=briefings or [], teams=teams
    )


def flagged(*teams: int) -> list[int]:
    """Return ten team flags with the given teams set."""
    return [1 if team in teams else 0 for team in range(10)]


def add_files(install: Path) -> None:
    """Write the made-up art and texts a briefing needs into ``install``.

    The font, the front text, the sound list and four WAV files. The icon
    sheets come from ``make_install``; the menu and the missions are the
    caller's.
    """
    front = [f"line {n}" for n in range(641)]
    front[640] = PAGE_WORD
    entries = [f"sfx\\{tag}.wav {name}" for name, tag in SOUNDS.items()]
    files: dict[str, bytes] = {
        "TIMES10.ABP": font_bytes(FONT_SHAPE, height=2, spacing=0),
        "FRONTTXT.TXT": crlf(*front),
        "SFX/SFX.LST": crlf("// made up", *entries),
    }
    wavs = {f"SFX/{tag}.wav": wav_bytes(i + 1) for i, tag in enumerate(SOUNDS.values())}
    write_files(install, {**files, **wavs})


def make_install(root: Path) -> Path:
    """Write a made-up install under ``root`` and return its folder.

    The training menu lists ALPHA (id 1, in the base folder and again in
    Balance of Power's folder with another group name), BRAVO (id 2, locked
    with ``&``) and GHOST (id 3, whose file is missing).
    """
    install = make_icon_install(root)
    add_files(install)
    menu = crlf(
        "[Basic]", "1", "ALPHA.TIE", "First Flight",
        "2", "& BRAVO.TIE", "Second Flight",
        "3", "GHOST.TIE", "Third Flight",
    )  # fmt: skip
    files: dict[str, bytes] = {
        "Train/MISSION.LST": menu,
        "Train/ALPHA.TIE": mission_bytes([{"name": "Base"}]),
        "BalanceOfPower/TRAIN/MISSION.LST": menu,
        "BalanceOfPower/TRAIN/ALPHA.TIE": mission_bytes([{"name": "Bop"}]),
    }
    return write_files(install, files)
