"""Print the game's view in the answer sheets' format, and dump a raw list.

Purpose:
    ``render_<kind>`` prints a game's-view object line for line as the answer
    sheets print it, header lines included, so a rendering diffs empty
    against its sheet. ``render_raw`` prints a reader's result as written,
    for the command line's ``lists dump``.

Flow:
    Header (``kind``, ``name``, ``file``), then the kind's lines; text values
    go through ``c_quote``.

Invariants:
    - Every rendering ends with one newline.
    - The sheets' vocabulary is kept as printed: a menu entry's title prints
      as ``description=``, unavailability as ``unavailable=``.
    - The sound line format (``sound <i> name= file=``) is this package's
      own: no sheet shows a filled sound table.

Call:
    ``render_menu(menu_view(menu, "training", "network"), name, file)``
"""

from __future__ import annotations

import dataclasses
import logging
from typing import Any

from .game import GameAwards
from .game import GameCutscenes
from .game import GameImages
from .game import GameMenu
from .game import GameSequence
from .game import GameShips
from .game import GameSounds
from .text import c_quote
from .text import TextLine
from .text import Word

logger = logging.getLogger(__name__)


def _header(kind: str, name: str, file: str) -> list[str]:
    return [f"kind {kind}", f"name {c_quote(name)}", f"file {c_quote(file)}"]


def _join(lines: list[str]) -> str:
    return "\n".join(lines) + "\n"


def render_menu(menu: GameMenu, name: str, file: str) -> str:
    """Return a menu's game view as a sheet: header, count, one line per entry.

    ``name`` and ``file`` are the header's game path and install file. Does
    not check them against the view.
    """
    lines = _header("menu", name, file) + [f"count {len(menu.entries)}"]
    for i, e in enumerate(menu.entries):
        lines.append(
            f"entry {i} id={e.id} unavailable={int(not e.available)} "
            f"section={c_quote(e.section)} file={c_quote(e.file)} "
            f"description={c_quote(e.title)}"
        )
    return _join(lines)


def render_sequence(
    sequence: GameSequence,
    name: str,
    file: str,
    menu: tuple[str, int, int] | None = None,
) -> str:
    """Return a sequence's game view as a sheet.

    ``menu`` is the menu's game path, the entry's index and its id, printed
    as the ``menu`` line; None leaves that line out. Does not check it.
    """
    lines = _header("sequence", name, file)
    if menu is not None:
        lines.append(f"menu {c_quote(menu[0])} entry={menu[1]} id={menu[2]}")
    lines.append(f"description {c_quote(sequence.description)}")
    lines.append(f"count_line {c_quote(sequence.count_line)} count={sequence.count}")
    for i, line in enumerate(sequence.missions):
        lines.append(f"mission {i} line={c_quote(line)}")
    return _join(lines)


def render_images(images: GameImages, name: str, file: str) -> str:
    """Return an image table as a sheet: result, count, images in table order.

    Does not check ``name`` and ``file`` against the view.
    """
    lines = _header("images", name, file)
    lines += [f"result {images.result}", f"count {len(images.images)}"]
    for i, image in enumerate(images.images):
        lines.append(
            f"image {i} name={c_quote(image.name)} width={image.width} "
            f"height={image.height} compressed={int(image.compressed)}"
        )
    return _join(lines)


def render_ships(ships: GameShips, name: str, file: str) -> str:
    """Return the ship view as a sheet: count, ships, the 18 type slots.

    Does not check ``name`` and ``file`` against the view.
    """
    lines = _header("ships", name, file) + [f"count {len(ships.ships)}"]
    for i, ship in enumerate(ships.ships):
        lines.append(f"ship {i} model={c_quote(ship.model)} type={ship.type}")
    lines.append("type_to_ship " + " ".join(str(s) for s in ships.type_to_ship))
    return _join(lines)


def render_sounds(sounds: GameSounds, name: str, file: str) -> str:
    """Return the sound table as a sheet: result, count, one line per sound.

    The sound line's form is this package's own (no sheet shows one). Does
    not check ``name`` and ``file`` against the view.
    """
    lines = _header("sounds", name, file)
    lines += [f"result {sounds.result}", f"count {len(sounds.sounds)}"]
    for i, sound in enumerate(sounds.sounds):
        lines.append(f"sound {i} name={c_quote(sound.name)} file={c_quote(sound.wav)}")
    return _join(lines)


def render_cutscenes(cutscenes: GameCutscenes, name: str, file: str) -> str:
    """Return the cutscene table as a sheet: result, count, one line each.

    Does not check ``name`` and ``file`` against the view.
    """
    lines = _header("cutscenes", name, file)
    lines += [f"result {cutscenes.result}", f"count {len(cutscenes.cutscenes)}"]
    for i, c in enumerate(cutscenes.cutscenes):
        lines.append(
            f"cutscene {i} movie={c_quote(c.movie)} campaign={c.campaign} "
            f"after_debriefing={c.after_debriefing} mission={c.mission} "
            f"thumbnail={c_quote(c.thumbnail)} description={c_quote(c.description)}"
        )
    return _join(lines)


def render_awards(awards: GameAwards, name: str, file: str) -> str:
    """Return the medal table as a sheet: result, count, three lines each.

    Does not check ``name`` and ``file`` against the view.
    """
    lines = _header("awards", name, file)
    lines += [f"result {awards.result}", f"count {len(awards.awards)}"]
    for i, a in enumerate(awards.awards):
        lines.append(f"award {i} campaign={a.campaign} main={c_quote(a.main)}")
        for row, names in (
            ("multiplayer", a.multiplayer),
            ("singleplayer", a.singleplayer),
        ):
            lines.append(f"award {i} {row} " + " ".join(c_quote(n) for n in names))
    return _join(lines)


# ---- the reader's own view ------------------------------------------------


def _value(value: Any) -> str:
    if isinstance(value, TextLine):
        return f"{value.line}:{c_quote(value.text)}+{c_quote(value.ending)}"
    if isinstance(value, Word):
        return f"{value.line}:{c_quote(value.text)}"
    if isinstance(value, str):
        return c_quote(value)
    if isinstance(value, bool):
        return str(int(value))
    if value is None:
        return "-"
    if isinstance(value, list):
        return "[" + " ".join(_value(v) for v in value) + "]"
    return str(value)


def _fields(item: Any) -> str:
    return " ".join(
        f"{f.name}={_value(getattr(item, f.name))}" for f in dataclasses.fields(item)
    )


def render_raw(result: Any) -> str:
    """Return a reader's result as text: one line per value or list item.

    Scalars and lines print as ``<field> <value>``; each item of a list of
    entries, groups, pairs or records prints as ``<field> <index>`` then its
    fields. A line prints as ``<number>:"text"+"ending"``, a word as
    ``<number>:"text"``, None as ``-``. Does not derive the game's view.
    """
    lines = []
    for f in dataclasses.fields(result):
        value = getattr(result, f.name)
        nested = isinstance(value, list) and any(
            dataclasses.is_dataclass(v) and not isinstance(v, (TextLine, Word))
            for v in value
        )
        if nested:
            lines.append(f"{f.name} {len(value)}")
            lines += [f"{f.name} {i} {_fields(v)}" for i, v in enumerate(value)]
        else:
            lines.append(f"{f.name} {_value(value)}")
    return _join(lines)
