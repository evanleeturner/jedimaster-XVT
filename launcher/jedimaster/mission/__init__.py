"""The mission sub-package: read, render and export XvT/BoP mission files.

Purpose:
    One import point for the reader, the model, the renderer and the JSON
    export.

Flow:
    ``read_mission`` (reader) -> ``Mission`` (model) -> ``render_mission``
    (render) or ``mission_to_json`` (to_json).

Invariants:
    - Nothing here reads files except ``read_mission`` given a path.

Call:
    ``from jedimaster.mission import read_mission, render_mission``
"""

from __future__ import annotations

import logging

from .binary import MissionFormatError
from .binary import ShortReadError
from .model import Mission
from .reader import CountLimitError
from .reader import read_mission
from .reader import UnsupportedVersionError
from .render import render_mission
from .to_json import mission_to_json

logger = logging.getLogger(__name__)

__all__ = [
    "CountLimitError",
    "Mission",
    "MissionFormatError",
    "ShortReadError",
    "UnsupportedVersionError",
    "mission_to_json",
    "read_mission",
    "render_mission",
]
