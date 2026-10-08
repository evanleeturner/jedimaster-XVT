"""Build synthetic text files, fonts and installs for the text, font and picture tests.

Purpose:
    Give the tests files without game data: a ``strings.txt`` laid out by
    the package's own table data with made-up words, CR LF joined lines, a
    menu font from rows of codes, and fake installs holding these files.

Flow:
    ``crlf`` joins lines; ``strings_lines`` and ``strings_bytes`` build a
    whole ``strings.txt``; ``font_bytes`` builds a font from glyph rows
    written with ``rows``; ``write_files`` writes an install.

Invariants:
    - No game data: every text is made up; table lengths come from the
      package (``jedimaster.text.tables``), never copied here.
    - The made-up words name their table and entry, so a test can tell
      where a line landed.

Call:
    ``strings_bytes(models={0: b"fX"})``; ``font_bytes({65: (3, [b"\\x03\\0"])})``
"""

from __future__ import annotations

import struct
from pathlib import Path

from jedimaster.text.tables import GENDER_TABLES
from jedimaster.text.tables import GOAL
from jedimaster.text.tables import goal_lines
from jedimaster.text.tables import MODELS
from jedimaster.text.tables import TABLES


def crlf(*lines: bytes) -> bytes:
    """Return the lines, each ended by CR LF."""
    return b"".join(line + b"\r\n" for line in lines)


def table_lines(name: str, kind: str, length: int, gender: bytes = b"m") -> list[bytes]:
    """Return made-up lines for one table: ``<name> <entry>`` each."""
    if kind == GOAL:
        return [
            f"{name} {row}.{v}".encode()
            for row, count in enumerate(goal_lines(length))
            for v in range(count)
        ]
    if kind == MODELS:
        return [gender + b" " + f"model {i}".encode() for i in range(length)]
    return [f"{name} {i}".encode() for i in range(length)]


def strings_lines(
    models: dict[int, bytes] | None = None,
    lines: dict[tuple[str, int], bytes] | None = None,
) -> list[bytes]:
    """Return a whole strings.txt's lines; gender tables follow when asked for.

    ``models`` replaces model name lines by index; ``lines`` replaces any
    other table's line ``(table, line index)``. The feminine and neutered
    tables are added when a model line starts with ``f`` or ``n``.
    """
    out: list[bytes] = []
    for table in TABLES:
        part = table_lines(*table)
        if table.kind == MODELS:
            for index, text in (models or {}).items():
                part[index] = text
            genders = {line[:1] for line in part}
        for (name, index), text in (lines or {}).items():
            if name == table.name:
                part[index] = text
        out += part
    for gender, table in GENDER_TABLES:
        if b"mfn"[gender : gender + 1] in genders:
            out += table_lines(*table)
    return out


def strings_bytes(**changes) -> bytes:
    """Return ``strings_lines(**changes)`` as a CR LF file."""
    return crlf(*strings_lines(**changes))


def rows(*codes: bytes) -> bytes:
    """Return glyph rows: each a 4-byte length (its own bytes), codes, ``0x80``."""
    out = b""
    for row in codes:
        body = row + b"\x80"
        out += struct.pack("<I", 4 + len(body)) + body
    return out


def font_bytes(
    glyphs: dict[int, tuple[int, list[bytes] | bytes]],
    height: int = 3,
    points: int = 9,
    in_use: int = 1,
    spacing: int = 0,
    spare: int = 7,
    heights: dict[int, int] | None = None,
    size: int | None = None,
) -> bytes:
    """Return a whole font: header, then each given glyph's rows.

    ``glyphs[c] = (width, rows)``: a list of each row's codes (its height
    is their count) or raw row data (height ``height``). Every other glyph
    points at a blank glyph of ``height`` skip rows, width 2. ``heights``
    overrides height entries; ``size`` the header's glyph data size.
    """
    data = bytearray(rows(*[b"\x42"] * height))
    offsets, widths, all_heights = [0] * 256, [2] * 256, [height] * 256
    for code, (width, body) in glyphs.items():
        offsets[code] = len(data)
        widths[code] = width
        if isinstance(body, list):
            all_heights[code] = len(body)
            body = rows(*body)
        data += body
    for code, value in (heights or {}).items():
        all_heights[code] = value
    header = struct.pack(
        "<I256I256B256BIBBB",
        len(data) if size is None else size,
        *offsets,
        *all_heights,
        *widths,
        points,
        in_use,
        spacing,
        spare,
    )
    return header + bytes(data)


def write_files(root: Path, files: dict[str, bytes]) -> Path:
    """Write ``files`` (install-relative path -> bytes) under ``root``; add Train."""
    for rel, data in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (root / "Train").mkdir(parents=True, exist_ok=True)
    return root
