"""Read the header of a Smacker movie file and work out its timing and size.

Purpose:
    Give the game's movies (``SMK2`` stock, ``SMK4``) their header as a
    data class: signature, stored size, frame count, frame rate, flags,
    audio buffers and rate words, tree sizes, and per-frame sizes and
    types; derive the frame duration, the picture's intended size and the
    audio track the decoder plays. Refuse a file that is not a Smacker file
    or is cut short, naming the file and the field.

Flow:
    ``read_header`` takes a path or bytes, unpacks the fixed 104 bytes, then
    the frame size and type tables, and checks that the header, the trees
    and the frames fit in the file. ``frame_duration_us`` turns the rate
    into microseconds; ``HeaderInfo`` properties give the rest.

Invariants:
    - Little-endian throughout; the rate is signed, every other number
      unsigned.
    - A frame's size is its table value with the two low flag bits cleared;
      bit 0 of the table value marks a key frame.
    - With the ring flag (bit 0) the tables hold one more entry than the
      frame count.
    - With bit 1 or bit 2 of the flags set, the picture is twice the stored
      height: each stored row is drawn twice.

Call:
    ``header = read_header(path)``
"""

from __future__ import annotations

import logging
import os
import struct
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger(__name__)

SIGNATURES = (b"SMK2", b"SMK4")
FIXED_SIZE = 104
TRACKS = 7
TREES = 4
TREE_NAMES = ("mmap", "mclr", "full", "type")
FLAG_RING = 1
FLAG_INTERLACED = 2
FLAG_DOUBLED = 4
DEFAULT_FRAME_US = 100_000
RATE_COMPRESSED = 1 << 31
RATE_HAS_DATA = 1 << 30
RATE_16_BIT = 1 << 29
RATE_STEREO = 1 << 28
RATE_MASK = 0xFFFFFF


class MovieFormatError(ValueError):
    """A movie file that is not a Smacker file or is cut short.

    ``label`` names the file, ``field`` the header part that failed.
    """

    def __init__(self, label: str, field: str, problem: str) -> None:
        super().__init__(f"{label}: {field}: {problem}")
        self.label = label
        self.field = field


@dataclass(frozen=True)
class AudioTrack:
    """One audio track's rate word, split into its parts."""

    index: int
    buffer_size: int
    word: int

    @property
    def rate(self) -> int:
        """The sample rate in hertz (bits 0 to 23)."""
        return self.word & RATE_MASK

    @property
    def compressed(self) -> bool:
        """True when the track is compressed (bit 31)."""
        return bool(self.word & RATE_COMPRESSED)

    @property
    def has_data(self) -> bool:
        """True when the track carries data (bit 30)."""
        return bool(self.word & RATE_HAS_DATA)

    @property
    def sixteen_bit(self) -> bool:
        """True for 16-bit samples, False for 8-bit (bit 29)."""
        return bool(self.word & RATE_16_BIT)

    @property
    def stereo(self) -> bool:
        """True for two channels (bit 28)."""
        return bool(self.word & RATE_STEREO)

    @property
    def channels(self) -> int:
        """The channel count: 2 for stereo, else 1."""
        return 2 if self.stereo else 1

    @property
    def compression(self) -> int:
        """The compression kind (bits 26 and 27)."""
        return (self.word >> 26) & 3

    @property
    def codec(self) -> str:
        """The decoder's name: ``smackaud`` when compressed, else raw PCM."""
        if self.compressed:
            return "smackaud"
        return "pcm_s16le" if self.sixteen_bit else "pcm_u8"


@dataclass(frozen=True)
class HeaderInfo:
    """The fields of a Smacker header, as stored."""

    signature: str
    width: int
    height: int
    frames: int
    rate: int
    flags: int
    audio_buffers: tuple[int, ...]
    tree_bytes: int
    tree_sizes: tuple[int, ...]
    audio_words: tuple[int, ...]
    unused: int
    frame_sizes: tuple[int, ...]
    frame_types: tuple[int, ...]

    @property
    def ring(self) -> bool:
        """True when the file holds one more frame than the count (bit 0)."""
        return bool(self.flags & FLAG_RING)

    @property
    def interlaced(self) -> bool:
        """True when the file is Y-interlaced (bit 1)."""
        return bool(self.flags & FLAG_INTERLACED)

    @property
    def doubled(self) -> bool:
        """True when bit 1 or bit 2 is set: stored height is half the picture's."""
        return bool(self.flags & (FLAG_INTERLACED | FLAG_DOUBLED))

    @property
    def y_doubled(self) -> bool:
        """True when the file is Y-doubled (bit 2)."""
        return bool(self.flags & FLAG_DOUBLED)

    @property
    def frame_us(self) -> int:
        """One frame's duration in microseconds, from the rate."""
        return frame_duration_us(self.rate)

    @property
    def picture_width(self) -> int:
        """The intended picture's width in pixels."""
        return self.width

    @property
    def picture_height(self) -> int:
        """The intended picture's height: twice the stored one when doubled."""
        return self.height * 2 if self.doubled else self.height

    @property
    def display_width(self) -> int:
        """The width a decoder that honours the flag reports: half when doubled."""
        return self.width // 2 if self.doubled else self.width

    @property
    def duration_us(self) -> int:
        """The movie's duration: the frame count times the frame duration."""
        return self.frames * self.frame_us

    @property
    def tracks(self) -> tuple[AudioTrack, ...]:
        """The seven audio tracks, in order."""
        return tuple(
            AudioTrack(i, self.audio_buffers[i], self.audio_words[i])
            for i in range(TRACKS)
        )

    @property
    def audio(self) -> AudioTrack | None:
        """The first track that carries data, or None when there is none."""
        for track in self.tracks:
            if track.has_data:
                return track
        return None

    def frame_size(self, index: int) -> int:
        """Return frame ``index`` (from 0) size in bytes, flag bits cleared."""
        return self.frame_sizes[index] & ~3

    def is_key_frame(self, index: int) -> bool:
        """Return True when frame ``index`` (from 0) is flagged a key frame."""
        return bool(self.frame_sizes[index] & 1)

    def has_palette(self, index: int) -> bool:
        """Return True when frame ``index`` (from 0) brings a palette."""
        return bool(self.frame_types[index] & 1)

    def expected_size(self) -> int:
        """Return the file length the header's sizes add up to."""
        return (
            FIXED_SIZE
            + 5 * len(self.frame_sizes)
            + self.tree_bytes
            + sum(size & ~3 for size in self.frame_sizes)
        )


