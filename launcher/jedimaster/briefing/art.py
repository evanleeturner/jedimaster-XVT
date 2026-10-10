"""The briefing's pictures and sounds, built from an install and kept.

Purpose:
    Give the page's art route the bytes of a fixed set of files: the five
    map icon sheets and the grey sheet as PNG pictures, font 10's glyph
    atlas as a PNG picture, and the four sounds the setup screen uses as
    WAV files. Nothing else is ever served.

Flow:
    ``ArtStore.get(name)`` builds the file on first use (icon sheets from
    ``jedimaster.icons``, the atlas from ``jedimaster.fonts``, a sound from
    the game's sound list and the WAV it names) and keeps the bytes.
    ``sound_files`` resolves the sound list; ``build`` also uses it.

Invariants:
    - ``ART_NAMES`` is the whole set; any other name, or a store with no
      install, answers None.
    - A file that cannot be built is None, with a WARNING, and is tried
      again on the next request.
    - Sheets are the same pictures ``icons export`` writes, the atlas the
      same as ``fonts export``: tinting and drawing happen on the page.

Call:
    ``art = ArtStore(install).get("mapicon0.png")``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from pathlib import Path

from ..fonts import atlas_png
from ..fonts import Font
from ..fonts import font_atlas
from ..fonts import FontFormatError
from ..fonts import read_font
from ..fonts.to_json import atlas_name
from ..icons import icons_view
from ..icons import picture_name
from ..icons import png_bytes
from ..icons.bmp import BmpFormatError
from ..lists import ListFormatError
from ..lists import read_sounds
from ..lists import sounds_view
from ..lists.files import resolve
from .names import FONT_GAME_NAME
from .names import GREY_SHEET
from .names import ICON_SHEETS
from .names import PNG_TYPE
from .names import SOUND_LIST
from .names import SOUND_NAMES
from .names import WAV_SUFFIX
from .names import WAV_TYPE

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class Art:
    """One file of the art set: its bytes and its media type."""

    data: bytes
    content_type: str


def sound_art_name(sound: str) -> str:
    """Return the art name of a sound: its list name plus ``.wav``.

    Does not check that the sound is one the player uses.
    """
    return f"{sound}{WAV_SUFFIX}"


SHEET_ART = {name: picture_name(name) for name in (*ICON_SHEETS, GREY_SHEET)}
"""The art name of each icon sheet, by image name."""
FONT_ART = atlas_name(FONT_GAME_NAME)
SOUND_ART = {sound_art_name(name): name for name in SOUND_NAMES}
"""Each sound's art name and its name in the game's list."""
ART_NAMES: tuple[str, ...] = (*SHEET_ART.values(), FONT_ART, *SOUND_ART)
"""Every name the art route serves, eleven in all."""


def read_font10(install: Path) -> Font | None:
    """Return font 10 as the install holds it, or None (with a WARNING).

    Resolves ``times10.abp`` (Balance of Power's first) and reads it. None
    when the file is missing or cannot be read. Does not read the other
    menu fonts.
    """
    path = resolve(install, FONT_GAME_NAME)
    if path is None or not path.is_file():
        logger.warning("%s: %s not found", install, FONT_GAME_NAME)
        return None
    try:
        return read_font(path)
    except (FontFormatError, OSError) as exc:
        logger.warning("cannot read %s: %s", path, exc)
        return None


def sound_files(install: Path) -> dict[str, Path]:
    """Return the WAV file of each of the player's sounds the install holds.

    Reads ``sfx\\sfx.lst`` (Balance of Power's first) and resolves the WAV
    word of each of ``SOUND_NAMES`` the list names; a sound the list does
    not name, or whose file is missing, is left out. Returns ``{}`` when the
    list is missing or unreadable (WARNING). Does not read the WAV files.
    """
    try:
        listing = resolve(install, SOUND_LIST)
        if listing is None or not listing.is_file():
            logger.warning("%s: %s not found", install, SOUND_LIST)
            return {}
        table = {s.name: s for s in sounds_view(read_sounds(listing)).sounds}
    except (ListFormatError, OSError, ValueError) as exc:
        logger.warning("cannot read the sound list: %s", exc)
        return {}
    found: dict[str, Path] = {}
    for name in SOUND_NAMES:
        entry = table.get(name)
        path = resolve(install, entry.wav) if entry is not None else None
        if path is None or not path.is_file():
            logger.warning("sound %s has no file", name)
            continue
        found[name] = path
    logger.debug("sounds found: %s", sorted(found))
    return found


class ArtStore:
    """The art set of one install, each file built once, on first request."""

    def __init__(self, install: Path | None) -> None:
        """Hold the install (or None: nothing is served)."""
        self.install = install
        self._kept: dict[str, Art] = {}

    def get(self, name: str) -> Art | None:
        """Return the file called ``name``, or None.

        None for a name outside ``ART_NAMES``, with no install, or when the
        file cannot be built (WARNING). A built file is kept: the next
        request returns the same object. Does not check the media's content.
        """
        if name not in ART_NAMES or self.install is None:
            return None
        if name in self._kept:
            return self._kept[name]
        art = self._build(self.install, name)
        if art is not None:
            self._kept[name] = art
            logger.info("built %s: %d bytes", name, len(art.data))
        return art

    def _build(self, install: Path, name: str) -> Art | None:
        try:
            if name in SOUND_ART:
                path = sound_files(install).get(SOUND_ART[name])
                return None if path is None else Art(path.read_bytes(), WAV_TYPE)
            if name == FONT_ART:
                font = read_font10(install)
                if font is None:
                    return None
                return Art(atlas_png(font_atlas(font)), PNG_TYPE)
            sheet = {art: sheet for sheet, art in SHEET_ART.items()}[name]
            picture = icons_view(install).sheets.get(sheet)
            if picture is None:
                logger.warning("sheet %s is not in the install", sheet)
                return None
            return Art(png_bytes(picture.bitmap), PNG_TYPE)
        except (BmpFormatError, ListFormatError, OSError, ValueError) as exc:
            logger.warning("cannot build %s: %s", name, exc)
            return None
