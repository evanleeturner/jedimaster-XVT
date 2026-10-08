"""Load a pilot from its two files the way the game does, and say what it logged.

Purpose:
    Give the full record the game holds after loading a pilot by its
    ``.plt`` name from the player's folder, the load's result (1 loaded,
    0 failed) and the lines the game logs on the way.

Flow:
    ``load_pilot`` finds ``<name>.plt`` and its ``.pl2`` (the name with its
    last letter made ``2``) in a folder and calls ``load_files``:
    1. the full record becomes 0;
    2. a ``.pl2`` of the full size becomes the full record (a shorter one
       fails the load, the record 0 again);
    3. with a ``.plt``: when there was no ``.pl2`` a new pilot's defaults
       go in first; the base record, when whole, is merged (a shorter one
       fails the load and the record keeps what it held); when there was
       no ``.pl2`` the network game's names are built from the pilot's
       name and the menus' entry;
    4. with neither file the load fails; a ``.pl2`` alone loads with a
       warning;
    5. after a merge, a rating above 24 or a side of 4 or more is logged as
       out of range and kept.

Invariants:
    - ``events`` holds the game's log lines of the load, in order, without
      the program's prefix; the same lines go to this module's logger,
      the warnings at WARNING.
    - A text is copied up to its first byte 0; one the game would run past
      its field is cut to the field (its size less one, then a byte 0),
      with a WARNING.
    - A file longer than its record is read for its record's size.

Call:
    ``load = load_pilot(folder, "Host0.plt", defaults); load.result``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
from typing import Any

from ..install import child_in_any_case
from ..text.sheet import quote
from .defaults import Defaults
from .layout import BASE_RECORD
from .layout import FULL_RECORD
from .layout import struct
from .merge import merge
from .record import locate
from .record import read_struct

logger = logging.getLogger(__name__)

PLT = ".plt"
LAST_RATING = 24
"""The highest rating: Jedi Master."""
SIDE_COUNT = 4
"""The side records: a ``current_faction_id`` of this or more is out of range."""
NAME = ("name",)
RATING = ("rating",)
RATING_NAME = ("rating_name",)
GAME_NAMES = (("multiplayer_game_name",), ("multiplayer_host_name",))
TRAINING_SLOTS = (
    ("mission_description_ids", 0),
    ("faction_statistics", 0, "mission_description_ids", 0),
    ("faction_statistics", 1, "mission_description_ids", 0),
    ("faction_statistics", 2, "mission_description_ids", 0),
)
"""Where a new pilot's first missions go: Rebel, Rebel, Imperial, network."""
TRAINING_SOURCES = (0, 0, 1, 2)
"""Which of ``Defaults.training`` each of ``TRAINING_SLOTS`` takes."""
LOADED_FIELDS = (
    ("rating", "rating"),
    ("missions", "total_missions_played_count"),
    ("score", "total_score"),
    ("faction", "current_faction_id"),
    ("promo", "current_rating_promo_points"),
    ("percent", "next_promotion_percent"),
    ("rank_change", "promotion_delta"),
)
"""The fields of the ``pilot.record_loaded`` line and the members they show."""


@dataclass(frozen=True)
class Event:
    """One line the game logs: its level and its text without the prefix."""

    level: int
    text: str


@dataclass
class PilotLoad:
    """What a load leaves: which files were there, the result, the record."""

    name: str
    pl2: bool
    plt: bool
    result: int
    data: bytes
    events: list[Event] = field(default_factory=list)

    @property
    def record(self) -> dict[str, Any]:
        """Return the full record read from ``data``, a new dict each call.

        Does not cache the record or check any value.
        """
        return read_struct(FULL_RECORD, self.data)


def pl2_name(plt_name: str) -> str:
    """Return the ``.pl2`` name of a ``.plt`` name: its last letter made ``2``.

    Returns ``plt_name`` with its last character replaced. Does not check
    the suffix; an empty name gives ``"2"``.
    """
    return plt_name[:-1] + "2"


def _event(events: list[Event], level: int, fmt: str, *args: Any) -> None:
    events.append(Event(level, fmt % args))
    logger.log(level, fmt, *args)


def _put(full: bytearray, path: tuple, value: int) -> None:
    offset, _ = locate(FULL_RECORD, path)
    full[offset : offset + 4] = value.to_bytes(4, "little", signed=True)
    logger.debug("%s at %d = %d", path, offset, value)


def _number(full: bytearray, path: tuple) -> int:
    offset, _ = locate(FULL_RECORD, path)
    return int.from_bytes(full[offset : offset + 4], "little", signed=True)


def c_text(data: bytes) -> bytes:
    """Return ``data`` up to its first byte 0, as a copy of a C string takes it.

    All of ``data`` when it holds no byte 0. Does not log.
    """
    end = data.find(0)
    return data if end < 0 else data[:end]


def _put_text(full: bytearray, path: tuple, text: bytes) -> None:
    offset, member = locate(FULL_RECORD, path)
    text = c_text(text)
    if len(text) >= member.size:
        logger.warning(
            "%s: %d bytes run past its %d; cut to %d",
            path[0],
            len(text),
            member.size,
            member.size - 1,
        )
        text = text[: member.size - 1]
    full[offset : offset + member.size] = text.ljust(member.size, b"\0")
    logger.debug("%s at %d = %r", path, offset, text)


