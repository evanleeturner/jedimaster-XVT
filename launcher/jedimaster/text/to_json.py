"""Export the text view as plain JSON data, and describe it as a schema.

Purpose:
    Give the page that will show the game's words every table and list of
    the six text files, each value with every byte kept as one character
    (Latin-1); a file the game refuses (the base game's ``strings.txt``)
    is recorded as an error, never a crash; and publish the JSON Schema
    (draft 2020-12) the export always satisfies.

Flow:
    ``text_to_json`` builds one object per kind with ``_PAYLOADS``;
    ``text_schema`` builds the schema by hand from ``closed_object``,
    ``ints`` and ``nullable``, which the fonts and pictures exports share.

Invariants:
    - The output holds only dicts, lists, strings, integers, booleans and
      null, so ``json.dumps`` always succeeds; every text is its bytes
      decoded as Latin-1, so encoding it back gives the bytes.
    - Every kind object has ``name``, ``file`` and ``error``, and its
      payload is null exactly when ``error`` is not.
    - Every object is closed (``additionalProperties`` false) and lists all
      its fields as required.

Call:
    ``json.dumps(text_to_json(text_view(install)), ensure_ascii=False)``
"""

from __future__ import annotations

import logging
from typing import Any

from .credits import CreditsFile
from .credits import HEADER_FIELDS
from .credits import WHITE
from .game import TEXT_FILES
from .game import TextFile
from .game import TextView
from .joystick import JoystickFile
from .menus import ErrorMessages
from .menus import FrontText
from .menus import NO_TEXT
from .specs import FIELDS
from .specs import SpecsFile
from .strings import StringsFile
from .strings import StringsFormatError
from .tables import ESCAPED
from .tables import GOAL
from .tables import LINES
from .tables import MODELS

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/text.schema.json"
JSON_FORMAT = "jedimaster.text"
JSON_FORMAT_VERSION = 1


def latin1(data: bytes) -> str:
    """Return ``data`` as text, one character per byte (Latin-1).

    Always succeeds; ``.encode("latin-1")`` gives the bytes back. Does
    not stop at a byte 0.
    """
    return data.decode("latin-1")


def _error(error: Exception | None, file: str) -> dict[str, Any] | None:
    if error is None:
        return None
    refused = isinstance(error, StringsFormatError)
    return {
        "message": f"{file}: {error.reason}" if refused else str(error),
        "table": error.table if refused else None,
        "line": error.line if refused else None,
        "file_line": error.file_line if refused else None,
        "game_stop": error.stops if refused else None,
        "stop": latin1(error.stop) if refused and error.stop is not None else None,
    }


def _strings(strings: StringsFile) -> dict[str, Any]:
    tables = [
        {
            "name": table.name,
            "kind": table.kind,
            "present": table.present,
            "lines": len(table.entries),
            "entries": [
                {
                    "index": entry.index,
                    "variant": entry.variant,
                    "gender": entry.gender,
                    "text": latin1(entry.text),
                    "line": entry.line,
                }
                for entry in table.entries
            ],
        }
        for table in strings.tables
    ]
    return {"tables": tables}


def _front(front: FrontText) -> dict[str, Any]:
    return {
        "entries": [latin1(text) for text in front.entries],
        "no_text": latin1(NO_TEXT),
    }


def _specs(specs: SpecsFile) -> dict[str, Any]:
    entries = [
        {name: latin1(getattr(entry, name)) for name, _ in FIELDS}
        for entry in specs.entries
    ]
    fields = [{"name": name, "size": size} for name, size in FIELDS]
    return {
        "fields": fields,
        "entries": entries,
        "complete": specs.complete,
        "cut": specs.cut,
    }


def _errors(errors: ErrorMessages) -> dict[str, Any]:
    return {"messages": [latin1(text) for text in errors.messages]}


def _joystick(joystick: JoystickFile) -> dict[str, Any]:
    actions = [
        {
            "code": action.code,
            "name": latin1(action.name),
            "description": latin1(action.description),
            "line": action.line,
        }
        for action in joystick.actions
    ]
    return {"actions": actions}


