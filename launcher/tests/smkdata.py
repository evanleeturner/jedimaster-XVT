"""Build movie inputs for the tests: Smacker files, subtitle files, lossless movies.

Purpose:
    Give the movie tests their made-up data, none of it from the game: a
    Smacker header with chosen fields (``smk_header``), a whole small
    Smacker file that ``ffmpeg`` decodes (``smk_file``), subtitle files as
    bytes with the records the game's reading gives them (``SUBTITLE_CASES``),
    an install folder holding them (``movie_install``), and a lossless movie
    of known frames (``lossless_movie``).

Flow:
    ``smk_header`` packs the fixed 104 bytes, the frame tables and padding;
    ``smk_file`` adds trees and frame data that decode; ``lossless_movie``
    pipes known RGBA frames through ``ffmpeg`` into a lossless codec.

Invariants:
    - No game data; the subtitle cases mirror 22 made-up files and their
      answer sheets.
    - ``ffmpeg`` is run only by ``lossless_movie`` and the tests that call it.

Call:
    ``data = smk_file(frames=4, width=8, height=8)``
"""

from __future__ import annotations

import logging
import shutil
import struct
import subprocess
from pathlib import Path

import pytest
from textdata import write_files

logger = logging.getLogger(__name__)

needs_ffmpeg = pytest.mark.skipif(
    shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None,
    reason="ffmpeg and ffprobe are not on the path",
)
STOCK_AUDIO = 0xD0002B11
PCM_AUDIO = 0x50002B11
LIST_TEXT = (
    "3\n// Filename\n// [campaign] [phase] [mission]\n"
    "intro\n1 0 7\nintrothumb\nA made-up first entry.\n"
    "OPENING\n1 0 76\nrebcut1\nSame as the opening, other case.\n"
    "Flyby1a\n2 1 56\nimpcut1\nThe network's stand-in.\n"
)
"""A cutscene list that repeats ``Opening`` (other case) and ``Flyby1a``."""


def smk_header(
    width: int = 640,
    height: int = 360,
    frames: int = 3,
    rate: int = -6666,
    flags: int = 0,
    audio_buffers: tuple[int, ...] = (0,) * 7,
    audio_words: tuple[int, ...] = (0,) * 7,
    tree_bytes: int = 0,
    tree_sizes: tuple[int, ...] = (4, 3, 2, 1),
    sizes: list[int] | None = None,
    types: list[int] | None = None,
    signature: bytes = b"SMK2",
    body: bool = True,
) -> bytes:
    """Return a Smacker file: header, frame tables, and zero padding for the rest.

    ``sizes`` (the tables' values, flag bits included) and ``types`` default
    to 8 bytes a frame, key frame flag on the first, palette bit on the
    first; with the ring flag the tables get one more entry. With ``body``
    False the padding for the trees and frames is left out.
    """
    entries = frames + (flags & 1)
    sizes = sizes if sizes is not None else [8 | (i == 0) for i in range(entries)]
    types = types if types is not None else [int(i == 0) for i in range(entries)]
    head = signature + struct.pack("<IIIiI", width, height, frames, rate, flags)
    head += struct.pack("<7I", *audio_buffers) + struct.pack("<I", tree_bytes)
    head += struct.pack("<4I", *tree_sizes) + struct.pack("<7I", *audio_words)
    head += struct.pack("<I", 0)
    head += struct.pack(f"<{len(sizes)}I", *sizes) + bytes(types)
    padding = tree_bytes + sum(size & ~3 for size in sizes)
    return head + (bytes(padding) if body else b"")


def _trees() -> bytes:
    bits: list[int] = []

    def put(value: int, count: int) -> None:
        bits.extend((value >> i) & 1 for i in range(count))

    for _ in range(3):
        put(1, 1)
        put(0, 1), put(0, 8), put(0, 1), put(0, 8)
        put(0, 16), put(0, 16), put(0, 16)
    put(1, 1), put(0, 1), put(3, 8)
    return bytes(
        sum(bit << j for j, bit in enumerate(bits[i : i + 8]))
        for i in range(0, len(bits), 8)
    )


