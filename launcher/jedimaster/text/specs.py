"""Read specdesc.txt, the craft database's text, as the game reads it.

Purpose:
    Fill the game's 93 craft entries of five fixed-size fields each (name,
    manufacturer, users, description, crew) from the file's lines, keeping
    what the game keeps and reporting what it would mis-read.

Flow:
    ``read_specs`` reads lines of buffer 256 (comments skipped, line feed
    dropped), then fills the entries in order, five lines each, each value
    cut to its field's size. It stops at the file's end.

Invariants:
    - A field keeps at most its size in bytes (``FIELDS``): 64 for name,
      manufacturer, users and crew, 256 for the description. A value of its
      field's size or more fills the field with no end mark, so the game
      reading it as text runs on into the next field: it is kept as cut,
      with a WARNING.
    - When the file ends early the entries read stay, the rest stay empty,
      and an entry cut part way is logged with a WARNING.

Call:
    ``specs = read_specs(path); specs.entries[0].name``
"""

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from dataclasses import field

from .lines import load
from .lines import read_lines

logger = logging.getLogger(__name__)

BUFFER = 256
ENTRIES = 93
FIELDS: tuple[tuple[str, int], ...] = (
    ("name", 64),
    ("manufacturer", 64),
    ("users", 64),
    ("description", 256),
    ("crew", 64),
)
"""Each entry's fields, in file order, with the size of each in bytes."""


@dataclass
class SpecEntry:
    """One craft's five fields, each as the game's field holds it."""

    name: bytes = b""
    manufacturer: bytes = b""
    users: bytes = b""
    description: bytes = b""
    crew: bytes = b""


@dataclass
class SpecsFile:
    """The 93 entries, the count read whole, and the one cut part way."""

    entries: list[SpecEntry] = field(default_factory=list)
    complete: int = 0
    cut: int | None = None


def read_specs(source: str | os.PathLike[str] | bytes | bytearray) -> SpecsFile:
    """Return specdesc.txt's 93 entries as the game fills them.

    Always returns 93 entries: those the file reaches filled field by field
    (each value cut to its field's size), the rest empty. ``complete``
    counts the entries read whole; ``cut`` is the entry the file ended
    inside, or None. Logs a WARNING for each value that fills its field and
    for a cut entry. Raises ``OSError`` when the path cannot be read. Does
    not read lines past the 93rd entry.
    """
    data, label = load(source)
    lines = read_lines(data, BUFFER, skip_comments=True)
    specs = SpecsFile([SpecEntry() for _ in range(ENTRIES)])
    taken = 0
    for index, entry in enumerate(specs.entries):
        for name, size in FIELDS:
            if taken >= len(lines):
                break
            line = lines[taken]
            taken += 1
            value = line.text[:size]
            setattr(entry, name, value)
            logger.debug("spec %d %s: %r", index, name, value)
            if len(line.text) >= size:
                logger.warning(
                    "%s: spec %d %s (line %d) holds %d bytes: it fills its "
                    "%d-byte field with no end mark",
                    label,
                    index,
                    name,
                    line.number,
                    len(line.text),
                    size,
                )
        else:
            specs.complete += 1
            continue
        if taken % len(FIELDS):
            specs.cut = index
            logger.warning(
                "%s: the file ends inside spec %d, after %d of its %d lines",
                label,
                index,
                taken % len(FIELDS),
                len(FIELDS),
            )
        break
    logger.info("specs %s: %d of %d entries read", label, specs.complete, ENTRIES)
    return specs