def _credits(credits: CreditsFile) -> dict[str, Any]:
    pages = [
        {
            "page": page.index,
            "header": {name: page.value(name) for name in HEADER_FIELDS},
            "header_read": len(page.header),
            "buffer": page.buffer,
            "more": page.more,
            "used": page.used,
            "lines": [
                {"text": latin1(line.text), "color": line.color} for line in page.lines
            ],
        }
        for page in credits.pages
    ]
    return {"start_color": WHITE, "pages": pages}


_PAYLOADS: dict[str, tuple[Any, tuple[str, ...]]] = {
    "strings": (_strings, ("tables",)),
    "front": (_front, ("entries", "no_text")),
    "specs": (_specs, ("fields", "entries", "complete", "cut")),
    "errors": (_errors, ("messages",)),
    "joystick": (_joystick, ("actions",)),
    "credits": (_credits, ("start_color", "pages")),
}
"""Each kind's payload builder and the keys it fills (null on an error)."""


def _kind(text_file: TextFile) -> dict[str, Any]:
    build, keys = _PAYLOADS[text_file.kind]
    data: dict[str, Any] = {
        "name": text_file.name,
        "file": text_file.file,
        "error": _error(text_file.error, text_file.file),
    }
    if text_file.result is None:
        data.update(dict.fromkeys(keys))
    else:
        data.update(build(text_file.result))
    return data


def text_to_json(view: TextView) -> dict[str, Any]:
    """Return the view as JSON-ready data: dicts, lists and scalars.

    Always returns a dict with ``format``, ``format_version``,
    ``balance_of_power`` and one object per kind of ``TEXT_FILES``; a file
    without a result has its ``error`` and null payload keys. Does not
    check the values against the game's limits.
    """
    data: dict[str, Any] = {
        "format": JSON_FORMAT,
        "format_version": JSON_FORMAT_VERSION,
        "balance_of_power": view.balance_of_power,
    }
    for kind, _ in TEXT_FILES:
        data[kind] = _kind(view.files[kind])
    logger.debug("exported %d text files", len(TEXT_FILES))
    return data


# ---- schema -------------------------------------------------------------


def closed_object(description: str, props: dict[str, Any]) -> dict[str, Any]:
    """Return a schema object requiring exactly ``props``, nothing else.

    Always returns a new dict. Does not check the property schemas.
    """
    return {
        "type": "object",
        "description": description,
        "properties": props,
        "required": list(props),
        "additionalProperties": False,
    }


def ints(*names: str, minimum: int | None = 0, maximum: int | None = None) -> dict:
    """Return ``{name: integer schema}`` for each name, bounds as given.

    Always returns a new dict; each schema is its own copy. Does not
    check that ``minimum`` is below ``maximum``.
    """
    kind: dict[str, Any] = {"type": "integer"}
    if minimum is not None:
        kind["minimum"] = minimum
    if maximum is not None:
        kind["maximum"] = maximum
    return {name: dict(kind) for name in names}


def nullable(schema: dict[str, Any]) -> dict[str, Any]:
    """Return a schema that accepts ``schema`` or null.

    Always returns a new dict. Does not copy ``schema``.
    """
    return {"anyOf": [schema, {"type": "null"}]}


def array(ref: str) -> dict[str, Any]:
    """Return an array schema whose items are ``#/$defs/<ref>``.

    Always returns a new dict. Does not check that the definition exists.
    """
    return {"type": "array", "items": {"$ref": f"#/$defs/{ref}"}}


TEXT = {"type": "string"}


