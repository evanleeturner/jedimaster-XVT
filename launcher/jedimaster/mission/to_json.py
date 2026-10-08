"""Export a ``Mission`` as plain JSON data, and describe that data as a schema.

Purpose:
    Give other programs every stored value of a mission, raw, with the
    document's name beside each value its lists name, and publish the JSON
    Schema (draft 2020-12) that the export always satisfies.

Flow:
    ``mission_to_json`` walks the model's dataclasses field by field. A field
    with ``enum`` metadata gets a sibling ``<field>_name`` (the document's
    name, or ``null`` when its list does not name the value); a field with
    ``enum_list`` metadata gets ``<field>_names``. ``mission_schema`` walks the
    same dataclasses' type hints and metadata to build the schema.

Invariants:
    - Raw values are never replaced: names are added beside them.
    - The output holds only dicts, lists, strings, integers, booleans and
      null, so ``json.dumps`` always succeeds.
    - Export and schema read the same field metadata, so a field added to the
      model appears in both.

Call:
    ``json.dumps(mission_to_json(read_mission(path)), ensure_ascii=False)``
"""

from __future__ import annotations

import dataclasses
import logging
import types
import typing
from typing import Any

from . import names
from .model import Mission

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/mission.schema.json"
JSON_FORMAT = "jedimaster.mission"
JSON_FORMAT_VERSION = 1

INT_RANGES: dict[str, tuple[int, int]] = {
    "u8": (0, 255),
    "s8": (-128, 127),
    "s16": (-32768, 32767),
}


def _name(list_name: str, value: Any) -> str | None:
    if list_name == "Designation":
        return names.designation_name(value)
    if list_name == "DesignationTeam":
        return names.designation_team_name(value)
    return names.name_of(list_name, value)


def _walk(value: Any) -> Any:
    if dataclasses.is_dataclass(value) and not isinstance(value, type):
        out: dict[str, Any] = {}
        for f in dataclasses.fields(value):
            raw = getattr(value, f.name)
            out[f.name] = _walk(raw)
            if "enum" in f.metadata:
                out[f.name + "_name"] = _name(f.metadata["enum"], raw)
            if "enum_list" in f.metadata:
                out[f.name + "_names"] = [
                    _name(f.metadata["enum_list"], v) for v in raw
                ]
        return out
    if isinstance(value, list):
        return [_walk(v) for v in value]
    if isinstance(value, dict):
        return {str(k): _walk(v) for k, v in value.items()}
    return value


def mission_to_json(mission: Mission) -> dict[str, Any]:
    """Return ``mission`` as JSON-ready data: dicts, lists and scalars.

    Always returns a dict with ``format``, ``format_version`` and one key per
    ``Mission`` field. Does not validate the values (an out-of-list value
    simply gets a ``null`` name) and does not include the file's bytes.
    """
    data = {"format": JSON_FORMAT, "format_version": JSON_FORMAT_VERSION}
    data.update(_walk(mission))
    logger.debug("exported %s flight groups", len(mission.flight_groups))
    return data


# ---- schema -------------------------------------------------------------


def _int_schema(kind: str | None) -> dict[str, Any]:
    if kind is None:
        return {"type": "integer", "minimum": 0}
    low, high = INT_RANGES[kind]
    return {"type": "integer", "minimum": low, "maximum": high}


def _str_schema(size: int | None) -> dict[str, Any]:
    schema: dict[str, Any] = {"type": "string"}
    if size is not None:
        schema["maxLength"] = size
    return schema


def _array(items: dict[str, Any], count: Any) -> dict[str, Any]:
    schema: dict[str, Any] = {"type": "array", "items": items}
    if isinstance(count, tuple):
        schema["minItems"], schema["maxItems"] = count
    elif count is not None:
        schema["minItems"] = schema["maxItems"] = count
    return schema


def _type_schema(hint: Any, meta: Any, defs: dict[str, Any]) -> dict[str, Any]:
    origin = typing.get_origin(hint)
    if hint is bool:
        return {"type": "boolean"}
    if hint is int:
        return _int_schema(meta.get("int"))
    if hint is str:
        return _str_schema(meta.get("size"))
    if dataclasses.is_dataclass(hint):
        _define(hint, defs)
        return {"$ref": f"#/$defs/{hint.__name__}"}
    if origin is dict:
        return {
            "type": "object",
            "propertyNames": {"pattern": "^0x[0-9a-f]{2}$"},
            "additionalProperties": _int_schema(meta.get("int")),
        }
    if origin is list:
        (inner,) = typing.get_args(hint)
        dims = meta.get("dims")
        if dims:
            inner_meta = dict(meta)
            if len(dims) > 1:
                inner_meta["dims"] = dims[1:]
            else:
                del inner_meta["dims"]
            return _array(_type_schema(inner, inner_meta, defs), dims[0])
        return _array(_type_schema(inner, meta, defs), meta.get("count"))
    if isinstance(hint, types.UnionType):
        raise TypeError(f"optional fields are not used by the model: {hint!r}")
    raise TypeError(f"no schema for type {hint!r}")


def _define(cls: type, defs: dict[str, Any]) -> None:
    if cls.__name__ in defs:
        return
    defs[cls.__name__] = {}  # placeholder against recursion
    hints = typing.get_type_hints(cls)
    props: dict[str, Any] = {}
    for f in dataclasses.fields(cls):
        props[f.name] = _type_schema(hints[f.name], f.metadata, defs)
        if "enum" in f.metadata:
            props[f.name + "_name"] = {"type": ["string", "null"]}
        if "enum_list" in f.metadata:
            props[f.name + "_names"] = {
                "type": "array",
                "items": {"type": ["string", "null"]},
            }
    doc = (cls.__doc__ or "").strip().splitlines()[0]
    defs[cls.__name__] = {
        "type": "object",
        "description": doc,
        "properties": props,
        "required": list(props),
        "additionalProperties": False,
    }


def mission_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``mission_to_json`` output.

    Always returns the same dict for the same model. Integer fields carry
    their storage range, fixed arrays their length, string fields their byte
    length as ``maxLength``. Does not encode cross-field rules (for example,
    one description for version 12 and three for 14; flight-group goal
    strings matching NumFGs).
    """
    defs: dict[str, Any] = {}
    _define(Mission, defs)
    root = defs.pop("Mission")
    root["properties"] = {
        "format": {"const": JSON_FORMAT},
        "format_version": {"const": JSON_FORMAT_VERSION},
        **root["properties"],
    }
    root["required"] = ["format", "format_version", *root["required"]]
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP mission (jedimaster export)",
        **root,
        "$defs": dict(sorted(defs.items())),
    }
