"""The answer sheets' shared form: quoted values and the three header lines.

Purpose:
    Quote a value the way every sheet of the text, font and picture readers
    prints it, and write the ``kind``, ``name`` and ``file`` lines that open
    every sheet but the pictures'.

Flow:
    ``quote`` escapes byte by byte and ``unquote`` undoes it; ``header``
    builds the three lines; ``join`` ends a sheet with one newline.

Invariants:
    - A quoted value stops at its first byte 0, or after ``limit`` bytes
      when one is given (the size of the field it was read from).
    - A double quote and a backslash print with a backslash before them;
      line feed, carriage return and tab as ``\\n``, ``\\r`` and ``\\t``;
      every other byte below 32 or above 126 as ``\\x`` and two lowercase
      hex digits.

Call:
    ``quote(b"a\\tb")`` -> ``'"a\\\\tb"'``
"""

from __future__ import annotations

import logging

logger = logging.getLogger(__name__)

ESCAPES = {0x22: '\\"', 0x5C: "\\\\", 0x0A: "\\n", 0x0D: "\\r", 0x09: "\\t"}


def quote(value: bytes | str, limit: int | None = None) -> str:
    """Return ``value`` between double quotes, escaped as the sheets print it.

    A ``str`` stands for its Latin-1 bytes. Stops at the first byte 0 and,
    when ``limit`` is given, after ``limit`` bytes. Always returns a quoted
    string; raises ``UnicodeEncodeError`` for a ``str`` outside Latin-1.
    Does not check the value otherwise.
    """
    data = value.encode("latin-1") if isinstance(value, str) else value
    if limit is not None:
        data = data[:limit]
    end = data.find(0)
    if end >= 0:
        data = data[:end]
    out = []
    for byte in data:
        if byte in ESCAPES:
            out.append(ESCAPES[byte])
        elif 32 <= byte <= 126:
            out.append(chr(byte))
        else:
            out.append(f"\\x{byte:02x}")
    return '"' + "".join(out) + '"'


UNESCAPES = {"n": 0x0A, "r": 0x0D, "t": 0x09, '"': 0x22, "\\": 0x5C}


def unquote(text: str) -> bytes:
    """Return the bytes a value printed by ``quote`` stands for.

    Surrounding double quotes are dropped when both are there; ``\\n``,
    ``\\r``, ``\\t``, ``\\"``, ``\\\\`` and ``\\xNN`` become their bytes,
    every other character its Latin-1 byte. Raises ``ValueError`` for an
    unknown or cut escape. Does not restore what ``quote`` cut off.
    """
    if len(text) >= 2 and text[0] == text[-1] == '"':
        text = text[1:-1]
    out = bytearray()
    i = 0
    while i < len(text):
        char = text[i]
        if char != "\\":
            out += char.encode("latin-1")
            i += 1
        elif text[i + 1 : i + 2] == "x" and len(text[i + 2 : i + 4]) == 2:
            out.append(int(text[i + 2 : i + 4], 16))
            i += 4
        elif text[i + 1 : i + 2] in UNESCAPES:
            out.append(UNESCAPES[text[i + 1]])
            i += 2
        else:
            raise ValueError(f"bad escape at {i} in {text!r}")
    return bytes(out)


def header(kind: str, name: str, file: str) -> list[str]:
    """Return the three lines that open a sheet: kind, game name, file.

    ``name`` and ``file`` are quoted with ``quote``. Does not check them.
    """
    return [f"kind {kind}", f"name {quote(name)}", f"file {quote(file)}"]


def join(lines: list[str]) -> str:
    """Return the lines joined by newlines, with one newline at the end.

    Does not check the lines for newlines of their own.
    """
    return "\n".join(lines) + "\n"
