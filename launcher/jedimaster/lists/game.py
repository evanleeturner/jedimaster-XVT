"""The game's view of each list: what the game keeps after reading it.

Purpose:
    Derive, from a reader's result, the values the game keeps, by the rules
    of the notes: menus lowercased with markers resolved into availability;
    sequence descriptions filtered; image and sound tables sorted and
    de-duplicated, missing bitmaps skipped; the ship table and its 18 type
    slots; cutscene and medal names cut to the game's lengths.

Flow:
    ``menu_view``, ``sequence_view``, ``images_view``, ``ships_view``,
    ``sounds_view``, ``cutscenes_view``, ``awards_view``: each takes the
    reader's dataclass plus what the game takes besides the file (mission
    type and view, flown missions, existing bitmaps, loadable sounds) and
    returns a ``Game*`` dataclass. ``MISSION_TYPES`` and ``menu_game_path``
    name the menu file the game reads for each mission type and view.

Invariants:
    - Pure: no file is read here; the caller passes what exists.
    - Raw values in the reader's result are never changed; the view is a new
      object.
    - Tables sorted "by name" sort by the names' bytes (Latin-1 code
      points), as C's ``strcmp`` does.

Call:
    ``menu_view(read_menu(path), "training", "rebel", flown={"rebel": {71}})``
"""

from __future__ import annotations

import logging
from collections.abc import Collection
from collections.abc import Mapping
from dataclasses import dataclass

from .model import AwardList
from .model import CutsceneList
from .model import ImageList
from .model import MenuEntry
from .model import MenuList
from .model import MenuSection
from .model import SequenceList
from .model import ShipList
from .model import SoundList
from .text import ascii_lower
from .text import TextLine

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class MissionType:
    """One mission type: its number, folder, and which menus it reads."""

    name: str
    number: int
    folder: str
    sided: bool
    plays: str | None


MISSION_TYPES: dict[str, MissionType] = {
    t.name: t
    for t in (
        MissionType("training", 0, "Train", True, None),
        MissionType("melee", 1, "Melee", False, None),
        MissionType("tournament", 2, "Tourn", False, "melee"),
        MissionType("combat", 3, "Combat", True, None),
        MissionType("battle", 4, "Battle", False, "combat"),
        MissionType("campaign", 5, "CAMPAIGN", True, "training"),
    )
}
"""The six mission types: ``sided`` ones give a solo pilot his side's menu;
``plays`` names the menu a sequence of this type plays missions from."""

VIEWS = ("network", "rebel", "imperial")
"""A network game, or a solo pilot on one side."""

NETWORK_MENU = "mission.lst"
DESCRIPTION_LIMIT = 4095
TYPE_LIMIT = 17
SHIP_NAME_LIMIT = 63
SHIP_SUFFIX = "opt"
TYPE_START = (0, 0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 8, 0, 9, 0)
"""The type-to-ship table's starting values, slot 0 first; a slot no kept
ship names keeps its starting value."""
COMPRESS_FLAG = 1
"""The only flag value that asks the game to compress an image."""
MOVIE_LIMIT = 127
THUMBNAIL_LIMIT = 31
AWARD_NAME_LIMIT = 31
AWARD_SLOTS = 16


def _mission_type(name: str) -> MissionType:
    if name not in MISSION_TYPES:
        raise ValueError(f"unknown mission type {name!r}; one of {list(MISSION_TYPES)}")
    return MISSION_TYPES[name]


def menu_file(mission_type: str, view: str) -> str:
    """Return the menu file the game reads for ``mission_type`` in ``view``.

    ``"mission.lst"`` for a network game and for a solo pilot of an unsided
    type; ``"rebel.lst"`` or ``"imperial.lst"`` for a solo pilot of a sided
    type. Raises ``ValueError`` for an unknown type or view.
    """
    kind = _mission_type(mission_type)
    if view not in VIEWS:
        raise ValueError(f"unknown view {view!r}; one of {list(VIEWS)}")
    if view == "network" or not kind.sided:
        return NETWORK_MENU
    return f"{view}.lst"


def menu_game_path(mission_type: str, view: str) -> str:
    """Return the game path of that menu: lowercase folder, backslash, file.

    Raises ``ValueError`` as ``menu_file`` does. Does not look at an install.
    """
    folder = _mission_type(mission_type).folder.lower()
    return f"{folder}\\{menu_file(mission_type, view)}"


