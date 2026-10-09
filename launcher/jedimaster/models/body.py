"""The body of a model file: bytes at a base address, read without overreach.

Purpose:
    Hold a model file's body with its base address, turn links (addresses)
    into body offsets, and read from it in the two ways the game does:
    strictly (what does not fit is refused) and padded (a payload cut by
    the body's end reads as zero bytes there).

Flow:
    ``reader.read_opt`` builds one ``Body``; the reader and the texture
    reader call its methods; every refusal is a ``ModelFormatError``.

Invariants:
    - A link of 0 is none; any other link names offset ``link - base``.
    - No method reads outside ``data``.

Call:
    ``body.need(body.offset(link, "node"), 24, "node")``
"""

from __future__ import annotations

import logging
import struct
from dataclasses import dataclass

logger = logging.getLogger(__name__)


class ModelFormatError(ValueError):
    """A model file the game refuses; the message names the rule broken."""


@dataclass(frozen=True)
class Body:
    """The body of a model file and its base address."""

    data: bytes
    base: int

    @property
    def size(self) -> int:
        """Return the body's size in bytes; does not count the file's marker."""
        return len(self.data)

    def offset(self, link: int, what: str) -> int:
        """Return the body offset a link names.

        Raises ``ModelFormatError`` for a link that does not fall inside
        the body (a link of 0 included). Does not check what lies there.
        """
        offset = link - self.base
        if link == 0 or not 0 <= offset < self.size:
            raise ModelFormatError(f"{what} link {link:#x} is not inside the body")
        return offset

    def need(self, offset: int, size: int, what: str) -> bytes:
        """Return ``size`` bytes at ``offset``, which must lie inside the body.

        Raises ``ModelFormatError`` when any of them lies outside. Does not
        pad.
        """
        if offset < 0 or size < 0 or offset + size > self.size:
            raise ModelFormatError(
                f"{what} ({size} bytes at body offset {offset}) runs past the body"
            )
        return self.data[offset : offset + size]

    def padded(self, offset: int, size: int) -> bytes:
        """Return ``size`` bytes at ``offset``, zeros past the body's end.

        Never raises for a cut payload. Does not check that ``offset`` is
        inside the body.
        """
        data = self.data[offset : offset + size]
        return data + bytes(size - len(data))

    def string(self, offset: int, what: str) -> bytes:
        """Return the bytes at ``offset`` up to their 0 byte, which must exist.

        Raises ``ModelFormatError`` when no 0 byte ends the string inside
        the body. Does not check the bytes themselves.
        """
        end = self.data.find(b"\0", offset)
        if end < 0:
            raise ModelFormatError(f"{what} at body offset {offset} has no 0 byte")
        return self.data[offset:end]

    def int32(self, offset: int, what: str) -> int:
        """Return the signed 4-byte integer at ``offset``.

        Raises ``ModelFormatError`` when the 4 bytes do not fit in the body.
        Does not check the value.
        """
        return struct.unpack("<i", self.need(offset, 4, what))[0]

    def uint32(self, offset: int, what: str) -> int:
        """Return the unsigned 4-byte integer at ``offset``.

        Raises ``ModelFormatError`` when the 4 bytes do not fit in the body.
        Does not check the value.
        """
        return struct.unpack("<I", self.need(offset, 4, what))[0]
