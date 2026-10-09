"""Write the movie export: ``movies.json``, and per movie an MP4, a PNG, a VTT.

Purpose:
    For the page that will play the game's movies: for every name the game
    asks for, write ``NAME.mp4`` (H.264 in ``yuv420p``, AAC at 48,000 Hz,
    at the intended picture size, the moov atom first), ``NAME.png`` (the
    frame at the movie's middle at the intended size) and, with a subtitle
    file, ``NAME.vtt`` (WebVTT of the shown captions); and ``movies.json``
    describing all of it, with the ffmpeg version and settings.

Flow:
    ``export_movies`` asks ``find.movie_names``, resolves each name, reads
    its header and subtitles, writes the files through ``ffmpeg`` and the
    PNG writer, and returns the data it wrote to ``movies.json``.

Invariants:
    - A name asked twice is exported once; output names are the asked name
      with anything but letters, digits, ``_``, ``.``, ``~`` and ``-``
      replaced by ``_``; a name already taken (in any letter case) gets
      ``-2``, ``-3`` and so on.
    - A flagged file's stored rows are each drawn twice, in the MP4 and in
      the PNG.
    - Only the non-empty lines of a caption go into the VTT; ``&``, ``<``
      and ``>`` are escaped and a carriage return becomes a space.
    - A name that does not resolve, or whose header is refused, writes no
      file and is reported in ``movies.json``.

Call:
    ``export_movies(install, Path("export/movies"))``
"""

from __future__ import annotations

import json
import logging
import os
import re
import subprocess
from pathlib import Path
from typing import Any

from ..icons.png import rgba_png
from .find import find_movie
from .find import movie_names
from .frames import BYTES_PER_PIXEL
from .frames import FFMPEG
from .frames import ffmpeg_version
from .frames import FfmpegFailed
from .frames import find_tool
from .frames import frame_rgba
from .header import HeaderInfo
from .header import MovieFormatError
from .header import read_header
from .subtitles import Caption
from .subtitles import read_records
from .subtitles import shown_captions
from .to_json import FOUND
from .to_json import MISSING
from .to_json import movie_entry
from .to_json import movies_to_json
from .to_json import subtitles_to_json
from .to_json import UNREADABLE

logger = logging.getLogger(__name__)

JSON_NAME = "movies.json"
UNSAFE = re.compile(r"[^A-Za-z0-9_.~-]")
DOUBLING = "format=rgb24,scale=iw:2*ih:flags=neighbor"
SETTINGS: dict[str, Any] = {
    "video_codec": "libx264",
    "preset": "medium",
    "crf": 18,
    "pix_fmt": "yuv420p",
    "fps_mode": "passthrough",
    "doubling_filter": DOUBLING,
    "audio_codec": "aac",
    "audio_bitrate": "128k",
    "audio_rate": 48000,
    "movflags": "+faststart",
}
"""The ffmpeg settings the MP4 files are written with."""


def output_stems(names: list[str]) -> dict[str, str]:
    """Return each name's output file stem: safe characters, no repeats.

    A stem already taken in any letter case gets ``-2``, ``-3``... Does not
    touch the file system.
    """
    stems: dict[str, str] = {}
    taken: set[str] = set()
    for name in names:
        base = UNSAFE.sub("_", name) or "_"
        candidate, count = base, 1
        while candidate.casefold() in taken:
            count += 1
            candidate = f"{base}-{count}"
        taken.add(candidate.casefold())
        stems[name] = candidate
    return stems


def mp4_command(
    ffmpeg: str,
    source: str | os.PathLike[str],
    target: str | os.PathLike[str],
    header: HeaderInfo,
) -> list[str]:
    """Return the ``ffmpeg`` command that writes a movie's MP4.

    H.264 and AAC as ``SETTINGS`` says, rows doubled when the header is
    flagged, audio only when the header has a track, the moov atom first.
    Does not run it.
    """
    chain = f"{DOUBLING}," if header.doubled else ""
    command = [ffmpeg, "-nostdin", "-v", "error", "-y", "-i", os.fspath(source)]
    command += ["-map", "0:v:0"]
    command += ["-map", "0:a:0"] if header.audio is not None else []
    command += ["-vf", f"{chain}format={SETTINGS['pix_fmt']}"]
    command += ["-c:v", SETTINGS["video_codec"], "-preset", SETTINGS["preset"]]
    command += ["-crf", str(SETTINGS["crf"]), "-pix_fmt", SETTINGS["pix_fmt"]]
    command += ["-fps_mode", SETTINGS["fps_mode"]]
    if header.audio is not None:
        command += ["-c:a", SETTINGS["audio_codec"], "-b:a", SETTINGS["audio_bitrate"]]
        command += ["-ar", str(SETTINGS["audio_rate"])]
    command += ["-movflags", SETTINGS["movflags"], os.fspath(target)]
    return command


