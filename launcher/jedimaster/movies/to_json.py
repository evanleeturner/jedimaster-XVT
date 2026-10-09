"""Describe the movie export as JSON data, and publish its JSON Schema.

Purpose:
    Give the page that will play the movies one ``movies.json``: every name
    the game asks for, where it resolved or that it did not, the header's
    fields, the picture's intended size, frames, frame duration, duration,
    the audio track, the subtitle records and shown captions, and the files
    written; and publish the JSON Schema (draft 2020-12) the export always
    satisfies.

Flow:
    ``movie_entry`` builds one name's entry from its parts; ``movies_to_json``
    wraps the entries with the format marker and the ffmpeg settings;
    ``movies_schema`` builds the schema with the text export's helpers.

Invariants:
    - Every name asked for has exactly one entry, in the asked order.
    - Text values keep every byte as one character (Latin-1 characters of
      the subtitle files), written as they are.
    - ``status`` is ``found``, ``missing`` (the name does not resolve) or
      ``unreadable`` (it resolves but the header is refused; ``error`` says
      why). Only ``found`` entries have a header, sizes and files.
    - Captions are worked out over the header's frame count; a name with
      no movie has none.

Call:
    ``movies_to_json(entries, balance_of_power, version, settings)``
"""

from __future__ import annotations

import logging
from typing import Any

from ..text.to_json import closed_object
from ..text.to_json import ints
from ..text.to_json import nullable
from .header import HeaderInfo
from .subtitles import Caption
from .subtitles import REASONS
from .subtitles import Record
from .subtitles import SubtitleFile

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/movies.schema.json"
JSON_FORMAT = "jedimaster.movies"
JSON_FORMAT_VERSION = 1
FOUND = "found"
MISSING = "missing"
UNREADABLE = "unreadable"
STATUSES = (FOUND, MISSING, UNREADABLE)
SIGNATURES = ("SMK2", "SMK4")


def header_to_json(header: HeaderInfo) -> dict[str, Any]:
    """Return the header's fields as JSON-ready data.

    Per-frame tables are summarised (key frames, palette frames, bytes),
    not listed. Always returns a new dict. Does not decode anything.
    """
    sizes = header.frame_sizes
    return {
        "signature": header.signature,
        "width": header.width,
        "height": header.height,
        "frames": header.frames,
        "rate": header.rate,
        "flags": header.flags,
        "ring": header.ring,
        "interlaced": header.interlaced,
        "y_doubled": header.y_doubled,
        "tree_bytes": header.tree_bytes,
        "tree_sizes": list(header.tree_sizes),
        "tracks": [
            {
                "index": t.index,
                "buffer_size": t.buffer_size,
                "word": t.word,
                "rate": t.rate,
                "compressed": t.compressed,
                "has_data": t.has_data,
                "sixteen_bit": t.sixteen_bit,
                "stereo": t.stereo,
                "compression": t.compression,
            }
            for t in header.tracks
        ],
        "table_frames": len(sizes),
        "key_frames": sum(1 for size in sizes if size & 1),
        "palette_frames": sum(1 for kind in header.frame_types if kind & 1),
        "frame_bytes": sum(size & ~3 for size in sizes),
    }


def subtitles_to_json(
    label: str, subtitles: SubtitleFile, captions: list[Caption]
) -> dict[str, Any]:
    """Return a subtitle file's records and shown captions as JSON data."""
    return {
        "file": label,
        "reason": subtitles.reason,
        "records": [_record(r) for r in subtitles.records],
        "captions": [_caption(c) for c in captions],
    }


def _record(record: Record) -> dict[str, Any]:
    return {"number": record.number, "lines": list(record.lines)}


def _caption(caption: Caption) -> dict[str, Any]:
    return {
        "record": caption.record,
        "start_frame": caption.start_frame,
        "end_frame": caption.end_frame,
        "start_us": caption.start_us,
        "end_us": caption.end_us,
        "lines": list(caption.lines),
    }


def movie_entry(
    name: str,
    status: str,
    label: str | None,
    error: str | None,
    header: HeaderInfo | None,
    subtitles: dict[str, Any] | None,
    files: dict[str, str | None],
) -> dict[str, Any]:
    """Return one asked-for name's entry.

    ``header`` None gives null size, timing, header and audio. ``files``
    names ``video``, ``poster`` and ``captions`` (None for one not written).
    Does not check that ``status`` agrees with the other parts.
    """
    track = header.audio if header is not None else None
    return {
        "name": name,
        "status": status,
        "file": label,
        "error": error,
        "header": header_to_json(header) if header is not None else None,
        "picture_width": header.picture_width if header is not None else None,
        "picture_height": header.picture_height if header is not None else None,
        "frames": header.frames if header is not None else None,
        "frame_us": header.frame_us if header is not None else None,
        "duration_us": header.duration_us if header is not None else None,
        "audio": (
            {
                "track": track.index,
                "codec": track.codec,
                "rate": track.rate,
                "channels": track.channels,
                "bits": 16 if track.sixteen_bit else 8,
            }
            if track is not None
            else None
        ),
        "subtitles": subtitles,
        "files": {key: files.get(key) for key in ("video", "poster", "captions")},
    }


def movies_to_json(
    entries: list[dict[str, Any]],
    balance_of_power: bool,
    ffmpeg_version: str,
    settings: dict[str, Any],
) -> dict[str, Any]:
    """Return the export as JSON-ready data: marker, ffmpeg, then the entries.

    ``settings`` is the ffmpeg settings used to write the MP4 files. Always
    returns a new dict. Does not write any file.
    """
    logger.debug("exported %d movie entries", len(entries))
    return {
        "format": JSON_FORMAT,
        "format_version": JSON_FORMAT_VERSION,
        "balance_of_power": balance_of_power,
        "ffmpeg": {"version": ffmpeg_version, "settings": settings},
        "movies": entries,
    }


