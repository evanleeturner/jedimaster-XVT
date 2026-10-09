"""Command line of the movies: ``python -m jedimaster movies <command>``.

Purpose:
    ``movies dump frames|subtitles <install> <name>`` prints a movie's
    frames sheet or its subtitle file's sheet; ``movies export <install>
    <out-folder>`` writes ``movies.json`` and each movie's MP4, PNG and
    VTT; ``movies captions <file.txt>`` prints a subtitle file's records
    and shown captions without a movie.

Flow:
    ``add_parser`` adds the ``movies`` command to the package's parser;
    ``run`` dispatches to ``_dump_frames``, ``_dump_subtitles``,
    ``_export`` or ``_captions``.

Invariants:
    - ``print`` and ``sys.stdout`` carry only the command's output;
      diagnostics go to the logging module.
    - ``dump frames`` prints nothing and exits 1 for a name that does not
      resolve (and for a header that is refused); ``dump subtitles`` prints
      the ``missing`` sheet and exits 0 for a subtitle file that does not
      resolve.
    - Exit status: 0 on success, 1 when a movie or file cannot be read or
      written or ``ffmpeg`` is missing or fails, 2 when the install is not
      found.

Call:
    ``python -m jedimaster movies dump frames <install> Opening``
"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from ..install import find_install
from .export import export_movies
from .find import find_movie
from .frames import FFMPEG
from .frames import FfmpegFailed
from .frames import FfmpegMissing
from .frames import find_tool
from .frames import iter_frames
from .frames import probe_video
from .header import MovieFormatError
from .header import read_header
from .render import frames_lines
from .render import render_captions
from .render import render_subtitles
from .subtitles import read_records
from .subtitles import shown_captions

logger = logging.getLogger(__name__)

DEFAULT_FRAME_US = 66730


def _balance_flag(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--no-balance-of-power",
        action="store_true",
        help="read the install as if Balance of Power were absent",
    )


def add_parser(sub: argparse._SubParsersAction) -> None:
    """Add the ``movies`` command and its subcommands to ``sub``.

    Returns None. Does not parse anything.
    """
    movies = sub.add_parser("movies", help="read the game's movies and subtitles")
    movies_sub = movies.add_subparsers(dest="movies_command", required=True)
    dump = movies_sub.add_parser("dump", help="print a movie's sheet")
    kinds = dump.add_subparsers(dest="dump_kind", required=True)
    for kind, text in (("frames", "frames sheet"), ("subtitles", "subtitles sheet")):
        one = kinds.add_parser(kind, help=f"print a movie's {text}")
        one.add_argument("install", help="the install folder")
        one.add_argument("name", help="the movie's name, as the game asks for it")
        _balance_flag(one)
    export = movies_sub.add_parser("export", help="write movies.json, MP4, PNG, VTT")
    export.add_argument("install", help="the install folder")
    export.add_argument("out", help="the output folder")
    _balance_flag(export)
    captions = movies_sub.add_parser(
        "captions", help="print a subtitle file's captions"
    )
    captions.add_argument("file", help="a subtitle file (.txt)")
    captions.add_argument(
        "--frames", type=int, help="the movie's frame count (default: the last number)"
    )
    captions.add_argument(
        "--frame-us",
        type=int,
        default=DEFAULT_FRAME_US,
        help=f"one frame's duration in microseconds (default {DEFAULT_FRAME_US})",
    )


def _install(args: argparse.Namespace) -> Path | None:
    install = find_install(args.install)
    if install is None:
        logger.error("not an install: %s", args.install)
    return install


def _dump_frames(args: argparse.Namespace) -> int:
    install = _install(args)
    if install is None:
        return 2
    found = find_movie(install, args.name, not args.no_balance_of_power)
    if found.video is None or found.video_label is None:
        logger.error("movie %s does not resolve under %s", args.name, install)
        return 1
    try:
        find_tool(FFMPEG)
        header = read_header(found.video)
        rate = probe_video(found.video)["avg_frame_rate"]
    except (MovieFormatError, FfmpegMissing, FfmpegFailed, OSError) as exc:
        logger.error("cannot read %s: %s", found.video, exc)
        return 1
    frames = iter_frames(
        found.video, header.width, header.height, header.frame_us, header.frames
    )
    try:
        for line in frames_lines(args.name, found.video_label, header, frames, rate):
            sys.stdout.write(line + "\n")
    except (FfmpegFailed, OSError) as exc:
        logger.error("cannot decode %s: %s", found.video, exc)
        return 1
    return 0


def _dump_subtitles(args: argparse.Namespace) -> int:
    install = _install(args)
    if install is None:
        return 2
    found = find_movie(install, args.name, not args.no_balance_of_power)
    parsed = None
    if found.subtitles is not None:
        try:
            parsed = read_records(
                found.subtitles.read_bytes(), found.subtitle_label or ""
            )
        except OSError as exc:
            logger.error("cannot read %s: %s", found.subtitles, exc)
            return 1
    sys.stdout.write(render_subtitles(args.name, found.subtitle_label, parsed))
    return 0


def _export(args: argparse.Namespace) -> int:
    install = _install(args)
    if install is None:
        return 2
    try:
        data = export_movies(install, args.out, not args.no_balance_of_power)
    except (FfmpegMissing, FfmpegFailed, OSError) as exc:
        logger.error("cannot export to %s: %s", args.out, exc)
        return 1
    found = sum(1 for movie in data["movies"] if movie["status"] == "found")
    print(
        f"exported {found} of {len(data['movies'])} movies and movies.json to {args.out}"
    )
    return 0


def _captions(args: argparse.Namespace) -> int:
    path = Path(args.file)
    try:
        parsed = read_records(path.read_bytes(), str(path))
    except OSError as exc:
        logger.error("cannot read %s: %s", path, exc)
        return 1
    if args.frames is not None:
        frames = args.frames
    else:
        frames = max((r.number for r in parsed.records), default=0)
    captions = shown_captions(parsed.records, frames, args.frame_us)
    sys.stdout.write(render_captions(str(path), parsed, captions, frames))
    return 0


def run(args: argparse.Namespace) -> int:
    """Run a ``movies`` subcommand; return its exit status.

    Returns 0 on success, 1 when a movie or file cannot be read or written
    or ``ffmpeg`` is missing or fails, 2 when the install is not found.
    Does not catch errors other than those.
    """
    if args.movies_command == "dump":
        if args.dump_kind == "frames":
            return _dump_frames(args)
        return _dump_subtitles(args)
    if args.movies_command == "export":
        return _export(args)
    return _captions(args)
