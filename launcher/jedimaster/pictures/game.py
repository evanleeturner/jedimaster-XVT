"""The menu screens' pictures: the list of names, each picture read, two hashes.

Purpose:
    Build the list of every picture name of the menu folders (the base
    game's ``frontres`` and Balance of Power's ``FRONTRES``), resolve each
    name the way the game does for one view, read each picture with the
    icon reader's ``read_bmp`` (8 bits per pixel, uncompressed or RLE8, the
    game's way), and fingerprint its pixels and colors.

Flow:
    ``picture_names`` lists both folders (any letter case), keeps the
    ``.bmp`` files, lowercases them, writes each as ``frontres\\<name>``,
    joins the two without repeats and sorts them by ``name_order``: their
    letters and digits first, punctuation left out, then all their bytes.
    ``pictures_view`` resolves and reads each name into a ``Picture``.
    ``fnv1a64`` is the hash; ``pixels_hash`` and ``colors_hash`` apply it.

Invariants:
    - Every view runs the whole list: a name that does not resolve in the
      view is ``missing``; one whose file the reader refuses is
      ``not_loaded``; nothing is guessed.
    - Colors are the palette's 565 values (``icons.game.color_565``),
      index 0 first. How a screen draws index 0 (skipped, or painted in its
      color) is the screen's business: the picture keeps the color.
    - ``file`` is the sheets' label: the game name lowercased with forward
      slashes, behind ``BalanceOfPower/`` when it resolved there.

Call:
    ``view = pictures_view(install); view.pictures[0].bitmap.width``
"""

from __future__ import annotations

import logging
import os
import struct
from dataclasses import dataclass
from pathlib import Path

from ..icons.bmp import BmpFile
from ..icons.bmp import BmpFormatError
from ..icons.bmp import read_bmp
from ..icons.game import color_565
from ..install import BALANCE_OF_POWER
from ..install import child_in_any_case
from ..lists.files import resolve
from ..lists.files import sheet_file

logger = logging.getLogger(__name__)

FOLDER = "frontres"
"""The menu pictures' folder, in the install and in ``BalanceOfPower/``."""
SUFFIX = b".bmp"
FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK_64 = (1 << 64) - 1
LOADED = "loaded"
MISSING = "missing"
NOT_LOADED = "not_loaded"


@dataclass
class Picture:
    """One name of the list in one view: where it resolved, what was read."""

    name: str
    status: str
    path: Path | None
    file: str | None
    bitmap: BmpFile | None
    colors: list[int]
    error: Exception | None


@dataclass
class PicturesView:
    """Every picture name of an install, resolved and read for one view."""

    install: Path
    balance_of_power: bool
    pictures: list[Picture]


def fnv1a64(data: bytes) -> int:
    """Return the 64-bit FNV-1a hash of ``data``.

    Starts from ``0xcbf29ce484222325``; for each byte, exclusive-or it in,
    then multiply by ``0x100000001b3`` modulo 2^64. Returns the offset
    basis for empty data. Does not take a seed.
    """
    value = FNV_OFFSET
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & MASK_64
    return value


def pixels_hash(bitmap: BmpFile) -> int:
    """Return ``fnv1a64`` of the decoded palette indices, rows top first.

    Hashes exactly ``width * height`` bytes. Does not look at the palette.
    """
    return fnv1a64(bitmap.pixels)


def colors_hash(colors: list[int]) -> int:
    """Return ``fnv1a64`` of the 565 colors, each two bytes, low byte first.

    Hashes the colors as given (256 for a picture). Raises
    ``struct.error`` for a value outside 0 to 65535. Does not check the
    count.
    """
    return fnv1a64(b"".join(struct.pack("<H", color) for color in colors))


def name_order(name: str) -> tuple[bytes, bytes]:
    """Return the sort key of a picture name: letters and digits, then bytes.

    The first part keeps only the name's ASCII letters and digits (so
    ``-``, ``.``, ``_``, ``~`` and the backslash are passed over, as a
    language-aware sort passes over punctuation); the second, every byte,
    breaks ties by byte value. Does not fold letter case: the names are
    already lowercase.
    """
    raw = os.fsencode(name)
    return bytes(b for b in raw if chr(b).isalnum() and b < 0x80), raw


def _bmp_names(folder: Path | None) -> list[str]:
    """Return the lowercased ``.bmp`` file names of a folder (none if absent)."""
    if folder is None or not folder.is_dir():
        return []
    names = []
    for entry in sorted(os.listdir(folder)):
        raw = os.fsencode(entry).lower()
        if raw.endswith(SUFFIX) and (folder / entry).is_file():
            names.append(os.fsdecode(raw))
    logger.debug("%s: %d pictures", folder, len(names))
    return names


def picture_names(install: str | os.PathLike[str]) -> list[str]:
    """Return the pictures' list: ``frontres\\<name>`` for both folders' files.

    Every file name ending ``.bmp`` (any letter case) in the install's
    ``frontres`` and ``BalanceOfPower/FRONTRES`` (each folder found in any
    letter case), lowercased (ASCII letters only), joined without repeats,
    sorted by ``name_order``. Returns ``[]`` when neither folder exists.
    Does not read the files.
    """
    install = Path(install)
    base = child_in_any_case(install, FOLDER)
    bop = child_in_any_case(install, BALANCE_OF_POWER)
    bop_folder = child_in_any_case(bop, FOLDER) if bop is not None else None
    names = set(_bmp_names(base)) | set(_bmp_names(bop_folder))
    listed = sorted((f"{FOLDER}\\{name}" for name in names), key=name_order)
    logger.info("pictures: %d names", len(listed))
    return listed


def load_picture(name: str, path: Path, label: str) -> Picture:
    """Return the picture ``name`` read from ``path``, labelled ``label``.

    ``not_loaded`` (with the error, and a WARNING) when ``read_bmp`` refuses
    the file or it cannot be read, else ``loaded`` with the bitmap and its
    565 colors. Does not check the size of the picture.
    """
    try:
        bitmap = read_bmp(path)
    except (BmpFormatError, OSError) as exc:
        logger.warning("picture %s: not loaded: %s", name, exc)
        return Picture(name, NOT_LOADED, path, label, None, [], exc)
    colors = [color_565(*entry) for entry in bitmap.palette]
    logger.debug("picture %s: %dx%d from %s", name, bitmap.width, bitmap.height, path)
    return Picture(name, LOADED, path, label, bitmap, colors, None)


def read_picture(install: Path, name: str, balance_of_power: bool) -> Picture:
    """Return one name of the list resolved and read for a view.

    ``missing`` when the name does not resolve to a file in the view, else
    what ``load_picture`` returns, labelled as the sheets label it. Does
    not check the name against the list.
    """
    path = resolve(install, name, balance_of_power)
    if path is None or not path.is_file():
        logger.debug("picture %s: missing", name)
        return Picture(name, MISSING, None, None, None, [], None)
    return load_picture(name, path, sheet_file(install, name, path))


def pictures_view(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> PicturesView:
    """Return every name of ``picture_names`` resolved and read for a view.

    Names resolve in ``BalanceOfPower/`` first unless ``balance_of_power``
    is False, any letter case. Always returns one ``Picture`` per name, in
    the list's order. Does not check that the install is one.
    """
    install = Path(install)
    pictures = [
        read_picture(install, name, balance_of_power) for name in picture_names(install)
    ]
    counts = {
        s: sum(p.status == s for p in pictures) for s in (LOADED, MISSING, NOT_LOADED)
    }
    logger.info("pictures view, balance of power %s: %s", balance_of_power, counts)
    return PicturesView(install, balance_of_power, pictures)
