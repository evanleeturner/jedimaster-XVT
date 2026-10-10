"""The JSON Schema of the control list, built from its dataclasses.

Purpose:
    Publish the JSON Schema (draft 2020-12) that every message between the
    page and the launcher satisfies, so both sides check the same document.

Flow:
    ``control_schema`` walks the dataclasses of ``protocol`` through their
    type hints: ``int``, ``str``, ``bool``, ``Literal`` (one value is a
    ``const``, several an ``enum``), ``X | None``, ``list[X]``, a union of
    dataclasses (``oneOf``) and a nested dataclass (``$ref``). A field's
    ``minimum``, ``maximum``, ``minLength`` and ``maxLength`` metadata pass
    through.

Invariants:
    - Same model, same dict.
    - Every object is closed (``additionalProperties`` false) and lists all
      its fields as required.

Call:
    ``json.dumps(control_schema(), indent=2)``
"""

from __future__ import annotations

import dataclasses
import logging
import types
import typing
from typing import Any

from .protocol import MESSAGE_CLASSES

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/control.schema.json"
LIMIT_KEYS = ("minimum", "maximum", "minLength", "maxLength")


def _literal_schema(values: tuple[Any, ...]) -> dict[str, Any]:
    if len(values) == 1:
        return {"const": values[0]}
    return {"enum": list(values)}


def _type_schema(hint: Any, defs: dict[str, Any]) -> dict[str, Any]:
    if hint is bool:
        return {"type": "boolean"}
    if hint is int:
        return {"type": "integer"}
    if hint is str:
        return {"type": "string"}
    if typing.get_origin(hint) is typing.Literal:
        return _literal_schema(typing.get_args(hint))
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
        prop = _type_schema(hints[f.name], defs)
        prop.update({k: v for k, v in f.metadata.items() if k in LIMIT_KEYS})
        props[f.name] = prop
    doc = (cls.__doc__ or "").strip().splitlines()[0]
    entry: dict[str, Any] = {
        "type": "object",
        "description": doc,
        "properties": props,
    }
    if props:
        entry["required"] = list(props)
    entry["additionalProperties"] = False
    defs[cls.__name__] = entry


def control_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of every control message.

    Always returns the same dict for the same model: one of the five
    requests, the two replies or the two pushes, each a closed object. Does
    not encode which result answers which command, or that a setting's value
    belongs to its name beyond what the argument classes say.
    """
    defs: dict[str, Any] = {}
    for cls in MESSAGE_CLASSES:
        _define(cls, defs)
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "Launcher control message",
        "oneOf": [{"$ref": f"#/$defs/{cls.__name__}"} for cls in MESSAGE_CLASSES],
        "$defs": dict(sorted(defs.items())),
    }
