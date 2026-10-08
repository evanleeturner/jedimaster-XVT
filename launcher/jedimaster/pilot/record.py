"""Read a pilot file's bytes into a record laid out by the layout's structs.

Purpose:
    Turn the bytes of a ``.pl2`` (the full record) or a ``.plt`` (the base
    record) into a record: each member by its name, arrays as nested
    lists, numbers as Python integers, byte fields as ``bytes`` and text
    fields as ``Text``, which keeps all the field's bytes and gives its text
    up to the first byte 0. Refuse a file whose size is not its record's.

Flow:
    ``read_pilot_file`` picks the record by the file's suffix (``kind_of``)
    and calls ``read_record``, which checks the size and calls
    ``read_struct``; ``read_struct`` walks the struct's members, reading
    each array whole and nesting it by its dimensions. ``locate`` gives a
    member's offset by its path, for code that writes into a record's
    bytes.

Invariants:
    - The layout is the only source of offsets, types and sizes.
    - Numbers are little-endian, ``i32`` signed, ``u32`` unsigned.
    - A ``u8`` or ``char`` array's last dimension is one ``bytes`` (or
      ``Text``) value; any dimensions before it nest as lists.
    - Nothing is dropped: a record's values cover every byte of its struct.

Call:
    ``record = read_record(data, FULL_RECORD); record["rating"]``
"""

from __future__ import annotations

import logging
import os
import struct as binary
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .layout import BASE_RECORD
from .layout import CHAR
from .layout import count
from .layout import element_size
from .layout import FULL_RECORD
from .layout import I32
from .layout import is_struct
from .layout import Member
from .layout import struct
from .layout import U8
from .layout import U32

logger = logging.getLogger(__name__)

SUFFIXES = {".pl2": FULL_RECORD, ".plt": BASE_RECORD}
"""Each pilot file suffix (any letter case) and the record it holds."""
NUMBER_FORMATS = {I32: "i", U32: "I"}


class PilotFormatError(ValueError):
    """A pilot file that cannot be read as its record: wrong size or kind."""


@dataclass(frozen=True)
class Text:
    """A text field: all its bytes; ``text`` is them up to the first byte 0."""

    raw: bytes

    @property
    def text(self) -> bytes:
        """Return the bytes before the first byte 0, or all of them.

        Does not decode the bytes.
        """
        end = self.raw.find(0)
        return self.raw if end < 0 else self.raw[:end]

    @property
    def ended(self) -> bool:
        """Return True when the field holds a byte 0 (its text ends inside).

        False when every byte is other than 0. Does not look at the text.
        """
        return 0 in self.raw


def nest(flat: list[Any], dims: tuple[int, ...]) -> Any:
    """Return ``flat`` as nested lists of ``dims``, the last index fastest.

    One value for no dimensions, else lists of lists. Does not check that
    ``flat`` holds the product of ``dims`` values.
    """
    if not dims:
        return flat[0]
    if len(dims) == 1:
        return list(flat)
    step = len(flat) // dims[0]
    return [nest(flat[i * step : (i + 1) * step], dims[1:]) for i in range(dims[0])]


def _member(member: Member, data: bytes, base: int) -> Any:
    start = base + member.offset
    if member.type in (U8, CHAR):
        run = member.dims[-1] if member.dims else 1
        chunks = [
            bytes(data[at : at + run]) for at in range(start, start + member.size, run)
        ]
        if member.type == CHAR:
            chunks = [Text(chunk) for chunk in chunks]
        if not member.dims:
            return chunks[0][0] if member.type == U8 else chunks[0]
        return nest(chunks, member.dims[:-1]) if len(member.dims) > 1 else chunks[0]
    if member.type in NUMBER_FORMATS:
        values = binary.unpack_from(
            f"<{count(member)}{NUMBER_FORMATS[member.type]}", data, start
        )
        return nest(list(values), member.dims)
    size = element_size(member)
    elements = [
        read_struct(member.type, data, start + i * size) for i in range(count(member))
    ]
    return nest(elements, member.dims)