SIZE = {"type": "integer", "minimum": 0}
LINES = {
    "type": "array",
    "items": {"type": "string"},
    "minItems": 3,
    "maxItems": 3,
}


def _header_defs() -> dict[str, Any]:
    """Return the schema definitions of the header and its audio tracks."""
    track = closed_object(
        "One audio track of the header: buffer, rate word and its parts.",
        {
            **ints("index", maximum=6),
            **ints("buffer_size", "word", maximum=0xFFFFFFFF),
            **ints("rate", maximum=0xFFFFFF),
            "compressed": {"type": "boolean"},
            "has_data": {"type": "boolean"},
            "sixteen_bit": {"type": "boolean"},
            "stereo": {"type": "boolean"},
            **ints("compression", maximum=3),
        },
    )
    header = closed_object(
        "The Smacker header's fields; the per-frame tables are summarised.",
        {
            "signature": {"enum": list(SIGNATURES)},
            **ints("width", "height", "frames", maximum=0xFFFFFFFF),
            "rate": {"type": "integer", "minimum": -(2**31), "maximum": 2**31 - 1},
            **ints("flags", "tree_bytes", maximum=0xFFFFFFFF),
            "ring": {"type": "boolean"},
            "interlaced": {"type": "boolean"},
            "y_doubled": {"type": "boolean"},
            "tree_sizes": {
                "type": "array",
                "items": SIZE,
                "minItems": 4,
                "maxItems": 4,
            },
            "tracks": {
                "type": "array",
                "items": {"$ref": "#/$defs/Track"},
                "minItems": 7,
                "maxItems": 7,
            },
            **ints("table_frames", "key_frames", "palette_frames", "frame_bytes"),
        },
    )
    return {"Track": track, "Header": header}


def _subtitle_defs() -> dict[str, Any]:
    """Return the schema definitions of the subtitle records and captions."""
    record = closed_object(
        "One subtitle record: its number and three lines.",
        {**ints("number", maximum=0xFFFFFFFF), "lines": LINES},
    )
    caption = closed_object(
        "One caption as shown: record index, frames, microseconds, lines.",
        {
            **ints("record", "start_us", "end_us"),
            **ints("start_frame", "end_frame", minimum=1),
            "lines": LINES,
        },
    )
    subtitles = closed_object(
        "A movie's subtitle file: records, why reading stopped, shown captions.",
        {
            "file": {"type": "string"},
            "reason": {"enum": list(REASONS)},
            "records": {"type": "array", "items": {"$ref": "#/$defs/Record"}},
            "captions": {"type": "array", "items": {"$ref": "#/$defs/Caption"}},
        },
    )
    return {"Record": record, "Caption": caption, "Subtitles": subtitles}


def _movie_defs() -> dict[str, Any]:
    """Return the schema definitions of the audio, the files and one movie."""
    audio = closed_object(
        "The audio track the decoder plays.",
        {
            **ints("track", maximum=6),
            "codec": {"type": "string"},
            **ints("rate", maximum=0xFFFFFF),
            **ints("channels", minimum=1, maximum=2),
            "bits": {"enum": [8, 16]},
        },
    )
    files = closed_object(
        "The files written for the movie, by name in the export folder.",
        {
            key: nullable({"type": "string", "pattern": pattern})
            for key, pattern in (
                ("video", r"^[A-Za-z0-9_.~-]+\.mp4$"),
                ("poster", r"^[A-Za-z0-9_.~-]+\.png$"),
                ("captions", r"^[A-Za-z0-9_.~-]+\.vtt$"),
            )
        },
    )
    movie = closed_object(
        "One name the game asks for: where it resolved, the header, the files.",
        {
            "name": {"type": "string"},
            "status": {"enum": list(STATUSES)},
            "file": nullable({"type": "string"}),
            "error": nullable({"type": "string"}),
            "header": nullable({"$ref": "#/$defs/Header"}),
            "picture_width": nullable(SIZE),
            "picture_height": nullable(SIZE),
            "frames": nullable(SIZE),
            "frame_us": nullable(SIZE),
            "duration_us": nullable(SIZE),
            "audio": nullable({"$ref": "#/$defs/Audio"}),
            "subtitles": nullable({"$ref": "#/$defs/Subtitles"}),
            "files": {"$ref": "#/$defs/Files"},
        },
    )
    return {"Audio": audio, "Files": files, "Movie": movie}


def _defs() -> dict[str, Any]:
    """Return every schema definition the export schema refers to."""
    return {**_header_defs(), **_subtitle_defs(), **_movie_defs()}


def movies_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``movies_to_json`` output.

    Always returns the same dict. Does not encode cross-field rules (that a
    ``found`` entry has a header, or that the files exist).
    """
    top = closed_object(
        "The game's movies and their subtitles (jedimaster export).",
        {
            "format": {"const": JSON_FORMAT},
            "format_version": {"const": JSON_FORMAT_VERSION},
            "balance_of_power": {"type": "boolean"},
            "ffmpeg": closed_object(
                "The ffmpeg version and the settings that wrote the MP4 files.",
                {
                    "version": {"type": "string"},
                    "settings": {"type": "object"},
                },
            ),
            "movies": {"type": "array", "items": {"$ref": "#/$defs/Movie"}},
        },
    )
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP movies and subtitles (jedimaster export)",
        **top,
        "$defs": _defs(),
    }
