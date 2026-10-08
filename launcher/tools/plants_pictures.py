"""The planted faults of the pictures: the list, the view, the hashes, the outputs.

Purpose:
    Break one rule of ``jedimaster/pictures/`` per plant: which files the
    list takes from which folders, their case, repeats and order; how a
    view resolves and reads each name; the two hashes; the sheet line; the
    PNG names, the JSON and each schema rule the rejection test names; the
    ``pictures`` command line.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``pictures-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant that changes the schema builder regenerates the schema files,
      so it fails the rejection test it names, not the drift test.

Call:
    ``from plants_pictures import PICTURES_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import Plant

logger = logging.getLogger(__name__)

GM = "jedimaster/pictures/game.py"
RD = "jedimaster/pictures/render.py"
JS = "jedimaster/pictures/to_json.py"
CL = "jedimaster/pictures/cli.py"
T_P = "tests/test_pictures.py::"
REJECTS = T_P + "test_schema_rejects_bad_data"
LIST = T_P + "test_list_joins_both_folders_lowercased_without_repeats"
ORDER = T_P + "test_list_order_passes_over_punctuation"
HASHES = T_P + "test_hashes_on_known_input"
EXPORT_JSON = T_P + "test_export_validates_and_gives_index_0"
EXPORT = T_P + "test_pictures_export"
DUMP = T_P + "test_pictures_dump"

PICTURES_PLANTS: list[Plant] = [
    Plant(
        "pictures-names-keep-case",
        GM,
        "        raw = os.fsencode(entry).lower()",
        "        raw = os.fsencode(entry)",
        (LIST,),
    ),
    Plant(
        "pictures-any-file",
        GM,
        "        if raw.endswith(SUFFIX) and (folder / entry).is_file():",
        "        if (folder / entry).is_file():",
        (LIST,),
    ),
    Plant(
        "pictures-base-folder-exact-case",
        GM,
        "    base = child_in_any_case(install, FOLDER)",
        "    base = install / FOLDER",
        (LIST,),
    ),
    Plant(
        "pictures-bop-folder-left-out",
        GM,
        "    names = set(_bmp_names(base)) | set(_bmp_names(bop_folder))",
        "    names = set(_bmp_names(base))",
        (LIST,),
    ),
    Plant(
        "pictures-repeats-kept",
        GM,
        "    names = set(_bmp_names(base)) | set(_bmp_names(bop_folder))",
        "    names = _bmp_names(base) + _bmp_names(bop_folder)",
        (LIST,),
    ),
    Plant(
        "pictures-byte-order",
        GM,
        'listed = sorted((f"{FOLDER}\\\\{name}" for name in names), key=name_order)',
        'listed = sorted((f"{FOLDER}\\\\{name}" for name in names), key=os.fsencode)',
        (LIST,),
    ),
    Plant(
        "pictures-order-punctuation-counted",
        GM,
        "    return bytes(b for b in raw if chr(b).isalnum() and b < 0x80), raw",
        "    return raw, raw",
        (ORDER,),
    ),
    Plant(
        "pictures-order-no-tie-break",
        GM,
        "    return bytes(b for b in raw if chr(b).isalnum() and b < 0x80), raw",
        '    return bytes(b for b in raw if chr(b).isalnum() and b < 0x80), b""',
        (ORDER,),
    ),
    Plant(
        "pictures-fnv-offset",
        GM,
        "FNV_OFFSET = 0xCBF29CE484222325",
        "FNV_OFFSET = 0",
        (HASHES,),
    ),
    Plant(
        "pictures-fnv-1-not-1a",
        GM,
        "        value = ((value ^ byte) * FNV_PRIME) & MASK_64",
        "        value = ((value * FNV_PRIME) ^ byte) & MASK_64",
        (HASHES,),
    ),
    Plant(
        "pictures-colors-high-byte-first",
        GM,
        'struct.pack("<H", color) for color in colors',
        'struct.pack(">H", color) for color in colors',
        (HASHES,),
    ),
    Plant(
        "pictures-colors-reversed",
        GM,
        "    colors = [color_565(*entry) for entry in bitmap.palette]",
        "    colors = [color_565(*entry) for entry in bitmap.palette[::-1]]",
        (HASHES,),
    ),
    Plant(
        "pictures-view-base-only",
        GM,
        "    path = resolve(install, name, balance_of_power)",
        "    path = resolve(install, name, False)",
        (T_P + "test_each_view_resolves_every_name",),
    ),
    Plant(
        "pictures-missing-not-loaded",
        GM,
        "        return Picture(name, MISSING, None, None, None, [], None)",
        "        return Picture(name, NOT_LOADED, None, None, None, [], None)",
        (T_P + "test_render_lines",),
    ),
    Plant(
        "pictures-refusal-raises",
        GM,
        "    except (BmpFormatError, OSError) as exc:",
        "    except OSError as exc:",
        (T_P + "test_a_refused_picture_is_not_loaded", DUMP),
    ),
    Plant(
        "pictures-render-colors-is-pixels",
        RD,
        'f"colors={colors_hash(picture.colors):016x}"',
        'f"colors={pixels_hash(bitmap):016x}"',
        (T_P + "test_render_lines",),
    ),
    Plant(
        "pictures-render-file-unquoted",
        RD,
        "file={quote(picture.file or '')}",
        "file={picture.file}",
        (T_P + "test_render_lines",),
    ),
    Plant(
        "pictures-render-all-missing",
        RD,
        "        word = picture.status if picture.status != LOADED else MISSING",
        "        word = MISSING",
        (T_P + "test_a_refused_picture_is_not_loaded",),
    ),
    Plant(
        "pictures-render-empty-list-blank",
        RD,
        '        return "\\n"',
        '        return ""',
        (T_P + "test_render_lines",),
    ),
    Plant(
        "pictures-png-name-tilde-lost",
        JS,
        'UNSAFE = re.compile(r"[^a-z0-9_.~-]")',
        'UNSAFE = re.compile(r"[^a-z0-9_.-]")',
        (EXPORT_JSON, EXPORT),
    ),
    Plant(
        "pictures-png-names-collide",
        JS,
        "        while candidate in taken:",
        "        while False:",
        (T_P + "test_png_names_are_safe_and_unique",),
    ),
    Plant(
        "pictures-json-index-1",
        JS,
        "        red, green, blue = bitmap.palette[TRANSPARENT]",
        "        red, green, blue = bitmap.palette[1]",
        (EXPORT_JSON,),
    ),
    Plant(
        "pictures-json-file-is-label",
        JS,
        '"file": _relative(picture.path, install) if bitmap is not None else None,',
        '"file": picture.file,',
        (EXPORT_JSON,),
    ),
    Plant(
        "pictures-schema-status-any",
        JS,
        '            "status": {"enum": [LOADED, MISSING, NOT_LOADED]},',
        '            "status": {"type": "string"},',
        (REJECTS + "[status]",),
        regen_schema=True,
    ),
    Plant(
        "pictures-schema-channels-unbounded",
        JS,
        '**ints("red", "green", "blue", maximum=255)',
        '**ints("red", "green", "blue")',
        (REJECTS + "[index-0-red]",),
        regen_schema=True,
    ),
    Plant(
        "pictures-schema-picture-any",
        JS,
        r'"pattern": r"^[a-z0-9_.~-]+\.png$"',
        r'"pattern": r"^.+$"',
        (REJECTS + "[picture-name]",),
        regen_schema=True,
    ),
    Plant(
        "pictures-schema-name-any",
        JS,
        '            "name": {"type": "string", "pattern": r"^frontres\\\\"},',
        '            "name": {"type": "string"},',
        (REJECTS + "[game-name]",),
        regen_schema=True,
    ),
    Plant(
        "pictures-schema-transparent-any",
        JS,
        '            "transparent_index": {"const": TRANSPARENT},',
        '            "transparent_index": {"type": "integer"},',
        (REJECTS + "[transparent-index]",),
        regen_schema=True,
    ),
    Plant(
        "pictures-schema-file-stale",
        JS,
        '"title": "XvT/BoP menu pictures (jedimaster export)",',
        '"title": "XvT/BoP menu pictures",',
        (T_P + "test_schema_file_matches_model",),
    ),
    Plant(
        "pictures-export-no-pngs",
        CL,
        "                write_png(out / names[picture.name], picture.bitmap)\n",
        "                pass\n",
        (EXPORT, T_P + "test_written_png_reads_back"),
    ),
    Plant(
        "pictures-export-always-bop",
        CL,
        "balance_of_power=not args.no_balance_of_power",
        "balance_of_power=True",
        (EXPORT,),
    ),
    Plant(
        "pictures-export-write-failure-hidden",
        CL,
        '        logger.error("cannot export to %s: %s", out, exc)\n        return 1',
        '        logger.error("cannot export to %s: %s", out, exc)\n        return 0',
        (EXPORT,),
    ),
    Plant(
        "pictures-export-no-install-status-1",
        CL,
        '        logger.error("not an install: %s", args.install)\n        return 2',
        '        logger.error("not an install: %s", args.install)\n        return 1',
        (EXPORT,),
    ),
    Plant(
        "pictures-dump-missing-status-1",
        CL,
        '        logger.error("picture not found: %s", args.bmp)\n        return 2',
        '        logger.error("picture not found: %s", args.bmp)\n        return 1',
        (DUMP,),
    ),
    Plant(
        "pictures-dump-bad-status-0",
        CL,
        '        logger.error("picture %s not loaded: %s", args.bmp, picture.error)\n'
        "        return 1",
        '        logger.error("picture %s not loaded: %s", args.bmp, picture.error)\n'
        "        return 0",
        (DUMP,),
    ),
]