def write_mp4(source: str | os.PathLike[str], target: Path, header: HeaderInfo) -> None:
    """Write the movie's MP4 with ``ffmpeg``.

    Raises ``FfmpegMissing`` when the program is absent and ``FfmpegFailed``
    (with its messages) when it fails. Does not check the result.
    """
    command = mp4_command(find_tool(FFMPEG), source, target, header)
    logger.debug("running %s", command)
    done = subprocess.run(command, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise FfmpegFailed(f"{FFMPEG} {source}: exit {done.returncode}: {done.stderr}")


def double_rows(rgba: bytes, width: int) -> bytes:
    """Return RGBA rows with each row drawn twice (``width`` pixels wide).

    Does not check that the length is a whole number of rows.
    """
    stride = width * BYTES_PER_PIXEL
    out = bytearray()
    for start in range(0, len(rgba), stride):
        row = rgba[start : start + stride]
        out += row + row
    return bytes(out)


def middle_frame(frames: int) -> int:
    """Return the number (from 1) of the frame at the movie's middle.

    The frame being shown at half the duration: ``frames // 2 + 1``.
    """
    return frames // 2 + 1


def write_poster(
    source: str | os.PathLike[str], target: Path, header: HeaderInfo
) -> None:
    """Write the middle frame as a PNG at the intended size.

    Raises ``ValueError`` when the decoder gives fewer frames than the
    header counts, and the ``frames`` module's errors for ``ffmpeg``.
    """
    index = middle_frame(header.frames)
    rgba = frame_rgba(source, header.width, header.height, index)
    if header.doubled:
        rgba = double_rows(rgba, header.width)
    target.write_bytes(rgba_png(header.picture_width, header.picture_height, rgba))


def vtt_time(us: int) -> str:
    """Return ``HH:MM:SS.mmm`` for ``us`` microseconds, rounded to the millisecond."""
    millis = (us + 500) // 1000
    seconds, millis = divmod(millis, 1000)
    minutes, seconds = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"


def vtt_text(text: str) -> str:
    """Return a caption line escaped for WebVTT: ``&``, ``<``, ``>``; CR to space."""
    escaped = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return escaped.replace("\r", " ")


def render_vtt(captions: list[Caption]) -> str:
    """Return a WebVTT file of the captions: only their non-empty lines.

    A caption with no non-empty line is left out. Always starts with
    ``WEBVTT``. Does not check that the times do not overlap.
    """
    blocks = ["WEBVTT\n"]
    for caption in captions:
        lines = [vtt_text(line) for line in caption.lines if line]
        if not lines:
            continue
        timing = f"{vtt_time(caption.start_us)} --> {vtt_time(caption.end_us)}"
        blocks.append("\n".join([timing, *lines]) + "\n")
    return "\n".join(blocks)


def _subtitles(
    label: str, path: Path, header: HeaderInfo | None
) -> tuple[dict[str, Any], list[Caption]]:
    parsed = read_records(path.read_bytes(), label)
    captions = (
        shown_captions(parsed.records, header.frames, header.frame_us)
        if header is not None
        else []
    )
    return subtitles_to_json(label, parsed, captions), captions


def _export_one(
    name: str, stem: str, install: Path, out: Path, bop: bool
) -> dict[str, Any]:
    found = find_movie(install, name, bop)
    header, error, status = None, None, MISSING
    if found.video is not None:
        status = FOUND
        try:
            header = read_header(found.video)
        except MovieFormatError as exc:
            status, error = UNREADABLE, str(exc)
            logger.warning("%s: %s", name, exc)
    subtitles, captions = None, []
    if found.subtitles is not None and found.subtitle_label is not None:
        subtitles, captions = _subtitles(found.subtitle_label, found.subtitles, header)
    files: dict[str, str | None] = {}
    if header is not None and found.video is not None and header.frames > 0:
        files["video"], files["poster"] = f"{stem}.mp4", f"{stem}.png"
        write_mp4(found.video, out / files["video"], header)
        try:
            write_poster(found.video, out / files["poster"], header)
        except ValueError as exc:
            logger.warning("%s: no poster: %s", name, exc)
            files["poster"] = None
        if subtitles is not None:
            files["captions"] = f"{stem}.vtt"
            (out / files["captions"]).write_text(render_vtt(captions), encoding="utf-8")
    logger.info("%s: %s %s", name, status, sorted(k for k, v in files.items() if v))
    return movie_entry(name, status, found.video_label, error, header, subtitles, files)


def export_movies(
    install: str | os.PathLike[str],
    out: str | os.PathLike[str],
    balance_of_power: bool = True,
) -> dict[str, Any]:
    """Write the export under ``out`` and return the data of ``movies.json``.

    Creates ``out``. Raises ``FfmpegMissing`` (before writing anything) when
    ``ffmpeg`` is absent, ``FfmpegFailed`` when a run fails, and ``OSError``
    when a file cannot be written. Does not remove files already in ``out``.
    """
    install, out = Path(install), Path(out)
    version = ffmpeg_version()
    names = movie_names(install, balance_of_power)
    stems = output_stems(names)
    out.mkdir(parents=True, exist_ok=True)
    entries = [
        _export_one(name, stems[name], install, out, balance_of_power) for name in names
    ]
    data = movies_to_json(entries, balance_of_power, version, dict(SETTINGS))
    text = json.dumps(data, ensure_ascii=False, indent=1) + "\n"
    (out / JSON_NAME).write_text(text, encoding="utf-8")
    logger.info("wrote %s: %d movies", out / JSON_NAME, len(entries))
    return data
