"""The planted faults of the text outputs: view, renderers, JSON, schema, command line.

Purpose:
    Break one piece of the text sub-package's outputs per plant: where the
    view resolves a file and what it keeps of a refusal, each renderer's
    form, the JSON export's values, each schema rule the rejection test
    names, and the ``text`` command line's outputs and statuses.

Flow:
    ``plant_faults`` joins this table with the others, in a fixed order,
    and applies each plant alone: write ``new`` over ``old`` in ``path``,
    run the suite, restore, and check that every test in ``expect`` failed.

Invariants:
    - Ids are unique across all tables, all starting ``text-``.
    - Each ``old`` text occurs exactly once in its file.
    - A plant that changes the schema builder regenerates the schema files,
      so it fails the rejection test it names, not the drift test.

Call:
    ``from plants_text_output import TEXT_OUTPUT_PLANTS``
"""

from __future__ import annotations

import logging

from plants_common import CLI
from plants_common import Plant

logger = logging.getLogger(__name__)

GM = "jedimaster/text/game.py"
RD = "jedimaster/text/render.py"
JS = "jedimaster/text/to_json.py"
CL = "jedimaster/text/cli.py"
T_OUT = "tests/test_text_output.py::"
REJECTS = T_OUT + "test_schema_rejects_bad_data"
RENDER_STRINGS = T_OUT + "test_render_strings_tables_and_a_refusal"
RENDER_OTHERS = T_OUT + "test_render_specs_errors_joystick"
RENDER_CREDITS = T_OUT + "test_render_credits_pages"
REFUSAL = T_OUT + "test_export_keeps_bytes_and_records_the_refusal"
DUMP_STATUS = T_OUT + "test_text_dump_statuses"
EXPORT = T_OUT + "test_text_export"

VIEW_PLANTS: list[Plant] = [
    Plant(
        "text-view-base-only",
        GM,
        "    path = resolve(install, name, balance_of_power)",
        "    path = resolve(install, name, False)",
        (T_OUT + "test_view_reads_balance_of_power_first_in_any_case",),
    ),
    Plant(
        "text-view-label-plain",
        GM,
        "    label = sheet_file(install, name, path)",
        "    label = name",
        (T_OUT + "test_view_reads_balance_of_power_first_in_any_case",),
    ),
    Plant(
        "text-view-refusal-raises",
        GM,
        "    except (StringsFormatError, OSError) as exc:",
        "    except OSError as exc:",
        (T_OUT + "test_view_reads_balance_of_power_first_in_any_case", REFUSAL),
    ),
    Plant(
        "text-view-missing-raises",
        GM,
        "        return TextFile(kind, name, None, name.lower(), None, error)",
        "        raise error",
        (T_OUT + "test_a_missing_file_is_kept_as_an_error",),
    ),
]

RENDER_PLANTS: list[Plant] = [
    Plant(
        "text-render-goal-unnumbered",
        RD,
        'lines.append(f"entry {entry.index}.{entry.variant} {quote(entry.text)}")',
        'lines.append(f"entry {entry.index} {quote(entry.text)}")',
        (RENDER_STRINGS,),
    ),
    Plant(
        "text-render-gender-letter",
        RD,
        'f"entry {entry.index} gender={entry.gender} {quote(entry.text)}"',
        'f"entry {entry.index} gender={entry.gender + 1} {quote(entry.text)}"',
        (RENDER_STRINGS,),
    ),
    Plant(
        "text-render-absent-silent",
        RD,
        '            lines.append(f"table {table.name} absent")\n',
        "",
        (RENDER_STRINGS,),
    ),
    Plant(
        "text-render-refusal-printed",
        RD,
        "    if result is not None:\n",
        "    if True:\n",
        (RENDER_STRINGS,),
    ),
    Plant(
        "text-render-front-count",
        RD,
        '    lines = [f"count {len(front.entries)}"]',
        '    lines = [f"count {len(front.entries) - 1}"]',
        (T_OUT + "test_render_header_and_front",),
    ),
    Plant(
        "text-render-specs-uncut",
        RD,
        'f"{name}={quote(getattr(entry, name), size)}" for name, size in FIELDS',
        'f"{name}={quote(getattr(entry, name))}" for name, size in FIELDS',
        (RENDER_OTHERS,),
    ),
    Plant(
        "text-render-errors-count-first",
        RD,
        '    lines = [f"entry {i} {quote(text)}" for i, text in enumerate(errors.messages)]\n'
        '    lines.append(f"count {len(errors.messages)}")',
        '    lines = [f"count {len(errors.messages)}"]\n'
        '    lines += [f"entry {i} {quote(text)}" for i, text in enumerate(errors.messages)]',
        (RENDER_OTHERS,),
    ),
    Plant(
        "text-render-joystick-name-uncut",
        RD,
        "name={quote(action.name, NAME_ROOM)}",
        "name={quote(action.name)}",
        (RENDER_OTHERS,),
    ),
    Plant(
        "text-render-joystick-description-short",
        RD,
        "description={quote(action.description, DESCRIPTION_ROOM)}",
        "description={quote(action.description, 64)}",
        (RENDER_OTHERS,),
    ),
    Plant(
        "text-render-result-0",
        RD,
        "RESULT = 1\n",
        "RESULT = 0\n",
        (RENDER_OTHERS, RENDER_CREDITS),
    ),
    Plant(
        "text-render-unread-silent",
        RD,
        '        return " header=unread"',
        '        return ""',
        (RENDER_CREDITS,),
    ),
    Plant(
        "text-render-unread-as-zero",
        RD,
        'words.append(f" {shown}={UNREAD if value is None else value}")',
        'words.append(f" {shown}={value or 0}")',
        (RENDER_CREDITS,),
    ),
    Plant(
        "text-render-duration-fade-swapped",
        RD,
        '    ("duration", "duration"),\n    ("fade", "fade"),',
        '    ("duration", "fade"),\n    ("fade", "duration"),',
        (RENDER_CREDITS,),
    ),
    Plant(
        "text-render-color-short-hex",
        RD,
        "color={line.color:04x}",
        "color={line.color:x}",
        (RENDER_CREDITS,),
    ),
]

