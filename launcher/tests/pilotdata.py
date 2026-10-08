"""Build synthetic pilot records, lists and installs for the pilot tests.

Purpose:
    Give the pilot tests bytes laid out from the package's layout data
    without any pilot of the game: records of zeros or of a pattern, a
    value written at a member's path, a value read back, and a fake
    install with a made-up ``fronttxt.txt`` and training lists.

Flow:
    ``offset`` walks the layout's structs by a path of names and indexes
    on its own (it does not use the package's ``locate``); ``record``
    builds a record's bytes from ``{path: value}``; ``value_at`` reads a
    32-bit number back; ``patterned`` fills every 32-bit word of a record
    with a value of its own; ``install`` writes a fake install.

Invariants:
    - No game data: sizes and offsets come from ``jedimaster.pilot.layout``
      at run time; every text and mission number is made up.
    - Numbers are written little-endian, ``u32`` unsigned, others signed.

Call:
    ``record(FULL_RECORD, {("rating",): 7, ("name",): b"Ace"})``
"""

from __future__ import annotations

from math import prod
from pathlib import Path

from textdata import crlf

from jedimaster.pilot.layout import FULL_RECORD
from jedimaster.pilot.layout import struct
from jedimaster.pilot.layout import U32

FRONT_ENTRIES = 480
"""Lines of the made-up fronttxt.txt: past both entries the load asks for."""


def _member(name: str, member_name: str):
    for member in struct(name).members:
        if member.name == member_name:
            return member
    raise KeyError(member_name)


def offset(name: str, path: tuple) -> tuple[int, object]:
    """Return the byte offset of ``path`` in struct ``name`` and its member.

    ``path`` alternates names and element indexes; fewer indexes than
    dimensions name the start of that row.
    """
    at, member, current = 0, None, name
    i = 0
    while i < len(path):
        member = _member(current, path[i])
        at += member.offset
        i += 1
        dims = member.dims
        index = 0
        used = 0
        while i < len(path) and isinstance(path[i], int):
            index = index * dims[used] + path[i]
            used += 1
            i += 1
        index *= prod(dims[used:])
        at += index * (member.size // max(prod(dims), 1))
        current = member.type
    return at, member


def put(data: bytearray, name: str, path: tuple, value) -> None:
    """Write ``value`` at ``path``: an int as one 32-bit number, else bytes."""
    at, member = offset(name, path)
    if isinstance(value, int):
        signed = member.type != U32
        data[at : at + 4] = value.to_bytes(4, "little", signed=signed)
    else:
        data[at : at + len(value)] = value


def record(name: str = FULL_RECORD, values: dict | None = None) -> bytearray:
    """Return a record's bytes: zeros, then each ``{path: value}`` written."""
    data = bytearray(struct(name).size)
    for path, value in (values or {}).items():
        put(data, name, path, value)
    return data


def value_at(data: bytes, name: str, path: tuple) -> int:
    """Return the signed 32-bit number at ``path``."""
    at, _ = offset(name, path)
    return int.from_bytes(data[at : at + 4], "little", signed=True)


def bytes_at(data: bytes, name: str, path: tuple, length: int) -> bytes:
    """Return ``length`` bytes at ``path``."""
    at, _ = offset(name, path)
    return bytes(data[at : at + length])


def patterned(name: str, seed: int) -> bytearray:
    """Return a record whose 32-bit word ``k`` is ``seed * 1_000_000 + k``."""
    size = struct(name).size
    data = bytearray(size)
    for k in range(size // 4):
        data[4 * k : 4 * k + 4] = (seed * 1_000_000 + k).to_bytes(4, "little")
    return data


def word(data: bytes, at: int) -> int:
    """Return the unsigned 32-bit number at byte ``at``."""
    return int.from_bytes(data[at : at + 4], "little")


def menu(*ids: int) -> bytes:
    """Return a training menu: a comment, a section, an entry per id."""
    lines = [b"// made-up list", b"[Section]"]
    for number in ids:
        lines += [str(number).encode(), f"m{number}.tie".encode(), b"Title"]
    return crlf(*lines)


def front(entries: dict[int, bytes]) -> bytes:
    """Return a fronttxt.txt of ``FRONT_ENTRIES`` lines, some replaced."""
    lines = [f"entry {i}".encode() for i in range(FRONT_ENTRIES)]
    for number, text in entries.items():
        lines[number] = text
    return crlf(*lines)


def install(
    root: Path,
    base: tuple[int, int, int] = (11, 12, 13),
    bop: tuple[int, int, int] | None = (21, 22, 23),
    texts: dict[int, bytes] | None = None,
    bop_texts: dict[int, bytes] | None = None,
) -> Path:
    """Write a fake install: fronttxt.txt and Train lists, maybe Balance of Power.

    ``base`` and ``bop`` give the first mission of the Rebel, Imperial and
    network lists of each; ``bop`` None leaves Balance of Power out.
    """
    folders = [(root, "Train", base, texts or {})]
    if bop is not None:
        folders.append((root / "BalanceOfPower", "TRAIN", bop, bop_texts or {}))
    for folder, train, firsts, entries in folders:
        (folder / train).mkdir(parents=True, exist_ok=True)
        (folder / "fronttxt.txt").write_bytes(front(entries))
        for list_name, first in zip(
            ("rebel", "imperial", "mission"), firsts, strict=True
        ):
            (folder / train / f"{list_name}.lst").write_bytes(menu(first, first + 1))
    return root
