"""The planted faults of the movie reading: header, subtitles, finding, decoding.

Purpose:
    Break one rule of ``jedimaster/movies/`` per plant: the header's
    signature, rate, flags, tables, audio rate word and refusals; the
    which names the game asks for and how each resolves; how the
    frames are decoded, numbered, timed and hashed.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``movies-``.
    - Each ``old`` text occurs exactly once in its file.

Call:
    ``from plants_movies_read import MOVIES_READ_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

HD = "jedimaster/movies/header.py"
SB = "jedimaster/movies/subtitles.py"
FD = "jedimaster/movies/find.py"
FR = "jedimaster/movies/frames.py"
T_H = "tests/test_movies_header.py::"
T_S = "tests/test_movies_subtitles.py::"
T_F = "tests/test_movies_find.py::"
T_R = "tests/test_movies_frames.py::"
CASES = T_S + "test_made_up_files_read_as_the_sheets_say"


def _plant(pid: str, path: str, old: str, new: str, *expect: str) -> Plant:
    return Plant(f"movies-{pid}", path, old, new, expect)


HEADER_PLANTS: list[Plant] = [
    _plant(
        "header-smk4-refused",
        HD,
        'SIGNATURES = (b"SMK2", b"SMK4")',
        'SIGNATURES = (b"SMK2",)',
        T_H + "test_signature_smk4_is_read_and_a_path_reads_the_same",
    ),
    _plant(
        "header-signature-unchecked",
        HD,
        "    if signature not in SIGNATURES:",
        "    if False:",
        T_H + "test_refuses_a_file_that_is_not_smacker_naming_the_signature",
    ),
    _plant(
        "header-rate-ms",
        HD,
        "        return rate * 1000",
        "        return rate * 100",
        T_H + "test_frame_duration_from_the_signed_rate",
    ),
    _plant(
        "header-rate-negative",
        HD,
        "        return -rate * 10",
        "        return -rate * 100",
        T_H + "test_frame_duration_from_the_signed_rate",
    ),
    _plant(
        "header-rate-zero",
        HD,
        "DEFAULT_FRAME_US = 100_000",
        "DEFAULT_FRAME_US = 10_000",
        T_H + "test_frame_duration_from_the_signed_rate",
    ),
    _plant(
        "header-duration",
        HD,
        "        return self.frames * self.frame_us",
        "        return (self.frames + 1) * self.frame_us",
        T_H + "test_duration_is_frames_times_frame_duration",
    ),
    _plant(
        "header-doubled-bit-2-ignored",
        HD,
        "        return bool(self.flags & (FLAG_INTERLACED | FLAG_DOUBLED))",
        "        return bool(self.flags & FLAG_INTERLACED)",
        T_H + "test_intended_picture_size_doubles_the_stored_height",
    ),
    _plant(
        "header-picture-height-not-doubled",
        HD,
        "        return self.height * 2 if self.doubled else self.height",
        "        return self.height",
        T_H + "test_intended_picture_size_doubles_the_stored_height",
    ),
    _plant(
        "header-display-width",
        HD,
        "        return self.width // 2 if self.doubled else self.width",
        "        return self.width",
        T_H + "test_intended_picture_size_doubles_the_stored_height",
    ),
    _plant(
        "header-ring-entry-dropped",
        HD,
        "    entries = frames + (flags & FLAG_RING)",
        "    entries = frames",
        T_H + "test_ring_flag_adds_one_table_entry_not_one_frame",
    ),
    _plant(
        "header-ring-prefix-dropped",
        HD,
        '        count += struct.unpack_from("<I", fixed, 20)[0] & FLAG_RING',
        "        count += 0",
        T_H + "test_a_path_with_the_ring_flag_reads_the_longer_tables",
    ),
    _plant(
        "header-ring-bit",
        HD,
        "FLAG_RING = 1\n",
        "FLAG_RING = 8\n",
        T_H + "test_ring_flag_adds_one_table_entry_not_one_frame",
    ),
    _plant(
        "header-frame-size-mask",
        HD,
        "        return self.frame_sizes[index] & ~3",
        "        return self.frame_sizes[index] & ~1",
        T_H + "test_frame_size_clears_its_two_flag_bits_and_bit_0_is_the_key_frame",
    ),
    _plant(
        "header-key-frame-bit",
        HD,
        "        return bool(self.frame_sizes[index] & 1)",
        "        return bool(self.frame_sizes[index] & 2)",
        T_H + "test_frame_size_clears_its_two_flag_bits_and_bit_0_is_the_key_frame",
    ),
    _plant(
        "header-palette-bit",
        HD,
        "        return bool(self.frame_types[index] & 1)",
        "        return bool(self.frame_types[index] & 2)",
        T_H + "test_frame_types_bit_0_is_a_palette",
    ),
    _plant(
        "header-expected-size-trees",
        HD,
        "            + self.tree_bytes\n",
        "",
        T_H + "test_every_field_is_read",
    ),
    _plant(
        "header-audio-first-with-data",
        HD,
        "            if track.has_data:",
        "            if track.rate:",
        T_H + "test_audio_track_is_the_first_with_data_and_its_word_is_split",
    ),
    _plant(
        "header-audio-channels",
        HD,
        "        return 2 if self.stereo else 1",
        "        return 2",
        T_H + "test_compression_kind_bits_and_mono_8_bit_pcm",
    ),
    _plant(
        "header-audio-pcm-names",
        HD,
        '        return "pcm_s16le" if self.sixteen_bit else "pcm_u8"',
        '        return "pcm_u8" if self.sixteen_bit else "pcm_s16le"',
        T_H + "test_audio_track_is_the_first_with_data_and_its_word_is_split",
    ),
    _plant(
        "header-audio-rate-mask",
        HD,
        "RATE_MASK = 0xFFFFFF",
        "RATE_MASK = 0xFFFF",
        T_H + "test_audio_track_is_the_first_with_data_and_its_word_is_split",
    ),
    _plant(
        "header-audio-compression-bits",
        HD,
        "        return (self.word >> 26) & 3",
        "        return (self.word >> 27) & 3",
        T_H + "test_compression_kind_bits_and_mono_8_bit_pcm",
    ),
    _plant(
        "header-audio-16-bit-bit",
        HD,
        "RATE_16_BIT = 1 << 29",
        "RATE_16_BIT = 1 << 27",
        T_H + "test_audio_track_is_the_first_with_data_and_its_word_is_split",
    ),
    _plant(
        "header-audio-compressed-bit",
        HD,
        "RATE_COMPRESSED = 1 << 31",
        "RATE_COMPRESSED = 1 << 27",
        T_H + "test_audio_track_is_the_first_with_data_and_its_word_is_split",
    ),
    _plant(
        "header-cut-fixed-part",
        HD,
        "    if len(data) < FIXED_SIZE:",
        "    if len(data) < FIXED_SIZE - 8:",
        T_H + "test_refuses_a_cut_fixed_header",
    ),
    _plant(
        "header-cut-table",
        HD,
        "    if len(data) < table_end:",
        "    if len(data) < FIXED_SIZE:",
        T_H + "test_refuses_a_cut_frame_table",
        T_H + "test_refuses_a_huge_frame_count_in_a_small_file",
    ),
    _plant(
        "header-cut-frames",
        HD,
        "    if info.expected_size() > total:",
        "    if info.expected_size() > total + 1:",
        T_H + "test_refuses_a_file_cut_inside_the_frames",
        T_H + "test_a_path_cut_inside_the_frames_is_refused",
    ),
    _plant(
        "header-longer-refused",
        HD,
        "    if info.expected_size() > total:",
        "    if info.expected_size() != total:",
        T_H + "test_a_file_longer_than_the_header_describes_is_accepted",
    ),
    _plant(
        "header-label-lost",
        HD,
        "    return os.fspath(source)",
        '    return "<file>"',
        T_H + "test_the_error_names_the_path",
    ),
    _plant(
        "header-missing-path-wrapped",
        HD,
        '    size = path.stat().st_size\n    with path.open("rb") as handle:',
        '    if not path.exists():\n        raise MovieFormatError(os.fspath(path), "file", "missing")\n'
        '    size = path.stat().st_size\n    with path.open("rb") as handle:',
        T_H + "test_a_missing_path_raises_os_error",
    ),
    _plant(
        "header-empty-field",
        HD,
        'raise MovieFormatError(label, "signature", f"only {len(data)} bytes")',
        'raise MovieFormatError(label, "header", f"only {len(data)} bytes")',
        T_H + "test_refuses_an_empty_and_a_three_byte_file",
    ),
    _plant(
        "header-interlaced-bit",
        HD,
        "        return bool(self.flags & FLAG_INTERLACED)",
        "        return bool(self.flags & FLAG_DOUBLED)",
        T_H + "test_flag_bits_have_their_names",
    ),
    _plant(
        "header-y-doubled-bit",
        HD,
        "        return bool(self.flags & FLAG_DOUBLED)",
        "        return bool(self.flags & FLAG_INTERLACED)",
        T_H + "test_flag_bits_have_their_names",
    ),
    _plant(
        "header-always-doubled",
        HD,
        "        return bool(self.flags & (FLAG_INTERLACED | FLAG_DOUBLED))",
        "        return True",
        T_H + "test_intended_picture_size_doubles_the_stored_height[0-False-180]",
        T_H + "test_intended_picture_size_doubles_the_stored_height[1-False-180]",
    ),
]

FIND_PLANTS: list[Plant] = [
    _plant(
        "find-order",
        FD,
        "    asked = [OPENING, *cutscene_movies(install, balance_of_power), NETWORK_FALLBACK]",
        "    asked = [*cutscene_movies(install, balance_of_power), OPENING, NETWORK_FALLBACK]",
        T_F + "test_names_are_opening_the_list_in_order_and_flyby1a",
    ),
    _plant(
        "find-repeat-case",
        FD,
        "        if name.casefold() not in seen:",
        "        if name not in seen:",
        T_F + "test_a_name_asked_twice_in_any_case_is_kept_once_at_its_first_place",
        T_F + "test_names_are_opening_the_list_in_order_and_flyby1a",
    ),
    _plant(
        "find-flyby-dropped",
        FD,
        'NETWORK_FALLBACK = "Flyby1a"',
        'NETWORK_FALLBACK = "Opening"',
        T_F + "test_without_a_list_the_two_fixed_names_remain",
    ),
    _plant(
        "find-list-first-only",
        FD,
        "    return [c.movie for c in view.cutscenes if c.movie]",
        "    return [c.movie for c in view.cutscenes[1:] if c.movie]",
        T_F + "test_names_are_opening_the_list_in_order_and_flyby1a",
    ),
    _plant(
        "find-list-bop-ignored",
        FD,
        '    path = resolve_game_path(install, _game_path("cutscene", ".lst"), balance_of_power)',
        '    path = resolve_game_path(install, _game_path("cutscene", ".lst"), True)',
        T_F + "test_the_view_without_balance_of_power_does_not_see_its_list",
    ),
    _plant(
        "find-bop-ignored",
        FD,
        "        found = resolve_game_path(install, _game_path(name, suffix), bop)",
        "        found = resolve_game_path(install, _game_path(name, suffix), True)",
        T_F + "test_balance_of_power_wins_over_the_base_game",
    ),
    _plant(
        "find-label-prefix",
        FD,
        '    return ("BalanceOfPower/" if inside else "") + f"{FOLDER}/{name}{suffix}"',
        '    return f"{FOLDER}/{name}{suffix}"',
        T_F + "test_a_file_resolves_in_any_case_and_the_label_keeps_the_asked_case",
    ),
    _plant(
        "find-label-prefix-always",
        FD,
        '    return ("BalanceOfPower/" if inside else "") + f"{FOLDER}/{name}{suffix}"',
        '    return "BalanceOfPower/" + f"{FOLDER}/{name}{suffix}"',
        T_F + "test_balance_of_power_wins_over_the_base_game",
    ),
    _plant(
        "find-label-disk-case",
        FD,
        '    return ("BalanceOfPower/" if inside else "") + f"{FOLDER}/{name}{suffix}"',
        '    return ("BalanceOfPower/" if inside else "") + f"{FOLDER}/{found.name}"',
        T_F + "test_a_file_resolves_in_any_case_and_the_label_keeps_the_asked_case",
    ),
    _plant(
        "find-folder-is-a-movie",
        FD,
        "    if found is None or not found.is_file():",
        "    if found is None:",
        T_F + "test_a_folder_named_like_a_movie_is_not_a_movie",
    ),
    _plant(
        "find-escape-allowed",
        FD,
        "    except ValueError as exc:",
        "    except ZeroDivisionError as exc:",
        T_F + "test_a_name_that_leaves_the_install_does_not_resolve",
    ),
    _plant(
        "find-no-warning-for-escape",
        FD,
        '        logger.warning("movie name %r cannot be a game path: %s", name, exc)',
        '        logger.debug("movie name %r cannot be a game path: %s", name, exc)',
        T_F + "test_a_name_that_leaves_the_install_does_not_resolve",
    ),
    _plant(
        "find-suffix-video",
        FD,
        'VIDEO_SUFFIX = ".smk"',
        'VIDEO_SUFFIX = ".smv"',
        T_F + "test_a_file_resolves_in_any_case_and_the_label_keeps_the_asked_case",
    ),
    _plant(
        "find-suffix-subtitles",
        FD,
        'SUBTITLE_SUFFIX = ".txt"',
        'SUBTITLE_SUFFIX = ".sub"',
        T_F + "test_the_video_and_the_subtitles_resolve_on_their_own",
    ),
    _plant(
        "find-folder",
        FD,
        'FOLDER = "movies"',
        'FOLDER = "movie"',
        T_F + "test_a_file_resolves_in_any_case_and_the_label_keeps_the_asked_case",
    ),
    _plant(
        "find-list-unreadable-kept",
        FD,
        '        logger.info("no cutscene list under %s", install)\n        return []',
        '        logger.info("no cutscene list under %s", install)\n        return ["intro"]',
        T_F + "test_without_a_list_the_two_fixed_names_remain",
    ),
    _plant(
        "find-unresolved-raises",
        FD,
        '        logger.debug("%s%s does not resolve", name, suffix)\n        return None, None',
        '        logger.debug("%s%s does not resolve", name, suffix)\n        raise FileNotFoundError(name)',
        T_F + "test_a_name_that_does_not_resolve_is_reported_not_raised",
    ),
]

FRAMES_PLANTS: list[Plant] = [
    _plant(
        "frames-start-time",
        FR,
        "        frame = Frame(number, (number - 1) * frame_us, frame_us, zlib.crc32(rgba))",
        "        frame = Frame(number, number * frame_us, frame_us, zlib.crc32(rgba))",
        T_R + "test_each_frame_has_its_number_times_and_crc",
    ),
    _plant(
        "frames-crc",
        FR,
        "        frame = Frame(number, (number - 1) * frame_us, frame_us, zlib.crc32(rgba))",
        "        frame = Frame(number, (number - 1) * frame_us, frame_us, zlib.adler32(rgba))",
        T_R + "test_each_frame_has_its_number_times_and_crc",
    ),
    _plant(
        "frames-number-from-0",
        FR,
        "    for number, rgba in enumerate(iter_rgba(path, width, height, limit), start=1):\n        frame = Frame(",
        "    for number, rgba in enumerate(iter_rgba(path, width, height, limit), start=0):\n        frame = Frame(",
        T_R + "test_each_frame_has_its_number_times_and_crc",
        T_R + "test_a_limit_stops_after_that_many_frames",
    ),
    _plant(
        "frames-duration",
        FR,
        "        frame = Frame(number, (number - 1) * frame_us, frame_us, zlib.crc32(rgba))",
        "        frame = Frame(number, (number - 1) * frame_us, frame_us + 1, zlib.crc32(rgba))",
        T_R + "test_each_frame_has_its_number_times_and_crc",
    ),
    _plant(
        "frames-bgra",
        FR,
        'RAW_ARGS = ("-f", "rawvideo", "-pix_fmt", "rgba", "-")',
        'RAW_ARGS = ("-f", "rawvideo", "-pix_fmt", "bgra", "-")',
        T_R + "test_the_decoded_bytes_are_the_frames_rgba_rows_top_first",
    ),
    _plant(
        "frames-three-bytes",
        FR,
        "BYTES_PER_PIXEL = 4",
        "BYTES_PER_PIXEL = 3",
        T_R + "test_the_decoded_bytes_are_the_frames_rgba_rows_top_first",
    ),
    _plant(
        "frames-limit-off-by-one",
        FR,
        "            while limit is None or count < limit:",
        "            while limit is None or count <= limit:",
        T_R + "test_a_limit_stops_after_that_many_frames",
    ),
    _plant(
        "frames-failure-silent",
        FR,
        "        if exhausted and proc.returncode != 0:",
        "        if False:",
        T_R + "test_a_file_ffmpeg_cannot_read_is_a_clear_error",
    ),
    _plant(
        "frames-tool-not-checked",
        FR,
        "    if found is None:\n        raise FfmpegMissing(",
        "    if False:\n        raise FfmpegMissing(",
        T_R + "test_a_missing_ffmpeg_is_a_clear_error",
    ),
    _plant(
        "frames-missing-message",
        FR,
        '            f"{name} was not found on the system path: install FFmpeg "',
        '            f"{name}: install FFmpeg "',
        T_R + "test_a_missing_ffmpeg_is_a_clear_error",
    ),
    _plant(
        "frames-version-pattern",
        FR,
        'VERSION_PATTERN = re.compile(r"version\\s+(\\S+)")',
        'VERSION_PATTERN = re.compile(r"version\\s+(\\d)")',
        T_R + "test_version_and_probe_report_what_the_programs_say",
    ),
    _plant(
        "frames-probe-rate",
        FR,
        '"stream=codec_name,width,height,avg_frame_rate"',
        '"stream=codec_name,width,height"',
        T_R + "test_version_and_probe_report_what_the_programs_say",
    ),
    _plant(
        "frames-probe-failure",
        FR,
        "    if done.returncode != 0 or not streams:",
        "    if False:",
        T_R + "test_a_file_ffmpeg_cannot_read_is_a_clear_error",
    ),
    _plant(
        "frames-no-frame-error",
        FR,
        '    raise ValueError(f"{path} has no frame {index}")',
        '    return b""',
        T_R + "test_frame_past_the_end_is_an_error",
    ),
    _plant(
        "frames-wrong-frame",
        FR,
        "        if number == index:",
        "        if number == 1:",
        T_R + "test_the_decoded_bytes_are_the_frames_rgba_rows_top_first",
    ),
    _plant(
        "frames-stored-size",
        FR,
        "    size = width * height * BYTES_PER_PIXEL",
        "    size = width * height * BYTES_PER_PIXEL // 2",
        T_R + "test_a_stored_size_that_is_not_the_streams_gives_other_frames",
        T_R + "test_the_decoded_bytes_are_the_frames_rgba_rows_top_first",
    ),
]

MOVIES_READ_PLANTS: list[Plant] = [
    *HEADER_PLANTS,
    *FIND_PLANTS,
    *FRAMES_PLANTS,
]
