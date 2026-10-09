"""The movies sub-package: the game's movies and their subtitle files.

Written from the game's files and notes describing how the game finds,
decodes and captions them, checked against the game's own reading.

Purpose:
    Find the movies of X-Wing vs. TIE Fighter and Balance of Power the way
    the game asks for them, read each Smacker header, decode the frames
    through ``ffmpeg`` to the bytes the game hands on, read the subtitle
    files and work out when each caption shows, print all of it in the
    answer sheets' format, and export MP4, PNG, WebVTT and JSON for a page.

Flow:
    ``find`` resolves names; ``header`` reads a file's header; ``frames``
    decodes; ``subtitles`` reads records and times captions; ``render``
    prints the sheets; ``to_json`` and ``export`` write the export; ``cli``
    is the ``movies`` command.

Invariants:
    - Standard library only, plus the ``ffmpeg`` and ``ffprobe`` programs
      for decoding and export; everything else works without them.
    - No game data in this sub-package's code.

Call:
    ``from jedimaster.movies import read_header, read_records``
"""

from __future__ import annotations

import logging

from .export import export_movies
from .export import render_vtt
from .find import find_movie
from .find import movie_names
from .frames import FfmpegFailed
from .frames import FfmpegMissing
from .frames import Frame
from .frames import iter_frames
from .header import HeaderInfo
from .header import MovieFormatError
from .header import read_header
from .render import render_frames
from .render import render_subtitles
from .subtitles import Caption
from .subtitles import read_records
from .subtitles import Record
from .subtitles import shown_captions
from .subtitles import SubtitleFile
from .to_json import movies_schema
from .to_json import movies_to_json

logger = logging.getLogger(__name__)

__all__ = [
    "Caption",
    "FfmpegFailed",
    "FfmpegMissing",
    "Frame",
    "HeaderInfo",
    "MovieFormatError",
    "Record",
    "SubtitleFile",
    "export_movies",
    "find_movie",
    "iter_frames",
    "movie_names",
    "movies_schema",
    "movies_to_json",
    "read_header",
    "read_records",
    "render_frames",
    "render_subtitles",
    "render_vtt",
    "shown_captions",
]
