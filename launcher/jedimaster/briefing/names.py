"""The fixed names the briefing player uses: art files, sounds, sheets, text.

Purpose:
    Hold in one place the short list of names the bundle, the art route and
    the player share, so no module spells one twice.

Flow:
    ``art.py`` and ``build.py`` read these constants; the page's art route
    accepts only ``ART_NAMES``.

Invariants:
    - ``ART_NAMES`` is the whole list of files the art route serves.
    - A sound's art name is its list name plus ``.wav``.

Call:
    ``from jedimaster.briefing.names import ART_NAMES``
"""

from __future__ import annotations

import logging

logger = logging.getLogger(__name__)

ICON_SHEETS = ("mapicon0", "mapicon1", "mapicon2", "mapicon3", "mapicon4")
GREY_SHEET = "greyicon"
FONT_KIND = "font10"
FONT_GAME_NAME = "times10.abp"
SOUND_NAMES = ("jewelsound", "sfxTarget1", "sfxTarget2", "sfxText")
SOUND_LIST = "sfx\\sfx.lst"
FRONT_FILE = "fronttxt.txt"
PAGE_STRING = 640
NO_TEXT = "No text."
PNG_TYPE = "image/png"
WAV_TYPE = "audio/wav"
WAV_SUFFIX = ".wav"

END_EVENT = 34
EVENT_VARIABLES: dict[int, int] = {
    **dict.fromkeys(range(0, END_EVENT + 1), 0),
    **dict.fromkeys((2, 4, 5, *range(9, 17)), 1),
    **dict.fromkeys((6, 7), 2),
    **dict.fromkeys(range(18, 26), 4),
}
"""How many variables each event type 0 to 34 takes; no other type is read."""
