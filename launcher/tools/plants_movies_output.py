"""The planted faults of the movie outputs: sheets, JSON, schema, export, command.

Purpose:
    Break one rule of ``jedimaster/movies/``'s outputs per plant: each line
    of the frames and subtitles sheets and of the captions print; the JSON
    fields and each schema rule the rejection test names; the MP4's
    settings, the poster, the WebVTT text and the files and statuses of the
    export; the ``movies`` command line's exit statuses and output.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``movies-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant that changes the schema builder regenerates the schema files,
      so it fails the rejection test it names, not the drift test.

Call:
    ``from plants_movies_output import MOVIES_OUTPUT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

RN = "jedimaster/movies/render.py"
JS = "jedimaster/movies/to_json.py"
EX = "jedimaster/movies/export.py"
CL = "jedimaster/movies/cli.py"
MN = "jedimaster/__main__.py"
T_R = "tests/test_movies_frames.py::"
T_S = "tests/test_movies_subtitles.py::"
T_E = "tests/test_movies_export.py::"
T_C = "tests/test_movies_cli.py::"
SHEET = T_R + "test_frames_sheet_prints_in_the_sheets_form"
FLAGGED = T_R + "test_a_flagged_file_displays_half_its_width_and_no_audio_prints_none"
REJECTS = T_E + "test_schema_rejects_bad_data"
JSON_FILE = T_E + "test_movies_json_is_written_and_validates"
ENTRY = T_E + "test_entry_reports_header_size_timing_and_audio"
STARTS = T_E + "test_a_sample_validates"


def _plant(pid: str, path: str, old: str, new: str, *expect: str, regen=False) -> Plant:
    return Plant(f"movies-{pid}", path, old, new, expect, regen)


RENDER_PLANTS: list[Plant] = [
    _plant(
        "render-codec",
        RN,
        'VIDEO_CODEC = "smackvid"',
        'VIDEO_CODEC = "smackvideo"',
        SHEET,
    ),
    _plant("render-container", RN, 'CONTAINER = "smk"', 'CONTAINER = "smacker"', SHEET),
    _plant(
        "render-display",
        RN,
        'f"display={header.display_width}x{header.height} rate={rate}"',
        'f"display={header.width}x{header.height} rate={rate}"',
        FLAGGED,
    ),
    _plant(
        "render-rate",
        RN,
        'f"display={header.display_width}x{header.height} rate={rate}"',
        'f"display={header.display_width}x{header.height} rate={NO_RATE}"',
        FLAGGED,
    ),
    _plant("render-audio-none", RN, 'yield "audio none"', 'yield "audio"', FLAGGED),
    _plant(
        "render-audio-channels",
        RN,
        "channels={track.channels}",
        "channels={track.rate}",
        SHEET,
    ),
    _plant(
        "render-duration",
        RN,
        'yield f"duration_us {header.duration_us}"',
        'yield f"duration_us {header.frames}"',
        SHEET,
    ),
    _plant("render-crc-width", RN, "rgba={frame.crc:08x}", "rgba={frame.crc:x}", SHEET),
    _plant(
        "render-frame-number",
        RN,
        'f"frame {frame.index} pts_us={frame.start_us} "',
        'f"frame {frame.index + 1} pts_us={frame.start_us} "',
        SHEET,
    ),
    _plant(
        "render-pts",
        RN,
        'f"frame {frame.index} pts_us={frame.start_us} "',
        'f"frame {frame.index} pts_us={frame.duration_us} "',
        SHEET,
    ),
    _plant(
        "render-frames-count",
        RN,
        'yield f"frames {count}"',
        'yield f"frames {header.frames}"',
        T_R + "test_the_frames_line_counts_the_frames_given_not_the_headers",
    ),
    _plant(
        "render-name",
        RN,
        'yield f"name {quote(video_name(name))}"',
        'yield f"name {quote(name)}"',
        SHEET,
    ),
    _plant(
        "render-file",
        RN,
        'yield f"file {quote(label)}"',
        'yield f"file {quote(name)}"',
        SHEET,
    ),
    _plant(
        "render-subtitle-name",
        RN,
        'return f"{FOLDER}/{name}{SUBTITLE_SUFFIX}"',
        'return f"{FOLDER}/{name}{VIDEO_SUFFIX}"',
        T_S + "test_sheet_of_a_made_up_file_renders_as_the_sheets_do",
    ),
    _plant(
        "render-missing",
        RN,
        'return join([*head, "missing"])',
        'return join([*head, "absent"])',
        T_S + "test_sheet_of_a_made_up_file_renders_as_the_sheets_do",
    ),
    _plant(
        "render-cue-number",
        RN,
        'return f"cue frame={record.number} {lines}"',
        'return f"cue frame={record.number + 1} {lines}"',
        T_S + "test_sheet_of_a_made_up_file_renders_as_the_sheets_do",
    ),
    _plant(
        "render-end-count",
        RN,
        'lines.append(f"end cues={len(subtitles.records)}")',
        'lines.append("end cues=0")',
        T_S + "test_sheet_of_a_made_up_file_renders_as_the_sheets_do",
    ),
    _plant(
        "render-subtitle-file",
        RN,
        "    lines = [*head, f\"file {quote(label or '')}\"]",
        "    lines = [*head]",
        T_S + "test_sheet_of_a_made_up_file_renders_as_the_sheets_do",
    ),
    _plant(
        "render-captions-reason",
        RN,
        'f"end cues={len(subtitles.records)} reason={subtitles.reason}"',
        'f"end cues={len(subtitles.records)} reason=eof"',
        T_C + "test_captions_prints_records_and_shown_captions_without_a_movie",
    ),
    _plant(
        "render-captions-count",
        RN,
        'lines.append(f"captions {len(captions)}")',
        'lines.append("captions 0")',
        T_C + "test_captions_prints_records_and_shown_captions_without_a_movie",
    ),
    _plant(
        "render-caption-times",
        RN,
        'f"us={caption.start_us}-{caption.end_us} {lines}"',
        'f"us={caption.start_us}-{caption.start_us} {lines}"',
        T_C + "test_captions_prints_records_and_shown_captions_without_a_movie",
    ),
]

JSON_PLANTS: list[Plant] = [
    _plant(
        "json-key-frames",
        JS,
        '"key_frames": sum(1 for size in sizes if size & 1),',
        '"key_frames": sum(1 for size in sizes if size & 2),',
        ENTRY,
    ),
    _plant(
        "json-palette-frames",
        JS,
        '"palette_frames": sum(1 for kind in header.frame_types if kind & 1),',
        '"palette_frames": sum(1 for kind in header.frame_types if kind & 2),',
        ENTRY,
    ),
    _plant(
        "json-frame-bytes",
        JS,
        '"frame_bytes": sum(size & ~3 for size in sizes),',
        '"frame_bytes": sum(size for size in sizes),',
        ENTRY,
    ),
    _plant(
        "json-table-frames",
        JS,
        '"table_frames": len(sizes),',
        '"table_frames": header.frames + 1,',
        ENTRY,
    ),
    _plant(
        "json-tracks",
        JS,
        "            for t in header.tracks\n",
        "            for t in header.tracks[:1]\n",
        ENTRY,
    ),
    _plant(
        "json-audio-bits",
        JS,
        '"bits": 16 if track.sixteen_bit else 8,',
        '"bits": 8 if track.sixteen_bit else 16,',
        ENTRY,
    ),
    _plant(
        "json-audio-codec",
        JS,
        '"codec": track.codec,',
        '"codec": "smackaud",',
        ENTRY,
    ),
    _plant(
        "json-picture-height",
        JS,
        '"picture_height": header.picture_height if header is not None else None,',
        '"picture_height": header.height if header is not None else None,',
        ENTRY,
    ),
    _plant(
        "json-frame-us",
        JS,
        '"frame_us": header.frame_us if header is not None else None,',
        '"frame_us": header.rate if header is not None else None,',
        ENTRY,
    ),
    _plant(
        "json-duration",
        JS,
        '"duration_us": header.duration_us if header is not None else None,',
        '"duration_us": header.frames if header is not None else None,',
        ENTRY,
    ),
    _plant(
        "json-balance-of-power",
        JS,
        '"balance_of_power": balance_of_power,',
        '"balance_of_power": True,',
        T_E + "test_the_view_without_balance_of_power_reports_every_name_missing",
    ),
    _plant(
        "json-ffmpeg-settings",
        JS,
        '"ffmpeg": {"version": ffmpeg_version, "settings": settings},',
        '"ffmpeg": {"version": ffmpeg_version, "settings": {}},',
        T_E + "test_the_ffmpeg_version_and_settings_are_recorded",
    ),
    _plant(
        "json-records",
        JS,
        '"records": [_record(r) for r in subtitles.records],',
        '"records": [_record(r) for r in subtitles.records[:1]],',
        T_E + "test_subtitle_records_and_captions_are_in_the_json",
    ),
    _plant(
        "json-caption-end",
        JS,
        '"end_us": caption.end_us,',
        '"end_us": caption.start_us,',
        T_E + "test_subtitle_records_and_captions_are_in_the_json",
    ),
    _plant(
        "json-record-number",
        JS,
        '"number": record.number,',
        '"number": record.number + 1,',
        T_E + "test_subtitle_records_and_captions_are_in_the_json",
    ),
    _plant(
        "json-schema-title-drift",
        JS,
        '"title": "XvT/BoP movies and subtitles (jedimaster export)",',
        '"title": "movies",',
        T_E + "test_schema_file_matches_model",
    ),
    _plant(
        "json-schema-format",
        JS,
        '"format": {"const": JSON_FORMAT},',
        '"format": {"type": "string"},',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-status",
        JS,
        '"status": {"enum": list(STATUSES)},',
        '"status": {"type": "string"},',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-signature",
        JS,
        '"signature": {"enum": list(SIGNATURES)},',
        '"signature": {"type": "string"},',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-tracks-count",
        JS,
        '                "minItems": 7,\n',
        "",
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-bits",
        JS,
        '"bits": {"enum": [8, 16]},',
        '"bits": {"type": "integer"},',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-video-name",
        JS,
        '("video", r"^[A-Za-z0-9_.~-]+\\.mp4$"),',
        '("video", r"^.*$"),',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-reason",
        JS,
        '"reason": {"enum": list(REASONS)},',
        '"reason": {"type": "string"},',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-start-frame",
        JS,
        '**ints("start_frame", "end_frame", minimum=1),',
        '**ints("start_frame", "end_frame", minimum=0),',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-lines",
        JS,
        '    "minItems": 3,\n    "maxItems": 3,\n',
        "",
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-open-movie",
        JS,
        'return {"Audio": audio, "Files": files, "Movie": movie}',
        'movie["additionalProperties"] = True\n    return {"Audio": audio, "Files": files, "Movie": movie}',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-schema-version",
        JS,
        '"version": {"type": "string"},',
        '"version": {},',
        REJECTS,
        regen=True,
    ),
    _plant(
        "json-sample-balance",
        JS,
        '"format_version": JSON_FORMAT_VERSION,\n        "balance_of_power": balance_of_power,',
        '"format_version": JSON_FORMAT_VERSION,\n        "balance_of_power": str(balance_of_power),',
        STARTS,
    ),
]

EXPORT_PLANTS: list[Plant] = [
    _plant(
        "export-unsafe-characters",
        EX,
        'UNSAFE = re.compile(r"[^A-Za-z0-9_.~-]")',
        'UNSAFE = re.compile(r"[^A-Za-z0-9_.~/-]")',
        T_E + "test_output_stems_are_safe_and_never_repeat_in_any_case",
    ),
    _plant(
        "export-stems-case",
        EX,
        "        while candidate.casefold() in taken:",
        "        while candidate in taken:",
        T_E + "test_output_stems_are_safe_and_never_repeat_in_any_case",
    ),
    _plant(
        "export-stem-empty",
        EX,
        'base = UNSAFE.sub("_", name) or "_"',
        'base = UNSAFE.sub("_", name) or "x"',
        T_E + "test_output_stems_are_safe_and_never_repeat_in_any_case",
    ),
    _plant(
        "export-no-doubling",
        EX,
        'chain = f"{DOUBLING}," if header.doubled else ""',
        'chain = ""',
        T_E + "test_the_mp4_command_names_the_codecs_and_puts_the_moov_atom_first",
        T_E
        + "test_a_flagged_file_is_drawn_at_twice_the_stored_height_and_has_no_audio",
    ),
    _plant(
        "export-doubling-filter",
        EX,
        'DOUBLING = "format=rgb24,scale=iw:2*ih:flags=neighbor"',
        'DOUBLING = "format=rgb24,scale=iw:ih:flags=neighbor"',
        T_E
        + "test_a_flagged_file_is_drawn_at_twice_the_stored_height_and_has_no_audio",
    ),
    _plant(
        "export-audio-not-mapped",
        EX,
        'command += ["-map", "0:a:0"] if header.audio is not None else []',
        "command += []",
        T_E + "test_mp4_is_h264_yuv420p_with_aac_at_48000_and_the_intended_size",
    ),
    _plant(
        "export-audio-always",
        EX,
        '    if header.audio is not None:\n        command += ["-c:a"',
        '    if True:\n        command += ["-c:a"',
        T_E
        + "test_the_mp4_command_has_no_doubling_and_no_audio_when_the_file_has_none",
    ),
    _plant(
        "export-audio-rate",
        EX,
        '"audio_rate": 48000,',
        '"audio_rate": 44100,',
        T_E + "test_mp4_is_h264_yuv420p_with_aac_at_48000_and_the_intended_size",
    ),
    _plant(
        "export-audio-codec",
        EX,
        '"audio_codec": "aac",',
        '"audio_codec": "ac3",',
        T_E + "test_mp4_is_h264_yuv420p_with_aac_at_48000_and_the_intended_size",
    ),
    _plant(
        "export-moov-last",
        EX,
        '"movflags": "+faststart",',
        '"movflags": "+use_metadata_tags",',
        T_E + "test_the_moov_atom_comes_before_the_media_data",
    ),
    _plant(
        "export-pixel-format",
        EX,
        '"pix_fmt": "yuv420p",',
        '"pix_fmt": "yuv444p",',
        T_E + "test_mp4_is_h264_yuv420p_with_aac_at_48000_and_the_intended_size",
    ),
    _plant(
        "export-video-codec",
        EX,
        '"video_codec": "libx264",',
        '"video_codec": "mpeg4",',
        T_E + "test_mp4_is_h264_yuv420p_with_aac_at_48000_and_the_intended_size",
    ),
    _plant(
        "export-ffmpeg-failure-silent",
        EX,
        "    if done.returncode != 0:\n        raise FfmpegFailed(",
        "    if False:\n        raise FfmpegFailed(",
        T_E + "test_an_odd_picture_size_cannot_be_written_as_yuv420p_and_is_an_error",
    ),
    _plant(
        "export-rows-once",
        EX,
        "        out += row + row",
        "        out += row",
        T_E + "test_double_rows_draws_each_stored_row_twice",
    ),
    _plant(
        "export-middle",
        EX,
        "    return frames // 2 + 1",
        "    return frames // 2 + 2",
        T_E + "test_the_middle_frame_is_the_one_showing_at_half_the_duration",
    ),
    _plant(
        "export-poster-not-doubled",
        EX,
        "    if header.doubled:\n        rgba = double_rows(rgba, header.width)",
        "    if False:\n        rgba = double_rows(rgba, header.width)",
        T_E + "test_png_of_a_flagged_file_draws_each_stored_row_twice",
    ),
    _plant(
        "export-vtt-rounding",
        EX,
        "    millis = (us + 500) // 1000",
        "    millis = us // 1000",
        T_E + "test_vtt_time_rounds_to_the_millisecond",
    ),
    _plant(
        "export-vtt-minutes",
        EX,
        "    minutes, seconds = divmod(seconds, 60)",
        "    minutes, seconds = divmod(seconds, 100)",
        T_E + "test_vtt_time_rounds_to_the_millisecond",
    ),
    _plant(
        "export-vtt-ampersand",
        EX,
        'text.replace("&", "&amp;")',
        'text.replace("&", "&")',
        T_E + "test_vtt_text_escapes_ampersand_and_angle_brackets_and_cr",
    ),
    _plant(
        "export-vtt-greater",
        EX,
        '.replace(">", "&gt;")',
        "",
        T_E + "test_vtt_text_escapes_ampersand_and_angle_brackets_and_cr",
    ),
    _plant(
        "export-vtt-less",
        EX,
        '.replace("<", "&lt;")',
        "",
        T_E + "test_vtt_text_escapes_ampersand_and_angle_brackets_and_cr",
    ),
    _plant(
        "export-vtt-cr",
        EX,
        '    return escaped.replace("\\r", " ")',
        "    return escaped",
        T_E + "test_vtt_text_escapes_ampersand_and_angle_brackets_and_cr",
    ),
    _plant(
        "export-vtt-empty-lines",
        EX,
        "        lines = [vtt_text(line) for line in caption.lines if line]",
        "        lines = [vtt_text(line) for line in caption.lines]",
        T_E + "test_vtt_holds_only_the_non_empty_lines_and_skips_empty_captions",
    ),
    _plant(
        "export-vtt-empty-captions",
        EX,
        "        if not lines:\n            continue",
        "        if False:\n            continue",
        T_E + "test_vtt_holds_only_the_non_empty_lines_and_skips_empty_captions",
    ),
    _plant(
        "export-vtt-header",
        EX,
        'blocks = ["WEBVTT\\n"]',
        'blocks = [""]',
        T_E + "test_vtt_holds_only_the_non_empty_lines_and_skips_empty_captions",
    ),
    _plant(
        "export-captions-frame-time",
        EX,
        "shown_captions(parsed.records, header.frames, header.frame_us)",
        "shown_captions(parsed.records, header.frames, 1)",
        T_E + "test_subtitle_records_and_captions_are_in_the_json",
    ),
    _plant(
        "export-captions-no-frames",
        EX,
        "shown_captions(parsed.records, header.frames, header.frame_us)",
        "shown_captions(parsed.records, 1, header.frame_us)",
        T_E + "test_subtitle_records_and_captions_are_in_the_json",
    ),
    _plant(
        "export-unreadable-error",
        EX,
        "            status, error = UNREADABLE, str(exc)",
        '            status, error = UNREADABLE, ""',
        T_E + "test_missing_and_unreadable_names_have_no_header_or_files",
    ),
    _plant(
        "export-missing-found",
        EX,
        "    header, error, status = None, None, MISSING",
        "    header, error, status = None, None, FOUND",
        T_E + "test_every_name_is_reported_once_in_the_asked_order",
    ),
    _plant(
        "export-captions-name",
        EX,
        'files["captions"] = f"{stem}.vtt"',
        'files["captions"] = f"{stem}.txt"',
        JSON_FILE,
    ),
    _plant(
        "export-captions-encoding",
        EX,
        'write_text(render_vtt(captions), encoding="utf-8")',
        'write_text(render_vtt(captions), encoding="latin-1")',
        T_E + "test_vtt_holds_the_shown_captions_escaped",
    ),
    _plant(
        "export-no-captions-file",
        EX,
        '        if subtitles is not None:\n            files["captions"]',
        '        if False:\n            files["captions"]',
        T_E + "test_vtt_holds_the_shown_captions_escaped",
    ),
    _plant(
        "export-zero-frames",
        EX,
        "if header is not None and found.video is not None and header.frames > 0:",
        "if header is not None and found.video is not None:",
        T_E + "test_a_movie_of_no_frames_is_found_and_writes_no_file",
    ),
    _plant(
        "export-version-late",
        EX,
        "    version = ffmpeg_version()\n",
        '    version = "unknown"\n',
        T_E + "test_a_missing_ffmpeg_stops_the_export_before_anything_is_written",
    ),
    _plant(
        "export-json-ascii",
        EX,
        "json.dumps(data, ensure_ascii=False, indent=1)",
        "json.dumps(data, ensure_ascii=True, indent=1)",
        T_E + "test_text_keeps_every_byte_as_one_character_in_the_json_file",
    ),
    _plant(
        "export-names-view",
        EX,
        "    names = movie_names(install, balance_of_power)",
        "    names = movie_names(install)",
        T_E + "test_the_view_without_balance_of_power_reports_every_name_missing",
    ),
    _plant(
        "export-bop-view",
        EX,
        "_export_one(name, stems[name], install, out, balance_of_power) for name in names",
        "_export_one(name, stems[name], install, out, True) for name in names",
        T_E + "test_the_view_without_balance_of_power_reports_every_name_missing",
    ),
]

CLI_PLANTS: list[Plant] = [
    _plant(
        "cli-unresolved-status",
        CL,
        '        logger.error("movie %s does not resolve under %s", args.name, install)\n        return 1',
        '        logger.error("movie %s does not resolve under %s", args.name, install)\n        return 0',
        T_C
        + "test_dump_frames_of_a_name_that_does_not_resolve_prints_nothing_and_exits_1",
    ),
    _plant(
        "cli-frames-view",
        CL,
        "    found = find_movie(install, args.name, not args.no_balance_of_power)\n    if found.video is None",
        "    found = find_movie(install, args.name, True)\n    if found.video is None",
        T_C
        + "test_dump_frames_of_a_name_that_does_not_resolve_prints_nothing_and_exits_1",
    ),
    _plant(
        "cli-subtitles-view",
        CL,
        "    found = find_movie(install, args.name, not args.no_balance_of_power)\n    parsed = None",
        "    found = find_movie(install, args.name, True)\n    parsed = None",
        T_C
        + "test_dump_subtitles_of_a_file_that_does_not_resolve_is_the_missing_sheet",
    ),
    _plant(
        "cli-frames-install-status",
        CL,
        "        return 2\n    found = find_movie(install, args.name, not args.no_balance_of_power)\n    if found.video",
        "        return 1\n    found = find_movie(install, args.name, not args.no_balance_of_power)\n    if found.video",
        T_C + "test_a_folder_that_is_not_an_install_is_status_2",
    ),
    _plant(
        "cli-subtitles-install-status",
        CL,
        "        return 2\n    found = find_movie(install, args.name, not args.no_balance_of_power)\n    parsed = None",
        "        return 1\n    found = find_movie(install, args.name, not args.no_balance_of_power)\n    parsed = None",
        T_C + "test_a_folder_that_is_not_an_install_is_status_2",
    ),
    _plant(
        "cli-export-install-status",
        CL,
        "        return 2\n    try:\n        data = export_movies(",
        "        return 1\n    try:\n        data = export_movies(",
        T_C + "test_a_folder_that_is_not_an_install_is_status_2",
    ),
    _plant(
        "cli-read-failure-status",
        CL,
        '        logger.error("cannot read %s: %s", found.video, exc)\n        return 1',
        '        logger.error("cannot read %s: %s", found.video, exc)\n        return 0',
        T_C + "test_dump_frames_without_ffmpeg_prints_nothing_and_exits_1",
        T_C + "test_dump_frames_of_a_refused_file_exits_1",
    ),
    _plant(
        "cli-rate",
        CL,
        '        rate = probe_video(found.video)["avg_frame_rate"]',
        '        rate = "25/1"',
        T_C + "test_dump_frames_prints_the_sheet",
    ),
    _plant(
        "cli-frame-lines",
        CL,
        '            sys.stdout.write(line + "\\n")',
        "            sys.stdout.write(line)",
        T_C + "test_dump_frames_prints_the_sheet",
    ),
    _plant(
        "cli-subtitles-sheet",
        CL,
        "render_subtitles(args.name, found.subtitle_label, parsed)",
        "render_subtitles(args.name, found.subtitle_label, None)",
        T_C + "test_dump_subtitles_prints_the_sheet",
    ),
    _plant(
        "cli-export-line",
        CL,
        "f\"exported {found} of {len(data['movies'])} movies and movies.json to {args.out}\"",
        'f"exported {found} movies to {args.out}"',
        T_C + "test_export_writes_the_files_and_prints_one_line",
    ),
    _plant(
        "cli-export-failure-status",
        CL,
        '        logger.error("cannot export to %s: %s", args.out, exc)\n        return 1',
        '        logger.error("cannot export to %s: %s", args.out, exc)\n        return 0',
        T_C + "test_export_without_ffmpeg_exits_1_and_writes_nothing",
    ),
    _plant(
        "cli-export-view",
        CL,
        "export_movies(install, args.out, not args.no_balance_of_power)",
        "export_movies(install, args.out, True)",
        T_C + "test_export_without_balance_of_power_reports_every_name_missing",
    ),
    _plant(
        "cli-captions-default",
        CL,
        "        frames = max((r.number for r in parsed.records), default=0)",
        "        frames = min((r.number for r in parsed.records), default=0)",
        T_C + "test_captions_default_frames_is_the_last_number",
    ),
    _plant(
        "cli-captions-frame-us",
        CL,
        "DEFAULT_FRAME_US = 66730",
        "DEFAULT_FRAME_US = 66660",
        T_C + "test_captions_default_frames_is_the_last_number",
    ),
    _plant(
        "cli-captions-read-status",
        CL,
        '        logger.error("cannot read %s: %s", path, exc)\n        return 1',
        '        logger.error("cannot read %s: %s", path, exc)\n        return 0',
        T_C + "test_captions_of_a_missing_file_exits_1",
    ),
    _plant(
        "cli-dump-kinds-swapped",
        CL,
        '        if args.dump_kind == "frames":',
        '        if args.dump_kind == "subtitles":',
        T_C + "test_dump_frames_prints_the_sheet",
    ),
    _plant(
        "cli-not-registered",
        MN,
        '    "movies": movies_cli,\n',
        '    "movies": text_cli,\n',
        T_C + "test_dump_subtitles_prints_the_sheet",
    ),
]

MOVIES_OUTPUT_PLANTS: list[Plant] = [
    *RENDER_PLANTS,
    *JSON_PLANTS,
    *EXPORT_PLANTS,
    *CLI_PLANTS,
]