def sequence_game_path(mission_type: str, file_name: str) -> str:
    """Return the game path of a sequence file a menu entry names.

    The menu's folder (lowercase), a backslash, then ``file_name`` as given
    (the game's view of an entry is already lowercased). Raises
    ``ValueError`` for an unknown type. Does not check that the type plays
    sequences.
    """
    return f"{_mission_type(mission_type).folder.lower()}\\{file_name}"


# ---- mission menus --------------------------------------------------------


@dataclass
class GameMenuEntry:
    """One entry the game keeps: section, id, availability, file, title."""

    section: str
    id: int
    available: bool
    file: str
    title: str
    line: int


@dataclass
class GameMenu:
    """The game's menu: which file it read and the entries it kept."""

    mission_type: str
    view: str
    game_path: str
    entries: list[GameMenuEntry]


def _flown_ids(view: str, flown: Mapping[str, Collection[int]] | None) -> set[int]:
    flown = flown or {}
    if view == "network":
        return {i for side in ("rebel", "imperial") for i in flown.get(side, ())}
    return set(flown.get(view, ()))


def _menu_entry(item: MenuEntry, section: str, flown: set[int]) -> GameMenuEntry:
    name = ascii_lower(item.file_name or "")
    if item.marker == "&":
        available = False
    elif item.marker == "*":
        available = item.id in flown
    else:
        available = True
    assert item.title_line is not None
    return GameMenuEntry(
        section=section,
        id=item.id,
        available=available,
        file=name,
        title=item.title_line.text,
        line=item.id_line.line,
    )


def menu_view(
    menu: MenuList,
    mission_type: str,
    view: str,
    flown: Mapping[str, Collection[int]] | None = None,
) -> GameMenu:
    """Return the game's view of a menu read for ``mission_type`` in ``view``.

    ``view`` is ``"network"``, or the solo pilot's side (``"rebel"`` or
    ``"imperial"``). ``flown`` maps a side to the campaign mission ids that
    side has flown (default: none). Keeps every complete entry in file order
    with the section in force; drops an entry the file ends inside. The file
    name is lowercased (A to Z only) with its marker removed; ``&`` entries
    are never available, ``*`` entries only when their id was flown by the
    pilot's side (solo) or by either side (network), others always; a ``*``
    line without its name keeps an empty name. Raises ``ValueError`` for an
    unknown type or view. Does not check that the menu read is the file the
    game reads for that view, or that the missions exist.
    """
    game_path = menu_game_path(mission_type, view)
    flown_ids = _flown_ids(view, flown)
    section = ""
    entries: list[GameMenuEntry] = []
    for item in menu.items:
        if isinstance(item, MenuSection):
            section = item.name
        elif isinstance(item, MenuEntry) and item.complete:
            entries.append(_menu_entry(item, section, flown_ids))
            logger.debug("menu keeps %s", entries[-1])
    logger.info("menu %s (%s): %d entries", game_path, view, len(entries))
    return GameMenu(mission_type, view, game_path, entries)


# ---- sequence files -------------------------------------------------------


@dataclass
class GameSequence:
    """The game's sequence: lines as read, count, and the description."""

    count_line: str
    count: int
    missions: list[str]
    description: str


def as_read(line: TextLine | None) -> str:
    """Return a line as the game's line reading returns it.

    The text, then a line feed when the line had one (a carriage return is
    dropped). Returns ``""`` for no line.
    """
    if line is None:
        return ""
    return line.text + ("\n" if line.ending.endswith("\n") else "")


def filter_description(text: str) -> str:
    """Return the setup screen's text: printable characters and line feeds.

    Keeps characters 32 to 126 and the line feed, drops every other
    character (carriage returns included), then keeps at most 4,095.
    Always returns a string, maybe empty; does not wrap or trim lines.
    """
    kept = "".join(c for c in text if c == "\n" or 32 <= ord(c) <= 126)
    return kept[:DESCRIPTION_LIMIT]


def sequence_view(sequence: SequenceList) -> GameSequence:
    """Return the game's view of a sequence file.

    The count line and each mission line as read (``as_read``), the count,
    and the filtered description. Does not match missions to a menu.
    """
    view = GameSequence(
        count_line=as_read(sequence.count_line),
        count=sequence.count,
        missions=[as_read(line) for line in sequence.missions],
        description=filter_description(sequence.description),
    )
    logger.info(
        "sequence: %d missions, %d description characters",
        len(view.missions),
        len(view.description),
    )
    return view


