"""Export a loaded pilot as JSON data, and describe that JSON with a schema.

Purpose:
    Give the page that will show a pilot the record the game holds after
    loading it, as plain JSON data with the game program's member names as
    keys, and publish the JSON Schema (draft 2020-12) the export always
    satisfies, built from the layout.

Flow:
    ``record_to_json`` walks a struct's members with a record's values;
    ``pilot_to_json`` wraps the full record with the load's facts;
    ``pilot_schema`` builds one definition per struct of the layout.

Invariants:
    - Numbers stay numbers; arrays stay nested lists, the last index
      fastest; a ``char`` field is its text up to its first byte 0, one
      character per byte (Latin-1); a ``u8`` field is its bytes as
      lowercase hex, two digits a byte.
    - Every element of an array of structs is exported, all-zero ones
      included.

Call:
    ``json.dumps(pilot_to_json(load, balance_of_power=True))``
"""

from __future__ import annotations

import logging
from typing import Any

from ..text.to_json import closed_object
from ..text.to_json import latin1
from .layout import CHAR
from .layout import FULL_RECORD
from .layout import I32
from .layout import is_struct
from .layout import Member
from .layout import struct
from .layout import STRUCTS
from .layout import U8
from .layout import U32
from .load import PilotLoad

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/pilot.schema.json"
JSON_FORMAT = "jedimaster.pilot"
JSON_FORMAT_VERSION = 1
RANGES = {I32: (-(2**31), 2**31 - 1), U32: (0, 2**32 - 1)}
"""The values a 32-bit member can hold."""


def _value(member: Member, value: Any) -> Any:
    if isinstance(value, list):
        return [_value(member, item) for item in value]
    if member.type == CHAR:
        return latin1(value.text)
    if member.type == U8:
        return value.hex() if isinstance(value, bytes) else f"{value:02x}"
    if is_struct(member.type):
        return record_to_json(member.type, value)
    return value


def record_to_json(name: str, record: dict[str, Any]) -> dict[str, Any]:
    """Return a record of struct ``name`` as JSON-ready data.

    A dict of every member by name, in layout order. Raises ``KeyError``
    when ``record`` lacks a member. Does not check the values' ranges.
    """
    return {m.name: _value(m, record[m.name]) for m in struct(name).members}


def pilot_to_json(load: PilotLoad, balance_of_power: bool) -> dict[str, Any]:
    """Return a ``PilotLoad`` as JSON-ready data: dicts, lists and scalars.

    Always returns ``format``, ``format_version``, ``file`` (the ``.plt``
    name), ``balance_of_power``, ``files`` (which were there), ``result``
    and ``record`` (the full record). Does not include the load's log.
    """
    data = {
        "format": JSON_FORMAT,
        "format_version": JSON_FORMAT_VERSION,
        "file": load.name,
        "balance_of_power": balance_of_power,
        "files": {"pl2": load.pl2, "plt": load.plt},
        "result": load.result,
        "record": record_to_json(FULL_RECORD, load.record),
    }
    logger.debug("exported %s: result %d", load.name, load.result)
    return data


def _leaf(member: Member) -> dict[str, Any]:
    run = member.dims[-1] if member.dims else 1
    if member.type == CHAR:
        return {"type": "string", "maxLength": run}
    if member.type == U8:
        return {"type": "string", "pattern": f"^([0-9a-f]{{2}}){{{run}}}$"}
    if is_struct(member.type):
        return {"$ref": f"#/$defs/{member.type}"}
    low, high = RANGES[member.type]
    return {"type": "integer", "minimum": low, "maximum": high}


def member_schema(member: Member) -> dict[str, Any]:
    """Return the schema of one member's exported value.

    Arrays nest one ``array`` schema per dimension with that many items;
    a ``u8`` or ``char`` member's last dimension is its string. Raises
    ``KeyError`` for an unknown type. Does not describe the member.
    """
    schema = _leaf(member)
    dims = member.dims[:-1] if member.type in (U8, CHAR) else member.dims
    for size in reversed(dims):
        schema = {"type": "array", "minItems": size, "maxItems": size, "items": schema}
    return schema


def pilot_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``pilot_to_json`` output.

    One closed definition per struct of the layout. Always returns the
    same dict. Does not encode cross-field rules (for example, that a
    failed load's record is all 0).
    """
    defs = {
        item.name: closed_object(
            f"{item.name}: {item.size} bytes in the game's layout.",
            {m.name: member_schema(m) for m in item.members},
        )
        for item in STRUCTS
    }
    top = closed_object(
        "A pilot as the game holds it after loading (jedimaster export).",
        {
            "format": {"const": JSON_FORMAT},
            "format_version": {"const": JSON_FORMAT_VERSION},
            "file": {"type": "string"},
            "balance_of_power": {"type": "boolean"},
            "files": closed_object(
                "Which of the pilot's files were there.",
                {"pl2": {"type": "boolean"}, "plt": {"type": "boolean"}},
            ),
            "result": {"enum": [0, 1]},
            "record": {"$ref": f"#/$defs/{FULL_RECORD}"},
        },
    )
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP pilot (jedimaster export)",
        **top,
        "$defs": defs,
    }
