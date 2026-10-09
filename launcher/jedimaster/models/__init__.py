"""The models sub-package: the 3D model files (.opt), as the game has them.

Written from the game's files and notes describing how the game reads and
draws them, checked against the game's own reading and drawing.

Purpose:
    Read the model files of X-Wing vs. TIE Fighter and Balance of Power
    the way the game reads them, regroup and draw their faces the way the
    game draws them for a detail level and a switch index, give their
    textures' colors and glow, hold the game's object and craft type
    tables and find each type's model, print all of it in the answer
    sheets' format, and export binary glTF files and JSON for a page.

Flow:
    ``reader.read_opt`` -> ``OptModel`` (``body``, ``textures``,
    ``model``); ``regroup`` and ``draw`` give the faces drawn; ``colors``
    the texture colors and glow; ``tables`` and ``objects`` the object
    types' models; ``game.models_view`` reads an install's models for one
    view; ``render`` prints; ``gltf`` and ``to_json`` export; ``cli`` is
    the ``models`` command.

Invariants:
    - Standard library only.
    - The two tables of ``tables`` are this sub-package's only game data.

Call:
    ``from jedimaster.models import read_opt, draw_pass, glb_bytes``
"""

from __future__ import annotations

import logging

from .body import ModelFormatError
from .colors import Glow
from .colors import glow_of
from .colors import rgb8
from .colors import texture_rgb
from .draw import Corner
from .draw import draw_model
from .draw import draw_pass
from .draw import DrawnComponent
from .draw import DrawnFace
from .draw import DrawPass
from .draw import passes
from .game import model_names
from .game import ModelEntry
from .game import models_view
from .game import ModelsView
from .gltf import glb_bytes
from .gltf import glb_names
from .gltf import gltf_document
from .model import face_data
from .model import FaceData
from .model import FaceRecord
from .model import OptModel
from .model import OptNode
from .model import OptTexture
from .objects import craft_model
from .objects import object_model
from .objects import objects_view
from .objects import ObjectsView
from .reader import read_opt
from .regroup import regroup
from .regroup import Resolver
from .render import model_lines
from .render import render_draw
from .render import render_file
from .render import render_objects
from .tables import CRAFT_OBJECTS
from .tables import OBJECT_TYPES
from .tables import ObjectType
from .to_json import models_schema
from .to_json import models_to_json

logger = logging.getLogger(__name__)

__all__ = [
    "CRAFT_OBJECTS",
    "Corner",
    "DrawPass",
    "DrawnComponent",
    "DrawnFace",
    "FaceData",
    "FaceRecord",
    "Glow",
    "ModelEntry",
    "ModelFormatError",
    "ModelsView",
    "OBJECT_TYPES",
    "ObjectType",
    "ObjectsView",
    "OptModel",
    "OptNode",
    "OptTexture",
    "Resolver",
    "craft_model",
    "draw_model",
    "draw_pass",
    "face_data",
    "glb_bytes",
    "glb_names",
    "glow_of",
    "gltf_document",
    "model_lines",
    "model_names",
    "models_schema",
    "models_to_json",
    "models_view",
    "object_model",
    "objects_view",
    "passes",
    "read_opt",
    "regroup",
    "render_draw",
    "render_file",
    "render_objects",
    "rgb8",
    "texture_rgb",
]