# ---- image lists ----------------------------------------------------------


@dataclass(frozen=True)
class Bitmap:
    """An existing bitmap: its size, and whether the game's coding shrinks it.

    ``compresses`` says whether the game's in-memory run-length coding of
    the bitmap's pixels comes out smaller; only the pixels decide it, and
    this package reads only headers, so it is True unless the caller knows
    better.
    """

    width: int
    height: int
    compresses: bool = True


@dataclass
class GameImage:
    """One image in the game's table."""

    name: str
    bitmap: str
    width: int
    height: int
    flag: int
    compressed: bool
    group: int


@dataclass
class GameImages:
    """The game's image table and how the read went."""

    result: int
    end: str
    images: list[GameImage]
    registered: list[int]
    repeats: list[int]
    missing: list[int]


def images_view(images: ImageList, bitmaps: Mapping[str, Bitmap]) -> GameImages:
    """Return the game's image table after reading ``images``.

    ``bitmaps`` maps each bitmap word as written to its ``Bitmap``; a word
    not in it is a missing bitmap. Groups are taken in file order: a name
    already registered is skipped (``repeats``), then a missing bitmap is
    skipped (``missing``), else the group registers (``registered``). The
    table is sorted by name. An image is compressed when its flag is 1 (0
    and every other value do not ask) and its bitmap ``compresses``. ``result`` is 1 when the read ended at
    the end of the file, 0 for the 0x1A mark, a short group or a bad flag.
    Does not read any file.
    """
    table: dict[str, GameImage] = {}
    registered: list[int] = []
    repeats: list[int] = []
    missing: list[int] = []
    for index, group in enumerate(images.groups):
        name = group.name.text
        bitmap = bitmaps.get(group.bitmap.text)
        if name in table:
            repeats.append(index)
            logger.debug("image %r repeats; group %d skipped", name, index)
            continue
        if bitmap is None:
            missing.append(index)
            logger.debug(
                "bitmap %r missing; group %d skipped", group.bitmap.text, index
            )
            continue
        table[name] = GameImage(
            name=name,
            bitmap=group.bitmap.text,
            width=bitmap.width,
            height=bitmap.height,
            flag=group.flag_value,
            compressed=group.flag_value == COMPRESS_FLAG and bitmap.compresses,
            group=index,
        )
        registered.append(index)
    result = 1 if images.end == "eof" else 0
    ordered = _by_name(table)
    logger.info("images: %d kept, result %d, end %s", len(ordered), result, images.end)
    return GameImages(result, images.end, ordered, registered, repeats, missing)


def _by_name(table: dict) -> list:
    """Return the table's values sorted by their names' bytes."""
    return [table[name] for name in sorted(table, key=lambda n: n.encode("latin-1"))]


# ---- ship list ------------------------------------------------------------


@dataclass
class GameShip:
    """One ship the game keeps: model name, craft type, source pair."""

    model: str
    type: int
    pair: int


@dataclass
class GameShips:
    """The game's ships and its type-to-ship table."""

    ships: list[GameShip]
    type_to_ship: list[int]


def ships_view(ships: ShipList) -> GameShips:
    """Return the ships the game keeps and its 18-slot type table.

    A pair is kept when its model ends in ``opt`` (any case); the kept name
    has those three letters lowercased, then is cut to 63 characters. Each
    kept ship whose type is 0 to 16 puts its position among kept ships in
    slot ``type``, a later one overwriting; other types fill no slot; the
    slots start at ``TYPE_START`` and a slot no kept ship names keeps its
    starting value. Does not check that a model exists.
    """
    kept: list[GameShip] = []
    table = list(TYPE_START)
    for index, pair in enumerate(ships.pairs):
        text = pair.model.text
        if ascii_lower(text[-3:]) != SHIP_SUFFIX:
            logger.debug("ship pair %d skipped: %r", index, text)
            continue
        name = (text[:-3] + ascii_lower(text[-3:]))[:SHIP_NAME_LIMIT]
        if 0 <= pair.type_value < TYPE_LIMIT:
            table[pair.type_value] = len(kept)
        kept.append(GameShip(name, pair.type_value, index))
    logger.info("ships: %d kept of %d pairs", len(kept), len(ships.pairs))
    return GameShips(kept, table)


# ---- sound list -----------------------------------------------------------


@dataclass
class GameSound:
    """One sound in the game's table."""

    name: str
    wav: str
    pair: int


