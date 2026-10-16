"""Check a document against a schema of the launch package.

Purpose:
    The launcher keeps to the standard library, so the command checks the
    documents it writes and reads with this small checker, which understands
    exactly the keywords the two launch schemas use.

Flow:
    ``errors`` walks a document along its schema and returns one plain
    sentence per problem; ``valid`` is true when there are none.

Invariants:
    - Keywords understood: ``type`` (object, array, string, integer,
      boolean), ``const``, ``enum``, ``oneOf``, ``required``, ``properties``,
      ``additionalProperties`` false, ``items`` (one schema for every
      element), ``maxItems``, ``minimum``, ``maximum``, ``minLength``,
      ``maxLength``, ``pattern``.
    - A keyword outside that list raises ValueError: a schema this checker
      cannot read is never passed silently.

Call:
    ``errors(document, launch_schema())`` -> ``[]`` when the document fits
"""

from __future__ import annotations

import logging
import re
from typing import Any

logger = logging.getLogger(__name__)

UNDERSTOOD = {
    "$schema", "$id", "title", "description", "type", "const", "enum", "oneOf",
    "required", "properties", "additionalProperties", "minimum", "maximum",
    "minLength", "maxLength", "pattern", "items", "maxItems",
}  # fmt: skip
TYPES = {
    "object": dict,
    "array": list,
    "string": str,
    "integer": int,
    "boolean": bool,
}


def _is_type(value: Any, name: str) -> bool:
    """Say whether value has the JSON type name; a bool is no integer."""
    if name == "integer" and isinstance(value, bool):
        return False
    return isinstance(value, TYPES[name])


def errors(
    document: Any, schema: dict[str, Any], where: str = "the document"
) -> list[str]:
    """Return one sentence for each way document breaks schema; empty when it fits.

    Raises ValueError for a schema keyword this checker does not know.
    """
    unknown = set(schema) - UNDERSTOOD
    if unknown:
        raise ValueError(f"schema keyword not understood: {sorted(unknown)}")
    found: list[str] = []
    if "const" in schema and document != schema["const"]:
        found.append(f"{where} must be {schema['const']!r}")
    if "enum" in schema and document not in schema["enum"]:
        found.append(f"{where} must be one of {schema['enum']}")
    if "oneOf" in schema:
        fitting = [s for s in schema["oneOf"] if not errors(document, s, where)]
        if len(fitting) != 1:
            found.append(f"{where} fits none of the allowed forms")
    if "type" in schema:
        if not _is_type(document, schema["type"]):
            return found + [f"{where} must be a {schema['type']}"]
    if isinstance(document, bool):
        return found
    if isinstance(document, int):
        if "minimum" in schema and document < schema["minimum"]:
            found.append(f"{where} must be at least {schema['minimum']}")
        if "maximum" in schema and document > schema["maximum"]:
            found.append(f"{where} must be at most {schema['maximum']}")
    if isinstance(document, str):
        if "minLength" in schema and len(document) < schema["minLength"]:
            found.append(f"{where} must hold at least {schema['minLength']} characters")
        if "maxLength" in schema and len(document) > schema["maxLength"]:
            found.append(f"{where} must hold at most {schema['maxLength']} characters")
        if "pattern" in schema and not re.search(schema["pattern"], document):
            found.append(f"{where} does not match the pattern {schema['pattern']}")
    if isinstance(document, dict):
        found += _members(document, schema, where)
    if isinstance(document, list):
        if "maxItems" in schema and len(document) > schema["maxItems"]:
            found.append(f"{where} must hold at most {schema['maxItems']} items")
        if "items" in schema:
            for index, item in enumerate(document):
                found += errors(item, schema["items"], f"{where}[{index}]")
    return found


def _members(document: dict[str, Any], schema: dict[str, Any], where: str) -> list[str]:
    """Return the problems of an object's members."""
    found = [
        f"{where} lacks {name!r}"
        for name in schema.get("required", [])
        if name not in document
    ]
    properties = schema.get("properties", {})
    for name, value in document.items():
        if name in properties:
            found += errors(value, properties[name], f"{where}.{name}")
        elif schema.get("additionalProperties") is False:
            found.append(f"{where} holds {name!r}, which is not allowed")
    return found


def valid(document: Any, schema: dict[str, Any]) -> bool:
    """Say whether document fits schema."""
    return not errors(document, schema)