def _defs() -> dict[str, Any]:
    entry = closed_object(
        "One entry: number (a goal table's row), variant, gender, text, line.",
        {
            **ints("index"),
            "variant": nullable({"type": "integer", "minimum": 0}),
            "gender": nullable({"type": "integer", "minimum": 0, "maximum": 2}),
            "text": TEXT,
            **ints("line", minimum=1),
        },
    )
    table = closed_object(
        "One strings.txt table, in the game's order; absent ones are empty.",
        {
            "name": TEXT,
            "kind": {"enum": [LINES, GOAL, MODELS, ESCAPED]},
            "present": {"type": "boolean"},
            **ints("lines"),
            "entries": array("Entry"),
        },
    )
    error = closed_object(
        "Why a file has no result; for a refused strings.txt, where, whether "
        "the game stops, and the message it shows (a line of the file).",
        {
            "message": TEXT,
            "table": nullable(TEXT),
            "line": nullable({"type": "integer", "minimum": 1}),
            "file_line": nullable({"type": "integer", "minimum": 1}),
            "game_stop": nullable({"type": "boolean"}),
            "stop": nullable(TEXT),
        },
    )
    spec = closed_object(
        "One craft entry: five fields, each as the game's field holds it.",
        {name: TEXT for name, _ in FIELDS},
    )
    field = closed_object(
        "A craft entry's field and its size in bytes.",
        {"name": TEXT, **ints("size", minimum=1)},
    )
    action = closed_object(
        "One joystick action: code byte, name, description, file line.",
        {
            **ints("code", maximum=255),
            "name": TEXT,
            "description": TEXT,
            **ints("line", minimum=1),
        },
    )
    line = closed_object(
        "One page buffer line: its text and its 16-bit 565 color.",
        {"text": TEXT, **ints("color", maximum=0xFFFF)},
    )
    numbers = {
        name: nullable({"type": "integer", "minimum": 0, "maximum": 0xFFFFFFFF})
        for name in HEADER_FIELDS
    }
    page = closed_object(
        "One credits page: header (null where not read), buffer, lines.",
        {
            **ints("page"),
            "header": closed_object("The page header's ten numbers.", numbers),
            **ints("header_read", maximum=len(HEADER_FIELDS)),
            **ints("buffer", maximum=1),
            "more": {"type": "boolean"},
            **ints("used", maximum=32),
            "lines": {**array("CreditsLine"), "minItems": 32, "maxItems": 32},
        },
    )
    return {
        "Action": action,
        "CreditsLine": line,
        "Entry": entry,
        "Error": error,
        "Field": field,
        "Page": page,
        "Spec": spec,
        "Table": table,
    }


def _kinds() -> dict[str, Any]:
    texts = {"type": "array", "items": TEXT}
    payloads: dict[str, dict[str, Any]] = {
        "strings": {"tables": nullable(array("Table"))},
        "front": {"entries": nullable(texts), "no_text": nullable(TEXT)},
        "specs": {
            "fields": nullable(array("Field")),
            "entries": nullable({**array("Spec"), "minItems": 93, "maxItems": 93}),
            "complete": nullable({"type": "integer", "minimum": 0, "maximum": 93}),
            "cut": nullable({"type": "integer", "minimum": 0, "maximum": 92}),
        },
        "errors": {"messages": nullable(texts)},
        "joystick": {"actions": nullable(array("Action"))},
        "credits": {
            "start_color": nullable({"const": WHITE}),
            "pages": nullable(array("Page")),
        },
    }
    return {
        kind: closed_object(
            f"The {name} file: game name, file, error, and what was read.",
            {
                "name": {"const": name},
                "file": TEXT,
                "error": nullable({"$ref": "#/$defs/Error"}),
                **payloads[kind],
            },
        )
        for kind, name in TEXT_FILES
    }


def text_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``text_to_json`` output.

    Always returns the same dict. Does not encode cross-field rules (for
    example, that a payload is null exactly when ``error`` is not).
    """
    top = closed_object(
        "The game's text files as the game reads them (jedimaster export).",
        {
            "format": {"const": JSON_FORMAT},
            "format_version": {"const": JSON_FORMAT_VERSION},
            "balance_of_power": {"type": "boolean"},
            **_kinds(),
        },
    )
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP text files (jedimaster export)",
        **top,
        "$defs": _defs(),
    }