def _pilot_name(full: bytearray) -> bytes:
    offset, member = locate(FULL_RECORD, NAME)
    raw = bytes(full[offset : offset + member.size])
    if 0 not in raw:
        logger.warning("name fills its %d bytes with no byte 0: cut there", len(raw))
    return c_text(raw)


def set_defaults(full: bytearray, defaults: Defaults, events: list[Event]) -> None:
    """Write a new pilot's choices into the full record's bytes.

    ``rating``, ``rating_name`` and the first training missions
    (``TRAINING_SLOTS``); logs ``pilot.defaults_set``. Returns None.
    Does not write the network game's names (``set_game_names`` does).
    """
    _put(full, RATING, defaults.rating)
    _put_text(full, RATING_NAME, defaults.rating_name)
    values = [defaults.training[i] for i in TRAINING_SOURCES]
    for path, value in zip(TRAINING_SLOTS, values, strict=True):
        _put(full, path, value)
    training = ",".join(str(v) for v in values)
    _event(events, logging.INFO, "pilot.defaults_set training=%s", quote(training))


def set_game_names(full: bytearray, defaults: Defaults) -> None:
    """Write the network game's and host's names: the pilot's name and suffix.

    Both are the text of ``name`` followed by ``defaults.game_name_suffix``,
    cut to 31 bytes with a WARNING when longer; a name with no byte 0 in
    its field is taken whole, with a WARNING. Returns None. Does not log
    an event of the game's.
    """
    text = _pilot_name(full) + defaults.game_name_suffix
    for path in GAME_NAMES:
        _put_text(full, path, text)


def _loaded(full: bytearray, events: list[Event]) -> None:
    values = {key: _number(full, (member,)) for key, member in LOADED_FIELDS}
    fields = " ".join(f"{key}={value}" for key, value in values.items())
    _event(events, logging.INFO, "pilot.record_loaded %s", fields)
    rating, faction = values["rating"], values["faction"]
    if rating > LAST_RATING or faction >= SIDE_COUNT:
        _event(
            events,
            logging.WARNING,
            "pilot.record_out_of_range rating=%d faction=%d",
            rating,
            faction,
        )


def load_files(
    name: str, plt: bytes | None, pl2: bytes | None, defaults: Defaults
) -> PilotLoad:
    """Return the load of a pilot whose ``.plt`` and ``.pl2`` hold these bytes.

    ``None`` stands for a file that is not there; ``name`` is the ``.plt``
    name the game logs. Result 1 when the pilot loaded, 0 when neither
    file is there, the ``.pl2`` is short (record all 0) or the ``.plt`` is
    short (record as it was before the merge). Never raises for a file's
    contents. Does not read files or check any value but the out-of-range
    pair.
    """
    events: list[Event] = []
    full = bytearray(struct(FULL_RECORD).size)
    _event(
        events,
        logging.INFO,
        "pilot.load_files path=%s expansion=%d base=%d",
        quote(name),
        pl2 is not None,
        plt is not None,
    )
    load = PilotLoad(name, pl2 is not None, plt is not None, 0, bytes(full), events)
    if pl2 is None and plt is None:
        logger.info("%s: neither file is there: the load fails", name)
        return load
    if pl2 is not None:
        if len(pl2) < len(full):
            logger.info("%s: .pl2 of %d bytes is short: the load fails", name, len(pl2))
            return load
        full[:] = pl2[: len(full)]
    if plt is None:
        _event(events, logging.WARNING, "pilot.record_missing")
        load.result, load.data = 1, bytes(full)
        return load
    if pl2 is None:
        set_defaults(full, defaults, events)
    base_size = struct(BASE_RECORD).size
    if len(plt) < base_size:
        logger.info("%s: .plt of %d bytes is short: the load fails", name, len(plt))
        load.data = bytes(full)
        return load
    merge(full, plt[:base_size])
    if pl2 is None:
        set_game_names(full, defaults)
    _loaded(full, events)
    load.result, load.data = 1, bytes(full)
    return load


def pilot_paths(
    folder: str | os.PathLike[str], name: str
) -> tuple[Path | None, Path | None]:
    """Return the pilot's ``.plt`` and ``.pl2`` files in ``folder``, or None.

    Each name matches in any letter case (an exact match first). Returns
    None for a file that is not there. Does not read the files.
    """
    found = []
    for wanted in (name, pl2_name(name)):
        path = child_in_any_case(Path(folder), wanted)
        found.append(path if path is not None and path.is_file() else None)
    return found[0], found[1]


def plt_name(name: str) -> str:
    """Return the ``.plt`` name a pilot is loaded by.

    ``name`` as it is when it ends in ``.plt`` (any letter case), else
    ``name`` with ``.plt`` added. Does not touch the file system.
    """
    return name if name.casefold().endswith(PLT) else name + PLT


def load_pilot(
    folder: str | os.PathLike[str], name: str, defaults: Defaults
) -> PilotLoad:
    """Return the load of the pilot ``name`` from ``folder``.

    ``name`` is the ``.plt`` name (``plt_name`` adds a missing suffix).
    Reads each file that is there and calls ``load_files``. Raises
    ``OSError`` when a file that is there cannot be read. Does not check
    that ``folder`` exists (a missing folder holds neither file).
    """
    name = plt_name(name)
    plt_path, pl2_path = pilot_paths(folder, name)
    plt = plt_path.read_bytes() if plt_path else None
    pl2 = pl2_path.read_bytes() if pl2_path else None
    load = load_files(name, plt, pl2, defaults)
    logger.info("loaded %s from %s: result %d", name, folder, load.result)
    return load
