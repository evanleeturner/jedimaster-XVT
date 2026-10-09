"""Print a movie's frames and subtitles in the answer sheets' form.

Purpose:
    ``render_frames`` and ``render_subtitles`` print what the two kinds of
    sheet hold, line for line, so a rendering diffs empty against its
    sheet; ``frames_lines`` streams the frames sheet for a movie too long
    to keep whole.

Flow:
    A frames sheet is ``kind``, ``name``, ``file``, ``container``,
    ``video``, ``audio``, ``duration_us``, one ``frame`` line each and a
    ``frames`` count; a subtitles sheet is ``kind``, ``name``, ``file``, a
    ``cue`` line per record and ``end``, or ``kind``, ``name`` and
    ``missing``. Values go through the shared ``quote``.

Invariants:
    - The video codec prints as the decoder names it, ``smackvid``; the
      audio codec by the header's track (``smackaud`` when compressed).
    - ``display`` is the size a decoder that honours the flags reports:
      half the width when the file is flagged doubled, else the stored size.
    - Every rendering ends with one newline.

Call:
    ``render_subtitles("Flyby1a", "movies/Flyby1a.txt", read_records(data))``
"""

from __future__ import annotations

import logging
from collections.abc import Iterable
from collections.abc import Iterator

from ..text.sheet import join
from ..text.sheet import quote
from .find import FOLDER
from .find import SUBTITLE_SUFFIX
from .find import VIDEO_SUFFIX
from .frames import Frame
from .header import HeaderInfo
from .subtitles import Caption
from .subtitles import Record
from .subtitles import SubtitleFile

logger = logging.getLogger(__name__)

VIDEO_CODEC = "smackvid"
CONTAINER = "smk"
NO_RATE = "0/0"


def video_name(name: str) -> str:
    """Return the sheet's ``name`` value of a movie: ``movies/NAME.smk``."""
    return f"{FOLDER}/{name}{VIDEO_SUFFIX}"


def subtitle_name(name: str) -> str:
    """Return the sheet's ``name`` value of a subtitle file: ``movies/NAME.txt``."""
    return f"{FOLDER}/{name}{SUBTITLE_SUFFIX}"


def frames_lines(
    name: str,
    label: str,
    header: HeaderInfo,
    frames: Iterable[Frame],
    rate: str = NO_RATE,
) -> Iterator[str]:
    """Yield the frames sheet's lines, the frame lines as ``frames`` come.

    ``name`` is the name asked, ``label`` the resolved file's label, ``rate``
    the decoder's frame rate text. The ``frames`` line counts the frames
    given, not the header's count. Does not decode anything itself.
    """
    yield "kind frames"
    yield f"name {quote(video_name(name))}"
    yield f"file {quote(label)}"
    yield f"container {CONTAINER}"
    yield (
        f"video {VIDEO_CODEC} width={header.width} height={header.height} "
        f"display={header.display_width}x{header.height} rate={rate}"
    )
    track = header.audio
    if track is None:
        yield "audio none"
    else:
        yield f"audio {track.codec} rate={track.rate} channels={track.channels}"
    yield f"duration_us {header.duration_us}"
    count = 0
    for frame in frames:
        count += 1
        yield (
            f"frame {frame.index} pts_us={frame.start_us} "
            f"duration_us={frame.duration_us} rgba={frame.crc:08x}"
        )
    yield f"frames {count}"


def render_frames(
    name: str,
    label: str,
    header: HeaderInfo,
    frames: Iterable[Frame],
    rate: str = NO_RATE,
) -> str:
    """Return the whole frames sheet as text.

    See ``frames_lines``. Holds the whole sheet in memory.
    """
    return join(list(frames_lines(name, label, header, frames, rate)))


def cue_line(record: Record) -> str:
    """Return one record's ``cue`` line: its number and three quoted lines."""
    lines = " ".join(quote(line) for line in record.lines)
    return f"cue frame={record.number} {lines}"


def render_subtitles(
    name: str, label: str | None, subtitles: SubtitleFile | None
) -> str:
    """Return the subtitles sheet; ``missing`` when ``subtitles`` is None.

    ``label`` is the resolved file's label. With ``subtitles`` None the
    sheet is ``kind``, ``name`` and ``missing``. Does not say why reading
    stopped (the sheets do not).
    """
    head = ["kind subtitles", f"name {quote(subtitle_name(name))}"]
    if subtitles is None:
        return join([*head, "missing"])
    lines = [*head, f"file {quote(label or '')}"]
    lines += [cue_line(record) for record in subtitles.records]
    lines.append(f"end cues={len(subtitles.records)}")
    return join(lines)


def caption_line(caption: Caption) -> str:
    """Return one shown caption as a line: frames, times, quoted lines."""
    lines = " ".join(quote(line) for line in caption.lines)
    return (
        f"caption frames={caption.start_frame}-{caption.end_frame} "
        f"us={caption.start_us}-{caption.end_us} {lines}"
    )


def render_captions(
    label: str, subtitles: SubtitleFile, captions: list[Caption], frames: int
) -> str:
    """Return a subtitle file's records and shown captions, without a movie.

    ``frames`` is the frame count the captions were worked out over. This
    form is the package's own: no sheet shows it.
    """
    lines = [f"file {quote(label)}", f"frames {frames}"]
    lines += [cue_line(record) for record in subtitles.records]
    lines.append(f"end cues={len(subtitles.records)} reason={subtitles.reason}")
    lines += [caption_line(caption) for caption in captions]
    lines.append(f"captions {len(captions)}")
    return join(lines)
