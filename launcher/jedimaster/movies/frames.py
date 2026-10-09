"""Decode a movie through ``ffmpeg`` to the bytes the game hands on per frame.

Purpose:
    Run the ``ffmpeg`` and ``ffprobe`` programs (found on the system path)
    to give, frame by frame, the index, start, duration and CRC-32 of the
    picture the game gets: red, green, blue, alpha, 4 bytes a pixel, at the
    stored size, rows top to bottom, never resized. Say clearly when the
    programs are missing.

Flow:
    ``find_tool`` locates a program or raises ``FfmpegMissing``;
    ``iter_rgba`` streams decoded frames from one ``ffmpeg`` run, one frame
    in memory at a time; ``iter_frames`` hashes them; ``frame_rgba`` takes
    one frame; ``probe_video`` asks ``ffprobe`` what the decoder reports.

Invariants:
    - Frames count from 1; frame i starts at (i - 1) times the frame
      duration and lasts one frame duration.
    - The CRC-32 is ``zlib.crc32`` of the frame's RGBA bytes, printed as 8
      lowercase hex digits by the sheets.
    - No frame is dropped or repeated: the decoder's frames are taken as
      they come (``-fps_mode passthrough``).
    - The decoding program is stopped when the reader stops early.

Call:
    ``for frame in iter_frames(path, 640, 360, 66730): print(frame.crc)``
"""

from __future__ import annotations

import json
import logging
import os
import re
import shutil
import subprocess
import tempfile
import zlib
from collections.abc import Iterator
from dataclasses import dataclass
from typing import IO

logger = logging.getLogger(__name__)

FFMPEG = "ffmpeg"
FFPROBE = "ffprobe"
BYTES_PER_PIXEL = 4
VERSION_PATTERN = re.compile(r"version\s+(\S+)")
DECODE_ARGS = ("-map", "0:v:0", "-fps_mode", "passthrough")
RAW_ARGS = ("-f", "rawvideo", "-pix_fmt", "rgba", "-")


class FfmpegMissing(RuntimeError):
    """``ffmpeg`` or ``ffprobe`` is not on the system path."""


class FfmpegFailed(RuntimeError):
    """A run of ``ffmpeg`` or ``ffprobe`` ended with an error."""


@dataclass(frozen=True)
class Frame:
    """One decoded frame: its number from 1, times in microseconds, CRC-32."""

    index: int
    start_us: int
    duration_us: int
    crc: int


def find_tool(name: str) -> str:
    """Return the path of the program ``name`` found on the system path.

    Raises ``FfmpegMissing`` naming the program when there is none. Does
    not run it.
    """
    found = shutil.which(name)
    if found is None:
        raise FfmpegMissing(
            f"{name} was not found on the system path: install FFmpeg "
            f"(the {FFMPEG} and {FFPROBE} programs) to decode the movies"
        )
    return found


def ffmpeg_version() -> str:
    """Return the version word of the ``ffmpeg`` program, e.g. ``6.1.1-3``.

    Raises ``FfmpegMissing`` when the program is absent and ``FfmpegFailed``
    when it does not print a version. Does not check the encoders.
    """
    done = subprocess.run(
        [find_tool(FFMPEG), "-version"], capture_output=True, text=True, check=False
    )
    match = VERSION_PATTERN.search(done.stdout.splitlines()[0] if done.stdout else "")
    if done.returncode != 0 or match is None:
        raise FfmpegFailed(f"{FFMPEG} -version: {done.stderr.strip() or 'no version'}")
    return match.group(1)


def probe_video(path: str | os.PathLike[str]) -> dict[str, str]:
    """Return what ``ffprobe`` reports for the first video stream.

    Keys ``codec_name``, ``width``, ``height`` and ``avg_frame_rate`` (text,
    ``0/0`` when the decoder gives none). Raises ``FfmpegMissing`` or
    ``FfmpegFailed`` (no video stream included). Does not decode a frame.
    """
    done = subprocess.run(
        [
            find_tool(FFPROBE),
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=codec_name,width,height,avg_frame_rate",
            "-of",
            "json",
            os.fspath(path),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    streams = json.loads(done.stdout or "{}").get("streams", [])
    if done.returncode != 0 or not streams:
        raise FfmpegFailed(f"{FFPROBE} {path}: {done.stderr.strip() or 'no video'}")
    return {key: str(value) for key, value in streams[0].items()}


def _read_exact(stream: IO[bytes], size: int) -> bytes:
    chunks = []
    while size > 0:
        chunk = stream.read(size)
        if not chunk:
            break
        chunks.append(chunk)
        size -= len(chunk)
    return b"".join(chunks)


def iter_rgba(
    path: str | os.PathLike[str], width: int, height: int, limit: int | None = None
) -> Iterator[bytes]:
    """Yield each decoded frame's RGBA bytes, ``width * height * 4`` long.

    Stops after ``limit`` frames when given. One ``ffmpeg`` run streams the
    frames, so memory holds one frame. A partial last frame is not yielded.
    Raises ``FfmpegMissing`` when the program is absent and ``FfmpegFailed``
    (with the program's messages) when it exits with an error after the
    frames it gave. Does not check the stored size against the stream's.
    """
    size = width * height * BYTES_PER_PIXEL
    command = [find_tool(FFMPEG), "-nostdin", "-v", "error", "-i", os.fspath(path)]
    command += [*DECODE_ARGS, *RAW_ARGS]
    with tempfile.TemporaryFile() as errors:
        proc = subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=errors, bufsize=0
        )
        assert proc.stdout is not None
        count = 0
        exhausted = False
        try:
            while limit is None or count < limit:
                frame = _read_exact(proc.stdout, size)
                if len(frame) < size:
                    exhausted = True
                    break
                count += 1
                yield frame
        finally:
            if not exhausted:
                proc.kill()
            proc.stdout.close()
            proc.wait()
        if exhausted and proc.returncode != 0:
            errors.seek(0)
            text = errors.read().decode("utf-8", "replace").strip()
            raise FfmpegFailed(f"{FFMPEG} {path}: exit {proc.returncode}: {text}")
    logger.debug("%s: %d frames decoded", path, count)


def iter_frames(
    path: str | os.PathLike[str],
    width: int,
    height: int,
    frame_us: int,
    limit: int | None = None,
) -> Iterator[Frame]:
    """Yield a ``Frame`` for each decoded frame: index, start, duration, CRC.

    Same stops and errors as ``iter_rgba``. Does not compare the count with
    a header's.
    """
    for number, rgba in enumerate(iter_rgba(path, width, height, limit), start=1):
        frame = Frame(number, (number - 1) * frame_us, frame_us, zlib.crc32(rgba))
        logger.debug("frame %d crc=%08x", frame.index, frame.crc)
        yield frame


def frame_rgba(
    path: str | os.PathLike[str], width: int, height: int, index: int
) -> bytes:
    """Return frame ``index`` (from 1) as RGBA bytes.

    Raises ``ValueError`` when the movie has fewer frames, ``FfmpegMissing``
    or ``FfmpegFailed`` as ``iter_rgba`` does. Decodes only up to that frame.
    """
    for number, rgba in enumerate(iter_rgba(path, width, height, index), start=1):
        if number == index:
            return rgba
    raise ValueError(f"{path} has no frame {index}")