JSON_PLANTS: list[Plant] = [
    Plant(
        "text-json-game-stop-lost",
        JS,
        '        "game_stop": error.stops if refused else None,',
        '        "game_stop": None,',
        (REFUSAL,),
    ),
    Plant(
        "text-json-stop-utf8",
        JS,
        '"stop": latin1(error.stop) if refused and error.stop is not None else None,',
        '"stop": error.stop.decode("utf-8", "replace") if refused else None,',
        (REFUSAL,),
    ),
    Plant(
        "text-json-line-count-as-text",
        JS,
        '            "lines": len(table.entries),',
        '            "lines": str(len(table.entries)),',
        (T_OUT + "test_export_validates_against_schema[True]",),
    ),
    Plant(
        "text-schema-file-stale",
        JS,
        '"title": "XvT/BoP text files (jedimaster export)",',
        '"title": "XvT/BoP text files",',
        (T_OUT + "test_schema_file_matches_model",),
    ),
    Plant(
        "text-json-utf8",
        JS,
        '    return data.decode("latin-1")',
        '    return data.decode("utf-8", "replace")',
        (REFUSAL,),
    ),
    Plant(
        "text-json-refusal-line-lost",
        JS,
        '        "line": error.line if refused else None,',
        '        "line": None,',
        (REFUSAL,),
    ),
    Plant(
        "text-json-message-with-path",
        JS,
        '        "message": f"{file}: {error.reason}" if refused else str(error),',
        '        "message": str(error),',
        (REFUSAL,),
    ),
    Plant(
        "text-json-no-text-empty",
        JS,
        '        "no_text": latin1(NO_TEXT),',
        '        "no_text": "",',
        (REFUSAL,),
    ),
    Plant(
        "text-json-refused-empty-not-null",
        JS,
        "        data.update(dict.fromkeys(keys))",
        "        data.update(dict.fromkeys(keys, []))",
        (REFUSAL,),
    ),
    Plant(
        "text-json-header-read-full",
        JS,
        '            "header_read": len(page.header),',
        '            "header_read": len(HEADER_FIELDS),',
        (REFUSAL,),
    ),
    Plant(
        "text-json-header-one-field",
        JS,
        '            "header": {name: page.value(name) for name in HEADER_FIELDS},',
        '            "header": {name: page.value("text_x") for name in HEADER_FIELDS},',
        (REFUSAL,),
    ),
    Plant(
        "text-json-code-shifted",
        JS,
        '            "code": action.code,',
        '            "code": action.code + 1,',
        (REFUSAL,),
    ),
    Plant(
        "text-schema-entries-untyped",
        JS,
        '    texts = {"type": "array", "items": TEXT}',
        '    texts = {"type": "array", "items": {}}',
        (REJECTS + "[front-entry]",),
        regen_schema=True,
    ),
    Plant(
        "text-schema-code-unbounded",
        JS,
        '            **ints("code", maximum=255),',
        '            **ints("code"),',
        (REJECTS + "[joystick-code]",),
        regen_schema=True,
    ),
    Plant(
        "text-schema-buffer-unbounded",
        JS,
        '            **ints("buffer", maximum=1),',
        '            **ints("buffer"),',
        (REJECTS + "[credits-buffer]",),
        regen_schema=True,
    ),
    Plant(
        "text-schema-page-lines-any-count",
        JS,
        '            "lines": {**array("CreditsLine"), "minItems": 32, "maxItems": 32},',
        '            "lines": array("CreditsLine"),',
        (REJECTS + "[credits-lines]",),
        regen_schema=True,
    ),
    Plant(
        "text-schema-kind-any",
        JS,
        '            "kind": {"enum": [LINES, GOAL, MODELS, ESCAPED]},',
        '            "kind": TEXT,',
        (REJECTS + "[strings-kind]",),
        regen_schema=True,
    ),
    Plant(
        "text-schema-gender-unbounded",
        JS,
        '"gender": nullable({"type": "integer", "minimum": 0, "maximum": 2}),',
        '"gender": nullable({"type": "integer", "minimum": 0}),',
        (REJECTS + "[strings-gender]",),
        regen_schema=True,
    ),
    Plant(
        "text-schema-specs-any-count",
        JS,
        '"entries": nullable({**array("Spec"), "minItems": 93, "maxItems": 93}),',
        '"entries": nullable(array("Spec")),',
        (REJECTS + "[specs-count]",),
        regen_schema=True,
    ),
    Plant(
        "text-schema-objects-open",
        JS,
        '        "additionalProperties": False,',
        '        "additionalProperties": True,',
        (
            REJECTS + "[errors-extra]",
            "tests/test_fonts_output.py::test_schema_rejects_bad_data[font-extra]",
            "tests/test_pictures.py::test_schema_rejects_bad_data[picture-extra]",
        ),
        regen_schema=True,
    ),
    Plant(
        "text-schema-name-any",
        JS,
        '                "name": {"const": name},',
        '                "name": TEXT,',
        (REJECTS + "[front-name]",),
        regen_schema=True,
    ),
]

