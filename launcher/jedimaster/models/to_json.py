"""Describe the models' export as JSON data, and publish its schema.

Purpose:
    Give the page that shows the models one ``models.json``: per model
    file its game name, file, version, components, detail levels,
    switches, ``.glb`` files and textures (name, size, glow); per object
    type its model's game path; per craft type its object type and model;
    and the JSON Schema (draft 2020-12) the export always satisfies.

Flow:
    ``glb_stem`` names a model's ``.glb`` files; ``models_to_json``
    describes a models view and an objects view; ``models_schema`` builds
    the schema with the text export's helpers.

Invariants:
    - Every name of the view's list is in ``models``; one not loaded has
      null file, version and counts, and no ``.glb`` files or textures.
    - ``file`` is the path the game name resolved to, relative to the
      install, with forward slashes and its letter case on disk.
    - ``model`` is the spec list's line as written (null when the type has
      none); ``model_name`` is the game name of the list's model it names
      (compared lowercased), null when the list has no such name.
    - A texture ``glows`` when the glTF material of a face that names it
      has an emissive picture: its palette glows and its name does not
      start with ``_``.

Call:
    ``models_to_json(models_view(install), objects_view(install))``
"""

from __future__ import annotations

import logging
import re
from pathlib import Path
from pathlib import PurePosixPath
from typing import Any

from ..text.to_json import closed_object
from ..text.to_json import ints
from ..text.to_json import nullable
from .draw import components
from .draw import most_children
from .game import LOADED
from .game import MISSING
from .game import ModelEntry
from .game import ModelsView
from .game import NOT_LOADED
from .gltf import glb_names
from .gltf import glows
from .gltf import texture_name
from .model import FACE_GROUP
from .model import NODE_SWITCH
from .model import TEXTURE
from .objects import craft_model
from .objects import craft_object
from .objects import object_model
from .objects import ObjectsView
from .objects import PROVING_GROUNDS_FLAG
from .tables import CRAFT_OBJECTS
from .tables import OBJECT_TYPES

logger = logging.getLogger(__name__)

SCHEMA_ID = "https://jedimaster.invalid/schema/models.schema.json"
JSON_FORMAT = "jedimaster.models"
JSON_FORMAT_VERSION = 1
UNSAFE = re.compile(r"[^a-z0-9_.~-]")
GLB_PATTERN = r"^[a-z0-9_.~-]+(_s[1-9][0-9]*)?\.glb$"


def glb_stem(name: str) -> str:
    """Return the stem of a model's ``.glb`` files: its game file name's stem.

    Lowercased, letters, digits, ``_``, ``.``, ``~`` and ``-`` kept,
    anything else ``_``; ``_`` for an empty stem. Does not check that the
    stems of a list are distinct (the list's names are).
    """
    stem = PurePosixPath(name.replace("\\", "/")).stem.lower()
    return UNSAFE.sub("_", stem) or "_"


def _relative(path: Path | None, install: Path) -> str | None:
    if path is None:
        return None
    try:
        return path.relative_to(install).as_posix()
    except ValueError:
        return path.as_posix()


def _textures(entry: ModelEntry) -> list[dict[str, Any]]:
    model = entry.model
    if model is None:
        return []
    out = []
    for node in model.nodes:
        if node.type != TEXTURE or node.texture is None:
            continue
        out.append(
            {
                "node": node.number,
                "name": texture_name(model, node.number),
                "width": node.texture.width,
                "height": node.texture.height,
                "glows": glows(model, node.number),
            }
        )
    return out


def model_json(entry: ModelEntry, install: Path) -> dict[str, Any]:
    """Return one model name's JSON: game name, status, file, counts, files.

    Null file, version and counts and empty lists for a model not loaded.
    Does not draw the model (the counts come from its nodes).
    """
    model = entry.model
    if model is None:
        counts: dict[str, Any] = dict.fromkeys(
            ("version", "components", "lods", "switches")
        )
        glb: list[str] = []
    else:
        switches = most_children(model, NODE_SWITCH)
        counts = {
            "version": model.version,
            "components": sum(1 for n in components(model) if n >= 0),
            "lods": most_children(model, FACE_GROUP),
            "switches": switches,
        }
        glb = glb_names(glb_stem(entry.name), switches)
    return {
        "name": entry.name,
        "status": entry.status,
        "file": _relative(entry.path, install) if model is not None else None,
        **counts,
        "glb": glb,
        "textures": _textures(entry),
    }


