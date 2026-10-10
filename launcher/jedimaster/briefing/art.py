"""The briefing's pictures and sounds, built from an install and kept.

Purpose:
    Give the page's art route the bytes of a fixed set of files: the five
    map icon sheets and the grey sheet as PNG pictures, font 10's and font
    12's glyph atlases as PNG pictures, the three pictures that lie under
    the map (one per background) and the four sounds the setup screen uses
    as WAV files. Nothing else is ever served.

Flow:
    ``ArtStore.get(name)`` builds the file on first use (icon sheets from
    ``jedimaster.icons``, an atlas from ``jedimaster.fonts``, an under-map
    picture from ``under``, a sound from the game's sound list and the WAV
    it names) and keeps the bytes.
    ``sound_files`` resolves the sound list; ``build`` also uses it.

Invariants:
    - ``ART_NAMES`` is the whole set; any other name, or a store with no
      install, answers None.
    - A file that cannot be built is None, with a WARNING, and is tried
      again on the next request.
    - Sheets are the same pictures ``icons export`` writes, the atlases the
      same as ``fonts export``: tinting and drawing happen on the page.
    - An under-map picture is the panel's 360 by 240 part of the game's
      saved picture, widened to 8 bits; the pad, the activity frame and the
      map's shading are required for it, and a missing one is a WARNING and
      no picture (the page draws black).

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
from ..icons import BmpFile
from ..icons import icons_view
from ..icons import picture_name
from ..icons import png_bytes
from ..icons import read_bmp
from ..icons.bmp import BmpFormatError
from ..lists import ListFormatError
from ..lists import read_sounds
from ..lists import sounds_view
from ..lists.files import resolve
from .names import FONT_GAME_NAME
from .names import GREY_SHEET
from .names import HEADING_FONT_NAME
from .names import ICON_SHEETS
from .names import PNG_TYPE
from .names import SOUND_LIST
from .names import SOUND_NAMES
from .names import WAV_SUFFIX
from .names import WAV_TYPE
from .under import BACKGROUND_FILES
from .under import saved_picture
from .under import under_art_name
from .under import widened_png

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
HEADING_FONT_ART = atlas_name(HEADING_FONT_NAME)
UNDER_ART = {under_art_name(kind): kind for kind in BACKGROUND_FILES}
"""Each under-map picture's art name and its background kind."""
SOUND_ART = {sound_art_name(name): name for name in SOUND_NAMES}
"""Each sound's art name and its name in the game's list."""
ART_NAMES: tuple[str, ...] = (
    *SHEET_ART.values(),
    FONT_ART,
    *SOUND_ART,
    HEADING_FONT_ART,
    *UNDER_ART,
)
"""Every name the art route serves, fifteen in all."""
PAD_FILE = "frontres\\BLANKPAD.BMP"
FRAME_FILE = "frontres\\ALLACTIV.BMP"
SHADING_FILE = "frontres\\map.bmp"


def read_game_font(install: Path, game_name: str) -> Font | None:
    """Return the font file ``game_name`` as the install holds it, or None.

    Resolves the file (Balance of Power's first) and reads it. None, with a
    WARNING, when the file is missing or cannot be read. Does not check
    that the name is one of the menu fonts.
    """
    path = resolve(install, game_name)
    if path is None or not path.is_file():
        logger.warning("%s: %s not found", install, game_name)
        return None
    try:
        return read_font(path)
    except (FontFormatError, OSError) as exc:
        logger.warning("cannot read %s: %s", path, exc)
        return None


def read_font10(install: Path) -> Font | None:
    """Return font 10 (``times10.abp``) as the install holds it, or None.

    As ``read_game_font`` for font 10: None with a WARNING when it is
    missing or unreadable.
    """
    return read_game_font(install, FONT_GAME_NAME)


def read_font12(install: Path) -> Font | None:
    """Return font 12 (``times12.abp``) as the install holds it, or None.

    As ``read_game_font`` for font 12: None with a WARNING when it is
    missing or unreadable.
    """
    return read_game_font(install, HEADING_FONT_NAME)


def read_layer(install: Path, game_path: str) -> BmpFile | None:
    """Return the bitmap ``game_path`` names, or None (with a WARNING).

    Resolves the file (Balance of Power's first) and reads it with the
    game's bitmap reader. None when it is missing or cannot be read.
    """
    path = resolve(install, game_path)
    if path is None or not path.is_file():
        logger.warning("%s: %s not found", install, game_path)
        return None
    try:
        return read_bmp(path)
    except (BmpFormatError, OSError) as exc:
        logger.warning("cannot read %s: %s", path, exc)
        return None


def under_picture(install: Path, kind: str) -> bytes | None:
    """Return the PNG of the picture under the map for background ``kind``.

    Reads the background, the pad, the activity frame and the map's
    shading; None (with a WARNING from the reader) when any of the four
    cannot be read. Does not keep anything: ``ArtStore`` does.
    """
    layers = [
        read_layer(install, BACKGROUND_FILES[kind]),
        read_layer(install, PAD_FILE),
        read_layer(install, FRAME_FILE),
        read_layer(install, SHADING_FILE),
    ]
    if any(layer is None for layer in layers):
        logger.debug("under-map picture %s: a layer is missing", kind)
        return None
    return widened_png(saved_picture(*layers))


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
            if name in (FONT_ART, HEADING_FONT_ART):
                game_name = FONT_GAME_NAME if name == FONT_ART else HEADING_FONT_NAME
                font = read_game_font(install, game_name)
                if font is None:
                    return None
                return Art(atlas_png(font_atlas(font)), PNG_TYPE)
            if name in UNDER_ART:
                png = under_picture(install, UNDER_ART[name])
                return None if png is None else Art(png, PNG_TYPE)
            sheet = {art: sheet for sheet, art in SHEET_ART.items()}[name]
            picture = icons_view(install).sheets.get(sheet)
            if picture is None:
                logger.warning("sheet %s is not in the install", sheet)
                return None
            return Art(png_bytes(picture.bitmap), PNG_TYPE)
        except (BmpFormatError, ListFormatError, OSError, ValueError) as exc:
            logger.warning("cannot build %s: %s", name, exc)
            return None