def smk_file(
    frames: int = 4,
    width: int = 8,
    height: int = 8,
    flags: int = 0,
    audio: bool = False,
    rate: int = -6666,
) -> bytes:
    """Return a small whole Smacker file that ``ffmpeg`` decodes.

    ``frames`` frames of ``width`` by ``height`` (stored), each one color; with ``audio`` a
    stereo 8-bit 11,025 Hz uncompressed track of 2000 bytes a frame. The
    picture inside is whatever the decoder makes of empty trees; the tests
    compare it only with the same decoder's other runs. Each frame brings a
    palette whose first color differs, so the frames differ from each other.
    """
    trees = _trees()
    chunk = 2000
    data = []
    for i in range(frames + (flags & 1)):
        body = b"\x01" + bytes([(i * 17 + 9) % 64, (i * 5 + 3) % 64, (i * 11 + 1) % 64])
        if audio:
            samples = bytes(128 + (i * 7 + j) % 50 for j in range(chunk))
            body += struct.pack("<I", 4 + chunk) + samples
        data.append(body + bytes(8))
    sizes = [len(d) | (i == 0) for i, d in enumerate(data)]
    head = smk_header(
        width,
        height,
        frames,
        rate,
        flags,
        (chunk if audio else 0, 0, 0, 0, 0, 0, 0),
        (PCM_AUDIO if audio else 0, 0, 0, 0, 0, 0, 0),
        len(trees),
        (64, 64, 64, 64),
        sizes,
        [1 | (2 if audio else 0)] * len(sizes),
        body=False,
    )
    return head + trees + b"".join(data)


def movie_install(root: Path, files: dict[str, bytes], bop: bool = True) -> Path:
    """Write ``files`` under ``root`` (and under ``BalanceOfPower/MOVIES``).

    ``files`` maps names inside the movies folder to bytes; the install gets
    its Train folder. Returns ``root``.
    """
    folder = "BalanceOfPower/MOVIES" if bop else "movies"
    return write_files(root, {f"{folder}/{name}": data for name, data in files.items()})


def lossless_movie(
    target: Path, frames: list[bytes], width: int, height: int, audio: bool = False
) -> Path:
    """Write ``frames`` (RGBA bytes each) as a lossless Matroska file; return it.

    Uses ``ffmpeg`` and the FFV1 codec, so decoding gives the same bytes.
    """
    command = ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba"]
    command += ["-s", f"{width}x{height}", "-r", "15", "-i", "-"]
    command += ["-c:v", "ffv1", "-pix_fmt", "bgra", str(target)]
    done = subprocess.run(command, input=b"".join(frames), capture_output=True)
    assert done.returncode == 0, done.stderr.decode()
    return target


def pattern_frames(count: int, width: int, height: int) -> list[bytes]:
    """Return ``count`` distinct RGBA frames, every pixel a known value."""
    frames = []
    for n in range(count):
        pixels = bytearray()
        for y in range(height):
            for x in range(width):
                pixels += bytes(
                    ((x * 40 + n * 7) % 256, (y * 50 + n) % 256, n * 30 % 256, 255)
                )
        frames.append(bytes(pixels))
    return frames


