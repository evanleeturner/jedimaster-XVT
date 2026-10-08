"""Build synthetic list files for the list tests.

Purpose:
    Write list bytes the way the stock files are written (CR LF line
    endings, an optional final 0x1A byte, an optional missing last line
    ending) from plain lines, so tests read back exactly what they wrote.

Flow:
    ``crlf(*lines)`` joins lines with CR LF; ``bmp_header`` builds the
    first bytes of a BMP file.

Invariants:
    - No game data: every name and title is a generic word.

Call:
    ``data = crlf("//", "[Section]", "1", "a.tie", "Title")``
"""

from __future__ import annotations

import logging
import struct

logger = logging.getLogger(__name__)


def crlf(*lines: str, final: bool = True, end_mark: bool = False) -> bytes:
    """Return ``lines`` joined by CR LF, Latin-1 encoded.

    ``final`` adds a CR LF after the last line; ``end_mark`` then adds the
    0x1A byte.
    """
    text = "\r\n".join(lines) + ("\r\n" if final else "")
    data = text.encode("latin-1")
    return data + (b"\x1a" if end_mark else b"")


def bmp_header(width: int, height: int, core: bool = False) -> bytes:
    """Return a BMP file's first bytes: file header and the info header start."""
    if core:
        info = struct.pack("<IHHHH", 12, width, height, 1, 8)
    else:
        info = struct.pack("<IiiHH", 40, width, height, 1, 8)
    return b"BM" + struct.pack("<IHHI", 0, 0, 0, 54) + info + b"\0" * 24
