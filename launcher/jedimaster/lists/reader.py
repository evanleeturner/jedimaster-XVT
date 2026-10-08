"""Read each kind of list into its model, keeping the file as written.

Purpose:
    One ``read_<kind>`` per kind of list. Each reads the way the game reads
    that kind (by line or by word, as the notes describe), keeps every value
    raw with its line, and stops where the game stops, keeping the rest as
    ``unread``.

Flow:
    ``load`` (bytes, refusing non-text) -> ``split_lines`` or
    ``split_words`` -> the kind's walk -> its dataclass.

Invariants:
    - Never raises on a text file: a cut-off entry, a bad number or a short
      group is recorded (``complete``, ``status``, ``end``), not refused.
    - Refuses only what cannot be read at all: a path that cannot be opened
      (``OSError``), a value that is neither path nor bytes (``TypeError``),
      bytes holding a NUL (``NotTextError``).
    - ``end`` values: ``"eof"`` (the file ran out where a value could
      start), ``"mark"`` (the 0x1A byte stood where a value could start),
      ``"short"`` (the file ran out inside a group), ``"bad_number"`` (a
      word that must be a number is not), ``"count"`` (a counted list read
      all its count), ``"bad_numbers"`` (a cutscene's numbers line).

Call:
    ``menu = read_menu(path_or_bytes)``
"""

from __future__ import annotations

import logging
import os

from .model import AwardList
from .model import AwardRecord
from .model import Cutscene
from .model import CutsceneList
from .model import ImageGroup
from .model import ImageList
from .model import ListFile
from .model import MenuComment
from .model import MenuEntry
from .model import MenuList
from .model import MenuSection
from .model import SequenceList
from .model import ShipList
from .model import ShipPair
from .model import SoundList
from .model import SoundPair
from .text import atoi
from .text import decode
from .text import END_MARK
from .text import has_end_mark
from .text import load
from .text import scan_numbers
from .text import split_lines
from .text import split_words
from .text import TextLine
from .text import whole_number
from .text import Word

logger = logging.getLogger(__name__)

Source = str | os.PathLike[str] | bytes

COMMENT = "//"
SECTION = "["
MARKERS = ("&", "*")
AWARD_NAMES = 15
CUTSCENE_NUMBERS = 3


def _is_comment(line: TextLine) -> bool:
    return line.text.startswith(COMMENT)


# ---- mission menus --------------------------------------------------------


def _star(text: str) -> tuple[list[str], str | None]:
    """Split a ``*`` file line: two space-ended numbers, then the name."""
    rest = text[2:]
    fields: list[str] = []
    for _ in range(2):
        cut = rest.find(" ")
        if cut < 0:
            fields.append(rest)
            return fields, None
        fields.append(rest[:cut])
        rest = rest[cut + 1 :]
    return fields, rest


def _entry(lines: list[TextLine], at: int) -> MenuEntry:
    id_line = lines[at]
    file_line = lines[at + 1] if at + 1 < len(lines) else None
    title_line = lines[at + 2] if at + 2 < len(lines) else None
    marker = ""
    star_texts: list[str] = []
    file_name: str | None = None
    if file_line is not None:
        text = file_line.text
        marker = text[0] if text[:1] in MARKERS else ""
        if marker == "&":
            file_name = text[2:]
        elif marker == "*":
            star_texts, file_name = _star(text)
        else:
            file_name = text
    entry = MenuEntry(
        id_line=id_line,
        id=atoi(id_line.text),
        file_line=file_line,
        title_line=title_line,
        marker=marker,
        star_texts=star_texts,
        star_numbers=[atoi(t) for t in star_texts],
        file_name=file_name,
        complete=title_line is not None,
    )
    logger.debug(
        "menu entry line %d: id=%d marker=%r file=%r complete=%s",
        id_line.line,
        entry.id,
        marker,
        file_name,
        entry.complete,
    )
    return entry


