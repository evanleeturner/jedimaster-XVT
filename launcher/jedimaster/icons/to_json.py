"""Export the icon view as plain JSON data, and describe it as a schema.

Purpose:
    Give the briefing page what it needs to draw icons later: each sheet
    (name, color, game path, file, size, the file name of its picture), the
    boxes, the craft table and the IFF table; and publish the JSON Schema
    (draft 2020-12) the export always satisfies.

Flow:
    ``icons_to_json`` walks the view and the tables; ``picture_name`` names
    each sheet's PNG file; ``icons_schema`` builds the schema by hand, one
    closed object per part.

Invariants:
    - The output holds only dicts, lists, strings, integers, booleans and
      null, so ``json.dumps`` always succeeds.
    - ``iffs`` has 256 entries: the sheet name each IFF byte draws with.
    - A file is the path the game path resolved to, relative to the
      install, with forward slashes and its letter case on disk.
    - Every object is closed (``additionalProperties`` false) and lists all
      its fields as required.

Call:
    ``json.dumps(icons_to_json(icons_view(install)), indent=1)``
"""

from __future__ import annotations

import logging
import re
from pathlib import Path
from typing import Any

from .game import IconView
from .game import LIST_GAME_PATH
from .game import SHEET_COLORS
from .game import sheet_for_iff
from .game import TRANSPARENT
from .tables import BOXES
from .tables import CRAFT_BOXES

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/icons.schema.json"
JSON_FORMAT = "jedimaster.icons"
JSON_FORMAT_VERSION = 1
IFF_VALUES = 256
UNSAFE = re.compile(r"[^A-Za-z0-9_.-]")


def picture_name(name: str) -> str:
    """Return the PNG file name of a sheet: its image name, made file-safe.

    Every character but letters, digits, ``_``, ``.`` and ``-`` becomes
    ``_``; ``.png`` is added. Does not check two names for a collision.
    """
    return f"{UNSAFE.sub('_', name) or '_'}.png"


def _relative(path: Path, install: Path) -> str:
    try:
        return path.relative_to(install).as_posix()
    except ValueError:
        return path.as_posix()


def icons_to_json(view: IconView) -> dict[str, Any]:
    """Return the view as JSON-ready data: dicts, lists and scalars.

    Always returns a dict with ``format``, ``format_version``,
    ``balance_of_power``, ``list``, ``transparent_index``, ``sheets`` (in
    table order), ``boxes``, ``crafts`` and ``iffs``. A sheet's ``color`` is
    None for an image ``SHEET_COLORS`` does not name. Does not include pixels or
    colors (the pictures hold them) and does not write the pictures.
    """
    sheets = [
        {
            "name": sheet.name,
            "color": SHEET_COLORS.get(sheet.name),
            "game_path": sheet.game_path,
            "file": _relative(sheet.path, view.install),
            "width": sheet.bitmap.width,
            "height": sheet.bitmap.height,
            "picture": picture_name(sheet.name),
        }
        for sheet in view.sheets.values()
    ]
    boxes = [
        {"box": i, **box._asdict(), "width": box.width, "height": box.height}
        for i, box in enumerate(BOXES)
    ]
    data = {
        "format": JSON_FORMAT,
        "format_version": JSON_FORMAT_VERSION,
        "balance_of_power": view.balance_of_power,
        "list": {
            "game_path": LIST_GAME_PATH,
            "file": _relative(view.list_path, view.install),
        },
        "transparent_index": TRANSPARENT,
        "sheets": sheets,
        "boxes": boxes,
        "crafts": [{"craft": t, "box": b} for t, b in enumerate(CRAFT_BOXES)],
        "iffs": [sheet_for_iff(iff) for iff in range(IFF_VALUES)],
    }
    logger.debug("exported %d sheets", len(sheets))
    return data


# ---- schema -------------------------------------------------------------


def _object(description: str, props: dict[str, Any]) -> dict[str, Any]:
    return {
        "type": "object",
        "description": description,
        "properties": props,
        "required": list(props),
        "additionalProperties": False,
    }


def _ints(*names: str, minimum: int | None = 0) -> dict[str, Any]:
    kind: dict[str, Any] = {"type": "integer"}
    if minimum is not None:
        kind["minimum"] = minimum
    return {name: dict(kind) for name in names}


def icons_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``icons_to_json`` output.

    Always returns the same dict. Does not encode cross-field rules (for
    example, that a craft names an existing box or a picture file exists).
    """
    text = {"type": "string"}
    sheet = _object(
        "One icon sheet: image name, color, where it came from, size, picture.",
        {
            "name": text,
            "color": {"anyOf": [text, {"type": "null"}]},
            "game_path": text,
            "file": text,
            **_ints("width", "height"),
            "picture": {"type": "string", "pattern": r"^[A-Za-z0-9_.-]+\.png$"},
        },
    )
    box = _object(
        "One box: its number, edges (inclusive) and size, in sheet pixels.",
        _ints("box", "left", "top", "right", "bottom", "width", "height"),
    )
    craft = _object("One craft type and the box of its icon.", _ints("craft", "box"))
    listed = _object(
        "The image list that registers the sheets.", {"game_path": text, "file": text}
    )
    top = _object(
        "The briefing map's icon sheets and tables (jedimaster export).",
        {
            "format": {"const": JSON_FORMAT},
            "format_version": {"const": JSON_FORMAT_VERSION},
            "balance_of_power": {"type": "boolean"},
            "list": {"$ref": "#/$defs/List"},
            "transparent_index": {"const": TRANSPARENT},
            "sheets": {"type": "array", "items": {"$ref": "#/$defs/Sheet"}},
            "boxes": {"type": "array", "items": {"$ref": "#/$defs/Box"}},
            "crafts": {"type": "array", "items": {"$ref": "#/$defs/Craft"}},
            "iffs": {
                "type": "array",
                "items": text,
                "minItems": IFF_VALUES,
                "maxItems": IFF_VALUES,
            },
        },
    )
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP briefing map icons (jedimaster export)",
        **top,
        "$defs": {"Box": box, "Craft": craft, "List": listed, "Sheet": sheet},
    }