@dataclass
class GameSounds:
    """The game's sound table, every pair it tried, and how the read went."""

    result: int
    end: str
    sounds: list[GameSound]
    attempts: list[int]
    repeats: list[int]
    failed: list[int]


def sounds_view(
    sounds: SoundList, loadable: Collection[str] | None = None
) -> GameSounds:
    """Return the game's sound table after reading ``sounds``.

    ``loadable`` holds the WAV words (as written) that load; None means
    every WAV loads. Pairs in file order: a name already loaded is skipped
    (``repeats``), a WAV that does not load is skipped (``failed``); every
    other pair loads. ``attempts`` lists every pair the game tried (all
    but repeats). The table is sorted by name. ``result`` is 0 when a word
    was left without its partner (a lone 0x1A byte included: this list has
    no end-mark rule), else 1; the pairs before it load either way. Does not read any file.
    """
    table: dict[str, GameSound] = {}
    attempts: list[int] = []
    repeats: list[int] = []
    failed: list[int] = []
    for index, pair in enumerate(sounds.pairs):
        name = pair.name.text
        if name in table:
            repeats.append(index)
            continue
        attempts.append(index)
        if loadable is not None and pair.wav.text not in loadable:
            failed.append(index)
            logger.debug("sound %r: %r does not load", name, pair.wav.text)
            continue
        table[name] = GameSound(name, pair.wav.text, index)
    result = 0 if sounds.end == "short" else 1
    ordered = _by_name(table)
    logger.info(
        "sounds: %d kept, %d failed, result %d", len(ordered), len(failed), result
    )
    return GameSounds(result, sounds.end, ordered, attempts, repeats, failed)


# ---- cutscene list --------------------------------------------------------


@dataclass
class GameCutscene:
    """One cutscene the game keeps."""

    movie: str
    campaign: int
    after_debriefing: int
    mission: int
    thumbnail: str
    description: str


@dataclass
class GameCutscenes:
    """The game's cutscene table."""

    result: int
    cutscenes: list[GameCutscene]


def _text(line: TextLine | None, limit: int) -> str:
    return line.text[:limit] if line is not None else ""


def cutscenes_view(cutscenes: CutsceneList) -> GameCutscenes:
    """Return the cutscenes the game keeps.

    Only entries read in full count; one the file ends inside or with a bad
    numbers line is not kept. Movie and description are cut to 127
    characters, the thumbnail to 31. ``result`` is 0 only when the file has no
    first line (an empty file), else 1. Does not check the movies exist.
    """
    kept = []
    for entry in cutscenes.entries:
        if entry.status != "read":
            continue
        campaign, phase, mission = entry.numbers
        kept.append(
            GameCutscene(
                movie=_text(entry.movie, MOVIE_LIMIT),
                campaign=campaign,
                after_debriefing=phase,
                mission=mission,
                thumbnail=_text(entry.thumbnail, THUMBNAIL_LIMIT),
                description=_text(entry.description, MOVIE_LIMIT),
            )
        )
    logger.info("cutscenes: %d kept", len(kept))
    result = 0 if cutscenes.count_line is None else 1
    return GameCutscenes(result, kept)


# ---- campaign medal list --------------------------------------------------


@dataclass
class GameAward:
    """One campaign's medals: main medal and two rows of 16 slots."""

    campaign: int
    main: str
    multiplayer: list[str]
    singleplayer: list[str]


@dataclass
class GameAwards:
    """The game's medal table."""

    result: int
    awards: list[GameAward]


def _slots(lines: list[TextLine]) -> list[str]:
    names = [line.text[:AWARD_NAME_LIMIT] for line in lines]
    return names + [""] * (AWARD_SLOTS - len(names))


def awards_view(awards: AwardList) -> GameAwards:
    """Return the medal records the game keeps.

    Only complete records count. Each name is cut to 31 characters; each row
    has 16 slots, the 16th always empty. ``result`` is 0 only when the file has no
    first line (an empty file), else 1. Does not check the images exist.
    """
    kept = []
    for record in awards.records:
        if not record.complete:
            continue
        assert record.campaign is not None and record.main is not None
        kept.append(
            GameAward(
                campaign=record.campaign,
                main=record.main.text[:AWARD_NAME_LIMIT],
                multiplayer=_slots(record.multiplayer),
                singleplayer=_slots(record.singleplayer),
            )
        )
    logger.info("awards: %d kept", len(kept))
    result = 0 if awards.count_line is None else 1
    return GameAwards(result, kept)
