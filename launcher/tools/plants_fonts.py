"""The planted faults of the fonts: reader, drawing, colors, view, outputs, command line.

Purpose:
    Break one rule of ``jedimaster/fonts/`` per plant: the header's
    fields, each glyph code, the row lengths, the refusals; the glyph and
    string drawing, the color bytes, the spacing, the stop at x 640, the
    measure; the view; the sheet's form; the atlas and the JSON; each
    schema rule the rejection test names; the ``fonts`` command line.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``fonts-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant that changes the schema builder regenerates the schema files,
      so it fails the rejection test it names, not the drift test.

Call:
    ``from plants_fonts import FONTS_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

RE = "jedimaster/fonts/reader.py"
DR = "jedimaster/fonts/draw.py"
CO = "jedimaster/fonts/colors.py"
GM = "jedimaster/fonts/game.py"
RD = "jedimaster/fonts/render.py"
JS = "jedimaster/fonts/to_json.py"
CL = "jedimaster/fonts/cli.py"
T_F = "tests/test_fonts.py::"
T_O = "tests/test_fonts_output.py::"
REJECTS = T_O + "test_schema_rejects_bad_data"
FORM = T_O + "test_render_font_form"
ATLAS = T_O + "test_atlas_png_reads_back_white_glyphs_on_clear_ground"
PLACES = T_O + "test_export_validates_and_places_glyphs"
COLORS = T_F + "test_color_bytes_1_to_6"
STOP = T_F + "test_the_stop_at_x_640"

READ_PLANTS: list[Plant] = [
    Plant(
        "fonts-points-swapped",
        RE,
        "    points, in_use, spacing, spare = fields[769:]",
        "    in_use, points, spacing, spare = fields[769:]",
        (T_F + "test_header_fields",),
    ),
    Plant(
        "fonts-height-not-entry-0",
        RE,
        "        return self.heights[0]",
        "        return max(self.heights[1:])",
        (T_F + "test_the_height_is_entry_0",),
    ),
    Plant(
        "fonts-draw-run-bytes-read",
        RE,
        "            at += value - DRAW_RUN\n",
        "",
        (T_F + "test_every_code_kind",),
    ),
    Plant(
        "fonts-small-run-byte-read",
        RE,
        "            runs.append(Run(True, value))\n            at += 1",
        "            runs.append(Run(True, value))",
        (T_F + "test_every_code_kind",),
    ),
    Plant(
        "fonts-skip-drawn",
        RE,
        "            runs.append(Run(False, value - SKIP_RUN))",
        "            runs.append(Run(True, value - SKIP_RUN))",
        (T_F + "test_every_code_kind", T_F + "test_draw_glyph_follows_the_runs"),
    ),
    Plant(
        "fonts-skip-from-0x41",
        RE,
        "        elif value >= SKIP_RUN:",
        "        elif value > SKIP_RUN:",
        (T_F + "test_every_code_kind",),
    ),
    Plant(
        "fonts-rows-follow-lengths",
        RE,
        "        at = after\n",
        "        at = at + length\n",
        (T_F + "test_a_row_length_that_disagrees_is_reported",),
    ),
    Plant(
        "fonts-row-length-silent",
        RE,
        "        if length != after - at:",
        "        if False:",
        (T_F + "test_a_row_length_that_disagrees_is_reported",),
    ),
    Plant(
        "fonts-short-file-crash",
        RE,
        "    if len(raw) < HEADER_SIZE:",
        "    if len(raw) < 1:",
        (T_F + "test_unreadable_fonts_are_refused",),
    ),
    Plant(
        "fonts-data-past-size",
        RE,
        "    data = raw[HEADER_SIZE : HEADER_SIZE + size]",
        "    data = raw[HEADER_SIZE:]",
        (T_F + "test_unreadable_fonts_are_refused",),
    ),
    Plant(
        "fonts-cut-row-ends",
        RE,
        "        if at >= len(data):\n            raise",
        "        if at >= len(data):\n            return runs, at\n        if at < 0:\n"
        "            raise",
        (T_F + "test_unreadable_fonts_are_refused",),
    ),
    Plant(
        "fonts-size-mismatch-silent",
        RE,
        "    if len(raw) - HEADER_SIZE != size:",
        "    if False:",
        (T_F + "test_glyph_data_size_mismatch_is_reported",),
    ),
]

DRAW_PLANTS: list[Plant] = [
    Plant(
        "fonts-width-0-drawn",
        DR,
        "    if glyph.width == 0:",
        "    if False:",
        (T_F + "test_runs_past_the_width_still_draw_but_width_0_draws_nothing",),
    ),
    Plant(
        "fonts-clipped-to-width",
        DR,
        "                pixels += [Pixel(column + i, y + row, color) for i in range(run.count)]",
        "                pixels += [\n"
        "                    Pixel(column + i, y + row, color)\n"
        "                    for i in range(run.count)\n"
        "                    if column + i < x + glyph.width\n"
        "                ]",
        (T_F + "test_runs_past_the_width_still_draw_but_width_0_draws_nothing",),
    ),
    Plant(
        "fonts-skip-not-advancing",
        DR,
        "            column += run.count\n",
        "            column += run.count if run.drawn else 0\n",
        (T_F + "test_draw_glyph_follows_the_runs",),
    ),
    Plant(
        "fonts-reset-to-white",
        DR,
        "            current = color\n",
        "            current = 0xFFFF\n",
        (COLORS,),
    ),
    Plant(
        "fonts-color-bytes-shifted",
        DR,
        "            current = TEXT_COLORS[byte].color",
        "            current = TEXT_COLORS[max(byte - 1, 2)].color",
        (COLORS,),
    ),
    Plant(
        "fonts-colors-from-the-first-code",
        CO,
        "    byte: TextColor(byte, COLOR_CODES[byte - 1], name)",
        "    byte: TextColor(byte, COLOR_CODES[byte - 2], name)",
        (COLORS,),
    ),
    Plant(
        "fonts-reset-byte-2",
        CO,
        "RESET_BYTE = 1\n",
        "RESET_BYTE = 2\n",
        (COLORS,),
    ),
    Plant(
        "fonts-spacing-ignored",
        DR,
        "            pen += font.widths[byte] + font.spacing",
        "            pen += font.widths[byte]",
        (COLORS,),
    ),
    Plant(
        "fonts-stop-past-640",
        DR,
        "        if pen >= STOP_X:",
        "        if pen > STOP_X:",
        (STOP,),
    ),
    Plant(
        "fonts-no-stop",
        DR,
        "        if pen >= STOP_X:",
        "        if False:",
        (STOP,),
    ),
    Plant(
        "fonts-nul-drawn",
        DR,
        "    return text if stop < 0 else text[:stop]",
        "    return text",
        (T_F + "test_a_string_ends_at_byte_0", T_F + "test_measure_and_spacing"),
    ),
    Plant(
        "fonts-measure-keeps-last-spacing",
        DR,
        "    return total - font.spacing",
        "    return total",
        (T_F + "test_measure_and_spacing",),
    ),
    Plant(
        "fonts-measure-counts-codes",
        DR,
        "for b in _end(text) if b > LAST_CODE)",
        "for b in _end(text) if b > 0)",
        (T_F + "test_measure_and_spacing",),
    ),
]

OUTPUT_PLANTS: list[Plant] = [
    Plant(
        "fonts-view-base-only",
        GM,
        "    path = resolve(install, name, balance_of_power)",
        "    path = resolve(install, name, False)",
        (T_O + "test_view_resolves_in_any_case_balance_of_power_first",),
    ),
    Plant(
        "fonts-view-label-plain",
        GM,
        "    label = sheet_file(install, name, path)",
        "    label = name",
        (T_O + "test_view_resolves_in_any_case_balance_of_power_first",),
    ),
    Plant(
        "fonts-view-bad-font-raises",
        GM,
        "    except (FontFormatError, OSError) as exc:",
        "    except OSError as exc:",
        (T_O + "test_view_keeps_a_missing_or_bad_font_as_an_error",),
    ),
    Plant(
        "fonts-render-marks-swapped",
        RD,
        '        grid[pixel.y][pixel.x] = "#"',
        '        grid[pixel.y][pixel.x] = "."',
        (FORM,),
    ),
    Plant(
        "fonts-render-header-size-only",
        RD,
        "    width, height = max(glyph.width, right), max(glyph.height, bottom)",
        "    width, height = glyph.width, glyph.height",
        (FORM,),
    ),
    Plant(
        "fonts-render-base-black",
        RD,
        "BASE_COLOR = 0xFFFF\n",
        "BASE_COLOR = 0x0000\n",
        (FORM,),
    ),
    Plant(
        "fonts-render-spare-is-in-use",
        RD,
        "field_60a {font.spare} height",
        "field_60a {font.in_use} height",
        (FORM,),
    ),
    Plant(
        "fonts-render-five-codes",
        RD,
        '" ".join(f"{c:04x}" for c in COLOR_CODES)',
        '" ".join(f"{c:04x}" for c in COLOR_CODES[1:])',
        (FORM,),
    ),
    Plant(
        "fonts-render-unwritten-dots",
        RD,
        'UNWRITTEN = "----"',
        'UNWRITTEN = "...."',
        (FORM,),
    ),
    Plant(
        "fonts-render-measure-is-pen",
        RD,
        "measure={measure_string(font, text)}",
        "measure={draw_string(font, text, BASE_COLOR).pen}",
        (FORM,),
    ),
    Plant(
        "fonts-atlas-8-columns",
        JS,
        "COLUMNS = 16\n",
        "COLUMNS = 8\n",
        (ATLAS, PLACES),
    ),
    Plant(
        "fonts-atlas-grey-ink",
        JS,
        'INK = b"\\xff\\xff\\xff\\xff"',
        'INK = b"\\x80\\x80\\x80\\xff"',
        (ATLAS,),
    ),
    Plant(
        "fonts-atlas-places-transposed",
        JS,
        "        left = (code % COLUMNS) * cell_width",
        "        left = (code // COLUMNS) * cell_width",
        (ATLAS, PLACES),
    ),
    Plant(
        "fonts-atlas-name-keeps-suffix",
        JS,
        '    return f"{PurePath(name.lower()).stem}.png"',
        '    return f"{name}.png"',
        (PLACES,),
    ),
    Plant(
        "fonts-json-missing-glyphs-empty",
        JS,
        "        data.update(text_colors=None, glyphs=None)",
        "        data.update(text_colors=None, glyphs=[])",
        (T_O + "test_export_of_a_missing_font_validates",),
    ),
    Plant(
        "fonts-schema-code-unbounded",
        JS,
        '            **ints("code", maximum=255),',
        '            **ints("code"),',
        (REJECTS + "[glyph-code]",),
        regen_schema=True,
    ),
    Plant(
        "fonts-schema-glyphs-any-count",
        JS,
        '                    "minItems": 256,\n                    "maxItems": 256,\n',
        "",
        (REJECTS + "[glyph-count]",),
        regen_schema=True,
    ),
    Plant(
        "fonts-schema-color-byte-any",
        JS,
        '            **ints("byte", minimum=2, maximum=6),',
        '            **ints("byte"),',
        (REJECTS + "[color-byte]",),
        regen_schema=True,
    ),
    Plant(
        "fonts-schema-columns-any",
        JS,
        '            "columns": {"const": COLUMNS},',
        '            "columns": {"type": "integer"},',
        (REJECTS + "[atlas-columns]",),
        regen_schema=True,
    ),
    Plant(
        "fonts-schema-picture-any",
        JS,
        r'"pattern": r"^[a-z0-9_.-]+\.png$"',
        r'"pattern": r"^.+$"',
        (REJECTS + "[picture-name]",),
        regen_schema=True,
    ),
    Plant(
        "fonts-schema-reset-any",
        JS,
        '            "reset_byte": {"const": RESET_BYTE},',
        '            "reset_byte": {"type": "integer"},',
        (REJECTS + "[reset-byte]",),
        regen_schema=True,
    ),
    Plant(
        "fonts-schema-file-stale",
        JS,
        '"title": "XvT/BoP menu fonts (jedimaster export)",',
        '"title": "XvT/BoP menu fonts",',
        (T_O + "test_schema_file_matches_model",),
    ),
    Plant(
        "fonts-dump-samples-dropped",
        CL,
        "        samples = [unquote(text) for text in args.text]",
        "        samples = []",
        (T_O + "test_fonts_dump",),
    ),
    Plant(
        "fonts-dump-name-case",
        CL,
        "render_font(font, path.name.lower(), str(path), samples)",
        "render_font(font, path.name, str(path), samples)",
        (T_O + "test_fonts_dump",),
    ),
    Plant(
        "fonts-dump-missing-status-1",
        CL,
        '        logger.error("font not found: %s", args.file)\n        return 2',
        '        logger.error("font not found: %s", args.file)\n        return 1',
        (T_O + "test_fonts_dump",),
    ),
    Plant(
        "fonts-dump-bad-status-0",
        CL,
        '        logger.error("cannot read %s: %s", path, exc)\n        return 1',
        '        logger.error("cannot read %s: %s", path, exc)\n        return 0',
        (T_O + "test_fonts_dump",),
    ),
    Plant(
        "fonts-export-always-bop",
        CL,
        "balance_of_power=not args.no_balance_of_power",
        "balance_of_power=True",
        (T_O + "test_fonts_export",),
    ),
    Plant(
        "fonts-export-atlas-by-kind",
        CL,
        "            target = out / atlas_name(font_file.name)",
        '            target = out / f"{font_file.kind}.png"',
        (T_O + "test_fonts_export",),
    ),
    Plant(
        "fonts-export-missing-font-ok",
        CL,
        "    return 0 if written == len(view.fonts) else 1",
        "    return 0",
        (T_O + "test_fonts_export",),
    ),
    Plant(
        "fonts-export-write-failure-hidden",
        CL,
        '        logger.error("cannot export to %s: %s", out, exc)\n        return 1',
        '        logger.error("cannot export to %s: %s", out, exc)\n        return 0',
        (T_O + "test_fonts_export",),
    ),
    Plant(
        "fonts-export-no-install-status-1",
        CL,
        '        logger.error("not an install: %s", args.install)\n        return 2',
        '        logger.error("not an install: %s", args.install)\n        return 1',
        (T_O + "test_fonts_export",),
    ),
]

FONTS_PLANTS: list[Plant] = [*READ_PLANTS, *DRAW_PLANTS, *OUTPUT_PLANTS]