def read_menu(source: Source) -> MenuList:
    """Return a mission menu as written: comments, sections, entries in order.

    Where an entry or section may start, a ``//`` line is a comment and a
    ``[`` line a section (its name drops the ``[`` and the line's last
    character); any other line, blank included, starts a three-line entry.
    Inside an entry no line is skipped. An entry the file ends inside is kept
    with ``complete`` False and the missing lines None. Raises only as
    ``load`` does. Does not lowercase, resolve markers into availability or
    check that a named mission exists.
    """
    data, label = load(source)
    lines = split_lines(data)
    items: list[MenuComment | MenuSection | MenuEntry] = []
    at = 0
    while at < len(lines):
        line = lines[at]
        if _is_comment(line):
            items.append(MenuComment(line))
            logger.debug("menu comment line %d: %r", line.line, line.text)
            at += 1
        elif line.text.startswith(SECTION):
            items.append(MenuSection(line, line.text[1:-1]))
            logger.debug("menu section line %d: %r", line.line, line.text[1:-1])
            at += 1
        else:
            items.append(_entry(lines, at))
            at += 3
    menu = MenuList(size=len(data), end_mark=has_end_mark(data), items=items)
    logger.info("read menu %s: %d items", label, len(items))
    return menu


# ---- sequence files -------------------------------------------------------


def read_sequence(source: Source) -> SequenceList:
    """Return a sequence file: count line, mission lines, raw description.

    Line 1 is the count (``atoi``); the next count lines are the missions
    (fewer when the file ends first; none for a count of 0 or less);
    ``description`` is every byte after them, as written (line endings and
    all, Latin-1 decoded), and ``description_line`` its first line number
    (None when nothing follows). Raises only as ``load`` does. Does not
    filter the description or check the missions exist.
    """
    data, label = load(source)
    lines = split_lines(data)
    count_line = lines[0] if lines else None
    count = atoi(count_line.text) if count_line else 0
    missions = lines[1 : 1 + max(count, 0)]
    used = 1 + len(missions) if lines else 0
    offset = sum(len(line.text) + len(line.ending) for line in lines[:used])
    description = decode(data)[offset:]
    sequence = SequenceList(
        size=len(data),
        end_mark=has_end_mark(data),
        count_line=count_line,
        count=count,
        missions=missions,
        description_line=used + 1 if description else None,
        description=description,
    )
    logger.debug("sequence count line: %r -> %d", count_line and count_line.text, count)
    for line in missions:
        logger.debug("sequence mission line %d: %r", line.line, line.text)
    logger.debug("sequence description: %d characters", len(description))
    logger.info(
        "read sequence %s: count %d, %d mission lines", label, count, len(missions)
    )
    return sequence


# ---- word lists -----------------------------------------------------------


def _word_end(words: list[Word], at: int, needed: int, mark: bool) -> str | None:
    """Return why a group of ``needed`` words cannot start at ``at``, or None.

    With ``mark`` False the 0x1A byte is a word like any other.
    """
    if at >= len(words):
        return "eof"
    if mark and words[at].text == END_MARK:
        return "mark"
    if at + needed > len(words):
        return "short"
    return None


def _repeat(seen: dict[str, int], name: str, index: int) -> int | None:
    first = seen.setdefault(name, index)
    return None if first == index else first


def read_images(source: Source) -> ImageList:
    """Return an image list: the skipped first line and its groups of three.

    After the first line, words are read three at a time (bitmap, name,
    flag). Reading stops at the end of the file (``end`` ``"eof"``), at the
    0x1A byte standing as a group's first word (``"mark"``), at a group of
    fewer than three words (``"short"``) or at a flag that is not a whole
    number (``"bad_number"``); the words from there on are ``unread``.
    ``repeat_of`` names the first group with the same image name. Raises
    only as ``load`` does. Does not check that a bitmap exists.
    """
    data, label = load(source)
    first, words = split_words(data, skip_first_line=True)
    logger.debug("image list first line (skipped): %r", first and first.text)
    groups: list[ImageGroup] = []
    seen: dict[str, int] = {}
    at = 0
    while True:
        end = _word_end(words, at, 3, mark=True)
        if end is None:
            flag = whole_number(words[at + 2].text)
            if flag is None:
                end = "bad_number"
        if end is not None:
            break
        bitmap, name, flag_word = words[at : at + 3]
        group = ImageGroup(
            bitmap, name, flag_word, flag, _repeat(seen, name.text, len(groups))
        )
        logger.debug(
            "image group line %d: %r %r %d", bitmap.line, bitmap.text, name.text, flag
        )
        groups.append(group)
        at += 3
    images = ImageList(
        size=len(data),
        end_mark=has_end_mark(data),
        first_line=first,
        groups=groups,
        end=end,
        unread=words[at:],
    )
    logger.info("read images %s: %d groups, end %s", label, len(groups), end)
    return images