LONG_254 = "z" * 254
LONG_255 = "y" * 255
SUBTITLE_CASES: dict[str, tuple[bytes, list[tuple[int, tuple[str, str, str]]], str]] = {
    "afternumber": (
        b"100 extra words\nA\nB\nC\n",
        [(100, ("extra words", "A", "B"))],
        "bad_number",
    ),
    "big": (
        b"70000\nA\nB\nC\n4294967295\nD\n.\n.\n",
        [(70000, ("A", "B", "C")), (4294967295, ("D", "", ""))],
        "eof",
    ),
    "blankafter": (b"100\n\nB\nC\nD\n", [(100, ("B", "C", "D"))], "eof"),
    "crlf": (
        b"100\r\nA\r\nB\r\nC\r\n250\r\n.\r\n.\r\n.\r\n",
        [(100, ("A", "B", "C")), (250, ("", "", ""))],
        "eof",
    ),
    "dots": (b"100\n.hidden\na.b\n.\n", [(100, ("", "a.b", ""))], "eof"),
    "empty": (b"", [], "eof"),
    "exact254": (
        b"100\n" + b"z" * 254 + b"\nB\nC\n",
        [(100, (LONG_254, "B", "C"))],
        "eof",
    ),
    "exact255": (
        b"100\n" + b"y" * 255 + b"\nB\nC\n",
        [(100, (LONG_255, "", "B"))],
        "bad_number",
    ),
    "latin1": (
        b"100\ncaf\xe9 \xa9 \x7f\nB\nC\n",
        [(100, ("caf\xe9 \xa9 \x7f", "B", "C"))],
        "eof",
    ),
    "leadspace": (
        b"\n\n  100\n  indented\n\tTabbed\nC\n",
        [(100, ("indented", "\tTabbed", "C"))],
        "eof",
    ),
    "lonecr": (b"100\rA\rB\rC\r", [(100, ("A\rB\rC\r", "", ""))], "eof"),
    "long300": (
        b"100\n" + b"x" * 300 + b"\nB\nC\nD\n",
        [(100, ("x" * 255, "x" * 45, "B"))],
        "bad_number",
    ),
    "negative": (b"-5\nA\nB\nC\n", [(4294967291, ("A", "B", "C"))], "eof"),
    "nonewlineend": (b"100\nA\nB\nC", [(100, ("A", "B", "C"))], "eof"),
    "nonnumber": (b"abc\nX\nY\nZ\n", [], "bad_number"),
    "nul": (b"100\nab\x00cd\nB\nC\n", [(100, ("ab", "B", "C"))], "eof"),
    "numeof": (b"100", [(100, ("", "", ""))], "eof"),
    "plain": (
        b"100\nfirst line\nsecond\nthird\n200\n.\n.\n.\n",
        [(100, ("first line", "second", "third")), (200, ("", "", ""))],
        "eof",
    ),
    "plus": (b"+7\nA\nB\nC\n", [(7, ("A", "B", "C"))], "eof"),
    "short": (b"100\nonly one\n", [(100, ("only one", "", ""))], "eof"),
    "twonumbers": (b"100\n200\nA\nB\nC\n", [(100, ("200", "A", "B"))], "bad_number"),
    "unsorted": (
        b"300\nA\n.\n.\n100\nB\n.\n.\n",
        [(300, ("A", "", "")), (100, ("B", "", ""))],
        "eof",
    ),
    "vtff": (b"\x0b\x0c100\x0b\x0c\nA\nB\nC\n", [(100, ("A", "B", "C"))], "eof"),
    "ffinline": (b"100\nA\x0cB\nC\nD\n", [(100, ("A\x0cB", "C", "D"))], "eof"),
    "huge": (
        b"99999999999999999999\nA\nB\nC\n",
        [(4294967295, ("A", "B", "C"))],
        "eof",
    ),
    "over32": (b"5000000000\nA\nB\nC\n", [(705032704, ("A", "B", "C"))], "eof"),
    "signonly": (b"-\nA\nB\nC\n", [], "bad_number"),
    "digitsalpha": (b"100abc\nA\nB\nC\n", [(100, ("abc", "A", "B"))], "bad_number"),
    "plusminus": (b"+-5\nA\nB\nC\n", [], "bad_number"),
    "hex": (b"0x10\nA\nB\nC\n", [(0, ("x10", "A", "B"))], "bad_number"),
    "num65535": (b"100\nA\n.\n.\n65535\nB\n.\n.\n", [(100, ("A", "", ""))], "end_mark"),
    "leadzero": (b"007\nA\nB\nC\n", [(7, ("A", "B", "C"))], "eof"),
    "crlf254": (
        b"100\r\n" + b"q" * 254 + b"\r\nB\r\nC\r\n",
        [(100, ("q" * 254 + "\r", "", "B"))],
        "bad_number",
    ),
    "crlf255": (
        b"100\r\n" + b"w" * 255 + b"\r\nB\r\nC\r\n",
        [(100, ("w" * 255, "", "B"))],
        "bad_number",
    ),
}
"""Each made-up file's bytes, records (number, lines) and why reading stops."""
