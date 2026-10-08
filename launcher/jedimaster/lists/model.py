"""Dataclasses for the lists as written: one per kind, raw values kept.

Purpose:
    Hold everything a list file says, in file order, so the game's view, the
    text dump and the JSON export all read one model and never the bytes.

Flow:
    ``reader`` builds these from bytes; ``game`` derives the game's view from
    them; ``render`` and ``to_json`` print them. Nothing here parses.

Invariants:
    - Values are kept as written: text keeps its letter case, numbers keep
      the text they were read from beside the value, repeated names stay.
    - Every value keeps the line it came from (``TextLine.line`` or
      ``Word.line``, counted from 1).
    - Where the game stops reading, the reader stops too and keeps the rest
      unread (``unread``), so nothing in the file is dropped or invented.
    - A value the file does not hold is ``None``, never a made-up default.
    - Each list keeps ``size`` (bytes) and ``end_mark`` (the file ends with
      the 0x1A byte).

Call:
    ``menu.items[0].kind``; ``images.groups[3].name.text``
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from dataclasses import field
from typing import Any

from .text import TextLine
from .text import Word

logger = logging.getLogger(__name__)


def _const(value: str) -> Any:
    return field(default=value, metadata={"const": value})


# ---- mission menus --------------------------------------------------------


@dataclass
class MenuComment:
    """A line starting with ``//`` where an entry or a section may start."""

    line: TextLine
    kind: str = _const("comment")


@dataclass
class MenuSection:
    """A line starting with ``[``: the section every later entry belongs to."""

    line: TextLine
    name: str
    kind: str = _const("section")


@dataclass
class MenuEntry:
    """Three lines: id, mission file (maybe with a marker), title."""

    id_line: TextLine
    id: int
    file_line: TextLine | None
    title_line: TextLine | None
    marker: str
    star_texts: list[str]
    star_numbers: list[int]
    file_name: str | None
    complete: bool
    kind: str = _const("entry")


@dataclass
class MenuList:
    """A mission menu (``mission.lst``, ``rebel.lst``, ``imperial.lst``)."""

    size: int
    end_mark: bool
    items: list[MenuComment | MenuSection | MenuEntry]
    kind: str = _const("menu")


# ---- sequence files -------------------------------------------------------


@dataclass
class SequenceList:
    """A tournament, battle or campaign: count, mission lines, description."""

    size: int
    end_mark: bool
    count_line: TextLine | None
    count: int
    missions: list[TextLine]
    description_line: int | None
    description: str
    kind: str = _const("sequence")


# ---- word lists -----------------------------------------------------------


@dataclass
class ImageGroup:
    """Three words: bitmap file, image name, compress flag."""

    bitmap: Word
    name: Word
    flag: Word
    flag_value: int
    repeat_of: int | None


@dataclass
class ImageList:
    """An image list: a skipped first line, then groups of three words."""

    size: int
    end_mark: bool
    first_line: TextLine | None
    groups: list[ImageGroup]
    end: str
    unread: list[Word]
    kind: str = _const("images")


@dataclass
class ShipPair:
    """Two words: model file, craft type."""

    model: Word
    type: Word
    type_value: int


@dataclass
class ShipList:
    """The ship list: pairs of words from the very first word."""

    size: int
    end_mark: bool
    pairs: list[ShipPair]
    end: str
    unread: list[Word]
    kind: str = _const("ships")


@dataclass
class SoundPair:
    """Two words: WAV file, sound name."""

    wav: Word
    name: Word
    repeat_of: int | None


@dataclass
class SoundList:
    """The sound list: a skipped first line, then pairs of words."""

    size: int
    end_mark: bool
    first_line: TextLine | None
    pairs: list[SoundPair]
    end: str
    unread: list[Word]
    kind: str = _const("sounds")


# ---- counted line lists ---------------------------------------------------


@dataclass
class Cutscene:
    """Four values (movie, numbers, thumbnail, description) and comments."""

    comments: list[TextLine]
    movie: TextLine | None
    numbers_line: TextLine | None
    numbers: list[int]
    thumbnail: TextLine | None
    description: TextLine | None
    status: str


@dataclass
class CutsceneList:
    """The cutscene list: a count, then that many four-value entries."""

    size: int
    end_mark: bool
    count_line: TextLine | None
    count: int
    entries: list[Cutscene]
    end: str
    unread: list[TextLine]
    kind: str = _const("cutscenes")


@dataclass
class AwardRecord:
    """Campaign id, main medal, 15 multiplayer and 15 single-player names."""

    comments: list[TextLine]
    campaign_line: TextLine | None
    campaign: int | None
    main: TextLine | None
    multiplayer: list[TextLine]
    singleplayer: list[TextLine]
    complete: bool


@dataclass
class AwardList:
    """The campaign medal list: a count, then that many 32-value records."""

    size: int
    end_mark: bool
    count_line: TextLine | None
    count: int
    records: list[AwardRecord]
    end: str
    unread: list[TextLine]
    kind: str = _const("awards")


ListFile = (
    MenuList
    | SequenceList
    | ImageList
    | ShipList
    | SoundList
    | CutsceneList
    | AwardList
)
"""Any reader's result."""

LIST_CLASSES: tuple[type, ...] = (
    MenuList,
    SequenceList,
    ImageList,
    ShipList,
    SoundList,
    CutsceneList,
    AwardList,
)