def read_ships(source: Source) -> ShipList:
    """Return the ship list: pairs of model file and craft type, from word 1.

    No line is skipped. Reading stops at the first pair that is not a word
    and a whole number: ``end`` is ``"eof"``, ``"mark"`` (the 0x1A byte as
    a pair's first word), ``"short"`` or ``"bad_number"``; the words from
    there on are ``unread``. Raises only as ``load`` does. Does not apply
    the ``opt`` rule, the 63-character cut or the type table.
    """
    data, label = load(source)
    _, words = split_words(data, skip_first_line=False)
    pairs: list[ShipPair] = []
    at = 0
    while True:
        end = _word_end(words, at, 2, mark=True)
        if end is None:
            value = whole_number(words[at + 1].text)
            if value is None:
                end = "bad_number"
        if end is not None:
            break
        pair = ShipPair(words[at], words[at + 1], value)
        logger.debug(
            "ship pair line %d: %r %d", pair.model.line, pair.model.text, value
        )
        pairs.append(pair)
        at += 2
    ships = ShipList(
        size=len(data),
        end_mark=has_end_mark(data),
        pairs=pairs,
        end=end,
        unread=words[at:],
    )
    logger.info("read ships %s: %d pairs, end %s", label, len(pairs), end)
    return ships


def read_sounds(source: Source) -> SoundList:
    """Return the sound list: the skipped first line and its pairs.

    After the first line, words are read two at a time (WAV file, sound
    name). ``end`` is ``"eof"`` or ``"short"`` (a word without its
    partner); this list has no end-mark rule, so a lone 0x1A byte is such a
    word (``end_mark`` still notes it). The words from there on are
    ``unread``. ``repeat_of`` names the first pair with the same
    sound name. Raises only as ``load`` does. Does not check that a WAV
    exists.
    """
    data, label = load(source)
    first, words = split_words(data, skip_first_line=True)
    logger.debug("sound list first line (skipped): %r", first and first.text)
    pairs: list[SoundPair] = []
    seen: dict[str, int] = {}
    at = 0
    while (end := _word_end(words, at, 2, mark=False)) is None:
        wav, name = words[at : at + 2]
        pairs.append(SoundPair(wav, name, _repeat(seen, name.text, len(pairs))))
        logger.debug("sound pair line %d: %r %r", wav.line, wav.text, name.text)
        at += 2
    sounds = SoundList(
        size=len(data),
        end_mark=has_end_mark(data),
        first_line=first,
        pairs=pairs,
        end=end,
        unread=words[at:],
    )
    logger.info("read sounds %s: %d pairs, end %s", label, len(pairs), end)
    return sounds


# ---- counted line lists ---------------------------------------------------


class _Lines:
    """A cursor over lines that skips ``//`` lines before a value."""

    def __init__(self, lines: list[TextLine]) -> None:
        self.lines = lines
        self.at = 0

    def value(self, comments: list[TextLine]) -> TextLine | None:
        """Skip comment lines into ``comments``; return the next line or None."""
        while self.at < len(self.lines) and _is_comment(self.lines[self.at]):
            comments.append(self.lines[self.at])
            self.at += 1
        if self.at >= len(self.lines):
            return None
        self.at += 1
        return self.lines[self.at - 1]

    def rest(self) -> list[TextLine]:
        return self.lines[self.at :]


def _count(lines: list[TextLine]) -> tuple[TextLine | None, int]:
    first = lines[0] if lines else None
    return first, atoi(first.text) if first else 0


