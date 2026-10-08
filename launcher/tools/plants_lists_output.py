"""The planted faults of the lists' rendering, install side, JSON and commands.

Purpose:
    Break one piece of the lists' renderer, ``files`` module, JSON export
    and schema, the install module's Balance of Power switch, or the
    ``lists`` commands per plant.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Each plant's text is exactly as it was when it lived in
      ``plant_faults.py``; ids are unique across all tables.
    - Each ``old`` text occurs exactly once in its file (the lists package, the install module, the command line).

Call:
    ``from plants_lists_output import LISTS_OUTPUT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import CLI
from plants_common import I
from plants_common import Plant
from plants_lists_read import LF
from plants_lists_read import LJ
from plants_lists_read import LV
from plants_lists_read import TL_CLI
from plants_lists_read import TL_INS
from plants_lists_read import TL_JSON
from plants_lists_read import TL_REN

logger = logging.getLogger(__name__)

LISTS_OUTPUT_PLANTS: list[Plant] = [
    # rendering
    Plant(
        "lists-render-available-flag",
        LV,
        "unavailable={int(not e.available)}",
        "unavailable={int(e.available)}",
        (TL_REN + "test_menu_sheet",),
    ),
    Plant(
        "lists-render-menu-line-always",
        LV,
        "    if menu is not None:\n",
        "    if True:\n",
        (TL_REN + "test_sequence_sheet",),
    ),
    Plant(
        "lists-render-image-height",
        LV,
        'f"height={image.height} compressed',
        'f"height={image.width} compressed',
        (TL_REN + "test_images_sheet",),
    ),
    Plant(
        "lists-render-17-slots",
        LV,
        '" ".join(str(s) for s in ships.type_to_ship)',
        '" ".join(str(s) for s in ships.type_to_ship[:17])',
        (TL_REN + "test_ships_sheet",),
    ),
    Plant(
        "lists-render-sound-swapped",
        LV,
        'f"sound {i} name={c_quote(sound.name)} file={c_quote(sound.wav)}"',
        'f"sound {i} name={c_quote(sound.wav)} file={c_quote(sound.name)}"',
        (TL_REN + "test_sounds_sheet",),
    ),
    Plant(
        "lists-render-phase-is-campaign",
        LV,
        "after_debriefing={c.after_debriefing}",
        "after_debriefing={c.campaign}",
        (TL_REN + "test_cutscenes_sheet",),
    ),
    Plant(
        "lists-render-rows-swapped",
        LV,
        '("multiplayer", a.multiplayer),',
        '("multiplayer", a.singleplayer),',
        (TL_REN + "test_awards_sheet",),
    ),
    Plant(
        "lists-raw-bool-words",
        LV,
        "        return str(int(value))\n",
        "        return str(value)\n",
        (TL_REN + "test_raw_dump_prints_values_as_written",),
    ),
    # install side
    Plant(
        "lists-files-no-bop",
        LF,
        "        roots.append(bop)\n",
        "        pass\n",
        (TL_INS + "test_list_files_by_kind",),
    ),
    Plant(
        "lists-files-two-views",
        LF,
        "for v in VIEWS} for t in MISSION_TYPES}",
        "for v in VIEWS[:2]} for t in MISSION_TYPES}",
        (TL_INS + "test_menu_paths_table",),
    ),
    Plant(
        "lists-kind-mission-only",
        LF,
        "if name in MENU_FILES:",
        'if name == "mission.lst":',
        (TL_INS + "test_kind_of[x/TRAIN/REBEL.LST-menu]",),
    ),
    Plant(
        "lists-kind-fixed-full-path",
        LF,
        'if name in (p.rsplit("\\\\", 1)[-1] for p in game_paths):',
        "if name == game_paths[0]:",
        tuple(
            TL_INS + f"test_kind_of[{p}]"
            for p in (
                "x/frontres/MAPICONS.LST-images",
                "frntspec.lst-ships",
                "Sfx/sfx.lst-sounds",
                "cutscene.lst-cutscenes",
                "campawds.lst-awards",
            )
        ),
    ),
    Plant(
        "lists-kind-battle-only",
        LF,
        'for t in ("tournament", "battle", "campaign")',
        'for t in ("battle",)',
        (
            TL_INS + "test_kind_of[x/Tourn/t9.lst-sequence]",
            TL_INS + "test_kind_of[x/CAMPAIGN/campgn3.lst-sequence]",
        ),
    ),
    Plant(
        "lists-kind-training-sequences",
        LF,
        'for t in ("tournament", "battle", "campaign")',
        'for t in ("tournament", "battle", "campaign", "training")',
        (TL_INS + "test_kind_of[x/Train/other.lst-None]",),
    ),
    Plant(
        "lists-kind-any-suffix",
        LF,
        "if path.suffix.casefold() == LIST_SUFFIX and path.parent",
        "if path.parent",
        (TL_INS + "test_kind_of[x/Battle/readme.txt-None]",),
    ),
    Plant(
        "lists-resolve-always-bop",
        I,
        "bop = _child(install, BALANCE_OF_POWER) if balance_of_power else None",
        "bop = _child(install, BALANCE_OF_POWER)",
        (TL_INS + "test_resolve_with_and_without_balance_of_power",),
    ),
    Plant(
        "lists-sheet-file-no-bop",
        LF,
        'return f"{BALANCE_OF_POWER}/{label}"',
        "return label",
        (TL_INS + "test_resolve_with_and_without_balance_of_power",),
    ),
    Plant(
        "lists-bmp-signed-height",
        LF,
        "return width, abs(height)",
        "return width, height",
        (TL_INS + "test_bmp_header_sizes",),
    ),
    Plant(
        "lists-bmp-core-as-info",
        LF,
        "if info == 12:",
        "if info == 40:",
        (TL_INS + "test_bmp_header_sizes",),
    ),
    Plant(
        "lists-bmp-any-mark",
        LF,
        'if len(head) < BMP_HEADER or head[:2] != b"BM":',
        "if len(head) < BMP_HEADER:",
        (TL_INS + "test_bmp_header_sizes",),
    ),
    Plant(
        "lists-bitmaps-swapped",
        LF,
        "bitmaps[word] = Bitmap(width, height)",
        "bitmaps[word] = Bitmap(height, width)",
        (TL_INS + "test_install_bitmaps_skip_missing_and_unreadable",),
    ),
    Plant(
        "lists-bitmaps-bad-header-raises",
        LF,
        "except (ListFormatError, OSError) as exc:",
        "except OSError as exc:",
        (TL_INS + "test_install_bitmaps_skip_missing_and_unreadable",),
    ),
    # JSON and the schema
    Plant(
        "lists-schema-no-null",
        LJ,
        'return {"anyOf": [_type_schema(inner, defs), {"type": "null"}]}',
        "return _type_schema(inner, defs)",
        (TL_JSON + "test_export_validates_against_schema[menu]",),
        regen_schema=True,
    ),
    Plant(
        "lists-schema-bool-integer",
        LJ,
        '    if hint is bool:\n        return {"type": "boolean"}',
        '    if hint is bool:\n        return {"type": "integer"}',
        tuple(
            TL_JSON + f"test_export_validates_against_schema[{k}]"
            for k in (
                "awards",
                "cutscenes",
                "images",
                "menu",
                "sequence",
                "ships",
                "sounds",
            )
        ),
        regen_schema=True,
    ),
    Plant(
        "lists-schema-bool-any",
        LJ,
        '    if hint is bool:\n        return {"type": "boolean"}',
        "    if hint is bool:\n        return {}",
        (TL_JSON + "test_schema_rejects_bad_data[menu-path2-yes]",),
        regen_schema=True,
    ),
    Plant(
        "lists-schema-int-any",
        LJ,
        '    if hint is int:\n        return {"type": "integer"}',
        "    if hint is int:\n        return {}",
        (
            TL_JSON + "test_schema_rejects_bad_data[menu-path0-1]",
            TL_JSON + "test_schema_rejects_bad_data[sequence-path4-None]",
        ),
        regen_schema=True,
    ),
    Plant(
        "lists-schema-kind-open",
        LJ,
        'props[f.name] = {"const": f.metadata["const"]}',
        'props[f.name] = {"type": "string"}',
        (TL_JSON + "test_schema_rejects_bad_data[menu-path1-entry]",),
        regen_schema=True,
    ),
    Plant(
        "lists-schema-extra-fields",
        LJ,
        '"additionalProperties": False,',
        '"additionalProperties": True,',
        (TL_JSON + "test_schema_rejects_bad_data[images-path3-1]",),
        regen_schema=True,
    ),
    Plant(
        "lists-schema-array-untyped",
        LJ,
        'return {"type": "array", "items": _type_schema(inner, defs)}',
        'return {"items": _type_schema(inner, defs)}',
        (TL_JSON + "test_schema_rejects_bad_data[awards-path5-n0]",),
        regen_schema=True,
    ),
    Plant(
        "lists-schema-any-version",
        LJ,
        '"format_version": {"const": JSON_FORMAT_VERSION},',
        '"format_version": {"type": "integer"},',
        (TL_JSON + "test_schema_rejects_bad_data[ships-path6-2]",),
        regen_schema=True,
    ),
    Plant(
        "lists-schema-drift",
        LJ,
        '"title": "XvT/BoP text list (jedimaster export)",',
        '"title": "XvT/BoP text list",',
        (TL_JSON + "test_schema_file_matches_model",),
    ),
    Plant(
        "lists-json-takes-anything",
        LJ,
        "if not isinstance(result, LIST_CLASSES):",
        "if False:",
        (TL_JSON + "test_export_refuses_other_objects",),
    ),
    # command line
    Plant(
        "lists-cli-dump-upper",
        CLI,
        "sys.stdout.write(render_raw(result))",
        "sys.stdout.write(render_raw(result).upper())",
        (TL_CLI + "test_lists_dump_by_path",),
    ),
    Plant(
        "lists-cli-kind-ignored",
        CLI,
        "kind = args.kind or kind_of(path)",
        "kind = kind_of(path)",
        (TL_CLI + "test_lists_dump_by_game_path_and_kind",),
    ),
    Plant(
        "lists-cli-export-flat",
        CLI,
        'target = out / path.relative_to(install).with_suffix(".json")',
        'target = out / path.with_suffix(".json").name',
        (TL_CLI + "test_lists_export_writes_one_json_per_list",),
    ),
    Plant(
        "lists-cli-export-no-install-check",
        CLI,
        "def _lists_export(args: argparse.Namespace) -> int:\n"
        "    install = find_install(args.install)\n    if install is None:",
        "def _lists_export(args: argparse.Namespace) -> int:\n"
        "    install = find_install(args.install)\n    if False:",
        (TL_CLI + "test_lists_export_needs_an_install",),
    ),
]
