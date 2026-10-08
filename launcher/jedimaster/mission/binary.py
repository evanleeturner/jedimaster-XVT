"""Little-endian reading helpers for the XvT mission reader.

Purpose:
    Read fixed-width integers and fixed-length strings out of a bytes buffer,
    one cursor at a time, and refuse a read that runs past the end.

Flow:
    A ``Cursor`` wraps the whole file and an offset. Each ``u8``/``s8``/
    ``u16``/``s16``/``s32`` call reads at the offset and advances it;
    ``string`` reads a fixed-length field, cuts it at the first NUL and decodes
    it as ISO-8859-1. ``skip`` advances without reading. ``sub`` hands out a
    cursor positioned at an absolute offset for a structure read.

Invariants:
    - Every multi-byte integer is little-endian.
    - A read never returns partial data: it raises ``ShortReadError`` before
      the offset moves past ``len(data)``.
    - Strings are decoded ISO-8859-1, so every byte maps to one character and
      decoding never fails.

Call:
    ``cur = Cursor(data); version = cur.s16(); name = cur.string(20)``
"""

from __future__ import annotations

import logging
import struct

logger = logging.getLogger(__name__)


class MissionFormatError(ValueError):
    """The bytes are not a mission this reader accepts."""


class ShortReadError(MissionFormatError):
    """A field reaches past the end of the data."""


def decode_fixed(raw: bytes) -> str:
    """Return ``raw`` cut at its first NUL and decoded as ISO-8859-1.

    Returns the whole field decoded when it holds no NUL, and ``""`` when the
    first byte is NUL. Does not check that the characters are printable.
    """
    cut = raw.find(b"\0")
    if cut >= 0:
        raw = raw[:cut]
    return raw.decode("iso-8859-1")


class Cursor:
    """A read position in a mission's bytes."""

    def __init__(self, data: bytes, offset: int = 0) -> None:
        self.data = data
        self.offset = offset

    def _take(self, size: int) -> bytes:
        end = self.offset + size
        if size < 0 or end > len(self.data):
            raise ShortReadError(
                f"short read: need {size} bytes at offset 0x{self.offset:x}, "
                f"file has {len(self.data)}"
            )
        chunk = self.data[self.offset : end]
        self.offset = end
        return chunk

    def raw(self, size: int) -> bytes:
        """Return the next ``size`` bytes; raises ``ShortReadError`` past the end.

        Does not interpret the bytes.
        """
        return self._take(size)

    def skip(self, size: int) -> None:
        """Advance ``size`` bytes; raises ``ShortReadError`` past the end.

        Returns nothing. Does not check that the skipped bytes are zero.
        """
        self._take(size)

    def u8(self) -> int:
        """Return the next byte as 0..255; raises ``ShortReadError`` at the end."""
        return self._take(1)[0]

    def s8(self) -> int:
        """Return the next byte as -128..127; raises ``ShortReadError`` at the end."""
        return struct.unpack("<b", self._take(1))[0]

    def u16(self) -> int:
        """Return the next 2 bytes as an unsigned little-endian integer.

        Raises ``ShortReadError`` when fewer than 2 bytes remain.
        """
        return struct.unpack("<H", self._take(2))[0]

    def s16(self) -> int:
        """Return the next 2 bytes as a signed little-endian integer.

        Raises ``ShortReadError`` when fewer than 2 bytes remain.
        """
        return struct.unpack("<h", self._take(2))[0]

    def s32(self) -> int:
        """Return the next 4 bytes as a signed little-endian integer.

        Raises ``ShortReadError`` when fewer than 4 bytes remain.
        """
        return struct.unpack("<i", self._take(4))[0]

    def u8s(self, count: int) -> list[int]:
        """Return the next ``count`` bytes as a list of 0..255 integers.

        Raises ``ShortReadError`` when fewer than ``count`` bytes remain.
        """
        return list(self._take(count))

    def s16s(self, count: int) -> list[int]:
        """Return the next ``count`` signed 16-bit integers as a list.

        Raises ``ShortReadError`` when fewer than ``2 * count`` bytes remain.
        """
        return list(struct.unpack(f"<{count}h", self._take(2 * count)))

    def string(self, size: int) -> str:
        """Return a ``size``-byte field cut at its first NUL, ISO-8859-1.

        Raises ``ShortReadError`` when fewer than ``size`` bytes remain. The
        cursor always advances the full ``size``. Does not check the bytes
        after the NUL.
        """
        return decode_fixed(self._take(size))

    def sub(self, offset: int) -> Cursor:
        """Return a new cursor on the same data at absolute ``offset``.

        Does not check the offset; the first read past the end raises.
        """
        return Cursor(self.data, offset)
