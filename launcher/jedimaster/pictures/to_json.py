"""Export the menu pictures as PNG files and JSON data, and describe the JSON.

Purpose:
    Give the page that will draw the menu screens each picture as a PNG in
    its palette's own 8-bit colors with index 0 fully transparent (the
    usual draw skips index 0), and the JSON naming each picture's game
    name, file, size, PNG and index 0's color, so the page can paint that
    color first where a screen draws a picture opaquely; and publish the
    JSON Schema (draft 2020-12) the export always satisfies.

Flow:
    ``png_names`` names each loaded picture's PNG; ``pictures_to_json``
    describes the view; ``pictures_schema`` builds the schema with the
    text export's helpers. The PNGs are written with ``icons.png``.

Invariants:
    - A PNG name is the game file name's stem (letters, digits, ``_``,
      ``.``, ``~`` and ``-`` kept, anything else ``_``) plus ``.png``; a
      name already taken gets ``-2``, ``-3`` and so on.
    - Every name of the view's list is in ``pictures``; one not loaded has
      null size, file, PNG and color.
    - ``file`` is the path the game name resolved to, relative to the
      install, with forward slashes and its letter case on disk.

Call:
    ``pictures_to_json(view, png_names(view))``
"""

from __future__ import annotations

import logging
import re
from pathlib import Path
from pathlib import PurePosixPath
from typing import Any

from ..icons.game import color_565
from ..icons.game import TRANSPARENT
from ..text.to_json import closed_object
from ..text.to_json import ints
from ..text.to_json import nullable
from .game import LOADED
from .game import MISSING
from .game import NOT_LOADED
from .game import Picture
from .game import PicturesView

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/pictures.schema.json"
JSON_FORMAT = "jedimaster.pictures"
JSON_FORMAT_VERSION = 1
UNSAFE = re.compile(r"[^a-z0-9_.~-]")


def png_names(view: PicturesView) -> dict[str, str]:
    """Return the PNG file name of each loaded picture, by game name.

    The game name's last part, its stem made file-safe, plus ``.png``; a
    repeat gets ``-2``, ``-3``... before ``.png``. Pictures not loaded get
    no name, nor do pictures without pixels (PNG has no empty image). Does
    not touch the file system.
    """
    names: dict[str, str] = {}
    taken: set[str] = set()
    for picture in view.pictures:
        bitmap = picture.bitmap
        if bitmap is None or bitmap.width <= 0 or bitmap.height <= 0:
            continue
        stem = UNSAFE.sub("_", PurePosixPath(picture.name.replace("\\", "/")).stem)
        candidate, count = f"{stem or '_'}.png", 1
        while candidate in taken:
            count += 1
            candidate = f"{stem or '_'}-{count}.png"
        taken.add(candidate)
        names[picture.name] = candidate
    return names


def _relative(path: Path | None, install: Path) -> str | None:
    if path is None:
        return None
    try:
        return path.relative_to(install).as_posix()
    except ValueError:
        return path.as_posix()


def _picture(picture: Picture, install: Path, png: str | None) -> dict[str, Any]:
    bitmap = picture.bitmap
    if bitmap is None:
        color = None
    else:
        red, green, blue = bitmap.palette[TRANSPARENT]
        color = {
            "red": red,
            "green": green,
            "blue": blue,
            "color": color_565(red, green, blue),
        }
    return {
        "name": picture.name,
        "status": picture.status,
        "file": _relative(picture.path, install) if bitmap is not None else None,
        "width": bitmap.width if bitmap is not None else None,
        "height": bitmap.height if bitmap is not None else None,
        "picture": png,
        "index_0": color,
    }


def pictures_to_json(view: PicturesView, names: dict[str, str]) -> dict[str, Any]:
    """Return the view as JSON-ready data: dicts, lists and scalars.

    Always returns ``format``, ``format_version``, ``balance_of_power``,
    ``transparent_index`` and ``pictures`` (every name, in the list's
    order); ``names`` gives each loaded picture's PNG (``png_names``). Does
    not include pixels (the PNGs hold them) or write the PNGs.
    """
    pictures = [_picture(p, view.install, names.get(p.name)) for p in view.pictures]
    logger.debug("exported %d pictures", len(pictures))
    return {
        "format": JSON_FORMAT,
        "format_version": JSON_FORMAT_VERSION,
        "balance_of_power": view.balance_of_power,
        "transparent_index": TRANSPARENT,
        "pictures": pictures,
    }


def pictures_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``pictures_to_json`` output.

    Always returns the same dict. Does not encode cross-field rules (for
    example, that a loaded picture has a PNG).
    """
    color = closed_object(
        "Index 0's palette color: 8-bit red, green, blue and its 565 value.",
        {**ints("red", "green", "blue", maximum=255), **ints("color", maximum=0xFFFF)},
    )
    size = {"type": "integer", "minimum": 0}
    picture = closed_object(
        "One picture name: status, file, size, PNG, index 0's color.",
        {
            "name": {"type": "string", "pattern": r"^frontres\\"},
            "status": {"enum": [LOADED, MISSING, NOT_LOADED]},
            "file": nullable({"type": "string"}),
            "width": nullable(size),
            "height": nullable(size),
            "picture": nullable({"type": "string", "pattern": r"^[a-z0-9_.~-]+\.png$"}),
            "index_0": nullable({"$ref": "#/$defs/Color"}),
        },
    )
    top = closed_object(
        "The menu screens' pictures (jedimaster export).",
        {
            "format": {"const": JSON_FORMAT},
            "format_version": {"const": JSON_FORMAT_VERSION},
            "balance_of_power": {"type": "boolean"},
            "transparent_index": {"const": TRANSPARENT},
            "pictures": {"type": "array", "items": {"$ref": "#/$defs/Picture"}},
        },
    )
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP menu pictures (jedimaster export)",
        **top,
        "$defs": {"Color": color, "Picture": picture},
    }