def _model_name(path: str | None, names: set[str]) -> str | None:
    if path is None:
        return None
    lowered = path.lower()
    return lowered if lowered in names else None


def models_to_json(models: ModelsView, objects: ObjectsView) -> dict[str, Any]:
    """Return the export's JSON-ready data: dicts, lists and scalars.

    Always returns ``format``, ``format_version``, ``balance_of_power``,
    ``lod``, ``models`` (every name, in the list's order), ``objects``
    (types 0 to 200) and ``crafts`` (types 0 to 95). Does not write the
    ``.glb`` files or check that they exist.
    """
    names = {entry.name for entry in models.models}
    objects_out = []
    for object_type, record in enumerate(OBJECT_TYPES):
        path = object_model(objects, object_type)
        objects_out.append(
            {
                "type": object_type,
                "model": path,
                "model_name": _model_name(path, names),
                "proving_grounds": bool(record.asset_flags & PROVING_GROUNDS_FLAG),
            }
        )
    crafts_out = []
    for craft_type in range(len(CRAFT_OBJECTS)):
        path = craft_model(objects, craft_type)
        crafts_out.append(
            {
                "type": craft_type,
                "object": craft_object(craft_type),
                "model": path,
                "model_name": _model_name(path, names),
            }
        )
    out = [model_json(entry, models.install) for entry in models.models]
    logger.debug("exported %d models", len(out))
    return {
        "format": JSON_FORMAT,
        "format_version": JSON_FORMAT_VERSION,
        "balance_of_power": models.balance_of_power,
        "lod": 1,
        "models": out,
        "objects": objects_out,
        "crafts": crafts_out,
    }


def _defs() -> dict[str, Any]:
    count = nullable({"type": "integer", "minimum": 1})
    texture = closed_object(
        "One texture node: its number, name, size, and whether it glows.",
        {
            **ints("node"),
            "name": {"type": "string"},
            **ints("width", "height", minimum=1, maximum=16384),
            "glows": {"type": "boolean"},
        },
    )
    model = closed_object(
        "One model name: status, file, version, counts, .glb files, textures.",
        {
            "name": {"type": "string", "pattern": r"^ivfiles\\"},
            "status": {"enum": [LOADED, MISSING, NOT_LOADED]},
            "file": nullable({"type": "string"}),
            "version": nullable({"enum": [0, 1, 2]}),
            "components": nullable({"type": "integer", "minimum": 0}),
            "lods": count,
            "switches": count,
            "glb": {
                "type": "array",
                "items": {"type": "string", "pattern": GLB_PATTERN},
            },
            "textures": {"type": "array", "items": {"$ref": "#/$defs/Texture"}},
        },
    )
    path = nullable({"type": "string"})
    obj = closed_object(
        "One object type: its model's game path and list name, proving grounds.",
        {
            **ints("type", maximum=len(OBJECT_TYPES) - 1),
            "model": path,
            "model_name": path,
            "proving_grounds": {"type": "boolean"},
        },
    )
    craft = closed_object(
        "One craft type: its object type and that type's model.",
        {
            **ints("type", maximum=len(CRAFT_OBJECTS) - 1),
            "object": nullable({"type": "integer", "minimum": 0}),
            "model": path,
            "model_name": path,
        },
    )
    return {"Texture": texture, "Model": model, "Object": obj, "Craft": craft}


def models_schema() -> dict[str, Any]:
    """Return the JSON Schema (draft 2020-12) of ``models_to_json`` output.

    Always returns the same dict. Does not encode cross-field rules (for
    example, that a loaded model has a file and ``.glb`` files).
    """
    top = closed_object(
        "The models, objects and craft (jedimaster export).",
        {
            "format": {"const": JSON_FORMAT},
            "format_version": {"const": JSON_FORMAT_VERSION},
            "balance_of_power": {"type": "boolean"},
            "lod": {"const": 1},
            "models": {"type": "array", "items": {"$ref": "#/$defs/Model"}},
            "objects": {
                "type": "array",
                "items": {"$ref": "#/$defs/Object"},
                "minItems": len(OBJECT_TYPES),
                "maxItems": len(OBJECT_TYPES),
            },
            "crafts": {
                "type": "array",
                "items": {"$ref": "#/$defs/Craft"},
                "minItems": len(CRAFT_OBJECTS),
                "maxItems": len(CRAFT_OBJECTS),
            },
        },
    )
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "XvT/BoP 3D models (jedimaster export)",
        **top,
        "$defs": _defs(),
    }
