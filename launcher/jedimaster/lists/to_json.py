"""Export a reader's result as plain JSON data, and describe it as a schema.

Purpose:
    Give other programs every value a list file holds, raw, with its line,
    and publish the JSON Schema (draft 2020-12) the export always satisfies.

Flow:
    ``list_to_json`` walks the model's dataclasses field by field.
    ``lists_schema`` walks the same dataclasses' type hints: ``int``,
    ``str``, ``bool``, ``X | None``, ``list[X]``, a union of dataclasses
    (``oneOf``), nested dataclasses (``$ref``), and a field's ``const``
    metadata (the ``kind`` tags).

Invariants:
    - The output holds only dicts, lists, strings, integers, booleans and
      null, so ``json.dumps`` always succeeds.
    - Export and schema read the same dataclasses, so a field added to the
      model appears in both.
    - Every object is closed (``additionalProperties`` false) and lists all
      its fields as required.

Call:
    ``json.dumps(list_to_json(read_menu(path)), ensure_ascii=False)``
"""

from __future__ import annotations

import dataclasses
import logging
import types
import typing
from typing import Any

from .model import LIST_CLASSES
from .model import ListFile

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/lists.schema.json"
JSON_FORMAT = "jedimaster.lists"
JSON_FORMAT_VERSION = 1


def _walk(value: Any) -> Any:
    if dataclasses.is_dataclass(value) and not isinstance(value, type):
        return {
            f.name: _walk(getattr(value, f.name)) for f in dataclasses.fields(value)
        }
    if isinstance(value, list):
        return [_walk(v) for v in value]
    return value


def list_to_json(result: ListFile) -> dict[str, Any]:
    """Return a reader's result as JSON-ready data: dicts, lists and scalars.

    Always returns a dict with ``format``, ``format_version`` and one key
    per field of the result's dataclass (``kind`` among them). Does not
    derive the game's view and does not include the file's bytes.
    """
    if not isinstance(result, LIST_CLASSES):
        raise TypeError(f"not a list reader's result: {type(result).__name__}")
    data: dict[str, Any] = {
        "format": JSON_FORMAT,
        "format_version": JSON_FORMAT_VERSION,
    }
    data.update(_walk(result))
    logger.debug("exported a %s list", result.kind)
    return data


# ---- schema -------------------------------------------------------------


def _type_schema(hint: Any, defs: dict[str, Any]) -> dict[str, Any]:
    if hint is bool:
        return {"type": "boolean"}
    if hint is int:
        return {"type": "integer"}
    if hint is str:
        return {"type": "string"}
    if dataclasses.is_dataclass(hint):
        _define(hint, defs)
        return {"$ref": f"#/$defs/{hint.__name__}"}
    if typing.get_origin(hint) is list:
        (inner,) = typing.get_args(hint)
        return {"type": "array", "items": _type_schema(inner, defs)}
    if isinstance(hint, types.UnionType):
        args = typing.get_args(hint)
        if type(None) in args:
            (inner,) = [a for a in args if a is not type(None)]
            return {"anyOf": [_type_schema(inner, defs), {"type": "null"}]}
        return {"oneOf": [_type_schema(a, defs) for a in args]}
    raise TypeError(f"no schema for type {hint!r}")


def _define(cls: type, defs: dict[str, Any]) -> None:
    if cls.__name__ in defs:
        return
    defs[cls.__name__] = {}  # placeholder against recursion
    hints = typing.get_type_hints(cls)
    props: dict[str, Any] = {}
    for f in dataclasses.fields(cls):
        if "const" in f.metadata:
            props[f.name] = {"const": f.metadata["const"]}
        else:
            props[f.name] = _type_schema(hints[f.name], defs)
    doc = (cls.__doc__ or "").strip().splitlines()[0]
    defs[cls.__name__] = {
        "type": "object",
        "description": doc,
        "properties": props,
        "required": list(props),
        "additionalProperties": False,
    }


def lists_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``list_to_json`` output.

    Always returns the same dict for the same model: one of the seven list
    kinds, told apart by their ``kind`` constant, each with ``format`` and
    ``format_version``. Does not encode cross-field rules (for example,
    that ``count`` matches the entries read, or line numbers rising).
    """
    defs: dict[str, Any] = {}
    for cls in LIST_CLASSES:
        _define(cls, defs)
        top = defs[cls.__name__]
        top["properties"] = {
            "format": {"const": JSON_FORMAT},
            "format_version": {"const": JSON_FORMAT_VERSION},
            **top["properties"],
        }
        top["required"] = ["format", "format_version", *top["required"]]
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP text list (jedimaster export)",
        "oneOf": [{"$ref": f"#/$defs/{cls.__name__}"} for cls in LIST_CLASSES],
        "$defs": dict(sorted(defs.items())),
    }
