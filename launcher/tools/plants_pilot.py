"""The planted faults of the pilot: layout, reader, sheets, merge, load, outputs.

Purpose:
    Break one rule of ``jedimaster/pilot/`` per plant: the layout's sizes
    and counts; how the reader reads each type, nests arrays, checks a
    file's size and kind and finds a member; how the sheets print each
    type, paths, left-out elements and headers; each merge rule; each step
    of the load, its defaults and its warnings; the JSON, each schema rule
    a rejection test names; the ``pilot`` command line.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``pilot-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant that changes the schema builder regenerates the schema files,
      so it fails the rejection test it names, not the drift test.

Call:
    ``from plants_pilot import PILOT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

LA = "jedimaster/pilot/layout.py"
RE = "jedimaster/pilot/record.py"
RD = "jedimaster/pilot/render.py"
ME = "jedimaster/pilot/merge.py"
LO = "jedimaster/pilot/load.py"
DE = "jedimaster/pilot/defaults.py"
JS = "jedimaster/pilot/to_json.py"
CL = "jedimaster/pilot/cli.py"
T_REC = "tests/test_pilot_record.py::"
T_REN = "tests/test_pilot_render.py::"
T_MER = "tests/test_pilot_merge.py::"
T_LOA = "tests/test_pilot_load.py::"
T_OUT = "tests/test_pilot_output.py::"
PACKED = T_REC + "test_layout_is_packed"
SIGNED = T_REC + "test_signed_unsigned_and_one_value"
TEXT = T_REC + "test_text_keeps_every_byte_and_its_text"
BYTES = T_REC + "test_bytes_and_struct_arrays"
LOCATE = T_REC + "test_locate_finds_members"
RANGE = T_LOA + "test_out_of_range_is_logged_and_kept"
REJECTS = T_OUT + "test_schema_rejects_bad_values"

_READ_PLANTS: list[Plant] = [
    Plant(
        "pilot-layout-element-size",
        LA,
        "    return struct(member.type).size\n",
        "    return struct(member.type).size + 1\n",
        (PACKED,),
    ),
    Plant(
        "pilot-layout-count-first-dimension",
        LA,
        "    return prod(member.dims)\n",
        "    return member.dims[0] if member.dims else 1\n",
        (PACKED, T_REC + "test_arrays_nest_last_index_fastest"),
    ),
    Plant(
        "pilot-read-i32-unsigned",
        RE,
        'NUMBER_FORMATS = {I32: "i", U32: "I"}',
        'NUMBER_FORMATS = {I32: "I", U32: "I"}',
        (SIGNED, T_REN + "test_numbers_signed_and_unsigned"),
    ),
    Plant(
        "pilot-read-u32-signed",
        RE,
        'NUMBER_FORMATS = {I32: "i", U32: "I"}',
        'NUMBER_FORMATS = {I32: "i", U32: "i"}',
        (SIGNED, T_REN + "test_numbers_signed_and_unsigned"),
    ),
    Plant(
        "pilot-nest-first-index-fastest",
        RE,
        "    return [nest(flat[i * step : (i + 1) * step], dims[1:]) for i in range(dims[0])]",
        "    return [nest(flat[i :: dims[0]], dims[1:]) for i in range(dims[0])]",
        (
            T_REC + "test_arrays_nest_last_index_fastest",
            T_REN + "test_arrays_print_in_a_row_last_index_fastest",
        ),
    ),
    Plant(
        "pilot-text-cut-at-end-mark",
        RE,
        "            chunks = [Text(chunk) for chunk in chunks]",
        '            chunks = [Text(chunk.split(b"\\0")[0]) for chunk in chunks]',
        (TEXT, T_REN + "test_text_quoted_to_its_end_then_every_byte"),
    ),
    Plant(
        "pilot-text-past-end-mark",
        RE,
        "        return self.raw if end < 0 else self.raw[:end]",
        "        return self.raw",
        (TEXT, T_OUT + "test_export_keeps_names_text_and_hex"),
    ),
    Plant(
        "pilot-text-always-ended",
        RE,
        "        return 0 in self.raw",
        "        return True",
        (TEXT,),
    ),
    Plant(
        "pilot-bytes-as-list",
        RE,
        "        return nest(chunks, member.dims[:-1]) if len(member.dims) > 1 else chunks[0]",
        "        return nest(chunks, member.dims[:-1]) if len(member.dims) > 1 "
        "else list(chunks[0])",
        (BYTES,),
    ),
    Plant(
        "pilot-struct-array-no-stride",
        RE,
        "        read_struct(member.type, data, start + i * size) for i in range(count(member))",
        "        read_struct(member.type, data, start) for i in range(count(member))",
        (BYTES,),
    ),
    Plant(
        "pilot-size-check-short-only",
        RE,
        "    if len(data) != size:",
        "    if len(data) < size:",
        (T_REC + "test_wrong_size_refused",),
    ),
    Plant(
        "pilot-kind-exact-case",
        RE,
        "    return SUFFIXES.get(Path(path).suffix.casefold())",
        "    return SUFFIXES.get(Path(path).suffix)",
        (T_REC + "test_files_read_as_their_kind", T_REC + "test_wrong_size_refused"),
    ),
    Plant(
        "pilot-locate-member-offset",
        RE,
        "        offset += member.offset\n        at += 1\n",
        "        at += 1\n",
        (LOCATE, T_LOA + "test_plt_alone_takes_the_defaults_then_merges"),
    ),
    Plant(
        "pilot-locate-row-start",
        RE,
        "    for size in member.dims[len(indexes) :]:\n        flat *= size\n",
        "",
        (LOCATE + "[path2]",),
    ),
    Plant(
        "pilot-locate-index-unchecked",
        RE,
        "        if not 0 <= index < member.dims[depth]:",
        "        if index > member.dims[depth]:",
        (T_REC + "test_locate_refuses_unknown_paths",),
    ),
]

_RENDER_PLANTS: list[Plant] = [
    Plant(
        "pilot-layout-dims-no-dash",
        RD,
        '            dims = "".join(f"[{d}]" for d in m.dims) or "-"',
        '            dims = "".join(f"[{d}]" for d in m.dims)',
        (T_REC + "test_layout_sheet_prints_the_layout_back",),
    ),
    Plant(
        "pilot-render-u8-upper",
        RD,
        '        return f"{path} = {raw.hex()}"',
        '        return f"{path} = {raw.hex().upper()}"',
        (T_REN + "test_bytes_print_as_hex",),
    ),
    Plant(
        "pilot-render-char-no-hex",
        RD,
        '        return f"{path} = {quote(raw)} hex={raw.hex()}"',
        '        return f"{path} = {quote(raw)}"',
        (
            T_REN + "test_text_quoted_to_its_end_then_every_byte",
            T_REN + "test_raw_and_load_headers",
        ),
    ),
    Plant(
        "pilot-render-zero-elements-kept",
        RD,
        "                if is_zero(element):\n                    continue\n",
        "",
        (T_REN + "test_zero_struct_elements_left_out_others_print",),
    ),
    Plant(
        "pilot-render-text-never-zero",
        RD,
        "        return not any(value.raw)",
        "        return False",
        (
            T_REN + "test_is_zero_and_flatten",
            T_REN + "test_zero_struct_elements_left_out_others_print",
        ),
    ),
    Plant(
        "pilot-render-unknown-is-zero",
        RD,
        "    return False\n\n\ndef _value_line",
        "    return True\n\n\ndef _value_line",
        (T_REN + "test_is_zero_and_flatten",),
    ),
    Plant(
        "pilot-render-index-dotted",
        RD,
        '                where = "".join(f"[{i}]" for i in index)',
        '                where = "".join(f".{i}" for i in index)',
        (
            T_REN + "test_zero_struct_elements_left_out_others_print",
            T_REN + "test_numbers_signed_and_unsigned",
        ),
    ),
    Plant(
        "pilot-render-struct-no-prefix",
        RD,
        '            lines += record_lines(member.type, value, path + ".")',
        '            lines += record_lines(member.type, value, "")',
        (T_REN + "test_arrays_print_in_a_row_last_index_fastest",),
    ),
    Plant(
        "pilot-render-members-sorted",
        RD,
        "    for member in struct(name).members:\n        value = record[member.name]",
        "    for member in sorted(struct(name).members):\n        value = record[member.name]",
        (T_REN + "test_members_in_layout_order",),
    ),
    Plant(
        "pilot-render-flatten-reversed",
        RD,
        "    return [leaf for item in value for leaf in flatten(item)]",
        "    return [leaf for item in value for leaf in flatten(item)][::-1]",
        (
            T_REN + "test_is_zero_and_flatten",
            T_REN + "test_arrays_print_in_a_row_last_index_fastest",
        ),
    ),
    Plant(
        "pilot-render-raw-header-swapped",
        RD,
        '    header = ["kind raw", f"bytes {read} of {struct(name).size}"]',
        '    header = ["kind raw", f"bytes {struct(name).size} of {read}"]',
        (T_REN + "test_raw_and_load_headers",),
    ),
    Plant(
        "pilot-render-files-swapped",
        RD,
        '        f"files pl2={int(load.pl2)} plt={int(load.plt)}",',
        '        f"files pl2={int(load.plt)} plt={int(load.pl2)}",',
        (T_REN + "test_raw_and_load_headers",),
    ),
]

_MERGE_PLANTS: list[Plant] = [
    Plant(
        "pilot-merge-six-kept-slots",
        ME,
        "KEPT_CRAFT_SLOTS = (4, 36, 41, 43, 45, 54, 78)",
        "KEPT_CRAFT_SLOTS = (4, 36, 41, 43, 45, 54)",
        (T_MER + "test_per_craft_tables",),
    ),
    Plant(
        "pilot-merge-past-base-count",
        ME,
        "                copied = craft < types and craft not in KEPT_CRAFT_SLOTS",
        "                copied = craft not in KEPT_CRAFT_SLOTS",
        (T_MER + "test_per_craft_tables",),
    ),
    Plant(
        "pilot-merge-craft-row-stride",
        ME,
        "                        dst + target.offset + (mission_type * kinds + start) * NUMBER,",
        "                        dst + target.offset + (mission_type * types + start) * NUMBER,",
        (T_MER + "test_per_craft_tables",),
    ),
    Plant(
        "pilot-merge-selection-state-copied",
        ME,
        "        if member.name in SIDE_NOT_COPIED:\n            continue\n",
        "        if member.name in SIDE_NOT_COPIED:\n"
        '            yield Copy(src + member.offset, dst + member.offset, member.size, "")\n'
        "            continue\n",
        (T_MER + "test_three_not_copied_and_full_only_members_kept",),
    ),
    Plant(
        "pilot-merge-payloads-overlap",
        ME,
        "            filled += member.size\n",
        "            filled += 0\n",
        (T_MER + "test_payloads_go_end_to_end",),
    ),
    Plant(
        "pilot-merge-run-not-shifted",
        ME,
        "    run_to = full_layout.members[full_names.index(SIDE_STATS) + 1].offset",
        "    run_to = full_layout.members[full_names.index(SIDE_STATS) + 2].offset",
        (T_MER + "test_run_after_stats_lands_four_bytes_early",),
    ),
    Plant(
        "pilot-merge-run-four-longer",
        ME,
        "    length = base_layout.size - run_from\n",
        "    length = base_layout.size - run_from + 4\n",
        (T_MER + "test_run_after_stats_lands_four_bytes_early",),
    ),
    Plant(
        "pilot-merge-side-first-part-skipped",
        ME,
        "    for member in base_layout.members[:after]:",
        "    for member in base_layout.members[1:after]:",
        (T_MER + "test_side_record_parts",),
    ),
    Plant(
        "pilot-merge-stats-only-craft",
        ME,
        "            yield Copy(src + member.offset, dst + target.offset, member.size, path)\n"
        "            continue\n",
        "            continue\n",
        (T_MER + "test_other_statistics_by_name",),
    ),
    Plant(
        "pilot-merge-names-not-copied",
        ME,
        "            plan.append(Copy(member.offset, target.offset, member.size, member.name))",
        "            pass",
        (T_MER + "test_members_of_the_same_name_copied", T_LOA + "test_both_files"),
    ),
    Plant(
        "pilot-merge-size-unchecked",
        ME,
        "    if len(full) != struct(FULL_RECORD).size or len(base) != struct(BASE_RECORD).size:",
        "    if False:",
        (T_MER + "test_merge_refuses_wrong_sizes",),
    ),
]

_LOAD_PLANTS: list[Plant] = [
    Plant(
        "pilot-load-neither-goes-on",
        LO,
        "    if pl2 is None and plt is None:",
        "    if False:",
        (T_LOA + "test_neither_file_fails",),
    ),
    Plant(
        "pilot-load-short-pl2-taken",
        LO,
        "        if len(pl2) < len(full):",
        "        if len(pl2) < 0:",
        (T_LOA + "test_short_pl2_fails_with_a_record_of_zeros",),
    ),
    Plant(
        "pilot-load-missing-not-warned",
        LO,
        '        _event(events, logging.WARNING, "pilot.record_missing")',
        '        _event(events, logging.INFO, "pilot.record_missing")',
        (T_LOA + "test_pl2_alone_loads_with_a_warning",),
    ),
    Plant(
        "pilot-load-defaults-over-pl2",
        LO,
        "    if pl2 is None:\n        set_defaults(full, defaults, events)",
        "    if True:\n        set_defaults(full, defaults, events)",
        (T_LOA + "test_both_files",),
    ),
    Plant(
        "pilot-load-short-plt-merged",
        LO,
        "    if len(plt) < base_size:",
        "    if len(plt) < 0:",
        (
            T_LOA + "test_short_plt_alone_keeps_the_defaults",
            T_LOA + "test_short_plt_with_pl2_keeps_the_pl2",
        ),
    ),
    Plant(
        "pilot-load-long-plt-whole",
        LO,
        "    merge(full, plt[:base_size])",
        "    merge(full, plt)",
        (T_LOA + "test_longer_files_are_read_for_their_record_size",),
    ),
    Plant(
        "pilot-load-names-over-pl2",
        LO,
        "    if pl2 is None:\n        set_game_names(full, defaults)",
        "    if True:\n        set_game_names(full, defaults)",
        (T_LOA + "test_both_files",),
    ),
    Plant(
        "pilot-load-imperial-from-rebel",
        LO,
        "TRAINING_SOURCES = (0, 0, 1, 2)",
        "TRAINING_SOURCES = (0, 0, 0, 2)",
        (
            T_LOA + "test_plt_alone_takes_the_defaults_then_merges",
            T_LOA + "test_short_plt_alone_keeps_the_defaults",
        ),
    ),
    Plant(
        "pilot-load-promo-worse",
        LO,
        '    ("promo", "current_rating_promo_points"),',
        '    ("promo", "current_rating_worse_promo_points"),',
        (T_LOA + "test_record_loaded_names_its_members",),
    ),
    Plant(
        "pilot-load-range-24-out",
        LO,
        "    if rating > LAST_RATING or faction >= SIDE_COUNT:",
        "    if rating >= LAST_RATING or faction >= SIDE_COUNT:",
        (RANGE + "[24-3-False]",),
    ),
    Plant(
        "pilot-load-range-side-4-in",
        LO,
        "    if rating > LAST_RATING or faction >= SIDE_COUNT:",
        "    if rating > LAST_RATING or faction > SIDE_COUNT:",
        (RANGE + "[0-4-True]",),
    ),
    Plant(
        "pilot-load-range-rating-ignored",
        LO,
        "    if rating > LAST_RATING or faction >= SIDE_COUNT:",
        "    if faction >= SIDE_COUNT:",
        (RANGE + "[25-0-True]",),
    ),
    Plant(
        "pilot-load-range-unsigned",
        LO,
        "    if rating > LAST_RATING or faction >= SIDE_COUNT:",
        "    if rating % 2**32 > LAST_RATING or faction % 2**32 >= SIDE_COUNT:",
        (RANGE + "[-1--1-False]",),
    ),
    Plant(
        "pilot-load-full-field-text",
        LO,
        "    if len(text) >= member.size:",
        "    if len(text) > member.size:",
        (T_LOA + "test_game_names_cut_to_their_field",),
    ),
    Plant(
        "pilot-load-text-past-end-mark",
        LO,
        "    text = c_text(text)\n",
        "",
        (T_LOA + "test_texts_stop_at_their_end_mark_and_rating_name_cut",),
    ),
    Plant(
        "pilot-load-name-run-on-silent",
        LO,
        "    if 0 not in raw:",
        "    if False:",
        (T_LOA + "test_name_without_end_mark_is_taken_whole",),
    ),
    Plant(
        "pilot-load-pl2-name-appended",
        LO,
        '    return plt_name[:-1] + "2"',
        '    return plt_name + "2"',
        (T_LOA + "test_load_pilot_from_a_folder",),
    ),
    Plant(
        "pilot-load-exact-case",
        LO,
        "        path = child_in_any_case(Path(folder), wanted)",
        "        path = Path(folder) / wanted",
        (T_LOA + "test_load_pilot_from_a_folder",),
    ),
    Plant(
        "pilot-load-no-suffix-added",
        LO,
        "    return name if name.casefold().endswith(PLT) else name + PLT",
        "    return name",
        (T_LOA + "test_load_pilot_from_a_folder",),
    ),
    Plant(
        "pilot-defaults-entry-123",
        DE,
        "RATING_NAME_ENTRY = 124",
        "RATING_NAME_ENTRY = 123",
        (T_LOA + "test_defaults_from_balance_of_power_first",),
    ),
    Plant(
        "pilot-defaults-entry-471",
        DE,
        "GAME_NAME_ENTRY = 470",
        "GAME_NAME_ENTRY = 471",
        (
            T_LOA + "test_defaults_from_balance_of_power_first",
            T_OUT + "test_pilot_load",
        ),
    ),
    Plant(
        "pilot-defaults-lists-swapped",
        DE,
        'LIST_VIEWS = ("rebel", "imperial", "network")',
        'LIST_VIEWS = ("imperial", "rebel", "network")',
        (T_LOA + "test_defaults_from_balance_of_power_first",),
    ),
    Plant(
        "pilot-defaults-lists-skip-bop",
        DE,
        "    menu = read_menu(_resolve(install, game_path, balance_of_power))",
        "    menu = read_menu(_resolve(install, game_path, False))",
        (
            T_LOA + "test_defaults_from_balance_of_power_first",
            T_LOA + "test_defaults_fall_back_to_the_install",
        ),
    ),
    Plant(
        "pilot-defaults-empty-list-minus-one",
        DE,
        "        return 0\n",
        "        return -1\n",
        (T_LOA + "test_defaults_edge_cases",),
    ),
    Plant(
        "pilot-defaults-missing-unchecked",
        DE,
        "    if path is None or not path.is_file():",
        "    if False:",
        (T_LOA + "test_defaults_edge_cases",),
    ),
]

_OUTPUT_PLANTS: list[Plant] = [
    Plant(
        "pilot-json-hex-upper",
        JS,
        '        return value.hex() if isinstance(value, bytes) else f"{value:02x}"',
        '        return value.hex().upper() if isinstance(value, bytes) else f"{value:02x}"',
        (
            T_OUT + "test_export_keeps_names_text_and_hex",
            T_OUT + "test_export_validates_against_schema",
        ),
    ),
    Plant(
        "pilot-json-text-all-bytes",
        JS,
        "        return latin1(value.text)",
        "        return latin1(value.raw)",
        (T_OUT + "test_export_keeps_names_text_and_hex",),
    ),
    Plant(
        "pilot-schema-i32-max",
        JS,
        "RANGES = {I32: (-(2**31), 2**31 - 1), U32: (0, 2**32 - 1)}",
        "RANGES = {I32: (-(2**31), 2**31), U32: (0, 2**32 - 1)}",
        (REJECTS + "[keys0-2147483647-2147483648]",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-i32-min",
        JS,
        "RANGES = {I32: (-(2**31), 2**31 - 1), U32: (0, 2**32 - 1)}",
        "RANGES = {I32: (-(2**31) - 1, 2**31 - 1), U32: (0, 2**32 - 1)}",
        (REJECTS + "[keys1--2147483648--2147483649]",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-u32-min",
        JS,
        "RANGES = {I32: (-(2**31), 2**31 - 1), U32: (0, 2**32 - 1)}",
        "RANGES = {I32: (-(2**31), 2**31 - 1), U32: (-1, 2**32 - 1)}",
        (REJECTS + "[keys2-4294967295--1]",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-text-one-longer",
        JS,
        '        return {"type": "string", "maxLength": run}',
        '        return {"type": "string", "maxLength": run + 1}',
        (REJECTS + "[keys3-xxxxxxxxxxxxxx-xxxxxxxxxxxxxxx]",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-hex-any-letter",
        JS,
        'f"^([0-9a-f]{{2}}){{{run}}}$"',
        'f"^([0-9a-z]{{2}}){{{run}}}$"',
        (REJECTS + "[keys4-0a-zz]",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-hex-any-count",
        JS,
        'f"^([0-9a-f]{{2}}){{{run}}}$"',
        'f"^([0-9a-f]{{2}})+$"',
        (REJECTS + "[keys5-0a-0a0]",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-arrays-no-minimum",
        JS,
        '"minItems": size, "maxItems": size,',
        '"maxItems": size,',
        (REJECTS + "[keys6-good6-bad6]",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-result-any-integer",
        JS,
        '            "result": {"enum": [0, 1]},',
        '            "result": {"type": "integer"},',
        (REJECTS + "[keys7-0-2]",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-format-any-string",
        JS,
        '            "format": {"const": JSON_FORMAT},',
        '            "format": {"type": "string"},',
        (T_OUT + "test_schema_rejects_unknown_and_missing_members",),
        regen_schema=True,
    ),
    Plant(
        "pilot-schema-drift",
        JS,
        '        "title": "XvT/BoP pilot (jedimaster export)",',
        '        "title": "XvT/BoP pilot",',
        (T_OUT + "test_schema_file_matches_model",),
    ),
    Plant(
        "pilot-cli-missing-file-exit-1",
        CL,
        '        logger.error("pilot file not found: %s", path)\n        return 2',
        '        logger.error("pilot file not found: %s", path)\n        return 1',
        (T_OUT + "test_pilot_dump",),
    ),
    Plant(
        "pilot-cli-balance-of-power-always",
        CL,
        "        defaults = install_defaults(install, not args.no_balance_of_power)",
        "        defaults = install_defaults(install, True)",
        (T_OUT + "test_pilot_load",),
    ),
    Plant(
        "pilot-cli-refused-exit-0",
        CL,
        '        logger.error("cannot load %s: %s", args.name, exc)\n        return 1',
        '        logger.error("cannot load %s: %s", args.name, exc)\n        return 0',
        (T_OUT + "test_pilot_load",),
    ),
    Plant(
        "pilot-cli-json-name",
        CL,
        'JSON_NAME = "pilot.json"',
        'JSON_NAME = "pilots.json"',
        (T_OUT + "test_pilot_export",),
    ),
]

PILOT_PLANTS: list[Plant] = [
    *_READ_PLANTS,
    *_RENDER_PLANTS,
    *_MERGE_PLANTS,
    *_LOAD_PLANTS,
    *_OUTPUT_PLANTS,
]
"""Every plant of the pilot sub-package, in the order above."""