def read_struct(name: str, data: bytes, offset: int = 0) -> dict[str, Any]:
    """Return the struct ``name`` read from ``data`` at ``offset``.

    A dict of every member by name, in layout order: numbers as integers,
    arrays as nested lists, struct members as dicts. Raises ``KeyError``
    for an unknown struct and ``struct.error`` when ``data`` is too short.
    Does not check that ``data`` ends where the struct does.
    """
    values: dict[str, Any] = {}
    for member in struct(name).members:
        value = _member(member, data, offset)
        if not is_struct(member.type):
            logger.debug(
                "%s.%s at %d: %r", name, member.name, offset + member.offset, value
            )
        values[member.name] = value
    return values


def read_record(data: bytes, name: str) -> dict[str, Any]:
    """Return the whole record ``name`` (``FULL_RECORD`` or ``BASE_RECORD``).

    Raises ``PilotFormatError`` when ``data`` is not exactly the record's
    size. Does not check any value.
    """
    size = struct(name).size
    if len(data) != size:
        raise PilotFormatError(f"{name} is {size} bytes, the data is {len(data)}")
    record = read_struct(name, data)
    logger.debug("read %s: %d members", name, len(record))
    return record


def kind_of(path: str | os.PathLike[str]) -> str | None:
    """Return the record a pilot file holds by its suffix, or None.

    ``FULL_RECORD`` for ``.pl2``, ``BASE_RECORD`` for ``.plt``, any letter
    case. Does not read the file.
    """
    return SUFFIXES.get(Path(path).suffix.casefold())


def read_pilot_file(path: str | os.PathLike[str]) -> tuple[str, dict[str, Any]]:
    """Return a pilot file's record name and its record, read as its kind.

    Raises ``PilotFormatError`` for a name that is neither ``.pl2`` nor
    ``.plt`` or a file of the wrong size, ``OSError`` when it cannot be
    read. Does not check any value.
    """
    name = kind_of(path)
    if name is None:
        raise PilotFormatError(f"{path}: not a .pl2 or .plt file")
    data = Path(path).read_bytes()
    try:
        record = read_record(data, name)
    except PilotFormatError as exc:
        raise PilotFormatError(f"{path}: {exc}") from None
    logger.info("read %s as %s (%d bytes)", path, name, len(data))
    return name, record


def locate(name: str, path: tuple[str | int, ...]) -> tuple[int, Member]:
    """Return the offset of a member (or one element of it) and its member.

    ``path`` alternates member names and indexes: ``("faction_statistics",
    1, "mission_description_ids", 0)`` is that side record's first id.
    Indexes count elements of an array, the last index fastest. Raises
    ``KeyError`` for an unknown name and ``IndexError`` for an index
    outside its array. Does not read any data.
    """
    layout = struct(name)
    offset = 0
    member: Member | None = None
    at = 0
    while at < len(path):
        member = next((m for m in layout.members if m.name == path[at]), None)
        if member is None:
            raise KeyError(f"{layout.name} has no member {path[at]!r}")
        offset += member.offset
        at += 1
        indexes = []
        while at < len(path) and isinstance(path[at], int):
            indexes.append(path[at])
            at += 1
        offset += _element(member, indexes) * element_size(member)
        if is_struct(member.type):
            layout = struct(member.type)
    assert member is not None, "an empty path names no member"
    return offset, member


def _element(member: Member, indexes: list[int]) -> int:
    flat = 0
    for depth, index in enumerate(indexes):
        if not 0 <= index < member.dims[depth]:
            raise IndexError(f"{member.name}: index {index} of {member.dims[depth]}")
        flat = flat * member.dims[depth] + index
    for size in member.dims[len(indexes) :]:
        flat *= size
    return flat
