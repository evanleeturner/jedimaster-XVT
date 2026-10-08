"""The planted faults of the icons: bitmap reader, view, outputs, command line.

Purpose:
    Break one piece of ``jedimaster/icons/`` per plant: the game's RLE8
    decoding (spills, moves, drops, the end code, padding), the fixed places
    of the palette and the pixels, each refusal, the 565 rule, the IFF and
    craft tables, the icon's position, index 0, the Balance of Power order,
    the three renderings, the JSON export and its schema, the PNG writer and
    the ``icons`` command line.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``icons-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant that changes the schema builder regenerates the schema files,
      so it fails the rejection test it names, not the drift test.

Call:
    ``from plants_icons import ICONS_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

B = "jedimaster/icons/bmp.py"
G = "jedimaster/icons/game.py"
TB = "jedimaster/icons/tables.py"
R = "jedimaster/icons/render.py"
J = "jedimaster/icons/to_json.py"
PNG = "jedimaster/icons/png.py"
CL = "jedimaster/icons/cli.py"
T_BMP = "tests/test_icons_bmp.py::"
T_GAME = "tests/test_icons_game.py::"
T_OUT = "tests/test_icons_output.py::"
T_CLI = "tests/test_icons_cli.py::"
REFUSED = T_BMP + "test_unreadable_files_are_refused"
RULE = T_GAME + "test_565_rule"
PLACE = T_GAME + "test_position_for_odd_and_even_sizes"
REJECTS = T_OUT + "test_schema_rejects_bad_data"
ENDLESS = T_BMP + "test_missing_end_code_is_reported"

BMP_PLANTS: list[Plant] = [
    # The game's RLE8 decoding.
    Plant(
        "icons-run-short",
        B,
        "            for _ in range(n):",
        "            for _ in range(n - 1):",
        (T_BMP + "test_run_writes_value_n_times",),
    ),
    Plant(
        "icons-eol-two-rows",
        B,
        "        self.row_start -= self.width\n",
        "        self.row_start -= 2 * self.width\n",
        (T_BMP + "test_end_of_line_moves_up_one_row",),
    ),
    Plant(
        "icons-end-ignored",
        B,
        "            end = True\n            break",
        "            end = True",
        (T_BMP + "test_end_of_bitmap_stops_reading",),
    ),
    Plant(
        "icons-move-swapped",
        B,
        "writer.move(data[i], data[i + 1])",
        "writer.move(data[i + 1], data[i])",
        (T_BMP + "test_move_goes_up_dy_rows_and_right_dx_columns",),
    ),
    Plant(
        "icons-odd-padding-kept",
        B,
        "            i += v + (v & 1)",
        "            i += v",
        (T_BMP + "test_odd_stretch_skips_its_padding_byte",),
    ),
    Plant(
        "icons-even-padding-skipped",
        B,
        "            i += v + (v & 1)",
        "            i += v + 1",
        (T_BMP + "test_even_stretch_has_no_padding_byte",),
    ),
    Plant(
        "icons-spill-clipped",
        B,
        "                self.spilled += 1\n",
        "                self.spilled += 1\n                self.position += 1\n"
        "                return\n",
        (
            T_BMP + "test_run_past_the_right_edge_lands_on_the_row_below",
            T_BMP + "test_stretch_past_the_right_edge_lands_on_the_row_below",
            T_BMP + "test_later_row_overwrites_a_spilled_pixel",
            T_BMP + "test_move_after_a_spill_keeps_the_column_past_the_width",
        ),
    ),
    Plant(
        "icons-spill-uncounted",
        B,
        "                self.spilled += 1\n",
        "                pass\n",
        (
            T_BMP + "test_run_past_the_right_edge_lands_on_the_row_below",
            T_BMP + "test_stretch_past_the_right_edge_lands_on_the_row_below",
            T_BMP + "test_later_row_overwrites_a_spilled_pixel",
        ),
    ),
    Plant(
        "icons-move-column-wrapped",
        B,
        "        column = self.position - self.row_start\n",
        "        column = (self.position - self.row_start) % max(self.width, 1)\n",
        (T_BMP + "test_move_after_a_spill_keeps_the_column_past_the_width",),
    ),
    Plant(
        "icons-before-start-wraps",
        B,
        "        if 0 <= self.position < len(self.buffer):",
        "        if self.position < len(self.buffer):",
        (T_BMP + "test_writes_before_the_buffer_are_counted_and_dropped",),
    ),
    Plant(
        "icons-past-end-written",
        B,
        "        if 0 <= self.position < len(self.buffer):",
        "        if 0 <= self.position <= len(self.buffer):",
        (
            T_BMP + "test_writes_past_the_buffer_are_counted_and_dropped",
            T_BMP + "test_random_codes_never_crash_or_write_outside",
        ),
    ),
    Plant(
        "icons-drops-uncounted",
        B,
        "            self.dropped += 1",
        "            pass",
        (
            T_BMP + "test_writes_before_the_buffer_are_counted_and_dropped",
            T_BMP + "test_writes_past_the_buffer_are_counted_and_dropped",
        ),
    ),
    Plant(
        "icons-drops-unwarned",
        B,
        "    if bmp.dropped:",
        "    if bmp.dropped > 2:",
        (T_BMP + "test_writes_before_the_buffer_are_counted_and_dropped",),
    ),
    Plant(
        "icons-end-flag-inverted",
        B,
        "writer.buffer, writer.spilled, writer.dropped, not end,",
        "writer.buffer, writer.spilled, writer.dropped, end,",
        (T_BMP + "test_end_of_bitmap_stops_reading", ENDLESS),
    ),
    Plant(
        "icons-end-unwarned",
        B,
        "    if bmp.end_missing:",
        "    if bmp.end_missing and bmp.bytes_read > 9:",
        (ENDLESS + "[run]", ENDLESS + "[move]", ENDLESS + "[stretch]"),
    ),
    Plant(
        "icons-move-unguarded",
        B,
        "            if i + 2 > len(data):\n                i = len(data)\n"
        "                break\n",
        "",
        (ENDLESS + "[move]", T_BMP + "test_random_codes_never_crash_or_write_outside"),
    ),
    Plant(
        "icons-bytes-read-unclamped",
        B,
        "not end, min(i, len(data))",
        "not end, i",
        (ENDLESS + "[stretch]",),
    ),
    Plant(
        "icons-half-code-counted",
        B,
        "not end, min(i, len(data))",
        "not end, len(data)",
        (ENDLESS + "[half-code]",),
    ),
    Plant(
        "icons-rle-reads-whole-file",
        B,
        "        codes = data[DATA_OFFSET : DATA_OFFSET + info[6]]",
        "        codes = data[DATA_OFFSET:]",
        (T_BMP + "test_rle_reads_only_the_image_size_bytes",),
    ),
    Plant(
        "icons-image-size-0-reads-all",
        B,
        "        codes = data[DATA_OFFSET : DATA_OFFSET + info[6]]",
        "        codes = data[DATA_OFFSET : DATA_OFFSET + (info[6] or len(data))]",
        (T_BMP + "test_an_image_size_of_0_decodes_nothing",),
    ),
    Plant(
        "icons-missing-bytes-uncounted",
        B,
        "        missing = info[6] - len(codes)",
        "        missing = 0",
        (T_BMP + "test_a_file_short_of_its_image_size_decodes_what_is_there",),
    ),
    Plant(
        "icons-missing-bytes-unwarned",
        B,
        "    if bmp.bytes_missing:",
        "    if bmp.bytes_missing > 6:",
        (T_BMP + "test_a_file_short_of_its_image_size_decodes_what_is_there",),
    ),
    # Uncompressed rows, the palette, the pixel data's place, the headers.
    Plant(
        "icons-plain-unpadded",
        B,
        "    stride = (width + 3) & ~3",
        "    stride = width",
        (
            T_BMP + "test_uncompressed_rows_are_padded_to_four[5]",
            T_BMP + "test_uncompressed_rows_are_padded_to_four[6]",
            T_BMP + "test_uncompressed_rows_are_padded_to_four[7]",
        ),
    ),
    Plant(
        "icons-plain-top-down",
        B,
        "        top = (height - 1 - row) * width",
        "        top = row * width",
        (
            T_BMP + "test_uncompressed_rows_are_padded_to_four[4]",
            T_BMP + "test_uncompressed_short_data_leaves_zeros_and_is_reported",
        ),
    ),
    Plant(
        "icons-plain-short-unreported",
        B,
        "len(data) < wanted,",
        "len(data) < 0,",
        (T_BMP + "test_uncompressed_short_data_leaves_zeros_and_is_reported",),
    ),
    Plant(
        "icons-palette-rgb-order",
        B,
        "        blue, green, red = data[",
        "        red, green, blue = data[",
        (
            T_BMP + "test_palette_and_pixels_at_fixed_places_whatever_the_headers_say",
            T_OUT + "test_png_reads_back_to_the_palette_colors_with_index_0_clear",
        ),
    ),
    Plant(
        "icons-data-at-header-offset",
        B,
        "        codes = data[DATA_OFFSET : DATA_OFFSET + info[6]]",
        "        codes = data[head[4] : head[4] + info[6]]",
        (T_BMP + "test_palette_and_pixels_at_fixed_places_whatever_the_headers_say",),
    ),
    Plant(
        "icons-offset-difference-hidden",
        B,
        "offset_differs=head[4] != DATA_OFFSET,",
        "offset_differs=False,",
        (T_BMP + "test_palette_and_pixels_at_fixed_places_whatever_the_headers_say",),
    ),
    Plant(
        "icons-offset-always-differs",
        B,
        "offset_differs=head[4] != DATA_OFFSET,",
        "offset_differs=head[4] != 0,",
        (T_BMP + "test_offset_at_1078_is_not_reported",),
    ),
    Plant(
        "icons-offset-unwarned",
        B,
        "    if bmp.offset_differs:",
        "    if bmp.offset_differs and bmp.width > 4:",
        (T_BMP + "test_palette_and_pixels_at_fixed_places_whatever_the_headers_say",),
    ),
    Plant(
        "icons-header-fields-swapped",
        B,
        "    x_pixels_per_meter: int\n    y_pixels_per_meter: int\n",
        "    y_pixels_per_meter: int\n    x_pixels_per_meter: int\n",
        (T_BMP + "test_header_fields_are_kept_as_written",),
    ),
    Plant(
        "icons-path-read-short",
        B,
        "            data = handle.read()",
        "            data = handle.read()[:-1]",
        (T_BMP + "test_a_path_reads_as_its_bytes",),
    ),
    # Each refusal.
    Plant(
        "icons-mark-unchecked",
        B,
        '    if head[0] != b"BM":',
        '    if head[0] not in (b"BM", b"BA"):',
        (REFUSED + "[mark]",),
    ),
    Plant(
        "icons-planes-unchecked",
        B,
        "    if planes != 1:",
        "    if planes > 2:",
        (REFUSED + "[planes]",),
    ),
    Plant(
        "icons-depth-unchecked",
        B,
        "    if bits != 8:",
        "    if bits not in (8, 4, 24):",
        (REFUSED + "[bits4]", REFUSED + "[bits24]"),
    ),
    Plant(
        "icons-compression-unchecked",
        B,
        "    if compression not in (PLAIN, RLE8):",
        "    if compression > 3:",
        (REFUSED + "[rle4]", REFUSED + "[bitfields]"),
    ),
    Plant(
        "icons-height-unchecked",
        B,
        "    if height < 0:",
        "    if height < -1:",
        (REFUSED + "[height]",),
    ),
    Plant(
        "icons-width-unchecked",
        B,
        "    if width < 0:",
        "    if width < -4:",
        (REFUSED + "[width]",),
    ),
    Plant(
        "icons-pixel-limit-raised",
        B,
        "MAX_PIXELS = 1 << 26",
        "MAX_PIXELS = 1 << 27",
        (REFUSED + "[pixels]",),
    ),
    Plant(
        "icons-short-file-unchecked",
        B,
        "    if len(data) < DATA_OFFSET:",
        "    if len(data) < DATA_OFFSET - 1:",
        (T_BMP + "test_a_file_too_short_for_its_palette_is_refused",),
    ),
]

GAME_PLANTS: list[Plant] = [
    # The 565 rule.
    Plant(
        "icons-565-green-five-bits",
        G,
        "    return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)",
        "    return (red >> 3) << 11 | (green >> 3) << 6 | (blue >> 3)",
        (RULE + "[white]", RULE + "[green]", RULE + "[step]", RULE + "[top]"),
    ),
    Plant(
        "icons-565-red-rounded",
        G,
        "    return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)",
        "    return ((red + 4) >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)",
        (RULE + "[red]", RULE + "[below]", RULE + "[near-top]"),
    ),
    Plant(
        "icons-565-stray-bit",
        G,
        "    return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3)",
        "    return (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3) | 0x20",
        (RULE + "[black]", RULE + "[blue]", RULE + "[below]"),
    ),
    # The IFF and craft tables, the position.
    Plant(
        "icons-iff4-purple",
        G,
        '    4: "mapicon1",',
        '    4: "mapicon4",',
        (T_GAME + "test_iff_table_for_all_256_values",),
    ),
    Plant(
        "icons-other-iff-red",
        G,
        'OTHER_SHEET = "mapicon0"',
        'OTHER_SHEET = "mapicon1"',
        (T_GAME + "test_iff_table_for_all_256_values",),
    ),
    Plant(
        "icons-craft-past-table",
        G,
        "    if 0 <= craft < len(CRAFT_BOXES):",
        "    if 0 <= craft <= len(CRAFT_BOXES):",
        (
            T_GAME + "test_craft_types_105_106_and_255",
            T_GAME + "test_no_icon_for_a_craft_without_a_box",
        ),
    ),
    Plant(
        "icons-box-past-sheet",
        TB,
        "    Box(  6,   9,  13,  20),  # 0",
        "    Box(  6,   9,  13, 200),  # 0",
        (T_GAME + "test_tables_lie_inside_a_sheet_and_name_existing_boxes",),
    ),
    Plant(
        "icons-craft-names-no-box",
        TB,
        "    67, 68, 69,  0,",
        "    67, 68, 70,  0,",
        (
            T_GAME + "test_tables_lie_inside_a_sheet_and_name_existing_boxes",
            T_OUT + "test_render_draws_runs_every_iff_and_craft",
        ),
    ),
    Plant(
        "icons-width-exclusive",
        TB,
        "        return self.right - self.left + 1",
        "        return self.right - self.left",
        (PLACE + "[two]",),
    ),
    Plant(
        "icons-height-exclusive",
        TB,
        "        return self.bottom - self.top + 1",
        "        return self.bottom - self.top",
        (PLACE + "[two]",),
    ),
    Plant(
        "icons-origin-rounded-up",
        G,
        "    return -(box.width >> 1), -(box.height >> 1)",
        "    return -((box.width + 1) >> 1), -(box.height >> 1)",
        (PLACE + "[odd]", PLACE + "[one]", PLACE + "[odd-even]"),
    ),
    Plant(
        "icons-origin-height-from-width",
        G,
        "    return -(box.width >> 1), -(box.height >> 1)",
        "    return -(box.width >> 1), -(box.width >> 1)",
        (PLACE + "[even]", PLACE + "[odd]", PLACE + "[odd-even]"),
    ),
    # The view: Balance of Power first, compressed images, failures.
    Plant(
        "icons-sheet-base-only",
        G,
        "    path = resolve(install, game_path, balance_of_power)",
        "    path = resolve(install, game_path, False)",
        (T_GAME + "test_view_reads_balance_of_power_first_with_a_sheet_missing",),
    ),
    Plant(
        "icons-sheet-always-bop",
        G,
        "_read_sheet(install, image.name, image.bitmap, balance_of_power)",
        "_read_sheet(install, image.name, image.bitmap, True)",
        (
            T_GAME + "test_view_without_balance_of_power_reads_the_base_install",
            T_CLI + "test_icons_export_without_balance_of_power",
        ),
    ),
    Plant(
        "icons-sheet-label-raw",
        G,
        "    label = sheet_file(install, game_path, path)",
        "    label = game_path",
        (T_GAME + "test_view_reads_balance_of_power_first_with_a_sheet_missing",),
    ),
    Plant(
        "icons-compressed-read",
        G,
        "        if image.compressed:",
        "        if image.compressed and image.width > 8:",
        (T_GAME + "test_view_keeps_a_compressed_image_without_pixels",),
    ),
    Plant(
        "icons-missing-list-unchecked",
        G,
        "    if list_path is None or not list_path.is_file():",
        "    if False:",
        (
            T_GAME + "test_view_without_its_list_is_refused",
            T_CLI + "test_icons_export_statuses",
        ),
    ),
    Plant(
        "icons-bad-sheet-raises",
        G,
        "    except (BmpFormatError, OSError) as exc:",
        "    except OSError as exc:",
        (T_GAME + "test_an_unreadable_sheet_is_left_out_with_a_warning",),
    ),
    # Drawing one icon.
    Plant(
        "icons-index-0-drawn",
        G,
        "            if index != TRANSPARENT:",
        "            if index >= TRANSPARENT:",
        (
            T_GAME + "test_draw_skips_index_0_and_places_the_box_on_p",
            T_GAME + "test_an_all_zero_box_draws_nothing",
        ),
    ),
    Plant(
        "icons-drawn-at-corner",
        G,
        "    x, y = icon_origin(box)",
        "    x, y = 0, 0",
        (T_GAME + "test_draw_skips_index_0_and_places_the_box_on_p",),
    ),
    Plant(
        "icons-index-as-color",
        G,
        "                color = sheet.colors[index]",
        "                color = index",
        (T_GAME + "test_draw_skips_index_0_and_places_the_box_on_p",),
    ),
    Plant(
        "icons-no-box-drawn",
        G,
        "    if number is None:\n        return None\n",
        "",
        (T_GAME + "test_no_icon_for_a_craft_without_a_box",),
    ),
    Plant(
        "icons-box-unclipped",
        G,
        "    if not (inside and 0 <= box.top <= box.bottom < bitmap.height):",
        "    if not (inside or True):",
        (T_GAME + "test_no_icon_when_the_box_lies_outside_the_sheet",),
    ),
]

OUTPUT_PLANTS: list[Plant] = [
    # The three renderings and the dump.
    Plant(
        "icons-colors-uppercase",
        R,
        '" ".join(f"{c:04x}" for c in sheet.colors)',
        '" ".join(f"{c:04X}" for c in sheet.colors)',
        (T_OUT + "test_render_sheets_prints_header_images_colors_and_rows",),
    ),
    Plant(
        "icons-result-inverted",
        R,
        '        f"result {view.images.result}",',
        '        f"result {1 - view.images.result}",',
        (T_OUT + "test_render_sheets_prints_header_images_colors_and_rows",),
    ),
    Plant(
        "icons-crafts-count",
        R,
        '    lines.append(f"crafts {len(CRAFT_BOXES)}")',
        '    lines.append(f"crafts {len(BOXES)}")',
        (T_OUT + "test_render_tables_prints_every_box_and_craft",),
    ),
    Plant(
        "icons-unwritten-dots",
        R,
        'UNWRITTEN = "----"',
        'UNWRITTEN = "...."',
        (T_OUT + "test_draw_lines_print_the_written_pixels_bounding_box",),
    ),
    Plant(
        "icons-bounding-box-short",
        R,
        "    width, height = max(xs) - left + 1, max(ys) - top + 1",
        "    width, height = max(xs) - left + 1, max(ys) - top",
        (T_OUT + "test_draw_lines_print_the_written_pixels_bounding_box",),
    ),
    Plant(
        "icons-iff-255-skipped",
        R,
        "DRAW_IFFS = (0, 1, 2, 3, 4, 5, 6, 7, 255)",
        "DRAW_IFFS = (0, 1, 2, 3, 4, 5, 6, 7)",
        (T_OUT + "test_render_draws_runs_every_iff_and_craft",),
    ),
    Plant(
        "icons-dump-prints-pixels",
        R,
        '        if field.name in ("palette", "pixels"):',
        '        if field.name in ("palette",):',
        (T_OUT + "test_render_bmp_prints_header_and_decoding_not_pixels",),
    ),
    # The JSON export and its schema.
    Plant(
        "icons-json-width-text",
        J,
        '            "width": sheet.bitmap.width,',
        '            "width": str(sheet.bitmap.width),',
        (
            T_OUT + "test_export_validates_against_schema",
            T_OUT + "test_export_holds_sheets_tables_and_pictures",
        ),
    ),
    Plant(
        "icons-json-file-label",
        J,
        '            "file": _relative(sheet.path, view.install),',
        '            "file": sheet.file,',
        (T_OUT + "test_export_holds_sheets_tables_and_pictures",),
    ),
    Plant(
        "icons-picture-keeps-slash",
        J,
        'UNSAFE = re.compile(r"[^A-Za-z0-9_.-]")',
        'UNSAFE = re.compile(r"[^A-Za-z0-9_./-]")',
        (T_OUT + "test_picture_names_are_file_safe",),
    ),
    Plant(
        "icons-picture-empty-name",
        J,
        "{UNSAFE.sub('_', name) or '_'}",
        "{UNSAFE.sub('_', name)}",
        (T_OUT + "test_picture_names_are_file_safe",),
    ),
    Plant(
        "icons-schema-title-stale",
        J,
        '"XvT/BoP briefing map icons (jedimaster export)"',
        '"XvT/BoP briefing map icons"',
        (T_OUT + "test_schema_file_matches_model",),
    ),
    Plant(
        "icons-schema-format-open",
        J,
        '            "format": {"const": JSON_FORMAT},',
        '            "format": {"type": "string"},',
        (REJECTS + "[format]",),
        regen_schema=True,
    ),
    Plant(
        "icons-schema-iffs-short",
        J,
        '                "minItems": IFF_VALUES,',
        '                "minItems": 0,',
        (REJECTS + "[iffs]",),
        regen_schema=True,
    ),
    Plant(
        "icons-schema-objects-open",
        J,
        '        "additionalProperties": False,',
        '        "additionalProperties": True,',
        (REJECTS + "[extra]",),
        regen_schema=True,
    ),
    Plant(
        "icons-schema-picture-any",
        J,
        r'"pattern": r"^[A-Za-z0-9_.-]+\.png$"',
        r'"pattern": r"^.+\.png$"',
        (REJECTS + "[picture]",),
        regen_schema=True,
    ),
    Plant(
        "icons-schema-negative-sizes",
        J,
        "minimum: int | None = 0",
        "minimum: int | None = None",
        (REJECTS + "[width]",),
        regen_schema=True,
    ),
    Plant(
        "icons-schema-color-any",
        J,
        '            "color": {"anyOf": [text, {"type": "null"}]},',
        '            "color": {},',
        (REJECTS + "[color]",),
        regen_schema=True,
    ),
    Plant(
        "icons-schema-ints-untyped",
        J,
        '    kind: dict[str, Any] = {"type": "integer"}',
        "    kind: dict[str, Any] = {}",
        (REJECTS + "[left]", REJECTS + "[box]"),
        regen_schema=True,
    ),
    Plant(
        "icons-schema-index-any",
        J,
        '            "transparent_index": {"const": TRANSPARENT},',
        '            "transparent_index": {"type": "integer"},',
        (REJECTS + "[index]",),
        regen_schema=True,
    ),
    # The PNG writer.
    Plant(
        "icons-png-filter-sub",
        PNG,
        "        raw.append(0)",
        "        raw.append(1)",
        (T_OUT + "test_png_reads_back_to_the_palette_colors_with_index_0_clear",),
    ),
    Plant(
        "icons-png-index-0-opaque",
        PNG,
        "0 if index == TRANSPARENT else 255",
        "255",
        (T_OUT + "test_png_reads_back_to_the_palette_colors_with_index_0_clear",),
    ),
    Plant(
        "icons-png-empty-allowed",
        PNG,
        "    if bmp.width <= 0 or bmp.height <= 0:",
        "    if bmp.width < 0 or bmp.height < 0:",
        (T_OUT + "test_png_of_an_empty_bitmap_is_refused",),
    ),
    # The command line.
    Plant(
        "icons-dump-game-path-ignored",
        CL,
        "        path = resolved\n",
        "        path = path\n",
        (T_CLI + "test_icons_dump_by_path_and_by_game_path",),
    ),
    Plant(
        "icons-dump-bad-file-status",
        CL,
        '        logger.error("cannot read bitmap %s: %s", path, exc)\n        return 1',
        '        logger.error("cannot read bitmap %s: %s", path, exc)\n        return 2',
        (T_CLI + "test_icons_dump_statuses",),
    ),
    Plant(
        "icons-export-picture-name",
        CL,
        "            target = out / picture_name(sheet.name)",
        '            target = out / f"{sheet.name}.bmp.png"',
        (T_CLI + "test_icons_export_writes_json_and_pictures",),
    ),
    Plant(
        "icons-export-always-bop",
        CL,
        "balance_of_power=not args.no_balance_of_power",
        "balance_of_power=True",
        (T_CLI + "test_icons_export_without_balance_of_power",),
    ),
    Plant(
        "icons-export-write-failure-hidden",
        CL,
        '        logger.error("cannot export to %s: %s", out, exc)\n        return 1',
        '        logger.error("cannot export to %s: %s", out, exc)\n        return 0',
        (T_CLI + "test_icons_export_statuses",),
    ),
]

ICONS_PLANTS: list[Plant] = [*BMP_PLANTS, *GAME_PLANTS, *OUTPUT_PLANTS]