def read_cutscenes(source: Source) -> CutsceneList:
    """Return the cutscene list: its count line and the entries read.

    Line 1 is the count (``atoi``). Each entry is four values, ``//`` lines
    skipped (and kept in ``comments``) before each. Reading stops after
    count entries (``end`` ``"count"``), when the file ends (``"eof"``; the
    entry it ends inside has ``status`` ``"cut"``), or at a numbers line
    without three numbers (``"bad_numbers"``, that entry's ``status`` too);
    the lines from there on are ``unread``. Raises only as ``load`` does.
    Does not cut names to the game's lengths.
    """
    data, label = load(source)
    lines = split_lines(data)
    count_line, count = _count(lines)
    cursor = _Lines(lines)
    cursor.at = 1 if lines else 0
    entries: list[Cutscene] = []
    end = "count"
    while len(entries) < count:
        comments: list[TextLine] = []
        values: list[TextLine | None] = []
        status = "read"
        for slot in range(4):
            line = cursor.value(comments)
            values.append(line)
            if line is None:
                status = "cut"
                break
            if slot == 1 and len(scan_numbers(line.text, CUTSCENE_NUMBERS)) < 3:
                status = "bad_numbers"
                break
        values += [None] * (4 - len(values))
        movie, numbers_line, thumbnail, description = values
        if movie is None and not comments:
            end = "eof"
            break
        entry = Cutscene(
            comments=comments,
            movie=movie,
            numbers_line=numbers_line,
            numbers=scan_numbers(numbers_line.text, 3) if numbers_line else [],
            thumbnail=thumbnail,
            description=description,
            status=status,
        )
        entries.append(entry)
        logger.debug("cutscene %d: %s %s", len(entries) - 1, status, entry.numbers)
        if status != "read":
            end = "eof" if status == "cut" else status
            break
    cutscenes = CutsceneList(
        size=len(data),
        end_mark=has_end_mark(data),
        count_line=count_line,
        count=count,
        entries=entries,
        end=end,
        unread=cursor.rest(),
    )
    logger.info("read cutscenes %s: %d entries, end %s", label, len(entries), end)
    return cutscenes


def read_awards(source: Source) -> AwardList:
    """Return the campaign medal list: its count line and the records read.

    Line 1 is the count (``atoi``). Each record is 32 values, ``//`` lines
    skipped (and kept) before each: campaign id (``atoi``), main medal, 15
    multiplayer and 15 single-player names. Reading stops after count
    records (``end`` ``"count"``) or when the file ends (``"eof"``; a record
    it ends inside has ``complete`` False); the lines from there on are
    ``unread``. Raises only as ``load`` does. Does not cut names.
    """
    data, label = load(source)
    lines = split_lines(data)
    count_line, count = _count(lines)
    cursor = _Lines(lines)
    cursor.at = 1 if lines else 0
    records: list[AwardRecord] = []
    end = "count"
    while len(records) < count:
        comments: list[TextLine] = []
        values: list[TextLine] = []
        while len(values) < 2 + 2 * AWARD_NAMES:
            line = cursor.value(comments)
            if line is None:
                break
            values.append(line)
        if not values and not comments:
            end = "eof"
            break
        record = AwardRecord(
            comments=comments,
            campaign_line=values[0] if values else None,
            campaign=atoi(values[0].text) if values else None,
            main=values[1] if len(values) > 1 else None,
            multiplayer=values[2 : 2 + AWARD_NAMES],
            singleplayer=values[2 + AWARD_NAMES :],
            complete=len(values) == 2 + 2 * AWARD_NAMES,
        )
        records.append(record)
        logger.debug("award record %d: campaign %s", len(records) - 1, record.campaign)
        if not record.complete:
            end = "eof"
            break
    awards = AwardList(
        size=len(data),
        end_mark=has_end_mark(data),
        count_line=count_line,
        count=count,
        records=records,
        end=end,
        unread=cursor.rest(),
    )
    logger.info("read awards %s: %d records, end %s", label, len(records), end)
    return awards


READERS = {
    "menu": read_menu,
    "sequence": read_sequence,
    "images": read_images,
    "ships": read_ships,
    "sounds": read_sounds,
    "cutscenes": read_cutscenes,
    "awards": read_awards,
}
"""Each kind's reader, by kind name."""


def read_list(kind: str, source: Source) -> ListFile:
    """Return ``READERS[kind](source)``.

    Raises ``ValueError`` for an unknown kind, otherwise only as the kind's
    reader does. Does not guess the kind from the file.
    """
    if kind not in READERS:
        raise ValueError(f"unknown list kind {kind!r}; one of {sorted(READERS)}")
    return READERS[kind](source)