CLI_PLANTS: list[Plant] = [
    Plant(
        "text-dump-refusal-status-0",
        CL,
        "        sys.stdout.write(render_text(args.kind, None, name, str(path)))\n"
        "        return 1",
        "        sys.stdout.write(render_text(args.kind, None, name, str(path)))\n"
        "        return 0",
        (DUMP_STATUS,),
    ),
    Plant(
        "text-dump-refusal-silent",
        CL,
        "        sys.stdout.write(render_text(args.kind, None, name, str(path)))\n",
        "",
        (DUMP_STATUS,),
    ),
    Plant(
        "text-dump-missing-status-1",
        CL,
        '        logger.error("text file not found: %s", args.file)\n        return 2',
        '        logger.error("text file not found: %s", args.file)\n        return 1',
        (DUMP_STATUS,),
    ),
    Plant(
        "text-find-file-absolute-crash",
        CL,
        "    except ValueError:\n",
        "    except KeyError:\n",
        (DUMP_STATUS,),
    ),
    Plant(
        "text-find-file-no-game-path",
        CL,
        "        resolved = resolve_game_path(install, given) if install else None",
        "        resolved = None",
        (T_OUT + "test_text_dump_by_path_and_game_path",),
    ),
    Plant(
        "text-export-always-bop",
        CL,
        "balance_of_power=not args.no_balance_of_power",
        "balance_of_power=True",
        (EXPORT,),
    ),
    Plant(
        "text-export-write-failure-hidden",
        CL,
        '        logger.error("cannot export to %s: %s", out, exc)\n        return 1',
        '        logger.error("cannot export to %s: %s", out, exc)\n        return 0',
        (EXPORT,),
    ),
    Plant(
        "text-export-no-install-status-1",
        CL,
        '        logger.error("not an install: %s", args.install)\n        return 2',
        '        logger.error("not an install: %s", args.install)\n        return 1',
        (EXPORT,),
    ),
    Plant(
        "text-command-not-dispatched",
        CLI,
        "    if args.command in SUBCOMMANDS:",
        '    if args.command == "fonts":',
        (T_OUT + "test_text_dump_by_path_and_game_path", EXPORT),
    ),
]

TEXT_OUTPUT_PLANTS: list[Plant] = [
    *VIEW_PLANTS,
    *RENDER_PLANTS,
    *JSON_PLANTS,
    *CLI_PLANTS,
]