def frame_duration_us(rate: int) -> int:
    """Return the frame duration in microseconds for a header rate.

    Above 0 the rate is milliseconds a frame; below 0, its negation in units
    of 10 microseconds; 0 is ten frames a second. Does not check the range.
    """
    if rate > 0:
        return rate * 1000
    if rate < 0:
        return -rate * 10
    return DEFAULT_FRAME_US


def _label(source: str | os.PathLike[str] | bytes | bytearray) -> str:
    if isinstance(source, (bytes, bytearray)):
        return "<bytes>"
    return os.fspath(source)


def _load_prefix(
    source: str | os.PathLike[str] | bytes | bytearray,
) -> tuple[bytes, int]:
    """Return the file's first bytes (all of them for bytes) and its length."""
    if isinstance(source, (bytes, bytearray)):
        return bytes(source), len(source)
    path = Path(source)
    size = path.stat().st_size
    with path.open("rb") as handle:
        fixed = handle.read(FIXED_SIZE)
        if len(fixed) < FIXED_SIZE or fixed[:4] not in SIGNATURES:
            return fixed, size
        count = struct.unpack_from("<I", fixed, 12)[0]
        count += struct.unpack_from("<I", fixed, 20)[0] & FLAG_RING
        # Never read more than the file holds, whatever the count claims.
        want = min(5 * count, max(size - FIXED_SIZE, 0))
        return fixed + handle.read(want), size


def read_header(source: str | os.PathLike[str] | bytes | bytearray) -> HeaderInfo:
    """Return the header of a Smacker file, from a path or from its bytes.

    Raises ``MovieFormatError`` (naming the file and the field) when the
    signature is not ``SMK2`` or ``SMK4``, when the fixed part or a frame
    table is cut short, or when the trees and frames the header describes do
    not fit in the file's length. Raises ``OSError`` for a path that cannot
    be read. A file longer than its header describes is accepted. Does not
    decode any frame.
    """
    label = _label(source)
    data, total = _load_prefix(source)
    if len(data) < 4:
        raise MovieFormatError(label, "signature", f"only {len(data)} bytes")
    signature = data[:4]
    if signature not in SIGNATURES:
        raise MovieFormatError(label, "signature", f"{signature!r} is not SMK2 or SMK4")
    if len(data) < FIXED_SIZE:
        raise MovieFormatError(label, "header", f"{len(data)} of {FIXED_SIZE} bytes")
    width, height, frames, rate, flags = struct.unpack_from("<IIIiI", data, 4)
    audio_buffers = struct.unpack_from("<7I", data, 24)
    (tree_bytes,) = struct.unpack_from("<I", data, 52)
    tree_sizes = struct.unpack_from("<4I", data, 56)
    audio_words = struct.unpack_from("<7I", data, 72)
    (unused,) = struct.unpack_from("<I", data, 100)
    entries = frames + (flags & FLAG_RING)
    table_end = FIXED_SIZE + 5 * entries
    if len(data) < table_end:
        raise MovieFormatError(
            label, "frame table", f"{len(data) - FIXED_SIZE} bytes for {entries} frames"
        )
    sizes = struct.unpack_from(f"<{entries}I", data, FIXED_SIZE)
    types = tuple(data[FIXED_SIZE + 4 * entries : table_end])
    info = HeaderInfo(
        signature.decode("ascii"),
        width,
        height,
        frames,
        rate,
        flags,
        audio_buffers,
        tree_bytes,
        tree_sizes,
        audio_words,
        unused,
        sizes,
        types,
    )
    if info.expected_size() > total:
        raise MovieFormatError(
            label,
            "frame data",
            f"the header describes {info.expected_size()} bytes, the file has {total}",
        )
    logger.debug(
        "%s: %s %dx%d frames=%d rate=%d flags=%d",
        label,
        info.signature,
        width,
        height,
        frames,
        rate,
        flags,
    )
    return info
