"""Which model each object uses: the spec lists read from an install.

Purpose:
    Read the six spec lists (``ivfiles\\SPEC*.LST``, ``SPEC2*``,
    ``SPEC3*``, each for the 640 and the 320 screens) the way the game
    reads them, and give the model's game path of an object type and of a
    craft type from the object tables of ``tables``.

Flow:
    ``objects_view`` resolves and reads each list (``read_spec_list``)
    for one view; ``list_lines`` counts a list's lines; ``object_model``
    and ``craft_model`` look a type up.

Invariants:
    - A list line is cut at its first line feed, carriage return or 0
      byte; an empty line is skipped and not counted; the others count
      from 0.
    - An object type has a model when its record flags have bit 0x02 and
      its asset flags bit 0x01; its model is line ``resource_index`` of
      list ``texture_group``. Asset flag 0x40 marks a model loaded only on
      the proving grounds.
    - The model paths are the 640 lists' unless a size is given; a type
      without a model, a list the view lacks or a line it lacks gives
      None.
    - A list's ``file`` label is its game path as written, with forward
      slashes, behind ``BalanceOfPower/`` when it resolved there.

Call:
    ``view = objects_view(install); object_model(view, 1)``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from pathlib import Path

from ..install import BALANCE_OF_POWER
from ..lists.files import resolve
from .tables import CRAFT_OBJECTS
from .tables import OBJECT_TYPES
from .tables import ObjectType

logger = logging.getLogger(__name__)

LIST_STEMS = ("SPEC", "SPEC2", "SPEC3")
"""The spec list of each texture group, before its screen size."""
SIZES = (640, 320)
"""The two screen sizes: 640 for the 640 by 480 and 480 by 360 screens."""
DEFAULT_SIZE = 640
MODEL_RECORD_FLAG = 0x02
MODEL_ASSET_FLAG = 0x01
TEXTURES_ASSET_FLAG = 0x02
PROVING_GROUNDS_FLAG = 0x40
LINE_ENDS = (b"\n", b"\r", b"\0")


@dataclass(frozen=True)
class SpecList:
    """One spec list of a view: where it resolved, its counted lines."""

    group: int
    size: int
    game_path: str
    path: Path | None
    file: str | None
    lines: tuple[bytes, ...]


@dataclass(frozen=True)
class ObjectsView:
    """The six spec lists of an install, resolved and read for one view."""

    install: Path
    balance_of_power: bool
    lists: dict[tuple[int, int], SpecList]


def list_game_path(group: int, size: int) -> str:
    """Return a spec list's game path, such as ``ivfiles\\SPEC2640.LST``.

    Raises ``IndexError`` for a group other than 0, 1 or 2. Does not check
    the size.
    """
    return f"ivfiles\\{LIST_STEMS[group]}{size}.LST"


def list_lines(data: bytes) -> list[bytes]:
    """Return a list's counted lines: each cut at its first LF, CR or 0 byte.

    Lines are read up to each line feed; empty lines are skipped. Always
    returns a list. Does not check what the lines name.
    """
    lines = []
    for raw in data.split(b"\n"):
        cut = min((i for i in (raw.find(e) for e in LINE_ENDS) if i >= 0), default=-1)
        line = raw if cut < 0 else raw[:cut]
        if line:
            lines.append(line)
    logger.debug("list: %d lines", len(lines))
    return lines


def list_label(install: Path, game_path: str, found: Path) -> str:
    """Return the sheets' ``file`` label of a list resolved to ``found``.

    The game path as written with forward slashes, behind
    ``BalanceOfPower/`` when ``found`` lies in that folder. Does not
    resolve anything.
    """
    label = game_path.replace("\\", "/")
    try:
        first = found.relative_to(install).parts[0]
    except (ValueError, IndexError):
        first = ""
    if first.casefold() == BALANCE_OF_POWER.casefold():
        return f"{BALANCE_OF_POWER}/{label}"
    return label


def read_spec_list(
    install: Path, group: int, size: int, balance_of_power: bool
) -> SpecList:
    """Return one spec list resolved and read for a view.

    A list the view lacks comes back with no path, no label and no lines.
    Raises ``OSError`` when a resolved list cannot be read. Does not check
    the lines.
    """
    game_path = list_game_path(group, size)
    found = resolve(install, game_path, balance_of_power)
    if found is None or not found.is_file():
        logger.debug("list %s: missing", game_path)
        return SpecList(group, size, game_path, None, None, ())
    lines = list_lines(found.read_bytes())
    label = list_label(install, game_path, found)
    logger.debug("list %s: %d lines from %s", game_path, len(lines), found)
    return SpecList(group, size, game_path, found, label, tuple(lines))


def objects_view(
    install: str | os.PathLike[str], balance_of_power: bool = True
) -> ObjectsView:
    """Return the six spec lists of an install for one view.

    Lists resolve in ``BalanceOfPower/`` first unless ``balance_of_power``
    is False, any letter case. Raises ``OSError`` when a list cannot be
    read. Does not check that the install is one.
    """
    install = Path(install)
    lists = {
        (group, size): read_spec_list(install, group, size, balance_of_power)
        for group in range(len(LIST_STEMS))
        for size in SIZES
    }
    logger.info(
        "objects view, balance of power %s: %d lists found",
        balance_of_power,
        sum(s.path is not None for s in lists.values()),
    )
    return ObjectsView(install, balance_of_power, lists)


def has_model(record: ObjectType) -> bool:
    """Return True when an object type's record names a model.

    Record flag 0x02 and asset flag 0x01 (asset flag 0x02 instead names a
    block of textures). Does not look at the lists.
    """
    return bool(
        record.record_flags & MODEL_RECORD_FLAG
        and record.asset_flags & MODEL_ASSET_FLAG
    )


def object_model(
    view: ObjectsView, object_type: int, size: int = DEFAULT_SIZE
) -> str | None:
    """Return the game path of an object type's model, or None.

    None for a type outside 0 to 200, a type without a model, or a list or
    line the view lacks. The path is the list's line as written (Latin-1).
    Does not check that the path names a model file.
    """
    if not 0 <= object_type < len(OBJECT_TYPES):
        return None
    record = OBJECT_TYPES[object_type]
    if not has_model(record):
        return None
    spec = view.lists.get((record.texture_group, size))
    if spec is None or not 0 <= record.resource_index < len(spec.lines):
        logger.debug("object %d: line %d not found", object_type, record.resource_index)
        return None
    return spec.lines[record.resource_index].decode("latin-1")


def craft_object(craft_type: int) -> int | None:
    """Return a craft type's object type, or None outside 0 to 95.

    Returns the table's value as printed, even one beyond the object
    table. Does not check it.
    """
    if not 0 <= craft_type < len(CRAFT_OBJECTS):
        return None
    return CRAFT_OBJECTS[craft_type]


def craft_model(
    view: ObjectsView, craft_type: int, size: int = DEFAULT_SIZE
) -> str | None:
    """Return the game path of a craft type's model, or None.

    The model of its object type (``object_model``); None for a craft type
    outside 0 to 95. Does not check that the path names a model file.
    """
    object_type = craft_object(craft_type)
    return None if object_type is None else object_model(view, object_type, size)
